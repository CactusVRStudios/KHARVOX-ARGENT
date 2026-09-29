#include "../src/RenderTrace.h"
#include "../src/BuildFeatures.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(){try{
    auto root=std::filesystem::temp_directory_path()/(L"argent-trace-test-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root);SetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",root.c_str());
    auto primary=reinterpret_cast<VkCommandBuffer>(uintptr_t(1)),child=reinterpret_cast<VkCommandBuffer>(uintptr_t(2));auto pool=reinterpret_cast<VkCommandPool>(uintptr_t(3));auto queue=reinterpret_cast<VkQueue>(uintptr_t(4));
    argent::trace::reset(primary,pool);argent::trace::reset(child,pool);
    argent::trace::record(primary,"execute",{argent::trace::id(child)});argent::trace::record(child,"stale_draw");
    argent::trace::reset(child);argent::trace::record(child,"fresh_draw");argent::trace::submit(queue,1,&primary);
    argent::trace::resetPool(pool);argent::trace::record(primary,"after_pool_reset");argent::trace::submit(queue,1,&primary);
    argent::trace::resetPool(pool,true);argent::trace::submit(queue,1,&primary);
    argent::trace::reset(primary,pool);argent::trace::record(primary,"execute",{argent::trace::id(primary)});argent::trace::submit(queue,1,&primary);
    std::ifstream file(root/"render-trace.tsv");std::string text((std::istreambuf_iterator<char>(file)),{});file.close();
    if(argent::cleanRelease){
        argent::trace::present();
        if(std::filesystem::exists(root/"render-trace.tsv")||argent::trace::currentFrame())throw std::runtime_error("Clean release accepted capture override");
    }else if(text.find("stale_draw")!=std::string::npos||text.find("fresh_draw")==std::string::npos||text.find("after_pool_reset")==std::string::npos||text.find("missing_command")==std::string::npos)throw std::runtime_error("Command lifetime/secondary expansion regression");
    std::filesystem::remove(root/"render-trace.tsv");std::filesystem::remove(root);
    std::cout<<"Command re-recording, pool reset/destruction and secondary recursion verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
