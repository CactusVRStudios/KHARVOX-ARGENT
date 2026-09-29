#include "../src/openxr/HandSmoothing.h"
#include "../src/openxr/WeaponConfig.h"
#include <stdexcept>
#include <iostream>
using namespace argent::input;
void check(bool ok){if(!ok)throw std::runtime_error("hand smoothing regression");}
bool near(float a,float b){return std::abs(a-b)<.00001f;}
int main(){try{
 HandSmoothing filter;
 XrPosef grip{{0,0,0,1},{0,0,0}},aim{{0,0,0,1},{0,0,-.1f}};
 filter.update(grip,aim,true,true,1000000000,true);
 grip.position.x=.01f;aim.position.x=.01f;
 filter.update(grip,aim,true,true,1011111111,true);
 check(grip.position.x>0&&grip.position.x<.01f);
 check(near(grip.position.x,aim.position.x)&&near(aim.position.z-grip.position.z,-.1f));
 // One rigid correction preserves aim/grip geometry through rotation too.
 const auto relative=euler(3,7,2);
 grip={{0,0,0,1},{.02f,0,0}};grip.orientation=euler(0,10,0);
 aim={product(grip.orientation,relative),add(grip.position,rotate(grip.orientation,{0,0,-.1f}))};
 filter.update(grip,aim,true,true,1022222222,true);
 const auto offset=rotate(inverse(grip.orientation),sub(aim.position,grip.position));
 const auto rot=product(inverse(grip.orientation),aim.orientation);
 check(near(offset.x,0)&&near(offset.z,-.1f)&&near(rot.y,relative.y)&&near(rot.w,relative.w));
 // Disabled is exact passthrough, and reacquisition/gaps never blend old poses.
 grip.position.x=.2f;aim.position.x=.3f;filter.update(grip,aim,true,true,1033333333,false);
 check(grip.position.x==.2f&&aim.position.x==.3f&&!filter.held);
 filter.update(grip,aim,true,true,1044444444,true);check(grip.position.x==.2f);
 filter.update(grip,aim,false,true,1055555555,true);check(!filter.held);
 grip.position.x=.25f;filter.update(grip,aim,true,true,1066666666,true);check(grip.position.x==.25f);
 grip.position.x=1;filter.update(grip,aim,true,true,1077777777,true);check(grip.position.x==1);
 grip.position.x=1.02f;filter.update(grip,aim,true,true,1300000000,true);check(grip.position.x==1.02f);
 // q and -q encode the same orientation; no 360-degree interpolation.
 grip.orientation={0,0,0,-1};filter.update(grip,aim,true,true,1311111111,true);
 check(std::abs(grip.orientation.w)>.999f);
 // Equivalent elapsed time gives equivalent slow-motion response at 72/144 Hz.
 auto response=[](int hz){HandSmoothing f;XrPosef g{{0,0,0,1},{}},a=g;
  f.update(g,a,true,true,1000000000,true);
  for(int j=1;j<=hz/8;++j){g.position.x=.001f;a=g;f.update(g,a,true,true,1000000000+XrTime(double(j)*1e9/hz),true);}return g.position.x;};
 check(std::abs(response(72)-response(144))<.00001f);
 WeaponConfig cfg;check(!cfg.handSmoothing);
 std::istringstream enabled("hand_smoothing 1\n");check(readWeaponConfig(enabled,cfg)&&cfg.handSmoothing);
 std::istringstream invalid("hand_smoothing 2\n");check(!readWeaponConfig(invalid,cfg)&&cfg.handSmoothing);
 std::istringstream absent("");check(readWeaponConfig(absent,cfg)&&!cfg.handSmoothing);
 std::cout<<"PASS hand smoothing geometry, resets, timing and config\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what();return 1;}}
