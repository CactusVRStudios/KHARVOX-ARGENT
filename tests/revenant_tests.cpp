#include "../src/RevenantPolicy.h"
#include <vector>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace argent::revenant;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
 std::vector<unsigned char> player(0x8900),demon(0x20a60);
 const uintptr_t p=uintptr_t(player.data()),d=uintptr_t(demon.data()),pt=42,dt=84;
 auto put=[](uintptr_t a,const auto& v){std::memcpy(reinterpret_cast<void*>(a),&v,sizeof(v));};
 auto read=[&](uintptr_t a,void* out,size_t n){if(!((a>=p&&a+n<=p+player.size())||(a>=d&&a+n<=d+demon.size())))return false;std::memcpy(out,reinterpret_cast<void*>(a),n);return true;};
 put(p,pt);put(d,dt);put(p+0x88b0,Handle{7,7,d});put(d+0x20a08,Handle{8,8,p});
 demon[0x20a48]=demon[0x20a49]=1;
 check(detect(p,pt,dt,read)==d,"Live local Revenant was not detected");
 demon[0x20a48]=0;check(!detect(p,pt,dt,read),"Non-local demon activated VR controls");demon[0x20a48]=1;
 demon[0x20a49]=0;check(!detect(p,pt,dt,read),"Third-person possession intro activated controls");demon[0x20a49]=1;
 put(p+0x88b0,Handle{7,6,d});check(!detect(p,pt,dt,read),"Stale controlled actor cache accepted");
 put(p+0x88b0,Handle{7,7,d});put(d+0x20a08,Handle{8,8,p+8});check(!detect(p,pt,dt,read),"Wrong player backlink accepted");
 put(d+0x20a08,Handle{8,8,p});put(d,uintptr_t(85));check(!detect(p,pt,dt,read),"Another demon type accepted");put(d,dt);
 put(p+0x88b0,Handle{0x1fffffe,0x1fffffe,d});check(!detect(p,pt,dt,read),"Ended possession left mode active");
 float basis[]{0,.8660254f,.5f,-1,0,0,0,-.5f,.8660254f},delta[]{12,-40,0};uint16_t angles[3]{};
 check(encodeView(basis,delta,angles),"Valid head aim rejected");
 check(std::abs(std::remainder(float(angles[0])*360/65536+delta[0]+30,360.f))<.01f,"Head pitch not preserved in native short angles");
 check(std::abs(std::remainder(float(angles[1])*360/65536+delta[1]-90,360.f))<.01f,"Native yaw delta not compensated");
 delta[1]=NAN;check(!encodeView(basis,delta,angles),"Invalid native angle accepted");
 const std::array<uint64_t,5> bindings{1,0x4000000,0x400000,4,0x100000000};
 check(actionButtons(~uint64_t(0),bindings,false,false,false,false)==(~uint64_t(0)&~(1ull|0x4000000ull|0x400000ull|4ull|0x100000000ull)),"Native actions not released");
 check(actionButtons(0,bindings,true,true,true,true)==(1ull|0x4000000ull|0x400000ull|4ull|0x100000000ull),"Revenant actions lost 64-bit jump or native binding");
 std::cout<<"Revenant detection, ownership, pitch/yaw and native actions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
