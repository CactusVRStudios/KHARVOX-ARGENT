#pragma once
#include <cmath>
namespace argent::player {
// XInput already rotates horizontal movement into the native body frame.
// Tilt that frame toward HMD forward without applying HMD yaw a second time.
inline bool swimmingBasis(const float* nativeRight,const float* head,float* forward,float* right) noexcept {
 if(!nativeRight||!head||!forward||!right)return false;
 float norm=0;for(int i=0;i<3;++i){if(!std::isfinite(head[i])||!std::isfinite(nativeRight[i]))return false;norm+=head[i]*head[i];}
 if(std::abs(norm-1.f)>.02f)return false;
 const float nr=std::hypot(nativeRight[0],nativeRight[1]);if(nr<.05f)return false;
 const float rx=nativeRight[0]/nr,ry=nativeRight[1]/nr;
 const float horizontal=std::hypot(head[0],head[1]);
 const float hx=horizontal>.0001f?head[0]/horizontal:-ry;
 const float hy=horizontal>.0001f?head[1]/horizontal:rx;
 const float inverse=1.f/std::sqrt(norm);
 auto tilt=[&](float x,float y,float* out){
  const float along=x*hx+y*hy,across=x*hy-y*hx;
  out[0]=along*head[0]*inverse+across*hy;
  out[1]=along*head[1]*inverse-across*hx;
  out[2]=along*head[2]*inverse;
 };
 tilt(-ry,rx,forward);tilt(rx,ry,right);return true;
}
}
