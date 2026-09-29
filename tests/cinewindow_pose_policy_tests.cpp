#include "../src/openxr/CinewindowPosePolicy.h"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace {

constexpr float pi = 3.14159265358979323846f;

bool near(float actual, float expected, float epsilon = 0.0001f) {
    return std::fabs(actual - expected) <= epsilon;
}

void require(bool condition) {
    if (!condition) std::abort();
}

} // namespace

int main() {
    kharvox::CinewindowAnchor anchor{};
    require(kharvox::captureCinewindowAnchor(
        anchor, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.6f, 2.0f));
    auto pose = kharvox::fixedCinewindowLayerPose(anchor, 2.0f);
    require(near(pose.positionX, 1.0f));
    require(near(pose.positionY, 1.6f));
    require(near(pose.positionZ, 0.0f));
    require(near(pose.orientationY, 0.0f));
    require(near(pose.orientationW, 1.0f));

    const float halfQuarterTurn = pi * 0.25f;
    require(kharvox::captureCinewindowAnchor(
        anchor, 0.0f, std::sin(halfQuarterTurn), 0.0f,
        std::cos(halfQuarterTurn), 0.0f, 1.7f, 0.0f));
    pose = kharvox::fixedCinewindowLayerPose(anchor, 3.0f);
    require(near(pose.positionX, -3.0f));
    require(near(pose.positionY, 1.7f));
    require(near(pose.positionZ, 0.0f));
    require(near(pose.orientationY, std::sin(halfQuarterTurn)));
    require(near(pose.orientationW, std::cos(halfQuarterTurn)));

    // A pitched + yawed headset preserves only the horizontal yaw.
    const float pitchHalfAngle = pi / 12.0f;
    const float pitchSin = std::sin(pitchHalfAngle);
    const float pitchCos = std::cos(pitchHalfAngle);
    const float yawSin = std::sin(halfQuarterTurn);
    const float yawCos = std::cos(halfQuarterTurn);
    require(kharvox::captureCinewindowAnchor(
        anchor, yawCos * pitchSin, yawSin * pitchCos,
        -yawSin * pitchSin, yawCos * pitchCos, 0.0f, 1.65f, 0.0f));
    pose = kharvox::fixedCinewindowLayerPose(anchor, 2.0f);
    require(near(pose.positionX, -2.0f));
    require(near(pose.positionY, 1.65f));
    require(near(pose.positionZ, 0.0f));
    require(near(pose.orientationY, yawSin));
    require(near(pose.orientationW, yawCos));

    // A pitch-only head pose must still create an upright, horizontal window.
    require(kharvox::captureCinewindowAnchor(
        anchor, std::sin(pi / 8.0f), 0.0f, 0.0f,
        std::cos(pi / 8.0f), 0.0f, 1.5f, 0.0f));
    pose = kharvox::fixedCinewindowLayerPose(anchor, 2.0f);
    require(near(pose.positionX, 0.0f));
    require(near(pose.positionY, 1.5f));
    require(near(pose.positionZ, -2.0f));
    require(near(pose.orientationY, 0.0f));
    require(near(pose.orientationW, 1.0f));

    // Roll does not alter the captured horizon either.
    require(kharvox::captureCinewindowAnchor(
        anchor, 0.0f, 0.0f, std::sin(pi / 8.0f),
        std::cos(pi / 8.0f), -1.0f, 1.8f, 4.0f));
    pose = kharvox::fixedCinewindowLayerPose(anchor, 2.0f);
    require(near(pose.positionX, -1.0f));
    require(near(pose.positionY, 1.8f));
    require(near(pose.positionZ, 2.0f));
    require(near(pose.orientationY, 0.0f));

    kharvox::resetCinewindowAnchor(anchor);
    require(!anchor.valid);
    require(!kharvox::captureCinewindowAnchor(
        anchor, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    require(!kharvox::captureCinewindowAnchor(
        anchor, 0.0f, 0.0f, 0.0f, 1.0f,
        std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f));
    // Startup can return VALID poses before focus/tracking. Never pin that height.
    kharvox::CinewindowCaptureReadiness readiness{};
    require(!readiness.ready(1000000000, false, true));
    require(!readiness.ready(2000000000, true, false));
    require(!readiness.ready(3000000000, true, true));
    require(!readiness.ready(3249999999, true, true));
    require(readiness.ready(3250000000, true, true));
    // Tracking interruption and taking off the headset restart the settling period.
    require(!readiness.ready(3300000000, true, false));
    require(!readiness.ready(4000000000, true, true));
    require(!readiness.ready(4200000000, false, true));
    require(!readiness.ready(5000000000, true, true));
    require(readiness.ready(5250000000, true, true));
    // A queued recenter must not capture a pose in the old LOCAL coordinates.
    readiness.referenceChanged(7000000000);
    require(!readiness.ready(6000000000, true, true));
    require(!readiness.ready(6999999999, true, true));
    require(!readiness.ready(7000000000, true, true));
    require(readiness.ready(7250000000, true, true));
    // No assumed standing height: LOCAL eye-level origins and seated use are valid.
    require(kharvox::captureCinewindowAnchor(anchor, 0, 0, 0, 1, 0, 0, 0));
    require(near(kharvox::fixedCinewindowLayerPose(anchor, 2).positionY, 0));
    return 0;
}
