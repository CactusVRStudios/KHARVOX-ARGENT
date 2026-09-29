#include <Windows.h>

extern "C" __declspec(dllexport) void* psvr2_toolkit_loader_get_module_handle() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&psvr2_toolkit_loader_get_module_handle),
        &module);
    return module;
}
