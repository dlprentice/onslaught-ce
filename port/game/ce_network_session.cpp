// SPDX-License-Identifier: GPL-3.0-only
// A bounded engineering route, not a finished multiplayer menu or campaign implementation.
// Early boot supplies the prepared source/schema/content admission digest; no caller label substitutes for it.
// Nothing is transmitted before explicit configuration passes admission. Invite secrets are never printed, and
// all deadlines use monotonic time.
#include "ce_network_stl.h"
#include "ce_network_session.h"
#include "ce_network_lockstep.h"
#include "ce_network_world.h"
#include "ce_network_boot.h"
#include "ce_network_loaded_admission.h"
#include "../network/network.h"
#include "../network/ice_game.h"
#include "../network/state_delivery.h"
#include "../network/compatibility.h"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

bool PortClockVirtual();
namespace {
const uint32_t kLevel = 852, kControlMagic = 0x43455331, kControlSchema = 5;
const unsigned kLifecycleWindow = 64;
const uint64_t kWaitMs = 30000;
// A guest holds a prepared basis offer this long for its next whole Apply before a newer ordinary image may replace it.
const uint64_t kOfferHoldMs = 250;
// A host that refuses an incompatible join keeps polling this long so its refusal, which names its version, leaves.
const uint64_t kRefusalFlushMs = 1500;
// After this much silence from the other player during play, the player is told the connection is lost and how long
// the session waits (the transport's silence limit, kWaitMs) before it stops.
const uint32_t kQuietNoticeMs = 2000;
// ENet caps the link at 256 KiB/s and enforces it by dropping single unsequenced packets, here single parts of a state
// image, so a run of large deltas (17 KB each after a lifecycle burst) then never arrives whole and the basis offer
// that would shrink them is lost too. State is sent within this budget instead; an image that does not fit is not
// sent (the Send0 path) and a later one goes when it does. The burst holds the largest delta Prepare emits.
const uint64_t kStateBytesPerSecond = 160 * 1024, kStateBurstBytes = 65536;
// The label each side shows the other when their builds do not admit each other.
#ifdef CE_VERSION
const char kVersionLabel[] = CE_VERSION;
#else
const char kVersionLabel[] = "private build";
#endif
enum ExitOutcome { EXIT_NONE, EXIT_GRACEFUL, EXIT_ERROR };
ExitOutcome exit_outcome = EXIT_NONE;
enum Stage { OFF, LOADING, LOADED, PREPARING, BASELINED, RUNNING, DRAINING, FAILED };
enum Control { PEER_LOADED = 1, BASELINE_APPLIED, START, START_APPLIED, LEAVING, HOST_WAIT, HOST_WAIT_ACK, HOST_RESUME, HOST_RESUME_ACK, BIRTHS_APPLIED,
    GUEST_BLOCK, GUEST_READY, GUEST_BLOCK_ACK, GUEST_READY_ACK, LIFECYCLE_APPLIED };
enum WaitPhase { WAIT_NONE, WAIT_SEND, WAIT_ACK, WAIT_HELD, RESUME_SEND, RESUME_ACK };
enum RepairPhase { REPAIR_OFF, REPAIR_QUIESCE, REPAIR_WAIT, REPAIR_PREPARED, REPAIR_SENT, REPAIR_APPLIED };
enum { QUIT_FRONTEND = 1, QUIT_SYSTEM = 2, QUIT_LOAD_ERROR = 3, QUIT_RESTART = 4, QUIT_USER_FRONTEND = 6 };
struct Session {
    CE_NetSession *net = NULL;
    CE_WorldFrame *capture = NULL, *incoming = NULL;
    Stage stage = OFF;
    bool requested = false, host = false, peer_loaded = false, peer_epoch_loaded = false;
    bool mission_vm_blocked = false, peer_leaving = false;
    bool baseline_applied = false, start_seen = false, start_applied = false, start_queued = false, world_started = false;
    bool have_frame = false, have_baseline = false, have_input = false;
    bool shared_wait = false, device_unready = false, local_blocked = false;
    bool service_ready_seen = false, device_ready_seen = false;
    bool wait_ack_pending = false, resume_ack_pending = false;
    bool native_unready = false, guest_desired_blocked = false, guest_reported_blocked = false;
    bool guest_request_pending = false, guest_request_queued = false, guest_request_blocked = false;
    bool peer_guest_blocked = false, peer_guest_ack_pending = false;
    uint32_t guest_generation = 0, guest_request_tick = 0, peer_guest_generation = 0, peer_guest_tick = 0;
    uint64_t guest_request_since = 0, peer_guest_ack_since = 0;
    WaitPhase wait_phase = WAIT_NONE;
    uint32_t wait_generation = 0, completed_wait = 0, wait_tick = 0;
    CE_StateRepairGate authorized_repair_gate = {};
    uint32_t authorized_repair_tick = 0;
    uint64_t wait_deadline = 0;
    unsigned remote_first = 0, remote_count = 0;
    uint32_t epoch = 0, level = 0, baseline_sequence = 0, baseline_tick = 0, last_capture_tick = 0;
    uint32_t input_size = 0;
    CE_WorldBirthAck birth_ack = {};
    uint32_t birth_ack_sent = 0;
    bool lifecycle_started = false, lifecycle_front = false, lifecycle_blocked = false;
    CE_WorldLifecycleInfo lifecycle_pending = {};
    uint8_t lifecycle_bytes[CE_WORLD_LIFECYCLE_MAX_BYTES] = {};
    CE_WorldLifecycleInfo lifecycle_outstanding[kLifecycleWindow] = {}, lifecycle_retired[kLifecycleWindow] = {};
    unsigned lifecycle_first = 0, lifecycle_count = 0, retired_first = 0, retired_count = 0;
    uint32_t lifecycle_published = 0, lifecycle_received = 0, lifecycle_acked = 0;
    uint64_t lifecycle_front_since = 0, lifecycle_sent_at[kLifecycleWindow] = {};
    struct LifecyclePacket { CE_WorldLifecycleInfo info; uint8_t bytes[CE_WORLD_LIFECYCLE_MAX_BYTES]; };
    LifecyclePacket lifecycle_incoming[kLifecycleWindow] = {};
    unsigned lifecycle_in_first = 0, lifecycle_in_count = 0;
    CE_WorldLifecycleAck lifecycle_ack = {};
    uint32_t lifecycle_ack_sent = 0;
    CE_StateSender *state_sender = NULL;
    CE_StateReceiver *state_receiver = NULL;
    uint8_t state_context[32] = {}, state_raw[CE_STATE_DELTA_MAX_IMAGE] = {};
    uint32_t state_raw_size = 0;
    CE_StateDeliveryTicket state_ticket = {};
    bool state_prepared = false, state_pending = false, state_pending_repair = false;
    uint64_t state_offer_since = 0;
    CE_StateKey state_pending_key = {};
    uint32_t state_pending_offer = 0;
    uint64_t state_pending_since = 0;
    RepairPhase repair_phase = REPAIR_OFF;
    uint64_t repair_since = 0;
    CE_StateRepairGate repair_gate = {};
    CE_StateDeliveryControl repair_announcement = {};
    CE_StateDeliveryTicket repair_ticket = {};
    CE_WorldInfo repair_info = {};
    bool repair_capture_ready = false;
    bool repair_installed = false, repair_ready_sent = false, repair_ack_sent = false;
    uint64_t last_periodic = 0;
    uint64_t refused_at = 0;
    uint32_t quiet_left = 0; char notice[96] = {};
    uint64_t state_credit = kStateBurstBytes * 1000, state_credit_at = 0; // byte-milliseconds
    CE_WorldInput remote[32] = {};
    uint8_t wire[CE_NET_MAX_MESSAGE], input[CE_NET_MAX_INPUT];
};
Session s;
char error_text[192];

bool EnvPresent(const char *name) { return getenv(name) != NULL; }
bool Fail(const char *message) {
    exit_outcome = EXIT_ERROR;
    if (s.stage != FAILED) {
        snprintf(error_text, sizeof error_text, "%s", message);
        fprintf(stderr, "onslaught-ce: network engineering session stopped: %s\n", error_text);
    }
    s.stage = FAILED;
    // Failure is an abort, never an authenticated graceful leave. Publish the detached pointer before destruction.
    CE_NetSession *net = s.net; s.net = NULL;
    CE_NetDestroy(net);
    return false;
}
bool WorldFail(const char *where) {
    char text[192]; snprintf(text, sizeof text, "%s: %.125s", where, CE_WorldError()); return Fail(text);
}
int SendControl(unsigned kind, uint32_t sequence, uint32_t tick);
bool DriveWait();
bool HostUnavailable();
bool GuestMayResume();
bool DriveStateControls();
bool DriveRepairHost();
bool DriveRepairGuest();
bool HandleStateControl(const CE_NetMessage &, const uint8_t *, bool terminal);
bool ReceiveStateImage(const CE_NetMessage &, const uint8_t *, bool repair);
bool StateExpired(uint64_t now);
bool RepairLocalReady();
int HostQuietPrefix();
bool RepairHold() {
    return s.host && s.repair_phase >= REPAIR_WAIT && s.repair_phase < REPAIR_APPLIED;
}
bool HostNeedsWait() { return HostUnavailable() || RepairHold(); }
bool StateKeyEqual(const CE_StateKey &a, const CE_StateKey &b) {
    return a.epoch == b.epoch && a.sequence == b.sequence && a.tick == b.tick && a.size == b.size &&
           !memcmp(a.context, b.context, sizeof a.context) && !memcmp(a.digest, b.digest, sizeof a.digest);
}
bool RepairGateEqual(const CE_StateRepairGate &a, const CE_StateRepairGate &b) {
    return a.wait_generation == b.wait_generation && a.lifecycle_baseline == b.lifecycle_baseline &&
           a.lifecycle_watermark == b.lifecycle_watermark;
}
CE_StateImage StateImage(const CE_WorldInfo &info) {
    CE_StateImage image = {};
    memcpy(image.identity.context, s.state_context, sizeof s.state_context);
    image.identity.epoch = info.epoch; image.identity.sequence = info.frame_sequence;
    image.data = s.state_raw; image.size = s.state_raw_size;
    return image;
}
bool StateFailure(const char *where, int result) {
    char text[192]; snprintf(text, sizeof text, "%s (%d)", where, result); return Fail(text);
}
bool AdmissionBoundary() {
    char error[160] = {};
    if (CE_CompatibilityCheckBoundary(error, sizeof error) != 1) {
        char text[192]; snprintf(text, sizeof text, "network admission boundary refused: %.140s", error); return Fail(text);
    }
    if (CE_NetLoadedAdmissionValidate() != 1) {
        char text[192]; snprintf(text, sizeof text, "network loaded admission refused: %.145s", CE_NetLoadedAdmissionError()); return Fail(text);
    }
    return true;
}
bool AdmissionCurrent() {
    CE_CompatibilityResult current = {}; char error[160] = {};
    if (CE_CompatibilityPrepared(&current, error, sizeof error) == 1 &&
        !memcmp(current.digest, s.state_context, sizeof s.state_context)) return true;
    char text[192]; snprintf(text, sizeof text, "network admission lost: %.155s", error); return Fail(text);
}
void StatePending(const CE_StateKey &key, uint32_t offer, bool repair, uint64_t now) {
    s.state_pending = true; s.state_pending_key = key; s.state_pending_offer = offer;
    s.state_pending_repair = repair; s.state_pending_since = now;
}
void RequestRepair() {
    if (s.repair_phase == REPAIR_OFF) {
        s.repair_phase = REPAIR_QUIESCE; s.repair_since = CE_NetNowMs();
    }
}
bool StateExpired(uint64_t now) {
    if (s.state_pending && (now < s.state_pending_since || now - s.state_pending_since >= kWaitMs)) return true;
    return s.repair_phase != REPAIR_OFF && (now < s.repair_since || now - s.repair_since >= kWaitMs);
}
void ClearRepair() {
    s.repair_phase = REPAIR_OFF; s.repair_since = 0; s.repair_gate = {};
    s.repair_announcement = {}; s.repair_ticket = {};
    s.repair_info = {}; s.repair_capture_ready = false;
    s.repair_installed = s.repair_ready_sent = s.repair_ack_sent = false;
}
bool StartLifecycle(const CE_WorldFrame *baseline) {
    if (!AdmissionCurrent()) return false;
    if (CE_WorldLifecycleStart(baseline) != 1) return WorldFail("lifecycle baseline admission refused");
    s.lifecycle_started = true;
    return true;
}
bool RuntimeReady() {
    return !s.shared_wait && !s.local_blocked && (s.host ? !s.peer_guest_blocked :
           (!s.guest_desired_blocked && !s.guest_request_pending && !s.guest_reported_blocked));
}
bool LifecycleTuple(const CE_WorldLifecycleInfo &info, const CE_WorldLifecycleAck &ack) {
    return info.epoch == ack.epoch && info.baseline_sequence == ack.baseline_sequence &&
           info.operation == ack.operation && info.source_tick == ack.source_tick;
}
bool LifecycleExpired(uint64_t now) {
    if (!s.host || !s.lifecycle_started) return false;
    if (s.lifecycle_front && (now < s.lifecycle_front_since || now - s.lifecycle_front_since >= kWaitMs)) return true;
    if (!s.lifecycle_count) return false;
    uint64_t since = s.lifecycle_sent_at[s.lifecycle_first];
    return now < since || now - since >= kWaitMs;
}
bool LifecycleReceipt(const CE_NetMessage &message, uint32_t operation, uint32_t tick,
                      uint32_t baseline, bool terminal) {
    if (!s.host || !s.lifecycle_started || (s.stage != RUNNING && s.stage != BASELINED) ||
        message.epoch != s.epoch || baseline != s.baseline_sequence || !operation || !tick)
        return Fail("lifecycle acknowledgement outside its admitted baseline");
    CE_WorldLifecycleAck ack = {message.epoch, baseline, operation, tick};
    // Transport delivery is not application proof. Only an exact tuple from the retained published prefix
    // (or its bounded duplicate history) may reach the World acknowledgement API, including terminal queues.
    for (unsigned i = 0; i < s.retired_count; ++i) {
        const CE_WorldLifecycleInfo &info = s.lifecycle_retired[(s.retired_first + i) % kLifecycleWindow];
        if (info.operation == operation)
            return LifecycleTuple(info, ack) || Fail("lifecycle duplicate acknowledgement changed its tuple");
    }
    unsigned prefix = 0;
    for (; prefix < s.lifecycle_count; ++prefix) {
        const CE_WorldLifecycleInfo &info = s.lifecycle_outstanding[(s.lifecycle_first + prefix) % kLifecycleWindow];
        if (info.operation != s.lifecycle_acked + prefix + 1) return Fail("lifecycle acknowledgement prefix is incomplete");
        if (info.operation == operation) break;
    }
    if (prefix == s.lifecycle_count || !LifecycleTuple(s.lifecycle_outstanding[(s.lifecycle_first + prefix) % kLifecycleWindow], ack))
        return Fail("lifecycle acknowledgement has no exact published prefix");
    if (terminal) return true;
    if (CE_WorldAcknowledgeLifecycle(&ack) != 1) return WorldFail("lifecycle acknowledgement refused");
    for (unsigned i = 0; i <= prefix; ++i) {
        if (s.retired_count == kLifecycleWindow) { s.retired_first = (s.retired_first + 1) % kLifecycleWindow; --s.retired_count; }
        s.lifecycle_retired[(s.retired_first + s.retired_count++) % kLifecycleWindow] = s.lifecycle_outstanding[s.lifecycle_first];
        s.lifecycle_first = (s.lifecycle_first + 1) % kLifecycleWindow; --s.lifecycle_count;
    }
    s.lifecycle_acked = operation;
    return true;
}
bool DriveLifecycle() {
    if (!s.host || !s.lifecycle_started || s.stage != RUNNING) return true;
    if (LifecycleExpired(CE_NetNowMs())) return Fail("lifecycle publication or application acknowledgement timed out");
    s.lifecycle_blocked = false;
    if (!RuntimeReady()) return true;
    for (unsigned budget = 0; budget < kLifecycleWindow; ++budget) {
        if (s.lifecycle_count == kLifecycleWindow) { s.lifecycle_blocked = true; return true; }
        if (!s.lifecycle_front) {
            int next = CE_WorldPeekLifecycle(&s.lifecycle_pending, s.lifecycle_bytes, sizeof s.lifecycle_bytes);
            if (next < 0) return WorldFail("lifecycle source refused");
            if (!next) return true;
            const CE_WorldLifecycleInfo &info = s.lifecycle_pending;
            if (info.epoch != s.epoch || info.baseline_sequence != s.baseline_sequence ||
                s.lifecycle_published == UINT32_MAX || info.operation != s.lifecycle_published + 1 ||
                !info.source_tick || !info.size || info.size > sizeof s.lifecycle_bytes)
                return Fail("lifecycle source returned an invalid prefix");
            s.lifecycle_front = true;
            s.lifecycle_front_since = CE_NetNowMs();
        }
        int sent = CE_NetSend(s.net, CE_NET_LIFECYCLE, s.lifecycle_pending.source_tick,
                              s.lifecycle_bytes, s.lifecycle_pending.size);
        if (sent < 0) return Fail("invalid lifecycle send");
        if (!sent) { s.lifecycle_blocked = true; return true; }
        if (CE_WorldLifecyclePublished(&s.lifecycle_pending) != 1) return WorldFail("lifecycle publication refused");
        unsigned slot = (s.lifecycle_first + s.lifecycle_count++) % kLifecycleWindow;
        s.lifecycle_outstanding[slot] = s.lifecycle_pending; s.lifecycle_sent_at[slot] = CE_NetNowMs();
        s.lifecycle_published = s.lifecycle_pending.operation; s.lifecycle_front = false;
    }
    s.lifecycle_blocked = true;
    return true;
}
bool DriveLifecycleAck() {
    if (s.host || !s.lifecycle_started || s.stage != RUNNING) return true;
    if (s.lifecycle_ack.operation <= s.lifecycle_ack_sent) return true;
    uint8_t data[28]; CE_NetWriter w; CE_NetWriterInit(&w, data, sizeof data);
    CE_NetWriteU32(&w, kControlMagic); CE_NetWriteU32(&w, kControlSchema);
    CE_NetWriteU32(&w, LIFECYCLE_APPLIED); CE_NetWriteU32(&w, s.level);
    CE_NetWriteU32(&w, s.lifecycle_ack.operation); CE_NetWriteU32(&w, s.lifecycle_ack.source_tick);
    CE_NetWriteU32(&w, s.lifecycle_ack.baseline_sequence);
    int sent = CE_NetSend(s.net, CE_NET_CONTROL, s.lifecycle_ack.source_tick, data, w.size);
    if (sent < 0) return Fail("invalid lifecycle acknowledgement send");
    if (sent == 1) s.lifecycle_ack_sent = s.lifecycle_ack.operation;
    return true;
}
int ReplicaReadiness() {
    int ready = CE_WorldFrameReadiness(s.incoming);
    if (ready != CE_WORLD_READY && ready != CE_WORLD_DEFER && ready != CE_WORLD_STALE) {
        WorldFail("replica frame readiness refused"); return CE_WORLD_INVALID;
    }
    if (ready == CE_WORLD_STALE) {
        if (s.state_prepared) {
            int result = CE_StateReceiverDiscard(s.state_receiver, &s.state_ticket);
            if (result != CE_DELIVERY_OK) { StateFailure("stale state ticket could not be discarded", result); return CE_WORLD_INVALID; }
            s.state_prepared = false;
        }
        s.have_frame = false;
    }
    return ready;
}
bool ApplyLifecycle() {
    if (!RuntimeReady()) return true;
    while (s.lifecycle_in_count) {
        const Session::LifecyclePacket &packet = s.lifecycle_incoming[s.lifecycle_in_first];
        int result = CE_WorldApplyLifecycle(packet.bytes, packet.info.size);
        if (result < 0) return WorldFail("replica lifecycle apply refused");
        if (!AdmissionCurrent()) return false;
        // Channel 3 is reliable and ordered. A missing prefix is a protocol failure, never permission to skip it.
        if (result != 1 && result != 2) return Fail("replica lifecycle application is missing its prefix");
        CE_WorldLifecycleAck proof = {};
        if (CE_WorldAppliedLifecycle(&proof) != 1 || !LifecycleTuple(packet.info, proof))
            return Fail("replica lifecycle has no exact whole-apply proof");
        s.lifecycle_ack = proof;
        s.lifecycle_in_first = (s.lifecycle_in_first + 1) % kLifecycleWindow; --s.lifecycle_in_count;
    }
    return DriveLifecycleAck();
}
bool DriveBirthAck() {
    if (s.host || !s.world_started || (s.stage != BASELINED && s.stage != RUNNING)) return true;
    CE_WorldBirthAck proof = {};
    if (!CE_WorldAppliedBirths(&proof)) { s.birth_ack = {}; return true; }
    if (proof.epoch != s.epoch || !proof.tick) return Fail("committed birth acknowledgement has invalid epoch or tick");
    if (!proof.serial) { s.birth_ack = {}; return true; }
    // Only a completed Apply grants this cumulative proof. Newer proof may supersede an unsent ACK;
    // retained gameplay input bytes and their sequence are independent and never rewritten here.
    if (proof.serial > s.birth_ack.serial) s.birth_ack = proof;
    if (s.birth_ack.serial <= s.birth_ack_sent) return true;
    int sent = SendControl(BIRTHS_APPLIED, s.birth_ack.serial, s.birth_ack.tick);
    if (sent < 0) return Fail("invalid birth acknowledgement send");
    if (sent == 1) s.birth_ack_sent = s.birth_ack.serial;
    return true;
}
bool Hex(const char *text, uint8_t *out) {
    if (!text || strlen(text) != 64) return false;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned value = 0;
        for (unsigned j = 0; j < 2; ++j) {
            unsigned char c = (unsigned char)text[i * 2 + j];
            unsigned n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
            if (n > 15) return false;
            value = value * 16 + n;
        }
        out[i] = (uint8_t)value;
    }
    unsigned nonzero = 0; for (unsigned i = 0; i < 32; ++i) nonzero |= out[i];
    return nonzero != 0;
}
bool Endpoint(const char *text, char *ip, uint16_t &port) {
    if (!text || strlen(text) > 21) return false;
    const char *p = text; unsigned used = 0;
    for (unsigned part = 0; part < 4; ++part) {
        unsigned value = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3 || used >= 15) return false;
            value = value * 10 + (*p - '0'); ip[used++] = *p++;
        }
        if (!digits || value > 255 || (digits > 1 && p[-(int)digits] == '0')) return false;
        if (part < 3) { if (*p++ != '.') return false; ip[used++] = '.'; }
    }
    ip[used] = 0;
    if (*p++ != ':') return false;
    unsigned value = 0, digits = 0;
    while (*p >= '0' && *p <= '9') { if (++digits > 5) return false; value = value * 10 + (*p++ - '0'); }
    if (*p || !digits || !value || value > 65535) return false;
    port = (uint16_t)value; return true;
}
// The compatibility digests differ, so these builds never admit each other: say so in the player's terms.
bool Incompatible(const CE_NetStatus &status) {
    char theirs[CE_NET_VERSION_BYTES + 1] = {}, text[192];
    if (!CE_NetPeerVersion(s.net, theirs, sizeof theirs))
        snprintf(text, sizeof text, "the other player has a different Onslaught CE version or game files (yours is %s)", kVersionLabel);
    else if (strcmp(theirs, kVersionLabel))
        snprintf(text, sizeof text, "the other player has a different Onslaught CE version: yours is %s, theirs is %s", kVersionLabel, theirs);
    else
        snprintf(text, sizeof text, "the other player's game files or build differ from yours (both Onslaught CE %s)", kVersionLabel);
    if (status.phase == CE_NET_FAILED || !s.host) return Fail(text);
    const uint64_t now = CE_NetNowMs();
    if (!s.refused_at) { s.refused_at = now ? now : 1; fprintf(stderr, "onslaught-ce: %s; telling the other player\n", text); }
    return now - s.refused_at < kRefusalFlushMs ? true : Fail(text);
}
bool Status(CE_NetStatus &status) {
    CE_NetGetStatus(s.net, &status);
    if (status.phase == CE_NET_FAILED && CE_NetGameProblem(s.net)[0]) return Fail(CE_NetGameProblem(s.net));
    if (status.error == CE_NET_INCOMPATIBLE) return Incompatible(status);
    if (status.phase == CE_NET_FAILED || (status.phase == CE_NET_CLOSED && status.error != CE_NET_PEER_LEFT)) return Fail("peer left, timed out, or failed transport admission");
    return true;
}
bool TerminalTransport() {
    CE_NetStatus status; CE_NetGetStatus(s.net, &status);
    return status.phase == CE_NET_CLOSED || status.phase == CE_NET_FAILED;
}
int SendControl(unsigned kind, uint32_t sequence = 0, uint32_t tick = 0) {
    uint8_t data[24]; CE_NetWriter w; CE_NetWriterInit(&w, data, sizeof data);
    CE_NetWriteU32(&w, kControlMagic); CE_NetWriteU32(&w, kControlSchema);
    CE_NetWriteU32(&w, kind); CE_NetWriteU32(&w, s.level); CE_NetWriteU32(&w, sequence); CE_NetWriteU32(&w, tick);
    return CE_NetSend(s.net, CE_NET_CONTROL, tick, data, w.size);
}
void LatchGuestAvailability() {
    if (s.host) return;
    // Read the owned window before consuming a possible RESUME. Unknown device readiness cannot clear a loss.
    s.local_blocked = s.native_unready || s.device_unready || CE_SessionWindowStatus() != 0;
    s.guest_desired_blocked = s.local_blocked;
}
bool HostUnavailable() { return s.local_blocked || s.peer_guest_blocked; }
bool GuestMayResume() {
    return !s.guest_desired_blocked && !s.guest_request_pending && !s.guest_reported_blocked;
}
bool RepairLocalReady() {
    if (s.stage != RUNNING || CE_SessionGameQuitCode() || !s.service_ready_seen || !s.device_ready_seen ||
        s.native_unready || s.device_unready || s.local_blocked || CE_SessionWindowStatus() != 0) return false;
    return s.host ? !s.peer_guest_blocked && !s.peer_guest_ack_pending : GuestMayResume();
}
bool RepairPrefix() {
    return s.repair_gate.lifecycle_baseline == s.baseline_sequence &&
           (s.host ? !s.lifecycle_count && !s.lifecycle_front && s.lifecycle_acked == s.repair_gate.lifecycle_watermark &&
                     s.lifecycle_published == s.repair_gate.lifecycle_watermark :
                     !s.lifecycle_in_count && s.lifecycle_received == s.repair_gate.lifecycle_watermark &&
                     s.lifecycle_ack.operation == s.repair_gate.lifecycle_watermark);
}
bool RepairHeldGate() {
    return s.shared_wait && s.wait_generation == s.repair_gate.wait_generation && s.wait_tick != 0 &&
           (s.host ? s.wait_phase == WAIT_HELD : !s.wait_ack_pending) && RepairPrefix();
}
bool StateFrameMatches(const CE_StateDeliveryTicket &ticket, CE_WorldInfo &info) {
    if (!CE_WorldFrameInfo(s.incoming, &info)) return WorldFail("decoded state header unavailable");
    if (info.epoch != ticket.key.epoch || info.frame_sequence != ticket.key.sequence || info.tick != ticket.key.tick ||
        info.epoch != s.epoch || info.level != s.level) return Fail("decoded state header differs from delivery ticket");
    return true;
}
bool ReceiveStateImage(const CE_NetMessage &message, const uint8_t *bytes, bool repair) {
    if (s.host || s.stage != RUNNING || !s.state_receiver || message.epoch != s.epoch)
        return Fail("state image arrived outside its replica barrier");
    if (repair) {
        if (s.repair_phase == REPAIR_OFF || !s.repair_installed || !s.repair_ready_sent ||
            message.tick != s.repair_announcement.key.tick || message.size != s.repair_announcement.key.size)
            return Fail("raw repair has no matching installed and queued READY");
        if (s.repair_phase == REPAIR_APPLIED || (s.state_prepared && s.state_ticket.repair)) {
            CE_StateImage duplicate = {}; memcpy(duplicate.identity.context, s.state_context, sizeof s.state_context);
            duplicate.identity.epoch = s.epoch; duplicate.identity.sequence = s.repair_announcement.key.sequence;
            duplicate.data = bytes; duplicate.size = message.size; CE_StateKey key = {};
            if (CE_StateDeliveryKey(&duplicate, message.tick, &key) != CE_DELIVERY_OK ||
                !StateKeyEqual(key, s.repair_announcement.key)) return Fail("duplicate raw repair changed its image");
            return true;
        }
    } else {
        // Only the envelope tick is inspected here. The actual delivery/codec implementation admits the header,
        // context, basis, digest and canonical delta; an unknown base must not hide a mismatched transport tick.
        CE_NetReader r; CE_NetReaderInit(&r, bytes, message.size);
        CE_NetReadU32(&r); CE_NetReadU32(&r); uint32_t tick = CE_NetReadU32(&r);
        if (r.error || tick != message.tick) return Fail("state delta envelope differs from its target tick");
        // Whole-Apply an offered basis before a newer ordinary image may replace it. A guest that renders slower than
        // the state rate would otherwise drop every periodic offer in phase, the sender's basis would never advance
        // and the growing deltas would end in transport throttling.
        if (s.state_prepared && s.state_ticket.offer_generation && !s.state_ticket.repair &&
            CE_NetNowMs() - s.state_offer_since < kOfferHoldMs) return true;
    }
    CE_StateDeliveryTicket ticket = {};
    int prepared = repair ? CE_StateReceiverPrepareRepair(s.state_receiver, bytes, message.size, CE_NetNowMs(), &ticket) :
                            CE_StateReceiverPrepare(s.state_receiver, bytes, message.size, CE_NetNowMs(), &ticket);
    if (!repair && (prepared == CE_DELIVERY_STALE || prepared == CE_DELIVERY_RETRY || prepared == CE_DELIVERY_REPAIR_REQUIRED)) return true;
    if (prepared != CE_DELIVERY_OK) return StateFailure("state image preparation refused", prepared);
    CE_StateImage image = {};
    if (CE_StateReceiverImage(s.state_receiver, &ticket, &image) != CE_DELIVERY_OK) return Fail("prepared state image is no longer current");
    if (!CE_WorldDecode(s.incoming, image.data, image.size)) return WorldFail("prepared state decode refused");
    CE_WorldInfo info = {};
    if (!StateFrameMatches(ticket, info)) return false;
    if (info.tick != message.tick) return Fail("decoded state differs from its transport tick");
    if (repair && (info.lifecycle_baseline != s.repair_gate.lifecycle_baseline ||
                   info.lifecycle_watermark != s.repair_gate.lifecycle_watermark || info.tick != s.wait_tick))
        return Fail("raw repair differs from its quiet lifecycle gate");
    s.state_ticket = ticket; s.state_prepared = s.have_frame = true;
    if (ticket.offer_generation && !repair) s.state_offer_since = CE_NetNowMs();
    return true;
}
bool HandleStateControl(const CE_NetMessage &message, const uint8_t *bytes, bool terminal) {
    CE_StateDeliveryControl control = {};
    if (CE_StateDeliveryDecodeControl(bytes, message.size, &control) != CE_DELIVERY_OK ||
        message.epoch != s.epoch || control.key.epoch != message.epoch || control.key.tick != message.tick ||
        memcmp(control.key.context, s.state_context, sizeof s.state_context) ||
        (s.stage != BASELINED && s.stage != RUNNING)) return Fail("malformed or out-of-barrier state control");
    if (s.host) {
        bool repair_proof = (control.kind == CE_DELIVERY_APPLIED && s.state_pending_repair &&
                             control.offer_generation == s.state_pending_offer && StateKeyEqual(control.key, s.state_pending_key)) ||
                            (control.kind == CE_DELIVERY_REPAIR_READY && s.repair_phase == REPAIR_PREPARED &&
                             control.offer_generation == s.repair_ticket.offer_generation && StateKeyEqual(control.key, s.repair_ticket.key));
        if (!terminal && repair_proof) {
            if (!RepairHeldGate()) return Fail("repair proof arrived after its held prefix changed");
            int quiet = HostQuietPrefix(); if (quiet < 0) return false;
            if (!quiet) return Fail("repair proof crossed a new lifecycle operation");
        }
        int admitted = CE_StateSenderControl(s.state_sender, &control, CE_NetNowMs(), terminal ? 1 : 0);
        if (admitted == CE_DELIVERY_INVALID || admitted == CE_DELIVERY_TIMEOUT || admitted == CE_DELIVERY_NOT_READY)
            return StateFailure("state sender control refused", admitted);
        if (terminal || admitted == CE_DELIVERY_STALE) return true;
        if (admitted == CE_DELIVERY_REPAIR_REQUIRED) { RequestRepair(); return true; }
        if (admitted != CE_DELIVERY_OK) return StateFailure("unexpected state sender control result", admitted);
        if (control.kind == CE_DELIVERY_APPLIED || control.kind == CE_DELIVERY_RETIRED || control.kind == CE_DELIVERY_SUPERSEDED) {
            if (!s.state_pending || control.offer_generation != s.state_pending_offer || !StateKeyEqual(control.key, s.state_pending_key))
                return Fail("state delivery proof has no exact caller publication obligation");
            if (s.state_pending_repair) {
                if (control.kind != CE_DELIVERY_APPLIED || s.repair_phase != REPAIR_SENT ||
                    !StateKeyEqual(control.key, s.repair_ticket.key)) return Fail("repair cannot retire without exact whole-Apply proof");
                s.repair_phase = REPAIR_APPLIED;
            }
            s.state_pending = s.state_pending_repair = false; s.state_pending_since = 0;
        }
        return true;
    }
    if (control.kind != CE_DELIVERY_REPAIR) {
        int admitted = CE_StateReceiverControl(s.state_receiver, &control, CE_NetNowMs(), terminal ? 1 : 0);
        return admitted == CE_DELIVERY_OK || admitted == CE_DELIVERY_STALE || StateFailure("state receiver control refused", admitted);
    }
    if (s.stage != RUNNING || !s.lifecycle_started ||
        !RepairGateEqual(control.gate, s.authorized_repair_gate) || control.key.tick != s.authorized_repair_tick)
        return Fail("repair announcement has no actually acknowledged quiet WAIT gate");
    if (s.repair_phase != REPAIR_OFF &&
        (!RepairGateEqual(control.gate, s.repair_announcement.gate) ||
         control.offer_generation != s.repair_announcement.offer_generation ||
         !StateKeyEqual(control.key, s.repair_announcement.key) || !StateKeyEqual(control.active_base, s.repair_announcement.active_base)))
        return Fail("repair announcement changed the retained transaction");
    int admitted = CE_StateReceiverCheckRepair(s.state_receiver, &control, &s.authorized_repair_gate, CE_NetNowMs());
    if (admitted != CE_DELIVERY_OK && admitted != CE_DELIVERY_STALE) return StateFailure("repair announcement admission refused", admitted);
    if (terminal || admitted == CE_DELIVERY_STALE) return true;
    if (!s.shared_wait || s.wait_ack_pending || control.gate.wait_generation != s.wait_generation ||
        control.gate.lifecycle_baseline != s.baseline_sequence || s.lifecycle_in_count ||
        control.gate.lifecycle_watermark != s.lifecycle_ack.operation || s.lifecycle_received != s.lifecycle_ack.operation)
        return Fail("repair announcement does not match the current held quiet prefix");
    if (s.repair_phase == REPAIR_OFF) {
        s.repair_phase = REPAIR_WAIT; s.repair_since = CE_NetNowMs();
        s.repair_gate = control.gate; s.repair_announcement = control;
    }
    return true;
}
bool DriveStateControls() {
    if (!s.world_started || (!s.state_sender && !s.state_receiver)) return true;
    CE_StateDeliveryControl control = {}; uint64_t now = CE_NetNowMs();
    int pending = s.host ? CE_StateSenderPoll(s.state_sender, now, &control) : CE_StateReceiverPoll(s.state_receiver, now, &control);
    if (pending == CE_DELIVERY_RETRY) return true;
    if (pending != CE_DELIVERY_OK) return StateFailure("state delivery service refused", pending);
    if (control.kind == CE_DELIVERY_REPAIR || control.kind == CE_DELIVERY_REPAIR_READY) {
        if (!RepairHeldGate()) return Fail("repair control lost its held quiet gate");
        if (!RepairLocalReady()) return true;
        if (s.host ? s.repair_phase != REPAIR_PREPARED : !s.repair_installed)
            return Fail("repair control has no prepared transaction");
    }
    uint8_t bytes[CE_STATE_DELIVERY_CONTROL_BYTES];
    if (CE_StateDeliveryEncodeControl(&control, bytes, sizeof bytes) != CE_DELIVERY_OK)
        return Fail("state control encoding refused");
    int sent = CE_NetSend(s.net, CE_NET_STATE_CONTROL, control.key.tick, bytes, sizeof bytes);
    if (sent < 0) return Fail("invalid state control send");
    int queued = s.host ? CE_StateSenderControlQueued(s.state_sender, &control, sent, now) :
                          CE_StateReceiverControlQueued(s.state_receiver, &control, sent, now);
    if (queued != (sent ? CE_DELIVERY_OK : CE_DELIVERY_RETRY)) return StateFailure("state control queue ticket changed", queued);
    if (sent && !s.host) {
        if (control.kind == CE_DELIVERY_REPAIR_READY) s.repair_ready_sent = true;
        if (control.kind == CE_DELIVERY_APPLIED && s.repair_phase == REPAIR_APPLIED &&
            StateKeyEqual(control.key, s.state_ticket.key) && control.offer_generation == s.state_ticket.offer_generation)
            s.repair_ack_sent = true;
    }
    return true;
}
int HostQuietPrefix() {
    if (s.lifecycle_front || s.lifecycle_count || s.lifecycle_acked != s.lifecycle_published) return 0;
    CE_WorldLifecycleInfo next = {}; uint8_t bytes[CE_WORLD_LIFECYCLE_MAX_BYTES];
    int pending = CE_WorldPeekLifecycle(&next, bytes, sizeof bytes);
    if (pending < 0) { WorldFail("repair lifecycle scope refused"); return -1; }
    return pending == 0 ? 1 : 0;
}
bool DriveRepairHost() {
    if (!RepairLocalReady()) return true;
    if (s.repair_phase == REPAIR_QUIESCE) {
        // Keep the guest's ordinary render/apply path live while settling existing application obligations.
        // No new host Update/STATE is allowed. Acquire the repair WAIT only after this exact prefix is drained.
        if (!RuntimeReady()) return true;
        if (!DriveLifecycle()) return false;
        int quiet = HostQuietPrefix(); if (quiet < 0) return false;
        if (!quiet || s.state_pending) return true;
        s.repair_gate = {0, s.baseline_sequence, s.lifecycle_acked};
        s.repair_phase = REPAIR_WAIT;
        return DriveWait();
    }
    if (s.repair_phase == REPAIR_APPLIED) return true; // normal acknowledged resume retires only Session's gate
    if (!s.shared_wait || s.wait_phase != WAIT_HELD) return true;
    if (!RepairHeldGate()) return Fail("repair lost its frozen WAIT or lifecycle prefix");
    int quiet = HostQuietPrefix(); if (quiet < 0) return false;
    if (!quiet) return Fail("new lifecycle operation appeared inside the repair barrier");
    if (s.repair_phase == REPAIR_WAIT) {
        if (!s.repair_capture_ready) {
            if (!AdmissionCurrent()) return false;
            if (!CE_WorldCapture(s.capture) || !CE_WorldFrameInfo(s.capture, &s.repair_info) ||
                !CE_WorldEncode(s.capture, s.state_raw, sizeof s.state_raw, &s.state_raw_size))
                return WorldFail("fresh repair capture refused");
            if (s.repair_info.epoch != s.epoch || s.repair_info.level != s.level || s.repair_info.tick != s.wait_tick ||
                s.repair_info.lifecycle_baseline != s.repair_gate.lifecycle_baseline ||
                s.repair_info.lifecycle_watermark != s.repair_gate.lifecycle_watermark)
                return Fail("fresh repair capture differs from the held quiet gate");
            s.repair_capture_ready = true;
        }
        const CE_StateImage image = StateImage(s.repair_info);
        CE_StateDeliveryControl announcement = {}; CE_StateDeliveryTicket ticket = {};
        int prepared = CE_StateSenderPrepareRepair(s.state_sender, &image, s.repair_info.tick, &s.repair_gate,
                                                   CE_NetNowMs(), &announcement, &ticket);
        if (prepared == CE_DELIVERY_RETRY) return true; // preserve this exact frame while respecting the repair rate
        if (prepared != CE_DELIVERY_OK) return StateFailure("repair image preparation refused", prepared);
        s.repair_announcement = announcement; s.repair_ticket = ticket; s.repair_phase = REPAIR_PREPARED;
        return DriveStateControls();
    }
    if (s.repair_phase != REPAIR_PREPARED) return true;
    CE_StateImage image = {};
    int available = CE_StateSenderRepairImage(s.state_sender, &s.repair_ticket, &image);
    if (available == CE_DELIVERY_NOT_READY) return true;
    if (available != CE_DELIVERY_OK) return StateFailure("announced repair image is no longer current", available);
    CE_WorldInfo info = {}; uint32_t bytes = 0;
    if (!CE_WorldFrameInfo(s.capture, &info) || !CE_WorldEncode(s.capture, s.wire, sizeof s.wire, &bytes))
        return WorldFail("retained repair frame refused");
    if (info.epoch != s.repair_info.epoch || info.frame_sequence != s.repair_info.frame_sequence || info.tick != s.repair_info.tick ||
        bytes != image.size || memcmp(s.wire, image.data, bytes)) return Fail("repair frame changed across its raw send lease");
    if (!AdmissionCurrent()) return false;
    if (CE_StateSenderValidate(s.state_sender, &s.repair_ticket, &image, info.tick) != CE_DELIVERY_OK)
        return Fail("repair ticket changed before raw send");
    int sent = CE_NetSend(s.net, CE_NET_STATE_REPAIR, info.tick, image.data, image.size);
    if (sent < 0) return Fail("invalid raw repair send");
    uint64_t now = CE_NetNowMs();
    int committed = CE_StateSenderSent(s.state_sender, &s.repair_ticket, &image, info.tick, sent, now);
    if (committed != (sent ? CE_DELIVERY_OK : CE_DELIVERY_RETRY)) return StateFailure("repair raw send ticket refused", committed);
    if (!sent) return true;
    if (CE_WorldFramePublished(s.capture) != 1) return WorldFail("repair world publication refused");
    StatePending(s.repair_ticket.key, s.repair_ticket.offer_generation, true, now);
    s.last_capture_tick = info.tick; s.last_periodic = now; s.repair_phase = REPAIR_SENT;
    return true;
}
bool DriveRepairGuest() {
    if (!RepairLocalReady()) return true;
    if (s.repair_phase == REPAIR_APPLIED) return DriveStateControls();
    if (!RepairHeldGate()) return Fail("replica repair lost its held quiet gate");
    if (!s.repair_installed) {
        int installed = CE_StateReceiverExpectRepair(s.state_receiver, &s.repair_announcement, &s.repair_gate, CE_NetNowMs());
        if (installed != CE_DELIVERY_OK) return StateFailure("repair announcement installation refused", installed);
        s.repair_installed = true; s.repair_phase = REPAIR_PREPARED;
        return DriveStateControls();
    }
    if (!s.have_frame || !s.state_prepared || !s.state_ticket.repair) return DriveStateControls();
    if (!s.repair_ready_sent) return Fail("raw repair reached Apply before queued READY");
    CE_StateImage image = {}; CE_WorldInfo info = {};
    if (CE_StateReceiverImage(s.state_receiver, &s.state_ticket, &image) != CE_DELIVERY_OK || !StateFrameMatches(s.state_ticket, info))
        return Fail("repair Apply lost its exact surviving ticket/header");
    if (info.lifecycle_baseline != s.repair_gate.lifecycle_baseline || info.lifecycle_watermark != s.repair_gate.lifecycle_watermark ||
        info.tick != s.wait_tick || CE_WorldFrameReadiness(s.incoming) != CE_WORLD_READY)
        return Fail("repair is not READY at its exact frozen prefix");
    if (!AdmissionCurrent()) return false;
    if (!CE_WorldApply(s.incoming)) return WorldFail("repair whole Apply refused");
    if (!AdmissionCurrent()) return false;
    int applied = CE_StateReceiverApplied(s.state_receiver, &s.state_ticket, info.epoch, info.frame_sequence, info.tick, CE_NetNowMs());
    if (applied != CE_DELIVERY_OK) return StateFailure("repair whole-Apply proof refused", applied);
    s.have_frame = s.state_prepared = false; s.repair_phase = REPAIR_APPLIED;
    return DriveStateControls();
}
void CancelAvailability() {
    s.guest_request_pending = s.guest_request_queued = s.peer_guest_ack_pending = false;
    s.guest_request_since = s.peer_guest_ack_since = 0;
}
bool AvailabilityControl(unsigned kind, uint32_t generation, uint32_t tick, bool terminal) {
    // The guest can queue BLOCK immediately after START_APPLIED. Both reliable controls may reach this host
    // in one receive pass, before PreparePlay assigns RUNNING. Only retain metadata at that proven handoff.
    bool start_handoff = s.host && s.stage == BASELINED && s.world_started && s.baseline_applied &&
                         s.start_queued && s.start_applied;
    if ((s.stage != RUNNING && !start_handoff) || !generation || !tick) return Fail("guest availability outside a running session");
    bool blocked = kind == GUEST_BLOCK || kind == GUEST_BLOCK_ACK;
    if (s.host) {
        if (kind != GUEST_BLOCK && kind != GUEST_READY) return Fail("guest availability has the wrong peer role");
        if (tick > CE_SessionGameTick() || tick < s.peer_guest_tick) return Fail("guest availability names an invalid source tick");
        if (generation < s.peer_guest_generation) return Fail("stale guest availability generation");
        if (generation == s.peer_guest_generation) {
            if (tick != s.peer_guest_tick || blocked != s.peer_guest_blocked) return Fail("guest availability changed its retained tuple");
        } else {
            if (s.peer_guest_generation == UINT32_MAX || generation != s.peer_guest_generation + 1 ||
                blocked == s.peer_guest_blocked || s.peer_guest_ack_pending)
                return Fail("gapped or unacknowledged guest availability transition");
            if (!blocked && (!s.shared_wait || s.wait_phase != WAIT_HELD))
                return Fail("guest readiness preceded the held shared wait");
            s.peer_guest_generation = generation; s.peer_guest_tick = tick; s.peer_guest_blocked = blocked;
        }
        if (!terminal) {
            if (!s.peer_guest_ack_pending) s.peer_guest_ack_since = CE_NetNowMs();
            s.peer_guest_ack_pending = true;
        }
        return true;
    }
    if (kind != GUEST_BLOCK_ACK && kind != GUEST_READY_ACK) return Fail("guest availability receipt has the wrong peer role");
    if (generation != s.guest_generation || tick != s.guest_request_tick || blocked != s.guest_request_blocked ||
        (s.guest_request_pending && !s.guest_request_queued))
        return Fail("guest availability receipt has no exact queued request");
    if (!terminal) {
        s.guest_reported_blocked = blocked; s.guest_request_pending = s.guest_request_queued = false;
        s.guest_request_since = 0;
    }
    return true;
}
bool DriveAvailability() {
    if (s.stage != RUNNING) return true;
    uint64_t now = CE_NetNowMs();
    if (s.host) {
        if (!s.peer_guest_ack_pending) return true;
        if (now < s.peer_guest_ack_since || now - s.peer_guest_ack_since >= kWaitMs)
            return Fail("guest availability receipt timed out");
        // A BLOCK receipt proves this wait is held. During RESUME, the previous held generation is insufficient:
        // queue the new WAIT and receive its ACK before permitting a rapidly recovered guest to send READY.
        if (s.peer_guest_blocked && (!s.shared_wait || s.wait_phase != WAIT_HELD)) return true;
        int sent = SendControl(s.peer_guest_blocked ? GUEST_BLOCK_ACK : GUEST_READY_ACK,
                               s.peer_guest_generation, s.peer_guest_tick);
        if (sent < 0) return Fail("invalid guest availability receipt send");
        if (sent == 1) { s.peer_guest_ack_pending = false; s.peer_guest_ack_since = 0; }
        return true;
    }
    LatchGuestAvailability();
    if (!s.guest_request_pending && s.guest_desired_blocked != s.guest_reported_blocked) {
        // A brief recovery never replaces an unsent BLOCK or bypasses the host's wait barrier.
        if (!s.guest_desired_blocked && (!s.shared_wait || s.wait_ack_pending)) return true;
        if (s.guest_generation == UINT32_MAX) return Fail("guest availability generation exhausted");
        uint32_t tick = CE_SessionGameTick();
        if (!tick) return Fail("guest availability has no admitted source tick");
        ++s.guest_generation; s.guest_request_tick = tick; s.guest_request_blocked = s.guest_desired_blocked;
        s.guest_request_pending = true; s.guest_request_queued = false; s.guest_request_since = now;
    }
    if (!s.guest_request_pending) return true;
    if (now < s.guest_request_since || now - s.guest_request_since >= kWaitMs)
        return Fail("guest availability acknowledgement timed out");
    if (!s.guest_request_queued) {
        int sent = SendControl(s.guest_request_blocked ? GUEST_BLOCK : GUEST_READY,
                               s.guest_generation, s.guest_request_tick);
        if (sent < 0) return Fail("invalid guest availability request send");
        if (sent == 1) s.guest_request_queued = true;
    }
    return true;
}
bool SetSharedWait(bool on) {
    if (CE_WorldSetInputSuspended(on ? CE_WORLD_INPUT_HOST_WAIT : 0u) != 1)
        return WorldFail("shared wait input gate refused");
    s.shared_wait = on;
    return true;
}
void RebaseAfterWait() {
    CE_WorldRebaseLocalSampler(); CE_SessionGameRebaseRenderClock();
    s.last_periodic = CE_NetNowMs();
}
bool WaitControl(unsigned kind, uint32_t generation, uint32_t tick, bool terminal) {
    if (s.stage != RUNNING || !generation || !tick) return Fail("shared wait control outside a running session");
    if (s.host ? (kind != HOST_WAIT_ACK && kind != HOST_RESUME_ACK) : (kind != HOST_WAIT && kind != HOST_RESUME))
        return Fail("shared wait control has the wrong peer role");
    if (generation == s.wait_generation && tick != s.wait_tick) return Fail("shared wait generation changed its frozen tick");
    if (generation <= s.completed_wait || generation < s.wait_generation) return true;
    if (s.host) {
        if (generation != s.wait_generation || tick != s.wait_tick) return Fail("shared wait acknowledgement has no matching generation");
        if (kind == HOST_WAIT_ACK && (s.wait_phase == WAIT_ACK || s.wait_phase == WAIT_HELD)) {
            if (!terminal && s.repair_phase == REPAIR_WAIT) {
                if (s.repair_gate.wait_generation && s.repair_gate.wait_generation != generation)
                    return Fail("repair acknowledgement changed its frozen WAIT generation");
                // Only this actually received exact ACK establishes the repair's shared-WAIT generation.
                // The native shell must be able to check the whole retained gate before routing to Render.
                s.repair_gate.wait_generation = generation;
            }
            s.wait_phase = WAIT_HELD; s.wait_deadline = 0; return true;
        }
        if (kind == HOST_WAIT_ACK && (s.wait_phase == RESUME_SEND || s.wait_phase == RESUME_ACK)) return true;
        if (kind == HOST_RESUME_ACK && s.wait_phase == RESUME_ACK) {
            if (terminal) {
                s.completed_wait = generation; s.wait_phase = WAIT_NONE; s.shared_wait = false;
                return true;
            }
            if (RepairHold()) return true;
            if (HostUnavailable()) return true; // a later admitted BLOCK cannot release this wait
            // Readiness is checked again by the shell before releasing the native model loop.
            s.completed_wait = generation; s.wait_phase = WAIT_NONE;
            if (!SetSharedWait(false)) return false;
            RebaseAfterWait();
            if (s.repair_phase == REPAIR_APPLIED) ClearRepair();
            return true;
        }
        return Fail("unexpected shared wait acknowledgement");
    }
    if (kind == HOST_WAIT) {
        if (generation < s.wait_generation) return true;
        if (generation == s.wait_generation) {
            if (tick != s.wait_tick) return Fail("shared wait generation changed its frozen tick");
            return true; // the original ACK is retained/reliable; a duplicate cannot rewind a resume
        }
        if (generation != s.wait_generation + 1) return Fail("shared wait generation gap");
        if (s.repair_phase != REPAIR_OFF) {
            if (s.repair_phase == REPAIR_APPLIED && s.repair_ack_sent) {
                if (!terminal) ClearRepair();
            } else return Fail("shared wait generation changed during repair");
        }
        s.wait_generation = generation; s.wait_tick = tick;
        s.wait_ack_pending = true; s.resume_ack_pending = false;
        if (terminal) { s.wait_deadline = 0; s.shared_wait = true; return true; }
        s.wait_deadline = CE_NetNowMs();
        return SetSharedWait(true);
    }
    if (kind == HOST_RESUME && generation == s.wait_generation && tick == s.wait_tick &&
        s.shared_wait && !s.wait_ack_pending) {
        if (s.repair_phase != REPAIR_OFF && (s.repair_phase != REPAIR_APPLIED || !s.repair_ack_sent))
            return Fail("repair resume preceded whole-Apply acknowledgement");
        if (terminal) { s.resume_ack_pending = true; s.wait_deadline = 0; return true; }
        if (!s.resume_ack_pending) { s.resume_ack_pending = true; s.wait_deadline = CE_NetNowMs(); }
        return true;
    }
    return Fail("unexpected host shared wait transition");
}
bool DriveWait() {
    if (s.stage != RUNNING) return true;
    uint64_t now = CE_NetNowMs();
    if (s.wait_deadline && (now < s.wait_deadline || now - s.wait_deadline >= kWaitMs))
        return Fail("shared wait acknowledgement timed out");
    if (s.host) {
        if (HostNeedsWait() && (!s.shared_wait || s.wait_phase == RESUME_SEND || s.wait_phase == RESUME_ACK)) {
            if (s.wait_generation == UINT32_MAX) return Fail("shared wait generation exhausted");
            ++s.wait_generation; s.wait_tick = CE_SessionGameTick(); s.wait_phase = WAIT_SEND;
            s.wait_deadline = now;
            if (!SetSharedWait(true)) return false;
        }
        if (s.wait_phase == WAIT_HELD && !HostNeedsWait() && !s.peer_guest_ack_pending) { s.wait_phase = RESUME_SEND; s.wait_deadline = now; }
        unsigned kind = s.wait_phase == WAIT_SEND ? HOST_WAIT : s.wait_phase == RESUME_SEND ? HOST_RESUME : 0;
        if (kind) {
            int sent = SendControl(kind, s.wait_generation, s.wait_tick);
            if (sent < 0) return Fail("invalid shared wait control send");
            if (sent) s.wait_phase = kind == HOST_WAIT ? WAIT_ACK : RESUME_ACK;
        }
    } else {
        unsigned kind = s.wait_ack_pending ? HOST_WAIT_ACK : s.resume_ack_pending ? HOST_RESUME_ACK : 0;
        if (kind == HOST_RESUME_ACK && !GuestMayResume()) return true;
        if (kind) {
            int sent = SendControl(kind, s.wait_generation, s.wait_tick);
            if (sent < 0) return Fail("invalid shared wait acknowledgement send");
            if (sent) {
                s.wait_deadline = 0;
                if (kind == HOST_WAIT_ACK) {
                    s.wait_ack_pending = false;
                    // A live, actually queued ACK authorizes only its already committed quiet prefix. Keep this
                    // proof through terminal queue draining; terminal metadata cannot invent a new WAIT barrier.
                    if (!s.lifecycle_in_count && s.lifecycle_received == s.lifecycle_ack.operation) {
                        s.authorized_repair_gate = {s.wait_generation, s.baseline_sequence, s.lifecycle_ack.operation};
                        s.authorized_repair_tick = s.wait_tick;
                    }
                }
                else {
                    s.resume_ack_pending = false; s.completed_wait = s.wait_generation;
                    if (!SetSharedWait(false)) return false;
                    RebaseAfterWait();
                    if (s.repair_phase == REPAIR_APPLIED) ClearRepair();
                }
            }
        }
    }
    return true;
}
bool HandleControl(const CE_NetMessage &message, const uint8_t *data, bool terminal) {
    CE_NetReader r; CE_NetReaderInit(&r, data, message.size);
    uint32_t magic = CE_NetReadU32(&r), schema = CE_NetReadU32(&r), kind = CE_NetReadU32(&r);
    uint32_t level = CE_NetReadU32(&r), sequence = CE_NetReadU32(&r), tick = CE_NetReadU32(&r);
    uint32_t baseline = kind == LIFECYCLE_APPLIED ? CE_NetReadU32(&r) : 0;
    if (!CE_NetReadDone(&r) || magic != kControlMagic || schema != kControlSchema || level != s.level || tick != message.tick)
        return Fail("malformed session control");
    if (kind == LIFECYCLE_APPLIED) return LifecycleReceipt(message, sequence, tick, baseline, terminal);
    if (kind == LEAVING) {
        CE_NetStatus status; CE_NetGetStatus(s.net, &status);
        if (sequence || tick || !message.epoch || message.epoch != status.epoch ||
            s.stage < LOADING || s.stage > RUNNING) return Fail("invalid peer departure control");
        s.peer_leaving = true;
        return true;
    }
    if (kind == BIRTHS_APPLIED) {
        if (!s.host || (s.stage != BASELINED && s.stage != RUNNING) || message.epoch != s.epoch)
            return Fail("birth acknowledgement outside the admitted host epoch");
        return CE_WorldAcknowledgeBirths(message.epoch, sequence, tick) == 1 || WorldFail("birth acknowledgement refused");
    }
    if (kind >= GUEST_BLOCK && kind <= GUEST_READY_ACK) {
        if (message.epoch != s.epoch) return Fail("guest availability belongs to another epoch");
        return AvailabilityControl(kind, sequence, tick, terminal);
    }
    if (kind >= HOST_WAIT && kind <= HOST_RESUME_ACK) {
        if (message.epoch != s.epoch) return Fail("shared wait control belongs to another epoch");
        return WaitControl(kind, sequence, tick, terminal);
    }
    if (kind == PEER_LOADED && !sequence && !tick && (s.stage == LOADING || s.stage == LOADED || s.stage == PREPARING)) {
        if (message.epoch == 1) { if (!terminal) s.peer_loaded = true; }
        else if (message.epoch == 2) { if (!terminal) s.peer_epoch_loaded = true; }
        else return Fail("loaded acknowledgement belongs to another epoch");
        return true;
    }
    if (kind == BASELINE_APPLIED && s.host && s.stage == BASELINED && message.epoch == s.epoch &&
        sequence == s.baseline_sequence && tick == s.baseline_tick) { if (!terminal) s.baseline_applied = true; return true; }
    if (kind == START && !s.host && s.stage == BASELINED && message.epoch == s.epoch &&
        sequence == s.baseline_sequence && tick == s.baseline_tick) { if (!terminal) s.start_seen = true; return true; }
    if (kind == START_APPLIED && s.host && s.stage == BASELINED && message.epoch == s.epoch &&
        sequence == s.baseline_sequence && tick == s.baseline_tick) {
        // Also retain receipt ordering while validating a frozen terminal queue. Poll retires that queue before
        // driving controls or returning to any ready predicate; this flag cannot admit play after departure.
        s.start_applied = true; return true;
    }
    return Fail("unexpected session control transition");
}
bool DrainRemote() {
    if (!RuntimeReady()) return true;
    while (s.remote_count) {
        int result = CE_WorldQueueRemoteInput(&s.remote[s.remote_first]);
        if (result < 0) return WorldFail("remote input refused");
        if (!result) return true;
        s.remote_first = (s.remote_first + 1) % 32; --s.remote_count;
    }
    return true;
}
bool Receive(bool terminal) {
    if (!terminal && !s.peer_leaving && !DrainRemote()) return false;
    // Ordinary service stays at 32. A terminal queue is frozen and bounded to 64 completed messages/256 KiB.
    for (unsigned budget = 0; budget < ((terminal || s.peer_leaving) ? 64u : 32u); ++budget) {
        CE_NetMessage message = {}; int got = CE_NetReceive(s.net, &message, s.wire, sizeof s.wire);
        if (got < 0) return Fail("received message exceeds the session capacity");
        if (!got) break;
        CE_NetStatus status; if (!Status(status)) return false;
        if (message.epoch != status.epoch) return Fail("received application message from a stale epoch");
        if (message.kind == CE_NET_CONTROL) {
            if (!HandleControl(message, s.wire, terminal || s.peer_leaving)) return false;
        } else if (message.kind == CE_NET_STATE_CONTROL) {
            if (!HandleStateControl(message, s.wire, terminal || s.peer_leaving)) return false;
        } else if ((terminal || s.peer_leaving) && (message.kind == CE_NET_INPUT ||
                   message.kind == CE_NET_STATE || message.kind == CE_NET_BASELINE || message.kind == CE_NET_LIFECYCLE ||
                   message.kind == CE_NET_STATE_REPAIR)) continue;
        else if (message.kind == CE_NET_INPUT) {
            if (s.remote_count == 32) return Fail("deferred input FIFO capacity exhausted");
            if (!s.host || (s.stage != RUNNING && !(s.stage == BASELINED && s.start_queued)) ||
                !CE_WorldDecodeInput(&s.remote[(s.remote_first + s.remote_count) % 32], s.wire, message.size))
                return Fail("input arrived outside the admitted host simulation");
            CE_WorldInput &input = s.remote[(s.remote_first + s.remote_count) % 32];
            if (input.epoch != message.epoch || input.tick != message.tick) return Fail("input envelope differs from its payload");
            ++s.remote_count;
            if (!DrainRemote()) return false;
        } else if (message.kind == CE_NET_LIFECYCLE) {
            if (s.host || s.stage != RUNNING || !s.lifecycle_started || s.lifecycle_in_count == kLifecycleWindow ||
                message.size > CE_WORLD_LIFECYCLE_MAX_BYTES)
                return Fail("lifecycle arrived outside its bounded replica barrier");
            Session::LifecyclePacket &packet = s.lifecycle_incoming[(s.lifecycle_in_first + s.lifecycle_in_count) % kLifecycleWindow];
            if (CE_WorldInspectLifecycle(s.wire, message.size, &packet.info) != 1)
                return WorldFail("lifecycle decode refused");
            if (packet.info.epoch != message.epoch || packet.info.source_tick != message.tick ||
                packet.info.baseline_sequence != s.baseline_sequence || packet.info.size != message.size ||
                s.lifecycle_received == UINT32_MAX || packet.info.operation != s.lifecycle_received + 1)
                return Fail("lifecycle envelope or ordered prefix differs from its payload");
            if (s.repair_phase >= REPAIR_WAIT && packet.info.operation > s.repair_gate.lifecycle_watermark)
                return Fail("lifecycle operation crossed a frozen repair watermark");
            memcpy(packet.bytes, s.wire, message.size); ++s.lifecycle_in_count;
            s.lifecycle_received = packet.info.operation;
        } else if (message.kind == CE_NET_STATE || message.kind == CE_NET_STATE_REPAIR) {
            if (!ReceiveStateImage(message, s.wire, message.kind == CE_NET_STATE_REPAIR)) return false;
        } else if (message.kind == CE_NET_BASELINE) {
            if (s.host || !s.world_started || (s.stage != PREPARING && s.stage != LOADED))
                return Fail("world state arrived outside its session barrier");
            if (!CE_WorldDecode(s.incoming, s.wire, message.size)) return WorldFail("world decode refused");
            CE_WorldInfo info = {}; if (!CE_WorldFrameInfo(s.incoming, &info)) return WorldFail("world header unavailable");
            if (info.epoch != message.epoch || info.tick != message.tick || info.level != s.level) return Fail("world envelope differs from its payload");
            if (s.have_baseline || s.baseline_applied) return Fail("duplicate initial baseline");
            memcpy(s.state_raw, s.wire, message.size); s.state_raw_size = message.size;
            s.baseline_sequence = info.frame_sequence; s.baseline_tick = info.tick;
            s.have_baseline = true; // a fast host may send this while the guest is finishing its load barrier
        } else return Fail("unexpected application message kind");
    }
    return true;
}
bool PeerDeparture() {
    if (exit_outcome == EXIT_ERROR || s.stage == FAILED) return false;
    exit_outcome = EXIT_GRACEFUL;
    if (!CE_SessionGameQuitCode()) CE_SessionGameSetQuit(QUIT_FRONTEND);
    s.stage = DRAINING;
    s.have_frame = s.have_baseline = s.have_input = false;
    s.remote_first = s.remote_count = 0;
    s.birth_ack = {}; s.birth_ack_sent = 0;
    CancelAvailability();
    CE_NetClose(s.net);
    fprintf(stderr, "onslaught-ce: peer deliberately left the engineering session\n");
    return false;
}
// During play the other player's silence is announced at once, then every five seconds of the countdown.
void QuietNotice() {
    const uint32_t quiet = s.stage == RUNNING ? CE_NetQuietMs(s.net, CE_NetNowMs()) : 0;
    if (quiet < kQuietNoticeMs || quiet >= kWaitMs) {
        if (s.quiet_left) fprintf(stderr, "onslaught-ce: Connection restored\n");
        s.quiet_left = 0; s.notice[0] = 0; return;
    }
    const uint32_t left = uint32_t((kWaitMs - quiet + 999) / 1000);
    snprintf(s.notice, sizeof s.notice, "Connection lost, waiting... (%u s)", left);
    if (!s.quiet_left || (left % 5 == 0 && left != s.quiet_left)) fprintf(stderr, "onslaught-ce: %s\n", s.notice);
    s.quiet_left = left;
}
bool Poll() {
    if (!AdmissionCurrent()) return false;
    LatchGuestAvailability();
    CE_NetPoll(s.net, CE_NetNowMs(), 32);
    CE_NetStatus status;
    if (!Status(status)) return false;
    QuietNotice();
    bool terminal = status.phase == CE_NET_CLOSED && status.error == CE_NET_PEER_LEFT;
    bool expired = LifecycleExpired(CE_NetNowMs()) || StateExpired(CE_NetNowMs());
    // Validate the frozen control queue before classifying a deadline. Late valid proof cannot release credit,
    // and malformed/foreign proof cannot hide behind timeout or departure. No application runs in this drain.
    if (!Receive(terminal || expired)) return false;
    if (expired) return Fail("state or lifecycle publication/application transaction timed out");
    if (terminal || s.peer_leaving) return PeerDeparture();
    if (!DriveAvailability()) return false;
    return DriveWait() && DriveBirthAck() && DriveLifecycleAck() && DriveStateControls();
}
template<class Predicate> bool Wait(Predicate ready, uint64_t limit = kWaitMs) {
    uint64_t start = CE_NetNowMs();
    for (;;) {
        // The native owner may already have closed while a final model/render callback is still unwinding.
        // Cancellation is not a peer admission failure, and no ready predicate may send/apply after that close.
        if (CE_SessionGameQuitCode() || s.stage == FAILED || s.stage == DRAINING) return false;
        if (!AdmissionCurrent()) return false;
        if (ready()) return true;
        if (s.stage == FAILED || s.stage == DRAINING) return false;
        int closed = CE_SessionGamePump();
        if (CE_SessionGameQuitCode()) return false;
        if (closed) return Fail("window pump stopped without a native quit code");
        if (!Poll()) return false;
        uint64_t now = CE_NetNowMs();
        if (now < start || now - start >= limit) return Fail("session load or start barrier timed out");
        CE_SessionGameIdle();
    }
}
bool ControlUntilQueued(unsigned kind, uint32_t sequence = 0, uint32_t tick = 0) {
    return Wait([=] {
        int result = SendControl(kind, sequence, tick);
        if (result == 1) { if (kind == START) s.start_queued = true; return true; }
        if (result < 0) Fail("invalid control send");
        return false;
    });
}
bool StateBudget(uint32_t bytes, uint64_t now) {
    if (s.state_credit_at && now > s.state_credit_at)
        s.state_credit = std::min<uint64_t>(kStateBurstBytes * 1000, s.state_credit + (now - s.state_credit_at) * kStateBytesPerSecond);
    s.state_credit_at = now;
    return uint64_t(bytes) * 1000 <= s.state_credit;
}
bool Capture(unsigned kind) {
    if (!AdmissionCurrent()) return false;
    if (!CE_WorldCapture(s.capture)) return WorldFail("host capture refused");
    CE_WorldInfo info = {}; uint32_t bytes = 0;
    if (!CE_WorldFrameInfo(s.capture, &info) || !CE_WorldEncode(s.capture, s.state_raw, sizeof s.state_raw, &bytes)) return WorldFail("host encode refused");
    s.state_raw_size = bytes; const CE_StateImage image = StateImage(info);
    CE_StateDeliveryTicket ticket = {};
    const uint8_t *payload = s.state_raw;
    if (kind == CE_NET_STATE) {
        int prepared = CE_StateSenderPrepare(s.state_sender, &image, info.tick, CE_NetNowMs(), s.wire, sizeof s.wire, &bytes, &ticket);
        if (prepared == CE_DELIVERY_REPAIR_REQUIRED) { RequestRepair(); return true; }
        if (prepared == CE_DELIVERY_STALE) return true;
        if (prepared != CE_DELIVERY_OK) return StateFailure("state delta preparation refused", prepared);
        payload = s.wire;
    }
    if (!AdmissionCurrent()) return false;
    if (kind == CE_NET_STATE && CE_StateSenderValidate(s.state_sender, &ticket, &image, info.tick) != CE_DELIVERY_OK)
        return Fail("state delta ticket changed before send");
    int sent = kind == CE_NET_STATE && !StateBudget(bytes, CE_NetNowMs()) ? 0 : CE_NetSend(s.net, kind, info.tick, payload, bytes);
    if (kind == CE_NET_STATE && sent == 1) s.state_credit -= uint64_t(bytes) * 1000;
    uint64_t queued_at = CE_NetNowMs();
    if (sent < 0) return Fail("invalid world send");
    if (kind == CE_NET_STATE) {
        int committed = CE_StateSenderSent(s.state_sender, &ticket, &image, info.tick, sent, queued_at);
        if (committed != (sent ? CE_DELIVERY_OK : CE_DELIVERY_RETRY)) return StateFailure("state delta send ticket refused", committed);
        if (sent && ticket.offer_generation) StatePending(ticket.key, ticket.offer_generation, false, queued_at);
    }
    // Publication acknowledges exactly this immutable frame's accepted transport enqueue, never Capture/Encode.
    if (sent == 1 && CE_WorldFramePublished(s.capture) != 1) return WorldFail("world publication refused");
    if (kind == CE_NET_BASELINE) {
        if (!sent) return Fail("baseline queue is full");
        s.baseline_sequence = info.frame_sequence; s.baseline_tick = info.tick;
        int seeded = CE_StateSenderBaselineSent(s.state_sender, &image, info.tick, sent, queued_at);
        if (seeded != CE_DELIVERY_OK) return StateFailure("baseline delivery seed refused", seeded);
        CE_StateKey key = {};
        if (CE_StateDeliveryKey(&image, info.tick, &key) != CE_DELIVERY_OK) return Fail("baseline delivery key refused");
        StatePending(key, 0, false, queued_at);
    }
    s.last_capture_tick = info.tick; s.last_periodic = CE_NetNowMs(); return true;
}
void RuntimeFailure() {
    if (!CE_SessionGameQuitCode()) CE_SessionGameSetQuit(QUIT_USER_FRONTEND);
}
}

extern "C" {
int CE_SessionConfigureLevel(uint32_t level) {
    if (CE_LockstepRequested()) return CE_LockstepConfigureLevel(level);
    if (exit_outcome == EXIT_GRACEFUL) exit_outcome = EXIT_NONE;
    if (s.requested || s.net || s.world_started) return Fail("previous network world has not been torn down");
    error_text[0] = 0;
    bool host = CE_NETWORK_ENABLED && EnvPresent("BEA_NET_HOST"), guest = CE_NETWORK_ENABLED && EnvPresent("BEA_NET_JOIN");
    if (!host && !guest) {
        if (CE_BootRequested()) { s.requested = true; return Fail("network boot intent changed before level admission"); }
        exit_outcome = EXIT_NONE; return 1;
    }
    s.requested = true; s.host = host; s.level = level; s.stage = LOADING;
    if (host == guest || level != kLevel) return Fail("engineering networking requires exactly one host/join endpoint and level 852");
    const char *disabled[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_RECORD", "BEA_PORT_PROBES", "BEA_RECORD", "BEA_PORT_UPDATE_DUMP"};
    for (const char *name : disabled) if (EnvPresent(name)) return Fail("engineering networking is unavailable during replay, recording or clock/probe diagnostics");
    if (PortClockVirtual()) return Fail("engineering networking requires the ordinary clock");
    CE_NetConfig config = {}; char ip[16];
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    const int native_secret = CE_LobbySignalingGameSecret(host ? CE_NET_HOST : CE_NET_GUEST, level, config.secret);
    if (native_secret < 0) return Fail("Native relay game authority is unavailable; leave and choose a new invitation.");
    if (!Endpoint(getenv(host ? "BEA_NET_HOST" : "BEA_NET_JOIN"), ip, config.port) ||
        !(native_secret > 0 || (native_secret == 0 && Hex(getenv("BEA_NET_SECRET"), config.secret))))
#else
    if (!Endpoint(getenv(host ? "BEA_NET_HOST" : "BEA_NET_JOIN"), ip, config.port) ||
        !Hex(getenv("BEA_NET_SECRET"), config.secret))
#endif
#else
    if (!Endpoint(getenv(host ? "BEA_NET_HOST" : "BEA_NET_JOIN"), ip, config.port) ||
        !Hex(getenv("BEA_NET_SECRET"), config.secret))
#endif
        return Fail("network endpoint or private invite is missing or invalid");
    CE_CompatibilityResult admission = {}; char admission_error[160] = {};
    if (CE_BootPrepared(&admission, admission_error, sizeof admission_error) != 1) {
        char text[192]; snprintf(text, sizeof text, "network content admission unavailable: %.140s", admission_error); return Fail(text);
    }
    memcpy(config.compatibility, admission.digest, sizeof config.compatibility);
    memcpy(s.state_context, admission.digest, sizeof s.state_context);
    config.role = host ? CE_NET_HOST : CE_NET_GUEST; config.ipv4 = ip;
    config.handshake_ms = config.silence_ms = (uint32_t)kWaitMs;
    snprintf(config.version, sizeof config.version, "%s", kVersionLabel);
    s.net = CE_NetCreateGame(&config);
    volatile uint8_t *secret = config.secret;
    for (unsigned i = 0; i < sizeof config.secret; ++i) secret[i] = 0;
    if (!s.net) return Fail(CE_NetGameProblem(NULL)[0] ? CE_NetGameProblem(NULL) : "network transport could not be created");
    s.capture = CE_WorldFrameCreate(); s.incoming = CE_WorldFrameCreate();
    if (!s.capture || !s.incoming) return Fail("network frame allocation failed");
    // Local role admission precedes world loading and the active replica fence. Keep this latched through
    // failure and native world destruction; only final session retirement may release mission execution.
    s.mission_vm_blocked = !s.host;
    exit_outcome = EXIT_NONE;
    fprintf(stderr, "onslaught-ce: explicit level-852 network engineering session (%s); not campaign co-op\n", host ? "host" : "guest");
    return 1;
}
int CE_SessionAfterLoad() {
    if (CE_LockstepRequested()) return CE_LockstepAfterLoad();
    if (!s.requested) return 1;
    if (CE_SessionGameQuitCode()) return 0;
    if (s.stage != LOADING) return Fail("network level loaded outside its admission stage");
    if (!Wait([] { CE_NetStatus status; return Status(status) && status.phase == CE_NET_ACTIVE; },
              CE_NetGameIceRequested() ? 180000 : kWaitMs)) return 0;
    s.stage = LOADED;
    if (!ControlUntilQueued(PEER_LOADED)) return 0;
    // The host needs the guest's first LOADED before advancing the epoch. The guest must not wait for an epoch-1
    // host LOADED: the authenticated epoch transition is allowed to clear that queued old-epoch message. Both still
    // require the explicit epoch-2 LOADED below, after each genuine world has successfully begun.
    if (s.host) {
        if (!Wait([] { return s.peer_loaded; })) return 0;
        if (!CE_NetBeginEpoch(s.net, 2)) return Fail("initial world epoch refused");
    }
    if (!Wait([] { CE_NetStatus status; return Status(status) && status.phase == CE_NET_ACTIVE && status.epoch == 2; })) return 0;
    s.epoch = 2;
    if (!AdmissionBoundary()) return 0;
    CE_WorldConfig config = {s.host ? CE_WORLD_HOST : CE_WORLD_REPLICA, s.epoch, s.host ? 0u : 1u, s.level};
    if (!CE_WorldBegin(&config)) return WorldFail("loaded world admission refused");
    s.world_started = true;
    if (s.host) s.state_sender = CE_StateSenderCreate(s.state_context, s.epoch);
    else s.state_receiver = CE_StateReceiverCreate(s.state_context, s.epoch);
    if (s.host ? !s.state_sender : !s.state_receiver) return Fail("state delivery allocation failed");
    if (!ControlUntilQueued(PEER_LOADED) || !Wait([] { return s.peer_epoch_loaded; })) return 0;
    s.stage = PREPARING; return 1;
}
int CE_SessionPreparePlay() {
    if (CE_LockstepRequested()) return CE_LockstepPreparePlay();
    if (!s.requested) { CE_SessionGamePreRun(); return 1; }
    if (CE_SessionGameQuitCode()) return 0;
    if (s.stage != PREPARING) return Fail("network pre-run entered before both players loaded");
    if (TerminalTransport() && !Poll()) return 0;
    if (!AdmissionBoundary()) return 0;
    if (s.host) {
        CE_SessionGamePreRun();
        if (s.stage == FAILED || CE_SessionGameQuitCode()) return 0;
        if (!Capture(CE_NET_BASELINE)) return 0;
        s.stage = BASELINED;
        if (!Wait([] {
                CE_StateKey basis = {};
                return s.baseline_applied && CE_StateSenderBasis(s.state_sender, &basis) == CE_DELIVERY_OK &&
                       basis.epoch == s.epoch && basis.sequence == s.baseline_sequence && basis.tick == s.baseline_tick;
            }) || !StartLifecycle(s.capture) ||
            !ControlUntilQueued(START, s.baseline_sequence, s.baseline_tick) ||
            !Wait([] { return s.start_applied; })) return 0;
    } else {
        if (!Wait([] {
                if (s.have_baseline) {
                    if (!CE_WorldApply(s.incoming)) return WorldFail("baseline refused");
                    if (!AdmissionCurrent()) return false;
                    CE_WorldInfo info = {};
                    if (!CE_WorldFrameInfo(s.incoming, &info)) return WorldFail("applied baseline header unavailable");
                    const CE_StateImage image = StateImage(info);
                    int seeded = CE_StateReceiverBaselineApplied(s.state_receiver, &image, info.tick, CE_NetNowMs());
                    if (seeded != CE_DELIVERY_OK) return StateFailure("applied baseline delivery seed refused", seeded);
                    s.have_baseline = false; s.baseline_applied = true; s.stage = BASELINED;
                }
                return s.baseline_applied;
            }) || !ControlUntilQueued(BASELINE_APPLIED, s.baseline_sequence, s.baseline_tick) ||
            !Wait([] { return s.start_seen; }) || !StartLifecycle(s.incoming) ||
            !ControlUntilQueued(START_APPLIED, s.baseline_sequence, s.baseline_tick)) return 0;
    }
    s.stage = RUNNING; s.last_periodic = CE_NetNowMs(); CE_SessionGameRebaseRenderClock();
    fprintf(stderr, "onslaught-ce: connected, the network session has started (you are player %d)\n", s.host ? 1 : 2);
    return 1;
}
#ifdef CE_NET_TEST_AUTOMATION
void CE_WorldInventoryLog();
#endif
int CE_SessionBeforeUpdate() {
#ifdef CE_NET_TEST_AUTOMATION
    CE_WorldInventoryLog();
#endif
    if (CE_LockstepRequested()) return CE_LockstepBeforeUpdate();
    if (!s.requested) return 1;
    if (CE_SessionGameQuitCode()) return 0;
    if (s.stage == FAILED || s.stage == DRAINING || !Poll()) { RuntimeFailure(); return 0; }
    if (s.repair_phase != REPAIR_OFF) return 0;
    if (s.stage == RUNNING && RuntimeReady()) {
        if (!DriveLifecycle()) { RuntimeFailure(); return 0; }
        return !s.lifecycle_blocked;
    }
    return s.host && s.stage == PREPARING;
}
void CE_SessionAfterFlush() {
    if (!s.requested || !s.host || s.stage != RUNNING || s.repair_phase != REPAIR_OFF || !RuntimeReady() || CE_SessionGameQuitCode()) return;
    if (TerminalTransport()) {
        if (!Poll()) RuntimeFailure();
        return;
    }
    if (!DriveLifecycle()) { RuntimeFailure(); return; }
    if (s.lifecycle_blocked) return;
    uint32_t tick = CE_SessionGameTick();
    if (tick != s.last_capture_tick && !Capture(CE_NET_STATE)) RuntimeFailure();
}
void CE_SessionAfterUpdate() { if (CE_LockstepRequested()) CE_LockstepAfterUpdate(); }
void CE_SessionBeforeRender() {
    if (CE_LockstepRequested()) { CE_LockstepBeforeRender(); return; }
    if (!s.requested || s.stage != RUNNING || CE_SessionGameQuitCode()) return;
    if (!Poll()) { RuntimeFailure(); return; }
    if (s.repair_phase != REPAIR_OFF) {
        if (!(s.host ? DriveRepairHost() : DriveRepairGuest())) RuntimeFailure();
        return;
    }
    if (!RuntimeReady()) return;
    if (!s.host) {
        // Validate deferred resources before any irreversible lifecycle Init. Recheck only if the committed
        // lifecycle prefix changes: a previously valid full state may then be stale, ready, or still deferred.
        uint32_t prefix = s.lifecycle_ack.operation;
        int ready = s.have_frame ? ReplicaReadiness() : CE_WORLD_DEFER;
        if (ready == CE_WORLD_INVALID) { RuntimeFailure(); return; }
        if (!ApplyLifecycle()) { RuntimeFailure(); return; }
        if (s.have_frame) {
            if (prefix != s.lifecycle_ack.operation) ready = ReplicaReadiness();
            if (ready == CE_WORLD_INVALID) { RuntimeFailure(); return; }
            if (ready == CE_WORLD_READY) {
                if (!s.state_prepared) { Fail("whole state Apply has no surviving delivery ticket"); RuntimeFailure(); return; }
                CE_WorldInfo info = {};
                if (!CE_WorldFrameInfo(s.incoming, &info)) { WorldFail("applied state header unavailable"); RuntimeFailure(); return; }
                CE_StateImage exact = {};
                if (CE_StateReceiverImage(s.state_receiver, &s.state_ticket, &exact) != CE_DELIVERY_OK ||
                    info.epoch != s.state_ticket.key.epoch || info.frame_sequence != s.state_ticket.key.sequence ||
                    info.tick != s.state_ticket.key.tick) { Fail("state ticket/header changed before whole Apply"); RuntimeFailure(); return; }
                if (!CE_WorldApply(s.incoming)) { WorldFail("replica apply refused"); RuntimeFailure(); return; }
                if (!AdmissionCurrent()) { RuntimeFailure(); return; }
                int applied = CE_StateReceiverApplied(s.state_receiver, &s.state_ticket, info.epoch, info.frame_sequence, info.tick, CE_NetNowMs());
                if (applied != CE_DELIVERY_OK) { StateFailure("whole state delivery proof refused", applied); RuntimeFailure(); return; }
                s.state_prepared = false;
                s.have_frame = false;
                if (!DriveBirthAck()) { RuntimeFailure(); return; }
                if (!DriveStateControls()) { RuntimeFailure(); return; }
            }
        }
        if (!s.have_input) {
            CE_WorldInput input = {}; int sampled = CE_WorldSampleInput(&input);
            if (sampled < 0) { WorldFail("local semantic input refused"); RuntimeFailure(); return; }
            if (sampled && !CE_WorldEncodeInput(&input, s.input, sizeof s.input, &s.input_size)) { WorldFail("local input encode refused"); RuntimeFailure(); return; }
            s.have_input = sampled != 0;
        }
        if (s.have_input) {
            CE_WorldInput input = {}; if (!CE_WorldDecodeInput(&input, s.input, s.input_size)) { WorldFail("pending input decode refused"); RuntimeFailure(); return; }
            int sent = CE_NetSend(s.net, CE_NET_INPUT, input.tick, s.input, s.input_size);
            if (sent < 0) { Fail("invalid semantic input send"); RuntimeFailure(); return; }
            if (sent) {
                if (CE_WorldInputSent(input.sequence) != 1) {
                    WorldFail("local input lease acknowledgement refused"); RuntimeFailure(); return;
                }
                s.have_input = false;
            }
        }
    } else {
        if (!DriveLifecycle()) { RuntimeFailure(); return; }
        if (s.lifecycle_blocked) return;
        uint64_t now = CE_NetNowMs();
        if (now >= s.last_periodic && now - s.last_periodic >= 50 && !Capture(CE_NET_STATE)) RuntimeFailure();
    }
}
int CE_SessionSystemService(int native_ready, int device_state) {
    if (CE_LockstepRequested()) return CE_SESSION_NATIVE;
    if (!s.requested) return CE_SESSION_NATIVE;
    if (CE_SessionGameQuitCode()) return CE_SESSION_EXIT;
    if (device_state == 1) s.device_unready = false;
    else if (device_state == 2) s.device_unready = true;
    if (s.stage == FAILED || s.stage == DRAINING) { RuntimeFailure(); return CE_SESSION_EXIT; }
    if (!AdmissionCurrent()) { RuntimeFailure(); return CE_SESSION_EXIT; }
    s.service_ready_seen = true;
    if (device_state == 1) s.device_ready_seen = true;
    else if (device_state == 2) s.device_ready_seen = false;
    s.local_blocked = !native_ready || s.device_unready || CE_SessionWindowStatus() != 0;
    s.native_unready = !native_ready;
    if (TerminalTransport() || LifecycleExpired(CE_NetNowMs()) || StateExpired(CE_NetNowMs())) {
        if (!Poll()) RuntimeFailure();
        return CE_SESSION_EXIT;
    }
    // Latch local host unavailability before receiving any resume acknowledgement from the previous iteration.
    if (!DriveAvailability() || !DriveWait() || !Poll()) { RuntimeFailure(); return CE_SESSION_EXIT; }
    if (!native_ready || s.device_unready) return CE_SESSION_WAIT;
    if (s.host && s.shared_wait) {
        // Route a ready repair transaction to BeforeRender, while BeforeUpdate still refuses every native tick.
        // Ordinary availability WAITs and every unavailable local/peer phase retain the original service route.
        bool repair_render = RepairHold() && s.wait_phase == WAIT_HELD && RepairLocalReady() && !HostUnavailable();
        if (repair_render && !RepairHeldGate()) {
            Fail("repair shell route lost its frozen WAIT or lifecycle prefix");
            RuntimeFailure(); return CE_SESSION_EXIT;
        }
        if (!repair_render) return CE_SESSION_WAIT;
    }
    return CE_SESSION_NATIVE;
}
int CE_SessionBeginTeardown(int quit) {
    if (CE_LockstepRequested()) return CE_LockstepBeginTeardown(quit);
    if (!s.requested) return quit;
    if (s.stage != DRAINING && s.stage != FAILED) { SendControl(LEAVING); if (s.net) CE_NetClose(s.net); }
    s.stage = DRAINING; s.have_frame = s.have_baseline = s.have_input = false;
    s.birth_ack = {}; s.birth_ack_sent = 0;
    CancelAvailability();
    if (quit == QUIT_RESTART) { fprintf(stderr, "onslaught-ce: network engineering retry requires a new session\n"); return QUIT_USER_FRONTEND; }
    return quit;
}
void CE_SessionAfterWorldShutdown() {
    if (CE_LockstepRequested()) { CE_LockstepAfterWorldShutdown(); return; }
    if (!s.requested) return;
    if (s.world_started) CE_WorldEnd();
    if (CE_WorldActive()) { Fail("world teardown did not retire every network binding"); return; }
    CE_WorldFrameDestroy(s.capture); CE_WorldFrameDestroy(s.incoming); CE_NetDestroy(s.net);
    CE_StateSenderDestroy(s.state_sender); CE_StateReceiverDestroy(s.state_receiver);
    CE_CompatibilityRetire();
    s = Session();
}
const char *CE_SessionError() { return CE_LockstepRequested() ? CE_LockstepError() : error_text; }
const char *CE_SessionNotice() { return CE_LockstepRequested() ? CE_LockstepNotice() : s.notice; }
int CE_SessionActive() { return CE_LockstepRequested() ? CE_LockstepActive() : s.requested && s.stage != OFF; }
int CE_SessionMissionVMBlocked() { return s.mission_vm_blocked ? 1 : 0; }
int CE_SessionExitCode(int native_code, int native_quit_seen) {
    if (CE_LockstepRequested()) return CE_LockstepExitCode(native_code, native_quit_seen);
    if (native_quit_seen || exit_outcome == EXIT_NONE) return native_code;
    return exit_outcome == EXIT_GRACEFUL ? 0 : 1;
}
}
