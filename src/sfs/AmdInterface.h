#pragma once
#include <spirv.hpp>
#include <map>
#include <set>
#include <regex>
#include <string>
#include <vector>
namespace argent::sfs {
// Eternal packs explicitly interpolated AMD inputs beside ordinary inputs.
// GLSL rejects the mixed qualifiers at one location, although the original
// SPIR-V expresses them per variable. Give the GLSL frontend temporary input
// locations, then restore EXACT native locations before creating any module.
// No interpolation qualifier or component is removed; vertex outputs stay put.
struct AmdInterface {
    std::string source;
    std::map<uint32_t,uint32_t> restore;
    explicit AmdInterface(const std::string& text):source(text){
        if(text.find("__explicitInterpAMD")==std::string::npos)return;
        static const std::regex decl(R"(layout\(location = ([0-9]+), component = ([0-9]+)\) ([^;\n]*?)in (float|vec[234]) ([A-Za-z_][A-Za-z_0-9]*);)");
        static const std::regex location(R"(layout\(location = ([0-9]+))");
        std::set<uint32_t> occupied;std::map<uint32_t,unsigned> kinds;
        for(std::sregex_iterator i(text.begin(),text.end(),location),end;i!=end;++i)occupied.insert(std::stoul((*i)[1]));
        for(std::sregex_iterator i(text.begin(),text.end(),decl),end;i!=end;++i){
            auto m=*i;kinds[std::stoul(m[1])]|=m[3].str().find("__explicitInterpAMD")!=std::string::npos?1u:2u;
        }
        struct Edit{size_t offset,length;std::string value;};std::vector<Edit> edits;
        for(std::sregex_iterator i(text.begin(),text.end(),decl),end;i!=end;++i){
            auto m=*i;const auto original=uint32_t(std::stoul(m[1]));if(kinds[original]!=3)continue;
            uint32_t temporary=0;while(occupied.count(temporary))++temporary;occupied.insert(temporary);
            restore.emplace(temporary,original);
            edits.push_back({size_t(m.position(1)),size_t(m.length(1)),std::to_string(temporary)});
        }
        for(auto i=edits.rbegin();i!=edits.rend();++i)source.replace(i->offset,i->length,i->value);
    }
    void restoreLocations(std::vector<uint32_t>& words)const{
        if(restore.empty())return;
        std::set<uint32_t> inputs;
        for(size_t p=5;p<words.size();p+=words[p]>>16)
            if(spv::Op(words[p]&65535)==spv::OpVariable&&words[p+3]==spv::StorageClassInput)inputs.insert(words[p+2]);
        for(size_t p=5;p<words.size();p+=words[p]>>16){
            if(spv::Op(words[p]&65535)!=spv::OpDecorate||words[p+2]!=spv::DecorationLocation||!inputs.count(words[p+1]))continue;
            auto found=restore.find(words[p+3]);if(found!=restore.end())words[p+3]=found->second;
        }
    }
};
}
