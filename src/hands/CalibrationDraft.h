#pragma once
#include <map>
#include <string>
#include <cstdint>
#include <filesystem>
#include <fstream>
namespace argent::calibration {
struct ApplyCommand {uint64_t revision{};std::string mode{"off"},profile{"default"};bool left{};};
template<class T> struct Draft {
 std::map<std::string,T> saved,pending;std::string scope;
 void select(std::string next){if(next!=scope){pending.clear();scope=std::move(next);}}
 const T* find(const std::string& key)const{
  auto i=pending.find(key);if(i!=pending.end())return &i->second;
  i=saved.find(key);return i==saved.end()?nullptr:&i->second;
 }
 T& edit(const std::string& key,const T& initial){return pending.try_emplace(key,initial).first->second;}
 template<class Save> bool apply(Save save){
  auto candidate=saved;for(const auto& row:pending)candidate[row.first]=row.second;
  if(!save(candidate))return false;saved=std::move(candidate);pending.clear();return true;
 }
};
struct ApplyRevision {
 uint64_t seen{};bool initialized{};
 bool consume(uint64_t value){if(!initialized){initialized=true;seen=value;return false;}
  const bool changed=value&&value!=seen;seen=value;return changed;}
};
inline void report(const std::filesystem::path& root,uint64_t revision,bool success,const std::string& target){
 std::ofstream out(root/L"calibration_apply_status.txt");out<<revision<<' '<<success<<' '<<target;
}
}
