#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
namespace argent::player {
// World-space transform, not an old tracking-space pose (which would follow
// the head/body). The animated root is shared across weapons, so include the
// primary weapon identity rather than relying on the root alone.
struct WeaponIdlePose {
 uintptr_t owner{},hands{},root{},weapon{};std::array<float,3> position{};std::array<float,9> basis{};bool valid{};
 bool apply(uintptr_t o,uintptr_t h,uintptr_t r,bool context,bool tracked,float* p,float* a,uintptr_t w=0){
  if(!context||o!=owner||h!=hands||r!=root||w!=weapon){valid=false;owner=o;hands=h;root=r;weapon=w;}
  if(!context||!o||!h||!r)return false;
  if(tracked){std::copy_n(p,3,position.begin());std::copy_n(a,9,basis.begin());valid=true;}
  else if(valid){std::copy(position.begin(),position.end(),p);std::copy(basis.begin(),basis.end(),a);}
  return valid;
 }
};
}
