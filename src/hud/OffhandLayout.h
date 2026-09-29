#pragma once
#include "HudLayoutPolicy.h"
#include "OffhandHudPolicy.h"
#include <sstream>
#include <iomanip>
namespace argent::hud {
inline bool nativeHudRole(int role){return role==7;} // Compass remains independent of hand binding.
inline bool handRole(int role){return !nativeHudRole(role)&&((role>=1&&role<=9)||role==11||(role>=13&&role<=15));}
inline bool calibratableHudRole(int role){return handRole(role)||nativeHudRole(role);}
struct HandPanel {
 kharvox::OffhandHudCalibration pose{};
 // Normalized center of the artwork, independently adjustable if the SWF
 // includes transparent padding. Rotation never changes this anchor.
 float pivotX=.5f,pivotY=.5f;
};
using HandPanels=std::array<std::array<HandPanel,2>,roles.size()>;
inline HandPanels defaultHandPanels(){
 HandPanels panels{};
 for(size_t r=0;r<roles.size();++r)for(int left=0;left<2;++left){
  auto& p=panels[r][left];
  // Calibration baseline: every graphic anchor starts at the tracked grip.
  // Keep both handedness slots independent, without an authored layout offset.
  p.pose.centimeters={0,0,0};p.pose.degrees={0,0,0};p.pose.scale=.15f;

 }
 return panels;
}
inline bool validPanel(const HandPanel& p){
 for(float v:p.pose.centimeters)if(!std::isfinite(v)||std::abs(v)>100)return false;
 for(float v:p.pose.degrees)if(!std::isfinite(v)||std::abs(v)>180)return false;
 return std::isfinite(p.pose.scale)&&p.pose.scale>=.02f&&p.pose.scale<=2&&
  std::isfinite(p.pivotX)&&std::isfinite(p.pivotY)&&p.pivotX>=0&&p.pivotX<=1&&p.pivotY>=0&&p.pivotY<=1;
}
inline bool readHandPanels(std::istream& in,HandPanels& output){
 int version{};if(!(in>>version)||version!=1)return false;auto next=defaultHandPanels();
 for(auto& role:next)for(auto& p:role){
  for(auto& v:p.pose.centimeters)if(!(in>>v))return false;
  for(auto& v:p.pose.degrees)if(!(in>>v))return false;
  if(!(in>>p.pose.scale>>p.pivotX>>p.pivotY)||!validPanel(p))return false;
 }
 output=next;return true;
}
inline void writeHandPanels(std::ostream& out,const HandPanels& panels){
 out<<std::setprecision(9)<<"1\n";
 for(const auto& role:panels)for(const auto& p:role){for(float v:p.pose.centimeters)out<<v<<' ';for(float v:p.pose.degrees)out<<v<<' ';out<<p.pose.scale<<' '<<p.pivotX<<' '<<p.pivotY<<'\n';}
}
inline bool handPanelPose(const HandPanel& p,const float* grip,const float* hand,float units,float aspect,
 float* origin,float* axis,float& ex,float& ey){
 if(!validPanel(p)||!std::isfinite(units)||units<=0)return false;
 float rotated[9]{},center[3]{};kharvox::offhandHudBasis(hand,p.pose,rotated);canvasAxes(rotated,axis);
 for(int i=0;i<3;++i){center[i]=grip[i];for(int j=0;j<3;++j)center[i]+=hand[j*3+i]*p.pose.centimeters[j]*units*.01f;}
 if(!kharvox::centeredOffhandHud(center,axis,p.pose.scale*units,aspect,origin,ex,ey))return false;
 for(int i=0;i<3;++i)origin[i]+=(.5f-p.pivotX)*axis[i]*ex+(.5f-p.pivotY)*axis[3+i]*ey;
 return true;
}
}
