#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace kharvox::sfs {
// Application-owned stereo sources. No WSI ownership or PRESENT layout reaches
// the driver. OpenXR consumes these on the same device; no D3D import is needed.
class SourceRing {
    struct Slot { VkImage image{}; VkDeviceMemory memory{}; VkFence retired{}; bool acquired{},pending{}; };
    struct Chain { std::array<Slot,5> slots{}; uint32_t count{},cursor{}; bool retired{}; };
    VkDevice device_{}; VkQueue queue_{}; VkPhysicalDeviceMemoryProperties memory_{};
    void (*lockQueue_)(){}; void (*unlockQueue_)(){};
    std::mutex mutex_; std::condition_variable available_;
    // mutex_ is held across fence waits and queue submits in acquire()/present().
    // The image-barrier hook asks ownsImage() for every barrier on every
    // recording thread, so it must never wait on mutex_. It reads this small
    // registry instead, which is only ever locked for a brief vector update.
    // Lock order when both are needed: mutex_ then imagesMutex_.
    std::shared_mutex imagesMutex_; std::vector<VkImage> ownedImages_;
    std::unordered_map<VkSwapchainKHR,std::unique_ptr<Chain>> chains_;
#define SOURCE_FUNCTIONS(X) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) \
    X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkWaitForFences) X(vkQueueSubmit)
#define DECLARE(name) PFN_##name name{};
    SOURCE_FUNCTIONS(DECLARE)
#undef DECLARE
    VkResult submit(VkQueue queue,const VkSubmitInfo& info,VkFence fence) {
        struct Guard { SourceRing& owner; Guard(SourceRing& s):owner(s){if(owner.lockQueue_)owner.lockQueue_();} ~Guard(){if(owner.unlockQueue_)owner.unlockQueue_();} } guard(*this);
        return vkQueueSubmit(queue,1,&info,fence);
    }
    void publishImages(const Chain& chain) {
        std::unique_lock<std::shared_mutex> lock(imagesMutex_);
        for(const auto& slot:chain.slots)if(slot.image)ownedImages_.push_back(slot.image);
    }
    // Withdraw before the images are destroyed so a recycled driver handle
    // cannot be mistaken for one of ours.
    void withdrawImages(const Chain& chain) {
        std::unique_lock<std::shared_mutex> lock(imagesMutex_);
        for(const auto& slot:chain.slots)if(slot.image)ownedImages_.erase(std::remove(ownedImages_.begin(),ownedImages_.end(),slot.image),ownedImages_.end());
    }
    void dispose(Chain& chain) {
        for(auto& slot:chain.slots){
            if(slot.image)vkDestroyImage(device_,slot.image,nullptr);
            if(slot.memory)vkFreeMemory(device_,slot.memory,nullptr);
            if(slot.retired)vkDestroyFence(device_,slot.retired,nullptr);
        }
    }
public:
    bool initialize(VkDevice device,VkQueue queue,PFN_vkGetDeviceProcAddr resolver,
                    const VkPhysicalDeviceMemoryProperties& memory,void(*lockQueue)(),void(*unlockQueue)()) {
        device_=device;queue_=queue;memory_=memory;lockQueue_=lockQueue;unlockQueue_=unlockQueue;
        if(!device||!queue||!resolver||bool(lockQueue)!=bool(unlockQueue))return false;
#define LOAD(name) name=reinterpret_cast<PFN_##name>(resolver(device,#name));if(!name)return false;
        SOURCE_FUNCTIONS(LOAD)
#undef LOAD
        return true;
    }
#undef SOURCE_FUNCTIONS
    bool owns(VkSwapchainKHR handle) {std::lock_guard<std::mutex> lock(mutex_);return chains_.count(handle)!=0;}
    bool ownsImage(VkImage image) {
        if(!image)return false;
        std::shared_lock<std::shared_mutex> lock(imagesMutex_);
        return std::find(ownedImages_.begin(),ownedImages_.end(),image)!=ownedImages_.end();
    }
    VkResult create(const VkSwapchainCreateInfoKHR& input,VkSwapchainKHR* output) {
        if(!output||input.flags||input.pNext||input.imageArrayLayers!=2||!input.imageExtent.width||!input.imageExtent.height||input.minImageCount>5)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        std::lock_guard<std::mutex> lock(mutex_);
        if(input.oldSwapchain&&!chains_.count(input.oldSwapchain))return VK_ERROR_INITIALIZATION_FAILED;
        auto chain=std::make_unique<Chain>();chain->count=(std::max)(input.minImageCount,2u);
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;
        info.format=input.imageFormat;info.extent={input.imageExtent.width,input.imageExtent.height,1};
        info.mipLevels=1;info.arrayLayers=2;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=input.imageUsage|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode=input.imageSharingMode;info.queueFamilyIndexCount=input.queueFamilyIndexCount;info.pQueueFamilyIndices=input.pQueueFamilyIndices;
        for(uint32_t n=0;n<chain->count;++n){
            auto& slot=chain->slots[n];
            auto result=vkCreateImage(device_,&info,nullptr,&slot.image);
            if(result!=VK_SUCCESS){dispose(*chain);return result;}
            VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device_,slot.image,&requirements);
            uint32_t type=UINT32_MAX;
            for(uint32_t n=0;n<memory_.memoryTypeCount;++n)if((requirements.memoryTypeBits&(1u<<n))&&(memory_.memoryTypes[n].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=n;break;}
            if(type==UINT32_MAX){dispose(*chain);return VK_ERROR_FEATURE_NOT_PRESENT;}
            VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=slot.image;
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.pNext=&dedicated;allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
            result=vkAllocateMemory(device_,&allocation,nullptr,&slot.memory);
            if(result==VK_SUCCESS)result=vkBindImageMemory(device_,slot.image,slot.memory,0);
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            if(result==VK_SUCCESS)result=vkCreateFence(device_,&fence,nullptr,&slot.retired);
            if(result!=VK_SUCCESS){dispose(*chain);return result;}
        }
        const auto handle=reinterpret_cast<VkSwapchainKHR>(chain.get());
        publishImages(*chain);
        chains_.emplace(handle,std::move(chain));
        if(input.oldSwapchain)chains_.at(input.oldSwapchain)->retired=true;
        *output=handle;return VK_SUCCESS;
    }
    VkResult enumerate(VkSwapchainKHR handle,uint32_t* count,VkImage* images) {
        std::lock_guard<std::mutex> lock(mutex_);auto found=chains_.find(handle);
        if(found==chains_.end()||!count)return VK_ERROR_INITIALIZATION_FAILED;
        const auto size=found->second->count;
        if(!images){*count=size;return VK_SUCCESS;}
        const uint32_t written=(std::min)(*count,size);
        for(uint32_t n=0;n<written;++n)images[n]=found->second->slots[n].image;
        *count=written;return written==size?VK_SUCCESS:VK_INCOMPLETE;
    }
    VkResult acquire(VkSwapchainKHR handle,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t* index) {
        if(!index||(!semaphore&&!fence))return VK_ERROR_INITIALIZATION_FAILED;
        std::unique_lock<std::mutex> lock(mutex_);auto found=chains_.find(handle);
        if(found==chains_.end())return VK_ERROR_OUT_OF_DATE_KHR;
        auto& chain=*found->second;if(chain.retired)return VK_ERROR_OUT_OF_DATE_KHR;
        const auto start=std::chrono::steady_clock::now();
        auto remaining=[&] {if(timeout==UINT64_MAX)return timeout;const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());return elapsed>=timeout?uint64_t(0):timeout-elapsed;};
        auto freeSlot=[&]{for(uint32_t n=0;n<chain.count;++n)if(!chain.slots[n].acquired)return true;return false;};
        if(!freeSlot()){
            if(!timeout)return VK_NOT_READY;
            if(timeout==UINT64_MAX)available_.wait(lock,freeSlot);
            else if(!available_.wait_for(lock,std::chrono::nanoseconds((std::min)(timeout,uint64_t(INT64_MAX))),freeSlot))return VK_TIMEOUT;
        }
        uint32_t selected=chain.cursor;
        for(uint32_t n=0;n<chain.count;++n){const auto candidate=(chain.cursor+n)%chain.count;if(!chain.slots[candidate].acquired){selected=candidate;break;}}
        auto& slot=chain.slots[selected];
        if(slot.pending){
            auto result=vkWaitForFences(device_,1,&slot.retired,VK_TRUE,remaining());
            if(result!=VK_SUCCESS)return !timeout&&result==VK_TIMEOUT?VK_NOT_READY:result;
            slot.pending=false;
        }
        // Even a virtual acquire MUST signal the application's synchronization.
        VkSubmitInfo signal{VK_STRUCTURE_TYPE_SUBMIT_INFO};signal.signalSemaphoreCount=semaphore?1:0;signal.pSignalSemaphores=semaphore?&semaphore:nullptr;
        const auto result=submit(queue_,signal,fence);
        if(result!=VK_SUCCESS)return result;
        slot.acquired=true;chain.cursor=(selected+1)%chain.count;*index=selected;return VK_SUCCESS;
    }
    VkResult present(VkQueue queue,const VkPresentInfoKHR& info,bool waitsConsumed) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(info.swapchainCount!=1||!info.pSwapchains||!info.pImageIndices)return VK_ERROR_FEATURE_NOT_PRESENT;
        auto found=chains_.find(info.pSwapchains[0]);
        if(found==chains_.end()||info.pImageIndices[0]>=found->second->count)return VK_ERROR_OUT_OF_DATE_KHR;
        auto& slot=found->second->slots[info.pImageIndices[0]];
        if(!slot.acquired)return VK_ERROR_INITIALIZATION_FAILED;
        auto result=vkResetFences(device_,1,&slot.retired);
        if(result!=VK_SUCCESS)return result;
        std::vector<VkPipelineStageFlags> stages(waitsConsumed?0:info.waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkSubmitInfo retire{VK_STRUCTURE_TYPE_SUBMIT_INFO};retire.waitSemaphoreCount=uint32_t(stages.size());retire.pWaitSemaphores=stages.empty()?nullptr:info.pWaitSemaphores;retire.pWaitDstStageMask=stages.data();
        result=submit(queue,retire,slot.retired);
        if(result==VK_SUCCESS){slot.pending=true;slot.acquired=false;available_.notify_one();}
        if(info.pResults)info.pResults[0]=result;
        return result;
    }
    // Caller must retire application/XR users before destroying a swapchain.
    VkResult destroy(VkSwapchainKHR handle) {
        std::lock_guard<std::mutex> lock(mutex_);auto found=chains_.find(handle);
        if(found==chains_.end())return VK_ERROR_OUT_OF_DATE_KHR;
        for(const auto& slot:found->second->slots)if(slot.pending){const auto result=vkWaitForFences(device_,1,&slot.retired,VK_TRUE,UINT64_MAX);if(result!=VK_SUCCESS)return result;}
        withdrawImages(*found->second);
        dispose(*found->second);chains_.erase(found);return VK_SUCCESS;
    }
    void clearAfterDeviceIdle(){
        std::lock_guard<std::mutex> lock(mutex_);
        {std::unique_lock<std::shared_mutex> images(imagesMutex_);ownedImages_.clear();}
        for(auto& entry:chains_)dispose(*entry.second);chains_.clear();
    }
};
}
