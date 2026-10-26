#pragma once
#include "transport.hpp"
#include "dlssg.hpp"

#include "pacing.hpp"
namespace argent::fg {
class Generator {
 Microsoft::WRL::ComPtr<ID3D12Device> device;
 std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2> colors;
 std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,6> outputs;
 unsigned generatedCount=1;
 unsigned maximumCount=1;
 std::array<FeaturePtr,2> features;

 Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries;
 Microsoft::WRL::ComPtr<ID3D12Resource> timestamps;
 Identity previous{};
 uint32_t width{},height{},rw{},rh{};
 DXGI_FORMAT format{};
 bool ready{},queryPending{};
 uint64_t frequency{};
 Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
 Microsoft::WRL::ComPtr<ID3D12PipelineState> pack,composite;
 Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptors,rtv;
 unsigned stride{},queryGenerated{};
 bool pipelines(DXGI_FORMAT target);
 bool allocate(ID3D12GraphicsCommandList*,uint32_t,uint32_t,uint32_t,uint32_t,DXGI_FORMAT);
public:
 double gpuMs{};unsigned gpuGenerated{};
 bool initialize(ID3D12Device*,ID3D12CommandQueue*,const wchar_t* directory);
 // Called only after the previous consumer fence completed.
 bool record(ID3D12GraphicsCommandList*,ID3D12Resource* stereo,const std::array<InputPtr,2>&,const Identity&);
 unsigned count()const{return generatedCount;}
 ID3D12Resource* output(unsigned eye,unsigned frame=0)const{return outputs[frame*2+eye].Get();}
 void compositeTo(ID3D12GraphicsCommandList*,ID3D12Resource*,DXGI_FORMAT targetFormat,unsigned frame=0);
 void compositeEyeTo(ID3D12GraphicsCommandList*,ID3D12Resource*,DXGI_FORMAT targetFormat,unsigned eye);
 void pollTiming();
 void reset(){previous={};}
 void destroyIdle();
};
}
