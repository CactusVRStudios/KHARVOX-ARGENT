#pragma once
#include "HudLayoutPolicy.h"
#include "../EternalCameraMath.h"
#include <istream>
#include <ostream>
#include <iomanip>
#include <algorithm>
namespace argent::hud {
inline Calibration defaultCompassCalibration(){return {1.f,.5f,0.f,-.2f};}
// Rebuild the full authored canvas in the current view, not in the native
// compass owner's unrelated/stale HUD camera. Pixel positions stay authored.
inline bool compassCanvas(const float* eye,const float* head,float units,float tanX,float tanY,
 Calibration c,float* origin,float* axis,float& width,float& height){
 if(!eye||!head||!origin||!axis||!valid(c)||!std::isfinite(units+tanX+tanY)||units<=0||tanX<=0||tanY<=0)return false;
 camera::Basis basis;for(int k=0;k<9;++k)basis[k]=head[k];
 if(!camera::validBasis(basis))return false;
 for(int k=0;k<3;++k)if(!std::isfinite(eye[k]))return false;
 canvasAxes(head,axis);
 width=2*c.distance*units*tanX;height=2*c.distance*units*tanY;
 for(int k=0;k<3;++k)origin[k]=eye[k]+units*(head[k]*c.distance-head[3+k]*c.right+head[6+k]*c.up)-axis[k]*width*.5f-axis[3+k]*height*.5f;
 return true;
}
inline bool readCompassCalibration(std::istream& in,Calibration& output){
 int version{};Calibration c;
 if(!(in>>version>>c.distance>>c.scale>>c.right>>c.up)||version!=1||!valid(c))return false;
 output=c;return true;
}
inline void writeCompassCalibration(std::ostream& out,Calibration c){
 out<<std::setprecision(9)<<"1 "<<c.distance<<' '<<c.scale<<' '<<c.right<<' '<<c.up<<'\n';
}
inline Calibration adjustCompass(Calibration c,int x,int y,int z,int size,bool fine,bool reset){
 if(reset)return defaultCompassCalibration();
 const float step=fine?.0025f:.005f;
 c.right=std::clamp(c.right+x*step,-2.f,2.f);c.up=std::clamp(c.up+y*step,-2.f,2.f);
 c.distance=std::clamp(c.distance-z*step,.3f,3.f);
 c.scale=std::clamp(c.scale+size*(fine?.005f:.01f),.1f,2.f);
 return c;
}
}
