#pragma once
#include "StereoCompiler.h"
#include "EternalVolumes.h"
#include "EternalLightGrid.h"
#include "EternalVk3d.h"
#include "EternalWaterBounds.h"
namespace argent::sfs {
struct ShaderOptions { bool project{}; bool broadcastStorageImages{}; uint32_t set{},binding{}; bool monoView{},computeStereo{true}; int indirectEye{-1}; bool screenSpaceUi{}; uint64_t volumeShader{},lightGridShader{},vk3dShader{}; bool nativeSampleLayer{}; uint64_t uiShader{}; bool nativeStorageWriteLayer{}; };
inline std::string stereoSource(const std::vector<uint32_t>& words,const ShaderOptions& options={}) {
    spirv_cross::Compiler inspect(words);auto model=inspect.get_execution_model();bool compute=model==spv::ExecutionModelGLCompute;
    if(!compute&&model!=spv::ExecutionModelVertex&&model!=spv::ExecutionModelFragment)throw std::runtime_error("Unsupported stage; no ray tracing transformation");
    if(options.broadcastStorageImages&&(!compute||options.computeStereo))throw std::runtime_error("Broadcast requires shared mono compute");
    const bool project=options.project;
    const auto volume=eternalVolumeRule(options.volumeShader);
    const auto grid=eternalLightGridRule(options.lightGridShader);
    const auto vk3d=eternalVk3dRule(options.vk3dShader);
    const bool vk3dUniform=vk3d&&vk3d->worldUniform;
    if(grid.pixels&&model!=spv::ExecutionModelFragment)throw std::runtime_error("Light grid correction requires material fragment stage");
    if(volume.uv&&model!=spv::ExecutionModelFragment&&!compute)throw std::runtime_error("Volume correction requires fragment or compute stage");
    if(project&&model!=spv::ExecutionModelVertex)throw std::runtime_error("Projection requires a vertex stage");
    uint32_t set=options.set,binding=options.binding;
    if(project||volume.uv||grid.pixels||vk3dUniform){
        auto resources=inspect.get_shader_resources();
        auto check=[&](const auto& list){for(auto& r:list)if(inspect.get_decoration(r.id,spv::DecorationDescriptorSet)==set&&inspect.get_decoration(r.id,spv::DecorationBinding)==binding)throw std::runtime_error("Projection binding collides with an existing descriptor");};
        check(resources.uniform_buffers);check(resources.storage_buffers);check(resources.sampled_images);check(resources.separate_images);check(resources.separate_samplers);check(resources.storage_images);check(resources.subpass_inputs);check(resources.acceleration_structures);
    }
    // Water's bindless material loops must not acquire an extra descriptor-size
    // query at each sample. Vulkan sampling clamps the array layer itself, as
    // used by Vk3D. Integer texel fetches retain their explicit layer guard.
    // The r160 reset identified this world-material shader exactly as well.
    // Filtered array sampling clamps layers natively; avoid adding descriptor
    // queries in its divergent material paths. Integer fetches keep the guard.
    const bool nativeSampleLayer=options.nativeSampleLayer||options.vk3dShader==0x24abb0e76a065289ull||options.vk3dShader==0xd300c0135fbca8b0ull;
    // This opt-in requires two-layer outputs. Eternal's exact water profile
    // has four such outputs; shared/broadcast shaders keep their size guard.
    const bool nativeStorageWriteLayer=options.nativeStorageWriteLayer||options.vk3dShader==0x24abb0e76a065289ull;
    if(nativeStorageWriteLayer&&(!compute||options.broadcastStorageImages))throw std::runtime_error("Direct storage eye layer requires non-broadcast compute");
    argent::sfs::StereoCompiler compiler(words,compute&&options.computeStereo,options.broadcastStorageImages,nativeSampleLayer,nativeStorageWriteLayer);auto glslOptions=compiler.get_common_options();glslOptions.version=460;glslOptions.vulkan_semantics=true;compiler.set_common_options(glslOptions);
    if(!compute)compiler.require_extension("GL_EXT_multiview");
    if(vk3d)compiler.require_extension("GL_EXT_nonuniform_qualifier");
    auto source=compiler.compile();std::string declarations;
    // Eternal's generated water mesh marks rejected vertices with clip.w=-inf.
    // A full matrix multiply turns 0 * -inf into NaN (including clip.z).
    // Preserve that native marker before applying any stereo/UI projection.
    const bool nativeRejectSentinel=project&&source.find("uintBitsToFloat(0xff800000u")!=std::string::npos;
    if(compute&&options.computeStereo)declarations="uint khSfsEye; uvec3 khSfsGlobalInvocationID; uvec3 khSfsWorkGroupID; uvec3 khSfsNumWorkGroups;\n";
    if(project||volume.uv||grid.pixels||vk3dUniform)declarations+="layout(set="+std::to_string(set)+", binding="+std::to_string(binding)+", std140) uniform ArgentEyeProjection { mat4 clipFromCenter[2]; vec4 eyeTranslation[2]; mat4 screenClip[2]; vec4 diagnostics; } argentProjection;\n";
    if(vk3d)applyEternalVk3d(source,*vk3d);
    if(options.vk3dShader==0x24abb0e76a065289ull)boundEternalWaterLists(source);
    if(volume.uv)correctEternalVolume(source,volume);
    if(grid.pixels)correctEternalLightGrid(source,grid);
    if(volume.uv||grid.pixels)declarations+=eternalVolumeHelper(compute?(options.computeStereo?"khSfsEye":"0"):"gl_ViewIndex");
    // Insert before the first declaration, after SPIRV-Cross's preprocessor block.
    size_t line=0,insert=source.size();while(line<source.size()){auto end=source.find('\n',line);if(end==std::string::npos)end=source.size();auto text=source.substr(line,end-line);auto first=text.find_first_not_of(" \t\r");if(first!=std::string::npos&&text[first]!='#'){insert=line;break;}line=end+1;}
    source.insert(insert,declarations);
    if((compute&&options.computeStereo)||project){auto main=source.find("void main()");if(main==std::string::npos)throw std::runtime_error("Missing GLSL main");source.replace(main,11,"void argentOriginalMain()");source+="\nvoid main() {\n";
        if(compute&&options.computeStereo&&options.indirectEye<0)source+="khSfsNumWorkGroups=gl_NumWorkGroups; khSfsNumWorkGroups.z/=2u;\nkhSfsEye=gl_WorkGroupID.z/khSfsNumWorkGroups.z;\nkhSfsWorkGroupID=gl_WorkGroupID; khSfsWorkGroupID.z%=khSfsNumWorkGroups.z;\nkhSfsGlobalInvocationID=gl_GlobalInvocationID; khSfsGlobalInvocationID.z-=khSfsEye*khSfsNumWorkGroups.z*gl_WorkGroupSize.z;\n";
        if(compute&&options.computeStereo&&options.indirectEye>=0)source+="khSfsEye="+std::to_string(options.indirectEye)+"u; khSfsNumWorkGroups=gl_NumWorkGroups; khSfsWorkGroupID=gl_WorkGroupID; khSfsGlobalInvocationID=gl_GlobalInvocationID;\n";
        source+="argentOriginalMain();\n";
        if(project){
            if(nativeRejectSentinel)source+="if (floatBitsToUint(gl_Position.w) == 0xff800000u) return;\n";
            if(options.screenSpaceUi){
                // Native GUI world-space flag, not distance: hand panels are
                // often closer than the old 8-unit screen-HUD threshold.
                int member=-1;uint32_t offset=0;
                switch(options.uiShader){
                 case 0xdc2d10822f8eda88ull:case 0x0749c071d7d1fdf5ull:
                 case 0x4f50b1f4caf20882ull:case 0x475b91f7adce5776ull:member=9;offset=132;break;
                 case 0xc8d657a2321cc9eeull:case 0x32fb81bae310c07dull:member=3;offset=48;break;
                }
                std::string screen="true"; // pure screen families have no world transform
                if(member>=0){
                 bool found=false;
                 for(const auto& r:compiler.get_shader_resources().uniform_buffers){
                  if(compiler.get_decoration(r.id,spv::DecorationDescriptorSet)!=1||compiler.get_decoration(r.id,spv::DecorationBinding)!=0)continue;
                  const auto& type=compiler.get_type(r.base_type_id);
                  if(type.member_types.size()<=size_t(member)||compiler.type_struct_member_offset(type,member)!=offset||compiler.get_type(type.member_types[member]).basetype!=spirv_cross::SPIRType::Float)throw std::runtime_error("Native GUI world flag contract mismatch");
                  auto name=compiler.get_name(r.id);if(name.empty())name="_"+std::to_string(r.id);
                  auto field=compiler.get_member_name(r.base_type_id,member);if(field.empty())field="_m"+std::to_string(member);
                  screen="!("+name+"."+field+" > 0.0)";found=true;
                 }
                 if(!found)throw std::runtime_error("Native GUI transform missing");
                 // Native render-to-texture override explicitly emits clip XY.
                 for(const auto& r:compiler.get_shader_resources().push_constant_buffers){
                  auto name=compiler.get_name(r.id);if(name.empty())name="_"+std::to_string(r.id);
                  auto field=compiler.get_member_name(r.base_type_id,0);if(field.empty())field="_m0";
                  screen="("+screen+" || "+name+"."+field+" > 0)";
                 }
                }
                source+="if ("+screen+") gl_Position=argentProjection.screenClip[gl_ViewIndex]*gl_Position; else ";
            }
            source+="{ vec4 originalClip = gl_Position; vec4 eyeClip = argentProjection.clipFromCenter[gl_ViewIndex]*originalClip+argentProjection.eyeTranslation[gl_ViewIndex];\n"
                "if (argentProjection.eyeTranslation[gl_ViewIndex].w != 0.0) {\n"
                " if (eyeClip.w > 0.000001) eyeClip = vec4(eyeClip.xy * (originalClip.w / eyeClip.w), originalClip.zw);\n"
                " else eyeClip = vec4(2.0,2.0,2.0,1.0);\n"
                "} gl_Position = eyeClip; }\n";
            if(options.screenSpaceUi)source+="if (argentProjection.diagnostics.y > 0.5 && argentProjection.diagnostics.w == 1.0) gl_Position=vec4(2.0,2.0,2.0,1.0);\n";
        }
        source+="}\n";
    }
    if(options.indirectEye < -1 || options.indirectEye > 1 || (options.indirectEye>=0&&(!compute||!options.computeStereo)))throw std::runtime_error("Invalid fixed-eye compute policy");
    if(options.monoView){size_t pos=0;while((pos=source.find("gl_ViewIndex",pos))!=std::string::npos){source.replace(pos,12,"0");++pos;}}
    return source;
}
}
