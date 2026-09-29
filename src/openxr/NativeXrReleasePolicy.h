#pragma once
#include "OpenXRRuntimePolicy.h"
namespace kharvox {
inline bool nativeXrReleaseRuntime(OpenXRRuntimeKind runtime){
 return isSteamBackedOpenXRRuntime(runtime)||runtime==OpenXRRuntimeKind::VirtualDesktop;
}
// ARGENT's owned stereo source always submits its copy on the exact queue
// bound to OpenXR. The same ordering applies to projection and menu quad
// layers; CPU resource reuse still waits for the private fence afterwards.
// Keep other runtimes on their existing schedule for this VDXR experiment.
inline bool sfsEarlyXrRelease(bool requested,OpenXRRuntimeKind runtime,bool ownedSource,
 bool sessionQueue,bool copyFence,bool queueSynchronized){
 return requested&&runtime==OpenXRRuntimeKind::VirtualDesktop&&ownedSource&&sessionQueue&&copyFence&&queueSynchronized;
}

// Called after a successful copy submission. publish() releases the XR images
// and ends the frame. Neither schedule can return to the next frame before
// wait() succeeds. Exceptions propagate to the caller's GPU retirement path.
template<class Wait,class Publish>
void finishSfsXrSubmission(bool early,Wait wait,Publish publish){
 if(!early)wait();
 publish();
 if(early)wait();
}
inline bool nativeEarlyXrRelease(bool requested,OpenXRRuntimeKind runtime,bool nativePair,bool projection,
 bool copyFence,bool queueSynchronized,bool readback){
 return requested&&nativeXrReleaseRuntime(runtime)&&nativePair&&projection&&copyFence&&queueSynchronized&&!readback;
}
inline bool nativeXrCopyFenceRequested(OpenXRRuntimeKind runtime,bool requested,bool nativePair,
 bool projection,bool queueSynchronized,bool readback){
 // Steam already used a private copy fence before this experiment. VDXR's
 // baseline/readback/loading paths retain queue-idle completion.
 return isSteamBackedOpenXRRuntime(runtime)
     ||nativeEarlyXrRelease(requested,runtime,nativePair,projection,true,queueSynchronized,readback);
}
// Image ownership may pass to the runtime after queue submission. CPU readers
// and application resources still require a verified completion before reuse.
class NativeXrCopyLifetime {
 bool early_{},submitted_{},completed_{};
public:
 explicit NativeXrCopyLifetime(bool early):early_(early){}
 void submitted(bool success){submitted_=success;}
 void completed(bool success){completed_=submitted_&&success;}
 bool canReleaseImages()const{return submitted_&&(early_||completed_);}
 bool canRetireResources()const{return submitted_&&completed_;}
};
}
