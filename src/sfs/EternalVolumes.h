#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
namespace argent::sfs {
struct VolumeRule {const char* uv;const char* depth;const char* anchor;bool material{},waterGeometry{},waterShading{},atmosphere{},amdAtmosphere{};const char* rayCall{};const char* rayFunction{};const char* rayUv{};};
inline VolumeRule withVolumeRay(VolumeRule rule,const char* call,const char* function,const char* uv){rule.rayCall=call;rule.rayFunction=function;rule.rayUv=uv;return rule;}
// Exact identities from the Vk3D fragment/compute source audits. These
// sample a shared center-camera volume; they are not per-eye 2D render targets.
inline VolumeRule eternalVolumeRule(uint64_t hash){
 switch(hash){
 // Cubemap surface with an opacity texture: both froxel lookup and spherical
 // atmosphere reconstruct from _323. Convert once after eye depth is read,
 // before the fog UV is copied, so the later ray sees the same center UV.
 case 0x2f28ea0af971c44bull:
 case 0xcbed08c426444de3ull:return {"_323","_342","vec2 _358 = _323 + vec2(0.0);"};

 // r281: reviewed translucent/emissive consumers in the r279 capture.
 // Convert the shared parent UV before its froxel copy and spherical ray;
 // earlier eye-depth, refraction, material and alpha computations stay native.
 case 0x929b3a127e479f1eull:return {"_1148","_1164","vec2 _1179 = _1148 + vec2(0.0);"};
 case 0x9341f6cd594c0e88ull:return {"_1148","_1164","vec2 _1179 = _1148 + vec2(0.0);"};
 case 0x272c4bf87f958132ull:return {"_836","_852","vec2 _867 = _836 + vec2(0.0);"};
 case 0x676701a8a44af14bull:return {"_492","_511","vec2 _527 = _492 + vec2(0.0);"};
 case 0xff896435dcf7729eull:return {"_559","_575","vec2 _591 = _559 + vec2(0.0);"};
 case 0xaf863fdbab5ef66dull:return {"_1039","_1055","vec2 _1070 = _1039 + vec2(0.0);"};
 case 0xe0c75045d413957dull:return {"_1130","_1146","vec2 _1161 = _1130 + vec2(0.0);"};
 case 0x8cd331a52413cad7ull:return {"_1130","_1146","vec2 _1161 = _1130 + vec2(0.0);"};
 case 0x252e77f4f532542full:return {"_318","_338","vec2 _354 = _318 + vec2(0.0);"};

 // r282: independently reviewed AMD atmosphere consumers.
 case 0xfdbe852191fcd430ull:return {"_1273","_1289","vec2 _1304 = _1273 + vec2(0.0);"};
 case 0xbbe3e62a9b6bd445ull:return {"_1273","_1289","vec2 _1304 = _1273 + vec2(0.0);"};
 case 0x94024ca5d565a3edull:return {"_961","_977","vec2 _992 = _961 + vec2(0.0);"};
 case 0x17be3ba41ca76733ull:return {"_684","_700","vec2 _716 = _684 + vec2(0.0);"};
 case 0x66b911a38af36c60ull:return {"_1255","_1271","vec2 _1286 = _1255 + vec2(0.0);"};
 case 0xa3a5694076f2a831ull:return {"_1255","_1271","vec2 _1286 = _1255 + vec2(0.0);"};
 case 0x971c804446f72158ull:return {"_1836","_1831","vec3 _1868 = textureLod(sampler3D(",false,false,false,true,true};

 // RX 6750 XT / Store capture (r254): center-camera fog volume consumers.
 // The first two are source-equivalent rain passes (AMD min/max intrinsics).
 // The others retain native eye depth/soft-particle alpha; only the shared
 // 3D volume UV is converted after depth reconstruction.
 case 0x90c822113652399bull:
 case 0x64b6c72f0500b6e6ull:return {"_342","_380","vec3 _412 = _62(_342);"};
 case 0x65caa09a41470219ull:return withVolumeRay({"_567","_562","vec3 _600 = textureLod(sampler3D("},"_50(_373)","_50","_373");
 case 0x9130f1d8f8a3d889ull:return {"_1164","_1180","vec2 _1195 = _1164 + vec2(0.0);"};
 case 0x83cd64dfce73a7b9ull:return withVolumeRay({"_602","_587","vec3 _636 = textureLod(sampler3D("},"_56(_571)","_56","_571");
 // Remaining Vk3D rain/water/RTX-flare corrections. Read per-eye depth first,
 // then convert reconstruction/volume UVs to the shared center-camera space.
 case 0xfa78e0be1164ef06ull:
 case 0x2118df348b9c11d6ull:return {"_342","_380","vec3 _412 = _62(_342);"};
 case 0x7349c1f96fbc381aull:return withVolumeRay({"_464","_458","vec3 _498 = textureLod(sampler3D("},"_47(_288)","_47","_288");
 case 0xa14db8d1c3a2ca43ull:return {"_1621","_1616","vec3 _1653 = textureLod(sampler3D(",false,false,false,true};
 case 0x24abb0e76a065289ull:return {"_845","_878","vec3 _1275 = _126(_845, _878);",false,false,true};
 // Water mesh reconstruction consumes an eye depth texture, then uses the
 // engine's center-camera inverse matrix. Undo the eye mapping first.
 case 0x57446083630d237cull:return {"_355","","vec3 _426 = _407;",false,true};
 // Vk3D 9d4cf14d31344060 material contract, manually audited on both captured
 // permutations. Preserve their own discard and nonuniform indexing code.
 case 0xb243e6c0f4fb279eull:
 case 0x0e86d471550c01dbull:return {"_5291","_5286","vec3 _5322 = textureLod(sampler3D(",true};
 case 0x51ac0baab3cafdbcull:return withVolumeRay({"_575","_570","vec3 _608 = textureLod(sampler3D("},"_50(_382)","_50","_382");
 case 0x3b62f61430a414f2ull:return withVolumeRay({"_843","_827","vec3 _877 = textureLod(sampler3D("},"_47(_811)","_47","_811");
 case 0x5dd52f1a139b6d51ull:return {"_778","_762","vec3 _810 = textureLod(sampler3D("};
 case 0xe3fc861a228d92dbull:return withVolumeRay({"_640","_624","vec3 _672 = textureLod(sampler3D("},"_50(_606)","_50","_606");
 case 0xde354aa23fae72c9ull:return withVolumeRay({"_1162","_1147","vec3 _1193 = textureLod(sampler3D("},"_42(_1131)","_42","_1131");
 case 0x6dc34fa808e94b8dull:return {"_779","_763","vec3 _813 = textureLod(sampler3D("};
 // r216: waterfall spray 48d473f4ab87ad6d uses the pre-r214 path
 // while the reported brightness pulsation is isolated.
 default:return {};
 }
}
inline void correctEternalVolume(std::string& source,VolumeRule rule){
 if(!rule.uv)return;
 const auto pos=source.find(rule.anchor);
 if(pos==std::string::npos||source.find(rule.anchor,pos+1)!=std::string::npos)throw std::runtime_error("Eternal volume anchor changed");
 if(rule.waterGeometry){
  source.insert(pos,
   "if (argentProjection.diagnostics.y > 0.5 && argentProjection.diagnostics.w != 3.0) {\n"
   " float argentWaterClipW = 1.0 / dot(vec4(_407, 1.0), _340._m7);\n"
   " vec2 argentWaterCenterUv = argentCenterVolumeUv(_355, argentWaterClipW);\n"
   " _407.xy = vec2(argentWaterCenterUv.x * 2.0 - 1.0, 1.0 - argentWaterCenterUv.y * 2.0);\n"
   "}\n        ");
  // Native history reprojects through the previous CENTER camera. The
  // position history is now per-eye; no previous-eye transform is supplied
  // by this shader interface. Use current displaced positions in stereo.
  const std::string begin="vec3 _953 = _447;";
  const std::string end="vec3 _1067 = mix(_983.xyz, _421.xyz, vec3(_1027));";
  auto a=source.find(begin),b=source.find(end);
  if(a==std::string::npos||b==std::string::npos||b<a||source.find(begin,a+1)!=std::string::npos||source.find(end,b+1)!=std::string::npos)throw std::runtime_error("Water position history anchor changed");
  auto history=source.substr(a,b+end.size()-a);history.replace(history.size()-end.size(),end.size(),"_1067 = mix(_983.xyz, _421.xyz, vec3(_1027));");
  source.replace(a,b+end.size()-a,"vec3 _1067 = _421.xyz;\n        if (argentProjection.diagnostics.y <= 0.5 || argentProjection.diagnostics.w == 3.0) {\n        "+history+"\n        }\n");
  return;
 }
 // KHARVOX center-UV conversion, including FOV scale and metric eye offset.
 // Leave eye-local depth sampling and alpha/depth tests at their original UVs.
 source.insert(pos,"if (argentProjection.diagnostics.w != 3.0) "+std::string(rule.uv)+" = argentCenterVolumeUv("+rule.uv+", "+rule.depth+");\n    ");
 // These resolve and translucent variants have a separate ray input.
 // Correct only that function argument: parent UV remains eye-local for the
 // later color/depth samples, while the existing jittered froxel UV is retained.
 if(rule.rayCall){
  const auto at=source.find(rule.rayCall);
  if(at==std::string::npos||source.find(rule.rayCall,at+1)!=std::string::npos)throw std::runtime_error("Shared atmosphere ray anchor changed");
  source.replace(at,std::string(rule.rayCall).size(),std::string(rule.rayFunction)+"((argentProjection.diagnostics.w != 3.0) ? argentCenterVolumeUv("+rule.rayUv+", "+rule.depth+") : "+rule.rayUv+")");
 }
 if(rule.atmosphere){
  // The optional flat sky background is a center-camera image, unlike the
  // per-eye scene/depth inputs. Undo the asymmetric eye projection BEFORE
  // its native aspect correction, and transform the explicit LOD gradients.
  // A sky direction has no IPD translation (infinite distance).
  const std::string skyUv=rule.amdAtmosphere?"_608":"_618";
  const std::string sky=rule.amdAtmosphere?"_706":"_716";
  const std::string aspect=rule.amdAtmosphere?"_701":"_711";
  const std::string ratio=rule.amdAtmosphere?"_698":"_708";
  const std::string dy=rule.amdAtmosphere?"_715":"_725";
  const std::string dx=rule.amdAtmosphere?"_719":"_729";
  const std::string pixel=rule.amdAtmosphere?"_613._m0.w":"_623._m0.w";
  const std::string skyBegin="vec2 "+sky+" = "+skyUv+";";
  const std::string skyEnd="vec2 "+dx+" = vec2("+dy+".y / "+ratio+", 0.0);";
  const auto begin=source.find(skyBegin),end=source.find(skyEnd);
  if(begin==std::string::npos||end==std::string::npos||end<begin||source.find(skyBegin,begin+1)!=std::string::npos||source.find(skyEnd,end+1)!=std::string::npos)throw std::runtime_error("Sky background contract changed");
  source.replace(begin,end+skyEnd.size()-begin,
   "vec2 argentSkyUv = "+skyUv+";\n"
   "            vec2 argentSkyScale = vec2(1.0);\n"
   "            if (argentProjection.diagnostics.y > 0.5 && argentProjection.diagnostics.w != 3.0) {\n"
   "                argentSkyUv = argentCenterSkyUv(argentSkyUv);\n"
   "                argentSkyScale = argentCenterSkyUv(vec2(1.0)) - argentCenterSkyUv(vec2(0.0));\n"
   "            }\n"
   "            vec2 "+sky+" = argentSkyUv;\n"
   "            "+sky+".x = 0.5 + ("+aspect+" * (argentSkyUv.x - 0.5));\n"
   "            vec2 "+dy+" = vec2(0.0, "+pixel+" * argentSkyScale.y);\n"
   "            vec2 "+dx+" = vec2("+pixel+" / "+ratio+" * argentSkyScale.x, 0.0);");
  // The spherical atmosphere LUT and sun use a second UV, separate from
  // the froxel UV above. Reconstruct with the same center-camera basis;
  // keep all eye-local depth/color sampling untouched.
  const std::string ray=rule.amdAtmosphere?"vec3 _1945 = _77(_619);":"vec3 _1730 = _77(_629);";
  auto at=source.find(ray);
  if(at==std::string::npos||source.find(ray,at+1)!=std::string::npos)throw std::runtime_error("Atmosphere ray anchor changed");
  const std::string uv=rule.amdAtmosphere?"_619":"_629";
  const std::string depth=rule.amdAtmosphere?"_1831":"_1616";
  const std::string result=rule.amdAtmosphere?"_1945":"_1730";
  source.replace(at,ray.size(),
   "vec2 argentAtmosphereUv = "+uv+";\n"
   "        if (argentProjection.diagnostics.w != 3.0) argentAtmosphereUv = argentCenterVolumeUv("+uv+", "+depth+");\n"
   "        vec3 "+result+" = _77(argentAtmosphereUv);");
 }
 if(rule.waterShading){
  // The water compute pass consumes the same center-camera coarse lists as
  // materials (decals, lights AND reflection probes). _m2.xy is the full
  // viewport; _m3.xy is FFT/grid resolution and _m3.zw is half-width water
  // sampling. _1535 already expands the checkerboard X coordinate.
  const std::string anchor="float _1595 = log2(";
  const auto grid=source.find(anchor);
  if(grid==std::string::npos||source.find(anchor,grid+1)!=std::string::npos)throw std::runtime_error("Water light grid anchor changed");
  source.insert(grid,
   "if (argentProjection.diagnostics.x > 0.5) {\n"
   " vec2 argentWaterGridPixel = clamp(argentCenterVolumeUv(_1547._m17.xy / _641._m2.xy, _1589) * _641._m2.xy, vec2(0.0), _641._m2.xy - vec2(1.0));\n"
   " _1580 = uint(argentWaterGridPixel.x) / 256u;\n"
   " _1585 = uint(argentWaterGridPixel.y) / 256u;\n"
   "}\n    ");
 }
 if(rule.material){
  correctEternalVolume(source,{"_5530","_5515","vec3 _5560 = textureLod(sampler3D("});
  correctEternalVolume(source,{"_7133","_7127","vec3 _7163 = textureLod(sampler3D("});
  const std::string anchor="vec2 _5049 = _5002.xy / vec2(_5002.w);";
  const auto refraction=source.find(anchor);
  if(refraction==std::string::npos||source.find(anchor,refraction+1)!=std::string::npos)throw std::runtime_error("Eternal material refraction anchor changed");
  source.insert(refraction,"_5002 = argentEyeTextureProjection(_5002);\n            ");
 }
}
inline std::string eternalVolumeHelper(const std::string& eye="gl_ViewIndex"){return
 "vec2 argentCenterSkyUv(vec2 uv) {\n"
 " mat4 m = argentProjection.clipFromCenter["+eye+"];\n"
 " return (((uv * 2.0 - 1.0) - m[3].xy) / vec2(m[0][0],m[1][1]) + 1.0) * 0.5;\n}\n"
 "vec2 argentCenterVolumeUv(vec2 uv, float depth) {\n"
 " mat4 m = argentProjection.clipFromCenter["+eye+"];\n"
 " vec2 shift = argentProjection.eyeTranslation["+eye+"].xy / max(abs(depth), 0.000001);\n"
 " float wScale = 1.0 + argentProjection.eyeTranslation["+eye+"].w / max(abs(depth), 0.000001);\n"
 " return (((uv * 2.0 - 1.0) * wScale - m[3].xy - shift) / vec2(m[0][0],m[1][1]) + 1.0) * 0.5;\n}\n"
 "vec4 argentEyeTextureProjection(vec4 uvClip) {\n"
 " vec4 clip = vec4(uvClip.xy * 2.0 - vec2(uvClip.w), 0.0, uvClip.w);\n"
 " clip = argentProjection.clipFromCenter["+eye+"] * clip + argentProjection.eyeTranslation["+eye+"];\n"
 " if (argentProjection.eyeTranslation["+eye+"].w != 0.0) clip.xy *= uvClip.w / max(clip.w, 0.000001);\n"
 " return vec4((clip.xy + vec2(uvClip.w)) * 0.5, uvClip.zw);\n}\n";
}
}
