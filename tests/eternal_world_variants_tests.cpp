#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/ShaderIdentity.h"
#include "../src/sfs/EternalProfile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
using namespace argent::sfs;
int main(int argc,char** argv){try{
 if(argc!=2)return 2;
 const std::set<uint64_t> variants={
#include "../src/sfs/EternalWorldVariants.inc"
 };
 const std::set<uint64_t> shadow={0x39d5b4f395660d17ull,0x373c10037d0c2c8aull,0x55efeba54edf76dull,0x933591cf674c52ccull,0x48e6018290b4a719ull,0x32281663537f5afcull,0xf136bdb90fe92ef6ull,0x1762f78a98b6df0full,0xa11e2ebf049d9d57ull,0xfd3a9c03e15a3bdull};
 for(auto h:shadow)if(variants.count(h))throw std::runtime_error("Explicit Vk3D shadow exclusion projected");
 const std::set<uint64_t> screen={0xe3881ca9ca28b278ull,0xaef5cc288c7813f2ull,0x70903b1635c3f665ull};
 const auto profile=eternalProfile();
 for(auto h:screen)if(profile.projectionShaders.count(h))throw std::runtime_error("Screen/texture-space surface receives world projection");
 bool capturedShadowTested=false;
 std::set<uint64_t> testedScreen;
 std::set<uint64_t> tested;
 for(const auto& file:std::filesystem::directory_iterator(argv[1])){
  if(file.path().extension()!=L".spv")continue;
  std::ifstream in(file.path(),std::ios::binary|std::ios::ate);std::vector<uint32_t> words(size_t(in.tellg())/4);in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),words.size()*4);
  auto hash=kharvox::sfs::profileHash(words.data(),uint32_t(words.size()*4));
  if(hash==0x39d5b4f395660d17ull){
   if(profile.projectionShaders.count(hash))throw std::runtime_error("Captured shadow atlas caster still receives eye projection");
   for(bool mono:{false,true}){
    ShaderOptions options;options.monoView=mono;options.project=profile.projectionShaders.count(hash)!=0;
    const auto source=stereoSource(words,options);
    if(source.find("argentProjection")!=std::string::npos||source.find("gl_Position.w = _12(_655, _656);")==std::string::npos)
     throw std::runtime_error("Native shadow light projection changed");
    if(compileStereoShader(words,options).empty())throw std::runtime_error("Shadow shader compilation failed");
   }
   capturedShadowTested=true;
  }
  if(screen.count(hash)){
   for(bool mono:{false,true}){
    ShaderOptions options;options.monoView=mono;
    const auto source=stereoSource(words,options);
    if(source.find("argentProjection")!=std::string::npos)throw std::runtime_error("Copy coverage transformed");
    auto patched=compileStereoShader(words,options);spirv_cross::Compiler result(patched);
    if(result.get_execution_model()!=spv::ExecutionModelVertex)throw std::runtime_error("Copy stage changed");
    if(hash!=0x70903b1635c3f665ull&&source.find("gl_Position = _54;")==std::string::npos)throw std::runtime_error("Native fullscreen position lost");
   }
   testedScreen.insert(hash);
  }
  if(!variants.count(hash))continue;
  spirv_cross::Compiler original(words);auto resources=original.get_shader_resources();
  if(original.get_execution_model()!=spv::ExecutionModelVertex)throw std::runtime_error("Non-vertex selected");
  ShaderOptions options;options.project=true;std::set<uint32_t> used;
  auto collect=[&](const auto& list){for(const auto& r:list)if(original.get_decoration(r.id,spv::DecorationDescriptorSet)==0)used.insert(original.get_decoration(r.id,spv::DecorationBinding));};
  collect(resources.uniform_buffers);collect(resources.storage_buffers);collect(resources.separate_images);collect(resources.separate_samplers);collect(resources.sampled_images);while(used.count(options.binding))++options.binding;
  for(bool mono:{false,true}){options.monoView=mono;auto source=stereoSource(words,options);auto patched=compileStereoShader(words,options);spirv_cross::Compiler result(patched);
   if(source.find("argentProjection.clipFromCenter")==std::string::npos)throw std::runtime_error("World projection missing");
   const auto out=result.get_shader_resources();if(out.storage_buffers.size()!=resources.storage_buffers.size())throw std::runtime_error("Animation buffer interface changed");
   for(auto& r:resources.storage_buffers){bool found=false;for(auto& o:out.storage_buffers)if(original.get_decoration(r.id,spv::DecorationDescriptorSet)==result.get_decoration(o.id,spv::DecorationDescriptorSet)&&original.get_decoration(r.id,spv::DecorationBinding)==result.get_decoration(o.id,spv::DecorationBinding))found=true;if(!found)throw std::runtime_error("Animation binding lost");}
  }tested.insert(hash);
 }
 if(!capturedShadowTested)throw std::runtime_error("Missing captured shadow caster fixture");
 if(tested!=variants)throw std::runtime_error("Missing real shader fixture");
 if(testedScreen!=screen)throw std::runtime_error("Missing screen/texture-space fixture");
 std::cout<<tested.size()<<" real common-camera variants compiled in stereo/mono; animation SSBOs preserved; explicit no-projection exceptions excluded\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
