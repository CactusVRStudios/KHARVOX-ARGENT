#include "../src/sfs/StereoSource.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstring>

// Offline only: emits the transformed source. Compilation does not authorize
// resource promotion, descriptor injection, or shader replacement in the game.
int main(int argc,char** argv){try{
    if(argc!=3&&argc!=5)throw std::runtime_error("Usage: ArgentSfsTranscode input.spv output.glsl [projection-set projection-binding]");
    std::ifstream in(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
    if(bytes.size()<20||bytes.size()%4)throw std::runtime_error("Invalid SPIR-V size");
    std::vector<uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
    argent::sfs::ShaderOptions options;
    if(argc==5){options.project=true;options.set=std::stoul(argv[3]);options.binding=std::stoul(argv[4]);}
    auto source=argent::sfs::stereoSource(words,options);
    std::ofstream out(argv[2]);out<<source;if(!out)throw std::runtime_error("Cannot write output");
    std::cout<<"Offline SFS source generated; stage="<<unsigned(spirv_cross::Compiler(words).get_execution_model())<<" projection="<<options.project<<" runtimeActivation=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
