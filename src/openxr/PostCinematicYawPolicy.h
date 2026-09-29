#pragma once

#include <cmath>
#include <cstdint>

namespace kharvox {

constexpr std::int64_t postCinematicYawMinimumHoldNanoseconds = 250000000;
constexpr std::int64_t postCinematicYawQuietNanoseconds = 500000000;
constexpr std::int64_t postCinematicYawTimeoutNanoseconds = 6000000000;
constexpr float postCinematicYawMotionThresholdDegrees = 0.05f;
constexpr int postCinematicYawStableFramesRequired = 12;

enum class PostCinematicYawGuardExit {
    None,
    Stable,
    Timeout
};

struct PostCinematicYawGuard {
    bool active{};
    std::int64_t startedAt{};
    std::int64_t lastMotionAt{};
    int stableFrames{};
    float absorbedDegrees{};
};

struct PostCinematicYawGuardUpdate {
    bool absorbBodyDelta{};
    PostCinematicYawGuardExit exit{PostCinematicYawGuardExit::None};
};

inline void armPostCinematicYawGuard(
    PostCinematicYawGuard& guard, std::int64_t displayTime) noexcept {
    guard.active = true;
    guard.startedAt = displayTime;
    guard.lastMotionAt = displayTime;
    guard.stableFrames = 0;
    guard.absorbedDegrees = 0.0f;
}

inline void resetPostCinematicYawGuard(PostCinematicYawGuard& guard) noexcept {
    guard = {};
}

inline bool cancelPostCinematicYawGuard(PostCinematicYawGuard& guard) noexcept {
    if (!guard.active) return false;
    guard.active = false;
    guard.stableFrames = 0;
    return true;
}

inline PostCinematicYawGuardUpdate updatePostCinematicYawGuard(
    PostCinematicYawGuard& guard,
    std::int64_t displayTime,
    float bodyDeltaDegrees,
    bool bodyDeltaValid) noexcept {
    PostCinematicYawGuardUpdate result{};
    if (!guard.active) return result;

    result.absorbBodyDelta = bodyDeltaValid && std::isfinite(bodyDeltaDegrees);
    if (result.absorbBodyDelta) {
        guard.absorbedDegrees += bodyDeltaDegrees;
        if (std::abs(bodyDeltaDegrees) > postCinematicYawMotionThresholdDegrees) {
            guard.lastMotionAt = displayTime;
            guard.stableFrames = 0;
        } else {
            ++guard.stableFrames;
        }
    } else {
        guard.stableFrames = 0;
    }

    const std::int64_t elapsed = displayTime > guard.startedAt
        ? displayTime - guard.startedAt : 0;
    const std::int64_t quiet = displayTime > guard.lastMotionAt
        ? displayTime - guard.lastMotionAt : 0;
    if (elapsed >= postCinematicYawTimeoutNanoseconds) {
        guard.active = false;
        result.exit = PostCinematicYawGuardExit::Timeout;
    } else if (elapsed >= postCinematicYawMinimumHoldNanoseconds
        && quiet >= postCinematicYawQuietNanoseconds
        && guard.stableFrames >= postCinematicYawStableFramesRequired) {
        guard.active = false;
        result.exit = PostCinematicYawGuardExit::Stable;
    }
    return result;
}

} // namespace kharvox
