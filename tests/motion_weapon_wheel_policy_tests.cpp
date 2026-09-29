#include "../src/openxr/MotionWeaponWheelPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << std::endl;
        std::abort();
    }
}

bool near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    kharvox::MotionWeaponWheelState state{};
    kharvox::MotionWeaponWheelInput input{};
    input.trackingValid = true;

    // 1. When wheel is inactive, output must be completely inert
    input.wheelActive = false;
    input.handPosition = {1.0f, 1.0f, 1.0f};
    auto out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickActive, "inactive wheel must not activate stick");
    check(!out.triggerHapticPulse, "inactive wheel must not trigger haptic");
    check(out.selectedSector == -1, "inactive wheel sector must be -1");

    // 2. First frame wheel opens: captures origin anchor and returns zero stick
    input.wheelActive = true;
    input.handPosition = {0.2f, 1.2f, -0.4f};
    input.hmdOrientation = {0.0f, 0.0f, 0.0f, 1.0f}; // Identity orientation
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickActive, "first frame anchor must not deflect stick");
    check(!out.triggerHapticPulse, "first frame must not trigger haptic");
    check(near(state.originHandPosition.x, 0.2f), "origin x must match anchor");
    check(near(state.originHandPosition.y, 1.2f), "origin y must match anchor");
    check(near(state.originHandPosition.z, -0.4f), "origin z must match anchor");

    // 3. Movement within deadzone (1.5 cm < 2.0 cm threshold)
    input.handPosition = {0.215f, 1.2f, -0.4f}; // +1.5 cm in X (Right)
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickActive, "deadzone displacement must not activate stick");
    check(out.selectedSector == -1, "deadzone sector must remain -1");
    check(!out.triggerHapticPulse, "deadzone must not trigger haptic pulse");

    // 4. Movement beyond deadzone to Sector 0 (Right: +X)
    // Distance = 4.25 cm (midway between 2.0 cm deadzone and 6.5 cm max reach)
    input.handPosition = {0.2425f, 1.2f, -0.4f}; // +4.25 cm
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.stickActive, "movement beyond deadzone must activate stick");
    check(out.selectedSector == 0, "positive X displacement must map to Sector 0 (Right)");
    check(out.triggerHapticPulse, "entering Sector 0 from deadzone must trigger haptic pulse");
    check(out.stickX > 0.4f && out.stickX < 0.6f, "stickX should be scaled approximately to 0.5");
    check(near(out.stickY, 0.0f), "pure horizontal movement must have zero stickY");

    // Sub-frame in same sector: must NOT repeat haptic pulse
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.triggerHapticPulse, "staying in same sector must not re-trigger haptic pulse");

    // 5. Max radius reach and clamping (> 6.5 cm)
    input.handPosition = {0.30f, 1.2f, -0.4f}; // +10 cm (well beyond 6.5 cm)
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(near(out.stickX, 1.0f), "full reach displacement must clamp stickX to 1.0");
    check(out.selectedSector == 0, "must remain in Sector 0");

    // 6. Sector transitions: test all 8 radial sectors
    // Sector 1: Up-Right (+X, +Y)
    input.handPosition = {0.25f, 1.25f, -0.4f};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 1, "Up-Right displacement must map to Sector 1");
    check(out.triggerHapticPulse, "crossing to Sector 1 must trigger haptic pulse");

    // Sector 2: Up (+Y)
    input.handPosition = {0.2f, 1.25f, -0.4f};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 2, "Up displacement must map to Sector 2");
    check(out.triggerHapticPulse, "crossing to Sector 2 must trigger haptic pulse");

    // Sector 3: Up-Left (-X, +Y)
    input.handPosition = {0.15f, 1.25f, -0.4f};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 3, "Up-Left displacement must map to Sector 3");

    // Sector 4: Left (-X)
    input.handPosition = {0.15f, 1.2f, -0.4f};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 4, "Left displacement must map to Sector 4");

    // Sector 5: Down-Left (-X, -Y)
    input.handPosition = {0.15f, 1.15f, -0.4f};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 5, "Down-Left displacement must map to Sector 5");

    // Sector 6: Down (-Y)
    input.handPosition = {0.2f, 1.15f, -0.4f};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 6, "Down displacement must map to Sector 6");

    // Sector 7: Down-Right (+X, -Y)
    input.handPosition = {0.25f, 1.15f, -0.4f};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 7, "Down-Right displacement must map to Sector 7");

    // 7. Physical stick bypass
    input.stickBypass = true;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.stickBypassActive, "stick bypass flag must activate stickBypassActive output");
    check(!out.stickActive, "stick bypass must not output motion stick values");
    check(!out.triggerHapticPulse, "stick bypass must suppress haptics");
    input.stickBypass = false;

    // 8. HMD Yaw rotation invariance
    // Reset wheel with HMD rotated 90 degrees to the right (Yaw = -90 deg around +Y)
    input.wheelActive = false;
    kharvox::updateMotionWeaponWheel(state, input);

    input.wheelActive = true;
    // Quaternion for -90 degrees around Y: y = -sin(45 deg) = -0.7071f, w = cos(45 deg) = 0.7071f
    input.hmdOrientation = {0.0f, -0.70710678f, 0.0f, 0.70710678f};
    input.handPosition = {0.0f, 1.0f, 0.0f};
    // Frame 1: capture anchor
    out = kharvox::updateMotionWeaponWheel(state, input);

    // When facing -90 degrees (facing towards +X world), the user's "screen right" is +Z world
    // Moving hand +5cm along world +Z should be perceived as moving to screen right (Sector 0)
    input.handPosition = {0.0f, 1.0f, 0.05f};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 0, "rotated HMD view must project screen right to Sector 0");
    check(out.stickX > 0.0f, "screen right displacement must result in positive stickX");

    // 9. Wheel close and state cleanup
    input.wheelActive = false;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickActive, "closing wheel must clear output");
    check(!state.wasActive, "closing wheel must reset state.wasActive");
    check(state.lastSelectedSector == -1, "closing wheel must reset lastSelectedSector");

    // Stick priority applies only outside a radial 0.30 deadzone.
    check(!kharvox::motionWheelStickBypass(-0.30f, 0), "boundary belongs to motion");
    check(kharvox::motionWheelStickBypass(-0.301f, 0), "above boundary gives stick priority");
    check(!kharvox::motionWheelStickBypass(0.20f, 0.20f), "diagonal inside radial deadzone");
    check(kharvox::motionWheelStickBypass(0.22f, 0.22f), "diagonal outside radial deadzone");
    input.wheelActive = true;
    input.hmdOrientation = {};
    input.handPosition = {};
    input.stickBypass = true;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.stickBypassActive && state.anchorValid, "opening with stick still captures motion anchor");
    input.stickBypass = false;
    input.handPosition.x = 0.05f;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickBypassActive && out.stickActive && out.selectedSector == 0,
        "centering stick immediately resumes hand selection");
    input.stickBypass = true;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.stickBypassActive && !out.triggerHapticPulse, "stick takeover suppresses motion clicks");
    input.stickBypass = false;
    input.handPosition = {0, 0.05f, 0};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickBypassActive && out.selectedSector == 2,
        "repeated hand-stick-hand transitions need no wheel reopen");
    input.stickBypass = true;
    input.trackingValid = false;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.stickBypassActive && !state.anchorValid,
        "tracking loss during stick use still clears the hand anchor");
    input.handPosition = {1, 2, 3};
    input.trackingValid = true;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.stickBypassActive && state.anchorValid && !out.triggerHapticPulse,
        "recovery reanchors even while stick owns output");
    input.stickBypass = false;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickBypassActive && !out.stickActive,
        "centering after tracking recovery does not jump to old anchor");
    input.wheelActive = false;
    kharvox::updateMotionWeaponWheel(state, input);
    input.wheelActive = true;
    input.hmdOrientation = {};
    input.handPosition = {};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickBypassActive && state.anchorValid, "next opening permits motion again");
    input.handPosition.x = 0.0201f;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.triggerHapticPulse && out.stickX > 0.15f,
        "first sector click must exceed native DOOM stick deadzone");
    input.trackingValid = false;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!state.anchorValid && !out.stickActive && !out.triggerHapticPulse,
        "tracking loss invalidates motion anchor and selection");
    input.trackingValid = true;
    input.handPosition = {1, 2, 3};
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(state.anchorValid && !out.stickActive && !out.triggerHapticPulse,
        "recovered distant position reanchors without jumping");
    input.handPosition.x += 0.05f;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.selectedSector == 0, "motion resumes relative to recovered anchor");
    input.handPosition.x = std::numeric_limits<float>::quiet_NaN();
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!out.stickActive && !state.anchorValid, "non-finite pose is rejected");
    input.trackingValid = false;
    input.stickBypass = true;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(out.stickBypassActive, "physical stick remains usable without pose tracking");
    input.config.enabled = false;
    out = kharvox::updateMotionWeaponWheel(state, input);
    check(!state.wasActive && !state.stickWasActive && !state.anchorValid,
        "disable clears the complete gesture state");

    kharvox::WeaponWheelStickHapticState stickHaptics{};
    check(!kharvox::updateWeaponWheelStickHaptics(stickHaptics, 1, 0, false),
        "inactive wheel must not click for ordinary locomotion");
    check(!kharvox::updateWeaponWheelStickHaptics(stickHaptics, 0.3f, 0, true),
        "stick deadzone must not generate a click");
    for (int sector = 0; sector < 8; ++sector) {
        const float angle = sector * 0.78539816339f;
        const float x = std::cos(angle), y = std::sin(angle);
        check(kharvox::updateWeaponWheelStickHaptics(stickHaptics, x, y, true),
            "entering each stick sector must click");
        check(stickHaptics.lastSector == sector, "stick and motion sectors must match");
        check(!kharvox::updateWeaponWheelStickHaptics(stickHaptics, x*0.7f, y*0.7f, true),
            "holding a sector or changing magnitude must not repeat clicks");
    }
    check(!kharvox::updateWeaponWheelStickHaptics(stickHaptics, 0, 0, false),
        "hand takeover clears stick sector without a click");
    check(kharvox::updateWeaponWheelStickHaptics(stickHaptics, 1, 0, true),
        "stick takeover after hand control must click again");
    check(!kharvox::updateWeaponWheelStickHaptics(stickHaptics,
        std::numeric_limits<float>::quiet_NaN(), 0, true), "invalid stick is inert");
    for (bool leftHanded : {false, true}) {
        for (bool swapped : {false, true}) {
            check(kharvox::weaponWheelHapticHand(true, leftHanded, swapped) == (swapped ? 1 : 0),
                "stick clicks follow physical selection controller regardless of weapon hand");
            check(kharvox::weaponWheelHapticHand(false, leftHanded, swapped) == (leftHanded ? 0 : 1),
                "motion clicks follow weapon hand regardless of stick mapping");
        }
    }
    std::cout << "All MotionWeaponWheelPolicy tests passed successfully!" << std::endl;
    return 0;
}
