#pragma once
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
namespace argent::revenant {
// Native usercmd_t and idDemonPlayer_Revenant::RevenantInput contracts.
inline constexpr size_t commandSize=0x98;
struct Handle {uint32_t generation{},cached{};uintptr_t pointer{};};
inline bool resolved(Handle h){return h.pointer&&h.generation==h.cached&&h.generation!=0x1fffffe;}
template<class Read> uintptr_t detect(uintptr_t player,uintptr_t playerType,uintptr_t demonType,Read read){
 uintptr_t type{};Handle controlled{},back{};unsigned char flags[2]{};
 if(!player||!playerType||!demonType||!read(player,&type,8)||type!=playerType||
    !read(player+0x88b0,&controlled,sizeof(controlled))||!resolved(controlled)||
    !read(controlled.pointer,&type,8)||type!=demonType||
    !read(controlled.pointer+0x20a48,flags,2)||flags[0]!=1||flags[1]!=1||
    !read(controlled.pointer+0x20a08,&back,sizeof(back))||!resolved(back)||back.pointer!=player)return 0;
 Handle again{};
 return read(player+0x88b0,&again,sizeof(again))&&again.pointer==controlled.pointer&&
  again.generation==controlled.generation&&resolved(again)?controlled.pointer:0;
}
inline bool viewAngles(const float* basis,float* desired){
 if(!basis||!desired)return false;
 for(int i=0;i<9;++i)if(!std::isfinite(basis[i]))return false;
 const float xy=std::hypot(basis[0],basis[1]);
 if(xy<.00001f)return false;
 desired[0]=-std::atan2(basis[2],xy)*57.2957795131f;desired[1]=std::atan2(basis[1],basis[0])*57.2957795131f;desired[2]=0;
 return true;
}
inline bool encodeView(const float* basis,const float* delta,uint16_t* angles){
 float desired[3]{};if(!delta||!angles||!viewAngles(basis,desired))return false;
 for(int i=0;i<3;++i){if(!std::isfinite(delta[i]))return false;}
 for(int i=0;i<3;++i)angles[i]=uint16_t(int32_t(std::lround(std::remainder(desired[i]-delta[i],360.f)*65536.f/360.f)));
 return true;
}
inline uint64_t actionButtons(uint64_t native,const std::array<uint64_t,5>& bindings,bool fire,bool mode,bool dash,bool fly){
 uint64_t mask=0;for(auto b:bindings)mask|=b;
 return (native&~mask)|(fire?bindings[0]:0)|(mode?bindings[1]:0)|(dash?bindings[2]:0)|(fly?(bindings[3]|bindings[4]):0);
}
}
