#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace argent::fg {
struct Layout {VkImage image{};VkImageSubresourceRange range{};VkImageLayout layout{};};
inline uint64_t rangeEnd(uint32_t base,uint32_t count){return count==UINT32_MAX?UINT64_MAX:uint64_t(base)+count;}
inline bool overlaps(const VkImageSubresourceRange& a,const VkImageSubresourceRange& b){
 return (a.aspectMask&b.aspectMask)&&a.baseArrayLayer<rangeEnd(b.baseArrayLayer,b.layerCount)&&b.baseArrayLayer<rangeEnd(a.baseArrayLayer,a.layerCount)&&
  a.baseMipLevel<rangeEnd(b.baseMipLevel,b.levelCount)&&b.baseMipLevel<rangeEnd(a.baseMipLevel,a.levelCount);
}
inline bool covers(const VkImageSubresourceRange& a,const VkImageSubresourceRange& b){
 return (a.aspectMask&b.aspectMask)==b.aspectMask&&a.baseArrayLayer<=b.baseArrayLayer&&rangeEnd(a.baseArrayLayer,a.layerCount)>=rangeEnd(b.baseArrayLayer,b.layerCount)&&
  a.baseMipLevel<=b.baseMipLevel&&rangeEnd(a.baseMipLevel,a.levelCount)>=rangeEnd(b.baseMipLevel,b.levelCount);
}
inline void recordLayout(std::vector<Layout>& list,Layout value){
 // Drop older ranges only when completely overwritten, including their aspects.
 list.erase(std::remove_if(list.begin(),list.end(),[&](const Layout& old){return old.image==value.image&&covers(value.range,old.range);}),list.end());
 list.push_back(value);
}
inline VkImageLayout observedLayout(const std::vector<Layout>& list,VkImage image,const VkImageSubresourceRange& range){
 for(auto i=list.rbegin();i!=list.rend();++i)if(i->image==image&&overlaps(i->range,range))
  return covers(i->range,range)?i->layout:VK_IMAGE_LAYOUT_UNDEFINED;
 return VK_IMAGE_LAYOUT_UNDEFINED;
}
inline VkImageLayout inputLayout(const std::vector<Layout>& prefix,const std::vector<Layout>& submitted,VkImage image,const VkImageSubresourceRange& range){
 // A local overlapping transition supersedes queue history, even if only part of
 // the requested range is known. Never fall back to an older whole-image state.
 for(const auto& l:prefix)if(l.image==image&&overlaps(l.range,range))return observedLayout(prefix,image,range);
 return observedLayout(submitted,image,range);
}
// Render-pass final transitions are implicit Vulkan commands, not image barriers.
// Keep only used attachments. Unused attachments undergo no layout transition.
inline std::vector<VkImageLayout> finalLayouts(const VkRenderPassCreateInfo& pass,bool writesOnly=false){
 std::vector<VkImageLayout> result(pass.attachmentCount,VK_IMAGE_LAYOUT_UNDEFINED);
 auto use=[&](const VkAttachmentReference& r){if(r.attachment<pass.attachmentCount&&(!writesOnly||pass.pAttachments[r.attachment].storeOp==VK_ATTACHMENT_STORE_OP_STORE))result[r.attachment]=pass.pAttachments[r.attachment].finalLayout;};
 for(uint32_t s=0;s<pass.subpassCount;++s){const auto& p=pass.pSubpasses[s];
  if(!writesOnly)for(uint32_t j=0;j<p.inputAttachmentCount;++j)use(p.pInputAttachments[j]);
  for(uint32_t j=0;j<p.colorAttachmentCount;++j){use(p.pColorAttachments[j]);if(p.pResolveAttachments)use(p.pResolveAttachments[j]);}
  if(p.pDepthStencilAttachment&&(!writesOnly||p.pDepthStencilAttachment->layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL||p.pDepthStencilAttachment->layout==VK_IMAGE_LAYOUT_GENERAL))use(*p.pDepthStencilAttachment);
 }
 return result;
}
inline std::vector<VkImageMemoryBarrier> finalTransitions(const std::vector<VkImageLayout>& final,const std::vector<Layout>& views,bool stereo){
 std::vector<VkImageMemoryBarrier> out;
 if(final.size()!=views.size())return out;
 for(size_t j=0;j<final.size();++j)if(views[j].image&&final[j]!=VK_IMAGE_LAYOUT_UNDEFINED){
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=views[j].image;b.subresourceRange=views[j].range;b.newLayout=final[j];
  // A non-multiview framebuffer uses just its first layer (Eternal uses layers=1).
  b.subresourceRange.layerCount=stereo?2:1;out.push_back(b);
 }
 return out; // Metadata only: these barriers must NOT be submitted to Vulkan.
}
}
