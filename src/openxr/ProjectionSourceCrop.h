#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace kharvox {
// Normalized source rectangle in tangent space, ordered left/right/top/bottom.
// Pixels and submitted FOV must describe the same rays, including asymmetric eyes.
template<class Fov>
std::array<float,4> projectionSourceCrop(const Fov& rendered,const Fov& submitted){
 const float l=std::tan(rendered.angleLeft),r=std::tan(rendered.angleRight);
 const float u=std::tan(rendered.angleUp),d=std::tan(rendered.angleDown);
 return {std::clamp((std::tan(submitted.angleLeft)-l)/(r-l),0.f,1.f),
  std::clamp((std::tan(submitted.angleRight)-l)/(r-l),0.f,1.f),
  std::clamp((u-std::tan(submitted.angleUp))/(u-d),0.f,1.f),
  std::clamp((u-std::tan(submitted.angleDown))/(u-d),0.f,1.f)};
}
}
