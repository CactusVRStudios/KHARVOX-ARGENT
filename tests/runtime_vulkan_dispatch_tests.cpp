#include "../src/openxr/RuntimeVulkanDispatch.h"
#include "../src/openxr/RuntimeDeviceCreateChain.h"
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include <cstdlib>
#include <iostream>

namespace {
VkInstance instance = reinterpret_cast<VkInstance>(uintptr_t(1));
VkPhysicalDevice downstreamPhysical = reinterpret_cast<VkPhysicalDevice>(uintptr_t(2));
VkPhysicalDevice publicPhysical = reinterpret_cast<VkPhysicalDevice>(uintptr_t(3));
void require(bool ok) { if (!ok) std::abort(); }
VKAPI_ATTR void VKAPI_CALL downstreamMemory(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties* out) {
    require(p == downstreamPhysical); out->memoryTypeCount = 7;
}
VKAPI_ATTR void VKAPI_CALL publicMemory(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties* out) {
    require(p == publicPhysical); out->memoryTypeCount = 9;
}
VKAPI_ATTR void VKAPI_CALL createAdapter() {}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL downstream(VkInstance i, const char* n) {
    require(i == instance);
    return kharvox::isPhysicalDeviceCommand(n) ? reinterpret_cast<PFN_vkVoidFunction>(downstreamMemory) : nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL loader(VkInstance i, const char* n) {
    require(i == instance);
    return kharvox::isPhysicalDeviceCommand(n) ? reinterpret_cast<PFN_vkVoidFunction>(publicMemory) : nullptr;
}
}
int main() {
    using namespace kharvox;
    require(selectOpenXRVulkanPath(OpenXRRuntimeKind::MetaOculus,false,true,true)==OpenXRVulkanPath::VulkanEnable2RuntimeManaged);
    require(selectOpenXRVulkanPath(OpenXRRuntimeKind::MetaOculus,false,false,true)==OpenXRVulkanPath::VulkanEnable2RuntimeManaged);
    require(selectOpenXRVulkanPath(OpenXRRuntimeKind::SteamVR,false,true,true)==OpenXRVulkanPath::VulkanEnable2RuntimeManaged);
    require(selectOpenXRVulkanPath(OpenXRRuntimeKind::SteamVR,false,true,false)==OpenXRVulkanPath::VulkanEnable1Direct);
    require(selectOpenXRVulkanPath(OpenXRRuntimeKind::VirtualDesktop,false,true,true)==OpenXRVulkanPath::VulkanEnable1Direct);
    require(selectOpenXRVulkanPath(OpenXRRuntimeKind::MetaOculus,false,false,false)==OpenXRVulkanPath::None);
    require(shouldPassthroughSteamRuntimeAuxiliary(OpenXRRuntimeKind::Unknown,false,"steamvr_vrclient_interop"));
    {
        VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};mv.multiview=VK_TRUE;
        VkPhysicalDeviceTimelineSemaphoreFeatures added{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};added.timelineSemaphore=VK_TRUE;added.pNext=&mv;
        VkLayerDeviceCreateInfo link{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO};link.function=VK_LAYER_LINK_INFO;link.pNext=&mv;
        VkLayerDeviceCreateInfo callback{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO};callback.function=VK_LOADER_DATA_CALLBACK;callback.pNext=&link;
        VkDeviceCreateInfo downstream{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};downstream.pNext=&callback;
        const char* extensions[]{"VK_KHR_timeline_semaphore"};
        VkDeviceCreateInfo runtime{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};runtime.pNext=&added;runtime.enabledExtensionCount=1;runtime.ppEnabledExtensionNames=extensions;
        argent::RuntimeDeviceCreateChain merged(runtime,downstream);
        auto first=static_cast<const VkLayerDeviceCreateInfo*>(merged.info.pNext);
        auto second=static_cast<const VkLayerDeviceCreateInfo*>(first->pNext);
        require(first!=&callback&&first->function==VK_LOADER_DATA_CALLBACK);
        require(second!=&link&&second->function==VK_LAYER_LINK_INFO&&second->pNext==&added);
        require(added.pNext==&mv&&callback.pNext==&link&&link.pNext==&mv);
        require(merged.info.enabledExtensionCount==1&&merged.info.ppEnabledExtensionNames==extensions);
    }
    {
        VkDeviceGroupDeviceCreateInfo group{VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO};
        VkPhysicalDeviceMultiviewFeatures feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};feature.pNext=&group;
        VkLayerDeviceCreateInfo loaderLink{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO};loaderLink.pNext=&feature;
        for(bool fail:{false,true}){
            try{
                argent::RestoreDeviceCreateLinks restore(&loaderLink);
                // Simulate nested loader relinking and a node whose storage expires.
                VkDeviceGroupDeviceCreateInfo temporary{VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO};
                feature.pNext=&temporary;
                loaderLink.pNext=nullptr;
                if(fail)throw 1;
            }catch(int){}
            require(loaderLink.pNext==&feature&&feature.pNext==&group&&group.pNext==nullptr);
        }
    }
    auto graphics=reinterpret_cast<VkQueue>(uintptr_t(10));
    auto auxiliary=reinterpret_cast<VkQueue>(uintptr_t(11));
    require(!selectXrGraphicsQueue(nullptr,auxiliary,VK_QUEUE_COMPUTE_BIT|VK_QUEUE_TRANSFER_BIT));
    require(selectXrGraphicsQueue(nullptr,graphics,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT));
    require(!selectXrGraphicsQueue(graphics,auxiliary,VK_QUEUE_COMPUTE_BIT));
    require(!selectXrGraphicsQueue(graphics,auxiliary,VK_QUEUE_OPTICAL_FLOW_BIT_NV));
    require(!selectXrGraphicsQueue(graphics,auxiliary,VK_QUEUE_GRAPHICS_BIT));
    require(selectXrGraphicsQueue(graphics,graphics,VK_QUEUE_GRAPHICS_BIT));
    require(!selectXrGraphicsQueue(nullptr,nullptr,VK_QUEUE_GRAPHICS_BIT));

    require(!resolveLayerCreateDevice(downstream,VK_NULL_HANDLE));
    auto createLookup=+[](VkInstance i,const char* name)->PFN_vkVoidFunction {
        require(i==instance && !std::strcmp(name,"vkCreateDevice"));
        return reinterpret_cast<PFN_vkVoidFunction>(createAdapter);
    };
    require(resolveLayerCreateDevice(createLookup,instance)==reinterpret_cast<PFN_vkCreateDevice>(createAdapter));
    for(bool xrSuccess:{false,true})for(auto vkResult:{VK_SUCCESS,VK_ERROR_DEVICE_LOST}) {
        auto output=reinterpret_cast<VkDevice>(uintptr_t(4)); auto status=vkResult;
        finishRuntimeVulkanCreate(xrSuccess,&status,&output);
        require((status==VK_SUCCESS)==(xrSuccess&&vkResult==VK_SUCCESS));
        require(bool(output)==(status==VK_SUCCESS));
    }
    VkDevice empty{};VkResult staleSuccess=VK_SUCCESS;
    finishRuntimeVulkanCreate(true,&staleSuccess,&empty);
    require(staleSuccess==VK_ERROR_INITIALIZATION_FAILED && !empty);

    for (const char* command : {"vkGetPhysicalDeviceMemoryProperties", "vkGetPhysicalDeviceProperties2",
         "vkEnumerateDeviceExtensionProperties", "vkEnumerateDeviceLayerProperties"}) {
        auto sim = resolveRuntimeVulkanProc(instance, command, false, true, downstream, loader, createAdapter);
        require(sim.function == reinterpret_cast<PFN_vkVoidFunction>(downstreamMemory));
        auto existing = resolveRuntimeVulkanProc(instance, command, false, false, downstream, loader, createAdapter);
        require(existing.function == reinterpret_cast<PFN_vkVoidFunction>(publicMemory));
        require(existing.route == RuntimeDispatchRoute::PhysicalLoader);
    }
    for(const char* command:{"vkCreateImage","vkGetDeviceProcAddr","vkCmdPipelineBarrier2KHR","vkQueueSubmit"})
        require(isRuntimeDeviceCommand(command));
    for(const char* command:{"vkCreateInstance","vkCreateDevice","vkEnumeratePhysicalDevices",
        "vkGetPhysicalDeviceProperties2","vkCreateWin32SurfaceKHR","vkDestroyInstance","vkUnknownCommand"})
        require(!isRuntimeDeviceCommand(command));
    const auto gameDevice=reinterpret_cast<VkDevice>(uintptr_t(99));
    auto runtimeGdpa=+[](VkDevice d,const char* name)->PFN_vkVoidFunction{
        require(d==reinterpret_cast<VkDevice>(uintptr_t(99)));
        // Deliberately permissive, as observed with NVIDIA GDPA: even
        // physical queries can return non-null, so pointer presence is unsafe.
        return createAdapter;
    };
    for(const char* command:{"vkCreateImage","vkCreateGraphicsPipelines","vkCmdPipelineBarrier","vkGetDeviceProcAddr"}){
        const auto result=resolveRuntimeVulkanProc(instance,command,true,false,downstream,loader,nullptr,gameDevice,runtimeGdpa);
        require(result.route==RuntimeDispatchRoute::RuntimeDevice);
        require(result.function==(!std::strcmp(command,"vkGetDeviceProcAddr")
            ?reinterpret_cast<PFN_vkVoidFunction>(runtimeGdpa):createAdapter));
    }
    require(resolveRuntimeVulkanProc(instance,"vkGetPhysicalDeviceMemoryProperties",true,false,
        downstream,loader,nullptr,gameDevice,runtimeGdpa).route==RuntimeDispatchRoute::SessionLoader);
    require(!resolveRuntimeVulkanProc(instance,"vkUnknownCommand",true,false,
        downstream,loader,nullptr,gameDevice,runtimeGdpa).function);
    VkPhysicalDeviceMemoryProperties props{};
    // Repeat after device creation, when the create adapter is no longer active.
    for (auto adapter : {PFN_vkVoidFunction(createAdapter), PFN_vkVoidFunction(nullptr)}) {
        auto sim = resolveRuntimeVulkanProc(instance, "vkGetPhysicalDeviceMemoryProperties", false, true,
            downstream, loader, adapter);
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(sim.function)(downstreamPhysical, &props);
        require(props.memoryTypeCount == 7);
    }
    auto steam = resolveRuntimeVulkanProc(instance, "vkGetPhysicalDeviceMemoryProperties", true, false,
        downstream, loader, nullptr);
    reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(steam.function)(publicPhysical, &props);
    require(props.memoryTypeCount == 9 && steam.route == RuntimeDispatchRoute::SessionLoader);
    require(resolveRuntimeVulkanProc(instance, "vkCreateDevice", false, true,
        downstream, loader, createAdapter).function == createAdapter);
    require(!resolveRuntimeVulkanProc(instance, "vkGetPhysicalDeviceMemoryProperties", false, true,
        nullptr, loader, nullptr).function);
    require(!resolveRuntimeVulkanProc(instance, "vkUnknownCommand", false, true,
        downstream, loader, nullptr).function);
    require(!resolveRuntimeVulkanProc(instance, nullptr, false, true, downstream, loader, nullptr).function);
    std::cout << "Runtime Vulkan dispatch tests passed\n";
}
