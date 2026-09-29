#pragma once
#include <algorithm>
#include <cstdint>
namespace argent::camera {
// Explicit experiment only. The normal headset FOV is never recalibrated here.
struct FovComparison {
 bool pending{},active{};uint64_t started{};int phase=-1;
 void request(bool start){pending=start;active=false;phase=-1;}
 int update(uint64_t now,bool ready,int automatic){
  if(pending&&ready&&automatic>120){pending=false;active=true;started=now;}
  if(active&&(!ready||now<started||now-started>=58000)){active=false;phase=-1;}
  if(!active)return automatic;
  const auto elapsed=now-started;
  // Ten seconds to return focus/settle, then A/B/A/B, twelve seconds each.
  phase=elapsed<10000?0:1+int((elapsed-10000)/12000);
  return phase==2||phase==4?std::min(120,automatic):automatic;
 }
};
}
