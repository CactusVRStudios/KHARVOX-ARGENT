#pragma once
#include "ShaderIdentity.h"
#include <vulkan/vulkan.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>

namespace kharvox::sfs {
struct ProfileShader {
    std::filesystem::path path;
    std::vector<uint32_t> words;
    explicit operator bool() const {return !words.empty();}
};
inline const char* stageSuffix(VkShaderStageFlagBits stage) {
    switch(stage){
    case VK_SHADER_STAGE_VERTEX_BIT:return "VS.vert.spv";
    case VK_SHADER_STAGE_FRAGMENT_BIT:return "PS.frag.spv";
    case VK_SHADER_STAGE_COMPUTE_BIT:return "CS.comp.spv";
    default:return nullptr;
    }
}
// Select exact pipeline variant first, then the stage's generic replacement.
// Compilation/injection is a separate step. A malformed exact replacement must
// not silently select a different shader, which could turn shadow work stereo.
inline ProfileShader loadProfileShader(const std::filesystem::path& root,
                                      uint64_t primary,uint64_t variant,
                                      VkShaderStageFlagBits stage) {
    const auto suffix=stageSuffix(stage);
    if(root.empty()||!suffix)return {};
    auto path=root/(shaderKey(primary)+"_"+shaderKey(variant)+"_"+suffix);
    if(!std::filesystem::exists(path))path=root/(shaderKey(primary)+"_"+suffix);
    if(!std::filesystem::exists(path))return {};
    const auto size=std::filesystem::file_size(path);
    if(size<20||size>16*1024*1024||size%4)throw std::runtime_error("Invalid SFS profile SPIR-V size");
    ProfileShader result{path,std::vector<uint32_t>(size/4)};
    std::ifstream file(path,std::ios::binary);
    if(!file.read(reinterpret_cast<char*>(result.words.data()),size)||result.words[0]!=0x07230203)
        throw std::runtime_error("Invalid SFS profile SPIR-V file");
    const uint32_t model=stage==VK_SHADER_STAGE_VERTEX_BIT?0:stage==VK_SHADER_STAGE_FRAGMENT_BIT?4:5;
    bool entry=false;
    for(size_t i=5;i<result.words.size();){
        const uint32_t n=result.words[i]>>16,op=result.words[i]&0xffff;
        if(!n||n>result.words.size()-i)throw std::runtime_error("Truncated SFS profile instruction");
        if(op==15&&n>=4&&result.words[i+1]==model)entry=true;
        i+=n;
    }
    if(!entry)throw std::runtime_error("SFS profile execution model does not match pipeline stage");
    return result;
}
}
