#pragma once
#include "GripThresholdPolicy.h"

namespace argent::input {
// Physical buttons feed the same HandsInput channels for both profiles.
inline constexpr const char* faceButtonComponent(bool index, int hand, bool upper) {
    return !index && hand == 0 ? (upper ? "y/click" : "x/click")
                              : (upper ? "b/click" : "a/click");
}
inline constexpr const char* pauseComponent(bool index) {
    // Index has no application menu button. Keep stick click for the dossier.
    return index ? "trackpad/force" : "menu/click";
}
inline bool updateIndexPause(float force, bool held) {
    return kharvox::updateGripPressed(force, held, kharvox::GripControllerProfile::ValveIndex);
}
}
