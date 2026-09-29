#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
namespace argent::sfs {
// A private creation-only cache: never serialize the engine's shared cache or
// inspect a destroyed pipeline after device loss. Failure falls back to the
// caller's cache. No GPU commands, fences or gameplay readbacks are added.
class MaterialPipelineCapture {
    VkDevice device_{};
    VkPipelineCache cache_{};
    PFN_vkGetPipelineCacheData get_{};
    PFN_vkDestroyPipelineCache destroy_{};
public:
    MaterialPipelineCapture(VkDevice device,PFN_vkGetDeviceProcAddr resolve,bool enabled):device_(device){
        if(!enabled)return;
        auto create=reinterpret_cast<PFN_vkCreatePipelineCache>(resolve(device,"vkCreatePipelineCache"));
        get_=reinterpret_cast<PFN_vkGetPipelineCacheData>(resolve(device,"vkGetPipelineCacheData"));
        destroy_=reinterpret_cast<PFN_vkDestroyPipelineCache>(resolve(device,"vkDestroyPipelineCache"));
        if(!create||!get_||!destroy_)return;
        VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        if(create(device,&info,nullptr,&cache_)!=VK_SUCCESS)cache_=VK_NULL_HANDLE;
    }
    MaterialPipelineCapture(const MaterialPipelineCapture&)=delete;
    MaterialPipelineCapture& operator=(const MaterialPipelineCapture&)=delete;
    ~MaterialPipelineCapture(){if(cache_)destroy_(device_,cache_,nullptr);}
    bool active()const{return cache_!=VK_NULL_HANDLE;}
    VkPipelineCache cache(VkPipelineCache fallback)const{return cache_?cache_:fallback;}
    std::vector<char> data()const{
        if(!cache_)return {};
        size_t size{};
        if(get_(device_,cache_,&size,nullptr)!=VK_SUCCESS||size>16u*1024*1024)throw std::runtime_error("Material pipeline cache size unavailable/too large");
        std::vector<char> result(size);
        if(!size)return result;
        if(get_(device_,cache_,&size,result.data())!=VK_SUCCESS||size>result.size())throw std::runtime_error("Material pipeline cache incomplete");
        result.resize(size);return result;
    }
};
}
