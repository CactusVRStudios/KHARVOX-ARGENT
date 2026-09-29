#include "NativeSfs.h"
#include "../openxr/GameImageLifetime.h"
#include "../hands/HandSceneDepthTracker.h"
#include "../QuadRuntime.h"
#include "../PoolMembers.h"
#include "NativeDispatch.h"
#include "SourceRing.h"
#include "PushReplay.h"
#include "CommandCpuTiming.h"
#include "CommandCensus.h"
#include "../openxr/DiagnosticGpuTiming.h"
#include "CommandBindings.h"
#include "RecordingCache.h"
#include "DescriptorCountCache.h"
#include "UnlockedDriverScope.h"
#include "ShaderCompiler.h"
#include "PortableUiIdentity.h"
#include "QueryResolve.h"
#include "ShaderProfile.h"
#include "PipelineIdentity.h"
#include "WaterCaptureMetadata.h"
#include "WaterGpuCapture.h"
#include "../DeviceFaultReport.h"
#include "../Diagnostics.h"
#include "../GpuAddressTrace.h"
#include "WaterPipelineRobustness.h"
#include "MaterialPipelineCapture.h"

#include "StereoResources.h"





#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <type_traits>

namespace argent::sfs {
using namespace kharvox::sfs;
namespace handDepth=kharvox::hands;
namespace {
// Developer-only native multiview/OpenXR prototype. Headset validation and
// complete profile lighting corrections are required before a playable release.
void note(const std::string& text){argent::log("[SFS] "+text);}
uint64_t threadCpuUs(){FILETIME created{},exited{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return 0;
    const auto ticks=[](FILETIME value){return (uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
    return (ticks(kernel)+ticks(user))/10;
}
// Immutable for the lifetime of a pipeline. Copies in command-local state let
// pass replay and compute dispatch avoid the shared metadata registry.
struct PipelineSnapshot {
    VkPipeline stereo{};
    bool computeStereo{},sky{};
    std::array<VkPipeline,2> indirect{};
    uintptr_t monoMarker{},stereoMarker{},endMarker{};
    std::array<uintptr_t,2> indirectMarkers{},indirectEndMarkers{};
};
struct CommandState {
    std::unique_ptr<argent::perf::CommandCensus> census;
    argent::perf::GpuTiming<34> gpu;
    uint32_t family=UINT32_MAX;bool primary{},gpuSubmitted{};
    uint64_t gpuRecordFrame{},gpuSubmitFrame{},gpuSubmits{};VkQueue gpuQueue{};
    struct GpuPass {uint64_t pass{},pipelineFirst{},pipelineLast{},draws{};uint32_t width{},height{},begin{},end{},compute{},x{},y{},z{};};
    std::array<GpuPass,16> gpuPasses{};unsigned gpuPassCount{},gpuRegionsSeen{},gpuPoints{},gpuEnd{};int gpuActivePass=-1;
    bool stereo{},waterEligible{},captureGraphicsQueue{};
    VkPipeline compute{};
    VkPipeline graphics{};
    PipelineSnapshot graphicsInfo{},computeInfo{};
    struct Descriptor {VkPipelineLayout layout{};VkDescriptorSet set{};std::vector<uint32_t> dynamic;uint64_t order{};};
    uint64_t descriptorOrder{};
    std::vector<Descriptor> descriptors;
    std::vector<Descriptor> computeDescriptors;
    // Reused after beginCommand (the application has retired this CB). Each
    // recorded copy owns separate scratch, including across queues/recorders.
    std::vector<std::unique_ptr<QueryResolveScratch>> queryScratch;
    size_t queryCopies{};
    CommandBindings bindings;
    PushReplay pushes;
    std::vector<VkImageMemoryBarrier> imageBarriers;
    std::vector<VkImageSubresourceRange> clearRanges;
};
struct State : std::enable_shared_from_this<State> {
    PFN_vkGetDeviceProcAddr resolver{};float timestampPeriod{};std::vector<uint32_t> timestampBits;
    std::unordered_map<VkCommandPool,uint32_t> poolFamilies;
    argent::perf::CensusCollector census;
    inline static std::atomic<uint64_t> nextCacheId{1};
    const uint64_t cacheId=nextCacheId.fetch_add(1,std::memory_order_relaxed);
    std::atomic<uint64_t> commandRetirement{1};
    std::atomic<uint64_t> commandCacheMisses{},passCacheMisses{},framebufferCacheMisses{};
    std::atomic<uint64_t> passRetirement{1},framebufferRetirement{1};
    std::atomic<uint64_t> pipelineLayoutRetirement{1};
    std::atomic<uint64_t> pipelineRetirement{1},pipelineCacheMisses{};
    std::atomic<bool> loggedNativePipelineBind{};
    std::atomic<uint64_t> descriptorCacheMisses{};
    VkDevice device{};NativeDispatch dispatch;
    std::unique_ptr<SourceRing> sources;
    // Command buffers are externally synchronized by Vulkan callers. Parallel
    // recorders share resource metadata; only creation/retirement needs exclusivity.
    std::shared_mutex mutex;
    Images images;
    std::unique_ptr<WaterGpuCapture> waterCapture;
    argent::DeviceFaultReport deviceFault;
    bool waterRobustness{};
    std::vector<VkQueueFlags> queueFlags;
    std::unordered_map<VkSwapchainKHR,std::vector<VkImage>> swapchains;
    std::unique_ptr<QueryResolvePipeline> queryResolver;
    std::unordered_map<VkQueryPool,VkQueryType> queryPools;
    std::atomic<uint64_t> stereoTimestamps{};
    std::atomic<uint64_t> stereoOcclusion{};
    VkBuffer params{};VkDeviceMemory paramsMemory{};
    EyeUniforms pendingUniforms{};
    FramePose pendingPose{},renderPose{};
    std::unordered_map<VkImage,FramePose> imagePoses;
    std::unordered_map<VkImage,EyeUniforms> imageUniforms;
    Configuration configuration;
    std::unordered_map<VkDescriptorSetLayout,uint32_t> layoutBindings;
    std::unordered_map<VkPipelineLayout,uint32_t> pipelineBindings;
    std::unordered_map<VkPipelineLayout,std::vector<uint32_t>> pipelineDynamicCounts;
    EyeUniforms renderUniforms;
    uint64_t sourcePoseSamples{};
    bool pending{},completed{true},frameValid{};
    bool profileTiming{};
    uint64_t profiledFrames{},retireNs{},uploadNs{},maxRetireNs{};
    std::atomic<uint64_t> pipelineBuilds{},pipelineBuildWallNs{},pipelineBuildCpuUs{},pipelineBuildCpuSamples{},pipelineBuildCompileNs{},pipelineBuildDriverNs{},pipelineBuildLockNs{},pipelineBuildMaxNs{};
    std::unordered_map<VkShaderModule,std::vector<uint32_t>> shaders;
    std::unordered_map<std::string,VkShaderModule> compiled;
    std::unordered_map<uint64_t,uint64_t> uiAliases;
    std::unordered_map<VkRenderPass,VkRenderPass> passes;
    std::unordered_map<VkImageView,uint32_t> viewLayers;
    std::unordered_map<VkImageView,VkImageViewCreateInfo> viewInfos;
    std::unordered_map<VkImageView,std::array<VkImageView,2>> eyeViews;
    std::unordered_map<VkFramebuffer,bool> framebufferStereo;
    std::unordered_map<VkPipeline,VkPipeline> stereoPipelines;
    std::unordered_map<VkPipeline,bool> computeStereo;
    std::unordered_map<VkPipeline,std::array<VkPipeline,2>> indirectPipelines;
    std::unordered_map<VkFramebuffer,bool> mixedFramebuffers;
    std::atomic<uint64_t> indirectMono{},indirectStereo{},mixedPasses{};
    uint32_t mixedDiagnostics{};
    uint32_t materialDiagnostics{};
    uint32_t materialCaptureCount{};
    std::unordered_map<VkDescriptorSetLayout,uint32_t> dynamicCounts;
    std::unordered_map<VkDescriptorSet,uint32_t> setDynamicCounts;
    argent::PoolMembers<VkDescriptorPool,VkDescriptorSet> setPools;
    std::unordered_map<VkCommandBuffer,CommandState> commands;
    std::unordered_map<VkCommandBuffer,VkCommandPool> commandPools;
    std::filesystem::path profile;
    PFN_vkCmdSetCheckpointNV checkpoint{};
    PFN_vkGetQueueCheckpointDataNV checkpointData{};
    std::unordered_map<VkPipeline,uintptr_t> pipelineMarkers;
    std::unordered_map<VkPipeline,uintptr_t> dispatchEndMarkers;
    std::map<uintptr_t,std::string> markerLabels; // Retain labels until device destruction.
    std::map<std::string,uintptr_t> xrMarkers;
};
void recordPipelineBuild(State* s,const char* kind,const std::string& shaders,uint64_t startNs,uint64_t cpuStartUs,uint64_t lockNs,uint64_t compileNs,uint64_t driverNs){
    if(!startNs)return;
    const auto endNs=CommandCpuTiming::now(),wallNs=endNs-startNs;
    // GetThreadTimes has coarse granularity on Windows. A short build can
    // report more thread CPU time than elapsed wall time across one clock tick.
    const bool cpuMeasured=wallNs>=50000000;
    const auto cpuEndUs=cpuMeasured?threadCpuUs():0,cpuUs=cpuMeasured&&cpuEndUs>=cpuStartUs?cpuEndUs-cpuStartUs:0;
    s->pipelineBuilds.fetch_add(1,std::memory_order_relaxed);
    s->pipelineBuildWallNs.fetch_add(wallNs,std::memory_order_relaxed);
    if(cpuMeasured){s->pipelineBuildCpuUs.fetch_add(cpuUs,std::memory_order_relaxed);s->pipelineBuildCpuSamples.fetch_add(1,std::memory_order_relaxed);}
    s->pipelineBuildCompileNs.fetch_add(compileNs,std::memory_order_relaxed);
    s->pipelineBuildDriverNs.fetch_add(driverNs,std::memory_order_relaxed);
    s->pipelineBuildLockNs.fetch_add(lockNs,std::memory_order_relaxed);
    CommandCpuTiming::maximum(s->pipelineBuildMaxNs,wallNs);
    if(wallNs>=1000000)note("PIPELINE_BUILD kind="+std::string(kind)+" startUs="+std::to_string(startNs/1000)+" endUs="+std::to_string(endNs/1000)
        +" thread="+std::to_string(GetCurrentThreadId())+" wallMs="+std::to_string(double(wallNs)/1000000.)
        +" threadCpuMs="+(cpuMeasured?std::to_string(double(cpuUs)/1000.):"na")+" lockMs="+std::to_string(double(lockNs)/1000000.)
        +" compileMs="+std::to_string(double(compileNs)/1000000.)+" driverMs="+std::to_string(double(driverNs)/1000000.)+" shaders="+shaders);
}
std::mutex devicesMutex;
std::unordered_map<void*,std::shared_ptr<State>> devices;
std::atomic<uint64_t> deviceGeneration{1};
template<class T>void* dispatchKey(T handle){return handle?*reinterpret_cast<void**>(handle):nullptr;}
template<class T>State* state(T handle){
    // The registry owns State throughout the Vulkan device lifetime. Valid
    // device/command calls cannot overlap externally synchronized destruction.
    // Borrow on the hot path instead of contending on a shared_ptr refcount for
    // every recorded command. Generation is checked before dereferencing the
    // cached pointer; shutdown keeps one owner until its metadata lock is gone.
    struct Cache {void* key{};uint64_t generation{};State* state{};};
    thread_local Cache cache;
    const auto generation=deviceGeneration.load(std::memory_order_acquire);
    const auto key=dispatchKey(handle);
    if(cache.key==key&&cache.generation==generation){
        if(auto cached=cache.state){
            if constexpr(std::is_same_v<T,VkDevice>){if(cached->device==handle)return cached;}
            else return cached;
        }
    }
    std::lock_guard<std::mutex> lock(devicesMutex);
    if constexpr(std::is_same_v<T,VkDevice>){
        // The loader can replace a newly-created device's dispatch table after
        // our CreateDevice returns. Keep identity by the device handle as well.
        std::shared_ptr<State> found;
        for(const auto& entry:devices)if(entry.second->device==handle){found=entry.second;break;}
        if(found){devices[key]=found;cache={key,generation,found.get()};return found.get();}
        throw std::runtime_error("Unregistered SFS device");
    }
    auto it=devices.find(key);if(it==devices.end())throw std::runtime_error("SFS device not initialized handle="+std::to_string(reinterpret_cast<uintptr_t>(handle))+" dispatch="+std::to_string(reinterpret_cast<uintptr_t>(key)));cache={key,generation,it->second.get()};return it->second.get();
}
#define FN(name) NativeDispatch::require(s->dispatch.name,#name)
#define RESULT_BEGIN try {auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
#define RESULT_END }catch(const std::exception& e){note(std::string(__FUNCTION__)+": "+e.what());return VK_ERROR_INITIALIZATION_FAILED;}
// Command hooks cannot return VkResult. Fail the owned diagnostic process on an
// unsupported command rather than record an invalid command or wait forever.
[[noreturn]] void commandFailure(const char* message){note(message);RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
#define HOOK_BUCKET(path) static CommandCpuTiming::Bucket bucket{__FUNCTION__,path};static const bool registered=(CommandCpuTiming::registerBucket(bucket),true);CommandCpuTiming timing(&bucket);
#define COMMAND_BEGIN try {HOOK_BUCKET("metadata") auto s=state(cb);timing.resolved();std::shared_lock<std::shared_mutex> lock(s->mutex);timing.acquired();
#define COMMAND_END }catch(const std::exception& e){const auto message=std::string(__FUNCTION__)+": "+e.what();commandFailure(message.c_str());}
RecordingCache<VkCommandBuffer,CommandState>& recordingCache(){
    thread_local RecordingCache<VkCommandBuffer,CommandState> cache;
    return cache;
}
// Caller already owns State::mutex. Never reacquire shared_mutex on a cache miss.
CommandState& commandUnderLock(State* s,VkCommandBuffer cb){
    return recordingCache().get(s->cacheId,s->commandRetirement.load(std::memory_order_acquire),cb,[&]() -> CommandState& {
        if(s->profileTiming)s->commandCacheMisses.fetch_add(1,std::memory_order_relaxed);
        return s->commands.at(cb);
    });
}
CommandState& localCommand(State* s,VkCommandBuffer cb){
    return recordingCache().get(s->cacheId,s->commandRetirement.load(std::memory_order_acquire),cb,[&]() -> CommandState& {
        if(s->profileTiming)s->commandCacheMisses.fetch_add(1,std::memory_order_relaxed);
        std::shared_lock<std::shared_mutex> lock(s->mutex);return s->commands.at(cb);
    });
}
// Only command-owned data and immutable downstream dispatch may use this path.
// Shared metadata must be copied under a lock before using a worker cache;
// lifetime generations invalidate those copies when resources are retired.
#define LOCAL_COMMAND_BEGIN try {HOOK_BUCKET("local") auto s=state(cb);timing.resolved();auto& local=localCommand(s,cb);timing.acquired();


void registerMarker(State* s,VkPipeline pipeline,std::string label){
 const auto id=s->markerLabels.size()+1;s->markerLabels.emplace(id,label);s->pipelineMarkers[pipeline]=id;
 if(label.rfind("compute ",0)==0){s->markerLabels.emplace(id+1,"after dispatch: "+label);s->dispatchEndMarkers[pipeline]=id+1;}
}

#include "TimestampQueries.inc"

VkShaderModule compiledModule(State* s,VkShaderModule original,uint64_t variant,bool& stereoCompute,uint32_t projectionBinding,bool skipProjection=false,int indirectEye=-1,bool monoView=false,uint64_t* compileNs=nullptr){
    const auto& words=s->shaders.at(original);
    const auto keyHash=profileHash(words.data(),uint32_t(words.size()*4));
    spirv_cross::Compiler inspect(words);auto model=inspect.get_execution_model();
    stereoCompute=model==spv::ExecutionModelGLCompute&&s->configuration.stereoComputeShaders.count(keyHash)!=0;
    if(model==spv::ExecutionModelGLCompute&&s->configuration.imageComputeStereo){
      const auto resources=inspect.get_shader_resources();bool writes=false,sharedWrites=false;
      for(auto& r:resources.storage_images){auto t=inspect.get_type(r.type_id);if(t.image.dim==spv::Dim2D&&!t.image.arrayed&&!inspect.has_decoration(r.id,spv::DecorationNonWritable))writes=true;}
      for(auto& r:resources.storage_buffers)if(!inspect.has_decoration(r.id,spv::DecorationNonWritable)&&!inspect.get_buffer_block_flags(r.id).get(spv::DecorationNonWritable))sharedWrites=true;
      stereoCompute=stereoCompute||(writes&&!sharedWrites);
    }
    ShaderOptions options;options.set=0;options.binding=projectionBinding;
    const auto vk3d=s->configuration.eternalVk3d?eternalVk3dRule(keyHash):nullptr;
    if(vk3d)options.vk3dShader=keyHash;
    if(s->configuration.eternalVolumes&&eternalVolumeRule(keyHash).uv)options.volumeShader=keyHash;
    if(s->configuration.eternalLightGrids&&model==spv::ExecutionModelFragment&&eternalLightGridRule(keyHash).pixels)options.lightGridShader=keyHash;
    options.screenSpaceUi=model==spv::ExecutionModelVertex&&s->configuration.screenUiShaders.count(keyHash)!=0;
    options.uiShader=options.screenSpaceUi?keyHash:0;
    if(model==spv::ExecutionModelVertex&&!options.screenSpaceUi&&!s->configuration.screenUiShaders.empty()){
        auto cached=s->uiAliases.find(keyHash);
        if(cached==s->uiAliases.end())cached=s->uiAliases.emplace(keyHash,portableUiProfile(words)).first;
        if(cached->second&&s->configuration.screenUiShaders.count(cached->second)){
            options.screenSpaceUi=true;options.uiShader=cached->second;
        }
    }
    options.project=!skipProjection&&model==spv::ExecutionModelVertex&&(s->configuration.projectionShaders.count(keyHash)!=0||options.screenSpaceUi);
    if((options.project||options.volumeShader||options.lightGridShader||(vk3d&&vk3d->worldUniform))&&projectionBinding==UINT32_MAX)throw std::runtime_error("Projected shader has no descriptor set zero");
    options.broadcastStorageImages=s->configuration.broadcastComputeShaders.count(keyHash)!=0;
    options.computeStereo=stereoCompute;options.indirectEye=indirectEye;options.monoView=monoView;
    const auto key=shaderKey(keyHash)+(options.project?"_project":"_flat")+"_binding"+std::to_string(projectionBinding)+"_"+std::to_string(indirectEye)+(monoView?"_mono":"");
    auto cached=s->compiled.find(key);if(cached!=s->compiled.end())return cached->second;
    const auto compileStart=s->profileTiming?CommandCpuTiming::now():0;
    auto shader=compileStereoShader(words,options);
    if(options.screenSpaceUi&&!monoView)note("UI_SHADER_PROFILE shader="+shaderKey(keyHash)+" profile="+shaderKey(options.uiShader)+" metadataAlias="+std::to_string(keyHash!=options.uiShader));
    if(argent::extendedLogging()&&s->configuration.eternalVk3d&&model==spv::ExecutionModelVertex&&!monoView&&!skipProjection&&!options.project&&!vk3d)
        note("SHADER_PROFILE_UNCLASSIFIED shader="+shaderKey(keyHash)+" stage=vertex nativeProjection=retained; may be shadow/fullscreen or an unknown variant");
    if(options.project&&!monoView)note("WORLD_PROJECTION shader="+shaderKey(keyHash));
    if(s->configuration.stereoComputeShaders.count(keyHash))note("COMPUTE_EYE_POLICY shader="+shaderKey(keyHash)+" stereo="+std::to_string(stereoCompute)+" fixedEye="+std::to_string(indirectEye));
    if(vk3d)note("VK3D_PROFILE shader="+shaderKey(keyHash)+" edits="+std::to_string(vk3d->edits.size())+" sharedCompute="+std::to_string(model==spv::ExecutionModelGLCompute&&!stereoCompute));
    if(options.vk3dShader==0x24abb0e76a065289ull)note("WATER_SAMPLE_LAYER native=1 integerFetchGuard=1 eye="+std::to_string(indirectEye));
    if(options.volumeShader)note("center-volume correction shader="+shaderKey(keyHash)+" stage="+std::to_string(model)+" eye="+std::to_string(indirectEye));
    if(options.lightGridShader&&!monoView)note("center-light-grid correction shader="+shaderKey(keyHash));
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=shader.size()*4;info.pCode=shader.data();VkShaderModule result{};
    if(FN(vkCreateShaderModule)(s->device,&info,nullptr,&result)!=VK_SUCCESS)throw std::runtime_error("Stereo shader creation failed");
    s->compiled.emplace(key,result);if(s->compiled.size()==1||s->compiled.size()%50==0)note("compiled stereo variants="+std::to_string(s->compiled.size()));
    if(compileStart){const auto end=CommandCpuTiming::now();if(compileNs)*compileNs+=end-compileStart;
        note("SHADER_COMPILE startUs="+std::to_string(compileStart/1000)+" endUs="+std::to_string(end/1000)+" thread="+std::to_string(GetCurrentThreadId())+" key="+key+" wallMs="+std::to_string(double(end-compileStart)/1000000.));}
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createShader(VkDevice d,const VkShaderModuleCreateInfo* i,const VkAllocationCallbacks* a,VkShaderModule* out){RESULT_BEGIN
    auto r=FN(vkCreateShaderModule)(d,i,a,out);if(r==VK_SUCCESS)s->shaders[*out]={i->pCode,i->pCode+i->codeSize/4};return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice d,VkShaderModule shader,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->shaders.erase(shader);FN(vkDestroyShaderModule)(d,shader,a);}
VKAPI_ATTR VkResult VKAPI_CALL captureCreateBuffer(VkDevice d,const VkBufferCreateInfo* i,const VkAllocationCallbacks* a,VkBuffer* out){auto s=state(d);auto info=*i;if(s->waterCapture&&(info.usage&(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)))info.usage|=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;auto r=FN(vkCreateBuffer)(d,&info,a,out);if(r==VK_SUCCESS&&s->waterCapture)s->waterCapture->buffer(*out,info);return r;}
VKAPI_ATTR void VKAPI_CALL captureDestroyBuffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks* a){auto s=state(d);if(s->waterCapture)s->waterCapture->forgetBuffer(b);FN(vkDestroyBuffer)(d,b,a);}
VKAPI_ATTR void VKAPI_CALL captureUpdateSets(VkDevice d,uint32_t n,const VkWriteDescriptorSet* writes,uint32_t count,const VkCopyDescriptorSet* copies){auto s=state(d);FN(vkUpdateDescriptorSets)(d,n,writes,count,copies);if(s->waterCapture)s->waterCapture->update(n,writes,count,copies);}
VKAPI_ATTR VkResult VKAPI_CALL createImage(VkDevice d,const VkImageCreateInfo* i,const VkAllocationCallbacks* a,VkImage* out){RESULT_BEGIN
    auto info=*i;
    if(s->waterCapture&&(info.usage&(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))&&!(info.usage&VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT))info.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if(stereoImage(info)&&(info.usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))info.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto result=s->images.create(d,info,a,out,FN(vkCreateImage));if(result==VK_SUCCESS){handDepth::handSceneImageCreated(*out,info);waterCaptureImage(d,*out,imageInfo(info));if(s->waterCapture)s->waterCapture->image(*out,imageInfo(info));}return result;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyImage(VkDevice d,VkImage image,const VkAllocationCallbacks* a){
 kharvox::gameImageLifetime().retire(kharvox::GameImageLifetime::key(image),[&]{
  auto s=state(d);if(s->waterCapture)s->waterCapture->forgetImage(image);handDepth::handSceneImageDestroyed(image);s->images.destroy(d,image,a,FN(vkDestroyImage));
 });
}
VKAPI_ATTR VkResult VKAPI_CALL createView(VkDevice d,const VkImageViewCreateInfo* i,const VkAllocationCallbacks* a,VkImageView* out){RESULT_BEGIN
    auto info=s->images.shaderViewInfo(*i);auto r=FN(vkCreateImageView)(d,&info,a,out);if(r==VK_SUCCESS){handDepth::handSceneImageViewCreated(*out,info);waterCaptureView(d,*out,info);if(s->waterCapture)s->waterCapture->view(*out,info);s->viewLayers[*out]=(s->images.layers(i->image)==2&&info.subresourceRange.baseArrayLayer==0&&info.subresourceRange.layerCount>=2)?2:1;if(!info.pNext)s->viewInfos[*out]=info;}return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyView(VkDevice d,VkImageView view,const VkAllocationCallbacks* a){
 kharvox::gameImageLifetime().retire(kharvox::GameImageLifetime::key(view),[&]{
  auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);auto eyes=s->eyeViews.find(view);if(eyes!=s->eyeViews.end()){for(auto eye:eyes->second)if(eye)FN(vkDestroyImageView)(d,eye,nullptr);s->eyeViews.erase(eyes);}handDepth::handSceneImageViewDestroyed(view);if(s->waterCapture)s->waterCapture->forgetView(view);s->viewInfos.erase(view);s->viewLayers.erase(view);FN(vkDestroyImageView)(d,view,a);
 });
}
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice d,const VkRenderPassCreateInfo* i,const VkAllocationCallbacks* a,VkRenderPass* out){RESULT_BEGIN
    auto input=*i;std::vector<VkAttachmentDescription> attachments;
    if(s->sources&&i->attachmentCount){attachments.assign(i->pAttachments,i->pAttachments+i->attachmentCount);for(auto& attachment:attachments){
        if(attachment.initialLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)attachment.initialLayout=VK_IMAGE_LAYOUT_GENERAL;
        if(attachment.finalLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)attachment.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    }input.pAttachments=attachments.data();}
    RenderPassPlan plan(input,true);if(!plan.valid())return VK_ERROR_FEATURE_NOT_PRESENT;
    auto r=FN(vkCreateRenderPass)(d,&input,a,out);if(r!=VK_SUCCESS)return r;VkRenderPass stereo{};r=FN(vkCreateRenderPass)(d,&plan.info(),a,&stereo);
    if(r!=VK_SUCCESS){FN(vkDestroyRenderPass)(d,*out,a);*out=VK_NULL_HANDLE;return r;}s->passes[*out]=stereo;handDepth::handSceneRenderPassCreated(*out,input);if(s->waterCapture)s->waterCapture->renderPass(*out,input);return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice d,VkRenderPass pass,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->passRetirement.fetch_add(1,std::memory_order_release);handDepth::handSceneRenderPassDestroyed(pass);if(s->waterCapture)s->waterCapture->forgetPass(pass);auto it=s->passes.find(pass);if(it!=s->passes.end()){FN(vkDestroyRenderPass)(d,it->second,a);s->passes.erase(it);}FN(vkDestroyRenderPass)(d,pass,a);}
VKAPI_ATTR VkResult VKAPI_CALL createFramebuffer(VkDevice d,const VkFramebufferCreateInfo* i,const VkAllocationCallbacks* a,VkFramebuffer* out){RESULT_BEGIN
    auto info=*i;bool stereo=i->attachmentCount!=0;
    if(i->flags&VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT)return VK_ERROR_FEATURE_NOT_PRESENT;
    for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewLayers.find(i->pAttachments[j]);if(v==s->viewLayers.end()||v->second<2)stereo=false;}
    uint32_t stereoAttachments{};for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewLayers.find(i->pAttachments[j]);if(v!=s->viewLayers.end()&&v->second>=2)++stereoAttachments;}
    const bool mixed=stereoAttachments&&stereoAttachments<i->attachmentCount;
    auto pass=s->passes.find(i->renderPass);if(pass==s->passes.end())return VK_ERROR_INITIALIZATION_FAILED;
    if(stereo)info.renderPass=pass->second;auto r=FN(vkCreateFramebuffer)(d,&info,a,out);if(r==VK_SUCCESS){
        handDepth::handSceneFramebufferCreated(*out,*i);if(s->waterCapture)s->waterCapture->framebuffer(*out,*i);s->framebufferStereo[*out]=stereo;s->mixedFramebuffers[*out]=mixed;
        if(mixed&&s->mixedDiagnostics++<24){
            note("[SFS-MIXED] framebuffer="+std::to_string(reinterpret_cast<uintptr_t>(*out))+" size="+std::to_string(i->width)+"x"+std::to_string(i->height)+" stereoAttachments="+std::to_string(stereoAttachments)+" total="+std::to_string(i->attachmentCount)+" mode=mono");
            for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewInfos.find(i->pAttachments[j]);if(v!=s->viewInfos.end())note("[SFS-MIXED] attachment="+std::to_string(j)+" image="+std::to_string(reinterpret_cast<uintptr_t>(v->second.image))+" format="+std::to_string(v->second.format)+" baseLayer="+std::to_string(v->second.subresourceRange.baseArrayLayer)+" viewLayers="+std::to_string(v->second.subresourceRange.layerCount));}
        }
    }return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyFramebuffer(VkDevice d,VkFramebuffer fb,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->framebufferRetirement.fetch_add(1,std::memory_order_release);handDepth::handSceneFramebufferDestroyed(fb);if(s->waterCapture)s->waterCapture->forgetFramebuffer(fb);s->framebufferStereo.erase(fb);s->mixedFramebuffers.erase(fb);FN(vkDestroyFramebuffer)(d,fb,a);}
VKAPI_ATTR VkResult VKAPI_CALL createLayout(VkDevice d,const VkDescriptorSetLayoutCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){RESULT_BEGIN
    std::vector<VkDescriptorSetLayoutBinding> bindings;if(i->bindingCount)bindings.assign(i->pBindings,i->pBindings+i->bindingCount);uint32_t dynamic=0;
    for(const auto& b:bindings){if(b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||b.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)dynamic+=b.descriptorCount;}
    if(i->pNext)throw std::runtime_error("SFS descriptor layout extension chain not supported by probe");
    uint32_t reserved=0;
    while(std::any_of(bindings.begin(),bindings.end(),[&](auto& b){return b.binding==reserved;})){if(reserved==UINT32_MAX)throw std::runtime_error("No free projection binding");++reserved;}
    for(uint32_t binding:{reserved})bindings.push_back({binding,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,nullptr});auto info=*i;info.bindingCount=uint32_t(bindings.size());info.pBindings=bindings.data();auto r=FN(vkCreateDescriptorSetLayout)(d,&info,a,out);if(r==VK_SUCCESS){s->dynamicCounts[*out]=dynamic;s->layoutBindings[*out]=reserved;if(s->waterCapture)s->waterCapture->layout(*out,*i);}return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice d,VkDescriptorSetLayout layout,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->dynamicCounts.erase(layout);s->layoutBindings.erase(layout);if(s->waterCapture)s->waterCapture->forgetLayout(layout);FN(vkDestroyDescriptorSetLayout)(d,layout,a);}
VKAPI_ATTR VkResult VKAPI_CALL createPipelineLayout(VkDevice d,const VkPipelineLayoutCreateInfo* i,const VkAllocationCallbacks* a,VkPipelineLayout* out){RESULT_BEGIN
    auto r=FN(vkCreatePipelineLayout)(d,i,a,out);if(r==VK_SUCCESS){s->pipelineBindings[*out]=i->setLayoutCount?s->layoutBindings.at(i->pSetLayouts[0]):UINT32_MAX;auto& counts=s->pipelineDynamicCounts[*out];for(uint32_t j=0;j<i->setLayoutCount;++j)counts.push_back(s->dynamicCounts.at(i->pSetLayouts[j]));}return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPipelineLayout(VkDevice d,VkPipelineLayout layout,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->pipelineLayoutRetirement.fetch_add(1,std::memory_order_release);s->pipelineDynamicCounts.erase(layout);s->pipelineBindings.erase(layout);FN(vkDestroyPipelineLayout)(d,layout,a);}
VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice d,const VkDescriptorPoolCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorPool* out){RESULT_BEGIN
    if(i->maxSets>UINT32_MAX/2)return VK_ERROR_OUT_OF_HOST_MEMORY;
    std::vector<VkDescriptorPoolSize> sizes;if(i->poolSizeCount)sizes.assign(i->pPoolSizes,i->pPoolSizes+i->poolSizeCount);sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,i->maxSets});auto info=*i;info.poolSizeCount=uint32_t(sizes.size());info.pPoolSizes=sizes.data();return FN(vkCreateDescriptorPool)(d,&info,a,out);
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL allocateSets(VkDevice d,const VkDescriptorSetAllocateInfo* i,VkDescriptorSet* out){RESULT_BEGIN
    auto r=FN(vkAllocateDescriptorSets)(d,i,out);if(r!=VK_SUCCESS)return r;
    if(s->waterCapture)s->waterCapture->allocateSets(*i,out);
    VkDescriptorBufferInfo buffer{s->params,0,sizeof(EyeUniforms)};
    std::vector<VkWriteDescriptorSet> writes(size_t(i->descriptorSetCount));
    for(uint32_t j=0;j<i->descriptorSetCount;++j){s->setPools.insert(i->descriptorPool,out[j]);s->setDynamicCounts[out[j]]=s->dynamicCounts.at(i->pSetLayouts[j]);for(uint32_t k=0;k<1;++k){auto& w=writes[size_t(j)];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=out[j];w.dstBinding=s->layoutBindings.at(i->pSetLayouts[j]);w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&buffer;}}
    FN(vkUpdateDescriptorSets)(d,uint32_t(writes.size()),writes.data(),0,nullptr);return VK_SUCCESS;
RESULT_END}
void forgetPool(State* s,VkDescriptorPool pool){
    if(s->waterCapture)s->waterCapture->pool(pool);
    s->setPools.retire(pool,[&](VkDescriptorSet set){s->setDynamicCounts.erase(set);});
}
VKAPI_ATTR VkResult VKAPI_CALL freeSets(VkDevice d,VkDescriptorPool pool,uint32_t count,const VkDescriptorSet* sets){RESULT_BEGIN
    auto r=FN(vkFreeDescriptorSets)(d,pool,count,sets);if(r==VK_SUCCESS){if(s->waterCapture)s->waterCapture->freeSets(count,sets);for(uint32_t i=0;i<count;++i){s->setDynamicCounts.erase(sets[i]);s->setPools.erase(sets[i]);}}return r;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL resetPool(VkDevice d,VkDescriptorPool pool,VkDescriptorPoolResetFlags flags){RESULT_BEGIN
    auto r=FN(vkResetDescriptorPool)(d,pool,flags);if(r==VK_SUCCESS)forgetPool(s,pool);return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice d,VkDescriptorPool pool,const VkAllocationCallbacks* allocator){
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);forgetPool(s,pool);FN(vkDestroyDescriptorPool)(d,pool,allocator);
}
VKAPI_ATTR VkResult VKAPI_CALL graphics(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo* infos,const VkAllocationCallbacks* a,VkPipeline* out){try {const auto requestNs=argent::cleanRelease?0:CommandCpuTiming::now();auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);const auto batchLockNs=argent::cleanRelease?0:CommandCpuTiming::now()-requestNs;
    for(uint32_t j=0;j<count;++j)out[j]=VK_NULL_HANDLE;
    for(uint32_t j=0;j<count;++j){const auto buildStart=s->profileTiming?(j==0?requestNs:CommandCpuTiming::now()):0;const auto cpuStart=buildStart?threadCpuUs():0;uint64_t compileNs{},driverNs{};std::string shaderHashes;
        auto info=infos[j];std::vector<VkPipelineShaderStageCreateInfo> stages(info.pStages,info.pStages+info.stageCount);const auto seed=pipelineSeed(info);
        // Correlate transparent material state with exact profile variants.
        // Creation-only and bounded: no string formatting on draw/record paths.
        if(false&&s->materialDiagnostics<512
            &&info.pColorBlendState&&info.pColorBlendState->attachmentCount){
            bool blended=false;
            for(uint32_t k=0;k<info.pColorBlendState->attachmentCount;++k)
                blended|=info.pColorBlendState->pAttachments[k].blendEnable!=0;
            if(blended){
                ++s->materialDiagnostics;
                std::string signature;
                for(const auto& stage:stages){const auto& code=s->shaders.at(stage.module);
                    signature+=" stage"+std::to_string(stage.stage)+"="+shaderKey(profileHash(code.data(),uint32_t(code.size()*4)))
                        +"_"+shaderKey(profileHash(code.data(),uint32_t(code.size()*4),seed));}
                const auto* depth=info.pDepthStencilState;
                note("[SFS-MATERIAL] translucent subpass="+std::to_string(info.subpass)
                    +" depthTest="+std::to_string(depth?depth->depthTestEnable:0)
                    +" depthWrite="+std::to_string(depth?depth->depthWriteEnable:0)
                    +" depthCompare="+std::to_string(depth?depth->depthCompareOp:0)+signature);
                for(uint32_t k=0;k<info.pColorBlendState->attachmentCount;++k){const auto& b=info.pColorBlendState->pAttachments[k];
                    note("[SFS-MATERIAL] attachment="+std::to_string(k)+" blend="+std::to_string(b.blendEnable)
                        +" src="+std::to_string(b.srcColorBlendFactor)+" dst="+std::to_string(b.dstColorBlendFactor)
                        +" op="+std::to_string(b.colorBlendOp)+" mask="+std::to_string(b.colorWriteMask));}
            }
        }
        const bool skipProjection=s->configuration.projectDepthOnly&&(!info.pDepthStencilState||!info.pDepthStencilState->depthTestEnable);
        auto monoStages=stages;bool captureMaterial=false;
        for(size_t k=0;k<stages.size();++k){auto& stage=stages[k];const auto original=stage.module;
            const auto& code=s->shaders.at(original);const auto variant=profileHash(code.data(),uint32_t(code.size()*4),seed);bool compute{};
            captureMaterial|=argent::gpuCrashDiagnostics()&&stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT&&profileHash(code.data(),uint32_t(code.size()*4))==0xd300c0135fbca8b0ull;
            if(buildStart){if(k)shaderHashes+=',';shaderHashes+=std::to_string(stage.stage)+":"+shaderKey(profileHash(code.data(),uint32_t(code.size()*4)));}
            stage.module=compiledModule(s,original,variant,compute,s->pipelineBindings.at(info.layout),skipProjection,-1,false,&compileNs);
            monoStages[k].module=compiledModule(s,original,variant,compute,s->pipelineBindings.at(info.layout),skipProjection,-1,true,&compileNs);
        }
        if(captureMaterial&&s->materialCaptureCount++>=128){
            if(s->materialCaptureCount==129)note("MATERIAL_PIPELINE_CAPTURE skipped=128-pipeline-limit");
            captureMaterial=false;
        }
        info.pStages=monoStages.data();
        // Derivative batch indices must not escape their original batch.
        if(info.flags&VK_PIPELINE_CREATE_DERIVATIVE_BIT)throw std::runtime_error("SFS derivative pipelines need batch remapping");
        const auto stereoPass=s->passes.at(info.renderPass);VkPipeline stereo{};
        {
         UnlockedDriverScope unlocked(lock);
         MaterialPipelineCapture materialCache(d,s->resolver,captureMaterial);
         if(captureMaterial&&!materialCache.active())note("MATERIAL_PIPELINE_CAPTURE unavailable");
         const auto started=s->profileTiming?CommandCpuTiming::now():0;
         auto r=FN(vkCreateGraphicsPipelines)(d,materialCache.cache(cache),1,&info,a,&out[j]);if(r!=VK_SUCCESS){note("PIPELINE_CREATE_FAILED kind=graphics variant=mono result="+std::to_string(r)+" shaders="+shaderHashes);return r;}
         info.pStages=stages.data();info.renderPass=stereoPass;
         r=FN(vkCreateGraphicsPipelines)(d,materialCache.cache(cache),1,&info,a,&stereo);
         if(r!=VK_SUCCESS){note("PIPELINE_CREATE_FAILED kind=graphics variant=stereo result="+std::to_string(r)+" shaders="+shaderHashes);FN(vkDestroyPipeline)(d,out[j],a);out[j]=VK_NULL_HANDLE;return r;}
         if(materialCache.active())try{
          const auto data=materialCache.data();wchar_t path[32768]{};const auto length=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
          if(!length||length>=32768)throw std::runtime_error("ARGENT_LOG unavailable");
          const auto directory=std::filesystem::path(std::wstring(path)+L".pipelines");std::filesystem::create_directories(directory);
          const auto filename="material-"+std::to_string(reinterpret_cast<uint64_t>(stereo))+".bin";
          std::ofstream file(directory/filename,std::ios::binary);file.write(data.data(),data.size());file.close();
          if(!file)throw std::runtime_error("Cannot save material pipeline cache");
          note("MATERIAL_PIPELINE_CAPTURE mono="+std::to_string(reinterpret_cast<uint64_t>(out[j]))+" stereo="+std::to_string(reinterpret_cast<uint64_t>(stereo))+" bytes="+std::to_string(data.size())+" file="+filename);
         }catch(const std::exception& e){note(std::string("MATERIAL_PIPELINE_CAPTURE failed=")+e.what());}
         if(started){driverNs=CommandCpuTiming::now()-started;const auto ms=double(driverNs)/1000000.;if(ms>=2)note("pipeline driver type=graphics variants=2 wallMs="+std::to_string(ms));}
        }
        if(s->waterCapture){std::vector<std::pair<uint32_t,uint64_t>> hashes;for(uint32_t k=0;k<infos[j].stageCount;++k){const auto& stage=infos[j].pStages[k];const auto& code=s->shaders.at(stage.module);hashes.push_back({stage.stage,profileHash(code.data(),uint32_t(code.size()*4))});}s->waterCapture->graphicsPipeline(out[j],infos[j],hashes);}
        handDepth::handSceneGraphicsPipelinesCreated(1,&infos[j],&out[j]);s->stereoPipelines[out[j]]=stereo;
        if(s->checkpoint){std::string label="graphics";for(uint32_t k=0;k<infos[j].stageCount;++k){const auto& stage=infos[j].pStages[k];const auto& code=s->shaders.at(stage.module);label+=" stage"+std::to_string(stage.stage)+"="+shaderKey(profileHash(code.data(),uint32_t(code.size()*4)));}registerMarker(s,out[j],label+" pipeline="+std::to_string(reinterpret_cast<uint64_t>(out[j])));registerMarker(s,stereo,label+" multiview pipeline="+std::to_string(reinterpret_cast<uint64_t>(stereo)));}
        if(argent::perf::enabled())note("PERF_PIPELINE kind=graphics pipeline="+std::to_string(reinterpret_cast<uint64_t>(out[j]))+" shaders="+shaderHashes);
        recordPipelineBuild(s,"graphics",shaderHashes,buildStart,cpuStart,j==0?batchLockNs:0,compileNs,driverNs);
    }return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL compute(VkDevice d,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo* infos,const VkAllocationCallbacks* a,VkPipeline* out){try {const auto requestNs=argent::cleanRelease?0:CommandCpuTiming::now();auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);const auto batchLockNs=argent::cleanRelease?0:CommandCpuTiming::now()-requestNs;
    for(uint32_t j=0;j<count;++j)out[j]=VK_NULL_HANDLE;
    for(uint32_t j=0;j<count;++j){
        const auto buildStart=s->profileTiming?(j==0?requestNs:CommandCpuTiming::now()):0;const auto cpuStart=buildStart?threadCpuUs():0;uint64_t compileNs{},driverNs{};
        auto info=infos[j];const auto original=info.stage.module;bool stereo{};
        if(info.flags&VK_PIPELINE_CREATE_DERIVATIVE_BIT)return VK_ERROR_FEATURE_NOT_PRESENT;
        const auto& originalCode=s->shaders.at(original);const auto profile=profileHash(originalCode.data(),uint32_t(originalCode.size()*4));const auto shaderHash=buildStart?shaderKey(profile):std::string{};
        VkPipelineRobustnessCreateInfoEXT waterRobustness{};
        if(protectWaterPipeline(profile,s->waterRobustness,info,waterRobustness))note("WATER_ROBUSTNESS shader="+shaderKey(profile)+" buffers=robust2 images=robust2");
        info.stage.module=compiledModule(s,original,0,stereo,s->pipelineBindings.at(info.layout),false,-1,false,&compileNs);
        const auto label=s->checkpoint?"compute shader="+shaderKey(profileHash(s->shaders.at(original).data(),uint32_t(s->shaders.at(original).size()*4))):std::string{};
        const bool indirect=stereo;
        std::array<VkShaderModule,2> modules{};
        if(indirect)for(int eye=0;eye<2;++eye){bool ignored{};modules[eye]=compiledModule(s,original,0,ignored,s->pipelineBindings.at(info.layout),false,eye,false,&compileNs);}
        std::array<VkPipeline,2> eyes{};
        {
        UnlockedDriverScope unlocked(lock);
        const auto started=s->profileTiming?CommandCpuTiming::now():0;
        auto r=FN(vkCreateComputePipelines)(d,cache,1,&info,a,&out[j]);if(r!=VK_SUCCESS)return r;
        if(indirect)for(int eye=0;eye<2;++eye){info.stage.module=modules[eye];r=FN(vkCreateComputePipelines)(d,cache,1,&info,a,&eyes[eye]);
            if(r!=VK_SUCCESS){for(auto p:eyes)if(p)FN(vkDestroyPipeline)(d,p,a);FN(vkDestroyPipeline)(d,out[j],a);out[j]=VK_NULL_HANDLE;return r;}}
        if(started){driverNs=CommandCpuTiming::now()-started;const auto ms=double(driverNs)/1000000.;if(ms>=2)note("pipeline driver type=compute variants="+std::to_string(indirect?3:1)+" wallMs="+std::to_string(ms));}
        }
        if(indirect)s->indirectPipelines[out[j]]=eyes;
        s->computeStereo[out[j]]=stereo;
        if(s->waterCapture)s->waterCapture->pipeline(out[j],profile);
        if(profile==0x24abb0e76a065289ull)note("WATER_DISPATCH mode=per-eye shader="+shaderKey(profile));
        if(s->checkpoint){registerMarker(s,out[j],label);for(unsigned eye=0;eye<2;++eye)if(eyes[eye])registerMarker(s,eyes[eye],label+" eye="+std::to_string(eye));}
        if(argent::perf::enabled())note("PERF_PIPELINE kind=compute pipeline="+std::to_string(reinterpret_cast<uint64_t>(out[j]))+" shaders="+shaderHash);
        recordPipelineBuild(s,"compute",shaderHash,buildStart,cpuStart,j==0?batchLockNs:0,compileNs,driverNs);
    }return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice d,VkPipeline pipeline,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);if(s->waterCapture)s->waterCapture->forgetPipeline(pipeline);handDepth::handScenePipelineDestroyed(pipeline);s->pipelineRetirement.fetch_add(1,std::memory_order_release);auto it=s->stereoPipelines.find(pipeline);if(it!=s->stereoPipelines.end()){FN(vkDestroyPipeline)(d,it->second,a);s->stereoPipelines.erase(it);}auto indirect=s->indirectPipelines.find(pipeline);if(indirect!=s->indirectPipelines.end()){for(auto eye:indirect->second)FN(vkDestroyPipeline)(d,eye,a);s->indirectPipelines.erase(indirect);}s->computeStereo.erase(pipeline);FN(vkDestroyPipeline)(d,pipeline,a);}
VKAPI_ATTR VkResult VKAPI_CALL beginCommand(VkCommandBuffer cb,const VkCommandBufferBeginInfo* i){try{auto s=state(cb);std::shared_lock<std::shared_mutex> lock(s->mutex);if(i->pInheritanceInfo&&i->pInheritanceInfo->renderPass)return VK_ERROR_FEATURE_NOT_PRESENT;auto& command=commandUnderLock(s,cb);command.stereo=false;command.compute=VK_NULL_HANDLE;command.graphics=VK_NULL_HANDLE;command.graphicsInfo={};command.computeInfo={};for(auto& descriptor:command.descriptors)descriptor.set=VK_NULL_HANDLE;for(auto& descriptor:command.computeDescriptors)descriptor.set=VK_NULL_HANDLE;command.queryCopies=0;command.bindings.clear();command.pushes.clear();
    // We already resolved the command under the lock. Prime the recording
    // thread's cache here instead of reacquiring it at the first pipeline bind.
    if(s->waterCapture)s->waterCapture->reset(cb);
    const auto captureFlags=command.family<s->queueFlags.size()?s->queueFlags[command.family]:0;
    command.captureGraphicsQueue=(captureFlags&VK_QUEUE_GRAPHICS_BIT)!=0;
    command.waterEligible=command.primary&&!(i->flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)&&(captureFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT));
    const auto result=FN(vkBeginCommandBuffer)(cb,i);
    if(result==VK_SUCCESS&&argent::perf::enabled()){
        const auto serial=argent::perf::frame.load(std::memory_order_relaxed);const auto mode=argent::perf::mode.load(std::memory_order_relaxed);
        if(!command.census&&argent::perf::sample(serial,mode))command.census=std::make_unique<argent::perf::CommandCensus>();
        if(command.census)command.census->begin(serial,mode);
        if(command.gpuSubmitted)note("PERF_GPU_DROPPED reason=rerecorded-before-readback recordFrame="+std::to_string(command.gpuRecordFrame));
        command.gpuSubmitted=false;command.gpuSubmits=0;command.gpuPassCount=0;command.gpuRegionsSeen=0;command.gpuPoints=1;command.gpuEnd=0;command.gpuActivePass=-1;
        const bool selected=mode==2&&argent::perf::sample(serial,mode)&&command.primary&&!(i->flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
        if(selected&&command.family<s->timestampBits.size())command.gpu.initialize(s->device,s->resolver,s->timestampPeriod,s->timestampBits[command.family]);
        command.gpu.begin(cb,selected);command.gpuRecordFrame=serial;
    }
    return result;}catch(const std::exception& e){note(e.what());return VK_ERROR_INITIALIZATION_FAILED;}}
VKAPI_ATTR VkResult VKAPI_CALL endCommand(VkCommandBuffer cb){try{auto s=state(cb);auto& c=localCommand(s,cb);
    if(!argent::cleanRelease&&c.gpu.recorded){c.gpuEnd=c.gpuPoints++;c.gpu.point(cb,c.gpuEnd);}
    const auto result=FN(vkEndCommandBuffer)(cb);
    if(!argent::cleanRelease&&c.census){if(result==VK_SUCCESS)s->census.finish(*c.census);else c.census->active=false;}
    if(result!=VK_SUCCESS){c.gpu.recorded=false;if(s->waterCapture)s->waterCapture->reset(cb);}return result;
}catch(const std::exception& e){note(e.what());return VK_ERROR_INITIALIZATION_FAILED;}}
VKAPI_ATTR VkResult VKAPI_CALL createCommandPool(VkDevice d,const VkCommandPoolCreateInfo* info,const VkAllocationCallbacks* a,VkCommandPool* out){RESULT_BEGIN
    const auto result=FN(vkCreateCommandPool)(d,info,a,out);if(result==VK_SUCCESS)s->poolFamilies[*out]=info->queueFamilyIndex;return result;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL allocateCommands(VkDevice d,const VkCommandBufferAllocateInfo* i,VkCommandBuffer* out){RESULT_BEGIN
    auto r=FN(vkAllocateCommandBuffers)(d,i,out);if(r==VK_SUCCESS)for(uint32_t j=0;j<i->commandBufferCount;++j){s->commandPools[out[j]]=i->commandPool;auto& c=s->commands.try_emplace(out[j]).first->second;c.primary=i->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY;const auto f=s->poolFamilies.find(i->commandPool);if(f!=s->poolFamilies.end())c.family=f->second;}return r;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL captureResetCommand(VkCommandBuffer cb,VkCommandBufferResetFlags flags){auto s=state(cb);auto r=reinterpret_cast<PFN_vkResetCommandBuffer>(s->resolver(s->device,"vkResetCommandBuffer"))(cb,flags);if(r==VK_SUCCESS&&s->waterCapture)s->waterCapture->reset(cb);return r;}
VKAPI_ATTR VkResult VKAPI_CALL captureResetPool(VkDevice d,VkCommandPool pool,VkCommandPoolResetFlags flags){auto s=state(d);auto r=reinterpret_cast<PFN_vkResetCommandPool>(s->resolver(d,"vkResetCommandPool"))(d,pool,flags);if(r==VK_SUCCESS&&s->waterCapture){std::shared_lock<std::shared_mutex> lock(s->mutex);for(const auto& c:s->commandPools)if(c.second==pool)s->waterCapture->reset(c.first);}return r;}
VKAPI_ATTR void VKAPI_CALL freeCommands(VkDevice d,VkCommandPool pool,uint32_t count,const VkCommandBuffer* commands){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->commandRetirement.fetch_add(1,std::memory_order_release);for(uint32_t j=0;j<count;++j){if(s->waterCapture)s->waterCapture->reset(commands[j]);s->commands.at(commands[j]).gpu.shutdownAfterCompletion();s->commands.erase(commands[j]);s->commandPools.erase(commands[j]);}FN(vkFreeCommandBuffers)(d,pool,count,commands);}
VKAPI_ATTR void VKAPI_CALL destroyCommandPool(VkDevice d,VkCommandPool pool,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->commandRetirement.fetch_add(1,std::memory_order_release);for(auto it=s->commandPools.begin();it!=s->commandPools.end();)if(it->second==pool){if(s->waterCapture)s->waterCapture->reset(it->first);s->commands.at(it->first).gpu.shutdownAfterCompletion();s->commands.erase(it->first);it=s->commandPools.erase(it);}else ++it;s->poolFamilies.erase(pool);FN(vkDestroyCommandPool)(d,pool,a);}
void checkpoint(State* s,VkCommandBuffer cb,uintptr_t marker){if(s->checkpoint&&marker)s->checkpoint(cb,reinterpret_cast<const void*>(marker));}
void bindGraphics(State* s,VkCommandBuffer cb,const CommandState& command){
    if(!command.graphics)return;
    FN(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,command.stereo?command.graphicsInfo.stereo:command.graphics);
    checkpoint(s,cb,command.stereo?command.graphicsInfo.stereoMarker:command.graphicsInfo.monoMarker);
}
VKAPI_ATTR void VKAPI_CALL bindPipeline(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipeline pipeline){LOCAL_COMMAND_BEGIN
    // Ray-tracing pipelines are created by the downstream driver and have their
    // own bind point. They must neither enter the graphics registry/cache nor
    // replace the graphics state replayed when a stereo render pass begins.
    if(point!=VK_PIPELINE_BIND_POINT_GRAPHICS&&point!=VK_PIPELINE_BIND_POINT_COMPUTE){
        if(!s->loggedNativePipelineBind.exchange(true,std::memory_order_relaxed))
            note("PIPELINE_NATIVE_BIND point="+std::to_string(point)+" handle="+std::to_string(reinterpret_cast<uintptr_t>(pipeline)));
        FN(vkCmdBindPipeline)(cb,point,pipeline);return;
    }
    thread_local DescriptorCountCache<VkPipeline,PipelineSnapshot> cache;
    const auto& snapshot=cache.get(s->cacheId,s->pipelineRetirement.load(std::memory_order_acquire),pipeline,[&]{
        if(s->profileTiming)s->pipelineCacheMisses.fetch_add(1,std::memory_order_relaxed);
        std::shared_lock<std::shared_mutex> lock(s->mutex);PipelineSnapshot result;
        if(point!=VK_PIPELINE_BIND_POINT_COMPUTE){
            const auto found=s->stereoPipelines.find(pipeline);
            if(found==s->stereoPipelines.end())throw std::runtime_error("SFS graphics pipeline unavailable point="+std::to_string(point)+" handle="+std::to_string(reinterpret_cast<uintptr_t>(pipeline))+" registered="+std::to_string(s->stereoPipelines.size())+"; inspect earlier pipeline/shader creation errors");
            result.stereo=found->second;if(s->waterCapture)result.sky=s->waterCapture->skyPipeline(pipeline);
        }else result.stereo=pipeline;
        if(point==VK_PIPELINE_BIND_POINT_COMPUTE){result.computeStereo=s->computeStereo.at(pipeline);auto indirect=s->indirectPipelines.find(pipeline);if(indirect!=s->indirectPipelines.end())result.indirect=indirect->second;}
        if(s->checkpoint){auto marker=s->pipelineMarkers.find(pipeline);if(marker!=s->pipelineMarkers.end())result.monoMarker=marker->second;
            marker=s->pipelineMarkers.find(result.stereo);if(marker!=s->pipelineMarkers.end())result.stereoMarker=marker->second;
            auto end=s->dispatchEndMarkers.find(pipeline);if(end!=s->dispatchEndMarkers.end())result.endMarker=end->second;
            for(size_t eye=0;eye<2;++eye){marker=s->pipelineMarkers.find(result.indirect[eye]);if(marker!=s->pipelineMarkers.end())result.indirectMarkers[eye]=marker->second;
                end=s->dispatchEndMarkers.find(result.indirect[eye]);if(end!=s->dispatchEndMarkers.end())result.indirectEndMarkers[eye]=end->second;}}
        return result;
    });
    if(!argent::cleanRelease&&local.census&&local.census->active){auto& k=point==VK_PIPELINE_BIND_POINT_COMPUTE?local.census->compute:local.census->graphics;k.pipeline=reinterpret_cast<uint64_t>(pipeline);k.compute=point==VK_PIPELINE_BIND_POINT_COMPUTE;local.census->add(argent::perf::Pipeline,k.compute!=0);}
    const bool stereo=point!=VK_PIPELINE_BIND_POINT_COMPUTE&&local.stereo;
    if(point==VK_PIPELINE_BIND_POINT_COMPUTE){local.compute=pipeline;local.computeInfo=snapshot;}else {local.graphics=pipeline;local.graphicsInfo=snapshot;}
    FN(vkCmdBindPipeline)(cb,point,stereo?snapshot.stereo:pipeline);
    const auto marker=stereo?snapshot.stereoMarker:snapshot.monoMarker;
    checkpoint(s,cb,marker);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL bindSets(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,const VkDescriptorSet* sets,uint32_t dynamicCount,const uint32_t* dynamic){LOCAL_COMMAND_BEGIN
    if(point!=VK_PIPELINE_BIND_POINT_GRAPHICS&&point!=VK_PIPELINE_BIND_POINT_COMPUTE){
        FN(vkCmdBindDescriptorSets)(cb,point,layout,first,count,sets,dynamicCount,dynamic);return;
    }
    if(!argent::cleanRelease&&local.census)local.census->add(argent::perf::Sets,point==VK_PIPELINE_BIND_POINT_COMPUTE);
    thread_local DescriptorCountCache<VkPipelineLayout,std::vector<uint32_t>> counts;
    const auto retirement=s->pipelineLayoutRetirement.load(std::memory_order_acquire);
    const auto& layoutCounts=counts.get(s->cacheId,retirement,layout,[&]{if(s->profileTiming)s->descriptorCacheMisses.fetch_add(1,std::memory_order_relaxed);std::shared_lock<std::shared_mutex> lock(s->mutex);return s->pipelineDynamicCounts.at(layout);});
    if(first>layoutCounts.size()||count>layoutCounts.size()-first)throw std::runtime_error("SFS descriptor set range mismatch");
    auto& descriptors=point==VK_PIPELINE_BIND_POINT_COMPUTE?local.computeDescriptors:local.descriptors;
    if(descriptors.size()<size_t(first)+count)descriptors.resize(size_t(first)+count);
    uint32_t offset=0;for(uint32_t j=0;j<count;++j){auto set=sets[j];const auto n=layoutCounts[first+j];if(n>dynamicCount-offset)throw std::runtime_error("SFS dynamic descriptor offset mismatch");
        {auto& cached=descriptors[first+j];cached.layout=layout;cached.set=set;cached.order=++local.descriptorOrder;cached.dynamic.clear();if(n)cached.dynamic.assign(dynamic+offset,dynamic+offset+n);}offset+=n;}
    if(offset!=dynamicCount)throw std::runtime_error("SFS unexpected dynamic descriptor offsets");FN(vkCmdBindDescriptorSets)(cb,point,layout,first,count,sets,dynamicCount,dynamic);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL bindVertices(VkCommandBuffer cb,uint32_t first,uint32_t count,const VkBuffer* buffers,const VkDeviceSize* offsets){LOCAL_COMMAND_BEGIN
    if(!argent::cleanRelease&&local.census)local.census->add(argent::perf::Vertices);
    auto& bindings=local.bindings;for(uint32_t j=0;j<count;++j)CommandBindings::set(bindings.vertices,first+j,CommandBindings::Vertex{buffers[j],offsets[j]});FN(vkCmdBindVertexBuffers)(cb,first,count,buffers,offsets);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL bindIndex(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,VkIndexType type){LOCAL_COMMAND_BEGIN
    if(!argent::cleanRelease&&local.census)local.census->add(argent::perf::Index);
    local.bindings.index={buffer,offset,type};FN(vkCmdBindIndexBuffer)(cb,buffer,offset,type);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL viewport(VkCommandBuffer cb,uint32_t first,uint32_t count,const VkViewport* values){LOCAL_COMMAND_BEGIN
    auto& bindings=local.bindings;for(uint32_t j=0;j<count;++j)CommandBindings::set(bindings.viewports,first+j,values[j]);FN(vkCmdSetViewport)(cb,first,count,values);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL scissor(VkCommandBuffer cb,uint32_t first,uint32_t count,const VkRect2D* values){LOCAL_COMMAND_BEGIN
    auto& bindings=local.bindings;for(uint32_t j=0;j<count;++j)CommandBindings::set(bindings.scissors,first+j,values[j]);FN(vkCmdSetScissor)(cb,first,count,values);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL push(VkCommandBuffer cb,VkPipelineLayout layout,VkShaderStageFlags flags,uint32_t offset,uint32_t size,const void* values){LOCAL_COMMAND_BEGIN
    if(!argent::cleanRelease&&local.census)local.census->add(argent::perf::Push,(flags&VK_SHADER_STAGE_COMPUTE_BIT)&&!(flags&VK_SHADER_STAGE_ALL_GRAPHICS));
    // Replay only stages owned by SFS; retain the original flags for the native
    // command so ray-tracing push constants remain independent.
    const auto replayFlags=flags&(VK_SHADER_STAGE_ALL_GRAPHICS|VK_SHADER_STAGE_COMPUTE_BIT);
    if(replayFlags)local.pushes.write(layout,replayFlags,offset,size,values);
    FN(vkCmdPushConstants)(cb,layout,flags,offset,size,values);
COMMAND_END}
void replayBindings(State* s,VkCommandBuffer cb,CommandState& command){
    bindGraphics(s,cb,command);
    for(uint32_t index=0;index<command.descriptors.size();++index){const auto& binding=command.descriptors[index];if(binding.set)FN(vkCmdBindDescriptorSets)(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,binding.layout,index,1,&binding.set,uint32_t(binding.dynamic.size()),binding.dynamic.data());}
    command.bindings.replay(s->dispatch,cb);
    command.pushes.replay([&](VkPipelineLayout layout,VkShaderStageFlags flags,uint32_t offset,uint32_t size,const void* values){FN(vkCmdPushConstants)(cb,layout,flags,offset,size,values);});
}
void countDraw(CommandState& c,bool indirect){
    if(!argent::cleanRelease&&c.census)c.census->add(indirect?argent::perf::DrawIndirect:argent::perf::Draw);
    if(c.gpuActivePass>=0){auto& p=c.gpuPasses[size_t(c.gpuActivePass)];const auto pipeline=reinterpret_cast<uint64_t>(c.graphics);if(!p.draws)p.pipelineFirst=pipeline;p.pipelineLast=pipeline;++p.draws;}
}
void captureDraw(State* s,VkCommandBuffer cb,const char* kind,uint64_t count,uint64_t instances,uint64_t first,int64_t offset,uint64_t instance){
    if(!s->waterCapture)return;auto& local=localCommand(s,cb);
    if(!local.graphicsInfo.sky&&!s->waterCapture->graphicsArmed())return;
    std::vector<WaterGpuCapture::BoundSet> bound;std::ostringstream detail;
    if(s->waterCapture->graphicsArmed()){
        for(const auto& b:local.descriptors)bound.push_back({b.set,b.dynamic});
        detail<<kind<<" count="<<count<<" instances="<<instances<<" first="<<first<<" offset="<<offset<<" instance="<<instance;
        for(unsigned n=0;n<local.bindings.viewports.size();++n)if(local.bindings.viewports[n].valid){auto& v=local.bindings.viewports[n].value;detail<<" viewport"<<n<<'='<<v.x<<','<<v.y<<','<<v.width<<','<<v.height<<','<<v.minDepth<<','<<v.maxDepth;}
        for(unsigned n=0;n<local.bindings.scissors.size();++n)if(local.bindings.scissors[n].valid){auto& v=local.bindings.scissors[n].value;detail<<" scissor"<<n<<'='<<v.offset.x<<','<<v.offset.y<<','<<v.extent.width<<','<<v.extent.height;}
        for(unsigned n=0;n<bound.size();++n){detail<<" set"<<n<<'='<<reinterpret_cast<uintptr_t>(bound[n].set);for(auto off:bound[n].dynamic)detail<<":"<<off;}
    }
    s->waterCapture->graphicsDraw(cb,local.graphics,bound,detail.str(),local.stereo);
}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer cb,uint32_t vertices,uint32_t instances,uint32_t first,uint32_t instance){
 try{auto s=state(cb);if(argent::perf::enabled())countDraw(localCommand(s,cb),false);captureDraw(s,cb,"draw",vertices,instances,first,0,instance);FN(vkCmdDraw)(cb,vertices,instances,first,instance);}catch(const std::exception& e){commandFailure(e.what());}}
VKAPI_ATTR void VKAPI_CALL drawIndexed(VkCommandBuffer cb,uint32_t count,uint32_t instances,uint32_t first,int32_t offset,uint32_t instance){
 try{auto s=state(cb);if(argent::perf::enabled())countDraw(localCommand(s,cb),false);captureDraw(s,cb,"indexed",count,instances,first,offset,instance);FN(vkCmdDrawIndexed)(cb,count,instances,first,offset,instance);}catch(const std::exception& e){commandFailure(e.what());}}
VKAPI_ATTR void VKAPI_CALL drawIndirect(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride){
 try{auto s=state(cb);if(argent::perf::enabled())countDraw(localCommand(s,cb),true);captureDraw(s,cb,"indirect(buffer/offset/stride)",count,reinterpret_cast<uintptr_t>(buffer),offset,stride,0);FN(vkCmdDrawIndirect)(cb,buffer,offset,count,stride);}catch(const std::exception& e){commandFailure(e.what());}}
VKAPI_ATTR void VKAPI_CALL drawIndexedIndirect(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride){
 try{auto s=state(cb);if(argent::perf::enabled())countDraw(localCommand(s,cb),true);captureDraw(s,cb,"indexedIndirect(buffer/offset/stride)",count,reinterpret_cast<uintptr_t>(buffer),offset,stride,0);FN(vkCmdDrawIndexedIndirect)(cb,buffer,offset,count,stride);}catch(const std::exception& e){commandFailure(e.what());}}
// Count-buffer variants can bypass all four classic draw entry points.
#define COUNT_DRAW(handler,api) \
VKAPI_ATTR void VKAPI_CALL handler(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,VkBuffer countBuffer,VkDeviceSize countOffset,uint32_t maxDrawCount,uint32_t stride){ \
 try{auto s=state(cb);if(argent::perf::enabled())countDraw(localCommand(s,cb),true); \
 auto label=std::string(#api)+" argumentBuffer="+std::to_string(reinterpret_cast<uintptr_t>(buffer)); \
 captureDraw(s,cb,label.c_str(),maxDrawCount,reinterpret_cast<uintptr_t>(countBuffer),countOffset,int64_t(offset),stride); \
 auto call=reinterpret_cast<PFN_##api>(s->resolver(s->device,#api));if(!call)throw std::runtime_error(#api " unavailable");call(cb,buffer,offset,countBuffer,countOffset,maxDrawCount,stride); \
 }catch(const std::exception& e){commandFailure(e.what());}}
COUNT_DRAW(drawIndirectCount,vkCmdDrawIndirectCount)
COUNT_DRAW(drawIndexedIndirectCount,vkCmdDrawIndexedIndirectCount)
COUNT_DRAW(drawIndirectCountKHR,vkCmdDrawIndirectCountKHR)
COUNT_DRAW(drawIndexedIndirectCountKHR,vkCmdDrawIndexedIndirectCountKHR)
COUNT_DRAW(drawIndirectCountAMD,vkCmdDrawIndirectCountAMD)
COUNT_DRAW(drawIndexedIndirectCountAMD,vkCmdDrawIndexedIndirectCountAMD)
#undef COUNT_DRAW
VKAPI_ATTR void VKAPI_CALL beginPass(VkCommandBuffer cb,const VkRenderPassBeginInfo* i,VkSubpassContents contents){LOCAL_COMMAND_BEGIN
    if(!argent::cleanRelease&&local.census)local.census->pass(reinterpret_cast<uint64_t>(i->renderPass),i->renderArea.extent.width,i->renderArea.extent.height);
    if(!argent::cleanRelease&&local.gpu.recorded)++local.gpuRegionsSeen;
    if(!argent::cleanRelease&&local.gpu.recorded&&local.gpuPassCount<local.gpuPasses.size()){auto& p=local.gpuPasses[local.gpuPassCount];p={};p.pass=reinterpret_cast<uint64_t>(i->renderPass);p.width=i->renderArea.extent.width;p.height=i->renderArea.extent.height;p.begin=local.gpuPoints++;local.gpu.point(cb,p.begin);local.gpuActivePass=int(local.gpuPassCount++);}
    struct FramebufferSnapshot {bool stereo{},mixed{};};
    thread_local DescriptorCountCache<VkFramebuffer,FramebufferSnapshot> framebuffers;
    thread_local DescriptorCountCache<VkRenderPass,VkRenderPass> passes;
    const auto framebuffer=framebuffers.get(s->cacheId,s->framebufferRetirement.load(std::memory_order_acquire),i->framebuffer,[&]{
        if(s->profileTiming)s->framebufferCacheMisses.fetch_add(1,std::memory_order_relaxed);
        std::shared_lock<std::shared_mutex> lock(s->mutex);return FramebufferSnapshot{s->framebufferStereo.at(i->framebuffer),s->mixedFramebuffers.at(i->framebuffer)};
    });
    auto info=*i;
    if(framebuffer.stereo)info.renderPass=passes.get(s->cacheId,s->passRetirement.load(std::memory_order_acquire),i->renderPass,[&]{
        if(s->profileTiming)s->passCacheMisses.fetch_add(1,std::memory_order_relaxed);
        std::shared_lock<std::shared_mutex> lock(s->mutex);return s->passes.at(i->renderPass);
    });
    handDepth::handSceneBeginRenderPass(cb,i);handDepth::handSceneBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,local.graphics);
    local.stereo=framebuffer.stereo;if(!argent::cleanRelease&&framebuffer.mixed)s->mixedPasses.fetch_add(1,std::memory_order_relaxed);
    if(s->waterCapture){auto shot=s->waterCapture->graphicsBegin(cb,*i,local.waterEligible&&local.captureGraphicsQueue,local.stereo);if(shot){std::shared_lock<std::shared_mutex> guard(s->mutex);const auto* bytes=reinterpret_cast<const unsigned char*>(&s->renderUniforms);shot->projection.assign(bytes,bytes+sizeof(s->renderUniforms));shot->frameSerial=s->renderPose.serial;}}
    FN(vkCmdBeginRenderPass)(cb,&info,contents);replayBindings(s,cb,local);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL endPass(VkCommandBuffer cb){LOCAL_COMMAND_BEGIN const auto& vp=local.bindings.viewports;if(!vp.empty()&&vp[0].valid)handDepth::handSceneViewport(cb,vp[0].value);handDepth::handSceneEndRenderPass(cb);FN(vkCmdEndRenderPass)(cb);if(s->waterCapture)s->waterCapture->graphicsEnd(cb);local.stereo=false;if(!argent::cleanRelease&&local.gpuActivePass>=0){auto& p=local.gpuPasses[size_t(local.gpuActivePass)];p.end=local.gpuPoints++;local.gpu.point(cb,p.end);local.gpuActivePass=-1;}if(!argent::cleanRelease&&local.census)local.census->pass(0,0,0);COMMAND_END}
VKAPI_ATTR void VKAPI_CALL nextPass(VkCommandBuffer cb,VkSubpassContents contents){LOCAL_COMMAND_BEGIN if(!argent::cleanRelease&&local.census)++local.census->graphics.subpass;handDepth::handSceneNextSubpass(cb);FN(vkCmdNextSubpass)(cb,contents);if(s->waterCapture)s->waterCapture->graphicsNext(cb);replayBindings(s,cb,local);COMMAND_END}
VKAPI_ATTR void VKAPI_CALL lineWidth(VkCommandBuffer cb,float width){LOCAL_COMMAND_BEGIN
    local.bindings.line=width;FN(vkCmdSetLineWidth)(cb,width);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL depthBias(VkCommandBuffer cb,float constant,float clamp,float slope){LOCAL_COMMAND_BEGIN
    local.bindings.bias={constant,clamp,slope};FN(vkCmdSetDepthBias)(cb,constant,clamp,slope);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL blendConstants(VkCommandBuffer cb,const float* values){LOCAL_COMMAND_BEGIN
    local.bindings.blend={values[0],values[1],values[2],values[3]};FN(vkCmdSetBlendConstants)(cb,values);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL depthBounds(VkCommandBuffer cb,float min,float max){LOCAL_COMMAND_BEGIN
    local.bindings.bounds={min,max};FN(vkCmdSetDepthBounds)(cb,min,max);
COMMAND_END}
#define STENCIL_WRAPPER(handler,api,slot) \
VKAPI_ATTR void VKAPI_CALL handler(VkCommandBuffer cb,VkStencilFaceFlags faces,uint32_t value){LOCAL_COMMAND_BEGIN \
    local.bindings.setStencil(slot,faces,value);FN(api)(cb,faces,value); \
COMMAND_END}
STENCIL_WRAPPER(stencilCompare,vkCmdSetStencilCompareMask,0)
STENCIL_WRAPPER(stencilWrite,vkCmdSetStencilWriteMask,1)
STENCIL_WRAPPER(stencilReference,vkCmdSetStencilReference,2)
#undef STENCIL_WRAPPER
struct ComputeGpuScope {
 CommandState& c;VkCommandBuffer cb;int slot=-1;
 ComputeGpuScope(CommandState& value,VkCommandBuffer command,uint32_t x,uint32_t y,uint32_t z,bool indirect):c(value),cb(command){
  if(argent::cleanRelease||!c.gpu.recorded)return;++c.gpuRegionsSeen;if(c.gpuPassCount>=c.gpuPasses.size())return;
  slot=int(c.gpuPassCount++);auto& p=c.gpuPasses[size_t(slot)];p={};p.compute=indirect?2:1;p.pipelineFirst=p.pipelineLast=reinterpret_cast<uint64_t>(c.compute);p.x=x;p.y=y;p.z=z;p.begin=c.gpuPoints++;c.gpu.point(cb,p.begin);
 }
 ~ComputeGpuScope(){if(slot>=0){auto& p=c.gpuPasses[size_t(slot)];p.end=c.gpuPoints++;c.gpu.point(cb,p.end);}}
};
VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer cb,uint32_t x,uint32_t y,uint32_t z){LOCAL_COMMAND_BEGIN
    ComputeGpuScope gpuScope(local,cb,x,y,z,false);
    if(!argent::cleanRelease&&local.census)local.census->add(argent::perf::Dispatch,true);
    if(!local.compute)throw std::runtime_error("SFS dispatch without compute pipeline");
    uint32_t depth{};if(!dispatchDepth(z,local.computeInfo.computeStereo,65535,depth))throw std::runtime_error("Stereo dispatch exceeds limit");
    WaterGpuCapture::Ticket capture;
    if(s->waterCapture&&s->waterCapture->armed()){std::vector<WaterGpuCapture::BoundSet> bound;for(const auto& b:local.computeDescriptors)bound.push_back({b.set,b.dynamic});capture=s->waterCapture->before(cb,local.compute,bound,x,y,depth,local.waterEligible,local.captureGraphicsQueue);if(capture){std::shared_lock<std::shared_mutex> guard(s->mutex);const auto* bytes=reinterpret_cast<const unsigned char*>(&s->renderUniforms);capture->projection.assign(bytes,bytes+sizeof(s->renderUniforms));capture->frameSerial=s->renderPose.serial;}}
    checkpoint(s,cb,local.computeInfo.monoMarker);FN(vkCmdDispatch)(cb,x,y,depth);checkpoint(s,cb,local.computeInfo.endMarker);
    if(capture)s->waterCapture->after(cb,capture);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL dispatchIndirect(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset){LOCAL_COMMAND_BEGIN
    ComputeGpuScope gpuScope(local,cb,0,0,0,true);
    if(!argent::cleanRelease&&local.census)local.census->add(argent::perf::DispatchIndirect,true);
    const auto pipeline=local.compute;const auto& info=local.computeInfo;
    if(!pipeline)throw std::runtime_error("SFS indirect dispatch without compute pipeline");
    if(!info.computeStereo){
        if(s->profileTiming)s->indirectMono.fetch_add(1,std::memory_order_relaxed);
        checkpoint(s,cb,info.monoMarker);FN(vkCmdDispatchIndirect)(cb,buffer,offset);checkpoint(s,cb,info.endMarker);return;
    }
    if(!info.indirect[0]||!info.indirect[1])throw std::runtime_error("SFS indirect compute uses an unsupported profile replacement; refusing left-eye-only output");
    if(s->profileTiming)s->indirectStereo.fetch_add(1,std::memory_order_relaxed);
    WaterGpuCapture::Ticket capture;
    // Atmosphere can use GPU-owned indirect counts. Capture around the entire
    // eye pair without reading or modifying those counts on the CPU.
    if(s->waterCapture&&s->waterCapture->armed()){std::vector<WaterGpuCapture::BoundSet> bound;for(const auto& b:local.computeDescriptors)bound.push_back({b.set,b.dynamic});capture=s->waterCapture->before(cb,pipeline,bound,0,0,0,local.waterEligible,local.captureGraphicsQueue);if(capture){std::shared_lock<std::shared_mutex> guard(s->mutex);const auto* bytes=reinterpret_cast<const unsigned char*>(&s->renderUniforms);capture->projection.assign(bytes,bytes+sizeof(s->renderUniforms));capture->frameSerial=s->renderPose.serial;}}
    // Counts remain GPU-owned and unchanged. Each variant uses the full original
    // workgroup grid and writes its own eye layer; shared-buffer-only work stays mono.
    for(size_t eye=0;eye<2;++eye){FN(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,info.indirect[eye]);checkpoint(s,cb,info.indirectMarkers[eye]);FN(vkCmdDispatchIndirect)(cb,buffer,offset);checkpoint(s,cb,info.indirectEndMarkers[eye]);}
    if(capture)s->waterCapture->after(cb,capture);
    FN(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
COMMAND_END}
VkImageSubresourceRange range(State* s,VkImage image,VkImageSubresourceRange value){if(s->images.layers(image)==2&&value.baseArrayLayer==0&&value.layerCount==1)value.layerCount=2;return value;}
VKAPI_ATTR void VKAPI_CALL barriers(VkCommandBuffer cb,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags deps,uint32_t nm,const VkMemoryBarrier* m,uint32_t nb,const VkBufferMemoryBarrier* b,uint32_t ni,const VkImageMemoryBarrier* i){
    // Buffer/global dependencies have no image layers or presentation layout
    // to translate. Avoid the shared metadata lock and command-map lookup.
    if(!ni){try{HOOK_BUCKET("buffer-only") auto s=state(cb);timing.resolved();timing.acquired();FN(vkCmdPipelineBarrier)(cb,src,dst,deps,nm,m,nb,b,0,i);}catch(const std::exception& e){commandFailure(e.what());}return;}
    COMMAND_BEGIN
    auto& images=commandUnderLock(s,cb).imageBarriers;images.clear();if(ni)images.assign(i,i+ni);for(auto& image:images){image.subresourceRange=range(s,image.image,image.subresourceRange);
        if(s->sources&&s->sources->ownsImage(image.image)){
            if(image.oldLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)image.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
            if(image.newLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)image.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        }
    }handDepth::handSceneImageBarriers(ni,images.data());FN(vkCmdPipelineBarrier)(cb,src,dst,deps,nm,m,nb,b,ni,images.data());
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL clearColor(VkCommandBuffer cb,VkImage image,VkImageLayout layout,const VkClearColorValue* value,uint32_t count,const VkImageSubresourceRange* ranges){COMMAND_BEGIN
    auto& copies=commandUnderLock(s,cb).clearRanges;copies.clear();if(count)copies.assign(ranges,ranges+count);for(auto& r:copies)r=range(s,image,r);FN(vkCmdClearColorImage)(cb,image,layout,value,count,copies.data());
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL clearDepth(VkCommandBuffer cb,VkImage image,VkImageLayout layout,const VkClearDepthStencilValue* value,uint32_t count,const VkImageSubresourceRange* ranges){COMMAND_BEGIN
    auto& copies=commandUnderLock(s,cb).clearRanges;copies.clear();if(count)copies.assign(ranges,ranges+count);for(auto& r:copies)r=range(s,image,r);FN(vkCmdClearDepthStencilImage)(cb,image,layout,value,count,copies.data());
COMMAND_END}
template<class T>std::vector<T> copyRegions(State* s,VkImage src,VkImage dst,uint32_t count,const T* regions){
    std::vector<T> result;for(uint32_t j=0;j<count;++j){auto region=regions[j];result.push_back(region);if(s->images.layers(dst)==2&&region.dstSubresource.baseArrayLayer==0&&region.dstSubresource.layerCount==1){region.dstSubresource.baseArrayLayer=1;if(s->images.layers(src)==2)region.srcSubresource.baseArrayLayer=1;result.push_back(region);}}return result;
}
VKAPI_ATTR void VKAPI_CALL copyImage(VkCommandBuffer cb,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageCopy* regions){COMMAND_BEGIN auto r=copyRegions(s,src,dst,count,regions);FN(vkCmdCopyImage)(cb,src,sl,dst,dl,uint32_t(r.size()),r.data());COMMAND_END}
VKAPI_ATTR void VKAPI_CALL blitImage(VkCommandBuffer cb,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageBlit* regions,VkFilter filter){COMMAND_BEGIN auto r=copyRegions(s,src,dst,count,regions);FN(vkCmdBlitImage)(cb,src,sl,dst,dl,uint32_t(r.size()),r.data(),filter);COMMAND_END}
VKAPI_ATTR void VKAPI_CALL resolveImage(VkCommandBuffer cb,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageResolve* regions){COMMAND_BEGIN auto r=copyRegions(s,src,dst,count,regions);FN(vkCmdResolveImage)(cb,src,sl,dst,dl,uint32_t(r.size()),r.data());COMMAND_END}
VKAPI_ATTR void VKAPI_CALL uploadImage(VkCommandBuffer cb,VkBuffer buffer,VkImage image,VkImageLayout layout,uint32_t count,const VkBufferImageCopy* regions){COMMAND_BEGIN
    std::vector<VkBufferImageCopy> copies;for(uint32_t j=0;j<count;++j){auto r=regions[j];copies.push_back(r);if(s->images.layers(image)==2&&r.imageSubresource.baseArrayLayer==0&&r.imageSubresource.layerCount==1){r.imageSubresource.baseArrayLayer=1;copies.push_back(r);}}FN(vkCmdCopyBufferToImage)(cb,buffer,image,layout,uint32_t(copies.size()),copies.data());
COMMAND_END}
}
bool nativeProbeEnabled(){static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("ARGENT_SFS_NATIVE_PROBE",value,8)==1&&value[0]=='1';}();return enabled;}
void waterCaptureSubmitted(VkDevice d,VkQueue q,uint32_t n,const VkCommandBuffer* cb){try{auto s=state(d);if(s->waterCapture){s->waterCapture->submitted(q,n,cb);s->waterCapture->poll();}}catch(const std::exception& e){note(std::string("WATER_GPU_CAPTURE error: ")+e.what());}}
bool initialize(VkDevice d,VkPhysicalDevice physical,PFN_vkGetDeviceProcAddr gdpa,const VkPhysicalDeviceMemoryProperties& memory,const Configuration& configuration){
    if(!nativeProbeEnabled())return true;
    auto s=std::make_shared<State>();
    try{s->device=d;s->resolver=gdpa;s->configuration=configuration;s->dispatch.load(d,gdpa);
        auto captureRoot=WaterGpuCapture::requestedRoot();if(!captureRoot.empty()){s->waterCapture=std::make_unique<WaterGpuCapture>(d,gdpa,memory,captureRoot,note,WaterGpuCapture::shadingShader,true);note("WATER_GPU_CAPTURE target=geometry-shading-and-scene snapshots=9");}
        // Queue families are supplied by configurePerformanceGpu before recording.

        s->queryResolver=std::make_unique<QueryResolvePipeline>();s->queryResolver->initialize(d,s->dispatch,memory);
        char timing[8]{};s->profileTiming=argent::perf::enabled()||(GetEnvironmentVariableA("ARGENT_SFS_PROFILE_TIMING",timing,8)==1&&timing[0]=='1');wchar_t path[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_SFS_PROFILE",path,32768);if(n&&n<32768)s->profile=path;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=sizeof(EyeUniforms);bi.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;if(FN(vkCreateBuffer)(d,&bi,nullptr,&s->params)!=VK_SUCCESS)return false;
        VkMemoryRequirements r{};FN(vkGetBufferMemoryRequirements)(d,s->params,&r);uint32_t index=UINT32_MAX;for(uint32_t j=0;j<memory.memoryTypeCount;++j)if((r.memoryTypeBits&(1u<<j))&&(memory.memoryTypes[j].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){index=j;break;}
        if(index==UINT32_MAX)throw std::runtime_error("No coherent SFS parameter memory");VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=r.size;ai.memoryTypeIndex=index;if(FN(vkAllocateMemory)(d,&ai,nullptr,&s->paramsMemory)!=VK_SUCCESS)throw std::runtime_error("SFS parameter allocation failed");if(FN(vkBindBufferMemory)(d,s->params,s->paramsMemory,0)!=VK_SUCCESS)throw std::runtime_error("SFS parameter bind failed");
        void* mapped{};if(FN(vkMapMemory)(d,s->paramsMemory,0,sizeof(EyeUniforms),0,&mapped)!=VK_SUCCESS)throw std::runtime_error("SFS parameter map failed");EyeUniforms initial{{identity(),identity()},{}};std::memcpy(mapped,&initial,sizeof(initial));FN(vkUnmapMemory)(d,s->paramsMemory);
        {std::lock_guard<std::mutex> lock(devicesMutex);devices[dispatchKey(d)]=s;deviceGeneration.fetch_add(1,std::memory_order_release);}note(vrEnabled()?"native SFS experimental OpenXR producer initialized":"native multiview probe initialized; fixed identity projection; NOT VR");return true;
    }catch(const std::exception& e){note(e.what());if(s->params)FN(vkDestroyBuffer)(d,s->params,nullptr);if(s->paramsMemory)FN(vkFreeMemory)(d,s->paramsMemory,nullptr);return false;}
}
void shutdown(VkDevice d){if(!nativeProbeEnabled())return;std::shared_ptr<State> s;try{s=state(d)->shared_from_this();}catch(const std::exception&){return;}std::unique_lock<std::shared_mutex> lock(s->mutex);if(FN(vkDeviceWaitIdle)(d)!=VK_SUCCESS)commandFailure("SFS shutdown retirement failed");if(s->waterCapture){s->waterCapture->poll(false);s->waterCapture->shutdown();}for(auto& c:s->commands)c.second.gpu.shutdownAfterCompletion();handDepth::handSceneDeviceDestroyed();s->commandRetirement.fetch_add(1,std::memory_order_release);s->commands.clear();s->queryResolver.reset();if(s->sources)s->sources->clearAfterDeviceIdle();for(auto& entry:s->eyeViews)for(auto eye:entry.second)if(eye)FN(vkDestroyImageView)(d,eye,nullptr);s->eyeViews.clear();for(auto& module:s->compiled)FN(vkDestroyShaderModule)(d,module.second,nullptr);FN(vkDestroyBuffer)(d,s->params,nullptr);FN(vkFreeMemory)(d,s->paramsMemory,nullptr);std::lock_guard<std::mutex> devicesLock(devicesMutex);for(auto it=devices.begin();it!=devices.end();)if(it->second==s)it=devices.erase(it);else ++it;deviceGeneration.fetch_add(1,std::memory_order_release);}
bool vrEnabled(){static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("ARGENT_SFS_NATIVE_VR",value,8)==1&&value[0]=='1';}();return nativeProbeEnabled()&&enabled;}
bool dlssEyeResources(VkCommandBuffer command,const std::array<const dlss::Resource*,30>& input,std::array<dlss::EyeParameters,2>& output,FramePose& pose){
    auto s=state(command);std::unique_lock<std::shared_mutex> lock(s->mutex);
    pose=s->renderPose;
    for(size_t i=0;i<input.size();++i){
        if(!input[i])continue;
        const auto& resource=*input[i];auto found=s->viewInfos.find(resource.view);
        if(found==s->viewInfos.end())return false;
        const auto& info=found->second;
        if(info.image!=resource.image||info.format!=resource.format||info.subresourceRange.levelCount!=1||
           (info.viewType!=VK_IMAGE_VIEW_TYPE_2D_ARRAY&&info.viewType!=VK_IMAGE_VIEW_TYPE_2D))return false;
        const bool stereo=info.viewType==VK_IMAGE_VIEW_TYPE_2D_ARRAY&&info.subresourceRange.layerCount==2;
        // Scene color, output, depth and motion must all describe both eyes.
        // Exposure and optional masks may legitimately be shared mono inputs.
        if(i<4&&!stereo)return false;
        if(!stereo&&info.subresourceRange.layerCount!=1)return false;
        auto& cached=s->eyeViews[resource.view];
        for(uint32_t eye=0;eye<2;++eye){
            const uint32_t index=stereo?eye:0;
            auto viewInfo=info;viewInfo.viewType=VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.subresourceRange.baseArrayLayer+=index;viewInfo.subresourceRange.layerCount=1;
            if(!cached[index]&&FN(vkCreateImageView)(s->device,&viewInfo,nullptr,&cached[index])!=VK_SUCCESS)return false;
            auto& out=output[eye].resources[i];out=resource;
            out.view=cached[index];out.range=viewInfo.subresourceRange;
        }
    }
    return true;
}
bool eyeAttachmentView(VkDevice d,VkImageView original,uint32_t eye,VkImageView& result){
    result=VK_NULL_HANDLE;if(!nativeProbeEnabled()||eye>1)return false;
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    auto found=s->viewInfos.find(original);if(found==s->viewInfos.end())return false;
    auto info=found->second;
    if(info.viewType!=VK_IMAGE_VIEW_TYPE_2D_ARRAY||info.subresourceRange.layerCount<2)return false;
    auto& cached=s->eyeViews[original][eye];
    if(!cached){info.viewType=VK_IMAGE_VIEW_TYPE_2D;info.subresourceRange.baseArrayLayer+=eye;info.subresourceRange.layerCount=1;
        if(FN(vkCreateImageView)(d,&info,nullptr,&cached)!=VK_SUCCESS)return false;}
    result=cached;return true;
}
void prepare(VkDevice d,const FramePose& pose,const EyeUniforms& uniforms){
    if(!vrEnabled())return;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    s->pendingUniforms=uniforms;s->pendingPose=pose;s->pending=true;
}
void copyCompleted(VkDevice d){if(!vrEnabled())return;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->completed=true;}
void configurePerformanceGpu(VkDevice d,float period,uint32_t count,const VkQueueFamilyProperties* families){
 auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->queueFlags.clear();for(uint32_t i=0;i<count;++i)s->queueFlags.push_back(families[i].queueFlags);if(!argent::perf::enabled())return;s->timestampPeriod=period;s->timestampBits.clear();
 for(uint32_t i=0;i<count;++i){s->timestampBits.push_back(families[i].timestampValidBits);note("PERF_GPU_SUPPORT family="+std::to_string(i)+" validBits="+std::to_string(families[i].timestampValidBits)+" periodNs="+std::to_string(period));}
}
void performanceSubmitted(VkDevice d,VkQueue queue,uint32_t count,const VkCommandBuffer* commands){
 if(!argent::perf::enabled())return;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
 for(uint32_t i=0;i<count;++i){const auto it=s->commands.find(commands[i]);if(it==s->commands.end())continue;auto& c=it->second;
  if(c.gpu.recorded&&c.gpuEnd){c.gpuSubmitted=true;c.gpuSubmitFrame=argent::perf::frame.load(std::memory_order_relaxed);c.gpuQueue=queue;++c.gpuSubmits;}}
}
void performanceReadCompleted(State* s){
 if(!argent::perf::enabled())return;
 for(auto& item:s->commands){auto& c=item.second;if(!c.gpuSubmitted)continue;c.gpuSubmitted=false;
  const auto submits=c.gpuSubmits;c.gpuSubmits=0;std::array<uint64_t,34> ticks{};
  const std::string tag=" recordFrame="+std::to_string(c.gpuRecordFrame)+" submitFrame="+std::to_string(c.gpuSubmitFrame)+" command="+std::to_string(reinterpret_cast<uint64_t>(item.first))+" queue="+std::to_string(reinterpret_cast<uint64_t>(c.gpuQueue))+" family="+std::to_string(c.family);
  if(!c.gpu.read(ticks,c.gpuPoints)){note("PERF_GPU_UNAVAILABLE"+tag);continue;}
  note("PERF_GPU_COMMAND"+tag+" submits="+std::to_string(submits)+" startTick="+std::to_string(ticks[0])+" endTick="+std::to_string(ticks[c.gpuEnd])+" spanMs="+std::to_string(c.gpu.ms(ticks[0],ticks[c.gpuEnd]))+" regionSamples="+std::to_string(c.gpuPassCount)+" regionsSeen="+std::to_string(c.gpuRegionsSeen));
  for(unsigned i=0;i<c.gpuPassCount;++i){const auto& p=c.gpuPasses[i];if(!p.end)continue;
   note("PERF_GPU_PASS"+tag+" compute="+std::to_string(p.compute)+" x="+std::to_string(p.x)+" y="+std::to_string(p.y)+" z="+std::to_string(p.z)+" pass="+std::to_string(p.pass)+" width="+std::to_string(p.width)+" height="+std::to_string(p.height)+" pipelineFirst="+std::to_string(p.pipelineFirst)+" pipelineLast="+std::to_string(p.pipelineLast)+" drawCalls="+std::to_string(p.draws)+" spanMs="+std::to_string(c.gpu.ms(ticks[p.begin],ticks[p.end])));}
 }
 s->census.report([](const std::string& message){note(message);});
}
VkResult beginFrame(VkDevice d,VkSwapchainKHR chain,uint32_t imageIndex){
    if(!vrEnabled())return VK_SUCCESS;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    if(s->pending&&s->completed){
    // The prototype shares a uniform buffer across recorded command buffers.
    // Retire all previous readers before writing; a frame ring can replace this
    // conservative wait once multiple queued game frames have explicit ownership.
    const auto clockNow=[] {return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    const auto retireStart=s->profileTiming?clockNow():0;
    const auto retired=FN(vkDeviceWaitIdle)(d);
    if(retired!=VK_SUCCESS){note("SFS frame parameter retirement failed result="+std::to_string(retired));return retired;}
    const auto uploadStart=s->profileTiming?clockNow():0;
    if(argent::perf::enabled()){
        const auto diagnosticStart=clockNow();performanceReadCompleted(s);argent::perf::nextFrame(s->pendingPose.serial,s->pendingPose.quadView);
        const auto diagnosticMs=double(clockNow()-diagnosticStart)/1000000.;
        if(diagnosticMs>=0.2)note("PERF_READBACK_CPU serial="+std::to_string(s->pendingPose.serial)+" ms="+std::to_string(diagnosticMs));
    }
    void* mapped{};const auto mapResult=FN(vkMapMemory)(d,s->paramsMemory,0,sizeof(EyeUniforms),0,&mapped);
    if(mapResult!=VK_SUCCESS){note("SFS frame parameter map failed result="+std::to_string(mapResult));return mapResult;}
    std::memcpy(mapped,&s->pendingUniforms,sizeof(EyeUniforms));FN(vkUnmapMemory)(d,s->paramsMemory);
    if(s->profileTiming){
        const auto retire=uploadStart-retireStart;
        s->retireNs+=retire;s->uploadNs+=clockNow()-uploadStart;
        if(retire>s->maxRetireNs)s->maxRetireNs=retire;
        if(++s->profiledFrames==120){
            const auto indirectMono=s->indirectMono.exchange(0,std::memory_order_relaxed),indirectStereo=s->indirectStereo.exchange(0,std::memory_order_relaxed),mixed=s->mixedPasses.exchange(0,std::memory_order_relaxed);
            note("[SFS-PATHS] frames=120 indirectShared="+std::to_string(indirectMono)+" indirectStereo="+std::to_string(indirectStereo)+" mixedMonoPasses="+std::to_string(mixed));
            CommandCpuTiming::report([](const std::string& value){note(value);});
            note("RECORDING_CACHE frames=120 commandMisses="+std::to_string(s->commandCacheMisses.exchange(0))+" framebufferMisses="+std::to_string(s->framebufferCacheMisses.exchange(0))+" passMisses="+std::to_string(s->passCacheMisses.exchange(0)));
            note("PIPELINE_CACHE frames=120 misses="+std::to_string(s->pipelineCacheMisses.exchange(0))+" retirement="+std::to_string(s->pipelineRetirement.load()));
            const auto builds=s->pipelineBuilds.exchange(0),buildWall=s->pipelineBuildWallNs.exchange(0),buildCpu=s->pipelineBuildCpuUs.exchange(0),cpuSamples=s->pipelineBuildCpuSamples.exchange(0),buildCompile=s->pipelineBuildCompileNs.exchange(0),buildDriver=s->pipelineBuildDriverNs.exchange(0),buildLock=s->pipelineBuildLockNs.exchange(0),buildMax=s->pipelineBuildMaxNs.exchange(0);
            note("PIPELINE_ACTIVITY frames=120 builds="+std::to_string(builds)+" wallSumMs="+std::to_string(double(buildWall)/1000000.)+" threadCpuLongBuilds="+std::to_string(cpuSamples)+" threadCpuLongSumMs="+std::to_string(double(buildCpu)/1000.)+" compileSumMs="+std::to_string(double(buildCompile)/1000000.)+" driverSumMs="+std::to_string(double(buildDriver)/1000000.)+" lockSumMs="+std::to_string(double(buildLock)/1000000.)+" maxWallMs="+std::to_string(double(buildMax)/1000000.));
            note("DESCRIPTOR_CACHE frames=120 misses="+std::to_string(s->descriptorCacheMisses.exchange(0))+" retirement="+std::to_string(s->pipelineLayoutRetirement.load()));
            const auto samples=CommandCpuTiming::samples.exchange(0,std::memory_order_relaxed);
            const auto wait=CommandCpuTiming::waitNs.exchange(0,std::memory_order_relaxed);
            const auto body=CommandCpuTiming::bodyNs.exchange(0,std::memory_order_relaxed);
            if(samples)note("command CPU timing frames=120 sampleStride=64 samples="+std::to_string(samples)
                +" sampledLookupLockMeanUs="+std::to_string(double(wait)/double(samples)/1000.0)
                +" sampledHookBodyMeanUs="+std::to_string(double(body)/double(samples)/1000.0)
                +" estimatedAggregateHookMsPerFrame="+std::to_string(double(wait+body)*64.0/120000000.0));
            note("parameter timing frames=120 deviceIdleMeanMs="+std::to_string(double(s->retireNs)/120000000.0)
                +" deviceIdleMaxMs="+std::to_string(double(s->maxRetireNs)/1000000.0)
                +" uploadMeanMs="+std::to_string(double(s->uploadNs)/120000000.0));
            s->profiledFrames=s->retireNs=s->uploadNs=s->maxRetireNs=0;
        }
    }
    handDepth::handSceneBeginFrame();s->renderPose=s->pendingPose;s->frameValid=true;s->pending=false;s->completed=false;
    s->renderUniforms=s->pendingUniforms;
    }
    // An acquired image uses the uniforms actually installed above, not the
    // newest pending XR prediction. Other swapchain images retain their pose.
    const auto found=s->swapchains.find(chain);
    if(found!=s->swapchains.end()&&imageIndex<found->second.size()){
        const auto image=found->second[imageIndex];
        if(s->frameValid){s->imagePoses[image]=s->renderPose;s->imageUniforms[image]=s->renderUniforms;}
        else {s->imagePoses.erase(image);s->imageUniforms.erase(image);}
    }
    return VK_SUCCESS;
}
bool pair(VkDevice d,VkImage image,VkExtent2D extent,VkFormat format,StereoFrame& result){
    if(!vrEnabled())return false;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    const auto found=s->imagePoses.find(image);
    if(found==s->imagePoses.end()||s->images.layers(image)!=2)return false;
    auto pose=found->second;
    result={};result.pose=pose;result.generation=pose.serial;
    for(uint32_t e=0;e<2;++e)result.eyes[e]={image,extent,format,sourceLayout(d,image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),pose.views[e].pose,pose.views[e].fov,e,pose.serial};
    return true;
}
void swapchainImages(VkDevice d,VkSwapchainKHR chain,uint32_t count,const VkImage* images){
    if(!nativeProbeEnabled())return;
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    auto& tracked=s->swapchains[chain];
    // Re-enumerating the same swapchain must not discard a valid acquisition.
    if(tracked==std::vector<VkImage>(images,images+count))return;
    for(auto image:tracked){s->imagePoses.erase(image);s->imageUniforms.erase(image);s->images.destroy(d,image,nullptr,nullptr);}
    tracked.assign(images,images+count);
    for(auto image:tracked)s->images.track(image,2);
}
void swapchainDestroyed(VkDevice d,VkSwapchainKHR chain){
    if(!nativeProbeEnabled())return;
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    auto found=s->swapchains.find(chain);if(found==s->swapchains.end())return;
    for(auto image:found->second){s->imagePoses.erase(image);s->imageUniforms.erase(image);s->images.destroy(d,image,nullptr,nullptr);}
    s->swapchains.erase(found);
}
void enableGpuCheckpoints(VkDevice d,PFN_vkGetDeviceProcAddr resolver){
 auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
 s->checkpoint=reinterpret_cast<PFN_vkCmdSetCheckpointNV>(resolver(d,"vkCmdSetCheckpointNV"));
 s->checkpointData=reinterpret_cast<PFN_vkGetQueueCheckpointDataNV>(resolver(d,"vkGetQueueCheckpointDataNV"));
 note("GPU_CHECKPOINTS enabled="+std::to_string(s->checkpoint&&s->checkpointData));
}
void enableGpuFaultReport(VkDevice d,PFN_vkGetDeviceProcAddr resolver){
 auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
 s->deviceFault.query=reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(resolver(d,"vkGetDeviceFaultInfoEXT"));
 note("GPU_FAULT_REPORT enabled="+std::to_string(s->deviceFault.query!=nullptr));
}
void enableWaterRobustness(VkDevice d){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->waterRobustness=true;}
void markXrCheckpoint(VkDevice d,VkCommandBuffer cb,const char* label){
 auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);if(!s->checkpoint)return;
 auto& id=s->xrMarkers[label];if(!id){id=s->markerLabels.size()+1;s->markerLabels.emplace(id,label);}
 s->checkpoint(cb,reinterpret_cast<const void*>(id));
}
void reportGpuCheckpoints(VkDevice d,VkQueue queue) noexcept {
 try{auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);
  bool reported=false;
  s->deviceFault.report(d,[&](const std::string& line){reported=true;note(line);},[](const std::vector<uint8_t>& bytes){
   wchar_t path[32768]{};const auto size=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
   if(!size||size>=32768)throw std::runtime_error("ARGENT_LOG unavailable for GPU fault dump");
   const auto target=std::filesystem::path(std::wstring(path)+L".gpu-fault.bin");
   std::ofstream file(target,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
   if(!file)throw std::runtime_error("Cannot write GPU fault dump");
   note("GPU_FAULT_BINARY bytes="+std::to_string(bytes.size())+" file="+target.filename().string());
  });
  if(reported){wchar_t path[32768]{};auto size=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
   if(size&&size<32768){argent::gpuAddressTrace().save(std::filesystem::path(std::wstring(path)+L".gpu-addresses.tsv"));note("GPU_ADDRESS_TRACE saved");}}
  if(!s->checkpointData)return;
  uint32_t count{};s->checkpointData(queue,&count,nullptr);std::vector<VkCheckpointDataNV> data(count);
  for(auto& row:data)row.sType=VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
  if(count)s->checkpointData(queue,&count,data.data());
  for(uint32_t i=0;i<count;++i){const auto& row=data[i];const auto id=reinterpret_cast<uintptr_t>(row.pCheckpointMarker);const auto label=s->markerLabels.find(id);
   note("GPU_CHECKPOINT queue="+std::to_string(reinterpret_cast<uintptr_t>(queue))+" stage="+std::to_string(row.stage)+" marker="+std::to_string(id)+" "+(label==s->markerLabels.end()?"unknown":label->second));}
 }catch(const std::exception& error){note(std::string("GPU_CHECKPOINT_FAILED ")+error.what());}
}
bool sourceRingRequested(){static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("ARGENT_SFS_SOURCE_RING",value,8)==1&&value[0]=='1';}();return vrEnabled()&&enabled;}
bool configureSourceRing(VkDevice d,PFN_vkGetDeviceProcAddr resolver,const VkPhysicalDeviceMemoryProperties& memory,VkQueue queue,void(*lock)(),void(*unlock)()){
    auto s=state(d);std::unique_lock<std::shared_mutex> guard(s->mutex);
    auto sources=std::make_unique<SourceRing>();if(!sources->initialize(d,queue,resolver,memory,lock,unlock))return false;
    s->sources=std::move(sources);CommandCpuTiming::enabled.store(s->profileTiming&&!argent::perf::enabled(),std::memory_order_relaxed);note("source ring ACTIVE: application-owned stereo images (engine-requested count), GENERAL layout, same-device OpenXR, desktop WSI bypass");return true;
}
bool sourceRingActive(VkDevice d){if(!nativeProbeEnabled())return false;try{return bool(state(d)->sources);}catch(const std::exception&){return false;}}
bool sourceSwapchain(VkDevice d,VkSwapchainKHR chain){return sourceRingActive(d)&&state(d)->sources->owns(chain);}
VkResult createSourceSwapchain(VkDevice d,const VkSwapchainCreateInfoKHR& info,VkSwapchainKHR* output){return state(d)->sources->create(info,output);}
VkResult sourceImages(VkDevice d,VkSwapchainKHR chain,uint32_t* count,VkImage* images){return state(d)->sources->enumerate(chain,count,images);}
VkResult acquireSource(VkDevice d,VkSwapchainKHR chain,uint64_t timeout,VkSemaphore sem,VkFence fence,uint32_t* index){return state(d)->sources->acquire(chain,timeout,sem,fence,index);}
VkResult presentSource(VkDevice d,VkQueue queue,const VkPresentInfoKHR& info,bool consumed){return state(d)->sources->present(queue,info,consumed);}
void destroySourceSwapchain(VkDevice d,VkSwapchainKHR chain){if(state(d)->sources->destroy(chain)!=VK_SUCCESS)commandFailure("SFS source destruction retirement failed");}
VkImageLayout sourceLayout(VkDevice d,VkImage image,VkImageLayout layout){
    if(layout!=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR||!sourceRingActive(d))return layout;
    return state(d)->sources->ownsImage(image)?VK_IMAGE_LAYOUT_GENERAL:layout;
}
PFN_vkVoidFunction wrapProc(VkDevice d,const char* name,PFN_vkVoidFunction next){if(!nativeProbeEnabled()||!next)return next;
    if(d)try{state(d);}catch(const std::exception&){return next;}
#define HOOK(api,handler) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(&handler)
    HOOK(vkCreateBuffer,captureCreateBuffer);HOOK(vkDestroyBuffer,captureDestroyBuffer);HOOK(vkUpdateDescriptorSets,captureUpdateSets);
    HOOK(vkCreateQueryPool,createQueryPool);HOOK(vkDestroyQueryPool,destroyQueryPool);
    HOOK(vkResetQueryPool,resetQueryPool);HOOK(vkResetQueryPoolEXT,resetQueryPoolEXT);HOOK(vkCmdResetQueryPool,resetQueryCommands);
    HOOK(vkCmdWriteTimestamp,timestamp);HOOK(vkCmdWriteTimestamp2,timestamp2);HOOK(vkCmdWriteTimestamp2KHR,timestamp2KHR);
    HOOK(vkGetQueryPoolResults,queryResults);HOOK(vkCmdCopyQueryPoolResults,copyQueryResults);
    HOOK(vkCmdBeginQuery,beginQuery);HOOK(vkCmdEndQuery,endQuery);
    HOOK(vkCreateShaderModule,createShader);HOOK(vkDestroyShaderModule,destroyShader);
    HOOK(vkCreateImage,createImage);HOOK(vkDestroyImage,destroyImage);HOOK(vkCreateImageView,createView);HOOK(vkDestroyImageView,destroyView);
    HOOK(vkCreateRenderPass,createPass);HOOK(vkDestroyRenderPass,destroyPass);HOOK(vkCreateFramebuffer,createFramebuffer);HOOK(vkDestroyFramebuffer,destroyFramebuffer);
    HOOK(vkCreatePipelineLayout,createPipelineLayout);HOOK(vkDestroyPipelineLayout,destroyPipelineLayout);
    HOOK(vkCreateDescriptorSetLayout,createLayout);HOOK(vkDestroyDescriptorSetLayout,destroyLayout);HOOK(vkCreateDescriptorPool,createPool);HOOK(vkAllocateDescriptorSets,allocateSets);
    HOOK(vkFreeDescriptorSets,freeSets);HOOK(vkResetDescriptorPool,resetPool);HOOK(vkDestroyDescriptorPool,destroyPool);
    HOOK(vkCreateGraphicsPipelines,graphics);HOOK(vkCreateComputePipelines,compute);HOOK(vkDestroyPipeline,destroyPipeline);
    HOOK(vkResetCommandBuffer,captureResetCommand);HOOK(vkResetCommandPool,captureResetPool);
    HOOK(vkCreateCommandPool,createCommandPool);HOOK(vkEndCommandBuffer,endCommand);
    // F8 sky diagnostics are explicitly opt-in, including clean releases.
    // Do not use the performance-build gate for capture instrumentation.
    if(!argent::cleanRelease||!WaterGpuCapture::requestedRoot().empty()){HOOK(vkCmdDraw,draw);HOOK(vkCmdDrawIndexed,drawIndexed);HOOK(vkCmdDrawIndirect,drawIndirect);HOOK(vkCmdDrawIndexedIndirect,drawIndexedIndirect);
        HOOK(vkCmdDrawIndirectCount,drawIndirectCount);HOOK(vkCmdDrawIndexedIndirectCount,drawIndexedIndirectCount);
        HOOK(vkCmdDrawIndirectCountKHR,drawIndirectCountKHR);HOOK(vkCmdDrawIndexedIndirectCountKHR,drawIndexedIndirectCountKHR);
        HOOK(vkCmdDrawIndirectCountAMD,drawIndirectCountAMD);HOOK(vkCmdDrawIndexedIndirectCountAMD,drawIndexedIndirectCountAMD);
    }
    HOOK(vkBeginCommandBuffer,beginCommand);HOOK(vkCmdBindPipeline,bindPipeline);HOOK(vkCmdBindDescriptorSets,bindSets);HOOK(vkCmdBindVertexBuffers,bindVertices);HOOK(vkCmdBindIndexBuffer,bindIndex);
    HOOK(vkAllocateCommandBuffers,allocateCommands);HOOK(vkFreeCommandBuffers,freeCommands);HOOK(vkDestroyCommandPool,destroyCommandPool);
    HOOK(vkCmdSetViewport,viewport);HOOK(vkCmdSetScissor,scissor);HOOK(vkCmdPushConstants,push);HOOK(vkCmdBeginRenderPass,beginPass);HOOK(vkCmdEndRenderPass,endPass);HOOK(vkCmdDispatch,dispatch);HOOK(vkCmdDispatchIndirect,dispatchIndirect);
    HOOK(vkCmdNextSubpass,nextPass);HOOK(vkCmdSetLineWidth,lineWidth);HOOK(vkCmdSetDepthBias,depthBias);HOOK(vkCmdSetBlendConstants,blendConstants);HOOK(vkCmdSetDepthBounds,depthBounds);
    HOOK(vkCmdSetStencilCompareMask,stencilCompare);HOOK(vkCmdSetStencilWriteMask,stencilWrite);HOOK(vkCmdSetStencilReference,stencilReference);
    HOOK(vkCmdPipelineBarrier,barriers);HOOK(vkCmdClearColorImage,clearColor);HOOK(vkCmdClearDepthStencilImage,clearDepth);
    HOOK(vkCmdCopyImage,copyImage);HOOK(vkCmdBlitImage,blitImage);HOOK(vkCmdResolveImage,resolveImage);HOOK(vkCmdCopyBufferToImage,uploadImage);
#undef HOOK
    return next;
}
}
