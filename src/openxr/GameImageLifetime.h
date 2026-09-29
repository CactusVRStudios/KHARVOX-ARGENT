#pragma once
#include <vulkan/vulkan.h>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <type_traits>
#include <vector>

namespace kharvox {
// Only handles actually borrowed by XR block retirement. Unrelated runtime
// destruction must remain possible during xrWait/EndFrame.
class GameImageLifetime {
public:
    struct Key {
        uintptr_t handle{};
        bool view{};
        bool operator<(const Key& other) const {
            return view==other.view ? handle<other.handle : view<other.view;
        }
    };
    template<class Handle> static Key key(Handle handle) {
        return {reinterpret_cast<uintptr_t>(handle),std::is_same_v<Handle,VkImageView>};
    }
    class Use {
    public:
        explicit Use(GameImageLifetime& owner):owner_(owner){}
        Use(const Use&)=delete;
        ~Use(){
            std::lock_guard lock(owner_.mutex_);
            for(const auto& key:keys_)if(--owner_.users_[key]==0)owner_.users_.erase(key);
            owner_.changed_.notify_all();
        }
        // Lookup and registration are atomic with respect to retirement.
        template<class Lookup,class Keys> bool select(Lookup lookup,Keys keys) {
            std::lock_guard lock(owner_.mutex_);
            if(!lookup())return false;
            const auto selected=keys();
            for(const auto& key:selected)
                if(key.handle&&owner_.retiring_.count(key))return false;
            for(const auto& key:selected)if(key.handle){++owner_.users_[key];keys_.push_back(key);}
            return true;
        }
    private:
        GameImageLifetime& owner_;
        std::vector<Key> keys_;
    };
    template<class Destroy> void retire(Key key,Destroy destroy) {
        if(!key.handle){destroy();return;}
        {
            std::unique_lock lock(mutex_);
            retiring_.insert(key);
            changed_.wait(lock,[&]{return !users_.count(key);});
        }
        // No lifetime/tracker/Native lock is held across downstream teardown.
        destroy();
        std::lock_guard lock(mutex_);
        retiring_.erase(key);
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::map<Key,unsigned> users_;
    std::set<Key> retiring_;
};
inline GameImageLifetime& gameImageLifetime() {
    static GameImageLifetime lifetime;
    return lifetime;
}
}
