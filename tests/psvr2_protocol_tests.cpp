#include <cstring>
#include <iostream>
#include <string>

#include "../src/psvr2/Psvr2IpcProtocol.h"

namespace {
int failures{};
void require(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
        ++failures;
    }
}
}

int main() {
    using namespace kharvox::psvr2;
    const std::string token(64, 'a');
    const auto hello = encodeHello(1, 10, token);
    DecodedIpcMessage decoded{};
    require(validateIpcMessage(hello.bytes.data(), hello.size, &decoded)
        == IpcValidationResult::Valid, "hello valid");
    require(constantTimeTokenEquals(decoded, token), "token accepted");
    require(!constantTimeTokenEquals(decoded, std::string(64, 'b')),
        "wrong token rejected");

    auto badVersion = hello;
    auto* versionHeader = reinterpret_cast<IpcMessageHeader*>(badVersion.bytes.data());
    versionHeader->version = ipcProtocolVersion + 1;
    require(validateIpcMessage(badVersion.bytes.data(), badVersion.size)
        == IpcValidationResult::UnsupportedVersion, "version rejected");

    ServerSessionState session{};
    require(handleServerMessage(session, decoded, token)
        == ServerSessionAction::Authorized, "session authorized");

    const auto heavy = weaponCommand(false, 2, 4, 7);
    const auto trigger = encodeTriggerState(2, 20, heavy);
    require(validateIpcMessage(trigger.bytes.data(), trigger.size, &decoded)
        == IpcValidationResult::Valid, "weapon payload valid");
    require(handleServerMessage(session, decoded, token)
        == ServerSessionAction::TriggerState, "trigger accepted");
    require(decodeTriggerCommand(decoded) == heavy, "trigger round trip");

    auto invalidRange = trigger;
    auto* payload = reinterpret_cast<TriggerStatePayload*>(
        invalidRange.bytes.data() + sizeof(IpcMessageHeader));
    payload->startPosition = 1;
    require(validateIpcMessage(invalidRange.bytes.data(), invalidRange.size)
        == IpcValidationResult::InvalidPayload, "range rejected");

    auto invalidEffect = trigger;
    payload = reinterpret_cast<TriggerStatePayload*>(
        invalidEffect.bytes.data() + sizeof(IpcMessageHeader));
    payload->effect = 99;
    require(validateIpcMessage(invalidEffect.bytes.data(), invalidEffect.size)
        == IpcValidationResult::InvalidPayload, "effect rejected");

    const auto vibration = vibrationCommand(true, 2, 7, 50);
    const auto vibrationMessage = encodeTriggerState(3, 21, vibration);
    require(validateIpcMessage(vibrationMessage.bytes.data(),
        vibrationMessage.size, &decoded) == IpcValidationResult::Valid,
        "vibration payload valid");
    require(decodeTriggerCommand(decoded) == vibration,
        "vibration payload round trip");
    require(unpackTriggerCommand(packTriggerCommand(vibration)) == vibration,
        "vibration atomic pack round trip");

    auto invalidVibration = vibrationMessage;
    payload = reinterpret_cast<TriggerStatePayload*>(
        invalidVibration.bytes.data() + sizeof(IpcMessageHeader));
    payload->amplitude = 9;
    require(validateIpcMessage(invalidVibration.bytes.data(),
        invalidVibration.size) == IpcValidationResult::InvalidPayload,
        "vibration amplitude rejected");

    for(const auto& advanced:{
            feedbackCommand(false,2,7),
            slopeFeedbackCommand(true,2,8,2,7),
            multiplePositionFeedbackCommand(false,
                superShotgunFeedbackPoints),
            multiplePositionVibrationCommand(true,45,
                chainsawVibrationPoints)}){
        const auto message=encodeTriggerState(4,22,advanced);
        require(validateIpcMessage(message.bytes.data(),message.size,&decoded)
            ==IpcValidationResult::Valid,"advanced mode payload valid");
        require(decodeTriggerCommand(decoded)==advanced,
            "advanced mode IPC round trip");
        require(unpackTriggerCommand(packTriggerCommand(advanced))==advanced,
            "advanced mode atomic round trip");
    }

    auto invalidPoints=encodeTriggerState(4,22,
        multiplePositionFeedbackCommand(false,superShotgunFeedbackPoints));
    payload=reinterpret_cast<TriggerStatePayload*>(
        invalidPoints.bytes.data()+sizeof(IpcMessageHeader));
    payload->controlPoints[4]=9;
    require(validateIpcMessage(invalidPoints.bytes.data(),invalidPoints.size)
        ==IpcValidationResult::InvalidPayload,"control point rejected");

    auto invalidReason = encodeTriggerState(4, 22,
        offCommand(false, TriggerOffReason::Menu));
    payload = reinterpret_cast<TriggerStatePayload*>(
        invalidReason.bytes.data() + sizeof(IpcMessageHeader));
    payload->offReason = 99;
    require(validateIpcMessage(invalidReason.bytes.data(), invalidReason.size)
        == IpcValidationResult::InvalidPayload, "off reason rejected");

    const auto shutdown = encodeShutdown(5, 30);
    require(validateIpcMessage(shutdown.bytes.data(), shutdown.size, &decoded)
        == IpcValidationResult::Valid, "shutdown valid");
    require(handleServerMessage(session, decoded, token)
        == ServerSessionAction::Shutdown, "shutdown accepted");

    ServerSessionState unauthorized{};
    require(handleServerMessage(unauthorized, decoded, token)
        == ServerSessionAction::Reject, "pre-auth command rejected");

    return failures == 0 ? 0 : 1;
}
