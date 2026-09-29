#pragma once
#include "HudGeometryPivot.h"
#include "../hands/HandHudMask.h"
namespace argent::hud {
// Preserve gaps between native triangles. Material bounds are not coverage;
// texture-alpha holes still require a separate sampled-alpha mask.
inline kharvox::hands::HandHudPanels handMaskGeometry(
 const unsigned char* vertices,int count,const uint16_t* indices,int indexCount,
 const unsigned char* surfaces,int surfaceCount,const float* origin,
 const float* axis,float scaleX,float scaleY){
 kharvox::hands::HandHudPanels result;
 GraphicBounds bounds;
 if(!origin||!axis||!std::isfinite(scaleX)||!std::isfinite(scaleY)||
    !submittedGraphicBounds(vertices,count,indices,indexCount,surfaces,surfaceCount,bounds))return result;
 for(int n=0;n<surfaceCount;++n){
  int firstVertex{},vertexCount{},firstIndex{},numIndices{};
  const auto* s=surfaces+n*0x88;
  std::memcpy(&firstVertex,s+0x10,4);std::memcpy(&vertexCount,s+0x14,4);
  std::memcpy(&firstIndex,s+0x18,4);std::memcpy(&numIndices,s+0x1c,4);
  const auto* v=vertices+firstVertex*48;
  for(int i=0;i+2<numIndices;i+=3){
   const auto* triangle=indices+firstIndex+i;
   if(!graphicBounds(v,vertexCount,triangle,3,bounds))continue;
   kharvox::hands::HandHudQuad panel{};
   for(int corner=0;corner<3;++corner){
    float xy[2];std::memcpy(xy,v+triangle[corner]*48,8);
    for(int k=0;k<3;++k){
     panel[corner][k]=origin[k]+axis[k]*xy[0]*scaleX+axis[3+k]*xy[1]*scaleY;
     if(!std::isfinite(panel[corner][k]))return {};
    }
   }
   // Existing strip pipeline: second triangle is degenerate, not a rectangle.
   panel[3]=panel[2];result.push_back(panel);
   if(result.size()>4096)return {};
  }
 }
 return result;
}
}
