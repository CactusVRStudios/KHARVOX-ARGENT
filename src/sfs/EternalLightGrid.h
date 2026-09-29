#pragma once
#include "EternalVolumes.h"
namespace argent::sfs {
// Exact captured identities, reviewed in docs/eternal-light-grid-audit.json.
// No runtime shape matching: unknown permutations remain unchanged.
struct LightGridRule {const char* pixels;const char* x;const char* y;const char* depth;const char* inverseViewport;const char* anchor;const char* small;};
inline LightGridRule eternalLightGridRule(uint64_t hash){switch(hash){
#include "EternalAmdLightGrid.inc"
 case 0xaec77811cb9b52bfull:return {"_1034._m17.xy","_2464","_2469","_2473","_780._m6.xy","float _2479 = log2(","_2514"};
 case 0x80bc40008f2ff283ull:return {"_918._m17.xy","_2719","_2724","_2728","_711._m6.xy","float _2734 = log2(","_2769"};
 case 0xad35ef5cb2b7cdfbull:return {"_938._m17.xy","_1436","_1441","_1445","_813._m18.xy","float _1451 = log2(",""};
 case 0x64ebec74e1eb8c68ull:return {"_1291._m17.xy","_1785","_1790","_1794","_951._m31.xy","float _1800 = log2(",""};
 case 0x0f1a84c056b88ae7ull:return {"_926._m17.xy","_1903","_1908","_1912","_648._m18.xy","float _1918 = log2(","_1953"};
 case 0xb243e6c0f4fb279eull:return {"_1300._m17.xy","_1794","_1799","_1803","_960._m31.xy","float _1809 = log2(",""};
 case 0x26e8e2bec0886394ull:return {"_1014._m17.xy","_2825","_2830","_2834","_807._m6.xy","float _2840 = log2(","_2875"};
 case 0xb7175ee2a9b2a9aaull:return {"_984._m17.xy","_1479","_1484","_1488","_812._m23.xy","float _1494 = log2(",""};
 case 0xbe6c710826839291ull:return {"_1006._m17.xy","_1595","_1600","_1604","_750._m20.xy","float _1610 = log2(",""};
 case 0x6d73118adc79bc52ull:return {"_793._m17.xy","_1551","_1556","_1560","_641._m6.xy","float _1566 = log2(","_1601"};
 case 0x71a00ce119ea7fedull:return {"_1131._m17.xy","_2587","_2592","_2596","_876._m18.xy","float _2602 = log2(","_2637"};
 case 0xa8296ffceb455ef2ull:return {"_843._m17.xy","_1324","_1329","_1333","_721._m18.xy","float _1339 = log2(",""};
 case 0xc8f0f3dbffff83aeull:return {"_1130._m17.xy","_2585","_2590","_2594","_876._m6.xy","float _2600 = log2(","_2635"};
 case 0x040c58e6b3a70d01ull:return {"_772._m17.xy","_1189","_1194","_1198","_654._m16.xy","float _1204 = log2(",""};
 case 0xd300c0135fbca8b0ull:return {"_1159._m17.xy","_2603","_2608","_2612","_813._m4.xy","float _2618 = log2(","_2652"};
 case 0xe575f378064db76cull:return {"_1056._m17.xy","_2422","_2427","_2431","_802._m6.xy","float _2437 = log2(","_2472"};
 case 0x74ea944dd9288b59ull:return {"_843._m17.xy","_1325","_1330","_1334","_721._m18.xy","float _1340 = log2(",""};
 case 0x312790d919ff4509ull:return {"_1057._m17.xy","_2486","_2491","_2495","_802._m18.xy","float _2501 = log2(","_2536"};
 case 0x87a36586bb833139ull:return {"_1159._m17.xy","_2448","_2453","_2457","_813._m4.xy","float _2463 = log2(","_2498"};
 case 0x8258cfbc7da01926ull:return {"_1034._m17.xy","_2478","_2483","_2487","_780._m6.xy","float _2493 = log2(","_2528"};
 case 0x0983b769a1137364ull:return {"_1057._m17.xy","_2426","_2431","_2435","_802._m18.xy","float _2441 = log2(","_2476"};
 case 0x0350f3b31d3084fbull:return {"_960._m17.xy","_1380","_1385","_1389","_679._m24.xy","float _1395 = log2(",""};
 case 0x0e86d471550c01dbull:return {"_1300._m17.xy","_1794","_1799","_1803","_960._m31.xy","float _1809 = log2(",""};
 case 0xd498e6e2f55f24b9ull:return {"_925._m17.xy","_1901","_1906","_1910","_648._m6.xy","float _1916 = log2(","_1950"};
 case 0xcd0ae79cd15def31ull:return {"_811._m17.xy","_2613","_2618","_2622","_686._m6.xy","float _2628 = log2(",""};
 case 0x3f1104e0ac5a69e4ull:return {"_883._m17.xy","_1641","_1646","_1650","_712._m6.xy","float _1656 = log2(","_1691"};
 case 0xc2f5df1b57cc1f3full:return {"_822._m17.xy","_1194","_1199","_1203","_559._m29.xy","float _1209 = log2(",""};
 case 0x37d49dd5b6038002ull:return {"_1035._m17.xy","_2480","_2485","_2489","_780._m18.xy","float _2495 = log2(","_2530"};
 case 0x2e418a20794234bcull:return {"_822._m17.xy","_1193","_1198","_1202","_559._m21.xy","float _1208 = log2(",""};
 case 0x93cd777d2892800bull:return {"_1035._m17.xy","_2540","_2545","_2549","_780._m18.xy","float _2555 = log2(","_2590"};
 case 0x94c21d06faa45ce5ull:return {"_761._m17.xy","_1018","_1023","_1027","_555._m8.xy","float _1033 = log2(","_1068"};
 case 0x691c6d2a5a40b9fdull:return {"_977._m17.xy","_2133","_2138","_2142","_721._m20.xy","float _2148 = log2(",""};
 case 0x2cc4b359f36db0abull:return {"_1159._m17.xy","_2589","_2594","_2598","_813._m4.xy","float _2604 = log2(","_2638"};
 case 0x7f959e5dca1dc4c9ull:return {"_1035._m17.xy","_2467","_2472","_2476","_780._m18.xy","float _2482 = log2(","_2517"};
 case 0x1271d4fa179fdd5full:return {"_960._m17.xy","_1375","_1380","_1384","_679._m12.xy","float _1390 = log2(",""};
 case 0x5f7ab97152cd8215ull:return {"_984._m17.xy","_1479","_1484","_1488","_812._m23.xy","float _1494 = log2(",""};
 case 0x43d63dccdb9273c7ull:return {"_846._m17.xy","_1344","_1349","_1353","_721._m18.xy","float _1359 = log2(",""};
 case 0x03aabc2ec5eaf73bull:return {"_918._m17.xy","_2719","_2724","_2728","_711._m6.xy","float _2734 = log2(","_2769"};
 case 0xe2e11e1b054ee934ull:return {"_875._m17.xy","_1365","_1370","_1374","_667._m16.xy","float _1380 = log2(","_1415"};
 case 0x60f1e0f7839f974dull:return {"_1035._m17.xy","_2527","_2532","_2536","_780._m18.xy","float _2542 = log2(","_2577"};
 default:return {};
}}
inline void correctEternalLightGrid(std::string& source,LightGridRule rule){
 if(!rule.pixels)return;
 const auto pos=source.find(rule.anchor);
 if(pos==std::string::npos||source.find(rule.anchor,pos+1)!=std::string::npos)throw std::runtime_error("Eternal light grid anchor changed");
 // Only the shared light-list addresses move to center-camera coordinates.
 // gl_FragCoord, eye-local depth, normals and material texture UVs are untouched.
 std::string correction="vec2 argentGridPixel = "+std::string(rule.pixels)+";\n    if (argentProjection.diagnostics.x > 0.5) argentGridPixel = clamp(argentCenterVolumeUv("+rule.pixels+" * "+rule.inverseViewport+", "+rule.depth+") / "+rule.inverseViewport+", vec2(0.0), vec2(1.0) / "+rule.inverseViewport+" - vec2(1.0));\n    ";
 correction+=std::string(rule.x)+" = uint(argentGridPixel.x) / 256u;\n    "+rule.y+" = uint(argentGridPixel.y) / 256u;\n    ";
 source.insert(pos,correction);
 if(*rule.small){
  std::string before="uvec2 "+std::string(rule.small)+" = uvec2("+rule.pixels+");";
  auto small=source.find(before);
  if(small==std::string::npos||source.find(before,small+1)!=std::string::npos)throw std::runtime_error("Eternal small light grid anchor changed");
  source.replace(small,before.size(),"uvec2 "+std::string(rule.small)+" = uvec2(argentGridPixel);");
 }
}
}
