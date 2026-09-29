#pragma once
#include "ControllerInput.h"
#include "ObjectiveDossierButton.h"
#include "UseMeleeButton.h"
namespace argent::input {
inline bool gameplayMappingContext(bool world,bool gameplay,bool scriptedMovement){return world&&(gameplay||scriptedMovement);}
struct HandsInput {std::array<XrVector2f,2> stick{};std::array<float,2> trigger{};std::array<bool,2> grip{},lower{},upper{},click{},menu{};};
// Match KHARVOX: the narrow mode swaps stick clicks but preserves face
// buttons and axes. Pose/velocity/haptics arrays always retain physical sides.
inline void routeLeftHandButtons(HandsInput& h,bool left,bool full){
 if(!left)return;
 std::swap(h.click[0],h.click[1]);
 if(full){std::swap(h.lower[0],h.lower[1]);std::swap(h.upper[0],h.upper[1]);std::swap(h.stick[0],h.stick[1]);}
 // The physical menu button remains Pause in both modes.
}
// KHARVOX's right-handed logical layout. Game actions are deliberately routed
// separately from raw menu controls; a held button cannot change meaning.
struct GameplayMapping {
 ObjectiveDossierButton dossier;
 UseMeleeButton useMelee;
 bool previousRevenant{},previousTutorial{};
 bool initialized{},previousWorld{},wheelHeld{},menuApplyHeld{},flameBlocked{},equipmentGripHeld{},equipmentPulseActive{};
 ULONGLONG equipmentPulseStart{};std::array<bool,2> held{},blocked{};
 void filterCalibration(HandsInput& h){
  useMelee={};equipmentPulseActive=false;
  h.trigger={};h.grip={};h.click={};h.lower[0]=false;
  // Keep both sticks: right-down opens the wheel, left selects, releasing
  // confirms. Right-up selects Crucible. These are needed to calibrate weapons.
 }
 std::array<bool,10> contextBlock{};std::array<bool,2> triggerBlock{},stickBlock{};
 XINPUT_GAMEPAD map(const HandsInput& raw,bool world,ULONGLONG now,bool legacy=false,bool dossierMenu=false,bool tutorial=false,bool revenant=false){
  const bool tutorialInput=tutorial&&!dossierMenu&&!revenant;
  // A visible tutorial owns input even when a playable camera remains behind it.
  // Presentation changes must not swap physical B back to gameplay Jump.
  world=world&&!tutorialInput;
  HandsInput h=raw;const bool changed=!initialized||world!=previousWorld||revenant!=previousRevenant||tutorialInput!=previousTutorial;
  previousTutorial=tutorialInput;
  if(changed){wheelHeld=false;equipmentPulseActive=false;equipmentGripHeld=false;dossier={};useMelee={};}
  previousRevenant=revenant;
  for(int hand=0;hand<2;++hand){
   bool* buttons[]={&h.grip[hand],&h.lower[hand],&h.upper[hand],&h.click[hand],&h.menu[hand]};
   for(int b=0;b<5;++b){auto& blocked=contextBlock[hand*5+b];if(changed)blocked=*buttons[b];if(!*buttons[b])blocked=false;if(blocked)*buttons[b]=false;}
   if(changed){triggerBlock[hand]=h.trigger[hand]>.1f;stickBlock[hand]=std::hypot(h.stick[hand].x,h.stick[hand].y)>.2f;}
   if(h.trigger[hand]<.1f)triggerBlock[hand]=false;
   if(std::hypot(h.stick[hand].x,h.stick[hand].y)<.2f)stickBlock[hand]=false;
   if(triggerBlock[hand])h.trigger[hand]=0;
   if(stickBlock[hand])h.stick[hand]={};
  }
  const std::array<bool,2> face{h.lower[1],h.upper[1]};
  if(initialized&&world!=previousWorld)for(int i=0;i<2;++i)blocked[i]=face[i]&&held[i];
  initialized=true;previousWorld=world;
  std::array<bool,2> routed{};for(int i=0;i<2;++i){if(!face[i])blocked[i]=false;routed[i]=face[i]&&!blocked[i];held[i]=face[i];}
  XINPUT_GAMEPAD p{};p.sThumbLX=axis(h.stick[0].x);p.sThumbLY=axis(h.stick[0].y);p.sThumbRX=axis(h.stick[1].x);
  auto button=[&](bool down,WORD mask){if(down)p.wButtons|=mask;};
  // Logical turn stick opens; logical movement stick selects. Physical sides
  // are resolved before mapping, including full left-hand swap.
  wheelHeld=world&&!revenant&&(wheelHeld?h.stick[1].y<-.55f:h.stick[1].y<-.75f);
  // One equipment-cycle pulse per grip press. The wheel's RB channel is
  // reserved for right-stick down; equipment uses D-pad Left.
  if(!world||revenant||wheelHeld||now<equipmentPulseStart)equipmentPulseActive=false;
  if(world&&!revenant&&!wheelHeld&&h.grip[0]&&!equipmentGripHeld){equipmentPulseActive=true;equipmentPulseStart=now;}
  equipmentGripHeld=raw.grip[0];
  if(wheelHeld&&raw.lower[0])flameBlocked=true;
  if(!raw.lower[0])flameBlocked=false;
  menuApplyHeld=!world&&!dossierMenu&&!tutorialInput&&(menuApplyHeld?h.stick[1].y<-.55f:h.stick[1].y<-.75f);
  if(changed)dossier={};
  const auto dossierAction=dossier.update(raw.click[0],!revenant&&(world||(tutorial&&!dossierMenu))&&!wheelHeld&&!contextBlock[3],now);
  const auto useAction=useMelee.update(raw.click[1],(world||tutorialInput)&&!revenant&&!wheelHeld&&!contextBlock[8],now);
  if(tutorialInput){
   // Keep native confirm/back, but retain the advertised VR weapon-mod
   // shortcut: logical left Y means D-pad Up, not native Xbox Y.
   // Never combine actions: B to close must not also generate A/confirm.
   button(routed[0],XINPUT_GAMEPAD_A);button(routed[1],XINPUT_GAMEPAD_B);
   button(h.lower[0],XINPUT_GAMEPAD_X);button(h.upper[0],XINPUT_GAMEPAD_DPAD_UP);
   button(h.grip[0],XINPUT_GAMEPAD_LEFT_SHOULDER);button(h.grip[1],XINPUT_GAMEPAD_RIGHT_SHOULDER);
   p.bLeftTrigger=trigger(h.trigger[0]);p.bRightTrigger=trigger(h.trigger[1]);
   p.sThumbRX=p.sThumbRY=0;
   // Expose every D-pad channel without adding physical pitch. Retain the
   // established Crucible shortcut and weapon-wheel shortcut.
   if(h.stick[1].y>.75f&&std::abs(h.stick[1].x)<.55f)button(true,XINPUT_GAMEPAD_DPAD_RIGHT);
   if(h.stick[1].y<-.75f&&std::abs(h.stick[1].x)<.55f)button(true,XINPUT_GAMEPAD_RIGHT_SHOULDER);
   if(h.stick[1].x<-.75f&&std::abs(h.stick[1].y)<.55f)button(true,XINPUT_GAMEPAD_DPAD_LEFT);
   if(h.stick[1].x>.75f&&std::abs(h.stick[1].y)<.55f)button(true,XINPUT_GAMEPAD_DPAD_UP);
   button(useAction==UseMeleeButton::Action::Use,XINPUT_GAMEPAD_LEFT_THUMB);
   button(useAction==UseMeleeButton::Action::Melee,XINPUT_GAMEPAD_RIGHT_THUMB);
  }else if(!world){
   // Dossier owns right-stick navigation while in Quad. Other menu/fallback
   // contexts never forward pitch, including temporary loss of hands.
   p.sThumbRY=dossierMenu?axis(h.stick[1].y):0;p.bLeftTrigger=trigger(h.trigger[0]);p.bRightTrigger=trigger(h.trigger[1]);
   // Eternal's graphics Apply uses native R3. Reserve stick-down for it,
   // without also navigating; contextBlock/stickBlock require a fresh press.
   button(menuApplyHeld||h.click[1],XINPUT_GAMEPAD_RIGHT_THUMB);
   if(menuApplyHeld)p.sThumbRX=p.sThumbRY=0;
   button(routed[0],XINPUT_GAMEPAD_A);button(routed[1],XINPUT_GAMEPAD_B);
   button(h.lower[0],XINPUT_GAMEPAD_X);button(h.upper[0],XINPUT_GAMEPAD_Y);
   button(h.grip[0],XINPUT_GAMEPAD_LEFT_SHOULDER);button(h.grip[1],XINPUT_GAMEPAD_RIGHT_SHOULDER);
  }else if(revenant){
   // Semantic channels consumed by the Revenant native-command adapter.
   p.bRightTrigger=trigger(h.trigger[1]);p.bLeftTrigger=trigger(h.trigger[0]);
   button(routed[1],XINPUT_GAMEPAD_A); // B: jump / hold jetpack.
   button(routed[0],XINPUT_GAMEPAD_B); // A: afterburner / dash.
  }else if(wheelHeld){
   p.wButtons=XINPUT_GAMEPAD_RIGHT_SHOULDER;
   // The readable SWF maps pixel X to viewer-right. Match its native
   // selection direction; a mirrored canvas must not be fixed via input.
   p.sThumbRX=axis(h.stick[0].x);p.sThumbRY=axis(h.stick[0].y);
   p.sThumbLX=p.sThumbLY=0;
  }else{
   button(routed[1],XINPUT_GAMEPAD_A); // Physical B -> Jump
   button(routed[0],XINPUT_GAMEPAD_B); // Right A is always Dash (also with old config files).
   p.bRightTrigger=trigger(h.trigger[1]);p.bLeftTrigger=h.grip[1]?255:0;
   button(h.trigger[0]>.55f,XINPUT_GAMEPAD_LEFT_SHOULDER); // JOY5: _quick2 fires the selected equipment.
   button(equipmentPulseActive&&now-equipmentPulseStart<80,XINPUT_GAMEPAD_DPAD_LEFT); // Native Switch Equipment.
   if(h.stick[1].y>.75f&&std::abs(h.stick[1].x)<.55f){
    button(true,XINPUT_GAMEPAD_DPAD_RIGHT); // JOY_DPAD_RIGHT: _weap1 / Crucible.
    p.sThumbRX=0; // Vertical shortcut must not also turn the player.
   }
   button(h.upper[0],XINPUT_GAMEPAD_DPAD_UP);
   // Eternal binds JOY8/R3 to _quick1 (melee), JOY7/L3 to _quickUse.
   // As in KHARVOX/DOOM 2016: 100 ms USE first, then melee (never both).
   button(useAction==UseMeleeButton::Action::Use,XINPUT_GAMEPAD_LEFT_THUMB);
   button(useAction==UseMeleeButton::Action::Melee,XINPUT_GAMEPAD_RIGHT_THUMB);
   // Ordinary locomotion must not pitch independently of the HMD.
   button(h.lower[0]&&!flameBlocked,XINPUT_GAMEPAD_Y); // Left X: native pm_flameBelchActivatedByJoyY (default 1).
  }
  // Only manual world locomotion: menu/wheel coordinates stay raw, and the
  // independently synthesized roomscale stick bypasses this mapping entirely.
  if(world&&!wheelHeld){
   const auto movement=kharvox::fullTravelMovementStick({h.stick[0].x,h.stick[0].y});
   p.sThumbLX=axis(movement.x);p.sThumbLY=axis(movement.y);
  }
  // Tutorial presentation is Quad, but its requested inventory action must
  // still reach the game. Keep combat routing in the gameplay branch above.
  button(dossierAction==ObjectiveDossierButton::Action::Objective,XINPUT_GAMEPAD_DPAD_DOWN);
  button(dossierAction==ObjectiveDossierButton::Action::Dossier,XINPUT_GAMEPAD_BACK);
  button(h.menu[0],XINPUT_GAMEPAD_START); // Physical left controller Menu -> Pause
  return p;
 }
};
}
