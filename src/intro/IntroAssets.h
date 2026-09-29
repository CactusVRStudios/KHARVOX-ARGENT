#pragma once
#include <windows.h>
#include <cstddef>
#include <compressapi.h>
#include <vector>
#include <cstdint>
#include <cstring>
namespace kharvox::intro {
struct Asset { const void* data{}; std::size_t size{}; };
#ifndef KHARVOX_COMPRESSED_ASSETS
inline Asset asset(int id){
    HMODULE module{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&asset),&module);
    auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    if(!resource)return {};auto loaded=LoadResource(module,resource);
    return loaded?Asset{LockResource(loaded),SizeofResource(module,resource)}:Asset{};
}
#else
inline std::vector<unsigned char> unpackAsset(int id){
    HMODULE module{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&unpackAsset),&module);auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    if(!resource)return {};auto loaded=LoadResource(module,resource);if(!loaded)return {};
    const auto size=SizeofResource(module,resource);const auto* bytes=static_cast<const unsigned char*>(LockResource(loaded));
    if(!bytes||size<8)return {};uint32_t header[2];std::memcpy(header,bytes,8);
    if(!header[1]||header[1]>1024*1024)return {};
    std::vector<unsigned char> output(header[1]);
    if(!header[0]){if(size-8!=output.size())return {};std::memcpy(output.data(),bytes+8,output.size());return output;}
    DECOMPRESSOR_HANDLE decoder{};if(!CreateDecompressor(header[0],nullptr,&decoder))return {};
    SIZE_T written{};const bool ok=Decompress(decoder,bytes+8,size-8,output.data(),output.size(),&written);
    CloseDecompressor(decoder);if(!ok||written!=output.size())return {};return output;
}
inline Asset asset(int id){
    const std::vector<unsigned char>* bytes=nullptr;
    if(id==101){static const auto font=unpackAsset(101);bytes=&font;}
    if(id==102){static const auto music=unpackAsset(102);bytes=&music;}
    if(id==104){static const auto copper=unpackAsset(104);bytes=&copper;}
    if(id==105){static const auto spriteFont=unpackAsset(105);bytes=&spriteFont;}
    if(id==106){static const auto scrollFont=unpackAsset(106);bytes=&scrollFont;}
    if(id==107){static const auto logo=unpackAsset(107);bytes=&logo;}
    return bytes&&!bytes->empty()?Asset{bytes->data(),bytes->size()}:Asset{};
}
#endif
struct PrivateFont {
    HANDLE handle{};
    PrivateFont(){auto bytes=asset(101);DWORD count{};if(bytes.data)handle=AddFontMemResourceEx(const_cast<void*>(bytes.data),DWORD(bytes.size),nullptr,&count);}
    ~PrivateFont(){if(handle)RemoveFontMemResourceEx(handle);}
    PrivateFont(const PrivateFont&)=delete;
    PrivateFont& operator=(const PrivateFont&)=delete;
};
}
