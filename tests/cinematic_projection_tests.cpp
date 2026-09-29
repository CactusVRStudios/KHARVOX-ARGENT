#include "../src/openxr/CinematicProjection.h"
#include "../src/hud/FlatMenuPolicy.h"
#include "../src/hud/TutorialPolicy.h"
#include <iostream>
#include <stdexcept>
using namespace argent;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::array<float,4> mul(const sfs::Matrix& m,std::array<float,4> p){std::array<float,4> r{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)r[i]+=m[j*4+i]*p[j];return r;}
int main(){try{
 check(hud::tutorialKind(0x2d19f98)==0&&hud::tutorialKind(0x2d07f90)==1&&hud::tutorialKind(0x2d08128)==2&&hud::tutorialKind(0x2d11dd8)==-1,"Tutorial class isolation");
 check(hud::flatMenuVtable(0x2d19b00)&&hud::flatMenuVtable(0x2d182d0)&&hud::flatMenuVtable(0x2d0a8c0)&&!hud::flatMenuVtable(0x2d004d8),"Rune/drone/dossier policy");
 check(hud::summaryMenuVtable(0x2d11dd8)&&hud::flatMenuVtable(0x2d11dd8)&&!hud::summaryMenuVtable(0x2d19b00)&&!hud::summaryMenuVtable(0x2d004d8),"Summary identity isolation");
 for(auto extent:{VkExtent2D{1920,1080},VkExtent2D{2082,2122},VkExtent2D{3440,1440}})
 for(float yaw:{0.f,1.2f})for(float x:{-.2f,0.f,.2f})for(float y:{-.15f,.1f})for(float z:{-.25f,0.f,.25f})for(float roll:{0.f,.5f})for(float pitch:{0.f,.4f}){
  sfs::FramePose pose;pose.quadView=pose.headPositionTracked=true;
  pose.quadPose={{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},{3,1.7f,-5}};
  sfs::Matrix plane;check(sfs::poseMatrix(pose.quadPose,1,plane),"Plane pose");
  auto world=[&](float a,float b,float c){auto p=mul(plane,{a,b,c,1});return XrVector3f{p[0],p[1],p[2]};};
  pose.predictedHead={pose.quadPose.orientation,world(x,y,2.5f+z)};
  for(int e=0;e<2;++e){const float eye=e?.032f:-.032f;
   const float rx=std::cos(roll),ry=std::sin(roll)*std::cos(pitch),rz=std::sin(roll)*std::sin(pitch);
   pose.views[e].pose={pose.quadPose.orientation,world(x+eye*rx,y+eye*ry,2.5f+z+eye*rz)};
  }
  sfs::Matrix source{};source[0]=1.1f;source[5]=-1.4f;source[11]=-1;source[14]=.06f;
  sfs::EyeUniforms uniforms;check(cinematicQuadProjection(source,pose,extent,1,uniforms),"Tracked cinematic rejected");
  const auto crop=widescreenQuadRect(extent);const float d=3*source[0]/(2*float(crop.extent.width)/extent.width);
  for(float depth:{d*.5f,d,d*3})for(int e=0;e<2;++e){
   auto p=mul(source,{.2f,.1f,-depth,1}),q=mul(uniforms.clipFromCenter[e],p);
   for(int i=0;i<4;++i)q[i]+=uniforms.eyeTranslation[e][i];
   const float actualX=q[0]/q[3],actualY=q[1]/q[3];
   // Independently intersect the physical eye-to-scene ray with the display.
   const float eye=e?.032f:-.032f;
   // Stabilized cinematic contract: tracked lateral/vertical eyes, reference
   // longitudinal distance. Forward motion is handled by the quad compositor.
   const float ex=x+eye*std::cos(roll),ey=y+eye*std::sin(roll)*std::cos(pitch),ez=2.5f;
   const float cropX=float(crop.extent.width)/extent.width,cropY=float(crop.extent.height)/extent.height;
   const float vx=.2f/(2*d*cropX/(source[0]*3)),vy=.1f/(-2*d*cropY/(source[5]*(3*9.f/16))),vz=(d-depth)/(d/2.5f);
   const float screenX=(ez*vx-vz*ex)/(ez-vz),screenY=(ez*vy-vz*ey)/(ez-vz);
   check(std::abs(actualX-2*screenX/3*cropX)<1e-5f&&std::abs(actualY+2*screenY/(3*9.f/16)*cropY)<1e-5f,"Projection disagrees with physical viewing rays");
   if(depth==d){check(std::abs(actualX-p[0]/p[3])<1e-5f&&std::abs(actualY-p[1]/p[3])<1e-5f,"Screen-plane point moved with headset");}
   const auto& m=uniforms.clipFromCenter[e];const auto& t=uniforms.eyeTranslation[e];
   check(t[3]==0.f&&m[0]==1.f&&m[5]==1.f,"Longitudinal pole reintroduced");
   const float restoredX=(actualX*(1+t[3]/depth)-m[12]-t[0]/depth)/m[0];
   const float restoredY=(actualY*(1+t[3]/depth)-m[13]-t[1]/depth)/m[5];
   check(std::abs(restoredX-p[0]/p[3])<1e-5f&&std::abs(restoredY-p[1]/p[3])<1e-5f,"Shared volume/decal inverse disagrees with geometry");
   // Mirror the depth-preserving shader branch.
   if(t[3]!=0){q[0]*=p[3]/q[3];q[1]*=p[3]/q[3];q[2]=p[2];q[3]=p[3];}
   check(q[2]==p[2]&&q[3]==p[3],"Cinematic projection changed native depth");
  }
  check(uniforms.clipFromCenter[0]!=uniforms.clipFromCenter[1],"Both eyes got mono transforms");
  check(uniforms.screenClip[0]==sfs::identity()&&uniforms.screenClip[1]==sfs::identity(),"Subtitles acquired stereo depth");
  // r106 logged shiftW about -0.265 near the water. Unlike the old transform,
  // clip W must remain positive on both sides of that depth, for both eyes.
  for(float depth:{.001f,.1f,.25f,.265f,.28f,1.f})for(int e=0;e<2;++e){
   const auto p=mul(source,{.01f,.01f,-depth,1});
   auto q=mul(uniforms.clipFromCenter[e],p);
   for(int i=0;i<4;++i)q[i]+=uniforms.eyeTranslation[e][i];
   check(q[3]==p[3]&&q[3]>0&&q[2]==p[2]&&std::isfinite(q[0]/q[3]+q[1]/q[3]),"Near geometry crossed the cinematic projection pole");
  }
  const auto saved=uniforms;pose.quadHeadLocked=true;
  check(!cinematicQuadProjection(source,pose,extent,1,uniforms)&&uniforms.clipFromCenter==saved.clipFromCenter,"Unanchored tracking accepted");
  pose.quadHeadLocked=false;pose.predictedHead.position=world(0,-1,2.5f);
  check(!cinematicQuadProjection(source,pose,extent,1,uniforms),"Floor-level pose jump accepted");
  pose.predictedHead.position=world(0,0,2.5f);pose.headPositionTracked=false;
  check(!cinematicQuadProjection(source,pose,extent,1,uniforms),"Lost position tracking accepted");
  pose.headPositionTracked=true;pose.quadView=false;
  check(!cinematicQuadProjection(source,pose,extent,1,uniforms),"Gameplay captured by cinematic projection");
 }
 std::cout<<"PASS: screen convergence, lateral/vertical parallax, tilted IPD, anchored yaw, crop, native depth, pole-free clip W, inverse reconstruction and tracking guards\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
