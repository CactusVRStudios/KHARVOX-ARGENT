#pragma once
#include "StereoProjection.h"
namespace argent {
// An authored camera looking through a fixed display, not the gameplay HMD
// camera. Screen-plane points stay fixed; only genuine scene depth has parallax.
inline bool cinematicQuadProjection(const sfs::Matrix& source,const sfs::FramePose& pose,
                                   VkExtent2D extent,float units,sfs::EyeUniforms& output){
 if(!pose.quadView||pose.quadHeadLocked||!pose.headPositionTracked||
    !std::isfinite(units)||units<=0||!std::isfinite(source[0]+source[5])||source[0]<=0||source[5]>=0)return false;
 const auto crop=widescreenQuadRect(extent);if(!crop.extent.width||!crop.extent.height)return false;
 sfs::Matrix plane,inversePlane;
 if(!sfs::poseMatrix(pose.quadPose,1,plane)||!sfs::inverse(plane,inversePlane))return false;
 auto local=[&](XrVector3f p){return XrVector3f{
  inversePlane[0]*p.x+inversePlane[4]*p.y+inversePlane[8]*p.z+inversePlane[12],
  inversePlane[1]*p.x+inversePlane[5]*p.y+inversePlane[9]*p.z+inversePlane[13],
  inversePlane[2]*p.x+inversePlane[6]*p.y+inversePlane[10]*p.z+inversePlane[14]};};
 const auto head=local(pose.predictedHead.position);
 // No free flight around the cinematic camera, including floor-level tracking
 // jumps. Outside the seated viewing volume, keep the authored flat picture.
 if(!std::isfinite(head.x+head.y+head.z)||std::abs(head.x)>.5f||std::abs(head.y)>.35f||std::abs(head.z-2.5f)>.5f)return false;
 const auto size=widescreenQuadSize(3.f);
 const float cropX=float(crop.extent.width)/extent.width,cropY=float(crop.extent.height)/extent.height;
 const float convergence=size.width*source[0]*units/(2*cropX);
 if(!std::isfinite(convergence)||convergence<=0)return false;
 sfs::EyeUniforms result{{sfs::identity(),sfs::identity()},{}};
 for(int e=0;e<2;++e){
  sfs::Matrix eye;if(!sfs::poseMatrix(pose.views[e].pose,1,eye))return false;
  const auto p=local(pose.views[e].pose.position);
  if(!std::isfinite(p.x+p.y+p.z)||p.z<1.8f||p.z>3.2f||std::abs(p.x-head.x)>.12f||std::abs(p.y-head.y)>.12f||std::abs(p.z-head.z)>.12f)return false;
  auto& m=result.clipFromCenter[e];auto& t=result.eyeTranslation[e];
  // Keep the authored clip W. A longitudinal eye shift can move its pole
  // into visible geometry; normalizing XY per vertex then disagrees with
  // depth interpolation and the native water/volume reconstruction passes.
  // Until those passes share a full depth transform, use parallel eyes at
  // the reference viewing distance. The compositor still tracks the quad.
  m[0]=m[5]=1.f;
  m[12]=2*p.x*cropX/size.width;m[13]=-2*p.y*cropY/size.height;
  t[0]=-m[12]*convergence;t[1]=-m[13]*convergence;
  t[3]=0.f;
 }
 // Native depth, clip W and perspective interpolation remain authored.
 output=result;return true;
}
}
