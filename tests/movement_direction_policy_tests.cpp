#include "../src/openxr/MovementDirectionPolicy.h"

#include <cmath>

namespace {
bool near(float left, float right) {
    return std::abs(left - right) < 0.001f;
}
}

int main() {
    using namespace kharvox;

    if (offHandForMovement(false) != MovementDirectionHand::Left) return 1;
    if (offHandForMovement(true) != MovementDirectionHand::Right) return 2;

    auto yaw = resolveMovementDirectionYaw(
        MovementDirectionMode::Head, 25.0f, true, -1.0f, 0.0f, 0.0f, 0.0f);
    if (yaw.usedOffHand || !near(yaw.degrees, 25.0f)) return 3;

    yaw = resolveMovementDirectionYaw(
        MovementDirectionMode::OffHand, 10.0f, true, 0.0f, -1.0f, 0.0f, 0.0f);
    if (!yaw.usedOffHand || !near(yaw.degrees, 0.0f)) return 4;

    yaw = resolveMovementDirectionYaw(
        MovementDirectionMode::OffHand, 10.0f, true, -1.0f, 0.0f, 30.0f, 10.0f);
    if (!yaw.usedOffHand || !near(yaw.degrees, 70.0f)) return 5;

    const auto rotated = rotateMovementStickForDirection({0.0f, 1.0f}, 90.0f);
    if (!near(rotated.x, -1.0f) || !near(rotated.y, 0.0f)) return 6;

    yaw = resolveMovementDirectionYaw(
        MovementDirectionMode::OffHand, -15.0f, false, -1.0f, 0.0f, 0.0f, 0.0f);
    if (yaw.usedOffHand || !near(yaw.degrees, -15.0f)) return 7;
    yaw = resolveMovementDirectionYaw(
        MovementDirectionMode::OffHand, -15.0f, true, 0.01f, 0.01f, 0.0f, 0.0f);
    if (yaw.usedOffHand || !near(yaw.degrees, -15.0f)) return 8;
    yaw = resolveMovementDirectionYaw(
        MovementDirectionMode::OffHand, -15.0f, true, NAN, -1.0f, 0.0f, 0.0f);
    if (yaw.usedOffHand || !near(yaw.degrees, -15.0f)) return 9;

    return 0;
}
