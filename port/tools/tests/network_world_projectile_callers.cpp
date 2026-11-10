// SPDX-License-Identifier: GPL-3.0-only
// Actual native callback bodies with identity/list/effect boundaries, not initialized actors or a world.
#include <cstdio>
#include <cstdlib>
#include <vector>
static bool replica;
int CE_WorldReplica() { return replica; }
enum { THING_TYPE_BATTLE_ENGINE=1 };
struct Identity { bool engine=false; bool IsA(int) { return engine; } };
using CThing=Identity;
template<class T> struct Reader {
    T *value=nullptr;
    T *ToRead() { return value; }
    T *operator->() { return value; }
    void operator=(T *p) { value=p; }
};
struct CLockInfo { Reader<CThing> mUnit; };
template<class T> struct List {
    std::vector<T *> items; unsigned cursor=0;
    T *First() { cursor=0; return items.empty()?nullptr:items[0]; }
    T *Next() { return ++cursor<items.size()?items[cursor]:nullptr; }
    void Remove(T *p) { for(unsigned i=0;i<items.size();++i) if(items[i]==p) { items.erase(items.begin()+i); return; } }
};
struct CBattleEngine:Identity {
    List<CLockInfo> mFiredLocks;
    CBattleEngine() { engine=true; }
    void LockHit(CThing *);
};
#include NATIVE_LOCKHIT
struct SetBoundary { unsigned removals=0; void Remove(void *) { ++removals; } };
struct { SetBoundary mSet60,mSet110; } WORLD;
struct Data { int mUnknown58=1; } data;
struct Effect { unsigned killed=0; void Kill() { ++killed; } };
struct LifecycleBoundary { unsigned retired=0; void Shutdown() { ++retired; } };
struct CRound:LifecycleBoundary {
    Data *mData=&data; Effect mEffect; Reader<CThing> mUnknownEC,mUnknownE8;
    void Shutdown();
};
// The full journal/native lifetime is exercised by the coupled lifecycle suite; this older lock test
// observes the additive entry hook without substituting the native Shutdown/LockHit bodies under test.
static unsigned journal_entries;
void CE_WorldRoundShutdown(CRound *round) { if(round->mEffect.killed) std::exit(3); ++journal_entries; }
#define SUPERTYPE LifecycleBoundary
#include NATIVE_ROUND_SHUTDOWN
#undef SUPERTYPE
static unsigned scans;
static void SeekBoundary() {
#include NATIVE_SEEK_GUARD
    ++scans;
}
int main() {
    unsigned checks=0;
    auto check=[&](bool ok,const char *what) { ++checks; if(!ok) { fprintf(stderr,"FAIL %s\n",what); exit(2); } };
    CBattleEngine owner; Identity first,second;
    CLockInfo *a=new CLockInfo,*b=new CLockInfo,*c=new CLockInfo;
    a->mUnit=&first; b->mUnit=&first; c->mUnit=&second; owner.mFiredLocks.items={a,b,c};
    CRound old,rollback; old.mUnknownEC=&owner; old.mUnknownE8=&first;
    rollback.mUnknownEC=&owner; rollback.mUnknownE8=&second;
    replica=true; old.Shutdown();
    check(owner.mFiredLocks.items.size()==3&&owner.mFiredLocks.items[0]==a&&owner.mFiredLocks.items[1]==b&&owner.mFiredLocks.items[2]==c,"replica despawn preserves both same-target locks and another target exactly");
    rollback.Shutdown();
    check(owner.mFiredLocks.items.size()==3&&owner.mFiredLocks.items[2]==c,"replica rollback cleanup cannot mutate another target lock");
    check(old.mEffect.killed==1&&old.retired==1&&!old.mUnknownE8.ToRead(),"native replica teardown still retires effect reader and object boundary");
    SeekBoundary(); check(scans==0,"replica init cannot execute native seeking scan");
    replica=false; CRound local; local.mUnknownEC=&owner; local.mUnknownE8=&first; local.Shutdown();
    check(owner.mFiredLocks.items.size()==2&&owner.mFiredLocks.items[0]==b&&owner.mFiredLocks.items[1]==c,"offline native callback removes exactly its first matching lock");
    SeekBoundary(); check(scans==1,"offline native seek dispatch remains active");
    check(journal_entries==3,"native Round Shutdown calls the lifecycle boundary before effect retirement");
    for(CLockInfo *lock:owner.mFiredLocks.items) delete lock;
    printf("{\"projectile_caller_checks\":%u,\"actors_initialized\":false}\n",checks);
}
