#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <exception>

namespace argent {
// Called only after VK_ERROR_DEVICE_LOST. No readback or polling in gameplay.
class DeviceFaultReport {
    std::atomic<bool> reported{};
    static std::string hex(uint64_t value){std::ostringstream out;out<<"0x"<<std::hex<<value;return out.str();}
    static const char* type(VkDeviceFaultAddressTypeEXT value){
        switch(value){
        case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:return "read-invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:return "write-invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:return "execute-invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:return "instruction-unknown";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:return "instruction-invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:return "instruction-fault";
        default:return "none";
        }
    }
public:
    PFN_vkGetDeviceFaultInfoEXT query{};
    template<class Log,class Save> void report(VkDevice device,Log log,Save save) noexcept {
        if(!query||reported.exchange(true))return;
        try{
            VkDeviceFaultCountsEXT counts{VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
            auto result=query(device,&counts,nullptr);
            log("GPU_FAULT_COUNTS result="+std::to_string(result)+" addresses="+std::to_string(counts.addressInfoCount)+" vendors="+std::to_string(counts.vendorInfoCount)+" binaryBytes="+std::to_string(counts.vendorBinarySize));
            if(result!=VK_SUCCESS)return;
            // Bound allocations after a driver failure; VK_INCOMPLETE still
            // gives useful textual details when an unusually large dump exists.
            const auto binarySize=counts.vendorBinarySize;
            counts.addressInfoCount=std::min(counts.addressInfoCount,4096u);
            counts.vendorInfoCount=std::min(counts.vendorInfoCount,4096u);
            counts.vendorBinarySize=binarySize<=64ull*1024*1024?binarySize:0;
            std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
            std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
            std::vector<uint8_t> binary(static_cast<size_t>(counts.vendorBinarySize));
            VkDeviceFaultInfoEXT info{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
            info.pAddressInfos=addresses.empty()?nullptr:addresses.data();
            info.pVendorInfos=vendors.empty()?nullptr:vendors.data();
            info.pVendorBinaryData=binary.empty()?nullptr:binary.data();
            result=query(device,&counts,&info);
            info.description[VK_MAX_DESCRIPTION_SIZE-1]=0;
            log("GPU_FAULT_INFO result="+std::to_string(result)+" description="+info.description);
            if(result!=VK_SUCCESS&&result!=VK_INCOMPLETE)return;
            for(size_t n=0;n<std::min<size_t>(counts.addressInfoCount,addresses.size());++n){const auto& a=addresses[n];
                log("GPU_FAULT_ADDRESS type="+std::string(type(a.addressType))+" address="+hex(a.reportedAddress)+" precision="+hex(a.addressPrecision));}
            for(size_t n=0;n<std::min<size_t>(counts.vendorInfoCount,vendors.size());++n){auto& v=vendors[n];v.description[VK_MAX_DESCRIPTION_SIZE-1]=0;
                log("GPU_FAULT_VENDOR code="+hex(v.vendorFaultCode)+" data="+hex(v.vendorFaultData)+" description="+v.description);}
            if(binarySize>64ull*1024*1024)log("GPU_FAULT_BINARY skipped=size-limit");
            else if(counts.vendorBinarySize&&counts.vendorBinarySize<=binary.size()){
                binary.resize(static_cast<size_t>(counts.vendorBinarySize));save(binary);
            }
        }catch(const std::exception& e){try{log(std::string("GPU_FAULT_REPORT_FAILED ")+e.what());}catch(...) {}}
        catch(...){}
    }
};
}
