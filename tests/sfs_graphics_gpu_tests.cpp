#include "../src/PerformanceDiagnostics.h"
#include <windows.h>
#include "../src/StereoReadback.h"
#ifdef ARGENT_TEST_OPENXR
#include "../src/QuadRuntime.h"
#endif
#include "../src/sfs/ShaderIdentity.h"
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/SourceRing.h"
#include "../src/openxr/CopyGpuTiming.h"
#ifdef KHARVOX_SFS_RING_RUNTIME
#include "../src/sfs/NativeSfs.h"
#endif
#include <iostream>
#include <string>
#include <stdexcept>
#include <cmath>
#include <thread>
#include <atomic>
#include <fstream>
#include <future>
#include "../src/openxr/GameImageLifetime.h"
#ifdef KHARVOX_SFS_RING_RUNTIME
static PFN_vkGetDeviceProcAddr driverResolver{};
static VkResult retirementFailure=VK_SUCCESS;
static unsigned parameterMaps{};
// Exercise the real SFS hooks without requiring a ray-tracing-capable test GPU.
// Only the synthetic ray commands are consumed here; graphics/compute still
// execute on the driver and are checked by the image/compute readbacks below.
static unsigned nativePipelineBinds{},nativeSetBinds{},nativePushes{};
static VkDevice testDevice{};
static VkPipeline lastGraphicsPipeline{};
static VkDescriptorSet lastGraphicsSet{};
static VKAPI_ATTR void VKAPI_CALL testBindPipeline(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipeline pipeline){
 if(point==VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR){++nativePipelineBinds;return;}
 if(point==VK_PIPELINE_BIND_POINT_GRAPHICS)lastGraphicsPipeline=pipeline;
 reinterpret_cast<PFN_vkCmdBindPipeline>(driverResolver(testDevice,"vkCmdBindPipeline"))(cb,point,pipeline);
}
static VKAPI_ATTR void VKAPI_CALL testBindSets(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,const VkDescriptorSet* sets,uint32_t dynamicCount,const uint32_t* dynamic){
 if(point==VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR){++nativeSetBinds;return;}
 if(point==VK_PIPELINE_BIND_POINT_GRAPHICS&&first==0&&count)lastGraphicsSet=sets[0];
 reinterpret_cast<PFN_vkCmdBindDescriptorSets>(driverResolver(testDevice,"vkCmdBindDescriptorSets"))(cb,point,layout,first,count,sets,dynamicCount,dynamic);
}
static VKAPI_ATTR void VKAPI_CALL testPush(VkCommandBuffer cb,VkPipelineLayout layout,VkShaderStageFlags flags,uint32_t offset,uint32_t size,const void* data){
 if(flags==VK_SHADER_STAGE_RAYGEN_BIT_KHR){++nativePushes;return;}
 reinterpret_cast<PFN_vkCmdPushConstants>(driverResolver(testDevice,"vkCmdPushConstants"))(cb,layout,flags,offset,size,data);
}
// Hold an unrelated metadata writer inside the driver to prove that warmed
// recording paths do not wait for the global SFS resource lock.
static std::atomic<bool> holdNextPass{};
static std::promise<void>* heldPassEntered{};
static std::shared_future<void> heldPassRelease;
static VKAPI_ATTR VkResult VKAPI_CALL testCreatePass(VkDevice device,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks* callbacks,VkRenderPass* pass){
 if(holdNextPass.exchange(false)){heldPassEntered->set_value();heldPassRelease.wait();}
 return reinterpret_cast<PFN_vkCreateRenderPass>(driverResolver(device,"vkCreateRenderPass"))(device,info,callbacks,pass);
}
static VKAPI_ATTR VkResult VKAPI_CALL testRetirement(VkDevice device){
 if(retirementFailure!=VK_SUCCESS)return retirementFailure;
 return reinterpret_cast<PFN_vkDeviceWaitIdle>(driverResolver(device,"vkDeviceWaitIdle"))(device);
}
static VKAPI_ATTR VkResult VKAPI_CALL testMap(VkDevice device,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void** data){
 ++parameterMaps;
 return reinterpret_cast<PFN_vkMapMemory>(driverResolver(device,"vkMapMemory"))(device,memory,offset,size,flags,data);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL testResolver(VkDevice device,const char* name){
 if(!std::strcmp(name,"vkCmdBindPipeline"))return reinterpret_cast<PFN_vkVoidFunction>(testBindPipeline);
 if(!std::strcmp(name,"vkCmdBindDescriptorSets"))return reinterpret_cast<PFN_vkVoidFunction>(testBindSets);
 if(!std::strcmp(name,"vkCmdPushConstants"))return reinterpret_cast<PFN_vkVoidFunction>(testPush);
 if(!std::strcmp(name,"vkDeviceWaitIdle"))return reinterpret_cast<PFN_vkVoidFunction>(testRetirement);
 if(!std::strcmp(name,"vkMapMemory"))return reinterpret_cast<PFN_vkVoidFunction>(testMap);
 if(!std::strcmp(name,"vkCreateRenderPass"))return reinterpret_cast<PFN_vkVoidFunction>(testCreatePass);
 return driverResolver(device,name);
}
#endif
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void ok(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("Vulkan result "+std::to_string(result));}
#ifndef ARGENT_TEST_OPENXR
static unsigned performanceGpuCommands{},performanceGpuRegions{},performanceGpuFailures{};
namespace argent {void log(const std::string& message){performanceGpuCommands+=message.find("PERF_GPU_COMMAND")!=std::string::npos;performanceGpuRegions+=message.find("PERF_GPU_PASS")!=std::string::npos;performanceGpuFailures+=message.find("PERF_GPU_UNAVAILABLE")!=std::string::npos;std::cout<<message<<std::endl;}}
#endif
int main(int argc,char** argv){try{
 const bool captureTest=argc==4&&std::string(argv[3])=="capture";
 if(captureTest){auto folder=std::filesystem::temp_directory_path()/("argent-sky-hook-test-"+std::to_string(GetCurrentProcessId()));SetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",folder.c_str());SetEnvironmentVariableA("ARGENT_WATER_GPU_CAPTURE","1");}
 const bool performanceTest=argc==4&&std::string(argv[3])=="perf";if(performanceTest)SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS","1");
#ifdef KHARVOX_SFS_RING_RUNTIME
 SetEnvironmentVariableA("ARGENT_SFS_NATIVE_PROBE","1");SetEnvironmentVariableA("ARGENT_SFS_NATIVE_VR","1");
#endif
 auto loader=LoadLibraryW(L"vulkan-1.dll");check(loader,"No Vulkan loader");
 auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
 auto createInstance=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
 VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};instanceInfo.pApplicationInfo=&app;
#ifdef ARGENT_TEST_OPENXR
 check(argent::initializeXR(),"OpenXR init failed");auto instanceExtensions=argent::xrExtensions(false);std::vector<const char*> instanceNames;for(auto& e:instanceExtensions)instanceNames.push_back(e.c_str());instanceInfo.enabledExtensionCount=uint32_t(instanceNames.size());instanceInfo.ppEnabledExtensionNames=instanceNames.data();
#endif
 VkInstance instance{};VkResult instanceResult{};
#ifdef ARGENT_TEST_OPENXR
 if(!argent::xrCreateGameInstance(gipa,&instanceInfo,nullptr,&instance,instanceResult))
#endif
 instanceResult=createInstance(&instanceInfo,nullptr,&instance);
 ok(instanceResult);
#define INSTANCE(name) auto name=reinterpret_cast<PFN_##name>(gipa(instance,#name));check(name,#name)
 INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkGetPhysicalDeviceProperties);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkDestroyInstance);
 uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));check(count,"No GPU");
 std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));
 const auto physical=physicals.front();VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);std::cout<<props.deviceName<<'\n';
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
 uint32_t family=UINT32_MAX;for(uint32_t n=0;n<count;++n)if(families[n].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=n;break;}check(family!=UINT32_MAX,"No graphics queue");
 float priority=1;VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
  VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
#ifdef KHARVOX_SFS_RING_RUNTIME
  INSTANCE(vkGetPhysicalDeviceFeatures2);
  VkPhysicalDeviceMultiviewFeatures multiview{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&multiview;
  vkGetPhysicalDeviceFeatures2(physical,&features);check(multiview.multiview,"GPU does not support multiview");
  multiview.multiviewGeometryShader=VK_FALSE;multiview.multiviewTessellationShader=VK_FALSE;
  VkPhysicalDeviceFeatures enabled{};check(features.features.occlusionQueryPrecise,"No precise occlusion queries");enabled.occlusionQueryPrecise=VK_TRUE;deviceInfo.pEnabledFeatures=&enabled;deviceInfo.pNext=&multiview;
#endif
#ifdef ARGENT_TEST_OPENXR
 auto deviceExtensions=argent::xrExtensions(true);std::vector<const char*> deviceNames;for(auto& e:deviceExtensions)deviceNames.push_back(e.c_str());deviceInfo.enabledExtensionCount=uint32_t(deviceNames.size());deviceInfo.ppEnabledExtensionNames=deviceNames.data();
#endif
 VkDevice device{};VkResult deviceResult{};
#ifdef ARGENT_TEST_OPENXR
 if(!argent::xrCreateGameDevice(gipa,vkGetDeviceProcAddr,instance,physical,&deviceInfo,&deviceInfo,nullptr,&device,deviceResult))
#endif
 deviceResult=vkCreateDevice(physical,&deviceInfo,nullptr,&device);
 ok(deviceResult);
#ifdef KHARVOX_SFS_RING_RUNTIME
 VkPhysicalDeviceMemoryProperties runtimeMemory{};vkGetPhysicalDeviceMemoryProperties(physical,&runtimeMemory);
 check(argc==3||performanceTest||captureTest,"Need vertex and fragment fixtures");
 auto readShader=[](const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);check(bool(f),"Missing shader fixture");std::vector<uint32_t> words(size_t(f.tellg())/4);f.seekg(0);f.read(reinterpret_cast<char*>(words.data()),words.size()*4);return words;};
 auto vertexWords=readShader(argv[1]),fragmentWords=readShader(argv[2]);
 argent::sfs::Configuration configuration{};
#ifdef ARGENT_TEST_SCREEN_UI
 configuration.screenUiShaders.insert(kharvox::sfs::profileHash(vertexWords.data(),uint32_t(vertexWords.size()*4)));
#else
 configuration.projectionShaders.insert(kharvox::sfs::profileHash(vertexWords.data(),uint32_t(vertexWords.size()*4)));
#endif
 driverResolver=vkGetDeviceProcAddr;
 testDevice=device;
 check(argent::sfs::initialize(device,physical,testResolver,runtimeMemory,configuration),"SFS init failed");
 if(performanceTest){VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);argent::sfs::configurePerformanceGpu(device,props.limits.timestampPeriod,uint32_t(families.size()),families.data());}
 auto resolve=[&](const char* name){return argent::sfs::wrapProc(device,name,vkGetDeviceProcAddr(device,name));};
 if(captureTest){
  for(const char* name:{"vkCmdDraw","vkCmdDrawIndexed","vkCmdDrawIndirect","vkCmdDrawIndexedIndirect"})check(resolve(name)!=vkGetDeviceProcAddr(device,name),"Capture draw hook bypassed by release resolver");
  for(const char* name:{"vkCmdDrawIndirectCount","vkCmdDrawIndexedIndirectCount","vkCmdDrawIndirectCountKHR","vkCmdDrawIndexedIndirectCountKHR","vkCmdDrawIndirectCountAMD","vkCmdDrawIndexedIndirectCountAMD"}){auto next=vkGetDeviceProcAddr(device,name);if(next)check(resolve(name)!=next,"Count-buffer draw hook bypassed");}
 }
#else
 auto resolve=[&](const char* name){return vkGetDeviceProcAddr(device,name);};
#endif
#define DEVICE(name) auto name=reinterpret_cast<PFN_##name>(resolve(#name));check(name,#name)
 DEVICE(vkGetDeviceQueue);DEVICE(vkCreateCommandPool);DEVICE(vkAllocateCommandBuffers);DEVICE(vkResetCommandBuffer);DEVICE(vkBeginCommandBuffer);DEVICE(vkEndCommandBuffer);DEVICE(vkCmdPipelineBarrier);DEVICE(vkCmdClearColorImage);DEVICE(vkCmdCopyImageToBuffer);
 DEVICE(vkCreateBuffer);DEVICE(vkGetBufferMemoryRequirements);DEVICE(vkAllocateMemory);DEVICE(vkBindBufferMemory);DEVICE(vkMapMemory);DEVICE(vkUnmapMemory);DEVICE(vkFreeMemory);DEVICE(vkDestroyBuffer);
 DEVICE(vkCreateSemaphore);DEVICE(vkDestroySemaphore);DEVICE(vkCreateFence);DEVICE(vkDestroyFence);DEVICE(vkResetFences);DEVICE(vkWaitForFences);DEVICE(vkQueueSubmit);DEVICE(vkDeviceWaitIdle);DEVICE(vkDestroyCommandPool);DEVICE(vkDestroyDevice);
 DEVICE(vkCreateQueryPool);DEVICE(vkDestroyQueryPool);DEVICE(vkCmdResetQueryPool);DEVICE(vkCmdWriteTimestamp);DEVICE(vkGetQueryPoolResults);DEVICE(vkCmdCopyQueryPoolResults);DEVICE(vkCmdBeginQuery);DEVICE(vkCmdEndQuery);
 VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qi.queryType=VK_QUERY_TYPE_TIMESTAMP;qi.queryCount=3;VkQueryPool timestamps{};ok(vkCreateQueryPool(device,&qi,nullptr,&timestamps));
 qi.queryType=VK_QUERY_TYPE_OCCLUSION;VkQueryPool occlusion{};ok(vkCreateQueryPool(device,&qi,nullptr,&occlusion));
 VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
#ifdef ARGENT_TEST_OPENXR
 argent::Device xrDevice;xrDevice.device=device;xrDevice.instance=instance;xrDevice.physical=physical;xrDevice.gipa=gipa;xrDevice.gdpa=vkGetDeviceProcAddr;xrDevice.graphicsQueue=queue;xrDevice.graphicsFamily=family;
 argent::Source xrSource;xrSource.extent={4,4};xrSource.format=VK_FORMAT_R8G8B8A8_UNORM;xrSource.displaySrgb=true;
 XrPosef calibrated{};bool calibratedValid=false;
#endif
 #ifdef KHARVOX_SFS_RING_RUNTIME
 check(argent::sfs::configureSourceRing(device,vkGetDeviceProcAddr,memory,queue,nullptr,nullptr),"Runtime ring init failed");
 struct RuntimeRing {
  VkDevice device;
  VkResult create(const VkSwapchainCreateInfoKHR& info,VkSwapchainKHR* out){return argent::sfs::createSourceSwapchain(device,info,out);}
  VkResult enumerate(VkSwapchainKHR c,uint32_t* n,VkImage* images){auto r=argent::sfs::sourceImages(device,c,n,images);if(r==VK_SUCCESS&&images)argent::sfs::swapchainImages(device,c,*n,images);return r;}
  VkResult acquire(VkSwapchainKHR c,uint64_t timeout,VkSemaphore sem,VkFence fence,uint32_t* index){return argent::sfs::acquireSource(device,c,timeout,sem,fence,index);}
  VkResult present(VkQueue q,const VkPresentInfoKHR& info,bool consumed){return argent::sfs::presentSource(device,q,info,consumed);}
  VkResult destroy(VkSwapchainKHR c){argent::sfs::swapchainDestroyed(device,c);argent::sfs::destroySourceSwapchain(device,c);return VK_SUCCESS;}
 } ring{device};
 DEVICE(vkCreateImageView);DEVICE(vkDestroyImageView);DEVICE(vkCreateRenderPass);DEVICE(vkDestroyRenderPass);DEVICE(vkCreateFramebuffer);DEVICE(vkDestroyFramebuffer);DEVICE(vkCmdBeginRenderPass);DEVICE(vkCmdEndRenderPass);
#else
 argent::sfs::SourceRing ring;check(ring.initialize(device,queue,vkGetDeviceProcAddr,memory,nullptr,nullptr),"Ring init failed");
#endif
 VkSwapchainCreateInfoKHR chainInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};chainInfo.minImageCount=2;chainInfo.imageFormat=VK_FORMAT_R8G8B8A8_UNORM;chainInfo.imageExtent={4,4};chainInfo.imageArrayLayers=2;chainInfo.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
#ifdef KHARVOX_SFS_TEST_INDIRECT
 chainInfo.imageUsage|=VK_IMAGE_USAGE_STORAGE_BIT;
#endif
 VkSwapchainKHR chain{};ok(ring.create(chainInfo,&chain));ok(ring.enumerate(chain,&count,nullptr));check(count==2,"Engine image count changed");
 std::array<VkImage,5> images{};uint32_t partial=1;check(ring.enumerate(chain,&partial,images.data())==VK_INCOMPLETE&&partial==1,"Enumeration truncation broken");count=2;ok(ring.enumerate(chain,&count,images.data()));
#ifdef KHARVOX_SFS_RING_RUNTIME
 // The game sees PRESENT layout; the owned driver images must see GENERAL.
 VkAttachmentDescription attachment{};attachment.format=chainInfo.imageFormat;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
 VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
 VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};passInfo.attachmentCount=1;passInfo.pAttachments=&attachment;passInfo.subpassCount=1;passInfo.pSubpasses=&subpass;VkRenderPass pass{};ok(vkCreateRenderPass(device,&passInfo,nullptr,&pass));
 std::array<VkImageView,2> views{};std::array<VkFramebuffer,2> framebuffers{};
 for(unsigned n=0;n<2;++n){
  check(argent::sfs::sourceLayout(device,images[n],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)==VK_IMAGE_LAYOUT_GENERAL,"Source layout not translated");
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=images[n];view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=chainInfo.imageFormat;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};ok(vkCreateImageView(device,&view,nullptr,&views[n]));
  VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=pass;framebuffer.attachmentCount=1;framebuffer.pAttachments=&views[n];framebuffer.width=framebuffer.height=4;framebuffer.layers=1;ok(vkCreateFramebuffer(device,&framebuffer,nullptr,&framebuffers[n]));
 }
#endif

 DEVICE(vkCreateShaderModule);DEVICE(vkDestroyShaderModule);DEVICE(vkCreateDescriptorSetLayout);DEVICE(vkDestroyDescriptorSetLayout);DEVICE(vkCreateDescriptorPool);DEVICE(vkDestroyDescriptorPool);DEVICE(vkAllocateDescriptorSets);DEVICE(vkCreatePipelineLayout);DEVICE(vkDestroyPipelineLayout);DEVICE(vkCreateGraphicsPipelines);DEVICE(vkDestroyPipeline);DEVICE(vkCmdBindPipeline);DEVICE(vkCmdBindDescriptorSets);DEVICE(vkCmdDraw);
 VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=vertexWords.size()*4;sm.pCode=vertexWords.data();VkShaderModule vs{},fs{};ok(vkCreateShaderModule(device,&sm,nullptr,&vs));sm.codeSize=fragmentWords.size()*4;sm.pCode=fragmentWords.data();ok(vkCreateShaderModule(device,&sm,nullptr,&fs));
 VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};VkDescriptorSetLayout descriptorLayout{};ok(vkCreateDescriptorSetLayout(device,&dl,nullptr,&descriptorLayout));
 VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;VkDescriptorPool descriptors{};ok(vkCreateDescriptorPool(device,&dp,nullptr,&descriptors));
 VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=descriptors;da.descriptorSetCount=1;da.pSetLayouts=&descriptorLayout;VkDescriptorSet set{};ok(vkAllocateDescriptorSets(device,&da,&set));
 VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&descriptorLayout;VkPipelineLayout layout{};ok(vkCreatePipelineLayout(device,&pl,nullptr,&layout));
 VkPipelineShaderStageCreateInfo stages[2]{};for(auto& stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fs;
 VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 VkViewport viewport{0,0,4,4,0,1};VkRect2D scissor{{0,0},{4,4}};VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
 VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&blend;
 VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gp.stageCount=2;gp.pStages=stages;gp.pVertexInputState=&vi;gp.pInputAssemblyState=&ia;gp.pViewportState=&vp;gp.pRasterizationState=&raster;gp.pMultisampleState=&ms;gp.pColorBlendState=&cb;gp.layout=layout;gp.renderPass=pass;VkPipeline pipeline{};ok(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,nullptr,&pipeline));
 VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family;poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
 VkCommandPool pool{};ok(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
 VkCommandBuffer command{};ok(vkAllocateCommandBuffers(device,&allocate,&command));
#ifdef KHARVOX_SFS_RING_RUNTIME
 // Real Vulkan eye views supplied to an external (unmodified) image consumer.
 // Repeated resolution must reuse handles, and never select the other layer.
 argent::dlss::Resource dlssInput{};dlssInput.view=views[0];dlssInput.image=images[0];
 dlssInput.range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};dlssInput.format=chainInfo.imageFormat;dlssInput.width=dlssInput.height=4;
 std::array<const argent::dlss::Resource*,30> dlssInputs{};for(int i=0;i<4;++i)dlssInputs[i]=&dlssInput;
 std::array<argent::dlss::EyeParameters,2> dlssEyes{};argent::sfs::FramePose dlssPose{};
 check(argent::sfs::dlssEyeResources(command,dlssInputs,dlssEyes,dlssPose),"External stereo view resolution failed");
 const auto leftDlss=dlssEyes[0].resources[0].view,rightDlss=dlssEyes[1].resources[0].view;
 check(leftDlss&&rightDlss&&leftDlss!=rightDlss,"DLSS eye views alias");
 check(dlssEyes[0].resources[0].range.baseArrayLayer==0&&dlssEyes[1].resources[0].range.baseArrayLayer==1&&dlssEyes[1].resources[0].range.layerCount==1,"DLSS subresource range is not per-eye");
 check(argent::sfs::dlssEyeResources(command,dlssInputs,dlssEyes,dlssPose)&&dlssEyes[0].resources[0].view==leftDlss&&dlssEyes[1].resources[0].view==rightDlss,"DLSS views allocated again on steady path");
 dlssInput.image=images[1];check(!argent::sfs::dlssEyeResources(command,dlssInputs,dlssEyes,dlssPose),"Mismatched NGX view/image pair accepted");
#endif
#ifdef KHARVOX_SFS_RING_RUNTIME
 // Distinct pools satisfy Vulkan's external synchronization rules. Record in
 // parallel while another thread creates/destroys resource metadata.
 std::array<VkCommandPool,4> parallelPools{};std::array<VkCommandBuffer,4> parallelCommands{};
 for(unsigned n=0;n<4;++n){ok(vkCreateCommandPool(device,&poolInfo,nullptr,&parallelPools[n]));auto info=allocate;info.commandPool=parallelPools[n];ok(vkAllocateCommandBuffers(device,&info,&parallelCommands[n]));}
 std::atomic<bool> start{false},failed{false};std::array<std::thread,4> workers;
 for(unsigned n=0;n<4;++n)workers[n]=std::thread([&,n]{while(!start.load())std::this_thread::yield();try{
   for(unsigned iteration=0;iteration<300;++iteration){auto cb=parallelCommands[n];ok(vkResetCommandBuffer(cb,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(cb,&begin));
    VkClearColorValue color{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};
    vkCmdClearColorImage(cb,images[0],VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);ok(vkEndCommandBuffer(cb));}
 }catch(...){failed=true;}});
 start=true;
 for(unsigned n=0;n<60;++n){VkRenderPass temporary{};ok(vkCreateRenderPass(device,&passInfo,nullptr,&temporary));vkDestroyRenderPass(device,temporary,nullptr);}
 for(auto& worker:workers)worker.join();check(!failed,"Parallel command recording failed");
 for(auto pool:parallelPools)vkDestroyCommandPool(device,pool,nullptr);
#endif
 VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bufferInfo.size=400;bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
 VkBuffer buffer{};ok(vkCreateBuffer(device,&bufferInfo,nullptr,&buffer));VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,buffer,&requirements);
 uint32_t memoryType=UINT32_MAX;for(uint32_t n=0;n<memory.memoryTypeCount;++n)if((requirements.memoryTypeBits&(1u<<n))&&(memory.memoryTypes[n].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){memoryType=n;break;}check(memoryType!=UINT32_MAX,"No readback memory");
 VkMemoryAllocateInfo memoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memoryInfo.allocationSize=requirements.size;memoryInfo.memoryTypeIndex=memoryType;
 VkDeviceMemory readback{};ok(vkAllocateMemory(device,&memoryInfo,nullptr,&readback));ok(vkBindBufferMemory(device,buffer,readback,0));
#ifdef KHARVOX_SFS_TEST_INDIRECT
 DEVICE(vkCreateShaderModule);DEVICE(vkDestroyShaderModule);DEVICE(vkCreateDescriptorSetLayout);DEVICE(vkDestroyDescriptorSetLayout);DEVICE(vkCreateDescriptorPool);DEVICE(vkDestroyDescriptorPool);DEVICE(vkAllocateDescriptorSets);DEVICE(vkUpdateDescriptorSets);DEVICE(vkCreatePipelineLayout);DEVICE(vkDestroyPipelineLayout);DEVICE(vkCreateComputePipelines);DEVICE(vkDestroyPipeline);DEVICE(vkCmdBindPipeline);DEVICE(vkCmdBindDescriptorSets);DEVICE(vkCmdDispatchIndirect);DEVICE(vkCmdDispatch);DEVICE(vkCmdUpdateBuffer);
 check(argc==2,"Missing compute fixture");std::ifstream shaderFile(argv[1],std::ios::binary|std::ios::ate);check(bool(shaderFile),"Cannot read shader");std::vector<uint32_t> words(size_t(shaderFile.tellg())/4);shaderFile.seekg(0);shaderFile.read(reinterpret_cast<char*>(words.data()),words.size()*4);
 VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shaderInfo.codeSize=words.size()*4;shaderInfo.pCode=words.data();VkShaderModule shader{};ok(vkCreateShaderModule(device,&shaderInfo,nullptr,&shader));
 VkDescriptorSetLayoutBinding imageBinding{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layoutInfo.bindingCount=1;layoutInfo.pBindings=&imageBinding;VkDescriptorSetLayout descriptorLayout{};ok(vkCreateDescriptorSetLayout(device,&layoutInfo,nullptr,&descriptorLayout));
 VkDescriptorPoolSize descriptorSize[2]{{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};descriptorInfo.maxSets=3;descriptorInfo.poolSizeCount=2;descriptorInfo.pPoolSizes=descriptorSize;VkDescriptorPool descriptors{};ok(vkCreateDescriptorPool(device,&descriptorInfo,nullptr,&descriptors));
 std::array<VkDescriptorSetLayout,2> layouts{descriptorLayout,descriptorLayout};std::array<VkDescriptorSet,2> sets{};VkDescriptorSetAllocateInfo setsInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};setsInfo.descriptorPool=descriptors;setsInfo.descriptorSetCount=2;setsInfo.pSetLayouts=layouts.data();ok(vkAllocateDescriptorSets(device,&setsInfo,sets.data()));
 for(unsigned n=0;n<2;++n){VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE,views[n],VK_IMAGE_LAYOUT_GENERAL};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=sets[n];write.dstBinding=0;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;write.pImageInfo=&imageInfo;vkUpdateDescriptorSets(device,1,&write,0,nullptr);}
 VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pipelineLayoutInfo.setLayoutCount=1;pipelineLayoutInfo.pSetLayouts=&descriptorLayout;VkPipelineLayout pipelineLayout{};ok(vkCreatePipelineLayout(device,&pipelineLayoutInfo,nullptr,&pipelineLayout));
 VkComputePipelineCreateInfo computeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};computeInfo.layout=pipelineLayout;computeInfo.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};computeInfo.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;computeInfo.stage.module=shader;computeInfo.stage.pName="main";VkPipeline pipeline{};ok(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&computeInfo,nullptr,&pipeline));
 VkBufferCreateInfo indirectInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};indirectInfo.size=16;indirectInfo.usage=VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;VkBuffer indirectBuffer{};ok(vkCreateBuffer(device,&indirectInfo,nullptr,&indirectBuffer));vkGetBufferMemoryRequirements(device,indirectBuffer,&requirements);
 for(uint32_t n=0;n<memory.memoryTypeCount;++n)if(requirements.memoryTypeBits&(1u<<n)){memoryInfo.memoryTypeIndex=n;break;}
 memoryInfo.allocationSize=requirements.size;VkDeviceMemory indirectMemory{};ok(vkAllocateMemory(device,&memoryInfo,nullptr,&indirectMemory));ok(vkBindBufferMemory(device,indirectBuffer,indirectMemory,0));
 DEVICE(vkCmdCopyBuffer);
 std::string sharedPath=argv[1];sharedPath.replace(sharedPath.find("indirect.comp.spv"),17,"indirect-shared.comp.spv");std::ifstream sharedFile(sharedPath,std::ios::binary|std::ios::ate);check(bool(sharedFile),"Cannot read shared shader");std::vector<uint32_t> sharedWords(size_t(sharedFile.tellg())/4);sharedFile.seekg(0);sharedFile.read(reinterpret_cast<char*>(sharedWords.data()),sharedWords.size()*4);shaderInfo.codeSize=sharedWords.size()*4;shaderInfo.pCode=sharedWords.data();VkShaderModule sharedShader{};ok(vkCreateShaderModule(device,&shaderInfo,nullptr,&sharedShader));
 imageBinding.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;VkDescriptorSetLayout sharedLayout{};ok(vkCreateDescriptorSetLayout(device,&layoutInfo,nullptr,&sharedLayout));setsInfo.descriptorSetCount=1;setsInfo.pSetLayouts=&sharedLayout;VkDescriptorSet sharedSet{};ok(vkAllocateDescriptorSets(device,&setsInfo,&sharedSet));
 VkDescriptorBufferInfo sharedBufferInfo{indirectBuffer,0,16};VkWriteDescriptorSet sharedWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};sharedWrite.dstSet=sharedSet;sharedWrite.dstBinding=0;sharedWrite.descriptorCount=1;sharedWrite.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;sharedWrite.pBufferInfo=&sharedBufferInfo;vkUpdateDescriptorSets(device,1,&sharedWrite,0,nullptr);
 pipelineLayoutInfo.pSetLayouts=&sharedLayout;VkPipelineLayout sharedPipelineLayout{};ok(vkCreatePipelineLayout(device,&pipelineLayoutInfo,nullptr,&sharedPipelineLayout));computeInfo.layout=sharedPipelineLayout;computeInfo.stage.module=sharedShader;VkPipeline sharedPipeline{};ok(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&computeInfo,nullptr,&sharedPipeline));
#endif
 // An application compute pipeline is bound before the query resolver. The
 // subsequent dispatch must preserve its descriptor, dynamic offset and push.
 DEVICE(vkCreateComputePipelines);DEVICE(vkCmdDispatch);DEVICE(vkCmdPushConstants);DEVICE(vkUpdateDescriptorSets);DEVICE(vkCmdCopyBuffer);
 auto restoreWords=argent::sfs::compileGlsl(R"(#version 450
 layout(local_size_x=1) in;
 layout(set=0,binding=0,std430) buffer Output {uint value;} outputData;
 layout(push_constant) uniform Push {uint value;} p;
 void main(){outputData.value=p.value;}
 )",spv::ExecutionModelGLCompute);
 VkShaderModuleCreateInfo restoreSi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};restoreSi.codeSize=restoreWords.size()*4;restoreSi.pCode=restoreWords.data();VkShaderModule restoreShader{};ok(vkCreateShaderModule(device,&restoreSi,nullptr,&restoreShader));
 const VkDeviceSize restoreOffset=std::max<VkDeviceSize>(16,props.limits.minStorageBufferOffsetAlignment);
 VkBufferCreateInfo restoreBi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};restoreBi.size=restoreOffset+16;restoreBi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
 VkBuffer restoreBuffer{};VkDeviceMemory restoreMemory{};ok(vkCreateBuffer(device,&restoreBi,nullptr,&restoreBuffer));vkGetBufferMemoryRequirements(device,restoreBuffer,&requirements);
 memoryInfo.allocationSize=requirements.size;memoryInfo.memoryTypeIndex=memoryType;ok(vkAllocateMemory(device,&memoryInfo,nullptr,&restoreMemory));ok(vkBindBufferMemory(device,restoreBuffer,restoreMemory,0));
 VkDescriptorSetLayoutBinding restoreBinding{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
 VkDescriptorSetLayoutCreateInfo restoreDl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};restoreDl.bindingCount=1;restoreDl.pBindings=&restoreBinding;VkDescriptorSetLayout restoreDescriptorLayout{};ok(vkCreateDescriptorSetLayout(device,&restoreDl,nullptr,&restoreDescriptorLayout));
 VkDescriptorPoolSize restoreSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1};VkDescriptorPoolCreateInfo restoreDp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};restoreDp.maxSets=1;restoreDp.poolSizeCount=1;restoreDp.pPoolSizes=&restoreSize;VkDescriptorPool restorePool{};ok(vkCreateDescriptorPool(device,&restoreDp,nullptr,&restorePool));
 VkDescriptorSetAllocateInfo restoreDa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};restoreDa.descriptorPool=restorePool;restoreDa.descriptorSetCount=1;restoreDa.pSetLayouts=&restoreDescriptorLayout;VkDescriptorSet restoreSet{};ok(vkAllocateDescriptorSets(device,&restoreDa,&restoreSet));
 VkDescriptorBufferInfo restoreBufferInfo{restoreBuffer,0,4};VkWriteDescriptorSet restoreWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};restoreWrite.dstSet=restoreSet;restoreWrite.dstBinding=0;restoreWrite.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;restoreWrite.descriptorCount=1;restoreWrite.pBufferInfo=&restoreBufferInfo;vkUpdateDescriptorSets(device,1,&restoreWrite,0,nullptr);
 VkPushConstantRange restoreRange{VK_SHADER_STAGE_COMPUTE_BIT,0,4};VkPipelineLayoutCreateInfo restorePl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};restorePl.setLayoutCount=1;restorePl.pSetLayouts=&restoreDescriptorLayout;restorePl.pushConstantRangeCount=1;restorePl.pPushConstantRanges=&restoreRange;VkPipelineLayout restoreLayout{};ok(vkCreatePipelineLayout(device,&restorePl,nullptr,&restoreLayout));
 VkComputePipelineCreateInfo restoreCi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};restoreCi.layout=restoreLayout;restoreCi.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};restoreCi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;restoreCi.stage.module=restoreShader;restoreCi.stage.pName="main";VkPipeline restorePipeline{};ok(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&restoreCi,nullptr,&restorePipeline));
#ifdef KHARVOX_SFS_RING_RUNTIME
 {
  DEVICE(vkCmdNextSubpass);DEVICE(vkCmdDispatchIndirect);
  std::array<VkSubpassDescription,2> emptySubpasses{};for(auto& sub:emptySubpasses)sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
  VkRenderPassCreateInfo emptyInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};emptyInfo.subpassCount=2;emptyInfo.pSubpasses=emptySubpasses.data();
  VkRenderPass emptyPass{};ok(vkCreateRenderPass(device,&emptyInfo,nullptr,&emptyPass));
  VkFramebufferCreateInfo emptyFbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};emptyFbInfo.renderPass=emptyPass;emptyFbInfo.width=emptyFbInfo.height=4;emptyFbInfo.layers=1;
  VkFramebuffer emptyFb{};ok(vkCreateFramebuffer(device,&emptyFbInfo,nullptr,&emptyFb));
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(command,&begin));
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,restorePipeline);const uint32_t offset=uint32_t(restoreOffset),value=41;
  vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,restoreLayout,0,1,&restoreSet,1,&offset);vkCmdPushConstants(command,restoreLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&value);
  auto recordWarmPaths=[&]{
   VkClearValue clear{};VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};info.renderPass=pass;info.framebuffer=framebuffers[0];info.renderArea.extent={4,4};info.clearValueCount=1;info.pClearValues=&clear;
   vkCmdBeginRenderPass(command,&info,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(command);
   info.renderPass=emptyPass;info.framebuffer=emptyFb;info.clearValueCount=0;info.pClearValues=nullptr;
   vkCmdBeginRenderPass(command,&info,VK_SUBPASS_CONTENTS_INLINE);vkCmdNextSubpass(command,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(command);
   vkCmdDispatch(command,1,1,1);vkCmdDispatchIndirect(command,restoreBuffer,0);
  };
  recordWarmPaths();
  const auto savedGraphics=lastGraphicsPipeline;const auto savedSet=lastGraphicsSet;
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,reinterpret_cast<VkPipeline>(uintptr_t(0x1234)));
  const auto raySet=reinterpret_cast<VkDescriptorSet>(uintptr_t(0x5678));
  vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,restoreLayout,0,1,&raySet,0,nullptr);
  vkCmdPushConstants(command,restoreLayout,VK_SHADER_STAGE_RAYGEN_BIT_KHR,0,4,&value);
  recordWarmPaths();
  check(nativePipelineBinds==1&&nativeSetBinds==1&&nativePushes==1,"Native ray commands were rejected or replayed by SFS");
  check(lastGraphicsPipeline==savedGraphics&&lastGraphicsSet==savedSet,"Native ray state overwrote stereo graphics replay state");
  std::cout<<"Ray-tracing bind/descriptor/push isolation passed through real SFS hooks\n";
  std::promise<void> entered,release,recorded;auto enteredFuture=entered.get_future();auto recordedFuture=recorded.get_future();
  heldPassEntered=&entered;heldPassRelease=release.get_future().share();holdNextPass=true;
  VkRenderPass temporary{};
  auto writer=std::async(std::launch::async,[&]{return vkCreateRenderPass(device,&passInfo,nullptr,&temporary);});
  // Release even if recording regresses to waiting on the writer. This makes a
  // regression fail within five seconds instead of hanging the test suite.
  auto watchdog=std::async(std::launch::async,[&]{const bool completed=recordedFuture.wait_for(std::chrono::seconds(5))==std::future_status::ready;release.set_value();return completed;});
  const bool enteredWriter=enteredFuture.wait_for(std::chrono::seconds(5))==std::future_status::ready;
  if(enteredWriter)recordWarmPaths();recorded.set_value();const bool withoutWait=watchdog.get();ok(writer.get());
  ok(vkEndCommandBuffer(command));ok(vkResetCommandBuffer(command,0));
  vkDestroyRenderPass(device,temporary,nullptr);vkDestroyFramebuffer(device,emptyFb,nullptr);vkDestroyRenderPass(device,emptyPass,nullptr);
  heldPassEntered=nullptr;heldPassRelease={};
  check(enteredWriter,"Metadata writer did not enter the test gate");check(withoutWait,"Warm renderpass/compute recording waited for the unrelated metadata writer");
  std::cout<<"Warm stereo/mono renderpass, next-subpass, direct/indirect dispatch passed while metadata writer was blocked\n";
 }
#endif
 VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore acquired{},rendered{};ok(vkCreateSemaphore(device,&semaphoreInfo,nullptr,&acquired));ok(vkCreateSemaphore(device,&semaphoreInfo,nullptr,&rendered));
 VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence completed{},acquireFence{};ok(vkCreateFence(device,&fenceInfo,nullptr,&completed));ok(vkCreateFence(device,&fenceInfo,nullptr,&acquireFence));
 auto present=[&](uint32_t index,bool consumed,uint32_t waits){VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};info.swapchainCount=1;info.pSwapchains=&chain;info.pImageIndices=&index;info.waitSemaphoreCount=waits;info.pWaitSemaphores=waits?&rendered:nullptr;VkResult perImage=VK_NOT_READY;info.pResults=&perImage;ok(ring.present(queue,info,consumed));ok(perImage);};
 VkPhysicalDeviceProperties timingProperties{};vkGetPhysicalDeviceProperties(physical,&timingProperties);
 kharvox::CopyGpuTiming timing;timing.initialize(device,vkGetDeviceProcAddr,timingProperties.limits.timestampPeriod,families[family].timestampValidBits);
 check(timing.pool!=VK_NULL_HANDLE,"GPU timestamp initialization failed");
 for(uint32_t frame=0;frame<20;++frame){
#ifdef KHARVOX_SFS_RING_RUNTIME
  if(frame==10){
   // The preceding fence retired GPU work. Replace warmed resources, including
   // driver-recycled handles, then verify eye pixels and compute output again.
   ok(vkResetCommandBuffer(command,0));
   for(auto fb:framebuffers)vkDestroyFramebuffer(device,fb,nullptr);
   vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipeline(device,restorePipeline,nullptr);vkDestroyRenderPass(device,pass,nullptr);
   ok(vkCreateRenderPass(device,&passInfo,nullptr,&pass));
   for(unsigned n=0;n<2;++n){VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};info.renderPass=pass;info.attachmentCount=1;info.pAttachments=&views[n];info.width=info.height=4;info.layers=1;ok(vkCreateFramebuffer(device,&info,nullptr,&framebuffers[n]));}
   gp.renderPass=pass;ok(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,nullptr,&pipeline));ok(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&restoreCi,nullptr,&restorePipeline));
  }
#endif
  uint32_t index=UINT32_MAX;ok(ring.acquire(chain,UINT64_MAX,acquired,acquireFence,&index));check(index==frame%2,"Ring rotation broken");ok(vkWaitForFences(device,1,&acquireFence,VK_TRUE,10000000000ull));ok(vkResetFences(device,1,&acquireFence));
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
#ifndef KHARVOX_SFS_RING_RUNTIME
  ok(vkResetCommandBuffer(command,0));ok(vkBeginCommandBuffer(command,&begin));timing.begin(command);
#endif
  auto barrier=[&](VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags src,VkAccessFlags dst){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=images[index];b.oldLayout=oldLayout;b.newLayout=newLayout;b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
#ifdef KHARVOX_SFS_RING_RUNTIME
  VkClearValue initialClear{};VkRenderPassBeginInfo passBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};passBegin.renderPass=pass;passBegin.framebuffer=framebuffers[index];passBegin.renderArea.extent={4,4};passBegin.clearValueCount=1;passBegin.pClearValues=&initialClear;
  argent::sfs::FramePose pose;pose.serial=frame+1;argent::sfs::EyeUniforms uniforms{{argent::sfs::identity(),argent::sfs::identity()},{}};
#ifdef ARGENT_TEST_OPENXR
  XrPosef head{};auto deadline=GetTickCount64()+15000;while(!argent::beginStereoFrame(xrDevice,xrSource,pose,head,GetEnvironmentVariableW(L"ARGENT_TEST_MENU_QUAD",nullptr,0)!=0)){check(GetTickCount64()<deadline,"OpenXR did not become renderable");Sleep(10);}
  if(!calibratedValid){calibrated=head;calibratedValid=true;}
  argent::sfs::Matrix projection{};projection[0]=1;projection[5]=-1;projection[10]=-1.001f;projection[11]=-1;projection[14]=-.06006f;
  check(argent::sfs::eyeProjection(projection,calibrated,pose.views,1,uniforms),"Tracked projection failed");
#else
 #ifdef ARGENT_TEST_SCREEN_UI
  uniforms.screenClip[0][12]=-.5f;uniforms.screenClip[1][12]=.5f;
 #else
  uniforms.clipFromCenter[0][12]=-.5f;uniforms.clipFromCenter[1][12]=.5f;
  // Exercise cinematic forward/backward eye offsets without changing expected
  // pixels: normalize projected XY while preserving the native clip depth.
  if(frame>=10)for(int e=0;e<2;++e){const float w=frame%2?1.25f:.75f;
    uniforms.clipFromCenter[e][0]=uniforms.clipFromCenter[e][5]=w;
    uniforms.clipFromCenter[e][12]*=w;uniforms.eyeTranslation[e][3]=w-1;
  }
 #endif
#endif
  argent::sfs::prepare(device,pose,uniforms);
  if(frame==0){
   const auto mapsBefore=parameterMaps;
   retirementFailure=VK_ERROR_DEVICE_LOST;
   check(argent::sfs::beginFrame(device,chain,index)==VK_ERROR_DEVICE_LOST,"Retirement error was swallowed or aborted");
   check(parameterMaps==mapsBefore,"Failed retirement must not write frame parameters");
   argent::sfs::StereoFrame rejected;
   check(!argent::sfs::pair(device,images[index],{4,4},chainInfo.imageFormat,rejected),"Failed retirement published a frame");
   retirementFailure=VK_SUCCESS;
  }
  ok(argent::sfs::beginFrame(device,chain,index));
  if(performanceTest)argent::perf::mode.store(2);
  ok(vkResetCommandBuffer(command,0));ok(vkBeginCommandBuffer(command,&begin));timing.begin(command);
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
  const uint32_t dynamicRestoreOffset=uint32_t(restoreOffset),restoreValue=77+frame;
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,restorePipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,restoreLayout,0,1,&restoreSet,1,&dynamicRestoreOffset);vkCmdPushConstants(command,restoreLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&restoreValue);
  vkCmdResetQueryPool(command,timestamps,0,3);vkCmdResetQueryPool(command,occlusion,0,3);
  vkCmdBeginRenderPass(command,&passBegin,VK_SUBPASS_CONTENTS_INLINE);
  vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timestamps,0);
  vkCmdBeginQuery(command,occlusion,0,VK_QUERY_CONTROL_PRECISE_BIT);
  vkCmdDraw(command,6,1,0,0);
  vkCmdEndQuery(command,occlusion,0);
  vkCmdBeginQuery(command,occlusion,1,VK_QUERY_CONTROL_PRECISE_BIT);vkCmdEndQuery(command,occlusion,1);
  vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamps,1);
  vkCmdEndRenderPass(command);
  // Last logical query exercises the mono path and the final physical slot.
  vkCmdBeginQuery(command,occlusion,2,VK_QUERY_CONTROL_PRECISE_BIT);vkCmdEndQuery(command,occlusion,2);
  vkCmdCopyQueryPoolResults(command,occlusion,0,3,buffer,208,24,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT);
  vkCmdCopyQueryPoolResults(command,occlusion,0,3,buffer,288,8,VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT);
  vkCmdCopyQueryPoolResults(command,occlusion,1,2,buffer,328,4,VK_QUERY_RESULT_WAIT_BIT);
  vkCmdDispatch(command,1,1,1);
  VkMemoryBarrier restoredReady{VK_STRUCTURE_TYPE_MEMORY_BARRIER};restoredReady.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;restoredReady.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&restoredReady,0,nullptr,0,nullptr);
  VkBufferCopy restoreCopy{restoreOffset,352,4};vkCmdCopyBuffer(command,restoreBuffer,buffer,1,&restoreCopy);
  vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamps,2);
  vkCmdCopyQueryPoolResults(command,timestamps,0,3,buffer,144,16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT);
  barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
#else
  barrier(frame<2?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
#endif
  const float red=float(frame+1)/32.0f;

#ifdef KHARVOX_SFS_TEST_INDIRECT
  barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  // GPU writes the indirect grid. Never map/read the counts back on the CPU.
  const uint32_t counts[4]{frame%4?4u:0u,4,3,0};vkCmdUpdateBuffer(command,indirectBuffer,0,sizeof(counts),counts);
  VkMemoryBarrier countsReady{VK_STRUCTURE_TYPE_MEMORY_BARRIER};countsReady.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;countsReady.dstAccessMask=VK_ACCESS_INDIRECT_COMMAND_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,0,1,&countsReady,0,nullptr,0,nullptr);
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&sets[index],0,nullptr);
  vkCmdDispatchIndirect(command,indirectBuffer,0);
  barrier(VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  // Direct dispatch must still use the original pipeline after the wrapper.
  vkCmdDispatch(command,4,4,1);
  countsReady.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&countsReady,0,nullptr,0,nullptr);
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,sharedPipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,sharedPipelineLayout,0,1,&sharedSet,0,nullptr);vkCmdDispatchIndirect(command,indirectBuffer,0);
  VkMemoryBarrier counterReady{VK_STRUCTURE_TYPE_MEMORY_BARRIER};counterReady.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;counterReady.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&counterReady,0,nullptr,0,nullptr);VkBufferCopy counterCopy{12,128,4};vkCmdCopyBuffer(command,indirectBuffer,buffer,1,&counterCopy);
  barrier(VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
#else
  barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
#endif
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};copy.imageExtent={4,4,1};vkCmdCopyImageToBuffer(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
  barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT);
  VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);timing.end(command);ok(vkEndCommandBuffer(command));
  VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;VkSubmitInfo draw{VK_STRUCTURE_TYPE_SUBMIT_INFO};draw.waitSemaphoreCount=1;draw.pWaitSemaphores=&acquired;draw.pWaitDstStageMask=&stage;draw.commandBufferCount=1;draw.pCommandBuffers=&command;draw.signalSemaphoreCount=1;draw.pSignalSemaphores=&rendered;ok(vkQueueSubmit(queue,1,&draw,VK_NULL_HANDLE));
#ifdef KHARVOX_SFS_RING_RUNTIME
  if(performanceTest)argent::sfs::performanceSubmitted(device,queue,1,&command);
#endif
#ifdef ARGENT_TEST_OPENXR
  argent::sfs::StereoFrame submittedPair;check(argent::sfs::pair(device,images[index],{4,4},chainInfo.imageFormat,submittedPair),"No stereo frame for XR");
  check(argent::presentStereoFrame(xrDevice,submittedPair,1,&rendered),"XR did not consume completed stereo pair");present(index,true,1);
#else
  if(frame%2){VkSubmitInfo consume{VK_STRUCTURE_TYPE_SUBMIT_INFO};consume.waitSemaphoreCount=1;consume.pWaitSemaphores=&rendered;consume.pWaitDstStageMask=&stage;ok(vkQueueSubmit(queue,1,&consume,VK_NULL_HANDLE));}
  present(index,frame%2,1);
#endif
  VkSubmitInfo finish{VK_STRUCTURE_TYPE_SUBMIT_INFO};ok(vkQueueSubmit(queue,1,&finish,completed));ok(vkWaitForFences(device,1,&completed,VK_TRUE,10000000000ull));ok(vkResetFences(device,1,&completed));
  double gpuMs{};check(timing.completed(gpuMs)&&std::isfinite(gpuMs)&&gpuMs>=0,"GPU timestamps unavailable after completion");check(!timing.completed(gpuMs),"GPU timestamp sample reused");
  void* mapped{};ok(vkMapMemory(device,readback,0,400,0,&mapped));auto bytes=static_cast<unsigned char*>(mapped);
  std::array<uint64_t,6> queryCpu{},queryGpu{};ok(vkGetQueryPoolResults(device,timestamps,0,3,sizeof(queryCpu),queryCpu.data(),16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT));
  std::memcpy(queryGpu.data(),bytes+144,sizeof(queryGpu));check(queryCpu==queryGpu,"Query copy changed logical stride/index/availability");
  uint32_t restored{};std::memcpy(&restored,bytes+352,4);check(restored==77+frame,"Query resolver disturbed application compute pipeline/descriptors/dynamic offsets/push constants");
  std::array<uint64_t,6> occlusionCpu{};
  ok(vkGetQueryPoolResults(device,occlusion,0,3,sizeof(occlusionCpu),occlusionCpu.data(),16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT));
  auto rawGet=reinterpret_cast<PFN_vkGetQueryPoolResults>(vkGetDeviceProcAddr(device,"vkGetQueryPoolResults"));
  std::array<uint64_t,12> physicalQueries{};ok(rawGet(device,occlusion,0,6,sizeof(physicalQueries),physicalQueries.data(),16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT));
  for(unsigned q=0;q<3;++q){uint64_t value{},available{};std::memcpy(&value,bytes+208+q*24,8);std::memcpy(&available,bytes+216+q*24,8);
   check(value==occlusionCpu[q*2]&&available&&occlusionCpu[q*2+1],"64-bit GPU/CPU occlusion aggregation or padded stride incorrect");
   check(value==physicalQueries[q*4]+physicalQueries[q*4+2],"Occlusion must sum both physical view results");
   uint32_t narrow[2]{};std::memcpy(narrow,bytes+288+q*8,8);check(narrow[0]==value&&narrow[1],"32-bit GPU occlusion aggregation incorrect");
  }
#ifndef ARGENT_TEST_OPENXR
  check(occlusionCpu[0]==16,"Precise occlusion must count samples from both eyes");
#endif
  check(occlusionCpu[2]==0&&occlusionCpu[4]==0,"Empty mono or stereo occlusion query is not zero");
  uint32_t narrowEmpty[2]{};std::memcpy(narrowEmpty,bytes+328,8);check(narrowEmpty[0]==0&&narrowEmpty[1]==0,"Query range without availability incorrect");
  if(frame==0){
   argent::Device captureDevice;captureDevice.device=device;captureDevice.instance=instance;captureDevice.physical=physical;captureDevice.graphicsQueue=queue;captureDevice.graphicsFamily=family;captureDevice.gipa=gipa;captureDevice.gdpa=vkGetDeviceProcAddr;
   const auto path=std::filesystem::temp_directory_path()/("argent-eye-readback-"+std::to_string(GetCurrentProcessId())+".ppm");
   argent::readbackStereo(captureDevice,images[index],{4,4},chainInfo.imageFormat,path);
   std::ifstream captured(path,std::ios::binary);std::string magic;unsigned width{},height{},maximum{};captured>>magic>>width>>height>>maximum;captured.get();check(magic=="P6"&&width==8&&height==4&&maximum==255,"Bad stereo export header");
   std::array<unsigned char,96> rgb{};captured.read(reinterpret_cast<char*>(rgb.data()),rgb.size());check(bool(captured),"Truncated stereo export");
   for(unsigned y=0;y<4;++y)for(unsigned eye=0;eye<2;++eye)for(unsigned x=0;x<4;++x)for(unsigned component=0;component<3;++component)check(rgb[(y*8+eye*4+x)*3+component]==bytes[eye*64+(y*4+x)*4+component],"Export changed eye, row or channel");
   captured.close();std::filesystem::remove(path);
  }
  check(queryCpu[1]&&queryCpu[3]&&queryCpu[5]&&queryCpu[0]&&queryCpu[2]>=queryCpu[0]&&queryCpu[4]>=queryCpu[2],"Multiview timestamps overlap or include unused query slots");
#ifndef ARGENT_TEST_OPENXR
  for(uint32_t eye=0;eye<2;++eye)for(uint32_t pixel=0;pixel<16;++pixel){const auto offset=eye*64+pixel*4;const bool lit=(pixel%4<2)==(eye==0);check(bytes[offset]==(lit?255:0),"Per-eye projection or multiview pipeline incorrect");}
#endif
  argent::sfs::StereoFrame pair;check(argent::sfs::pair(device,images[index],{4,4},chainInfo.imageFormat,pair),"No qualified stereo pair");check(pair.generation==pose.serial&&pair.eyes[0].serial==pair.eyes[1].serial&&pair.eyes[1].layer==1,"Frame provenance mismatch");argent::sfs::copyCompleted(device);

#ifdef KHARVOX_SFS_TEST_INDIRECT
  for(unsigned eye=0;eye<2;++eye)for(unsigned pixel=0;pixel<16;++pixel)check(std::abs(int(bytes[eye*64+pixel*4+2])-(frame%4?128:32))<=1,"Indirect grid/eye output or pipeline restoration incorrect");
  uint32_t sharedCount{};std::memcpy(&sharedCount,bytes+128,4);check(sharedCount==(frame%4?48u:0u),"Shared-buffer-only indirect work was duplicated");
#endif
  vkUnmapMemory(device,readback);
 }
 // A reset subrange must reset both physical slots. CPU NOT_READY leaves
 // result bytes untouched and still reports zero availability for each query.
 ok(vkResetCommandBuffer(command,0));VkCommandBufferBeginInfo resetBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(command,&resetBegin));vkCmdResetQueryPool(command,occlusion,1,2);ok(vkEndCommandBuffer(command));
 VkSubmitInfo resetSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO};resetSubmit.commandBufferCount=1;resetSubmit.pCommandBuffers=&command;ok(vkQueueSubmit(queue,1,&resetSubmit,completed));ok(vkWaitForFences(device,1,&completed,VK_TRUE,10000000000ull));ok(vkResetFences(device,1,&completed));
 std::array<uint64_t,4> pendingResults{111,999,222,999};check(vkGetQueryPoolResults(device,occlusion,1,2,sizeof(pendingResults),pendingResults.data(),16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)==VK_NOT_READY,"Reset occlusion range unexpectedly ready");check(pendingResults==std::array<uint64_t,4>{111,0,222,0},"Unavailable query overwrote result bytes or failed to reset availability");
 std::array<uint32_t,2> leased{};for(auto& index:leased){ok(ring.acquire(chain,UINT64_MAX,VK_NULL_HANDLE,acquireFence,&index));ok(vkWaitForFences(device,1,&acquireFence,VK_TRUE,10000000000ull));ok(vkResetFences(device,1,&acquireFence));}
 uint32_t unavailable=99;check(ring.acquire(chain,0,acquired,VK_NULL_HANDLE,&unavailable)==VK_NOT_READY&&unavailable==99,"Acquired a leased image");check(ring.acquire(chain,1000000,acquired,VK_NULL_HANDLE,&unavailable)==VK_TIMEOUT,"Exhaustion timeout broken");
 chainInfo.oldSwapchain=chain;VkSwapchainKHR replacement{};ok(ring.create(chainInfo,&replacement));check(ring.acquire(chain,0,acquired,VK_NULL_HANDLE,&unavailable)==VK_ERROR_OUT_OF_DATE_KHR,"Retired chain acquired");
 for(auto index:leased)present(index,false,0);
 ok(vkDeviceWaitIdle(device));
#ifdef KHARVOX_SFS_TEST_INDIRECT
 vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,shader,nullptr);vkDestroyPipelineLayout(device,pipelineLayout,nullptr);vkDestroyDescriptorPool(device,descriptors,nullptr);vkDestroyDescriptorSetLayout(device,descriptorLayout,nullptr);vkDestroyBuffer(device,indirectBuffer,nullptr);vkFreeMemory(device,indirectMemory,nullptr);
 vkDestroyPipeline(device,sharedPipeline,nullptr);vkDestroyShaderModule(device,sharedShader,nullptr);vkDestroyPipelineLayout(device,sharedPipelineLayout,nullptr);vkDestroyDescriptorSetLayout(device,sharedLayout,nullptr);
#endif
#ifdef KHARVOX_SFS_RING_RUNTIME
 for(unsigned n=0;n<2;++n){
  vkDestroyFramebuffer(device,framebuffers[n],nullptr);
  std::future<void> retirement;bool blocked=false;
  {
   kharvox::GameImageLifetime::Use borrowed(kharvox::gameImageLifetime());
   check(borrowed.select([]{return true;},[&]{return std::array{kharvox::GameImageLifetime::key(views[n])};}),"Could not borrow game view");
   std::promise<void> started;auto entered=started.get_future();
   retirement=std::async(std::launch::async,[&]{started.set_value();vkDestroyImageView(device,views[n],nullptr);});
   entered.get();blocked=retirement.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout;
  }
  retirement.get();check(blocked,"SFS destroyed a view still borrowed by XR");
 }
 vkDestroyRenderPass(device,pass,nullptr);
#endif
 vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyDescriptorPool(device,descriptors,nullptr);vkDestroyDescriptorSetLayout(device,descriptorLayout,nullptr);vkDestroyShaderModule(device,vs,nullptr);vkDestroyShaderModule(device,fs,nullptr);
 vkDestroyPipeline(device,restorePipeline,nullptr);vkDestroyPipelineLayout(device,restoreLayout,nullptr);vkDestroyDescriptorPool(device,restorePool,nullptr);vkDestroyDescriptorSetLayout(device,restoreDescriptorLayout,nullptr);vkDestroyShaderModule(device,restoreShader,nullptr);vkDestroyBuffer(device,restoreBuffer,nullptr);vkFreeMemory(device,restoreMemory,nullptr);
 vkDestroyQueryPool(device,occlusion,nullptr);vkDestroyQueryPool(device,timestamps,nullptr);timing.shutdownAfterCompletion();
 ok(ring.destroy(chain));ok(ring.destroy(replacement));
 vkDestroyFence(device,completed,nullptr);vkDestroyFence(device,acquireFence,nullptr);vkDestroySemaphore(device,acquired,nullptr);vkDestroySemaphore(device,rendered,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,readback,nullptr);
#ifdef KHARVOX_SFS_RING_RUNTIME
#ifdef ARGENT_TEST_OPENXR
 argent::shutdownXR(device);
#endif
 argent::sfs::shutdown(device);
#endif
#ifndef ARGENT_TEST_OPENXR
 if(performanceTest)check(performanceGpuCommands>=1&&performanceGpuRegions>=2&&performanceGpuFailures==0,"Real GPU diagnostic readback/regions failed");
#endif
 vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
 std::cout<<"Source ring: 20 frames, both eye readbacks, acquire signals, consumed/unconsumed waits, exhaustion and recreation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
