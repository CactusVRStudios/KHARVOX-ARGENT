#include "../src/EternalRevenant.h"
#include "../src/EternalPresentation.h"
#include "../src/EternalCameraHook.h"
#include <vector>
#include <cstring>
#include <string>
#include <stdexcept>
#include <iostream>
namespace argent {void log(const std::string&) {}}
namespace {
using Think=void(__fastcall*)(void*,const void*,const void*);
using SetBasis=void(__fastcall*)(void*,const float*);
using SetView=void(__fastcall*)(void*,const float*,bool);
unsigned views{},axes{},calls{},physics{};bool aimReady=true;
float desired[3]{};uint64_t buttons{},oldButtons{};
void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
void __fastcall basis(void*,const float*){++axes;}
void __fastcall view(void* object,const float* angles,bool force){
 check(force,"Local input buffers were not forced to HMD view");++views;std::memcpy(desired,angles,12);
 const float delta[]{12,-40,0};std::memcpy(reinterpret_cast<unsigned char*>(object)+0x58e8+0x3fa8,delta,12);
}
void __fastcall native(void*,const void* previous,const void* current){
 ++calls;std::memcpy(&buttons,reinterpret_cast<const unsigned char*>(current)+0x10,8);
 std::memcpy(&oldButtons,reinterpret_cast<const unsigned char*>(previous)+0x10,8);
}
}
namespace argent::camera {
void publishPhysics(uintptr_t,XrVector3f) noexcept {++physics;}
bool revenantAim(uintptr_t,float* axis) noexcept {const float m[]{0,.8660254f,.5f,-1,0,0,0,-.5f,.8660254f};std::memcpy(axis,m,sizeof(m));return aimReady;}
}
namespace argent::revenant {void* installFixture(Think,SetBasis,SetView,uintptr_t);}
int main(){try{
 std::vector<unsigned char> player(0x8900),demon(0x38000);
 auto p=uintptr_t(player.data()),d=uintptr_t(demon.data());
 auto put=[](uintptr_t a,const auto& v){std::memcpy(reinterpret_cast<void*>(a),&v,sizeof(v));};
 put(p,uintptr_t(42));put(d,uintptr_t(84));put(p+0x88b0,argent::revenant::Handle{7,7,d});put(d+0x20a08,argent::revenant::Handle{8,8,p});
 demon[0x20a48]=demon[0x20a49]=1;
 const std::array<uint64_t,5> bindings{1,0x4000000,0x400000,4,0x100000000};put(d+0x37490,bindings);
 argent::presentation::player=p;argent::presentation::playerVtable=42;argent::presentation::gameplayInput=true;
 auto hook=reinterpret_cast<Think>(argent::revenant::installFixture(native,basis,view,84));
 argent::input::Snapshot sample{};sample.active=true;sample.tick=GetTickCount64();sample.revenantActor=d;
 sample.pad.bLeftTrigger=sample.pad.bRightTrigger=255;sample.pad.wButtons=XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B;
 argent::input::publish(sample);
 std::array<unsigned char,0x98> prior{},command{};
 hook(demon.data(),prior.data(),command.data());
 check(calls==1&&views==1&&axes==1&&physics==1,"Native adapter bypassed local-view synchronization");
 check(std::abs(desired[0]+30)<.001f&&std::abs(desired[1]-90)<.001f,"Native view setter lost pitch or yaw");
 check(buttons==(1ull|0x4000000ull|0x400000ull|4ull|0x100000000ull)&&oldButtons==0,"Wrong native action masks on first tick");
 const auto held=buttons;hook(demon.data(),prior.data(),command.data());
 check(oldButtons==held&&buttons==held,"Held mode input retriggered as new press");
 sample.pad={};argent::input::publish(sample);hook(demon.data(),prior.data(),command.data());
 check(buttons==0&&oldButtons==held,"Release edge missing");
 check(command==std::array<unsigned char,0x98>{}&&prior==command,"Hook modified caller input storage");
 const auto before=views;command[9]=1;hook(demon.data(),prior.data(),command.data());check(views==before,"Inhibited tutorial/pause changed native view");command[9]=0;
 demon[0x20a48]=0;hook(demon.data(),prior.data(),command.data());check(views==before,"Stale cached actor controlled non-local demon");demon[0x20a48]=1;
 aimReady=false;hook(demon.data(),prior.data(),command.data());check(views==before,"Missing HMD aim changed native view");
 std::cout<<"Revenant native adapter: local view, action edges, pause and caller preservation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

