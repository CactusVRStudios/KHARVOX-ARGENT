#define argentMonkeyLaunch testedMonkeyLaunch
#include "../src/EternalMonkeyBar.cpp"
#undef argentMonkeyLaunch
#include <iostream>
#include <stdexcept>
#include <limits>
#include <vector>
namespace argent {void log(const std::string&) {}}
bool ready=true,aligned=false;
float nativeData[9]{};
const float* __fastcall nativeGetter(void*) {return nativeData;}
unsigned cancellations{};
unsigned stops{},completions{};void* stoppedOwner{};bool refill{},complete=true;
void __fastcall stopFixture(void* owner,bool refillMeter) {++stops;stoppedOwner=owner;refill=refillMeter;}
bool __fastcall completionFixture(void*,unsigned short*,int) {++completions;return complete;}
void __fastcall nativeCancelFixture(void*) {++cancellations;}
namespace argent::camera {
bool monkeyBarPose(float* origin,float* axis,float* offset) noexcept {
 const float o[]{1,2,3},a[]{0,.8f,.6f,-1,0,0,0,-.6f,.8f};
 if(offset)std::memcpy(offset,o,sizeof(o));std::memcpy(origin,o,sizeof(o));std::memcpy(axis,a,sizeof(a));return ready;
}
}
extern "C" {int monkeyFixture();void monkeyFixtureResume();}
extern "C" void argentMonkeyLaunch(void* mechanic,float* x,float* y) {
 aligned=uintptr_t(mechanic)==0x5678&&(uintptr_t(_AddressOfReturnAddress())&15)==8;
 *x=10;*y=20;
}
void check(bool ok){if(!ok)throw std::runtime_error("monkey bar regression");}
int main(){try{
 using namespace argent;
 argentMonkeyResume=reinterpret_cast<void*>(&monkeyFixtureResume);
 check(monkeyFixture()==0&&aligned);
 float a[]{-1,0,0},x=4,y=5;
 check(monkey::launchHeading(a,x,y)&&x==-1&&y==0);
 a[0]=0;a[2]=1;check(!monkey::launchHeading(a,x,y)&&x==-1&&y==0);
 a[0]=std::numeric_limits<float>::quiet_NaN();check(!monkey::launchHeading(a,x,y));
 check(monkey::axisCaller(0x1399bf7)&&!monkey::axisCaller(0x1399bf1));
 check(monkey::originCaller(0x139a85f)&&!monkey::originCaller(0x1399bf7));
 void* owner=reinterpret_cast<void*>(0x1234);presentation::player=uintptr_t(owner);presentation::worldPresentation=true;
 monkey::nativeAxis=monkey::nativeOrigin=&nativeGetter;
 monkey::Pose pose;check(monkey::capture(owner,pose)&&monkey::fresh(owner,pose));
 check(pose.origin[2]==3&&pose.axis[1]==.8f);
 monkey::nativeAxis=monkey::nativeOrigin=&nativeGetter;
 check(monkey::axisFor(owner,0x123)==nativeData&&monkey::originFor(owner,0x123)==nativeData);
 check(monkey::originFor(owner,0x139a79a)!=nativeData);
 ready=false; // Remaining query reads reuse exactly the snapshot, not a new pose.
 check(monkey::axisFor(owner,0x139a842)[1]==.8f&&monkey::originFor(owner,0x139a85f)[2]==3);
 check(monkey::axisFor(owner,0x139a8f0)[8]==.8f&&monkey::originFor(owner,0x139a90e)[2]==3);
 check(monkey::axisFor(reinterpret_cast<void*>(0x4567),0x139a842)==nativeData);
 check(monkey::originFor(owner,0x139a79a)==nativeData&&monkey::axisFor(owner,0x139a842)==nativeData);
 ready=true;check(monkey::axisFor(owner,0x1399bf7)!=nativeData&&monkey::originFor(owner,0x1399c83)[2]==3);
 monkey::view.tick-=101;check(monkey::originFor(owner,0x1399c83)==nativeData);
 check(!monkey::fresh(reinterpret_cast<void*>(0x4567),pose));
 pose.tick-=101;check(!monkey::fresh(owner,pose));
 ready=false;check(!monkey::capture(owner,pose)&&!pose.valid);
 ready=true;presentation::syncAttack=true;check(!monkey::capture(owner,pose));
 presentation::syncAttack=false;presentation::worldPresentation=false;presentation::gameplayInput=false;
 check(!monkey::capture(owner,pose));
 // A dash advances simulation while the rendered HMD pose stays unchanged.
 presentation::worldPresentation=true;nativeData[0]=50;nativeData[1]=-20;
 check(monkey::capture(owner,pose)&&pose.origin[0]==51&&pose.origin[1]==-18&&pose.origin[2]==3);
 float badOffset[]{0,0,std::numeric_limits<float>::infinity()},output[]{7,8,9};
 check(!monkey::currentOrigin(nativeData,badOffset,output)&&output[0]==7&&output[2]==9);
 for(int state:{0,1,4,5})check(!monkey::protectDashHandoff(0xfbdc3a,state,0));
 check(monkey::protectDashHandoff(0xfbdc3a,2,0)&&monkey::protectDashHandoff(0xfbddc9,3,99));
 check(!monkey::protectDashHandoff(0xfbdc3a,2,100)&&!monkey::protectDashHandoff(0x1234,2,0));
 std::vector<unsigned char> player(0x37000),fsm(32);
 owner=player.data();presentation::player=uintptr_t(owner);presentation::skippedMonkeyBarOwner=uintptr_t(owner);
 auto mechanic=player.data()+0x36e48;auto fsmPointer=uintptr_t(fsm.data());int state=2;
 std::memcpy(mechanic+0x18,&owner,8);std::memcpy(mechanic+0x50,&fsmPointer,8);std::memcpy(fsm.data()+12,&state,4);
 monkey::nativeCancel=&nativeCancelFixture;monkey::acceptedBar(uintptr_t(owner));
 monkey::cancelFor(mechanic,0xfbdc3a);check(cancellations==0);
 monkey::cancelFor(mechanic,0x1234);check(cancellations==1);
 state=4;std::memcpy(fsm.data()+12,&state,4);monkey::cancelFor(mechanic,0xfbdc3a);check(cancellations==2);
 state=2;std::memcpy(fsm.data()+12,&state,4);monkey::entryTick-=101;
 monkey::cancelFor(mechanic,0xfbdc3a);check(cancellations==3);
 monkey::acceptedBar(uintptr_t(owner));presentation::skippedMonkeyBarOwner=0;
 monkey::cancelFor(mechanic,0xfbdc3a);check(cancellations==4);
 // An accepted dash/bar handoff must use native stop without refilling and
 // wait for normal native cleanup, with a bounded fallback and narrow scope.
 player.resize(0x4d000);owner=player.data();presentation::player=uintptr_t(owner);
 presentation::skippedMonkeyBarOwner=uintptr_t(owner);
 std::vector<unsigned char> controller(0x2a0),dash(0x120);
 auto cp=uintptr_t(controller.data()),dp=uintptr_t(dash.data());
 std::memcpy(player.data()+0x4ce88,&cp,8);std::memcpy(controller.data()+0x290,&owner,8);
 std::memcpy(controller.data()+0x298,&dp,8);dash[0x118]=1;
 auto handle=reinterpret_cast<unsigned short*>(player.data()+0x36e48+0x108);*handle=0xffff;
 monkey::nativeStopDash=&stopFixture;monkey::nativeCompletion=&completionFixture;
 monkey::acceptedBar(uintptr_t(owner));check(stops==1&&stoppedOwner==owner&&!refill);
 check(!monkey::completionFor(nullptr,handle,1,0x1398a8c)&&stops==2);
 check(monkey::completionFor(nullptr,handle,1,0x1234)&&stops==2);
 unsigned short other=0xffff;check(monkey::completionFor(nullptr,&other,1,0x1398a8c)&&stops==2);
 complete=false;check(!monkey::completionFor(nullptr,handle,1,0x1398a8c)&&stops==2);complete=true;
 dash[0x118]=0;player[0xd25f]=1;check(!monkey::completionFor(nullptr,handle,1,0x1398a8c)&&stops==3);
 player[0xd25f]=0;check(monkey::completionFor(nullptr,handle,1,0x1398a8c)&&stops==3);
 monkey::acceptedBar(uintptr_t(owner));check(stops==3);
 dash[0x118]=1;monkey::entryTick-=101;
 check(monkey::completionFor(nullptr,handle,1,0x1398a8c)&&stops==3);
 presentation::syncAttack=true;monkey::acceptedBar(uintptr_t(owner));
 check(monkey::completionFor(nullptr,handle,1,0x1398a8c)&&stops==3);presentation::syncAttack=false;
 presentation::skippedMonkeyBarOwner=0;
 check(monkey::completionFor(nullptr,handle,1,0x1398a8c)&&stops==3);
 uintptr_t wrongOwner=1;std::memcpy(controller.data()+0x290,&wrongOwner,8);
 monkey::acceptedBar(uintptr_t(owner));check(stops==3);
 std::cout<<"PASS monkey bar scope, freshness, direction and native bridge registers\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
