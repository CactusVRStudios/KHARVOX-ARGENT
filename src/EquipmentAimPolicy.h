#pragma once
#include "EternalCameraMath.h"
#include <cstring>
namespace argent::player {
// Reflected equipSlot_t: shoulders and launchers, independent of unlocks,
// weapon names, selected grenade variant, or left/right controller handedness.
inline bool headAimedEquipment(int slot) noexcept {
 return slot==5||slot==6||slot==9||slot==10;
}
inline bool hideEquipmentVisual(int slot,bool vrGameplay,bool nativeAnimation) noexcept {
 return vrGameplay&&!nativeAnimation&&headAimedEquipment(slot);
}
enum class AimSource {Native,Head,Controller};
inline bool applyEquipmentDirection(int slot,bool headReady,const camera::Basis& head,float* axis) noexcept {
 if(!headAimedEquipment(slot)||!headReady||!axis||!camera::validBasis(head))return false;
 std::memcpy(axis,head.data(),sizeof(head));
 return true;
}
inline AimSource applyFireDirection(int slot,bool headReady,const camera::Basis& head,
 bool controllerReady,const camera::Basis& controller,float* muzzleAxis,float* fireAxis) noexcept {
 const bool equipment=headAimedEquipment(slot);
 const auto& direction=equipment?head:controller;
 if(!muzzleAxis||!fireAxis||!(equipment?headReady:controllerReady)||!camera::validBasis(direction))return AimSource::Native;
 // Origins and native projectile speed, spread and ballistic parameters belong
 // to the game. Update both direction outputs consumed by native firing.
 std::memcpy(muzzleAxis,direction.data(),sizeof(direction));
 std::memcpy(fireAxis,direction.data(),sizeof(direction));
 return equipment?AimSource::Head:AimSource::Controller;
}
}
