#pragma once

#include <cstdint>

// The XInput hook calls only KharvoxBhapticsSubmitRumble. That function is
// strictly atomic and never performs allocation, logging, file, pipe, or
// network I/O. All IPC is owned by a separate worker.
void KharvoxBhapticsIpcStart();
void KharvoxBhapticsSubmitRumble(std::uint16_t lowMotor, std::uint16_t highMotor);
void KharvoxBhapticsIpcRequestStop();
