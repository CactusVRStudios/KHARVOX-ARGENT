#pragma once
#include "ControllerInput.h"
#include "XInputHapticsPolicy.h"
namespace argent::input {
using SetState=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
inline SetState originalSetState{};
inline std::atomic<uint32_t> rumble{},rumblePeak{};
inline std::atomic<ULONGLONG> rumbleTick{};
inline DWORD WINAPI setState(DWORD user,XINPUT_VIBRATION* vibration){
 const auto result=originalSetState?originalSetState(user,vibration):ERROR_DEVICE_NOT_CONNECTED;
 if(user||!vibration)return result;
 const auto value=kharvox::packXInputRumble(vibration->wLeftMotorSpeed,vibration->wRightMotorSpeed);
 rumble=value;rumbleTick=GetTickCount64();auto old=rumblePeak.load();while(!rumblePeak.compare_exchange_weak(old,kharvox::mergeXInputRumblePeaks(old,value))){}
 return fresh(snapshot(),GetTickCount64())?ERROR_SUCCESS:result;
}
inline bool installHaptics(){
 if(originalSetState)return true;
 auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
 auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
 const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!directory.VirtualAddress)return false;
 for(auto row=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);row->Name;++row){
  auto name=reinterpret_cast<const char*>(base+row->Name);
  if(_stricmp(name,"xinput1_3.dll")&&_stricmp(name,"xinput1_4.dll")&&_stricmp(name,"xinput9_1_0.dll"))continue;
  auto library=GetModuleHandleA(name);auto target=library?GetProcAddress(library,"XInputSetState"):nullptr;if(!target)continue;
  for(auto thunk=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+row->FirstThunk);thunk->u1.Function;++thunk){
   if(thunk->u1.Function!=reinterpret_cast<uintptr_t>(target))continue;
   DWORD old{};if(!VirtualProtect(&thunk->u1.Function,sizeof(void*),PAGE_READWRITE,&old))return false;
   originalSetState=reinterpret_cast<SetState>(target);
   InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&thunk->u1.Function),reinterpret_cast<void*>(&setState));
   DWORD ignored;VirtualProtect(&thunk->u1.Function,sizeof(void*),old,&ignored);return true;
  }
 }return false;
}
}
