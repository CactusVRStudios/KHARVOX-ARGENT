#include "../src/RenderResolution.h"
#include <iostream>
#include <stdexcept>
#include <limits>
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(){try{
 auto p=argent::renderResolution(2496,2688,.8f,true);
 check(p.sourceWidth==2234&&p.sourceHeight==2406&&p.engineScale==1&&p.fsrUpscale,"80% must feed genuinely smaller full engine images into FSR");
 check(double(p.sourceWidth)*p.sourceHeight/(2496.*2688.)>.8&&double(p.sourceWidth)*p.sourceHeight/(2496.*2688.)<.803,"Preserve pixel-budget percentage after alignment");
 p=argent::renderResolution(2496,2688,.8f,false);check(p.sourceWidth==2496&&p.sourceHeight==2688&&p.engineScale==.8f&&!p.fsrUpscale,"FSR off preserves native engine scaling");
 for(float s:{1.f,1.5f,2.f}){p=argent::renderResolution(2496,2688,s,true);check(p.sourceWidth==2496&&p.engineScale==s&&!p.fsrUpscale,"Do not add same-size FSR or double supersampling");}
 p=argent::renderResolution(2496,2688,std::numeric_limits<float>::quiet_NaN(),true);check(p.engineScale==1&&!p.fsrUpscale,"Invalid saved scale fallback");
 p=argent::renderResolution(2496,2688,.4f,true);check(p.sourceWidth<2496&&p.sourceHeight<2688&&p.sourceWidth%2==0&&p.sourceHeight%2==0,"Low scale bounds and even extents");
 std::cout<<"PASS actual FSR input/output separation, pixel budget, native scaling and supersampling\n";
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
