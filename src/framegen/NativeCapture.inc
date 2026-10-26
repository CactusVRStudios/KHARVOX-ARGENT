// Included inside argent::fg. Capture native, non-jittered game inputs without
// entering the game's NGX path or changing its DLSS quality selection.
namespace {
bool readNative(uintptr_t address,void* destination,size_t bytes){
 SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),destination,bytes,&got)&&got==bytes;
}
bool nativeInputs(Device& d,const sfs::StereoFrame& pair,uint32_t count,const VkSemaphore* waits,bool& consumed,bool& nativePath,std::unique_ptr<kharvox::GameImageLifetime::Use>& borrowed){
 if(!pair.pose.framegenEligible||!camera::stats().installed)return false;
 const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));int aa=-1;
 if(!camera::readRenderControl(base,{0x6685ce0,"r_antialiasing","",0},readNative,aa)||aa!=0)return false;
 nativePath=true;
 {
  std::lock_guard lock(mutex);
  if(context&&(context->failed||context->pending.count(pair.pose.serial)))return false;
 }
 std::array<dlss::Resource,2> input{};std::array<dlss::EyeParameters,2> eyes{};
 std::array<VkImageLayout,2> sourceLayouts{};
 kharvox::hands::HandSceneTarget depth{};
 borrowed=std::make_unique<kharvox::GameImageLifetime::Use>(kharvox::gameImageLifetime());
 const char* reason="native render targets unavailable";
 const bool selected=borrowed->select([&]{
  std::array<dlss::Resource,2> motion{};
  if(!nativeMotionSources(base+build::rva(0x66e2c30),readNative,motion))return false;
  {
   std::lock_guard lock(mutex);uint64_t order{};
   for(const auto& r:motion){auto w=submittedWrites.find(r.image);
    if(w!=submittedWrites.end()&&currentNativeWrite(w->second,r,pair.pose.serial,d.graphicsQueue)&&w->second.order>order){input[1]=r;order=w->second.order;}
   }
   if(!order){
    static uint64_t probes{};
    if(extendedLogging()&&(++probes==1||probes%240==0)){
     std::string detail="FRAMEGEN_NATIVE_PROBE serial="+std::to_string(pair.pose.serial)+" aa=0";
     for(unsigned i=0;i<2;++i){auto w=submittedWrites.find(motion[i].image);detail+=" motion"+std::to_string(i)+"="+std::to_string(reinterpret_cast<uintptr_t>(motion[i].image));
      if(w!=submittedWrites.end())detail+=" writtenSerial="+std::to_string(w->second.serial)+" layers="+std::to_string(w->second.source.range.layerCount)+" queueMatch="+std::to_string(w->second.queue==d.graphicsQueue);else detail+=" producer=unseen";
     }
     log(detail);
    }
    reason="no current stereo motion-vector producer";return false;
   }
  }
  // Resolve depth from the pass that wrote this exact motion target. A general
  // extent search also finds DOOM's upscaled depth copy and is ambiguous at 1:1.
  {std::lock_guard lock(mutex);if(!nativeTargets.find(input[1].image,d.graphicsQueue,pair.pose.serial,depth)){reason="submitted motion pass depth association missing";return false;}}
  auto& r=input[0];r.image=depth.depthImage;r.view=depth.depthView;r.format=depth.depthFormat;
  r.width=depth.extent.width;r.height=depth.extent.height;r.range={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,2};
  if(depth.samples!=VK_SAMPLE_COUNT_1_BIT||depth.depthOffset.x||depth.depthOffset.y||depth.depthArrayLayer||r.width!=input[1].width||r.height!=input[1].height){reason="scene depth convention or extent mismatch";return false;}
  {
   std::lock_guard lock(mutex);auto w=submittedWrites.find(r.image);
   if(w==submittedWrites.end()||!currentNativeWrite(w->second,r,pair.pose.serial,d.graphicsQueue)){reason="no current stereo depth producer";return false;}
   auto ls=submittedLayouts.find(d.graphicsQueue);if(ls==submittedLayouts.end())return false;
   for(unsigned j=0;j<2;++j){auto range=input[j].range;if(!j)range.aspectMask=kharvox::hands::handSceneDepthAspect(r.format);
    sourceLayouts[j]=observedLayout(ls->second,input[j].image,range);
    if(sourceLayouts[j]==VK_IMAGE_LAYOUT_UNDEFINED||sourceLayouts[j]==VK_IMAGE_LAYOUT_PREINITIALIZED){reason="native input layout unresolved";return false;}
   }
  }
  // No FG lock across SFS lookup: native command recording takes these locks in
  // the opposite order. Pin the original handles before exposing cached views.
  if(!sfs::framegenEyeInputs(d.device,input,eyes)){reason="native inputs not sampleable stereo images";return false;}
  return true;
 },[&]{using L=kharvox::GameImageLifetime;return std::vector<L::Key>{L::key(input[0].image),L::key(input[0].view),L::key(input[1].image),L::key(input[1].view)};});
 if(!selected){state("waiting",reason);return false;}
 return captureNativeInputs(d,pair,input,eyes,sourceLayouts,count,waits,consumed,depth.reverseDepth,true);
}
}
bool captureNativeInputs(Device& d,const sfs::StereoFrame& pair,const std::array<dlss::Resource,2>& input,const std::array<dlss::EyeParameters,2>& eyes,const std::array<VkImageLayout,2>& sourceLayouts,uint32_t count,const VkSemaphore* waits,bool& consumed,bool reverseDepth,bool deferCompletion){
 if(!requested()||!pair.pose.framegenEligible)return false;
 for(unsigned j=0;j<2;++j){const auto& r=input[j];if(!r.image||!r.width||!r.height||r.width>8192||r.height>8192||r.width!=input[0].width||r.height!=input[0].height||r.range.layerCount!=2||r.range.levelCount!=1||sourceLayouts[j]==VK_IMAGE_LAYOUT_UNDEFINED)return false;
  for(unsigned e=0;e<2;++e){const auto& eye=eyes[e].resources[j+2];if(!eye.view||eye.image!=r.image||eye.range.layerCount!=1||eye.range.baseArrayLayer!=r.range.baseArrayLayer+e||eye.range.aspectMask!=r.range.aspectMask)return false;}
 }
 attach(d);
 std::lock_guard lock(mutex);if(!context||!context->ready||context->failed||context->gpu.dev.device!=d.device)return false;
 auto& c=*context;auto& g=c.gpu;auto prep=prepared.find(pair.pose.serial);
 if(prep==prepared.end()||c.pending.count(pair.pose.serial))return false;
 try{
  // Retire previous DX/Vulkan consumers before reusing imported input storage.
  g.waitAll();c.inFlight.reset();
  const auto w=input[0].width,h=input[0].height;auto p=c.nativeLease;
  if(!p||p->width!=w||p->height!=h){
   p.reset();for(auto& l:c.leases)if(l->native&&l.use_count()==1&&l->width==w&&l->height==h){p=l;break;}
   if(!p){if(c.leases.size()>=6){state("waiting","native input pool full");return false;}p=std::make_shared<Lease>();c.leases.push_back(p);initializeLease(c,*p,w,h);}
   c.nativeLease=p;
  }
  p->native=true;p->depthInverted=reverseDepth;p->serial=pair.pose.serial;p->metadata=prep->second;p->inputsReady=false;p->params={};
  for(auto& params:p->params){params.set<float>(0x3c,-float(w));params.set<float>(0x40,-float(h));}
  Interop::check(V(vkResetEvent)(d.device,p->complete),"reset native FG input event");g.beginVk(2);
  auto transition=[&](unsigned j,VkImageLayout from,VkImageLayout to){
   VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=input[j].image;b.oldLayout=from;b.newLayout=to;
   b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange=input[j].range;
   if(!j&&(input[j].range.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT))b.subresourceRange.aspectMask=kharvox::hands::handSceneDepthAspect(input[j].format);
   b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
   V(vkCmdPipelineBarrier)(g.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
  };
  for(unsigned j=0;j<2;++j)transition(j,sourceLayouts[j],VK_IMAGE_LAYOUT_GENERAL);
  for(unsigned e=0;e<2;++e){auto& eye=*p->eyes[e];VkDescriptorImageInfo images[4]{};VkWriteDescriptorSet writes[4]{};
   for(unsigned j=0;j<4;++j){images[j]={j<2?c.sampler:VK_NULL_HANDLE,j<2?eyes[e].resources[j+2].view:p->views[e*2+j-2],VK_IMAGE_LAYOUT_GENERAL};
    writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[j].dstSet=p->sets[e];writes[j].dstBinding=j;writes[j].descriptorCount=1;writes[j].descriptorType=j<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[j].pImageInfo=&images[j];}
   V(vkUpdateDescriptorSets)(d.device,4,writes,0,nullptr);
   for(auto* i:{&eye.depth,&eye.motion})g.barrier(g.command,i->image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_EXTERNAL,d.graphicsFamily);
   V(vkCmdBindPipeline)(g.command,VK_PIPELINE_BIND_POINT_COMPUTE,c.pipeline);V(vkCmdBindDescriptorSets)(g.command,VK_PIPELINE_BIND_POINT_COMPUTE,c.pipelineLayout,0,1,&p->sets[e],0,nullptr);V(vkCmdDispatch)(g.command,(w+7)/8,(h+7)/8,1);
   for(auto* i:{&eye.depth,&eye.motion})g.barrier(g.command,i->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,d.graphicsFamily,VK_QUEUE_FAMILY_EXTERNAL);
  }
  for(unsigned j=0;j<2;++j)transition(j,VK_IMAGE_LAYOUT_GENERAL,sourceLayouts[j]);
  V(vkCmdSetEvent)(g.command,p->complete,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  // Queue order covers conversion before the color handoff signals DX. Retain
  // borrowed handles through generate's scoped retirement, including failure.
  if(deferCompletion)g.submitNative(count,waits,&consumed);else g.submitVk(count,waits,&consumed);
  p->inputsReady=p->recorded=p->submitted=true;c.pending.emplace(p->serial,p);
  if(++c.captureSamples==1||c.captureSamples%240==0)log("FRAMEGEN_NATIVE_INPUT serial="+std::to_string(p->serial)+" aa=0 eyes=2 depth="+std::to_string(reinterpret_cast<uintptr_t>(input[0].image))+" motion="+std::to_string(reinterpret_cast<uintptr_t>(input[1].image))+" size="+std::to_string(w)+"x"+std::to_string(h)+" reverseDepth="+std::to_string(reverseDepth)+" jitter=0 source=native-render-targets");
  return true;
 }catch(const std::exception& e){c.failed=true;state("fallback",e.what());return false;}
}
