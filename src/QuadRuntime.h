#pragma once
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include "sfs/NativeSfs.h"

namespace argent {
void log(const std::string& message);
std::filesystem::path runtimePath();
bool initializeXR();
bool xrCreateGameInstance(PFN_vkGetInstanceProcAddr next,const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*,VkResult&);
bool xrCreateGameDevice(PFN_vkGetInstanceProcAddr next,PFN_vkGetDeviceProcAddr gdpa,VkInstance,VkPhysicalDevice,const VkDeviceCreateInfo* runtimeInfo,const VkDeviceCreateInfo* downstreamInfo,const VkAllocationCallbacks*,VkDevice*,VkResult&);
void xrBindGameDevice(VkDevice device,PFN_vkGetDeviceProcAddr gdpa);
std::vector<std::string> xrExtensions(bool device);
struct Device {
    std::shared_ptr<std::recursive_mutex> queueMutex=std::make_shared<std::recursive_mutex>();
    VkQueue graphicsQueue{};
    uint32_t graphicsFamily{},graphicsIndex{};
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    PFN_vkGetInstanceProcAddr gipa{};
    PFN_vkGetDeviceProcAddr gdpa{};
    PFN_vkSetDeviceLoaderData setLoaderData{};
    template<class T> T proc(const char* name) const { return reinterpret_cast<T>(gdpa(device,name)); }
};
struct Source {
    VkExtent2D extent{};
    VkFormat format{};
    bool transferable{};
    std::vector<VkImage> images;
    bool displaySrgb{}; // WSI SRGB_NONLINEAR bytes, including UNORM attachments.
};
// Returns whether the application's binary present wait semaphores were consumed.
bool presentQuad(Device& device,VkQueue queue,uint32_t family,uint32_t index,
                 const Source& source,uint32_t imageIndex,const VkPresentInfoKHR& present);
void prepareSteamFrame(Device& device,VkSwapchainKHR swapchain);
// Locate before rendering. The resulting serial/time must accompany both eyes.
bool beginStereoFrame(Device& device,const Source& source,sfs::FramePose& pose,XrPosef& head,bool quadView=false,
                      const sfs::Matrix* cinematicProjection=nullptr,sfs::EyeUniforms* cinematicUniforms=nullptr);
// Sources are graphics-queue-owned; waits signal completion of both eye layers.
// Returns whether the binary waits were consumed, including on submission error.
using StereoMirror = std::function<void(VkImage,VkExtent2D,VkImageLayout)>;
bool presentStereoFrame(Device& device,const sfs::StereoFrame& frame,
                        uint32_t waitCount,const VkSemaphore* waits,const StereoMirror& mirror = {});
void cancelStereoFrame();
void shutdownXR(VkDevice device);
}
