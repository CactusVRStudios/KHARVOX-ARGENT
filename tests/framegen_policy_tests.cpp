#include "../src/framegen/policy.hpp"
#include "../src/framegen/AdapterPolicy.h"
#include "../src/BodyYawFollow.h"
#include <iostream>
#include <stdexcept>
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
 using namespace argent::fg;
 check(std::wstring(runtimeRelativePath(L"NVIDIA GeForce RTX 5090"))==L"rtx50/nvngx_dlssg.dll","Blackwell bundled native DLL");
 check(std::wstring(runtimeRelativePath(L"NVIDIA GeForce RTX 4080 SUPER"))==L"rtx40/dlssg_sm86/runtime/nvngx_dlssg.dll","Ada runtime");
 check(std::wstring(runtimeRelativePath(L"NVIDIA GeForce RTX 3080"))==L"rtx20-30/dlssg_sm86/runtime/nvngx_dlssg.dll","Ampere runtime");
 Identity a{10,1920,1080,true,true,false},b{11,1920,1080,true,true,false};check(consecutive(a,b),"neighbors");
 b.pair=12;check(!consecutive(a,b),"no interpolation over dropped pair");b.pair=11;b.reset=true;check(!consecutive(a,b),"reset");b.reset=false;b.gameplay=false;check(!consecutive(a,b),"menu");b.gameplay=true;b.width=1280;check(!consecutive(a,b),"resize");
 check(supportedAdapter(0x10de,L"NVIDIA GeForce RTX 2060"),"Turing");check(supportedAdapter(0x10de,L"NVIDIA GeForce RTX 3080 Laptop GPU"),"Ampere");check(supportedAdapter(0x10de,L"NVIDIA GeForce RTX 4080 SUPER"),"Ada");check(supportedAdapter(0x10de,L"NVIDIA GeForce RTX 5090"),"Blackwell");
 check(!supportedAdapter(0x1002,L"AMD Radeon RX 9070 XT"),"AMD hidden");check(!supportedAdapter(0x10de,L"NVIDIA GeForce GTX 1660"),"GTX hidden");check(!supportedAdapter(0x10de,L"NVIDIA Quadro RTX 5000"),"unknown professional profile hidden");check(!supportedAdapter(0x10de,L"NVIDIA GeForce RTX 4090",true),"software hidden");
 using argent::camera::BodyYawFollow;
 BodyYawFollow turn;turn.smooth(1,200,true,1000);turn.advanceForRender(1010);check(std::abs(turn.accepted-2)<.001,"wall-clock render advance");turn.advanceForRender(1010);check(std::abs(turn.accepted-2)<.001,"same interval not integrated twice");turn.smooth(1,200,true,1020);check(std::abs(turn.accepted-4)<.001,"publication consumes only remaining interval");turn.smooth(0,200,true,1025);float stop=turn.accepted;turn.advanceForRender(1030);check(turn.accepted==stop,"no turn after release");turn.smooth(1,200,true,1040);stop=turn.accepted;turn.advanceForRender(2000);check(turn.accepted==stop,"stale prediction capped");
 BodyYawFollow regular,late;regular.smooth(1,200,true,1000);late.smooth(1,200,true,1000);
 for(int t=1010;t<=1100;t+=10){regular.smooth(1,200,true,t);late.advanceForRender(t-5);late.smooth(1,200,true,t);}check(std::abs(regular.accepted-late.accepted)<.001,"FG wait jitter cannot change total turn");
 BodyYawFollow carrier;carrier.observe(0);carrier.request(10,false,true);carrier.request(0,false,true);carrier.observe(2);check(std::abs(carrier.accepted-2)<.001,"late native carrier still rebased after turn release");carrier.request(0,false,true);carrier.observe(3);check(std::abs(carrier.accepted-3)<.001,"second late carrier");carrier.request(0,false,true);carrier.observe(4);check(std::abs(carrier.accepted-3)<.001,"carrier tail bounded");
 std::cout<<"PASS: frame boundaries, adapter gating, wall-clock turn, release and delayed body carrier\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
