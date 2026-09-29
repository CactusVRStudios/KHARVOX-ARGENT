#include "../src/DeviceFaultReport.h"
#include "../src/GpuAddressTrace.h"
#include <cassert>
#include <cstring>
#include <iostream>
static unsigned calls{},mode{};
static VKAPI_ATTR VkResult VKAPI_CALL query(VkDevice,VkDeviceFaultCountsEXT* counts,VkDeviceFaultInfoEXT* info){
 ++calls;
 if(!info){
  if(mode==2)return VK_ERROR_UNKNOWN;
  counts->addressInfoCount=mode==1?5000:1;counts->vendorInfoCount=1;
  counts->vendorBinarySize=mode==1?128ull*1024*1024:4;return VK_SUCCESS;
 }
 if(mode==1){assert(counts->addressInfoCount==4096&&counts->vendorBinarySize==0&&info->pVendorBinaryData==nullptr);}
 else{assert(counts->vendorBinarySize==4);std::memcpy(info->pVendorBinaryData,"dump",4);}
 info->pAddressInfos[0]={VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT,0x1234000,4096};counts->addressInfoCount=1;
 std::strcpy(info->description,"test fault");std::strcpy(info->pVendorInfos[0].description,"test vendor");
 info->pVendorInfos[0].vendorFaultCode=42;info->pVendorInfos[0].vendorFaultData=7;
 return mode==1?VK_INCOMPLETE:VK_SUCCESS;
}
int main(){
 for(mode=0;mode<3;++mode){
  argent::DeviceFaultReport report;report.query=query;calls=0;std::string log;unsigned saves{};
  auto emit=[&](const std::string& line){log+=line+'\n';};
  auto save=[&](const std::vector<uint8_t>& data){assert(std::string(data.begin(),data.end())=="dump");++saves;};
  report.report(VK_NULL_HANDLE,emit,save);report.report(VK_NULL_HANDLE,emit,save);
  assert(calls==(mode==2?1:2));assert(saves==(mode==0?1:0));
  if(mode!=2){assert(log.find("type=read-invalid address=0x1234000 precision=0x1000")!=std::string::npos);assert(log.find("test vendor")!=std::string::npos);}
  if(mode==1)assert(log.find("skipped=size-limit")!=std::string::npos);
 }
 argent::DeviceFaultReport disabled;disabled.report(VK_NULL_HANDLE,[](auto){assert(false);},[](auto){assert(false);});
 {
  argent::GpuAddressTrace trace;
  VkDebugUtilsObjectNameInfoEXT object{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};object.objectType=VK_OBJECT_TYPE_BUFFER;object.objectHandle=7;
  VkDeviceAddressBindingCallbackDataEXT binding{VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT};binding.baseAddress=0x1000;binding.size=0x200;binding.bindingType=VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT;
  VkDebugUtilsMessengerCallbackDataEXT data{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};data.pNext=&binding;data.objectCount=1;data.pObjects=&object;
  trace.record(data);binding.bindingType=VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT;trace.record(data);
  object.objectHandle=8;binding.bindingType=VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT;trace.record(data);
  const auto path=std::filesystem::temp_directory_path()/"argent-gpu-address-test.tsv";trace.save(path);
  std::ifstream in(path);const std::string text((std::istreambuf_iterator<char>(in)),{});in.close();std::filesystem::remove(path);
  assert(text.find("bound\t3\t9\t0x8\t0x1000\t0x200")!=std::string::npos);
  assert(text.find("unbound\t2\t9\t0x7\t0x1000\t0x200")!=std::string::npos);
  assert(text.find("bound\t1\t")==std::string::npos);
 }
 {
  // r158 lost 39,944 bindings after reaching the old 65,536-entry ceiling.
  // Replay a larger live set and ensure its newest resource is retained.
  argent::GpuAddressTrace trace;
  VkDebugUtilsObjectNameInfoEXT object{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};object.objectType=VK_OBJECT_TYPE_BUFFER;
  VkDeviceAddressBindingCallbackDataEXT binding{VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT};binding.size=4096;binding.bindingType=VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT;
  VkDebugUtilsMessengerCallbackDataEXT data{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};data.pNext=&binding;data.objectCount=1;data.pObjects=&object;
  for(unsigned n=1;n<=157528;++n){object.objectHandle=n;binding.baseAddress=uint64_t(n)*4096;trace.record(data);}
  data.objectCount=0;data.pObjects=nullptr;binding.baseAddress=0x43b00000;binding.flags=VK_DEVICE_ADDRESS_BINDING_INTERNAL_OBJECT_BIT_EXT;trace.record(data);
  binding.bindingType=VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT;trace.record(data);
  const auto path=std::filesystem::temp_directory_path()/"argent-gpu-address-stress.tsv";trace.save(path);
  std::ifstream in(path);const std::string text((std::istreambuf_iterator<char>(in)),{});in.close();std::filesystem::remove(path);
  assert(text.find("events=157530 dropped=0 callbackErrors=0 retiredEvicted=0")!=std::string::npos);
  assert(text.find("bound\t157528\t9\t")!=std::string::npos);
  assert(text.find("unbound\t157530\t0\t0x0\t0x43b00000\t0x1000\t1")!=std::string::npos);
  assert(text.find("bound\t157529\t")==std::string::npos);
 }
 std::cout<<"Device fault report: successful/incomplete/failed queries, bounded allocations, binary output and single reporting passed\n";
}
