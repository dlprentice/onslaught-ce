// Actual authored selected-entry wrapper and Take admission/capture prefix.
// The rest of possession is outside this deterministic handoff contract.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
static unsigned checks;
static void Require(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
struct CPlayer{};struct CBattleEngine{};struct CCONTROLLER{};struct CSquad{};
static bool owned=true,footValid=true,mapOpen=false,selectionValid=true;
static int releases=0,selectionChecks=0;
struct CUnit{CSquad squad;int GetThingNumber(){return 7;}CSquad* GetSquad(){return &squad;}int GetAIState(){return owned?3:9;}};
static CPlayer player;static CBattleEngine engine;static CCONTROLLER controller;static CUnit actor;
static bool gPathDrive=false,gDriveGuide=false,gOnFoot=false,gDeliveryAssigned=false,gRelaxedAssigned=false,gFootFirstPerson=false;
static void* gFootRenderedView=nullptr;static int gFireState=0,gLastNotedState=0,gThing=0,gPrevAI=0;
static uint32_t gUnitHandle=0;
static uint32_t CeEntityHandleFor(const CUnit* u){return u?1u:0u;}
static CUnit* gUnit=nullptr;static CSquad* gPreviousSquad=nullptr;static const int* gLaw=nullptr;
constexpr int CE_PLAYUNIT_CLASS_INFANTRY=0,CE_PLAYUNIT_FIRE_NONE=0;
struct Law{int law=1;};static Law classes[2];static Law* CePlayUnitClasses(){return classes;}
static bool IsOrdinaryFoot(CUnit*,CSquad*){return footValid;}
static bool IsRelaxed(CSquad*){return false;}
static bool CeFactoryIsVehicle(CUnit*){return false;}static int CeCmdDeliveredSquad(){return -1;}
static void CeCmdUnitOrderRelease(CUnit* u){Require(u==&actor,"accepted handoff releases only selected unit");++releases;owned=false;}
static void CePlayUnitSlotsReleaseUnit(CUnit*){}
static bool PortCeCmdMapOpen(){return mapOpen;}
static bool SelectionReady(uint64_t,CUnit* candidate,int,CPlayer** p,CBattleEngine** e,CCONTROLLER** c,CUnit** u,int* kind,const char** reason){
    ++selectionChecks;if(!selectionValid)return false;*p=&player;*e=&engine;*c=&controller;*u=candidate;*kind=0;if(reason)*reason=nullptr;return true;
}
#include "take-under-test.inc"
int main(){
    const char* reason=nullptr;gPathDrive=true;
    Require(!CePlayUnitTakeSelection(1,&actor,7,&reason)&&selectionChecks==1,"accepted selection eligibility cannot hide Take refusal");
    Require(owned&&releases==0&&gUnit==nullptr,"Take refusal preserves current individual owner");
    Require(reason!=nullptr,"Take refusal supplies a current reason");
    gPathDrive=false;
    Require(CePlayUnitTakeSelection(1,&actor,7,&reason),"accepted selected Take reports success");
    Require(!owned&&releases==1&&gUnit==&actor,"accepted Take releases selected individual ownership once");
    Require(gPrevAI==9&&gPreviousSquad==&actor.squad,"ownership releases before handback captures");
    owned=true;releases=0;gUnit=nullptr;mapOpen=true;int before=selectionChecks;
    Require(!CePlayUnitTakeSelection(1,&actor,7,&reason)&&selectionChecks==before,"open Commander refuses possession before selection callback");
    Require(owned&&releases==0,"open Commander cannot release an individual order");
    mapOpen=false;selectionValid=false;
    Require(!CePlayUnitTakeSelection(1,&actor,7,&reason)&&owned&&releases==0,"failed live selection leaves ownership untouched");
    selectionValid=true;footValid=false;
    Require(!CePlayUnitTakeSelection(1,&actor,7,&reason)&&owned&&releases==0,"changed infantry admission leaves ownership untouched");
    std::printf("checks=%u failures=0 skips=0\n",checks);return 0;
}
