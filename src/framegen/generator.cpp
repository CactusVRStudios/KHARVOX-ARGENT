#include "generator.hpp"
#include "options.hpp"
#include "../QuadRuntime.h"
#include <filesystem>
#include <d3dcompiler.h>
namespace argent::fg {
namespace {
void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
 if(a==b)return;D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&x);
}
}
bool Generator::pipelines(DXGI_FORMAT target){
 const char* source=R"(
Texture2D<float4> source:register(t0);RWTexture2D<float4> outputImage:register(u0);
cbuffer C:register(b0){uint offsetX;uint width;uint height;uint unused;};
[numthreads(8,8,1)]void cs(uint3 t:SV_DispatchThreadID){if(t.x<width&&t.y<height)outputImage[t.xy]=source.Load(int3(t.x+offsetX,t.y,0));}
float4 vs(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}
float4 ps(float4 p:SV_Position):SV_Target {float4 c=source.Load(int3(uint(p.x)-offsetX,uint(p.y),0));float3 low=c.rgb/12.92;float3 high=pow((c.rgb+0.055)/1.055,2.4);return float4(lerp(high,low,step(c.rgb,0.04045)),c.a);}
)";
 Microsoft::WRL::ComPtr<ID3DBlob> cs,vs,ps,blob,errors;
 auto compile=[&](const char* entry,const char* model,Microsoft::WRL::ComPtr<ID3DBlob>& out){return SUCCEEDED(D3DCompile(source,strlen(source),"ARGENT FG color transfer",nullptr,nullptr,entry,model,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&out,&errors));};
 if(!compile("cs","cs_5_0",cs)||!compile("vs","vs_5_0",vs)||!compile("ps","ps_5_0",ps))return false;
 D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};
 D3D12_ROOT_PARAMETER params[3]{};for(unsigned i=0;i<2;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
 params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,4};
 D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=3;rs.pParameters=params;
 if(FAILED(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors))||FAILED(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
 D3D12_COMPUTE_PIPELINE_STATE_DESC cp{};cp.pRootSignature=root.Get();cp.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
 if(FAILED(device->CreateComputePipelineState(&cp,IID_PPV_ARGS(&pack))))return false;
 D3D12_GRAPHICS_PIPELINE_STATE_DESC gp{};gp.pRootSignature=root.Get();gp.VS={vs->GetBufferPointer(),vs->GetBufferSize()};gp.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
 gp.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
 gp.SampleMask=UINT_MAX;gp.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gp.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;gp.RasterizerState.DepthClipEnable=TRUE;
 gp.DepthStencilState.DepthEnable=FALSE;gp.DepthStencilState.StencilEnable=FALSE;gp.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 gp.NumRenderTargets=1;gp.RTVFormats[0]=target;gp.SampleDesc.Count=1;
 if(FAILED(device->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&composite))))return false;
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=9;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&descriptors))))return false;
 hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
 if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv))))return false;
 stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);return true;
}
bool Generator::initialize(ID3D12Device* d,ID3D12CommandQueue* queue,const wchar_t* directory){
 device=d;Capability caps;
 caps=argent::fg::initialize(d,directory);
 if(!caps.available){state("fallback",caps.reason.c_str());return false;}
 maximumCount=generatedCount=1;
 queue->GetTimestampFrequency(&frequency);
 D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=2;
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
 D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=16;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 if(FAILED(d->CreateQueryHeap(&q,IID_PPV_ARGS(&queries)))||FAILED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timestamps)))){state("fallback","FG timestamp allocation failed",true);return false;}
 ready=true;state("available","Waiting for complete stereo inputs",true);return true;
}
bool Generator::allocate(ID3D12GraphicsCommandList* list,uint32_t w,uint32_t h,uint32_t rwidth,uint32_t rheight,DXGI_FORMAT f){
 if(width==w&&height==h&&rw==rwidth&&rh==rheight&&format==f&&(features[0]&&features[1]))return true;
 // record() is only entered after the preceding GPU work finished.
 for(auto& feature:features)feature.reset();for(auto& r:colors)r.Reset();for(auto& r:outputs)r.Reset();previous={};width=height=0;
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;d.Format=f;d.SampleDesc.Count=1;
 std::string reason;
 for(unsigned e=0;e<2;++e){
  d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&colors[e]))))return false;
  d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  for(unsigned frame=0;frame<maximumCount;++frame)if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_SOURCE,nullptr,IID_PPV_ARGS(&outputs[frame*2+e]))))return false;
  features[e]=create(list,w,h,rwidth,rheight,f,reason);
  if(!features[e]){state("fallback",reason.c_str(),true);return false;}
 }
 width=w;height=h;rw=rwidth;rh=rheight;format=f;return true;
}
bool Generator::record(ID3D12GraphicsCommandList* list,ID3D12Resource* stereo,const std::array<InputPtr,2>& inputs,const Identity& id){
 if(!ready)return false;
 if(!id.gameplay||!id.complete||!inputs[0]||!inputs[1]){reset();state("fallback","Waiting for complete scene depth/motion inputs (pause menus excluded)",true);return false;}
 const auto desc=stereo->GetDesc();
 auto rwidth=inputs[0]->capture.depth.width,rheight=inputs[0]->capture.depth.height;
 if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM){reset();state("fallback","Unsupported FG color format",true);return false;}
 if(!root&&!pipelines(desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)){state("fallback","FG color pipeline creation failed",true);return false;}
 if(!allocate(list,id.width,id.height,rwidth,rheight,DXGI_FORMAT_R8G8B8A8_UNORM)){reset();return false;}
 const bool interpolate=consecutive(previous,id);
 list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
 barrier(list,stereo,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 auto cpu=descriptors->GetCPUDescriptorHandleForHeapStart();auto gpu=descriptors->GetGPUDescriptorHandleForHeapStart();
 D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
 device->CreateShaderResourceView(stereo,&srv,cpu);
 bool ok=true;std::string reason;
 for(unsigned e=0;e<2;++e){
  auto& p=*inputs[e];
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R8G8B8A8_UNORM;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
  auto cu=cpu;cu.ptr+=(1+e)*stride;device->CreateUnorderedAccessView(colors[e].Get(),nullptr,&uav,cu);
  for(unsigned frame=0;frame<generatedCount;++frame){auto su=cpu;su.ptr+=(3+frame*2+e)*stride;srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;device->CreateShaderResourceView(outputs[frame*2+e].Get(),&srv,su);}
  barrier(list,colors[e].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12DescriptorHeap* heaps[]={descriptors.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root.Get());list->SetPipelineState(pack.Get());
  list->SetComputeRootDescriptorTable(0,gpu);auto gu=gpu;gu.ptr+=(1+e)*stride;list->SetComputeRootDescriptorTable(1,gu);unsigned constants[]={e*width,width,height,0};list->SetComputeRoot32BitConstants(2,4,constants,0);
  list->Dispatch((width+7)/8,(height+7)/8,1);
  barrier(list,colors[e].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  barrier(list,p.depth.dx.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  barrier(list,p.motion.dx.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  // A reset evaluation primes the current real frame cheaply; NGX count=0 is unsupported.
  const unsigned evaluationCount=1;
  for(unsigned frame=0;frame<evaluationCount;++frame){
   auto output=outputs[frame*2+e].Get();
   barrier(list,output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   Evaluation eval{colors[e].Get(),p.depth.dx.Get(),p.motion.dx.Get(),output,p.capture.camera,!interpolate,evaluationCount,frame+1};
   ok=evaluate(*features[e],list,eval,reason)&&ok;
   barrier(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
  }
  barrier(list,p.depth.dx.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
  barrier(list,p.motion.dx.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
 }
 barrier(list,stereo,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
 list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);list->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,timestamps.Get(),0);queryPending=true;queryGenerated=interpolate?generatedCount:0;
 if(!ok){reset();state("fallback",reason.c_str(),true);return false;}
 const bool gap=previous.complete&&!interpolate;
 if(!interpolate)state("history building",gap?"Skipped native neighbor; adjusting producer cadence":"Priming valid stereo history",true);
 // Keep "active" through subsequent successful evaluations; recording GPU
 // work is not a new availability state. Only xrEndFrame may activate it.
 previous=id;
 return interpolate&&generatedCount;
}
void Generator::compositeTo(ID3D12GraphicsCommandList* list,ID3D12Resource* target,DXGI_FORMAT targetFormat,unsigned frame){
 if(frame>=generatedCount)return;
 D3D12_RENDER_TARGET_VIEW_DESC v{};v.Format=targetFormat;v.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
 const auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target,&v,handle);
 ID3D12DescriptorHeap* heaps[]={descriptors.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRootSignature(root.Get());list->SetPipelineState(composite.Get());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);
 for(unsigned e=0;e<2;++e){
  barrier(list,outputs[frame*2+e].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  auto gpu=descriptors->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=(3+frame*2+e)*stride;list->SetGraphicsRootDescriptorTable(0,gpu);
  unsigned constants[]={e*width,width,height,0};list->SetGraphicsRoot32BitConstants(2,4,constants,0);
  D3D12_VIEWPORT viewport{float(e*width),0,float(width),float(height),0,1};D3D12_RECT rect{LONG(e*width),0,LONG((e+1)*width),LONG(height)};
  list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);list->DrawInstanced(3,1,0,0);
  barrier(list,outputs[frame*2+e].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
 }
}
void Generator::compositeEyeTo(ID3D12GraphicsCommandList* list,ID3D12Resource* target,DXGI_FORMAT targetFormat,unsigned eye){
 if(eye>=2)return;
 D3D12_RENDER_TARGET_VIEW_DESC v{};v.Format=targetFormat;v.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
 auto handle=rtv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=eye*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 device->CreateRenderTargetView(target,&v,handle);
 ID3D12DescriptorHeap* heaps[]={descriptors.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRootSignature(root.Get());list->SetPipelineState(composite.Get());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);
 barrier(list,outputs[eye].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
 auto gpu=descriptors->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=(3+eye)*stride;list->SetGraphicsRootDescriptorTable(0,gpu);
 unsigned constants[]={0,width,height,0};list->SetGraphicsRoot32BitConstants(2,4,constants,0);
 D3D12_VIEWPORT viewport{0,0,float(width),float(height),0,1};D3D12_RECT rect{0,0,LONG(width),LONG(height)};
 list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);list->DrawInstanced(3,1,0,0);
 barrier(list,outputs[eye].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
}
void Generator::pollTiming(){
 if(!queryPending||!frequency)return;uint64_t* t{};D3D12_RANGE range{0,16};
 if(SUCCEEDED(timestamps->Map(0,&range,reinterpret_cast<void**>(&t)))){gpuMs=1000.*double(t[1]-t[0])/double(frequency);gpuGenerated=queryGenerated;D3D12_RANGE none{0,0};timestamps->Unmap(0,&none);}queryPending=false;
}
void Generator::destroyIdle(){ready=false;for(auto& f:features)f.reset();for(auto& r:colors)r.Reset();for(auto& r:outputs)r.Reset();queries.Reset();timestamps.Reset();reset();width=height=0;}
}

