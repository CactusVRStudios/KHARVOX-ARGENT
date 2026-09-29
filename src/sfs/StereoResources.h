#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace kharvox::sfs {
// Reconstructed from provider 4.25.5.608, CreateImage RVA 0x1a62a0.
// This describes allocation eligibility, not whether a pass should execute twice.
inline bool stereoImage(const VkImageCreateInfo& i) {
    return i.imageType==VK_IMAGE_TYPE_2D && i.arrayLayers==1 &&
        (i.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT));
}

inline VkImageCreateInfo imageInfo(const VkImageCreateInfo& original) {
    auto result=original;
    if(stereoImage(original)) {
        result.arrayLayers=2;
        // Provider changes PREINITIALIZED to UNDEFINED. Unlike it, reject
        // that case in the allocator: dropping initial contents is not safe.
    }
    return result;
}

// Device-owned registry. Store only successful allocations, and erase before
// destruction so a recycled driver handle cannot inherit stereo classification.
class Images {
    std::shared_mutex mutex_;
    std::unordered_map<VkImage,uint32_t> layers_;
public:
    void track(VkImage image,uint32_t count){std::unique_lock<std::shared_mutex> lock(mutex_);layers_[image]=count;}
    uint32_t layers(VkImage image) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto found=layers_.find(image);return found==layers_.end()?0:found->second;
    }
    VkResult create(VkDevice device, const VkImageCreateInfo& input,
                    const VkAllocationCallbacks* allocator, VkImage* output,
                    PFN_vkCreateImage next) {
        if(!output||!next)return VK_ERROR_INITIALIZATION_FAILED;
        const bool stereo=stereoImage(input);
        if(stereo && input.initialLayout!=VK_IMAGE_LAYOUT_UNDEFINED)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        auto info=imageInfo(input);
        VkImage image{};
        const auto result=next(device,&info,allocator,&image);
        if(result!=VK_SUCCESS)return result;
        { std::unique_lock<std::shared_mutex> lock(mutex_); layers_[image]=stereo?2:1; }
        *output=image;
        return result;
    }
    void destroy(VkDevice device,VkImage image,const VkAllocationCallbacks* allocator,
                 PFN_vkDestroyImage next) {
        { std::unique_lock<std::shared_mutex> lock(mutex_); layers_.erase(image); }
        if(next)next(device,image,allocator);
    }
    VkImageViewCreateInfo viewInfo(const VkImageViewCreateInfo& original) {
        auto result=original;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto found=layers_.find(original.image);
        // CreateImageView RVA 0x1a6620: 2D (1) -> 2D_ARRAY (5), one layer -> two.
        if(found!=layers_.end() && found->second==2 &&
           original.viewType==VK_IMAGE_VIEW_TYPE_2D &&
           original.subresourceRange.baseArrayLayer==0 &&
           original.subresourceRange.layerCount==1) {
            result.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            result.subresourceRange.layerCount=2;
        }
        return result;
    }
    // Use only with a shader compiled by the typed SFS transformer. A mono
    // sampled image has a one-layer array view; injected sampling clamps the
    // requested eye to that layer. Existing arrays/cubes/3D views are unchanged.
    VkImageViewCreateInfo shaderViewInfo(const VkImageViewCreateInfo& original) {
        auto result=viewInfo(original);
        if(result.viewType==VK_IMAGE_VIEW_TYPE_2D)
            result.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        return result;
    }
};

// Own the pNext storage for the duration of vkCreateRenderPass. The provider
// constructs mask/correlation 3 at RVA 0x1a8808/0x1a8813. We support all subpasses
// rather than hardcoding subpassCount=1, and preserve the caller's extension chain.
class RenderPassPlan {
    std::vector<uint32_t> masks_;
    uint32_t correlation_=3;
    VkRenderPassMultiviewCreateInfo multiview_{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};
    VkRenderPassCreateInfo info_{};
    bool valid_=true;
public:
    RenderPassPlan(const VkRenderPassCreateInfo& input,bool stereo):info_(input) {
        if(!stereo)return;
        for(auto p=static_cast<const VkBaseInStructure*>(input.pNext);p;p=p->pNext)
            if(p->sType==VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO)valid_=false;
        if(!input.subpassCount || !input.pSubpasses)valid_=false;
        if(!valid_)return;
        masks_.assign(input.subpassCount,3);
        multiview_.pNext=input.pNext;
        multiview_.subpassCount=input.subpassCount;
        multiview_.pViewMasks=masks_.data();
        multiview_.correlationMaskCount=1;
        multiview_.pCorrelationMasks=&correlation_;
        info_.pNext=&multiview_;
    }
    RenderPassPlan(const RenderPassPlan&)=delete;
    RenderPassPlan& operator=(const RenderPassPlan&)=delete;
    bool valid() const {return valid_;}
    const VkRenderPassCreateInfo& info() const {return info_;}
};

// Dispatch RVA 0x1a385b asks a bound-pipeline policy for a multiplier; only Z is
// multiplied. Shared particle buffers must select mono, not two invocations.
inline bool dispatchDepth(uint32_t source,bool stereo,uint32_t limit,uint32_t& result) {
    const uint32_t multiplier=stereo?2:1;
    if(source>std::numeric_limits<uint32_t>::max()/multiplier)return false;
    const auto value=source*multiplier;
    if(value>limit)return false;
    result=value;
    return true;
}
}
