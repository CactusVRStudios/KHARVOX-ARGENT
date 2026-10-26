#pragma once
#include "bridge.hpp"
#include <d3d12.h>
#include <memory>
#include <string>
namespace argent::fg {
inline constexpr char ProjectId[]="577e0d57-2d38-4ff1-b391-992597ba488a";
struct Capability {bool available{};uint32_t result{},frames{};std::string reason;};
Capability initialize(ID3D12Device* device,const wchar_t* runtimeDirectory);
struct Feature;
struct FeatureDeleter {void operator()(Feature*)const;};
using FeaturePtr=std::unique_ptr<Feature,FeatureDeleter>;
FeaturePtr create(ID3D12GraphicsCommandList*,uint32_t width,uint32_t height,uint32_t renderWidth,uint32_t renderHeight,DXGI_FORMAT color,std::string& reason);
struct Evaluation {
 ID3D12Resource *color{},*depth{},*motion{},*output{};
 Camera camera{};
 bool reset{};
 unsigned count=1,index=1;
};
bool evaluate(Feature&,ID3D12GraphicsCommandList*,const Evaluation&,std::string& reason);
}
