#pragma once
#include "sfs/EyeProjection.h"
namespace argent::camera {
// Angular envelope in head coordinates. Use both eyes, including asymmetric
// frusta/canted displays. Translation is not an angular FOV; retain a margin.
inline bool headsetEnvelope(const XrPosef& head,const std::array<XrView,2>& eyes,float& x,float& y){
 sfs::Matrix h,hi;if(!sfs::poseMatrix(head,1,h)||!sfs::inverse(h,hi))return false;
 float tx=0,ty=0;
 for(const auto& eye:eyes){sfs::Matrix e;if(!sfs::poseMatrix(eye.pose,1,e))return false;
  const auto rotation=sfs::multiply(hi,e);auto f=eye.fov;
  if(!(f.angleLeft<0&&f.angleRight>0&&f.angleDown<0&&f.angleUp>0))return false;
  for(float a:{f.angleLeft,f.angleRight,f.angleDown,f.angleUp})if(!std::isfinite(a)||std::abs(a)>=1.55f)return false;
  for(float ax:{f.angleLeft,f.angleRight})for(float ay:{f.angleDown,f.angleUp}){
   const float vx=std::tan(ax),vy=std::tan(ay);
   const float rx=rotation[0]*vx+rotation[4]*vy-rotation[8];
   const float ry=rotation[1]*vx+rotation[5]*vy-rotation[9];
   const float rz=rotation[2]*vx+rotation[6]*vy-rotation[10];
   if(rz>=-.01f)return false;tx=std::max(tx,std::abs(rx/rz));ty=std::max(ty,std::abs(ry/rz));
  }
 }
 constexpr float margin=2.f*3.14159265f/180.f;
 if(std::atan(tx)+margin>=1.55f||std::atan(ty)+margin>=1.55f)return false;
 x=std::tan(std::atan(tx)+margin);y=std::tan(std::atan(ty)+margin);return true;
}
inline int engineFov(float requiredX,float requiredY,float observedX,float observedY,int current){
 if(current<10||current>150||!std::isfinite(requiredX+requiredY+observedX+observedY)||requiredX<=0||requiredY<=0||observedX<=0||observedY<=0)return 0;
 const float ratio=std::max(requiredX/observedX,requiredY/observedY);
 const float angle=2*std::atan(std::tan(current*3.14159265f/360.f)*ratio)*180.f/3.14159265f;
 return std::clamp(int(std::ceil(angle)),10,150);
}
}
