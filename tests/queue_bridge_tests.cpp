#define XR_USE_GRAPHICS_API_VULKAN
#include "../src/QueueBridge.h"
#include <iostream>
#include <cstring>
static void ok(VkResult r){if(r)throw std::runtime_error("Vulkan result "+std::to_string(r));}
int main(){try{
    auto lib=LoadLibraryW(L"vulkan-1.dll");
    argent::Device d;d.gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(lib,"vkGetInstanceProcAddr"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ic.pApplicationInfo=&app;
    ok(reinterpret_cast<PFN_vkCreateInstance>(d.gipa(nullptr,"vkCreateInstance"))(&ic,nullptr,&d.instance));
#define I(name) reinterpret_cast<PFN_##name>(d.gipa(d.instance,#name))
#define V(name) d.proc<PFN_##name>(#name)
    uint32_t n=0;ok(I(vkEnumeratePhysicalDevices)(d.instance,&n,nullptr));std::vector<VkPhysicalDevice> devices(n);ok(I(vkEnumeratePhysicalDevices)(d.instance,&n,devices.data()));
    uint32_t graphics=UINT32_MAX,transfer=UINT32_MAX;
    for(auto p:devices){I(vkGetPhysicalDeviceQueueFamilyProperties)(p,&n,nullptr);std::vector<VkQueueFamilyProperties> fs(n);I(vkGetPhysicalDeviceQueueFamilyProperties)(p,&n,fs.data());
        for(uint32_t i=0;i<n;++i){if(fs[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)graphics=i;if((fs[i].queueFlags&VK_QUEUE_TRANSFER_BIT)&&!(fs[i].queueFlags&VK_QUEUE_GRAPHICS_BIT))transfer=i;}
        if(graphics!=UINT32_MAX&&transfer!=UINT32_MAX){d.physical=p;break;}graphics=transfer=UINT32_MAX;
    }
    if(!d.physical)throw std::runtime_error("Dedicated transfer queue required for regression test");
    float priority=1;VkDeviceQueueCreateInfo queues[2]{};
    for(auto& q:queues){q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;q.queueCount=1;q.pQueuePriorities=&priority;}queues[0].queueFamilyIndex=graphics;queues[1].queueFamilyIndex=transfer;
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=2;dc.pQueueCreateInfos=queues;ok(I(vkCreateDevice)(d.physical,&dc,nullptr,&d.device));
    d.gdpa=I(vkGetDeviceProcAddr);VkQueue gq{},tq{};V(vkGetDeviceQueue)(d.device,graphics,0,&gq);V(vkGetDeviceQueue)(d.device,transfer,0,&tq);
    VkPhysicalDeviceMemoryProperties props{};I(vkGetPhysicalDeviceMemoryProperties)(d.physical,&props);
    auto allocate=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags){VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=UINT32_MAX;for(uint32_t i=0;i<props.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&flags)==flags){ai.memoryTypeIndex=i;break;}VkDeviceMemory m{};ok(V(vkAllocateMemory)(d.device,&ai,nullptr,&m));return m;};
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=256;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;VkBuffer read{};ok(V(vkCreateBuffer)(d.device,&bi,nullptr,&read));VkMemoryRequirements req{};V(vkGetBufferMemoryRequirements)(d.device,read,&req);auto readMem=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);ok(V(vkBindBufferMemory)(d.device,read,readMem,0));
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=VK_IMAGE_TYPE_2D;ii.format=VK_FORMAT_R8G8B8A8_UNORM;ii.extent={8,8,1};ii.mipLevels=ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImage image{};ok(V(vkCreateImage)(d.device,&ii,nullptr,&image));V(vkGetImageMemoryRequirements)(d.device,image,&req);auto imageMem=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);ok(V(vkBindImageMemory)(d.device,image,imageMem,0));
    VkCommandPool pools[2]{};VkCommandBuffer cmds[2]{};
    for(int i=0;i<2;++i){VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=i?graphics:transfer;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;ok(V(vkCreateCommandPool)(d.device,&pi,nullptr,&pools[i]));VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pools[i];ai.commandBufferCount=1;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ok(V(vkAllocateCommandBuffers)(d.device,&ai,&cmds[i]));}
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore rendered{};ok(V(vkCreateSemaphore)(d.device,&sci,nullptr,&rendered));
    argent::QueueBridge bridge;bridge.prepare(d,transfer,graphics,{8,8},ii.format);
    for(int frame=0;frame<3;++frame){
        auto c=cmds[0];ok(V(vkResetCommandBuffer)(c,0));VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(V(vkBeginCommandBuffer)(c,&cb));
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=frame?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        V(vkCmdPipelineBarrier)(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkClearColorValue color{};color.float32[frame]=1;V(vkCmdClearColorImage)(c,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&b.subresourceRange);
        b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
        V(vkCmdPipelineBarrier)(c,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);ok(V(vkEndCommandBuffer)(c));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&c;si.signalSemaphoreCount=1;si.pSignalSemaphores=&rendered;ok(V(vkQueueSubmit)(tq,1,&si,VK_NULL_HANDLE));
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&rendered;bool consumed=false;bridge.stage(d,tq,image,{8,8},present,consumed,VK_IMAGE_LAYOUT_GENERAL);if(!consumed)throw std::runtime_error("Present wait not consumed");
        c=cmds[1];ok(V(vkResetCommandBuffer)(c,0));ok(V(vkBeginCommandBuffer)(c,&cb));VkBufferCopy copy{0,0,256};V(vkCmdCopyBuffer)(c,bridge.buffer,read,1,&copy);
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;V(vkCmdPipelineBarrier)(c,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,nullptr,0,nullptr);ok(V(vkEndCommandBuffer)(c));
        VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;si.signalSemaphoreCount=0;si.waitSemaphoreCount=1;si.pWaitSemaphores=&bridge.ready;si.pWaitDstStageMask=&stage;si.pCommandBuffers=&c;ok(V(vkQueueSubmit)(gq,1,&si,VK_NULL_HANDLE));ok(V(vkQueueWaitIdle)(gq));
        void* mapped{};ok(V(vkMapMemory)(d.device,readMem,0,256,0,&mapped));auto bytes=static_cast<unsigned char*>(mapped);for(int i=0;i<256;++i)if(bytes[i]!=((i%4==frame)?255:0))throw std::runtime_error("Cross-queue stale or corrupt pixels");V(vkUnmapMemory)(d.device,readMem);
    }
    ok(V(vkDeviceWaitIdle)(d.device));bridge.destroy(d);V(vkDestroySemaphore)(d.device,rendered,nullptr);for(auto p:pools)V(vkDestroyCommandPool)(d.device,p,nullptr);V(vkDestroyImage)(d.device,image,nullptr);V(vkFreeMemory)(d.device,imageMem,nullptr);V(vkDestroyBuffer)(d.device,read,nullptr);V(vkFreeMemory)(d.device,readMem,nullptr);V(vkDestroyDevice)(d.device,nullptr);I(vkDestroyInstance)(d.instance,nullptr);
    std::cout<<"Three changing GPU frames copied from family "<<transfer<<" to "<<graphics<<" with binary semaphore reuse and byte verification\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
