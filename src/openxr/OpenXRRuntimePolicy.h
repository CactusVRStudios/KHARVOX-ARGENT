#pragma once

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace kharvox {

enum class OpenXRRuntimeKind {
    VirtualDesktop,
    MetaOculus,
    SteamVR,
    VDXR4Steam,
    Unknown
};

enum class OpenXRVulkanPath {
    None,
    VulkanEnable1Direct,
    VulkanEnable2RuntimeManaged,
    VulkanEnable2VirtualDesktopBridge
};

inline std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

inline std::string activeOpenXRRuntimeManifest() {
    const DWORD environmentSize = GetEnvironmentVariableW(
        L"XR_RUNTIME_JSON", nullptr, 0);
    if (environmentSize > 1) {
        std::wstring value(environmentSize, L'\0');
        const DWORD copied = GetEnvironmentVariableW(
            L"XR_RUNTIME_JSON", value.data(), environmentSize);
        if (copied > 0 && copied < environmentSize) {
            value.resize(copied);
            return wideToUtf8(value);
        }
    }

    HKEY key{};
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0,
            KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};

    DWORD type{};
    DWORD bytes{};
    const LONG sizeResult = RegQueryValueExW(
        key, L"ActiveRuntime", nullptr, &type, nullptr, &bytes);
    if (sizeResult != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ)
        || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return {};
    }

    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    const LONG valueResult = RegQueryValueExW(
        key, L"ActiveRuntime", nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (valueResult != ERROR_SUCCESS) return {};

    std::wstring manifest(value.data());
    if (type == REG_EXPAND_SZ) {
        const DWORD expandedSize = ExpandEnvironmentStringsW(
            manifest.c_str(), nullptr, 0);
        if (expandedSize > 1) {
            std::wstring expanded(expandedSize, L'\0');
            if (ExpandEnvironmentStringsW(
                    manifest.c_str(), expanded.data(), expandedSize) > 0) {
                expanded.resize(expandedSize - 1);
                manifest = std::move(expanded);
            }
        }
    }
    return wideToUtf8(manifest);
}

inline OpenXRRuntimeKind classifyOpenXRRuntime(std::string identity) {
    std::transform(identity.begin(), identity.end(), identity.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (identity.find("virtualdesktop") != std::string::npos
        || identity.find("virtual desktop") != std::string::npos)
        return OpenXRRuntimeKind::VirtualDesktop;
    if (identity.find("oculus") != std::string::npos
        || identity.find("meta_openxr") != std::string::npos
        || identity.find("meta-openxr") != std::string::npos)
        return OpenXRRuntimeKind::MetaOculus;
    if (identity.find("vdxr4steam") != std::string::npos)
        return OpenXRRuntimeKind::VDXR4Steam;
    if (identity.find("steamxr") != std::string::npos
        || identity.find("steamvr") != std::string::npos)
        return OpenXRRuntimeKind::SteamVR;
    return OpenXRRuntimeKind::Unknown;
}

inline bool isSteamBackedOpenXRRuntime(OpenXRRuntimeKind kind) {
    return kind == OpenXRRuntimeKind::SteamVR
        || kind == OpenXRRuntimeKind::VDXR4Steam;
}

inline bool shouldPassthroughSteamRuntimeAuxiliary(
    OpenXRRuntimeKind runtime, bool nestedCreate,
    const std::string& applicationName) {
    // The explicit interop name is sufficient proof even if a future
    // SteamVR-backed runtime uses an unrecognized manifest name. This prevents
    // the runtime's Vulkan create from recursively initializing KHARVOX while
    // the outer OpenXR create still owns the state mutex.
    return applicationName == "steamvr_vrclient_interop"
        || (isSteamBackedOpenXRRuntime(runtime) && nestedCreate);
}

inline const char* openXRRuntimeKindName(OpenXRRuntimeKind kind) {
    switch (kind) {
        case OpenXRRuntimeKind::VirtualDesktop: return "Virtual Desktop";
        case OpenXRRuntimeKind::MetaOculus: return "Meta/Oculus";
        case OpenXRRuntimeKind::SteamVR: return "SteamVR (untested)";
        case OpenXRRuntimeKind::VDXR4Steam: return "VDXR4Steam (SteamVR-backed)";
        default: return "Unknown";
    }
}

inline OpenXRVulkanPath selectOpenXRVulkanPath(
    OpenXRRuntimeKind runtime, bool mediationRequested,
    bool supportsVulkanEnable1, bool supportsVulkanEnable2) {
    // Virtual Desktop retains its physically validated callback bridge.
    if (runtime == OpenXRRuntimeKind::VirtualDesktop
        && mediationRequested && supportsVulkanEnable2)
        return OpenXRVulkanPath::VulkanEnable2VirtualDesktopBridge;

    // Prefer the standards-based runtime-managed path when SteamVR exposes
    // XR_KHR_vulkan_enable2. The runtime-owned nested Vulkan instance is passed
    // through without KHARVOX recursion. Keep enable1 as a compatibility
    // fallback when enable2 is unavailable.
    if (isSteamBackedOpenXRRuntime(runtime))
        return supportsVulkanEnable2
            ? OpenXRVulkanPath::VulkanEnable2RuntimeManaged
            : supportsVulkanEnable1
                ? OpenXRVulkanPath::VulkanEnable1Direct
                : OpenXRVulkanPath::None;

    // Meta requires its runtime-managed enable2 device creation. Unknown
    // runtimes retain the standards-based enable2 preference and may fall back
    // to enable1 when enable2 is unavailable.
    if (runtime != OpenXRRuntimeKind::VirtualDesktop && supportsVulkanEnable2)
        return OpenXRVulkanPath::VulkanEnable2RuntimeManaged;
    if (supportsVulkanEnable1)
        return OpenXRVulkanPath::VulkanEnable1Direct;
    return OpenXRVulkanPath::None;
}

inline const char* openXRVulkanPathName(OpenXRVulkanPath path) {
    switch (path) {
        case OpenXRVulkanPath::VulkanEnable1Direct:
            return "XR_KHR_vulkan_enable direct Vulkan";
        case OpenXRVulkanPath::VulkanEnable2RuntimeManaged:
            return "XR_KHR_vulkan_enable2 runtime-managed Vulkan";
        case OpenXRVulkanPath::VulkanEnable2VirtualDesktopBridge:
            return "XR_KHR_vulkan_enable2 Virtual Desktop bridge";
        default:
            return "no safe compatible Vulkan path";
    }
}

inline bool useCenteredProjectionFov(OpenXRRuntimeKind runtime) {
    // Virtual Desktop's proven AER path renders a centred symmetric frustum.
    // Its submitted Projection FOV must describe that same frustum; mixing an
    // asymmetric runtime FOV with the centred source causes binocular disparity.
    return runtime == OpenXRRuntimeKind::VirtualDesktop;
}

inline float centeredProjectionHalfAngle(float negativeAngle,
                                         float positiveAngle) {
    // Preserve a centred render/submit frustum while covering the larger side
    // of an asymmetric headset FOV. Averaging both sides leaves an uncovered
    // strip on the wider side (observed at the lower VDXR lens edge).
    return std::max(std::abs(negativeAngle), std::abs(positiveAngle));
}

inline float immersiveProjectionHalfAngle(OpenXRRuntimeKind runtime,
    bool nativeStereo, float leftNegative, float leftPositive,
    float rightNegative, float rightPositive) {
    if(runtime == OpenXRRuntimeKind::VirtualDesktop && !nativeStereo)
        return 0.5f * std::min(leftPositive-leftNegative,
                               rightPositive-rightNegative);
    // Native renders and submits this same symmetric frustum. It must enclose
    // both runtime eyes: an average cuts off the wider lower/outer lens edge.
    return std::max(centeredProjectionHalfAngle(leftNegative,leftPositive),
                    centeredProjectionHalfAngle(rightNegative,rightPositive));
}

constexpr std::uint64_t automaticDoomFocusStartupFrames = 120;

inline bool shouldAutomaticallyFocusDoom(std::uint64_t submittedLayerFrames) {
    return submittedLayerFrames < automaticDoomFocusStartupFrames;
}

constexpr std::uint64_t earlySwapchainRecoveryMaxSubmittedFrames = 8;

inline bool shouldArmEarlySwapchainCopyRecovery(
    bool compatibleReplacement, std::uint32_t retiredStablePresents,
    std::uint64_t submittedLayerFrames) {
    // DOOM can replace an otherwise identical desktop swapchain immediately
    // after KHARVOX has submitted its first XR image. Reusing that replacement
    // as a transfer source on its very first Present has been observed to race
    // the Vulkan driver and return VK_ERROR_DEVICE_LOST. Restrict recovery to
    // the startup window so ordinary level/runtime swapchain changes retain
    // their existing zero-gap behaviour.
    return compatibleReplacement && retiredStablePresents > 0
        && submittedLayerFrames > 0
        && submittedLayerFrames <= earlySwapchainRecoveryMaxSubmittedFrames;
}

inline bool useSteamLinkSameFrameMonoFallback(
    bool steamMetaCompatibilityMode, bool projectionGameplayActive,
    bool nvidiaAfwRequested, bool nvidiaAfwInitializationAttempted,
    bool nvidiaAfwActive) {
    // AER is a real alternating-eye stereo renderer. Never collapse it to the
    // old same-frame diagnostic path merely because SteamVR reports its Meta
    // compatibility runtime. Mono is retained only as a safe fallback after an
    // explicitly requested AFW path has actually failed to initialize.
    return steamMetaCompatibilityMode
        && projectionGameplayActive
        && nvidiaAfwRequested
        && nvidiaAfwInitializationAttempted
        && !nvidiaAfwActive;
}

inline bool useSteamXrCoherentAerPair(
    OpenXRRuntimeKind runtime, bool projectionGameplayActive,
    bool alternatingStereoActive, bool nativePackedStereo,
    bool nvidiaAfwRequested, bool pipelineWarmupActive,
    bool immersiveCinematicActive = false) {
    // SteamVR, direct Virtual Desktop and Meta use a shared source pose and publish
    // completed eye pairs during gameplay. Animated cinematics need this on
    // every runtime because head reprojection cannot recover scene animation.
    return (runtime == OpenXRRuntimeKind::SteamVR
            || runtime == OpenXRRuntimeKind::VirtualDesktop
            || runtime == OpenXRRuntimeKind::MetaOculus
            || immersiveCinematicActive)
        && projectionGameplayActive
        && alternatingStereoActive
        && !nativePackedStereo
        && !nvidiaAfwRequested
        && !pipelineWarmupActive;
}

inline bool useCoherentAerPairPresentationContext(
    bool projectionGameplayActive, bool immersiveCinematicActive,
    bool quadMode, bool centeredQuadTransitionPending) {
    // A direct-SteamXR immersive cinematic is still an AER Projection. It
    // needs the same completed-pair contract as gameplay; dropping that
    // contract at Glory Kill entry exposes two different animation times.
    return !quadMode && !centeredQuadTransitionPending
        && (projectionGameplayActive || immersiveCinematicActive);
}

inline double coherentAerFullPairHz(std::int64_t predictedDisplayPeriod) {
    // One complete AER pair needs two runtime frames. No headset frequency is
    // assumed: 90 Hz becomes 45 pairs/s, 120 Hz becomes 60 and 144 Hz becomes
    // 72, while xrEndFrame and compositor reprojection retain the native rate.
    return predictedDisplayPeriod > 0
        ? 1.0e9 / (2.0 * static_cast<double>(predictedDisplayPeriod))
        : 0.0;
}

inline bool selectQuadPresentation(
    bool completeFrameQuadRequested, bool steamQuadOnly,
    bool packedStereoFrame, bool cutscene, bool immersiveCinematic,
    bool worldActive, bool gameplayPresentationInterrupted,
    bool fullscreenMenu, bool pauseMenu, bool deathMenu,
    bool tutorialProjectionExclusion) {
    // Native full-screen interfaces own the complete composed image and must
    // take precedence even when the gameplay camera still reports a packed
    // stereo frame underneath the interface.
    if (completeFrameQuadRequested || steamQuadOnly
        || fullscreenMenu || pauseMenu || deathMenu)
        return true;

    // Tutorial overlays and videos are part of the playable Projection view,
    // not automatic full-frame interfaces. Suppress every automatic Quad cause
    // (cutscene, missing world camera, or interrupted gameplay capture) while
    // native Tutorial activity is observed. Explicit diagnostics and unrelated
    // native full-screen menus above retain their own higher-priority policy.
    if (tutorialProjectionExclusion) return false;

    if (packedStereoFrame) return false;
    return (cutscene && !immersiveCinematic)
        || !worldActive
        || gameplayPresentationInterrupted;
}

inline bool shouldArmImmersiveCamera(bool requested, bool quadMode,
                                     bool centeredQuadTransitionPending) {
    // A centered full-frame UI must receive the authored camera, including
    // when the engine keeps rendering through its cinematic caller.
    return requested && !quadMode && !centeredQuadTransitionPending;
}

inline bool shouldKeepCinematicImmersive(
    bool immersiveMode, bool otherCinematicsInQuad,
    bool cutscene, bool worldActive,
    bool fullscreenMenu, bool deathMenu,
    bool firstPersonAssistActive, bool tutorialCinematicActive) {
    return immersiveMode && cutscene && worldActive
        && !fullscreenMenu && !deathMenu
        && (!otherCinematicsInQuad || firstPersonAssistActive
            || tutorialCinematicActive);
}

inline bool shouldRouteNativeParticipantToComfortQuad(
    bool immersiveMode, bool nativeParticipantActive,
    bool tutorialProjectionExclusion,
    bool interactiveWeaponSequence) {
    // Some scripted sequences (including the VEGA upgrade path) keep DOOM's
    // ordinary gameplay-camera caller and are visible only through the native
    // adaptive participant. In Comfort/non-Immersive mode they must still use
    // the Cinewindow; otherwise the mono authored camera is submitted as a VR
    // Projection until the participant ends.
    return !immersiveMode && nativeParticipantActive
        && !tutorialProjectionExclusion && !interactiveWeaponSequence;
}

inline bool shouldKeepNativeParticipantInComfortProjection(
    bool immersiveMode, bool nativeParticipantActive,
    bool tutorialProjectionExclusion,
    bool playerWeaponControlActive,
    bool weaponTrackingActive,
    bool knownWeaponActive) {
    // A weapon model is not sufficient evidence: VEGA and other passive
    // sequences render scripted hands too. Require DOOM's live view/button
    // permissions, a valid VR weapon pose and a classified active weapon.
    return !immersiveMode && nativeParticipantActive
        && !tutorialProjectionExclusion
        && playerWeaponControlActive
        && weaponTrackingActive
        && knownWeaponActive;
}

inline float fullFrameQuadDistanceMeters(bool) {
    // Every full-frame Cinewindow uses the Main Menu geometry. Keeping one
    // distance avoids a visible depth jump between the menu, Praetor/Argent
    // upgrade screens, cinematics and End-of-Level presentation.
    return 2.0f;
}

inline float fullFrameQuadWidthMeters(bool) {
    return 2.0f;
}

inline float fullFrameQuadHeightMeters(float width, int pixelWidth, int pixelHeight) {
    // Match the submitted subimage, including any aspect-preserving crop.
    return width * (pixelWidth > 0 && pixelHeight > 0
        ? static_cast<float>(pixelHeight) / static_cast<float>(pixelWidth)
        : 9.0f / 16.0f);
}

inline int fullFrameQuadCroppedHeight(int width, int height) {
    // Keep the full horizontal image and remove excess above/below its centre.
    if(width<=0||height<=0)return height;
    return std::min(height,std::max(1,int(int64_t(width)*9/16)));
}

inline bool shouldSuppressStereoForTimedTransition(
    std::uint64_t now, std::uint64_t guardUntil) {
    return guardUntil && now <= guardUntil;
}

constexpr unsigned centeredQuadPipelineSettleFrames = 3;

inline bool shouldBlackoutCenteredQuadTransition(
    bool transitionPending, bool quadPresented) {
    return transitionPending && quadPresented;
}

inline bool shouldBeginCenteredQuadTransition(
    bool quadRequested, bool quadAlreadyPresented, bool transitionPending) {
    return quadRequested && !quadAlreadyPresented && !transitionPending;
}

inline bool shouldCancelCenteredQuadTransition(
    bool quadRequested, bool transitionPending) {
    return !quadRequested && transitionPending;
}

inline bool shouldSuppressStereoForCenteredQuadTransition(
    bool transitionPending) {
    return transitionPending;
}

inline unsigned advanceCenteredQuadTransition(unsigned framesRemaining) {
    return framesRemaining > 0 ? framesRemaining - 1 : 0;
}

} // namespace kharvox
