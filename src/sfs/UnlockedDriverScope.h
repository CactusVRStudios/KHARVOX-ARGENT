#pragma once
#include <mutex>
namespace argent::sfs {
// Only use after copying every metadata value needed by the driver. Vulkan
// object lifetime and command recording remain externally synchronized by
// the application; this lock protects our maps, not driver execution.
template<class Mutex> struct UnlockedDriverScope {
 std::unique_lock<Mutex>& lock;
 explicit UnlockedDriverScope(std::unique_lock<Mutex>& value):lock(value){lock.unlock();}
 ~UnlockedDriverScope(){lock.lock();}
 UnlockedDriverScope(const UnlockedDriverScope&)=delete;
 UnlockedDriverScope& operator=(const UnlockedDriverScope&)=delete;
};
}
