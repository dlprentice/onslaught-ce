// Native callbacks are shaped fakes; the controller and consumer body are real.
#include "ce_command_selection.h"
#include "ce_cmdorders.h"
#define CE_VERSION 1
#include "ce_commander.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <cstring>
namespace UnitControl=ce_command_selection;
namespace UnitPanel=ce_command_panel;
static UnitControl::SelectionController gUnitUi;
static CeCmdUnitSnapshot gUnitNative[UnitControl::kMaxUnits];
static CeCmdUnitSnapshot live[8];
static int gUnitNativeCount, liveCount;
static bool gUnitSnapshotComplete, complete, usable, allowed, eligible, open;
static uint64_t world=1;
static uint32_t gUnitContextGeneration=1, revision;
static CeCmdSquad squads[4];
static CeCommandLifetimeKey gUnitFocusKeys[UnitControl::kMaxRequestKeys];
static unsigned gUnitFocusCount;
static const int kUnitFocus=-3;
static float gViewX,gViewY,gViewSpan,gStrategicFocusZ;
static struct {int squad;float fromX,fromY,fromZ;uint64_t start;} gFocusTrip;
static int closes,canCalls,takeCalls,refreshes,individuals,groups,releases;
static bool canTake,takeAccept,closeFails,replaceOnClose,replaceOnGroup,disableOnIndividual,disableOnGroup,contextOnGroup;
static int lastVerb,lastOrdinal;
static CeCommandLifetimeKey lastKey;
static const char* note;
class CUnit {public:int index;};
static CUnit units[8];
static void Require(bool ok,const char* label){if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
static int checks;
#define Check(ok,label) do {Require(ok,label);++checks;} while(0)
static const CeCmdSquad* ByOrdinal(int n){return n>=0&&n<4&&squads[n].members?&squads[n]:nullptr;}
static bool StrategicCanAct(){return allowed;}
bool CeCmdUnitOrdersEligible(){return eligible;}
uint64_t CeCmdUnitWorld(){return world;}
static void Note(const char* s){note=s;}
static uint64_t Ns(){return 9;}
static void Refresh(){++refreshes;}
static void RefreshUnitFrame(){
 gUnitNativeCount=liveCount;gUnitSnapshotComplete=complete;
 UnitControl::UnitSnapshot rows[8]={};
 for(int i=0;i<liveCount;++i){gUnitNative[i]=live[i];auto& r=rows[i];r.key=live[i].key;
  r.facts.kind=UnitPanel::EntityKind::Unit;r.facts.relation=UnitPanel::TeamRelation::Friendly;
  r.facts.control=UnitPanel::ControlOwner::CommandAI;r.facts.availability=UnitPanel::Availability::Available;r.facts.selectable=true;
  r.facts.capabilities=UnitPanel::ActionBit(UnitPanel::Action::Move)|UnitPanel::ActionBit(UnitPanel::Action::Hold)|UnitPanel::ActionBit(UnitPanel::Action::Advance)|UnitPanel::ActionBit(UnitPanel::Action::FallBack)|UnitPanel::ActionBit(UnitPanel::Action::ReturnToAI)|UnitPanel::ActionBit(UnitPanel::Action::Focus)|UnitPanel::ActionBit(UnitPanel::Action::TakeControl);
  r.screen={float(20+40*i),20,10,true};
 }
 UnitControl::Frame f={};f.world=world;f.context_generation=gUnitContextGeneration;f.snapshot_generation=++revision;
 f.complete=complete&&usable;f.units=rows;f.unit_count=liveCount;f.targeted_actions=UnitPanel::ActionBit(UnitPanel::Action::Move)|UnitPanel::ActionBit(UnitPanel::Action::Advance);gUnitUi.BeginFrame(f);
}
static void Close(){++closes;if(closeFails)return;open=false;if(replaceOnClose)++live[0].key.generation;}
bool PortCeCmdMapOpen(){return open;}
static int FindLive(const CeCommandLifetimeKey& k){for(int i=0;i<liveCount;++i)if(UnitControl::SameKey(k,live[i].key))return i;return -1;}
int CeCmdUnitCollect(CeCmdUnitSnapshot* out,int capacity,bool* all){*all=complete&&capacity>=liveCount;int n=capacity<liveCount?capacity:liveCount;for(int i=0;i<n;++i)out[i]=live[i];return n;}
bool CeCmdUnitVisit(const CeCommandLifetimeKey& k,bool(*visitor)(CUnit*,int,void*),void* user){int n=FindLive(k);return eligible&&n>=0&&visitor(&units[n],n,user);}
static uint64_t CePlayUnitSelectionWorld(){return world;}
static bool CePlayUnitCanTakeSelection(uint64_t,CUnit*,int,const char** why){++canCalls;if(!canTake)*why="not eligible";return canTake;}
static bool CePlayUnitTakeSelection(uint64_t,CUnit*,int,const char** why){++takeCalls;if(!takeAccept)*why="Take refused";else ++releases;return takeAccept;}
static bool Individual(const CeCommandLifetimeKey& key){if(!eligible||FindLive(key)<0)return false;++individuals;lastKey=key;if(disableOnIndividual)allowed=false;return true;}
bool CeCmdUnitMove(const CeCommandLifetimeKey& k,float,float){return Individual(k);}
bool CeCmdUnitHold(const CeCommandLifetimeKey& k){return Individual(k);}
bool CeCmdUnitReturnAI(const CeCommandLifetimeKey& k){return Individual(k);}
bool CeCmdIssue(int n,int verb,float,float,const char**){++groups;lastVerb=verb;lastOrdinal=n;if(replaceOnGroup&&groups==1)++live[2].key.generation;if(disableOnGroup)eligible=false;if(contextOnGroup)++gUnitContextGeneration;return true;}
#include "consumer-under-test.inc"
static void Reset(int count=3){
 gUnitUi.Reset();liveCount=count;world=1;gUnitContextGeneration=1;complete=usable=allowed=eligible=open=true;
 canTake=takeAccept=true;closeFails=replaceOnClose=replaceOnGroup=disableOnIndividual=disableOnGroup=contextOnGroup=false;
 closes=canCalls=takeCalls=refreshes=individuals=groups=releases=0;gUnitFocusCount=0;gFocusTrip={};note=nullptr;
 std::memset(squads,0,sizeof(squads));std::memset(live,0,sizeof(live));
 for(int i=0;i<count;++i){units[i].index=i;live[i].key={1,uint64_t(10+i),1};live[i].squadKey={1,uint64_t(100+i/2),1};live[i].ordinal=i/2;live[i].commandable=live[i].friendly=live[i].groundPoint=true;live[i].position[0]=float(i*10);++squads[i/2].members;squads[i/2].ordinal=i/2;squads[i/2].friendly=squads[i/2].commandable=true;}
 RefreshUnitFrame();
}
static void Pick(int n,bool add=false){gUnitUi.PointerDown(float(20+40*n),20);gUnitUi.PointerUp(float(20+40*n),20,{add,false});}
static UnitControl::Request Request(UnitPanel::Action action,bool point=false){
 UnitControl::Request r={};r.action=action;r.has_point=point;r.point={10,20,0};r.world=world;r.context_generation=gUnitContextGeneration;r.selection_revision=gUnitUi.Panel().SelectionRevision();
 r.selected_count=gUnitUi.SelectionCount();for(int i=0;i<liveCount;++i)if(gUnitUi.IsSelected(live[i].key))r.keys[r.key_count++]=live[i].key;return r;
}
int main(){
 Reset();Pick(0);auto r=Request(UnitPanel::Action::Move,true);DispatchUnitRequest(r);
 Check(individuals==1&&groups==0&&UnitControl::SameKey(lastKey,r.keys[0]),"partial move keeps only intended key");
 Reset();Pick(0);Pick(1,true);r=Request(UnitPanel::Action::Move,true);DispatchUnitRequest(r);
 Check(groups==1&&individuals==0&&lastVerb==CE_CMD_MOVE,"whole squad moves once through native squad order");
 Reset();Pick(0);Pick(1,true);r=Request(UnitPanel::Action::Hold);DispatchUnitRequest(r);
 Check(groups==1&&individuals==0&&lastVerb==CE_CMD_HOLD,"whole squad holds once");
 Reset();Pick(0);r=Request(UnitPanel::Action::Advance,true);DispatchUnitRequest(r);
 Check(groups==0&&individuals==0,"partial advance cannot command unselected members");
 Reset();Pick(0);r=Request(UnitPanel::Action::ReturnToAI);DispatchUnitRequest(r);
 Check(individuals==1&&groups==0,"partial return releases only intended owner");
 Reset();Pick(0);Pick(1,true);squads[0].verb=CE_CMD_HOLD;r=Request(UnitPanel::Action::ReturnToAI);DispatchUnitRequest(r);
 Check(groups==1&&lastVerb==CE_CMD_RELEASE&&individuals==0,"whole standing order returns through native squad path");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);eligible=false;DispatchUnitRequest(r);
 Check(individuals==0&&groups==0&&refreshes==0,"ordinary-solo gate refuses before refresh");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);allowed=false;DispatchUnitRequest(r);
 Check(individuals==0&&groups==0,"current camera authority required");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);++world;DispatchUnitRequest(r);
 Check(individuals==0&&groups==0&&refreshes==0,"old world request refused before refresh");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);++gUnitContextGeneration;DispatchUnitRequest(r);
 Check(individuals==0&&groups==0,"old input context refused");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);Pick(1);DispatchUnitRequest(r);
 Check(individuals==0&&groups==0,"changed selection refuses old request");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);r.point.x=std::numeric_limits<float>::quiet_NaN();DispatchUnitRequest(r);
 Check(individuals==0&&groups==0,"nonfinite target refused");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);complete=false;DispatchUnitRequest(r);
 Check(individuals==0&&groups==0,"incomplete native snapshot refuses request");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);usable=false;DispatchUnitRequest(r);
 Check(individuals==0&&groups==0,"unusable view frame refuses request");
 Reset();Pick(0);r=Request(UnitPanel::Action::Move,true);++live[0].key.generation;DispatchUnitRequest(r);
 Check(individuals==0&&groups==0,"replacement after intent is not ordered");
 Reset(4);for(int i=0;i<4;++i)Pick(i,i!=0);r=Request(UnitPanel::Action::Advance,true);replaceOnGroup=true;DispatchUnitRequest(r);
 Check(groups==1,"callback replacement cannot receive second cohort order");
 Reset(4);for(int i=0;i<4;++i)Pick(i,i!=0);r=Request(UnitPanel::Action::Advance,true);disableOnGroup=true;DispatchUnitRequest(r);
 Check(groups==1,"earlier callback can revoke ordinary-solo authority");
 Reset(4);for(int i=0;i<4;++i)Pick(i,i!=0);r=Request(UnitPanel::Action::Advance,true);contextOnGroup=true;DispatchUnitRequest(r);
 Check(groups==1,"earlier callback can revoke input context");
 Reset(4);Pick(0);Pick(2,true);r=Request(UnitPanel::Action::Hold);disableOnIndividual=true;DispatchUnitRequest(r);
 Check(individuals==1,"individual callback rechecks current camera authority");
 Reset();Pick(0);r=Request(UnitPanel::Action::TakeControl);canTake=false;DispatchUnitRequest(r);
 Check(closes==0&&takeCalls==0&&releases==0,"failed selection eligibility does not close or release");
 Reset();Pick(0);r=Request(UnitPanel::Action::TakeControl);closeFails=true;DispatchUnitRequest(r);
 Check(closes==1&&takeCalls==0&&releases==0,"failed close cannot take control");
 Reset();Pick(0);r=Request(UnitPanel::Action::TakeControl);replaceOnClose=true;DispatchUnitRequest(r);
 Check(closes==1&&takeCalls==0&&releases==0,"Close callback re-resolves exact unit lifetime");
 Reset();Pick(0);r=Request(UnitPanel::Action::TakeControl);takeAccept=false;DispatchUnitRequest(r);
 Check(closes==1&&takeCalls==1&&releases==0&&note&&std::strcmp(note,"Take refused")==0,"Take refusal preserves owner and is reported");
 Reset();Pick(0);r=Request(UnitPanel::Action::TakeControl);DispatchUnitRequest(r);
 Check(closes==1&&takeCalls==1&&releases==1&&!open,"accepted Take follows Close and resolves once");
 Reset();Pick(0);Pick(1,true);r=Request(UnitPanel::Action::TakeControl);DispatchUnitRequest(r);
 Check(closes==0&&takeCalls==0,"Take requires exactly one intended selected unit");
 Reset();Pick(0);r=Request(UnitPanel::Action::TakeControl);r.selected_count=2;DispatchUnitRequest(r);
 Check(closes==0&&takeCalls==0,"blocked second selection cannot silently become single Take");
 Reset();Pick(0);r=Request(UnitPanel::Action::Focus);DispatchUnitRequest(r);
 Check(gUnitFocusCount==1&&gFocusTrip.squad==kUnitFocus&&groups==0&&individuals==0&&closes==0,"Focus updates only view trip");
 std::printf("checks=%d failures=0 skips=0\n",checks);return 0;
}
