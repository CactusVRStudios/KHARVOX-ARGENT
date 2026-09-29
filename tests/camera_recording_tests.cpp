#include "../src/CameraCapture.h"
#include <iostream>
#include <stdexcept>
#include <thread>
namespace argent {
void log(const std::string&){}
namespace sfs {bool vrEnabled(){return true;}}
namespace trace {uint64_t currentFrame(){return 0;}}
namespace camera {void observeRenderedCamera(const void*,size_t){}}
struct CameraCaptureTestAccess {
 static void check(bool value){if(!value)throw std::runtime_error("camera recording mismatch");}
 static void run(){
  CameraCapture camera;auto cb=reinterpret_cast<VkCommandBuffer>(1);auto pipeline=reinterpret_cast<VkPipeline>(2);
  sfs::Matrix measured{};check(!camera.projection(measured,100));
  camera.liveProjection=sfs::identity();camera.liveProjectionValid=true;camera.liveProjectionTick=GetTickCount64();
  check(camera.projection(measured,100));camera.liveProjectionTick=GetTickCount64()-1000;
  check(!camera.projection(measured,100)&&camera.projection(measured));
  auto sl=reinterpret_cast<VkDescriptorSetLayout>(3);auto pl=reinterpret_cast<VkPipelineLayout>(4);auto set=reinterpret_cast<VkDescriptorSet>(5);
  VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr};
  VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=1;li.pBindings=&binding;camera.layout(sl,&li);
  VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pi.setLayoutCount=1;pi.pSetLayouts=&sl;camera.pipelineLayout(pl,&pi);
  camera.pipeline(pipeline,0,0);camera.command(cb);camera.bindPipeline(cb,pipeline);
  uint32_t dynamic=256;camera.bindSets(cb,0,1,&set,1,&dynamic,VK_PIPELINE_BIND_POINT_GRAPHICS,pl);camera.draw(cb);
  check(camera.commands.at(cb).commonCandidates.size()==1&&camera.commands.at(cb).commonCandidates[0].second==256);
  // Once warmed, draws/binds must proceed even while unrelated metadata is locked.
  std::unique_lock<std::shared_mutex> held(camera.mutex);
  for(int i=0;i<1000;++i){camera.bindSets(cb,0,1,&set,1,&dynamic,VK_PIPELINE_BIND_POINT_GRAPHICS,pl);camera.draw(cb);}
  held.unlock();check(camera.commands.at(cb).commonCandidates.size()==1);
  camera.freeCommand(cb);camera.command(cb);camera.draw(cb);check(camera.commands.at(cb).commonCandidates.empty());
  camera.bindPipeline(cb,pipeline);camera.retirePipelineLayout(pl);binding.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;camera.layout(sl,&li);camera.pipelineLayout(pl,&pi);
  camera.bindSets(cb,0,1,&set,1,&dynamic,VK_PIPELINE_BIND_POINT_GRAPHICS,pl);camera.draw(cb);check(camera.commands.at(cb).commonCandidates[0].second==0);
  camera.command(cb);camera.bindPipeline(cb,reinterpret_cast<VkPipeline>(99));camera.bindSets(cb,0,1,&set,0,nullptr,VK_PIPELINE_BIND_POINT_GRAPHICS,pl);camera.draw(cb);check(camera.commands.at(cb).commonCandidates.empty());
  auto cb2=reinterpret_cast<VkCommandBuffer>(6);camera.command(cb2);camera.bindPipeline(cb2,pipeline);camera.bindPipeline(cb,pipeline);
  auto worker=[&](VkCommandBuffer command){for(int i=0;i<10000;++i){camera.bindSets(command,0,1,&set,0,nullptr,VK_PIPELINE_BIND_POINT_GRAPHICS,pl);camera.draw(command);}};
  std::thread a(worker,cb),b(worker,cb2);a.join();b.join();
  check(camera.commands.at(cb).commonCandidates.size()==1&&camera.commands.at(cb2).commonCandidates.size()==1);
  // Water capture must preserve descriptor copies, dynamic offsets and mapped
  // constants without scheduling the old synchronous GPU readback path.
  auto pool=reinterpret_cast<VkDescriptorPool>(20);
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&sl;
  camera.allocate(&ai,&set);
  auto view=reinterpret_cast<VkImageView>(21);
  VkDescriptorImageInfo image{};image.imageView=view;image.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=2;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image;
  camera.update(1,&write,0,nullptr);
  VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};copy.srcSet=set;copy.dstSet=set;copy.srcBinding=2;copy.dstBinding=3;copy.descriptorCount=1;
  camera.update(0,nullptr,1,&copy);check(camera.probeImages.at(set).at({3,0}).imageView==view);
  auto buffer=reinterpret_cast<VkBuffer>(22);auto mem=reinterpret_cast<VkDeviceMemory>(23);
  std::vector<unsigned char> bytes(2048,42);
  camera.memory.allocated(mem,bytes.size());camera.memory.created(buffer,bytes.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);camera.memory.bound(buffer,mem,0);camera.memory.mapped(mem,0,bytes.size(),bytes.data());
  binding.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;camera.layout(sl,&li);
  VkDescriptorBufferInfo info{buffer,0,1024};write.dstBinding=0;write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;write.pImageInfo=nullptr;write.pBufferInfo=&info;camera.update(1,&write,0,nullptr);
  camera.probePipeline(pipeline,"43dccf7a0bc3d66e72953edd0877ddc959d2600dc247ca2253456260872536e4");
  camera.probeEpoch=GetTickCount64();camera.probeEnd=GetTickCount64()+60000;camera.probePoll=camera.probeEnd;
  camera.bindPipeline(cb,pipeline,VK_PIPELINE_BIND_POINT_COMPUTE);camera.bindSets(cb,0,1,&set,1,&dynamic,VK_PIPELINE_BIND_POINT_COMPUTE,pl);camera.dispatch(cb);
  camera.submitProbe(1,&cb);check(camera.probeCount==1&&camera.waterCaptureBytes==2048&&!camera.gpuProbePending());
  camera.retirePool(pool);check(camera.probeImages.count(set)==0);
 }
};
}
int main(){try{
 auto root=std::filesystem::temp_directory_path()/("argent-water-test-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(root);
 SetEnvironmentVariableW(L"ARGENT_WATER_INPUT_CAPTURE",L"1");SetEnvironmentVariableW(L"ARGENT_LOG",(root/L"test.log").c_str());
 argent::CameraCaptureTestAccess::run();
 bool constants=false;for(const auto& file:std::filesystem::directory_iterator(root))if(file.path().extension()==L".bin")constants|=file.file_size()==1024;
 if(!constants||std::filesystem::file_size(root/L"water-bindings.tsv")==0)throw std::runtime_error("Water capture files missing");
 std::cout<<"PASS camera recording, descriptor lifetime, water constants and no GPU readback\n";
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
