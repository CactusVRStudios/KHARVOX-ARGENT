#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/AmdInterface.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace argent::sfs;
void require(bool b){if(!b)throw std::runtime_error("AMD interface regression");}
int main(int argc,char** argv){try{
 const std::string header="#version 460\n#extension GL_AMD_shader_explicit_vertex_parameter : require\n";
 for(auto name:{"NoPersp","NoPerspCentroid","NoPerspSample","Smooth","SmoothCentroid","SmoothSample","PullModel"}){
  auto source=header+"layout(location=0) out vec4 color; void main(){color=vec4(gl_BaryCoord"+name+"AMD.xy,0,1);}";
  auto words=compileGlsl(source,spv::ExecutionModelFragment);
  const auto translated=stereoSource(words);require(translated.find("gl_BuiltIn_")==std::string::npos);
  require(!compileStereoShader(words,{}).empty());
 }
 const auto mixed=header+"layout(location = 23, component = 0) in float ordinary;\nlayout(location = 23, component = 1) __explicitInterpAMD in float explicitValue;\nlayout(location = 0) out vec4 color;\nvoid main(){color=vec4(ordinary,interpolateAtVertexAMD(explicitValue,0),0,1);}";
 auto words=compileGlsl(mixed,spv::ExecutionModelFragment);spirv_cross::Compiler inspect(words);
 auto resources=inspect.get_shader_resources();require(resources.stage_inputs.size()==2);
 for(auto& input:resources.stage_inputs)require(inspect.get_decoration(input.id,spv::DecorationLocation)==23);
 require(inspect.get_decoration(resources.stage_outputs[0].id,spv::DecorationLocation)==0);
 require(!compileStereoShader(words,{}).empty());
 unsigned count=0;
 if(argc>1)for(auto& file:std::filesystem::directory_iterator(argv[1])){
  if(file.path().extension()!=L".frag")continue;
  std::ifstream stream(file.path());std::string source((std::istreambuf_iterator<char>(stream)),{});
  try{require(!compileGlsl(source,spv::ExecutionModelFragment).empty());++count;}
  catch(const std::exception& e){std::cerr<<file.path()<<"\n"<<e.what();return 1;}
 }
 std::cout<<"AMD builtin roundtrips and interface restoration passed; captured shaders compiled="<<count<<"\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
