#pragma once
#include "../sfs/NativeSfs.h"
#include <cstring>
namespace argent {
// Tracking-space anchoring does not require controller/input focus.
inline bool menuAnchorSessionVisible(XrSessionState state){
    return state==XR_SESSION_STATE_VISIBLE||state==XR_SESSION_STATE_FOCUSED;
}
// Match KHARVOX: center-crop the source instead of squeezing the complete eye.
inline XrExtent2Df widescreenQuadSize(float width){return {width,width*9.0f/16.0f};}
inline XrRect2Di widescreenQuadRect(VkExtent2D extent){
 int32_t w=int32_t(extent.width),h=int32_t(extent.height);
 if(w<=0||h<=0)return {};
 int32_t cw=w,ch=h;
 if(int64_t(w)*9<=int64_t(h)*16)ch=std::max(1,int(int64_t(w)*9/16));
 else cw=std::max(1,int(int64_t(h)*16/9));
 return {{(w-cw)/2,(h-ch)/2},{cw,ch}};
}
inline std::array<XrCompositionLayerQuad,2> stereoQuadLayers(const sfs::FramePose& pose,
    const std::array<XrSwapchain,2>& eyes,VkExtent2D extent,XrSpace viewSpace,XrSpace localSpace){
 std::array<XrCompositionLayerQuad,2> result{};
 for(unsigned e=0;e<2;++e){auto& layer=result[e];layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
  layer.space=pose.quadHeadLocked?viewSpace:localSpace;layer.pose=pose.quadPose;
  layer.eyeVisibility=pose.stereoQuad?(e?XR_EYE_VISIBILITY_RIGHT:XR_EYE_VISIBILITY_LEFT):XR_EYE_VISIBILITY_BOTH;
  layer.subImage.swapchain=eyes[e];layer.subImage.imageRect=widescreenQuadRect(extent);layer.size=widescreenQuadSize(3.f);
 }
 return result;
}
// Preserve the rendered local eye offset/FOV, but attach the pixels to the
// head pose consumed by Eternal. xrEndFrame still uses the current displayTime.
inline void rebaseProjectionViews(std::array<XrCompositionLayerProjectionView,2>& views,
                                 XrPosef predicted,XrPosef rendered){
 sfs::Matrix oldHead,newHead,inverseHead;
 if(!sfs::poseMatrix(predicted,1,oldHead)||!sfs::poseMatrix(rendered,1,newHead)||!sfs::inverse(oldHead,inverseHead))return;
 auto transform=sfs::multiply(newHead,inverseHead);
 auto mul=[](XrQuaternionf a,XrQuaternionf b){return XrQuaternionf{a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};};
 auto inverse=predicted.orientation;inverse.x=-inverse.x;inverse.y=-inverse.y;inverse.z=-inverse.z;
 const auto delta=mul(rendered.orientation,inverse);
 for(auto& v:views){auto p=v.pose.position;v.pose.position={transform[0]*p.x+transform[4]*p.y+transform[8]*p.z+transform[12],transform[1]*p.x+transform[5]*p.y+transform[9]*p.z+transform[13],transform[2]*p.x+transform[6]*p.y+transform[10]*p.z+transform[14]};v.pose.orientation=mul(delta,v.pose.orientation);}
}
// Adapted from KHARVOX OpenXRBootstrap's nativeFrame projection submission:
// the submitted poses/FOV belong to the rendered pixels, not a newer prediction.
inline bool validStereoPair(const sfs::StereoFrame& pair,const sfs::FramePose& expected,
                            VkExtent2D extent,VkFormat format){
    if(!expected.serial||pair.generation!=expected.serial||pair.pose.serial!=expected.serial||
       pair.pose.displayTime!=expected.displayTime||pair.pose.quadView!=expected.quadView||pair.pose.stereoQuad!=expected.stereoQuad||!extent.width||!extent.height)return false;
    if(expected.stereoQuad&&(!expected.quadView||expected.quadHeadLocked))return false;
    if(expected.quadView&&(pair.pose.quadHeadLocked!=expected.quadHeadLocked||std::memcmp(&pair.pose.quadPose,&expected.quadPose,sizeof(XrPosef))))return false;
    for(unsigned e=0;e<2;++e){const auto& eye=pair.eyes[e];
        if(!eye.image||eye.serial!=expected.serial||eye.extent.width!=extent.width||
           eye.extent.height!=extent.height||eye.format!=format||
           eye.layout==VK_IMAGE_LAYOUT_UNDEFINED||eye.layout==VK_IMAGE_LAYOUT_PREINITIALIZED)return false;
        if(std::memcmp(&eye.pose,&expected.views[e].pose,sizeof(XrPosef))||
           std::memcmp(&eye.fov,&expected.views[e].fov,sizeof(XrFovf)))return false;
        sfs::Matrix pose;if(!sfs::poseMatrix(eye.pose,1,pose))return false;
        auto f=eye.fov;
        for(float a:{f.angleLeft,f.angleRight,f.angleUp,f.angleDown})
            if(!std::isfinite(a)||std::abs(a)>=1.5707f)return false;
        if(f.angleLeft>=f.angleRight||f.angleDown>=f.angleUp)return false;
    }
    return pair.eyes[0].image!=pair.eyes[1].image||pair.eyes[0].layer!=pair.eyes[1].layer;
}
inline std::array<XrCompositionLayerProjectionView,2> stereoProjectionViews(
    const sfs::StereoFrame& frame,XrSwapchain swapchain,VkExtent2D extent){
    std::array<XrCompositionLayerProjectionView,2> views{};
    for(unsigned e=0;e<2;++e){auto& view=views[e];view.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        view.pose=frame.eyes[e].pose;view.fov=frame.eyes[e].fov;
        view.subImage.swapchain=swapchain;view.subImage.imageArrayIndex=e;
        view.subImage.imageRect.extent={int32_t(extent.width),int32_t(extent.height)};
    }
    return views;
}
}
