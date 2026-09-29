#pragma once
#include <cstdint>
namespace argent::hud {
inline int tutorialKind(uintptr_t table){
 switch(table){case 0x2d19f98:return 0;case 0x2d07f90:return 1;case 0x2d08128:return 2;default:return -1;}
}
}
