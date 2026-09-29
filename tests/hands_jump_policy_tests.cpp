#include "../src/openxr/HandsJumpPolicy.h"
#include <cstdlib>
#include <limits>
void check(bool value) { if (!value) std::abort(); }
int main() {
    kharvox::HandsJumpState jump;
    int64_t t=1000000000;
    auto step=[&](float left,float right,bool context=true,bool tracked=true) {
        t+=10000000;
        return jump.update(t,context,tracked,left,right);
    };
    check(!step(2,2)); // Tracking acquired mid-motion must not jump.
    check(!step(0,0));
    check(!step(2,1.89f)); // Both hands, upward component only.
    check(!step(-3,-3));
    check(step(1.9f,1.9f));
    for(int i=0;i<9;++i) check(step(2,2));
    check(!step(2,2)); // Pulse releases even if the movement continues.
    t+=1000000000;
    check(!step(2,2)); // No repeated jumps without settling.
    check(!step(0,2));
    check(!step(2,2));
    check(!step(0,0));
    check(step(2,2));
    check(!step(2,2,false)); // Menu/focus/disabled cancels a held pulse.
    check(!step(2,2));
    check(!step(0,0));
    check(step(2,2));
    check(!step(0,0,true,false)); // Tracking loss clears gesture/pulse.
    check(!step(2,2));
    check(!step(0,0));
    check(!step(std::numeric_limits<float>::quiet_NaN(),2));
    check(!step(2,2));
    check(!step(0,0));
    check(step(2,2));
    check(step(0,0));
    t+=110000000;
    check(!step(2,2)); // Cooldown blocks a second fast gesture.
}
