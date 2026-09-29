#include "../src/openxr/StereoProjection.h"
#include "../src/openxr/DisplayFormat.h"
#include "../src/openxr/StereoCopyBarrier.h"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 check(argent::menuAnchorSessionVisible(XR_SESSION_STATE_VISIBLE),"Visible menu incorrectly requires input focus to anchor");
 check(argent::menuAnchorSessionVisible(XR_SESSION_STATE_FOCUSED),"Focused menu cannot anchor");
 for(auto state:{XR_SESSION_STATE_IDLE,XR_SESSION_STATE_READY,XR_SESSION_STATE_SYNCHRONIZED,XR_SESSION_STATE_STOPPING})
  check(!argent::menuAnchorSessionVisible(state),"Invisible session captured menu anchor");
 for(auto initial:{VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}){
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout=initial;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  argent::finishStereoCopyBarrier(barrier,true);
  check(barrier.oldLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&barrier.newLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,"XR target must preserve copied pixels in attachment layout on first use and reuse");
  check(barrier.srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&(barrier.dstAccessMask&VK_ACCESS_COLOR_ATTACHMENT_READ_BIT),"Hand rendering must see transfer writes");
 }
 for(auto initial:{VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}){
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout=initial;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,1,1};
  argent::finishStereoCopyBarrier(barrier,false);
  check(barrier.oldLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&barrier.newLayout==initial&&barrier.subresourceRange.baseArrayLayer==1,"Game source layout or eye changed after copying");
 }
 const auto portrait=argent::widescreenQuadRect({2496,2688});
 check(portrait.offset.x==0&&portrait.offset.y==642&&portrait.extent.width==2496&&portrait.extent.height==1404,"Portrait source not centrally cropped");
 const auto wide=argent::widescreenQuadRect({1920,1080});
 check(wide.offset.x==0&&wide.offset.y==0&&wide.extent.width==1920&&wide.extent.height==1080,"Native widescreen changed");
 const auto ultra=argent::widescreenQuadRect({3440,1440});
 check(ultra.offset.x==440&&ultra.extent.width==2560&&ultra.extent.height==1440,"Ultrawide crop stretched");

 for(float width:{3.0f,3.2f}){const auto size=argent::widescreenQuadSize(width);
  check(std::abs(size.width/size.height-16.f/9.f)<.00001f,"Quad must remain widescreen with portrait eye targets");}
 check(argent::xrDisplayFormat(VK_FORMAT_B8G8R8A8_UNORM,true)==VK_FORMAT_B8G8R8A8_SRGB,"Display bytes interpreted as linear");
 check(argent::xrDisplayFormat(VK_FORMAT_R8G8B8A8_UNORM,true)==VK_FORMAT_R8G8B8A8_SRGB,"RGBA display format lost");
 check(argent::xrDisplayFormat(VK_FORMAT_B8G8R8A8_SRGB,true)==VK_FORMAT_B8G8R8A8_SRGB,"sRGB changed twice");
 check(argent::xrDisplayFormat(VK_FORMAT_R8G8B8A8_UNORM,false)==VK_FORMAT_R8G8B8A8_UNORM,"Linear fixture changed");
 argent::sfs::FramePose expected{};expected.serial=42;expected.displayTime=1000;
 argent::sfs::StereoFrame pair{};pair.generation=42;
 for(unsigned e=0;e<2;++e){auto& v=expected.views[e];v.type=XR_TYPE_VIEW;v.pose.orientation.w=1;v.pose.position.x=e?.032f:-.032f;v.fov={-.8f,.8f,.8f,-.8f};
  pair.eyes[e]={reinterpret_cast<VkImage>(uintptr_t(1)),{1024,1024},VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_GENERAL,v.pose,v.fov,e,42};}
 pair.pose=expected;
 auto valid=[&](const auto& p){return argent::validStereoPair(p,expected,{1024,1024},VK_FORMAT_R8G8B8A8_UNORM);};
 check(valid(pair),"Valid stereo rejected");
 auto wrongMode=pair;wrongMode.pose.quadView=true;check(!valid(wrongMode),"Wrong composition mode accepted");
 expected.quadView=true;expected.quadPose.orientation.w=1;expected.quadPose.position.z=-2.5f;pair.pose=expected;
 check(valid(pair),"Valid menu quad rejected");
 wrongMode=pair;wrongMode.pose.quadHeadLocked=true;check(!valid(wrongMode),"Changed quad reference space accepted");
 expected.quadHeadLocked=true;pair.pose=expected;check(valid(pair),"Head-locked startup quad rejected");
 expected.quadHeadLocked=false;pair.pose=expected;
 expected.stereoQuad=true;pair.pose=expected;check(valid(pair),"Stereo cinematic pair rejected");
 const std::array<XrSwapchain,2> chains{reinterpret_cast<XrSwapchain>(uintptr_t(10)),reinterpret_cast<XrSwapchain>(uintptr_t(20))};
 auto layers=argent::stereoQuadLayers(expected,chains,{2082,2122},XR_NULL_HANDLE,XR_NULL_HANDLE);
 check(layers[0].eyeVisibility==XR_EYE_VISIBILITY_LEFT&&layers[1].eyeVisibility==XR_EYE_VISIBILITY_RIGHT,"Stereo cinema routed to both eyes");
 for(int e=0;e<2;++e)check(layers[e].subImage.swapchain==chains[e]&&layers[e].subImage.imageArrayIndex==0&&std::memcmp(&layers[e].pose,&expected.quadPose,sizeof(XrPosef))==0,"Cinematic eye/plane mismatch");
 wrongMode=pair;wrongMode.pose.stereoQuad=false;check(!valid(wrongMode),"Mono/stereo cinematic history mixed");
 expected.stereoQuad=false;pair.pose=expected;
 layers=argent::stereoQuadLayers(expected,chains,{2082,2122},XR_NULL_HANDLE,XR_NULL_HANDLE);
 check(layers[0].eyeVisibility==XR_EYE_VISIBILITY_BOTH,"2D menus acquired eye-specific layers");
 wrongMode=pair;wrongMode.pose.quadPose.position.z=-1;check(!valid(wrongMode),"Changed quad anchor accepted");
 expected.quadView=false;pair.pose=expected;
 auto bad=pair;bad.eyes[1].serial=41;check(!valid(bad),"Mixed eye frames accepted");
 bad=pair;bad.pose.displayTime=999;check(!valid(bad),"Old prediction accepted");
 bad=pair;bad.eyes[1].layer=0;check(!valid(bad),"Duplicated mono image accepted");
 bad=pair;bad.eyes[0].pose.position.x+=.1f;check(!valid(bad),"Pose changed after rendering accepted");
 bad=pair;bad.eyes[0].extent.width=512;check(!valid(bad),"Resized stale eye accepted");
 bad=pair;bad.eyes[1].layout=VK_IMAGE_LAYOUT_UNDEFINED;check(!valid(bad),"Undefined pixels accepted");
 auto views=argent::stereoProjectionViews(pair,XR_NULL_HANDLE,{1024,1024});
 check(views[0].subImage.imageArrayIndex==0&&views[1].subImage.imageArrayIndex==1,"XR eye layers exchanged");
 check(views[0].pose.position.x<0&&views[1].pose.position.x>0,"XR eye poses exchanged");
 const float h=std::sqrt(.5f);XrPosef predicted{{0,0,0,1},{0,0,0}},rendered{{0,h,0,h},{1,2,3}};
 argent::rebaseProjectionViews(views,predicted,rendered);
 check(std::abs(views[0].pose.position.x-1)<1e-5f&&std::abs(views[0].pose.position.z-3.032f)<1e-5f&&std::abs(views[1].pose.position.z-2.968f)<1e-5f,"Rendered pose did not preserve local IPD");
 check(std::abs(views[0].pose.orientation.y-h)<1e-5f&&views[0].subImage.imageArrayIndex==0&&views[0].fov.angleLeft==-.8f,"Pose correction changed projection or eye assignment");
 std::cout<<"Stereo frame identity, rendered pose and XR array routing verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
