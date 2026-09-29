#include "../src/sfs/CommandCensus.h"
#include "../src/openxr/DiagnosticGpuTiming.h"
#include "../src/AaOverridePolicy.h"
#include "../src/FovComparison.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
namespace argent {void log(const std::string&) {}}
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static unsigned resets{},stamps{},destroyed{},lastCount{};static VkQueryResultFlags resultFlags{};static VkResult result=VK_SUCCESS;static bool available=true;
static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice,const VkQueryPoolCreateInfo* i,const VkAllocationCallbacks*,VkQueryPool* out){check(i->queryCount==4,"Private query size");*out=reinterpret_cast<VkQueryPool>(uintptr_t(2));return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice,VkQueryPool,const VkAllocationCallbacks*){++destroyed;}
static VKAPI_ATTR void VKAPI_CALL reset(VkCommandBuffer,VkQueryPool,uint32_t first,uint32_t count){check(!first&&count==4,"Reset entire private pool");++resets;}
static VKAPI_ATTR void VKAPI_CALL stamp(VkCommandBuffer,VkPipelineStageFlagBits,VkQueryPool,uint32_t index){check(index<4,"Out of bounds GPU timestamp");++stamps;}
static VKAPI_ATTR VkResult VKAPI_CALL results(VkDevice,VkQueryPool,uint32_t,uint32_t count,size_t,void* data,VkDeviceSize stride,VkQueryResultFlags flags){
 resultFlags=flags;lastCount=count;for(unsigned i=0;i<count;++i){uint64_t v[2]{uint64_t(250+i*5)&255,available?1ull:0ull};std::memcpy(static_cast<char*>(data)+i*stride,v,sizeof(v));}return result;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice,const char* name){
#define MAP(vk,fn) if(!std::strcmp(name,#vk))return reinterpret_cast<PFN_vkVoidFunction>(fn)
 MAP(vkCreateQueryPool,create);MAP(vkDestroyQueryPool,destroy);MAP(vkCmdResetQueryPool,reset);MAP(vkCmdWriteTimestamp,stamp);MAP(vkGetQueryPoolResults,results);
#undef MAP
 return nullptr;
}
int main(){try{
 using namespace argent::perf;
 check(scheduledMode(0,-1)==0&&scheduledMode(7999,-1)==0&&scheduledMode(8000,-1)==1&&scheduledMode(16000,-1)==2&&scheduledMode(24000,-1)==0,"Diagnostic phase boundaries");
 check(scheduledMode(99999,0)==0&&scheduledMode(0,2)==2,"Manual override");
 check(!sample(0,2)&&!sample(16,0)&&sample(16,1)&&!sample(17,2),"Sampling gate");
 CommandCensus census;census.begin(16,1);census.pass(11,100,200);census.graphics.pipeline=22;census.add(Draw);census.add(Sets);census.compute.pipeline=33;census.compute.compute=1;census.add(Dispatch,true);
 check(census.rows.size()==2&&census.rows[0].counts.calls[Draw]==1&&census.rows[1].key.compute==1,"Graphics/compute attribution");
 for(unsigned i=0;i<200;++i){census.graphics.pipeline=100+i;census.add(Draw);}
 check(census.rows.size()==128&&census.overflow.total()>0,"Bounded census preserves overflow");
 CensusCollector collector;collector.finish(census);unsigned totals{},groups{},threads{};
 collector.report([&](const std::string& s){totals+=s.find("PERF_COMMAND_TOTAL")==0;groups+=s.find("PERF_COMMAND_GROUP")==0;threads+=s.find("PERF_RECORD_THREAD")==0;});
 check(totals==1&&groups==20&&threads==1,"Bounded grouped/thread report");
 census.begin(17,1);census.add(Draw);check(!census.active&&census.rows.empty(),"Unsampled CB must stay empty");
 GpuTiming<4> gpu;const auto device=reinterpret_cast<VkDevice>(uintptr_t(1));const auto cb=reinterpret_cast<VkCommandBuffer>(uintptr_t(3));
 check(!gpu.initialize(device,resolver,1,0),"Unsupported timestamp family rejected");check(gpu.initialize(device,resolver,1000,8),"GPU timing initialization");
 gpu.begin(cb,false);gpu.point(cb,1);std::array<uint64_t,4> ticks{};check(!gpu.read(ticks)&&resets==0&&stamps==0,"Baseline emits no GPU queries");
 gpu.begin(cb,true);gpu.point(cb,1);gpu.point(cb,2);gpu.point(cb,3);check(resets==1&&stamps==4,"Timestamp count");
 available=false;check(!gpu.read(ticks),"Unavailable data rejected");available=true;result=VK_NOT_READY;check(!gpu.read(ticks),"Nonblocking query result");result=VK_SUCCESS;
 check(gpu.read(ticks,3)&&lastCount==3&&!(resultFlags&VK_QUERY_RESULT_WAIT_BIT),"No added wait, unused queries excluded");
 check(gpu.ms(250,4)==.01,"Timestamp valid-bit wraparound");gpu.shutdownAfterCompletion();gpu.shutdownAfterCompletion();check(destroyed==1,"Destroy exactly once");
 argent::camera::AaOverridePolicy aa;int actual=1,writes=0,before{},after{};bool accepted{};
 check(!aa.ready(100,1,true)&&!aa.ready(1599,1,true)&&aa.ready(1600,1,true),"AA safe-context delay");
 auto read=[&](int& value){value=actual;return true;};auto set=[&](int value){actual=value;++writes;return true;};
 check(!aa.apply(1600,-1,read,set,before,after,accepted)&&writes==0,"Unchecked launcher preserves AA");
 check(aa.apply(1600,0,read,set,before,after,accepted)&&before==1&&after==0&&accepted,"AA engine setter and readback");
 check(!aa.apply(1700,0,read,set,before,after,accepted)&&writes==1,"No redundant AA writes");actual=1;
 check(!aa.apply(2000,0,read,set,before,after,accepted)&&aa.apply(3600,0,read,set,before,after,accepted)&&actual==0,"Reset reapplied with cooldown");
 check(!aa.ready(4000,0,false)&&!aa.ready(5000,1,true)&&aa.ready(6500,1,true),"Loading transition re-arms delay");
 using argent::camera::aaTarget;
 check(aaTarget(1,false)==0&&aaTarget(0,false)==0&&aaTarget(2,false)==2,"TAA blocked and engine-selected DLSS preserved");
 check(aaTarget(1,true)==0&&aaTarget(0,true)==0&&aaTarget(2,true)==0,"FSR selection forces all native AA off");
 check(aaTarget(2,false,true)==0,"Failed stereo DLSS hook safely disables DLSS");
 check(aaTarget(-1,false)==0&&aaTarget(7,false)==0,"Unknown mode targets AA=0 through validated setter");
 argent::camera::FovComparison fov;check(fov.update(100,true,148)==148&&!fov.active,"FOV test opt-in only");
 fov.request(true);check(fov.update(100,false,148)==148&&fov.pending,"Wait for gameplay");
 check(fov.update(200,true,148)==148&&fov.phase==0,"Return-focus grace period");
 check(fov.update(10200,true,148)==148&&fov.phase==1,"A baseline");
 check(fov.update(22200,true,148)==120&&fov.phase==2,"B reduced FOV");
 check(fov.update(34200,true,148)==148&&fov.phase==3,"A repeated baseline");
 check(fov.update(46200,true,148)==120&&fov.phase==4,"B repeated reduced FOV");
 check(fov.update(58200,true,148)==148&&!fov.active,"Timed automatic restoration");
 fov.request(true);fov.update(60000,true,148);check(fov.update(83000,false,148)==148&&!fov.active,"Abort on pause/loading");
 fov.request(true);fov.update(90000,true,148);fov.request(false);check(fov.update(92000,true,148)==148&&!fov.pending,"Explicit restoration");
 argent::perf::fovPhase=2;argent::perf::engineFov=120;census.begin(32,1);census.add(Draw);collector.finish(census);collector.reported=0;
 bool tagged=false;collector.report([&](const std::string& s){if(s.find("PERF_FOV_COMMANDS mode=1 phase=2 actual=120 recordFrames=32 draws=1")!=std::string::npos)tagged=true;});
 check(tagged,"Recorded geometry tied to FOV phase and unique frame IDs");
 std::cout<<"performance diagnostics tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
