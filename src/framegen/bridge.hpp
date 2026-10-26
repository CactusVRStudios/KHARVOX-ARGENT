#pragma once
#include <array>
#include <cstdint>
namespace argent::fg {
struct Camera {
 std::array<float,16> viewToClip{},clipToView{},lens{},clipToPrev{},prevToClip{};
 std::array<float,2> jitter{},motionScale{},pinhole{};
 std::array<float,3> position{},up{},right{},forward{};
 float nearPlane{},farPlane{},fov{},aspect{},invalidMotion{};
 uint8_t inverted{},cameraMotion{},motion3d{},reset{},orthographic{},dilated{},jittered{},padding[5]{};
};
static_assert(sizeof(Camera)==424);
}
