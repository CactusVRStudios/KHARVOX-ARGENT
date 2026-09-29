#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

#include "Psvr2TriggerPolicy.h"
#include <psvr2tk_capi.h>

namespace kharvox::psvr2 {

constexpr int toolkitResultOk = 0;
constexpr int toolkitResultDriverInactive = -1;
constexpr int toolkitResultNoSlot = -2;
constexpr int toolkitResultTimeout = -3;
constexpr int toolkitResultInvalidParameter = -4;

enum class BackendLoadResult {
    Ready,
    LoaderMissing,
    LoaderLoadFailed,
    LoaderExportMissing,
    CapiUnavailable,
    CapiExportMissing,
};

const char* backendLoadResultName(BackendLoadResult result);
const char* toolkitResultName(int result);

class Psvr2ToolkitBackend {
public:
    Psvr2ToolkitBackend() = default;
    ~Psvr2ToolkitBackend();
    Psvr2ToolkitBackend(const Psvr2ToolkitBackend&) = delete;
    Psvr2ToolkitBackend& operator=(const Psvr2ToolkitBackend&) = delete;

    BackendLoadResult loadFromDirectory(const std::filesystem::path& directory);
    int initialize();
    bool driverActive(bool& callSucceeded);
    int apply(const TriggerCommand& command);
    int applyOff(VRControllerType controller);
    void shutdown();
    void unload();
    bool loaded() const { return capiModule_ != nullptr; }
    bool initialized() const { return initialized_; }

private:
    using GetModuleHandleFn = void* (*)();
    using InitFn = int (*)();
    using DeinitFn = void (*)();
    using GetDriverActiveFn = bool (*)();
    using SetTriggerEffectFn = int (*)(
        VRControllerType, const ScePadTriggerEffectCommand&);

    HMODULE loaderModule_{};
    HMODULE capiModule_{};
    GetModuleHandleFn getModuleHandle_{};
    InitFn init_{};
    DeinitFn deinit_{};
    GetDriverActiveFn getDriverActive_{};
    SetTriggerEffectFn setTriggerEffect_{};
    bool initialized_{};
};

} // namespace kharvox::psvr2
