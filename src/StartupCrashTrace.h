#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace argent {
// First-chance diagnostics leave exception handling to the game and Windows.
inline HANDLE startupCrashFile=INVALID_HANDLE_VALUE;
inline LONG startupCrashCount{};
inline LONG CALLBACK startupCrashTrace(EXCEPTION_POINTERS* exception){
    if(!exception||exception->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||
        InterlockedIncrement(&startupCrashCount)>4)return EXCEPTION_CONTINUE_SEARCH;
    char line[2048]{};DWORD written{};
    auto write=[&]{WriteFile(startupCrashFile,line,DWORD(strlen(line)),&written,nullptr);};
    auto* record=exception->ExceptionRecord;
    sprintf_s(line,"ACCESS_VIOLATION thread=%lu address=%p operation=%llu target=%llx\r\n",
        GetCurrentThreadId(),record->ExceptionAddress,record->ExceptionInformation[0],record->ExceptionInformation[1]);write();
    auto* c=exception->ContextRecord;
    sprintf_s(line,"RIP=%llx RSP=%llx RAX=%llx RCX=%llx RDX=%llx R8=%llx R9=%llx\r\n",c->Rip,c->Rsp,c->Rax,c->Rcx,c->Rdx,c->R8,c->R9);write();
    void* frames[40]{};auto count=CaptureStackBackTrace(0,40,frames,nullptr);
    for(unsigned n=0;n<=count;++n){
        auto address=n?frames[n-1]:record->ExceptionAddress;
        MEMORY_BASIC_INFORMATION memory{};char module[MAX_PATH]{};
        VirtualQuery(address,&memory,sizeof(memory));
        if(memory.Type==MEM_IMAGE)GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase),module,MAX_PATH);
        sprintf_s(line,"%u %p %s +0x%llx\r\n",n,address,module,
            reinterpret_cast<ULONG_PTR>(address)-reinterpret_cast<ULONG_PTR>(memory.AllocationBase));write();
    }
    FlushFileBuffers(startupCrashFile);
    return EXCEPTION_CONTINUE_SEARCH;
}
inline void installStartupCrashTrace(){
    static const bool installed=[] {
        wchar_t path[32768]{};auto size=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
        if(!size||size+10>=32768)return false;
        wcscat_s(path,L".crash.txt");
        startupCrashFile=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        return startupCrashFile!=INVALID_HANDLE_VALUE&&AddVectoredExceptionHandler(1,startupCrashTrace)!=nullptr;
    }();
    (void)installed;
}
}
