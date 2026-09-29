#include "../src/hands/HandSceneDepthTracker.h"
#include <stdexcept>
#include <iostream>
using namespace kharvox::hands;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
template<class T>T h(size_t i){return reinterpret_cast<T>(i);}
void frame(size_t id,VkExtent2D extent,bool reverse=true,bool reduced=false){
 auto color=h<VkImage>(id),depth=h<VkImage>(id+1);auto cv=h<VkImageView>(id+2),dv=h<VkImageView>(id+3);
 VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.extent={extent.width,extent.height,1};image.format=VK_FORMAT_D32_SFLOAT;image.samples=VK_SAMPLE_COUNT_1_BIT;
 handSceneImageCreated(depth,image);
 VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=color;view.format=VK_FORMAT_R8G8B8A8_UNORM;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};handSceneImageViewCreated(cv,view);
 view.image=depth;view.format=VK_FORMAT_D32_SFLOAT;view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;handSceneImageViewCreated(dv,view);
 VkAttachmentDescription attachments[2]{};attachments[0].format=VK_FORMAT_R8G8B8A8_UNORM;attachments[1].format=VK_FORMAT_D32_SFLOAT;attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
 VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=2;pass.pAttachments=attachments;auto rp=h<VkRenderPass>(id+4);handSceneRenderPassCreated(rp,pass);
 VkImageView views[]{cv,dv};VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=rp;fb.attachmentCount=2;fb.pAttachments=views;fb.width=extent.width;fb.height=extent.height;auto f=h<VkFramebuffer>(id+5);handSceneFramebufferCreated(f,fb);
 VkClearValue clear[2]{};clear[1].depthStencil.depth=reverse?0:1;VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=rp;begin.framebuffer=f;begin.clearValueCount=2;begin.pClearValues=clear;
 auto cb=h<VkCommandBuffer>(id+6);handSceneBeginRenderPass(cb,&begin);if(reduced)handSceneViewport(cb,{0,0,float(extent.width/2),float(extent.height/2),0,1});handSceneEndRenderPass(cb);
}
int main(){try{
 handSceneDeviceDestroyed();HandSceneTarget target{};
 frame(100,{800,900});check(handSceneDepthForExtent({1600,1800},target)&&target.depthImage==h<VkImage>(101)&&target.reverseDepth,"Upscaled scene depth selection failed");
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=target.depthImage;b.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;handSceneImageBarriers(1,&b);
 check(handSceneDepthForExtent({1600,1800},target)&&target.depthLayout==b.newLayout,"Final depth layout lost");
 frame(200,{2048,2048});check(handSceneDepthForExtent({1600,1800},target)&&target.depthImage==h<VkImage>(101),"Shadow depth replaced scene depth");
 frame(300,{800,900});check(!handSceneDepthForExtent({1600,1800},target),"Ambiguous depth accepted");
 handSceneImageDestroyed(h<VkImage>(301));check(handSceneDepthForExtent({1600,1800},target),"Destroyed candidate remained cached");
 handSceneBeginFrame();check(!handSceneDepthForExtent({1600,1800},target),"Previous frame depth reused");
 frame(400,{800,900},false);check(handSceneDepthForExtent({1600,1800},target)&&!target.reverseDepth,"Normal depth convention lost");
 handSceneImageViewDestroyed(h<VkImageView>(403));check(!handSceneDepthForExtent({1600,1800},target),"Destroyed view reused");
 handSceneBeginFrame();frame(500,{1600,1800},false,true);check(handSceneDepthForExtent({1600,1800},target)&&target.extent.width==800&&target.extent.height==900,"Internal viewport used allocation size");
 handSceneDeviceDestroyed();std::cout<<"PASS: scene-depth selection, ambiguity, layout, lifetime, scale and conventions\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
