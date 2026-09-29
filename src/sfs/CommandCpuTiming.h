#pragma once
#include "../BuildFeatures.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <array>
#include <string>

namespace kharvox::sfs {
struct CommandCpuTiming {
    inline static std::atomic<bool> enabled{false};
    inline static std::atomic<uint64_t> samples{},waitNs{},bodyNs{};
    struct alignas(64) Bucket {
        const char* name; const char* path;
        std::atomic<uint64_t> count{},lookup{},wait{},body{},maxWait{},maxBody{};
        Bucket(const char* n,const char* p):name(n),path(p){}
    };
    inline static std::array<std::atomic<Bucket*>,128> buckets{};
    inline static std::atomic<unsigned> registered{};
    static void registerBucket(Bucket& bucket){const auto slot=registered.fetch_add(1);if(slot<buckets.size())buckets[slot].store(&bucket,std::memory_order_release);}
    static void maximum(std::atomic<uint64_t>& dst,uint64_t n){auto old=dst.load(std::memory_order_relaxed);while(old<n&&!dst.compare_exchange_weak(old,n,std::memory_order_relaxed)){} }
    template<class Report> static void report(Report emit){
        for(auto& entry:buckets)if(auto* b=entry.load(std::memory_order_acquire)){
            const auto count=b->count.exchange(0);if(!count)continue;
            const auto lookup=b->lookup.exchange(0),wait=b->wait.exchange(0),body=b->body.exchange(0);
            emit("HOOK_CPU name="+std::string(b->name)+" path="+b->path+" samples="+std::to_string(count)+" sampleStride=64 lookupMeanUs="+std::to_string(double(lookup)/count/1000.)+" lockMeanUs="+std::to_string(double(wait)/count/1000.)+" bodyMeanUs="+std::to_string(double(body)/count/1000.)+" lockMaxUs="+std::to_string(b->maxWait.exchange(0)/1000.)+" bodyMaxUs="+std::to_string(b->maxBody.exchange(0)/1000.));
        }
    }
    Bucket* bucket{};uint64_t start{},resolvedAt{},locked{};
    static uint64_t now(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    explicit CommandCpuTiming(Bucket* b=nullptr):bucket(b){
        if(!enabled.load(std::memory_order_relaxed))return;
        // Randomized sampling avoids locking onto a repeated command sequence.
        thread_local uint32_t random=0x9e3779b9u;random^=random<<13;random^=random>>17;random^=random<<5;
        if(!(random&63))start=now();
    }
    void resolved(){if(start)resolvedAt=now();}
    void acquired(){if(start)locked=now();}
    ~CommandCpuTiming(){if(locked){const auto finish=now();waitNs.fetch_add(locked-start,std::memory_order_relaxed);bodyNs.fetch_add(finish-locked,std::memory_order_relaxed);samples.fetch_add(1,std::memory_order_relaxed);
        if(bucket){const auto resolved=resolvedAt?resolvedAt:start;bucket->lookup.fetch_add(resolved-start,std::memory_order_relaxed);bucket->wait.fetch_add(locked-resolved,std::memory_order_relaxed);bucket->body.fetch_add(finish-locked,std::memory_order_relaxed);maximum(bucket->maxWait,locked-resolved);maximum(bucket->maxBody,finish-locked);bucket->count.fetch_add(1,std::memory_order_relaxed);}
    }}
};
}
