// SPDX-License-Identifier: GPL-3.0-only
// Exact editor lifecycle functions on authored engine boundaries. These are not native scene tests.
#include "ce_editor.h"
#include "ce_modes.h"
#include "ce_editor_ui.h"
#include "ce_editor_storage.h"
#include "ce_project.h"
#include "ce_world_view.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <string>
#include <vector>
using BOOL = int;
using WORD = unsigned short;
const BOOL TRUE = 1, FALSE = 0;
enum EGameState { GAME_STATE_STARTING, GAME_STATE_PLAYING };
enum EControlType { CONTROL_GAME_MENU };
enum { MEMTYPE_CAMERA = 1, QT_USER_QUIT_TO_FRONTEND = 1, QT_RESTART_LEVEL, QT_QUIT_TO_FRONTEND,
       QT_USER_QUIT_TO_TITLE_SCREEN, QT_QUIT_TIMEOUT, QT_QUIT_TO_SYSTEM };
static unsigned failures, checks, destroyedCameras, warmups, viewInvalidations;
static void Check(bool value, const char* message) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL %s\n", message); }
}
void* operator new(std::size_t bytes, int) { return ::operator new(bytes); }
void operator delete(void* value, int) noexcept { ::operator delete(value); }
class FVector { public: float X, Y, Z; FVector(float x=0,float y=0,float z=0):X(x),Y(y),Z(z){} };
struct FMatrix {
    float m[3][3];
    void MakeRotationYawF(float angle) {
        const float c=std::cos(angle), s=std::sin(angle);
        *this=FMatrix{{{c,-s,0},{s,c,0},{0,0,1}}};
    }
    FVector operator*(const FVector& v) const {
        return FVector(m[0][0]*v.X+m[0][1]*v.Y+m[0][2]*v.Z,m[1][0]*v.X+m[1][1]*v.Y+m[1][2]*v.Z,m[2][0]*v.X+m[2][1]*v.Y+m[2][2]*v.Z);
    }
};
class CCamera {
public:
    float mZoom=1;
    FVector position;
    FMatrix orientation{{{1,0,0},{0,1,0},{0,0,1}}};
    virtual ~CCamera(){++destroyedCameras;}
    float GetZoom(){return mZoom;}
    FVector GetPos(){return position;}
    FMatrix GetOrientation(){return orientation;}
};
struct { int mGridWidth=400, mGridHeight=300; float GetWaterLevel(){return 10.f;} } MAP;
struct CGenericCamera : CCamera {};
class CController;
struct IController {
    virtual ~IController() = default;
    virtual void ReceiveButtonAction(CController*,int,float) = 0;
    virtual BOOL CanBeControlledWhenInPause() = 0;
    virtual EControlType GetControlType() = 0;
};
class CController {
public:
    IController* target=nullptr;
    void SetToControl(IController* value){target=value;}
    IController* GetToControl(){return target;}
    void RelinquishControl(){target=nullptr;}
};
using CCONTROLLER = CController;
using pKeyTrapper = bool(*)(WORD,int);
struct { pKeyTrapper trap=nullptr; pKeyTrapper GetKeytrap(){return trap;} void SetKeytrap(pKeyTrapper p){trap=p;} } LT;
class CCareer {
public:
    unsigned progress[32];
    float musicVolume = 0, soundVolume = 0;
    int invertYAxis[2] = {}, invertJetYAxis[2] = {};
    int isGod[2] = {}, vibration[2] = {}, controllerConfiguration[2] = {};
    int screenPosX = 0, screenPosY = 0;
    float GetMusicVolume() { return musicVolume; }
    float GetSoundVolume() { return soundVolume; }
    void SetMusicVolume(float value) { musicVolume = value; }
    void SetSoundVolume(float value) { soundVolume = value; }
    int GetInvertYAxis(int player) { return invertYAxis[player]; }
    int GetInvertJetYAxis(int player) { return invertJetYAxis[player]; }
    int GetIsGod(int player) { return isGod[player]; }
    int GetVibration(int player) { return vibration[player]; }
    int GetControllerConfigurationNum(int player) { return controllerConfiguration[player]; }
    void SetInvertYAxis(int player, int value) { invertYAxis[player] = value; }
    void SetInvertJetYAxis(int player, int value) { invertJetYAxis[player] = value; }
    void SetIsGod(int player, int value) { isGod[player] = value; }
    void SetVibration(int player, int value) { vibration[player] = value; }
    void SetControllerConfigurationNum(int player, int value) { controllerConfiguration[player] = value; }
    int GetScreenPosX() { return screenPosX; }
    int GetScreenPosY() { return screenPosY; }
    void SetScreenPosX(int value) { screenPosX = value; }
    void SetScreenPosY(int value) { screenPosY = value; }
    int mCareerInProgress = 0;
    void Save(char* destination);
    void SaveAndMarkInProgress(char* destination);
    char* WriteOptions(char* destination);
} CAREER;
FVector operator+(const FVector& a,const FVector& b){return FVector(a.X+b.X,a.Y+b.Y,a.Z+b.Z);}
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
    const char* name; unsigned identity; FVector position; FVector renderPosition; unsigned moves=0, indexed=0;
    CThing(const char* n,unsigned id,FVector p):name(n),identity(id),position(p),renderPosition(p){}
    bool unit=true;int allegiance=0;
    bool IsA(int) const{return unit;}
    int GetAllegiance() const{return allegiance;}
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
using CUnit=CThing; const int THING_TYPE_UNIT=1;
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
// The collision refresh after a move (production: EditorRefreshCollision, map-who sector and mesh part cache); here it
// counts, so a test sees each move refreshed.
static void EditorRefreshCollision(CThing *thing){ if (thing) ++thing->indexed; }
enum { kCollideNothing, kCollideGround, kCollideThing };
struct CLine { FVector start, end; CLine(FVector a, FVector b):start(a),end(b){} };
struct CWorldLineColReport { CThing* mHitThing=nullptr; int mHitType=kCollideNothing; float mDistToImpact=0; };
static bool fixtureSupportExists=true, fixtureSupportMesh=false, fixtureCoverExists=false, fixtureWallExists=false;
static float fixtureSupportZ=0, fixtureCoverZ=0, fixtureWallX=0;
static float fixtureSupportMinX=-1e9f;   // the support ends here (an edge): nothing to stand on at smaller x
static float fixtureStepX=1e9f, fixtureStepZ=0;   // from this x on, the support is at another height (a slope or a step)
struct {
    std::vector<CThing*> things;
    std::vector<CThing*>& GetThingNB(){return things;}
    void FindFirstThingToHitLine(CLine line, void*, CWorldLineColReport* report, bool, bool, int, int, bool) {
        const float supportZ = line.start.X>=fixtureStepX ? fixtureStepZ : fixtureSupportZ;
        if (fixtureSupportExists && line.start.X>=fixtureSupportMinX && supportZ>=line.start.Z && supportZ<=line.end.Z) {
            report->mHitType=fixtureSupportMesh ? kCollideThing : kCollideGround;
            report->mDistToImpact=supportZ-line.start.Z;
        }
        // A wall (a thing) at x = fixtureWallX, met by a level line that crosses it.
        if (fixtureWallExists && line.start.Z==line.end.Z &&
            (line.start.X-fixtureWallX)*(line.end.X-fixtureWallX)<=0 && line.start.X!=line.end.X) {
            report->mHitType=kCollideThing;
            report->mDistToImpact=std::fabs(fixtureWallX-line.start.X);
            return;
        }
        // A roof or bridge (a thing) over the start; the nearer of the two surfaces is the first hit.
        if (fixtureCoverExists && fixtureCoverZ>=line.start.Z && fixtureCoverZ<=line.end.Z &&
            (report->mHitType==kCollideNothing || fixtureCoverZ-line.start.Z<report->mDistToImpact)) {
            report->mHitType=kCollideThing;
            report->mDistToImpact=fixtureCoverZ-line.start.Z;
        }
    }
} WORLD;
struct TestPilotEngine { float COfGHeight(){return 1.f;} FVector pos=FVector(0,0,0),old=FVector(0,0,0); FVector GetOldPos(){return old;} bool dead=false; FMatrix orient{{{1,0,0},{0,1,0},{0,0,1}}}; FMatrix GetOrientation(){return orient;} FVector GetPos(){return pos;} bool IsDying(){return dead;} };
class CPlayer { public: TestPilotEngine engine; bool exists=true; TestPilotEngine* GetBattleEngine(){return exists ? &engine : nullptr;} };
struct CGame {
    CPlayer pilot; CPlayer* GetPlayer(int){return &pilot;}
    BOOL mPause=FALSE; EGameState mGameState=GAME_STATE_STARTING; int level=777,players=1,quit=0;
    bool multiplayer=false,freeCamera=false,controllerAvailable=true; CCamera* camera=nullptr; CCONTROLLER controller;
    int GetCurrentLevel(){return level;} bool IsMultiplayer(){return multiplayer;}
    int GetNPlayers(){return players;} CCONTROLLER* GetController(int){return controllerAvailable?&controller:nullptr;}
    CCamera* GetCamera(int){return camera;} CCamera* GetCurrentCamera(int){return camera;}
    bool IsFreeCameraOn(int){return freeCamera;} BOOL IsPaused(){return mPause;}
    EGameState GetGameState(){return mGameState;}
    void SetCurrentCamera(int,CCamera* value,bool dispose){CCamera* old=camera;camera=value;if(dispose)delete old;}
    void SetQuit(int value){quit=value;}
} GAME;
struct { bool menuUp=false, mShowJoypad=false; bool IsMenuUp(){return menuUp;} } GAMEINTERFACE;
struct { float simTime=0.f; const float& GetTime(){return simTime;} } EVENT_MANAGER;
struct { bool running=true; int mSelectedLevel=0,quit=0; unsigned starts=0;
    bool IsRunning(){return running;} void SetQuit(int level){quit=level;mQuit=level;++starts;} int mQuit=0; } FRONTEND;
struct { bool mRecordDemo=false,mPlaybackDemo=false,mStressTest=false,mAttractMode=false; } CLIPARAMS;
struct CDXBitmapFont {}; struct CDXTexture {};
static bool menus=true,recording=false,network=false,session=false,lockstep=false,netMenu=false,mods=false,museum=false,gallery=false,photo=false,commander=false,drawHooks=false;
bool PortCeMenus(){return menus;} bool PortRecording(){return recording;}
extern "C" int __attribute__((weak)) PortCeRecordFeaturesAllowed(void){return 0;}
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
static unsigned long long fixtureEditorTimeNs = 1000000000;
unsigned long long PortClockRawNs() { return fixtureEditorTimeNs; }
unsigned long long PortClockPeekNs(){return 0;}
void CeWorldViewInvalidate(CeWorldViewOwner){++viewInvalidations;}
float MOUSE_X=0,MOUSE_Y=0;
void ShutdownMouse(){}
#include "editor-declarations.private.inc"
#ifdef DRIVER_EDITOR_VIEW
bool EditorFit(bool);
#else
static bool EditorFit(bool){return true;}
#endif
unsigned cues = 0;
void EditorCue(int){++cues;}
EditorHitResult EditorHit(float,float){return EditorHitResult{-1,-1,-1,false,0};}
#ifndef DRIVER_EDITOR_VIEW
void EditorStartView(){}
#endif
void ProjectLabel(const char* file,char* out,size_t capacity){std::snprintf(out,capacity,"%s",file?file:"");}
static void EditorCameraUpdate(float){}
static bool EditorRay(float,float,float,CeEditorPoint*){return false;}
static bool EditorReachPointAt(float x,float y,CeEditorPoint* target){if(x<0||y<0)return false;*target={x,y,0};return true;}
// Projection is authored input in this lifecycle fixture, as for Reach Point.
// Actual surface picking is covered by the separately recorded native journey.
static bool EditorPlayerStartAt(float x,float y,CeEditorPoint& target){if(x<0||y<0)return false;target={x,y,-1};return true;}

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
    FRONTEND.running=true;FRONTEND.quit=0;FRONTEND.mQuit=0;FRONTEND.starts=0;
    CLIPARAMS={};GAME.level=777;GAME.players=1;GAME.multiplayer=GAME.freeCamera=false;
    GAME.controllerAvailable=true;GAME.controller.target=nullptr;GAME.mPause=FALSE;GAME.quit=0;
    GAME.mGameState=GAME_STATE_STARTING;GAME.camera=new CGenericCamera();LT.trap=nullptr;
    GAMEINTERFACE.menuUp=false;GAMEINTERFACE.mShowJoypad=false;EVENT_MANAGER.simTime=0.f;
    for(unsigned i=0;i<32;++i)CAREER.progress[i]=i+10;
    objectA.unit=true;objectA.allegiance=0;objectB.unit=false;objectB.allegiance=0;
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
// A refusal never fails the engine's load: the level parks paused without the editor's camera or input, the first frame asks
// for the ordinary quit to the menu, and the menu page gets the sentence with the entry career back.
static void ExpectRefusal(const char* sentence,const CCareer& career,const char* why) {
    Check(gEditorPhase==Refused&&!PortCeEditorPlaying()&&!PortCeEditorWorldViewToken()&&GAME.IsPaused(),why);
    Check(!PortCeEditorSuppressFlightHud()&&LT.trap!=EditorTrap&&GAME.controller.target!=&gEditorInput,why);
    Check(!PortCeProjectCareerAllowed()&&PortCeEditorActive(),why);
    GAME.quit=0;PortCeEditorFrame();
    Check(GAME.quit==QT_USER_QUIT_TO_FRONTEND,why);
    PortCeEditorLevelEnd();
    Check(!GAME.IsPaused()&&PortCeEditorActive()&&!PortCeProjectCareerAllowed(),why);
    Check(PortCeEditorTakeReturn()&&!PortCeEditorActive()&&CareerEquals(career),why);
    Check(!std::strcmp(PortCeEditorMessage(),sentence),why);
    if(std::strcmp(PortCeEditorMessage(),sentence))std::fprintf(stderr,"  got: %s\n",PortCeEditorMessage());
    Check(PortCeEditorMenuReturn()==777,why);
}
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
    Check(PortCeEditorAfterLoad()&&!gEditorReady,"an incomplete load parks as a refusal and never becomes editable");
    ExpectRefusal("The map did not load the way the editor expected. Nothing was changed.",initial,"incomplete load returns to the page");
    CAREER.progress[0]=123;Check(!PortCeEditorTakeReturn()&&CAREER.progress[0]==123,"repeated return never replays a stale career");
    Check(PortCeEditorMenuReturn()==0,"editor page return is consumed once");
    // Every refusal reason the load can meet: the engine's load succeeds, nothing is applied, the page shows the sentence.
    struct Refusal { const char* why; const char* sentence; };
    const Refusal mapRefusals[]={
        {"multiplayer","The editor opens one-player maps only."},{"players","The editor opens one-player maps only."},
        {"free-camera","The editor opens one-player maps only."},{"controller","The editor opens one-player maps only."},
        {"camera","The editor opens one-player maps only."},
        {"ambiguous","This map has no objects the editor can move yet."},
        {"repeated","The map did not load the way the editor expected. Nothing was changed."},
        {"other-level","The map did not load the way the editor expected. Nothing was changed."},
        {"unterminated","This map has an object the editor cannot read yet."},
        {"unprintable","This map has an object the editor cannot read yet."},
        {"too-many","This map has too many objects for the editor yet."},
    };
    for(const Refusal& r:mapRefusals){
        Reset();const CCareer entry=CAREER;Check(PortCeEditorBegin(777),"refusal case starts");
        const std::string why=std::string("refusal ")+r.why;
        if(!std::strcmp(r.why,"multiplayer"))GAME.multiplayer=true;
        if(!std::strcmp(r.why,"players"))GAME.players=2;
        if(!std::strcmp(r.why,"free-camera"))GAME.freeCamera=true;
        if(!std::strcmp(r.why,"controller"))GAME.controllerAvailable=false;
        if(!std::strcmp(r.why,"camera"))GAME.SetCurrentCamera(0,nullptr,true);
        if(!std::strcmp(r.why,"ambiguous"))WORLD.things={&objectA,&objectA};
        bool engine=true;float x=1,y=2,z=3;
        if(!std::strcmp(r.why,"repeated")){engine=PortCeProjectBeginWorld(777,false)&&PortCeProjectEndWorld(false);}
        if(!std::strcmp(r.why,"other-level")){engine=PortCeProjectBeginWorld(778,false);}
        if(!std::strcmp(r.why,"unterminated")){const char bad[3]={'A','B','C'};
            engine=PortCeProjectBeginWorld(777,false)&&PortCeProjectApplyPlacement(bad,sizeof(bad),false,&x,&y,&z);}
        if(!std::strcmp(r.why,"unprintable")){engine=PortCeProjectBeginWorld(777,false)&&PortCeProjectApplyPlacement("Bad\x01Name",9,false,&x,&y,&z);}
        if(!std::strcmp(r.why,"too-many")){engine=PortCeProjectBeginWorld(777,false);char name[32];
            for(int i=0;engine&&i<=CeEditorState::ObjectLimit;++i){std::snprintf(name,sizeof(name),"Object%d",i);x=1;y=2;z=3;
                engine=PortCeProjectApplyPlacement(name,sizeof(name),false,&x,&y,&z);}}
        if(!std::strcmp(r.why,"repeated")||!std::strcmp(r.why,"other-level")||!std::strcmp(r.why,"unterminated")||
           !std::strcmp(r.why,"unprintable")||!std::strcmp(r.why,"too-many")) {
            // The rest of the level's own load arrives after the refusal and must still be answered with success, unchanged.
            x=7;y=8;z=9;
            engine=engine&&PortCeProjectBeginWorld(777,false)&&PortCeProjectApplyPlacement("OtherAnchor",12,false,&x,&y,&z)&&
                   x==7&&y==8&&z==9&&PortCeProjectEndWorld(false)&&PortCeEditorAfterLoad();
        } else engine=Load();
        Check(engine,(why+": the engine's load is never failed").c_str());
        Check(!gEditorReady&&!gEditorCamera,(why+": nothing is parked for editing").c_str());
        if(!std::strcmp(r.why,"controller"))GAME.controllerAvailable=true;
        ExpectRefusal(r.sentence,entry,why.c_str());
    }

    Reset();CCamera* prior=GAME.camera;Check(PortCeEditorBegin(777)&&Load(),"authored world parks successfully");
    const auto firstToken=PortCeEditorWorldViewToken();
    Check(firstToken&&GAME.camera==gEditorCamera&&GAME.camera!=prior&&GAME.IsPaused(),"park owns actual camera slot and paused state");
    Check(drawHooks&&PortCeEditorSuppressFlightHud(),"park suppresses flight HUD");
    PortCeEditorFrame();Check(GAME.controller.target==&gEditorInput&&LT.trap==EditorTrap,"frame owns editor input without pilot control");
    const unsigned indexedBefore=objectA.indexed;
    Check(gEditorState.Select(0)&&gEditorState.BeginMove()&&gEditorState.Move({2,4,6})&&gEditorState.FinishMove(true)&&ApplyOne(0),"actual MoveTo adapter accepts authored placement change");
    Check(objectA.indexed==indexedBefore+1,"a moved object's collision is refreshed where it now is, so line tests find it there");
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
    {   // The working set of Open is static: a project that opens on a parked scene uses no large stack frame.
        Reset();Check(PortCeEditorBegin(777)&&Load(),"open fixture parks");
        char error[192];CeProjectDocument* document=PortCeProjectDocumentNew(777,"Authored",error,sizeof(error));
        const CeProjectPosition position={"OtherAnchor",1,2,3};
        Check(document&&PortCeProjectDocumentUpdate(document,&position,1,error,sizeof(error)),"open fixture document");
        Check(EditorApplyDocument(document,"authored.ceproject")&&gEditorFileCurrent&&!gEditorState.Dirty(),"a valid project opens and is current");
        CeProjectDocument* other=PortCeProjectDocumentNew(778,"Other map",error,sizeof(error));
        Check(other&&!EditorApplyDocument(other,"other.ceproject")&&gEditorReady,"a project for another map is refused inside the editor");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    // Supply the project a successful editor save would hand to the loader. From there use the
    // real world, frame, restart and return hooks; no test writes the runner's progress.
    Reset();
    const CCareer routeCareer=CAREER;
    Check(PortCeEditorBegin(777)&&Load(), "two-stop lifecycle enters authoring");
    const CeProjectReach stops[]={{true,0,0,0},{true,96,0,0}};
    const CeProjectPosition anchor={"AuthoredAnchor",1,2,3};
    char error[256]={};
    gEditorPlayDocument=PortCeProjectDocumentNew(777,"Authored route",error,sizeof(error));
    Check(gEditorPlayDocument&&PortCeProjectDocumentUpdate(gEditorPlayDocument,&anchor,1,error,sizeof(error),stops,2), "two-stop lifecycle gets an authored revision");
    gEditorPlayback=PortCeProjectPlaybackNew(gEditorPlayDocument,error,sizeof(error));
    Check(gEditorPlayback&&EditorPrepareReload(LoadPlaytest), "two-stop lifecycle requests test");
    PortCeEditorLevelEnd();
    Check(Load()&&PortCeEditorPlaying()&&!PortCeProjectCareerAllowed(), "two-stop lifecycle loads isolated test");
    auto pilotAt=[](float x,float previous) {
        GAME.mGameState=GAME_STATE_PLAYING;
        GAME.pilot.engine.old=FVector(previous,0,0); GAME.pilot.engine.pos=FVector(x,0,0);
        PortCeEditorFrame();
    };
    pilotAt(stops[0].x,stops[0].x);
    Check(gEditorReachStage==1&&!gEditorReached, "visiting Stop 1 leaves Stop 2 to do before restart");
    Check(PortCeEditorResolveQuit(QT_RESTART_LEVEL)==QT_RESTART_LEVEL, "ordinary test restart requests a fresh world");
    PortCeEditorLevelEnd();
    Check(Load()&&PortCeEditorPlaying(), "restarted two-stop test loads");
    pilotAt(stops[1].x,stops[1].x);
    Check(!gEditorReached, "spawning at Stop 2 after restart earns no completion");
    pilotAt(stops[0].x,stops[1].x);
    Check(!gEditorReached, "after restart visiting Stop 1 still leaves Stop 2 to do");
    pilotAt(stops[1].x,stops[0].x);
    Check(gEditorReached&&!PortCeProjectCareerAllowed(), "revisiting Stop 2 in order completes only the project");
    Check(PortCeEditorResolveQuit(QT_USER_QUIT_TO_FRONTEND)==QT_RESTART_LEVEL, "leaving the completed test reloads authoring");
    PortCeEditorLevelEnd();
    Check(Load()&&gEditorReady&&!PortCeEditorPlaying()&&CareerEquals(routeCareer), "returned authoring retains the original career");
    PortCeEditorLevelEnd();PortCeEditorTakeReturn();

    Reset();GAME.SetCurrentCamera(0,nullptr,true);std::free(gEditorCareer);gEditorCareer=nullptr;
    Check(destroyedCameras>0,"owned cameras are released");
    Check(viewInvalidations>0,"teardown invalidates shared world view");
    std::printf("native editor lifecycle: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
