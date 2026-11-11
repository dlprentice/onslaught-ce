// SPDX-License-Identifier: GPL-3.0-only
// Actual adapter dispatch and native eligibility predicate with authored player/maths boundaries.
// No native actors, factories, rendering, devices or transport are initialized.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "ce_network_world.h"

using BOOL=int;
using SINT=int;
const int TRUE=1,FALSE=0,GAME_STATE_PLAYING=3;
const int BATTLE_ENGINE_STATE_WALKER=1,BATTLE_ENGINE_STATE_MORPHING_INTO_JET=2;
const int MOUSE_AXIS_MINUS=13,MOUSE_AXIS_PLUS=14;
const unsigned kInputs=16;
struct FVector {
    float X,Y,Z;
    FVector(float x=0,float y=0,float z=0):X(x),Y(y),Z(z) {}
    FVector operator^(const FVector &v) const { return FVector(Y*v.Z-Z*v.Y,Z*v.X-X*v.Z,X*v.Y-Y*v.X); }
    void Normalise() { float length=std::sqrt(X*X+Y*Y+Z*Z); if(length) { X/=length; Y/=length; Z/=length; } }
};
struct FMatrix {
    float yaw,pitch,roll;
    FMatrix(float y=0,float p=0,float r=0):yaw(y),pitch(p),roll(r) {}
    void MakeRotationYawF(float y) { yaw=y; }
    // The fixture's flat surface makes the ground pitch zero for every tested yaw.
    FVector operator*(const FVector &) const { return FVector(-std::sin(yaw),std::cos(yaw),0); }
};
struct CPlayer;
class CBattleEngine { public:
    CPlayer *player=nullptr;
    int state=BATTLE_ENGINE_STATE_WALKER;
    bool ground=true,object=false,dying=false,powered=true;
    struct { float mYaw=0,mPitch=0,mRoll=0; } mCurrentOrientation;
    FVector mPos;
    FMatrix mOrientation,mOldOrientation;
    float mShakeR=0,mYawShake=0,mPitchShake=0,mRollShake=0;
    bool IsWalkerOnGroundOrObject() { return state==BATTLE_ENGINE_STATE_WALKER&&(ground||object); }
    bool IsDying() { return dying; }
    bool GetIsPoweredUp() { return powered; }
    int GetState() { return state; }
    bool IsOnGround() { return ground; }
    bool IsOnObject() { return object; }
    CPlayer *GetPlayer() { return player; }
};
class CController { public: CPlayer *owner=nullptr; CPlayer *GetToControl() { return owner; } };
enum Mutation { MORPH,LOSE_GROUND,DIE,POWER_OFF,PAUSE,LEAVE_PLAYING } mutation;
struct CPlayer {
    CBattleEngine engine;
    unsigned number=0,calls=0,last_button=0;
    CBattleEngine *GetBattleEngine() { return &engine; }
    unsigned GetNumber() { return number; }
    void ReceiveButtonAction(CController *,unsigned,float);
};
struct {
    CPlayer players[2]; CController controllers[2];
    CController *mController[2]={&controllers[0],&controllers[1]};
    bool paused=false; int phase=GAME_STATE_PLAYING;
    CPlayer *GetPlayer(int i) { return &players[i]; }
    CController *GetController(int i) { return &controllers[i]; }
    bool IsPaused() { return paused; }
    int GetGameState() { return phase; }
} GAME;
void CPlayer::ReceiveButtonAction(CController *from,unsigned button,float) {
    if(from!=GAME.GetController(1)) std::abort();
    ++calls; last_button=button;
    if(button!=33) return;
    // The real native Morph case assigns MORPHING_INTO_JET to a flight-enabled, sufficiently charged walker.
    // Other cases are adverse receiver-boundary controls, not claims that Morph itself has those effects.
    switch(mutation) {
    case MORPH: engine.state=BATTLE_ENGINE_STATE_MORPHING_INTO_JET; break;
    case LOSE_GROUND: engine.ground=engine.object=false; break;
    case DIE: engine.dying=true; break;
    case POWER_OFF: engine.powered=false; break;
    case PAUSE: GAME.paused=true; break;
    case LEAVE_PLAYING: GAME.phase=4; break;
    }
}
struct { FVector Normal(FVector) { return FVector(0,0,-1); } } MAP;
struct { unsigned GetFrameCount() { return 101; } } EVENT_MANAGER;
struct { int role=CE_WORLD_HOST; } config;
struct { struct { int mType=MOUSE_AXIS_PLUS; } mInput[2]; } CONTROL_BINDINGS[2];
unsigned remote_invert_ground=0,remote_invert_jet=0,remote_walker_any=0;
bool active=true;
int Fail(const char *) { return 0; }
/* ACTUAL_ACTION_ORDER */
/* ACTUAL_VALID_INPUT */
/* ACTUAL_INPUT_QUEUE */;
InputQueue commands;
/* ACTUAL_NATIVE_ELIGIBILITY */
class Adapter { public:
/* ACTUAL_FLUSH */
};

static unsigned checks=0;
static void Check(bool value,const char *message) {
    ++checks; if(!value) { std::fprintf(stderr,"FAIL %s\n",message); std::exit(2); }
}
static CE_WorldInput Packet() {
    CE_WorldInput in={}; in.epoch=7; in.sequence=1; in.tick=101; in.player=1;
    in.mouse_ground_aim=1; in.ground_yaw=.125f; in.ground_pitch=.25f; return in;
}
static void Reset() {
    for(unsigned i=0;i<2;++i) {
        GAME.players[i]=CPlayer(); GAME.players[i].number=i+1;
        GAME.players[i].engine.player=&GAME.players[i]; GAME.controllers[i].owner=&GAME.players[i];
    }
    GAME.players[0].engine.mCurrentOrientation.mYaw=9.f;
    GAME.paused=false; GAME.phase=GAME_STATE_PLAYING; config.role=CE_WORLD_HOST; active=true;
    commands=InputQueue(); mutation=MORPH;
}
static void Queue(CE_WorldInput in) { Check(commands.Push(in,7,100)==1,"actual input queue accepts the selected semantic packet"); }
static bool Unrotated() { return GAME.players[1].engine.mCurrentOrientation.mYaw==0.f&&GAME.players[1].engine.mCurrentOrientation.mPitch==0.f; }
static void HostUnchanged() { Check(GAME.players[0].calls==0&&GAME.players[0].engine.mCurrentOrientation.mYaw==9.f,"canonical P1 is never the remote input owner"); }
int main() {
    Reset(); CE_WorldInput in=Packet(); Queue(in); Adapter::Flush(1);
    Check(GAME.players[1].engine.mCurrentOrientation.mYaw==.125f&&GAME.players[1].engine.mCurrentOrientation.mPitch==.25f,"ordinary grounded P2 aim is preserved");
    Check(commands.consumed==1&&commands.count==0,"ordinary dispatched input is consumed exactly once"); HostUnchanged();
    Adapter::Flush(1); Check(commands.consumed==1&&GAME.players[1].engine.mCurrentOrientation.mYaw==.125f,"missing next input cannot repeat aim");
    Check(commands.Push(in,7,101)==1&&commands.count==0,"consumed duplicate cannot redispatch");

    for(Mutation change : {MORPH,LOSE_GROUND,DIE,POWER_OFF,PAUSE,LEAVE_PLAYING}) {
        Reset(); mutation=change; in=Packet(); in.count=1; in.actions[0]={33,CE_WORLD_ACTION_BINDING,1.f}; Queue(in);
        Check(MouseLookActive(&GAME.players[1].engine),"native ground-aim eligibility holds before dispatch");
        Adapter::Flush(1);
        Check(GAME.players[1].calls==1&&GAME.players[1].last_button==33,"the native receiver boundary receives the admitted morph action once");
        Check(!MouseLookActive(&GAME.players[1].engine),"actual native predicate refuses the post-dispatch state");
        Check(Unrotated(),change==MORPH?"morphing engine cannot receive ground aim after dispatch":"post-dispatch ineligible engine cannot receive ground aim");
        Check(commands.consumed==1&&commands.count==0,"morph action retains consumption ACK even when aim is no longer eligible"); HostUnchanged();
    }

    Reset(); in=Packet(); in.invert_ground=1; Queue(in); Adapter::Flush(1);
    Check(GAME.players[1].engine.mCurrentOrientation.mPitch==-.25f,"ground pitch inversion is applied once"); HostUnchanged();
    Reset(); in=Packet(); in.count=1; in.actions[0]={25,CE_WORLD_ACTION_MOUSE_AXIS,.5f}; Queue(in); Adapter::Flush(1);
    Check(GAME.players[1].calls==0&&!Unrotated()&&commands.consumed==1,"ground mouse axis is not dispatched a second time");
    Reset(); in=Packet(); in.count=1; in.actions[0]={25,CE_WORLD_ACTION_BINDING,.5f}; Queue(in); Adapter::Flush(1);
    Check(GAME.players[1].calls==1&&commands.consumed==1,"a nonmouse binding retains its native dispatch");
    Reset(); in=Packet(); in.mouse_ground_aim=0; in.ground_yaw=in.ground_pitch=0; in.count=1; in.actions[0]={25,0,.5f}; Queue(in); Adapter::Flush(1);
    Check(GAME.players[1].calls==1&&Unrotated()&&commands.consumed==1,"ordinary non-ground input remains native receiver input");

    Reset(); Queue(Packet()); Adapter::Flush(0);
    Check(Unrotated()&&commands.consumed==0&&commands.count==1,"P1 controller flush leaves the remote batch queued"); HostUnchanged();
    Reset(); Queue(Packet()); GAME.paused=true; Adapter::Flush(1);
    Check(Unrotated()&&commands.consumed==0&&commands.count==1,"paused input is retained without dispatch or ACK");
    Reset(); Queue(Packet()); GAME.controllers[1].owner=&GAME.players[0]; Adapter::Flush(1);
    Check(Unrotated()&&commands.consumed==0&&commands.count==1,"wrong controller owner cannot dispatch or ACK input"); HostUnchanged();
    Reset(); config.role=CE_WORLD_REPLICA; Queue(Packet()); Adapter::Flush(1);
    Check(Unrotated()&&commands.consumed==0&&commands.count==1,"replica flush never executes authoritative input");
    Reset(); active=false; Queue(Packet()); Check(Adapter::Flush(1)==0,"offline controller keeps its original caller route");
    Check(Unrotated()&&commands.consumed==0,"offline path does not consume network input");
    std::printf("{\"checks\":%u,\"native_game\":false,\"rendered\":false,\"heard\":false}\n",checks);
}
