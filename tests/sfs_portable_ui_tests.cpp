#include "../src/sfs/PortableUiIdentity.h"
#include "../src/sfs/ShaderCompiler.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace argent::sfs;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    check(argc==2,"Missing shader corpus");unsigned count=0;std::set<uint64_t> found;
    const std::set<uint64_t> known{0xdc2d10822f8eda88ull,0xf37a280cc1dc1a83ull,0x0749c071d7d1fdf5ull,0x4f50b1f4caf20882ull,0x475b91f7adce5776ull,0xc8d657a2321cc9eeull,0x32fb81bae310c07dull};
    for(const auto& f:std::filesystem::directory_iterator(argv[1])){
        if(f.path().extension()!=L".spv")continue;
        std::ifstream in(f.path(),std::ios::binary|std::ios::ate);std::vector<uint32_t> words(size_t(in.tellg())/4);in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),words.size()*4);
        const auto raw=kharvox::sfs::profileHash(words.data(),uint32_t(words.size()*4));const auto alias=portableUiProfile(words);++count;
        check(alias==(known.count(raw)?raw:0),"False UI classification or missing reference UI");
        if(!alias)continue;found.insert(alias);
        auto renamed=words;renamed[2]^=0x12340000;
        // Existing result ID, debug-only OpName. No executable instruction changes.
        renamed.insert(renamed.begin()+5,{(4u<<16)|spv::OpName,1,0x74657374,0});
        check(portableUiProfile(renamed)==alias,"Debug/generator variant lost its UI policy");
        for(bool mono:{false,true}){
            ShaderOptions options;options.project=true;options.screenSpaceUi=true;options.uiShader=alias;options.binding=63;options.monoView=mono;
            check(!compileStereoShader(renamed,options).empty(),"Metadata variant did not compile");
            auto source=stereoSource(renamed,options);check(source.find("screenClip")!=std::string::npos,"UI projection lost");
            if(alias!=0xf37a280cc1dc1a83ull)check(source.find(" > 0.0)")!=std::string::npos,"World-space hand panel flag lost");
        }
        for(size_t i=5;i<renamed.size();i+=renamed[i]>>16){
            if(spv::Op(renamed[i]&0xffffu)==spv::OpDecorate&&renamed[i+2]==spv::DecorationBinding){
                renamed[i+3]+=1;check(!portableUiProfile(renamed),"Different descriptor layout incorrectly aliased");break;
            }
        }
    }
    check(found==known,"Corpus missing UI families");check(!portableUiProfile({1,2,3}),"Invalid shader accepted");
    check(!portableUiProfile({spv::MagicNumber,0x10000,0,1,0,0}),"Zero-length instruction accepted");
    std::cout<<count<<" reference shaders checked; seven metadata-independent UI families, stereo/mono and negative layout cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
