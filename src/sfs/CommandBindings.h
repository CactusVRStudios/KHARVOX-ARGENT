#pragma once
#include "NativeDispatch.h"
#include <array>
#include <optional>
#include <vector>

namespace kharvox::sfs {
// Vulkan externally synchronizes each command buffer. Keep its replay state as
// values, retaining vector capacity across recordings; no owning callbacks or
// tree nodes are allocated on each bind. Replay order matches the former keys.
struct CommandBindings {
    template<class T> struct Entry {T value{};bool valid{};};
    struct Vertex {VkBuffer buffer{};VkDeviceSize offset{};};
    struct Index {VkBuffer buffer{};VkDeviceSize offset{};VkIndexType type{};};
    std::vector<Entry<Vertex>> vertices;
    std::vector<Entry<VkViewport>> viewports;
    std::vector<Entry<VkRect2D>> scissors;
    std::optional<Index> index;
    std::optional<float> line;
    std::optional<std::array<float,3>> bias;
    std::optional<std::array<float,4>> blend;
    std::optional<std::array<float,2>> bounds;
    std::array<std::array<Entry<uint32_t>,2>,3> stencil{};

    template<class T> static void set(std::vector<Entry<T>>& entries,uint32_t slot,const T& value){
        if(entries.size()<=slot)entries.resize(size_t(slot)+1);
        entries[slot]={value,true};
    }
    void clear(){
        for(auto& e:vertices)e.valid=false;
        for(auto& e:viewports)e.valid=false;
        for(auto& e:scissors)e.valid=false;
        index.reset();line.reset();bias.reset();blend.reset();bounds.reset();
        for(auto& kind:stencil)for(auto& face:kind)face.valid=false;
    }
    void setStencil(unsigned kind,VkStencilFaceFlags faces,uint32_t value){
        for(unsigned i=0;i<2;++i)if(faces&(1u<<i))stencil[kind][i]={value,true};
    }
    void replay(const NativeDispatch& dispatch,VkCommandBuffer cb)const{
#define REPLAY(name, ...) NativeDispatch::require(dispatch.name,#name)(cb,__VA_ARGS__)
        for(uint32_t i=0;i<vertices.size();++i)if(vertices[i].valid){const auto& v=vertices[i].value;REPLAY(vkCmdBindVertexBuffers,i,1,&v.buffer,&v.offset);}
        if(index)REPLAY(vkCmdBindIndexBuffer,index->buffer,index->offset,index->type);
        for(uint32_t i=0;i<viewports.size();++i)if(viewports[i].valid)REPLAY(vkCmdSetViewport,i,1,&viewports[i].value);
        for(uint32_t i=0;i<scissors.size();++i)if(scissors[i].valid)REPLAY(vkCmdSetScissor,i,1,&scissors[i].value);
        if(line)REPLAY(vkCmdSetLineWidth,*line);
        if(bias)REPLAY(vkCmdSetDepthBias,(*bias)[0],(*bias)[1],(*bias)[2]);
        if(blend)REPLAY(vkCmdSetBlendConstants,blend->data());
        if(bounds)REPLAY(vkCmdSetDepthBounds,(*bounds)[0],(*bounds)[1]);
        const std::array<PFN_vkCmdSetStencilReference,3> setters{
            dispatch.vkCmdSetStencilCompareMask,dispatch.vkCmdSetStencilWriteMask,dispatch.vkCmdSetStencilReference};
        for(unsigned kind=0;kind<3;++kind)for(unsigned face=0;face<2;++face)
            if(stencil[kind][face].valid)NativeDispatch::require(setters[kind],"stencil replay")(cb,1u<<face,stencil[kind][face].value);
#undef REPLAY
    }
};
}
