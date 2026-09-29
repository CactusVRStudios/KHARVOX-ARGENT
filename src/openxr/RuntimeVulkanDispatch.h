#pragma once
#include <vulkan/vulkan.h>
#include <cstring>
#include <algorithm>
#include <iterator>

namespace kharvox {
inline bool selectXrGraphicsQueue(VkQueue current,VkQueue candidate,VkQueueFlags flags) {
    // XR copies also record graphics commands; compute/transfer-only queues
    // cannot replace the graphics queue when DOOM enumerates auxiliary queues.
    return candidate && (flags & VK_QUEUE_GRAPHICS_BIT) && (!current || current==candidate);
}

inline PFN_vkCreateDevice resolveLayerCreateDevice(PFN_vkGetInstanceProcAddr next, VkInstance instance) {
    // vkCreateDevice is an instance command. Null-instance lookup is only
    // specified for global commands and is not a portable driver shortcut.
    return next && instance ? reinterpret_cast<PFN_vkCreateDevice>(next(instance, "vkCreateDevice")) : nullptr;
}
template<class Handle> inline void finishRuntimeVulkanCreate(bool xrSucceeded, VkResult* result, Handle* output) {
    if (!result || !output) return;
    if (!xrSucceeded || (*result == VK_SUCCESS && !*output)) *result = VK_ERROR_INITIALIZATION_FAILED;
    if (*result != VK_SUCCESS) *output = VK_NULL_HANDLE;
}
inline bool isPhysicalDeviceCommand(const char* name) {
    return name && (!std::strncmp(name, "vkGetPhysicalDevice", 19) ||
        !std::strcmp(name, "vkEnumerateDeviceExtensionProperties") ||
        !std::strcmp(name, "vkEnumerateDeviceLayerProperties"));
}

// Some drivers return non-null GDPA pointers for physical/instance queries.
// Classify by the API's first dispatchable argument, never by pointer presence.
inline bool isRuntimeDeviceCommand(const char* name){
    if(!name)return false;
    static constexpr const char* commands[]{
#include "RuntimeDeviceCommands.inc"
    };
    return std::binary_search(std::begin(commands),std::end(commands),name,
        [](const char* a,const char* b){return std::strcmp(a,b)<0;});
}

enum class RuntimeDispatchRoute { Downstream, CreateDevice, SessionLoader, PhysicalLoader, RuntimeDevice };
struct RuntimeDispatchResult {
    PFN_vkVoidFunction function{};
    RuntimeDispatchRoute route{RuntimeDispatchRoute::Downstream};
};

// A physical handle must stay at the dispatch level which enumerated it.
// The simulator enumerates through our downstream callback and retains that
// callback for session/swapchain creation. Steam's retained session callback
// and the existing non-simulator physical-device route use the public loader.
inline RuntimeDispatchResult resolveRuntimeVulkanProc(
    VkInstance instance, const char* name, bool sessionLoader, bool simulator,
    PFN_vkGetInstanceProcAddr next, PFN_vkGetInstanceProcAddr loader,
    PFN_vkVoidFunction createDevice, VkDevice runtimeDevice=VK_NULL_HANDLE,
    PFN_vkGetDeviceProcAddr runtimeGdpa=nullptr) {
    if (!name) return {};
    // A retained GIPA can also request device commands. Route those below the
    // game's stereo transforms just like GDPA; keep instance/physical commands
    // on their existing loader level. Only the SFS caller supplies this route.
    if(runtimeDevice&&runtimeGdpa&&isRuntimeDeviceCommand(name)){
        if(!std::strcmp(name,"vkGetDeviceProcAddr"))
            return {reinterpret_cast<PFN_vkVoidFunction>(runtimeGdpa),RuntimeDispatchRoute::RuntimeDevice};
        if(auto command=runtimeGdpa(runtimeDevice,name))
            return {command,RuntimeDispatchRoute::RuntimeDevice};
    }
    if (sessionLoader && loader)
        return {loader(instance, name), RuntimeDispatchRoute::SessionLoader};
    if (!std::strcmp(name, "vkCreateDevice") && createDevice)
        return {createDevice, RuntimeDispatchRoute::CreateDevice};
    if (isPhysicalDeviceCommand(name) && !simulator && loader)
        return {loader(instance, name), RuntimeDispatchRoute::PhysicalLoader};
    // Do not fall back to public exports if a downstream command is absent.
    return {next ? next(instance, name) : nullptr, RuntimeDispatchRoute::Downstream};
}
} // namespace kharvox
