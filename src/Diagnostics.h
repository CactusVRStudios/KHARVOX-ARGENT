#pragma once
#include <windows.h>
#include "BuildFeatures.h"

namespace argent {
inline bool environmentFlag(const char* name){
    char value[8]{};return GetEnvironmentVariableA(name,value,sizeof(value))==1&&value[0]=='1';
}
// Fixed at process launch. No environment lookup in recording/submit loops.
inline bool extendedLogging(){
    static const bool enabled=environmentFlag("ARGENT_EXTENDED_LOGGING")||environmentFlag("ARGENT_SFS_PROFILE_TIMING");
    return enabled;
}
inline bool gpuCrashDiagnostics(){
    static const bool enabled=environmentFlag("ARGENT_GPU_DIAGNOSTICS");
    return enabled;
}
}
