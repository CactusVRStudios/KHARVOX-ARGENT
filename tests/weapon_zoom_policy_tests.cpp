#include "../src/WeaponZoomPolicy.h"
#include "../src/MeleeWeaponVisibility.h"
#include <cassert>
#include <limits>
#include <initializer_list>
int main(){
 using namespace argent;
 assert(keepPrecisionBoltVisible(true,true,true,true,1));
 assert(!keepPrecisionBoltVisible(false,true,true,true,1));
 assert(!keepPrecisionBoltVisible(true,false,true,true,1));
 assert(!keepPrecisionBoltVisible(true,true,false,true,1));
 assert(!keepPrecisionBoltVisible(true,true,true,false,1));
 for(int selected:{-1,0,2,3})assert(!keepPrecisionBoltVisible(true,true,true,true,selected));
 constexpr uintptr_t hands=0x10000,item=hands+0x29b0;
 for(int type=1;type<=3;++type){
  assert(keepMeleeWeaponVisible(true,false,false,true,hands,item,type));
  assert(!keepMeleeWeaponVisible(true,true,false,true,hands,item,type));
  assert(!keepMeleeWeaponVisible(true,false,true,true,hands,item,type));
  assert(!keepMeleeWeaponVisible(false,false,false,true,hands,item,type));
  assert(!keepMeleeWeaponVisible(true,false,false,false,hands,item,type));
 }
 for(int type : {-1,0,4,100})assert(!keepMeleeWeaponVisible(true,false,false,true,hands,item,type));
 assert(!keepMeleeWeaponVisible(true,false,false,true,hands,hands+0x3728,1));
 assert(!keepMeleeWeaponVisible(true,false,false,true,0,item,1));
 for(int mode=-1;mode<=4;++mode){
  assert(vrWeaponZoomMode(mode,false)==mode);
  assert(vrWeaponZoomMode(mode,true)==(mode==1?2:mode));
 }
 // Enter VR while already zoomed; leave the native blend intact outside VR.
 for(float blend : {0.f,0.1f,0.5f,1.f}){
  assert(vrWeaponZoomBlend(blend,true)==0.f);
  assert(vrWeaponZoomBlend(blend,false)==blend);
 }
 assert(validWeaponZoomPresentation(45,50,1,1,1));
 assert(validWeaponZoomPresentation(0,0,0,0,0));
 assert(validWeaponZoomPresentation(90,90,0,0,3));
 // Live weapon mod: 80 world / 55 hands was still shrinking the model.
 assert(vrWeaponHandsFov(55,80,true)==80);
 assert(vrWeaponHandsFov(55,80,false)==55);
 assert(vrWeaponHandsFov(0,0,true)==0);
 for(float fov : {80.f,90.f,105.f,130.f}){
  assert(vrWeaponWorldFov(80,fov,true)==fov);
  assert(vrWeaponWorldFov(80,fov,false)==80);
 }
 assert(vrWeaponWorldFov(80,0,true)==80);
 assert(vrWeaponWorldFov(80,std::numeric_limits<float>::quiet_NaN(),true)==80);
 assert(vrWeaponHandsFov(55,std::numeric_limits<float>::infinity(),true)==55);
 assert(!validWeaponZoomPresentation(std::numeric_limits<float>::quiet_NaN(),50,1,1,1));
 assert(!validWeaponZoomPresentation(45,180,1,1,1));
 assert(!validWeaponZoomPresentation(45,50,2,1,1));
 assert(!validWeaponZoomPresentation(45,50,1,9,1));
 assert(!validWeaponZoomPresentation(45,50,1,1,4));
}
