#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace kharvox {

// OpenXR reports the compositor's nominal display period even when the game
// misses every other display interval. Retain the fastest plausible period
// seen in the session so a temporary 60 fps cinematic cannot disguise a
// 120 Hz headset as a 60 Hz device.
constexpr std::int64_t minimumPlausibleDisplayPeriodNs = 5'000'000;
constexpr std::int64_t maximumPlausibleDisplayPeriodNs = 20'000'000;

constexpr std::int64_t selectFastestPlausibleDisplayPeriod(
    std::int64_t current,
    std::int64_t candidate) {
    if (candidate < minimumPlausibleDisplayPeriodNs
        || candidate > maximumPlausibleDisplayPeriodNs) return current;
    return current == 0 ? candidate : std::min(current, candidate);
}

inline int targetGameHzForDisplayPeriod(std::int64_t periodNs) {
    if (periodNs <= 0) return 0;
    const auto rounded = static_cast<int>(std::lround(1'000'000'000.0 / periodNs));
    return std::clamp(rounded, 60, 200);
}

constexpr bool shouldHoldImmersiveRefresh(
    bool immersiveMode,
    bool quadMode,
    bool nativeCutsceneActive,
    bool upgradeTransitionGuardActive,
    bool nativeAdaptiveParticipantGuardActive) {
    return immersiveMode && !quadMode
        && (nativeCutsceneActive || upgradeTransitionGuardActive
            || nativeAdaptiveParticipantGuardActive);
}

constexpr unsigned long long updateNativeAdaptiveParticipantGuardUntil(
    bool eligible,
    bool participantObserved,
    unsigned long long now,
    unsigned long long previousUntil,
    unsigned long long releaseGraceMilliseconds) {
    if (!eligible) return 0;
    if (participantObserved) {
        const auto remaining = ~0ull - now;
        return remaining < releaseGraceMilliseconds
            ? ~0ull
            : now + releaseGraceMilliseconds;
    }
    return previousUntil && now <= previousUntil ? previousUntil : 0;
}

constexpr bool nativeAdaptiveParticipantGuardIsActive(
    unsigned long long now,
    unsigned long long until) {
    return until && now <= until;
}

} // namespace kharvox
