#pragma once

#include <cmath>
#include <cstdint>

namespace kharvox {

// Use predicted display times, including the effective time of a LOCAL-space
// change. VALID alone may describe a stale pose while the headset wakes up.
struct CinewindowCaptureReadiness {
    int64_t trackedSince{};
    int64_t referenceChangeTime{};

    void referenceChanged(int64_t time) {
        if (time > referenceChangeTime) referenceChangeTime = time;
        trackedSince = 0;
    }

    bool ready(int64_t displayTime, bool focused, bool tracked) {
        if (!focused || !tracked || displayTime <= 0
            || displayTime < referenceChangeTime) {
            trackedSince = 0;
            return false;
        }
        if (!trackedSince || displayTime < trackedSince) trackedSince = displayTime;
        return displayTime - trackedSince >= 250000000; // 250 ms, independent of refresh rate.
    }
};

struct CinewindowAnchor {
    bool valid{};
    float positionX{};
    float positionY{};
    float positionZ{};
    float forwardX{};
    float forwardZ{-1.0f};
    float orientationY{};
    float orientationW{1.0f};
};

struct CinewindowLayerPose {
    float orientationX{};
    float orientationY{};
    float orientationZ{};
    float orientationW{1.0f};
    float positionX{};
    float positionY{};
    float positionZ{};
};

inline void resetCinewindowAnchor(CinewindowAnchor& anchor) {
    anchor = {};
}

inline bool captureCinewindowAnchor(
    CinewindowAnchor& anchor,
    float orientationX, float orientationY, float orientationZ, float orientationW,
    float positionX, float positionY, float positionZ) {
    const bool finite = std::isfinite(orientationX) && std::isfinite(orientationY)
        && std::isfinite(orientationZ) && std::isfinite(orientationW)
        && std::isfinite(positionX) && std::isfinite(positionY)
        && std::isfinite(positionZ);
    const float lengthSquared = orientationX * orientationX
        + orientationY * orientationY + orientationZ * orientationZ
        + orientationW * orientationW;
    if (!finite || lengthSquared <= 0.000001f) return false;

    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    const float qx = orientationX * inverseLength;
    const float qy = orientationY * inverseLength;
    const float qz = orientationZ * inverseLength;
    const float qw = orientationW * inverseLength;

    // OpenXR view forward is local -Z. Rotate it by the current headset
    // orientation and project it onto tracking-space XZ so pitch and roll can
    // never tilt the Cinewindow away from the gravity-level horizon.
    const float uvX = -qy;
    const float uvY = qx;
    const float uuvX = -qz * uvY;
    const float uuvZ = qx * uvY + qy * qy;
    const float forwardX = 2.0f * (qw * uvX + uuvX);
    const float forwardZ = -1.0f + 2.0f * uuvZ;
    const float horizontalLengthSquared =
        forwardX * forwardX + forwardZ * forwardZ;
    if (horizontalLengthSquared <= 0.000001f) return false;

    const float inverseHorizontalLength =
        1.0f / std::sqrt(horizontalLengthSquared);
    const float horizontalForwardX = forwardX * inverseHorizontalLength;
    const float horizontalForwardZ = forwardZ * inverseHorizontalLength;
    const float yaw = std::atan2(-horizontalForwardX, -horizontalForwardZ);
    const float halfYaw = yaw * 0.5f;

    anchor.valid = true;
    anchor.positionX = positionX;
    anchor.positionY = positionY;
    anchor.positionZ = positionZ;
    anchor.forwardX = horizontalForwardX;
    anchor.forwardZ = horizontalForwardZ;
    anchor.orientationY = std::sin(halfYaw);
    anchor.orientationW = std::cos(halfYaw);
    return true;
}

inline CinewindowLayerPose fixedCinewindowLayerPose(
    const CinewindowAnchor& anchor, float distanceMeters) {
    CinewindowLayerPose pose{};
    if (!anchor.valid || !std::isfinite(distanceMeters)) return pose;
    pose.orientationY = anchor.orientationY;
    pose.orientationW = anchor.orientationW;
    pose.positionX = anchor.positionX + anchor.forwardX * distanceMeters;
    pose.positionY = anchor.positionY;
    pose.positionZ = anchor.positionZ + anchor.forwardZ * distanceMeters;
    return pose;
}

} // namespace kharvox
