// OpenXR prediction/render/submission sequence adapted from KHARVOX.
// Included inside namespace argent; all Vulkan calls use downstream dispatch.
namespace {
kharvox::CinewindowAnchor menuAnchor;
kharvox::CinewindowCaptureReadiness menuReadiness;
bool previousMenuQuad{};
bool externalFramegen{};
bool earlyStereoReleaseRequested(uint64_t serial){
    if constexpr(cleanRelease)return true;
    static const auto modeFile=[] {wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
        return n&&n<32768?std::filesystem::path(path).parent_path()/L"test-xr-sync-mode":runtimePath()/L"logs/test-xr-sync-mode";}();
    static bool early=true,initialized=false;
    if(!initialized||serial%120==0){
        int mode=1;std::ifstream input(modeFile);if(input){int requested{};if(input>>requested&&(requested==0||requested==1))mode=requested;}
        if(!initialized||early!=(mode==1))log("XR_SYNC_MODE requestedEarly="+std::to_string(mode)+" source=test-xr-sync-mode");
        early=mode==1;initialized=true;
    }
    return early;
}
double beginPreparedStereo(Device& d){
    if(!stereoPending.waited)return 0;
    const bool steam=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
    const auto beginFn=XR(xrBeginFrame);
    double elapsed{};
    auto call=[&]{
        XrFrameBeginInfo info{XR_TYPE_FRAME_BEGIN_INFO};
        std::lock_guard<std::recursive_mutex> q(*d.queueMutex);
        const auto start=std::chrono::steady_clock::now();
        const auto result=beginFn(session,&info);
        elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        check(result,"xrBeginFrame stereo");
        if(steam){++steamBeginCalls;steamFrameBegun=true;}
    };
    if(steam)xrWorker().invoke(call);else call();
    stereoPending.waited=false;stereoPending.begun=true;
    if(framegenClock.active)framegenWait.prime(session,XR(xrWaitFrame));
    return elapsed;
}
void cancelStereoImpl(){
    framegenClock.active=false;
    // Even a prepared image that was cancelled before XR copying must balance
    // its successful wait. Never leave the runtime stuck in pre-rendering.
    if(stereoPending.waited)beginPreparedStereo(device);
    if(!stereoPending.begun)return;
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=stereoPending.pose.displayTime;
    end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
    const auto endCall=[&]{std::lock_guard<std::recursive_mutex> queueGuard(*device.queueMutex);return XR(xrEndFrame)(session,&end);};
    const auto result=steamRuntime?xrWorker().invoke(endCall):endCall();
    stereoPending.begun=false;
    if(steamRuntime){steamFrameBegun=false;++steamEndCalls;}
    check(result,"xrEndFrame empty stereo");
}
DWORD beginStereoCycle(Device& d,fg::WaitedFrame& waited,bool beginNow=true){
    const bool steam=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
    const auto waitFn=XR(xrWaitFrame);
    auto call=[&]{
        waited=framegenWait.take(session,waitFn);
        if(steam)++steamWaitCalls;
        check(waited.result,"xrWaitFrame stereo");
        return GetCurrentThreadId();
    };
    const auto thread=steam?xrWorker().invoke(call):call();
    stereoPending.waited=true;
    stereoPending.pose.displayTime=waited.state.predictedDisplayTime;
    if(beginNow)waited.beginCallMs=beginPreparedStereo(d);
    return thread;
}
void stopFramegenClock(){
    framegenClock.active=false;
    std::exception_ptr failure;
    try{cancelStereoImpl();}catch(...){failure=std::current_exception();}
    if(framegenWait.pending()){
        // A successful speculative wait still needs its matching begin/end,
        // including on pause, swapchain recreation and session shutdown.
        try{
            fg::WaitedFrame waited;
            beginStereoCycle(device,waited);
            cancelStereoImpl();
        }catch(...){if(!failure)failure=std::current_exception();}
    }
    if(failure)std::rethrow_exception(failure);
}
bool beginStereoImpl(Device& d,const Source& source,sfs::FramePose& pose,XrPosef& head,bool quadView,
                     const sfs::Matrix* cinematicProjection,sfs::EyeUniforms* cinematicUniforms){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!instance||failed||!d.graphicsQueue)return false;
    try{
        if(stereoPending.begun||stereoPending.waited)throw std::runtime_error("Previous stereo frame was not retired");
        if(!session){menuAnchor={};menuReadiness={};previousMenuQuad=false;startSession(d,d.graphicsQueue,d.graphicsFamily,d.graphicsIndex);externalFramegen=fg::externalFramegenLoaded();if(externalFramegen)log("FRAMEGEN_EXTERNAL OFXR detected: wall-clock turn timing, completed Vulkan eye copies, no native DLSS-G stacking");}
        if(boundDevice!=d.device)throw std::runtime_error("Stereo Vulkan device changed");
        events();if(!running)return false;createSwapchain(source,2,!quadView);
        // Do not initialize a second NGX/D3D12 session during startup/menu device
        // setup. Wait for actual successful native stereo DLSS and gameplay.
        fg::observeNativeInputs();
        if(!quadView&&!cinematicProjection&&dlss::stereoEvaluated.load(std::memory_order_acquire)&&!dlss::failed())fg::attach(d);
        const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
        const DWORD callerThread=GetCurrentThreadId();
        fg::WaitedFrame waited;
        // Circle prepares native/FG offscreen images before the XR consumer's
        // rendering interval. Keep the same separation here: only XR copies and
        // overlays run between begin/end, not the entire native game GPU drain.
        const bool deferredBegin=fg::ready()&&!externalFramegen&&!quadView&&!cinematicProjection;
        if(fg::requested()){
            static int lastDeferred=-1;
            if(lastDeferred!=int(deferredBegin)){
                log("FRAMEGEN_LIFECYCLE deferred="+std::to_string(deferredBegin)+" nativeDlssEvaluated="+std::to_string(dlss::stereoEvaluated.load())+" quad="+std::to_string(quadView)+" cinematic="+std::to_string(cinematicProjection!=nullptr));
                lastDeferred=int(deferredBegin);
            }
        }
        if(!deferredBegin)framegenClock.active=false;
        const DWORD lifecycleThread=beginStereoCycle(d,waited,!deferredBegin);
        const auto& state=waited.state;
        framegenBeginWaitMs=waited.callerWaitMs;
        framegenBeginCallMs=waited.beginCallMs;
        framegenClock.observe(state.predictedDisplayPeriod,headsetRefreshHz);
        if(quadView||cinematicProjection||externalFramegen)framegenClock.active=false;
        stereoDisplayTime=state.predictedDisplayTime;
        if(steamRuntime){
            if(stereoSerial<8||stereoSerial%120==0)log("[STEAM-XR-STEREO] prepared lifecycleThread="+std::to_string(lifecycleThread)+" callerThread="+std::to_string(callerThread)+" shouldRender="+std::to_string(state.shouldRender)+" predicted="+std::to_string(state.predictedDisplayTime)+" period="+std::to_string(state.predictedDisplayPeriod)+" deferredBegin="+std::to_string(deferredBegin)+" wait/begin/end="+std::to_string(steamWaitCalls)+"/"+std::to_string(steamBeginCalls)+"/"+std::to_string(steamEndCalls));
        }
        const auto realTime=framegenClock.realTime(state.predictedDisplayTime,!quadView&&!cinematicProjection);
        stereoPending.pose={};stereoPending.pose.displayTime=realTime;
        controllerActions.update(realTime,sessionState==XR_SESSION_STATE_FOCUSED,!quadView);
        stereoPending.hands=input::snapshot();
        if(!state.shouldRender){menuReadiness.trackedSince=0;cancelStereoImpl();return false;}
        auto& next=stereoPending.pose;next.serial=++stereoSerial;
        for(auto& view:next.views)view.type=XR_TYPE_VIEW;
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime=realTime;locate.space=localSpace;
        XrViewState views{XR_TYPE_VIEW_STATE};uint32_t count{};
        check(XR(xrLocateViews)(session,&locate,&views,2,&count,next.views.data()),"xrLocateViews stereo");
        constexpr auto valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        if(count!=2||(views.viewStateFlags&valid)!=valid){menuReadiness.trackedSince=0;cancelStereoImpl();return false;}
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        check(XR(xrLocateSpace)(space,localSpace,realTime,&location),"xrLocateSpace head");
        constexpr auto headValid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if((location.locationFlags&headValid)!=headValid){menuReadiness.trackedSince=0;cancelStereoImpl();return false;}
        head=location.pose;next.predictedHead=head;next.quadView=quadView;
        camera::updateHeadsetFov(head,next.views.data(),2);
        if(referenceChangeTime)menuReadiness.referenceChanged(referenceChangeTime);
        if(referenceChangeTime&&state.predictedDisplayTime>=referenceChangeTime){next.recenterRequested=true;referenceChangeTime=0;menuAnchor={};}
        next.headPositionTracked=(location.locationFlags&XR_SPACE_LOCATION_POSITION_TRACKED_BIT)!=0;
        if(quadView){
            if(!previousMenuQuad){menuAnchor={};menuReadiness.trackedSince=0;}
            constexpr auto trackedViews=XR_VIEW_STATE_ORIENTATION_TRACKED_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT;
            constexpr auto trackedHead=XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            const bool ready=menuReadiness.ready(state.predictedDisplayTime,menuAnchorSessionVisible(sessionState),
                (views.viewStateFlags&trackedViews)==trackedViews&&(location.locationFlags&trackedHead)==trackedHead);
            if(!menuAnchor.valid&&ready){auto q=head.orientation;auto p=head.position;
                if(kharvox::captureCinewindowAnchor(menuAnchor,q.x,q.y,q.z,q.w,p.x,p.y,p.z))
                    log("MENU_ANCHOR captured after tracked settling height="+std::to_string(p.y));}
            next.quadHeadLocked=!menuAnchor.valid;
            // VIEW space keeps the temporary panel at eye height, even if LOCAL
            // tracking reports a stale floor-level position during startup.
            if(next.quadHeadLocked)next.quadPose={{0,0,0,1},{0,0,-2.5f}};
            else {auto p=kharvox::fixedCinewindowLayerPose(menuAnchor,2.5f);
                next.quadPose={{p.orientationX,p.orientationY,p.orientationZ,p.orientationW},{p.positionX,p.positionY,p.positionZ}};}
        }else {menuAnchor={};menuReadiness.trackedSince=0;}
        next.stereoQuad=false;
        constexpr auto tracked=XR_VIEW_STATE_ORIENTATION_TRACKED_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT;
        if(cinematicProjection&&cinematicUniforms&&(views.viewStateFlags&tracked)==tracked)
            next.stereoQuad=cinematicQuadProjection(*cinematicProjection,next,source.extent,camera::unitsPerMeter(),*cinematicUniforms);
        previousMenuQuad=quadView;pose=next;
        return true;
    }catch(const std::exception& error){
        log(std::string("STEREO_BEGIN_FAILED ")+error.what());
        try{stopFramegenClock();}catch(...){}failed=true;return false;
    }
}
using StereoScheduleClock=std::chrono::steady_clock;
struct EndpointTiming {double generationMs{},waitMs{};XrTime midpointTime{};};
void noteFramegenCadence(bool generated,double generationMs,double endpointWaitMs,double endpointBeginMs);
void publishStereoCopy(Device& d,const sfs::StereoFrame& pair,
    const std::array<XrCompositionLayerProjectionView,2>& views,
    std::array<bool,2>& acquired,std::array<bool,2>& waited,bool measure,
    StereoScheduleClock::time_point& releaseAt,StereoScheduleClock::time_point& endAt,StereoScheduleClock::time_point& endDone){
    using ScheduleClock=StereoScheduleClock;
        if(measure)releaseAt=ScheduleClock::now();
        for(unsigned e=0;e<2;++e){
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            {std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);check(XR(xrReleaseSwapchainImage)(stereoEyes[e].handle,&release),"release stereo eye image");}acquired[e]=waited[e]=false;
        }
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=localSpace;projection.viewCount=2;projection.views=views.data();
        std::array<const XrCompositionLayerBaseHeader*,3> layers{reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection),nullptr,nullptr};
        uint32_t layerCount=1;
        std::array<XrCompositionLayerQuad,2> quads{};
        if(pair.pose.quadView){
            layerCount=pair.pose.stereoQuad?2:1;
            quads=stereoQuadLayers(pair.pose,{stereoEyes[0].handle,stereoEyes[1].handle},extent,space,localSpace);
            for(unsigned e=0;e<layerCount;++e)layers[e]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[e]);
        }
        XrCompositionLayerQuad bindingsLayer{};
        if(pair.pose.quadView&&!pair.pose.stereoQuad&&pauseBindingsReady&&presentation::pauseRootVisible()){
            bindingsLayer=pauseBindingsLayer(quads[0]);
            layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&bindingsLayer);
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=pair.pose.displayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=layerCount;end.layers=layers.data();
        const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
        XrResult endResult{XR_SUCCESS};
        DWORD lifecycleThread=GetCurrentThreadId();
        if(measure)endAt=ScheduleClock::now();
        if(steamRuntime){
            lifecycleThread=xrWorker().invoke([&]{
                std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);
                endResult=XR(xrEndFrame)(session,&end);
                return GetCurrentThreadId();
            });
        }else{
            static FrameTiming::Totals t;FrameTiming timing("xrEndFrame",t);
            std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);
            endResult=XR(xrEndFrame)(session,&end);
        }
        if(measure)endDone=ScheduleClock::now();
        stereoPending.begun=false;
        if(steamRuntime){steamFrameBegun=false;++steamEndCalls;}
        check(endResult,"xrEndFrame composition");
        if(pair.pose.stereoDiagnostic&&extendedLogging())log("SYNC_STEREO_SUBMIT serial="+std::to_string(pair.pose.serial)+
            " sync="+std::to_string(pair.pose.diagnosticSync)+" quad="+std::to_string(pair.pose.quadView)+
            " stereoQuad="+std::to_string(pair.pose.stereoQuad)+" distinctEyeSwapchains="+std::to_string(stereoEyes[0].handle!=stereoEyes[1].handle)+
            " distinctSourceSubresources="+std::to_string(pair.eyes[0].image!=pair.eyes[1].image||pair.eyes[0].layer!=pair.eyes[1].layer)+
            " sourceLayers="+std::to_string(pair.eyes[0].layer)+","+std::to_string(pair.eyes[1].layer)+
            " endFrameResult="+std::to_string(endResult)+" pixelStereo=unverified");
        if(steamRuntime&&(steamEndCalls<=8||steamEndCalls%120==0))log("[STEAM-XR-STEREO] end lifecycleThread="+std::to_string(lifecycleThread)+" callerThread="+std::to_string(GetCurrentThreadId())+" wait/begin/end="+std::to_string(steamWaitCalls)+"/"+std::to_string(steamBeginCalls)+"/"+std::to_string(steamEndCalls));
        if(pair.generation==1||(extendedLogging()&&pair.generation%120==0))log(std::string(pair.pose.stereoQuad?"CINEMATIC_STEREO_QUAD_SUBMITTED serial=":pair.pose.quadView?"MENU_QUAD_SUBMITTED serial=":"STEREO_PROJECTION_SUBMITTED serial=")+std::to_string(pair.generation)+" perEyeSwapchains=1 initializedLayouts=1");

}
void stageStereoEndpoint(Device d,sfs::StereoFrame pair,
    std::array<XrCompositionLayerProjectionView,2> views,
    std::array<bool,2> acquired,std::array<bool,2> waited,EndpointTiming timing){
    // Everything borrowed from the game (including depth for hands) has been
    // consumed and fence-completed. Only owned XR images and value snapshots
    // survive this hand-off. No mirror/engine callback can escape the present.
    framegenEndpoint.stage([d,pair,views,acquired,waited,timing]() mutable {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        try{
            const double beginMs=beginPreparedStereo(d);
            StereoScheduleClock::time_point releaseAt{},endAt{},endDone{};
            publishStereoCopy(d,pair,views,acquired,waited,true,releaseAt,endAt,endDone);
            if(!failed)noteFramegenCadence(true,timing.generationMs,timing.waitMs,beginMs);
            static uint64_t count{};
            if(extendedLogging()&&(++count==1||count%120==0))
                log("FRAMEGEN_2X present order=generated,real asyncEndpoint=1 copiedBeforeHandoff=1 serial="+
                    std::to_string(pair.pose.serial)+" intervalNs="+std::to_string(pair.pose.displayTime-timing.midpointTime)+
                    " endpointBeginMs="+std::to_string(beginMs));
        }catch(const std::exception& e){
            log(std::string("FRAMEGEN_2X async endpoint failure: ")+e.what());
            for(unsigned eye=0;eye<2;++eye)if(acquired[eye]&&waited[eye]){
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                std::lock_guard<std::recursive_mutex> q(*d.queueMutex);
                XR(xrReleaseSwapchainImage)(stereoEyes[eye].handle,&release);
            }
            fg::reset();try{stopFramegenClock();}catch(...){}failed=true;
        }
    });
}

bool presentStereoImpl(Device& d,const sfs::StereoFrame& pair,uint32_t waitCount,const VkSemaphore* waits,const StereoMirror& mirror,const EndpointTiming* deferredEndpoint=nullptr){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    // KHARVOX keeps borrowed game depth alive through submission and its fence.
    kharvox::GameImageLifetime::Use gameImages(kharvox::gameImageLifetime());
    bool consumed=false,submitted=false,complete=false;
    std::array<uint32_t,2> targets{};
    std::array<bool,2> acquired{},waited{};
    try{
        if(stereoPending.waited&&!deferredEndpoint)framegenBeginCallMs=beginPreparedStereo(d);
        if(failed||(!stereoPending.begun&&!(deferredEndpoint&&stereoPending.waited)))return false;
        if(boundDevice!=d.device||swapchainLayers!=2||!stereoEyes[0].handle||!stereoEyes[1].handle||
           !validStereoPair(pair,stereoPending.pose,sourceExtent,format)){
            log("STEREO_PAIR_REJECTED stale/mixed frame, eye, extent, format or pose");cancelStereoImpl();return false;
        }
        const bool earlyRelease=!deferredEndpoint&&!mirror&&!externalFramegen&&kharvox::sfsEarlyXrRelease(earlyStereoReleaseRequested(pair.generation),runtimeKind,
            sfs::sourceRingActive(d.device),boundQueue==d.graphicsQueue&&boundDevice==d.device,
            fence!=VK_NULL_HANDLE,bool(d.queueMutex));
        const bool measure=extendedLogging();
        using ScheduleClock=std::chrono::steady_clock;
        ScheduleClock::time_point submitDone{},releaseAt{},endAt{},endDone{},completedAt{};
        double fenceWaitMs{};
        prepareHands(d,pair.pose.quadView);
        auto views=stereoProjectionViews(pair,XR_NULL_HANDLE,extent);
        for(unsigned e=0;e<2;++e){
            views[e].subImage.swapchain=stereoEyes[e].handle;
            views[e].subImage.imageArrayIndex=0;
        }
        if(!pair.pose.quadView&&!pair.pose.generated){
            XrPosef rendered{};const bool matched=camera::renderedHead(pair.pose.serial,rendered);
            if(matched)rebaseProjectionViews(views,pair.pose.predictedHead,rendered);
            if(extendedLogging()&&pair.pose.serial%120==0)log("STEREO_RENDER_POSE serial="+std::to_string(pair.pose.serial)+" matched="+std::to_string(matched));
        }
        for(unsigned e=0;e<2;++e){
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            {std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);check(XR(xrAcquireSwapchainImage)(stereoEyes[e].handle,&acquireInfo,&targets[e]),"xrAcquireSwapchainImage stereo eye");}acquired[e]=true;
            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};waitInfo.timeout=XR_INFINITE_DURATION;
            {static FrameTiming::Totals t;FrameTiming timing("xrWaitSwapchainImage",t);check(XR(xrWaitSwapchainImage)(stereoEyes[e].handle,&waitInfo),"xrWaitSwapchainImage stereo eye");}waited[e]=true;
        }
        checkVk(VK(vkResetCommandBuffer)(command,0),"reset stereo copy");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checkVk(VK(vkBeginCommandBuffer)(command,&begin),"begin stereo copy");
        if(extendedLogging())sfs::markXrCheckpoint(d.device,command,"XR copy begin");
        copyTiming.begin(command,pair.pose.serial%copyTimingStride==0);
        diagnosticCopyTiming.begin(command,argent::perf::enabled()&&argent::perf::mode.load(std::memory_order_relaxed)==2&&argent::perf::sample(pair.pose.serial,2));
        std::array<VkImageMemoryBarrier,4> barriers{};
        for(auto& b:barriers){b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
        for(unsigned e=0;e<2;++e){auto& b=barriers[e];b.image=pair.eyes[e].image;b.subresourceRange.baseArrayLayer=pair.eyes[e].layer;
            b.oldLayout=pair.eyes[e].layout;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;}
        for(unsigned e=0;e<2;++e){auto& dst=barriers[2+e];dst.image=stereoEyes[e].images.at(targets[e]).image;
            const bool initialized=targets[e]<stereoEyes[e].initialized.size()&&stereoEyes[e].initialized[targets[e]];
            dst.oldLayout=initialized?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;dst.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dst.srcAccessMask=initialized?(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT):0;dst.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;}
        VK(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,4,barriers.data());
        diagnosticCopyTiming.point(command,1);
        for(unsigned e=0;e<2;++e){
            const auto dstImage=stereoEyes[e].images.at(targets[e]).image;
            if(fsr1.active()&&!pair.pose.quadView){
                auto filtered=fsr1.record(command,pair.eyes[e].image,e,pair.generation,{0,0,sourceExtent.width,sourceExtent.height},extent,pair.eyes[e].layer);
                if(!filtered)throw std::runtime_error("FSR1 rejected eye image");
                VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
                blit.srcOffsets[1]=blit.dstOffsets[1]={int32_t(extent.width),int32_t(extent.height),1};
                VK(vkCmdBlitImage)(command,filtered,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dstImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
            }else{
                VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,pair.eyes[e].layer,1};
                copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={extent.width,extent.height,1};
                VK(vkCmdCopyImage)(command,pair.eyes[e].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dstImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            }
        }
        for(unsigned i=0;i<4;++i)finishStereoCopyBarrier(barriers[i],i>=2);
        VK(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,4,barriers.data());
        copyTiming.end(command);
        diagnosticCopyTiming.point(command,2);
        if(extendedLogging())sfs::markXrCheckpoint(d.device,command,"XR copy end");
        recordHands(targets,pair,views,gameImages);
        diagnosticCopyTiming.point(command,3);
        checkVk(VK(vkEndCommandBuffer)(command),"end stereo copy");checkVk(VK(vkResetFences)(d.device,1,&fence),"reset stereo copy fence");
        std::vector<VkPipelineStageFlags> stages(waitCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=waitCount;submit.pWaitSemaphores=waits;submit.pWaitDstStageMask=stages.data();submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        {std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);checkVk(VK(vkQueueSubmit)(boundQueue,1,&submit,fence),"submit stereo copy");}
        consumed=submitted=true;
        if(measure)submitDone=ScheduleClock::now();
        auto finishCopy=[&]{
        const auto waitStart=measure?ScheduleClock::now():ScheduleClock::time_point{};
        {static FrameTiming::Totals before,after;FrameTiming timing(earlyRelease?"deferredRenderAndCopyFence":"renderAndCopyFence",earlyRelease?after:before);const auto result=VK(vkWaitForFences)(d.device,1,&fence,VK_TRUE,UINT64_MAX);
         if(result==VK_ERROR_DEVICE_LOST)sfs::reportGpuCheckpoints(d.device,boundQueue);
         checkVk(result,"wait stereo copy");}complete=true;
        if(measure){completedAt=ScheduleClock::now();fenceWaitMs=std::chrono::duration<double,std::milli>(completedAt-waitStart).count();}
        handRenderer.finishSceneIntegratedFrame();
        // Mirror the final eye while it is still acquired, including hands and laser.
        if(mirror)mirror(stereoEyes[1].images.at(targets[1]).image,extent,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if(diagnosticCopyTiming.recorded){
            std::array<uint64_t,4> ticks{};
            if(diagnosticCopyTiming.read(ticks))log("PERF_GPU_XR serial="+std::to_string(pair.pose.serial)+" context="+(pair.pose.quadView?"quad":"world")+
                " queue="+std::to_string(reinterpret_cast<uint64_t>(boundQueue))+" startTick="+std::to_string(ticks[0])+" endTick="+std::to_string(ticks[3])+
                " barriersMs="+std::to_string(diagnosticCopyTiming.ms(ticks[0],ticks[1]))+" copyFsrMs="+std::to_string(diagnosticCopyTiming.ms(ticks[1],ticks[2]))+
                " handsMs="+std::to_string(diagnosticCopyTiming.ms(ticks[2],ticks[3]))+" totalMs="+std::to_string(diagnosticCopyTiming.ms(ticks[0],ticks[3]))+
                " fsr="+std::to_string(fsr1.active()&&!pair.pose.quadView)+" fenceWaitMs="+std::to_string(fenceWaitMs));
            else log("PERF_GPU_XR_UNAVAILABLE serial="+std::to_string(pair.pose.serial));
        }
        for(unsigned e=0;e<2;++e)if(targets[e]<stereoEyes[e].initialized.size())stereoEyes[e].initialized[targets[e]]=true;
        // Read only after the existing fence. Profiling adds no wait or queue
        // submission; this interval includes copy barriers and optional FSR.
        double gpuMs{};if(copyTiming.completed(gpuMs)){
            struct Totals{uint32_t samples{};double sum{},maximum{};};static Totals totals[2];
            auto& t=totals[pair.pose.quadView?0:1];t.sum+=gpuMs;t.maximum=(std::max)(t.maximum,gpuMs);
            if(++t.samples>=(copyTimingStride==1?120u:16u)){
                log("FRAME_GPU section=xrStereoCopy samples="+std::to_string(t.samples)+" meanMs="+std::to_string(t.sum/t.samples)+" maxMs="+std::to_string(t.maximum)+" context="+(pair.pose.quadView?"quad":"world"));t={};
            }
        }
        };
        auto publish=[&]{
            if(deferredEndpoint)stageStereoEndpoint(d,pair,views,acquired,waited,*deferredEndpoint);
            else publishStereoCopy(d,pair,views,acquired,waited,measure,releaseAt,endAt,endDone);
        };
        kharvox::finishSfsXrSubmission(earlyRelease,finishCopy,publish);
        if(measure&&!deferredEndpoint&&(pair.generation<=8||pair.generation%120==0)){
            const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
            log("XR_SUBMIT_ORDER serial="+std::to_string(pair.generation)+" context="+(pair.pose.quadView?"quad":"world")+
                " early="+std::to_string(earlyRelease)+" submitToReleaseMs="+std::to_string(ms(submitDone,releaseAt))+
                " submitToEndMs="+std::to_string(ms(submitDone,endAt))+" endFrameMs="+std::to_string(ms(endAt,endDone))+
                " submitToCompletionMs="+std::to_string(ms(submitDone,completedAt))+" fenceWaitMs="+std::to_string(fenceWaitMs)+" completionVerified=1");
        }
    }catch(const std::exception& error){
        log(std::string("STEREO_PRESENT_FAILED ")+error.what());
        if(!submitted)fsr1.discardRecordedFrame();
        if(submitted&&!complete){std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);complete=VK(vkQueueWaitIdle)(boundQueue)==VK_SUCCESS;}
        if(!submitted||complete)handRenderer.finishSceneIntegratedFrame();

        if(!submitted||complete)for(unsigned e=0;e<2;++e)if(acquired[e]&&waited[e]){
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);XR(xrReleaseSwapchainImage)(stereoEyes[e].handle,&release);
        }
        try{cancelStereoImpl();}catch(...){}failed=true;
    }
    return consumed;
}
}
bool beginStereoFrame(Device& d,const Source& source,sfs::FramePose& pose,XrPosef& head,bool quadView,
                      const sfs::Matrix* cinematicProjection,sfs::EyeUniforms* cinematicUniforms){
    waitStereoOutput();
    auto begin=[&]{return beginStereoImpl(d,source,pose,head,quadView,cinematicProjection,cinematicUniforms);};return simulator?xrWorker().invoke(begin):begin();
}
namespace {
void noteFramegenCadence(bool generated,double generationMs,double endpointWaitMs,double endpointBeginMs){
    if(!fg::requested()||!extendedLogging())return;
    using Clock=std::chrono::steady_clock;
    static Clock::time_point start{};
    static uint64_t real{},extra{};
    static double beginWait{},endWait{},generation{},beginCall{},endBeginCall{};
    const auto now=Clock::now();
    if(start==Clock::time_point{}){start=now;return;}
    ++real;extra+=generated;beginWait+=framegenBeginWaitMs;endWait+=endpointWaitMs;generation+=generationMs;
    beginCall+=framegenBeginCallMs;endBeginCall+=endpointBeginMs;
    const auto seconds=std::chrono::duration<double>(now-start).count();
    if(seconds<2)return;
    log("FRAMEGEN_CADENCE targetDisplayHz="+std::to_string(framegenClock.displayHz())+
        " targetRealHz="+std::to_string(framegenClock.nativeHz())+
        " realHz="+std::to_string(real/seconds)+" generatedHz="+std::to_string(extra/seconds)+
        " submittedHz="+std::to_string((real+extra)/seconds)+
        " beginWaitMeanMs="+std::to_string(beginWait/real)+" endpointWaitMeanMs="+std::to_string(endWait/real)+
        " generationWallMeanMs="+std::to_string(generation/real)+
        " beginCallMeanMs="+std::to_string(beginCall/real)+" endpointBeginCallMeanMs="+std::to_string(endBeginCall/real)+
        " waitAhead="+std::to_string(framegenClock.active));
    start=now;real=extra=0;beginWait=endWait=generation=beginCall=endBeginCall=0;
}
}
bool presentStereoFrame(Device& d,const sfs::StereoFrame& pair,uint32_t n,const VkSemaphore* waits,const StereoMirror& mirror){
    auto present=[&]{
        std::lock_guard<std::recursive_mutex> guard(mutex);
        if((!stereoPending.begun&&!stereoPending.waited)||!validStereoPair(pair,stereoPending.pose,sourceExtent,format))return presentStereoImpl(d,pair,n,waits,mirror);
        bool consumed=false;sfs::StereoFrame generated;
        const auto generationStart=std::chrono::steady_clock::now();
        const bool hasGenerated=fg::generate(d,pair,n,waits,consumed,generated);
        const double generationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-generationStart).count();
        if(!hasGenerated){
            framegenClock.active=false;
            auto real=pair;real.pose.displayTime=stereoDisplayTime;stereoPending.pose=real.pose;
            const bool shown=presentStereoImpl(d,real,consumed?0:n,consumed?nullptr:waits,mirror);
            if(shown&&!failed)noteFramegenCadence(false,generationMs,0,0);
            return shown||consumed;
        }
        // Copy the exact endpoint and its overlays before letting the producer
        // proceed. Only the runtime's publication wait may overlap game CPU work.
        const auto realPending=stereoPending.pose;
        framegenClock.active=!externalFramegen&&framegenClock.period>0;
        generated.pose.displayTime=stereoDisplayTime;
        stereoPending.pose=generated.pose;
        // Record the desktop once, from the exact real endpoint. A callback
        // selected at native present must not duplicate a midpoint mirror copy.
        presentStereoImpl(d,generated,0,nullptr,{});
        if(failed||stereoPending.begun){framegenClock.active=false;fg::reset();return consumed;}
        try{
            const bool asyncEndpoint=!simulator&&sfs::sourceRingActive(d.device);
            fg::WaitedFrame waited;beginStereoCycle(d,waited,!asyncEndpoint);
            const auto& next=waited.state;
            framegenClock.observe(next.predictedDisplayPeriod,headsetRefreshHz);
            stereoPending.pose=realPending;stereoPending.pose.displayTime=next.predictedDisplayTime;
            if(!next.shouldRender){stopFramegenClock();fg::reset();return consumed;}
            framegenClock.active=!externalFramegen&&framegenClock.period>0;
            // The endpoint copy completes here. Begin/end can then run on the
            // output worker, overlapping native CPU work until the next acquire.
            auto endpoint=pair;endpoint.pose.displayTime=next.predictedDisplayTime;
            EndpointTiming timing{generationMs,waited.callerWaitMs,generated.pose.displayTime};
            presentStereoImpl(d,endpoint,0,nullptr,mirror,asyncEndpoint?&timing:nullptr);
            if(!asyncEndpoint&&!failed)noteFramegenCadence(true,generationMs,waited.callerWaitMs,waited.beginCallMs);
        }catch(const std::exception& e){log(std::string("FRAMEGEN_2X endpoint failure: ")+e.what());fg::reset();try{stopFramegenClock();}catch(...){}failed=true;}
        return consumed;
    };return simulator?xrWorker().invoke(present):present();
}
void dispatchStereoOutput(){framegenEndpoint.dispatch();}
void waitStereoOutput(){
    if(!framegenEndpoint.pending())return;
    static FrameTiming::Totals t;FrameTiming timing("framegenEndpointBackpressure",t);
    framegenEndpoint.drain();
}
void cancelStereoFrame(){waitStereoOutput();auto cancel=[] {std::lock_guard<std::recursive_mutex> guard(mutex);stopFramegenClock();};if(simulator)xrWorker().invoke(cancel);else cancel();}
