#include "../src/bhaptics/BhapticsIpcProtocol.h"

#include <cstdint>
#include <cstring>
#include <string>

int main() {
    using namespace kharvox::bhaptics;
    const std::string token = "0123456789abcdef0123456789abcdef";

    const auto hello = encodeHello(1, 100, token);
    DecodedIpcMessage decoded{};
    if (validateIpcMessage(hello.bytes.data(), hello.size, &decoded)
            != IpcValidationResult::Valid
        || !constantTimeTokenEquals(decoded, token))
        return 1;

    auto badMagic = hello;
    badMagic.bytes[0] ^= 1;
    if (validateIpcMessage(badMagic.bytes.data(), badMagic.size)
        != IpcValidationResult::BadMagic)
        return 2;

    auto badVersion = hello;
    IpcMessageHeader header{};
    std::memcpy(&header, badVersion.bytes.data(), sizeof(header));
    ++header.version;
    std::memcpy(badVersion.bytes.data(), &header, sizeof(header));
    if (validateIpcMessage(badVersion.bytes.data(), badVersion.size)
        != IpcValidationResult::UnsupportedVersion)
        return 3;

    auto unknownType = hello;
    std::memcpy(&header, unknownType.bytes.data(), sizeof(header));
    header.type = 0x7fff;
    std::memcpy(unknownType.bytes.data(), &header, sizeof(header));
    if (validateIpcMessage(unknownType.bytes.data(), unknownType.size)
        != IpcValidationResult::UnknownType)
        return 4;

    auto oversized = hello;
    std::memcpy(&header, oversized.bytes.data(), sizeof(header));
    header.payloadBytes = static_cast<std::uint32_t>(ipcMaximumPayloadBytes + 1);
    std::memcpy(oversized.bytes.data(), &header, sizeof(header));
    if (validateIpcMessage(oversized.bytes.data(), oversized.size)
        != IpcValidationResult::PayloadTooLarge)
        return 5;

    if (validateIpcMessage(hello.bytes.data(), hello.size - 1)
        != IpcValidationResult::SizeMismatch)
        return 6;

    const auto rumble = encodeRumbleState(2, 101, 1234, 5678);
    if (validateIpcMessage(rumble.bytes.data(), rumble.size, &decoded)
            != IpcValidationResult::Valid)
        return 7;
    const auto payload = decodeRumbleState(decoded);
    if (payload.lowMotor != 1234 || payload.highMotor != 5678)
        return 8;

    const auto stop = encodeEmptyMessage(IpcMessageType::RumbleStop, 3, 102);
    const auto shutdown = encodeEmptyMessage(IpcMessageType::Shutdown, 4, 103);
    if (validateIpcMessage(stop.bytes.data(), stop.size) != IpcValidationResult::Valid
        || validateIpcMessage(shutdown.bytes.data(), shutdown.size)
            != IpcValidationResult::Valid)
        return 9;

    ServerSessionState session{};
    validateIpcMessage(hello.bytes.data(), hello.size, &decoded);
    if (handleServerMessage(session, decoded, token)
        != ServerSessionAction::Authorized)
        return 10;
    validateIpcMessage(rumble.bytes.data(), rumble.size, &decoded);
    if (handleServerMessage(session, decoded, token)
        != ServerSessionAction::Rumble)
        return 11;
    validateIpcMessage(stop.bytes.data(), stop.size, &decoded);
    if (handleServerMessage(session, decoded, token)
        != ServerSessionAction::Stop)
        return 12;
    validateIpcMessage(shutdown.bytes.data(), shutdown.size, &decoded);
    if (handleServerMessage(session, decoded, token)
        != ServerSessionAction::Shutdown)
        return 13;
    if (handleServerMessage(session, decoded, token)
        != ServerSessionAction::Reject)
        return 14;

    ServerSessionState badSession{};
    validateIpcMessage(hello.bytes.data(), hello.size, &decoded);
    if (handleServerMessage(badSession, decoded, "wrong-token")
        != ServerSessionAction::Reject)
        return 15;

    // A pipe loss creates a fresh session. The same launch token can authorize
    // the reconnect, while sequence validation starts from the new connection.
    ServerSessionState reconnected{};
    if (handleServerMessage(reconnected, decoded, token)
        != ServerSessionAction::Authorized)
        return 16;
    return 0;
}
