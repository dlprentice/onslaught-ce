// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_STATE_DELIVERY_H
#define CE_STATE_DELIVERY_H
#include "state_delta.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CE_STATE_DELIVERY_ENVELOPE 20u
#define CE_STATE_DELIVERY_CONTROL_BYTES 184u
// A lost or superseded basis offer costs one status period; at combat image sizes under loss a 1 s cadence let the
// basis fall seconds behind, so deltas grew past what the link delivers whole. Both cadences stay well above one frame.
#define CE_STATE_DELIVERY_STATUS_MS 250u
#define CE_STATE_DELIVERY_OFFER_MS 250u
#define CE_STATE_DELIVERY_TIMEOUT_MS 30000u
#define CE_STATE_DELIVERY_REPAIR_INTERVAL_MS 30000u
enum CE_StateDeliveryResult { CE_DELIVERY_OK = 0, CE_DELIVERY_INVALID = -1, CE_DELIVERY_RETRY = 1,
    CE_DELIVERY_STALE, CE_DELIVERY_REPAIR_REQUIRED, CE_DELIVERY_NOT_READY, CE_DELIVERY_TIMEOUT, CE_DELIVERY_NOT_FIT };
enum CE_StateDeliveryControlKind { CE_DELIVERY_APPLIED = 1, CE_DELIVERY_STATUS, CE_DELIVERY_PENDING,
    CE_DELIVERY_RETIRED, CE_DELIVERY_SUPERSEDED, CE_DELIVERY_MISSING_BASE, CE_DELIVERY_REPAIR, CE_DELIVERY_REPAIR_READY };
typedef struct CE_StateKey {
    uint8_t context[32];
    uint32_t epoch, sequence, tick, size;
    uint8_t digest[32];
} CE_StateKey;
// An opaque caller-supplied proof context, not permission to create a barrier. Session must already hold shared WAIT,
// stop authoritative advancement/new lifecycle operations and drain exactly this existing lifecycle scope/watermark.
typedef struct CE_StateRepairGate { uint32_t wait_generation, lifecycle_baseline, lifecycle_watermark; } CE_StateRepairGate;
typedef struct CE_StateDeliveryControl {
    uint32_t kind, offer_generation;
    CE_StateRepairGate gate;
    CE_StateKey key, active_base; // active_base/gate used only by REPAIR/REPAIR_READY; zero in every other control
} CE_StateDeliveryControl;
typedef struct CE_StateDeliveryTicket {
    uint64_t generation;
    uint32_t offer_generation, repair;
    CE_StateKey key;
} CE_StateDeliveryTicket;
typedef struct CE_StateSender CE_StateSender;
typedef struct CE_StateReceiver CE_StateReceiver;

// Key.digest = BLAKE2b-256("CE-state-applied-v1\0" || context || epoch/sequence/tick/size BE || imageDigest).
// imageDigest is the unchanged state_delta image digest; clocks/floats/lifecycle bytes are never interpreted here.
int CE_StateDeliveryKey(const CE_StateImage *, uint32_t tick, CE_StateKey *);
// Canonical fixed 184-byte CDA1/version1 controls, no Session control-kind reservation or network calls.
int CE_StateDeliveryEncodeControl(const CE_StateDeliveryControl *, uint8_t *out, uint32_t capacity);
int CE_StateDeliveryDecodeControl(const uint8_t *, uint32_t size, CE_StateDeliveryControl *);

// Constant-size allocations only: sender retains exactly two <=64KiB images; receiver retains two plus one prepared
// image and one codec scratch image. No queue/ring allocation. Destroy only after the owning Session stops using it.
CE_StateSender *CE_StateSenderCreate(const uint8_t context[32], uint32_t epoch);
CE_StateReceiver *CE_StateReceiverCreate(const uint8_t context[32], uint32_t epoch);
void CE_StateSenderDestroy(CE_StateSender *);
void CE_StateReceiverDestroy(CE_StateReceiver *);
// Called ONLY after reliable initial BASELINE Send1 / genuine complete baseline WorldApply success, respectively.
// Sender has no usable basis until its exact APPLIED control arrives. Existing Session START barrier remains required.
int CE_StateSenderBaselineSent(CE_StateSender *, const CE_StateImage *, uint32_t tick, int send_result, uint64_t now_ms);
int CE_StateReceiverBaselineApplied(CE_StateReceiver *, const CE_StateImage *, uint32_t tick, uint64_t now_ms);

// CSD1/version1/offerflag/targetTick/baseTick/offerGeneration (20 bytes), followed by unchanged CED1.
// One outstanding preparation. Ordinary Send0 discards it; repair Send0 retains its exact raw image/ticket.
// Validate immediately BEFORE Send; Sent binds the same image/tick and current basis revision AFTER Send. A stale
// post-Send1 ticket is a caller error requiring Session failure, not evidence that an already queued packet vanished.
int CE_StateSenderPrepare(CE_StateSender *, const CE_StateImage *, uint32_t tick, uint64_t now_ms,
                          uint8_t *out, uint32_t capacity, uint32_t *size, CE_StateDeliveryTicket *);
int CE_StateSenderValidate(CE_StateSender *, const CE_StateDeliveryTicket *, const CE_StateImage *, uint32_t tick);
int CE_StateSenderSent(CE_StateSender *, const CE_StateDeliveryTicket *, const CE_StateImage *, uint32_t tick,
                       int send_result, uint64_t now_ms);
int CE_StateSenderControl(CE_StateSender *, const CE_StateDeliveryControl *, uint64_t now_ms, int terminal);
int CE_StateSenderPoll(CE_StateSender *, uint64_t now_ms, CE_StateDeliveryControl *);
int CE_StateSenderControlQueued(CE_StateSender *, const CE_StateDeliveryControl *, int send_result, uint64_t now_ms);
int CE_StateSenderBasis(const CE_StateSender *, CE_StateKey *); // NOT_READY until genuine applied ACK

// No automatic reliable fallback: this is an explicit repair ticket for Session's admitted shared-WAIT transaction.
// Session sends the separate REPAIR announcement and exact raw image via reliable BASELINE-purpose transport,
// retaining the matching WorldFrame across Send0. Raw data has NO added header and may use all 65536 bytes.
// Receiver installs the announcement before emitting REPAIR_READY. Sender cannot expose/send its raw image until
// that exact receipt arrives, so independent reliable channels cannot deliver an unannounced raw image.
// No normal STATE is prepared while repair is required/inflight. No LifecycleStart, epoch reset or clock rewrite.
// Reliable repairs are explicitly limited to one Send1 per 30 seconds; Session must fail boundedly if it cannot
// reach/admit the quiet watermark, queue the exact raw image or obtain whole-Apply proof within its transaction.
int CE_StateSenderPrepareRepair(CE_StateSender *, const CE_StateImage *, uint32_t tick,
                                const CE_StateRepairGate *, uint64_t now_ms,
                                CE_StateDeliveryControl *announcement, CE_StateDeliveryTicket *);
int CE_StateSenderRepairImage(CE_StateSender *, const CE_StateDeliveryTicket *, CE_StateImage *);

// Prepare never acknowledges or applies World state. A successful newer Prepare atomically coalesces raw+ticket;
// failure/unknown base/older arrival preserves the previous prepared image and ticket. WorldDecode, lifecycle READY
// and complete WorldApply must all succeed before Applied is called with the exact surviving ticket/header.
int CE_StateReceiverPrepare(CE_StateReceiver *, const uint8_t *wire, uint32_t size, uint64_t now_ms, CE_StateDeliveryTicket *);
// Same repair-announcement admission as ExpectRepair: OK/STALE/INVALID at the owner's prior clock. Never changes
// any clock, cache, ticket, response or state, including on refusal or an exact duplicate. OK does not install the
// announcement, emit REPAIR_READY or establish Session's shared-WAIT proof; ExpectRepair is still required.
int CE_StateReceiverCheckRepair(const CE_StateReceiver *, const CE_StateDeliveryControl *, const CE_StateRepairGate *, uint64_t now_ms);
int CE_StateReceiverExpectRepair(CE_StateReceiver *, const CE_StateDeliveryControl *, const CE_StateRepairGate *, uint64_t now_ms);
int CE_StateReceiverPrepareRepair(CE_StateReceiver *, const uint8_t *raw, uint32_t size, uint64_t now_ms, CE_StateDeliveryTicket *);
int CE_StateReceiverImage(CE_StateReceiver *, const CE_StateDeliveryTicket *, CE_StateImage *);
int CE_StateReceiverApplied(CE_StateReceiver *, const CE_StateDeliveryTicket *, uint32_t epoch, uint32_t sequence,
                            uint32_t tick, uint64_t now_ms);
int CE_StateReceiverDiscard(CE_StateReceiver *, const CE_StateDeliveryTicket *);
int CE_StateReceiverControl(CE_StateReceiver *, const CE_StateDeliveryControl *, uint64_t now_ms, int terminal);
int CE_StateReceiverPoll(CE_StateReceiver *, uint64_t now_ms, CE_StateDeliveryControl *);
int CE_StateReceiverControlQueued(CE_StateReceiver *, const CE_StateDeliveryControl *, int send_result, uint64_t now_ms);

// All calls on an owner run on one caller thread; no implicit time reads. now_ms is caller monotonic elapsed time,
// distinct from immutable source tick. Invalid/backward times refuse. One status request is outstanding at a time;
// timeout never evicts a basis or promotes Send1. A receiver retirement fence is not its actual Applied floor.
// terminal controls run the same semantic admission without changing clocks, bases, tickets, fences or responses;
// valid terminal calls return STALE. Known malformed identities/phase and impossible future proofs remain invalid.
// Metadata/control outputs must be distinct from every input object/data, owner storage and other outputs.
// Declared byte capacities must be accessible, overflow-safe and disjoint. Refusals leave caller outputs unchanged.
#ifdef __cplusplus
}
#endif
#endif
