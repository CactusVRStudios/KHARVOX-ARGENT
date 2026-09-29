#pragma once
#include "WeaponHandling.h"
#include "../hands/HandWeaponProfile.h"
#include "../hands/CalibrationDraft.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
namespace argent::input {
struct WeaponConfig {
 calibration::ApplyCommand calibrationApply;
 bool automaticPresentation{true},cinematicsQuad{true},syncImmersive{true},movementImmersive{true},shoulderChainsaw{true},crouchEnabled{};
 bool legacyLayout{},twoHandEnabled{true},physicalGloryKill{true},bhapticsEnabled{},psvr2AdaptiveTriggers{};
 bool showHands{true};
 bool laserSight{};bool handsJump{true};bool handSmoothing{};
 bool cinematics3d{};
 // Model-local metres: forward, left, up. Shared by both dominant hands.
 XrVector3f weaponPivot{-.15f,0,0};
 std::string calibrationMode{"off"};
 float smoothSpeed{230},glorySpeed{2.8f};int gloryHands{2};
 bool leftHanded{},offhandMovement{},captureSupport{},snapTurn{};float snapDegrees{45};
 bool leftHandSwapSticks{};
 std::string profile{"default"};std::array<PoseCalibration,2> hand{};
 std::map<std::string,WeaponCalibration> weapons{{"default",{}}};
 WeaponCalibration selected()const {auto i=weapons.find(profile);auto c=i==weapons.end()?weapons.at("default"):i->second;c.twoHand=c.twoHand&&twoHandEnabled;return c;}
 WeaponCalibration selectedFor(const std::string& active)const {auto i=weapons.find(active);auto c=i==weapons.end()?weapons.at("default"):i->second;c.twoHand=c.twoHand&&twoHandEnabled&&active!="crucible"&&active!="sentinel_hammer";return c;}
};
inline bool safeCalibration(const PoseCalibration& p){return camera::validPosition(p.offset)&&length(p.offset)<=.5f&&camera::validQuaternion(p.rotation);}
inline bool readWeaponConfig(std::istream& input,WeaponConfig& output){
 WeaponConfig next;std::string line;
 while(std::getline(input,line)){if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::string key;row>>key;
  if(key=="dominant"){std::string value;row>>value;if(value!="left"&&value!="right")return false;next.leftHanded=value=="left";}
  else if(key=="left_hand_swap"){std::string v;row>>v;if(v!="buttons"&&v!="buttons-and-sticks")return false;next.leftHandSwapSticks=v=="buttons-and-sticks";}
  else if(key=="weapon_pivot"){
   if(!(row>>next.weaponPivot.x>>next.weaponPivot.y>>next.weaponPivot.z)||!camera::validPosition(next.weaponPivot)||(next.weaponPivot.x<-.45001f||next.weaponPivot.x>.15001f)||std::abs(next.weaponPivot.y)>.3f||std::abs(next.weaponPivot.z)>.3f)return false;
  }
  else if(key=="show_hands"){int v;if(!(row>>v)||v<0||v>1)return false;next.showHands=v!=0;}
  else if(key=="hands_jump"){int v;if(!(row>>v)||v<0||v>1)return false;next.handsJump=v!=0;}
  else if(key=="hand_smoothing"){int v;if(!(row>>v)||v<0||v>1)return false;next.handSmoothing=v!=0;}
  else if(key=="laser_sight"){int v;if(!(row>>v)||v<0||v>1)return false;next.laserSight=v!=0;}
  else if(key=="cinematics_3d"){int v;if(!(row>>v)||v<0||v>1)return false;next.cinematics3d=v!=0;}
  else if(key=="calibration_mode"){row>>next.calibrationMode;if(next.calibrationMode!="off"&&next.calibrationMode!="hands"&&next.calibrationMode!="weapon"&&next.calibrationMode!="support"&&next.calibrationMode!="hud")return false;}
  else if(key=="calibration_apply"){if(!(row>>next.calibrationApply.revision))return false;}
  else if(key=="calibration_apply_profile"){row>>next.calibrationApply.profile;}
  else if(key=="calibration_apply_mode"){row>>next.calibrationApply.mode;}
  else if(key=="calibration_apply_left"){int v;if(!(row>>v)||v<0||v>1)return false;next.calibrationApply.left=v!=0;}
  else if(key=="movement"){std::string value;row>>value;if(value!="head"&&value!="offhand")return false;next.offhandMovement=value=="offhand";}
  else if(key=="turn"){std::string mode;row>>mode;if(mode!="smooth"&&mode!="snap")return false;next.snapTurn=mode=="snap";if(next.snapTurn&&(!(row>>next.snapDegrees)||!std::isfinite(next.snapDegrees)||next.snapDegrees<10||next.snapDegrees>180))return false;}
  else if(key=="snap_turn_angle"){if(!(row>>next.snapDegrees)||!std::isfinite(next.snapDegrees)||next.snapDegrees<45||next.snapDegrees>90)return false;}
  else if(key=="smooth_turn_speed"){if(!(row>>next.smoothSpeed)||!std::isfinite(next.smoothSpeed)||next.smoothSpeed<150||next.smoothSpeed>400)return false;}
  else if(key=="physical_glory_kill_speed"){if(!(row>>next.glorySpeed)||!std::isfinite(next.glorySpeed)||next.glorySpeed<1||next.glorySpeed>4)return false;}
  else if(key=="physical_glory_kill_hands"){std::string v;row>>v;if(v!="left"&&v!="right"&&v!="both")return false;next.gloryHands=v=="left"?0:v=="right"?1:2;}
  else if(key=="physical_glory_kill"){int v;if(!(row>>v)||v<0||v>1)return false;next.physicalGloryKill=v!=0;}
  else if(key=="automatic_presentation"||key=="cinematics_quad"||key=="sync_immersive"||key=="movement_immersive"||key=="shoulder_chainsaw"||key=="crouch_enabled"||key=="two_hand_enabled"||key=="virtual_gunstock"||key=="bhaptics_enabled"||key=="psvr2_adaptive_triggers"){
   int value;if(!(row>>value)||value<0||value>1)return false;
   if(key=="automatic_presentation")next.automaticPresentation=value;
   else if(key=="cinematics_quad")next.cinematicsQuad=true; // Eternal cinematics always use Quad; accept legacy settings.
   else if(key=="sync_immersive")next.syncImmersive=value;
   else if(key=="movement_immersive")next.movementImmersive=value;
   else if(key=="shoulder_chainsaw")next.shoulderChainsaw=value;
   else if(key=="crouch_enabled")next.crouchEnabled=value;
   else if(key=="bhaptics_enabled")next.bhapticsEnabled=value;
   else if(key=="psvr2_adaptive_triggers")next.psvr2AdaptiveTriggers=value;
   else next.twoHandEnabled=value;
  }
  else if(key=="controller_layout"){std::string v;row>>v;if(v!="argent"&&v!="legacy")return false;next.legacyLayout=v=="legacy";}
  else if(key=="profile"){row>>next.profile;if(next.profile.empty())return false;}
  else if(key=="capture_support"){int v;if(!(row>>v)||v<0||v>1)return false;next.captureSupport=v!=0;}
  else if(key=="hand"||key=="weapon"){
   std::string name;PoseCalibration p;float pitch,yaw,roll;
   if(!(row>>name>>p.offset.x>>p.offset.y>>p.offset.z>>pitch>>yaw>>roll)||!std::isfinite(pitch+yaw+roll))return false;
   p.rotation=euler(pitch,yaw,roll);if(!safeCalibration(p))return false;
   if(key=="hand"){if(name!="left"&&name!="right")return false;next.hand[name=="right"]=p;}
   else {WeaponCalibration c;c.pose=p;int enabled;
    if(!(row>>c.support.x>>c.support.y>>c.support.z>>c.radius>>c.weight>>enabled)||!camera::validPosition(c.support)||length(c.support)<.08f||length(c.support)>1.2f||!std::isfinite(c.radius+c.weight)||c.radius<.08f||c.radius>.4f||c.weight<0||c.weight>1||(enabled!=0&&enabled!=1))return false;
    c.twoHand=enabled!=0;next.weapons[name]=c;}
  }else return false;
 }
 if(!next.weapons.count(next.profile)&&kharvox::hands::handProfile(next.profile)==kharvox::hands::HandWeaponKind::Unknown)return false;output=next;return true;
}
}
