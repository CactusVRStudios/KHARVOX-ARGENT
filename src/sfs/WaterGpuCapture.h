#pragma once
#include <windows.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <tuple>
#include <future>
#include <chrono>
#include <cstring>

namespace argent::sfs {
// Optional, exact-dispatch capture. Copies are recorded around the water pass
// in the application's command buffer. Completion uses a subsequent empty
// queue submission, never a CPU wait on the game's render thread.
class WaterGpuCapture {
 template<class T>T proc(const char* name)const{return reinterpret_cast<T>(resolve(device,name));}
 static void ok(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("water capture Vulkan result="+std::to_string(r));}
 struct Allocation {
  VkDevice device{};PFN_vkGetDeviceProcAddr resolve{};VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize size{};VkMemoryPropertyFlags properties{};
  std::shared_ptr<std::atomic<uint64_t>> resident;
  ~Allocation(){if(mapped)reinterpret_cast<PFN_vkUnmapMemory>(resolve(device,"vkUnmapMemory"))(device,memory);if(buffer)reinterpret_cast<PFN_vkDestroyBuffer>(resolve(device,"vkDestroyBuffer"))(device,buffer,nullptr);if(memory)reinterpret_cast<PFN_vkFreeMemory>(resolve(device,"vkFreeMemory"))(device,memory,nullptr);if(resident)resident->fetch_sub(size);}
 };
 struct Fence {
  VkDevice device{};PFN_vkDestroyFence destroy{};VkFence value{};
  ~Fence(){if(value)destroy(device,value,nullptr);}
 };
 struct Binding {VkDescriptorType type{};VkDescriptorImageInfo image{};VkDescriptorBufferInfo buffer{};uint32_t dynamicIndex=UINT32_MAX;};
 struct Set {VkDescriptorPool pool{};std::map<std::pair<uint32_t,uint32_t>,Binding> bindings;std::map<std::pair<uint32_t,uint32_t>,uint32_t> dynamic;};
 struct Item {
  uint32_t set{},binding{},element{};bool after{};VkDeviceSize offset{},size{};
  VkImage image{};VkImageView view{};VkBuffer buffer{};VkDeviceSize sourceOffset{};
  VkImageLayout layout{};VkFormat format{};VkExtent3D extent{};VkImageSubresourceLayers layers{};VkImageAspectFlags barrierAspect{};
 };
public:
 // Readback favors CPU-cached host memory, not upload-oriented/BAR memory.
 static uint32_t readbackMemoryType(const VkPhysicalDeviceMemoryProperties& props,uint32_t mask){
  uint32_t selected=UINT32_MAX;int best=-1;
  for(uint32_t i=0;i<props.memoryTypeCount;++i){const auto flags=props.memoryTypes[i].propertyFlags;if(!(mask&(1u<<i))||!(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))continue;
   const int score=((flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?8:0)+((flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?2:0)+((flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)?0:1);
   if(score>best){best=score;selected=i;}
  }return selected;
 }
 static constexpr uint64_t sceneShader=0xdbd3b48a7649984cull; // captured scene postprocess, both eyes
 static constexpr uint64_t atmosphereShader=0xa14db8d1c3a2ca43ull,amdAtmosphereShader=0x971c804446f72158ull;
 static constexpr uint64_t compositeShader=0x7349c1f96fbc381aull;
 static bool atmosphere(uint64_t hash){return hash==atmosphereShader||hash==amdAtmosphereShader||hash==0x51ac0baab3cafdbcull||hash==0x65caa09a41470219ull;}
 static constexpr uint64_t shadingShader=0x24abb0e76a065289ull,geometryShader=0x57446083630d237cull;
 struct Batch {
  std::shared_ptr<Allocation> allocation;std::vector<Item> items;std::vector<std::string> skipped;
  std::vector<std::shared_ptr<Fence>> fences;
  std::filesystem::path folder;std::vector<unsigned char> cpuPixels;std::vector<unsigned char> projection;std::string graphicsDetails;uint32_t x{},y{},z{};uint64_t pipeline{},tick{},generation{},frameSerial{},shader{};bool queued{},saved{},failed{},recordedAfter{},graphics{};
 };
 using Ticket=std::shared_ptr<Batch>;
 struct BoundSet {VkDescriptorSet set{};std::vector<uint32_t> dynamic;};
private:
 VkDevice device{};PFN_vkGetDeviceProcAddr resolve{};VkPhysicalDeviceMemoryProperties memory{};
 std::function<void(const std::string&)> log;
 std::filesystem::path root;std::mutex mutex;
 uint64_t captureShader{shadingShader};bool pairedCapture{},sceneFallback{};
 std::map<VkImage,VkImageCreateInfo> images;std::map<VkImageView,VkImageViewCreateInfo> views;
 std::map<VkBuffer,VkBufferCreateInfo> buffers;
 std::map<VkDescriptorSetLayout,std::map<std::pair<uint32_t,uint32_t>,uint32_t>> layouts;
 std::map<VkDescriptorSet,Set> sets;std::map<VkPipeline,uint64_t> waterPipelines;
 std::map<VkCommandBuffer,std::vector<Ticket>> commands;std::vector<Ticket> pending;
 std::future<void> writer;bool completionReported=true;
 std::shared_ptr<std::atomic<uint64_t>> resident=std::make_shared<std::atomic<uint64_t>>(0);
 std::atomic<unsigned> remaining{};unsigned ordinal{};uint64_t session{},nextTick{};bool keyDown{};uint64_t keyPoll{};
 std::set<std::tuple<uint64_t,bool,bool,unsigned>> observedDispatches;
 static constexpr uint64_t batchLimit=256ull*1024*1024,residentLimit=768ull*1024*1024;
 static uint32_t texelBytes(VkFormat f,VkImageAspectFlags aspect){
  if(aspect==VK_IMAGE_ASPECT_DEPTH_BIT){switch(f){case VK_FORMAT_D16_UNORM:case VK_FORMAT_D16_UNORM_S8_UINT:return 2;case VK_FORMAT_X8_D24_UNORM_PACK32:case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT:case VK_FORMAT_D32_SFLOAT_S8_UINT:return 4;default:return 0;}}
  if(aspect!=VK_IMAGE_ASPECT_COLOR_BIT)return 0;
  switch(f){case VK_FORMAT_R8_UNORM:return 1;case VK_FORMAT_R8G8_UNORM:case VK_FORMAT_R16_SFLOAT:return 2;case VK_FORMAT_R32_SFLOAT:case VK_FORMAT_R32_UINT:case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B10G11R11_UFLOAT_PACK32:case VK_FORMAT_R16G16_SFLOAT:return 4;case VK_FORMAT_R16G16B16A16_SFLOAT:return 8;case VK_FORMAT_R32G32B32A32_SFLOAT:return 16;default:return 0;}
 }
 static bool combinedDepth(VkFormat f){return f==VK_FORMAT_D16_UNORM_S8_UINT||f==VK_FORMAT_D24_UNORM_S8_UINT||f==VK_FORMAT_D32_SFLOAT_S8_UINT;}
 std::shared_ptr<Allocation> allocate(VkDeviceSize bytes){
  if(bytes>batchLimit||resident->load()+bytes>residentLimit)throw std::runtime_error("water capture staging budget exceeded");
  auto a=std::make_shared<Allocation>();a->device=device;a->resolve=resolve;a->size=bytes;a->resident=resident;resident->fetch_add(bytes);
  VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;ok(proc<PFN_vkCreateBuffer>("vkCreateBuffer")(device,&b,nullptr,&a->buffer));
  VkMemoryRequirements req{};proc<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device,a->buffer,&req);
  const auto type=readbackMemoryType(memory,req.memoryTypeBits);
  if(type==UINT32_MAX)throw std::runtime_error("capture needs host-visible memory");
  a->properties=memory.memoryTypes[type].propertyFlags;
  VkMemoryAllocateInfo m{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};m.allocationSize=req.size;m.memoryTypeIndex=type;ok(proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device,&m,nullptr,&a->memory));
  ok(proc<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device,a->buffer,a->memory,0));ok(proc<PFN_vkMapMemory>("vkMapMemory")(device,a->memory,0,VK_WHOLE_SIZE,0,&a->mapped));return a;
 }
 void copies(VkCommandBuffer cb,const Ticket& t,bool after){
  auto barrier=proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
  VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,nullptr,0,nullptr);
  for(const auto& item:t->items){if(item.after!=after)continue;
   if(item.buffer){VkBufferCopy region{item.sourceOffset,item.offset,item.size};proc<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer")(cb,item.buffer,t->allocation->buffer,1,&region);continue;}
   VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.oldLayout=item.layout;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=item.image;b.subresourceRange={item.barrierAspect,item.layers.mipLevel,1,item.layers.baseArrayLayer,item.layers.layerCount};
   barrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
   VkBufferImageCopy region{};region.bufferOffset=item.offset;region.imageSubresource=item.layers;region.imageExtent=item.extent;
   proc<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(cb,item.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,t->allocation->buffer,1,&region);
   std::swap(b.oldLayout,b.newLayout);b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
   barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
  }
  VkMemoryBarrier done{VK_STRUCTURE_TYPE_MEMORY_BARRIER};done.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;done.dstAccessMask=VK_ACCESS_HOST_READ_BIT|VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&done,0,nullptr,0,nullptr);
 }
 void save(const Ticket& t){
  std::filesystem::create_directories(t->folder);std::ofstream meta(t->folder/"resources.tsv");
  meta<<"file\tphase\tset\tbinding\telement\tsource\tview\tsourceOffset\tbytes\tformat\twidth\theight\tlayers\tbaseLayer\tmip\taspect\tlayout\tdepth\n";
  for(const auto& i:t->items){auto file=std::string(i.after?"after":"before")+"-s"+std::to_string(i.set)+"-b"+std::to_string(i.binding)+"-e"+std::to_string(i.element)+".bin";
   std::ofstream out(t->folder/file,std::ios::binary);out.write(reinterpret_cast<const char*>(t->cpuPixels.data())+i.offset,std::streamsize(i.size));out.close();if(!out)throw std::runtime_error("water capture file write failed");
   meta<<file<<'\t'<<(i.after?"after":"before")<<'\t'<<i.set<<'\t'<<i.binding<<'\t'<<i.element<<'\t'<<(i.buffer?reinterpret_cast<uintptr_t>(i.buffer):reinterpret_cast<uintptr_t>(i.image))<<'\t'<<reinterpret_cast<uintptr_t>(i.view)<<'\t'<<i.sourceOffset<<'\t'<<i.size<<'\t'<<i.format<<'\t'<<i.extent.width<<'\t'<<i.extent.height<<'\t'<<i.layers.layerCount<<'\t'<<i.layers.baseArrayLayer<<'\t'<<i.layers.mipLevel<<'\t'<<i.layers.aspectMask<<'\t'<<i.layout<<'\t'<<i.extent.depth<<'\n';
  }
  if(!t->graphicsDetails.empty())std::ofstream(t->folder/"graphics.txt")<<t->graphicsDetails;
  if(!t->projection.empty()){std::ofstream p(t->folder/"eye-projection.bin",std::ios::binary);p.write(reinterpret_cast<const char*>(t->projection.data()),t->projection.size());p.close();if(!p)throw std::runtime_error("water projection write failed");}
  meta.close();if(!meta)throw std::runtime_error("water capture manifest write failed");
  std::ofstream summary(t->folder/"capture.txt");summary<<"shader="<<std::hex<<t->shader<<std::dec<<"\npipeline="<<t->pipeline<<"\nframeSerial="<<t->frameSerial<<"\nrecordTick="<<t->tick<<"\ndispatch="<<t->x<<','<<t->y<<','<<t->z<<"\n";for(const auto& reason:t->skipped)summary<<"skipped="<<reason<<'\n';summary<<"GPU_COMPLETED=1\n";summary.close();if(!summary)throw std::runtime_error("water capture completion write failed");
  log("WATER_GPU_CAPTURE saved="+t->folder.string()+" bytes="+std::to_string(t->cpuPixels.size()));
 }
public:
 WaterGpuCapture(VkDevice d,PFN_vkGetDeviceProcAddr r,const VkPhysicalDeviceMemoryProperties& m,std::filesystem::path p,std::function<void(const std::string&)> l,uint64_t shader=shadingShader,bool paired=false):device(d),resolve(r),memory(m),log(std::move(l)),root(std::move(p)),captureShader(shader),pairedCapture(paired){}
 static std::filesystem::path requestedRoot(){wchar_t flag[8]{},path[32768]{};if(GetEnvironmentVariableW(L"ARGENT_WATER_GPU_CAPTURE",flag,8)!=1||flag[0]!=L'1')return {};auto n=GetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",path,32768);return n&&n<32768?std::filesystem::path(path)/"water-gpu":std::filesystem::path{};}
 ~WaterGpuCapture(){if(writer.valid())writer.wait();}
 // Only for tests/device retirement; never called by the live submit path.
 void waitForWriter(){if(writer.valid())writer.wait();}
 bool armed()const{return remaining.load()!=0;}
 void image(VkImage h,const VkImageCreateInfo& i){std::lock_guard<std::mutex> l(mutex);auto copy=i;copy.pNext=nullptr;copy.pQueueFamilyIndices=nullptr;images[h]=copy;}
 void forgetImage(VkImage h){std::lock_guard<std::mutex> l(mutex);images.erase(h);}
 void view(VkImageView h,const VkImageViewCreateInfo& i){std::lock_guard<std::mutex> l(mutex);auto copy=i;copy.pNext=nullptr;views[h]=copy;}
 void forgetView(VkImageView h){std::lock_guard<std::mutex> l(mutex);views.erase(h);}
 void buffer(VkBuffer h,const VkBufferCreateInfo& i){std::lock_guard<std::mutex> l(mutex);auto copy=i;copy.pNext=nullptr;copy.pQueueFamilyIndices=nullptr;buffers[h]=copy;}
 void forgetBuffer(VkBuffer h){std::lock_guard<std::mutex> l(mutex);buffers.erase(h);}
 void layout(VkDescriptorSetLayout h,const VkDescriptorSetLayoutCreateInfo& i){std::lock_guard<std::mutex> l(mutex);auto& d=layouts[h];d.clear();for(uint32_t n=0;n<i.bindingCount;++n){auto& b=i.pBindings[n];if(b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||b.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)for(uint32_t j=0;j<b.descriptorCount;++j)d[{b.binding,j}]=0;}uint32_t n=0;for(auto& v:d)v.second=n++;}
 void forgetLayout(VkDescriptorSetLayout h){std::lock_guard<std::mutex> l(mutex);layouts.erase(h);}
 void allocateSets(const VkDescriptorSetAllocateInfo& i,const VkDescriptorSet* out){std::lock_guard<std::mutex> l(mutex);for(uint32_t n=0;n<i.descriptorSetCount;++n)sets[out[n]]={i.descriptorPool,{},layouts[i.pSetLayouts[n]]};}
 void freeSets(uint32_t n,const VkDescriptorSet* out){std::lock_guard<std::mutex> l(mutex);for(uint32_t j=0;j<n;++j)sets.erase(out[j]);}
 void pool(VkDescriptorPool p){std::lock_guard<std::mutex> l(mutex);for(auto it=sets.begin();it!=sets.end();)if(it->second.pool==p)it=sets.erase(it);else ++it;}
 void update(uint32_t count,const VkWriteDescriptorSet* writes,uint32_t copies,const VkCopyDescriptorSet* copy){
  std::lock_guard<std::mutex> l(mutex);
  for(uint32_t n=0;n<count;++n){auto& w=writes[n];if(w.dstBinding>=64||w.descriptorCount>16)continue;auto found=sets.find(w.dstSet);if(found==sets.end())continue;
   for(uint32_t j=0;j<w.descriptorCount;++j){Binding b{};b.type=w.descriptorType;auto key=std::make_pair(w.dstBinding,w.dstArrayElement+j);auto dyn=found->second.dynamic.find(key);if(dyn!=found->second.dynamic.end())b.dynamicIndex=dyn->second;
    switch(b.type){case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:if(w.pBufferInfo)b.buffer=w.pBufferInfo[j];break;case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:if(w.pImageInfo)b.image=w.pImageInfo[j];break;default:break;}
    found->second.bindings[key]=b;
   }
  }
  for(uint32_t n=0;n<copies;++n){auto& c=copy[n];if(c.descriptorCount>16)continue;auto src=sets.find(c.srcSet),dst=sets.find(c.dstSet);if(src==sets.end()||dst==sets.end())continue;for(uint32_t j=0;j<c.descriptorCount;++j){auto it=src->second.bindings.find({c.srcBinding,c.srcArrayElement+j});if(it==src->second.bindings.end())continue;auto b=it->second;auto key=std::make_pair(c.dstBinding,c.dstArrayElement+j);auto dyn=dst->second.dynamic.find(key);b.dynamicIndex=dyn==dst->second.dynamic.end()?UINT32_MAX:dyn->second;dst->second.bindings[key]=b;}}
 }
 void pipeline(VkPipeline p,uint64_t hash){std::lock_guard<std::mutex> l(mutex);waterPipelines[p]=hash; /* Census all shaders; snapshot selection below remains exact. */}
 void forgetPipeline(VkPipeline p){std::lock_guard<std::mutex> l(mutex);waterPipelines.erase(p);graphicsPipelines.erase(p);}
 void reset(VkCommandBuffer cb){std::lock_guard<std::mutex> l(mutex);auto it=commands.find(cb);if(it!=commands.end())for(const auto& t:it->second)if(!t->graphics&&t->generation==session&&!t->queued&&!t->saved&&!t->failed){if(pairedCapture){remaining=0;log("WATER_GPU_CAPTURE cancelled: paired command recording discarded");}else remaining.fetch_add(1);}commands.erase(cb);graphicsPasses.erase(cb);}
 bool arm(){std::lock_guard<std::mutex> l(mutex);if(remaining.load()||graphicsActive.load()||!pending.empty()||writer.valid()){log("WATER_GPU_CAPTURE already active");return false;}completionReported=false;session=GetTickCount64();graphicsDeadline=session+5000;graphicsShots=graphicsDraws=0;drawJournal.clear();drawOccurrences.clear();passDecisions.clear();graphicsActive=true;observedDispatches.clear();ordinal=0;nextTick=0;sceneFallback=false;remaining=pairedCapture?9:3;std::filesystem::create_directories(root/std::to_string(session));std::ofstream(root/std::to_string(session)/"armed.txt")<<"F8; snapshots="<<remaining.load()<<"\n";log("WATER_GPU_CAPTURE armed snapshots="+std::to_string(remaining.load())+" hotkey=F8");return true;}
 // Called under the application's queue lock, never during command recording.
 void poll(bool hotkey=true){
  if(hotkey&&GetTickCount64()>=keyPoll){keyPoll=GetTickCount64()+16;DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);bool down=pid==GetCurrentProcessId()&&(GetAsyncKeyState(VK_F8)&0x8000);if(down&&!keyDown&&arm())MessageBeep(MB_OK);keyDown=down;}
  std::lock_guard<std::mutex> l(mutex);
  if(writer.valid()&&writer.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){try{writer.get();}catch(const std::exception& e){remaining=0;log(std::string("WATER_GPU_CAPTURE writer failed: ")+e.what());completionReported=true;}}
  if(graphicsActive&&GetTickCount64()>=graphicsDeadline)graphicsActive=false;
  if(!drawJournal.empty()){std::ofstream(root/std::to_string(session)/"graphics-draws.log",std::ios::app)<<drawJournal;drawJournal.clear();}
  for(auto it=pending.begin();it!=pending.end();){auto& t=*it;bool ready=!t->fences.empty();for(const auto& fence:t->fences){auto r=proc<PFN_vkGetFenceStatus>("vkGetFenceStatus")(device,fence->value);if(r!=VK_SUCCESS){ready=false;if(r!=VK_NOT_READY&&!t->failed){t->failed=true;remaining=0;log("WATER_GPU_CAPTURE incomplete GPU result="+std::to_string(r));}break;}}
   if(ready&&!t->saved&&!t->failed&&!writer.valid()){try{
    // Snapshot CPU-owned bytes while the queue lock prevents a CB replay.
    // The writer never touches mapped Vulkan storage or resource handles.
    const auto copyStart=GetTickCount64();
    if(!(t->allocation->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=t->allocation->memory;range.offset=0;range.size=VK_WHOLE_SIZE;ok(proc<PFN_vkInvalidateMappedMemoryRanges>("vkInvalidateMappedMemoryRanges")(device,1,&range));}
    auto snapshot=std::make_shared<Batch>(*t);
    snapshot->cpuPixels.resize(size_t(t->allocation->size));std::memcpy(snapshot->cpuPixels.data(),t->allocation->mapped,size_t(t->allocation->size));
    snapshot->fences.clear();snapshot->allocation.reset();
    snapshot->graphicsDetails+="readbackMemoryFlags="+std::to_string(t->allocation->properties)+"\nreadbackCpuCopyMs="+std::to_string(GetTickCount64()-copyStart)+"\nfileWriter=background\n";
    writer=std::async(std::launch::async,[this,snapshot]{save(snapshot);});t->saved=true;t->fences.clear();
   }catch(const std::exception& e){t->failed=true;remaining=0;log(e.what());}}
   if(t->saved)it=pending.erase(it);else ++it;
  }
  if(!completionReported&&!remaining.load()&&pending.empty()&&!writer.valid()){completionReported=true;log("WATER_GPU_CAPTURE complete");if(hotkey)MessageBeep(MB_ICONASTERISK);}
 }
 Ticket before(VkCommandBuffer cb,VkPipeline pipeline,const std::vector<BoundSet>& bound,uint32_t x,uint32_t y,uint32_t z,bool eligible,bool graphicsQueue=true){
  if(!remaining.load())return {};
  std::lock_guard<std::mutex> l(mutex);auto now=GetTickCount64();auto selected=waterPipelines.find(pipeline);
  if(selected!=waterPipelines.end()&&observedDispatches.emplace(selected->second,eligible,graphicsQueue,ordinal>=6?1u:0u).second){
   std::ofstream seen(root/std::to_string(session)/"dispatch-seen.tsv",std::ios::app);
   seen<<std::hex<<selected->second<<std::dec<<'\t'<<eligible<<'\t'<<graphicsQueue<<'\t'<<ordinal<<'\t'<<x<<','<<y<<','<<z<<'\n';
  }
  if(!eligible)return {};
  // Waterfall particles can render without the water geometry dispatch. Do
  // not strand F8 forever: capture scene input/output on its exact postpass.
  if(pairedCapture&&!sceneFallback&&ordinal==0&&now-session>=2000&&selected!=waterPipelines.end()&&selected->second==sceneShader){
   sceneFallback=true;remaining=3;log("WATER_GPU_CAPTURE fallback=scene-postprocess reason=no-water-dispatch");
  }
  // Always finish a water capture with scene input/output for both eyes.
  // Water dispatches can exist even when the reported defect is in the sky.
  const bool sceneTail=pairedCapture&&ordinal>=6;
  const bool paired=pairedCapture&&!sceneFallback&&!sceneTail;
  const bool second=paired&&(ordinal%2==1);
  uint64_t expected=paired?(second?shadingShader:geometryShader):captureShader;
  if(sceneFallback||sceneTail){
   const unsigned stage=sceneTail?ordinal-6:ordinal;
   auto available=[&](uint64_t hash){return std::any_of(waterPipelines.begin(),waterPipelines.end(),[&](const auto& p){return p.second==hash;});};
   // Distinct pipeline boundaries: do not spend every tail slot on the
   // early atmosphere pass, whose output precedes the visible clouds.
   expected=sceneShader;
   if(stage==0){auto at=std::find_if(waterPipelines.begin(),waterPipelines.end(),[](const auto& p){return atmosphere(p.second);});if(at!=waterPipelines.end())expected=selected!=waterPipelines.end()&&atmosphere(selected->second)?selected->second:at->second;}
   else if(stage==1&&available(compositeShader))expected=compositeShader;
   // A registered permutation need not execute in this level.
   if(expected!=sceneShader&&now>=std::max(session,nextTick)+2000&&selected!=waterPipelines.end()&&selected->second==sceneShader)expected=sceneShader;
  }
  if(!remaining.load()||now<nextTick||(!second&&!pending.empty())||selected==waterPipelines.end()||selected->second!=expected)return {};
  // Only one recording may reserve a snapshot before it reaches submission.
  for(const auto& command:commands)for(const auto& t:command.second)if(!t->queued&&!t->saved&&!t->failed&&!(second&&command.first==cb&&t->shader==geometryShader))return {};
  auto t=std::make_shared<Batch>();t->shader=expected;t->x=x;t->y=y;t->z=z;t->tick=now;t->generation=session;t->pipeline=reinterpret_cast<uintptr_t>(pipeline);t->folder=root/std::to_string(session)/("snapshot-"+std::to_string(++ordinal));VkDeviceSize total=0;
  auto skip=[&](uint32_t set,uint32_t binding,const char* reason){t->skipped.push_back("s"+std::to_string(set)+" b"+std::to_string(binding)+" "+reason);};
  for(uint32_t sn=0;sn<std::min<size_t>(2,bound.size());++sn){auto set=sets.find(bound[sn].set);if(set==sets.end())continue;
   std::vector<decltype(set->second.bindings.cbegin())> entries;
   for(auto entry=set->second.bindings.cbegin();entry!=set->second.bindings.cend();++entry)entries.push_back(entry);
   if(t->shader==compositeShader&&sn==1)std::stable_sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){auto priority=[](unsigned binding){return binding==8?0:binding==6?1:binding==7?2:binding+3;};return priority(a->first.first)<priority(b->first.first);});
   for(const auto& entry:entries){const auto& pair=*entry;const auto& b=pair.second;Item i{};i.set=sn;i.binding=pair.first.first;i.element=pair.first.second;i.offset=(total+15)&~VkDeviceSize(15);
    if(b.buffer.buffer){auto found=buffers.find(b.buffer.buffer);if(found==buffers.end()){skip(sn,i.binding,"buffer not tracked");continue;}auto offset=b.buffer.offset;if(b.dynamicIndex!=UINT32_MAX){if(b.dynamicIndex>=bound[sn].dynamic.size()){skip(sn,i.binding,"missing dynamic offset");continue;}offset+=bound[sn].dynamic[b.dynamicIndex];}
     auto& info=found->second;if(offset>info.size||!(info.usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)){skip(sn,i.binding,"buffer not copyable");continue;}auto size=b.buffer.range==VK_WHOLE_SIZE?info.size-offset:b.buffer.range;if(!size||size>info.size-offset||size>16*1024*1024||size%4||offset%4){skip(sn,i.binding,"buffer size/alignment");continue;}i.buffer=b.buffer.buffer;i.sourceOffset=offset;i.size=size;
    }else if(b.image.imageView){
     const bool geometry=t->shader==geometryShader,scene=t->shader==sceneShader,sky=atmosphere(t->shader),composite=t->shader==compositeShader;
     // Shading binding 27 is the shared scene shadow atlas (comparison
     // sampler at 28). Capture both layers to diagnose missing-eye shadows.
     const bool input=composite?((sn==1&&i.binding>=1&&i.binding<=7)||(sn==0&&(i.binding==3||i.binding==4||i.binding==6||i.binding==7||i.binding==9||i.binding==10))):sky?((sn==1&&i.binding>=1&&i.binding<=3)||(sn==0&&(i.binding==2||i.binding==5||i.binding==6||i.binding==8||i.binding==9||i.binding==10))):scene?(sn==1&&(i.binding==3||i.binding==4)):geometry?((sn==1&&i.binding>=1&&i.binding<=3)||(sn==0&&i.binding==4)):(sn==0&&(i.binding==2||i.binding==4||i.binding==5||i.binding==6||i.binding==18||i.binding==27));
     const bool output=sn==1&&(composite?i.binding==8:sky?(i.binding==1||i.binding==4):scene?i.binding==5:geometry?i.binding==4:(i.binding>=1&&i.binding<=4));if(!input&&!output)continue;
     auto v=views.find(b.image.imageView);if(v==views.end()){skip(sn,i.binding,"view not tracked");continue;}auto im=images.find(v->second.image);if(im==images.end()){skip(sn,i.binding,"image not tracked");continue;}const auto& vi=v->second;const auto& inf=im->second;
     // Shared atmosphere volumes are 3D, not eye-array images. Preserve
     // their full depth for offline ray/volume comparisons.
     if((sky||composite)&&inf.imageType==VK_IMAGE_TYPE_3D){appendImage(t,b.image.imageView,b.image.imageLayout,sn,i.binding,false,total);continue;}
     auto aspect=vi.subresourceRange.aspectMask;
     // A compute-only family supports color transfers, but depth/stencil
     // copies require capabilities we have not enabled. Omit those resources.
     if(!graphicsQueue&&(aspect&(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT))){skip(sn,i.binding,"depth copy omitted on compute queue");continue;}
     auto bytes=texelBytes(inf.format,aspect);auto mip=vi.subresourceRange.baseMipLevel;
     if(!bytes||inf.imageType!=VK_IMAGE_TYPE_2D||inf.samples!=VK_SAMPLE_COUNT_1_BIT||!(inf.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)||mip>=inf.mipLevels||vi.subresourceRange.baseArrayLayer>=inf.arrayLayers){skip(sn,i.binding,"unsupported image format/range");continue;}
     if(b.image.imageLayout!=VK_IMAGE_LAYOUT_GENERAL&&b.image.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&b.image.imageLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL){skip(sn,i.binding,"unsupported image layout");continue;}
     auto layers=vi.subresourceRange.layerCount==VK_REMAINING_ARRAY_LAYERS?inf.arrayLayers-vi.subresourceRange.baseArrayLayer:vi.subresourceRange.layerCount;if(layers>2||!layers||layers>inf.arrayLayers-vi.subresourceRange.baseArrayLayer){skip(sn,i.binding,"unsupported eye layer range");continue;}
     i.after=output;i.image=vi.image;i.view=b.image.imageView;i.layout=b.image.imageLayout;i.format=inf.format;i.extent={std::max(1u,inf.extent.width>>mip),std::max(1u,inf.extent.height>>mip),1};i.layers={aspect,mip,vi.subresourceRange.baseArrayLayer,layers};i.barrierAspect=combinedDepth(inf.format)?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:aspect;i.size=VkDeviceSize(i.extent.width)*i.extent.height*layers*bytes;
    }else continue;
    if(i.size>batchLimit||i.offset>batchLimit-i.size){skip(sn,i.binding,"snapshot budget");continue;}total=i.offset+i.size;t->items.push_back(i);
    // Atmosphere binding 1 is read/write: preserve its pre-dispatch contents too.
    if(atmosphere(t->shader)&&i.image&&sn==1&&i.binding==1){auto before=i;before.after=false;before.offset=(total+15)&~VkDeviceSize(15);if(before.offset<=batchLimit-before.size){total=before.offset+before.size;t->items.push_back(before);}else skip(sn,i.binding,"before snapshot budget");}
   }
  }
  if(t->items.empty()){remaining=0;log("WATER_GPU_CAPTURE no supported resources");return {};}
  try{t->allocation=allocate(total);}catch(const std::exception& e){remaining=0;log(e.what());return {};}
  commands[cb].push_back(t);remaining.fetch_sub(1);nextTick=paired&&!second?0:now+250;copies(cb,t,false);log("WATER_GPU_CAPTURE recorded="+t->folder.string());return t;
 }
 void after(VkCommandBuffer cb,const Ticket& t){if(t){copies(cb,t,true);t->recordedAfter=true;}}
 // Queue is externally synchronized by caller. Retain every fence on replay;
 // don't read staging while another submission of the same CB can overwrite it.
 void submitted(VkQueue queue,uint32_t count,const VkCommandBuffer* cbs){
  std::lock_guard<std::mutex> l(mutex);
  if(graphicsActive&&graphicsDraws<12000)for(uint32_t n=0;n<count;++n)drawJournal+="SUBMIT queue="+std::to_string(reinterpret_cast<uintptr_t>(queue))+" cb="+std::to_string(reinterpret_cast<uintptr_t>(cbs[n]))+" tick="+std::to_string(GetTickCount64())+"\n";
  std::vector<Ticket> list;for(uint32_t n=0;n<count;++n){auto command=commands.find(cbs[n]);if(command!=commands.end())for(auto& t:command->second)if(!t->saved&&!t->failed&&t->recordedAfter)list.push_back(t);}if(list.empty())return;
  auto fence=std::make_shared<Fence>();fence->device=device;fence->destroy=proc<PFN_vkDestroyFence>("vkDestroyFence");VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};ok(proc<PFN_vkCreateFence>("vkCreateFence")(device,&info,nullptr,&fence->value));ok(proc<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,0,nullptr,fence->value));
  for(auto& t:list){t->fences.push_back(fence);if(!t->queued){t->queued=true;pending.push_back(t);}}
 }
 // Device has already been retired by the owning runtime.
 void shutdown(){
  // Owner already waited for device idle; finish outstanding files before
  // releasing Vulkan storage. No game frame is being rendered here.
  for(;;){waitForWriter();poll(false);std::lock_guard<std::mutex> l(mutex);if(!writer.valid()){pending.clear();commands.clear();graphicsPasses.clear();break;}}
 }
#include "SkyDrawCapture.inl"
};
}
