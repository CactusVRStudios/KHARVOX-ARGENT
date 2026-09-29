#include "../src/sfs/NonuniformSamplers.h"
#include "../src/sfs/ShaderCompiler.h"
#include <iostream>
#include <set>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 const auto original=argent::sfs::compileGlsl(R"(#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(local_size_x=8) in;
layout(set=0,binding=0) uniform texture2D images[4];
layout(set=0,binding=1) uniform sampler s;
layout(set=0,binding=2,rgba32f) uniform writeonly image2D outputImage;
void main(){uint i=gl_LocalInvocationID.x%4u;
 imageStore(outputImage,ivec2(gl_GlobalInvocationID.xy),textureLod(sampler2D(images[nonuniformEXT(i)],s),vec2(0.5),0.0));}
)",spv::ExecutionModelGLCompute);
 auto fixed=original;const auto added=argent::sfs::preserveNonuniformSamplers(fixed);
 require(added>0,"Regression fixture must reproduce missing sampler decoration");
 const auto saved=fixed;require(argent::sfs::preserveNonuniformSamplers(fixed)==0&&fixed==saved,"Pass is not idempotent");
 std::set<uint32_t> marked;unsigned samples=0;
 for(size_t p=5;p<fixed.size();p+=fixed[p]>>16)if((fixed[p]&65535)==spv::OpDecorate&&fixed[p+2]==spv::DecorationNonUniform)marked.insert(fixed[p+1]);
 for(size_t p=5;p<fixed.size();p+=fixed[p]>>16)if((fixed[p]&65535)==spv::OpImageSampleExplicitLod){++samples;require(marked.count(fixed[p+3]),"Final sample operand lacks NonUniform");}
 require(samples==1,"Texture operation changed");
 auto instructions=[](const std::vector<uint32_t>& w){std::vector<uint32_t> out(w.begin(),w.begin()+5);for(size_t p=5;p<w.size();p+=w[p]>>16)if((w[p]&65535)!=spv::OpDecorate)out.insert(out.end(),w.begin()+p,w.begin()+p+(w[p]>>16));return out;};
 require(instructions(original)==instructions(fixed),"Pass modified executable instructions or IDs");
 auto uniform=argent::sfs::compileGlsl("#version 460\nlayout(local_size_x=1) in;void main(){}",spv::ExecutionModelGLCompute);
 const auto uniformBefore=uniform;require(argent::sfs::preserveNonuniformSamplers(uniform)==0&&uniform==uniformBefore,"Uniform shader changed");
 std::cout<<"Nonuniform final operand, instruction preservation, idempotence and uniform no-op passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
