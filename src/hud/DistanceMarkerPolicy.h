#pragma once
#include "HudGeometryPivot.h"
#include "../EternalCameraMath.h"
#include <string_view>
namespace argent::hud {
inline constexpr uintptr_t distanceMarkerVtable=0x2d004d8;
inline bool objectiveMarker(std::string_view name){return name=="objective_marker_major"||name=="objective_marker_minor";}
inline float objectiveScale(float native,bool objective,bool gameplay){
 return objective&&gameplay&&std::isfinite(native)&&native>0?native/3.f:native;
}
struct DistanceMarkerCanvas {
 std::array<float,3> origin{},eye{};
 camera::Basis axis{};
 float pixelX{},pixelY{};
 bool screenSpace{};
 camera::Basis head{};
 float tanX{},tanY{};
 // Native per-entry scaling can already have applied the size correction.
 float geometryScale=1.f/3.f;
};
inline bool markerCanvasCaller(uintptr_t relative,bool screenSpace){return relative==(screenSpace?0x157cfda:0x157cf4a);}
inline bool screenBillboardDistanceMarker(unsigned char* vertices,int count,const GraphicBounds& b,const DistanceMarkerCanvas& c){
 if(!vertices||count<=0||count>=0x4000||!camera::validBasis(c.head)||
    !std::isfinite(c.pixelX+c.pixelY+c.tanX+c.tanY+c.origin[0]+c.origin[1])||
    c.pixelX<=1e-8f||c.pixelY>=-1e-8f||c.tanX<=0||c.tanY<=0||!std::isfinite(c.geometryScale)||c.geometryScale<=0)return false;
 const float cx=(b.minX+b.maxX)*.5f,cy=(b.minY+b.maxY)*.5f;
 if(!std::isfinite(cx+cy)||b.maxX<=b.minX||b.maxY<=b.minY)return false;
 const float nx=c.origin[0]+cx*c.pixelX,ny=c.origin[1]+cy*c.pixelY;
 std::array<float,3> center{};
 for(int k=0;k<3;++k)center[k]=c.head[k]-c.head[3+k]*nx*c.tanX+c.head[6+k]*ny*c.tanY;
 const float length=std::hypot(center[0],center[1]);if(!std::isfinite(length)||length<1e-5f)return false;
 const float right[3]={center[1]/length,-center[0]/length,0};
 auto transform=[&](int i,float* out){
  std::memcpy(out,vertices+i*48,12);
  const float x=(out[0]-cx)*c.pixelX*c.tanX*c.geometryScale,y=-(out[1]-cy)*c.pixelY*c.tanY*c.geometryScale;
  float delta[3];for(int k=0;k<3;++k)delta[k]=center[k]+right[k]*x-(k==2?y:0);
  float depth{},px{},py{};for(int k=0;k<3;++k){depth+=c.head[k]*delta[k];px-=c.head[3+k]*delta[k];py+=c.head[6+k]*delta[k];}
  if(!std::isfinite(depth)||depth<=1e-5f)return false;
  out[0]=(px/(depth*c.tanX)-c.origin[0])/c.pixelX;
  out[1]=(py/(depth*c.tanY)-c.origin[1])/c.pixelY;
  for(int k=0;k<3;++k)if(!std::isfinite(out[k])||std::abs(out[k])>1e7f)return false;
  return true;
 };
 for(int i=0;i<count;++i){float p[3];if(!transform(i,p))return false;}
 for(int i=0;i<count;++i){float p[3];transform(i,p);std::memcpy(vertices+i*48,p,12);}
 return true;
}
// Reorient submitted artwork in its existing canvas space. Its visible center
// stays fixed in world space; movie padding never becomes the pivot.
inline bool billboardDistanceMarker(unsigned char* vertices,int count,const GraphicBounds& bounds,const DistanceMarkerCanvas& c){
 if(c.screenSpace)return screenBillboardDistanceMarker(vertices,count,bounds,c);
 if(!vertices||count<=0||count>=0x4000||!camera::validBasis(c.axis)||
    !std::isfinite(c.pixelX+c.pixelY+c.geometryScale)||c.pixelX<=1e-8f||c.pixelY<=1e-8f||c.geometryScale<=0)return false;
 for(int k=0;k<3;++k)if(!std::isfinite(c.origin[k])||!std::isfinite(c.eye[k]))return false;
 const float cx=(bounds.minX+bounds.maxX)*.5f,cy=(bounds.minY+bounds.maxY)*.5f;
 if(!std::isfinite(cx+cy)||bounds.maxX<=bounds.minX||bounds.maxY<=bounds.minY)return false;
 std::array<float,3> center{};
 for(int k=0;k<3;++k)center[k]=c.origin[k]+c.axis[k]*cx*c.pixelX+c.axis[3+k]*cy*c.pixelY;
 float nx=center[0]-c.eye[0],ny=center[1]-c.eye[1];float length=std::hypot(nx,ny);
 if(length<1e-5f){nx=c.axis[6];ny=c.axis[7];length=std::hypot(nx,ny);}
 if(!std::isfinite(length)||length<1e-5f)return false;
 nx/=length;ny/=length;
 const float right[3]={ny,-nx,0},down[3]={0,0,-1},normal[3]={nx,ny,0};
 auto transform=[&](int i,float* out){
  float p[3];std::memcpy(p,vertices+i*48,sizeof(p));
  const float x=(p[0]-cx)*c.pixelX*c.geometryScale,y=(p[1]-cy)*c.pixelY*c.geometryScale,z=p[2]*c.geometryScale;
  for(int row=0;row<3;++row){
   float local{};for(int k=0;k<3;++k)local+=(right[k]*x+down[k]*y+normal[k]*z)*c.axis[row*3+k];
   out[row]=(row==0?cx:row==1?cy:0)+local/(row==0?c.pixelX:row==1?c.pixelY:1.f);
   if(!std::isfinite(out[row])||std::abs(out[row])>1e7f)return false;
  }
  return true;
 };
 // Validate the whole batch before changing any vertex. UV/color/material data
 // and native animation remain untouched, including the distance text.
 for(int i=0;i<count;++i){float p[3];if(!transform(i,p))return false;}
 for(int i=0;i<count;++i){float p[3];transform(i,p);std::memcpy(vertices+i*48,p,sizeof(p));}
 return true;
}
}
