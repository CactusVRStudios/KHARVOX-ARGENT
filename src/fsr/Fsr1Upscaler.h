#pragma once

#include "FsrDispatch.h"
#include <cstdint>

struct Fsr1SourceRect {
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

// Optional spatial upscaler shared by AER and Native Stereo. Every
// method fails closed so the caller can retain its existing linear blit.
class Fsr1Upscaler {
public:
    Fsr1Upscaler();
    ~Fsr1Upscaler();
    Fsr1Upscaler(const Fsr1Upscaler&) = delete;
    Fsr1Upscaler& operator=(const Fsr1Upscaler&) = delete;

    bool initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    const FsrDispatch& dispatch,
                    VkFormat sourceFormat, VkExtent2D sourceExtent,
                    VkExtent2D maximumOutputExtent);
    bool active() const;
    // Caller must prove all submissions using these images have completed.
    void releaseAfterCompletion();
    void discardRecordedFrame();

    // Copies one current cached eye, performs official FSR 1 EASU followed by
    // RCAS, and returns a transfer-source image of outputExtent.
    VkImage record(VkCommandBuffer commandBuffer, VkImage source, int eye,
                   std::uint64_t sourceRevision, Fsr1SourceRect sourceRect,
                   VkExtent2D outputExtent, uint32_t sourceLayer=0);

private:
    struct Impl;
    Impl* impl_{};
};
