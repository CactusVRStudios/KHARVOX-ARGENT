#pragma once
#include <windows.h>
#include <vulkan/vulkan.h>
#include <filesystem>
#include <fstream>
#include <mutex>
namespace argent::sfs {
// Effective SFS allocations/views, not the application's pre-promotion request.
// Creation-only metadata: no GPU readback, barriers or extra dispatches.
struct WaterCaptureMetadata {
 std::filesystem::path root;
 std::mutex mutex;
 size_t records{};
 WaterCaptureMetadata(){wchar_t path[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",path,32768);if(n&&n<32768)root=path;}
};
inline WaterCaptureMetadata& waterCaptureMetadata(){static WaterCaptureMetadata state;return state;}
inline void waterCaptureImage(VkDevice d,VkImage image,const VkImageCreateInfo& i) noexcept {try{
 auto& s=waterCaptureMetadata();if(s.root.empty())return;std::lock_guard<std::mutex> lock(s.mutex);if(s.records>=200000)return;
 std::filesystem::create_directories(s.root);
 std::ofstream(s.root/"images.tsv",std::ios::app)<<++s.records<<'\t'<<GetTickCount64()<<'\t'<<reinterpret_cast<uintptr_t>(d)<<'\t'<<reinterpret_cast<uintptr_t>(image)<<'\t'<<i.format<<'\t'<<i.extent.width<<'\t'<<i.extent.height<<'\t'<<i.extent.depth<<'\t'<<i.arrayLayers<<'\t'<<i.mipLevels<<'\t'<<i.usage<<'\n';
}catch(...) {}}
inline void waterCaptureView(VkDevice d,VkImageView view,const VkImageViewCreateInfo& i) noexcept {try{
 auto& s=waterCaptureMetadata();if(s.root.empty())return;std::lock_guard<std::mutex> lock(s.mutex);if(s.records>=200000)return;
 const auto& r=i.subresourceRange;
 std::filesystem::create_directories(s.root);
 std::ofstream(s.root/"image-views.tsv",std::ios::app)<<++s.records<<'\t'<<GetTickCount64()<<'\t'<<reinterpret_cast<uintptr_t>(d)<<'\t'<<reinterpret_cast<uintptr_t>(view)<<'\t'<<reinterpret_cast<uintptr_t>(i.image)<<'\t'<<i.viewType<<'\t'<<i.format<<'\t'<<r.aspectMask<<'\t'<<r.baseMipLevel<<'\t'<<r.levelCount<<'\t'<<r.baseArrayLayer<<'\t'<<r.layerCount<<'\n';
}catch(...) {}}
}
