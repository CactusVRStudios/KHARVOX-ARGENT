#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace argent {
struct RenderResolution {
 uint32_t sourceWidth{},sourceHeight{};
 float engineScale=1.f;
 bool fsrUpscale{};
};
// Preserve the launcher's existing percentage of pixel count. FSR receives
// reduced engine OUTPUT images, with native internal resolution scaling off.
inline RenderResolution renderResolution(uint32_t width,uint32_t height,float scale,bool fsr){
 if(!std::isfinite(scale))scale=1.f;scale=std::clamp(scale,.4f,2.f);
 RenderResolution plan{width,height,scale,false};
 if(fsr&&scale<1.f&&width>=64&&height>=64){
  auto dimension=[&](uint32_t full){return std::min(full,std::max(64u,uint32_t(std::ceil(double(full)*std::sqrt(double(scale))/2.))*2));};
  plan.sourceWidth=dimension(width);plan.sourceHeight=dimension(height);
  plan.engineScale=1.f;plan.fsrUpscale=plan.sourceWidth<width||plan.sourceHeight<height;
 }
 return plan;
}
}
