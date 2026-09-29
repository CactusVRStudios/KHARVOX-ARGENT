#include "EternalCameraHook.h"
#include "AaOverridePolicy.h"
#include "FovComparison.h"
#include "PerformanceDiagnostics.h"
#include "Diagnostics.h"
#include "EternalCameraMath.h"
#include "CameraBasisPolicy.h"
#include "EternalRenderControls.h"
#include "HeadsetFov.h"
#include "AnimationFovGuard.h"
#include <unordered_map>
#include "hud/EternalWeaponWheel.h"
#include "hud/HudPlaceholder.h"
#include "hud/EternalHitMarkers.h"
#include "RoomscaleFollow.h"
#include "BodyYawFollow.h"
#include "EternalPlayerHooks.h"
#include "EternalDlssHook.h"
#include "EternalPresentation.h"
#include "QuadRuntime.h"
#include "openxr/ControllerInput.h"
#include <MinHook.h>
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <intrin.h>
namespace argent::camera { namespace {
using Setter=void(__fastcall*)(void*,const float*,const float*);
Setter original{};void* target{};
std::mutex poseMutex;
XrQuaternionf current{0,0,0,1},reference{0,0,0,1};
XrVector3f currentPosition{},referencePosition{};
bool translate{},needPositionReference{true};
bool active{},needReference{true};ULONGLONG updated{};
RoomscaleFollow follow;BodyYawFollow yawFollow;
uint64_t weaponEpoch{1};
uintptr_t viewActor{};
uintptr_t physicsOwner{},anchorOwner{};XrVector3f physicsPosition{},anchorOffset{};ULONGLONG physicsTick{};
Basis bodyBasis{1,0,0,0,1,0,0,0,1};std::array<float,3> bodyOrigin{};bool bodyValid{};
bool animationActive{};Basis animationBasis{};XrQuaternionf animationReference{};
void* animationCamera{};ULONGLONG animationCameraTick{};
std::atomic<uint64_t> calls{},applied{},rejected{};
std::atomic<bool> installed{};
struct WorldHands {input::Snapshot sample{};std::array<std::array<float,3>,2> position{};std::array<Basis,2> axes{};};
WorldHands placedWorldHands;
struct WorldLaser {std::array<float,3> origin{};Basis axis{};std::array<char,32> profile{};uint64_t epoch{};ULONGLONG tick{};bool valid{};};
WorldLaser placedWorldLaser,matchedWorldLaser;XrPosef matchedLaserPose{};
struct AppliedCamera {Basis basis{};XrPosef head{};ULONGLONG tick{};std::array<float,3> origin{};WorldHands hands{};uint64_t id{};std::array<kharvox::hands::HandHudPanels,16> panels{};
 WorldLaser laser{};
};
std::array<AppliedCamera,128> history{};size_t historyCursor{};
uint64_t renderSerial{},matchedSerial{};XrPosef matchedHead{};
uint64_t cameraId{},matchedCameraId{};
input::Snapshot matchedHands{};float matchedDepthA{},matchedDepthB{};
std::array<float,3> hudOrigin{};Basis hudAxis{};ULONGLONG hudTick{};
std::array<float,3> hudGrip{};Basis hudHand{};bool hudHandValid{},hudLeftMode{};
// Called only under poseMutex. Remote possession is a different camera owner,
// even though the engine keeps the original idPlayer alive.
void synchronizeControlledActor(){
 const auto next=revenant::actor.load();if(next==viewActor)return;
 viewActor=next;++weaponEpoch;
 needReference=true;needPositionReference=true;bodyValid=false;animationActive=false;
 physicsOwner=anchorOwner=0;physicsTick=0;follow={};yawFollow={};placedWorldHands={};
 history={};historyCursor=0;matchedSerial=0;matchedHands={};hudHandValid=false;hudTick=0;
 input::weaponApplied({});input::followStick({});input::headMovement(0);input::followTurn(0);
 log(std::string("ETERNAL_CONTROLLED_ACTOR mode=")+(next?"campaign-revenant":"slayer")+" camera=rebase");
}
void applyCrouchHeight(std::array<float,3>& origin){
 // Eternal's pm_crouchviewheight default is 0.8763 metres. Keep controller and
 // view on the same body origin when button crouch changes the native capsule.
 if(!viewActor&&presentation::crouched.load()&&GetTickCount64()-presentation::crouchTick.load()<100)
  origin[2]+=std::min(0.f,.8763f-anchorOffset.z);
}
using Poll=int(__fastcall*)(void*,int);
Poll originalPoll{};
using Discover=void(__fastcall*)(void*);
Discover discover{};
std::atomic<ULONGLONG> lastDiscovery{};
void maintainRenderControls();
int __fastcall pollController(void* object,int user){
 if(user==0)maintainRenderControls();
 // Eternal's discovery thread only tries disconnected slots after its own
 // rescan flag is raised. VR becomes ready after that initial scan.
 if(user==0&&discover){
  bool ready;{std::lock_guard<std::mutex> lock(input::stateMutex);ready=input::fresh(input::state,GetTickCount64());}
  auto last=lastDiscovery.load();const auto now=GetTickCount64();
  if(ready&&now-last>1000&&lastDiscovery.compare_exchange_strong(last,now))discover(object);
 }
 return originalPoll(object,user);
}
using CvarSetter=bool(__fastcall*)(void*,const char*,bool);
CvarSetter setCvar{};uintptr_t cvarBase{};
std::atomic<uint64_t> controlTicks{};
LightCullingScope lightCulling;
WheelBloomScope wheelBloom;
std::mutex controlMutex;
std::mutex fovMutex;
float requiredFovX{},requiredFovY{},observedFovX{},observedFovY{};
float calibratedFovX{},calibratedFovY{};int calibratedFov{};
ULONGLONG observedFovTick{},fovChanged{};
int automaticFov=90;
using FinalizeCamera=void(__fastcall*)(void*);
FinalizeCamera originalFinalizeCamera{};
void __fastcall finalizeCamera(void* object){
 // Only touch a camera while the engine owns it in this callback. Never retain
 // a pointer for asynchronous restoration. Each camera has independent state.
 thread_local std::unordered_map<void*,AnimationFovGuard> guards;
 auto* fov=reinterpret_cast<float*>(static_cast<unsigned char*>(object)+0xb8);
 auto previous=guards.find(object);
 if(previous!=guards.end()){previous->second.restore(fov[0],fov[1]);guards.erase(previous);}
 originalFinalizeCamera(object);
 bool protect;{
  std::lock_guard<std::mutex> lock(poseMutex);
  const auto now=GetTickCount64();
  protect=active&&animationActive&&animationCamera==object&&now-updated<250&&now-animationCameraTick<250;
 }
 if(!protect)return;
 float x,y;{std::lock_guard<std::mutex> lock(fovMutex);x=requiredFovX;y=requiredFovY;}
 AnimationFovGuard guard;
 if(guard.apply(fov[0],fov[1],x,y)){
  guards.emplace(object,guard);
  static std::atomic<uint64_t> count{};
  if(count.fetch_add(1)%600==0)log("ETERNAL_ANIMATION_FOV native="+std::to_string(guard.nativeX)+","+std::to_string(guard.nativeY)+" protected="+std::to_string(fov[0])+","+std::to_string(fov[1]));
 }
}
std::filesystem::path lightCullingTestMarker;
void maintainRenderControls(){
 if(!setCvar)return;
 std::unique_lock<std::mutex> controlLock(controlMutex,std::try_to_lock);
 if(!controlLock.owns_lock())return;
 bool world;{std::lock_guard<std::mutex> lock(poseMutex);world=active;}
 const bool wheelShown=world&&presentation::gameplayInput.load()&&!presentation::hideGameplayHud.load()&&hud::weaponWheelVisible();
 // Visibility transitions are immediate; ordinary maintenance stays throttled.
 auto read=[](uintptr_t address,void* output,size_t size){
  SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&got)&&got==size;
 };
 // RT must not wait for the ordinary 120-callback maintenance interval.
 int rayTracing{};const bool rayTracingEnabled=readRenderControl(cvarBase,rayTracingControl,read,rayTracing)&&rayTracing!=0;
 if(controlTicks.fetch_add(1)%120&&wheelShown==wheelBloom.held&&!rayTracingEnabled)return;
 const bool gameplayWorld=world;
 // AA recreates temporal render targets. Do not execute a queued diagnostic
 // at the first camera callback while the level is still constructing them.
 const auto aaNow=GetTickCount64();
 static AaOverridePolicy aaPolicy;
 const auto aaContext=presentation::classify(presentation::snapshot(),aaNow,presentation::options());
 const bool aaReady=aaPolicy.ready(aaNow,int(aaContext),aaContext!=presentation::Mode::Unknown);
 // Opt-in A/B test only. Removing the marker restores the engine's value
 // during the running level, without a restart. Default is normal GPU culling.
 const auto attributes=cleanRelease||lightCullingTestMarker.empty()?INVALID_FILE_ATTRIBUTES:GetFileAttributesW(lightCullingTestMarker.c_str());
 world=world&&attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
 const bool transition=world!=lightCulling.held;
 // Execute on an engine camera/input callback. Use its setter (allocation, locking,
 // numeric conversion and change notifications), never raw CVar value writes.
 thread_local bool busy=false;if(busy)return;busy=true;

 const bool bloomTransition=wheelShown!=wheelBloom.held;
 const bool bloomOk=wheelBloom.update(wheelShown,[&](int& value){return readRenderControl(cvarBase,wheelBloomControl,read,value);},[&](int value){
  const auto text=std::to_string(value);return setCvar(reinterpret_cast<void*>(cvarBase+build::rva(wheelBloomControl.rva)),text.c_str(),true);
 });
 if(bloomTransition||!bloomOk)log("ETERNAL_WHEEL_BLOOM visible="+std::to_string(wheelShown)+" success="+std::to_string(bloomOk)+" restore="+std::to_string(wheelBloom.restore));
 // FSR owns AA while selected; otherwise preserve engine-selected DLSS.
 static const bool fsrActive=environmentFlag("ARGENT_FSR1");
 const RenderControl aaMode{0x6685ce0,"r_antialiasing","",0};
 int aa{};const bool aaKnown=readRenderControl(cvarBase,aaMode,read,aa);
 const int desiredAa=aaTarget(aaKnown?aa:-1,fsrActive,dlss::failed());
 if(aaReady&&desiredAa>=0){
  int before{},after{};bool accepted{};
  if(aaPolicy.apply(aaNow,desiredAa,[&](int& value){return readRenderControl(cvarBase,aaMode,read,value);},[&](int value){const auto text=std::to_string(value);return setCvar(reinterpret_cast<void*>(cvarBase+build::rva(aaMode.rva)),text.c_str(),true);},before,after,accepted))
   log("ETERNAL_AA_ENFORCED before="+std::to_string(before)+" requested="+std::to_string(desiredAa)+" accepted="+std::to_string(accepted)+" after="+std::to_string(after)+" verified="+std::to_string(after==desiredAa)+" context="+presentation::name(aaContext));
 }
#ifndef ARGENT_CLEAN_RELEASE
 static FovComparison fovTest;static int lastFovPhase=-2;static uint64_t fovReport{};
 if(perf::enabled()&&!lightCullingTestMarker.empty()){
  auto request=lightCullingTestMarker.parent_path()/L"fov-compare.request";std::ifstream command(request);int value=-1;
  if(command>>value){command.close();std::error_code error;std::filesystem::remove(request,error);
   if(!error&&(value==0||value==1)){fovTest.request(value==1);log("PERF_FOV_REQUEST start="+std::to_string(value));}
  }
 }
#endif
 int selectedFov;
 {std::lock_guard<std::mutex> lock(fovMutex);int actual{};
  if(gameplayWorld&&readRenderControl(cvarBase,renderControls[0],read,actual)&&actual==automaticFov&&aaNow-fovChanged>1500&&observedFovTick>fovChanged+1000&&aaNow-observedFovTick<500){
   if(!calibratedFov){calibratedFov=actual;calibratedFovX=observedFovX;calibratedFovY=observedFovY;}
   const int next=engineFov(requiredFovX,requiredFovY,calibratedFovX,calibratedFovY,calibratedFov);
   if(next&&std::abs(next-automaticFov)>=2){automaticFov=next;fovChanged=aaNow;
    log("HEADSET_FOV engine="+std::to_string(next)+" requiredTanX="+std::to_string(requiredFovX)+" requiredTanY="+std::to_string(requiredFovY)+" observedTanX="+std::to_string(observedFovX)+" observedTanY="+std::to_string(observedFovY));}
  }
#ifdef ARGENT_CLEAN_RELEASE
  selectedFov=automaticFov;
#else
  selectedFov=fovTest.update(aaNow,aaReady&&gameplayWorld&&aaContext==presentation::Mode::Gameplay&&calibratedFov>0,automaticFov);
  perf::fovComparison.store(fovTest.active,std::memory_order_relaxed);
#endif
 }
 enforceRenderControls(cvarBase,read,setCvar,[](const RenderControl& c,bool accepted,int before,bool verified,int after){
  log("ETERNAL_CONTROL name="+std::string(c.name)+" requested="+c.value+" before="+std::to_string(before)+" accepted="+std::to_string(accepted)+" verified="+std::to_string(verified)+" after="+std::to_string(after));
 },selectedFov);
#ifndef ARGENT_CLEAN_RELEASE
 int actualFov{};const bool fovKnown=readRenderControl(cvarBase,renderControls[0],read,actualFov);
 perf::engineFov.store(fovKnown?actualFov:0,std::memory_order_relaxed);perf::fovPhase.store(fovTest.phase,std::memory_order_relaxed);
 if(perf::enabled()&&(lastFovPhase!=fovTest.phase||aaNow-fovReport>=1000)){
  int actual{};const bool known=readRenderControl(cvarBase,renderControls[0],read,actual);
  std::lock_guard<std::mutex> lock(fovMutex);
  log("PERF_FOV_STATE serial="+std::to_string(perf::frame.load())+" phase="+std::to_string(fovTest.phase)+" pending="+std::to_string(fovTest.pending)+" automatic="+std::to_string(automaticFov)+" requested="+std::to_string(selectedFov)+" actual="+(known?std::to_string(actual):"unknown")+" verified="+std::to_string(known&&actual==selectedFov)+" tanX="+std::to_string(observedFovX)+" tanY="+std::to_string(observedFovY)+" ageMs="+std::to_string(aaNow>=observedFovTick?aaNow-observedFovTick:0)+" context="+presentation::name(aaContext));
  lastFovPhase=fovTest.phase;fovReport=aaNow;
 }
#endif
 // Launcher render scale is a fixed internal-resolution fraction. Dynamic
 // resolution stays off; the engine's scaling gate must remain enabled.
 static const float renderScale=[] {char value[32]{};GetEnvironmentVariableA("ARGENT_RENDER_SCALE",value,32);char* end{};float v=std::strtof(value,&end);return end!=value&&!*end&&std::isfinite(v)&&v>=.4f&&v<=2.f?v:1.f;}();
 const auto scaleText=std::to_string(renderScale);
 const RenderControl scales[]={{0x66e8ab0,"r_enableResolutionScale","1",1},{0x66ec860,"rs_enable","0",0},{0x66ec8e0,"rs_forceResolution",scaleText.c_str(),int(renderScale),true}};
 const bool dlssOwnsScale=aaKnown&&aa==2;
 static int lastLoggedAa=-1;static uint64_t lastAaReport{};
 if(extendedLogging()&&aaKnown&&(lastLoggedAa!=aa||aaNow-lastAaReport>=2000)){
  int safe{},blur{};
  const bool safeKnown=readRenderControl(cvarBase,RenderControl{0x66de720,"r_TAASafeMode","",0},read,safe);
  const bool blurKnown=readRenderControl(cvarBase,RenderControl{0x66deb80,"r_motionblur","",0},read,blur);
  log("ETERNAL_TEMPORAL_STATE aa="+std::to_string(aa)+" taaSafeMode="+(safeKnown?std::to_string(safe):"unknown")+
      " motionBlur="+(blurKnown?std::to_string(blur):"unknown")+" requestedAa="+std::to_string(desiredAa)+" verified="+std::to_string(desiredAa<0||aa==desiredAa)+" nativeTaaBlocked=1 dlssSelected="+std::to_string(aa==2)+" readOnly=1");
  lastLoggedAa=aa;lastAaReport=aaNow;
 }
 static int scaleOwner=-1;
 if(scaleOwner!=int(dlssOwnsScale)){log(std::string("ETERNAL_SCALE owner=")+(dlssOwnsScale?"DLSS":"launcher"));scaleOwner=int(dlssOwnsScale);}
 static bool scaleReported=false;
 if(!dlssOwnsScale)for(const auto& c:scales){int value{};float fraction{};const bool known=readRenderControl(cvarBase,c,read,value,c.floating?&fraction:nullptr);
  const bool same=known&&(c.floating?std::abs(fraction-renderScale)<.0001f:value==c.integer);
  if(known&&!same)setCvar(reinterpret_cast<void*>(cvarBase+build::rva(c.rva)),c.value,true);
  if(!scaleReported||!same){const bool verified=readRenderControl(cvarBase,c,read,value,c.floating?&fraction:nullptr);log("ETERNAL_SCALE name="+std::string(c.name)+" requested="+c.value+" read="+std::to_string(verified)+" value="+std::to_string(c.floating?fraction:float(value)));}
 }
 scaleReported=true;
 const bool ok=lightCulling.update(world,[&](int& value){return readRenderControl(cvarBase,lightCullingControl,read,value);},[&](int value){
  const auto text=std::to_string(value);return setCvar(reinterpret_cast<void*>(cvarBase+build::rva(lightCullingControl.rva)),text.c_str(),false);
 });
 if(transition||!ok){int value{};const bool verified=readRenderControl(cvarBase,lightCullingControl,read,value);
  log("ETERNAL_LIGHT_CULL world="+std::to_string(world)+" success="+std::to_string(ok)+" verified="+std::to_string(verified)+" value="+std::to_string(value)+" restore="+std::to_string(lightCulling.restore));
 }
 busy=false;
}
constexpr unsigned char signature[]={0xf2,0x0f,0x10,0x02,0xf2,0x0f,0x11,0x81,0x24,0x01,0x00,0x00,0x8b,0x42,0x08,0x89,0x81,0x2c,0x01,0x00,0x00,0x41,0x0f,0x10,0x00,0x0f,0x11,0x81,0x30,0x01,0x00,0x00,0x41,0x0f,0x10,0x48,0x10,0x0f,0x11,0x89,0x40,0x01,0x00,0x00,0x41,0x8b,0x40,0x20,0x89,0x81,0x50,0x01,0x00,0x00,0xc3};
bool probeStoreImage(){
 auto base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
 if(!base)return false;
 const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>4096)return false;
 const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.TimeDateStamp!=0x69bc663d||nt->OptionalHeader.SizeOfImage!=0x74f1000)return false;
 constexpr unsigned char setter[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20};
 constexpr unsigned char width[]={0x8b,0x05,0x8e,0x0e,0xd1,0x01,0xc3,0xcc};
 constexpr unsigned char height[]={0x8b,0x05,0xa2,0x0e,0xd1,0x01,0xc3,0xcc};
 return !std::memcmp(base+0x376530,setter,sizeof(setter))&&
        !std::memcmp(base+0x1d4be60,width,sizeof(width))&&
        !std::memcmp(base+0x1d4be50,height,sizeof(height));
}
bool approvedStoreImage(){static const bool approved=[](){
 if(!probeStoreImage())return false;
 const auto image=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
 if(const auto failed=build::validateStoreCode(image)){log("ETERNAL_BUILD refused: Store contract RVA="+std::to_string(failed));return false;}
 build::microsoftStore=true;
 log("ETERNAL_BUILD profile=microsoft-store-20260319 native-contracts="+std::to_string(build::storeCodeCount)+" verified=1");return true;
 }();return approved;}
bool approvedImage(){
 if(approvedStoreImage())return true;
 wchar_t path[32768]{};if(!GetModuleFileNameW(nullptr,path,32768))return false;
 std::ifstream file(std::filesystem::path(path),std::ios::binary);if(!file)return false;
 BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
 bool ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
 char buffer[65536];while(ok&&file){file.read(buffer,sizeof(buffer));if(file.gcount())ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer),ULONG(file.gcount()),0)>=0;}
 unsigned char digest[32]{};ok=ok&&!file.bad()&&BCryptFinishHash(hash,digest,32,0)>=0;
 if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
 constexpr unsigned char expected[]={0x69,0xdc,0x13,0xe8,0x8d,0x1c,0x19,0x13,0x3e,0xad,0x79,0x50,0xdc,0x64,0xeb,0xcb,0xd4,0xa5,0xa3,0xf6,0xbd,0x6f,0x9c,0x33,0x6e,0xbf,0xfe,0x56,0xdf,0x6a,0x1c,0x11};
 return ok&&!std::memcmp(digest,expected,32);
}
void __fastcall cameraSetter(void* object,const float* position,const float* basis){
 maintainRenderControls();
 const bool authored=presentation::refreshAnimationCamera();
 // Monkey bars retain native physics/launch velocity, but do not borrow the
 // authored first-person camera joint. Keep the normal upright VR view.
 const bool monkeyView=presentation::monkeyBarAnimation.load()&&!presentation::syncAttack.load()&&!presentation::interactionAnimation.load()&&!presentation::meatHookAnimation.load();
 const bool sync=authored&&!monkeyView;
 ++calls;Basis transformed;std::array<float,3> moved{};bool use=false,usePosition=false;
 {std::lock_guard<std::mutex> lock(poseMutex);
  synchronizeControlledActor();
  if(active&&GetTickCount64()-updated<250&&position&&basis){
   Basis input;std::memcpy(input.data(),basis,sizeof(input));
   std::array<float,3> stable{position[0],position[1],position[2]};
   if(validBasis(input)){
    if(needReference){reference=yawQuaternion(yawDegrees(current));yawFollow={};needReference=false;historyCursor=0;matchedSerial=0;follow={};anchorOwner=0;}
    const float horizontal=std::hypot(input[0],input[1]);
    if(sync){
     if(!animationActive){
      animationBasis=bodyValid?bodyBasis:Basis{1,0,0,0,1,0,0,0,1};
      if(!bodyValid&&horizontal>.05f){const float x=input[0]/horizontal,y=input[1]/horizontal;animationBasis={x,y,0,-y,x,0,0,0,1};}
      animationReference=yawFollow.reference(reference);animationActive=true;
      anchorOwner=0;follow.previousValid=false;follow.commanded=false;yawFollow.command=0;
      input::followStick({});input::followTurn(0);
      log("ETERNAL_KILL_CAMERA active=1 position=native-animation rotation=held-body-plus-head translation=off");
     }
     // KHARVOX animation-camera contract: native authored position, a level
     // gameplay basis held at entry, free HMD look, no physical translation.
     // Do not replace the persistent gameplay/weapon origin with an anim joint.
     use=rotateBasis(animationBasis,animationReference,current,transformed);
    }else{
    if(animationActive){
     animationActive=false;anchorOwner=0;needPositionReference=true;follow={};
     yawFollow.previousValid=false;yawFollow.command=0;
     log("ETERNAL_KILL_CAMERA active=0 anchor=rebase");
    }
    if(horizontal>.05f&&(!viewActor||!bodyValid)){
     // Native Revenant yaw already includes our absolute HMD direction. Keep
     // the entry basis and apply HMD + stick exactly once; never feed it back.
     if(!viewActor)yawFollow.observe(std::atan2(input[1],input[0])*57.2957795131f);
     const float x=input[0]/horizontal,y=input[1]/horizontal;bodyBasis={x,y,0,-y,x,0,0,0,1};
    }
    if(presentation::scriptedMovement.load()&&!monkeyView)anchorOwner=0;
    else if(physicsOwner&&GetTickCount64()-physicsTick<100){
     if(anchorOwner!=physicsOwner){anchorOwner=physicsOwner;anchorOffset={position[0]-physicsPosition.x,position[1]-physicsPosition.y,position[2]-physicsPosition.z};}
     stable={physicsPosition.x+anchorOffset.x,physicsPosition.y+anchorOffset.y,physicsPosition.z+anchorOffset.z};
     applyCrouchHeight(stable);
    }
    bodyOrigin=stable;bodyValid=true;
    // Use KHARVOX's gravity-level gameplay basis, not the native recoil/aim
    // pitch and roll. Only the subsequently applied HMD may tilt the view.
    Basis level;
    use=kharvox::makeStableGameplayBodyBasis(true,false,viewActor?bodyBasis.data():input.data(),bodyBasis.data(),level.data())&&
        rotateBasis(level,yawFollow.reference(reference),current,transformed);
    // Physics anchoring also suppresses native positional kicks when only
    // orientation tracking is available. It must not depend on translation.
    moved=stable;usePosition=true;
    if(translate){
     if(needPositionReference){referencePosition=currentPosition;needPositionReference=false;follow={};}
     auto origin=referencePosition;origin.x+=follow.accepted.x;origin.z+=follow.accepted.z;
     usePosition=translatePosition(stable.data(),bodyBasis,{yawFollow.reference(reference),origin},{current,currentPosition},unitsPerMeter(),moved);
    }
    }
    // Animation suppresses physical translation in the engine, not in the
    // submitted tracking metadata. Tag this frame with the sampled HMD pose
    // so the compositor cannot reintroduce the entire suppressed offset.
    if(use){
     hudOrigin=usePosition?moved:std::array<float,3>{position[0],position[1],position[2]};hudAxis=transformed;hudTick=GetTickCount64();
     // Publish the hand with this camera's body/reference transform, never
     // substitute a later physics pose while the HUD is being submitted.
     const auto sample=placedWorldHands.sample;const int off=sample.dominant==0?1:0;
     auto zero=referencePosition;zero.x+=follow.accepted.x;zero.z+=follow.accepted.z;
     hudLeftMode=sample.dominant==0;
     hudHandValid=!animationActive&&!needPositionReference&&input::fresh(sample,hudTick)&&sample.gripValid[off]&&
      rotateBasis(bodyBasis,yawFollow.reference(reference),sample.grip[off].orientation,hudHand)&&
      translatePosition(bodyOrigin.data(),bodyBasis,{yawFollow.reference(reference),zero},sample.grip[off],unitsPerMeter(),hudGrip);
     auto frameHands=placedWorldHands;
     frameHands.sample.active=frameHands.sample.active&&!animationActive;
     // The support hand and its HUD use exactly the same native placement.
     if(hudHandValid){frameHands.position[off]=hudGrip;frameHands.axes[off]=hudHand;}
     else frameHands.sample.gripValid[off]=false;
     auto& frame=history[historyCursor++%history.size()];
     frame={transformed,{current,(usePosition||(animationActive&&translate))?currentPosition:referencePosition},hudTick,hudOrigin,frameHands,++cameraId};
     frame.laser=placedWorldLaser; // Also support weapon update before camera update.
    }
   }
   if(!use)++rejected;
  }
 }
 {std::lock_guard<std::mutex> lock(poseMutex);animationCamera=use&&sync?object:nullptr;animationCameraTick=GetTickCount64();}
 if(use)++applied;
 original(object,usePosition?moved.data():position,use?transformed.data():basis);
}
bool activate(){
 HMODULE pinned{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&cameraSetter),&pinned))return false;
 auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
 auto created=MH_CreateHook(target,reinterpret_cast<void*>(&cameraSetter),reinterpret_cast<void**>(&original));
 if(created!=MH_OK){log("ETERNAL_HOOK create failed="+std::to_string(created));return false;}
 auto enabled=MH_EnableHook(target);if(enabled!=MH_OK){MH_RemoveHook(target);log("ETERNAL_HOOK enable failed="+std::to_string(enabled));return false;}
 installed=true;return true;
}
}
namespace {
uint32_t eyeWidth{},eyeHeight{};
using ClientRect=BOOL(WINAPI*)(HWND,LPRECT);
ClientRect nativeClientRect{};uintptr_t renderImageBase{};
using RefreshExtent=void(__fastcall*)();RefreshExtent nativeRefreshExtent{};
void __fastcall refreshRenderExtent(){
 // These are the engine's output dimensions, not the window CVars. Its own
 // refresh then derives scene/DLSS dimensions and preserves its AA policy.
 auto output=reinterpret_cast<uint32_t*>(renderImageBase+build::rva(0x39aabe4));
 output[0]=eyeWidth;output[1]=eyeHeight;
 nativeRefreshExtent();
}
BOOL WINAPI renderClientRect(HWND window,LPRECT rect){
 const auto caller=build::semanticRva(reinterpret_cast<uintptr_t>(_ReturnAddress())-renderImageBase);
 auto ok=nativeClientRect(window,rect);
 // Only the two audited Vulkan swapchain extent queries receive virtual size.
 // Window management, mouse coordinates and minimized windows retain real WSI.
 const bool swapchainCaller=caller==0x1d09134||caller==0x1d091ba;
 if(ok&&rect&&swapchainCaller&&rect->right>rect->left&&rect->bottom>rect->top){
  rect->right=rect->left+LONG(eyeWidth);rect->bottom=rect->top+LONG(eyeHeight);
 }
 return ok;
}
uint32_t __fastcall renderWidth(void*){return eyeWidth;}
uint32_t __fastcall renderHeight(void*){return eyeHeight;}
}
bool storeExecutable() noexcept {return approvedStoreImage();}
bool installRenderExtent(uint32_t width,uint32_t height) noexcept {try{
 if(eyeWidth)return eyeWidth==width&&eyeHeight==height;
 if(!width||!height||!approvedImage())return false;
 auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
 // Eternal output-resolution accessors; independent from native window size.
 // Same approach as KHARVOX IndependentEngineSize, with Eternal signatures.
 constexpr unsigned char w[]={0x8b,0x05,0x0e,0xb3,0xce,0x01,0xc3,0xcc};
 constexpr unsigned char h[]={0x8b,0x05,0x22,0xb3,0xce,0x01,0xc3,0xcc};
 const bool store=approvedStoreImage();
 auto wt=base+build::rva(0x1cbf8d0);auto ht=base+build::rva(0x1cbf8c0);
 constexpr unsigned char storeW[]={0x8b,0x05,0x8e,0x0e,0xd1,0x01,0xc3,0xcc};
 constexpr unsigned char storeH[]={0x8b,0x05,0xa2,0x0e,0xd1,0x01,0xc3,0xcc};
 if(store ? (std::memcmp(wt,storeW,sizeof(storeW))||std::memcmp(ht,storeH,sizeof(storeH)))
          : (std::memcmp(wt,w,sizeof(w))||std::memcmp(ht,h,sizeof(h))))return false;
 constexpr unsigned char rect1[]={0xff,0x15,0x14,0x2f,0xd1,0x00};
 constexpr unsigned char rect2[]={0xff,0x15,0x8e,0x2e,0xd1,0x00};
 constexpr unsigned char storeRect1[]={0xff,0x15,0x34,0x5c,0xd2,0x00};
 constexpr unsigned char storeRect2[]={0xff,0x15,0xae,0x5b,0xd2,0x00};
 if(store ? (std::memcmp(base+0x1d9568e,storeRect1,6)||std::memcmp(base+0x1d95714,storeRect2,6))
          : (std::memcmp(base+0x1d0912e,rect1,6)||std::memcmp(base+0x1d091b4,rect2,6)))return false;
 constexpr unsigned char refresh[]={0x48,0x83,0xec,0x28,0x80,0x3d,0xe7,0xf1,0x9b,0x04,0x01,0x75,0x16};
 constexpr unsigned char storeRefresh[]={0x48,0x83,0xec,0x28,0x80,0x3d,0x57,0xa6,0x9e,0x04,0x01,0x75,0x16};
 auto rt=base+build::rva(0x1cbfa60);
 if(store?std::memcmp(rt,storeRefresh,sizeof(storeRefresh)):std::memcmp(rt,refresh,sizeof(refresh)))return false;
 auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
 if(MH_CreateHook(wt,reinterpret_cast<void*>(&renderWidth),nullptr)!=MH_OK)return false;
 if(MH_CreateHook(ht,reinterpret_cast<void*>(&renderHeight),nullptr)!=MH_OK){MH_RemoveHook(wt);return false;}
 if(MH_CreateHook(rt,reinterpret_cast<void*>(&refreshRenderExtent),reinterpret_cast<void**>(&nativeRefreshExtent))!=MH_OK){MH_RemoveHook(wt);MH_RemoveHook(ht);return false;}
 eyeWidth=width;eyeHeight=height;
 renderImageBase=reinterpret_cast<uintptr_t>(base);
 if(MH_EnableHook(wt)==MH_OK&&MH_EnableHook(ht)==MH_OK&&MH_EnableHook(rt)==MH_OK){
  auto slot=reinterpret_cast<void**>(base+build::rva(0x2a1c048));DWORD protection{};
  if(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protection)){
   nativeClientRect=reinterpret_cast<ClientRect>(*slot);
   InterlockedExchangePointer(slot,reinterpret_cast<void*>(&renderClientRect));DWORD ignored{};VirtualProtect(slot,sizeof(void*),protection,&ignored);
   refreshRenderExtent();
   log("ETERNAL_RENDER_EXTENT "+std::to_string(width)+"x"+std::to_string(height)+" engineAccessors=1 swapchainClientQueries=2 sceneRefresh=1");return true;
  }
 }
 MH_DisableHook(wt);MH_DisableHook(ht);MH_DisableHook(rt);MH_RemoveHook(wt);MH_RemoveHook(ht);MH_RemoveHook(rt);eyeWidth=eyeHeight=0;return false;
 }catch(...){return false;}}
bool install() noexcept {try{
 if(installed)return true;
 if(!approvedImage()){log("ETERNAL_HOOK refused: unsupported executable hash");return false;}
 auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
 auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
 auto section=IMAGE_FIRST_SECTION(nt);unsigned matches=0;
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)if(section[i].Characteristics&IMAGE_SCN_MEM_EXECUTE){
  auto start=base+section[i].VirtualAddress;auto size=section[i].Misc.VirtualSize;
  for(size_t j=0;j+sizeof(signature)<=size;++j)if(start[j]==signature[0]&&!std::memcmp(start+j,signature,sizeof(signature))){target=start+j;++matches;}
 }
 if(matches!=1){target=nullptr;log("ETERNAL_HOOK refused: camera setter signature not unique");return false;}
 // Full executable hash above plus the complete setter prologue. Its audited
 // contract is bool(wrapper, string, force); the third argument skips flags,
 // but retains engine-owned storage, conversion and notifications.
 constexpr unsigned char setterBytes[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x09,0x41,0x0f,0xb6,0xf0,0x48,0x8b,0xfa,0x48,0x85,0xd2,0x75};
 const bool store=approvedStoreImage();
 if(std::memcmp(base+build::rva(0x376020),setterBytes,sizeof(setterBytes))){log("ETERNAL_CONTROL refused: setter signature mismatch");return false;}
 cvarBase=reinterpret_cast<uintptr_t>(base);setCvar=reinterpret_cast<CvarSetter>(base+build::rva(0x376020));
 wchar_t logPath[32768]{};const auto length=GetEnvironmentVariableW(L"ARGENT_LOG",logPath,32768);
 if(length&&length<32768)lightCullingTestMarker=std::filesystem::path(logPath).parent_path()/L"test-light-gpu-culling";
 if(!activate())return false;
 // Verified in both approved binaries: finalizer calls the authored camera
 // preparation (which can rewrite FOV), then returns before scene submission.
 constexpr unsigned char finalizeBytes[]={0x48,0x8b,0xc4,0x57,0x48,0x83,0xec,0x70,0x48,0x89,0x58,0x08,0x48,0x8b,0xf9,0x48,0x89,0x70,0x18};
 auto finalizeTarget=base+build::rva(0x1481d30);
 bool fovGuardOk=false;
 if(!std::memcmp(finalizeTarget,finalizeBytes,sizeof(finalizeBytes))&&
    MH_CreateHook(finalizeTarget,reinterpret_cast<void*>(&finalizeCamera),reinterpret_cast<void**>(&originalFinalizeCamera))==MH_OK){
  fovGuardOk=MH_EnableHook(finalizeTarget)==MH_OK;
  if(!fovGuardOk)MH_RemoveHook(finalizeTarget);
 }
 log("ETERNAL_ANIMATION_FOV installed="+std::to_string(fovGuardOk));
#ifndef ARGENT_CAMERA_TESTING
 const bool playerOk=player::install(base),presentationOk=presentation::install(base);
 if(!revenant::install(base))log("ETERNAL_REVENANT unavailable: native contract rejected");
 const bool hudOk=hud::installWeaponWheel(base),hitOk=hud::installHitMarkerSuppression(base);
 log("ETERNAL_BUILD_HOOKS player="+std::to_string(playerOk)+" presentation="+std::to_string(presentationOk)+" hud="+std::to_string(hudOk)+" hitmarkers="+std::to_string(hitOk));
#endif
 // Both functions are covered by approvedImage; also check full entry bytes.
 constexpr unsigned char pollBytes[]={0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x40};
 constexpr unsigned char discoverBytes[]={0x80,0xb9,0xfc,0x01,0x00,0x00,0x00,0x75,0x07,0xc6,0x81,0xfd,0x01,0x00,0x00,0x01};
 if(!std::memcmp(base+build::rva(0x1dc57a0),pollBytes,sizeof(pollBytes))&&!std::memcmp(base+build::rva(0x1dc48f0),discoverBytes,sizeof(discoverBytes))){
  discover=reinterpret_cast<Discover>(base+build::rva(0x1dc48f0));
  const auto result=MH_CreateHook(base+build::rva(0x1dc57a0),reinterpret_cast<void*>(&pollController),reinterpret_cast<void**>(&originalPoll));
  const bool enabled=result==MH_OK&&MH_EnableHook(base+build::rva(0x1dc57a0))==MH_OK;
  log("ETERNAL_INPUT_DISCOVERY installed="+std::to_string(enabled));
 }
 log("ETERNAL_HOOK installed verified camera setter; 6DoF view, unitsPerMeter="+std::to_string(unitsPerMeter())+"; dormant in quad; physics body unchanged");return true;
 }catch(...){return false;}}
void stop() noexcept {
 {std::lock_guard<std::mutex> lock(poseMutex);++weaponEpoch;input::weaponApplied({});placedWorldHands={};active=false;needReference=true;needPositionReference=true;translate=false;historyCursor=0;matchedSerial=0;follow={};bodyValid=false;anchorOwner=0;animationActive=false;input::followStick({});input::followTurn(0);yawFollow={};}
 // Keep the trampoline valid until process exit: a game thread may already
 // be inside the detour. The layer remains loaded for the process lifetime.
}
uint64_t weaponContext() noexcept {std::lock_guard<std::mutex> lock(poseMutex);return !viewActor&&active&&!animationActive&&!presentation::syncAttack.load()&&!needReference?weaponEpoch:0;}
void update(XrQuaternionf head,bool world,bool recenter) noexcept {
 std::lock_guard<std::mutex> lock(poseMutex);
 translate=false;needPositionReference=true;
 if(!world||!validQuaternion(head)){++weaponEpoch;active=false;needReference=true;return;}
 if(!active||recenter)needReference=true;
 current=head;updated=GetTickCount64();active=installed;
}
float unitsPerMeter() noexcept {
 // Same meter-scale baseline as the validated Eternal stereo path. DOOM 2016's
 // 39.3701 factor must not be copied into Eternal. Physical scale needs VR QA.
 static const float scale=[] {char text[64]{};const auto n=GetEnvironmentVariableA("ARGENT_WORLD_SCALE",text,64);
  if(!n||n>=64)return 1.f;char* end{};const float v=std::strtof(text,&end);
  return end&&!*end&&std::isfinite(v)&&v>=.01f&&v<=100.f?v:1.f;}();return scale;
}
void updatePose(XrPosef head,bool world,bool recenter,bool positionTracked) noexcept {
 const auto controls=input::snapshot();
 const bool climbing=presentation::attachedWallClimb();
 const bool gameplay=presentation::gameplayInput.load()&&!presentation::syncAttack.load();
 std::lock_guard<std::mutex> lock(poseMutex);
 synchronizeControlledActor();
 if(!world||!validQuaternion(head.orientation)){++weaponEpoch;active=false;needReference=true;needPositionReference=true;translate=false;input::followStick({});input::headMovement(0);input::followTurn(0);yawFollow={};return;}
 if(!active||recenter){++weaponEpoch;needReference=true;needPositionReference=true;if(viewActor)bodyValid=false;}
 if(!positionTracked||!validPosition(head.position)){translate=false;needPositionReference=true;}
 else {translate=true;currentPosition=head.position;}
 current=head.orientation;updated=GetTickCount64();active=installed;
 yawFollow.snap(controls.pad.sThumbRX/32767.f,controls.snapDegrees,gameplay&&active&&!needReference&&controls.snapTurn&&input::fresh(controls,GetTickCount64())&&!(controls.pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER));
 yawFollow.smooth(controls.pad.sThumbRX/32767.f,controls.smoothSpeed,gameplay&&active&&!needReference&&controls.managedTurn&&!controls.snapTurn&&input::fresh(controls,GetTickCount64())&&!(controls.pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER),GetTickCount64());
 Basis movementBasis{};
 float movementYaw=0;
 if(active&&!needReference&&rotateBasis(Basis{1,0,0,0,1,0,0,0,1},yawFollow.reference(reference),current,movementBasis)&&std::hypot(movementBasis[0],movementBasis[1])>.05f)
  movementYaw=std::atan2(movementBasis[1],movementBasis[0])*57.2957795131f;
 float moveDirection=movementYaw;
 if(controls.offhandMovement&&controls.aimValid[1-controls.dominant]){
  Basis offhand{};if(rotateBasis(Basis{1,0,0,0,1,0,0,0,1},yawFollow.reference(reference),controls.aim[1-controls.dominant].orientation,offhand)&&std::hypot(offhand[0],offhand[1])>.2f)moveDirection=std::atan2(offhand[1],offhand[0])*57.2957795131f;
 }
 // Wall movement stays in the native wall frame. Synthetic body-yaw chasing
 // fights the climbing camera's angle limits; roomscale must not become an
 // unintended climb-stick command. HMD and managed visual turning remain free.
 input::headMovement(gameplay&&!viewActor&&!climbing?moveDirection:0);
 input::followTurn(yawFollow.request(movementYaw,(!controls.snapTurn&&!controls.managedTurn&&std::abs(int(controls.pad.sThumbRX))>4915)||(controls.pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER),!viewActor&&!climbing&&gameplay&&active&&!needReference&&bodyValid&&GetTickCount64()-physicsTick<100&&input::fresh(controls,GetTickCount64())));
 static unsigned bodyReport{};if(extendedLogging()&&++bodyReport%300==0)log("ETERNAL_BODY_YAW accepted="+std::to_string(yawFollow.accepted)+" residual="+std::to_string(movementYaw)+" command="+std::to_string(yawFollow.command));
 const bool ready=!climbing&&gameplay&&active&&translate&&!needReference&&!needPositionReference&&bodyValid&&GetTickCount64()-physicsTick<100&&input::fresh(controls,GetTickCount64())&&(!viewActor||controls.revenantActor==viewActor);
 const bool manual=std::abs(int(controls.pad.sThumbLX))>4915||std::abs(int(controls.pad.sThumbLY))>4915;
 auto room=follow.update(physicsOwner,physicsPosition,head.position,referencePosition,bodyBasis,yawFollow.reference(reference),unitsPerMeter(),manual,ready);
 if(viewActor){
  // Follow measures engine displacement in the held entry/body frame. The
  // Revenant's native movement already uses the commanded HMD heading, so
  // express only the synthetic stick in that frame. Keep acceptance in world
  // space; rotating it too would consume the wrong displacement after turns.
  const auto local=kharvox::rotateMovementStickForDirection({room.x,room.y},-movementYaw);
  room={local.x,local.y};
 }
 input::followStick(room);
}
void publishPhysics(uintptr_t owner,XrVector3f position) noexcept {
 if(!owner||!validPosition(position))return;std::lock_guard<std::mutex> lock(poseMutex);
 synchronizeControlledActor();
 if(viewActor&&owner!=viewActor)return;
 physicsOwner=owner;physicsPosition=position;physicsTick=GetTickCount64();
}
bool revenantAim(uintptr_t actor,float* axis) noexcept {
 std::lock_guard<std::mutex> lock(poseMutex);
 if(!axis||!actor||actor!=viewActor||actor!=revenant::actor.load()||!active||!bodyValid||needReference||GetTickCount64()-updated>=100)return false;
 Basis aim{};if(!rotateBasis(bodyBasis,yawFollow.reference(reference),current,aim))return false;
 std::memcpy(axis,aim.data(),sizeof(aim));return true;
}
bool controllerPlacement(XrPosef hand,float* origin,float* axis,const input::Snapshot* rendered) noexcept {
 if(!origin||!axis)return false;std::lock_guard<std::mutex> lock(poseMutex);
 if(viewActor||!active||animationActive||presentation::nativeAnimation.load()||presentation::syncAttack.load()||!bodyValid||needReference||needPositionReference||GetTickCount64()-updated>=100)return false;
 auto zero=referencePosition;zero.x+=follow.accepted.x;zero.z+=follow.accepted.z;
 Basis desired;std::array<float,3> position;
 auto liveOrigin=bodyOrigin;
 if(anchorOwner==physicsOwner&&physicsOwner&&GetTickCount64()-physicsTick<100)liveOrigin={physicsPosition.x+anchorOffset.x,physicsPosition.y+anchorOffset.y,physicsPosition.z+anchorOffset.z};
 if(anchorOwner==physicsOwner&&physicsOwner&&GetTickCount64()-physicsTick<100)applyCrouchHeight(liveOrigin);
 if(!rotateBasis(bodyBasis,yawFollow.reference(reference),hand.orientation,desired)||!translatePosition(liveOrigin.data(),bodyBasis,{yawFollow.reference(reference),zero},hand,unitsPerMeter(),position))return false;
 if(rendered){
  WorldHands next;next.sample=*rendered;
  for(int i=0;i<2;++i)next.sample.gripValid[i]=rendered->gripValid[i]&&
   rotateBasis(bodyBasis,yawFollow.reference(reference),rendered->grip[i].orientation,next.axes[i])&&
   translatePosition(liveOrigin.data(),bodyBasis,{yawFollow.reference(reference),zero},rendered->grip[i],unitsPerMeter(),next.position[i]);
  placedWorldHands=next;
 }
 std::memcpy(origin,position.data(),sizeof(position));std::memcpy(axis,desired.data(),sizeof(desired));return true;
}
bool hudProjection(float& tanX,float& tanY) noexcept {
 std::lock_guard<std::mutex> lock(fovMutex);
 if(GetTickCount64()-observedFovTick>=500||observedFovX<=0||observedFovY<=0)return false;
 tanX=observedFovX;tanY=observedFovY;return true;
}
bool hudCamera(float* origin,float* axis,uint64_t* frame) noexcept {
 if(!origin||!axis)return false;std::lock_guard<std::mutex> lock(poseMutex);
 if(!active||needReference||animationActive||!presentation::gameplayInput.load()||presentation::syncAttack.load()||GetTickCount64()-hudTick>=100)return false;
 std::memcpy(origin,hudOrigin.data(),sizeof(hudOrigin));std::memcpy(axis,hudAxis.data(),sizeof(hudAxis));if(frame)*frame=cameraId;return true;
}
bool wallClimbView(float* axis) noexcept {
 if(!axis)return false;std::lock_guard<std::mutex> lock(poseMutex);
 // Wall-hold can use an authored camera. Use its actual rendered HMD basis,
 // including managed turning, without loosening the HUD/weapon animation gate.
 if(!active||needReference||GetTickCount64()-updated>=100||GetTickCount64()-hudTick>=100||
    !(presentation::worldPresentation.load()||presentation::gameplayInput.load())||
    presentation::syncAttack.load()||!validBasis(hudAxis))return false;
 std::memcpy(axis,hudAxis.data(),sizeof(hudAxis));return true;
}
bool monkeyBarPose(float* origin,float* axis,float* headOffset) noexcept {
 if(!origin||!axis)return false;std::lock_guard<std::mutex> lock(poseMutex);
 if(!active||needReference||GetTickCount64()-updated>=100||GetTickCount64()-hudTick>=100||
    !(presentation::worldPresentation.load()||presentation::gameplayInput.load())||
    presentation::syncAttack.load()||!validBasis(hudAxis))return false;
 for(float component:hudOrigin)if(!std::isfinite(component))return false;
 if(headOffset){
  if(animationActive||!bodyValid)return false;
  for(int i=0;i<3;++i){headOffset[i]=hudOrigin[i]-bodyOrigin[i];if(!std::isfinite(headOffset[i]))return false;}
 }
 std::memcpy(origin,hudOrigin.data(),sizeof(hudOrigin));std::memcpy(axis,hudAxis.data(),sizeof(hudAxis));return true;
}
bool swimmingView(float* axis) noexcept {
 if(!axis)return false;std::lock_guard<std::mutex> lock(poseMutex);
 const auto now=GetTickCount64();
 if(viewActor||!active||needReference||animationActive||!presentation::gameplayInput.load()||
    now-updated>=100||now-hudTick>=100||!validBasis(hudAxis))return false;
 std::memcpy(axis,hudAxis.data(),sizeof(hudAxis));return true;
}
void publishHudPanels(uint64_t frame,int role,const kharvox::hands::HandHudPanels& panels){
 if(!frame||role<0||role>=16||panels.size()>4096)return;
 std::lock_guard<std::mutex> lock(poseMutex);
 for(auto& h:history)if(h.id==frame){h.panels[role]=panels;return;}
}
kharvox::hands::HandHudPanels renderedHudPanels(uint64_t serial){
 kharvox::hands::HandHudPanels result;std::lock_guard<std::mutex> lock(poseMutex);
 if(!serial||serial!=matchedSerial||!active||presentation::hideGameplayHud.load())return result;
 for(const auto& h:history)if(h.id==matchedCameraId){
  for(const auto& panels:h.panels)for(const auto& panel:panels){
   if(result.size()>=4096)return {};
   kharvox::hands::HandHudQuad transformed{};
   for(int i=0;i<4;++i){const auto p=worldPointInTracking(panel[i],h.origin,h.basis,h.head,unitsPerMeter());transformed[i]={p.x,p.y,p.z};}
   result.push_back(transformed);
  }
  break;
 }
 return result;
}
Stats stats() noexcept{return {calls.load(),applied.load(),rejected.load(),installed.load()};}
kharvox::hands::HandHudPanels renderedHudPlaceholder(uint64_t serial){
 const int selected=hud::calibrationPlaceholderRole();if(selected<0)return {};
 auto note=[&](const char* status,size_t count=0){
  static std::atomic<ULONGLONG> noted{};const auto now=GetTickCount64();auto previous=noted.load();
  if(now-previous>2000&&noted.compare_exchange_strong(previous,now))
   log("ETERNAL_HUD_PLACEHOLDER selected="+std::string(hud::roles[selected].name)+" status="+status+" quads="+std::to_string(count));
 };
 AppliedCamera snapshot;bool found=false;
 {std::lock_guard<std::mutex> lock(poseMutex);
  if(!serial||serial!=matchedSerial||!active||animationActive||presentation::hideGameplayHud.load()||
   !presentation::gameplayInput.load()||presentation::syncAttack.load()||presentation::nativeAnimation.load()){note("camera-or-presentation-unavailable");return {};}
  for(const auto& h:history)if(h.id==matchedCameraId){
   snapshot=h;found=true;break;
  }
 }
 if(!found){note("history-unavailable");return {};}
 const auto& hands=snapshot.hands;const int off=hands.sample.dominant==0?1:0;
 if(!hands.sample.active||!hands.sample.gripValid[off]){note("support-pose-unavailable");return {};}
 kharvox::hands::HandHudPanels panels;
 for(int role=0;role<int(hud::roles.size());++role){
  if(!hud::showCalibrationPlaceholder(true,selected,role,!snapshot.panels[role].empty()))continue;
  auto next=hud::calibrationPlaceholder(role,hands.sample.dominant==0,hands.position[off].data(),hands.axes[off].data(),unitsPerMeter(),snapshot.origin.data());
  panels.insert(panels.end(),next.begin(),next.end());
 }
 for(auto& panel:panels)for(auto& corner:panel){const auto p=worldPointInTracking(corner,snapshot.origin,snapshot.basis,snapshot.head,unitsPerMeter());corner={p.x,p.y,p.z};}
 note("generated",panels.size());return panels;
}
bool hudOffhand(float* origin,float* axis,bool& leftMode) noexcept {
 if(!origin||!axis)return false;std::lock_guard<std::mutex> lock(poseMutex);
 if(!active||needReference||!hudHandValid||animationActive||!presentation::gameplayInput.load()||presentation::syncAttack.load()||GetTickCount64()-hudTick>=100)return false;
 std::memcpy(origin,hudGrip.data(),sizeof(hudGrip));std::memcpy(axis,hudHand.data(),sizeof(hudHand));leftMode=hudLeftMode;return true;
}
void beginRender(uint64_t serial) noexcept {std::lock_guard<std::mutex> lock(poseMutex);renderSerial=serial;matchedSerial=0;}
void observeRenderedCamera(const void* common,size_t bytes) noexcept {
 if(!common||bytes<108)return;
 float v[27];std::memcpy(v,common,sizeof(v));
 const float nx=std::sqrt(v[21]*v[21]+v[22]*v[22]+v[23]*v[23]);
 const float ny=std::sqrt(v[24]*v[24]+v[25]*v[25]+v[26]*v[26]);
 if(!std::isfinite(nx+ny)||nx<.1f||ny<.1f)return;
 std::lock_guard<std::mutex> lock(poseMutex);if(!active||!renderSerial||matchedSerial==renderSerial)return;
 constexpr float matchTolerance=1e-6f;const AppliedCamera* match=nullptr;const auto now=GetTickCount64();
 // The compact frame UBO stores camera origin immediately after its three
 // world-ray vectors (floats 18..26). Orientation alone is ambiguous when
 // simulation advances while a previous translated view is submitted.
 std::array<float,3> drawOrigin{};const bool hasDrawOrigin=bytes>=120;
 if(hasDrawOrigin){std::memcpy(drawOrigin.data(),static_cast<const unsigned char*>(common)+108,sizeof(drawOrigin));if(!validPosition({drawOrigin[0],drawOrigin[1],drawOrigin[2]}))return;}
 float bestPositionError=hasDrawOrigin?.002f*unitsPerMeter():0.f;
 bestPositionError*=bestPositionError;
 // Match the world-space ray axes actually uploaded for this render, rather
 // than guessing whether the engine camera ran before or after acquire.
 for(size_t j=0;j<std::min(historyCursor,history.size());++j){const auto& h=history[(historyCursor-1-j)%history.size()];
  if(now-h.tick>=250)continue;
  float dx=0,dy=0;for(int k=0;k<3;++k){dx+=v[21+k]/nx*h.basis[3+k];dy+=v[24+k]/ny*h.basis[6+k];}
  const float error=std::abs(1-std::abs(dx))+std::abs(1-std::abs(dy));
  // History is newest first. Within the existing angular tolerance, tiny
  // floating-point differences must not select an older hand/weapon frame.
  // In particular, vertical hand motion cannot disambiguate camera angles.
  if(error<matchTolerance){
   if(!hasDrawOrigin){match=&h;break;}
   float positionError=0;for(int k=0;k<3;++k){const float delta=h.origin[k]-drawOrigin[k];positionError+=delta*delta;}
   // Newest first: retain the newest pose on equal camera positions.
   if(positionError<=bestPositionError&&(!match||positionError<bestPositionError)){match=&h;bestPositionError=positionError;}
  }
 }
 if(match){matchedHead=match->head;matchedSerial=renderSerial;matchedCameraId=match->id;matchedHands=match->hands.sample;matchedDepthA=v[2];matchedDepthB=v[3];
  matchedWorldLaser=match->laser;
  // Use the same rendered-camera transform as hands and native geometry.
  // Inverting the placement transform cancels real camera/weapon offsets
  // during walking and turning, making the beam slide relative to the mesh.
  matchedWorldLaser.valid=matchedWorldLaser.valid&&worldPoseInTracking(
   match->laser.origin,match->laser.axis,match->origin,match->basis,match->head,unitsPerMeter(),matchedLaserPose);
  for(int i=0;i<2;++i)matchedHands.gripValid[i]=matchedHands.gripValid[i]&&worldPoseInTracking(
   match->hands.position[i],match->hands.axes[i],match->origin,match->basis,match->head,unitsPerMeter(),matchedHands.grip[i]);
  std::lock_guard<std::mutex> fovLock(fovMutex);observedFovX=nx*.5f;observedFovY=ny*.5f;observedFovTick=now;
 }
}
void updateHeadsetFov(const XrPosef& head,const XrView* views,uint32_t count) noexcept {
 if(!views||count!=2)return;float x{},y{};
 if(!headsetEnvelope(head,{views[0],views[1]},x,y))return;
 std::lock_guard<std::mutex> lock(fovMutex);requiredFovX=x;requiredFovY=y;
}
bool renderedHead(uint64_t serial,XrPosef& pose) noexcept {std::lock_guard<std::mutex> lock(poseMutex);if(!serial||serial!=matchedSerial||!active)return false;pose=matchedHead;return true;}
bool renderedHands(uint64_t serial,input::Snapshot& hands,float& depthA,float& depthB) noexcept {
 std::lock_guard<std::mutex> lock(poseMutex);if(!serial||serial!=matchedSerial||!active)return false;
 hands=matchedHands;depthA=matchedDepthA;depthB=matchedDepthB;return hands.active;
}
void publishLaser(const float* origin,const float* axis,const char* profile) noexcept {
 std::lock_guard<std::mutex> lock(poseMutex);
 placedWorldLaser={};auto& laser=placedWorldLaser;
 if(origin&&axis&&profile&&active&&!animationActive){
  std::memcpy(laser.origin.data(),origin,sizeof(laser.origin));
  std::memcpy(laser.axis.data(),axis,sizeof(laser.axis));
  strncpy_s(laser.profile.data(),laser.profile.size(),profile,_TRUNCATE);
  laser.epoch=weaponEpoch;laser.tick=GetTickCount64();
  laser.valid=validBasis(laser.axis)&&validPosition({origin[0],origin[1],origin[2]});

 }
 // Native updates may follow the camera callback. Never alter an already
 // matched render snapshot: observeRenderedCamera freezes its own copy.
 for(auto& h:history)if(h.id==cameraId&&h.id){h.laser=laser;break;}
}
bool renderedLaser(uint64_t serial,const char* profile,XrPosef& pose) noexcept {
 std::lock_guard<std::mutex> lock(poseMutex);
 if(!serial||serial!=matchedSerial||!active||animationActive||!profile)return false;
 const auto& laser=matchedWorldLaser;
 if(!laser.valid||laser.epoch!=weaponEpoch||GetTickCount64()-laser.tick>=100||std::strcmp(profile,laser.profile.data()))return false;
 pose=matchedLaserPose;return true;
}
#ifdef ARGENT_CAMERA_TESTING
// Only built into the isolated native-hook test executable, never ArgentLayer.
void finalizeFixture(void* object,FinalizeCamera native){originalFinalizeCamera=native;finalizeCamera(object);}
void* installFixture(){
 auto memory=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!memory)return nullptr;
 std::memcpy(memory,signature,sizeof(signature));DWORD old{};
 if(!VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old))return nullptr;
 FlushInstructionCache(GetCurrentProcess(),memory,sizeof(signature));target=memory;
 return activate()?memory:nullptr;
}
#endif
}

