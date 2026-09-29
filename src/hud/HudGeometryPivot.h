#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace argent::hud {
// Suppress only the generated draw geometry. Native SWF update/render must
// continue running because it also advances timelines and animation events.
inline bool hideGraphic(unsigned char* vertices,int count){
 if(!vertices||count<=0||count>=0x4000)return false;
 const float zero[3]{};
 for(int i=0;i<count;++i)std::memcpy(vertices+i*48,zero,sizeof(zero));
 return true;
}
struct GraphicBounds {float minX{},minY{},maxX{},maxY{};};
// Eternal's GUI upload: 48-byte vertices (XY at 0/4, RGBA at 28),
// uint16 indices. Measure drawn triangles, excluding fully transparent ones.
inline bool graphicBounds(const unsigned char* vertices,int count,const uint16_t* indices,int indexCount,GraphicBounds& out){
 if(!vertices||!indices||count<=0||count>=0x4000||indexCount<3||indexCount>=0x8000)return false;
 GraphicBounds b{1e20f,1e20f,-1e20f,-1e20f};
 for(int i=0;i+2<indexCount;i+=3){
  const auto a=indices[i],c=indices[i+1],d=indices[i+2];
  if(a>=count||c>=count||d>=count)return false;
  if(!(vertices[a*48+31]|vertices[c*48+31]|vertices[d*48+31]))continue;
  float xy[3][2];int k=0;
  for(auto v:{a,c,d}){std::memcpy(xy[k],vertices+v*48,8);if(!std::isfinite(xy[k][0])||!std::isfinite(xy[k][1]))return false;++k;}
  if(std::abs((xy[1][0]-xy[0][0])*(xy[2][1]-xy[0][1])-(xy[2][0]-xy[0][0])*(xy[1][1]-xy[0][1]))<1e-6f)continue;
  for(auto v:{a,c,d}){float xy[2];std::memcpy(xy,vertices+v*48,8);
   if(!std::isfinite(xy[0])||!std::isfinite(xy[1])||std::abs(xy[0])>1e6f||std::abs(xy[1])>1e6f)return false;
   b.minX=std::min(b.minX,xy[0]);b.maxX=std::max(b.maxX,xy[0]);b.minY=std::min(b.minY,xy[1]);b.maxY=std::max(b.maxY,xy[1]);
  }
 }
 if(b.maxX-b.minX<.01f||b.maxY-b.minY<.01f)return false;
 out=b;return true;
}
// Each native 0x88-byte draw surface owns a vertex slice and LOCAL indices.
// Adjacent materials restart their index base at zero; never read the aggregate
// index array as a single mesh. Native odd index padding is surface-local too.
inline bool submittedGraphicBounds(const unsigned char* vertices,int count,const uint16_t* indices,int indexCount,
 const unsigned char* surfaces,int surfaceCount,GraphicBounds& out){
 if(!vertices||!indices||!surfaces||count<=0||count>=0x4000||indexCount<3||indexCount>=0x8000||surfaceCount<=0||surfaceCount>indexCount/3)return false;
 GraphicBounds total{1e20f,1e20f,-1e20f,-1e20f};bool found=false;
 for(int n=0;n<surfaceCount;++n){
  int firstVertex{},vertexCount{},firstIndex{},numIndices{};
  const auto* s=surfaces+n*0x88;
  std::memcpy(&firstVertex,s+0x10,4);std::memcpy(&vertexCount,s+0x14,4);
  std::memcpy(&firstIndex,s+0x18,4);std::memcpy(&numIndices,s+0x1c,4);
  if(firstVertex<0||vertexCount<=0||firstVertex>count-vertexCount||firstIndex<0||numIndices<3||firstIndex>indexCount-numIndices)return false;
  for(int i=0;i<numIndices;++i)if(indices[firstIndex+i]>=vertexCount)return false;
  GraphicBounds b;
  if(!graphicBounds(vertices+firstVertex*48,vertexCount,indices+firstIndex,numIndices,b))continue;
  total.minX=std::min(total.minX,b.minX);total.minY=std::min(total.minY,b.minY);
  total.maxX=std::max(total.maxX,b.maxX);total.maxY=std::max(total.maxY,b.maxY);found=true;
 }
 if(found)out=total;return found;
}
// Rebase artwork in local pixel space BEFORE upload. Native world transform
// already anchors the chosen canvas pivot at the grip. UVs, colors, materials,
// indices and Z remain unchanged. Width now refers to artwork, not padding.
inline bool centerGraphic(unsigned char* vertices,int count,const GraphicBounds& b,float canvasWidth,float canvasHeight,float pivotX,float pivotY,bool fitWidth=true){
 const float w=b.maxX-b.minX,h=b.maxY-b.minY;
 if(!vertices||count<=0||count>=0x4000||!std::isfinite(w+h+canvasWidth+canvasHeight+pivotX+pivotY)||w<.01f||h<.01f||canvasWidth<=0||canvasHeight<=0||pivotX<0||pivotX>1||pivotY<0||pivotY>1)return false;
 const float factor=fitWidth?canvasWidth/w:1.f,cx=b.minX+pivotX*w,cy=b.minY+pivotY*h;
 // Validate all writes before touching the native batch.
 for(int i=0;i<count;++i){float xy[2];std::memcpy(xy,vertices+i*48,8);if(!std::isfinite(xy[0])||!std::isfinite(xy[1]))return false;}
 for(int i=0;i<count;++i){float xy[2];std::memcpy(xy,vertices+i*48,8);
  xy[0]=(xy[0]-cx)*factor+canvasWidth*pivotX;xy[1]=(xy[1]-cy)*factor+canvasHeight*pivotY;
  std::memcpy(vertices+i*48,xy,8);
 }
 return true;
}
inline bool centerGraphicMetric(unsigned char* vertices,int count,const GraphicBounds& b,float canvasWidth,float canvasHeight,float canvasExtent,float artworkMeters){
 if(!std::isfinite(canvasExtent+artworkMeters)||canvasExtent<=0||artworkMeters<=0)return false;
 const float pixelWidth=canvasWidth*artworkMeters/canvasExtent;
 const float graphicWidth=b.maxX-b.minX;
 if(!std::isfinite(pixelWidth)||graphicWidth<=0)return false;
 // Reuse the validated vertex transform, then scale about the canvas center.
 if(!centerGraphic(vertices,count,b,canvasWidth,canvasHeight,.5f,.5f,false))return false;
 const float factor=pixelWidth/graphicWidth;
 for(int i=0;i<count;++i){float xy[2];std::memcpy(xy,vertices+i*48,8);
  xy[0]=(xy[0]-canvasWidth*.5f)*factor+canvasWidth*.5f;
  xy[1]=(xy[1]-canvasHeight*.5f)*factor+canvasHeight*.5f;
  std::memcpy(vertices+i*48,xy,8);
 }
 return true;
}
}
