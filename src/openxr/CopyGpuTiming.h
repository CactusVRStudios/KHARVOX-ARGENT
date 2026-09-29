#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

namespace kharvox {
// Optional timestamps around the XR command buffer, read only after its existing
// completion boundary. Never add a GPU wait merely to obtain profiling data.
struct CopyGpuTiming {
    VkDevice device{}; VkQueryPool pool{};
    PFN_vkDestroyQueryPool destroy{};
    PFN_vkCmdResetQueryPool reset{};
    PFN_vkCmdWriteTimestamp stamp{};
    PFN_vkGetQueryPoolResults results{};
    float period{}; uint32_t bits{}; bool recorded{};
    void initialize(VkDevice d,PFN_vkGetDeviceProcAddr resolver,float ns,uint32_t validBits) {
        if(pool||!resolver||!validBits||validBits>64||ns<=0)return;
        device=d;period=ns;bits=validBits;
        auto create=reinterpret_cast<PFN_vkCreateQueryPool>(resolver(d,"vkCreateQueryPool"));
        destroy=reinterpret_cast<PFN_vkDestroyQueryPool>(resolver(d,"vkDestroyQueryPool"));
        reset=reinterpret_cast<PFN_vkCmdResetQueryPool>(resolver(d,"vkCmdResetQueryPool"));
        stamp=reinterpret_cast<PFN_vkCmdWriteTimestamp>(resolver(d,"vkCmdWriteTimestamp"));
        results=reinterpret_cast<PFN_vkGetQueryPoolResults>(resolver(d,"vkGetQueryPoolResults"));
        if(!create||!destroy||!reset||!stamp||!results)return;
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=2;
        if(create(d,&info,nullptr,&pool)!=VK_SUCCESS)pool=VK_NULL_HANDLE;
    }
    void begin(VkCommandBuffer command,bool sample=true) {
        recorded=bool(pool)&&sample;if(!recorded)return;
        reset(command,pool,0,2);
        stamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool,0);
    }
    void end(VkCommandBuffer command) {
        if(recorded)stamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pool,1);
    }
    bool completed(double& milliseconds) {
        if(!recorded)return false;recorded=false;
        uint64_t values[2]{};
        if(results(device,pool,0,2,sizeof(values),values,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)!=VK_SUCCESS)return false;
        const uint64_t mask=bits==64?UINT64_MAX:((uint64_t{1}<<bits)-1);
        milliseconds=double((values[1]-values[0])&mask)*double(period)/1000000.0;
        return true;
    }
    void shutdownAfterCompletion() {if(pool)destroy(device,pool,nullptr);pool=VK_NULL_HANDLE;recorded=false;}
};
}
