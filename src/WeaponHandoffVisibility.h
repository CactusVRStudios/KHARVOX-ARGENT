#pragma once
namespace argent::player {
// Authored cameras (Glory Kill etc.) own the native rig. Drone and skipped
// Monkey Bar paths remain suppressed. Gameplay never falls back to flat poses.
inline bool hideFirstPersonRoot(bool vrRequested,bool nativeAnimation,bool vrPoseReady,bool armsRoot,bool suppressAuthoredRig=false){
 (void)vrRequested;
 if(suppressAuthoredRig)return true;
 if(nativeAnimation)return false;
 return armsRoot||!vrPoseReady;
}
}
