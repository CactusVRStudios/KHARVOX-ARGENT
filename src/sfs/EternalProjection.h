#pragma once
#include "EyeProjection.h"
#include <cstring>
namespace argent::sfs {
// Eternal common frame UBO, observed in the captured world/depth shaders.
// Version-specific experimental contract, independent of DOOM 2016 offsets.
inline bool eternalProjection(const void* bytes,size_t count,Matrix& output){
 if(!bytes||count<108)return false;
 float v[27];std::memcpy(v,bytes,sizeof(v));
 for(float f:v)if(!std::isfinite(f))return false;
 if(v[5]<=.05f||v[5]>20||v[8]<64||v[9]<64)return false;
 if(v[2]>=-.9f||v[2]<-1.1f||v[3]>=0||v[3]<-10)return false;
 // Frame constants contain rotated world-space ray steps, not reciprocal
 // scales at floats 19/20. Their lengths are invariant under camera rotation.
 float horizontal=std::sqrt(v[21]*v[21]+v[22]*v[22]+v[23]*v[23]);
 float vertical=std::sqrt(v[24]*v[24]+v[25]*v[25]+v[26]*v[26]);
 if(horizontal<.1f||horizontal>40||vertical<.1f||vertical>40)return false;
 float dot=v[21]*v[24]+v[22]*v[25]+v[23]*v[26];
 if(std::abs(dot)>horizontal*vertical*.01f)return false;
 // Eternal clamps its projection aspect to at least 1 even when the render
 // target is portrait (e.g. 2496x2688 OpenXR eyes). The ray axes describe the
 // actual projection; rejecting their square aspect leaves identity stereo.
 const float projectionAspect=std::max(1.f,v[8]/v[9]);
 if(std::abs(horizontal/vertical-projectionAspect)>.03f||std::abs(2/vertical-v[5])>.03f)return false;
 Matrix p{};p[0]=2/horizontal;p[5]=-2/vertical;p[10]=v[2];p[11]=-1;p[14]=v[3];
 output=p;return true;
}
}
