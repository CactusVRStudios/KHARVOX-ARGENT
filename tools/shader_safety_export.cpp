// Offline corpus audit. Never creates Vulkan objects or installs replacements.
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/ShaderIdentity.h"
#include "../src/sfs/EternalProfile.h"
#include "../src/sfs/PortableUiIdentity.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace argent::sfs;
int main(int argc,char** argv){try{
 if(argc!=3)return 2;
 std::ifstream in(argv[1],std::ios::binary|std::ios::ate);
 if(!in||in.tellg()<20||size_t(in.tellg())%4)throw std::runtime_error("Invalid input");
 std::vector<uint32_t> words(size_t(in.tellg())/4);in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),words.size()*4);
 std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
 spirv_cross::Compiler inspect(words);const auto stage=inspect.get_execution_model();
 const auto hash=kharvox::sfs::profileHash(words.data(),uint32_t(words.size()*4));
 std::ofstream meta(dir/"meta.txt");meta<<kharvox::sfs::shaderKey(hash)<<'\n'<<unsigned(stage)<<'\n';
 if(stage!=spv::ExecutionModelVertex&&stage!=spv::ExecutionModelFragment&&stage!=spv::ExecutionModelGLCompute){meta<<"unsupported\n";return 0;}
 try{spirv_cross::CompilerGLSL original(words);auto gl=original.get_common_options();gl.version=460;gl.vulkan_semantics=true;original.set_common_options(gl);std::ofstream(dir/"original.glsl")<<original.compile();}
 catch(const std::exception& e){std::ofstream(dir/"original-error.txt")<<e.what();}
 const auto profile=eternalProfile();const auto resources=inspect.get_shader_resources();ShaderOptions options;
 auto collect=[&](const auto& rows){for(const auto& r:rows)if(inspect.get_decoration(r.id,spv::DecorationDescriptorSet)==0)options.binding=std::max(options.binding,inspect.get_decoration(r.id,spv::DecorationBinding)+1);};
 collect(resources.uniform_buffers);collect(resources.storage_buffers);collect(resources.separate_images);collect(resources.separate_samplers);collect(resources.sampled_images);collect(resources.storage_images);collect(resources.subpass_inputs);
 const bool compute=stage==spv::ExecutionModelGLCompute;bool writes=false,shared=false;
 for(const auto& r:resources.storage_images){const auto t=inspect.get_type(r.type_id);if(t.image.dim==spv::Dim2D&&!t.image.arrayed&&!inspect.has_decoration(r.id,spv::DecorationNonWritable))writes=true;}
 for(const auto& r:resources.storage_buffers)if(!inspect.has_decoration(r.id,spv::DecorationNonWritable)&&!inspect.get_buffer_block_flags(r.id).get(spv::DecorationNonWritable))shared=true;
 options.computeStereo=compute&&(profile.stereoComputeShaders.count(hash)||(writes&&!shared));
 options.broadcastStorageImages=profile.broadcastComputeShaders.count(hash)!=0;
 if(eternalVk3dRule(hash))options.vk3dShader=hash;
 if(eternalVolumeRule(hash).uv)options.volumeShader=hash;
 if(stage==spv::ExecutionModelFragment&&eternalLightGridRule(hash).pixels)options.lightGridShader=hash;
 options.uiShader=stage==spv::ExecutionModelVertex?(profile.screenUiShaders.count(hash)?hash:portableUiProfile(words)):0;
 options.screenSpaceUi=options.uiShader!=0;
 options.project=stage==spv::ExecutionModelVertex&&(profile.projectionShaders.count(hash)||options.screenSpaceUi);
 std::ofstream(dir/"policy.txt")<<"project "<<options.project<<"\nui "<<options.uiShader<<"\ncomputeStereo "<<options.computeStereo<<"\nimageWrites "<<writes<<"\nsharedWrites "<<shared<<"\nbroadcast "<<options.broadcastStorageImages<<"\nvolume "<<options.volumeShader<<"\ngrid "<<options.lightGridShader<<"\nvk3d "<<options.vk3dShader<<'\n';
 int failures=0;
 auto emit=[&](std::string name,ShaderOptions o){try{
  const auto source=stereoSource(words,o);std::ofstream(dir/(name+".glsl"))<<source;
  const auto spv=compileStereoShader(words,o);std::ofstream binary(dir/(name+".spv"),std::ios::binary);binary.write(reinterpret_cast<const char*>(spv.data()),spv.size()*4);
  meta<<name<<" ok\n";
 }catch(const std::exception& e){++failures;meta<<name<<" failed\n";std::ofstream(dir/(name+"-error.txt"))<<e.what();}};
 emit("stereo",options);auto mono=options;mono.monoView=true;emit("mono",mono);
 if(options.computeStereo)for(int eye=0;eye<2;++eye){auto fixed=options;fixed.indirectEye=eye;emit("eye"+std::to_string(eye),fixed);}
 if(options.project){options.project=false;emit("unprojected",options);}
 return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
