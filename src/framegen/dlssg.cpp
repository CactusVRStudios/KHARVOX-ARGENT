// NGX wrapper adapted through KHARVOX CIRCLE (MIT).
// Copyright (c) 2026 dariulone. See third-party/framegen/ngx-wrapper-LICENSE.txt.
// ARGENT supplies the matched native camera and the actual SFS eye projection.
#include "dlssg.hpp"
#include "options.hpp"
#include "community.hpp"
#include <filesystem>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <nvsdk_ngx_helpers_dlssg_d3d.h>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <cmath>
namespace argent::fg {
namespace {std::mutex mutex;ID3D12Device* initialized{};Capability capability;
std::string error(const char* what,NVSDK_NGX_Result r){char b[160];sprintf_s(b,"%s: NGX 0x%08X",what,unsigned(r));return b;}}
Capability initialize(ID3D12Device* device,const wchar_t* runtimeDirectory){
 std::lock_guard lock(mutex);
 if(initialized==device)return capability;
 // Never reinitialize a second device in an existing NGX session: requires
 // explicit lifecycle validation. Do not shut down Eternal's NGX instance.
 if(initialized)return {false,0,0,"D3D12 device changed; restart required for NGX"};
 if(communityRequested()){
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  DXGI_ADAPTER_DESC1 desc{};
  if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter)))||FAILED(adapter->GetDesc1(&desc))||desc.VendorId!=0x10de)
   return {false,0,0,"Community DLSS requires an NVIDIA RTX adapter"};
  std::string reason;
  if(!loadCommunityRuntime(desc.VendorId,desc.Description,reason))return {false,0,0,reason};
 }
 initialized=device;
 const wchar_t* paths[]={runtimeDirectory};NVSDK_NGX_FeatureCommonInfo info{};
 info.PathListInfo.Path=paths;info.PathListInfo.Length=1;
 wchar_t data[MAX_PATH]{};GetTempPathW(MAX_PATH,data);
 auto r=NVSDK_NGX_D3D12_Init_with_ProjectID(ProjectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,"KHARVOX ARGENT",data,device,&info);
 capability.result=unsigned(r);
 if(NVSDK_NGX_FAILED(r)){capability.reason=error("initialize",r);return capability;}
 NVSDK_NGX_Parameter* p{};r=NVSDK_NGX_D3D12_GetCapabilityParameters(&p);
 if(NVSDK_NGX_FAILED(r)||!p){capability.reason=error("capabilities",r);return capability;}
 int supported=0,driver=0;p->Get(NVSDK_NGX_Parameter_FrameGeneration_Available,&supported);
 p->Get(NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver,&driver);
 p->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax,&capability.frames);
 NVSDK_NGX_D3D12_DestroyParameters(p);
 capability.available=supported!=0&&!driver;
 if(!capability.available)capability.reason=driver?"NVIDIA driver update required":"DLSS-G unavailable on this adapter/runtime";
 return capability;
}
struct Feature {NVSDK_NGX_Handle* handle{};NVSDK_NGX_Parameter* parameters{};};
void FeatureDeleter::operator()(Feature* p)const{
 if(!p)return;
 // Caller must have completed its GPU fence before release.
 if(p->handle)NVSDK_NGX_D3D12_ReleaseFeature(p->handle);
 if(p->parameters)NVSDK_NGX_D3D12_DestroyParameters(p->parameters);
 delete p;
}
FeaturePtr create(ID3D12GraphicsCommandList* list,uint32_t w,uint32_t h,uint32_t rw,uint32_t rh,DXGI_FORMAT color,std::string& reason){
 FeaturePtr f(new Feature);
 auto r=NVSDK_NGX_D3D12_AllocateParameters(&f->parameters);
 if(NVSDK_NGX_FAILED(r)){reason=error("parameters",r);return {};}
 NVSDK_NGX_DLSSG_Create_Params p{};p.Width=w;p.Height=h;p.RenderWidth=rw;p.RenderHeight=rh;p.NativeBackbufferFormat=color;p.DynamicResolutionScaling=false;
 r=NGX_D3D12_CREATE_DLSSG(list,1,1,&f->handle,f->parameters,&p);
 if(NVSDK_NGX_FAILED(r)||!f->handle){reason=error("create",r);return {};}
 return f;
}
bool evaluate(Feature& f,ID3D12GraphicsCommandList* list,const Evaluation& e,std::string& reason){
 const auto& c=e.camera;
 if(c.motion3d||c.orthographic||!c.cameraMotion||!std::isfinite(c.aspect)||c.aspect<=0){reason="Unsupported camera/MV convention";return false;}
 NVSDK_NGX_D3D12_DLSSG_Eval_Params p{};p.pBackbuffer=e.color;p.pDepth=e.depth;p.pMVecs=e.motion;p.pOutputInterpFrame=e.output;
 NVSDK_NGX_DLSSG_Opt_Eval_Params o{};
 memcpy(o.cameraViewToClip,c.viewToClip.data(),64);memcpy(o.clipToCameraView,c.clipToView.data(),64);
 memcpy(o.clipToPrevClip,c.clipToPrev.data(),64);memcpy(o.prevClipToClip,c.prevToClip.data(),64);memcpy(o.clipToLensClip,c.lens.data(),64);
 o.jitterOffset[0]=c.jitter[0];o.jitterOffset[1]=c.jitter[1];
 auto mv=e.motion->GetDesc(),depth=e.depth->GetDesc(),color=e.color->GetDesc();
 o.mvecScale[0]=c.motionScale[0]*float(mv.Width);o.mvecScale[1]=c.motionScale[1]*float(mv.Height);
 memcpy(o.cameraPos,c.position.data(),12);memcpy(o.cameraUp,c.up.data(),12);memcpy(o.cameraRight,c.right.data(),12);memcpy(o.cameraFwd,c.forward.data(),12);
 o.cameraNear=c.nearPlane;o.cameraFar=c.farPlane;o.cameraFOV=c.fov;o.cameraAspectRatio=c.aspect;
 o.colorBuffersHDR=false;o.depthInverted=c.inverted!=0;o.cameraMotionIncluded=c.cameraMotion!=0;o.reset=e.reset||c.reset;
 o.motionVectorsInvalidValue=c.invalidMotion;o.motionVectorsDilated=c.dilated!=0;o.menuDetectionEnabled=false;
 o.multiFrameCount=e.count;o.multiFrameIndex=e.index;
 o.backbufferSubrectSize={unsigned(color.Width),color.Height};o.outputInterpSubrectSize=o.backbufferSubrectSize;
 o.mvecsSubrectSize={unsigned(mv.Width),mv.Height};o.depthSubrectSize={unsigned(depth.Width),depth.Height};
 auto r=NGX_D3D12_EVALUATE_DLSSG(list,f.handle,f.parameters,&p,&o);
 if(NVSDK_NGX_FAILED(r)){reason=error("evaluate",r);return false;}return true;
}
}
