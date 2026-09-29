#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace kharvox::bhaptics {

constexpr std::uint32_t ipcMagic = 0x3242484bu; // "KHB2" in little endian.
constexpr std::uint16_t ipcProtocolVersion = 1;
constexpr std::size_t ipcMaximumTokenBytes = 128;
constexpr std::size_t ipcMaximumPayloadBytes = 256;
constexpr std::size_t ipcMaximumMessageBytes = 512;

enum class IpcMessageType : std::uint16_t {
    Hello = 1,
    RumbleState = 2,
    RumbleStop = 3,
    Shutdown = 4,
};

#pragma pack(push, 1)
struct IpcMessageHeader {
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t type{};
    std::uint32_t sequence{};
    std::uint64_t monotonicMilliseconds{};
    std::uint32_t payloadBytes{};
};

struct RumbleStatePayload {
    std::uint16_t lowMotor{};
    std::uint16_t highMotor{};
};
#pragma pack(pop)

static_assert(sizeof(IpcMessageHeader) == 24, "IPC header layout changed");
static_assert(sizeof(RumbleStatePayload) == 4, "IPC rumble layout changed");

struct EncodedIpcMessage {
    std::array<std::uint8_t, ipcMaximumMessageBytes> bytes{};
    std::size_t size{};
};

enum class IpcValidationResult {
    Valid,
    TooSmall,
    BadMagic,
    UnsupportedVersion,
    UnknownType,
    PayloadTooLarge,
    SizeMismatch,
    InvalidPayload,
};

struct DecodedIpcMessage {
    IpcMessageHeader header{};
    const std::uint8_t* payload{};
};

inline bool isKnownMessageType(std::uint16_t value) {
    switch (static_cast<IpcMessageType>(value)) {
    case IpcMessageType::Hello:
    case IpcMessageType::RumbleState:
    case IpcMessageType::RumbleStop:
    case IpcMessageType::Shutdown:
        return true;
    }
    return false;
}

inline IpcValidationResult validateIpcMessage(
    const void* data, std::size_t size, DecodedIpcMessage* decoded = nullptr) {
    if (!data || size < sizeof(IpcMessageHeader))
        return IpcValidationResult::TooSmall;

    IpcMessageHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != ipcMagic)
        return IpcValidationResult::BadMagic;
    if (header.version != ipcProtocolVersion)
        return IpcValidationResult::UnsupportedVersion;
    if (!isKnownMessageType(header.type))
        return IpcValidationResult::UnknownType;
    if (header.payloadBytes > ipcMaximumPayloadBytes)
        return IpcValidationResult::PayloadTooLarge;
    if (size != sizeof(IpcMessageHeader) + header.payloadBytes)
        return IpcValidationResult::SizeMismatch;

    const auto* payload = static_cast<const std::uint8_t*>(data)
        + sizeof(IpcMessageHeader);
    switch (static_cast<IpcMessageType>(header.type)) {
    case IpcMessageType::Hello: {
        if (header.payloadBytes < sizeof(std::uint16_t))
            return IpcValidationResult::InvalidPayload;
        std::uint16_t tokenBytes{};
        std::memcpy(&tokenBytes, payload, sizeof(tokenBytes));
        if (tokenBytes == 0 || tokenBytes > ipcMaximumTokenBytes
            || header.payloadBytes != sizeof(tokenBytes) + tokenBytes)
            return IpcValidationResult::InvalidPayload;
        break;
    }
    case IpcMessageType::RumbleState:
        if (header.payloadBytes != sizeof(RumbleStatePayload))
            return IpcValidationResult::InvalidPayload;
        break;
    case IpcMessageType::RumbleStop:
    case IpcMessageType::Shutdown:
        if (header.payloadBytes != 0)
            return IpcValidationResult::InvalidPayload;
        break;
    }

    if (decoded) {
        decoded->header = header;
        decoded->payload = payload;
    }
    return IpcValidationResult::Valid;
}

inline EncodedIpcMessage encodeIpcMessage(
    IpcMessageType type, std::uint32_t sequence,
    std::uint64_t monotonicMilliseconds,
    const void* payload, std::uint32_t payloadBytes) {
    EncodedIpcMessage encoded{};
    if (payloadBytes > ipcMaximumPayloadBytes
        || (payloadBytes != 0 && !payload))
        return encoded;
    IpcMessageHeader header{
        ipcMagic,
        ipcProtocolVersion,
        static_cast<std::uint16_t>(type),
        sequence,
        monotonicMilliseconds,
        payloadBytes
    };
    encoded.size = sizeof(header) + payloadBytes;
    std::memcpy(encoded.bytes.data(), &header, sizeof(header));
    if (payloadBytes)
        std::memcpy(encoded.bytes.data() + sizeof(header), payload, payloadBytes);
    return encoded;
}

inline EncodedIpcMessage encodeHello(
    std::uint32_t sequence, std::uint64_t monotonicMilliseconds,
    std::string_view token) {
    EncodedIpcMessage encoded{};
    if (token.empty() || token.size() > ipcMaximumTokenBytes)
        return encoded;
    std::array<std::uint8_t, sizeof(std::uint16_t) + ipcMaximumTokenBytes> payload{};
    const auto tokenBytes = static_cast<std::uint16_t>(token.size());
    std::memcpy(payload.data(), &tokenBytes, sizeof(tokenBytes));
    std::memcpy(payload.data() + sizeof(tokenBytes), token.data(), token.size());
    return encodeIpcMessage(IpcMessageType::Hello, sequence,
        monotonicMilliseconds, payload.data(),
        static_cast<std::uint32_t>(sizeof(tokenBytes) + token.size()));
}

inline EncodedIpcMessage encodeRumbleState(
    std::uint32_t sequence, std::uint64_t monotonicMilliseconds,
    std::uint16_t lowMotor, std::uint16_t highMotor) {
    const RumbleStatePayload payload{lowMotor, highMotor};
    return encodeIpcMessage(IpcMessageType::RumbleState, sequence,
        monotonicMilliseconds, &payload, sizeof(payload));
}

inline EncodedIpcMessage encodeEmptyMessage(
    IpcMessageType type, std::uint32_t sequence,
    std::uint64_t monotonicMilliseconds) {
    return encodeIpcMessage(type, sequence, monotonicMilliseconds, nullptr, 0);
}

inline bool constantTimeTokenEquals(
    const DecodedIpcMessage& message, std::string_view expected) {
    if (static_cast<IpcMessageType>(message.header.type) != IpcMessageType::Hello
        || message.header.payloadBytes < sizeof(std::uint16_t))
        return false;
    std::uint16_t tokenBytes{};
    std::memcpy(&tokenBytes, message.payload, sizeof(tokenBytes));
    std::size_t difference = tokenBytes ^ expected.size();
    const auto compareBytes = tokenBytes < expected.size()
        ? tokenBytes : expected.size();
    for (std::size_t i = 0; i < compareBytes; ++i)
        difference |= message.payload[sizeof(tokenBytes) + i]
            ^ static_cast<std::uint8_t>(expected[i]);
    return difference == 0;
}

inline RumbleStatePayload decodeRumbleState(const DecodedIpcMessage& message) {
    RumbleStatePayload payload{};
    if (static_cast<IpcMessageType>(message.header.type) == IpcMessageType::RumbleState
        && message.header.payloadBytes == sizeof(payload))
        std::memcpy(&payload, message.payload, sizeof(payload));
    return payload;
}

enum class ServerSessionAction {
    None,
    Authorized,
    Rumble,
    Stop,
    Shutdown,
    Reject,
};

struct ServerSessionState {
    bool authorized{};
    std::uint32_t lastSequence{};
};

inline ServerSessionAction handleServerMessage(
    ServerSessionState& state, const DecodedIpcMessage& message,
    std::string_view expectedToken) {
    if (message.header.sequence <= state.lastSequence)
        return ServerSessionAction::Reject;
    state.lastSequence = message.header.sequence;
    const auto type = static_cast<IpcMessageType>(message.header.type);
    if (!state.authorized) {
        if (type != IpcMessageType::Hello
            || !constantTimeTokenEquals(message, expectedToken))
            return ServerSessionAction::Reject;
        state.authorized = true;
        return ServerSessionAction::Authorized;
    }
    switch (type) {
    case IpcMessageType::RumbleState: return ServerSessionAction::Rumble;
    case IpcMessageType::RumbleStop: return ServerSessionAction::Stop;
    case IpcMessageType::Shutdown: return ServerSessionAction::Shutdown;
    case IpcMessageType::Hello: return ServerSessionAction::Reject;
    }
    return ServerSessionAction::Reject;
}

} // namespace kharvox::bhaptics
