// Optional FSR 1 output scaling. Menus retain the original byte-copy path.
Fsr1Upscaler fsr1;VkExtent2D sourceExtent{};bool swapchainFsrRequested{};
bool fsrRequested(){static const bool on=[] {char value[8]{};GetEnvironmentVariableA("ARGENT_FSR1",value,8);return std::string(value)=="1";}();return on;}
VkExtent2D fsrOutput(const Source& source,VkFormat targetFormat){
 auto queueProps=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(device.gipa(device.instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
 auto formatProps=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(device.gipa(device.instance,"vkGetPhysicalDeviceFormatProperties"));
 auto getProps=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(device.gipa(device.instance,"vkGetPhysicalDeviceProperties"));
 if(!queueProps||!formatProps||!getProps)return source.extent;
 uint32_t n{};queueProps(device.physical,&n,nullptr);std::vector<VkQueueFamilyProperties> queues(n);queueProps(device.physical,&n,queues.data());
 if(boundFamily>=n||!(queues[boundFamily].queueFlags&VK_QUEUE_COMPUTE_BIT)){log("FSR1 unavailable: copy queue lacks compute");return source.extent;}
 VkFormatProperties from{},to{};formatProps(device.physical,VK_FORMAT_R8G8B8A8_UNORM,&from);formatProps(device.physical,targetFormat,&to);
 if(!(from.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||!(to.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))return source.extent;
 std::array<XrViewConfigurationView,2> views{{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
 uint32_t count{};if(XR_FAILED(XR(xrEnumerateViewConfigurationViews)(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views.data()))||count!=2)return source.extent;
 VkExtent2D output=source.extent;
 for(const auto& v:views){output.width=std::max(output.width,v.recommendedImageRectWidth);output.height=std::max(output.height,v.recommendedImageRectHeight);}
 if(output.width==source.extent.width&&output.height==source.extent.height){log("FSR1 bypass: source already at headset resolution; no EASU/RCAS passes");return source.extent;}
 VkPhysicalDeviceProperties limits{};getProps(device.physical,&limits);
 for(const auto& v:views)if(output.width>v.maxImageRectWidth||output.height>v.maxImageRectHeight)return source.extent;
 if(output.width>limits.limits.maxImageDimension2D||output.height>limits.limits.maxImageDimension2D)return source.extent;
 FsrDispatch dispatch{};
 dispatch.allocateDescriptorSets=VK(vkAllocateDescriptorSets);
 dispatch.allocateMemory=VK(vkAllocateMemory);
 dispatch.bindImageMemory=VK(vkBindImageMemory);
 dispatch.cmdBindDescriptorSets=VK(vkCmdBindDescriptorSets);
 dispatch.cmdBindPipeline=VK(vkCmdBindPipeline);
 dispatch.cmdCopyImage=VK(vkCmdCopyImage);
 dispatch.cmdDispatch=VK(vkCmdDispatch);
 dispatch.cmdPipelineBarrier=VK(vkCmdPipelineBarrier);
 dispatch.cmdPushConstants=VK(vkCmdPushConstants);
 dispatch.createComputePipelines=VK(vkCreateComputePipelines);
 dispatch.createDescriptorPool=VK(vkCreateDescriptorPool);
 dispatch.createDescriptorSetLayout=VK(vkCreateDescriptorSetLayout);
 dispatch.createImage=VK(vkCreateImage);
 dispatch.createImageView=VK(vkCreateImageView);
 dispatch.createPipelineLayout=VK(vkCreatePipelineLayout);
 dispatch.createSampler=VK(vkCreateSampler);
 dispatch.createShaderModule=VK(vkCreateShaderModule);
 dispatch.destroyDescriptorPool=VK(vkDestroyDescriptorPool);
 dispatch.destroyDescriptorSetLayout=VK(vkDestroyDescriptorSetLayout);
 dispatch.destroyImage=VK(vkDestroyImage);
 dispatch.destroyImageView=VK(vkDestroyImageView);
 dispatch.destroyPipeline=VK(vkDestroyPipeline);
 dispatch.destroyPipelineLayout=VK(vkDestroyPipelineLayout);
 dispatch.destroySampler=VK(vkDestroySampler);
 dispatch.destroyShaderModule=VK(vkDestroyShaderModule);
 dispatch.freeMemory=VK(vkFreeMemory);
 dispatch.getImageMemoryRequirements=VK(vkGetImageMemoryRequirements);
 dispatch.queueSubmit=VK(vkQueueSubmit);
 dispatch.updateDescriptorSets=VK(vkUpdateDescriptorSets);
 dispatch.getPhysicalDeviceMemoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(device.gipa(device.instance,"vkGetPhysicalDeviceMemoryProperties"));
 // Pass display format, not attachment format: Eternal's UNORM bytes are
 // display-encoded. RCAS decodes once before the final UNORM->sRGB blit.
 if(!fsr1.initialize(device.physical,device.device,dispatch,targetFormat,source.extent,output))return source.extent;
 return output;
}
