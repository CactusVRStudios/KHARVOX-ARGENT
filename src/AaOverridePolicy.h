#pragma once
#include <cstdint>
namespace argent::camera {
// Native TAA (1) stays disabled. Engine-selected DLSS (2) is permitted only
// when FSR 1 is inactive and the stereo DLSS hook is healthy.
inline int aaTarget(int current,bool fsrActive,bool dlssFailed=false){
 return !fsrActive&&!dlssFailed&&current==2?2:0;
}
// Target recreation only in a stable context; retries use the engine setter.
struct AaOverridePolicy {
 int context=-1;uint64_t stableSince{},lastAttempt{};bool attempted{};
 bool ready(uint64_t now,int current,bool known){
  if(!known){context=-1;stableSince=now;return false;}
  if(context!=current){context=current;stableSince=now;return false;}
  return now>=stableSince&&now-stableSince>=1500;
 }
 template<class Read,class Set> bool apply(uint64_t now,int desired,Read read,Set set,int& before,int& after,bool& accepted){
  if(desired<0||desired>2||!read(before)||before==desired)return false;
  if(attempted&&(now<lastAttempt||now-lastAttempt<2000))return false;
  attempted=true;lastAttempt=now;accepted=set(desired);after=-1;read(after);return true;
 }
};
}
