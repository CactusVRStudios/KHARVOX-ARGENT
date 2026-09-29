#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <istream>
namespace kharvox {
// A render publication may outlive Present by one boundary, but must never
// be replaced with a live physics pose or carried into another level.
struct OffhandHudRenderFrame {
    std::array<float,3> origin{};
    std::array<float,9> axis{};
    uint64_t present{},level{};
    bool valid{};
    bool usable(uint64_t now,uint64_t currentLevel) const {
        return valid&&level==currentLevel&&now>=present&&now-present<=1;
    }
};
struct OffhandHudCalibration {
    std::array<float,3> centimeters{8,0,8};
    std::array<float,3> degrees{}; // pitch, yaw, roll
    float scale{0.40f};
};
inline const char* offhandHudName(int surface){return surface==0?"Life":surface==1?"Ammo":"ProgMeter";}
struct OffhandHudConfig { bool enabled{true};std::array<OffhandHudCalibration,6> modes{}; };
// This one SWF must finish inside its owner Frame: ProgMeter extraction uses
// that frame's pose and geometry scope. All unrelated SWFs retain native jobs.
inline bool drawProgMeterInline(bool enabled,uintptr_t ownerSwf,uintptr_t drawSwf,
    uintptr_t ownerGui,uintptr_t drawGui,int surface){
    return enabled&&ownerSwf&&ownerSwf==drawSwf&&ownerGui&&ownerGui==drawGui&&surface==0;
}
inline bool suppressOffhandHudFallback(bool managed,bool gameplay,bool cinematic,bool ledge,bool syncAttack,bool tracked){
    return managed&&(!gameplay||cinematic||ledge||syncAttack||!tracked);
}
inline int offhandHudSurface(uintptr_t caller,int width,int height,int scale){
    if(caller!=0xbdcf54||width!=512||height!=300)return -1;
    return scale==83?0:scale==100?1:-1;
}
inline int ownedOffhandHudSurface(uintptr_t ownerVtable,uintptr_t caller,int width,int height,int scale){
    const int surface=offhandHudSurface(caller,width,height,scale);
    // Live SP objects: WeaponInfo uses .083; BottomLeft uses .100.
    // Preserve the existing calibration slots, rather than inferring their
    // identities from manager names. Bottom also uses .100 but is not owned.
    return (surface==0&&ownerVtable==0x2240888)
        ||(surface==1&&ownerVtable==0x2240978)?surface:-1;
}
// Measure the nearest corner without changing the hand-bound transform.
inline bool offhandHudNearestDepth(const float* center,const float* axis,float width,float aspect,
    const float* eye,const float* forward,float& nearest){
    if(!std::isfinite(width)||width<=0||!std::isfinite(aspect)||aspect<=0)return false;
    float depth=0,radius=0,norm=0;
    for(int i=0;i<3;++i){if(!std::isfinite(center[i])||!std::isfinite(eye[i])||!std::isfinite(forward[i]))return false;
        depth+=(center[i]-eye[i])*forward[i];norm+=forward[i]*forward[i];}
    if(std::abs(norm-1.f)>.01f)return false;
    for(int row=0;row<2;++row){float length=0,dot=0;
        for(int i=0;i<3;++i){const float v=axis[row*3+i];if(!std::isfinite(v))return false;length+=v*v;dot+=v*forward[i];}
        if(length<1e-10f)return false;
        radius+=.5f*(row?width/aspect:width)*std::abs(dot)/std::sqrt(length);
    }
    nearest=depth-radius;return std::isfinite(nearest);
}
inline bool offhandHudNearVisible(bool wasVisible,float nearest,float minimumDepth,float margin){
    return std::isfinite(nearest)&&nearest>=minimumDepth+(wasVisible?0.f:margin);
}
inline bool readOffhandHudConfig(std::istream& stream,OffhandHudConfig& out){
    int version{},enabled{};OffhandHudConfig value;
    if(!(stream>>version>>enabled)||(version!=1&&version!=2&&version!=3)||(enabled!=0&&enabled!=1))return false;
    for(int i=0;i<(version==1?2:version==2?4:6);++i){auto& mode=value.modes[i];
        for(auto& v:mode.centimeters)if(!(stream>>v)||!std::isfinite(v)||std::abs(v)>100)return false;
        for(auto& v:mode.degrees)if(!(stream>>v)||!std::isfinite(v)||std::abs(v)>180)return false;
        if(!(stream>>mode.scale)||!std::isfinite(mode.scale)||mode.scale<.02f||mode.scale>2)return false;
    }
    if(version==1){value.modes[2]=value.modes[0];value.modes[3]=value.modes[1];}
    if(version<3){
        for(int hand=0;hand<2;++hand){value.modes[4+hand]=value.modes[hand];
            value.modes[4+hand].centimeters[1]=std::clamp(value.modes[4+hand].centimeters[1]+14.f,-100.f,100.f);
            value.modes[4+hand].centimeters[2]=std::clamp(value.modes[4+hand].centimeters[2]+8.f,-100.f,100.f);}
    }
    value.enabled=enabled!=0;out=value;return true;
}
inline void offhandHudBasis(const float* hand,const OffhandHudCalibration& c,float* out){
    std::array<float,9> local{1,0,0,0,1,0,0,0,1};
    // Intrinsic pitch (forward/up), yaw (forward/left), roll (left/up).
    const int pairs[3][2]{{0,2},{0,1},{1,2}};
    for(int r=0;r<3;++r){const float a=c.degrees[r]*.01745329251994329577f;
        const float cs=std::cos(a),sn=std::sin(a);const int x=pairs[r][0],y=pairs[r][1];
        for(int j=0;j<3;++j){const float u=local[x*3+j],v=local[y*3+j];local[x*3+j]=cs*u+sn*v;local[y*3+j]=-sn*u+cs*v;}}
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){out[i*3+j]=0;for(int k=0;k<3;++k)out[i*3+j]+=local[i*3+k]*hand[k*3+j];}
}
inline void offhandHudOrigin(const float* grip,const float* hand,const float* panel,
    const OffhandHudCalibration& c,int surface,float unitsPerMeter,float* out){
    for(int i=0;i<3;++i){out[i]=grip[i];
        for(int j=0;j<3;++j)out[i]+=hand[j*3+i]*c.centimeters[j]*unitsPerMeter*.01f;
        out[i]+=hand[3+i]*(surface==0?1.f:surface==1?-1.f:0.f)*14.f*unitsPerMeter*.01f;}
}
// Final GUI uses local X/Y pixels multiplied by its physical extents. Center
// the complete canvas after those extents are known, independent of rotation.
inline bool centeredOffhandHud(const float* center,const float* axis,float width,
    float aspect,float* origin,float& extentX,float& extentY){
    float lengths[2]{};
    for(int row=0;row<2;++row){for(int j=0;j<3;++j){const float v=axis[row*3+j];if(!std::isfinite(v))return false;lengths[row]+=v*v;}lengths[row]=std::sqrt(lengths[row]);}
    if(!std::isfinite(width)||width<=0||!std::isfinite(aspect)||aspect<=0||lengths[0]<.00001f||lengths[1]<.00001f)return false;
    extentX=width/lengths[0];extentY=width/aspect/lengths[1];
    for(int i=0;i<3;++i){if(!std::isfinite(center[i]))return false;origin[i]=center[i]-.5f*(axis[i]*extentX+axis[3+i]*extentY);}
    return true;
}

}
