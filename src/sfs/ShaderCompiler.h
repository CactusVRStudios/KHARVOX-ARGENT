#pragma once
#include "StereoSource.h"
namespace argent::sfs {
// KHARVOX glslang backend, with Eternal descriptor positions supplied explicitly.
std::vector<uint32_t> compileStereoShader(const std::vector<uint32_t>& words,const ShaderOptions& options={});
std::vector<uint32_t> compileGlsl(const std::string& source,spv::ExecutionModel model);
}
