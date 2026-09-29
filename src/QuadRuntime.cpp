#include "PerformanceDiagnostics.h"
#include "openxr/DiagnosticGpuTiming.h"
#include "hud/EternalWeaponWheel.h"
#include "hud/TutorialBindingHook.h"
#include "hands/HandSceneDepthTracker.h"
#include "fsr/Fsr1Upscaler.h"
#include "openxr/PhysicalGloryKill.h"
#include "openxr/HandsJumpPolicy.h"
#include "openxr/EternalMotionWheel.h"
#include "openxr/CrucibleGesture.h"
#include "QuadRuntime.h"
#include "EternalCameraHook.h"
#include "EternalPlayerHooks.h"
#include "EternalPresentation.h"
#include "XrWorker.h"
#include "QueueBridge.h"
#include "openxr/CopyGpuTiming.h"
#include "openxr/StereoProjection.h"
#include "openxr/StereoCopyBarrier.h"
#include "openxr/GameImageLifetime.h"
#include "openxr/CinewindowPosePolicy.h"
#include "openxr/CinematicProjection.h"
#include "FrameTiming.h"
#include "openxr/OpenXRRuntimePolicy.h"
#include "openxr/RuntimeVulkanDispatch.h"
#include "openxr/RuntimeDeviceCreateChain.h"
#include "openxr/NativeXrReleasePolicy.h"
#include <array>
#include <atomic>
#include "hands/HandRenderer.h"
#include "hands/HandDispatch.h"
#include "hands/WeaponPoseCalibration.h"
#include "openxr/DisplayFormat.h"
#include "openxr/ControllerInput.h"
#include "openxr/GameplayMapping.h"
#include "openxr/WeaponConfig.h"
#include "openxr/HandSmoothing.h"
#include "openxr/ShoulderChainsaw.h"
#include "openxr/HapticBridge.h"
#include "openxr/GripThresholdPolicy.h"
#include "openxr/ControllerProfile.h"
#include "bhaptics/BhapticsIpcClient.h"
#include "psvr2/Psvr2IpcClient.h"
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <wincodec.h>
#include <wrl/client.h>

namespace argent {
std::filesystem::path runtimePath(){
    HMODULE module{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&runtimePath),&module);
    wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);return std::filesystem::path(path).parent_path();
}
void log(const std::string& message){
    static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);
    static const auto file=[] {wchar_t dest[32768]{};const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",dest,32768);
        return n&&n<32768?std::filesystem::path(dest):runtimePath()/L"logs/ArgentQuad.log";}();
    static std::ofstream out;
    if(!out.is_open()||!out){out.close();out.clear();std::error_code ignored;std::filesystem::create_directories(file.parent_path(),ignored);out.open(file,std::ios::app);}
    // Keep the handle open, but retain per-line flushing for crash diagnostics.
    out<<GetTickCount64()<<" pid="<<GetCurrentProcessId()<<" "<<message<<std::endl;
}
namespace {
std::recursive_mutex mutex;
HMODULE loader{};PFN_xrGetInstanceProcAddr get{};
XrInstance instance{};XrSystemId system{};XrSession session{};XrSpace space{},localSpace{};XrSwapchain swapchain{};
bool attempted{},running{},failed{},simulator{};VkDevice boundDevice{};Device device;
bool enable2{};
VkPhysicalDevice runtimeSelectedPhysical{};
kharvox::OpenXRRuntimeKind runtimeKind{};
std::atomic<PFN_vkGetInstanceProcAddr> runtimeNext{};
std::atomic<PFN_vkGetDeviceProcAddr> runtimeGdpa{};
std::atomic<VkDevice> runtimeDevice{};
std::atomic<PFN_vkCreateDevice> runtimeCreate{};
std::atomic<VkPhysicalDevice> runtimePhysical{};
std::atomic<const VkDeviceCreateInfo*> runtimeDownstream{};
std::atomic<bool> runtimeSessionRoute{};
PFN_vkGetInstanceProcAddr publicGipa(){
    auto module=GetModuleHandleW(L"vulkan-1.dll");
    return module?reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(module,"vkGetInstanceProcAddr")):nullptr;
}
VKAPI_ATTR VkResult VKAPI_CALL runtimeCreateAdapter(VkPhysicalDevice,const VkDeviceCreateInfo* ci,const VkAllocationCallbacks* a,VkDevice* out){
    const auto create=runtimeCreate.load();const auto physical=runtimePhysical.load();const auto downstream=runtimeDownstream.load();
    if(!create||!physical||!ci||!downstream)return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceCreateInfo merged=*ci;
    merged.pNext=downstream->pNext;
    log("[RUNTIME-ENABLE2] vkCreateDevice adapter reattached Vulkan loader chain");
    return create(physical,&merged,a,out);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL runtimeGipa(VkInstance i,const char* name){
    const auto resolved=kharvox::resolveRuntimeVulkanProc(i,name,runtimeSessionRoute.load(),simulator,
        runtimeNext.load(),publicGipa(),runtimeCreate.load()?reinterpret_cast<PFN_vkVoidFunction>(runtimeCreateAdapter):nullptr,
        runtimeDevice.load(),runtimeGdpa.load());
    static std::atomic<uint32_t> traceCount{};
    const auto trace=traceCount.fetch_add(1,std::memory_order_relaxed);
    if(trace<128){
        const char* route="downstream";
        switch(resolved.route){
        case kharvox::RuntimeDispatchRoute::CreateDevice: route="create-device"; break;
        case kharvox::RuntimeDispatchRoute::SessionLoader: route="session-loader-gipa"; break;
        case kharvox::RuntimeDispatchRoute::PhysicalLoader: route="physical-loader"; break;
        case kharvox::RuntimeDispatchRoute::RuntimeDevice: route="runtime-device-downstream"; break;
        default: break;
        }
        log(std::string("[RUNTIME-ENABLE2] GIPA thread=")+std::to_string(GetCurrentThreadId())+
            " name="+(name?name:"NULL")+" adapter="+route+
            " returned="+std::to_string(reinterpret_cast<uintptr_t>(resolved.function)));
    }
    return resolved.function;
}
VkQueue boundQueue{};uint32_t boundFamily{};VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};
VkExtent2D extent{};VkFormat format{};std::vector<XrSwapchainImageVulkanKHR> images;
struct EyeSwapchain {XrSwapchain handle{};std::vector<XrSwapchainImageVulkanKHR> images;std::vector<bool> initialized;};
std::array<EyeSwapchain,2> stereoEyes;
struct PauseBindingsImage {XrSwapchain handle{};uint32_t width{},height{};std::vector<XrSwapchainImageVulkanKHR> images;};
PauseBindingsImage pauseBindings;
bool pauseBindingsReady{};
uint64_t frames{};XrSessionState sessionState=XR_SESSION_STATE_UNKNOWN;
uint32_t swapchainLayers{};
VkFormat compositionFormat{};
uint64_t steamVrNativePresentDeferrals{};
bool steamFramePrepared{},steamFrameBegun{};
XrFrameState steamPreparedFrame{XR_TYPE_FRAME_STATE};
uint64_t steamPreparedFrames{},steamConsumedPreparedFrames{},steamRepeatedAcquires{},steamWaitCalls{},steamBeginCalls{},steamEndCalls{},steamOrderViolations{};
struct StereoPending {bool begun{};sfs::FramePose pose{};input::Snapshot hands{};} stereoPending;
uint64_t stereoSerial{};
XrTime referenceChangeTime{};
QueueBridge bridge;
kharvox::CopyGpuTiming copyTiming;
argent::perf::GpuTiming<4> diagnosticCopyTiming;
uint32_t copyTimingStride=16;

template<class T> T xr(const char* name){static std::unordered_map<std::string,PFN_xrVoidFunction> cache;auto found=cache.find(name);if(found!=cache.end())return reinterpret_cast<T>(found->second);PFN_xrVoidFunction fn{};auto r=get(instance,name,&fn);if(XR_FAILED(r)||!fn)throw std::runtime_error(std::string("XR proc missing: ")+name);cache[name]=fn;return reinterpret_cast<T>(fn);}
#define XR(name) xr<PFN_##name>(#name)
#define VK(name) device.proc<PFN_##name>(#name)
void check(XrResult r,const char* operation){if(XR_FAILED(r))throw std::runtime_error(std::string(operation)+" result="+std::to_string(r));}
void checkVk(VkResult r,const char* operation){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+" result="+std::to_string(r));}
#include "XrActions.inc"
#include "FsrRuntime.inc"
#include "HandsRuntime.inc"
#include "PauseBindings.inc"
void destroySwapchain(){releaseHands();fsr1.releaseAfterCompletion();if(swapchain){XR(xrDestroySwapchain)(swapchain);swapchain=XR_NULL_HANDLE;}images.clear();for(auto& eye:stereoEyes){if(eye.handle){XR(xrDestroySwapchain)(eye.handle);eye.handle=XR_NULL_HANDLE;}eye.images.clear();eye.initialized.clear();}}
void createSwapchain(const Source& source,uint32_t layers=1,bool immersive=false){
    const bool upscale=immersive&&fsrRequested();
    const auto targetFormat=xrDisplayFormat(source.format,source.displaySrgb);
    if((layers==2?stereoEyes[0].handle:swapchain)&&sourceExtent.width==source.extent.width&&sourceExtent.height==source.extent.height&&swapchainFsrRequested==upscale&&format==source.format&&compositionFormat==targetFormat&&swapchainLayers==layers)return;
    destroySwapchain();
    const auto output=upscale?fsrOutput(source,targetFormat):source.extent;
    sourceExtent=source.extent;swapchainFsrRequested=upscale;
    if(upscale&&!fsr1.active())log("FSR1 using native eye copy");
    uint32_t count=0;check(XR(xrEnumerateSwapchainFormats)(session,0,&count,nullptr),"xrEnumerateSwapchainFormats");
    std::vector<int64_t> formats(count);check(XR(xrEnumerateSwapchainFormats)(session,count,&count,formats.data()),"xrEnumerateSwapchainFormats");
    if(std::find(formats.begin(),formats.end(),targetFormat)==formats.end())throw std::runtime_error("XR swapchain does not support required display format "+std::to_string(targetFormat));
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    char mirrorEnabled[8]{};
    if(layers==2&&GetEnvironmentVariableA("ARGENT_DESKTOP_MIRROR",mirrorEnabled,sizeof(mirrorEnabled))==1&&mirrorEnabled[0]=='1')ci.usageFlags|=XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    ci.format=targetFormat;ci.sampleCount=1;ci.width=output.width;ci.height=output.height;ci.faceCount=1;ci.arraySize=layers==2?1:layers;ci.mipCount=1;
    if(layers==2){
        for(unsigned eye=0;eye<2;++eye){
            check(XR(xrCreateSwapchain)(session,&ci,&stereoEyes[eye].handle),"xrCreateSwapchain eye");
            check(XR(xrEnumerateSwapchainImages)(stereoEyes[eye].handle,0,&count,nullptr),"xrEnumerateSwapchainImages eye");
            stereoEyes[eye].images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            check(XR(xrEnumerateSwapchainImages)(stereoEyes[eye].handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(stereoEyes[eye].images.data())),"xrEnumerateSwapchainImages eye");
            stereoEyes[eye].initialized.assign(count,false);
        }
        extent=output;format=source.format;compositionFormat=targetFormat;swapchainLayers=layers;log("XR_EYE_SWAPCHAINS images="+std::to_string(stereoEyes[0].images.size())+"/"+std::to_string(stereoEyes[1].images.size())+" extent="+std::to_string(extent.width)+"x"+std::to_string(extent.height)+" sourceFormat="+std::to_string(format)+" compositionFormat="+std::to_string(compositionFormat));
        return;
    }
    check(XR(xrCreateSwapchain)(session,&ci,&swapchain),"xrCreateSwapchain");
    check(XR(xrEnumerateSwapchainImages)(swapchain,0,&count,nullptr),"xrEnumerateSwapchainImages");
    images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    check(XR(xrEnumerateSwapchainImages)(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"xrEnumerateSwapchainImages");
    extent=output;format=source.format;compositionFormat=targetFormat;swapchainLayers=layers;log("XR_SWAPCHAIN images="+std::to_string(count)+" extent="+std::to_string(extent.width)+"x"+std::to_string(extent.height)+" layers="+std::to_string(layers)+" sourceFormat="+std::to_string(format)+" compositionFormat="+std::to_string(compositionFormat));
}
void startSession(Device& d,VkQueue q,uint32_t family,uint32_t index){
    device=d;
    XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
    check(enable2?XR(xrGetVulkanGraphicsRequirements2KHR)(instance,system,&requirements):XR(xrGetVulkanGraphicsRequirementsKHR)(instance,system,&requirements),"XR Vulkan requirements");
    char deferredSteamDevice[8]{};
    char forceSteamEnable1ForBinding[8]{};
    const bool forcedSteamEnable1=kharvox::isSteamBackedOpenXRRuntime(runtimeKind)
        &&GetEnvironmentVariableA("ARGENT_STEAMVR_FORCE_ENABLE1",forceSteamEnable1ForBinding,sizeof(forceSteamEnable1ForBinding))==1
        &&forceSteamEnable1ForBinding[0]=='1';
    const bool steamVrDeferredDevice=kharvox::isSteamBackedOpenXRRuntime(runtimeKind)
        &&GetEnvironmentVariableA("ARGENT_STEAMVR_DEFER_XR_DEVICE",deferredSteamDevice,sizeof(deferredSteamDevice))==1
        &&deferredSteamDevice[0]=='1'
        &&!forcedSteamEnable1;
    VkPhysicalDevice selected=runtimeSelectedPhysical;
    XrVulkanGraphicsDeviceGetInfoKHR gpuInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};gpuInfo.systemId=system;gpuInfo.vulkanInstance=d.instance;
    if(!selected)check(enable2?XR(xrGetVulkanGraphicsDevice2KHR)(instance,&gpuInfo,&selected):XR(xrGetVulkanGraphicsDeviceKHR)(instance,system,d.instance,&selected),"XR Vulkan graphics device");
    // The loader wraps physical-device handles above this layer. Compare GPU UUIDs
    // using the dispatch level owning each handle; never feed a layered physical
    // handle to the loader/runtime or infer GPU identity from pointer equality.
    auto vulkan=GetModuleHandleW(L"vulkan-1.dll");
    auto publicGet=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkan,"vkGetInstanceProcAddr"));
    auto selectedGet=enable2&&simulator?d.gipa:publicGet;
    auto publicProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(selectedGet(d.instance,"vkGetPhysicalDeviceProperties2KHR"));
    auto nextProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(d.gipa(d.instance,"vkGetPhysicalDeviceProperties2KHR"));
    if(!publicProperties||!nextProperties)throw std::runtime_error("GPU UUID verification unavailable");
    VkPhysicalDeviceIDProperties gameID{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES},xrID{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 gameProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2},xrProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    gameProps.pNext=&gameID;xrProps.pNext=&xrID;nextProperties(d.physical,&gameProps);publicProperties(selected,&xrProps);
    if(memcmp(gameID.deviceUUID,xrID.deviceUUID,VK_UUID_SIZE))throw std::runtime_error("XR GPU UUID does not match game GPU");
    log(std::string("XR_GPU_UUID_MATCH ")+gameProps.properties.deviceName);
    const auto bindingPhysical=steamVrDeferredDevice?d.physical:selected;
    if(steamVrDeferredDevice)log("XR_SESSION binding uses game physical for deferred SteamVR device");
    else if(forcedSteamEnable1)log("XR_SESSION binding uses runtime physical for forced SteamVR enable1 path");
    XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};binding.instance=d.instance;binding.physicalDevice=bindingPhysical;binding.device=d.device;binding.queueFamilyIndex=family;binding.queueIndex=index;
    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};ci.next=&binding;ci.systemId=system;
    log("XR_SESSION creating Vulkan binding family="+std::to_string(family)+" index="+std::to_string(index));
    check(XR(xrCreateSession)(instance,&ci,&session),"xrCreateSession");boundDevice=d.device;boundQueue=q;boundFamily=family;
    {uint32_t n{};check(XR(xrEnumerateSwapchainFormats)(session,0,&n,nullptr),"pause image formats");
     std::vector<int64_t> formats(n);check(XR(xrEnumerateSwapchainFormats)(session,n,&n,formats.data()),"pause image formats");
     log(initializePauseBindings(formats)?"PAUSE_BINDINGS uploaded=1 pauseRootOnly=1":"PAUSE_BINDINGS unavailable; normal menu retained");}
    XrReferenceSpaceCreateInfo sci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};sci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;sci.poseInReferenceSpace.orientation.w=1;
    check(XR(xrCreateReferenceSpace)(session,&sci,&space),"xrCreateReferenceSpace");
    sci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    check(XR(xrCreateReferenceSpace)(session,&sci,&localSpace),"xrCreateReferenceSpace LOCAL");
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pci.queueFamilyIndex=family;
    checkVk(VK(vkCreateCommandPool)(d.device,&pci,nullptr,&pool),"vkCreateCommandPool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    checkVk(VK(vkAllocateCommandBuffers)(d.device,&ai,&command),"vkAllocateCommandBuffers");
    if(d.setLoaderData)checkVk(d.setLoaderData(d.device,command),"vkSetDeviceLoaderData");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};checkVk(VK(vkCreateFence)(d.device,&fi,nullptr,&fence),"vkCreateFence");
    char gpuTimingSetting[8]{};const auto gpuTimingLength=GetEnvironmentVariableA("ARGENT_GPU_TIMING",gpuTimingSetting,sizeof(gpuTimingSetting));
    const bool explicitGpuTiming=!cleanRelease&&environmentFlag("ARGENT_GPU_TIMING")&&!(gpuTimingLength==1&&gpuTimingSetting[0]=='0');
    copyTimingStride=explicitGpuTiming?1:16;
    if(explicitGpuTiming||argent::perf::enabled()){
        uint32_t count{};
        auto queueProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
        queueProperties(d.physical,&count,nullptr);
        std::vector<VkQueueFamilyProperties> families(count);queueProperties(d.physical,&count,families.data());
        if(family<count){
            if(explicitGpuTiming)copyTiming.initialize(d.device,d.gdpa,gameProps.properties.limits.timestampPeriod,families[family].timestampValidBits);
            if(argent::perf::enabled()){
                diagnosticCopyTiming.initialize(d.device,d.gdpa,gameProps.properties.limits.timestampPeriod,families[family].timestampValidBits);
                log("PERF_XR_GPU_SUPPORT active="+std::to_string(bool(diagnosticCopyTiming.pool))+" periodNs="+std::to_string(diagnosticCopyTiming.period)+" validBits="+std::to_string(diagnosticCopyTiming.bits));
            }
        }
    }
    log("XR_COPY_GPU_TIMING active="+std::to_string(bool(copyTiming.pool))+" stride="+std::to_string(copyTimingStride));
    controllerActions.create();
    log("XR_SESSION created: Vulkan composition ready");
}
void events(){
    for(;;){
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};auto r=XR(xrPollEvent)(instance,&event);
        if(r==XR_EVENT_UNAVAILABLE)break;check(r,"xrPollEvent");
        if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)throw std::runtime_error("XR instance loss pending");
        if(event.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED){
            const auto& changed=*reinterpret_cast<XrEventDataInteractionProfileChanged*>(&event);
            if(changed.session==session)controllerActions.syncCount=0;
        }
        if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){
            const auto& change=*reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&event);
            if(change.session==session&&change.referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL)referenceChangeTime=change.changeTime;
        }
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
            auto& state=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);if(state.session!=session)continue;
            sessionState=state.state;log("XR_STATE="+std::to_string(state.state));
            if(state.state!=XR_SESSION_STATE_FOCUSED)input::clear();
            if(state.state==XR_SESSION_STATE_READY&&!running){XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;check(XR(xrBeginSession)(session,&bi),"xrBeginSession");running=true;}
            if(state.state==XR_SESSION_STATE_STOPPING&&running){check(XR(xrEndSession)(session),"xrEndSession");running=false;}
            if(state.state==XR_SESSION_STATE_EXITING||state.state==XR_SESSION_STATE_LOSS_PENDING)throw std::runtime_error("XR session exiting/lost");
        }
    }
}
}
bool initializeXRImpl(){
    std::lock_guard<std::recursive_mutex> guard(mutex);if(attempted)return instance&&!failed;attempted=true;
    try{
        loader=LoadLibraryExW((runtimePath()/L"openxr_loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!loader)throw std::runtime_error("OpenXR loader unavailable");get=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));if(!get)throw std::runtime_error("xrGetInstanceProcAddr unavailable");
        uint32_t count=0;check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,0,&count,nullptr),"XR extensions");
        std::vector<XrExtensionProperties> props(count,{XR_TYPE_EXTENSION_PROPERTIES});check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,count,&count,props.data()),"XR extensions");
        bool vulkan=false,vulkan2=false;for(auto& p:props){if(!strcmp(p.extensionName,XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))vulkan=true;if(!strcmp(p.extensionName,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))vulkan2=true;}
        runtimeKind=kharvox::classifyOpenXRRuntime(kharvox::activeOpenXRRuntimeManifest());
        auto path=kharvox::selectOpenXRVulkanPath(runtimeKind,false,vulkan,vulkan2);
        char forceSteamEnable1[8]{};
        if(kharvox::isSteamBackedOpenXRRuntime(runtimeKind)
            &&GetEnvironmentVariableA("ARGENT_STEAMVR_FORCE_ENABLE1",forceSteamEnable1,sizeof(forceSteamEnable1))==1
            &&forceSteamEnable1[0]=='1'
            &&vulkan)
            path=kharvox::OpenXRVulkanPath::VulkanEnable1Direct;
        if(path==kharvox::OpenXRVulkanPath::None)throw std::runtime_error("No OpenXR Vulkan graphics extension available");
        enable2=path==kharvox::OpenXRVulkanPath::VulkanEnable2RuntimeManaged;
        const char* extension=enable2?XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME:XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
        log(std::string("XR_VULKAN_PATH ")+extension);
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ci.applicationInfo.applicationName,"DOOM Eternal");
        strcpy_s(ci.applicationInfo.engineName,"idTech7");
        ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=1;ci.enabledExtensionNames=&extension;
        log(std::string("XR_APPLICATION_IDENTITY app=")+ci.applicationInfo.applicationName+" engine="+ci.applicationInfo.engineName);
        check(XR(xrCreateInstance)(&ci,&instance),"xrCreateInstance");
        XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;check(XR(xrGetSystem)(instance,&si,&system),"xrGetSystem");
        XrGraphicsRequirementsVulkanKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};check(enable2?XR(xrGetVulkanGraphicsRequirements2KHR)(instance,system,&req):XR(xrGetVulkanGraphicsRequirementsKHR)(instance,system,&req),"XR graphics requirements");
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};check(XR(xrGetInstanceProperties)(instance,&ip),"XR properties");simulator=!strcmp(ip.runtimeName,"OpenXR Simulator Runtime");log(std::string("XR_INITIALIZED runtime=")+ip.runtimeName);return true;
    }catch(const std::exception& e){log(std::string("XR_DISABLED ")+e.what());failed=true;return false;}
}
bool initializeXR(){
    const auto activeRuntime=kharvox::classifyOpenXRRuntime(kharvox::activeOpenXRRuntimeManifest());
    if(sfs::vrEnabled()&&kharvox::isSteamBackedOpenXRRuntime(activeRuntime)){
        return xrWorker().invoke([]{
            log("[STEAM-XR-THREAD] initializing OpenXR on worker thread="+std::to_string(GetCurrentThreadId()));
            return initializeXRImpl();
        });
    }
    return initializeXRImpl();
}
bool xrCreateGameInstance(PFN_vkGetInstanceProcAddr next,const VkInstanceCreateInfo* ci,const VkAllocationCallbacks* a,VkInstance* out,VkResult& result){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!instance||failed||!enable2)return false;
    result=VK_ERROR_INITIALIZATION_FAILED;
    try{
        XrVulkanInstanceCreateInfoKHR info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        info.systemId=system;info.pfnGetInstanceProcAddr=next;info.vulkanCreateInfo=ci;info.vulkanAllocator=a;
        const auto r=XR(xrCreateVulkanInstanceKHR)(instance,&info,out,&result);
        kharvox::finishRuntimeVulkanCreate(XR_SUCCEEDED(r),&result,out);
        log("XR_VULKAN_INSTANCE xr="+std::to_string(r)+" vk="+std::to_string(result));
    }catch(const std::exception& e){log(e.what());*out=VK_NULL_HANDLE;}
    return true;
}
bool xrCreateGameDevice(PFN_vkGetInstanceProcAddr next,PFN_vkGetDeviceProcAddr gdpa,VkInstance vi,VkPhysicalDevice physical,const VkDeviceCreateInfo* runtimeInfo,const VkDeviceCreateInfo* downstreamInfo,const VkAllocationCallbacks* a,VkDevice* out,VkResult& result){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!instance||failed||!enable2)return false;
    result=VK_ERROR_INITIALIZATION_FAILED;
    try{
        XrVulkanGraphicsDeviceGetInfoKHR query{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};query.systemId=system;query.vulkanInstance=vi;
        VkPhysicalDevice selected{};check(XR(xrGetVulkanGraphicsDevice2KHR)(instance,&query,&selected),"xrGetVulkanGraphicsDevice2KHR");
        auto selectedGet=simulator?next:publicGipa();
        auto properties=[&](PFN_vkGetInstanceProcAddr get,VkPhysicalDevice gpu,VkPhysicalDeviceIDProperties& id){
            auto fn=get?reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get(vi,"vkGetPhysicalDeviceProperties2")):nullptr;
            if(!fn&&get)fn=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get(vi,"vkGetPhysicalDeviceProperties2KHR"));
            if(!fn)throw std::runtime_error("XR GPU UUID query unavailable");
            VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&id;fn(gpu,&p);
        };
        VkPhysicalDeviceIDProperties gameId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES},xrId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        properties(next,physical,gameId);properties(selectedGet,selected,xrId);
        if(memcmp(gameId.deviceUUID,xrId.deviceUUID,VK_UUID_SIZE))throw std::runtime_error("XR GPU UUID does not match game GPU");
        runtimeNext=next;runtimePhysical=physical;runtimeDownstream=downstreamInfo;
        runtimeSessionRoute=false;runtimeCreate=kharvox::resolveLayerCreateDevice(next,vi);
        XrVulkanDeviceCreateInfoKHR info{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        info.systemId=system;info.pfnGetInstanceProcAddr=runtimeGipa;info.vulkanPhysicalDevice=selected;info.vulkanCreateInfo=runtimeInfo;info.vulkanAllocator=a;
        log("xrCreateVulkanDeviceKHR cross-thread layer adapter armed");
        XrResult r{};
        {
            RestoreDeviceCreateLinks restoreLinks(downstreamInfo->pNext);
            r=XR(xrCreateVulkanDeviceKHR)(instance,&info,out,&result);
        }
        kharvox::finishRuntimeVulkanCreate(XR_SUCCEEDED(r),&result,out);
        if(result==VK_SUCCESS){runtimeSelectedPhysical=selected;runtimeSessionRoute=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);}
        log("XR_VULKAN_DEVICE xr="+std::to_string(r)+" vk="+std::to_string(result));
    }catch(const std::exception& e){log(e.what());*out=VK_NULL_HANDLE;}
    runtimeCreate=nullptr;runtimeDownstream=nullptr;runtimePhysical=VK_NULL_HANDLE;
    return true;
}
void xrBindGameDevice(VkDevice gameDevice,PFN_vkGetDeviceProcAddr gdpa){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!enable2||failed||!gameDevice||!gdpa)return;
    runtimeGdpa=gdpa;
    runtimeDevice=gameDevice;
    log("[RUNTIME-ENABLE2] retained callback bound after ARGENT device initialization");
}
std::vector<std::string> xrExtensions(bool isDevice){
    std::lock_guard<std::recursive_mutex> guard(mutex);std::vector<std::string> result;if(!instance||failed||enable2)return result;
    try{
        auto query=isDevice?reinterpret_cast<PFN_xrGetVulkanInstanceExtensionsKHR>(XR(xrGetVulkanDeviceExtensionsKHR)):XR(xrGetVulkanInstanceExtensionsKHR);
        uint32_t size=0;check(query(instance,system,0,&size,nullptr),"XR Vulkan extensions");std::vector<char> text(size?size:1);
        check(query(instance,system,uint32_t(text.size()),&size,text.data()),"XR Vulkan extensions");
        std::istringstream stream(text.data());std::string name;while(stream>>name){result.push_back(name);log("XR_REQUIRED_EXTENSION "+name);}
    }catch(const std::exception& e){log(e.what());failed=true;}
    return result;
}
#include "StereoXr.inc"
void prepareSteamFrame(Device&,VkSwapchainKHR swapchain){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!kharvox::isSteamBackedOpenXRRuntime(runtimeKind)||!session||!running||!swapchain||failed)return;
    if(steamFramePrepared||steamFrameBegun){
        const auto repeated=++steamRepeatedAcquires;
        if(repeated<=8||repeated%120==0)log("[STEAM-XR-SPLIT] acquire arrived before prepared frame was presented; keeping one pending frame count="+std::to_string(repeated)+" thread="+std::to_string(GetCurrentThreadId()));
        return;
    }
    try{
        events();if(!running)return;
        const DWORD acquireCallerThread=GetCurrentThreadId();
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frame{XR_TYPE_FRAME_STATE};
        XrResult waitResult{XR_ERROR_RUNTIME_FAILURE};
        XrResult beginResult{XR_ERROR_RUNTIME_FAILURE};
        const DWORD lifecycleThread=xrWorker().invoke([&]{
            waitResult=XR(xrWaitFrame)(session,&wait,&frame);
            if(XR_SUCCEEDED(waitResult)){
                XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
                beginResult=XR(xrBeginFrame)(session,&begin);
            }
            return GetCurrentThreadId();
        });
        ++steamWaitCalls;
        if(XR_FAILED(waitResult)){log("[STEAM-XR-SPLIT] xrWaitFrame result="+std::to_string(waitResult)+" lifecycleThread="+std::to_string(lifecycleThread)+" acquireCallerThread="+std::to_string(acquireCallerThread));return;}
        ++steamBeginCalls;
        if(XR_FAILED(beginResult)){log("[STEAM-XR-SPLIT] xrBeginFrame result="+std::to_string(beginResult)+" lifecycleThread="+std::to_string(lifecycleThread)+" acquireCallerThread="+std::to_string(acquireCallerThread));return;}
        steamPreparedFrame=frame;
        steamFramePrepared=true;
        steamFrameBegun=true;
        const auto prepared=++steamPreparedFrames;
        if(prepared<=8||prepared%120==0)log("[STEAM-XR-SPLIT] prepared="+std::to_string(prepared)+" shouldRender="+std::to_string(frame.shouldRender)+" predicted="+std::to_string(frame.predictedDisplayTime)+" period="+std::to_string(frame.predictedDisplayPeriod)+" lifecycleThread="+std::to_string(lifecycleThread)+" acquireCallerThread="+std::to_string(acquireCallerThread)+" wait/begin/end="+std::to_string(steamWaitCalls)+"/"+std::to_string(steamBeginCalls)+"/"+std::to_string(steamEndCalls));
    }catch(const std::exception& e){log(std::string("[STEAM-XR-SPLIT] prepare failed: ")+e.what());}
}
bool presentQuadImpl(Device& d,VkQueue q,uint32_t family,uint32_t index,const Source& source,uint32_t imageIndex,const VkPresentInfoKHR& present){
    std::unique_lock<std::recursive_mutex> guard(mutex);if(!instance||failed||imageIndex>=source.images.size())return false;
    bool consumed=false,begun=false,acquired=false,waited=false;XrTime time{};
    try{
        if(!session){
            if(kharvox::isSteamBackedOpenXRRuntime(runtimeKind)){
                static const bool allowSteamVrSession=[]{
                    char forceSteamEnable1[8]{};
                    if(GetEnvironmentVariableA("ARGENT_STEAMVR_FORCE_ENABLE1",forceSteamEnable1,sizeof(forceSteamEnable1))==1&&forceSteamEnable1[0]=='1')return true;
                    wchar_t marker[32768]{};
                    const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",marker,32768);
                    if(!n||n>=32768)return false;
                    return GetFileAttributesW((std::filesystem::path(marker).parent_path()/L"enable_steamvr_xr_session").c_str())!=INVALID_FILE_ATTRIBUTES;
                }();
                if(!allowSteamVrSession){
                    static bool logged{};
                    if(!logged){logged=true;log("[STEAM-XR] xrCreateSession gated off; create logs/enable_steamvr_xr_session to test session startup");}
                    return false;
                }
                constexpr uint64_t requiredSteamVrNativePresents=300;
                if(steamVrNativePresentDeferrals<requiredSteamVrNativePresents){
                    ++steamVrNativePresentDeferrals;
                    if(steamVrNativePresentDeferrals==1||steamVrNativePresentDeferrals%60==0)
                        log("[STEAM-XR] deferring xrCreateSession until native presents stabilize count="+std::to_string(steamVrNativePresentDeferrals)+"/"+std::to_string(requiredSteamVrNativePresents));
                    return false;
                }
                log("[STEAM-XR] Releasing ARGENT runtime lock before xrCreateSession");
                guard.unlock();
                startSession(d,d.graphicsQueue,d.graphicsFamily,d.graphicsIndex);
                guard.lock();
            }else startSession(d,d.graphicsQueue,d.graphicsFamily,d.graphicsIndex);
        }
        if(boundDevice!=d.device){
            static uint64_t skipped{};
            if(++skipped==1||skipped%600==0)log("XR_SKIP binding_mismatch family="+std::to_string(family)+" index="+std::to_string(index)+" boundFamily="+std::to_string(boundFamily));
            return false;
        }
        events();if(!running)return false;
        createSwapchain(source);
        const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
        XrFrameState frame{XR_TYPE_FRAME_STATE};
        if(steamRuntime&&steamFramePrepared){
            frame=steamPreparedFrame;
            steamFramePrepared=false;
            begun=steamFrameBegun;
            if(!steamFrameBegun){++steamOrderViolations;log("[STEAM-XR-SPLIT] prepared frame lost begun state violation="+std::to_string(steamOrderViolations));}
            const auto consumedPrepared=++steamConsumedPreparedFrames;
            if(consumedPrepared<=8||consumedPrepared%120==0)log("[STEAM-XR-SPLIT] consumed="+std::to_string(consumedPrepared)+" predicted="+std::to_string(frame.predictedDisplayTime)+" period="+std::to_string(frame.predictedDisplayPeriod)+" thread="+std::to_string(GetCurrentThreadId())+" wait/begin/end="+std::to_string(steamWaitCalls)+"/"+std::to_string(steamBeginCalls)+"/"+std::to_string(steamEndCalls));
        }else{
            if(steamRuntime&&steamFrameBegun){++steamOrderViolations;log("[STEAM-XR-ORDER] local frame still open before xrWaitFrame violation="+std::to_string(steamOrderViolations));}
            XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
            XrResult waitResult{XR_SUCCESS};
            XrResult beginResult{XR_SUCCESS};
            DWORD lifecycleThread=GetCurrentThreadId();
            if(steamRuntime){
                lifecycleThread=xrWorker().invoke([&]{
                    waitResult=XR(xrWaitFrame)(session,&wi,&frame);
                    if(XR_SUCCEEDED(waitResult)){
                        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
                        beginResult=XR(xrBeginFrame)(session,&bi);
                    }
                    return GetCurrentThreadId();
                });
            }else check(XR(xrWaitFrame)(session,&wi,&frame),"xrWaitFrame");
            if(steamRuntime)++steamWaitCalls;
            if(XR_FAILED(waitResult)){log("[STEAM-XR-ORDER] xrWaitFrame result="+std::to_string(waitResult)+" lifecycleThread="+std::to_string(lifecycleThread)+" callerThread="+std::to_string(GetCurrentThreadId()));check(waitResult,"xrWaitFrame");}
            if(!steamRuntime){XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};check(XR(xrBeginFrame)(session,&bi),"xrBeginFrame");}
            if(steamRuntime){++steamBeginCalls;steamFrameBegun=true;}
            if(XR_FAILED(beginResult)){log("[STEAM-XR-ORDER] xrBeginFrame result="+std::to_string(beginResult)+" lifecycleThread="+std::to_string(lifecycleThread)+" callerThread="+std::to_string(GetCurrentThreadId()));check(beginResult,"xrBeginFrame");}
            begun=true;
        }
        time=frame.predictedDisplayTime;
        static int previousRender=-1;
        if(previousRender!=int(frame.shouldRender)){previousRender=int(frame.shouldRender);log("XR_SHOULD_RENDER="+std::to_string(previousRender)+" state="+std::to_string(sessionState));}
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};const XrCompositionLayerBaseHeader* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
        if(frame.shouldRender){
            const bool crossQueue=q!=boundQueue;
            if(crossQueue)bridge.prepare(d,family,boundFamily,source.extent,source.format);
            uint32_t target=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(XR(xrAcquireSwapchainImage)(swapchain,&ai,&target),"xrAcquireSwapchainImage");acquired=true;
            XrSwapchainImageWaitInfo swi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};swi.timeout=XR_INFINITE_DURATION;check(XR(xrWaitSwapchainImage)(swapchain,&swi),"xrWaitSwapchainImage");waited=true;
            if(crossQueue)bridge.stage(d,q,source.images[imageIndex],source.extent,present,consumed);
            checkVk(VK(vkResetCommandBuffer)(command,0),"vkResetCommandBuffer");
            VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};cbi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;checkVk(VK(vkBeginCommandBuffer)(command,&cbi),"vkBeginCommandBuffer");
            copyTiming.begin(command,frames%copyTimingStride==0);
            VkImageMemoryBarrier barriers[2]{};
            for(auto& b:barriers){b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
            barriers[0].image=source.images[imageIndex];barriers[0].oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barriers[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            barriers[1].image=images.at(target).image;barriers[1].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            VK(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,crossQueue?1:2,barriers+(crossQueue?1:0));
            VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={extent.width,extent.height,1};
            if(crossQueue){VkBufferImageCopy region{};region.imageSubresource=copy.dstSubresource;region.imageExtent=copy.extent;VK(vkCmdCopyBufferToImage)(command,bridge.buffer,barriers[1].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);}
            else VK(vkCmdCopyImage)(command,barriers[0].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,barriers[1].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            for(auto& b:barriers)std::swap(b.oldLayout,b.newLayout);
            barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barriers[0].dstAccessMask=0;
            barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VK(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,crossQueue?1:2,barriers+(crossQueue?1:0));
            copyTiming.end(command);
            checkVk(VK(vkEndCommandBuffer)(command),"vkEndCommandBuffer");checkVk(VK(vkResetFences)(d.device,1,&fence),"vkResetFences");
            std::vector<VkPipelineStageFlags> stages(present.waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=present.waitSemaphoreCount;submit.pWaitSemaphores=present.pWaitSemaphores;submit.pWaitDstStageMask=stages.data();submit.commandBufferCount=1;submit.pCommandBuffers=&command;
            VkPipelineStageFlags bridgeStage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            if(crossQueue){submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&bridge.ready;submit.pWaitDstStageMask=&bridgeStage;}
            {std::lock_guard<std::recursive_mutex> lock(*d.queueMutex);checkVk(VK(vkQueueSubmit)(boundQueue,1,&submit,fence),"vkQueueSubmit copy");}consumed=true;
            // Diagnostic boot deliberately waits on CPU: simple, verifiable ownership before release/present.
            checkVk(VK(vkWaitForFences)(d.device,1,&fence,VK_TRUE,UINT64_MAX),"vkWaitForFences copy");
            double copyMs{};if(copyTiming.completed(copyMs)&&(frames==0||frames%300==0))log("XR_COPY_GPU_MS="+std::to_string(copyMs));
            if(crossQueue){static uint64_t bridged{};if(++bridged==1||bridged%300==0)log("XR_QUEUE_BRIDGE count="+std::to_string(bridged)+" sourceFamily="+std::to_string(family)+" xrFamily="+std::to_string(boundFamily));}
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};check(XR(xrReleaseSwapchainImage)(swapchain,&ri),"xrReleaseSwapchainImage");acquired=false;
            quad.space=space;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.subImage.swapchain=swapchain;quad.subImage.imageRect=widescreenQuadRect(extent);
            quad.pose.orientation.w=1;quad.pose.position.z=-2.5f;quad.size=widescreenQuadSize(3.2f);
        }
        XrCompositionLayerQuad bindingsLayer{};
        std::array<const XrCompositionLayerBaseHeader*,2> menuLayers{layer,nullptr};uint32_t menuLayerCount=frame.shouldRender?1:0;
        if(frame.shouldRender&&pauseBindingsReady&&presentation::pauseRootVisible()){
            bindingsLayer=pauseBindingsLayer(quad);menuLayers[menuLayerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&bindingsLayer);
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=time;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=menuLayerCount;end.layers=menuLayerCount?menuLayers.data():nullptr;
        auto r=steamRuntime?xrWorker().invoke([&]{return XR(xrEndFrame)(session,&end);}):XR(xrEndFrame)(session,&end);begun=false;if(steamRuntime){steamFrameBegun=false;++steamEndCalls;}check(r,"xrEndFrame");
        if(frame.shouldRender){++frames;if(frames==1||frames%300==0)log("QUAD_FRAME_SUBMITTED count="+std::to_string(frames)+" result="+std::to_string(r)+" state="+std::to_string(sessionState)+" gameImage="+std::to_string(imageIndex)+" predictedTime="+std::to_string(time));}
    }catch(const std::exception& e){
        log(std::string("QUAD_DISABLED ")+e.what());failed=true;
        if(acquired&&waited){XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR(xrReleaseSwapchainImage)(swapchain,&ri);}
        if(begun){XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=time;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;if(kharvox::isSteamBackedOpenXRRuntime(runtimeKind))xrWorker().invoke([&]{XR(xrEndFrame)(session,&end);});else XR(xrEndFrame)(session,&end);}
    }
    return consumed;
}
bool presentQuad(Device& d,VkQueue q,uint32_t family,uint32_t index,const Source& source,uint32_t imageIndex,const VkPresentInfoKHR& present){
    auto frame=[&]{return presentQuadImpl(d,q,family,index,source,imageIndex,present);};
    return simulator?xrWorker().invoke(frame):frame();
}
void shutdownXRImpl(VkDevice d){
    std::lock_guard<std::recursive_mutex> guard(mutex);if(boundDevice!=d)return;
    try{cancelStereoImpl();}catch(const std::exception& e){log(std::string("STEREO_SHUTDOWN_END_FAILED ")+e.what());}
    controllerActions.destroy();
    VK(vkDeviceWaitIdle)(d);copyTiming.shutdownAfterCompletion();diagnosticCopyTiming.shutdownAfterCompletion();bridge.destroy(device);destroySwapchain();releasePauseBindings();if(localSpace)XR(xrDestroySpace)(localSpace);if(space)XR(xrDestroySpace)(space);if(session)XR(xrDestroySession)(session);
    localSpace=XR_NULL_HANDLE;
    if(fence)VK(vkDestroyFence)(d,fence,nullptr);if(pool)VK(vkDestroyCommandPool)(d,pool,nullptr);
    space=XR_NULL_HANDLE;session=XR_NULL_HANDLE;fence=VK_NULL_HANDLE;pool=VK_NULL_HANDLE;boundDevice=VK_NULL_HANDLE;running=false;failed=true;
    log("XR_SHUTDOWN");
    runtimeDevice=VK_NULL_HANDLE;runtimeGdpa=nullptr;
}
void shutdownXR(VkDevice d){if(simulator)xrWorker().invoke([&]{shutdownXRImpl(d);});else shutdownXRImpl(d);
    if(runtimeDevice.load()==d){runtimeDevice=VK_NULL_HANDLE;runtimeGdpa=nullptr;}}
}

