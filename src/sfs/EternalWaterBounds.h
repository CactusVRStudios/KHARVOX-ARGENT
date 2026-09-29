#pragma once
#include "EternalVk3d.h"
namespace argent::sfs {
// Captured d9b3c836c4f2a473 compute module (runtime hash 24abb0e76a065289).
// Each coarse cluster is 64 uints: one count and at most 63 indices. Validate
// consumers before the index reaches a UBO or bindless texture descriptor.
// Valid native lists are unchanged, including in cinematic/identity mode.
inline void boundEternalWaterLists(std::string& source){
 const Vk3dRule bounds{0,false,{
  {"uint _1635 = _1639._m0[_1610 * 64u];","uint _1635 = _1610 < uint(_1639._m0.length()) / 64u ? _1639._m0[_1610 * 64u] : 0u;"},
  {"uint _2715 = _2719._m0[_1610 * 64u];","uint _2715 = _1610 < uint(_2719._m0.length()) / 64u ? _2719._m0[_1610 * 64u] : 0u;"},
  {"uint _3154 = _3158._m0[_1610 * 64u];","uint _3154 = _1610 < uint(_3158._m0.length()) / 64u ? _3158._m0[_1610 * 64u] : 0u;"},
  {"uint _1646 = _1635 & 255u;","uint _1646 = min(_1635 & 255u, 63u);"},
  {"uint _2724 = _2715 & 255u;","uint _2724 = min(_2715 & 255u, 63u);"},
  {"uint _3163 = _3154 & 255u;","uint _3163 = min(_3154 & 255u, 63u);"},
  {"int _1696 = 2;","if (_1687 >= 1024u) continue;\n        int _1696 = 2;"},
  {"float _2787 = 0.0039215688593685626983642578125;","if (_2778 >= 1024u) continue;\n            float _2787 = 0.0039215688593685626983642578125;"},
  {"int _3199 = 4;","if (_3190 >= 1024u) continue;\n        int _3199 = 4;"},
  {"uint _1927 = _1917 + 1u;","uint _1927 = _1917 + 1u;\n        if (_1917 >= 34816u || _1927 >= 34816u) continue;"},
  {"uint _2068 = _1917 + 2u;","uint _2068 = _1917 + 2u;\n            if (_2068 >= 34816u) continue;"},
  {"uint _3542 = _3538 + 0u;","uint _3542 = _3538 + 0u;\n                    if (_3542 >= 34816u) continue;"},
  {"int _3712 = 5;","if ((_3316 >> 22u) + _3670 >= 819u) break;\n                        int _3712 = 5;"},
  {"int _3862 = 5;","if ((_3316 >> 22u) + _3670 >= 819u) continue;\n            int _3862 = 5;"},
 }};
 applyEternalVk3d(source,bounds);
}
}
