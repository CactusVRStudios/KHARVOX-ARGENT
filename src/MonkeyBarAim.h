#pragma once
#include <cstdint>
#include <cmath>
namespace argent::monkey {
inline bool axisCaller(uintptr_t rva) {
 return rva==0x1399bf7||rva==0x139a842||rva==0x139a8f0||rva==0x1397a7c||rva==0x1397aef;
}
inline bool originCaller(uintptr_t rva) {return rva==0x139a79a||rva==0x1399c83||rva==0x139a295||rva==0x139a85f||rva==0x139a90e;}
inline bool launchHeading(const float* axis,float& x,float& y) {
 const float length=std::hypot(axis[0],axis[1]);
 if(!std::isfinite(length)||length<0.05f)return false;
 x=axis[0]/length;y=axis[1]/length;return true;
}
bool install(unsigned char* image);
void acceptedBar(uintptr_t owner);
inline bool protectDashHandoff(uintptr_t caller,int state,uint64_t age) {
 return (caller==0xfbdc3a||caller==0xfbddc9)&&(state==2||state==3)&&age<100;
}
inline bool currentOrigin(const float* native,const float* headOffset,float* result) {
 float updated[3];
 for(int i=0;i<3;++i){updated[i]=native[i]+headOffset[i];if(!std::isfinite(updated[i]))return false;}
 for(int i=0;i<3;++i)result[i]=updated[i];return true;
}
}
