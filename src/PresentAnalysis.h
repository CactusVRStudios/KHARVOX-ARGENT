#pragma once
#include "FrameTiming.h"
#include "PerformanceDiagnostics.h"
#include <array>
#include <algorithm>
#include <sstream>
namespace argent {
// Per-frame records are buffered and emitted as one line per second. These
// are CPU entry intervals, not GPU timestamps or headset display intervals.
struct PresentAnalysis {
 using Clock=std::chrono::steady_clock;
 struct Record {uint64_t entry{},interval{},present{},outside{};bool world{};unsigned mode{};int fovPhase=-1,fov{};};
 struct History {
  std::mutex mutex;std::array<Record,256> records{};size_t count{};
  uint64_t previousEntry{},previousExit{},reportAt{},frame{},cpuAt{},processCpu{},threadCpu{};
  DWORD thread{};uint64_t previousPresent{};bool previousWorld{};unsigned previousMode{};int previousFovPhase=-1,previousFov{};
 };
 static History& history(){static History h;return h;}
 static uint64_t now(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());}
 static uint64_t fileTime(FILETIME f){return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
 static uint64_t cpu(bool process){FILETIME created{},exited{},kernel{},user{};
  const bool ok=process?GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user):GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user);
  return ok?(fileTime(kernel)+fileTime(user))/10:0;
 }
 uint64_t entered{};bool world{};unsigned mode{};int fovPhase=-1,fov{};
 explicit PresentAnalysis(bool gameplay):world(gameplay){if(extendedLogging()){entered=now();mode=perf::mode.load(std::memory_order_relaxed);fovPhase=perf::fovPhase.load(std::memory_order_relaxed);fov=perf::engineFov.load(std::memory_order_relaxed);}}
 ~PresentAnalysis(){if(!entered)return;try{
  const auto exit=now();auto& h=history();std::string batch,load;
  {std::lock_guard<std::mutex> guard(h.mutex);
   ++h.frame;
   if(h.previousEntry&&entered>=h.previousEntry){
    h.records[h.count++]={h.previousEntry,entered-h.previousEntry,h.previousPresent,entered>=h.previousExit?entered-h.previousExit:0,h.previousWorld,h.previousMode,h.previousFovPhase,h.previousFov};
   }
   h.previousEntry=entered;h.previousExit=exit;h.previousPresent=exit-entered;h.previousWorld=world;h.previousMode=mode;
   h.previousFovPhase=fovPhase;h.previousFov=fov;
   if(!h.reportAt){h.reportAt=exit;h.cpuAt=exit;h.processCpu=cpu(true);h.threadCpu=cpu(false);h.thread=GetCurrentThreadId();}
   if(h.count&&(exit-h.reportAt>=1000000||h.count==h.records.size())){
    std::array<uint64_t,256> sorted{};uint64_t sum{},maxPresent{},sumOutside{},sumPresent{};unsigned over25{},over50{};
    for(size_t i=0;i<h.count;++i){const auto& r=h.records[i];sorted[i]=r.interval;sum+=r.interval;sumPresent+=r.present;sumOutside+=r.outside;maxPresent=std::max(maxPresent,r.present);over25+=r.interval>25000;over50+=r.interval>50000;}
    std::sort(sorted.begin(),sorted.begin()+h.count);
    const auto percentile=[&](size_t p){return sorted[std::min(h.count-1,(h.count*p+99)/100-1)]/1000.;};
    std::ostringstream out;out<<"FRAME_TIMELINE frames="<<h.count<<" endFrame="<<h.frame<<" clock=steady_us meanMs="<<double(sum)/h.count/1000.<<" p50Ms="<<percentile(50)<<" p95Ms="<<percentile(95)<<" p99Ms="<<percentile(99)<<" maxMs="<<sorted[h.count-1]/1000.<<" presentMeanMs="<<double(sumPresent)/h.count/1000.<<" presentMaxMs="<<maxPresent/1000.<<" outsidePresentMeanMs="<<double(sumOutside)/h.count/1000.<<" over25="<<over25<<" over50="<<over50<<" recordsUs=";
    for(size_t i=0;i<h.count;++i){if(i)out<<',';const auto& r=h.records[i];out<<r.interval<<'/'<<r.present<<'/'<<r.outside<<'/'<<(r.world?'G':'Q');}out<<" entriesUs=";for(size_t i=0;i<h.count;++i){if(i)out<<',';out<<h.records[i].entry;}if(perf::enabled()){out<<" perfModes=";for(size_t i=0;i<h.count;++i){if(i)out<<',';out<<h.records[i].mode;}}batch=out.str();
    if(perf::enabled()){std::ostringstream f;f<<" fovStates=";for(size_t i=0;i<h.count;++i){if(i)f<<',';f<<h.records[i].fovPhase<<'/'<<h.records[i].fov;}batch+=f.str();}
    const auto process=cpu(true),thread=cpu(false);const auto elapsed=exit-h.cpuAt;
    std::ostringstream usage;usage<<"PROCESS_CPU windowMs="<<elapsed/1000.<<" processCoreEquivalents="<<(elapsed&&process>=h.processCpu?double(process-h.processCpu)/elapsed:0.)<<" presentThread="<<GetCurrentThreadId()<<" presentThreadCoreEquivalents="<<(h.thread==GetCurrentThreadId()&&elapsed&&thread>=h.threadCpu?double(thread-h.threadCpu)/elapsed:-1.);load=usage.str();
    h.cpuAt=exit;h.processCpu=process;h.threadCpu=thread;h.thread=GetCurrentThreadId();h.count=0;h.reportAt=exit;
   }
  }
  if(!batch.empty()){log(batch);log(load);}
 }catch(...){} }
};
}
