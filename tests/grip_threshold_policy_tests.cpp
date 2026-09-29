#include "../src/openxr/GripThresholdPolicy.h"
#include "../src/openxr/ControllerProfile.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

int main() {
    using namespace kharvox;

    if (std::strcmp(gripInputComponent(GripControllerProfile::Default),
            "squeeze/value") != 0) return 1;
    if (std::strcmp(gripInputComponent(GripControllerProfile::ValveIndex),
            "squeeze/force") != 0) return 2;

    // Existing controller profiles retain the previous > 0.55 behavior.
    if (updateGripPressed(0.55f, false, GripControllerProfile::Default)) return 3;
    if (!updateGripPressed(0.56f, false, GripControllerProfile::Default)) return 4;
    if (updateGripPressed(0.55f, true, GripControllerProfile::Default)) return 5;

    // Valve Index needs a deliberate squeeze to engage.
    if (updateGripPressed(0.70f, false, GripControllerProfile::ValveIndex)) return 6;
    if (!updateGripPressed(0.71f, false, GripControllerProfile::ValveIndex)) return 7;

    // Once engaged, hysteresis prevents chatter while the grip is held.
    if (!updateGripPressed(0.60f, true, GripControllerProfile::ValveIndex)) return 8;
    if (updateGripPressed(0.55f, true, GripControllerProfile::ValveIndex)) return 9;

    if (updateGripPressed(NAN, false, GripControllerProfile::ValveIndex)) return 10;
    if (updateGripPressed(NAN, true, GripControllerProfile::ValveIndex)) return 11;

    using namespace argent::input;
    if(std::strcmp(faceButtonComponent(false,0,false),"x/click") ||
       std::strcmp(faceButtonComponent(false,0,true),"y/click") ||
       std::strcmp(faceButtonComponent(true,0,false),"a/click") ||
       std::strcmp(faceButtonComponent(true,0,true),"b/click"))return 12;
    for(bool index:{false,true}){
        if(std::strcmp(faceButtonComponent(index,1,false),"a/click") ||
           std::strcmp(faceButtonComponent(index,1,true),"b/click"))return 13;
    }
    if(std::strcmp(pauseComponent(true),"trackpad/force") ||
       std::strcmp(pauseComponent(false),"menu/click"))return 14;
    bool held=false;
    const float force[]{0.f,.3f,.70f,.71f,.60f,.56f,.55f,.60f,.8f,0.f};
    const bool expected[]{false,false,false,true,true,true,false,false,true,false};
    for(unsigned i=0;i<10;++i){
        held=updateIndexPause(force[i],held);if(held!=expected[i])return 15;
    }
    if(updateIndexPause(NAN,true))return 16;
    return 0;
}
