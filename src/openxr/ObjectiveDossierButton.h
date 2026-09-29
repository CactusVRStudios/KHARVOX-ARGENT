#pragma once
#include <cstdint>
namespace argent::input {
// Short action is deferred until release so a long press never emits both.
// Pulses survive multiple XR samples; native XInput polling may run later.
struct ObjectiveDossierButton {
 enum class Action { None, Objective, Dossier };
 static constexpr uint64_t holdMs=600,pulseMs=100;
 bool pressing{},longSent{},blocked{};uint64_t started{},pulseStart{};
 Action pulse{Action::None};
 Action update(bool down,bool enabled,uint64_t now){
  if(!enabled){pressing=longSent=false;blocked=down;pulse=Action::None;return pulse;}
  if(blocked){if(!down)blocked=false;return Action::None;}
  if(pulse!=Action::None&&(now<pulseStart||now-pulseStart>=pulseMs))pulse=Action::None;
  if(!pressing&&down){pressing=true;longSent=false;started=now;}
  if(pressing){
   if(now<started){pressing=false;blocked=down;pulse=Action::None;return pulse;}
   if(!longSent&&now-started>=holdMs){pulse=Action::Dossier;pulseStart=now;longSent=true;}
   if(!down){if(!longSent){pulse=Action::Objective;pulseStart=now;}pressing=false;}
  }
  return pulse;
 }
};
}
