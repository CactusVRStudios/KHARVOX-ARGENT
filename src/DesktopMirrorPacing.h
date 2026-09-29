#pragma once
#include <chrono>
#include <cstdint>

namespace argent {
// Only skip optional desktop work. Never sleep or delay the XR frame.
struct DesktopMirrorPacing {
    using Clock=std::chrono::steady_clock;
    Clock::duration interval{};
    Clock::time_point next{};
    bool scheduled{};
    void configure(uint32_t fps) {
        interval=fps?std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/fps)):Clock::duration{};
        scheduled=false;
    }
    bool due(Clock::time_point now) const {return interval==Clock::duration{}||!scheduled||now>=next;}
    void submitted(Clock::time_point now) {
        if(interval==Clock::duration{})return;
        // Keep the cadence across non-multiple rates (e.g. 90 Hz XR / 60 Hz
        // desktop), but don't catch up in bursts after a pause or busy WSI.
        if(!scheduled||now-next>=interval)next=now+interval;
        else next+=interval;
        scheduled=true;
    }
};
}
