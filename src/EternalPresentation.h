#pragma once
#include "EternalBuildProfile.h"
#include "PresentationPolicy.h"
#include "EternalPresentationFrame.h"
#include "WallClimbAim.h"
#include "EternalRevenant.h"
#include <windows.h>
#include <atomic>
#include <mutex>
namespace argent::presentation {
bool pauseRootVisible();
inline std::mutex mutex;
inline Sample latest;
inline std::atomic<uintptr_t> player{};
inline std::atomic<uint64_t> playerTick{};
inline std::atomic<bool> scriptedMovement{};
inline std::atomic<bool> syncAttack{};
inline std::atomic<uintptr_t> playerVtable{};
inline bool attachedWallClimb(){
 const auto owner=player.load(),expected=playerVtable.load();
 if(!owner||!expected)return false;
 SIZE_T got{};auto read=[&](uintptr_t address,void* out,size_t size){
  return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&got)&&got==size;
 };
 uintptr_t table{},mechanicOwner{},stateOwner{};int state{};
 const auto mechanic=owner+0x35130;
 return read(owner,&table,sizeof(table))&&table==expected&&
  read(mechanic+0x18,&mechanicOwner,sizeof(mechanicOwner))&&mechanicOwner==owner&&
  read(mechanic+0x138,&stateOwner,sizeof(stateOwner))&&stateOwner&&
  read(stateOwner+0xc,&state,sizeof(state))&&::argent::player::attachedWallClimbState(state);
}
// idAnimatedEntity::isSyncAttackInstigator, inherited by idPlayer. Read at
// the native camera call too: the render-frame classification arrives later.
inline bool refreshSyncAttack(){
 const auto expected=playerVtable.load(),owner=player.load();
 if(!expected)return syncAttack.load(); // Native camera test fixture.
 uintptr_t table{};unsigned char instigator{};SIZE_T got{};
 const bool active=owner&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(owner),&table,sizeof(table),&got)&&got==sizeof(table)&&table==expected&&
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(owner+0x1a01),&instigator,1,&got)&&got==1&&instigator==1;
 syncAttack=active;return active;
}
// Native interaction/locomotion camera ownership is separate from the menu.
inline std::atomic<bool> interactionAnimation{},monkeyBarAnimation{},meatHookAnimation{},nativeAnimation{};
// Armed only by the ModBot menu. Ordinary weapon-mod switches must retain VR
// control. The native modChangeAnimPlaying bit spans the menu and its outro.
inline std::atomic<uintptr_t> upgradeAnimationOwner{};
inline std::atomic<bool> droneAnimation{};
// First-person GorillaBar lifecycle; bodyAnimInfo only covers third person.
inline std::atomic<uintptr_t> skippedMonkeyBarOwner{};
inline bool refreshDroneAnimation(){
 auto armed=upgradeAnimationOwner.load();const auto owner=player.load();
 if(!armed||armed!=owner){droneAnimation=false;return false;}
 unsigned char flags{};SIZE_T got{};
 if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(owner+0xd2c8+0x8da5),&flags,1,&got)&&got==1){
  const bool active=(flags&0x80)!=0;droneAnimation=active;
  if(!active)upgradeAnimationOwner.compare_exchange_strong(armed,0);
 }
 return droneAnimation.load();
}
inline bool refreshAnimationCamera(){
 const auto controlled=revenant::refresh(player.load(),playerVtable.load());
 if(controlled){syncAttack=false;interactionAnimation=false;monkeyBarAnimation=false;meatHookAnimation=false;droneAnimation=false;nativeAnimation=false;return false;}
 const bool sync=refreshSyncAttack();const auto owner=player.load(),expected=playerVtable.load();
 if(!expected){nativeAnimation=sync||scriptedMovement.load();return nativeAnimation.load();}
 uintptr_t table{};SIZE_T got{};
 auto read=[&](size_t offset,void* out,size_t size){return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(owner+offset),out,size,&got)&&got==size;};
 bool interaction=false,monkey=false,ledge=false,meat=false,drone=false;
 if(owner&&read(0,&table,sizeof(table))&&table==expected){
  unsigned char view{},bar{};int type{},ledgeState=16;
  // Reflected idPlayer::inInteractionView, playerMechanicInteract.activeInteractType,
  // playerBodyAnimInfo_t.monkeybarAnimIsPlaying, mechanic ledge state.
  if(read(0x167e9,&view,1))interaction=view==1;
  if(read(0x370c0+0x98,&type,4))interaction=interaction||(type>0&&type<4);
  drone=refreshDroneAnimation();
  interaction=interaction||drone;
  if(read(0x2f568+0x78,&bar,1))monkey=bar==1;
  monkey=monkey||skippedMonkeyBarOwner.load()==owner;
  if(read(0x2fb70+0x5278,&ledgeState,4))ledge=activeLedge(ledgeState);
  // Native primary weapon handle: only use its resolved, generation-matched
  // cached pointer. Never resolve/mutate an entity handle from the render thread.
  uint32_t generation{},cached{};uintptr_t weapon{},weaponType{};unsigned char entered{};
  const size_t handle=0xd2c8+0x29b0;
  if(read(handle+0x30,&generation,4)&&read(handle+0x34,&cached,4)&&generation==cached&&generation!=0x1fffffe&&
     read(handle+0x38,&weapon,8)&&weapon&&
     ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(weapon),&weaponType,8,&got)&&got==8&&
     weaponType==expected-build::rva(0x2db5698)+build::rva(0x2e0dbc8)&&
     ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(weapon+0x3d9b),&entered,1,&got)&&got==1)meat=entered==1;

 }
 droneAnimation=drone;interactionAnimation=interaction;monkeyBarAnimation=monkey;meatHookAnimation=meat;
 // Meathook is player-controlled traversal, not an authored camera sequence.
 // Keep its native flag for diagnostics; it must not seize camera/hands or
 // disable the inputs needed to shoot, release and finish the grapple.
 nativeAnimation=sync||interaction||monkey||ledge;return nativeAnimation.load();
}
inline std::atomic<bool> gameplayInput{};
inline std::atomic<bool> worldPresentation{};
inline std::atomic<bool> hideGameplayHud{};
inline std::atomic<bool> crouched{};
inline std::atomic<uint64_t> crouchTick{};
inline std::atomic<unsigned> optionBits{15};
inline Options options(){auto b=optionBits.load();return {bool(b&1),bool(b&2),bool(b&4),bool(b&8),bool(b&16)};}
inline Sample snapshot(){std::lock_guard<std::mutex> lock(mutex);return latest;}
bool install(unsigned char* verifiedImage) noexcept;
}
