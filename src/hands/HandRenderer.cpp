#include "HandRenderer.h"
#include "HandSceneDepthCopy.h"
#include "HandCalibrationPolicy.h"
#include "CalibrationDraft.h"
#include "HandDispatch.h"
#include "HandHudMaskSpv.h"
#include "HandHudPlaceholderSpv.h"
#include <windows.h>
#include <wincodec.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace kharvox::hands {
namespace {

struct Mat4 { float m[16]{}; };
struct Vertex {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
    float baseColor[4]{1.f,1.f,1.f,1.f};
    // x=metallic, y=roughness, z=normal texture, w=metal/rough texture.
    float material[4]{0.f,1.f,0.f,0.f};
};
struct DecodedImage { std::uint32_t width{},height{};std::vector<std::uint8_t> rgba; };

Mat4 identity(){Mat4 r{};r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.f;return r;}
Mat4 multiply(const Mat4&a,const Mat4&b){Mat4 r{};for(int c=0;c<4;c++)for(int row=0;row<4;row++)for(int k=0;k<4;k++)r.m[c*4+row]+=a.m[k*4+row]*b.m[c*4+k];return r;}
Mat4 translation(float x,float y,float z){auto r=identity();r.m[12]=x;r.m[13]=y;r.m[14]=z;return r;}
Mat4 scale(float value){auto r=identity();r.m[0]=r.m[5]=r.m[10]=value;return r;}
Mat4 scaleAxes(float x,float y,float z){auto r=identity();r.m[0]=x;r.m[5]=y;r.m[10]=z;return r;}
Mat4 quaternion(float x,float y,float z,float w){
    Mat4 r=identity();const float xx=x*x,yy=y*y,zz=z*z,xy=x*y,xz=x*z,yz=y*z,wx=w*x,wy=w*y,wz=w*z;
    r.m[0]=1-2*(yy+zz);r.m[1]=2*(xy+wz);r.m[2]=2*(xz-wy);
    r.m[4]=2*(xy-wz);r.m[5]=1-2*(xx+zz);r.m[6]=2*(yz+wx);
    r.m[8]=2*(xz+wy);r.m[9]=2*(yz-wx);r.m[10]=1-2*(xx+yy);return r;
}
Mat4 rotationDegrees(const float degrees[3]){
    constexpr float d=0.01745329251994329577f;
    const float hx=degrees[0]*d*.5f,hy=degrees[1]*d*.5f,hz=degrees[2]*d*.5f;
    const float sx=std::sin(hx),cx=std::cos(hx),sy=std::sin(hy),cy=std::cos(hy),sz=std::sin(hz),cz=std::cos(hz);
    return quaternion(sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz);
}
Mat4 poseMatrix(const HandPose&pose){return multiply(translation(pose.position[0],pose.position[1],pose.position[2]),quaternion(pose.orientation[0],pose.orientation[1],pose.orientation[2],pose.orientation[3]));}
Mat4 inversePose(const HandPose&pose){
    const Mat4 inverseRotation=quaternion(-pose.orientation[0],-pose.orientation[1],-pose.orientation[2],pose.orientation[3]);
    return multiply(inverseRotation,translation(-pose.position[0],-pose.position[1],-pose.position[2]));
}
Mat4 projection(const HandEyeView&view){
    const float left=std::tan(view.angleLeft),right=std::tan(view.angleRight),down=std::tan(view.angleDown),up=std::tan(view.angleUp);
    const float nearZ=std::clamp(view.nearZ,.001f,10.f);
    const float farZ=std::max(view.farZ,nearZ+1.f);Mat4 r{};
    r.m[0]=2.f/(right-left);r.m[5]=2.f/(down-up);
    // Vulkan's positive viewport uses the OpenXR Vulkan convention where the
    // vertical tangent span is (down-up).  Using the opposite denominator for
    // the centre term gives the two eyes different vertical offsets.
    r.m[8]=(right+left)/(right-left);r.m[9]=(up+down)/(down-up);
    r.m[10]=-farZ/(farZ-nearZ);r.m[11]=-1.f;r.m[14]=-(farZ*nearZ)/(farZ-nearZ);if(view.nativeDepth){r.m[10]=view.depthA;r.m[14]=view.depthB;}return r;
}
std::string trim(std::string value){const auto first=value.find_first_not_of(" \t\r\n");if(first==std::string::npos)return {};const auto last=value.find_last_not_of(" \t\r\n");return value.substr(first,last-first+1);}
bool readBytes(const std::filesystem::path&path,std::vector<std::uint8_t>&bytes){std::ifstream in(path,std::ios::binary);if(!in)return false;in.seekg(0,std::ios::end);const auto size=in.tellg();if(size<=0)return false;bytes.resize(static_cast<size_t>(size));in.seekg(0);return bool(in.read(reinterpret_cast<char*>(bytes.data()),size));}

bool decodePng(const void*data,size_t size,DecodedImage&decoded){
    if(!data||!size||size>UINT_MAX)return false;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);const bool uninitialize=SUCCEEDED(com);
    IWICImagingFactory*factory{};IWICStream*stream{};IWICBitmapDecoder*decoder{};IWICBitmapFrameDecode*frame{};IWICFormatConverter*converter{};
    bool ok=false;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))
        &&SUCCEEDED(factory->CreateStream(&stream))
        &&SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<void*>(data)),static_cast<DWORD>(size)))
        &&SUCCEEDED(factory->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder))
        &&SUCCEEDED(decoder->GetFrame(0,&frame))
        &&SUCCEEDED(factory->CreateFormatConverter(&converter))
        &&SUCCEEDED(converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0.f,WICBitmapPaletteTypeCustom))){
        UINT width{},height{};
        if(SUCCEEDED(converter->GetSize(&width,&height))&&width&&height&&uint64_t(width)*height<=268435456ull){
            decoded.width=width;decoded.height=height;decoded.rgba.resize(size_t(width)*height*4);
            ok=SUCCEEDED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(decoded.rgba.size()),decoded.rgba.data()));
        }
    }
    if(converter)converter->Release();if(frame)frame->Release();if(decoder)decoder->Release();if(stream)stream->Release();if(factory)factory->Release();if(uninitialize)CoUninitialize();return ok;
}

const cgltf_accessor* attribute(const cgltf_primitive&primitive,cgltf_attribute_type type){for(cgltf_size i=0;i<primitive.attributes_count;i++)if(primitive.attributes[i].type==type&&primitive.attributes[i].index==0)return primitive.attributes[i].data;return nullptr;}
void transformPosition(const float m[16],float v[3]){const float x=v[0],y=v[1],z=v[2];v[0]=m[0]*x+m[4]*y+m[8]*z+m[12];v[1]=m[1]*x+m[5]*y+m[9]*z+m[13];v[2]=m[2]*x+m[6]*y+m[10]*z+m[14];}
void transformNormal(const float m[16],float v[3]){const float x=v[0],y=v[1],z=v[2];v[0]=m[0]*x+m[4]*y+m[8]*z;v[1]=m[1]*x+m[5]*y+m[9]*z;v[2]=m[2]*x+m[6]*y+m[10]*z;const float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(length>1e-6f){v[0]/=length;v[1]/=length;v[2]/=length;}}
bool nodeInScene(const cgltf_node*node,const cgltf_scene*scene){if(!scene)return true;const cgltf_node*root=node;while(root&&root->parent)root=root->parent;for(cgltf_size i=0;i<scene->nodes_count;i++)if(scene->nodes[i]==root)return true;return false;}
bool embeddedTextureBytes(const cgltf_texture_view&view,const void*&bytes,size_t&size){bytes=nullptr;size=0;if(!view.texture||!view.texture->image)return false;const auto*bufferView=view.texture->image->buffer_view;if(!bufferView||!bufferView->buffer||!bufferView->buffer->data)return false;bytes=static_cast<const std::uint8_t*>(bufferView->buffer->data)+bufferView->offset;size=bufferView->size;return true;}

} // namespace

struct HandRenderer::Impl {
    struct Texture {VkImage image{};VkDeviceMemory memory{};VkImageView view{};};
    struct DepthTarget {VkImage image{};VkDeviceMemory memory{};VkImageView view{};};
    struct Asset {VkBuffer vertices{};VkDeviceMemory vertexMemory{};VkBuffer indices{};VkDeviceMemory indexMemory{};std::uint32_t indexCount{};std::array<Texture,3> textures{};VkDescriptorSet descriptor{};bool ready{};std::string filename;};
    VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};std::uint32_t queueFamily{};KharvoxVulkanDispatch vk{};VkFormat format{};HandLog log;
    struct ScenePipeline {VkFormat colorFormat{VK_FORMAT_UNDEFINED};VkFormat depthFormat{VK_FORMAT_UNDEFINED};bool reverseDepth{};VkRenderPass renderPass{};VkPipeline pipeline{},hudMask{},hudPlaceholder{};};
    VkShaderModule hudMaskShader{};
    VkShaderModule hudPlaceholderShader{};
    VkCommandPool uploadPool{};VkCommandBuffer uploadCommand{};VkSampler sampler{};VkDescriptorSetLayout descriptorLayout{};VkDescriptorPool descriptorPool{};VkPipelineLayout pipelineLayout{};VkPipeline pipeline{};VkRenderPass renderPass{};VkShaderModule vertexShader{},fragmentShader{};
    std::vector<ScenePipeline> scenePipelines;std::vector<VkFramebuffer> sceneFramebuffers;
    struct SceneDepth {DepthTarget target;VkExtent2D extent{};VkFormat format{};bool initialized{};};
    std::vector<SceneDepth> sceneDepthCopies;size_t sceneDepthCopiesUsed{};
    std::array<std::vector<VkImageView>,2> eyeViews{};std::array<std::vector<DepthTarget>,2> depthTargets{};std::array<std::vector<VkFramebuffer>,2> framebuffers{};std::array<VkExtent2D,2> extents{};
    Asset laserAsset{};
    std::array<Asset,4> assets{};std::array<bool,4> mirrorX{};HandCalibration leftCalibration{},rightCalibration{};
    std::array<std::array<HandCalibration,2>,
        static_cast<size_t>(HandWeaponKind::Count)> weaponCalibrations{}, defaultWeaponCalibrations{};
    std::filesystem::path root;bool initialized{};
    HandCalibration defaultLeftCalibration{},defaultRightCalibration{};
    CalibrationMode calibrationMode{CalibrationMode::None};bool calibrateLeft{true};
    bool calibrationPlusWasDown{},calibrationHandWasDown{},calibrationResetWasDown{};
    HandWeaponKind calibrationWeapon{HandWeaponKind::Unknown};
    bool calibrationLeftHanded{},calibrationContextInitialized{};
    ULONGLONG lastCalibrationStep{};
    argent::calibration::Draft<HandCalibration> calibrationDraft;
    argent::calibration::ApplyRevision applyRevision;
    static std::string poseKey(HandWeaponKind weapon,bool modeLeft,bool physicalLeft){
        return handPoseKey(weapon,modeLeft,physicalLeft);
    }

    void say(const std::string&message)const{if(log)log("[HANDS] "+message);}
    std::uint32_t memoryType(std::uint32_t bits,VkMemoryPropertyFlags required)const{VkPhysicalDeviceMemoryProperties p{};vk.getPhysicalDeviceMemoryProperties(physical,&p);for(std::uint32_t i=0;i<p.memoryTypeCount;i++)if((bits&(1u<<i))&&(p.memoryTypes[i].propertyFlags&required)==required)return i;return UINT32_MAX;}
    bool buffer(VkDeviceSize size,VkBufferUsageFlags usage,const void*source,VkBuffer&out,VkDeviceMemory&memory){VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=size;bi.usage=usage;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;if(vk.createBuffer(device,&bi,nullptr,&out)!=VK_SUCCESS)return false;VkMemoryRequirements req{};vk.getBufferMemoryRequirements(device,out,&req);const auto type=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);if(type==UINT32_MAX){vk.destroyBuffer(device,out,nullptr);out=VK_NULL_HANDLE;return false;}VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;if(vk.allocateMemory(device,&ai,nullptr,&memory)!=VK_SUCCESS||vk.bindBufferMemory(device,out,memory,0)!=VK_SUCCESS){if(memory)vk.freeMemory(device,memory,nullptr);vk.destroyBuffer(device,out,nullptr);memory=VK_NULL_HANDLE;out=VK_NULL_HANDLE;return false;}void*mapped{};if(vk.mapMemory(device,memory,0,size,0,&mapped)!=VK_SUCCESS){vk.freeMemory(device,memory,nullptr);vk.destroyBuffer(device,out,nullptr);memory=VK_NULL_HANDLE;out=VK_NULL_HANDLE;return false;}std::memcpy(mapped,source,static_cast<size_t>(size));vk.unmapMemory(device,memory);return true;}
    bool texture(const DecodedImage&decoded,Texture&out,bool srgb);
    bool depthTarget(VkExtent2D extent,DepthTarget&out,VkFormat depthFormat=VK_FORMAT_D32_SFLOAT,bool transfer=false);
    void destroyDepth(DepthTarget& target){
        if(target.view)vk.destroyImageView(device,target.view,nullptr);
        if(target.image)vk.destroyImage(device,target.image,nullptr);
        if(target.memory)vk.freeMemory(device,target.memory,nullptr);
        target={};
    }
    bool loadAsset(const std::filesystem::path&path,Asset&asset);
    bool createCommonResources();
    bool createGraphicsPipeline(VkRenderPass compatibleRenderPass,
        VkCompareOp depthCompare,bool encodeSrgb,VkPipeline&out,bool hudMask=false,bool placeholder=false);
    ScenePipeline* scenePipeline(const HandSceneTarget&target);
    void drawGeometry(VkCommandBuffer commandBuffer,VkPipeline selectedPipeline,
        VkExtent2D extent,const HandEyeView&view,const HandPose&leftGrip,
        const HandPose&rightGrip,const HandVisibilityOutput&visibility,
        const HandGameplayState&gameplay,const HandPose&laser = {});
    void destroyAsset(Asset&asset);
    HandCalibration& selectedCalibration();
    const HandCalibration& selectedCalibration()const;
    const HandCalibration& renderCalibration(bool left,HandModelKind kind,
        HandWeaponKind weapon,bool leftHanded)const;
    void loadCalibrationFile(const wchar_t* filename, bool defaults);
    bool saveCalibration();
    void writeCalibrationStatus(const char*message=nullptr)const;
    void pollCalibration(const HandGameplayState&gameplay);
};

bool HandRenderer::Impl::texture(const DecodedImage&decoded,Texture&out,bool srgb){
    if(!decoded.width||!decoded.height||decoded.rgba.empty())return false;
    VkBuffer staging{};VkDeviceMemory stagingMemory{};
    if(!buffer(decoded.rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            decoded.rgba.data(),staging,stagingMemory))return false;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType=VK_IMAGE_TYPE_2D;imageInfo.format=srgb
        ?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent={decoded.width,decoded.height,1};imageInfo.mipLevels=1;
    imageInfo.arrayLayers=1;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    bool ok=vk.createImage(device,&imageInfo,nullptr,&out.image)==VK_SUCCESS;
    VkMemoryRequirements requirements{};
    if(ok){vk.getImageMemoryRequirements(device,out.image,&requirements);const auto type=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);if(type==UINT32_MAX)ok=false;else{VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;ok=vk.allocateMemory(device,&allocation,nullptr,&out.memory)==VK_SUCCESS&&vk.bindImageMemory(device,out.image,out.memory,0)==VK_SUCCESS;}}
    if(ok){
        vk.resetCommandBuffer(uploadCommand,0);VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;ok=vk.beginCommandBuffer(uploadCommand,&begin)==VK_SUCCESS;
        if(ok){VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.image=out.image;barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;barrier.subresourceRange.levelCount=1;barrier.subresourceRange.layerCount=1;vk.cmdPipelineBarrier(uploadCommand,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);VkBufferImageCopy copy{};copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;copy.imageExtent={decoded.width,decoded.height,1};vk.cmdCopyBufferToImage(uploadCommand,staging,out.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;vk.cmdPipelineBarrier(uploadCommand,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);ok=vk.endCommandBuffer(uploadCommand)==VK_SUCCESS;}
        if(ok){VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&uploadCommand;ok=vk.queueSubmit(queue,1,&submit,VK_NULL_HANDLE)==VK_SUCCESS&&vk.queueWaitIdle(queue)==VK_SUCCESS;}
    }
    if(staging)vk.destroyBuffer(device,staging,nullptr);if(stagingMemory)vk.freeMemory(device,stagingMemory,nullptr);
    if(ok){VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=out.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=srgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;vi.subresourceRange.levelCount=1;vi.subresourceRange.layerCount=1;ok=vk.createImageView(device,&vi,nullptr,&out.view)==VK_SUCCESS;}
    return ok;
}

bool HandRenderer::Impl::depthTarget(VkExtent2D extent,DepthTarget&out,VkFormat depthFormat,bool transfer){
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType=VK_IMAGE_TYPE_2D;imageInfo.format=depthFormat;
    imageInfo.extent={extent.width,extent.height,1};imageInfo.mipLevels=1;
    imageInfo.arrayLayers=1;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(transfer)imageInfo.usage|=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if(vk.createImage(device,&imageInfo,nullptr,&out.image)!=VK_SUCCESS)return false;
    VkMemoryRequirements requirements{};vk.getImageMemoryRequirements(device,out.image,&requirements);
    const auto type=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(type==UINT32_MAX)return false;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
    if(vk.allocateMemory(device,&allocation,nullptr,&out.memory)!=VK_SUCCESS
        ||vk.bindImageMemory(device,out.image,out.memory,0)!=VK_SUCCESS)return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=out.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.format=depthFormat;
    view.subresourceRange.aspectMask=handSceneDepthAspect(depthFormat);
    view.subresourceRange.levelCount=1;view.subresourceRange.layerCount=1;
    return vk.createImageView(device,&view,nullptr,&out.view)==VK_SUCCESS;
}

bool HandRenderer::Impl::loadAsset(const std::filesystem::path&path,Asset&asset){
    asset.filename=path.filename().u8string();cgltf_options options{};cgltf_data*data{};
    const auto utf8=path.u8string();if(cgltf_parse_file(&options,utf8.c_str(),&data)!=cgltf_result_success){say("cannot parse "+asset.filename+"; model disabled");return false;}
    const auto freeData=[&]{cgltf_free(data);};
    if(cgltf_load_buffers(&options,data,utf8.c_str())!=cgltf_result_success){say("cannot load GLB buffers for "+asset.filename+"; model disabled");freeData();return false;}
    // The supplied files contain two posed scenes in one GLB. Respect the
    // file's default scene instead of blindly selecting the first mesh node;
    // the Fist and Gun files intentionally select different scenes.
    const cgltf_node*node{};const cgltf_primitive*primitive{};
    for(cgltf_size n=0;n<data->nodes_count&&!primitive;n++)
        if(data->nodes[n].mesh&&data->nodes[n].mesh->primitives_count
            &&nodeInScene(&data->nodes[n],data->scene)){
            node=&data->nodes[n];primitive=&data->nodes[n].mesh->primitives[0];
        }
    if(!primitive||primitive->type!=cgltf_primitive_type_triangles||!primitive->indices){say(asset.filename+" must contain one indexed triangle primitive");freeData();return false;}
    const auto*positions=attribute(*primitive,cgltf_attribute_type_position);const auto*normals=attribute(*primitive,cgltf_attribute_type_normal);const auto*uvs=attribute(*primitive,cgltf_attribute_type_texcoord);const auto*joints=attribute(*primitive,cgltf_attribute_type_joints);const auto*weights=attribute(*primitive,cgltf_attribute_type_weights);
    if(!positions||!normals||!uvs||positions->count!=normals->count||positions->count!=uvs->count){say(asset.filename+" requires POSITION, NORMAL and TEXCOORD_0");freeData();return false;}
    cgltf_float nodeTransform[16];cgltf_node_transform_world(node,nodeTransform);
    const bool skinned=node->skin&&joints&&weights
        &&joints->count==positions->count&&weights->count==positions->count
        &&node->skin->inverse_bind_matrices
        &&node->skin->inverse_bind_matrices->count>=node->skin->joints_count;
    std::vector<Mat4>jointTransforms;
    if(skinned){
        jointTransforms.resize(node->skin->joints_count);
        for(cgltf_size joint=0;joint<node->skin->joints_count;joint++){
            cgltf_float world[16]{};cgltf_float inverseBind[16]{};
            cgltf_node_transform_world(node->skin->joints[joint],world);
            if(!cgltf_accessor_read_float(node->skin->inverse_bind_matrices,
                    joint,inverseBind,16)){
                say(asset.filename+" has unreadable inverse bind matrices");
                freeData();return false;
            }
            Mat4 worldMatrix{},inverseBindMatrix{};
            std::copy_n(world,16,worldMatrix.m);
            std::copy_n(inverseBind,16,inverseBindMatrix.m);
            jointTransforms[joint]=multiply(worldMatrix,inverseBindMatrix);
        }
    }
    const auto*material=primitive->material;float baseFactor[4]{1,1,1,1};float metallic=1.f,roughness=1.f;bool hasNormal=false,hasMr=false;
    if(material&&material->has_pbr_metallic_roughness){for(int i=0;i<4;i++)baseFactor[i]=material->pbr_metallic_roughness.base_color_factor[i];metallic=material->pbr_metallic_roughness.metallic_factor;roughness=material->pbr_metallic_roughness.roughness_factor;hasNormal=material->normal_texture.texture!=nullptr;hasMr=material->pbr_metallic_roughness.metallic_roughness_texture.texture!=nullptr;}
    // Controller hands are opaque geometry. Blender's exported base-color PNG
    // may retain unused transparent texels even though glTF alphaMode is
    // OPAQUE; letting those texels reach our blend pipeline made valid surface
    // fragments look partially transparent.
    baseFactor[3]=1.f;
    std::vector<Vertex>vertices(positions->count);for(cgltf_size i=0;i<positions->count;i++){auto&v=vertices[i];cgltf_accessor_read_float(positions,i,v.position,3);cgltf_accessor_read_float(normals,i,v.normal,3);cgltf_accessor_read_float(uvs,i,v.uv,2);if(skinned){cgltf_uint jointIndices[4]{};cgltf_float jointWeights[4]{};if(!cgltf_accessor_read_uint(joints,i,jointIndices,4)||!cgltf_accessor_read_float(weights,i,jointWeights,4)){say(asset.filename+" has unreadable skin weights");freeData();return false;}const std::array<float,3>sourcePosition{{v.position[0],v.position[1],v.position[2]}};const std::array<float,3>sourceNormal{{v.normal[0],v.normal[1],v.normal[2]}};v.position[0]=v.position[1]=v.position[2]=0.f;v.normal[0]=v.normal[1]=v.normal[2]=0.f;float totalWeight=0.f;for(size_t influence=0;influence<4;influence++){const float weight=jointWeights[influence];const auto joint=jointIndices[influence];if(weight<=0.f||joint>=jointTransforms.size())continue;float posedPosition[3]{sourcePosition[0],sourcePosition[1],sourcePosition[2]};float posedNormal[3]{sourceNormal[0],sourceNormal[1],sourceNormal[2]};transformPosition(jointTransforms[joint].m,posedPosition);transformNormal(jointTransforms[joint].m,posedNormal);for(int component=0;component<3;component++){v.position[component]+=posedPosition[component]*weight;v.normal[component]+=posedNormal[component]*weight;}totalWeight+=weight;}if(totalWeight<=.0001f){v.position[0]=sourcePosition[0];v.position[1]=sourcePosition[1];v.position[2]=sourcePosition[2];v.normal[0]=sourceNormal[0];v.normal[1]=sourceNormal[1];v.normal[2]=sourceNormal[2];transformPosition(nodeTransform,v.position);transformNormal(nodeTransform,v.normal);}else{const float inverseWeight=1.f/totalWeight;for(int component=0;component<3;component++){v.position[component]*=inverseWeight;v.normal[component]*=inverseWeight;}const float normalLength=std::sqrt(v.normal[0]*v.normal[0]+v.normal[1]*v.normal[1]+v.normal[2]*v.normal[2]);if(normalLength>.000001f){v.normal[0]/=normalLength;v.normal[1]/=normalLength;v.normal[2]/=normalLength;}}}else{transformPosition(nodeTransform,v.position);transformNormal(nodeTransform,v.normal);}for(int j=0;j<4;j++)v.baseColor[j]=baseFactor[j];v.material[0]=metallic;v.material[1]=roughness;v.material[2]=hasNormal?1.f:0.f;v.material[3]=hasMr?1.f:0.f;}
    std::vector<std::uint32_t>indices(primitive->indices->count);for(cgltf_size i=0;i<primitive->indices->count;i++)indices[i]=static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive->indices,i));
    bool ok=buffer(vertices.size()*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices.data(),asset.vertices,asset.vertexMemory)&&buffer(indices.size()*sizeof(std::uint32_t),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,indices.data(),asset.indices,asset.indexMemory);asset.indexCount=static_cast<std::uint32_t>(indices.size());
    std::array<DecodedImage,3>decoded{};decoded[0]={1,1,{255,255,255,255}};decoded[1]={1,1,{128,128,255,255}};decoded[2]={1,1,{255,255,255,255}};
    auto decodeView=[&](const cgltf_texture_view&view,DecodedImage&target,const char*label){const void*bytes{};size_t size{};if(!embeddedTextureBytes(view,bytes,size)||!decodePng(bytes,size,target)){say(asset.filename+" "+label+" texture unavailable; using neutral fallback");return false;}return true;};
    if(material&&material->has_pbr_metallic_roughness){decodeView(material->pbr_metallic_roughness.base_color_texture,decoded[0],"base-color");if(hasMr)decodeView(material->pbr_metallic_roughness.metallic_roughness_texture,decoded[2],"metallic/roughness");if(hasNormal)decodeView(material->normal_texture,decoded[1],"normal");}
    for(size_t alpha=3;alpha<decoded[0].rgba.size();alpha+=4)
        decoded[0].rgba[alpha]=255;
    for(size_t i=0;i<asset.textures.size()&&ok;i++)
        ok=texture(decoded[i],asset.textures[i],i==0);
    if(ok){VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=descriptorPool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&descriptorLayout;ok=vk.allocateDescriptorSets(device,&allocation,&asset.descriptor)==VK_SUCCESS;}
    if(ok){std::array<VkDescriptorImageInfo,3>infos{};std::array<VkWriteDescriptorSet,3>writes{};for(std::uint32_t i=0;i<3;i++){infos[i].sampler=sampler;infos[i].imageView=asset.textures[i].view;infos[i].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=asset.descriptor;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&infos[i];}vk.updateDescriptorSets(device,3,writes.data(),0,nullptr);}
    freeData();asset.ready=ok;if(ok)say("loaded "+asset.filename+" vertices="+std::to_string(vertices.size())+" indices="+std::to_string(indices.size())+(skinned?" static-pose=baked":"")+" PBR=base-color"+(hasNormal?"+normal":"")+(hasMr?"+metallic-roughness":""));else say("GPU resources failed for "+asset.filename+"; model disabled");return ok;
}

bool HandRenderer::Impl::createGraphicsPipeline(
    VkRenderPass compatibleRenderPass,VkCompareOp depthCompare,bool encodeSrgb,
    VkPipeline&out,bool hudMask,bool placeholder){
    if(!vertexShader||!fragmentShader||!pipelineLayout||!compatibleRenderPass)
        return false;
    std::array<VkPipelineShaderStageCreateInfo,2>stages{};
    stages[0]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertexShader;
    stages[0].pName="main";
    stages[1]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragmentShader;
    stages[1].pName="main";
    const std::uint32_t outputTransfer=encodeSrgb?1u:0u;
    const VkSpecializationMapEntry transferEntry{0,0,sizeof(outputTransfer)};
    const VkSpecializationInfo transferInfo{1,&transferEntry,
        sizeof(outputTransfer),&outputTransfer};
    stages[1].pSpecializationInfo=&transferInfo;
    VkVertexInputBindingDescription vertexBinding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription,5>attributes{{
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,position)},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,normal)},
        {2,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,uv)},
        {3,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,baseColor)},
        {4,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,material)}}};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount=1;vertexInput.pVertexBindingDescriptions=&vertexBinding;
    vertexInput.vertexAttributeDescriptionCount=static_cast<std::uint32_t>(attributes.size());vertexInput.pVertexAttributeDescriptions=attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=1;viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1.f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depthState.depthTestEnable=VK_TRUE;depthState.depthWriteEnable=VK_TRUE;depthState.depthCompareOp=depthCompare;
    VkPipelineColorBlendAttachmentState blend{};blend.blendEnable=VK_FALSE;blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blendState.attachmentCount=1;blendState.pAttachments=&blend;
    std::array<VkDynamicState,2>dynamicStates{{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR}};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=static_cast<std::uint32_t>(dynamicStates.size());dynamic.pDynamicStates=dynamicStates.data();
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.stageCount=static_cast<std::uint32_t>(stages.size());info.pStages=stages.data();info.pVertexInputState=&vertexInput;info.pInputAssemblyState=&assembly;info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pMultisampleState=&multisample;info.pDepthStencilState=&depthState;info.pColorBlendState=&blendState;info.pDynamicState=&dynamic;info.layout=pipelineLayout;info.renderPass=compatibleRenderPass;
    if(hudMask){
        stages[0].module=hudMaskShader;info.stageCount=1;
        vertexInput.vertexBindingDescriptionCount=vertexInput.vertexAttributeDescriptionCount=0;
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;blend.colorWriteMask=0;
        if(placeholder){
            info.stageCount=2;stages[1].module=hudPlaceholderShader;stages[1].pSpecializationInfo=nullptr;
            blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        }
    }
    return vk.createGraphicsPipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&out)==VK_SUCCESS;
}

bool HandRenderer::Impl::createCommonResources(){
    if(!vk.cmdDraw&&vk.getDeviceProcAddr)vk.cmdDraw=reinterpret_cast<PFN_vkCmdDraw>(vk.getDeviceProcAddr(device,"vkCmdDraw"));
    if(!vk.createCommandPool||!vk.allocateCommandBuffers||!vk.createBuffer
        ||!vk.getBufferMemoryRequirements||!vk.bindBufferMemory||!vk.mapMemory
        ||!vk.unmapMemory||!vk.cmdCopyBufferToImage||!vk.createRenderPass
        ||!vk.createGraphicsPipelines||!vk.cmdBeginRenderPass
        ||!vk.cmdDrawIndexed||!vk.cmdDraw){say("required Vulkan functions unavailable; renderer disabled");return false;}
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;poolInfo.queueFamilyIndex=queueFamily;if(vk.createCommandPool(device,&poolInfo,nullptr,&uploadPool)!=VK_SUCCESS)return false;
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commandInfo.commandPool=uploadPool;commandInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commandInfo.commandBufferCount=1;if(vk.allocateCommandBuffers(device,&commandInfo,&uploadCommand)!=VK_SUCCESS)return false;
    if(vk.setDeviceLoaderData&&vk.setDeviceLoaderData(device,uploadCommand)!=VK_SUCCESS)return false;
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};samplerInfo.magFilter=VK_FILTER_LINEAR;samplerInfo.minFilter=VK_FILTER_LINEAR;samplerInfo.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;samplerInfo.addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;samplerInfo.addressModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT;samplerInfo.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;samplerInfo.maxLod=0.f;if(vk.createSampler(device,&samplerInfo,nullptr,&sampler)!=VK_SUCCESS)return false;
    std::array<VkDescriptorSetLayoutBinding,3>bindings{};for(std::uint32_t i=0;i<bindings.size();i++){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;}VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layoutInfo.bindingCount=static_cast<std::uint32_t>(bindings.size());layoutInfo.pBindings=bindings.data();if(vk.createDescriptorSetLayout(device,&layoutInfo,nullptr,&descriptorLayout)!=VK_SUCCESS)return false;
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,12};VkDescriptorPoolCreateInfo descriptorPoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};descriptorPoolInfo.maxSets=4;descriptorPoolInfo.poolSizeCount=1;descriptorPoolInfo.pPoolSizes=&poolSize;if(vk.createDescriptorPool(device,&descriptorPoolInfo,nullptr,&descriptorPool)!=VK_SUCCESS)return false;
    VkAttachmentDescription color{};
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription depth{};
    depth.format=VK_FORMAT_D32_SFLOAT;
    depth.samples=VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorReference{
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference{
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    VkRenderPassCreateInfo renderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    const std::array<VkAttachmentDescription,2> attachments{{color,depth}};
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (vk.createRenderPass(device, &renderPassInfo, nullptr, &renderPass) !=
        VK_SUCCESS)
      return false;
    std::vector<std::uint8_t>vertexSpv,fragmentSpv;if(!readBytes(root/L"HandPbr.vert.spv",vertexSpv)||!readBytes(root/L"HandPbr.frag.spv",fragmentSpv)||vertexSpv.size()%4||fragmentSpv.size()%4){say("hand shader binaries missing or invalid; renderer disabled");return false;}VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shaderInfo.codeSize=vertexSpv.size();shaderInfo.pCode=reinterpret_cast<const std::uint32_t*>(vertexSpv.data());if(vk.createShaderModule(device,&shaderInfo,nullptr,&vertexShader)!=VK_SUCCESS)return false;shaderInfo.codeSize=fragmentSpv.size();shaderInfo.pCode=reinterpret_cast<const std::uint32_t*>(fragmentSpv.data());if(vk.createShaderModule(device,&shaderInfo,nullptr,&fragmentShader)!=VK_SUCCESS){vk.destroyShaderModule(device,vertexShader,nullptr);vertexShader=VK_NULL_HANDLE;return false;}
    VkPushConstantRange push{};push.stageFlags=VK_SHADER_STAGE_VERTEX_BIT;push.offset=0;push.size=sizeof(Mat4)*2;VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pipelineLayoutInfo.setLayoutCount=1;pipelineLayoutInfo.pSetLayouts=&descriptorLayout;pipelineLayoutInfo.pushConstantRangeCount=1;pipelineLayoutInfo.pPushConstantRanges=&push;bool ok=vk.createPipelineLayout(device,&pipelineLayoutInfo,nullptr,&pipelineLayout)==VK_SUCCESS;
    shaderInfo.codeSize=sizeof(argentHandHudMask);shaderInfo.pCode=argentHandHudMask;
    if(ok)ok=vk.createShaderModule(device,&shaderInfo,nullptr,&hudMaskShader)==VK_SUCCESS;
    shaderInfo.codeSize=sizeof(argentHandHudPlaceholder);shaderInfo.pCode=argentHandHudPlaceholder;
    if(ok)ok=vk.createShaderModule(device,&shaderInfo,nullptr,&hudPlaceholderShader)==VK_SUCCESS;
    if(ok)ok=createGraphicsPipeline(renderPass,VK_COMPARE_OP_LESS_OR_EQUAL,
        format==VK_FORMAT_R8G8B8A8_UNORM||format==VK_FORMAT_B8G8R8A8_UNORM,pipeline);
    return ok;
}

HandRenderer::Impl::ScenePipeline* HandRenderer::Impl::scenePipeline(
    const HandSceneTarget&target){
    for(auto&candidate:scenePipelines)
        if(candidate.colorFormat==target.colorFormat
            &&candidate.depthFormat==target.depthFormat
            &&candidate.reverseDepth==target.reverseDepth)return &candidate;
    if(target.samples!=VK_SAMPLE_COUNT_1_BIT)return nullptr;
    ScenePipeline candidate{};candidate.colorFormat=target.colorFormat;
    candidate.depthFormat=target.depthFormat;candidate.reverseDepth=target.reverseDepth;
    VkAttachmentDescription color{};color.format=target.colorFormat;
    color.samples=VK_SAMPLE_COUNT_1_BIT;color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription depth{};depth.format=target.depthFormat;
    depth.samples=target.samples;depth.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const std::array<VkAttachmentDescription,2>attachments{{color,depth}};
    VkAttachmentReference colorReference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1;subpass.pColorAttachments=&colorReference;
    subpass.pDepthStencilAttachment=&depthReference;
    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount=static_cast<std::uint32_t>(attachments.size());
    renderPassInfo.pAttachments=attachments.data();renderPassInfo.subpassCount=1;
    renderPassInfo.pSubpasses=&subpass;
    if(vk.createRenderPass(device,&renderPassInfo,nullptr,&candidate.renderPass)!=VK_SUCCESS)
        return nullptr;
    const VkCompareOp compare=target.reverseDepth?VK_COMPARE_OP_GREATER_OR_EQUAL
                                                 :VK_COMPARE_OP_LESS_OR_EQUAL;
    // DOOM's final WSI image is UNORM even though it contains display-encoded
    // pixels. The hand shader normally writes linear values to an sRGB OpenXR
    // target, so encode explicitly only when drawing into this native image.
    const bool encodeSrgb=target.colorFormat==VK_FORMAT_R8G8B8A8_UNORM
        ||target.colorFormat==VK_FORMAT_B8G8R8A8_UNORM
        ||target.colorFormat==VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    if(!createGraphicsPipeline(candidate.renderPass,compare,encodeSrgb,
            candidate.pipeline)){
        vk.destroyRenderPass(device,candidate.renderPass,nullptr);return nullptr;
    }
    if(!createGraphicsPipeline(candidate.renderPass,compare,false,candidate.hudMask,true)){
        vk.destroyPipeline(device,candidate.pipeline,nullptr);vk.destroyRenderPass(device,candidate.renderPass,nullptr);return nullptr;
    }
    if(!createGraphicsPipeline(candidate.renderPass,compare,false,candidate.hudPlaceholder,true,true)){
        vk.destroyPipeline(device,candidate.hudMask,nullptr);vk.destroyPipeline(device,candidate.pipeline,nullptr);
        vk.destroyRenderPass(device,candidate.renderPass,nullptr);return nullptr;
    }
    scenePipelines.push_back(candidate);
    say(std::string("DOOM scene-depth integration ready colorFormat=")
        +std::to_string(target.colorFormat)+" depthFormat="
        +std::to_string(target.depthFormat)+(target.reverseDepth?" reverse-Z":" forward-Z")
        +(encodeSrgb?" manual-sRGB":" native-sRGB"));
    return &scenePipelines.back();
}

void HandRenderer::Impl::drawGeometry(VkCommandBuffer commandBuffer,
    VkPipeline selectedPipeline,VkExtent2D extent,const HandEyeView&view,
    const HandPose&leftGrip,const HandPose&rightGrip,
    const HandVisibilityOutput&visibility,const HandGameplayState&gameplay,const HandPose&laser){
    const bool drawLeft=visibility.left!=HandModelKind::None&&leftGrip.valid;
    const bool drawRight=visibility.right!=HandModelKind::None&&rightGrip.valid;
    if(!drawLeft&&!drawRight&&!laser.valid&&view.hudPlaceholder.empty())return;
    const int32_t rectX=std::clamp(view.imageRectX,0,int32_t(extent.width));
    const int32_t rectY=std::clamp(view.imageRectY,0,int32_t(extent.height));
    const uint32_t requestedWidth=view.imageRectWidth?view.imageRectWidth:extent.width;
    const uint32_t requestedHeight=view.imageRectHeight?view.imageRectHeight:extent.height;
    const uint32_t rectWidth=std::min(requestedWidth,extent.width-uint32_t(rectX));
    const uint32_t rectHeight=std::min(requestedHeight,extent.height-uint32_t(rectY));
    if(!rectWidth||!rectHeight)return;
    VkRect2D scissor{{rectX,rectY},{rectWidth,rectHeight}};
    vk.cmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,selectedPipeline);
    VkViewport viewport{float(rectX),float(rectY),float(rectWidth),float(rectHeight),0,1};
    vk.cmdSetViewport(commandBuffer,0,1,&viewport);vk.cmdSetScissor(commandBuffer,0,1,&scissor);
    Mat4 viewProjection=multiply(projection(view),inversePose(view.pose));
    if(!view.hudPanels.empty()||!view.hudPlaceholder.empty()){
        const auto scene=std::find_if(scenePipelines.begin(),scenePipelines.end(),[&](const auto& p){return p.pipeline==selectedPipeline;});
        if(scene!=scenePipelines.end()){
            auto drawPanels=[&](const HandHudPanels& panels,VkPipeline p){
            vk.cmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,p);
            for(const auto& panel:panels){
                float clip[4][4]{};bool valid=true;
                for(int i=0;i<4;++i){
                    for(int r=0;r<4;++r){clip[i][r]=viewProjection.m[12+r];for(int k=0;k<3;++k)clip[i][r]+=viewProjection.m[k*4+r]*panel[i][k];}
                    for(float v:clip[i])valid=valid&&std::isfinite(v);
                    // Real panel depth, in the same convention as the hands.
                    // A nearer glove must be able to occlude the native HUD.
                    if(scene->reverseDepth!=(view.nativeDepth&&view.depthB>0.f))
                        clip[i][2]=clip[i][3]-clip[i][2];
                }
                if(!valid)continue;
                vk.cmdPushConstants(commandBuffer,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(clip),clip);
                vk.cmdDraw(commandBuffer,4,1,0,0);
            }
            };
            drawPanels(view.hudPanels,scene->hudMask);
            drawPanels(view.hudPlaceholder,scene->hudPlaceholder);
            vk.cmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,selectedPipeline);
        }
    }
    // DOOM uses reverse-Z on its main scene target. Map the hand projection's
    // [near=0, far=1] clip depth to [near=1, far=0] before the shared test.
    const bool reverse=selectedPipeline!=pipeline&&std::any_of(scenePipelines.begin(),scenePipelines.end(),[&](const ScenePipeline&candidate){return candidate.pipeline==selectedPipeline&&candidate.reverseDepth;});
    // Eternal's captured common-buffer projection is forward-Z even when the
    // scene attachment is reverse-Z. Native coefficients do not imply that
    // their convention already matches the depth compare (the old shortcut
    // made rear hand surfaces win and put the hands in front of the world).
    const bool projectionReverse=view.nativeDepth&&view.depthB>0.f;
    if(reverse!=projectionReverse)for(int column=0;column<4;column++)
        viewProjection.m[column*4+2]=viewProjection.m[column*4+3]-viewProjection.m[column*4+2];
    auto draw=[&](bool left,HandModelKind kind,const HandPose&pose){
        const size_t index=left?(kind==HandModelKind::GunHolding?2:0)
                               :(kind==HandModelKind::GunHolding?3:1);
        auto&asset=assets[index];if(!asset.ready)return;
        const auto&calibration=renderCalibration(left,kind,gameplay.weapon,
            gameplay.leftHanded);
        const float xScale=mirrorX[index]?-calibration.scale:calibration.scale;
        const Mat4 local=multiply(translation(calibration.position[0],calibration.position[1],calibration.position[2]),multiply(rotationDegrees(calibration.rotationDegrees),scaleAxes(xScale,calibration.scale,calibration.scale)));
        struct Push{Mat4 model;Mat4 viewProjection;}push{multiply(poseMatrix(pose),local),viewProjection};
        vk.cmdPushConstants(commandBuffer,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
        vk.cmdBindDescriptorSets(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&asset.descriptor,0,nullptr);
        VkDeviceSize offset{};vk.cmdBindVertexBuffers(commandBuffer,0,1,&asset.vertices,&offset);
        vk.cmdBindIndexBuffer(commandBuffer,asset.indices,0,VK_INDEX_TYPE_UINT32);
        vk.cmdDrawIndexed(commandBuffer,asset.indexCount,1,0,0,0);
    };
    if(drawLeft)draw(true,visibility.left,leftGrip);
    if(drawRight)draw(false,visibility.right,rightGrip);
    if(laser.valid&&laserAsset.ready){
        const auto descriptor=std::find_if(assets.begin(),assets.end(),[](const Asset& a){return a.ready;});
        if(descriptor!=assets.end()){
            struct Push{Mat4 model;Mat4 viewProjection;}push{poseMatrix(laser),viewProjection};
            vk.cmdPushConstants(commandBuffer,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
            vk.cmdBindDescriptorSets(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&descriptor->descriptor,0,nullptr);
            VkDeviceSize offset{};vk.cmdBindVertexBuffers(commandBuffer,0,1,&laserAsset.vertices,&offset);
            vk.cmdBindIndexBuffer(commandBuffer,laserAsset.indices,0,VK_INDEX_TYPE_UINT32);
            vk.cmdDrawIndexed(commandBuffer,laserAsset.indexCount,1,0,0,0);
        }
    }
}

void HandRenderer::Impl::destroyAsset(Asset&asset){for(auto&texture:asset.textures){if(texture.view)vk.destroyImageView(device,texture.view,nullptr);if(texture.image)vk.destroyImage(device,texture.image,nullptr);if(texture.memory)vk.freeMemory(device,texture.memory,nullptr);}if(asset.indices)vk.destroyBuffer(device,asset.indices,nullptr);if(asset.indexMemory)vk.freeMemory(device,asset.indexMemory,nullptr);if(asset.vertices)vk.destroyBuffer(device,asset.vertices,nullptr);if(asset.vertexMemory)vk.freeMemory(device,asset.vertexMemory,nullptr);asset={};}

const HandCalibration&HandRenderer::Impl::renderCalibration(bool left,
    HandModelKind kind,HandWeaponKind weapon,bool leftHanded)const{
    if(auto value=calibrationDraft.find(poseKey(weapon,leftHanded,left)))return *value;
    if(auto value=calibrationDraft.find(poseKey(HandWeaponKind::Unknown,leftHanded,left)))return *value;
    const auto selection=selectHandCalibrationProfile(left,leftHanded,weapon,
        kind==HandModelKind::GunHolding);
    if(selection.weaponSpecific)
        return weaponCalibrations[selection.weaponIndex][selection.handIndex];
    return left?leftCalibration:rightCalibration;
}

HandCalibration&HandRenderer::Impl::selectedCalibration(){
    const auto initial=renderCalibration(calibrateLeft,HandModelKind::GunHolding,calibrationWeapon,calibrationLeftHanded);
    return calibrationDraft.edit(poseKey(calibrationWeapon,calibrationLeftHanded,calibrateLeft),initial);
}

const HandCalibration&HandRenderer::Impl::selectedCalibration()const{
    return renderCalibration(calibrateLeft,HandModelKind::GunHolding,calibrationWeapon,calibrationLeftHanded);
}

void HandRenderer::Impl::loadCalibrationFile(const wchar_t* filename, bool defaults){
    std::ifstream input(root/filename);
    // Repository defaults are the baseline. A legacy v1 save deliberately
    // uses its global wrists for every weapon; partial v2 saves override only
    // their named profiles, preserving all other accepted defaults.
    auto resetWeaponProfiles=[&]{
        for(auto&profile:weaponCalibrations){profile[0]=leftCalibration;
            profile[1]=rightCalibration;}
    };
    if(!input){if(defaults){resetWeaponProfiles();say("hand calibration defaults missing; using hand_models.cfg wrists");}return;}
    std::unordered_map<std::string,std::string>values;std::string line;
    while(std::getline(input,line)){line=trim(line);if(line.empty()||line[0]=='#')continue;const auto equals=line.find('=');if(equals!=std::string::npos)values[trim(line.substr(0,equals))]=trim(line.substr(equals+1));}
    auto vector3=[&](const std::string&key,float value[3]){const auto found=values.find(key);if(found==values.end())return false;std::istringstream stream(found->second);float parsed[3]{};if(!(stream>>parsed[0]>>parsed[1]>>parsed[2])||!std::isfinite(parsed[0])||!std::isfinite(parsed[1])||!std::isfinite(parsed[2]))return false;std::copy_n(parsed,3,value);return true;};
    const bool leftPosition=vector3("left_position",leftCalibration.position);
    const bool leftRotation=vector3("left_rotation",leftCalibration.rotationDegrees);
    const bool rightPosition=vector3("right_position",rightCalibration.position);
    const bool rightRotation=vector3("right_rotation",rightCalibration.rotationDegrees);
    if(defaults || values["version"] != "2") resetWeaponProfiles();
    unsigned profileValues{};
    for(int value=static_cast<int>(HandWeaponKind::CombatShotgun);
        value<static_cast<int>(HandWeaponKind::Count);++value){
        const auto weapon=static_cast<HandWeaponKind>(value);
        const std::string key=HandWeaponKindKey(weapon);
        auto&profile=weaponCalibrations[static_cast<size_t>(weapon)];
        profileValues+=vector3(key+"_left_position",profile[0].position)?1u:0u;
        profileValues+=vector3(key+"_left_rotation",profile[0].rotationDegrees)?1u:0u;
        profileValues+=vector3(key+"_right_position",profile[1].position)?1u:0u;
        profileValues+=vector3(key+"_right_rotation",profile[1].rotationDegrees)?1u:0u;
    }
    if(leftPosition||leftRotation||rightPosition||rightRotation||profileValues)
        say(std::string(defaults?"default":"saved")+" hand calibration loaded; per-weapon values="
            +std::to_string(profileValues));
}

void HandRenderer::Impl::writeCalibrationStatus(const char*message)const{
    if(calibrationMode==CalibrationMode::None)return;
    const auto&value=selectedCalibration();
    const auto path=root/L"hand_calibration_status.txt";
    std::ofstream output(path,std::ios::trunc);
    if(!output)return;
    output<<std::fixed<<std::setprecision(3)
        <<(calibrationMode==CalibrationMode::Rotation?"ROTATION":"POSITION")
        <<" | hand="<<(calibrateLeft?"LEFT":"RIGHT")
        <<" | profile="<<HandWeaponKindKey(calibrationWeapon)
        <<" | mode="<<(calibrationLeftHanded?"LEFT_HANDED":"RIGHT_HANDED")
        <<" | position="<<value.position[0]<<' '<<value.position[1]<<' '<<value.position[2]
        <<" | rotation="<<value.rotationDegrees[0]<<' '<<value.rotationDegrees[1]<<' '<<value.rotationDegrees[2];
    if(message&&*message)output<<" | "<<message;
}

bool HandRenderer::Impl::saveCalibration(){
    return calibrationDraft.apply([&](const auto& values){
        const auto target=root/L"hand_pose_calibration_saved.cfg",temporary=root/L"hand_pose_calibration_saved.tmp";
        std::ofstream out(temporary);out<<std::fixed<<std::setprecision(6);
        for(const auto& [key,value]:values){out<<key;
            for(float v:value.position)out<<' '<<v;
            for(float v:value.rotationDegrees)out<<' '<<v;
            out<<' '<<value.scale<<'\n';}
        out.flush();const bool ok=bool(out);out.close();
        return ok&&MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    });
}

void HandRenderer::Impl::pollCalibration(const HandGameplayState&gameplay){
    if(calibrationMode==CalibrationMode::None)return;
    if(calibrationWeapon!=HandWeaponKind::Unknown&&calibrationWeapon!=gameplay.weapon)return;
    if(calibrationLeftHanded!=gameplay.leftHanded)return;
    if(!calibrationContextInitialized){
        // Start on the weapon hand; Num 0 selects the other physical hand
        // within this same weapon profile and dominant-hand mode.
        calibrateLeft=gameplay.leftHanded;
        calibrationContextInitialized=true;
    }
    DWORD foregroundProcess{};
    GetWindowThreadProcessId(GetForegroundWindow(),&foregroundProcess);
    const bool focused=foregroundProcess==GetCurrentProcessId();
    // Modified numpad belongs to HUD calibration, not hand/weapon calibration.
    const bool hudModifier=(GetAsyncKeyState(VK_MENU)&0x8000)||(GetAsyncKeyState(VK_CONTROL)&0x8000);
    const bool plusDown=(GetAsyncKeyState(VK_ADD)&0x8000)!=0;
    const bool handDown=(GetAsyncKeyState(VK_NUMPAD0)&0x8000)!=0;
    const bool resetDown=(GetAsyncKeyState(VK_NUMPAD5)&0x8000)!=0;
    if(!focused||hudModifier){
        calibrationPlusWasDown=plusDown;calibrationHandWasDown=handDown;
        calibrationResetWasDown=resetDown;return;
    }
    const auto nextMode=updateHandCalibrationMode(calibrationMode,plusDown,
        calibrationPlusWasDown);
    if(nextMode!=calibrationMode){
        calibrationMode=nextMode;
        writeCalibrationStatus("Num + switched rotation / position");
    }
    const bool switchHand=handDown&&!calibrationHandWasDown;
    const bool reset=resetDown&&!calibrationResetWasDown;
    calibrationHandWasDown=handDown;calibrationResetWasDown=resetDown;
    if(switchHand){calibrateLeft=!calibrateLeft;writeCalibrationStatus("Num 0 switched hand");}
    if(reset){
        auto&value=selectedCalibration();
        const auto selection=selectHandCalibrationProfile(calibrateLeft,
            calibrationLeftHanded,calibrationWeapon,true);
        value=selection.weaponSpecific
            ?defaultWeaponCalibrations[selection.weaponIndex][selection.handIndex]
            :(calibrateLeft?defaultLeftCalibration:defaultRightCalibration);
        writeCalibrationStatus("preview - press APPLY to save");return;
    }
    const ULONGLONG now=GetTickCount64();
    if(now-lastCalibrationStep<90)return;
    const bool fine=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
    const std::array<int,6>keys{{VK_NUMPAD4,VK_NUMPAD6,VK_NUMPAD2,VK_NUMPAD8,VK_NUMPAD7,VK_NUMPAD9}};
    std::array<bool,6>down{};bool any{};for(size_t i=0;i<keys.size();i++){down[i]=(GetAsyncKeyState(keys[i])&0x8000)!=0;any|=down[i];}
    if(!any)return;
    auto&value=selectedCalibration();
    float*components=calibrationMode==CalibrationMode::Rotation?value.rotationDegrees:value.position;
    const float step=calibrationMode==CalibrationMode::Rotation?(fine?1.f:5.f):(fine?.001f:.005f);
    if(down[0])components[0]-=step;if(down[1])components[0]+=step;
    if(down[2])components[1]-=step;if(down[3])components[1]+=step;
    // OpenXR forward is -Z: Num 7 moves the hand farther forward, Num 9 back.
    if(down[4])components[2]-=step;if(down[5])components[2]+=step;
    if(calibrationMode==CalibrationMode::Rotation)for(float&component:value.rotationDegrees)component=std::remainder(component,360.f);
    else for(float&component:value.position)component=std::clamp(component,-.5f,.5f);
    lastCalibrationStep=now;writeCalibrationStatus("preview - press APPLY to save");
}

HandRenderer::HandRenderer():impl_(new Impl){}
HandRenderer::~HandRenderer(){shutdown();delete impl_;}

bool HandRenderer::initialize(VkPhysicalDevice physicalDevice,VkDevice device,
    VkQueue queue,std::uint32_t queueFamily,const KharvoxVulkanDispatch&dispatch,
    VkFormat colorFormat,const std::array<VkExtent2D,2>&eyeExtents,
    const std::array<std::vector<VkImage>,2>&eyeImages,
    const std::wstring&runtimeDirectory,HandLog logger,bool arrayStereo,bool sceneDepthOnly){
    shutdown();impl_->physical=physicalDevice;impl_->device=device;impl_->queue=queue;impl_->queueFamily=queueFamily;impl_->vk=dispatch;impl_->format=colorFormat;impl_->extents=eyeExtents;impl_->root=runtimeDirectory;impl_->log=std::move(logger);
    std::unordered_map<std::string,std::string>config;std::ifstream input(impl_->root/L"hand_models.cfg");std::string line;while(std::getline(input,line)){line=trim(line);if(line.empty()||line[0]=='#')continue;const auto equals=line.find('=');if(equals!=std::string::npos)config[trim(line.substr(0,equals))]=trim(line.substr(equals+1));}
    if(!input&&!std::filesystem::exists(impl_->root/L"hand_models.cfg")){impl_->say("hand_models.cfg missing; static hands disabled");return false;}
    auto vector3=[&](const char*key,float value[3]){std::istringstream stream(config[key]);return bool(stream>>value[0]>>value[1]>>value[2]);};vector3("left_position",impl_->leftCalibration.position);vector3("left_rotation",impl_->leftCalibration.rotationDegrees);vector3("right_position",impl_->rightCalibration.position);vector3("right_rotation",impl_->rightCalibration.rotationDegrees);try{if(config.count("left_scale"))impl_->leftCalibration.scale=std::clamp(std::stof(config["left_scale"]),.05f,2.f);if(config.count("right_scale"))impl_->rightCalibration.scale=std::clamp(std::stof(config["right_scale"]),.05f,2.f);}catch(...){impl_->say("invalid hand scale; using safe defaults");}impl_->mirrorX[0]=config["left_fist_mirror_x"]=="1";impl_->mirrorX[1]=config["right_fist_mirror_x"]=="1";impl_->mirrorX[2]=config["left_gun_mirror_x"]=="1";impl_->mirrorX[3]=config["right_gun_mirror_x"]=="1";
    impl_->loadCalibrationFile(L"hand_models_calibration_default.cfg",true);
    impl_->defaultLeftCalibration=impl_->leftCalibration;impl_->defaultRightCalibration=impl_->rightCalibration;
    impl_->defaultWeaponCalibrations=impl_->weaponCalibrations;
    impl_->loadCalibrationFile(L"hand_models_calibration_saved.cfg",false);
    impl_->calibrationDraft={};
    for(const auto* filename:{L"hand_pose_calibration_default.cfg",L"hand_pose_calibration_saved.cfg"}){std::ifstream in(impl_->root/filename);std::string key;HandCalibration value;
     while(in>>key>>value.position[0]>>value.position[1]>>value.position[2]>>value.rotationDegrees[0]>>value.rotationDegrees[1]>>value.rotationDegrees[2]>>value.scale){
      bool valid=std::isfinite(value.scale)&&value.scale>0;
      for(float v:value.position)valid=valid&&std::isfinite(v);
      for(float v:value.rotationDegrees)valid=valid&&std::isfinite(v);
      if(valid)impl_->calibrationDraft.saved[key]=value;
     }}

    char calibrationMode[24]{};
    if(GetEnvironmentVariableA("ARGENT_HAND_CALIBRATION",calibrationMode,sizeof(calibrationMode))>0){
        if(!_stricmp(calibrationMode,"rotation"))impl_->calibrationMode=CalibrationMode::Rotation;
        else if(!_stricmp(calibrationMode,"position"))impl_->calibrationMode=CalibrationMode::Position;
    }
    if(!impl_->createCommonResources()){impl_->say("Vulkan setup failed; native game rendering continues");shutdown();return false;}
    const std::array<const char*,4>keys{{"left_fist","right_fist","left_gun","right_gun"}};for(size_t i=0;i<keys.size();i++){const auto found=config.find(keys[i]);if(found==config.end()||found->second.empty()){impl_->say(std::string(keys[i])+" not configured; that hand pose is disabled");continue;}const auto path=impl_->root/std::filesystem::u8path(found->second);if(!std::filesystem::exists(path)){impl_->say(path.filename().u8string()+" missing; model disabled");continue;}impl_->loadAsset(path,impl_->assets[i]);}
    // One closed beam mesh: no crossed compositor ribbons or orientation singularity.
    std::vector<Vertex> beamVertices;
    std::vector<uint32_t> beamIndices;
    for(unsigned ring=0;ring<2;++ring)for(unsigned i=0;i<8;++i){
        const float angle=float(i)*6.28318530718f/8.f;
        Vertex v{};v.position[0]=.002f*std::cos(angle);v.position[1]=.002f*std::sin(angle);
        v.position[2]=ring?-20.f:0.f;v.normal[0]=std::cos(angle);v.normal[1]=std::sin(angle);
        v.baseColor[0]=1.f;v.baseColor[1]=.005f;v.baseColor[2]=.002f;v.material[0]=-1.f;
        beamVertices.push_back(v);
    }
    for(uint32_t i=0;i<8;++i){const auto n=(i+1)%8;
        for(auto v:{i,n,i+8,n,n+8,i+8})beamIndices.push_back(v);
    }
    impl_->laserAsset.ready=impl_->buffer(beamVertices.size()*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        beamVertices.data(),impl_->laserAsset.vertices,impl_->laserAsset.vertexMemory)
        &&impl_->buffer(beamIndices.size()*sizeof(uint32_t),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        beamIndices.data(),impl_->laserAsset.indices,impl_->laserAsset.indexMemory);
    impl_->laserAsset.indexCount=static_cast<uint32_t>(beamIndices.size());
    for (size_t eye = 0; eye < 2; eye++) {
      impl_->eyeViews[eye].resize(eyeImages[eye].size());
      impl_->depthTargets[eye].resize(eyeImages[eye].size());
      impl_->framebuffers[eye].resize(eyeImages[eye].size());
      for (size_t index = 0; index < eyeImages[eye].size(); index++) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = eyeImages[eye][index];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = colorFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        view.subresourceRange.baseArrayLayer = arrayStereo ? uint32_t(eye) : 0;
        if (impl_->vk.createImageView(device, &view, nullptr,
                                      &impl_->eyeViews[eye][index]) !=
            VK_SUCCESS)
          continue;
        if(sceneDepthOnly)continue;
        if(!impl_->depthTarget(eyeExtents[eye],impl_->depthTargets[eye][index])){
          impl_->say("depth buffer creation failed; hand surface disabled for one eye image");
          continue;
        }
        const std::array<VkImageView,2>attachments{{impl_->eyeViews[eye][index],impl_->depthTargets[eye][index].view}};
        VkFramebufferCreateInfo framebuffer{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = impl_->renderPass;
        framebuffer.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebuffer.pAttachments = attachments.data();
        framebuffer.width = eyeExtents[eye].width;
        framebuffer.height = eyeExtents[eye].height;
        framebuffer.layers = 1;
        impl_->vk.createFramebuffer(device, &framebuffer, nullptr,
                                    &impl_->framebuffers[eye][index]);
      }
    }
    if(!sceneDepthOnly)for(const auto& eye:impl_->framebuffers){
      if(eye.empty()||std::any_of(eye.begin(),eye.end(),[](VkFramebuffer framebuffer){return !framebuffer;})){
        impl_->say("incomplete stereo targets; both hands disabled to avoid one-eye rendering");
        return false;
      }
    }
    for(const auto& eye:impl_->eyeViews)if(eye.empty()||std::any_of(eye.begin(),eye.end(),[](VkImageView v){return !v;}))return false;
    const auto available=availability();impl_->initialized=available.leftFist||available.rightFist||available.leftGun||available.rightGun;impl_->say(std::string("renderer ")+(impl_->initialized?"ready":"has no usable assets")+(sceneDepthOnly?"; scene-depth hands, constant environment lighting":"; self-depth overlay hands, constant environment lighting"));impl_->writeCalibrationStatus("Num 0 switches hand; Num 5 resets selected hand");return impl_->initialized;
}

void HandRenderer::shutdown() {
  if (!impl_ || !impl_->device)
    return;
  if (impl_->queue && impl_->vk.queueWaitIdle)
    impl_->vk.queueWaitIdle(impl_->queue);
  for(auto framebuffer:impl_->sceneFramebuffers)
    if(framebuffer)impl_->vk.destroyFramebuffer(impl_->device,framebuffer,nullptr);
  for(auto& copy:impl_->sceneDepthCopies)impl_->destroyDepth(copy.target);
  for(auto&scene:impl_->scenePipelines){
    if(scene.hudMask)impl_->vk.destroyPipeline(impl_->device,scene.hudMask,nullptr);
    if(scene.hudPlaceholder)impl_->vk.destroyPipeline(impl_->device,scene.hudPlaceholder,nullptr);
    if(scene.pipeline)impl_->vk.destroyPipeline(impl_->device,scene.pipeline,nullptr);
    if(scene.renderPass)impl_->vk.destroyRenderPass(impl_->device,scene.renderPass,nullptr);
  }
  for (auto &eye : impl_->framebuffers)
    for (auto framebuffer : eye)
      if (framebuffer)
        impl_->vk.destroyFramebuffer(impl_->device, framebuffer, nullptr);
  for (auto &eye : impl_->eyeViews)
    for (auto view : eye)
      if (view)
        impl_->vk.destroyImageView(impl_->device, view, nullptr);
  for(auto&eye:impl_->depthTargets)for(auto&depth:eye){
    if(depth.view)impl_->vk.destroyImageView(impl_->device,depth.view,nullptr);
    if(depth.image)impl_->vk.destroyImage(impl_->device,depth.image,nullptr);
    if(depth.memory)impl_->vk.freeMemory(impl_->device,depth.memory,nullptr);
  }
  impl_->destroyAsset(impl_->laserAsset);
  for (auto &asset : impl_->assets)
    impl_->destroyAsset(asset);
  if (impl_->pipeline)
    impl_->vk.destroyPipeline(impl_->device, impl_->pipeline, nullptr);
  if(impl_->fragmentShader)
    impl_->vk.destroyShaderModule(impl_->device,impl_->fragmentShader,nullptr);
  if(impl_->hudMaskShader)impl_->vk.destroyShaderModule(impl_->device,impl_->hudMaskShader,nullptr);
  if(impl_->hudPlaceholderShader)impl_->vk.destroyShaderModule(impl_->device,impl_->hudPlaceholderShader,nullptr);
  if(impl_->vertexShader)
    impl_->vk.destroyShaderModule(impl_->device,impl_->vertexShader,nullptr);
  if (impl_->pipelineLayout)
    impl_->vk.destroyPipelineLayout(impl_->device, impl_->pipelineLayout,
                                    nullptr);
  if (impl_->renderPass)
    impl_->vk.destroyRenderPass(impl_->device, impl_->renderPass, nullptr);
  if (impl_->descriptorPool)
    impl_->vk.destroyDescriptorPool(impl_->device, impl_->descriptorPool,
                                    nullptr);
  if (impl_->descriptorLayout)
    impl_->vk.destroyDescriptorSetLayout(impl_->device, impl_->descriptorLayout,
                                         nullptr);
  if (impl_->sampler)
    impl_->vk.destroySampler(impl_->device, impl_->sampler, nullptr);
  if (impl_->uploadPool)
    impl_->vk.destroyCommandPool(impl_->device, impl_->uploadPool, nullptr);
  const auto logger = impl_->log;
  *impl_ = Impl{};
  impl_->log = logger;
  if (logger)
    logger("[HANDS] renderer resources released");
}

void HandRenderer::configureCalibration(HandWeaponKind profile,bool leftHanded,bool enabled,const argent::calibration::ApplyCommand& command){
    auto& p=*impl_;
    if(p.applyRevision.consume(command.revision)&&command.mode=="hands"){
        const auto target=command.profile+(command.left?"_left":"_right");
        argent::calibration::report(p.root,command.revision,p.calibrationDraft.scope==target&&p.saveCalibration(),"hands "+target);
    }
    if(p.calibrationWeapon!=profile||p.calibrationLeftHanded!=leftHanded)p.calibrationContextInitialized=false;
    p.calibrationWeapon=profile;p.calibrationLeftHanded=leftHanded;
    p.calibrationDraft.select(enabled?std::string(HandWeaponKindKey(profile))+(leftHanded?"_left":"_right"):std::string{});
}

void HandRenderer::setCalibrationEnabled(bool enabled){
    if(enabled==(impl_->calibrationMode!=CalibrationMode::None))return;
    impl_->calibrationMode=enabled?CalibrationMode::Rotation:CalibrationMode::None;
    impl_->calibrationPlusWasDown=impl_->calibrationHandWasDown=impl_->calibrationResetWasDown=false;
    impl_->calibrationContextInitialized=false;
}

HandAssetAvailability HandRenderer::availability()const{return {impl_->assets[0].ready,impl_->assets[1].ready,impl_->assets[2].ready,impl_->assets[3].ready};}
const HandCalibration&HandRenderer::leftCalibration()const{return impl_->leftCalibration;}
const HandCalibration&HandRenderer::rightCalibration()const{return impl_->rightCalibration;}

void HandRenderer::record(VkCommandBuffer commandBuffer,std::uint32_t eye,
    std::uint32_t imageIndex,const HandEyeView&view,const HandPose&leftGrip,
    const HandPose&rightGrip,const HandVisibilityOutput&visibility,
    const HandGameplayState&gameplay){
    if(!impl_->initialized||eye>=2||imageIndex>=impl_->framebuffers[eye].size()||!impl_->framebuffers[eye][imageIndex])return;impl_->pollCalibration(gameplay);const bool drawLeft=visibility.left!=HandModelKind::None&&leftGrip.valid;const bool drawRight=visibility.right!=HandModelKind::None&&rightGrip.valid;if(!drawLeft&&!drawRight)return;
    const auto&extent=impl_->extents[eye];
    const int32_t rectX=std::clamp(view.imageRectX,0,int32_t(extent.width));
    const int32_t rectY=std::clamp(view.imageRectY,0,int32_t(extent.height));
    const uint32_t requestedWidth=view.imageRectWidth?view.imageRectWidth:extent.width;
    const uint32_t requestedHeight=view.imageRectHeight?view.imageRectHeight:extent.height;
    const uint32_t rectWidth=std::min(requestedWidth,extent.width-uint32_t(rectX));
    const uint32_t rectHeight=std::min(requestedHeight,extent.height-uint32_t(rectY));
    if(!rectWidth||!rectHeight)return;
    VkRect2D scissor{{rectX,rectY},{rectWidth,rectHeight}};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = impl_->renderPass;
    begin.framebuffer = impl_->framebuffers[eye][imageIndex];
    begin.renderArea = scissor;
    std::array<VkClearValue,2>clearValues{};clearValues[1].depthStencil={1.f,0};
    begin.clearValueCount=static_cast<uint32_t>(clearValues.size());
    begin.pClearValues=clearValues.data();
    impl_->vk.cmdBeginRenderPass(commandBuffer, &begin,
                                 VK_SUBPASS_CONTENTS_INLINE);
    impl_->drawGeometry(commandBuffer,impl_->pipeline,extent,view,leftGrip,
        rightGrip,visibility,gameplay);
    impl_->vk.cmdEndRenderPass(commandBuffer);
}

bool HandRenderer::recordSceneIntegrated(VkCommandBuffer commandBuffer,
    const HandSceneTarget&target,const HandEyeView&view,
    const HandPose&leftGrip,const HandPose&rightGrip,
    const HandVisibilityOutput&visibility,const HandGameplayState&gameplay,const HandPose&laser){
    if(!impl_->initialized||!target.colorView||!target.depthView
        ||target.colorFormat==VK_FORMAT_UNDEFINED
        ||!handSceneDepthFormat(target.depthFormat)
        ||target.samples!=VK_SAMPLE_COUNT_1_BIT)return false;
    const bool drawLeft=visibility.left!=HandModelKind::None&&leftGrip.valid;
    const bool drawRight=visibility.right!=HandModelKind::None&&rightGrip.valid;
    if(!drawLeft&&!drawRight&&!laser.valid&&view.hudPlaceholder.empty())return false;
    impl_->pollCalibration(gameplay);
    auto*scene=impl_->scenePipeline(target);if(!scene)return false;
    Impl::SceneDepth* privateDepth=nullptr;
    if(target.copyDepthForHands){
        const auto slot=impl_->sceneDepthCopiesUsed++;
        if(slot==impl_->sceneDepthCopies.size())impl_->sceneDepthCopies.emplace_back();
        privateDepth=&impl_->sceneDepthCopies[slot];
        if(privateDepth->format!=target.depthFormat||privateDepth->extent.width!=target.extent.width
            ||privateDepth->extent.height!=target.extent.height){
            // This slot is reused only after finishSceneIntegratedFrame, which
            // follows the owner fence and destroys all borrowing framebuffers.
            impl_->destroyDepth(privateDepth->target);*privateDepth={};
        }
        if(!privateDepth->target.view){
            if(!impl_->depthTarget(target.extent,privateDepth->target,target.depthFormat,true)){
                impl_->destroyDepth(privateDepth->target);return false;
            }
            privateDepth->format=target.depthFormat;privateDepth->extent=target.extent;
            impl_->say("Native private hand depth ready; scene depth/stencil preserved");
        }
    }
    const std::array<VkImageView,2>attachments{{target.colorView,privateDepth?privateDepth->target.view:target.depthView}};
    VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass=scene->renderPass;
    framebufferInfo.attachmentCount=static_cast<std::uint32_t>(attachments.size());
    framebufferInfo.pAttachments=attachments.data();framebufferInfo.width=target.extent.width;
    framebufferInfo.height=target.extent.height;framebufferInfo.layers=1;
    VkFramebuffer framebuffer{};
    if(impl_->vk.createFramebuffer(impl_->device,&framebufferInfo,nullptr,&framebuffer)!=VK_SUCCESS)
        return false;
    impl_->sceneFramebuffers.push_back(framebuffer);
    if(privateDepth){
        copyHandSceneDepth(impl_->vk,commandBuffer,target.depthImage,privateDepth->target.image,
            target.extent,handSceneDepthAspect(target.depthFormat),privateDepth->initialized,target.depthArrayLayer,target.depthExtent,target.depthOffset);
        privateDepth->initialized=true;
    }
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass=scene->renderPass;begin.framebuffer=framebuffer;
    begin.renderArea={{0,0},target.extent};
    impl_->vk.cmdBeginRenderPass(commandBuffer,&begin,VK_SUBPASS_CONTENTS_INLINE);
    impl_->drawGeometry(commandBuffer,scene->pipeline,target.extent,view,leftGrip,
        rightGrip,visibility,gameplay,laser);
    impl_->vk.cmdEndRenderPass(commandBuffer);
    return true;
}

bool HandRenderer::recordSceneDepth(VkCommandBuffer cb,uint32_t eye,uint32_t index,
    HandSceneTarget target,const HandEyeView& view,const HandPose& left,const HandPose& right,
    const HandVisibilityOutput& visibility,const HandGameplayState& gameplay,const HandPose& laser){
    if(!impl_->initialized||eye>=2||index>=impl_->eyeViews[eye].size())return false;
    target.depthExtent=target.extent;target.extent=impl_->extents[eye];
    if(target.depthExtent.width!=target.extent.width||target.depthExtent.height!=target.extent.height){
        if(!impl_->vk.getPhysicalDeviceFormatProperties)return false;
        VkFormatProperties properties{};
        impl_->vk.getPhysicalDeviceFormatProperties(impl_->physical,target.depthFormat,&properties);
        const auto required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT;
        if((properties.optimalTilingFeatures&required)!=required)return false;
    }
    target.colorView=impl_->eyeViews[eye][index];target.colorFormat=impl_->format;
    target.copyDepthForHands=true;target.depthArrayLayer=eye;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image=target.depthImage;barrier.oldLayout=target.depthLayout;
    barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange={handSceneDepthAspect(target.depthFormat),0,1,eye,1};
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    impl_->vk.cmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    const bool recorded=recordSceneIntegrated(cb,target,view,left,right,visibility,gameplay,laser);
    std::swap(barrier.oldLayout,barrier.newLayout);std::swap(barrier.srcAccessMask,barrier.dstAccessMask);
    impl_->vk.cmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    return recorded;
}

void HandRenderer::finishSceneIntegratedFrame(){
    if(!impl_->device)return;
    for(auto framebuffer:impl_->sceneFramebuffers)
        if(framebuffer)impl_->vk.destroyFramebuffer(impl_->device,framebuffer,nullptr);
    impl_->sceneFramebuffers.clear();
    impl_->sceneDepthCopiesUsed=0;
}

} // namespace kharvox::hands
