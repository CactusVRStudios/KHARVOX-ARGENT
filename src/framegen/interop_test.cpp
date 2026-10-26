// End-to-end synthetic Vulkan capture -> D3D12 DLSS-G -> Vulkan stereo test.
#include "FrameGeneration.h"
#include "Interop.h"
#include "InputLayouts.h"
#include "DeviceFeatures.h"
#include "../EternalCameraHook.h"
#include "../hands/HandSceneDepthTracker.h"
#include "interop_native_fixture.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <future>
namespace argent {void log(const std::string& s){std::cout<<s<<std::endl;}}
namespace argent::camera {
Stats stats() noexcept{return {};}
bool renderedWorldCamera(uint64_t,float* origin,float* axis,float* projection) noexcept {const float b[]={1,0,0,0,1,0,0,0,1};std::fill(origin,origin+3,0.f);std::copy(b,b+9,axis);std::fill(projection,projection+16,0.f);projection[0]=1;projection[5]=-1;projection[10]=-1.0001f;projection[11]=-1;projection[14]=-.1f;return true;}
bool renderedHead(uint64_t,XrPosef& p) noexcept {p={{0,0,0,1},{0,0,0}};return true;}
}
namespace argent::sfs {bool framegenEyeInputs(VkDevice,const std::array<dlss::Resource,2>&,std::array<dlss::EyeParameters,2>&){return false;}}
namespace kharvox::hands {bool handSceneTargetForColor(VkImage,VkExtent2D,HandSceneTarget&){return false;}}
static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int wmain(int argc,wchar_t** argv){try{
 check(argc>=2,"Usage: ArgentFramegenInteropTest <framegen-directory> [--cpu-fallback] [--native-inputs] [--bgra]");bool fallback=false,nativeTest=false,bgra=false;
 for(int a=2;a<argc;++a){const std::wstring option=argv[a];if(option==L"--cpu-fallback")fallback=true;else if(option==L"--native-inputs")nativeTest=true;else if(option==L"--bgra")bgra=true;else check(false,"Unknown test option");}
 const auto colorVk=bgra?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM;const auto colorDx=bgra?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
 SetEnvironmentVariableA("ARGENT_FRAMEGEN_2X","1");SetEnvironmentVariableW(L"ARGENT_FG_DIRECTORY",std::filesystem::absolute(argv[1]).c_str());
 auto loader=LoadLibraryW(L"vulkan-1.dll");check(loader,"Vulkan loader");argent::Device d;d.gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ii.pApplicationInfo=&app;
 auto create=reinterpret_cast<PFN_vkCreateInstance>(d.gipa(nullptr,"vkCreateInstance"));argent::fg::Interop::check(create(&ii,nullptr,&d.instance),"test instance");
#define I(n) reinterpret_cast<PFN_##n>(d.gipa(d.instance,#n))
 uint32_t count{};I(vkEnumeratePhysicalDevices)(d.instance,&count,nullptr);std::vector<VkPhysicalDevice> devices(count);I(vkEnumeratePhysicalDevices)(d.instance,&count,devices.data());
 for(auto p:devices){VkPhysicalDeviceProperties props{};I(vkGetPhysicalDeviceProperties)(p,&props);if(props.vendorID!=0x10de)continue;d.physical=p;std::cout<<props.deviceName<<'\n';break;}check(d.physical,"NVIDIA GPU");
 uint32_t n{};I(vkGetPhysicalDeviceQueueFamilyProperties)(d.physical,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);I(vkGetPhysicalDeviceQueueFamilyProperties)(d.physical,&n,families.data());bool family=false;for(uint32_t i=0;i<n;++i)if((families[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){d.graphicsFamily=i;family=true;break;}check(family,"graphics/compute queue");
 float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=d.graphicsFamily;qi.queueCount=1;qi.pQueuePriorities=&priority;const char* extensions[]={VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};argent::fg::DescriptorFeatureRequest request(&indexing,true);VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,request.head()};d.framegenUpdateAfterBind=request.enabled();di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.enabledExtensionCount=3;d.framegenExternalSemaphore=!fallback;di.ppEnabledExtensionNames=extensions;
 argent::fg::Interop::check(I(vkCreateDevice)(d.physical,&di,nullptr,&d.device),"test device");d.gdpa=I(vkGetDeviceProcAddr);
#define V(n) d.proc<PFN_##n>(#n)
 V(vkGetDeviceQueue)(d.device,d.graphicsFamily,0,&d.graphicsQueue);
 argent::fg::Interop g;g.initialize(d);check(g.gpuHandoff==!fallback,"selected handoff path available");
 if(g.gpuHandoff){
  // Hold the DX queue to prove CPU submission can finish before either API's
  // consumer has finished, while the Vulkan output cannot run prematurely.
  Microsoft::WRL::ComPtr<ID3D12Fence> hold;
  argent::fg::Interop::check(g.dx->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&hold)),"test gate fence");
  argent::fg::Interop::check(g.queue->Wait(hold.Get(),1),"test queue gate");
  std::thread release([hold]{std::this_thread::sleep_for(std::chrono::milliseconds(250));hold->Signal(1);});
  const auto start=std::chrono::steady_clock::now();
  bool blocked=false;double queuedMs{};
  try{
   g.beginVk();g.submitInput(0,nullptr,nullptr);
   g.beginDx();g.submitDx(false);g.beginVk(1);g.submitOutput();
   queuedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   blocked=V(vkGetFenceStatus)(d.device,g.fence)==VK_NOT_READY&&g.done->GetCompletedValue()<g.value;
   g.waitAll();
  }catch(...){release.join();throw;}
  release.join();check(queuedMs<200&&blocked,"CPU queues ahead but output waits for DX signal");
  check(g.done->GetCompletedValue()==g.value,"cross-API chain completed before reuse");
  std::cout<<"PASS: blocked DX queue; CPU handoff "<<queuedMs<<" ms, Vulkan output correctly held until shared fence signal\n";
 }
 check(!argent::fg::ready(),"requested FrameGen cannot defer XR before initialization");
 argent::fg::attach(d);
 check(argent::fg::ready(),"successful initialization enables FG lifecycle");
 constexpr uint32_t w=640,h=480;
 NativeFixture native;if(nativeTest)native.create(g,w,h);
 std::array<argent::fg::SharedImage,2> color,depth,motion;std::array<VkImageView,4> views{};
 for(unsigned e=0;e<2;++e){g.make(color[e],w,h,colorVk,colorDx,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,D3D12_RESOURCE_FLAG_NONE);g.make(depth[e],w,h,VK_FORMAT_R32_SFLOAT,DXGI_FORMAT_R32_FLOAT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);g.make(motion[e],w,h,VK_FORMAT_R32G32B32A32_SFLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
 for(unsigned j=0;j<2;++j){VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=j?motion[e].image:depth[e].image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=j?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R32_SFLOAT;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};argent::fg::Interop::check(V(vkCreateImageView)(d.device,&vi,nullptr,&views[e*2+j]),"test native view");}}
 VkAttachmentDescription attachments[2]{};VkAttachmentReference references[2]{};
 for(unsigned j=0;j<2;++j){auto& a=attachments[j];a.format=j?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R32_SFLOAT;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;references[j]={j,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};}
 VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=2;sub.pColorAttachments=references;
 VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=2;rp.pAttachments=attachments;rp.subpassCount=1;rp.pSubpasses=&sub;VkRenderPass pass{};argent::fg::Interop::check(V(vkCreateRenderPass)(d.device,&rp,nullptr,&pass),"implicit-layout test pass");std::array<VkFramebuffer,2> framebuffers{};
 for(unsigned e=0;e<2;++e){VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=pass;fi.attachmentCount=2;fi.pAttachments=&views[e*2];fi.width=w;fi.height=h;fi.layers=1;argent::fg::Interop::check(V(vkCreateFramebuffer)(d.device,&fi,nullptr,&framebuffers[e]),"test framebuffer");}
 argent::sfs::StereoFrame result;unsigned generatedCount=0;
 for(unsigned frame=0;frame<8;++frame){
  argent::sfs::FramePose pose;pose.serial=frame+1;pose.displayTime=(frame+1)*11111111;pose.framegenEligible=true;pose.predictedHead={{0,0,0,1},{0,0,0}};for(unsigned e=0;e<2;++e){pose.views[e]={XR_TYPE_VIEW};pose.views[e].pose={{0,0,0,1},{e?.03f:-.03f,0,0}};pose.views[e].fov={-.8f,.8f,.8f,-.8f};}
  argent::sfs::Matrix projection{};projection[0]=1;projection[5]=-1;projection[10]=-1.0001f;projection[11]=-1;projection[14]=-.1f;argent::sfs::EyeUniforms uniforms;check(argent::sfs::parallelEyeProjection(projection,pose.views,1,uniforms),"test eye projection");argent::fg::prepare(pose,uniforms,projection);
  g.beginVk();argent::fg::beginCommand(g.command,d.graphicsFamily);std::array<argent::dlss::EyeParameters,2> parameters;argent::sfs::StereoFrame pair;pair.pose=pose;pair.generation=pose.serial;
  for(unsigned e=0;e<2;++e){auto& eye=parameters[e];eye.params.set<float>(0x3c,float(w));eye.params.set<float>(0x40,float(h));eye.params.set<int>(0x38,frame==0);
   g.barrier(g.command,color[e].image,frame?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
   VkClearColorValue clear{};clear.float32[e]=.7f;clear.float32[2]=.2f;clear.float32[3]=1;
   VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};V(vkCmdClearColorImage)(g.command,color[e].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);g.barrier(g.command,color[e].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
   VkClearValue values[2]{};values[0].color.float32[0]=.5f;
   VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffers[e];begin.renderArea.extent={w,h};begin.clearValueCount=2;begin.pClearValues=values;
   V(vkCmdBeginRenderPass)(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);V(vkCmdEndRenderPass)(g.command);
   // Exactly the metadata path used by SFS. No explicit barrier exists for these inputs.
   std::vector<argent::fg::Layout> inputViews;
   for(unsigned j=0;j<2;++j){auto& r=eye.resources[j+2];r.image=j?motion[e].image:depth[e].image;r.view=views[e*2+j];r.range=range;r.width=w;r.height=h;r.format=attachments[j].format;inputViews.push_back({r.image,range,VK_IMAGE_LAYOUT_UNDEFINED});}
   auto finals=argent::fg::finalTransitions(argent::fg::finalLayouts(rp),inputViews,false);argent::fg::imageBarriers(g.command,uint32_t(finals.size()),finals.data());
   pair.eyes[e]={color[e].image,{w,h},colorVk,VK_IMAGE_LAYOUT_GENERAL,pose.views[e].pose,pose.views[e].fov,0,pose.serial};
  }
  argent::fg::submitting(d.graphicsQueue,1,&g.command);g.submitVk();
  const bool useNative=nativeTest&&(frame<4||frame>=6);
  g.beginVk();argent::fg::beginCommand(g.command,d.graphicsFamily);
  if(useNative)native.record(g);else argent::fg::capture(g.command,parameters,pose);
  // Reproduce Eternal: capture recorded under the old pose, then acquire installs
  // the next frame's uniforms before the recorded work is submitted.
  auto installed=pose;installed.serial=pose.serial+1;argent::fg::prepare(installed,uniforms,projection);pair.pose=installed;pair.generation=installed.serial;for(auto& e:pair.eyes)e.serial=installed.serial;
  argent::fg::submitting(d.graphicsQueue,1,&g.command,&installed);g.submitVk();bool consumed=false;
  VkSemaphore nativeWait{};VkEvent producerGate{};std::future<void> releaseGate;
  if(useNative){
   if(frame==2&&g.gpuHandoff){
    VkEventCreateInfo info{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};argent::fg::Interop::check(V(vkCreateEvent)(d.device,&info,nullptr,&producerGate),"native producer gate");
    g.beginVk(2);V(vkCmdWaitEvents)(g.command,1,&producerGate,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,nullptr,0,nullptr,0,nullptr);g.submitNative(0,nullptr,nullptr);
    const auto setEvent=V(vkSetEvent);releaseGate=std::async(std::launch::async,[=]{std::this_thread::sleep_for(std::chrono::milliseconds(250));setEvent(d.device,producerGate);});
   }
   VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};argent::fg::Interop::check(V(vkCreateSemaphore)(d.device,&si,nullptr,&nativeWait),"native fixture semaphore");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&nativeWait;argent::fg::Interop::check(V(vkQueueSubmit)(d.graphicsQueue,1,&submit,VK_NULL_HANDLE),"native producer signal");
   const auto captureStart=std::chrono::steady_clock::now();
   check(argent::fg::captureNativeInputs(d,pair,native.resources,native.eyes,{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},1,&nativeWait,consumed,true,true)&&consumed,"reverse-depth native inputs converted and producer wait consumed once");
   if(producerGate){const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-captureStart).count();check(elapsed<200&&V(vkGetFenceStatus)(d.device,g.fences[2])==VK_NOT_READY,"native conversion queues without waiting for blocked producer");std::cout<<"PASS: native capture queued in "<<elapsed<<" ms while producer still blocked\n";}

  }
  const bool made=argent::fg::generate(d,pair,0,nullptr,consumed,result);check(consumed,"native color waits consumed");
  if(nativeWait)V(vkDestroySemaphore)(d.device,nativeWait,nullptr);
  if(producerGate){releaseGate.get();g.waitSlot(2);V(vkDestroyEvent)(d.device,producerGate,nullptr);}
  if(made)++generatedCount;
  const bool switched=nativeTest&&(frame==4||frame==6);if(!frame||switched)check(!made,"first frame and source changes must prime history");else if(frame>=3)check(made,"Vulkan->D3D12->Vulkan generation after initialization");
  std::cout<<"Interop frame "<<frame<<" generated="<<made<<'\n';
 }
 check(generatedCount>=4,"sustained stereo interpolation after shader compilation");
 // Verify actual generated pixels after all ownership transfers.
 VkBuffer buffer{};VkDeviceMemory memory{};VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=w*h*4*2;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;argent::fg::Interop::check(V(vkCreateBuffer)(d.device,&bi,nullptr,&buffer),"readback buffer");VkMemoryRequirements req{};V(vkGetBufferMemoryRequirements)(d.device,buffer,&req);VkPhysicalDeviceMemoryProperties mp{};I(vkGetPhysicalDeviceMemoryProperties)(d.physical,&mp);uint32_t type=UINT32_MAX;for(uint32_t t=0;t<mp.memoryTypeCount;++t)if((req.memoryTypeBits&(1u<<t))&&(mp.memoryTypes[t].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=t;break;}check(type!=UINT32_MAX,"readback type");VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;argent::fg::Interop::check(V(vkAllocateMemory)(d.device,&ai,nullptr,&memory),"readback memory");argent::fg::Interop::check(V(vkBindBufferMemory)(d.device,buffer,memory,0),"readback binding");
 g.beginVk();for(unsigned e=0;e<2;++e){g.barrier(g.command,result.eyes[e].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy{};copy.bufferOffset=uint64_t(e)*w*h*4;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};V(vkCmdCopyImageToBuffer)(g.command,result.eyes[e].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);}g.submitVk();unsigned char* pixels{};argent::fg::Interop::check(V(vkMapMemory)(d.device,memory,0,VK_WHOLE_SIZE,0,reinterpret_cast<void**>(&pixels)),"readback map");auto left=pixels+(w*h/2+w/2)*4,right=left+w*h*4;const auto red=bgra?2:0;std::cout<<"RGBA L="<<unsigned(left[red])<<','<<unsigned(left[1])<<" R="<<unsigned(right[red])<<','<<unsigned(right[1])<<'\n';check(left[red]>left[1]+60&&right[1]>right[red]+60,"distinct generated eyes survive external memory roundtrip");V(vkUnmapMemory)(d.device,memory);
 V(vkDeviceWaitIdle)(d.device);argent::fg::destroyIdle(d.device);if(nativeTest)native.destroy(g);for(auto fb:framebuffers)V(vkDestroyFramebuffer)(d.device,fb,nullptr);V(vkDestroyRenderPass)(d.device,pass,nullptr);for(auto v:views)V(vkDestroyImageView)(d.device,v,nullptr);for(unsigned e=0;e<2;++e){g.destroy(color[e]);g.destroy(depth[e]);g.destroy(motion[e]);}V(vkDestroyBuffer)(d.device,buffer,nullptr);V(vkFreeMemory)(d.device,memory,nullptr);g.destroyIdle();V(vkDestroyDevice)(d.device,nullptr);I(vkDestroyInstance)(d.instance,nullptr);
 std::cout<<"PASS: queue-inherited render-pass layouts, deferred descriptors, conversion, per-eye NGX, external ownership and final Vulkan pixels. No XR delivery claimed.\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
