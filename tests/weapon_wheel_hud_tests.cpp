#include "../src/hud/WeaponWheelHudPolicy.h"
#include "../src/hud/OffhandHudPolicy.h"
#include "../src/hud/HudLayoutPolicy.h"
#include "../src/hud/OffhandLayout.h"
#include "../src/hud/HandHudGroup.h"
#include "../src/hud/HudGeometryPivot.h"
#include "../src/hud/HudHandMaskGeometry.h"
#include "../src/hud/HudDrawScope.h"
#include "../src/hud/HudCalibrationPreview.h"
#include "../src/hud/HudPlaceholder.h"
#include "../src/hud/CompassCalibration.h"
#include "../src/hands/HandVisibilityPolicy.h"
#include <array>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <fstream>
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main(){try{
 {
  using namespace argent::hud;
  auto p=defaultHandPanels();
  const float grip[]{0,0,0},hand[]{1,0,0,0,1,0,0,0,1};
  const float eye[]{-1,0,0},high[]{-1,0,10};
  auto group=handHudGroup(p,false,grip,hand,eye,1);
  std::array<HandHudPivot,2> fixed{};
  std::istringstream pivotFile("1 1 3 4 5 0 0 0 0");
  check(readHandHudPivots(pivotFile,fixed)&&fixed[0].enabled&&!fixed[1].enabled,"Independent pivot config rejected");
  auto anchored=handHudGroup(p,false,grip,hand,eye,1,fixed[0]);
  for(auto& role:p)role[0].pose.centimeters[1]+=10;
  auto shifted=handHudGroup(p,false,grip,hand,eye,1,fixed[0]);
  check(anchored.pivot==shifted.pivot,"Moving elements moved fixed parent");
  check(std::abs(anchored.pivot[0]-.03f)+std::abs(anchored.pivot[1]-.04f)+std::abs(anchored.pivot[2]-.05f)<1e-6f,"Parent centimeter conversion wrong");
  float stationary[]{anchored.sourceCenter[0],anchored.sourceCenter[1],anchored.sourceCenter[2]};anchored.point(stationary);
  for(int k=0;k<3;++k)check(std::abs(stationary[k]-anchored.pivot[k])<1e-6f,"Artwork center missed hand anchor");
  float shiftedCenter[]{shifted.sourceCenter[0],shifted.sourceCenter[1],shifted.sourceCenter[2]};shifted.point(shiftedCenter);
  for(int k=0;k<3;++k)check(std::abs(shiftedCenter[k]-stationary[k])<1e-6f,"Accumulated layout translation detached HUD from hand");
  std::istringstream badPivot("1 1 300 0 0 0 0 0 0");
  check(!readHandHudPivots(badPivot,fixed)&&fixed[0].centimeters[0]==3,"Invalid pivot damaged saved config");
  p=defaultHandPanels();
  check(group.rotation==std::array<float,9>{1,0,0,0,1,0,0,0,1},"Upright HUD rotated");
  for(float angle:{-170.f,-90.f,-30.f,30.f,90.f,170.f})for(bool left:{false,true}){
   const float r=angle*.017453292519943295f,c=std::cos(r),s=std::sin(r);
   const float rolledHand[]{1,0,0,0,c,s,0,-s,c};
   auto rolled=handHudGroup(p,left,grip,rolledHand,eye,1);
   auto elevated=handHudGroup(p,left,grip,rolledHand,high,1);
   float up[]{0,-s,c},normal[]{1,0,0};
   rolled.vector(up);rolled.vector(normal);
   check(std::abs(up[0])+std::abs(up[1])+std::abs(up[2]-1)<1e-5f,"Hand roll left text sideways");
   check(std::abs(normal[0]-1)+std::abs(normal[1])+std::abs(normal[2])<1e-5f,"Roll compensation tilted HUD plane");
   check(rolled.rotation==elevated.rotation,"Camera height tilted billboard");
   float point[]{0,-2*c,-2*s};rolled.point(point);
   check(std::abs(point[0])+std::abs(point[1]+2)+std::abs(point[2])<1e-5f,"Parent lost relative artwork offset");
  }
  const std::array<std::array<float,9>,3> poses{{
   {0,0,1,0,1,0,-1,0,0}, {-1,0,0,0,-1,0,0,0,1}, {0,1,0,-1,0,0,0,0,1}}};
  for(const auto& pose:poses)for(bool left:{false,true})for(float yaw:{0.f,1.f,2.f,3.14f}){
   const float e[]{-std::cos(yaw),-std::sin(yaw),.5f};
   auto facing=handHudGroup(p,left,grip,pose.data(),e,1);
   float normal[]{pose[0],pose[1],pose[2]},up[]{pose[6],pose[7],pose[8]};
   facing.vector(normal);facing.vector(up);
   check(std::abs(normal[0]-std::cos(yaw))+std::abs(normal[1]-std::sin(yaw))+std::abs(normal[2])<1e-5f,"Back/tilted hand showed HUD rear");
   check(std::abs(up[0])+std::abs(up[1])+std::abs(up[2]-1)<1e-5f,"Hand tilt tilted camera billboard");
   float a[]{.12f,-.04f,.08f},b[]{-.1f,.03f,.02f};float before=0,after=0;
   for(int k=0;k<3;++k)before+=(a[k]-b[k])*(a[k]-b[k]);
   facing.point(a);facing.point(b);
   for(int k=0;k<3;++k)after+=(a[k]-b[k])*(a[k]-b[k]);
   check(std::abs(before-after)<1e-5f,"Billboard changed element spacing");
  }
  const float overhead[]{0,0,1};
  auto pole=handHudGroup(p,false,grip,hand,overhead,1);
  check(pole.rotation==group.rotation,"Overhead camera fallback unstable");
  check(handHudGroup(p,false,grip,hand,nullptr,1).rotation==group.rotation,"Missing camera changed parent");
 }
 {
  using namespace argent::hud;
  check(calibratableHudRole(7)&&!handRole(7),"Compass missing from calibration or rebound to hand");
  int role=6;do{role=(role+1)%16;}while(!calibratableHudRole(role));check(role==7,"Selection skips compass");
  auto c=defaultCompassCalibration();check(c.up==-.2f&&c.scale==.5f,"Compass defaults wrong");
  for(float yaw:{0.f,1.2f})for(float pitch:{0.f,.5f})for(float roll:{0.f,.4f})for(float units:{1.f,100.f}){
   const argent::camera::Basis identity{1,0,0,0,1,0,0,0,1};argent::camera::Basis head;
   auto q=argent::camera::product({0,0,std::sin(yaw/2),std::cos(yaw/2)},argent::camera::product({0,std::sin(pitch/2),0,std::cos(pitch/2)},{std::sin(roll/2),0,0,std::cos(roll/2)}));
   check(argent::camera::rotateBasis(identity,{0,0,0,1},q,head),"Compass head fixture failed");
   const float eye[]{3*units,7*units,2*units};float o[3],a[9],w,h;
   check(compassCanvas(eye,head.data(),units,1.2f,.9f,c,o,a,w,h),"Head-locked compass rejected");
   float center[3];for(int k=0;k<3;++k)center[k]=o[k]+a[k]*w*.5f+a[3+k]*h*.5f-eye[k];
   for(int row=0;row<3;++row){float local=0;for(int k=0;k<3;++k)local+=center[k]*head[row*3+k]/units;
    check(std::abs(local-(row==0?1.f:row==2?-.2f:0.f))<1e-5f,"Compass drifts under head rotation/translation");}
   for(int k=0;k<3;++k)check(std::abs(a[k]+head[3+k])<1e-6f&&std::abs(a[3+k]+head[6+k])<1e-6f,"Compass canvas fails to follow head orientation");
   check(!compassCanvas(eye,head.data(),units,0,.9f,c,o,a,w,h),"Invalid projection accepted");
  }
  auto changed=adjustCompass(c,1,-1,1,1,false,false);
  check(changed.right>c.right&&changed.up<c.up&&changed.distance<c.distance&&changed.scale>c.scale,"Compass controls inverted");
  std::stringstream file;writeCompassCalibration(file,changed);Calibration loaded;
  check(readCompassCalibration(file,loaded)&&loaded.up==changed.up&&loaded.right==changed.right&&loaded.distance==changed.distance&&loaded.scale==changed.scale,"Compass save/reload mismatch");
  std::istringstream invalid("1 0 1 0 -0.2");const auto before=loaded;
  check(!readCompassCalibration(invalid,loaded)&&loaded.up==before.up,"Invalid compass settings replaced valid state");
  check(adjustCompass(changed,0,0,0,0,false,true).up==-.2f,"Compass reset does not restore downward offset");
  for(float units:{1.f,100.f}){
   const float eye[]{0,0,0},head[]{1,0,0,0,1,0,0,0,1},origin[]{units,0,units*.5f};
   float moved[3],neutral[3],axis[9],factor;auto zero=c;zero.up=0;
   check(layout(origin,head,eye,head,eye,head,units,c,moved,axis,factor)&&layout(origin,head,eye,head,eye,head,units,zero,neutral,axis,factor),"Compass transform failed");
   check(std::abs((moved[2]-neutral[2])/units+.2f)<1e-5f&&moved[0]==neutral[0]&&moved[1]==neutral[1],"Compass offset not 20 cm down");
  }
 }
 for(bool enabled:{false,true})for(bool visible:{false,true})for(int selected:{1,8})for(int role=-1;role<17;++role){
  check(argent::hud::showCalibrationPlaceholder(enabled,selected,role,visible)==
   (enabled&&argent::hud::handRole(role)&&(selected==role||!visible)),"Missing HUD or selected calibration outline incorrectly filtered");
 }
 check(argent::hud::showCalibrationPlaceholder(true,1,8,false),"Hidden keycard filtered while health selected");
 check(argent::hud::showCalibrationPlaceholder(true,1,1,true),"Visible health suppressed initial calibration indicator");
 for(bool hands:{false,true})for(bool hc:{false,true})for(bool hud:{false,true})
  check(kharvox::hands::rendererRequired(hands,hc,hud)==(hands||hc||hud),"HUD calibration requires visible hand models");
 {
  const float grip[]{3,5,7},hand[]{1,0,0,0,1,0,0,0,1};
  for(float ratio:{.2f,1.f,3.f})for(float pivot:{0.f,.3f,1.f}){
   auto p=argent::hud::defaultHandPanels()[1][0];p.pose.scale=.15f;p.pivotX=pivot;p.pivotY=pivot;
   const auto frame=argent::hud::placeholderGeometry(p,grip,hand,100.f,ratio,true);
   check(frame.size()==6,"Measured placeholder must have border and anchor");
   float width=0,height=0;for(int k=0;k<3;++k){width+=std::pow(frame[0][1][k]-frame[0][0][k],2);height+=std::pow(frame[2][2][k]-frame[2][0][k],2);}
   check(std::abs(std::sqrt(width)-15.f)<.001f&&std::abs(std::sqrt(height)-15.f*ratio)<.001f,"Placeholder metric width/aspect mismatch");
   for(int k=0;k<3;++k)check(std::abs((frame[4][0][k]+frame[4][3][k])*.5f-grip[k])<.001f,"Placeholder pivot does not match grip anchor");
   check(argent::hud::placeholderGeometry(p,grip,hand,100.f,ratio,false).size()==34,"Unknown dimensions must be dashed");
   p.pose.degrees={25,-40,15};p.pose.centimeters={3,-7,9};
   const auto rotated=argent::hud::placeholderGeometry(p,grip,hand,100.f,ratio,true);
   std::array<unsigned char,4*48> vertices{};
   for(int i=0;i<4;++i){const float xy[]{100.f+(i&1?400.f:0.f),200.f+(i&2?400.f*ratio:0.f)};std::memcpy(vertices.data()+i*48,xy,8);}
   check(argent::hud::centerGraphic(vertices.data(),4,{100,200,500,200+400*ratio},3840,2160,pivot,pivot),"Native artwork centering failed");
   float origin[3],axes[9],ex,ey;check(argent::hud::handPanelPose(p,grip,hand,100,3840.f/2160,origin,axes,ex,ey),"Native panel pose failed");
   for(int i=0;i<4;++i){float xy[2];std::memcpy(xy,vertices.data()+i*48,8);for(int k=0;k<3;++k){
    const float expected=origin[k]+axes[k]*xy[0]*ex/3840+axes[3+k]*xy[1]*ey/2160;
    check(std::abs(rotated[i<2?0:1][i][k]-expected)<.001f,"Placeholder differs from real artwork transform");
   }}
  }
  check(!argent::hud::validArtworkRatio(0)&&!argent::hud::validArtworkRatio(std::numeric_limits<float>::quiet_NaN()),"Invalid artwork dimensions accepted");
 }
 for(int role=-1;role<17;++role)for(bool enabled:{false,true})for(bool gameplay:{false,true})for(bool animation:{false,true})for(bool tracked:{false,true}){
  check(argent::hud::calibrationPreviewAllowed(enabled,8,role,gameplay,animation,tracked)==
   (enabled&&role==8&&gameplay&&!animation&&tracked),"Preview escaped selected keycard/gameplay scope");
 }
 for(int role:{0,7,10,12})check(!argent::hud::calibrationPreviewAllowed(true,role,role,true,false,true),"Non-hand role received preview");
 unsigned char visible=0;
 {argent::hud::PreviewValueScope<unsigned char> show(visible,1);check(visible==1,"Preview owner remained hidden");}
 check(visible==0,"Preview leaked native visibility");
 int originalMovie{},previewMovie{};void* movie=&originalMovie;
 try{argent::hud::PreviewValueScope<void*> swap(movie,&previewMovie);check(movie==&previewMovie,"Preview movie not isolated");throw 1;}catch(int){}
 check(movie==&originalMovie,"Preview did not restore native movie");
 check(argent::hud::nativeHudRole(7)&&!argent::hud::handRole(7),"Compass still bound to the hand");
 check(argent::hud::handRole(1)&&argent::hud::handRole(2),"Health/ammo lost hand binding");
 for(bool pose:{false,true}){
  check(argent::hud::hideManagedHud(false,true,true,false,pose),"Live glory kill did not hide hand HUD");
  check(argent::hud::hideManagedHud(false,false,true,true,pose),"Native traversal did not hide hand HUD");
  check(argent::hud::hideManagedHud(false,false,true,false,pose)==!pose,"Missing-pose fallback or restoration failed");
  check(!argent::hud::hideManagedHud(false,false,false,true,pose),"Hand animation suppressed unrelated native HUD");
 }
 check(argent::hud::valid({1,kharvox::weaponWheelWidthMeters,0,0},3.f),"Three-times wheel rejected by calibration");
 check(!argent::hud::valid({1,2.4f,0,0})&&!argent::hud::valid({1,3.1f,0,0},3.f),"Wheel range leaked to other HUD roles");
 check(std::abs(kharvox::weaponWheelWidthMeters/.8f-3.f)<1e-5f,"Wheel is not three times the previous width");
 int health{},ammo{},radar{},menu{};void* hidden=nullptr;
 for(void* swf:{&health,&ammo,&radar}){
  {argent::hud::HudDrawScope scope(hidden,swf,true,true);
   check(hidden==swf,"Animation HUD draw not suppressed");
   {argent::hud::HudDrawScope nested(hidden,&menu,false,true);
    check(hidden==nullptr,"Shared-camera menu suppressed");}
   check(hidden==swf,"Nested render lost HUD suppression");
  }
  check(hidden==nullptr,"HUD suppression leaked outside render");
  {argent::hud::HudDrawScope scope(hidden,swf,true,false);
   check(hidden==nullptr,"HUD not restored after animation");}
 }
 check(kharvox::ownedWeaponWheel(0x2d03ea8)&&!kharvox::ownedWeaponWheel(0x22456f8)&&!kharvox::ownedWeaponWheel(0x2d03ea0),"HUD owner is not Eternal-only");
 const float eye[]{5,7,11};float center[3]{},flat[9]{};
 // Forward/left/up engine basis, including a yaw and a 90-degree head roll.
 const std::array<std::array<float,9>,3> heads{{{1,0,0,0,1,0,0,0,1},{0,1,0,-1,0,0,0,0,1},{0,1,0,0,0,1,1,0,0}}};
 for(const auto& head:heads){
  float wheelAxis[9]{},wheelCenter[3]{},wheelOrigin[3]{},wheelX{},wheelY{};
  kharvox::weaponWheelCanvasAxes(head.data(),wheelAxis);
  for(float units:{1.f,32.f,100.f}){
   for(int i=0;i<3;++i)wheelCenter[i]=eye[i]+units*(head[i]+.18f*head[3+i]);
   for(float aspect:{1.f,16.f/9.f,2.f}){
    check(kharvox::centeredOffhandHud(wheelCenter,wheelAxis,.8f*units,aspect,wheelOrigin,wheelX,wheelY),"Wheel facing pose rejected");
    for(int i=0;i<3;++i)check(std::abs(wheelOrigin[i]+.5f*(wheelAxis[i]*wheelX+wheelAxis[3+i]*wheelY)-wheelCenter[i])<1e-4f,"Wheel rotation changed its metric center");
    check(std::abs(wheelX-.8f*units)<1e-4f,"Wheel rotation changed size");
    float largeOrigin[3]{},largeX{},largeY{};
    check(kharvox::centeredOffhandHud(wheelCenter,wheelAxis,kharvox::weaponWheelWidthMeters*units,aspect,largeOrigin,largeX,largeY),"Larger wheel rejected");
    check(std::abs(largeX-3*wheelX)<1e-4f&&std::abs(largeY-3*wheelY)<1e-4f,"Wheel size ratio changed with aspect/world units");
    for(int i=0;i<3;++i)check(std::abs(largeOrigin[i]+.5f*(wheelAxis[i]*largeX+wheelAxis[3+i]*largeY)-wheelCenter[i])<1e-4f,"Three-times wheel moved its midpoint");
   }
  }
  float facing=0,wheelDown=0,wheelRight=0,determinant=0;
  for(int i=0;i<3;++i){
   facing+=wheelAxis[6+i]*head[i];wheelDown-=wheelAxis[3+i]*head[6+i];
   wheelRight-=wheelAxis[i]*head[3+i];
   determinant+=(wheelAxis[(i+1)%3]*wheelAxis[3+(i+2)%3]-wheelAxis[(i+2)%3]*wheelAxis[3+(i+1)%3])*wheelAxis[6+i];
  }
  check(std::abs(facing-1)<1e-5&&std::abs(wheelRight-1)<1e-5&&std::abs(wheelDown-1)<1e-5&&std::abs(determinant-1)<1e-5,"Wheel artwork mirrored or upside down");
  check(kharvox::weaponWheelPose(eye,head.data(),1,center),"No centered pose");
  for(int i=0;i<3;++i)check(std::abs(center[i]-eye[i]-head[i])<1e-5,"Wheel not one meter forward");
  argent::hud::canvasAxes(head.data(),flat);
  float right=0,down=0;for(int i=0;i<3;++i){right-=flat[i]*head[3+i];down-=flat[3+i]*head[6+i];}
  check(std::abs(right-1)<1e-5&&std::abs(down-1)<1e-5,"HUD mirrored or upside down");
  float native[9]{};
  for(int i=0;i<3;++i){native[i]=-2*head[3+i];native[3+i]=3*head[6+i];native[6+i]=4*head[i];}
  check(kharvox::flatWeaponWheelAxis(native,head.data(),flat),"Valid canvas rejected");
  float origin[3]{},extentX{},extentY{};
  check(kharvox::centeredOffhandHud(center,flat,3,16.f/9,origin,extentX,extentY),"Canvas pivot conversion failed");
  for(int i=0;i<3;++i)check(std::abs(origin[i]+.5f*(flat[i]*extentX+flat[3+i]*extentY)-center[i])<1e-5f,"Visible canvas midpoint is not on camera forward");
  for(int i=0;i<9;++i)check(std::abs(flat[i]-native[i])<1e-5,"Canvas size or handedness changed");
  // Authored tilt: both in-plane axes still have unique nearest head axes.
  for(int i=0;i<3;++i){native[i]=-2*.98f*head[3+i]+2*.2f*head[6+i];native[3+i]=3*.2f*head[3+i]+3*.98f*head[6+i];}
  check(kharvox::flatWeaponWheelAxis(native,head.data(),flat),"Tilt could not be flattened");
  float dot=0;for(int i=0;i<3;++i)dot+=flat[i]*head[6+i];check(std::abs(dot)<1e-5,"Authored canvas tilt survived");
 }
 check(!kharvox::weaponWheelPose(eye,heads[0].data(),0,center),"Invalid scale accepted");
 const float bodyCanvas[]{0,-2,0,0,0,3,4,0,0};
 for(const auto& head:heads){
  check(kharvox::headlockedWeaponWheelAxis(bodyCanvas,heads[0].data(),head.data(),flat),"Large physical yaw/roll rejected canvas");
  for(int i=0;i<3;++i){check(std::abs(flat[i]+2*head[3+i])<1e-5f,"Canvas horizontal axis did not follow HMD");check(std::abs(flat[3+i]-3*head[6+i])<1e-5f,"Canvas vertical axis did not follow HMD");}
 }
 auto invalid=heads[0];invalid[0]=std::numeric_limits<float>::quiet_NaN();
 check(!kharvox::flatWeaponWheelAxis(invalid.data(),heads[0].data(),flat),"Nonfinite canvas accepted");
 const float nativeOrigin[]{15,9,10};float moved[3]{},outAxis[9]{},factor{};
 argent::hud::Calibration calibration;calibration.right=.1f;
 check(argent::hud::layout(nativeOrigin,bodyCanvas,eye,heads[0].data(),eye,heads[1].data(),1,calibration,moved,outAxis,factor),"HUD layout rejected");
 check(std::abs(factor-.07f)<1e-5,"HUD physical/angular scaling wrong");
 check(std::abs(moved[0]-4.96f)<1e-5&&std::abs(moved[1]-8.f)<1e-5&&std::abs(moved[2]-10.93f)<1e-5,"HUD layout did not preserve native offset under head turn");
 check(argent::hud::roleIndex(0x2cfedd8)==1&&argent::hud::roleIndex(0x2d03ea9)==-1,"Unowned HUD accepted");
 check(!argent::hud::valid({0,1,0,0})&&!argent::hud::valid({1,1,INFINITY,0}),"Invalid calibration accepted");
 auto panels=argent::hud::defaultHandPanels();
 std::ifstream defaultsFile(std::filesystem::path(__FILE__).parent_path().parent_path()/"assets/argent_offhand_hud.cfg");
 argent::hud::HandPanels shipped;
 check(argent::hud::readHandPanels(defaultsFile,shipped),"Packaged defaults cannot be loaded");
 // Live calibration may intentionally place the two handedness slots
 // independently. Preserve the saved values through serialization.
 std::stringstream shippedRoundtrip;argent::hud::writeHandPanels(shippedRoundtrip,shipped);
 argent::hud::HandPanels reloaded;
 check(argent::hud::readHandPanels(shippedRoundtrip,reloaded),"Calibrated HUD defaults cannot roundtrip");
 for(size_t role=0;role<shipped.size();++role)for(int hand=0;hand<2;++hand){
  const auto& a=shipped[role][hand],&b=reloaded[role][hand];
  check(a.pose.centimeters==b.pose.centimeters&&a.pose.degrees==b.pose.degrees&&
   a.pose.scale==b.pose.scale&&a.pivotX==b.pivotX&&a.pivotY==b.pivotY,
   "Calibrated HUD changed on roundtrip");
 }
 check(std::abs(shipped[8][0].pose.scale-.08f)<1e-6f,"Calibrated keycard size missing");
 for(int role:{1,2,3,4,5,6,8,9,11,13,14,15}){
  const auto& right=shipped[role][0];const auto& left=shipped[role][1];
  check(right.pose.degrees==shipped[1][0].pose.degrees&&left.pose.degrees==shipped[1][1].pose.degrees,"HUD panels have inconsistent orientations");
  check(std::abs(right.pose.centimeters[0]-left.pose.centimeters[0])<1e-5f&&
   std::abs(right.pose.centimeters[1]+left.pose.centimeters[1])<1e-5f&&
   std::abs(right.pose.centimeters[2]-left.pose.centimeters[2])<1e-5f&&
   right.pose.scale==left.pose.scale&&std::abs(right.pivotX+left.pivotX-1.f)<1e-5f,
   "Left-handed HUD does not match mirrored layout");
 }
 {
  std::ifstream groupFile(std::filesystem::path(__FILE__).parent_path().parent_path()/"assets/argent_hud_group.cfg");
  std::array<argent::hud::HandHudPivot,2> pivots{};
  check(argent::hud::readHandHudPivots(groupFile,pivots),"HUD group pivots cannot be loaded");
  check(pivots[0].enabled&&pivots[1].enabled&&
   std::abs(pivots[0].centimeters[0]-pivots[1].centimeters[0])<.0001f&&
   std::abs(pivots[0].centimeters[1]+pivots[1].centimeters[1])<.0001f&&
   std::abs(pivots[0].centimeters[2]-pivots[1].centimeters[2])<.0001f,
   "Left-hand HUD group pivot is not mirrored");
 }
 std::ifstream groupFile(std::filesystem::path(__FILE__).parent_path().parent_path()/"assets/argent_hud_group.cfg");
 std::array<argent::hud::HandHudPivot,2> shippedPivots{};
 check(argent::hud::readHandHudPivots(groupFile,shippedPivots)&&
  shippedPivots[0].enabled&&shippedPivots[1].enabled&&
  shippedPivots[0].centimeters[0]==shippedPivots[1].centimeters[0]&&
  shippedPivots[0].centimeters[1]==-shippedPivots[1].centimeters[1]&&
  shippedPivots[0].centimeters[2]==shippedPivots[1].centimeters[2],
  "Left HUD parent is not mirrored onto the other offhand");
 // Packaged profiles may contain authored calibration; only factory defaults
 // below are required to be centered at the grip.
 for(const auto& role:shipped)for(const auto& p:role){float o[3]{},a[9]{},ex{},ey{};
  check(argent::hud::handPanelPose(p,eye,heads[0].data(),1,16.f/9,o,a,ex,ey),"Packaged HUD calibration has an invalid pose");
 }
 for(const auto& role:panels)for(const auto& panel:role){
  check(panel.pose.centimeters==std::array<float,3>{0,0,0},"HUD default is not at grip origin");
  for(const auto& basis:heads){float o[3]{},a[9]{},ex{},ey{};
   check(argent::hud::handPanelPose(panel,eye,basis.data(),1,16.f/9,o,a,ex,ey),"Zero default pose rejected");
   for(int i=0;i<3;++i)check(std::abs(o[i]+a[i]*ex*.5f+a[3+i]*ey*.5f-eye[i])<1e-5,"Default graphic anchor differs from grip");
  }
 }

 check(argent::hud::handRole(1)&&argent::hud::handRole(2)&&argent::hud::handRole(13)&&!argent::hud::handRole(0)&&!argent::hud::handRole(12),"Wrong hand-bound roles");
 for(const auto& head:heads)for(int left=0;left<2;++left){
  auto panel=panels[1][left];panel.pivotX=.3f;panel.pivotY=.7f;
  panel.pose.degrees={25,45,15};float origin[3]{},ax[9]{},ex{},ey{};
  check(argent::hud::handPanelPose(panel,eye,head.data(),2,16.f/9,origin,ax,ex,ey),"Offhand transform failed");
  for(int i=0;i<3;++i){float anchor=eye[i];for(int j=0;j<3;++j)anchor+=head[j*3+i]*panel.pose.centimeters[j]*.02f;
   check(std::abs(origin[i]+ax[i]*ex*panel.pivotX+ax[3+i]*ey*panel.pivotY-anchor)<1e-5,"Graphic pivot moved with rotation");}
  panel.pose.scale*=1.5f;float enlarged[3]{},newAx[9]{},nx{},ny{};
  check(argent::hud::handPanelPose(panel,eye,head.data(),2,16.f/9,enlarged,newAx,nx,ny),"Scale failed");
  for(int i=0;i<3;++i)check(std::abs(enlarged[i]+newAx[i]*nx*panel.pivotX+newAx[3+i]*ny*panel.pivotY-origin[i]-ax[i]*ex*panel.pivotX-ax[3+i]*ey*panel.pivotY)<1e-5,"Scaling changed pivot");
 }
 check(panels[1][0].pose.centimeters[1]==-panels[1][1].pose.centimeters[1],"Left mode default not mirrored in position");
 panels[1][1].pose.centimeters[0]=22;std::stringstream saved;argent::hud::writeHandPanels(saved,panels);
 argent::hud::HandPanels loaded;check(argent::hud::readHandPanels(saved,loaded)&&loaded[1][1].pose.centimeters[0]==22&&loaded[1][0].pose.centimeters[0]==0,"Handedness save roundtrip failed");
 std::stringstream corrupt("1 8 0 nan");check(!argent::hud::readHandPanels(corrupt,loaded)&&loaded[1][1].pose.centimeters[0]==22,"Corrupt file overwrote calibration");
 // Real content far from canvas center: a lower-left HUD on a 3840x2160 canvas.
 std::array<unsigned char,8*48> verts{};
 const float xy[8][2]={{100,1700},{500,1700},{500,1900},{100,1900},{0,0},{3840,0},{3840,2160},{0,2160}};
 for(int v=0;v<8;++v){std::memcpy(verts.data()+v*48,xy[v],8);verts[v*48+31]=v<4?255:0;verts[v*48+32]=77;}
 const uint16_t indices[]{0,1,2,0,2,3,4,5,6,4,6,7,0,0,0};
 argent::hud::GraphicBounds bounds;
 check(argent::hud::graphicBounds(verts.data(),8,indices,15,bounds)&&bounds.minX==100&&bounds.maxX==500&&bounds.minY==1700&&bounds.maxY==1900,"Transparent padding affected graphic bounds");
 {
  for(float size:{.35f,.75f}){
   auto compass=verts;
   check(argent::hud::centerGraphicMetric(compass.data(),8,bounds,3840,2160,4.f,size),"Compass graphic placement failed");
   argent::hud::GraphicBounds visible;
   check(argent::hud::graphicBounds(compass.data(),8,indices,15,visible),"Compass graphic disappeared");
   check(std::abs((visible.minX+visible.maxX)*.5f-1920)<.001f&&std::abs((visible.minY+visible.maxY)*.5f-1080)<.001f,"Compass artwork not centered");
   check(std::abs((visible.maxX-visible.minX)/3840*4.f-size)<.0001f,"Compass calibration did not change graphic width");
  }
 }
 for(float resolutionScale:{.5f,1.f,2.f}){
  auto wheel=verts;auto b=bounds;
  b.minX*=resolutionScale;b.maxX*=resolutionScale;b.minY*=resolutionScale;b.maxY*=resolutionScale;
  for(int v=0;v<8;++v){float p[2]{xy[v][0]*resolutionScale,xy[v][1]*resolutionScale};std::memcpy(wheel.data()+v*48,p,8);}
  check(argent::hud::centerGraphic(wheel.data(),8,b,3840*resolutionScale,2160*resolutionScale,.5f,.5f,false),"Wheel artwork centering failed");
  argent::hud::GraphicBounds centered;
  check(argent::hud::graphicBounds(wheel.data(),8,indices,15,centered),"Centered wheel lost geometry");
  check(std::abs((centered.minX+centered.maxX)/2-1920*resolutionScale)<.001f&&std::abs((centered.minY+centered.maxY)/2-1080*resolutionScale)<.001f,"Visible wheel not at canvas midpoint");
  check(std::abs(centered.maxX-centered.minX-400*resolutionScale)<.001f&&std::abs(centered.maxY-centered.minY-200*resolutionScale)<.001f,"Centering resized wheel");
  for(int v=0;v<8;++v)check(!std::memcmp(wheel.data()+v*48+8,verts.data()+v*48+8,40),"Wheel centering modified texture data");
 }
 for(const auto& head:heads)for(int left=0;left<2;++left){
  auto vertices=verts;auto panel=argent::hud::defaultHandPanels()[1][left];panel.pose.degrees={30,60,10};
  check(argent::hud::centerGraphic(vertices.data(),8,bounds,3840,2160,panel.pivotX,panel.pivotY),"Geometry recenter failed");
  float o[3]{},a[9]{},ex{},ey{};check(argent::hud::handPanelPose(panel,eye,head.data(),1,3840.f/2160,o,a,ex,ey),"Graphic pose failed");
  float center[2]{};for(int v=0;v<4;++v){float p[2];std::memcpy(p,vertices.data()+v*48,8);for(int j=0;j<2;++j)center[j]+=p[j]*.25f;}
  for(int j=0;j<3;++j)check(std::abs(o[j]+a[j]*ex*center[0]/3840+a[3+j]*ey*center[1]/2160-eye[j])<1e-5,"Actual artwork rotates around screen center instead of hand");
  for(int v=0;v<8;++v)check(!std::memcmp(vertices.data()+v*48+8,verts.data()+v*48+8,40),"Recenter changed UV/color/Z/material data");
 }
 // Real native surface contract: a second material restarts indices at zero.
 std::array<unsigned char,8*48> multi{};
 for(int v=0;v<8;++v){std::memcpy(multi.data()+v*48,xy[v],8);multi[v*48+31]=255;}
 // Packed normal/tangent alpha at byte 23 is zero even on visible vertices.
 std::array<unsigned char,2*0x88> surfaces{};
 const int ranges[2][4]={{0,4,0,6},{4,4,6,6}};
 for(int n=0;n<2;++n)std::memcpy(surfaces.data()+n*0x88+0x10,ranges[n],16);
 const uint16_t localIndices[]{0,1,2,0,2,3,0,1,2,0,2,3};
 check(argent::hud::submittedGraphicBounds(multi.data(),8,localIndices,12,surfaces.data(),2,bounds)&&bounds.minX==0&&bounds.maxX==3840&&bounds.maxY==2160,"Material-local index bases or native color offset wrong");
 for(int v=4;v<8;++v)multi[v*48+31]=0;
 {
  const float o[]{0,0,0},a[]{1,0,0,0,1,0,0,0,1};
  const auto masks=argent::hud::handMaskGeometry(multi.data(),8,localIndices,12,surfaces.data(),2,o,a,1,1);
  check(masks.size()==2,"Transparent surface generated hand masks");
  for(int i=0;i<2;++i){
   check(masks[i][3]==masks[i][2],"HUD triangle inflated into rectangle");
   for(int c=0;c<3;++c)for(int k=0;k<2;++k)
    check(masks[i][c][k]==xy[localIndices[i*3+c]][k],"HUD mask changed submitted coverage");
  }
 }
 check(argent::hud::submittedGraphicBounds(multi.data(),8,localIndices,12,surfaces.data(),2,bounds)&&bounds.minX==100&&bounds.maxX==500,"Transparent native surface contaminated bounds");
 const int invalidBase=7;std::memcpy(surfaces.data()+0x88+0x10,&invalidBase,4);
 {
  const float o[]{0,0,0},a[]{1,0,0,0,1,0,0,0,1};
  check(argent::hud::handMaskGeometry(multi.data(),8,localIndices,12,surfaces.data(),2,o,a,1,1).empty(),"Invalid surface produced partial masks");
 }
 check(!argent::hud::submittedGraphicBounds(multi.data(),8,localIndices,12,surfaces.data(),2,bounds),"Out-of-range native vertex slice accepted");
 uint16_t bad[]{0,1,99};check(!argent::hud::graphicBounds(verts.data(),8,bad,3,bounds),"Invalid indices accepted");
 auto hiddenVertices=verts;
 check(argent::hud::hideGraphic(hiddenVertices.data(),8),"Animation HUD suppression rejected valid batch");
 check(!argent::hud::graphicBounds(hiddenVertices.data(),8,indices,15,bounds),"Hidden HUD still has drawable triangles");
 for(int v=0;v<8;++v)check(!std::memcmp(hiddenVertices.data()+v*48+12,verts.data()+v*48+12,36),"Suppression changed UV/color/material data");
 auto unchanged=verts;
 check(!argent::hud::hideGraphic(unchanged.data(),0x4000)&&unchanged==verts,"Invalid batch was modified");
 std::cout<<"PASS: Eternal owner, center with yaw/roll, scale, handedness and authored tilt\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
