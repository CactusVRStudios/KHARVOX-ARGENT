#pragma once
#include "QuadRuntime.h"
namespace argent::capture {
void shader(VkDevice,VkShaderModule,const VkShaderModuleCreateInfo*) noexcept;
void forgetShader(VkDevice,VkShaderModule) noexcept;
void graphics(VkDevice,uint32_t,const VkGraphicsPipelineCreateInfo*,const VkPipeline*) noexcept;
void compute(VkDevice,uint32_t,const VkComputePipelineCreateInfo*,const VkPipeline*) noexcept;
void descriptorLayout(VkDevice,VkDescriptorSetLayout,const VkDescriptorSetLayoutCreateInfo*) noexcept;
void pipelineLayout(VkDevice,VkPipelineLayout,const VkPipelineLayoutCreateInfo*) noexcept;
void renderPass(VkDevice,VkRenderPass,const VkRenderPassCreateInfo*) noexcept;
void forgetDevice(VkDevice) noexcept;
std::string shaderIdentity(VkDevice,VkShaderModule) noexcept;
}
