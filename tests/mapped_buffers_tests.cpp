#include "../src/MappedBuffers.h"
#include <array>
#include <iostream>
#include <stdexcept>
static void check(bool v){if(!v)throw std::runtime_error("Mapped buffer lifetime/range regression");}
int main(){try{
    argent::MappedBuffers m;auto memory=reinterpret_cast<VkDeviceMemory>(uintptr_t(1));auto buffer=reinterpret_cast<VkBuffer>(uintptr_t(2));std::array<unsigned char,128> bytes{};for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i);std::vector<unsigned char> out;
    m.allocated(memory,256);m.created(buffer,96);m.bound(buffer,memory,64);m.mapped(memory,32,128,bytes.data());
    check(m.read(buffer,0,64,out)&&out.front()==32&&out.back()==95);check(m.read(buffer,95,1,out)&&out[0]==127);check(!m.read(buffer,96,1,out));check(!m.read(buffer,UINT64_MAX,1,out));
    m.unmapped(memory);check(!m.read(buffer,0,1,out));m.mapped(memory,128,VK_WHOLE_SIZE,bytes.data());check(!m.read(buffer,0,1,out));check(m.read(buffer,64,1,out)&&out[0]==0);
    m.freed(memory);m.allocated(memory,256);m.mapped(memory,0,128,bytes.data());check(!m.read(buffer,0,1,out));m.bound(buffer,memory,0);check(m.read(buffer,0,1,out));m.destroyed(buffer);check(!m.read(buffer,0,1,out));
    m.created(buffer,96,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);m.bound(buffer,memory,0);check(!m.copyable(buffer,0,96));
    m.created(buffer,96,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);check(!m.copyable(buffer,0,96));m.bound(buffer,memory,0);
    check(m.copyable(buffer,0,96));check(!m.copyable(buffer,1,96));check(!m.copyable(buffer,UINT64_MAX,4));
    m.freed(memory);check(!m.copyable(buffer,0,96));
    std::cout<<"Mapped offsets, partial maps, overflow, unmap/free, transfer usage and handle reuse verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
