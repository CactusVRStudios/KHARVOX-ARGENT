#pragma once

#include "HandWeaponProfile.h"

#include <cstddef>
#include <string>

namespace kharvox::hands {

enum class CalibrationMode { None, Rotation, Position };
inline std::string handPoseKey(HandWeaponKind weapon,bool modeLeft,bool physicalLeft){
    return std::string(HandWeaponKindKey(weapon))+(modeLeft?"_mode_left":"_mode_right")+(physicalLeft?"_hand_left":"_hand_right");
}

// High-bit edge tracking, not GetAsyncKeyState's shared/consumable low bit.
// A held Num + toggles only once even when polled for both rendered eyes.
inline CalibrationMode updateHandCalibrationMode(CalibrationMode mode,
    bool plusDown, bool& previousPlusDown) {
    const bool pressed = plusDown && !previousPlusDown;
    previousPlusDown = plusDown;
    if (!pressed || mode == CalibrationMode::None) return mode;
    return mode == CalibrationMode::Rotation
        ? CalibrationMode::Position : CalibrationMode::Rotation;
}

struct HandCalibrationProfileSelection {
    bool weaponSpecific{};
    std::size_t handIndex{}; // 0=physical left, 1=physical right
    std::size_t weaponIndex{};
};

inline bool handCalibrationWeaponValid(HandWeaponKind weapon) {
    const auto value = static_cast<int>(weapon);
    return value > static_cast<int>(HandWeaponKind::Unknown) &&
           value < static_cast<int>(HandWeaponKind::Count);
}

inline HandCalibrationProfileSelection selectHandCalibrationProfile(
    bool physicalLeft, bool leftHanded, HandWeaponKind weapon,
    bool gunHoldingPose) {
    return {gunHoldingPose && physicalLeft == leftHanded &&
                handCalibrationWeaponValid(weapon),
            physicalLeft ? 0u : 1u,
            handCalibrationWeaponValid(weapon)
                ? static_cast<std::size_t>(weapon)
                : 0u};
}

} // namespace kharvox::hands
