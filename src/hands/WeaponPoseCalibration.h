#pragma once
#include <windows.h>
#include "HandCalibrationPolicy.h"
#include "CalibrationDraft.h"
#include "../openxr/WeaponConfig.h"
#include <iomanip>
namespace argent::input {
// Same KHARVOX key layout/steps; Eternal uses its own controller placement.
// Saved deltas are applied to the selected baseline, separately for each hand.
class WeaponPoseCalibration {
public:
 struct Delta {XrVector3f position{},degrees{};};
 calibration::Draft<Delta> draft;
 calibration::ApplyRevision applyRevision;
 kharvox::hands::CalibrationMode mode{kharvox::hands::CalibrationMode::Rotation};
 bool plus{},reset{};ULONGLONG lastStep{};bool loaded{};
 std::filesystem::path root;
 static bool valid(const Delta& d){return camera::validPosition(d.position)&&length(d.position)<=.8661f&&camera::validPosition(d.degrees)&&std::abs(d.degrees.x)<=180&&std::abs(d.degrees.y)<=180&&std::abs(d.degrees.z)<=180;}
 static std::string key(const std::string& profile,bool left){return profile+(left?"_left":"_right");}
 void load(const std::filesystem::path& directory){if(loaded)return;root=directory;loaded=true;
  for(const auto* filename:{L"weapon_pose_calibration_default.cfg",L"weapon_pose_calibration_saved.cfg"}){
  std::ifstream in(root/filename);std::string line;
  while(std::getline(in,line)){std::istringstream row(line);std::string name;Delta d;
   if(row>>name>>d.position.x>>d.position.y>>d.position.z>>d.degrees.x>>d.degrees.y>>d.degrees.z){if(valid(d))draft.saved[name]=d;}}}
 }
 Delta resolved(const std::string& profile,bool left)const{
  auto value=draft.find(key(profile,left));
  if(!value)value=draft.find(key("default",left));
  return value?*value:Delta{};
 }
 static bool canEdit(const std::string& selected,const std::string& equipped){return selected=="default"||selected==equipped;}
 void apply(WeaponCalibration& c,const std::string& profile,bool left)const{
  const auto d=resolved(profile,left);
  c.pose.offset=add(c.pose.offset,d.position);
  c.pose.rotation=product(c.pose.rotation,euler(d.degrees.x,d.degrees.y,d.degrees.z));
 }
 bool saveValues(const std::map<std::string,Delta>& committed)const{
  const auto target=root/L"weapon_pose_calibration_saved.cfg",temporary=root/L"weapon_pose_calibration_saved.tmp";
  std::ofstream out(temporary);out<<std::fixed<<std::setprecision(6);
  for(const auto& [name,d]:committed)out<<name<<' '<<d.position.x<<' '<<d.position.y<<' '<<d.position.z<<' '<<d.degrees.x<<' '<<d.degrees.y<<' '<<d.degrees.z<<'\n';
  out.flush();const bool ok=bool(out);out.close();return ok&&MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
 }
 bool save(){return draft.apply([&](const auto& candidate){return saveValues(candidate);});}
 void configure(const std::string& profile,bool left,bool enabled,const calibration::ApplyCommand& command){
  if(applyRevision.consume(command.revision)&&command.mode=="weapon"){
   const auto target=key(command.profile,command.left);
   calibration::report(root,command.revision,draft.scope==target&&save(),"weapon "+target);
  }
  draft.select(enabled?key(profile,left):std::string{});
 }
 bool step(const std::string& profile,bool left,const std::array<bool,6>& keys,bool fine,bool resetNow){
  if(!resetNow&&std::none_of(keys.begin(),keys.end(),[](bool v){return v;}))return false;
  // First edit starts from the currently inherited pose. Once created, this
  // weapon override is independent of subsequent changes to the default.
  auto initial=resolved(profile,left);
  auto& d=draft.edit(key(profile,left),initial);if(resetNow){d={};return true;}
  auto& v=mode==kharvox::hands::CalibrationMode::Rotation?d.degrees:d.position;
  const float amount=mode==kharvox::hands::CalibrationMode::Rotation?(fine?1.f:5.f):(fine?.001f:.005f);
  v.x+=(int(keys[1])-int(keys[0]))*amount;v.y+=(int(keys[3])-int(keys[2]))*amount;v.z+=(int(keys[5])-int(keys[4]))*amount;
  if(mode==kharvox::hands::CalibrationMode::Rotation){v.x=std::remainder(v.x,360.f);v.y=std::remainder(v.y,360.f);v.z=std::remainder(v.z,360.f);}
  else {v.x=std::clamp(v.x,-.5f,.5f);v.y=std::clamp(v.y,-.5f,.5f);v.z=std::clamp(v.z,-.5f,.5f);}return true;
 }
 void poll(const std::string& profile,bool left,bool active){
  if(!active){plus=reset=false;return;}
  auto down=[](int k){return (GetAsyncKeyState(k)&0x8000)!=0;};
  const bool p=down(VK_ADD),r=down(VK_NUMPAD5);DWORD process{};GetWindowThreadProcessId(GetForegroundWindow(),&process);
  if(process!=GetCurrentProcessId()||down(VK_MENU)||down(VK_CONTROL)){plus=p;reset=r;return;}
  const auto previous=mode;mode=kharvox::hands::updateHandCalibrationMode(mode,p,plus);
  const bool resetNow=r&&!reset;reset=r;const auto now=GetTickCount64();
  bool changed=false;
  if(resetNow||now-lastStep>=90){std::array<bool,6> keys{down(VK_NUMPAD4),down(VK_NUMPAD6),down(VK_NUMPAD2),down(VK_NUMPAD8),down(VK_NUMPAD7),down(VK_NUMPAD9)};
   changed=step(profile,left,keys,down(VK_SHIFT),resetNow);if(changed)lastStep=now;}
  if(changed||previous!=mode){std::ofstream out(root/L"weapon_calibration_status.txt");out<<key(profile,left)<<" | "<<(mode==kharvox::hands::CalibrationMode::Rotation?"ROTATION":"POSITION")<<" | Num + mode; 4/6 X, 2/8 Y, 7/9 Z; Shift fine; 5 reset\n";}
 }
};
}
