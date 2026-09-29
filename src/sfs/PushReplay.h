#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <cstring>
#include <stdexcept>

namespace kharvox::sfs {
// Preserve partial/stage-specific writes, but replay contiguous words with the
// same layout in one command. No captured function or allocation per word.
class PushReplay {
    struct Stage {std::vector<uint32_t> words;std::vector<VkPipelineLayout> layouts;};
    std::array<Stage,6> stages_;
public:
    void clear(){for(auto& stage:stages_){stage.words.clear();stage.layouts.clear();}}
    void write(VkPipelineLayout layout,VkShaderStageFlags flags,uint32_t offset,uint32_t size,const void* bytes){
        // Eternal uses Vulkan's ALL sentinel, which includes reserved/future
        // bits. Replay the six stages supported by this VS/FS/CS renderer.
        if(flags==VK_SHADER_STAGE_ALL)flags=0x3fu;
        if(!layout||(offset|size)&3u||offset>65536||size>65536-offset||flags&~0x3fu)
            throw std::runtime_error("Unsupported SFS push constant range");
        for(unsigned bit=0;bit<stages_.size();++bit)if(flags&(1u<<bit)){
            auto& stage=stages_[bit];const auto end=(offset+size)/4;
            if(stage.words.size()<end){stage.words.resize(end);stage.layouts.resize(end);}
            if(size)std::memcpy(stage.words.data()+offset/4,bytes,size);
            for(auto word=offset/4;word<end;++word)stage.layouts[word]=layout;
        }
    }
    template<class Emit>uint64_t replay(Emit&& emit)const {
        uint64_t calls{};
        for(unsigned bit=0;bit<stages_.size();++bit){const auto& stage=stages_[bit];
            for(size_t first=0;first<stage.words.size();){
                const auto layout=stage.layouts[first];if(!layout){++first;continue;}
                size_t end=first+1;while(end<stage.words.size()&&stage.layouts[end]==layout)++end;
                emit(layout,VkShaderStageFlags(1u<<bit),uint32_t(first*4),uint32_t((end-first)*4),stage.words.data()+first);
                ++calls;first=end;
            }
        }
        return calls;
    }
};
}
