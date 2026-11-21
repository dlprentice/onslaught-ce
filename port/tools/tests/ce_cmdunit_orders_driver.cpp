#include "ce_cmdunit_orders_fixture.h"
#include "ce_commander.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <new>
#include <initializer_list>

bool fixtureReadsForbidden=false;
bool fixtureRuntime=true;
bool fixtureLand=true;
bool fixtureSkipPoint=false;
void (*fixtureAfterPoint)()=NULL;
void (*fixtureAfterStop)()=NULL;
static CUnit* registry[256]={};
static unsigned checks=0;
void FixtureRead(){if(fixtureReadsForbidden)std::abort();}
void FixtureRegister(CUnit* u){for(auto& p:registry)if(!p){p=u;return;}std::abort();}
void FixtureUnregister(CUnit* u){for(auto& p:registry)if(p==u){p=NULL;return;}}
static void Require(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
static bool Equal(const FVector& a,const FVector& b){return a.X==b.X&&a.Y==b.Y&&a.Z==b.Z;}
void CeCmdSquadGone(const void*) {}
bool CeCmdUnitOrdersEligible(){return fixtureRuntime;}
bool CeCmdUnitAuthorityNow(CUnit* candidate,CeCmdUnitAuthority* out){
    if(!out)return false;*out=CeCmdUnitAuthority{};
    for(CUnit* u:registry)if(u==candidate&&u){
        if(u->shutting||u->dying||u->life<=0||(u->squad&&u->squad->IsShuttingDown()))return false;
        out->found=true;out->commandable=u->commandable;out->groundPoint=u->ground;
        out->squad=u->squad;out->number=u->number;out->kind=u->kind;out->ordinal=1;return true;
    }
    return false;
}
int CeCmdUnitCollectNative(CeCmdUnitNativeRecord* out,int capacity,bool* complete){
    *complete=true;int count=0;
    for(CUnit* u:registry)if(u){
        CeCmdUnitAuthority authority{};
        if(!CeCmdUnitAuthorityNow(u,&authority)||!u->squad)continue;
        if(count==capacity){*complete=false;continue;}
        auto& item=out[count++];item={};item.unit=u;item.authority=authority;
        item.authority.friendly=true;item.squadNumber=10000+u->number;item.squadKind=100;
        item.members=1;item.position[0]=u->position.X;item.position[1]=u->position.Y;item.position[2]=u->position.Z;
        std::snprintf(item.name,sizeof(item.name),"Fixture unit");
    }
    return count;
}
static bool fixtureVisited=false;
static bool VisitCurrent(CUnit* unit,int number,void* expected){
    fixtureVisited=true;return unit==expected&&unit->number==number;
}
bool CeCmdUnitGroundPoint(float x,float y,float out[3]){
    if(!fixtureLand||!std::isfinite(x)||!std::isfinite(y))return false;
    out[0]=x;out[1]=y;out[2]=0;return true;
}
struct Actor {
    CGuide guide;CUnit unit;CSquad squad{1};
    Actor(){unit.mGuide=&guide;guide.unit=&unit;unit.squad=&squad;}
};
static CeCommandLifetimeKey Watch(Actor& a){CeCommandLifetimeKey k{};CeCmdUnitAuthority facts{};Require(CeCmdUnitWatch(&a.unit,&k,&facts)&&facts.found,"watch current native member");return k;}
static bool Owned(const CeCommandLifetimeKey& k,CeCmdUnitOrderKind kind){CeCmdUnitOrderView v{};return CeCmdUnitOrderRead(k,&v)&&v.owned&&v.order==kind;}
static void EndDuringPoint(){CeCmdUnitOrdersWorldEnd();fixtureAfterPoint=NULL;}
static void EndDuringStop(){CeCmdUnitOrdersWorldEnd();fixtureAfterStop=NULL;}
static Actor* callbackActor=NULL;
static CeCommandLifetimeKey callbackKey{};
static void RoutineDuringCall(){fixtureAfterPoint=NULL;fixtureAfterStop=NULL;callbackActor->guide.GoToPoint(FVector(90,91,0),false);}
static void OverrideDuringCall(){fixtureAfterPoint=NULL;fixtureAfterStop=NULL;callbackActor->guide.GoToPoint(FVector(90,91,0),true);}
static void StateDuringCall(){fixtureAfterPoint=NULL;fixtureAfterStop=NULL;++callbackActor->unit.state;}
static void DeleteDuringCall(){fixtureAfterPoint=NULL;fixtureAfterStop=NULL;delete callbackActor;callbackActor=NULL;}
static void NestedDuringCall(){fixtureAfterPoint=NULL;fixtureAfterStop=NULL;Require(CeCmdUnitMove(callbackKey,44,45),"nested move accepted");}
static bool Point(const CeCommandLifetimeKey& key,CeCmdUnitOrderKind kind,FVector point){
    CeCmdUnitOrderView view{};return CeCmdUnitOrderRead(key,&view)&&view.owned&&view.order==kind&&
        view.destination[0]==point.X&&view.destination[1]==point.Y&&view.destination[2]==point.Z;
}

int main(){
    Actor a;const FVector native(30,40,0);
    Require(PortCeCmdGuidePoint(NULL,false),"absent guide preserves native behavior");
    a.guide.GoToPoint(native,false);Require(a.guide.writes==1,"unowned native point remains enabled");
    auto key=Watch(a);const int state=a.unit.state;const CSquad* squad=a.unit.squad;
    Require(CeCmdUnitMove(key,10,20),"selected move accepted");
    Require(Owned(key,CE_UNIT_ORDER_MOVE),"move ownership visible");
    Require(a.guide.writes==2&&Equal(a.guide.destination,FVector(10,20,0)),"native point receives requested destination");
    a.guide.GoToPoint(native,false);Require(a.guide.writes==2,"routine maintenance cannot replace selected point");
    Require(a.unit.state==state&&a.unit.squad==squad,"move keeps native AI state and squad");
    fixtureLand=false;Require(!CeCmdUnitMove(key,60,70),"invalid shore point refused");
    Require(Owned(key,CE_UNIT_ORDER_MOVE)&&Equal(a.guide.destination,FVector(10,20,0)),"refusal preserves prior legal point");fixtureLand=true;
    Require(CeCmdUnitMove(key,12,22),"later individual point replaces old one");
    a.guide.GoToPoint(native,true);Require(a.guide.writes==4&&!Owned(key,CE_UNIT_ORDER_MOVE),"explicit native override wins and releases ownership");
    Require(CeCmdUnitMove(key,10,20),"move before explicit squad replacement");
    CeCmdUnitOrderRelease(&a.unit);a.guide.GoToPoint(native,false);
    Require(!Owned(key,CE_UNIT_ORDER_MOVE)&&Equal(a.guide.destination,native),"released member follows later current squad point");
    a.unit.state=state;Require(CeCmdUnitMove(key,10,20),"move before return AI");
    const int stops=a.unit.stops;Require(CeCmdUnitReturnAI(key)&&a.unit.stops==stops+1,"return AI clears only temporary guide point");
    Require(!Owned(key,CE_UNIT_ORDER_MOVE)&&a.unit.state==state&&a.unit.squad==squad,"return preserves current squad and AI state");
    a.guide.GoToPoint(native,false);Require(Equal(a.guide.destination,native),"current native AI can write again");
    Require(!CeCmdUnitReturnAI(key)&&a.unit.stops==stops+1,"duplicate return is inert");
    Require(CeCmdUnitHold(key)&&Owned(key,CE_UNIT_ORDER_HOLD),"hold is an owned native stop");
    const int heldWrites=a.guide.writes;a.guide.GoToPoint(native,false);Require(a.guide.writes==heldWrites,"hold resists routine point replacement");
    Require(CeCmdUnitReturnAI(key),"hold returns to current AI");
    Require(CeCmdUnitMove(key,10,20),"move before script change");IScript script{1};a.unit.script=&script;
    const int scriptStops=a.unit.stops;Require(!CeCmdUnitReturnAI(key)&&a.unit.stops==scriptStops,"stale return cannot stop a new script owner");a.unit.script=NULL;
    Require(CeCmdUnitMove(key,10,20),"move before squad transfer");CSquad other{2};a.unit.squad=&other;a.guide.GoToPoint(native,false);
    Require(!Owned(key,CE_UNIT_ORDER_MOVE)&&Equal(a.guide.destination,native),"squad transfer releases the point");a.unit.squad=&a.squad;
    Require(CeCmdUnitMove(key,10,20),"move before guide replacement");CGuide replacement;replacement.unit=&a.unit;a.unit.mGuide=&replacement;
    const int oldWrites=a.guide.writes;a.guide.GoToPoint(native,false);Require(a.guide.writes==oldWrites+1,"obsolete guide is not held by stale ownership");a.unit.mGuide=&a.guide;
    Require(CeCmdUnitMove(key,10,20),"move before AI state transfer");++a.unit.state;a.guide.GoToPoint(native,false);Require(!Owned(key,CE_UNIT_ORDER_MOVE),"AI state transfer releases ownership");a.unit.state=state;
    Require(CeCmdUnitMove(key,10,20),"move before allegiance transfer");++a.unit.allegiance;a.guide.GoToPoint(native,false);Require(!Owned(key,CE_UNIT_ORDER_MOVE),"allegiance transfer releases ownership");--a.unit.allegiance;
    Require(CeCmdUnitMove(key,10,20),"move before mode fence");fixtureRuntime=false;const int fencedWrites=a.guide.writes;a.guide.GoToPoint(native,false);
    Require(a.guide.writes==fencedWrites+1,"disabled recording or network context preserves native point");Require(!CeCmdUnitMove(key,1,2),"fenced mode cannot dispatch new move");fixtureRuntime=true;
    a.unit.commandable=false;Require(!CeCmdUnitMove(key,1,2),"uncommandable member has no move");a.unit.commandable=true;a.unit.ground=false;Require(!CeCmdUnitMove(key,1,2)&&!CeCmdUnitHold(key),"unsupported actor has no false ground actions");a.unit.ground=true;
    auto forged=key;++forged.generation;Require(!CeCmdUnitMove(forged,1,2),"forged lifetime refused");
    Require(CeCmdUnitMove(key,10,20),"move before destruction notification");fixtureReadsForbidden=true;PortCeCmdThingGone(&a.unit);PortCeCmdThingGone(&a.unit);PortCeCmdThingGone(NULL);fixtureReadsForbidden=false;
    Require(!CeCmdUnitMove(key,1,2),"destroyed lifetime key is invalid");auto replacementKey=Watch(a);Require(replacementKey.id==key.id&&replacementKey.generation!=key.generation,"same address and same native id get a new lifetime");
    Require(!CeCmdUnitMove(key,1,2)&&CeCmdUnitMove(replacementKey,3,4),"replacement accepts only new identity");
    const int worldStops=a.unit.stops;fixtureReadsForbidden=true;CeCmdUnitOrdersWorldEnd();CeCmdUnitOrdersWorldEnd();fixtureReadsForbidden=false;Require(a.unit.stops==worldStops,"world teardown never calls native actors");
    auto worldKey=Watch(a);Require(worldKey.world!=replacementKey.world&&!CeCmdUnitMove(replacementKey,1,2),"world reload rejects old key");
    fixtureAfterPoint=&EndDuringPoint;Require(!CeCmdUnitMove(worldKey,8,9),"world end during dispatch cannot report a live owned order");
    auto nextKey=Watch(a);fixtureAfterStop=&EndDuringStop;Require(!CeCmdUnitHold(nextKey),"world end during hold cannot retain ownership");
    auto finalKey=Watch(a);Require(CeCmdUnitMove(finalKey,8,9),"new world can own a new order");a.guide.GoToPoint(native,true);Require(!Owned(finalKey,CE_UNIT_ORDER_MOVE),"later external override supersedes the current order");
    CeCmdUnitOrdersWorldEnd();
    auto causalKey=Watch(a);callbackActor=&a;
    fixtureAfterPoint=&RoutineDuringCall;Require(CeCmdUnitMove(causalKey,10,20),"callback routine write cannot steal move");
    Require(Equal(a.guide.destination,FVector(10,20,0))&&Point(causalKey,CE_UNIT_ORDER_MOVE,FVector(10,20,0)),"one-shot move retains actual requested point");
    fixtureAfterPoint=&OverrideDuringCall;Require(!CeCmdUnitMove(causalKey,10,20),"callback explicit override supersedes dispatch");
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE)&&Equal(a.guide.destination,FVector(90,91,0)),"override destination has no stale CE owner");
    a.unit.position=FVector(5,6,7);fixtureAfterStop=&RoutineDuringCall;
    const int beforeHeld=a.guide.writes;Require(CeCmdUnitHold(causalKey),"hold rejects callback routine point");
    Require(a.guide.writes==beforeHeld&&Point(causalKey,CE_UNIT_ORDER_HOLD,a.unit.position),"hold snapshot has actual stopped position");
    fixtureAfterStop=&OverrideDuringCall;Require(!CeCmdUnitHold(causalKey)&&!Owned(causalKey,CE_UNIT_ORDER_HOLD),"hold callback explicit override wins");
    fixtureSkipPoint=true;Require(!CeCmdUnitMove(causalKey,10,20),"missing native guide write refuses ownership");fixtureSkipPoint=false;
    const int beforeSkipped=a.guide.writes;a.guide.GoToPoint(native,false);
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE)&&a.guide.writes==beforeSkipped+1,"unobserved dispatch releases routine writes");
    fixtureAfterPoint=&StateDuringCall;Require(!CeCmdUnitMove(causalKey,10,20),"state transfer during move refuses ownership");a.unit.state=state;
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE),"failed move cannot revive after state returns");
    fixtureAfterStop=&StateDuringCall;Require(!CeCmdUnitHold(causalKey),"state transfer during hold refuses ownership");a.unit.state=state;
    Require(!Owned(causalKey,CE_UNIT_ORDER_HOLD),"failed hold cannot revive after state returns");
    callbackKey=causalKey;fixtureAfterPoint=&NestedDuringCall;
    Require(!CeCmdUnitMove(causalKey,10,20),"outer dispatch cannot claim a newer nested order");
    Require(Point(causalKey,CE_UNIT_ORDER_MOVE,FVector(44,45,0)),"outer failure preserves newer nested ownership");
    fixtureAfterStop=&EndDuringStop;Require(!CeCmdUnitReturnAI(causalKey),"return cannot claim AI after world ended");
    causalKey=Watch(a);callbackKey=causalKey;Require(CeCmdUnitMove(causalKey,10,20),"move before reentrant return");
    fixtureAfterStop=&NestedDuringCall;
    Require(!CeCmdUnitReturnAI(causalKey),"return cannot claim AI after newer order");
    fixtureAfterStop=NULL;Require(Point(causalKey,CE_UNIT_ORDER_MOVE,FVector(44,45,0)),"return preserves newer callback order");
    Actor neighbour;neighbour.unit.number=2;auto neighbourKey=Watch(neighbour);
    Require(CeCmdUnitMove(neighbourKey,20,30),"neighbour owns separate order");
    a.guide.GoToPoint(native,true);Require(Owned(neighbourKey,CE_UNIT_ORDER_MOVE),"guide override cannot clear neighbour");
    Require(CeCmdUnitMove(causalKey,10,20),"move before isolated release");CeCmdUnitOrderRelease(&a.unit);a.guide.GoToPoint(native,false);
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE)&&Owned(neighbourKey,CE_UNIT_ORDER_MOVE),"release affects only its native unit");
    Require(CeCmdUnitMove(causalKey,10,20),"move before isolated destruction");PortCeCmdThingGone(&a.unit);
    Require(Owned(neighbourKey,CE_UNIT_ORDER_MOVE),"unit destruction preserves neighbour order");
    causalKey=Watch(a);Require(CeCmdUnitMove(causalKey,10,20),"move before squad destruction");PortCeCmdThingGone(&a.squad);
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE)&&Owned(neighbourKey,CE_UNIT_ORDER_MOVE),"squad destruction releases only affected orders");
    Require(CeCmdUnitMove(causalKey,10,20),"move before temporary membership loss");FixtureUnregister(&a.unit);
    CeCmdUnitOrderView absent{};Require(!CeCmdUnitOrderRead(causalKey,&absent),"absent member cannot be read");FixtureRegister(&a.unit);
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE),"failed read retires old ownership");
    Require(CeCmdUnitMove(causalKey,10,20),"move before unavailable return");FixtureUnregister(&a.unit);
    Require(!CeCmdUnitReturnAI(causalKey),"absent member cannot return AI");FixtureRegister(&a.unit);
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE),"failed return retires old ownership");
    Require(CeCmdUnitMove(causalKey,10,20),"move before dying hook");a.unit.dying=true;a.guide.GoToPoint(native,false);a.unit.dying=false;
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE),"dying hook retires ownership");
    Require(CeCmdUnitMove(causalKey,10,20),"move before zero-life hook");a.unit.life=0;a.guide.GoToPoint(native,false);a.unit.life=100;
    Require(!Owned(causalKey,CE_UNIT_ORDER_MOVE),"zero-life hook retires ownership");
    CeCmdUnitOrdersWorldEnd();
    for(bool hold : {false,true}){
        callbackActor=new Actor;callbackActor->unit.number=3;const auto deletedKey=Watch(*callbackActor);
        if(hold)fixtureAfterStop=&DeleteDuringCall;else fixtureAfterPoint=&DeleteDuringCall;
        Require(!(hold?CeCmdUnitHold(deletedKey):CeCmdUnitMove(deletedKey,1,2)),"actual destruction during dispatch refuses ownership");
        Require(callbackActor==NULL&&!CeCmdUnitOrderRead(deletedKey,&absent),"deleted actor has no readable identity");
    }
    alignas(Actor) unsigned char storage[sizeof(Actor)];Actor* reused=new(storage) Actor;reused->unit.number=4;
    const auto oldStorageKey=Watch(*reused);Require(CeCmdUnitMove(oldStorageKey,1,2),"placement actor owns order");reused->~Actor();
    reused=new(storage) Actor;reused->unit.number=4;const auto newStorageKey=Watch(*reused);
    Require(oldStorageKey.id==newStorageKey.id&&oldStorageKey.generation!=newStorageKey.generation,"real reused storage changes lifetime");
    Require(!CeCmdUnitMove(oldStorageKey,1,2)&&CeCmdUnitMove(newStorageKey,3,4),"real reused storage rejects old key");reused->~Actor();
    CeCmdUnitOrdersWorldEnd();
    Actor capacity[65];CeCommandLifetimeKey capacityKeys[65];
    for(unsigned i=0;i<65;++i){capacity[i].unit.number=100+int(i);capacityKeys[i]=Watch(capacity[i]);}
    for(unsigned i=0;i<64;++i)Require(CeCmdUnitMove(capacityKeys[i],float(i),1),"bounded order slot accepts member");
    Require(!CeCmdUnitMove(capacityKeys[64],64,1),"full live ownership refuses overflow");
    capacity[0].unit.commandable=false;
    Require(CeCmdUnitMove(capacityKeys[64],64,1),"full ownership reclaims only stale slot");
    Require(Owned(capacityKeys[1],CE_UNIT_ORDER_MOVE),"capacity reclamation preserves live neighbour");
    CeCmdUnitOrdersWorldEnd();
    CeCmdUnitSnapshot snapshot[256]{};bool complete=true;
    Require(CeCmdUnitCollect(snapshot,1,&complete)==1&&!complete,"truncated native snapshot remains incomplete");
    Require(CeCmdUnitCollect(snapshot,256,&complete)==67&&complete,"bulk snapshot observes all current members");
    CeCmdUnitSnapshot first{};auto snapshotKey=Watch(a);
    Require(CeCmdUnitInspect(snapshotKey,&first)&&first.key.world==CeCmdUnitWorld(),"snapshot reinspection finds current lifetime");
    Require(first.squadKey.world==first.key.world&&first.squadKey.generation&&first.squadKey.id!=first.key.id,"snapshot carries distinct squad lifetime");
    Require(!first.orderOwned&&first.friendly&&first.commandable,"snapshot reports actual current authority");
    Require(CeCmdUnitMove(snapshotKey,19,23),"snapshot member receives move");
    Require(CeCmdUnitInspect(snapshotKey,&first)&&first.orderOwned,"snapshot reports current individual owner");
    ++a.unit.state;
    Require(CeCmdUnitInspect(snapshotKey,&first)&&!first.orderOwned,"snapshot retires ownership after native transfer");
    a.unit.state=state;
    Require(CeCmdUnitMove(snapshotKey,3,4),"move before shutdown observation");
    a.unit.shutting=true;const int shutdownWrites=a.guide.writes;
    CeCommandLifetimeKey shutdownKey{};
    Require(!CeCmdUnitVisibleKey(&a.unit,&shutdownKey),"shutdown visible actor cannot acquire another key");
    a.guide.GoToPoint(native,false);
    Require(a.guide.writes==shutdownWrites+1,"shutdown owner releases routine guide writes");
    a.unit.shutting=false;
    Require(!Owned(snapshotKey,CE_UNIT_ORDER_MOVE),"shutdown ownership cannot revive when flags clear");
    Require(CeCmdUnitMove(snapshotKey,3,4),"move before squad shutdown observation");
    a.squad.shutting=true;const int squadShutdownWrites=a.guide.writes;
    a.guide.GoToPoint(native,false);
    Require(a.guide.writes==squadShutdownWrites+1,"shutdown squad releases routine guide writes");
    Require(!CeCmdUnitInspect(snapshotKey,&first),"unit in shutdown squad acquires no snapshot key");
    a.squad.shutting=false;
    Require(!Owned(snapshotKey,CE_UNIT_ORDER_MOVE),"squad shutdown ownership cannot revive");
    Require(CeCmdUnitMove(snapshotKey,3,4),"move before unrelated squad shutdown");
    neighbour.squad.shutting=true;PortCeCmdThingGone(&neighbour.squad);
    Require(Owned(snapshotKey,CE_UNIT_ORDER_MOVE),"unrelated squad shutdown preserves neighbour owner");
    neighbour.squad.shutting=false;
    alignas(CSquad) unsigned char squadStorage[sizeof(CSquad)];
    CSquad* reusedSquad=new(squadStorage) CSquad{77};
    a.unit.squad=reusedSquad;
    Require(CeCmdUnitInspect(snapshotKey,&first),"live transferred unit has current squad snapshot");
    const auto reusedSquadKey=first.squadKey;
    reusedSquad->shutting=true;
    Require(!CeCmdUnitInspect(snapshotKey,&first),"shutting squad cannot mint fresh key before destruction");
    reusedSquad->~CSquad();reusedSquad=new(squadStorage) CSquad{77};a.unit.squad=reusedSquad;
    Require(CeCmdUnitInspect(snapshotKey,&first)&&first.squadKey.id==reusedSquadKey.id&&first.squadKey.generation!=reusedSquadKey.generation,"same squad address and id receive new lifetime");
    Require(first.key.world==snapshotKey.world&&first.key.generation==snapshotKey.generation,"squad replacement preserves the living unit lifetime");
    a.unit.squad=&a.squad;reusedSquad->~CSquad();
    Require(CeCmdUnitInspect(snapshotKey,&first),"restored membership has current observed squad");
    const auto squadKey=first.squadKey;
    PortCeCmdThingGone(&a.squad);
    Require(CeCmdUnitInspect(snapshotKey,&first)&&first.squadKey.id==squadKey.id&&first.squadKey.generation!=squadKey.generation,"snapshot rejects reused squad lifetime");
    Require(CeCmdUnitVisit(snapshotKey,VisitCurrent,&a.unit)&&fixtureVisited,"visitor receives revalidated native member");
    PortCeCmdThingGone(&a.unit);fixtureVisited=false;
    Require(!CeCmdUnitVisit(snapshotKey,VisitCurrent,&a.unit)&&!fixtureVisited,"stale lifetime never reaches native visitor");
    CeCommandLifetimeKey visible{};
    Require(CeCmdUnitVisibleKey(&a.unit,&visible)&&visible.id==snapshotKey.id&&visible.generation!=snapshotKey.generation,"visible key uses destruction-aware identity");
    Require(CeCmdUnitInspect(visible,&first)&&!CeCmdUnitInspect(snapshotKey,&first),"fresh snapshot cannot revive old unit key");
    fixtureRuntime=false;complete=true;
    Require(CeCmdUnitCollect(snapshot,256,&complete)==0&&!complete,"ineligible mode has no usable snapshot");
    Require(!CeCmdUnitVisibleKey(&a.unit,&visible),"ineligible mode cannot watch visible member");
    fixtureRuntime=true;const auto oldWorld=CeCmdUnitWorld();CeCmdUnitOrdersWorldEnd();
    Require(CeCmdUnitWorld()!=oldWorld&&!CeCmdUnitVisit(visible,VisitCurrent,&a.unit),"world boundary retires snapshot visitor keys");
    CeCmdUnitOrdersWorldEnd();std::printf("checks=%u failures=0 skips=0\n",checks);return 0;
}
