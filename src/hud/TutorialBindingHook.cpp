#include "TutorialBindingHook.h"
#include "../EternalBuildProfile.h"
#include "../Diagnostics.h"
#include "../QuadRuntime.h"
#include "../openxr/WeaponConfig.h"
#include <MinHook.h>
#include <windows.h>
#include <atomic>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace argent::hud { namespace {
using Lookup=const char*(__fastcall*)(const void*);
Lookup originalLookup{};
std::atomic<unsigned> activeLayout{};
const char* __fastcall localizedText(const void* id) {
 const char* original=originalLookup(id);
 if(!original)return original;
 const auto length=strnlen_s(original,8192);
 if(length==8192||!std::memchr(original,'_',length))return original;
 try {
  const unsigned mode=activeLayout.load(std::memory_order_relaxed);
  // Native callers may retain returned pointers. Never erase or mutate a
  // cached string, including when the layout or language changes.
  static std::mutex mutex;
  static std::array<std::map<std::string,std::string,std::less<>>,16> cache;
  std::lock_guard<std::mutex> lock(mutex);
  auto& strings=cache[mode];const std::string_view source(original,length);
  if(auto it=strings.find(source);it!=strings.end())return it->second.c_str();
  auto replaced=replaceTutorialBindings(source,{bool(mode&1),bool(mode&2),bool(mode&4),bool(mode&8)});
  if(replaced==source||replaced.size()>=8192||strings.size()>=4096)return original;
  const auto inserted=strings.emplace(std::string(source),std::move(replaced));
  log("ETERNAL_TUTORIAL_BINDING layout="+std::to_string(mode)+" strings="+std::to_string(strings.size()));
  return inserted.first->second.c_str();
 }catch(...){return original;}
}
}
void setTutorialBindingLayout(TutorialBindingLayout layout) noexcept {
 activeLayout.store(unsigned(layout.left)|(unsigned(layout.left&&layout.swapSticks)<<1)|
  (unsigned(layout.indexLeft)<<2)|(unsigned(layout.indexRight)<<3),std::memory_order_relaxed);
}
bool installTutorialBindings(unsigned char* base) noexcept {
 try {
  // Read the initial layout before the game creates tutorial widgets. The
  // OpenXR input path publishes subsequent configuration/profile changes.
  wchar_t path[32768]{};const auto count=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
  if(count&&count<32768){std::ifstream file(std::filesystem::path(path).parent_path().parent_path()/"assets"/"argent_controls.cfg");
   input::WeaponConfig config;if(file&&input::readWeaponConfig(file,config))setTutorialBindingLayout({config.leftHanded,config.leftHandSwapSticks});}
  constexpr unsigned char steam[]={0x48,0x83,0xec,0x58,0x48,0x8b,0x15,0x0d,0x10,0xf1,0x03,0x48,0x8d,0x05,0x26,0xc7,0x6e,0x02};
  constexpr unsigned char store[]={0x48,0x83,0xec,0x58,0x48,0x8b,0x15,0x7d,0x84,0xfc,0x03,0x48,0x8d,0x05,0x16,0xb5,0x78,0x02};
  auto target=base?base+build::rva(0x360bb0):nullptr;
  if(!target||std::memcmp(target,build::microsoftStore?store:steam,sizeof(steam))){log("ETERNAL_TUTORIAL_BINDING refused: lookup signature mismatch");return false;}
  bool ok=MH_CreateHook(target,reinterpret_cast<void*>(&localizedText),reinterpret_cast<void**>(&originalLookup))==MH_OK;
  if(ok){ok=MH_EnableHook(target)==MH_OK;if(!ok)MH_RemoveHook(target);}
  log(std::string("ETERNAL_TUTORIAL_BINDING installed=")+(ok?"1":"0"));return ok;
 }catch(...){log("ETERNAL_TUTORIAL_BINDING install failed");return false;}
}
}
