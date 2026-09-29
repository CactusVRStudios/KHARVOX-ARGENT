#pragma once
#include <cmath>
#include <cstdint>

namespace kharvox {
struct HandsJumpState {
    bool armed{};
    int64_t pulseUntil{}, cooldownUntil{};

    bool update(int64_t now, bool enabledContext, bool tracked,
                float leftUpSpeed, float rightUpSpeed) {
        if (!enabledContext || !tracked || now <= 0
            || !std::isfinite(leftUpSpeed) || !std::isfinite(rightUpSpeed)) {
            *this = {};
            return false;
        }
        // Require both hands to settle before accepting a new upward gesture.
        // Starting/resuming tracking in motion must never generate a jump.
        if (leftUpSpeed <= 0.5f && rightUpSpeed <= 0.5f) armed = true;
        if (armed && now >= cooldownUntil
            && leftUpSpeed >= 1.9f && rightUpSpeed >= 1.9f) {
            armed = false;
            pulseUntil = now + 100000000;
            cooldownUntil = now + 350000000;
        }
        return now < pulseUntil;
    }
};
}
