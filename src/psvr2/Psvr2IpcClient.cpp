#include "Psvr2IpcClient.h"

#include <Windows.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <string>

#include "Psvr2IpcProtocol.h"

namespace {

std::atomic<bool> workerStarted{};
std::atomic<bool> stopRequested{};
std::atomic<std::uint64_t> desiredCommand{};
std::atomic<std::uint64_t> desiredGeneration{};
std::atomic<HANDLE> stopEvent{};
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

bool requested() {
    const auto value = readEnvironment(L"KHARVOX_USE_PSVR2_TOOLKIT", 8);
    return value == L"1";
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
    if (token.empty() || token.size() > kharvox::psvr2::ipcMaximumTokenBytes)
        return false;
    for (const auto character : token)
        if (!std::isxdigit(static_cast<unsigned char>(character)))
            return false;
    return true;
}

bool writeWithTimeout(HANDLE pipe, const void* data, DWORD bytes) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) return false;
    DWORD written{};
    bool success = WriteFile(pipe, data, bytes, &written, &overlapped) != FALSE;
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        const HANDLE waits[]{overlapped.hEvent,
            stopEvent.load(std::memory_order_acquire)};
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

bool writeMessage(HANDLE pipe,
    const kharvox::psvr2::EncodedIpcMessage& message) {
    return message.size != 0 && writeWithTimeout(pipe,
        message.bytes.data(), static_cast<DWORD>(message.size));
}

HANDLE connectPipe() {
    while (!stopRequested.load(std::memory_order_acquire)) {
        if (WaitNamedPipeW(pipePath.c_str(), 250)) {
            HANDLE pipe = CreateFileW(pipePath.c_str(), GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) return pipe;
        }
        if (WaitForSingleObject(stopEvent.load(std::memory_order_acquire), 250)
            == WAIT_OBJECT_0)
            break;
    }
    return INVALID_HANDLE_VALUE;
}

DWORD WINAPI workerMain(void*) {
    using namespace kharvox::psvr2;
    std::uint32_t sequence{};
    while (!stopRequested.load(std::memory_order_acquire)) {
        HANDLE pipe = connectPipe();
        if (pipe == INVALID_HANDLE_VALUE) break;
        if (!writeMessage(pipe, encodeHello(++sequence, GetTickCount64(),
                sessionToken))) {
            CloseHandle(pipe);
            continue;
        }

        std::uint64_t sentGeneration{};
        while (!stopRequested.load(std::memory_order_acquire)) {
            const auto generation = desiredGeneration.load(std::memory_order_acquire);
            if (generation != sentGeneration) {
                const auto command = unpackTriggerCommand(
                    desiredCommand.load(std::memory_order_acquire));
                if (!writeMessage(pipe, encodeTriggerState(
                        ++sequence, GetTickCount64(), command)))
                    break;
                sentGeneration = generation;
            }
            if (WaitForSingleObject(stopEvent.load(std::memory_order_acquire), 50)
                == WAIT_OBJECT_0)
                break;
        }
        CancelIoEx(pipe, nullptr);
        CloseHandle(pipe);
    }

    workerStarted.store(false, std::memory_order_release);
    if (const HANDLE event = stopEvent.exchange(nullptr,
            std::memory_order_acq_rel))
        CloseHandle(event);
    return 0;
}

} // namespace

void KharvoxPsvr2IpcStart() {
    bool expected = false;
    if (!workerStarted.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel))
        return;
    if (!requested()) {
        workerStarted.store(false, std::memory_order_release);
        return;
    }

    const auto pipeName = readEnvironment(L"KHARVOX_PSVR2_PIPE_NAME", 128);
    const auto tokenWide = readEnvironment(L"KHARVOX_PSVR2_SESSION_TOKEN",
        kharvox::psvr2::ipcMaximumTokenBytes);
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
    desiredCommand.store(kharvox::psvr2::packTriggerCommand(
        kharvox::psvr2::offCommand(false,
            kharvox::psvr2::TriggerOffReason::SessionEnd)),
        std::memory_order_release);
    desiredGeneration.store(1, std::memory_order_release);
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stopEvent.store(event, std::memory_order_release);
    if (!event) {
        workerStarted.store(false, std::memory_order_release);
        return;
    }
    const HANDLE worker = CreateThread(nullptr, 0, workerMain, nullptr, 0, nullptr);
    if (!worker) {
        CloseHandle(event);
        stopEvent.store(nullptr, std::memory_order_release);
        workerStarted.store(false, std::memory_order_release);
        return;
    }
    CloseHandle(worker);
}

void KharvoxPsvr2SubmitTrigger(
    const kharvox::psvr2::TriggerCommand& command) {
    if (!workerStarted.load(std::memory_order_relaxed)
        || !kharvox::psvr2::validTriggerCommand(command))
        return;
    const auto packed = kharvox::psvr2::packTriggerCommand(command);
    if (desiredCommand.exchange(packed, std::memory_order_acq_rel) != packed)
        desiredGeneration.fetch_add(1, std::memory_order_release);
}

void KharvoxPsvr2IpcRequestStop() {
    if (!workerStarted.load(std::memory_order_acquire)) return;
    stopRequested.store(true, std::memory_order_release);
    if (const HANDLE event = stopEvent.load(std::memory_order_acquire))
        SetEvent(event);
}
