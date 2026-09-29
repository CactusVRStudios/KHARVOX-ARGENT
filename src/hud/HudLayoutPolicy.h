#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace argent::hud {
struct Role {uintptr_t vtable;const char* name;};
// Complete-object RTTI verified in the supported Eternal executable.
inline constexpr std::array<Role,16> roles{{
 {0x2d03ea8,"WeaponWheel"},{0x2cfedd8,"HealthInfo"},{0x2d03798,"WeaponInfo"},
 {0x2cfa878,"AbilityIndicators"},{0x2cfab88,"BloodPunch"},{0x2cfd998,"Dash"},
 {0x2cfe9e8,"ExtraLives"},{0x2cfb9a0,"Compass"},{0x2cff218,"Keycard"},
 {0x2cff3d8,"LowWarning"},{0x2cfafa0,"BossVitals"},{0x2d01c20,"RuneInfo"},
 {0x2d02748,"Subtitles"},{0x2d03420,"TutorialObjectives"},
 {0x2cff930,"MissionChallenge"},{0x2d04878,"Notification_Objectives"}
}};
struct Calibration {float distance=1.f,scale=.7f,right=0,up=0;};
inline bool valid(Calibration c,float maximumScale=2.f){return std::isfinite(c.distance+c.scale+c.right+c.up)&&c.distance>=.3f&&c.distance<=3.f&&c.scale>=.1f&&c.scale<=maximumScale&&std::abs(c.right)<=2.f&&std::abs(c.up)<=2.f;}
inline int roleIndex(uintptr_t vtable){for(size_t i=0;i<roles.size();++i)if(roles[i].vtable==vtable)return int(i);return -1;}
inline void canvasAxes(const float* head,float* axis){
 for(int i=0;i<3;++i){axis[i]=-head[3+i];axis[3+i]=-head[6+i];axis[6+i]=head[i];}
}
// Preserve authored screen-space layout, but place its plane at a metric depth.
// Source pose must be the native camera passed to HUD update, before authored
// offsets, not the already tilted GUI parent transform.
inline bool layout(const float* origin,const float* axis,const float* eye,const float* camera,
 const float* headEye,const float* head,float units,Calibration c,float* result,float* resultAxis,float& factor){
 if(!valid(c)||!std::isfinite(units)||units<=0)return false;
 float local[3]{};
 for(int r=0;r<3;++r)for(int i=0;i<3;++i)local[r]+=(origin[i]-eye[i])*camera[r*3+i];
 const float depth=local[0];
 // Reject unrecognized/world-like canvases instead of pulling them to the face.
 if(!std::isfinite(depth)||depth<.05f*units||depth>100.f*units)return false;
 factor=c.distance*units/depth*c.scale;
 local[0]=c.distance*units;local[1]=local[1]*factor-c.right*units;local[2]=local[2]*factor+c.up*units;
 for(int i=0;i<3;++i){
  result[i]=headEye[i];for(int r=0;r<3;++r)result[i]+=local[r]*head[r*3+i];
  if(!std::isfinite(result[i]))return false;
 }
 for(int row=0;row<3;++row)for(int i=0;i<3;++i){
  float v=0;for(int r=0;r<3;++r){float x=0;for(int j=0;j<3;++j)x+=axis[row*3+j]*camera[r*3+j];v+=x*head[r*3+i];}
  if(!std::isfinite(v))return false;resultAxis[row*3+i]=v;
 }
 return true;
}
}
