#pragma once
#include <cmath>
namespace argent::camera {
// Preserve the native near/far reach. Invalid input leaves all fields intact.
inline bool headFocusTrace(float* start,float* nearEnd,float* farEnd,const float* origin,const float* forward){
 float reach[2]{};float* ends[]={nearEnd,farEnd};
 float norm=0;
 for(int i=0;i<3;++i){if(!std::isfinite(origin[i])||!std::isfinite(forward[i])||!std::isfinite(start[i]))return false;norm+=forward[i]*forward[i];}
 if(!std::isfinite(norm)||norm<.99f||norm>1.01f)return false;
 for(int e=0;e<2;++e){
  for(int i=0;i<3;++i){const float d=ends[e][i]-start[i];reach[e]+=d*d;}
  reach[e]=std::sqrt(reach[e]);
  if(!std::isfinite(reach[e])||reach[e]<.01f||reach[e]>10000.f)return false;
 }
 for(int i=0;i<3;++i){start[i]=origin[i];nearEnd[i]=origin[i]+forward[i]*reach[0];farEnd[i]=origin[i]+forward[i]*reach[1];}
 return true;
}
}
