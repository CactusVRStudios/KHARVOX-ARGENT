#include "../src/DesktopMirror.h"
#include <cassert>
#include <cstring>
#include <iostream>
namespace argent {void log(const std::string&) {}}
static VkFormat createdFormat{};static uint64_t nextHandle=10;static uint32_t nextImage{};static VkResult acquireResult=VK_SUCCESS,fenceStatus=VK_SUCCESS;
static unsigned submits{},presents{},waits{},acquires{},resets{};static VkSemaphore signal{},presentWait{};static bool idle{};
static bool scaled{},emptyTest{},finalEyeTest{};static unsigned eyeTransitions{};static unsigned blits{},clears{};static VkExtent2D createdExtent{};
static VKAPI_ATTR VkResult VKAPI_CALL createChain(VkDevice,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks*,VkSwapchainKHR* out){createdFormat=info->imageFormat;createdExtent=info->imageExtent;*out=VkSwapchainKHR(nextHandle++);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL images(VkDevice,VkSwapchainKHR,uint32_t* n,VkImage* out){if(out)for(unsigned i=0;i<*n;++i)out[i]=VkImage(100+i);else *n=3;return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* out){*out=VkCommandPool(nextHandle++);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer* out){*out=VkCommandBuffer(30);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL createSem(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore* out){*out=VkSemaphore(nextHandle++);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){*out=VkFence(nextHandle++);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice,VkSwapchainKHR,uint64_t timeout,VkSemaphore sem,VkFence,uint32_t* index){++acquires;assert(timeout==0&&sem);*index=nextImage;return acquireResult;}
static VKAPI_ATTR VkResult VKAPI_CALL status(VkDevice,VkFence){return fenceStatus;}
static VKAPI_ATTR VkResult VKAPI_CALL resetFence(VkDevice,uint32_t,const VkFence*){++resets;return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL resetCommand(VkCommandBuffer,VkCommandBufferResetFlags){return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer,const VkCommandBufferBeginInfo*){return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer){return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t count,const VkImageMemoryBarrier* images){
 if(emptyTest){assert(count==1&&images->image!=VkImage(4));}
 if(finalEyeTest&&count==2){assert(images[0].image==VkImage(777));
  if(eyeTransitions++%2==0)assert(images[0].oldLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL&&images[0].newLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  else assert(images[0].oldLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&images[0].newLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
 }
}
static VKAPI_ATTR void VKAPI_CALL clear(VkCommandBuffer,VkImage image,VkImageLayout layout,const VkClearColorValue* value,uint32_t count,const VkImageSubresourceRange* range){
 assert(image!=VkImage(4)&&layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&count==1&&range->layerCount==1);
 assert(value->float32[0]==0&&value->float32[1]==0&&value->float32[2]==0&&value->float32[3]==1);++clears;
}
static VKAPI_ATTR void VKAPI_CALL copy(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t n,const VkImageCopy* c){assert(n==1&&c->srcSubresource.layerCount==1&&c->extent.width==800);}
static VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue,uint32_t n,const VkSubmitInfo* s,VkFence f){++submits;assert(n==1&&f&&s->waitSemaphoreCount==1&&s->signalSemaphoreCount==1);signal=*s->pSignalSemaphores;return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue,const VkPresentInfoKHR* p){++presents;assert(p->waitSemaphoreCount==1);presentWait=*p->pWaitSemaphores;assert(presentWait==signal);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){++waits;return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL deviceIdle(VkDevice){idle=true;return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice,VkFence,const VkAllocationCallbacks*){assert(idle);}
static VKAPI_ATTR void VKAPI_CALL destroySem(VkDevice,VkSemaphore,const VkAllocationCallbacks*){assert(idle);}
static VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice,VkCommandPool,const VkAllocationCallbacks*){assert(idle);}
static VKAPI_ATTR void VKAPI_CALL destroyChain(VkDevice,VkSwapchainKHR,const VkAllocationCallbacks*){assert(idle);}
static VKAPI_ATTR VkResult VKAPI_CALL caps(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR* out){out->currentExtent=emptyTest?VkExtent2D{1280,720}:VkExtent2D{800,600};return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL blit(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t n,const VkImageBlit* b,VkFilter){
 assert(n==1&&b->srcOffsets[1].x==2496&&b->srcOffsets[1].y==2688&&b->dstOffsets[0].x==121&&b->dstOffsets[0].y==0&&b->dstOffsets[1].x==678&&b->dstOffsets[1].y==600);++blits;
}
static VKAPI_ATTR VkResult VKAPI_CALL surfaceFormats(VkPhysicalDevice,VkSurfaceKHR,uint32_t* count,VkSurfaceFormatKHR* out){if(out)*out={VK_FORMAT_R8G8B8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};else *count=1;return VK_SUCCESS;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance,const char* name){if(!std::strcmp(name,"vkGetPhysicalDeviceSurfaceFormatsKHR"))return reinterpret_cast<PFN_vkVoidFunction>(surfaceFormats);return scaled&&!std::strcmp(name,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR")?reinterpret_cast<PFN_vkVoidFunction>(caps):nullptr;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL proc(VkDevice,const char* name){
#define MAP(api, fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
 MAP(vkCmdBlitImage,blit);
 MAP(vkCmdClearColorImage,clear);
 MAP(vkCreateSwapchainKHR,createChain);MAP(vkGetSwapchainImagesKHR,images);MAP(vkCreateCommandPool,createPool);MAP(vkAllocateCommandBuffers,allocate);MAP(vkCreateSemaphore,createSem);MAP(vkCreateFence,createFence);MAP(vkAcquireNextImageKHR,acquire);MAP(vkGetFenceStatus,status);MAP(vkResetFences,resetFence);MAP(vkResetCommandBuffer,resetCommand);MAP(vkBeginCommandBuffer,begin);MAP(vkEndCommandBuffer,end);MAP(vkCmdPipelineBarrier,barrier);MAP(vkCmdCopyImage,copy);MAP(vkQueueSubmit,submit);MAP(vkQueuePresentKHR,present);MAP(vkWaitForFences,wait);MAP(vkDeviceWaitIdle,deviceIdle);MAP(vkDestroyFence,destroyFence);MAP(vkDestroySemaphore,destroySem);MAP(vkDestroyCommandPool,destroyPool);MAP(vkDestroySwapchainKHR,destroyChain);
#undef MAP
 assert(false);return nullptr;
}
int main(){
 for(uint32_t height:{720u,1080u,1440u,2160u}){
  VkExtent2D target{height*16/9,height};
  for(VkExtent2D source: {VkExtent2D{2496,2688},VkExtent2D{4842,2688},VkExtent2D{1920,1080}}){
   auto fit=argent::fitDesktopEye(source,target);
   assert(fit.extent.width<=target.width&&fit.extent.height<=target.height);
   const auto error=int64_t(fit.extent.width)*source.height-int64_t(fit.extent.height)*source.width;
   assert(std::abs(error)<=std::max(source.width,source.height));
  }
 }
 argent::Device d;d.device=VkDevice(1);d.graphicsQueue=VkQueue(2);d.gipa=instanceProc;d.gdpa=proc;argent::DesktopMirror mirror;
 VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.imageExtent={800,600};assert(mirror.create(d,info,0,true));
 mirror.present(d,VkImage(4),d.graphicsQueue);auto first=signal;assert(submits==1&&presents==1&&waits==0);
 fenceStatus=VK_NOT_READY;mirror.present(d,VkImage(4),d.graphicsQueue);assert(submits==1&&acquires==1&&resets==0);
 fenceStatus=VK_SUCCESS;nextImage=1;mirror.present(d,VkImage(4),d.graphicsQueue);assert(submits==2&&signal!=first&&resets==1&&waits==0);
 nextImage=0;mirror.present(d,VkImage(4),d.graphicsQueue);assert(submits==3&&signal==first);
 acquireResult=VK_TIMEOUT;mirror.present(d,VkImage(4),d.graphicsQueue);assert(submits==3&&waits==0);
 acquireResult=VK_SUCCESS;mirror.present(d,VkImage(4),VkQueue(999));assert(submits==4&&waits==1); // Cross-queue source retirement must wait.
 mirror.destroy(d);assert(idle);
 scaled=true;info.imageExtent={2496,2688};assert(mirror.create(d,info,60,true));
 using Clock=argent::DesktopMirrorPacing::Clock;auto start=Clock::time_point{};
 mirror.present(d,VkImage(4),d.graphicsQueue,start);assert(blits==1);
 const auto oldAcquires=acquires,oldSubmits=submits,oldPresents=presents,oldResets=resets;
 mirror.present(d,VkImage(4),d.graphicsQueue,start+std::chrono::milliseconds(8));
 assert(acquires==oldAcquires&&submits==oldSubmits&&presents==oldPresents&&resets==oldResets);
 mirror.present(d,VkImage(4),d.graphicsQueue,start+std::chrono::milliseconds(17));assert(blits==2);mirror.destroy(d);
 for(unsigned xrRate:{72u,80u,90u,120u,144u}){
  argent::DesktopMirrorPacing cadence;cadence.configure(60);unsigned copied=0;
  for(unsigned i=0;i<xrRate*10;++i){auto now=start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(i)/xrRate));
   if(cadence.due(now)){cadence.submitted(now);++copied;}}
  assert(copied>=599&&copied<=601);
  auto later=start+std::chrono::seconds(20);assert(cadence.due(later));cadence.submitted(later);
  assert(!cadence.due(later+std::chrono::milliseconds(1))); // No catch-up burst.
 }
 // Final XR eye uses its output resolution (including FSR), not the game's source extent.
 finalEyeTest=true;info.imageExtent={800,600};info.imageFormat=VK_FORMAT_R8G8B8A8_UNORM;assert(mirror.create(d,info,0,true));assert(createdFormat==VK_FORMAT_R8G8B8A8_SRGB);
 const auto finalWaits=waits,finalBlits=blits;
 mirror.present(d,VkImage(777),d.graphicsQueue,start,{2496,2688},VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,true);
 assert(eyeTransitions==2&&waits==finalWaits+1&&blits==finalBlits+1);
 mirror.destroy(d);finalEyeTest=false;info.imageExtent={2496,2688};
 emptyTest=true;assert(mirror.create(d,info,60,false));assert(mirror.needsFrame());
 assert(createdExtent.width==1280&&createdExtent.height==720&&info.imageExtent.width==2496&&info.imageExtent.height==2688);
 const auto beforeClears=clears,beforeBlits=blits,beforeWaits=waits;
 mirror.present(d,VkImage(4),VkQueue(999),start);assert(clears==beforeClears+1&&!mirror.needsFrame());
 const auto blankAcquires=acquires,blankSubmits=submits,blankPresents=presents,blankResets=resets;
 for(unsigned frame=1;frame<=1200;++frame)mirror.present(d,VkImage(4),VkQueue(999),start+std::chrono::milliseconds(frame*10));
 assert(acquires==blankAcquires&&submits==blankSubmits&&presents==blankPresents&&resets==blankResets);
 assert(clears==beforeClears+1&&blits==beforeBlits&&waits==beforeWaits);mirror.destroy(d);
 assert(mirror.create(d,info,60,false));assert(mirror.needsFrame());mirror.present(d,VkImage(4),d.graphicsQueue,start);
 assert(clears==beforeClears+2&&!mirror.needsFrame());mirror.destroy(d);
 std::cout<<"Mirror: lifetime, independent extents, cadence and one-shot blank 720p output without eye reads verified\n";
}
