// SPDX-License-Identifier: GPL-3.0-only
// Actual sound/layout/inversion caller bodies with controlled boundaries; HUD wrapper admission has its own native caller fixture.
// No game objects or device are initialized here.
#include <cstdio>
#include <cstdlib>
#include <cmath>
using BOOL=int;
constexpr int TRUE=1,FALSE=0,PC=1;
#define TARGET PC
#define SASSERT(a,b)
static bool network,replica;
static bool ready;
int CE_WorldCanRender() { return ready; }
static unsigned local_player;
int CE_WorldActive() { return network; }
int CE_WorldReplica() { return replica; }
int CE_WorldViewPlayer(int original) { return network?(int)local_player:original; }
static int guest_ground,guest_jet;
int CE_WorldInvertPitch(unsigned player,int ground,int original) { return network&&player==1?(ground?guest_ground:guest_jet):original; }
struct FVector {
    float X=0,Y=0,Z=0;
    FVector()=default;
    FVector(float x,float y,float z):X(x),Y(y),Z(z) {}
    void Set(float x,float y,float z) { X=x;Y=y;Z=z; }
    float MagnitudeSq() const { return X*X+Y*Y+Z*Z; }
    void operator-=(const FVector &v) { X-=v.X;Y-=v.Y;Z-=v.Z; }
    void operator+=(const FVector &v) { X+=v.X;Y+=v.Y;Z+=v.Z; }
    FVector operator-(const FVector &v) const { return FVector(X-v.X,Y-v.Y,Z-v.Z); }
};
struct FMatrix { FMatrix Transpose() const { return *this; } FVector operator*(const FVector &v) const { return v; } };
const FVector ZERO_FVECTOR;
const FMatrix ID_FMATRIX;
struct Camera { FVector pos; FVector GetPos() { return pos; } FMatrix GetOrientation() { return {}; } } cameras[2];
using CCamera=Camera;
struct Game { bool multi=true; bool IsMultiplayer() { return multi; } Camera *GetCamera(int i) { return &cameras[i]; } } GAME;
struct Platform { int GetWindowWidth() { return 640; } int GetWindowHeight() { return 480; } } PLATFORM;
struct Owner { FVector pos; FVector GetSoundPos() { return pos; } FVector GetSoundVelocity() { return {}; } } owner;
struct Reader { Owner *owner=nullptr; Owner *ToRead() { return owner; } };
enum {ST_FOLLOWDONTDIE,ST_FOLLOWANDDIE,ST_NOTRACKING,ST_SETINITIALPOSITION,ST_FIXEDPOSITION};
enum {SAMT_LEFT,SAMT_RIGHT,SAMT_OTHER};
struct Sample { int mType=SAMT_OTHER; } sample;
struct CSoundEvent { int mTrack=ST_FOLLOWDONTDIE; Reader mOwner; bool mIgnoreOwnerPos=false; FVector mPos,mVelocity; Sample *mSample=&sample; int mPan=0; };
struct { bool mFlipSpeakers=false; } CLIPARAMS;
struct CSoundManager { void UpdateSoundPosition(CSoundEvent *,BOOL); int CalculatePan(FVector) { return 0; } };
struct Player { int number=1; int GetNumber() { return number; } bool GetIsInFPV() { return true; } } player;
struct Engine { Player *GetPlayer() { return &player; } bool ground=true; bool IsWalkerOnGroundOrObject() { return ground; } } engine;
struct { int mCurrentViewpoint=0; } ENGINE;
struct Career { int ground=1,jet=0; int GetInvertYAxis(int) { return ground; } int GetInvertJetYAxis(int) { return jet; } } CAREER;
enum {BUTTON_MECH_PITCH_UP=26,BUTTON_MECH_PITCH_DOWN=28};
#include NATIVE_SOUND
static float CompassY(Engine *be) {
#include NATIVE_COMPASS
    return cy;
}
static bool DrawBody() {
    Player *mPlayer=&player; bool mVisible=false;
#include NATIVE_BODY
}
static float PitchValue(float val) {
    Engine *mBattleEngine=&engine; int button=BUTTON_MECH_PITCH_UP,mNumber=2;
#include NATIVE_PITCH
    return val;
}
static void AlphaStep(int &mWinLoseScreenAlpha,int step) {
#include NATIVE_ALPHA
}
static unsigned renders;
static void RenderBoundary() {
#include NATIVE_RENDER_GUARD
    ++renders;
}
int main() {
    unsigned checks=0;
    auto check=[&](bool ok,const char *what) { ++checks; if(!ok) { fprintf(stderr,"FAIL %s\n",what); exit(2); } };
    cameras[0].pos=FVector(100,0,0); cameras[1].pos=FVector(200,0,0); owner.pos=FVector(10,0,0);
    CSoundEvent event; event.mOwner.owner=&owner; CSoundManager sound;
    network=true; replica=true; local_player=1; player.number=2;
    sound.UpdateSoundPosition(&event,TRUE); check(event.mPos.X==-190,"guest listener never falls back to the origin");
    check(CompassY(&engine)==240,"guest compass is centered in the full screen");
    check(!DrawBody(),"guest first-person view hides its own actual body");
    guest_ground=0; guest_jet=1; engine.ground=true; check(PitchValue(.25f)==.25f,"guest ground preference differs from host without double inversion");
    engine.ground=false; check(PitchValue(.25f)==-.25f,"guest jet preference applied exactly once");
    int alpha=80; for(int render=0;render<144;++render) AlphaStep(alpha,16); check(alpha==80,"replica alpha is authoritative at different render rates");
    ready=false; RenderBoundary(); check(renders==0,"unprepared replica never reaches native render consumers");
    ready=true; RenderBoundary(); check(renders==1,"prepared world reaches native render consumers");
    replica=false; local_player=0; player.number=1; sound.UpdateSoundPosition(&event,TRUE); check(event.mPos.X==-90,"host listener uses only its local camera");
    check(CompassY(&engine)==240,"host compass is centered in the full screen");
    network=false; GAME.multi=true; player.number=1; check(CompassY(&engine)==120,"offline player1 split layout retained");
    player.number=2; check(CompassY(&engine)==360,"offline player2 split layout retained");
    sound.UpdateSoundPosition(&event,TRUE); check(event.mPos.X==-90,"offline nearest-camera sound retained");
    GAME.multi=false; check(CompassY(&engine)==240,"offline single-player center retained");
    engine.ground=true; check(PitchValue(.25f)==-.25f,"offline career inversion retained");
    printf("{\"caller_checks\":%u,\"rendered\":false,\"heard\":false}\n",checks);
}
