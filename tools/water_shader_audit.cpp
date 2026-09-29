// Offline pipeline compilation only: never binds resources or dispatches work.
#include <windows.h>
#include <vulkan/vulkan.h>
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/WaterPipelineRobustness.h"
#include "vendor-analysis/aftermath-sdk/include/GFSDK_Aftermath_GpuCrashDump.h"
#include "vendor-analysis/aftermath-sdk/include/GFSDK_Aftermath_GpuCrashDumpDecoding.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <iomanip>
#include <sstream>
static std::filesystem::path output;
static std::mutex files;
static void ok(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Vulkan result="+std::to_string(r));}
static void GFSDK_AFTERMATH_CALL crash(const void*,uint32_t,void*){}
static void GFSDK_AFTERMATH_CALL debug(const void* data,uint32_t size,void*){
 std::lock_guard<std::mutex> lock(files);
 std::cout<<"debug callback bytes="<<size<<'\n';
 GFSDK_Aftermath_ShaderDebugInfoIdentifier id{};
 const auto result=GFSDK_Aftermath_GetShaderDebugInfoIdentifier(GFSDK_Aftermath_Version_API,data,size,&id);
 if(result!=GFSDK_Aftermath_Result_Success){std::cout<<"debug identifier failed="<<result<<'\n';return;}
 std::ostringstream name;name<<std::hex<<id.id[0]<<'-'<<id.id[1]<<".nvdbg";
 std::ofstream file(output/name.str(),std::ios::binary);file.write(static_cast<const char*>(data),size);
 std::cout<<"debug="<<name.str()<<" bytes="<<size<<'\n';
}
int main(int argc,char** argv){try{
 if(argc<3)throw std::runtime_error("Usage: water_shader_audit original.spv output-directory [robust]");
 output=argv[2];std::filesystem::create_directories(output);
 std::ifstream input(argv[1],std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Missing shader");
 std::vector<uint32_t> words(size_t(input.tellg())/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),words.size()*4);
 argent::sfs::ShaderOptions options;options.vk3dShader=options.volumeShader=0x24abb0e76a065289ull;options.binding=33;
 const bool radial=argc>3&&std::string(argv[3])=="radial";
 if(radial){options={};options.binding=4;options.computeStereo=true;}
 auto source=argent::sfs::stereoSource(words,options);
 if(argc>3&&std::string(argv[3])=="r159"){
  std::ifstream saved("docs/r160-water-analysis/legacy/water.comp");
  if(!saved)throw std::runtime_error("Missing preserved r159 GLSL");
  source=std::string(std::istreambuf_iterator<char>(saved),{});
 }
 if(argc>3&&std::string(argv[3])=="legacy")for(auto pair:std::vector<std::pair<const char*,const char*>>{{"_1927","_1930"},{"_2048","_2051"},{"_2068","_2071"},{"_3542","_3545"}}){
  const auto sampler=std::string("sampler2DArray(_1924[nonuniformEXT(")+pair.first+")], "+pair.second+")";
  const auto wrapped="nonuniformEXT("+sampler+")";const auto at=source.find(wrapped);
  if(at==std::string::npos)throw std::runtime_error("Missing corrected sampler");source.replace(at,wrapped.size(),sampler);
 }
 if(argc>4)source.insert(source.rfind("void main() {")+13,"\nif(gl_GlobalInvocationID.x==1234567u) return;\n");
 std::ofstream(output/"water.comp")<<source;
 const auto shader=argent::sfs::compileGlsl(source,spv::ExecutionModelGLCompute);
 std::ofstream(output/"water.spv",std::ios::binary).write(reinterpret_cast<const char*>(shader.data()),shader.size()*4);
 auto loader=LoadLibraryW(L"vulkan-1.dll");
 auto result=GFSDK_Aftermath_EnableGpuCrashDumps(GFSDK_Aftermath_Version_API,GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,0,crash,debug,nullptr,nullptr,nullptr);
 if(result!=GFSDK_Aftermath_Result_Success)throw std::runtime_error("Aftermath initialization failed="+std::to_string(result));
 auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
 auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_2;
 VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;VkInstance instance{};ok(create(&ici,nullptr,&instance));
#define INSTANCE(n) auto n=reinterpret_cast<PFN_##n>(gipa(instance,#n));if(!n)throw std::runtime_error(#n)
 INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceFeatures2);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkDestroyInstance);
 uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));std::vector<VkPhysicalDevice> ps(count);ok(vkEnumeratePhysicalDevices(instance,&count,ps.data()));if(ps.empty())throw std::runtime_error("No GPU");auto physical=ps.front();
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> qs(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,qs.data());uint32_t family=0;while(family<count&&!(qs[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;
 VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
 VkPhysicalDeviceDiagnosticsConfigFeaturesNV diagnosticFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV};indexing.pNext=&diagnosticFeature;
 VkPhysicalDevicePipelineRobustnessFeaturesEXT pipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT};diagnosticFeature.pNext=&pipelineFeature;
 VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&indexing;vkGetPhysicalDeviceFeatures2(physical,&features);
 if(!diagnosticFeature.diagnosticsConfig)throw std::runtime_error("NVIDIA diagnostics configuration unsupported");
 features.features.robustBufferAccess=argc>3&&std::string(argv[3])=="robust"?VK_TRUE:VK_FALSE;
 VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR executable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};executable.pipelineExecutableInfo=VK_TRUE;indexing.pNext=&executable;
 VkDeviceDiagnosticsConfigCreateInfoNV diagnostics{VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV};diagnostics.flags=VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV|VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV|VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;executable.pNext=&diagnosticFeature;pipelineFeature.pNext=&diagnostics;
 const char* extensions[]{VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME,VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME,VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME,VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME};
 float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
 VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.pNext=&features;dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;dci.enabledExtensionCount=4;dci.ppEnabledExtensionNames=extensions;VkDevice device{};ok(vkCreateDevice(physical,&dci,nullptr,&device));
#define DEVICE(n) auto n=reinterpret_cast<PFN_##n>(vkGetDeviceProcAddr(device,#n));if(!n)throw std::runtime_error(#n)
 DEVICE(vkGetDeviceQueue);DEVICE(vkQueueSubmit);DEVICE(vkQueueWaitIdle);
 VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);ok(vkQueueSubmit(queue,0,nullptr,VK_NULL_HANDLE));ok(vkQueueWaitIdle(queue));
 DEVICE(vkCreateCommandPool);DEVICE(vkAllocateCommandBuffers);DEVICE(vkBeginCommandBuffer);DEVICE(vkCmdSetCheckpointNV);DEVICE(vkEndCommandBuffer);DEVICE(vkDestroyCommandPool);
 VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family;VkCommandPool pool{};ok(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));
 VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;VkCommandBuffer command{};ok(vkAllocateCommandBuffers(device,&alloc,&command));
 VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(command,&begin));vkCmdSetCheckpointNV(command,reinterpret_cast<void*>(1));ok(vkEndCommandBuffer(command));
 VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;ok(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));ok(vkQueueWaitIdle(queue));vkDestroyCommandPool(device,pool,nullptr);
 DEVICE(vkCreateDescriptorSetLayout);DEVICE(vkCreatePipelineLayout);DEVICE(vkCreateShaderModule);DEVICE(vkCreateComputePipelines);DEVICE(vkGetPipelineExecutablePropertiesKHR);DEVICE(vkGetPipelineExecutableInternalRepresentationsKHR);DEVICE(vkGetPipelineExecutableStatisticsKHR);DEVICE(vkDestroyPipeline);DEVICE(vkDestroyShaderModule);DEVICE(vkDestroyPipelineLayout);DEVICE(vkDestroyDescriptorSetLayout);DEVICE(vkDestroyDevice);
 // Exact captured original layouts, plus SFS's reserved UBO in every set.
 const std::vector<std::vector<int>> types=radial?std::vector<std::vector<int>>{{6,6,7,0},{6,2,3}}:std::vector<std::vector<int>>{{6,6,2,0,2,2,2,2,0,7,6,6,6,0,2,2,0,0,2,7,6,6,2,0,7,0,6,2,0,2,0,2,0},{6,3,3,3,3,2},{2}};
 VkDescriptorSetLayout layouts[3]{};
 for(unsigned set=0;set<types.size();++set){std::vector<VkDescriptorSetLayoutBinding> bindings;
  for(uint32_t b=0;b<types[set].size();++b)bindings.push_back({b,static_cast<VkDescriptorType>(types[set][b]),set==2?34816u:1u,VK_SHADER_STAGE_ALL,nullptr});
  bindings.push_back({uint32_t(bindings.size()),VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,49,nullptr});
  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.bindingCount=uint32_t(bindings.size());ci.pBindings=bindings.data();ok(vkCreateDescriptorSetLayout(device,&ci,nullptr,&layouts[set]));
 }
 VkPushConstantRange push{VK_SHADER_STAGE_ALL,0,128};VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pci.setLayoutCount=uint32_t(types.size());pci.pSetLayouts=layouts;pci.pushConstantRangeCount=1;pci.pPushConstantRanges=&push;VkPipelineLayout layout{};ok(vkCreatePipelineLayout(device,&pci,nullptr,&layout));
 VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smci.codeSize=shader.size()*4;smci.pCode=shader.data();VkShaderModule module{};ok(vkCreateShaderModule(device,&smci,nullptr,&module));
 DEVICE(vkCreatePipelineCache);DEVICE(vkGetPipelineCacheData);DEVICE(vkDestroyPipelineCache);
 VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};VkPipelineCache cache{};ok(vkCreatePipelineCache(device,&cacheInfo,nullptr,&cache));
 VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.flags=VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR|VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;ci.layout=layout;ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.module=module;ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.pName="main";VkPipelineRobustnessCreateInfoEXT robustness{};if(!radial)argent::sfs::protectWaterPipeline(0x24abb0e76a065289ull,true,ci,robustness);VkPipeline pipeline{};ok(vkCreateComputePipelines(device,cache,1,&ci,nullptr,&pipeline));
 // Bind the compiled pipeline in a submitted command buffer, without dispatch.
 // Some driver work is deferred until a pipeline is first recorded for use.
 DEVICE(vkCmdBindPipeline);
 ok(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));alloc.commandPool=pool;
 ok(vkAllocateCommandBuffers(device,&alloc,&command));ok(vkBeginCommandBuffer(command,&begin));
 vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
 ok(vkEndCommandBuffer(command));submit.pCommandBuffers=&command;
 ok(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));ok(vkQueueWaitIdle(queue));
 vkDestroyCommandPool(device,pool,nullptr);
 VkPipelineExecutableInfoKHR info{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};info.pipeline=pipeline;
 ok(vkGetPipelineExecutableStatisticsKHR(device,&info,&count,nullptr));std::vector<VkPipelineExecutableStatisticKHR> stats(count,{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});ok(vkGetPipelineExecutableStatisticsKHR(device,&info,&count,stats.data()));for(const auto& s:stats)std::cout<<s.name<<"="<<s.value.u64<<'\n';
 ok(vkGetPipelineExecutableInternalRepresentationsKHR(device,&info,&count,nullptr));std::vector<VkPipelineExecutableInternalRepresentationKHR> reps(count,{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR});ok(vkGetPipelineExecutableInternalRepresentationsKHR(device,&info,&count,reps.data()));std::vector<std::vector<char>> data(count);for(uint32_t n=0;n<count;++n){data[n].resize(reps[n].dataSize);reps[n].pData=data[n].data();}ok(vkGetPipelineExecutableInternalRepresentationsKHR(device,&info,&count,reps.data()));for(uint32_t n=0;n<count;++n){std::cout<<"representation="<<reps[n].name<<" size="<<reps[n].dataSize<<'\n';std::ofstream(output/("representation-"+std::to_string(n)+(reps[n].isText?".txt":".bin")),std::ios::binary).write(data[n].data(),reps[n].dataSize);}
 size_t cacheSize{};ok(vkGetPipelineCacheData(device,cache,&cacheSize,nullptr));std::vector<char> cacheData(cacheSize);ok(vkGetPipelineCacheData(device,cache,&cacheSize,cacheData.data()));
 std::ofstream(output/"pipeline-cache.bin",std::ios::binary).write(cacheData.data(),cacheSize);vkDestroyPipelineCache(device,cache,nullptr);
 vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,module,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);for(auto l:layouts)if(l)vkDestroyDescriptorSetLayout(device,l,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);GFSDK_Aftermath_DisableGpuCrashDumps();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
