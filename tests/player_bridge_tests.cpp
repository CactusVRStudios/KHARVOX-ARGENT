#include <cstdint>
#include <iostream>
extern "C" {void* argentHandsResume{};int bridgeFixture();void bridgeFixtureResume();}
static bool arguments{};
extern "C" void argentHandsTransform(void* hands,void* root,float* origin,float* axis){
 arguments=uintptr_t(hands)==0x1234&&uintptr_t(root)==0x5678&&reinterpret_cast<char*>(axis)-reinterpret_cast<char*>(origin)==0x18;
 origin[0]=1.f;axis[0]=2.f;
}
int main(){argentHandsResume=reinterpret_cast<void*>(&bridgeFixtureResume);const auto result=bridgeFixture();if(result||!arguments){std::cerr<<"Hands bridge damaged registers, flags or caller stack\n";return 1;}std::cout<<"Native hands bridge preserves volatile registers/flags and updates exact caller origin/basis\n";}
