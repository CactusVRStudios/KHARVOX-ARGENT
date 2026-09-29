#include "../src/EquipmentAimPolicy.h"
#include <cassert>
#include <limits>
int main(){
 using namespace argent;
 using namespace argent::player;
 const camera::Basis head{0,0,1, 0,1,0, -1,0,0}; // Head looks up.
 const camera::Basis hand{1,0,0, 0,1,0, 0,0,1}; // Gun stays horizontal.
 struct Launch {float origin[3];camera::Basis axis;};
 for(int slot=1;slot<11;++slot){
  Launch muzzle{{1,2,3},hand},fire{{4,5,6},hand};
  const auto source=applyFireDirection(slot,true,head,true,hand,muzzle.axis.data(),fire.axis.data());
  const bool equipment=slot==5||slot==6||slot==9||slot==10;
  assert(hideEquipmentVisual(slot,true,false)==equipment);
  assert(!hideEquipmentVisual(slot,false,false)&&!hideEquipmentVisual(slot,true,true));
  assert(source==(equipment?AimSource::Head:AimSource::Controller));
  assert(muzzle.axis==(equipment?head:hand)&&fire.axis==muzzle.axis);
  assert(muzzle.origin[0]==1&&muzzle.origin[1]==2&&muzzle.origin[2]==3);
  assert(fire.origin[0]==4&&fire.origin[1]==5&&fire.origin[2]==6);
  // Unavailable/stale HMD data cannot fall back to a tracked weapon.
  if(equipment){
   muzzle.axis=fire.axis=hand;
   assert(applyFireDirection(slot,false,head,true,head,muzzle.axis.data(),fire.axis.data())==AimSource::Native);
   assert(muzzle.axis==hand&&fire.axis==hand);
   // Untracked controllers do not disable head-directed equipment.
   assert(applyFireDirection(slot,true,head,false,hand,muzzle.axis.data(),fire.axis.data())==AimSource::Head);
  }
 }
 auto bad=head;bad[0]=std::numeric_limits<float>::quiet_NaN();
 auto first=hand,second=hand;
 assert(applyFireDirection(5,true,bad,true,head,first.data(),second.data())==AimSource::Native);
 assert(first==hand&&second==hand);
 assert(applyFireDirection(9,true,head,true,hand,nullptr,second.data())==AimSource::Native);
 assert(second==hand);
 assert(applyFireDirection(2,true,head,false,hand,first.data(),second.data())==AimSource::Native);
 assert(first==hand&&second==hand);
 // Exercise the earlier joint source and the separate final-throw boundary.
 // Both must preserve pitch for every equipment slot, even with a flat gun.
 for(int slot:{5,6,9,10})for(float pitch:{-1.2f,-.5f,0.f,.5f,1.2f}){
  camera::Basis tracked{};
  const XrQuaternionf pose{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)};
  assert(camera::rotateBasis(hand,{0,0,0,1},pose,tracked));
  assert(std::abs(tracked[2]-std::sin(pitch))<.0001f);
  Launch joint{{1,2,3},hand};
  assert(applyEquipmentDirection(slot,true,tracked,joint.axis.data()));
  const auto nativeCachedAxis=joint.axis; // Native Flame Belch caches the source.
  assert(nativeCachedAxis==tracked&&joint.origin[2]==3);
  auto finalThrow=hand;
  assert(applyEquipmentDirection(slot,true,tracked,finalThrow.data()));
  assert(finalThrow==tracked);
  assert(!applyEquipmentDirection(slot,false,tracked,finalThrow.data()));
  assert(finalThrow==tracked);
 }
 for(int slot:{0,1,2,3,4,7,8,11}){
  auto native=hand;assert(!applyEquipmentDirection(slot,true,head,native.data()));assert(native==hand);
 }
 auto native=hand;
 assert(!applyEquipmentDirection(5,true,bad,native.data())&&native==hand);
 assert(!applyEquipmentDirection(9,true,head,nullptr));
}
