// SPDX-License-Identifier: GPL-3.0-only
// Actual VM/stack/list/monitor/resume bodies; authored instructions, resources, movement and event-clock boundaries.
// No game actor initialization, retail scripts, hardware, network or class-layout acceptance is claimed.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <vector>
using BOOL=int;using SINT=int;using DWORD=uint32_t;using UBYTE=unsigned char;
#define TRUE 1
#define FALSE 0
#define PC 1
#define TARGET PC
#define SIMPLE_MESHES 1
#define MEMTYPE_SPTRSET 1
#define MEMTYPE_DELETION_CALLBACK_LIST 2
#define MT_SCRIPT 3
#define START_OF_FRAME 0
#define NEXT_FRAME -1.f
#define SAFE_DELETE(p) do{delete(p);(p)=nullptr;}while(0)
#define ASSERT(v) do{if(!(v))std::abort();}while(0)
void *operator new(size_t n,int){return ::operator new(n);}void *operator new[](size_t n,int){return ::operator new[](n);}
void operator delete(void *p,int)noexcept{::operator delete(p);}void operator delete[](void *p,int)noexcept{::operator delete[](p);}
static unsigned checks,instructions,values_deleted,codes_deleted,stops,moves,reschedules,arrivals;
static bool blocked;
static std::vector<int> deletion_order;
// Expected causal failures exit immediately; successful cases still run full destructor/LeakSanitizer checks.
static void Check(bool value,const char *message){++checks;if(!value){fprintf(stderr,"FAIL %s\n",message);std::_Exit(2);}}
extern "C" int CE_SessionMissionVMBlocked(){return blocked?1:0;}
struct Log {void AddMessage(const char*,...){ }void AddString(const char*){ }} LOG;
#include "SPtrSet.h"
#include "Monitor.h"
#include "activereader.h"
#include "Event.h"
#include "scheduledevent.h"
#include "lists.inc"
#include "monitor.inc"
#include "reader.inc"
#include "events.inc"
struct FVector {float X,Y,Z;FVector(float x=0,float y=0,float z=0):X(x),Y(y),Z(z){}FVector operator-(FVector b){return {X-b.X,Y-b.Y,Z-b.Z};}float MagnitudeXY(){return std::sqrt(X*X+Y*Y);}};
class CThing;class CDXMemBuffer;
#include "datatype.inc"
struct Value: CDataType {int id;explicit Value(int value):id(value){}~Value(){++values_deleted;deletion_order.push_back(id);}CDataType *Clone()override{return new Value(id);}int GetType()override{return DT_INT;}float GetFloat()override{return 1.f;}};
#include "stack_type.inc"
#include "stack.inc"
class CVM;class IScript;struct CSymbolTable{};
#include "opcode.inc"
struct CAsmInstruction {int opcode;explicit CAsmInstruction(int op):opcode(op){}virtual ~CAsmInstruction()=default;int GetOpcode(){return opcode;}virtual void Execute(CVM*,CDataStack*,CSymbolTable*)=0;};
struct InstructionTable {std::vector<CAsmInstruction*> items;CAsmInstruction *at(int n){Check(n>=0&&(size_t)n<items.size(),"authored instruction resource bounds");return items[n];}};
struct CMissionScriptObjectCode {bool mInitRun=false;int mUnknown5c=0,mUnknown60=0,mEventPCs[13]{};InstructionTable mInstructions;CSymbolTable *mSymbols=nullptr;IScript *mScript=nullptr;~CMissionScriptObjectCode(){++codes_deleted;}};
#include "vm_type.inc"
#include "vm.inc"
class CThing: public CMonitor {public:FVector pos;bool IsA(unsigned mask){return (mask&16u)!=0;}FVector GetPos(){return pos;}void Stop(){++stops;}};
struct CComplexThing:CThing {};
struct CUnit:CComplexThing {float Unknown94(){return 2.f;}void GoToPoint(FVector){++moves;}};
struct CWaypoint:CMonitor {FVector pos;CActiveReader<CWaypoint> mNext;FVector GetPos(){return pos;}};
#define THING_TYPE_UNIT 16u
#define THING_TYPE_UNKNOWN_20000000 0x20000000u
#define GAME_STATE_LEVEL_LOST 5
struct {int GetGameState(){return 3;}} GAME;
enum {SCRIPT_MESSAGE_FOLLOW_WAYPOINT=2000,SCRIPT_MESSAGE_RESUME,SCRIPT_MESSAGE_TIMER};
enum {SCRIPT_EVENT_TIMER=2};
CDataType *gEventArg=nullptr;
struct EventBoundary {std::vector<CScheduledEvent*> events;float GetTime(){return 1.f;}void AddEvent(int n,CMonitor *target,float t,int=START_OF_FRAME,CMonitor *data=nullptr){auto e=new CScheduledEvent;e->Set(n,t,target,data);events.push_back(e);++reschedules;}void Clear(){for(auto e:events)delete e;events.clear();}} EVENT_MANAGER;
class IScript:public CMonitor {public:
 CComplexThing *mDefaultThing;CMissionScriptObjectCode *mCode;CThing *mThing;CWaypoint *mWaypoint;BOOL mFollowingWaypoints,mWaitingForWaypoints;CVM *mWaypointVM;int mArrivedValue;SPtrSet<CVM> mSavedVMs;CVM *mAnimVM;
 IScript(CComplexThing*,CMissionScriptObjectCode*);~IScript();void FinishedPlayingAnim();void HandleEvent(CEvent*)override;void UpdateWaypointFollowing();void Pause(CDataType**,int,CDataType**);void Arrived(int){++arrivals;}
};
#include "script.inc"
struct Action:CAsmInstruction {IScript *suspend=nullptr;explicit Action(int op=OP_NOP):CAsmInstruction(op){}void Execute(CVM*,CDataStack*,CSymbolTable*)override{++instructions;if(suspend){Value duration(99);CDataType *args[]={&duration};suspend->Pause(args,1,nullptr);}}};
struct CodeInput {Action init,ret{OP_RETURN},action;CMissionScriptObjectCode *code=new CMissionScriptObjectCode;CodeInput(){code->mInstructions.items={&init,&ret,&action,&ret};for(auto &pc:code->mEventPCs)pc=2;}};
static void Mark(CVM &vm,CMissionScriptObjectCode *code){vm.mCode=code;vm.mPC=8;vm.mFlags=9;vm.mStackBase=0;vm.mCallDepth=0;vm.mStop=17;}
static void Entry(const char *name){
 CodeInput input,previous;input.code->mUnknown5c=1;CVM vm;Mark(vm,previous.code);vm.mStack.Push(new Value(1));blocked=true;
 if(!strcmp(name,"blocked-goto"))vm.GotoInstruction(2);
 else {CDataType *args[]={new Value(2),new Value(3)};if(!strcmp(name,"blocked-event"))vm.CallEvent(input.code,0,args,2);else vm.CallFunction(input.code,2,args,2);}
 Check(instructions==0&&vm.mPC==8&&vm.mCode==previous.code&&vm.mFlags==9&&vm.mStop==17&&!input.code->mInitRun,"blocked entry precedes VM and code mutation");
 Check(vm.mStack.Size()==0&&values_deleted==(!strcmp(name,"blocked-goto")?1u:3u),"blocked entry destroys transferred and existing owned values exactly once");
 if(strcmp(name,"blocked-goto"))Check(deletion_order==std::vector<int>({2,3,1}),"argument ownership matches native absent-handler ordering");
 delete input.code;delete previous.code;
}
static void Normal(const char *name){
 CodeInput input;CVM vm;blocked=false;input.code->mUnknown5c=1;CDataType *args[]={new Value(1),new Value(2)};
 if(!strcmp(name,"normal-event")){vm.CallEvent(input.code,0,args,2);Check(instructions==2&&input.code->mInitRun,"normal event initializes and executes handler once");CDataType *again[]={new Value(3)};vm.CallEvent(input.code,0,again,1);Check(instructions==3&&values_deleted==3,"normal repeated event retains init-once and owned cleanup");}
 else if(!strcmp(name,"normal-function")){vm.CallFunction(input.code,2,args,2);Check(instructions==2&&input.code->mInitRun&&values_deleted==2,"normal named function initializes and owns args");}
 else {delete args[0];delete args[1];vm.mCode=input.code;vm.mStack.Push(new Value(3));vm.GotoInstruction(2);Check(instructions==1&&values_deleted==3,"normal continuation executes and clears moved stack");}
 Check(vm.mStack.Size()==0&&!vm.mRunning,"normal VM returns empty and idle");delete input.code;
}
static void Move(){
 CodeInput input;CVM donor,receiver;Mark(donor,input.code);donor.mStack.Push(new Value(2));receiver.mStack.Push(new Value(1));receiver=donor;
 Check(donor.mStack.Size()==0&&receiver.mStack.Size()==1&&values_deleted==1&&receiver.mPC==8,"native assignment moves stack and clears previous receiver");
 blocked=true;receiver.GotoInstruction(2);Check(values_deleted==2&&receiver.mStack.Size()==0&&donor.mStack.Size()==0,"fenced moved continuation destroys only new owner values");delete input.code;
}
static void Resume(const char *name){
 bool block=strncmp(name,"blocked-",8)==0;CodeInput input;CUnit owner;auto script=new IScript(&owner,input.code);
 // Actual Pause and Run suspension transfer ownership: shallow saved copy, live stack count cleared by native Run.
 input.init.suspend=script;input.code->mInstructions.items={&input.init,&input.action,&input.ret};for(auto &pc:input.code->mEventPCs)pc=1;MISSION_SCRIPT_VM.mCode=input.code;MISSION_SCRIPT_VM.mPC=0;MISSION_SCRIPT_VM.mStackBase=0;MISSION_SCRIPT_VM.mStack.Push(new Value(10));blocked=false;MISSION_SCRIPT_VM.Run();
 Check(script->mSavedVMs.Size()==1&&MISSION_SCRIPT_VM.mStack.Size()==0,"actual Pause and Run leave one sole saved-stack owner");auto saved=script->mSavedVMs.First();auto event=EVENT_MANAGER.events.front();Check(event->GetData()==saved,"native scheduled resume watches actual saved VM");
 unsigned destroyed=values_deleted,executed=instructions,scheduled=reschedules;int global_pc=MISSION_SCRIPT_VM.mPC;CWaypoint waypoint;if(strstr(name,"waypoint")){script->mWaypoint=&waypoint;script->mWaitingForWaypoints=TRUE;script->mFollowingWaypoints=TRUE;script->mWaypointVM=saved;}
 blocked=block;
 if(strstr(name,"animation")){script->mAnimVM=saved;script->FinishedPlayingAnim();}
 else if(strstr(name,"waypoint")){CScheduledEvent follow;follow.Set(SCRIPT_MESSAGE_FOLLOW_WAYPOINT,0,script,nullptr);script->HandleEvent(&follow);}
 else if(strstr(name,"timer")){CScheduledEvent timer;timer.Set(SCRIPT_MESSAGE_TIMER,0,script,nullptr);script->HandleEvent(&timer);}
 else script->HandleEvent(event);
 if(block){
  Check(script->mSavedVMs.Size()==1&&script->mSavedVMs.First()==saved&&saved->mStack.Size()==1&&event->GetData()==saved&&values_deleted==destroyed,"blocked parent retains sole saved ownership until native destructor");
  Check(instructions==executed&&reschedules==scheduled&&!stops&&!moves&&!arrivals&&MISSION_SCRIPT_VM.mPC==global_pc&&MISSION_SCRIPT_VM.mStack.Size()==0,"blocked resume and waypoint parent have zero VM movement or reschedule effects");
 }else if(strstr(name,"timer")){
  Check(script->mSavedVMs.Size()==1&&instructions==executed+1,"ordinary timer calls native event while retaining suspended owner");
 }else{
  Check(script->mSavedVMs.Size()==0&&event->GetData()==nullptr&&values_deleted==destroyed+1&&instructions==executed+1,"ordinary resume moves and executes saved VM once");
  if(strstr(name,"waypoint"))Check(stops==1&&!moves&&reschedules==scheduled,"ordinary waypoint executes native stop boundary");
 }
 delete script;Check(event->GetData()==nullptr&&event->GetToCall()==nullptr&&values_deleted==destroyed+1&&codes_deleted==1,"unchanged script destructor deletes retained values and retires native event readers exactly once");
 MISSION_SCRIPT_VM.Clean();EVENT_MANAGER.Clear();
}
int main(int argc,char **argv){
 if(argc!=2)return 1;GenericSPtrSet::Init(256);const char *name=argv[1];
 if(!strcmp(name,"blocked-event")||!strcmp(name,"blocked-function")||!strcmp(name,"blocked-goto"))Entry(name);
 else if(!strcmp(name,"normal-event")||!strcmp(name,"normal-function")||!strcmp(name,"normal-goto"))Normal(name);
 else if(!strcmp(name,"stack-move"))Move();
 else if(strstr(name,"animation")||strstr(name,"message")||strstr(name,"waypoint")||strstr(name,"timer"))Resume(name);
 else return 1;
 MISSION_SCRIPT_VM.Clean();EVENT_MANAGER.Clear();GenericSPtrSet::Shutdown();
 printf("{\"case\":\"%s\",\"checks\":%u,\"instructions\":%u,\"values_deleted\":%u,\"stops\":%u,\"moves\":%u,\"real_actors_or_scripts\":false}\n",name,checks,instructions,values_deleted,stops,moves);
}
