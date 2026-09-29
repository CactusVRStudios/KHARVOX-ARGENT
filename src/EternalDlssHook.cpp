#include "EternalBuildProfile.h"
#include "EternalDlssHook.h"
#include "Diagnostics.h"
#include "EternalDlssAbi.h"
#include "EternalCameraHook.h"
#include "sfs/NativeSfs.h"
#include "QuadRuntime.h"
#include "BuildFeatures.h"
#include <MinHook.h>
#include <windows.h>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace argent::dlss {
namespace {
using Result=uint32_t;
constexpr Result success=1;
using Create=Result(__fastcall*)(VkCommandBuffer,uint32_t,void*,void**);
using Release=Result(__fastcall*)(void*);
using Evaluate=Result(__fastcall*)(VkCommandBuffer,void*,void*,const Parameters*);
Create createOriginal{};Release releaseOriginal{};Evaluate evaluateOriginal{};
struct Pair {void* right{};uint64_t serial{},tick{},samples{},windowSamples{},cpuNs{},maxNs{};bool quad{},seen{},stereoQuad{};};
std::mutex mutex;
std::unordered_map<void*,Pair> pairs;
bool installed{};
void fallback(const char* reason){
    if(stereoFailed.exchange(true,std::memory_order_relaxed))return;
    log(std::string("DLSS_STEREO fallback AA=0: ")+reason);
    if constexpr(argent::cleanRelease)return;
    wchar_t path[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
    if(n&&n<32768)std::ofstream(std::filesystem::path(path).parent_path()/"aa-mode.request")<<0;
}
Result __fastcall create(VkCommandBuffer command,uint32_t feature,void* params,void** output){
    const auto left=createOriginal(command,feature,params,output);
    if(feature!=1||left!=success||!output||!*output)return left;
    void* right{};const auto result=createOriginal(command,feature,params,&right);
    if(result!=success||!right||right==*output){fallback("independent feature creation failed");return left;}
    {std::lock_guard<std::mutex> lock(mutex);pairs.emplace(*output,Pair{right});}
    log("DLSS_STEREO created independent eye histories");return left;
}
Result __fastcall release(void* handle){
    void* right{};{std::lock_guard<std::mutex> lock(mutex);auto found=pairs.find(handle);if(found!=pairs.end()){right=found->second.right;pairs.erase(found);}}
    if(right){auto result=releaseOriginal(right);log("DLSS_STEREO released right history result="+std::to_string(result));}
    return releaseOriginal(handle);
}
Result __fastcall evaluate(VkCommandBuffer command,void* handle,void* nativeParams,const Parameters* input){
    const bool timing=extendedLogging();
    const auto start=timing?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    // NGX parameter methods are not thread-safe. Serialize only this pair of
    // feature evaluations, not game draws or command recording in general.
    std::lock_guard<std::mutex> lock(mutex);auto found=pairs.find(handle);
    if(found==pairs.end()||!input){fallback("missing stereo history");return evaluateOriginal(command,handle,nativeParams,input);}
    auto& pair=found->second;std::array<EyeParameters,2> eyes;sfs::FramePose pose;
    const bool valid=prepareEyes(*input,eyes,[&](const auto& resources,auto& out){return sfs::dlssEyeResources(command,resources,out,pose);},false);
    if(!valid){fallback("unsupported eye resource contract");return evaluateOriginal(command,handle,nativeParams,input);}
    const auto tick=GetTickCount64();
    const bool reset=!pair.seen||pair.quad!=pose.quadView||pair.stereoQuad!=pose.stereoQuad||pose.recenterRequested||tick-pair.tick>250||pose.serial<pair.serial||pose.serial>pair.serial+1;
    if(reset)for(auto& eye:eyes)eye.params.set<int>(0x38,1);
    const auto left=evaluateOriginal(command,handle,nativeParams,&eyes[0].params);
    const auto right=left==success?evaluateOriginal(command,pair.right,nativeParams,&eyes[1].params):left;
    if(left!=success||right!=success){fallback("native evaluation failed");return left!=success?left:right;}
    pair.seen=true;pair.serial=pose.serial;pair.quad=pose.quadView;pair.stereoQuad=pose.stereoQuad;pair.tick=tick;
    if(!timing){if(++pair.samples==1)log("DLSS_STEREO evaluated eyes=2 timing=off");return success;}
    const auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
    pair.cpuNs+=ns;pair.maxNs=std::max(pair.maxNs,ns);++pair.samples;++pair.windowSamples;
    if(pair.samples==1||pair.samples%240==0){
        log("DLSS_STEREO evaluated eyes=2 serial="+std::to_string(pose.serial)+" reset="+std::to_string(reset)+
            " input="+std::to_string(input->get<uint32_t>(0x30))+"x"+std::to_string(input->get<uint32_t>(0x34))+
            " cpuMeanMs="+std::to_string(double(pair.cpuNs)/pair.windowSamples/1e6)+" cpuMaxMs="+std::to_string(double(pair.maxNs)/1e6));
        pair.cpuNs=pair.maxNs=pair.windowSamples=0;
    }
    return success;
}
}
bool install() noexcept {try{
    if(installed)return true;
    if(!camera::stats().installed||!sfs::vrEnabled())return false;
    char enabled[8]{};if(GetEnvironmentVariableA("ARGENT_DLSS_STEREO",enabled,8)==1&&enabled[0]=='0')return false;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uint8_t common[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    const uint8_t releaseBytes[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x1d,0x2f,0x91,0xa5,0x04};
    const uint8_t storeReleaseBytes[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x1d,0x6f,0x3f,0xa7,0x04};
    void* targets[]={reinterpret_cast<void*>(base+build::rva(0x2268b30)),reinterpret_cast<void*>(base+build::rva(0x1cc7aa0)),reinterpret_cast<void*>(base+build::rva(0x2268f40))};
    if(std::memcmp(targets[0],common,sizeof(common))||std::memcmp(targets[1],common,sizeof(common))||std::memcmp(targets[2],build::microsoftStore?storeReleaseBytes:releaseBytes,sizeof(releaseBytes))){fallback("entry signature mismatch");return false;}
    void* hooks[]={reinterpret_cast<void*>(&create),reinterpret_cast<void*>(&evaluate),reinterpret_cast<void*>(&release)};
    void** originals[]={reinterpret_cast<void**>(&createOriginal),reinterpret_cast<void**>(&evaluateOriginal),reinterpret_cast<void**>(&releaseOriginal)};
    size_t created=0;
    for(;created<3;++created)if(MH_CreateHook(targets[created],hooks[created],originals[created])!=MH_OK)break;
    if(created!=3){while(created)MH_RemoveHook(targets[--created]);fallback("hook creation failed");return false;}
    for(auto target:targets)MH_QueueEnableHook(target);
    if(MH_ApplyQueued()!=MH_OK){for(auto target:targets){MH_DisableHook(target);MH_RemoveHook(target);}fallback("hook activation failed");return false;}
    installed=true;log(std::string("DLSS_STEREO installed: dual histories, cached single-layer views, no image copies; build=")+(build::microsoftStore?"microsoft-store":"steam"));return true;
}catch(...){fallback("installation exception");return false;}}
}
