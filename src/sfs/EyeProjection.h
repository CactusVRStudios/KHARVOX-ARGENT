#pragma once
#include <openxr/openxr.h>
#include <array>
#include <cmath>
#include <algorithm>
#include "FrameProjection.h"

namespace argent::sfs {
using Matrix=std::array<float,16>; // column-major, GLSL std140
inline Matrix identity(){Matrix m{};for(int i=0;i<4;++i)m[i*5]=1;return m;}
inline Matrix multiply(const Matrix& a,const Matrix& b){Matrix r{};for(int c=0;c<4;++c)for(int row=0;row<4;++row)for(int k=0;k<4;++k)r[c*4+row]+=a[k*4+row]*b[c*4+k];return r;}
inline bool inverse(const Matrix& m,Matrix& result){
    double a[4][8]{};for(int r=0;r<4;++r){for(int c=0;c<4;++c){if(!std::isfinite(m[c*4+r]))return false;a[r][c]=m[c*4+r];}a[r][r+4]=1;}
    for(int c=0;c<4;++c){int pivot=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;if(std::abs(a[pivot][c])<1e-12)return false;for(int j=0;j<8;++j)std::swap(a[c][j],a[pivot][j]);double scale=a[c][c];for(auto& v:a[c])v/=scale;for(int r=0;r<4;++r)if(r!=c){scale=a[r][c];for(int j=0;j<8;++j)a[r][j]-=scale*a[c][j];}}
    Matrix out{};for(int r=0;r<4;++r)for(int c=0;c<4;++c){out[c*4+r]=float(a[r][c+4]);if(!std::isfinite(out[c*4+r]))return false;}result=out;return true;
}
inline bool poseMatrix(const XrPosef& pose,float units,Matrix& result){
    auto q=pose.orientation;double norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if(!std::isfinite(norm)||std::abs(norm-1)>0.001||!std::isfinite(units)||units<=0)return false;
    float x=q.x,y=q.y,z=q.z,w=q.w;Matrix m=identity();
    m[0]=1-2*(y*y+z*z);m[1]=2*(x*y+z*w);m[2]=2*(x*z-y*w);
    m[4]=2*(x*y-z*w);m[5]=1-2*(x*x+z*z);m[6]=2*(y*z+x*w);
    m[8]=2*(x*z+y*w);m[9]=2*(y*z-x*w);m[10]=1-2*(x*x+y*y);
    m[12]=pose.position.x*units;m[13]=pose.position.y*units;m[14]=pose.position.z*units;
    for(auto value:m)if(!std::isfinite(value))return false;result=m;return true;
}
struct alignas(16) EyeUniforms {std::array<Matrix,2> clipFromCenter;std::array<std::array<float,4>,2> eyeTranslation{};std::array<Matrix,2> screenClip{identity(),identity()};std::array<float,4> diagnostics{};};
static_assert(sizeof(EyeUniforms)==304,"Matches ArgentEyeProjection std140 block");
// KHARVOX's parallel-eye contract preserves the engine depth exactly. The
// engine camera must handle head orientation; rotating only selected clip-space
// geometry disagrees with Eternal's culling, reconstruction and lighting.
inline bool parallelEyeProjection(const Matrix& source,const std::array<XrView,2>& eyes,float units,EyeUniforms& output){
    if(!std::isfinite(source[0])||!std::isfinite(source[5])||source[0]<=0||source[5]>=0)return false;
    auto center=eyes[0].pose;
    center.position={(eyes[0].pose.position.x+eyes[1].pose.position.x)*.5f,(eyes[0].pose.position.y+eyes[1].pose.position.y)*.5f,(eyes[0].pose.position.z+eyes[1].pose.position.z)*.5f};
    const float x=std::atan(1/source[0]),y=std::atan(-1/source[5]);
    kharvox::sfs::FrameUniforms frame;
    if(!kharvox::sfs::frameProjection(center,{-x,x,y,-y},eyes,units,true,frame))return false;
    EyeUniforms result{};result.clipFromCenter=frame.clip;result.eyeTranslation=frame.translation;
    output=result;return true;
}
// KHARVOX screen UI policy: asymmetric projection, no metric IPD translation.
// Use the current head reference so menus do not acquire the world camera's
// calibrated rotation. Only recognized UI families use these matrices.
inline bool screenProjection(const Matrix& source,const XrPosef& head,const std::array<XrView,2>& eyes,EyeUniforms& output){
    if(!std::isfinite(source[0])||!std::isfinite(source[5])||source[0]<=0||source[5]>=0)return false;
    const float x=std::atan(1/source[0]),y=std::atan(-1/source[5]);
    kharvox::sfs::FrameUniforms frame;
    if(!kharvox::sfs::frameProjection(head,{-x,x,y,-y},eyes,1,true,frame))return false;
    output.screenClip=frame.clip;return true;
}
// Source camera contract: right-handed view, -Z forward, perspective bottom
// row (0,0,-1,0), Vulkan clip Y down. Preserve the measured source depth terms,
// including reversed/infinite depth; never guess engine FOV, units, or matrices.
// cameraInTrackingSpace is the calibrated source-camera pose, not automatically
// the current head pose (which would cancel the desired head rotation).
inline bool eyeProjection(const Matrix& sourceProjection,const XrPosef& cameraInTrackingSpace,
                          const std::array<XrView,2>& eyes,float unitsPerMeter,EyeUniforms& output){
    Matrix sourceInverse,camera;
    if(!inverse(sourceProjection,sourceInverse)||!poseMatrix(cameraInTrackingSpace,unitsPerMeter,camera))return false;
    const auto& p=sourceProjection;
    for(int i:{1,2,3,4,6,7,12,13,15})if(std::abs(p[i])>1e-6f)return false;
    if(std::abs(p[11]+1)>1e-6f||p[0]<=0||p[5]>=0)return false;
    EyeUniforms result{};
    for(int i=0;i<2;++i){
        Matrix eye,eyeInverse;if(!poseMatrix(eyes[i].pose,unitsPerMeter,eye)||!inverse(eye,eyeInverse))return false;
        auto f=eyes[i].fov;
        for(float angle:{f.angleLeft,f.angleRight,f.angleUp,f.angleDown})if(!std::isfinite(angle)||std::abs(angle)>=1.5707f)return false;
        float l=std::tan(f.angleLeft),r=std::tan(f.angleRight),b=std::tan(f.angleDown),t=std::tan(f.angleUp);
        if(!(r>l&&t>b))return false;
        Matrix projection=p;projection[0]=2/(r-l);projection[5]=-2/(t-b);projection[8]=(r+l)/(r-l);projection[9]=-(t+b)/(t-b);
        result.clipFromCenter[i]=multiply(multiply(multiply(projection,eyeInverse),camera),sourceInverse);
        for(auto value:result.clipFromCenter[i])if(!std::isfinite(value))return false;
    }
    output=result;return true;
}
}
