#pragma once
#include <vulkan/vulkan.h>
namespace argent {
// Explicit GPU crash diagnostics only, independent of Extended Logging.
// Debug tables are needed to decode the exact runtime
// material binary; offline reconstruction differed from the crashing pipeline.
inline constexpr VkDeviceDiagnosticsConfigFlagsNV gpuDiagnosticFlags=
 VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV|
 VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV|
 VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV;
}
