#pragma once

#include <array>
#include <optional>
#include <string>

#include "BhapticsMappingPolicy.h"

namespace kharvox::bhaptics {

class IBhapticsBackend {
public:
    virtual ~IBhapticsBackend() = default;

    virtual bool registerAndInitialize(
        const std::string& apiKey, const std::string& appId,
        const std::string& initData) = 0;
    virtual std::optional<bool> websocketConnected() = 0;
    virtual void closeWebsocket() noexcept = 0;
    virtual std::optional<bool> playerInstalled() = 0;
    virtual std::optional<bool> playerRunning() = 0;
    virtual bool launchPlayer() = 0;
    virtual std::string deviceInfoJson() = 0;
    virtual bool deviceConnected(int position) = 0;
    virtual int playDot(int position, int durationMilliseconds,
        const std::array<int, tactSuitMotorCount>& motors) = 0;
    virtual bool stop(int requestId) = 0;
    virtual void stopAll() noexcept = 0;
};

enum class BackendInitializationResult {
    Ready,
    MissingCredentials,
    PlayerNotInstalled,
    PlayerNotRunning,
    RegistrationFailed,
    ConnectionTimeout,
};

template <typename SleepFunction>
BackendInitializationResult initializeBackend(
    IBhapticsBackend& backend,
    const std::string& appId, const std::string& apiKey,
    unsigned maximumConnectionPolls, SleepFunction&& sleepFunction) {
    // Raw SDK2 playDot playback can register with an empty app/key pair. This
    // is the public, credential-free KHARVOX mode. A partial developer pair is
    // rejected so a mistyped environment cannot silently select another mode.
    if (appId.empty() != apiKey.empty())
        return BackendInitializationResult::MissingCredentials;

    if (const auto installed = backend.playerInstalled(); installed && !*installed)
        return BackendInitializationResult::PlayerNotInstalled;

    if (const auto running = backend.playerRunning(); running && !*running) {
        backend.launchPlayer();
        bool started = false;
        for (unsigned poll = 0; poll < maximumConnectionPolls; ++poll) {
            sleepFunction();
            const auto current = backend.playerRunning();
            if (!current || *current) {
                started = true;
                break;
            }
        }
        if (!started)
            return BackendInitializationResult::PlayerNotRunning;
    }

    // Official SDK2 ABI: registryAndInit(apiKey, workspace/app ID, init data).
    if (!backend.registerAndInitialize(apiKey, appId, ""))
        return BackendInitializationResult::RegistrationFailed;

    const auto initialConnection = backend.websocketConnected();
    if (!initialConnection || *initialConnection)
        return BackendInitializationResult::Ready;
    for (unsigned poll = 0; poll < maximumConnectionPolls; ++poll) {
        sleepFunction();
        const auto connected = backend.websocketConnected();
        if (!connected || *connected)
            return BackendInitializationResult::Ready;
    }
    return BackendInitializationResult::ConnectionTimeout;
}

class BhapticsRumbleEngine {
public:
    explicit BhapticsRumbleEngine(IBhapticsBackend& backend,
        float intensityScale = defaultIntensityScale)
        : backend_(backend), intensityScale_(intensityScale) {}

    void setRawRumble(std::uint16_t low, std::uint16_t high) {
        low_ = low;
        high_ = high;
    }

    void tick(std::uint64_t nowMilliseconds) {
        if (nowMilliseconds >= nextDeviceCheckMilliseconds_) {
            const auto json = backend_.deviceInfoJson();
            inventoryRecognized_ = deviceJsonMayContainPosition(
                json, tactSuitPosition);
            // Player/SDK2 combinations can keep both device queries empty even
            // while the Player can ping the vest and accepts playDot commands.
            // Keep these values for diagnostics, but never use them as a hard
            // playback gate. The playDot result is authoritative.
            deviceConnected_ = backend_.deviceConnected(tactSuitPosition);
            nextDeviceCheckMilliseconds_ = nowMilliseconds + deviceRecheckMilliseconds;
        }

        const auto desired = mapRumbleToTactSuit(low_, high_, intensityScale_);
        const auto command = selectRumbleCommand(dispatch_, desired, nowMilliseconds);
        if (command == RumbleCommand::None)
            return;
        if (command == RumbleCommand::Stop) {
            stopCurrentRequest(nowMilliseconds);
            return;
        }

        if (requestId_ > 0)
            backend_.stop(requestId_);
        requestId_ = backend_.playDot(tactSuitPosition,
            dotDurationMilliseconds, desired.motors);
        if (requestId_ > 0)
            noteRumblePlayed(dispatch_, desired, nowMilliseconds);
        else
            noteRumbleStopped(dispatch_, nowMilliseconds);
    }

    void disconnected(std::uint64_t nowMilliseconds) {
        low_ = 0;
        high_ = 0;
        stopCurrentRequest(nowMilliseconds);
    }

    void shutdown() noexcept {
        try {
            if (requestId_ > 0)
                backend_.stop(requestId_);
            backend_.stopAll();
            backend_.closeWebsocket();
        } catch (...) {
        }
        requestId_ = 0;
        dispatch_ = {};
    }

    bool deviceConnected() const { return deviceConnected_; }
    bool inventoryRecognized() const { return inventoryRecognized_; }
    int currentRequestId() const { return requestId_; }

private:
    void stopCurrentRequest(std::uint64_t nowMilliseconds) {
        if (requestId_ > 0)
            backend_.stop(requestId_);
        requestId_ = 0;
        if (dispatch_.active)
            noteRumbleStopped(dispatch_, nowMilliseconds);
    }

    IBhapticsBackend& backend_;
    float intensityScale_{};
    std::uint16_t low_{};
    std::uint16_t high_{};
    bool deviceConnected_{};
    bool inventoryRecognized_{};
    int requestId_{};
    std::uint64_t nextDeviceCheckMilliseconds_{};
    RumbleDispatchState dispatch_{};
};

} // namespace kharvox::bhaptics
