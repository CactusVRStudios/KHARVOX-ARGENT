#include <vulkan/vulkan.h>
#include "../src/sfs/StereoResources.h"
#include <cstring>
#include <windows.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <cmath>

static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static void ok(VkResult value){check(value==VK_SUCCESS,"Vulkan operation failed");}
int main(){try{
    auto loader=LoadLibraryW(L"vulkan-1.dll");check(loader,"Vulkan loader unavailable");
    auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
    auto createInstance=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;
    VkInstance instance{};ok(createInstance(&ici,nullptr,&instance));
#define INSTANCE(name) auto name=reinterpret_cast<PFN_##name>(gipa(instance,#name));check(name,#name)
    INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkGetPhysicalDeviceProperties);
    INSTANCE(vkGetPhysicalDeviceFeatures2);INSTANCE(vkCreateDevice);INSTANCE(vkDestroyInstance);INSTANCE(vkGetDeviceProcAddr);
    uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));check(count,"No Vulkan GPU");
    std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));
    VkPhysicalDevice physical{};uint32_t family=UINT32_MAX;
    for(auto candidate:physicals){
        uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
        std::vector<VkQueueFamilyProperties> props(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,props.data());
        for(uint32_t i=0;i<n;++i)if(props[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){physical=candidate;family=i;break;}
        if(physical)break;
    }
    check(physical,"No graphics queue");
    VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);std::cout<<props.deviceName<<'\n';
    float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;
    VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&mv;
    vkGetPhysicalDeviceFeatures2(physical,&features);check(mv.multiview,"GPU lacks multiview");
    mv.multiviewGeometryShader=mv.multiviewTessellationShader=VK_FALSE;dci.pNext=&mv;
    VkDevice device{};ok(vkCreateDevice(physical,&dci,nullptr,&device));
#define DEVICE(name) auto name=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name));check(name,#name)
    DEVICE(vkGetDeviceQueue);DEVICE(vkDestroyDevice);DEVICE(vkCreateCommandPool);DEVICE(vkDestroyCommandPool);
    DEVICE(vkAllocateCommandBuffers);DEVICE(vkResetCommandBuffer);DEVICE(vkBeginCommandBuffer);DEVICE(vkEndCommandBuffer);
    DEVICE(vkQueueSubmit);DEVICE(vkQueueWaitIdle);DEVICE(vkCreateBuffer);DEVICE(vkDestroyBuffer);
    DEVICE(vkCreateFence);DEVICE(vkDestroyFence);DEVICE(vkWaitForFences);
    DEVICE(vkGetBufferMemoryRequirements);DEVICE(vkAllocateMemory);DEVICE(vkFreeMemory);DEVICE(vkBindBufferMemory);
    DEVICE(vkMapMemory);DEVICE(vkUnmapMemory);DEVICE(vkCreateImage);DEVICE(vkDestroyImage);
    DEVICE(vkGetImageMemoryRequirements);DEVICE(vkBindImageMemory);DEVICE(vkCmdPipelineBarrier);
    DEVICE(vkCmdClearDepthStencilImage);DEVICE(vkCmdCopyImageToBuffer);DEVICE(vkCmdCopyImage);
    DEVICE(vkCreateRenderPass);DEVICE(vkDestroyRenderPass);DEVICE(vkCreateImageView);DEVICE(vkDestroyImageView);
    DEVICE(vkCreateFramebuffer);DEVICE(vkDestroyFramebuffer);DEVICE(vkCmdBeginRenderPass);DEVICE(vkCmdEndRenderPass);
    VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
    VkPhysicalDeviceMemoryProperties memoryProps{};vkGetPhysicalDeviceMemoryProperties(physical,&memoryProps);
    auto memoryType=[&](uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<memoryProps.memoryTypeCount;++i)if((bits&(1u<<i))&&(memoryProps.memoryTypes[i].propertyFlags&flags)==flags)return i;throw std::runtime_error("No memory type");};
    kharvox::sfs::Images registry;
    VkImageCreateInfo input{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    input.imageType=VK_IMAGE_TYPE_2D;input.format=VK_FORMAT_D32_SFLOAT;
    input.extent={8,8,1};input.mipLevels=1;input.arrayLayers=1;input.samples=VK_SAMPLE_COUNT_1_BIT;
    input.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image{};ok(registry.create(device,input,nullptr,&image,vkCreateImage));
    VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,image,&req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;
    alloc.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory imageMemory{};ok(vkAllocateMemory(device,&alloc,nullptr,&imageMemory));ok(vkBindImageMemory(device,image,imageMemory,0));
    VkImageViewCreateInfo original{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};original.image=image;
    original.viewType=VK_IMAGE_VIEW_TYPE_2D;original.format=input.format;
    original.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
    auto transformed=registry.viewInfo(original);
    VkImageView view{};ok(vkCreateImageView(device,&transformed,nullptr,&view));
    VkAttachmentDescription attachment{};attachment.format=input.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.pDepthStencilAttachment=&reference;
    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};passInfo.attachmentCount=1;passInfo.pAttachments=&attachment;passInfo.subpassCount=1;passInfo.pSubpasses=&subpass;
    kharvox::sfs::RenderPassPlan plan(passInfo,true);check(plan.valid(),"Invalid stereo plan");
    VkRenderPass pass{};ok(vkCreateRenderPass(device,&plan.info(),nullptr,&pass));
    VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fbInfo.renderPass=pass;fbInfo.attachmentCount=1;fbInfo.pAttachments=&view;fbInfo.width=fbInfo.height=8;fbInfo.layers=1;
    VkFramebuffer framebuffer{};ok(vkCreateFramebuffer(device,&fbInfo,nullptr,&framebuffer));
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=512;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer{};ok(vkCreateBuffer(device,&bi,nullptr,&buffer));vkGetBufferMemoryRequirements(device,buffer,&req);
    alloc.allocationSize=req.size;alloc.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory bufferMemory{};ok(vkAllocateMemory(device,&alloc,nullptr,&bufferMemory));ok(vkBindBufferMemory(device,buffer,bufferMemory,0));
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family;
    VkCommandPool pool{};ok(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));
    VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cbInfo.commandPool=pool;cbInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cbInfo.commandBufferCount=1;
    VkCommandBuffer command{};ok(vkAllocateCommandBuffers(device,&cbInfo,&command));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(command,&begin));
    VkClearValue clear{};clear.depthStencil.depth=.375f;
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rp.renderPass=pass;rp.framebuffer=framebuffer;rp.renderArea.extent={8,8};rp.clearValueCount=1;rp.pClearValues=&clear;
    vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(command);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    VkBufferImageCopy read{};read.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,2};read.imageExtent={8,8,1};
    vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&read);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    ok(vkEndCommandBuffer(command));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};ok(vkCreateFence(device,&fenceInfo,nullptr,&fence));
    ok(vkQueueSubmit(queue,1,&submit,fence));ok(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
    void* mapped{};ok(vkMapMemory(device,bufferMemory,0,512,0,&mapped));
    for(unsigned pixel=0;pixel<128;++pixel)check(std::abs(static_cast<float*>(mapped)[pixel]-.375f)<1e-6f,"Multiview failed to write both image layers");
    vkUnmapMemory(device,bufferMemory);vkDestroyFence(device,fence,nullptr);
    vkDestroyCommandPool(device,pool,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,bufferMemory,nullptr);
    vkDestroyFramebuffer(device,framebuffer,nullptr);vkDestroyRenderPass(device,pass,nullptr);vkDestroyImageView(device,view,nullptr);
    registry.destroy(device,image,nullptr,vkDestroyImage);vkFreeMemory(device,imageMemory,nullptr);
    vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
    std::cout<<"Own SFS resource core: one multiview pass wrote both GPU array layers; provider DLL not used\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
