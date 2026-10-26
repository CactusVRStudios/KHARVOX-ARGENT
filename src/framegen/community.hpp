#pragma once
#include <string>
#include <cstdint>
namespace argent::fg {
// Call at Vulkan device creation, before the game primes NGX capabilities.
bool loadCommunityRuntime(uint32_t vendor,const std::wstring& adapter,std::string& reason);
}
