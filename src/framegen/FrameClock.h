#pragma once
#include "../XrWorker.h"
#include <openxr/openxr.h>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <stdexcept>

namespace argent::fg {
// One waited (pre-rendering) frame may coexist with one begun (rendering)
// frame. Only xrWaitFrame runs here: no XR lifecycle, queue or game-state lock.
// The owner must consume and begin/end this frame before destroying the session.
struct WaitedFrame {
    XrFrameState state{XR_TYPE_FRAME_STATE};
    XrResult result{XR_SUCCESS};
    double runtimeWaitMs{};
    double callerWaitMs{};
    double beginCallMs{};
    bool prefetched{};
};
class FrameWaitAhead {
    std::future<WaitedFrame> pending_;
    XrSession session_{};
    static WaitedFrame wait(XrSession session, PFN_xrWaitFrame fn) {
        WaitedFrame result;
        const auto start=std::chrono::steady_clock::now();
        XrFrameWaitInfo info{XR_TYPE_FRAME_WAIT_INFO};
        result.result=fn(session,&info,&result.state);
        result.runtimeWaitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        return result;
    }
public:
    bool pending() const {return pending_.valid();}
    bool prime(XrSession session, PFN_xrWaitFrame fn) {
        if(pending())return false;
        // Process lifetime, just like the lifecycle worker; never join in DllMain.
        static auto* worker=new XrWorker;
        session_=session;
        pending_=worker->post([session,fn]{return wait(session,fn);});
        return true;
    }
    WaitedFrame take(XrSession session, PFN_xrWaitFrame fn) {
        const auto start=std::chrono::steady_clock::now();
        const bool prefetched=pending();
        if(prefetched&&session!=session_)throw std::logic_error("Frame wait belongs to another XR session");
        auto result=prefetched?pending_.get():wait(session,fn);
        result.prefetched=prefetched;
        result.callerWaitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        return result;
    }
};

struct FrameClock {
    XrDuration period{};
    bool active{};
    // Circle's display-target rule: prefer physical refresh over SteamVR's
    // throttled application period (which can alternate between 11 and 22 ms).
    void observe(XrDuration value,double headsetHz=0) {
        period=std::isfinite(headsetHz)&&headsetHz>=25&&headsetHz<=250?
            XrDuration(std::llround(1e9/headsetHz)):(value>0?value:0);
    }
    double displayHz() const {return period>0?1e9/double(period):0;}
    double nativeHz() const {return displayHz()/2;}
    // The real frame follows the generated midpoint by one display period.
    XrTime realTime(XrTime first, bool gameplay) const {
        return first+(active&&gameplay?period:0);
    }
};
}
