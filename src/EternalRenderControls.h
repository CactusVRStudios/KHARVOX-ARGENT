#pragma once
#include "EternalBuildProfile.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace argent::camera {
struct RenderControl {uintptr_t rva;const char* name;const char* value;int integer;bool floating{};};
// Eternal-only registrations, verified against the supported executable.
inline constexpr RenderControl rayTracingControl{0x66e3dc0,"r_enableRayTracing","0",0};
inline constexpr std::array<RenderControl,21> renderControls{{
 {0x45f5b80,"g_fov","90",90},
 {0x66deb80,"r_motionblur","0",0},
 {0x6b4ab80,"in_joystick","1",1},
 {0x46c6150,"in_MarkJoystickInactiveOnMouseInput","0",0},
 {0x4671260,"hands_fovScale","1",1},
 {0x6681f80,"r_skipFlares","1",1},
 {0x66deb00,"r_lensFlaresRatio","0",0,true},
 {0x66dff60,"r_cineLensflaresEnabled","0",0},
 // Native mouse-device gate (registration 0x27b0e0; consumer 0x1dbcfb0).
 // VR owns input; no OS mouse hook or global cursor/input suppression.
 {0x6b54890,"in_mouse","0",0},
 // Native HUD reticle mode: 0=full, 1=dot, 2=hidden. Registration
 // 0x197f90, consumer 0xefd2b3 selects RETICLE_STYLE_NONE for value 2.
 {0x45f8610,"g_reticleMode","2",2},
 // Native shake gate: registration 0x20623d, camera consumer 0x147e4ca.
 // Reapply after profile/menu changes; keep XR tracking and aim untouched.
 {0x468cbe0,"view_skipShakes","1",1},
 // Additional view-kick gates at 0x147f819 and 0x14806b9.
 {0x468ed60,"view_mpViewKick","0",0},
 // Weapon Bob menu setting and native view-bob/weapon-sway gate.
 // Registrations 0x194ebd and 0x1c3f2d in the supported Eternal executable.
 {0x45f5100,"g_setting_hands_bob","0",0},
 {0x463a940,"pm_noBob","1",1},
 // Eternal's weapon projection depth priority (default 0.2). Unlike Doom
 // 2016's entity depth hack, this feeds both the projection (0x1ce189d)
 // and shader depth parameters (0x1c5641b). VR hands need unbiased scene
 // depth for mutual occlusion; use the native setter to keep both consistent.
 {0x66eb320,"r_customViewProjectionMatrixDepthBias","0",0,true},
 // Independent of view_skipShakes: registration 0x2061c0, consumer 0x147f6cc.
 {0x468cc60,"view_skipKicks","1",1},
 // Native melee target-view interpolation (0x143f421, blend at 0x143f735).
 // HMD owns viewing direction; preserve the attack/lunge itself.
 {0x4688570,"meleeLunge_snapViewToTarget","0",0},
 // SSR off by default; reapply after graphics preset/profile changes.
 // Native registration 0x24cefd; indirect wrapper identity checked at runtime.
 {0x6687d20,"r_SSR","0",0},
 // Native third-person Monkey Bar animation gate (consumer 0x140874d).
 {0x467f220,"pm_bodyAnim_monkeyBarThirdPersonEnable","0",0},
 // Wheel DOF was authored for a flat overlay. Our world-space wheel is
 // affected by that scene blur too. Registration 0x1cb54d; consumers
 // 0xf0d322/0xf0d9e6. Keep other scene/cinematic DOF controls unchanged.
 {0x4641bd0,"weaponWheel_dof_enable","0",0},
 // Native RT gate, registration Steam 0x25ad8d / Store 0x25b2cd.
 // Reassert after graphics menu or preset changes, not just at launch.
 rayTracingControl
}};
inline constexpr RenderControl lightCullingControl{0x6683c00,"r_skipLightGPUCulling","1",1};
inline constexpr RenderControl wheelBloomControl{0x66dd0e0,"r_hdrBloom","0",0};
// Temporary wheel-only suppression. Restore the prior setting, but never
// overwrite an external change made while the scope was active.
struct WheelBloomScope {
 bool held{};int restore{};
 template<class Read,class Set> bool update(bool visible,Read read,Set set){
  if(!visible&&!held)return true;
  int current{};if(!read(current))return false;
  if(visible){
   if(!held||current!=0)restore=current;
   held=true;
   if(current!=0&&(!set(0)||!read(current)||current!=0))return false;
  }else{
   if(current==0&&restore!=0&&(!set(restore)||!read(current)||current!=restore))return false;
   held=false;
  }
  return true;
 }
};
// KHARVOX's gameplay ownership policy, adapted to Eternal's indirect CVars.
// Preserve the game's value outside VR and respect later external changes.
struct LightCullingScope {
 bool held{};int restore{};
 template<class Read,class Set> bool update(bool world,Read read,Set set){
  if(!world&&!held)return true;
  int current{};if(!read(current))return false;
  if(world){
   if(!held||current!=1)restore=current;
   held=true;
   if(current!=1){if(!set(1)||!read(current)||current!=1)return false;}
  }else{
   if(current==1&&restore!=1){if(!set(restore)||!read(current)||current!=restore)return false;}
   held=false;
  }
  return true;
 }
};
// Read the indirect Eternal layout, validating the name before trusting a value.
// The wrapper can be redirected by registration: never cache its data pointer.
template<class Read> bool readRenderControl(uintptr_t base,const RenderControl& control,Read read,int& value,float* floating=nullptr){
 uintptr_t data{},name{};char actual[64]{};const auto length=std::strlen(control.name)+1;
 if(!base||length>sizeof(actual)||!read(base+build::rva(control.rva),&data,sizeof(data))||!data||
    !read(data+0x28,&name,sizeof(name))||!name||!read(name,actual,length)||
    std::memcmp(actual,control.name,length))return false;
 return read(data+8,&value,sizeof(value))&&(!floating||read(data+12,floating,sizeof(*floating)));
}
template<class Read,class Set,class Report>
void enforceRenderControls(uintptr_t base,Read read,Set set,Report report,int fov=90){
 const auto fovText=std::to_string(fov);
 for(auto control:renderControls){
  if(control.rva==0x45f5b80){control.integer=fov;control.value=fovText.c_str();}
  int before{},after{};float beforeFloat{},afterFloat{};
  if(!readRenderControl(base,control,read,before,control.floating?&beforeFloat:nullptr)){report(control,false,0,false,0);continue;}
  // Fractional flare ratios and weapon depth bias can have integer value zero.
  if(before==control.integer&&(!control.floating||beforeFloat==float(control.integer)))continue;
  const bool accepted=set(reinterpret_cast<void*>(base+build::rva(control.rva)),control.value,true);
  const bool verified=readRenderControl(base,control,read,after,control.floating?&afterFloat:nullptr)&&(!control.floating||afterFloat==float(control.integer));
  report(control,accepted,before,verified,after);
 }
}
}


