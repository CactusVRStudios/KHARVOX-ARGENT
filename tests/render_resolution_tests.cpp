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
 for(float scale:{.4f,.8f,1.f,1.3f,2.f}){
  p=argent::renderResolution(2496,2688,scale,false,true);
  const double pixels=double(p.sourceWidth)*p.sourceHeight/(2496.*2688.);
  check(pixels+1e-6>=scale&&pixels<scale+.004,"Framegen RenderScale changes actual eye output pixel budget");
  check(p.engineScale==1&&!p.fsrUpscale,"DLSS-G must not stack internal scaling or FSR");
  check(p.sourceWidth%2==0&&p.sourceHeight%2==0,"Framegen output remains even for color packing");
 }
 p=argent::renderResolution(2496,2688,1.f,false,true);
 check(p.sourceWidth==2496&&p.sourceHeight==2688,"100% framegen preserves runtime native output");
 std::cout<<"PASS actual FSR input/output separation, pixel budget, native scaling and supersampling\n";
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
