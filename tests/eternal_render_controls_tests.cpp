#include "../src/EternalRenderControls.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace argent::camera;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(int argc,char** argv){try{
 argent::build::microsoftStore=argc>1&&std::string(argv[1])=="--store";
 // Simulated process memory exercises wrapper redirection and exact name guards.
 const uintptr_t base=0x10000000;std::array<std::array<unsigned char,80>,renderControls.size()> data{};
 std::array<uintptr_t,renderControls.size()> pointers{};std::array<const char*,renderControls.size()> names{};
 for(size_t i=0;i<data.size();++i){pointers[i]=0x20000000+i*0x100;names[i]=renderControls[i].name;uintptr_t name=0x30000000+i*0x100;std::memcpy(data[i].data()+0x28,&name,8);int n=i==0?80:renderControls[i].integer;std::memcpy(data[i].data()+8,&n,4);}
 auto read=[&](uintptr_t addr,void* out,size_t n){for(size_t i=0;i<data.size();++i){
  if(addr==base+argent::build::rva(renderControls[i].rva)&&n==8){std::memcpy(out,&pointers[i],8);return true;}
  if(addr>=pointers[i]&&addr+n<=pointers[i]+80){std::memcpy(out,data[i].data()+addr-pointers[i],n);return true;}
  if(addr==0x30000000+i*0x100&&n<=std::strlen(names[i])+1){std::memcpy(out,names[i],n);return true;}
 }return false;};
 unsigned writes=0,reports=0;auto set=[&](void* obj,const char* value,bool force){check(force,"Missing engine force flag");for(size_t i=0;i<data.size();++i)if(reinterpret_cast<uintptr_t>(obj)==base+argent::build::rva(renderControls[i].rva)){int n=std::stoi(value);std::memcpy(data[i].data()+8,&n,4);float f=std::stof(value);std::memcpy(data[i].data()+12,&f,4);++writes;return true;}throw std::runtime_error("Unknown wrapper written");};
 auto report=[&](const RenderControl& c,bool accepted,int,bool valid,int after){check(accepted&&valid&&after==c.integer,"Setter readback mismatch");++reports;};
 enforceRenderControls(base,read,set,report);check(writes==1&&reports==1,"Initial correction count");
 enforceRenderControls(base,read,set,report);check(writes==1,"Unchanged values rewritten");
 int preset=80;std::memcpy(data[0].data()+8,&preset,4);preset=1;std::memcpy(data[1].data()+8,&preset,4);
 pointers[0]+=0x10000;enforceRenderControls(base,read,set,report);check(writes==3,"Level reset or redirected wrapper missed");
 names[0]="x_fov";bool rejected=false;
 enforceRenderControls(base,read,set,[&](const RenderControl& c,bool accepted,int,bool valid,int){check(c.rva==renderControls[0].rva&&!accepted&&!valid,"Identity failure not reported");rejected=true;});
 check(rejected&&writes==3,"Mismatched CVar modified");
 names[0]=renderControls[0].name;
 const size_t ratio=6;check(renderControls[ratio].floating,"Lens-flare ratio requires float verification");
 float fractional=0.5f;std::memcpy(data[ratio].data()+12,&fractional,4);
 enforceRenderControls(base,read,set,report);check(writes==4,"Fractional flare ratio incorrectly treated as zero");
 enforceRenderControls(base,read,set,report);check(writes==4,"Disabled flares repeatedly rewritten");
 int flaresEnabled=0;std::memcpy(data[5].data()+8,&flaresEnabled,4);flaresEnabled=1;std::memcpy(data[7].data()+8,&flaresEnabled,4);
 enforceRenderControls(base,read,set,report);check(writes==6,"Flare settings not restored after preset reset");

 const size_t mouse=8;check(std::string(renderControls[mouse].name)=="in_mouse","Wrong mouse device gate");
 int mouseEnabled=1;std::memcpy(data[mouse].data()+8,&mouseEnabled,4);
 enforceRenderControls(base,read,set,report);check(writes==7,"Mouse reset not suppressed");
 enforceRenderControls(base,read,set,report);check(writes==7,"Disabled mouse rewritten every poll");

 const size_t reticle=9;check(std::string(renderControls[reticle].name)=="g_reticleMode"&&renderControls[reticle].integer==2,"Wrong reticle hide mode");
 int reticleFull=0;std::memcpy(data[reticle].data()+8,&reticleFull,4);
 enforceRenderControls(base,read,set,report);check(writes==8,"Profile reset reenabled reticle");
 enforceRenderControls(base,read,set,report);check(writes==8,"Hidden reticle repeatedly rewritten");

 const size_t shakes=10,kick=11;
 check(std::string(renderControls[shakes].name)=="view_skipShakes"&&renderControls[shakes].integer==1,"Wrong shake gate");
 check(std::string(renderControls[kick].name)=="view_mpViewKick"&&renderControls[kick].integer==0,"Wrong view-kick gate");
 int shakeEnabled=0,kickEnabled=1;
 std::memcpy(data[shakes].data()+8,&shakeEnabled,4);std::memcpy(data[kick].data()+8,&kickEnabled,4);
 pointers[shakes]+=0x10000;
 enforceRenderControls(base,read,set,report);check(writes==10,"Reload/menu reset reenabled camera shake");
 enforceRenderControls(base,read,set,report);check(writes==10,"Disabled camera shake repeatedly rewritten");

 int bobEnabled=1,noBobDisabled=0;
 std::memcpy(data[12].data()+8,&bobEnabled,4);std::memcpy(data[13].data()+8,&noBobDisabled,4);
 check(std::string(renderControls[12].name)=="g_setting_hands_bob"&&std::string(renderControls[13].name)=="pm_noBob","Wrong Weapon Bob controls");
 enforceRenderControls(base,read,set,report);check(writes==12,"Weapon Bob reset not corrected");
 enforceRenderControls(base,read,set,report);check(writes==12,"Weapon Bob repeatedly rewritten");
 const size_t depthBias=14;
 check(std::string(renderControls[depthBias].name)=="r_customViewProjectionMatrixDepthBias"&&renderControls[depthBias].floating,"Wrong weapon depth control");
 float weaponBias=0.2f;
 std::memcpy(data[depthBias].data()+12,&weaponBias,4);
 enforceRenderControls(base,read,set,report);check(writes==13,"Fractional weapon depth priority was not removed");
 enforceRenderControls(base,read,set,report);check(writes==13,"Unbiased weapon depth repeatedly rewritten");
 pointers[depthBias]+=0x10000;
 std::memcpy(data[depthBias].data()+12,&weaponBias,4);
 enforceRenderControls(base,read,set,report);check(writes==14,"Reload restored weapon depth priority");
 int kicks=0,snapView=1;
 check(std::string(renderControls[15].name)=="view_skipKicks"&&std::string(renderControls[16].name)=="meleeLunge_snapViewToTarget","Wrong melee camera gates");
 std::memcpy(data[15].data()+8,&kicks,4);std::memcpy(data[16].data()+8,&snapView,4);
 enforceRenderControls(base,read,set,report);check(writes==16,"Melee target pull or camera kicks still enabled");
 enforceRenderControls(base,read,set,report);check(writes==16,"Stable melee gates repeatedly written");
 const size_t ssr=17;check(std::string(renderControls[ssr].name)=="r_SSR"&&renderControls[ssr].integer==0,"SSR default must be off");
 int ssrEnabled=1;std::memcpy(data[ssr].data()+8,&ssrEnabled,4);
 enforceRenderControls(base,read,set,report);check(writes==17,"Graphics reset reenabled SSR");
 enforceRenderControls(base,read,set,report);check(writes==17,"Disabled SSR repeatedly rewritten");
 const size_t wheelDof=19;
 check(std::string(renderControls[wheelDof].name)=="weaponWheel_dof_enable"&&renderControls[wheelDof].integer==0,"Wrong wheel DOF gate");
 int dofEnabled=1;std::memcpy(data[wheelDof].data()+8,&dofEnabled,4);
 enforceRenderControls(base,read,set,report);check(writes==18,"Wheel blur not disabled");
 enforceRenderControls(base,read,set,report);check(writes==18,"Wheel blur repeatedly rewritten");
 pointers[wheelDof]+=0x10000;std::memcpy(data[wheelDof].data()+8,&dofEnabled,4);
 enforceRenderControls(base,read,set,report);check(writes==19,"Redirected wheel blur gate missed");
 LightCullingScope scope;int light=0;unsigned lightWrites=0;
 WheelBloomScope bloom;int bloomValue=1;unsigned bloomWrites=0;
 auto readBloom=[&](int& value){value=bloomValue;return true;};
 auto setBloom=[&](int value){bloomValue=value;++bloomWrites;return true;};
 check(bloom.update(false,readBloom,setBloom)&&bloomWrites==0,"Closed wheel changed bloom");
 check(bloom.update(true,readBloom,setBloom)&&bloomValue==0&&bloom.restore==1,"Open wheel retained bloom");
 check(bloom.update(true,readBloom,setBloom)&&bloomWrites==1,"Stable wheel rewrote bloom");
 check(!bloom.update(false,readBloom,[](int){return false;})&&bloom.held,"Failed restore was forgotten");
 check(bloom.update(false,readBloom,setBloom)&&bloomValue==1&&!bloom.held,"Wheel close failed to restore bloom");
 bloomValue=0;
 check(bloom.update(true,readBloom,setBloom)&&bloom.restore==0,"Original bloom-off setting lost");
 check(bloom.update(false,readBloom,setBloom)&&bloomValue==0,"Wheel enabled previously disabled bloom");
 bloomValue=1;bloom.update(true,readBloom,setBloom);bloomValue=1;
 const auto beforeExternal=bloomWrites;
 check(bloom.update(false,readBloom,setBloom)&&bloomWrites==beforeExternal,"External bloom setting overwritten");
 auto readLight=[&](int& value){value=light;return true;};auto setLight=[&](int value){light=value;++lightWrites;return true;};
 check(scope.update(false,readLight,setLight)&&lightWrites==0,"Quad wrote light CVar");
 check(scope.update(true,readLight,setLight)&&light==1&&scope.held,"World did not disable GPU light rejection");
 check(scope.update(true,readLight,setLight)&&lightWrites==1,"Stable world repeated light writes");
 check(scope.update(false,readLight,setLight)&&light==0&&!scope.held,"Quad did not restore light value");
 light=1;check(scope.update(true,readLight,setLight)&&scope.restore==1,"Original enabled value lost");
 light=0;check(scope.update(false,readLight,setLight)&&light==0,"External change overwritten on exit");
 check(!scope.update(true,readLight,[](int){return false;})&&light==0,"Denied engine write counted as correction");
 const size_t rt=renderControls.size()-1;
 check(std::string(renderControls[rt].name)=="r_enableRayTracing"&&renderControls[rt].integer==0,"Missing RT gate");
 int rtOn=1;std::memcpy(data[rt].data()+8,&rtOn,4);const auto beforeRt=writes;
 enforceRenderControls(base,read,set,report);check(writes==beforeRt+1,"RT menu enable not corrected");
 enforceRenderControls(base,read,set,report);check(writes==beforeRt+1,"RT off rewritten every poll");
 pointers[rt]+=0x10000;std::memcpy(data[rt].data()+8,&rtOn,4);
 enforceRenderControls(base,read,set,report);check(writes==beforeRt+2,"RT preset/redirected wrapper not corrected");
 std::cout<<"Eternal indirect CVars: preset reset, redirection, engine setter, readback and name guard pass\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}


