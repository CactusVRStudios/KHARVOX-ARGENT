#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/ShaderIdentity.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
int main(int argc,char** argv){try{
 if(argc!=2)return 2;unsigned tested=0;
 const char* ids[]={"11a5583e4714e917761179145d72fdd24c2ec731288925eab1a838720a8f1480","18901e5a51c8ac0749487bf64e3c3bbafcf5246d6382092a03ee785c9b4014aa","1868863aee2bf99544d8a0e5c7a135b234e5bff6ace63427983e812f7a0031c4","d3a3027c9b66571f3778e584dc1ec8876b5a1e755104ec5814ecad2d27bcf9cc","cb70782d3982d2ee335f10d1e5028cabe57e309f0f1318c54f6a2d61acab9736","8b8a404a0b3f43cedf05159855ae04c4df9d9c3b6b25f8d8f9e9abb658bec940"};
 std::vector<std::string> fixtures(std::begin(ids),std::end(ids));
 fixtures.push_back("0d2460891f36829205560d9ca98157057e85a8ea38e505122e7f200bf1f9694c");
 fixtures.push_back("c18e5c3c56045ce657aad3cde2af58783aa390f306870d0ffb7193348e557917");
 fixtures.push_back("11e1ccbd4622b3e1bdeb205a1d70bcd66255b435ba7374e02416d01f0f0f5090");
 fixtures.push_back("2627611e3518150318dd34a479ec61af0882ccda986d04a6f33a48da63cb6a3a");
 fixtures.push_back("470b41397e2384050a82161a1530f931922ac6023df2d77e6234a7bc1ce5341d");
 fixtures.push_back("6ce72d56ff87c8cbc9e9c1c11c70d126781900fbe3038d8043e85000a2aaa1c1");
 fixtures.push_back("713bfd0ec45070d29a81180ae46885d9b83c2d965c3448c6ca75433d2e4e802b");
 fixtures.push_back("b3610269557a9d9109cfefd3ca80ea859f4e32286d818137c539b72838ac0d34");
 fixtures.push_back("e1fdb27f579530f02fabd8984d7fc64e6a4d0a6e701b97d997bc056601028ec2");

 fixtures.push_back("7f5cf931a0b5d2d1f87d7943ed9212d41aefc912da7af8b337dbbe27a101fe00");
 fixtures.push_back("12086101ed9cd600e3a5d554137a8f121f9fd4afa5eb6a9805a2badbda2a4043");
 fixtures.push_back("a7f2f54efc28e4921e17e8758dba15c5827a44e90139a229ee4ccbf002d56115");
 fixtures.push_back("514985d5f0a11f3e6e2591d732bcee94c642fa45bb4692024dc495151ac0c3c8");
 fixtures.push_back("9a12f95cebd26553fa4db663388eef4dfad82e3aaeefc935026d9130727f942e");
 fixtures.push_back("92402acde0df7772c286a97b043f48047d6d0c1e6dc025312e92e8b6777cc174");
 fixtures.push_back("29898e0a14fee3632bdce0491bf166c7f8517f8f85776545f3a96b5c01b20b8e");
 fixtures.push_back("43dccf7a0bc3d66e72953edd0877ddc959d2600dc247ca2253456260872536e4");
 fixtures.push_back("5e726a06c43405f55c205d32ff2eb45b008521044f60ded1bc70ff24f5dbd605");
 for(const auto& id:fixtures){
  std::ifstream file(std::filesystem::path(argv[1])/(std::string(id)+".spv"),std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Missing captured fragment");
  std::vector<uint32_t> words(size_t(file.tellg())/4);file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),words.size()*4);
  argent::sfs::ShaderOptions options;options.volumeShader=kharvox::sfs::profileHash(words.data(),uint32_t(words.size()*4));
  if(!argent::sfs::eternalVolumeRule(options.volumeShader).uv)throw std::runtime_error("Captured shader identity changed");
  spirv_cross::Compiler reflect(words);auto resources=reflect.get_shader_resources();std::set<uint32_t> used;
  auto collect=[&](const auto& list){for(auto& r:list)if(reflect.get_decoration(r.id,spv::DecorationDescriptorSet)==0)used.insert(reflect.get_decoration(r.id,spv::DecorationBinding));};
  collect(resources.uniform_buffers);collect(resources.storage_buffers);collect(resources.separate_images);collect(resources.separate_samplers);collect(resources.sampled_images);collect(resources.storage_images);
  while(used.count(options.binding))++options.binding;
  const std::set<uint64_t> transparentAtmosphere{
0x929b3a127e479f1eull,0x9341f6cd594c0e88ull,0x272c4bf87f958132ull,0x676701a8a44af14bull,0xff896435dcf7729eull,0xaf863fdbab5ef66dull,0xe0c75045d413957dull,0x8cd331a52413cad7ull,0x252e77f4f532542full};
  if(transparentAtmosphere.count(options.volumeShader)){
   spirv_cross::CompilerGLSL original(words);auto gl=original.get_common_options();gl.version=460;gl.vulkan_semantics=true;original.set_common_options(gl);
   auto baseline=original.compile(),patched=baseline;const auto rule=argent::sfs::eternalVolumeRule(options.volumeShader);
   const auto anchor=baseline.find(rule.anchor);if(anchor==std::string::npos)throw std::runtime_error("Missing reviewed atmosphere anchor");
   argent::sfs::correctEternalVolume(patched,rule);
   if(patched.substr(0,anchor)!=baseline.substr(0,anchor))throw std::runtime_error("Eye depth/alpha/material logic modified before atmosphere");
   const auto resumed=patched.find(rule.anchor);
   if(resumed==std::string::npos||patched.substr(resumed)!=baseline.substr(anchor))throw std::runtime_error("Original atmosphere/material suffix changed");
   if(patched.find(std::string(rule.uv)+" = argentCenterVolumeUv("+rule.uv+", "+rule.depth+");")!=anchor+std::string("if (argentProjection.diagnostics.w != 3.0) ").size())throw std::runtime_error("Atmosphere conversion missing or misplaced");
  }
  if(options.volumeShader==0xcbed08c426444de3ull){
   auto source=argent::sfs::stereoSource(words,options);
   auto depth=source.find("float _342 = _357;");
   auto convert=source.find("_323 = argentCenterVolumeUv(_323, _342);");
   auto fog=source.find("vec2 _358 = _323 + vec2(0.0);");
   auto ray=source.find("vec3 _477 = _42(_323);");
   if(depth==std::string::npos||convert==std::string::npos||fog==std::string::npos||ray==std::string::npos||!(depth<convert&&convert<fog&&fog<ray))throw std::runtime_error("Cubemap fog and atmospheric ray disagree on camera space");
   if(source.find("vec2(_291, _293)")==std::string::npos||source.find("samplerCube(_302, _304), _309.xyz")==std::string::npos)throw std::runtime_error("Cubemap material sampling changed");
  }
  if(auto ray=argent::sfs::eternalVolumeRule(options.volumeShader);ray.rayCall){
   auto source=argent::sfs::stereoSource(words,options);
   if(source.find(ray.rayCall)!=std::string::npos||source.find(std::string("argentCenterVolumeUv(")+ray.rayUv+", "+ray.depth+")")==std::string::npos)throw std::runtime_error("Executed atmosphere resolve still uses eye-local ray");
   if(reflect.get_execution_model()==spv::ExecutionModelGLCompute)for(int eye:{0,1}){auto fixed=options;fixed.indirectEye=eye;if(argent::sfs::compileStereoShader(words,fixed).empty())throw std::runtime_error("Resolve fixed-eye compile failed");}
  }
  if(options.volumeShader==0xa14db8d1c3a2ca43ull){
   auto source=argent::sfs::stereoSource(words,options);
   if(source.find("_711 * (argentSkyUv.x - 0.5)")==std::string::npos||source.find("_623._m0.w * argentSkyScale.y")==std::string::npos||source.find("_623._m0.w / _708 * argentSkyScale.x")==std::string::npos||source.find("_711 * (_618.x - 0.5)")!=std::string::npos)throw std::runtime_error("Flat sky background still uses eye UV or native-only gradients");
   for(int eye:{0,1}){auto fixed=options;fixed.indirectEye=eye;if(argent::sfs::compileStereoShader(words,fixed).empty())throw std::runtime_error("Fixed-eye sky shader failed compilation");}
   if(source.find("_77(argentAtmosphereUv)")==std::string::npos || source.find("argentCenterVolumeUv(_629, _1616)")==std::string::npos || source.find("vec3 _1730 = _77(_629);")!=std::string::npos)
    throw std::runtime_error("Atmosphere and sun still reconstruct a center ray from eye UV");
   if(source.find("vec2 _1621 = _629 + _1567;")==std::string::npos)
    throw std::runtime_error("Original fog input unexpectedly changed");
  }
  if(argent::sfs::eternalVolumeRule(options.volumeShader).waterGeometry){
   auto source=argent::sfs::stereoSource(words,options);
   const auto sampled=source.find("float _367 = textureLod(");
   const auto undo=source.find("vec2 argentWaterCenterUv = argentCenterVolumeUv(");
   const auto reconstruct=source.find("vec4 _421 = _48(");
   if(sampled==std::string::npos||undo==std::string::npos||reconstruct==std::string::npos||!(sampled<undo&&undo<reconstruct))throw std::runtime_error("Water depth must be sampled in eye space before world reconstruction");
   const auto current=source.find("vec3 _1067 = _421.xyz;");
   const auto historyGate=source.find("if (argentProjection.diagnostics.y <= 0.5 || argentProjection.diagnostics.w == 3.0)",current);
   const auto history=source.find("vec3 _953 = _447;");
   if(current==std::string::npos||historyGate==std::string::npos||history==std::string::npos||!(current<historyGate&&historyGate<history))throw std::runtime_error("Water center-camera history remains active in stereo");
   if(source.find("imageStore(")==std::string::npos||source.find("imageStore(",source.find("imageStore(")+1)!=std::string::npos)throw std::runtime_error("Water geometry output count changed");
  }
  if(argent::sfs::eternalVolumeRule(options.volumeShader).material){
   const auto original=argent::sfs::stereoSource(words,{}),patched=argent::sfs::stereoSource(words,options);
   auto count=[](const std::string& s,const std::string& token){unsigned n=0;size_t pos=0;while((pos=s.find(token,pos))!=std::string::npos){++n;pos+=token.size();}return n;};
   if(count(patched," = argentCenterVolumeUv(")!=3||count(patched,"_5002 = argentEyeTextureProjection(_5002);")!=1)throw std::runtime_error("Incomplete Vk3D material correction");
   for(const auto* token:{"discard;","nonuniformEXT(","gl_FragCoord","imageStore("})if(count(original,token)!=count(patched,token))throw std::runtime_error("Material semantics changed outside the reviewed corrections");
  }
  for(bool mono:{false,true}){options.monoView=mono;auto source=argent::sfs::stereoSource(words,options);if(source.find("argentCenterVolumeUv(")==std::string::npos||source.find(argent::sfs::eternalVolumeRule(options.volumeShader).anchor)==std::string::npos)throw std::runtime_error("Lost volume correction");if(argent::sfs::compileStereoShader(words,options).empty())throw std::runtime_error("Empty compilation");}
  if(reflect.get_execution_model()==spv::ExecutionModelGLCompute){
   options.monoView=false;
   for(int eye:{-1,0,1}){options.indirectEye=eye;auto source=argent::sfs::stereoSource(words,options);if(source.find("clipFromCenter[khSfsEye]")==std::string::npos||source.find("gl_ViewIndex")!=std::string::npos)throw std::runtime_error("Compute fog selected the wrong eye");if(argent::sfs::compileStereoShader(words,options).empty())throw std::runtime_error("Compute fog compilation failed");}
   options.indirectEye=-1;options.computeStereo=false;if(argent::sfs::compileStereoShader(words,options).empty())throw std::runtime_error("Shared compute fog compilation failed");
  }
  auto rule=argent::sfs::eternalVolumeRule(options.volumeShader);std::string changed="void main() {}";bool rejected=false;try{argent::sfs::correctEternalVolume(changed,rule);}catch(const std::runtime_error&){rejected=true;}if(!rejected)throw std::runtime_error("Unknown structure accepted");++tested;
 }
 // Captured scene-depth variants covered by Vk3D's generic clip-Y rule.
 for(const auto* id:{"ce003a20b18c10bd41af2c362c01934e7f48f154c5708ce2d828480bb657832b","2a8ac25e7f0e8a4b96a59a9f3f8de5ec61909bbc57b39d54d7b83a75ccdc5a7b"}){
  std::ifstream file(std::filesystem::path(argv[1])/(std::string(id)+".spv"),std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Missing scene depth fixture");
  std::vector<uint32_t> words(size_t(file.tellg())/4);file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),words.size()*4);
  spirv_cross::Compiler reflect(words);const auto resources=reflect.get_shader_resources();std::set<uint32_t> used;
  auto collect=[&](const auto& list){for(const auto& r:list)if(reflect.get_decoration(r.id,spv::DecorationDescriptorSet)==0)used.insert(reflect.get_decoration(r.id,spv::DecorationBinding));};
  collect(resources.uniform_buffers);collect(resources.storage_buffers);collect(resources.sampled_images);collect(resources.separate_images);collect(resources.separate_samplers);
  argent::sfs::ShaderOptions options;options.project=true;while(used.count(options.binding))++options.binding;
  for(bool mono:{false,true}){options.monoView=mono;if(argent::sfs::compileStereoShader(words,options).empty())throw std::runtime_error("Scene depth projection compilation failed");}
 }
 // Shared radial-distortion mask reads its writable-declared SSBO but never
 // changes it. Both eye layers must receive the same neutral/active mask.
 const auto maskPath=std::filesystem::path(argv[1])/"dbf9b67018ba42e2b52affd0d319bdb10f7317e5d58b96af983250c7c2135be7.spv";
 std::ifstream maskFile(maskPath,std::ios::binary|std::ios::ate);if(!maskFile)throw std::runtime_error("Missing distortion fixture");
 std::vector<uint32_t> mask(size_t(maskFile.tellg())/4);maskFile.seekg(0);maskFile.read(reinterpret_cast<char*>(mask.data()),mask.size()*4);
 if(kharvox::sfs::profileHash(mask.data(),uint32_t(mask.size()*4))!=0x866fd44ba9bfea56ull)throw std::runtime_error("Distortion identity changed");
 argent::sfs::ShaderOptions shared;shared.computeStereo=false;shared.broadcastStorageImages=true;
 auto source=argent::sfs::stereoSource(mask,shared);auto first=source.find("imageStore(");auto second=source.find("imageStore(",first+1);
 if(first==std::string::npos||second==std::string::npos||source.find("imageStore(",second+1)!=std::string::npos||source.find(".z > 1")==std::string::npos)throw std::runtime_error("Distortion image not broadcast to both layers");
 if(argent::sfs::compileStereoShader(mask,shared).empty())throw std::runtime_error("Distortion compilation failed");
 std::cout<<tested<<" exact volume shaders (fragment and compute) and shared distortion mask compile correctly\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

