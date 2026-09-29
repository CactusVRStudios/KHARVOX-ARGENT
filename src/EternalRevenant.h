#pragma once
#include "RevenantPolicy.h"
#include <windows.h>
#include <atomic>
namespace argent::revenant {
inline std::atomic<uintptr_t> actor{},expectedType{};
inline std::atomic<bool> installed{};
inline bool active(){return actor.load()!=0;}
inline uintptr_t refresh(uintptr_t player,uintptr_t playerType){
 SIZE_T got{};auto read=[&](uintptr_t address,void* out,size_t size){return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&got)&&got==size;};
 const auto next=installed.load()?detect(player,playerType,expectedType.load(),read):0;
 actor=next;return next;
}
bool install(unsigned char* image) noexcept;
}
