#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/ShaderIdentity.h"
#include "../src/sfs/EternalProfile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <tuple>
using namespace argent::sfs;
using Interface=std::set<std::tuple<uint32_t,uint32_t,unsigned,unsigned,unsigned>>;
static Interface outputs(spirv_cross::Compiler& c){
 Interface result;
 for(const auto& r:c.get_shader_resources().stage_outputs){const auto& t=c.get_type(r.type_id);
  result.emplace(c.get_decoration(r.id,spv::DecorationLocation),c.get_decoration(r.id,spv::DecorationComponent),unsigned(t.basetype),t.vecsize,t.columns);
 }return result;
}
int main(int argc,char** argv){try{
 if(argc!=2)return 2;
 const std::set<uint64_t> selected{
#include "../src/sfs/EternalAmdWorldVariants.inc"
 };
 // Captured shadow casters and fullscreen copies must remain native.
 const std::set<uint64_t> excluded{0x463b036a702fe2acull,0x55efeba54edf76dull,0xa9b23681b6806a97ull,
  0x7b602d834ed07263ull,0xf136bdb90fe92ef6ull,0xae599ad70e8b9b0full,0x15b2875ebe8bc8ebull,0xcdb5b2cacc93a58full,
  0x70903b1635c3f665ull,0xe3881ca9ca28b278ull,0xaef5cc288c7813f2ull};
 const std::set<uint64_t> volumeCases{0x3b62f61430a414f2ull,0x7349c1f96fbc381aull,0x2f28ea0af971c44bull,0xfdbe852191fcd430ull,0xbbe3e62a9b6bd445ull,0x94024ca5d565a3edull,0x17be3ba41ca76733ull,0x66b911a38af36c60ull,0xa3a5694076f2a831ull,0x971c804446f72158ull,0x676701a8a44af14bull,0x252e77f4f532542full,0x90c822113652399bull,0x64b6c72f0500b6e6ull,0x65caa09a41470219ull,0x9130f1d8f8a3d889ull,0x83cd64dfce73a7b9ull};
 const auto profile=eternalProfile();std::set<uint64_t> tested,untouched,volumesTested;bool resolveTested=false;
 for(const auto& file:std::filesystem::directory_iterator(argv[1])){
  if(file.path().extension()!=L".spv")continue;
  std::ifstream in(file.path(),std::ios::binary|std::ios::ate);const auto size=in.tellg();
  if(size<20||size_t(size)%4)throw std::runtime_error("Invalid fixture");
  std::vector<uint32_t> words(size_t(size)/4);in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),size);
  const auto hash=kharvox::sfs::profileHash(words.data(),uint32_t(words.size()*4));
  if(volumeCases.count(hash)){
   spirv_cross::Compiler original(words);const bool compute=original.get_execution_model()==spv::ExecutionModelGLCompute;
   ShaderOptions options;options.volumeShader=hash;options.vk3dShader=eternalVk3dRule(hash)?hash:0;
   auto res=original.get_shader_resources();
   auto collect=[&](const auto& list){for(const auto& r:list)if(original.get_decoration(r.id,spv::DecorationDescriptorSet)==0)options.binding=std::max(options.binding,original.get_decoration(r.id,spv::DecorationBinding)+1);};
   collect(res.uniform_buffers);collect(res.storage_buffers);collect(res.storage_images);collect(res.separate_images);collect(res.separate_samplers);collect(res.sampled_images);
   if(auto ray=eternalVolumeRule(hash);ray.rayCall){auto source=stereoSource(words,options);if(source.find(ray.rayCall)!=std::string::npos||source.find(std::string("argentCenterVolumeUv(")+ray.rayUv+", "+ray.depth+")")==std::string::npos)throw std::runtime_error("AMD resolve ray remains eye-local");}
   if(hash==0x971c804446f72158ull){
    auto source=stereoSource(words,options);
    if(source.find("_701 * (argentSkyUv.x - 0.5)")==std::string::npos||source.find("_613._m0.w * argentSkyScale.y")==std::string::npos||source.find("_613._m0.w / _698 * argentSkyScale.x")==std::string::npos||source.find("_701 * (_608.x - 0.5)")!=std::string::npos)throw std::runtime_error("AMD sky background UV/gradients uncorrected");
    if(source.find("argentCenterVolumeUv(_619, _1831)")==std::string::npos||source.find("vec3 _1945 = _77(argentAtmosphereUv);")==std::string::npos||source.find("vec3 _1945 = _77(_619);")!=std::string::npos)throw std::runtime_error("AMD sun/atmosphere ray uncorrected");
   }
   for(bool mono:{false,true})for(int eye:{-1,0,1}){
    if(!compute&&eye!=-1)continue;
    options.monoView=mono;options.indirectEye=eye;
    auto source=stereoSource(words,options);auto rule=eternalVolumeRule(hash);
    if(source.find(std::string(rule.uv)+" = argentCenterVolumeUv("+rule.uv+", "+rule.depth+")")==std::string::npos)throw std::runtime_error("AMD volume UV correction missing");
    if(compileStereoShader(words,options).empty())throw std::runtime_error("AMD volume failed compilation");
   }
   volumesTested.insert(hash);
  }
  if(hash==0x9c2423225ead23c9ull){
   if(!profile.stereoComputeShaders.count(hash)||profile.broadcastComputeShaders.count(hash))throw std::runtime_error("AMD world resolve must execute per eye, not broadcast");
   spirv_cross::Compiler original(words);const auto resources=original.get_shader_resources();
   if(original.get_execution_model()!=spv::ExecutionModelGLCompute||resources.storage_buffers.size()!=1)throw std::runtime_error("Resolve contract changed");
   const auto& buffer=resources.storage_buffers.front();
   if(original.get_decoration(buffer.id,spv::DecorationDescriptorSet)!=1||original.get_decoration(buffer.id,spv::DecorationBinding)!=9)throw std::runtime_error("Resolve exposure binding changed");
   for(int eye:{-1,0,1}){
    ShaderOptions options;options.computeStereo=true;options.indirectEye=eye;
    const auto source=stereoSource(words,options);
    if(source.find("imageStore(")==std::string::npos||source.find("int(khSfsEye)")==std::string::npos||source.find("khSfsEye="+(eye<0?std::string("gl_WorkGroupID.z"):std::to_string(eye)+"u"))==std::string::npos)throw std::runtime_error("Resolve eye routing missing");
    auto compiled=compileStereoShader(words,options);spirv_cross::Compiler patched(compiled);
    if(patched.get_shader_resources().storage_buffers.size()!=1)throw std::runtime_error("Exposure input lost");
   }
   resolveTested=true;
  }
  if(excluded.count(hash)){
   if(profile.projectionShaders.count(hash))throw std::runtime_error("Shadow/copy receives eye projection");
   if(stereoSource(words).find("argentProjection")!=std::string::npos)throw std::runtime_error("Native projection changed");
   untouched.insert(hash);
  }
  if(!selected.count(hash))continue;
  if(!profile.projectionShaders.count(hash))throw std::runtime_error("AMD world profile not enabled");
  spirv_cross::Compiler original(words);if(original.get_execution_model()!=spv::ExecutionModelVertex)throw std::runtime_error("Wrong stage");
  ShaderOptions options;options.project=true;const auto resources=original.get_shader_resources();
  auto collect=[&](const auto& rows){for(const auto& r:rows)if(original.get_decoration(r.id,spv::DecorationDescriptorSet)==0)
   options.binding=std::max(options.binding,original.get_decoration(r.id,spv::DecorationBinding)+1);};
  collect(resources.uniform_buffers);collect(resources.storage_buffers);collect(resources.separate_images);collect(resources.separate_samplers);
  for(bool mono:{false,true}){
   options.monoView=mono;const auto source=stereoSource(words,options);
   const auto token=mono?"argentProjection.clipFromCenter[0]":"argentProjection.clipFromCenter[gl_ViewIndex]";
   if(source.find(token)==std::string::npos)throw std::runtime_error("Per-eye projection missing");
   const auto compiled=compileStereoShader(words,options);spirv_cross::Compiler patched(compiled);
   if(outputs(original)!=outputs(patched))throw std::runtime_error("Packed AMD vertex/fragment interface changed");
   if(patched.get_shader_resources().storage_buffers.size()!=resources.storage_buffers.size())throw std::runtime_error("Skinning buffer lost");
  }
  tested.insert(hash);
 }
 if(tested!=selected||untouched!=excluded||!resolveTested||volumesTested!=volumeCases)throw std::runtime_error("Missing real AMD fixtures");
 std::cout<<tested.size()<<" AMD scene vertices compiled for stereo/mono; packed outputs preserved; "<<untouched.size()<<" shadow/copy exclusions verified\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
