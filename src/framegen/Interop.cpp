#include "Interop.h"
#include <cstring>
namespace argent::fg {
#define V(n) proc<PFN_##n>(#n)
#define I(n) instanceProc<PFN_##n>(#n)
void Interop::initialize(const Device& d){
 dev=d;
 // Never select an adapter by enumeration order on a hybrid-GPU system.
 VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
 VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&id};I(vkGetPhysicalDeviceProperties2)(dev.physical,&p);
 if(p.properties.vendorID!=0x10de||!id.deviceLUIDValid)throw std::runtime_error("NVIDIA RTX with a valid Vulkan adapter LUID required");
 V(vkGetMemoryWin32HandlePropertiesKHR);
 Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
 check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");LUID luid{};memcpy(&luid,id.deviceLUID,sizeof(luid));
 check(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)),"match Vulkan adapter");
 check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dx)),"D3D12 device");
 D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;check(dx->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"D3D12 queue");
 check(dx->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"D3D12 allocator");
 check(dx->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"D3D12 list");check(list->Close(),"close initial list");
 check(dx->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&done)),"D3D12 fence");event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("D3D12 fence event");
 VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;cp.queueFamilyIndex=dev.graphicsFamily;
 check(V(vkCreateCommandPool)(dev.device,&cp,nullptr,&pool),"FG command pool");
 VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=uint32_t(commands.size());
 check(V(vkAllocateCommandBuffers)(dev.device,&ca,commands.data()),"FG command allocation");
 for(auto cb:commands)if(dev.setLoaderData)check(dev.setLoaderData(dev.device,cb),"FG loader data");
 VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
 for(auto& f:fences)check(V(vkCreateFence)(dev.device,&fc,nullptr,&f),"FG Vulkan fence");
 command=commands[0];fence=fences[0];initializeHandoff();
}
void Interop::make(SharedImage& out,uint32_t w,uint32_t h,VkFormat f,DXGI_FORMAT df,VkImageUsageFlags usage,D3D12_RESOURCE_FLAGS flags){
 VkPhysicalDeviceExternalImageFormatInfo ex{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};ex.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
 VkPhysicalDeviceImageFormatInfo2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,&ex};query.format=f;query.type=VK_IMAGE_TYPE_2D;query.tiling=VK_IMAGE_TILING_OPTIMAL;query.usage=usage;
 VkExternalImageFormatProperties ep{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};VkImageFormatProperties2 props{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,&ep};
 check(I(vkGetPhysicalDeviceImageFormatProperties2)(dev.physical,&query,&props),"FG external format");
 if(!(ep.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))throw std::runtime_error("FG image import unsupported");
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=df;desc.SampleDesc.Count=1;desc.Flags=flags;
 check(dx->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out.dx)),"shared texture");
 HANDLE handle{};check(dx->CreateSharedHandle(out.dx.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"shared handle");
 try{
 VkExternalMemoryImageCreateInfo ec{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};ec.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
 VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,&ec};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=f;ci.extent={w,h,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=usage;
 check(V(vkCreateImage)(dev.device,&ci,nullptr,&out.image),"import image");
 VkMemoryRequirements req{};V(vkGetImageMemoryRequirements)(dev.device,out.image,&req);
 VkMemoryWin32HandlePropertiesKHR hp{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};check(V(vkGetMemoryWin32HandlePropertiesKHR)(dev.device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,handle,&hp),"import properties");
 VkPhysicalDeviceMemoryProperties mp{};I(vkGetPhysicalDeviceMemoryProperties)(dev.physical,&mp);
 unsigned type=UINT32_MAX;for(unsigned i=0;i<mp.memoryTypeCount;++i)if((hp.memoryTypeBits&req.memoryTypeBits)&(1u<<i)){type=i;if(mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)break;}
 if(type==UINT32_MAX)throw std::runtime_error("FG shared memory type");
 VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=out.image;
 VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,&dedicated};import.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;import.handle=handle;
 VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,&import};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
 check(V(vkAllocateMemory)(dev.device,&ai,nullptr,&out.memory),"import allocation");check(V(vkBindImageMemory)(dev.device,out.image,out.memory,0),"import binding");
 }catch(...){CloseHandle(handle);throw;}CloseHandle(handle);
}
void Interop::destroy(SharedImage& i){if(i.image)V(vkDestroyImage)(dev.device,i.image,nullptr);if(i.memory)V(vkFreeMemory)(dev.device,i.memory,nullptr);i={};}
void Interop::barrier(VkCommandBuffer c,VkImage image,VkImageLayout old,VkImageLayout next,uint32_t src,uint32_t dst,uint32_t layer){
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=old;b.newLayout=next;b.srcQueueFamilyIndex=src;b.dstQueueFamilyIndex=dst;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,layer,1};
 b.srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED||src==VK_QUEUE_FAMILY_EXTERNAL?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
 b.dstAccessMask=dst==VK_QUEUE_FAMILY_EXTERNAL?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
 V(vkCmdPipelineBarrier)(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
void Interop::initializeHandoff(){
 if(!dev.framegenExternalSemaphore){log("FRAMEGEN_INTEROP gpuHandoff=0 reason=extension-not-enabled");return;}
 try{
  VkPhysicalDeviceExternalSemaphoreInfo query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
  query.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
  VkExternalSemaphoreProperties properties{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
  I(vkGetPhysicalDeviceExternalSemaphoreProperties)(dev.physical,&query,&properties);
  if(!(properties.externalSemaphoreFeatures&VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT))throw std::runtime_error("D3D12 fence import unavailable");
  check(dx->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&inputReady)),"shared input fence");
  const auto import=V(vkImportSemaphoreWin32HandleKHR);
  auto bind=[&](ID3D12Fence* source,VkSemaphore& semaphore){
   HANDLE handle{};check(dx->CreateSharedHandle(source,nullptr,GENERIC_ALL,nullptr,&handle),"shared fence handle");
   try{
    // D3D12_FENCE imports retain their value semantics on a binary VkSemaphore.
    // This path uses VkD3D12FenceSubmitInfoKHR, without enabling timeline features
    // on the game's existing device chain.
    VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(V(vkCreateSemaphore)(dev.device,&create,nullptr,&semaphore),"FG external semaphore");
    VkImportSemaphoreWin32HandleInfoKHR info{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
    info.semaphore=semaphore;info.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;info.handle=handle;
    check(import(dev.device,&info),"FG permanent fence import");
   }catch(...){CloseHandle(handle);throw;}
   CloseHandle(handle);
  };
  bind(inputReady.Get(),inputSemaphore);bind(done.Get(),outputSemaphore);gpuHandoff=true;
  log("FRAMEGEN_INTEROP gpuHandoff=1 Vulkan-signal/D3D12-wait/D3D12-signal/Vulkan-wait");
 }catch(const std::exception& error){
  if(inputSemaphore)V(vkDestroySemaphore)(dev.device,inputSemaphore,nullptr);
  if(outputSemaphore)V(vkDestroySemaphore)(dev.device,outputSemaphore,nullptr);
  inputSemaphore=outputSemaphore={};inputReady.Reset();
  log(std::string("FRAMEGEN_INTEROP gpuHandoff=0 safe CPU fallback: ")+error.what());
 }
}
void Interop::beginVk(unsigned nextSlot){
 if(nextSlot>=commands.size())throw std::runtime_error("FG invalid command slot");
 slot=nextSlot;command=commands[slot];fence=fences[slot];
 if(pending[slot]){check(V(vkWaitForFences)(dev.device,1,&fence,VK_TRUE,UINT64_MAX),"retire FG command");pending[slot]=false;}
 check(V(vkResetCommandBuffer)(command,0),"reset FG command");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(V(vkBeginCommandBuffer)(command,&bi),"begin FG command");
}
void Interop::submit(uint32_t count,const VkSemaphore* waits,VkSemaphore signal,uint64_t signalValue,const uint64_t* waitValues,bool* consumed,bool wait){
 check(V(vkEndCommandBuffer)(command),"end FG command");check(V(vkResetFences)(dev.device,1,&fence),"reset FG fence");
 std::vector<VkPipelineStageFlags> stages(count,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
 std::vector<uint64_t> binaryValues(count,0);
 VkD3D12FenceSubmitInfoKHR values{VK_STRUCTURE_TYPE_D3D12_FENCE_SUBMIT_INFO_KHR};
 values.waitSemaphoreValuesCount=count;values.pWaitSemaphoreValues=waitValues?waitValues:binaryValues.data();
 values.signalSemaphoreValuesCount=signal?1:0;values.pSignalSemaphoreValues=signal?&signalValue:nullptr;
 VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.pNext=signal||waitValues?&values:nullptr;
 si.waitSemaphoreCount=count;si.pWaitSemaphores=waits;si.pWaitDstStageMask=stages.data();si.commandBufferCount=1;si.pCommandBuffers=&command;
 si.signalSemaphoreCount=signal?1:0;si.pSignalSemaphores=signal?&signal:nullptr;
 {std::lock_guard<std::recursive_mutex> lock(*dev.queueMutex);check(V(vkQueueSubmit)(dev.graphicsQueue,1,&si,fence),"submit FG copy");}
 pending[slot]=true;if(consumed)*consumed=true;
 if(wait){check(V(vkWaitForFences)(dev.device,1,&fence,VK_TRUE,UINT64_MAX),"wait FG copy");pending[slot]=false;}
}
void Interop::submitVk(uint32_t count,const VkSemaphore* waits,bool* consumed){submit(count,waits,{},0,nullptr,consumed,true);}
void Interop::submitNative(uint32_t count,const VkSemaphore* waits,bool* consumed){submit(count,waits,{},0,nullptr,consumed,!gpuHandoff);}
void Interop::waitSlot(unsigned index){
 if(index>=fences.size())throw std::runtime_error("FG invalid retirement slot");
 if(pending[index]){check(V(vkWaitForFences)(dev.device,1,&fences[index],VK_TRUE,UINT64_MAX),"retire FG input conversion");pending[index]=false;}
}
void Interop::submitInput(uint32_t count,const VkSemaphore* waits,bool* consumed){
 if(!gpuHandoff){submitVk(count,waits,consumed);return;}
 const auto next=inputValue+1;
 submit(count,waits,inputSemaphore,next,nullptr,consumed,false);inputValue=next;
 check(queue->Wait(inputReady.Get(),inputValue),"GPU wait for Vulkan inputs");
}
void Interop::submitOutput(){
 if(!gpuHandoff){waitDx();submitVk();return;}
 // Same Vulkan queue as the later XR copy. Its completion fence also covers
 // this join, the generated image and every preceding cross-API dependency.
 submit(1,&outputSemaphore,{},0,&value,nullptr,false);
}
void Interop::beginDx(){waitDx();check(allocator->Reset(),"reset FG allocator");check(list->Reset(allocator.Get(),nullptr),"reset FG list");}
void Interop::submitDx(bool wait){
 check(list->Close(),"close FG list");ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
 const auto next=value+1;check(queue->Signal(done.Get(),next),"signal FG consumer");value=next;
 if(wait)waitDx();
}
void Interop::waitDx(){if(done&&done->GetCompletedValue()<value){check(done->SetEventOnCompletion(value,event),"FG event completion");if(WaitForSingleObject(event,INFINITE)!=WAIT_OBJECT_0)throw std::runtime_error("FG fence wait");}if(dx)check(dx->GetDeviceRemovedReason(),"FG device state");}
void Interop::waitAll(){
 std::array<VkFence,3> active{};uint32_t count{};
 for(unsigned i=0;i<fences.size();++i)if(pending[i])active[count++]=fences[i];
 if(count){check(V(vkWaitForFences)(dev.device,count,active.data(),VK_TRUE,UINT64_MAX),"retire FG GPU chain");pending={};}
 waitDx();
}
void Interop::destroyIdle(){
 waitAll();
 if(inputSemaphore)V(vkDestroySemaphore)(dev.device,inputSemaphore,nullptr);
 if(outputSemaphore)V(vkDestroySemaphore)(dev.device,outputSemaphore,nullptr);
 inputSemaphore=outputSemaphore={};inputReady.Reset();
 for(auto f:fences)if(f)V(vkDestroyFence)(dev.device,f,nullptr);
 if(pool)V(vkDestroyCommandPool)(dev.device,pool,nullptr);
 fences={};fence={};commands={};command={};pool={};if(event)CloseHandle(event);event={};
}

#undef V
#undef I
}
