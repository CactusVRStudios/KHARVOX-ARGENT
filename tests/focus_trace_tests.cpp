#include "../src/FocusTrace.h"
#include <cstdint>
#include <limits>
#include <iostream>
extern "C" {void* argentFocusResume{};int bridgeFixture();void bridgeFixtureResume();}
static bool arguments{};
extern "C" void argentFocusTransform(void* tracker,void* owner){arguments=uintptr_t(tracker)==0x5678&&uintptr_t(owner)==0x1234;}
int main(){
 argentFocusResume=reinterpret_cast<void*>(&bridgeFixtureResume);
 if(bridgeFixture()||!arguments)return 1;
 float start[]{1,2,3},nearEnd[]{4,2,3},farEnd[]{11,2,3};
 const float origin[]{10,20,30},up[]{0,0,1};
 if(!argent::camera::headFocusTrace(start,nearEnd,farEnd,origin,up))return 2;
 if(start[0]!=10||start[1]!=20||start[2]!=30||nearEnd[0]!=10||nearEnd[1]!=20||nearEnd[2]!=33||farEnd[2]!=40)return 3;
 const float side[]{0,-1,0};
 if(!argent::camera::headFocusTrace(start,nearEnd,farEnd,origin,side)||nearEnd[1]!=17||farEnd[1]!=10)return 4;
 float invalid[]{std::numeric_limits<float>::quiet_NaN(),0,0};
 if(argent::camera::headFocusTrace(start,nearEnd,farEnd,invalid,side)||start[0]!=10||nearEnd[1]!=17)return 5;
 farEnd[0]=std::numeric_limits<float>::infinity();
 if(argent::camera::headFocusTrace(start,nearEnd,farEnd,origin,up)||nearEnd[1]!=17)return 6;
 std::cout<<"Focus bridge registers/flags/arguments and HMD ray reach/invalid-input policy verified\n";
}
