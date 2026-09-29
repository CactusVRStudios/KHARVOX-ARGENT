#include "../src/sfs/CommandBindings.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <new>
using namespace kharvox::sfs;
static bool measureAllocations{};static size_t allocations{};
void* operator new(size_t n){if(measureAllocations)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
struct Event {unsigned kind,slot;uint64_t value;bool operator==(const Event& o)const{return kind==o.kind&&slot==o.slot&&value==o.value;}};
static std::vector<Event> events;static bool capture=true;static volatile uint64_t checksum{};
static void event(unsigned kind,unsigned slot,uint64_t value){if(capture)events.push_back({kind,slot,value});else checksum+=kind+slot+value;}
static VKAPI_ATTR void VKAPI_CALL vertex(VkCommandBuffer,uint32_t first,uint32_t n,const VkBuffer* b,const VkDeviceSize* offsets){for(uint32_t j=0;j<n;++j)event(0,first+j,uint64_t(b[j])+offsets[j]);}
static VKAPI_ATTR void VKAPI_CALL index(VkCommandBuffer,VkBuffer b,VkDeviceSize o,VkIndexType type){event(1,type,uint64_t(b)+o);}
static VKAPI_ATTR void VKAPI_CALL viewport(VkCommandBuffer,uint32_t first,uint32_t n,const VkViewport* v){for(uint32_t j=0;j<n;++j)event(2,first+j,uint64_t(int64_t(v[j].height)));}
static VKAPI_ATTR void VKAPI_CALL scissor(VkCommandBuffer,uint32_t first,uint32_t n,const VkRect2D* v){for(uint32_t j=0;j<n;++j)event(3,first+j,v[j].extent.width);}
static VKAPI_ATTR void VKAPI_CALL line(VkCommandBuffer,float x){event(4,0,uint64_t(x));}
static VKAPI_ATTR void VKAPI_CALL bias(VkCommandBuffer,float a,float b,float c){event(5,0,uint64_t(a+10*b+100*c));}
static VKAPI_ATTR void VKAPI_CALL blend(VkCommandBuffer,const float* v){event(6,0,uint64_t(v[0]+10*v[1]+100*v[2]+1000*v[3]));}
static VKAPI_ATTR void VKAPI_CALL bounds(VkCommandBuffer,float a,float b){event(7,0,uint64_t(a+10*b));}
static VKAPI_ATTR void VKAPI_CALL compare(VkCommandBuffer,VkStencilFaceFlags f,uint32_t v){event(8,f,v);}
static VKAPI_ATTR void VKAPI_CALL write(VkCommandBuffer,VkStencilFaceFlags f,uint32_t v){event(9,f,v);}
static VKAPI_ATTR void VKAPI_CALL reference(VkCommandBuffer,VkStencilFaceFlags f,uint32_t v){event(10,f,v);}
int main(){
 NativeDispatch d{};d.vkCmdBindVertexBuffers=vertex;d.vkCmdBindIndexBuffer=index;d.vkCmdSetViewport=viewport;d.vkCmdSetScissor=scissor;d.vkCmdSetLineWidth=line;d.vkCmdSetDepthBias=bias;d.vkCmdSetBlendConstants=blend;d.vkCmdSetDepthBounds=bounds;d.vkCmdSetStencilCompareMask=compare;d.vkCmdSetStencilWriteMask=write;d.vkCmdSetStencilReference=reference;
 CommandBindings c;CommandBindings::set(c.vertices,3,CommandBindings::Vertex{VkBuffer(10),uint64_t(1)<<34});CommandBindings::set(c.vertices,1,CommandBindings::Vertex{VkBuffer(5),7});
 c.index={VkBuffer(11),9,VK_INDEX_TYPE_UINT32};CommandBindings::set(c.viewports,2,VkViewport{0,0,100,-99,0,1});CommandBindings::set(c.scissors,1,VkRect2D{{0,0},{103,55}});
 c.line=2;c.bias={1,2,3};c.blend={1,2,3,4};c.bounds={0,1};c.setStencil(0,VK_STENCIL_FACE_FRONT_AND_BACK,17);c.setStencil(0,VK_STENCIL_FACE_BACK_BIT,23);c.setStencil(1,VK_STENCIL_FACE_FRONT_BIT,31);c.setStencil(2,VK_STENCIL_FACE_BACK_BIT,47);
 c.replay(d,VK_NULL_HANDLE);
 const std::vector<Event> expected{{0,1,12},{0,3,10+(uint64_t(1)<<34)},{1,VK_INDEX_TYPE_UINT32,20},{2,2,uint64_t(int64_t(-99))},{3,1,103},{4,0,2},{5,0,321},{6,0,4321},{7,0,10},{8,1,17},{8,2,23},{9,1,31},{10,2,47}};assert(events==expected);
 c.clear();events.clear();c.replay(d,VK_NULL_HANDLE);assert(events.empty());CommandBindings::set(c.vertices,0,CommandBindings::Vertex{VkBuffer(3),4});c.replay(d,VK_NULL_HANDLE);assert(events.size()==1&&events[0]==(Event{0,0,7}));
 // Warm-state recording must allocate no tree nodes/callbacks; compare the old
 // exact map/capturing-callback pattern with identical bind/replay operations.
 capture=false;constexpr unsigned rounds=20000;std::map<uint64_t,std::function<void()>> old;auto owner=std::make_shared<int>(0);
 auto fillNew=[&]{c.clear();for(unsigned j=0;j<4;++j){CommandBindings::set(c.vertices,j,CommandBindings::Vertex{VkBuffer(10+j),j*64});CommandBindings::set(c.viewports,j,VkViewport{0,0,100,-100,0,1});}c.replay(d,VK_NULL_HANDLE);};fillNew();
 auto run=[&](auto fn){allocations=0;measureAllocations=true;auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<rounds;++i)fn();auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();measureAllocations=false;return std::make_pair(ns,allocations);};
 auto legacy=run([&]{old.clear();for(unsigned j=0;j<4;++j){old[0x20000+j]=[owner,j,b=VkBuffer(10+j),o=VkDeviceSize(j*64)]{vertex(VK_NULL_HANDLE,j,1,&b,&o);};old[0x40000+j]=[owner,j,v=VkViewport{0,0,100,-100,0,1}]{viewport(VK_NULL_HANDLE,j,1,&v);};}for(auto& entry:old)entry.second();});
 auto value=run(fillNew);assert(value.second==0);assert(legacy.second>0);
 std::cout<<"record+replay rounds="<<rounds<<" legacyNs="<<legacy.first<<" valueNs="<<value.first<<" legacyAllocations="<<legacy.second<<" valueAllocations="<<value.second<<'\n';
}
