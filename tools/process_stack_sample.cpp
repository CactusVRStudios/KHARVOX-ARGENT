#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <array>
#include <iostream>
#include <map>
#include <string>
#include <vector>
// Diagnostic for the owned game: no injection or writes. Always balance the
// temporary suspend, including on exceptions. Resolve names after resuming.
struct Thread {HANDLE handle;bool suspended{};~Thread(){if(suspended)ResumeThread(handle);if(handle)CloseHandle(handle);}};
int main(int argc,char** argv){
 if(argc!=2)return 2;const DWORD pid=std::stoul(argv[1]);
 HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
 if(!process){std::cerr<<"OpenProcess "<<GetLastError();return 1;}
 SymSetOptions(SYMOPT_UNDNAME|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
 if(!SymInitialize(process,".",TRUE)){CloseHandle(process);return 1;}
 std::map<std::string,unsigned> stacks;
 for(unsigned sample=0;sample<20;++sample){
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
  THREADENTRY32 entry{sizeof(entry)};std::vector<DWORD> ids;
  if(Thread32First(snapshot,&entry))do{if(entry.th32OwnerProcessID==pid)ids.push_back(entry.th32ThreadID);}while(Thread32Next(snapshot,&entry));
  CloseHandle(snapshot);
  for(auto id:ids){
   Thread thread{OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,id)};if(!thread.handle)continue;
   if(SuspendThread(thread.handle)==DWORD(-1))continue;thread.suspended=true;
   CONTEXT context{};context.ContextFlags=CONTEXT_FULL;std::array<DWORD64,13> addresses{};unsigned size{};
   if(GetThreadContext(thread.handle,&context)){
    STACKFRAME64 frame{};frame.AddrPC={context.Rip,0,AddrModeFlat};frame.AddrStack={context.Rsp,0,AddrModeFlat};frame.AddrFrame={context.Rbp,0,AddrModeFlat};
    addresses[size++]=context.Rip;
    for(unsigned depth=0;depth<12;++depth){if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread.handle,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)||!frame.AddrPC.Offset)break;addresses[size++]=frame.AddrPC.Offset;}
   }
   ResumeThread(thread.handle);thread.suspended=false;
   std::string key;
   for(unsigned i=0;i<size;++i){const auto address=addresses[i];
    IMAGEHLP_MODULE64 module{sizeof(module)};SymGetModuleInfo64(process,address,&module);
    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+512]{};auto symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=511;DWORD64 displacement{};
    key+=std::string(module.ModuleName)+"!";
    if(SymFromAddr(process,address,&displacement,symbol)){char hex[64];sprintf_s(hex,"+0x%llx[rva=0x%llx]",displacement,address-module.BaseOfImage);key+=std::string(symbol->Name)+hex;}
    else {char hex[32];sprintf_s(hex,"0x%llx",address-module.BaseOfImage);key+=hex;}
    key+=";";
   }
   ++stacks[key];
  }
  Sleep(50);
 }
 for(const auto& entry:stacks)std::cout<<entry.second<<'\t'<<entry.first<<'\n';
 SymCleanup(process);CloseHandle(process);return 0;
}
