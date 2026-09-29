#include "ShaderCompiler.h"
#include "AmdInterface.h"
#include "NonuniformSamplers.h"
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <memory>
#include <mutex>
namespace argent::sfs {
std::vector<uint32_t> compileStereoShader(const std::vector<uint32_t>& words,const ShaderOptions& options){
    // r165 control: retain exact profile corrections, but withdraw the r164
    // corpus-wide decoration pass until its driver effects are isolated.
    auto result=compileGlsl(stereoSource(words,options),spirv_cross::Compiler(words).get_execution_model());
    // Only reviewed AMD material rules opt in. Their bindless image indices
    // are already marked; the final sampler operand must retain that marker.
    const auto* rule=eternalVk3dRule(options.vk3dShader);
    if(rule&&rule->preserveNonuniform)preserveNonuniformSamplers(result);
    return result;
}
std::vector<uint32_t> compileGlsl(const std::string& source,spv::ExecutionModel model){
    const AmdInterface amd(model==spv::ExecutionModelFragment?source:std::string{});
    const auto& frontendSource=model==spv::ExecutionModelFragment?amd.source:source;
    static std::once_flag initialization;
    std::call_once(initialization,[]{if(!glslang_initialize_process())throw std::runtime_error("glslang initialization failed");});
    std::vector<uint32_t> result;
    glslang_input_t input{};input.language=GLSLANG_SOURCE_GLSL;
    input.stage=model==spv::ExecutionModelVertex?GLSLANG_STAGE_VERTEX:model==spv::ExecutionModelFragment?GLSLANG_STAGE_FRAGMENT:GLSLANG_STAGE_COMPUTE;
    input.client=GLSLANG_CLIENT_VULKAN;input.client_version=GLSLANG_TARGET_VULKAN_1_1;
    input.target_language=GLSLANG_TARGET_SPV;input.target_language_version=GLSLANG_TARGET_SPV_1_3;
    input.code=frontendSource.c_str();input.default_version=450;input.default_profile=GLSLANG_NO_PROFILE;
    input.messages=static_cast<glslang_messages_t>(GLSLANG_MSG_SPV_RULES_BIT|GLSLANG_MSG_VULKAN_RULES_BIT);input.resource=glslang_default_resource();
    std::unique_ptr<glslang_shader_t,decltype(&glslang_shader_delete)> shader(glslang_shader_create(&input),glslang_shader_delete);
    if(!shader)throw std::runtime_error("SFS: shader allocation failed");
    if(!glslang_shader_preprocess(shader.get(),&input)||!glslang_shader_parse(shader.get(),&input))
        throw std::runtime_error(std::string("SFS shader compile: ")+glslang_shader_get_info_log(shader.get())+"\n"+source);
    std::unique_ptr<glslang_program_t,decltype(&glslang_program_delete)> program(glslang_program_create(),glslang_program_delete);
    if(!program)throw std::runtime_error("SFS: program allocation failed");
    glslang_program_add_shader(program.get(),shader.get());
    if(!glslang_program_link(program.get(),input.messages))throw std::runtime_error(glslang_program_get_info_log(program.get()));
    glslang_program_SPIRV_generate(program.get(),input.stage);
    const auto size=glslang_program_SPIRV_get_size(program.get());if(!size)throw std::runtime_error("SFS: SPIR-V generation failed");
    result.resize(size);glslang_program_SPIRV_get(program.get(),result.data());amd.restoreLocations(result);
    return result;
}
}
