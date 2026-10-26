#pragma once
#include "../hands/HandSceneTarget.h"
#include <map>
#include <vector>
#include <algorithm>
namespace argent::fg {
// Protected by the caller's FG mutex. A recording can precede acquire; assign
// the frame serial only when the native queue receives that command buffer.
class NativeInputTargets {
 using Target=kharvox::hands::HandSceneTarget;
 struct Submitted {Target target;VkQueue queue{};uint64_t serial{};};
 std::map<VkCommandBuffer,std::vector<Target>> commands;
 std::map<VkImage,Submitted> submitted;
public:
 void begin(VkCommandBuffer command){commands.erase(command);}
 void record(VkCommandBuffer command,uint32_t count,const Target* targets){
  auto& list=commands[command];for(uint32_t i=0;i<count;++i){const auto& t=targets[i];
   if(!t.colorImage||!t.colorView||!t.depthImage||!t.depthView)continue;
   list.erase(std::remove_if(list.begin(),list.end(),[&](const Target& old){return old.colorImage==t.colorImage;}),list.end());list.push_back(t);
  }
 }
 void submit(VkCommandBuffer command,VkQueue queue,uint64_t serial){
  if(!queue||!serial)return;auto found=commands.find(command);if(found==commands.end())return;
  for(const auto& t:found->second)submitted[t.colorImage]={t,queue,serial};
 }
 bool find(VkImage color,VkQueue queue,uint64_t serial,Target& out)const{
  const auto found=submitted.find(color);if(found==submitted.end()||!serial||found->second.serial!=serial||found->second.queue!=queue)return false;
  out=found->second.target;return true;
 }
 void forget(VkImage image){
  for(auto& entry:commands){auto& list=entry.second;list.erase(std::remove_if(list.begin(),list.end(),[&](const Target& t){return t.colorImage==image||t.depthImage==image;}),list.end());}
  for(auto i=submitted.begin();i!=submitted.end();)if(i->second.target.colorImage==image||i->second.target.depthImage==image)i=submitted.erase(i);else ++i;
 }
 void failed(VkQueue queue){for(auto i=submitted.begin();i!=submitted.end();)if(i->second.queue==queue)i=submitted.erase(i);else ++i;}
 void clear(){commands.clear();submitted.clear();}
};
}
