#pragma once
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace argent::mouse {
inline bool gameRunning() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return true; // Do not edit live settings.
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
    if(Process32FirstW(snapshot,&entry))do {
        if(!_wcsicmp(entry.szExeFile,L"DOOMEternalx64vk.exe")){found=true;break;}
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
}
// Replace only the disabled mouse value; preserve all other bytes and encoding.
inline bool enableSavedMouse(std::string& bytes) {
    size_t offset=0,stride=1;
    if(bytes.size()>=2&&static_cast<unsigned char>(bytes[0])==0xff&&static_cast<unsigned char>(bytes[1])==0xfe){offset=2;stride=2;}
    else if(bytes.size()>=2&&static_cast<unsigned char>(bytes[0])==0xfe&&static_cast<unsigned char>(bytes[1])==0xff){offset=3;stride=2;}
    else if(bytes.compare(0,3,"\xef\xbb\xbf")==0)offset=3;
    std::string text;for(size_t i=offset;i<bytes.size();i+=stride)text+=bytes[i];
    const std::regex setting(R"((^|\n)([ \t]*in_mouse[ \t]+"?)0("?[ \t]*(\r?\n|$)))");
    bool changed=false;
    for(auto it=std::sregex_iterator(text.begin(),text.end(),setting);it!=std::sregex_iterator();++it){
        const auto position=size_t(it->position(2)+it->length(2));
        bytes[offset+position*stride]='1';changed=true;
    }
    return changed;
}
inline bool repairSavedMouse() {
    if(gameRunning())return false;
    PWSTR saved{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_SavedGames,0,nullptr,&saved)))return false;
    const auto base=std::filesystem::path(saved)/L"id Software";CoTaskMemFree(saved);
    try {
        if(!std::filesystem::exists(base))return true;
        for(const auto& directory:std::filesystem::directory_iterator(base)){
            if(!directory.is_directory()||directory.path().filename().wstring().find(L"DOOMEternal")!=0)continue;
            for(const auto& file:std::filesystem::recursive_directory_iterator(directory.path())){
                if(!file.is_regular_file()||_wcsicmp(file.path().filename().c_str(),L"DOOMEternalConfig.cfg"))continue;
                std::ifstream input(file.path(),std::ios::binary);
                if(!input)return false;
                std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();
                if(!enableSavedMouse(bytes))continue;
                auto temporary=file.path();temporary+=L".argent-mouse.tmp";
                std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
                output.write(bytes.data(),bytes.size());output.close();
                if(!output||!ReplaceFileW(file.path().c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr))return false;
            }
        }
        return true;
    }catch(...){return false;}
}
// The inherited process handle survives launcher closure and cannot be confused
// with a reused PID. The helper also runs after a game crash.
inline bool startRestoreMonitor(HANDLE game) {
    HANDLE inherited{};
    if(!DuplicateHandle(GetCurrentProcess(),game,GetCurrentProcess(),&inherited,SYNCHRONIZE,TRUE,0))return false;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --restore-mouse-handle "+std::to_wstring(reinterpret_cast<uintptr_t>(inherited));
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    const bool ok=CreateProcessW(executable,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;
    CloseHandle(inherited);
    if(ok){CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    return ok;
}
inline int restoreAfterSession(const wchar_t* argument) {
    wchar_t* end{};const auto value=wcstoull(argument,&end,10);
    if(!value||!end||*end)return 1;
    const HANDLE game=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
    const DWORD waited=WaitForSingleObject(game,INFINITE);CloseHandle(game);
    if(waited!=WAIT_OBJECT_0)return 1;
    for(int attempt=0;attempt<10;++attempt){if(repairSavedMouse())return 0;Sleep(500);}
    return 1; // Next normal launcher startup retries the migration.
}
}
