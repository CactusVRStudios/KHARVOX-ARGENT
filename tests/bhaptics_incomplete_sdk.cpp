#include <cstdint>

extern "C" __declspec(dllexport) std::uint8_t __cdecl registryAndInit(
    const char*, const char*, const char*) {
    return 1;
}
