#pragma once
#include <cstdint>
namespace argent {
// Only for command recording: Vulkan requires the device to remain alive
// throughout the call. Registry generation invalidates borrowed pointers.
template<class State> struct CommandDeviceCache {
 void* key{};uint64_t generation{};State* value{};
 template<class Resolve> State* get(void* k,uint64_t g,Resolve resolve){
  if(key!=k||generation!=g||!value){value=resolve();key=k;generation=g;}
  return value;
 }
};
// One slot per API call site and recording thread, not per function signature:
// Vulkan commands with identical signatures must never share an address.
template<class Function> struct CommandDispatchSlot {
 uint64_t deviceId{};Function value{};
 template<class Resolve> Function get(uint64_t id,Resolve resolve){
  if(deviceId!=id){value=resolve();deviceId=id;}
  return value;
 }
};
}
#define ARGENT_COMMAND_PROC(state, name) ([&] { \
 static thread_local argent::CommandDispatchSlot<PFN_##name> slot; \
 return slot.get((state)->dispatchId,[&] {return (state)->proc<PFN_##name>(#name);}); \
}())
