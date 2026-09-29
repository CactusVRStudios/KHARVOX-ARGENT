#pragma once
#include <dxgi1_2.h>
#include "CubeD3D11.h"

namespace kharvox::intro {
inline constexpr UINT desktopWidth=1280,desktopHeight=720;
inline XrView desktopView(const std::array<XrView,2>& eyes){
    auto view=eyes[0];
    view.pose.position={(eyes[0].pose.position.x+eyes[1].pose.position.x)*.5f,
        (eyes[0].pose.position.y+eyes[1].pose.position.y)*.5f,(eyes[0].pose.position.z+eyes[1].pose.position.z)*.5f};
    const float vertical=std::tan(.65f),horizontal=vertical*float(desktopWidth)/desktopHeight;
    view.fov={-std::atan(horizontal),std::atan(horizontal),.65f,-.65f};
    return view;
}
class DesktopPreview {
    HWND window{};
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    ULONGLONG lastFrame{};
    bool wasBlack{},visible{};
    static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
        if(message==WM_CLOSE){PostQuitMessage(0);return 0;}
        if(message==WM_ERASEBKGND)return 1;
        return DefWindowProcW(window,message,w,l);
    }
    static void check(HRESULT result){if(FAILED(result))throw std::runtime_error("Intro desktop preview failed: "+std::to_string(unsigned(result)));}
public:
    ~DesktopPreview(){shutdown();}
    void shutdown(){target.Reset();swapchain.Reset();if(window){DestroyWindow(window);window=nullptr;}}
    void initialize(ID3D11Device* device,bool show=true){
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=windowProc;
        wc.lpszClassName=L"KharvoxIntroPreview";wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register intro preview window");
        constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
        RECT bounds{0,0,LONG(desktopWidth),LONG(desktopHeight)};AdjustWindowRect(&bounds,style,FALSE);
        window=CreateWindowExW(0,wc.lpszClassName,L"KHARVOX Intro",style,CW_USEDEFAULT,CW_USEDEFAULT,
            bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window)throw std::runtime_error("Cannot create intro preview window");
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        check(device->QueryInterface(IID_PPV_ARGS(&dxgi)));check(dxgi->GetAdapter(&adapter));check(adapter->GetParent(IID_PPV_ARGS(&factory)));
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=desktopWidth;desc.Height=desktopHeight;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        check(factory->CreateSwapChainForHwnd(device,window,&desc,nullptr,nullptr,&swapchain));
        check(factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER));
        Microsoft::WRL::ComPtr<ID3D11Texture2D> image;check(swapchain->GetBuffer(0,IID_PPV_ARGS(&image)));
        check(device->CreateRenderTargetView(image.Get(),nullptr,&target));
        visible=show;
    }
    static bool pump(){bool quit=false;MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)quit=true;TranslateMessage(&message);DispatchMessageW(&message);}return quit;}
    void draw(ID3D11DeviceContext* context,CubeRenderer& renderer,const Scene& scene,
        const std::array<XrView,2>& eyes,float time,bool black){
        const auto now=GetTickCount64();


        if(lastFrame&&((!IsWindowVisible(window)||IsIconic(window))||(now-lastFrame<33&&black==wasBlack)))return;
        renderer.drawEye(context,target.Get(),scene,desktopView(eyes),desktopWidth,desktopHeight,time,black);
        auto result=swapchain->Present(0,DXGI_PRESENT_DO_NOT_WAIT);
        if(result!=DXGI_ERROR_WAS_STILL_DRAWING)check(result);
        if(!lastFrame&&visible)ShowWindow(window,SW_SHOWNOACTIVATE);
        lastFrame=now;wasBlack=black;
    }
    HWND handle()const{return window;}
};
}

