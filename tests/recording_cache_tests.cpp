#include "../src/sfs/RecordingCache.h"
#include "../src/sfs/DescriptorCountCache.h"
#include "../src/sfs/UnlockedDriverScope.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
#include <future>
using argent::sfs::RecordingCache;
static void check(bool x){if(!x)throw std::runtime_error("recording cache contract failed");}
struct Command {volatile uint64_t value{};};
int main(int argc,char**){try{
 {
  argent::sfs::DescriptorCountCache<unsigned,std::vector<uint32_t>> layouts;
  unsigned reads=0;
  auto resolveLayout=[&]{++reads;return std::vector<uint32_t>{0,2,1};};
  for(unsigned poolReset=0;poolReset<10000;++poolReset){
   const auto& counts=layouts.get(1,1,7,resolveLayout);
   check(counts.size()==3&&counts[1]==2&&counts[2]==1);
  }
  check(reads==1); // Descriptor-pool churn does not invalidate pipeline metadata.
  check(layouts.get(1,2,7,[]{return std::vector<uint32_t>{4};})[0]==4);
 }
 {
  argent::sfs::DescriptorCountCache<unsigned> descriptors;unsigned calls=0;
  auto resolve=[&]{++calls;return 3u;};
  check(descriptors.get(1,1,42,resolve)==3);
  check(descriptors.get(1,1,42,resolve)==3&&calls==1);
  check(descriptors.get(1,2,42,[]{return 0u;})==0); // Recycled set after pool reset/free.
  check(descriptors.get(2,2,42,resolve)==3&&calls==2); // Device reuse.
  bool failed=false;try{descriptors.get(2,2,43,[]()->uint32_t{throw 1;});}catch(int){failed=true;}
  check(failed&&descriptors.get(2,2,43,resolve)==3);
  for(unsigned i=0;i<10000;++i)check(descriptors.get(2,2,i,[&]{return i%5;})==(i==42||i==43?3:i%5));
  check(descriptors.counts.size()<=4096);
 }
 // A slow driver call must not monopolize the metadata lock. Publication
 // after success, early return, and exceptions must regain exclusive access.
 {
  std::shared_mutex metadata;std::unique_lock<std::shared_mutex> owner(metadata);
  {argent::sfs::UnlockedDriverScope scope(owner);
   auto reader=std::async(std::launch::async,[&]{std::shared_lock<std::shared_mutex> read(metadata);return 42;});
   check(reader.get()==42&&!owner.owns_lock());
  }
  check(owner.owns_lock());
  try{argent::sfs::UnlockedDriverScope scope(owner);throw 7;}catch(int){}
  check(owner.owns_lock());
  auto failure=[&]{argent::sfs::UnlockedDriverScope scope(owner);return -1;};
  check(failure()==-1&&owner.owns_lock());
 }
 RecordingCache<unsigned,Command> cache;std::unordered_map<unsigned,Command> commands;commands.try_emplace(1);unsigned lookups=0;
 auto resolve=[&]() -> Command& {++lookups;return commands.at(1);};
 auto* first=&cache.get(1,1,1,resolve);first->value=42;
 commands.reserve(4096);for(unsigned n=2;n<1024;++n)commands.try_emplace(n);
 check(&cache.get(1,1,1,resolve)==first&&lookups==1&&first->value==42);
 commands.erase(1);commands.try_emplace(1);cache.get(1,2,1,resolve).value=7;
 check(lookups==2&&commands.at(1).value==7);
 Command another;
 check(&cache.get(2,2,1,[&]() -> Command&{++lookups;return another;})==&another&&lookups==3);
 check(&cache.get(2,2,2,resolve)==&commands.at(1)&&lookups==4);
 bool rejected=false;
 try{cache.get(3,3,1,[]() -> Command& {throw std::runtime_error("missing");});}catch(const std::runtime_error&){rejected=true;}
 check(rejected);
 check(&cache.get(3,3,1,resolve)==&commands.at(1)&&lookups==5);
 // Independent command buffers, shared map: no per-recording shared lock.
 std::shared_mutex mutex;std::atomic<uint64_t> retirement{1};
 auto run=[&](bool fast){std::atomic<unsigned> ready{};std::atomic<bool> go{};std::vector<std::thread> threads;
  constexpr unsigned workers=4,count=500000;
  for(unsigned n=1;n<=workers;++n)threads.emplace_back([&,n]{RecordingCache<unsigned,Command> local;
   auto find=[&]() -> Command& {std::shared_lock<std::shared_mutex> lock(mutex);return commands.at(n);};
   ++ready;while(!go.load(std::memory_order_acquire))std::this_thread::yield();
   for(unsigned i=0;i<count;++i){
    if(fast)local.get(5,retirement.load(std::memory_order_acquire),n,find).value=i;
    else {std::shared_lock<std::shared_mutex> lock(mutex);commands.at(n).value=i;}
   }
  });
  while(ready!=workers)std::this_thread::yield();auto start=std::chrono::steady_clock::now();go.store(true,std::memory_order_release);
  for(auto& t:threads)t.join();for(unsigned n=1;n<=workers;++n)check(commands.at(n).value==count-1);
  return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 };
 if(argc>1){const auto before=run(false),after=run(true);std::cout<<"Synthetic 4-thread/2M-local-command lookup: lockedMs="<<before<<" cachedMs="<<after<<'\n';}
 else run(true);
 std::cout<<"PASS: rehash, retirement, device/command reuse, resolver failure and parallel recording\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
