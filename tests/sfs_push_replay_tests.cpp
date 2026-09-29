#include "../src/sfs/PushReplay.h"
#include <map>
#include <functional>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <random>

void require(bool value){if(!value)throw std::runtime_error("Push replay mismatch");}
int main(){try{
    {
        kharvox::sfs::PushReplay allStages;
        uint32_t value=123;auto layout=reinterpret_cast<VkPipelineLayout>(uintptr_t(1));
        allStages.write(layout,VK_SHADER_STAGE_ALL,0,4,&value);
        uint32_t stages=0;
        allStages.replay([&](VkPipelineLayout actual,VkShaderStageFlags stage,uint32_t offset,uint32_t size,const void* data){require(actual==layout&&offset==0&&size==4&&*static_cast<const uint32_t*>(data)==value);stages|=stage;});
        require(stages==0x3fu);
    }
    kharvox::sfs::PushReplay replay;
    using Value=std::pair<VkPipelineLayout,uint32_t>;
    std::map<std::pair<unsigned,unsigned>,Value> reference;
    std::mt19937 random(719);
    for(unsigned trial=0;trial<1000;++trial){
        if(trial%37==0){replay.clear();reference.clear();}
        auto layout=reinterpret_cast<VkPipelineLayout>(uintptr_t(1+random()%3));
        unsigned first=random()%48,count=1+random()%16,flags=1+random()%63;
        std::array<uint32_t,16> words{};for(auto& word:words)word=random();
        replay.write(layout,flags,first*4,count*4,words.data());
        for(unsigned bit=0;bit<6;++bit)if(flags&(1u<<bit))for(unsigned n=0;n<count;++n)reference[{bit,first+n}]={layout,words[n]};
        decltype(reference) observed;
        replay.replay([&](VkPipelineLayout l,VkShaderStageFlags stage,uint32_t offset,uint32_t size,const void* values){
            unsigned bit=0;while((1u<<bit)!=stage)++bit;
            const auto words=static_cast<const uint32_t*>(values);
            for(unsigned n=0;n<size/4;++n)observed[{bit,offset/4+n}]={l,words[n]};
        });
        require(reference==observed);
    }
    replay.clear();std::array<uint32_t,64> words{};
    auto layout=reinterpret_cast<VkPipelineLayout>(uintptr_t(1));
    replay.write(layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(words),words.data());
    require(replay.replay([](auto,auto,auto,auto,auto){})==1);
    // Microbenchmark the removed map/function pattern, not a game FPS claim.
    volatile uint64_t checksum=0;
    const auto before=std::chrono::steady_clock::now();
    for(unsigned i=0;i<10000;++i){std::map<uint64_t,std::function<void()>> old;
        for(unsigned n=0;n<64;++n)old[n]=[&,value=i+n]{checksum+=value;};
        for(const auto& entry:old)entry.second();
    }
    const auto middle=std::chrono::steady_clock::now();
    for(unsigned i=0;i<10000;++i){replay.clear();for(unsigned n=0;n<64;++n)words[n]=i+n;
        replay.write(layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(words),words.data());
        replay.replay([&](auto,auto,auto,uint32_t size,const void* bytes){auto values=static_cast<const uint32_t*>(bytes);for(unsigned n=0;n<size/4;++n)checksum+=values[n];});
    }
    const auto after=std::chrono::steady_clock::now();
    std::cout<<"1000 partial/mixed-stage/layout/reset comparisons passed; 256-byte replay: 64 calls -> 1\n"
        <<"CPU microbenchmark oldMs="<<std::chrono::duration<double,std::milli>(middle-before).count()
        <<" newMs="<<std::chrono::duration<double,std::milli>(after-middle).count()<<" checksum="<<checksum<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
