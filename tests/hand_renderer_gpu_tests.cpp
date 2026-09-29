#include "../src/hands/HandRenderer.h"
#include "../src/hands/HandDispatch.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
static void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
static void ok(VkResult r){check(r==VK_SUCCESS,"Vulkan operation failed");}
int main(int argc,char**argv){try{
check(argc==2||argc==3,"Missing runtime fixture");
const bool separateEyes=argc==3;
    auto loader=LoadLibraryW(L"vulkan-1.dll");check(loader,"Vulkan loader unavailable");
    auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
    auto createInstance=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;
    VkInstance instance{};ok(createInstance(&ici,nullptr,&instance));
#define INSTANCE(name) auto name=reinterpret_cast<PFN_##name>(gipa(instance,#name));check(name,#name)
    INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkGetPhysicalDeviceProperties);
    INSTANCE(vkCreateDevice);INSTANCE(vkDestroyInstance);INSTANCE(vkGetDeviceProcAddr);
    uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));check(count,"No Vulkan GPU");
    std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));
    VkPhysicalDevice physical{};uint32_t family=UINT32_MAX;
    for(auto candidate:physicals){
        uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
        std::vector<VkQueueFamilyProperties> props(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,props.data());
        for(uint32_t i=0;i<n;++i)if(props[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){physical=candidate;family=i;break;}
        if(physical)break;
    }
    check(physical,"No compute queue");
    VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);std::cout<<props.deviceName<<'\n';
    float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;
    VkDevice device{};ok(vkCreateDevice(physical,&dci,nullptr,&device));
#define DEVICE(name) auto name=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name));check(name,#name)
    DEVICE(vkGetDeviceQueue);DEVICE(vkDestroyDevice);DEVICE(vkCreateCommandPool);DEVICE(vkDestroyCommandPool);
    DEVICE(vkAllocateCommandBuffers);DEVICE(vkResetCommandBuffer);DEVICE(vkBeginCommandBuffer);DEVICE(vkEndCommandBuffer);
    DEVICE(vkQueueSubmit);DEVICE(vkQueueWaitIdle);DEVICE(vkCreateBuffer);DEVICE(vkDestroyBuffer);
    DEVICE(vkGetBufferMemoryRequirements);DEVICE(vkAllocateMemory);DEVICE(vkFreeMemory);DEVICE(vkBindBufferMemory);
    DEVICE(vkMapMemory);DEVICE(vkUnmapMemory);DEVICE(vkCreateImage);DEVICE(vkDestroyImage);
    DEVICE(vkGetImageMemoryRequirements);DEVICE(vkBindImageMemory);DEVICE(vkCmdPipelineBarrier);
    DEVICE(vkCmdClearColorImage);DEVICE(vkCmdCopyImageToBuffer);
    KharvoxVulkanDispatch dispatch{};
    dispatch.getDeviceProcAddr=reinterpret_cast<PFN_vkGetDeviceProcAddr>(vkGetDeviceProcAddr(device,"vkGetDeviceProcAddr"));
    dispatch.createCommandPool=reinterpret_cast<PFN_vkCreateCommandPool>(vkGetDeviceProcAddr(device,"vkCreateCommandPool"));
    dispatch.destroyCommandPool=reinterpret_cast<PFN_vkDestroyCommandPool>(vkGetDeviceProcAddr(device,"vkDestroyCommandPool"));
    dispatch.allocateCommandBuffers=reinterpret_cast<PFN_vkAllocateCommandBuffers>(vkGetDeviceProcAddr(device,"vkAllocateCommandBuffers"));
    dispatch.resetCommandBuffer=reinterpret_cast<PFN_vkResetCommandBuffer>(vkGetDeviceProcAddr(device,"vkResetCommandBuffer"));
    dispatch.beginCommandBuffer=reinterpret_cast<PFN_vkBeginCommandBuffer>(vkGetDeviceProcAddr(device,"vkBeginCommandBuffer"));
    dispatch.endCommandBuffer=reinterpret_cast<PFN_vkEndCommandBuffer>(vkGetDeviceProcAddr(device,"vkEndCommandBuffer"));
    dispatch.cmdPipelineBarrier=reinterpret_cast<PFN_vkCmdPipelineBarrier>(vkGetDeviceProcAddr(device,"vkCmdPipelineBarrier"));
    dispatch.cmdBlitImage=reinterpret_cast<PFN_vkCmdBlitImage>(vkGetDeviceProcAddr(device,"vkCmdBlitImage"));
    dispatch.cmdCopyImage=reinterpret_cast<PFN_vkCmdCopyImage>(vkGetDeviceProcAddr(device,"vkCmdCopyImage"));
    dispatch.cmdCopyBufferToImage=reinterpret_cast<PFN_vkCmdCopyBufferToImage>(vkGetDeviceProcAddr(device,"vkCmdCopyBufferToImage"));
    dispatch.cmdClearColorImage=reinterpret_cast<PFN_vkCmdClearColorImage>(vkGetDeviceProcAddr(device,"vkCmdClearColorImage"));
    dispatch.createImage=reinterpret_cast<PFN_vkCreateImage>(vkGetDeviceProcAddr(device,"vkCreateImage"));
    dispatch.destroyImage=reinterpret_cast<PFN_vkDestroyImage>(vkGetDeviceProcAddr(device,"vkDestroyImage"));
    dispatch.getImageMemoryRequirements=reinterpret_cast<PFN_vkGetImageMemoryRequirements>(vkGetDeviceProcAddr(device,"vkGetImageMemoryRequirements"));
    dispatch.allocateMemory=reinterpret_cast<PFN_vkAllocateMemory>(vkGetDeviceProcAddr(device,"vkAllocateMemory"));
    dispatch.freeMemory=reinterpret_cast<PFN_vkFreeMemory>(vkGetDeviceProcAddr(device,"vkFreeMemory"));
    dispatch.bindImageMemory=reinterpret_cast<PFN_vkBindImageMemory>(vkGetDeviceProcAddr(device,"vkBindImageMemory"));
    dispatch.createBuffer=reinterpret_cast<PFN_vkCreateBuffer>(vkGetDeviceProcAddr(device,"vkCreateBuffer"));
    dispatch.destroyBuffer=reinterpret_cast<PFN_vkDestroyBuffer>(vkGetDeviceProcAddr(device,"vkDestroyBuffer"));
    dispatch.getBufferMemoryRequirements=reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(vkGetDeviceProcAddr(device,"vkGetBufferMemoryRequirements"));
    dispatch.bindBufferMemory=reinterpret_cast<PFN_vkBindBufferMemory>(vkGetDeviceProcAddr(device,"vkBindBufferMemory"));
    dispatch.mapMemory=reinterpret_cast<PFN_vkMapMemory>(vkGetDeviceProcAddr(device,"vkMapMemory"));
    dispatch.unmapMemory=reinterpret_cast<PFN_vkUnmapMemory>(vkGetDeviceProcAddr(device,"vkUnmapMemory"));
    dispatch.getPhysicalDeviceMemoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));
    dispatch.getPhysicalDeviceProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance,"vkGetPhysicalDeviceProperties"));
    dispatch.getPhysicalDeviceQueueFamilyProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
    dispatch.createImageView=reinterpret_cast<PFN_vkCreateImageView>(vkGetDeviceProcAddr(device,"vkCreateImageView"));
    dispatch.destroyImageView=reinterpret_cast<PFN_vkDestroyImageView>(vkGetDeviceProcAddr(device,"vkDestroyImageView"));
    dispatch.createSampler=reinterpret_cast<PFN_vkCreateSampler>(vkGetDeviceProcAddr(device,"vkCreateSampler"));
    dispatch.destroySampler=reinterpret_cast<PFN_vkDestroySampler>(vkGetDeviceProcAddr(device,"vkDestroySampler"));
    dispatch.createShaderModule=reinterpret_cast<PFN_vkCreateShaderModule>(vkGetDeviceProcAddr(device,"vkCreateShaderModule"));
    dispatch.destroyShaderModule=reinterpret_cast<PFN_vkDestroyShaderModule>(vkGetDeviceProcAddr(device,"vkDestroyShaderModule"));
    dispatch.createDescriptorSetLayout=reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(vkGetDeviceProcAddr(device,"vkCreateDescriptorSetLayout"));
    dispatch.destroyDescriptorSetLayout=reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(vkGetDeviceProcAddr(device,"vkDestroyDescriptorSetLayout"));
    dispatch.createDescriptorPool=reinterpret_cast<PFN_vkCreateDescriptorPool>(vkGetDeviceProcAddr(device,"vkCreateDescriptorPool"));
    dispatch.destroyDescriptorPool=reinterpret_cast<PFN_vkDestroyDescriptorPool>(vkGetDeviceProcAddr(device,"vkDestroyDescriptorPool"));
    dispatch.allocateDescriptorSets=reinterpret_cast<PFN_vkAllocateDescriptorSets>(vkGetDeviceProcAddr(device,"vkAllocateDescriptorSets"));
    dispatch.updateDescriptorSets=reinterpret_cast<PFN_vkUpdateDescriptorSets>(vkGetDeviceProcAddr(device,"vkUpdateDescriptorSets"));
    dispatch.createPipelineLayout=reinterpret_cast<PFN_vkCreatePipelineLayout>(vkGetDeviceProcAddr(device,"vkCreatePipelineLayout"));
    dispatch.destroyPipelineLayout=reinterpret_cast<PFN_vkDestroyPipelineLayout>(vkGetDeviceProcAddr(device,"vkDestroyPipelineLayout"));
    dispatch.createComputePipelines=reinterpret_cast<PFN_vkCreateComputePipelines>(vkGetDeviceProcAddr(device,"vkCreateComputePipelines"));
    dispatch.createGraphicsPipelines=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(vkGetDeviceProcAddr(device,"vkCreateGraphicsPipelines"));
    dispatch.destroyPipeline=reinterpret_cast<PFN_vkDestroyPipeline>(vkGetDeviceProcAddr(device,"vkDestroyPipeline"));
    dispatch.cmdBindPipeline=reinterpret_cast<PFN_vkCmdBindPipeline>(vkGetDeviceProcAddr(device,"vkCmdBindPipeline"));
    dispatch.cmdBindDescriptorSets=reinterpret_cast<PFN_vkCmdBindDescriptorSets>(vkGetDeviceProcAddr(device,"vkCmdBindDescriptorSets"));
    dispatch.cmdPushConstants=reinterpret_cast<PFN_vkCmdPushConstants>(vkGetDeviceProcAddr(device,"vkCmdPushConstants"));
    dispatch.cmdDispatch=reinterpret_cast<PFN_vkCmdDispatch>(vkGetDeviceProcAddr(device,"vkCmdDispatch"));
    dispatch.createRenderPass=reinterpret_cast<PFN_vkCreateRenderPass>(vkGetDeviceProcAddr(device,"vkCreateRenderPass"));
    dispatch.destroyRenderPass=reinterpret_cast<PFN_vkDestroyRenderPass>(vkGetDeviceProcAddr(device,"vkDestroyRenderPass"));
    dispatch.createFramebuffer=reinterpret_cast<PFN_vkCreateFramebuffer>(vkGetDeviceProcAddr(device,"vkCreateFramebuffer"));
    dispatch.destroyFramebuffer=reinterpret_cast<PFN_vkDestroyFramebuffer>(vkGetDeviceProcAddr(device,"vkDestroyFramebuffer"));
    dispatch.cmdBeginRenderPass=reinterpret_cast<PFN_vkCmdBeginRenderPass>(vkGetDeviceProcAddr(device,"vkCmdBeginRenderPass"));
    dispatch.cmdEndRenderPass=reinterpret_cast<PFN_vkCmdEndRenderPass>(vkGetDeviceProcAddr(device,"vkCmdEndRenderPass"));
    dispatch.cmdBindVertexBuffers=reinterpret_cast<PFN_vkCmdBindVertexBuffers>(vkGetDeviceProcAddr(device,"vkCmdBindVertexBuffers"));
    dispatch.cmdBindIndexBuffer=reinterpret_cast<PFN_vkCmdBindIndexBuffer>(vkGetDeviceProcAddr(device,"vkCmdBindIndexBuffer"));
    dispatch.cmdDrawIndexed=reinterpret_cast<PFN_vkCmdDrawIndexed>(vkGetDeviceProcAddr(device,"vkCmdDrawIndexed"));
    dispatch.cmdSetViewport=reinterpret_cast<PFN_vkCmdSetViewport>(vkGetDeviceProcAddr(device,"vkCmdSetViewport"));
    dispatch.cmdSetScissor=reinterpret_cast<PFN_vkCmdSetScissor>(vkGetDeviceProcAddr(device,"vkCmdSetScissor"));
    dispatch.createSemaphore=reinterpret_cast<PFN_vkCreateSemaphore>(vkGetDeviceProcAddr(device,"vkCreateSemaphore"));
    dispatch.destroySemaphore=reinterpret_cast<PFN_vkDestroySemaphore>(vkGetDeviceProcAddr(device,"vkDestroySemaphore"));
    dispatch.createFence=reinterpret_cast<PFN_vkCreateFence>(vkGetDeviceProcAddr(device,"vkCreateFence"));
    dispatch.destroyFence=reinterpret_cast<PFN_vkDestroyFence>(vkGetDeviceProcAddr(device,"vkDestroyFence"));
    dispatch.resetFences=reinterpret_cast<PFN_vkResetFences>(vkGetDeviceProcAddr(device,"vkResetFences"));
    dispatch.waitForFences=reinterpret_cast<PFN_vkWaitForFences>(vkGetDeviceProcAddr(device,"vkWaitForFences"));
    dispatch.queueSubmit=reinterpret_cast<PFN_vkQueueSubmit>(vkGetDeviceProcAddr(device,"vkQueueSubmit"));
    dispatch.queueWaitIdle=reinterpret_cast<PFN_vkQueueWaitIdle>(vkGetDeviceProcAddr(device,"vkQueueWaitIdle"));

    auto clearDepth=reinterpret_cast<PFN_vkCmdClearDepthStencilImage>(vkGetDeviceProcAddr(device,"vkCmdClearDepthStencilImage"));
    dispatch.getPhysicalDeviceFormatProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(gipa(instance,"vkGetPhysicalDeviceFormatProperties"));
    VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
    VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    auto allocate=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags){
        uint32_t type=UINT32_MAX;for(uint32_t i=0;i<memory.memoryTypeCount;++i)
            if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
        check(type!=UINT32_MAX,"Memory type unavailable");VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;VkDeviceMemory out{};ok(vkAllocateMemory(device,&ai,nullptr,&out));return out;
    };
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pci.queueFamilyIndex=family;
    VkCommandPool pool{};ok(vkCreateCommandPool(device,&pci,nullptr,&pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;
    VkCommandBuffer cb{};ok(vkAllocateCommandBuffers(device,&cai,&cb));

    constexpr uint32_t width=256,height=256;constexpr VkDeviceSize bytes=width*height*4;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bci.size=bytes*3;bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer readback{};ok(vkCreateBuffer(device,&bci,nullptr,&readback));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,readback,&req);
    auto host=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);ok(vkBindBufferMemory(device,readback,host,0));
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;ci.extent={width,height,1};ci.mipLevels=1;ci.arrayLayers=2;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.arrayLayers=separateEyes?1:2;
    VkImage image{};ok(vkCreateImage(device,&ci,nullptr,&image));vkGetImageMemoryRequirements(device,image,&req);auto imageMemory=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);ok(vkBindImageMemory(device,image,imageMemory,0));
    VkImage rightImage=image;VkDeviceMemory rightMemory{};
    if(separateEyes){ok(vkCreateImage(device,&ci,nullptr,&rightImage));vkGetImageMemoryRequirements(device,rightImage,&req);rightMemory=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);ok(vkBindImageMemory(device,rightImage,rightMemory,0));}
    const std::array<VkImage,2> colors{image,rightImage};
    VkImage depthImage{};VkDeviceMemory depthMemory{};VkImageView depthView{};
    auto depthInfo=ci;depthInfo.arrayLayers=2;depthInfo.format=VK_FORMAT_D32_SFLOAT;depthInfo.extent={width/2,height/2,1};
    depthInfo.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ok(dispatch.createImage(device,&depthInfo,nullptr,&depthImage));
    VkMemoryRequirements depthRequirements{};dispatch.getImageMemoryRequirements(device,depthImage,&depthRequirements);
    depthMemory=allocate(depthRequirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);ok(dispatch.bindImageMemory(device,depthImage,depthMemory,0));
    VkImageViewCreateInfo depthViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};depthViewInfo.image=depthImage;
    depthViewInfo.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;depthViewInfo.format=VK_FORMAT_D32_SFLOAT;
    depthViewInfo.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,2};ok(dispatch.createImageView(device,&depthViewInfo,nullptr,&depthView));
    auto transition=[&](VkImageLayout old,VkImageLayout next){for(unsigned e=0;e<(separateEyes?2u:1u);++e){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=old;b.newLayout=next;b.srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=colors[e];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,ci.arrayLayers};vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);}};
    using namespace kharvox::hands;
    HandRenderer renderer;
    std::array<std::vector<VkImage>,2> images{{{image},{rightImage}}};
    check(renderer.initialize(physical,device,queue,family,dispatch,ci.format,{{{width,height},{width,height}}},images,std::filesystem::path(argv[1]).wstring(),[](const std::string&s){std::cout<<s<<'\n';},!separateEyes),"Hand initialization failed");
    auto available=renderer.availability();check(available.leftFist&&available.rightFist&&available.leftGun&&available.rightGun,"One or more models failed to load");
    HandEyeView view;view.pose.valid=true;view.angleLeft=view.angleDown=-.785398f;view.angleRight=view.angleUp=.785398f;view.imageRectWidth=width;view.imageRectHeight=height;
    HandPose grip;grip.valid=true;grip.position[2]=-.5f;
    std::array<std::vector<unsigned char>,8> baseline;
    for(unsigned frame=0;frame<120;++frame){
        if(frame==8){renderer.shutdown();check(renderer.initialize(physical,device,queue,family,dispatch,ci.format,{{{width,height},{width,height}}},images,std::filesystem::path(argv[1]).wstring(),{},!separateEyes,true),"Scene-only initialization failed");}
        const unsigned eye=frame%2;HandVisibilityOutput visible;
        switch((frame/2)%4){case 0:visible.left=HandModelKind::Fist;break;case 1:visible.right=HandModelKind::GunHolding;break;case 2:visible.right=HandModelKind::Fist;break;default:visible.left=HandModelKind::GunHolding;break;}
        ok(vkResetCommandBuffer(cb,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(cb,&begin));
        transition(frame?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue clear{{0,0,0,1}};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,ci.arrayLayers};
        for(unsigned e=0;e<(separateEyes?2u:1u);++e)vkCmdClearColorImage(cb,colors[e],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
        transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const bool laserOnly=frame>=112;
        const bool scene=frame>=8, reverse=laserOnly?frame>=116:(frame>=16&&frame<32)||(frame>=40&&frame<72)||(frame>=76&&frame<88)||(frame>=96&&frame<104)||frame>=108, occluded=laserOnly?((frame/2)%2==1):(scene&&frame<56&&((frame/2)%2==0))||frame>=104;
        const bool hudMask=frame>=32&&frame<48;
        const bool partialMask=frame>=64&&frame<72;
        const bool triangleMask=frame>=80&&frame<88;
        const bool behindMask=frame>=88&&frame<112;
        HandPose laser{};
        if(laserOnly){visible={};laser={{-.15f,0.f,-.3f},{0.f,-.38268343f,0.f,.92387953f},true};}
        view.hudPlaceholder.clear();
        if(frame>=72&&frame<80){
            visible={};
            view.hudPlaceholder.push_back({{{-.1f,-.1f,-.5f},{.1f,-.1f,-.5f},{-.1f,-.08f,-.5f},{.1f,-.08f,-.5f}}});
        }
        view.hudPanels.clear();
        if(hudMask)view.hudPanels.push_back({{{-1,-1,-.1f},{1,-1,-.1f},{-1,1,-.1f},{1,1,-.1f}}});
        if(partialMask)view.hudPanels.push_back({{{0,-1,-.1f},{1,-1,-.1f},{0,1,-.1f},{1,1,-.1f}}});
        if(triangleMask)view.hudPanels.push_back({{{-.1f,-.1f,-.1f},{.1f,-.1f,-.1f},{-.1f,.1f,-.1f},{-.1f,.1f,-.1f}}});
        if(behindMask)view.hudPanels.push_back({{{-2,-2,-2},{2,-2,-2},{-2,2,-2},{2,2,-2}}});
        if(scene){
            VkImageMemoryBarrier db{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};db.image=depthImage;
            db.oldLayout=frame==8?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            db.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;db.srcQueueFamilyIndex=db.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            db.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,2};db.srcAccessMask=frame==8?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;db.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            dispatch.cmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&db);
            // An intermediate occluder catches an inverted hand depth slope;
            // clearing to the absolute near endpoint cannot detect that bug.
            VkClearDepthStencilValue dc{occluded?.5f:(reverse?0.f:1.f),0};clearDepth(cb,depthImage,db.newLayout,&dc,1,&db.subresourceRange);
            db.oldLayout=db.newLayout;db.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;db.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;db.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
            dispatch.cmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&db);
            HandSceneTarget target{};target.depthImage=depthImage;target.depthView=depthView;target.depthFormat=VK_FORMAT_D32_SFLOAT;
            target.depthLayout=db.newLayout;target.extent={width/2,height/2};target.reverseDepth=reverse;
            // r023: Eternal supplies a forward projection to a reverse target.
            const bool projectionReverse=reverse&&frame<24;
            view.nativeDepth=true;view.depthA=projectionReverse?0.f:-1.f;view.depthB=projectionReverse?.02f:-.02f;
            check(renderer.recordSceneDepth(cb,eye,0,target,view,grip,grip,visible,{HandWeaponKind::CombatShotgun,false},laser),"Scene hand/laser recording failed");
            db.oldLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;db.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            db.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;db.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            dispatch.cmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&db);
            VkBufferImageCopy copyDepth{};copyDepth.bufferOffset=bytes*2;copyDepth.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,eye,1};copyDepth.imageExtent={width/2,height/2,1};
            vkCmdCopyImageToBuffer(cb,depthImage,db.newLayout,readback,1,&copyDepth);
            std::swap(db.oldLayout,db.newLayout);std::swap(db.srcAccessMask,db.dstAccessMask);
            dispatch.cmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&db);
        }else renderer.record(cb,eye,0,view,grip,grip,visible,{HandWeaponKind::CombatShotgun,false});
        transition(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,ci.arrayLayers};copy.imageExtent={width,height,1};
        for(unsigned e=0;e<(separateEyes?2u:1u);++e){copy.bufferOffset=bytes*e;vkCmdCopyImageToBuffer(cb,colors[e],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);}
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        ok(vkEndCommandBuffer(cb));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;ok(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));ok(vkQueueWaitIdle(queue));
        void* mapped{};ok(vkMapMemory(device,host,0,bytes*3,0,&mapped));auto data=static_cast<const unsigned char*>(mapped);unsigned changed[2]{};
        if(scene){const auto depths=reinterpret_cast<const float*>(data+bytes*2);for(unsigned i=0;i<width*height/4;++i)check(depths[i]==(occluded?.5f:(reverse?0.f:1.f)),"Hands/HUD modified native scene depth");}
        for(unsigned e=0;e<2;++e)for(unsigned i=0;i<width*height;++i)if(data[bytes*e+i*4]||data[bytes*e+i*4+1]||data[bytes*e+i*4+2])++changed[e];
        const auto pixels=data+bytes*eye;
        if(frame>=72&&frame<80){unsigned amber{};for(unsigned i=0;i<width*height;++i)if(pixels[i*4]>240&&pixels[i*4+1]>140&&pixels[i*4+2]<40&&pixels[i*4+1]<190)++amber;
            check(amber>20,"Placeholder missing with hidden hands");
            check(pixels[(height/2*width+width/2)*4]==0,"Placeholder filled transparent center");}
        if(frame>=56&&frame<64)baseline[frame-56].assign(pixels,pixels+bytes);
        if(behindMask){const auto& reference=baseline[(frame-88)%8];
            for(unsigned i=0;i<bytes;++i)check(pixels[i]==(occluded?(i%4==3?255:0):reference[i]),"HUD behind hand/world overrode nearer depth");
        }
        if(triangleMask){const auto& reference=baseline[frame-80];
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<3;++c){
                const auto i=(y*width+x)*4+c;
                // Projection flips Y for Vulkan's positive-height viewport.
                if(y>x+2)check(pixels[i]==0,"HUD triangle failed to protect artwork");
                if(x>y+2)check(pixels[i]==reference[i],"HUD triangle masked transparent bounding-box corner");
            }
        }
        if(partialMask){const auto& reference=baseline[frame-64];
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<3;++c){
                const auto i=(y*width+x)*4+c;
                check(pixels[i]==(x<width/2?reference[i]:0),"HUD mask changed pixels outside projected panel or failed inside it");
            }
        }
        vkUnmapMemory(device,host);std::cout<<"frame "<<frame<<" changed="<<changed[0]<<","<<changed[1]<<'\n';
        renderer.finishSceneIntegratedFrame();
        check(partialMask||(occluded||hudMask?changed[eye]==0:changed[eye]>(laserOnly?0:20)),"Hand/laser visibility/HUD protection failed");check(changed[1-eye]==0,"Hand/laser leaked into other array eye");check(changed[eye]<width*height/2,"Hand overwrote background");
    }
    renderer.shutdown();if(separateEyes){vkDestroyImage(device,rightImage,nullptr);vkFreeMemory(device,rightMemory,nullptr);}dispatch.destroyImageView(device,depthView,nullptr);dispatch.destroyImage(device,depthImage,nullptr);dispatch.freeMemory(device,depthMemory,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,imageMemory,nullptr);vkDestroyBuffer(device,readback,nullptr);vkFreeMemory(device,host,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
    std::cout<<"PASS: fist and gun hands, both array eyes, background preserved\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
