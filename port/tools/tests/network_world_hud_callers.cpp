// SPDX-License-Identifier: GPL-3.0-only
// Actual SetViewpoint/interpolated-camera/HUD predicates, with controlled transform and player boundaries.
// This verifies camera ownership and HUD admission only; no native actor, math, asset or device equivalence claim.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
using SINT=int; using BOOL=int;
constexpr int TRUE=1,FALSE=0,MEMTYPE_CAMERA=0;
static unsigned checks,lookups,bad_lookups,draws;
static void Check(bool ok,const char *why) { ++checks; if(!ok) { std::fprintf(stderr,"FAIL %s\n",why); std::exit(2); } }
struct FVector {
    float X=0,Y=0,Z=0;
    FVector operator+(const FVector &o)const { return {X+o.X,Y+o.Y,Z+o.Z}; }
    FVector operator-(const FVector &o)const { return {X-o.X,Y-o.Y,Z-o.Z}; }
    FVector operator*(float f)const { return {X*f,Y*f,Z*f}; }
};
struct FMatrix {
    float tag=1;
    FMatrix operator+(const FMatrix&o)const{return {tag+o.tag};}
    FMatrix operator-(const FMatrix&o)const{return {tag-o.tag};}
    FMatrix operator*(float f)const{return {tag*f};}
    void Normalise() {} // controlled transform boundary; no arithmetic comparison is made here
};
struct CViewport { int X=0,Y=0,Width=640,Height=480; };
struct CCamera {
    virtual ~CCamera()=default;
    virtual FVector GetPos(){return {};}
    virtual FVector GetOldPos(){return {};}
    virtual FMatrix GetOrientation(){return {};}
    virtual FMatrix GetOldOrientation(){return {};}
    virtual float GetZoom(){return 1.f;}
    virtual float GetOldZoom(){return 1.f;}
    virtual void PrepareForInterpolation(){}
    virtual bool GetShowHUD()=0;
};
struct LogicalCamera:CCamera { bool hud=true; bool GetShowHUD(){return hud;} } logical[2];
struct CinematicCamera:CCamera {
#include NATIVE_CINEMATIC_HUD
} cinematic;
struct CPanCamera:CCamera { bool GetShowHUD(){return true;} };	// only named by the world adapter's lockstep branch, which is not taken here
int CE_LockstepActive(void){return 0;}
uint32_t CE_LockstepLocalPlayer(void){return 0;}	// no lockstep session: the adapter's own role mapping is under test
struct ReplicaCamera:CCamera { struct {unsigned hud;} state;
#include NATIVE_REPLICA_HUD
} replica_camera;
struct CInterpolatedCamera:CCamera {
    CInterpolatedCamera(CCamera*);
#include NATIVE_INTERPOLATED_HUD
    FMatrix mOrientation; FVector mPos; float mZoom;
};
struct CPlayer { int number=1; int GetNumber(){return number;} } players[2];
static bool active;
struct { uint32_t local_player=0; } config;
struct Game {
    CCamera *cameras[2]={&logical[0],&logical[1]};
    CCamera *GetCamera(int i){++lookups;if(i<0||i>=2){++bad_lookups;return nullptr;}return cameras[i];}
    float GetFrameRenderFraction2(){return .5f;}
} GAME;
#include NATIVE_WORLD_ACTIVE
#include NATIVE_WORLD_VIEW
void *operator new(std::size_t n,int){return ::operator new(n);}
struct CEngine {
    CViewport mViewport[2]; CPlayer *mPlayer[2]={}; CCamera *mCamera[2]={};
    void SetViewpoint(int,CCamera*,CViewport*,CPlayer*);
    CCamera *GetCameraForViewpoint(int i){return mCamera[i];}
    ~CEngine(){for(auto *c:mCamera)delete c;}
} ENGINE;
#include NATIVE_INTERPOLATED_CTOR
#include NATIVE_SET_VIEWPOINT
static void RenderOverlayForViewpoint(CPlayer*,int){++draws;}
static bool Overlay(CPlayer *player,int i){unsigned before=draws;
#include NATIVE_HUD_OVERLAY
return draws!=before;}
static bool Shared(CPlayer *player,int i){BOOL render=FALSE;if(player){
#include NATIVE_HUD_SHARED
}return render;}
static void Bind(unsigned role){config.local_player=role;players[role].number=role+1;CViewport viewport;ENGINE.SetViewpoint(0,GAME.GetCamera(role),&viewport,&players[role]);
    Check(dynamic_cast<CInterpolatedCamera*>(ENGINE.mCamera[0])!=nullptr,"actual SetViewpoint wraps the selected logical camera");
    Check(ENGINE.mPlayer[0]==&players[role],"single viewport retains selected canonical player");
    Check(!ENGINE.mCamera[0]->GetShowHUD(),"native interpolated camera HUD remains hard false");
}
int main(){
    active=true;players[0].number=1;players[1].number=2;
    const unsigned roles[]={
#ifdef HUD_GUEST_FIRST
        1,0
#else
        0,1
#endif
    };
    for(unsigned role:roles){
        GAME.cameras[role]=&logical[role];logical[role].hud=true;logical[1-role].hud=false;Bind(role);
        Check(Overlay(ENGINE.mPlayer[0],0),role?"guest logical HUD true survives native false wrapper":"host logical HUD true survives native false wrapper");
        Check(Shared(ENGINE.mPlayer[0],0),role?"guest shared HUD uses logical camera":"host shared HUD uses logical camera");
        logical[role].hud=false;logical[1-role].hud=true;
        Check(!Overlay(ENGINE.mPlayer[0],0)&&!Shared(ENGINE.mPlayer[0],0),"logical HUD false cannot use another player's true HUD");
        GAME.cameras[role]=&cinematic;Bind(role);
        Check(!Overlay(ENGINE.mPlayer[0],0)&&!Shared(ENGINE.mPlayer[0],0),"native cinematic false is preserved");
        GAME.cameras[role]=&replica_camera;replica_camera.state.hud=1;Bind(role);
        Check(Overlay(ENGINE.mPlayer[0],0)&&Shared(ENGINE.mPlayer[0],0),"replica authoritative true HUD is used");
        replica_camera.state.hud=0;Check(!Overlay(ENGINE.mPlayer[0],0)&&!Shared(ENGINE.mPlayer[0],0),"replica authoritative false HUD is preserved");
        GAME.cameras[role]=nullptr;Check(!Overlay(ENGINE.mPlayer[0],0)&&!Shared(ENGINE.mPlayer[0],0),"missing logical camera fails closed");
        GAME.cameras[role]=&logical[role];logical[role].hud=true;
        Check(!Overlay(nullptr,0)&&!Shared(nullptr,0),"missing canonical player fails closed");
    }
    config.local_player=0;players[1].number=2;
    Check(!Overlay(&players[1],0)&&!Shared(&players[1],0),"valid camera index with mismatched canonical role fails closed");
    const uint32_t malformed_roles[]={2u,3u,0xffffffffu};
    for(uint32_t malformed:malformed_roles){
        config.local_player=malformed;players[0].number=(int)malformed+1;lookups=bad_lookups=0;
        Check(!Overlay(&players[0],0)&&!Shared(&players[0],0)&&lookups==0&&bad_lookups==0,"malformed mapped role never indexes logical camera storage");
    }
    active=false;config.local_player=99;players[0].number=1;players[1].number=2;
    for(unsigned role=0;role<2;++role){GAME.cameras[role]=&logical[role];logical[role].hud=true;
        Check(Overlay(&players[role],role)&&Shared(&players[role],role),"offline original per-viewpoint true predicate retained");
        logical[role].hud=false;Check(!Overlay(&players[role],role)&&!Shared(&players[role],role),"offline original false predicate retained");}
    std::printf("{\"hud_caller_checks\":%u,\"actors_initialized\":false,\"rendered\":false}\n",checks);
}
