// Included inside the bootstrap namespace. One static compositor image is
// uploaded during session setup; opening Pause never decodes or uploads it.
bool loadPauseBindingsPixels(std::vector<unsigned char>& pixels, UINT& width, UINT& height) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        const auto path = (runtimePath()/L"assets/argent-controller-bindings.png");
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf())))
            && SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, &decoder))
            && SUCCEEDED(decoder->GetFrame(0, &frame))
            && SUCCEEDED(factory->CreateFormatConverter(&converter))
            && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
            && SUCCEEDED(converter->GetSize(&width, &height))
            && width && height && width <= 4096 && height <= 4096) {
            pixels.resize(size_t(width) * height * 4);
            ok = SUCCEEDED(converter->CopyPixels(nullptr, width * 4,
                static_cast<UINT>(pixels.size()), pixels.data()));
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return ok;
}

bool initializePauseBindings(const std::vector<int64_t>& formats) {
    if (pauseBindingsReady) return true;
    constexpr int64_t format = VK_FORMAT_R8G8B8A8_SRGB;
    if (std::find(formats.begin(), formats.end(), format) == formats.end()) return false;
    std::vector<unsigned char> pixels;
    UINT width{}, height{};
    if (!loadPauseBindingsPixels(pixels, width, height)) return false;
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkCommandPool pool{};
    bool ok = false, imageWaited=false;
    // Every failure unwinds resources. Submitted work is completed before
    // freeing its staging buffer/pool or releasing the OpenXR image.
    do {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = pixels.size(); bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (VK(vkCreateBuffer)(boundDevice, &bi, nullptr, &buffer) != VK_SUCCESS) break;
        VkMemoryRequirements requirements{};
        VK(vkGetBufferMemoryRequirements)(boundDevice, buffer, &requirements);
        VkPhysicalDeviceMemoryProperties properties{};
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(device.gipa(device.instance,"vkGetPhysicalDeviceMemoryProperties"))(device.physical, &properties);
        uint32_t type = properties.memoryTypeCount;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((requirements.memoryTypeBits & (1u << i))
                && (properties.memoryTypes[i].propertyFlags & flags) == flags) { type = i; break; }
        }
        if (type == properties.memoryTypeCount) break;
        VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mi.allocationSize = requirements.size; mi.memoryTypeIndex = type;
        if (VK(vkAllocateMemory)(boundDevice, &mi, nullptr, &memory) != VK_SUCCESS
            || VK(vkBindBufferMemory)(boundDevice, buffer, memory, 0) != VK_SUCCESS) break;
        void* mapped{};
        if (VK(vkMapMemory)(boundDevice, memory, 0, pixels.size(), 0, &mapped) != VK_SUCCESS) break;
        std::memcpy(mapped, pixels.data(), pixels.size()); VK(vkUnmapMemory)(boundDevice, memory);
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
        ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format = format; ci.sampleCount = 1; ci.width = width; ci.height = height;
        ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
        if (XR_FAILED(XR(xrCreateSwapchain)(session, &ci, &pauseBindings.handle))) break;
        pauseBindings.width = width; pauseBindings.height = height;
        uint32_t count{};
        if(XR_FAILED(XR(xrEnumerateSwapchainImages)(pauseBindings.handle,0,&count,nullptr))||!count)break;
        pauseBindings.images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        if(XR_FAILED(XR(xrEnumerateSwapchainImages)(pauseBindings.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(pauseBindings.images.data()))))break;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        uint32_t index{};
        if (XR_FAILED(XR(xrAcquireSwapchainImage)(pauseBindings.handle, &acquire, &index))
            || index >= pauseBindings.images.size()) break;
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = XR_INFINITE_DURATION;
        if (XR(xrWaitSwapchainImage)(pauseBindings.handle, &wait) != XR_SUCCESS) break;
        imageWaited=true;
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pi.queueFamilyIndex = boundFamily;
        if (VK(vkCreateCommandPool)(boundDevice, &pi, nullptr, &pool) != VK_SUCCESS) break;
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCommandBuffer command{};
        if (VK(vkAllocateCommandBuffers)(boundDevice, &ai, &command) != VK_SUCCESS) break;
        if(device.setLoaderData&&device.setLoaderData(boundDevice,command)!=VK_SUCCESS)break;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (VK(vkBeginCommandBuffer)(command, &begin) != VK_SUCCESS) break;
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = pauseBindings.images[index].image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        VK(vkCmdCopyBufferToImage)(command, buffer, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        VK(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (VK(vkEndCommandBuffer)(command) != VK_SUCCESS) break;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        {
            std::lock_guard<std::recursive_mutex> access(*device.queueMutex);
            if (VK(vkQueueSubmit)(boundQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) break;
            if (VK(vkQueueWaitIdle)(boundQueue) != VK_SUCCESS) break;
        }
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        ok = XR_SUCCEEDED(XR(xrReleaseSwapchainImage)(pauseBindings.handle, &release));
        imageWaited=false;
    } while (false);
    if(imageWaited){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR(xrReleaseSwapchainImage)(pauseBindings.handle,&release);}
    if (pool) VK(vkDestroyCommandPool)(boundDevice, pool, nullptr);
    if (buffer) VK(vkDestroyBuffer)(boundDevice, buffer, nullptr);
    if (memory) VK(vkFreeMemory)(boundDevice, memory, nullptr);
    if (!ok && pauseBindings.handle) { XR(xrDestroySwapchain)(pauseBindings.handle); pauseBindings = {}; }
    pauseBindingsReady = ok;
    return ok;
}


XrCompositionLayerQuad pauseBindingsLayer(const XrCompositionLayerQuad& menu){
    auto layer=menu;
    layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    layer.subImage.swapchain=pauseBindings.handle;
    layer.subImage.imageArrayIndex=0;
    layer.subImage.imageRect={{0,0},{int32_t(pauseBindings.width),int32_t(pauseBindings.height)}};
    layer.size={menu.size.width,menu.size.width*float(pauseBindings.height)/float(pauseBindings.width)};
    const auto offset=argent::input::rotate(menu.pose.orientation,{0,-(menu.size.height+layer.size.height)*.5f-.025f,0});
    layer.pose.position=argent::input::add(menu.pose.position,offset);
    return layer;
}
void releasePauseBindings(){
    if(pauseBindings.handle)XR(xrDestroySwapchain)(pauseBindings.handle);
    pauseBindings={};pauseBindingsReady=false;
}
