#pragma once
#include "OffhandLayout.h"
namespace argent::hud {
struct HandHudPivot {bool enabled{};std::array<float,3> centimeters{};};
inline bool readHandHudPivots(std::istream& in,std::array<HandHudPivot,2>& out){
 int version{};std::array<HandHudPivot,2> next{};
 if(!(in>>version)||version!=1)return false;
 for(auto& p:next){int enabled{};if(!(in>>enabled)||(enabled!=0&&enabled!=1))return false;p.enabled=enabled!=0;
  for(float& v:p.centimeters)if(!(in>>v)||!std::isfinite(v)||std::abs(v)>100)return false;}
 out=next;return true;
}
struct HandHudGroup {
 std::array<float,3> pivot{},sourceCenter{};
 std::array<float,9> rotation{1,0,0,0,1,0,0,0,1};
 void vector(float* v) const {
  const std::array<float,3> p{v[0],v[1],v[2]};
  for(int k=0;k<3;++k){v[k]=0;for(int j=0;j<3;++j)v[k]+=rotation[k*3+j]*p[j];}
 }
 void point(float* p) const {
  // Rotate the artwork around its own center, then place it on the hand anchor.
  // Rotating around the target anchor retained the accumulated layout offset.
  for(int k=0;k<3;++k)p[k]-=sourceCenter[k];
  vector(p);for(int k=0;k<3;++k)p[k]+=pivot[k];
 }
 void canvas(float* origin,float* axes) const {
  point(origin);for(int row=0;row<3;++row)vector(axes+row*3);
 }
};
// One stable parent for all configured roles, including currently hidden ones.
// Hand tracking controls the anchor, not orientation. The common artwork frame
// stays world-upright and faces the camera in yaw, independently of hand tilt.
// HMD pitch/roll never tilt the billboard; all elements share one rigid frame.
inline HandHudGroup handHudGroup(const HandPanels& panels,bool left,
 const float* grip,const float* hand,const float* eye,float units,const HandHudPivot& fixed={}){
 HandHudGroup result;
 if(!grip||!hand||!eye||!std::isfinite(units)||units<=0)return result;
 for(int k=0;k<3;++k)if(!std::isfinite(grip[k])||!std::isfinite(eye[k]))return result;
 for(int k=0;k<9;++k)if(!std::isfinite(hand[k]))return result;
 float reference[9]{};kharvox::offhandHudBasis(hand,panels[1][left].pose,reference);
 int count=0;
 for(int role=0;role<int(panels.size());++role)if(handRole(role)){
  const auto& p=panels[role][left];if(!validPanel(p))return {};
  float basis[9]{};kharvox::offhandHudBasis(hand,p.pose,basis);
  for(int k=0;k<3;++k){
   float center=grip[k];
   for(int j=0;j<3;++j)center+=hand[j*3+k]*p.pose.centimeters[j]*units*.01f;
   center-=basis[3+k]*(.5f-p.pivotX)*p.pose.scale*units;
   result.pivot[k]+=center;
  }
  ++count;
 }
 for(float& v:result.pivot)v/=float(count);
 result.sourceCenter=result.pivot;
 if(fixed.enabled)for(int k=0;k<3;++k){
  result.pivot[k]=grip[k];
  for(int j=0;j<3;++j){if(!std::isfinite(fixed.centimeters[j]))return {};
   result.pivot[k]+=hand[j*3+k]*fixed.centimeters[j]*units*.01f;}
 }
 float x=result.pivot[0]-eye[0],y=result.pivot[1]-eye[1];
 const float length=std::hypot(x,y);
 // Directly above/below the anchor, yaw is undefined: use a stable world axis.
 if(length<units*.001f){x=1.f;y=0.f;}else{x/=length;y/=length;}
 const float target[]{x,y,0,-y,x,0,0,0,1};
 for(int k=0;k<3;++k)for(int j=0;j<3;++j){
  result.rotation[k*3+j]=0;
  for(int row=0;row<3;++row)result.rotation[k*3+j]+=target[row*3+k]*reference[row*3+j];
 }
 return result;
}
}
