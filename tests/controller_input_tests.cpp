#include "../src/openxr/HapticBridge.h"
#include "../src/openxr/ControllerInput.h"
#include "../src/openxr/GameplayMapping.h"
#include "../src/openxr/ObjectiveDossierButton.h"
#include "../src/RoomscaleFollow.h"
#include "../src/openxr/ControllerProfile.h"
#include <iostream>
#include <stdexcept>
using namespace argent::input;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
DWORD WINAPI absent(DWORD,XINPUT_STATE*){return ERROR_DEVICE_NOT_CONNECTED;}
int main(){try{
 for(bool left:{false,true})for(bool full:{false,true}){
  HandsInput physical{};physical.click[0]=true;physical.lower[0]=true;physical.upper[1]=true;
  physical.stick[0]={.25f,.5f};physical.stick[1]={-.75f,-1.f};physical.menu[0]=true;
  auto routed=physical;routeLeftHandButtons(routed,left,full);
  check(routed.click[left?1:0]&&!routed.click[left?0:1],"Stick click swap incorrect");
  const int side=left&&full?1:0;
  check(routed.lower[side]&&routed.upper[1-side]&&routed.stick[side].x==.25f&&routed.stick[side].y==.5f,"Face/axis layout incorrect");
  check(routed.menu[0]&&!routed.menu[1],"Physical Pause moved");
 }
 for(bool full:{false,true}){
  GameplayMapping mapping;HandsInput physical{};ULONGLONG tick=1000;
  auto poll=[&](bool world=true,bool tutorial=false){auto logical=physical;routeLeftHandButtons(logical,true,full);return mapping.map(logical,world,tick,false,false,tutorial);};
  poll();const int move=full?1:0,turn=1-move;tick+=10;physical.stick[move].y=1;
  check(poll().sThumbLY==32767,"Left layout movement stick missing");
  physical={};tick+=10;poll();physical.stick[turn].x=.8f;tick+=10;
  check(poll().sThumbRX>0&&poll().sThumbLX==0,"Left layout turn stick missing");
  physical={};tick+=10;poll();physical.stick[turn].y=1;tick+=10;
  check(poll().wButtons&XINPUT_GAMEPAD_DPAD_RIGHT,"Left layout Crucible missing");
  physical={};tick+=10;poll();physical.stick[turn].y=-1;physical.stick[move].x=.8f;tick+=10;
  auto wheel=poll();check((wheel.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)&&wheel.sThumbRX>0&&wheel.sThumbLX==0,"Left layout wheel/select missing");
  physical={};tick+=10;poll();physical.lower[full?1:0]=true;tick+=10;
  check(poll().wButtons&XINPUT_GAMEPAD_Y,"Left layout Flame Belch missing");
  physical={};tick+=10;poll(false,true);physical.click[1]=true;tick+=10;poll(false,true);tick+=610;
  check(poll(false,true).wButtons&XINPUT_GAMEPAD_BACK,"Left layout tutorial Dossier blocked");
  physical={};tick+=250;poll(false,true);physical.upper[full?0:1]=true;tick+=10;
  check(poll(false,true).wButtons==XINPUT_GAMEPAD_B,"Left layout tutorial B mixed with gameplay Jump");
  physical={};tick+=250;poll(false,true);physical.upper[full?1:0]=true;tick+=10;
  check(poll(false,true).wButtons==XINPUT_GAMEPAD_DPAD_UP,"Left layout tutorial weapon mod swap missing or combined");
 }
 {
  GameplayMapping m;HandsInput h{};m.map(h,true,1);
  h.grip[0]=kharvox::updateGripPressed(.71f,false,kharvox::GripControllerProfile::ValveIndex);
  auto p=m.map(h,true,10);
  check((p.wButtons&XINPUT_GAMEPAD_DPAD_LEFT)&&!(p.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER),"Index squeeze must cycle equipment without the weapon wheel");
  h.grip[1]=kharvox::updateGripPressed(.71f,false,kharvox::GripControllerProfile::ValveIndex);
  check(m.map(h,true,100).bLeftTrigger==255,"Index squeeze must trigger alternate fire");
  h.grip[1]=kharvox::updateGripPressed(.6f,h.grip[1],kharvox::GripControllerProfile::ValveIndex);
  check(m.map(h,true,110).bLeftTrigger==255,"Index grip hysteresis must retain alternate fire");
  h.grip[1]=kharvox::updateGripPressed(.55f,h.grip[1],kharvox::GripControllerProfile::ValveIndex);
  check(!m.map(h,true,120).bLeftTrigger,"Index grip release must stop alternate fire");
  h.menu[0]=updateIndexPause(.8f,false);
  check(m.map(h,true,130).wButtons&XINPUT_GAMEPAD_START,"Index trackpad must reach pause");
  h.menu[0]=updateIndexPause(0.f,true);
  check(!(m.map(h,true,140).wButtons&XINPUT_GAMEPAD_START),"Index pause release stuck");
 }
 {
  for(bool leftHanded:{false,true}){
   GameplayMapping m;HandsInput h{};m.map(h,true,1);
   auto route=[&](ULONGLONG tick){auto filtered=h;m.filterCalibration(filtered);
    if(leftHanded){std::swap(filtered.trigger[0],filtered.trigger[1]);std::swap(filtered.grip[0],filtered.grip[1]);}
    return m.map(filtered,true,tick);};
   h.trigger={1,1};h.grip={true,true};h.click={true,true};h.lower[0]=true;
   h.stick[1].y=-1;auto p=route(10);
   check(m.wheelHeld&&p.wButtons==XINPUT_GAMEPAD_RIGHT_SHOULDER&&!p.bRightTrigger&&!p.bLeftTrigger,"Calibration blocked wheel or leaked combat");
   h.stick[0]={.6f,.8f};p=route(20);
   check(p.sThumbRX==axis(.6f)&&p.sThumbRY==axis(.8f)&&!p.sThumbLX&&!p.sThumbLY,"Calibration blocked weapon selection");
   h.stick={};p=route(30);
   check(!m.wheelHeld&&!p.wButtons&&!p.bRightTrigger&&!p.bLeftTrigger,"Calibration did not release wheel to confirm");
   h.stick[1].y=1;p=route(40);
   check(p.wButtons==XINPUT_GAMEPAD_DPAD_RIGHT&&!p.bRightTrigger&&!p.bLeftTrigger,"Calibration blocked Crucible");
   h.stick={};check(!route(50).wButtons,"Crucible shortcut stuck during calibration");
   m.equipmentPulseActive=true;m.equipmentPulseStart=50;
   check(!route(60).wButtons,"Equipment pulse leaked into calibration");
  }
 }
 {
  for(bool demon:{false,true}){
   GameplayMapping mapping;HandsInput raw{};mapping.map(raw,true,1,false,false,false,demon);
   for(float magnitude:{.1f,.5f,.7f,.849f,.85f,.9f,1.f})for(float angle:{0.f,.785398163f,1.570796327f,3.141592654f,-1.570796327f}){
    raw.stick[0]={std::cos(angle)*magnitude,std::sin(angle)*magnitude};
    const auto pad=mapping.map(raw,true,2,false,false,false,demon);
    const float expected=magnitude<=.5f?magnitude:std::min(1.f,.5f+(magnitude-.5f)*(.5f/.35f));
    const float x=pad.sThumbLX/32767.f,y=pad.sThumbLY/32767.f;
    check(std::abs(std::hypot(x,y)-expected)<.0001f,"Manual movement travel curve incorrect");
    check(std::abs(x*std::sin(angle)-y*std::cos(angle))<.0001f,"Travel compensation changed direction");
    for(float yaw:{-90.f,35.f,180.f}){
     const auto rotated=kharvox::rotateMovementStickForDirection({x,y},yaw);
     check(std::abs(std::hypot(rotated.x,rotated.y)-expected)<.0001f,"Movement direction rotation changed compensated speed");
    }
   }
  }
  GameplayMapping mapping;HandsInput raw{};mapping.map(raw,false,1);raw.stick[0].y=.9f;
  check(mapping.map(raw,false,2).sThumbLY==axis(.9f),"Menu navigation received travel compensation");
  raw={};mapping.map(raw,true,3);raw.stick[0].x=.7f;raw.stick[1].y=-1;
  auto wheel=mapping.map(raw,true,4);
  check(wheel.sThumbRX==axis(.7f)&&wheel.sThumbLX==0&&wheel.sThumbLY==0,"Weapon wheel received travel compensation");
  raw={};Snapshot room{};room.active=true;room.tick=GetTickCount64();room.pad=mapping.map(raw,true,5);
  clear();publish(room);followStick({.7f,.1f});XINPUT_STATE output{};
  check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbLX==axis(.7f)&&output.Gamepad.sThumbLY==axis(.1f),"Roomscale received travel compensation");
  clear();
 }
 {
  GameplayMapping m;HandsInput h{};m.map(h,true,1);h.stick[0].y=1;
  for(auto flags:{std::array<bool,3>{true,true,false},{true,false,true},{true,true,false}}){
   auto p=m.map(h,gameplayMappingContext(flags[0],flags[1],flags[2]),10);
   check(p.sThumbLY==axis(1.f),"Traversal/landing blocked held forward stick");
  }
  auto p=m.map(h,false,20,false,true);check(p.sThumbLY==0,"Held movement crossed into dossier navigation");
  h={};m.map(h,false,30,false,true);h.lower[1]=true;h.grip[0]=true;h.stick[1].y=-1;
  p=m.map(h,false,40,false,true);
  check((p.wButtons&XINPUT_GAMEPAD_A)&&(p.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER),"Dossier confirm/tab missing");
  check(!(p.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB)&&p.sThumbRY==axis(-1.f),"Dossier scrolling became graphics Apply");
  h={};h.upper[1]=true;p=m.map(h,false,50,false,true);check(p.wButtons&XINPUT_GAMEPAD_B,"Dossier Back missing");
 }
 {
  using Action=ObjectiveDossierButton::Action;ObjectiveDossierButton b;
  check(b.update(true,true,1000)==Action::None,"Press prematurely emitted objective");
  check(b.update(false,true,1599)==Action::Objective,"599 ms release was not short");
  check(b.update(false,true,1698)==Action::Objective&&b.update(false,true,1699)==Action::None,"Short pulse lifetime incorrect");
  b={};b.update(true,true,2000);check(b.update(true,true,2599)==Action::None,"Long fired before 600 ms");
  check(b.update(true,true,2600)==Action::Dossier,"Long missing at 600 ms");
  check(b.update(true,true,2700)==Action::None&&b.update(true,true,4000)==Action::None,"Held dossier repeated");
  check(b.update(false,true,4100)==Action::None,"Long release emitted objective");
  b={};b.update(true,true,5000);check(b.update(false,true,5600)==Action::Dossier,"Threshold release lost long press");
  b={};b.update(true,true,6000);b.update(true,false,6300);check(b.update(true,true,7000)==Action::None&&b.update(false,true,7010)==Action::None,"Context crossing emitted action");
  b.update(true,true,7100);check(b.update(false,true,7200)==Action::Objective,"Fresh press after context blocked");
  b={};b.update(true,true,9000);check(b.update(true,true,8999)==Action::None&&b.update(false,true,9100)==Action::None,"Backward clock generated action");
 }
 originalGetState=absent;XINPUT_STATE output{};clear();
 check(getState(0,&output)==ERROR_DEVICE_NOT_CONNECTED,"Inactive VR created a controller");
 Snapshot s;s.active=true;s.tick=GetTickCount64();s.pad.sThumbLX=axis(.5f);s.pad.bRightTrigger=trigger(1);s.pad.wButtons=XINPUT_GAMEPAD_A;publish(s);
 check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.wButtons==XINPUT_GAMEPAD_A&&output.Gamepad.bRightTrigger==255&&output.Gamepad.sThumbLX==16383,"VR input not delivered to XInput");
 s.pad.sThumbLX=0;s.pad.sThumbLY=32767;publish(s);headMovement(90);
 check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbLX<-32760&&std::abs(int(output.Gamepad.sThumbLY))<2,"Head-relative forward did not rotate left");
 const auto yawPacket=output.dwPacketNumber;headMovement(0);
 check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbLY>32760&&output.dwPacketNumber!=yawPacket,"Direction changed without a new XInput packet");
 const auto stablePacket=output.dwPacketNumber;getState(0,&output);
 check(output.dwPacketNumber==stablePacket,"Identical poll changed packet");
 headMovement(90);
 s.pad.sThumbLY=0;publish(s);followStick({0,1});
 check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbLY==32767&&output.Gamepad.sThumbLX==0,"Body follow was incorrectly rotated by head yaw");
 const auto roomPacket=output.dwPacketNumber;followStick({.5f,0});getState(0,&output);
 check(output.dwPacketNumber!=roomPacket&&output.Gamepad.sThumbLX==axis(.5f),"Roomscale changed without a new packet");
 const auto liveRoomPacket=output.dwPacketNumber;roomTick=GetTickCount64()-101;getState(0,&output);
 check(output.dwPacketNumber!=liveRoomPacket&&output.Gamepad.sThumbLX==0&&output.Gamepad.sThumbLY==0,"Expired roomscale retained packet");
 headMovement(0);followStick({});
 followTurn(-.5f);check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbRX<-16000,"Body yaw not delivered");
 s.pad.sThumbRX=axis(.6f);publish(s);check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbRX>19000,"Physical following overrode manual turn");
 s.snapTurn=true;publish(s);check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbRX<-16000,"Snap native catch-up blocked by held stick");
 s.pad.wButtons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;publish(s);check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbRX>19000,"Snap broke native weapon wheel");
 s.snapTurn=false;s.managedTurn=true;s.pad.wButtons=0;publish(s);check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbRX<-16000,"smooth turn doubled native joystick yaw");
 s.pad.wButtons=XINPUT_GAMEPAD_RIGHT_SHOULDER;publish(s);check(getState(0,&output)==ERROR_SUCCESS&&output.Gamepad.sThumbRX>19000,"smooth turn blocked weapon wheel");
 s.managedTurn=false;s.pad.sThumbRX=0;s.pad.wButtons=XINPUT_GAMEPAD_A;publish(s);followTurn(0);
 XINPUT_VIBRATION vibration{1234,50000};check(setState(0,&vibration)==ERROR_SUCCESS&&rumble.load()==kharvox::packXInputRumble(1234,50000),"Native rumble was not captured");
 check(setState(1,&vibration)==ERROR_DEVICE_NOT_CONNECTED&&setState(0,nullptr)==ERROR_DEVICE_NOT_CONNECTED,"Haptics changed other slots/null calls");
 check(getState(1,&output)==ERROR_DEVICE_NOT_CONNECTED,"VR replaced another player");
 check(getState(0,nullptr)==ERROR_DEVICE_NOT_CONNECTED,"Null output accepted");
 check(fresh(s,s.tick+249)&&!fresh(s,s.tick+250)&&!fresh(s,s.tick-1),"Tracking timeout incorrect");
 clear();check(getState(0,&output)==ERROR_DEVICE_NOT_CONNECTED,"Focus loss retained VR input");
 XINPUT_GAMEPAD native{};native.wButtons=XINPUT_GAMEPAD_B;native.bLeftTrigger=220;
 s.pad.bLeftTrigger=100;merge(native,s.pad);check(native.wButtons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B)&&native.bLeftTrigger==220,"Native buttons/triggers were lost");
 check(axis(2)==32767&&axis(-2)==-32767&&trigger(-1)==0,"Input range invalid");
 {
  GameplayMapping m;HandsInput h;m.map(h,true,10000);h.click[0]=true;
  check(!(m.map(h,true,10010).wButtons&(XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_DPAD_DOWN)),"Dossier click fired on press");
  h.click[0]=false;check(m.map(h,true,10100).wButtons==XINPUT_GAMEPAD_DPAD_DOWN,"Short L3 did not show objective");
  m.map(h,true,10300);h.click[0]=true;m.map(h,true,10400);
  check(m.map(h,true,11000).wButtons==XINPUT_GAMEPAD_BACK,"Long L3 did not open dossier");
  h.click[0]=false;check(!m.map(h,true,11200).wButtons,"Long L3 release generated second action");
  h.lower[0]=true;check(m.map(h,true,11210).wButtons==XINPUT_GAMEPAD_Y,"X did not trigger Flame Belch");
  h.lower[0]=false;h.menu[0]=true;check(m.map(h,true,11211).wButtons==XINPUT_GAMEPAD_START,"Left Menu did not pause");
  h.menu[0]=false;h.menu[1]=true;check(!m.map(h,true,11212).wButtons,"Right Menu unexpectedly paused");
  h={};m.map(h,true,11211);h.click[1]=true;
  check(m.map(h,true,11212).wButtons==XINPUT_GAMEPAD_LEFT_THUMB,"Right click did not start exclusive Use phase");
  check(m.map(h,true,11213).wButtons==XINPUT_GAMEPAD_LEFT_THUMB,"Held Use dropped");
  h.click[1]=false;check(m.map(h,true,11214).wButtons==XINPUT_GAMEPAD_LEFT_THUMB,"Short click lost Use pulse");
  h.click[1]=true;m.map(h,true,11215);
  check(!m.map(h,false,11216).wButtons,"Held Use leaked into menu");
  h={};m.map(h,false,11217);h.click[1]=true;
  check(m.map(h,false,11218).wButtons==XINPUT_GAMEPAD_RIGHT_THUMB,"Menu R3 gained gameplay Use");
  check(!m.map(h,true,11219).wButtons,"Held menu R3 became Use");
  h={};m.map(h,true,11220);h.click[1]=true;h.stick[1].y=-1;
  check(m.map(h,true,11221).wButtons==XINPUT_GAMEPAD_RIGHT_SHOULDER,"Wheel right click leaked Melee/Use");
  h.lower[0]=true;check(m.map(h,true,11221).wButtons==XINPUT_GAMEPAD_RIGHT_SHOULDER,"Wheel X leaked Flame Belch");
  h.click[0]=true;check(m.map(h,true,11221).wButtons==XINPUT_GAMEPAD_RIGHT_SHOULDER,"Wheel L3 leaked dossier");
  h.stick[1].y=0;check(!m.map(h,true,11222).wButtons,"Held wheel buttons became actions after wheel closed");
  h.click[0]=false;h.lower[0]=false;m.map(h,true,11223);h.lower[0]=true;
  check(m.map(h,true,11224).wButtons==XINPUT_GAMEPAD_Y,"Fresh X after wheel did not trigger Flame Belch");
  h={};m.map(h,false,11300);h.lower[0]=true;check(m.map(h,false,11310).wButtons==XINPUT_GAMEPAD_X,"Menu X changed");
  h.lower[0]=false;h.click[0]=true;check(!m.map(h,false,11311).wButtons,"Menu L3 triggered dossier or Flame Belch");
  h.click[0]=false;h.menu[0]=true;check(m.map(h,false,11312).wButtons==XINPUT_GAMEPAD_START,"Left Menu could not close menu");
  h.menu[0]=false;h.click[0]=true;
  check(!m.map(h,true,12000).wButtons,"Held menu L3 opened dossier in gameplay");h.click[0]=false;m.map(h,true,12010);
  h.click[0]=true;m.map(h,true,12100);h.stick[1].y=-1;m.map(h,true,12200);
  h.stick[1].y=0;check(!m.map(h,true,12900).wButtons,"Wheel cancellation leaked pending dossier");
  h.click[0]=false;check(!m.map(h,true,13000).wButtons,"Wheel cancellation leaked objective on release");
 }
 {
  GameplayMapping m;HandsInput h;m.map(h,true,20000);h.click[1]=true;
  check(m.map(h,true,20010).wButtons==XINPUT_GAMEPAD_LEFT_THUMB,"Use phase missing");
  h.click[1]=false;
  check(m.map(h,true,20109).wButtons==XINPUT_GAMEPAD_LEFT_THUMB,"Use pulse too short");
  check(m.map(h,true,20110).wButtons==XINPUT_GAMEPAD_RIGHT_THUMB,"Melee must follow Use at 100ms");
  check(m.map(h,true,20209).wButtons==XINPUT_GAMEPAD_RIGHT_THUMB,"Short click lost melee pulse");
  check(!m.map(h,true,20210).wButtons,"Pulse exceeded 200ms");
  h.click[1]=true;m.map(h,true,20300);
  check(m.map(h,true,20600).wButtons==XINPUT_GAMEPAD_RIGHT_THUMB,"Held click lost melee");
  h.click[1]=false;check(!m.map(h,true,20601).wButtons,"Held melee stuck on release");
  h.click[1]=true;m.map(h,true,20700);h.stick[1].y=-1;m.map(h,true,20720);
  h.stick[1].y=0;check(!m.map(h,true,20820).wButtons,"Wheel did not cancel pending melee");
  UseMeleeButton b;b.update(true,true,900);
  check(b.update(true,true,899)==UseMeleeButton::Action::None,"Clock rollback generated input");
 }
 {
  GameplayMapping m;HandsInput h;m.map(h,false,30000,false,false,true);
  h.click[0]=true;check(!m.map(h,false,30010,false,false,true).wButtons,"Tutorial dossier fired before hold threshold");
  check(m.map(h,false,30610,false,false,true).wButtons==XINPUT_GAMEPAD_BACK,"Quad tutorial blocked requested dossier");
  check(!m.map(h,false,30620,false,true,true).wButtons,"Opening dossier retained tutorial inventory pulse");
  h.click[0]=false;m.map(h,false,30700,false,false,true);h.click[0]=true;m.map(h,false,30710,false,false,true);
  h.click[0]=false;check(m.map(h,false,30800,false,false,true).wButtons==XINPUT_GAMEPAD_DPAD_DOWN,"Quad tutorial blocked short objective action");
  m.map(h,false,31000);h.click[0]=true;m.map(h,false,31010);
  check(!m.map(h,false,31610).wButtons,"Ordinary menu accepted gameplay dossier gesture");
 }
 {
  GameplayMapping m;HandsInput h;m.map(h,true,32000);
  h.stick[1].y=1;
  check(!(m.map(h,false,32001,false,false,true).wButtons&XINPUT_GAMEPAD_DPAD_RIGHT),"Held stick crossed tutorial entry");
  h.stick[1]={};m.map(h,false,32002,false,false,true);
  h.stick[1]={.2f,1};auto p=m.map(h,false,32003,false,false,true);
  check(p.wButtons==XINPUT_GAMEPAD_DPAD_RIGHT&&!p.sThumbRX&&!p.sThumbRY,"Quad tutorial blocked Crucible or leaked stick navigation");
  check(!(m.map(h,false,32004,false,true,true).wButtons&XINPUT_GAMEPAD_DPAD_RIGHT),"Dossier accepted tutorial Crucible shortcut");
  check(!(m.map(h,false,32005).wButtons&XINPUT_GAMEPAD_DPAD_RIGHT),"Ordinary menu accepted Crucible shortcut");
  h.stick[1]={.8f,.8f};check(!(m.map(h,false,32006,false,false,true).wButtons&XINPUT_GAMEPAD_DPAD_RIGHT),"Tutorial diagonal selected Crucible");
  h.stick[1]={};check(!m.map(h,false,32007,false,false,true).wButtons,"Tutorial Crucible stuck after release");
 }
 {
  // Tutorial prompts retain confirm/back and the VR weapon-mod shortcut,
  // even with a playable world camera behind the overlay.
  GameplayMapping m;HandsInput h;ULONGLONG tick=40000;
  auto readTutorial=[&](){return m.map(h,true,++tick,false,false,true);};
  readTutorial();WORD reached{};
  auto press=[&](bool& key,WORD expected){key=true;auto p=readTutorial();check(p.wButtons==expected,"Tutorial native button remapped or combined");reached|=p.wButtons;key=false;readTutorial();};
  press(h.lower[1],XINPUT_GAMEPAD_A);press(h.upper[1],XINPUT_GAMEPAD_B);
  press(h.lower[0],XINPUT_GAMEPAD_X);press(h.upper[0],XINPUT_GAMEPAD_DPAD_UP);
  press(h.grip[0],XINPUT_GAMEPAD_LEFT_SHOULDER);press(h.grip[1],XINPUT_GAMEPAD_RIGHT_SHOULDER);
  for(auto pair:std::array<std::pair<XrVector2f,WORD>,4>{{{{0,1},XINPUT_GAMEPAD_DPAD_RIGHT},{{0,-1},XINPUT_GAMEPAD_RIGHT_SHOULDER},{{-1,0},XINPUT_GAMEPAD_DPAD_LEFT},{{1,0},XINPUT_GAMEPAD_DPAD_UP}}}){
   h.stick[1]=pair.first;auto p=readTutorial();check(p.wButtons==pair.second&&!p.sThumbRX&&!p.sThumbRY,"Tutorial directional shortcut missing");reached|=p.wButtons;h.stick[1]={};readTutorial();
  }
  h.trigger={1,1};auto p=readTutorial();check(p.bLeftTrigger==255&&p.bRightTrigger==255,"Tutorial triggers blocked");h.trigger={};readTutorial();
  h.click[0]=true;readTutorial();h.click[0]=false;p=readTutorial();check(p.wButtons==XINPUT_GAMEPAD_DPAD_DOWN,"Tutorial objective unavailable");reached|=p.wButtons;
  tick+=300;readTutorial();h.click[0]=true;readTutorial();tick+=600;p=readTutorial();check(p.wButtons==XINPUT_GAMEPAD_BACK,"World camera blocked tutorial dossier");reached|=p.wButtons;h.click[0]=false;readTutorial();
  tick+=300;readTutorial();h.click[1]=true;p=readTutorial();check(p.wButtons==XINPUT_GAMEPAD_LEFT_THUMB,"Tutorial Use missing");reached|=p.wButtons;tick+=101;p=readTutorial();check(p.wButtons==XINPUT_GAMEPAD_RIGHT_THUMB,"Tutorial R3 missing");reached|=p.wButtons;h.click[1]=false;tick+=300;readTutorial();
  press(h.menu[0],XINPUT_GAMEPAD_START);
  check(reached==0x73ff,"Tutorial shortcut coverage changed");
  // B remains B through changes of the background camera; a new press is
  // required only at real tutorial entry/exit, not at a camera fluctuation.
  h.upper[1]=true;check(readTutorial().wButtons==XINPUT_GAMEPAD_B,"Tutorial B missing");
  check(m.map(h,false,++tick,false,false,true).wButtons==XINPUT_GAMEPAD_B,"Camera transition swallowed held tutorial B");
  check(!(m.map(h,true,++tick).wButtons&XINPUT_GAMEPAD_A),"Held tutorial B became gameplay Jump");
 }
 GameplayMapping routing;HandsInput hands;routing.map(hands,true,900);hands.upper[1]=true;hands.trigger[1]=1;hands.grip[1]=true;
 auto pad=routing.map(hands,true,1000);check((pad.wButtons&XINPUT_GAMEPAD_A)&&!(pad.wButtons&XINPUT_GAMEPAD_B)&&pad.bRightTrigger==255&&pad.bLeftTrigger==255,"KHARVOX Jump/fire/mod mapping incorrect");
 hands.upper[1]=false;hands.lower[1]=true;pad=routing.map(hands,true,1010);
 check((pad.wButtons&XINPUT_GAMEPAD_B)&&!(pad.wButtons&XINPUT_GAMEPAD_LEFT_THUMB),"Physical A did not route Dash");
 pad=routing.map(hands,false,1020);check(!(pad.wButtons&XINPUT_GAMEPAD_A),"Held Dash became menu confirm");
 hands.lower[1]=false;routing.map(hands,false,1030);hands.lower[1]=true;pad=routing.map(hands,false,1040);check(pad.wButtons&XINPUT_GAMEPAD_A,"Fresh menu confirm missing");
 hands={};routing.map(hands,true,1100);hands.lower[0]=true;pad=routing.map(hands,true,1200);check((pad.wButtons&XINPUT_GAMEPAD_Y)&&!(pad.wButtons&(XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_DPAD_LEFT)),"X must emit native Flame Belch Y only");
 hands.lower[0]=false;hands.stick[1].y=1;pad=routing.map(hands,true,1201);check(pad.wButtons==XINPUT_GAMEPAD_DPAD_RIGHT&&!pad.sThumbRX,"Right stick up did not select Crucible exclusively");
 hands.stick[1]={.8f,.8f};pad=routing.map(hands,true,1201);check(!(pad.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT)&&pad.sThumbRX==axis(.8f),"Diagonal turn accidentally selected Crucible");
 hands.stick[1].y=0;hands.trigger[0]=1;pad=routing.map(hands,true,1202);check((pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)&&!(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)&&!pad.bLeftTrigger&&!(pad.wButtons&XINPUT_GAMEPAD_Y),"Left trigger did not route equipment fire via native JOY5");
 hands.trigger[0]=0;hands.grip[0]=true;pad=routing.map(hands,true,1203);check(pad.wButtons==XINPUT_GAMEPAD_DPAD_LEFT,"Left grip must send only D-pad Left for equipment cycling");hands.grip[0]=false;
 hands.grip[0]=true;pad=routing.map(hands,true,1300);check(!pad.wButtons,"Held left grip repeated equipment or opened weapon wheel");
 hands.grip[0]=false;routing.map(hands,true,1301);
 hands.stick[1].y=-1;pad=routing.map(hands,true,1400);check(!(pad.wButtons&(XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_B)),"Stick-down emitted removed crouch/dash");
 check(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER,"Right stick down did not open wheel");
 hands.stick[0]={.6f,.8f};hands.stick[1]={.3f,-.6f};hands.trigger={1,1};hands.grip={true,true};hands.click[1]=true;
 pad=routing.map(hands,true,1410);
 check(pad.wButtons==XINPUT_GAMEPAD_RIGHT_SHOULDER&&!pad.sThumbLX&&!pad.sThumbLY&&pad.sThumbRX==axis(.6f)&&pad.sThumbRY==axis(.8f)&&!pad.bLeftTrigger&&!pad.bRightTrigger,"Wheel did not preserve readable-canvas selection and suppress combat/movement");
 s.pad=pad;s.active=true;s.tick=GetTickCount64();publish(s);followStick({1,1});followTurn(1);
 check(getState(0,&output)==ERROR_SUCCESS&&!output.Gamepad.sThumbLX&&!output.Gamepad.sThumbLY&&output.Gamepad.sThumbRX==pad.sThumbRX&&output.Gamepad.sThumbRY==pad.sThumbRY,"Body following polluted wheel selection");
 followStick({});followTurn(0);hands={};pad=routing.map(hands,true,1420);check(!(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER),"Wheel did not confirm on release");
 {
  GameplayMapping wheel;HandsInput directions;wheel.map(directions,true,1421);
  directions.stick[1].y=-1;
  for(const auto v:std::array<XrVector2f,4>{{{1,0},{-1,0},{0,1},{0,-1}}}){
   directions.stick[0]=v;const auto selection=wheel.map(directions,true,1422);
   check(selection.sThumbRX==axis(v.x)&&selection.sThumbRY==axis(v.y),"Wheel cardinal direction mapping incorrect");
  }
  directions.stick[1]={.5f,0};directions.stick[0]={.7f,.4f};
  const auto normal=wheel.map(directions,true,1423);
  check(std::abs(std::hypot(float(normal.sThumbLX),float(normal.sThumbLY))/32767.f-.9374654f)<.0001f&&
        std::abs(normal.sThumbLX*.4f-normal.sThumbLY*.7f)<1.f&&normal.sThumbRX==axis(.5f),"Wheel exit lost compensated movement direction or changed turn");
 }
 hands.grip[0]=true;pad=routing.map(hands,true,1430);check(pad.wButtons==XINPUT_GAMEPAD_DPAD_LEFT,"Fresh offhand grip must cycle equipment without wheel input");
 pad=routing.map(hands,true,1530);check(!pad.wButtons,"Held offhand grip retained equipment/wheel input");
 hands={};hands.stick[1].y=-1;routing.map(hands,false,1440);pad=routing.map(hands,true,1450);check(!(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER),"Held menu stick opened wheel after context change");
 hands={};routing.map(hands,false,1451);hands.stick[1].y=1;pad=routing.map(hands,false,1452);check(!(pad.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT),"Menu right stick up activated Crucible");
 hands={};routing.map(hands,true,1460);hands.stick[1].y=-1;pad=routing.map(hands,true,1470,true);check(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER,"Legacy config lost new wheel binding");
 hands={};hands.trigger[1]=1;hands.grip[1]=true;pad=routing.map(hands,false,1500);check(!pad.bRightTrigger&&!(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER),"Held attack/mod crossed menu transition");
 hands={};routing.map(hands,false,1510);hands.lower[1]=true;pad=routing.map(hands,true,1520);check(!(pad.wButtons&XINPUT_GAMEPAD_B),"Held confirm became dash");
 hands={};routing.map(hands,true,1530);hands.lower[1]=true;pad=routing.map(hands,true,1540,true);check((pad.wButtons&XINPUT_GAMEPAD_B)&&!(pad.wButtons&XINPUT_GAMEPAD_LEFT_THUMB),"Old config must still map right A to Dash");
 hands={};routing.map(hands,false,1600);hands.stick[1]={.2f,-1};pad=routing.map(hands,false,1610);
 check(pad.wButtons==XINPUT_GAMEPAD_RIGHT_THUMB&&!pad.sThumbRX&&!pad.sThumbRY,"Menu stick-down did not emit R3 exclusively");
 hands.stick[1].y=1;pad=routing.map(hands,false,1611);
 check(pad.sThumbRY==0,"Menu/fallback context leaked physical right-stick pitch");
 hands.stick[1].y=-1;pad=routing.map(hands,false,1612);
 hands.stick[1].y=-.6f;pad=routing.map(hands,false,1620);check(pad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB,"Menu apply threshold chattered");
 hands={};pad=routing.map(hands,false,1630);check(!(pad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB),"Menu apply stuck after release");
 hands.click[1]=true;pad=routing.map(hands,false,1640);check(pad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB,"Menu physical R3 missing");
 hands={};routing.map(hands,true,1650);hands.stick[1].y=-1;routing.map(hands,true,1660);pad=routing.map(hands,false,1670);
 check(!(pad.wButtons&(XINPUT_GAMEPAD_RIGHT_THUMB|XINPUT_GAMEPAD_RIGHT_SHOULDER)),"Held wheel confirmed graphics after menu transition");
 hands={};routing.map(hands,false,1680);hands.stick[1].y=-1;pad=routing.map(hands,false,1690);check(pad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB,"Fresh apply after transition missing");
 GameplayMapping demonMap;HandsInput demonInput{};
 auto demonRoute=[&](bool demon,ULONGLONG tick){return demonMap.map(demonInput,true,tick,false,false,false,demon);};
 demonRoute(false,2000);demonInput.stick[1].y=-1;demonInput.grip[0]=true;demonInput.trigger[1]=1;
 demonRoute(false,2010);auto dp=demonRoute(true,2020);
 check(!dp.wButtons&&!dp.bRightTrigger&&!demonMap.wheelHeld,"Held Slayer input crossed possession");
 demonInput={};demonRoute(true,2030);
 demonInput.trigger={1,1};demonInput.upper[1]=true;demonInput.lower[1]=true;
 dp=demonRoute(true,2040);check(dp.bLeftTrigger==255&&dp.bRightTrigger==255&&dp.wButtons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B),"Demon fire, mode, flight or dash missing");
 demonInput={};demonInput.grip={true,true};demonInput.click={true,true};demonInput.lower[0]=true;demonInput.upper[0]=true;demonInput.stick[1].y=-1;
 dp=demonRoute(true,2050);check(!dp.wButtons&&!dp.bLeftTrigger&&!demonMap.wheelHeld,"Slayer actions leaked into demon mode");
 dp=demonRoute(false,2060);check(!dp.wButtons&&!dp.bLeftTrigger,"Held demon inputs crossed return to Slayer");
 demonInput={};demonRoute(false,2070);demonInput.grip[0]=true;dp=demonRoute(false,2080);
 check(dp.wButtons==XINPUT_GAMEPAD_DPAD_LEFT,"Slayer equipment cycle did not resume");
 argent::camera::RoomscaleFollow follow;argent::camera::Basis body{1,0,0,0,1,0,0,0,1};XrVector3f foot{},head{0,0,-.3f},zero{};XrQuaternionf q{0,0,0,1};
 auto stick=follow.update(1,foot,head,zero,body,q,1,false,true);check(stick.y>0&&std::abs(stick.x)<1e-5,"Roomscale forward route wrong");
 follow.update(1,foot,head,zero,body,q,1,false,true);check(follow.accepted.z==0,"Wall collision was treated as accepted motion");
 foot.x=.05f;follow.update(1,foot,head,zero,body,q,1,false,true);check(std::abs(follow.accepted.z+.05f)<1e-5,"Accepted physics did not reduce tracking offset");
 foot.x=.1f;stick=follow.update(1,foot,head,zero,body,q,1,true,true);check(stick.x==0&&stick.y==0&&std::abs(follow.accepted.z+.05f)<1e-5,"Manual stick movement consumed roomscale offset");
 foot.x=100;follow.update(1,foot,head,zero,body,q,1,false,true);check(std::abs(follow.accepted.z+.05f)<1e-5,"Teleport counted as roomscale walking");
 follow.update(2,foot,head,zero,body,q,1,false,true);check(follow.accepted.z==0,"Player change retained roomscale reference");
 std::cout<<"VR XInput snapshot, routing, focus release, timeout and merge verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
