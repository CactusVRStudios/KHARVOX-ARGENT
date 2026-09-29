#include "../src/FrameTiming.h"
#include "../src/PresentAnalysis.h"
#include "../src/sfs/CommandCpuTiming.h"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {std::vector<std::string> lines;}
namespace argent {void log(const std::string& s){lines.push_back(s);}}
int main(int argc,char** argv){try{
    const bool requested=argc==2&&std::string(argv[1])=="on";
    const bool enabled=requested;
    SetEnvironmentVariableA("ARGENT_EXTENDED_LOGGING",requested?"1":"0");
    SetEnvironmentVariableA("ARGENT_GPU_DIAGNOSTICS",requested?"0":"1");
    if(argent::gpuCrashDiagnostics()==requested)throw std::runtime_error("GPU instrumentation coupled to Extended Logging");
    SetEnvironmentVariableA("ARGENT_GPU_DIAGNOSTICS",requested?"1":"0");
    if(argent::gpuCrashDiagnostics()==requested)throw std::runtime_error("GPU diagnostic flag not fixed at launch");
    SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS","1");
    if(argent::cleanRelease&&argent::perf::enabled())throw std::runtime_error("Clean release accepted profiler override");
    SetEnvironmentVariableA("ARGENT_SFS_PROFILE_TIMING","0");
    argent::FrameTiming::Totals totals;
    for(int n=0;n<120;++n){argent::FrameTiming timing("fixture",totals);}
    if(enabled){
        if(lines.size()!=1||lines[0].find("FRAME_CPU section=fixture samples=120 ")!=0)throw std::runtime_error("Missing extended frame timings");
    }else if(!lines.empty()||totals.count||totals.sum||totals.max||totals.lastReport!=argent::FrameTiming::Clock::time_point{}){
        throw std::runtime_error("Normal mode touched profiling state");
    }
    lines.clear();
    auto& history=argent::PresentAnalysis::history();
    {argent::PresentAnalysis timing(true);}
    if(enabled){
        history.previousEntry=argent::PresentAnalysis::now()-30000;
        history.previousExit=history.previousEntry+10000;
        history.previousPresent=10000;history.previousWorld=true;
        history.reportAt=argent::PresentAnalysis::now()-2000000;
        {argent::PresentAnalysis timing(true);}
        if(lines.size()!=2||lines[0].find("FRAME_TIMELINE frames=1 ")!=0||lines[0].find("/10000/")==std::string::npos||lines[0].find(" clock=steady_us ")==std::string::npos||lines[0].find(" entriesUs=")==std::string::npos||lines[1].find("PROCESS_CPU ")!=0)throw std::runtime_error("Missing paired frame timeline/CPU report");
    }else if(history.frame||!lines.empty())throw std::runtime_error("Disabled timeline changed state");
    using Timing=kharvox::sfs::CommandCpuTiming;
    static Timing::Bucket bucket{"fixture","metadata"};Timing::registerBucket(bucket);Timing::enabled=requested;
    for(int i=0;i<64000;++i){Timing t(&bucket);t.resolved();t.acquired();}
    const auto samples=bucket.count.load();
    if(enabled&&(samples<500||samples>1500))throw std::runtime_error("Sampling distribution incorrect");
    if(!enabled&&(samples||Timing::samples.load()))throw std::runtime_error("Disabled hook timing changed state");
    lines.clear();Timing::report([](const std::string& s){lines.push_back(s);});
    if(enabled&&(lines.size()!=1||lines[0].find("HOOK_CPU name=fixture path=metadata ")!=0||lines[0].find("lockMaxUs=")==std::string::npos))throw std::runtime_error("Missing hook breakdown");
    if(bucket.count.load())throw std::runtime_error("Hook report did not reset window");
    // The launch flag is cached: no per-frame environment lookup/change.
    SetEnvironmentVariableA("ARGENT_EXTENDED_LOGGING",enabled?"0":"1");
    if(argent::extendedLogging()!=enabled)throw std::runtime_error("Logging flag not fixed at launch");
    std::cout<<"PASS: profiling "<<(enabled?"on":"off")<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
