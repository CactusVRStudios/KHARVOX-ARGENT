#include "../src/DesktopWindow.h"
#include <stdexcept>
#include <iostream>
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){try{
    for(int height:{720,1080,1440,2160}){
        auto on=argent::desktopMirrorExtent(true,height),off=argent::desktopMirrorExtent(false,height);
        require(on.height==height&&on.width==height*16/9,"Mirror resolution mapping");
        require(off.width==1280&&off.height==720,"Disabled must use 720p");
    }
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ArgentDesktopSizeFixture";
    require(RegisterClassW(&wc)!=0,"RegisterClass");
    auto window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,100,120,1904,993,nullptr,nullptr,wc.hInstance,nullptr);
    require(window!=nullptr,"CreateWindow");
    RECT before{},after{},client{};GetWindowRect(window,&before);
    require(argent::requestDesktopClientSize(window),"Resize request");
    require(GetClientRect(window,&client)&&client.right==1280&&client.bottom==720,"Client is not 720p");
    GetWindowRect(window,&after);require(before.left==after.left&&before.top==after.top,"Window moved");
    require(!IsWindowVisible(window),"Resize unexpectedly showed window");
    require(argent::requestDesktopClientSize(window),"Repeated request");
    require(!argent::requestDesktopClientSize(nullptr),"Accepted null window");
    require(!argent::requestDesktopClientSize(window,0,0),"Accepted empty client");
    DestroyWindow(window);std::cout<<"PASS: owned window has independent 1280x720 client\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
