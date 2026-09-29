#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace kharvox::bhaptics {

constexpr int tactSuitPosition = 0;
constexpr std::size_t tactSuitMotorCount = 32;
constexpr float rumbleDeadzone = 0.04f;
constexpr float defaultIntensityScale = 1.0f;
constexpr float maximumIntensityScale = 2.0f;
constexpr int maximumMotorIntensity = 100;
constexpr int materialIntensityDifference = 2;
constexpr std::uint64_t maximumUpdateRateMilliseconds = 50; // 20 Hz.
constexpr std::uint64_t sustainedRumbleRefreshMilliseconds = 250;
constexpr std::uint64_t stoppedRumbleRefreshMilliseconds = 1000;
constexpr std::uint64_t deviceRecheckMilliseconds = 2000;
constexpr int dotDurationMilliseconds = 100;

struct MappedRumble {
    std::array<int, tactSuitMotorCount> motors{};
    int lowIntensity{};
    int highIntensity{};
    bool active{};
};

inline int normalizeMotor(std::uint16_t raw, float intensityScale) {
    const float normalized = static_cast<float>(raw) / 65535.0f;
    if (normalized <= rumbleDeadzone)
        return 0;
    const float filtered = (normalized - rumbleDeadzone) / (1.0f - rumbleDeadzone);
    const float scale = std::clamp(intensityScale, 0.0f, maximumIntensityScale);
    return static_cast<int>(std::lround(std::clamp(
        filtered * scale * maximumMotorIntensity, 0.0f,
        static_cast<float>(maximumMotorIntensity))));
}

inline MappedRumble mapRumbleToTactSuit(
    std::uint16_t lowMotor, std::uint16_t highMotor,
    float intensityScale = defaultIntensityScale) {
    MappedRumble mapped{};
    mapped.lowIntensity = normalizeMotor(lowMotor, intensityScale);
    mapped.highIntensity = normalizeMotor(highMotor, intensityScale);
    mapped.active = mapped.lowIntensity != 0 || mapped.highIntensity != 0;
    if (!mapped.active)
        return mapped;

    // TactSuit Pro position 0 is represented as 16 front and 16 back motors.
    // High-frequency XInput energy uses the upper two rows; low-frequency
    // energy uses the lower two rows. This keeps the MVP deterministic while
    // preserving both motor channels without device-address assumptions.
    for (std::size_t sideOffset : {std::size_t{0}, std::size_t{16}}) {
        for (std::size_t i = 0; i < 8; ++i)
            mapped.motors[sideOffset + i] = mapped.highIntensity;
        for (std::size_t i = 8; i < 16; ++i)
            mapped.motors[sideOffset + i] = mapped.lowIntensity;
    }
    return mapped;
}

inline bool materiallyDifferent(
    const MappedRumble& left, const MappedRumble& right) {
    if (left.active != right.active)
        return true;
    if (!left.active)
        return false;
    return std::abs(left.lowIntensity - right.lowIntensity)
            >= materialIntensityDifference
        || std::abs(left.highIntensity - right.highIntensity)
            >= materialIntensityDifference;
}

enum class RumbleCommand {
    None,
    Play,
    Stop,
};

struct RumbleDispatchState {
    bool active{};
    MappedRumble last{};
    std::uint64_t nextUpdateMilliseconds{};
    std::uint64_t refreshMilliseconds{};
};

inline RumbleCommand selectRumbleCommand(
    const RumbleDispatchState& state, const MappedRumble& desired,
    std::uint64_t nowMilliseconds) {
    if (nowMilliseconds < state.nextUpdateMilliseconds)
        return RumbleCommand::None;
    if (!desired.active)
        return state.active ? RumbleCommand::Stop : RumbleCommand::None;
    if (!state.active || materiallyDifferent(state.last, desired)
        || nowMilliseconds >= state.refreshMilliseconds)
        return RumbleCommand::Play;
    return RumbleCommand::None;
}

inline void noteRumblePlayed(
    RumbleDispatchState& state, const MappedRumble& mapped,
    std::uint64_t nowMilliseconds) {
    state.active = true;
    state.last = mapped;
    state.nextUpdateMilliseconds = nowMilliseconds + maximumUpdateRateMilliseconds;
    state.refreshMilliseconds = nowMilliseconds + sustainedRumbleRefreshMilliseconds;
}

inline void noteRumbleStopped(
    RumbleDispatchState& state, std::uint64_t nowMilliseconds) {
    state = {};
    state.nextUpdateMilliseconds = nowMilliseconds + maximumUpdateRateMilliseconds;
    state.refreshMilliseconds = nowMilliseconds + stoppedRumbleRefreshMilliseconds;
}

inline std::uint32_t packRumble(std::uint16_t low, std::uint16_t high) {
    return static_cast<std::uint32_t>(low)
        | (static_cast<std::uint32_t>(high) << 16);
}

inline std::uint16_t lowMotor(std::uint32_t packed) {
    return static_cast<std::uint16_t>(packed & 0xffffu);
}

inline std::uint16_t highMotor(std::uint32_t packed) {
    return static_cast<std::uint16_t>(packed >> 16);
}

inline std::uint32_t mergeRumblePeaks(std::uint32_t left, std::uint32_t right) {
    return packRumble(std::max(lowMotor(left), lowMotor(right)),
        std::max(highMotor(left), highMotor(right)));
}

inline bool deviceJsonMayContainPosition(
    std::string_view json, int position) {
    const auto first = json.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return false;
    const auto last = json.find_last_not_of(" \t\r\n");
    const auto trimmed = json.substr(first, last - first + 1);
    if (trimmed == "[]" || trimmed == "{}" || trimmed == "null")
        return false;
    const auto digit = static_cast<char>('0' + position);
    const std::array<std::string_view, 4> prefixes{
        "\"position\":", "\"position\": ",
        "\"Position\":", "\"Position\": "
    };
    for (const auto prefix : prefixes) {
        const auto found = json.find(prefix);
        if (found != std::string_view::npos
            && found + prefix.size() < json.size()
            && json[found + prefix.size()] == digit)
            return true;
    }
    // SDK revisions have used textual position names. Do not parse or retain
    // device addresses; a matching vest entry is confirmed with
    // isbHapticsConnected(position) before playback.
    return json.find("Vest") != std::string_view::npos
        || json.find("TactSuit") != std::string_view::npos
        || (trimmed.front() == '[' && trimmed.find('{') != std::string_view::npos);
}

} // namespace kharvox::bhaptics
