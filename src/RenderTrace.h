#pragma once
#include "QuadRuntime.h"
#include <array>
#include <cstring>
namespace argent::trace {
// Capture records use recording epochs so recycled Vulkan handles do not join
// unrelated command buffers. This observes commands; it never changes them.
void reset(VkCommandBuffer,VkCommandPool=VK_NULL_HANDLE) noexcept;
void resetPool(VkCommandPool,bool destroy=false) noexcept;
uint64_t currentFrame() noexcept;
void forget(VkCommandBuffer) noexcept;
void record(VkCommandBuffer,const char*,std::array<uint64_t,6> = {}) noexcept;
void submit(VkQueue,uint32_t,const VkCommandBuffer*) noexcept;
void present() noexcept;
void object(VkDevice,const char*,uint64_t,std::array<uint64_t,6> = {}) noexcept;
template<class T> uint64_t id(T h){return reinterpret_cast<uintptr_t>(h);}
inline uint64_t floatBits(float v){uint32_t bits;std::memcpy(&bits,&v,sizeof(bits));return bits;}
}
