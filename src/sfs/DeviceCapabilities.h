#pragma once
#include <vulkan/vulkan.h>
namespace argent::sfs {
// Capability policy, not a vendor allow-list. Do not mutate the application's
// feature chain or add a second representation of Vulkan 1.1 multiview.
struct MultiviewRequest { bool present{},enabled{true},duplicate{}; };
inline MultiviewRequest multiviewRequest(const void* chain){
    MultiviewRequest result;
    for(auto node=static_cast<const VkBaseInStructure*>(chain);node;node=node->pNext){
        VkBool32 enabled{};
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES)
            enabled=reinterpret_cast<const VkPhysicalDeviceMultiviewFeatures*>(node)->multiview;
        else if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES)
            enabled=reinterpret_cast<const VkPhysicalDeviceVulkan11Features*>(node)->multiview;
        else continue;
        result.duplicate|=result.present;result.present=true;result.enabled&=enabled!=0;
    }
    return result;
}
inline const char* stereoCapabilityFailure(const VkPhysicalDeviceProperties& properties,
    VkBool32 multiview,uint32_t maxViews,VkExtent2D extent,const void* chain){
    if(properties.apiVersion<VK_API_VERSION_1_1)return "SFS requires Vulkan 1.1";
    if(!multiview||maxViews<2)return "SFS requires multiview with at least two views";
    if(properties.limits.maxImageArrayLayers<2)return "SFS requires two-layer images";
    if(extent.width>properties.limits.maxImageDimension2D||extent.height>properties.limits.maxImageDimension2D)
        return "Render resolution exceeds the GPU image-size limit";
    const auto requested=multiviewRequest(chain);
    if(requested.duplicate)return "Duplicate multiview feature structures";
    if(!requested.enabled)return "Application feature chain disables multiview";
    return nullptr;
}
}
