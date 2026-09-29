#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
namespace argent::sfs {
struct Vk3dEdit {std::string before,after;bool all{};};
struct Vk3dRule {uint64_t hash;bool worldUniform;std::vector<Vk3dEdit> edits;bool preserveNonuniform{};};
inline const std::vector<Vk3dRule>& eternalVk3dRules(){
 static const std::vector<Vk3dRule> rules={
#include "EternalVk3dRules.inc"
#include "EternalAmdMaterialRules.inc"
 };return rules;
}
inline const Vk3dRule* eternalVk3dRule(uint64_t hash){for(const auto& r:eternalVk3dRules())if(r.hash==hash)return &r;return nullptr;}
inline void applyEternalVk3d(std::string& source,const Vk3dRule& rule){
 for(const auto& edit:rule.edits){
  auto pos=source.find(edit.before);
  if(pos==std::string::npos){
   // A captured module may already carry this nonuniform decoration.
   if(edit.all&&source.find(edit.after)!=std::string::npos)continue;
   throw std::runtime_error("Vk3D correction anchor changed: "+edit.before);
  }
  if(!edit.all&&source.find(edit.before,pos+edit.before.size())!=std::string::npos)throw std::runtime_error("Vk3D correction anchor is ambiguous: "+edit.before);
  do {source.replace(pos,edit.before.size(),edit.after);pos=edit.all?source.find(edit.before,pos+edit.after.size()):std::string::npos;}while(pos!=std::string::npos);
 }
}
}
