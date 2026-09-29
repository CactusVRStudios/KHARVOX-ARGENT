#pragma once

#include "HandVisibilityPolicy.h"
#include "HandSceneTarget.h"
#include "HandHudMask.h"
#include "HandWeaponProfile.h"
#include <vulkan/vulkan.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

struct KharvoxVulkanDispatch;
namespace argent::calibration {struct ApplyCommand;}

namespace kharvox::hands {

struct HandPose {
    float position[3]{};
    float orientation[4]{0.f, 0.f, 0.f, 1.f};
    bool valid{};
};

struct HandEyeView {
    HandPose pose{};
    float angleLeft{};
    float angleRight{};
    float angleUp{};
    float angleDown{};
    // The projection layer may submit only a centered sub-rectangle of the
    // OpenXR swapchain.  Rendering into the full image would shift and scale
    // the hand differently in each eye when that rectangle is sampled.
    std::int32_t imageRectX{};
    std::int32_t imageRectY{};
    std::uint32_t imageRectWidth{};
    std::uint32_t imageRectHeight{};
    // Projection range in metres. The normal OpenXR overlay uses these
    // defaults; the DOOM scene-depth path supplies the native scene range.
    float nearZ{0.02f};
    float farZ{100.0f};
    bool nativeDepth{};float depthA{},depthB{};
    HandHudPanels hudPanels;
    HandHudPanels hudPlaceholder;
};

struct HandCalibration {
    float position[3]{};
    float rotationDegrees[3]{};
    float scale{0.4f};
};

struct HandGameplayState {
    HandWeaponKind weapon{HandWeaponKind::Unknown};
    bool leftHanded{};
};

using HandLog = std::function<void(const std::string&)>;

class HandRenderer {
public:
    HandRenderer();
    ~HandRenderer();
    HandRenderer(const HandRenderer&) = delete;
    HandRenderer& operator=(const HandRenderer&) = delete;

    bool initialize(VkPhysicalDevice physicalDevice, VkDevice device,
        VkQueue queue, std::uint32_t queueFamily,
        const KharvoxVulkanDispatch& dispatch, VkFormat colorFormat,
        const std::array<VkExtent2D, 2>& eyeExtents,
        const std::array<std::vector<VkImage>, 2>& eyeImages,
        const std::wstring& runtimeDirectory, HandLog log, bool arrayStereo = false, bool sceneDepthOnly = false);
    void shutdown();
    void setCalibrationEnabled(bool enabled);
    void configureCalibration(HandWeaponKind profile,bool leftHanded,bool enabled,const argent::calibration::ApplyCommand& command);

    HandAssetAvailability availability() const;
    const HandCalibration& leftCalibration() const;
    const HandCalibration& rightCalibration() const;

    void record(VkCommandBuffer commandBuffer, std::uint32_t eye,
        std::uint32_t imageIndex, const HandEyeView& view,
        const HandPose& leftGrip, const HandPose& rightGrip,
        const HandVisibilityOutput& visibility,
        const HandGameplayState& gameplay);

    // Records into the completed native DOOM color image while borrowing its
    // depth attachment. Returns false without changing ownership when that
    // render target cannot be used safely, allowing the caller to retain the
    // isolated overlay fallback.
    bool recordSceneIntegrated(VkCommandBuffer commandBuffer,
        const HandSceneTarget& target, const HandEyeView& view,
        const HandPose& leftGrip, const HandPose& rightGrip,
        const HandVisibilityOutput& visibility,
        const HandGameplayState& gameplay, const HandPose& laser = {});
    bool recordSceneDepth(VkCommandBuffer commandBuffer,uint32_t eye,uint32_t imageIndex,
        HandSceneTarget target,const HandEyeView& view,const HandPose& left,const HandPose& right,
        const HandVisibilityOutput& visibility,const HandGameplayState& gameplay,const HandPose& laser = {});
    void finishSceneIntegratedFrame();

private:
    struct Impl;
    Impl* impl_{};
};

} // namespace kharvox::hands
