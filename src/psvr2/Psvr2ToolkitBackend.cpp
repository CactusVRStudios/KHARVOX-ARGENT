#include "Psvr2ToolkitBackend.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace kharvox::psvr2 {

namespace {

template<typename Function>
Function loadExport(HMODULE module, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

template<typename Function>
bool safeGetDriverActive(Function function, bool& succeeded) {
#if defined(_MSC_VER)
    __try {
        const bool active = function();
        succeeded = true;
        return active;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        succeeded = false;
        return false;
    }
#else
    succeeded = true;
    return function();
#endif
}

} // namespace

const char* backendLoadResultName(BackendLoadResult result) {
    switch (result) {
    case BackendLoadResult::Ready: return "ready";
    case BackendLoadResult::LoaderMissing: return "loader-missing";
    case BackendLoadResult::LoaderLoadFailed: return "loader-load-failed";
    case BackendLoadResult::LoaderExportMissing: return "loader-export-missing";
    case BackendLoadResult::CapiUnavailable: return "capi-unavailable";
    case BackendLoadResult::CapiExportMissing: return "capi-export-missing";
    }
    return "unknown";
}

const char* toolkitResultName(int result) {
    switch (result) {
    case toolkitResultOk: return "PSVR2TK_RESULT_OK";
    case toolkitResultDriverInactive: return "PSVR2TK_RESULT_DRIVER_INACTIVE";
    case toolkitResultNoSlot: return "PSVR2TK_RESULT_NO_SLOT";
    case toolkitResultTimeout: return "PSVR2TK_RESULT_TIMEOUT";
    case toolkitResultInvalidParameter: return "PSVR2TK_RESULT_INVALID_PARAMETER";
    default: return "PSVR2TK_RESULT_UNKNOWN";
    }
}

Psvr2ToolkitBackend::~Psvr2ToolkitBackend() {
    shutdown();
    unload();
}

BackendLoadResult Psvr2ToolkitBackend::loadFromDirectory(
    const std::filesystem::path& directory) {
    unload();
    const auto path = directory / L"psvr2_toolkit_capi_loader.dll";
    if (!std::filesystem::is_regular_file(path))
        return BackendLoadResult::LoaderMissing;

    loaderModule_ = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!loaderModule_)
        return BackendLoadResult::LoaderLoadFailed;
    getModuleHandle_ = loadExport<GetModuleHandleFn>(loaderModule_,
        "psvr2_toolkit_loader_get_module_handle");
    if (!getModuleHandle_) {
        unload();
        return BackendLoadResult::LoaderExportMissing;
    }

    capiModule_ = static_cast<HMODULE>(getModuleHandle_());
    if (!capiModule_) {
        unload();
        return BackendLoadResult::CapiUnavailable;
    }
    init_ = loadExport<InitFn>(capiModule_, "psvr2_toolkit_init");
    deinit_ = loadExport<DeinitFn>(capiModule_, "psvr2_toolkit_deinit");
    getDriverActive_ = loadExport<GetDriverActiveFn>(capiModule_,
        "psvr2_toolkit_get_driver_active");
    setTriggerEffect_ = loadExport<SetTriggerEffectFn>(capiModule_,
        "psvr2_toolkit_set_trigger_effect");
    if (!init_ || !deinit_ || !getDriverActive_ || !setTriggerEffect_) {
        unload();
        return BackendLoadResult::CapiExportMissing;
    }
    return BackendLoadResult::Ready;
}

int Psvr2ToolkitBackend::initialize() {
    if (!init_) return toolkitResultInvalidParameter;
    int result = toolkitResultInvalidParameter;
#if defined(_MSC_VER)
    __try { result = init_(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result = toolkitResultInvalidParameter;
    }
#else
    result = init_();
#endif
    initialized_ = result == toolkitResultOk;
    return result;
}

bool Psvr2ToolkitBackend::driverActive(bool& callSucceeded) {
    callSucceeded = false;
    if (!initialized_ || !getDriverActive_) return false;
    return safeGetDriverActive(getDriverActive_, callSucceeded);
}

int Psvr2ToolkitBackend::applyOff(VRControllerType controller) {
    if (!initialized_ || !setTriggerEffect_)
        return toolkitResultInvalidParameter;
    ScePadTriggerEffectCommand command{};
    command.mode = SCE_PAD_TRIGGER_EFFECT_MODE_OFF;
    int result = toolkitResultInvalidParameter;
#if defined(_MSC_VER)
    __try { result = setTriggerEffect_(controller, command); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result = toolkitResultInvalidParameter;
    }
#else
    result = setTriggerEffect_(controller, command);
#endif
    return result;
}

int Psvr2ToolkitBackend::apply(const TriggerCommand& requested) {
    if (!initialized_ || !setTriggerEffect_ || !validTriggerCommand(requested))
        return toolkitResultInvalidParameter;
    ScePadTriggerEffectCommand command{};
    switch(requested.effect){
    case TriggerEffect::Weapon:
        command.mode=SCE_PAD_TRIGGER_EFFECT_MODE_WEAPON;break;
    case TriggerEffect::Vibration:
        command.mode=SCE_PAD_TRIGGER_EFFECT_MODE_VIBRATION;break;
    case TriggerEffect::Feedback:
        command.mode=SCE_PAD_TRIGGER_EFFECT_MODE_FEEDBACK;break;
    case TriggerEffect::SlopeFeedback:
        command.mode=SCE_PAD_TRIGGER_EFFECT_MODE_SLOPE_FEEDBACK;break;
    case TriggerEffect::MultiplePositionFeedback:
        command.mode=SCE_PAD_TRIGGER_EFFECT_MODE_MULTIPLE_POSITION_FEEDBACK;break;
    case TriggerEffect::MultiplePositionVibration:
        command.mode=SCE_PAD_TRIGGER_EFFECT_MODE_MULTIPLE_POSITION_VIBRATION;break;
    case TriggerEffect::Off:
        command.mode=SCE_PAD_TRIGGER_EFFECT_MODE_OFF;break;
    }
    if (requested.effect == TriggerEffect::Weapon) {
        command.commandData.weaponParam.startPosition = requested.startPosition;
        command.commandData.weaponParam.endPosition = requested.endPosition;
        command.commandData.weaponParam.strength = requested.strength;
    } else if (requested.effect == TriggerEffect::Vibration) {
        command.commandData.vibrationParam.position = requested.position;
        command.commandData.vibrationParam.amplitude = requested.amplitude;
        command.commandData.vibrationParam.frequency = requested.frequency;
    } else if (requested.effect == TriggerEffect::Feedback) {
        command.commandData.feedbackParam.position=requested.position;
        command.commandData.feedbackParam.strength=requested.strength;
    } else if (requested.effect == TriggerEffect::SlopeFeedback) {
        command.commandData.slopeFeedbackParam.startPosition=requested.startPosition;
        command.commandData.slopeFeedbackParam.endPosition=requested.endPosition;
        command.commandData.slopeFeedbackParam.startStrength=requested.startStrength;
        command.commandData.slopeFeedbackParam.endStrength=requested.endStrength;
    } else if (requested.effect == TriggerEffect::MultiplePositionFeedback) {
        std::copy(requested.controlPoints.begin(),requested.controlPoints.end(),
            std::begin(command.commandData.multiplePositionFeedbackParam.strength));
    } else if (requested.effect == TriggerEffect::MultiplePositionVibration) {
        command.commandData.multiplePositionVibrationParam.frequency=requested.frequency;
        std::copy(requested.controlPoints.begin(),requested.controlPoints.end(),
            std::begin(command.commandData.multiplePositionVibrationParam.amplitude));
    }
    const auto controller = requested.hand == TriggerHand::Left
        ? VRControllerType::Left : VRControllerType::Right;
    int result = toolkitResultInvalidParameter;
#if defined(_MSC_VER)
    __try { result = setTriggerEffect_(controller, command); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result = toolkitResultInvalidParameter;
    }
#else
    result = setTriggerEffect_(controller, command);
#endif
    return result;
}

void Psvr2ToolkitBackend::shutdown() {
    if (!initialized_) return;
    if (setTriggerEffect_) {
        applyOff(VRControllerType::Left);
        applyOff(VRControllerType::Right);
    }
#if defined(_MSC_VER)
    if (deinit_) {
        __try { deinit_(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { }
    }
#else
    if (deinit_) deinit_();
#endif
    initialized_ = false;
}

void Psvr2ToolkitBackend::unload() {
    shutdown();
    init_ = nullptr;
    deinit_ = nullptr;
    getDriverActive_ = nullptr;
    setTriggerEffect_ = nullptr;
    getModuleHandle_ = nullptr;
    if (capiModule_ && capiModule_ != loaderModule_)
        FreeLibrary(capiModule_);
    capiModule_ = nullptr;
    if (loaderModule_) FreeLibrary(loaderModule_);
    loaderModule_ = nullptr;
}

} // namespace kharvox::psvr2
