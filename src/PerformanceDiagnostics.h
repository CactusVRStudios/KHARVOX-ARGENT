#pragma once
#include "Diagnostics.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace argent { void log(const std::string&); }
namespace argent::perf {
inline bool enabled(){if constexpr(cleanRelease)return false;static const bool value=environmentFlag("ARGENT_PERFORMANCE_DIAGNOSTICS");return value;}
// Shared by the XR and SFS translation units. Modes differ only in diagnostics.
inline std::atomic<unsigned> mode{};
inline std::atomic<uint64_t> frame{};
inline std::atomic<bool> fovComparison{};
inline std::atomic<int> fovPhase{-1},engineFov{};
inline unsigned scheduledMode(uint64_t elapsedMs,int overrideMode){return overrideMode>=0&&overrideMode<=2?unsigned(overrideMode):unsigned((elapsedMs/8000)%3);}
inline bool sample(uint64_t serial,unsigned selected){return selected>0&&serial>0&&serial%16==0;}
inline std::filesystem::path directory(){wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);return n&&n<32768?std::filesystem::path(path).parent_path():std::filesystem::path{};}
inline void nextFrame(uint64_t serial,bool quad){
 if(!enabled())return;
 static const auto start=GetTickCount64();static uint64_t checked{};static int overrideMode=-1,last=-1;
 const auto now=GetTickCount64();
 if(!checked||now-checked>=1000){checked=now;std::ifstream in(directory()/L"test-perf-mode");int requested=-1;if(!(in>>requested)||requested < -1||requested>2)requested=-1;overrideMode=requested;}
 const auto selected=fovComparison.load(std::memory_order_relaxed)?1u:scheduledMode(now-start,overrideMode);mode.store(selected,std::memory_order_relaxed);frame.store(serial,std::memory_order_relaxed);
 if(last!=int(selected)){last=int(selected);log("PERF_MODE mode="+std::to_string(selected)+" serial="+std::to_string(serial)+" context="+(quad?"quad":"world")+" automatic="+std::to_string(overrideMode<0)+" periodSeconds=8 sampleStride=16 legacyHookTiming=0 checkpoints=0");}
}
}
