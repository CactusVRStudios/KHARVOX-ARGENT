#include <windows.h>
#include <vulkan/vulkan.h>
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/MaterialPipelineCapture.h"
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
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_2;
 VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;VkInstance instance{};ok(create(&ici,nullptr,&instance));
#define INSTANCE(name) auto name=reinterpret_cast<PFN_##name>(gipa(instance,#name));check(name,#name)
 INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkDestroyInstance);
 uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));check(count,"No GPU");std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));const auto physical=physicals.front();
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
 uint32_t family=UINT32_MAX;for(uint32_t n=0;n<count;++n)if(families[n].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=n;break;}check(family!=UINT32_MAX,"No compute queue");
 float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
 VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;INSTANCE(vkGetPhysicalDeviceFeatures2);
 VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
 VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};VkPhysicalDeviceMultiviewFeatures multiview{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};indexing.pNext=&multiview;supported.pNext=&indexing;vkGetPhysicalDeviceFeatures2(physical,&supported);
 check(indexing.shaderSampledImageArrayNonUniformIndexing,"Nonuniform sampled image indexing unsupported");
 VkPhysicalDeviceDescriptorIndexingFeatures enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};enabled.shaderSampledImageArrayNonUniformIndexing=VK_TRUE;check(multiview.multiview,"Multiview unsupported");enabled.pNext=&multiview;dci.pNext=&enabled;
 VkDevice device{};ok(vkCreateDevice(physical,&dci,nullptr,&device));
#define DEVICE(name) auto name=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name));check(name,#name)
 DEVICE(vkGetDeviceQueue);DEVICE(vkCreateImage);DEVICE(vkDestroyImage);DEVICE(vkGetImageMemoryRequirements);DEVICE(vkAllocateMemory);DEVICE(vkFreeMemory);DEVICE(vkBindImageMemory);DEVICE(vkCreateImageView);DEVICE(vkDestroyImageView);
 DEVICE(vkCreateBuffer);DEVICE(vkDestroyBuffer);DEVICE(vkGetBufferMemoryRequirements);DEVICE(vkBindBufferMemory);DEVICE(vkMapMemory);DEVICE(vkUnmapMemory);DEVICE(vkCreateSampler);DEVICE(vkDestroySampler);
 DEVICE(vkCreateDescriptorSetLayout);DEVICE(vkDestroyDescriptorSetLayout);DEVICE(vkCreateDescriptorPool);DEVICE(vkDestroyDescriptorPool);DEVICE(vkAllocateDescriptorSets);DEVICE(vkUpdateDescriptorSets);
 DEVICE(vkCreatePipelineLayout);DEVICE(vkDestroyPipelineLayout);DEVICE(vkCreateShaderModule);DEVICE(vkDestroyShaderModule);DEVICE(vkCreateComputePipelines);DEVICE(vkDestroyPipeline);
 DEVICE(vkCreateCommandPool);DEVICE(vkDestroyCommandPool);DEVICE(vkAllocateCommandBuffers);DEVICE(vkBeginCommandBuffer);DEVICE(vkEndCommandBuffer);DEVICE(vkCmdPipelineBarrier);DEVICE(vkCmdClearColorImage);DEVICE(vkCmdBindPipeline);DEVICE(vkCmdBindDescriptorSets);DEVICE(vkCmdDispatch);DEVICE(vkCmdCopyImageToBuffer);
 DEVICE(vkCreateFence);DEVICE(vkDestroyFence);DEVICE(vkQueueSubmit);DEVICE(vkWaitForFences);DEVICE(vkDestroyDevice);
 VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
 auto memoryType=[&](uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t n=0;n<memory.memoryTypeCount;++n)if((bits&(1u<<n))&&(memory.memoryTypes[n].propertyFlags&flags)==flags)return n;throw std::runtime_error("No memory type");};
 std::array<VkImage,5> images{};std::array<VkDeviceMemory,5> imageMemory{};std::array<VkImageView,5> views{};
 for(unsigned n=0;n<5;++n){VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R32G32B32A32_SFLOAT;ci.extent={8,8,1};ci.mipLevels=1;ci.arrayLayers=2;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;ok(vkCreateImage(device,&ci,nullptr,&images[n]));VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,images[n],&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);ok(vkAllocateMemory(device,&ai,nullptr,&imageMemory[n]));ok(vkBindImageMemory(device,images[n],imageMemory[n],0));}
 for(unsigned n=0;n<5;++n){VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=images[n];ci.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;ci.format=VK_FORMAT_R32G32B32A32_SFLOAT;ci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,n<2?1u:2u};ok(vkCreateImageView(device,&ci,nullptr,&views[n]));}
 VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sci.magFilter=sci.minFilter=VK_FILTER_NEAREST;sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;VkSampler sampler{};ok(vkCreateSampler(device,&sci,nullptr,&sampler));
 VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bci.size=2*8*8*4*sizeof(float);bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;VkBuffer buffer{};ok(vkCreateBuffer(device,&bci,nullptr,&buffer));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffer,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VkDeviceMemory readback{};ok(vkAllocateMemory(device,&ai,nullptr,&readback));ok(vkBindBufferMemory(device,buffer,readback,0));
 const std::array<VkDescriptorSetLayoutBinding,3> bindings{{{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}}};
 VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lci.bindingCount=3;lci.pBindings=bindings.data();VkDescriptorSetLayout layout{};ok(vkCreateDescriptorSetLayout(device,&lci,nullptr,&layout));
 VkDescriptorPoolSize sizes[3]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4},{VK_DESCRIPTOR_TYPE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
 VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pci.maxSets=1;pci.poolSizeCount=3;pci.pPoolSizes=sizes;VkDescriptorPool pool{};ok(vkCreateDescriptorPool(device,&pci,nullptr,&pool));
 VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};dai.descriptorPool=pool;dai.descriptorSetCount=1;dai.pSetLayouts=&layout;VkDescriptorSet set{};ok(vkAllocateDescriptorSets(device,&dai,&set));
 for(unsigned n=0;n<6;++n){VkDescriptorImageInfo image{n==4?sampler:VK_NULL_HANDLE,n==4?VK_NULL_HANDLE:views[n<4?n:4],VK_IMAGE_LAYOUT_GENERAL};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=n<4?0:n-3;write.dstArrayElement=n<4?n:0;write.descriptorCount=1;write.descriptorType=bindings[write.dstBinding].descriptorType;write.pImageInfo=&image;vkUpdateDescriptorSets(device,1,&write,0,nullptr);}
 const auto original=argent::sfs::compileGlsl(R"(#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set=0,binding=0) uniform texture2D materials[4];
layout(set=0,binding=1) uniform sampler materialSampler;
layout(location=0) out vec4 outputColor;
void main(){
 ivec2 p=ivec2(gl_FragCoord.xy);
 uint i=uint(p.x+p.y*3)%4u;
 vec4 a=texture(nonuniformEXT(sampler2D(materials[nonuniformEXT(i)],materialSampler)),vec2(0.5));
 vec4 b=textureGrad(nonuniformEXT(sampler2D(materials[nonuniformEXT((i+1u)%4u)],materialSampler)),vec2(0.5),vec2(0),vec2(0));
 outputColor=vec4(a.xy,b.xy);
})",spv::ExecutionModelFragment);
 argent::sfs::ShaderOptions options;options.nativeSampleLayer=true;const auto words=argent::sfs::compileStereoShader(original,options);
 VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smci.codeSize=words.size()*4;smci.pCode=words.data();VkShaderModule module{};ok(vkCreateShaderModule(device,&smci,nullptr,&module));VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};plci.setLayoutCount=1;plci.pSetLayouts=&layout;VkPipelineLayout pipelineLayout{};ok(vkCreatePipelineLayout(device,&plci,nullptr,&pipelineLayout));DEVICE(vkCreateGraphicsPipelines);DEVICE(vkCreateRenderPass);DEVICE(vkDestroyRenderPass);DEVICE(vkCreateFramebuffer);DEVICE(vkDestroyFramebuffer);DEVICE(vkCmdBeginRenderPass);DEVICE(vkCmdEndRenderPass);DEVICE(vkCmdDraw);
 const auto vertexWords=argent::sfs::compileGlsl(R"(#version 460
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.0-1.0,0,1);})",spv::ExecutionModelVertex);
 smci.codeSize=vertexWords.size()*4;smci.pCode=vertexWords.data();VkShaderModule vertex{};ok(vkCreateShaderModule(device,&smci,nullptr,&vertex));
 VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;uint32_t viewMask=3;
 VkRenderPassMultiviewCreateInfo mv{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};mv.subpassCount=1;mv.pViewMasks=&viewMask;
 VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.pNext=&mv;rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&sub;VkRenderPass renderPass{};ok(vkCreateRenderPass(device,&rp,nullptr,&renderPass));
 VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=renderPass;fb.attachmentCount=1;fb.pAttachments=&views[4];fb.width=fb.height=8;fb.layers=1;VkFramebuffer framebuffer{};ok(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
 VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=module;
 VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 VkViewport viewport{0,0,8,8,0,1};VkRect2D scissor{{0,0},{7,7}};VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
 VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.lineWidth=1;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&blend;
 VkGraphicsPipelineCreateInfo cpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};cpci.stageCount=2;cpci.pStages=stages;cpci.pVertexInputState=&vi;cpci.pInputAssemblyState=&ia;cpci.pViewportState=&vp;cpci.pRasterizationState=&rs;cpci.pMultisampleState=&ms;cpci.pColorBlendState=&cb;cpci.layout=pipelineLayout;cpci.renderPass=renderPass;
 VkPipeline pipeline{};
 {argent::sfs::MaterialPipelineCapture capture(device,vkGetDeviceProcAddr,true);
  check(capture.active(),"Private material cache unavailable");
  ok(vkCreateGraphicsPipelines(device,capture.cache(VK_NULL_HANDLE),1,&cpci,nullptr,&pipeline));
  check(capture.data().size()>=32,"Material pipeline cache data missing");
 }

 VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpi.queueFamilyIndex=family;VkCommandPool commandPool{};ok(vkCreateCommandPool(device,&cpi,nullptr,&commandPool));VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=commandPool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;VkCommandBuffer command{};ok(vkAllocateCommandBuffers(device,&cai,&command));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(command,&begin));
 auto barrier=[&](VkImage image,VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags src,VkAccessFlags dst){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=oldLayout;b.newLayout=newLayout;b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
 for(unsigned tex=0;tex<4;++tex){
  barrier(images[tex],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  for(unsigned eye=0;eye<2;++eye){VkClearColorValue color{};color.float32[0]=float(tex+1)*0.125f;color.float32[1]=eye?0.75f:0.25f;
   VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1};vkCmdClearColorImage(command,images[tex],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);}
  barrier(images[tex],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 }
 barrier(images[4],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
 VkClearValue clear{};VkRenderPassBeginInfo beginPass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};beginPass.renderPass=renderPass;beginPass.framebuffer=framebuffer;beginPass.renderArea={{0,0},{8,8}};beginPass.clearValueCount=1;beginPass.pClearValues=&clear;vkCmdBeginRenderPass(command,&beginPass,VK_SUBPASS_CONTENTS_INLINE);
 vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,0,nullptr);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
 barrier(images[4],VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};copy.imageExtent={8,8,1};vkCmdCopyImageToBuffer(command,images[4],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);ok(vkEndCommandBuffer(command));
 VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};ok(vkCreateFence(device,&fci,nullptr,&fence));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;ok(vkQueueSubmit(queue,1,&submit,fence));ok(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));void* mapped{};ok(vkMapMemory(device,readback,0,bci.size,0,&mapped));const auto* values=static_cast<const float*>(mapped);
 for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
  unsigned index=(x+y*3)%4;const unsigned next=(index+1)%4;const float expected[]{float(index+1)*0.125f,eye&&index>=2?0.75f:0.25f,float(next+1)*0.125f,eye&&next>=2?0.75f:0.25f};
  for(unsigned c=0;c<4;++c)check(std::abs(values[(eye*64+y*8+x)*4+c]-(x<7&&y<7?expected[c]:0.f))<0.0001f,"Incorrect divergent material texture or stereo eye");
 }vkUnmapMemory(device,readback);
 vkDestroyFramebuffer(device,framebuffer,nullptr);vkDestroyRenderPass(device,renderPass,nullptr);vkDestroyShaderModule(device,vertex,nullptr);vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,commandPool,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,pipelineLayout,nullptr);vkDestroyShaderModule(device,module,nullptr);vkDestroyDescriptorPool(device,pool,nullptr);vkDestroyDescriptorSetLayout(device,layout,nullptr);vkDestroySampler(device,sampler,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,readback,nullptr);for(auto view:views)vkDestroyImageView(device,view,nullptr);for(unsigned n=0;n<5;++n){vkDestroyImage(device,images[n],nullptr);vkFreeMemory(device,imageMemory[n],nullptr);}vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
 std::cout<<"GPU fragment bindless sampling: divergent mono/stereo descriptors, implicit LOD and gradients, scissor edge, both eyes passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
