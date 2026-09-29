#pragma once
#include "QuadRuntime.h"
#include <stdexcept>

namespace argent {
// A concurrent buffer crosses queue families; neither the game's image nor the
// OpenXR image leaves its owning family. The semaphore carries GPU visibility.
class QueueBridge {
    VkDeviceMemory memory{};
    VkCommandPool pool{};
    VkCommandBuffer command{};
    VkFence fence{};
    uint32_t sourceFamily=UINT32_MAX,targetFamily=UINT32_MAX;
    VkDeviceSize capacity{};
    static void check(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Queue bridge Vulkan result="+std::to_string(r));}
public:
    VkBuffer buffer{};
    VkSemaphore ready{};
    void destroy(Device& d){
        if(fence)d.proc<PFN_vkDestroyFence>("vkDestroyFence")(d.device,fence,nullptr);
        if(ready)d.proc<PFN_vkDestroySemaphore>("vkDestroySemaphore")(d.device,ready,nullptr);
        if(pool)d.proc<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(d.device,pool,nullptr);
        if(buffer)d.proc<PFN_vkDestroyBuffer>("vkDestroyBuffer")(d.device,buffer,nullptr);
        if(memory)d.proc<PFN_vkFreeMemory>("vkFreeMemory")(d.device,memory,nullptr);
        *this=QueueBridge{};
    }
    void prepare(Device& d,uint32_t from,uint32_t to,VkExtent2D extent,VkFormat format){
        if(format!=VK_FORMAT_B8G8R8A8_UNORM&&format!=VK_FORMAT_B8G8R8A8_SRGB&&format!=VK_FORMAT_R8G8B8A8_UNORM&&format!=VK_FORMAT_R8G8B8A8_SRGB)
            throw std::runtime_error("Queue bridge requires verified RGBA8/BGRA8 format");
        VkDeviceSize bytes=VkDeviceSize(extent.width)*extent.height*4;
        if(buffer&&sourceFamily==from&&targetFamily==to&&capacity>=bytes)return;
        destroy(d);sourceFamily=from;targetFamily=to;capacity=bytes;
        uint32_t families[]={from,to};
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode=from==to?VK_SHARING_MODE_EXCLUSIVE:VK_SHARING_MODE_CONCURRENT;
        bi.queueFamilyIndexCount=from==to?0:2;bi.pQueueFamilyIndices=families;
        check(d.proc<PFN_vkCreateBuffer>("vkCreateBuffer")(d.device,&bi,nullptr,&buffer));
        VkMemoryRequirements req{};d.proc<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(d.device,buffer,&req);
        VkPhysicalDeviceMemoryProperties props{};reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceMemoryProperties"))(d.physical,&props);
        uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<props.memoryTypeCount;++i)if(req.memoryTypeBits&(1u<<i)){if(type==UINT32_MAX)type=i;if(props.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT){type=i;break;}}
        if(type==UINT32_MAX)throw std::runtime_error("Queue bridge has no memory type");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
        check(d.proc<PFN_vkAllocateMemory>("vkAllocateMemory")(d.device,&ai,nullptr,&memory));
        check(d.proc<PFN_vkBindBufferMemory>("vkBindBufferMemory")(d.device,buffer,memory,0));
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pi.queueFamilyIndex=from;
        check(d.proc<PFN_vkCreateCommandPool>("vkCreateCommandPool")(d.device,&pi,nullptr,&pool));
        VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ci.commandPool=pool;ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ci.commandBufferCount=1;
        check(d.proc<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(d.device,&ci,&command));
        if(d.setLoaderData)check(d.setLoaderData(d.device,command));
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(d.proc<PFN_vkCreateFence>("vkCreateFence")(d.device,&fi,nullptr,&fence));
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};check(d.proc<PFN_vkCreateSemaphore>("vkCreateSemaphore")(d.device,&si,nullptr,&ready));
    }
    void stage(Device& d,VkQueue queue,VkImage image,VkExtent2D extent,const VkPresentInfoKHR& present,bool& consumed,VkImageLayout sourceLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR){
        check(d.proc<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(d.proc<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&begin));
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=sourceLayout;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        auto barrier=d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");barrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={extent.width,extent.height,1};
        d.proc<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=sourceLayout;b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=0;
        barrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
        check(d.proc<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command));
        check(d.proc<PFN_vkResetFences>("vkResetFences")(d.device,1,&fence));
        std::vector<VkPipelineStageFlags> stages(present.waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&command;si.waitSemaphoreCount=present.waitSemaphoreCount;si.pWaitSemaphores=present.pWaitSemaphores;si.pWaitDstStageMask=stages.data();si.signalSemaphoreCount=1;si.pSignalSemaphores=&ready;
        {std::lock_guard<std::recursive_mutex> lock(*d.queueMutex);check(d.proc<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&si,fence));}
        consumed=true;
        check(d.proc<PFN_vkWaitForFences>("vkWaitForFences")(d.device,1,&fence,VK_TRUE,UINT64_MAX));
    }
};
}
