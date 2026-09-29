#pragma once
#include "EternalCameraMath.h"
#include <algorithm>
#include <cstdint>
namespace argent::camera {
struct RoomscaleFollow {
 XrVector3f accepted{},previousPhysics{},worldDirection{},trackingDirection{};
 bool previousValid{},commanded{};uintptr_t owner{};
 XrVector2f update(uintptr_t who,XrVector3f physics,XrVector3f head,XrVector3f zero,
                  const Basis& body,XrQuaternionf reference,float units,bool manual,bool enabled){
  if(!enabled||!who||!validPosition(physics)||!validBasis(body)){previousValid=false;commanded=false;return {};}
  if(owner!=who){*this={};owner=who;}
  const float dx=head.x-zero.x-accepted.x,dz=head.z-zero.z-accepted.z;
  const float distance=std::hypot(dx,dz);
  if(previousValid&&commanded&&!manual){
   const float moved=(physics.x-previousPhysics.x)*worldDirection.x+(physics.y-previousPhysics.y)*worldDirection.y;
   if(moved>0&&moved<units*.12f){const float meters=std::min(moved/units,distance);accepted.x+=trackingDirection.x*meters;accepted.z+=trackingDirection.z*meters;}
  }
  previousPhysics=physics;previousValid=true;commanded=false;
  const float x=head.x-zero.x-accepted.x,z=head.z-zero.z-accepted.z,len=std::hypot(x,z);
  if(manual||len<.0025f||len>2.f)return {};
  const float yaw=std::atan2(2*(reference.w*reference.y+reference.x*reference.z),1-2*(reference.x*reference.x+reference.y*reference.y));
  const float right=(std::cos(yaw)*x-std::sin(yaw)*z)/len,forward=-(std::sin(yaw)*x+std::cos(yaw)*z)/len;
  worldDirection={body[0]*forward-body[3]*right,body[1]*forward-body[4]*right,0};
  trackingDirection={x/len,0,z/len};commanded=true;
  // Speed is feedback-controlled; only accepted engine displacement changes
  // the tracking origin. Walls never become a direct physics teleport.
  constexpr float deadzone=7849.f/32767.f;
  const float magnitude=std::clamp(deadzone+std::min(len*20.f,5.5f)/5.5f*(1-deadzone),deadzone+.01f,1.f);
  return {right*magnitude,forward*magnitude};
 }
};
}
