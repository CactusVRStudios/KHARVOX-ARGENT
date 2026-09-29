#include "../EternalBuildProfile.h"
#include "EternalHitMarkers.h"
#include "../QuadRuntime.h"
#include <MinHook.h>
#include <cstdint>
#include <cstring>

namespace argent::hud { namespace {
using UpdateReticle = uintptr_t(__fastcall*)(void*, void*);
UpdateReticle originalUpdate{};
uintptr_t __fastcall updateReticle(void* reticle, void* time) {
 // Native idHUD_Reticle::UpdateReticleData compares this expiration time
 // with time[0] before showing the hit animation. Clear before consumption,
 // including in quad presentation; the remaining HUD update runs unchanged.
 // Independently recovered from the supplied proxy's hook at RVA 0x1eab30.
 if(reticle){
  constexpr uint64_t expired=0;
  std::memcpy(static_cast<unsigned char*>(reticle)+0x1c8,&expired,sizeof(expired));
 }
 return originalUpdate(reticle,time);
}
}
bool installHitMarkerSuppression(unsigned char* base) noexcept {
 constexpr uintptr_t rva=0xefd230;
 constexpr unsigned char entry[]={0x40,0x53,0x57,0x41,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0xb9,0xf8,0,0,0,0x4c,0x8b,0xfa};
 constexpr unsigned char consumer[]={0x48,0x8b,0x83,0xc8,1,0,0,0x49,0x39,0x07};
 if(!base||std::memcmp(base+build::rva(rva),entry,sizeof(entry))||
    std::memcmp(base+build::rva(0xefd4c2),consumer,sizeof(consumer))){
  log("ETERNAL_HITMARKERS refused: signature/expiry consumer mismatch");return false;
 }
 if(MH_CreateHook(base+build::rva(rva),reinterpret_cast<void*>(&updateReticle),reinterpret_cast<void**>(&originalUpdate))!=MH_OK){
  log("ETERNAL_HITMARKERS refused: hook creation failed");return false;
 }
 if(MH_EnableHook(base+build::rva(rva))!=MH_OK){
  MH_RemoveHook(base+build::rva(rva));log("ETERNAL_HITMARKERS refused: hook enable failed");return false;
 }
 log("ETERNAL_HITMARKERS disabled=1 policy=expire-before-reticle-update");return true;
}
}
