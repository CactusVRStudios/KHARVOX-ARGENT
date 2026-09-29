#include "../src/CommandDispatchCache.h"
#include <stdexcept>
#include <iostream>
#include <map>
#include <string>
#include <chrono>
#include <memory>
static void check(bool v){if(!v)throw std::runtime_error("command cache contract failed");}
static void a(){} static void b(){}
int main(int argc,char**){
 try{
  argent::CommandDispatchSlot<void(*)()> first,second;unsigned resolved=0;
  auto ra=[&]{++resolved;return &a;};auto rb=[&]{++resolved;return &b;};
  check(first.get(1,ra)==a&&first.get(1,rb)==a&&resolved==1);
  check(second.get(1,rb)==b&&resolved==2); // Same signature, different command.
  check(first.get(2,rb)==b&&first.get(1,ra)==a&&resolved==4);
  check(first.get(3,[]{return (void(*)())nullptr;})==nullptr);
  check(first.get(3,ra)==nullptr&&resolved==4); // Cache unavailable commands too.
  int old=1,replacement=2;auto key=reinterpret_cast<void*>(1);
  argent::CommandDeviceCache<int> device;unsigned lookups=0;
  auto oldLookup=[&]{++lookups;return &old;};auto newLookup=[&]{++lookups;return &replacement;};
  check(device.get(key,1,oldLookup)==&old&&device.get(key,1,newLookup)==&old&&lookups==1);
  check(device.get(key,2,newLookup)==&replacement&&lookups==2); // Reused dispatch address.
  check(device.get(reinterpret_cast<void*>(2),2,oldLookup)==&old&&lookups==3);
  check(device.get(key,3,[]{return (int*)nullptr;})==nullptr);
  check(device.get(key,3,newLookup)==&replacement&&lookups==4);
  std::cout<<"PASS: command identity, device lifetime, reused handles, null and interleaved devices\n";
  if(argc>1){
   constexpr unsigned n=4000000;volatile uintptr_t sink=0;volatile uint64_t id=1;
   std::map<std::string,void(*)(),std::less<>> names{{"vkCmdDrawIndexed",a}};
   std::shared_ptr<int> owner=std::make_shared<int>(7);std::weak_ptr<int> weak=owner;
   auto measure=[&](auto run){auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<n;++i)run();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
   auto before=measure([&]{auto held=weak.lock();auto fn=names.find("vkCmdDrawIndexed")->second;sink=reinterpret_cast<uintptr_t>(fn)+*held;});
   argent::CommandDispatchSlot<void(*)()> slot;argent::CommandDeviceCache<int> borrowed;
   auto after=measure([&]{auto ptr=borrowed.get(key,id,[&]{return owner.get();});auto fn=slot.get(id,[]{return &a;});sink=reinterpret_cast<uintptr_t>(fn)+*ptr;});
   std::cout<<"Synthetic lookup-only benchmark calls="<<n<<" beforeMs="<<before<<" afterMs="<<after<<" sink="<<sink<<"\n";
  }
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what();return 1;}
}
