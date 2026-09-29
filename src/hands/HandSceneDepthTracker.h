#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include "HandSceneTarget.h"

namespace kharvox::hands {

bool handSceneTrackingEnabled();
bool handSceneDepthFormat(VkFormat format);
VkImageAspectFlags handSceneDepthAspect(VkFormat format);

void handSceneImageCreated(VkImage image, const VkImageCreateInfo& info);
void handSceneImageDestroyed(VkImage image);
void handSceneImageBarriers(std::uint32_t count,const VkImageMemoryBarrier* barriers);
void handSceneImageViewCreated(VkImageView view,
                               const VkImageViewCreateInfo& info);
void handSceneImageViewDestroyed(VkImageView view);
void handSceneFramebufferCreated(VkFramebuffer framebuffer,
                                 const VkFramebufferCreateInfo& info);
void handSceneFramebufferDestroyed(VkFramebuffer framebuffer);
void handSceneRenderPassCreated(VkRenderPass renderPass,
                                const VkRenderPassCreateInfo& info);
void handSceneRenderPass2Created(VkRenderPass renderPass,
                                 const VkRenderPassCreateInfo2& info);
void handSceneRenderPassDestroyed(VkRenderPass renderPass);
void handSceneGraphicsPipelinesCreated(
    std::uint32_t count, const VkGraphicsPipelineCreateInfo* infos,
    const VkPipeline* pipelines);
void handScenePipelineDestroyed(VkPipeline pipeline);
void handSceneBeginRenderPass(VkCommandBuffer commandBuffer,
                              const VkRenderPassBeginInfo* info);
void handSceneBindPipeline(VkCommandBuffer commandBuffer,
                           VkPipelineBindPoint bindPoint,
                           VkPipeline pipeline);
void handSceneNextSubpass(VkCommandBuffer commandBuffer);
void handSceneEndRenderPass(VkCommandBuffer commandBuffer);

bool handSceneTargetForColor(VkImage colorImage, VkExtent2D requiredExtent,
                             HandSceneTarget& target);
std::string handSceneTargetDiagnostic(VkImage colorImage,
                                      VkExtent2D requiredExtent);
void handSceneDeviceDestroyed();
void handSceneBeginFrame();
void handSceneViewport(VkCommandBuffer,const VkViewport&);
bool handSceneDepthForExtent(VkExtent2D extent,HandSceneTarget& target);

} // namespace kharvox::hands
