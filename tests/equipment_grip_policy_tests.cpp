#include "../src/openxr/EquipmentGripPolicy.h"
#include <cstdlib>
static void check(bool value){if(!value)std::abort();}
int main(){
    using kharvox::EquipmentGripPolicy;
    EquipmentGripPolicy p;
    check(!p.update(true,true,true,false,1000000000));
    check(p.update(false,true,true,false,1100000000));
    check(p.update(false,true,true,false,1150000000));
    check(!p.update(false,true,true,false,1200000000));
    // Grabbing and then moving out of range never emits equipment on release.
    p={};p.update(true,true,true,true,1000000000);
    p.update(true,true,true,false,1050000000);
    check(!p.update(false,true,true,false,1100000000));
    // Entering the grab range after pressing consumes the press too.
    p={};p.update(true,true,true,false,1000000000);
    p.update(true,true,true,true,1050000000);
    check(!p.update(false,true,true,false,1100000000));
    // BFG hold, menu transitions, tracking loss and backwards timestamps.
    p={};p.update(true,true,true,false,1000000000);
    check(!p.update(false,true,true,false,1700000000));
    p={};p.update(true,true,true,false,1000000000);
    p.update(true,false,true,false,1050000000);
    check(!p.update(false,true,true,false,1100000000));
    p={};p.update(true,true,true,false,1000000000);
    p.update(true,true,false,false,1050000000);
    check(!p.update(false,true,true,false,1100000000));
    p={};p.update(true,true,true,false,1000000000);p.cancel();
    check(!p.update(false,true,true,false,1100000000));
    p={};p.update(true,true,true,false,1000000000);
    check(!p.update(false,true,true,false,900000000));
    // A clean press after cancellation works.
    p.update(true,true,true,false,2000000000);
    check(p.update(false,true,true,false,2100000000));
}
