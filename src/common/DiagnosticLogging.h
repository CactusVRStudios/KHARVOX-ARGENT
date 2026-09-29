#pragma once
#include <windows.h>
namespace kharvox {
inline bool extendedDiagnosticsEnabled() noexcept {
    static const bool enabled=[] {
        char value[8]{};
        return GetEnvironmentVariableA("KHARVOX_EXTENDED_LOGGING", value, sizeof(value)) == 1
            && value[0] == '1';
    }();
    return enabled;
}
}
