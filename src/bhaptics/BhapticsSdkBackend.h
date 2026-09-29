#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

#include "IBhapticsBackend.h"

namespace kharvox::bhaptics {

class DynamicBhapticsBackend final : public IBhapticsBackend {
public:
    DynamicBhapticsBackend() = default;
    ~DynamicBhapticsBackend() override;

    DynamicBhapticsBackend(const DynamicBhapticsBackend&) = delete;
    DynamicBhapticsBackend& operator=(const DynamicBhapticsBackend&) = delete;

    bool loadFromDirectory(const std::wstring& bridgeDirectory,
        std::string& failureReason);
    bool loaded() const { return module_ != nullptr; }
    bool requiredExportsPresent() const {
        return registryAndInit_ != nullptr && playDot_ != nullptr;
    }

    bool registerAndInitialize(const std::string& apiKey,
        const std::string& appId, const std::string& initData) override;
    std::optional<bool> websocketConnected() override;
    void closeWebsocket() noexcept override;
    std::optional<bool> playerInstalled() override;
    std::optional<bool> playerRunning() override;
    bool launchPlayer() override;
    std::string deviceInfoJson() override;
    bool deviceConnected(int position) override;
    int playDot(int position, int durationMilliseconds,
        const std::array<int, tactSuitMotorCount>& motors) override;
    bool stop(int requestId) override;
    void stopAll() noexcept override;

    void unload() noexcept;

private:
    using Bool8 = std::uint8_t;
    using RegistryAndInitFn = Bool8(__cdecl*)(const char*, const char*, const char*);
    using BoolNoArgsFn = Bool8(__cdecl*)();
    using VoidNoArgsFn = void(__cdecl*)();
    using BoolIntFn = Bool8(__cdecl*)(int);
    using BoolBoolFn = Bool8(__cdecl*)(Bool8);
    using StringNoArgsFn = const char*(__cdecl*)();
    // ABI from the x64 SDK2 DLL and its accompanying BhapticsSDK2.h:
    // playDot(position, duration, motorValues, motorValueLen).
    using PlayDotFn = int(__cdecl*)(int, int, int*, int);

    template <typename Function>
    Function find(const char* name) const {
        return module_ ? reinterpret_cast<Function>(GetProcAddress(module_, name)) : nullptr;
    }

    HMODULE module_{};
    RegistryAndInitFn registryAndInit_{};
    BoolNoArgsFn wsIsConnected_{};
    VoidNoArgsFn wsClose_{};
    BoolNoArgsFn isPlayerInstalled_{};
    BoolNoArgsFn isPlayerRunning_{};
    BoolBoolFn launchPlayer_{};
    StringNoArgsFn getDeviceInfoJson_{};
    BoolIntFn isBhapticsConnected_{};
    PlayDotFn playDot_{};
    BoolIntFn stop_{};
    BoolNoArgsFn stopAll_{};
};

} // namespace kharvox::bhaptics
