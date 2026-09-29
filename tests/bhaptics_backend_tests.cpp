#include "../src/bhaptics/BhapticsSdkBackend.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace {

class FakeBackend final : public kharvox::bhaptics::IBhapticsBackend {
public:
    bool registrationResult{true};
    std::optional<bool> installed{true};
    std::optional<bool> running{true};
    std::optional<bool> connected{true};
    bool launchResult{true};
    bool device{true};
    std::string json{"[{\"position\":0}]"};
    int nextRequest{7};
    int registerCalls{};
    int launchCalls{};
    int playCalls{};
    int stopCalls{};
    int stopAllCalls{};
    int closeCalls{};
    int deviceInfoCalls{};
    int deviceConnectedCalls{};

    bool registerAndInitialize(const std::string&, const std::string&,
        const std::string&) override {
        ++registerCalls;
        return registrationResult;
    }
    std::optional<bool> websocketConnected() override { return connected; }
    void closeWebsocket() noexcept override { ++closeCalls; }
    std::optional<bool> playerInstalled() override { return installed; }
    std::optional<bool> playerRunning() override { return running; }
    bool launchPlayer() override { ++launchCalls; return launchResult; }
    std::string deviceInfoJson() override { ++deviceInfoCalls; return json; }
    bool deviceConnected(int) override { ++deviceConnectedCalls; return device; }
    int playDot(int, int,
        const std::array<int, kharvox::bhaptics::tactSuitMotorCount>&) override {
        ++playCalls;
        return nextRequest++;
    }
    bool stop(int) override { ++stopCalls; return true; }
    void stopAll() noexcept override { ++stopAllCalls; }
};

} // namespace

int main(int argc, char** argv) {
    using namespace kharvox::bhaptics;
    const auto noSleep = [] {};

    FakeBackend fake;
    if (initializeBackend(fake, "", "", 1, noSleep)
            != BackendInitializationResult::Ready
        || fake.registerCalls != 1)
        return 1;

    if (initializeBackend(fake, "app", "", 1, noSleep)
        != BackendInitializationResult::MissingCredentials)
        return 16;

    fake.installed = false;
    if (initializeBackend(fake, "app", "key", 1, noSleep)
        != BackendInitializationResult::PlayerNotInstalled)
        return 2;

    fake.installed = true;
    fake.running = false;
    if (initializeBackend(fake, "app", "key", 2, noSleep)
        != BackendInitializationResult::PlayerNotRunning
        || fake.launchCalls == 0)
        return 3;

    fake.running = true;
    fake.registrationResult = false;
    if (initializeBackend(fake, "app", "key", 1, noSleep)
        != BackendInitializationResult::RegistrationFailed)
        return 4;

    fake.registrationResult = true;
    fake.connected = false;
    if (initializeBackend(fake, "app", "key", 2, noSleep)
        != BackendInitializationResult::ConnectionTimeout)
        return 5;
    fake.connected = true;
    if (initializeBackend(fake, "app", "key", 1, noSleep)
        != BackendInitializationResult::Ready)
        return 6;
    // Startup readiness is the local Player connection, never vest presence.
    if (fake.deviceInfoCalls != 0 || fake.deviceConnectedCalls != 0)
        return 31;

    BhapticsRumbleEngine engine(fake);
    fake.device = false;
    engine.setRawRumble(65535, 0);
    engine.tick(0);
    if (engine.deviceConnected() || fake.playCalls != 1
        || engine.currentRequestId() <= 0)
        return 7;

    fake.device = true;
    engine.tick(deviceRecheckMilliseconds);
    if (!engine.deviceConnected() || fake.playCalls != 2
        || engine.currentRequestId() <= 0)
        return 8;
    engine.setRawRumble(0, 65535);
    engine.tick(deviceRecheckMilliseconds + maximumUpdateRateMilliseconds);
    if (fake.playCalls != 3 || fake.stopCalls == 0)
        return 9;
    engine.setRawRumble(0, 0);
    engine.tick(deviceRecheckMilliseconds + 2 * maximumUpdateRateMilliseconds);
    if (engine.currentRequestId() != 0)
        return 10;
    engine.shutdown();
    if (fake.stopAllCalls == 0 || fake.closeCalls == 0)
        return 11;

    FakeBackend changedInventory;
    changedInventory.json = "[]";
    BhapticsRumbleEngine inventoryTolerantEngine(changedInventory);
    inventoryTolerantEngine.setRawRumble(0, 65535);
    inventoryTolerantEngine.tick(0);
    if (!inventoryTolerantEngine.deviceConnected()
        || inventoryTolerantEngine.inventoryRecognized()
        || changedInventory.playCalls != 1)
        return 29;
    inventoryTolerantEngine.shutdown();

    FakeBackend queryAndPlaybackFailure;
    queryAndPlaybackFailure.device = false;
    queryAndPlaybackFailure.json = "[]";
    queryAndPlaybackFailure.nextRequest = -1;
    BhapticsRumbleEngine playbackProbeEngine(queryAndPlaybackFailure);
    playbackProbeEngine.setRawRumble(65535, 65535);
    playbackProbeEngine.tick(0);
    if (playbackProbeEngine.deviceConnected()
        || playbackProbeEngine.inventoryRecognized()
        || queryAndPlaybackFailure.playCalls != 1
        || playbackProbeEngine.currentRequestId() > 0)
        return 30;
    playbackProbeEngine.shutdown();

    DynamicBhapticsBackend missingDll;
    std::string reason;
    if (missingDll.loadFromDirectory(
            (std::filesystem::temp_directory_path() / "kharvox-no-sdk").wstring(), reason)
        || reason.empty())
        return 12;

    if (argc < 2)
        return 13;
    DynamicBhapticsBackend missingExports;
    reason.clear();
    if (missingExports.loadFromDirectory(
            std::filesystem::path(argv[1]).wstring(), reason)
        || reason.find("required SDK2 exports") == std::string::npos)
        return 14;
    if (argc >= 3) {
        DynamicBhapticsBackend validSdk;
        reason.clear();
        if (!validSdk.loadFromDirectory(
                std::filesystem::path(argv[2]).wstring(), reason)
            || !reason.empty())
            return 15;
    }
    return 0;
}
