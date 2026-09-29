#pragma once
namespace argent::player {
// Scoped input to the native collision-aware shape solver. Its physics flags
// and selected capsule remain engine-owned; transient usercmd bytes are restored.
struct CrouchCommand {
 unsigned char* physics;unsigned char can,up,previous;
 CrouchCommand(unsigned char* p,bool down,bool wasDown):physics(p),can(p[0x4170]),up(p[0x3dfa]),previous(p[0x3e92]){
  p[0x4170]=1;if(down)p[0x3dfa]=static_cast<unsigned char>(-127);
  if(wasDown)p[0x3e92]=static_cast<unsigned char>(-127);
 }
 ~CrouchCommand(){physics[0x4170]=can;physics[0x3dfa]=up;physics[0x3e92]=previous;}
 CrouchCommand(const CrouchCommand&)=delete;
};
}
