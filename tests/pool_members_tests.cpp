#include "../src/PoolMembers.h"
#include <cstdlib>
#include <unordered_set>
int main(){
    argent::PoolMembers<unsigned,unsigned> pools;
    for(unsigned p=0;p<2000;++p)for(unsigned m=0;m<20;++m)pools.insert(p,p*20+m);
    pools.erase(141);pools.insert(8,142); // Individual free and reused handle.
    std::unordered_set<unsigned> retired;
    pools.retire(7,[&](unsigned m){if(m<140||m>=160||m==141||m==142)std::abort();retired.insert(m);});
    if(retired.size()!=18)return 1;
    pools.retire(7,[](unsigned){std::abort();});
    retired.clear();pools.retire(8,[&](unsigned m){retired.insert(m);});
    if(retired.size()!=21||!retired.count(142))return 2;
    pools.insert(7,140);unsigned count=0;pools.retire(7,[&](unsigned m){if(m!=140)std::abort();++count;});
    return count==1?0:3;
}
