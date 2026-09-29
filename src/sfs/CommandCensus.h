#pragma once
#include "../PerformanceDiagnostics.h"
#include <array>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>

namespace argent::perf {
enum Op:unsigned {Draw,DrawIndirect,Dispatch,DispatchIndirect,Sets,Vertices,Index,Push,Pipeline,OpCount};
struct CensusKey {
 uint64_t pass{},pipeline{};uint32_t width{},height{},subpass{},compute{};
 bool operator<(const CensusKey& r)const{return std::tie(pass,pipeline,width,height,subpass,compute)<std::tie(r.pass,r.pipeline,r.width,r.height,r.subpass,r.compute);}
 bool operator==(const CensusKey& r)const{return !(*this<r)&&!(r<*this);}
};
struct CensusCounts {
 std::array<uint64_t,OpCount> calls{};
 void add(const CensusCounts& v){for(unsigned i=0;i<OpCount;++i)calls[i]+=v.calls[i];}
 uint64_t total()const{uint64_t n{};for(auto v:calls)n+=v;return n;}
 std::string fields()const{std::ostringstream s;s<<" draws="<<calls[Draw]<<" indirectDrawCalls="<<calls[DrawIndirect]<<" dispatches="<<calls[Dispatch]<<" indirectDispatches="<<calls[DispatchIndirect]<<" sets="<<calls[Sets]<<" vertices="<<calls[Vertices]<<" index="<<calls[Index]<<" push="<<calls[Push]<<" pipelineBinds="<<calls[Pipeline];return s.str();}
};
struct CensusRow {CensusKey key;CensusCounts counts;};
struct CommandCensus {
 bool active{};uint64_t serial{},started{},cycles{};DWORD thread{};unsigned selected{};
 int fovPhase=-1,fov{};
 CensusKey graphics{},compute{};std::vector<CensusRow> rows;size_t cached[2]{SIZE_MAX,SIZE_MAX};CensusCounts overflow;
 static uint64_t clock(){LARGE_INTEGER value;QueryPerformanceCounter(&value);return uint64_t(value.QuadPart);}
 static double elapsedMs(uint64_t a,uint64_t b){static const double frequency=[] {LARGE_INTEGER f;QueryPerformanceFrequency(&f);return double(f.QuadPart);}();return double(b-a)*1000./frequency;}
 void begin(uint64_t f,unsigned m){serial=f;selected=m;active=sample(f,m);rows.clear();graphics={};compute={};cached[0]=cached[1]=SIZE_MAX;overflow={};
  if(active){fovPhase=perf::fovPhase.load(std::memory_order_relaxed);fov=perf::engineFov.load(std::memory_order_relaxed);thread=GetCurrentThreadId();cycles=0;QueryThreadCycleTime(GetCurrentThread(),&cycles);started=clock();}}
 void add(Op op,bool isCompute=false){if(!active)return;const unsigned c=isCompute?1:0;const auto& key=c?compute:graphics;auto& slot=cached[c];
  if(slot>=rows.size()||!(rows[slot].key==key)){slot=SIZE_MAX;for(size_t i=0;i<rows.size();++i)if(rows[i].key==key){slot=i;break;}
   if(slot==SIZE_MAX){if(rows.size()>=128){++overflow.calls[op];return;}slot=rows.size();rows.push_back({key,{}});}}
  ++rows[slot].counts.calls[op];
 }
 void pass(uint64_t id,uint32_t w,uint32_t h,uint32_t sub=0){graphics.pass=id;graphics.width=w;graphics.height=h;graphics.subpass=sub;}
};
struct CensusCollector {
 struct ThreadTotals {uint64_t commands{},cycles{},migrated{};double wallMs{};};
 struct FovTotals {std::set<uint64_t> frames;CensusCounts counts;};
 struct Batch {std::map<CensusKey,CensusCounts> rows;std::map<DWORD,ThreadTotals> threads;std::map<uint64_t,unsigned> frames;std::map<std::pair<int,int>,FovTotals> fov;CensusCounts overflow;uint64_t commands{},first{},last{};};
 std::mutex mutex;std::array<Batch,3> batches;uint64_t reported{};
 void finish(CommandCensus& c){if(!c.active)return;const auto ended=CommandCensus::clock();uint64_t cycles{};const bool same=c.thread==GetCurrentThreadId();if(same)QueryThreadCycleTime(GetCurrentThread(),&cycles);
  std::lock_guard<std::mutex> lock(mutex);auto& b=batches[c.selected];++b.commands;++b.frames[c.serial];if(!b.first)b.first=c.serial;b.last=c.serial;
  auto& t=b.threads[c.thread];++t.commands;t.wallMs+=CommandCensus::elapsedMs(c.started,ended);if(same&&cycles>=c.cycles)t.cycles+=cycles-c.cycles;else ++t.migrated;
  if(c.fovPhase>0){auto& f=b.fov[{c.fovPhase,c.fov}];f.frames.insert(c.serial);for(const auto& r:c.rows)f.counts.add(r.counts);f.counts.add(c.overflow);}
  for(const auto& r:c.rows){auto it=b.rows.find(r.key);if(it!=b.rows.end())it->second.add(r.counts);else if(b.rows.size()<4096)b.rows.emplace(r.key,r.counts);else b.overflow.add(r.counts);}b.overflow.add(c.overflow);c.active=false;
 }
 template<class Emit> void report(Emit emit){const auto now=GetTickCount64();if(reported&&now-reported<1000)return;reported=now;std::array<Batch,3> copy;
  {std::lock_guard<std::mutex> lock(mutex);batches.swap(copy);}
  for(unsigned mode=1;mode<3;++mode){auto& b=copy[mode];if(!b.commands)continue;
   const std::string tag=" mode="+std::to_string(mode)+" firstRecordFrame="+std::to_string(b.first)+" lastRecordFrame="+std::to_string(b.last)+" sampledFrames="+std::to_string(b.frames.size());
   std::vector<CensusRow> sorted;CensusCounts all;for(const auto& p:b.rows){sorted.push_back({p.first,p.second});all.add(p.second);}all.add(b.overflow);
   std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.counts.total()>b.counts.total();});
   emit("PERF_COMMAND_TOTAL"+tag+" recordedCommandBuffers="+std::to_string(b.commands)+" groups="+std::to_string(sorted.size())+" overflowCalls="+std::to_string(b.overflow.total())+all.fields());
   for(const auto& p:b.fov){std::ostringstream ids;for(auto f:p.second.frames){if(ids.tellp()>0)ids<<',';ids<<f;}emit("PERF_FOV_COMMANDS mode="+std::to_string(mode)+" phase="+std::to_string(p.first.first)+" actual="+std::to_string(p.first.second)+" recordFrames="+ids.str()+p.second.counts.fields());}
   for(size_t i=0;i<(std::min)(size_t(20),sorted.size());++i){const auto& r=sorted[i];emit("PERF_COMMAND_GROUP"+tag+" pass="+std::to_string(r.key.pass)+" pipeline="+std::to_string(r.key.pipeline)+" width="+std::to_string(r.key.width)+" height="+std::to_string(r.key.height)+" subpass="+std::to_string(r.key.subpass)+" compute="+std::to_string(r.key.compute)+r.counts.fields());}
   for(const auto& p:b.threads)emit("PERF_RECORD_THREAD"+tag+" thread="+std::to_string(p.first)+" commands="+std::to_string(p.second.commands)+" wallSumMs="+std::to_string(p.second.wallMs)+" cpuCycles="+std::to_string(p.second.cycles)+" migrated="+std::to_string(p.second.migrated));
  }
 }
};
}
