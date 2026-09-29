#pragma once
#include "OffhandLayout.h"
#include "../hands/HandHudMask.h"
namespace argent::hud {
inline bool showCalibrationPlaceholder(bool enabled,int selected,int role,bool nativeVisible){
 return enabled&&handRole(role)&&(role==selected||!nativeVisible);
}
inline bool validArtworkRatio(float ratio){return std::isfinite(ratio)&&ratio>=.01f&&ratio<=100.f;}
// Same fit-to-artwork-width and normalized pivot as centerGraphic. The movie's
// transparent padding cancels out; only the drawn height/width ratio remains.
inline kharvox::hands::HandHudPanels placeholderGeometry(const HandPanel& panel,
 const float* grip,const float* hand,float units,float ratio,bool measured,bool selected=false){
 kharvox::hands::HandHudPanels result;
 if(!validArtworkRatio(ratio))return result;
 float origin[3]{},axis[9]{},ex{},ey{};
 if(!handPanelPose(panel,grip,hand,units,1.f,origin,axis,ex,ey))return result;
 auto rect=[&](float x,float y,float w,float h){
  kharvox::hands::HandHudQuad q{};
  for(int i=0;i<4;++i)for(int k=0;k<3;++k)
   q[i][k]=origin[k]+axis[k]*ex*(x+(i&1?w:0))+
    axis[3+k]*ey*(panel.pivotY+(y+(i&2?h:0)-panel.pivotY)*ratio);
  result.push_back(q);
 };
 const float line=selected?.025f:.012f;
 const float t=std::min(line,line*ratio),ty=t/ratio;
 const int segments=measured?1:8;
 for(int n=0;n<segments;++n){const float a=float(n)/segments,b=(measured?1.f:.6f)/segments;
  rect(a,0,b,ty);rect(a,1-ty,b,ty);rect(0,a,t,b);rect(1-t,a,t,b);
 }
 rect(panel.pivotX-.04f,panel.pivotY-ty*.5f,.08f,ty);
 rect(panel.pivotX-t*.5f,panel.pivotY-.04f/ratio,t,.08f/ratio);
 return result;
}
}
