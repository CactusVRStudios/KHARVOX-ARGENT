#pragma once
#include <string_view>
#include <cstdint>
namespace argent::fg {
// Unknown/new architectures stay on the official capability-checked path.
inline bool automaticCommunity(uint32_t vendor,std::wstring_view name,unsigned multiplier){
 if(vendor!=0x10de)return false;
 return name.find(L"RTX 20")!=name.npos||name.find(L"RTX 30")!=name.npos||
        (name.find(L"RTX 40")!=name.npos);
}
}

