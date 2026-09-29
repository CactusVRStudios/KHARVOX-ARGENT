#pragma once

#include <cmath>
#include <cstdint>

namespace kharvox {

inline bool snapTurnMayActivateAtStereoBoundary(
    bool alternatingStereoActive, bool nativePackedStereo,
    bool pipelineWarmupActive, int currentRenderEye) {
    if (!alternatingStereoActive || nativePackedStereo) return true;
    return !pipelineWarmupActive && currentRenderEye == 1;
}

struct SnapTurnEyeCompensation {
    bool apply{};
    float degrees{};
};

struct SnapTurnYawState {
    float acceptedPhysicalYaw{};
    float accumulatedArtificialTurn{};
};

inline float wrapSnapTurnDegrees(float value) {
    while (value > 180.f) value -= 360.f;
    while (value < -180.f) value += 360.f;
    return value;
}

inline SnapTurnYawState applySnapTurnToPhysicalResidual(
    float acceptedPhysicalYaw, float accumulatedArtificialTurn,
    float snapDegrees) {
    if (!std::isfinite(acceptedPhysicalYaw)
        || !std::isfinite(accumulatedArtificialTurn)
        || !std::isfinite(snapDegrees))
        return {acceptedPhysicalYaw, accumulatedArtificialTurn};
    // physicalResidual = trackedHeadYaw - acceptedPhysicalYaw. Subtracting the
    // snap therefore applies it instantly without a multi-frame artificial-yaw
    // carrier. The proven physical body-follow path can reconcile DOOM's body.
    return {
        wrapSnapTurnDegrees(acceptedPhysicalYaw - snapDegrees),
        wrapSnapTurnDegrees(accumulatedArtificialTurn + snapDegrees)};
}

inline SnapTurnEyeCompensation snapTurnEyeCompensation(
    bool transitionActive, bool nativePackedStereo, bool cachedEyeValid,
    std::uint64_t currentGeneration, std::uint64_t cachedGeneration,
    float currentArtificialTurnDegrees, float cachedArtificialTurnDegrees) {
    if (!transitionActive || nativePackedStereo || !cachedEyeValid
        || cachedGeneration == currentGeneration
        || !std::isfinite(currentArtificialTurnDegrees)
        || !std::isfinite(cachedArtificialTurnDegrees))
        return {};
    return {true, wrapSnapTurnDegrees(
        cachedArtificialTurnDegrees - currentArtificialTurnDegrees)};
}

inline bool snapTurnStereoTransitionComplete(
    bool transitionActive, std::uint64_t currentGeneration,
    bool leftEyeValid, std::uint64_t leftGeneration,
    bool rightEyeValid, std::uint64_t rightGeneration) {
    return transitionActive && leftEyeValid && rightEyeValid
        && leftGeneration == currentGeneration
        && rightGeneration == currentGeneration;
}

} // namespace kharvox
