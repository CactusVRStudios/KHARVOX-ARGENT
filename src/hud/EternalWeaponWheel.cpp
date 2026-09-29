#include "../EternalBuildProfile.h"
#include "EternalWeaponWheel.h"
#include "WeaponWheelHudPolicy.h"
#include "OffhandHudPolicy.h"
#include "HudLayoutPolicy.h"
#include "OffhandLayout.h"
#include "HandHudGroup.h"
#include "HudGeometryPivot.h"
#include "HudHandMaskGeometry.h"
#include "HudDrawScope.h"
#include "HudCalibrationPreview.h"
#include "HudPlaceholder.h"
#include "CompassCalibration.h"
#include "DistanceMarkerPolicy.h"
#include "FlatMenuPolicy.h"
#include "TutorialBindingHook.h"
#include "../EternalCameraHook.h"
#include "../EternalPresentation.h"
#include "../Diagnostics.h"
#include "../QuadRuntime.h"
#include <MinHook.h>
#include <windows.h>
#include <intrin.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
namespace argent::hud { namespace {
using Update=void(__fastcall*)(void*,const float*,const float*,void*,void*);
using Render=void(__fastcall*)(void*,void*,void*,bool,bool,bool);
using DrawSwf=void(__fastcall*)(void*,void*,uint64_t);
DrawSwf originalDrawSwf{};
thread_local void* hiddenSwf{};
void captureMarkerCanvas(void* canvas);
bool drawCalibrationPreview(void* swf,void* canvas,uint64_t time);
void __fastcall drawSwf(void* swf,void* canvas,uint64_t time){
 if(swf&&swf==hiddenSwf){
  static std::atomic<ULONGLONG> noted{};
  const auto now=GetTickCount64();auto previous=noted.load();
  if(now-previous>1000&&noted.compare_exchange_strong(previous,now))
   log("ETERNAL_HUD hidden=1 stage=swf-draw timeline=preserved");
  return;
 }
 if(drawCalibrationPreview(swf,canvas,time))return;
 captureMarkerCanvas(canvas);
 originalDrawSwf(swf,canvas,time);
}
using Canvas=void(__fastcall*)(void*,int,int,float,float);
using CreateCanvas=void(__fastcall*)(void*,int,bool,float);
CreateCanvas createCanvas{};
using SubmitGeometry=uintptr_t(__fastcall*)(void*,void*,int);
SubmitGeometry originalSubmit{};
struct GeometryOwner {void* entity{};uintptr_t owner{};float width{},height{},pivotX{},pivotY{};ULONGLONG tick{};uint64_t frame{};std::array<float,3> origin{};std::array<float,9> axis{};float extentX{},extentY{},artworkMeters{};};
std::array<GeometryOwner,roles.size()> geometryOwners{};
std::mutex geometryMutex;
std::array<float,roles.size()> artworkRatios{};
bool artworkRatiosDirty{};
struct MarkerGeometry {void* entity{};DistanceMarkerCanvas canvas;ULONGLONG tick{};};
MarkerGeometry markerGeometry;
// POIs share a movie. Only transform a batch containing one objective marker;
// never rotate unrelated pickups or multiple markers around a combined pivot.
bool singleObjective(void* owner){
 if(!owner)return false;const auto p=static_cast<const unsigned char*>(owner);
 const auto groups=*reinterpret_cast<const unsigned char*const*>(p+0x100);
 const int count=*reinterpret_cast<const int*>(p+0x108);if(!groups||count<1||count>64)return false;
 unsigned visible{};
 for(int i=0;i<count;++i){const auto g=groups+i*0x28;
  const auto decl=*reinterpret_cast<const unsigned char*const*>(g);
  const auto entries=*reinterpret_cast<const unsigned char*const*>(g+0x10);
  const int n=*reinterpret_cast<const int*>(g+0x18);if(n<0||n>128||(!entries&&n))return false;
  for(int j=0;j<n;++j){const auto entry=entries+j*0x98;
   if(!*reinterpret_cast<void*const*>(entry+0x58)||entry[0x8d]||*reinterpret_cast<const float*>(entry+0x64)<=0)continue;
   if(!decl||++visible>1)return false;
   const auto name=*reinterpret_cast<const char*const*>(decl+8);
   if(!name||!objectiveMarker(std::string_view(name,strnlen(name,64))))return false;
  }
 }
 return visible==1;
}
uintptr_t __fastcall submitGeometry(void* context,void* entity,int flags){
 GeometryOwner owned{};int role=-1;
 {std::lock_guard<std::mutex> lock(geometryMutex);const auto now=GetTickCount64();
  for(size_t i=0;i<geometryOwners.size();++i){const auto& o=geometryOwners[i];if(o.entity==entity&&entity&&now>=o.tick&&now-o.tick<100){owned=o;role=int(i);break;}}
 }
 if(role>=0&&context){
  // Called on the native SWF worker after streaming vertex writes finish.
  // Match the exact GUI entity, not thread-local state from the game thread.
  auto buffer=*static_cast<unsigned char**>(context);
  if(buffer){
   _mm_sfence();int count{},indices{};std::memcpy(&count,buffer+0xd0000,4);std::memcpy(&indices,buffer+0xd0004,4);
   int surfaceCount{};const unsigned char* surfaces{};
   std::memcpy(&surfaces,buffer+0xd0008,sizeof(surfaces));std::memcpy(&surfaceCount,buffer+0xd0010,4);
   GraphicBounds bounds;
   const bool measured=submittedGraphicBounds(buffer,count,reinterpret_cast<const uint16_t*>(buffer+0xc0000),indices,surfaces,surfaceCount,bounds);
   if(measured&&handRole(role)){
    const float ratio=(bounds.maxY-bounds.minY)/(bounds.maxX-bounds.minX);
    if(validArtworkRatio(ratio)){std::lock_guard<std::mutex> lock(geometryMutex);
     if(std::abs(artworkRatios[role]-ratio)>.0001f){artworkRatios[role]=ratio;artworkRatiosDirty=true;}}
   }
   const bool corrected=measured&&(nativeHudRole(role)?
    centerGraphicMetric(buffer,count,bounds,owned.width,owned.height,owned.extentX,owned.artworkMeters):
    centerGraphic(buffer,count,bounds,owned.width,owned.height,owned.pivotX,owned.pivotY,role!=0));
   kharvox::hands::HandHudPanels panels;
   if(corrected&&handRole(role)){
    panels=handMaskGeometry(buffer,count,reinterpret_cast<const uint16_t*>(buffer+0xc0000),
     indices,surfaces,surfaceCount,owned.origin.data(),owned.axis.data(),
     owned.extentX/owned.width,owned.extentY/owned.height);
   }
   if(handRole(role))camera::publishHudPanels(owned.frame,role,panels);
   static std::array<std::atomic<ULONGLONG>,roles.size()> noted{};const auto now=GetTickCount64();auto previous=noted[role].load();
   if(now-previous>5000&&noted[role].compare_exchange_strong(previous,now)&&(extendedLogging()||!previous)){
    log("ETERNAL_HUD_GRAPHIC role="+std::string(roles[role].name)+" corrected="+std::to_string(corrected)+" vertices="+std::to_string(count)+" surfaces="+std::to_string(surfaceCount)+" indices="+std::to_string(indices)+" canvas="+std::to_string(owned.width)+","+std::to_string(owned.height)+" bounds="+std::to_string(bounds.minX)+","+std::to_string(bounds.minY)+","+std::to_string(bounds.maxX)+","+std::to_string(bounds.maxY));
   }
  }
 }
 MarkerGeometry marker;
 {std::lock_guard<std::mutex> lock(geometryMutex);if(entity&&entity==markerGeometry.entity&&GetTickCount64()-markerGeometry.tick<100)marker=markerGeometry;}
 if(marker.entity&&context){auto buffer=*static_cast<unsigned char**>(context);if(buffer){
  _mm_sfence();int count{},indices{},surfaceCount{};const unsigned char* surfaces{};
  std::memcpy(&count,buffer+0xd0000,4);std::memcpy(&indices,buffer+0xd0004,4);
  std::memcpy(&surfaces,buffer+0xd0008,sizeof(surfaces));std::memcpy(&surfaceCount,buffer+0xd0010,4);
  GraphicBounds b;const bool corrected=submittedGraphicBounds(buffer,count,reinterpret_cast<const uint16_t*>(buffer+0xc0000),indices,surfaces,surfaceCount,b)&&billboardDistanceMarker(buffer,count,b,marker.canvas);
  static std::atomic<ULONGLONG> noted{};const auto now=GetTickCount64();auto previous=noted.load();
  if(now-previous>5000&&noted.compare_exchange_strong(previous,now)&&(extendedLogging()||!previous))
   log("ETERNAL_DISTANCE_MARKER corrected="+std::to_string(corrected)+" ratio=0.333333 upright=1 artworkBounds="+std::to_string(b.minX)+","+std::to_string(b.minY)+","+std::to_string(b.maxX)+","+std::to_string(b.maxY));
 }}
 return originalSubmit(context,entity,flags);
}

Update originalUpdate{};Render originalRender{};Canvas originalCanvas{};
uintptr_t image{};
using UpdatePoiEntry=void(__fastcall*)(void*,void*,void*,void*,void*);
UpdatePoiEntry originalPoiEntry{};
bool nativeMarkerScale{};
void __fastcall updatePoiEntry(void* owner,void* view,void* entry,void* declaration,void* group){
 const char* name=declaration?*reinterpret_cast<const char**>(static_cast<unsigned char*>(declaration)+8):nullptr;
 const bool objective=name&&objectiveMarker(std::string_view(name,strnlen(name,64)));
 if(entry&&objective&&presentation::gameplayInput.load()){
  auto& scale=*reinterpret_cast<float*>(static_cast<unsigned char*>(entry)+0x60);
  PreviewValueScope<float> scaled(scale,objectiveScale(scale,true,true));
  originalPoiEntry(owner,view,entry,declaration,group);
  static std::atomic<ULONGLONG> noted{};const auto now=GetTickCount64();auto previous=noted.load();
  if(now-previous>2000&&noted.compare_exchange_strong(previous,now))log("ETERNAL_DISTANCE_MARKER nativeEntry=1 name="+std::string(name)+" ratio=0.333333 symbolAndText=1");
 }else originalPoiEntry(owner,view,entry,declaration,group);
}
std::atomic<bool> wheelVisible{};
std::atomic<ULONGLONG> wheelVisibleTick{};
struct Source {uintptr_t owner{};float eye[3]{},axis[9]{};ULONGLONG tick{};};
std::array<Source,roles.size()> sources{};
std::array<Calibration,roles.size()> calibrations{};
std::mutex stateMutex;
// Register all native HUD owners, not only the sixteen calibrated roles.
// The shared GUI renderer also draws menus and world screens: never blanket-skip it.
struct AnimationHudOwner {uintptr_t table{};ULONGLONG tick{};};
std::map<uintptr_t,AnimationHudOwner> animationHudOwners;
std::atomic<ULONGLONG> flatMenuDrawTick{};
std::atomic<ULONGLONG> tutorialDrawTick{};
std::atomic<ULONGLONG> gameplayMenuDrawTick{};
HandPanels handPanels=defaultHandPanels();
std::array<HandHudPivot,2> handPivots{};
Calibration compassCalibration=defaultCompassCalibration();
std::atomic<bool> handCalibrationActive{};
std::atomic<int> selectedHandRole{1};std::atomic<ULONGLONG> selectionUntil{};
#include "NativeHudPreview.inc"
std::filesystem::path handConfigPath(){
 static const auto path=[](){HMODULE module{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&handConfigPath),&module);wchar_t name[32768]{};GetModuleFileNameW(module,name,32768);return std::filesystem::path(name).parent_path()/L"assets"/L"argent_offhand_hud.cfg";}();return path;
}
void loadHandPanels(){std::ifstream file(handConfigPath());HandPanels next;if(file&&readHandPanels(file,next))handPanels=next;
 std::ifstream pivots(handConfigPath().parent_path()/L"argent_hud_group.cfg");pivots.imbue(std::locale::classic());readHandHudPivots(pivots,handPivots);
 std::ifstream compass(handConfigPath().parent_path()/L"argent_compass.cfg");compass.imbue(std::locale::classic());
 readCompassCalibration(compass,compassCalibration);
 std::ifstream sizes(handConfigPath().parent_path()/L"argent_hud_artwork.cfg");
 int role;float ratio;while(sizes>>role>>ratio)if(role>=0&&role<int(roles.size())&&validArtworkRatio(ratio))artworkRatios[role]=ratio;
}

void loadCalibration(){
 static ULONGLONG checked{};const auto now=GetTickCount64();if(now-checked<1000)return;checked=now;
 static const auto path=[](){HMODULE module{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&loadCalibration),&module);wchar_t name[32768]{};GetModuleFileNameW(module,name,32768);return std::filesystem::path(name).parent_path()/L"assets"/L"argent_hud.cfg";}();
 WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info))return;
 static FILETIME stamp{};if(CompareFileTime(&stamp,&info.ftLastWriteTime)==0)return;
 std::ifstream file(path);if(!file)return;
 auto next=std::array<Calibration,roles.size()>{};next[0].scale=kharvox::weaponWheelWidthMeters;
 std::string line;bool ok=true;while(std::getline(file,line)){
  if(line.empty()||line[0]=='#')continue;
  std::istringstream row(line);std::string name;Calibration c;if(!(row>>name))continue;
  int index=-1;for(size_t i=0;i<roles.size();++i)if(name==roles[i].name)index=int(i);
  if(index<0||!(row>>c.distance>>c.scale>>c.right>>c.up)||!valid(c,index==0?3.f:2.f)){ok=false;break;}
  next[index]=c;
 }
 stamp=info.ftLastWriteTime;
 if(ok){calibrations=next;log("ETERNAL_HUD calibration loaded assets/argent_hud.cfg");}
 else log("ETERNAL_HUD invalid calibration retained previous values");
}
struct Pending {void* entity{};int role=-1;Source source{};Calibration calibration{};float eye[3]{},head[9]{},grip[3]{},hand[9]{};bool handBound{};HandPanel panel{};HandHudGroup group{};uint64_t frame{};bool marker{};DistanceMarkerCanvas markerCanvas;bool markerCanvasReady{};};
thread_local Pending pending;
void captureMarkerCanvas(void* canvas){
 if(!pending.marker||!pending.markerCanvasReady||canvas!=pending.entity)return;
 MarkerGeometry next;next.entity=canvas;next.tick=GetTickCount64();next.canvas=pending.markerCanvas;
 const auto bytes=static_cast<unsigned char*>(canvas);
 // Screen-space render resets these AFTER the canvas sizing call.
 std::memcpy(next.canvas.origin.data(),bytes+0xf8,12);std::memcpy(next.canvas.axis.data(),bytes+0x104,36);
 std::lock_guard<std::mutex> lock(geometryMutex);markerGeometry=next;
}
void __fastcall update(void* owner,const float* position,const float* axis,void* entity,void* time){
 if(owner){
  std::lock_guard<std::mutex> lock(stateMutex);
  const auto now=GetTickCount64();
  if(animationHudOwners.size()>256){
   for(auto it=animationHudOwners.begin();it!=animationHudOwners.end();)
    if(now-it->second.tick>1000)it=animationHudOwners.erase(it);else ++it;
  }
  animationHudOwners[uintptr_t(owner)+0x10]={*static_cast<const uintptr_t*>(owner),now};
 }
 int index=owner?roleIndex(build::semanticRva(*static_cast<const uintptr_t*>(owner)-image)):-1;
 const bool preview=previewEligible(index);
 if(index>=0&&position&&axis){
  {std::lock_guard<std::mutex> lock(stateMutex);loadCalibration();auto& s=sources[index];s.owner=uintptr_t(owner);s.tick=GetTickCount64();std::memcpy(s.eye,position,sizeof(s.eye));std::memcpy(s.axis,axis,sizeof(s.axis));}
  auto bytes=static_cast<unsigned char*>(owner);auto gui=bytes+0x10;float eye[3]{},basis[9]{};
  // Owned hand panels and the wheel need a native world canvas.
  if((index==0||calibratableHudRole(index))&&(bytes[0xb8]||preview)&&createCanvas&&*reinterpret_cast<void**>(gui)&&
     !*reinterpret_cast<void**>(gui+8)&&!*reinterpret_cast<void**>(gui+0x18)&&camera::hudCamera(eye,basis)){
   createCanvas(gui,0,false,1.f);log("ETERNAL_HUD role="+std::string(roles[index].name)+" flat-to-world created="+std::to_string(*reinterpret_cast<void**>(gui+8)!=nullptr));
  }
 }
 if(preview){PreviewValueScope<unsigned char> visible(*(static_cast<unsigned char*>(owner)+0xb8),1);originalUpdate(owner,position,axis,entity,time);}
 else originalUpdate(owner,position,axis,entity,time);
 if(index==0){wheelVisible=static_cast<unsigned char*>(owner)[0xb8]!=0;wheelVisibleTick=GetTickCount64();}
}
void __fastcall render(void* gui,void* view,void* time,bool offscreen,bool advance,bool draw){
 bool ownedHud=false,poi=false;int ownedRole=-1;
 if(gui){
  std::lock_guard<std::mutex> lock(stateMutex);
  const auto found=animationHudOwners.find(uintptr_t(gui));
  // HUD updates can pause during the animation while its separate camera
  // continues rendering. Validate ownership rather than expiring by age.
  uintptr_t table{};SIZE_T got{};
  const bool readable=ReadProcessMemory(GetCurrentProcess(),
   reinterpret_cast<void*>(uintptr_t(gui)-0x10),&table,sizeof(table),&got)&&got==sizeof(table);
  ownedHud=readable&&found!=animationHudOwners.end()&&table==found->second.table;
  if(ownedHud)ownedRole=roleIndex(build::semanticRva(table-image));
  // Some modal menus bypass the ordinary HUD update but still draw their GUI.
  if(readable&&draw&&flatMenuVtable(build::semanticRva(table-image)))flatMenuDrawTick=GetTickCount64();
  if(readable&&draw&&tutorialKind(build::semanticRva(table-image))>=0)tutorialDrawTick=GetTickCount64();
  if(readable&&draw&&gameplayMenuVtable(build::semanticRva(table-image)))gameplayMenuDrawTick=GetTickCount64();
  poi=ownedHud&&build::semanticRva(table-image)==distanceMarkerVtable;
 }
 // Native render falls back to 'view' when gui+0x10 is null. During sync
 // this is the separate HUD camera, not one of our promoted world canvases.
 // Gate its SWF draw before worker scheduling, keeping native timeline work.
 std::unique_lock<std::recursive_mutex> previewLock(previewMutex);
 if(!handCalibrationActive.load())destroyPreview();
 void* movie=ownedHud&&draw&&previewEligible(ownedRole)?preparePreview(gui?*static_cast<void**>(gui):nullptr,ownedRole,time):nullptr;
 if(!movie)previewLock.unlock();
 void* unused{};
 PreviewValueScope<void*> movieScope(gui?*static_cast<void**>(gui):unused,movie?movie:(gui?*static_cast<void**>(gui):nullptr));
 PreviewValueScope<void*> drawScope(drawingPreview,movie);
 struct Restore {Pending previous;~Restore(){pending=previous;}} restore{pending};pending={};
 const bool cameraAvailable=camera::hudCamera(pending.eye,pending.head,&pending.frame);
 // Give the compass a metric canvas, without attaching it to a controller.
 if(nativeHudRole(ownedRole)&&cameraAvailable&&*reinterpret_cast<void**>(static_cast<unsigned char*>(gui)+8))offscreen=false;
 bool left{};
 const bool handAvailable=handRole(ownedRole)&&cameraAvailable&&camera::hudOffhand(pending.grip,pending.hand,left);
 HudDrawScope visibility(hiddenSwf,gui?*static_cast<void**>(gui):nullptr,ownedHud,
  hideManagedHud(presentation::hideGameplayHud.load(),presentation::syncAttack.load(),
   handRole(ownedRole),presentation::nativeAnimation.load(),handAvailable));
 struct RestoreEntity {void** slot{};void* previous{};~RestoreEntity(){if(slot)*slot=previous;}} activeEntity;
 struct RestoreProjection {float* slot{};float previous{};~RestoreProjection(){if(slot)*slot=previous;}} projection;
 if((!offscreen||poi)&&cameraAvailable){
  pending.marker=poi&&singleObjective(static_cast<unsigned char*>(gui)-0x10)&&
   *reinterpret_cast<void**>(static_cast<unsigned char*>(gui)+8)&&
   *reinterpret_cast<void**>(static_cast<unsigned char*>(gui)+8)==*reinterpret_cast<void**>(static_cast<unsigned char*>(gui)+0x10);
  pending.markerCanvas.screenSpace=offscreen;
  pending.markerCanvas.geometryScale=nativeMarkerScale?1.f:1.f/3.f;
  if(pending.marker&&offscreen&&!camera::hudProjection(pending.markerCanvas.tanX,pending.markerCanvas.tanY))pending.marker=false;
  if(!offscreen){std::lock_guard<std::mutex> lock(stateMutex);for(size_t i=0;i<sources.size();++i){const auto& s=sources[i];
   if(s.owner&&uintptr_t(gui)==s.owner+0x10&&(calibratableHudRole(int(i))||GetTickCount64()-s.tick<100)&&roleIndex(build::semanticRva(*reinterpret_cast<const uintptr_t*>(s.owner)-image))==int(i)){
    pending.role=int(i);pending.source=s;pending.calibration=nativeHudRole(int(i))?compassCalibration:calibrations[i];break;
   }
  }}
  if(handRole(pending.role)){
   if(!handAvailable){originalRender(gui,view,time,offscreen,movie?false:advance,draw);return;}
   {std::lock_guard<std::mutex> lock(stateMutex);pending.panel=handPanels[pending.role][left?1:0];
    pending.group=handHudGroup(handPanels,left,pending.grip,pending.hand,pending.eye,camera::unitsPerMeter(),handPivots[left?1:0]);}
   pending.handBound=true;
   // Brief blink identifies the chosen graphic without moving its pivot.
   if(pending.role==selectedHandRole.load()&&GetTickCount64()<selectionUntil.load()&&(GetTickCount64()/150)%2)return;
  }
  auto bytes=static_cast<unsigned char*>(gui);
  if(nativeHudRole(pending.role)&&handCalibrationActive.load()&&selectedHandRole.load()==pending.role&&GetTickCount64()<selectionUntil.load()&&(GetTickCount64()/150)%2)return;
  if(pending.role==0||pending.handBound||nativeHudRole(pending.role)){
   activeEntity.slot=reinterpret_cast<void**>(bytes+0x10);activeEntity.previous=*activeEntity.slot;
   *activeEntity.slot=*reinterpret_cast<void**>(bytes+8);
  }
  if(pending.role>=0)pending.entity=*reinterpret_cast<void**>(bytes+0x10);
  if(pending.marker)pending.entity=*reinterpret_cast<void**>(bytes+0x10);
  if(pending.entity&&!pending.marker){
   // Native -1 sentinel selects the scene projection (0x157cb5f).
   // A per-GUI flat FOV would invalidate the calibrated metric plane.
   projection.slot=reinterpret_cast<float*>(bytes+0x6c);projection.previous=*projection.slot;*projection.slot=-1.f;
  }
 }
 if(poi&&!pending.marker){std::lock_guard<std::mutex> lock(geometryMutex);markerGeometry={};}
 originalRender(gui,view,time,offscreen,movie?false:advance,draw);
}
void __fastcall canvas(void* entity,int width,int height,float extentX,float extentY){
 const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
 float origin[3]{},axis[9]{},native[9]{};bool corrected=false;
 if(entity&&entity==pending.entity&&pending.role>=0&&caller==image+build::rva(0x157cf4a)&&width>0&&height>0){
  auto bytes=static_cast<unsigned char*>(entity);const float units=camera::unitsPerMeter();
  std::memcpy(native,bytes+0x104,sizeof(native));
  if(nativeHudRole(pending.role)){
   float tanX=1.f,tanY=float(height)/width;
   camera::hudProjection(tanX,tanY);
   corrected=compassCanvas(pending.eye,pending.head,units,tanX,tanY,pending.calibration,origin,axis,extentX,extentY);
  }else if(pending.handBound){
   corrected=handPanelPose(pending.panel,pending.grip,pending.hand,units,float(width)/height,origin,axis,extentX,extentY);
   if(corrected)pending.group.canvas(origin,axis);
  }else if(pending.role==0){
   float center[3]{};const auto c=pending.calibration;
   for(int i=0;i<3;++i)center[i]=pending.eye[i]+units*(pending.head[i]*c.distance-pending.head[3+i]*c.right+pending.head[6+i]*c.up);
   // Keep SWF lettering readable: pixel X goes right, pixel Y goes down.
   // Artwork centering at upload preserves the same midpoint after rotation.
   kharvox::weaponWheelCanvasAxes(pending.head,axis);
   corrected=kharvox::centeredOffhandHud(center,axis,c.scale*units,float(width)/height,origin,extentX,extentY);
  }else{
   float sourceOrigin[3]{},factor{};std::memcpy(sourceOrigin,bytes+0xf8,sizeof(sourceOrigin));
   corrected=layout(sourceOrigin,native,pending.source.eye,pending.source.axis,pending.eye,pending.head,units,pending.calibration,origin,axis,factor);
   if(corrected){extentX*=factor;extentY*=factor;}
  }
  if(extendedLogging()){
   static thread_local std::array<ULONGLONG,roles.size()> noted{};const auto now=GetTickCount64();
   if(now-noted[pending.role]>=5000){noted[pending.role]=now;
    log("ETERNAL_HUD role="+std::string(roles[pending.role].name)+" corrected="+std::to_string(corrected)+" canvas="+std::to_string(width)+"x"+std::to_string(height)+" distanceMeters="+std::to_string(pending.calibration.distance)+" scale="+std::to_string(pending.calibration.scale)+" extents="+std::to_string(extentX)+","+std::to_string(extentY));
   }
  }
 }
 originalCanvas(entity,width,height,extentX,extentY);
 if(pending.marker&&entity==pending.entity&&markerCanvasCaller(build::semanticRva(caller-image),pending.markerCanvas.screenSpace)&&width>0&&height>0){
  auto& c=pending.markerCanvas;
  std::memcpy(c.eye.data(),pending.eye,12);std::memcpy(c.head.data(),pending.head,36);
  c.pixelX=extentX/width;c.pixelY=extentY/height;pending.markerCanvasReady=true;
 }
 if((pending.handBound||pending.role==0||nativeHudRole(pending.role))&&entity==pending.entity){
  std::lock_guard<std::mutex> lock(geometryMutex);
  const float pivotX=pending.role==0?.5f:pending.panel.pivotX;
  const float pivotY=pending.role==0?.5f:pending.panel.pivotY;
  geometryOwners[pending.role]=corrected?GeometryOwner{entity,pending.source.owner,float(width),float(height),pivotX,pivotY,GetTickCount64()}:GeometryOwner{};
  if(corrected){auto& g=geometryOwners[pending.role];g.frame=pending.frame;g.extentX=extentX;g.extentY=extentY;g.artworkMeters=pending.calibration.scale;std::memcpy(g.origin.data(),origin,sizeof(origin));std::memcpy(g.axis.data(),axis,sizeof(axis));}
 }
 if(!corrected)return;
 auto bytes=static_cast<unsigned char*>(entity);
 if((bytes[0xb0]&0xc)!=0xc){std::memcpy(bytes+0x158,origin,sizeof(origin));std::memcpy(bytes+0x164,axis,sizeof(axis));}
 std::memcpy(bytes+0xf8,origin,sizeof(origin));std::memcpy(bytes+0x104,axis,sizeof(axis));
}
}
bool weaponWheelVisible() noexcept {
 return wheelVisible.load()&&GetTickCount64()-wheelVisibleTick.load()<250;
}
int calibrationPlaceholderRole(){
 const int role=selectedHandRole.load();
 return handCalibrationActive.load()&&handRole(role)?role:-1;
}
kharvox::hands::HandHudPanels calibrationPlaceholder(int role,bool leftMode,const float* grip,const float* hand,float units,const float* eye){
 if(!handRole(role)||calibrationPlaceholderRole()<0)return {};
 HandPanel panel;HandHudGroup group;{std::lock_guard<std::mutex> lock(stateMutex);panel=handPanels[role][leftMode?1:0];group=handHudGroup(handPanels,leftMode,grip,hand,eye,units,handPivots[leftMode?1:0]);}
 float ratio;{std::lock_guard<std::mutex> lock(geometryMutex);ratio=artworkRatios[role];}
 const bool measured=validArtworkRatio(ratio);
 auto result=placeholderGeometry(panel,grip,hand,units,measured?ratio:1.f,measured,role==selectedHandRole.load());
 for(auto& quad:result)for(auto& point:quad)group.point(point.data());
 return result;
}
bool flatMenuVisible(bool gameplayOnly,bool tutorialOnly) noexcept {
 const auto drawn=tutorialOnly?tutorialDrawTick.load():gameplayOnly?gameplayMenuDrawTick.load():flatMenuDrawTick.load();if(drawn&&GetTickCount64()-drawn<150)return true;
 std::lock_guard<std::mutex> lock(stateMutex);
 for(const auto& entry:animationHudOwners){
  if(tutorialOnly&&tutorialKind(build::semanticRva(entry.second.table-image))<0)continue;
  if(gameplayOnly?!gameplayMenuVtable(build::semanticRva(entry.second.table-image)):!flatMenuVtable(build::semanticRva(entry.second.table-image)))continue;
  const auto owner=entry.first-0x10;uintptr_t table{};unsigned char visible{};SIZE_T got{};
  if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(owner),&table,sizeof(table),&got)&&got==sizeof(table)&&table==entry.second.table&&
     ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(owner+0xb8),&visible,1,&got)&&got==1&&visible==1)return true;
 }
 return false;
}
void pollHandCalibration(bool enabled,bool leftMode){
 const bool previous=handCalibrationActive.exchange(enabled);
 if(previous!=enabled)log("ETERNAL_HUD_CALIBRATION enabled="+std::to_string(enabled)+" selected="+std::string(roles[selectedHandRole.load()].name));
 static ULONGLONG lastSizeSave{};
 if(GetTickCount64()-lastSizeSave>2000){
  lastSizeSave=GetTickCount64();std::lock_guard<std::mutex> lock(geometryMutex);
  if(artworkRatiosDirty){
   const auto path=handConfigPath().parent_path()/L"argent_hud_artwork.cfg";auto tmp=path;tmp+=L".tmp";
   std::ofstream out(tmp);out.imbue(std::locale::classic());out<<std::setprecision(9);
   for(size_t r=0;r<artworkRatios.size();++r)if(validArtworkRatio(artworkRatios[r]))out<<r<<' '<<artworkRatios[r]<<'\n';
   out.close();if(out&&MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))artworkRatiosDirty=false;
  }
 }
 static bool wasPlus{},wasReset{},wasNext{},wasPivot{},wasActive{};static int mode=0;static ULONGLONG nextStep{};
 if(!enabled){wasActive=false;nextStep=0;wasPlus=wasReset=wasNext=wasPivot=false;return;}
 DWORD process{};GetWindowThreadProcessId(GetForegroundWindow(),&process);
 auto down=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;};
 const bool active=enabled&&process==GetCurrentProcessId()&&!down(VK_MENU)&&!down(VK_CONTROL);
 const bool plus=down(VK_ADD),reset=down(VK_NUMPAD5),next=down(VK_NUMPAD0),pivot=down(VK_DECIMAL);
 const bool switchMode=plus&&!wasPlus,resetNow=reset&&!wasReset,nextNow=next&&!wasNext,pivotNow=pivot&&!wasPivot;
 wasPlus=plus;wasReset=reset;wasNext=next;wasPivot=pivot;
 if(active&&!wasActive)selectionUntil=GetTickCount64()+1200;
 wasActive=active;if(!active){nextStep=0;return;}
 if(nextNow){int r=selectedHandRole;do{r=(r+1)%int(roles.size());}while(!calibratableHudRole(r));selectedHandRole=r;selectionUntil=GetTickCount64()+1200;log("ETERNAL_HUD calibration selected="+std::string(roles[r].name));}
 if(switchMode)mode=mode==0?1:0;if(pivotNow)mode=2;
 if(nativeHudRole(selectedHandRole.load()))mode=0;
 if(switchMode||pivotNow)log("ETERNAL_HUD calibration mode="+std::string(mode==0?"position":mode==1?"rotation":"artwork-pivot"));
 const auto now=GetTickCount64();if(now<nextStep&&!resetNow)return;
 const int x=int(down(VK_NUMPAD6))-int(down(VK_NUMPAD4)),y=int(down(VK_NUMPAD8))-int(down(VK_NUMPAD2)),z=int(down(VK_NUMPAD9))-int(down(VK_NUMPAD7));
 const int size=int(down(VK_MULTIPLY))-int(down(VK_DIVIDE));if(!x&&!y&&!z&&!size&&!resetNow){nextStep=0;return;}
 if(nativeHudRole(selectedHandRole.load())){
  Calibration c;{std::lock_guard<std::mutex> lock(stateMutex);c=compassCalibration;}
  c=adjustCompass(c,x,y,z,size,down(VK_SHIFT),resetNow);
  const auto path=handConfigPath().parent_path()/L"argent_compass.cfg";auto temp=path;temp+=L".tmp";
  std::ofstream out(temp);out.imbue(std::locale::classic());writeCompassCalibration(out,c);out.close();
  if(!out||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
   log("ETERNAL_HUD compass calibration save failed");nextStep=now+1000;return;
  }
  {std::lock_guard<std::mutex> lock(stateMutex);compassCalibration=c;}
  nextStep=now+100;log("ETERNAL_HUD calibration saved role=Compass upMeters="+std::to_string(c.up));return;
 }
 HandPanels panels;{std::lock_guard<std::mutex> lock(stateMutex);panels=handPanels;}
 const int r=selectedHandRole,hand=leftMode?1:0;auto& p=panels[r][hand];const bool fine=down(VK_SHIFT);
 if(resetNow)p=defaultHandPanels()[r][hand];
 else{
  if(mode==0){const float step=fine?.25f:.5f;p.pose.centimeters[0]-=z*step;p.pose.centimeters[1]-=x*step;p.pose.centimeters[2]+=y*step;for(auto& v:p.pose.centimeters)v=std::clamp(v,-100.f,100.f);}
  if(mode==1){const float step=fine?2.5f:5.f;p.pose.degrees[0]=std::remainder(p.pose.degrees[0]+y*step,360.f);p.pose.degrees[1]=std::remainder(p.pose.degrees[1]+x*step,360.f);p.pose.degrees[2]=std::remainder(p.pose.degrees[2]+z*step,360.f);}
  if(mode==2){const float step=fine?.0025f:.01f;p.pivotX=std::clamp(p.pivotX+x*step,0.f,1.f);p.pivotY=std::clamp(p.pivotY-y*step,0.f,1.f);}
  p.pose.scale=std::clamp(p.pose.scale+size*(fine?.005f:.01f),.02f,2.f);
 }
 const auto path=handConfigPath();auto temp=path;temp+=L".tmp";std::ofstream file(temp);file.imbue(std::locale::classic());writeHandPanels(file,panels);file.close();
 if(!file||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){log("ETERNAL_HUD calibration save failed; retaining previous values");nextStep=now+1000;return;}
 {std::lock_guard<std::mutex> lock(stateMutex);handPanels=panels;}
 nextStep=now+100;log("ETERNAL_HUD calibration saved role="+std::string(roles[r].name)+" leftMode="+std::to_string(leftMode));
}
bool installWeaponWheel(unsigned char* base) noexcept {
 constexpr unsigned char updateBytes[]={0x40,0x53,0x55,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xc0,0,0,0};
 constexpr unsigned char renderBytes[]={0x4c,0x8b,0xdc,0x55,0x53,0x41,0x54,0x41,0x56};
 constexpr unsigned char drawBytes[]={0x4c,0x8b,0xdc,0x49,0x89,0x53,0x10,0x55,0x53,0x41,0x55,0x41,0x56};
 constexpr unsigned char canvasBytes[]={0x40,0x53,0x48,0x83,0xec,0x60};
 constexpr unsigned char submitBytes[]={0x48,0x8b,0x01,0x4c,0x8b,0xca,0x48,0x89,0x82,0x20,0x46,0,0};
 constexpr unsigned char createBytes[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
 if(!base||std::memcmp(base+build::rva(0x182e610),drawBytes,sizeof(drawBytes))||std::memcmp(base+build::rva(0x194cb20),submitBytes,sizeof(submitBytes))||std::memcmp(base+build::rva(0x15ef980),updateBytes,sizeof(updateBytes))||
    std::memcmp(base+build::rva(0x157ca10),renderBytes,sizeof(renderBytes))||std::memcmp(base+build::rva(0x194f840),canvasBytes,sizeof(canvasBytes))||
    std::memcmp(base+build::rva(0x157c400),createBytes,sizeof(createBytes))||
    *reinterpret_cast<uintptr_t*>(base+build::rva(0x2d03ea8)+39*8)!=uintptr_t(base+build::rva(0xecfea0))){
  log("ETERNAL_WEAPON_WHEEL refused: signature/vtable mismatch");return false;
 }
 image=uintptr_t(base);
 installTutorialBindings(base);
 constexpr unsigned char poiBytes[]={0x40,0x55,0x56,0x41,0x55,0x41,0x56,0x41,0x57};
 const bool poiSignature=std::memcmp(base+build::rva(0xeee970),poiBytes,sizeof(poiBytes))==0;
 // r110's native movie clone produced no usable preview. Placeholders are
 // rendered independently and must not alter native HUD owners or timelines.
 previewApiReady=false;
 loadHandPanels();
 calibrations[0].scale=kharvox::weaponWheelWidthMeters;
 createCanvas=reinterpret_cast<CreateCanvas>(base+build::rva(0x157c400));
 bool ok=MH_CreateHook(base+build::rva(0x15ef980),reinterpret_cast<void*>(&update),reinterpret_cast<void**>(&originalUpdate))==MH_OK;
 if(ok&&poiSignature){
  nativeMarkerScale=MH_CreateHook(base+build::rva(0xeee970),reinterpret_cast<void*>(&updatePoiEntry),reinterpret_cast<void**>(&originalPoiEntry))==MH_OK;
  if(nativeMarkerScale)nativeMarkerScale=MH_EnableHook(base+build::rva(0xeee970))==MH_OK;
  if(!nativeMarkerScale)MH_RemoveHook(base+build::rva(0xeee970));
 }
 if(ok&&previewApiReady){
  previewApiReady=MH_CreateHook(base+build::rva(0x15ef6c0),reinterpret_cast<void*>(&renderOwner),reinterpret_cast<void**>(&originalOwnerRender))==MH_OK;
  if(previewApiReady)previewApiReady=MH_EnableHook(base+build::rva(0x15ef6c0))==MH_OK;
  if(!previewApiReady)MH_RemoveHook(base+build::rva(0x15ef6c0));
 }
 if(ok)ok=MH_CreateHook(base+build::rva(0x157ca10),reinterpret_cast<void*>(&render),reinterpret_cast<void**>(&originalRender))==MH_OK;
 if(ok)ok=MH_CreateHook(base+build::rva(0x194f840),reinterpret_cast<void*>(&canvas),reinterpret_cast<void**>(&originalCanvas))==MH_OK;
 if(ok)ok=MH_CreateHook(base+build::rva(0x194cb20),reinterpret_cast<void*>(&submitGeometry),reinterpret_cast<void**>(&originalSubmit))==MH_OK;
 if(ok)ok=MH_CreateHook(base+build::rva(0x182e610),reinterpret_cast<void*>(&drawSwf),reinterpret_cast<void**>(&originalDrawSwf))==MH_OK;
 if(ok)ok=MH_EnableHook(base+build::rva(0x182e610))==MH_OK&&MH_EnableHook(base+build::rva(0x194cb20))==MH_OK&&MH_EnableHook(base+build::rva(0x194f840))==MH_OK&&MH_EnableHook(base+build::rva(0x157ca10))==MH_OK&&MH_EnableHook(base+build::rva(0x15ef980))==MH_OK;
 if(!ok){previewApiReady=false;nativeMarkerScale=false;for(auto rva:{0xeee970,0x15ef6c0,0x15ef980,0x157ca10,0x194f840,0x194cb20,0x182e610}){MH_DisableHook(base+build::rva(rva));MH_RemoveHook(base+build::rva(rva));}}
 log("ETERNAL_DISTANCE_MARKER nativeScaleInstalled="+std::to_string(nativeMarkerScale));
 log("ETERNAL_HUD_PREVIEW installed="+std::to_string(previewApiReady)+" scope=selected-calibration-role");
 log(std::string("ETERNAL_WEAPON_WHEEL installed=")+(ok?"1":"0")+" policy=KHARVOX final-canvas-center");return ok;
}
}

