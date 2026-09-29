#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

namespace kharvox::hands {

// A non-owning snapshot of the DOOM framebuffer attachments associated with
// one native swapchain image. The layer keeps the Vulkan objects alive; the
// hand renderer only borrows them while recording the Present copy command.
struct HandSceneTarget {
    VkImage colorImage{};
    VkImageView colorView{};
    VkFormat colorFormat{VK_FORMAT_UNDEFINED};
    VkImage depthImage{};
    VkImageView depthView{};
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    VkImageLayout depthLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkExtent2D extent{};
    VkOffset2D depthOffset{};
    VkExtent2D depthExtent{}; // native render resolution, before DLSS/FSR
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    bool reverseDepth{};
    // Native mirrors survive across frames. Hand self-depth must never be
    // written back into their engine-owned depth/stencil contents.
    bool copyDepthForHands{};
    uint32_t depthArrayLayer{};
};

inline bool handSceneDepthFormat(VkFormat f){return f==VK_FORMAT_D32_SFLOAT||f==VK_FORMAT_D32_SFLOAT_S8_UINT||f==VK_FORMAT_D24_UNORM_S8_UINT||f==VK_FORMAT_D16_UNORM;}
inline VkImageAspectFlags handSceneDepthAspect(VkFormat f){return VK_IMAGE_ASPECT_DEPTH_BIT|((f==VK_FORMAT_D32_SFLOAT_S8_UINT||f==VK_FORMAT_D24_UNORM_S8_UINT)?VK_IMAGE_ASPECT_STENCIL_BIT:0);}
}
