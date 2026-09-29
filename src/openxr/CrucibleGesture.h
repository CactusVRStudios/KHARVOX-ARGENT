#pragma once
#include "PhysicalGloryKill.h"
namespace argent::input {
struct CrucibleGesture {
 PhysicalGloryKill sword,melee;
 bool initialized{},previousCrucible{};int previousPrimary{};
 struct Result {bool fire{},punch{};};
 Result update(int64_t time,bool enabled,bool crucible,int primary,int normalHands,float threshold,
               const std::array<XrPosef,2>& poses,const std::array<XrVector3f,2>& velocity,
               const std::array<bool,2>& valid){
  if(!initialized||previousCrucible!=crucible||previousPrimary!=primary){sword={};melee={};}
  initialized=true;previousCrucible=crucible;previousPrimary=primary;
  if(!enabled){sword={};melee={};return {};}
  if(crucible&&!valid[primary])sword={};
  const bool fire=sword.update(time,crucible&&valid[primary],primary,threshold,poses,velocity,valid,true);
  if(crucible&&!valid[1-primary])melee={};
  const bool punch=melee.update(time,!fire,crucible?1-primary:normalHands,threshold,poses,velocity,valid);
  return {fire,punch};
 }
};
}
