// Exercise the actual authored authority and collector functions with native-shaped facts.
#include "ce_cmdorders.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static unsigned checks;
static void Require(bool ok, const char* label) {
    ++checks; if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
}
struct FVector { float X=3, Y=4, Z=5; };
struct CUnitData { const char* mName="Unit"; };
struct CSquad { int number=500; unsigned kind=256;bool shutting=false;bool IsShuttingDown(){return shutting;}
    int GetThingNumber(){return number;} unsigned GetThingType(){return kind;} };
class CUnit {
public:
    CSquad* squad=nullptr; bool dying=false,shutting=false; float life=100; int allegiance=1;
    const void* script=nullptr; int number=42; unsigned kind=1; CUnitData data; FVector position;
    CSquad* GetSquad(){if(shutting)Require(false,"shutdown reader rejected before squad query");return squad;} bool IsShuttingDown(){return shutting;} bool IsDying(){return dying;}
    float GetLife(){return life;} int GetAllegiance(){return allegiance;}
    const void* GetMissionScript(){return script;} int GetThingNumber(){return number;}
    unsigned GetThingType(){return kind;} FVector GetPos(){return position;}
    const CUnitData* GetUnitData(){return &data;}
};
enum { THING_TYPE_INFANTRY=1, THING_TYPE_GROUND_UNIT=2, THING_TYPE_AIR_UNIT=4,
    THING_TYPE_NAVAL=8, THING_TYPE_BUILDING=16, THING_TYPE_COMPONENT=32,
    THING_TYPE_EMPLACEMENT=64, THING_TYPE_MECH=128 };
enum { CE_WHY_NONE, CE_WHY_ENEMY, CE_WHY_SCRIPTED };
struct CeCmdSquad { int ordinal=0,why=0,members=0; bool friendly=true,commandable=true; };
struct Rec { CSquad* squad=nullptr; bool alive=false; CUnit* units[256]{};
    int count=0; CeCmdSquad facts; };
static Rec gRecs[4]; static int gNRecs=2,gDropped=0; static bool gEnabled=true,runtime=true,mapOn=true;
static int scans=0;
bool CeCmdUnitOrdersEligible(){return runtime;} bool CeCmdMapOn(){return mapOn;}
static void Scan(){++scans;} static int PlayerAllegiance(){return 1;}
static int Units(Rec& record,CUnit** out,int capacity){
    Require(record.alive&&record.squad,"dead record is skipped before native member traversal");
    if(record.squad->shutting)Require(false,"shutdown squad rejected before native readers");
    int count=record.count<capacity?record.count:capacity;
    for(int i=0;i<count;++i)out[i]=record.units[i];return count;
}
static void Describe(int ordinal,Rec& record,int,CeCmdSquad& out){if(record.squad->shutting)Require(false,"shutdown squad rejected before description");out=record.facts;out.ordinal=ordinal;out.members=record.count;}
#include "authority-under-test.inc"

int main(){
    gRecs[0].squad=reinterpret_cast<CSquad*>(uintptr_t(1)); // Dead record must short-circuit this poison address.
    CSquad squad,other;other.number=501;CUnit first,second;first.squad=&squad;second.squad=&squad;second.number=43;
    Rec& live=gRecs[1];live.alive=true;live.squad=&squad;live.units[0]=&first;live.units[1]=&second;live.count=2;
    CeCmdUnitAuthority authority{};
    Require(CeCmdUnitAuthorityNow(&first,&authority)&&authority.found&&authority.commandable&&authority.groundPoint,"current friendly member has native authority");
    first.shutting=true;
    Require(!CeCmdUnitAuthorityNow(&first,&authority),"healthy shutting-down member has no authority");
    CeCmdUnitNativeRecord shutdownOutput[256]{};bool shutdownComplete=false;
    Require(CeCmdUnitCollectNative(shutdownOutput,256,&shutdownComplete)==1&&shutdownComplete&&shutdownOutput[0].unit==&second,"shutdown member cannot be recollected through active reader");
    first.shutting=false;
    squad.shutting=true;
    Require(!CeCmdUnitAuthorityNow(&first,&authority),"healthy unit in shutting-down squad has no authority");
    Require(CeCmdUnitCollectNative(shutdownOutput,256,&shutdownComplete)==0&&shutdownComplete,"shutting-down squad produces no fresh entity keys");
    squad.shutting=false;
    first.squad=&other;
    Require(!CeCmdUnitAuthorityNow(&first,&authority)&&!authority.found,"transferred unit is not authorized by old record");
    first.squad=&squad;first.allegiance=2;
    Require(CeCmdUnitAuthorityNow(&first,&authority)&&!authority.commandable&&!authority.friendly,"own allegiance can revoke friendly squad authority");
    first.allegiance=1;first.script=&other;
    Require(CeCmdUnitAuthorityNow(&first,&authority)&&authority.scripted&&!authority.commandable,"unit script independently revokes order authority");
    first.script=nullptr;first.life=0;
    Require(!CeCmdUnitAuthorityNow(&first,&authority),"zero-life native member is refused");
    first.life=100;first.dying=true;
    Require(!CeCmdUnitAuthorityNow(&first,&authority),"dying native member is refused");first.dying=false;
    first.kind=THING_TYPE_GROUND_UNIT|THING_TYPE_NAVAL;
    Require(CeCmdUnitAuthorityNow(&first,&authority)&&!authority.groundPoint,"naval member never gains ground point capability");first.kind=1;
    live.facts.commandable=false;
    Require(CeCmdUnitAuthorityNow(&first,&authority)&&!authority.commandable,"native squad refusal remains binding");live.facts.commandable=true;
    CeCmdUnitNativeRecord output[256]{};bool complete=false;int before=scans;
    Require(CeCmdUnitCollectNative(output,256,&complete)==2&&complete&&scans==before+1,"bulk snapshot scans current world once");
    Require(output[0].unit==&first&&output[0].squadNumber==squad.number&&output[0].authority.squad==&squad&&output[0].members==2,"bulk snapshot uses current native membership and identity");
    Require(output[0].position[0]==3&&output[0].position[1]==4&&output[0].position[2]==5&&!std::strcmp(output[0].name,"Unit"),"bulk snapshot carries observed position and bounded name");
    Require(CeCmdUnitCollectNative(output,1,&complete)==1&&!complete,"native overflow cannot claim complete snapshot");
    first.allegiance=2;
    Require(CeCmdUnitCollectNative(output,256,&complete)==1&&complete&&output[0].unit==&second,"native snapshot excludes member with foreign allegiance");first.allegiance=1;
    live.units[1]=&first;
    Require(CeCmdUnitCollectNative(output,256,&complete)==1&&complete,"duplicate native readers produce one unit");live.units[1]=&second;
    gDropped=1;
    Require(CeCmdUnitCollectNative(output,256,&complete)==2&&!complete,"untracked native squads make snapshot incomplete");gDropped=0;
    live.count=256;for(int i=2;i<256;++i)live.units[i]=&first;
    Require(CeCmdUnitCollectNative(output,256,&complete)==2&&!complete,"saturated native member reader cannot claim completeness");live.count=2;
    runtime=false;before=scans;
    Require(!CeCmdUnitAuthorityNow(&first,&authority)&&CeCmdUnitCollectNative(output,256,&complete)==0&&!complete&&scans==before,"ordinary solo gate precedes native reads");runtime=true;
    mapOn=false;before=scans;
    Require(!CeCmdUnitAuthorityNow(&first,&authority)&&CeCmdUnitCollectNative(output,256,&complete)==0&&!complete&&scans==before,"disabled feature performs no native scan");mapOn=true;
    Require(CeCmdUnitCollectNative(nullptr,256,&complete)==0&&!complete,"invalid output cannot claim usable snapshot");
    std::printf("checks=%u failures=0 skips=0\n",checks);return 0;
}
