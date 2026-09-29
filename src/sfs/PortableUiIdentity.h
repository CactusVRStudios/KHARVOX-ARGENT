#pragma once
#include "ShaderIdentity.h"
#include <spirv.hpp>
#include <vector>
namespace argent::sfs {
// Eternal strips the semantic member names used by KHARVOX's DOOM 2016 UI
// classifier. A similar-looking UBO is therefore insufficient evidence here.
// Recognize only known UI instruction streams with different debug metadata.
// Preserve IDs, constants, decorations, bindings and every executable opcode.
inline uint64_t portableUiFingerprint(const std::vector<uint32_t>& words){
    if(words.size()<5||words[0]!=spv::MagicNumber)return 0;
    auto canonical=std::vector<uint32_t>(words.begin(),words.begin()+5);
    canonical[2]=0; // Generator metadata has no execution semantics.
    for(size_t p=5;p<words.size();){
        const auto count=words[p]>>16;
        if(!count||count>words.size()-p)return 0;
        const auto op=spv::Op(words[p]&0xffffu);
        const bool debug=op==spv::OpName||op==spv::OpMemberName||op==spv::OpSource||
            op==spv::OpSourceExtension||op==spv::OpSourceContinued||op==spv::OpLine||op==spv::OpNoLine||op==spv::OpModuleProcessed;
        if(!debug)canonical.insert(canonical.end(),words.begin()+p,words.begin()+p+count);
        p+=count;
    }
    return kharvox::sfs::profileHash(canonical.data(),uint32_t(canonical.size()*4));
}
inline uint64_t portableUiProfile(const std::vector<uint32_t>& words){
    const auto fingerprint=portableUiFingerprint(words);
    struct Alias{uint64_t canonical,profile;};
    constexpr Alias aliases[]{
#include "PortableUiAliases.inc"
    };
    for(auto alias:aliases)if(alias.canonical==fingerprint)return alias.profile;
    return 0;
}
}
