#pragma once
#include <openxr/openxr.h>
#include <cmath>
#include <array>
namespace argent::camera {
using Basis=std::array<float,9>;
inline bool validPosition(XrVector3f p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
// KHARVOX room-space convention: XR right/up/back -> engine forward/left/up.
// Use yaw only for translation, so looking up/down never tilts physical height.
// This moves the view; player-physics/collision reconciliation is separate.
inline bool translatePosition(const float* position,const Basis& basis,XrPosef reference,
                              XrPosef head,float unitsPerMeter,std::array<float,3>& output);
inline bool validBasis(const Basis& b){
 for(float v:b)if(!std::isfinite(v))return false;
 for(int i=0;i<3;++i)for(int j=0;j<3;++j){float dot=0;for(int k=0;k<3;++k)dot+=b[i*3+k]*b[j*3+k];if(std::abs(dot-(i==j?1.f:0.f))>.02f)return false;}
 const float det=b[0]*(b[4]*b[8]-b[5]*b[7])-b[1]*(b[3]*b[8]-b[5]*b[6])+b[2]*(b[3]*b[7]-b[4]*b[6]);
 return std::abs(det-1.f)<.03f;
}
inline bool validQuaternion(XrQuaternionf q){float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&std::abs(n-1.f)<.002f;}
inline bool translatePosition(const float* position,const Basis& basis,XrPosef reference,
                              XrPosef head,float unitsPerMeter,std::array<float,3>& output){
 if(!position||!validBasis(basis)||!validQuaternion(reference.orientation)||
    !validPosition(reference.position)||!validPosition(head.position)||
    !std::isfinite(unitsPerMeter)||unitsPerMeter<=0)return false;
 for(int i=0;i<3;++i)if(!std::isfinite(position[i]))return false;
 auto q=reference.orientation;
 const float yaw=std::atan2(2*(q.w*q.y+q.x*q.z),1-2*(q.x*q.x+q.y*q.y));
 const float dx=head.position.x-reference.position.x,dz=head.position.z-reference.position.z;
 const float right=std::cos(yaw)*dx-std::sin(yaw)*dz;
 const float back=std::sin(yaw)*dx+std::cos(yaw)*dz;
 const float length=std::hypot(basis[0],basis[1]);
 if(length<.05f)return false;
 const float fx=basis[0]/length,fy=basis[1]/length;
 output={position[0]+unitsPerMeter*(-back*fx+right*fy),
         position[1]+unitsPerMeter*(-back*fy-right*fx),
         position[2]+unitsPerMeter*(head.position.y-reference.position.y)};
 return validPosition({output[0],output[1],output[2]});
}
inline XrQuaternionf product(XrQuaternionf a,XrQuaternionf b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
// OpenXR: right/up/back. id camera basis rows: forward/left/up.
// Compose the relative HMD rotation in camera-local space; preserve body aim.
inline bool rotateBasis(const Basis& input,XrQuaternionf reference,XrQuaternionf head,Basis& output){
 if(!validBasis(input)||!validQuaternion(reference)||!validQuaternion(head))return false;
 reference.x=-reference.x;reference.y=-reference.y;reference.z=-reference.z;
 auto q=product(reference,head);float x=-q.z,y=-q.x,z=q.y,w=q.w;
 float r[9]={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
 Basis result{};for(int row=0;row<3;++row)for(int c=0;c<3;++c)for(int k=0;k<3;++k)result[row*3+c]+=r[k*3+row]*input[k*3+c];
 if(!validBasis(result))return false;output=result;return true;
}
inline XrVector3f rotateVector(XrQuaternionf q,XrVector3f p){
 const auto r=product(product(q,{p.x,p.y,p.z,0}),{-q.x,-q.y,-q.z,q.w});
 return {r.x,r.y,r.z};
}
inline XrVector3f worldPointInTracking(const std::array<float,3>& point,
 const std::array<float,3>& eye,const Basis& camera,XrPosef head,float units){
 float d[3]{};for(int r=0;r<3;++r)for(int k=0;k<3;++k)d[r]+=(point[k]-eye[k])*camera[r*3+k]/units;
 const auto p=rotateVector(head.orientation,{-d[1],d[2],-d[0]});
 return {head.position.x+p.x,head.position.y+p.y,head.position.z+p.z};
}
// Convert the actual engine placement back into the rendered camera's tracking
// space. Raw XR grips alone omit body movement between weapon and camera updates.
inline bool worldPoseInTracking(const std::array<float,3>& point,const Basis& axes,
 const std::array<float,3>& eye,const Basis& camera,XrPosef head,float units,XrPosef& out){
 if(!validBasis(axes)||!validBasis(camera)||!validQuaternion(head.orientation)||!std::isfinite(units)||units<=0)return false;
 const int index[3]={1,2,0};const float sign[3]={-1,1,-1};float m[3][3]{};
 for(int r=0;r<3;++r)for(int c=0;c<3;++c)for(int k=0;k<3;++k)
  m[r][c]+=sign[r]*sign[c]*camera[index[r]*3+k]*axes[index[c]*3+k];
 XrQuaternionf q{};const float trace=m[0][0]+m[1][1]+m[2][2];
 if(trace>0){const float s=2*std::sqrt(trace+1);q={(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s*.25f};}
 else {int i=m[1][1]>m[0][0]?1:0;if(m[2][2]>m[i][i])i=2;const int j=(i+1)%3,k=(i+2)%3;
  const float s=2*std::sqrt(1+m[i][i]-m[j][j]-m[k][k]);float v[3]{};v[i]=s*.25f;v[j]=(m[i][j]+m[j][i])/s;v[k]=(m[i][k]+m[k][i])/s;
  q={v[0],v[1],v[2],(m[k][j]-m[j][k])/s};}
 XrPosef next{product(head.orientation,q),worldPointInTracking(point,eye,camera,head,units)};
 if(!validPosition(next.position)||!validQuaternion(next.orientation))return false;out=next;return true;
}
}
