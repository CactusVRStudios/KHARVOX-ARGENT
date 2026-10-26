#pragma once
#include "transport.hpp"
#include <dxgi1_4.h>
#include <stdexcept>
namespace argent::fg {
// All calls bypass the layer's interception. Owned commands are retired before reuse.
struct Interop {
 Device dev;
 Microsoft::WRL::ComPtr<ID3D12Device> dx;
 Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
 Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
 Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
 Microsoft::WRL::ComPtr<ID3D12Fence> done;
 uint64_t value{};
 HANDLE event{};
 VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};
 // Input conversion has its own slot so color/DX recording can overlap it.
 std::array<VkCommandBuffer,3> commands{};
 std::array<VkFence,3> fences{};
 std::array<bool,3> pending{};
 unsigned slot{};
 Microsoft::WRL::ComPtr<ID3D12Fence> inputReady;
 VkSemaphore inputSemaphore{},outputSemaphore{};
 uint64_t inputValue{};
 bool gpuHandoff{};
 template<class T>T proc(const char* n){auto f=dev.proc<T>(n);if(!f)throw std::runtime_error(n);return f;}
 template<class T>T instanceProc(const char* n){auto f=reinterpret_cast<T>(dev.gipa(dev.instance,n));if(!f)throw std::runtime_error(n);return f;}
 static void check(VkResult r,const char* where){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(where)+" Vulkan="+std::to_string(r));}
 static void check(HRESULT r,const char* where){if(FAILED(r))throw std::runtime_error(std::string(where)+" HRESULT="+std::to_string(uint32_t(r)));}
 void initialize(const Device&);
 void make(SharedImage&,uint32_t,uint32_t,VkFormat,DXGI_FORMAT,VkImageUsageFlags,D3D12_RESOURCE_FLAGS);
 void destroy(SharedImage&);
 void beginVk(unsigned slot=0);
 void submitVk(uint32_t=0,const VkSemaphore* =nullptr,bool* consumed=nullptr);
 void submitInput(uint32_t,const VkSemaphore*,bool* consumed);
 void submitOutput();
 void submitNative(uint32_t,const VkSemaphore*,bool* consumed);
 void waitSlot(unsigned);
 void waitAll();
 void beginDx();void submitDx(bool wait=true);void waitDx();
 void barrier(VkCommandBuffer,VkImage,VkImageLayout,VkImageLayout,uint32_t=VK_QUEUE_FAMILY_IGNORED,uint32_t=VK_QUEUE_FAMILY_IGNORED,uint32_t layer=0);
 void destroyIdle();
private:
 void initializeHandoff();
 void submit(uint32_t,const VkSemaphore*,VkSemaphore,uint64_t,const uint64_t*,bool*,bool wait);
};
}
