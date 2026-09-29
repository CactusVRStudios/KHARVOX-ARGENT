#pragma once
#include <unordered_map>
#include <unordered_set>
namespace argent {
// Caller owns synchronization. Reset visits only members of the retiring pool.
template<class Pool,class Member> class PoolMembers {
    std::unordered_map<Pool,std::unordered_set<Member>> members;
    std::unordered_map<Member,Pool> owners;
public:
    void erase(Member member){
        auto owner=owners.find(member);if(owner==owners.end())return;
        auto pool=members.find(owner->second);
        if(pool!=members.end()){pool->second.erase(member);if(pool->second.empty())members.erase(pool);}
        owners.erase(owner);
    }
    void insert(Pool pool,Member member){erase(member);members[pool].insert(member);owners[member]=pool;}
    template<class Retire> void retire(Pool pool,Retire&& retireMember){
        auto found=members.find(pool);if(found==members.end())return;
        for(auto member:found->second){retireMember(member);owners.erase(member);}
        members.erase(found);
    }
};
}
