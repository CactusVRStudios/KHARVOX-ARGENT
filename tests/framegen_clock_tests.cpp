#include "../src/framegen/FrameClock.h"
#include "../src/framegen/EndpointOutput.h"
#include <cmath>
#include <iostream>

namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Runtime {
    std::mutex mutex;
    std::condition_variable changed;
    bool held{},entered{},waiting{},prepared{},begun{},overlap{};
    unsigned waits{},begins{},ends{};
    XrDuration period=11111111;
    XrTime time=1000000000;
    XrResult result=XR_SUCCESS;
} runtime;
const auto session=reinterpret_cast<XrSession>(uintptr_t(1));
XrResult XRAPI_CALL mockWait(XrSession,const XrFrameWaitInfo*,XrFrameState* out){
    std::unique_lock lock(runtime.mutex);
    if(runtime.waiting||runtime.prepared){runtime.overlap=true;return XR_ERROR_CALL_ORDER_INVALID;}
    runtime.waiting=runtime.entered=true;++runtime.waits;runtime.changed.notify_all();
    runtime.changed.wait(lock,[]{return !runtime.held;});
    runtime.waiting=false;
    out->predictedDisplayPeriod=runtime.period;
    out->predictedDisplayTime=(runtime.time+=runtime.period);
    out->shouldRender=XR_TRUE;
    runtime.prepared=XR_SUCCEEDED(runtime.result);
    return runtime.result;
}
void begin(){std::lock_guard lock(runtime.mutex);check(runtime.prepared&&!runtime.begun,"begin requires one waited frame and no begun frame");runtime.prepared=false;runtime.begun=true;++runtime.begins;}
void end(){std::lock_guard lock(runtime.mutex);check(runtime.begun,"end requires begin");runtime.begun=false;++runtime.ends;}
void verifyBalanced(){std::lock_guard lock(runtime.mutex);check(!runtime.overlap&&!runtime.waiting&&!runtime.prepared&&!runtime.begun,"no outstanding or overlapping frame");check(runtime.waits==runtime.begins&&runtime.begins==runtime.ends,"all successful waits retired");}
}
int main(){try{
    using namespace argent::fg;
    FrameClock clock;
    for(double hz:{72.,80.,90.,120.,144.}){
        clock.observe(XrDuration(std::llround(1e9/hz)));clock.active=true;
        check(std::abs(clock.displayHz()-hz)<.001,"headset target follows period");
        check(std::abs(clock.nativeHz()-hz/2)<.001,"native target is half refresh");
        check(clock.realTime(1000000000,true)==1000000000+clock.period,"endpoint pose one display ahead");
        check(clock.realTime(1000000000,false)==1000000000,"no menu pose lead");
        clock.active=false;check(clock.realTime(1000000000,true)==1000000000,"no inactive pose lead");
    }
    clock.observe(0);check(clock.displayHz()==0&&clock.nativeHz()==0,"invalid period cannot create fixed fallback cap");
    // SteamVR can report an application half-rate without changing the HMD.
    clock.observe(22222222,90);check(std::abs(clock.nativeHz()-45)<.001,"90 Hz hardware must not become a 22.5 Hz producer");
    clock.observe(11111111,90);check(std::abs(clock.nativeHz()-45)<.001,"alternating application period does not change target");
    clock.observe(22222222,120);check(std::abs(clock.nativeHz()-60)<.001,"physical refresh switch overrides stale application period");
    clock.observe(13888889,0);check(std::abs(clock.nativeHz()-36)<.001,"runtime-period fallback without refresh extension");
    FrameWaitAhead pipeline;
    auto first=pipeline.take(session,mockWait);check(!first.prefetched&&XR_SUCCEEDED(first.result),"initial synchronous wait");begin();
    {
        std::lock_guard lock(runtime.mutex);runtime.held=true;runtime.entered=false;
    }
    check(pipeline.prime(session,mockWait),"start wait while current frame is rendering");
    check(!pipeline.prime(session,mockWait),"duplicate prime cannot queue another wait");
    {
        std::unique_lock lock(runtime.mutex);
        check(runtime.changed.wait_for(lock,std::chrono::seconds(2),[]{return runtime.entered;}),"worker enters wait independently");
    }
    // This is the regression: current-frame submission must remain possible
    // while the runtime blocks the next wait. No XR lifecycle/queue mutex held.
    end();
    {
        std::lock_guard lock(runtime.mutex);runtime.held=false;runtime.changed.notify_all();
    }
    auto next=pipeline.take(session,mockWait);check(next.prefetched&&!pipeline.pending(),"consume exactly one pre-wait");begin();end();
    verifyBalanced();
    // Two runtime ticks per real frame; no third wait on the game path.
    for(double hz:{72.,90.,120.,80.,144.,90.}){
        {std::lock_guard lock(runtime.mutex);runtime.period=XrDuration(std::llround(1e9/hz));}
        unsigned before=runtime.waits;
        for(unsigned pair=0;pair<200;++pair){
            auto midpoint=pipeline.take(session,mockWait);begin();clock.observe(midpoint.state.predictedDisplayPeriod);clock.active=true;
            check(std::abs(clock.nativeHz()-hz/2)<.001,"live refresh switch");
            pipeline.prime(session,mockWait);end();
            auto real=pipeline.take(session,mockWait);begin();
            check(real.state.predictedDisplayTime==clock.realTime(midpoint.state.predictedDisplayTime,true),"midpoint then exact endpoint one tick apart");
            pipeline.prime(session,mockWait);end();
        }
        // Pause/shutdown consumes an outstanding wait with an empty frame.
        pipeline.take(session,mockWait);begin();end();
        check(runtime.waits-before==401,"200 pairs plus one drained wait, no third tick");
        verifyBalanced();
    }
    // A failed runtime wait must not remain queued or be begun as a frame.
    runtime.result=XR_ERROR_SESSION_NOT_RUNNING;
    pipeline.prime(session,mockWait);auto failure=pipeline.take(session,mockWait);
    check(failure.result==XR_ERROR_SESSION_NOT_RUNNING&&!pipeline.pending()&&!runtime.prepared,"failure delivered and drained");
    runtime.result=XR_SUCCESS;
    auto fallback=pipeline.take(session,mockWait);check(!fallback.prefetched,"fallback returns to ordinary wait");begin();end();
    // The runtime may block endpoint begin while native CPU work proceeds.
    // A publication cannot start until source retirement dispatches it, and
    // acquire/teardown must drain it before reusing XR state.
    EndpointOutput output;
    std::promise<void> entered,unblock;
    auto enteredFuture=entered.get_future();auto gate=unblock.get_future().share();
    std::atomic<unsigned> published{},gameTicks{};
    output.stage([&]{entered.set_value();gate.wait();++published;});
    check(enteredFuture.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout,"staging cannot race source retirement");
    bool rejected=false;try{output.stage([]{});}catch(const std::logic_error&){rejected=true;}
    check(rejected,"only one exact endpoint can be outstanding");
    output.dispatch();output.dispatch();
    check(enteredFuture.wait_for(std::chrono::seconds(2))==std::future_status::ready,"endpoint independently reaches blocked runtime");
    ++gameTicks;check(gameTicks==1&&published==0,"game CPU progresses while endpoint waits");
    auto drained=std::async(std::launch::async,[&]{output.drain();});
    check(drained.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout,"next acquire must wait for publication");
    unblock.set_value();drained.get();output.drain();
    check(published==1,"duplicate dispatch/drain cannot repeat endpoint");
    // Cancellation after staging (before native present returns) still retires
    // the owned XR image. A reported failure cannot poison later hand-offs.
    output.stage([&]{++published;});output.drain();
    check(published==2,"shutdown drains a staged but not dispatched endpoint");
    output.stage([]{throw std::runtime_error("runtime failure");});
    bool propagated=false;try{output.drain();}catch(const std::runtime_error&){propagated=true;}
    check(propagated,"runtime failure is propagated after retirement");
    output.stage([&]{++published;});output.dispatch();output.drain();
    check(published==3,"handoff recovers after a failed task");
    std::cout<<"PASS: wait overlap, single in-flight wait, ordered retirement, 72/80/90/120/144 Hz, refresh changes, pause and failed wait\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
