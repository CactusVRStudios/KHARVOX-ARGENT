#pragma once
#include <vulkan/vulkan.h>
#include <atomic>
#include "../hands/HandSceneTarget.h"
namespace argent::fg {
// SFS tests and non-FG builds need no D3D dependency. Installed before capture.
using BeginObserver=void(*)(VkCommandBuffer,uint32_t);
using BarrierObserver=void(*)(VkCommandBuffer,uint32_t,const VkImageMemoryBarrier*);
using ImageObserver=void(*)(VkImage);
using TargetObserver=void(*)(VkCommandBuffer,uint32_t,const kharvox::hands::HandSceneTarget*);
inline std::atomic<BeginObserver> onBegin{};
inline std::atomic<BarrierObserver> onBarriers{};
inline std::atomic<BarrierObserver> onWrites{};
inline std::atomic<TargetObserver> onTargets{};
inline std::atomic<ImageObserver> onImageDestroyed{};
inline void observeBegin(VkCommandBuffer c,uint32_t family){if(auto f=onBegin.load())f(c,family);}
inline void observeBarriers(VkCommandBuffer c,uint32_t n,const VkImageMemoryBarrier* b){if(auto f=onBarriers.load())f(c,n,b);}
inline void observeWrites(VkCommandBuffer c,uint32_t n,const VkImageMemoryBarrier* b){if(auto f=onWrites.load())f(c,n,b);}
inline void observeTargets(VkCommandBuffer c,uint32_t n,const kharvox::hands::HandSceneTarget* t){if(auto f=onTargets.load())f(c,n,t);}
inline void observeImageDestroyed(VkImage image){if(auto f=onImageDestroyed.load())f(image);}
}
