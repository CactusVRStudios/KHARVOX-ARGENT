#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/ShaderIdentity.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <map>
using namespace argent::sfs;
// Check the sampling operand, not just the array index or loaded image.
// GLSL sampler constructors do not propagate nonuniformEXT automatically.
static void nonuniformSampleOperands(const std::vector<uint32_t>& words,unsigned expected){
 std::set<uint32_t> decorated,combined;
 std::map<uint32_t,uint32_t> copies;
 for(size_t p=5;p<words.size();p+=words[p]>>16){
  const auto op=spv::Op(words[p]&0xffffu);
  if(op==spv::OpDecorate&&words[p+2]==spv::DecorationNonUniform)decorated.insert(words[p+1]);
 }
 for(size_t p=5;p<words.size();p+=words[p]>>16){
  const auto op=spv::Op(words[p]&0xffffu);
  if(op==spv::OpSampledImage&&(decorated.count(words[p+3])||decorated.count(words[p+4])))combined.insert(words[p+2]);
  if(op==spv::OpCopyObject)copies[words[p+2]]=words[p+3];
 }
 unsigned accesses=0;
 for(size_t p=5;p<words.size();p+=words[p]>>16){
  const auto op=spv::Op(words[p]&0xffffu);
  if(op!=spv::OpImageSampleExplicitLod&&op!=spv::OpImageSampleImplicitLod)continue;
  auto operand=words[p+3],origin=operand;
  while(copies.count(origin))origin=copies.at(origin);
  if(!combined.count(origin))continue;
  ++accesses;
  if(!decorated.count(operand))throw std::runtime_error("Sample operand lost NonUniform after sampler constructor");
 }
 if(expected==~0u ? accesses==0 : accesses!=expected)throw std::runtime_error("Unexpected number of nonuniform material samples");
}
static void waterSampleOperands(const std::vector<uint32_t>& words){
 nonuniformSampleOperands(words,4);
 unsigned writes=0;
 for(size_t p=5;p<words.size();p+=words[p]>>16){
  const auto op=spv::Op(words[p]&0xffffu);
  if(op==spv::OpImageQuerySize||op==spv::OpImageQuerySizeLod)throw std::runtime_error("Water still reads descriptor dimensions");
  if(op==spv::OpImageWrite)++writes;
 }
 if(writes!=6)throw std::runtime_error("Water output write removed");
}
static std::set<std::pair<uint32_t,uint32_t>> buffers(spirv_cross::Compiler& c){
 std::set<std::pair<uint32_t,uint32_t>> result;
 for(const auto& r:c.get_shader_resources().storage_buffers)result.emplace(c.get_decoration(r.id,spv::DecorationDescriptorSet),c.get_decoration(r.id,spv::DecorationBinding));
 return result;
}
static std::set<std::pair<uint32_t,uint32_t>> sampledBindings(spirv_cross::Compiler& c){
 std::set<std::pair<uint32_t,uint32_t>> result;
 const auto resources=c.get_shader_resources();
 for(const auto& r:resources.separate_images)result.emplace(c.get_decoration(r.id,spv::DecorationDescriptorSet),c.get_decoration(r.id,spv::DecorationBinding));
 for(const auto& r:resources.sampled_images)result.emplace(c.get_decoration(r.id,spv::DecorationDescriptorSet),c.get_decoration(r.id,spv::DecorationBinding));
 return result;
}
int main(int argc,char** argv){try{
 if(argc<2)return 2;std::set<uint64_t> tested,uiTested;bool waterVertexTested=false;
 const std::set<uint64_t> uiHashes{0xdc2d10822f8eda88ull,0xf37a280cc1dc1a83ull,0x0749c071d7d1fdf5ull,0x4f50b1f4caf20882ull,0x475b91f7adce5776ull,0xc8d657a2321cc9eeull,0x32fb81bae310c07dull};unsigned decorated=0;
 std::vector<std::filesystem::directory_entry> files;
 for(int arg=1;arg<argc;++arg)for(const auto& file:std::filesystem::directory_iterator(argv[arg]))files.push_back(file);
 for(const auto& file:files){
  if(file.path().extension()!=L".spv")continue;
  std::ifstream in(file.path(),std::ios::binary|std::ios::ate);std::vector<uint32_t> words(size_t(in.tellg())/4);in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),words.size()*4);
  const auto hash=kharvox::sfs::profileHash(words.data(),uint32_t(words.size()*4));if(uiHashes.count(hash)){
   ShaderOptions ui;ui.project=true;ui.screenSpaceUi=true;ui.uiShader=hash;ui.binding=63;
   auto source=stereoSource(words,ui);
   if(source.find("gl_Position.w <= 8.0")!=std::string::npos)throw std::runtime_error("Near hand HUD still classified by depth");
   if(hash!=0xf37a280cc1dc1a83ull&&source.find(" > 0.0)")==std::string::npos)throw std::runtime_error("Native world flag missing");
   for(bool mono:{false,true}){ui.monoView=mono;compileStereoShader(words,ui);}
   uiTested.insert(hash);
  }
  if(hash==0xbb9feb4c28ae79b4ull){
   spirv_cross::Compiler originalWater(words);
   for(bool mono:{false,true}){
    ShaderOptions water;water.project=true;water.binding=10;water.monoView=mono;
    const auto source=stereoSource(words,water);
    const auto wrapper=source.rfind("void main()");
    const auto guard=source.find("if (floatBitsToUint(gl_Position.w) == 0xff800000u) return;",wrapper);
    const auto matrix=source.find("vec4 eyeClip",wrapper);
    if(wrapper==std::string::npos||guard==std::string::npos||matrix==std::string::npos||guard>matrix)
     throw std::runtime_error("Water rejection marker reaches stereo matrix multiplication");
    const auto compiled=compileStereoShader(words,water);spirv_cross::Compiler patchedWater(compiled);
    if(sampledBindings(originalWater)!=sampledBindings(patchedWater))throw std::runtime_error("Water vertex resource interface changed");
   }
   waterVertexTested=true;
  }
  const auto* rule=eternalVk3dRule(hash);if(!rule||tested.count(hash))continue;
  spirv_cross::Compiler original(words);const auto resources=original.get_shader_resources();const bool compute=original.get_execution_model()==spv::ExecutionModelGLCompute;
  ShaderOptions options;options.vk3dShader=hash;options.volumeShader=hash;options.lightGridShader=compute?0:hash;
  bool writes=false,shared=false;
  for(const auto& r:resources.storage_images){auto t=original.get_type(r.type_id);if(t.image.dim==spv::Dim2D&&!t.image.arrayed&&!original.has_decoration(r.id,spv::DecorationNonWritable))writes=true;}
  for(const auto& r:resources.storage_buffers)if(!original.has_decoration(r.id,spv::DecorationNonWritable)&&!original.get_buffer_block_flags(r.id).get(spv::DecorationNonWritable))shared=true;
  options.computeStereo=writes&&!shared;
  std::set<uint32_t> used;auto collect=[&](const auto& list){for(const auto& r:list)if(original.get_decoration(r.id,spv::DecorationDescriptorSet)==0)used.insert(original.get_decoration(r.id,spv::DecorationBinding));};
  collect(resources.uniform_buffers);collect(resources.storage_buffers);collect(resources.separate_images);collect(resources.separate_samplers);collect(resources.storage_images);collect(resources.sampled_images);
  while(used.count(options.binding))++options.binding;
  for(bool mono:{false,true}){
   options.monoView=mono;const auto source=stereoSource(words,options);const auto patched=compileStereoShader(words,options);spirv_cross::Compiler result(patched);
   if(rule->preserveNonuniform)nonuniformSampleOperands(patched,~0u);
   if(buffers(original)!=buffers(result))throw std::runtime_error("Shared buffer interface changed");
   if(sampledBindings(original)!=sampledBindings(result))throw std::runtime_error("Material texture descriptor bindings changed");
   if(hash==0x24abb0e76a065289ull){
    const auto remap=source.find("vec2 argentWaterGridPixel = clamp(");
    const auto list=source.find("uint _1610 = ");
    if(remap==std::string::npos||list==std::string::npos||remap>=list||
       source.find("argentCenterVolumeUv(_1547._m17.xy / _641._m2.xy, _1589)")==std::string::npos)
     throw std::runtime_error("Water lists must use full viewport and linear depth before lookup");
    // Screen-space G-buffer/refraction operations must retain their eye UVs;
    // the coarse-list correction cannot alter the shared material coordinate.
    if(source.find("_1547._m17 = vec4(_1535, _854, 1.0);")==std::string::npos||
       source.find("vec2 _2620 = vec2(_2580.x * _641._m40, _2580.y);")==std::string::npos||
       source.find("_1580 = uint(argentWaterGridPixel.x) / 256u;")==std::string::npos||
       source.find("_1585 = uint(argentWaterGridPixel.y) / 256u;")==std::string::npos)
     throw std::runtime_error("Water grid correction changed eye sampling or missed an axis");
    waterSampleOperands(patched);
    for(int eye:{0,1}){auto fixed=options;fixed.indirectEye=eye;waterSampleOperands(compileStereoShader(words,fixed));}
    // Keep all four bindless material samples, without injected size queries.
    unsigned materialSamples=0;
    std::istringstream lines(source);std::string line;
    while(std::getline(lines,line))if(line.find("_1924[nonuniformEXT(")!=std::string::npos){
     if(line.find("textureSize(")!=std::string::npos)throw std::runtime_error("Water bindless sampling still queries descriptor dimensions");
     if(line.find("textureGrad(")!=std::string::npos||line.find("textureLod(")!=std::string::npos)++materialSamples;
    }
    if(materialSamples!=4)throw std::runtime_error("Water material sample removed");
    // This fixture uses only filtered samples, no integer texel fetches.
    // Two-layer water outputs use the known eye directly, with no injected
    // descriptor metadata access (the r159 GPU fault mapped to that query).
    if(source.find("textureSize(")!=std::string::npos||source.find("imageSize(")!=std::string::npos)
     throw std::runtime_error("Water sampled/storage layer policy mismatch");
   }
   if(hash==0x49770da99bf758d5ull){
    if(source.find("clamp(_131 * 15.0, 0.0, 15.0)")==std::string::npos ||
       source.find("int _166 = min(_162 + 4, 60);")==std::string::npos)
     throw std::runtime_error("Portal radial kernel can read past its shared table");
    // Exhaust all interpolation intervals, including the terminal row. The
    // original successor for row 15 is index 64, outside the 64-word table.
    for(int row=0;row<=15;++row)for(int component=0;component<4;++component){
     const int first=row*4+component, next=std::min(row*4+4,60)+component;
     if(first>=64||next>=64)throw std::runtime_error("Portal kernel boundary invalid");
     if(row<15 && next!=(row+1)*4+component)throw std::runtime_error("Interior kernel changed");
     if(row==15 && next!=first)throw std::runtime_error("Terminal kernel does not clamp");
    }
   }
   if(hash==0xd300c0135fbca8b0ull){
    nonuniformSampleOperands(patched,23);
    if(source.find("textureSize(")!=std::string::npos)throw std::runtime_error("World material still queries descriptor dimensions");
    auto samples=[](const std::vector<uint32_t>& w){unsigned n=0;for(size_t p=5;p<w.size();p+=w[p]>>16){auto op=spv::Op(w[p]&0xffffu);if(op>=spv::OpImageSampleImplicitLod&&op<=spv::OpImageDrefGather)++n;}return n;};
    if(samples(words)!=samples(patched))throw std::runtime_error("World material texture operation removed");
   }
   if(compute){for(unsigned axis=0;axis<3;++axis)if(original.get_execution_mode_argument(spv::ExecutionModeLocalSize,axis)!=result.get_execution_mode_argument(spv::ExecutionModeLocalSize,axis))throw std::runtime_error("Local workgroup size changed");}
   if(shared&&source.find("khSfsGlobalInvocationID")!=std::string::npos)throw std::runtime_error("Shared buffer allocator executes twice");
   if(hash==0x6d29aabc70f827ecull || hash==0xdbe8cca95d29594full){
    const bool coarse=hash==0x6d29aabc70f827ecull;
    const std::string geometry=coarse?"_226 < 1000000015047466219876688855040.0":"_282 < 1000000015047466219876688855040.0";
    const std::string depth=coarse?"_997":"_921";
    for(const auto& gate:{geometry,depth}){
     if(source.find("if ("+gate+")")==std::string::npos || source.find("|| ("+gate+")")!=std::string::npos)
      throw std::runtime_error("Decal geometry/depth rejection bypassed; native 63-ID lists can overflow");
    }
    if(!coarse && (source.find("if (argentProjection.diagnostics.x > 0.5) _888 = 0.0;")==std::string::npos || source.find("if (argentProjection.diagnostics.x > 0.5) _908 = 1.0;")==std::string::npos))
     throw std::runtime_error("Native-center decal list incorrectly depends on an eye's HiZ image");
   }
   if(rule->worldUniform&&source.find("argentProjection.diagnostics.y > 0.5")==std::string::npos&&source.find("argentProjection.diagnostics.x > 0.5")==std::string::npos)throw std::runtime_error("World-only workaround leaks into quad path");
   if(hash==0x2231c6332b953a73ull || hash==0x753a805ff3972c88ull){
    const bool coarse=hash==0x2231c6332b953a73ull;
    const std::string geometry=coarse?"_226 < 1000000015047466219876688855040.0":"_325 < 1000000015047466219876688855040.0";
    const std::string depth=coarse?"_1005":"_975";
    for(const auto& gate:{geometry,depth}){
     if(source.find("if (((argentProjection.diagnostics.y > 0.5) && argentProjection.diagnostics.w == 5.0) || ("+gate+"))")==std::string::npos)
      throw std::runtime_error("Native light geometry bypass must be restricted to explicit A/B mode 5");
    }
    if(!coarse && (source.find("if (argentProjection.diagnostics.x > 0.5) _942 = 0.0;")==std::string::npos || source.find("if (argentProjection.diagnostics.x > 0.5) _962 = 1.0;")==std::string::npos))
     throw std::runtime_error("Native light candidates must not depend on eye-space HiZ");
   }
   bool needsNonuniform=false;for(const auto& e:rule->edits)if(e.all)needsNonuniform=true;
   if(needsNonuniform){bool found=false;for(size_t i=5;i<patched.size();){auto count=patched[i]>>16;if(!count)throw std::runtime_error("Invalid instruction");if((patched[i]&65535)==spv::OpDecorate&&count>=3&&patched[i+2]==spv::DecorationNonUniform)found=true;i+=count;}if(!found)throw std::runtime_error("Nonuniform decoration lost during compilation");++decorated;}
  }
  auto bad=std::string("void main(){}");bool rejected=false;try{applyEternalVk3d(bad,*rule);}catch(const std::runtime_error&){rejected=true;}if(!rejected)throw std::runtime_error("Unknown shader structure accepted");
  tested.insert(hash);std::cout<<kharvox::sfs::shaderKey(hash)<<" verified\n";
 }
 if(uiTested!=uiHashes)throw std::runtime_error("Native UI fixture missing");
 if(!waterVertexTested)throw std::runtime_error("Captured water vertex fixture missing");
 if(tested.size()!=eternalVk3dRules().size())throw std::runtime_error("A profile rule has no real captured fixture");
 std::cout<<tested.size()<<" exact Vk3D modules compiled, "<<decorated<<" decorated variants verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
