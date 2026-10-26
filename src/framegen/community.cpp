#include "community.hpp"
#include "options.hpp"
#include "backend_policy.hpp"
#include "AdapterPolicy.h"
#include <filesystem>
#include <mutex>
namespace argent::fg {
bool loadCommunityRuntime(uint32_t vendor,const std::wstring& adapter,std::string& reason){
 static std::mutex mutex;std::lock_guard lock(mutex);
 if(!supportedAdapter(vendor,adapter)){reason="No bundled DLSS-G profile for this adapter";return false;}
 if(vendor==0x10de&&adapter.find(L"RTX 50")!=adapter.npos){
  SetEnvironmentVariableA("ARGENT_FG_COMMUNITY","0");return true;
 }
 SetEnvironmentVariableA("ARGENT_FG_COMMUNITY","1");
 static HMODULE module{};static std::wstring profile;
 const bool ada=adapter.find(L"RTX 40")!=adapter.npos;
 if(vendor!=0x10de||(!ada&&adapter.find(L"RTX 20")==adapter.npos&&adapter.find(L"RTX 30")==adapter.npos)){
  reason="Bundled Community DLSS profile supports RTX 20/30/40; no verified profile for this GPU (real-frame fallback)";return false;
 }
 const std::wstring selected=ada?L"rtx40":L"rtx20-30";
 if(module){if(profile==selected)return true;reason="Community GPU profile changed; restart required";return false;}
 wchar_t directory[32768]{};auto length=GetEnvironmentVariableW(L"ARGENT_FG_DIRECTORY",directory,32768);
 if(!length||length>=32768){reason="Community runtime directory missing";return false;}
 const auto proxy=std::filesystem::path(directory)/selected/L"version.dll";
 if(!proxy.is_absolute()){reason="Community runtime directory must be absolute";return false;}
 // Keep hooks and kernel resources alive until process exit. No game DLL replacement.
 module=LoadLibraryExW(proxy.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 if(!module){reason="Community runtime could not be loaded; run the runtime installer";return false;}
 profile=selected;return true;
}
}
