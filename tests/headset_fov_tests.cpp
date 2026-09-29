#include "../src/HeadsetFov.h"
#include "../src/AnimationFovGuard.h"
#include <limits>
#include <iostream>
#include <stdexcept>
void check(bool v){if(!v)throw std::runtime_error("headset FOV mismatch");}
int main(){try{
 argent::camera::AnimationFovGuard guard;
 float ax=60,ay=45;
 check(guard.apply(ax,ay,1.f,1.f)&&std::abs(ax-90)<.001f&&std::abs(ay-90)<.001f);
 guard.restore(ax,ay);check(ax==60&&ay==45); // Animation exit / no native rewrite.
 ax=120;ay=100;check(!guard.apply(ax,ay,1,1)&&ax==120&&ay==100);
 ax=60;ay=45;check(guard.apply(ax,ay,1,1));ax=110;
 guard.restore(ax,ay);check(ax==110&&ay==45); // Preserve a newer native write per axis.
 check(!guard.apply(ax,ay,0,1)&&ax==110&&ay==45);
 check(!guard.apply(ax,ay,std::numeric_limits<float>::quiet_NaN(),1));
 ax=60;ay=45;check(guard.apply(ax,ay,2,1)&&ax>120&&std::abs(ay-90)<.001f);
 guard.restore(ax,ay);check(ax==60&&ay==45);
 XrPosef head{{0,0,0,1},{0,0,0}};std::array<XrView,2> eyes{};
 for(auto& e:eyes){e.pose=head;e.fov={-.8f,.8f,.7f,-.7f};}
 float x{},y{};check(argent::camera::headsetEnvelope(head,eyes,x,y));
 check(x>std::tan(.8f)&&y>std::tan(.7f));
 const auto f=argent::camera::engineFov(x,y,1,1,90);check(f>90&&f<100);
 eyes[1].fov.angleRight=1.f;check(argent::camera::headsetEnvelope(head,eyes,x,y));check(x>std::tan(1.f));
 check(argent::camera::engineFov(1,1,1,1,90)==90);
 check(argent::camera::engineFov(1,1,2,1,90)==90); // Vertical envelope wins.
 check(argent::camera::engineFov(0,1,1,1,90)==0);
 const float previous=x;eyes[1].pose.orientation={0,-std::sin(.05f),0,std::cos(.05f)};
 check(argent::camera::headsetEnvelope(head,eyes,x,y)&&x>previous); // Outward cant.
 check(argent::camera::engineFov(100,100,1,1,90)==150);
 eyes[0].fov.angleLeft=0;check(!argent::camera::headsetEnvelope(head,eyes,x,y));
 std::cout<<"PASS asymmetric stereo envelope, safety margin and measured projection conversion\n";
 }catch(const std::exception& e){std::cerr<<e.what();return 1;}}
