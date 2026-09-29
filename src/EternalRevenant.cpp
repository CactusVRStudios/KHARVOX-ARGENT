#include "EternalRevenant.h"
#include "EternalPresentation.h"
#include "EternalCameraHook.h"
#include "EternalCameraMath.h"
#include "EternalBuildProfile.h"
#include "Diagnostics.h"
#include "QuadRuntime.h"
#include <MinHook.h>
#include <cstring>
namespace argent::revenant { namespace {
using Think=void(__fastcall*)(void*,const void*,const void*);
using SetBasis=void(__fastcall*)(void*,const float*);
using SetView=void(__fastcall*)(void*,const float*,bool);
Think original{};SetBasis setBasis{};SetView setView{};
thread_local uintptr_t commandActor{};
thread_local uint64_t previousActions{};
bool read(uintptr_t address,void* out,size_t size){SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&got)&&got==size;}
void __fastcall think(void* object,const void* previous,const void* current){
 const auto demon=uintptr_t(object);Handle back{};
 const bool controlled=read(demon+0x20a08,&back,sizeof(back))&&resolved(back)&&
    detect(back.pointer,presentation::playerVtable.load(),expectedType.load(),read)==demon;
 if(controlled){
  presentation::player=back.pointer;presentation::playerTick=GetTickCount64();actor=demon;
 }
 const auto sample=input::snapshot();
 if(!controlled||demon!=actor.load()||!presentation::gameplayInput.load()||!input::fresh(sample,GetTickCount64())||sample.revenantActor!=demon||!current){commandActor=0;previousActions=0;original(object,previous,current);return;}
 // The game camera reads command angles + physics deltaViewAngles. Feed that
 // exact contract, retaining native physics, projectile origins and animation.
 std::array<unsigned char,commandSize> command{},oldCommand{};camera::Basis aim{};
 XrVector3f foot{};float delta[3]{};std::array<uint64_t,5> bindings{};
 if(!read(demon+0x58e8+0xb0,&foot,sizeof(foot))||!camera::validPosition(foot)||
    !read(uintptr_t(current),command.data(),command.size())||
    !read(demon+0x37490,bindings.data(),sizeof(bindings))){original(object,previous,current);return;}
 camera::publishPhysics(demon,foot);
 uint16_t angles[3]{};float desired[3]{};
 if(!camera::revenantAim(demon,aim.data())||!viewAngles(aim.data(),desired)){commandActor=0;previousActions=0;original(object,previous,current);return;}
 // Inhibited native commands (pause, tutorial, possession intro) retain their
 // original semantics. No simulation state is touched from the XR thread.
 if(command[9]||!previous||!read(uintptr_t(previous),oldCommand.data(),oldCommand.size())){commandActor=0;previousActions=0;original(object,previous,current);return;}
 // Local DemonCamera reads the player's input adapter, whereas remote paths
 // decode usercmd shorts. The native setter synchronizes both input buffers
 // and physics deltaViewAngles; merely replacing usercmd would miss local VR.
 setView(object,desired,true);
 if(!read(demon+0x58e8+0x3fa8,delta,sizeof(delta))||!encodeView(aim.data(),delta,angles)){commandActor=0;previousActions=0;original(object,previous,current);return;}
 std::memcpy(command.data()+0x1c,angles,sizeof(angles));
 uint64_t buttons{};std::memcpy(&buttons,command.data()+0x10,8);
 buttons=actionButtons(buttons,bindings,sample.pad.bRightTrigger>127,sample.pad.bLeftTrigger>127,
  (sample.pad.wButtons&XINPUT_GAMEPAD_B)!=0,(sample.pad.wButtons&XINPUT_GAMEPAD_A)!=0);
 std::memcpy(command.data()+0x10,&buttons,8);
 // Native edge detection receives the previous VR action state as well.
 // Otherwise a held mode button can toggle on every tick when its flat bind
 // differs from our semantic LT channel.
 uint64_t mask=0;for(auto b:bindings)mask|=b;
 uint64_t oldButtons{};std::memcpy(&oldButtons,oldCommand.data()+0x10,8);
 oldButtons=(oldButtons&~mask)|(commandActor==demon?previousActions:0);
 std::memcpy(oldCommand.data()+0x10,&oldButtons,8);
 commandActor=demon;previousActions=buttons&mask;
 // Some Revenant attacks precede the shared camera update. Publish the same
 // aim through the native setter before those attacks, including pitch.
 setBasis(object,aim.data());
 original(object,oldCommand.data(),command.data());
 if(extendedLogging()){static unsigned count{};if(++count%300==1)log("ETERNAL_REVENANT input=1 aim=HMD physics=demon forwardZ="+std::to_string(aim[2]));}
}
}
#ifdef ARGENT_REVENANT_TESTING
void* installFixture(Think native,SetBasis basis,SetView view,uintptr_t type){original=native;setBasis=basis;setView=view;expectedType=type;return reinterpret_cast<void*>(&think);}
#endif
bool install(unsigned char* image) noexcept {
 const auto type=build::rva(0x2d99918),update=build::rva(0x133daa0),basis=build::rva(0x12c07c0),view=build::rva(0x12c0620);
 if(!type||!update||!basis||!view)return false;
 constexpr unsigned char thinkBytes[]{0x40,0x55,0x53,0x56,0x57,0x48,0x8d,0xac,0x24,0x58,0xff,0xff,0xff,0x48,0x81,0xec,0xa8,0x01,0,0};
 if(std::memcmp(image+update,thinkBytes,sizeof(thinkBytes)))return false;
 const unsigned char basisBytes[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30};
 if(std::memcmp(image+basis,basisBytes,sizeof(basisBytes)))return false;
 constexpr unsigned char viewBytes[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20};
 if(std::memcmp(image+view,viewBytes,sizeof(viewBytes)))return false;
 uintptr_t method{};std::memcpy(&method,image+type+0x1968,8);if(method!=uintptr_t(image)+update)return false;
 setBasis=reinterpret_cast<SetBasis>(image+basis);setView=reinterpret_cast<SetView>(image+view);expectedType=uintptr_t(image)+type;
 if(MH_CreateHook(image+update,reinterpret_cast<void*>(&think),reinterpret_cast<void**>(&original))!=MH_OK)return false;
 if(MH_EnableHook(image+update)!=MH_OK){MH_RemoveHook(image+update);return false;}
 installed=true;log("ETERNAL_REVENANT ready=1 detection=resolved-local-first-person input=native-bindings");return true;
}
}
