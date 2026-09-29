#pragma once

#include <cmath>

namespace kharvox {

inline bool horizontalHeadingFromBasis(
    const float axis[9], float& forwardX, float& forwardY) noexcept {
    if (!axis || !std::isfinite(axis[0]) || !std::isfinite(axis[1])) return false;
    const float length = std::hypot(axis[0], axis[1]);
    if (!std::isfinite(length) || length <= 0.001f) return false;
    forwardX = axis[0] / length;
    forwardY = axis[1] / length;
    return true;
}

inline bool makeGravityLevelBodyBasis(
    const float sourceAxis[9], const float fallbackAxis[9],
    float outputAxis[9]) noexcept {
    if (!sourceAxis || !outputAxis) return false;

    float forwardX{};
    float forwardY{};
    if (!horizontalHeadingFromBasis(sourceAxis, forwardX, forwardY)
        && !horizontalHeadingFromBasis(fallbackAxis, forwardX, forwardY))
        return false;

    // DOOM uses a Z-up world basis. Preserve only the native horizontal
    // heading; pitch and roll belong to OpenXR head tracking in VR.
    outputAxis[0] = forwardX;
    outputAxis[1] = forwardY;
    outputAxis[2] = 0.0f;
    outputAxis[3] = -forwardY;
    outputAxis[4] = forwardX;
    outputAxis[5] = 0.0f;
    outputAxis[6] = 0.0f;
    outputAxis[7] = 0.0f;
    outputAxis[8] = 1.0f;
    return true;
}

inline bool makeStableGameplayBodyBasis(
    bool gameplayCamera, bool animatedSequenceActive,
    const float sourceAxis[9], const float fallbackAxis[9],
    float outputAxis[9]) noexcept {
    // A VR gameplay body contributes yaw only. Native pitch/roll from recoil,
    // melee or a just-finished animation must not become a persistent world
    // tilt; physical HMD pitch/roll is applied later in the render path.
    // An explicitly classified animated sequence keeps its authored camera
    // motion until control returns to gameplay.
    return gameplayCamera && !animatedSequenceActive
        && makeGravityLevelBodyBasis(sourceAxis, fallbackAxis, outputAxis);
}

} // namespace kharvox
