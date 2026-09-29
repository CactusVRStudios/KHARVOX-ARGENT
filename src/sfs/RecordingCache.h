#pragma once
#include <cstdint>
namespace argent::sfs {
// Borrow only per-command-buffer data. Vulkan requires the application to
// externally synchronize recording/free/reset of a command buffer and pool.
// The resolver protects the map; cached element addresses survive rehash.
// Retirement and device lifetime IDs prevent dereferencing recycled entries.
template<class Handle,class Command> struct RecordingCache {
 uint64_t device{},retirement{};Handle handle{};Command* command{};
 template<class Resolve> Command& get(uint64_t d,uint64_t r,Handle h,Resolve resolve){
  if(!command||device!=d||retirement!=r||handle!=h){
   auto* next=&resolve();device=d;retirement=r;handle=h;command=next;
  }
  return *command;
 }
};
}
