#pragma once
#include <vulkan/vulkan.h>
namespace argent {
// A transfer copy preserves bytes. Declare the already encoded WSI pixels as
// sRGB to the XR compositor; do not gamma-encode them a second time.
inline VkFormat xrDisplayFormat(VkFormat source,bool displaySrgb){
    if(displaySrgb){
        if(source==VK_FORMAT_R8G8B8A8_UNORM)return VK_FORMAT_R8G8B8A8_SRGB;
        if(source==VK_FORMAT_B8G8R8A8_UNORM)return VK_FORMAT_B8G8R8A8_SRGB;
    }
    return source;
}
}
