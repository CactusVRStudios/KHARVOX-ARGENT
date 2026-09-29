#pragma once
#include <spirv.hpp>
#include <set>
#include <stdexcept>
#include <vector>
namespace argent::sfs {
// A GLSL sampler constructor does not inherit nonuniformEXT from its image.
// Preserve known nonuniform resource identity through opaque constructions and
// copies, including OpImage used by size/fetch operations. No index is guessed
// to be nonuniform; only existing decorations seed this propagation.
inline unsigned preserveNonuniformSamplers(std::vector<uint32_t>& words){
 if(words.size()<5)throw std::runtime_error("Invalid SPIR-V header");
 std::set<uint32_t> decorated,required;
 size_t annotationsEnd=words.size();
 for(size_t p=5;p<words.size();){
  const auto count=words[p]>>16,op=words[p]&65535;
  if(!count||p+count>words.size())throw std::runtime_error("Invalid SPIR-V instruction");
  if(op==spv::OpDecorate&&count>=3&&words[p+2]==spv::DecorationNonUniform)decorated.insert(words[p+1]);
  if(op>=spv::OpTypeVoid&&op<=spv::OpTypeForwardPointer&&annotationsEnd==words.size())annotationsEnd=p;
  p+=count;
 }
 required=decorated;bool changed;
 do {changed=false;
  for(size_t p=5;p<words.size();p+=words[p]>>16){
   const auto count=words[p]>>16,op=words[p]&65535;
   if((op==spv::OpSampledImage&&count==5&&(required.count(words[p+3])||required.count(words[p+4])))||
      ((op==spv::OpCopyObject||op==spv::OpImage)&&count==4&&required.count(words[p+3])))
    changed=required.insert(words[p+2]).second||changed;
  }
 }while(changed);
 std::vector<uint32_t> annotations;
 for(auto id:required)if(!decorated.count(id))annotations.insert(annotations.end(),{(3u<<16)|spv::OpDecorate,id,spv::DecorationNonUniform});
 words.insert(words.begin()+annotationsEnd,annotations.begin(),annotations.end());
 return unsigned(annotations.size()/3);
}
}
