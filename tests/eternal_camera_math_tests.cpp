#include "../src/EternalCameraMath.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace argent::camera;
void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(){try{
 Basis identity{1,0,0,0,1,0,0,0,1},out{};XrQuaternionf neutral{0,0,0,1};float s=std::sqrt(.5f);
 check(rotateBasis(identity,neutral,neutral,out)&&out==identity,"Neutral pose changed camera");
 check(rotateBasis(identity,neutral,{0,s,0,s},out)&&std::abs(out[0])<1e-5&&std::abs(out[1]-1)<1e-5,"XR left yaw did not turn forward toward engine left");
 check(rotateBasis(identity,neutral,{s,0,0,s},out)&&std::abs(out[2]-1)<1e-5,"XR pitch up did not turn forward toward engine up");
 check(rotateBasis(identity,neutral,{0,0,s,s},out)&&std::abs(out[5]+1)<1e-5,"XR roll conversion incorrect");
 Basis body{0,1,0,-1,0,0,0,0,1};check(rotateBasis(body,neutral,{0,s,0,s},out)&&std::abs(out[0]+1)<1e-5,"Body rotation lost");
 check(rotateBasis(body,{0,s,0,s},{0,s,0,s},out)&&validBasis(out)&&std::abs(out[1]-1)<1e-5,"Recenter did not remove head offset");
 auto before=out;auto invalid=identity;invalid[0]=2;
 check(!rotateBasis(invalid,neutral,neutral,out)&&out==before,"Non-orthonormal game basis accepted");
 check(!rotateBasis(identity,neutral,{0,0,0,0},out)&&out==before,"Invalid tracked quaternion accepted");
 invalid=identity;invalid[0]=std::numeric_limits<float>::quiet_NaN();check(!validBasis(invalid),"NaN accepted");
 float origin[3]={10,20,30};std::array<float,3> moved{};
 XrPosef ref{neutral,{0,1.7f,0}},head=ref;head.position={.2f,1.4f,-.1f};
 check(translatePosition(origin,identity,ref,head,1,moved)&&std::abs(moved[0]-10.1f)<1e-5f&&std::abs(moved[1]-19.8f)<1e-5f&&std::abs(moved[2]-29.7f)<1e-5f,"Room translation axes/scale incorrect");
 check(translatePosition(origin,body,ref,head,1,moved)&&std::abs(moved[0]-10.2f)<1e-5f&&std::abs(moved[1]-20.1f)<1e-5f,"Body yaw not applied to room movement");
 auto tilted=identity;check(rotateBasis(identity,neutral,{std::sin(.3f),0,0,std::cos(.3f)},tilted),"Pitch fixture invalid");
 check(translatePosition(origin,tilted,ref,head,1,moved)&&std::abs(moved[2]-29.7f)<1e-5f,"Looking up changed physical height");
 ref.orientation={0,s,0,s};head=ref;head.position.x=-.1f;
 check(translatePosition(origin,identity,ref,head,1,moved)&&std::abs(moved[0]-10.1f)<1e-5f&&std::abs(moved[1]-20)<1e-5f,"Translation recenter yaw incorrect");
 check(translatePosition(origin,identity,head,head,1,moved)&&moved==std::array<float,3>{10,20,30},"Translation recenter moved world");
 auto held=moved;head.position.x=std::numeric_limits<float>::quiet_NaN();check(!translatePosition(origin,identity,ref,head,1,moved)&&moved==held,"Invalid translation changed output");
 // Recover tracked grips from native world placements, including locomotion
 // between weapon and camera updates, recenter yaw, pitch and 180-degree turns.
 for(float yaw:{0.f,.4f,1.5707963f,3.14159265f})for(float scale:{.5f,1.f,39.37f}){
  XrPosef zero{{0,std::sin(.2f),0,std::cos(.2f)},{.1f,1.7f,.2f}};
  XrPosef h{{std::sin(.15f),0,0,std::cos(.15f)},{.2f,1.6f,.3f}};
  XrPosef grip{{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},{-.25f,1.2f,-.4f}};
  Basis cb{},gb{};std::array<float,3> eye{},gp{};
  check(rotateBasis(body,zero.orientation,h.orientation,cb)&&rotateBasis(body,zero.orientation,grip.orientation,gb),"Basis fixture failed");
  check(translatePosition(origin,body,zero,h,scale,eye)&&translatePosition(origin,body,zero,grip,scale,gp),"Position fixture failed");
  XrPosef recovered{};check(worldPoseInTracking(gp,gb,eye,cb,h,scale,recovered),"World hand conversion failed");
  check(std::abs(recovered.position.x-grip.position.x)<1e-4f&&std::abs(recovered.position.y-grip.position.y)<1e-4f&&std::abs(recovered.position.z-grip.position.z)<1e-4f,"Stationary hand shifted");
  const auto a=recovered.orientation,b=grip.orientation;
  check(std::abs(std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)-1)<1e-4f,"World grip rotation changed");
  gp[0]+=.1f*scale;
  check(worldPoseInTracking(gp,gb,eye,cb,h,scale,recovered),"Moving hand conversion failed");
  const auto delta=std::hypot(recovered.position.x-grip.position.x,recovered.position.y-grip.position.y,recovered.position.z-grip.position.z);
  check(std::abs(delta-.1f)<1e-4f,"Locomotion offset was discarded or scaled twice");
 }
 std::cout<<"Eternal camera and rendered world-hand conversion pass\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
