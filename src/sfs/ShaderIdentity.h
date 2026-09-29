#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <iomanip>
#include <sstream>

namespace kharvox::sfs {
// Recovered at RVA 0x1cc5f0 in 4.25.5.608. This is NOT standard MurmurHash64A:
// its 64-bit rounds use the 32-bit multiplier 0x5bd1e995 and seed 0x1000193.
// Exact compatibility is needed to resolve the existing DOOM shader filenames.
inline uint64_t profileHash(const void* source,uint32_t length,uint64_t seed=0x1000193) {
    constexpr uint64_t m=0x5bd1e995;
    auto bytes=static_cast<const uint8_t*>(source);
    uint64_t h=seed^(uint64_t(length)*m);
    uint32_t offset=0;
    for(;length-offset>=8;offset+=8){
        uint64_t word=0;
        for(unsigned i=0;i<8;++i)word|=uint64_t(bytes[offset+i])<<(8*i);
        word*=m;word^=word>>47;word*=m;h^=word;h*=m;
    }
    const auto tail=length-offset;
    for(unsigned i=0;i<tail;++i)h^=uint64_t(bytes[offset+i])<<(8*i);
    if(tail)h*=m;
    h^=h>>47;h*=m;h^=h>>47;
    return h;
}
inline std::string shaderKey(uint64_t hash) {
    std::ostringstream out;out<<std::hex<<hash;return out.str();
}
}
