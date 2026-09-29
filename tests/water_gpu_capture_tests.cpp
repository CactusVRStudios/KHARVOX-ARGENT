#include "../src/sfs/WaterGpuCapture.h"
#include <array>
#include <cmath>
#include <iostream>
#include <cstring>
#include <thread>
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static void ok(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Vulkan "+std::to_string(r));}
static std::atomic<unsigned> errors{};
static VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++errors;std::cerr<<data->pMessage<<'\n';}return VK_FALSE;}
int main(int argc,char** argv){try{
 VkPhysicalDeviceMemoryProperties choices{};choices.memoryTypeCount=4;
 choices.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
 choices.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
 choices.memoryTypes[2].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
 choices.memoryTypes[3].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
 check(argent::sfs::WaterGpuCapture::readbackMemoryType(choices,15)==2,"Readback picked uncached upload/BAR memory");
 check(argent::sfs::WaterGpuCapture::readbackMemoryType(choices,3)==1,"Cached noncoherent fallback ignored");
 check(argent::sfs::WaterGpuCapture::readbackMemoryType(choices,1)==0,"Host-visible fallback failed");
 check(argent::sfs::WaterGpuCapture::readbackMemoryType(choices,8)==UINT32_MAX,"Non-host-visible memory accepted");
 const bool scene=argc==2&&std::strcmp(argv[1],"scene")==0;
 const bool paired=argc==2&&std::strcmp(argv[1],"paired")==0;
 const bool geometry=paired||(argc==2&&std::strcmp(argv[1],"geometry")==0);
 auto loader=LoadLibraryW(L"vulkan-1.dll");check(loader,"Vulkan loader missing");auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
 auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
 auto enumerate=reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(gipa(nullptr,"vkEnumerateInstanceLayerProperties"));uint32_t n{};ok(enumerate(&n,nullptr));std::vector<VkLayerProperties> layers(n);ok(enumerate(&n,layers.data()));bool validation=false;for(auto& l:layers)validation|=std::strcmp(l.layerName,"VK_LAYER_KHRONOS_validation")==0;
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;
 const char* layer="VK_LAYER_KHRONOS_validation";const char* extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
 if(validation){ici.enabledLayerCount=1;ici.ppEnabledLayerNames=&layer;ici.enabledExtensionCount=1;ici.ppEnabledExtensionNames=&extension;}
 VkInstance instance{};ok(create(&ici,nullptr,&instance));
#define I(name) auto name=reinterpret_cast<PFN_##name>(gipa(instance,#name));check(name,#name)
 I(vkEnumeratePhysicalDevices);I(vkGetPhysicalDeviceQueueFamilyProperties);I(vkGetPhysicalDeviceMemoryProperties);I(vkCreateDevice);I(vkGetDeviceProcAddr);I(vkDestroyInstance);
 VkDebugUtilsMessengerEXT messenger{};if(validation){auto c=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(gipa(instance,"vkCreateDebugUtilsMessengerEXT"));VkDebugUtilsMessengerCreateInfoEXT di{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};di.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;di.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;di.pfnUserCallback=debug;ok(c(instance,&di,nullptr,&messenger));}
 ok(vkEnumeratePhysicalDevices(instance,&n,nullptr));check(n,"GPU missing");std::vector<VkPhysicalDevice> physical(n);ok(vkEnumeratePhysicalDevices(instance,&n,physical.data()));
 vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&n,families.data());uint32_t family=UINT32_MAX;for(uint32_t j=0;j<n;++j)if(families[j].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=j;break;}check(family!=UINT32_MAX,"Graphics queue missing");
 float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;VkDevice device{};ok(vkCreateDevice(physical[0],&dci,nullptr,&device));
#define D(name) auto name=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name));check(name,#name)
 D(vkCreateRenderPass);D(vkDestroyRenderPass);D(vkCreateFramebuffer);D(vkDestroyFramebuffer);D(vkCmdBeginRenderPass);D(vkCmdEndRenderPass);D(vkCmdClearAttachments);
 D(vkGetDeviceQueue);D(vkCreateImage);D(vkDestroyImage);D(vkGetImageMemoryRequirements);D(vkAllocateMemory);D(vkFreeMemory);D(vkBindImageMemory);D(vkCreateImageView);D(vkDestroyImageView);D(vkCreateBuffer);D(vkDestroyBuffer);D(vkGetBufferMemoryRequirements);D(vkBindBufferMemory);D(vkMapMemory);D(vkUnmapMemory);D(vkCreateCommandPool);D(vkDestroyCommandPool);D(vkAllocateCommandBuffers);D(vkBeginCommandBuffer);D(vkResetCommandPool);D(vkEndCommandBuffer);D(vkCmdPipelineBarrier);D(vkCmdClearDepthStencilImage);D(vkCmdClearColorImage);D(vkQueueSubmit);D(vkQueueWaitIdle);D(vkDestroyDevice);
 VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical[0],&memory);
 auto allocate=[&](VkMemoryRequirements r,VkMemoryPropertyFlags flags){uint32_t type=UINT32_MAX;for(uint32_t j=0;j<memory.memoryTypeCount;++j)if((r.memoryTypeBits&(1u<<j))&&(memory.memoryTypes[j].propertyFlags&flags)==flags){type=j;break;}check(type!=UINT32_MAX,"Memory type missing");VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=type;VkDeviceMemory m{};ok(vkAllocateMemory(device,&a,nullptr,&m));return m;};
 auto root=std::filesystem::temp_directory_path()/("argent-water-gpu-"+std::to_string(GetCurrentProcessId()));
 const auto target=scene?argent::sfs::WaterGpuCapture::sceneShader:geometry?argent::sfs::WaterGpuCapture::geometryShader:argent::sfs::WaterGpuCapture::shadingShader;
 const auto submitThread=std::this_thread::get_id();std::atomic<bool> backgroundSaved{},foregroundSaved{};
 argent::sfs::WaterGpuCapture capture(device,vkGetDeviceProcAddr,memory,root,[&](const std::string& m){if(m.find("WATER_GPU_CAPTURE saved=")!=std::string::npos){if(std::this_thread::get_id()==submitThread)foregroundSaved=true;else backgroundSaved=true;}std::cout<<m<<'\n';},target,paired||scene);
 std::array<VkImage,4> images{};std::array<VkImageView,4> views{};std::array<VkDeviceMemory,4> memories{};const VkFormat formats[]={VK_FORMAT_D16_UNORM,VK_FORMAT_X8_D24_UNORM_PACK32,VK_FORMAT_D32_SFLOAT_S8_UINT,geometry?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM};
 for(unsigned j=0;j<4;++j){VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};i.imageType=VK_IMAGE_TYPE_2D;i.format=formats[j];i.extent={4,4,1};i.mipLevels=1;i.arrayLayers=2;i.samples=VK_SAMPLE_COUNT_1_BIT;i.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;if(j==3)i.usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ok(vkCreateImage(device,&i,nullptr,&images[j]));capture.image(images[j],i);VkMemoryRequirements r{};vkGetImageMemoryRequirements(device,images[j],&r);memories[j]=allocate(r,0);ok(vkBindImageMemory(device,images[j],memories[j],0));
  VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=images[j];v.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;v.format=formats[j];v.subresourceRange={VkImageAspectFlags(j<3?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,2};ok(vkCreateImageView(device,&v,nullptr,&views[j]));capture.view(views[j],v);
 }
 VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=512;bi.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;VkBuffer buffer{};ok(vkCreateBuffer(device,&bi,nullptr,&buffer));capture.buffer(buffer,bi);VkMemoryRequirements br{};vkGetBufferMemoryRequirements(device,buffer,&br);auto bm=allocate(br,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);ok(vkBindBufferMemory(device,buffer,bm,0));void* mapped{};ok(vkMapMemory(device,bm,0,512,0,&mapped));for(unsigned j=0;j<512;++j)static_cast<unsigned char*>(mapped)[j]=static_cast<unsigned char>(j);vkUnmapMemory(device,bm);
 // Metadata-only handles; the capture never binds these as real Vulkan sets.
 auto layout=reinterpret_cast<VkDescriptorSetLayout>(uintptr_t(100));std::array<VkDescriptorSet,2> sets{reinterpret_cast<VkDescriptorSet>(uintptr_t(101)),reinterpret_cast<VkDescriptorSet>(uintptr_t(102))};
 VkDescriptorSetLayoutBinding lb{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=1;li.pBindings=&lb;capture.layout(layout,li);
 VkDescriptorSetLayout layouts[]={layout,layout};VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sai.descriptorSetCount=2;sai.pSetLayouts=layouts;capture.allocateSets(sai,sets.data());
 for(unsigned j=0;j<4;++j){VkDescriptorImageInfo ii{};ii.imageView=views[j];ii.imageLayout=j<3?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=sets[geometry?(j==2?0:1):(j<3?0:1)];w.dstBinding=geometry?(j<2?j+1:4):(j==0?2:j==1?4:j==2?5:1);w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;w.pImageInfo=&ii;capture.update(1,&w,0,nullptr);
  if(scene&&j==3){w.dstSet=sets[1];w.dstBinding=5;capture.update(1,&w,0,nullptr);w.dstBinding=3;capture.update(1,&w,0,nullptr);}
  if(scene&&j==0){w.dstSet=sets[1];w.dstBinding=4;capture.update(1,&w,0,nullptr);}
  if(geometry&&j==3){w.dstBinding=3;capture.update(1,&w,0,nullptr);} // History aliases output in fixture: before must retain old contents.
  if(j==0){w.dstSet=sets[0];w.dstBinding=27;capture.update(1,&w,0,nullptr);} // Shared scene shadow atlas, distinct eye contents.
 }
 VkDescriptorBufferInfo binfo{buffer,128,64};VkWriteDescriptorSet bw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};bw.dstSet=sets[0];bw.dstBinding=0;bw.descriptorCount=1;bw.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;bw.pBufferInfo=&binfo;capture.update(1,&bw,0,nullptr);
 binfo={buffer,0,256};bw.dstBinding=9;bw.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;capture.update(1,&bw,0,nullptr);
 auto pipeline=reinterpret_cast<VkPipeline>(uintptr_t(103));capture.pipeline(pipeline,target);auto secondPipeline=reinterpret_cast<VkPipeline>(uintptr_t(104));if(paired)capture.pipeline(secondPipeline,argent::sfs::WaterGpuCapture::shadingShader);
 VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.queueFamilyIndex=family;pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool pool{};ok(vkCreateCommandPool(device,&pci,nullptr,&pool));VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;VkCommandBuffer cb{};ok(vkAllocateCommandBuffers(device,&cai,&cb));for(unsigned iteration=0;iteration<3;++iteration){if(iteration){Sleep(260);ok(vkResetCommandPool(device,pool,0));capture.reset(cb);}
 VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(cb,&begin));
 for(unsigned j=0;j<4;++j){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=images[j];b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.subresourceRange={VkImageAspectFlags(j<2?VK_IMAGE_ASPECT_DEPTH_BIT:j==2?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,2};vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
  for(unsigned eye=0;eye<2;++eye){auto range=b.subresourceRange;range.baseArrayLayer=eye;range.layerCount=1;if(j<3){VkClearDepthStencilValue clear{eye?.75f:.25f,7};vkCmdClearDepthStencilImage(cb,images[j],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);}else{VkClearColorValue clear{};vkCmdClearColorImage(cb,images[j],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);}}
  b.oldLayout=b.newLayout;b.newLayout=j<3?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
 }
 std::vector<argent::sfs::WaterGpuCapture::BoundSet> bound{{sets[0],{64}},{sets[1],{0}}};
 if(!iteration){check(!capture.before(cb,pipeline,bound,1,1,2,true),"Unarmed capture recorded");capture.arm();if(scene){check(!capture.before(cb,pipeline,bound,1,1,2,true),"Scene fallback did not wait for water");Sleep(2010);}}check(!capture.before(cb,pipeline,bound,1,1,2,false),"Ineligible command captured");auto ticket=capture.before(cb,pipeline,bound,1,1,2,true);check(bool(ticket),"Capture not recorded");
 for(unsigned eye=0;eye<2;++eye){VkClearColorValue color{};color.float32[eye]=1; color.float32[3]=geometry?-3.4e38f:1;VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1};vkCmdClearColorImage(cb,images[3],VK_IMAGE_LAYOUT_GENERAL,&color,1,&r);}
 capture.after(cb,ticket);
 argent::sfs::WaterGpuCapture::Ticket second;
 if(paired){check(!capture.before(cb,pipeline,bound,1,1,2,true),"Repeated geometry accepted before shading");second=capture.before(cb,secondPipeline,bound,1,1,2,true);check(bool(second),"Paired shading blocked by pending geometry");capture.after(cb,second);}
 ok(vkEndCommandBuffer(cb));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;ok(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));capture.submitted(queue,1,&cb);ok(vkQueueWaitIdle(queue));for(unsigned drain=0;drain<16;++drain){capture.poll(false);capture.waitForWriter();}
 auto read=[&](const char* name){std::ifstream f(ticket->folder/name,std::ios::binary|std::ios::ate);check(bool(f),"Capture file missing");std::vector<unsigned char> b(size_t(f.tellg()));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),b.size());return b;};
 auto constants=read("before-s0-b0-e0.bin");check(constants.size()==64&&constants[0]==192&&constants[63]==255,"Dynamic UBO offset lost");if(!scene){auto d16=read(geometry?"before-s1-b1-e0.bin":"before-s0-b2-e0.bin");check(d16.size()==64,"D16 size");for(unsigned eye=0;eye<2;++eye)for(unsigned px=0;px<16;++px){uint16_t v{};std::memcpy(&v,d16.data()+(eye*16+px)*2,2);check(std::abs(float(v)/65535.f-(eye?.75f:.25f))<.00003f,"D16 eye data");}
 auto list=read("before-s0-b9-e0.bin");check(list.size()==256&&list[0]==0&&list[255]==255,"GPU list contents");
 if(!geometry)check(read("before-s0-b27-e0.bin")==d16,"Shadow atlas eye contents or layer order changed");
 auto d24=read(geometry?"before-s1-b2-e0.bin":"before-s0-b4-e0.bin");check(d24.size()==128,"D24 size");for(unsigned eye=0;eye<2;++eye)for(unsigned px=0;px<16;++px){uint32_t v{};std::memcpy(&v,d24.data()+(eye*16+px)*4,4);check(std::abs(float(v&0xFFFFFF)/16777215.f-(eye?.75f:.25f))<.000001f,"D24 eye data");}
 auto d32=read(geometry?"before-s0-b4-e0.bin":"before-s0-b5-e0.bin");check(d32.size()==128,"D32S8 depth aspect size");for(unsigned eye=0;eye<2;++eye)for(unsigned px=0;px<16;++px){float v{};std::memcpy(&v,d32.data()+(eye*16+px)*4,4);check(v==(eye?.75f:.25f),"D32S8 eye data");}
 }else{auto prior=read("before-s1-b3-e0.bin");check(prior.size()==128&&std::all_of(prior.begin(),prior.end(),[](auto b){return b==0;}),"Scene input captured after overwrite");check(ticket->shader==argent::sfs::WaterGpuCapture::sceneShader,"Scene fallback shader mismatch");}
 auto color=read(scene?"after-s1-b5-e0.bin":geometry?"after-s1-b4-e0.bin":"after-s1-b1-e0.bin");check(color.size()==(geometry?512:128),"RGBA size");for(unsigned eye=0;eye<2;++eye)for(unsigned px=0;px<16;++px){auto i=(eye*16+px)*4;if(geometry){float v[4];std::memcpy(v,color.data()+i*4,16);check(v[eye]==1&&v[1-eye]==0&&v[3]==-3.4e38f,"Position/validity marker lost");}else check(color[i+eye]==255&&color[i+1-eye]==0&&color[i+3]==255,"After-pass eye output");}
 if(geometry){auto history=read("before-s1-b3-e0.bin");check(history.size()==512&&std::all_of(history.begin(),history.end(),[](auto b){return b==0;}),"History was captured after its overwrite");}
 check(std::filesystem::exists(ticket->folder/"capture.txt"),"Completion missing");
 if(paired){check(std::filesystem::exists(second->folder/"capture.txt"),"Paired shading completion missing");check(ticket->shader==argent::sfs::WaterGpuCapture::geometryShader&&second->shader==argent::sfs::WaterGpuCapture::shadingShader,"Capture shader identities mixed");second.reset();}capture.reset(cb);ticket.reset();}
 if(paired){
  auto scenePipeline=reinterpret_cast<VkPipeline>(uintptr_t(105));capture.pipeline(scenePipeline,argent::sfs::WaterGpuCapture::sceneShader);
  VkDescriptorImageInfo info{};info.imageView=views[3];info.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=sets[1];write.dstBinding=5;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;write.pImageInfo=&info;capture.update(1,&write,0,nullptr);
  for(unsigned n=0;n<3;++n){
   const bool sky=n==0;
   const bool composite=n==1;
   capture.pipeline(scenePipeline,sky?argent::sfs::WaterGpuCapture::amdAtmosphereShader:composite?argent::sfs::WaterGpuCapture::compositeShader:argent::sfs::WaterGpuCapture::sceneShader);
   if(sky||composite){write.dstBinding=composite?8:1;capture.update(1,&write,0,nullptr);}
   Sleep(260);ok(vkResetCommandPool(device,pool,0));capture.reset(cb);
   VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(cb,&begin));
   std::vector<argent::sfs::WaterGpuCapture::BoundSet> bound{{sets[0],{64}},{sets[1],{0}}};
   check(!capture.before(cb,pipeline,bound,1,1,2,true),"Water accepted during scene tail");
   auto shot=capture.before(cb,scenePipeline,bound,1,1,2,true,n!=0);check(bool(shot),"Scene tail missing after water");capture.after(cb,shot);
   ok(vkEndCommandBuffer(cb));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;ok(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));capture.submitted(queue,1,&cb);ok(vkQueueWaitIdle(queue));for(unsigned drain=0;drain<16;++drain){capture.poll(false);capture.waitForWriter();}
   std::ifstream file(shot->folder/(sky?"after-s1-b1-e0.bin":composite?"after-s1-b8-e0.bin":"after-s1-b5-e0.bin"),std::ios::binary);float pixels[128]{};file.read(reinterpret_cast<char*>(pixels),sizeof(pixels));
   check(file.gcount()==sizeof(pixels)&&pixels[0]==1&&pixels[1]==0&&pixels[64]==0&&pixels[65]==1,"Scene tail lost eye layers");
   if(sky){std::ifstream before(shot->folder/"before-s1-b1-e0.bin",std::ios::binary);float prior[128]{};before.read(reinterpret_cast<char*>(prior),sizeof(prior));check(before.gcount()==sizeof(prior)&&std::memcmp(prior,pixels,sizeof(prior))==0,"Read/write atmosphere input missing");}
   if(n==0){check(!std::filesystem::exists(shot->folder/"before-s1-b2-e0.bin"),"Compute-only capture copied depth");std::ifstream summary(shot->folder/"capture.txt");std::string text((std::istreambuf_iterator<char>(summary)),{});check(text.find("depth copy omitted on compute queue")!=std::string::npos,"Compute depth omission not reported");}
   check(std::filesystem::exists(shot->folder/"capture.txt"),"Scene tail completion missing");capture.reset(cb);
  }
 }
 if(!geometry&&!scene){
  // Real GPU render-pass copies, with synthetic draw metadata (no game shaders).
  VkAttachmentDescription a{};a.format=formats[3];a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=a.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
  VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
  VkRenderPassCreateInfo pi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pi.attachmentCount=1;pi.pAttachments=&a;pi.subpassCount=1;pi.pSubpasses=&sub;VkRenderPass pass{};ok(vkCreateRenderPass(device,&pi,nullptr,&pass));capture.renderPass(pass,pi);
  VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=pass;fi.attachmentCount=1;fi.pAttachments=&views[3];fi.width=fi.height=4;fi.layers=2;VkFramebuffer fb{};ok(vkCreateFramebuffer(device,&fi,nullptr,&fb));capture.framebuffer(fb,fi);
  VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gp.renderPass=pass;
  auto sky=reinterpret_cast<VkPipeline>(uintptr_t(999));capture.graphicsPipeline(sky,gp,{{VK_SHADER_STAGE_FRAGMENT_BIT,0x2f28ea0af971c44bull}});check(capture.skyPipeline(sky),"AMD sky recognition missing");
  VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rp.renderPass=pass;rp.framebuffer=fb;rp.renderArea.extent={4,4};
  check(!capture.graphicsBegin(cb,rp,false,true),"Ineligible sky pass copied");capture.graphicsDraw(cb,sky,{},"learning",true);capture.graphicsEnd(cb);
  ok(vkResetCommandPool(device,pool,0));capture.reset(cb);VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(vkBeginCommandBuffer(cb,&begin));
  auto shot=capture.graphicsBegin(cb,rp,true,false);check(bool(shot),"Learned mono/shared sky framebuffer not captured");
  vkCmdBeginRenderPass(cb,&rp,VK_SUBPASS_CONTENTS_INLINE);
  auto ordinary=reinterpret_cast<VkPipeline>(uintptr_t(998));capture.graphicsPipeline(ordinary,gp,{{VK_SHADER_STAGE_FRAGMENT_BIT,123}});
  for(unsigned n=0;n<13000;++n)capture.graphicsDraw(cb,ordinary,{},"repeated world draw",false);
  capture.graphicsDraw(cb,sky,{{sets[0],{64}},{sets[1],{0}}},"synthetic indexed draw",false);
  for(unsigned eye=0;eye<2;++eye){VkClearAttachment c{};c.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;c.clearValue.color.float32[2]=eye?0.f:1.f;c.clearValue.color.float32[0]=eye?1.f:0.f;c.clearValue.color.float32[1]=eye?1.f:0.f;c.clearValue.color.float32[3]=1;VkClearRect r{{{0,0},{4,4}},eye,1};vkCmdClearAttachments(cb,1,&c,1,&r);}
  vkCmdEndRenderPass(cb);capture.graphicsEnd(cb);ok(vkEndCommandBuffer(cb));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;ok(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));capture.submitted(queue,1,&cb);ok(vkQueueWaitIdle(queue));for(unsigned drain=0;drain<16;++drain){capture.poll(false);capture.waitForWriter();}
  auto read=[&](const char* file){std::ifstream f(shot->folder/file,std::ios::binary);return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),{});};
  auto before=read("before-s100-b0-e0.bin"),after=read("after-s100-b0-e0.bin");check(before.size()==128&&after.size()==128,"Sky eye images missing");
  check(before[0]==255&&before[65]==255,"Sky pre-pass red/green layers lost");check(after[2]==255&&after[64]==255&&after[65]==255,"Sky post-pass blue/yellow layers lost");
  std::ifstream details(shot->folder/"graphics.txt");std::string text((std::istreambuf_iterator<char>(details)),{});check(text.find("actualSkyDraws=1")!=std::string::npos,"Sky executed draw identity missing");
  std::ifstream journal(shot->folder.parent_path()/"graphics-draws.log");std::string recorded((std::istreambuf_iterator<char>(journal)),{});check(recorded.find("synthetic indexed draw")!=std::string::npos,"Repeated geometry displaced rare sky draw");check(recorded.size()<20000,"Repeated geometry was not bounded");
  auto inputs=shot->folder.parent_path()/(shot->folder.filename().string()+"-inputs-1");std::ifstream constants(inputs/"after-s0-b0-e0.bin",std::ios::binary);char byte{};constants.get(byte);check(static_cast<unsigned char>(byte)==192,"Sky dynamic buffer offset lost");
  capture.reset(cb);capture.forgetFramebuffer(fb);capture.forgetPass(pass);capture.forgetPipeline(sky);vkDestroyFramebuffer(device,fb,nullptr);vkDestroyRenderPass(device,pass,nullptr);
 }
 check(!capture.armed(),"Three-snapshot capture did not stop");capture.shutdown();check(backgroundSaved&&!foregroundSaved,"Disk output ran on submitting thread");
 for(unsigned j=0;j<4;++j){vkDestroyImageView(device,views[j],nullptr);vkDestroyImage(device,images[j],nullptr);vkFreeMemory(device,memories[j],nullptr);}vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,bm,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);
 if(messenger)reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(gipa(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);check(errors==0,"Vulkan validation errors");
 std::cout<<"PASS GPU water capture: D16/D32S8 both eyes, dynamic constants, before/after output, validation="<<validation<<"\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
