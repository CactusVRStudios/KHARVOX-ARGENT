#include "BuildFeatures.h"
#include "ShaderCapture.h"
#include "RenderTrace.h"
#include "sfs/PipelineIdentity.h"
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace argent::capture {
namespace {
// The copied provider hash/packet is an explicitly unverified lookup candidate.
// SHA-256 is the capture identity. Nothing captured here enables a replacement.
struct Module {std::string sha,candidate;std::vector<uint32_t> words;};
struct State {
    std::mutex mutex;std::map<std::pair<VkDevice,VkShaderModule>,Module> modules;
    std::set<std::string> dumped;size_t memoryBytes{},diskBytes{},records{};
};
State state;
const std::filesystem::path& root(){static auto path=[] {wchar_t text[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",text,32768);return n&&n<32768?std::filesystem::path(text):std::filesystem::path{};}();return path;}
constexpr size_t budget=256*1024*1024;
std::string sha256(const void* bytes,size_t size){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};std::string result;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0){
        unsigned char digest[32]{};
        if(BCryptHashData(hash,(PUCHAR)bytes,ULONG(size),0)>=0&&BCryptFinishHash(hash,digest,32,0)>=0){std::ostringstream s;for(auto b:digest)s<<std::hex<<std::setw(2)<<std::setfill('0')<<int(b);result=s.str();}
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm,0);return result;
}
std::ofstream output(const char* name){std::filesystem::create_directories(root());return std::ofstream(root()/name,std::ios::app);}
bool enabled(){return !root().empty();}
}
std::string shaderIdentity(VkDevice d,VkShaderModule m) noexcept {try{std::lock_guard<std::mutex> guard(state.mutex);auto i=state.modules.find({d,m});return i==state.modules.end()?std::string{}:i->second.sha;}catch(...){return {};}}
void shader(VkDevice d,VkShaderModule m,const VkShaderModuleCreateInfo* ci) noexcept {try{
    if((!enabled()&&!sfs::vrEnabled())||!ci||ci->codeSize<20||ci->codeSize>16*1024*1024||ci->codeSize%4||!ci->pCode||ci->pCode[0]!=0x07230203)return;
    std::lock_guard<std::mutex> guard(state.mutex);if(state.memoryBytes+ci->codeSize>budget)return;
    Module module;module.sha=sha256(ci->pCode,ci->codeSize);if(module.sha.empty())return;
    module.candidate=kharvox::sfs::shaderKey(kharvox::sfs::profileHash(ci->pCode,uint32_t(ci->codeSize)));
    module.words.assign(ci->pCode,ci->pCode+ci->codeSize/4);
    auto old=state.modules.find({d,m});if(old!=state.modules.end())state.memoryBytes-=old->second.words.size()*4;
    state.memoryBytes+=ci->codeSize;state.modules[{d,m}]=module;
    if(!enabled())return;
    if(state.dumped.count(module.sha)||state.diskBytes+ci->codeSize>budget)return;
    std::filesystem::create_directories(root()/"spirv");
    std::ofstream out(root()/"spirv"/(module.sha+".spv"),std::ios::binary);out.write(reinterpret_cast<const char*>(ci->pCode),ci->codeSize);out.close();if(!out)return;
    state.dumped.insert(module.sha);state.diskBytes+=ci->codeSize;
    output("shaders.tsv")<<module.sha<<'\t'<<module.candidate<<'\t'<<ci->codeSize<<'\n';
    if(state.dumped.size()==1||state.dumped.size()%250==0)log("CAPTURE uniqueShaders="+std::to_string(state.dumped.size())+" bytes="+std::to_string(state.diskBytes));
}catch(...) {}}
void forgetShader(VkDevice d,VkShaderModule m) noexcept {try{if(!enabled()&&!sfs::vrEnabled())return;std::lock_guard<std::mutex> guard(state.mutex);auto it=state.modules.find({d,m});if(it!=state.modules.end()){state.memoryBytes-=it->second.words.size()*4;state.modules.erase(it);}}catch(...) {}}
void graphics(VkDevice d,uint32_t count,const VkGraphicsPipelineCreateInfo* infos,const VkPipeline* pipelines) noexcept {try{
    if(!enabled()||!infos)return;std::lock_guard<std::mutex> guard(state.mutex);auto out=output("graphics.tsv");
    for(uint32_t i=0;i<count&&state.records<200000;++i){const auto& p=infos[i];auto seed=kharvox::sfs::pipelineSeed(p);
        for(uint32_t j=0;j<p.stageCount;++j){auto it=state.modules.find({d,p.pStages[j].module});if(it==state.modules.end())continue;
            auto& module=it->second;auto variant=kharvox::sfs::profileHash(module.words.data(),uint32_t(module.words.size()*4),seed);
            out<<++state.records<<'\t'<<module.sha<<'\t'<<module.candidate<<'\t'<<kharvox::sfs::shaderKey(variant)<<'\t'<<p.pStages[j].stage<<'\t'<<reinterpret_cast<uintptr_t>(p.layout)<<'\t'<<reinterpret_cast<uintptr_t>(p.renderPass)<<'\t'<<p.subpass<<'\t'<<trace::id(pipelines[i])<<'\t'<<trace::currentFrame()<<'\n';
        }
    }
}catch(...) {}}
void compute(VkDevice d,uint32_t count,const VkComputePipelineCreateInfo* infos,const VkPipeline* pipelines) noexcept {try{
    if(!enabled()||!infos)return;std::lock_guard<std::mutex> guard(state.mutex);auto out=output("compute.tsv");
    for(uint32_t i=0;i<count&&state.records<200000;++i){auto it=state.modules.find({d,infos[i].stage.module});if(it!=state.modules.end())out<<++state.records<<'\t'<<it->second.sha<<'\t'<<it->second.candidate<<'\t'<<reinterpret_cast<uintptr_t>(infos[i].layout)<<'\t'<<trace::id(pipelines[i])<<'\t'<<trace::currentFrame()<<'\n';}
}catch(...) {}}
void descriptorLayout(VkDevice d,VkDescriptorSetLayout layout,const VkDescriptorSetLayoutCreateInfo* ci) noexcept {try{
    if(!enabled()||!ci)return;std::lock_guard<std::mutex> guard(state.mutex);if(state.records>=200000)return;auto out=output("descriptor-layouts.tsv");
    for(uint32_t i=0;i<ci->bindingCount;++i){const auto& b=ci->pBindings[i];out<<++state.records<<'\t'<<reinterpret_cast<uintptr_t>(d)<<'\t'<<reinterpret_cast<uintptr_t>(layout)<<'\t'<<b.binding<<'\t'<<b.descriptorType<<'\t'<<b.descriptorCount<<'\t'<<b.stageFlags<<'\n';}
}catch(...) {}}
void pipelineLayout(VkDevice d,VkPipelineLayout layout,const VkPipelineLayoutCreateInfo* ci) noexcept {try{
    if(!enabled()||!ci)return;std::lock_guard<std::mutex> guard(state.mutex);if(state.records>=200000)return;auto out=output("pipeline-layouts.tsv");
    for(uint32_t i=0;i<ci->setLayoutCount;++i)out<<++state.records<<'\t'<<reinterpret_cast<uintptr_t>(d)<<'\t'<<reinterpret_cast<uintptr_t>(layout)<<'\t'<<i<<'\t'<<reinterpret_cast<uintptr_t>(ci->pSetLayouts[i])<<'\t'<<ci->pushConstantRangeCount<<'\n';
}catch(...) {}}
void renderPass(VkDevice d,VkRenderPass pass,const VkRenderPassCreateInfo* ci) noexcept {try{
    if(!enabled()||!ci)return;std::lock_guard<std::mutex> guard(state.mutex);if(state.records>=200000)return;auto out=output("renderpasses.tsv");
    for(uint32_t i=0;i<ci->attachmentCount;++i){const auto& a=ci->pAttachments[i];out<<++state.records<<'\t'<<reinterpret_cast<uintptr_t>(d)<<'\t'<<reinterpret_cast<uintptr_t>(pass)<<'\t'<<i<<'\t'<<a.format<<'\t'<<a.samples<<'\t'<<a.loadOp<<'\t'<<a.storeOp<<'\t'<<a.initialLayout<<'\t'<<a.finalLayout<<'\t'<<ci->subpassCount<<'\n';}
}catch(...) {}}
void forgetDevice(VkDevice d) noexcept {try{std::lock_guard<std::mutex> guard(state.mutex);for(auto it=state.modules.begin();it!=state.modules.end();)if(it->first.first==d){state.memoryBytes-=it->second.words.size()*4;it=state.modules.erase(it);}else ++it;}catch(...) {}}
}
