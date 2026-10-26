#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <cstring>
#include <memory>
#include <vector>

namespace argent::fg {
// Clone only the chain prefix that needs a different next pointer. Never mutate
// the application's feature structures, and never append duplicate feature nodes.
class DescriptorFeatureRequest {
 std::vector<std::unique_ptr<unsigned char[]>> storage;
 const void* head_{};bool enabled_{};
 static size_t size(VkStructureType s){switch(s){
#define FG_STRUCT(tag,type) case tag:return sizeof(type)
  FG_STRUCT(VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VkLayerDeviceCreateInfo);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,VkPhysicalDeviceFeatures2);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,VkPhysicalDeviceVulkan11Features);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,VkPhysicalDeviceVulkan12Features);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,VkPhysicalDeviceVulkan13Features);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,VkPhysicalDeviceDescriptorIndexingFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES,VkPhysicalDeviceMultiviewFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,VkPhysicalDevice16BitStorageFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES,VkPhysicalDevice8BitStorageFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,VkPhysicalDeviceShaderFloat16Int8Features);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,VkPhysicalDeviceBufferDeviceAddressFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES,VkPhysicalDeviceVariablePointersFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,VkPhysicalDeviceScalarBlockLayoutFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,VkPhysicalDeviceHostQueryResetFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,VkPhysicalDeviceTimelineSemaphoreFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES,VkPhysicalDeviceVulkanMemoryModelFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,VkPhysicalDeviceShaderDrawParametersFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO,VkDeviceGroupDeviceCreateInfo);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,VkPhysicalDeviceRobustness2FeaturesEXT);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR,VkPhysicalDeviceFragmentShadingRateFeaturesKHR);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES,VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES,VkPhysicalDeviceShaderAtomicInt64Features);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES,VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,VkPhysicalDeviceRayTracingPipelineFeaturesKHR);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,VkPhysicalDeviceRayQueryFeaturesKHR);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,VkPhysicalDeviceAccelerationStructureFeaturesKHR);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,VkPhysicalDeviceSynchronization2Features);
  FG_STRUCT(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,VkPhysicalDeviceDynamicRenderingFeatures);
#undef FG_STRUCT
  default:return 0;
 }}
 static VkBool32* flag(VkBaseOutStructure* node){
  if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES)return &reinterpret_cast<VkPhysicalDeviceDescriptorIndexingFeatures*>(node)->descriptorBindingSampledImageUpdateAfterBind;
  if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)return &reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(node)->descriptorBindingSampledImageUpdateAfterBind;
  return nullptr;
 }
 VkBaseOutStructure* copy(const void* p,size_t n){auto bytes=std::make_unique<unsigned char[]>(n);std::memcpy(bytes.get(),p,n);auto result=reinterpret_cast<VkBaseOutStructure*>(bytes.get());storage.push_back(std::move(bytes));return result;}
public:
 DescriptorFeatureRequest(const void* input,bool enable):head_(input){
  std::vector<const VkBaseInStructure*> prefix;
  for(auto n=static_cast<const VkBaseInStructure*>(input);n;n=n->pNext){
   prefix.push_back(n);
   if(n->sType!=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES&&n->sType!=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)continue;
   auto value=n->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES?reinterpret_cast<const VkPhysicalDeviceDescriptorIndexingFeatures*>(n)->descriptorBindingSampledImageUpdateAfterBind:reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(n)->descriptorBindingSampledImageUpdateAfterBind;
   if(value||!enable){enabled_=value!=0;return;}
   for(auto p:prefix)if(!size(p->sType))return; // Unknown prefix: preserve it, fail closed.
   VkBaseOutStructure* next=nullptr;
   for(auto p=prefix.rbegin();p!=prefix.rend();++p){auto clone=copy(*p,size((*p)->sType));if(next)clone->pNext=next;else *flag(clone)=VK_TRUE;next=clone;}
   head_=next;enabled_=true;return;
  }
  if(enable){
   std::vector<const VkBaseInStructure*> loaders;auto tail=static_cast<const VkBaseInStructure*>(input);
   while(tail&&tail->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO){loaders.push_back(tail);tail=tail->pNext;}
   VkPhysicalDeviceDescriptorIndexingFeatures features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};features.pNext=const_cast<VkBaseInStructure*>(tail);features.descriptorBindingSampledImageUpdateAfterBind=VK_TRUE;
   auto next=copy(&features,sizeof(features));for(auto p=loaders.rbegin();p!=loaders.rend();++p){auto clone=copy(*p,sizeof(VkLayerDeviceCreateInfo));clone->pNext=next;next=clone;}head_=next;enabled_=true;
  }
 }
 const void* head()const{return head_;}
 bool enabled()const{return enabled_;}
};
}
