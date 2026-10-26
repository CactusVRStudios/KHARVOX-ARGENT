#pragma once
#include "../EternalDlssAbi.h"
#include "InputLayouts.h"
#include <array>
#include <cstring>
namespace argent::fg {
struct NativeWrite {Layout source;uint64_t serial{},order{};VkQueue queue{};};
inline bool currentNativeWrite(const NativeWrite& write,const dlss::Resource& resource,uint64_t serial,VkQueue queue){
 return serial&&queue&&write.serial==serial&&write.queue==queue&&write.source.image==resource.image&&covers(write.source.range,resource.range);
}
// idRenderSystemLocal::_motionVector[2], audited from image creation and the
// native NGX resource constructor. Names guard against stale object layouts.
template<class Read> bool nativeMotionSources(uintptr_t root,Read read,std::array<dlss::Resource,2>& out){
 out={};
 for(unsigned i=0;i<2;++i){
  uintptr_t object{},name{},alias{};uint32_t flags{};char actual[16]{};
  const char* expected=i?"_motionvector1":"_motionvector0";
  if(!read(root+0x360+i*8,&object,8)||!object||!read(object+8,&name,8)||!name||
     !read(name,actual,15)||std::strcmp(actual,expected)||
     !read(object+0x9c,&flags,4)||
     !read(object+0xc8,&alias,8)||alias!=object)return false;
  auto& r=out[i];
  if(!read(object+0xd0,&r.view,8)||!read(object+0xe8,&r.image,8)||
     !read(object+0x64,&r.width,4)||!read(object+0x68,&r.height,4)||
     !read(object+0x128,&r.format,4)||!r.view||!r.image||!r.width||!r.height||
     r.width>8192||r.height>8192||r.format!=VK_FORMAT_R16G16_SFLOAT)return false;
  // 0x100 is the resolution-array representation, not an unsupported alias.
  // Native resource constructors 1cc5a40 / 1d51fd0 dereference the selected
  // image at table+0x108+index*8 and the parallel view table at object+0xd0.
  if(flags&0x100){
   const auto table=reinterpret_cast<uintptr_t>(r.image),views=reinterpret_cast<uintptr_t>(r.view);int32_t index{},after{};
   if(!read(table,&index,4)||index<0||index>=32||
      !read(table+0x108+uintptr_t(index)*8,&r.image,8)||!r.image||
      !read(views+uintptr_t(index)*8,&r.view,8)||!r.view||
      !read(table,&after,4)||after!=index)return false;
  }
  r.range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};
 }
 return out[0].image!=out[1].image;
}
}
