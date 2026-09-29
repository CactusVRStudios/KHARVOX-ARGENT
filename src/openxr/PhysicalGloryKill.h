#pragma once
#include "WeaponHandling.h"
#include <cstdint>
namespace argent::input {
// KHARVOX OpenXRBootstrap gesture: forward velocity, 45% rearm,
// 350 ms shared cooldown, 200 ms native melee pulse. No forced kill/teleport.
struct PhysicalGloryKill {
 std::array<bool,2> armed{};int64_t cooldown{},pulse{};
 bool update(int64_t time,bool context,int hands,float threshold,
             const std::array<XrPosef,2>& poses,const std::array<XrVector3f,2>& velocity,
             const std::array<bool,2>& valid,bool swing=false){
  if(!context){armed={};pulse=0;return false;}
  for(int h=0;h<2;++h){
   if((hands!=2&&hands!=h)||!valid[h]){armed[h]=false;continue;}
   const auto f=unit(rotate(poses[h].orientation,{0,0,-1}));
   const float speed=swing?length(velocity[h]):velocity[h].x*f.x+velocity[h].y*f.y+velocity[h].z*f.z;
   if(!std::isfinite(speed)){armed[h]=false;continue;}
   if(speed<=threshold*.45f)armed[h]=true;
   if(armed[h]&&speed>=threshold&&time>=cooldown){armed={};cooldown=time+350000000;pulse=time+200000000;break;}
  }
  return time<pulse;
 }
};
}
