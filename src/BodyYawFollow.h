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
 float carrierCommand{};unsigned carrierTail{};
 void snap(float stick,float degrees,bool enabled){if(!enabled||std::abs(stick)<.25f)snapArmed=true;if(enabled&&snapArmed&&std::abs(stick)>.7f){accepted=wrapYaw(accepted+std::copysign(degrees,stick));snapArmed=false;}}
 double turnTick{},integratedTick{};float smoothRate{};
 void smooth(float stick,float speed,bool enabled,double now){
  const auto elapsed=integratedTick&&now>=integratedTick?now-integratedTick:0;turnTick=integratedTick=now;
  smoothRate=0;
  // A stalled frame must not turn the player through the whole missing time.
  if(enabled&&std::isfinite(stick)&&std::isfinite(speed)&&std::abs(stick)>.15f){
   smoothRate=std::copysign((std::min(std::abs(stick),1.f)-.15f)/.85f,stick)*speed;
   if(elapsed<=100)accepted=wrapYaw(accepted+smoothRate*float(elapsed)*.001f);
  }
 }
 // PR #15 principle: use elapsed CPU time, never the FG runtime's predicted
 // display-time jumps. Consume each interval once. A release publishes rate=0.
 void advanceForRender(double now){
  if(!smoothRate||!turnTick||now<=integratedTick||now-turnTick>100)return;
  accepted=wrapYaw(accepted+smoothRate*float(now-integratedTick)*.001f);integratedTick=now;
 }
 void observe(float yaw){
  if(previousValid){const float delta=wrapYaw(yaw-previous);
   const float carrier=std::abs(command)>.001f?command:carrierTail?carrierCommand:0.f;
   if(std::abs(carrier)>.001f&&std::abs(delta)<30.f){accepted=wrapYaw(accepted+delta);if(std::abs(delta)>.01f)sign=delta*carrier>=0?1.f:-1.f;}}
  previous=yaw;previousValid=true;
 }
 float request(float residual,bool manual,bool active){
  const float previousCommand=command;
  command=0;if(active&&!manual&&std::isfinite(residual)&&std::abs(residual)>.35f)
   command=std::copysign(std::clamp(std::abs(residual)/90.f,.29f,1.f),residual)/sign;
  // Native body turning can retire after the stick target already stopped.
  // Keep its last command for two publications, without extending the stick turn.
  if(!active||manual){carrierTail=0;carrierCommand=0;}
  else if(std::abs(command)>.001f){carrierCommand=command;carrierTail=2;}
  else if(std::abs(previousCommand)>.001f){carrierCommand=previousCommand;carrierTail=2;}
  else if(carrierTail)--carrierTail;
  return command;
 }
 XrQuaternionf reference(XrQuaternionf initial)const{return product(initial,yawQuaternion(accepted));}
};
}
