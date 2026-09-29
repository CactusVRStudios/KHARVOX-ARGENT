#pragma once
#include <openxr/openxr.h>
#include <array>
#include <cmath>

namespace kharvox::sfs {
struct alignas(16) FrameUniforms {
    struct LegacyEye {std::array<float,4> stereo{},custom{};};
    std::array<LegacyEye,2> legacy{};
    std::array<std::array<float,16>,2> clip{};
    std::array<std::array<float,4>,2> translation{};
    std::array<std::array<float,16>,2> previousClip{};
    std::array<std::array<float,4>,2> previousTranslation{};
    FrameUniforms(){for(auto& m:clip)for(unsigned i=0;i<4;++i)m[i*5]=1;previousClip=clip;}
};
static_assert(sizeof(FrameUniforms)==384,"std140 SFS frame layout");
// Exact x/y projection for parallel eye cameras. Canted/longitudinal eye offsets
// need the game's depth projection, and must not silently use this approximation.
inline bool frameProjection(const XrPosef& center,const XrFovf& source,
                            const std::array<XrView,2>& eyes,float unitsPerMeter,
                            bool world,FrameUniforms& output){
    FrameUniforms result;
    if(!world){output=result;return true;}
    if(!std::isfinite(unitsPerMeter)||unitsPerMeter<=0)return false;
    const float sourceX=std::tan(source.angleRight)-std::tan(source.angleLeft);
    const float sourceY=std::tan(source.angleUp)-std::tan(source.angleDown);
    if(!(sourceX>0&&sourceY>0)||!std::isfinite(sourceX+sourceY))return false;
    const auto q=center.orientation;
    const auto rotateInverse=[&](XrVector3f v){
        const XrVector3f u{-q.x,-q.y,-q.z};
        const XrVector3f t{2*(u.y*v.z-u.z*v.y),2*(u.z*v.x-u.x*v.z),2*(u.x*v.y-u.y*v.x)};
        return XrVector3f{v.x+q.w*t.x+u.y*t.z-u.z*t.y,v.y+q.w*t.y+u.z*t.x-u.x*t.z,v.z+q.w*t.z+u.x*t.y-u.y*t.x};
    };
    for(unsigned e=0;e<2;++e){
        const auto orientation=eyes[e].pose.orientation;
        const float dot=q.x*orientation.x+q.y*orientation.y+q.z*orientation.z+q.w*orientation.w;
        if(!std::isfinite(dot)||std::abs(dot)<0.99999f)return false;
        const auto p=eyes[e].pose.position;
        const auto offset=rotateInverse({p.x-center.position.x,p.y-center.position.y,p.z-center.position.z});
        if(!std::isfinite(offset.x+offset.y+offset.z)||std::abs(offset.z)>0.0005f)return false;
        const float l=std::tan(eyes[e].fov.angleLeft),r=std::tan(eyes[e].fov.angleRight);
        const float b=std::tan(eyes[e].fov.angleDown),t=std::tan(eyes[e].fov.angleUp);
        if(!(r>l&&t>b)||!std::isfinite(l+r+b+t))return false;
        auto& m=result.clip[e];m[0]=sourceX/(r-l);m[5]=sourceY/(t-b);
        m[12]=((std::tan(source.angleRight)+std::tan(source.angleLeft))-(r+l))/(r-l);
        m[13]=((t+b)-(std::tan(source.angleUp)+std::tan(source.angleDown)))/(t-b);
        result.translation[e][0]=-2*offset.x*unitsPerMeter/(r-l);
        result.translation[e][1]= 2*offset.y*unitsPerMeter/(t-b);
        // Affine clip displacement: slope * clipW + intercept. Unlike the
        // fixed-display separation/convergence product, this supports parallel
        // VR projection (zero slope, nonzero eye translation) without division.
        result.legacy[e].stereo[0]=m[12];
        result.legacy[e].stereo[2]=result.translation[e][0];

    }
    output=result;return true;
}
}
