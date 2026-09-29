#include "BuildFeatures.h"
#include "CameraCapture.h"
#include "EternalCameraHook.h"
#include "RenderTrace.h"
#include "sfs/EternalProjection.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <cmath>
namespace argent {
namespace {
const std::filesystem::path& captureRoot(){static auto path=[]{if constexpr(cleanRelease)return std::filesystem::path{};wchar_t s[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",s,32768);return n&&n<32768?std::filesystem::path(s):std::filesystem::path{};}();return path;}
bool waterInputCapture(){static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("ARGENT_WATER_INPUT_CAPTURE",value,8)==1&&value[0]=='1';}();return enabled;}
}
bool CameraCapture::enabled(){return sfs::vrEnabled()||!captureRoot().empty();}
CameraCapture::Command& CameraCapture::localCommand(VkCommandBuffer command){
 thread_local sfs::RecordingCache<VkCommandBuffer,Command> cache;
 return cache.get(identity,commandRetirement.load(std::memory_order_acquire),command,[&]() -> Command& {
  std::shared_lock<std::shared_mutex> guard(mutex);return commands.at(command);
 });
}
void CameraCapture::pipelineLayout(VkPipelineLayout pipeline,const VkPipelineLayoutCreateInfo* info){
 std::unique_lock<std::shared_mutex> guard(mutex);uint32_t index=UINT32_MAX;
 if(info->setLayoutCount){const auto& dynamic=layouts.at(info->pSetLayouts[0]).dynamic;
  auto found=std::find(dynamic.begin(),dynamic.end(),std::make_pair(0u,0u));
  if(found!=dynamic.end())index=uint32_t(found-dynamic.begin());
 }
 cameraDynamicIndices[pipeline]=index;
}
void CameraCapture::retirePipelineLayout(VkPipelineLayout pipeline){
 std::unique_lock<std::shared_mutex> guard(mutex);
 layoutRetirement.fetch_add(1,std::memory_order_release);cameraDynamicIndices.erase(pipeline);
}
void CameraCapture::probePipeline(VkPipeline p,const std::string& sha){
 const char* label=nullptr;
 if(sha=="6378d9197bc1fbf36aefcef4a64e876f9cb16ce583833718e15372326ad93570")label="world-material";
 if(sha=="fdf9deea0da0466a80972d8d1c8c7bd8ee8acab749c1046b114a93ca273c301a")label="decal-bounds";
 if(sha=="526d84676aeb069214ffdb18885420888f0e8f2f112588c1748e01154b916b61")label="decal-coarse";
 if(sha=="125ee2f9bca68c76b1b72203dcdbee998a5e1fde3ed9d0f5e1a31a5bb9a026c8")label="decal-fine";
 if(sha=="477234f2c7e66623eecfe6d65fd41b82ce537139a070c9ffd6472dbc3ef0b74a")label="light-bounds";
 if(sha=="43dccf7a0bc3d66e72953edd0877ddc959d2600dc247ca2253456260872536e4")label="water-shading";
 if(!label)return;
 std::unique_lock<std::shared_mutex> guard(mutex);probePipelines[p]=label;
}
void CameraCapture::sampleProbe(Command& cmd,unsigned point){
 const auto epoch=probeEpoch.load(std::memory_order_relaxed);const auto& bound=cmd.probeBindings[point];
 if(!epoch||!bound.set||!probePipelines.count(bound.pipeline)||cmd.probeSamples.size()>=16)return;
 if(waterInputCapture()&&probePipelines.at(bound.pipeline)!="water-shading")return;
 for(const auto& sample:cmd.probeSamples)if(sample.epoch==epoch&&sample.bound.pipeline==bound.pipeline&&sample.bound.set==bound.set&&sample.bound.dynamic==bound.dynamic)return;
 cmd.probeSamples.push_back({epoch,bound});
}
void CameraCapture::dispatch(VkCommandBuffer cb){
 if(!probeEpoch.load(std::memory_order_relaxed))return;
 std::shared_lock<std::shared_mutex> guard(mutex);auto it=commands.find(cb);if(it!=commands.end())sampleProbe(it->second,1);
}
void CameraCapture::submitProbe(uint32_t count,const VkCommandBuffer* buffers){
 // Called under the metadata lock. Only explicitly requested, host-mapped
 // inputs are copied at submit before the application can recycle uploads.
 // Copyable GPU buffers are queued for the explicitly requested readback.
 const auto now=GetTickCount64();
 static const auto root=[] {wchar_t path[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);return n&&n<32768?std::filesystem::path(path).parent_path():std::filesystem::path{};}();
 if(root.empty())return;
 const bool waterProbe=waterInputCapture();
 if(probeEpoch.load(std::memory_order_relaxed)&&now>=probeEnd){probeEpoch.store(0,std::memory_order_relaxed);log("DECAL_INPUT_PROBE complete epoch="+std::to_string(probeOutputEpoch)+" samples="+std::to_string(probeCount));}
 if(now>=probePoll&&!probeEpoch.load(std::memory_order_relaxed)){
  probePoll=now+(waterProbe?2000:1000);
  if(waterProbe&&waterCaptureBytes>=64*1024*1024)return;std::error_code ec;
  if(waterProbe||std::filesystem::remove(root/"capture-decal-inputs.request",ec)){
   probeOutputEpoch=now;probeCount=0;probeWritten.clear();probeEnd=now+500;probeEpoch.store(now,std::memory_order_relaxed);
   log("DECAL_INPUT_PROBE armed epoch="+std::to_string(now));
  }
 }
 const auto epoch=probeEpoch.load(std::memory_order_relaxed);if(!epoch||probeCount>=16)return;
 for(uint32_t i=0;i<count&&probeCount<16;++i){auto cmd=commands.find(buffers[i]);if(cmd==commands.end())continue;
  for(const auto& sample:cmd->second.probeSamples){
   if(sample.epoch!=epoch||probeCount>=16)continue;
   auto tag=probePipelines.find(sample.bound.pipeline);auto set=sets.find(sample.bound.set);if(tag==probePipelines.end()||set==sets.end())continue;
   const bool water=tag->second=="water-shading";if(waterProbe&&!water)continue;
   const auto identity=std::make_pair(tag->second,sample.bound.set);if(std::find(probeWritten.begin(),probeWritten.end(),identity)!=probeWritten.end())continue;
   probeWritten.push_back(identity);const auto ordinal=probeCount++;
   if(water){
    std::ofstream images(root/"water-bindings.tsv",std::ios::app);
    auto bindings=probeImages.find(sample.bound.set);
    if(bindings!=probeImages.end())for(const auto& entry:bindings->second){
     const auto& image=entry.second;
     images<<epoch<<'\t'<<ordinal<<'\t'<<trace::id(sample.bound.pipeline)<<'\t'<<trace::id(sample.bound.set)<<'\t'<<entry.first.first<<'\t'<<entry.first.second<<'\t'<<trace::id(image.imageView)<<'\t'<<image.imageLayout<<'\n';
    }
    waterCaptureBytes+=1024;
   }
   std::map<std::pair<uint32_t,uint32_t>,uint32_t> dynamic;auto sl=setLayouts.find(sample.bound.set);
   if(sl!=setLayouts.end()){auto l=layouts.find(sl->second);if(l!=layouts.end())for(size_t j=0;j<l->second.dynamic.size()&&j<sample.bound.dynamic.size();++j)dynamic[l->second.dynamic[j]]=sample.bound.dynamic[j];}
   std::ofstream meta(root/("decal-inputs-"+std::to_string(epoch)+".tsv"),std::ios::app);
   const std::vector<uint32_t> slots=water?std::vector<uint32_t>{0,9,19,24}:std::vector<uint32_t>{0,2,15,27,35};
   for(uint32_t slot:slots){
    if(slot==2&&tag->second!="decal-bounds"&&tag->second!="light-bounds")continue;
    if(slot>=15&&tag->second!="world-material"&&!water)continue;
    auto found=set->second.find({slot,0});if(found==set->second.end())continue;
    const auto& binding=found->second;const uint64_t dyn=binding.dynamic?dynamic[{slot,0}]:0;
    if(binding.offset>UINT64_MAX-dyn)continue;
    const auto offset=binding.offset+dyn;const auto size=size_t(std::min<VkDeviceSize>(binding.range,slot==0?1024:1024*1024));
    std::vector<unsigned char> bytes;const bool available=slot==0&&size&&memory.read(binding.buffer,offset,size,bytes);
    const auto file="decal-inputs-"+std::to_string(epoch)+"-"+std::to_string(ordinal)+"-"+tag->second+"-b"+std::to_string(slot)+".bin";
    meta<<tag->second<<'\t'<<trace::id(sample.bound.pipeline)<<'\t'<<trace::id(sample.bound.set)<<'\t'<<slot<<'\t'<<trace::id(binding.buffer)<<'\t'<<offset<<'\t'<<binding.range<<'\t'<<size<<'\t'<<available<<'\t'<<file<<'\n';
    if(available){std::ofstream out(root/file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(water)waterCaptureBytes+=bytes.size();}
    else if(!waterProbe&&slot!=0&&size&&size%4==0&&offset%4==0&&memory.copyable(binding.buffer,offset,size)){
     bool duplicate=false;for(const auto& r:pendingProbeReadback)duplicate|=r.buffer==binding.buffer&&r.offset==offset&&r.size==size;
     if(!duplicate&&pendingProbeReadback.size()<4){pendingProbeReadback.push_back({binding.buffer,offset,sample.bound.pipeline,slot,0,size,root/file});probeGpuReady.store(true,std::memory_order_relaxed);}
    }

   }
  }
 }
}
bool CameraCapture::projection(sfs::Matrix& result,uint64_t maxAgeMs){std::shared_lock<std::shared_mutex> guard(mutex);if(!liveProjectionValid||(maxAgeMs&&GetTickCount64()-liveProjectionTick>maxAgeMs))return false;result=liveProjection;return true;}
void CameraCapture::layout(VkDescriptorSetLayout l,const VkDescriptorSetLayoutCreateInfo* ci){std::unique_lock<std::shared_mutex> guard(mutex);auto& item=layouts[l];item.dynamic.clear();for(uint32_t i=0;i<ci->bindingCount;++i){auto& b=ci->pBindings[i];if(b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||b.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)for(uint32_t j=0;j<b.descriptorCount;++j)item.dynamic.push_back({b.binding,j});}std::sort(item.dynamic.begin(),item.dynamic.end());}
void CameraCapture::allocate(const VkDescriptorSetAllocateInfo* ci,const VkDescriptorSet* out){std::unique_lock<std::shared_mutex> guard(mutex);for(uint32_t i=0;i<ci->descriptorSetCount;++i){sets[out[i]].clear();probeImages.erase(out[i]);setLayouts[out[i]]=ci->pSetLayouts[i];setPools.insert(ci->descriptorPool,out[i]);}}
void CameraCapture::retireSets(uint32_t n,const VkDescriptorSet* out){std::unique_lock<std::shared_mutex> guard(mutex);for(uint32_t i=0;i<n;++i){sets.erase(out[i]);probeImages.erase(out[i]);setLayouts.erase(out[i]);setPools.erase(out[i]);}}
void CameraCapture::retirePool(VkDescriptorPool p){std::unique_lock<std::shared_mutex> guard(mutex);setPools.retire(p,[&](VkDescriptorSet set){sets.erase(set);probeImages.erase(set);setLayouts.erase(set);});}
void CameraCapture::update(uint32_t n,const VkWriteDescriptorSet* writes,uint32_t nc,const VkCopyDescriptorSet* copies){std::unique_lock<std::shared_mutex> guard(mutex);
    for(uint32_t i=0;i<n;++i){auto& w=writes[i];bool dyn=w.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||w.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;bool buffer=dyn||w.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||w.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;if(waterInputCapture()&&w.pImageInfo&&w.dstBinding<64&&(w.descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||w.descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER||w.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE||w.descriptorType==VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)){for(uint32_t j=0;j<w.descriptorCount&&uint64_t(w.dstArrayElement)+j<16;++j)probeImages[w.dstSet][{w.dstBinding,w.dstArrayElement+j}]=w.pImageInfo[j];}if(!buffer||!w.pBufferInfo)continue;for(uint32_t j=0;j<w.descriptorCount;++j){auto& b=w.pBufferInfo[j];sets[w.dstSet][{w.dstBinding,w.dstArrayElement+j}]={b.buffer,b.offset,b.range,dyn};}}
    for(uint32_t i=0;i<nc;++i){auto& c=copies[i];for(uint32_t j=0;j<c.descriptorCount;++j){if(waterInputCapture()&&c.dstBinding<64&&uint64_t(c.dstArrayElement)+j<16){auto source=probeImages.find(c.srcSet);if(source!=probeImages.end()){auto image=source->second.find({c.srcBinding,c.srcArrayElement+j});if(image!=source->second.end())probeImages[c.dstSet][{c.dstBinding,c.dstArrayElement+j}]=image->second;}}auto found=sets[c.srcSet].find({c.srcBinding,c.srcArrayElement+j});if(found!=sets[c.srcSet].end())sets[c.dstSet][{c.dstBinding,c.dstArrayElement+j}]=found->second;}}
}
void CameraCapture::pipeline(VkPipeline p,uint32_t binding,uint32_t offset){std::unique_lock<std::shared_mutex> guard(mutex);pipelines[p]={binding,offset};}
void CameraCapture::destroyPipeline(VkPipeline p){std::unique_lock<std::shared_mutex> guard(mutex);pipelines.erase(p);probePipelines.erase(p);}
void CameraCapture::command(VkCommandBuffer c,VkCommandPool p){std::unique_lock<std::shared_mutex> guard(mutex);auto& cmd=commands[c];cmd.events.clear();cmd.commonCandidates.clear();cmd.livePipeline={};cmd.liveCameraPipeline=false;cmd.liveSet={};cmd.liveDynamic=0;cmd.probeBindings={};cmd.probeSamples.clear();if(p)cmd.pool=p;}
void CameraCapture::freeCommand(VkCommandBuffer c){std::unique_lock<std::shared_mutex> guard(mutex);commandRetirement.fetch_add(1,std::memory_order_release);commands.erase(c);}
void CameraCapture::resetPool(VkCommandPool p,bool destroy){std::unique_lock<std::shared_mutex> guard(mutex);if(destroy)commandRetirement.fetch_add(1,std::memory_order_release);for(auto i=commands.begin();i!=commands.end();)if(i->second.pool==p){if(destroy)i=commands.erase(i);else {i->second.events.clear();i->second.commonCandidates.clear();i->second.livePipeline={};i->second.liveCameraPipeline=false;i->second.liveSet={};i->second.liveDynamic=0;i->second.probeBindings={};i->second.probeSamples.clear();++i;}}else ++i;}
void CameraCapture::append(VkCommandBuffer c,Event e){std::unique_lock<std::shared_mutex> guard(mutex);auto it=commands.find(c);if(it!=commands.end()&&it->second.events.size()<32768)it->second.events.push_back(std::move(e));}
void CameraCapture::bindPipeline(VkCommandBuffer c,VkPipeline p,VkPipelineBindPoint point){
 if(probeEpoch.load(std::memory_order_relaxed)&&(point==VK_PIPELINE_BIND_POINT_GRAPHICS||point==VK_PIPELINE_BIND_POINT_COMPUTE)){std::shared_lock<std::shared_mutex> guard(mutex);auto it=commands.find(c);if(it!=commands.end())it->second.probeBindings[point==VK_PIPELINE_BIND_POINT_COMPUTE?1:0].pipeline=p;}
 if(point!=VK_PIPELINE_BIND_POINT_GRAPHICS)return;
if(sfs::vrEnabled()){std::shared_lock<std::shared_mutex> guard(mutex);auto it=commands.find(c);if(it!=commands.end()){it->second.livePipeline=p;it->second.liveCameraPipeline=pipelines.count(p)!=0;}return;}append(c,{Event::PipelineBind,trace::id(p)});}
void CameraCapture::bindSets(VkCommandBuffer c,uint32_t first,uint32_t n,const VkDescriptorSet* sets,uint32_t nd,const uint32_t* dyn,VkPipelineBindPoint point,VkPipelineLayout pipelineLayout){
 if(probeEpoch.load(std::memory_order_relaxed)&&first==0&&n&&(point==VK_PIPELINE_BIND_POINT_GRAPHICS||point==VK_PIPELINE_BIND_POINT_COMPUTE)){std::shared_lock<std::shared_mutex> guard(mutex);auto it=commands.find(c);if(it!=commands.end()){auto& bound=it->second.probeBindings[point==VK_PIPELINE_BIND_POINT_COMPUTE?1:0];bound.set=sets[0];bound.dynamic.clear();if(nd)bound.dynamic.assign(dyn,dyn+nd);}}
 if(point!=VK_PIPELINE_BIND_POINT_GRAPHICS)return;
if(sfs::vrEnabled()){if(first||!n)return;auto& cmd=localCommand(c);cmd.liveSet=sets[0];cmd.liveDynamic=0;if(nd){thread_local sfs::DescriptorCountCache<VkPipelineLayout> indices;const auto index=indices.get(identity,layoutRetirement.load(std::memory_order_acquire),pipelineLayout,[&]{std::shared_lock<std::shared_mutex> guard(mutex);return cameraDynamicIndices.at(pipelineLayout);});if(index<nd)cmd.liveDynamic=dyn[index];}return;}Event e{Event::Sets,first,n,nd};for(uint32_t i=0;i<n;++i)e.values.push_back(trace::id(sets[i]));for(uint32_t i=0;i<nd;++i)e.values.push_back(dyn[i]);append(c,std::move(e));}
void CameraCapture::push(VkCommandBuffer c,uint32_t offset,uint32_t size,const void* data){if(sfs::vrEnabled()||offset>=16)return;Event e{Event::Push,offset};size=std::min(size,16-offset);auto bytes=static_cast<const unsigned char*>(data);for(uint32_t i=0;i<size;++i)e.values.push_back(bytes[i]);append(c,std::move(e));}
void CameraCapture::draw(VkCommandBuffer c){
 if(probeEpoch.load(std::memory_order_relaxed)){std::shared_lock<std::shared_mutex> guard(mutex);auto it=commands.find(c);if(it!=commands.end())sampleProbe(it->second,0);}
if(sfs::vrEnabled()){auto& cmd=localCommand(c);if(cmd.commonCandidates.size()>=8||!cmd.liveSet||!cmd.liveCameraPipeline)return;auto candidate=std::make_pair(cmd.liveSet,cmd.liveDynamic);if(std::find(cmd.commonCandidates.begin(),cmd.commonCandidates.end(),candidate)==cmd.commonCandidates.end())cmd.commonCandidates.push_back(candidate);return;}append(c,{Event::Draw});}
void CameraCapture::copy(VkCommandBuffer c,VkBuffer src,VkBuffer dst,uint32_t n,const VkBufferCopy* regions){if(sfs::vrEnabled())return;for(uint32_t i=0;i<n;++i)if(regions[i].size<=4*1024*1024)append(c,{Event::Copy,trace::id(src),trace::id(dst),regions[i].size,{regions[i].srcOffset,regions[i].dstOffset}});}
void CameraCapture::submit(VkQueue queue,uint32_t n,const VkCommandBuffer* submitted){
    const bool realtime=sfs::vrEnabled();const auto frame=trace::currentFrame();if(!realtime&&frame!=120&&frame!=600&&frame!=1800&&frame!=3600&&frame!=7200&&frame!=14400)return;
    std::unique_lock<std::shared_mutex> guard(mutex);submitProbe(n,submitted);if(lastFrame!=frame){lastFrame=frame;captured=0;}if(!realtime&&captured>=256)return;
    if(realtime){
        for(uint32_t i=0;i<n;++i){auto cmd=commands.find(submitted[i]);if(cmd==commands.end())continue;
            for(const auto& candidate:cmd->second.commonCandidates){auto set=sets.find(candidate.first);if(set==sets.end())continue;auto binding=set->second.find({0,0});if(binding==set->second.end())continue;auto cb=binding->second;if(cb.dynamic)cb.offset+=candidate.second;
                std::vector<unsigned char> bytes;sfs::Matrix projection;
                if((cb.range==VK_WHOLE_SIZE||cb.range>=120)&&memory.read(cb.buffer,cb.offset,120,bytes)){
                    const bool valid=sfs::eternalProjection(bytes.data(),bytes.size(),projection);
                    if(probeEpoch.load(std::memory_order_relaxed)&&!captureRoot().empty()&&captured<8){
                        float values[27];std::memcpy(values,bytes.data(),sizeof(values));
                        std::ofstream out(captureRoot()/"camera-common.tsv",std::ios::app);out<<std::setprecision(9)<<frame<<'\t'<<valid<<'\t'<<trace::id(candidate.first);
                        for(float value:values)out<<'\t'<<value;out<<'\n';++captured;
                    }
                    if(valid){liveProjection=projection;liveProjectionValid=true;liveProjectionTick=GetTickCount64();camera::observeRenderedCamera(bytes.data(),bytes.size());return;}
                }
            }
        }
        return;
    }
    std::ofstream out;if(!realtime)out.open(captureRoot()/"camera-samples.tsv",std::ios::app);out<<std::setprecision(9);
    struct Shadow {VkDeviceSize offset;std::vector<unsigned char> bytes;};std::unordered_map<VkBuffer,std::vector<Shadow>> shadows;size_t copied{};
    auto read=[&](VkBuffer b,VkDeviceSize offset,size_t count,std::vector<unsigned char>& bytes){auto it=shadows.find(b);if(it!=shadows.end())for(auto j=it->second.rbegin();j!=it->second.rend();++j)if(offset>=j->offset&&offset-j->offset<=j->bytes.size()&&count<=j->bytes.size()-(offset-j->offset)){bytes.assign(j->bytes.begin()+size_t(offset-j->offset),j->bytes.begin()+size_t(offset-j->offset)+count);return true;}return memory.read(b,offset,count,bytes);};
    for(uint32_t ci=0;ci<n;++ci){auto found=commands.find(submitted[ci]);if(found==commands.end())continue;
        VkPipeline pipeline{};std::array<unsigned char,16> pushes{};std::map<uint32_t,VkDescriptorSet> boundSets;std::map<std::pair<uint32_t,uint32_t>,uint32_t> dynamic;
        for(auto& e:found->second.events){
            if(e.type==Event::PipelineBind)pipeline=reinterpret_cast<VkPipeline>(e.a);
            else if(e.type==Event::Push)for(size_t i=0;i<e.values.size();++i)pushes[e.a+i]=static_cast<unsigned char>(e.values[i]);
            else if(e.type==Event::Sets){size_t cursor=size_t(e.b);for(uint32_t j=0;j<e.b;++j){auto set=reinterpret_cast<VkDescriptorSet>(e.values[j]);boundSets[uint32_t(e.a)+j]=set;auto sl=setLayouts.find(set);if(sl==setLayouts.end())continue;for(auto& b:layouts[sl->second].dynamic){if(cursor<e.values.size()&&b.second==0)dynamic[{uint32_t(e.a)+j,b.first}]=uint32_t(e.values[cursor]);++cursor;}}}
            else if(!realtime&&e.type==Event::Copy&&copied+e.c<=16*1024*1024){std::vector<unsigned char> data;if(read(reinterpret_cast<VkBuffer>(e.a),e.values[0],size_t(e.c),data)){copied+=data.size();shadows[reinterpret_cast<VkBuffer>(e.b)].push_back({e.values[1],std::move(data)});}}
            else if(e.type==Event::Draw&&(realtime||captured<256)){auto pi=pipelines.find(pipeline);if(pi==pipelines.end())continue;auto bound=boundSets.find(0);if(bound==boundSets.end())continue;auto set=sets.find(bound->second);if(set==sets.end())continue;auto binding=set->second.find({pi->second.binding,0});if(binding==set->second.end())continue;
                if(realtime){auto common=set->second.find({0,0});if(common!=set->second.end()){auto cb=common->second;if(cb.dynamic)cb.offset+=dynamic[{0,0}];std::vector<unsigned char> data;sfs::Matrix candidate;if((cb.range==VK_WHOLE_SIZE||cb.range>=120)&&read(cb.buffer,cb.offset,120,data)){
 if(sfs::eternalProjection(data.data(),data.size(),candidate)){liveProjection=candidate;liveProjectionValid=true;liveProjectionTick=GetTickCount64();camera::observeRenderedCamera(data.data(),data.size());return;}
 static unsigned rejected=0;if(rejected<12){++rejected;float v[21];std::memcpy(v,data.data(),sizeof(v));log("ETERNAL_CAMERA_REJECTED depth="+std::to_string(v[2])+","+std::to_string(v[3])+" viewport="+std::to_string(v[8])+","+std::to_string(v[9])+" inverseScale="+std::to_string(v[19])+","+std::to_string(v[20]));}
 }}continue;}
                uint32_t index{};std::memcpy(&index,pushes.data()+pi->second.pushOffset,4);auto& b=binding->second;VkDeviceSize relative=VkDeviceSize(index)*64;uint32_t dyn=b.dynamic?dynamic[{0,pi->second.binding}]:0;std::vector<unsigned char> bytes;
                bool valid=(b.range==VK_WHOLE_SIZE||(relative<=b.range&&64<=b.range-relative))&&b.offset<=UINT64_MAX-relative&&b.offset+relative<=UINT64_MAX-dyn&&read(b.buffer,b.offset+relative+dyn,64,bytes);
                if(!valid&&pendingReadback.size()<256&&(b.range==VK_WHOLE_SIZE||(relative<=b.range&&64<=b.range-relative))&&b.offset<=UINT64_MAX-relative&&b.offset+relative<=UINT64_MAX-dyn)pendingReadback.push_back({b.buffer,b.offset+relative+dyn,pipeline,pi->second.binding,index});
                out<<frame<<'\t'<<trace::id(queue)<<'\t'<<trace::id(pipeline)<<'\t'<<pi->second.binding<<'\t'<<index<<'\t'<<(valid?"mapped_or_staged":"unavailable");
                if(valid){float values[16];std::memcpy(values,bytes.data(),64);for(auto f:values)out<<'\t'<<f;}
                out<<'\n';++captured;
                // Capture the shared frame constants independently of object MVP.
                auto common=set->second.find({0,0});if(common!=set->second.end()){auto& cb=common->second;auto offset=cb.offset+(cb.dynamic?dynamic[{0,0}]:0);size_t length=cb.range==VK_WHOLE_SIZE?1024:size_t(std::min<VkDeviceSize>(cb.range,1024));if(length&&read(cb.buffer,offset,length,bytes)){std::ofstream data(captureRoot()/("camera-ubo-"+std::to_string(frame)+"-"+std::to_string(trace::id(cb.buffer))+"-"+std::to_string(offset)+".bin"),std::ios::binary);data.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}}
            }
        }
    }
}
void CameraCapture::readback(Device& d,VkQueue queue,uint32_t family,bool probeOnly){
    std::vector<Readback> requests;{std::unique_lock<std::shared_mutex> guard(mutex);requests.swap(probeOnly?pendingProbeReadback:pendingReadback);if(probeOnly)probeGpuReady.store(false,std::memory_order_relaxed);}if(requests.empty())return;
    VkBuffer buffer{};VkDeviceMemory memory{};VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};bool submitted=false;
    auto check=[](VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Camera GPU readback result="+std::to_string(r));};
#define GPU(name) d.proc<PFN_##name>(#name)
    auto cleanup=[&]{if(submitted)GPU(vkQueueWaitIdle)(queue);if(fence)GPU(vkDestroyFence)(d.device,fence,nullptr);if(pool)GPU(vkDestroyCommandPool)(d.device,pool,nullptr);if(buffer)GPU(vkDestroyBuffer)(d.device,buffer,nullptr);if(memory)GPU(vkFreeMemory)(d.device,memory,nullptr);};
    try{
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};for(const auto& r:requests)bi.size+=r.size;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;check(GPU(vkCreateBuffer)(d.device,&bi,nullptr,&buffer));
        VkMemoryRequirements req{};GPU(vkGetBufferMemoryRequirements)(d.device,buffer,&req);VkPhysicalDeviceMemoryProperties props{};reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceMemoryProperties"))(d.physical,&props);
        uint32_t type=UINT32_MAX;for(uint32_t i=0;i<props.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}if(type==UINT32_MAX)throw std::runtime_error("No coherent readback memory");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;check(GPU(vkAllocateMemory)(d.device,&ai,nullptr,&memory));check(GPU(vkBindBufferMemory)(d.device,buffer,memory,0));
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;check(GPU(vkCreateCommandPool)(d.device,&pi,nullptr,&pool));VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ci.commandPool=pool;ci.commandBufferCount=1;ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;check(GPU(vkAllocateCommandBuffers)(d.device,&ci,&command));if(d.setLoaderData)check(d.setLoaderData(d.device,command));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(GPU(vkBeginCommandBuffer)(command,&begin));VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;GPU(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        VkDeviceSize cursor=0;for(const auto& r:requests){VkBufferCopy copy{r.offset,cursor,r.size};GPU(vkCmdCopyBuffer)(command,r.buffer,buffer,1,&copy);cursor+=r.size;}
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;GPU(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);check(GPU(vkEndCommandBuffer)(command));VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(GPU(vkCreateFence)(d.device,&fi,nullptr,&fence));VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&command;check(GPU(vkQueueSubmit)(queue,1,&si,fence));submitted=true;check(GPU(vkWaitForFences)(d.device,1,&fence,VK_TRUE,UINT64_MAX));submitted=false;
        void* mapped{};check(GPU(vkMapMemory)(d.device,memory,0,bi.size,0,&mapped));std::vector<unsigned char> values(size_t(bi.size));std::memcpy(values.data(),mapped,size_t(bi.size));GPU(vkUnmapMemory)(d.device,memory);
        cursor=0;for(const auto& r:requests){
         if(!r.output.empty()){std::ofstream out(r.output,std::ios::binary);out.write(reinterpret_cast<const char*>(values.data()+size_t(cursor)),std::streamsize(r.size));if(!out)throw std::runtime_error("Cannot write decal GPU buffer");log("DECAL_GPU_BUFFER "+r.output.string()+" bytes="+std::to_string(r.size));}
         else {std::ofstream out(captureRoot()/"camera-gpu.tsv",std::ios::app);out<<std::setprecision(9)<<trace::currentFrame()<<'\t'<<trace::id(r.pipeline)<<'\t'<<r.binding<<'\t'<<r.index;float floats[16];std::memcpy(floats,values.data()+size_t(cursor),64);for(auto f:floats)out<<'\t'<<f;out<<'\n';}
         cursor+=r.size;
        }
        log("CAMERA_GPU_READBACK buffers="+std::to_string(requests.size())+" frame="+std::to_string(trace::currentFrame()));cleanup();
    }catch(const std::exception& e){log(e.what());cleanup();}
#undef GPU
}
}

