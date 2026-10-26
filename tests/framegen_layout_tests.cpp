#include "../src/framegen/InputLayouts.h"
#include "../src/framegen/DeviceFeatures.h"
#include "../src/framegen/NativeMotionSources.h"
#include "../src/framegen/DepthProjection.h"
#include <iostream>
#include <stdexcept>
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
 using namespace argent::fg;
 VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};indexing.runtimeDescriptorArray=VK_TRUE;
 VkLayerDeviceCreateInfo loader{};loader.sType=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;loader.pNext=&indexing;loader.function=VK_LOADER_DATA_CALLBACK;
 DescriptorFeatureRequest enabled(&loader,true);check(enabled.enabled(),"queue descriptor feature enabled");
 auto clonedLoader=static_cast<const VkLayerDeviceCreateInfo*>(enabled.head());check(clonedLoader!=&loader&&clonedLoader->function==loader.function,"loader prefix preserved in owned storage");
 auto clonedIndex=static_cast<const VkPhysicalDeviceDescriptorIndexingFeatures*>(clonedLoader->pNext);check(clonedIndex->runtimeDescriptorArray&&clonedIndex->descriptorBindingSampledImageUpdateAfterBind,"other engine features preserved");
 check(!indexing.descriptorBindingSampledImageUpdateAfterBind&&loader.pNext==&indexing,"application chain unchanged");
 DescriptorFeatureRequest disabled(&loader,false);check(disabled.head()==&loader&&!disabled.enabled(),"unsupported feature does not modify chain");
 VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO,reinterpret_cast<const VkBaseInStructure*>(&indexing)};DescriptorFeatureRequest refused(&unknown,true);check(!refused.enabled()&&refused.head()==&unknown,"unknown prefix fails closed");
 VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};v12.timelineSemaphore=VK_TRUE;DescriptorFeatureRequest core(&v12,true);check(core.enabled()&&static_cast<const VkPhysicalDeviceVulkan12Features*>(core.head())->timelineSemaphore,"core 1.2 patched without duplicate extension node");
 VkLayerDeviceCreateInfo emptyLoader{};emptyLoader.sType=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;DescriptorFeatureRequest absent(&emptyLoader,true);check(absent.enabled()&&static_cast<const VkBaseInStructure*>(absent.head())->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,"new feature inserted after loader prefix");
 auto image=reinterpret_cast<VkImage>(uintptr_t(1));
 VkImageSubresourceRange both{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2},right=both;right.baseArrayLayer=1;right.layerCount=1;
 auto left=right;left.baseArrayLayer=0;
 std::vector<Layout> layouts;
 recordLayout(layouts,{image,both,VK_IMAGE_LAYOUT_GENERAL});
 recordLayout(layouts,{image,right,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL});
 check(observedLayout(layouts,image,right)==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"new eye layout overrides old whole-image layout");
 check(observedLayout(layouts,image,left)==VK_IMAGE_LAYOUT_GENERAL,"unmodified eye retained");
 check(observedLayout(layouts,image,both)==VK_IMAGE_LAYOUT_UNDEFINED,"mixed layouts cannot masquerade as old shared layout");
 auto depth=both;depth.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
 check(observedLayout(layouts,image,depth)==VK_IMAGE_LAYOUT_UNDEFINED,"aspect mismatch rejected");
 recordLayout(layouts,{image,both,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
 check(layouts.size()==1,"overwritten ranges retired");
 check(observedLayout(layouts,image,right)==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"both eyes readable after implicit pass end");
 auto remaining=both;remaining.layerCount=VK_REMAINING_ARRAY_LAYERS;remaining.levelCount=VK_REMAINING_MIP_LEVELS;
 check(covers(remaining,right),"remaining layers and mips supported");
 VkAttachmentDescription attachments[3]{};attachments[0].finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;attachments[1].finalLayout=VK_IMAGE_LAYOUT_GENERAL;attachments[2].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
 VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},input{1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
 VkSubpassDescription sub{};sub.colorAttachmentCount=1;sub.pColorAttachments=&color;sub.inputAttachmentCount=1;sub.pInputAttachments=&input;
 VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=3;pass.pAttachments=attachments;pass.subpassCount=1;pass.pSubpasses=&sub;
 auto finals=finalLayouts(pass);check(finals[2]==VK_IMAGE_LAYOUT_UNDEFINED,"unused attachment not transitioned");
 auto writesOnly=finalLayouts(pass,true);check(writesOnly[0]!=VK_IMAGE_LAYOUT_UNDEFINED&&writesOnly[1]==VK_IMAGE_LAYOUT_UNDEFINED,"read-only input is not a producer");
 VkAttachmentReference roDepth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};sub.pDepthStencilAttachment=&roDepth;
 check(finalLayouts(pass,true)[1]==VK_IMAGE_LAYOUT_UNDEFINED,"read-only depth is not a producer");
 roDepth.layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;check(finalLayouts(pass,true)[1]!=VK_IMAGE_LAYOUT_UNDEFINED,"writable depth is a producer");sub.pDepthStencilAttachment=nullptr;
 std::vector<Layout> views(3,{image,both,VK_IMAGE_LAYOUT_UNDEFINED});auto transitions=finalTransitions(finals,views,true);
 check(transitions.size()==2&&transitions[0].subresourceRange.layerCount==2,"multiview final layouts cover both eyes");
 check(finalTransitions(finals,views,false)[0].subresourceRange.layerCount==1,"mono pass cannot promote untouched right layer");
 std::vector<Layout> prefix;
 check(inputLayout(prefix,layouts,image,right)==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"separate DLSS command inherits submitted producer layout");
 recordLayout(prefix,{image,right,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL});
 check(inputLayout(prefix,layouts,image,right)==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"local transition overrides submitted history");
 check(inputLayout(prefix,layouts,image,both)==VK_IMAGE_LAYOUT_UNDEFINED,"partial local state cannot inherit stale whole-image layout");
 std::vector<Layout> otherQueue;check(inputLayout({},otherQueue,image,left)==VK_IMAGE_LAYOUT_UNDEFINED,"unobserved queue cannot inherit other queue state");
 layouts.clear();check(observedLayout(layouts,image,left)==VK_IMAGE_LAYOUT_UNDEFINED,"new command cannot reuse stale layout");
 // A bounded fake address space exercises exactly the audited idImage fields.
 std::array<unsigned char,8192> memory{};const uintptr_t base=0x10000;
 auto put=[&](size_t offset,const auto& value){std::memcpy(memory.data()+offset,&value,sizeof(value));};
 auto read=[&](uintptr_t a,void* out,size_t size){if(a<base||size>memory.size()||a-base>memory.size()-size)return false;std::memcpy(out,memory.data()+a-base,size);return true;};
 for(unsigned i=0;i<2;++i){const size_t o=0x400+i*0x200;put(0x360+i*8,base+o);put(o+8,base+0x900+i*32);put(o+0xc8,base+o);put(o+0xd0,uintptr_t(100+i));put(o+0xe8,uintptr_t(200+i));put(o+0x64,uint32_t(1920));put(o+0x68,uint32_t(1080));put(o+0x128,VK_FORMAT_R16G16_SFLOAT);std::strcpy(reinterpret_cast<char*>(memory.data()+0x900+i*32),i?"_motionvector1":"_motionvector0");}
 std::array<argent::dlss::Resource,2> native{};check(nativeMotionSources(base,read,native)&&native[1].width==1920&&native[0].range.layerCount==2,"both native ping-pong resources decoded");
 const auto queue=reinterpret_cast<VkQueue>(uintptr_t(1));NativeWrite produced{{native[0].image,native[0].range,VK_IMAGE_LAYOUT_GENERAL},9,1,queue};
 check(currentNativeWrite(produced,native[0],9,queue),"same-frame stereo producer accepted");
 check(!currentNativeWrite(produced,native[0],10,queue),"previous frame motion rejected");
 check(!currentNativeWrite(produced,native[0],9,reinterpret_cast<VkQueue>(uintptr_t(2))),"other queue motion rejected");
 check(!currentNativeWrite(produced,native[1],9,queue),"other ping-pong image rejected");
 produced.source.range.layerCount=1;check(!currentNativeWrite(produced,native[0],9,queue),"left-only producer rejected");
 for(unsigned i=0;i<2;++i){const size_t o=0x400+i*0x200,table=0x1000+i*0x400,views=0x1800+i*0x100;
  put(o+0x9c,uint32_t(0x307));put(o+0xe8,base+table);put(o+0xd0,base+views);put(table,int32_t(2));put(table+0x108+2*8,uintptr_t(2000+i));put(views+2*8,uintptr_t(3000+i));}
 check(nativeMotionSources(base,read,native)&&native[0].image==reinterpret_cast<VkImage>(uintptr_t(2000))&&native[1].view==reinterpret_cast<VkImageView>(uintptr_t(3001)),"resolution-array handles dereferenced together");
 put(0x1000,int32_t(32));check(!nativeMotionSources(base,read,native),"resolution index overflow rejected");put(0x1000,int32_t(-1));check(!nativeMotionSources(base,read,native),"negative resolution index rejected");put(0x1000,int32_t(2));
 unsigned indexReads=0;auto racingRead=[&](uintptr_t a,void* out,size_t size){if(!read(a,out,size))return false;if(a==base+0x1000&&++indexReads==2){int32_t changed=3;std::memcpy(out,&changed,4);}return true;};check(!nativeMotionSources(base,racingRead,native),"changing image/view selector rejected");
 put(0x400+0xc8,base+0x600);check(!nativeMotionSources(base,read,native),"unsupported cross-object alias rejected");put(0x400+0xc8,base+0x400);
 put(0x600+0x128,VK_FORMAT_R32_SFLOAT);check(!nativeMotionSources(base,read,native),"unexpected native motion format rejected");put(0x600+0x128,VK_FORMAT_R16G16_SFLOAT);
 memory[0x900]='x';check(!nativeMotionSources(base,read,native),"wrong object name rejected");check(!nativeMotionSources(base+memory.size(),read,native),"unreadable object rejected");
 std::array<float,16> projection{};projection[0]=1;projection[5]=-1;projection[10]=-100.f/99.9f;projection[11]=-1;projection[14]=-10.f/99.9f;
 float nearPlane{},farPlane{};const auto original=projection;
 check(depthProjection(projection,true,nearPlane,farPlane)&&projection[14]>0&&std::abs(nearPlane-.1f)<.0001f&&std::abs(farPlane-100.f)<.01f,"reverse depth projection preserves near/far planes");
 const auto projected=[&](float z){return (projection[10]*-z+projection[14])/z;};check(std::abs(projected(.1f)-1)<.0001f&&std::abs(projected(100))<.0001f,"reverse Z maps near to one and far to zero");
 check(depthProjection(projection,false,nearPlane,farPlane)&&std::abs(projection[10]-original[10])<.0001f&&projection[14]==original[14],"forward/reverse conversion roundtrip");
 projection[10]=-1;projection[14]=-.1f;check(depthProjection(projection,true,nearPlane,farPlane)&&farPlane==1000000.f,"infinite reverse projection supported");projection[14]=0;check(!depthProjection(projection,true,nearPlane,farPlane),"degenerate projection rejected");
 std::cout<<"PASS: implicit render-pass layouts, eye/aspect isolation, overlap precedence and command reset\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
