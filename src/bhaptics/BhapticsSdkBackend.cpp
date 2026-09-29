#include "BhapticsSdkBackend.h"

#include <algorithm>
#include <array>
#include <filesystem>

namespace kharvox::bhaptics {

DynamicBhapticsBackend::~DynamicBhapticsBackend() {
    unload();
}

bool DynamicBhapticsBackend::loadFromDirectory(
    const std::wstring& bridgeDirectory, std::string& failureReason) {
    unload();
    const auto dllPath = std::filesystem::path(bridgeDirectory)
        / L"bhaptics_library.dll";
    if (!std::filesystem::is_regular_file(dllPath)) {
        failureReason = "bhaptics_library.dll is missing";
        return false;
    }

    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    module_ = LoadLibraryExW(dllPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module_) {
        failureReason = "bhaptics_library.dll could not be loaded safely";
        return false;
    }

    registryAndInit_ = find<RegistryAndInitFn>("registryAndInit");
    wsIsConnected_ = find<BoolNoArgsFn>("wsIsConnected");
    wsClose_ = find<VoidNoArgsFn>("wsClose");
    isPlayerInstalled_ = find<BoolNoArgsFn>("isPlayerInstalled");
    isPlayerRunning_ = find<BoolNoArgsFn>("isPlayerRunning");
    launchPlayer_ = find<BoolBoolFn>("launchPlayer");
    getDeviceInfoJson_ = find<StringNoArgsFn>("getDeviceInfoJson");
    isBhapticsConnected_ = find<BoolIntFn>("isbHapticsConnected");
    playDot_ = find<PlayDotFn>("playDot");
    stop_ = find<BoolIntFn>("stop");
    stopAll_ = find<BoolNoArgsFn>("stopAll");

    if (!requiredExportsPresent()) {
        failureReason = "required SDK2 exports are missing:";
        if (!registryAndInit_)
            failureReason += " registryAndInit";
        if (!playDot_)
            failureReason += " playDot";
        unload();
        return false;
    }
    failureReason.clear();
    return true;
}

bool DynamicBhapticsBackend::registerAndInitialize(
    const std::string& apiKey, const std::string& appId,
    const std::string& initData) {
    return registryAndInit_
        && registryAndInit_(apiKey.c_str(), appId.c_str(), initData.c_str()) != 0;
}

std::optional<bool> DynamicBhapticsBackend::websocketConnected() {
    if (!wsIsConnected_)
        return std::nullopt;
    return wsIsConnected_() != 0;
}

void DynamicBhapticsBackend::closeWebsocket() noexcept {
    try {
        if (wsClose_)
            wsClose_();
    } catch (...) {
    }
}

std::optional<bool> DynamicBhapticsBackend::playerInstalled() {
    if (!isPlayerInstalled_)
        return std::nullopt;
    return isPlayerInstalled_() != 0;
}

std::optional<bool> DynamicBhapticsBackend::playerRunning() {
    if (!isPlayerRunning_)
        return std::nullopt;
    return isPlayerRunning_() != 0;
}

bool DynamicBhapticsBackend::launchPlayer() {
    return launchPlayer_ && launchPlayer_(1) != 0;
}

std::string DynamicBhapticsBackend::deviceInfoJson() {
    if (!getDeviceInfoJson_)
        return {};
    const char* json = getDeviceInfoJson_();
    if (!json)
        return {};
    constexpr std::size_t maximumDeviceJsonBytes = 64 * 1024;
    const auto length = strnlen_s(json, maximumDeviceJsonBytes);
    if (length == maximumDeviceJsonBytes)
        return {};
    return {json, length};
}

bool DynamicBhapticsBackend::deviceConnected(int position) {
    return isBhapticsConnected_ && isBhapticsConnected_(position) != 0;
}

int DynamicBhapticsBackend::playDot(
    int position, int durationMilliseconds,
    const std::array<int, tactSuitMotorCount>& motors) {
    if (!playDot_)
        return -1;
    auto mutableMotors = motors;
    return playDot_(position, durationMilliseconds,
        mutableMotors.data(), static_cast<int>(mutableMotors.size()));
}

bool DynamicBhapticsBackend::stop(int requestId) {
    return stop_ && stop_(requestId) != 0;
}

void DynamicBhapticsBackend::stopAll() noexcept {
    try {
        if (stopAll_)
            stopAll_();
    } catch (...) {
    }
}

void DynamicBhapticsBackend::unload() noexcept {
    if (!module_)
        return;
    stopAll();
    closeWebsocket();
    FreeLibrary(module_);
    module_ = nullptr;
    registryAndInit_ = nullptr;
    wsIsConnected_ = nullptr;
    wsClose_ = nullptr;
    isPlayerInstalled_ = nullptr;
    isPlayerRunning_ = nullptr;
    launchPlayer_ = nullptr;
    getDeviceInfoJson_ = nullptr;
    isBhapticsConnected_ = nullptr;
    playDot_ = nullptr;
    stop_ = nullptr;
    stopAll_ = nullptr;
}

} // namespace kharvox::bhaptics
