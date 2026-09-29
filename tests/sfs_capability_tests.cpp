#include "../src/sfs/DeviceCapabilities.h"
#include <iostream>
#include <stdexcept>
using namespace argent::sfs;
void check(bool b){if(!b)throw std::runtime_error("SFS capability policy failed");}
int main(){try{
    VkPhysicalDeviceProperties p{};p.apiVersion=VK_API_VERSION_1_1;
    p.limits.maxImageArrayLayers=256;p.limits.maxImageDimension2D=16384;
    for(auto vendor:{0x1002u,0x10deu,0x8086u}){p.vendorID=vendor;check(!stereoCapabilityFailure(p,1,2,{2496,2688},nullptr));}
    check(stereoCapabilityFailure(p,0,2,{},nullptr));check(stereoCapabilityFailure(p,1,1,{},nullptr));
    check(stereoCapabilityFailure(p,1,2,{20000,20000},nullptr));
    p.limits.maxImageArrayLayers=1;check(stereoCapabilityFailure(p,1,2,{},nullptr));p.limits.maxImageArrayLayers=256;
    p.apiVersion=VK_API_VERSION_1_0;check(stereoCapabilityFailure(p,1,2,{},nullptr));p.apiVersion=VK_API_VERSION_1_1;
    VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
    check(stereoCapabilityFailure(p,1,2,{},&mv));mv.multiview=1;check(!stereoCapabilityFailure(p,1,2,{},&mv));
    VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};v11.multiview=1;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&v11;
    check(multiviewRequest(&features).present);check(!stereoCapabilityFailure(p,1,2,{},&features));
    v11.multiview=0;check(stereoCapabilityFailure(p,1,2,{},&features));v11.multiview=1;
    v11.pNext=&mv;check(stereoCapabilityFailure(p,1,2,{},&features));
    check(features.pNext==&v11&&v11.pNext==&mv&&mv.multiview==1);
    std::cout<<"Vendor-neutral SFS limits and immutable multiview feature-chain checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
