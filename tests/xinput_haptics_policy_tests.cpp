#include "../src/openxr/XInputHapticsPolicy.h"

#include <cmath>
#include <cstdint>

namespace {

bool near(float actual, float expected, float tolerance = 0.001f) {
    return std::fabs(actual - expected) <= tolerance;
}

} // namespace

int main() {
    using namespace kharvox;

    const auto zero = mapXInputRumble(0, 0);
    WeaponRumbleContext context;
    if(context.update(true,false,true,100))return 70;
    if(!context.update(true,true,true,110))return 71;
    if(!context.update(true,false,true,300))return 72;
    if(context.update(true,false,false,301))return 73;
    if(context.update(true,false,true,302))return 74;
    context.update(true,true,false,400);
    if(!context.update(true,false,true,420))return 75;
    if(context.update(false,true,true,421))return 76;
    const auto shot=mapXInputRumble(32000,16000);
    for(bool left:{false,true}){
        const int primary=left?0:1,off=1-primary;
        auto one=routeXInputHapticSignal(shot,true,left,false);
        auto two=routeXInputHapticSignal(shot,true,left,true);
        auto released=routeXInputHapticSignal(shot,true,left,false);
        if(!one[primary].active||one[off].active||!two[0].active||!two[1].active||released[off].active)return 77;
        XInputHapticOutputState state;
        noteXInputHapticApplySucceeded(state,two[off],500);
        if(selectXInputHapticCommand(state,released[off],501)!=XInputHapticCommand::Stop)return 78;
    }
    if (zero.active || !near(zero.amplitude, 0.0f))
        return 1;

    const auto lowOnly = mapXInputRumble(65535, 0);
    if (!lowOnly.active || !near(lowOnly.amplitude, 1.0f)
        || !near(lowOnly.frequencyHz, xinputHapticLowFrequencyHz))
        return 2;

    const auto highOnly = mapXInputRumble(0, 65535);
    if (!highOnly.active || !near(highOnly.amplitude, 1.0f)
        || !near(highOnly.frequencyHz, xinputHapticHighFrequencyHz))
        return 3;

    const auto bothEqual = mapXInputRumble(32768, 32768);
    if (!bothEqual.active
        || !near(bothEqual.frequencyHz,
            (xinputHapticLowFrequencyHz + xinputHapticHighFrequencyHz) * 0.5f))
        return 4;

    const auto different = mapXInputRumble(65535, 16384);
    if (!different.active
        || different.frequencyHz <= xinputHapticLowFrequencyHz
        || different.frequencyHz >= 100.0f
        || !near(different.amplitude, 1.0f))
        return 5;

    const std::uint16_t atDeadzone = static_cast<std::uint16_t>(
        xinputHapticMotorMaximum * xinputHapticDeadzone);
    if (mapXInputRumble(atDeadzone, atDeadzone).active)
        return 6;

    const auto maximum = mapXInputRumble(65535, 65535);
    if (!maximum.active || !near(maximum.amplitude, 1.0f)
        || maximum.frequencyHz < xinputHapticMinimumFrequencyHz
        || maximum.frequencyHz > xinputHapticMaximumFrequencyHz)
        return 7;

    const auto baseline = mapXInputRumble(32768, 32768);
    const auto tinyChange = mapXInputRumble(32800, 32768);
    if (materiallyDifferent(baseline, tinyChange))
        return 8;

    XInputHapticOutputState state{};
    if (selectXInputHapticCommand(state, baseline, 1000)
        != XInputHapticCommand::Apply)
        return 9;
    noteXInputHapticApplySucceeded(state, baseline, 1000);
    if (selectXInputHapticCommand(state, baseline, 1040)
        != XInputHapticCommand::None)
        return 10;
    if (selectXInputHapticCommand(state, baseline, 1075)
        != XInputHapticCommand::Apply)
        return 11;
    noteXInputHapticApplySucceeded(state, baseline, 1075);
    if (selectXInputHapticCommand(state, zero, 1080)
        != XInputHapticCommand::Stop)
        return 12;
    noteXInputHapticStopAttempted(state);
    if (selectXInputHapticCommand(state, zero, 1081)
        != XInputHapticCommand::None)
        return 13;

    noteXInputHapticApplyFailed(state, 2000);
    if (selectXInputHapticCommand(state, baseline, 2200)
            != XInputHapticCommand::None
        || selectXInputHapticCommand(state, baseline,
            2000 + xinputHapticErrorRetryMilliseconds)
            != XInputHapticCommand::Apply)
        return 14;

    resetXInputHapticOutputState(state);
    if (state.effectActive || state.retryAtMilliseconds != 0)
        return 15;

    const auto lowPeak = packXInputRumble(50000, 1000);
    const auto highPeak = packXInputRumble(2000, 60000);
    const auto mergedPeak = mergeXInputRumblePeaks(lowPeak, highPeak);
    if (xinputRumbleLowMotor(mergedPeak) != 50000
        || xinputRumbleHighMotor(mergedPeak) != 60000)
        return 16;

    XInputRumbleFrameAccumulator accumulator{};
    auto frameRumble = selectXInputRumbleForFrame(
        accumulator, 0, highPeak, 3000);
    if (frameRumble != highPeak)
        return 17;
    frameRumble = selectXInputRumbleForFrame(
        accumulator, 0, 0, 3020);
    if (frameRumble != highPeak)
        return 18;
    frameRumble = selectXInputRumbleForFrame(
        accumulator, 0, 0,
        3000 + xinputHapticTransientHoldMilliseconds);
    if (frameRumble != 0 || accumulator.heldTransient != 0)
        return 19;

    const auto continuous = packXInputRumble(32000, 0);
    frameRumble = selectXInputRumbleForFrame(
        accumulator, continuous, 0, 4000);
    if (frameRumble != continuous)
        return 20;
    resetXInputRumbleFrameAccumulator(accumulator);
    if (accumulator.heldTransient != 0
        || accumulator.holdUntilMilliseconds != 0)
        return 21;

    auto routed = routeXInputHapticSignal(highOnly, false, false, false);
    if (!routed[0].active || !routed[1].active)
        return 22;
    routed = routeXInputHapticSignal(highOnly, true, false, false);
    if (routed[0].active || !routed[1].active)
        return 23;
    routed = routeXInputHapticSignal(highOnly, true, true, false);
    if (!routed[0].active || routed[1].active)
        return 24;
    routed = routeXInputHapticSignal(highOnly, true, false, true);
    if (!routed[0].active || !routed[1].active)
        return 25;

    const auto fallback = selectNativeOrWeaponFireHapticSignal(zero, true);
    if (!fallback.active
        || !near(fallback.amplitude, weaponFireHapticFallbackAmplitude)
        || !near(fallback.frequencyHz, weaponFireHapticFallbackFrequencyHz))
        return 26;
    if (selectNativeOrWeaponFireHapticSignal(zero, false).active)
        return 27;
    const auto nativePreferred = selectNativeOrWeaponFireHapticSignal(lowOnly, true);
    if (!nativePreferred.active
        || !near(nativePreferred.amplitude, lowOnly.amplitude)
        || !near(nativePreferred.frequencyHz, lowOnly.frequencyHz))
        return 28;

    const auto nativeBhaptics = selectBhapticsRumbleState(
        highPeak, highOnly, nativePreferred);
    if (nativeBhaptics != highPeak)
        return 29;
    const auto fallbackBhaptics = selectBhapticsRumbleState(
        0, zero, fallback);
    if (xinputRumbleLowMotor(fallbackBhaptics) != 0
        || xinputRumbleHighMotor(fallbackBhaptics) == 0)
        return 30;
    if (selectBhapticsRumbleState(0, zero, zero) != 0)
        return 31;

    const auto nativeFanout = selectAdditiveHapticFanout(
        highPeak, highOnly, true);
    if (!nativeFanout.controllerSignal.active
        || !near(nativeFanout.controllerSignal.amplitude, highOnly.amplitude)
        || !near(nativeFanout.controllerSignal.frequencyHz, highOnly.frequencyHz)
        || nativeFanout.bhapticsRumble != highPeak)
        return 32;

    const auto fallbackFanout = selectAdditiveHapticFanout(0, zero, true);
    if (!fallbackFanout.controllerSignal.active
        || !near(fallbackFanout.controllerSignal.amplitude,
            weaponFireHapticFallbackAmplitude)
        || xinputRumbleLowMotor(fallbackFanout.bhapticsRumble) != 0
        || xinputRumbleHighMotor(fallbackFanout.bhapticsRumble) == 0)
        return 33;

    const auto inactiveFanout = selectAdditiveHapticFanout(0, zero, false);
    if (inactiveFanout.controllerSignal.active
        || inactiveFanout.bhapticsRumble != 0)
        return 34;

    // Regression: a wheel click immediately followed by native rumble ending
    // must remain an Apply, not the Stop issued by the former second writer.
    ControllerClickState click{};
    queueControllerClick(click, 100);
    XInputHapticOutputState clickOutput{};
    noteXInputHapticApplySucceeded(clickOutput, highOnly, 90);
    const auto clickOnly = mixControllerClick(zero, click, 100);
    if (!clickOnly.active || !near(clickOnly.amplitude, 0.5f)
        || selectXInputHapticCommand(clickOutput, clickOnly, 100) != XInputHapticCommand::Apply
        || controllerHapticDurationMilliseconds(zero, click, 100) != 25)
        return 35;
    noteXInputHapticApplySucceeded(clickOutput, clickOnly, 100, 25);
    if (selectXInputHapticCommand(clickOutput, mixControllerClick(zero, click, 124), 124)
        == XInputHapticCommand::Stop)
        return 36;
    if (selectXInputHapticCommand(clickOutput, mixControllerClick(zero, click, 125), 125)
        != XInputHapticCommand::Stop)
        return 37;
    const XInputHapticSignal strong{0.9f, 80.f, true};
    const auto preserved = mixControllerClick(strong, click, 110);
    if (!near(preserved.amplitude, strong.amplitude)
        || !near(preserved.frequencyHz, strong.frequencyHz)) return 38;
    const XInputHapticSignal weak{0.2f, 80.f, true};
    if (!near(mixControllerClick(weak, click, 110).amplitude, 0.5f)
        || !near(mixControllerClick(weak, click, 125).amplitude, weak.amplitude)) return 39;
    // One hand's click cannot reach the other controller or bHaptics fanout.
    const ControllerClickState otherHand{};
    if (mixControllerClick(zero, otherHand, 110).active
        || selectAdditiveHapticFanout(0, zero, false).bhapticsRumble != 0) return 40;
    click = {}; // loss of focus / cancelled wheel
    if (mixControllerClick(zero, click, 110).active) return 41;
    // An equal-strength game effect arriving after a short click must be
    // refreshed at 25ms, not treated as a still-running 100ms native pulse.
    if (selectXInputHapticCommand(clickOutput, clickOnly, 125)
        != XInputHapticCommand::Apply) return 42;
    return 0;
}
