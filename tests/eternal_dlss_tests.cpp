#include "../src/EternalDlssAbi.h"
#include <stdexcept>
#include <iostream>
using namespace argent::dlss;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(){try{
    Parameters input;std::array<Resource,5> originals{};
    for(size_t i=0;i<originals.size();++i){
        auto& r=originals[i];r.view=reinterpret_cast<VkImageView>(uintptr_t(10+i));r.image=reinterpret_cast<VkImage>(uintptr_t(100+i));
        r.range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};r.width=1448;r.height=1559;
        input.set<const Resource*>(i<4?resourceOffsets[i]:0x50,&r);
    }
    input.set<float>(0x28,.25f);input.set<float>(0x2c,-.125f);
    input.set<float>(0x3c,-1448.f);input.set<float>(0x40,-1559.f);
    input.set<uint32_t>(0x30,1448);input.set<uint32_t>(0x34,1559);
    const auto originalBytes=input.bytes;std::array<EyeParameters,2> eyes;
    auto resolve=[](const auto& resources,auto& out){for(size_t i=0;i<resources.size();++i)if(resources[i])for(int eye=0;eye<2;++eye){out[eye].resources[i]=*resources[i];out[eye].resources[i].range.baseArrayLayer=i<4?eye:0;}return true;};
    check(prepareEyes(input,eyes,resolve,true),"valid Eternal parameter block refused");
    check(input.bytes==originalBytes,"engine parameters mutated");
    for(int eye=0;eye<2;++eye){
        check(eyes[eye].params.get<int>(0x38)==1,"history reset lost");
        check(eyes[eye].params.get<float>(0x28)==.25f&&eyes[eye].params.get<float>(0x3c)==-1448.f,"jitter or motion scale changed");
        for(int i=0;i<4;++i){const auto r=eyes[eye].params.get<const Resource*>(resourceOffsets[i]);check(r==&eyes[eye].resources[i]&&r!=&originals[i]&&r->range.baseArrayLayer==eye,"resource lifetime or eye mapping incorrect");}
        check(eyes[eye].params.get<const Resource*>(0x50)->range.baseArrayLayer==0,"shared exposure changed eyes");
        check(eyes[eye].params.get<const Resource*>(0xa0)==nullptr,"null optional resource became non-null");
    }
    check(prepareEyes(input,eyes,resolve,false)&&eyes[0].params.get<int>(0x38)==0,"reset became permanent");
    originals[2].type=1;check(!prepareEyes(input,eyes,resolve,false),"buffer accepted as depth image");originals[2].type=0;
    input.set<const Resource*>(0x20,nullptr);check(!prepareEyes(input,eyes,resolve,false),"missing motion input accepted");
    std::cout<<"Eternal NGX ABI, eye ownership, optional exposure, temporal constants and reset validated\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
