#pragma once
#include "WeaponHandling.h"
#include "../BodyYawFollow.h"
namespace argent::input {
// Zone bounds and release/entry hysteresis from KHARVOX BackWeaponPolicy.
// Eternal has a dedicated chainsaw action: no favorite or inventory fallback.
struct ShoulderChainsaw {
 bool zone{},armed{},previousGrip{},consumed{};uint64_t until{};
 bool update(XrPosef head,XrPosef grip,bool tracking,bool enabled,bool down,bool left,uint64_t now){
  if(!down){armed=true;consumed=false;}
  if(!enabled||!tracking){zone=false;armed=!down;until=0;previousGrip=down;return false;}
  const auto delta=rotate(inverse(camera::yawQuaternion(camera::yawDegrees(head.orientation))),sub(grip.position,head.position));
  const float x=left?-delta.x:delta.x;
  if(zone)zone=x>=-.02f&&x<=.82f&&delta.y>=-.92f&&delta.y<=.26f&&delta.z>=-.01f&&delta.z<=.86f;
  else zone=x>=.06f&&x<=.68f&&delta.y>=-.78f&&delta.y<=.12f&&delta.z>=.06f&&delta.z<=.72f;
  if(zone&&armed&&down&&!previousGrip){armed=false;consumed=true;until=now+100;}
  previousGrip=down;return now<until;
 }
};
}
