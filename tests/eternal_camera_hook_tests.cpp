#include <vector>
#include "../src/EternalCameraHook.h"
#include "../src/EternalCameraMath.h"
#include "../src/EternalPresentation.h"
#include <windows.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace argent {void log(const std::string& s){std::cout<<s<<'\n';}}
namespace argent::hud {
bool weaponWheelVisible() noexcept{return false;}
int calibrationPlaceholderRole(){return -1;}
kharvox::hands::HandHudPanels calibrationPlaceholder(int,bool,const float*,const float*,float,const float*){return {};}
}
namespace argent::camera {void* installFixture();void finalizeFixture(void*,void(__fastcall*)(void*));void updateHeadsetFov(const XrPosef&,const XrView*,uint32_t) noexcept;}
using namespace argent::camera;
void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(){try{
 auto setter=reinterpret_cast<void(__fastcall*)(void*,const float*,const float*)>(installFixture());check(setter!=nullptr,"Native hook installation failed");
 unsigned char object[512];std::memset(object,0xcd,sizeof(object));float position[3]={12,23,34};Basis identity{1,0,0,0,1,0,0,0,1};
 auto call=[&]{setter(object,position,identity.data());Basis result;std::memcpy(result.data(),object+0x130,sizeof(result));check(!std::memcmp(position,object+0x124,sizeof(position)),"Hook changed position");check(object[0x123]==0xcd&&object[0x154]==0xcd,"Setter damaged neighboring object fields");return result;};
 check(call()==identity,"Dormant hook changed basis");update({0,0,0,1},true,true);check(call()==identity,"First world call not recentered");
 float s=std::sqrt(.5f);update({0,s,0,s},true,false);auto result=call();check(std::abs(result[1]-1)<1e-5,"Actual detour did not rotate camera");
 update({0,s,0,s},true,true);check(std::abs(call()[0]-1)<1e-5,"Native recenter failed");
 update({s,0,0,s},false,false);check(call()==identity,"Quad mode altered camera");
 update({0,0,0,1},true,true);call();update({0,s,0,s},true,false);Sleep(270);check(call()==identity,"Stale tracking altered camera");
 stop();check(call()==identity,"Stopped hook altered camera");check(stats().calls==8&&stats().applied==4,"Unexpected detour call accounting");
 XrPosef head{{0,0,0,1},{0,1.7f,0}};updatePose(head,true,true,true);setter(object,position,identity.data());
 head.position={.2f,1.4f,-.1f};updatePose(head,true,false,true);setter(object,position,identity.data());
 float moved[3];std::memcpy(moved,object+0x124,sizeof(moved));
 check(std::abs(moved[0]-12.1f)<1e-5f&&std::abs(moved[1]-22.8f)<1e-5f&&std::abs(moved[2]-33.7f)<1e-5f,"Native 6DoF translation incorrect");
 check(position[0]==12&&position[1]==23&&position[2]==34&&object[0x123]==0xcd&&object[0x154]==0xcd,"Translation changed caller data or object bounds");
 updatePose(head,true,true,true);setter(object,position,identity.data());check(!std::memcmp(position,object+0x124,sizeof(position)),"Position recenter failed");
 head.position.x+=.1f;updatePose(head,true,false,false);setter(object,position,identity.data());check(!std::memcmp(position,object+0x124,sizeof(position)),"Untracked position applied");
 updatePose(head,true,false,true);setter(object,position,identity.data());check(!std::memcmp(position,object+0x124,sizeof(position)),"Tracking recovery failed to rebase");
 head.position.x+=.2f;updatePose(head,true,false,true);Sleep(270);setter(object,position,identity.data());check(!std::memcmp(position,object+0x124,sizeof(position)),"Stale 6DoF position applied");
 updatePose(head,false,false,true);check(call()==identity,"6DoF affected quad");
 updatePose(head,true,true,true);setter(object,position,identity.data());
 head.orientation={0,s,0,s};updatePose(head,true,false,true);setter(object,position,identity.data());
 Basis renderedBasis;std::memcpy(renderedBasis.data(),object+0x130,sizeof(renderedBasis));
 float common[27]{};for(int k=0;k<3;++k){common[21+k]=-4*renderedBasis[3+k];common[24+k]=-2*renderedBasis[6+k];}
 // Publish a newer prediction without applying it. Metadata must stay with
 // the basis uploaded for the old rendered camera, not this new prediction.
 auto rendered=head;head.orientation={0,0,0,1};updatePose(head,true,false,true);
 beginRender(42);observeRenderedCamera(common,sizeof(common));XrPosef matched{};
 check(renderedHead(42,matched)&&std::abs(matched.orientation.y-rendered.orientation.y)<1e-5f,"Rendered camera tagged with newer prediction");
 check(!renderedHead(43,matched),"Rendered camera crossed frame identity");
 beginRender(43);common[21]=common[22]=common[23]=0;observeRenderedCamera(common,sizeof(common));
 check(!renderedHead(43,matched),"Invalid UBO created pose match");
 stop();check(!renderedHead(42,matched),"Stopped camera retained rendered pose");
 // Native camera recoil/contact offsets must not detach the controller's
 // anchor. Both physical yaw and a blocked roomscale step share that anchor.
 argent::presentation::gameplayInput=true;
 publishPhysics(42,{0,0,0});position[0]=position[1]=0;position[2]=1.7f;
 head={{0,0,0,1},{0,1.7f,0}};updatePose(head,true,true,true);setter(object,position,identity.data());
 XrPosef hand{{0,0,0,1},{.2f,1.3f,-.3f}};float weapon[3]{},weaponAxis[9]{};
 check(controllerPlacement(hand,weapon,weaponAxis),"fixture weapon placement unavailable");
 std::array<float,3> anchored{weapon[0],weapon[1],weapon[2]};
 position[0]=.12f;position[1]=-.08f;position[2]=1.5f;setter(object,position,identity.data());
 check(controllerPlacement(hand,weapon,weaponAxis)&&std::abs(weapon[0]-anchored[0])<1e-5f&&std::abs(weapon[1]-anchored[1])<1e-5f&&std::abs(weapon[2]-anchored[2])<1e-5f,"Native contact camera offset detached weapon");
 head.orientation={0,s,0,s};hand.orientation=head.orientation;updatePose(head,true,false,true);setter(object,position,identity.data());
 check(controllerPlacement(hand,weapon,weaponAxis)&&std::abs(weaponAxis[1]-1)<1e-5f,"Physical hand yaw lost after contact");
 argent::input::Snapshot used;used.active=true;used.tick=GetTickCount64();used.dominant=1;
 used.grip[0]=used.grip[1]=hand;used.gripValid[0]=used.gripValid[1]=true;
 check(controllerPlacement(hand,weapon,weaponAxis,&used),"Rendered hand placement unavailable");
 setter(object,position,identity.data());std::memcpy(renderedBasis.data(),object+0x130,sizeof(renderedBasis));
 for(int k=0;k<3;++k){common[21+k]=-4*renderedBasis[3+k];common[24+k]=-2*renderedBasis[6+k];}common[2]=-1;common[3]=-.05f;
 float hudEye[3]{},hudAxes[9]{};uint64_t hudFrame{};
 check(hudCamera(hudEye,hudAxes,&hudFrame)&&hudFrame,"HUD frame not identified");
 kharvox::hands::HandHudPanels panels{{{{0,0,1},{0,1,1},{0,0,2},{0,1,2}}}};
 publishHudPanels(hudFrame,1,panels);
 const std::array<float,3> laserWorld{weapon[0],weapon[1],weapon[2]};Basis laserAxis{};std::memcpy(laserAxis.data(),weaponAxis,sizeof(laserAxis));
 publishLaser(laserWorld.data(),laserAxis.data(),"super_shotgun");
 auto newer=used;newer.tick++;newer.grip[1].position.x+=.2f;
 check(controllerPlacement(newer.grip[1],weapon,weaponAxis,&newer),"Newer hand fixture failed");
 argent::input::weaponApplied(newer);
 beginRender(98);observeRenderedCamera(common,sizeof(common));
 argent::input::Snapshot captured;float da{},db{};
 check(renderedHands(98,captured,da,db)&&captured.tick==used.tick&&std::abs(captured.grip[1].position.x-hand.position.x)<1e-5f&&da==-1&&db==-.05f,"Rendered hand replaced by newer placement");
 check(renderedHudPanels(98).size()==1&&renderedHudPanels(97).empty(),"HUD panels crossed frame identity");
 XrPosef laserPose{},expectedLaser{};std::array<float,3> laserEye{hudEye[0],hudEye[1],hudEye[2]};Basis laserCamera{};std::memcpy(laserCamera.data(),hudAxes,sizeof(laserCamera));
 XrPosef laserHead{};check(renderedHead(98,laserHead)&&worldPoseInTracking(laserWorld,laserAxis,laserEye,laserCamera,laserHead,unitsPerMeter(),expectedLaser),"Laser fixture conversion failed");
 check(renderedLaser(98,"super_shotgun",laserPose)&&std::abs(laserPose.position.y-expectedLaser.position.y)<1e-5f,"Laser used newer action pose");
 check(!renderedLaser(99,"super_shotgun",laserPose)&&!renderedLaser(98,"rocket_launcher",laserPose),"Laser crossed frame or weapon identity");
 publishLaser(nullptr,nullptr,nullptr);check(renderedLaser(98,"super_shotgun",laserPose),"Late update changed already rendered muzzle");
 beginRender(99);observeRenderedCamera(common,sizeof(common));check(!renderedLaser(99,"super_shotgun",laserPose),"Hidden weapon retained laser in next frame");
 publishLaser(laserWorld.data(),laserAxis.data(),"super_shotgun");
 beginRender(98);observeRenderedCamera(common,sizeof(common));
 auto triangles=panels;triangles.resize(256,panels.front());
 publishHudPanels(hudFrame,1,triangles);
 check(renderedHudPanels(98).size()==256,"Triangle masks truncated at old rectangle limit");
 publishHudPanels(hudFrame,1,panels);
 argent::presentation::hideGameplayHud=true;check(renderedHudPanels(98).empty(),"Hidden HUD retained hand masks");argent::presentation::hideGameplayHud=false;
 setter(object,position,identity.data());beginRender(97);observeRenderedCamera(common,sizeof(common));
 check(renderedHudPanels(97).empty(),"Unsubmitted HUD retained stale masks");
 check(renderedLaser(97,"super_shotgun",laserPose),"Weapon-before-camera ordering lost muzzle");
 publishLaser(nullptr,nullptr,nullptr);beginRender(97);observeRenderedCamera(common,sizeof(common));check(!renderedLaser(97,"super_shotgun",laserPose),"Unsubmitted laser retained stale source");
 // Compare against the visible native muzzle in the rendered camera space,
 // not a stationary tracking pose: locomotion can advance between callbacks.
 for(int step=0;step<12;++step){
  if(step%2)setter(object,position,identity.data());
  publishPhysics(42,{step*.04f,step*-.025f,0});
  auto movingHand=hand;
  const float angle=step*.06f;movingHand.orientation=product(hand.orientation,XrQuaternionf{0,std::sin(angle),0,std::cos(angle)});
  check(controllerPlacement(movingHand,weapon,weaponAxis,&used),"Moving laser placement failed");
  float muzzle[3];for(int k=0;k<3;++k)muzzle[k]=weapon[k]+weaponAxis[k]*.25f;
  publishLaser(muzzle,weaponAxis,"super_shotgun");
  if(!(step%2))setter(object,position,identity.data());
  beginRender(200+step);observeRenderedCamera(common,sizeof(common));
  XrPosef beam{};check(renderedLaser(200+step,"super_shotgun",beam),"Moving laser missing");
  float eye[3],axes[9];XrPosef rendered{},expected{};
  check(hudCamera(eye,axes)&&renderedHead(200+step,rendered),"Moving camera fixture missing");
  Basis cameraBasis{},muzzleBasis{};std::memcpy(cameraBasis.data(),axes,sizeof(axes));std::memcpy(muzzleBasis.data(),weaponAxis,sizeof(weaponAxis));
  check(worldPoseInTracking({muzzle[0],muzzle[1],muzzle[2]},muzzleBasis,{eye[0],eye[1],eye[2]},cameraBasis,rendered,unitsPerMeter(),expected),"Muzzle conversion failed");
  check(std::abs(beam.position.x-expected.position.x)<1e-5f&&std::abs(beam.position.y-expected.position.y)<1e-5f&&std::abs(beam.position.z-expected.position.z)<1e-5f,"Laser detached from rendered muzzle during walking");
  const auto q=beam.orientation,e=expected.orientation;
  check(std::abs(std::abs(q.x*e.x+q.y*e.y+q.z*e.z+q.w*e.w)-1)<1e-5f,"Laser direction trails rendered muzzle");
 }
 // The camera can be queued before the item animation advances the muzzle.
 // Replace the pre-animation capture before render matching, then freeze it.
 setter(object,position,identity.data());
 float animatedMuzzle[3];for(int k=0;k<3;++k)animatedMuzzle[k]=weapon[k];
 publishLaser(animatedMuzzle,weaponAxis,"super_shotgun");
 animatedMuzzle[0]+=.12f;animatedMuzzle[1]-=.09f;
 publishLaser(animatedMuzzle,weaponAxis,"super_shotgun");
 beginRender(250);observeRenderedCamera(common,sizeof(common));
 XrPosef animatedBeam{},animatedExpected{},animationHead{};float animationEye[3],animationAxes[9];
 check(renderedLaser(250,"super_shotgun",animatedBeam)&&renderedHead(250,animationHead)&&hudCamera(animationEye,animationAxes),"Post-animation muzzle unavailable");
 Basis animationBasis{},animationMuzzleBasis{};std::memcpy(animationBasis.data(),animationAxes,sizeof(animationAxes));std::memcpy(animationMuzzleBasis.data(),weaponAxis,sizeof(weaponAxis));
 check(worldPoseInTracking({animatedMuzzle[0],animatedMuzzle[1],animatedMuzzle[2]},animationMuzzleBasis,{animationEye[0],animationEye[1],animationEye[2]},animationBasis,animationHead,unitsPerMeter(),animatedExpected),"Post-animation conversion failed");
 check(std::abs(animatedBeam.position.x-animatedExpected.position.x)<1e-5f&&std::abs(animatedBeam.position.z-animatedExpected.position.z)<1e-5f,"Render used pre-animation muzzle");
 // The GPU may submit an older camera while simulation advances at exactly
 // the same heading. A direction-only match selects the wrong muzzle.
 float translatedCommon[30]{};std::memcpy(translatedCommon,common,sizeof(common));
 std::memcpy(translatedCommon+27,animationEye,sizeof(animationEye));
 publishPhysics(42,{2.f,-3.f,0});setter(object,position,identity.data());
 float newerMuzzle[3]{animatedMuzzle[0]+4.f,animatedMuzzle[1]-2.f,animatedMuzzle[2]+.3f};
 publishLaser(newerMuzzle,weaponAxis,"super_shotgun");
 beginRender(251);observeRenderedCamera(translatedCommon,sizeof(translatedCommon));
 XrPosef oldDrawBeam{};check(renderedLaser(251,"super_shotgun",oldDrawBeam),"Translated GPU frame lost native muzzle");
 check(std::abs(oldDrawBeam.position.x-animatedExpected.position.x)<1e-5f&&std::abs(oldDrawBeam.position.y-animatedExpected.position.y)<1e-5f&&std::abs(oldDrawBeam.position.z-animatedExpected.position.z)<1e-5f,"Equal camera angles selected muzzle from a different movement frame");
 translatedCommon[27]+=1000.f;
 beginRender(252);observeRenderedCamera(translatedCommon,sizeof(translatedCommon));
 check(!renderedLaser(252,"super_shotgun",oldDrawBeam),"Unmatched draw position reused stale muzzle");
 publishPhysics(42,{0,0,0});setter(object,position,identity.data());
 // Almost identical camera angles must not resurrect an older vertical hand
 // pose merely because its floating-point dot product is a closer match.
 auto vertical=newer;vertical.tick+=10;vertical.grip[0].position.y+=.15f;vertical.grip[1].position.y+=.15f;
 check(controllerPlacement(vertical.grip[1],weapon,weaponAxis,&vertical),"Vertical hand placement failed");
 head.orientation=product(head.orientation,XrQuaternionf{.0002f,0,0,std::sqrt(1.f-.0002f*.0002f)});
 updatePose(head,true,false,true);setter(object,position,identity.data());
 beginRender(101);observeRenderedCamera(common,sizeof(common));
 check(renderedHands(101,captured,da,db)&&captured.tick==vertical.tick,"Near-identical camera selected an older hand/weapon pose");
 // Exercise the actual native detour with fixed physics but moving animation
 // position and a rolled animation basis. HMD translation must not interfere.
 stop();head={{0,0,0,1},{0,1.7f,0}};position[0]=position[1]=0;position[2]=1.7f;
 publishPhysics(42,{0,0,0});updatePose(head,true,true,true);setter(object,position,identity.data());
 argent::presentation::syncAttack=true;
 Basis rolled{1,0,0,0,0,1,0,-1,0};
 head.position={.3f,1.2f,-.2f};updatePose(head,true,false,true);
 position[0]=4;position[1]=3;position[2]=2;setter(object,position,rolled.data());
 check(!std::memcmp(position,object+0x124,sizeof(position)),"Glory Kill camera lost native animation position");
 std::memcpy(result.data(),object+0x130,sizeof(result));check(result==identity,"Animation roll replaced held body basis");
 check(!controllerPlacement(hand,weapon,weaponAxis),"Controller overrode authored kill hands");
 Basis swimAxis{};check(!swimmingView(swimAxis.data()),"Swimming pose leaked into authored animation");
 XrView animationEyes[2]{};for(auto& eye:animationEyes){eye.pose=head;eye.fov={-.8f,.8f,.7f,-.7f};}
 updateHeadsetFov(head,animationEyes,2);
 auto authoredFov=+[](void* camera){float f[2]={55,40};std::memcpy(static_cast<unsigned char*>(camera)+0xb8,f,sizeof(f));};
 auto unchangedFov=+[](void*){};
 finalizeFixture(object,authoredFov);float protectedFov[2]{};std::memcpy(protectedFov,object+0xb8,sizeof(protectedFov));
 check(protectedFov[0]>90&&protectedFov[1]>80,"Late native animation FOV overwrite escaped guard");
 unsigned char otherCamera[512]{};finalizeFixture(otherCamera,authoredFov);
 float otherFov[2]{};std::memcpy(otherFov,otherCamera+0xb8,sizeof(otherFov));
 check(otherFov[0]==55&&otherFov[1]==40,"Animation FOV leaked to another camera");
 head.orientation={0,s,0,s};updatePose(head,true,false,true);position[0]=5;setter(object,position,rolled.data());
 std::memcpy(result.data(),object+0x130,sizeof(result));
 check(std::abs(result[1]-1)<1e-5f&&!std::memcmp(position,object+0x124,sizeof(position)),"Glory Kill free look or animated travel failed");
 for(int k=0;k<3;++k){common[21+k]=-4*result[3+k];common[24+k]=-2*result[6+k];}
 common[2]=-1;common[3]=-.05f;
 beginRender(99);observeRenderedCamera(common,sizeof(common));
 check(!renderedHands(99,captured,da,db),"Animation retained gameplay hands");
 check(!renderedHands(100,captured,da,db),"Wrong frame reused hand pose");
 check(renderedHead(99,matched)&&std::abs(matched.position.x-head.position.x)<1e-5f,"Animation compositor pose reintroduced suppressed head translation");
 argent::presentation::syncAttack=false;publishPhysics(42,{5,3,.3f});updatePose(head,true,false,true);setter(object,position,identity.data());
 finalizeFixture(object,unchangedFov);std::memcpy(protectedFov,object+0xb8,sizeof(protectedFov));
 check(protectedFov[0]==55&&protectedFov[1]==40,"Animation FOV persisted after gameplay resumed");
 check(!std::memcmp(position,object+0x124,sizeof(position)),"Kill exit retained old physics anchor or head offset");
 check(controllerPlacement(hand,weapon,weaponAxis),"Controller did not resume after Glory Kill");
 stop();argent::presentation::gameplayInput=false;
 // Read the actual native-field contract through ReadProcessMemory. A stale
 // hands timestamp must not lose a live sync; a reused non-player must fail.
 std::array<unsigned char,0x1a10> nativePlayer{};uintptr_t table=0x12345678;
 std::memcpy(nativePlayer.data(),&table,sizeof(table));nativePlayer[0x1a01]=1;
 argent::presentation::playerVtable=table;argent::presentation::player=uintptr_t(nativePlayer.data());argent::presentation::playerTick=0;
 check(argent::presentation::refreshSyncAttack(),"Live sync depended on hands-root freshness");
 nativePlayer[0x1a01]=0;check(!argent::presentation::refreshSyncAttack(),"Native sync exit not recognized");
 nativePlayer[0x1a01]=1;nativePlayer[0]=0;
 check(!argent::presentation::refreshSyncAttack(),"Reused non-player object classified as Glory Kill");
 // Native interaction and monkey-bar states use the same authored camera contract.
 std::vector<unsigned char> animationPlayer(0x37160);
 std::memcpy(animationPlayer.data(),&table,sizeof(table));
 const int dormantLedge=16;std::memcpy(animationPlayer.data()+0x34de8,&dormantLedge,4);
 argent::presentation::player=uintptr_t(animationPlayer.data());
 for(size_t flag:{size_t(0x167e9)}){
  animationPlayer[flag]=1;
  check(argent::presentation::refreshAnimationCamera(),"Native non-kill animation not detected");
  position[0]+=2;updatePose(head,true,false,true);setter(object,position,rolled.data());
  check(!std::memcmp(position,object+0x124,sizeof(position)),"Interaction/monkey camera lost animated position");
  check(!controllerPlacement(hand,weapon,weaponAxis),"Controller weapon shown during native interaction");
  animationPlayer[flag]=0;check(!argent::presentation::refreshAnimationCamera(),"Native animation failed to release");
 }
 // Monkey bars follow physics, not the swinging first-person camera joint.
 stop();publishPhysics(42,{0,0,0});head={{0,0,0,1},{0,1.7f,0}};
 position[0]=position[1]=0;position[2]=1.7f;
 updatePose(head,true,true,true);setter(object,position,identity.data());
 animationPlayer[0x2f5e0]=1;argent::presentation::scriptedMovement=true;
 publishPhysics(42,{2,0,0});position[0]=10;
 updatePose(head,true,false,true);setter(object,position,rolled.data());
 float barPosition[3];std::memcpy(barPosition,object+0x124,sizeof(barPosition));
 check(std::abs(barPosition[0]-2)<1e-5f,"Monkey bar followed animation joint instead of physics");
 std::memcpy(result.data(),object+0x130,sizeof(result));
 check(result==identity,"Monkey bar tilted the VR horizon");
 animationPlayer[0x2f5e0]=0;argent::presentation::scriptedMovement=false;
 argent::presentation::refreshAnimationCamera();
 // Recorded ModBot sequence: mod-change remains active for 4.4 s after
 // menu close, while all generic interaction flags stay zero.
 const size_t modFlag=0xd2c8+0x8da5;
 animationPlayer[modFlag]=0xa0;
 check(!argent::presentation::refreshAnimationCamera(),"Ordinary mod switch seized VR camera");
 argent::presentation::upgradeAnimationOwner=uintptr_t(animationPlayer.data());
 check(argent::presentation::refreshAnimationCamera()&&argent::presentation::droneAnimation.load(),"Drone outro not recognized");
 position[0]+=2;updatePose(head,true,false,true);setter(object,position,identity.data());
 check(!std::memcmp(position,object+0x124,sizeof(position)),"Drone animation lost native travel");
 check(!controllerPlacement(hand,weapon,weaponAxis),"Drone outro returned controller weapon early");
 for(int frame=0;frame<400;++frame)check(argent::presentation::refreshAnimationCamera(),"Drone outro used a fixed timeout");
 animationPlayer[modFlag]=0x20;
 check(!argent::presentation::refreshAnimationCamera()&&!argent::presentation::upgradeAnimationOwner.load(),"Drone end did not release ownership");
 check(!controllerPlacement(hand,weapon,weaponAxis),"Animation exit reused camera anchor before rebase");
 updatePose(head,true,false,true);setter(object,position,identity.data());
 check(controllerPlacement(hand,weapon,weaponAxis),"VR placement did not resume after post-animation camera rebase");
 argent::presentation::skippedMonkeyBarOwner=uintptr_t(animationPlayer.data());
 check(argent::presentation::refreshAnimationCamera()&&argent::presentation::monkeyBarAnimation.load(),"First-person bar lifecycle ignored without third-person flag");
 argent::presentation::skippedMonkeyBarOwner=0;
 check(!argent::presentation::refreshAnimationCamera(),"Bar lifecycle retained after native exit");
 animationPlayer[modFlag]=0xa0;
 check(!argent::presentation::refreshAnimationCamera(),"Later mod switch reused drone session");
 animationPlayer[modFlag]=0;
 std::vector<unsigned char> shotgun(0x4000);
 const uintptr_t shotgunType=table-0x2db5698+0x2e0dbc8,shotgunPtr=uintptr_t(shotgun.data());
 std::memcpy(shotgun.data(),&shotgunType,8);shotgun[0x3d9b]=1;
 std::memcpy(animationPlayer.data()+0xd2c8+0x29b0+0x38,&shotgunPtr,8);
 check(!argent::presentation::refreshAnimationCamera()&&argent::presentation::meatHookAnimation.load(),"Meat hook incorrectly seized authored camera");
 updatePose(head,true,false,true);setter(object,position,identity.data());
 check(controllerPlacement(hand,weapon,weaponAxis),"Meat hook disabled controller weapon");
 animationPlayer[0x1a01]=1;
 check(argent::presentation::refreshAnimationCamera()&&argent::presentation::syncAttack.load(),"Meat hook suppressed real Glory Kill");
 updatePose(head,true,false,true);setter(object,position,identity.data());
 check(!controllerPlacement(hand,weapon,weaponAxis),"Glory Kill failed to take camera ownership during meathook");
 animationPlayer[0x1a01]=0;
 check(!argent::presentation::refreshAnimationCamera()&&argent::presentation::meatHookAnimation.load(),"Latched meat hook prevented Glory Kill camera exit");
 updatePose(head,true,false,true);setter(object,position,identity.data());
 check(controllerPlacement(hand,weapon,weaponAxis),"Meat hook retained authored camera after Glory Kill");
 shotgun[0x3d9b]=0;check(!argent::presentation::refreshAnimationCamera(),"Meat hook exit retained animation");
 shotgun[0x3d9b]=1;shotgun[0]=0;check(!argent::presentation::refreshAnimationCamera(),"Wrong weapon type accepted as meat hook");
 const int activeInteract=2;std::memcpy(animationPlayer.data()+0x37158,&activeInteract,4);
 check(argent::presentation::refreshAnimationCamera()&&argent::presentation::interactionAnimation.load(),"Mechanic interaction not detected");
 animationPlayer[0]=0;check(!argent::presentation::refreshAnimationCamera(),"Stale owner retained animation");
 argent::presentation::player=0;argent::presentation::playerVtable=0;
 argent::presentation::gameplayInput=true;argent::presentation::syncAttack=false;head={{0,0,0,1},{0,1.7f,0}};updatePose(head,true,true,true);setter(object,position,identity.data());
 float hudPosition[3]{},hudBasis[9]{};
 check(hudCamera(hudPosition,hudBasis)&&!std::memcmp(hudPosition,object+0x124,sizeof(hudPosition))&&!std::memcmp(hudBasis,object+0x130,sizeof(hudBasis)),"HUD camera differs from actual native setter result");
 float barOrigin[3]{},barAxis[9]{},barOffset[3]{};
 check(monkeyBarPose(barOrigin,barAxis,barOffset)&&!std::memcmp(barAxis,hudBasis,sizeof(barAxis)),"Monkey pose lost rendered head basis");
 const auto initialOffset=std::array<float,3>{barOffset[0],barOffset[1],barOffset[2]};
 float dashedPosition[]{position[0]+20,position[1]-10,position[2]};
 setter(object,dashedPosition,identity.data());
 check(monkeyBarPose(barOrigin,barAxis,barOffset),"Dash invalidated Monkey pose");
 for(int i=0;i<3;++i)check(std::abs(barOffset[i]-initialOffset[i])<.001f,"World travel leaked into physical head offset");
 setter(object,position,identity.data());
 // Offhand publication uses the same camera frame and respects handedness.
 argent::input::Snapshot controls{};controls.active=true;controls.tick=GetTickCount64();controls.dominant=1;
 controls.grip[0]={{0,0,0,1},{-.25f,1.2f,-.3f}};controls.gripValid[0]=true;
 auto publishHands=[&]{argent::input::publish(controls);check(controllerPlacement(controls.grip[0],weapon,weaponAxis,&controls),"HUD placement fixture failed");setter(object,position,identity.data());};
 publishHands();
 float grip[3]{},hudHandBasis[9]{};bool leftMode=true;
 check(hudOffhand(grip,hudHandBasis,leftMode)&&!leftMode,"Normal mode did not bind left grip");
 controls.dominant=0;controls.grip[1]={{0,0,0,1},{.25f,1.2f,-.3f}};controls.gripValid[1]=true;
 publishHands();
 check(hudOffhand(grip,hudHandBasis,leftMode)&&leftMode,"Left mode did not bind right grip");
 controls.gripValid[1]=false;publishHands();
 check(!hudOffhand(grip,hudHandBasis,leftMode),"Lost controller retained an old HUD pose");
 argent::input::clear();
 // Recoil pitch/roll must not tilt the world; a physically tilted head must.
 Basis recoil{.8f,0,.6f,0,1,0,-.6f,0,.8f};
 setter(object,position,recoil.data());std::memcpy(result.data(),object+0x130,sizeof(result));
 check(result==identity,"Native recoil pitch leaked into VR camera");
 setter(object,position,rolled.data());std::memcpy(result.data(),object+0x130,sizeof(result));
 check(result==identity,"Native roll leaked into VR camera");
 head.orientation={.258819f,0,0,.965926f};updatePose(head,true,false,true);
 setter(object,position,recoil.data());std::memcpy(result.data(),object+0x130,sizeof(result));
 Basis expected;check(rotateBasis(identity,{0,0,0,1},head.orientation,expected),"Invalid fixture HMD pitch");
 for(int i=0;i<9;++i)check(std::abs(result[i]-expected[i])<1e-5f,"Native recoil changed physical head rotation");
 updatePose(head,false,false,true);check(!hudCamera(hudPosition,hudBasis),"Quad retained immersive wheel camera");
 // Campaign possession uses a separate owner and an absolute HMD heading.
 std::vector<unsigned char> revenantPlayerFixture(0x50000),nativeDemon(0x38000);
 const uintptr_t np=uintptr_t(revenantPlayerFixture.data()),nd=uintptr_t(nativeDemon.data()),pt=42,dt=84;
 auto put=[](uintptr_t a,const auto& v){std::memcpy(reinterpret_cast<void*>(a),&v,sizeof(v));};
 put(np,pt);put(np+0x2fb70+0x5278,int(16));put(nd,dt);put(np+0x88b0,argent::revenant::Handle{7,7,nd});put(nd+0x20a08,argent::revenant::Handle{8,8,np});
 nativeDemon[0x20a48]=nativeDemon[0x20a49]=1;
 argent::presentation::player=np;argent::presentation::playerVtable=pt;
 argent::revenant::expectedType=dt;argent::revenant::installed=true;
 argent::presentation::refreshAnimationCamera();
 head={{0,0,0,1},{0,1.7f,0}};updatePose(head,true,true,true);publishPhysics(nd,{10,20,30});
 position[0]=10;position[1]=20;position[2]=32;setter(object,position,identity.data());
 head.orientation={0,.258819f,0,.965926f};updatePose(head,true,false,true);
 Basis absolute{};check(revenantAim(nd,absolute.data()),"Demon head aim missing");
 for(int frame=0;frame<6;++frame){
  // The next native camera already carries last tick's commanded HMD yaw.
  setter(object,position,absolute.data());std::memcpy(result.data(),object+0x130,sizeof(result));
  for(int k=0;k<9;++k)check(std::abs(result[k]-absolute[k])<1e-5f,"Demon native yaw fed back/doubled HMD rotation");
  updatePose(head,true,false,true);
 }
 check(argent::input::bodyTurn==0&&argent::input::movementYaw==0,"Slayer body-follow or movement rotation leaked into possession");
 check(!weaponContext(),"Slayer weapon placement active on demon rig");
 head.orientation={.258819f,0,0,.965926f};updatePose(head,true,false,true);
 check(revenantAim(nd,absolute.data())&&absolute[2]>.49f,"Demon aiming lost HMD pitch");
 publishPhysics(123,{500,500,500});setter(object,position,absolute.data());std::memcpy(moved,object+0x124,sizeof(moved));
 check(std::abs(moved[0]-10)<.001f&&std::abs(moved[2]-32)<.001f,"Slayer physics replaced demon anchor");
 // Physical steps must reach native XInput in the Revenant's HMD-relative
 // movement frame, including after managed stick turns. Native collision
 // displacement consumes the tracking offset without moving the view twice.
 controls={};controls.active=true;controls.managedTurn=true;controls.revenantActor=nd;
 auto demonPose=[&](bool recenter=false,bool tracked=true){
  controls.tick=GetTickCount64();argent::input::publish(controls);
  updatePose(head,true,recenter,tracked);
 };
 auto roomWorld=[&]{
  Basis aim{};check(revenantAim(nd,aim.data()),"Roomscale demon aim unavailable");
  XINPUT_STATE pad{};check(argent::input::getState(0,&pad)==ERROR_SUCCESS,"Demon roomscale XInput unavailable");
  const float right=pad.Gamepad.sThumbLX/32767.f,forward=pad.Gamepad.sThumbLY/32767.f;
  return XrVector2f{aim[0]*forward+aim[1]*right,aim[1]*forward-aim[0]*right};
 };
 for(float yaw:{0.f,90.f,-90.f,135.f}){
  head={{0,0,0,1},{0,1.7f,0}};demonPose(true);publishPhysics(nd,{10,20,30});setter(object,position,identity.data());
  const float half=yaw*.00872664626f;head.orientation={0,std::sin(half),0,std::cos(half)};head.position.z=-.12f;demonPose();
  auto direction=roomWorld();check(direction.x>.4f&&std::abs(direction.y)<.001f,"Demon physical forward step changed with head yaw");
  head.position={.12f,1.7f,0};demonPose();direction=roomWorld();
  check(std::abs(direction.x)<.001f&&direction.y<-.4f,"Demon physical sidestep changed with head yaw");
 }
 head={{0,0,0,1},{0,1.7f,0}};demonPose(true);publishPhysics(nd,{10,20,30});setter(object,position,identity.data());
 head.position.z=-.12f;controls.snapTurn=true;controls.snapDegrees=45;controls.pad.sThumbRX=32767;demonPose();
 auto direction=roomWorld();check(direction.x>.3f&&direction.y<-.3f&&std::abs(direction.x+direction.y)<.001f,"Demon roomscale lost managed snap-turn frame");
 controls.pad={};controls.snapTurn=false;
 head={{0,0,0,1},{0,1.7f,0}};demonPose(true);publishPhysics(nd,{10,20,30});setter(object,position,identity.data());
 head.position.z=-.12f;demonPose();setter(object,position,identity.data());
 float beforeFollow[3]{};std::memcpy(beforeFollow,object+0x124,sizeof(beforeFollow));
 const auto blockedStick=argent::input::roomStick.y;
 for(int frame=0;frame<4;++frame){publishPhysics(nd,{10,20,30});demonPose();}
 check(std::abs(argent::input::roomStick.y-blockedStick)<1e-5f,"Blocked demon step consumed tracking offset");
 publishPhysics(nd,{10.02f,20,30});demonPose();position[0]=10.02f;setter(object,position,identity.data());
 std::memcpy(moved,object+0x124,sizeof(moved));
 check(argent::input::roomStick.y<blockedStick&&argent::input::roomStick.y>0&&std::abs(moved[0]-beforeFollow[0])<.001f,"Accepted demon physics doubled view translation or failed to consume offset");
 controls.pad.sThumbLX=20000;demonPose();
 check(argent::input::roomStick.x==0&&argent::input::roomStick.y==0,"Demon roomscale fought manual stick");
 XINPUT_STATE manualPad{};argent::input::getState(0,&manualPad);
 check(manualPad.Gamepad.sThumbLX==20000,"Demon roomscale replaced manual movement");
 controls.pad={};argent::presentation::gameplayInput=false;demonPose();
 check(argent::input::roomStick.x==0&&argent::input::roomStick.y==0,"Demon followed physical steps in menu");
 argent::presentation::gameplayInput=true;controls.revenantActor=0;demonPose();
 check(argent::input::roomStick.y==0,"Demon consumed input from previous actor");
 controls.revenantActor=nd;demonPose(false,false);
 check(argent::input::roomStick.y==0,"Demon followed untracked position");
 demonPose(true);setter(object,position,identity.data());demonPose();
 check(argent::input::roomStick.x==0&&argent::input::roomStick.y==0,"Demon recenter retained old movement");
 nativeDemon[0x20a48]=0;setter(object,position,identity.data());
 check(!argent::revenant::active()&&!revenantAim(nd,absolute.data())&&weaponContext(),"Return to Slayer retained demon camera state");
 // While attached, free looking must not drive the native clamped camera or
 // rotate wall-relative movement. Roomscale is not a climb-stick command.
 std::array<unsigned char,32> climbState{};
 put(np+0x35130+0x18,np);put(np+0x35130+0x138,uintptr_t(climbState.data()));
 put(uintptr_t(climbState.data())+12,int(3));
 controls={};controls.active=true;controls.managedTurn=true;
 head={{0,0,0,1},{0,1.7f,0}};stop();
 controls.tick=GetTickCount64();argent::input::publish(controls);
 updatePose(head,true,true,true);publishPhysics(np,{10,20,30});setter(object,position,identity.data());
 head.position.x=.15f;head.orientation={0,s,0,s};
 updatePose(head,true,false,true);setter(object,position,identity.data());
 check(argent::input::bodyTurn==0&&argent::input::movementYaw==0&&argent::input::roomStick.x==0&&argent::input::roomStick.y==0,"Wall hold sent synthetic body/climb movement while looking around");
 Basis climbAim{};check(wallClimbView(climbAim.data())&&climbAim[1]>.99f,"Wall hold lost HMD yaw");
 controls.pad.sThumbLX=12000;controls.pad.sThumbLY=20000;controls.tick=GetTickCount64();argent::input::publish(controls);
 updatePose(head,true,false,true);XINPUT_STATE climbPad{};argent::input::getState(0,&climbPad);
 check(climbPad.Gamepad.sThumbLX==12000&&climbPad.Gamepad.sThumbLY==20000,"Free look rotated wall-relative climbing input");
 controls.pad={};controls.snapTurn=true;controls.pad.sThumbRX=32767;controls.tick=GetTickCount64();argent::input::publish(controls);
 updatePose(head,true,false,true);setter(object,position,identity.data());
 check(wallClimbView(climbAim.data())&&climbAim[0]>.70f&&climbAim[1]>.70f&&argent::input::bodyTurn==0,"Climbing lost managed visual turn or drove native camera clamp");
 controls.pad={};controls.snapTurn=false;controls.tick=GetTickCount64();argent::input::publish(controls);
 head.orientation={0,1,0,0};updatePose(head,true,false,true);setter(object,position,identity.data());
 check(wallClimbView(climbAim.data())&&climbAim[0]<-.70f&&climbAim[1]>.70f,"Climbing restricted physical look behind");
 put(uintptr_t(climbState.data())+12,int(1));publishPhysics(np,{10,20,30});updatePose(head,true,false,true);
 check(std::abs(argent::input::bodyTurn)>.1f&&std::abs(argent::input::movementYaw)>1,"Leaving wall did not restore ordinary body following");
 argent::input::clear();
 check(swimmingView(swimAxis.data()),"Fresh gameplay pose missing for swim adapter");
 argent::presentation::gameplayInput=false;check(!swimmingView(swimAxis.data()),"Swimming pose leaked into menu");
 argent::presentation::gameplayInput=true;stop();check(!swimmingView(swimAxis.data()),"Stopped tracking retained swimming pose");
 argent::revenant::installed=false;argent::revenant::expectedType=0;argent::presentation::player=0;argent::presentation::playerVtable=0;
 std::cout<<"Native Eternal setter trampoline, object bounds, quad, recenter, stale pose pass\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

