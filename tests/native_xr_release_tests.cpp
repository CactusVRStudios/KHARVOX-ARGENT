#include "../src/openxr/NativeXrReleasePolicy.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <initializer_list>
#include <stdexcept>
#include <vector>
using namespace kharvox;
int main(){
 for(auto runtime:{OpenXRRuntimeKind::VirtualDesktop,OpenXRRuntimeKind::SteamVR,
     OpenXRRuntimeKind::VDXR4Steam,OpenXRRuntimeKind::MetaOculus,OpenXRRuntimeKind::Unknown}){
  for(unsigned bits=0;bits<32;++bits)
   assert(sfsEarlyXrRelease(bits&1,runtime,bits&2,bits&4,bits&8,bits&16)
       ==(runtime==OpenXRRuntimeKind::VirtualDesktop&&bits==31));
 }
 for(bool early:{false,true}){
  // Simulated runtime observes whether the CPU has already drained the GPU.
  // Application buffers cannot be reused until the scheduler returns.
  std::vector<int> events;bool completed=false,published=false,returned=false;
  finishSfsXrSubmission(early,[&]{events.push_back(1);completed=true;},[&]{
   assert(completed==!early);events.push_back(2);published=true;
  });
  returned=true;assert(completed&&published&&returned);
  assert(events==(early?std::vector<int>{2,1}:std::vector<int>{1,2}));
  // A failed completion can never return to resource reuse, even if OpenXR
  // already owns the images. The caller must drain/retain them on failure.
  completed=published=returned=false;
  try{finishSfsXrSubmission(early,[]{throw std::runtime_error("fence failed");},[&]{published=true;});returned=true;}catch(const std::runtime_error&){}
  assert(!returned&&!completed&&published==early);
  // An XR failure propagates to the existing cleanup path, which must know
  // whether a deferred fence still needs retirement.
  completed=returned=false;
  try{finishSfsXrSubmission(early,[&]{completed=true;},[]{throw std::runtime_error("XR failed");});returned=true;}catch(const std::runtime_error&){}
  assert(!returned&&completed==!early);
 }
 // Runtime selection is independent of the six per-frame safety gates.
 // In particular VDXR is VirtualDesktop, not the Steam-backed VDXR4Steam.
 for(auto runtime:{OpenXRRuntimeKind::VirtualDesktop,OpenXRRuntimeKind::SteamVR,
     OpenXRRuntimeKind::VDXR4Steam,OpenXRRuntimeKind::MetaOculus,OpenXRRuntimeKind::Unknown}){
  const bool supported=runtime==OpenXRRuntimeKind::VirtualDesktop||runtime==OpenXRRuntimeKind::SteamVR||runtime==OpenXRRuntimeKind::VDXR4Steam;
  const bool steam=runtime==OpenXRRuntimeKind::SteamVR||runtime==OpenXRRuntimeKind::VDXR4Steam;
  for(unsigned bits=0;bits<64;++bits){
   const bool requested=bits&1,pair=bits&2,projection=bits&4,fence=bits&8,synchronized=bits&16,readback=bits&32;
   assert(nativeEarlyXrRelease(requested,runtime,pair,projection,fence,synchronized,readback)==(supported&&bits==31));
   // Steam preserves its earlier always-fenced behavior. VDXR only replaces
   // queueIdle on eligible test frames; opting out/readback/AER keep baseline.
   const bool expectedFence=steam||(runtime==OpenXRRuntimeKind::VirtualDesktop&&requested&&pair&&projection&&synchronized&&!readback);
   assert(nativeXrCopyFenceRequested(runtime,requested,pair,projection,synchronized,readback)==expectedFence);
  }
 }
 for(bool early:{false,true}){
  NativeXrCopyLifetime lifetime(early);
  assert(!lifetime.canReleaseImages()&&!lifetime.canRetireResources());
  // Even a spurious completion cannot authorize an unsubmitted image.
  lifetime.completed(true);
  assert(!lifetime.canReleaseImages()&&!lifetime.canRetireResources());
  lifetime.submitted(false);
  lifetime.completed(true);
  assert(!lifetime.canReleaseImages()&&!lifetime.canRetireResources());
  lifetime.submitted(true);
  assert(lifetime.canReleaseImages()==early);
  assert(!lifetime.canRetireResources());
  // Failed completion must retain CBs and framebuffers even when the runtime
  // has already accepted the released image.
  lifetime.completed(false);
  assert(!lifetime.canRetireResources());
  lifetime.completed(true);
  assert(lifetime.canReleaseImages()&&lifetime.canRetireResources());
 }
}
