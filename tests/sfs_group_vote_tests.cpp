#include "../src/sfs/ShaderCompiler.h"
#include <spirv.hpp>
#include <iostream>
#include <stdexcept>
using namespace argent::sfs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static std::vector<uint32_t> shader(spv::Op vote,spv::Scope scope){
    std::vector<uint32_t> w{0x07230203,0x00010000,0,32,0};
    auto op=[&](spv::Op code,std::initializer_list<uint32_t> args){w.push_back((uint32_t(args.size()+1)<<16)|code);w.insert(w.end(),args);};
    op(spv::OpCapability,{spv::CapabilityShader});op(spv::OpCapability,{spv::CapabilityGroups});
    op(spv::OpMemoryModel,{spv::AddressingModelLogical,spv::MemoryModelGLSL450});
    op(spv::OpEntryPoint,{spv::ExecutionModelFragment,10,0x6e69616d,0,9});
    op(spv::OpExecutionMode,{10,spv::ExecutionModeOriginUpperLeft});
    op(spv::OpDecorate,{9,spv::DecorationLocation,0});
    op(spv::OpTypeVoid,{1});op(spv::OpTypeFunction,{2,1});op(spv::OpTypeBool,{3});
    op(spv::OpTypeInt,{4,32,0});op(spv::OpConstant,{4,5,uint32_t(scope)});
    op(spv::OpConstantTrue,{3,6});op(spv::OpTypeFloat,{7,32});
    op(spv::OpTypePointer,{8,spv::StorageClassOutput,7});op(spv::OpVariable,{8,9,spv::StorageClassOutput});
    op(spv::OpConstant,{7,14,0x3f800000});op(spv::OpConstant,{7,15,0});
    op(spv::OpFunction,{1,10,0,2});op(spv::OpLabel,{11});
    op(vote,{3,12,5,6});op(spv::OpSelect,{7,13,12,14,15});
    op(spv::OpStore,{9,13});op(spv::OpReturn,{});op(spv::OpFunctionEnd,{});return w;
}
int main(){try{
    for(auto vote:{spv::OpGroupAll,spv::OpGroupAny}){
        auto input=shader(vote,spv::ScopeSubgroup);
        auto compiled=compileStereoShader(input);auto source=stereoSource(input);
        check(!compiled.empty(),"Legacy vote did not compile");
        check(source.find(vote==spv::OpGroupAll?"allInvocationsARB":"anyInvocationARB")!=std::string::npos,"Vote semantics lost");
        check(source.find("unimplemented op")==std::string::npos,"Unsupported operation survived");
        bool rejected=false;try{compileStereoShader(shader(vote,spv::ScopeWorkgroup));}
        catch(const std::exception& e){rejected=std::string(e.what()).find("requires subgroup scope")!=std::string::npos;}
        check(rejected,"Workgroup vote silently reduced to subgroup");
    }
    std::cout<<"AMD legacy subgroup all/any compilation and scope rejection passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
