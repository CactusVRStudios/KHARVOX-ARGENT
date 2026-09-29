#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <iterator>

#include <psvr2tk_capi.h>

namespace {
std::atomic<int> initCalls{};
std::atomic<int> deinitCalls{};
std::atomic<int> setCalls{};
std::atomic<int> offCalls{};
std::atomic<int> lastController{-1};
std::atomic<int> lastMode{-1};
std::atomic<int> lastStart{};
std::atomic<int> lastEnd{};
std::atomic<int> lastStrength{};
std::atomic<int> lastFourth{};
std::array<std::atomic<int>,10> lastPoints{};

int environmentInt(const wchar_t* name, int fallback) {
    wchar_t buffer[32]{};
    if (!GetEnvironmentVariableW(name, buffer,
            static_cast<DWORD>(std::size(buffer))))
        return fallback;
    wchar_t* end{};
    const long value = ::wcstol(buffer, &end, 10);
    return end && *end == L'\0' ? static_cast<int>(value) : fallback;
}
}

extern "C" __declspec(dllexport) void fake_psvr2_reset() {
    initCalls = 0;
    deinitCalls = 0;
    setCalls = 0;
    offCalls = 0;
    lastController = -1;
    lastMode = -1;
    lastStart = 0;
    lastEnd = 0;
    lastStrength = 0;
    lastFourth = 0;
    for(auto&point:lastPoints)point=0;
}

extern "C" __declspec(dllexport) int fake_psvr2_get(int key) {
    switch (key) {
    case 0: return initCalls;
    case 1: return deinitCalls;
    case 2: return setCalls;
    case 3: return offCalls;
    case 4: return lastController;
    case 5: return lastMode;
    case 6: return lastStart;
    case 7: return lastEnd;
    case 8: return lastStrength;
    case 9: return lastFourth;
    default: break;
    }
    if(key>=10&&key<20)return lastPoints[key-10];
    return -1;
}

extern "C" __declspec(dllexport) void* psvr2_toolkit_loader_get_module_handle() {
    if (environmentInt(L"KHARVOX_FAKE_PSVR2_MODULE", 1) == 0) return nullptr;
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&psvr2_toolkit_loader_get_module_handle),
        &module);
    return module;
}

extern "C" __declspec(dllexport) int psvr2_toolkit_init() {
    ++initCalls;
    return environmentInt(L"KHARVOX_FAKE_PSVR2_INIT_RESULT", 0);
}

extern "C" __declspec(dllexport) void psvr2_toolkit_deinit() {
    ++deinitCalls;
}

extern "C" __declspec(dllexport) bool psvr2_toolkit_get_driver_active() {
    return environmentInt(L"KHARVOX_FAKE_PSVR2_DRIVER_ACTIVE", 1) != 0;
}

extern "C" __declspec(dllexport) int psvr2_toolkit_set_trigger_effect(
    VRControllerType controller, const ScePadTriggerEffectCommand& command) {
    ++setCalls;
    lastController = static_cast<int>(controller);
    lastMode = static_cast<int>(command.mode);
    if (command.mode == SCE_PAD_TRIGGER_EFFECT_MODE_VIBRATION) {
        lastStart = command.commandData.vibrationParam.position;
        lastEnd = command.commandData.vibrationParam.amplitude;
        lastStrength = command.commandData.vibrationParam.frequency;
    } else if(command.mode==SCE_PAD_TRIGGER_EFFECT_MODE_WEAPON) {
        lastStart = command.commandData.weaponParam.startPosition;
        lastEnd = command.commandData.weaponParam.endPosition;
        lastStrength = command.commandData.weaponParam.strength;
    } else if(command.mode==SCE_PAD_TRIGGER_EFFECT_MODE_FEEDBACK){
        lastStart=command.commandData.feedbackParam.position;
        lastEnd=command.commandData.feedbackParam.strength;
    } else if(command.mode==SCE_PAD_TRIGGER_EFFECT_MODE_SLOPE_FEEDBACK){
        lastStart=command.commandData.slopeFeedbackParam.startPosition;
        lastEnd=command.commandData.slopeFeedbackParam.endPosition;
        lastStrength=command.commandData.slopeFeedbackParam.startStrength;
        lastFourth=command.commandData.slopeFeedbackParam.endStrength;
    } else if(command.mode==SCE_PAD_TRIGGER_EFFECT_MODE_MULTIPLE_POSITION_FEEDBACK){
        for(int i=0;i<10;++i)
            lastPoints[i]=command.commandData.multiplePositionFeedbackParam.strength[i];
    } else if(command.mode==SCE_PAD_TRIGGER_EFFECT_MODE_MULTIPLE_POSITION_VIBRATION){
        lastStart=command.commandData.multiplePositionVibrationParam.frequency;
        for(int i=0;i<10;++i)
            lastPoints[i]=command.commandData.multiplePositionVibrationParam.amplitude[i];
    }
    if (command.mode == SCE_PAD_TRIGGER_EFFECT_MODE_OFF) ++offCalls;
    return environmentInt(L"KHARVOX_FAKE_PSVR2_SET_RESULT", 0);
}
