// SPDX-License-Identifier: GPL-3.0-only
// The first authoritative game adapter. It serializes named scalar fields, never object memory or pointers.
// Unsupported classes/controllers stop the session instead of silently producing an incomplete world.
#include "ce_network_world.h"
#include "ce_network_lockstep.h"
#include "../network/network.h"
#define EXPLOSION_CPP
#define CANNON_CPP
#include "common.h"
#define RADARWARNINGRECEIVER_CPP
#include "RadarWarningReceiver.h"
#undef RADARWARNINGRECEIVER_CPP
#include "game.h"
#include "world.h"
#include "player.h"
#include "PCController.h"
#include "eventmanager.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "MCBattleEngine.h"
#include "round.h"
#include "explosion.h"
#undef EXPLOSION_CPP
#include "mesh.h"
#include "Camera.h"
#include "Cockpit.h"
#include "BattleEngineDataManager.h"
#include "map.h"
#include "SoundManager.h"
#include "MCCannon.h"
#include "Guide.h"
#define FEATURE_CPP
#include "feature.h"
#undef FEATURE_CPP
#undef CANNON_CPP
#include "rtmesh.h"
#include "meshpose.h"
#include "RTBuilding.h"
#include "DestructableSegmentsController.h"
#include "DestroyableSegment.h"
#include "component.h"
#include "Camera.h"
#include "mcbuggy.h"
#include "MCTentacle.h"
#include "MCComponent.h"
#include "MCGroundAttack.h"
#include "Motioncontroller.h"
#include <typeinfo>
#include "../platform/common/typeinfo_bridge.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
extern float gMouseSensitivity;

namespace {
const uint32_t kMagic = 0x43455731u; // CEW1
const uint32_t kInputMagic = 0x43454931u; // CEI1
const unsigned kEntities = 128, kParts = 96, kWeapons = 16, kLocks = 16, kInputs = 16, kThreats = 32, kFlashes = 32, kName = 96, kBirths = 64;
// Temporary foreground engineering admission, not an Internet effect-aging policy.
const uint32_t kNewBirthAge = 2;
enum Kind { STATIC_THING = 1, ENGINE_THING = 2, ROUND_THING = 3, CANNON_THING = 4, FEATURE_THING = 5, BUILDING_THING = 6, EXPLOSION_THING = 7 };
struct Vec { float v[3]; };
struct Mat { float v[9]; };
struct Animation { uint32_t present, mode, index, looped; float frame, old_frame, increment; };
struct Core {
    uint32_t id, kind, initial_number, player, type, flags;
    char class_name[kName], mesh[kName];
    Vec pos, old_pos, velocity;
    Mat orientation, old_orientation;
    float last_move, ground, water, object;
    Animation animation;
};
struct Pose { Vec pos, old_pos; Mat orientation, old_orientation; float frame, old_frame; };
struct WeaponState {
    char name[kName], mode[kName];
    uint32_t emitter, mount, target, active;
    float charge, next_fire;
    int32_t burst, charged, launch, angle;
};
struct LockState { uint32_t target, locked, direct; float start, finish; };
struct ThreatState { uint32_t round, seen; float yaw, distance, time; };
struct FlashState { float yaw, start; };
struct CameraState { Vec pos, old_pos; Mat orientation, old_orientation; float zoom, old_zoom; uint32_t hud; };
// Experimental engineering cohort: host-produced render inputs on the genuine engine-owned cockpit.
struct CockpitState {
    char mesh[kName]; uint64_t schema;
    uint32_t should_render,mode,index;
    Vec shake,old_shake; Mat orientation,old_orientation;
    float frame,old_frame;
};
#define ENGINE_FLOATS(X) \
    X(mYawvel) X(mPitchvel) X(mRollvel) X(mZoom) X(mOldZoom) X(mDesiredZoom) \
    X(mLastDamageTime) X(mOldLife) X(mOldEnergy) X(mLowArmourStartTime) X(mLowEnergyStartTime) \
    X(mDangerStartTime) X(mZoomOutTime) X(mTakeOffTime) X(mAugValue) X(mAugActiveTime) \
    X(mStallTime) X(mAugmentedTime) X(mYawShake) X(mPitchShake) X(mRollShake) X(mShakeR) \
    X(mDesiredAutoAimYawOffset) X(mAutoAimYawOffset) X(mOldAutoAimYawOffset) \
    X(mDesiredAutoAimPitchOffset) X(mAutoAimPitchOffset) X(mOldAutoAimPitchOffset) \
    X(mTransformStartTime) X(mChangedWeaponTime) X(mStealth) X(mDesiredStealth) \
    X(mAmmoDepletedTime) X(mWeaponOverheatedTime)
#define ENGINE_INTS(X) \
    X(mState) X(mAugActive) X(mStalling) X(mCloaked) X(mTrackingActive) X(mVisible) X(mPoweredUp) \
    X(mSlowMovement) X(mFlightModeActive) X(mEngineState) X(mOldEngineState) X(mThrustersOn)
struct EngineState {
    float life, energy, shields, max_shields;
    int32_t hit,hit_decay;
    Vec euler, old_euler;
    uint32_t configuration, allegiance, crosshair, crosshair_range, auto_aim, standing_on;
#define DECL_FLOAT(name) float name;
    ENGINE_FLOATS(DECL_FLOAT)
#undef DECL_FLOAT
#define DECL_INT(name) uint32_t name;
    ENGINE_INTS(DECL_INT)
#undef DECL_INT
    float stores[6]; uint32_t store_heat[6], store_overheat[6];
    uint32_t walker_weapon, jet_weapon, weapon_count, lock_count, fired_count, part_count;
    float walk_cycle, old_walk_cycle, jet_thrust;
    WeaponState weapons[kWeapons]; LockState locks[kLocks], fired[kLocks]; Pose parts[kParts];
    uint32_t view, preferred_view, stats[PS_NUM_PLAYERSTATS];
    CameraState camera; CockpitState cockpit;
    float tracking[4];
    uint32_t radar_present, new_threat, threat_count, flash_count;
    float new_threat_time;
    ThreatState threats[kThreats]; FlashState flashes[kFlashes];
};
struct RoundState {
    char definition[kName];
    uint32_t target, launcher, allegiance, jumps, armed, special, no_gravity, incoming_member, incarnation; uint64_t schema;
    float start; Vec beam_start, destination;
};
struct UnitPresentation {
    char definition[kName]; uint64_t mesh_schema;
    float life,energy,shields,max_shields,initial_life;
    Vec euler; float turret[6];
    uint32_t allegiance,active,vulnerable,behavior,icon,fenrir;
    int32_t hit,hit_decay;
};
struct CannonState { UnitPresentation unit; float drop,old_drop; uint32_t state,above_water; };
struct FeatureState {
    char definition[kName],sound[kName]; uint64_t mesh_schema;
    float life,initial_life;
    uint32_t allegiance,above_water,invincible,lockable,explosion,icon;
};
struct SegmentState {
    uint32_t type,parent,part,number,stages;
    uint32_t vulnerable,destroyed,cascading,swapped,has_direction,hit;
    float health,max_health,last_damage_time,last_damage_amount,size,total_time,time;
    Vec direction;
};
struct SegmentGraph {
    uint64_t schema;
    uint32_t count,slots,core,core_count,changed,primary_visible,pickup_mode,no_imposter;
    float fall_threshold,total_health,total_size;
    uint32_t alias[kParts]; SegmentState nodes[kParts];
};
struct BuildingState {
    UnitPresentation unit; SegmentGraph graph;
    uint32_t door,can_shut,shut;
    float door_time,close_time,last_hit;
};
struct ExplosionBirth {
    uint32_t serial,id,tick,definition,deep,ready,originator,attached,use_radius,volumetric,important,collision,orientation_type,mask,desired,min_collision,map_who,allegiance;
    uint64_t schema;
    char name[kName]; Vec pos,euler; Mat orientation; float radius;
};
struct ExplosionState { uint32_t serial,definition,deep,originator,allegiance; uint64_t schema; float radius,current_radius; };
struct RoundBirth {
    uint32_t shot,inventory,index,allegiance,actor_phase;
    uint64_t schema,mode_schema;
    char weapon[kName],mode[kName];
    Vec pos,velocity,destination; Mat orientation; float lifespan;
};
struct Entity { Core core; EngineState engine; RoundState round; CannonState cannon; FeatureState feature; BuildingState building; ExplosionState explosion; };
const unsigned kLifeOps=64,kLifeKeyBytes=400,kLifeReceipts=16;
const uint32_t kLifeMagic=0x43454c31u; // CEL1
struct LifeKey { uint32_t id,size; uint8_t bytes[kLifeKeyBytes]; };
struct LifeRecord {
    CE_WorldLifecycleInfo info; uint32_t birth_serial;
    uint32_t definition; uint64_t definition_schema; char definition_name[kName];
    ExplosionBirth birth; Core core; ExplosionState explosion; LifeKey dependency; RoundBirth round_birth; RoundState round;
};
struct LifeSlot { CE_WorldLifecycleInfo info; uint32_t birth_serial; bool published; uint8_t bytes[CE_WORLD_LIFECYCLE_MAX_BYTES]; };
struct LifeLive { CThing *thing; uint32_t id,birth_serial,kind; };
LifeSlot life_journal[kLifeOps],life_duplicates[kLifeOps];
CE_WorldLifecycleInfo life_retired[kLifeReceipts];
LifeKey life_keys[2]; LifeLive life_live[kEntities];
uint8_t life_baseline_bytes[CE_NET_MAX_MESSAGE]; uint32_t life_baseline_size;
uint32_t life_proof_sequence,life_proof_tick; bool life_proof_valid;
uint32_t life_baseline,life_baseline_tick,life_generated,life_published,life_ack,life_applied,life_tick,life_ordinal,life_seen_id;
unsigned life_first,life_count,life_duplicate_first,life_duplicate_count,life_retired_first,life_retired_count,life_key_count,life_live_count;
bool life_enabled,life_failed; CThing *life_pending; LifeRecord life_pending_record;
bool life_creating,life_initialized;
CRound *round_pending,*round_spawn; LifeRecord round_pending_record;
const LifeRecord *applying_round; bool creating_round,round_initialized;
CWeapon *shot_weapon; CWeaponModeData *shot_mode; uint32_t shot_generated,shot_active,shot_inventory,shot_index,last_round_shot;

uint32_t life_committed_tick;
uint32_t life_applied_ordinal;
void LifeReset();
void LifeInvalidateProof();
bool LifeRecordProof(const CE_WorldFrame &);
bool LifeAppend(LifeRecord &);
bool LifeSourceHeader(LifeRecord &,uint32_t kind,uint32_t id,uint32_t serial);
bool LifeRecordBound(const LifeRecord &);
bool LifeDependency(const LifeRecord &);
int LifeReadiness(const CE_WorldFrame &);
bool EncodeLife(const LifeRecord &,uint8_t *,uint32_t &);
bool DecodeLife(const uint8_t *,uint32_t,LifeRecord &);
struct Binding { CThing *thing; uint32_t id, initial_number, kind, segment_slots; uint8_t segment_hit[kParts]; CCockpit *cockpit_initialized; uint32_t cockpit_epoch; ExplosionBirth birth; uint32_t round_incarnation; uint64_t round_schema; };
CE_WorldConfig config;
Binding bindings[kEntities];
uint32_t binding_count, next_id, applied_tick, captured_tick, applied_sequence, captured_sequence, replica_ack;
bool active, applying, poisoned;
char error_text[192];
bool sampling, sampling_failed, mouse_axis;
CE_WorldInput sample;
uint32_t generated_sequence, sent_sequence, sampled_tick, input_suspension, remote_invert_ground, remote_invert_jet, remote_walker_any;

ExplosionBirth birth_journal[kBirths];
uint32_t birth_count,birth_generated,birth_published,birth_ack,birth_applied,published_frame,published_tick;
bool birth_failed,creating_explosion;
const ExplosionBirth *applying_birth;
CExplosion *birth_spawn;
int Fail(const char *message) { snprintf(error_text, sizeof error_text, "%s", message); return 0; }
int FailKind(const char *what, const char *name) {
    snprintf(error_text, sizeof error_text, "%s: %.95s", what, name ? name : "<null>"); return 0;
}
bool Name(char *out, const char *in) {
    if (!in) { out[0] = 0; return true; }
    size_t size = strlen(in);
    if (size >= kName) return false;
    for (size_t i = 0; i < size; ++i) if ((unsigned char)in[i] < 32 || (unsigned char)in[i] > 126) return false;
    memcpy(out, in, size + 1); return true;
}
Vec GetVec(const FVector &v) { Vec out = {{v.X, v.Y, v.Z}}; return out; }
void PutVec(FVector &out, const Vec &v) { out.Set(v.v[0], v.v[1], v.v[2]); }
Mat GetMat(const FMatrix &m) {
    Mat out; for (unsigned i = 0; i < 3; ++i) { Vec row = GetVec(m.Row[i]); for (unsigned j = 0; j < 3; ++j) out.v[i * 3 + j] = row.v[j]; } return out;
}
void PutMat(FMatrix &out, const Mat &m) {
    for (unsigned i = 0; i < 3; ++i) out.Row[i].Set(m.v[i * 3], m.v[i * 3 + 1], m.v[i * 3 + 2]);
}
Binding *ByThing(const CThing *thing) { for (unsigned i = 0; i < binding_count; ++i) if (bindings[i].thing == thing) return &bindings[i]; return NULL; }
Binding *ById(uint32_t id) { for (unsigned i = 0; i < binding_count; ++i) if (bindings[i].id == id && id) return &bindings[i]; return NULL; }
uint32_t Id(CThing *thing) { Binding *b = thing ? ByThing(thing) : NULL; return b ? b->id : 0; }
CThing *Resolve(uint32_t id) { Binding *b = ById(id); return b ? b->thing : NULL; }
bool StaticKind(const char *name) {
    const char *names[] = {"CStart", "CSpawnPoint", "CLevelScriptThing", "CSphereTrigger", "CWaypoint", "CDebris"};
    for (const char *known : names) if (!strcmp(known, name)) return true;
    return false;
}
uint32_t ThingKind(CThing *thing) {
    if (dynamic_cast<CBattleEngine *>(thing)) return ENGINE_THING;
    if (dynamic_cast<CRound *>(thing)) return ROUND_THING;
    if (!strcmp(thing->_GetClassName(),"CExplosion")&&dynamic_cast<CExplosion *>(thing)) return EXPLOSION_THING;
    if (!strcmp(thing->_GetClassName(),"CCannon")&&dynamic_cast<CCannon *>(thing)) return CANNON_THING;
    if (!strcmp(thing->_GetClassName(),"CFeature")&&dynamic_cast<CFeature *>(thing)) return FEATURE_THING;
    if (!strcmp(thing->_GetClassName(),"CBuilding")&&dynamic_cast<CBuilding *>(thing)) return BUILDING_THING;
    if (StaticKind(thing->_GetClassName()) && !dynamic_cast<CActor *>(thing)) return STATIC_THING;
    return 0;
}
Binding *Bind(CThing *thing, uint32_t id, uint32_t initial) {
    if (!thing || !id || binding_count >= kEntities || ByThing(thing) || ById(id)) return NULL;
    Binding &b = bindings[binding_count++]; b.thing = thing; b.id = id; b.initial_number = initial; b.kind = ThingKind(thing); b.segment_slots=0; b.cockpit_initialized=NULL; b.cockpit_epoch=0; b.round_incarnation=0; b.round_schema=0; memset(&b.birth,0,sizeof b.birth); memset(b.segment_hit,0,sizeof b.segment_hit); return &b;
}
void WriteName(CE_NetWriter *w, const char *s) {
    size_t n = strlen(s); if (n >= kName) { w->error = 1; return; }
    CE_NetWriteU8(w, (uint8_t)n); CE_NetWriteBytes(w, (const uint8_t *)s, (uint32_t)n);
}
void ReadName(CE_NetReader *r, char *out) {
    unsigned n = CE_NetReadU8(r); if (n >= kName) { r->error = 1; out[0] = 0; return; }
    CE_NetReadBytes(r, (uint8_t *)out, n); out[n] = 0;
    for (unsigned i = 0; i < n; ++i) if ((unsigned char)out[i] < 32 || (unsigned char)out[i] > 126) r->error = 1;
}
void WriteVec(CE_NetWriter *w, const Vec &v) { for (float f : v.v) CE_NetWriteF32(w, f); }
void ReadVec(CE_NetReader *r, Vec &v) { for (float &f : v.v) f = CE_NetReadF32(r); }
void WriteMat(CE_NetWriter *w, const Mat &m) { for (float f : m.v) CE_NetWriteF32(w, f); }
void ReadMat(CE_NetReader *r, Mat &m) { for (float &f : m.v) f = CE_NetReadF32(r); }
const uint32_t native_actions[]={25,27,26,28,29,30,31,32,59,33,19,18,20,16,17,21};
unsigned ActionRank(uint32_t button) {
    for(unsigned i=0;i<sizeof native_actions/sizeof *native_actions;++i) if(native_actions[i]==button) return i+1;
    return 0;
}
bool ActionAllowed(uint32_t button) { return ActionRank(button)!=0; }
struct LocalInput {
    CE_WorldInput items[kInputs];
    uint32_t first,count;
    bool failed,leased;
    bool Poll(const CE_WorldInput &in) {
        if(count==kInputs) { failed=true; return false; }
        items[(first+count)%kInputs]=in; ++count; return true;
    }
    int Peek(CE_WorldInput &out) {
        if(failed) return -1;
        if(!count) return 0;
        out=items[first]; leased=true; return 1;
    }
    int Sent(uint32_t sequence) {
        if(!leased||!count||items[first].sequence!=sequence) return -1;
        first=(first+1)%kInputs; --count; leased=false; return 1;
    }
} local_input;
struct LocalClock {
    uint64_t last_bucket; bool started;
    bool Ticket(uint64_t now_ms) {
        uint64_t bucket=now_ms/50; // CLOCK_TICK is 0.05 seconds; render polls cannot add extra samples.
        if(started&&bucket<=last_bucket) return false;
        last_bucket=bucket; started=true; return true;
    }
} local_clock;
bool FreshTick(uint32_t applied,uint32_t sampled) { return applied>sampled; }

// Once mutation starts, any refusal or exception poisons the replica. The caller must tear down
// the genuine world before rendering or admitting more input; the event fence stays installed.
struct ApplyScope {
    bool committed;
    ApplyScope():committed(false) { applying=true; }
    ~ApplyScope() { creating_explosion=false; applying_birth=NULL; birth_spawn=NULL; creating_round=false; applying_round=NULL; round_spawn=round_pending=NULL; applying=false; if(!committed) { poisoned=true; LifeInvalidateProof(); } }
};
bool Range(float value,float low,float high) { return value>=low&&value<=high; }
bool VecBound(const Vec &v,float limit=1048576.f) {
    for(float f:v.v) if(!Range(f,-limit,limit)) return false;
    return true;
}
bool MatBound(const Mat &m) {
    for(float f:m.v) if(!Range(f,-4.f,4.f)) return false;
    float d=m.v[0]*(m.v[4]*m.v[8]-m.v[5]*m.v[7])-m.v[1]*(m.v[3]*m.v[8]-m.v[5]*m.v[6])+m.v[2]*(m.v[3]*m.v[7]-m.v[4]*m.v[6]);
    return fabsf(d)>=.0001f&&fabsf(d)<=64.f;
}
bool TimeBound(float time) { return Range(time,-1048576.f,1048576.f); }
bool AngleBound(float angle) { return Range(angle,-64.f,64.f); }
bool AnimationBound(const Animation &a) {
    return !a.present||(Range(a.frame,0.f,1.f)&&Range(a.old_frame,0.f,1.f)&&Range(a.increment,0.f,1048576.f)&&(int32_t)a.index>=-1);
}
bool FrameBound(float frame,int frames) { return frames>0&&Range(frame,0.f,(float)frames); }
bool AnimationIndexBound(uint32_t index,int modes) { return (int32_t)index==-1||(modes>0&&index<(uint32_t)modes); }
bool UnitKind(uint32_t kind) { return kind==ENGINE_THING||kind==CANNON_THING||kind==BUILDING_THING; }
int SegmentHit(float now,int hit,float damage_time,float amount) {
    float age=now-damage_time;
    if(age>=1.f) return 0;
    int next=hit+(int)(amount*100.f); if(next>100) next=100;
    int level=next-(int)(age*20.f)*10;
    return level<0?0:(level>100?100:level);
}
Vec ExtraDirection(bool present,const FVector *direction) { return present?GetVec(*direction):Vec{{0.f,0.f,0.f}}; }
bool EndCountBound(uint32_t phase,float count) {
    // Native constructor/LoadLevel use -1 until a result starts. Results begin at five or two seconds;
    // level-won can step just below zero before its quit is observed by the caller.
    if(phase<=GAME_STATE_PLAYING) return count==-1.f;
    return Range(count,phase==GAME_STATE_LEVEL_WON?-CLOCK_TICK:0.f,5.f);
}
bool ValidInput(const CE_WorldInput &in) {
    if (!in.epoch || !in.sequence || !in.tick || in.player != 1 || in.count > CE_WORLD_MAX_ACTIONS ||
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
struct InputQueue {
    CE_WorldInput items[kInputs]; uint32_t due[kInputs];
    uint32_t first, count, accepted, consumed, last_due;
    int Push(const CE_WorldInput &in,uint32_t epoch,uint32_t now) {
        if(!ValidInput(in)||in.epoch!=epoch) { Fail("input is not for the admitted remote player"); return -1; }
        if(in.sequence<=accepted) return 1; // accepted commands are immutable and never dispatched twice
        if(in.sequence!=accepted+1) { Fail("input sequence gap requires resynchronization"); return -1; }
        if(in.tick>now&&in.tick-now>=kInputs) { Fail("input desired tick exceeds the host window"); return -1; }
        uint32_t scheduled=now+1;
        if(in.tick>scheduled) scheduled=in.tick;
        if(count&&last_due>=scheduled) scheduled=last_due+1;
        if(!scheduled||count==kInputs||scheduled-now>=kInputs) return 0;
        unsigned index=(first+count)%kInputs; items[index]=in; due[index]=scheduled;
        ++count; accepted=in.sequence; last_due=scheduled; return 1;
    }
    bool Peek(uint32_t now,CE_WorldInput &out) const { if(!count||due[first]>now) return false; out=items[first]; return true; }
    void Consumed() { consumed=items[first].sequence; first=(first+1)%kInputs; --count; }
} commands;
Vec BeamStart(bool beam,const FVector *position) { return beam ? GetVec(*position) : Vec{{0.f,0.f,0.f}}; }
}

struct ObjectiveState { int32_t state, string_id; };

struct CE_WorldFrame {
    uint32_t epoch, tick, sequence, level, phase, paused, count, consumed_sequence, birth_count;
    uint32_t lifecycle_baseline,lifecycle_watermark;
    ExplosionBirth births[kBirths];
    int32_t score, lives[2];
    int32_t counters_a[NUM_WORLD_COUNTERS],counters_b[NUM_WORLD_COUNTERS];
    ObjectiveState primary[MAX_PRIMARY_OBJECTIVES], secondary[MAX_SECONDARY_OBJECTIVES];
    float end_count; uint32_t fade, alpha;
    Entity entities[kEntities];
};

// Only the generated mirror grants this class field access. The matching source layout remains untouched.
class CENetworkWorldAdapter {
public:
    static void CaptureObjectives(CE_WorldFrame &out) {
        for (unsigned i=0; i<MAX_PRIMARY_OBJECTIVES; ++i) {
            CMissionObjective *objective=GAME.GetPrimaryObjective(i);
            out.primary[i]={objective->GetStatus(),objective->GetStringID()};
        }
        for (unsigned i=0; i<MAX_SECONDARY_OBJECTIVES; ++i) {
            CMissionObjective *objective=GAME.GetSecondaryObjective(i);
            out.secondary[i]={objective->GetStatus(),objective->GetStringID()};
        }
    }
    static void ApplyObjectives(const CE_WorldFrame &in) {
        for (unsigned i=0; i<MAX_PRIMARY_OBJECTIVES; ++i)
            GAME.GetPrimaryObjective(i)->Set(in.primary[i].state,in.primary[i].string_id);
        for (unsigned i=0; i<MAX_SECONDARY_OBJECTIVES; ++i)
            GAME.GetSecondaryObjective(i)->Set(in.secondary[i].state,in.secondary[i].string_id);
    }
    static bool ObjectivesBound(const CE_WorldFrame &in) {
        for (unsigned i=0; i<MAX_PRIMARY_OBJECTIVES; ++i)
            if (in.primary[i].state<MOS_NOT_DEFINED || in.primary[i].state>MOS_FAILED) return false;
        for (unsigned i=0; i<MAX_SECONDARY_OBJECTIVES; ++i)
            if (in.secondary[i].state<MOS_NOT_DEFINED || in.secondary[i].state>MOS_FAILED) return false;
        return true;
    }
    class ReplicaCamera : public CCamera {
    public:
        CameraState state;
        FVector GetPos() { FVector out; PutVec(out,state.pos); return out; }
        FVector GetOldPos() { FVector out; PutVec(out,state.old_pos); return out; }
        FMatrix GetOrientation() { FMatrix out; PutMat(out,state.orientation); return out; }
        FMatrix GetOldOrientation() { FMatrix out; PutMat(out,state.old_orientation); return out; }
        const float GetZoom() { return state.zoom; }
        const float GetOldZoom() { return state.old_zoom; }
        bool GetShowHUD() { return state.hud != 0; }
    };
    static CMCBattleEngine *Mech(CBattleEngine *engine) {
        CMotionController *controller = engine->mMotionController ? engine->mMotionController : engine->mHackBackupMC;
        return dynamic_cast<CMCBattleEngine *>(controller);
    }
    static bool CoreCapture(CThing *thing, Core &out) {
        Binding *b = ByThing(thing); if (!b) return false;
        out.id = b->id; out.kind = b->kind; out.initial_number = b->initial_number;
        out.flags = (unsigned short)thing->mFlags; out.type = thing->mThingType;
        if(b->kind==EXPLOSION_THING&&b->birth.deep) {
            FMatrix identity(0.f,0.f,0.f); out.orientation=out.old_orientation=GetMat(identity);
            return Name(out.class_name,thing->_GetClassName()); // native deep-water arm never initializes mPos or own fields
        }
        out.pos = out.old_pos = GetVec(thing->mPos);
        FMatrix identity(0.f, 0.f, 0.f); out.orientation = out.old_orientation = GetMat(identity);
        if (!Name(out.class_name, thing->_GetClassName())) return false;
        CMesh *mesh = thing->GetRTMesh(); if (!Name(out.mesh, mesh ? mesh->GetName() : "")) return false;
        if (CComplexThing *complex = dynamic_cast<CComplexThing *>(thing)) {
            out.orientation = GetMat(complex->mOrientation);
            if (complex->mAnimation) {
                CAnimation *a = complex->mAnimation; out.animation.present = 1;
                out.animation.frame = a->mFrame; out.animation.old_frame = a->mLastFrame; out.animation.increment = a->mFrameIncrement;
                out.animation.mode = a->mAnimMode; out.animation.index = a->mAnimIndex; out.animation.looped = a->mForceLooped;
            }
            if (out.kind == STATIC_THING && complex->mMotionController) return FailKind("unsupported static controller", out.class_name);
        }
        if (CActor *actor = dynamic_cast<CActor *>(thing)) {
            out.old_pos = GetVec(actor->mOldPos); out.velocity = GetVec(actor->mVelocity); out.old_orientation = GetMat(actor->mOldOrientation);
            out.last_move = actor->mLastMoveTime; out.ground = actor->mLastTimeOnGround; out.water = actor->mLastTimeInWater; out.object = actor->mLastTimeOnObject;
        }
        return true;
    }
    struct MeshSchema {
        uint64_t value=14695981039346656037ull;
        void Byte(uint8_t v) { value^=v; value*=1099511628211ull; }
        void Word(uint32_t v) { for(unsigned i=0;i<4;++i) Byte((uint8_t)(v>>(24-i*8))); }
        bool Text(const char *text,unsigned cap=kName) {
            if(!text) { Word(0); return true; }
            unsigned n=0; while(n<cap&&text[n]) ++n;
            if(n==cap) return false;
            // Authored part labels may contain trailing tabs. These bytes are hashed locally, not sent as text.
            Word(n); for(unsigned i=0;i<n;++i) Byte((uint8_t)text[i]);
            return true;
        }
    };
    static bool OwnedRendererSchema(CRenderThing *renderer,IRenderableThing *owner,uint64_t &out,const Animation &animation) {
        CRTMesh *rt=dynamic_cast<CRTMesh *>(renderer);
        if(!rt||rt->mChild||rt->mThing!=owner||!rt->mMesh||!rt->mPose||
            rt->mPose->mNumParts!=rt->mMesh->mNumParts||!rt->mPose->mPositions||!rt->mPose->mOrientations||!rt->mPose->mFrames||!rt->mPose->mParents)
            return Fail("loaded renderer owner or allocated pose topology differs");
        MeshSchema hash; CMesh *seen[4]={}; unsigned levels=0;
        for(CMesh *mesh=rt->mMesh;mesh;mesh=mesh->mNextInChain) {
            if(levels==4) return Fail("mesh LOD chain exceeds admitted topology");
            for(unsigned i=0;i<levels;++i) if(seen[i]==mesh) return Fail("cyclic mesh LOD chain");
            seen[levels++]=mesh;
            if(mesh->mNumParts<1||mesh->mNumParts>(int)kParts||mesh->mNumParts>rt->mPose->mNumParts||!mesh->mParts||
                mesh->mNumAnimModes<0||mesh->mNumAnimModes>256||(mesh->mNumAnimModes&&!mesh->mAnimModes)||!hash.Text(mesh->GetName()))
                return Fail("unsupported loaded mesh dimensions");
            if(animation.present&&(!AnimationIndexBound(animation.index,mesh->mNumAnimModes)||
                ((int32_t)animation.index==-1?((int32_t)animation.mode!=AM_INVALID&&(int32_t)animation.mode!=AM_NONE):mesh->mAnimModes[animation.index].mMode!=(int32_t)animation.mode)))
                return Fail("animation index or mode differs from a locally selected LOD");
            hash.Word(mesh->mNumParts); hash.Word(mesh->mNumAnimModes);
            for(int i=0;i<mesh->mNumAnimModes;++i) {
                CAnimMode &mode=mesh->mAnimModes[i];
                if(!hash.Text(mode.mModeName,sizeof mode.mModeName)) return Fail("unsupported native animation label");
                hash.Word(mode.mMode); hash.Word(mode.mStart); hash.Word(mode.mLast); hash.Word(mode.mFrameScaler);
            }
            for(int i=0;i<mesh->mNumParts;++i) {
                CMeshPart *part=mesh->mParts[i];
                if(!part||part->mPartNumber!=i||part->mNumChildren<0||part->mNumChildren>(int)kParts||
                    (part->mNumChildren&&!part->mChildren)||part->mNumVFrames<1||part->mNumHFrames<1||part->mNumAFrames<0||!hash.Text(part->mName,sizeof part->mName))
                    return Fail("unsupported native mesh part topology");
                int parent=-1;
                if(part->mParent) {
                    parent=part->mParent->mPartNumber;
                    if(parent<0||parent>=mesh->mNumParts||mesh->mParts[parent]!=part->mParent) return Fail("mesh parent is outside local LOD");
                }
                int next=part->mNextMeshPartInChain?part->mNextMeshPartInChain->mPartNumber:-1;
                if(part->mNumChainedParts<0||part->mNumChainedParts>=mesh->mNumParts||
                    (next!=-1&&(next<0||next>=mesh->mNumParts||mesh->mParts[next]!=part->mNextMeshPartInChain))) return Fail("unsupported damage mesh chain");
                CMeshPart *chain=part; unsigned chain_depth=0;
                while(chain->mNextMeshPartInChain) { chain=chain->mNextMeshPartInChain; int index=chain->mPartNumber;
                    if(index<0||index>=mesh->mNumParts||mesh->mParts[index]!=chain||++chain_depth>=(unsigned)mesh->mNumParts) return Fail("cyclic or foreign damage mesh chain"); }
                if(chain_depth!=(unsigned)part->mNumChainedParts) return Fail("damage mesh chain count differs");
                hash.Word(part->mIsChained!=0); hash.Word(part->mNumChainedParts); hash.Word(next+1);
                hash.Word(i); hash.Word(part->mPartType); hash.Word(parent+1); hash.Word(part->mNumVFrames); hash.Word(part->mNumHFrames); hash.Word(part->mNumAFrames); hash.Word(part->mNumChildren);
                for(int child=0;child<part->mNumChildren;++child) {
                    CMeshPart *p=part->mChildren[child]; int index=p?p->mPartNumber:-1;
                    if(index<0||index>=mesh->mNumParts||mesh->mParts[index]!=p||p->mParent!=part) return Fail("mesh child is outside local LOD");
                    hash.Word(index);
                }
                CMeshPart *walk=part; unsigned depth=0;
                while(walk->mParent) { walk=walk->mParent; if(++depth>=(unsigned)mesh->mNumParts) return Fail("cyclic local mesh hierarchy"); }
                if(walk!=mesh->mParts[0]) return Fail("native mesh has an unrooted part");
            }
        }
        if(rt->mNumEffects<0||rt->mNumEffects>32||(rt->mNumEffects&&(!rt->mEffectParts||!rt->mEffectPartPointers||!rt->mEffectIds||!rt->mEffectTypes||!rt->mEffects||!rt->mEffectActive))) return Fail("unsupported local effect-slot topology");
        hash.Word(levels); hash.Word(rt->mNumEffects);
        for(int i=0;i<rt->mNumEffects;++i) {
            CMeshPart *part=rt->mEffectPartPointers[i]; int index=part?part->mPartNumber:-1;
            if(index<0||index>=rt->mMesh->mNumParts||rt->mMesh->mParts[index]!=part) return Fail("effect part differs from local mesh");
            hash.Word(rt->mEffectParts[i]); hash.Word(index); hash.Word(rt->mEffectIds[i]); hash.Word(rt->mEffectTypes[i]!=NULL);
        }
        out=hash.value; return true;
    }
    static bool RendererSchema(CThing *thing,uint64_t &out,const Animation &animation) {
        return OwnedRendererSchema(thing->mRenderThing,static_cast<IRenderableThing *>(thing),out,animation);
    }
    static bool CockpitBound(const CockpitState &in) {
        return in.mesh[0]&&in.schema&&in.should_render<=1&&VecBound(in.shake,16.f)&&VecBound(in.old_shake,16.f)&&
            MatBound(in.orientation)&&MatBound(in.old_orientation)&&Range(in.frame,0.f,1.f)&&Range(in.old_frame,0.f,1.f)&&(int32_t)in.index>=-1;
    }
    static bool CockpitTopology(CBattleEngine *engine,CockpitState &out,const CockpitState *expected=NULL) {
        CCockpit *cockpit=engine->mCockpit;
        if(!cockpit||!PortTypeInfoEqual(typeid(*cockpit),typeid(CDXCockpit))||cockpit->mBattleEngine!=engine||!cockpit->mRenderThing||
            !PortTypeInfoEqual(typeid(*cockpit->mRenderThing),typeid(CRTMesh))) return Fail("unsupported genuine cockpit owner or renderer");
        CRTMesh *rt=(CRTMesh *)cockpit->mRenderThing;
        CBattleEngineData *definition=engine->GetConfiguration();
        if(!definition||!definition->mCockpit||!rt->mMesh||!Name(out.mesh,rt->mMesh->GetName())||strcmp(definition->mCockpit,out.mesh))
            return Fail("cockpit authored model differs from the genuine engine configuration");
        // Constructor initializes animation clock/index, but not old world/shake transforms. Topology admission
        // deliberately does not inspect those old fields; host capture needs the native initialized marker first.
        Animation animation={}; animation.present=1;
        animation.mode=expected?expected->mode:(uint32_t)cockpit->mAnimMode;
        animation.index=expected?expected->index:(uint32_t)cockpit->mRealAnimIndex;
        animation.frame=expected?expected->frame:cockpit->mFrame; animation.old_frame=expected?expected->old_frame:cockpit->mOldFrame;
        return OwnedRendererSchema(rt,static_cast<IRenderableThing *>(cockpit),out.schema,animation);
    }
    static void CockpitInitialized(CCockpit *cockpit) {
        if(!active||config.role!=CE_WORLD_HOST||!cockpit) return;
        CBattleEngine *owner=cockpit->mBattleEngine; Binding *binding=ByThing(owner);
        if(binding&&binding->kind==ENGINE_THING&&owner->mCockpit==cockpit) {
            binding->cockpit_initialized=cockpit; binding->cockpit_epoch=config.epoch;
        }
    }
    static bool CockpitCapture(CBattleEngine *engine,CockpitState &out) {
        CCockpit *cockpit=engine->mCockpit; Binding *binding=ByThing(engine);
        if(!cockpit||!binding||binding->cockpit_initialized!=cockpit||binding->cockpit_epoch!=config.epoch)
            return Fail("authoritative cockpit has not completed native presentation initialization");
        if(!CockpitTopology(engine,out)) return false;
        out.should_render=!!cockpit->mShouldRender; out.mode=cockpit->mAnimMode; out.index=cockpit->mRealAnimIndex;
        out.shake=GetVec(cockpit->mShakePos); out.old_shake=GetVec(cockpit->mOldShakePos);
        out.orientation=GetMat(cockpit->mOrientation); out.old_orientation=GetMat(cockpit->mOldOrientation);
        out.frame=cockpit->mFrame; out.old_frame=cockpit->mOldFrame; return true;
    }
    static void CockpitApply(CCockpit *cockpit,const CockpitState &in) {
        cockpit->mShouldRender=in.should_render; cockpit->mAnimMode=(EAnimMode)in.mode; cockpit->mRealAnimIndex=(int32_t)in.index;
        PutVec(cockpit->mShakePos,in.shake); PutVec(cockpit->mOldShakePos,in.old_shake);
        PutMat(cockpit->mOrientation,in.orientation); PutMat(cockpit->mOldOrientation,in.old_orientation);
        cockpit->mFrame=in.frame; cockpit->mOldFrame=in.old_frame;
        // Native forced pose Update still keeps a same-render-frame cache. Apply precedes every emitter/draw query.
        cockpit->mRenderThing->mPose->mLastFrame=-9999;
    }
    static bool ExplosionDefinition(CExplosionData *data,uint32_t &index,uint64_t &schema,char *name) {
        if(!data||!UPhysicsManager::mExplosions||UPhysicsManager::mExplosions->Size()<1||UPhysicsManager::mExplosions->Size()>1024||
            !Name(name,data->mName)||!name[0]||!TimeBound(data->mRadius)||!TimeBound(data->mDamage)||data->mVolumetric<0||data->mVolumetric>1024||
            data->mSmart<0||data->mSmart>1||data->mOriented<0||data->mOriented>1||data->mLight< -1||data->mLight>4)
            return Fail("unsupported resolved explosion definition");
        MeshSchema hash; if(!hash.Text(data->mName)) return false;
        unsigned found=0,n=0; ListIterator<CExplosionData> it(UPhysicsManager::mExplosions);
        for(CExplosionData *d=it.First();d;d=it.Next(),++n) {
            if(d==data) { index=n; ++found; }
            else if(d->mName&&!strcmp(d->mName,data->mName)) return Fail("explosion definition name is not unique");
        }
        if(found!=1) return Fail("explosion definition is not uniquely registered");
        const char *names[]={data->mParticleAName,data->mParticleBName,data->mParticleCName,data->mParticleDName,data->mSoundAName,data->mSoundBName};
        for(const char *text:names) if(!hash.Text(text)) return Fail("explosion effect identity is too long");
        const bool present[]={data->mParticleA!=NULL,data->mParticleB!=NULL,data->mParticleC!=NULL,data->mParticleD!=NULL,data->mSoundA!=NULL,data->mSoundB!=NULL};
        for(bool value:present) hash.Word(value);
        uint32_t radius,damage; memcpy(&radius,&data->mRadius,4); memcpy(&damage,&data->mDamage,4); hash.Word(radius); hash.Word(damage);
        hash.Word(data->mVolumetric); hash.Word(data->mTime); hash.Word(data->mSmart); hash.Word(data->mOriented); hash.Word(data->mLight);
        schema=hash.value; return true;
    }
    static bool BirthBound(const ExplosionBirth &b,uint32_t tick) {
        if(!b.serial||!b.id||b.tick>tick||b.deep>1||b.ready!=1||!VecBound(b.pos)) return false;
        if(b.deep) return !b.definition&&!b.schema&&!b.name[0]&&!b.attached&&!b.use_radius&&!b.volumetric&&!b.important&&!b.collision&&!b.radius&&!b.mask&&!b.desired&&!b.min_collision&&!b.map_who&&!b.allegiance;
        return b.definition<1024&&b.schema&&b.name[0]&&b.use_radius<=1&&b.volumetric<=1&&b.important<=1&&b.collision<=kCollideThing&&
            b.orientation_type<=DIRECTION_COSINE_MATRIX&&VecBound(b.euler,64.f)&&MatBound(b.orientation)&&TimeBound(b.radius)&&
            b.desired<=ECL_MESH&&b.min_collision<=ECL_MESH&&b.map_who<=1&&b.allegiance<=6&&(b.mask&THING_TYPE_EXPLOSION);
    }
    static void BirthFailure(const char *why) { birth_failed=true; if(life_enabled) life_failed=true; Fail(why); }
    static CWeapon *WeaponAt(CBattleEngine *engine,uint32_t inventory,uint32_t index) {
        if(!engine||!engine->mWalkerPart||!engine->mJetPart) return NULL;
        if(inventory<2) return index?NULL:(inventory?engine->mWalkerPart->mAugWeapon:engine->mWalkerPart->mPrimaryWeapon);
        SPtrSet<CWeapon> *list=inventory==2?&engine->mWalkerPart->mWeapons:inventory==3?&engine->mJetPart->mWeapons:inventory==4?&engine->mWeapons:NULL;
        if(!list||list->Size()>kWeapons||index>=kWeapons) return NULL;
        ListIterator<CWeapon> it(list); unsigned n=0;
        for(CWeapon *weapon=it.First();weapon;weapon=it.Next(),++n) if(n==index) return weapon;
        return NULL;
    }
    // Native construction performs a sampled line query; admit a bounded nominal path before any factory.
    static bool RoundDefinition(CRoundData *data,uint64_t &schema) {
        if(!data||!data->mName||(strcmp(data->mName,"Mech Pulse Bolt Medium")&&strcmp(data->mName,"Mech Pulse Bolt Large"))||
           !UPhysicsManager::mRounds||UPhysicsManager::mRounds->Size()<1||UPhysicsManager::mRounds->Size()>1024||
           data->mUnknown70||data->mBeam||data->mFlak||data->mWeirdoSeek||data->mProximity||data->mSeekDelay||data->mRearm||!data->mExplode||data->mUnknown48||data->mJumps||data->mGravity||data->mBounceFactor||data->mUnknown28||
           data->mWiggle||data->mGroundHugging||data->mUnknown6C||data->mStreamLaser||data->mGillMBreath||data->mUnknown60||data->mUnknown58||
           data->mCanGoUnderWater||data->mPassiveCollision||data->mTreeCollision||data->mUnknown0c||data->mSmart||
           !(data->mRadius>0.f)||!(data->mSpeed>0.f)||!(data->mLifeSpan>0.f)||!(data->mUnknown1C>0.f)||
           !Range(data->mSpeed*data->mLifeSpan,.001f,32768.f))
            return Fail("round lifecycle admits selected straight pulse definitions only");
        MeshSchema hash; unsigned found=0; ListIterator<CRoundData> it(UPhysicsManager::mRounds);
        for(CRoundData *other=it.First();other;other=it.Next()) {
            if(other==data) ++found;
            else if(other->mName&&!strcmp(other->mName,data->mName)) return Fail("round definition name is ambiguous");
        }
        if(found!=1) return Fail("round definition is not uniquely registered");
        const char *names[]={data->mName,data->mUnknown08,data->mUnknown0c,data->mParticleAName,data->mParticleBName};
        for(const char *name:names) if(!hash.Text(name)) return Fail("round resource name exceeds identity bound");
        hash.Word(data->mParticleA!=NULL); hash.Word(data->mParticleB!=NULL);
#define ROUND_HASH_FLOAT(field) { if(!TimeBound(data->field)) return Fail("round definition has unsupported numeric state"); uint32_t bits; memcpy(&bits,&data->field,4); hash.Word(bits); }
        ROUND_HASH_FLOAT(mUnknown1C) ROUND_HASH_FLOAT(mRearm) ROUND_HASH_FLOAT(mLifeSpan) ROUND_HASH_FLOAT(mUnknown28) ROUND_HASH_FLOAT(mSpeed)
        ROUND_HASH_FLOAT(mBounceFactor) ROUND_HASH_FLOAT(mSeekDelay) ROUND_HASH_FLOAT(mWiggle) ROUND_HASH_FLOAT(mGravity) ROUND_HASH_FLOAT(mSeekAngle)
        ROUND_HASH_FLOAT(mSeekTerminationTime) ROUND_HASH_FLOAT(mFlakInaccuracy) ROUND_HASH_FLOAT(mProximity) ROUND_HASH_FLOAT(mRadius)
        ROUND_HASH_FLOAT(mUnknown90) ROUND_HASH_FLOAT(mJumpDelay) ROUND_HASH_FLOAT(mLength)
#undef ROUND_HASH_FLOAT
        const uint32_t words[]={(uint32_t)data->mUnknown48,(uint32_t)data->mFlak,(uint32_t)data->mBeam,(uint32_t)data->mWeirdoSeek,(uint32_t)data->mUnknown58,(uint32_t)data->mCanGoUnderWater,
            (uint32_t)data->mUnknown60,(uint32_t)data->mPassiveCollision,(uint32_t)data->mGroundHugging,(uint32_t)data->mUnknown6C,(uint32_t)data->mUnknown70,(uint32_t)data->mExplode,
            (uint32_t)data->mSmart,(uint32_t)data->mSoundMaterial,(uint32_t)data->mJumps,(uint32_t)data->mStreamLaser,(uint32_t)data->mGillMBreath,(uint32_t)data->mTreeCollision};
        for(uint32_t word:words) hash.Word(word);
        schema=hash.value; return true;
    }
    static bool RoundEntryValues(CRoundData *data,const RoundBirth &birth) {
        // SpawnRound supplies this exact data lifespan for the admitted non-flak path. Its velocity is the
        // orientation's Y column (MatBound <=4/component) times speed/20. A conservative speed/4 envelope
        // includes float rounding without accepting an unbounded native Map line-query loop at Init.
        if(!data||birth.lifespan!=data->mLifeSpan||!VecBound(birth.velocity,data->mSpeed*.25f)) return false;
        FVector velocity; PutVec(velocity,birth.velocity);
        const float magnitude=velocity.Magnitude(); // Init divides by this native norm times GAME_FR.
        return std::isfinite(magnitude)&&magnitude>0.f;
    }
    static bool PulseMode(CWeapon *weapon,CWeaponModeData *mode,uint64_t &schema) {
        if(!weapon||!weapon->mData||!weapon->mData->mName||strcmp(weapon->mData->mName,"Pulse Cannon Pod")||!mode||!mode->mName||
           (strcmp(mode->mName,"Mech Pulse Cannon Charged")&&strcmp(mode->mName,"Mech Pulse Cannon Charged 2"))||mode->mClipName||
           mode->mVolleySize!=1||mode->mBurstSize!=1||!TimeBound(mode->mInaccuracy)||!TimeBound(mode->mReloadTime))
            return Fail("shot context requires the selected single-volley pulse weapon mode");
        uint64_t round_schema=0; if(!RoundDefinition(mode->mRound,round_schema)) return false;
        if(!UPhysicsManager::mWeaponModes||UPhysicsManager::mWeaponModes->Size()<1||UPhysicsManager::mWeaponModes->Size()>1024) return false;
        bool member=false; MeshSchema hash; hash.Text(weapon->mData->mName); hash.Text(mode->mName);
        for(unsigned i=0;i<5;++i) { hash.Word(weapon->mData->mChargeModes[i]); if(CWeapon::GetWeaponMode(weapon->mData->mChargeModes[i])==mode) member=true; }
        unsigned registered=0; ListIterator<CWeaponModeData> it(UPhysicsManager::mWeaponModes);
        for(CWeaponModeData *other=it.First();other;other=it.Next()) {
            if(other==mode) ++registered;
            else if(other->mName&&!strcmp(other->mName,mode->mName)) return Fail("shot mode name is ambiguous");
        }
        if(!member||registered!=1) return Fail("shot mode is outside the genuine weapon charge table");
        const char *names[]={mode->mClipName,mode->mParticleAName,mode->mParticleBName,mode->mSoundAName,mode->mSoundBName,mode->mSoundCName};
        for(const char *name:names) if(!hash.Text(name)) return false;
        const uint32_t words[]={mode->mParticleA!=NULL,mode->mParticleB!=NULL,mode->mSoundA!=NULL,mode->mSoundB!=NULL,mode->mSoundC!=NULL,
            (uint32_t)mode->mVolleySize,(uint32_t)mode->mBurstSize,(uint32_t)mode->mSoundPerBurst,(uint32_t)mode->mMuzzleLight};
        for(uint32_t word:words) hash.Word(word);
        const float mode_values[]={mode->mInaccuracy,mode->mReloadTime}; for(float value:mode_values) { uint32_t bits; memcpy(&bits,&value,4); hash.Word(bits); }
        hash.Word((uint32_t)round_schema); hash.Word((uint32_t)(round_schema>>32)); schema=hash.value; return true;
    }
    static uint32_t ShotBegin(CWeapon *weapon,CWeaponModeData *mode) {
        if(!active||config.role!=CE_WORLD_HOST||!mode) return 0;
        if(!life_enabled) { BirthFailure("pre-baseline shot history is not retained"); return 0; }
        if(birth_failed||life_failed||shot_active||!weapon||!weapon->mAttachedTo||shot_generated==0xffffffffu) {
            BirthFailure("shot context is failed, nested or exhausted"); return 0;
        }
        Binding *owner=ByThing(weapon->mAttachedTo); uint64_t schema=0;
        if(!owner||owner->kind!=ENGINE_THING||!PulseMode(weapon,mode,schema)) { BirthFailure("shot owner or mode is unsupported"); return 0; }
        unsigned found=0; uint32_t inventory=0,index=0;
        for(unsigned list=0;list<5;++list) for(unsigned n=0;n<kWeapons;++n) if(WeaponAt((CBattleEngine *)owner->thing,list,n)==weapon) { ++found; inventory=list; index=n; }
        if(found!=1) { BirthFailure("shot weapon does not have one canonical owned inventory identity"); return 0; }
        shot_weapon=weapon; shot_mode=mode; shot_inventory=inventory; shot_index=index; shot_active=++shot_generated; return shot_active;
    }
    static void ShotEnd(CWeapon *weapon,uint32_t id) {
        if(!active||config.role!=CE_WORLD_HOST||!id) return;
        if(shot_weapon!=weapon||shot_active!=id||round_pending) BirthFailure("shot scope ended with mismatched or incomplete native Init");
        shot_weapon=NULL; shot_mode=NULL; shot_active=0;
    }
    static void RoundEntry(CRound *round,CInitThing *base) {
        if(!active) return;
        if(config.role==CE_WORLD_REPLICA) {
            if(!applying_round||round_spawn!=round||round_pending!=round) { poisoned=true; Fail("uncontrolled replica round Init"); }
            return;
        }
        if(!life_enabled) { BirthFailure("pre-baseline round history is not retained"); return; }
        if(birth_failed||life_failed) return;
        CRoundInitThing *init=(CRoundInitThing *)base;
        if(round_pending||life_count==kLifeOps||life_live_count==kEntities||!shot_active||!shot_weapon||!shot_mode||!PortTypeInfoEqual(typeid(*round),typeid(CRound))||
           round->mUnknownEC.ToRead()!=shot_weapon->mAttachedTo||round->mUnknownE8.ToRead()||init->mRoundData!=shot_mode->mRound||
           init->mOrientationType!=DIRECTION_COSINE_MATRIX||init->mJumpsPerformed||init->mInitialDelay||init->mName[0]||init->mScript[0]||init->mSpawnScript[0]||
           !init->mActive||init->mForceRadius!=-1.f||init->mInitCST.mForThing||init->mInitCST.mNotSeekCollisionWithBF||init->mInitCST.mShape||init->mInitCST.mDesiredCollision!=ECL_APPROX_GEOMETRY_SHAPES||init->mInitCST.mMinCollision!=ECL_OUTER_SPHERE||init->mInitCST.mMaxCollision!=ECL_MESH||init->mInitCST.mCollisionResponse!=ECR_SLIDE||init->mInitCST.mDoOBBForMeshCol||!init->mInitCST.mStartCollideOnNextFrame||init->mInitCST.mTimeBeforeStart!=-1.f||
           init->mInitCST.mUseFixedMeshTransforms||init->mInitCST.mIgnoreAnimationCollision||init->mInitCST.mRoundLength||init->mInitCST.mIgnoreThing)
            { BirthFailure("round entry needs an unsupported initialization or dependency context"); return; }
        LifeRecord record={}; RoundBirth &entry=record.round_birth;
        entry.shot=shot_active; entry.inventory=shot_inventory; entry.index=shot_index; entry.allegiance=init->mAllegiance;
        if(!RoundDefinition(init->mRoundData,entry.schema)||!PulseMode(shot_weapon,shot_mode,entry.mode_schema)||
           !Name(entry.weapon,shot_weapon->mData->mName)||!Name(entry.mode,shot_mode->mName)) { birth_failed=life_failed=true; return; }
        entry.pos=GetVec(init->mPos); entry.velocity=GetVec(init->mVelocity); entry.orientation=GetMat(init->mOrientation);
        entry.destination=GetVec(init->mDest); entry.lifespan=init->mLifeSpan;
        uint32_t owner=Id(shot_weapon->mAttachedTo); unsigned key=0; while(key<life_key_count&&life_keys[key].id!=owner) ++key;
        if(key==life_key_count||!RoundEntryValues(init->mRoundData,entry)||!VecBound(entry.pos)||!VecBound(entry.velocity)||!VecBound(entry.destination)||!MatBound(entry.orientation)||!Range(entry.lifespan,.001f,1048576.f))
            { BirthFailure("round original entry or baseline owner is outside admission bounds"); return; }
        record.dependency=life_keys[key]; round_pending=round; round_pending_record=record;
    }
    static void RoundComplete(CRound *round) {
        if(!active) return;
        if(config.role==CE_WORLD_REPLICA) {
            if(applying_round&&round_spawn==round&&round_pending==round) { round_initialized=true; round_pending=NULL; }
            else { poisoned=true; Fail("round Init completion has no live controlled token"); }
            return;
        }
        if(!life_enabled||birth_failed||life_failed) return;
        if(round_pending!=round) { BirthFailure("round Init completion token was invalidated"); return; }
        if(round->mDoFullMoveCount!=1||round->mUnknown124||round->mUnknown130!=1||round->mMotionController||round->mAnimation||round->mRenderThing||
           (round->mFlags&(TF_DYING|TF_DECLARED_SHUTDOWN))) { BirthFailure("round native Init did not reach the selected initialized state"); return; }
        Binding *binding=Bind(round,next_id++,0); if(!binding||!next_id) { BirthFailure("round identity capacity exhausted"); return; }
        LifeRecord record=round_pending_record; record.round_birth.actor_phase=round->mDoFullMoveCount;
        if(!LifeSourceHeader(record,CE_WORLD_LIFECYCLE_ROUND_BIRTH,binding->id,life_generated+1)) { BirthFailure("round operation sequence exhausted"); return; }
        binding->round_incarnation=record.info.operation; binding->round_schema=record.round_birth.schema;
        if(!CoreCapture(round,record.core)||!RoundCapture(round,record.round)||!LifeAppend(record)) { BirthFailure("completed round cannot be encoded"); return; }
        life_live[life_live_count++]={round,binding->id,binding->round_incarnation,ROUND_THING}; round_pending=NULL;
    }
    static void RoundShutdown(CRound *round) {
        if(!active||!life_enabled||applying) return;
        if(round_pending==round) { round_pending=NULL; BirthFailure("round shut down before Init completion"); return; }
        unsigned index=0; while(index<life_live_count&&life_live[index].thing!=round) ++index;
        if(index==life_live_count) return;
        if(config.role!=CE_WORLD_HOST) { poisoned=true; Fail("uncontrolled replica round shutdown"); return; }
        if(GAME.mQuit==QT_NONE) {
            LifeRecord record={};
            if(!CoreCapture(round,record.core)||!RoundCapture(round,record.round)||!LifeSourceHeader(record,CE_WORLD_LIFECYCLE_ROUND_DESPAWN,life_live[index].id,life_live[index].birth_serial)||!LifeAppend(record))
                BirthFailure("round shutdown endpoint cannot be encoded");
        }
        life_live[index]=life_live[--life_live_count];
    }
    static bool LifeDefinition(CExplosionData *data,LifeRecord &record) {
        if(!ExplosionDefinition(data,record.definition,record.definition_schema,record.definition_name)) return false;
        if(strcmp(record.definition_name,"Mech Pulse Hit Medium")&&strcmp(record.definition_name,"Mech Pulse Hit Large"))
            return Fail("compact lifecycle admits selected pulse definitions only");
        return (data->mRadius>0.f&&data->mRadius<=3.f&&data->mDamage>0.f&&data->mVolumetric<=1)||Fail("compact pulse definition shape is unsupported");
    }
    static bool LifeEntry(CExplosion *explosion,CExplosionInitThing *init) {
        if(life_failed||life_pending||life_count==kLifeOps||life_live_count==kEntities) {
            BirthFailure("compact lifecycle pending, nested or capacity refusal"); return false;
        }
        LifeRecord r={};
        if(!LifeDefinition(init->mBehaviour,r)||init->mAttachedTo||init->mUseAttachedRadius||init->mAllowVolumetric) {
            BirthFailure("compact lifecycle requires selected pulse with no attachment or volumetric context"); return false;
        }
        uint32_t originator=Id(init->mOriginator);
        if(init->mOriginator) {
            unsigned key=0; while(key<life_key_count&&life_keys[key].id!=originator) ++key;
            if(key==life_key_count) { BirthFailure("compact birth originator is not a baseline engine dependency"); return false; }
            r.dependency=life_keys[key];
        }
        life_pending=explosion; life_pending_record=r; return true;
    }
    static void LifeComplete(CExplosion *explosion) {
        if(!life_enabled||!active) return;
        if(config.role==CE_WORLD_REPLICA) {
            if(life_creating&&applying_birth&&birth_spawn==explosion&&life_pending==explosion) { life_initialized=true; life_pending=NULL; }
            else { poisoned=true; Fail("compact replica Init completion has no live creation token"); }
            return;
        }
        if(life_failed||life_pending!=explosion) { BirthFailure("compact Init completion token was invalidated"); return; }
        Binding *b=ByThing(explosion); LifeRecord r=life_pending_record;
        if(!b||!b->birth.ready) { BirthFailure("compact birth did not reach native ready seam"); return; }
        r.birth=b->birth; r.birth_serial=b->birth.serial;
        if(!CoreCapture(explosion,r.core)||!ExplosionCapture(explosion,r.explosion)||
           !LifeSourceHeader(r,CE_WORLD_LIFECYCLE_BIRTH,b->id,b->birth.serial)||!LifeAppend(r)) {
            BirthFailure("compact completed birth cannot be encoded"); return;
        }
        life_live[life_live_count++]={explosion,b->id,b->birth.serial,EXPLOSION_THING}; life_pending=NULL;
    }
    static void LifeForget(const void *pointer,bool destruction) {
        if(!life_enabled||!active) return;
        if(round_pending==pointer) { round_pending=NULL; if(config.role==CE_WORLD_REPLICA) poisoned=true; BirthFailure("round destroyed before Init completion"); return; }
        if(life_pending==pointer) { life_pending=NULL; if(config.role==CE_WORLD_REPLICA) poisoned=true; BirthFailure("compact object destroyed before Init completion"); return; }
        if(applying) return;
        unsigned index=0; while(index<life_live_count&&life_live[index].thing!=pointer) ++index;
        if(index==life_live_count) return;
        if(config.role!=CE_WORLD_HOST) { poisoned=true; Fail("uncontrolled replica lifecycle destruction"); return; }
        if(GAME.mQuit==QT_NONE) {
            LifeRecord r={};
            if(destruction||life_live[index].kind!=EXPLOSION_THING||!LifeSourceHeader(r,CE_WORLD_LIFECYCLE_DESPAWN,life_live[index].id,life_live[index].birth_serial)||!LifeAppend(r))
                BirthFailure("compact destruction was not an ordered native Shutdown");
        }
        life_live[index]=life_live[--life_live_count];
    }
    static void ExplosionBirthEntry(CExplosion *explosion,CInitThing *base) {
        if(!active) return;
        if(config.role==CE_WORLD_REPLICA) {
            if(!applying_birth||birth_spawn!=explosion) { poisoned=true; Fail("uncontrolled native replica explosion Init"); }
            return;
        }
        if(birth_failed) return;
        CExplosionInitThing *init=(CExplosionInitThing *)base;
        if(life_enabled&&!LifeEntry(explosion,init)) return;
        ExplosionBirth b={};
        b.tick=EVENT_MANAGER.GetFrameCount(); b.pos=GetVec(init->mPos); b.deep=!(MAP.GetWaterLevel()+1.5f>init->mPos.Z);
        b.orientation={{1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f}};
        b.originator=Id(init->mOriginator);
        if((init->mOriginator&&!b.originator)||!VecBound(b.pos)||birth_generated==0xffffffffu) {
            BirthFailure("explosion birth identity or position is unsupported"); return;
        }
        if(!life_enabled&&birth_count==kBirths) { BirthFailure("explosion birth journal capacity exhausted; session requires teardown"); return; }
        if(!b.deep) {
            if(!ExplosionDefinition(init->mBehaviour,b.definition,b.schema,b.name)) { birth_failed=true; return; }
            b.attached=Id(init->mAttachedTo); b.use_radius=!!init->mUseAttachedRadius; b.volumetric=!!init->mAllowVolumetric; b.important=!!init->mImportant;
            b.allegiance=init->mAllegiance; b.collision=init->mColType; b.orientation_type=init->mOrientationType;
            if(b.orientation_type==EULER_ANGLES) b.euler={{init->mYaw,init->mPitch,init->mRoll}};
            else if(b.orientation_type==DIRECTION_COSINE_MATRIX) b.orientation=GetMat(init->mOrientation);
            CInitCSThing &c=init->mInitCST;
            if((init->mAttachedTo&&!b.attached)||init->mName[0]||init->mScript[0]||init->mSpawnScript[0]||!init->mActive||init->mForceRadius!=-1.f||
                c.mShape||c.mUseFixedMeshTransforms||c.mIgnoreAnimationCollision||c.mRoundLength!=0.f||
                (b.volumetric&&b.attached&&init->mBehaviour->mVolumetric>1)) {
                BirthFailure("explosion birth needs unsupported script, shape or volumetric context"); return;
            }
        } else b.ready=1;
        b.serial=birth_generated+1;
        Binding *binding=Bind(explosion,next_id++,0);
        if(!binding||!next_id) { BirthFailure("explosion identity capacity exhausted"); return; }
        b.id=binding->id; binding->birth=b; if(!life_enabled) birth_journal[birth_count++]=b; birth_generated=b.serial;
    }
    static void ExplosionReady(CExplosion *explosion,CInitThing *base) {
        if(!active) return;
        CExplosionInitThing *init=(CExplosionInitThing *)base;
        if(config.role==CE_WORLD_REPLICA) {
            if(!applying_birth||birth_spawn!=explosion||applying_birth->deep) { poisoned=true; Fail("unexpected native replica explosion initialization"); return; }
            explosion->mRadius=applying_birth->radius; explosion->mCurrentRadius=explosion->mRadius<=3.f?explosion->mRadius:0.f;
            explosion->mFlags=(short)((explosion->mFlags&~TF_IN_MAP_WHO)|(applying_birth->map_who?TF_IN_MAP_WHO:0));
            init->mInitCST.mNotSeekCollisionWithBF=applying_birth->mask;
            return;
        }
        Binding *binding=ByThing(explosion);
        if(!binding||binding->birth.deep||birth_failed) { BirthFailure("explosion normal-ready seam has no admitted birth"); return; }
        ExplosionBirth &b=binding->birth; b.radius=explosion->mRadius; b.map_who=!!(explosion->mFlags&TF_IN_MAP_WHO);
        b.mask=init->mInitCST.mNotSeekCollisionWithBF; b.desired=init->mInitCST.mDesiredCollision; b.min_collision=init->mInitCST.mMinCollision; b.ready=1;
        if(!BirthBound(b,EVENT_MANAGER.GetFrameCount())||(b.radius>3.f&&init->mBehaviour->mDamage>0.f&&init->mBehaviour->mTime<=0)) {
            BirthFailure("explosion normal-ready state exceeds native bounds"); return;
        }
        if(life_enabled) return;
        for(unsigned i=0;i<birth_count;++i) if(birth_journal[i].serial==b.serial) { birth_journal[i]=b; return; }
        BirthFailure("explosion journal lost its pending normal birth");
    }
    static bool ExplosionCapture(CExplosion *explosion,ExplosionState &out) {
        Binding *binding=ByThing(explosion); if(!binding||!binding->birth.ready) return Fail("live explosion has no complete birth history");
        out.serial=binding->birth.serial; out.definition=binding->birth.definition; out.schema=binding->birth.schema; out.deep=binding->birth.deep;
        out.originator=Id(explosion->mOriginator.ToRead());
        if(explosion->mOriginator.ToRead()&&!out.originator) return Fail("explosion originator is outside the admitted world");
        if(!out.deep) { out.allegiance=explosion->mAllegiance; out.radius=explosion->mRadius; out.current_radius=explosion->mCurrentRadius; }
        return true;
    }
    static bool UnitCapture(CUnit *unit,UnitPresentation &out,const Core &core) {
        if(!unit->mUnitData||!Name(out.definition,unit->mUnitData->mName)||unit->mSpawners.Size()||unit->mChildren.Size()||
            !unit->mAI||unit->mAI->mForUnit!=unit||(unit->mGuide&&unit->mGuide->mUnit!=unit)||
            unit->mThrusters.Size()||unit->mUnknown1B4.Size()||unit->mUnknown1C4.Size()||(unit->mUnknown1AC&&unit->mUnknown1AC->Full()))
            return Fail("unit definition or spawned-child topology is unsupported");
        if(!RendererSchema(unit,out.mesh_schema,core.animation)) return false;
        out.life=unit->mLife; out.energy=unit->mEnergy; out.shields=unit->mShields; out.max_shields=unit->mMaxShields;
        out.initial_life=unit->mUnitData->mUnknownC0; out.behavior=unit->mUnitData->mBehaviour; out.icon=unit->mUnitData->mIcon; out.fenrir=!!unit->mUnitData->mFenrir;
        out.allegiance=unit->mAllegiance; out.active=!!unit->mActive; out.vulnerable=!!unit->mVulnerable;
        out.hit=unit->mUnknown218; out.hit_decay=unit->mUnknown21C;
        out.euler={{unit->mCurrentOrientation.mYaw,unit->mCurrentOrientation.mPitch,unit->mCurrentOrientation.mRoll}};
        out.turret[0]=unit->mUnknownE0; out.turret[1]=unit->mUnknownE4; out.turret[2]=unit->mUnknownE8;
        out.turret[3]=unit->mUnknownEC; out.turret[4]=unit->mUnknownF0; out.turret[5]=unit->mUnknownF4;
        return true;
    }
    static bool CannonCapture(CCannon *cannon,CannonState &out,const Core &core) {
        CMCCannon *mc=dynamic_cast<CMCCannon *>(cannon->mMotionController);
        if(!mc||!PortTypeInfoEqual(typeid(*mc),typeid(CMCCannon))||mc->mThing!=static_cast<IRenderableThing *>(cannon)||cannon->mSegments||!cannon->mGuide||!PortTypeInfoEqual(typeid(*cannon->mGuide),typeid(CTerrainGuide))||!cannon->mAI||!PortTypeInfoEqual(typeid(*cannon->mAI),typeid(CUnitAI))||
            !cannon->mRenderThing||!PortTypeInfoEqual(typeid(*cannon->mRenderThing),typeid(CRTMesh))) return Fail("unsupported loaded cannon controller or segment topology");
        if(!UnitCapture(cannon,out.unit,core)) return false;
        out.drop=cannon->mUnknown250; out.old_drop=cannon->mUnknown254; out.state=cannon->mState; out.above_water=!!cannon->mAboveWater; return true;
    }
    static bool FeatureCapture(CFeature *feature,FeatureState &out,const Core &core) {
        CFeatureData *data=feature->mData;
        if(feature->mMotionController||!data||!feature->mRenderThing||!PortTypeInfoEqual(typeid(*feature->mRenderThing),typeid(CRTMesh))||!Name(out.definition,data->mName)||!Name(out.sound,data->mSoundName)) return Fail("unsupported feature definition or controller");
        if(!RendererSchema(feature,out.mesh_schema,core.animation)) return false;
        out.life=feature->mLife; out.above_water=!!feature->mAboveWater; out.allegiance=feature->mAllegiance;
        out.initial_life=data->mLife; out.invincible=!!data->mInvincible; out.lockable=!!data->mUnknown14; out.explosion=data->mExplosion; out.icon=data->mIcon; return true;
    }
    static unsigned SegmentId(CDestroyableSegment *node,CDestroyableSegment **nodes,unsigned count) {
        for(unsigned i=0;i<count;++i) if(nodes[i]==node) return i+1;
        return 0;
    }
    static bool SegmentCapture(CBuilding *building,SegmentGraph &out,CDestroyableSegment **nodes=NULL) {
        CDestructableSegmentsController *controller=building->mSegments;
        CDestructableSegmentsMotionController *mc=dynamic_cast<CDestructableSegmentsMotionController *>(building->mMotionController);
        CMesh *mesh=building->GetRTMesh(); CDestroyableSegment *storage[kParts]={}; if(!nodes) nodes=storage;
        if(!controller||!mc||!PortTypeInfoEqual(typeid(*mc),typeid(CDestructableSegmentsMotionController))||mc->mSegments!=controller||
            mc->mThing!=static_cast<IRenderableThing *>(building)||controller->mOwner!=building||!mesh||!controller->mSegments||
            controller->mNumSegments<1||controller->mNumSegments>(int)kParts||controller->mNumSegments!=mesh->mNumParts)
            return Fail("unsupported loaded building segment controller");
        out.slots=controller->mNumSegments;
        for(unsigned slot=0;slot<out.slots;++slot) {
            CDestroyableSegment *node=controller->mSegments[slot]; if(!node) continue;
            unsigned id=SegmentId(node,nodes,out.count);
            if(!id) { if(out.count==kParts) return Fail("too many unique building segments"); nodes[out.count++]=node; id=out.count; }
            out.alias[slot]=id;
        }
        out.core=SegmentId(controller->mCore,nodes,out.count);
        if(!out.core) return Fail("building core is outside its loaded segment slots");
        MeshSchema hash; hash.Word(out.slots); hash.Word(out.count); hash.Word(out.core);
        for(unsigned slot=0;slot<out.slots;++slot) hash.Word(out.alias[slot]);
        for(unsigned i=0;i<out.count;++i) {
            CDestroyableSegment *node=nodes[i]; SegmentState &state=out.nodes[i];
            if(node->mController!=controller||!WORLD.mOwnedF0.Contains(node)||node->mPart<0||node->mPart>=(int)out.slots||
                node->mChildren.Size()<0||node->mChildren.Size()>(int)out.count) return Fail("building segment owner or topology differs");
            if(PortTypeInfoEqual(typeid(*node),typeid(CDestroyableCoreSegment))) state.type=DST_CORE;
            else if(PortTypeInfoEqual(typeid(*node),typeid(CDestroyableSwapSegment))) state.type=DST_SWAP;
            else if(PortTypeInfoEqual(typeid(*node),typeid(CDestroyableExtraSegment))) state.type=DST_EXTRA;
            else return Fail("unsupported building segment subclass");
            state.parent=SegmentId(node->mParent,nodes,out.count); state.part=node->mPart;
            if(node->mParent&&!state.parent) return Fail("building segment parent is outside its loaded graph");
            hash.Word(state.type); hash.Word(state.parent); hash.Word(state.part); hash.Word(node->mChildren.Size());
            bool children[kParts]={}; ListIterator<CDestroyableSegment> it(&node->mChildren);
            for(CDestroyableSegment *child=it.First();child;child=it.Next()) {
                unsigned id=SegmentId(child,nodes,out.count);
                if(!id||children[id-1]||child->mParent!=node) return Fail("building child aliases or backlinks differ");
                children[id-1]=true; hash.Word(id);
            }
            if(node->mParent&&!node->mParent->mChildren.Contains(node)) return Fail("building parent misses its child");
            state.health=node->mHealth; state.max_health=node->mMaxHealth; state.last_damage_time=node->mLastDamageTime;
            state.last_damage_amount=node->mLastDamageAmount; state.size=node->mSize;
            state.vulnerable=!!node->mVulnerable; state.destroyed=!!node->mDestroyed;
            if(!TimeBound(state.last_damage_time)||!TimeBound(state.last_damage_amount)||!TimeBound(EVENT_MANAGER.GetTime())||building->mUnknown218< -100||building->mUnknown218>100)
                return Fail("segment hit presentation exceeds native conversion bounds");
            state.hit=SegmentHit(EVENT_MANAGER.GetTime(),building->mUnknown218,state.last_damage_time,state.last_damage_amount);
            if(state.type==DST_CORE) {
                CDestroyableCoreSegment *core=(CDestroyableCoreSegment *)node;
                state.number=core->mNumber; state.total_time=core->mTotalTime; state.time=core->mTime; state.cascading=!!core->mCascading;
            } else if(state.type==DST_SWAP) {
                CDestroyableSwapSegment *swap=(CDestroyableSwapSegment *)node;
                state.stages=swap->mNumStages; state.swapped=!!swap->mSwapped;
            } else {
                CDestroyableExtraSegment *extra=(CDestroyableExtraSegment *)node;
                state.has_direction=!!extra->mHasDirection; state.direction=ExtraDirection(state.has_direction,&extra->mDirection);
            }
            hash.Word(state.number); hash.Word(state.stages);
        }
        out.schema=hash.value; out.core_count=controller->mUnknown20; out.changed=!!controller->mChanged;
        out.primary_visible=!!controller->mUnknown24; out.pickup_mode=controller->mUnknown28; out.no_imposter=controller->mUnknown2C;
        out.fall_threshold=controller->mUnknown14; out.total_health=controller->mTotalHealth; out.total_size=controller->mTotalSize;
        if(!SegmentBound(out)) return Fail("loaded segment graph exceeds native consumer bounds");
        for(unsigned slot=0;slot<out.slots;++slot) if(out.alias[slot]&&!mesh->mParts[slot]->mIsChained) {
            const SegmentState &state=out.nodes[out.alias[slot]-1];
            if(state.type==DST_SWAP&&state.stages>(uint32_t)mesh->mParts[slot]->mNumChainedParts) return Fail("swap stage exceeds its authored damage chain");
        }
        return true;
    }
    static bool BuildingCapture(CBuilding *building,BuildingState &out,const Core &core) {
        if(!building->mAI||!PortTypeInfoEqual(typeid(*building->mAI),typeid(CRepairPadAI))||building->mGuide||!building->mRenderThing||!PortTypeInfoEqual(typeid(*building->mRenderThing),typeid(CRTBuilding))||!UnitCapture(building,out.unit,core)||
            !SegmentCapture(building,out.graph)) return Fail("unsupported loaded building presentation graph");
        out.door=building->mUnknown254; out.door_time=building->mUnknown258; out.close_time=building->mUnknown25C;
        out.can_shut=!!building->mUnknown260; out.shut=building->mUnknown264; out.last_hit=building->mUnknown268; return true;
    }
    static bool CaptureWeapon(CWeapon *w, WeaponState &out) {
        if (!w->mData || !Name(out.name, w->mData->mName) || !Name(out.mode, w->mMode ? w->mMode->mName : "")) return false;
        // Battle-engine part inventories own their weapons separately from CUnit::mWeapons. Their init uses a
        // default emitter and does not initialize every CInitEquipment member; do not read those unrelated bytes.
        out.emitter = 0; out.mount = 0; out.target = Id(w->mTarget.ToRead());
        if (w->mTarget.ToRead() && !out.target) return Fail("weapon target outside admitted world");
        out.active = !!w->mActive; out.charge = w->mCharge; out.next_fire = w->mNextFireTime;
        out.burst = w->mBurstCount; out.charged = w->mUnknown68; out.launch = w->mUnknown70; out.angle = w->mUnknown74; return true;
    }
    static unsigned Weapons(CBattleEngine *e,CWeapon **out) {
        unsigned count=0;
        CWeapon *special[]={e->mWalkerPart->mPrimaryWeapon,e->mWalkerPart->mAugWeapon};
        for(CWeapon *w:special) if(w) { if(count==kWeapons) return kWeapons+1; out[count++]=w; }
        SPtrSet<CWeapon> *lists[]={&e->mWalkerPart->mWeapons,&e->mJetPart->mWeapons,&e->mWeapons};
        for(SPtrSet<CWeapon> *list:lists) { ListIterator<CWeapon> it(list);
            for(CWeapon *w=it.First();w;w=it.Next()) { if(count==kWeapons) return kWeapons+1; out[count++]=w; }
        } return count;
    }
    static bool CaptureLocks(SPtrSet<CLockInfo> &list, LockState *out, uint32_t &count) {
        ListIterator<CLockInfo> it(&list); count = 0;
        for (CLockInfo *lock = it.First(); lock; lock = it.Next()) {
            if (count >= kLocks) return Fail("lock list exceeds adapter capacity");
            LockState &s = out[count++]; s.target = Id(lock->mUnit.ToRead());
            if (lock->mUnit.ToRead() && !s.target) return Fail("lock target outside admitted world");
            s.start = lock->mStart; s.finish = lock->mFinish; s.locked = !!lock->mLocked; s.direct = !!lock->mDirectLock;
        } return true;
    }
    static bool EngineCapture(CBattleEngine *e, EngineState &out, Core &core) {
        CPlayer *player = e->GetPlayer();
        if (!player || player->GetNumber() < 1 || player->GetNumber() > 2 || player->GetBattleEngine() != e || !e->mWalkerPart || !e->mJetPart)
            return Fail("engine is not bound to a genuine admitted player");
        core.player = player->GetNumber();
        if(!CockpitCapture(e,out.cockpit)) return false;
        out.configuration = e->mConfigurationId; out.allegiance = e->mAllegiance;
        out.life = e->mLife; out.energy = e->mEnergy; out.shields = e->mShields; out.max_shields = e->mMaxShields; out.hit=e->mUnknown218; out.hit_decay=e->mUnknown21C;
        out.euler = {{e->mCurrentOrientation.mYaw, e->mCurrentOrientation.mPitch, e->mCurrentOrientation.mRoll}};
        out.old_euler = {{e->mOldEulerAngles.mYaw, e->mOldEulerAngles.mPitch, e->mOldEulerAngles.mRoll}};
#define CAPTURE_FLOAT(name) out.name = e->name;
        ENGINE_FLOATS(CAPTURE_FLOAT)
#undef CAPTURE_FLOAT
#define CAPTURE_INT(name) out.name = e->name;
        ENGINE_INTS(CAPTURE_INT)
#undef CAPTURE_INT
        // Tracking offsets have no initialized value until tracking activates. They are not rendered while inactive.
        if(e->mTrackingActive) { out.tracking[0]=e->mTrackingYawOffset; out.tracking[1]=e->mTrackingPitchOffset;
            out.tracking[2]=e->mOldTrackingYawOffset; out.tracking[3]=e->mOldTrackingPitchOffset; }
        for (unsigned i = 0; i < 6; ++i) { out.stores[i] = e->mStoreValue[i]; out.store_heat[i] = !!e->mStoreHeat[i]; out.store_overheat[i] = !!e->mStoreOverheat[i]; }
        out.crosshair = Id(e->mCurrentUnitOverCrosshair.ToRead()); out.crosshair_range = Id(e->mCurrentUnitOverCrosshairRegardlessOfRange.ToRead());
        out.auto_aim = Id(e->mAutoAimTarget.ToRead()); out.standing_on = Id(e->mStandingOnThing.ToRead());
        if ((e->mCurrentUnitOverCrosshair.ToRead() && !out.crosshair) || (e->mCurrentUnitOverCrosshairRegardlessOfRange.ToRead() && !out.crosshair_range) ||
            (e->mAutoAimTarget.ToRead() && !out.auto_aim) || (e->mStandingOnThing.ToRead() && !out.standing_on)) return Fail("engine reference outside admitted world");
        out.walker_weapon = e->mWalkerPart->mCurrentWeapon; out.jet_weapon = e->mJetPart->mCurrentWeapon;
        out.walk_cycle = e->mWalkerPart->mWalkCycle; out.old_walk_cycle = e->mWalkerPart->mOldWalkCycle; out.jet_thrust = e->mJetPart->mThrusterValue;
        CWeapon *weapons[kWeapons]; out.weapon_count=Weapons(e,weapons);
        if(out.weapon_count>kWeapons) return Fail("unsupported engine weapon inventory");
        for(unsigned i=0;i<out.weapon_count;++i) if(!CaptureWeapon(weapons[i],out.weapons[i])) return false;
        if (!CaptureLocks(e->mLocks, out.locks, out.lock_count) || !CaptureLocks(e->mFiredLocks, out.fired, out.fired_count)) return false;
        CMCBattleEngine *mc = Mech(e);
        if (!mc || !mc->mMesh || !mc->mPartPos || !mc->mOldPartPos || !mc->mPartOri || !mc->mOldPartOri || !mc->mPartFrame || !mc->mOldPartFrame ||
            mc->mMesh->mNumParts < 1 || mc->mMesh->mNumParts > (int)kParts) return Fail("unsupported or unprepared engine pose controller");
        out.part_count = mc->mMesh->mNumParts;
        for (unsigned i = 0; i < out.part_count; ++i) {
            if(mc->mPartFrame[i]<0.f) return Fail("authoritative pose has an unprepared part");
            Pose &p = out.parts[i]; p.pos = GetVec(mc->mPartPos[i]); p.old_pos = GetVec(mc->mOldPartPos[i]);
            p.orientation = GetMat(mc->mPartOri[i]); p.frame = mc->mPartFrame[i]; p.old_frame = mc->mOldPartFrame[i];
            p.old_orientation = p.old_frame==-1.f ? p.orientation : GetMat(mc->mOldPartOri[i]);
        }
        out.view = player->mCurrentViewMode; out.preferred_view = player->mPreferedControlView;
        for (unsigned i = 0; i < PS_NUM_PLAYERSTATS; ++i) out.stats[i] = player->mStat[i];
        CCamera *camera=GAME.GetCamera(core.player-1); if(!camera) return Fail("player camera is missing");
        out.camera.pos=GetVec(camera->GetPos()); out.camera.old_pos=GetVec(camera->GetOldPos());
        out.camera.orientation=GetMat(camera->GetOrientation()); out.camera.old_orientation=GetMat(camera->GetOldOrientation());
        out.camera.zoom=camera->GetZoom(); out.camera.old_zoom=camera->GetOldZoom(); out.camera.hud=camera->GetShowHUD();
        CRadarWarningReceiver *radar=e->mCachedRadarWarningReceiver;
        out.radar_present=radar!=NULL;
        if(radar) {
            out.new_threat=!!radar->mNewThreat; out.new_threat_time=radar->mNewThreatTime;
            ListIterator<SRadarThreat> threats(&radar->mThreats);
            for(SRadarThreat *t=threats.First();t;t=threats.Next()) {
                if(out.threat_count==kThreats) return Fail("radar threats exceed adapter capacity");
                ThreatState &s=out.threats[out.threat_count++]; s.round=Id(t->mRound.ToRead());
                if(t->mRound.ToRead()&&!s.round) return Fail("radar threat outside admitted world");
                s.seen=!!t->mSeen; s.yaw=t->mYaw; s.distance=t->mDistance; s.time=t->mTime;
            }
        }
        ListIterator<CDamageFlash> flashes(&e->mDamageFlashes);
        for(CDamageFlash *flash=flashes.First();flash;flash=flashes.Next()) {
            if(out.flash_count==kFlashes) return Fail("damage flashes exceed adapter capacity");
            out.flashes[out.flash_count++]={flash->mYaw,flash->mStartTime};
        }
        return true;
    }
    static bool RoundCapture(CRound *round, RoundState &out) {
        if (!round->mData || !Name(out.definition, round->mData->mName)) return false;
        Binding *binding=ByThing(round); out.incarnation=binding?binding->round_incarnation:0; out.schema=binding?binding->round_schema:0;
        if(life_enabled&&(!out.incarnation||!out.schema)) return Fail("round has no complete lifecycle incarnation");
        if(life_enabled) { uint64_t schema=0; if(!RoundDefinition(round->mData,schema)||schema!=out.schema) return Fail("live round definition changed after its birth"); }

        out.target = Id(round->mUnknownE8.ToRead()); out.launcher = Id(round->mUnknownEC.ToRead());
        if ((round->mUnknownE8.ToRead() && !out.target) || (round->mUnknownEC.ToRead() && !out.launcher)) return Fail("round reference outside admitted world");
        out.allegiance = round->mAllegiance; out.jumps = round->mJumpsPerformed; out.start = round->mStartTime;
        out.beam_start = BeamStart(round->mData->mBeam,&round->mBeamStart); out.destination = GetVec(round->mDest);
        out.incoming_member=WORLD.mSet110.Contains(round);
        out.armed = round->mUnknown120; out.special = round->mUnknown128; out.no_gravity = round->mUnknown12C; return true;
    }
    static bool Capture(CE_WorldFrame &out) {
        if (!active || config.role != CE_WORLD_HOST) return Fail("capture requires an admitted host");
        if(birth_failed||life_failed||life_pending||round_pending||shot_active) return Fail("lifecycle history failed or native initialization is pending; session requires teardown");
        memset(&out, 0, sizeof out); out.epoch = config.epoch; out.tick = EVENT_MANAGER.GetFrameCount(); out.level = GAME.GetCurrentLevel();
        out.lifecycle_baseline=life_enabled?life_baseline:0; out.lifecycle_watermark=life_enabled?life_generated:0;
        out.sequence=++captured_sequence; out.consumed_sequence=commands.consumed; out.birth_count=birth_count; memcpy(out.births,birth_journal,birth_count*sizeof *birth_journal);
        out.phase = GAME.mGameState; out.paused = !!GAME.mPause; out.score = GAME.mScore; out.lives[0] = GAME.mPlayer1Lives; out.lives[1] = GAME.mPlayer2Lives;
        for(unsigned i=0;i<NUM_WORLD_COUNTERS;++i) { out.counters_a[i]=WORLD.mCountersA[i]; out.counters_b[i]=WORLD.mCountersB[i]; }
        CaptureObjectives(out);
        if (!ObjectivesBound(out)) return Fail("unsupported native mission objective state");
        out.end_count=GAME.mEndLevelCount; out.fade=!!GAME.mFadeOut; out.alpha=GAME.mWinLoseScreenAlpha;
        if (out.level != config.level || !out.tick || !out.sequence || out.tick < captured_tick) return Fail("host crossed an unadmitted level or tick barrier");
        ListIterator<CThing> it(&WORLD.GetThingNB());
        for (CThing *t = it.First(); t; t = it.Next()) {
            uint32_t kind = ThingKind(t); if (!kind) return FailKind("unsupported world kind", t->_GetClassName());
            if(!ByThing(t)&&kind!=ROUND_THING) return FailKind("new non-projectile kind is not admitted",t->_GetClassName());
            if (!ByThing(t) && (!next_id || !Bind(t, next_id++, 0))) return Fail("entity identity capacity exhausted");
        }
        for (CThing *t = it.First(); t; t = it.Next()) {
            if (out.count >= kEntities) return Fail("world exceeds adapter capacity");
            Entity &entity = out.entities[out.count++]; if (!CoreCapture(t, entity.core)) return Fail("cannot capture typed core state");
            if (entity.core.kind == ENGINE_THING && !EngineCapture((CBattleEngine *)t, entity.engine, entity.core)) return false;
            if (entity.core.kind == ROUND_THING && !RoundCapture((CRound *)t, entity.round)) return false;
            if (entity.core.kind == CANNON_THING && !CannonCapture((CCannon *)t, entity.cannon, entity.core)) return false;
            if (entity.core.kind == FEATURE_THING && !FeatureCapture((CFeature *)t, entity.feature, entity.core)) return false;
            if (entity.core.kind == BUILDING_THING && !BuildingCapture((CBuilding *)t, entity.building, entity.core)) return false;
            if (entity.core.kind == EXPLOSION_THING && !ExplosionCapture((CExplosion *)t,entity.explosion)) return false;
        }
        // The pointer/list order is never a wire identity or ordering contract.
        for (unsigned i = 1; i < out.count; ++i) for (unsigned j = i; j && out.entities[j - 1].core.id > out.entities[j].core.id; --j) {
            Entity tmp = out.entities[j]; out.entities[j] = out.entities[j - 1]; out.entities[j - 1] = tmp;
        }
        captured_tick = out.tick; return true;
    }
    static bool Begin(const CE_WorldConfig &in) {
        if(active) return Fail("a live world must leave its level before another network barrier");
        CE_WorldEnd();
        if ((in.role != CE_WORLD_HOST && in.role != CE_WORLD_REPLICA) || !in.epoch || in.local_player > 1 || in.level != 852 ||
            (in.role == CE_WORLD_HOST ? in.local_player != 0 : in.local_player != 1) || GAME.GetCurrentLevel() != (int)in.level || GAME.GetNPlayers() != 2 ||
            GAME.GetGameState()<GAME_STATE_PRE_RUNNING || GAME.GetGameState()>GAME_STATE_PLAYING)
            return Fail("first world adapter requires level 852 and canonical host/guest player slots");
        for (unsigned i = 0; i < 2; ++i) {
            CPlayer *p = GAME.GetPlayer(i);
            if (!p || !p->GetBattleEngine() || p->GetNumber() != (int)i + 1 || p->GetBattleEngine()->GetPlayer() != p || !GAME.mController[i] ||
                GAME.mController[i]->GetToControl()!=p || GAME.mController[i]->GetPadNumber()!=(int)i || GAME.IsFreeCameraOn(i))
                return Fail("two genuine loaded player/controller/engine bindings are required");
        }
        for(unsigned i=0;i<2;++i) {
            CController *c=GAME.mController[i];
            if(!dynamic_cast<CPCController *>(c)||c->mPlaying||c->mRecording) return Fail("network input requires genuine non-playback PC controllers");
        }
        for(unsigned i=0;i<sizeof native_actions/sizeof *native_actions;++i)
            if(CONTROL_BINDINGS[i].mId!=(int)native_actions[i]) return Fail("native input mapping order differs from the admitted protocol");
        for(unsigned i=0;i<2;++i) {
            CockpitState topology={}; if(!CockpitTopology(GAME.GetPlayer(i)->GetBattleEngine(),topology)) return false;
        }
        config = in; next_id = 1;
        ListIterator<CThing> it(&WORLD.GetThingNB());
        for (CThing *t = it.First(); t; t = it.Next()) {
            uint32_t kind = ThingKind(t);
            if (!kind || kind == ROUND_THING || kind == EXPLOSION_THING) { CE_WorldEnd(); return FailKind("unsupported baseline kind", t->_GetClassName()); }
            if (!Bind(t, next_id++, (uint32_t)(unsigned short)t->GetThingNumber() + 1)) { CE_WorldEnd(); return Fail("baseline identity capacity exhausted"); }
        }
        for(CThing *t=it.First();t;t=it.Next()) if(ThingKind(t)==CANNON_THING||ThingKind(t)==FEATURE_THING||ThingKind(t)==BUILDING_THING) {
            Entity state={};
            if(!CoreCapture(t,state.core)||(state.core.kind==CANNON_THING&&!CannonCapture((CCannon *)t,state.cannon,state.core))||
                (state.core.kind==FEATURE_THING&&!FeatureCapture((CFeature *)t,state.feature,state.core))||
                (state.core.kind==BUILDING_THING&&!BuildingCapture((CBuilding *)t,state.building,state.core))) {
                char reason[sizeof error_text]; memcpy(reason,error_text,sizeof reason); CE_WorldEnd(); memcpy(error_text,reason,sizeof reason); return false;
            }
        }
        if(in.role==CE_WORLD_REPLICA) for(unsigned i=0;i<2;++i) {
            CBattleEngine *e=GAME.GetPlayer(i)->GetBattleEngine(); CMCBattleEngine *mc=Mech(e);
            CRenderThing *walker=e->mHackRenderThingIsWalker?e->mRenderThing:e->mOtherMeshHackRenderThing;
            CMesh *mesh=walker?walker->GetRTMesh():NULL;
            if(!mc||!mesh||mesh->mNumParts<1||mesh->mNumParts>(int)kParts||mesh->GetAnimModeByName("LegMotion")==AM_INVALID||!mesh->GetEmitterSubpart("Footbase",1)) {
                CE_WorldEnd(); return Fail("replica has no supported genuine walker pose topology");
            }
            // A replica skips PreRun and never processes movement. Initialize only the native immutable mesh
            // topology/arrays now; the first authoritative frame supplies every pose before it can render.
            if(!mc->mMesh) mc->Init(mesh);
        }
        active = true; return true;
    }
    static bool References(const CE_WorldFrame &f, uint32_t id, bool unit = false) {
        if (!id) return true;
        for (unsigned i = 0; i < f.count; ++i) if (f.entities[i].core.id == id) return !unit || UnitKind(f.entities[i].core.kind);
        return false;
    }
    static bool EngineReference(const CE_WorldFrame &f,uint32_t id) {
        for(unsigned i=0;i<f.count;++i) if(f.entities[i].core.id==id) return f.entities[i].core.kind==ENGINE_THING;
        return false;
    }
    static bool UnitBound(const UnitPresentation &u) {
        if(!u.definition[0]||!u.mesh_schema||u.allegiance>6||u.active>1||u.vulnerable>1||u.fenrir>1||
            !TimeBound(u.life)||!TimeBound(u.energy)||!TimeBound(u.shields)||!TimeBound(u.max_shields)||!TimeBound(u.initial_life)||
            !VecBound(u.euler,64.f)||u.hit< -100||u.hit>100||u.hit_decay!=10) return false;
        for(float angle:u.turret) if(!AngleBound(angle)) return false;
        return true;
    }
    static bool SegmentBound(const SegmentGraph &g) {
        if(!g.schema||!g.count||g.count>kParts||!g.slots||g.slots>kParts||!g.core||g.core>g.count||g.core_count<1||g.core_count>g.count||
            g.changed>1||g.primary_visible>1||g.pickup_mode>1||g.no_imposter>1||!TimeBound(g.fall_threshold)||!TimeBound(g.total_health)||!TimeBound(g.total_size)) return false;
        unsigned cores=0; bool seen[kParts]={};
        for(unsigned slot=0;slot<g.slots;++slot) { if(g.alias[slot]>g.count) return false; if(g.alias[slot]) seen[g.alias[slot]-1]=true; }
        for(unsigned i=0;i<g.count;++i) {
            const SegmentState &n=g.nodes[i];
            if(!seen[i]||n.type>DST_EXTRA||n.parent>g.count||n.parent==i+1||n.part>=g.slots||g.alias[n.part]!=i+1||
                n.vulnerable>1||n.destroyed>1||n.cascading>1||n.swapped>1||n.has_direction>1||n.hit>100||
                !TimeBound(n.health)||!TimeBound(n.max_health)||!TimeBound(n.last_damage_time)||!TimeBound(n.last_damage_amount)||!TimeBound(n.size)||
                !TimeBound(n.total_time)||!TimeBound(n.time)||!VecBound(n.direction)) return false;
            unsigned walk=i+1,depth=0;
            while(g.nodes[walk-1].parent) { walk=g.nodes[walk-1].parent; if(walk>g.count||++depth>=g.count) return false; }
            if(walk!=g.core) return false;
            if(n.type==DST_CORE) {
                ++cores; if(!n.number||n.number>g.core_count||n.stages||n.swapped||n.has_direction) return false;
                for(unsigned j=0;j<i;++j) if(g.nodes[j].type==DST_CORE&&g.nodes[j].number==n.number) return false;
            } else if(n.number||n.total_time||n.time||n.cascading) return false;
            if(n.type==DST_SWAP) {
                if(n.stages>=g.slots||n.has_direction) return false;
                // Native GetDamageStage converts floor to SINT before its clamp. Guard the conversion itself.
                if(n.max_health!=0.f&&!Range((n.max_health-n.health)/n.max_health*n.stages+.5f,-1048576.f,1048576.f)) return false;
            } else if(n.stages||n.swapped) return false;
            if(!n.has_direction&&(n.direction.v[0]||n.direction.v[1]||n.direction.v[2])) return false;
        }
        return cores==g.core_count&&g.nodes[g.core-1].type==DST_CORE&&g.nodes[g.core-1].number==1;
    }
    static bool Validate(const CE_WorldFrame &f) {
        if (!f.epoch || !f.tick || f.tick>20000000 || !f.sequence || f.level != 852 || f.phase < GAME_STATE_PRE_RUNNING || f.phase > GAME_STATE_GAME_DRAWN || f.paused > 1 || f.fade>1 || f.alpha>255 || !EndCountBound(f.phase,f.end_count) || !f.count || f.count > kEntities || f.birth_count>kBirths)
            return Fail("invalid world frame header");
        if (!ObjectivesBound(f)) return Fail("invalid mission objective state");
        if((!f.lifecycle_baseline&&f.lifecycle_watermark)||(f.lifecycle_baseline&&(f.birth_count||f.sequence<=f.lifecycle_baseline)))
            return Fail("invalid lifecycle world scope");
        for(unsigned i=0;i<f.birth_count;++i) {
            if(!BirthBound(f.births[i],f.tick)||(i&&(f.births[i].serial!=f.births[i-1].serial+1||f.births[i].id<=f.births[i-1].id))) return Fail("invalid, gapped or unordered explosion birth journal");
        }
        unsigned players = 0;
        for (unsigned i = 0; i < f.count; ++i) {
            const Entity &s = f.entities[i]; const Core &c = s.core;
            if (!c.id || (i && c.id <= f.entities[i - 1].core.id) || c.kind < STATIC_THING || c.kind > EXPLOSION_THING || c.flags > 65535 || c.player > 2 ||
                c.animation.present > 1 || c.animation.looped > 1) return Fail("invalid or unordered entity record");
            if(!VecBound(c.pos)||!VecBound(c.old_pos)||!VecBound(c.velocity)||!MatBound(c.orientation)||!MatBound(c.old_orientation)||
                !TimeBound(c.last_move)||!TimeBound(c.ground)||!TimeBound(c.water)||!TimeBound(c.object)||!AnimationBound(c.animation)) return Fail("entity numeric state exceeds native consumer bounds");
            if (c.kind == ENGINE_THING) {
                const EngineState &e = s.engine;
                if(!CockpitBound(e.cockpit)||e.hit< -100||e.hit>100||e.hit_decay!=10||!Range(e.life,-1048576.f,1048576.f)||!Range(e.energy,-1048576.f,1048576.f)||!Range(e.shields,-1048576.f,1048576.f)||!Range(e.max_shields,0.f,1048576.f)||e.allegiance>6||
                    !VecBound(e.euler,64.f)||!VecBound(e.old_euler,64.f)||!VecBound(e.camera.pos)||!VecBound(e.camera.old_pos)||!MatBound(e.camera.orientation)||!MatBound(e.camera.old_orientation)||
                    !Range(e.camera.zoom,.001f,1024.f)||!Range(e.camera.old_zoom,.001f,1024.f)||!AngleBound(e.walk_cycle)||!AngleBound(e.old_walk_cycle)||!TimeBound(e.jet_thrust)||!TimeBound(e.new_threat_time)) return Fail("engine numeric state exceeds native consumer bounds");
#define VALIDATE_FLOAT(name) if(!TimeBound(e.name)) return Fail("engine scalar exceeds native consumer bounds");
                ENGINE_FLOATS(VALIDATE_FLOAT)
#undef VALIDATE_FLOAT
                for(float tracking:e.tracking) if(!AngleBound(tracking)) return Fail("tracking angle exceeds native consumer bounds");
                for(float store:e.stores) if(!Range(store,-1048576.f,1048576.f)) return Fail("store value exceeds native consumer bounds");
                for(unsigned j=0;j<e.part_count&&j<kParts;++j) {
                    const Pose &p=e.parts[j];
                    if(!VecBound(p.pos)||!VecBound(p.old_pos)||!MatBound(p.orientation)||!MatBound(p.old_orientation)||!Range(p.frame,0.f,1048576.f)||
                        !(p.old_frame==-1.f||Range(p.old_frame,0.f,1048576.f))) return Fail("pose exceeds native consumer bounds");
                }
                if (strcmp(c.class_name,"CBattleEngine") || !c.player || (players & (1u << c.player)) || e.mState > 3 || e.mEngineState>2 || e.mOldEngineState>2 || e.weapon_count > kWeapons || e.lock_count > kLocks || e.fired_count > kLocks ||
                    e.part_count < 1 || e.part_count > kParts || e.view > PLAYER_3RD_PERSON_VIEW || e.preferred_view > PLAYER_3RD_PERSON_VIEW ||
                    e.camera.hud>1 || !(e.camera.zoom>0.f) || !(e.camera.old_zoom>0.f) ||
                    e.radar_present>1 || e.new_threat>1 || e.threat_count>kThreats || e.flash_count>kFlashes || (!e.radar_present&&e.threat_count) ||
                    !References(f, e.crosshair, true) || !References(f, e.crosshair_range, true) || !References(f, e.auto_aim) || !References(f, e.standing_on)) return Fail("invalid engine state or reference");
                players |= 1u << c.player;
                const uint32_t booleans[]={e.mAugActive,e.mStalling,e.mCloaked,e.mTrackingActive,e.mVisible,e.mPoweredUp,e.mSlowMovement,e.mFlightModeActive,e.mThrustersOn};
                for(uint32_t value:booleans) if(value>1) return Fail("invalid engine boolean");
                for(unsigned j=0;j<6;++j) if(e.store_heat[j]>1||e.store_overheat[j]>1) return Fail("invalid store boolean");
                for (unsigned j = 0; j < e.weapon_count; ++j) if (e.weapons[j].active > 1 || !TimeBound(e.weapons[j].charge)||!TimeBound(e.weapons[j].next_fire)||!References(f, e.weapons[j].target)) return Fail("invalid weapon reference or numeric state");
                for (unsigned j = 0; j < e.lock_count; ++j) if (e.locks[j].locked > 1 || e.locks[j].direct > 1 || !TimeBound(e.locks[j].start)||!TimeBound(e.locks[j].finish)||!References(f, e.locks[j].target, true)) return Fail("invalid lock reference");
                for (unsigned j = 0; j < e.fired_count; ++j) if (e.fired[j].locked > 1 || e.fired[j].direct > 1 || !TimeBound(e.fired[j].start)||!TimeBound(e.fired[j].finish)||!References(f, e.fired[j].target, true)) return Fail("invalid fired-lock reference");
                for(unsigned j=0;j<e.threat_count;++j) {
                    if(e.threats[j].seen>1||!AngleBound(e.threats[j].yaw)||!Range(e.threats[j].distance,0.f,1048576.f)||!TimeBound(e.threats[j].time)||!References(f,e.threats[j].round)) return Fail("invalid radar threat reference");
                    if(e.threats[j].round) for(unsigned k=0;k<f.count;++k) if(f.entities[k].core.id==e.threats[j].round&&f.entities[k].core.kind!=ROUND_THING) return Fail("radar threat is not a projectile");
                }
                for(unsigned j=0;j<e.flash_count;++j) if(!AngleBound(e.flashes[j].yaw)||!TimeBound(e.flashes[j].start)) return Fail("damage flash exceeds native consumer bounds");
            } else if (c.player) return Fail("non-engine entity claims a player");
            if (c.kind == STATIC_THING && !StaticKind(c.class_name)) return FailKind("unsupported static kind", c.class_name);
            if (c.kind == ROUND_THING && ((strcmp(c.class_name,"CRound") && strcmp(c.class_name,"CMissile")) || s.round.armed>1 || s.round.no_gravity>1 ||
                s.round.incoming_member>1 || (s.round.incoming_member&&(!s.round.target||!EngineReference(f,s.round.target))) ||
                s.round.allegiance>6 || !References(f, s.round.target) || !References(f, s.round.launcher, true))) return Fail("invalid projectile reference");
            if(c.kind==ROUND_THING&&f.lifecycle_baseline&&(strcmp(c.class_name,"CRound")||!s.round.incarnation||!s.round.schema||s.round.incarnation>f.lifecycle_watermark||s.round.target||s.round.jumps||s.round.armed||s.round.no_gravity||s.round.special||s.round.incoming_member)) return Fail("projectile requires the selected committed lifecycle incarnation");
            if(c.kind==ROUND_THING&&(!TimeBound(s.round.start)||!VecBound(s.round.beam_start)||!VecBound(s.round.destination))) return Fail("projectile exceeds native consumer bounds");
            if(c.kind==CANNON_THING) {
                const CannonState &cannon=s.cannon; const UnitPresentation &unit=cannon.unit;
                if(strcmp(c.class_name,"CCannon")||!UnitBound(unit)||!TimeBound(cannon.drop)||!TimeBound(cannon.old_drop)||cannon.state>3||cannon.above_water>1) return Fail("invalid typed cannon state");
            }
            if(c.kind==BUILDING_THING) {
                const BuildingState &b=s.building;
                if(strcmp(c.class_name,"CBuilding")||!UnitBound(b.unit)||!SegmentBound(b.graph)||b.door>3||b.shut>3||b.can_shut>1||
                    !TimeBound(b.door_time)||!TimeBound(b.close_time)||!TimeBound(b.last_hit)) return Fail("invalid typed building graph or state");
            }
            if(c.kind==EXPLOSION_THING) {
                const ExplosionState &e=s.explosion;
                if(strcmp(c.class_name,"CExplosion")||!e.serial||e.deep>1||!References(f,e.originator)||c.initial_number||c.animation.present||c.mesh[0]||
                    (e.deep?(e.definition||e.schema||e.radius||e.current_radius||e.allegiance||c.type!=THING_TYPE_NONE):(!e.schema||e.definition>=1024||e.allegiance>6||!TimeBound(e.radius)||!TimeBound(e.current_radius)||!(c.type&THING_TYPE_EXPLOSION))))
                    return Fail("invalid live explosion state");
            }
            if(c.kind==FEATURE_THING) {
                const FeatureState &feature=s.feature;
                if(strcmp(c.class_name,"CFeature")||!feature.definition[0]||!feature.mesh_schema||feature.allegiance>6||feature.above_water>1||feature.invincible>1||feature.lockable>1||
                    !TimeBound(feature.life)||!TimeBound(feature.initial_life)) return Fail("invalid typed feature state");
            }
        }
        return players == 6 || Fail("world frame does not contain both players");
    }
    static CThing *Baseline(const Core &c) {
        if (!c.initial_number) return NULL;
        ListIterator<CThing> it(&WORLD.GetThingNB());
        for (CThing *t = it.First(); t; t = it.Next())
            if ((uint32_t)(unsigned short)t->GetThingNumber() + 1 == c.initial_number && ThingKind(t) == c.kind && !strcmp(t->_GetClassName(), c.class_name)) return t;
        return NULL;
    }
    static bool Compatible(CThing *thing, const Entity &s) {
        if (!thing || ThingKind(thing) != s.core.kind || strcmp(thing->_GetClassName(), s.core.class_name)) return Fail("entity class changed across the barrier");
        uint32_t dynamic_type=s.core.kind==ENGINE_THING?(THING_TYPE_GROUND_UNIT|THING_TYPE_AIR_UNIT|THING_TYPE_VEHICLE):0;
        if((thing->mThingType^s.core.type)&~dynamic_type) return Fail("authored entity type bits differ");
        if(((uint32_t)(unsigned short)thing->mFlags^s.core.flags)&(TF_IN_MAP_WHO|TF_IS_BIG_THING)) return Fail("local world registration flags differ from the authoritative instance");
        if (s.core.kind == ENGINE_THING) {
            CBattleEngine *e = (CBattleEngine *)thing; const EngineState &in = s.engine; CMCBattleEngine *mc = Mech(e);
            if (!e->mPlayer.ToRead() || e->mPlayer->GetNumber() != (int)s.core.player || e->mConfigurationId != (int)in.configuration || !e->mWalkerPart || !e->mJetPart || !mc || !mc->mMesh ||
                mc->mMesh->mNumParts != (int)in.part_count || !mc->mPartPos || !mc->mOldPartPos || !mc->mPartOri || !mc->mOldPartOri || !mc->mPartFrame || !mc->mOldPartFrame)
                return Fail("unsupported engine configuration, player binding or pose topology");
            CockpitState cockpit_topology={};
            if(!CockpitTopology(e,cockpit_topology,&in.cockpit)||strcmp(cockpit_topology.mesh,in.cockpit.mesh)||cockpit_topology.schema!=in.cockpit.schema)
                return Fail("cockpit model, pose or animation schema differs across the barrier");
            if(in.radar_present!=(uint32_t)(e->mCachedRadarWarningReceiver!=NULL)) return Fail("radar equipment topology differs");
            for(unsigned i=0;i<in.part_count;++i) {
                CMeshPart *part=mc->mMesh->mParts[i]; const Pose &pose=in.parts[i];
                if(!part||part->mPartNumber!=(int)i||!FrameBound(pose.frame,part->mNumVFrames)||
                    !(pose.old_frame==-1.f||FrameBound(pose.old_frame,part->mNumVFrames))) return Fail("pose frame differs from native part topology");
            }
            CWeapon *weapons[kWeapons]; unsigned count=Weapons(e,weapons);
            if(count!=in.weapon_count) return Fail("unsupported engine weapon topology");
            for (unsigned index=0;index<count;++index) { CWeapon *w=weapons[index];
                if (!w->mData || strcmp(w->mData->mName, in.weapons[index].name) ||
                    (in.weapons[index].mode[0] && !UPhysicsManager::GetWeaponModeData((char *)in.weapons[index].mode))) return Fail("unsupported engine weapon topology");
            }
            unsigned walker_count=e->mWalkerPart->mWeapons.Size()+(e->mWalkerPart->mPrimaryWeapon?1:0), jet_count=e->mJetPart->mWeapons.Size();
            if ((walker_count ? in.walker_weapon>=walker_count : in.walker_weapon!=0) || (jet_count ? in.jet_weapon>=jet_count : in.jet_weapon!=0))
                return Fail("unsupported selected weapon");
        } else {
            CMesh *mesh = thing->GetRTMesh(); const char *name = mesh ? mesh->GetName() : "";
            if (strcmp(name, s.core.mesh)) return Fail("entity mesh differs across the barrier");
            CComplexThing *complex = dynamic_cast<CComplexThing *>(thing);
            if (complex && s.core.kind == STATIC_THING && complex->mMotionController) return Fail("unsupported static pose controller");
        }
        if(s.core.kind==ROUND_THING) {
            CRound *round=(CRound *)thing; Binding *binding=ByThing(thing); uint64_t schema=0;
            if(!PortTypeInfoEqual(typeid(*round),typeid(CRound))||round->mRenderThing||round->mMotionController||round->mAnimation||
               !RoundDefinition(round->mData,schema)||schema!=s.round.schema||!s.round.incarnation||
               (binding&&binding->round_incarnation&&(binding->round_incarnation!=s.round.incarnation||binding->round_schema!=schema)))
                return Fail("projectile definition, native topology or lifecycle incarnation differs");
        }
        if(s.core.kind==EXPLOSION_THING) {
            CExplosion *explosion=(CExplosion *)thing; const ExplosionState &in=s.explosion; Binding *binding=ByThing(thing);
            if(binding&&binding->birth.serial&&binding->birth.serial!=in.serial) return Fail("explosion incarnation changed");
            if(explosion->mRenderThing||explosion->mMotionController||explosion->mAnimation) return Fail("unsupported native explosion render topology");
            if(in.deep) {
                if(explosion->mThingType!=THING_TYPE_NONE||explosion->mCollisionSeekingThing) return Fail("deep-water explosion was initialized as a normal blast");
            } else {
                uint32_t index=0; uint64_t schema=0; char name[kName];
                if(!ExplosionDefinition(explosion->mBehaviour,index,schema,name)||index!=in.definition||schema!=in.schema) return Fail("live explosion definition differs");
            }
        }
        CComplexThing *complex = dynamic_cast<CComplexThing *>(thing);
        if (s.core.animation.present != (uint32_t)(complex && complex->mAnimation)) return Fail("entity animation topology differs");
        if(s.core.kind==CANNON_THING) {
            CCannon *c=(CCannon *)thing; CannonState current={};
            if(!CannonCapture(c,current,s.core)) return false;
            const UnitPresentation &a=current.unit,&b=s.cannon.unit;
            if(strcmp(a.definition,b.definition)||a.mesh_schema!=b.mesh_schema||a.initial_life!=b.initial_life||a.behavior!=b.behavior||a.icon!=b.icon||a.fenrir!=b.fenrir||
                c->mUnitData!=UPhysicsManager::GetUnitData((char *)b.definition)) return Fail("cannon authored definition or mesh schema differs");
        }
        if(s.core.kind==BUILDING_THING) {
            CBuilding *b=(CBuilding *)thing; BuildingState current={};
            if(!BuildingCapture(b,current,s.core)) return false;
            const UnitPresentation &a=current.unit,&in=s.building.unit;
            if(strcmp(a.definition,in.definition)||a.mesh_schema!=in.mesh_schema||a.initial_life!=in.initial_life||a.behavior!=in.behavior||a.icon!=in.icon||a.fenrir!=in.fenrir||
                b->mUnitData!=UPhysicsManager::GetUnitData((char *)in.definition)||current.graph.schema!=s.building.graph.schema||
                current.graph.count!=s.building.graph.count||current.graph.slots!=s.building.graph.slots||current.graph.core!=s.building.graph.core||
                current.graph.core_count!=s.building.graph.core_count) return Fail("building authored definition or segment schema differs");
            for(unsigned i=0;i<current.graph.slots;++i) if(current.graph.alias[i]!=s.building.graph.alias[i]) return Fail("building slot aliases differ");
            for(unsigned i=0;i<current.graph.count;++i) {
                const SegmentState &old=current.graph.nodes[i],&next=s.building.graph.nodes[i];
                if(old.type!=next.type||old.parent!=next.parent||old.part!=next.part||old.number!=next.number||old.stages!=next.stages||
                    (old.destroyed&&!next.destroyed)||(old.swapped&&!next.swapped)) return Fail("building segment topology or irreversible state differs");
            }
        }
        if(s.core.kind==FEATURE_THING) {
            CFeature *feature=(CFeature *)thing; FeatureState current={};
            if(!FeatureCapture(feature,current,s.core)) return false;
            const FeatureState &in=s.feature;
            if(strcmp(current.definition,in.definition)||strcmp(current.sound,in.sound)||current.mesh_schema!=in.mesh_schema||current.initial_life!=in.initial_life||
                current.invincible!=in.invincible||current.lockable!=in.lockable||current.explosion!=in.explosion||current.icon!=in.icon||
                feature->mData!=UPhysicsManager::GetFeatureData((char *)in.definition)) return Fail("feature authored definition or mesh schema differs");
        }
        CMesh *animation_mesh=thing->GetRTMesh();
        if(s.core.kind==ENGINE_THING) {
            CBattleEngine *e=(CBattleEngine *)thing;
            CMesh *other=e->mOtherMeshHackRenderThing?e->mOtherMeshHackRenderThing->GetRTMesh():NULL;
            if(!animation_mesh||strcmp(animation_mesh->GetName(),s.core.mesh)) animation_mesh=other;
            if(!animation_mesh||strcmp(animation_mesh->GetName(),s.core.mesh)) return Fail("engine mesh is not one of its native configurations");
        }
        if(s.core.animation.present&&!AnimationIndexBound(s.core.animation.index,animation_mesh?animation_mesh->mNumAnimModes:0)) return Fail("animation index exceeds native mesh topology");
        return true;
    }
    static void UnitApply(CUnit *unit,const UnitPresentation &in) {
        unit->mLife=in.life; unit->mEnergy=in.energy; unit->mShields=in.shields; unit->mMaxShields=in.max_shields;
        unit->mAllegiance=in.allegiance; unit->mActive=in.active; unit->mVulnerable=in.vulnerable;
        unit->mUnknown218=in.hit; unit->mUnknown21C=in.hit_decay;
        unit->mCurrentOrientation.mYaw=in.euler.v[0]; unit->mCurrentOrientation.mPitch=in.euler.v[1]; unit->mCurrentOrientation.mRoll=in.euler.v[2];
        unit->mUnknownE0=in.turret[0]; unit->mUnknownE4=in.turret[1]; unit->mUnknownE8=in.turret[2];
        unit->mUnknownEC=in.turret[3]; unit->mUnknownF0=in.turret[4]; unit->mUnknownF4=in.turret[5];
    }
    static void CannonApply(CCannon *cannon,const CannonState &in) {
        UnitApply(cannon,in.unit); cannon->mUnknown250=in.drop; cannon->mUnknown254=in.old_drop;
        cannon->mState=in.state; cannon->mAboveWater=in.above_water;
        cannon->mRenderThing->mPose->mLastFrame=-9999;
    }
    static void FeatureApply(CFeature *feature,const FeatureState &in) {
        feature->mLife=in.life; feature->mAllegiance=in.allegiance; feature->mAboveWater=in.above_water;
        feature->mRenderThing->mPose->mLastFrame=-9999;
    }
    static bool BuildingApply(CBuilding *building,const BuildingState &in) {
        SegmentGraph current={}; CDestroyableSegment *nodes[kParts]={};
        if(!SegmentCapture(building,current,nodes)) return false;
        UnitApply(building,in.unit); CDestructableSegmentsController *c=building->mSegments;
        c->mChanged=in.graph.changed; c->mUnknown24=in.graph.primary_visible; c->mUnknown28=in.graph.pickup_mode; c->mUnknown2C=in.graph.no_imposter;
        c->mUnknown14=in.graph.fall_threshold; c->mTotalHealth=in.graph.total_health; c->mTotalSize=in.graph.total_size;
        for(unsigned i=0;i<in.graph.count;++i) {
            CDestroyableSegment *n=nodes[i]; const SegmentState &state=in.graph.nodes[i];
            // Break's presentation cleanup only. Damage, spawners, rubble and scheduled callbacks remain host-owned.
            if(!n->mDestroyed&&state.destroyed) {
                CMeshPart *part=building->GetRTMesh()->mParts[n->mPart];
                for(int child=0;child<part->mNumChildren;++child) {
                    int effect=building->mRenderThing->FindEffect(part->mChildren[child]);
                    CParticleEffectLink *link=effect<0?NULL:(CParticleEffectLink *)building->mRenderThing->GetEffect(effect);
                    if(link) link->Kill();
                }
            }
            n->mHealth=state.health; n->mMaxHealth=state.max_health; n->mLastDamageTime=state.last_damage_time;
            n->mLastDamageAmount=state.last_damage_amount; n->mVulnerable=state.vulnerable; n->mDestroyed=state.destroyed; n->mSize=state.size;
            if(state.type==DST_CORE) {
                CDestroyableCoreSegment *core=(CDestroyableCoreSegment *)n; core->mTotalTime=state.total_time; core->mTime=state.time; core->mCascading=state.cascading;
            } else if(state.type==DST_SWAP) ((CDestroyableSwapSegment *)n)->mSwapped=state.swapped;
            else { CDestroyableExtraSegment *extra=(CDestroyableExtraSegment *)n; extra->mHasDirection=state.has_direction; PutVec(extra->mDirection,state.direction); }
        }
        building->mUnknown254=in.door; building->mUnknown258=in.door_time; building->mUnknown25C=in.close_time;
        building->mUnknown260=in.can_shut; building->mUnknown264=in.shut; building->mUnknown268=in.last_hit;
        Binding *binding=ByThing(building); if(!binding) return Fail("building lost its presentation identity");
        binding->segment_slots=in.graph.slots;
        for(unsigned slot=0;slot<in.graph.slots;++slot) binding->segment_hit[slot]=in.graph.alias[slot]?in.graph.nodes[in.graph.alias[slot]-1].hit:0;
        building->mRenderThing->mPose->mLastFrame=-9999; return true;
    }
    static int HitEffect(CUnit *unit,int part,int *out) {
        if(CE_LockstepTicking()&&out) {
            // Retail's query raises the unit's flash counter as a side effect (Unknown107: the counter and its decay event),
            // so a peer drawing a damaged building would change the simulation. Lockstep answers without that effect:
            // the counter saturates within a frame or two in retail, then falls by 10 every 20th of a second.
            if(!unit->mSegments) { *out=unit->mUnknown218; return 1; }
            float elapsed=EVENT_MANAGER.GetTime()-unit->mSegments->GetLastDamageTime(part);
            if(!(elapsed<1.0f)) { *out=0; return 1; }
            SINT level=(unit->mSegments->GetLastDamageAmount(part)>0.f?100:unit->mUnknown218)-(SINT)(elapsed*20.0f)*10;
            *out=level<0?0:level>100?100:level; return 1;
        }
        Binding *b=ByThing(unit); if(!CE_WorldReplica()||!b||b->kind!=BUILDING_THING||!out) return 0;
        *out=part>=0&&part<(int)b->segment_slots?b->segment_hit[part]:0; return 1;
    }
    static void CoreApply(CThing *thing, const Core &in) {
        if(in.kind==EXPLOSION_THING&&in.type==THING_TYPE_NONE) { thing->mFlags=(short)in.flags; return; }
        FVector pos; PutVec(pos, in.pos); thing->Teleport(pos); thing->SetObjective((in.flags&TF_MARKED_OBJECTIVE)!=0); thing->mFlags = (short)in.flags; thing->mThingType=in.type;
        if (CComplexThing *complex = dynamic_cast<CComplexThing *>(thing)) {
            PutMat(complex->mOrientation, in.orientation);
            if (complex->mAnimation) {
                CAnimation *a = complex->mAnimation; a->mFrame = in.animation.frame; a->mLastFrame = in.animation.old_frame;
                a->mFrameIncrement = in.animation.increment; a->mAnimMode = (EAnimMode)in.animation.mode; a->mAnimIndex = (int32_t)in.animation.index; a->mForceLooped = in.animation.looped;
            }
        }
        if (CActor *actor = dynamic_cast<CActor *>(thing)) {
            PutVec(actor->mOldPos, in.old_pos); PutVec(actor->mVelocity, in.velocity); PutMat(actor->mOldOrientation, in.old_orientation);
            actor->mLastMoveTime = in.last_move; actor->mLastTimeOnGround = in.ground; actor->mLastTimeInWater = in.water; actor->mLastTimeOnObject = in.object;
        }
    }
    static bool ApplyLocks(SPtrSet<CLockInfo> &list, const LockState *in, unsigned count) {
        while (CLockInfo *lock = list.First()) { list.Remove(lock); delete lock; }
        for (unsigned i = 0; i < count; ++i) {
            CLockInfo *lock = new(MEMTYPE_BATTLEENGINE) CLockInfo;
            if(!lock) return Fail("lock allocation refused; replica requires teardown");
            lock->mUnit = (CUnit *)Resolve(in[i].target); lock->mStart = in[i].start; lock->mFinish = in[i].finish;
            lock->mLocked = in[i].locked; lock->mDirectLock = in[i].direct; list.Append(lock);
        }
        return true;
    }
    static bool EngineApply(CBattleEngine *e, const EngineState &in) {
        CockpitApply(e->mCockpit,in.cockpit);
        e->mLife = in.life; e->mEnergy = in.energy; e->mShields = in.shields; e->mMaxShields = in.max_shields; e->mAllegiance = in.allegiance; e->mUnknown218=in.hit; e->mUnknown21C=in.hit_decay;
        e->mCurrentOrientation.mYaw = in.euler.v[0]; e->mCurrentOrientation.mPitch = in.euler.v[1]; e->mCurrentOrientation.mRoll = in.euler.v[2];
        e->mOldEulerAngles.mYaw = in.old_euler.v[0]; e->mOldEulerAngles.mPitch = in.old_euler.v[1]; e->mOldEulerAngles.mRoll = in.old_euler.v[2];
#define APPLY_FLOAT(name) e->name = in.name;
        ENGINE_FLOATS(APPLY_FLOAT)
#undef APPLY_FLOAT
        e->mState = (EBattleEngineState)in.mState; e->mEngineState = (EEngineState)in.mEngineState; e->mOldEngineState = (EEngineState)in.mOldEngineState;
        e->mAugActive = in.mAugActive; e->mStalling = in.mStalling; e->mCloaked = in.mCloaked; e->mTrackingActive = in.mTrackingActive;
        e->mVisible = in.mVisible; e->mPoweredUp = in.mPoweredUp; e->mSlowMovement = in.mSlowMovement; e->mFlightModeActive = in.mFlightModeActive; e->mThrustersOn = in.mThrustersOn;
        e->mTrackingYawOffset=in.tracking[0]; e->mTrackingPitchOffset=in.tracking[1]; e->mOldTrackingYawOffset=in.tracking[2]; e->mOldTrackingPitchOffset=in.tracking[3];
        for (unsigned i = 0; i < 6; ++i) { e->mStoreValue[i] = in.stores[i]; e->mStoreHeat[i] = in.store_heat[i]; e->mStoreOverheat[i] = in.store_overheat[i]; }
        e->mCurrentUnitOverCrosshair = (CUnit *)Resolve(in.crosshair); e->mCurrentUnitOverCrosshairRegardlessOfRange = (CUnit *)Resolve(in.crosshair_range);
        e->mAutoAimTarget = Resolve(in.auto_aim); e->mStandingOnThing = Resolve(in.standing_on);
        e->mWalkerPart->mCurrentWeapon = in.walker_weapon; e->mJetPart->mCurrentWeapon = in.jet_weapon;
        e->mWalkerPart->mWalkCycle = in.walk_cycle; e->mWalkerPart->mOldWalkCycle = in.old_walk_cycle; e->mJetPart->mThrusterValue = in.jet_thrust;
        CWeapon *weapons[kWeapons]; unsigned count=Weapons(e,weapons);
        for (unsigned n=0;n<count;++n) { CWeapon *w=weapons[n];
            const WeaponState &s = in.weapons[n]; w->mTarget = Resolve(s.target); w->mActive = s.active; w->mCharge = s.charge; w->mNextFireTime = s.next_fire;
            w->mBurstCount = s.burst; w->mUnknown68 = s.charged; w->mUnknown70 = s.launch; w->mUnknown74 = s.angle;
            w->mMode = s.mode[0] ? UPhysicsManager::GetWeaponModeData((char *)s.mode) : NULL;
        }
        if(!ApplyLocks(e->mLocks, in.locks, in.lock_count)||!ApplyLocks(e->mFiredLocks, in.fired, in.fired_count)) return false;
        e->MassiveHackPutUsInRightMesh(); // genuine mesh owner; the replica ProcessMovement hook prevents simulation here
        CMCBattleEngine *mc = Mech(e);
        for (unsigned i = 0; i < in.part_count; ++i) {
            const Pose &p = in.parts[i]; PutVec(mc->mPartPos[i], p.pos); PutVec(mc->mOldPartPos[i], p.old_pos);
            PutMat(mc->mPartOri[i], p.orientation); PutMat(mc->mOldPartOri[i], p.old_orientation); mc->mPartFrame[i] = p.frame; mc->mOldPartFrame[i] = p.old_frame;
        }
        mc->mWasUsed = TRUE; mc->mUsed = TRUE; mc->mNeedsUpdate = TRUE;
        CPlayer *player = e->GetPlayer(); player->mCurrentViewMode = (EPlayerCameraView)in.view; player->mPreferedControlView = (EPlayerCameraView)in.preferred_view;
        for (unsigned i = 0; i < PS_NUM_PLAYERSTATS; ++i) player->mStat[i] = in.stats[i];
        ReplicaCamera *camera=dynamic_cast<ReplicaCamera *>(GAME.GetCamera(player->GetNumber()-1));
        if(!camera) { camera=new(MEMTYPE_CAMERA) ReplicaCamera; if(!camera) return Fail("camera allocation refused; replica requires teardown"); GAME.SetCurrentCamera(player->GetNumber()-1,camera); }
        camera->state=in.camera;
        CRadarWarningReceiver *radar=e->mCachedRadarWarningReceiver;
        if(radar) {
            while(SRadarThreat *t=radar->mThreats.First()) { radar->mThreats.Remove(t); delete t; }
            for(unsigned i=0;i<in.threat_count;++i) {
                const ThreatState &s=in.threats[i]; SRadarThreat *t=new(MEMTYPE_RADAR) SRadarThreat(s.yaw,s.distance,(int32_t)s.round,Resolve(s.round),s.time);
                if(!t) return Fail("threat allocation refused; replica requires teardown");
                t->mSeen=s.seen; radar->mThreats.Append(t);
            }
            radar->mNewThreat=in.new_threat; radar->mNewThreatTime=in.new_threat_time;
        }
        while(CDamageFlash *flash=e->mDamageFlashes.First()) { e->mDamageFlashes.Remove(flash); delete flash; }
        for(unsigned i=0;i<in.flash_count;++i) { CDamageFlash *flash=new(MEMTYPE_BATTLEENGINE) CDamageFlash;
            if(!flash) return Fail("flash allocation refused; replica requires teardown");
            flash->mYaw=in.flashes[i].yaw; flash->mStartTime=in.flashes[i].start; e->mDamageFlashes.Append(flash); }
        return true;
    }
    static void RoundApply(CRound *round, const RoundState &in) {
        Binding *binding=ByThing(round); if(binding) { binding->round_incarnation=in.incarnation; binding->round_schema=in.schema; }
        // A replica adopts the authoritative target and notice-board membership without ReleaseTarget's LockHit
        // gameplay callback. Readers still register through their genuine monitored setter.
        WORLD.mSet110.Remove(round); round->mUnknownE8 = Resolve(in.target);
        if(in.incoming_member) WORLD.mSet110.Add(round);
        round->mUnknownEC = Resolve(in.launcher); round->mAllegiance = (EAllegiance)in.allegiance;
        round->mJumpsPerformed = in.jumps; round->mStartTime = in.start;
        if(round->mData->mBeam) PutVec(round->mBeamStart, in.beam_start);
        PutVec(round->mDest, in.destination);
        round->mUnknown120 = in.armed; round->mUnknown128 = in.special; round->mUnknown12C = in.no_gravity;
        if (round->mEffect.Full()) { round->mEffect.SetPos(round->mPos); round->mEffect.SetOrientation(round->mOrientation); }
    }
    static bool BirthCompatible(const ExplosionBirth &b,CExplosionData *&data) {
        data=NULL;
        if(b.deep) return (MAP.GetWaterLevel()+1.5f<=b.pos.v[2])||Fail("deep-water birth disagrees with the local map");
        if(!(MAP.GetWaterLevel()+1.5f>b.pos.v[2])) return Fail("normal explosion birth disagrees with the local map");
        data=UPhysicsManager::GetExplosionData(b.definition); uint32_t index=0; uint64_t schema=0; char name[kName];
        if(!ExplosionDefinition(data,index,schema,name)||index!=b.definition||schema!=b.schema||strcmp(name,b.name)||
            (b.volumetric&&b.attached&&data->mVolumetric>1)||b.map_who!=(uint32_t)!(b.radius<=0.f||data->mDamage<=.0015f)||(!b.map_who&&b.mask!=THING_TYPE_EVERYTHING)||(b.radius>3.f&&data->mDamage>0.f&&data->mTime<=0))
            return Fail("explosion birth definition or volumetric context differs");
        return true;
    }
    static CThing *FrameThing(const CE_WorldFrame &f,CThing **targets,uint32_t id) {
        for(unsigned i=0;id&&i<f.count;++i) if(f.entities[i].core.id==id) return targets[i];
        return NULL; // a historical originator/ignore target may already have genuinely despawned
    }
    static CExplosion *CreateExplosion(const CE_WorldFrame &f,CThing **targets,const ExplosionBirth &b,CExplosionData *data) {
        creating_explosion=true; CExplosion *explosion=UPhysicsManager::SpawnExplosion(b.deep?0:(SINT)b.definition); creating_explosion=false;
        if(!explosion) { Fail("explosion allocation refused; replica requires teardown"); return NULL; }
        CExplosionInitThing init; PutVec(init.mPos,b.pos); init.mOriginator=FrameThing(f,targets,b.originator);
        if(!b.deep) {
            init.mBehaviour=data; init.mColType=(ECollisionType)b.collision; init.mAllegiance=(EAllegiance)b.allegiance;
            init.mAttachedTo=FrameThing(f,targets,b.attached); init.mUseAttachedRadius=b.use_radius; init.mAllowVolumetric=b.volumetric; init.mImportant=b.important;
            init.mOrientationType=(EOrientationType)b.orientation_type;
            if(b.orientation_type==EULER_ANGLES) { init.mYaw=b.euler.v[0]; init.mPitch=b.euler.v[1]; init.mRoll=b.euler.v[2]; }
            else PutMat(init.mOrientation,b.orientation);
            init.mInitCST.mNotSeekCollisionWithBF=b.mask; init.mInitCST.mDesiredCollision=(ECollisionLevel)b.desired; init.mInitCST.mMinCollision=(ECollisionLevel)b.min_collision;
        }
        applying_birth=&b; birth_spawn=explosion; explosion->Init(&init); applying_birth=NULL; birth_spawn=NULL;
        if(poisoned) return NULL;
        return explosion;
    }
    static void ExplosionApply(CExplosion *explosion,const ExplosionState &in,Binding &binding) {
        binding.birth.serial=in.serial; binding.birth.id=binding.id; binding.birth.definition=in.definition; binding.birth.schema=in.schema; binding.birth.deep=in.deep; binding.birth.ready=1;
        explosion->mOriginator=Resolve(in.originator);
        if(!in.deep) { explosion->mAllegiance=in.allegiance; explosion->mRadius=in.radius; explosion->mCurrentRadius=in.current_radius; }
    }
    static bool LifeCompatible(const LifeRecord &record) {
        if(!LifeRecordBound(record)||!LifeDependency(record)) return Fail("compact record or baseline dependency differs");
        if(record.info.kind==CE_WORLD_LIFECYCLE_DESPAWN) return true;
        if(record.info.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH||record.info.kind==CE_WORLD_LIFECYCLE_ROUND_DESPAWN) {
            CRoundData *data=UPhysicsManager::GetRoundData((char *)record.round.definition); uint64_t schema=0;
            if(!RoundDefinition(data,schema)||schema!=record.round.schema) return Fail("round lifecycle definition differs from local resources");
            bool owner=false; for(unsigned i=0;i<life_key_count;++i) if(life_keys[i].id==record.round.launcher) owner=true;
            if(!owner) return Fail("round lifecycle launcher is outside baseline dependencies");
            if(record.info.kind==CE_WORLD_LIFECYCLE_ROUND_DESPAWN) return true;
            if(!RoundEntryValues(data,record.round_birth)) return Fail("round entry does not match the selected source lifespan or velocity envelope");
            Binding *binding=ById(record.dependency.id); if(!binding||binding->kind!=ENGINE_THING) return Fail("round owner is no longer live");
            CWeapon *weapon=WeaponAt((CBattleEngine *)binding->thing,record.round_birth.inventory,record.round_birth.index);
            CWeaponModeData *mode=NULL;
            if(weapon&&weapon->mData&&!strcmp(weapon->mData->mName,record.round_birth.weapon)) for(unsigned i=0;i<5;++i) {
                CWeaponModeData *candidate=CWeapon::GetWeaponMode(weapon->mData->mChargeModes[i]);
                if(candidate&&candidate->mName&&!strcmp(candidate->mName,record.round_birth.mode)) { if(mode&&mode!=candidate) return false; mode=candidate; }
            }
            uint64_t mode_schema=0;
            return (mode&&mode->mRound==data&&PulseMode(weapon,mode,mode_schema)&&mode_schema==record.round_birth.mode_schema)||Fail("round weapon mode dependency differs");
        }
        CExplosionData *data=UPhysicsManager::GetExplosionData(record.definition); LifeRecord local={};
        if(!LifeDefinition(data,local)||local.definition!=record.definition||local.definition_schema!=record.definition_schema||strcmp(local.definition_name,record.definition_name))
            return Fail("compact birth resolved definition differs from baseline resources");
        CExplosionData *normal=NULL; if(!BirthCompatible(record.birth,normal)) return false;
        if(!record.birth.deep) {
            uint32_t type=THING_TYPE_THING|THING_TYPE_COMPLEX_THING|THING_TYPE_EXPLOSION;
            if(data->mDamage>3.f) type|=THING_TYPE_CAN_DESTROY_TREES;
            if(record.core.type!=type) return Fail("compact birth type differs from genuine native initialization");
        }
        return true;
    }
    static bool LifeOwner(const LifeRecord &record) {
        if(!record.dependency.id) return true;
        Binding *b=ById(record.dependency.id);
        return (b&&b->kind==ENGINE_THING&&b->initial_number&&ThingKind(b->thing)==ENGINE_THING&&
            !strcmp(b->thing->_GetClassName(),"CBattleEngine"))||Fail("compact baseline engine dependency is no longer live");
    }
    static bool InitialLifecycleContext(const CE_WorldFrame &baseline) {
        return GAME.GetCurrentLevel()==(int)config.level&&GAME.GetNPlayers()==2&&GAME.GetGameState()>=GAME_STATE_PRE_RUNNING&&
            GAME.GetGameState()<=GAME_STATE_PLAYING&&GAME.mQuit==QT_NONE&&baseline.phase<=GAME_STATE_PLAYING&&EVENT_MANAGER.IsValid()&&
            EVENT_MANAGER.GetFrameCount()>=baseline.tick&&!life_pending&&!life_creating&&!creating_explosion&&!applying_birth&&!birth_spawn&&
            !round_pending&&!creating_round&&!applying_round&&!round_spawn&&!shot_active;
    }
    static bool FrameResources(const CE_WorldFrame &frame) {
        for(unsigned i=0;i<frame.count;++i) {
            const Entity &s=frame.entities[i]; Binding *binding=ById(s.core.id);
            if(binding) { if(!Compatible(binding->thing,s)) return false; continue; }
            if(Baseline(s.core)) return Fail("deferred state changed a canonical baseline identity");
            if(s.core.id<=life_seen_id) return Fail("deferred state reuses a retired identity");
            if(s.core.kind==ROUND_THING) {
                CRoundData *data=UPhysicsManager::GetRoundData((char *)s.round.definition);
                uint64_t schema=0;
                if(!RoundDefinition(data,schema)||strcmp(s.core.class_name,"CRound")||schema!=s.round.schema||!s.round.incarnation||
                   s.round.incarnation<=life_applied||s.round.incarnation>frame.lifecycle_watermark) return Fail("deferred state projectile definition or construction differs");
            } else if(s.core.kind==EXPLOSION_THING) {
                if(frame.lifecycle_watermark<=life_applied||s.explosion.serial<=birth_applied) return Fail("deferred state has no future lifecycle construction");
                if(!s.explosion.deep) {
                    LifeRecord definition={};
                    if(!LifeDefinition(UPhysicsManager::GetExplosionData(s.explosion.definition),definition)||definition.definition_schema!=s.explosion.schema)
                        return Fail("deferred explosion definition differs");
                }
            } else return Fail("deferred state contains an unadmitted new entity family");
        }
        return true;
    }
    static bool LifeMutate(const LifeRecord &record) {
        if(!LifeOwner(record)) return false;
        ApplyScope scope;
        struct PendingScope { ~PendingScope() { life_creating=false; life_pending=NULL; } } pending_scope;
        if(record.info.kind==CE_WORLD_LIFECYCLE_BIRTH) {
            creating_explosion=true; CExplosion *explosion=UPhysicsManager::SpawnExplosion(record.definition); creating_explosion=false;
            if(!explosion) return Fail("compact native explosion allocation refused; teardown required");
            const ExplosionBirth &b=record.birth; CExplosionInitThing init; PutVec(init.mPos,b.pos); init.mOriginator=Resolve(b.originator);
            init.mBehaviour=UPhysicsManager::GetExplosionData(record.definition);
            if(!b.deep) {
                init.mColType=(ECollisionType)b.collision; init.mAllegiance=(EAllegiance)b.allegiance;
                init.mAttachedTo=NULL; init.mUseAttachedRadius=FALSE; init.mAllowVolumetric=FALSE; init.mImportant=b.important;
                init.mOrientationType=(EOrientationType)b.orientation_type;
                if(b.orientation_type==EULER_ANGLES) { init.mYaw=b.euler.v[0]; init.mPitch=b.euler.v[1]; init.mRoll=b.euler.v[2]; }
                else PutMat(init.mOrientation,b.orientation);
                init.mInitCST.mNotSeekCollisionWithBF=b.mask; init.mInitCST.mDesiredCollision=(ECollisionLevel)b.desired; init.mInitCST.mMinCollision=(ECollisionLevel)b.min_collision;
            }
            // Conventional delayed local presentation: original source tick is metadata, never a clock override.
            // Native particle/sound effects start once here and keep their own local native duration.
            // Reciprocal collision dispatch, own damage/grow and all event scheduling are replica-fenced.
            applying_birth=&b; birth_spawn=explosion; life_creating=true; life_initialized=false; life_pending=explosion;
            explosion->Init(&init); life_creating=false; applying_birth=NULL; birth_spawn=NULL;
            if(poisoned||!life_initialized) return Fail("compact native Init did not complete its controlled lifetime");
            Entity entity={}; entity.core=record.core; entity.explosion=record.explosion;
            if(!Compatible(explosion,entity)) return false;
            Binding *binding=Bind(explosion,record.info.id,0); if(!binding) return Fail("compact identity bind failed; teardown required");
            binding->birth=b; CoreApply(explosion,record.core); ExplosionApply(explosion,record.explosion,*binding);
            life_live[life_live_count++]={explosion,record.info.id,record.birth_serial,EXPLOSION_THING};
        } else if(record.info.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH) {
            CRoundData *data=UPhysicsManager::GetRoundData((char *)record.round.definition);
            creating_round=true; CRound *round=UPhysicsManager::SpawnRound(data); creating_round=false;
            if(!round) return Fail("round native allocation refused; teardown required");
            CRoundInitThing init; const RoundBirth &birth=record.round_birth;
            init.mRoundData=data; init.mLifeSpan=birth.lifespan; PutVec(init.mPos,birth.pos); PutVec(init.mVelocity,birth.velocity);
            PutVec(init.mDest,birth.destination); PutMat(init.mOrientation,birth.orientation); init.mOrientationType=DIRECTION_COSINE_MATRIX;
            init.mAllegiance=(EAllegiance)birth.allegiance; round->mUnknownEC=Resolve(record.round.launcher);
            applying_round=&record; round_spawn=round; round_pending=round; round_initialized=false;
            round->Init(&init); applying_round=NULL; round_spawn=NULL;
            if(poisoned||!round_initialized||round->mDoFullMoveCount!=1) return Fail("round native Init did not complete its controlled phase");
            Entity entity={}; entity.core=record.core; entity.round=record.round;
            if(!Compatible(round,entity)) return false;
            Binding *binding=Bind(round,record.info.id,0); if(!binding) return Fail("round identity bind failed; teardown required");
            binding->round_incarnation=record.birth_serial; binding->round_schema=record.round.schema;
            CoreApply(round,record.core); RoundApply(round,record.round);
            life_live[life_live_count++]={round,record.info.id,record.birth_serial,ROUND_THING};
        } else {
            unsigned index=0; while(index<life_live_count&&life_live[index].id!=record.info.id) ++index;
            if(index==life_live_count) return Fail("compact despawn has no controlled native object");
            CThing *thing=life_live[index].thing; life_live[index]=life_live[--life_live_count];
            if(record.info.kind==CE_WORLD_LIFECYCLE_ROUND_DESPAWN) { CoreApply(thing,record.core); RoundApply((CRound *)thing,record.round); }
            for(unsigned i=0;i<binding_count;++i) if(bindings[i].thing==thing) { bindings[i]=bindings[--binding_count]; break; }
            // Detached particle and audio resources retain their own native lifetimes. No guessed FX expiration.
            thing->Shutdown();
        }
        scope.committed=true; return true;
    }
    static bool PlanBirths(const CE_WorldFrame &f,uint32_t &next) {
        next=birth_applied;
        for(unsigned i=0;i<f.birth_count;++i) {
            const ExplosionBirth &b=f.births[i]; if(b.serial<=birth_applied) continue;
            if(b.serial!=next+1||f.tick-b.tick>kNewBirthAge) return Fail("unapplied explosion birth gap or foreground age limit exceeded");
            next=b.serial;
        }
        return true;
    }
    static bool Apply(const CE_WorldFrame &f) {
        if(poisoned) return Fail("replica is poisoned; mandatory world teardown is required");
        if (!active || config.role != CE_WORLD_REPLICA || f.epoch != config.epoch || f.level != config.level || f.tick < applied_tick || f.sequence<=applied_sequence || f.consumed_sequence<replica_ack || f.consumed_sequence>sent_sequence)
            return Fail("replica rejected stale or foreign world frame");
        if (!Validate(f)) return false;
        if(life_enabled&&LifeReadiness(f)!=CE_WORLD_READY) return Fail("world state cannot overtake compact lifecycle operations");
        // Validate every existing object, asset and relationship before mutating any game object.
        CThing *targets[kEntities] = {}; CExplosionData *birth_definitions[kBirths]={};
        uint32_t next_birth=0; if(!PlanBirths(f,next_birth)) return false;
        for(unsigned i=0;i<f.birth_count;++i) if(f.births[i].serial>birth_applied&&!BirthCompatible(f.births[i],birth_definitions[i])) return false;
        for (unsigned i = 0; i < f.count; ++i) {
            const Entity &s = f.entities[i]; Binding *b = applied_tick ? ById(s.core.id) : NULL;
            targets[i] = b ? b->thing : Baseline(s.core);
            if(life_enabled&&targets[i]&&Id(targets[i])!=s.core.id) return Fail("world state changed a canonical identity");
            if (!targets[i] && s.core.kind != ROUND_THING && s.core.kind!=EXPLOSION_THING) return Fail("new non-projectile entity requires a codec not yet admitted");
            if (targets[i] && !Compatible(targets[i], s)) return false;
            if (s.core.kind == ROUND_THING) {
                if(!life_enabled||!targets[i]) return Fail("full state cannot construct a projectile without its committed lifecycle incarnation");
                CRoundData *definition = UPhysicsManager::GetRoundData((char *)s.round.definition);
                if (!definition || (targets[i] && ((CRound *)targets[i])->mData != definition)) return Fail("unknown or changed projectile definition");
                if(strcmp(s.core.class_name,definition->mUnknown70?"CMissile":"CRound")) return Fail("projectile class differs from its genuine factory");
            }
            if(life_enabled&&s.core.kind==EXPLOSION_THING&&!targets[i]) return Fail("compact live explosion was not constructed by its operation");
            if(s.core.kind==EXPLOSION_THING&&!targets[i]) {
                bool found=false;
                for(unsigned b=0;b<f.birth_count;++b) if(f.births[b].serial==s.explosion.serial&&f.births[b].id==s.core.id&&f.births[b].serial>birth_applied&&
                    f.births[b].deep==s.explosion.deep&&f.births[b].definition==s.explosion.definition&&f.births[b].schema==s.explosion.schema) found=true;
                if(!found) return Fail("new live explosion has no unapplied authoritative birth");
            }
            for (unsigned j = 0; j < i; ++j) if (targets[i] && targets[i] == targets[j]) return Fail("two IDs claim the same live object");
        }
        for(unsigned i=0;i<f.birth_count;++i) if(f.births[i].serial>birth_applied) for(unsigned j=0;j<f.count;++j) if(f.entities[j].core.id==f.births[i].id) {
            const ExplosionState &e=f.entities[j].explosion; const ExplosionBirth &b=f.births[i];
            if(targets[j]||f.entities[j].core.kind!=EXPLOSION_THING||e.serial!=b.serial||e.definition!=b.definition||e.schema!=b.schema||e.deep!=b.deep||
                e.allegiance!=b.allegiance||e.radius!=b.radius) return Fail("new explosion birth conflicts with a live identity or immutable state");
        }
        if(life_enabled) for(unsigned i=0;i<life_live_count;++i) {
            bool found=false; for(unsigned j=0;j<f.count;++j) {
                const Entity &entity=f.entities[j];
                if(entity.core.id==life_live[i].id&&entity.core.kind==life_live[i].kind&&
                   (entity.core.kind==EXPLOSION_THING?entity.explosion.serial:entity.round.incarnation)==life_live[i].birth_serial) found=true;
            }
            if(!found) return Fail("full state omitted a compact object before its explicit despawn");
        }
        // Creation uses the game's genuine factory and Init; replica scheduling is suppressed throughout.
        ApplyScope scope;
        for(unsigned i=0;i<f.birth_count;++i) if(f.births[i].serial>birth_applied) {
            const ExplosionBirth &b=f.births[i]; unsigned live=f.count;
            for(unsigned j=0;j<f.count;++j) if(f.entities[j].core.id==b.id) { live=j; break; }
            if(live<f.count&&targets[live]) return Fail("new explosion birth reuses a live identity; replica requires teardown");
            CExplosion *explosion=CreateExplosion(f,targets,b,birth_definitions[i]); if(!explosion) return false;
            if(live<f.count) {
                targets[live]=explosion; if(!Compatible(explosion,f.entities[live])) return false;
            } else explosion->Shutdown(); // native temporary blast; its detached particle effect owns its own lifetime
        }
        // Replace the ID table only after all objects exist. References resolve against this complete frame.
        Binding old[kEntities]; unsigned old_count = binding_count; memcpy(old, bindings, sizeof old); binding_count = 0;
        for (unsigned i = 0; i < f.count; ++i) Bind(targets[i], f.entities[i].core.id, f.entities[i].core.initial_number);
        for (unsigned i = 0; i < f.count; ++i) {
            const Entity &s = f.entities[i]; CoreApply(targets[i], s.core);
            if (s.core.kind == ENGINE_THING&&!EngineApply((CBattleEngine *)targets[i], s.engine)) return false;
            if (s.core.kind == ROUND_THING) RoundApply((CRound *)targets[i], s.round);
            if (s.core.kind == CANNON_THING) CannonApply((CCannon *)targets[i],s.cannon);
            if (s.core.kind == FEATURE_THING) FeatureApply((CFeature *)targets[i],s.feature);
            if (s.core.kind == BUILDING_THING && !BuildingApply((CBuilding *)targets[i],s.building)) return false;
            if (s.core.kind == EXPLOSION_THING) ExplosionApply((CExplosion *)targets[i],s.explosion,*ById(s.core.id));
        }
        for (unsigned i = 0; i < old_count; ++i) if (!ByThing(old[i].thing)) old[i].thing->Shutdown();
        GAME.mGameState = (EGameState)f.phase; GAME.mPause = f.paused; GAME.mScore = f.score; GAME.mPlayer1Lives = f.lives[0]; GAME.mPlayer2Lives = f.lives[1];
        for(unsigned i=0;i<NUM_WORLD_COUNTERS;++i) { WORLD.mCountersA[i]=f.counters_a[i]; WORLD.mCountersB[i]=f.counters_b[i]; }
        ApplyObjectives(f);
        GAME.mEndLevelCount=f.end_count; GAME.mFadeOut=f.fade; GAME.mWinLoseScreenAlpha=f.alpha;
        if((bool)GAME.mPausedAllGameSounds!=(bool)f.paused) {
            if(f.paused) SOUND.PauseAllSamples(); else SOUND.UnPauseAllSamples();
            GAME.mPausedAllGameSounds=f.paused;
        }
        EVENT_MANAGER.mFrameCount = f.tick; EVENT_MANAGER.mTime = f.tick * CLOCK_TICK;
        GAME.mUpdateFrameNumber=f.tick;
        if(life_enabled) { life_committed_tick=f.tick; for(unsigned i=0;i<f.count;++i) if(f.entities[i].core.id>life_seen_id) life_seen_id=f.entities[i].core.id; }
        applied_tick = f.tick; applied_sequence=f.sequence; replica_ack=f.consumed_sequence; birth_applied=next_birth; scope.committed=true; return true;
    }
    static int Flush(unsigned player) {
        if (!active) return 0;
        if (config.role == CE_WORLD_REPLICA) return 1;
        if (player != 1) return 0;
        uint32_t tick = EVENT_MANAGER.GetFrameCount(); CE_WorldInput in;
        if(!commands.Peek(tick,in)) return 1; // missing input is neutral, never a repeated fire edge
        CPlayer *p = GAME.GetPlayer(1); CBattleEngine *e = p ? p->GetBattleEngine() : NULL;
        if(!e||GAME.IsPaused()||GAME.GetController(1)->GetToControl()!=p) return 1;
        remote_invert_ground=in.invert_ground; remote_invert_jet=in.invert_jet; remote_walker_any=in.walker_mouse_any;
        bool ground_mouse=in.mouse_ground_aim&&e->IsWalkerOnGroundOrObject();
        for (unsigned i = 0; i < in.count; ++i) {
            uint32_t button=in.actions[i].button;
            if(ground_mouse&&in.actions[i].source==CE_WORLD_ACTION_MOUSE_AXIS&&button>=25&&button<=28) continue;
            p->ReceiveButtonAction(GAME.mController[1],button,in.actions[i].value);
        }
        commands.Consumed(); // acknowledge delivery through the real player receiver, not receipt or prediction
        // A dispatched action can morph this engine; only its current walker/ground state admits mouse look.
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
        return 1;
    }
    static void PollLocal() {
        if(!active||poisoned||input_suspension||config.role!=CE_WORLD_REPLICA||!FreshTick(applied_tick,sampled_tick)||GAME.GetGameState()!=GAME_STATE_PLAYING||GAME.IsPaused()||local_input.failed) return;
        double now_ms=(double)PLATFORM.GetSysTimeFloat()*1000.;
        if(!(now_ms>=0.&&now_ms<1000000000000.)) { local_input.failed=true; Fail("local sampler clock is invalid"); return; }
        if(!local_clock.Ticket((uint64_t)now_ms)) return;
        if(local_input.count==kInputs) { local_input.failed=true; Fail("local input FIFO capacity exhausted; session requires teardown"); return; }
        CController *controller=GAME.GetController(0);
        if(!controller) { local_input.failed=true; Fail("local player controller is unavailable"); return; }
        if(controller->GetToControl()!=GAME.GetPlayer(0)) {
            MOUSE_X=PLATFORM.GetWindowWidth()/2; MOUSE_Y=PLATFORM.GetWindowHeight()/2; return;
        }
        int pad=controller->GetPadNumber(); if(pad!=0||controller->mPlaying||controller->mRecording) { local_input.failed=true; Fail("local physical controller binding or playback state is unsupported"); return; }
        memset(&sample,0,sizeof sample); sampling=true; sampling_failed=false; mouse_axis=false;
        sample.last_applied_birth_serial=birth_applied; sample.epoch=config.epoch; sample.sequence=generated_sequence+1; sample.tick=applied_tick+1; sample.player=1;
        sample.invert_ground=!!CAREER.GetInvertYAxis(0); sample.invert_jet=!!CAREER.GetInvertJetYAxis(0);
        PollMouseState();
        SControlBinding *yaw=FindControlBinding(BUTTON_MECH_YAW_RIGHT);
        sample.walker_mouse_any=yaw&&(yaw->mInput[pad].mType==MOUSE_AXIS_PLUS_ANY||yaw->mInput[pad].mType==MOUSE_AXIS_MINUS_ANY);
        CPlayer *p=GAME.GetPlayer(1); CBattleEngine *e=p?p->GetBattleEngine():NULL;
        sample.mouse_ground_aim=e&&!e->IsDying()&&e->GetIsPoweredUp()&&e->GetState()==BATTLE_ENGINE_STATE_WALKER&&e->IsWalkerOnGroundOrObject()&&yaw&&(yaw->mInput[pad].mType==MOUSE_AXIS_PLUS||yaw->mInput[pad].mType==MOUSE_AXIS_MINUS);
        if(sample.mouse_ground_aim) {
            sample.ground_yaw=-(MOUSE_X-PLATFORM.GetWindowWidth()/2)*(gMouseSensitivity*.002f);
            sample.ground_pitch=-(MOUSE_Y-PLATFORM.GetWindowHeight()/2)*(gMouseSensitivity*.001f);
            sample.ground_yaw=fmodf(sample.ground_yaw,6.2831853f);
            if(sample.ground_yaw>3.1415927f) sample.ground_yaw-=6.2831853f;
            if(sample.ground_yaw< -3.1415927f) sample.ground_yaw+=6.2831853f;
            if(sample.ground_pitch>3.1415927f) sample.ground_pitch=3.1415927f;
            if(sample.ground_pitch< -3.1415927f) sample.ground_pitch=-3.1415927f;
        }
        BOOL locked=gMouseAxisLocked[pad]; gMouseAxisLocked[pad]=FALSE;
        controller->Flush(); gMouseAxisLocked[pad]=locked; sampling=false; mouse_axis=false;
        if(sample.mouse_ground_aim) { MOUSE_X=PLATFORM.GetWindowWidth()/2; MOUSE_Y=PLATFORM.GetWindowHeight()/2; }
        UpdateMouseCentre(false); LT.ClearKeyWasUp();
        if(sampling_failed||!ValidInput(sample)||!local_input.Poll(sample)) {
            local_input.failed=true; Fail("local semantic sample cannot be retained exactly"); return;
        }
        generated_sequence=sample.sequence;
        sampled_tick=applied_tick;
    }
    static int SampleInput(CE_WorldInput *out) {
        if(!out||!active||poisoned||config.role!=CE_WORLD_REPLICA) { Fail("sampling requires a healthy admitted replica"); return -1; }
        int ready=local_input.Peek(*out);
        if(ready<0) Fail("local input FIFO refused; no semantic sample was discarded silently");
        return ready;
    }
    static int InputSent(uint32_t sequence) {
        if(!active||poisoned||config.role!=CE_WORLD_REPLICA||local_input.Sent(sequence)!=1) { Fail("input send acknowledgement does not match the leased front"); return -1; }
        sent_sequence=sequence; return 1;
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
        out.walker_mouse_any=yaw&&(yaw->mInput[pad].mType==MOUSE_AXIS_PLUS_ANY||yaw->mInput[pad].mType==MOUSE_AXIS_MINUS_ANY);
        CPlayer *p=GAME.GetPlayer(local_player); CBattleEngine *e=p?p->GetBattleEngine():NULL;
        out.mouse_ground_aim=e&&!e->IsDying()&&e->GetIsPoweredUp()&&e->GetState()==BATTLE_ENGINE_STATE_WALKER&&e->IsWalkerOnGroundOrObject()&&yaw&&(yaw->mInput[pad].mType==MOUSE_AXIS_PLUS||yaw->mInput[pad].mType==MOUSE_AXIS_MINUS);
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
    // The host's Flush for its guest, for either player: the same receiver, mouse-look and pitch-limit arithmetic.
    static void LockstepApply(uint32_t player,const CE_WorldInput &in) {
        CPlayer *p=GAME.GetPlayer(player); CBattleEngine *e=p?p->GetBattleEngine():NULL;
        if(!e||GAME.IsPaused()||GAME.GetController(player)->GetToControl()!=p) return;
        bool ground_mouse=in.mouse_ground_aim&&e->IsWalkerOnGroundOrObject();
        for(unsigned i=0;i<in.count;++i) {
            uint32_t button=in.actions[i].button;
            if(ground_mouse&&in.actions[i].source==CE_WORLD_ACTION_MOUSE_AXIS&&button>=25&&button<=28) continue;
            p->ReceiveButtonAction(GAME.GetController(player),button,in.actions[i].value);
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
    // Each thing's selected fields, in world-list order, in a nonstandard-offset FNV-1a-style digest; collisions are possible.
    static uint64_t LockstepThings(FILE *dump,uint32_t &count) {
        uint64_t all=1469598103934665603ull; count=0;
        auto mix=[](uint64_t h,const void *p,size_t n) { const uint8_t *b=(const uint8_t *)p; for(size_t i=0;i<n;++i) { h^=b[i]; h*=1099511628211ull; } return h; };
        ListIterator<CThing> it(&WORLD.GetThingNB());
        for(CThing *t=it.First();t;t=it.Next()) {
            ++count; const char *name=t->_GetClassName(); uint64_t one=1469598103934665603ull;
            one=mix(one,name,strlen(name)+1); one=mix(one,&t->mPos.X,4); one=mix(one,&t->mPos.Y,4); one=mix(one,&t->mPos.Z,4);
            CUnit *unit=dynamic_cast<CUnit *>(t); float life=unit?unit->mLife:0.f;
            if(unit) one=mix(one,&life,4);
            // orientation and (actors) velocity: drift there precedes any position difference
            uint64_t motion=1469598103934665603ull;
            auto xyz=[&](uint64_t h,const FVector &v) { h=mix(h,&v.X,4); h=mix(h,&v.Y,4); return mix(h,&v.Z,4); };	// never the unused pad
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
    static ComponentState lock_components[256]; static unsigned lock_component_count;
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
            if(lock_component_count==256) return 0;
            lock_components[lock_component_count++]={c,c->mPos,c->mOrientation,c->mUnknown278};
        }
        return 1;
    }
    static void LockstepBeginTick() {
        size_t at=0;
        for(size_t i=0;i<lock_region_count;++i) { memcpy(lock_regions[i].at,lock_bytes+at,lock_regions[i].size); at+=lock_regions[i].size; }
        for(unsigned i=0;i<lock_component_count;++i) {
            ComponentState &c=lock_components[i]; c.thing->mPos=c.pos; c.thing->mOrientation=c.orientation; c.thing->mUnknown278=c.stamp;
        }
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
        for(int p=0;p<2;++p) {
            CPlayer *player=GAME.GetPlayer(p); CBattleEngine *e=player?player->GetBattleEngine():NULL;
            if(!e) continue;
            ListIterator<CWeapon> it(&e->mWeapons);
            for(CWeapon *w=it.First();w;w=it.Next())
                for(unsigned i=0;i<count;++i) if(weapons[i]==w) w->MoveEmitter(orientate[i]);
        }
    }
    static CE_WorldInput *lock_sample; static bool lock_mouse_axis;
};
CENetworkWorldAdapter::Region *CENetworkWorldAdapter::lock_regions; uint8_t *CENetworkWorldAdapter::lock_bytes;
size_t CENetworkWorldAdapter::lock_region_count, CENetworkWorldAdapter::lock_region_cap, CENetworkWorldAdapter::lock_byte_count, CENetworkWorldAdapter::lock_byte_cap;
bool CENetworkWorldAdapter::lock_overflow;
CENetworkWorldAdapter::ComponentState CENetworkWorldAdapter::lock_components[256]; unsigned CENetworkWorldAdapter::lock_component_count;
CE_WorldInput *CENetworkWorldAdapter::lock_sample; bool CENetworkWorldAdapter::lock_mouse_axis;

namespace {
void WriteCore(CE_NetWriter *w, const Core &c) {
    CE_NetWriteU32(w,c.id); CE_NetWriteU8(w,c.kind); CE_NetWriteU32(w,c.initial_number); CE_NetWriteU8(w,c.player);
    CE_NetWriteU32(w,c.type); CE_NetWriteU16(w,c.flags); WriteName(w,c.class_name); WriteName(w,c.mesh);
    WriteVec(w,c.pos); WriteVec(w,c.old_pos); WriteVec(w,c.velocity); WriteMat(w,c.orientation); WriteMat(w,c.old_orientation);
    CE_NetWriteF32(w,c.last_move); CE_NetWriteF32(w,c.ground); CE_NetWriteF32(w,c.water); CE_NetWriteF32(w,c.object);
    CE_NetWriteU8(w,c.animation.present);
    if (c.animation.present) { CE_NetWriteU32(w,c.animation.mode); CE_NetWriteU32(w,c.animation.index); CE_NetWriteU8(w,c.animation.looped);
        CE_NetWriteF32(w,c.animation.frame); CE_NetWriteF32(w,c.animation.old_frame); CE_NetWriteF32(w,c.animation.increment); }
}
void ReadCore(CE_NetReader *r, Core &c) {
    c.id=CE_NetReadU32(r); c.kind=CE_NetReadU8(r); c.initial_number=CE_NetReadU32(r); c.player=CE_NetReadU8(r);
    c.type=CE_NetReadU32(r); c.flags=CE_NetReadU16(r); ReadName(r,c.class_name); ReadName(r,c.mesh);
    ReadVec(r,c.pos); ReadVec(r,c.old_pos); ReadVec(r,c.velocity); ReadMat(r,c.orientation); ReadMat(r,c.old_orientation);
    c.last_move=CE_NetReadF32(r); c.ground=CE_NetReadF32(r); c.water=CE_NetReadF32(r); c.object=CE_NetReadF32(r);
    c.animation.present=CE_NetReadU8(r);
    if (c.animation.present) { c.animation.mode=CE_NetReadU32(r); c.animation.index=CE_NetReadU32(r); c.animation.looped=CE_NetReadU8(r);
        c.animation.frame=CE_NetReadF32(r); c.animation.old_frame=CE_NetReadF32(r); c.animation.increment=CE_NetReadF32(r); }
}
void WriteCockpit(CE_NetWriter *w,const CockpitState &c) {
    WriteName(w,c.mesh); CE_NetWriteU64(w,c.schema); CE_NetWriteU8(w,c.should_render); CE_NetWriteU32(w,c.mode); CE_NetWriteU32(w,c.index);
    WriteVec(w,c.shake); WriteVec(w,c.old_shake); WriteMat(w,c.orientation); WriteMat(w,c.old_orientation);
    CE_NetWriteF32(w,c.frame); CE_NetWriteF32(w,c.old_frame);
}
void ReadCockpit(CE_NetReader *r,CockpitState &c) {
    ReadName(r,c.mesh); c.schema=CE_NetReadU64(r); c.should_render=CE_NetReadU8(r); c.mode=CE_NetReadU32(r); c.index=CE_NetReadU32(r);
    ReadVec(r,c.shake); ReadVec(r,c.old_shake); ReadMat(r,c.orientation); ReadMat(r,c.old_orientation);
    c.frame=CE_NetReadF32(r); c.old_frame=CE_NetReadF32(r);
}
void WriteEngine(CE_NetWriter *w,const EngineState &e) {
    WriteCockpit(w,e.cockpit);
    CE_NetWriteF32(w,e.life); CE_NetWriteF32(w,e.energy); CE_NetWriteF32(w,e.shields); CE_NetWriteF32(w,e.max_shields); CE_NetWriteU32(w,e.hit); CE_NetWriteU32(w,e.hit_decay); WriteVec(w,e.euler); WriteVec(w,e.old_euler);
    CE_NetWriteU32(w,e.configuration); CE_NetWriteU32(w,e.allegiance); CE_NetWriteU32(w,e.crosshair); CE_NetWriteU32(w,e.crosshair_range); CE_NetWriteU32(w,e.auto_aim); CE_NetWriteU32(w,e.standing_on);
#define WRITE_FLOAT(name) CE_NetWriteF32(w,e.name);
    ENGINE_FLOATS(WRITE_FLOAT)
#undef WRITE_FLOAT
#define WRITE_INT(name) CE_NetWriteU32(w,e.name);
    ENGINE_INTS(WRITE_INT)
#undef WRITE_INT
    for(unsigned i=0;i<6;++i) { CE_NetWriteF32(w,e.stores[i]); CE_NetWriteU8(w,e.store_heat[i]); CE_NetWriteU8(w,e.store_overheat[i]); }
    CE_NetWriteU32(w,e.walker_weapon); CE_NetWriteU32(w,e.jet_weapon); CE_NetWriteU8(w,e.weapon_count); CE_NetWriteU8(w,e.lock_count); CE_NetWriteU8(w,e.fired_count); CE_NetWriteU16(w,e.part_count);
    CE_NetWriteF32(w,e.walk_cycle); CE_NetWriteF32(w,e.old_walk_cycle); CE_NetWriteF32(w,e.jet_thrust);
    for(unsigned i=0;i<e.weapon_count;++i) { const WeaponState &s=e.weapons[i]; WriteName(w,s.name); WriteName(w,s.mode); CE_NetWriteU32(w,s.emitter); CE_NetWriteU32(w,s.mount);
        CE_NetWriteU32(w,s.target); CE_NetWriteU8(w,s.active); CE_NetWriteF32(w,s.charge); CE_NetWriteF32(w,s.next_fire); CE_NetWriteU32(w,s.burst); CE_NetWriteU32(w,s.charged); CE_NetWriteU32(w,s.launch); CE_NetWriteU32(w,s.angle); }
    for(unsigned list=0;list<2;++list) for(unsigned i=0;i<(list?e.fired_count:e.lock_count);++i) { const LockState &s=(list?e.fired:e.locks)[i]; CE_NetWriteU32(w,s.target); CE_NetWriteU8(w,s.locked); CE_NetWriteU8(w,s.direct); CE_NetWriteF32(w,s.start); CE_NetWriteF32(w,s.finish); }
    for(unsigned i=0;i<e.part_count;++i) { const Pose &p=e.parts[i]; WriteVec(w,p.pos); WriteVec(w,p.old_pos); WriteMat(w,p.orientation); WriteMat(w,p.old_orientation); CE_NetWriteF32(w,p.frame); CE_NetWriteF32(w,p.old_frame); }
    CE_NetWriteU8(w,e.view); CE_NetWriteU8(w,e.preferred_view); for(uint32_t stat:e.stats) CE_NetWriteU32(w,stat);
    WriteVec(w,e.camera.pos); WriteVec(w,e.camera.old_pos); WriteMat(w,e.camera.orientation); WriteMat(w,e.camera.old_orientation);
    CE_NetWriteF32(w,e.camera.zoom); CE_NetWriteF32(w,e.camera.old_zoom); CE_NetWriteU8(w,e.camera.hud);
    for(float value:e.tracking) CE_NetWriteF32(w,value);
    CE_NetWriteU8(w,e.radar_present); CE_NetWriteU8(w,e.new_threat); CE_NetWriteF32(w,e.new_threat_time);
    CE_NetWriteU8(w,e.threat_count); CE_NetWriteU8(w,e.flash_count);
    for(unsigned i=0;i<e.threat_count;++i) { const ThreatState &t=e.threats[i]; CE_NetWriteU32(w,t.round); CE_NetWriteU8(w,t.seen); CE_NetWriteF32(w,t.yaw); CE_NetWriteF32(w,t.distance); CE_NetWriteF32(w,t.time); }
    for(unsigned i=0;i<e.flash_count;++i) { CE_NetWriteF32(w,e.flashes[i].yaw); CE_NetWriteF32(w,e.flashes[i].start); }
}
void ReadEngine(CE_NetReader *r,EngineState &e) {
    ReadCockpit(r,e.cockpit);
    e.life=CE_NetReadF32(r); e.energy=CE_NetReadF32(r); e.shields=CE_NetReadF32(r); e.max_shields=CE_NetReadF32(r); e.hit=(int32_t)CE_NetReadU32(r); e.hit_decay=(int32_t)CE_NetReadU32(r); ReadVec(r,e.euler); ReadVec(r,e.old_euler);
    e.configuration=CE_NetReadU32(r); e.allegiance=CE_NetReadU32(r); e.crosshair=CE_NetReadU32(r); e.crosshair_range=CE_NetReadU32(r); e.auto_aim=CE_NetReadU32(r); e.standing_on=CE_NetReadU32(r);
#define READ_FLOAT(name) e.name=CE_NetReadF32(r);
    ENGINE_FLOATS(READ_FLOAT)
#undef READ_FLOAT
#define READ_INT(name) e.name=CE_NetReadU32(r);
    ENGINE_INTS(READ_INT)
#undef READ_INT
    for(unsigned i=0;i<6;++i) { e.stores[i]=CE_NetReadF32(r); e.store_heat[i]=CE_NetReadU8(r); e.store_overheat[i]=CE_NetReadU8(r); }
    e.walker_weapon=CE_NetReadU32(r); e.jet_weapon=CE_NetReadU32(r); e.weapon_count=CE_NetReadU8(r); e.lock_count=CE_NetReadU8(r); e.fired_count=CE_NetReadU8(r); e.part_count=CE_NetReadU16(r);
    e.walk_cycle=CE_NetReadF32(r); e.old_walk_cycle=CE_NetReadF32(r); e.jet_thrust=CE_NetReadF32(r);
    if(e.weapon_count>kWeapons||e.lock_count>kLocks||e.fired_count>kLocks||e.part_count>kParts) { r->error=1; return; }
    for(unsigned i=0;i<e.weapon_count;++i) { WeaponState &s=e.weapons[i]; ReadName(r,s.name); ReadName(r,s.mode); s.emitter=CE_NetReadU32(r); s.mount=CE_NetReadU32(r);
        s.target=CE_NetReadU32(r); s.active=CE_NetReadU8(r); s.charge=CE_NetReadF32(r); s.next_fire=CE_NetReadF32(r); s.burst=CE_NetReadU32(r); s.charged=CE_NetReadU32(r); s.launch=CE_NetReadU32(r); s.angle=CE_NetReadU32(r); }
    for(unsigned list=0;list<2;++list) for(unsigned i=0;i<(list?e.fired_count:e.lock_count);++i) { LockState &s=(list?e.fired:e.locks)[i]; s.target=CE_NetReadU32(r); s.locked=CE_NetReadU8(r); s.direct=CE_NetReadU8(r); s.start=CE_NetReadF32(r); s.finish=CE_NetReadF32(r); }
    for(unsigned i=0;i<e.part_count;++i) { Pose &p=e.parts[i]; ReadVec(r,p.pos); ReadVec(r,p.old_pos); ReadMat(r,p.orientation); ReadMat(r,p.old_orientation); p.frame=CE_NetReadF32(r); p.old_frame=CE_NetReadF32(r); }
    e.view=CE_NetReadU8(r); e.preferred_view=CE_NetReadU8(r); for(uint32_t &stat:e.stats) stat=CE_NetReadU32(r);
    ReadVec(r,e.camera.pos); ReadVec(r,e.camera.old_pos); ReadMat(r,e.camera.orientation); ReadMat(r,e.camera.old_orientation);
    e.camera.zoom=CE_NetReadF32(r); e.camera.old_zoom=CE_NetReadF32(r); e.camera.hud=CE_NetReadU8(r);
    for(float &value:e.tracking) value=CE_NetReadF32(r);
    e.radar_present=CE_NetReadU8(r); e.new_threat=CE_NetReadU8(r); e.new_threat_time=CE_NetReadF32(r);
    e.threat_count=CE_NetReadU8(r); e.flash_count=CE_NetReadU8(r);
    if(e.threat_count>kThreats||e.flash_count>kFlashes) { r->error=1; return; }
    for(unsigned i=0;i<e.threat_count;++i) { ThreatState &t=e.threats[i]; t.round=CE_NetReadU32(r); t.seen=CE_NetReadU8(r); t.yaw=CE_NetReadF32(r); t.distance=CE_NetReadF32(r); t.time=CE_NetReadF32(r); }
    for(unsigned i=0;i<e.flash_count;++i) { e.flashes[i].yaw=CE_NetReadF32(r); e.flashes[i].start=CE_NetReadF32(r); }
}
}

namespace {
void WriteBirth(CE_NetWriter *w,const ExplosionBirth &b) {
    CE_NetWriteU32(w,b.serial); CE_NetWriteU32(w,b.id); CE_NetWriteU32(w,b.tick); CE_NetWriteU8(w,b.deep); CE_NetWriteU8(w,b.ready); CE_NetWriteU32(w,b.originator);
    if(b.deep) { WriteVec(w,b.pos); return; }
    CE_NetWriteU16(w,b.definition); CE_NetWriteU64(w,b.schema); WriteName(w,b.name); CE_NetWriteU32(w,b.attached);
    CE_NetWriteU8(w,b.use_radius); CE_NetWriteU8(w,b.volumetric); CE_NetWriteU8(w,b.important); CE_NetWriteU8(w,b.collision); CE_NetWriteU8(w,b.allegiance);
    CE_NetWriteU8(w,b.orientation_type); WriteVec(w,b.pos);
    if(b.orientation_type==EULER_ANGLES) WriteVec(w,b.euler); else WriteMat(w,b.orientation);
    CE_NetWriteF32(w,b.radius); CE_NetWriteU32(w,b.mask); CE_NetWriteU8(w,b.desired); CE_NetWriteU8(w,b.min_collision); CE_NetWriteU8(w,b.map_who);
}
void ReadBirth(CE_NetReader *r,ExplosionBirth &b) {
    b.serial=CE_NetReadU32(r); b.id=CE_NetReadU32(r); b.tick=CE_NetReadU32(r); b.deep=CE_NetReadU8(r); b.ready=CE_NetReadU8(r); b.originator=CE_NetReadU32(r);
    b.orientation={{1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f}};
    if(b.deep) { ReadVec(r,b.pos); return; }
    b.definition=CE_NetReadU16(r); b.schema=CE_NetReadU64(r); ReadName(r,b.name); b.attached=CE_NetReadU32(r);
    b.use_radius=CE_NetReadU8(r); b.volumetric=CE_NetReadU8(r); b.important=CE_NetReadU8(r); b.collision=CE_NetReadU8(r); b.allegiance=CE_NetReadU8(r);
    b.orientation_type=CE_NetReadU8(r); ReadVec(r,b.pos);
    if(b.orientation_type==EULER_ANGLES) ReadVec(r,b.euler); else ReadMat(r,b.orientation);
    b.radius=CE_NetReadF32(r); b.mask=CE_NetReadU32(r); b.desired=CE_NetReadU8(r); b.min_collision=CE_NetReadU8(r); b.map_who=CE_NetReadU8(r);
}
void WriteExplosion(CE_NetWriter *w,const ExplosionState &e) {
    CE_NetWriteU32(w,e.serial); CE_NetWriteU8(w,e.deep); CE_NetWriteU32(w,e.originator);
    if(!e.deep) { CE_NetWriteU16(w,e.definition); CE_NetWriteU64(w,e.schema); CE_NetWriteU8(w,e.allegiance); CE_NetWriteF32(w,e.radius); CE_NetWriteF32(w,e.current_radius); }
}
void ReadExplosion(CE_NetReader *r,ExplosionState &e) {
    e.serial=CE_NetReadU32(r); e.deep=CE_NetReadU8(r); e.originator=CE_NetReadU32(r);
    if(!e.deep) { e.definition=CE_NetReadU16(r); e.schema=CE_NetReadU64(r); e.allegiance=CE_NetReadU8(r); e.radius=CE_NetReadF32(r); e.current_radius=CE_NetReadF32(r); }
}
void WriteRound(CE_NetWriter *w,const RoundState &r) {
    CE_NetWriteU32(w,r.incarnation); CE_NetWriteU64(w,r.schema); WriteName(w,r.definition);
    CE_NetWriteU32(w,r.target); CE_NetWriteU32(w,r.launcher); CE_NetWriteU8(w,r.allegiance); CE_NetWriteU32(w,r.jumps);
    CE_NetWriteU8(w,r.armed); CE_NetWriteU32(w,r.special); CE_NetWriteU8(w,r.no_gravity); CE_NetWriteU8(w,r.incoming_member);
    CE_NetWriteF32(w,r.start); WriteVec(w,r.beam_start); WriteVec(w,r.destination);
}
void ReadRound(CE_NetReader *r,RoundState &out) {
    out.incarnation=CE_NetReadU32(r); out.schema=CE_NetReadU64(r); ReadName(r,out.definition);
    out.target=CE_NetReadU32(r); out.launcher=CE_NetReadU32(r); out.allegiance=CE_NetReadU8(r); out.jumps=CE_NetReadU32(r);
    out.armed=CE_NetReadU8(r); out.special=CE_NetReadU32(r); out.no_gravity=CE_NetReadU8(r); out.incoming_member=CE_NetReadU8(r);
    out.start=CE_NetReadF32(r); ReadVec(r,out.beam_start); ReadVec(r,out.destination);
}
void WriteRoundBirth(CE_NetWriter *w,const RoundBirth &b) {
    CE_NetWriteU32(w,b.shot); CE_NetWriteU8(w,b.inventory); CE_NetWriteU8(w,b.index); CE_NetWriteU8(w,b.allegiance); CE_NetWriteU8(w,b.actor_phase);
    CE_NetWriteU64(w,b.schema); CE_NetWriteU64(w,b.mode_schema); WriteName(w,b.weapon); WriteName(w,b.mode);
    WriteVec(w,b.pos); WriteVec(w,b.velocity); WriteVec(w,b.destination); WriteMat(w,b.orientation); CE_NetWriteF32(w,b.lifespan);
}
void ReadRoundBirth(CE_NetReader *r,RoundBirth &out) {
    out.shot=CE_NetReadU32(r); out.inventory=CE_NetReadU8(r); out.index=CE_NetReadU8(r); out.allegiance=CE_NetReadU8(r); out.actor_phase=CE_NetReadU8(r);
    out.schema=CE_NetReadU64(r); out.mode_schema=CE_NetReadU64(r); ReadName(r,out.weapon); ReadName(r,out.mode);
    ReadVec(r,out.pos); ReadVec(r,out.velocity); ReadVec(r,out.destination); ReadMat(r,out.orientation); out.lifespan=CE_NetReadF32(r);
}

// Compact records carry original source identity and entry/initialized fields; no native memory or FX TTL.
// Selected round records are exact CRound only. Shot IDs associate an admitted native weapon scope, not sound
// or muzzle replay. Explicit despawn kills the native round-owned trail; detached impact FX retain local lifetime.
void LifeReset() {
    life_enabled=life_failed=life_creating=life_initialized=false; life_pending=NULL;
    round_pending=round_spawn=NULL; applying_round=NULL; creating_round=round_initialized=false;
    shot_weapon=NULL; shot_mode=NULL; shot_generated=shot_active=shot_inventory=shot_index=last_round_shot=0;
    memset(&round_pending_record,0,sizeof round_pending_record);
    life_baseline_size=life_baseline=life_baseline_tick=life_generated=life_published=life_ack=life_applied=life_tick=life_ordinal=life_seen_id=life_committed_tick=life_applied_ordinal=0;
    life_first=life_count=life_duplicate_first=life_duplicate_count=life_retired_first=life_retired_count=life_key_count=life_live_count=0;
    life_proof_valid=false; life_proof_sequence=life_proof_tick=0;
    memset(life_journal,0,sizeof life_journal); memset(life_duplicates,0,sizeof life_duplicates);
    memset(life_retired,0,sizeof life_retired); memset(life_keys,0,sizeof life_keys); memset(life_live,0,sizeof life_live);
    memset(&life_pending_record,0,sizeof life_pending_record);
}
void LifeInvalidateProof() {
    life_proof_valid=false; life_proof_sequence=life_proof_tick=0;
    // Once started, these bytes are also the immutable owned baseline. Invalidation never clears its ownership.
    if(!life_enabled) life_baseline_size=0;
}
bool LifeRecordProof(const CE_WorldFrame &frame) {
    if(life_enabled) return true;
    LifeInvalidateProof(); uint32_t size=0;
    if(!CE_WorldEncode(&frame,life_baseline_bytes,sizeof life_baseline_bytes,&size)) return false;
    // One bounded candidate: latest successful pre-Start local publication or whole Apply, never Capture/Send0.
    life_baseline_size=size; life_proof_sequence=frame.sequence; life_proof_tick=frame.tick; life_proof_valid=true;
    return true;
}
bool LifeSame(const CE_WorldLifecycleInfo &a,const CE_WorldLifecycleInfo &b) {
    return a.epoch==b.epoch&&a.baseline_sequence==b.baseline_sequence&&a.operation==b.operation&&a.source_tick==b.source_tick&&
        a.ordinal==b.ordinal&&a.kind==b.kind&&a.id==b.id&&a.size==b.size;
}
bool LifeKeyFor(const Entity &entity,LifeKey &out) {
    if(entity.core.kind!=ENGINE_THING||!entity.core.id||!entity.core.initial_number||!entity.core.player) return false;
    LifeKey key={}; key.id=entity.core.id; CE_NetWriter w; CE_NetWriterInit(&w,key.bytes,sizeof key.bytes);
    CE_NetWriteU32(&w,entity.core.id); CE_NetWriteU32(&w,entity.core.initial_number); CE_NetWriteU32(&w,entity.core.player);
    WriteName(&w,entity.core.class_name); WriteName(&w,entity.core.mesh); CE_NetWriteU32(&w,entity.engine.configuration);
    CE_NetWriteU32(&w,entity.engine.part_count); WriteName(&w,entity.engine.cockpit.mesh); CE_NetWriteU64(&w,entity.engine.cockpit.schema);
    if(w.error) return false; key.size=w.size; out=key; return true;
}
bool RoundRecordBound(const LifeRecord &record) {
    const Core &c=record.core; const RoundState &r=record.round; const RoundBirth &b=record.round_birth;
    if(c.kind!=ROUND_THING||c.id!=record.info.id||strcmp(c.class_name,"CRound")||c.initial_number||c.player||c.mesh[0]||c.animation.present||
       c.type!=(THING_TYPE_THING|THING_TYPE_COMPLEX_THING|THING_TYPE_ACTOR|THING_TYPE_AMMUNITION)||
       (c.flags&~(TF_IN_MAP_WHO|TF_IS_BIG_THING|TF_DYING|TF_DECLARED_SHUTDOWN))||!(c.flags&TF_IN_MAP_WHO)||
       !VecBound(c.pos)||!VecBound(c.old_pos)||!VecBound(c.velocity)||!MatBound(c.orientation)||!MatBound(c.old_orientation)||
       !TimeBound(c.last_move)||!TimeBound(c.ground)||!TimeBound(c.water)||!TimeBound(c.object)||!AnimationBound(c.animation)||
       r.incarnation!=record.birth_serial||!r.schema||!r.launcher||r.target||r.incoming_member||r.jumps||r.armed||r.no_gravity||r.special||
       (strcmp(r.definition,"Mech Pulse Bolt Medium")&&strcmp(r.definition,"Mech Pulse Bolt Large"))||r.allegiance>6||!TimeBound(r.start)||
       !VecBound(r.destination)||r.beam_start.v[0]||r.beam_start.v[1]||r.beam_start.v[2]) return false;
    if(record.info.kind==CE_WORLD_LIFECYCLE_ROUND_DESPAWN) return record.birth_serial<record.info.operation;
    return record.birth_serial==record.info.operation&&!(c.flags&(TF_DYING|TF_DECLARED_SHUTDOWN))&&r.start==c.last_move&&
       b.shot&&b.inventory<=4&&b.index<kWeapons&&b.allegiance==r.allegiance&&b.actor_phase==1&&b.schema==r.schema&&b.mode_schema&&
       !memcmp(c.old_pos.v,b.pos.v,sizeof b.pos.v)&&!memcmp(c.velocity.v,b.velocity.v,sizeof b.velocity.v)&&
       !memcmp(c.orientation.v,b.orientation.v,sizeof b.orientation.v)&&!memcmp(c.old_orientation.v,b.orientation.v,sizeof b.orientation.v)&&
       !strcmp(b.weapon,"Pulse Cannon Pod")&&(!strcmp(b.mode,"Mech Pulse Cannon Charged")||!strcmp(b.mode,"Mech Pulse Cannon Charged 2"))&&
       VecBound(b.pos)&&VecBound(b.velocity)&&VecBound(b.destination)&&MatBound(b.orientation)&&Range(b.lifespan,.001f,1048576.f)&&
       record.dependency.id==r.launcher&&record.dependency.size&&record.dependency.size<=kLifeKeyBytes;
}

bool LifeRecordBound(const LifeRecord &r) {
    const CE_WorldLifecycleInfo &i=r.info;
    if(!i.epoch||!i.baseline_sequence||!i.operation||!i.source_tick||i.source_tick>20000000||!i.ordinal||i.ordinal>kLifeOps||i.ordinal>i.operation||!i.id||!r.birth_serial||r.birth_serial>i.operation||
        (i.kind<CE_WORLD_LIFECYCLE_BIRTH||i.kind>CE_WORLD_LIFECYCLE_ROUND_DESPAWN)) return false;
    if(i.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH||i.kind==CE_WORLD_LIFECYCLE_ROUND_DESPAWN) return RoundRecordBound(r);
    if(i.kind==CE_WORLD_LIFECYCLE_DESPAWN) return r.birth_serial<i.operation;
    const Core &c=r.core; const ExplosionBirth &b=r.birth; const ExplosionState &e=r.explosion;
    if((strcmp(r.definition_name,"Mech Pulse Hit Medium")&&strcmp(r.definition_name,"Mech Pulse Hit Large"))||r.definition>=1024||!r.definition_schema||
        !CENetworkWorldAdapter::BirthBound(b,i.source_tick)||b.tick!=i.source_tick||b.id!=i.id||b.serial!=r.birth_serial||b.attached||b.use_radius||b.volumetric||
        c.id!=i.id||c.kind!=EXPLOSION_THING||strcmp(c.class_name,"CExplosion")||c.initial_number||c.player||c.mesh[0]||c.animation.present||
        c.flags!=(uint32_t)(TF_DECLARED_SHUTDOWN|((b.deep||b.map_who)?TF_IN_MAP_WHO:0))||!VecBound(c.pos)||!VecBound(c.old_pos)||!VecBound(c.velocity)||!MatBound(c.orientation)||!MatBound(c.old_orientation)||
        !TimeBound(c.last_move)||!TimeBound(c.ground)||!TimeBound(c.water)||!TimeBound(c.object)||!AnimationBound(c.animation)||
        e.serial!=b.serial||e.deep!=b.deep||e.originator!=b.originator||r.dependency.id!=b.originator||r.dependency.size>kLifeKeyBytes||
        (!!r.dependency.id!=!!r.dependency.size)) return false;
    if(b.deep) return !e.definition&&!e.schema&&!e.radius&&!e.current_radius&&!e.allegiance&&c.type==THING_TYPE_NONE;
    return b.definition==r.definition&&b.schema==r.definition_schema&&!strcmp(b.name,r.definition_name)&&
        e.definition==b.definition&&e.schema==b.schema&&e.allegiance==b.allegiance&&e.radius==b.radius&&e.current_radius==b.radius&&
        b.radius>0.f&&b.radius<=3.f&&((c.type&~THING_TYPE_CAN_DESTROY_TREES)==(THING_TYPE_THING|THING_TYPE_COMPLEX_THING|THING_TYPE_EXPLOSION));
}
bool LifeDependency(const LifeRecord &r) {
    if((r.info.kind!=CE_WORLD_LIFECYCLE_BIRTH&&r.info.kind!=CE_WORLD_LIFECYCLE_ROUND_BIRTH)||!r.dependency.id) return true;
    for(unsigned i=0;i<life_key_count;++i) if(life_keys[i].id==r.dependency.id)
        return r.dependency.size==life_keys[i].size&&!memcmp(r.dependency.bytes,life_keys[i].bytes,r.dependency.size);
    return false;
}
bool EncodeLife(const LifeRecord &r,uint8_t *bytes,uint32_t &size) {
    if(!bytes||!LifeRecordBound(r)) return false;
    CE_NetWriter w; CE_NetWriterInit(&w,bytes,CE_WORLD_LIFECYCLE_MAX_BYTES);
    CE_NetWriteU32(&w,kLifeMagic); CE_NetWriteU32(&w,CE_WORLD_LIFECYCLE_SCHEMA);
    const CE_WorldLifecycleInfo &i=r.info;
    CE_NetWriteU32(&w,i.epoch); CE_NetWriteU32(&w,i.baseline_sequence); CE_NetWriteU32(&w,i.operation);
    CE_NetWriteU32(&w,i.source_tick); CE_NetWriteU32(&w,i.ordinal); CE_NetWriteU32(&w,i.kind); CE_NetWriteU32(&w,i.id); CE_NetWriteU32(&w,r.birth_serial);
    if(i.kind==CE_WORLD_LIFECYCLE_BIRTH) {
        CE_NetWriteU16(&w,r.definition); CE_NetWriteU64(&w,r.definition_schema); WriteName(&w,r.definition_name);
        WriteBirth(&w,r.birth); WriteCore(&w,r.core); WriteExplosion(&w,r.explosion);
    } else if(i.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH||i.kind==CE_WORLD_LIFECYCLE_ROUND_DESPAWN) {
        WriteCore(&w,r.core); WriteRound(&w,r.round);
        if(i.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH) WriteRoundBirth(&w,r.round_birth);
    }
    if(i.kind==CE_WORLD_LIFECYCLE_BIRTH||i.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH) {
        CE_NetWriteU32(&w,r.dependency.id); CE_NetWriteU16(&w,r.dependency.size); CE_NetWriteBytes(&w,r.dependency.bytes,r.dependency.size);
    }
    if(w.error) return false; size=w.size; return true;
}
bool DecodeLife(const uint8_t *bytes,uint32_t size,LifeRecord &out) {
    if(!bytes||size<40||size>CE_WORLD_LIFECYCLE_MAX_BYTES) return false;
    LifeRecord r={}; CE_NetReader rd; CE_NetReaderInit(&rd,bytes,size);
    uint32_t magic=CE_NetReadU32(&rd),schema=CE_NetReadU32(&rd); CE_WorldLifecycleInfo &i=r.info;
    i.epoch=CE_NetReadU32(&rd); i.baseline_sequence=CE_NetReadU32(&rd); i.operation=CE_NetReadU32(&rd);
    i.source_tick=CE_NetReadU32(&rd); i.ordinal=CE_NetReadU32(&rd); i.kind=CE_NetReadU32(&rd); i.id=CE_NetReadU32(&rd); r.birth_serial=CE_NetReadU32(&rd); i.size=size;
    if(i.kind==CE_WORLD_LIFECYCLE_BIRTH) {
        r.definition=CE_NetReadU16(&rd); r.definition_schema=CE_NetReadU64(&rd); ReadName(&rd,r.definition_name);
        ReadBirth(&rd,r.birth); ReadCore(&rd,r.core); ReadExplosion(&rd,r.explosion);
    } else if(i.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH||i.kind==CE_WORLD_LIFECYCLE_ROUND_DESPAWN) {
        ReadCore(&rd,r.core); ReadRound(&rd,r.round);
        if(i.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH) ReadRoundBirth(&rd,r.round_birth);
    }
    if(i.kind==CE_WORLD_LIFECYCLE_BIRTH||i.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH) {
        r.dependency.id=CE_NetReadU32(&rd); r.dependency.size=CE_NetReadU16(&rd);
        if(r.dependency.size>kLifeKeyBytes) rd.error=1; else CE_NetReadBytes(&rd,r.dependency.bytes,r.dependency.size);
    }
    if(magic!=kLifeMagic||schema!=CE_WORLD_LIFECYCLE_SCHEMA||!CE_NetReadDone(&rd)||!LifeRecordBound(r)) return false;
    out=r; return true;
}
bool LifeSourceHeader(LifeRecord &r,uint32_t kind,uint32_t id,uint32_t serial) {
    uint32_t tick=EVENT_MANAGER.GetFrameCount();
    if(!tick||tick<life_tick||life_generated==0xffffffffu||(tick==life_tick&&life_ordinal==kLifeOps)) return false;
    r.info={config.epoch,life_baseline,life_generated+1,tick,tick==life_tick?life_ordinal+1:1,kind,id,0}; r.birth_serial=serial; return true;
}
bool LifeAppend(LifeRecord &r) {
    if(life_failed||life_count==kLifeOps||r.info.operation!=life_generated+1||r.info.epoch!=config.epoch||r.info.baseline_sequence!=life_baseline||
       r.info.source_tick<life_tick||r.info.ordinal!=(r.info.source_tick==life_tick?life_ordinal+1:1)||!LifeDependency(r)) return false;
    LifeSlot slot={}; uint32_t size=0;
    if(!EncodeLife(r,slot.bytes,size)) return false;
    slot.info=r.info; slot.info.size=size; slot.birth_serial=r.birth_serial;
    life_journal[(life_first+life_count)%kLifeOps]=slot; ++life_count;
    life_generated=r.info.operation; life_tick=r.info.source_tick; life_ordinal=r.info.ordinal; return true;
}
bool LifeRetire(uint32_t operation,uint32_t tick) {
    unsigned retire=0; bool found=false;
    for(;retire<life_count;++retire) {
        LifeSlot &slot=life_journal[(life_first+retire)%kLifeOps];
        if(!slot.published) break;
        if(slot.info.operation==operation) { found=slot.info.source_tick==tick; ++retire; break; }
    }
    if(!found) { Fail("lifecycle ACK does not name an exact published operation"); return false; }
    for(unsigned j=0;j<retire;++j) {
        LifeSlot &slot=life_journal[(life_first+j)%kLifeOps];
        unsigned index=(life_retired_first+life_retired_count)%kLifeReceipts;
        if(life_retired_count==kLifeReceipts) { index=life_retired_first; life_retired_first=(life_retired_first+1)%kLifeReceipts; }
        else ++life_retired_count;
        life_retired[index]=slot.info;
    }
    life_first=(life_first+retire)%kLifeOps; life_count-=retire; life_ack=operation; return true;
}

int LifeReadiness(const CE_WorldFrame &f) {
    if(!active||config.role!=CE_WORLD_REPLICA||poisoned||life_failed||f.epoch!=config.epoch||f.level!=config.level||!CENetworkWorldAdapter::Validate(f)) return CE_WORLD_INVALID;
    if(!life_enabled) return f.lifecycle_baseline?CE_WORLD_INVALID:CE_WORLD_READY;
    if(f.lifecycle_baseline!=life_baseline||f.tick<life_baseline_tick) return CE_WORLD_INVALID;
    // STATE and reliable lifecycle channels can cross. Validate the entire historical frame before dropping
    // it, but do not resolve its retired native objects or reject its older committed-event clock as a new state.
    if(f.lifecycle_watermark<life_applied||f.sequence<=applied_sequence) return CE_WORLD_STALE;
    if(f.tick<life_committed_tick) return CE_WORLD_INVALID;
    if(f.lifecycle_watermark>life_applied) return CE_WORLD_DEFER;
    return CE_WORLD_READY;
}

void WriteUnit(CE_NetWriter *w,const UnitPresentation &u) {
    WriteName(w,u.definition); CE_NetWriteU64(w,u.mesh_schema);
    CE_NetWriteF32(w,u.life); CE_NetWriteF32(w,u.energy); CE_NetWriteF32(w,u.shields); CE_NetWriteF32(w,u.max_shields); CE_NetWriteF32(w,u.initial_life);
    WriteVec(w,u.euler); for(float v:u.turret) CE_NetWriteF32(w,v);
    CE_NetWriteU8(w,u.allegiance); CE_NetWriteU8(w,u.active); CE_NetWriteU8(w,u.vulnerable);
    CE_NetWriteU32(w,u.behavior); CE_NetWriteU32(w,u.icon); CE_NetWriteU8(w,u.fenrir);
    CE_NetWriteU32(w,u.hit); CE_NetWriteU32(w,u.hit_decay);
}
void ReadUnit(CE_NetReader *r,UnitPresentation &u) {
    ReadName(r,u.definition); u.mesh_schema=CE_NetReadU64(r);
    u.life=CE_NetReadF32(r); u.energy=CE_NetReadF32(r); u.shields=CE_NetReadF32(r); u.max_shields=CE_NetReadF32(r); u.initial_life=CE_NetReadF32(r);
    ReadVec(r,u.euler); for(float &v:u.turret) v=CE_NetReadF32(r);
    u.allegiance=CE_NetReadU8(r); u.active=CE_NetReadU8(r); u.vulnerable=CE_NetReadU8(r);
    u.behavior=CE_NetReadU32(r); u.icon=CE_NetReadU32(r); u.fenrir=CE_NetReadU8(r);
    u.hit=(int32_t)CE_NetReadU32(r); u.hit_decay=(int32_t)CE_NetReadU32(r);
}
void WriteBuilding(CE_NetWriter *w,const BuildingState &b) {
    WriteUnit(w,b.unit); const SegmentGraph &g=b.graph;
    CE_NetWriteU64(w,g.schema); CE_NetWriteU8(w,g.count); CE_NetWriteU8(w,g.slots); CE_NetWriteU8(w,g.core); CE_NetWriteU8(w,g.core_count);
    CE_NetWriteU8(w,g.changed); CE_NetWriteU8(w,g.primary_visible); CE_NetWriteU8(w,g.pickup_mode); CE_NetWriteU8(w,g.no_imposter);
    CE_NetWriteF32(w,g.fall_threshold); CE_NetWriteF32(w,g.total_health); CE_NetWriteF32(w,g.total_size);
    for(unsigned i=0;i<g.slots;++i) CE_NetWriteU8(w,g.alias[i]);
    for(unsigned i=0;i<g.count;++i) {
        const SegmentState &n=g.nodes[i];
        CE_NetWriteU8(w,n.type); CE_NetWriteU8(w,n.parent); CE_NetWriteU8(w,n.part); CE_NetWriteU8(w,n.number); CE_NetWriteU8(w,n.stages);
        CE_NetWriteU8(w,n.vulnerable); CE_NetWriteU8(w,n.destroyed); CE_NetWriteU8(w,n.cascading); CE_NetWriteU8(w,n.swapped); CE_NetWriteU8(w,n.has_direction); CE_NetWriteU8(w,n.hit);
        CE_NetWriteF32(w,n.health); CE_NetWriteF32(w,n.max_health); CE_NetWriteF32(w,n.last_damage_time); CE_NetWriteF32(w,n.last_damage_amount); CE_NetWriteF32(w,n.size);
        CE_NetWriteF32(w,n.total_time); CE_NetWriteF32(w,n.time); WriteVec(w,n.direction);
    }
    CE_NetWriteU8(w,b.door); CE_NetWriteU8(w,b.can_shut); CE_NetWriteU8(w,b.shut);
    CE_NetWriteF32(w,b.door_time); CE_NetWriteF32(w,b.close_time); CE_NetWriteF32(w,b.last_hit);
}
void ReadBuilding(CE_NetReader *r,BuildingState &b) {
    ReadUnit(r,b.unit); SegmentGraph &g=b.graph;
    g.schema=CE_NetReadU64(r); g.count=CE_NetReadU8(r); g.slots=CE_NetReadU8(r); g.core=CE_NetReadU8(r); g.core_count=CE_NetReadU8(r);
    g.changed=CE_NetReadU8(r); g.primary_visible=CE_NetReadU8(r); g.pickup_mode=CE_NetReadU8(r); g.no_imposter=CE_NetReadU8(r);
    g.fall_threshold=CE_NetReadF32(r); g.total_health=CE_NetReadF32(r); g.total_size=CE_NetReadF32(r);
    if(g.count>kParts||g.slots>kParts) { r->error=1; return; }
    for(unsigned i=0;i<g.slots;++i) g.alias[i]=CE_NetReadU8(r);
    for(unsigned i=0;i<g.count;++i) {
        SegmentState &n=g.nodes[i];
        n.type=CE_NetReadU8(r); n.parent=CE_NetReadU8(r); n.part=CE_NetReadU8(r); n.number=CE_NetReadU8(r); n.stages=CE_NetReadU8(r);
        n.vulnerable=CE_NetReadU8(r); n.destroyed=CE_NetReadU8(r); n.cascading=CE_NetReadU8(r); n.swapped=CE_NetReadU8(r); n.has_direction=CE_NetReadU8(r); n.hit=CE_NetReadU8(r);
        n.health=CE_NetReadF32(r); n.max_health=CE_NetReadF32(r); n.last_damage_time=CE_NetReadF32(r); n.last_damage_amount=CE_NetReadF32(r); n.size=CE_NetReadF32(r);
        n.total_time=CE_NetReadF32(r); n.time=CE_NetReadF32(r); ReadVec(r,n.direction);
    }
    b.door=CE_NetReadU8(r); b.can_shut=CE_NetReadU8(r); b.shut=CE_NetReadU8(r);
    b.door_time=CE_NetReadF32(r); b.close_time=CE_NetReadF32(r); b.last_hit=CE_NetReadF32(r);
}
void WriteCannon(CE_NetWriter *w,const CannonState &c) {
    WriteUnit(w,c.unit); CE_NetWriteF32(w,c.drop); CE_NetWriteF32(w,c.old_drop); CE_NetWriteU8(w,c.state); CE_NetWriteU8(w,c.above_water);
}
void ReadCannon(CE_NetReader *r,CannonState &c) {
    ReadUnit(r,c.unit); c.drop=CE_NetReadF32(r); c.old_drop=CE_NetReadF32(r); c.state=CE_NetReadU8(r); c.above_water=CE_NetReadU8(r);
}
void WriteFeature(CE_NetWriter *w,const FeatureState &f) {
    WriteName(w,f.definition); WriteName(w,f.sound); CE_NetWriteU64(w,f.mesh_schema);
    CE_NetWriteF32(w,f.life); CE_NetWriteF32(w,f.initial_life);
    CE_NetWriteU8(w,f.allegiance); CE_NetWriteU8(w,f.above_water); CE_NetWriteU8(w,f.invincible); CE_NetWriteU8(w,f.lockable);
    CE_NetWriteU32(w,f.explosion); CE_NetWriteU32(w,f.icon);
}
void ReadFeature(CE_NetReader *r,FeatureState &f) {
    ReadName(r,f.definition); ReadName(r,f.sound); f.mesh_schema=CE_NetReadU64(r);
    f.life=CE_NetReadF32(r); f.initial_life=CE_NetReadF32(r);
    f.allegiance=CE_NetReadU8(r); f.above_water=CE_NetReadU8(r); f.invincible=CE_NetReadU8(r); f.lockable=CE_NetReadU8(r);
    f.explosion=CE_NetReadU32(r); f.icon=CE_NetReadU32(r);
}
}

extern "C" {
int CE_WorldBegin(const CE_WorldConfig *in) { return in ? CENetworkWorldAdapter::Begin(*in) : Fail("missing world configuration"); }
void CE_WorldEnd() {
    if(active&&binding_count) { Fail("live world must be torn down before clearing the network event fence"); return; }
    LifeReset();
    active=false; applying=poisoned=false; binding_count=next_id=applied_tick=captured_tick=applied_sequence=captured_sequence=replica_ack=0;
    birth_count=birth_generated=birth_published=birth_ack=birth_applied=published_frame=published_tick=0; birth_failed=creating_explosion=false; applying_birth=NULL; birth_spawn=NULL;
    memset(birth_journal,0,sizeof birth_journal);
    generated_sequence=sent_sequence=sampled_tick=input_suspension=remote_invert_ground=remote_invert_jet=remote_walker_any=0; sampling=sampling_failed=mouse_axis=false;
    memset(&config,0,sizeof config); memset(&commands,0,sizeof commands); memset(&local_input,0,sizeof local_input); memset(&sample,0,sizeof sample); memset(&local_clock,0,sizeof local_clock); error_text[0]=0;
}
#ifdef CE_NET_TEST_AUTOMATION
// Test-automation diagnostic (BEA_PEERB_INVENTORY=N): every N ticks, the world's things by class and adapter kind.
void CE_WorldInventoryLog() {
    static uint32_t last; const char *e=getenv("BEA_PEERB_INVENTORY"); if(!e||!*e) return;
    uint32_t every=(uint32_t)atoi(e), tick=EVENT_MANAGER.GetFrameCount(); if(!every||tick<last+every) return; last=tick;
    const char *names[256]; unsigned counts[256], kinds[256], n=0, total=0;
    ListIterator<CThing> it(&WORLD.GetThingNB());
    for(CThing *t=it.First();t;t=it.Next()) {
        const char *c=t->_GetClassName(); unsigned i=0; ++total;
        while(i<n&&strcmp(names[i],c)) ++i;
        if(i==n) { if(n==256) continue; names[n]=c; counts[n]=0; kinds[n]=ThingKind(t); ++n; }
        ++counts[i];
    }
    fprintf(stderr,"peerb-inventory tick %u level %d state %d things %u:",tick,GAME.GetCurrentLevel(),(int)GAME.GetGameState(),total);
    for(unsigned i=0;i<n;++i) fprintf(stderr," %s=%u/k%u",names[i],counts[i],kinds[i]);
    fprintf(stderr,"\n");
}
#endif
const char *CE_WorldError() { return error_text; }
int CE_WorldActive() { return active || CE_LockstepActive(); }
int CE_WorldReplica() { return active && config.role==CE_WORLD_REPLICA; }
int CE_WorldCanRender() { return !CE_WorldReplica()||(!poisoned&&applied_tick!=0); }
uint32_t CE_WorldLocalPlayer() { return CE_LockstepActive() ? CE_LockstepLocalPlayer() : active ? config.local_player : 0; }
CE_WorldFrame *CE_WorldFrameCreate() { return (CE_WorldFrame *)calloc(1,sizeof(CE_WorldFrame)); }
void CE_WorldFrameDestroy(CE_WorldFrame *frame) { free(frame); }
int CE_WorldCapture(CE_WorldFrame *frame) { return frame ? CENetworkWorldAdapter::Capture(*frame) : Fail("missing capture destination"); }
int CE_WorldFramePublished(const CE_WorldFrame *frame) {
    if(!active||config.role!=CE_WORLD_HOST||!frame||frame->epoch!=config.epoch||frame->level!=config.level||!frame->sequence||frame->sequence>captured_sequence||
        frame->sequence<published_frame||frame->tick>captured_tick||!CENetworkWorldAdapter::Validate(*frame)) return Fail("world publication does not name an admitted captured host frame");
    uint32_t highest=frame->birth_count?frame->births[frame->birth_count-1].serial:0;
    if(highest>birth_generated) return Fail("world publication claims an ungenerated birth serial");
    if(!LifeRecordProof(*frame)) return Fail("published frame cannot retain its exact local baseline proof");
    if(highest>birth_published) birth_published=highest;
    if(frame->tick>published_tick) published_tick=frame->tick;
    published_frame=frame->sequence; return 1;
}
int CE_WorldAppliedBirths(CE_WorldBirthAck *out) {
    if(life_enabled||!out||!CE_WorldReplica()||poisoned||applying||!config.epoch||!applied_tick||!applied_sequence) return 0;
    *out={config.epoch,birth_applied,applied_tick}; return 1;
}
int CE_WorldApply(const CE_WorldFrame *frame) {
    if(!life_enabled) LifeInvalidateProof();
    int applied=frame ? CENetworkWorldAdapter::Apply(*frame) : Fail("missing replica frame");
    if(!applied||poisoned) { LifeInvalidateProof(); return 0; }
    if(!LifeRecordProof(*frame)) { poisoned=true; return Fail("whole Apply cannot retain its exact local baseline proof; teardown required"); }
    return 1;
}
int CE_WorldFrameInfo(const CE_WorldFrame *f,CE_WorldInfo *out) {
    if(!f||!out) return Fail("missing frame metadata destination");
    *out={f->epoch,f->level,f->tick,f->sequence,f->phase,f->paused,f->consumed_sequence,f->lifecycle_baseline,f->lifecycle_watermark}; return 1;
}
int CE_WorldEncode(const CE_WorldFrame *f,uint8_t *out,uint32_t capacity,uint32_t *size) {
    if(size) *size=0; if(!f||!out||!size||capacity>CE_NET_MAX_MESSAGE||!CENetworkWorldAdapter::Validate(*f)) return Fail("cannot encode invalid world frame");
    CE_NetWriter w; CE_NetWriterInit(&w,out,capacity); CE_NetWriteU32(&w,kMagic); CE_NetWriteU32(&w,CE_WORLD_SCHEMA);
    CE_NetWriteU32(&w,f->epoch); CE_NetWriteU32(&w,f->tick); CE_NetWriteU32(&w,f->sequence); CE_NetWriteU32(&w,f->consumed_sequence); CE_NetWriteU32(&w,f->lifecycle_baseline); CE_NetWriteU32(&w,f->lifecycle_watermark); CE_NetWriteU32(&w,f->level); CE_NetWriteU8(&w,f->phase); CE_NetWriteU8(&w,f->paused);
    CE_NetWriteU32(&w,f->score); CE_NetWriteU32(&w,f->lives[0]); CE_NetWriteU32(&w,f->lives[1]); CE_NetWriteU16(&w,f->count);
    CE_NetWriteF32(&w,f->end_count); CE_NetWriteU8(&w,f->fade); CE_NetWriteU8(&w,f->alpha);
    for(unsigned i=0;i<NUM_WORLD_COUNTERS;++i) { CE_NetWriteU32(&w,f->counters_a[i]); CE_NetWriteU32(&w,f->counters_b[i]); }
    for(unsigned i=0;i<MAX_PRIMARY_OBJECTIVES;++i) { CE_NetWriteU32(&w,(uint32_t)f->primary[i].state); CE_NetWriteU32(&w,(uint32_t)f->primary[i].string_id); }
    for(unsigned i=0;i<MAX_SECONDARY_OBJECTIVES;++i) { CE_NetWriteU32(&w,(uint32_t)f->secondary[i].state); CE_NetWriteU32(&w,(uint32_t)f->secondary[i].string_id); }
    CE_NetWriteU8(&w,f->birth_count); for(unsigned i=0;i<f->birth_count;++i) WriteBirth(&w,f->births[i]);
    for(unsigned i=0;i<f->count&&!w.error;++i) { const Entity &s=f->entities[i]; WriteCore(&w,s.core);
        if(s.core.kind==ENGINE_THING) WriteEngine(&w,s.engine);
        if(s.core.kind==CANNON_THING) WriteCannon(&w,s.cannon);
        if(s.core.kind==FEATURE_THING) WriteFeature(&w,s.feature);
        if(s.core.kind==BUILDING_THING) WriteBuilding(&w,s.building);
        if(s.core.kind==EXPLOSION_THING) WriteExplosion(&w,s.explosion);
        if(s.core.kind==ROUND_THING) WriteRound(&w,s.round);
    }
    if(w.error) return Fail("world exceeds wire capacity or contains nonfinite state"); *size=w.size; return 1;
}
int CE_WorldDecode(CE_WorldFrame *out,const uint8_t *data,uint32_t size) {
    if(!out||!data||size>CE_NET_MAX_MESSAGE) return Fail("invalid world payload size");
    CE_WorldFrame *f=CE_WorldFrameCreate(); if(!f) return Fail("world decode allocation refused"); CE_NetReader r; CE_NetReaderInit(&r,data,size);
    uint32_t magic=CE_NetReadU32(&r), schema=CE_NetReadU32(&r);
    f->epoch=CE_NetReadU32(&r); f->tick=CE_NetReadU32(&r); f->sequence=CE_NetReadU32(&r); f->consumed_sequence=CE_NetReadU32(&r); f->lifecycle_baseline=CE_NetReadU32(&r); f->lifecycle_watermark=CE_NetReadU32(&r); f->level=CE_NetReadU32(&r); f->phase=CE_NetReadU8(&r); f->paused=CE_NetReadU8(&r);
    f->score=CE_NetReadU32(&r); f->lives[0]=CE_NetReadU32(&r); f->lives[1]=CE_NetReadU32(&r); f->count=CE_NetReadU16(&r);
    f->end_count=CE_NetReadF32(&r); f->fade=CE_NetReadU8(&r); f->alpha=CE_NetReadU8(&r);
    for(unsigned i=0;i<NUM_WORLD_COUNTERS;++i) { f->counters_a[i]=(int32_t)CE_NetReadU32(&r); f->counters_b[i]=(int32_t)CE_NetReadU32(&r); }
    for(unsigned i=0;i<MAX_PRIMARY_OBJECTIVES;++i) { f->primary[i].state=(int32_t)CE_NetReadU32(&r); f->primary[i].string_id=(int32_t)CE_NetReadU32(&r); }
    for(unsigned i=0;i<MAX_SECONDARY_OBJECTIVES;++i) { f->secondary[i].state=(int32_t)CE_NetReadU32(&r); f->secondary[i].string_id=(int32_t)CE_NetReadU32(&r); }
    f->birth_count=CE_NetReadU8(&r); if(f->birth_count>kBirths) r.error=1;
    for(unsigned i=0;i<f->birth_count&&!r.error;++i) ReadBirth(&r,f->births[i]);
    if(magic!=kMagic||schema!=CE_WORLD_SCHEMA||f->count>kEntities) r.error=1;
    for(unsigned i=0;i<f->count&&!r.error;++i) { Entity &s=f->entities[i]; ReadCore(&r,s.core);
        if(s.core.kind==ENGINE_THING) ReadEngine(&r,s.engine);
        if(s.core.kind==CANNON_THING) ReadCannon(&r,s.cannon);
        if(s.core.kind==FEATURE_THING) ReadFeature(&r,s.feature);
        if(s.core.kind==BUILDING_THING) ReadBuilding(&r,s.building);
        if(s.core.kind==EXPLOSION_THING) ReadExplosion(&r,s.explosion);
        if(s.core.kind==ROUND_THING) ReadRound(&r,s.round);
    }
    int ok=CE_NetReadDone(&r)&&CENetworkWorldAdapter::Validate(*f); if(ok) memcpy(out,f,sizeof *out); CE_WorldFrameDestroy(f);
    return ok||Fail("malformed world frame; destination unchanged");
}
int CE_WorldLifecycleStart(const CE_WorldFrame *baseline) {
    if(!active||poisoned||applying||birth_failed||life_failed||!baseline||baseline->epoch!=config.epoch||baseline->level!=config.level||
       baseline->lifecycle_baseline||baseline->lifecycle_watermark||baseline->birth_count||!CENetworkWorldAdapter::Validate(*baseline))
        return Fail("lifecycle start requires the identical locally committed initial baseline");
    if(!life_enabled&&!CENetworkWorldAdapter::InitialLifecycleContext(*baseline))
        return Fail("initial lifecycle admission requires the current quiescent loaded world and event epoch");
    uint8_t bytes[CE_NET_MAX_MESSAGE]; uint32_t size=0;
    if(!CE_WorldEncode(baseline,bytes,sizeof bytes,&size)) return 0;
    if(!life_proof_valid||baseline->sequence!=life_proof_sequence||baseline->tick!=life_proof_tick||size!=life_baseline_size||memcmp(bytes,life_baseline_bytes,size))
        return Fail("lifecycle baseline bytes differ from the prior successful local publication or whole Apply");
    if(life_enabled) return (size==life_baseline_size&&!memcmp(bytes,life_baseline_bytes,size))||Fail("existing lifecycle ownership cannot be replaced");
    if(birth_count||birth_generated||birth_published||birth_ack||birth_applied||life_pending||life_live_count)
        return Fail("pre-baseline explosion history is not admitted by compact lifecycle");
    if(config.role==CE_WORLD_HOST) {
        if(baseline->sequence!=published_frame||baseline->tick!=published_tick||baseline->sequence>captured_sequence)
            return Fail("lifecycle baseline has not been locally published");
    } else if(baseline->sequence!=applied_sequence||baseline->tick!=applied_tick)
        return Fail("lifecycle baseline has not been wholly applied");
    LifeKey keys[2]={}; unsigned count=0; uint32_t maximum=0;
    for(unsigned i=0;i<baseline->count;++i) {
        const Entity &e=baseline->entities[i]; if(e.core.id>maximum) maximum=e.core.id;
        if(e.core.kind==EXPLOSION_THING||e.core.kind==ROUND_THING) return Fail("initial baseline contains unsupported historic lifecycle actors");
        if(e.core.kind==ENGINE_THING) {
            Binding *binding=ById(e.core.id);
            if(count==2||!binding||binding->kind!=ENGINE_THING||binding->initial_number!=e.core.initial_number||
               !CENetworkWorldAdapter::Compatible(binding->thing,e)||!LifeKeyFor(e,keys[count]))
                return Fail("lifecycle baseline engine dependency differs from the live owner");
            ++count;
        }
    }
    if(count!=2) return Fail("lifecycle requires two canonical baseline dependencies");
    memcpy(life_keys,keys,sizeof keys); life_key_count=count; memcpy(life_baseline_bytes,bytes,size); life_baseline_size=size;
    life_baseline=baseline->sequence; life_baseline_tick=baseline->tick; life_committed_tick=baseline->tick; life_tick=baseline->tick;
    life_seen_id=maximum; life_enabled=true; return 1;
}
int CE_WorldPeekLifecycle(CE_WorldLifecycleInfo *out,uint8_t *bytes,uint32_t capacity) {
    if(!active||config.role!=CE_WORLD_HOST||!life_enabled||life_failed||birth_failed||!out||!bytes) return -1;
    for(unsigned j=0;j<life_count;++j) {
        const LifeSlot &slot=life_journal[(life_first+j)%kLifeOps]; if(slot.published) continue;
        if(capacity<slot.info.size) { Fail("lifecycle destination capacity is insufficient"); return -1; }
        memcpy(bytes,slot.bytes,slot.info.size); *out=slot.info; return 1;
    }
    return 0;
}
int CE_WorldLifecyclePublished(const CE_WorldLifecycleInfo *info) {
    if(!active||config.role!=CE_WORLD_HOST||!life_enabled||life_failed||!info||info->epoch!=config.epoch||info->baseline_sequence!=life_baseline) return 0;
    for(unsigned j=0;j<life_count;++j) {
        LifeSlot &slot=life_journal[(life_first+j)%kLifeOps];
        if(slot.info.operation!=info->operation) continue;
        if(!LifeSame(slot.info,*info)||(!slot.published&&info->operation!=life_published+1)) return Fail("lifecycle publication tuple or order differs");
        if(!slot.published) { slot.published=true; life_published=info->operation; if((info->kind==CE_WORLD_LIFECYCLE_BIRTH||info->kind==CE_WORLD_LIFECYCLE_DESPAWN)&&slot.birth_serial>birth_published) birth_published=slot.birth_serial; }
        return 1;
    }
    for(unsigned j=0;j<life_retired_count;++j) if(LifeSame(life_retired[(life_retired_first+j)%kLifeReceipts],*info)) return 1;
    return Fail("lifecycle publication tuple is unknown or expired");
}
int CE_WorldInspectLifecycle(const uint8_t *bytes,uint32_t size,CE_WorldLifecycleInfo *out) {
    LifeRecord record={};
    if(!out||!DecodeLife(bytes,size,record)) { Fail("malformed compact lifecycle record; destination unchanged"); return -1; }
    *out=record.info; return 1;
}
int CE_WorldApplyLifecycle(const uint8_t *bytes,uint32_t size) {
    LifeRecord r={};
    if(!active||config.role!=CE_WORLD_REPLICA||!life_enabled||poisoned||life_failed||applying||!DecodeLife(bytes,size,r)||
       r.info.epoch!=config.epoch||r.info.baseline_sequence!=life_baseline||!CENetworkWorldAdapter::LifeCompatible(r)) {
        Fail("compact lifecycle is malformed, foreign or incompatible; no mutation"); return -1;
    }
    if(r.info.operation<=life_applied) {
        for(unsigned j=0;j<life_duplicate_count;++j) {
            const LifeSlot &slot=life_duplicates[(life_duplicate_first+j)%kLifeOps];
            if(LifeSame(slot.info,r.info)&&!memcmp(slot.bytes,bytes,size)) return 2;
        }
        Fail("compact duplicate proof differs or expired; no replay"); return -1;
    }
    if(r.info.source_tick<life_committed_tick||r.info.source_tick<applied_tick) {
        Fail("unseen lifecycle record would rewind committed source order"); return -1;
    }
    if(!CENetworkWorldAdapter::LifeOwner(r)) return -1;
    const bool round=r.info.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH||r.info.kind==CE_WORLD_LIFECYCLE_ROUND_DESPAWN;
    const bool birth=r.info.kind==CE_WORLD_LIFECYCLE_BIRTH||r.info.kind==CE_WORLD_LIFECYCLE_ROUND_BIRTH;
    const uint32_t family=round?ROUND_THING:EXPLOSION_THING;
    Binding *known=ById(r.info.id);
    if(birth) {
        if(r.info.id<=life_seen_id||known||(!round&&r.birth_serial<=birth_applied)||(round&&r.round_birth.shot<=last_round_shot)) {
            Fail("future birth conflicts with a committed identity or shot context"); return -1;
        }
    } else if((known&&(known->kind!=family||(round?known->round_incarnation:known->birth.serial)!=r.birth_serial))||(!known&&r.info.id<=life_seen_id)) {
        Fail("future despawn cannot name a baseline or retired identity"); return -1;
    }
    // Fully decoded, scalar/definition/dependency validated, but a prior operation is still required.
    if(r.info.operation!=life_applied+1) return 0;
    if(r.info.ordinal!=(r.info.source_tick==life_tick?life_applied_ordinal+1:1)) {
        Fail("compact same-tick ordinal is gapped or reused"); return -1;
    }
    if(birth) {
        if(r.info.id<=life_seen_id||ById(r.info.id)||(!round&&r.birth_serial!=birth_applied+1)||life_live_count==kEntities||binding_count==kEntities) {
            Fail("compact birth reuses an identity, incarnation or bounded slot"); return -1;
        }
    } else {
        unsigned index=0; while(index<life_live_count&&life_live[index].id!=r.info.id) ++index;
        Binding *binding=ById(r.info.id);
        if(index==life_live_count||life_live[index].kind!=family||life_live[index].birth_serial!=r.birth_serial||!binding||binding->thing!=life_live[index].thing||
           binding->kind!=family||(round?binding->round_incarnation:binding->birth.serial)!=r.birth_serial||
           (round&&binding->round_schema!=r.round.schema)) {
            Fail("compact despawn does not match its live native incarnation"); return -1;
        }
    }
    if(!CENetworkWorldAdapter::LifeMutate(r)) return -1;
    // The only application-proof publication point is after every native creation/reader/scalar/shutdown step.
    unsigned index=(life_duplicate_first+life_duplicate_count)%kLifeOps;
    if(life_duplicate_count==kLifeOps) { index=life_duplicate_first; life_duplicate_first=(life_duplicate_first+1)%kLifeOps; }
    else ++life_duplicate_count;
    LifeSlot &slot=life_duplicates[index]; slot={}; slot.info=r.info; slot.birth_serial=r.birth_serial; memcpy(slot.bytes,bytes,size);
    life_applied=r.info.operation; life_tick=life_committed_tick=r.info.source_tick; life_applied_ordinal=r.info.ordinal;
    if(birth) { if(round) last_round_shot=r.round_birth.shot; else birth_applied=r.birth_serial; life_seen_id=r.info.id; }
    return 1;
}

int CE_WorldAppliedLifecycle(CE_WorldLifecycleAck *out) {
    if(!out||!active||config.role!=CE_WORLD_REPLICA||!life_enabled||life_failed||poisoned||applying||!life_applied) return 0;
    *out={config.epoch,life_baseline,life_applied,life_tick}; return 1;
}
int CE_WorldAcknowledgeLifecycle(const CE_WorldLifecycleAck *ack) {
    if(!ack||!active||config.role!=CE_WORLD_HOST||!life_enabled||life_failed||ack->epoch!=config.epoch||ack->baseline_sequence!=life_baseline||
       !ack->operation||!ack->source_tick||ack->source_tick<life_baseline_tick||ack->source_tick>EVENT_MANAGER.GetFrameCount()||ack->operation>life_published)
        { Fail("foreign or unpublished lifecycle application ACK"); return -1; }
    if(ack->operation<=life_ack) return 1;
    return LifeRetire(ack->operation,ack->source_tick)?1:-1;
}
int CE_WorldFrameReadiness(const CE_WorldFrame *frame) {
    if(!frame) return CE_WORLD_INVALID;
    int ready=LifeReadiness(*frame);
    if(life_enabled&&(ready==CE_WORLD_READY||ready==CE_WORLD_DEFER)&&!CENetworkWorldAdapter::FrameResources(*frame)) return CE_WORLD_INVALID;
    return ready;
}

int CE_WorldEncodeInput(const CE_WorldInput *in,uint8_t *out,uint32_t capacity,uint32_t *size) {
    if(size) *size=0; if(!in||!out||!size||capacity>CE_NET_MAX_INPUT||!ValidInput(*in)) return Fail("invalid semantic input");
    CE_NetWriter w; CE_NetWriterInit(&w,out,capacity); CE_NetWriteU32(&w,kInputMagic); CE_NetWriteU32(&w,CE_WORLD_INPUT_SCHEMA);
    CE_NetWriteU32(&w,in->epoch); CE_NetWriteU32(&w,in->sequence); CE_NetWriteU32(&w,in->tick); CE_NetWriteU32(&w,in->last_applied_birth_serial); CE_NetWriteU8(&w,in->player); CE_NetWriteU8(&w,in->count);
    CE_NetWriteU8(&w,in->invert_ground); CE_NetWriteU8(&w,in->invert_jet); CE_NetWriteU8(&w,in->mouse_ground_aim); CE_NetWriteU8(&w,in->walker_mouse_any); CE_NetWriteF32(&w,in->ground_yaw); CE_NetWriteF32(&w,in->ground_pitch);
    for(unsigned i=0;i<in->count;++i) { CE_NetWriteU8(&w,in->actions[i].button); CE_NetWriteU8(&w,in->actions[i].source); CE_NetWriteF32(&w,in->actions[i].value); }
    if(w.error) return Fail("input exceeds wire capacity"); *size=w.size; return 1;
}
int CE_WorldDecodeInput(CE_WorldInput *out,const uint8_t *data,uint32_t size) {
    if(!out||!data||size>CE_NET_MAX_INPUT) return Fail("invalid input payload size"); CE_WorldInput in={}; CE_NetReader r; CE_NetReaderInit(&r,data,size);
    uint32_t magic=CE_NetReadU32(&r),schema=CE_NetReadU32(&r); in.epoch=CE_NetReadU32(&r); in.sequence=CE_NetReadU32(&r); in.tick=CE_NetReadU32(&r); in.last_applied_birth_serial=CE_NetReadU32(&r); in.player=CE_NetReadU8(&r); in.count=CE_NetReadU8(&r);
    in.invert_ground=CE_NetReadU8(&r); in.invert_jet=CE_NetReadU8(&r); in.mouse_ground_aim=CE_NetReadU8(&r); in.walker_mouse_any=CE_NetReadU8(&r);
    in.ground_yaw=CE_NetReadF32(&r); in.ground_pitch=CE_NetReadF32(&r); if(in.count>CE_WORLD_MAX_ACTIONS) r.error=1;
    for(unsigned i=0;i<in.count&&!r.error;++i) { in.actions[i].button=CE_NetReadU8(&r); in.actions[i].source=CE_NetReadU8(&r); in.actions[i].value=CE_NetReadF32(&r); }
    if(magic!=kInputMagic||schema!=CE_WORLD_INPUT_SCHEMA||!CE_NetReadDone(&r)||!ValidInput(in)) return Fail("malformed semantic input; destination unchanged"); *out=in; return 1;
}
bool ValidBirthAck(const CE_WorldInput &in,bool) {
    // A newer independent application ACK may overtake an immutable queued gameplay input.
    return in.last_applied_birth_serial<=birth_published;
}
void RetireBirths(uint32_t serial) {
    if(serial<=birth_ack) return;
    unsigned retired=0; while(retired<birth_count&&birth_journal[retired].serial<=serial) ++retired;
    if(retired) { birth_count-=retired; memmove(birth_journal,birth_journal+retired,birth_count*sizeof *birth_journal); }
    birth_ack=serial;
}
int CE_WorldAcknowledgeBirths(uint32_t epoch,uint32_t serial,uint32_t tick) {
    if(!active||config.role!=CE_WORLD_HOST||epoch!=config.epoch||!tick||tick>published_tick||
        tick>EVENT_MANAGER.GetFrameCount()||serial>birth_published) {
        Fail("birth application ACK is foreign, unpublished or from a future tick"); return -1;
    }
    if(serial>birth_ack) {
        const ExplosionBirth *birth=NULL;
        for(unsigned i=0;i<birth_count;++i) if(birth_journal[i].serial==serial) { birth=&birth_journal[i]; break; }
        if(!birth||tick<birth->tick) { Fail("birth application ACK predates its retained birth"); return -1; }
        RetireBirths(serial);
    }
    return 1;
}
int CE_WorldQueueRemoteInput(const CE_WorldInput *in) {
    if(!active||config.role!=CE_WORLD_HOST||!in) { Fail("input is not for an admitted host"); return -1; }
    bool duplicate=in->sequence<=commands.accepted;
    if(!ValidBirthAck(*in,duplicate)) { Fail("explosion birth acknowledgement is stale or unpublished"); return -1; }
    int admitted=commands.Push(*in,config.epoch,EVENT_MANAGER.GetFrameCount());
    if(admitted==1&&!duplicate) RetireBirths(in->last_applied_birth_serial);
    return admitted;
}
int CE_WorldSampleInput(CE_WorldInput *out) { return CENetworkWorldAdapter::SampleInput(out); }
int CE_WorldInputSent(uint32_t sequence) { return CENetworkWorldAdapter::InputSent(sequence); }
int CE_WorldSetInputSuspended(uint32_t mask) {
    if(mask&~(uint32_t)CE_WORLD_INPUT_HOST_WAIT) { Fail("unknown local input suspension reason"); return -1; }
    if(CE_WorldReplica()) input_suspension=mask;
    return 1;
}
void CE_WorldRebaseLocalSampler() {
    if(!CE_WorldReplica()) return;
    memset(&local_clock,0,sizeof local_clock); sampled_tick=applied_tick;
}
uint32_t CE_WorldLastGeneratedSequence() { return CE_WorldReplica()?generated_sequence:0; }
void CE_WorldPollLocalInput() { CENetworkWorldAdapter::PollLocal(); }
uint32_t CE_WorldConsumedSequence() { return active&&config.role==CE_WORLD_HOST?commands.consumed:replica_ack; }
int CE_WorldControllerFlush(uint32_t player) { return CE_LockstepTicking() ? CE_LockstepControllerFlush(player) : CENetworkWorldAdapter::Flush(player); }
int CE_WorldLockstepValidInput(const CE_WorldInput *in) {
    if (!in || in->count > CE_WORLD_MAX_ACTIONS) return 0;
    // Lockstep carries neither the replica epoch/sequence nor a fixed player field: validate its controls using the
    // existing shared semantic rule without pretending those transport fields came from the packet.
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
// The tutorial's solo script gates player 1. In the private two-player prototype, mirror those gates only for that
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
int CE_WorldLockstepGates(uint64_t *out) { return out && CENetworkWorldAdapter::LockstepPlayerGates(*out); }
int CE_WorldLockstepEndTick(void) { return CENetworkWorldAdapter::LockstepEndTick(); }
void CE_WorldLockstepBeginTick(void) { CENetworkWorldAdapter::LockstepBeginTick(); }
// Lockstep co-op: a peer shows its own player, except while a mission script's pan camera holds camera 0 (scripts
// set their cutscene cameras there): then both peers show it.
int CE_WorldViewPlayer(int original) {
    if(CE_LockstepActive()) return CE_LockstepLocalPlayer()==1&&dynamic_cast<CPanCamera *>(GAME.GetCamera(0)) ? 0 : (int)CE_LockstepLocalPlayer();
    return active ? (int)config.local_player : original;
}
float CE_WorldViewAspect(float original) { return CE_WorldActive() ? .75f : original; }
int CE_WorldInvertPitch(uint32_t player,int grounded,int original) { if(CE_LockstepTicking()) return CE_LockstepInvertPitch(player,grounded,original); return active&&config.role==CE_WORLD_HOST&&player==1 ? (int)(grounded?remote_invert_ground:remote_invert_jet) : original; }
int CE_WorldExplosionSpawnAllowed() {
    if(!CE_WorldReplica()) return 1;
    if(poisoned) return 0;
    if(creating_explosion) return 1;
    poisoned=true;
    return Fail("unrequested native replica explosion allocation; mandatory world teardown is required");
}
int CE_WorldRoundSpawnAllowed() {
    if(!CE_WorldReplica()) return 1;
    if(!poisoned&&creating_round) return 1;
    poisoned=true; return Fail("unrequested native replica round or weapon execution; teardown is required");
}
int CE_WorldWalkerBindingType(uint32_t player,int original) { if(CE_LockstepTicking()) return CE_LockstepWalkerAny(player,original) ? MOUSE_AXIS_PLUS_ANY : MOUSE_AXIS_PLUS; return active&&config.role==CE_WORLD_HOST&&player==1 ? (remote_walker_any?MOUSE_AXIS_PLUS_ANY:MOUSE_AXIS_PLUS) : original; }
void CE_WorldMouseAxis(int on) { if(sampling) mouse_axis=on!=0; if(CENetworkWorldAdapter::lock_sample) CENetworkWorldAdapter::lock_mouse_axis=on!=0; }
void CE_WorldForget(const void *thing) {
    if(!active||applying) return;
    for(unsigned i=0;i<binding_count;++i) if(bindings[i].thing==thing) { bindings[i]=bindings[--binding_count]; return; }
}
}
int CE_WorldBypassMouse(CBattleEngine *engine) {
    if(CE_LockstepTicking()) return 1; // mouse look reaches either engine only through its player's tick sample
    if(!active) return 0; if(config.role==CE_WORLD_REPLICA) return 1;
    return engine&&engine->GetPlayer()&&engine->GetPlayer()->GetNumber()==2;
}
int CE_WorldCaptureAction(CController *controller,int button,float value) {
    if(CE_LockstepTicking()) { CENetworkWorldAdapter::LockstepCapture(button,value); return 1; } // never dispatched natively
    if(!active||config.role!=CE_WORLD_REPLICA) return 0;
    if(sampling&&controller==GAME.GetController(0)&&ActionAllowed(button)) {
        if(sample.count==CE_WORLD_MAX_ACTIONS) { sampling_failed=true; Fail("one controller flush exceeds the semantic action bound"); }
        else sample.actions[sample.count++]={(uint32_t)button,mouse_axis?CE_WORLD_ACTION_MOUSE_AXIS:CE_WORLD_ACTION_BINDING,value};
    }
    return 1; // a replica never dispatches physical controls into gameplay, including out-of-band controller flushes
}

int CE_WorldUnitHitEffect(CUnit *unit,int part,int *out) { return CENetworkWorldAdapter::HitEffect(unit,part,out); }

void CE_WorldCockpitInitialized(CCockpit *cockpit) { CENetworkWorldAdapter::CockpitInitialized(cockpit); }
void CE_WorldExplosionBirth(CExplosion *e,CInitThing *i) { CENetworkWorldAdapter::ExplosionBirthEntry(e,i); }
void CE_WorldExplosionReady(CExplosion *e,CInitThing *i) { CENetworkWorldAdapter::ExplosionReady(e,i); }
void CE_WorldExplosionInitialized(CExplosion *e,CInitThing *) { CENetworkWorldAdapter::LifeComplete(e); }
void CE_WorldLifecycleForget(const void *thing) { CENetworkWorldAdapter::LifeForget(thing,false); }
void CE_WorldLifecycleDestroyed(const void *thing) {
    CENetworkWorldAdapter::LifeForget(thing,true);
    if(active&&life_enabled&&!applying) CE_WorldForget(thing); // direct-delete failure must not retain a dangling binding
}

uint32_t CE_WorldShotBegin(CWeapon *weapon,CWeaponModeData *mode) { return CENetworkWorldAdapter::ShotBegin(weapon,mode); }
void CE_WorldShotEnd(CWeapon *weapon,uint32_t shot) { CENetworkWorldAdapter::ShotEnd(weapon,shot); }
void CE_WorldRoundBirth(CRound *round,CInitThing *init) { CENetworkWorldAdapter::RoundEntry(round,init); }
void CE_WorldRoundInitialized(CRound *round,CInitThing *) { CENetworkWorldAdapter::RoundComplete(round); }
void CE_WorldRoundShutdown(CRound *round) { CENetworkWorldAdapter::RoundShutdown(round); }
int CE_WorldActorInitPhase(CActor *actor,int multiplier,int *out) {
    if(!CE_WorldReplica()) return 0;
    if(out) *out=0; // selected multiplier-one CRound phase; never rerun the host's RNG on replicas
    if(out&&!poisoned&&applying&&applying_round&&round_spawn&&actor==static_cast<CActor *>(round_spawn)&&round_pending==round_spawn&&multiplier==1&&
       PortTypeInfoEqual(typeid(*actor),typeid(CRound))) return 1;
    poisoned=true; Fail("replica actor Init has no selected round phase token; teardown is required"); return 1;
}
