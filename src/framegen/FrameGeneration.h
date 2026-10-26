#pragma once
#include "../QuadRuntime.h"
#include "../EternalDlssAbi.h"
#include "options.hpp"
namespace argent::fg {
void attach(Device&);
bool ready();
void observeNativeInputs();
// Called only with current, queue-validated stereo resources pinned by the
// caller. Default conversion is synchronous. With deferCompletion, the caller
// must retain views and producer semaphores until generate() returns.
bool captureNativeInputs(Device&,const sfs::StereoFrame&,const std::array<dlss::Resource,2>&,
 const std::array<dlss::EyeParameters,2>&,const std::array<VkImageLayout,2>&,
 uint32_t,const VkSemaphore*,bool& consumed,bool reverseDepth=false,bool deferCompletion=false);
void beginCommand(VkCommandBuffer,uint32_t);
void imageBarriers(VkCommandBuffer,uint32_t,const VkImageMemoryBarrier*);
// Resolve descriptor layouts in actual queue submission order, before driver submit.
void submitting(VkQueue,uint32_t,const VkCommandBuffer*,const sfs::FramePose* = nullptr);
void submissionFailed(VkQueue);
void forgetImage(VkImage);
void capture(VkCommandBuffer,const std::array<dlss::EyeParameters,2>&,const sfs::FramePose&);
void cancelCapture(uint64_t);
void prepare(const sfs::FramePose&,const sfs::EyeUniforms&,const sfs::Matrix&);
// Consumes source waits at most once. GPU resources remain owned through both presentations.
bool generate(Device&,const sfs::StereoFrame&,uint32_t,const VkSemaphore*,bool& consumed,sfs::StereoFrame&);
void reset();
void destroyIdle(VkDevice);
}
