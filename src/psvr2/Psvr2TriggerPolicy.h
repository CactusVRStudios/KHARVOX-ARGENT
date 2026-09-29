#pragma once

#include <array>
#include <cstdint>

#include "../weapon/LegacyHapticsWeaponKind.h"

namespace kharvox::psvr2 {

enum class TriggerEffect : std::uint8_t {
    Off = 0,
    Weapon = 1,
    Vibration = 2,
    Feedback = 3,
    SlopeFeedback = 4,
    MultiplePositionFeedback = 5,
    MultiplePositionVibration = 6,
};

enum class TriggerHand : std::uint8_t {
    Left = 0,
    Right = 1,
};

enum class TriggerOffReason : std::uint8_t {
    None = 0,
    Disabled,
    UnsupportedRuntime,
    Unfocused,
    Menu,
    Loading,
    Death,
    Cinematic,
    SessionEnd,
    UnsupportedWeapon,
    InvalidMessage,
};

struct TriggerCommand {
    TriggerEffect effect{TriggerEffect::Off};
    TriggerHand hand{TriggerHand::Right};
    std::uint8_t startPosition{};
    std::uint8_t endPosition{};
    std::uint8_t strength{};
    std::uint8_t position{};
    std::uint8_t amplitude{};
    std::uint8_t frequency{};
    std::uint8_t startStrength{};
    std::uint8_t endStrength{};
    std::array<std::uint8_t, 10> controlPoints{};
    TriggerOffReason offReason{TriggerOffReason::UnsupportedWeapon};
};

inline bool operator==(const TriggerCommand& left, const TriggerCommand& right) {
    return left.effect == right.effect && left.hand == right.hand
        && left.startPosition == right.startPosition
        && left.endPosition == right.endPosition
        && left.strength == right.strength
        && left.position == right.position
        && left.amplitude == right.amplitude
        && left.frequency == right.frequency
        && left.startStrength == right.startStrength
        && left.endStrength == right.endStrength
        && left.controlPoints == right.controlPoints
        && left.offReason == right.offReason;
}

inline bool operator!=(const TriggerCommand& left, const TriggerCommand& right) {
    return !(left == right);
}

struct TriggerPolicyInput {
    bool launcherEnabled{};
    bool steamVrCompatibleRuntime{};
    bool openXrFocused{};
    bool gameplayActive{};
    KharvoxWeaponKind weapon{KharvoxWeaponKind::Unknown};
    bool fireDown{};
    std::uint64_t fireHeldMilliseconds{};
    KharvoxWeaponAmmoState ammoState{KharvoxWeaponAmmoState::Unknown};
    bool leftHanded{};
    TriggerOffReason inactiveReason{TriggerOffReason::Loading};
};

struct ChaingunVibrationProfile {
    std::uint8_t amplitude{};
    std::uint8_t frequency{};
};

inline ChaingunVibrationProfile chaingunVibrationProfile(
    std::uint64_t fireHeldMilliseconds) {
    // Quantized steps track the weapon's spin-up without issuing a different
    // Toolkit command every OpenXR frame. The final stage is the approved
    // full-speed 8/55 profile.
    if (fireHeldMilliseconds < 200) return {6, 25};
    if (fireHeldMilliseconds < 450) return {7, 35};
    if (fireHeldMilliseconds < 750) return {8, 45};
    return {8, 55};
}

inline TriggerCommand offCommand(
    bool leftHanded, TriggerOffReason reason) {
    TriggerCommand command{};
    command.hand = leftHanded ? TriggerHand::Left : TriggerHand::Right;
    command.offReason = reason;
    return command;
}

inline TriggerCommand weaponCommand(
    bool leftHanded, std::uint8_t startPosition,
    std::uint8_t endPosition, std::uint8_t strength) {
    TriggerCommand command{};
    command.effect = TriggerEffect::Weapon;
    command.hand = leftHanded ? TriggerHand::Left : TriggerHand::Right;
    command.startPosition = startPosition;
    command.endPosition = endPosition;
    command.strength = strength;
    command.offReason = TriggerOffReason::None;
    return command;
}

inline TriggerCommand vibrationCommand(
    bool leftHanded, std::uint8_t position,
    std::uint8_t amplitude, std::uint8_t frequency) {
    TriggerCommand command{};
    command.effect = TriggerEffect::Vibration;
    command.hand = leftHanded ? TriggerHand::Left : TriggerHand::Right;
    command.position = position;
    command.amplitude = amplitude;
    command.frequency = frequency;
    command.offReason = TriggerOffReason::None;
    return command;
}

inline TriggerCommand feedbackCommand(
    bool leftHanded, std::uint8_t position, std::uint8_t strength) {
    TriggerCommand command{};
    command.effect = TriggerEffect::Feedback;
    command.hand = leftHanded ? TriggerHand::Left : TriggerHand::Right;
    command.position = position;
    command.strength = strength;
    command.offReason = TriggerOffReason::None;
    return command;
}

inline TriggerCommand slopeFeedbackCommand(
    bool leftHanded, std::uint8_t startPosition,
    std::uint8_t endPosition, std::uint8_t startStrength,
    std::uint8_t endStrength) {
    TriggerCommand command{};
    command.effect = TriggerEffect::SlopeFeedback;
    command.hand = leftHanded ? TriggerHand::Left : TriggerHand::Right;
    command.startPosition = startPosition;
    command.endPosition = endPosition;
    command.startStrength = startStrength;
    command.endStrength = endStrength;
    command.offReason = TriggerOffReason::None;
    return command;
}

inline TriggerCommand multiplePositionFeedbackCommand(
    bool leftHanded, const std::array<std::uint8_t, 10>& strengths) {
    TriggerCommand command{};
    command.effect = TriggerEffect::MultiplePositionFeedback;
    command.hand = leftHanded ? TriggerHand::Left : TriggerHand::Right;
    command.controlPoints = strengths;
    command.offReason = TriggerOffReason::None;
    return command;
}

inline TriggerCommand multiplePositionVibrationCommand(
    bool leftHanded, std::uint8_t frequency,
    const std::array<std::uint8_t, 10>& amplitudes) {
    TriggerCommand command{};
    command.effect = TriggerEffect::MultiplePositionVibration;
    command.hand = leftHanded ? TriggerHand::Left : TriggerHand::Right;
    command.frequency = frequency;
    command.controlPoints = amplitudes;
    command.offReason = TriggerOffReason::None;
    return command;
}

constexpr std::array<std::uint8_t, 10> superShotgunFeedbackPoints{
    0, 0, 3, 6, 3, 3, 6, 8, 8, 8};
constexpr std::array<std::uint8_t, 10> chainsawVibrationPoints{
    0, 0, 2, 3, 4, 5, 6, 7, 8, 8};

inline TriggerCommand selectTriggerCommand(const TriggerPolicyInput& input) {
    if (!input.launcherEnabled)
        return offCommand(input.leftHanded, TriggerOffReason::Disabled);
    if (!input.steamVrCompatibleRuntime)
        return offCommand(input.leftHanded, TriggerOffReason::UnsupportedRuntime);
    if (!input.openXrFocused)
        return offCommand(input.leftHanded, TriggerOffReason::Unfocused);
    if (!input.gameplayActive)
        return offCommand(input.leftHanded, input.inactiveReason);

    const bool ammoBlocksFireVibration =
        input.ammoState == KharvoxWeaponAmmoState::Empty
        || input.ammoState == KharvoxWeaponAmmoState::Unavailable;
    if (input.fireDown && !ammoBlocksFireVibration) {
        switch (input.weapon) {
        case KharvoxWeaponKind::HeavyAssaultRifle:
        case KharvoxWeaponKind::AssaultRifle:
            return vibrationCommand(input.leftHanded, 3, 7, 40);
        case KharvoxWeaponKind::Chaingun: {
            const auto profile=chaingunVibrationProfile(
                input.fireHeldMilliseconds);
            return vibrationCommand(input.leftHanded, 2,
                profile.amplitude,profile.frequency);
        }
        case KharvoxWeaponKind::PlasmaRifle:
            return vibrationCommand(input.leftHanded, 4, 3, 60);
        case KharvoxWeaponKind::Chainsaw:
            return multiplePositionVibrationCommand(input.leftHanded, 45,
                chainsawVibrationPoints);
        default:
            break;
        }
    }

    switch (input.weapon) {
    case KharvoxWeaponKind::Pistol:
        return weaponCommand(input.leftHanded, 4, 6, 3);

    case KharvoxWeaponKind::PlasmaRifle:
        return feedbackCommand(input.leftHanded, 4, 3);

    case KharvoxWeaponKind::HeavyAssaultRifle:
    case KharvoxWeaponKind::AssaultRifle:
    case KharvoxWeaponKind::Chaingun:
    case KharvoxWeaponKind::ArcCannon:
        return weaponCommand(input.leftHanded, 3, 5, 5);

    case KharvoxWeaponKind::GaussCannon:
        return slopeFeedbackCommand(input.leftHanded, 2, 8, 2, 7);

    case KharvoxWeaponKind::Shotgun:
    case KharvoxWeaponKind::SuperShotgun:
    case KharvoxWeaponKind::RocketLauncher:
    case KharvoxWeaponKind::MancubusGland:
        return weaponCommand(input.leftHanded, 2, 4, 7);

    case KharvoxWeaponKind::Bfg:
        return slopeFeedbackCommand(input.leftHanded, 2, 8, 3, 8);

    case KharvoxWeaponKind::Unknown:
    case KharvoxWeaponKind::Fists:
    case KharvoxWeaponKind::Count:
        return offCommand(input.leftHanded, TriggerOffReason::UnsupportedWeapon);
    }
    return offCommand(input.leftHanded, TriggerOffReason::UnsupportedWeapon);
}

inline const char* triggerHandName(TriggerHand hand) {
    return hand == TriggerHand::Left ? "left" : "right";
}

inline const char* triggerProfileName(const TriggerCommand& command) {
    if (command.effect == TriggerEffect::Vibration)
        return "vibration";
    if (command.effect == TriggerEffect::Feedback)
        return "feedback";
    if (command.effect == TriggerEffect::SlopeFeedback)
        return "slope-feedback";
    if (command.effect == TriggerEffect::MultiplePositionFeedback)
        return "multiple-position-feedback";
    if (command.effect == TriggerEffect::MultiplePositionVibration)
        return "multiple-position-vibration";
    if (command.effect != TriggerEffect::Weapon)
        return "off";
    if (command.startPosition == 4 && command.endPosition == 6
        && command.strength == 3)
        return "light";
    if (command.startPosition == 3 && command.endPosition == 5
        && command.strength == 5)
        return "medium";
    if (command.startPosition == 2 && command.endPosition == 4
        && command.strength == 7)
        return "heavy";
    return "custom";
}

inline const char* triggerOffReasonName(TriggerOffReason reason) {
    switch (reason) {
    case TriggerOffReason::None: return "none";
    case TriggerOffReason::Disabled: return "disabled";
    case TriggerOffReason::UnsupportedRuntime: return "unsupported-runtime";
    case TriggerOffReason::Unfocused: return "focus-lost";
    case TriggerOffReason::Menu: return "menu";
    case TriggerOffReason::Loading: return "loading";
    case TriggerOffReason::Death: return "death";
    case TriggerOffReason::Cinematic: return "cinematic";
    case TriggerOffReason::SessionEnd: return "session-end";
    case TriggerOffReason::UnsupportedWeapon: return "unsupported-weapon";
    case TriggerOffReason::InvalidMessage: return "invalid-message";
    }
    return "unknown";
}

inline bool knownTriggerOffReason(TriggerOffReason reason) {
    switch (reason) {
    case TriggerOffReason::None:
    case TriggerOffReason::Disabled:
    case TriggerOffReason::UnsupportedRuntime:
    case TriggerOffReason::Unfocused:
    case TriggerOffReason::Menu:
    case TriggerOffReason::Loading:
    case TriggerOffReason::Death:
    case TriggerOffReason::Cinematic:
    case TriggerOffReason::SessionEnd:
    case TriggerOffReason::UnsupportedWeapon:
    case TriggerOffReason::InvalidMessage:
        return true;
    }
    return false;
}

inline bool validTriggerCommand(const TriggerCommand& command) {
    const std::array<std::uint8_t, 10> emptyPoints{};
    if (command.hand != TriggerHand::Left && command.hand != TriggerHand::Right)
        return false;
    if (command.effect == TriggerEffect::Off)
        return command.startPosition == 0 && command.endPosition == 0
            && command.strength == 0
            && command.position == 0 && command.amplitude == 0
            && command.frequency == 0
            && command.startStrength == 0 && command.endStrength == 0
            && command.controlPoints == emptyPoints
            && knownTriggerOffReason(command.offReason)
            && command.offReason != TriggerOffReason::None;
    if (command.offReason != TriggerOffReason::None)
        return false;
    if (command.effect == TriggerEffect::Weapon)
        return command.position == 0 && command.amplitude == 0
            && command.frequency == 0
            && command.startStrength == 0 && command.endStrength == 0
            && command.controlPoints == emptyPoints
            && command.startPosition >= 2 && command.startPosition <= 7
            && command.endPosition > command.startPosition
            && command.endPosition <= 8
            && command.strength >= 1 && command.strength <= 8;
    if (command.effect == TriggerEffect::Vibration)
        return command.startPosition == 0 && command.endPosition == 0
            && command.strength == 0
            && command.startStrength == 0 && command.endStrength == 0
            && command.controlPoints == emptyPoints
            && command.position <= 9
            && command.amplitude >= 1 && command.amplitude <= 8
            && command.frequency >= 1;
    if (command.effect == TriggerEffect::Feedback)
        return command.startPosition == 0 && command.endPosition == 0
            && command.amplitude == 0 && command.frequency == 0
            && command.startStrength == 0 && command.endStrength == 0
            && command.controlPoints == emptyPoints
            && command.position <= 9
            && command.strength >= 1 && command.strength <= 8;
    if (command.effect == TriggerEffect::SlopeFeedback)
        return command.strength == 0 && command.position == 0
            && command.amplitude == 0 && command.frequency == 0
            && command.controlPoints == emptyPoints
            && command.startPosition < command.endPosition
            && command.endPosition <= 9
            && command.startStrength >= 1 && command.startStrength <= 8
            && command.endStrength >= 1 && command.endStrength <= 8;
    if (command.effect == TriggerEffect::MultiplePositionFeedback
        || command.effect == TriggerEffect::MultiplePositionVibration) {
        bool any{};
        for (const auto point : command.controlPoints) {
            if (point > 8) return false;
            any = any || point != 0;
        }
        if (!any || command.startPosition != 0 || command.endPosition != 0
            || command.strength != 0 || command.position != 0
            || command.amplitude != 0 || command.startStrength != 0
            || command.endStrength != 0)
            return false;
        return command.effect == TriggerEffect::MultiplePositionVibration
            ? command.frequency >= 1
            : command.frequency == 0;
    }
    return false;
}

} // namespace kharvox::psvr2

