#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "../src/openxr/WeaponConfig.h"
#include "../src/openxr/EternalMotionWheel.h"
#include "../src/LaserSightPolicy.h"
#include "../src/openxr/ShoulderChainsaw.h"
#include "../src/BodyYawFollow.h"
#include "../src/openxr/PhysicalGloryKill.h"
#include "../src/openxr/CrucibleGesture.h"
#include "../src/WeaponIdlePose.h"
#include "../src/CrucibleRestPose.h"
#include "../src/WeaponHandoffVisibility.h"
#include "../src/LauncherSettings.h"
#include "../src/SwimmingAim.h"
#include "../src/MeathookAim.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
using namespace argent::input;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
bool almostEqual(float a,float b){return std::abs(a-b)<.0002f;}
int main(){
 for(bool left:{false,true})for(bool full:{false,true}){
  EternalMotionWheel wheel;XINPUT_GAMEPAD pad{};pad.wButtons=XINPUT_GAMEPAD_RIGHT_SHOULDER;
  XrPosef hand{{0,0,0,1},{}};XrQuaternionf head{0,0,0,1};
  auto result=wheel.update(pad,true,left,full,hand,true,head);check(!result.click&&!pad.sThumbRX,"Wheel initial anchor moved");
  hand.position.x=.065f;result=wheel.update(pad,true,left,full,hand,true,head);
  check(pad.sThumbRX>32000&&result.click&&result.hand==(left?0:1),"Motion wheel or physical haptic hand wrong");
  pad.sThumbRX=0;pad.sThumbRY=axis(.8f);result=wheel.update(pad,true,left,full,hand,false,head);
  check(result.stickOwned&&result.click&&result.hand==(left&&full?1:0)&&pad.sThumbRY==axis(.8f),"Stick priority/haptics lost without tracking");
  result=wheel.update(pad,true,left,full,hand,false,head);check(!result.click,"Same stick sector repeats click");
  pad.sThumbRX=pad.sThumbRY=0;result=wheel.update(pad,true,left,full,hand,true,head);
  check(!result.click&&pad.sThumbRX==0,"Tracking reacquisition jumped selection");
  pad={};result=wheel.update(pad,false,left,full,hand,true,head);
  check(!result.active&&!result.click&&!wheel.motion.anchorValid,"Closed wheel retained gesture");
 }

 {
  argent::player::CrucibleRestPose hammer;hammer.cached=true;
  check(hammer.selectPhysical(1050,1000,false,true,false),"Hammer visual policy regression");
  check(!hammer.selectPhysical(1100,1000,true,true,false),"Hammer visual policy regression"); // Manual trigger wins.
  hammer.cached=true;
  check(!hammer.selectPhysical(1200,1150,false,false,false),"Hammer visual policy regression"); // Enemy cinematic/traversal wins.
  hammer.cached=true;
  check(hammer.selectPhysical(1350,1300,false,true,false),"Hammer visual policy regression");
  check(!hammer.selectPhysical(2900,1300,false,true,false),"Hammer visual policy regression"); // No stuck visual lock.
  check(!hammer.selectPhysical(3000,1300,false,true,true),"Hammer visual policy regression");
  check(hammer.canCapture(3400),"Hammer visual policy regression");
 }
try{
 // Reconstruct native idAngles from controller world basis, including pitch
 // and roll. Neither handedness nor body yaw may mirror the target direction.
 for(float pitch:{-80.f,-25.f,0.f,35.f,80.f})for(float yaw:{-170.f,-90.f,0.f,65.f,170.f})for(float roll:{-45.f,0.f,45.f}){
  constexpr float rad=.01745329252f;
  const float cp=std::cos(pitch*rad),sp=std::sin(pitch*rad),cy=std::cos(yaw*rad),sy=std::sin(yaw*rad),cr=std::cos(roll*rad),sr=std::sin(roll*rad);
  argent::camera::Basis basis{cp*cy,cp*sy,-sp,sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,sr*cp,cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
  float angles[3]{};check(argent::player::meathookViewAngles(basis,angles),"Meathook rejected valid controller basis");
  check(almostEqual(angles[0],pitch)&&almostEqual(angles[1],yaw)&&almostEqual(angles[2],roll),"Meathook target angles lost pitch/roll or mirrored yaw");
 }
 {float unchanged[3]{1,2,3};argent::camera::Basis invalid{};
  check(!argent::player::meathookViewAngles(invalid,unchanged)&&unchanged[0]==1&&unchanged[1]==2&&unchanged[2]==3,"Invalid Meathook tracking changed native view");}
 {
  argent::player::CrucibleRestPose p;
  using E=argent::player::CrucibleRestPose::Events;
  const float identity[9]={1,0,0,0,1,0,0,0,1},hand[3]={10,20,30},origin[3]={11,22,33};
  check(!p.select(1000,0,false,true,true,{}),"Idle must remain native");
  check(!p.canCapture(1200)&&p.canCapture(1300),"Must wait for settled idle");
  check(p.capture(hand,identity,origin,identity),"Capture idle attachment");
  check(!p.select(1400,1400,false,true,true,{}),"Input alone must not invent a native swing");
  check(p.select(1450,1400,false,true,false,E{1,0,1440}),"Native begin must select velocity rest pose");
  const float moved[3]={40,50,60},turned[9]={0,1,0,-1,0,0,0,0,1};float o[3]{},a[9]{};
  p.apply(moved,turned,o,a);
  check(almostEqual(o[0],38)&&almostEqual(o[1],51)&&almostEqual(o[2],63),"Rest attachment must follow translated and rotated hand");
  for(int k=0;k<9;++k)check(almostEqual(a[k],turned[k]),"Rest orientation must rotate with controller");
  check(p.select(1650,1400,false,true,true,E{1,0,1440}),"Idle-labelled node must not end an active native swing");
  check(!p.select(1700,1400,true,true,false,E{1,0,1440}),"Real trigger must win immediately");
  check(!p.select(1710,1400,false,true,false,E{1,0,1440}),"Release must not resurrect overridden gesture");
  p.capture(hand,identity,origin,identity);
  check(p.select(1800,1800,false,true,false,E{3,2,1800}),"Next gesture must work");
  check(!p.select(1810,1800,false,false,false,E{3,2,1800}),"Enemy hit/authored camera must keep native pose");
  check(!p.select(1820,1800,false,true,false,E{3,2,1800}),"Hit bypass must remain for the rest of attack");
  p.capture(hand,identity,origin,identity);
  check(!p.select(2400,1800,false,true,false,E{5,4,2400}),"Old velocity must not claim later trigger attack");
  check(p.select(2500,2500,false,true,false,E{7,6,2500}),"Fresh gesture");
  check(p.select(2600,2500,false,true,false,E{7,8,2500}),"Recovery transition must keep rest pose until real idle");
  check(!p.select(2620,2500,false,true,true,E{7,8,2500}),"Native end and idle must release visual hold");
  check(!p.select(2650,2500,false,true,false,E{7,8,2500}),"Ended attack cannot restart from snapshot");
  check(p.select(2700,2700,false,true,false,E{9,8,2700}),"Fresh bounded attack");
  check(!p.select(4201,2700,false,true,false,E{9,8,2700}),"Missing end event must not latch forever");
  check(!p.select(4250,2700,false,true,false,E{9,8,2700}),"Timeout cannot rearm from held event");
  p={};p.capture(hand,identity,origin,identity);
  check(!p.select(5000,5000,false,true,false,E{1,2,5000}),"Begin and end in one frame must stay native");
  p={};p.capture(moved,turned,o,a);float round[3]{},axes[9]{};p.apply(hand,identity,round,axes);
  for(int k=0;k<3;++k)check(almostEqual(round[k],origin[k]),"Capturing rotated idle must preserve local offset");
 }
 // Native commands have already been yaw-rotated by XInput. Compose them
 // with the adapted physics frame and compare against head-local swimming.
 for(float body:{-170.f,0.f,45.f,150.f})for(float yaw:{-150.f,0.f,90.f,170.f})for(float pitch:{-80.f,-45.f,0.f,60.f,89.f}){
  const float b=body*.01745329252f,h=yaw*.01745329252f,p=pitch*.01745329252f;
  float nr[3]={std::sin(b),-std::cos(b),0},head[3]={std::cos(h)*std::cos(p),std::sin(h)*std::cos(p),std::sin(p)},nf[3]{},right[3]{};
  check(argent::player::swimmingBasis(nr,head,nf,right),"Valid swim basis rejected");
  for(float forward:{-1.f,0.f,1.f})for(float strafe:{-.5f,0.f,.5f}){
   const float x=std::cos(h)*forward+std::sin(h)*strafe,y=std::sin(h)*forward-std::cos(h)*strafe;
   const float f=x*std::cos(b)+y*std::sin(b),r=x*nr[0]+y*nr[1];
   check(almostEqual(nf[0]*f+right[0]*r,head[0]*forward+std::sin(h)*strafe)&&almostEqual(nf[1]*f+right[1]*r,head[1]*forward-std::cos(h)*strafe)&&almostEqual(nf[2]*f+right[2]*r,head[2]*forward),"Swim pitch lost, strafe tilted, or HMD yaw applied twice");
  }
 }
 {float right[3]={0,-1,0},head[3]={0,0,-1},a[3]{},b[3]{};
  check(argent::player::swimmingBasis(right,head,a,b)&&almostEqual(a[2],-1)&&almostEqual(b[1],-1),"Vertical dive failed");
  head[0]=9;check(!argent::player::swimmingBasis(right,head,a,b),"Invalid tracking accepted for swimming");}
 using argent::player::hideFirstPersonRoot;
 check(hideFirstPersonRoot(true,true,false,false,true),"Drone authored weapon must remain hidden");
 check(hideFirstPersonRoot(false,true,false,true,true),"Drone authored arms flashed in Quad");

 check(!hideFirstPersonRoot(true,true,false,true),"Glory Kill native arms must be visible");
 check(hideFirstPersonRoot(false,false,false,true),"Quad restored flat arms");
 check(hideFirstPersonRoot(true,false,false,true),"Native arms flashed during VR handoff");
 check(hideFirstPersonRoot(true,false,false,false),"Unplaced flat weapon flashed during VR handoff");
 check(hideFirstPersonRoot(true,false,true,true),"Native arms restored after VR placement");
 check(!hideFirstPersonRoot(true,false,true,false),"Placed VR weapon stayed hidden");
 check(!hideFirstPersonRoot(false,true,false,false),"Authored camera must retain its native weapon");
 check(hideFirstPersonRoot(false,false,false,false),"Quad transition restored unplaced flat weapon");
 // Full transition: gameplay -> authored kill -> gameplay awaiting VR pose.
 for(bool arms:{false,true}){
  check(hideFirstPersonRoot(true,false,false,arms),"Handoff exposed flat model");
  check(!hideFirstPersonRoot(true,true,false,arms),"Kill failed to restore native rig");
  check(hideFirstPersonRoot(true,true,false,arms,true),"Drone/Monkey Bar exemption lost");
  check(hideFirstPersonRoot(true,false,false,arms),"Kill exit exposed flat fallback");
 }
 using namespace argent::camera;
 BodyYawFollow yaw;yaw.observe(0);check(yaw.request(90,false,true)<0,"physical left turn must request native left stick");yaw.observe(20);check(almostEqual(yaw.accepted,20),"accepted native yaw missing");
 Basis native{},rendered{};rotateBasis(Basis{1,0,0,0,1,0,0,0,1},yawQuaternion(0),yawQuaternion(20),native);
 rotateBasis(native,yaw.reference(yawQuaternion(0)),yawQuaternion(90),rendered);check(almostEqual(rendered[0],0)&&almostEqual(rendered[1],1),"body catch-up double rotated view/weapon");
 yaw.request(70,true,true);yaw.observe(30);check(almostEqual(yaw.accepted,20),"manual turn consumed tracking yaw");yaw.snap(1,45,true);check(almostEqual(yaw.accepted,65),"snap not applied");yaw.snap(1,45,true);check(almostEqual(yaw.accepted,65),"held snap repeated");yaw.snap(0,45,true);yaw.snap(1,45,true);check(almostEqual(yaw.accepted,110),"snap did not rearm");
 XrPosef primary{{0,0,0,1},{0,1,0}},supportPose{{0,0,0,1},{0,1,-.3f}};WeaponCalibration c;TwoHandSupport two;
 auto q=two.update(primary,supportPose,true,true,c);check(two.latched&&almostEqual(q.w,1),"support did not acquire");
 supportPose.position.x=.1f;q=two.update(primary,supportPose,true,true,c);auto forward=rotate(q,{0,0,-1});check(forward.x>0&&forward.z<0,"support aims wrong way");
 two.update(primary,supportPose,false,true,c);check(two.latched,"tracking loss discarded held support");
 check(almostEqual(two.update(primary,supportPose,false,true,c).w,primary.orientation.w),"Untracked support changed aim");
 two.update(primary,supportPose,true,true,c);check(two.latched,"held grip failed to resume after tracking loss");
 supportPose.position.x=1;two.update(primary,supportPose,true,true,c);check(two.latched,"distant hand released held support");
 two.update(primary,supportPose,true,false,c);check(!two.latched,"released grip retained support");
 two.update(primary,supportPose,true,true,c);check(!two.latched,"distant fresh grip acquired support");
 two.update(primary,supportPose,true,false,c);supportPose.position.x=0;
 two.update(primary,supportPose,true,true,c);check(two.latched,"support did not rearm near grab point");
 PoseCalibration hand;hand.offset={.1f,0,0};auto pose=calibrated(primary,primary,hand,c);check(almostEqual(pose.position.x,.1f),"hand offset missing");
 primary.orientation=euler(0,90,0);pose=calibrated(primary,primary,hand,c);check(almostEqual(pose.position.z,-.1f),"offset failed to follow physical yaw");
 // Yaw equivariance: physically rotate both hands, solved aim rotates equally.
 XrPosef a{{0,0,0,1},{0,0,0}},b{{0,0,0,1},{.08f,0,-.3f}};TwoHandSupport t1,t2;auto q1=t1.update(a,b,true,true,c);auto turn=euler(0,120,0);a.orientation=turn;b.position=rotate(turn,b.position);auto q2=t2.update(a,b,true,true,c);
 auto expected=rotate(turn,rotate(q1,{0,0,-1})),actual=rotate(q2,{0,0,-1});check(length(sub(expected,actual))<.0002f,"two-hand orientation lost physical turn");
 WeaponConfig pivotConfig;check(almostEqual(pivotConfig.weaponPivot.x,-.15f),"Calibrated pivot default lost");std::istringstream pivotInput("weapon_pivot -0.1 0.025 -0.015\n");
 check(readWeaponConfig(pivotInput,pivotConfig)&&almostEqual(pivotConfig.weaponPivot.x,-.1f)&&almostEqual(pivotConfig.weaponPivot.y,.025f)&&almostEqual(pivotConfig.weaponPivot.z,-.015f),"Pivot configuration rejected");
 for(auto text:{"weapon_pivot 1 0 0","weapon_pivot nan 0 0","weapon_pivot 0 0"}){std::istringstream invalidPivot(text);check(!readWeaponConfig(invalidPivot,pivotConfig),"Invalid pivot accepted");}
 WeaponConfig config;check(config.handsJump,"Hands Jump default differs from KHARVOX");
 {std::istringstream off("hands_jump 0\n");check(readWeaponConfig(off,config)&&!config.handsJump,"Hands Jump disable missing");
 std::istringstream on("hands_jump 1\n");check(readWeaponConfig(on,config)&&config.handsJump,"Hands Jump enable missing");
 std::istringstream bad("hands_jump 2\n");check(!readWeaponConfig(bad,config),"Invalid Hands Jump accepted");}
 check(!config.laserSight,"Laser enabled by default");
 check(!config.leftHandSwapSticks,"Full stick swap enabled by default");
 std::istringstream leftLayout("dominant left\nleft_hand_swap buttons-and-sticks\n");check(readWeaponConfig(leftLayout,config)&&config.leftHanded&&config.leftHandSwapSticks,"Full left-hand layout not parsed");
 std::istringstream narrowLayout("dominant left\nleft_hand_swap buttons\n");check(readWeaponConfig(narrowLayout,config)&&config.leftHanded&&!config.leftHandSwapSticks,"Narrow left-hand layout not parsed");
 std::istringstream invalidLayout("left_hand_swap invalid\n");check(!readWeaponConfig(invalidLayout,config),"Invalid layout accepted");
 std::istringstream laserConfig("laser_sight 1\nshow_hands 0\n");check(readWeaponConfig(laserConfig,config)&&config.laserSight&&!config.showHands,"Laser depends on visible VR hands");
 std::istringstream badLaser("laser_sight 2\n");check(!readWeaponConfig(badLaser,config),"Invalid laser switch accepted");
 for(const auto key:{"crucible","chainsaw","fists","default"})check(!argent::laserWeaponAllowed(kharvox::hands::handProfile(key)),"Laser enabled for melee or unknown weapon");
 for(const auto key:{"combat_shotgun","heavy_cannon","plasma_rifle","rocket_launcher","super_shotgun","ballista","chaingun","bfg"})check(argent::laserWeaponAllowed(kharvox::hands::handProfile(key)),"Firearm laser missing");
 std::istringstream good("dominant left\nmovement offhand\nturn snap 30\nprofile shotgun\nweapon shotgun 0 0 0 0 5 0 0 0 -.3 .12 .8 1\n");check(readWeaponConfig(good,config)&&config.leftHanded&&config.snapTurn&&config.profile=="shotgun","configuration not accepted");
 std::istringstream bad("weapon bad 9 0 0 0 0 0 0 0 -.3 .12 1 1\n");check(!readWeaponConfig(bad,config)&&config.profile=="shotgun","invalid configuration partially published");
 std::istringstream missing("profile absent\n");check(!readWeaponConfig(missing,config),"unknown weapon profile accepted");
 std::istringstream launcherAngles("turn smooth\nsnap_turn_angle 85\nsmooth_turn_speed 295\n");
 check(readWeaponConfig(launcherAngles,config)&&!config.snapTurn&&almostEqual(config.snapDegrees,85)&&almostEqual(config.smoothSpeed,295),"independent launcher turn values not consumed");
 std::istringstream options("automatic_presentation 0\ncinematics_quad 0\ncinematics_3d 1\nsync_immersive 0\nmovement_immersive 0\ncontroller_layout legacy\ncrouch_enabled 0\nshoulder_chainsaw 0\nvirtual_gunstock 0\nbhaptics_enabled 0\npsvr2_adaptive_triggers 0\n");check(readWeaponConfig(options,config)&&config.cinematics3d&&!config.automaticPresentation&&config.cinematicsQuad&&!config.syncImmersive&&!config.movementImmersive&&config.legacyLayout&&!config.crouchEnabled&&!config.shoulderChainsaw&&!config.selected().twoHand&&!config.bhapticsEnabled&&!config.psvr2AdaptiveTriggers,"launcher policy settings ignored");
 ShoulderChainsaw shoulder;XrPosef h{{0,0,0,1},{0,1.7f,0}},g{{0,0,0,1},{.25f,1.5f,.2f}};
 check(!shoulder.update(h,g,true,true,false,false,1000),"shoulder fired without grip");
 check(shoulder.update(h,g,true,true,true,false,1001)&&shoulder.consumed,"shoulder grab did not fire");
 check(!shoulder.update(h,g,true,true,true,false,1101)&&shoulder.consumed,"held shoulder repeated");
 shoulder.update(h,g,false,true,true,false,1110);check(!shoulder.update(h,g,true,true,true,false,1120),"tracking recovery fired held grip");
 shoulder.update(h,g,true,true,false,false,1130);auto rotation=euler(0,90,0);g.position=add(h.position,rotate(rotation,sub(g.position,h.position)));h.orientation=rotation;
 check(shoulder.update(h,g,true,true,true,false,1140),"rotated head-relative shoulder missed");
 shoulder={};h.orientation={0,0,0,1};g.position={-.25f,1.5f,.2f};shoulder.update(h,g,true,true,false,true,1200);
 check(shoulder.update(h,g,true,true,true,true,1201),"left-handed shoulder missed");
 check(!shoulder.update(h,g,true,false,true,true,1202),"shoulder remained active in menu");

 BodyYawFollow smooth;smooth.smooth(1,230,true,1000);for(int t=1010;t<=1100;t+=10)smooth.smooth(1,230,true,t);
 check(almostEqual(smooth.accepted,23),"smooth turn speed is not degrees per second");
 smooth.smooth(1,230,true,5100);check(almostEqual(smooth.accepted,23),"stall caused large turn");smooth.smooth(.1f,230,true,5110);check(almostEqual(smooth.accepted,23),"turn deadzone missing");
 smooth.smooth(-1,400,false,5120);check(almostEqual(smooth.accepted,23),"menu smooth turn active");smooth.smooth(-1,400,true,5130);check(almostEqual(smooth.accepted,19),"left smooth turn incorrect");
 std::istringstream saved("hand left 0.1 0 0 0 0 0\nweapon default 0 0 0 0 5 0 0 0 -.3 .12 .8 1\n");
 auto configText=argent::launcher::updateSettings(saved,{{"dominant","left"},{"turn","snap 180"},{"smooth_turn_speed","400"},{"physical_glory_kill","1"},{"physical_glory_kill_speed","2.8"},{"physical_glory_kill_hands","both"}});
 std::istringstream roundtrip(configText);check(readWeaponConfig(roundtrip,config)&&config.leftHanded&&config.gloryHands==2&&config.physicalGloryKill&&almostEqual(config.smoothSpeed,400)&&almostEqual(config.hand[0].offset.x,.1f)&&almostEqual(config.snapDegrees,180),"launcher settings/calibration roundtrip failed");
 for(auto badRow:{"smooth_turn_speed nan","smooth_turn_speed 401","physical_glory_kill_speed 4.1","physical_glory_kill 2","physical_glory_kill_hands dominant","turn snap 9"}){std::istringstream bad(badRow);check(!readWeaponConfig(bad,config),"unsafe gesture/turn setting accepted");}
 PhysicalGloryKill punch;std::array<XrPosef,2> punchPose{{{{0,0,0,1},{}},{{0,0,0,1},{}}}};std::array<XrVector3f,2> speed{};std::array<bool,2> tracked{true,true};
 auto hit=[&](int64_t ms,bool enabled=true,int hand=2){return punch.update(ms*1000000,enabled,hand,2.8f,punchPose,speed,tracked);};
 check(!hit(1000),"resting hand fired");speed[1]={0,0,-2.79f};check(!hit(1010),"below threshold fired");speed[1].z=-2.8f;check(hit(1020),"right forward punch missed");check(hit(1219)&&!hit(1220),"200ms pulse incorrect");
 speed={};hit(1230);speed[0].z=-4;check(!hit(1300),"shared cooldown missing");check(hit(1370),"left punch missed after cooldown");check(!hit(1380,false),"menu retained melee pulse");
 tracked[0]=false;hit(1800);tracked[0]=true;check(!hit(1810),"tracking recovery fired before rearm");speed={};hit(1820);speed[0].x=4;check(!hit(1830),"sideways swipe fired");speed[0]={0,0,-4};check(!hit(1840,true,1),"right-only option accepted left punch");
 punch={};speed={};punchPose[0].orientation=euler(0,90,0);hit(2000);speed[0]=rotate(punchPose[0].orientation,{0,0,-3});check(hit(2010,true,0),"physically rotated left punch missed");
 {
  for(int primary:{0,1}){
   CrucibleGesture g;speed={};punchPose={{{{0,0,0,1},{}},{{0,0,0,1},{}}}};tracked={true,true};
   auto swing=[&](int64_t ms,bool crucible=true,bool enabled=true){return g.update(ms*1000000,enabled,crucible,primary,2,2.8f,punchPose,speed,tracked);};
   check(!swing(3000).fire,"Crucible rest fired");speed[primary]={2.79f,0,0};check(!swing(3010).fire,"Crucible fired below threshold");
   speed[primary]={0,3,0};auto r=swing(3020);check(r.fire&&!r.punch,"Sword swing failed or invoked Glory Kill");
   check(swing(3219).fire&&!swing(3220).fire,"Crucible trigger pulse duration wrong");
   check(!swing(3400).fire,"Fast held motion retriggered Crucible");speed={};swing(3410);speed[1-primary]={0,0,-3};r=swing(3420);
   check(r.punch&&!r.fire,"Crucible off-hand did not route Glory Kill exclusively");
   check(!swing(3430,false).punch,"Weapon switch retained Crucible gesture pulse");
   speed={};swing(3800);speed[primary]={3,0,0};check(swing(3810).fire,"Rearmed sword swing failed");tracked[primary]=false;check(!swing(3820).fire,"Lost tracking retained sword trigger");
   tracked[primary]=true;check(!swing(3830).fire,"Tracking recovery fired without rearm");
   check(!swing(3840,true,false).fire,"Menu retained Crucible trigger");
  }
  std::istringstream cfg("profile crucible\n");WeaponConfig c;check(readWeaponConfig(cfg,c),"Crucible calibration profile rejected");
  check(!c.selectedFor("crucible").twoHand,"Crucible enabled support grab");

 }
 argent::player::WeaponIdlePose frozen;float p[3]{1,2,3},basis[9]{1,0,0,0,1,0,0,0,1};
 check(frozen.apply(1,2,3,true,true,p,basis),"tracked weapon not stored");p[0]=99;basis[0]=.3f;
 check(frozen.apply(1,2,3,true,false,p,basis)&&p[0]==1&&basis[0]==1,"idle weapon moved/rescaled with native hands");
 p[0]=4;check(frozen.apply(1,2,3,true,true,p,basis)&&p[0]==4,"tracking resume kept frozen pose");
 check(!frozen.apply(1,2,3,false,false,p,basis)&&!frozen.apply(1,2,3,true,false,p,basis),"menu reused stale frozen pose");
 frozen.apply(1,2,3,true,true,p,basis);check(!frozen.apply(2,2,3,true,false,p,basis),"new player inherited idle weapon");
 frozen.apply(2,2,3,true,true,p,basis);check(!frozen.apply(2,2,4,true,false,p,basis),"new weapon inherited idle transform");
 std::cout<<"Physical yaw reconciliation, snap latch, calibrated transforms, two-hand release/equivariance and transactional configuration pass\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
