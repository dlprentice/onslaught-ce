// SPDX-License-Identifier: GPL-3.0-only
// Game boundary for two-player lockstep co-op: semantic input sampling/application,
// per-tick render-state fences, deterministic state hashing and view/input hooks.
#include "ce_network_world.h"
#include "ce_network_lockstep.h"
#define CANNON_CPP
#include "common.h"
#include "game.h"
#include "world.h"
#include "player.h"
#include "PCController.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "MCBattleEngine.h"
#include "mesh.h"
#include "Camera.h"
#include "map.h"
#include "MCCannon.h"
#undef CANNON_CPP
#include "rtmesh.h"
#include "DestructableSegmentsController.h"
#include "component.h"
#include "mcbuggy.h"
#include "MCTentacle.h"
#include "MCComponent.h"
#include "MCGroundAttack.h"
#include "Motioncontroller.h"
#include <typeinfo>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
extern float gMouseSensitivity;
extern "C" int CE_NetAutomationIdle(void);
extern "C" int CE_NetWindowAutomationIdle(void);
extern "C" int CE_NetUpdateAutomationIdle(void);
extern "C" int CE_NetClockOrdinary(void);
extern "C" int PortCeOnlineMatchActive(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotCount(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotTeam(int slot) __attribute__((weak));
extern "C" CPlayer *PortCeMultiBeGetSlotPlayer(int slot) __attribute__((weak));
extern "C" CBattleEngine *PortCeMultiBeGetSlotEngine(int slot) __attribute__((weak));
extern "C" CCamera *PortCeMultiBeAuxCamera(int slot) __attribute__((weak));
extern "C" void PortCeMultiBeSetAuxCamera(int slot, CCamera *cam) __attribute__((weak));
extern "C" int PortCeMultiBeIsAuxDeathCam(int slot) __attribute__((weak));
extern "C" int PortCeMultiBeWinningTeam(void) __attribute__((weak));

namespace {
const uint32_t native_actions[]={25,27,26,28,29,30,31,32,59,33,19,18,20,16,17,21};
unsigned ActionRank(uint32_t button) {
    for(unsigned i=0;i<sizeof native_actions/sizeof *native_actions;++i) if(native_actions[i]==button) return i+1;
    return 0;
}
bool ActionAllowed(uint32_t button) { return ActionRank(button)!=0; }
bool ValidInput(const CE_WorldInput &in) {
    if (!in.epoch || !in.sequence || !in.tick || in.player > 3u || in.count > CE_WORLD_MAX_ACTIONS ||
        in.invert_ground>1 || in.invert_jet>1 || in.mouse_ground_aim>1 || in.walker_mouse_any>1 || (in.mouse_ground_aim&&in.walker_mouse_any) ||
        !(in.ground_yaw >= -3.142f && in.ground_yaw <= 3.142f) ||
        !(in.ground_pitch >= -3.142f && in.ground_pitch <= 3.142f)) return false;
    if(!in.mouse_ground_aim&&(in.ground_yaw!=0.f||in.ground_pitch!=0.f)) return false;
    unsigned last_rank=0;
    for (unsigned i = 0; i < in.count; ++i) {
        unsigned rank=ActionRank(in.actions[i].button);
        if (!rank||rank<=last_rank||in.actions[i].source>CE_WORLD_ACTION_MOUSE_AXIS||!(in.actions[i].value>=-1.f&&in.actions[i].value<=1.f)) return false;
        last_rank=rank;
    }
    return true;
}
}

class CENetworkWorldAdapter {
public:
    static bool OnlineMultiActive() {
        return PortCeOnlineMatchActive && PortCeOnlineMatchActive() != 0;
    }
    static unsigned OnlineSlotCount() {
        if (!OnlineMultiActive() || !PortCeOnlineMatchSlotCount) return 2u;
        int n = PortCeOnlineMatchSlotCount();
        return (n >= 2 && n <= 4) ? (unsigned)n : 2u;
    }
    static CPlayer *SlotPlayer(uint32_t slot) {
        if (OnlineMultiActive() && PortCeMultiBeGetSlotPlayer) {
            if (CPlayer *p = PortCeMultiBeGetSlotPlayer((int)slot)) return p;
        }
        return (int)slot < GAME.GetNPlayers() ? GAME.GetPlayer((int)slot) : NULL;
    }
    static CBattleEngine *SlotEngine(uint32_t slot) {
        if (OnlineMultiActive() && PortCeMultiBeGetSlotEngine) {
            if (CBattleEngine *e = PortCeMultiBeGetSlotEngine((int)slot)) return e;
        }
        CPlayer *p = SlotPlayer(slot);
        return p ? p->GetBattleEngine() : NULL;
    }
    // Lockstep co-op: this peer's physical controls (controller 0, as in a solo level) become one semantic sample
    // for its own player; nothing is dispatched to gameplay here. Both peers apply every sample at the same tick.
    static void LockstepSample(uint32_t local_player,CE_WorldInput &out) {
        memset(&out,0,sizeof out); lock_sample=&out; lock_mouse_axis=false;
        CController *controller=GAME.GetController(0);
        if(!controller||controller->GetToControl()!=GAME.GetPlayer(0)) { lock_sample=NULL; return; }
        int pad=controller->GetPadNumber();
        out.invert_ground=!!CAREER.GetInvertYAxis(0); out.invert_jet=!!CAREER.GetInvertJetYAxis(0);
        SControlBinding *yaw=FindControlBinding(BUTTON_MECH_YAW_RIGHT);
        out.walker_mouse_any=yaw&&(yaw->PlayerInput(pad)->mType==MOUSE_AXIS_PLUS_ANY||yaw->PlayerInput(pad)->mType==MOUSE_AXIS_MINUS_ANY);
        CBattleEngine *e=SlotEngine(local_player);
        out.mouse_ground_aim=e&&!e->IsDying()&&e->GetIsPoweredUp()&&e->GetState()==BATTLE_ENGINE_STATE_WALKER&&e->IsWalkerOnGroundOrObject()&&yaw&&(yaw->PlayerInput(pad)->mType==MOUSE_AXIS_PLUS||yaw->PlayerInput(pad)->mType==MOUSE_AXIS_MINUS);
        if(out.mouse_ground_aim) {
            out.ground_yaw=-(MOUSE_X-PLATFORM.GetWindowWidth()/2)*(gMouseSensitivity*.002f);
            out.ground_pitch=-(MOUSE_Y-PLATFORM.GetWindowHeight()/2)*(gMouseSensitivity*.001f);
            out.ground_yaw=fmodf(out.ground_yaw,6.2831853f);
            if(out.ground_yaw>3.1415927f) out.ground_yaw-=6.2831853f;
            if(out.ground_yaw< -3.1415927f) out.ground_yaw+=6.2831853f;
            if(out.ground_pitch>3.1415927f) out.ground_pitch=3.1415927f;
            if(out.ground_pitch< -3.1415927f) out.ground_pitch=-3.1415927f;
        }
        BOOL locked=gMouseAxisLocked[pad]; gMouseAxisLocked[pad]=FALSE;
        controller->Flush(); gMouseAxisLocked[pad]=locked; lock_sample=NULL; lock_mouse_axis=false;
        if(out.mouse_ground_aim) { MOUSE_X=PLATFORM.GetWindowWidth()/2; MOUSE_Y=PLATFORM.GetWindowHeight()/2; }
    }
    static void LockstepCapture(int button,float value) {
        if(!lock_sample||!ActionAllowed((uint32_t)button)||lock_sample->count==CE_WORLD_MAX_ACTIONS) return;
        lock_sample->actions[lock_sample->count++]={(uint32_t)button,lock_mouse_axis?CE_WORLD_ACTION_MOUSE_AXIS:CE_WORLD_ACTION_BINDING,value};
    }
    // Apply one sampled tick of controls for either player (or any active 0..3 online slot) using the same receiver, mouse-look and pitch-limit arithmetic.
    static void LockstepApply(uint32_t player,const CE_WorldInput &in) {
        CPlayer *p=NULL; CBattleEngine *e=NULL; CController *ctrl=NULL;
        if (OnlineMultiActive()) {
            if (player >= OnlineSlotCount()) return;
            p = SlotPlayer(player);
            e = SlotEngine(player);
            if (!p || !e || GAME.IsPaused()) return;
            ctrl = ((int)player < GAME.GetNPlayers() && GAME.GetController((int)player))
                ? GAME.GetController((int)player) : GAME.GetController(0);
            if (!ctrl) return;
            if ((int)player < GAME.GetNPlayers() && GAME.GetController((int)player) &&
                GAME.GetController((int)player)->GetToControl() != p) return;
        } else {
            p=GAME.GetPlayer(player); e=p?p->GetBattleEngine():NULL;
            if(!e||GAME.IsPaused()||GAME.GetController(player)->GetToControl()!=p) return;
            ctrl=GAME.GetController(player);
        }
        struct AuxCameraGuard {
            int slot;
            CCamera *aux;
            CCamera *saved;
            AuxCameraGuard(int s, CCamera *c) : slot(-1), aux(NULL), saved(NULL) {
                if (s >= 0 && s < MAX_PLAYERS && c) {
                    slot = s;
                    aux = c;
                    saved = GAME.mCurrentCamera[s];
                    GAME.mCurrentCamera[s] = c;
                }
            }
            ~AuxCameraGuard() {
                if (slot >= 0) {
                    if (GAME.mCurrentCamera[slot] != aux && PortCeMultiBeSetAuxCamera) {
                        PortCeMultiBeSetAuxCamera(slot, GAME.mCurrentCamera[slot]);
                    }
                    GAME.mCurrentCamera[slot] = saved;
                }
            }
        };
        CCamera *auxCam = (OnlineMultiActive() && (int)player >= GAME.GetNPlayers() && PortCeMultiBeAuxCamera)
            ? PortCeMultiBeAuxCamera((int)player) : NULL;
        AuxCameraGuard auxGuard((int)player, auxCam);
        if (auxCam && !GAME.IsPaused() && GAME.GetGameState() == GAME_STATE_PLAYING &&
            !e->IsDying() && e->GetIsPoweredUp()) {
            e->mCurrentUnitOverCrosshair = e->CalcUnitOverCrossHair(NULL, TRUE, TRUE);
        }
        bool ground_mouse=in.mouse_ground_aim&&e->IsWalkerOnGroundOrObject();
        if (player < 4u)
            lock_mouse_ground_aim[player] = (ground_mouse && !GAME.IsPaused() && GAME.GetGameState() == GAME_STATE_PLAYING && !e->IsDying() && e->GetIsPoweredUp()) ? 1u : 0u;
        for(unsigned i=0;i<in.count;++i) {
            uint32_t button=in.actions[i].button;
            if(ground_mouse&&in.actions[i].source==CE_WORLD_ACTION_MOUSE_AXIS&&button>=25&&button<=28) continue;
            p->ReceiveButtonAction(ctrl,button,in.actions[i].value);
        }
        if (ground_mouse&&e->IsWalkerOnGroundOrObject()&&!GAME.IsPaused()&&GAME.GetGameState()==GAME_STATE_PLAYING&&!e->IsDying()&&e->GetIsPoweredUp()) {
            e->mCurrentOrientation.mYaw += in.ground_yaw; e->mCurrentOrientation.mPitch += in.invert_ground?-in.ground_pitch:in.ground_pitch;
            FMatrix ym; ym.MakeRotationYawF(e->mCurrentOrientation.mYaw); FVector yv = ym * FVector(0.f, 1.f, 0.f);
            FVector normal = e->IsOnGround() && !e->IsOnObject() ? MAP.Normal(e->mPos) : FVector(0.f, 0.f, -1.f);
            FVector cross = yv ^ normal; FVector pitch = cross ^ normal; pitch.Normalise(); float ground = -pitch.Z;
            if (e->mCurrentOrientation.mPitch - ground > .6f) e->mCurrentOrientation.mPitch = ground + .6f;
            if (e->mCurrentOrientation.mPitch - ground < -.9f) e->mCurrentOrientation.mPitch = ground - .9f;
            e->mOrientation = FMatrix(e->mCurrentOrientation.mYaw + cosf(e->mShakeR) * e->mYawShake,
                e->mCurrentOrientation.mPitch + cosf(e->mShakeR) * e->mPitchShake, e->mCurrentOrientation.mRoll + cosf(e->mShakeR) * e->mRollShake);
            e->mOldOrientation = e->mOrientation;
        }
    }
    static void LockstepRandom(bool save,int &value,int &seed) {
        CSeedUniformRandomNumberStream *rng=GAME.mRandomStream; if(!rng) { if(save) value=seed=0; return; }
        if(save) { value=rng->mValue; seed=rng->mSeed; } else { rng->mValue=value; rng->mSeed=seed; }
    }
    // Selected script gates and weapon choices, encoded without process pointers. Present weapons need nonempty names
    // shorter than 256 bytes, and list traversal must match a size of at most 64. Missing engines, parts and optional
    // weapon slots are recorded as absent; this does not validate the expected inventory topology.
    static bool LockstepPlayerGates(uint64_t &out) {
        uint64_t hash=1469598103934665603ull;
        auto bytes=[&](const void *p,size_t n) { const uint8_t *b=(const uint8_t *)p; for(size_t i=0;i<n;++i) { hash^=b[i]; hash*=1099511628211ull; } };
        auto word=[&](uint32_t value) { uint8_t b[4]={(uint8_t)value,(uint8_t)(value>>8),(uint8_t)(value>>16),(uint8_t)(value>>24)}; bytes(b,4); };
        auto weapon=[&](CWeapon *w) -> bool {
            word(w?1u:0u); if(!w) return true;
            if(!w->mData||!w->mData->mName) return false;
            const char *name=w->mData->mName; size_t n=0; while(n<256&&name[n]) ++n;
            if(!n||n==256) return false;
            word((uint32_t)n); bytes(name,n); word((uint32_t)w->mActive); return true;
        };
        auto list=[&](SPtrSet<CWeapon> &weapons) -> bool {
            int size=weapons.Size(); if(size<0||size>64) return false;
            word((uint32_t)size); int seen=0; ListIterator<CWeapon> it(&weapons);
            for(CWeapon *w=it.First();w;w=it.Next()) { if(!weapon(w)||++seen>size) return false; }
            return seen==size;
        };
        if(GAME.GetNPlayers()!=2) return false;
        CBattleEngine *first_engine=NULL;
        for(unsigned i=0;i<2;++i) {
            CPlayer *p=GAME.GetPlayer(i); if(!p||p->GetNumber()!=(int)i+1) return false;
            CBattleEngine *e=p->GetBattleEngine();
            if(e && (e==first_engine || e->GetPlayer()!=p)) return false;
            if(i==0) first_engine=e;
            word(i); word(e?1u:0u); if(!e) continue; // an absent engine is a valid death/respawn state
            word((uint32_t)e->mFlightModeActive); word((uint32_t)e->mPoweredUp); word((uint32_t)e->mActive);
            // GetCurrentWeapon chooses the part from mState, then the walker may choose augmentation at index zero.
            // Read the stored indices directly: calling GetCurrentWeapon can mutate an out-of-range walker index.
            word((uint32_t)e->mState); word((uint32_t)e->mAugActive);
            word(e->mWalkerPart?1u:0u);
            if(e->mWalkerPart) {
                word((uint32_t)e->mWalkerPart->mCurrentWeapon);
                if(!weapon(e->mWalkerPart->mPrimaryWeapon)||!weapon(e->mWalkerPart->mAugWeapon)||!list(e->mWalkerPart->mWeapons)) return false;
            }
            word(e->mJetPart?1u:0u);
            if(e->mJetPart) {
                word((uint32_t)e->mJetPart->mCurrentWeapon);
                if(!list(e->mJetPart->mWeapons)) return false;
            }
            if(!list(e->mWeapons)) return false;
        }
        out=hash; return true;
    }
    // Online 2-4 player multi-BE gates: hashes ALL active Battle Engine slots 0..slot_count-1 (human AND AI).
    static bool LockstepOnlinePlayerGates(uint64_t &out, unsigned slot_count) {
        if (slot_count < 2u || slot_count > 4u) return false;
        uint64_t hash=1469598103934665603ull;
        auto bytes=[&](const void *p,size_t n) { const uint8_t *b=(const uint8_t *)p; for(size_t i=0;i<n;++i) { hash^=b[i]; hash*=1099511628211ull; } };
        auto word=[&](uint32_t value) { uint8_t b[4]={(uint8_t)value,(uint8_t)(value>>8),(uint8_t)(value>>16),(uint8_t)(value>>24)}; bytes(b,4); };
        auto weapon=[&](CWeapon *w) -> bool {
            word(w?1u:0u); if(!w) return true;
            if(!w->mData||!w->mData->mName) return false;
            const char *name=w->mData->mName; size_t n=0; while(n<256&&name[n]) ++n;
            if(!n||n==256) return false;
            word((uint32_t)n); bytes(name,n); word((uint32_t)w->mActive); return true;
        };
        auto list=[&](SPtrSet<CWeapon> &weapons) -> bool {
            int size=weapons.Size(); if(size<0||size>64) return false;
            word((uint32_t)size); int seen=0; ListIterator<CWeapon> it(&weapons);
            for(CWeapon *w=it.First();w;w=it.Next()) { if(!weapon(w)||++seen>size) return false; }
            return seen==size;
        };
        const unsigned core_players = (unsigned)(GAME.GetNPlayers() > 0 ? GAME.GetNPlayers() : 0);
        if (core_players < 2u) return false;
        word(slot_count);
        CBattleEngine *seen_engines[4] = {};
        for(unsigned i=0;i<slot_count;++i) {
            CPlayer *p=SlotPlayer(i);
            if(!p) {
                if(i < core_players) return false;
                word(i); word(0u); word(0u); continue;
            }
            if(p->GetNumber()!=(int)i+1) return false;
            CBattleEngine *e=SlotEngine(i);
            if(e) {
                if(e->GetPlayer()!=p) return false;
                for(unsigned prev=0;prev<i;++prev) if(seen_engines[prev]==e) return false;
            }
            seen_engines[i]=e;
            word(i); word(1u); word(e?1u:0u); if(!e) continue;
            word((uint32_t)e->mAllegiance);
            word((uint32_t)e->mFlightModeActive); word((uint32_t)e->mPoweredUp); word((uint32_t)e->mActive);
            word((uint32_t)e->mState); word((uint32_t)e->mAugActive);
            word(e->mWalkerPart?1u:0u);
            if(e->mWalkerPart) {
                word((uint32_t)e->mWalkerPart->mCurrentWeapon);
                if(!weapon(e->mWalkerPart->mPrimaryWeapon)||!weapon(e->mWalkerPart->mAugWeapon)||!list(e->mWalkerPart->mWeapons)) return false;
            }
            word(e->mJetPart?1u:0u);
            if(e->mJetPart) {
                word((uint32_t)e->mJetPart->mCurrentWeapon);
                if(!list(e->mJetPart->mWeapons)) return false;
            }
            if(!list(e->mWeapons)) return false;
        }
        out=hash; return true;
    }
    // Each thing's selected fields, in world-list order, in a nonstandard-offset FNV-1a-style digest; collisions are possible.
    static uint64_t LockstepThings(FILE *dump,uint32_t &count) {
        uint64_t all=1469598103934665603ull; count=0;
        auto mix=[](uint64_t h,const void *p,size_t n) { const uint8_t *b=(const uint8_t *)p; for(size_t i=0;i<n;++i) { h^=b[i]; h*=1099511628211ull; } return h; };
        auto f32=[&](uint64_t h,float v) { if(v==0.f) v=0.f; return mix(h,&v,4); };
        ListIterator<CThing> it(&WORLD.GetThingNB());
        for(CThing *t=it.First();t;t=it.Next()) {
            ++count; const char *name=t->_GetClassName(); uint64_t one=1469598103934665603ull;
            one=mix(one,name,strlen(name)+1); one=f32(one,t->mPos.X); one=f32(one,t->mPos.Y); one=f32(one,t->mPos.Z);
            CUnit *unit=dynamic_cast<CUnit *>(t); float life=unit?unit->mLife:0.f;
            if(unit) one=f32(one,life);
            // orientation and (actors) velocity: drift there precedes any position difference
            uint64_t motion=1469598103934665603ull;
            auto xyz=[&](uint64_t h,const FVector &v) { h=f32(h,v.X); h=f32(h,v.Y); return f32(h,v.Z); };	// never the unused pad
            if(CComplexThing *ct=dynamic_cast<CComplexThing *>(t)) for(int r=0;r<3;++r) motion=xyz(motion,ct->mOrientation.Row[r]);
            if(CActor *a=dynamic_cast<CActor *>(t)) motion=xyz(motion,a->mVelocity);
            one=mix(one,&motion,8);
            all=mix(all,&one,8);
            if(dump) fprintf(dump,"%s %.9g %.9g %.9g %.9g %016llx %016llx\n",name,t->mPos.X,t->mPos.Y,t->mPos.Z,life,(unsigned long long)one,(unsigned long long)motion);
        }
        return all;
    }
    // What a peer's own frames write into simulation state, kept out of the shared simulation. Rendering writes a
    // component's position and orientation from its parent's interpolated emitter (CComponent::GetRenderPos), and
    // pose and transform caches are keyed by the render frame number, whose count differs between peers. At the end
    // of a tick the components' state is saved; at the start of the next it is put back, the render fraction is
    // exactly 1 (its wall-clock rounding is not), and the frame number moves on by more than CMeshPose's 30-frame
    // reuse window, so every cache the tick reads is rebuilt from simulation state alone.
    struct ComponentState { CComponent *thing; FVector pos; FMatrix orientation; SINT stamp; };
    static ComponentState *lock_components; static unsigned lock_component_count, lock_component_cap;
    // Motion controllers keep pose state that drawing advances: a mech's bones and legs step when anything asked for
    // its transforms since the last tick (CMCMech::mUsed, and GetPartTransform's forced ProcessMovement), its body lean
    // smooths per evaluation (UpdateBone's mUnknown88), wheeled and tentacle controllers update on the render clock,
    // turret controllers note the yaw and pitch they last drew. Projectile launch points, targetable positions and mesh
    // collision read that state, so each tick saves every controller's bytes and arrays and the next restores them.
    struct Region { void *at; uint32_t size; };
    static Region *lock_regions; static uint8_t *lock_bytes; static size_t lock_region_count, lock_region_cap, lock_byte_count, lock_byte_cap;
    static bool lock_overflow;
    static void Keep(void *at,size_t size) {
        if(!at||!size) return;
        if(lock_region_count==lock_region_cap) {
            size_t cap=lock_region_cap?lock_region_cap*2:1024; Region *r=(Region *)realloc(lock_regions,cap*sizeof *r);
            if(!r) { lock_overflow=true; return; } lock_regions=r; lock_region_cap=cap;
        }
        if(lock_byte_count+size>lock_byte_cap) {
            size_t cap=lock_byte_cap?lock_byte_cap:1<<20; while(cap<lock_byte_count+size) cap*=2;
            uint8_t *b=(uint8_t *)realloc(lock_bytes,cap); if(!b) { lock_overflow=true; return; } lock_bytes=b; lock_byte_cap=cap;
        }
        lock_regions[lock_region_count++]={at,(uint32_t)size}; memcpy(lock_bytes+lock_byte_count,at,size); lock_byte_count+=size;
    }
    static int ThingParts(CThing *t) {
        CRenderThing *rt=t->GetRenderThing(); CMesh *mesh=rt?rt->GetRTMesh():NULL; return mesh?mesh->mNumParts:0;
    }
    static void KeepController(CThing *t,CMotionController *mc) {
        if(CMCMech *m=dynamic_cast<CMCMech *>(mc)) {
            Keep(m,sizeof(CMCMech));
            int legs=m->mNumLegs>0?m->mNumLegs:0, parts=m->mMesh?m->mMesh->mNumParts:0;
            if(m->mLegPos) { Keep(m->mLegPos,legs*sizeof(FVector)); Keep(m->mLegA,legs*sizeof(BOOL)); Keep(m->mLegB,legs*sizeof(float));
                Keep(m->mLegC,legs*sizeof(SINT)); Keep(m->mLegTarget,legs*sizeof(FVector)); }
            if(m->mToeFrames) Keep(m->mToeFrames,legs*(m->mNumToes>0?m->mNumToes:0)*sizeof(SINT));
            if(m->mPartPos) { Keep(m->mPartOri,parts*sizeof(FMatrix)); Keep(m->mPartPos,parts*sizeof(FVector)); Keep(m->mPartFrame,parts*sizeof(float));
                Keep(m->mOldPartOri,parts*sizeof(FMatrix)); Keep(m->mOldPartPos,parts*sizeof(FVector)); Keep(m->mOldPartFrame,parts*sizeof(float)); }
        } else if(CMCBuggy *b=dynamic_cast<CMCBuggy *>(mc)) {
            Keep(b,sizeof(CMCBuggy)); int wheels=b->mNumWheels>0?b->mNumWheels:0, parts=ThingParts(t);
            if(b->mWheelPos) { Keep(b->mWheelPos,wheels*sizeof(FVector)); Keep(b->mWheelSpin,wheels*sizeof(float)); }
            if(b->mPartPos) { Keep(b->mPartOri,parts*sizeof(FMatrix)); Keep(b->mPartPos,parts*sizeof(FVector)); Keep(b->mPartFrame,parts*sizeof(float));
                Keep(b->mOldPartOri,parts*sizeof(FMatrix)); Keep(b->mOldPartPos,parts*sizeof(FVector)); Keep(b->mOldPartFrame,parts*sizeof(float)); }
        } else if(CMCTentacle *c=dynamic_cast<CMCTentacle *>(mc)) {
            Keep(c,sizeof(CMCTentacle)); int parts=ThingParts(t);
            if(c->mPartPos) { Keep(c->mPartOri,parts*sizeof(FMatrix)); Keep(c->mPartPos,parts*sizeof(FVector)); Keep(c->mPartFrame,parts*sizeof(float));
                Keep(c->mOldPartOri,parts*sizeof(FMatrix)); Keep(c->mOldPartPos,parts*sizeof(FVector)); Keep(c->mOldPartFrame,parts*sizeof(float)); }
        }
        else if(CMCGroundVehicle *v=dynamic_cast<CMCGroundVehicle *>(mc)) Keep(v,sizeof(CMCGroundVehicle));
        else if(CMCCannon *k=dynamic_cast<CMCCannon *>(mc)) Keep(k,sizeof(CMCCannon));
        else if(CMCComponent *o=dynamic_cast<CMCComponent *>(mc)) Keep(o,sizeof(CMCComponent));
        else if(CMCSentinel *n=dynamic_cast<CMCSentinel *>(mc)) Keep(n,sizeof(CMCSentinel));
        else if(CMCGroundAttack *g=dynamic_cast<CMCGroundAttack *>(mc)) Keep(g,sizeof(CMCGroundAttack));
        else if(CMCDropship *d=dynamic_cast<CMCDropship *>(mc)) Keep(d,sizeof(CMCDropship));
        else if(CDestructableSegmentsMotionController *x=dynamic_cast<CDestructableSegmentsMotionController *>(mc)) {
            if(x->mSegments) Keep(&x->mSegments->mChanged,sizeof x->mSegments->mChanged);
        } else {
            static const char *seen[32]; static unsigned seen_count; const char *name=typeid(*mc).name(); unsigned i=0;
            while(i<seen_count&&strcmp(seen[i],name)) ++i;
            if(i==seen_count&&seen_count<32) { seen[seen_count++]=name; fprintf(stderr,"onslaught-ce: co-op: unfenced motion controller kind %s\n",name); }
        }
    }
    static int LockstepEndTick() {
        lock_region_count=lock_byte_count=0; lock_overflow=false;
        {
            ListIterator<CThing> it(&WORLD.GetThingNB());
            for(CThing *t=it.First();t;t=it.Next()) if(CMotionController *mc=t->GetMotionController()) KeepController(t,mc);
        }
        if(lock_overflow) return 0;
        lock_component_count=0;
        ListIterator<CThing> it(&WORLD.GetThingNB());
        for(CThing *t=it.First();t;t=it.Next()) {
            CComponent *c=dynamic_cast<CComponent *>(t); if(!c) continue;
            if(lock_component_count==lock_component_cap) {
                unsigned cap=lock_component_cap?lock_component_cap*2u:256u;
                ComponentState *next=(ComponentState *)realloc(lock_components,cap*sizeof *next);
                if(!next) return 0;
                lock_components=next; lock_component_cap=cap;
            }
            lock_components[lock_component_count++]={c,c->mPos,c->mOrientation,c->mUnknown278};
        }
        // The fence's size as play goes on: a line each time the peak grows by 16 (and the first).
        static unsigned peak, next_report;
        if(lock_component_count>peak) {
            peak=lock_component_count;
            if(peak>=next_report) { fprintf(stderr,"onslaught-ce: co-op tick fence: %u components (peak; cap %u), %u bytes of controller state\n",peak,lock_component_cap,(unsigned)lock_byte_count); next_report=peak+16; }
        }
        return 1;
    }
    static void LockstepBeginTick() {
        memset(lock_mouse_ground_aim, 0, sizeof(lock_mouse_ground_aim));
        size_t at=0;
        for(size_t i=0;i<lock_region_count;++i) { memcpy(lock_regions[i].at,lock_bytes+at,lock_regions[i].size); at+=lock_regions[i].size; }
#ifndef CE_LOCKSTEP_FENCE_CONTROL	// build.py --lockstep-fence-control: the negative control leaves what drawing refreshed in place
        for(unsigned i=0;i<lock_component_count;++i) {
            ComponentState &c=lock_components[i]; c.thing->mPos=c.pos; c.thing->mOrientation=c.orientation; c.thing->mUnknown278=c.stamp;
        }
#endif
        GAME.mFraction=GAME.mFraction2=1.f;
        GAME.mRenderFrameNumber+=31;
    }
    // Lockstep co-op in a solo level: the second player starts 12 units to the first player's right, facing the same way,
    // on the same side, on the ground as CStart::Init settles it.
    static void PartnerStart(CStartInitThing *it) {
        CPlayer *first=GAME.GetPlayer(0); CBattleEngine *e=first?first->GetBattleEngine():NULL;
        if(!e) return;
        float yaw=e->GetCurrentOrientation().mYaw;
        FMatrix ym; ym.MakeRotationYawF(yaw); FVector side=ym*FVector(1.f,0.f,0.f);
        it->mPos=e->mPos+side*12.f; it->mYaw=yaw; it->mAllegiance=(EAllegiance)e->mAllegiance;
    }
    // Deferred muzzle-effect placements, for weapons still on a player's engine (a weapon can go with its engine mid-tick).
    static void PlaceEmitters(void *const *weapons,const int *orientate,unsigned count) {
        const unsigned slots = OnlineMultiActive() ? OnlineSlotCount() : 2u;
        for(unsigned p=0;p<slots;++p) {
            CBattleEngine *e=OnlineMultiActive() ? SlotEngine(p) : (GAME.GetPlayer((int)p)?GAME.GetPlayer((int)p)->GetBattleEngine():NULL);
            if(!e) continue;
            ListIterator<CWeapon> it(&e->mWeapons);
            for(CWeapon *w=it.First();w;w=it.Next())
                for(unsigned i=0;i<count;++i) if(weapons[i]==w) w->MoveEmitter(orientate[i]);
        }
    }
    static CE_WorldInput *lock_sample; static bool lock_mouse_axis;
    static uint8_t lock_mouse_ground_aim[4];
};
CENetworkWorldAdapter::Region *CENetworkWorldAdapter::lock_regions; uint8_t *CENetworkWorldAdapter::lock_bytes;
size_t CENetworkWorldAdapter::lock_region_count, CENetworkWorldAdapter::lock_region_cap, CENetworkWorldAdapter::lock_byte_count, CENetworkWorldAdapter::lock_byte_cap;
bool CENetworkWorldAdapter::lock_overflow;
CENetworkWorldAdapter::ComponentState *CENetworkWorldAdapter::lock_components; unsigned CENetworkWorldAdapter::lock_component_count, CENetworkWorldAdapter::lock_component_cap;
CE_WorldInput *CENetworkWorldAdapter::lock_sample; bool CENetworkWorldAdapter::lock_mouse_axis;
uint8_t CENetworkWorldAdapter::lock_mouse_ground_aim[4];

extern "C" {
// Clock, input, pacing and data owners' answers checked by admitted lockstep co-op starts (ce_network_lockstep.cpp).
// NULL when all are ordinary, else a fixed local reason. A test-automation build counts only authored window input
// as idle (CE_NetWindowAutomationIdle); the rest refuse in every build.
const char *CE_NetAutomationRefusal(void) {
    const int clock = CE_NetClockOrdinary();
    if (clock < 0) return "native clock configuration is not initialized";
    if (clock != 1) return "networking requires the ordinary native clock";
    const int automation = CE_NetAutomationIdle();
    const int window = CE_NetWindowAutomationIdle();
    const int update = CE_NetUpdateAutomationIdle();
    if (automation < 0 || window < 0 || update < 0)
        return "native automation configuration is not initialized";
    if (automation != 1 || window != 1 || update != 1)
        return "native input, pacing or data automation is configured or was used";
    return NULL;
}
int CE_WorldActive() { return CE_LockstepActive(); }
int CE_WorldCanRender() { return 1; }
uint32_t CE_WorldLocalPlayer() { return CE_LockstepActive() ? CE_LockstepLocalPlayer() : 0; }
int CE_WorldControllerFlush(uint32_t player) { return CE_LockstepTicking() ? CE_LockstepControllerFlush(player) : 0; }
int CE_WorldLockstepValidInput(const CE_WorldInput *in) {
    if (!in || in->count > CE_WORLD_MAX_ACTIONS) return 0;
    // Lockstep carries neither an epoch/sequence nor a fixed player field: validate its controls using the
    // shared semantic rule without pretending those transport fields came from the packet.
    CE_WorldInput candidate=*in; candidate.epoch=1; candidate.sequence=1; candidate.tick=1; candidate.player=1;
    // A centered mouse axis may be followed by CE's gamepad default for the same binding in one native flush.
    // Validate that exact pair as one action, but keep both in the original sample so the game applies them in order.
    unsigned kept=0;
    for (unsigned i=0; i<in->count; ++i) {
        const CE_WorldAction &a=in->actions[i];
        if (i+1<in->count && a.source==CE_WORLD_ACTION_MOUSE_AXIS && a.value==0.f &&
            in->actions[i+1].button==a.button && in->actions[i+1].source==CE_WORLD_ACTION_BINDING) continue;
        candidate.actions[kept++]=a;
    }
    candidate.count=kept;
    return ValidInput(candidate) ? 1 : 0;
}
void CE_WorldLockstepSample(uint32_t local_player, CE_WorldInput *out) { CENetworkWorldAdapter::LockstepSample(local_player,*out); }
void CE_WorldLockstepApply(uint32_t player, const CE_WorldInput *in) { CENetworkWorldAdapter::LockstepApply(player,*in); }
void CE_WorldLockstepPartnerStart(void *init) { CENetworkWorldAdapter::PartnerStart((CStartInitThing *)init); }
// The tutorial's solo script gates player 1. In two-player co-op, mirror those gates only for that
// exact engine; callers remain the native script operations. Other things, levels and ordinary play keep their behavior.
void *CE_LockstepTutorialPartner(void *thing) {
    if (!thing || !CE_LockstepCoopPlayers() || GAME.GetNPlayers() != 2) return NULL;
    CPlayer *first = GAME.GetPlayer(0), *second = GAME.GetPlayer(1);
    CBattleEngine *host = first ? first->GetBattleEngine() : NULL;
    CBattleEngine *guest = second ? second->GetBattleEngine() : NULL;
    if (!host || !guest || host == guest || static_cast<void *>(static_cast<CThing *>(host)) != thing) return NULL;
    return static_cast<CThing *>(guest);
}
void CE_WorldLockstepPlaceEmitters(void *const *weapons, const int *orientate, unsigned count) { CENetworkWorldAdapter::PlaceEmitters(weapons,orientate,count); }
void CE_WorldLockstepRandom(int save, int *value, int *seed) { CENetworkWorldAdapter::LockstepRandom(save!=0,*value,*seed); }
uint64_t CE_WorldLockstepThings(FILE *dump, uint32_t *count) { return CENetworkWorldAdapter::LockstepThings(dump,*count); }
int CE_WorldLockstepGates(uint64_t *out) {
    if (!out) return 0;
    if (CENetworkWorldAdapter::OnlineMultiActive())
        return CENetworkWorldAdapter::LockstepOnlinePlayerGates(*out, CENetworkWorldAdapter::OnlineSlotCount());
    return CENetworkWorldAdapter::LockstepPlayerGates(*out);
}
int CE_WorldLockstepEndTick(void) { return CENetworkWorldAdapter::LockstepEndTick(); }
void CE_WorldLockstepBeginTick(void) { CENetworkWorldAdapter::LockstepBeginTick(); }
// Lockstep co-op: a peer shows its own player, except while a mission script's pan camera holds camera 0 (scripts
// set their cutscene cameras there): then both peers show it.
int CE_WorldViewPlayer(int original) {
    if(CE_LockstepActive()) {
        const uint32_t local = CE_LockstepLocalPlayer();
        if (CENetworkWorldAdapter::OnlineMultiActive()) {
            if (local != 0 && dynamic_cast<CPanCamera *>(GAME.GetCamera(0))) return 0;
            return (int)local;
        }
        return local==1&&dynamic_cast<CPanCamera *>(GAME.GetCamera(0)) ? 0 : (int)local;
    }
    return original;
}
int CE_WorldLocalWon(int game_state) {
    if (game_state == GAME_STATE_LEVEL_WON) return 1;
    if (game_state != GAME_STATE_PLAYER_1_WON && game_state != GAME_STATE_PLAYER_2_WON) return 0;
    if (CENetworkWorldAdapter::OnlineMultiActive() && PortCeOnlineMatchSlotTeam) {
        const int local = (int)CE_LockstepLocalPlayer();
        const int localTeam = PortCeOnlineMatchSlotTeam(local);
        const int winTeam = PortCeMultiBeWinningTeam ? PortCeMultiBeWinningTeam() : -1;
        if (winTeam >= 0) return (localTeam == winTeam) ? 1 : 0;
        const int t0 = PortCeOnlineMatchSlotTeam(0);
        const int t1 = PortCeOnlineMatchSlotTeam(1);
        if (game_state == GAME_STATE_PLAYER_1_WON) return (localTeam == t0) ? 1 : 0;
        return (t1 != t0) ? ((localTeam == t1) ? 1 : 0) : ((localTeam != t0) ? 1 : 0);
    }
    return game_state == (CE_WorldLocalPlayer() == 0 ? GAME_STATE_PLAYER_1_WON : GAME_STATE_PLAYER_2_WON) ? 1 : 0;
}
float CE_WorldViewAspect(float original) { return CE_WorldActive() ? .75f : original; }
int CE_WorldInvertPitch(uint32_t player,int grounded,int original) { return CE_LockstepTicking() ? CE_LockstepInvertPitch(player,grounded,original) : original; }
int CE_WorldWalkerBindingType(uint32_t player,int original) { return CE_LockstepTicking() ? (CE_LockstepWalkerAny(player,original) ? MOUSE_AXIS_PLUS_ANY : MOUSE_AXIS_PLUS) : original; }
int CE_WorldMouseGroundAim(uint32_t player,int original) {
    if (!CE_LockstepTicking()) return original;
    return (player < 4u && CENetworkWorldAdapter::lock_mouse_ground_aim[player]) ? 1 : 0;
}
void CE_WorldMouseAxis(int on) { if(CENetworkWorldAdapter::lock_sample) CENetworkWorldAdapter::lock_mouse_axis=on!=0; }
}
CCamera *CE_WorldViewCamera(int original) {
    if (!CE_LockstepActive()) return GAME.GetCamera(original);
    const int view = CE_WorldViewPlayer(original);
    if (CENetworkWorldAdapter::OnlineMultiActive() && view >= GAME.GetNPlayers()) {
        if (CCamera *aux = PortCeMultiBeAuxCamera ? PortCeMultiBeAuxCamera(view) : NULL)
            return aux;
        return GAME.GetCamera(0);
    }
    if (view >= 0 && view < GAME.GetNPlayers()) {
        if (CCamera *cam = GAME.GetCamera(view)) return cam;
    }
    return GAME.GetCamera(0);
}
CPlayer *CE_WorldViewPlayerPtr(int original) {
    if (!CE_LockstepActive()) return GAME.GetPlayer(original);
    const int view = CE_WorldViewPlayer(original);
    if (CENetworkWorldAdapter::OnlineMultiActive() && view >= GAME.GetNPlayers()) {
        if (CPlayer *aux = PortCeMultiBeGetSlotPlayer ? PortCeMultiBeGetSlotPlayer(view) : NULL)
            return aux;
        return GAME.GetPlayer(0);
    }
    if (view >= 0 && view < GAME.GetNPlayers()) {
        if (CPlayer *p = GAME.GetPlayer(view)) return p;
    }
    return GAME.GetPlayer(0);
}
int CE_WorldViewShowHUD(int original, CPlayer *player) {
    if (!player) return 0;
    if (!CE_WorldActive()) {
        CCamera *cam = GAME.GetCamera(original);
        return (cam && cam->GetShowHUD()) ? 1 : 0;
    }
    const int hud_player = CE_WorldViewPlayer(original);
    if (hud_player < 0 || hud_player >= 4) return 0;
    if (CENetworkWorldAdapter::OnlineMultiActive() && hud_player >= GAME.GetNPlayers()) {
        if (PortCeMultiBeIsAuxDeathCam && PortCeMultiBeIsAuxDeathCam(hud_player)) return 0;
        CBattleEngine *e = CENetworkWorldAdapter::SlotEngine((uint32_t)hud_player);
        if (!e || e->IsDying()) return 0;
        CCamera *aux = PortCeMultiBeAuxCamera ? PortCeMultiBeAuxCamera(hud_player) : NULL;
        if (!aux || !aux->GetShowHUD()) return 0;
        return (player->GetNumber() == hud_player + 1) ? 1 : 0;
    }
    if (player->GetNumber() != hud_player + 1) return 0;
    CCamera *cam = CE_WorldViewCamera(original);
    return (cam && cam->GetShowHUD()) ? 1 : 0;
}
int CE_WorldBypassMouse(CBattleEngine *) {
    return CE_LockstepTicking() ? 1 : 0; // mouse look reaches either engine only through its player's tick sample
}
int CE_WorldCaptureAction(CController *,int button,float value) {
    if(!CE_LockstepTicking()) return 0;
    CENetworkWorldAdapter::LockstepCapture(button,value);
    return 1; // never dispatched natively during a lockstep tick
}
void CE_WorldLifecycleDestroyed(const void *) {}
