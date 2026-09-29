#pragma once
#include "PresentationPolicy.h"
#include <cstddef>
namespace argent::presentation {
// Offsets and enum bounds from this EXE's reflected gameFrameReturn_t and
// idPlayerMechanicLedgeGrabState_t. Inactive player views are not initialized.
inline bool activeLedge(int state){return state>=0&&state<16;}
template<class Read> bool decodeFrame(Read read,uint64_t tick,Sample& output){
 unsigned char valid{},inGame{},paused{},cutscene{};
 if(!read(0x40,&valid,1)||!read(0x41,&inGame,1)||valid>1||inGame>1)return false;
 Sample result;result.tick=tick;result.valid=valid;result.inGame=inGame;
 if(valid&&inGame){
  if(!read(0x8219,&paused,1)||!read(0x65,&cutscene,1)||paused>1||cutscene>1)return false;
  result.paused=paused;result.cutscene=cutscene;
 }
 output=result;return true;
}
}
