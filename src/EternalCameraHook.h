#pragma once
#include "openxr/ControllerInput.h"
#include "hands/HandHudMask.h"
#include <openxr/openxr.h>
#include <cstdint>
#include <cstddef>
namespace argent::camera {
bool install() noexcept;
bool storeExecutable() noexcept;
void updateHeadsetFov(const XrPosef&,const XrView*,uint32_t) noexcept;
bool installRenderExtent(uint32_t width,uint32_t height) noexcept;
void stop() noexcept;
void update(XrQuaternionf head,bool world,bool recenter) noexcept;
void updatePose(XrPosef head,bool world,bool recenter,bool positionTracked) noexcept;
float unitsPerMeter() noexcept;
void beginRender(uint64_t serial) noexcept;
void observeRenderedCamera(const void* common,size_t bytes) noexcept;
bool renderedHead(uint64_t serial,XrPosef& pose) noexcept;
void publishLaser(const float* origin,const float* axis,const char* profile) noexcept;
bool renderedLaser(uint64_t serial,const char* profile,XrPosef& pose) noexcept;
bool hudCamera(float* origin,float* axis,uint64_t* frame=nullptr) noexcept;
bool wallClimbView(float* axis) noexcept;
bool swimmingView(float* axis) noexcept;
bool monkeyBarPose(float* origin,float* axis,float* headOffset=nullptr) noexcept;
bool hudProjection(float& tanX,float& tanY) noexcept;
void publishHudPanels(uint64_t frame,int role,const kharvox::hands::HandHudPanels& panels);
kharvox::hands::HandHudPanels renderedHudPanels(uint64_t serial);
kharvox::hands::HandHudPanels renderedHudPlaceholder(uint64_t serial);
bool hudOffhand(float* origin,float* axis,bool& leftMode) noexcept;
void publishPhysics(uintptr_t owner,XrVector3f position) noexcept;
bool revenantAim(uintptr_t actor,float* axis) noexcept;
uint64_t weaponContext() noexcept;
bool controllerPlacement(XrPosef hand,float* origin,float* axis,const input::Snapshot* rendered=nullptr) noexcept;
struct Stats {uint64_t calls{},applied{},rejected{};bool installed{};};
Stats stats() noexcept;
}

namespace argent::camera { bool renderedHands(uint64_t serial,input::Snapshot& hands,float& depthA,float& depthB) noexcept; }
