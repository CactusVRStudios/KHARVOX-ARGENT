#pragma once
#include <windows.h>
namespace argent {
struct DesktopExtent { int width,height; };
inline DesktopExtent desktopMirrorExtent(bool enabled,int height){
 if(!enabled)return {1280,720};
 switch(height){case 720:return {1280,720};case 1440:return {2560,1440};case 2160:return {3840,2160};default:return {1920,1080};}
}
inline DesktopExtent configuredDesktopExtent(){
 wchar_t enabled[8]{},height[16]{};
 const bool on=GetEnvironmentVariableW(L"ARGENT_DESKTOP_MIRROR",enabled,8)==1&&enabled[0]==L'1';
 GetEnvironmentVariableW(L"ARGENT_MIRROR_HEIGHT",height,16);
 return desktopMirrorExtent(on,lstrcmpW(height,L"720")==0?720:lstrcmpW(height,L"1440")==0?1440:lstrcmpW(height,L"2160")==0?2160:1080);
}
}
