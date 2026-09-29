#pragma once
#include <cmath>
#include <cstring>
namespace argent::player {
// Move only the current weapon attachment between controller frames. The
// shared animated hands root must never retain a weapon-specific correction.
inline bool calibrateWeaponAttachment(const float* fromPosition,const float* fromAxis,
 const float* toPosition,const float* toAxis,float* position,float* axis){
 float local[3]{},rotation[9]{},nextPosition[3]{},nextAxis[9]{};
 for(int k=0;k<3;++k)if(!std::isfinite(fromPosition[k])||!std::isfinite(toPosition[k])||!std::isfinite(position[k]))return false;
 for(int k=0;k<9;++k)if(!std::isfinite(fromAxis[k])||!std::isfinite(toAxis[k])||!std::isfinite(axis[k]))return false;
 for(int r=0;r<3;++r){
  for(int k=0;k<3;++k)local[r]+=(position[k]-fromPosition[k])*fromAxis[r*3+k];
  for(int c=0;c<3;++c)for(int k=0;k<3;++k)rotation[r*3+c]+=axis[r*3+k]*fromAxis[c*3+k];
 }
 for(int k=0;k<3;++k){nextPosition[k]=toPosition[k];for(int r=0;r<3;++r)nextPosition[k]+=local[r]*toAxis[r*3+k];}
 for(int r=0;r<3;++r)for(int k=0;k<3;++k)for(int c=0;c<3;++c)nextAxis[r*3+k]+=rotation[r*3+c]*toAxis[c*3+k];
 std::memcpy(position,nextPosition,sizeof(nextPosition));std::memcpy(axis,nextAxis,sizeof(nextAxis));return true;
}
inline bool matchingWeaponCalibration(const char* sampled,const char* equipped){
 return sampled&&equipped&&sampled[0]&&std::strcmp(sampled,equipped)==0;
}
}
