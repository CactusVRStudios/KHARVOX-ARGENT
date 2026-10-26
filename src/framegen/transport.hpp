#pragma once
#include "bridge.hpp"
#include "policy.hpp"
#include "../QuadRuntime.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
namespace argent::fg {
struct SharedImage {VkImage image{};VkDeviceMemory memory{};Microsoft::WRL::ComPtr<ID3D12Resource> dx;bool initialized{};};
struct Input {struct {Camera camera;struct {unsigned width{},height{};} depth;} capture;SharedImage depth,motion;};
using InputPtr=std::shared_ptr<Input>;
}
