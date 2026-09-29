// Included inside WaterGpuCapture. All copies occur outside render passes.
private:
 struct GraphicsPipeline {std::string state;bool sky{};};
 struct CaptureFramebuffer {std::vector<VkImageView> attachments;};
 struct GraphicsPass {VkRenderPass pass{};VkFramebuffer framebuffer{};uint32_t subpass{};Ticket ticket;unsigned draws{},skyDraws{};std::vector<std::vector<BoundSet>> inputs;};
 std::map<VkPipeline,GraphicsPipeline> graphicsPipelines;
 std::map<VkRenderPass,std::vector<VkAttachmentDescription>> capturePasses;
 std::map<VkFramebuffer,CaptureFramebuffer> captureFramebuffers;
 std::set<std::pair<VkRenderPass,VkFramebuffer>> observedSkyPasses;
 std::map<VkCommandBuffer,GraphicsPass> graphicsPasses;
 std::atomic<bool> graphicsActive{};uint64_t graphicsDeadline{};unsigned graphicsShots{},graphicsDraws{};
 std::string drawJournal;
 std::map<std::pair<VkPipeline,VkRenderPass>,unsigned> drawOccurrences;
 std::set<std::tuple<VkRenderPass,VkFramebuffer,bool,bool>> passDecisions;
 bool appendImage(const Ticket& t,VkImageView view,VkImageLayout layout,uint32_t set,uint32_t binding,bool after,VkDeviceSize& total){
  auto reject=[&](const char* why){t->skipped.push_back("s"+std::to_string(set)+" b"+std::to_string(binding)+" "+why);return false;};
  auto v=views.find(view);if(v==views.end())return reject("view not tracked");auto im=images.find(v->second.image);if(im==images.end())return reject("image not tracked");
  const auto& vi=v->second;const auto& inf=im->second;auto mip=vi.subresourceRange.baseMipLevel;auto aspect=vi.subresourceRange.aspectMask;
  if(aspect&VK_IMAGE_ASPECT_DEPTH_BIT)aspect=VK_IMAGE_ASPECT_DEPTH_BIT;
  auto bytes=texelBytes(inf.format,aspect);uint32_t block=1;
  if(!bytes&&aspect==VK_IMAGE_ASPECT_COLOR_BIT){switch(inf.format){
   case VK_FORMAT_BC1_RGB_UNORM_BLOCK:case VK_FORMAT_BC1_RGB_SRGB_BLOCK:case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:case VK_FORMAT_BC4_UNORM_BLOCK:case VK_FORMAT_BC4_SNORM_BLOCK:bytes=8;block=4;break;
   case VK_FORMAT_BC2_UNORM_BLOCK:case VK_FORMAT_BC2_SRGB_BLOCK:case VK_FORMAT_BC3_UNORM_BLOCK:case VK_FORMAT_BC3_SRGB_BLOCK:case VK_FORMAT_BC5_UNORM_BLOCK:case VK_FORMAT_BC5_SNORM_BLOCK:case VK_FORMAT_BC6H_UFLOAT_BLOCK:case VK_FORMAT_BC6H_SFLOAT_BLOCK:case VK_FORMAT_BC7_UNORM_BLOCK:case VK_FORMAT_BC7_SRGB_BLOCK:bytes=16;block=4;break;
   default:break;
  }}
  if(!bytes||inf.samples!=VK_SAMPLE_COUNT_1_BIT||!(inf.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)||mip>=inf.mipLevels)return reject("unsupported format/samples/transfer usage");
  if(layout!=VK_IMAGE_LAYOUT_GENERAL&&layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL&&layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL&&layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)return reject("undefined or unsupported layout");
  auto base=vi.subresourceRange.baseArrayLayer;if(base>=inf.arrayLayers)return reject("invalid layer base");
  auto layers=vi.subresourceRange.layerCount==VK_REMAINING_ARRAY_LAYERS?inf.arrayLayers-base:vi.subresourceRange.layerCount;
  if(!layers||layers>6||layers>inf.arrayLayers-base||(inf.imageType==VK_IMAGE_TYPE_3D&&(base||layers!=1)))return reject("unsupported layer range");
  Item i{};i.set=set;i.binding=binding;i.after=after;i.image=vi.image;i.view=view;i.layout=layout;i.format=inf.format;
  i.extent={std::max(1u,inf.extent.width>>mip),std::max(1u,inf.extent.height>>mip),std::max(1u,inf.extent.depth>>mip)};
  i.layers={aspect,mip,base,layers};i.barrierAspect=combinedDepth(inf.format)?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:aspect;
  i.size=VkDeviceSize((i.extent.width+block-1)/block)*((i.extent.height+block-1)/block)*i.extent.depth*layers*bytes;i.offset=(total+15)&~VkDeviceSize(15);
  if(i.size>batchLimit||i.offset>batchLimit-i.size)return reject("snapshot budget");
  total=i.offset+i.size;t->items.push_back(i);return true;
 }
 Ticket graphicsTicket(const std::string& suffix){auto t=std::make_shared<Batch>();t->graphics=true;t->tick=GetTickCount64();t->generation=session;t->folder=root/std::to_string(session)/suffix;return t;}
 bool stageGraphics(VkCommandBuffer cb,const Ticket& t,VkDeviceSize bytes){try{t->allocation=allocate(std::max<VkDeviceSize>(bytes,16));commands[cb].push_back(t);return true;}catch(const std::exception& e){log(e.what());return false;}}
public:
 bool graphicsArmed()const{return graphicsActive.load();}
 void graphicsPipeline(VkPipeline pipeline,const VkGraphicsPipelineCreateInfo& info,const std::vector<std::pair<uint32_t,uint64_t>>& hashes){
  std::lock_guard<std::mutex> l(mutex);GraphicsPipeline p;std::ostringstream out;
  for(auto [stage,hash]:hashes){out<<"stage"<<stage<<'='<<std::hex<<hash<<std::dec<<' ';p.sky|=stage==VK_SHADER_STAGE_FRAGMENT_BIT&&(hash==0xcbed08c426444de3ull||hash==0x2f28ea0af971c44bull);}
  out<<"subpass="<<info.subpass;
  if(auto d=info.pDepthStencilState)out<<" depthTest="<<d->depthTestEnable<<" depthWrite="<<d->depthWriteEnable<<" depthCompare="<<d->depthCompareOp<<" stencil="<<d->stencilTestEnable;
  if(auto r=info.pRasterizationState)out<<" cull="<<r->cullMode<<" frontFace="<<r->frontFace;
  if(auto b=info.pColorBlendState)for(unsigned n=0;n<b->attachmentCount;++n){auto& a=b->pAttachments[n];out<<" blend"<<n<<'='<<a.blendEnable<<','<<a.srcColorBlendFactor<<','<<a.dstColorBlendFactor<<','<<a.colorBlendOp<<','<<a.srcAlphaBlendFactor<<','<<a.dstAlphaBlendFactor<<','<<a.alphaBlendOp<<','<<a.colorWriteMask;}
  if(auto v=info.pViewportState){if(v->pViewports)for(unsigned n=0;n<v->viewportCount;++n){auto& a=v->pViewports[n];out<<" staticViewport"<<n<<'='<<a.x<<','<<a.y<<','<<a.width<<','<<a.height<<','<<a.minDepth<<','<<a.maxDepth;}if(v->pScissors)for(unsigned n=0;n<v->scissorCount;++n){auto& a=v->pScissors[n];out<<" staticScissor"<<n<<'='<<a.offset.x<<','<<a.offset.y<<','<<a.extent.width<<','<<a.extent.height;}}
  if(auto d=info.pDynamicState)for(unsigned n=0;n<d->dynamicStateCount;++n)out<<" dynamic="<<d->pDynamicStates[n];
  p.state=out.str();graphicsPipelines[pipeline]=std::move(p);
 }
 bool skyPipeline(VkPipeline pipeline){std::lock_guard<std::mutex> l(mutex);auto p=graphicsPipelines.find(pipeline);return p!=graphicsPipelines.end()&&p->second.sky;}
 void renderPass(VkRenderPass pass,const VkRenderPassCreateInfo& info){std::lock_guard<std::mutex> l(mutex);if(info.attachmentCount)capturePasses[pass].assign(info.pAttachments,info.pAttachments+info.attachmentCount);}
 void forgetPass(VkRenderPass pass){std::lock_guard<std::mutex> l(mutex);capturePasses.erase(pass);for(auto it=observedSkyPasses.begin();it!=observedSkyPasses.end();)if(it->first==pass)it=observedSkyPasses.erase(it);else ++it;}
 void framebuffer(VkFramebuffer fb,const VkFramebufferCreateInfo& info){std::lock_guard<std::mutex> l(mutex);if(info.attachmentCount)captureFramebuffers[fb].attachments.assign(info.pAttachments,info.pAttachments+info.attachmentCount);}
 void forgetFramebuffer(VkFramebuffer fb){std::lock_guard<std::mutex> l(mutex);captureFramebuffers.erase(fb);for(auto it=observedSkyPasses.begin();it!=observedSkyPasses.end();)if(it->second==fb)it=observedSkyPasses.erase(it);else ++it;}
 Ticket graphicsBegin(VkCommandBuffer cb,const VkRenderPassBeginInfo& info,bool eligible,bool stereo){
  std::lock_guard<std::mutex> l(mutex);auto& p=graphicsPasses[cb];p={};p.pass=info.renderPass;p.framebuffer=info.framebuffer;
  if(!graphicsActive||graphicsShots>=3||!observedSkyPasses.count({p.pass,p.framebuffer}))return {};
  if(passDecisions.emplace(p.pass,p.framebuffer,eligible,stereo).second)drawJournal+="SKY_PASS eligible="+std::to_string(eligible)+" stereo="+std::to_string(stereo)+" pass="+std::to_string(reinterpret_cast<uintptr_t>(p.pass))+" fb="+std::to_string(reinterpret_cast<uintptr_t>(p.framebuffer))+"\n";
  if(!eligible)return {};
  auto pass=capturePasses.find(p.pass);auto fb=captureFramebuffers.find(p.framebuffer);if(pass==capturePasses.end()||fb==captureFramebuffers.end())return {};
  auto t=graphicsTicket("sky-pass-"+std::to_string(++graphicsShots));VkDeviceSize total{};
  t->graphicsDetails="Scope=whole render pass; NOT an isolated draw. Set 100 denotes framebuffer attachments. Texture snapshots use base mip of the bound view.\npass="+std::to_string(reinterpret_cast<uintptr_t>(p.pass))+" framebuffer="+std::to_string(reinterpret_cast<uintptr_t>(p.framebuffer))+"\n";
  t->graphicsDetails+="renderArea="+std::to_string(info.renderArea.offset.x)+","+std::to_string(info.renderArea.offset.y)+","+std::to_string(info.renderArea.extent.width)+","+std::to_string(info.renderArea.extent.height)+"\n";
  for(unsigned n=0;n<std::min(pass->second.size(),fb->second.attachments.size());++n){auto& a=pass->second[n];auto view=fb->second.attachments[n];
   t->graphicsDetails+="attachment="+std::to_string(n)+" load="+std::to_string(a.loadOp)+" store="+std::to_string(a.storeOp)+" initial="+std::to_string(a.initialLayout)+" final="+std::to_string(a.finalLayout)+"\n";
   if(a.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD)appendImage(t,view,a.initialLayout,100,n,false,total);else t->skipped.push_back("attachment "+std::to_string(n)+" before: loadOp does not preserve contents");
   if(a.storeOp==VK_ATTACHMENT_STORE_OP_STORE)appendImage(t,view,a.finalLayout,100,n,true,total);else t->skipped.push_back("attachment "+std::to_string(n)+" after: storeOp does not preserve contents");
  }
  if(!stageGraphics(cb,t,total))return {};p.ticket=t;copies(cb,t,false);return t;
 }
 void graphicsNext(VkCommandBuffer cb){std::lock_guard<std::mutex> l(mutex);++graphicsPasses[cb].subpass;}
 void graphicsDraw(VkCommandBuffer cb,VkPipeline pipeline,const std::vector<BoundSet>& bound,const std::string& details,bool stereo){
  std::lock_guard<std::mutex> l(mutex);auto g=graphicsPipelines.find(pipeline);if(g==graphicsPipelines.end())return;auto it=graphicsPasses.find(cb);if(it==graphicsPasses.end())return;auto& p=it->second;
  if(g->second.sky)observedSkyPasses.insert({p.pass,p.framebuffer});
  if(!graphicsActive)return;
  ++p.draws;if(g->second.sky)++p.skyDraws;
  std::ostringstream row;row<<"cb="<<reinterpret_cast<uintptr_t>(cb)<<" pass="<<reinterpret_cast<uintptr_t>(p.pass)<<" fb="<<reinterpret_cast<uintptr_t>(p.framebuffer)<<" subpass="<<p.subpass<<" draw="<<p.draws<<" pipeline="<<reinterpret_cast<uintptr_t>(pipeline)<<" stereo="<<stereo<<" sky="<<g->second.sky<<' '<<g->second.state<<' '<<details<<'\n';
  auto& occurrence=drawOccurrences[{pipeline,p.pass}];++occurrence;
  if((occurrence<=4&&drawOccurrences.size()<=4096)||(g->second.sky&&occurrence<=128)){++graphicsDraws;drawJournal+=row.str();}
  if(p.ticket&&g->second.sky){p.ticket->graphicsDetails+=row.str();if(p.inputs.size()<4)p.inputs.push_back(bound);else p.ticket->skipped.push_back("additional sky draw inputs omitted (four-draw limit)");}
 }
 void graphicsEnd(VkCommandBuffer cb){
  std::lock_guard<std::mutex> l(mutex);auto found=graphicsPasses.find(cb);if(found==graphicsPasses.end())return;auto p=std::move(found->second);graphicsPasses.erase(found);if(!p.ticket)return;
  p.ticket->graphicsDetails+="actualSkyDraws="+std::to_string(p.skyDraws)+" totalRecordedDraws="+std::to_string(p.draws)+"\n";
  copies(cb,p.ticket,true);p.ticket->recordedAfter=true;
  unsigned draw{};for(const auto& bound:p.inputs){auto t=graphicsTicket(p.ticket->folder.filename().string()+"-inputs-"+std::to_string(++draw));t->projection=p.ticket->projection;t->frameSerial=p.ticket->frameSerial;
   t->graphicsDetails="Descriptors from actual sky draw; resource bytes copied AFTER whole render pass. Mutable inputs may have changed within pass. Bindless opacity texture is not copied; material constants are included.\n";VkDeviceSize total{};
   for(unsigned sn=0;sn<std::min<size_t>(3,bound.size());++sn){auto set=sets.find(bound[sn].set);if(set==sets.end())continue;for(auto& [key,b]:set->second.bindings){
    if(key.second)continue;
    if(b.buffer.buffer){auto buf=buffers.find(b.buffer.buffer);if(buf==buffers.end())continue;auto off=b.buffer.offset;if(b.dynamicIndex!=UINT32_MAX){if(b.dynamicIndex>=bound[sn].dynamic.size()){t->skipped.push_back("missing dynamic offset");continue;}off+=bound[sn].dynamic[b.dynamicIndex];}
     auto& info=buf->second;auto size=b.buffer.range==VK_WHOLE_SIZE?(off<=info.size?info.size-off:0):b.buffer.range;
     if(off>info.size||size>info.size-off||!size||size>1024*1024||size%4||off%4||!(info.usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)){t->skipped.push_back("buffer not copyable s"+std::to_string(sn)+" b"+std::to_string(key.first));continue;}
     Item i{};i.after=true;i.set=sn;i.binding=key.first;i.buffer=b.buffer.buffer;i.sourceOffset=off;i.offset=(total+15)&~VkDeviceSize(15);i.size=size;if(i.offset>batchLimit-size)continue;total=i.offset+size;t->items.push_back(i);
    }else if(b.image.imageView){bool attachment=false;auto view=views.find(b.image.imageView);auto fb=captureFramebuffers.find(p.framebuffer);if(view!=views.end()&&fb!=captureFramebuffers.end())for(auto av:fb->second.attachments){auto a=views.find(av);attachment|=a!=views.end()&&a->second.image==view->second.image;}
     if(attachment){t->skipped.push_back("input aliases render attachment; use pass snapshot");continue;}
     if((sn==2&&key.first==1)||(sn==0&&(key.first==8||key.first==10||key.first==11)))appendImage(t,b.image.imageView,b.image.imageLayout,sn,key.first,true,total);
    }
   }}
   if(stageGraphics(cb,t,total)){copies(cb,t,true);t->recordedAfter=true;}
  }
 }
