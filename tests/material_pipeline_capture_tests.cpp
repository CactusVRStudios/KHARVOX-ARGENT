#include "../src/sfs/MaterialPipelineCapture.h"
#include <cassert>
#include <cstring>
#include <iostream>
static unsigned creates{},destroys{},reads{},mode{};
static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice,const VkPipelineCacheCreateInfo* i,const VkAllocationCallbacks*,VkPipelineCache* p){++creates;assert(i->initialDataSize==0);if(mode==1)return VK_ERROR_OUT_OF_HOST_MEMORY;*p=reinterpret_cast<VkPipelineCache>(7);return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice,VkPipelineCache p,const VkAllocationCallbacks*){assert(p==reinterpret_cast<VkPipelineCache>(7));++destroys;}
static VKAPI_ATTR VkResult VKAPI_CALL get(VkDevice,VkPipelineCache,size_t* size,void* p){++reads;if(!p){*size=mode==2?32u*1024*1024:4;return VK_SUCCESS;}assert(*size==4);std::memcpy(p,"test",4);return mode==3?VK_INCOMPLETE:VK_SUCCESS;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice,const char* n){if(!std::strcmp(n,"vkCreatePipelineCache"))return reinterpret_cast<PFN_vkVoidFunction>(create);if(!std::strcmp(n,"vkGetPipelineCacheData"))return reinterpret_cast<PFN_vkVoidFunction>(get);if(!std::strcmp(n,"vkDestroyPipelineCache"))return reinterpret_cast<PFN_vkVoidFunction>(destroy);return nullptr;}
int main(){
 const auto fallback=reinterpret_cast<VkPipelineCache>(9);
 {argent::sfs::MaterialPipelineCapture c({},resolve,false);assert(!c.active()&&c.cache(fallback)==fallback&&c.data().empty());}assert(!creates&&!destroys&&!reads);
 {argent::sfs::MaterialPipelineCapture c({},resolve,true);assert(c.active()&&c.cache(fallback)!=fallback);assert(c.data()==std::vector<char>({'t','e','s','t'}));}assert(creates==1&&destroys==1&&reads==2);
 mode=1;{argent::sfs::MaterialPipelineCapture c({},resolve,true);assert(!c.active()&&c.cache(fallback)==fallback);}assert(destroys==1);
 for(mode=2;mode<=3;++mode){bool rejected=false;{argent::sfs::MaterialPipelineCapture c({},resolve,true);try{c.data();}catch(const std::runtime_error&){rejected=true;}}assert(rejected);}assert(destroys==3);
 std::cout<<"Material cache capture: disabled path, private cache, creation fallback, size/incomplete rejection and cleanup passed\n";
}
