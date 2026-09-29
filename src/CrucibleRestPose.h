#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
namespace argent::player {
// Only the visible attachment pose is replaced. Native state/events keep running.
struct CrucibleRestPose {
 struct Events {uint64_t begin{},end{},tick{};};
 uint64_t seen{},began{},idleSince{}; bool attacking{},cached{};
 float offset[3]{},basis[9]{};
 void clearPose(){cached=false;idleSince=0;began=0;attacking=false;}
 bool select(uint64_t now,uint64_t swing,bool manual,bool eligible,bool idle,Events events){
  const bool event=events.begin&&events.begin!=seen;seen=events.begin;
  if(!eligible||manual){clearPose();return false;}
  const bool nativeSwing=events.begin>events.end;
  if(event&&nativeSwing&&swing&&events.tick>=swing&&events.tick-swing<500&&
     now>=events.tick&&now-events.tick<300){attacking=true;began=events.tick;}
  if(attacking&&((!nativeSwing&&idle)||now<began||now-began>1500))attacking=false;
  // pendingAction is consumed before rendering. Use the real animweb state
  // only for idle capture; explicit swing events delimit the attack lifetime.
  if(idle&&!nativeSwing&&!attacking&&(!swing||(now>=swing&&now-swing>=300))){if(!idleSince)idleSince=now;}
  else idleSince=0;
  return attacking&&cached;
 }
 // Hammer uses different native anim events. Bound its visual-only replacement
 // to the physical input pulse; never block native slam/damage/state progression.
 bool selectPhysical(uint64_t now,uint64_t swing,bool manual,bool eligible,bool idle){
  const bool active=swing&&now>=swing&&now-swing<1500&&(!idle||now-swing<300);
  return select(now,swing,manual,eligible,idle,{swing,active?0:swing,swing});
 }
 bool canCapture(uint64_t now)const{return idleSince&&now>=idleSince&&now-idleSince>=300;}
 bool capture(const float* hand,const float* axes,const float* origin,const float* rotation){
  for(int k=0;k<3;++k)if(!std::isfinite(hand[k])||!std::isfinite(origin[k]))return false;
  for(int k=0;k<9;++k)if(!std::isfinite(axes[k])||!std::isfinite(rotation[k]))return false;
  for(int r=0;r<3;++r){
   offset[r]=0;for(int k=0;k<3;++k)offset[r]+=(origin[k]-hand[k])*axes[r*3+k];
   for(int c=0;c<3;++c){basis[r*3+c]=0;for(int k=0;k<3;++k)basis[r*3+c]+=rotation[r*3+k]*axes[c*3+k];}
  }
  return cached=true;
 }
 void apply(const float* hand,const float* axes,float* origin,float* rotation)const{
  for(int k=0;k<3;++k){origin[k]=hand[k];for(int r=0;r<3;++r)origin[k]+=offset[r]*axes[r*3+k];}
  for(int r=0;r<3;++r)for(int k=0;k<3;++k){rotation[r*3+k]=0;for(int c=0;c<3;++c)rotation[r*3+k]+=basis[r*3+c]*axes[c*3+k];}
 }
};
}
