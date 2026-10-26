#pragma once
#include <cstdint>
#include <string>
namespace argent::fg {
inline const wchar_t* runtimeRelativePath(const std::wstring& name){
 return name.find(L"RTX 50")!=name.npos?L"rtx50/nvngx_dlssg.dll":
     name.find(L"RTX 40")!=name.npos?L"rtx40/dlssg_sm86/runtime/nvngx_dlssg.dll":
     L"rtx20-30/dlssg_sm86/runtime/nvngx_dlssg.dll";
}
inline bool supportedAdapter(uint32_t vendor,const std::wstring& name,bool software=false){
 if(software||vendor!=0x10de||name.find(L"GeForce RTX ")==name.npos)return false;
 for(const auto* series:{L"RTX 20",L"RTX 30",L"RTX 40",L"RTX 50"})if(name.find(series)!=std::wstring::npos)return true;
 return false;
}
}
