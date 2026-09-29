#include "../common/DiagnosticLogging.h"
#include <Windows.h>
#include <Aclapi.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "Psvr2IpcProtocol.h"
#include "Psvr2ToolkitBackend.h"

namespace {

constexpr std::uint64_t maximumLogBytes = 256 * 1024;
constexpr std::uint64_t normalPollMilliseconds = 250;
constexpr std::uint64_t retryMinimumMilliseconds = 500;
constexpr std::uint64_t retryMaximumMilliseconds = 8000;

std::atomic<bool> shutdownRequested{};
std::atomic<std::uint64_t> desiredCommand{};
HANDLE shutdownEvent{};

std::filesystem::path bridgeDirectory() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

std::filesystem::path logPath() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
    if (length == 0 || length >= path.size())
        return {};
    return std::filesystem::path(std::wstring(path.data(), length))
        / L"KHARVOX-psvr2-toolkit.log";
}

void writeLog(std::string_view message) noexcept {
    if (!kharvox::extendedDiagnosticsEnabled()) return;
    try {
        const auto path = logPath();
        if (std::filesystem::exists(path)
            && std::filesystem::file_size(path) >= maximumLogBytes)
            std::ofstream(path, std::ios::trunc).close();
        std::ofstream stream(path, std::ios::app);
        stream << '[' << GetTickCount64() << "] " << message << '\n';
    } catch (...) {
    }
}

struct RateLimitedLog {
    std::string lastMessage;
    std::uint64_t nextAllowed{};

    void write(std::string message, std::uint64_t intervalMilliseconds = 5000) {
        const auto now = GetTickCount64();
        if (message != lastMessage || now >= nextAllowed) {
            writeLog(message);
            lastMessage = std::move(message);
            nextAllowed = now + intervalMilliseconds;
        }
    }
};

std::wstring readWideEnvironment(const wchar_t* name, std::size_t maximum) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required <= 1 || required > maximum + 1) return {};
    std::wstring value(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied != required - 1) return {};
    value.resize(copied);
    return value;
}

std::string readAsciiEnvironment(const wchar_t* name, std::size_t maximum) {
    const auto wide = readWideEnvironment(name, maximum);
    if (wide.empty()) return {};
    std::string result;
    result.reserve(wide.size());
    for (const auto character : wide) {
        if (character < 0x20 || character > 0x7e) return {};
        result.push_back(static_cast<char>(character));
    }
    return result;
}

bool validPipeName(const std::wstring& name) {
    if (name.empty() || name.size() > 128) return false;
    for (const auto character : name) {
        if (!(character >= L'a' && character <= L'z')
            && !(character >= L'A' && character <= L'Z')
            && !(character >= L'0' && character <= L'9')
            && character != L'_' && character != L'-')
            return false;
    }
    return true;
}

DWORD readParentProcessId() {
    const auto text = readAsciiEnvironment(L"KHARVOX_PSVR2_PARENT_PID", 16);
    DWORD value{};
    const auto conversion = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return conversion.ec == std::errc{}
        && conversion.ptr == text.data() + text.size() ? value : 0;
}

struct CurrentUserSecurity {
    std::vector<std::uint8_t> tokenUserBuffer;
    PACL acl{};
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{};

    ~CurrentUserSecurity() { if (acl) LocalFree(acl); }

    bool initialize() {
        HANDLE token{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        DWORD bytes{};
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        tokenUserBuffer.resize(bytes);
        const bool ready = bytes != 0 && GetTokenInformation(token, TokenUser,
            tokenUserBuffer.data(), bytes, &bytes) != FALSE;
        CloseHandle(token);
        if (!ready) return false;

        const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(
            tokenUserBuffer.data());
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = static_cast<LPWSTR>(tokenUser->User.Sid);
        if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS)
            return false;
        if (!InitializeSecurityDescriptor(&descriptor,
                SECURITY_DESCRIPTOR_REVISION)
            || !SetSecurityDescriptorOwner(&descriptor,
                tokenUser->User.Sid, FALSE)
            || !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE))
            return false;
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = &descriptor;
        attributes.bInheritHandle = FALSE;
        return true;
    }
};

bool parentStillAlive(HANDLE parent) {
    return parent && WaitForSingleObject(parent, 0) == WAIT_TIMEOUT;
}

HANDLE createAndConnectPipe(const std::wstring& path,
    SECURITY_ATTRIBUTES* security, HANDLE parent) {
    HANDLE pipe = CreateNamedPipeW(path.c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 0, 4096, 1000, security);
    if (pipe == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
    }
    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    if (connected || GetLastError() == ERROR_PIPE_CONNECTED)
        SetEvent(overlapped.hEvent);
    else if (GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(overlapped.hEvent);
        CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
    }

    while (parentStillAlive(parent)
        && !shutdownRequested.load(std::memory_order_acquire)) {
        const HANDLE waits[]{overlapped.hEvent, parent, shutdownEvent};
        const DWORD wait = WaitForMultipleObjects(3, waits, FALSE, 250);
        if (wait == WAIT_OBJECT_0) {
            DWORD ignored{};
            const bool ready = GetOverlappedResult(pipe, &overlapped,
                &ignored, FALSE) != FALSE || GetLastError() == ERROR_PIPE_CONNECTED;
            CloseHandle(overlapped.hEvent);
            if (ready) return pipe;
            CloseHandle(pipe);
            return INVALID_HANDLE_VALUE;
        }
        if (wait == WAIT_OBJECT_0 + 1 || wait == WAIT_OBJECT_0 + 2) break;
    }
    CancelIoEx(pipe, &overlapped);
    WaitForSingleObject(overlapped.hEvent, 50);
    CloseHandle(overlapped.hEvent);
    CloseHandle(pipe);
    return INVALID_HANDLE_VALUE;
}

bool readExact(HANDLE pipe, HANDLE parent, void* destination,
    DWORD bytes, DWORD idleTimeoutMilliseconds) {
    auto* output = static_cast<std::uint8_t*>(destination);
    DWORD total{};
    while (total < bytes && parentStillAlive(parent)
        && !shutdownRequested.load(std::memory_order_acquire)) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) return false;
        DWORD read{};
        bool success = ReadFile(pipe, output + total, bytes - total,
            &read, &overlapped) != FALSE;
        if (!success && GetLastError() == ERROR_IO_PENDING) {
            const HANDLE waits[]{overlapped.hEvent, parent, shutdownEvent};
            const DWORD wait = WaitForMultipleObjects(3, waits, FALSE,
                idleTimeoutMilliseconds);
            if (wait == WAIT_OBJECT_0)
                success = GetOverlappedResult(pipe, &overlapped, &read, FALSE) != FALSE;
            else {
                CancelIoEx(pipe, &overlapped);
                WaitForSingleObject(overlapped.hEvent, 50);
                success = false;
            }
        }
        CloseHandle(overlapped.hEvent);
        if (!success || read == 0) return false;
        total += read;
    }
    return total == bytes;
}

bool readMessage(HANDLE pipe, HANDLE parent,
    kharvox::psvr2::EncodedIpcMessage& encoded,
    kharvox::psvr2::DecodedIpcMessage& decoded) {
    using namespace kharvox::psvr2;
    IpcMessageHeader header{};
    if (!readExact(pipe, parent, &header, sizeof(header), 1000)) return false;
    if (header.payloadBytes > ipcMaximumPayloadBytes) return false;
    encoded.size = sizeof(header) + header.payloadBytes;
    std::memcpy(encoded.bytes.data(), &header, sizeof(header));
    if (header.payloadBytes && !readExact(pipe, parent,
            encoded.bytes.data() + sizeof(header), header.payloadBytes, 1000))
        return false;
    return validateIpcMessage(encoded.bytes.data(), encoded.size, &decoded)
        == IpcValidationResult::Valid;
}

struct PipeThreadContext {
    std::wstring pipePath;
    std::string token;
    SECURITY_ATTRIBUTES* security{};
    HANDLE parent{};
};

DWORD WINAPI pipeThreadMain(void* rawContext) {
    using namespace kharvox::psvr2;
    const auto& context = *static_cast<PipeThreadContext*>(rawContext);
    while (parentStillAlive(context.parent)
        && !shutdownRequested.load(std::memory_order_acquire)) {
        HANDLE pipe = createAndConnectPipe(context.pipePath,
            context.security, context.parent);
        if (pipe == INVALID_HANDLE_VALUE) continue;
        ServerSessionState session{};
        bool connected = true;
        while (connected && parentStillAlive(context.parent)
            && !shutdownRequested.load(std::memory_order_acquire)) {
            EncodedIpcMessage encoded{};
            DecodedIpcMessage decoded{};
            if (!readMessage(pipe, context.parent, encoded, decoded)) break;
            switch (handleServerMessage(session, decoded, context.token)) {
            case ServerSessionAction::Authorized:
                break;
            case ServerSessionAction::TriggerState: {
                const auto command = decodeTriggerCommand(decoded);
                desiredCommand.store(packTriggerCommand(command),
                    std::memory_order_release);
                break;
            }
            case ServerSessionAction::Shutdown:
                shutdownRequested.store(true, std::memory_order_release);
                SetEvent(shutdownEvent);
                connected = false;
                break;
            case ServerSessionAction::Reject:
                connected = false;
                break;
            case ServerSessionAction::None:
                break;
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

int applyDesired(kharvox::psvr2::Psvr2ToolkitBackend& backend,
    const kharvox::psvr2::TriggerCommand& command,
    bool clearBothFirst) {
    using namespace kharvox::psvr2;
    if (clearBothFirst) {
        const int left = backend.applyOff(VRControllerType::Left);
        if (left != toolkitResultOk) return left;
        const int right = backend.applyOff(VRControllerType::Right);
        if (right != toolkitResultOk) return right;
        if (command.effect == TriggerEffect::Off) return toolkitResultOk;
    }
    return backend.apply(command);
}

void logApplied(const kharvox::psvr2::TriggerCommand& command) {
    using namespace kharvox::psvr2;
    if (command.effect == TriggerEffect::Off) {
        writeLog(std::string("[PSVR2TK] trigger effect off reason=")
            + triggerOffReasonName(command.offReason));
        return;
    }
    if (command.effect == TriggerEffect::Vibration) {
        writeLog(std::string("[PSVR2TK] ") + triggerHandName(command.hand)
            + " trigger vibration position=" + std::to_string(command.position)
            + " amplitude=" + std::to_string(command.amplitude)
            + " frequency=" + std::to_string(command.frequency));
        return;
    }
    if(command.effect==TriggerEffect::Feedback){
        writeLog(std::string("[PSVR2TK] ")+triggerHandName(command.hand)
            +" trigger feedback position="+std::to_string(command.position)
            +" strength="+std::to_string(command.strength));
        return;
    }
    if(command.effect==TriggerEffect::SlopeFeedback){
        writeLog(std::string("[PSVR2TK] ")+triggerHandName(command.hand)
            +" trigger slope start="+std::to_string(command.startPosition)
            +" end="+std::to_string(command.endPosition)
            +" strength="+std::to_string(command.startStrength)+"->"
            +std::to_string(command.endStrength));
        return;
    }
    if(command.effect==TriggerEffect::MultiplePositionFeedback
        ||command.effect==TriggerEffect::MultiplePositionVibration){
        std::string points;
        for(const auto point:command.controlPoints){
            if(!points.empty())points+=',';
            points+=std::to_string(point);
        }
        writeLog(std::string("[PSVR2TK] ")+triggerHandName(command.hand)
            +" trigger "+triggerProfileName(command)
            +(command.effect==TriggerEffect::MultiplePositionVibration
                ?" frequency="+std::to_string(command.frequency):"")
            +" points="+points);
        return;
    }
    writeLog(std::string("[PSVR2TK] ") + triggerHandName(command.hand)
        + " weapon effect profile=" + triggerProfileName(command)
        + " start=" + std::to_string(command.startPosition)
        + " end=" + std::to_string(command.endPosition)
        + " strength=" + std::to_string(command.strength));
}

} // namespace

int wmain() {
    using namespace kharvox::psvr2;
    try {
        writeLog("[PSVR2TK] bridge started");
        const auto pipeName = readWideEnvironment(L"KHARVOX_PSVR2_PIPE_NAME", 128);
        const auto token = readAsciiEnvironment(L"KHARVOX_PSVR2_SESSION_TOKEN",
            ipcMaximumTokenBytes);
        const DWORD parentId = readParentProcessId();
        if (!validPipeName(pipeName) || token.empty() || parentId == 0) {
            writeLog("[PSVR2TK] bridge configuration rejected");
            return 2;
        }
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentId);
        if (!parent) {
            writeLog("[PSVR2TK] parent unavailable");
            return 3;
        }
        CurrentUserSecurity security;
        if (!security.initialize()) {
            writeLog("[PSVR2TK] local IPC security unavailable");
            CloseHandle(parent);
            return 4;
        }

        shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!shutdownEvent) {
            CloseHandle(parent);
            return 5;
        }
        desiredCommand.store(packTriggerCommand(offCommand(false,
            TriggerOffReason::SessionEnd)), std::memory_order_release);

        PipeThreadContext pipeContext{
            L"\\\\.\\pipe\\" + pipeName, token, &security.attributes, parent};
        HANDLE pipeThread = CreateThread(nullptr, 0, pipeThreadMain,
            &pipeContext, 0, nullptr);
        if (!pipeThread) {
            CloseHandle(shutdownEvent);
            CloseHandle(parent);
            return 6;
        }

        Psvr2ToolkitBackend backend;
        TriggerDeliveryState delivery{};
        RateLimitedLog failureLog;
        bool loaderLogged{};
        bool driverWasActive{};
        bool driverStateKnown{};
        std::uint64_t connectionGeneration{};
        std::uint64_t retryDelay = retryMinimumMilliseconds;
        std::uint64_t retryAt{};
        std::uint64_t commandRetryAt{};

        while (!shutdownRequested.load(std::memory_order_acquire)
            && parentStillAlive(parent)) {
            const auto now = GetTickCount64();
            if (!backend.loaded() && now >= retryAt) {
                const auto loadResult = backend.loadFromDirectory(bridgeDirectory());
                if (loadResult != BackendLoadResult::Ready) {
                    if (loadResult == BackendLoadResult::CapiUnavailable) {
                        if (!loaderLogged) {
                            writeLog("[PSVR2TK] loader available");
                            loaderLogged = true;
                        }
                        failureLog.write("[PSVR2TK] CAPI unavailable");
                    } else {
                        failureLog.write(std::string("[PSVR2TK] CAPI unavailable result=")
                            + backendLoadResultName(loadResult));
                    }
                    retryAt = now + retryDelay;
                    retryDelay = (retryDelay * 2 > retryMaximumMilliseconds)
                        ? retryMaximumMilliseconds : retryDelay * 2;
                } else {
                    if (!loaderLogged) {
                        writeLog("[PSVR2TK] loader available");
                        loaderLogged = true;
                    }
                    const int initResult = backend.initialize();
                    if (initResult != toolkitResultOk) {
                        failureLog.write(std::string("[PSVR2TK] CAPI result=")
                            + toolkitResultName(initResult)
                            + " value=" + std::to_string(initResult));
                        backend.unload();
                        retryAt = now + retryDelay;
                        retryDelay = (retryDelay * 2 > retryMaximumMilliseconds)
                            ? retryMaximumMilliseconds : retryDelay * 2;
                    } else {
                        writeLog("[PSVR2TK] initialized");
                        retryDelay = retryMinimumMilliseconds;
                        retryAt = 0;
                    }
                }
            }

            if (backend.initialized()) {
                bool activeCallSucceeded{};
                const bool active = backend.driverActive(activeCallSucceeded);
                if (!activeCallSucceeded) {
                    failureLog.write("[PSVR2TK] CAPI result=driver-query-failed");
                    backend.unload();
                    driverWasActive = false;
                    driverStateKnown = false;
                    retryAt = now + retryDelay;
                } else if (!active) {
                    if (!driverStateKnown || driverWasActive)
                        writeLog("[PSVR2TK] driver inactive");
                    driverWasActive = false;
                    driverStateKnown = true;
                } else {
                    if (!driverWasActive) {
                        writeLog("[PSVR2TK] driver active");
                        driverWasActive = true;
                        driverStateKnown = true;
                        ++connectionGeneration;
                        commandRetryAt = 0;
                    }
                    const auto command = unpackTriggerCommand(
                        desiredCommand.load(std::memory_order_acquire));
                    if (validTriggerCommand(command)
                        && now >= commandRetryAt
                        && shouldApplyTriggerCommand(delivery, command,
                            connectionGeneration)) {
                        const bool reconnected = !delivery.hasLastApplied
                            || delivery.appliedConnectionGeneration
                                != connectionGeneration;
                        const bool handChanged = delivery.hasLastApplied
                            && delivery.lastApplied.hand != command.hand;
                        const int result = applyDesired(backend, command,
                            reconnected || handChanged);
                        if (result == toolkitResultOk) {
                            markTriggerCommandApplied(delivery, command,
                                connectionGeneration);
                            commandRetryAt = 0;
                            logApplied(command);
                        } else {
                            failureLog.write(std::string("[PSVR2TK] CAPI result=")
                                + toolkitResultName(result)
                                + " value=" + std::to_string(result));
                            if (result == toolkitResultDriverInactive) {
                                driverWasActive = false;
                                driverStateKnown = true;
                                writeLog("[PSVR2TK] driver inactive");
                            }
                            commandRetryAt = now + 1000;
                        }
                    }
                }
            }
            WaitForSingleObject(shutdownEvent,
                static_cast<DWORD>(normalPollMilliseconds));
        }

        shutdownRequested.store(true, std::memory_order_release);
        SetEvent(shutdownEvent);
        backend.shutdown();
        backend.unload();
        WaitForSingleObject(pipeThread, 2000);
        CloseHandle(pipeThread);
        CloseHandle(shutdownEvent);
        shutdownEvent = nullptr;
        CloseHandle(parent);
        writeLog("[PSVR2TK] bridge stopped");
        return 0;
    } catch (...) {
        writeLog("[PSVR2TK] bridge stopped after internal error");
        return 1;
    }
}
