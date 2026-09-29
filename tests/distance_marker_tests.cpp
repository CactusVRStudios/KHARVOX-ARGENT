#include "../src/hud/DistanceMarkerPolicy.h"
#include "../src/hud/HudCalibrationPreview.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace argent::hud;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 for(float scale:{.5f,1.f,4.f}){
  check(std::abs(objectiveScale(scale,true,true)-scale/3.f)<1e-6f,"Per-entry objective size incorrect");
  check(objectiveScale(scale,false,true)==scale&&objectiveScale(scale,true,false)==scale,"Pickup/menu scale changed");
  float stored=scale;{PreviewValueScope<float> scope(stored,objectiveScale(stored,true,true));check(std::abs(stored-scale/3)<1e-6f,"Scoped entry scale missing");}
  check(stored==scale,"Native entry scale compounded across frames");
 }
 check(objectiveScale(0,true,true)==0&&objectiveScale(-1,true,true)==-1,"Hidden/invalid scale changed");
 check(objectiveMarker("objective_marker_major")&&objectiveMarker("objective_marker_minor")&&!objectiveMarker("pickup")&&!objectiveMarker("objective_marker_major_other"),"Marker scope too broad");
 check(markerCanvasCaller(0x157cfda,true)&&markerCanvasCaller(0x157cf4a,false)&&!markerCanvasCaller(0xeefd26,true)&&!markerCanvasCaller(0x157cf4a,true),"Wrong screen canvas routing");
 for(float factor:{1.f/3.f,1.f})for(float resolution:{1.f,2.f})for(float roll:{0.f,.5f,1.2f})for(float pitch:{0.f,.4f}){
  DistanceMarkerCanvas c;c.screenSpace=true;c.origin={-1,1,0};c.pixelX=2/(2082*resolution);c.pixelY=-2/(2122*resolution);c.tanX=1.2f;c.tanY=1.1f;
  c.geometryScale=factor;
  const float cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
  c.head={cp,0,sp,-sp*sr,cr,cp*sr,-sp*cr,-sr,cp*cr};
  GraphicBounds b{950*resolution,900*resolution,1150*resolution,1100*resolution};
  const float cx=(b.minX+b.maxX)/2,cy=(b.minY+b.maxY)/2;
  std::array<unsigned char,5*48> bytes{};bytes.fill(0x7a);
  float points[5][3]={{b.minX,b.minY,0},{b.maxX,b.minY,0},{b.minX,b.maxY,0},{b.maxX,b.maxY,0},{cx,cy,0}};
  for(int i=0;i<5;++i)std::memcpy(bytes.data()+48*i,points[i],12);
  auto ray=[&](float x,float y){std::array<float,3> p{};for(int k=0;k<3;++k)p[k]=c.head[k]-c.head[3+k]*(c.origin[0]+x*c.pixelX)*c.tanX+c.head[6+k]*(c.origin[1]+y*c.pixelY)*c.tanY;return p;};
  const auto center=ray(cx,cy);const float length=std::hypot(center[0],center[1]);
  check(billboardDistanceMarker(bytes.data(),5,b,c),"Screen-space marker skipped");
  std::array<std::array<float,3>,5> world{};
  for(int i=0;i<5;++i){float p[3];std::memcpy(p,bytes.data()+i*48,12);auto r=ray(p[0],p[1]);
   const float t=length*length/(r[0]*center[0]+r[1]*center[1]);for(int k=0;k<3;++k)world[i][k]=r[k]*t;
   for(int j=12;j<48;++j)check(bytes[i*48+j]==0x7a,"Screen attributes changed");
  }
  for(int k=0;k<3;++k)check(std::abs(world[4][k]-center[k])<1e-5f,"Screen artwork pivot moved");
  check(std::abs(world[1][2]-world[0][2])<1e-5f,"Screen billboard inherited roll");
  check(std::abs(world[2][0]-world[0][0])<1e-5f&&std::abs(world[2][1]-world[0][1])<1e-5f,"Screen billboard inherited pitch");
  check(std::abs(std::hypot(world[1][0]-world[0][0],world[1][1]-world[0][1])-(b.maxX-b.minX)*c.pixelX*c.tanX*factor)<1e-5f,"Screen width wrong or double-scaled");
  check(std::abs(world[0][2]-world[2][2]+(b.maxY-b.minY)*c.pixelY*c.tanY*factor)<1e-5f,"Screen text height wrong or double-scaled");
  float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(bytes.data()+4*48,&nan,4);const auto saved=bytes;
  check(!billboardDistanceMarker(bytes.data(),5,b,c)&&bytes==saved,"Screen rewrite not atomic");
 }
 for(float factor:{1.f/3.f,1.f})for(float resolution:{1.f,2.f})for(float roll:{0.f,.5f,1.57f})for(float pitch:{0.f,.4f}){
  const auto q=argent::camera::product({0,0,std::sin(roll*.5f),std::cos(roll*.5f)},{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)});
  argent::camera::Basis head{},identity{1,0,0,0,1,0,0,0,1};
  check(argent::camera::rotateBasis(identity,{0,0,0,1},q,head),"Fixture basis failed");
  DistanceMarkerCanvas c;c.origin={2,-1,1.8f};c.eye={0,0,1.7f};c.pixelX=.002f/resolution;c.pixelY=.003f/resolution;
  c.geometryScale=factor;
  for(int k=0;k<3;++k){c.axis[k]=-head[3+k];c.axis[3+k]=-head[6+k];c.axis[6+k]=head[k];}
  // Off-center visible symbol + text in a much larger padded movie.
  GraphicBounds b{1700*resolution,900*resolution,2000*resolution,1100*resolution};
  std::array<unsigned char,5*48> bytes{};bytes.fill(0x7a);
  const float cx=(b.minX+b.maxX)*.5f,cy=(b.minY+b.maxY)*.5f;
  const float vertices[5][3]={{b.minX,b.minY,0},{b.maxX,b.minY,0},{b.minX,b.maxY,0},{b.maxX,b.maxY,0},{cx,cy,0}};
  for(int i=0;i<5;++i)std::memcpy(bytes.data()+i*48,vertices[i],12);
  auto world=[&](int i){float p[3];std::memcpy(p,bytes.data()+i*48,12);std::array<float,3> out=c.origin;
   for(int k=0;k<3;++k)out[k]+=c.axis[k]*p[0]*c.pixelX+c.axis[3+k]*p[1]*c.pixelY+c.axis[6+k]*p[2];return out;};
  const auto center=world(4);
  check(billboardDistanceMarker(bytes.data(),5,b,c),"Billboard rejected valid geometry");
  const auto pivot=world(4),a=world(0),right=world(1),down=world(2);
  for(int k=0;k<3;++k)check(std::abs(center[k]-pivot[k])<1e-5f,"Visible artwork pivot moved");
  check(std::abs(right[2]-a[2])<1e-5f,"Text rotates with head roll");
  check(std::abs(down[0]-a[0])<1e-5f&&std::abs(down[1]-a[1])<1e-5f&&down[2]<a[2],"Billboard inherited head pitch or inverted text");
  check(std::abs(std::hypot(right[0]-a[0],right[1]-a[1])-.6f*factor)<1e-5f&&std::abs(down[2]-a[2]+.6f*factor)<1e-5f,"Symbol/text ratio wrong or double-scaled");
  const float nx=center[0]-c.eye[0],ny=center[1]-c.eye[1];
  check(std::abs((right[0]-a[0])*nx+(right[1]-a[1])*ny)<1e-4f,"Billboard does not face viewer");
  for(int i=0;i<5;++i)for(int j=12;j<48;++j)check(bytes[i*48+j]==0x7a,"UV/color/material changed");
  const auto saved=bytes;c.pixelX=0;check(!billboardDistanceMarker(bytes.data(),5,b,c)&&bytes==saved,"Invalid canvas damaged geometry");
  c.pixelX=.002f;float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(bytes.data()+4*48,&nan,4);const auto invalid=bytes;
  check(!billboardDistanceMarker(bytes.data(),5,b,c)&&bytes==invalid,"Invalid last vertex caused partial rewrite");
 }
 std::cout<<"PASS: distance symbol/text group, artwork pivot, upright billboard, one-third size, resolution independence\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
