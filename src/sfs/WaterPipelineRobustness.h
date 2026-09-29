#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
namespace argent::sfs {
// Only the shader identified by both r157 fault dumps/checkpoints. The feature
// enables pipeline overrides; other pipelines retain their device defaults.
inline bool protectWaterPipeline(uint64_t hash,bool enabled,VkComputePipelineCreateInfo& info,VkPipelineRobustnessCreateInfoEXT& robustness){
 if(!enabled||hash!=0x24abb0e76a065289ull)return false;
 for(auto n=static_cast<const VkBaseInStructure*>(info.stage.pNext);n;n=n->pNext)
  if(n->sType==VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO_EXT)return false;
 robustness={VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO_EXT};
 robustness.storageBuffers=VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT;
 robustness.uniformBuffers=VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT;
 robustness.vertexInputs=VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT_EXT;
 robustness.images=VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2_EXT;
 robustness.pNext=info.stage.pNext;info.stage.pNext=&robustness;return true;
}
}
