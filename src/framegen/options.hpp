#pragma once
#include <windows.h>
#include <string>
namespace argent::fg {
inline bool requested(){static const bool on=[] {char b[8]{};GetEnvironmentVariableA("ARGENT_FRAMEGEN_2X",b,8);return std::string(b)=="1";}();return on;}
inline bool communityRequested(){char b[8]{};GetEnvironmentVariableA("ARGENT_FG_COMMUNITY",b,8);return std::string(b)!="0";}
}
