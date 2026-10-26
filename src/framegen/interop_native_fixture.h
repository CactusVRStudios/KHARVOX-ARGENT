#pragma once
// Synthetic two-layer game targets. These are ordinary Vulkan allocations,
// deliberately independent of DLSS and of the DX-imported conversion outputs.
struct NativeFixture {
 std::array<argent::dlss::Resource,2> resources{};
 std::array<argent::dlss::EyeParameters,2> eyes{};
 std::array<VkDeviceMemory,2> memory{};
 void create(argent::fg::Interop& g,uint32_t width,uint32_t height){
  for(unsigned j=0;j<2;++j){auto& r=resources[j];r.width=width;r.height=height;r.format=j?VK_FORMAT_R16G16_SFLOAT:VK_FORMAT_D32_SFLOAT_S8_UINT;r.range={VkImageAspectFlags(j?VK_IMAGE_ASPECT_COLOR_BIT:VK_IMAGE_ASPECT_DEPTH_BIT),0,1,0,2};
   VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=r.format;ci.extent={width,height,1};ci.mipLevels=1;ci.arrayLayers=2;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
   argent::fg::Interop::check(g.proc<PFN_vkCreateImage>("vkCreateImage")(g.dev.device,&ci,nullptr,&r.image),"native fixture image");
   VkMemoryRequirements req{};g.proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(g.dev.device,r.image,&req);VkPhysicalDeviceMemoryProperties mp{};g.instanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(g.dev.physical,&mp);
   uint32_t type=UINT32_MAX;for(uint32_t t=0;t<mp.memoryTypeCount;++t)if((req.memoryTypeBits&(1u<<t))&&(mp.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=t;break;}
   if(type==UINT32_MAX)throw std::runtime_error("native fixture memory type");VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
   argent::fg::Interop::check(g.proc<PFN_vkAllocateMemory>("vkAllocateMemory")(g.dev.device,&ai,nullptr,&memory[j]),"native fixture memory");
   argent::fg::Interop::check(g.proc<PFN_vkBindImageMemory>("vkBindImageMemory")(g.dev.device,r.image,memory[j],0),"native fixture binding");
   for(unsigned e=0;e<2;++e){auto& out=eyes[e].resources[j+2];out=r;out.range.baseArrayLayer=e;out.range.layerCount=1;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=r.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=r.format;vi.subresourceRange=out.range;
    argent::fg::Interop::check(g.proc<PFN_vkCreateImageView>("vkCreateImageView")(g.dev.device,&vi,nullptr,&out.view),"native fixture eye view");
   }
  }
 }
 void record(argent::fg::Interop& g){
  for(unsigned j=0;j<2;++j){auto& r=resources[j];VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=r.image;b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange=r.range;if(!j)b.subresourceRange.aspectMask|=VK_IMAGE_ASPECT_STENCIL_BIT;b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
   g.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(g.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
   if(j){VkClearColorValue clear{};g.proc<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(g.command,r.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&b.subresourceRange);}
   else{VkClearDepthStencilValue clear{.5f,0};g.proc<PFN_vkCmdClearDepthStencilImage>("vkCmdClearDepthStencilImage")(g.command,r.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&b.subresourceRange);}
   b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
   g.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(g.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
  }
 }
 void destroy(argent::fg::Interop& g){for(unsigned j=0;j<2;++j){for(auto& e:eyes)if(e.resources[j+2].view)g.proc<PFN_vkDestroyImageView>("vkDestroyImageView")(g.dev.device,e.resources[j+2].view,nullptr);if(resources[j].image)g.proc<PFN_vkDestroyImage>("vkDestroyImage")(g.dev.device,resources[j].image,nullptr);if(memory[j])g.proc<PFN_vkFreeMemory>("vkFreeMemory")(g.dev.device,memory[j],nullptr);}}
};
