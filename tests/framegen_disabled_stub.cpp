// The standalone XR probe exercises SFS presentation without an engine or NGX.
// Frame generation is covered separately by ArgentFramegenInteropTest.
#include "../src/framegen/FrameGeneration.h"
namespace argent::fg {
void attach(Device&) {}
bool ready() { return false; }
void observeNativeInputs() {}
bool generate(Device&, const sfs::StereoFrame&, uint32_t, const VkSemaphore*,
              bool& consumed, sfs::StereoFrame&) {
    consumed = false;
    return false;
}
void reset() {}
}
