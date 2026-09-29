#include "BhapticsIpcClient.h"

#include <Windows.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <string>

#include "BhapticsIpcProtocol.h"
#include "BhapticsMappingPolicy.h"

namespace {

std::atomic<bool> workerStarted{};
std::atomic<bool> stopRequested{};
std::atomic<std::uint32_t> currentRumble{};
std::atomic<std::uint32_t> pendingRumblePeak{};
std::atomic<HANDLE> stopEvent{};
HANDLE workerHandle{};
std::wstring pipePath;
std::string sessionToken;

std::wstring readEnvironment(const wchar_t* name, std::size_t maximumCharacters) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length <= 1 || length > maximumCharacters + 1)
        return {};
    std::wstring value(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), length);
    if (copied != length - 1)
        return {};
    value.resize(copied);
    return value;
}

bool validPipeName(const std::wstring& name) {
    if (name.empty() || name.size() > 128)
        return false;
    for (const auto character : name) {
        if (!(character >= L'a' && character <= L'z')
            && !(character >= L'A' && character <= L'Z')
            && !(character >= L'0' && character <= L'9')
            && character != L'_' && character != L'-')
            return false;
    }
    return true;
}

bool validToken(const std::string& token) {
    if (token.empty() || token.size() > kharvox::bhaptics::ipcMaximumTokenBytes)
        return false;
    for (const auto character : token) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::isxdigit(byte))
            return false;
    }
    return true;
}

bool writeWithTimeout(HANDLE pipe, const void* data, DWORD bytes) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent)
        return false;
    DWORD written{};
    bool success = WriteFile(pipe, data, bytes, &written, &overlapped) != FALSE;
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        const HANDLE waits[]{overlapped.hEvent, stopEvent.load(std::memory_order_acquire)};
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 250);
        if (wait == WAIT_OBJECT_0)
            success = GetOverlappedResult(pipe, &overlapped, &written, FALSE) != FALSE;
        else {
            CancelIoEx(pipe, &overlapped);
            WaitForSingleObject(overlapped.hEvent, 50);
            success = false;
        }
    }
    CloseHandle(overlapped.hEvent);
    return success && written == bytes;
}

bool writeMessage(HANDLE pipe, const kharvox::bhaptics::EncodedIpcMessage& message) {
    return message.size != 0
        && writeWithTimeout(pipe, message.bytes.data(), static_cast<DWORD>(message.size));
}

HANDLE connectPipe() {
    while (!stopRequested.load(std::memory_order_acquire)) {
        if (WaitNamedPipeW(pipePath.c_str(), 250)) {
            HANDLE pipe = CreateFileW(pipePath.c_str(), GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
                return pipe;
        }
        if (WaitForSingleObject(stopEvent.load(std::memory_order_acquire), 250) == WAIT_OBJECT_0)
            break;
    }
    return INVALID_HANDLE_VALUE;
}

DWORD WINAPI workerMain(void*) {
    using namespace kharvox::bhaptics;
    std::uint32_t sequence{};
    while (!stopRequested.load(std::memory_order_acquire)) {
        HANDLE pipe = connectPipe();
        if (pipe == INVALID_HANDLE_VALUE)
            break;
        if (!writeMessage(pipe, encodeHello(++sequence,
                GetTickCount64(), sessionToken))) {
            CloseHandle(pipe);
            continue;
        }

        std::uint32_t lastSent = 0xffffffffu;
        std::uint64_t refreshAt{};
        while (!stopRequested.load(std::memory_order_acquire)) {
            const auto now = GetTickCount64();
            const auto current = currentRumble.load(std::memory_order_acquire);
            const auto peak = pendingRumblePeak.exchange(0, std::memory_order_acq_rel);
            const auto latest = mergeRumblePeaks(current, peak);
            if (latest != lastSent || now >= refreshAt) {
                const auto message = latest == 0
                    ? encodeEmptyMessage(IpcMessageType::RumbleStop,
                        ++sequence, now)
                    : encodeRumbleState(++sequence, now,
                        lowMotor(latest), highMotor(latest));
                if (!writeMessage(pipe, message))
                    break;
                lastSent = latest;
                refreshAt = now + (latest == 0
                    ? stoppedRumbleRefreshMilliseconds
                    : sustainedRumbleRefreshMilliseconds);
            }
            if (WaitForSingleObject(stopEvent.load(std::memory_order_acquire), maximumUpdateRateMilliseconds)
                == WAIT_OBJECT_0)
                break;
        }
        if (stopRequested.load(std::memory_order_acquire))
            writeMessage(pipe, encodeEmptyMessage(
                IpcMessageType::RumbleStop, ++sequence, GetTickCount64()));
        CancelIoEx(pipe, nullptr);
        CloseHandle(pipe);
    }

    currentRumble.store(0, std::memory_order_release);
    pendingRumblePeak.store(0, std::memory_order_release);
    workerStarted.store(false, std::memory_order_release);
    if (const HANDLE event = stopEvent.exchange(nullptr, std::memory_order_acq_rel))
        CloseHandle(event);
    return 0;
}

} // namespace

void KharvoxBhapticsIpcStart() {
    bool expected = false;
    if (!workerStarted.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel))
        return;

    const auto pipeName = readEnvironment(L"KHARVOX_BHAPTICS_PIPE_NAME", 128);
    const auto tokenWide = readEnvironment(L"KHARVOX_BHAPTICS_SESSION_TOKEN",
        kharvox::bhaptics::ipcMaximumTokenBytes);
    if (!validPipeName(pipeName) || tokenWide.empty()) {
        workerStarted.store(false, std::memory_order_release);
        return;
    }
    sessionToken.clear();
    sessionToken.reserve(tokenWide.size());
    for (const auto character : tokenWide) {
        if (character > 0x7f) {
            sessionToken.clear();
            break;
        }
        sessionToken.push_back(static_cast<char>(character));
    }
    if (!validToken(sessionToken)) {
        sessionToken.clear();
        workerStarted.store(false, std::memory_order_release);
        return;
    }

    pipePath = L"\\\\.\\pipe\\" + pipeName;
    stopRequested.store(false, std::memory_order_release);
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stopEvent.store(event, std::memory_order_release);
    if (!event) {
        workerStarted.store(false, std::memory_order_release);
        return;
    }
    workerHandle = CreateThread(nullptr, 0, workerMain, nullptr, 0, nullptr);
    if (!workerHandle) {
        CloseHandle(event);
        stopEvent.store(nullptr, std::memory_order_release);
        workerStarted.store(false, std::memory_order_release);
        return;
    }
    // The worker owns its lifetime until process shutdown. Closing this handle
    // avoids a kernel-handle leak without blocking the Vulkan/OpenXR thread.
    CloseHandle(workerHandle);
    workerHandle = nullptr;
}

void KharvoxBhapticsSubmitRumble(
    std::uint16_t lowMotor, std::uint16_t highMotor) {
    if (!workerStarted.load(std::memory_order_relaxed))
        return;
    const auto packed = kharvox::bhaptics::packRumble(lowMotor, highMotor);
    currentRumble.store(packed, std::memory_order_release);
    auto observed = pendingRumblePeak.load(std::memory_order_relaxed);
    while (true) {
        const auto merged = kharvox::bhaptics::mergeRumblePeaks(observed, packed);
        if (merged == observed)
            break;
        if (pendingRumblePeak.compare_exchange_weak(observed, merged,
                std::memory_order_release, std::memory_order_relaxed))
            break;
    }
}

void KharvoxBhapticsIpcRequestStop() {
    if (!workerStarted.load(std::memory_order_acquire))
        return;
    stopRequested.store(true, std::memory_order_release);
    if (const HANDLE event = stopEvent.load(std::memory_order_acquire))
        SetEvent(event);
}
