#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/ShaderIdentity.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
int main(int argc,char** argv){try{
 if(argc!=2)return 2;unsigned tested=0;
 for(const auto& entry:std::filesystem::directory_iterator(argv[1])){
  if(entry.path().extension()!=L".spv")continue;
  std::ifstream file(entry.path(),std::ios::binary|std::ios::ate);std::vector<uint32_t> words(size_t(file.tellg())/4);file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),words.size()*4);
  const auto hash=kharvox::sfs::profileHash(words.data(),uint32_t(words.size()*4));const auto rule=argent::sfs::eternalLightGridRule(hash);if(!rule.pixels)continue;
  spirv_cross::Compiler reflect(words);if(reflect.get_execution_model()!=spv::ExecutionModelFragment)throw std::runtime_error("Light grid identity is not a fragment");
  const auto resources=reflect.get_shader_resources();std::set<uint32_t> bindings;
  auto collect=[&](const auto& list){for(auto& r:list)if(reflect.get_decoration(r.id,spv::DecorationDescriptorSet)==0)bindings.insert(reflect.get_decoration(r.id,spv::DecorationBinding));};
  collect(resources.uniform_buffers);collect(resources.storage_buffers);collect(resources.separate_images);collect(resources.separate_samplers);collect(resources.sampled_images);collect(resources.storage_images);
  argent::sfs::ShaderOptions options;options.lightGridShader=hash;while(bindings.count(options.binding))++options.binding;
  const auto original=argent::sfs::stereoSource(words,{});
  for(bool mono:{false,true}){options.monoView=mono;const auto fixed=argent::sfs::stereoSource(words,options);
   if(fixed.find("argentGridPixel")==std::string::npos||fixed.find(std::string(rule.x)+" = uint(argentGridPixel.x) / 256u;")==std::string::npos)throw std::runtime_error("Grid not remapped before lookup");
   if(fixed.find("if (argentProjection.diagnostics.x > 0.5)")==std::string::npos)throw std::runtime_error("Default-off runtime switch missing");
   // Preserve every original eye-local FragCoord expression. Only shared-grid
   // integer addresses are patched, not all screen/depth/texture operations.
   auto occurrences=[](const std::string& s,const std::string& token){size_t n=0,p=0;while((p=s.find(token,p))!=std::string::npos){++n;p+=token.size();}return n;};
   if(occurrences(original,"gl_FragCoord")!=occurrences(fixed,"gl_FragCoord"))throw std::runtime_error("Eye-local coordinates changed");
   const auto compiled=argent::sfs::compileStereoShader(words,options);if(compiled.empty())throw std::runtime_error("Grid compilation failed");
   spirv_cross::Compiler patched(compiled);bool layoutVerified=false;
   for(const auto& buffer:patched.get_shader_resources().uniform_buffers)if(patched.get_decoration(buffer.id,spv::DecorationDescriptorSet)==0&&patched.get_decoration(buffer.id,spv::DecorationBinding)==options.binding){
    const auto& type=patched.get_type(buffer.base_type_id);
    if(type.member_types.size()!=4||patched.type_struct_member_offset(type,3)!=288||patched.get_declared_struct_size(type)!=304)throw std::runtime_error("CPU/shader diagnostics layout mismatch");
    layoutVerified=true;
   }
   if(!layoutVerified)throw std::runtime_error("Diagnostics UBO missing");
  }
  std::string unknown="void main() {}";bool rejected=false;try{argent::sfs::correctEternalLightGrid(unknown,rule);}catch(const std::runtime_error&){rejected=true;}if(!rejected)throw std::runtime_error("Unknown grid structure accepted");
  ++tested;
 }
 if(tested!=40)throw std::runtime_error("Captured material fixture count changed");
 if(argent::sfs::eternalLightGridRule(0).pixels)throw std::runtime_error("Unknown identity accepted");
 std::cout<<tested<<" exact material shaders: mono/stereo light grid correction compiled\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
