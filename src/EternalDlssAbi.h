#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace argent::dlss {
// Audited against the supported Eternal executable's resource constructor
// (1cc5a40) and evaluation helper (1cc7aa0). Not a modern SDK structure cast.
struct Resource {
    VkImageView view{};
    VkImage image{};
    VkImageSubresourceRange range{};
    VkFormat format{};
    uint32_t width{},height{},type{};
    bool readWrite{};
    std::array<uint8_t,3> padding{};
};
static_assert(sizeof(Resource)==0x38&&offsetof(Resource,type)==0x30);
struct alignas(8) Parameters {
    std::array<uint8_t,0x168> bytes{};
    template<class T>T get(size_t offset) const {T value;std::memcpy(&value,bytes.data()+offset,sizeof(value));return value;}
    template<class T>void set(size_t offset,const T& value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
};
inline constexpr std::array<size_t,30> resourceOffsets={
    0,8,0x18,0x20,0x48,0x50,0x58,
    0xa0,0xa8,0xb0,0xb8,0xc0,0xc8,0xd0,0xd8,0xe0,0xe8,0xf0,0xf8,0x100,0x108,0x110,0x118,
    0x128,0x130,0x138,0x140,0x148,0x158,0x160};
struct EyeParameters {
    Parameters params;
    std::array<Resource,resourceOffsets.size()> resources{};
};
// Own all substituted pointers for the synchronous native evaluation. Never
// mutate the engine's descriptor structs or shared parameter backing storage.
template<class Resolve>bool prepareEyes(const Parameters& input,std::array<EyeParameters,2>& eyes,Resolve&& resolve,bool reset){
    std::array<const Resource*,resourceOffsets.size()> originals{};
    for(size_t i=0;i<resourceOffsets.size();++i){
        originals[i]=input.get<const Resource*>(resourceOffsets[i]);
        if(i<4&&!originals[i])return false;
        if(originals[i]&&(originals[i]->type!=0||!originals[i]->view||!originals[i]->image))return false;
    }
    for(auto& eye:eyes){eye.params=input;if(reset)eye.params.set<int>(0x38,1);}
    if(!resolve(originals,eyes))return false;
    for(size_t i=0;i<originals.size();++i)if(originals[i])for(auto& eye:eyes)
        eye.params.set<const Resource*>(resourceOffsets[i],&eye.resources[i]);
    return true;
}
}
