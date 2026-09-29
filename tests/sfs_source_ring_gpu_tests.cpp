#include <windows.h>
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
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void ok(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("Vulkan result "+std::to_string(result));}
int main(int argc,char** argv){try{
#ifdef KHARVOX_SFS_RING_RUNTIME
 SetEnvironmentVariableA("KHARVOX_SFS_NATIVE_PROBE","1");
#endif
 auto loader=LoadLibraryW(L"vulkan-1.dll");check(loader,"No Vulkan loader");
 auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
 auto createInstance=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
 VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};instanceInfo.pApplicationInfo=&app;
 VkInstance instance{};ok(createInstance(&instanceInfo,nullptr,&instance));
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
  deviceInfo.pNext=&multiview;
#endif
 VkDevice device{};ok(vkCreateDevice(physical,&deviceInfo,nullptr,&device));
#ifdef KHARVOX_SFS_RING_RUNTIME
 VkPhysicalDeviceMemoryProperties runtimeMemory{};vkGetPhysicalDeviceMemoryProperties(physical,&runtimeMemory);
 check(kharvox::sfs::initialize(device,physical,vkGetDeviceProcAddr,runtimeMemory),"SFS init failed");
 auto resolve=[&](const char* name){return kharvox::sfs::wrapProc(device,name,vkGetDeviceProcAddr(device,name));};
#else
 auto resolve=[&](const char* name){return vkGetDeviceProcAddr(device,name);};
#endif
#define DEVICE(name) auto name=reinterpret_cast<PFN_##name>(resolve(#name));check(name,#name)
 DEVICE(vkGetDeviceQueue);DEVICE(vkCreateCommandPool);DEVICE(vkAllocateCommandBuffers);DEVICE(vkResetCommandBuffer);DEVICE(vkBeginCommandBuffer);DEVICE(vkEndCommandBuffer);DEVICE(vkCmdPipelineBarrier);DEVICE(vkCmdClearColorImage);DEVICE(vkCmdCopyImageToBuffer);
 DEVICE(vkCreateBuffer);DEVICE(vkGetBufferMemoryRequirements);DEVICE(vkAllocateMemory);DEVICE(vkBindBufferMemory);DEVICE(vkMapMemory);DEVICE(vkUnmapMemory);DEVICE(vkFreeMemory);DEVICE(vkDestroyBuffer);
 DEVICE(vkCreateSemaphore);DEVICE(vkDestroySemaphore);DEVICE(vkCreateFence);DEVICE(vkDestroyFence);DEVICE(vkResetFences);DEVICE(vkWaitForFences);DEVICE(vkQueueSubmit);DEVICE(vkDeviceWaitIdle);DEVICE(vkDestroyCommandPool);DEVICE(vkDestroyDevice);
 VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
 #ifdef KHARVOX_SFS_RING_RUNTIME
 check(kharvox::sfs::configureSourceRing(device,vkGetDeviceProcAddr,memory,queue,nullptr,nullptr),"Runtime ring init failed");
 struct RuntimeRing {
  VkDevice device;
  VkResult create(const VkSwapchainCreateInfoKHR& info,VkSwapchainKHR* out){return kharvox::sfs::createSourceSwapchain(device,info,out);}
  VkResult enumerate(VkSwapchainKHR c,uint32_t* n,VkImage* images){auto r=kharvox::sfs::sourceImages(device,c,n,images);if(r==VK_SUCCESS&&images)kharvox::sfs::swapchainImages(device,c,*n,images);return r;}
  VkResult acquire(VkSwapchainKHR c,uint64_t timeout,VkSemaphore sem,VkFence fence,uint32_t* index){return kharvox::sfs::acquireSource(device,c,timeout,sem,fence,index);}
  VkResult present(VkQueue q,const VkPresentInfoKHR& info,bool consumed){return kharvox::sfs::presentSource(device,q,info,consumed);}
  VkResult destroy(VkSwapchainKHR c){kharvox::sfs::swapchainDestroyed(device,c);kharvox::sfs::destroySourceSwapchain(device,c);return VK_SUCCESS;}
 } ring{device};
 DEVICE(vkCreateImageView);DEVICE(vkDestroyImageView);DEVICE(vkCreateRenderPass);DEVICE(vkDestroyRenderPass);DEVICE(vkCreateFramebuffer);DEVICE(vkDestroyFramebuffer);DEVICE(vkCmdBeginRenderPass);DEVICE(vkCmdEndRenderPass);
#else
 kharvox::sfs::SourceRing ring;check(ring.initialize(device,queue,vkGetDeviceProcAddr,memory,nullptr,nullptr),"Ring init failed");
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
  check(kharvox::sfs::sourceLayout(device,images[n],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)==VK_IMAGE_LAYOUT_GENERAL,"Source layout not translated");
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=images[n];view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=chainInfo.imageFormat;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};ok(vkCreateImageView(device,&view,nullptr,&views[n]));
  VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=pass;framebuffer.attachmentCount=1;framebuffer.pAttachments=&views[n];framebuffer.width=framebuffer.height=4;framebuffer.layers=1;ok(vkCreateFramebuffer(device,&framebuffer,nullptr,&framebuffers[n]));
 }
#endif
 VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family;poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
 VkCommandPool pool{};ok(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
 VkCommandBuffer command{};ok(vkAllocateCommandBuffers(device,&allocate,&command));
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
 VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bufferInfo.size=144;bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
 VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore acquired{},rendered{};ok(vkCreateSemaphore(device,&semaphoreInfo,nullptr,&acquired));ok(vkCreateSemaphore(device,&semaphoreInfo,nullptr,&rendered));
 VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence completed{},acquireFence{};ok(vkCreateFence(device,&fenceInfo,nullptr,&completed));ok(vkCreateFence(device,&fenceInfo,nullptr,&acquireFence));
 auto present=[&](uint32_t index,bool consumed,uint32_t waits){VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};info.swapchainCount=1;info.pSwapchains=&chain;info.pImageIndices=&index;info.waitSemaphoreCount=waits;info.pWaitSemaphores=waits?&rendered:nullptr;VkResult perImage=VK_NOT_READY;info.pResults=&perImage;ok(ring.present(queue,info,consumed));ok(perImage);};
 VkPhysicalDeviceProperties timingProperties{};vkGetPhysicalDeviceProperties(physical,&timingProperties);
 kharvox::CopyGpuTiming timing;timing.initialize(device,vkGetDeviceProcAddr,timingProperties.limits.timestampPeriod,families[family].timestampValidBits);
 check(timing.pool!=VK_NULL_HANDLE,"GPU timestamp initialization failed");
 for(uint32_t frame=0;frame<20;++frame){
  uint32_t index=UINT32_MAX;ok(ring.acquire(chain,UINT64_MAX,acquired,acquireFence,&index));check(index==frame%2,"Ring rotation broken");ok(vkWaitForFences(device,1,&acquireFence,VK_TRUE,10000000000ull));ok(vkResetFences(device,1,&acquireFence));
  ok(vkResetCommandBuffer(command,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(command,&begin));timing.begin(command);
  auto barrier=[&](VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags src,VkAccessFlags dst){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=images[index];b.oldLayout=oldLayout;b.newLayout=newLayout;b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
#ifdef KHARVOX_SFS_RING_RUNTIME
  VkClearValue initialClear{};VkRenderPassBeginInfo passBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};passBegin.renderPass=pass;passBegin.framebuffer=framebuffers[index];passBegin.renderArea.extent={4,4};passBegin.clearValueCount=1;passBegin.pClearValues=&initialClear;
  vkCmdBeginRenderPass(command,&passBegin,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(command);
  barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
#else
  barrier(frame<2?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
#endif
  const float red=float(frame+1)/32.0f;
  for(uint32_t eye=0;eye<2;++eye){VkClearColorValue color{};color.float32[0]=red;color.float32[1]=float(eye);color.float32[3]=1;VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1};vkCmdClearColorImage(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);}
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
  if(frame%2){VkSubmitInfo consume{VK_STRUCTURE_TYPE_SUBMIT_INFO};consume.waitSemaphoreCount=1;consume.pWaitSemaphores=&rendered;consume.pWaitDstStageMask=&stage;ok(vkQueueSubmit(queue,1,&consume,VK_NULL_HANDLE));}
  present(index,frame%2,1);
  VkSubmitInfo finish{VK_STRUCTURE_TYPE_SUBMIT_INFO};ok(vkQueueSubmit(queue,1,&finish,completed));ok(vkWaitForFences(device,1,&completed,VK_TRUE,10000000000ull));ok(vkResetFences(device,1,&completed));
  double gpuMs{};check(timing.completed(gpuMs)&&std::isfinite(gpuMs)&&gpuMs>=0,"GPU timestamps unavailable after completion");check(!timing.completed(gpuMs),"GPU timestamp sample reused");
  void* mapped{};ok(vkMapMemory(device,readback,0,144,0,&mapped));auto bytes=static_cast<unsigned char*>(mapped);
  for(uint32_t eye=0;eye<2;++eye)for(uint32_t pixel=0;pixel<16;++pixel){const auto offset=eye*64+pixel*4;check(std::abs(int(bytes[offset])-int(std::lround(red*255)))<=1,"Stale frame pixels");check(bytes[offset+1]==eye*255,"Wrong eye layer");}
#ifdef KHARVOX_SFS_TEST_INDIRECT
  for(unsigned eye=0;eye<2;++eye)for(unsigned pixel=0;pixel<16;++pixel)check(std::abs(int(bytes[eye*64+pixel*4+2])-(frame%4?128:32))<=1,"Indirect grid/eye output or pipeline restoration incorrect");
  uint32_t sharedCount{};std::memcpy(&sharedCount,bytes+128,4);check(sharedCount==(frame%4?48u:0u),"Shared-buffer-only indirect work was duplicated");
#endif
  vkUnmapMemory(device,readback);
 }
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
 for(unsigned n=0;n<2;++n){vkDestroyFramebuffer(device,framebuffers[n],nullptr);vkDestroyImageView(device,views[n],nullptr);}vkDestroyRenderPass(device,pass,nullptr);
#endif
 timing.shutdownAfterCompletion();
 ok(ring.destroy(chain));ok(ring.destroy(replacement));
 vkDestroyFence(device,completed,nullptr);vkDestroyFence(device,acquireFence,nullptr);vkDestroySemaphore(device,acquired,nullptr);vkDestroySemaphore(device,rendered,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,readback,nullptr);
#ifdef KHARVOX_SFS_RING_RUNTIME
 kharvox::sfs::shutdown(device);
#endif
 vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
 std::cout<<"Source ring: 20 frames, both eye readbacks, acquire signals, consumed/unconsumed waits, exhaustion and recreation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
