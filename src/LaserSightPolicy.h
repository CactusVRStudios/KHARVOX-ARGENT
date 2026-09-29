#pragma once
#include "hands/HandWeaponProfile.h"
namespace argent {
inline bool laserWeaponAllowed(kharvox::hands::HandWeaponKind kind){
 using W=kharvox::hands::HandWeaponKind;
 switch(kind){case W::CombatShotgun:case W::HeavyCannon:case W::PlasmaRifle:
 case W::RocketLauncher:case W::SuperShotgun:case W::Ballista:case W::Chaingun:case W::Bfg:return true;
 default:return false;}
}
}
