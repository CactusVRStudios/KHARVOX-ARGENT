#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kharvox {

constexpr float xinputHapticMotorMaximum = 65535.0f;
constexpr float xinputHapticDeadzone = 0.04f;
constexpr float xinputHapticMinimumAmplitude = 0.0f;
constexpr float xinputHapticMaximumAmplitude = 1.0f;
constexpr float xinputHapticLowFrequencyHz = 60.0f;
constexpr float xinputHapticHighFrequencyHz = 220.0f;
constexpr float xinputHapticMinimumFrequencyHz = 40.0f;
constexpr float xinputHapticMaximumFrequencyHz = 320.0f;
constexpr float xinputHapticAmplitudeChangeThreshold = 0.02f;
constexpr float xinputHapticFrequencyChangeThresholdHz = 4.0f;
constexpr std::uint64_t xinputHapticRefreshIntervalMilliseconds = 75;
constexpr std::uint64_t xinputHapticPulseDurationMilliseconds = 100;
constexpr std::uint64_t xinputHapticErrorRetryMilliseconds = 500;
constexpr std::uint64_t xinputHapticTransientHoldMilliseconds = 45;
constexpr std::uint64_t weaponFireHapticFallbackHoldMilliseconds = 60;
constexpr float weaponFireHapticFallbackAmplitude = 0.45f;
constexpr float weaponFireHapticFallbackFrequencyHz = 160.0f;

inline std::uint32_t packXInputRumble(
    std::uint16_t lowMotorSpeed, std::uint16_t highMotorSpeed) {
    return static_cast<std::uint32_t>(lowMotorSpeed)
        | (static_cast<std::uint32_t>(highMotorSpeed) << 16);
}

inline std::uint16_t xinputRumbleLowMotor(std::uint32_t packed) {
    return static_cast<std::uint16_t>(packed & 0xffffu);
}

inline std::uint16_t xinputRumbleHighMotor(std::uint32_t packed) {
    return static_cast<std::uint16_t>(packed >> 16);
}

inline std::uint32_t mergeXInputRumblePeaks(
    std::uint32_t left, std::uint32_t right) {
    return packXInputRumble(
        std::max(xinputRumbleLowMotor(left), xinputRumbleLowMotor(right)),
        std::max(xinputRumbleHighMotor(left), xinputRumbleHighMotor(right)));
}

struct XInputRumbleFrameAccumulator {
    std::uint32_t heldTransient{};
    std::uint64_t holdUntilMilliseconds{};
};

inline std::uint32_t selectXInputRumbleForFrame(
    XInputRumbleFrameAccumulator& accumulator,
    std::uint32_t current,
    std::uint32_t pendingPeak,
    std::uint64_t nowMilliseconds) {
    if (pendingPeak != 0) {
        accumulator.heldTransient = pendingPeak;
        accumulator.holdUntilMilliseconds = nowMilliseconds
            + xinputHapticTransientHoldMilliseconds;
    }
    if (accumulator.heldTransient != 0
        && nowMilliseconds >= accumulator.holdUntilMilliseconds) {
        accumulator = {};
    }
    return mergeXInputRumblePeaks(current, accumulator.heldTransient);
}

inline void resetXInputRumbleFrameAccumulator(
    XInputRumbleFrameAccumulator& accumulator) {
    accumulator = {};
}

struct XInputHapticSignal {
    float amplitude{};
    float frequencyHz{};
    bool active{};
};

inline XInputHapticSignal selectNativeOrWeaponFireHapticSignal(
    const XInputHapticSignal& nativeSignal,
    bool weaponFireFallbackActive) {
    if (nativeSignal.active)
        return nativeSignal;
    if (!weaponFireFallbackActive)
        return {};
    return {
        weaponFireHapticFallbackAmplitude,
        weaponFireHapticFallbackFrequencyHz,
        true
    };
}

inline std::uint32_t selectBhapticsRumbleState(
    std::uint32_t nativeRumble,
    const XInputHapticSignal& nativeSignal,
    const XInputHapticSignal& combinedSignal) {
    if (nativeSignal.active)
        return nativeRumble;
    if (!combinedSignal.active)
        return 0;
    const auto fallbackMotor = static_cast<std::uint16_t>(std::lround(
        std::clamp(combinedSignal.amplitude, 0.0f, 1.0f)
            * xinputHapticMotorMaximum));
    // The explicit weapon fallback has no native low/high motor pair. Treat
    // it as upper-torso/high-frequency energy while retaining native pairs
    // unchanged for every signal DOOM actually sends through XInput.
    return packXInputRumble(0, fallbackMotor);
}

struct AdditiveHapticFanout {
    XInputHapticSignal controllerSignal{};
    std::uint32_t bhapticsRumble{};
};

inline AdditiveHapticFanout selectAdditiveHapticFanout(
    std::uint32_t nativeRumble,
    const XInputHapticSignal& nativeSignal,
    bool weaponFireFallbackActive) {
    const auto controllerSignal = selectNativeOrWeaponFireHapticSignal(
        nativeSignal, weaponFireFallbackActive);
    return {
        controllerSignal,
        selectBhapticsRumbleState(
            nativeRumble, nativeSignal, controllerSignal)
    };
}

struct WeaponRumbleContext {
    std::uint64_t fireUntil{};
    bool weaponSignal{};
    bool update(bool gameplay,bool fireDown,bool signalActive,std::uint64_t now) {
        if(!gameplay){fireUntil=0;weaponSignal=false;return false;}
        if(fireDown)fireUntil=now+weaponFireHapticFallbackHoldMilliseconds;
        // Keep a shot's rumble tail on the weapon hand after trigger release.
        // A silent boundary ends that signal; non-weapon feedback stays bilateral.
        if(!signalActive)weaponSignal=false;
        else if(now<fireUntil)weaponSignal=true;
        return signalActive&&weaponSignal;
    }
};

inline std::array<XInputHapticSignal, 2> routeXInputHapticSignal(
    const XInputHapticSignal& combined,
    bool weaponRumble,
    bool leftHanded,
    bool twoHandLatched) {
    std::array<XInputHapticSignal, 2> desired{combined, combined};
    if (combined.active && weaponRumble && !twoHandLatched)
        desired[leftHanded ? 1 : 0] = {};
    return desired;
}

inline float filterXInputHapticMotor(std::uint16_t motorSpeed) {
    const float normalized = static_cast<float>(motorSpeed)
        / xinputHapticMotorMaximum;
    if (normalized <= xinputHapticDeadzone)
        return 0.0f;
    return std::clamp(
        (normalized - xinputHapticDeadzone) / (1.0f - xinputHapticDeadzone),
        0.0f, 1.0f);
}

inline XInputHapticSignal mapXInputRumble(
    std::uint16_t lowMotorSpeed, std::uint16_t highMotorSpeed) {
    const float low = filterXInputHapticMotor(lowMotorSpeed);
    const float high = filterXInputHapticMotor(highMotorSpeed);
    const float weight = low + high;
    if (weight <= 0.0f)
        return {};

    XInputHapticSignal signal{};
    signal.active = true;
    signal.amplitude = std::clamp(
        std::max(low, high), xinputHapticMinimumAmplitude,
        xinputHapticMaximumAmplitude);
    signal.frequencyHz = std::clamp(
        (low * xinputHapticLowFrequencyHz
            + high * xinputHapticHighFrequencyHz) / weight,
        xinputHapticMinimumFrequencyHz, xinputHapticMaximumFrequencyHz);
    return signal;
}

inline bool materiallyDifferent(
    const XInputHapticSignal& left, const XInputHapticSignal& right) {
    if (left.active != right.active)
        return true;
    if (!left.active)
        return false;
    return std::fabs(left.amplitude - right.amplitude)
            >= xinputHapticAmplitudeChangeThreshold
        || std::fabs(left.frequencyHz - right.frequencyHz)
            >= xinputHapticFrequencyChangeThresholdHz;
}

enum class XInputHapticCommand {
    None,
    Apply,
    Stop,
};

// UI clicks join the per-hand output after native/bHaptics fanout. They never
// create vest rumble or replace a stronger native controller effect.
struct ControllerClickState {
    std::uint64_t untilMilliseconds{};
};

inline void queueControllerClick(ControllerClickState& state, std::uint64_t now) {
    state.untilMilliseconds = now + 25;
}

inline XInputHapticSignal mixControllerClick(
    const XInputHapticSignal& native, const ControllerClickState& click,
    std::uint64_t now) {
    if (now >= click.untilMilliseconds) return native;
    if (native.active && native.amplitude >= 0.5f) return native;
    return {0.5f, 160.0f, true};
}

inline std::uint64_t controllerHapticDurationMilliseconds(
    const XInputHapticSignal& native, const ControllerClickState& click,
    std::uint64_t now) {
    if (!native.active && now < click.untilMilliseconds)
        return click.untilMilliseconds - now;
    return xinputHapticPulseDurationMilliseconds;
}

struct XInputHapticOutputState {
    bool effectActive{};
    XInputHapticSignal lastApplied{};
    std::uint64_t refreshAtMilliseconds{};
    std::uint64_t retryAtMilliseconds{};
};

inline XInputHapticCommand selectXInputHapticCommand(
    const XInputHapticOutputState& state,
    const XInputHapticSignal& desired,
    std::uint64_t nowMilliseconds) {
    if (!desired.active)
        return state.effectActive
            ? XInputHapticCommand::Stop : XInputHapticCommand::None;
    if (nowMilliseconds < state.retryAtMilliseconds)
        return XInputHapticCommand::None;
    if (!state.effectActive
        || materiallyDifferent(state.lastApplied, desired)
        || nowMilliseconds >= state.refreshAtMilliseconds)
        return XInputHapticCommand::Apply;
    return XInputHapticCommand::None;
}

inline void noteXInputHapticApplySucceeded(
    XInputHapticOutputState& state,
    const XInputHapticSignal& applied,
    std::uint64_t nowMilliseconds,
    std::uint64_t durationMilliseconds = xinputHapticPulseDurationMilliseconds) {
    state.effectActive = true;
    state.lastApplied = applied;
    state.refreshAtMilliseconds = nowMilliseconds
        + std::min(xinputHapticRefreshIntervalMilliseconds, durationMilliseconds);
    state.retryAtMilliseconds = 0;
}

inline void noteXInputHapticApplyFailed(
    XInputHapticOutputState& state, std::uint64_t nowMilliseconds) {
    state.retryAtMilliseconds = nowMilliseconds
        + xinputHapticErrorRetryMilliseconds;
}

inline void noteXInputHapticStopAttempted(XInputHapticOutputState& state) {
    // The pulses are deliberately short. Even if xrStopHapticFeedback fails,
    // treating this as stopped prevents a zero-valued XInput state from
    // producing one stop call per frame.
    state = {};
}

inline void resetXInputHapticOutputState(XInputHapticOutputState& state) {
    state = {};
}

} // namespace kharvox
