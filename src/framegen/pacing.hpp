#pragma once
#include "../QuadRuntime.h"
#include <chrono>
#include <map>
#include <mutex>
namespace argent::fg {
// Status only. XR pacing and exact endpoint ownership live in StereoXr.inc.
inline void state(const char* s,const char* reason="",bool=false){
 static std::mutex mutex;std::lock_guard lock(mutex);
 static std::map<std::string,std::chrono::steady_clock::time_point> seen;
 const auto now=std::chrono::steady_clock::now();std::string next=std::string(s)+": "+reason;
 const auto last=seen.find(next);if(last!=seen.end()&&now-last->second<std::chrono::seconds(5))return;
 if(seen.size()>128)seen.clear();seen[next]=now;argent::log("FRAMEGEN_2X "+next);
}
}
