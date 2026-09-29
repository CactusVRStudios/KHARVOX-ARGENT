#pragma once
#include <cstdint>
namespace kharvox {
// Nanoseconds from predicted display time, matching the existing BFG hold.
struct EquipmentGripPolicy {
    bool down{}, blocked{true};
    int64_t started{}, pulseUntil{};
    void cancel() { blocked=true; pulseUntil=0; }
    bool update(bool pressed,bool gameplay,bool trackingValid,bool grabOwnsPress,int64_t now) {
        if(!gameplay||!trackingValid) { cancel(); down=pressed; return false; }
        if(pressed&&!down) { started=now; blocked=grabOwnsPress; }
        if(grabOwnsPress)blocked=true; // Ownership sticks until release, including leaving the grab zone.
        if(!pressed&&down&&!blocked&&now>=started&&now-started<350000000)
            pulseUntil=now+100000000;
        down=pressed;
        return now<pulseUntil;
    }
};
}
