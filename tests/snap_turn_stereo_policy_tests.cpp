#include "../src/openxr/SnapTurnStereoPolicy.h"

#include <cassert>
#include <cmath>

int main() {
    using namespace kharvox;

    assert(snapTurnMayActivateAtStereoBoundary(false, false, false, 0));
    assert(snapTurnMayActivateAtStereoBoundary(true, true, false, 0));
    assert(!snapTurnMayActivateAtStereoBoundary(true, false, true, 1));
    assert(!snapTurnMayActivateAtStereoBoundary(true, false, false, 0));
    assert(snapTurnMayActivateAtStereoBoundary(true, false, false, 1));

    auto yaw = applySnapTurnToPhysicalResidual(10.f, 20.f, 45.f);
    assert(std::abs(yaw.acceptedPhysicalYaw + 35.f) < .0001f);
    assert(std::abs(yaw.accumulatedArtificialTurn - 65.f) < .0001f);
    yaw = applySnapTurnToPhysicalResidual(-10.f, -20.f, -45.f);
    assert(std::abs(yaw.acceptedPhysicalYaw - 35.f) < .0001f);
    assert(std::abs(yaw.accumulatedArtificialTurn + 65.f) < .0001f);

    auto compensation = snapTurnEyeCompensation(
        true, false, true, 2, 1, 45.f, 0.f);
    assert(compensation.apply);
    assert(std::abs(compensation.degrees + 45.f) < .0001f);

    compensation = snapTurnEyeCompensation(
        true, false, true, 2, 1, -45.f, 0.f);
    assert(compensation.apply);
    assert(std::abs(compensation.degrees - 45.f) < .0001f);

    assert(!snapTurnEyeCompensation(
        true, false, true, 2, 2, 45.f, 45.f).apply);
    assert(!snapTurnEyeCompensation(
        true, true, true, 2, 1, 45.f, 0.f).apply);
    assert(!snapTurnEyeCompensation(
        true, false, false, 2, 1, 45.f, 0.f).apply);

    assert(!snapTurnStereoTransitionComplete(true, 2, true, 2, true, 1));
    assert(snapTurnStereoTransitionComplete(true, 2, true, 2, true, 2));
    assert(!snapTurnStereoTransitionComplete(false, 2, true, 2, true, 2));
}
