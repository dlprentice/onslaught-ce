// Actual authored roster Scan/Find and destruction callback with value-only facts.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
static unsigned checks;static bool nativeReadsForbidden=false;
static void Require(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
struct CSquad { int number;bool normal;int GetThingNumber(){Require(!nativeReadsForbidden,"native readers are not touched during invalidation");return number;} };
struct Rec { CSquad* squad;int thing;bool alive,seen,normal,reassert,guard;unsigned born,died; };
constexpr int kMaxSquads=8;static Rec gRecs[kMaxSquads]{};static int gNRecs=0,gDropped=0;static unsigned gTick=1;
static bool IsNormal(CSquad* squad){Require(!nativeReadsForbidden,"native type is not read during invalidation");return squad->normal;}
struct World {std::vector<CSquad*> squads;std::vector<CSquad*>& GetSquadNB(){return squads;}} WORLD;
template<class T> struct ListIterator {std::vector<T*>* rows;size_t index=0;explicit ListIterator(std::vector<T*>* value):rows(value){}T* First(){index=0;return rows->empty()?nullptr:(*rows)[0];}T* Next(){return ++index<rows->size()?(*rows)[index]:nullptr;}};
static bool gWatching=false;struct Identity {unsigned forgotten=0;void Forget(uintptr_t){++forgotten;}} gIdentities;
constexpr unsigned kOrders=4;struct Order {bool used;const void* unit;const void* squad;} gOrders[kOrders]{};
static void Clear(Order& order){order.used=false;}
#include "roster-under-test.inc"
int main(){
    alignas(CSquad) unsigned char storage[sizeof(CSquad)];CSquad* current=new(storage) CSquad{12,true};CSquad neighbour{13,false};
    WORLD.squads={current,&neighbour};Scan();
    Require(gNRecs==2&&gRecs[0].alive&&gRecs[0].normal&&!gRecs[1].normal,"initial native metadata observed");
    const int oldNumber=current->number;nativeReadsForbidden=true;
    PortCeCmdThingGone(current);
    Require(!gRecs[0].alive&&gRecs[0].squad==nullptr,"unwatched departure retires authored roster");
    Require(gRecs[1].alive&&gRecs[1].squad==&neighbour,"unrelated roster entry preserved");
    PortCeCmdThingGone(current);PortCeCmdThingGone(nullptr);
    Require(!gRecs[0].alive&&gIdentities.forgotten==0,"unwatched repeated callback is inert");
    current->~CSquad();current=new(storage) CSquad{oldNumber,false};nativeReadsForbidden=false;
    Scan();
    Require(gNRecs==3&&gRecs[2].alive&&gRecs[2].squad==current&&!gRecs[2].normal,"same storage and number receive fresh native metadata");
    Require(!gRecs[0].alive&&gRecs[0].squad==nullptr,"old roster ordinal cannot revive");
    gRecs[0].squad=reinterpret_cast<CSquad*>(uintptr_t(1));
    Require(Find(current,oldNumber)==&gRecs[2],"dead poison record is not treated as native identity");
    gWatching=true;gOrders[0]={true,current,current};gOrders[1]={true,&neighbour,&neighbour};nativeReadsForbidden=true;
    PortCeCmdThingGone(current);
    Require(!gRecs[2].alive&&!gOrders[0].used&&gOrders[1].used,"watched departure retires only affected roster and order");
    Require(gIdentities.forgotten==1&&gRecs[1].alive,"watched departure preserves neighbour identity");
    PortCeCmdThingGone(current);
    Require(!gOrders[0].used&&gOrders[1].used,"repeated base callback cannot affect neighbour");
    nativeReadsForbidden=false;current->~CSquad();
    std::printf("checks=%u failures=0 skips=0\n",checks);return 0;
}
