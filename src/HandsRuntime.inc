// KHARVOX hand renderer: explicit downstream dispatch, no per-frame uploads.
kharvox::hands::HandRenderer handRenderer;
bool handsInitialized{},handsAttempted{};
void releaseHands(){
    if(handsAttempted){std::lock_guard<std::recursive_mutex> guard(*device.queueMutex);handRenderer.shutdown();}
    handsInitialized=handsAttempted=false;
}
void prepareHands(Device& d,bool quad){
    const auto& config=controllerActions.configuration;
    if(quad||handsAttempted||(!config.laserSight&&!kharvox::hands::rendererRequired(config.showHands,config.calibrationMode=="hands",config.calibrationMode=="hud")))return;
    handsAttempted=true;
    KharvoxVulkanDispatch vk{};
    vk.setDeviceLoaderData=d.setLoaderData;
    vk.getDeviceProcAddr=d.gdpa;
    vk.createCommandPool=d.proc<PFN_vkCreateCommandPool>("vkCreateCommandPool");
    vk.destroyCommandPool=d.proc<PFN_vkDestroyCommandPool>("vkDestroyCommandPool");
    vk.allocateCommandBuffers=d.proc<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers");
    vk.resetCommandBuffer=d.proc<PFN_vkResetCommandBuffer>("vkResetCommandBuffer");
    vk.beginCommandBuffer=d.proc<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");
    vk.endCommandBuffer=d.proc<PFN_vkEndCommandBuffer>("vkEndCommandBuffer");
    vk.cmdPipelineBarrier=d.proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
    vk.cmdBlitImage=d.proc<PFN_vkCmdBlitImage>("vkCmdBlitImage");
    vk.cmdCopyImage=d.proc<PFN_vkCmdCopyImage>("vkCmdCopyImage");
    vk.cmdCopyBufferToImage=d.proc<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage");
    vk.cmdClearColorImage=d.proc<PFN_vkCmdClearColorImage>("vkCmdClearColorImage");
    vk.createImage=d.proc<PFN_vkCreateImage>("vkCreateImage");
    vk.destroyImage=d.proc<PFN_vkDestroyImage>("vkDestroyImage");
    vk.getImageMemoryRequirements=d.proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements");
    vk.allocateMemory=d.proc<PFN_vkAllocateMemory>("vkAllocateMemory");
    vk.freeMemory=d.proc<PFN_vkFreeMemory>("vkFreeMemory");
    vk.bindImageMemory=d.proc<PFN_vkBindImageMemory>("vkBindImageMemory");
    vk.createBuffer=d.proc<PFN_vkCreateBuffer>("vkCreateBuffer");
    vk.destroyBuffer=d.proc<PFN_vkDestroyBuffer>("vkDestroyBuffer");
    vk.getBufferMemoryRequirements=d.proc<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements");
    vk.bindBufferMemory=d.proc<PFN_vkBindBufferMemory>("vkBindBufferMemory");
    vk.mapMemory=d.proc<PFN_vkMapMemory>("vkMapMemory");
    vk.unmapMemory=d.proc<PFN_vkUnmapMemory>("vkUnmapMemory");
    vk.getPhysicalDeviceMemoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceMemoryProperties"));
    vk.getPhysicalDeviceFormatProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceFormatProperties"));
    vk.getPhysicalDeviceProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceProperties"));
    vk.getPhysicalDeviceQueueFamilyProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
    vk.createImageView=d.proc<PFN_vkCreateImageView>("vkCreateImageView");
    vk.destroyImageView=d.proc<PFN_vkDestroyImageView>("vkDestroyImageView");
    vk.createSampler=d.proc<PFN_vkCreateSampler>("vkCreateSampler");
    vk.destroySampler=d.proc<PFN_vkDestroySampler>("vkDestroySampler");
    vk.createShaderModule=d.proc<PFN_vkCreateShaderModule>("vkCreateShaderModule");
    vk.destroyShaderModule=d.proc<PFN_vkDestroyShaderModule>("vkDestroyShaderModule");
    vk.createDescriptorSetLayout=d.proc<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout");
    vk.destroyDescriptorSetLayout=d.proc<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout");
    vk.createDescriptorPool=d.proc<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool");
    vk.destroyDescriptorPool=d.proc<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool");
    vk.allocateDescriptorSets=d.proc<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets");
    vk.updateDescriptorSets=d.proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets");
    vk.createPipelineLayout=d.proc<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout");
    vk.destroyPipelineLayout=d.proc<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout");
    vk.createComputePipelines=d.proc<PFN_vkCreateComputePipelines>("vkCreateComputePipelines");
    vk.createGraphicsPipelines=d.proc<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines");
    vk.destroyPipeline=d.proc<PFN_vkDestroyPipeline>("vkDestroyPipeline");
    vk.cmdBindPipeline=d.proc<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
    vk.cmdBindDescriptorSets=d.proc<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets");
    vk.cmdPushConstants=d.proc<PFN_vkCmdPushConstants>("vkCmdPushConstants");
    vk.cmdDispatch=d.proc<PFN_vkCmdDispatch>("vkCmdDispatch");
    vk.createRenderPass=d.proc<PFN_vkCreateRenderPass>("vkCreateRenderPass");
    vk.destroyRenderPass=d.proc<PFN_vkDestroyRenderPass>("vkDestroyRenderPass");
    vk.createFramebuffer=d.proc<PFN_vkCreateFramebuffer>("vkCreateFramebuffer");
    vk.destroyFramebuffer=d.proc<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer");
    vk.cmdBeginRenderPass=d.proc<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass");
    vk.cmdEndRenderPass=d.proc<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass");
    vk.cmdBindVertexBuffers=d.proc<PFN_vkCmdBindVertexBuffers>("vkCmdBindVertexBuffers");
    vk.cmdBindIndexBuffer=d.proc<PFN_vkCmdBindIndexBuffer>("vkCmdBindIndexBuffer");
    vk.cmdDrawIndexed=d.proc<PFN_vkCmdDrawIndexed>("vkCmdDrawIndexed");
    vk.cmdDraw=d.proc<PFN_vkCmdDraw>("vkCmdDraw");
    vk.cmdSetViewport=d.proc<PFN_vkCmdSetViewport>("vkCmdSetViewport");
    vk.cmdSetScissor=d.proc<PFN_vkCmdSetScissor>("vkCmdSetScissor");
    vk.createSemaphore=d.proc<PFN_vkCreateSemaphore>("vkCreateSemaphore");
    vk.destroySemaphore=d.proc<PFN_vkDestroySemaphore>("vkDestroySemaphore");
    vk.createFence=d.proc<PFN_vkCreateFence>("vkCreateFence");
    vk.destroyFence=d.proc<PFN_vkDestroyFence>("vkDestroyFence");
    vk.resetFences=d.proc<PFN_vkResetFences>("vkResetFences");
    vk.waitForFences=d.proc<PFN_vkWaitForFences>("vkWaitForFences");
    vk.queueSubmit=d.proc<PFN_vkQueueSubmit>("vkQueueSubmit");
    vk.queueWaitIdle=d.proc<PFN_vkQueueWaitIdle>("vkQueueWaitIdle");
    std::array<std::vector<VkImage>,2> eyeImages;
    const bool separateEyes=swapchainLayers==2&&stereoEyes[0].handle&&stereoEyes[1].handle;
    if(separateEyes){
        for(unsigned eye=0;eye<2;++eye)for(auto image:stereoEyes[eye].images)eyeImages[eye].push_back(image.image);
    }else{
        for(auto image:images)for(auto& eye:eyeImages)eye.push_back(image.image);
    }
    std::lock_guard<std::recursive_mutex> guard(*d.queueMutex);
    handsInitialized=handRenderer.initialize(d.physical,d.device,boundQueue,boundFamily,vk,
        compositionFormat,{extent,extent},eyeImages,runtimePath().wstring(),log,!separateEyes,true);
    log(std::string("ARGENT_HANDS initialized=")+(handsInitialized?"1":"0")+(swapchainLayers==2?" perEyeSwapchains=1":" arrayLayers=2")+" KHARVOX_PBR=1");
}
kharvox::hands::HandPose handPose(const XrPosef& p,bool valid){
    return {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},valid};
}
void recordHands(const std::array<uint32_t,2>& targets,const sfs::StereoFrame& pair,const std::array<XrCompositionLayerProjectionView,2>& views,kharvox::GameImageLifetime::Use& gameImages){
    if(!handsInitialized)return;
    const auto& config=controllerActions.configuration;
    handRenderer.configureCalibration(kharvox::hands::handProfile(config.profile),config.leftHanded,
        config.calibrationMode=="hands",config.calibrationApply);
    if(pair.pose.quadView)return;
    // Camera-history snapshot includes the native weapon/HUD world placements,
    // converted back to this rendered frame's tracking space.
    input::Snapshot sample{};float depthA{},depthB{};
    const bool matched=camera::renderedHands(pair.pose.serial,sample,depthA,depthB);
    const auto hudPanels=camera::renderedHudPanels(pair.pose.serial);
    const auto hudPlaceholder=camera::renderedHudPlaceholder(pair.pose.serial);
    kharvox::hands::HandSceneTarget scene{};
    VkImageView eyeDepth{};
    const bool stereoDepth=gameImages.select([&]{
        return kharvox::hands::handSceneDepthForExtent(pair.eyes[0].extent,scene)
            &&sfs::eyeAttachmentView(device.device,scene.depthView,1,eyeDepth);
    },[&]{return std::array{kharvox::GameImageLifetime::key(scene.depthImage),
        kharvox::GameImageLifetime::key(scene.depthView)};});
    if(!matched||!stereoDepth||!std::isfinite(depthA+depthB)||depthB==0){
        if(extendedLogging()&&pair.pose.serial%120==0)log("HANDS_SCENE unavailable matched="+std::to_string(matched)+" depth="+std::to_string(stereoDepth));
        return;
    }
    const bool climbing=presentation::attachedWallClimb();
    const bool gameplay=presentation::gameplayInput.load()&&!presentation::syncAttack.load()&&!presentation::nativeAnimation.load()&&!presentation::droneAnimation.load()&&!presentation::scriptedMovement.load()&&!climbing&&!revenant::active();
    if(extendedLogging()){
        static bool lastClimbing{};
        if(lastClimbing!=climbing){lastClimbing=climbing;log(std::string("HANDS_CLIMB visible=")+(climbing?"0":"1"));}
    }
    const bool fresh=sample.active; // Latched weapon pose also remains frozen during controller idle.
    const std::string equippedProfile=player::weaponProfile();
    const auto activeProfile=equippedProfile;
    const auto visibility=kharvox::hands::selectHandVisibility({config.leftHanded,sample.twoHand,
        activeProfile=="fists",handRenderer.availability(),config.showHands||config.calibrationMode=="hands",gameplay&&fresh});
    handRenderer.setCalibrationEnabled(gameplay&&config.calibrationMode=="hands"&&
        (config.profile=="default"||config.profile==equippedProfile));
    const kharvox::hands::HandGameplayState state{kharvox::hands::handProfile(activeProfile),config.leftHanded};
    const auto left=handPose(sample.grip[0],fresh&&sample.gripValid[0]);
    const auto right=handPose(sample.grip[1],fresh&&sample.gripValid[1]);
    XrPosef laserPose{};
    kharvox::hands::HandPose sceneLaser{};
    if(config.laserSight&&gameplay&&fresh&&camera::renderedLaser(pair.pose.serial,equippedProfile.c_str(),laserPose))
        sceneLaser=handPose(laserPose,true);
    for(unsigned eye=0;eye<2;++eye){
        kharvox::hands::HandEyeView view;view.pose=handPose(views[eye].pose,true);
        view.angleLeft=views[eye].fov.angleLeft;view.angleRight=views[eye].fov.angleRight;
        view.angleUp=views[eye].fov.angleUp;view.angleDown=views[eye].fov.angleDown;
        view.imageRectX=views[eye].subImage.imageRect.offset.x;view.imageRectY=views[eye].subImage.imageRect.offset.y;
        view.imageRectWidth=views[eye].subImage.imageRect.extent.width;view.imageRectHeight=views[eye].subImage.imageRect.extent.height;
        view.nativeDepth=true;view.depthA=depthA;view.depthB=depthB/camera::unitsPerMeter();
        view.hudPanels=hudPanels;
        view.hudPlaceholder=hudPlaceholder;
        if(extendedLogging())sfs::markXrCheckpoint(device.device,command,eye?"XR hands right begin":"XR hands left begin");
        const bool recorded=handRenderer.recordSceneDepth(command,eye,targets[eye],scene,view,left,right,visibility,state,sceneLaser);
        if(extendedLogging())sfs::markXrCheckpoint(device.device,command,eye?"XR hands right end":"XR hands left end");
        if((extendedLogging()||!hudPlaceholder.empty())&&pair.pose.serial%120==0)log("HANDS_SCENE eye="+std::to_string(eye)+" recorded="+std::to_string(recorded)+" poseTick="+std::to_string(sample.tick)+" hudPanels="+std::to_string(hudPanels.size())+" placeholderQuads="+std::to_string(hudPlaceholder.size())+" worldPlacement=1 depth="+std::to_string(scene.extent.width)+"x"+std::to_string(scene.extent.height));
    }
}
