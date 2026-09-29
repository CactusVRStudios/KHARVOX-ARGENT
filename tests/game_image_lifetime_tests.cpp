#include "../src/vulkan/GameImageRetirementDispatch.h"
#include <atomic>
#include <array>
#include <cassert>
#include <chrono>
#include <future>
#include <thread>

static std::atomic<bool> imageAlive{true}, copyComplete{false}, framebufferRetired{false};
static VkDevice expectedDevice=reinterpret_cast<VkDevice>(1);
static VkImage expectedImage=reinterpret_cast<VkImage>(2);
static VkImageView expectedView=reinterpret_cast<VkImageView>(3);
static VkAllocationCallbacks expectedAllocator{};
static unsigned viewsDestroyed{};
static void VKAPI_PTR destroyImage(VkDevice device,VkImage image,const VkAllocationCallbacks* allocator) {
    assert(device==expectedDevice && image==expectedImage && allocator==&expectedAllocator);
    assert(copyComplete && framebufferRetired);
    imageAlive=false;
}
static void VKAPI_PTR destroyView(VkDevice device,VkImageView view,const VkAllocationCallbacks* allocator) {
    assert(device==expectedDevice && view==expectedView && allocator==nullptr);
    ++viewsDestroyed;
}
int main() {
    auto& lifetime=kharvox::gameImageLifetime();
    const auto retireImage=reinterpret_cast<PFN_vkDestroyImage>(kharvox::GameImageRetirementEntry<VkImage>::wrap(reinterpret_cast<PFN_vkVoidFunction>(&destroyImage)));
    const auto retireView=reinterpret_cast<PFN_vkDestroyImageView>(kharvox::GameImageRetirementEntry<VkImageView>::wrap(reinterpret_cast<PFN_vkVoidFunction>(&destroyView)));
    std::promise<void> destructionEntered;
    auto entered=destructionEntered.get_future();
    std::future<void> retirement;
    {
        kharvox::GameImageLifetime::Use frame(lifetime);
        assert(frame.select([&]{return imageAlive.load();},[&]{return std::array{kharvox::GameImageLifetime::key(expectedImage)};}));
        // Unrelated runtime teardown must not block behind an XR frame.
        retireView(expectedDevice,expectedView,nullptr);
        assert(viewsDestroyed==1);
        retirement=std::async(std::launch::async,[&]{
            destructionEntered.set_value();
            retireImage(expectedDevice,expectedImage,&expectedAllocator);
        });
        entered.get();
        assert(retirement.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout);
        assert(imageAlive); // Source still valid while recording/submitting.
        copyComplete=true;
        framebufferRetired=true;
    }
    assert(retirement.wait_for(std::chrono::seconds(2))==std::future_status::ready);
    retirement.get();
    assert(!imageAlive);
    // A rebuild can complete before the next frame acquires the gate.
    imageAlive=true;
    { kharvox::GameImageLifetime::Use nextFrame(lifetime);
      assert(nextFrame.select([&]{return imageAlive.load();},[&]{return std::array{kharvox::GameImageLifetime::key(expectedImage)};})); }
    // A source cannot be selected after retirement starts but before its
    // tracker has been invalidated by the downstream destruction callback.
    std::promise<void> destroyStarted, allowDestruction;
    auto started=destroyStarted.get_future();auto allowed=allowDestruction.get_future();
    auto rebuilding=std::async(std::launch::async,[&]{
        lifetime.retire(kharvox::GameImageLifetime::key(expectedView),[&]{
            destroyStarted.set_value();allowed.wait();
        });
    });
    started.get();
    { kharvox::GameImageLifetime::Use frame(lifetime);
      assert(!frame.select([]{return true;},[&]{return std::array{kharvox::GameImageLifetime::key(expectedView)};})); }
    allowDestruction.set_value();rebuilding.get();
}
