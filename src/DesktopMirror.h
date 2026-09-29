#pragma once
#include "QuadRuntime.h"
#include "DesktopMirrorPacing.h"
#include "openxr/DisplayFormat.h"
#include <cstdlib>
#include <stdexcept>
namespace argent {
inline VkRect2D fitDesktopEye(VkExtent2D source,VkExtent2D target){
 if(!source.width||!source.height||!target.width||!target.height)return {};
 VkExtent2D fit=target;
 if(uint64_t(source.width)*target.height>uint64_t(target.width)*source.height)
  fit.height=uint32_t(uint64_t(target.width)*source.height/source.width);
 else fit.width=uint32_t(uint64_t(target.height)*source.width/source.height);
 if(!fit.width)fit.width=1;if(!fit.height)fit.height=1;
 return {{int32_t((target.width-fit.width)/2),int32_t((target.height-fit.height)/2)},fit};
}
// Development mirror of an already-rendered eye; it never renders a second frame.
class DesktopMirror {
 VkSwapchainKHR chain{};std::vector<VkImage> images;VkExtent2D extent{},sourceExtent{};
 VkCommandPool pool{};VkCommandBuffer command{};VkSemaphore acquired{};VkFence done{};
 std::vector<VkSemaphore> ready;
 bool pending{};
 bool mirrorEye{},blankPresented{};
 DesktopMirrorPacing pacing;
 static void check(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Mirror Vulkan result="+std::to_string(r));}
public:
 static bool configuredEnabled(){
  char value[8]{};return GetEnvironmentVariableA("ARGENT_DESKTOP_MIRROR",value,sizeof(value))==1&&value[0]=='1';
 }
 static uint32_t configuredFps(){
  char value[32]{};const auto n=GetEnvironmentVariableA("ARGENT_MIRROR_MAX_FPS",value,sizeof(value));
  if(!n||n>=sizeof(value))return 60;
  char* end{};const auto fps=std::strtoul(value,&end,10);
  return end!=value&&!*end&&fps<=240?uint32_t(fps):60;
 }
 VkSwapchainKHR handle()const{return chain;}
 bool needsFrame()const{return chain&&(mirrorEye||!blankPresented);}
 bool create(Device& d,const VkSwapchainCreateInfoKHR& original,uint32_t maxFps=configuredFps(),bool showEye=configuredEnabled()){
  mirrorEye=showEye;blankPresented=false;
  pacing.configure(maxFps);
  auto info=original;info.oldSwapchain=VK_NULL_HANDLE;info.imageArrayLayers=1;info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;info.flags=0;info.pNext=nullptr;
  sourceExtent=original.imageExtent;
  // The finished XR eye contains sRGB pixels. Keep blit conversion symmetric.
  if(showEye){
   const auto target=xrDisplayFormat(original.imageFormat,original.imageColorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
   if(target!=info.imageFormat){
    auto formats=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(d.gipa(d.instance,"vkGetPhysicalDeviceSurfaceFormatsKHR"));
    uint32_t count{};bool supported=false;
    if(formats&&formats(d.physical,info.surface,&count,nullptr)==VK_SUCCESS){
     std::vector<VkSurfaceFormatKHR> available(count);
     if(formats(d.physical,info.surface,&count,available.data())==VK_SUCCESS)
      for(auto f:available)if((f.format==target||f.format==VK_FORMAT_UNDEFINED)&&f.colorSpace==info.imageColorSpace)supported=true;
    }
    if(!supported){log("Desktop mirror: matching sRGB surface format unavailable");return false;}
    info.imageFormat=target;
   }
  }
  auto surfaceCaps=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(d.gipa(d.instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
  VkSurfaceCapabilitiesKHR caps{};
  if(surfaceCaps&&surfaceCaps(d.physical,info.surface,&caps)==VK_SUCCESS){
   if(caps.currentExtent.width!=UINT32_MAX)info.imageExtent=caps.currentExtent;
  }
  // The desktop must not pace XR. Game settings may recreate a FIFO chain
  // after startup CVars have run, so also select a supported unpaced mode here.
  auto presentModes=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(d.gipa(d.instance,"vkGetPhysicalDeviceSurfacePresentModesKHR"));
  uint32_t modeCount{};
  if(presentModes&&presentModes(d.physical,info.surface,&modeCount,nullptr)==VK_SUCCESS){
   std::vector<VkPresentModeKHR> modes(modeCount);
   if(presentModes(d.physical,info.surface,&modeCount,modes.data())==VK_SUCCESS){
    for(auto mode:modes)if(mode==VK_PRESENT_MODE_IMMEDIATE_KHR){info.presentMode=mode;break;}
   }
  }
  log("SFS_MIRROR_PRESENT requested="+std::to_string(original.presentMode)+" selected="+std::to_string(info.presentMode)+" maxFps="+std::to_string(maxFps));
  info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;info.queueFamilyIndexCount=0;info.pQueueFamilyIndices=nullptr;
  if(d.proc<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(d.device,&info,nullptr,&chain)!=VK_SUCCESS)return false;
  extent=info.imageExtent;uint32_t n{};check(d.proc<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(d.device,chain,&n,nullptr));images.resize(n);check(d.proc<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(d.device,chain,&n,images.data()));
  log("SFS_DESKTOP mode="+std::string(mirrorEye?"eye":"blank")+" extent="+std::to_string(extent.width)+"x"+std::to_string(extent.height));
  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pi.queueFamilyIndex=d.graphicsFamily;
  check(d.proc<PFN_vkCreateCommandPool>("vkCreateCommandPool")(d.device,&pi,nullptr,&pool));
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
  check(d.proc<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(d.device,&ai,&command));if(d.setLoaderData)check(d.setLoaderData(d.device,command));
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};check(d.proc<PFN_vkCreateSemaphore>("vkCreateSemaphore")(d.device,&si,nullptr,&acquired));
  // A present wait is retired by reacquiring that WSI image, not merely by
  // completion of the copy fence. Keep one ready semaphore per WSI image.
  ready.resize(images.size());for(auto& sem:ready)check(d.proc<PFN_vkCreateSemaphore>("vkCreateSemaphore")(d.device,&si,nullptr,&sem));
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(d.proc<PFN_vkCreateFence>("vkCreateFence")(d.device,&fi,nullptr,&done));return true;
 }
 void present(Device& d,VkImage source,VkQueue sourceRetirementQueue,DesktopMirrorPacing::Clock::time_point now=DesktopMirrorPacing::Clock::now(),VkExtent2D finalExtent={},VkImageLayout sourceLayout=VK_IMAGE_LAYOUT_GENERAL,bool waitForSource=false){
  if(!needsFrame()||!pacing.due(now))return;
  const auto readExtent=finalExtent.width&&finalExtent.height?finalExtent:sourceExtent;
  if(pending){
   const auto status=d.proc<PFN_vkGetFenceStatus>("vkGetFenceStatus")(d.device,done);
   if(status==VK_NOT_READY)return; // Desktop skips a copy instead of stalling XR.
   check(status);check(d.proc<PFN_vkResetFences>("vkResetFences")(d.device,1,&done));pending=false;
  }
  uint32_t index{};auto r=d.proc<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(d.device,chain,0,acquired,VK_NULL_HANDLE,&index);
  if(r!=VK_SUCCESS&&r!=VK_SUBOPTIMAL_KHR)return;
  std::lock_guard<std::recursive_mutex> lock(*d.queueMutex);
  check(d.proc<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(d.proc<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&begin));
  if(!mirrorEye){
   // Paint the actual WSI image once. No eye read, per-frame copy or present
   // remains after this clear; the compositor retains the black window.
   VkImageMemoryBarrier blank{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};blank.image=images[index];
   blank.srcQueueFamilyIndex=blank.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
   blank.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   blank.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;blank.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;blank.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
   d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&blank);
   VkClearColorValue black{};black.float32[3]=1;
   d.proc<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(command,images[index],blank.newLayout,&black,1,&blank.subresourceRange);
   blank.oldLayout=blank.newLayout;blank.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   blank.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;blank.dstAccessMask=0;
   d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&blank);
  }else{
  VkImageMemoryBarrier b[2]{};for(auto& v:b){v.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;v.srcQueueFamilyIndex=v.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
  b[0].image=source;b[0].oldLayout=sourceLayout;b[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  b[1].image=images[index];b[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,b);
  if(readExtent.width==extent.width&&readExtent.height==extent.height){
   VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={extent.width,extent.height,1};
   d.proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,source,b[0].newLayout,images[index],b[1].newLayout,1,&copy);
  }else{
   VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
   const auto fit=fitDesktopEye(readExtent,extent);
   if(fit.extent.width!=extent.width||fit.extent.height!=extent.height){
    VkClearColorValue black{};black.float32[3]=1;
    d.proc<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(command,images[index],b[1].newLayout,&black,1,&b[1].subresourceRange);
    VkMemoryBarrier clearDone{VK_STRUCTURE_TYPE_MEMORY_BARRIER};clearDone.srcAccessMask=clearDone.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&clearDone,0,nullptr,0,nullptr);
   }
   blit.srcOffsets[1]={int32_t(readExtent.width),int32_t(readExtent.height),1};
   blit.dstOffsets[0]={fit.offset.x,fit.offset.y,0};
   blit.dstOffsets[1]={fit.offset.x+int32_t(fit.extent.width),fit.offset.y+int32_t(fit.extent.height),1};
   d.proc<PFN_vkCmdBlitImage>("vkCmdBlitImage")(command,source,b[0].newLayout,images[index],b[1].newLayout,1,&blit,VK_FILTER_NEAREST);
  }
  b[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b[0].newLayout=sourceLayout;b[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b[0].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
  b[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b[1].newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;b[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b[1].dstAccessMask=0;
  d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,b);
  }
  check(d.proc<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command));VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&stage;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
  submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&ready[index];
  check(d.proc<PFN_vkQueueSubmit>("vkQueueSubmit")(d.graphicsQueue,1,&submit,done));pending=true;pacing.submitted(now);
  // SourceRing retires the source image on the application's present queue.
  // Same-queue retirement covers this read; another queue needs the old wait.
  // XR images must finish their mirror read before xrReleaseSwapchainImage.
  if(mirrorEye&&(waitForSource||sourceRetirementQueue!=d.graphicsQueue))check(d.proc<PFN_vkWaitForFences>("vkWaitForFences")(d.device,1,&done,VK_TRUE,UINT64_MAX));
  VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};p.waitSemaphoreCount=1;p.pWaitSemaphores=&ready[index];p.swapchainCount=1;p.pSwapchains=&chain;p.pImageIndices=&index;
  const auto presented=d.proc<PFN_vkQueuePresentKHR>("vkQueuePresentKHR")(d.graphicsQueue,&p);
  if(!mirrorEye&&(presented==VK_SUCCESS||presented==VK_SUBOPTIMAL_KHR))blankPresented=true;
 }
 void destroy(Device& d){
  if(!chain)return;d.proc<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(d.device);
  if(done)d.proc<PFN_vkDestroyFence>("vkDestroyFence")(d.device,done,nullptr);
  if(acquired)d.proc<PFN_vkDestroySemaphore>("vkDestroySemaphore")(d.device,acquired,nullptr);
  for(auto sem:ready)if(sem)d.proc<PFN_vkDestroySemaphore>("vkDestroySemaphore")(d.device,sem,nullptr);ready.clear();pending=false;
  if(pool)d.proc<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(d.device,pool,nullptr);
  d.proc<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(d.device,chain,nullptr);chain=VK_NULL_HANDLE;
 }
};
}
