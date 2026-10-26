#pragma once
#include <cstdint>
namespace argent::fg {
struct Identity {uint64_t pair{};uint32_t width{},height{};bool complete{},gameplay{},reset{};};
inline bool consecutive(const Identity& a,const Identity& b){return a.complete&&b.complete&&a.gameplay&&b.gameplay&&!b.reset&&b.pair==a.pair+1&&a.width==b.width&&a.height==b.height;}
}
