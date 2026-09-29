#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/ShaderIdentity.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstring>
int main(int argc,char** argv){try{
    if(argc==4&&(std::strcmp(argv[1],"--eternal-compute-source")==0||std::strcmp(argv[1],"--eternal-fragment-source")==0)){
        std::ifstream input(argv[2],std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Missing input");
        const auto length=input.tellg();if(length<20||size_t(length)%4)throw std::runtime_error("Invalid SPIR-V");
        std::vector<uint32_t> words(size_t(length)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),length);
        spirv_cross::Compiler inspect(words);
        const bool fragment=std::strcmp(argv[1],"--eternal-fragment-source")==0;
        if(inspect.get_execution_model()!=(fragment?spv::ExecutionModelFragment:spv::ExecutionModelGLCompute))throw std::runtime_error("Unexpected shader stage");
        const auto hash=kharvox::sfs::profileHash(words.data(),uint32_t(words.size()*4));
        argent::sfs::ShaderOptions options;const auto resources=inspect.get_shader_resources();
        auto collect=[&](const auto& rows){for(const auto& row:rows)if(inspect.get_decoration(row.id,spv::DecorationDescriptorSet)==0)options.binding=std::max(options.binding,inspect.get_decoration(row.id,spv::DecorationBinding)+1);};
        collect(resources.uniform_buffers);collect(resources.storage_buffers);collect(resources.separate_images);collect(resources.separate_samplers);collect(resources.storage_images);
        bool writes=false,sharedWrites=false;
        for(const auto& r:resources.storage_images){auto t=inspect.get_type(r.type_id);if(t.image.dim==spv::Dim2D&&!t.image.arrayed&&!inspect.has_decoration(r.id,spv::DecorationNonWritable))writes=true;}
        for(const auto& r:resources.storage_buffers)if(!inspect.has_decoration(r.id,spv::DecorationNonWritable)&&!inspect.get_buffer_block_flags(r.id).get(spv::DecorationNonWritable))sharedWrites=true;
        options.computeStereo=!fragment&&writes&&!sharedWrites;
        if(argent::sfs::eternalVolumeRule(hash).uv)options.volumeShader=hash;
        if(argent::sfs::eternalVk3dRule(hash))options.vk3dShader=hash;
        if(fragment&&argent::sfs::eternalLightGridRule(hash).pixels)options.lightGridShader=hash;
        const auto source=argent::sfs::stereoSource(words,options);std::ofstream output(argv[3]);output<<source;if(!output)throw std::runtime_error("Cannot write source");
        const auto compiled=argent::sfs::compileGlsl(source,inspect.get_execution_model());
        std::ofstream binary(std::string(argv[3])+".spv",std::ios::binary);binary.write(reinterpret_cast<const char*>(compiled.data()),compiled.size()*4);
        std::cout<<"hash="<<std::hex<<hash<<std::dec<<" binding="<<options.binding<<'\n';return 0;
    }
    if(argc!=3&&argc!=5)throw std::runtime_error("Usage: ArgentSfsCompile input.spv output.spv [projection-set projection-binding]");
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    if(bytes.size()<20||bytes.size()%4)throw std::runtime_error("Invalid SPIR-V file");
    std::vector<uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
    argent::sfs::ShaderOptions options;
    if(argc==5){options.project=true;options.set=std::stoul(argv[3]);options.binding=std::stoul(argv[4]);}
    auto result=argent::sfs::compileStereoShader(words,options);
    std::ofstream output(argv[2],std::ios::binary);output.write(reinterpret_cast<const char*>(result.data()),result.size()*4);
    if(!output)throw std::runtime_error("Cannot write SPIR-V");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
