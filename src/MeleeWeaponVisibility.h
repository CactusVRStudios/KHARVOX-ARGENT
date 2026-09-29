#pragma once
#include <cstdint>
namespace argent {
// Reflected idHands::meleeD5Type_t: NONE=0, FORWARD=1,
// POWER_STRIKE=2, COMBO=3. Do not reinterpret every animation as a punch.
inline bool keepMeleeWeaponVisible(bool gameplay,bool sync,bool traversal,
 bool freshPlacement,uintptr_t hands,uintptr_t item,int meleeType) noexcept {
 return gameplay&&!sync&&!traversal&&freshPlacement&&hands&&
        item==hands+0x29b0&&meleeType>=1&&meleeType<=3;
}
}
