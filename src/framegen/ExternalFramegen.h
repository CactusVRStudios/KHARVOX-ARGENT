#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <algorithm>
namespace argent::fg {
inline bool externalFramegenLoaded(){
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());if(snapshot==INVALID_HANDLE_VALUE)return false;
 MODULEENTRY32W entry{sizeof(entry)};bool found=false;
 if(Module32FirstW(snapshot,&entry))do{std::wstring name=entry.szModule;std::transform(name.begin(),name.end(),name.begin(),towlower);
  if(name.find(L"xrframebridge")!=name.npos||name.find(L"ofxr")!=name.npos||name.find(L"openxr_framegen")!=name.npos){found=true;break;}
 }while(Module32NextW(snapshot,&entry));CloseHandle(snapshot);return found;
}
}
