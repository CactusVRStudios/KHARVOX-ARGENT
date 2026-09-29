#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kharvox {

inline bool motionWheelStickBypass(float x, float y, float deadzone = 0.30f) {
    return x*x + y*y > deadzone*deadzone;
}

inline int weaponWheelSector(float x, float y, int requestedSectorCount = 8) {
    if (!std::isfinite(x) || !std::isfinite(y)) return -1;
    // Angle in range [0, 2*PI)
    constexpr float twoPi = 6.28318530717958647692f;
    float angle = std::atan2(y, x);
    if (angle < 0.0f) {
        angle += twoPi;
    }

    const int sectorCount = std::max(1, requestedSectorCount);
    const float sectorSpan = twoPi / static_cast<float>(sectorCount);
    const float halfSector = sectorSpan * 0.5f;

    float centeredAngle = angle + halfSector;
    while (centeredAngle >= twoPi) centeredAngle -= twoPi;

    return static_cast<int>(centeredAngle / sectorSpan) % sectorCount;
}

struct WeaponWheelStickHapticState {
    int lastSector{-1};
};

inline bool updateWeaponWheelStickHaptics(WeaponWheelStickHapticState& state,
    float x, float y, bool ownsSelection) {
    const int sector = ownsSelection && motionWheelStickBypass(x, y)
        ? weaponWheelSector(x, y) : -1;
    const bool click = sector >= 0 && sector != state.lastSector;
    state.lastSector = sector;
    return click;
}

inline int weaponWheelHapticHand(bool stickOwnsSelection, bool leftHanded,
    bool swappedSticks) {
    return stickOwnsSelection ? (swappedSticks ? 1 : 0) : (leftHanded ? 0 : 1);
}

struct MotionWeaponWheelVec3 {
    float x{};
    float y{};
    float z{};
};

struct MotionWeaponWheelQuat {
    float x{};
    float y{};
    float z{};
    float w{1.0f};
};

struct MotionWeaponWheelConfig {
    bool enabled{true};
    bool hapticsEnabled{true};
    float deadzoneMeters{0.02f};    // 2.0 cm deadzone from origin before activating stick
    float maxRadiusMeters{0.065f};  // 6.5 cm reach for 100% stick deflection
    float nativeStickDeadzone{0.15f};
    int sectorCount{8};             // 8 radial weapon slots (45 degrees per slot)
};

struct MotionWeaponWheelInput {
    MotionWeaponWheelConfig config{};
    bool wheelActive{};
    bool stickBypass{};
    bool trackingValid{};
    MotionWeaponWheelVec3 handPosition{};
    MotionWeaponWheelQuat hmdOrientation{};
    std::uint64_t nowNanoseconds{};
};

struct MotionWeaponWheelState {
    bool wasActive{};
    bool anchorValid{};
    bool stickWasActive{};
    MotionWeaponWheelVec3 originHandPosition{};
    int lastSelectedSector{-1};
};

struct MotionWeaponWheelOutput {
    float stickX{};
    float stickY{};
    bool stickActive{};
    bool stickBypassActive{};
    bool triggerHapticPulse{};
    int selectedSector{-1};
};

inline MotionWeaponWheelVec3 rotateVectorByQuat(const MotionWeaponWheelQuat& q, const MotionWeaponWheelVec3& v) {
    // Rodriguez quaternion rotation formula: v' = v + w * t + (q_xyz x t), where t = 2 * (q_xyz x v)
    const float tx = 2.0f * (q.y * v.z - q.z * v.y);
    const float ty = 2.0f * (q.z * v.x - q.x * v.z);
    const float tz = 2.0f * (q.x * v.y - q.y * v.x);

    const float cx = q.y * tz - q.z * ty;
    const float cy = q.z * tx - q.x * tz;
    const float cz = q.x * ty - q.y * tx;

    return {
        v.x + q.w * tx + cx,
        v.y + q.w * ty + cy,
        v.z + q.w * tz + cz
    };
}

inline MotionWeaponWheelOutput updateMotionWeaponWheel(
    MotionWeaponWheelState& state,
    const MotionWeaponWheelInput& input) {

    MotionWeaponWheelOutput output{};

    if (!input.config.enabled || !input.wheelActive) {
        state = {};
        return output;
    }

    // Stick priority is instantaneous. Preserve the wheel-opening anchor while
    // using the stick, but still invalidate/reacquire it on tracking loss.
    if (!state.wasActive) {
        state = {};
        state.wasActive = true;
    }
    state.stickWasActive = input.stickBypass;
    output.stickBypassActive = input.stickBypass;

    const auto& p = input.handPosition;
    const auto& q = input.hmdOrientation;
    const float qLength = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (!input.trackingValid || !std::isfinite(p.x) || !std::isfinite(p.y)
        || !std::isfinite(p.z) || !std::isfinite(qLength)
        || qLength < 0.5f || qLength > 1.5f) {
        state.anchorValid = false;
        state.lastSelectedSector = -1;
        return output;
    }
    // Reacquire at the current hand position after any invalid tracking frame.
    if (!state.anchorValid) {
        state.anchorValid = true;
        state.originHandPosition = input.handPosition;
        state.lastSelectedSector = -1;
        return output;
    }

    if (input.stickBypass) {
        state.lastSelectedSector = -1;
        return output;
    }

    // OpenXR coordinate system: +X Right, +Y Up, -Z Forward
    const MotionWeaponWheelVec3 hmdRight = rotateVectorByQuat(input.hmdOrientation, {1.0f, 0.0f, 0.0f});
    const MotionWeaponWheelVec3 hmdUp = rotateVectorByQuat(input.hmdOrientation, {0.0f, 1.0f, 0.0f});

    const MotionWeaponWheelVec3 delta{
        input.handPosition.x - state.originHandPosition.x,
        input.handPosition.y - state.originHandPosition.y,
        input.handPosition.z - state.originHandPosition.z
    };

    // Project 3D displacement onto 2D HMD View Plane
    const float screenX = delta.x * hmdRight.x + delta.y * hmdRight.y + delta.z * hmdRight.z;
    const float screenY = delta.x * hmdUp.x + delta.y * hmdUp.y + delta.z * hmdUp.z;

    const float distance = std::sqrt(screenX * screenX + screenY * screenY);

    if (distance <= input.config.deadzoneMeters) {
        state.lastSelectedSector = -1;
        output.stickX = 0.0f;
        output.stickY = 0.0f;
        output.stickActive = false;
        output.selectedSector = -1;
        output.triggerHapticPulse = false;
        return output;
    }

    const float span = std::max(0.001f, input.config.maxRadiusMeters - input.config.deadzoneMeters);
    const float scaled = std::clamp((distance - input.config.deadzoneMeters) / span, 0.0f, 1.0f);
    // A sector click must correspond to an input DOOM can actually select.
    const float floor = std::clamp(input.config.nativeStickDeadzone + 0.01f, 0.0f, 1.0f);
    const float intensity = floor + scaled * (1.0f - floor);
    const float dirX = screenX / distance;
    const float dirY = screenY / distance;

    output.stickX = dirX * intensity;
    output.stickY = dirY * intensity;
    output.stickActive = true;

    const int sector = weaponWheelSector(dirX, dirY, input.config.sectorCount);
    output.selectedSector = sector;

    if (input.config.hapticsEnabled && sector != state.lastSelectedSector) {
        output.triggerHapticPulse = true;
    }
    state.lastSelectedSector = sector;

    return output;
}

} // namespace kharvox
