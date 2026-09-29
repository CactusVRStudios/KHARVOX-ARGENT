#pragma once

#include <cmath>
#include <algorithm>

namespace kharvox {

enum class MovementDirectionMode { Head, OffHand };
enum class MovementDirectionHand { Left, Right };

struct MovementDirectionYaw {
    float degrees{};
    bool usedOffHand{};
};

struct MovementStick2D {
    float x{};
    float y{};
};

// Beta 0.2.2 experiment: preserve the lower half of analog travel, then
// scale the vector radially to full speed at 85%, independent of direction.
inline MovementStick2D fullTravelMovementStick(MovementStick2D stick) {
    if(!std::isfinite(stick.x)||!std::isfinite(stick.y))return {};
    const float magnitude=std::hypot(stick.x,stick.y);
    if(magnitude<=.5f)return stick;
    const float target=std::min(1.f,.5f+(magnitude-.5f)*(.5f/.35f));
    return {stick.x*target/magnitude,stick.y*target/magnitude};
}

inline MovementDirectionHand offHandForMovement(bool leftHanded) {
    return leftHanded ? MovementDirectionHand::Right
                      : MovementDirectionHand::Left;
}

inline float wrapMovementDegrees(float value) {
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

inline MovementDirectionYaw resolveMovementDirectionYaw(
    MovementDirectionMode mode, float headResidualYaw,
    bool offHandTrackingValid, float offHandForwardX, float offHandForwardZ,
    float acceptedPhysicalYaw, float artificialTurnVisualYaw) {
    const float safeHeadYaw = std::isfinite(headResidualYaw)
        ? wrapMovementDegrees(headResidualYaw) : 0.0f;
    constexpr float minimumHorizontalForwardSquared = 0.04f;
    const float horizontalSquared = offHandForwardX * offHandForwardX
        + offHandForwardZ * offHandForwardZ;
    if (mode != MovementDirectionMode::OffHand || !offHandTrackingValid
        || !std::isfinite(offHandForwardX) || !std::isfinite(offHandForwardZ)
        || !std::isfinite(acceptedPhysicalYaw)
        || !std::isfinite(artificialTurnVisualYaw)
        || horizontalSquared < minimumHorizontalForwardSquared)
        return {safeHeadYaw, false};

    constexpr float radiansToDegrees = 57.295779513082320876f;
    // OpenXR aim poses point along -Z. Project that ray onto the horizon so
    // pitching or rolling the controller never tilts locomotion vertically.
    const float trackingYaw = std::atan2(-offHandForwardX, -offHandForwardZ)
        * radiansToDegrees;
    return {wrapMovementDegrees(trackingYaw - acceptedPhysicalYaw
        + artificialTurnVisualYaw), true};
}

inline MovementStick2D rotateMovementStickForDirection(
    MovementStick2D stick, float residualYawDegrees) {
    if (!std::isfinite(stick.x) || !std::isfinite(stick.y)
        || !std::isfinite(residualYawDegrees))
        return {};
    constexpr float degreesToRadians = 0.01745329251994329577f;
    const float yaw = -residualYawDegrees * degreesToRadians;
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    return {
        cosine * stick.x + sine * stick.y,
        cosine * stick.y - sine * stick.x
    };
}

} // namespace kharvox
