#pragma once
#include "QuadRuntime.h"
#include <fstream>
#include <stdexcept>
namespace argent {
// Explicit diagnostic only. Copies already-rendered layers, never screenshots
// another app or changes the projection. Caller chooses a writable output path.
inline void readbackStereo(Device& d,VkImage image,VkExtent2D extent,VkFormat format,const std::filesystem::path& output){
 if(format!=VK_FORMAT_B8G8R8A8_UNORM&&format!=VK_FORMAT_B8G8R8A8_SRGB&&format!=VK_FORMAT_R8G8B8A8_UNORM&&format!=VK_FORMAT_R8G8B8A8_SRGB)throw std::runtime_error("Unsupported eye readback format");
 std::lock_guard<std::recursive_mutex> lock(*d.queueMutex);
 auto check=[](VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Eye readback Vulkan result="+std::to_string(r));};
 struct Resources {
  Device& d;VkBuffer buffer{};VkDeviceMemory memory{};VkCommandPool pool{};VkFence fence{};void* mapped{};bool submitted{};
  ~Resources(){
   if(submitted)d.proc<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(d.graphicsQueue);
   if(mapped)d.proc<PFN_vkUnmapMemory>("vkUnmapMemory")(d.device,memory);
   if(fence)d.proc<PFN_vkDestroyFence>("vkDestroyFence")(d.device,fence,nullptr);
   if(pool)d.proc<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(d.device,pool,nullptr);
   if(buffer)d.proc<PFN_vkDestroyBuffer>("vkDestroyBuffer")(d.device,buffer,nullptr);
   if(memory)d.proc<PFN_vkFreeMemory>("vkFreeMemory")(d.device,memory,nullptr);
  }
 } r{d};
 const VkDeviceSize eyeBytes=VkDeviceSize(extent.width)*extent.height*4;
 VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=eyeBytes*2;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
 check(d.proc<PFN_vkCreateBuffer>("vkCreateBuffer")(d.device,&bi,nullptr,&r.buffer));
 VkMemoryRequirements req{};d.proc<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(d.device,r.buffer,&req);
 VkPhysicalDeviceMemoryProperties props{};reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceMemoryProperties"))(d.physical,&props);
 uint32_t type=UINT32_MAX;for(uint32_t i=0;i<props.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
 if(type==UINT32_MAX)throw std::runtime_error("No coherent eye readback memory");
 VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
 check(d.proc<PFN_vkAllocateMemory>("vkAllocateMemory")(d.device,&ai,nullptr,&r.memory));check(d.proc<PFN_vkBindBufferMemory>("vkBindBufferMemory")(d.device,r.buffer,r.memory,0));
 VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=d.graphicsFamily;
 check(d.proc<PFN_vkCreateCommandPool>("vkCreateCommandPool")(d.device,&pi,nullptr,&r.pool));
 VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ci.commandPool=r.pool;ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ci.commandBufferCount=1;VkCommandBuffer cb{};
 check(d.proc<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(d.device,&ci,&cb));if(d.setLoaderData)check(d.setLoaderData(d.device,cb));
 VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(d.proc<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb,&begin));
 VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};
 d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
 VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};copy.imageExtent={extent.width,extent.height,1};
 d.proc<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(cb,image,VK_IMAGE_LAYOUT_GENERAL,r.buffer,1,&copy);
 VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=r.buffer;host.size=VK_WHOLE_SIZE;
 d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
 check(d.proc<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb));VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(d.proc<PFN_vkCreateFence>("vkCreateFence")(d.device,&fi,nullptr,&r.fence));
 VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;check(d.proc<PFN_vkQueueSubmit>("vkQueueSubmit")(d.graphicsQueue,1,&submit,r.fence));r.submitted=true;
 check(d.proc<PFN_vkWaitForFences>("vkWaitForFences")(d.device,1,&r.fence,VK_TRUE,UINT64_MAX));r.submitted=false;
 check(d.proc<PFN_vkMapMemory>("vkMapMemory")(d.device,r.memory,0,VK_WHOLE_SIZE,0,&r.mapped));
 const auto* pixels=static_cast<const unsigned char*>(r.mapped);const bool bgra=format==VK_FORMAT_B8G8R8A8_UNORM||format==VK_FORMAT_B8G8R8A8_SRGB;
 std::ofstream file(output,std::ios::binary);file<<"P6\n"<<extent.width*2<<' '<<extent.height<<"\n255\n";
 std::vector<char> row(size_t(extent.width)*6);
 for(uint32_t y=0;y<extent.height;++y){for(unsigned eye=0;eye<2;++eye)for(uint32_t x=0;x<extent.width;++x){const auto* p=pixels+eye*eyeBytes+(size_t(y)*extent.width+x)*4;auto* dst=row.data()+(eye*size_t(extent.width)+x)*3;dst[0]=char(p[bgra?2:0]);dst[1]=char(p[1]);dst[2]=char(p[bgra?0:2]);}file.write(row.data(),row.size());}
 if(!file)throw std::runtime_error("Cannot write eye readback");log("STEREO_READBACK "+output.string());
}
inline void readbackStereoIfRequested(Device& d,VkImage image,VkExtent2D extent,VkFormat format){
 static const auto directory=[] {wchar_t path[32768]{};GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);return std::filesystem::path(path).parent_path();}();
 static uint64_t frames{};if(++frames%120||directory.empty())return;
 const auto request=directory/"capture-eyes.request";std::error_code error;
 if(!std::filesystem::remove(request,error))return;
 try{readbackStereo(d,image,extent,format,directory/("eyes-"+std::to_string(GetTickCount64())+".ppm"));}catch(const std::exception& e){log(e.what());}
}
}
