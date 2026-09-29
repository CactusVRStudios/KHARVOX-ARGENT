#pragma once
#include <cstdint>
#include <cmath>
namespace kharvox {
// Supported Eternal PE RTTI: idHUD_WeaponWheel. Never use DOOM 2016 RVAs.
inline bool ownedWeaponWheel(uintptr_t vtable) {
    return vtable==0x2d03ea8;
}
inline constexpr float weaponWheelDistanceMeters=1.0f;
inline constexpr float weaponWheelWidthMeters=2.4f;
// SWF pixel X increases to the viewer's right and Y down. Do not infer
// readability from the transform's Z axis: native GUI winding owns facing.
inline void weaponWheelCanvasAxes(const float* head,float* axis){
    for(int i=0;i<3;++i){
        axis[i]=-head[3+i];
        axis[3+i]=-head[6+i];
        axis[6+i]=head[i];
    }
}
inline bool weaponWheelPose(const float* eye,const float* head,float units,float* center) {
    if(!std::isfinite(units)||units<=0)return false;
    for(int i=0;i<3;++i){
        center[i]=eye[i]+head[i]*weaponWheelDistanceMeters*units;
        if(!std::isfinite(center[i]))return false;
    }
    return true;
}
// Preserve the native canvas handedness and row lengths, remove authored tilt.
inline bool flatWeaponWheelAxis(const float* native,const float* head,float* out) {
    int used=0;
    for(int row=0;row<3;++row){
        float local[3]{},length=0;
        for(int i=0;i<3;++i){
            if(!std::isfinite(native[row*3+i]))return false;
            length+=native[row*3+i]*native[row*3+i];
            for(int j=0;j<3;++j)local[j]+=native[row*3+i]*head[j*3+i];
        }
        length=std::sqrt(length);if(length<.00001f)return false;
        int best=0;for(int j=1;j<3;++j)if(std::abs(local[j])>std::abs(local[best]))best=j;
        if((used&(1<<best)) || (row<2&&best==0))return false;
        used|=1<<best;
        for(int i=0;i<3;++i)out[row*3+i]=head[best*3+i]*(local[best]<0?-length:length);
    }
    return true;
}
// Identify canvas row directions in its native parent frame first. Comparing
// directly to the HMD would swap/reject rows after a large physical head turn.
inline bool headlockedWeaponWheelAxis(const float* native,const float* parent,const float* head,float* out){
    float flat[9]{};
    if(!flatWeaponWheelAxis(native,parent,flat))return false;
    for(int r=0;r<3;++r)for(int c=0;c<3;++c){
        float value=0;
        for(int k=0;k<3;++k){
            float local=0;for(int j=0;j<3;++j)local+=flat[r*3+j]*parent[k*3+j];
            value+=local*head[k*3+c];
        }
        if(!std::isfinite(value))return false;out[r*3+c]=value;
    }
    return true;
}
}
