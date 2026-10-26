#pragma once
#include <array>
#include <cmath>
namespace argent::fg {
// Eternal's compact camera data can describe forward Z while the associated
// raster pass writes reverse Z. Match the projection to the actual depth image.
inline bool depthProjection(std::array<float,16>& projection,bool inverted,float& nearPlane,float& farPlane){
 if(!std::isfinite(projection[10]+projection[14])||projection[14]==0||std::abs(projection[11]+1)>.001f||std::abs(projection[15])>.001f)return false;
 if((projection[14]>0)!=inverted)for(unsigned column=0;column<4;++column)projection[column*4+2]=projection[column*4+3]-projection[column*4+2];
 const float a=projection[10],b=projection[14];
 nearPlane=inverted?b/(a+1):b/a;
 const float denominator=inverted?a:a+1;
 farPlane=std::abs(denominator)>1e-8f?b/denominator:1000000.f;
 return std::isfinite(nearPlane)&&std::isfinite(farPlane)&&nearPlane>0&&farPlane>nearPlane;
}
}
