#pragma once
#include "NativeDispatch.h"
#include "ShaderCompiler.h"
#include <memory>
#include <vector>

namespace argent::sfs {
// Occlusion queries in a multiview pass occupy two consecutive physical slots.
// Sum both without a CPU readback or shaderInt64 requirement. Application query
// buffers only need TRANSFER_DST usage; the internal storage never aliases them.
class QueryResolvePipeline {
public:
 VkDevice device{};kharvox::sfs::NativeDispatch api;
 VkPhysicalDeviceMemoryProperties memory{};
 VkDescriptorSetLayout descriptors{};VkPipelineLayout layout{};VkPipeline pipeline{};
 static void check(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("Occlusion query resolver Vulkan allocation failed: "+std::to_string(result));}
 void initialize(VkDevice d,const kharvox::sfs::NativeDispatch& dispatch,const VkPhysicalDeviceMemoryProperties& properties){
  device=d;api=dispatch;memory=properties;
  VkDescriptorSetLayoutBinding bindings[2]{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
  VkDescriptorSetLayoutCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};di.bindingCount=2;di.pBindings=bindings;
  check(api.vkCreateDescriptorSetLayout(device,&di,nullptr,&descriptors));
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,12};
  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&descriptors;li.pushConstantRangeCount=1;li.pPushConstantRanges=&range;
  check(api.vkCreatePipelineLayout(device,&li,nullptr,&layout));
  const auto words=compileGlsl(R"(#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) readonly buffer Raw {uvec4 value[];} raw;
layout(set=0,binding=1,std430) writeonly buffer Result {uint value[];} result;
layout(push_constant) uniform Params {uint count;uint wide;uint availability;} p;
void main(){
 uint i=gl_GlobalInvocationID.x;if(i>=p.count)return;
 uvec4 a=raw.value[2*i],b=raw.value[2*i+1];
 uint low=a.x+b.x;uint high=a.y+b.y+uint(low<a.x);
 uint available=uint(any(notEqual(a.zw,uvec2(0)))&&any(notEqual(b.zw,uvec2(0))));
 uint step=(1+p.wide)*(1+p.availability),base=i*step;
 result.value[base]=low;
 if(p.wide!=0)result.value[base+1]=high;
 if(p.availability!=0){result.value[base+1+p.wide]=available;if(p.wide!=0)result.value[base+3]=0;}
}
)",spv::ExecutionModelGLCompute);
  VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};si.codeSize=words.size()*4;si.pCode=words.data();VkShaderModule shader{};
  check(api.vkCreateShaderModule(device,&si,nullptr,&shader));
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.layout=layout;ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=shader;ci.stage.pName="main";
  auto status=api.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline);api.vkDestroyShaderModule(device,shader,nullptr);check(status);
 }
 ~QueryResolvePipeline(){if(pipeline)api.vkDestroyPipeline(device,pipeline,nullptr);if(layout)api.vkDestroyPipelineLayout(device,layout,nullptr);if(descriptors)api.vkDestroyDescriptorSetLayout(device,descriptors,nullptr);}
};
class QueryResolveScratch {
 QueryResolvePipeline& owner;
 VkBuffer raw{},result{};VkDeviceMemory rawMemory{},resultMemory{};
 VkDescriptorPool pool{};VkDescriptorSet set{};
 void buffer(VkDeviceSize size,VkBufferUsageFlags usage,VkBuffer& handle,VkDeviceMemory& memory){
  auto& a=owner.api;auto d=owner.device;VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=size;bi.usage=usage;
  QueryResolvePipeline::check(a.vkCreateBuffer(d,&bi,nullptr,&handle));VkMemoryRequirements r{};a.vkGetBufferMemoryRequirements(d,handle,&r);
  uint32_t type=UINT32_MAX;for(uint32_t i=0;i<owner.memory.memoryTypeCount;++i)if(r.memoryTypeBits&(1u<<i)){type=i;if(owner.memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)break;}
  if(type==UINT32_MAX)throw std::runtime_error("No query resolver memory type");
  VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mi.allocationSize=r.size;mi.memoryTypeIndex=type;
  QueryResolvePipeline::check(a.vkAllocateMemory(d,&mi,nullptr,&memory));QueryResolvePipeline::check(a.vkBindBufferMemory(d,handle,memory,0));
 }
public:
 uint32_t capacity{};
 explicit QueryResolveScratch(QueryResolvePipeline& p):owner(p){}
 void initialize(uint32_t count){
  capacity=count;buffer(VkDeviceSize(count)*32,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,raw,rawMemory);
  buffer(VkDeviceSize(count)*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,result,resultMemory);
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=1;pi.poolSizeCount=1;pi.pPoolSizes=&size;
  QueryResolvePipeline::check(owner.api.vkCreateDescriptorPool(owner.device,&pi,nullptr,&pool));
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&owner.descriptors;
  QueryResolvePipeline::check(owner.api.vkAllocateDescriptorSets(owner.device,&ai,&set));
  VkDescriptorBufferInfo buffers[2]{{raw,0,VkDeviceSize(count)*32},{result,0,VkDeviceSize(count)*16}};
  VkWriteDescriptorSet writes[2]{};for(uint32_t i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i];}
  owner.api.vkUpdateDescriptorSets(owner.device,2,writes,0,nullptr);
 }
 ~QueryResolveScratch(){auto& a=owner.api;auto d=owner.device;if(pool)a.vkDestroyDescriptorPool(d,pool,nullptr);if(raw)a.vkDestroyBuffer(d,raw,nullptr);if(result)a.vkDestroyBuffer(d,result,nullptr);if(rawMemory)a.vkFreeMemory(d,rawMemory,nullptr);if(resultMemory)a.vkFreeMemory(d,resultMemory,nullptr);}
 void copy(VkCommandBuffer cb,VkQueryPool queries,uint32_t first,uint32_t count,VkBuffer dst,VkDeviceSize offset,VkDeviceSize stride,VkQueryResultFlags flags){
  auto& a=owner.api;
  // Internal results always include 64-bit availability; the user's flags and
  // stride are applied by the resolver and final transfer. WAIT/PARTIAL survive.
  a.vkCmdCopyQueryPoolResults(cb,queries,first*2,count*2,raw,0,16,flags|VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  a.vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
  a.vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,owner.pipeline);a.vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,owner.layout,0,1,&set,0,nullptr);
  const uint32_t params[3]{count,uint32_t(bool(flags&VK_QUERY_RESULT_64_BIT)),uint32_t(bool(flags&VK_QUERY_RESULT_WITH_AVAILABILITY_BIT))};
  a.vkCmdPushConstants(cb,owner.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),params);a.vkCmdDispatch(cb,(count+63)/64,1,1);
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  a.vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
  const VkDeviceSize bytes=4*(1+params[1])*(1+params[2]);
  if(stride==bytes||count==1){VkBufferCopy region{0,offset,bytes*count};a.vkCmdCopyBuffer(cb,result,dst,1,&region);}
  else {std::vector<VkBufferCopy> regions;regions.reserve(count);for(uint32_t i=0;i<count;++i)regions.push_back({i*bytes,offset+i*stride,bytes});a.vkCmdCopyBuffer(cb,result,dst,count,regions.data());}
 }
};
}
