#pragma once
#include "../hands/HandHudMask.h"
namespace argent::hud {
int calibrationPlaceholderRole();
kharvox::hands::HandHudPanels calibrationPlaceholder(int role,bool leftMode,const float* grip,const float* hand,float units,const float* eye);
}
namespace argent::hud { bool installWeaponWheel(unsigned char* verifiedImage) noexcept; }
namespace argent::hud { void pollHandCalibration(bool enabled,bool leftMode); }
namespace argent::hud { bool weaponWheelVisible() noexcept; }
namespace argent::hud { bool flatMenuVisible(bool gameplayOnly=false,bool tutorialOnly=false) noexcept; }
