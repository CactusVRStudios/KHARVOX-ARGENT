#pragma once
#include <vulkan/vulkan.h>
#include <map>
#include <deque>
#include <tuple>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <algorithm>
namespace argent {
// Resource lifetime events only; no per-frame readback or shader instrumentation.
class GpuAddressTrace {
 struct Row {uint64_t serial,object,base,size;uint32_t type,flags;};
 std::mutex mutex;
 std::map<std::tuple<uint64_t,uint32_t,uint64_t>,Row> active;
 std::deque<Row> retired;
 uint64_t serial{},dropped{},retiredEvicted{};
 std::atomic<uint64_t> callbackErrors{};
 static constexpr size_t activeLimit=1048576,retiredLimit=65536;
public:
 void record(const VkDebugUtilsMessengerCallbackDataEXT& data){
  auto node=static_cast<const VkBaseInStructure*>(data.pNext);
  while(node&&node->sType!=VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT)node=node->pNext;
  if(!node)return;
  const auto& binding=*reinterpret_cast<const VkDeviceAddressBindingCallbackDataEXT*>(node);
  std::lock_guard<std::mutex> lock(mutex);
  // Driver-internal bindings can have no application object. Keep their address
  // ranges too; otherwise a missing match could falsely suggest a stale object.
  VkDebugUtilsObjectNameInfoEXT internal{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
  for(uint32_t n=0;n<std::max(1u,data.objectCount);++n){const auto& object=data.objectCount?data.pObjects[n]:internal;
   auto key=std::make_tuple(binding.baseAddress,uint32_t(object.objectType),object.objectHandle);
   Row row{++serial,object.objectHandle,binding.baseAddress,binding.size,uint32_t(object.objectType),binding.flags};
   if(binding.bindingType==VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT){
    if(active.size()<activeLimit||active.count(key))active[key]=row;else ++dropped;
   }else{
    active.erase(key);retired.push_back(row);if(retired.size()>retiredLimit){retired.pop_front();++retiredEvicted;}
   }
  }
 }
 void callbackFailed()noexcept{callbackErrors.fetch_add(1,std::memory_order_relaxed);}
 void save(const std::filesystem::path& path){
  std::lock_guard<std::mutex> lock(mutex);std::ofstream out(path);
  out<<"# events="<<serial<<" dropped="<<dropped<<" callbackErrors="<<callbackErrors.load()<<" retiredEvicted="<<retiredEvicted<<" activeLimit="<<activeLimit<<"\nstate\tserial\tobjectType\tobject\tbase\tsize\tflags\n";
  auto write=[&](const char* state,const Row& r){out<<state<<'\t'<<std::dec<<r.serial<<'\t'<<r.type<<"\t0x"<<std::hex<<r.object<<"\t0x"<<r.base<<"\t0x"<<r.size<<'\t'<<std::dec<<r.flags<<'\n';};
  for(const auto& item:active)write("bound",item.second);
  for(const auto& row:retired)write("unbound",row);
  if(!out)throw std::runtime_error("Cannot write GPU address trace");
 }
};
// Shared by the game instance and its device. Ignore unrelated runtime instances.
inline GpuAddressTrace& gpuAddressTrace(){static GpuAddressTrace trace;return trace;}
inline VKAPI_ATTR VkBool32 VKAPI_CALL gpuAddressCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,VkDebugUtilsMessageTypeFlagsEXT types,const VkDebugUtilsMessengerCallbackDataEXT* data,void*) noexcept {
 if(data&&(types&VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT))try{gpuAddressTrace().record(*data);}catch(...){gpuAddressTrace().callbackFailed();}
 return VK_FALSE;
}
}
