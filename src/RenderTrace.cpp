#include "BuildFeatures.h"
#include "RenderTrace.h"
#include <fstream>
#include <unordered_map>
#include <set>
#include <cstring>

namespace argent::trace {
namespace {
struct Event {const char* name;std::array<uint64_t,6> args;};
struct Command {VkCommandPool pool{};uint64_t epoch{};std::vector<Event> events;bool truncated{};};
std::mutex mutex;
std::unordered_map<VkCommandBuffer,Command> commands;
uint64_t epoch{},frame{},rows{},objectRows{};
uint64_t requestedFrame=UINT64_MAX;
size_t stored{};
constexpr size_t maxStored=250000,maxPerCommand=16384,maxRows=300000;
const std::filesystem::path& root(){static auto p=[]{wchar_t s[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",s,32768);return n&&n<32768?std::filesystem::path(s):std::filesystem::path{};}();return p;}
bool enabled(){return !cleanRelease&&!root().empty();}
bool sample(){return frame==requestedFrame||frame==0||frame==120||frame==600||frame==1800||frame==3600||frame==7200||frame==14400||frame==28800;}
void emit(std::ofstream& out,VkQueue q,VkCommandBuffer c,std::set<VkCommandBuffer>& visiting,unsigned depth){
    if(rows>=maxRows||depth>8||!visiting.insert(c).second)return;
    auto found=commands.find(c);
    if(found==commands.end()){out<<frame<<'\t'<<id(q)<<'\t'<<id(c)<<"\t0\tmissing_command\t0\t0\t0\t0\t0\t0\n";++rows;visiting.erase(c);return;}
    auto& cmd=found->second;
    for(auto& e:cmd.events){if(rows++>=maxRows)break;out<<frame<<'\t'<<id(q)<<'\t'<<id(c)<<'\t'<<cmd.epoch<<'\t'<<e.name;for(auto a:e.args)out<<'\t'<<a;out<<'\n';if(!strcmp(e.name,"execute"))emit(out,q,reinterpret_cast<VkCommandBuffer>(e.args[0]),visiting,depth+1);}
    if(cmd.truncated){out<<frame<<'\t'<<id(q)<<'\t'<<id(c)<<'\t'<<cmd.epoch<<"\ttruncated\t0\t0\t0\t0\t0\t0\n";++rows;}
    visiting.erase(c);
}
}
void reset(VkCommandBuffer c,VkCommandPool pool) noexcept {try{if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);auto& cmd=commands[c];stored-=cmd.events.size();cmd.events.clear();cmd.truncated=false;cmd.epoch=++epoch;if(pool)cmd.pool=pool;}catch(...) {}}
void resetPool(VkCommandPool pool,bool destroy) noexcept {try{if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);for(auto i=commands.begin();i!=commands.end();)if(i->second.pool==pool){stored-=i->second.events.size();if(destroy)i=commands.erase(i);else {i->second.events.clear();i->second.truncated=false;i->second.epoch=++epoch;++i;}}else ++i;}catch(...) {}}
uint64_t currentFrame() noexcept {if constexpr(cleanRelease)return 0;std::lock_guard<std::mutex> lock(mutex);return frame;}
void forget(VkCommandBuffer c) noexcept {try{if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);auto it=commands.find(c);if(it!=commands.end()){stored-=it->second.events.size();commands.erase(it);}}catch(...) {}}
void record(VkCommandBuffer c,const char* name,std::array<uint64_t,6> args) noexcept {try{if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);auto& cmd=commands[c];if(stored>=maxStored||cmd.events.size()>=maxPerCommand){cmd.truncated=true;return;}cmd.events.push_back({name,args});++stored;}catch(...) {}}
void submit(VkQueue q,uint32_t count,const VkCommandBuffer* buffers) noexcept {try{if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);if(!sample()||rows>=maxRows)return;std::ofstream out(root()/"render-trace.tsv",std::ios::app);std::set<VkCommandBuffer> visiting;for(uint32_t i=0;i<count;++i)emit(out,q,buffers[i],visiting,0);}catch(...) {}}
void present() noexcept {try{if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);++frame;if(frame%120==0){std::error_code error;if(std::filesystem::remove(root()/"capture-frame.request",error))requestedFrame=frame;}}catch(...) {}}
void object(VkDevice d,const char* kind,uint64_t handle,std::array<uint64_t,6> args) noexcept {try{if(!enabled())return;std::lock_guard<std::mutex> lock(mutex);if(objectRows++>=maxRows)return;std::filesystem::create_directories(root());std::ofstream out(root()/"render-objects.tsv",std::ios::app);out<<frame<<'\t'<<id(d)<<'\t'<<kind<<'\t'<<handle;for(auto a:args)out<<'\t'<<a;out<<'\n';}catch(...) {}}
}
