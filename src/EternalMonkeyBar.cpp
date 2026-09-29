#include "MonkeyBarAim.h"
#include "EternalCameraHook.h"
#include "EternalCameraMath.h"
#include "EternalBuildProfile.h"
#include "EternalPresentation.h"
#include "Diagnostics.h"
#include "QuadRuntime.h"
#include <MinHook.h>
#include <intrin.h>
#include <cstring>
extern "C" {void* argentMonkeyResume{};void argentMonkeyBridge();}
namespace argent::monkey {namespace {
unsigned char* image{};
using Getter=const float*(__fastcall*)(void*);
Getter nativeAxis{},nativeOrigin{};
using Cancel=void(__fastcall*)(void*);
Cancel nativeCancel{};
using StopDash=void(__fastcall*)(void*,bool);
using Completion=bool(__fastcall*)(void*,unsigned short*,int);
StopDash nativeStopDash{};
Completion nativeCompletion{};
std::atomic<uintptr_t> entryOwner{};
std::atomic<ULONGLONG> entryTick{};
struct Pose {uintptr_t owner{};ULONGLONG tick{};float origin[3]{};float axis[9]{};bool valid{};};
thread_local Pose query,view;
bool local(void* owner) {
 return owner&&uintptr_t(owner)==presentation::player.load()&&
  (presentation::worldPresentation.load()||presentation::gameplayInput.load())&&!presentation::syncAttack.load();
}
template<class T> bool read(uintptr_t address,T& value) {
 SIZE_T done{};
 return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&done)&&done==sizeof(value);
}
bool activeDash(uintptr_t owner) {
 uintptr_t controller{},dash{},backlink{};unsigned char active{},physicsActive{};
 return read(owner+0x4ce88,controller)&&controller&&read(controller+0x290,backlink)&&backlink==owner&&
  read(controller+0x298,dash)&&dash&&read(dash+0x118,active)&&read(owner+0xd25f,physicsActive)&&
  (active||physicsActive);
}
bool completionFor(void* entity,unsigned short* handle,int frames,uintptr_t caller) {
 const bool complete=nativeCompletion(entity,handle,frames);
 const auto owner=entryOwner.load();
 if(!complete||caller!=0x1398a8c||!owner||uintptr_t(handle)!=owner+0x36e48+0x108||
    presentation::skippedMonkeyBarOwner.load()!=owner||!local(reinterpret_cast<void*>(owner))||!nativeStopDash)return complete;
 if(!activeDash(owner))return complete;
 // StopDash requests a native FSM transition. Let the regular game tick perform
 // ExitActive/EnterIdle before the skipped animation advances into launch.
 // A pending native transition can initially reject the request, so retry here.
 if(GetTickCount64()-entryTick.load()<100){
  nativeStopDash(reinterpret_cast<void*>(owner),false);
  if(extendedLogging())log("ETERNAL_MONKEYBAR dashHandoff=waiting-native-stop");
  return false;
 }
 if(extendedLogging())log("ETERNAL_MONKEYBAR dashHandoff=timeout native-completion=preserved");
 return complete;
}
bool __fastcall completion(void* entity,unsigned short* handle,int frames) {
 return completionFor(entity,handle,frames,build::semanticRva(uintptr_t(_ReturnAddress())-uintptr_t(image)));
}
bool capture(void* owner,Pose& pose) {
 pose.valid=false;
 float offset[3];
 if(!local(owner)||!camera::monkeyBarPose(pose.origin,pose.axis,offset))return false;
 // Simulation may already have advanced several metres during a dash.
 // Retain the physical HMD offset, never the preceding render's world origin.
 const auto native=nativeOrigin(owner);
 if(!native||!currentOrigin(native,offset,pose.origin))return false;
 pose.owner=uintptr_t(owner);pose.tick=GetTickCount64();pose.valid=true;return true;
}
bool fresh(void* owner,const Pose& pose) {
 return local(owner)&&pose.valid&&pose.owner==uintptr_t(owner)&&GetTickCount64()-pose.tick<100;
}
const float* axisFor(void* owner,uintptr_t caller) {
 if(!axisCaller(caller))return nativeAxis(owner);
 // Query forward, up and both origins must come from the same snapshot.
 if(caller==0x139a842||caller==0x139a8f0)return fresh(owner,query)?query.axis:nativeAxis(owner);
 return capture(owner,view)?view.axis:nativeAxis(owner);
}
const float* originFor(void* owner,uintptr_t caller) {
 if(caller==0x139a79a)return capture(owner,query)?query.origin:nativeOrigin(owner);
 if(caller==0x1399c83||caller==0x139a295)return fresh(owner,view)?view.origin:nativeOrigin(owner);
 return originCaller(caller)&&fresh(owner,query)?query.origin:nativeOrigin(owner);
}
const float* __fastcall axis(void* owner) {
 return axisFor(owner,build::semanticRva(uintptr_t(_ReturnAddress())-uintptr_t(image)));
}
const float* __fastcall origin(void* owner) {
 return originFor(owner,build::semanticRva(uintptr_t(_ReturnAddress())-uintptr_t(image)));
}
void cancelFor(void* mechanic,uintptr_t caller) {
 const auto owner=presentation::player.load();
 if(owner&&entryOwner.load()==owner&&presentation::skippedMonkeyBarOwner.load()==owner&&
    uintptr_t(mechanic)==owner+0x36e48&&local(reinterpret_cast<void*>(owner))&&
    GetTickCount64()-entryTick.load()<100){
  uintptr_t backlink{},fsm{};int state{};SIZE_T done{};
  const auto read=[&](uintptr_t p,void* out,SIZE_T size){return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),out,size,&done)&&done==size;};
  if(read(uintptr_t(mechanic)+0x18,&backlink,8)&&backlink==owner&&
     read(uintptr_t(mechanic)+0x50,&fsm,8)&&fsm&&read(fsm+0xc,&state,4)&&
     protectDashHandoff(caller,state,GetTickCount64()-entryTick.load())){
   if(extendedLogging())log("ETERNAL_MONKEYBAR dashCancel=deferred state="+std::to_string(state));
   return;
  }
 }
 nativeCancel(mechanic);
}
void __fastcall cancel(void* mechanic) {
 cancelFor(mechanic,build::semanticRva(uintptr_t(_ReturnAddress())-uintptr_t(image)));
}
}
void acceptedBar(uintptr_t owner) {
 entryTick=GetTickCount64();entryOwner=owner;
 if(!nativeStopDash||!local(reinterpret_cast<void*>(owner)))return;
 const bool active=activeDash(owner);
 if(active)nativeStopDash(reinterpret_cast<void*>(owner),false);
 if(extendedLogging())log(std::string("ETERNAL_MONKEYBAR accepted=1 dashActive=")+(active?"1 stop=requested refill=0":"0"));
}
bool install(unsigned char* base) {
 image=base;
 const unsigned char getterTail[]={0x83,0x7a,8,0,0x41,0x0f,0x44,0xc0,0x48,3,0xc1,0xc3};
 for(auto rva:{uintptr_t(0x13e8950),uintptr_t(0x13e8970)}) {
  const auto p=image+build::rva(rva);
  if(std::memcmp(p,"\x48\x8b\x15",3)||std::memcmp(p+18,getterTail,sizeof(getterTail)))return false;
 }
 for(auto caller:{uintptr_t(0x1399bf7),uintptr_t(0x139a842),uintptr_t(0x139a8f0),uintptr_t(0x1397a7c),uintptr_t(0x1397aef)})
  if(std::memcmp(image+build::rva(caller)-6,"\xff\x90\x70\x04\x00\x00",6))return false;
 for(auto caller:{uintptr_t(0x139a79a),uintptr_t(0x1399c83),uintptr_t(0x139a295),uintptr_t(0x139a85f),uintptr_t(0x139a90e)})
  if(std::memcmp(image+build::rva(caller)-6,"\xff\x90\x78\x04\x00\x00",6))return false;
 if(std::memcmp(image+build::rva(0x1398d5d),"\x0f\x28\x15",3))return false;
 if(std::memcmp(image+build::rva(0x13989b0),"\x40\x53\x48\x83\xec\x20\x48\x8b\x01\x48\x8b\xd9",12))return false;
 for(auto ret:{uintptr_t(0xfbdc3a),uintptr_t(0xfbddc9)}){
  auto call=image+build::rva(ret)-5;int displacement{};std::memcpy(&displacement,call+1,4);
  if(*call!=0xe8||call+5+displacement!=image+build::rva(0x13989b0))return false;
 }
 const auto stop=image+build::rva(0x13e5be0);
 if(std::memcmp(stop,"\x48\x8b\x89\x88\xce\x04\x00\x48\x85\xc9\x0f\x85",12)||stop[16]!=0xc3)return false;
 int delta{};std::memcpy(&delta,stop+12,4);
 if(stop+16+delta!=image+build::rva(0xfd6790))return false;
 const auto ability=image+build::rva(0xfd6790);
 if(std::memcmp(ability,"\x40\x53\x48\x83\xec\x20\x48\x83\xb9\x98\x02\x00\x00\x00",14)||
    std::memcmp(ability+0x13,"\x84\xd2\x74\x38",4)||ability[0x5b]!=0xe9)return false;
 std::memcpy(&delta,ability+0x5c,4);
 if(ability+0x60+delta!=image+build::rva(0xfbbf60))return false;
 if(std::memcmp(image+build::rva(0xb420e0),"\x48\x89\x5c\x24\x08\x48\x89\x74\x24\x18\x57\x48\x83\xec\x20",15))return false;
 const auto completedCall=image+build::rva(0x1398a8c)-5;
 std::memcpy(&delta,completedCall+1,4);
 if(*completedCall!=0xe8||completedCall+5+delta!=image+build::rva(0xb420e0))return false;
 void* targets[]={image+build::rva(0x13e8950),image+build::rva(0x13e8970),image+build::rva(0x1398d5d),image+build::rva(0x13989b0),image+build::rva(0xb420e0)};
 void* detours[]={reinterpret_cast<void*>(&axis),reinterpret_cast<void*>(&origin),reinterpret_cast<void*>(&argentMonkeyBridge),reinterpret_cast<void*>(&cancel),reinterpret_cast<void*>(&completion)};
 void** originals[]={reinterpret_cast<void**>(&nativeAxis),reinterpret_cast<void**>(&nativeOrigin),&argentMonkeyResume,reinterpret_cast<void**>(&nativeCancel),reinterpret_cast<void**>(&nativeCompletion)};
 unsigned created=0;
 for(;created<5;++created)if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK)break;
 if(created==5){unsigned enabled=0;for(;enabled<5;++enabled)if(MH_EnableHook(targets[enabled])!=MH_OK)break;
  if(enabled==5){nativeStopDash=reinterpret_cast<StopDash>(stop);log("ETERNAL_MONKEYBAR aim=HMD origin=current-native+HMD-offset launch=HMD dash-handoff=native-stop-before-launch native-force=1");return true;}
  for(unsigned i=0;i<enabled;++i)MH_DisableHook(targets[i]);
 }
 for(unsigned i=0;i<created;++i)MH_RemoveHook(targets[i]);
 return false;
}
}
extern "C" void argentMonkeyLaunch(void* mechanic,float* x,float* y) {
 using namespace argent;
 const auto owner=presentation::player.load();uintptr_t backlink{};SIZE_T done{};
 if(!owner||uintptr_t(mechanic)!=owner+0x36e48||
    !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<char*>(mechanic)+0x18,&backlink,sizeof(backlink),&done)||
    done!=sizeof(backlink)||backlink!=owner)return;
 float origin[3],axis[9];
 if(camera::monkeyBarPose(origin,axis)&&monkey::launchHeading(axis,*x,*y)&&extendedLogging())
  log("ETERNAL_MONKEYBAR launch=HMD x="+std::to_string(*x)+" y="+std::to_string(*y));
}
