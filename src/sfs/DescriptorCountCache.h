#pragma once
#include <cstdint>
#include <unordered_map>
namespace argent::sfs {
// Values are immutable for the keyed Vulkan object's lifetime. Cache values, never
// metadata pointers. Retirement invalidates recycled handles on every worker.
template<class Handle,class Value=uint32_t> struct DescriptorCountCache {
 uint64_t device{},retirement{};
 std::unordered_map<Handle,Value> counts;
 template<class Resolve> const Value& get(uint64_t d,uint64_t r,Handle h,Resolve resolve){
  if(device!=d||retirement!=r){counts.clear();device=d;retirement=r;}
  auto found=counts.find(h);if(found!=counts.end())return found->second;
  const auto value=resolve();
  if(counts.size()>=4096)counts.clear();
  return counts.emplace(h,value).first->second;
 }
};
}
