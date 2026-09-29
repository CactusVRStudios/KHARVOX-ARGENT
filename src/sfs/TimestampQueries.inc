// Keep the game's logical timestamp indices independent of multiview's N
// consecutive physical queries. Only the first timestamp is returned; Vulkan
// permits the other view's timestamp to be zero. Never wait for unused odd
// slots written outside a multiview pass.
VKAPI_ATTR VkResult VKAPI_CALL createQueryPool(VkDevice d,const VkQueryPoolCreateInfo* info,const VkAllocationCallbacks* a,VkQueryPool* out){RESULT_BEGIN
 auto copy=*info;const bool expanded=info->queryType==VK_QUERY_TYPE_TIMESTAMP||info->queryType==VK_QUERY_TYPE_OCCLUSION;
 if(expanded){if(info->queryCount>UINT32_MAX/2)return VK_ERROR_OUT_OF_HOST_MEMORY;copy.queryCount*=2;}
 auto r=FN(vkCreateQueryPool)(d,&copy,a,out);if(r==VK_SUCCESS){s->queryPools[*out]=info->queryType;note("query pool type="+std::to_string(info->queryType)+" logical="+std::to_string(info->queryCount)+" physical="+std::to_string(copy.queryCount));}return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyQueryPool(VkDevice d,VkQueryPool pool,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->queryPools.erase(pool);FN(vkDestroyQueryPool)(d,pool,a);}
bool queryType(State* s,VkQueryPool pool,VkQueryType type){auto i=s->queryPools.find(pool);return i!=s->queryPools.end()&&i->second==type;}
bool timestampPool(State* s,VkQueryPool pool){return queryType(s,pool,VK_QUERY_TYPE_TIMESTAMP);}
bool occlusionPool(State* s,VkQueryPool pool){return queryType(s,pool,VK_QUERY_TYPE_OCCLUSION);}
bool expandedPool(State* s,VkQueryPool pool){return timestampPool(s,pool)||occlusionPool(s,pool);}
VKAPI_ATTR void VKAPI_CALL beginQuery(VkCommandBuffer cb,VkQueryPool pool,uint32_t query,VkQueryControlFlags flags){COMMAND_BEGIN
 if(occlusionPool(s,pool)){query*=2;if(s->commands.at(cb).stereo&&s->stereoOcclusion.fetch_add(1,std::memory_order_relaxed)==0)note("MULTIVIEW_OCCLUSION isolated logical slots; CPU/GPU results sum both views");}
 FN(vkCmdBeginQuery)(cb,pool,query,flags);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL endQuery(VkCommandBuffer cb,VkQueryPool pool,uint32_t query){COMMAND_BEGIN
 const bool expanded=occlusionPool(s,pool);if(expanded)query*=2;
 FN(vkCmdEndQuery)(cb,pool,query);
 // A mono pass consumes one physical slot. Complete the other with an empty
 // query so both paths have identical layout and WAIT never targets an unused
 // odd slot. No CPU view-count metadata can race command-buffer submission.
 if(expanded&&!s->commands.at(cb).stereo){FN(vkCmdBeginQuery)(cb,pool,query+1,0);FN(vkCmdEndQuery)(cb,pool,query+1);}
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL resetQueryPool(VkDevice d,VkQueryPool pool,uint32_t first,uint32_t count){auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);if(expandedPool(s,pool)){first*=2;count*=2;}FN(vkResetQueryPool)(d,pool,first,count);}
VKAPI_ATTR void VKAPI_CALL resetQueryPoolEXT(VkDevice d,VkQueryPool pool,uint32_t first,uint32_t count){auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);if(expandedPool(s,pool)){first*=2;count*=2;}FN(vkResetQueryPoolEXT)(d,pool,first,count);}
VKAPI_ATTR void VKAPI_CALL resetQueryCommands(VkCommandBuffer cb,VkQueryPool pool,uint32_t first,uint32_t count){COMMAND_BEGIN
 if(expandedPool(s,pool)){first*=2;count*=2;}FN(vkCmdResetQueryPool)(cb,pool,first,count);
COMMAND_END}
void noteTimestamp(State* s,VkCommandBuffer cb){if(s->commands.at(cb).stereo&&s->stereoTimestamps.fetch_add(1,std::memory_order_relaxed)==0)note("MULTIVIEW_TIMESTAMP logical indices isolated: first stereo-pass timestamp observed");}
VKAPI_ATTR void VKAPI_CALL timestamp(VkCommandBuffer cb,VkPipelineStageFlagBits stage,VkQueryPool pool,uint32_t query){COMMAND_BEGIN
 if(timestampPool(s,pool)){query*=2;noteTimestamp(s,cb);}FN(vkCmdWriteTimestamp)(cb,stage,pool,query);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL timestamp2(VkCommandBuffer cb,VkPipelineStageFlags2 stage,VkQueryPool pool,uint32_t query){COMMAND_BEGIN
 if(timestampPool(s,pool)){query*=2;noteTimestamp(s,cb);}FN(vkCmdWriteTimestamp2)(cb,stage,pool,query);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL timestamp2KHR(VkCommandBuffer cb,VkPipelineStageFlags2 stage,VkQueryPool pool,uint32_t query){COMMAND_BEGIN
 if(timestampPool(s,pool)){query*=2;noteTimestamp(s,cb);}FN(vkCmdWriteTimestamp2KHR)(cb,stage,pool,query);
COMMAND_END}
VKAPI_ATTR VkResult VKAPI_CALL queryResults(VkDevice d,VkQueryPool pool,uint32_t first,uint32_t count,size_t size,void* data,VkDeviceSize stride,VkQueryResultFlags flags){
 auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);
 if(!expandedPool(s,pool))return FN(vkGetQueryPoolResults)(d,pool,first,count,size,data,stride,flags);
 const size_t bytes=((flags&VK_QUERY_RESULT_64_BIT)?8u:4u)*((flags&VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)?2u:1u);
 VkResult result=VK_SUCCESS;
 for(uint32_t i=0;i<count;++i){if(stride&&i>(SIZE_MAX/stride))return VK_ERROR_UNKNOWN;const auto offset=size_t(i*stride);if(offset>size||bytes>size-offset)return VK_ERROR_UNKNOWN;
  auto* output=static_cast<unsigned char*>(data)+offset;VkResult r{};
  if(occlusionPool(s,pool)){
   uint64_t raw[4]{};r=FN(vkGetQueryPoolResults)(d,pool,(first+i)*2,2,sizeof(raw),raw,16,flags|VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);if(r<0)return r;
   const uint64_t total=raw[0]+raw[2],available=uint64_t(raw[1]!=0&&raw[3]!=0);
   const size_t word=(flags&VK_QUERY_RESULT_64_BIT)?8:4;
   if(available||(flags&VK_QUERY_RESULT_PARTIAL_BIT))std::memcpy(output,&total,word);
   if(flags&VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)std::memcpy(output+word,&available,word);
  }else r=FN(vkGetQueryPoolResults)(d,pool,(first+i)*2,1,bytes,output,stride,flags);
  if(r<0)return r;if(r!=VK_SUCCESS)result=r;
 }return result;
}
VKAPI_ATTR void VKAPI_CALL copyQueryResults(VkCommandBuffer cb,VkQueryPool pool,uint32_t first,uint32_t count,VkBuffer dst,VkDeviceSize offset,VkDeviceSize stride,VkQueryResultFlags flags){COMMAND_BEGIN
 if(occlusionPool(s,pool)&&count){
  auto& command=s->commands.at(cb);const size_t slot=command.queryCopies++;
  if(command.queryScratch.size()<=slot)command.queryScratch.resize(slot+1);
  auto& scratch=command.queryScratch[slot];
  if(!scratch||scratch->capacity<count){auto next=std::make_unique<QueryResolveScratch>(*s->queryResolver);next->initialize(count);scratch=std::move(next);}
  scratch->copy(cb,pool,first,count,dst,offset,stride,flags);
  // Restore every compute binding and stage-specific push write disturbed by
  // the internal resolver. No application dispatch/descriptor rebind required.
  if(command.compute)FN(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,command.compute);
  std::vector<uint32_t> restore;
  for(uint32_t i=0;i<command.computeDescriptors.size();++i)if(command.computeDescriptors[i].set)restore.push_back(i);
  // Replay in original order: binding an incompatible layout can disturb
  // earlier sets. Index order alone could disturb a newer application binding.
  std::sort(restore.begin(),restore.end(),[&](uint32_t a,uint32_t b){return command.computeDescriptors[a].order<command.computeDescriptors[b].order;});
  for(auto i:restore){const auto& bound=command.computeDescriptors[i];FN(vkCmdBindDescriptorSets)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,bound.layout,i,1,&bound.set,uint32_t(bound.dynamic.size()),bound.dynamic.data());}
  command.pushes.replay([&](VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t start,uint32_t size,const void* value){FN(vkCmdPushConstants)(cb,layout,stages,start,size,value);});
  return;
 }
 if(!timestampPool(s,pool)){FN(vkCmdCopyQueryPoolResults)(cb,pool,first,count,dst,offset,stride,flags);return;}
 for(uint32_t i=0;i<count;++i)FN(vkCmdCopyQueryPoolResults)(cb,pool,(first+i)*2,1,dst,offset+i*stride,stride,flags);
COMMAND_END}
