#pragma once
#include "ShaderIdentity.h"
#include <vulkan/vulkan.h>
#include <array>
#include <cstring>

namespace kharvox::sfs {
// Compatibility serialization recovered from provider 4.25.5.608 RVA 0x1ca2b0.
// Deliberately excludes pointers, attachment blend equations and vertex layouts.
// This identifies profile variants; it is NOT a safe pipeline-cache identity.
inline std::array<uint32_t,68> pipelinePacket(const VkGraphicsPipelineCreateInfo& p) {
    std::array<uint32_t,68> a{};
    auto f=[](float x){uint32_t v;std::memcpy(&v,&x,4);return v;};
    a[0]=p.sType;a[1]=p.flags;a[2]=p.stageCount;
    if(auto s=p.pVertexInputState){a[3]=s->sType;a[4]=s->flags;a[5]=s->vertexBindingDescriptionCount;a[6]=s->vertexAttributeDescriptionCount;}
    if(auto s=p.pInputAssemblyState){a[7]=s->sType;a[8]=s->flags;a[9]=s->topology;a[10]=s->primitiveRestartEnable;}
    if(auto s=p.pTessellationState){a[11]=s->sType;a[12]=s->flags;a[13]=s->patchControlPoints;}
    if(auto s=p.pViewportState){a[14]=s->sType;a[15]=s->flags;a[16]=s->viewportCount;a[17]=s->scissorCount;}
    if(auto s=p.pRasterizationState){a[18]=s->sType;a[19]=s->flags;a[20]=s->depthClampEnable;a[21]=s->rasterizerDiscardEnable;a[22]=s->polygonMode;a[23]=s->cullMode;a[24]=s->frontFace;a[25]=s->depthBiasEnable;a[26]=f(s->depthBiasConstantFactor);a[27]=f(s->depthBiasClamp);a[28]=f(s->depthBiasSlopeFactor);a[29]=f(s->lineWidth);}
    if(auto s=p.pMultisampleState){a[30]=s->sType;a[31]=s->flags;a[32]=s->rasterizationSamples;a[33]=s->sampleShadingEnable;a[34]=f(s->minSampleShading);a[35]=s->alphaToCoverageEnable;a[36]=s->alphaToOneEnable;}
    if(auto s=p.pDepthStencilState){
        a[37]=s->sType;a[38]=s->flags;a[39]=s->depthTestEnable;a[40]=s->depthWriteEnable;a[41]=s->depthCompareOp;a[42]=s->depthBoundsTestEnable;a[43]=s->stencilTestEnable;
        auto stencil=[&](size_t i,const VkStencilOpState& v){a[i]=v.failOp;a[i+1]=v.passOp;a[i+2]=v.depthFailOp;a[i+3]=v.compareOp;a[i+4]=v.compareMask;a[i+5]=v.writeMask;a[i+6]=v.reference;};
        stencil(44,s->front);stencil(51,s->back);a[58]=f(s->minDepthBounds);a[59]=f(s->maxDepthBounds);
    }
    if(auto s=p.pColorBlendState){a[60]=s->sType;a[61]=s->flags;a[62]=s->logicOpEnable;a[63]=s->logicOp;a[64]=s->attachmentCount;}
    if(auto s=p.pDynamicState){a[65]=s->sType;a[66]=s->flags;a[67]=s->dynamicStateCount;}
    return a;
}
inline uint64_t pipelineSeed(const VkGraphicsPipelineCreateInfo& p) {
    const auto packet=pipelinePacket(p);
    std::array<uint8_t,272> bytes{};
    for(size_t i=0;i<packet.size();++i)for(unsigned j=0;j<4;++j)bytes[i*4+j]=uint8_t(packet[i]>>(8*j));
    return profileHash(bytes.data(),uint32_t(bytes.size()));
}
}
