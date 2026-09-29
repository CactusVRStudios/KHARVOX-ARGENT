#pragma once
#include <windows.h>
#include <Xinput.h>
#include <openxr/openxr.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <cstring>
#include <atomic>
#include "MovementDirectionPolicy.h"

namespace argent::input {
// Frame snapshots, not independent atomics: XInput must see a coherent action
// sample. Never inject OS keyboard/mouse events or affect another process.
struct Snapshot {
 XrPosef weaponBase{};std::array<char,32> weaponProfile{};bool weaponCorrection{};
 uint64_t crucibleSwing{};bool manualWeaponTrigger{};
 uintptr_t revenantActor{};
 XrVector3f weaponPivot{-.15f,0,0};
 XrPosef weapon{};bool weaponValid{},twoHand{},offhandMovement{};int dominant{1};bool snapTurn{},managedTurn{};float snapDegrees{45},smoothSpeed{230};
 XINPUT_GAMEPAD pad{};std::array<XrPosef,2> aim{},grip{};
 std::array<bool,2> aimValid{},gripValid{};bool active{};ULONGLONG tick{};
 bool crouchEnabled{},crouchRequested{};
 bool laserSight{};
};
inline std::mutex stateMutex;
inline Snapshot state;
inline Snapshot weaponRendered;
inline void weaponApplied(const Snapshot& sample){std::lock_guard<std::mutex> lock(stateMutex);weaponRendered=sample;}
inline Snapshot weaponSample(){std::lock_guard<std::mutex> lock(stateMutex);return weaponRendered;}
inline XrVector2f roomStick{};inline ULONGLONG roomTick{};
inline float bodyTurn{};inline ULONGLONG bodyTurnTick{};
inline void followTurn(float x){std::lock_guard<std::mutex> lock(stateMutex);bodyTurn=x;bodyTurnTick=GetTickCount64();}
inline float movementYaw{};inline ULONGLONG movementTick{};
inline void headMovement(float yaw){std::lock_guard<std::mutex> lock(stateMutex);movementYaw=yaw;movementTick=GetTickCount64();}
inline Snapshot snapshot(){std::lock_guard<std::mutex> lock(stateMutex);return state;}
inline void followStick(XrVector2f value){std::lock_guard<std::mutex> lock(stateMutex);roomStick=value;roomTick=GetTickCount64();}
inline DWORD packet=1;
inline XINPUT_GAMEPAD lastDeliveredPad{};inline bool deliveredPadValid{};
inline bool samePad(const XINPUT_GAMEPAD& a,const XINPUT_GAMEPAD& b){
 return a.wButtons==b.wButtons&&a.bLeftTrigger==b.bLeftTrigger&&a.bRightTrigger==b.bRightTrigger&&
  a.sThumbLX==b.sThumbLX&&a.sThumbLY==b.sThumbLY&&a.sThumbRX==b.sThumbRX&&a.sThumbRY==b.sThumbRY;
}
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
inline GetState originalGetState{};
inline std::atomic<uint64_t> getStateCalls{},injectedCalls{};
inline SHORT axis(float value){return std::isfinite(value)?SHORT(std::clamp(value,-1.f,1.f)*32767.f):0;}
inline BYTE trigger(float value){return std::isfinite(value)?BYTE(std::clamp(value,0.f,1.f)*255.f):0;}
inline bool fresh(const Snapshot& sample,ULONGLONG now){return sample.active&&now>=sample.tick&&now-sample.tick<250;}
inline void publish(Snapshot sample){std::lock_guard<std::mutex> lock(stateMutex);state=sample;++packet;}
inline void clear(){weaponApplied({});publish({});followStick({});headMovement(0);followTurn(0);}
inline void merge(XINPUT_GAMEPAD& native,const XINPUT_GAMEPAD& vr){
 native.sThumbLX=vr.sThumbLX;native.sThumbLY=vr.sThumbLY;
 native.sThumbRX=vr.sThumbRX;native.sThumbRY=vr.sThumbRY;
 native.bLeftTrigger=std::max(native.bLeftTrigger,vr.bLeftTrigger);
 native.bRightTrigger=std::max(native.bRightTrigger,vr.bRightTrigger);native.wButtons|=vr.wButtons;
}
inline DWORD WINAPI getState(DWORD user,XINPUT_STATE* output){
 ++getStateCalls;
 const DWORD result=originalGetState?originalGetState(user,output):ERROR_DEVICE_NOT_CONNECTED;
 if(user!=0||!output)return result;
 std::lock_guard<std::mutex> lock(stateMutex);const auto now=GetTickCount64();
 if(!fresh(state,now)){deliveredPadValid=false;return result;}
 // Assemble both movement axes and all overrides in one local gamepad.
 // Publish only the completed pair, with its matching packet number.
 XINPUT_GAMEPAD delivered=result==ERROR_SUCCESS?output->Gamepad:XINPUT_GAMEPAD{};
 merge(delivered,state.pad);
 if(now-movementTick<100){const auto rotated=kharvox::rotateMovementStickForDirection({state.pad.sThumbLX/32767.f,state.pad.sThumbLY/32767.f},movementYaw);delivered.sThumbLX=axis(rotated.x);delivered.sThumbLY=axis(rotated.y);}
 if(!(state.pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)&&now-roomTick<100&&std::abs(int(state.pad.sThumbLX))<4915&&std::abs(int(state.pad.sThumbLY))<4915){delivered.sThumbLX=axis(roomStick.x);delivered.sThumbLY=axis(roomStick.y);}
 if((state.snapTurn||state.managedTurn)&&!(state.pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER))delivered.sThumbRX=0;
 if(now-bodyTurnTick<100&&(state.snapTurn||state.managedTurn||std::abs(int(state.pad.sThumbRX))<4915)&&!(state.pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER))delivered.sThumbRX=axis(bodyTurn);
 // Camera/physics updates and expiration can change the final axes between
 // XR publications. Signal the actual returned state, not just the XR sample.
 if(!deliveredPadValid||!samePad(lastDeliveredPad,delivered)){
  ++packet;lastDeliveredPad=delivered;deliveredPadValid=true;
 }
 *output=XINPUT_STATE{packet,delivered};++injectedCalls;return ERROR_SUCCESS;
}
// KHARVOX uses a main-module XInput IAT bridge. Eternal imports GetState by
// ordinal (2 in its shipped XInput1_3), so identify the resolved export address,
// never assume a common ordinal number or patch unrelated DLL imports.
inline bool install(){
 static bool installed=false;if(installed)return true;
 auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
 const auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
 const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
 const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
 if(!directory.VirtualAddress)return false;
 const auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);
 for(auto row=imports;row->Name;++row){
  const char* name=reinterpret_cast<char*>(base+row->Name);
  if(_stricmp(name,"xinput1_3.dll")&&_stricmp(name,"xinput1_4.dll")&&_stricmp(name,"xinput9_1_0.dll"))continue;
  auto library=GetModuleHandleA(name);if(!library)continue;
  auto expected=GetProcAddress(library,"XInputGetState");if(!expected)continue;
  for(auto thunk=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+row->FirstThunk);thunk->u1.Function;++thunk){
   if(thunk->u1.Function!=reinterpret_cast<uintptr_t>(expected))continue;
   HMODULE pin{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&getState),&pin))return false;
   DWORD previous{};if(!VirtualProtect(&thunk->u1.Function,sizeof(void*),PAGE_READWRITE,&previous))return false;
   originalGetState=reinterpret_cast<GetState>(expected);
   InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&thunk->u1.Function),reinterpret_cast<void*>(&getState));
   DWORD ignored{};VirtualProtect(&thunk->u1.Function,sizeof(void*),previous,&ignored);installed=true;return true;
  }
 }
 return false;
}
}
