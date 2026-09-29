#pragma once
#include <algorithm>
#include <cmath>
namespace argent::camera {
// Native Eternal render-view FOVs are full angles in degrees. Headset envelope
// inputs are tangent half-angles and already include the stereo safety margin.
struct AnimationFovGuard {
 float nativeX{},nativeY{},writtenX{},writtenY{};bool held{};
 void restore(float& x,float& y){
  if(held){if(x==writtenX)x=nativeX;if(y==writtenY)y=nativeY;}
  held=false;
 }
 bool apply(float& x,float& y,float requiredX,float requiredY){
  auto valid=[](float v){return std::isfinite(v)&&v>0.f;};
  if(!valid(x)||!valid(y)||x>=179.f||y>=179.f||!valid(requiredX)||!valid(requiredY))return false;
  const float fx=2.f*std::atan(requiredX)*57.2957795131f;
  const float fy=2.f*std::atan(requiredY)*57.2957795131f;
  if(fx>=179.f||fy>=179.f)return false;
  nativeX=x;nativeY=y;writtenX=std::max(x,fx);writtenY=std::max(y,fy);
  held=writtenX!=x||writtenY!=y;x=writtenX;y=writtenY;return held;
 }
};
}
