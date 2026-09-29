#include "HandSceneDepthTracker.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace kharvox::hands {
namespace {

template <typename Handle> std::uint64_t handleKey(Handle handle) {
    return reinterpret_cast<std::uint64_t>(handle);
}

struct ImageInfo {
    VkExtent3D extent{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkImageUsageFlags usage{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
};
struct ViewInfo {
    VkImage image{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkImageAspectFlags aspects{};
    bool stereoBase{};
};
struct FramebufferInfo {
    VkRenderPass renderPass{};
    VkExtent2D extent{};
    std::vector<VkImageView> attachments;
};
struct AttachmentInfo {
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkAttachmentLoadOp loadOp{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
    VkAttachmentStoreOp storeOp{VK_ATTACHMENT_STORE_OP_DONT_CARE};
    VkImageLayout finalLayout{VK_IMAGE_LAYOUT_UNDEFINED};
};
struct RenderPassInfo {
    std::vector<AttachmentInfo> attachments;
};
struct PipelineInfo {
    bool depthTest{};
    bool depthWrite{};
    VkCompareOp compare{VK_COMPARE_OP_LESS_OR_EQUAL};
};
struct ActivePass {
    VkViewport viewport{};bool viewportKnown{};
    VkFramebuffer framebuffer{};
    VkRenderPass renderPass{};
    float clearDepth{1.f};
    bool clearDepthKnown{};
    bool reverseDepth{};
    bool depthPipelineSeen{};
    std::uint32_t subpass{};
};

std::mutex trackerMutex;
std::unordered_map<std::uint64_t, ImageInfo> images;
std::unordered_map<std::uint64_t, ViewInfo> views;
std::unordered_map<std::uint64_t, FramebufferInfo> framebuffers;
std::unordered_map<std::uint64_t, RenderPassInfo> renderPasses;
std::unordered_map<std::uint64_t, PipelineInfo> pipelines;
std::unordered_map<std::uint64_t, ActivePass> activePasses;
std::unordered_map<std::uint64_t, HandSceneTarget> targets;
// LOAD passes and secondary-command-buffer draws may carry no new evidence
// about the depth convention. Keep the convention of the image's prepass.
std::unordered_map<std::uint64_t, bool> depthConventions;
std::uint64_t begunPasses{},passesWithColorAndDepth{},publishedTargets{};

bool reverseCompare(VkCompareOp compare) {
    return compare == VK_COMPARE_OP_GREATER ||
           compare == VK_COMPARE_OP_GREATER_OR_EQUAL;
}

template <typename Attachment>
AttachmentInfo attachmentInfo(const Attachment& attachment) {
    return {attachment.format, attachment.loadOp, attachment.storeOp, attachment.finalLayout};
}

} // namespace

bool handSceneTrackingEnabled() { return true; }

void handSceneImageCreated(VkImage image, const VkImageCreateInfo& info) {
    if (!handSceneTrackingEnabled() || !image)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    images[handleKey(image)] = {info.extent, info.format, info.usage,
                                info.samples};
    depthConventions.erase(handleKey(image));
}

void handSceneImageDestroyed(VkImage image) {
    if (!handSceneTrackingEnabled() || !image)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    images.erase(handleKey(image));
    depthConventions.erase(handleKey(image));
    targets.erase(handleKey(image));
    for (auto it = targets.begin(); it != targets.end();) {
        if (it->second.depthImage == image)
            it = targets.erase(it);
        else
            ++it;
    }
}

void handSceneImageBarriers(std::uint32_t count,const VkImageMemoryBarrier* barriers) {
    if(!handSceneTrackingEnabled()||!barriers)return;
    // Color-only and buffer-only barriers cannot change a tracked depth layout.
    std::uint32_t firstDepth=0;
    while(firstDepth<count && !(barriers[firstDepth].subresourceRange.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT))++firstDepth;
    if(firstDepth==count)return;
    std::lock_guard lock(trackerMutex);
    for(std::uint32_t i=firstDepth;i<count;++i){
        const auto& barrier=barriers[i];
        if(!(barrier.subresourceRange.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT))continue;
        for(auto& entry:targets)
            if(entry.second.depthImage==barrier.image)entry.second.depthLayout=barrier.newLayout;
    }
}

void handSceneImageViewCreated(VkImageView view,
                               const VkImageViewCreateInfo& info) {
    if (!handSceneTrackingEnabled() || !view)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    views[handleKey(view)] = {info.image, info.format,
                              info.subresourceRange.aspectMask,info.subresourceRange.baseMipLevel==0&&info.subresourceRange.baseArrayLayer==0&&info.subresourceRange.layerCount>=2};
}

void handSceneImageViewDestroyed(VkImageView view) {
    if (!handSceneTrackingEnabled() || !view)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    views.erase(handleKey(view));
    for (auto it = targets.begin(); it != targets.end();) {
        if (it->second.colorView == view || it->second.depthView == view)
            it = targets.erase(it);
        else
            ++it;
    }
}

void handSceneFramebufferCreated(VkFramebuffer framebuffer,
                                 const VkFramebufferCreateInfo& info) {
    if (!handSceneTrackingEnabled() || !framebuffer)
        return;
    FramebufferInfo copy{};
    copy.renderPass = info.renderPass;
    copy.extent = {info.width, info.height};
    if (info.pAttachments)
        copy.attachments.assign(info.pAttachments,
                                info.pAttachments + info.attachmentCount);
    std::lock_guard<std::mutex> lock(trackerMutex);
    framebuffers[handleKey(framebuffer)] = std::move(copy);
}

void handSceneFramebufferDestroyed(VkFramebuffer framebuffer) {
    if (!handSceneTrackingEnabled() || !framebuffer)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    framebuffers.erase(handleKey(framebuffer));
}

void handSceneRenderPassCreated(VkRenderPass renderPass,
                                const VkRenderPassCreateInfo& info) {
    if (!handSceneTrackingEnabled() || !renderPass)
        return;
    RenderPassInfo copy{};
    for (std::uint32_t i = 0; i < info.attachmentCount; ++i)
        copy.attachments.push_back(attachmentInfo(info.pAttachments[i]));
    std::lock_guard<std::mutex> lock(trackerMutex);
    renderPasses[handleKey(renderPass)] = std::move(copy);
}

void handSceneRenderPass2Created(VkRenderPass renderPass,
                                 const VkRenderPassCreateInfo2& info) {
    if (!handSceneTrackingEnabled() || !renderPass)
        return;
    RenderPassInfo copy{};
    for (std::uint32_t i = 0; i < info.attachmentCount; ++i)
        copy.attachments.push_back(attachmentInfo(info.pAttachments[i]));
    std::lock_guard<std::mutex> lock(trackerMutex);
    renderPasses[handleKey(renderPass)] = std::move(copy);
}

void handSceneRenderPassDestroyed(VkRenderPass renderPass) {
    if (!handSceneTrackingEnabled() || !renderPass)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    renderPasses.erase(handleKey(renderPass));
}

void handSceneGraphicsPipelinesCreated(
    std::uint32_t count, const VkGraphicsPipelineCreateInfo* infos,
    const VkPipeline* created) {
    if (!handSceneTrackingEnabled() || !infos || !created)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    for (std::uint32_t i = 0; i < count; ++i) {
        PipelineInfo value{};
        if (infos[i].pDepthStencilState) {
            value.depthTest =
                infos[i].pDepthStencilState->depthTestEnable == VK_TRUE;
            value.depthWrite =
                infos[i].pDepthStencilState->depthWriteEnable == VK_TRUE;
            value.compare = infos[i].pDepthStencilState->depthCompareOp;
        }
        pipelines[handleKey(created[i])] = value;
    }
}

void handScenePipelineDestroyed(VkPipeline pipeline) {
    if (!handSceneTrackingEnabled() || !pipeline)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    pipelines.erase(handleKey(pipeline));
}

void handSceneBeginRenderPass(VkCommandBuffer commandBuffer,
                              const VkRenderPassBeginInfo* info) {
    if (!handSceneTrackingEnabled() || !commandBuffer || !info)
        return;
    ActivePass active{};
    active.framebuffer = info->framebuffer;
    active.renderPass = info->renderPass;
    std::lock_guard<std::mutex> lock(trackerMutex);
    const auto pass = renderPasses.find(handleKey(info->renderPass));
    if (pass != renderPasses.end() && info->pClearValues) {
        const auto count = std::min<std::size_t>(
            pass->second.attachments.size(), info->clearValueCount);
        for (std::size_t i = 0; i < count; ++i) {
            if (handSceneDepthFormat(pass->second.attachments[i].format) &&
                pass->second.attachments[i].loadOp ==
                    VK_ATTACHMENT_LOAD_OP_CLEAR) {
                active.clearDepth = info->pClearValues[i].depthStencil.depth;
                active.clearDepthKnown = true;
                active.reverseDepth = active.clearDepth < .5f;
                break;
            }
        }
    }
    activePasses[handleKey(commandBuffer)] = active;
    ++begunPasses;
}

void handSceneBindPipeline(VkCommandBuffer commandBuffer,
                           VkPipelineBindPoint bindPoint,
                           VkPipeline pipeline) {
    if (!handSceneTrackingEnabled() ||
        bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    const auto active = activePasses.find(handleKey(commandBuffer));
    const auto details = pipelines.find(handleKey(pipeline));
    if (active == activePasses.end() || details == pipelines.end() ||
        !details->second.depthTest || !details->second.depthWrite)
        return;
    active->second.depthPipelineSeen = true;
    if (!active->second.clearDepthKnown)
        active->second.reverseDepth = reverseCompare(details->second.compare);
}

void handSceneViewport(VkCommandBuffer cb,const VkViewport& viewport){
 std::lock_guard<std::mutex> lock(trackerMutex);auto found=activePasses.find(handleKey(cb));
 if(found!=activePasses.end()){found->second.viewport=viewport;found->second.viewportKnown=true;}
}

void handSceneNextSubpass(VkCommandBuffer commandBuffer) {
    if (!handSceneTrackingEnabled())
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    const auto active = activePasses.find(handleKey(commandBuffer));
    if (active != activePasses.end())
        ++active->second.subpass;
}

void handSceneEndRenderPass(VkCommandBuffer commandBuffer) {
    if (!handSceneTrackingEnabled())
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    const auto activeIt = activePasses.find(handleKey(commandBuffer));
    if (activeIt == activePasses.end())
        return;
    const ActivePass active = activeIt->second;
    activePasses.erase(activeIt);
    // idTech 6 records much of the scene in secondary command buffers. The
    // primary owns Begin/EndRenderPass while pipeline binds and draws can live
    // in those secondaries, so requiring a draw observed on the primary drops
    // the otherwise exact WSI-color/depth framebuffer association. The exact
    // presented color-image identity is the stronger and sufficient guard.
    const auto framebuffer = framebuffers.find(handleKey(active.framebuffer));
    const auto pass = renderPasses.find(handleKey(active.renderPass));
    if (framebuffer == framebuffers.end() || pass == renderPasses.end() ||
        framebuffer->second.attachments.size() !=
            pass->second.attachments.size())
        return;

    const auto& attachmentViews = framebuffer->second.attachments;
    const auto& attachmentDescriptions = pass->second.attachments;
    if(active.clearDepthKnown||active.depthPipelineSeen){
        for(auto view:attachmentViews){
            const auto found=views.find(handleKey(view));
            if(found!=views.end()&&(found->second.aspects&VK_IMAGE_ASPECT_DEPTH_BIT))
                depthConventions[handleKey(found->second.image)]=active.reverseDepth;
        }
    }
    for (std::size_t colorIndex = 0; colorIndex < attachmentViews.size();
         ++colorIndex) {
        const auto colorViewIt = views.find(handleKey(attachmentViews[colorIndex]));
        if (colorViewIt == views.end() ||
            !(colorViewIt->second.aspects & VK_IMAGE_ASPECT_COLOR_BIT))
            continue;
        for (std::size_t depthIndex = 0; depthIndex < attachmentViews.size();
             ++depthIndex) {
            const auto depthViewIt =
                views.find(handleKey(attachmentViews[depthIndex]));
            if (depthViewIt == views.end() ||
                !(depthViewIt->second.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) || !depthViewIt->second.stereoBase ||
                (!handSceneDepthFormat(attachmentDescriptions[depthIndex].format)||attachmentDescriptions[depthIndex].storeOp!=VK_ATTACHMENT_STORE_OP_STORE))
                continue;
            const auto depthImageIt = images.find(handleKey(depthViewIt->second.image));
            if (depthImageIt == images.end() ||
                depthImageIt->second.samples != VK_SAMPLE_COUNT_1_BIT)
                continue;
            HandSceneTarget target{};
            target.colorImage = colorViewIt->second.image;
            target.colorView = attachmentViews[colorIndex];
            target.colorFormat = attachmentDescriptions[colorIndex].format;
            target.depthImage = depthViewIt->second.image;
            target.depthView = attachmentViews[depthIndex];
            target.depthFormat = attachmentDescriptions[depthIndex].format;
            target.depthLayout = attachmentDescriptions[depthIndex].finalLayout;
            target.extent = framebuffer->second.extent;
            if(active.viewportKnown){const auto& v=active.viewport;
                // Only a normal full depth range can be compared with the
                // captured scene projection. Reject special weapon/shadow ranges.
                if(!std::isfinite(v.x+v.y+v.width+v.height)||v.x<0||v.y<0||v.width<1||v.height<1||v.minDepth!=0||v.maxDepth!=1
                    ||v.x+v.width>target.extent.width||v.y+v.height>target.extent.height)continue;
                target.depthOffset={int32_t(std::lround(v.x)),int32_t(std::lround(v.y))};
                target.extent={uint32_t(std::lround(v.width)),uint32_t(std::lround(v.height))};
            }
            target.samples = depthImageIt->second.samples;
            const auto convention=depthConventions.find(handleKey(target.depthImage));
            target.reverseDepth = convention!=depthConventions.end()?convention->second:active.reverseDepth;
            ++passesWithColorAndDepth;
            targets[handleKey(target.colorImage)] = target;
            ++publishedTargets;
            break;
        }
    }
}

std::string handSceneTargetDiagnostic(VkImage colorImage,
                                      VkExtent2D requiredExtent) {
    if (!handSceneTrackingEnabled())
        return "tracking=disabled";
    std::lock_guard<std::mutex> lock(trackerMutex);
    std::size_t colorViews{};
    for(const auto&entry:views)
        if(entry.second.image==colorImage
            &&(entry.second.aspects&VK_IMAGE_ASPECT_COLOR_BIT))++colorViews;
    const auto target=targets.find(handleKey(colorImage));
    std::string result="views="+std::to_string(views.size())
        +" colorViewsForWsi="+std::to_string(colorViews)
        +" framebuffers="+std::to_string(framebuffers.size())
        +" renderPasses="+std::to_string(renderPasses.size())
        +" begun="+std::to_string(begunPasses)
        +" paired="+std::to_string(passesWithColorAndDepth)
        +" published="+std::to_string(publishedTargets)
        +" targets="+std::to_string(targets.size());
    if(target!=targets.end())
        result+=" candidateExtent="+std::to_string(target->second.extent.width)
            +"x"+std::to_string(target->second.extent.height)
            +" requiredExtent="+std::to_string(requiredExtent.width)
            +"x"+std::to_string(requiredExtent.height)
            +" depthFormat="+std::to_string(target->second.depthFormat);
    return result;
}

bool handSceneTargetForColor(VkImage colorImage, VkExtent2D requiredExtent,
                             HandSceneTarget& target) {
    target = {};
    if (!handSceneTrackingEnabled() || !colorImage)
        return false;
    std::lock_guard<std::mutex> lock(trackerMutex);
    const auto found = targets.find(handleKey(colorImage));
    if (found == targets.end() ||
        found->second.extent.width != requiredExtent.width ||
        found->second.extent.height != requiredExtent.height ||
        !found->second.colorView || !found->second.depthView ||
        found->second.depthLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        return false;
    target = found->second;
    return true;
}

// Only associations recorded in this frame may supply occlusion. Multiple
// equally sized depth images are ambiguous; never pick an arbitrary one.
void handSceneBeginFrame(){std::lock_guard<std::mutex> lock(trackerMutex);targets.clear();}
bool handSceneDepthForExtent(VkExtent2D extent,HandSceneTarget& target){
 std::lock_guard<std::mutex> lock(trackerMutex);target={};uint64_t area=0;bool ambiguous=false;
 for(const auto& entry:targets){const auto& t=entry.second;
  if(!t.depthView||t.depthLayout==VK_IMAGE_LAYOUT_UNDEFINED||!t.extent.height||!extent.height)continue;
  if(std::abs(double(t.extent.width)/t.extent.height-double(extent.width)/extent.height)>.005)continue;
  auto size=uint64_t(t.extent.width)*t.extent.height;
  if(size>area){area=size;target=t;ambiguous=false;}
  else if(size==area&&target.depthImage!=t.depthImage)ambiguous=true;
 }
 return area&&!ambiguous;
}

void handSceneDeviceDestroyed() {
    if (!handSceneTrackingEnabled())
        return;
    std::lock_guard<std::mutex> lock(trackerMutex);
    images.clear();
    views.clear();
    framebuffers.clear();
    renderPasses.clear();
    pipelines.clear();
    activePasses.clear();
    targets.clear();
    depthConventions.clear();
    begunPasses=passesWithColorAndDepth=publishedTargets=0;
}

} // namespace kharvox::hands
