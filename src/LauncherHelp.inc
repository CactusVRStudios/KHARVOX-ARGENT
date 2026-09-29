// Native, independently scrollable help pages. Coordinates below are content coordinates.
struct HelpRow { const wchar_t* title; const wchar_t* text; };
const HelpRow bindingRows[]{
    {L"Valve Index",L"Same layout as Quest: left A = X (Flame Belch), left B = Y (weapon mod). Right A/B and both sticks/triggers keep the listed functions. Press the left trackpad for Pause. Grip uses squeeze force: engage above 70%, release at 55% or below."},
    {L"Left stick",L"Move. Click briefly: Mission Info. Hold click: Dossier."},
    {L"Left X / Y",L"X: Flame Belch. Y: switch weapon mod."},
    {L"Left trigger",L"Fire selected equipment / grenade."},
    {L"Left grip",L"Near the weapon: two-hand support. Away from the weapon: next equipment item. Does not open the weapon wheel."},
    {L"Left menu button",L"Pause / menu."},
    {L"Right stick",L"Left/right: turn. Up: Crucible. Hold down: weapon wheel; choose with the left stick, release right stick down to confirm."},
    {L"Right stick click",L"Use / melee / Glory Kill when a valid target is available."},
    {L"Right A / B",L"A: Dash. B: Jump."},
    {L"Right trigger",L"Primary fire."},
    {L"Right grip",L"In front: alternate fire / weapon mod (including Meathook). Behind the shoulder: chainsaw."},
    {L"Left-handed mode",L"Enable Left Handed Mode under Movement to show the layout selector. Button Swap puts weapon trigger/grip and Use/melee stick-click on the left, equipment trigger/grip and Mission Info/Dossier stick-click on the right. Face buttons and stick directions stay on their original sides. Button and Stick Swap also exchanges face-button pairs and stick directions: move/select weapons with the right stick; turn, Crucible (up) and weapon wheel (down) with the left. The physical Menu button stays Pause. The image shows right-handed mode."},
    {L"Menus and tutorials",L"A: confirm. B: back / close. Follow the prompt; left stick click still opens the Dossier and right stick up selects Crucible in tutorials."},
    {L"Revenant drone",L"Automatic campaign mode: each trigger fires its corresponding cannon. B: jump / hold for jetpack. A: afterburner."}
};
const HelpRow instructionRows[]{
    {L"Getting started",L"Connect your headset and controllers and start your OpenXR runtime. Select the DOOM Eternal executable, then press PLAY. Steam and Microsoft Store installations are supported.\nKeep the game window focused for input. The native VR intro runs before the game. After its first start, Disable VR Intro appears under Rendering."},
    {L"Limitations",L"KHARVOX: ARGENT is a private project developed with limited resources. Compatibility and performance can vary between hardware configurations.\nAMD graphics cards currently have known rendering issues. Compatibility may improve with future updates. Some effects, particularly water, can also produce visual artifacts in VR.\nThe mod has been tested with VDXR, SteamVR\u2019s OpenXR runtime and Meta\u2019s OpenXR runtime. Other runtimes have not been validated and are unsupported."},
    {L"Reccomended Headsets and Runtimes:",L"Quest: VDXR or MetaXR\nIndex: SteamVR\nPlease do not use SteamVR with Meta headsets. It\u2019s known to cause issues!"},
    {L"Example Configuration",L"Ryzen 9 5900X, 32GB DDR4, 4080S\n70% Renderscale, FSR on, DLSS off, HighDetails"},
    {L"Rendering",L"The game world is rendered in stereoscopic VR. Supported menus and tutorials automatically appear on a virtual screen.\nEnable 3D Cinematics for stereoscopic presentation of supported cinematic sequences. Restart the game after changing rendering options."},
    {L"RenderScale and FSR Upscaling",L"100% is the native reference pixel count. Lower values reduce rendering work.\nEnable FSR Upscaling to upscale reduced eye images and sharpen the result. FSR is disabled at 100% or higher. Start at 100%, then reduce gradually if needed.\nThe SteamVR path uses 100% and does not apply the launcher\u2019s FSR resolution reduction. Adjust resolution through SteamVR instead."},
    {L"DLSS and anti-aliasing",L"Select DLSS in the game\u2019s graphics settings if your graphics card supports it. Stereo DLSS uses separate histories for each eye but remains experimental.\nDOOM Eternal normally applies temporal anti-aliasing, which can make the image look blurry in VR. ARGENT blocks native TAA in stereo rendering.\nDisable FSR Upscaling in the launcher when using DLSS. FSR forces the game\u2019s anti-aliasing off, so choose either FSR or DLSS. Compare image clarity, stability and performance in your headset."},
    {L"Desktop Mirror",L"Enable Desktop Mirror (Right Eye) under Rendering to display the right-eye view on your monitor. This is useful for spectators, screenshots and recording. It is disabled by default.\nThe mirror preserves the eye image\u2019s aspect ratio. Black bars may appear because a VR eye image does not usually match a 16:9 desktop window."},
    {L"Mirror resolution",L"Choose the desktop output resolution from the dropdown:\n- 720p: 1280 \u00d7 720\n- 1080p: 1920 \u00d7 1080\n- 2K / 1440p: 2560 \u00d7 1440\n- 4K: 3840 \u00d7 2160\nThe default selection is 1080p. When Desktop Mirror is disabled, the desktop window uses 720p.\nMirror resolution controls the desktop output size, not the headset\u2019s rendering resolution. Higher output resolutions may increase GPU overhead and will not add detail beyond the rendered eye image. Restart the game after changing these settings."},
    {L"Movement and controls",L"Index controller can access the Pause Menu with Touchpad click.\nSee Controller Binding for the full mapping.\nUnder Movement, choose smooth or snap turning, adjust turning speed or snap angle, and select head-directed or off-hand-directed movement.\nEnable Left Handed Mode to reveal the layout selector:\n- Button Swap: Moves weapon trigger/grip and Use/melee stick-click to the left controller. Equipment trigger/grip and Mission Info/Dossier stick-click move to the right. Face buttons and stick directions stay on their original sides.\n- Button and Stick Swap: Also swaps the face-button pairs and stick directions. Move and select weapons with the right stick; turn, select Crucible and open the weapon wheel with the left stick.\nThe physical Menu button remains Pause."},
    {L"Two-hand support and physical melee",L"Enable Virtual Gunstock for two-hand support. Hold the off-hand grip near the weapon\u2019s support point to grab it. Release the grip to let go. A grip press away from the weapon cycles equipment.\nPhysical Glory Kill / melee uses controller speed, the Punch speed threshold and the selected punch hand. A valid target and the game\u2019s normal activation conditions are still required."},
    {L"PSVR2 Toolkit",L"PSVR2 Adaptive Triggers is experimental and requires PlayStation VR2 hardware, a working PSVR2 SteamVR setup and PSVR2 Toolkit installed separately. PSVR2Toolkit version 1.0 or higher is required.\nIt provides weapon-specific adaptive-trigger resistance on the weapon hand. Normal controller rumble works independently.\nMissing or inactive Toolkit software does not block game startup, but adaptive-trigger effects will be unavailable."},
    {L"bHaptics",L"Use compatible bHaptics gear and bHaptics Player for Windows. Pair and test your devices in the Player before starting the game, then enable bHaptics under VR Options.\nMissing hardware, an unavailable Player or a local bridge error does not block game startup, but suit feedback will be unavailable. Normal controller rumble works independently."},
    {L"Logging and troubleshooting",L"Extended Logging is disabled by default. Enable it under Rendering before pressing PLAY when investigating a problem.\nAdditional logging can affect performance. Disable it again when you have finished collecting logs."},
    {L"About",L"KHARVOX:ARGENT is an independent community project and is not affiliated with, endorsed by, or sponsored by the publisher or developers of supported games.\nAll trademarks and product names are property of their respective owners. A legally acquired installation of each supported game is required!"}
};
struct HelpState { int scroll{}, height{}, wheel{}; bool bindings{}; };
HelpState helpStates[2]{{0,0,0,true},{}};
Gdiplus::Image* controllerBindingImage{};

int paintHelp(HDC dc, int width, HelpState& state, bool draw) {
    const int left=14, right=width-18; int y=14;
    SetBkMode(dc,TRANSPARENT);
    auto paragraph=[&](const wchar_t* text,int x,int w,HFONT font,COLORREF color){
        auto old=SelectObject(dc,font);RECT measure{0,0,w,0};
        DrawTextW(dc,text,-1,&measure,DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
        if(draw){RECT area{x,y-state.scroll,x+w,y-state.scroll+measure.bottom};SetTextColor(dc,color);DrawTextW(dc,text,-1,&area,DT_WORDBREAK|DT_NOPREFIX);}
        SelectObject(dc,old);return measure.bottom;
    };
    if(state.bindings){
        if(controllerBindingImage&&controllerBindingImage->GetLastStatus()==Gdiplus::Ok){
            const int h=int((right-left)*double(controllerBindingImage->GetHeight())/controllerBindingImage->GetWidth());
            if(draw){Gdiplus::Graphics g(dc);g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);g.DrawImage(controllerBindingImage,left,y-state.scroll,right-left,h);}
            y+=h+18;
        }else y+=paragraph(L"Controller diagram unavailable. See the complete bindings below.",left,right-left,uiFont,Muted)+12;
        for(const auto& row:bindingRows){
            int a=paragraph(row.title,left,175,uiFont,Accent);
            int b=paragraph(row.text,left+190,right-left-190,uiFont,Text);
            y+=std::max(a,b)+12;
            if(draw){RECT line{left,y-state.scroll,right,y-state.scroll+1};FillRect(dc,&line,fieldBrush);}
            y+=10;
        }
    }else for(const auto& row:instructionRows){
        y+=paragraph(row.title,left,right-left,titleFont,Accent)+8;
        y+=paragraph(row.text,left,right-left,uiFont,Text)+22;
    }
    return y+10;
}
void updateHelpScroll(HWND hwnd,HelpState& state,int position){
    RECT r{};GetClientRect(hwnd,&r);
    HDC dc=GetDC(hwnd);state.height=paintHelp(dc,r.right,state,false);ReleaseDC(hwnd,dc);
    state.scroll=std::clamp(position,0,std::max(0,state.height-int(r.bottom)));
    SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,state.height-1,UINT(r.bottom),state.scroll,0};
    SetScrollInfo(hwnd,SB_VERT,&si,TRUE);InvalidateRect(hwnd,nullptr,FALSE);
}
LRESULT CALLBACK helpProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* state=reinterpret_cast<HelpState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){state=static_cast<HelpState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
    if(!state)return DefWindowProcW(hwnd,msg,wp,lp);
    RECT r{};GetClientRect(hwnd,&r);
    switch(msg){
    case WM_SIZE:updateHelpScroll(hwnd,*state,state->scroll);return 0;
    case WM_LBUTTONDOWN:SetFocus(hwnd);return 0;
    case WM_GETDLGCODE:return DLGC_WANTARROWS;
    case WM_MOUSEWHEEL:{state->wheel+=GET_WHEEL_DELTA_WPARAM(wp);int steps=state->wheel/WHEEL_DELTA;state->wheel%=WHEEL_DELTA;updateHelpScroll(hwnd,*state,state->scroll-steps*72);return 0;}
    case WM_VSCROLL:{int p=state->scroll;switch(LOWORD(wp)){
        case SB_LINEUP:p-=24;break;case SB_LINEDOWN:p+=24;break;
        case SB_PAGEUP:p-=r.bottom;break;case SB_PAGEDOWN:p+=r.bottom;break;
        case SB_TOP:p=0;break;case SB_BOTTOM:p=state->height;break;
        case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(hwnd,SB_VERT,&si);p=si.nTrackPos;break;}}
        updateHelpScroll(hwnd,*state,p);return 0;}
    case WM_KEYDOWN:{int p=state->scroll;switch(wp){case VK_UP:p-=24;break;case VK_DOWN:p+=24;break;case VK_PRIOR:p-=r.bottom;break;case VK_NEXT:p+=r.bottom;break;case VK_HOME:p=0;break;case VK_END:p=state->height;break;default:return DefWindowProcW(hwnd,msg,wp,lp);}updateHelpScroll(hwnd,*state,p);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(buffer,bitmap);FillRect(buffer,&r,backgroundBrush);paintHelp(buffer,r.right,*state,true);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(hwnd,&ps);return 0;}
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

