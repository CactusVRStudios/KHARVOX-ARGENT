#pragma once
#include "ControllerInput.h"
#include "MotionWeaponWheelPolicy.h"
namespace argent::input {
struct EternalMotionWheel {
 kharvox::MotionWeaponWheelState motion;
 kharvox::WeaponWheelStickHapticState stickHaptics;
 struct Result {bool active{},tracked{},stickOwned{},click{};int hand{};};
 Result update(XINPUT_GAMEPAD& pad,bool enabled,bool left,bool fullSwap,
  const XrPosef& hand,bool tracking,const XrQuaternionf& head){
  kharvox::MotionWeaponWheelInput in;
  in.wheelActive=enabled&&(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER);
  in.trackingValid=tracking;in.handPosition={hand.position.x,hand.position.y,hand.position.z};
  in.hmdOrientation={head.x,head.y,head.z,head.w};
  in.config.nativeStickDeadzone=.30f;
  const float x=pad.sThumbRX/32767.f,y=pad.sThumbRY/32767.f;
  in.stickBypass=kharvox::motionWheelStickBypass(x,y);
  const auto out=kharvox::updateMotionWeaponWheel(motion,in);
  const bool stick=in.wheelActive&&out.stickBypassActive;
  const bool click=kharvox::updateWeaponWheelStickHaptics(stickHaptics,x,y,stick);
  if(in.wheelActive&&!stick){pad.sThumbRX=axis(out.stickX);pad.sThumbRY=axis(out.stickY);}
  return {in.wheelActive,in.trackingValid,stick,stick?click:out.triggerHapticPulse,
   kharvox::weaponWheelHapticHand(stick,left,left&&fullSwap)};
 }
};
}
