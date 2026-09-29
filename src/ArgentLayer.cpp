#include "QuadRuntime.h"
#include "StartupCrashTrace.h"
#include "GpuAddressTrace.h"
#include "GpuDiagnosticsConfig.h"
#include "ShaderCapture.h"
#include "RenderTrace.h"
#include "CameraCapture.h"
#include "EternalCameraHook.h"
#include "EternalDlssHook.h"
#include "EternalPresentation.h"
#include "hud/EternalWeaponWheel.h"
#include "DesktopMirror.h"
#include "DesktopWindow.h"
#include <vulkan/vulkan_win32.h>
#include "FrameTiming.h"
#include "PresentAnalysis.h"
#include "CommandDispatchCache.h"
#include "StereoReadback.h"
#include "openxr/RuntimeVulkanDispatch.h"
#include "openxr/OpenXRRuntimePolicy.h"
#include "sfs/NativeSfs.h"
#include "sfs/EternalProfile.h"
#include "sfs/DeviceCapabilities.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <map>
#include <fstream>

namespace {
std::recursive_mutex stateMutex;
template<class H> void* key(H h){return h?*reinterpret_cast<void**>(h):nullptr;}
struct Instance { VkInstance handle{}; PFN_vkGetInstanceProcAddr gipa{}; PFN_GetPhysicalDeviceProcAddr physicalProc{}; PFN_vkCreateDevice createDevice{}; bool game{},runtimeAuxiliary{}; VkDebugUtilsMessengerEXT addressMessenger{}; };
struct State : argent::Device {
    inline static std::atomic<uint64_t> nextDispatchId{1};
    const uint64_t dispatchId=nextDispatchId.fetch_add(1,std::memory_order_relaxed);
    argent::CameraCapture camera;
    bool game{};
    bool runtimeAuxiliary{};
    bool sfs{};
    XrPosef calibratedHead{}; bool calibrated{};
    bool menuQuad{true};
    argent::presentation::Policy presentation;
    argent::presentation::Mode presentationMode{argent::presentation::Mode::Unknown};
    bool cinematicStereo{};
    argent::sfs::FramePose framePose{};
    std::vector<uint32_t> queueFamilies;
    template<class T> T proc(const char* name) const {
        // Resolve per device/lifetime, following KHARVOX's downstream dispatch
        // policy. Recording threads must not resolve and scan the SFS wrapper
        // table for every draw. Owning string keys also support transient names.
        struct Cache {uint64_t id{};std::map<std::string,PFN_vkVoidFunction,std::less<>> functions;};
        thread_local Cache cache;
        if(cache.id!=dispatchId){cache.functions.clear();cache.id=dispatchId;}
        auto found=cache.functions.find(name);if(found!=cache.functions.end())return reinterpret_cast<T>(found->second);
        auto next=gdpa(device,name);
        auto resolved=sfs?argent::sfs::wrapProc(device,name,next):next;cache.functions.emplace(name,resolved);return reinterpret_cast<T>(resolved);
    }
    std::unordered_map<VkSwapchainKHR,argent::Source> sources;
    std::unordered_map<VkSwapchainKHR,std::unique_ptr<argent::DesktopMirror>> mirrors;
    struct Queue {uint32_t family,index;bool graphics;};
    std::unordered_map<VkQueue,Queue> queues;
    std::vector<VkQueueFamilyProperties> families;
};
std::unordered_map<void*,Instance> instances;
std::unordered_map<void*,std::shared_ptr<State>> devices;
std::atomic<uint64_t> deviceGeneration{1};
std::atomic<uint64_t> presents{};
template<class S,class F> void observeCamera(const S& s,F&& f) noexcept {try{if(s->game&&argent::CameraCapture::enabled())f(s->camera);}catch(...){}}
std::atomic<uint32_t> instanceCreateDepth{};
struct InstanceCreateScope {
    bool nested{};
    InstanceCreateScope():nested(instanceCreateDepth.fetch_add(1,std::memory_order_acq_rel)!=0){}
    ~InstanceCreateScope(){instanceCreateDepth.fetch_sub(1,std::memory_order_acq_rel);}
};
Instance instanceOf(void* k){std::lock_guard<std::recursive_mutex> l(stateMutex);auto it=instances.find(k);return it==instances.end()?Instance{}:it->second;}
std::shared_ptr<State> deviceOf(void* k){
    struct Cache {void* key{};uint64_t generation{};std::weak_ptr<State> state;};thread_local Cache cache;
    auto generation=deviceGeneration.load(std::memory_order_acquire);
    if(cache.key==k&&cache.generation==generation)if(auto state=cache.state.lock())return state;
    std::lock_guard<std::recursive_mutex> l(stateMutex);auto it=devices.find(k);if(it==devices.end())return nullptr;cache={k,generation,it->second};return it->second;
}
bool gameProcess(){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);return _wcsicmp(std::filesystem::path(path).filename().c_str(),L"DOOMEternalx64vk.exe")==0;}
State* commandDeviceOf(void* k){
    thread_local argent::CommandDeviceCache<State> cache;
    return cache.get(k,deviceGeneration.load(std::memory_order_acquire),[&] {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        auto found=devices.find(k);return found==devices.end()?nullptr:found->second.get();
    });
}
VkExtent2D requestedEyeExtent(){
    static const VkExtent2D requested=[] {
        auto dimension=[](const char* name){char value[32]{};auto n=GetEnvironmentVariableA(name,value,sizeof(value));
            if(!n||n>=sizeof(value))return 0u;char* end{};auto v=std::strtoul(value,&end,10);return end!=value&&!*end&&v>=64&&v<=16384?uint32_t(v):0u;};
        return VkExtent2D{dimension("ARGENT_EYE_WIDTH"),dimension("ARGENT_EYE_HEIGHT")};
    }();
    return requested;
}
void exposeEyeExtent(const Instance& instance,VkPhysicalDevice physical,VkSurfaceCapabilitiesKHR& caps){
    if(!instance.game||!argent::sfs::vrEnabled()||!argent::sfs::sourceRingRequested())return;
    const auto requested=requestedEyeExtent();if(!requested.width||!requested.height)return;
    auto get=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(instance.gipa(instance.handle,"vkGetPhysicalDeviceProperties"));
    VkPhysicalDeviceProperties props{};if(!get)return;get(physical,&props);
    if(requested.width>props.limits.maxImageDimension2D||requested.height>props.limits.maxImageDimension2D)return;
    // Only the application's virtual SourceRing sees this extent. Actual WSI
    // and OpenXR runtime instances always query the downstream driver directly.
    caps.currentExtent=caps.minImageExtent=caps.maxImageExtent=requested;
}
template<class T> T* chain(const void* p,VkStructureType type,VkLayerFunction function){
    while(p){auto n=static_cast<const VkBaseInStructure*>(p);if(n->sType==type){auto c=(T*)p;if(c->function==function)return c;}p=n->pNext;}return nullptr;
}
std::vector<const char*> extensions(uint32_t count,const char*const* names,const std::vector<std::string>& extra){
    std::vector<const char*> all;for(uint32_t i=0;i<count;++i)all.push_back(names[i]);
    for(auto& e:extra)if(std::none_of(all.begin(),all.end(),[&](auto n){return e==n;}))all.push_back(e.c_str());return all;
}
}
extern "C" {
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice,const char*);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(VkInstance i,const VkWin32SurfaceCreateInfoKHR* ci,const VkAllocationCallbacks* a,VkSurfaceKHR* out){
    auto s=instanceOf(key(i));auto next=s.gipa?reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(s.gipa(i,"vkCreateWin32SurfaceKHR")):nullptr;
    if(!next)return VK_ERROR_EXTENSION_NOT_PRESENT;
    if(ci&&s.game&&argent::sfs::vrEnabled()&&argent::sfs::sourceRingRequested()){
        const auto desktop=argent::configuredDesktopExtent();
        const bool requested=argent::requestDesktopClientSize(ci->hwnd,desktop.width,desktop.height);
        argent::log("SFS_DESKTOP_WINDOW requested="+std::to_string(desktop.width)+"x"+std::to_string(desktop.height)+" accepted="+(requested?"1":"0"));
    }
    return next(i,ci,a,out);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice p,VkSurfaceKHR surface,VkSurfaceCapabilitiesKHR* caps){
    auto s=instanceOf(key(p));auto next=s.gipa?reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(s.gipa(s.handle,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR")):nullptr;
    if(!next)return VK_ERROR_EXTENSION_NOT_PRESENT;auto r=next(p,surface,caps);if(r==VK_SUCCESS&&caps)exposeEyeExtent(s,p,*caps);return r;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice p,const VkPhysicalDeviceSurfaceInfo2KHR* info,VkSurfaceCapabilities2KHR* caps){
    auto s=instanceOf(key(p));auto next=s.gipa?reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(s.gipa(s.handle,"vkGetPhysicalDeviceSurfaceCapabilities2KHR")):nullptr;
    if(!next)return VK_ERROR_EXTENSION_NOT_PRESENT;auto r=next(p,info,caps);if(r==VK_SUCCESS&&caps)exposeEyeExtent(s,p,caps->surfaceCapabilities);return r;
}
PFN_vkVoidFunction surfaceIntercept(const char* n,PFN_vkVoidFunction next){
    if(!next||!n)return next;
    if(!strcmp(n,"vkCreateWin32SurfaceKHR"))return reinterpret_cast<PFN_vkVoidFunction>(vkCreateWin32SurfaceKHR);
    if(!strcmp(n,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))return reinterpret_cast<PFN_vkVoidFunction>(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    if(!strcmp(n,"vkGetPhysicalDeviceSurfaceCapabilities2KHR"))return reinterpret_cast<PFN_vkVoidFunction>(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
    return next;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* ci,const VkAllocationCallbacks* a,VkInstance* out){
    InstanceCreateScope createScope;
    auto c=chain<VkLayerInstanceCreateInfo>(ci->pNext,VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,VK_LAYER_LINK_INFO);
    if(!c||!c->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto link=c->u.pLayerInfo;auto get=link->pfnNextGetInstanceProcAddr;auto pg=link->pfnNextGetPhysicalDeviceProcAddr;c->u.pLayerInfo=link->pNext;
    bool game=gameProcess()&&!createScope.nested;
    const char* app=ci->pApplicationInfo?ci->pApplicationInfo->pApplicationName:nullptr;
    if(app&&std::strstr(app,"steamvr"))game=false;
    const bool nested=createScope.nested;
    const auto runtimeKind=kharvox::classifyOpenXRRuntime(kharvox::activeOpenXRRuntimeManifest());
    const bool steamRuntimeAuxiliary=gameProcess()&&kharvox::shouldPassthroughSteamRuntimeAuxiliary(runtimeKind,nested,app?app:"");
    if(game){argent::installStartupCrashTrace();argent::log(std::string("vkCreateInstance app=")+(app?app:"unknown"));}
    if(steamRuntimeAuxiliary)argent::log(std::string("SteamVR auxiliary Vulkan instance passthrough app=")+(app?app:"unknown")+" nested="+std::to_string(nested));
    if(game&&argent::sfs::vrEnabled()&&argent::sfs::sourceRingRequested()){
        auto extent=requestedEyeExtent();
        if(extent.width&&extent.height&&!argent::camera::installRenderExtent(extent.width,extent.height)){argent::log("ETERNAL_RENDER_EXTENT refused: unsupported engine accessors");return VK_ERROR_INITIALIZATION_FAILED;}
    }
    auto required=game&&(!argent::sfs::nativeProbeEnabled()||argent::sfs::vrEnabled())&&argent::initializeXR()?argent::xrExtensions(false):std::vector<std::string>{};
    bool addressDebug=false;char gpuDiagnostics[8]{};
    if(game&&GetEnvironmentVariableA("ARGENT_GPU_DIAGNOSTICS",gpuDiagnostics,8)==1&&gpuDiagnostics[0]=='1'){
        auto enumerate=reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(get(nullptr,"vkEnumerateInstanceExtensionProperties"));
        uint32_t count{};if(enumerate&&enumerate(nullptr,&count,nullptr)==VK_SUCCESS){std::vector<VkExtensionProperties> props(count);
            if(enumerate(nullptr,&count,props.data())==VK_SUCCESS)for(const auto& prop:props)if(!std::strcmp(prop.extensionName,VK_EXT_DEBUG_UTILS_EXTENSION_NAME))addressDebug=true;}
        if(addressDebug)required.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    auto enabled=extensions(ci->enabledExtensionCount,ci->ppEnabledExtensionNames,required);
    auto modified=*ci;modified.enabledExtensionCount=uint32_t(enabled.size());modified.ppEnabledExtensionNames=enabled.data();
    auto create=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"));
    VkResult r=VK_ERROR_INITIALIZATION_FAILED;
    if(steamRuntimeAuxiliary)r=create?create(ci,a,out):VK_ERROR_INITIALIZATION_FAILED;
    else if(!game||!argent::xrCreateGameInstance(get,&modified,a,out,r))r=create(&modified,a,out);
    if(r==VK_SUCCESS){
        Instance state{*out,get,pg,reinterpret_cast<PFN_vkCreateDevice>(get(*out,"vkCreateDevice")),game,steamRuntimeAuxiliary};
        if(addressDebug){auto messenger=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(get(*out,"vkCreateDebugUtilsMessengerEXT"));
            VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;info.pfnUserCallback=argent::gpuAddressCallback;
            const auto mr=messenger?messenger(*out,&info,nullptr,&state.addressMessenger):VK_ERROR_EXTENSION_NOT_PRESENT;
            argent::log("GPU_ADDRESS_MESSENGER result="+std::to_string(mr));}
        std::lock_guard<std::recursive_mutex> l(stateMutex);instances[key(*out)]=state;
    }
    if(game)argent::log("vkCreateInstance result="+std::to_string(r));return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance i,const VkAllocationCallbacks* a){
    auto s=instanceOf(key(i));{std::lock_guard<std::recursive_mutex> l(stateMutex);instances.erase(key(i));}
    if(s.addressMessenger)reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(s.gipa(i,"vkDestroyDebugUtilsMessengerEXT"))(i,s.addressMessenger,nullptr);
    if(s.gipa)reinterpret_cast<PFN_vkDestroyInstance>(s.gipa(i,"vkDestroyInstance"))(i,a);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice p,const VkDeviceCreateInfo* ci,const VkAllocationCallbacks* a,VkDevice* out){
    auto c=chain<VkLayerDeviceCreateInfo>(ci->pNext,VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VK_LAYER_LINK_INFO);
    if(!c||!c->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto cb=chain<VkLayerDeviceCreateInfo>(ci->pNext,VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VK_LOADER_DATA_CALLBACK);
    auto setData=cb?cb->u.pfnSetDeviceLoaderData:nullptr;
    auto link=c->u.pLayerInfo;auto get=link->pfnNextGetInstanceProcAddr;auto gdpa=link->pfnNextGetDeviceProcAddr;c->u.pLayerInfo=link->pNext;
    auto i=instanceOf(key(p));if(!i.handle)return VK_ERROR_INITIALIZATION_FAILED;
    if(i.runtimeAuxiliary){
        auto create=kharvox::resolveLayerCreateDevice(get,i.handle);
        if(!create)create=i.createDevice;
        const auto r=create?create(p,ci,a,out):VK_ERROR_INITIALIZATION_FAILED;
        if(r==VK_SUCCESS){
            auto s=std::make_shared<State>();s->device=*out;s->physical=p;s->instance=i.handle;s->gipa=i.gipa;s->gdpa=gdpa;s->setLoaderData=setData;s->runtimeAuxiliary=true;
            std::lock_guard<std::recursive_mutex> l(stateMutex);devices[key(*out)]=s;deviceGeneration.fetch_add(1,std::memory_order_release);
            if(gameProcess())argent::log("SteamVR auxiliary Vulkan device passthrough");
        }
        return r;
    }
    const bool sfsRequested=i.game&&argent::sfs::nativeProbeEnabled();
    if(sfsRequested){
        auto queryFeatures=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2"));
        if(!queryFeatures)queryFeatures=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2KHR"));
        auto queryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(i.gipa(i.handle,"vkGetPhysicalDeviceProperties2"));
        if(!queryProperties)queryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(i.gipa(i.handle,"vkGetPhysicalDeviceProperties2KHR"));
        if(!queryFeatures||!queryProperties){argent::log("SFS_INIT_REFUSED Vulkan capability queries unavailable");return VK_ERROR_FEATURE_NOT_PRESENT;}
        VkPhysicalDeviceMultiviewFeatures views{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&views;queryFeatures(p,&features);
        VkPhysicalDeviceMultiviewProperties viewLimits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES};
        VkPhysicalDeviceSubgroupProperties subgroups{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};viewLimits.pNext=&subgroups;
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&viewLimits;queryProperties(p,&properties);
        const auto& gpu=properties.properties;
        argent::log("SFS_GPU device="+std::string(gpu.deviceName)+" vendor="+std::to_string(gpu.vendorID)+" deviceId="+std::to_string(gpu.deviceID)+
            " driver="+std::to_string(gpu.driverVersion)+" api="+std::to_string(gpu.apiVersion)+" multiview="+std::to_string(views.multiview)+
            " maxViews="+std::to_string(viewLimits.maxMultiviewViewCount)+" maxLayers="+std::to_string(gpu.limits.maxImageArrayLayers)+
            " subgroupSize="+std::to_string(subgroups.subgroupSize)+" subgroupStages="+std::to_string(subgroups.supportedStages)+" subgroupOps="+std::to_string(subgroups.supportedOperations));
        if(const auto failure=argent::sfs::stereoCapabilityFailure(gpu,views.multiview,viewLimits.maxMultiviewViewCount,requestedEyeExtent(),ci->pNext)){
            argent::log(std::string("SFS_INIT_REFUSED ")+failure);return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        if(gpu.vendorID==0x1002)argent::log("SFS_GPU AMD capability checks passed; ARGENT headset rendering still requires hardware validation");
    }
    auto required=i.game&&(!sfsRequested||argent::sfs::vrEnabled())?argent::xrExtensions(true):std::vector<std::string>{};
    bool gpuCheckpoints=false,gpuFaultExtension=false,gpuFault=false,addFaultFeatures=false;char diagnostics[8]{};
    VkPhysicalDeviceFaultFeaturesEXT faultFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    bool nvDiagnostics=false,addNvFeatures=false,addNvConfig=false;
    VkPhysicalDeviceDiagnosticsConfigFeaturesNV nvFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV};
    VkDeviceDiagnosticsConfigCreateInfoNV nvConfig{VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV};
    if(sfsRequested)argent::log("GPU_DIAGNOSTICS_REQUESTED enabled="+std::to_string(argent::gpuCrashDiagnostics())+" extendedLogging="+std::to_string(argent::extendedLogging())+" nonuniformBulk=0");
    if(sfsRequested&&GetEnvironmentVariableA("ARGENT_GPU_DIAGNOSTICS",diagnostics,8)==1&&diagnostics[0]=='1'){
        auto enumerate=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(i.gipa(i.handle,"vkEnumerateDeviceExtensionProperties"));
        uint32_t count{};if(enumerate&&enumerate(p,nullptr,&count,nullptr)==VK_SUCCESS){std::vector<VkExtensionProperties> available(count);
            if(enumerate(p,nullptr,&count,available.data())==VK_SUCCESS)for(const auto& ext:available){
                if(!std::strcmp(ext.extensionName,VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME))gpuCheckpoints=true;
                if(!std::strcmp(ext.extensionName,VK_EXT_DEVICE_FAULT_EXTENSION_NAME))gpuFaultExtension=true;
                if(!std::strcmp(ext.extensionName,VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME))nvDiagnostics=true;
            }}
        if(gpuCheckpoints)required.emplace_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
        argent::log("GPU_CHECKPOINT_EXTENSION supported="+std::to_string(gpuCheckpoints));
        if(gpuFaultExtension){
            auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2"));
            if(features){VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};supported.pNext=&faultFeatures;features(p,&supported);}
            gpuFault=faultFeatures.deviceFault==VK_TRUE;addFaultFeatures=gpuFault;
            // Respect the application's feature choice; avoid duplicate nodes.
            for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext)
                if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT){
                    faultFeatures=*reinterpret_cast<const VkPhysicalDeviceFaultFeaturesEXT*>(node);
                    gpuFault=faultFeatures.deviceFault==VK_TRUE;addFaultFeatures=false;break;
                }
            if(gpuFault)required.emplace_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
        }
        argent::log("GPU_FAULT_EXTENSION supported="+std::to_string(gpuFaultExtension)+" enabled="+std::to_string(gpuFault)+" vendorBinary="+std::to_string(gpuFault&&faultFeatures.deviceFaultVendorBinary));
        if(nvDiagnostics){
            auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2"));
            VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};supported.pNext=&nvFeatures;
            if(features)features(p,&supported);
            nvDiagnostics=nvFeatures.diagnosticsConfig==VK_TRUE;addNvFeatures=nvDiagnostics;addNvConfig=nvDiagnostics;
            nvConfig.flags=argent::gpuDiagnosticFlags;
            for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext){
                if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV){nvDiagnostics=nvDiagnostics&&reinterpret_cast<const VkPhysicalDeviceDiagnosticsConfigFeaturesNV*>(node)->diagnosticsConfig;addNvFeatures=false;}
                if(node->sType==VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV){nvConfig.flags=reinterpret_cast<const VkDeviceDiagnosticsConfigCreateInfoNV*>(node)->flags;addNvConfig=false;}
            }
            if(nvDiagnostics)required.emplace_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
            else{addNvFeatures=false;addNvConfig=false;}
        }
        argent::log("GPU_NV_DIAGNOSTICS enabled="+std::to_string(nvDiagnostics)+" flags="+std::to_string(nvDiagnostics?nvConfig.flags:0)+" resourceTracking="+std::to_string(nvDiagnostics&&(nvConfig.flags&VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV))+" shaderErrors="+std::to_string(nvDiagnostics&&(nvConfig.flags&VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV)));
    }
    bool waterRobustness=false,addWaterRobustness=false,addressReport=false,addAddressReport=false;
    VkPhysicalDevicePipelineRobustnessFeaturesEXT waterFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT};
    VkPhysicalDeviceAddressBindingReportFeaturesEXT addressFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT};
    if(sfsRequested){
        auto enumerate=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(i.gipa(i.handle,"vkEnumerateDeviceExtensionProperties"));
        uint32_t count{};std::vector<VkExtensionProperties> available;
        if(enumerate&&enumerate(p,nullptr,&count,nullptr)==VK_SUCCESS){available.resize(count);if(enumerate(p,nullptr,&count,available.data())!=VK_SUCCESS)available.clear();}
        auto has=[&](const char* name){return std::any_of(available.begin(),available.end(),[&](const auto& ext){return !std::strcmp(ext.extensionName,name);});};
        auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2"));
        if(features&&has(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME)&&has(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)){
            VkPhysicalDeviceRobustness2FeaturesEXT robustness2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
            VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};supported.pNext=&waterFeatures;waterFeatures.pNext=&robustness2;features(p,&supported);waterFeatures.pNext=nullptr;
            waterRobustness=waterFeatures.pipelineRobustness&&robustness2.robustBufferAccess2&&robustness2.robustImageAccess2;
            auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(i.gipa(i.handle,"vkGetPhysicalDeviceProperties2"));
            VkPhysicalDeviceDescriptorIndexingProperties indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&indexing;
            if(properties)properties(p,&props);waterRobustness=waterRobustness&&indexing.robustBufferAccessUpdateAfterBind;
            addWaterRobustness=waterRobustness;
            for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext){
                if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT){waterRobustness=waterRobustness&&reinterpret_cast<const VkPhysicalDevicePipelineRobustnessFeaturesEXT*>(node)->pipelineRobustness;addWaterRobustness=false;}
                if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES){waterRobustness=waterRobustness&&reinterpret_cast<const VkPhysicalDeviceVulkan14Features*>(node)->pipelineRobustness;addWaterRobustness=false;}
            }
            if(waterRobustness)required.emplace_back(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME);
        }
        if(features&&i.addressMessenger&&has(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME)){
            VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};supported.pNext=&addressFeatures;features(p,&supported);
            addressReport=addressFeatures.reportAddressBinding==VK_TRUE;addAddressReport=addressReport;
            for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext)if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT){addressReport=addressReport&&reinterpret_cast<const VkPhysicalDeviceAddressBindingReportFeaturesEXT*>(node)->reportAddressBinding;addAddressReport=false;}
            if(addressReport)required.emplace_back(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
        }
        argent::log("WATER_ROBUSTNESS_FEATURE enabled="+std::to_string(waterRobustness)+" scope=water-pipeline-only");
        argent::log("GPU_ADDRESS_REPORT enabled="+std::to_string(addressReport));
    }
    if(argent::extendedLogging()){
        bool indexing=false;const char* source="absent";
        for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext){
            if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES){indexing=reinterpret_cast<const VkPhysicalDeviceDescriptorIndexingFeatures*>(node)->shaderSampledImageArrayNonUniformIndexing!=0;source="descriptor-indexing";}
            if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES){indexing=reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node)->shaderSampledImageArrayNonUniformIndexing!=0;source="vulkan12";}
        }
        argent::log("GPU_MATERIAL_FEATURES sampledImageNonUniform="+std::to_string(indexing)+" source="+source);
    }
    auto enabled=extensions(ci->enabledExtensionCount,ci->ppEnabledExtensionNames,required);
    auto modified=*ci;modified.enabledExtensionCount=uint32_t(enabled.size());modified.ppEnabledExtensionNames=enabled.data();
    VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
    auto runtimeModified=modified;
    while(runtimeModified.pNext&&static_cast<const VkBaseInStructure*>(runtimeModified.pNext)->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)
        runtimeModified.pNext=static_cast<const VkBaseInStructure*>(runtimeModified.pNext)->pNext;
    auto runtimeWaterFeatures=waterFeatures;auto runtimeAddressFeatures=addressFeatures;
    auto runtimeNvFeatures=nvFeatures;auto runtimeNvConfig=nvConfig;
    if(addNvFeatures){nvFeatures.pNext=const_cast<void*>(modified.pNext);modified.pNext=&nvFeatures;runtimeNvFeatures.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeNvFeatures;}
    if(addNvConfig){nvConfig.pNext=modified.pNext;modified.pNext=&nvConfig;runtimeNvConfig.pNext=runtimeModified.pNext;runtimeModified.pNext=&runtimeNvConfig;}
    if(addWaterRobustness){waterFeatures.pNext=const_cast<void*>(modified.pNext);modified.pNext=&waterFeatures;runtimeWaterFeatures.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeWaterFeatures;}
    if(addAddressReport){addressFeatures.pNext=const_cast<void*>(modified.pNext);modified.pNext=&addressFeatures;runtimeAddressFeatures.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeAddressFeatures;}
    auto runtimeFaultFeatures=faultFeatures;
    if(addFaultFeatures){
        faultFeatures.pNext=const_cast<void*>(modified.pNext);modified.pNext=&faultFeatures;
        runtimeFaultFeatures.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeFaultFeatures;
    }
    VkPhysicalDeviceMultiviewFeatures runtimeMv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
    if(sfsRequested){
        const bool present=argent::sfs::multiviewRequest(ci->pNext).present;
        if(!present){mv.multiview=VK_TRUE;mv.pNext=const_cast<void*>(modified.pNext);modified.pNext=&mv;
            runtimeMv.multiview=VK_TRUE;runtimeMv.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeMv;}
    }
    auto create=reinterpret_cast<PFN_vkCreateDevice>(get(i.handle,"vkCreateDevice"));
    VkResult r=VK_ERROR_INITIALIZATION_FAILED;
    char steamDefer[8]{};
    const bool deferSteamXrDevice=i.game&&GetEnvironmentVariableA("ARGENT_STEAMVR_DEFER_XR_DEVICE",steamDefer,sizeof(steamDefer))==1&&steamDefer[0]=='1';
    if(deferSteamXrDevice)argent::log("XR_VULKAN_DEVICE deferred: SteamVR safe-start uses normal game vkCreateDevice");
    if(deferSteamXrDevice||!i.game||!argent::xrCreateGameDevice(get,gdpa,i.handle,p,&runtimeModified,&modified,a,out,r))r=create(p,&modified,a,out);
    if(r==VK_SUCCESS){
        auto s=std::make_shared<State>();s->device=*out;s->physical=p;s->instance=i.handle;s->gipa=i.gipa;s->gdpa=gdpa;s->setLoaderData=setData;s->game=i.game;
        uint32_t count=0;auto families=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(i.gipa(i.handle,"vkGetPhysicalDeviceQueueFamilyProperties"));
        families(p,&count,nullptr);s->families.resize(count);families(p,&count,s->families.data());
        if(sfsRequested){
            VkPhysicalDeviceMemoryProperties memory{};
            reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(i.gipa(i.handle,"vkGetPhysicalDeviceMemoryProperties"))(p,&memory);
            auto config=argent::sfs::vrEnabled()?argent::sfs::eternalProfile():argent::sfs::Configuration{};
            if(!argent::sfs::initialize(*out,p,gdpa,memory,config)){
                reinterpret_cast<PFN_vkDestroyDevice>(gdpa(*out,"vkDestroyDevice"))(*out,a);*out=VK_NULL_HANDLE;return VK_ERROR_INITIALIZATION_FAILED;
            }
            if(gpuCheckpoints)argent::sfs::enableGpuCheckpoints(*out,gdpa);
            if(gpuFault)argent::sfs::enableGpuFaultReport(*out,gdpa);
            if(waterRobustness)argent::sfs::enableWaterRobustness(*out);
            {
                VkPhysicalDeviceProperties properties{};reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(i.gipa(i.handle,"vkGetPhysicalDeviceProperties"))(p,&properties);
                argent::sfs::configurePerformanceGpu(*out,properties.limits.timestampPeriod,uint32_t(s->families.size()),s->families.data());
            }
            s->sfs=true;for(uint32_t qi=0;qi<ci->queueCreateInfoCount;++qi)s->queueFamilies.push_back(ci->pQueueCreateInfos[qi].queueFamilyIndex);argent::log(argent::sfs::vrEnabled()?"SFS_VR active: experimental Eternal camera and OpenXR projection":"SFS_ENGINE_PROBE active: multiview internals, desktop mono output, no XR projection");
        }
        if(s->sfs&&argent::sfs::vrEnabled()){
            // Both verified builds have mapped NGX entry points and ABI guards.
            // Skipping Store leaves DLSS evaluating only the original eye.
            if(argent::camera::install())argent::dlss::install();
        }
        {std::lock_guard<std::recursive_mutex> l(stateMutex);devices[key(*out)]=s;deviceGeneration.fetch_add(1,std::memory_order_release);}
        argent::xrBindGameDevice(*out,gdpa);
    }
    if(i.game)argent::log("vkCreateDevice result="+std::to_string(r));return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice d,const VkAllocationCallbacks* a){
    auto s=deviceOf(key(d));if(!s)return;
    for(auto& m:s->mirrors)m.second->destroy(*s);s->mirrors.clear();
    if(s->game){argent::camera::stop();argent::shutdownXR(d);}
    if(s->sfs)argent::sfs::shutdown(d);
    argent::capture::forgetDevice(d);
    {std::lock_guard<std::recursive_mutex> l(stateMutex);devices.erase(key(d));deviceGeneration.fetch_add(1,std::memory_order_release);}
    s->proc<PFN_vkDestroyDevice>("vkDestroyDevice")(d,a);
}
void rememberQueue(const std::shared_ptr<State>& s,VkQueue q,uint32_t family,uint32_t index){
    std::lock_guard<std::recursive_mutex> l(stateMutex);
    s->queues[q]={family,index,family<s->families.size()&&(s->families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)!=0};
    const auto flags=family<s->families.size()?s->families[family].queueFlags:0;
    if(kharvox::selectXrGraphicsQueue(s->graphicsQueue,q,flags)){s->graphicsQueue=q;s->graphicsFamily=family;s->graphicsIndex=index;}
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice d,uint32_t f,uint32_t i,VkQueue* q){
    auto s=deviceOf(key(d));s->proc<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(d,f,i,q);rememberQueue(s,*q,f,i);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice d,const VkDeviceQueueInfo2* i,VkQueue* q){
    auto s=deviceOf(key(d));s->proc<PFN_vkGetDeviceQueue2>("vkGetDeviceQueue2")(d,i,q);if(*q)rememberQueue(s,*q,i->queueFamilyIndex,i->queueIndex);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice d,VkSwapchainKHR sc,uint32_t* count,VkImage* images){
 auto s=deviceOf(key(d));if(argent::sfs::sourceSwapchain(d,sc))return argent::sfs::sourceImages(d,sc,count,images);
 return s->proc<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(d,sc,count,images);
}
bool quadSteamPrepareAllowed(const std::shared_ptr<State>& s){
 return s&&(!s->sfs||!argent::sfs::vrEnabled());
}
VkResult prepareStereo(const std::shared_ptr<State>& s,VkSwapchainKHR sc,uint32_t image){
 if(!s->sfs||!argent::sfs::vrEnabled())return VK_SUCCESS;
 argent::Source source;{std::lock_guard<std::recursive_mutex> lock(stateMutex);auto it=s->sources.find(sc);if(it==s->sources.end())return VK_SUCCESS;source=it->second;}
 argent::sfs::FramePose pose;XrPosef head;
 const auto options=argent::presentation::options();
 auto sample=argent::presentation::snapshot();sample.flatMenu=argent::hud::flatMenuVisible(true)||(options.cinematics3d&&sample.cutscene&&argent::hud::flatMenuVisible());
 const auto mode=argent::presentation::classify(sample,GetTickCount64(),options);
 bool nextQuad=s->menuQuad;
 const bool manualToggle=(GetAsyncKeyState(VK_F11)&1)!=0;
 if(options.automatic)nextQuad=s->presentation.update(mode,options,GetTickCount64());
 else if(manualToggle)nextQuad=!nextQuad;
 argent::presentation::scriptedMovement=mode==argent::presentation::Mode::Sync||mode==argent::presentation::Mode::Traversal||mode==argent::presentation::Mode::Interaction;
 argent::presentation::gameplayInput=options.automatic?mode==argent::presentation::Mode::Gameplay:!nextQuad;
 argent::presentation::worldPresentation=!nextQuad;
 argent::presentation::hideGameplayHud=argent::presentation::hideHudDuringAnimation(mode,nextQuad);
 const bool changedQuad=nextQuad!=s->menuQuad;
 if(changedQuad){s->menuQuad=nextQuad;if(!nextQuad)s->calibrated=false;argent::camera::stop();}
 if(mode!=s->presentationMode||changedQuad){s->presentationMode=mode;argent::log(std::string("SFS_MODE automatic=")+std::to_string(options.automatic)+" context="+argent::presentation::name(mode)+" quad="+std::to_string(s->menuQuad));}
 argent::sfs::EyeUniforms uniforms{{argent::sfs::identity(),argent::sfs::identity()},{}};
 argent::sfs::Matrix cinematicProjection{};
 const bool wantCinema=argent::presentation::stereoCinematic(mode,options,s->menuQuad)&&s->camera.projection(cinematicProjection,100);
 if(!argent::beginStereoFrame(*s,source,pose,head,s->menuQuad,wantCinema?&cinematicProjection:nullptr,&uniforms)){argent::camera::stop();return VK_SUCCESS;}
 if(s->cinematicStereo!=pose.stereoQuad){s->cinematicStereo=pose.stereoQuad;
  argent::log(std::string("CINEMATIC_STEREO active=")+std::to_string(pose.stereoQuad)+" context="+argent::presentation::name(mode)+" cameraFresh="+std::to_string(wantCinema)+" headLocked="+std::to_string(pose.quadHeadLocked)+" tracked="+std::to_string(pose.headPositionTracked));}
 argent::camera::beginRender(pose.serial);
 const bool recenter=!s->calibrated||pose.recenterRequested||(GetAsyncKeyState(VK_F12)&1);
 if(recenter){s->calibratedHead=head;s->calibrated=true;argent::log("SFS_RECENTER calibrated current head");}
 argent::camera::updatePose(head,!s->menuQuad,recenter,pose.headPositionTracked);
 if(argent::extendedLogging()&&pose.serial%120==0){auto h=argent::camera::stats();argent::log("ETERNAL_HOOK calls="+std::to_string(h.calls)+" applied="+std::to_string(h.applied)+" rejected="+std::to_string(h.rejected));}
  argent::sfs::Matrix projection{};projection[0]=1;projection[5]=-float(source.extent.width)/float(source.extent.height);
  if(!s->menuQuad&&s->camera.projection(projection)){
  if(!argent::sfs::parallelEyeProjection(projection,pose.views,argent::camera::unitsPerMeter(),uniforms)){argent::cancelStereoFrame();return VK_SUCCESS;}
  if(argent::extendedLogging()&&(pose.serial==1||pose.serial%120==0))argent::log("ETERNAL_CAMERA_STEREO serial="+std::to_string(pose.serial)+" fx="+std::to_string(projection[0])+" fy="+std::to_string(projection[5])+" unitsPerMeter="+std::to_string(argent::camera::unitsPerMeter())+" positionTracked="+std::to_string(pose.headPositionTracked));
 }
  if(!s->menuQuad&&!argent::sfs::screenProjection(projection,head,pose.views,uniforms)){argent::log("SFS_UI_PROJECTION_UNAVAILABLE");}
  static const auto gridMarker=[] {wchar_t p[32768]{};const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",p,32768);return n&&n<32768?std::filesystem::path(p).parent_path()/L"test-light-grid":std::filesystem::path{};}();
  static bool gridTest=false;
  if(!argent::cleanRelease&&pose.serial%120==0&&!gridMarker.empty()){
   const auto attributes=GetFileAttributesW(gridMarker.c_str());const bool enabled=attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
   if(enabled!=gridTest){gridTest=enabled;argent::log("ETERNAL_GRID_TEST enabled="+std::to_string(gridTest));}
  }
  const auto shaderPolicy=argent::presentation::shaderPolicy(s->menuQuad,pose.stereoQuad);
  uniforms.diagnostics[0]=shaderPolicy.centerGrid?1.f:0.f;
  uniforms.diagnostics[1]=shaderPolicy.worldWorkarounds?1.f:0.f;
  if(argent::extendedLogging()&&pose.stereoQuad&&pose.serial%120==0){
   argent::log("CINEMATIC_SHADER_POLICY worldWorkarounds="+std::to_string(shaderPolicy.worldWorkarounds)+
    " centerGrid="+std::to_string(shaderPolicy.centerGrid)+" centerGridDepthSafe="+std::to_string(shaderPolicy.centerGrid)+" fx="+std::to_string(cinematicProjection[0])+
    " gainLeft="+std::to_string(uniforms.clipFromCenter[0][0])+" gainRight="+std::to_string(uniforms.clipFromCenter[1][0])+
    " shiftWLeft="+std::to_string(uniforms.eyeTranslation[0][3])+" shiftWRight="+std::to_string(uniforms.eyeTranslation[1][3]));
  }
  static bool coarseDecals=false;
  if(!argent::cleanRelease&&pose.serial%120==0&&!gridMarker.empty()){
   const auto marker=gridMarker.parent_path()/L"test-decal-coarse";
   const auto attributes=GetFileAttributesW(marker.c_str());
   const bool enabled=attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
   if(enabled!=coarseDecals){coarseDecals=enabled;argent::log("ETERNAL_DECAL_COARSE enabled="+std::to_string(enabled));}
  }
  uniforms.diagnostics[2]=coarseDecals?1.f:0.f;
  static int screenProbe=0;
  if(!argent::cleanRelease&&pose.serial%120==0&&!gridMarker.empty()){
   int requested=0;std::ifstream file(gridMarker.parent_path()/L"test-screen-mode");
   if(!(file>>requested)||requested<0||requested>5)requested=0;
   if(requested!=screenProbe){screenProbe=requested;argent::log("ETERNAL_SCREEN_PROBE mode="+std::to_string(screenProbe));}
  }
  uniforms.diagnostics[3]=!s->menuQuad?float(screenProbe):0.f;
  if(screenProbe==2)uniforms.diagnostics[0]=0.f;
  s->framePose=pose;argent::sfs::prepare(s->device,pose,uniforms);
 VkResult result;
 {static argent::FrameTiming::Totals t;argent::FrameTiming timing("uniformRetirementAndUpload",t);
  // Match KHARVOX: no queue submit may race the drain or uniform upload.
  // Acquire queue before SFS metadata, matching the submission lock order.
  std::lock_guard<std::recursive_mutex> queueLock(*s->queueMutex);
  result=argent::sfs::beginFrame(s->device,sc,image);}
 if(result!=VK_SUCCESS){
  if(result==VK_ERROR_DEVICE_LOST){
   std::vector<VkQueue> queues;{std::lock_guard<std::recursive_mutex> lock(stateMutex);for(const auto& q:s->queues)queues.push_back(q.first);}
   for(auto queue:queues)argent::sfs::reportGpuCheckpoints(s->device,queue);
  }
  argent::camera::stop();argent::cancelStereoFrame();
 }
 return result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice d,VkSwapchainKHR sc,uint64_t timeout,VkSemaphore sem,VkFence fence,uint32_t* image){
 auto s=deviceOf(key(d));VkResult r;
 if(argent::sfs::sourceSwapchain(d,sc)){static argent::FrameTiming::Totals t;argent::FrameTiming timing("sourceAcquire",t);std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);r=argent::sfs::acquireSource(d,sc,timeout,sem,fence,image);}
 else r=s->proc<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(d,sc,timeout,sem,fence,image);
 if(r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR){if(quadSteamPrepareAllowed(s))argent::prepareSteamFrame(*s,sc);const auto prepared=prepareStereo(s,sc,*image);if(prepared!=VK_SUCCESS)return prepared;}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice d,const VkAcquireNextImageInfoKHR* info,uint32_t* image){
 auto s=deviceOf(key(d));if(argent::sfs::sourceSwapchain(d,info->swapchain))return info->deviceMask==1?vkAcquireNextImageKHR(d,info->swapchain,info->timeout,info->semaphore,info->fence,image):VK_ERROR_FEATURE_NOT_PRESENT;
 auto r=s->proc<PFN_vkAcquireNextImage2KHR>("vkAcquireNextImage2KHR")(d,info,image);
 if((r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR)&&image){if(quadSteamPrepareAllowed(s))argent::prepareSteamFrame(*s,info->swapchain);const auto prepared=prepareStereo(s,info->swapchain,*image);if(prepared!=VK_SUCCESS)return prepared;}
 return r;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice d,const VkSwapchainCreateInfoKHR* ci,const VkAllocationCallbacks* a,VkSwapchainKHR* out){
    auto s=deviceOf(key(d));auto modified=*ci;bool copy=false;
    if(s->sfs&&argent::sfs::sourceRingRequested()){
      if(!s->graphicsQueue)return VK_ERROR_INITIALIZATION_FAILED;
      if(!argent::sfs::sourceRingActive(d)){VkPhysicalDeviceMemoryProperties mem{};
       reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(s->gipa(s->instance,"vkGetPhysicalDeviceMemoryProperties"))(s->physical,&mem);
       if(!argent::sfs::configureSourceRing(d,s->gdpa,mem,s->graphicsQueue,nullptr,nullptr))return VK_ERROR_INITIALIZATION_FAILED;}
      if(ci->oldSwapchain){auto old=s->mirrors.find(ci->oldSwapchain);if(old!=s->mirrors.end()){old->second->destroy(*s);s->mirrors.erase(old);}}
      modified.imageArrayLayers=2;
      if(s->queueFamilies.size()>1){modified.imageSharingMode=VK_SHARING_MODE_CONCURRENT;modified.queueFamilyIndexCount=uint32_t(s->queueFamilies.size());modified.pQueueFamilyIndices=s->queueFamilies.data();}
      auto r=argent::sfs::createSourceSwapchain(d,modified,out);if(r!=VK_SUCCESS){argent::log("SFS_SOURCE_CREATE_FAILED result="+std::to_string(r));return r;}
      argent::Source source;source.extent=ci->imageExtent;source.format=ci->imageFormat;source.displaySrgb=ci->imageColorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;source.transferable=true;uint32_t count{};
      argent::sfs::sourceImages(d,*out,&count,nullptr);source.images.resize(count);argent::sfs::sourceImages(d,*out,&count,source.images.data());
      argent::sfs::swapchainImages(d,*out,count,source.images.data());
      {std::lock_guard<std::recursive_mutex> lock(stateMutex);s->sources[*out]=std::move(source);}
      auto mirror=std::make_unique<argent::DesktopMirror>();try{if(mirror->create(*s,*ci)){s->mirrors[*out]=std::move(mirror);}else argent::log("SFS_DESKTOP unavailable");}catch(const std::exception& e){mirror->destroy(*s);argent::log(e.what());}
      argent::log("SFS_SOURCE_SWAPCHAIN "+std::to_string(ci->imageExtent.width)+"x"+std::to_string(ci->imageExtent.height)+" requestedPresentMode="+std::to_string(ci->presentMode));return r;
    }
    if(s->game){
        VkSurfaceCapabilitiesKHR caps{};
        auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(s->gipa(s->instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
        copy=query&&query(s->physical,ci->surface,&caps)==VK_SUCCESS&&(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)&&ci->imageArrayLayers==1;
        if(copy)modified.imageUsage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    auto r=s->proc<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(d,&modified,a,out);
    if(r==VK_SUCCESS&&s->game){
        argent::Source source;source.extent=ci->imageExtent;source.format=ci->imageFormat;source.displaySrgb=ci->imageColorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;source.transferable=copy;
        auto images=s->proc<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");uint32_t count=0;
        if(images(d,*out,&count,nullptr)==VK_SUCCESS){source.images.resize(count);if(images(d,*out,&count,source.images.data())!=VK_SUCCESS)source.transferable=false;}
        {std::lock_guard<std::recursive_mutex> l(stateMutex);s->sources[*out]=std::move(source);}
        argent::log("SWAPCHAIN "+std::to_string(ci->imageExtent.width)+"x"+std::to_string(ci->imageExtent.height)+" format="+std::to_string(ci->imageFormat)+" transferable="+std::to_string(copy));
    }
    return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice d,VkSwapchainKHR sc,const VkAllocationCallbacks* a){
    auto s=deviceOf(key(d));auto mirror=s->mirrors.find(sc);if(mirror!=s->mirrors.end()){mirror->second->destroy(*s);s->mirrors.erase(mirror);}
    {std::lock_guard<std::recursive_mutex> l(stateMutex);s->sources.erase(sc);}
    if(argent::sfs::sourceSwapchain(d,sc)){argent::cancelStereoFrame();argent::sfs::swapchainDestroyed(d,sc);argent::sfs::destroySourceSwapchain(d,sc);}
    else s->proc<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(d,sc,a);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue q,const VkPresentInfoKHR* p){
    argent::PresentAnalysis analysis(argent::presentation::gameplayInput.load());
    auto s=deviceOf(key(q));if(!s)return VK_ERROR_DEVICE_LOST;
    bool consumed=false;argent::Source source;State::Queue queue{};bool known=false;
    if(s->game&&p->swapchainCount==1){
        {std::lock_guard<std::recursive_mutex> l(stateMutex);auto si=s->sources.find(p->pSwapchains[0]);auto qi=s->queues.find(q);
        if(si!=s->sources.end()&&qi!=s->queues.end()){source=si->second;queue=qi->second;known=true;}}
        const bool sfsVrPresent=s->sfs&&argent::sfs::vrEnabled();
        const bool sourcePresent=argent::sfs::sourceSwapchain(s->device,p->pSwapchains[0]);
        if(known&&sfsVrPresent){
          argent::sfs::StereoFrame pair;
          static std::atomic<uint64_t> sfsPairMiss{},sfsPresentMiss{};
          const bool pairReady=p->pImageIndices[0]<source.images.size()&&argent::sfs::pair(s->device,source.images[p->pImageIndices[0]],source.extent,source.format,pair);
          if(pairReady){
            argent::StereoMirror finalMirror;
            auto mirror=s->mirrors.find(p->pSwapchains[0]);
            if(sourcePresent&&mirror!=s->mirrors.end()&&mirror->second->needsFrame())
              finalMirror=[&](VkImage eye,VkExtent2D extent,VkImageLayout layout){
                try{static argent::FrameTiming::Totals t;argent::FrameTiming timing("desktopMirror",t);
                  mirror->second->present(*s,eye,q,argent::DesktopMirrorPacing::Clock::now(),extent,layout,true);
                }catch(const std::exception& e){argent::log(e.what());}
              };
            consumed=argent::presentStereoFrame(*s,pair,p->waitSemaphoreCount,p->pWaitSemaphores,finalMirror);
            if(!consumed){auto n=++sfsPresentMiss;if(n<=8||(argent::extendedLogging()&&n%120==0))argent::log("SFS_PRESENT_NOT_CONSUMED count="+std::to_string(n)+" image="+std::to_string(p->pImageIndices[0])+" sourcePresent="+std::to_string(sourcePresent));}
          }else{
            auto n=++sfsPairMiss;if(n<=8||(argent::extendedLogging()&&n%120==0))argent::log("SFS_PRESENT_NO_PAIR count="+std::to_string(n)+" image="+std::to_string(p->pImageIndices[0])+" images="+std::to_string(source.images.size())+" extent="+std::to_string(source.extent.width)+"x"+std::to_string(source.extent.height));
            argent::cancelStereoFrame();
          }
          if(consumed)argent::readbackStereoIfRequested(*s,source.images[p->pImageIndices[0]],source.extent,source.format);
          if(sourcePresent){
            VkResult r;{static argent::FrameTiming::Totals t;argent::FrameTiming timing("sourceRetire",t);std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);r=argent::sfs::presentSource(s->device,q,*p,consumed);}
            argent::sfs::copyCompleted(s->device);argent::trace::present();auto n=++presents;
            if(n==1||(argent::extendedLogging()&&n%120==0))argent::log("SFS_GAME_PRESENT count="+std::to_string(n)+" result="+std::to_string(r)+" XRcopied="+std::to_string(consumed));return r;
          }
          argent::sfs::copyCompleted(s->device);
        }
        if(!sfsVrPresent&&known&&source.transferable&&s->graphicsQueue)consumed=argent::presentQuad(*s,q,queue.family,queue.index,source,p->pImageIndices[0],*p);
    }
    auto ready=*p;if(consumed){ready.waitSemaphoreCount=0;ready.pWaitSemaphores=nullptr;}
    VkResult r;{std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);r=s->proc<PFN_vkQueuePresentKHR>("vkQueuePresentKHR")(q,&ready);}
    if(s->game){argent::trace::present();auto n=++presents;if(n==1||n%600==0)argent::log("GAME_PRESENT count="+std::to_string(n)+" result="+std::to_string(r)+" XRcopied="+std::to_string(consumed)+" known="+std::to_string(known)+" graphics="+std::to_string(queue.graphics)+" transferable="+std::to_string(source.transferable)+" family="+std::to_string(queue.family)+" index="+std::to_string(queue.index)+" swapchains="+std::to_string(p->swapchainCount));}
    return r;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue q,uint32_t n,const VkSubmitInfo* p,VkFence f){
 auto s=deviceOf(key(q));static argent::FrameTiming::Totals total;argent::FrameTiming timing("gameQueueSubmitTotal",total);
 std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);
 {static argent::FrameTiming::Totals camera;argent::FrameTiming timing("cameraSubmit",camera);observeCamera(s,[&](auto& c){for(uint32_t i=0;i<n;++i)c.submit(q,p[i].commandBufferCount,p[i].pCommandBuffers);});}
 VkResult r;{static argent::FrameTiming::Totals driver;argent::FrameTiming timing("gameQueueSubmitDriver",driver);r=s->proc<PFN_vkQueueSubmit>("vkQueueSubmit")(q,n,p,f);}
 if(r==VK_SUCCESS&&s->sfs)for(uint32_t i=0;i<n;++i)argent::sfs::waterCaptureSubmitted(s->device,q,p[i].commandBufferCount,p[i].pCommandBuffers);
 if(r==VK_SUCCESS&&s->sfs&&argent::perf::enabled())for(uint32_t i=0;i<n;++i)argent::sfs::performanceSubmitted(s->device,q,p[i].commandBufferCount,p[i].pCommandBuffers);
 if(r==VK_SUCCESS&&s->game&&s->camera.gpuProbePending()){
  uint32_t family=UINT32_MAX;{std::lock_guard<std::recursive_mutex> lock(stateMutex);auto it=s->queues.find(q);if(it!=s->queues.end())family=it->second.family;}
  if(family!=UINT32_MAX)s->camera.readback(*s,q,family,true);
 }
 if(r==VK_SUCCESS&&s->game)for(uint32_t i=0;i<n;++i)argent::trace::submit(q,p[i].commandBufferCount,p[i].pCommandBuffers);return r;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue q,uint32_t n,const VkSubmitInfo2* p,VkFence f){auto s=deviceOf(key(q));std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);const auto result=s->proc<PFN_vkQueueSubmit2>("vkQueueSubmit2")(q,n,p,f);if(result==VK_SUCCESS&&s->sfs)for(uint32_t i=0;i<n;++i)for(uint32_t j=0;j<p[i].commandBufferInfoCount;++j){const auto cb=p[i].pCommandBufferInfos[j].commandBuffer;argent::sfs::waterCaptureSubmitted(s->device,q,1,&cb);if(argent::perf::enabled())argent::sfs::performanceSubmitted(s->device,q,1,&cb);}return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(VkQueue q,uint32_t n,const VkSubmitInfo2* p,VkFence f){auto s=deviceOf(key(q));std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);const auto result=s->proc<PFN_vkQueueSubmit2KHR>("vkQueueSubmit2KHR")(q,n,p,f);if(result==VK_SUCCESS&&s->sfs)for(uint32_t i=0;i<n;++i)for(uint32_t j=0;j<p[i].commandBufferInfoCount;++j){const auto cb=p[i].pCommandBufferInfos[j].commandBuffer;argent::sfs::waterCaptureSubmitted(s->device,q,1,&cb);if(argent::perf::enabled())argent::sfs::performanceSubmitted(s->device,q,1,&cb);}return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue q){auto s=deviceOf(key(q));std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);return s->proc<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(q);}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice d,uint32_t n,const VkFence* fences,VkBool32 all,uint64_t timeout){
 auto s=deviceOf(key(d));static argent::FrameTiming::Totals t;argent::FrameTiming timing("gameWaitForFences",t);
 const auto result=s->proc<PFN_vkWaitForFences>("vkWaitForFences")(d,n,fences,all,timeout);
 static std::atomic<bool> reported{};if(result==VK_ERROR_DEVICE_LOST&&!reported.exchange(true)){
  argent::log("GAME_DEVICE_LOST vkWaitForFences");std::vector<VkQueue> queues;{std::lock_guard<std::recursive_mutex> lock(stateMutex);for(const auto& q:s->queues)queues.push_back(q.first);}
  if(s->sfs)for(auto q:queues)argent::sfs::reportGpuCheckpoints(d,q);
 }
 // Eternal retries immediately after device loss (observed ~14M calls/s).
 // Preserve the error and the live process/dumps, but yield between retries.
 // Successful waits and timeouts retain their normal timing.
 if(result==VK_ERROR_DEVICE_LOST)Sleep(10);
 return result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice d,const VkSemaphoreWaitInfo* info,uint64_t timeout){
 auto s=deviceOf(key(d));static argent::FrameTiming::Totals t;argent::FrameTiming timing("gameWaitSemaphores",t);
 return s->proc<PFN_vkWaitSemaphores>("vkWaitSemaphores")(d,info,timeout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphoresKHR(VkDevice d,const VkSemaphoreWaitInfo* info,uint64_t timeout){
 auto s=deviceOf(key(d));static argent::FrameTiming::Totals t;argent::FrameTiming timing("gameWaitSemaphoresKHR",t);
 return s->proc<PFN_vkWaitSemaphoresKHR>("vkWaitSemaphoresKHR")(d,info,timeout);
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice d){
 auto s=deviceOf(key(d));static argent::FrameTiming::Totals t;argent::FrameTiming timing("gameDeviceWaitIdle",t);
 std::lock_guard<std::recursive_mutex> lock(*s->queueMutex);
 return s->proc<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(d);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice d,const VkShaderModuleCreateInfo* ci,const VkAllocationCallbacks* a,VkShaderModule* out){auto s=deviceOf(key(d));auto r=s->proc<PFN_vkCreateShaderModule>("vkCreateShaderModule")(d,ci,a,out);if(r==VK_SUCCESS&&s->game)argent::capture::shader(d,*out,ci);return r;}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice d,VkShaderModule m,const VkAllocationCallbacks* a){auto s=deviceOf(key(d));if(s->game)argent::capture::forgetShader(d,m);s->proc<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(d,m,a);}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo* ci,const VkAllocationCallbacks* a,VkPipeline* out){auto s=deviceOf(key(d));auto r=s->proc<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(d,cache,count,ci,a,out);if(r==VK_SUCCESS&&s->game)argent::capture::graphics(d,count,ci,out);if(r==VK_SUCCESS)observeCamera(s,[&](auto& c){for(uint32_t j=0;j<count;++j)for(uint32_t k=0;k<ci[j].stageCount;++k)if(ci[j].pStages[k].stage==VK_SHADER_STAGE_FRAGMENT_BIT)c.probePipeline(out[j],argent::capture::shaderIdentity(d,ci[j].pStages[k].module));});if(r==VK_SUCCESS)observeCamera(s,[&](auto& c){for(uint32_t j=0;j<count;++j)for(uint32_t k=0;k<ci[j].stageCount;++k)if(ci[j].pStages[k].stage==VK_SHADER_STAGE_VERTEX_BIT){auto sha=argent::capture::shaderIdentity(d,ci[j].pStages[k].module);if(sha=="bb70988550d5be2d06bc4b9ee0ab2f871ed9cc1c68ad18184881a385d8a29cd3")c.pipeline(out[j],2,0);if(sha=="81544c6fef48ee0e42c56c8dacbd65644fb81ef4cd8f96f0dc04778595a19e01")c.pipeline(out[j],3,4);}});return r;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo* ci,const VkAllocationCallbacks* a,VkPipeline* out){auto s=deviceOf(key(d));auto r=s->proc<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(d,cache,count,ci,a,out);if(r==VK_SUCCESS&&s->game)argent::capture::compute(d,count,ci,out);if(r==VK_SUCCESS)observeCamera(s,[&](auto& c){for(uint32_t j=0;j<count;++j)c.probePipeline(out[j],argent::capture::shaderIdentity(d,ci[j].stage.module));});return r;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice d,const VkDescriptorSetLayoutCreateInfo* ci,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){auto s=deviceOf(key(d));auto r=s->proc<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(d,ci,a,out);if(r==VK_SUCCESS&&s->game)argent::capture::descriptorLayout(d,*out,ci);if(r==VK_SUCCESS)observeCamera(s,[&](auto& c){c.layout(*out,ci);});return r;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice d,const VkPipelineLayoutCreateInfo* ci,const VkAllocationCallbacks* a,VkPipelineLayout* out){auto s=deviceOf(key(d));auto r=s->proc<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(d,ci,a,out);if(r==VK_SUCCESS&&s->game)argent::capture::pipelineLayout(d,*out,ci);if(r==VK_SUCCESS)observeCamera(s,[&](auto& c){c.pipelineLayout(*out,ci);});return r;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice d,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks* a,VkRenderPass* out){auto s=deviceOf(key(d));auto r=s->proc<PFN_vkCreateRenderPass>("vkCreateRenderPass")(d,ci,a,out);if(r==VK_SUCCESS&&s->game)argent::capture::renderPass(d,*out,ci);return r;}
#include "CameraCaptureHooks.inc"
#include "RenderTraceHooks.inc"
PFN_vkVoidFunction intercept(const char* n){
#define HOOK(name) if(!strcmp(n,#name))return reinterpret_cast<PFN_vkVoidFunction>(name)
    HOOK(vkGetInstanceProcAddr);HOOK(vkGetDeviceProcAddr);HOOK(vkCreateInstance);HOOK(vkDestroyInstance);HOOK(vkCreateDevice);HOOK(vkDestroyDevice);
    HOOK(vkGetSwapchainImagesKHR);HOOK(vkAcquireNextImageKHR);HOOK(vkAcquireNextImage2KHR);HOOK(vkGetDeviceQueue);HOOK(vkGetDeviceQueue2);HOOK(vkCreateSwapchainKHR);HOOK(vkDestroySwapchainKHR);HOOK(vkQueuePresentKHR);
    HOOK(vkQueueSubmit);HOOK(vkQueueSubmit2);HOOK(vkQueueSubmit2KHR);HOOK(vkQueueWaitIdle);
    HOOK(vkWaitForFences);HOOK(vkWaitSemaphores);HOOK(vkWaitSemaphoresKHR);HOOK(vkDeviceWaitIdle);
    HOOK(vkCreateShaderModule);HOOK(vkDestroyShaderModule);HOOK(vkCreateGraphicsPipelines);HOOK(vkCreateComputePipelines);
    HOOK(vkCreateDescriptorSetLayout);HOOK(vkCreatePipelineLayout);HOOK(vkDestroyPipelineLayout);HOOK(vkCreateRenderPass);
    HOOK(vkAllocateCommandBuffers);HOOK(vkFreeCommandBuffers);HOOK(vkBeginCommandBuffer);HOOK(vkResetCommandBuffer);HOOK(vkResetCommandPool);HOOK(vkDestroyCommandPool);
    HOOK(vkCmdSetViewport);HOOK(vkCmdSetScissor);HOOK(vkCmdBindPipeline);HOOK(vkCmdBeginRenderPass);HOOK(vkCmdEndRenderPass);HOOK(vkCmdNextSubpass);HOOK(vkCmdDraw);HOOK(vkCmdDrawIndexed);HOOK(vkCmdDrawIndirect);HOOK(vkCmdDrawIndexedIndirect);HOOK(vkCmdDispatch);HOOK(vkCmdDispatchIndirect);HOOK(vkCmdExecuteCommands);
    HOOK(vkCreateImage);HOOK(vkCreateImageView);HOOK(vkCreateFramebuffer);
    HOOK(vkAllocateMemory);HOOK(vkFreeMemory);HOOK(vkMapMemory);HOOK(vkUnmapMemory);HOOK(vkCreateBuffer);HOOK(vkDestroyBuffer);HOOK(vkBindBufferMemory);HOOK(vkBindBufferMemory2);HOOK(vkBindBufferMemory2KHR);
    HOOK(vkAllocateDescriptorSets);HOOK(vkUpdateDescriptorSets);HOOK(vkFreeDescriptorSets);HOOK(vkResetDescriptorPool);HOOK(vkDestroyDescriptorPool);HOOK(vkDestroyPipeline);HOOK(vkCmdBindDescriptorSets);HOOK(vkCmdPushConstants);HOOK(vkCmdCopyBuffer);
#undef HOOK
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i,const char* n){
    if(!n)return nullptr;auto s=instanceOf(key(i));if(s.runtimeAuxiliary)return s.gipa?s.gipa(i,n):nullptr;if(auto f=intercept(n))return f;auto next=s.gipa?s.gipa(i,n):nullptr;next=surfaceIntercept(n,next);return s.game?argent::sfs::wrapProc(VK_NULL_HANDLE,n,next):next;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice d,const char* n){
    if(!n)return nullptr;auto s=deviceOf(key(d));if(!s)return nullptr;
    if(s->runtimeAuxiliary)return s->gdpa?s->gdpa(d,n):nullptr;
    auto next=s->gdpa(d,n);if(!next)return nullptr;if(auto f=intercept(n))return f;return s->sfs?argent::sfs::wrapProc(d,n,next):next;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL physicalProc(VkInstance i,const char* n){auto s=instanceOf(key(i));return surfaceIntercept(n,s.physicalProc?s.physicalProc(i,n):(s.gipa?s.gipa(i,n):nullptr));}
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* v){
    if(!v||v->loaderLayerInterfaceVersion<2)return VK_ERROR_INITIALIZATION_FAILED;
    HMODULE pinned{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&vkNegotiateLoaderLayerInterfaceVersion),&pinned);
    v->loaderLayerInterfaceVersion=2;v->pfnGetInstanceProcAddr=vkGetInstanceProcAddr;v->pfnGetDeviceProcAddr=vkGetDeviceProcAddr;v->pfnGetPhysicalDeviceProcAddr=physicalProc;return VK_SUCCESS;
}
}


