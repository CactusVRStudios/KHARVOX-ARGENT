#include <windows.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#include "openxr/OpenXRRuntimePolicy.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <filesystem>

int main() {
    bool vkOk=false, xrOk=false;
    auto vk=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!vk) std::cout<<"VULKAN_LOADER_MISSING win32="<<GetLastError()<<'\n';
    else {
        auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk,"vkGetInstanceProcAddr"));
        auto create=get?reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance")):nullptr;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName="KHARVOX ARGENT probe"; app.apiVersion=VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo=&app;
        VkInstance instance{};
        auto r=create?create(&ci,nullptr,&instance):VK_ERROR_INITIALIZATION_FAILED;
        std::cout<<"vkCreateInstance="<<r<<'\n';
        if(r==VK_SUCCESS){
            auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance,"vkEnumeratePhysicalDevices"));
            auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance,"vkGetPhysicalDeviceProperties"));
            uint32_t count=0;
            r=enumerate(instance,&count,nullptr);
            if(r==VK_SUCCESS && count){
                std::vector<VkPhysicalDevice> devices(count);
                r=enumerate(instance,&count,devices.data());
                if(r==VK_SUCCESS){
                    for(uint32_t i=0;i<count;++i){VkPhysicalDeviceProperties p{};properties(devices[i],&p);std::cout<<"GPU="<<p.deviceName<<" api="<<p.apiVersion<<'\n';}
                    vkOk=true;
                }
            }
            reinterpret_cast<PFN_vkDestroyInstance>(get(instance,"vkDestroyInstance"))(instance,nullptr);
        }
        FreeLibrary(vk);
    }
    wchar_t exe[32768]{};
    if(!GetModuleFileNameW(nullptr,exe,32768))return 3;
    auto loader=std::filesystem::path(exe).parent_path()/L"openxr_loader.dll";
    auto xr=LoadLibraryExW(loader.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!xr)std::cout<<"OPENXR_LOADER_MISSING win32="<<GetLastError()<<'\n';
    else {
        auto get=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(xr,"xrGetInstanceProcAddr"));
        PFN_xrVoidFunction fn{};
        if(!get || XR_FAILED(get(XR_NULL_HANDLE,"xrCreateInstance",&fn))){FreeLibrary(xr);return 4;}
        auto create=reinterpret_cast<PFN_xrCreateInstance>(fn);
        bool enable1=false,enable2=false;
        if(XR_SUCCEEDED(get(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",&fn))){
            auto enumerate=reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(fn);uint32_t count{};
            if(XR_SUCCEEDED(enumerate(nullptr,0,&count,nullptr))){
                std::vector<XrExtensionProperties> extensions(count,{XR_TYPE_EXTENSION_PROPERTIES});
                if(XR_SUCCEEDED(enumerate(nullptr,count,&count,extensions.data())))for(const auto& e:extensions){
                    enable1|=!strcmp(e.extensionName,XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
                    enable2|=!strcmp(e.extensionName,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
                }
            }
        }
        const auto manifest=kharvox::activeOpenXRRuntimeManifest();
        const auto path=kharvox::selectOpenXRVulkanPath(kharvox::classifyOpenXRRuntime(manifest),false,enable1,enable2);
        std::cout<<"XR_MANIFEST="<<manifest<<'\n'<<"XR_VULKAN_ENABLE1="<<enable1<<" XR_VULKAN_ENABLE2="<<enable2<<'\n';
        std::cout<<"XR_VULKAN_PATH="<<kharvox::openXRVulkanPathName(path)<<'\n';
        if(path==kharvox::OpenXRVulkanPath::None){FreeLibrary(xr);return 5;}
        const bool managed=path==kharvox::OpenXRVulkanPath::VulkanEnable2RuntimeManaged;
        const char* extension=managed?XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME:XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        ci.enabledExtensionCount=1;ci.enabledExtensionNames=&extension;
        strcpy_s(ci.applicationInfo.applicationName,"KHARVOX ARGENT probe");
        ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        XrInstance instance{};
        auto r=create(&ci,&instance);
        std::cout<<"xrCreateInstance="<<r<<'\n';
        if(XR_SUCCEEDED(r)){
            get(instance,"xrGetInstanceProperties",&fn);
            XrInstanceProperties p{XR_TYPE_INSTANCE_PROPERTIES};
            if(XR_SUCCEEDED(reinterpret_cast<PFN_xrGetInstanceProperties>(fn)(instance,&p)))std::cout<<"XR_RUNTIME="<<p.runtimeName<<'\n';
            get(instance,"xrGetSystem",&fn);
            XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
            XrSystemId system{};
            r=reinterpret_cast<PFN_xrGetSystem>(fn)(instance,&si,&system);
            std::cout<<"xrGetSystem="<<r<<'\n';xrOk=XR_SUCCEEDED(r);
            if(xrOk){
                const char* name=managed?"xrGetVulkanGraphicsRequirements2KHR":"xrGetVulkanGraphicsRequirementsKHR";
                auto status=get(instance,name,&fn);
                XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
                if(XR_SUCCEEDED(status)&&fn)status=reinterpret_cast<PFN_xrGetVulkanGraphicsRequirementsKHR>(fn)(instance,system,&requirements);
                xrOk=XR_SUCCEEDED(status);std::cout<<"XR_GRAPHICS_REQUIREMENTS="<<status<<'\n';
            }
            if(xrOk&&XR_SUCCEEDED(get(instance,"xrEnumerateViewConfigurationViews",&fn))){
                auto enumerate=reinterpret_cast<PFN_xrEnumerateViewConfigurationViews>(fn);
                uint32_t viewsCount{};auto status=enumerate(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&viewsCount,nullptr);
                if(XR_SUCCEEDED(status)&&viewsCount&&viewsCount<=16){
                    std::vector<XrViewConfigurationView> views(viewsCount,{XR_TYPE_VIEW_CONFIGURATION_VIEW});
                    status=enumerate(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,viewsCount,&viewsCount,views.data());
                    if(XR_SUCCEEDED(status))for(uint32_t eye=0;eye<viewsCount;++eye)
                        std::cout<<"XR_EYE"<<eye<<"_RECOMMENDED="<<views[eye].recommendedImageRectWidth<<"x"<<views[eye].recommendedImageRectHeight<<'\n';
                }
            }
            get(instance,"xrDestroyInstance",&fn);reinterpret_cast<PFN_xrDestroyInstance>(fn)(instance);
        }
        FreeLibrary(xr);
    }
    std::cout<<"DIAGNOSTIC_ONLY: no XR session, game image, engine hooks or stereo proof\n";
    return vkOk&&xrOk?0:2;
}
