#pragma once
#include <cstdint>
namespace argent::input {
// KHARVOX/DOOM 2016 sequence: let interaction run before melee consumes input.
struct UseMeleeButton {
 enum class Action { None, Use, Melee };
 bool pressing{},blocked{},active{};uint64_t started{};
 Action update(bool down,bool enabled,uint64_t now){
  if(!enabled){pressing=active=false;blocked=down;return Action::None;}
  if(blocked){if(!down)blocked=false;return Action::None;}
  if(active&&now<started){active=pressing=false;blocked=down;return Action::None;}
  if(down&&!pressing){started=now;active=true;}
  pressing=down;
  if(!active)return Action::None;
  const auto elapsed=now-started;
  if(elapsed<100)return Action::Use;
  if(down||elapsed<200)return Action::Melee;
  active=false;return Action::None;
 }
};
}
