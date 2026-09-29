#include "../common/DiagnosticLogging.h"
#include <Windows.h>
#include <Aclapi.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "BhapticsIpcProtocol.h"
#include "BhapticsMappingPolicy.h"
#include "BhapticsSdkBackend.h"

namespace {

constexpr std::uint64_t maximumLogBytes = 256 * 1024;

std::filesystem::path bridgeDirectory() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

std::filesystem::path logPath() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
    if (length == 0 || length >= path.size())
        return {};
    return std::filesystem::path(std::wstring(path.data(), length))
        / L"KHARVOX-bhaptics.log";
}

void writeLog(std::string_view message) noexcept {
    if (!kharvox::extendedDiagnosticsEnabled()) return;
    try {
        const auto path = logPath();
        if (std::filesystem::exists(path)
            && std::filesystem::file_size(path) >= maximumLogBytes)
            std::ofstream(path, std::ios::trunc).close();
        std::ofstream stream(path, std::ios::app);
        stream << "[" << GetTickCount64() << "] " << message << '\n';
    } catch (...) {
    }
}

std::wstring readWideEnvironment(const wchar_t* name, std::size_t maximum) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required <= 1 || required > maximum + 1)
        return {};
    std::wstring value(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied != required - 1)
        return {};
    value.resize(copied);
    return value;
}

std::string readAsciiEnvironment(const wchar_t* name, std::size_t maximum) {
    const auto wide = readWideEnvironment(name, maximum);
    if (wide.empty())
        return {};
    std::string result;
    result.reserve(wide.size());
    for (const auto character : wide) {
        if (character < 0x20 || character > 0x7e)
            return {};
        result.push_back(static_cast<char>(character));
    }
    return result;
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

DWORD readParentProcessId() {
    const auto text = readAsciiEnvironment(L"KHARVOX_BHAPTICS_PARENT_PID", 16);
    DWORD value{};
    const auto conversion = std::from_chars(text.data(), text.data() + text.size(), value);
    return conversion.ec == std::errc{} && conversion.ptr == text.data() + text.size()
        ? value : 0;
}

float readIntensityScale() {
    const auto text = readAsciiEnvironment(L"KHARVOX_BHAPTICS_INTENSITY", 16);
    if (text.empty())
        return kharvox::bhaptics::defaultIntensityScale;
    char* end{};
    const float value = std::strtof(text.c_str(), &end);
    if (!end || *end != '\0')
        return kharvox::bhaptics::defaultIntensityScale;
    return std::clamp(value, 0.0f,
        kharvox::bhaptics::maximumIntensityScale);
}

struct CurrentUserSecurity {
    std::vector<std::uint8_t> tokenUserBuffer;
    PACL acl{};
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{};

    ~CurrentUserSecurity() {
        if (acl)
            LocalFree(acl);
    }

    bool initialize() {
        HANDLE token{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        DWORD bytes{};
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        tokenUserBuffer.resize(bytes);
        const bool tokenReady = bytes != 0
            && GetTokenInformation(token, TokenUser,
                tokenUserBuffer.data(), bytes, &bytes) != FALSE;
        CloseHandle(token);
        if (!tokenReady)
            return false;

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
    if (pipe == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
    }
    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    if (connected) {
        SetEvent(overlapped.hEvent);
    } else {
        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED)
            SetEvent(overlapped.hEvent);
        else if (error != ERROR_IO_PENDING) {
            CloseHandle(overlapped.hEvent);
            CloseHandle(pipe);
            return INVALID_HANDLE_VALUE;
        }
    }

    while (parentStillAlive(parent)) {
        const HANDLE waits[]{overlapped.hEvent, parent};
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 250);
        if (wait == WAIT_OBJECT_0) {
            DWORD ignored{};
            const bool ready = GetOverlappedResult(pipe, &overlapped,
                &ignored, FALSE) != FALSE || GetLastError() == ERROR_PIPE_CONNECTED;
            CloseHandle(overlapped.hEvent);
            if (ready)
                return pipe;
            CloseHandle(pipe);
            return INVALID_HANDLE_VALUE;
        }
        if (wait == WAIT_OBJECT_0 + 1)
            break;
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
    while (total < bytes && parentStillAlive(parent)) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent)
            return false;
        DWORD read{};
        bool success = ReadFile(pipe, output + total, bytes - total,
            &read, &overlapped) != FALSE;
        if (!success && GetLastError() == ERROR_IO_PENDING) {
            const HANDLE waits[]{overlapped.hEvent, parent};
            const DWORD wait = WaitForMultipleObjects(2, waits, FALSE,
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
        if (!success || read == 0)
            return false;
        total += read;
    }
    return total == bytes;
}

bool readMessage(HANDLE pipe, HANDLE parent,
    kharvox::bhaptics::EncodedIpcMessage& encoded,
    kharvox::bhaptics::DecodedIpcMessage& decoded) {
    using namespace kharvox::bhaptics;
    IpcMessageHeader header{};
    if (!readExact(pipe, parent, &header, sizeof(header), 3000))
        return false;
    if (header.payloadBytes > ipcMaximumPayloadBytes)
        return false;
    encoded.size = sizeof(header) + header.payloadBytes;
    std::memcpy(encoded.bytes.data(), &header, sizeof(header));
    if (header.payloadBytes && !readExact(pipe, parent,
            encoded.bytes.data() + sizeof(header), header.payloadBytes, 1000))
        return false;
    return validateIpcMessage(encoded.bytes.data(), encoded.size, &decoded)
        == IpcValidationResult::Valid;
}

const char* initializationResultName(
    kharvox::bhaptics::BackendInitializationResult result) {
    using kharvox::bhaptics::BackendInitializationResult;
    switch (result) {
    case BackendInitializationResult::Ready: return "ready";
    case BackendInitializationResult::MissingCredentials: return "credentials unavailable";
    case BackendInitializationResult::PlayerNotInstalled: return "Player not installed";
    case BackendInitializationResult::PlayerNotRunning: return "Player did not start";
    case BackendInitializationResult::RegistrationFailed: return "SDK registration failed";
    case BackendInitializationResult::ConnectionTimeout: return "Player connection timed out";
    }
    return "unavailable";
}

} // namespace

int wmain() {
    using namespace kharvox::bhaptics;
    try {
        writeLog("bridge starting");
        const auto pipeName = readWideEnvironment(L"KHARVOX_BHAPTICS_PIPE_NAME", 128);
        const auto token = readAsciiEnvironment(L"KHARVOX_BHAPTICS_SESSION_TOKEN",
            ipcMaximumTokenBytes);
        const DWORD parentId = readParentProcessId();
        if (!validPipeName(pipeName) || token.empty() || parentId == 0) {
            writeLog("bridge configuration rejected");
            return 2;
        }

        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentId);
        if (!parent) {
            writeLog("launcher process unavailable");
            return 3;
        }

        CurrentUserSecurity security;
        if (!security.initialize()) {
            writeLog("current-user pipe security initialization failed");
            CloseHandle(parent);
            return 4;
        }

        DynamicBhapticsBackend backend;
        std::string loadFailure;
        // The launcher selects either a release-local SDK or the user's shared
        // SDK installation. Never search PATH or the working directory.
        const std::filesystem::path sharedSdk = readWideEnvironment(
            L"KHARVOX_BHAPTICS_SDK_DIRECTORY", 32767);
        const auto sdkDirectory = sharedSdk.is_absolute() ? sharedSdk : bridgeDirectory();
        bool backendReady = backend.loadFromDirectory(
            sdkDirectory.wstring(), loadFailure);
        if (!backendReady)
            writeLog(loadFailure);
        else {
            const auto appId = readAsciiEnvironment(L"KHARVOX_BHAPTICS_APP_ID", 256);
            const auto apiKey = readAsciiEnvironment(L"KHARVOX_BHAPTICS_API_KEY", 256);
            writeLog(appId.empty() && apiKey.empty()
                ? "SDK mode: local dot playback (no portal access)"
                : "SDK mode: private portal-linked development");
            // A cold Player (Electron/WebView plus device services) regularly needs
            // more than the former two-second window. This wait is for the local
            // Player websocket only; device/vest presence is deliberately not part
            // of startup readiness.
            const auto initialization = initializeBackend(backend, appId, apiKey, 40,
                [] { Sleep(250); });
            backendReady = initialization == BackendInitializationResult::Ready;
            writeLog(std::string("SDK status: ") + initializationResultName(initialization));
        }

        BhapticsRumbleEngine engine(backend, readIntensityScale());
        const std::wstring pipePath = L"\\\\.\\pipe\\" + pipeName;
        bool shutdown = false;
        bool authorizationLogged = false;
        bool firstRumbleLogged = false;
        bool firstPlaybackLogged = false;
        int loggedDeviceState = -1;
        while (!shutdown && parentStillAlive(parent)) {
            HANDLE pipe = createAndConnectPipe(pipePath, &security.attributes, parent);
            if (pipe == INVALID_HANDLE_VALUE)
                continue;

            ServerSessionState session{};
            bool connected = true;
            while (connected && !shutdown && parentStillAlive(parent)) {
                EncodedIpcMessage encoded{};
                DecodedIpcMessage decoded{};
                if (!readMessage(pipe, parent, encoded, decoded))
                    break;
                const auto action = handleServerMessage(session, decoded, token);
                switch (action) {
                case ServerSessionAction::Authorized:
                    if (!authorizationLogged) {
                        writeLog("client IPC authenticated");
                        authorizationLogged = true;
                    }
                    break;
                case ServerSessionAction::Rumble: {
                    const auto rumble = decodeRumbleState(decoded);
                    if (backendReady) {
                        if (!firstRumbleLogged) {
                            writeLog("first non-zero game rumble received");
                            firstRumbleLogged = true;
                        }
                        engine.setRawRumble(rumble.lowMotor, rumble.highMotor);
                        engine.tick(GetTickCount64());
                        const int deviceState = engine.deviceConnected() ? 1 : 0;
                        if (deviceState != loggedDeviceState) {
                            writeLog(deviceState
                                ? "SDK device query reports TactSuit connected"
                                : "SDK device query does not report TactSuit; playback probe remains enabled");
                            loggedDeviceState = deviceState;
                        }
                        const auto mapped = mapRumbleToTactSuit(
                            rumble.lowMotor, rumble.highMotor,
                            readIntensityScale());
                        if (!firstPlaybackLogged && mapped.active) {
                            writeLog(engine.currentRequestId() > 0
                                ? "first playDot request accepted"
                                : "first playDot request was not accepted");
                            firstPlaybackLogged = true;
                        }
                    }
                    break;
                }
                case ServerSessionAction::Stop:
                    if (backendReady) {
                        engine.setRawRumble(0, 0);
                        engine.tick(GetTickCount64());
                    }
                    break;
                case ServerSessionAction::Shutdown:
                    shutdown = true;
                    break;
                case ServerSessionAction::Reject:
                    connected = false;
                    break;
                case ServerSessionAction::None:
                    break;
                }
            }
            if (backendReady)
                engine.disconnected(GetTickCount64());
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }

        engine.shutdown();
        backend.unload();
        CloseHandle(parent);
        writeLog("bridge stopped cleanly");
        return 0;
    } catch (...) {
        writeLog("bridge stopped after an internal error");
        return 1;
    }
}
