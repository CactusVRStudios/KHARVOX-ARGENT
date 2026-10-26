#pragma once
#include "../XrWorker.h"
#include <cstdint>
#include <stdexcept>

namespace argent::fg {
// Bounded hand-off for an already copied exact endpoint. The task owns only
// XR publication data, never game images, command buffers or mirror callbacks.
// Stage after GPU completion; dispatch after native source retirement. Drain
// before the next XR frame or teardown, without holding XR/queue/SFS locks.
class EndpointOutput {
    std::mutex mutex_;
    std::function<void()> staged_;
    std::shared_future<void> active_;
    uint64_t epoch_{};
    void dispatchLocked() {
        if(!staged_)return;
        static auto* worker=new XrWorker; // independent of the XR lifecycle worker
        active_=worker->post(std::move(staged_)).share();
        staged_={};
    }
public:
    bool pending() {std::lock_guard lock(mutex_);return bool(staged_)||active_.valid();}
    void stage(std::function<void()> task) {
        std::lock_guard lock(mutex_);
        if(staged_||active_.valid())throw std::logic_error("Previous exact endpoint not retired");
        staged_=std::move(task);++epoch_;
    }
    void dispatch() {std::lock_guard lock(mutex_);dispatchLocked();}
    void drain() {
        std::shared_future<void> task;uint64_t epoch;
        {std::lock_guard lock(mutex_);dispatchLocked();task=active_;epoch=epoch_;}
        if(!task.valid())return;
        std::exception_ptr error;
        try{task.get();}catch(...){error=std::current_exception();}
        {std::lock_guard lock(mutex_);if(epoch==epoch_)active_={};}
        if(error)std::rethrow_exception(error);
    }
};
}
