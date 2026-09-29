#include <cstdint>
#include <iostream>
extern "C" {void* argentMeathookResume{};int bridgeFixture(int);void bridgeFixtureResume();}
static bool apply{},arguments{};
extern "C" bool argentMeathookTargetView(void* weapon,void* owner,float* origin,float* angles) noexcept {
 arguments=uintptr_t(weapon)==0x1234&&uintptr_t(owner)==0x5678&&reinterpret_cast<uintptr_t>(angles)-reinterpret_cast<uintptr_t>(origin)==0x70;
 if(apply){origin[0]=1;origin[1]=2;origin[2]=3;angles[0]=20;angles[1]=30;angles[2]=0;}
 return apply;
}
int main(){
 argentMeathookResume=reinterpret_cast<void*>(&bridgeFixtureResume);
 for(bool enabled:{false,true}){apply=enabled;arguments=false;if(bridgeFixture(enabled)||!arguments)return 1;}
 std::cout<<"Meathook bridge arguments, registers, flags, native fallback and origin caches verified\n";
}
