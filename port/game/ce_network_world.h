// SPDX-License-Identifier: GPL-3.0-only
// Typed game boundary for an explicitly admitted two-player network session. Not a resumable game save.
#ifndef CE_NETWORK_WORLD_H
#define CE_NETWORK_WORLD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CE_WORLD_SCHEMA 11u
#define CE_WORLD_LIFECYCLE_SCHEMA 2u
#define CE_WORLD_LIFECYCLE_MAX_BYTES 1000u
// Input layout and world object coverage evolve independently.
#define CE_WORLD_INPUT_SCHEMA 5u
#define CE_WORLD_MAX_ACTIONS 32u
enum CE_WorldRole { CE_WORLD_HOST = 1, CE_WORLD_REPLICA = 2 };
typedef struct CE_WorldConfig { uint32_t role, epoch, local_player, level; } CE_WorldConfig;
enum CE_WorldActionSource { CE_WORLD_ACTION_BINDING = 0, CE_WORLD_ACTION_MOUSE_AXIS = 1 };
typedef struct CE_WorldAction { uint32_t button, source; float value; } CE_WorldAction;
typedef struct CE_WorldInput {
    uint32_t epoch, sequence, tick, player, count, last_applied_birth_serial;
    uint32_t invert_ground, invert_jet, mouse_ground_aim, walker_mouse_any;
    // Scaled angular deltas in radians, BEFORE inversion. The host applies the guest's explicit preferences once.
    float ground_yaw, ground_pitch;
    CE_WorldAction actions[CE_WORLD_MAX_ACTIONS];
} CE_WorldInput;
typedef struct CE_WorldFrame CE_WorldFrame;
typedef struct CE_WorldInfo {
    uint32_t epoch, level, tick, frame_sequence, phase, paused, last_consumed_sequence;
    uint32_t lifecycle_baseline, lifecycle_watermark;
} CE_WorldInfo;
typedef struct CE_WorldBirthAck { uint32_t epoch, serial, tick; } CE_WorldBirthAck;
typedef struct CE_WorldLifecycleInfo {
    uint32_t epoch, baseline_sequence, operation, source_tick, ordinal, kind, id, size;
} CE_WorldLifecycleInfo;
typedef struct CE_WorldLifecycleAck {
    uint32_t epoch, baseline_sequence, operation, source_tick;
} CE_WorldLifecycleAck;
enum CE_WorldLifecycleKind { CE_WORLD_LIFECYCLE_BIRTH=1u, CE_WORLD_LIFECYCLE_DESPAWN=2u, CE_WORLD_LIFECYCLE_ROUND_BIRTH=3u, CE_WORLD_LIFECYCLE_ROUND_DESPAWN=4u };
enum CE_WorldReadiness { CE_WORLD_INVALID=-1, CE_WORLD_DEFER=0, CE_WORLD_READY=1, CE_WORLD_STALE=2 };

// Begin only after the genuine level and both genuine player objects have loaded. There is no implicit opt-in.
// The first adapter deliberately admits level 852 only; unsupported object/controller kinds refuse capture/apply.
int CE_WorldBegin(const CE_WorldConfig *);
// End only after world teardown. A live world keeps its event fence until its objects have been destroyed.
void CE_WorldEnd(void);
const char *CE_WorldError(void);
int CE_WorldActive(void);
int CE_WorldReplica(void);
int CE_WorldCanRender(void); // a replica requires its first successful frame and no poisoned apply
uint32_t CE_WorldLocalPlayer(void);

CE_WorldFrame *CE_WorldFrameCreate(void);
void CE_WorldFrameDestroy(CE_WorldFrame *);
int CE_WorldCapture(CE_WorldFrame *); // host, after gameplay event Flush
int CE_WorldFramePublished(const CE_WorldFrame *); // call ONLY after exact CE_NetSend==1; 1 success, 0 refusal
// Metadata is available only after a whole successful replica Apply; 0 leaves the destination unchanged.
int CE_WorldAppliedBirths(CE_WorldBirthAck *);
// Cumulative application ACK, independent of gameplay input. 1 accepted/idempotent; -1 refused.
int CE_WorldAcknowledgeBirths(uint32_t epoch, uint32_t serial, uint32_t tick);
int CE_WorldApply(const CE_WorldFrame *); // replica, complete validated frame before rendering
// Apply failure after mutation latches a poisoned replica: tear down the world before any rendering/retry.
int CE_WorldEncode(const CE_WorldFrame *, uint8_t *out, uint32_t capacity, uint32_t *size);
int CE_WorldDecode(CE_WorldFrame *, const uint8_t *data, uint32_t size);
int CE_WorldFrameInfo(const CE_WorldFrame *, CE_WorldInfo *);
// Explicit post-baseline opt-in. Session owns proof of reliable peer baseline admission; this API verifies local
// publication/application, epoch and exact baseline bytes, and never clears an existing lifecycle lease or queue.
// The prior local candidate is the latest successful pre-Start FramePublished/whole Apply; Capture/Encode/Send0
// cannot replace it. After Start the same bounded buffer retains the initial owned baseline, until teardown.
int CE_WorldLifecycleStart(const CE_WorldFrame *baseline);
// Each immutable operation fits one <=1000B payload.64 retained operations; Send0 retains exact bytes/metadata.
// Peek/inspect0 or -1 leave destinations unchanged. Published is called ONLY after exact reliable Send1.
int CE_WorldPeekLifecycle(CE_WorldLifecycleInfo *,uint8_t *out,uint32_t capacity);
int CE_WorldLifecyclePublished(const CE_WorldLifecycleInfo *);
int CE_WorldInspectLifecycle(const uint8_t *,uint32_t size,CE_WorldLifecycleInfo *);
// Conventional local presentation: authoritative source tick/order/IDs remain unchanged; native particle/sound
// effects start once at successful local event application and retain native local lifetimes. No global clock
// override, host-age FX reconstruction or inferred expiry. Detached FX can outlive explicit actor despawn.
// Selected pulse rounds retain original entry/initialized state and ordered explicit despawn. Internal ShotID
// associates native weapon context only: muzzle/sound/light replay and transient-flight presentation are separate.
//1 committed,2 exact duplicate,0 fully validated future-order defer,-1 refusal. Native mutation failure poisons.
int CE_WorldApplyLifecycle(const uint8_t *,uint32_t size);
int CE_WorldAppliedLifecycle(CE_WorldLifecycleAck *); // only a whole successful operation commit;0 unchanged
int CE_WorldAcknowledgeLifecycle(const CE_WorldLifecycleAck *); // cumulative published whole-Apply proof
int CE_WorldFrameReadiness(const CE_WorldFrame *); // full validation; latest state cannot overtake operations
int CE_WorldEncodeInput(const CE_WorldInput *, uint8_t *out, uint32_t capacity, uint32_t *size);
int CE_WorldDecodeInput(CE_WorldInput *, const uint8_t *data, uint32_t size);
// Peek one immutable native20Hz sample from the bounded16-batch FIFO; repeated peeks retain exact bytes.
// The caller retains its serialized front until reliable transport accepts it, then acknowledges that sequence.
int CE_WorldSampleInput(CE_WorldInput *); // 1 ready, 0 empty, -1 failure
int CE_WorldInputSent(uint32_t sequence); // 1 popped; -1 mismatch/refusal; call ONLY after CE_NetSend==1
void CE_WorldPollLocalInput(void); // native Update hook; clock-gated20Hz, no merging between sampled ticks
enum CE_WorldInputSuspend { CE_WORLD_INPUT_HOST_WAIT = 1u };
int CE_WorldSetInputSuspended(uint32_t reason_mask); // 1 known mask set/no-op inactive or host; -1 unknown bits
void CE_WorldRebaseLocalSampler(void); // consume current host-tick grant/reset wall ticket; retain FIFO/lease/sequences
uint32_t CE_WorldLastGeneratedSequence(void); // replica generation watermark; 0 inactive/host/no sample
int CE_WorldQueueRemoteInput(const CE_WorldInput *); // 1 queued/duplicate, 0 bounded backpressure, -1 refusal
uint32_t CE_WorldConsumedSequence(void);

// Proposed mirror hooks: all return their original/offline behaviour until Begin succeeds.
int CE_WorldControllerFlush(uint32_t player); // returns 1 when the adapter handled/suppressed this controller
int CE_WorldViewPlayer(int original_player);
float CE_WorldViewAspect(float original_aspect);
int CE_WorldInvertPitch(uint32_t player, int grounded, int original);
int CE_WorldWalkerBindingType(uint32_t player, int original_type);
// Lockstep uses the same native action, order and numeric bounds as the replicated-world input path.
int CE_WorldLockstepValidInput(const CE_WorldInput *input);
void CE_WorldMouseAxis(int on);
int CE_WorldExplosionSpawnAllowed(void);
int CE_WorldRoundSpawnAllowed(void);
void CE_WorldForget(const void *thing); // before CThing::Shutdown destroys the local object
#ifdef __cplusplus
}
class CBattleEngine;
class CCockpit;
class CController;
class CUnit;
class CExplosion;
class CInitThing;
class CRound;
class CActor;
class CWeapon;
class CWeaponModeData;
int CE_WorldBypassMouse(CBattleEngine *);
void CE_WorldCockpitInitialized(CCockpit *); // native Process hook after both old-transform copies
void CE_WorldExplosionBirth(CExplosion *,CInitThing *);
void CE_WorldExplosionReady(CExplosion *,CInitThing *);
void CE_WorldExplosionInitialized(CExplosion *,CInitThing *); // common native Init exit; no reads of invalid token
void CE_WorldLifecycleForget(const void *); // before native shutdown invalidates the controlled object
void CE_WorldLifecycleDestroyed(const void *); // direct-delete/pending-token safety; no duplicate despawn
uint32_t CE_WorldShotBegin(CWeapon *,CWeaponModeData *); // source context only; no muzzle/audio replay
void CE_WorldShotEnd(CWeapon *,uint32_t);
void CE_WorldRoundBirth(CRound *,CInitThing *);
void CE_WorldRoundInitialized(CRound *,CInitThing *);
void CE_WorldRoundShutdown(CRound *);
int CE_WorldActorInitPhase(CActor *,int multiplier,int *out); // exact controlled replica CRound, otherwise offline pass-through
int CE_WorldUnitHitEffect(CUnit *,int part,int *out);
int CE_WorldCaptureAction(CController *, int button, float value);
#endif
#endif
