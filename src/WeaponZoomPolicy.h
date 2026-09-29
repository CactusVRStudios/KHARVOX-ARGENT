#pragma once
#include <cmath>

namespace argent {
inline bool keepPrecisionBoltVisible(bool vrReady,bool poseReady,bool primaryItem,
                                    bool precisionBolt,int selectedMode) noexcept {
 return vrReady&&poseReady&&primaryItem&&precisionBolt&&selectedMode==1;
}
// Eternal's reflected idDeclWeapon::zoomMode_t. Keep mode 0 (no zoom)
// and mode 3 (resolve secondary weapon) intact.
inline int vrWeaponZoomMode(int nativeMode, bool controllerPresentation) noexcept {
 return controllerPresentation && nativeMode == 1 ? 2 : nativeMode;
}
inline float vrWeaponZoomBlend(float nativeBlend, bool controllerPresentation) noexcept {
 return controllerPresentation ? 0.f : nativeBlend;
}
inline float vrWeaponWorldFov(float nativeFov,float gameplayFov,bool controllerPresentation) noexcept {
 return controllerPresentation&&std::isfinite(gameplayFov)&&gameplayFov>0.f&&gameplayFov<180.f?gameplayFov:nativeFov;
}
inline float vrWeaponHandsFov(float nativeHandsFov,float worldFov,bool controllerPresentation) noexcept {
 // Positive handsFov selects handsFov/worldFov in BOTH native zoom transitions.
 // Use exactly one, rather than zero (which selects another authored scale).
 return controllerPresentation&&std::isfinite(worldFov)&&worldFov>0.f&&worldFov<180.f?worldFov:nativeHandsFov;
}
inline bool validWeaponZoomPresentation(float fov, float handsFov, unsigned char hide,
                                       int scope, int mode) noexcept {
 return std::isfinite(fov) && fov >= 0.f && fov < 180.f &&
        std::isfinite(handsFov) && handsFov >= 0.f && handsFov < 180.f &&
        hide <= 1 && scope >= 0 && scope <= 8 && mode >= 0 && mode <= 3;
}
}
