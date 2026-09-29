#pragma once
#include "WeaponHandling.h"
namespace argent::input {
// Tracking-space filtering happens before calibration and support solving.
// One rigid correction per controller keeps its grip and aim spaces together.
struct HandSmoothing {
 XrPosef filtered{},previous{};XrTime tick{};bool held{};
 void update(XrPosef& grip,XrPosef& aim,bool gripValid,bool aimValid,XrTime time,bool enabled){
  if(!enabled||!gripValid||!camera::validPosition(grip.position)||!camera::validQuaternion(grip.orientation)){
   held=false;return;
  }
  const auto raw=grip;
  const double dt=double(time-tick)*1e-9;
  const float travel=held?length(sub(raw.position,previous.position)):0;
  auto q=normalized(raw.orientation);
  const auto old=normalized(filtered.orientation);
  float cosine=old.x*q.x+old.y*q.y+old.z*q.z+old.w*q.w;
  if(!held||dt<=0||dt>.1||travel>.35f||std::abs(cosine)<.7071067f){
   filtered=raw;
  }else{
   // Mild at rest (12 ms time constant), approaching 3 ms during fast motion.
   // Time-based coefficients avoid changing strength with headset refresh rate.
   auto pq=normalized(previous.orientation);
   const float stepDot=std::abs(pq.x*q.x+pq.y*q.y+pq.z*q.z+pq.w*q.w);
   const float angularSpeed=2.f*std::acos(std::clamp(stepDot,0.f,1.f))/float(dt);
   const float activity=std::clamp(std::max(travel/float(dt)/1.5f,angularSpeed/6.f),0.f,1.f);
   const float alpha=1.f-std::exp(-float(dt)/(.012f-.009f*activity));
   filtered.position=add(filtered.position,mul(sub(raw.position,filtered.position),alpha));
   if(cosine<0){q.x=-q.x;q.y=-q.y;q.z=-q.z;q.w=-q.w;}
   filtered.orientation=normalized({old.x+(q.x-old.x)*alpha,old.y+(q.y-old.y)*alpha,
    old.z+(q.z-old.z)*alpha,old.w+(q.w-old.w)*alpha});
  }
  previous=raw;tick=time;held=true;
  if(aimValid){
   const auto correction=normalized(product(filtered.orientation,inverse(normalized(raw.orientation))));
   aim.position=add(filtered.position,rotate(correction,sub(aim.position,raw.position)));
   aim.orientation=normalized(product(correction,aim.orientation));
  }
  grip=filtered;
 }
};
}
