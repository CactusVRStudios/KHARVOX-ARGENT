#include "../src/WallClimbAim.h"
#include "../src/EternalPresentation.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <vector>
extern "C" {void* argentWallGateResume{};void* argentWallImpulseResume{};
 int wallGateFixture();int wallImpulseFixture();void wallGateFixtureResume();void wallImpulseFixtureResume();}
static bool gateArgs{},impulseArgs{};
extern "C" bool argentWallClimbGate(void* mechanic,bool blocked){
 gateArgs=uintptr_t(mechanic)==0x5678&&blocked&&(uintptr_t(_AddressOfReturnAddress())&15)==8;return false;
}
extern "C" void argentWallClimbImpulse(void* mechanic,float* xy,float* z,uintptr_t caller){
 uint32_t bits{};std::memcpy(&bits,z,4);
 impulseArgs=uintptr_t(mechanic)==0x5678&&caller==0x9999&&bits==0x11&&(uintptr_t(_AddressOfReturnAddress())&15)==8;
 xy[0]=10;xy[1]=20;*z=30;
}
void check(bool b){if(!b)throw std::runtime_error("Wall-climb contract");}
int main(){try{
 using namespace argent::player;
 argentWallGateResume=reinterpret_cast<void*>(&wallGateFixtureResume);
 argentWallImpulseResume=reinterpret_cast<void*>(&wallImpulseFixtureResume);
 check(wallGateFixture()==0&&gateArgs);check(wallImpulseFixture()==0&&impulseArgs);
 for(int state=-1;state<=10;++state)check(attachedWallClimbState(state)==(state>=3&&state<=9));
 {
  std::vector<unsigned char> memory(0x35130+0x140);
  std::array<unsigned char,16> stateData{};
  const auto owner=reinterpret_cast<uintptr_t>(memory.data()),table=uintptr_t(0x12345678),stateOwner=reinterpret_cast<uintptr_t>(stateData.data());
  std::memcpy(memory.data(),&table,sizeof(table));
  std::memcpy(memory.data()+0x35130+0x18,&owner,sizeof(owner));
  std::memcpy(memory.data()+0x35130+0x138,&stateOwner,sizeof(stateOwner));
  argent::presentation::player=owner;argent::presentation::playerVtable=table;
  for(int state:{2,3,9,10}){
   std::memcpy(stateData.data()+0xc,&state,sizeof(state));
   check(argent::presentation::attachedWallClimb()==(state>=3&&state<=9));
  }
  argent::presentation::player=0;check(!argent::presentation::attachedWallClimb());
  argent::presentation::playerVtable=0;
 }
 const float normal[]{1,0,0};
 for(float nativePitch:{-.5f,0.f,.5f})for(float yaw:{-2.8f,-1.4f,0.f,1.4f,2.8f})for(float headPitch:{-.8f,0.f,.8f}){
  const float native[]{std::cos(nativePitch),0,std::sin(nativePitch)};
  const float head[]{std::cos(yaw)*std::cos(headPitch),std::sin(yaw)*std::cos(headPitch),std::sin(headPitch)};
  float result[3]{};check(wallClimbForward(native,head,result));
  check(result[0]==head[0]&&result[1]==head[1]&&result[2]==head[2]);
  check(std::abs(std::atan2(result[1],result[0])-yaw)<.0001f);
  check(std::abs(result[0]*result[0]+result[1]*result[1]+result[2]*result[2]-1)<.0001f);
  bool blocked{};check(wallClimbBlocked(result,normal,60,blocked));
  check(blocked==(-result[0]>.5f));
 }
 const float away[]{1,0,0},toward[]{-1,0,0},up[]{0,0,1};bool blocked{};
 check(wallClimbBlocked(away,normal,60,blocked)&&!blocked);
 check(wallClimbBlocked(toward,normal,60,blocked)&&blocked);
 float output[]{4,5,6};
 check(!wallClimbForward(away,up,output)&&output[0]==4&&output[2]==6);
 const float bad[]{std::numeric_limits<float>::quiet_NaN(),0,0};
 check(!wallClimbForward(bad,away,output)&&output[1]==5);
 check(!wallClimbBlocked(away,bad,60,blocked));
 std::cout<<"Wall-climb HMD yaw/pitch, gate and both bridge ABIs passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
