#include <windows.h>
#include <vulkan/vulkan.h>
#include "../src/sfs/ShaderCompiler.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void ok(VkResult value){if(value!=VK_SUCCESS)throw std::runtime_error("Vulkan result "+std::to_string(value));}
int main(){try{
 auto loader=LoadLibraryW(L"vulkan-1.dll");check(loader,"Vulkan loader unavailable");
 auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
 auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
 VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;VkInstance instance{};ok(create(&ici,nullptr,&instance));
#define INSTANCE(name) auto name=reinterpret_cast<PFN_##name>(gipa(instance,#name));check(name,#name)
 INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkDestroyInstance);
 uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));check(count,"No GPU");std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));const auto physical=physicals.front();
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
 uint32_t family=UINT32_MAX;for(uint32_t n=0;n<count;++n)if(families[n].queueFlags&VK_QUEUE_COMPUTE_BIT){family=n;break;}check(family!=UINT32_MAX,"No compute queue");
 float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
 VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;VkDevice device{};ok(vkCreateDevice(physical,&dci,nullptr,&device));
#define DEVICE(name) auto name=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name));check(name,#name)
 DEVICE(vkGetDeviceQueue);DEVICE(vkCreateImage);DEVICE(vkDestroyImage);DEVICE(vkGetImageMemoryRequirements);DEVICE(vkAllocateMemory);DEVICE(vkFreeMemory);DEVICE(vkBindImageMemory);DEVICE(vkCreateImageView);DEVICE(vkDestroyImageView);
 DEVICE(vkCreateBuffer);DEVICE(vkDestroyBuffer);DEVICE(vkGetBufferMemoryRequirements);DEVICE(vkBindBufferMemory);DEVICE(vkMapMemory);DEVICE(vkUnmapMemory);DEVICE(vkCreateSampler);DEVICE(vkDestroySampler);
 DEVICE(vkCreateDescriptorSetLayout);DEVICE(vkDestroyDescriptorSetLayout);DEVICE(vkCreateDescriptorPool);DEVICE(vkDestroyDescriptorPool);DEVICE(vkAllocateDescriptorSets);DEVICE(vkUpdateDescriptorSets);
 DEVICE(vkCreatePipelineLayout);DEVICE(vkDestroyPipelineLayout);DEVICE(vkCreateShaderModule);DEVICE(vkDestroyShaderModule);DEVICE(vkCreateComputePipelines);DEVICE(vkDestroyPipeline);
 DEVICE(vkCreateCommandPool);DEVICE(vkDestroyCommandPool);DEVICE(vkAllocateCommandBuffers);DEVICE(vkBeginCommandBuffer);DEVICE(vkEndCommandBuffer);DEVICE(vkCmdPipelineBarrier);DEVICE(vkCmdClearColorImage);DEVICE(vkCmdBindPipeline);DEVICE(vkCmdBindDescriptorSets);DEVICE(vkCmdDispatch);DEVICE(vkCmdCopyImageToBuffer);
 DEVICE(vkCreateFence);DEVICE(vkDestroyFence);DEVICE(vkQueueSubmit);DEVICE(vkWaitForFences);DEVICE(vkDestroyDevice);
 VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
 auto memoryType=[&](uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t n=0;n<memory.memoryTypeCount;++n)if((bits&(1u<<n))&&(memory.memoryTypes[n].propertyFlags&flags)==flags)return n;throw std::runtime_error("No memory type");};
 std::array<VkImage,2> images{};std::array<VkDeviceMemory,2> imageMemory{};std::array<VkImageView,3> views{};
 for(unsigned n=0;n<2;++n){VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R32G32B32A32_SFLOAT;ci.extent={8,8,1};ci.mipLevels=1;ci.arrayLayers=2;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;ok(vkCreateImage(device,&ci,nullptr,&images[n]));VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,images[n],&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);ok(vkAllocateMemory(device,&ai,nullptr,&imageMemory[n]));ok(vkBindImageMemory(device,images[n],imageMemory[n],0));}
 for(unsigned n=0;n<3;++n){VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=n<2?images[0]:images[1];ci.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;ci.format=VK_FORMAT_R32G32B32A32_SFLOAT;ci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,n==0?1u:2u};ok(vkCreateImageView(device,&ci,nullptr,&views[n]));}
 VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sci.magFilter=sci.minFilter=VK_FILTER_NEAREST;sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;VkSampler sampler{};ok(vkCreateSampler(device,&sci,nullptr,&sampler));
 VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bci.size=2*8*8*4*sizeof(float);bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;VkBuffer buffer{};ok(vkCreateBuffer(device,&bci,nullptr,&buffer));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffer,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VkDeviceMemory readback{};ok(vkAllocateMemory(device,&ai,nullptr,&readback));ok(vkBindBufferMemory(device,buffer,readback,0));
 const std::array<VkDescriptorSetLayoutBinding,3> bindings{{{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
 VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lci.bindingCount=3;lci.pBindings=bindings.data();VkDescriptorSetLayout layout{};ok(vkCreateDescriptorSetLayout(device,&lci,nullptr,&layout));VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pci.maxSets=1;pci.poolSizeCount=2;pci.pPoolSizes=sizes;VkDescriptorPool pool{};ok(vkCreateDescriptorPool(device,&pci,nullptr,&pool));VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};dai.descriptorPool=pool;dai.descriptorSetCount=1;dai.pSetLayouts=&layout;VkDescriptorSet set{};ok(vkAllocateDescriptorSets(device,&dai,&set));
 for(unsigned n=0;n<3;++n){VkDescriptorImageInfo image{n<2?sampler:VK_NULL_HANDLE,views[n],VK_IMAGE_LAYOUT_GENERAL};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=n;write.descriptorCount=1;write.descriptorType=bindings[n].descriptorType;write.pImageInfo=&image;vkUpdateDescriptorSets(device,1,&write,0,nullptr);}
 const auto original=argent::sfs::compileGlsl(R"(#version 460
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform sampler2D monoTex;
layout(set=0,binding=1) uniform sampler2D stereoTex;
layout(set=0,binding=2,rgba32f) uniform writeonly image2D outputTex;
shared float values[64];
void main(){
 ivec2 p=ivec2(gl_GlobalInvocationID.xy);
 values[gl_LocalInvocationIndex]=textureLod(monoTex,vec2(0.5),0).x;
 barrier();
 imageStore(outputTex,p,vec4(values[(gl_LocalInvocationIndex+1u)%64u],textureGrad(stereoTex,vec2(0.5),vec2(0),vec2(0)).x,texelFetch(monoTex,p,0).x,textureLod(stereoTex,vec2(0.5),0).x));
})",spv::ExecutionModelGLCompute);
 argent::sfs::ShaderOptions options;options.nativeSampleLayer=true;const auto words=argent::sfs::compileStereoShader(original,options);
 VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smci.codeSize=words.size()*4;smci.pCode=words.data();VkShaderModule module{};ok(vkCreateShaderModule(device,&smci,nullptr,&module));VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};plci.setLayoutCount=1;plci.pSetLayouts=&layout;VkPipelineLayout pipelineLayout{};ok(vkCreatePipelineLayout(device,&plci,nullptr,&pipelineLayout));VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cpci.layout=pipelineLayout;cpci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};cpci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cpci.stage.module=module;cpci.stage.pName="main";VkPipeline pipeline{};ok(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cpci,nullptr,&pipeline));
 VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpi.queueFamilyIndex=family;VkCommandPool commandPool{};ok(vkCreateCommandPool(device,&cpi,nullptr,&commandPool));VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=commandPool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;VkCommandBuffer command{};ok(vkAllocateCommandBuffers(device,&cai,&command));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(command,&begin));
 auto barrier=[&](VkImage image,VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags src,VkAccessFlags dst){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=oldLayout;b.newLayout=newLayout;b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
 barrier(images[0],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
 for(unsigned eye=0;eye<2;++eye){VkClearColorValue color{};color.float32[0]=eye?0.75f:0.25f;VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1};vkCmdClearColorImage(command,images[0],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);}
 barrier(images[0],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);barrier(images[1],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
 vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&set,0,nullptr);vkCmdDispatch(command,1,1,2);
 barrier(images[1],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};copy.imageExtent={8,8,1};vkCmdCopyImageToBuffer(command,images[1],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);ok(vkEndCommandBuffer(command));
 VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};ok(vkCreateFence(device,&fci,nullptr,&fence));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;ok(vkQueueSubmit(queue,1,&submit,fence));ok(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));void* mapped{};ok(vkMapMemory(device,readback,0,bci.size,0,&mapped));const auto* values=static_cast<const float*>(mapped);
 for(unsigned eye=0;eye<2;++eye)for(unsigned pixel=0;pixel<64;++pixel)for(unsigned channel=0;channel<4;++channel){const float expected=channel%2&&eye?0.75f:0.25f;check(std::abs(values[(eye*64+pixel)*4+channel]-expected)<0.0001f,"Incorrect sampled/fetched mono or stereo layer");}
 vkUnmapMemory(device,readback);
 vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,commandPool,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,pipelineLayout,nullptr);vkDestroyShaderModule(device,module,nullptr);vkDestroyDescriptorPool(device,pool,nullptr);vkDestroyDescriptorSetLayout(device,layout,nullptr);vkDestroySampler(device,sampler,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,readback,nullptr);for(auto view:views)vkDestroyImageView(device,view,nullptr);for(unsigned n=0;n<2;++n){vkDestroyImage(device,images[n],nullptr);vkFreeMemory(device,imageMemory[n],nullptr);}vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
 std::cout<<"GPU sampling: one/two-layer views, LOD, gradients, integer fetch and shared-memory barrier passed for both eyes\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
