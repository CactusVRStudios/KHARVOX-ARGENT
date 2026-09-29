#pragma once
#include <vulkan/vulkan.h>

namespace argent {
inline void finishStereoCopyBarrier(VkImageMemoryBarrier& barrier,bool xrTarget){
    const auto sourceLayout=barrier.oldLayout;
    barrier.oldLayout=barrier.newLayout;
    // UNDEFINED is only an initial discard state, never a destination layout.
    barrier.newLayout=xrTarget?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:sourceLayout;
    barrier.srcAccessMask=xrTarget?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask=xrTarget?(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
        :(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
}
}
