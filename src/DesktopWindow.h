#pragma once
#include <windows.h>
#include "DesktopMirrorResolution.h"

namespace argent {
// Called once at native surface creation, never in the frame/recording loop.
// SetWindowPos takes the outer size; preserve the requested client dimensions.
inline bool requestDesktopClientSize(HWND window, int width=1280, int height=720){
    DWORD owner{};
    if(!window||!GetWindowThreadProcessId(window,&owner)||owner!=GetCurrentProcessId()||width<=0||height<=0)return false;
    const auto style=DWORD(GetWindowLongPtrW(window,GWL_STYLE));
    if(style&WS_CHILD||IsIconic(window)||IsZoomed(window))return false;
    RECT client{};
    if(!GetClientRect(window,&client))return false;
    if(client.right-client.left==width&&client.bottom-client.top==height)return true;
    RECT outer{0,0,width,height};
    if(!AdjustWindowRectExForDpi(&outer,style,GetMenu(window)!=nullptr,
        DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),GetDpiForWindow(window)))return false;
    // Queue to the owning UI thread when necessary; never wait on another
    // thread's window procedure from the Vulkan initialization path.
    return SetWindowPos(window,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,
        SWP_ASYNCWINDOWPOS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
}
}
