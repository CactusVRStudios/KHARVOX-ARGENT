#pragma once
#include "QuadRuntime.h"
#include "MappedBuffers.h"
#include "PoolMembers.h"
#include "sfs/RecordingCache.h"
#include "sfs/DescriptorCountCache.h"
#include <map>
#include <unordered_map>
#include <array>
#include <shared_mutex>
#include <atomic>
namespace argent {
class CameraCapture {
    friend struct CameraCaptureTestAccess;
    struct Binding {VkBuffer buffer{};VkDeviceSize offset{},range{};bool dynamic{};};
    struct Layout {std::vector<std::pair<uint32_t,uint32_t>> dynamic;};
    struct Pipeline {uint32_t binding{},pushOffset{};};
    struct Event {enum Type{PipelineBind,Sets,Push,Draw,Copy} type;uint64_t a{},b{},c{};std::vector<uint64_t> values;};
    struct ProbeBinding {VkPipeline pipeline{};VkDescriptorSet set{};std::vector<uint32_t> dynamic;};
    struct ProbeSample {uint64_t epoch{};ProbeBinding bound;};
    struct Command {VkCommandPool pool{};std::vector<Event> events;VkPipeline livePipeline{};bool liveCameraPipeline{};VkDescriptorSet liveSet{};uint32_t liveDynamic{};std::vector<std::pair<VkDescriptorSet,uint32_t>> commonCandidates;std::array<ProbeBinding,2> probeBindings;std::vector<ProbeSample> probeSamples;};
    inline static std::atomic<uint64_t> nextIdentity{1};
    const uint64_t identity=nextIdentity.fetch_add(1,std::memory_order_relaxed);
    std::atomic<uint64_t> commandRetirement{1},layoutRetirement{1};
    std::unordered_map<VkPipelineLayout,uint32_t> cameraDynamicIndices;
    Command& localCommand(VkCommandBuffer);
    // Vulkan callers externally synchronize recording/submission/retirement of
    // each command buffer. Warm VR draw/bind paths borrow only that buffer's
    // stable map entry. Shared metadata and diagnostic probes retain locking.
    std::shared_mutex mutex;
    std::unordered_map<VkDescriptorSetLayout,Layout> layouts;
    std::unordered_map<VkDescriptorSet,std::map<std::pair<uint32_t,uint32_t>,Binding>> sets;
    std::unordered_map<VkDescriptorSet,VkDescriptorSetLayout> setLayouts;
    std::unordered_map<VkDescriptorSet,std::map<std::pair<uint32_t,uint32_t>,VkDescriptorImageInfo>> probeImages;
    size_t waterCaptureBytes{};
    PoolMembers<VkDescriptorPool,VkDescriptorSet> setPools;
    std::unordered_map<VkPipeline,Pipeline> pipelines;
    std::unordered_map<VkCommandBuffer,Command> commands;
    std::unordered_map<VkPipeline,std::string> probePipelines;
    std::atomic<uint64_t> probeEpoch{};
    uint64_t probePoll{},probeEnd{},probeOutputEpoch{};size_t probeCount{};
    std::vector<std::pair<std::string,VkDescriptorSet>> probeWritten;
    void sampleProbe(Command&,unsigned point);
    void submitProbe(uint32_t,const VkCommandBuffer*);
    uint64_t lastFrame=UINT64_MAX;size_t captured{};
    struct Readback {VkBuffer buffer;VkDeviceSize offset;VkPipeline pipeline;uint32_t binding,index;VkDeviceSize size{64};std::filesystem::path output;};
    std::vector<Readback> pendingReadback;
    std::vector<Readback> pendingProbeReadback;
    std::atomic<bool> probeGpuReady{};
    // Snapshot at submit: the game's mapped upload storage may be recycled
    // before the next swapchain acquire. Never retain its address for rereading.
    sfs::Matrix liveProjection{};
    bool liveProjectionValid{};
    uint64_t liveProjectionTick{};
    void append(VkCommandBuffer,Event);
public:
    MappedBuffers memory;
    static bool enabled();
    bool projection(sfs::Matrix&,uint64_t maxAgeMs=0);
    void layout(VkDescriptorSetLayout,const VkDescriptorSetLayoutCreateInfo*);
    void pipelineLayout(VkPipelineLayout,const VkPipelineLayoutCreateInfo*);
    void retirePipelineLayout(VkPipelineLayout);
    void allocate(const VkDescriptorSetAllocateInfo*,const VkDescriptorSet*);
    void update(uint32_t,const VkWriteDescriptorSet*,uint32_t,const VkCopyDescriptorSet*);
    void retirePool(VkDescriptorPool);
    void retireSets(uint32_t,const VkDescriptorSet*);
    void pipeline(VkPipeline,uint32_t binding,uint32_t pushOffset);
    void probePipeline(VkPipeline,const std::string& shaderIdentity);
    void destroyPipeline(VkPipeline);
    void command(VkCommandBuffer,VkCommandPool=VK_NULL_HANDLE);
    void freeCommand(VkCommandBuffer);
    void resetPool(VkCommandPool,bool destroy);
    void bindPipeline(VkCommandBuffer,VkPipeline,VkPipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS);
    void bindSets(VkCommandBuffer,uint32_t,uint32_t,const VkDescriptorSet*,uint32_t,const uint32_t*,VkPipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,VkPipelineLayout=VK_NULL_HANDLE);
    void dispatch(VkCommandBuffer);
    void push(VkCommandBuffer,uint32_t,uint32_t,const void*);
    void draw(VkCommandBuffer);
    void copy(VkCommandBuffer,VkBuffer,VkBuffer,uint32_t,const VkBufferCopy*);
    void submit(VkQueue,uint32_t,const VkCommandBuffer*);
    bool gpuProbePending() const{return probeGpuReady.load(std::memory_order_relaxed);}
    void readback(Device&,VkQueue,uint32_t family,bool probeOnly=false);
};
}
