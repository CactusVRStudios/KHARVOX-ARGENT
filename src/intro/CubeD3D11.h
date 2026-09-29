#pragma once
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>
#include "CubeScene.h"

namespace kharvox::intro {
inline HRESULT createEyeTarget(ID3D11Device* device, ID3D11Texture2D* texture,
    DXGI_FORMAT swapchainFormat, ID3D11RenderTargetView** target) {
    if(!device||!texture||!target)return E_INVALIDARG;


    D3D11_TEXTURE2D_DESC image{};
    texture->GetDesc(&image);
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.Format=swapchainFormat;
    if(image.ArraySize>1) {
        if(image.SampleDesc.Count>1) {
            view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
            view.Texture2DMSArray.ArraySize=1;
        } else {
            view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.ArraySize=1;
        }
    } else view.ViewDimension=image.SampleDesc.Count>1
        ?D3D11_RTV_DIMENSION_TEXTURE2DMS:D3D11_RTV_DIMENSION_TEXTURE2D;
    return device->CreateRenderTargetView(texture,&view,target);
}


class CubeRenderer {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11VertexShader> vertex;
    Ptr<ID3D11PixelShader> pixel;
    Ptr<ID3D11InputLayout> layout;
    Ptr<ID3D11Buffer> instances,constants;
    Ptr<ID3D11RasterizerState> raster;
    Ptr<ID3D11ShaderResourceView> copper;
    Ptr<ID3D11SamplerState> sampler;
    UINT capacity{},count{};
    static void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Intro GPU renderer failed");}
public:
    void shutdown(){instances.Reset();constants.Reset();layout.Reset();vertex.Reset();pixel.Reset();raster.Reset();copper.Reset();sampler.Reset();device.Reset();count=capacity=0;}
    static HRESULT finishGpu(ID3D11Device* device,ID3D11DeviceContext* context){
        if(!device||!context)return S_OK;
        Ptr<ID3D11Query> complete;D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};
        HRESULT result=device->CreateQuery(&desc,&complete);if(FAILED(result))return result;
        context->End(complete.Get());context->ClearState();context->Flush();
        const auto deadline=GetTickCount64()+2000;
        while((result=context->GetData(complete.Get(),nullptr,0,0))==S_FALSE){
            if(GetTickCount64()>=deadline)return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            Sleep(1);
        }
        return result;
    }
    void initialize(ID3D11Device* source){
        device=source;
        auto png=asset(104);if(!png.data)throw std::runtime_error("Embedded Copper_35.png missing");
        Ptr<IWICImagingFactory> imaging;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&imaging)));
        Ptr<IWICStream> stream;check(imaging->CreateStream(&stream));
        check(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<void*>(png.data)),DWORD(png.size)));
        Ptr<IWICBitmapDecoder> decoder;check(imaging->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder));
        Ptr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame));
        UINT width{},height{};check(frame->GetSize(&width,&height));
        if(!width||!height||width>4096||height>4096)throw std::runtime_error("Invalid copper palette dimensions");
        Ptr<IWICFormatConverter> converter;check(imaging->CreateFormatConverter(&converter));
        check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        std::vector<BYTE> rgba(width*height*4);check(converter->CopyPixels(nullptr,width*4,UINT(rgba.size()),rgba.data()));
        D3D11_TEXTURE2D_DESC image{};image.Width=width;image.Height=height;image.MipLevels=1;image.ArraySize=1;
        image.Format=DXGI_FORMAT_R8G8B8A8_UNORM;image.SampleDesc.Count=1;image.Usage=D3D11_USAGE_IMMUTABLE;image.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{rgba.data(),width*4,0};Ptr<ID3D11Texture2D> texture;
        check(device->CreateTexture2D(&image,&data,&texture));check(device->CreateShaderResourceView(texture.Get(),nullptr,&copper));
        D3D11_SAMPLER_DESC sample{};sample.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sample.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;sample.AddressV=D3D11_TEXTURE_ADDRESS_WRAP;sample.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sample.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sample,&sampler));
        const char* shader=R"HLSL(
Texture2D copperPalette : register(t0);
SamplerState copperSampler : register(s0);
cbuffer View : register(b0) {
    float4 inverseEye;
    float4 anchorYaw;
    float4 eyeTime;
    float4 tangents;
};
struct Input { float3 center:CENTER; float2 extent:EXTENT; float4 color:COLOR; float mode:MODE; float3 tangent:TANGENT; float3 bitangent:BITANGENT; };
struct Output { float4 position:SV_Position; float4 color:COLOR; float localY:TEXCOORD0; nointerpolation float mode:TEXCOORD1; };
float3 rotateEye(float3 p){float3 t=2*cross(inverseEye.xyz,p);return p+inverseEye.w*t+cross(inverseEye.xyz,t);}
Output vsMain(Input input,uint id:SV_VertexID){
    const float2 corners[6]={float2(-.5,.5),float2(.5,.5),float2(-.5,-.5),float2(-.5,-.5),float2(.5,.5),float2(.5,-.5)};
    float2 offset=corners[id]*input.extent;
    float3 local=input.center;
    if(input.mode>2.5&&input.mode<3.5)local+=input.tangent*offset.x+input.bitangent*offset.y;
    else if(input.mode<1.5||input.mode>3.5)local.xy+=offset;
    float sn=sin(anchorYaw.w),cs=cos(anchorYaw.w);
    float3 world=anchorYaw.xyz+float3(cs*local.x+sn*local.z,local.y,-sn*local.x+cs*local.z);
    float3 camera=rotateEye(world-eyeTime.xyz);
    if(input.mode>1.5&&input.mode<2.5)camera.xy+=offset;
    float depth=-camera.z;
    Output output;
    output.position=float4((2*camera.x-(tangents.y+tangents.x)*depth)/(tangents.y-tangents.x),
        (2*camera.y-(tangents.z+tangents.w)*depth)/(tangents.z-tangents.w),depth-.05,depth);
    output.color=input.color;output.localY=local.y;output.mode=input.mode;return output;
}
float4 psMain(Output input):SV_Target {
    if(input.mode!=1)return input.color;
    return float4(copperPalette.Sample(copperSampler,float2(.5,frac(input.localY*2.2-eyeTime.w*.28))).rgb*input.color.rgb,1);
}
)HLSL";
        Ptr<ID3DBlob> vs,ps,errors;
        auto compile=[&](const char* entry,const char* profile,ID3DBlob** result){
            HRESULT hr=D3DCompile(shader,std::strlen(shader),"Intro renderer",nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,result,&errors);
            if(FAILED(hr))throw std::runtime_error(errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Intro shader compilation failed");
        };
        compile("vsMain","vs_4_0",&vs);compile("psMain","ps_4_0",&ps);
        check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex));
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel));
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"CENTER",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_INSTANCE_DATA,1},
            {"EXTENT",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_INSTANCE_DATA,1},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D11_INPUT_PER_INSTANCE_DATA,1},
            {"MODE",0,DXGI_FORMAT_R32_FLOAT,0,36,D3D11_INPUT_PER_INSTANCE_DATA,1},
            {"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,44,D3D11_INPUT_PER_INSTANCE_DATA,1},
            {"BITANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,56,D3D11_INPUT_PER_INSTANCE_DATA,1}};
        static_assert(sizeof(Quad)==68);
        check(device->CreateInputLayout(elements,6,vs->GetBufferPointer(),vs->GetBufferSize(),&layout));
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=64;desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&desc,nullptr,&constants));
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&rs,&raster));
    }

    void prepare(ID3D11DeviceContext* context,const Scene& scene,float time,float beatAge=-1.f){
        auto quads=scene.geometry(time,beatAge);count=UINT(quads.size());
        if(count>capacity){
            instances.Reset();capacity=std::max(4096u,count*2);
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=capacity*sizeof(Quad);desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            check(device->CreateBuffer(&desc,nullptr,&instances));
        }
        if(count){D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(instances.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
            std::memcpy(mapped.pData,quads.data(),quads.size()*sizeof(Quad));context->Unmap(instances.Get(),0);}
    }
    void drawEye(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        const Scene& scene,const XrView& eye,int width,int height,float time,bool black){
        const float background[]{0,0,0,1};context->ClearRenderTargetView(target,background);
        if(black||!count)return;
        const float view[16]={-eye.pose.orientation.x,-eye.pose.orientation.y,-eye.pose.orientation.z,eye.pose.orientation.w,
            scene.anchor.x,scene.anchor.y,scene.anchor.z,scene.yaw,
            eye.pose.position.x,eye.pose.position.y,eye.pose.position.z,time,
            tanf(eye.fov.angleLeft),tanf(eye.fov.angleRight),tanf(eye.fov.angleUp),tanf(eye.fov.angleDown)};
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
        std::memcpy(mapped.pData,view,sizeof(view));context->Unmap(constants.Get(),0);
        auto cb=constants.Get();context->VSSetConstantBuffers(0,1,&cb);context->PSSetConstantBuffers(0,1,&cb);
        auto palette=copper.Get();auto sampling=sampler.Get();context->PSSetShaderResources(0,1,&palette);context->PSSetSamplers(0,1,&sampling);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
        auto vb=instances.Get();UINT stride=sizeof(Quad),offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->RSSetState(raster.Get());D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
        context->OMSetRenderTargets(1,&target,nullptr);context->OMSetBlendState(nullptr,nullptr,0xffffffff);context->OMSetDepthStencilState(nullptr,0);
        context->DrawInstanced(6,count,0,0);

        context->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
}
