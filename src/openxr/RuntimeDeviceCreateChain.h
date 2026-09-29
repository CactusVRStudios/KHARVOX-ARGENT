#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <vector>

namespace argent {
// Nested device creation may temporarily relink shared application/loader nodes.
// The outer loader walks its original chain again after our layer returns.
class RestoreDeviceCreateLinks {
    struct Link {VkBaseOutStructure* node;VkBaseOutStructure* next;};
    std::vector<Link> links;
public:
    explicit RestoreDeviceCreateLinks(const void* chain){
        for(auto node=static_cast<const VkBaseInStructure*>(chain);node;node=node->pNext)
            links.push_back({reinterpret_cast<VkBaseOutStructure*>(const_cast<VkBaseInStructure*>(node)),
                reinterpret_cast<VkBaseOutStructure*>(const_cast<VkBaseInStructure*>(node->pNext))});
    }
    ~RestoreDeviceCreateLinks(){for(auto& link:links)if(link.node->pNext!=link.next)link.node->pNext=link.next;}
    RestoreDeviceCreateLinks(const RestoreDeviceCreateLinks&)=delete;
    RestoreDeviceCreateLinks& operator=(const RestoreDeviceCreateLinks&)=delete;
};
// XR may add feature structs. Preserve its entire chain and extension list;
// reattach only the loader records belonging to our downstream create call.
class RuntimeDeviceCreateChain {
    std::vector<VkLayerDeviceCreateInfo> loader;
public:
    VkDeviceCreateInfo info;
    RuntimeDeviceCreateChain(const VkDeviceCreateInfo& runtime,const VkDeviceCreateInfo& downstream):info(runtime){
        for(auto n=static_cast<const VkBaseInStructure*>(downstream.pNext);n;n=n->pNext)
            if(n->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)
                loader.push_back(*reinterpret_cast<const VkLayerDeviceCreateInfo*>(n));
        for(auto it=loader.rbegin();it!=loader.rend();++it){it->pNext=info.pNext;info.pNext=&*it;}
    }
    RuntimeDeviceCreateChain(const RuntimeDeviceCreateChain&)=delete;
    RuntimeDeviceCreateChain& operator=(const RuntimeDeviceCreateChain&)=delete;
};
}
