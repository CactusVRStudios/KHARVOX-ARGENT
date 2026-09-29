#pragma once
#include <windows.h>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>

namespace argent {
// Simulator creates a Win32 preview on the session thread. That thread must
// pump messages between frames, even when the game's render thread is idle.
class XrWorker {
    std::mutex mutex;
    std::condition_variable wake;
    std::queue<std::function<void()>> tasks;
public:
    XrWorker() {
        std::thread([this]{
            for(;;){
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait_for(lock,std::chrono::milliseconds(2),[&]{return !tasks.empty();});
                    if(!tasks.empty()){task=std::move(tasks.front());tasks.pop();}
                }
                if(task)task();
                MSG msg{};
                while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
                    TranslateMessage(&msg);DispatchMessageW(&msg);
                }
            }
        }).detach();
    }
    template<class F> auto invoke(F&& fn) {
        using R=decltype(fn());
        auto task=std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        auto result=task->get_future();
        {std::lock_guard<std::mutex> lock(mutex);tasks.emplace([task]{(*task)();});}
        wake.notify_one();return result.get();
    }
};
// Process-lifetime worker: layer is pinned, no join from DLL teardown/loader lock.
inline XrWorker& xrWorker(){static auto* worker=new XrWorker;return *worker;}
}
