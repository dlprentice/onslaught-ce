// SPDX-License-Identifier: GPL-3.0-only
// Exact editor lifecycle functions on authored engine boundaries. These are not native scene tests.
#include "ce_editor.h"
#include "ce_editor_storage.h"
#include "ce_project.h"
#include "ce_world_view.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <vector>
using BOOL = int;
using WORD = unsigned short;
const BOOL TRUE = 1, FALSE = 0;
enum EGameState { GAME_STATE_STARTING, GAME_STATE_PLAYING };
enum EControlType { CONTROL_GAME_MENU };
enum { MEMTYPE_CAMERA = 1, QT_USER_QUIT_TO_FRONTEND = 1 };
static unsigned failures, checks, destroyedCameras, warmups, viewInvalidations;
static void Check(bool value, const char* message) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL %s\n", message); }
}
void* operator new(std::size_t bytes, int) { return ::operator new(bytes); }
void operator delete(void* value, int) noexcept { ::operator delete(value); }
class FVector { public: float X, Y, Z; FVector(float x=0,float y=0,float z=0):X(x),Y(y),Z(z){} };
class CCamera { public: float mZoom=1; virtual ~CCamera(){++destroyedCameras;} float GetZoom(){return mZoom;} };
struct CGenericCamera : CCamera {};
struct CController;
struct IController {
    virtual ~IController() = default;
    virtual void ReceiveButtonAction(CController*,int,float) = 0;
    virtual BOOL CanBeControlledWhenInPause() = 0;
    virtual EControlType GetControlType() = 0;
};
struct CController {
    IController* target=nullptr;
    void SetToControl(IController* value){target=value;}
    IController* GetToControl(){return target;}
    void RelinquishControl(){target=nullptr;}
};
using CCONTROLLER = CController;
using pKeyTrapper = bool(*)(WORD,int);
struct { pKeyTrapper trap=nullptr; pKeyTrapper GetKeytrap(){return trap;} void SetKeytrap(pKeyTrapper p){trap=p;} } LT;
struct CCareer { unsigned progress[32]; } CAREER;
FVector operator+(const FVector& a,const FVector& b){return FVector(a.X+b.X,a.Y+b.Y,a.Z+b.Z);}
struct FMatrix {
    float m[3][3];
    FVector operator*(const FVector& v) const {
        return FVector(m[0][0]*v.X+m[0][1]*v.Y+m[0][2]*v.Z,m[1][0]*v.X+m[1][1]*v.Y+m[1][2]*v.Z,m[2][0]*v.X+m[2][1]*v.Y+m[2][2]*v.Z);
    }
};
// The engine's things carry the renderable interface as a second base, so its pointer differs from the thing's.
struct IAudibleThing { virtual ~IAudibleThing()=default; int pad[3]; };
class IRenderableThing {
  public:
    virtual ~IRenderableThing()=default;
    virtual FVector GetRenderPos()=0;
    virtual FMatrix GetRenderOrientation()=0;
};
// MoveTo moves the current translation only; the render translation stays where interpolation last left it, as paused.
struct CThing : IAudibleThing, IRenderableThing {
    const char* name; unsigned identity; FVector position; FVector renderPosition; unsigned moves=0;
    CThing(const char* n,unsigned id,FVector p):name(n),identity(id),position(p),renderPosition(p){}
    const char* GetName(){return name;} unsigned GetThingNumber(){return identity;}
    FVector GetPos(){return position;} void MoveTo(FVector value){position=value;++moves;}
    // The actor's render-position getter: the production block the patched mirror holds, then the ordinary interpolated translation.
    FVector GetRenderPos() override {
#ifdef DRIVER_CE_SNIPPET
#define CE_VERSION 1
#endif
#include "editor-render-translation.private.inc"
#undef CE_VERSION
        return renderPosition;
    }
    FMatrix GetRenderOrientation() override {return FMatrix{{{0,-1,0},{1,0,0},{0,0,1}}};}
};
// A component takes its place from its parent's render position, as the CComponent things attached to the Marshall and the Battleship
// on installed map 221 do.
struct CComponentStandIn : IRenderableThing {
    IRenderableThing* parent; FVector offset;
    CComponentStandIn(IRenderableThing* p,FVector o):parent(p),offset(o){}
    FVector GetRenderPos() override {return parent->GetRenderPos()+offset;}
    FMatrix GetRenderOrientation() override {return parent->GetRenderOrientation();}
};
template<class T> struct ListIterator {
    std::vector<T*>* list; size_t index=0;
    explicit ListIterator(std::vector<T*>* p):list(p){}
    T* First(){index=0;return list->empty()?nullptr:(*list)[0];}
    T* Next(){return ++index<list->size()?(*list)[index]:nullptr;}
};
struct { std::vector<CThing*> things; std::vector<CThing*>& GetThingNB(){return things;} } WORLD;
struct CGame {
    BOOL mPause=FALSE; EGameState mGameState=GAME_STATE_STARTING; int level=777,players=1,quit=0;
    bool multiplayer=false,freeCamera=false,controllerAvailable=true; CCamera* camera=nullptr; CCONTROLLER controller;
    int GetCurrentLevel(){return level;} bool IsMultiplayer(){return multiplayer;}
    int GetNPlayers(){return players;} CCONTROLLER* GetController(int){return controllerAvailable?&controller:nullptr;}
    CCamera* GetCamera(int){return camera;} CCamera* GetCurrentCamera(int){return camera;}
    bool IsFreeCameraOn(int){return freeCamera;} BOOL IsPaused(){return mPause;}
    void SetCurrentCamera(int,CCamera* value,bool dispose){CCamera* old=camera;camera=value;if(dispose)delete old;}
    void SetQuit(int value){quit=value;}
} GAME;
struct { bool running=true; int mSelectedLevel=0,quit=0; unsigned starts=0;
    bool IsRunning(){return running;} void SetQuit(int level){quit=level;++starts;} } FRONTEND;
struct { bool mRecordDemo=false,mPlaybackDemo=false,mStressTest=false,mAttractMode=false; } CLIPARAMS;
struct CDXBitmapFont {}; struct CDXTexture {};
static bool menus=true,recording=false,network=false,session=false,lockstep=false,netMenu=false,mods=false,museum=false,gallery=false,photo=false,commander=false,drawHooks=false;
bool PortCeMenus(){return menus;} bool PortRecording(){return recording;}
extern "C" int CE_BootRequested(){return network;}
extern "C" int CE_SessionActive(){return session;}
extern "C" int CE_LockstepRequested(){return lockstep;}
bool PortCeNetActive(){return netMenu;}
int PortModsActive(){return mods;}
extern "C" bool PortCeMuseumVisitActive() __attribute__((weak));
extern "C" bool PortCeMuseumVisitActive(){return museum;}
extern "C" bool PortCeMuseumActive() __attribute__((weak));
extern "C" bool PortCeMuseumActive(){return gallery;}
bool PortCePhotoOn(){return photo;} bool PortCeCmdSuppressFlightHud(){return commander;}
void PortCeDrawHooks(bool value){drawHooks=value;}
const char* PortGameDir(){return "/authored-unused-game";} const char* PortUserDir(){return "/authored-unused-user";}
unsigned long long PortClockRawNs(){return 1000000000;}
void CeWorldViewInvalidate(CeWorldViewOwner){++viewInvalidations;}
float MOUSE_X=0,MOUSE_Y=0;
void ShutdownMouse(){}
#include "editor-declarations.private.inc"
static void EditorFit(bool){}
static void EditorCameraUpdate(float){}
static bool EditorRay(float,float,float,CeEditorPoint*){return false;}
bool EditorTrap(WORD,int){return true;}
#include "editor-lifecycle.private.inc"
static bool Near(FVector a,FVector b){return std::fabs(a.X-b.X)<1e-4f&&std::fabs(a.Y-b.Y)<1e-4f&&std::fabs(a.Z-b.Z)<1e-4f;}
static FVector Expected(IRenderableThing* thing,FVector local,FVector translation){return thing->GetRenderOrientation()*local+translation;}
// What the unpatched pose renderer composes for one part: the thing's render orientation and its render position, nothing else.
static FVector PoseTranslation(IRenderableThing* thing,FVector local){return thing->GetRenderOrientation()*local+thing->GetRenderPos();}
static void Warmup() {
#include "editor-prerun.private.inc"
    ++warmups;
}
static bool OtherTrap(WORD,int){return true;}
static CThing objectA{"AuthoredAnchor",11,FVector(4,5,6)};
static CThing objectB{"OtherAnchor",12,FVector(7,8,9)};
static void Reset() {
    PortCeEditorCancel();
    GAME.SetCurrentCamera(0,nullptr,true);
    menus=true;recording=network=session=lockstep=netMenu=mods=museum=gallery=photo=commander=drawHooks=false;
    FRONTEND.running=true;FRONTEND.quit=0;FRONTEND.starts=0;
    CLIPARAMS={};GAME.level=777;GAME.players=1;GAME.multiplayer=GAME.freeCamera=false;
    GAME.controllerAvailable=true;GAME.controller.target=nullptr;GAME.mPause=FALSE;GAME.quit=0;
    GAME.mGameState=GAME_STATE_STARTING;GAME.camera=new CGenericCamera();LT.trap=nullptr;
    for(unsigned i=0;i<32;++i)CAREER.progress[i]=i+10;
    objectA.position=FVector(4,5,6);objectB.position=FVector(7,8,9);objectA.moves=objectB.moves=0;
    objectA.renderPosition=FVector(4,5,16);objectB.renderPosition=FVector(7,8,19);
    WORLD.things={&objectA,&objectB};warmups=0;
}
static bool Load() {
    bool ok=PortCeProjectBeginWorld(777,false);
    float x=1,y=2,z=3;
    ok=ok&&PortCeProjectApplyPlacement("AuthoredAnchor",15,false,&x,&y,&z);
    x=7;y=8;z=9;
    ok=ok&&PortCeProjectApplyPlacement("OtherAnchor",12,false,&x,&y,&z);
    return ok&&PortCeProjectEndWorld(false)&&PortCeEditorAfterLoad();
}
static bool CareerEquals(const CCareer& value){return !std::memcmp(&value,&CAREER,sizeof(value));}
int main(int argc, char**) {
    if (argc > 1) {
        Reset(); const CCareer before=CAREER;
        Check(!PortCeEditorBegin(777)&&!PortCeEditorActive()&&FRONTEND.starts==0&&CareerEquals(before),"startup project intent refuses authoring without mutation");
        GAME.SetCurrentCamera(0,nullptr,true);
        return failures?1:0;
    }
    Reset(); const CCareer initial=CAREER;
    Check(PortCeProjectCareerAllowed(),"ordinary careers remain allowed");
    Warmup();Check(warmups==1,"ordinary warmup still runs");
    bool* fences[]={&menus,&recording,&network,&session,&lockstep,&netMenu,&mods,&museum,&gallery,&FRONTEND.running,&CLIPARAMS.mRecordDemo,
                   &CLIPARAMS.mPlaybackDemo,&CLIPARAMS.mStressTest,&CLIPARAMS.mAttractMode};
    for(bool* fence:fences){bool before=*fence;*fence=!before;
        Check(!PortCeEditorBegin(777)&&!PortCeEditorActive()&&FRONTEND.starts==0&&CareerEquals(initial),"entry refusal does not load or change career");*fence=before;}
    for(const char* name:{"BEA_NET_HOST","BEA_LOCKSTEP_JOIN"}) {
        setenv(name,"authored-intent",1);Check(!PortCeEditorBegin(777)&&!PortCeEditorActive(),"pending launch environment refuses authoring");unsetenv(name);
    }
    for(int level:{0,-1,10000})Check(!PortCeEditorBegin(level),"unsupported map number refused");
    Check(PortCeEditorBegin(777)&&PortCeEditorActive()&&FRONTEND.starts==1,"request becomes active before one level start");
    Check(!PortCeProjectCareerAllowed()&&!PortCeEditorBegin(777),"request isolates career and refuses duplicate entry");
    CAREER.progress[3]=999;Warmup();Check(warmups==1,"requested authoring skips mission warmup before load completes");
    Check(!PortCeEditorAfterLoad(),"incomplete load cannot park or become editable");
    PortCeEditorLevelEnd();Check(PortCeEditorActive()&&!PortCeProjectCareerAllowed(),"failed-load teardown retains career exclusion");
    Check(PortCeEditorTakeReturn()&&!PortCeEditorActive()&&CareerEquals(initial),"failed-load return restores entry career");
    CAREER.progress[0]=123;Check(!PortCeEditorTakeReturn()&&CAREER.progress[0]==123,"repeated return never replays a stale career");
    Check(PortCeEditorMenuReturn()==777&&PortCeEditorMenuReturn()==0,"editor page return is consumed once");

    Reset();CCamera* prior=GAME.camera;Check(PortCeEditorBegin(777)&&Load(),"authored world parks successfully");
    const auto firstToken=PortCeEditorWorldViewToken();
    Check(firstToken&&GAME.camera==gEditorCamera&&GAME.camera!=prior&&GAME.IsPaused(),"park owns actual camera slot and paused state");
    Check(drawHooks&&PortCeEditorSuppressFlightHud(),"park suppresses flight HUD");
    PortCeEditorFrame();Check(GAME.controller.target==&gEditorInput&&LT.trap==EditorTrap,"frame owns editor input without pilot control");
    Check(gEditorState.Select(0)&&gEditorState.BeginMove()&&gEditorState.Move({2,4,6})&&gEditorState.FinishMove(true)&&ApplyOne(0),"actual MoveTo adapter accepts authored placement change");
    Check(objectA.position.X==5&&objectA.position.Y==7&&objectA.position.Z==9,"movement preserves object Init offset");
    {   // The parked editor answers bound placements' render position with the current translation; everything else keeps the ordinary one.
        IRenderableThing* a=&objectA; IRenderableThing* b=&objectB;
        CThing stranger("Stranger",77,FVector(40,50,60)); IRenderableThing* s=&stranger;
        stranger.renderPosition=FVector(40,50,70);
        CComponentStandIn component(a,FVector(0,0,-3)), strangerComponent(s,FVector(0,0,-3));
        const FVector local(1,2,3),mount(0,0,-3);
        const unsigned movesA=objectA.moves,movesB=objectB.moves;
        const FVector posA=objectA.position,renderA=objectA.renderPosition,posB=objectB.position,renderB=objectB.renderPosition;
        Check((void*)a!=(void*)&objectA,"authored renderable interface sits at a non-zero offset, as the engine's does");
        Check(!Near(Expected(a,local,posA),Expected(a,local,renderA)),"fixture separates the current and the ordinary render translation");
#ifdef DRIVER_CE_SNIPPET
        const bool editorDraws=true;
        const FVector boundA=posA,boundB=posB;
#else
        const bool editorDraws=false;
        const FVector boundA=renderA,boundB=renderB;   // the default build never asks the editor
#endif
        Check(Near(a->GetRenderPos(),boundA),"parked editor answers a moved placement's render position with its current translation");
        Check(Near(PoseTranslation(a,local),Expected(a,local,boundA)),"the unpatched pose composition draws the moved mesh at the current translation");
        Check(Near(PoseTranslation(b,local),Expected(b,local,boundB)),"every bound placement is corrected, not only the moved one");
        Check(Near(component.GetRenderPos(),boundA+mount),"a component of a moved placement follows its parent");
        Check(Near(s->GetRenderPos(),stranger.renderPosition)&&Near(strangerComponent.GetRenderPos(),stranger.renderPosition+mount),"an unbound thing and its component keep the ordinary translation");
        FVector got(-7,-8,-9);
        Check(PortCeEditorDrawPosition(a,&got)&&Near(got,posA),"draw query returns the live actor's current translation");
        FVector untouched(-7,-8,-9);
        Check(!PortCeEditorDrawPosition(s,&untouched)&&Near(untouched,FVector(-7,-8,-9)),"a refused query leaves the output unchanged");
        Check(!PortCeEditorDrawPosition(nullptr,&untouched)&&!PortCeEditorDrawPosition(a,nullptr),"null arguments are refused");
        auto fallsBack=[&](const char* why){
            FVector probe(-7,-8,-9);
            Check(!PortCeEditorDrawPosition(a,&probe)&&Near(probe,FVector(-7,-8,-9)),why);
            Check(Near(a->GetRenderPos(),renderA)&&Near(component.GetRenderPos(),renderA+mount),why);
        };
        GAME.mPause=FALSE; fallsBack("an unpaused world keeps the ordinary translation"); GAME.mPause=TRUE;
        CCamera* camera=GAME.camera; GAME.camera=nullptr; fallsBack("a lost camera keeps the ordinary translation"); GAME.camera=camera;
        GAME.level=778; fallsBack("a changed level keeps the ordinary translation"); GAME.level=777;
        WORLD.things={&objectB}; fallsBack("an actor no longer in the world keeps the ordinary translation"); WORLD.things={&objectA,&objectB};
        objectA.identity=12; fallsBack("a changed creation identity keeps the ordinary translation"); objectA.identity=11;
        objectA.position=FVector(NAN,0,0); fallsBack("a non-finite translation keeps the ordinary translation"); objectA.position=posA;
        gEditorRequested=false; fallsBack("no editor session keeps the ordinary translation"); gEditorRequested=true;
        Check(objectA.moves==movesA&&objectB.moves==movesB&&Near(objectA.position,posA)&&Near(objectA.renderPosition,renderA)&&
              Near(objectB.position,posB)&&Near(objectB.renderPosition,renderB),"the draw query never writes actor state");
        Check(Near(a->GetRenderPos(),boundA)&&Near(component.GetRenderPos(),boundA+mount),"the corrected translation returns once the conditions return");
        {   // The answer does not depend on the editor's own state: a saved (clean) session and a move in progress draw at the current translation too.
            gEditorState.MarkSaved();
            Check(!gEditorState.Dirty()&&Near(a->GetRenderPos(),boundA)&&Near(component.GetRenderPos(),boundA+mount),"a saved session keeps the current translation");
            Check(gEditorState.Select(0)&&gEditorState.BeginMove()&&gEditorState.Move({3,5,7})&&ApplyOne(0)&&gEditorState.Moving()&&!Near(objectA.GetPos(),posA),
                  "a move is open and has moved the actor");
            const FVector dragged=editorDraws?objectA.GetPos():renderA;
            Check(Near(a->GetRenderPos(),dragged)&&Near(component.GetRenderPos(),dragged+mount),"a move in progress draws at the dragged translation");
            Check(gEditorState.FinishMove(true),"the move closes");
        }
    }
    photo=true;PortCeEditorLevelEnd();
    Check(GAME.camera==prior&&!GAME.IsPaused()&&!GAME.controller.target&&!LT.trap&&!PortCeEditorWorldViewToken(),"teardown restores camera pause input and view ownership");
    Check(objectA.position.X==4&&objectA.position.Y==5&&objectA.position.Z==6&&drawHooks,"teardown restores actor positions and existing photo suppression");
    Check(PortCeEditorActive()&&!PortCeProjectCareerAllowed(),"successful teardown remains isolated until frontend return");
    {
        IRenderableThing* a=&objectA; FVector probe(-7,-8,-9);
        Check(!PortCeEditorDrawPosition(a,&probe)&&Near(a->GetRenderPos(),objectA.renderPosition),"after teardown every render position returns to the ordinary translation");
    }
    Check(PortCeEditorTakeReturn()&&CareerEquals(initial),"successful return restores career");
    Check(PortCeEditorBegin(777)&&Load()&&PortCeEditorWorldViewToken()!=firstToken,"new session cannot reuse old view identity");
    PortCeEditorFrame();CCamera* external=new CGenericCamera();GAME.SetCurrentCamera(0,external,true);LT.trap=OtherTrap;
    GAME.controller.target=nullptr;PortCeEditorFrame();Check(GAME.quit==QT_USER_QUIT_TO_FRONTEND,"camera owner replacement requests exit");
    PortCeEditorLevelEnd();Check(GAME.camera==external&&LT.trap==OtherTrap,"teardown never overwrites a replacement camera or input owner");
    Check(PortCeEditorTakeReturn(),"replacement owner session still returns");

    Reset();prior=GAME.camera;Check(PortCeEditorBegin(777)&&Load(),"external restoration load");
    GAME.SetCurrentCamera(0,prior,true);unsigned destroyedBefore=destroyedCameras;
    PortCeEditorLevelEnd();Check(GAME.camera==prior&&destroyedCameras==destroyedBefore,"already restored camera remains live and is not deleted");
    PortCeEditorTakeReturn();
    Reset();Check(PortCeEditorBegin(777)&&Load(),"changed-level load");
    GAME.SetCurrentCamera(0,new CGenericCamera(),true);GAME.level=778;destroyedBefore=destroyedCameras;
    PortCeEditorLevelEnd();Check(destroyedCameras==destroyedBefore+1&&!gEditorPreviousCamera,"detached previous camera is released after a level change");
    PortCeEditorTakeReturn();
    Reset();Check(PortCeEditorBegin(777)&&Load(),"second authored load");LT.trap=OtherTrap;
    PortCeEditorFrame();Check(GAME.quit==QT_USER_QUIT_TO_FRONTEND&&!gEditorInputOwned,"existing input owner blocks editor capture");
    PortCeEditorTakeReturn();Check(LT.trap==OtherTrap,"refused input owner is preserved");
    Reset();Check(PortCeEditorBegin(777),"pending cancellation start");CAREER.progress[5]=888;PortCeEditorCancel();
    Check(!PortCeEditorActive()&&CareerEquals(initial),"pre-park cancellation restores career");
    Reset();Check(PortCeEditorBegin(777),"duplicate object load start");
    WORLD.things={&objectA,&objectA};Check(!Load(),"ambiguous world identity refuses authoring");
    Check(!gEditorReady&&!PortCeProjectCareerAllowed(),"ambiguous load cannot expose partial editing");
    PortCeEditorTakeReturn();Reset();GAME.SetCurrentCamera(0,nullptr,true);std::free(gEditorCareer);gEditorCareer=nullptr;
    Check(destroyedCameras>0,"owned cameras are released");
    Check(viewInvalidations>0,"teardown invalidates shared world view");
    std::printf("native editor lifecycle: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
