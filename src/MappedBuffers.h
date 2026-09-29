#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <mutex>

namespace argent {
// Read only ranges returned by Vulkan, never guessed engine addresses.
class MappedBuffers {
    struct Memory {VkDeviceSize size{},offset{},length{};const unsigned char* data{};};
    struct Buffer {VkDeviceSize size{},offset{};VkDeviceMemory memory{};VkBufferUsageFlags usage{};};
    std::mutex mutex;
    std::unordered_map<VkDeviceMemory,Memory> memories;
    std::unordered_map<VkBuffer,Buffer> buffers;
public:
    void allocated(VkDeviceMemory m,VkDeviceSize size){std::lock_guard<std::mutex> l(mutex);memories[m]={size};}
    void freed(VkDeviceMemory m){std::lock_guard<std::mutex> l(mutex);memories.erase(m);for(auto& e:buffers)if(e.second.memory==m)e.second.memory=VK_NULL_HANDLE;}
    void mapped(VkDeviceMemory m,VkDeviceSize offset,VkDeviceSize size,const void* data){std::lock_guard<std::mutex> l(mutex);auto it=memories.find(m);if(it==memories.end()||offset>it->second.size)return;auto& r=it->second;auto length=size==VK_WHOLE_SIZE?r.size-offset:size;if(length>r.size-offset)return;r.offset=offset;r.length=length;r.data=static_cast<const unsigned char*>(data);}
    void unmapped(VkDeviceMemory m){std::lock_guard<std::mutex> l(mutex);auto i=memories.find(m);if(i!=memories.end()){i->second.data=nullptr;i->second.length=0;}}
    void created(VkBuffer b,VkDeviceSize size,VkBufferUsageFlags usage=0){std::lock_guard<std::mutex> l(mutex);buffers[b]={size,0,VK_NULL_HANDLE,usage};}
    bool copyable(VkBuffer b,VkDeviceSize offset,VkDeviceSize size){std::lock_guard<std::mutex> l(mutex);auto i=buffers.find(b);return i!=buffers.end()&&i->second.memory&&memories.count(i->second.memory)&&(i->second.usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)&&offset<=i->second.size&&size<=i->second.size-offset;}
    void destroyed(VkBuffer b){std::lock_guard<std::mutex> l(mutex);buffers.erase(b);}
    void bound(VkBuffer b,VkDeviceMemory m,VkDeviceSize offset){std::lock_guard<std::mutex> l(mutex);auto i=buffers.find(b);if(i!=buffers.end()){i->second.memory=m;i->second.offset=offset;}}
    bool read(VkBuffer b,VkDeviceSize offset,size_t size,std::vector<unsigned char>& out){
        std::lock_guard<std::mutex> l(mutex);auto bi=buffers.find(b);if(bi==buffers.end())return false;const auto& buffer=bi->second;
        if(offset>buffer.size||size>buffer.size-offset)return false;
        auto mi=memories.find(buffer.memory);if(mi==memories.end())return false;const auto& mem=mi->second;
        if(!mem.data||buffer.offset>mem.size||offset>mem.size-buffer.offset)return false;
        auto absolute=buffer.offset+offset;if(absolute<mem.offset)return false;auto relative=absolute-mem.offset;
        if(relative>mem.length||size>mem.length-relative)return false;
        out.resize(size);std::memcpy(out.data(),mem.data+relative,size);return true;
    }
};
}
