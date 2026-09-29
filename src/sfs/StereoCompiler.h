#pragma once
#include <spirv_glsl.hpp>
#include <stdexcept>
#include <vector>
namespace argent::sfs {
using namespace spirv_cross;
class StereoCompiler final:public CompilerGLSL {
    bool compute_{};
    bool computeStage_{};
    bool broadcastWrites_{};
    bool nativeSampleLayer_{};
    bool nativeStorageWriteLayer_{};
    static bool promote(const SPIRType& type){return type.image.dim==spv::Dim2D&&!type.image.arrayed;}
    std::string eye()const{return computeStage_?(compute_?"khSfsEye":"0"):"gl_ViewIndex";}
    uint32_t coordinate(uint32_t source,bool integer,const std::string& layer){
        auto type=expression_type(source);type.vecsize=3;type.basetype=integer?SPIRType::Int:SPIRType::Float;
        const auto typeId=ir.increase_bound_by(2),id=typeId+1;
        set<SPIRType>(typeId,type);
        const auto text=(integer?"ivec3(ivec2(":"vec3(vec2(")+to_expression(source)+"), "+layer+")";
        set<SPIRExpression>(id,text,typeId,true);
        inherit_expression_dependencies(id,source);
        return id;
    }
    std::string sampledLayer(const TextureFunctionBaseArguments& args){
        // Sampling/gather clamp an array coordinate to the view's layer range.
        // Vk3D uses VIEW directly. Integer texel fetches still need our clamp.
        if(nativeSampleLayer_&&!args.is_fetch)return "int("+eye()+")";
        // Resource policy must choose one or two layers explicitly. Do not
        // infer Eternal shadow/scene semantics from a comparison sampler.
        auto image=convert_separate_image_to_expression(args.img);
        return "min(int("+eye()+"), textureSize("+image+(args.imgtype->image.ms?").z - 1)":", 0).z - 1)");
    }
protected:
    std::string image_type_glsl(const SPIRType& original,uint32_t id,bool member)override{
        auto type=original;if(promote(type))type.image.arrayed=true;
        return CompilerGLSL::image_type_glsl(type,id,member);
    }
    std::string to_function_name(const TextureFunctionNameArguments& original)override{
        if(!promote(*original.base.imgtype))return CompilerGLSL::to_function_name(original);
        if(original.base.is_proj)throw std::runtime_error("SFS: projective image sampling requires explicit conversion");
        auto args=original;auto type=*args.base.imgtype;type.image.arrayed=true;args.base.imgtype=&type;
        return CompilerGLSL::to_function_name(args);
    }
    std::string to_function_args(const TextureFunctionArguments& original,bool* forward)override{
        if(!promote(*original.base.imgtype))return CompilerGLSL::to_function_args(original,forward);
        auto args=original;auto type=*args.base.imgtype;
        args.coord=coordinate(args.coord,args.base.is_fetch,sampledLayer(args.base));
        args.coord_components=3;type.image.arrayed=true;args.base.imgtype=&type;
        return CompilerGLSL::to_function_args(args,forward);
    }
    void emit_instruction(const Instruction& instruction)override{
        const auto op=static_cast<spv::Op>(instruction.op);const auto* words=stream(instruction);
        // AMD DOOM modules use the legacy Groups vote opcode. SPIRV-Cross
        // otherwise emits an unimplemented comment and leaves its result ID
        // undefined. Only subgroup scope maps to GLSL's invocation vote.
        if(op==spv::OpGroupAll||op==spv::OpGroupAny){
            if(get_constant(words[2]).scalar()!=spv::ScopeSubgroup)
                throw std::runtime_error("SFS: legacy group vote requires subgroup scope");
            emit_unary_func_op(words[0],words[1],words[3],
                op==spv::OpGroupAll?"allInvocationsARB":"anyInvocationARB");
            require_extension_internal("GL_ARB_shader_group_vote");
            register_control_dependent_expression(words[1]);
            return;
        }
        // DOOM's particle module stores an integer-backed buffer flag into a
        // local bool. Its original driver accepts it; GLSL requires conversion.
        if(op==spv::OpStore&&expression_type(words[0]).basetype==SPIRType::Boolean&&
           (expression_type(words[1]).basetype==SPIRType::UInt||expression_type(words[1]).basetype==SPIRType::Int)){
            auto type=expression_type(words[1]);const bool unsignedValue=type.basetype==SPIRType::UInt;
            type.basetype=SPIRType::Boolean;
            if(type.vecsize!=1)throw std::runtime_error("SFS: unsupported vector boolean store");
            auto ids=ir.increase_bound_by(2);set<SPIRType>(ids,type);
            set<SPIRExpression>(ids+1,"("+to_expression(words[1])+(unsignedValue?" != 0u)":" != 0)"),ids,true);
            inherit_expression_dependencies(ids+1,words[1]);
            EmbeddedInstruction copy;copy.op=instruction.op;copy.count=instruction.count;copy.length=instruction.length;
            for(uint32_t j=0;j<instruction.length;++j)copy.ops.push_back(words[j]);copy.ops[1]=ids+1;
            CompilerGLSL::emit_instruction(copy);
            return;
        }
        const bool read=op==spv::OpImageRead,write=op==spv::OpImageWrite;
        if((read||write)&&promote(expression_type(words[write?0:2]))){
            EmbeddedInstruction copy;copy.op=instruction.op;copy.count=instruction.count;copy.length=instruction.length;
            for(uint32_t j=0;j<instruction.length;++j)copy.ops.push_back(words[j]);
            auto image=to_non_uniform_aware_expression(words[write?0:2]);
            auto index=write?1:3;
            // Water outputs are explicitly allocated with two eye layers.
            // Querying their dimensions adds a descriptor-metadata LDG that
            // faults on NVIDIA (r159 water PC 0x5c0). Use the known eye layer.
            const auto layer=write&&nativeStorageWriteLayer_?"int("+eye()+")":
                "min(int("+eye()+"), imageSize("+image+").z - 1)";
            copy.ops[index]=coordinate(words[index],true,layer);
            CompilerGLSL::emit_instruction(copy);
            if(write&&broadcastWrites_){
                // Shared exposure/history executes once; only its image result
                // is broadcast. Buffer updates must not run a second time.
                copy.ops[index]=coordinate(words[index],true,"1");
                statement("if (imageSize(",image,").z > 1)");begin_scope();
                CompilerGLSL::emit_instruction(copy);end_scope();
            }
            return;
        }
        CompilerGLSL::emit_instruction(instruction);
        if((op==spv::OpImageQuerySize||op==spv::OpImageQuerySizeLod)&&promote(expression_type(words[2])))
            get<SPIRExpression>(words[1]).expression="("+get<SPIRExpression>(words[1]).expression+").xy";
    }
    std::string builtin_to_glsl(spv::BuiltIn builtin,spv::StorageClass storage)override{
        if(builtin>=spv::BuiltInBaryCoordNoPerspAMD&&builtin<=spv::BuiltInBaryCoordPullModelAMD)
            require_extension_internal("GL_AMD_shader_explicit_vertex_parameter");
        switch(builtin){
        case spv::BuiltInBaryCoordNoPerspAMD:return "gl_BaryCoordNoPerspAMD";
        case spv::BuiltInBaryCoordNoPerspCentroidAMD:return "gl_BaryCoordNoPerspCentroidAMD";
        case spv::BuiltInBaryCoordNoPerspSampleAMD:return "gl_BaryCoordNoPerspSampleAMD";
        case spv::BuiltInBaryCoordSmoothAMD:return "gl_BaryCoordSmoothAMD";
        case spv::BuiltInBaryCoordSmoothCentroidAMD:return "gl_BaryCoordSmoothCentroidAMD";
        case spv::BuiltInBaryCoordSmoothSampleAMD:return "gl_BaryCoordSmoothSampleAMD";
        case spv::BuiltInBaryCoordPullModelAMD:return "gl_BaryCoordPullModelAMD";
        default:break;
        }
        if(compute_){
            if(builtin==spv::BuiltInGlobalInvocationId)return "khSfsGlobalInvocationID";
            if(builtin==spv::BuiltInWorkgroupId)return "khSfsWorkGroupID";
            if(builtin==spv::BuiltInNumWorkgroups)return "khSfsNumWorkGroups";
        }
        return CompilerGLSL::builtin_to_glsl(builtin,storage);
    }
public:
    StereoCompiler(const std::vector<uint32_t>& words,bool compute,bool broadcast=false,bool nativeSampleLayer=false,bool nativeStorageWriteLayer=false):CompilerGLSL(words),compute_(compute),computeStage_(get_execution_model()==spv::ExecutionModelGLCompute),broadcastWrites_(broadcast),nativeSampleLayer_(nativeSampleLayer),nativeStorageWriteLayer_(nativeStorageWriteLayer){}
};
}

