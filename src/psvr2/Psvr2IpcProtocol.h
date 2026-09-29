#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>

#include "Psvr2TriggerPolicy.h"

namespace kharvox::psvr2 {

constexpr std::uint32_t ipcMagic = 0x3250484bu; // "KHP2" little endian.
constexpr std::uint16_t ipcProtocolVersion = 3;
constexpr std::size_t ipcMaximumTokenBytes = 128;
constexpr std::size_t ipcMaximumPayloadBytes = 128;
constexpr std::size_t ipcMaximumMessageBytes = 256;

enum class IpcMessageType : std::uint16_t {
    Hello = 1,
    TriggerState = 2,
    Shutdown = 3,
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

struct TriggerStatePayload {
    std::uint8_t effect{};
    std::uint8_t hand{};
    std::uint8_t offReason{};
    std::uint8_t startPosition{};
    std::uint8_t endPosition{};
    std::uint8_t strength{};
    std::uint8_t position{};
    std::uint8_t amplitude{};
    std::uint8_t frequency{};
    std::uint8_t startStrength{};
    std::uint8_t endStrength{};
    std::uint8_t controlPoints[10]{};
};
#pragma pack(pop)

static_assert(sizeof(IpcMessageHeader) == 24, "PSVR2 IPC header changed");
static_assert(sizeof(TriggerStatePayload) == 21, "PSVR2 IPC payload changed");

struct EncodedIpcMessage {
    std::array<std::uint8_t, ipcMaximumMessageBytes> bytes{};
    std::size_t size{};
};

struct DecodedIpcMessage {
    IpcMessageHeader header{};
    const std::uint8_t* payload{};
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

inline bool knownMessageType(std::uint16_t value) {
    switch (static_cast<IpcMessageType>(value)) {
    case IpcMessageType::Hello:
    case IpcMessageType::TriggerState:
    case IpcMessageType::Shutdown:
        return true;
    }
    return false;
}

inline TriggerCommand decodeTriggerCommand(const DecodedIpcMessage& message) {
    TriggerStatePayload payload{};
    if (static_cast<IpcMessageType>(message.header.type)
            == IpcMessageType::TriggerState
        && message.header.payloadBytes == sizeof(payload))
        std::memcpy(&payload, message.payload, sizeof(payload));
    TriggerCommand command{};
    command.effect=static_cast<TriggerEffect>(payload.effect);
    command.hand=static_cast<TriggerHand>(payload.hand);
    command.offReason=static_cast<TriggerOffReason>(payload.offReason);
    command.startPosition=payload.startPosition;
    command.endPosition=payload.endPosition;
    command.strength=payload.strength;
    command.position=payload.position;
    command.amplitude=payload.amplitude;
    command.frequency=payload.frequency;
    command.startStrength=payload.startStrength;
    command.endStrength=payload.endStrength;
    std::copy(std::begin(payload.controlPoints),std::end(payload.controlPoints),
        command.controlPoints.begin());
    return command;
}

inline IpcValidationResult validateIpcMessage(
    const void* data, std::size_t size, DecodedIpcMessage* decoded = nullptr) {
    if (!data || size < sizeof(IpcMessageHeader))
        return IpcValidationResult::TooSmall;
    IpcMessageHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != ipcMagic) return IpcValidationResult::BadMagic;
    if (header.version != ipcProtocolVersion)
        return IpcValidationResult::UnsupportedVersion;
    if (!knownMessageType(header.type))
        return IpcValidationResult::UnknownType;
    if (header.payloadBytes > ipcMaximumPayloadBytes)
        return IpcValidationResult::PayloadTooLarge;
    if (size != sizeof(header) + header.payloadBytes)
        return IpcValidationResult::SizeMismatch;

    const auto* payload = static_cast<const std::uint8_t*>(data) + sizeof(header);
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
    case IpcMessageType::TriggerState: {
        if (header.payloadBytes != sizeof(TriggerStatePayload))
            return IpcValidationResult::InvalidPayload;
        DecodedIpcMessage temporary{header, payload};
        if (!validTriggerCommand(decodeTriggerCommand(temporary)))
            return IpcValidationResult::InvalidPayload;
        break;
    }
    case IpcMessageType::Shutdown:
        if (header.payloadBytes != 0)
            return IpcValidationResult::InvalidPayload;
        break;
    }
    if (decoded) *decoded = {header, payload};
    return IpcValidationResult::Valid;
}

inline EncodedIpcMessage encodeIpcMessage(
    IpcMessageType type, std::uint32_t sequence,
    std::uint64_t milliseconds, const void* payload,
    std::uint32_t payloadBytes) {
    EncodedIpcMessage encoded{};
    if (payloadBytes > ipcMaximumPayloadBytes
        || (payloadBytes != 0 && !payload))
        return encoded;
    const IpcMessageHeader header{
        ipcMagic, ipcProtocolVersion, static_cast<std::uint16_t>(type),
        sequence, milliseconds, payloadBytes};
    encoded.size = sizeof(header) + payloadBytes;
    std::memcpy(encoded.bytes.data(), &header, sizeof(header));
    if (payloadBytes)
        std::memcpy(encoded.bytes.data() + sizeof(header), payload, payloadBytes);
    return encoded;
}

inline EncodedIpcMessage encodeHello(
    std::uint32_t sequence, std::uint64_t milliseconds,
    std::string_view token) {
    EncodedIpcMessage encoded{};
    if (token.empty() || token.size() > ipcMaximumTokenBytes)
        return encoded;
    std::array<std::uint8_t, sizeof(std::uint16_t) + ipcMaximumTokenBytes> payload{};
    const auto tokenBytes = static_cast<std::uint16_t>(token.size());
    std::memcpy(payload.data(), &tokenBytes, sizeof(tokenBytes));
    std::memcpy(payload.data() + sizeof(tokenBytes), token.data(), token.size());
    return encodeIpcMessage(IpcMessageType::Hello, sequence, milliseconds,
        payload.data(), static_cast<std::uint32_t>(sizeof(tokenBytes) + token.size()));
}

inline EncodedIpcMessage encodeTriggerState(
    std::uint32_t sequence, std::uint64_t milliseconds,
    const TriggerCommand& command) {
    if (!validTriggerCommand(command)) return {};
    TriggerStatePayload payload{};
    payload.effect=static_cast<std::uint8_t>(command.effect);
    payload.hand=static_cast<std::uint8_t>(command.hand);
    payload.offReason=static_cast<std::uint8_t>(command.offReason);
    payload.startPosition=command.startPosition;
    payload.endPosition=command.endPosition;
    payload.strength=command.strength;
    payload.position=command.position;
    payload.amplitude=command.amplitude;
    payload.frequency=command.frequency;
    payload.startStrength=command.startStrength;
    payload.endStrength=command.endStrength;
    std::copy(command.controlPoints.begin(),command.controlPoints.end(),
        std::begin(payload.controlPoints));
    return encodeIpcMessage(IpcMessageType::TriggerState, sequence,
        milliseconds, &payload, sizeof(payload));
}

inline EncodedIpcMessage encodeShutdown(
    std::uint32_t sequence, std::uint64_t milliseconds) {
    return encodeIpcMessage(IpcMessageType::Shutdown, sequence,
        milliseconds, nullptr, 0);
}

inline bool constantTimeTokenEquals(
    const DecodedIpcMessage& message, std::string_view expected) {
    if (static_cast<IpcMessageType>(message.header.type) != IpcMessageType::Hello
        || message.header.payloadBytes < sizeof(std::uint16_t))
        return false;
    std::uint16_t tokenBytes{};
    std::memcpy(&tokenBytes, message.payload, sizeof(tokenBytes));
    std::size_t difference = tokenBytes ^ expected.size();
    const auto count = tokenBytes < expected.size() ? tokenBytes : expected.size();
    for (std::size_t index = 0; index < count; ++index)
        difference |= message.payload[sizeof(tokenBytes) + index]
            ^ static_cast<std::uint8_t>(expected[index]);
    return difference == 0;
}

enum class ServerSessionAction {
    None,
    Authorized,
    TriggerState,
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
    case IpcMessageType::TriggerState: return ServerSessionAction::TriggerState;
    case IpcMessageType::Shutdown: return ServerSessionAction::Shutdown;
    case IpcMessageType::Hello: return ServerSessionAction::Reject;
    }
    return ServerSessionAction::Reject;
}

inline std::uint64_t packTriggerCommand(const TriggerCommand& command) {
    std::uint64_t packed=static_cast<std::uint64_t>(command.effect)&0x7u;
    packed|=(static_cast<std::uint64_t>(command.hand)&0x1u)<<3;
    const auto nibble=[&](unsigned shift,std::uint8_t value){
        packed|=(static_cast<std::uint64_t>(value)&0xfu)<<shift;
    };
    switch(command.effect){
    case TriggerEffect::Off:
        nibble(4,static_cast<std::uint8_t>(command.offReason));
        break;
    case TriggerEffect::Weapon:
        nibble(4,command.startPosition);nibble(8,command.endPosition);
        nibble(12,command.strength);
        break;
    case TriggerEffect::Vibration:
        nibble(4,command.position);nibble(8,command.amplitude);
        packed|=static_cast<std::uint64_t>(command.frequency)<<12;
        break;
    case TriggerEffect::Feedback:
        nibble(4,command.position);nibble(8,command.strength);
        break;
    case TriggerEffect::SlopeFeedback:
        nibble(4,command.startPosition);nibble(8,command.endPosition);
        nibble(12,command.startStrength);nibble(16,command.endStrength);
        break;
    case TriggerEffect::MultiplePositionFeedback:
        for(unsigned i=0;i<command.controlPoints.size();++i)
            nibble(4+i*4,command.controlPoints[i]);
        break;
    case TriggerEffect::MultiplePositionVibration:
        packed|=static_cast<std::uint64_t>(command.frequency)<<4;
        for(unsigned i=0;i<command.controlPoints.size();++i)
            nibble(12+i*4,command.controlPoints[i]);
        break;
    }
    return packed;
}

inline TriggerCommand unpackTriggerCommand(std::uint64_t packed) {
    TriggerCommand command{};
    command.effect=static_cast<TriggerEffect>(packed&0x7u);
    command.hand=static_cast<TriggerHand>((packed>>3)&0x1u);
    command.offReason=command.effect==TriggerEffect::Off
        ?static_cast<TriggerOffReason>((packed>>4)&0xfu)
        :TriggerOffReason::None;
    const auto nibble=[&](unsigned shift){
        return static_cast<std::uint8_t>((packed>>shift)&0xfu);
    };
    switch(command.effect){
    case TriggerEffect::Off: break;
    case TriggerEffect::Weapon:
        command.startPosition=nibble(4);command.endPosition=nibble(8);
        command.strength=nibble(12);break;
    case TriggerEffect::Vibration:
        command.position=nibble(4);command.amplitude=nibble(8);
        command.frequency=static_cast<std::uint8_t>((packed>>12)&0xffu);break;
    case TriggerEffect::Feedback:
        command.position=nibble(4);command.strength=nibble(8);break;
    case TriggerEffect::SlopeFeedback:
        command.startPosition=nibble(4);command.endPosition=nibble(8);
        command.startStrength=nibble(12);command.endStrength=nibble(16);break;
    case TriggerEffect::MultiplePositionFeedback:
        for(unsigned i=0;i<command.controlPoints.size();++i)
            command.controlPoints[i]=nibble(4+i*4);
        break;
    case TriggerEffect::MultiplePositionVibration:
        command.frequency=static_cast<std::uint8_t>((packed>>4)&0xffu);
        for(unsigned i=0;i<command.controlPoints.size();++i)
            command.controlPoints[i]=nibble(12+i*4);
        break;
    }
    return command;
}

struct TriggerDeliveryState {
    TriggerCommand lastApplied{};
    bool hasLastApplied{};
    std::uint64_t appliedConnectionGeneration{};
};

inline bool materiallySameTriggerEffect(
    const TriggerCommand& left, const TriggerCommand& right) {
    if (left.effect != right.effect || left.hand != right.hand) return false;
    if (left.effect == TriggerEffect::Off) return true;
    return left == right;
}

inline bool shouldApplyTriggerCommand(
    const TriggerDeliveryState& state, const TriggerCommand& desired,
    std::uint64_t connectionGeneration) {
    return !state.hasLastApplied
        || !materiallySameTriggerEffect(state.lastApplied, desired)
        || state.appliedConnectionGeneration != connectionGeneration;
}

inline void markTriggerCommandApplied(
    TriggerDeliveryState& state, const TriggerCommand& command,
    std::uint64_t connectionGeneration) {
    state.lastApplied = command;
    state.hasLastApplied = true;
    state.appliedConnectionGeneration = connectionGeneration;
}

} // namespace kharvox::psvr2
