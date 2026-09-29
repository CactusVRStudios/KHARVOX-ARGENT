#pragma once
#include <cmath>
namespace argent::player {
inline bool attachedWallClimbState(int state) noexcept {return state>=3&&state<=9;}
// Supply the full HMD forward vector before native launch bias and speed.
inline bool wallClimbForward(const float* native,const float* head,float* out) noexcept {
 if(!native||!head||!out)return false;
 float nn{},hn{};
 for(int i=0;i<3;++i){if(!std::isfinite(native[i])||!std::isfinite(head[i]))return false;nn+=native[i]*native[i];hn+=head[i]*head[i];}
 if(std::abs(nn-1.f)>.02f||std::abs(hn-1.f)>.02f)return false;
 // The engine constructs its launch rotation axis from horizontal forward.
 // Avoid its degenerate cross product at an exactly vertical look direction.
 if(std::hypot(head[0],head[1])<.05f)return false;
 out[0]=head[0];out[1]=head[1];out[2]=head[2];return true;
}
inline bool wallClimbBlocked(const float* forward,const float* normal,float angle,bool& blocked) noexcept {
 if(!forward||!normal||!std::isfinite(angle)||angle<0||angle>180)return false;
 float norm{},dot{};for(int i=0;i<3;++i){if(!std::isfinite(forward[i])||!std::isfinite(normal[i]))return false;norm+=normal[i]*normal[i];dot+=forward[i]*normal[i];}
 if(std::abs(norm-1.f)>.02f)return false;
 blocked=-dot>std::cos(angle*0.01745329251994329577f);return true;
}
}
