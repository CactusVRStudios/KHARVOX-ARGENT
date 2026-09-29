#pragma once
#include <atomic>
namespace argent::dlss {
// Requires the camera hook's full supported-executable hash validation first.
bool install() noexcept;
inline std::atomic<bool> stereoFailed{};
inline bool failed() noexcept {return stereoFailed.load(std::memory_order_relaxed);}
}
