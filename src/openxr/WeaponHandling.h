#pragma once
#include "../EternalCameraMath.h"
#include <algorithm>
namespace argent::input {
using camera::product;
inline XrVector3f add(XrVector3f a,XrVector3f b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline XrVector3f sub(XrVector3f a,XrVector3f b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline XrVector3f mul(XrVector3f a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float dot(XrVector3f a,XrVector3f b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline XrVector3f cross(XrVector3f a,XrVector3f b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(XrVector3f a){return std::sqrt(dot(a,a));}
inline XrVector3f unit(XrVector3f a){auto n=length(a);return n>.0001f?mul(a,1/n):XrVector3f{};}
inline XrQuaternionf inverse(XrQuaternionf q){return {-q.x,-q.y,-q.z,q.w};}
inline XrVector3f rotate(XrQuaternionf q,XrVector3f v){auto r=product(product(q,{v.x,v.y,v.z,0}),inverse(q));return {r.x,r.y,r.z};}
inline XrQuaternionf normalized(XrQuaternionf q){float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return n>.0001f?XrQuaternionf{q.x/n,q.y/n,q.z/n,q.w/n}:XrQuaternionf{0,0,0,1};}
inline XrQuaternionf euler(float pitch,float yaw,float roll){constexpr float h=.00872664626f;return product(product({0,std::sin(yaw*h),0,std::cos(yaw*h)},{std::sin(pitch*h),0,0,std::cos(pitch*h)}),{0,0,std::sin(roll*h),std::cos(roll*h)});}
inline XrQuaternionf fromTo(XrVector3f a,XrVector3f b){a=unit(a);b=unit(b);float d=std::clamp(dot(a,b),-1.f,1.f);if(d<-.999f){auto c=unit(cross(a,std::abs(a.x)<.8f?XrVector3f{1,0,0}:XrVector3f{0,1,0}));return {c.x,c.y,c.z,0};}auto c=cross(a,b);return normalized({c.x,c.y,c.z,1+d});}
struct PoseCalibration {XrVector3f offset{};XrQuaternionf rotation{0,0,0,1};};
struct WeaponCalibration {PoseCalibration pose;XrVector3f support{0,0,-.30f};float radius{.14f},weight{1};bool twoHand{true};};
inline XrPosef calibrated(XrPosef aim,XrPosef grip,const PoseCalibration& hand,const WeaponCalibration& weapon){
 XrPosef result;result.orientation=normalized(product(product(aim.orientation,hand.rotation),weapon.pose.rotation));
 result.position=add(add(grip.position,rotate(grip.orientation,hand.offset)),rotate(result.orientation,weapon.pose.offset));return result;
}
// KHARVOX calibrated support-vector contract: do not assume the support hand
// lies on a universal barrel axis. Preserve primary-hand roll with shortest arc.
struct TwoHandSupport {
 bool latched{},wasGrip{};
 XrQuaternionf update(XrPosef primary,XrPosef support,bool valid,bool grip,const WeaponCalibration& c){
  const float distance=length(sub(support.position,add(primary.position,rotate(primary.orientation,c.support))));
  const float separation=length(sub(support.position,primary.position));
  if(!grip||!c.twoHand)latched=false;
  else if(!latched&&!wasGrip&&valid&&separation>=.08f&&separation<=1.2f&&distance<=c.radius)latched=true;
  wasGrip=grip;
  if(!latched||!valid)return primary.orientation;
  auto correction=fromTo(rotate(primary.orientation,c.support),sub(support.position,primary.position));
  if(correction.w<0){correction.x=-correction.x;correction.y=-correction.y;correction.z=-correction.z;correction.w=-correction.w;}
  const float w=std::clamp(c.weight,0.f,1.f);correction=normalized({correction.x*w,correction.y*w,correction.z*w,1+(correction.w-1)*w});
  return normalized(product(correction,primary.orientation));
 }
};
}
