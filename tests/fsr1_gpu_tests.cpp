// Executes the shipped FSR compute path on independent current-eye images.
// No game, headset, OpenXR session, or private shader inputs are required.
#include "../src/fsr/Fsr1Upscaler.h"
#include <windows.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <filesystem>
namespace argent {void log(const std::string& text){std::cout<<text<<"\n";} std::filesystem::path runtimePath(){wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);return std::filesystem::path(p).parent_path();}}

static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static void ok(VkResult value){check(value==VK_SUCCESS,"Vulkan operation failed");}
namespace kharvox::native {
[[noreturn]] void fail(const char* reason){throw std::runtime_error(reason);}
}
static void handleCopySubmitResult(Fsr1Upscaler& fsr,VkResult submitResult){
    if(submitResult!=VK_SUCCESS)fsr.discardRecordedFrame();
}
static VkResult injectedSubmitResult{VK_SUCCESS};
static unsigned rejectedSubmits{};
static VkResult VKAPI_CALL rejectCopySubmit(VkQueue queue,uint32_t count,const VkSubmitInfo* submits,VkFence){
    check(queue&&count==1&&submits&&submits[0].commandBufferCount==1,
        "Expected a recorded copy submission");
    ++rejectedSubmits;
    return injectedSubmitResult;
}
static PFN_vkCmdDispatch dispatchCompute{};
static unsigned dispatchCount{};
static void VKAPI_CALL countedDispatch(VkCommandBuffer cb,uint32_t x,uint32_t y,uint32_t z){
    ++dispatchCount;
    dispatchCompute(cb,x,y,z);
}
static PFN_vkCmdPipelineBarrier imageBarrier{};
static unsigned undefinedTransitions{};
static void VKAPI_CALL countedBarrier(VkCommandBuffer cb,VkPipelineStageFlags src,VkPipelineStageFlags dst,
    VkDependencyFlags flags,uint32_t memoryCount,const VkMemoryBarrier* memory,
    uint32_t bufferCount,const VkBufferMemoryBarrier* buffers,uint32_t imageCount,const VkImageMemoryBarrier* images){
    for(uint32_t n=0;n<imageCount;++n)if(images[n].oldLayout==VK_IMAGE_LAYOUT_UNDEFINED)++undefinedTransitions;
    imageBarrier(cb,src,dst,flags,memoryCount,memory,bufferCount,buffers,imageCount,images);
}
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
    INSTANCE(vkCreateDevice);INSTANCE(vkDestroyInstance);INSTANCE(vkGetDeviceProcAddr);
    uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));check(count,"No Vulkan GPU");
    std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));
    VkPhysicalDevice physical{};uint32_t family=UINT32_MAX;
    for(auto candidate:physicals){
        uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
        std::vector<VkQueueFamilyProperties> props(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,props.data());
        for(uint32_t i=0;i<n;++i)if(props[i].queueFlags&VK_QUEUE_COMPUTE_BIT){physical=candidate;family=i;break;}
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
    FsrDispatch dispatch{};
    dispatch.cmdPipelineBarrier=reinterpret_cast<PFN_vkCmdPipelineBarrier>(vkGetDeviceProcAddr(device,"vkCmdPipelineBarrier"));
    dispatch.cmdCopyImage=reinterpret_cast<PFN_vkCmdCopyImage>(vkGetDeviceProcAddr(device,"vkCmdCopyImage"));
    dispatch.createImage=reinterpret_cast<PFN_vkCreateImage>(vkGetDeviceProcAddr(device,"vkCreateImage"));
    dispatch.destroyImage=reinterpret_cast<PFN_vkDestroyImage>(vkGetDeviceProcAddr(device,"vkDestroyImage"));
    dispatch.getImageMemoryRequirements=reinterpret_cast<PFN_vkGetImageMemoryRequirements>(vkGetDeviceProcAddr(device,"vkGetImageMemoryRequirements"));
    dispatch.allocateMemory=reinterpret_cast<PFN_vkAllocateMemory>(vkGetDeviceProcAddr(device,"vkAllocateMemory"));
    dispatch.freeMemory=reinterpret_cast<PFN_vkFreeMemory>(vkGetDeviceProcAddr(device,"vkFreeMemory"));
    dispatch.bindImageMemory=reinterpret_cast<PFN_vkBindImageMemory>(vkGetDeviceProcAddr(device,"vkBindImageMemory"));
    dispatch.getPhysicalDeviceMemoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));
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
    dispatch.destroyPipeline=reinterpret_cast<PFN_vkDestroyPipeline>(vkGetDeviceProcAddr(device,"vkDestroyPipeline"));
    dispatch.cmdBindPipeline=reinterpret_cast<PFN_vkCmdBindPipeline>(vkGetDeviceProcAddr(device,"vkCmdBindPipeline"));
    dispatch.cmdBindDescriptorSets=reinterpret_cast<PFN_vkCmdBindDescriptorSets>(vkGetDeviceProcAddr(device,"vkCmdBindDescriptorSets"));
    dispatch.cmdPushConstants=reinterpret_cast<PFN_vkCmdPushConstants>(vkGetDeviceProcAddr(device,"vkCmdPushConstants"));
    dispatch.cmdDispatch=reinterpret_cast<PFN_vkCmdDispatch>(vkGetDeviceProcAddr(device,"vkCmdDispatch"));
    dispatchCompute=dispatch.cmdDispatch;dispatch.cmdDispatch=countedDispatch;
    imageBarrier=dispatch.cmdPipelineBarrier;dispatch.cmdPipelineBarrier=countedBarrier;
    dispatch.queueSubmit=reinterpret_cast<PFN_vkQueueSubmit>(vkGetDeviceProcAddr(device,"vkQueueSubmit"));
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
    constexpr uint32_t width=128,height=72;constexpr VkDeviceSize bytes=width*height*4;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bci.size=bytes*2;bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer readback{};ok(vkCreateBuffer(device,&bci,nullptr,&readback));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,readback,&req);
    auto host=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);ok(vkBindBufferMemory(device,readback,host,0));
    auto transition=[&](VkImage image,VkImageLayout old,VkImageLayout next){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=old;b.newLayout=next;b.srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
    for(auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_B8G8R8A8_SRGB}){
        std::array<VkImage,2> source{};std::array<VkDeviceMemory,2> sourceMemory{};
        for(int e=0;e<2;++e){VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={64,36,1};ci.mipLevels=1;ci.arrayLayers=2;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;ok(vkCreateImage(device,&ci,nullptr,&source[e]));vkGetImageMemoryRequirements(device,source[e],&req);sourceMemory[e]=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);ok(vkBindImageMemory(device,source[e],sourceMemory[e],0));}
        Fsr1Upscaler fsr;check(fsr.initialize(physical,device,dispatch,format,{64,36},{width,height}),"FSR initialization failed");
        for(uint64_t frame=1;frame<=3;++frame){
            ok(vkResetCommandBuffer(cb,0));
            VkCommandBufferBeginInfo abandoned{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(cb,&abandoned));
            for(int e=0;e<2;++e){
                transition(source[e],frame==1?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                check(fsr.record(cb,source[e],e,frame,{0,0,64,36},{width,height},1)!=VK_NULL_HANDLE,"Abandoned recording failed");
            }
            ok(vkEndCommandBuffer(cb));
            if(frame==1){
                fsr.discardRecordedFrame();
            }else{
                const auto savedSubmit=dispatch.queueSubmit;
                dispatch.queueSubmit=rejectCopySubmit;
                injectedSubmitResult=frame==2?VK_ERROR_OUT_OF_HOST_MEMORY:VK_ERROR_OUT_OF_DEVICE_MEMORY;
                VkSubmitInfo rejected{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                rejected.commandBufferCount=1;rejected.pCommandBuffers=&cb;
                const auto result=dispatch.queueSubmit(queue,1,&rejected,VK_NULL_HANDLE);
                check(result==injectedSubmitResult,"Copy submission did not return injected OOM");
                handleCopySubmitResult(fsr,result);
                dispatch.queueSubmit=savedSubmit;
            }
            dispatchCount=undefinedTransitions=0;
            ok(vkResetCommandBuffer(cb,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(cb,&begin));
            std::array<std::array<float,3>,2> colors{{{0.1f*float(frame),0.2f,0.7f},{0.8f,0.1f*float(frame),0.1f}}};
            std::array<VkImage,2> outputs{};
            for(int e=0;e<2;++e){transition(source[e],frame==1?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkClearColorValue color{{colors[e][0],colors[e][1],colors[e][2],1}};VkClearColorValue poison{{1,0,1,1}};VkImageSubresourceRange unused{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdClearColorImage(cb,source[e],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&poison,1,&unused);VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,1,1};vkCmdClearColorImage(cb,source[e],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);transition(source[e],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                outputs[e]=fsr.record(cb,source[e],e,frame,{0,0,64,36},{width,height},1);check(outputs[e],"FSR output missing");
                handleCopySubmitResult(fsr,VK_SUCCESS);
                check(fsr.record(cb,source[e],e,frame,{0,0,64,36},{width,height},1)==outputs[e],"Same-eye revision reuse failed");
                VkBufferImageCopy copy{};copy.bufferOffset=bytes*e;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};vkCmdCopyImageToBuffer(cb,outputs[e],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);
            }
            check(outputs[0]!=outputs[1],"Eye outputs alias");
            check(dispatchCount==4,"Discarded output reused or valid output recomputed");
            check(undefinedTransitions==6,"Discarded FSR image layout reused");
            VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};hostBarrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;hostBarrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&hostBarrier,0,nullptr,0,nullptr);
            ok(vkEndCommandBuffer(cb));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;ok(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));ok(vkQueueWaitIdle(queue));
            void* mapped{};ok(vkMapMemory(device,host,0,bytes*2,0,&mapped));auto data=static_cast<const unsigned char*>(mapped);
            for(int e=0;e<2;++e)for(uint32_t y=4;y<height-4;++y)for(uint32_t x=4;x<width-4;++x)for(int c=0;c<3;++c)
                check(std::abs(int(data[bytes*e+4*(y*width+x)+c])-int(std::lround(colors[e][c]*255)))<=4,"FSR current-eye color/revision mismatch");
            vkUnmapMemory(device,host);
        }
        std::cout<<"format "<<format<<": abandoned recording and host/device OOM submissions recovered; same-revision EASU+RCAS, UNDEFINED layouts, cached reuse and both eye readbacks passed\n";
        fsr.releaseAfterCompletion();
        for(int e=0;e<2;++e){vkDestroyImage(device,source[e],nullptr);vkFreeMemory(device,sourceMemory[e],nullptr);}
    }
    check(rejectedSubmits==8,"Missing OOM submission coverage");
    vkDestroyBuffer(device,readback,nullptr);vkFreeMemory(device,host,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
