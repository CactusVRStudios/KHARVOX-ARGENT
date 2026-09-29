#pragma once
#include "EternalCameraMath.h"
#include <algorithm>
#include <cstdint>
namespace argent::camera {
inline float wrapYaw(float a){return std::remainder(a,360.f);}
inline float yawDegrees(XrQuaternionf q){return std::atan2(2*(q.w*q.y+q.x*q.z),1-2*(q.x*q.x+q.y*q.y))*57.2957795131f;}
inline XrQuaternionf yawQuaternion(float degrees){float h=degrees*.00872664626f;return {0,std::sin(h),0,std::cos(h)};}
// Adapted from KHARVOX updateAcceptedPhysicalYaw/nativePhysicalBodyTurnX.
struct BodyYawFollow {
 float accepted{},previous{},command{},sign{-1};bool previousValid{},snapArmed{true};
 void snap(float stick,float degrees,bool enabled){if(!enabled||std::abs(stick)<.25f)snapArmed=true;if(enabled&&snapArmed&&std::abs(stick)>.7f){accepted=wrapYaw(accepted+std::copysign(degrees,stick));snapArmed=false;}}
 uint64_t turnTick{};
 void smooth(float stick,float speed,bool enabled,uint64_t now){
  const auto elapsed=turnTick&&now>=turnTick?now-turnTick:0;turnTick=now;
  // A stalled frame must not turn the player through the whole missing time.
  if(enabled&&elapsed<=250&&std::isfinite(stick)&&std::isfinite(speed)&&std::abs(stick)>.15f)
   accepted=wrapYaw(accepted+std::copysign((std::min(std::abs(stick),1.f)-.15f)/.85f,stick)*speed*float(elapsed)*.001f);
 }
 void observe(float yaw){
  if(previousValid){const float delta=wrapYaw(yaw-previous);
   if(std::abs(command)>.001f&&std::abs(delta)<30.f){accepted=wrapYaw(accepted+delta);if(std::abs(delta)>.01f)sign=delta*command>=0?1.f:-1.f;}}
  previous=yaw;previousValid=true;
 }
 float request(float residual,bool manual,bool active){
  command=0;if(active&&!manual&&std::isfinite(residual)&&std::abs(residual)>.35f)
   command=std::copysign(std::clamp(std::abs(residual)/90.f,.29f,1.f),residual)/sign;
  return command;
 }
 XrQuaternionf reference(XrQuaternionf initial)const{return product(initial,yawQuaternion(accepted));}
};
}
