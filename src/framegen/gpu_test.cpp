// Standalone GPU validation. Readback is confined to this test executable.
#include "generator.hpp"
#include "community.hpp"
#include "options.hpp"
#include <nvsdk_ngx_helpers_dlssg_d3d.h>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <thread>
using Microsoft::WRL::ComPtr;
namespace argent {void log(const std::string& s){puts(s.c_str());}}
void check(bool ok,const char* s){if(!ok){fprintf(stderr,"FAILED %s\n",s);exit(1);}}
void transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,0,a,b};l->ResourceBarrier(1,&x);}
int wmain(int argc,wchar_t** argv){
 if(argc<2||argc>3){puts("Usage: ArgentFramegenGpuTest <framegen-directory> [--half-input]");return 2;}
 const bool half=argc==3&&std::wstring(argv[2])==L"--half-input";
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 ComPtr<IDXGIFactory6> factory;check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"factory");
 ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> d;
 for(unsigned i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if((desc.VendorId==0x10de)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))))break;adapter.Reset();}
 check(bool(d),"NVIDIA device");ComPtr<ID3D12InfoQueue> messages;d.As(&messages);
 ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
 D3D12_COMMAND_QUEUE_DESC q{};check(SUCCEEDED(d->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))),"queue");
 check(SUCCEEDED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))),"allocator");
 check(SUCCEEDED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))),"list");list->Close();
 d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);uint64_t value=0;
 auto execute=[&]{check(SUCCEEDED(list->Close()),"close");ID3D12CommandList* ls[]={list.Get()};queue->ExecuteCommandLists(1,ls);queue->Signal(fence.Get(),++value);fence->SetEventOnCompletion(value,event);check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"GPU fence");check(SUCCEEDED(d->GetDeviceRemovedReason()),"device removal");};
 auto make=[&](unsigned w,unsigned h,DXGI_FORMAT f,D3D12_RESOURCE_STATES state){ComPtr<ID3D12Resource> p;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=w;r.Height=h;r.DepthOrArraySize=r.MipLevels=1;r.SampleDesc.Count=1;r.Format=f;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;check(SUCCEEDED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&p))),"texture");return p;};
 constexpr unsigned w=640,h=480;const unsigned rw=half?w/2:w,rh=half?h/2:h;auto stereo=make(2*w,h,DXGI_FORMAT_B8G8R8A8_UNORM,D3D12_RESOURCE_STATE_COMMON);auto target=make(2*w,h,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,D3D12_RESOURCE_STATE_RENDER_TARGET);
 std::array<argent::fg::InputPtr,2> inputs;for(auto& p:inputs){p=std::make_shared<argent::fg::Input>();p->depth.dx=make(rw,rh,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_COMMON);p->motion.dx=make(rw,rh,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_STATE_COMMON);p->capture.depth.width=rw;p->capture.depth.height=rh;
 auto& c=p->capture.camera;for(auto* matrix:{&c.viewToClip,&c.clipToView,&c.lens,&c.clipToPrev,&c.prevToClip})for(int i=0;i<4;++i)(*matrix)[i*5]=1;
 c.motionScale={1,1};c.up={0,1,0};c.right={1,0,0};c.forward={0,0,1};c.nearPlane=.1f;c.farPlane=100;c.fov=1.57f;c.aspect=float(w)/h;c.cameraMotion=1;c.invalidMotion=3.402823466e38f;}
 ComPtr<ID3D12DescriptorHeap> rtv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=5;d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv));auto base=rtv->GetCPUDescriptorHandleForHeapStart();unsigned step=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 auto handle=[&](unsigned i){auto x=base;x.ptr+=i*step;return x;};d->CreateRenderTargetView(stereo.Get(),nullptr,handle(0));for(unsigned e=0;e<2;++e){d->CreateRenderTargetView(inputs[e]->depth.dx.Get(),nullptr,handle(1+2*e));d->CreateRenderTargetView(inputs[e]->motion.dx.Get(),nullptr,handle(2+2*e));}

 SetEnvironmentVariableW(L"ARGENT_FG_DIRECTORY",std::filesystem::absolute(argv[1]).c_str());
 SetEnvironmentVariableA("ARGENT_FG_COMMUNITY","1");
 argent::fg::Generator generator;auto directory=std::filesystem::path(argv[1])/L"rtx40/dlssg_sm86/runtime";
 check(generator.initialize(d.Get(),queue.Get(),directory.c_str()),"NGX capability");
 unsigned generated=0;constexpr unsigned frameCount=8;
 for(unsigned frame=0;frame<frameCount;++frame){
 allocator->Reset();list->Reset(allocator.Get(),nullptr);
 transition(list.Get(),stereo.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
 for(unsigned e=0;e<2;++e){D3D12_RECT rect{LONG(e*w),0,LONG((e+1)*w),LONG(h)};float color[4]={e?.1f:.7f,e?.7f:.1f,.2f,1};list->ClearRenderTargetView(handle(0),color,1,&rect);
 D3D12_RECT moving{LONG(e*w+100+(frame%30)*8),120,LONG(e*w+180+(frame%30)*8),360};float bright[4]={.95f,.95f,.95f,1};list->ClearRenderTargetView(handle(0),bright,1,&moving);
 auto& p=*inputs[e];float depth[4]={.5f,0,0,0},motion[4]{};
 for(unsigned k=0;k<2;++k){auto resource=k?p.motion.dx.Get():p.depth.dx.Get();transition(list.Get(),resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ClearRenderTargetView(handle(1+2*e+k),k?motion:depth,0,nullptr);
 if(k){D3D12_RECT mr{LONG((100+(frame%30)*8)*rw/w),LONG(120*rh/h),LONG((180+(frame%30)*8)*rw/w),LONG(360*rh/h)};float mv[4]={-8.f/w,0,0,0};list->ClearRenderTargetView(handle(1+2*e+k),mv,1,&mr);}
 transition(list.Get(),resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);}}
 transition(list.Get(),stereo.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);

 argent::fg::Identity id{frame+1,w,h,true,true,false};
 bool made=generator.record(list.Get(),stereo.Get(),inputs,id);
 if(made){++generated;generator.compositeTo(list.Get(),target.Get(),DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);}
 execute();generator.pollTiming();printf("Frame %u generated %d GPU %.3fms\n",frame,made,generator.gpuMs);
 check(made==(frame!=0),"only adjacent complete pairs are generated");
 }
 check(generated==frameCount-1,"two-eye history and generation");
 // Read back only the small synthetic fixture, never live gameplay.
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};uint64_t bytes{};auto td=target->GetDesc();d->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&bytes);
 ComPtr<ID3D12Resource> readback;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback));
 allocator->Reset();list->Reset(allocator.Get(),nullptr);transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION dest{},src{};dest.pResource=readback.Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dest.PlacedFootprint=footprint;src.pResource=target.Get();list->CopyTextureRegion(&dest,0,0,0,&src,nullptr);execute();
 unsigned char* pixels{};D3D12_RANGE range{0,size_t(bytes)};check(SUCCEEDED(readback->Map(0,&range,reinterpret_cast<void**>(&pixels))),"readback map");auto left=pixels+(h/2)*footprint.Footprint.RowPitch+(w-20)*4;auto right=left+w*4;printf("BGRA L %u %u %u %u R %u %u %u %u\n",left[0],left[1],left[2],left[3],right[0],right[1],right[2],right[3]);check(left[2]>left[1]+60&&right[1]>right[2]+60,"distinct generated eyes survive BGRA/sRGB composite");
 // A moving white object must remain between the two real-frame positions.
 for(unsigned e=0;e<2;++e){double sum=0;unsigned count=0;for(unsigned y=180;y<220;++y)for(unsigned x=50;x<w-50;++x){auto p=pixels+y*footprint.Footprint.RowPitch+(e*w+x)*4;if(p[0]>230&&p[1]>230&&p[2]>230){sum+=x;++count;}}
 check(count>100,"interpolated moving object");const auto center=sum/count;printf("Eye %u generated object centroid %.3f\n",e,center);check(center>185&&center<198,"interpolation remains between adjacent endpoints");}
 readback->Unmap(0,nullptr);
 generator.destroyIdle();
 unsigned errors=0;if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T size{};messages->GetMessage(i,nullptr,&size);std::vector<unsigned char> buffer(size);auto m=reinterpret_cast<D3D12_MESSAGE*>(buffer.data());messages->GetMessage(i,m,&size);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"D3D12: %s\n",m->pDescription);++errors;}}
 check(!errors,"D3D12 validation errors");CloseHandle(event);puts("PASS: independent generator eyes, warmup, GPU fences, color conversion and composite. No XR delivery claimed.");return 0;
}
