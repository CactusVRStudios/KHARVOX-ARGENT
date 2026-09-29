#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

namespace argent::perf {
// Private pools, downstream entry points, no WAIT_BIT and no added submission.
// The caller must have proved completion before read/destroy. A reset is always
// recorded in the same CB before its timestamps, never host-reset in flight.
template<unsigned N> struct GpuTiming {
 VkDevice device{};VkQueryPool pool{};float period{};uint32_t bits{};bool recorded{};
 PFN_vkDestroyQueryPool destroy{};PFN_vkCmdResetQueryPool reset{};
 PFN_vkCmdWriteTimestamp stamp{};PFN_vkGetQueryPoolResults results{};
 bool initialize(VkDevice d,PFN_vkGetDeviceProcAddr resolver,float ns,uint32_t valid){
  if(pool)return true;if(!resolver||valid==0||valid>64||ns<=0)return false;
  device=d;period=ns;bits=valid;
  auto create=reinterpret_cast<PFN_vkCreateQueryPool>(resolver(d,"vkCreateQueryPool"));
  destroy=reinterpret_cast<PFN_vkDestroyQueryPool>(resolver(d,"vkDestroyQueryPool"));
  reset=reinterpret_cast<PFN_vkCmdResetQueryPool>(resolver(d,"vkCmdResetQueryPool"));
  stamp=reinterpret_cast<PFN_vkCmdWriteTimestamp>(resolver(d,"vkCmdWriteTimestamp"));
  results=reinterpret_cast<PFN_vkGetQueryPoolResults>(resolver(d,"vkGetQueryPoolResults"));
  if(!create||!destroy||!reset||!stamp||!results)return false;
  VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=N;
  if(create(d,&info,nullptr,&pool)!=VK_SUCCESS)pool=VK_NULL_HANDLE;return bool(pool);
 }
 void begin(VkCommandBuffer cb,bool selected){recorded=bool(pool)&&selected;if(recorded){reset(cb,pool,0,N);stamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool,0);}}
 void point(VkCommandBuffer cb,unsigned index){if(recorded&&index>0&&index<N)stamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pool,index);}
 bool read(std::array<uint64_t,N>& ticks,unsigned count=N){
  if(!recorded||count==0||count>N)return false;
  struct Value{uint64_t tick,available;};std::array<Value,N> values{};
  if(results(device,pool,0,count,sizeof(values),values.data(),sizeof(Value),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)!=VK_SUCCESS)return false;
  for(unsigned i=0;i<count;++i){if(!values[i].available)return false;ticks[i]=values[i].tick;}return true;
 }
 double ms(uint64_t start,uint64_t end)const{const uint64_t mask=bits==64?UINT64_MAX:((uint64_t{1}<<bits)-1);return double((end-start)&mask)*double(period)/1000000.;}
 void shutdownAfterCompletion(){if(pool)destroy(device,pool,nullptr);pool=VK_NULL_HANDLE;recorded=false;}
};
}
