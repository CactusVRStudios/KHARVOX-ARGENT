#include "../src/bhaptics/BhapticsMappingPolicy.h"

#include <cstdint>

int main() {
    using namespace kharvox::bhaptics;

    if (normalizeMotor(0, 1.0f) != 0)
        return 1;
    const auto deadzone = static_cast<std::uint16_t>(65535.0f * rumbleDeadzone);
    if (normalizeMotor(deadzone, 1.0f) != 0)
        return 2;
    if (normalizeMotor(65535, 1.0f) != 100
        || normalizeMotor(65535, 4.0f) != 100
        || normalizeMotor(65535, 0.0f) != 0)
        return 3;

    const auto lowOnly = mapRumbleToTactSuit(65535, 0);
    const auto highOnly = mapRumbleToTactSuit(0, 65535);
    const auto both = mapRumbleToTactSuit(32768, 49152);
    if (!lowOnly.active || lowOnly.lowIntensity != 100
        || lowOnly.highIntensity != 0)
        return 4;
    if (!highOnly.active || highOnly.highIntensity != 100
        || highOnly.lowIntensity != 0)
        return 5;
    if (!both.active || both.lowIntensity <= 0 || both.highIntensity <= 0)
        return 6;
    for (std::size_t side : {std::size_t{0}, std::size_t{16}}) {
        for (std::size_t i = 0; i < 8; ++i)
            if (both.motors[side + i] != both.highIntensity)
                return 7;
        for (std::size_t i = 8; i < 16; ++i)
            if (both.motors[side + i] != both.lowIntensity)
                return 8;
    }

    RumbleDispatchState state{};
    const auto zero = mapRumbleToTactSuit(0, 0);
    if (selectRumbleCommand(state, zero, 0) != RumbleCommand::None
        || selectRumbleCommand(state, both, 0) != RumbleCommand::Play)
        return 9;
    noteRumblePlayed(state, both, 0);
    if (selectRumbleCommand(state, both, 25) != RumbleCommand::None
        || selectRumbleCommand(state, both, sustainedRumbleRefreshMilliseconds)
            != RumbleCommand::Play)
        return 10;
    if (selectRumbleCommand(state, zero, 25) != RumbleCommand::None
        || selectRumbleCommand(state, zero, maximumUpdateRateMilliseconds)
            != RumbleCommand::Stop)
        return 11;
    noteRumbleStopped(state, maximumUpdateRateMilliseconds);
    if (state.active)
        return 12;

    // Many fast hook updates coalesce to one bounded latest peak rather than
    // growing a queue. Each motor keeps the highest observed transient.
    std::uint32_t peak{};
    for (std::uint16_t i = 1; i < 1000; ++i)
        peak = mergeRumblePeaks(peak, packRumble(i, 1000 - i));
    if (lowMotor(peak) != 999 || highMotor(peak) != 999)
        return 13;

    if (!deviceJsonMayContainPosition("[{\"position\":0}]", 0)
        || !deviceJsonMayContainPosition("[{\"name\":\"TactSuit Pro\"}]", 0)
        || deviceJsonMayContainPosition("[]", 0))
        return 14;
    return 0;
}
