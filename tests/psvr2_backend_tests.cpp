#include <Windows.h>

#include <filesystem>
#include <iostream>

#include "../src/psvr2/Psvr2ToolkitBackend.h"

namespace {
int failures{};
void require(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
        ++failures;
    }
}

void setEnvironment(const wchar_t* name, const wchar_t* value) {
    SetEnvironmentVariableW(name, value);
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace kharvox::psvr2;
    if (argc != 4) return 2;
    const std::filesystem::path completeDirectory = argv[1];
    const std::filesystem::path incompleteDirectory = argv[2];
    const std::filesystem::path missingDirectory = argv[3];

    {
        Psvr2ToolkitBackend backend;
        require(backend.loadFromDirectory(missingDirectory)
            == BackendLoadResult::LoaderMissing, "loader missing");
    }

    setEnvironment(L"KHARVOX_FAKE_PSVR2_MODULE", L"0");
    {
        Psvr2ToolkitBackend backend;
        require(backend.loadFromDirectory(completeDirectory)
            == BackendLoadResult::CapiUnavailable, "loader returns no module");
    }
    setEnvironment(L"KHARVOX_FAKE_PSVR2_MODULE", L"1");

    {
        Psvr2ToolkitBackend backend;
        require(backend.loadFromDirectory(incompleteDirectory)
            == BackendLoadResult::CapiExportMissing, "required export missing");
    }

    HMODULE fake = LoadLibraryW((completeDirectory
        / L"psvr2_toolkit_capi_loader.dll").c_str());
    if (!fake) return 3;
    const auto reset = reinterpret_cast<void(*)()>(
        GetProcAddress(fake, "fake_psvr2_reset"));
    const auto get = reinterpret_cast<int(*)(int)>(
        GetProcAddress(fake, "fake_psvr2_get"));
    if (!reset || !get) return 4;

    setEnvironment(L"KHARVOX_FAKE_PSVR2_INIT_RESULT", L"-4");
    {
        reset();
        Psvr2ToolkitBackend backend;
        require(backend.loadFromDirectory(completeDirectory)
            == BackendLoadResult::Ready, "fake loader ready");
        require(backend.initialize() == toolkitResultInvalidParameter,
            "init fails");
        require(!backend.initialized(), "failed init not initialized");
    }

    setEnvironment(L"KHARVOX_FAKE_PSVR2_INIT_RESULT", L"-2");
    {
        Psvr2ToolkitBackend backend;
        require(backend.loadFromDirectory(completeDirectory)
            == BackendLoadResult::Ready, "no-slot loader ready");
        require(backend.initialize() == toolkitResultNoSlot, "no slot result");
    }

    setEnvironment(L"KHARVOX_FAKE_PSVR2_INIT_RESULT", L"0");
    setEnvironment(L"KHARVOX_FAKE_PSVR2_DRIVER_ACTIVE", L"0");
    {
        Psvr2ToolkitBackend backend;
        require(backend.loadFromDirectory(completeDirectory)
            == BackendLoadResult::Ready && backend.initialize() == 0,
            "driver inactive setup");
        bool callSucceeded{};
        require(!backend.driverActive(callSucceeded) && callSucceeded,
            "driver inactive");
    }

    setEnvironment(L"KHARVOX_FAKE_PSVR2_DRIVER_ACTIVE", L"1");
    setEnvironment(L"KHARVOX_FAKE_PSVR2_SET_RESULT", L"0");
    {
        reset();
        Psvr2ToolkitBackend backend;
        require(backend.loadFromDirectory(completeDirectory)
            == BackendLoadResult::Ready && backend.initialize() == 0,
            "success setup");
        const auto command = weaponCommand(false, 2, 4, 7);
        require(backend.apply(command) == toolkitResultOk,
            "weapon command success");
        require(get(4) == static_cast<int>(VRControllerType::Right)
            && get(5) == SCE_PAD_TRIGGER_EFFECT_MODE_WEAPON
            && get(6) == 2 && get(7) == 4 && get(8) == 7,
            "weapon ABI translation");
        const auto vibration = vibrationCommand(true, 2, 7, 50);
        require(backend.apply(vibration) == toolkitResultOk,
            "vibration command success");
        require(get(4) == static_cast<int>(VRControllerType::Left)
            && get(5) == SCE_PAD_TRIGGER_EFFECT_MODE_VIBRATION
            && get(6) == 2 && get(7) == 7 && get(8) == 50,
            "vibration ABI translation");

        const auto feedback = feedbackCommand(false, 4, 3);
        require(backend.apply(feedback) == toolkitResultOk,
            "feedback command success");
        require(get(4) == static_cast<int>(VRControllerType::Right)
            && get(5) == SCE_PAD_TRIGGER_EFFECT_MODE_FEEDBACK
            && get(6) == 4 && get(7) == 3,
            "feedback ABI translation");

        const auto slope = slopeFeedbackCommand(true, 2, 8, 3, 8);
        require(backend.apply(slope) == toolkitResultOk,
            "slope feedback command success");
        require(get(4) == static_cast<int>(VRControllerType::Left)
            && get(5) == SCE_PAD_TRIGGER_EFFECT_MODE_SLOPE_FEEDBACK
            && get(6) == 2 && get(7) == 8 && get(8) == 3 && get(9) == 8,
            "slope feedback ABI translation");

        const std::array<std::uint8_t, 10> feedbackPoints{
            0, 0, 3, 6, 3, 3, 6, 8, 8, 8};
        const auto multipleFeedback = multiplePositionFeedbackCommand(
            false, feedbackPoints);
        require(backend.apply(multipleFeedback) == toolkitResultOk,
            "multiple-position feedback command success");
        bool feedbackPointsMatch = get(5)
            == SCE_PAD_TRIGGER_EFFECT_MODE_MULTIPLE_POSITION_FEEDBACK;
        for (int index = 0; index < 10; ++index)
            feedbackPointsMatch = feedbackPointsMatch
                && get(10 + index) == feedbackPoints[index];
        require(feedbackPointsMatch,
            "multiple-position feedback ABI translation");

        const std::array<std::uint8_t, 10> vibrationPoints{
            0, 0, 2, 3, 4, 5, 6, 7, 8, 8};
        const auto multipleVibration = multiplePositionVibrationCommand(
            true, 45, vibrationPoints);
        require(backend.apply(multipleVibration) == toolkitResultOk,
            "multiple-position vibration command success");
        bool vibrationPointsMatch = get(4)
                == static_cast<int>(VRControllerType::Left)
            && get(5) == SCE_PAD_TRIGGER_EFFECT_MODE_MULTIPLE_POSITION_VIBRATION
            && get(6) == 45;
        for (int index = 0; index < 10; ++index)
            vibrationPointsMatch = vibrationPointsMatch
                && get(10 + index) == vibrationPoints[index];
        require(vibrationPointsMatch,
            "multiple-position vibration ABI translation");

        backend.shutdown();
        require(get(3) == 2, "off both controllers on shutdown");
        require(get(1) == 1, "deinit on shutdown");
    }

    setEnvironment(L"KHARVOX_FAKE_PSVR2_SET_RESULT", L"12345");
    {
        Psvr2ToolkitBackend backend;
        require(backend.loadFromDirectory(completeDirectory)
            == BackendLoadResult::Ready && backend.initialize() == 0,
            "unknown result setup");
        require(backend.apply(weaponCommand(true, 4, 6, 3)) == 12345,
            "unknown result preserved as error");
        require(std::string(toolkitResultName(12345))
            == "PSVR2TK_RESULT_UNKNOWN", "unknown result symbolic");
    }

    FreeLibrary(fake);
    SetEnvironmentVariableW(L"KHARVOX_FAKE_PSVR2_MODULE", nullptr);
    SetEnvironmentVariableW(L"KHARVOX_FAKE_PSVR2_INIT_RESULT", nullptr);
    SetEnvironmentVariableW(L"KHARVOX_FAKE_PSVR2_DRIVER_ACTIVE", nullptr);
    SetEnvironmentVariableW(L"KHARVOX_FAKE_PSVR2_SET_RESULT", nullptr);
    return failures == 0 ? 0 : 1;
}
