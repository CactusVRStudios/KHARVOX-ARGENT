#include "FrameGeneration.h"
#include "Interop.h"
#include "generator.hpp"
#include "ObservedLayouts.h"
#include "InputLayouts.h"
#include "ExternalFramegen.h"
#include "AdapterPolicy.h"
#include "NativeMotionSources.h"
#include "DepthProjection.h"
#include "NativeInputTargets.h"
#include "../EternalRenderControls.h"
#include "../hands/HandSceneDepthTracker.h"
#include "../openxr/GameImageLifetime.h"
#include "inputs_spv.hpp"
#include "../EternalCameraHook.h"
#include "../Diagnostics.h"
#include "../openxr/StereoProjection.h"
#include <map>
#include <chrono>
#include <limits>
namespace argent::fg {
namespace {
std::mutex mutex;
struct Prepared {sfs::FramePose pose{};sfs::EyeUniforms uniforms{};sfs::Matrix projection{};};
std::map<uint64_t,Prepared> prepared;
// Record local transitions; resolve inherited layouts later in queue submission order.
std::map<VkCommandBuffer,std::vector<Layout>> layouts;
std::map<VkCommandBuffer,uint32_t> commandFamilies;
std::map<VkQueue,std::vector<Layout>> submittedLayouts;
std::map<VkCommandBuffer,std::vector<Layout>> commandWrites;
std::map<VkImage,NativeWrite> submittedWrites;
uint64_t writeOrder{};
NativeInputTargets nativeTargets;
struct Lease {
 std::array<InputPtr,2> eyes{std::make_shared<Input>(),std::make_shared<Input>()};
 VkEvent complete{};VkDescriptorPool descriptors{};std::array<VkDescriptorSet,2> sets{};std::array<VkImageView,4> views{};
 uint32_t width{},height{};uint64_t serial{};bool recorded{},inputsReady{},submitted{},native{},depthInverted{};
 VkBuffer dispatch{};VkDeviceMemory dispatchMemory{};VkDispatchIndirectCommand* dispatchMapped{};
 std::array<std::array<dlss::Resource,2>,2> sources{};std::vector<Layout> prefix;
 std::array<std::array<VkImageLayout,2>,2> sourceLayouts{};bool diagnostic{};
 Prepared metadata;std::array<dlss::Parameters,2> params;
};
struct Context {
 Interop gpu;Generator generator;bool ready{},failed{},previousNative{},previousDepthInverted{};
 VkDescriptorSetLayout setLayout{};VkPipelineLayout pipelineLayout{};VkPipeline pipeline{};VkSampler sampler{};
 std::vector<std::shared_ptr<Lease>> leases;std::map<uint64_t,std::shared_ptr<Lease>> pending;
 SharedImage color;std::array<SharedImage,2> generated;
 std::shared_ptr<Lease> inFlight; // pins descriptor/input storage until the GPU join retires
 std::shared_ptr<Lease> nativeLease;
 uint32_t width{},height{};VkFormat format{};uint64_t rejectedSerial{},captureSamples{},generatedSamples{};
 std::array<sfs::Matrix,2> previousVP{};sfs::FramePose previousPose{};uint64_t previousTick{};
};
std::unique_ptr<Context> context;
std::map<VkCommandBuffer,std::vector<std::weak_ptr<Lease>>> captures;
#define V(n) g.proc<PFN_##n>(#n)
void pipeline(Context& c){
 auto& g=c.gpu;
 VkDescriptorSetLayoutBinding bindings[4]{};for(unsigned i=0;i<4;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
 VkDescriptorBindingFlags flags[]={VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,0,0};
 VkDescriptorSetLayoutBindingFlagsCreateInfo bf{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};bf.bindingCount=4;bf.pBindingFlags=flags;
 VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,&bf};si.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;si.bindingCount=4;si.pBindings=bindings;Interop::check(V(vkCreateDescriptorSetLayout)(g.dev.device,&si,nullptr,&c.setLayout),"FG descriptor layout");
 VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&c.setLayout;Interop::check(V(vkCreatePipelineLayout)(g.dev.device,&li,nullptr,&c.pipelineLayout),"FG pipeline layout");
 VkSamplerCreateInfo sa{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sa.magFilter=sa.minFilter=VK_FILTER_NEAREST;sa.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sa.addressModeU=sa.addressModeV=sa.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;Interop::check(V(vkCreateSampler)(g.dev.device,&sa,nullptr,&c.sampler),"FG sampler");
 VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof(argentFgInputs);sm.pCode=argentFgInputs;VkShaderModule shader{};Interop::check(V(vkCreateShaderModule)(g.dev.device,&sm,nullptr,&shader),"FG conversion shader");
 VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.layout=c.pipelineLayout;pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=shader;pi.stage.pName="main";
 auto r=V(vkCreateComputePipelines)(g.dev.device,{},1,&pi,nullptr,&c.pipeline);V(vkDestroyShaderModule)(g.dev.device,shader,nullptr);Interop::check(r,"FG conversion pipeline");
}
VkImageLayout observed(VkCommandBuffer cb,const dlss::Resource& r){
 auto it=layouts.find(cb);return it==layouts.end()?VK_IMAGE_LAYOUT_UNDEFINED:observedLayout(it->second,r.image,r.range);
}
bool readable(VkImageLayout l){return l==VK_IMAGE_LAYOUT_GENERAL||l==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||l==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL||l==VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;}
void initializeLease(Context& c,Lease& p,uint32_t w,uint32_t h){
 auto& g=c.gpu;p.width=w;p.height=h;
 VkEventCreateInfo ec{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};Interop::check(V(vkCreateEvent)(g.dev.device,&ec,nullptr,&p.complete),"FG lease event");
 VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=sizeof(VkDispatchIndirectCommand);bc.usage=VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
 Interop::check(V(vkCreateBuffer)(g.dev.device,&bc,nullptr,&p.dispatch),"FG dispatch gate");
 VkMemoryRequirements req{};V(vkGetBufferMemoryRequirements)(g.dev.device,p.dispatch,&req);VkPhysicalDeviceMemoryProperties mp{};g.instanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(g.dev.physical,&mp);
 uint32_t type=UINT32_MAX;for(uint32_t t=0;t<mp.memoryTypeCount;++t)if((req.memoryTypeBits&(1u<<t))&&(mp.memoryTypes[t].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=t;break;}
 if(type==UINT32_MAX)throw std::runtime_error("FG coherent dispatch gate memory unavailable");VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=type;
 Interop::check(V(vkAllocateMemory)(g.dev.device,&ma,nullptr,&p.dispatchMemory),"FG dispatch memory");Interop::check(V(vkBindBufferMemory)(g.dev.device,p.dispatch,p.dispatchMemory,0),"FG dispatch binding");
 Interop::check(V(vkMapMemory)(g.dev.device,p.dispatchMemory,0,VK_WHOLE_SIZE,0,reinterpret_cast<void**>(&p.dispatchMapped)),"FG dispatch map");
 for(unsigned e=0;e<2;++e){auto& eye=*p.eyes[e];eye.capture.depth={w,h};
  g.make(eye.depth,w,h,VK_FORMAT_R32_SFLOAT,DXGI_FORMAT_R32_FLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  g.make(eye.motion,w,h,VK_FORMAT_R32G32B32A32_SFLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  for(unsigned j=0;j<2;++j){VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.image=j?eye.motion.image:eye.depth.image;vi.format=j?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R32_SFLOAT;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};Interop::check(V(vkCreateImageView)(g.dev.device,&vi,nullptr,&p.views[e*2+j]),"FG output view");}
 }
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4}};
 VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.flags=VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;pc.maxSets=2;pc.poolSizeCount=2;pc.pPoolSizes=sizes;Interop::check(V(vkCreateDescriptorPool)(g.dev.device,&pc,nullptr,&p.descriptors),"FG descriptor pool");
 VkDescriptorSetLayout sl[]={c.setLayout,c.setLayout};VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=p.descriptors;ai.descriptorSetCount=2;ai.pSetLayouts=sl;Interop::check(V(vkAllocateDescriptorSets)(g.dev.device,&ai,p.sets.data()),"FG descriptor sets");
}
void colors(Context& c,const sfs::StereoFrame& pair){
 auto& g=c.gpu;const auto w=pair.eyes[0].extent.width,h=pair.eyes[0].extent.height;auto f=pair.eyes[0].format;
 if(c.width==w&&c.height==h&&c.format==f)return;
 if(f!=VK_FORMAT_B8G8R8A8_SRGB&&f!=VK_FORMAT_B8G8R8A8_UNORM&&f!=VK_FORMAT_R8G8B8A8_UNORM&&f!=VK_FORMAT_R8G8B8A8_SRGB)throw std::runtime_error("FG requires an 8-bit SDR stereo source");
 g.waitDx();g.destroy(c.color);for(auto& i:c.generated)g.destroy(i);
 const bool bgra=f==VK_FORMAT_B8G8R8A8_SRGB||f==VK_FORMAT_B8G8R8A8_UNORM;
 const auto vk=bgra?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM;const auto dx=bgra?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
 g.make(c.color,w*2,h,vk,dx,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,D3D12_RESOURCE_FLAG_NONE);
 // Write NGX composition directly into the two Vulkan-imported eye images.
 // This removes the intermediate stereo atlas and the full-resolution split copy.
 for(auto& i:c.generated)g.make(i,w,h,vk,dx,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
 c.width=w;c.height=h;c.format=f;c.generator.reset();c.previousPose={};
}
bool cameras(Context& c,Lease& p,std::array<sfs::Matrix,2>& vp){
 std::array<float,3> origin{};std::array<float,9> basis{};
 sfs::Matrix source{};
 if(!camera::renderedWorldCamera(p.serial,origin.data(),basis.data(),source.data()))return false;
 if(source[0]<=0||source[5]>=0||std::abs(source[11]+1)>.001f||std::abs(source[15])>.001f)return false;
 float nearPlane{},farPlane{};if(!depthProjection(source,p.native&&p.depthInverted,nearPlane,farPlane))return false;
 for(unsigned e=0;e<2;++e){
  auto& out=p.eyes[e]->capture.camera;out={};auto world=sfs::identity();
  // Eternal basis: forward, left, up; camera space: right, up, back.
  for(unsigned k=0;k<3;++k){world[k]=-basis[3+k];world[4+k]=basis[6+k];world[8+k]=-basis[k];world[12+k]=origin[k];}
  // clipFromCenter carries asymmetric FOV. Translation is supplied separately.
  auto projection=sfs::multiply(p.metadata.uniforms.clipFromCenter[e],source);
  const auto& t=p.metadata.uniforms.eyeTranslation[e];
  for(unsigned k=0;k<3;++k)world[12+k]-=world[k]*t[0]/projection[0]+world[4+k]*t[1]/projection[5];
  sfs::Matrix view{};if(!sfs::inverse(world,view))return false;
  if(!sfs::inverse(projection,out.clipToView))return false;out.viewToClip=projection;out.lens=sfs::identity();vp[e]=sfs::multiply(projection,view);
  sfs::Matrix inverseVP{};if(!sfs::inverse(vp[e],inverseVP))return false;
  out.clipToPrev=c.previousPose.serial?sfs::multiply(c.previousVP[e],inverseVP):sfs::identity();if(!sfs::inverse(out.clipToPrev,out.prevToClip))return false;
  for(unsigned k=0;k<3;++k){out.position[k]=world[12+k];out.right[k]=world[k];out.up[k]=world[4+k];out.forward[k]=-world[8+k];}
  out.nearPlane=nearPlane;out.farPlane=farPlane;out.inverted=p.native&&p.depthInverted;
  out.fov=p.metadata.pose.views[e].fov.angleUp-p.metadata.pose.views[e].fov.angleDown;out.aspect=-projection[5]/projection[0];out.cameraMotion=1;out.invalidMotion=std::numeric_limits<float>::max();
  out.jitter={p.params[e].get<float>(0x28),p.params[e].get<float>(0x2c)};
  out.motionScale={p.params[e].get<float>(0x3c)/p.width,p.params[e].get<float>(0x40)/p.height};out.reset=p.params[e].get<int>(0x38)!=0;
  for(auto v:out.motionScale)if(!std::isfinite(v)||v==0)return false;
 }
 return true;
}
XrPosef midpoint(XrPosef a,XrPosef b){
 auto& q=a.orientation;auto r=b.orientation;float dot=q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w;if(dot<0){r.x=-r.x;r.y=-r.y;r.z=-r.z;r.w=-r.w;}
 q={q.x+r.x,q.y+r.y,q.z+r.z,q.w+r.w};float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);if(n>0){q.x/=n;q.y/=n;q.z/=n;q.w/=n;}
 a.position={(a.position.x+b.position.x)*.5f,(a.position.y+b.position.y)*.5f,(a.position.z+b.position.z)*.5f};return a;
}
}
void attach(Device& d){
 if(!requested())return;std::lock_guard lock(mutex);if(context)return;
 context=std::make_unique<Context>();auto& c=*context;
 try{
  if(externalFramegenLoaded())throw std::runtime_error("External OFXR frame generation detected; native DLSS-G disabled to avoid stacked generation");
  if(!d.framegenUpdateAfterBind)throw std::runtime_error("Native device lacks sampled-image update-after-bind for queue-ordered DLSS inputs");
  c.gpu.initialize(d);pipeline(c);
  wchar_t directory[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_FG_DIRECTORY",directory,32768);if(!n||n>=32768)throw std::runtime_error("Bundled frame generation directory missing");
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 desc{};
  Interop::check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"FG runtime adapter factory");
  Interop::check(factory->EnumAdapterByLuid(c.gpu.dx->GetAdapterLuid(),IID_PPV_ARGS(&adapter)),"FG runtime adapter");
  Interop::check(adapter->GetDesc1(&desc),"FG runtime adapter description");
  if(!supportedAdapter(desc.VendorId,desc.Description))throw std::runtime_error("Unsupported FG adapter");
  const auto library=std::filesystem::path(directory)/runtimeRelativePath(desc.Description);
  if(!std::filesystem::is_regular_file(library))throw std::runtime_error("Bundled DLSS-G DLL missing: "+library.string());
  const auto runtime=library.parent_path();
  log("FRAMEGEN_RUNTIME dll="+library.string()+" bundled=1");
  if(!c.generator.initialize(c.gpu.dx.Get(),c.gpu.queue.Get(),runtime.c_str())){c.failed=true;return;}
  c.ready=true;onBegin=beginCommand;onBarriers=imageBarriers;onImageDestroyed=forgetImage;state("ready","2X, independent eyes, generated then pinned real endpoint");
 }catch(const std::exception& e){c.failed=true;state("fallback",e.what());}
}
bool ready(){std::lock_guard lock(mutex);return context&&context->ready&&!context->failed;}
void recordWrites(VkCommandBuffer cb,uint32_t n,const VkImageMemoryBarrier* b){std::lock_guard lock(mutex);auto& list=commandWrites[cb];for(uint32_t i=0;i<n;++i)recordLayout(list,{b[i].image,b[i].subresourceRange,b[i].newLayout});}
void recordTargets(VkCommandBuffer cb,uint32_t n,const kharvox::hands::HandSceneTarget* targets){std::lock_guard lock(mutex);nativeTargets.record(cb,n,targets);}
void observeNativeInputs(){if(!requested())return;onBegin=beginCommand;onBarriers=imageBarriers;onWrites=recordWrites;onTargets=recordTargets;onImageDestroyed=forgetImage;}
void beginCommand(VkCommandBuffer cb,uint32_t family){std::lock_guard lock(mutex);layouts[cb].clear();commandWrites[cb].clear();nativeTargets.begin(cb);captures.erase(cb);commandFamilies[cb]=family;}
void imageBarriers(VkCommandBuffer cb,uint32_t n,const VkImageMemoryBarrier* b){std::lock_guard lock(mutex);auto& list=layouts[cb];for(uint32_t i=0;i<n;++i)
 recordLayout(list,{b[i].image,b[i].subresourceRange,b[i].newLayout});
}
void submitting(VkQueue queue,uint32_t count,const VkCommandBuffer* commands,const sfs::FramePose* submittedPose){
 if(!requested())return;std::lock_guard lock(mutex);
 // Observe producers before NGX exists. DLSS-off startup must not depend on a
 // prior DLSS evaluation, and stale ping-pong targets must never pass as current.
 for(uint32_t i=0;i<count;++i){const auto cb=commands[i];auto family=commandFamilies.find(cb);auto writes=commandWrites.find(cb);
  if(submittedPose&&submittedPose->framegenEligible&&family!=commandFamilies.end()&&family->second!=UINT32_MAX){
   nativeTargets.submit(cb,queue,submittedPose->serial);
   if(writes!=commandWrites.end())for(const auto& w:writes->second)submittedWrites[w.image]={w,submittedPose->serial,++writeOrder,queue};
  }
 }
 auto& inherited=submittedLayouts[queue];
 if(!context||!context->ready||context->failed){for(uint32_t i=0;i<count;++i){auto ls=layouts.find(commands[i]);if(ls!=layouts.end())for(const auto& l:ls->second)recordLayout(inherited,l);}return;}
 auto& c=*context;auto& g=c.gpu;
 for(uint32_t i=0;i<count;++i){auto cb=commands[i];auto ci=captures.find(cb);
  if(ci!=captures.end())for(const auto& weak:ci->second)if(auto p=weak.lock()){
   if(p->submitted)continue;p->submitted=true;bool valid=queue==g.dev.graphicsQueue;
   // Eternal records DLSS before acquiring the output image. SFS installs the
   // uniforms at acquire, before this submission. Bind metadata to those actual
   // GPU uniforms, not to the previous frame's CPU recording serial.
   if(submittedPose&&submittedPose->serial!=p->serial){
    auto next=prepared.find(submittedPose->serial);auto previous=c.pending.find(p->serial);
    if(previous!=c.pending.end()&&previous->second==p)c.pending.erase(previous);
    if(!submittedPose->framegenEligible||!submittedPose->serial||next==prepared.end()||c.pending.count(submittedPose->serial)){state("waiting","Submitted input frame is ineligible or ambiguous");continue;}
    if(p->diagnostic)log("FRAMEGEN_INPUT_BIND recordedSerial="+std::to_string(p->serial)+" submittedSerial="+std::to_string(submittedPose->serial));
    p->serial=submittedPose->serial;p->metadata=next->second;c.pending.emplace(p->serial,p);
   }
   for(unsigned e=0;e<2;++e)for(unsigned j=0;j<2;++j){const auto& r=p->sources[e][j];auto l=inputLayout(p->prefix,inherited,r.image,r.range);p->sourceLayouts[e][j]=l;valid&=readable(l);
    if(p->diagnostic)log("FRAMEGEN_INPUT eye="+std::to_string(e)+" resource="+(j?"motion":"depth")+" size="+std::to_string(r.width)+"x"+std::to_string(r.height)+" layout="+std::to_string(l)+" layoutValid="+std::to_string(readable(l))+" source=queue-order queueMatch="+std::to_string(queue==g.dev.graphicsQueue));
   }
   if(!valid){c.rejectedSerial=p->serial;state("waiting","Native DLSS input layout unresolved in submission order; capture safely skipped");continue;}
   for(unsigned e=0;e<2;++e){VkDescriptorImageInfo images[2]{};VkWriteDescriptorSet writes[2]{};
    for(unsigned j=0;j<2;++j){images[j]={c.sampler,p->sources[e][j].view,p->sourceLayouts[e][j]};auto& w=writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=p->sets[e];w.dstBinding=j;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&images[j];}
    V(vkUpdateDescriptorSets)(g.dev.device,2,writes,0,nullptr);
   }
   *p->dispatchMapped={(p->width+7)/8,(p->height+7)/8,1};p->inputsReady=true;
  }
  auto ls=layouts.find(cb);if(ls!=layouts.end())for(const auto& l:ls->second)recordLayout(inherited,l);
 }
}
void submissionFailed(VkQueue queue){if(!requested())return;std::lock_guard lock(mutex);nativeTargets.failed(queue);submittedLayouts.erase(queue);for(auto i=submittedWrites.begin();i!=submittedWrites.end();)if(i->second.queue==queue)i=submittedWrites.erase(i);else ++i;if(context){context->failed=true;state("fallback","Native Vulkan queue submission failed");}}
void forgetImage(VkImage image){std::lock_guard lock(mutex);
 auto erase=[&](std::vector<Layout>& list){list.erase(std::remove_if(list.begin(),list.end(),[&](const Layout& l){return l.image==image;}),list.end());};
 for(auto& x:submittedLayouts)erase(x.second);for(auto& x:layouts)erase(x.second);for(auto& x:commandWrites)erase(x.second);submittedWrites.erase(image);nativeTargets.forget(image);
}
void prepare(const sfs::FramePose& p,const sfs::EyeUniforms& u,const sfs::Matrix& m){if(!requested())return;std::lock_guard lock(mutex);prepared[p.serial]={p,u,m};while(prepared.size()>16)prepared.erase(prepared.begin());}
void cancelCapture(uint64_t serial){if(!requested())return;std::lock_guard lock(mutex);if(context)context->pending.erase(serial);}
void capture(VkCommandBuffer cb,const std::array<dlss::EyeParameters,2>& eyes,const sfs::FramePose& pose){
 if(!requested())return;std::lock_guard lock(mutex);if(!context||!context->ready||context->failed||!pose.framegenEligible)return;
 auto& c=*context;auto& g=c.gpu;if(c.pending.count(pose.serial))return;auto prep=prepared.find(pose.serial);if(prep==prepared.end())return;
 const auto family=commandFamilies.find(cb);if(family==commandFamilies.end()||family->second!=g.dev.graphicsFamily){state("waiting","DLSS capture requires a single-use graphics-queue recording");return;}
 try{
  uint32_t w=eyes[0].resources[2].width,h=eyes[0].resources[2].height;if(!w||!h||w>8192||h>8192)return;
  for(unsigned e=0;e<2;++e)for(unsigned j=0;j<2;++j){const auto& r=eyes[e].resources[j+2];
   if(r.width!=w||r.height!=h||r.range.layerCount!=1){c.rejectedSerial=pose.serial;state("waiting","Native DLSS input dimensions or eye range mismatch");return;}}
  for(auto i=c.pending.begin();i!=c.pending.end();)if(i->first+8<pose.serial&&V(vkGetEventStatus)(g.dev.device,i->second->complete)==VK_EVENT_SET)i=c.pending.erase(i);else ++i;
  std::shared_ptr<Lease> p;for(auto& l:c.leases)if(!l->native&&l.use_count()==1&&l->recorded&&l->width==w&&l->height==h&&V(vkGetEventStatus)(g.dev.device,l->complete)==VK_EVENT_SET){p=l;break;}
  if(!p){if(c.leases.size()>=6){state("fallback","Input pool full; retaining unsubmitted recordings safely");return;}p=std::make_shared<Lease>();c.leases.push_back(p);initializeLease(c,*p,w,h);}
  p->serial=pose.serial;p->metadata=prep->second;p->inputsReady=false;p->submitted=false;p->prefix=layouts[cb];
  p->diagnostic=++c.captureSamples==1||c.captureSamples%240==0;*p->dispatchMapped={0,0,0};
  for(unsigned e=0;e<2;++e)for(unsigned j=0;j<2;++j)p->sources[e][j]=eyes[e].resources[j+2];
  captures[cb].push_back(p);Interop::check(V(vkResetEvent)(g.dev.device,p->complete),"reset FG input event");
  for(unsigned e=0;e<2;++e){p->params[e]=eyes[e].params;auto& eye=*p->eyes[e];
   VkDescriptorImageInfo images[4]{};VkWriteDescriptorSet writes[4]{};
   for(unsigned j=0;j<4;++j){images[j]={j<2?c.sampler:VK_NULL_HANDLE,p->views[e*2+(j%2)],VK_IMAGE_LAYOUT_GENERAL};writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[j].dstSet=p->sets[e];writes[j].dstBinding=j;writes[j].descriptorCount=1;writes[j].descriptorType=j<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[j].pImageInfo=&images[j];}
   V(vkUpdateDescriptorSets)(g.dev.device,4,writes,0,nullptr);
   for(auto* i:{&eye.depth,&eye.motion})g.barrier(cb,i->image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_EXTERNAL,g.dev.graphicsFamily);
   VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};mb.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;V(vkCmdPipelineBarrier)(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,nullptr,0,nullptr);
   V(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,c.pipeline);V(vkCmdBindDescriptorSets)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,c.pipelineLayout,0,1,&p->sets[e],0,nullptr);V(vkCmdDispatchIndirect)(cb,p->dispatch,0);
   for(auto* i:{&eye.depth,&eye.motion})g.barrier(cb,i->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,g.dev.graphicsFamily,VK_QUEUE_FAMILY_EXTERNAL);
  }
  V(vkCmdSetEvent)(cb,p->complete,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);p->recorded=true;c.pending.emplace(pose.serial,p);
 }catch(const std::exception& e){c.failed=true;state("fallback",e.what());}
}
#include "NativeCapture.inc"
namespace {
struct NativeInputCompletion {
 std::unique_ptr<kharvox::GameImageLifetime::Use> borrowed;
 ~NativeInputCompletion(){
  // This is destroyed after generate's FG lock, before its queue lock. Never
  // release borrowed engine handles while their conversion is still in flight.
  const auto start=std::chrono::steady_clock::now();bool waited=false;
  {std::lock_guard lock(mutex);if(context)try{waited=context->gpu.pending[2];context->gpu.waitSlot(2);}catch(const std::exception& e){context->failed=true;state("fallback",e.what());}}
  borrowed.reset();
  static uint64_t samples{};if(waited&&extendedLogging()&&(++samples==1||samples%120==0))log("FRAMEGEN_NATIVE_TIMING deferred=1 completionWaitMs="+std::to_string(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()));
 }
};
}
bool generate(Device& d,const sfs::StereoFrame& pair,uint32_t count,const VkSemaphore* waits,bool& consumed,sfs::StereoFrame& result){
 consumed=false;if(!requested())return false;std::lock_guard<std::recursive_mutex> queueLock(*d.queueMutex);
 NativeInputCompletion nativeCompletion;
 bool nativePath=false;nativeInputs(d,pair,count,waits,consumed,nativePath,nativeCompletion.borrowed);
 std::lock_guard lock(mutex);if(!context||!context->ready||context->failed)return false;
 auto& c=*context;auto& g=c.gpu;
 if(!pair.pose.framegenEligible||d.device!=g.dev.device){c.generator.reset();c.previousPose={};return false;}
 auto found=c.pending.find(pair.pose.serial);if(found==c.pending.end()){c.generator.reset();c.previousPose={};if(!nativePath&&c.rejectedSerial!=pair.pose.serial)state("waiting","No complete DLSS capture for this stereo frame");return false;}
 auto lease=found->second;c.pending.erase(found);if(!lease->inputsReady){c.generator.reset();c.previousPose={};state("waiting","Input layouts unresolved at native queue submission");return false;}
 try{
  const auto retireStart=std::chrono::steady_clock::now();
  // Native capture already retired the preceding generation before queuing its
  // new conversion. Do not immediately drain that conversion on the CPU.
  if(!(lease->native&&g.pending[2]))g.waitAll();c.generator.pollTiming();c.inFlight.reset();
  const auto retired=std::chrono::steady_clock::now();
  if(c.previousNative!=lease->native||c.previousDepthInverted!=lease->depthInverted){c.generator.reset();c.previousPose={};c.previousNative=lease->native;c.previousDepthInverted=lease->depthInverted;log(std::string("FRAMEGEN_SOURCE ")+(lease->native?"native AA=0":"DLSS")+" reverseDepth="+std::to_string(lease->depthInverted));}
  std::array<sfs::Matrix,2> vp{};if(!cameras(c,*lease,vp)){c.generator.reset();c.previousPose={};state("waiting","Actual rendered camera or projection unavailable");return false;}
  colors(c,pair);g.beginVk();c.inFlight=lease;
  // Consume the native waits once. Signal a GPU dependency covering capture
  // and color copy; the DX queue waits without stopping this CPU thread.
  g.barrier(g.command,c.color.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_QUEUE_FAMILY_EXTERNAL,d.graphicsFamily);
  for(unsigned e=0;e<2;++e){const auto& eye=pair.eyes[e];g.barrier(g.command,eye.image,eye.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,eye.layer);
   VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,eye.layer,1};copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstOffset.x=int32_t(e*c.width);copy.extent={c.width,c.height,1};V(vkCmdCopyImage)(g.command,eye.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,c.color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
   g.barrier(g.command,eye.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,eye.layout,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,eye.layer);
  }
  g.barrier(g.command,c.color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,d.graphicsFamily,VK_QUEUE_FAMILY_EXTERNAL);
  for(auto& eye:c.generated){
   if(!eye.initialized)g.barrier(g.command,eye.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
   g.barrier(g.command,eye.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,d.graphicsFamily,VK_QUEUE_FAMILY_EXTERNAL);eye.initialized=true;
  }
  g.submitInput(consumed?0:count,consumed?nullptr:waits,&consumed);
  if(!g.gpuHandoff&&V(vkGetEventStatus)(d.device,lease->complete)!=VK_EVENT_SET){c.generator.reset();c.previousPose={};state("fallback","Input conversion not completed by the native present");return false;}
  const auto inputQueued=std::chrono::steady_clock::now();
  auto tick=GetTickCount64();bool reset=pair.pose.recenterRequested||tick-c.previousTick>250||lease->eyes[0]->capture.camera.reset||lease->eyes[1]->capture.camera.reset;
  if(c.previousPose.serial){auto a=c.previousPose.predictedHead.position,b=pair.pose.predictedHead.position;reset=reset||std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)>1.f;}
  g.beginDx();bool interpolated=c.generator.record(g.list.Get(),c.color.dx.Get(),lease->eyes,{pair.pose.serial,c.width,c.height,true,true,reset});
  if(interpolated)for(unsigned e=0;e<2;++e){auto& dst=c.generated[e];
   D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={dst.dx.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET};g.list->ResourceBarrier(1,&b);
   const auto df=dst.dx->GetDesc().Format;c.generator.compositeEyeTo(g.list.Get(),dst.dx.Get(),df==DXGI_FORMAT_B8G8R8A8_UNORM?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,e);
   std::swap(b.Transition.StateBefore,b.Transition.StateAfter);g.list->ResourceBarrier(1,&b);
  }
  g.submitDx(!g.gpuHandoff);if(!g.gpuHandoff)c.generator.pollTiming();
  const auto dxQueued=std::chrono::steady_clock::now();
  auto actual=pair.pose;XrPosef rendered{};
  if(camera::renderedHead(pair.pose.serial,rendered)){auto views=stereoProjectionViews(pair,XR_NULL_HANDLE,pair.eyes[0].extent);rebaseProjectionViews(views,actual.predictedHead,rendered);for(unsigned e=0;e<2;++e)actual.views[e].pose=views[e].pose;actual.predictedHead=rendered;}
  const auto previous=c.previousPose;c.previousPose=actual;c.previousVP=vp;c.previousTick=tick;
  g.beginVk(1);
  // Always reacquire, including history-prime frames, so the next release is
  // balanced. The output queue dependency covers both NGX and eye composition.
  for(auto& dst:c.generated)g.barrier(g.command,dst.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_EXTERNAL,d.graphicsFamily);
  g.submitOutput();
  if(!interpolated||!previous.serial)return false;
  result=pair;result.pose.generated=true;
  result.pose.predictedHead=midpoint(previous.predictedHead,actual.predictedHead);
  for(unsigned e=0;e<2;++e){auto& dst=c.generated[e];
   result.pose.views[e].pose=midpoint(previous.views[e].pose,actual.views[e].pose);result.eyes[e].image=dst.image;result.eyes[e].layout=VK_IMAGE_LAYOUT_GENERAL;result.eyes[e].layer=0;result.eyes[e].pose=result.pose.views[e].pose;
  }
  if(extendedLogging()&&(c.generatedSamples==0||c.generatedSamples%120==0)){
   const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
   log("FRAMEGEN_INTEROP_TIMING gpuHandoff="+std::to_string(g.gpuHandoff)+" retireMs="+std::to_string(ms(retireStart,retired))+
       " inputPrepareMs="+std::to_string(ms(retired,inputQueued))+" dxRecordSubmitMs="+std::to_string(ms(inputQueued,dxQueued))+
       " directEyeOutput=1 outputRecordSubmitMs="+std::to_string(ms(dxQueued,std::chrono::steady_clock::now()))+" gpuTimingPreviousCompleted="+std::to_string(g.gpuHandoff));
  }
  if(++c.generatedSamples==1||c.generatedSamples%120==0)log("FRAMEGEN_2X generated serial="+std::to_string(pair.pose.serial)+" gpuMs="+std::to_string(c.generator.gpuMs)+" eyes=2");return true;
 }catch(const std::exception& e){c.failed=true;state("fallback",e.what());return false;}
}
void reset(){if(!requested())return;std::lock_guard lock(mutex);if(context){context->generator.reset();context->previousPose={};}}
void destroyIdle(VkDevice device){std::lock_guard lock(mutex);if(context&&context->gpu.dev.device!=device)return;
 onBegin=nullptr;onBarriers=nullptr;onWrites=nullptr;onTargets=nullptr;onImageDestroyed=nullptr;nativeTargets.clear();
 if(!context){layouts.clear();commandFamilies.clear();prepared.clear();captures.clear();submittedLayouts.clear();commandWrites.clear();submittedWrites.clear();return;}
 auto& c=*context;auto& g=c.gpu;
 try{g.waitAll();c.inFlight.reset();c.generator.destroyIdle();for(auto& p:c.leases){if(p->dispatchMapped)V(vkUnmapMemory)(device,p->dispatchMemory);if(p->dispatch)V(vkDestroyBuffer)(device,p->dispatch,nullptr);if(p->dispatchMemory)V(vkFreeMemory)(device,p->dispatchMemory,nullptr);for(auto v:p->views)if(v)V(vkDestroyImageView)(device,v,nullptr);if(p->descriptors)V(vkDestroyDescriptorPool)(device,p->descriptors,nullptr);if(p->complete)V(vkDestroyEvent)(device,p->complete,nullptr);for(auto& eye:p->eyes){g.destroy(eye->depth);g.destroy(eye->motion);}}
 g.destroy(c.color);for(auto& i:c.generated)g.destroy(i);if(c.pipeline)V(vkDestroyPipeline)(device,c.pipeline,nullptr);if(c.pipelineLayout)V(vkDestroyPipelineLayout)(device,c.pipelineLayout,nullptr);if(c.setLayout)V(vkDestroyDescriptorSetLayout)(device,c.setLayout,nullptr);if(c.sampler)V(vkDestroySampler)(device,c.sampler,nullptr);g.destroyIdle();}catch(const std::exception& e){log(std::string("FRAMEGEN shutdown: ")+e.what());}
 context.reset();layouts.clear();commandFamilies.clear();prepared.clear();captures.clear();submittedLayouts.clear();commandWrites.clear();submittedWrites.clear();
}
#undef V
}
