#ifndef CE_NETWORK_OFF
#include "ce_network_stl.h"
#endif
#include "ce_network_result_exchange.h"
// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_network_lockstep.cpp: two-player lockstep co-op (prototype, test-automation builds only).
//
// Both peers load the same admitted two-player test level and run the genuine simulation. Each 20 Hz tick (one CGame::Update)
// runs only when both players' inputs for that tick are present: a peer's own controls are sampled at tick T and
// applied at T + delay on both peers; the other player's arrive over the authenticated transport. Admission and
// completion controls, a digest of script-visible pre-run flags, and selected state hashes also cross the connection;
// no saved-career file is exchanged. Mission scripts, AI, triggers, radio, cutscenes and the level's end run on both.
// The C runtime's rand and the game's random stream are put back after each tick's update, so the draws a peer makes
// for its own view (particles and other effects drawn only where they are seen) never reach the shared simulation.
// Every few ticks each peer hashes selected simulation fields and sends them; a mismatch stops the session. Equal
// hashes do not establish complete state equality, career agreement, content identity or player-ready multiplayer.
//
// BEA_LOCKSTEP_HOST=IP:PORT or BEA_LOCKSTEP_JOIN=IP:PORT, BEA_LOCKSTEP_SECRET=64 hex digits; BEA_LOCKSTEP_DELAY (ticks,
// default 3), BEA_LOCKSTEP_HASH_EVERY (ticks, default 20). Test only: BEA_LOCKSTEP_SKEW_AT=T applies this peer's own
// input one tick late from tick T (a deliberate desync), BEA_LOCKSTEP_DUMP=PATH writes every hashed tick's things,
// BEA_LOCKSTEP_QUIT_AT=T ends both peers' games after tick T, BEA_LOCKSTEP_STALL_EVERY=N stalls this peer 300 ms every N ticks
// (BEA_LOCKSTEP_STALL_MS=M lengthens every stall; BEA_LOCKSTEP_STALL_AT=T stalls it once, after tick T: a frozen peer, as a loaded machine makes;
// BEA_LOCKSTEP_STALL_FROM=T starts the every-N stalls at tick T; BEA_LOCKSTEP_FLIP_CW_AT=T leaves the x87 control word at 027f after tick T-1, BEA_LOCKSTEP_NO_CW_FENCE=1 keeps the tick entry from repairing it),
// BEA_LOCKSTEP_NO_PARTICLES=1 makes this peer create no particles while the other peer's pool is untouched (the widest difference
// two views can make to what a created effect lets the simulation do), BEA_LOCKSTEP_EXTRA_RENDERS=K makes it draw at least K+1
// frames between two ticks (the widest difference in how often the two views are redrawn), BEA_LOCKSTEP_POISON_STACK=B fills the stack below the
// update's first call with the byte B at every update (two peers poisoned differently read different values from any local the simulation never wrote);
// BEA_LOCKSTEP_TEST_UNINIT_READ=1 adds the deliberate bug that makes such a read matter (the control of that gate).
#include "ce_network_lockstep.h"
#include "ce_network_chat_ui.h"
#include <algorithm>
#include <limits>
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_TEXT)
#include "../network/text_chat_owner.h"
#endif
#endif
#include "ce_network_campaign_return.h"
#include "ce_network_world.h"
#include "ce_network_session.h"
#include "../network/network.h"
#include "../network/ice_game.h"
#include "ce_network_boot.h"
#include "common.h"
#include "game.h"
#include "world.h"
#include "player.h"
#include "eventmanager.h"
#include "Listener.h"
#include <typeinfo>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(CE_NET_TEST_AUTOMATION) && !defined(_WIN32)
#include <sys/prctl.h>
#endif
// The game's own file layer (game-relative paths), kept before the dump's #undef below.
static FILE *GameOpen(const char *name, const char *mode) { return port_fopen(name, mode); }
// The test dump is the port's own output, written where the caller asks (as updatedump.cpp): not the path layer.
#undef fopen

extern "C" void PortCrtRandState(unsigned int *calls, unsigned int *hold);	// crtrand.cpp
extern "C" void PortCrtRandSet(unsigned int calls, unsigned int hold);
extern "C" void (*gPortCrtTrace)(void *caller);
#ifdef CE_NET_TEST_AUTOMATION
extern "C" int CE_CompatibilityLockstepDigest(uint32_t level, uint8_t out[32]);
#endif
extern "C" int CE_NetPreRunFlagsDigest(const uint8_t *flags, uint32_t count, uint8_t *out);
extern "C" void CE_WorldLockstepSample(uint32_t local_player, CE_WorldInput *out);
extern "C" void CE_WorldLockstepApply(uint32_t player, const CE_WorldInput *in);
extern "C" void CE_WorldLockstepRandom(int save, int *value, int *seed);
extern "C" uint64_t CE_WorldLockstepThings(FILE *dump, uint32_t *count);
extern "C" int CE_WorldLockstepGates(uint64_t *out);
extern "C" void CE_WorldLockstepPartnerStart(void *init);
extern "C" void CE_WorldLockstepPlaceEmitters(void *const *weapons, const int *orientate, unsigned count);
extern "C" int CE_WorldLockstepEndTick(void);
extern "C" void CE_WorldLockstepBeginTick(void);

// The label each side shows the other when their builds do not admit each other (as the session's world mode).
#ifdef CE_VERSION
const char kVersionLabel[] = CE_VERSION;
#else
const char kVersionLabel[] = "private build";
#endif
namespace {
const uint32_t kMagic = 0x43454c31u;	// CEL1
enum { MSG_READY = 1, MSG_INPUT = 2, MSG_HASH = 3, MSG_DESYNC = 4,
	MSG_FINAL = 5, MSG_FINAL_ACK = 6, MSG_FINAL_RELEASE = 7, MSG_FINAL_RECEIPT = 8,
	MSG_MISSION_FINAL = 9, MSG_FINAL_DONE = 10, MSG_FINAL_DONE_ACK = 11, MSG_START_ACK = 12, MSG_PRERUN_ACK = 13 };
// FINISHED: the level was won or lost at the same tick on both peers; from then on each peer's end screen is its own.
enum Stage { OFF, CONNECTING, LOADED, STARTING, PRERUN, RUNNING, STOPPING, FINISHED, FAILED, ENDED };
const unsigned kRing = 64;				// ticks of input/hash history; delay and hash spacing stay well inside it
// kStartMs: how long a peer waits for the other to connect and to pass each start barrier, from its own load's end. The two peers load independently (a slow disk, a software
// renderer, a loaded machine: 45 s apart in measured sessions), so a minute from one's own load was too tight.
const uint64_t kWaitNoticeMs = 500, kGiveUpMs = 30000, kStartMs = 180000;

struct Slot { uint32_t tick; bool present; CE_WorldInput input; };
struct HashSlot { uint32_t tick; bool local, remote; uint64_t mine, theirs; };
struct Lockstep {
    bool campaign = false, campaign_result_started = false, campaign_deferred_set = false;
    CeCampaignIdentity campaign_identity = {};
    CeCampaignOutcome campaign_outcome = CE_CAMPAIGN_OUTCOME_UNKNOWN;
    CeNetworkCampaign campaign_state = {};
    CeResultExchange campaign_result = {};
    CeResultMessage campaign_deferred = {};
    uint32_t campaign_sent_kind = 0;

	Stage stage = OFF;
	bool admission_required = false; // fixed at configuration; a lost boot flag must not become an automation exemption
	bool host = false, peer_ready = false, ready_sent = false, outcome_error = false;
	bool connection_established = false; // only connection attempts, not later readiness waits, may retry
	bool start_ack_received = false;
	bool prerun_ack_sent = false, prerun_ack_received = false, prerun_finished = false;
	bool shown = false;					// a co-op level is up: one view, the local player's, until its world shuts down
	uint32_t level = 0, delay = 3, hash_every = 20, skew_at = 0, quit_at = 0, stall_every = 0, stall_at = 0, stall_from = 0, stall_ms = 300, flip_cw_at = 0;
	bool test_uninit_read = false;		// test: BEA_LOCKSTEP_TEST_UNINIT_READ=1 makes the update read a poisoned local into the generator (the deliberate bug the stack gate must catch)
	bool counts_reported = false;		// the transport's counts were printed (once per session)
	int poison_stack = -1;				// test: the byte the stack below the update is filled with at every tick start (-1: off)
	uint32_t tick = 0;					// the next tick to run (0 = the first after the start barrier)
	bool applied[2] = {}, sampled = false, ran = false;
	uint8_t pre_run_slots[MAX_CAREER_SLOTS*8] = {}; // local script-visible flags before the pre-run
	uint8_t pre_run_digest[32] = {};
	bool pre_run_slots_captured = false;
	CE_NetSession *net = NULL;
	Slot mine[kRing] = {}, theirs[kRing] = {};
	HashSlot hashes[kRing] = {};
	CE_WorldInput current[2] = {};		// the inputs applied at this tick, for the invert/walker hooks
	unsigned crt_calls = 0, crt_hold = 0;	// the simulation's rand state at the end of the last update
	int rng_value = 0, rng_seed = 0;
	uint64_t waiting_since = 0;
	uint32_t hashes_compared = 0;
	// Test-only endpoint: both peers must compare the final state and acknowledge before teardown.
	bool final_local = false, final_peer = false, final_ack_sent = false, final_ack_received = false;
	bool final_release_sent = false, final_receipt_sent = false, final_confirmed = false;
	bool final_done_sent = false, final_done_received = false;
	bool final_mission = false, final_peer_mission = false;
	uint32_t final_tick = 0, final_peer_tick = 0;
	uint64_t final_hash = 0, final_peer_hash = 0, final_started = 0;
	char notice[96] = {};
	FILE *dump = NULL;
	CE_NetConfig config = {}; char ip[16] = {};
} L;
bool requested_checked, requested;
// Muzzle effects to place after this tick's update (CE_LockstepDeferEmitter).
void *deferred_weapons[64]; int deferred_orientate[64]; unsigned deferred_count; bool placing;

bool Env(const char *name) { const char *v = getenv(name); return v && *v; }
uint32_t EnvU(const char *name, uint32_t def) { const char *v = getenv(name); return v && *v ? (uint32_t)strtoul(v, NULL, 10) : def; }

// One line a session on what the transport saw: messages out and in, and the packets it refused (malformed, not sealed with this session's secret, from a stranger),
// so a test of bad input can tell refusal from nothing happening.
void ReportNetworkCounts(CE_NetSession *net) {
	if (!net || L.counts_reported) return;
	L.counts_reported = true;
	CE_NetStatus st; CE_NetGetStatus(net, &st);
	fprintf(stderr, "onslaught-ce: co-op network: %u messages sent, %u received, %u packets rejected\n", (unsigned)st.sent_messages, (unsigned)st.received_messages, (unsigned)st.rejected_packets);
}
char last_error[200];	// the first failure's words, kept past the session's reset (the lobby reads it after the world is gone)
char connect_why[200];	// what a player is told when the connection never comes up (set when the wait begins; the wait's deadline and a transport that gives up first say the same)
// How a finished or failed session asks the game to leave the level. A shipped co-op game (started by the lobby, admitted by the boot) goes back to the front end: the lobby then tells
// the player what happened, where a quit to the system reads as a closed window and shows nothing. A test-automation pair has no lobby and quits to the system, as it always did.

#if defined(CE_NETWORK_TEXT)
CeTextChat::Owner chat;
CE_NetSession *chat_session = nullptr;
uint64_t chat_generation = 0;
uint32_t chat_epoch = 0;
void ChatRetire() {
    chat.Close(); chat_session = nullptr; chat_epoch = 0;
}
uint32_t ChatResult(CeTextChat::Result result) {
    switch (result) {
    case CeTextChat::Result::Ok: return CE_CHAT_OK;
    case CeTextChat::Result::NotOpen: case CeTextChat::Result::Disabled: return CE_CHAT_CLOSED;
    case CeTextChat::Result::RateLimited: return CE_CHAT_RATE_LIMITED;
    case CeTextChat::Result::Backpressure: case CeTextChat::Result::ClockRegression: return CE_CHAT_BUSY;
    default: return CE_CHAT_INVALID;
    }
}
#else
void ChatRetire() {}
#endif
int LeaveCode() { return (L.admission_required || CE_BootLockstep()) ? QT_QUIT_TO_FRONTEND : QT_QUIT_TO_SYSTEM; }
static void CampaignStop(CeCampaignStop reason);
bool Fail(const char *message, bool desync_queued = false) {
    CampaignStop(CE_CAMPAIGN_STOP_FAILURE);
	if (L.stage != FAILED) { fprintf(stderr, "onslaught-ce: co-op session stopped: %s\n", message); if (!last_error[0]) snprintf(last_error, sizeof last_error, "%s", message); }
	L.stage = FAILED; L.outcome_error = true; L.notice[0] = 0;
	{ volatile uint8_t *secret = L.config.secret; for (unsigned i = 0; i < sizeof L.config.secret; ++i) secret[i] = 0; }
	// A queued desync is ordered ahead of graceful leave on the reliable control channel.
	// Other failures abort, so the peer is not told that this player left voluntarily.
	CE_NetSession *net = L.net; L.net = NULL;
	if (net) { CE_NetPoll(net, CE_NetNowMs(), 0); ReportNetworkCounts(net); if (desync_queued) (ChatRetire(), CE_NetClose(net)); (ChatRetire(), CE_NetDestroy(net)); }
	if (!CE_SessionGameQuitCode()) CE_SessionGameSetQuit(LeaveCode());
	return false;
}
bool AdmissionLost(const char *why) {
	char text[200]; snprintf(text, sizeof text, "co-op content admission lost: %.145s", why); return Fail(text);
}
// Confirmed completion and teardown no longer own admitted simulation reads.
// Their boot/compatibility context may already be retired; keep the existing
// outcome instead of turning a later native end-screen boundary into failure.
bool AdmissionRetired() {
    return L.stage==OFF||L.stage==FINISHED||L.stage==ENDED;
}
bool AdmissionCurrent() {
	if(!L.admission_required||AdmissionRetired())return true;
	if (L.stage == FAILED) return false;
	CE_CompatibilityResult current = {}; char why[160] = {};
	if (CE_BootPrepared(&current, why, sizeof why) == 1 &&
		!memcmp(current.digest, L.config.compatibility, sizeof current.digest)) return true;
	return AdmissionLost(why[0] ? why : "the prepared context changed");
}
bool AdmissionLive() {
	if(!L.admission_required||AdmissionRetired())return true;
	if (L.stage == FAILED) return false;
	char why[160] = {};
	// Root/role/context checks remain at startup, update and poll barriers. Inner send/apply
	// fences only observe the sticky state, without repeated root copies or an input rescan.
	return CE_CompatibilityAdmissionLive(why, sizeof why) == 1 || AdmissionLost(why);
}
bool Endpoint(const char *text, char *ip, uint16_t &port) {
	unsigned a, b, c, d, p; char tail;
	if (!text || sscanf(text, "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &p, &tail) != 5 || a > 255 || b > 255 || c > 255 || d > 255 || !p || p > 65535) return false;
	snprintf(ip, 16, "%u.%u.%u.%u", a, b, c, d); port = (uint16_t)p; return true;
}
bool Hex(const char *text, uint8_t *out) {
	if (!text || strlen(text) != 2 * CE_NET_SECRET_BYTES) return false;
	for (unsigned i = 0; i < CE_NET_SECRET_BYTES; ++i) {
		unsigned v; if (sscanf(text + 2 * i, "%2x", &v) != 1) return false; out[i] = (uint8_t)v;
	}
	return true;
}

// FNV-1a-style digest with a fixed nonstandard offset; floats use their bit patterns. The digest may collide.
struct Hash {
	uint64_t h = 1469598103934665603ull;
	void Bytes(const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } }
	void U32(uint32_t v) { Bytes(&v, 4); }
	void F32(float v) { Bytes(&v, 4); }
	void Text(const char *s) { Bytes(s, strlen(s) + 1); }
};
// Selected simulation state: random streams, event clock and queue counters, game state, player flight/power/active,
// engine form, augmentation and weapon selections and named-weapon gates, plus each thing's class, position,
// orientation, velocity and unit life, and the script-visible game slot flags. The rand states are the ones saved
// at the end of the update; other career state, persistence and mission-script execution state are not included.
bool CapturePreRunSlots() {
	L.pre_run_slots_captured=false;
	for (int slot=0;slot<MAX_CAREER_SLOTS*8;++slot) L.pre_run_slots[slot]=(uint8_t)(GAME.GetSlot(slot)!=0);
	if (!CE_NetPreRunFlagsDigest(L.pre_run_slots,sizeof L.pre_run_slots,L.pre_run_digest)) return false;
	L.pre_run_slots_captured=true;
	return true;
}
bool PreRunSlotsUnchanged() {
	if (!L.pre_run_slots_captured) return false;
	for (int slot=0;slot<MAX_CAREER_SLOTS*8;++slot)
		if ((uint8_t)(GAME.GetSlot(slot)!=0)!=L.pre_run_slots[slot]) return false;
	return true;
}
uint64_t StateHash(uint32_t tick) {
	if (!L.pre_run_slots_captured) { Fail("co-op pre-run slot flags were not captured"); return 0; }
	Hash all;
	all.U32(tick); all.U32(L.crt_calls); all.U32(L.crt_hold); all.U32((uint32_t)L.rng_value); all.U32((uint32_t)L.rng_seed);
	all.F32(EVENT_MANAGER.GetTime()); all.U32(EVENT_MANAGER.GetFrameCount()); all.U32(EVENT_MANAGER.GetCurrentEventProcessNum());
	all.U32((uint32_t)EVENT_MANAGER.TotalEvents()); all.U32((uint32_t)GAME.GetGameState());
	// Game::GetSlot exposes MAX_CAREER_SLOTS*8 bits; the rest of the backing array has no script-visible slot.
	all.Bytes(L.pre_run_slots, sizeof L.pre_run_slots);
	for (int slot=0;slot<MAX_CAREER_SLOTS*8;++slot) all.U32((uint32_t)GAME.GetSlot(slot));
	if (L.dump) fprintf(L.dump, "T %u crt %u %08x rng %d %d ev %08x %lu %lu %d state %d\n", tick, L.crt_calls, L.crt_hold, L.rng_value, L.rng_seed,
		[] { float t = EVENT_MANAGER.GetTime(); uint32_t b; memcpy(&b, &t, 4); return b; }(), (unsigned long)EVENT_MANAGER.GetFrameCount(),
		(unsigned long)EVENT_MANAGER.GetCurrentEventProcessNum(), EVENT_MANAGER.TotalEvents(), (int)GAME.GetGameState());
	uint64_t gates = 0;
	if (!CE_WorldLockstepGates(&gates)) { Fail("co-op player gates or weapon inventory could not be compared"); return 0; }
	all.Bytes(&gates, sizeof gates);
	uint32_t count = 0; uint64_t things = CE_WorldLockstepThings(L.dump, &count);
	all.U32(count); all.Bytes(&things, 8);
	return all.h;
}

void Encode(uint8_t *buffer, uint32_t &size, uint32_t type, uint32_t tick) {
	CE_NetWriter w; CE_NetWriterInit(&w, buffer, CE_NET_MAX_INPUT);
	CE_NetWriteU32(&w, kMagic); CE_NetWriteU8(&w, (uint8_t)type); CE_NetWriteU32(&w, tick); size = w.size;
}
bool Send(const uint8_t *data, uint32_t size, uint32_t tick) {
	if (!AdmissionLive()) return false;
	int sent = CE_NetSend(L.net, CE_NET_CONTROL, tick, data, size);
	if (sent < 0) return Fail("a co-op message could not be queued");
	if (sent == 0) return Fail("the connection to the other player is backed up");
	return true;
}
bool SendReady() {
	if (!L.pre_run_slots_captured) return Fail("co-op pre-run flags were not captured");
	uint8_t b[CE_NET_MAX_INPUT]; uint32_t n; Encode(b, n, MSG_READY, 0);
	CE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n;
	CE_NetWriteU32(&w, L.level); CE_NetWriteU32(&w, L.delay); CE_NetWriteU32(&w, L.hash_every); CE_NetWriteU32(&w, L.quit_at);
	// The invited peer receives a deterministic BLAKE2b-256 digest, not the raw flags or a save file. It remains
	// linkable and guessable for low-entropy snapshots; this test-only path is not a player-facing privacy policy.
	CE_NetWriteBytes(&w, L.pre_run_digest, sizeof L.pre_run_digest);
	if (w.error || !Send(b, w.size, 0)) return false;
	L.ready_sent = true;
	return true;
}
bool SendPreRunAck() {
	uint8_t b[CE_NET_MAX_INPUT]; uint32_t n; Encode(b, n, MSG_PRERUN_ACK, 0);
	return Send(b, n, 0);
}
bool SendInput(const CE_WorldInput &in, uint32_t tick) {
	uint8_t b[CE_NET_MAX_INPUT]; uint32_t n; Encode(b, n, MSG_INPUT, tick);
	CE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n;
	CE_NetWriteU8(&w, (uint8_t)in.invert_ground); CE_NetWriteU8(&w, (uint8_t)in.invert_jet); CE_NetWriteU8(&w, (uint8_t)in.walker_mouse_any);
	CE_NetWriteU8(&w, (uint8_t)in.mouse_ground_aim); CE_NetWriteF32(&w, in.ground_yaw); CE_NetWriteF32(&w, in.ground_pitch);
	CE_NetWriteU8(&w, (uint8_t)in.count);
	for (unsigned i = 0; i < in.count; ++i) {
		CE_NetWriteU8(&w, (uint8_t)in.actions[i].button); CE_NetWriteU8(&w, (uint8_t)in.actions[i].source); CE_NetWriteF32(&w, in.actions[i].value);
	}
	if (w.error) return Fail("a control sample does not fit its message");
	return Send(b, w.size, tick);
}
bool SendHash(uint32_t tick, uint64_t hash) {
	uint8_t b[CE_NET_MAX_INPUT]; uint32_t n; Encode(b, n, MSG_HASH, tick);
	CE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n; CE_NetWriteU64(&w, hash);
	return !w.error && Send(b, w.size, tick);
}
bool SendStartAck(uint64_t hash) {
	uint8_t b[CE_NET_MAX_INPUT]; uint32_t n; Encode(b, n, MSG_START_ACK, 0);
	CE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n; CE_NetWriteU64(&w, hash);
	return !w.error && Send(b, w.size, 0);
}
// Every terminal control is tied to the exact stop tick and hash. Transport delivery alone is not application proof.
bool SendFinal(uint32_t type, uint64_t hash) {
	uint8_t b[CE_NET_MAX_INPUT]; uint32_t n; Encode(b, n, type, L.final_tick);
	CE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n; CE_NetWriteU64(&w, hash);
	return !w.error && Send(b, w.size, L.final_tick);
}

static const uint32_t MSG_CAMPAIGN_RESULT = 101;

static CeCampaignRole CampaignRole() {
    return L.host ? CE_CAMPAIGN_HOST : CE_CAMPAIGN_GUEST;
}

static bool CampaignConfigure() {
    ce_campaign_return_clear();
    CE_CampaignLaunch launch = {};
    int host = 0;
    if (!CE_BootCampaignGet(&launch, &host)) return true;
    if (launch.version != 1 || launch.mission != L.level ||
        bool(host) != L.host || !launch.session || !launch.attempt) {
        CE_BootCampaignClear();
        return Fail("The campaign launch does not match the loaded session.");
    }
    L.campaign = true;
    L.campaign_identity = {launch.session, launch.attempt, launch.mission};
    ce_network_campaign_init(&L.campaign_state);
    ce_result_exchange_init(&L.campaign_result);
    return true;
}

static void CampaignStop(CeCampaignStop reason) {
    if (!L.campaign) return;
    if (L.campaign_result_started)
        ce_result_exchange_disconnect(&L.campaign_result, L.campaign_identity);
    ce_network_campaign_stop(&L.campaign_state, L.campaign_identity, reason);
    CE_BootCampaignClear();
}

static bool CampaignReady() {
    if (!L.campaign) return true;
    if (!L.campaign_result_started) return false;
    return L.host ? L.campaign_result.phase == CE_RESULT_HOST_RECEIPTED :
        L.campaign_result.phase == CE_RESULT_GUEST_DECIDED;
}

void CampaignCaptureReturn() {
    CeCampaignReturn saved = {};
    if (L.campaign && !ce_campaign_return_get(&saved))
        ce_campaign_return_capture(&L.campaign_state,
            L.final_confirmed && CampaignReady(), L.outcome_error);
}


static bool CampaignSendPending() {
    if (!L.campaign || !L.campaign_result_started) return true;
    CeResultMessage message;
    if (!ce_result_exchange_pending(&L.campaign_result, &message)) return true;
    if (L.campaign_sent_kind == message.kind) return true;
    uint8_t buffer[192];
    uint32_t size = 0;
    Encode(buffer, size, MSG_CAMPAIGN_RESULT, L.final_tick);
    size_t written = 0;
    if (!ce_result_message_encode(&message, buffer + size, sizeof(buffer) - size, &written) ||
        !Send(buffer, size + uint32_t(written), L.final_tick))
        return Fail("The campaign result could not be sent.");
    L.campaign_sent_kind = message.kind;
    return true;
}

static bool CampaignAccept(const CeResultMessage &message) {
    CeResultAction action = ce_result_exchange_receive(&L.campaign_result, &message);
    if (action != CE_RESULT_ACCEPTED && action != CE_RESULT_DUPLICATE)
        return Fail("The campaign result did not agree.");
    L.campaign_state = L.campaign_result.observed;
    return CampaignSendPending();
}

static bool CampaignTerminal() {
    if (!L.campaign) return true;
    if (L.campaign_outcome != CE_CAMPAIGN_OUTCOME_WIN &&
        L.campaign_outcome != CE_CAMPAIGN_OUTCOME_LOSS)
        return Fail("The mission ended without an observed campaign outcome.");
    if (ce_result_exchange_begin(&L.campaign_result, L.campaign_identity, CampaignRole(),
            L.campaign_outcome, L.final_tick, L.final_hash) != CE_RESULT_ACCEPTED)
        return Fail("The campaign result could not begin.");
    L.campaign_result_started = true;
    L.campaign_state = L.campaign_result.observed;
    if (L.campaign_deferred_set) {
        L.campaign_deferred_set = false;
        if (!CampaignAccept(L.campaign_deferred)) return false;
    }
    return CampaignSendPending();
}

static bool CampaignReceive(const uint8_t *data, size_t size, uint32_t tick) {
    CeResultMessage message;
    if (!L.campaign || !ce_result_message_decode(&message, data, size) ||
        message.identity.session != L.campaign_identity.session ||
        message.identity.attempt != L.campaign_identity.attempt ||
        message.identity.mission != L.campaign_identity.mission ||
        message.terminal_tick != tick)
        return Fail("The campaign result descriptor is invalid.");
    if (!L.campaign_result_started) {
        if (L.host || message.kind != CE_RESULT_OFFER || message.sender != CE_CAMPAIGN_HOST ||
            !L.final_peer || !L.final_peer_mission || L.final_peer_tick != tick ||
            L.final_peer_hash != message.terminal_fingerprint)
            return Fail("The campaign result arrived outside its mission boundary.");
        if (L.campaign_deferred_set &&
            (L.campaign_deferred.outcome != message.outcome ||
             L.campaign_deferred.terminal_tick != message.terminal_tick ||
             L.campaign_deferred.terminal_fingerprint != message.terminal_fingerprint))
            return Fail("The pending campaign result changed.");
        L.campaign_deferred = message;
        L.campaign_deferred_set = true;
        return true;
    }
    if (tick != L.final_tick) return Fail("The campaign result belongs to another terminal tick.");
    return CampaignAccept(message);
}

bool FinalProgress() {
    if (L.campaign) {
        if (!CampaignSendPending()) return false;
        if (!CampaignReady()) return true;
    }

	if (L.stage != STOPPING || !L.final_local || !L.final_peer) return true;
	if (L.final_tick != L.final_peer_tick || L.final_mission != L.final_peer_mission)
		return Fail("the two peers reached different co-op endpoints");
	if (L.final_hash != L.final_peer_hash) {
		uint8_t b[CE_NET_MAX_INPUT]; uint32_t n; Encode(b, n, MSG_DESYNC, L.final_tick);
		CE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n;
		CE_NetWriteU64(&w, L.final_hash); CE_NetWriteU64(&w, L.final_peer_hash);
		const bool desync_queued = !w.error && CE_NetSend(L.net, CE_NET_CONTROL, L.final_tick, b, w.size) > 0;
		return Fail("the final co-op states differ", desync_queued);
	}
	// Count includes the shared start hash at tick zero. A missing scheduled comparison is not a pass.
	const uint32_t expected = 1u + L.final_tick / L.hash_every;
	if (L.hashes_compared != expected) return Fail("the final co-op state arrived before every scheduled comparison");
	if (!L.final_ack_sent) {
		if (!SendFinal(MSG_FINAL_ACK, L.final_hash)) return false;
		L.final_ack_sent = true;
	}
	if (L.host && L.final_ack_received && !L.final_release_sent) {
		if (!SendFinal(MSG_FINAL_RELEASE, L.final_hash)) return false;
		L.final_release_sent = true;
	}
	return true;
}
void FinalComplete() {
    if (L.campaign && !CampaignReady()) return;
    if (L.campaign && L.campaign_outcome == CE_CAMPAIGN_OUTCOME_WIN &&
        !ce_network_campaign_agreed_win(&L.campaign_state)) {
        Fail("The campaign win has not been agreed.");
        return;
    }
	L.final_confirmed = true;
	if (L.final_mission) {
		// The selected simulation state is compared, not every objective or persistence field.
		fprintf(stderr, "onslaught-ce: co-op mission selected state confirmed at tick %u, hash %016llx, %u scheduled hashes compared\n",
			L.final_tick, (unsigned long long)L.final_hash, L.hashes_compared);
		L.stage = FINISHED; L.notice[0] = 0;
	} else {
		fprintf(stderr, "onslaught-ce: co-op test confirmed final tick %u, terminal hash %016llx, %u scheduled hashes compared\n",
			L.final_tick, (unsigned long long)L.final_hash, L.hashes_compared);
		if (!CE_SessionGameQuitCode()) CE_SessionGameSetQuit(QT_QUIT_TO_SYSTEM);
	}
}
bool Compare(HashSlot &h) {
	if (!h.local || !h.remote) return true;
	++L.hashes_compared;
	if (h.mine == h.theirs) return true;
	bool desync_queued = false;
	{	// tell the other player first, so both games stop with the same verdict and both hashes in their logs
		uint8_t b[CE_NET_MAX_INPUT]; uint32_t n; Encode(b, n, MSG_DESYNC, h.tick);
		CE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n; CE_NetWriteU64(&w, h.mine); CE_NetWriteU64(&w, h.theirs);
		desync_queued = !w.error && CE_NetSend(L.net, CE_NET_CONTROL, h.tick, b, w.size) > 0;
	}
	char text[160];
	snprintf(text, sizeof text, "the two games went out of step at tick %u (this game %016llx, the other %016llx)", h.tick,
		(unsigned long long)h.mine, (unsigned long long)h.theirs);
	return Fail(text, desync_queued);
}
bool Receive() {
	CE_NetMessage m; uint8_t b[CE_NET_MAX_INPUT];
	for (;;) {
		int got = CE_NetReceive(L.net, &m, b, sizeof b);
		if (got < 0) return Fail("an oversized message arrived from the other player");
		if (!got) return true;
		if (m.kind != CE_NET_CONTROL) return Fail("an unexpected message kind arrived from the other player");
		CE_NetReader r; CE_NetReaderInit(&r, b, m.size);
		uint32_t magic = CE_NetReadU32(&r), type = CE_NetReadU8(&r), tick = CE_NetReadU32(&r);
		if (r.error || magic != kMagic) return Fail("a malformed message arrived from the other player");

        if (type == MSG_CAMPAIGN_RESULT) {
            if (!CampaignReceive(r.data + r.position, r.size - r.position, tick) ||
                !FinalProgress()) return false;
            continue;
        }
        if (type == MSG_READY) {
			uint32_t level = CE_NetReadU32(&r), delay = CE_NetReadU32(&r), every = CE_NetReadU32(&r), end = CE_NetReadU32(&r);
			uint8_t digest[32]; CE_NetReadBytes(&r, digest, sizeof digest);
			if (!CE_NetReadDone(&r) || m.tick != 0 || tick != 0 || L.stage != STARTING || L.peer_ready || !L.pre_run_slots_captured)
				return Fail("a malformed start message arrived");
			// A difference is named on both sides: this game's own start message goes out first when it has not yet (the other player compares what it receives with its own
			// settings, and would otherwise see only that this game left).
			if (level != L.level || delay != L.delay || every != L.hash_every || end != L.quit_at) {
				if (!L.ready_sent) SendReady();
				return Fail("the other player started a different level or co-op setting");
			}
			if (memcmp(digest,L.pre_run_digest,sizeof digest)!=0) {
				if (!L.ready_sent) SendReady();
				return Fail("the two games have different pre-run career flags");
			}
			L.peer_ready = true;
		} else if (type == MSG_PRERUN_ACK) {
			if (!CE_NetReadDone(&r) || m.tick != 0 || tick != 0 || L.stage != STARTING || !L.peer_ready ||
				L.prerun_ack_received || L.prerun_finished)
				return Fail("an unexpected co-op pre-run acknowledgement arrived");
			L.prerun_ack_received = true;
		} else if (type == MSG_INPUT) {
			CE_WorldInput in = {};
			in.invert_ground = CE_NetReadU8(&r); in.invert_jet = CE_NetReadU8(&r); in.walker_mouse_any = CE_NetReadU8(&r);
			in.mouse_ground_aim = CE_NetReadU8(&r); in.ground_yaw = CE_NetReadF32(&r); in.ground_pitch = CE_NetReadF32(&r);
			in.count = CE_NetReadU8(&r);
			if (in.count > CE_WORLD_MAX_ACTIONS) return Fail("a control sample has too many actions");
			for (unsigned i = 0; i < in.count; ++i) {
				in.actions[i].button = CE_NetReadU8(&r); in.actions[i].source = CE_NetReadU8(&r); in.actions[i].value = CE_NetReadF32(&r);
			}
			if (!CE_NetReadDone(&r) || m.tick != tick || !CE_WorldLockstepValidInput(&in))
				return Fail("a malformed control sample arrived");
			if (tick < L.tick || tick >= L.tick + kRing) return Fail("a control sample arrived outside the input window");
			Slot &s = L.theirs[tick % kRing];
			if (s.present && s.tick == tick) return Fail("a control sample arrived twice");
			s.tick = tick; s.present = true; s.input = in;
		} else if (type == MSG_HASH) {
			uint64_t hash = CE_NetReadU64(&r);
			if (L.stage == STARTING && (!L.prerun_ack_sent || !L.prerun_ack_received))
				return Fail("a co-op start hash arrived before pre-run admission");
			if (!CE_NetReadDone(&r) || !L.hash_every || tick % L.hash_every || m.tick != tick)
				return Fail("a malformed state hash arrived");
			// The peer cannot get ahead by more than the negotiated input delay plus one in-progress tick.
			// Older hashes remain valid until they leave the bounded ring; use wide arithmetic at uint32 limits.
			uint64_t now = L.tick, span = (uint64_t)L.hash_every * kRing;
			uint64_t ahead = (L.stage == STOPPING || L.stage == FINISHED) ? 0u : (uint64_t)L.delay + 1u;
			if ((uint64_t)tick > now + ahead || (uint64_t)tick + span <= now)
				return Fail("a state hash arrived outside retained tick history");
			HashSlot &h = L.hashes[(tick / L.hash_every) % kRing];
			if (h.tick != tick) {
				if (h.local != h.remote) return Fail("a state hash would replace an unmatched comparison");
				h = HashSlot{tick, false, false, 0, 0};
			}
			if (h.remote) return Fail("a state hash arrived twice");
			h.remote = true; h.theirs = hash;
			if (!Compare(h)) return false;
		} else if (type == MSG_START_ACK) {
			uint64_t hash = CE_NetReadU64(&r); HashSlot &h = L.hashes[0];
			if (!CE_NetReadDone(&r) || m.tick != 0 || tick != 0 || L.stage != STARTING || L.start_ack_received ||
				!h.local || !h.remote || !L.hashes_compared || h.mine != h.theirs || hash != h.mine)
				return Fail("an unexpected co-op start acknowledgement arrived");
			L.start_ack_received = true;
		} else if (type == MSG_FINAL || type == MSG_MISSION_FINAL || type == MSG_FINAL_ACK ||
			type == MSG_FINAL_RELEASE || type == MSG_FINAL_RECEIPT || type == MSG_FINAL_DONE || type == MSG_FINAL_DONE_ACK) {
			uint64_t hash = CE_NetReadU64(&r);
			if (!CE_NetReadDone(&r) || !tick || m.tick != tick || (L.stage != RUNNING && L.stage != STOPPING))
				return Fail("a malformed final co-op message arrived");
			if (type == MSG_FINAL || type == MSG_MISSION_FINAL) {
				bool mission = type == MSG_MISSION_FINAL;
				uint64_t ahead = (L.stage == STOPPING || L.stage == FINISHED) ? 0u : (uint64_t)L.delay + 1u;
				if (L.final_peer || (!mission && (!L.quit_at || tick != L.quit_at)) ||
					(uint64_t)tick < L.tick || (uint64_t)tick > (uint64_t)L.tick + ahead)
					return Fail("an unexpected final co-op state arrived");
				L.final_peer = true; L.final_peer_hash = hash; L.final_peer_tick = tick; L.final_peer_mission = mission;
				if (!FinalProgress()) return false;
			} else {
				if (L.stage != STOPPING || !L.final_ack_sent || tick != L.final_tick || hash != L.final_hash)
					return Fail("a co-op final acknowledgement arrived before matching states");
				if (type == MSG_FINAL_ACK) {
					if (L.final_ack_received) return Fail("the co-op final acknowledgement arrived twice");
					L.final_ack_received = true;
					if (!FinalProgress()) return false;
				} else if (type == MSG_FINAL_RELEASE) {
					if (L.host || !L.final_ack_received || L.final_receipt_sent)
						return Fail("an unexpected co-op final release arrived");
					if (!SendFinal(MSG_FINAL_RECEIPT, L.final_hash)) return false;
					L.final_receipt_sent = true;
				} else if (type == MSG_FINAL_RECEIPT) {
					if (!L.host || !L.final_release_sent || !L.final_ack_received || L.final_done_sent || L.final_confirmed)
						return Fail("an unexpected co-op final receipt arrived");
					// Queueing DONE is not proof of delivery. Keep the host polling until the guest acknowledges it.
					if (!SendFinal(MSG_FINAL_DONE, L.final_hash)) return false;
					L.final_done_sent = true;
				} else if (type == MSG_FINAL_DONE) {
					if (L.host || !L.final_receipt_sent || !L.final_ack_received || L.final_done_received || L.final_confirmed)
						return Fail("an unexpected co-op final completion arrived");
					if (!SendFinal(MSG_FINAL_DONE_ACK, L.final_hash)) return false;
					L.final_done_received = true;
					// A mission can continue locally; if this ACK is lost after departure the host still reports incomplete.
					// A synthetic test waits for the host's clean departure.
					if (L.final_mission) FinalComplete();
				} else {
					if (!L.host || !L.final_done_sent || L.final_confirmed)
						return Fail("an unexpected co-op completion acknowledgement arrived");
					FinalComplete();
				}
			}
		} else if (type == MSG_DESYNC) {
			uint64_t theirs = CE_NetReadU64(&r), mine = CE_NetReadU64(&r);
			char text[160];
			snprintf(text, sizeof text, "the two games went out of step at tick %u (this game %016llx, the other %016llx)", tick,
				(unsigned long long)mine, (unsigned long long)theirs);
			return Fail(text);
		} else return Fail("an unknown message arrived from the other player");
	}
}

#if defined(CE_NETWORK_TEXT)
void ChatPump() {

    if (!L.net) { if (chat_session) ChatRetire(); return; }
    CE_NetStatus status{}; CE_NetGetStatus(L.net, &status);
    if (L.stage != RUNNING || status.phase != CE_NET_ACTIVE) {
        chat.SetSuspended(true); return;
    }
    const uint64_t now = CE_NetNowMs();
    if (chat_session != L.net) {
        ChatRetire();
        if (chat_generation == (std::numeric_limits<uint64_t>::max)()) {
            Fail("Text chat session counter exhausted"); return;
        }
        const CeTextChat::Binding binding{++chat_generation, status.epoch,
                                          status.local_player, CeTextChat::Schema};
        if (chat.Open(binding, now) != CeTextChat::Result::Ok) {
            Fail("Text chat session could not be admitted"); return;
        }
        chat_session = L.net; chat_epoch = status.epoch;
    }
    if (chat_epoch != status.epoch) {
        if (chat.BeginEpoch(chat_generation, status.epoch, now) != CeTextChat::Result::Ok) {
            Fail("Text chat epoch could not be admitted"); return;
        }
        chat_epoch = status.epoch;
    }
    chat.SetSuspended(false);
    uint8_t bytes[CE_NET_TEXT_MAX_FRAME]{}; uint32_t epoch = 0, size = 0;
    for (unsigned i = 0; i != CE_NET_TEXT_RECEIVE_CAPACITY; ++i) {
        const uint32_t received = CE_NetTextReceive(L.net, &epoch, bytes, sizeof(bytes), &size);
        if (received == CE_NET_TEXT_EMPTY || received == CE_NET_TEXT_CLOSED) break;
        if (received != CE_NET_TEXT_OK) {
            std::memset(bytes, 0, sizeof(bytes));
            Fail("Text chat receive ownership failed"); return;
        }
        const auto accepted = chat.Receive(chat_generation, epoch, bytes, size, now);
        std::memset(bytes, 0, sizeof(bytes));
        if (chat.State().disconnect_required) {
            Fail(accepted == CeTextChat::Result::UpdateRequired ?
                 "Text chat peer requires a compatible update" : "Invalid authenticated text chat data");
            return;
        }
    }
    std::memset(bytes, 0, sizeof(bytes));
    CeTextChat::Outgoing outgoing{};
    if (chat.PeekOutgoing(outgoing) == CeTextChat::Result::Ok) {
        const uint32_t sent = CE_NetTextSend(L.net, outgoing.binding.epoch,
                                            outgoing.frame.bytes, outgoing.frame.size, now);
        if (sent == CE_NET_TEXT_OK) {
            const auto committed = chat.CommitOutgoing(outgoing.binding, outgoing.sequence);
            CeTextChat::Clear(outgoing);
            if (committed != CeTextChat::Result::Ok)
                Fail("Text chat send ownership failed");
        } else {
            CeTextChat::Clear(outgoing);
            if (sent != CE_NET_TEXT_WOULD_BLOCK && sent != CE_NET_TEXT_RATE_LIMITED &&
                sent != CE_NET_TEXT_CLOSED)
                Fail("Local text chat configuration failed");
        }
    }
    CeTextChat::Clear(outgoing);

}
#endif
bool Poll() {
	if (!AdmissionCurrent()) return false;
	if (L.stage == FINISHED) {	// the level is over: the connection only reports that the other player has gone
		if (!L.net) return true;
		CE_NetPoll(L.net, CE_NetNowMs(), 64);
		CE_NetStatus st; CE_NetGetStatus(L.net, &st);
		if (!Receive()) return false; // never discard a delayed mismatch or malformed control after mission confirmation
		if (st.phase == CE_NET_CLOSED || st.phase == CE_NET_FAILED) {
			fprintf(stderr, "onslaught-ce: the other player has left the finished co-op level\n");
			CE_NetSession *net = L.net; L.net = NULL; (ChatRetire(), CE_NetDestroy(net));
		}
		return true;
	}
	if (!L.net) return false;
	CE_NetPoll(L.net, CE_NetNowMs(), 64);
	CE_NetStatus status; CE_NetGetStatus(L.net, &status);
	if (status.phase == CE_NET_FAILED && CE_NetGameProblem(L.net)[0]) return Fail(CE_NetGameProblem(L.net));
	if (status.phase == CE_NET_CLOSED && status.error == CE_NET_PEER_LEFT) {
		if (!Receive() || L.stage == FAILED) return false;
		if (L.stage == STARTING) return Fail("the other player left before confirming the co-op start");
		if (L.stage == FINISHED && L.final_mission && L.final_confirmed) {
			fprintf(stderr, "onslaught-ce: the other player has left the finished co-op level\n");
			CE_NetSession *net = L.net; L.net = NULL; (ChatRetire(), CE_NetDestroy(net));
			return true;
		}
		if (L.stage == STOPPING && !L.final_mission && !L.host && L.final_done_received && L.final_receipt_sent && L.final_ack_received) {
			FinalComplete(); L.stage = ENDED; return false;
		}
		if (L.quit_at && !L.final_confirmed) return Fail("the other player left before final co-op confirmation");
		if ((L.stage == STOPPING || L.final_peer) && !L.final_confirmed)
			return Fail("the other player left before final mission confirmation");
		if (CE_BootLockstep()) return Fail("the other player left the co-op game");	// a shipped game: back to the front end, where the lobby tells the player
		fprintf(stderr, "onslaught-ce: the other player left the co-op game\n");
		L.stage = ENDED; if (!CE_SessionGameQuitCode()) CE_SessionGameSetQuit(QT_QUIT_TO_SYSTEM);
		return false;
	}
	if (status.phase == CE_NET_FAILED && L.stage == STARTING && !L.connection_established && !L.host && status.error != CE_NET_AUTH &&
		status.error != CE_NET_INCOMPATIBLE && !CE_NetGameIceRequested()) return true;	// direct joining peers may retry before start
	if (status.phase == CE_NET_FAILED || status.phase == CE_NET_CLOSED) {
		if (!Receive() || L.stage == FAILED) return false;	// a last message (a desync verdict) names the reason
		char peer[CE_NET_VERSION_BYTES] = {};
		if (status.error == CE_NET_INCOMPATIBLE) {
			// The host refused this peer's hello: its game files, its Onslaught CE build or the level differ. Two labels that differ are named; equal labels are not
			// ("theirs" would read as nonsense), the cause is then in the files or the build.
			char text[200]; CE_NetPeerVersion(L.net, peer, sizeof peer);
			if (peer[0] && strcmp(peer, L.config.version))
				snprintf(text, sizeof text, "the other player has a different Onslaught CE version (yours %s, theirs %s)", L.config.version, peer);
			else
				snprintf(text, sizeof text, "the other player's game files, Onslaught CE build or level differ from yours (both say %s)", L.config.version);
			return Fail(text);
		}
		// A connection that never came up (a wrong invite, no host) is told as the wait tells it, whichever gives up first, the transport's handshake or the wait's own deadline.
		if (!L.connection_established && L.stage == STARTING && connect_why[0]) return Fail(connect_why);
		return Fail(status.error == CE_NET_TIMEOUT ? "the other player stopped responding" : "the connection to the other player failed");
	}
	const bool chat_poll_result = (status.phase != CE_NET_ACTIVE || Receive());
#if defined(CE_NETWORK_TEXT)
    if (chat_poll_result) ChatPump();
    return chat_poll_result && L.stage != FAILED;
#else
    return chat_poll_result;
#endif

}
template<class Ready> bool Block(Ready ready, uint64_t limit, const char *what) {
	uint64_t start = CE_NetNowMs();
	for (;;) {
		if (!AdmissionCurrent()) return false;
		if (ready()) return AdmissionCurrent(); // a ready barrier can skip Poll entirely
		if (CE_SessionGameQuitCode() || !Poll() || L.stage == FAILED || L.stage == ENDED) return false;
		if (CE_NetNowMs() - start > limit) return Fail(what);
		if (CE_SessionGamePump() || CE_SessionGameQuitCode()) return false;
		CE_SessionGameIdle();
	}
}
void TraceRand(void *caller) { if (L.dump) fprintf(L.dump, "r %u %p\n", L.tick, caller); }
unsigned short ControlWord() { unsigned short cw; __asm__ volatile("fnstcw %0" : "=m"(cw)); return cw; }
void SetControlWord(unsigned short cw) { __asm__ volatile("fldcw %0" : : "m"(cw)); }
const unsigned short kControlWord = 0x007f;	// the x87 control word the port's float maths needs: 24-bit precision, round to nearest, all exceptions masked
unsigned short tick_cw;	// the x87 control word as this tick's update began (test trace)
#ifdef CE_NET_TEST_AUTOMATION
// Test: the dead stack below the caller is filled with one byte, so the update's frames start from it instead of what the last frames left (what the draw loop did
// since the previous tick, which differs between two peers); StackProbe reads a local no one wrote, from a frame that lands in that fill.
__attribute__((noinline)) void PoisonStack(unsigned char byte) {
	volatile unsigned char fill[96 * 1024];
	memset((void *)fill, byte, sizeof fill);
	__asm__ volatile("" : : "r"(fill) : "memory");
}
// (its address escapes, or the compiler shrinks the array to the one byte read and the byte lands outside the fill)
__attribute__((noinline)) unsigned StackProbe(void) {
	volatile unsigned char never_written[1024];
	__asm__ volatile("" : : "r"(never_written) : "memory");
	return never_written[512];
}
#endif
bool SaveRandom() {
	if (!CE_WorldLockstepEndTick()) return Fail("the co-op tick fence could not keep the drawn state");
	PortCrtRandState(&L.crt_calls, &L.crt_hold);
	CE_WorldLockstepRandom(1, &L.rng_value, &L.rng_seed);
	return true;
}
void RestoreRandom() {
	PortCrtRandSet(L.crt_calls, L.crt_hold);
	CE_WorldLockstepRandom(0, &L.rng_value, &L.rng_seed);
	CE_WorldLockstepBeginTick();
}
}

extern "C" {
// The process was started for a lockstep session: from the lobby's handoff, which the boot admitted for a co-op level, or, in a test-automation build, from the BEA_LOCKSTEP_*
// variables. WinMain runs the boot right after the command line and before the game's memory manager exists, so the answer is settled before the manager's first allocation
// (patch 0156 zeroes every allocation of a co-op process from the first); the boot's flag is read at every call because the boot sets it, not this module.
int CE_LockstepRequested(void) {
	if (CE_BootLockstep()) return 1;
#ifdef CE_NET_TEST_AUTOMATION
	if (!requested_checked) {
		requested_checked = true; requested = Env("BEA_LOCKSTEP_HOST") || Env("BEA_LOCKSTEP_JOIN");
#ifndef _WIN32
		// A test pair that perturbs the allocator (MALLOC_PERTURB_) or poisons the stack is there to expose what the retail code reads without writing: the crash it
		// provokes is a finding, not a core to keep. Such a process is made non-dumpable (execve resets the flag, so it is set here, not by the launcher).
		if (requested && (getenv("MALLOC_PERTURB_") || Env("BEA_LOCKSTEP_POISON_STACK"))) prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
	}
	return requested;
#else
	return 0;
#endif
}
int CE_LockstepActive(void) { return L.shown; }
int CE_LockstepTicking(void) { return L.stage == RUNNING; }
int CE_LockstepSharing(void) { return L.stage == CONNECTING || L.stage == LOADED || L.stage == STARTING || L.stage == PRERUN || L.stage == RUNNING || L.stage == STOPPING; }
uint32_t CE_LockstepLocalPlayer(void) { return L.host ? 0u : 1u; }
int CE_LockstepConfigureLevel(uint32_t level) {
	if (L.stage != OFF) return Fail("a co-op session can run one level per process");
	// A session started from the lobby (the shipped route) comes with the process's admission: the roles, endpoint and secret in the BEA_NET_* variables the lobby's handoff set,
	// and the digest of the content the admission read. A test-automation build can instead be driven from BEA_LOCKSTEP_* variables with no admission.
	const bool admitted = CE_BootLockstep() != 0;
#ifdef CE_NET_TEST_AUTOMATION
	const char *host_name = admitted ? "BEA_NET_HOST" : "BEA_LOCKSTEP_HOST", *join_name = admitted ? "BEA_NET_JOIN" : "BEA_LOCKSTEP_JOIN", *secret_name = admitted ? "BEA_NET_SECRET" : "BEA_LOCKSTEP_SECRET";
#else
	const char *host_name = "BEA_NET_HOST", *join_name = "BEA_NET_JOIN", *secret_name = "BEA_NET_SECRET";
#endif
	bool host = Env(host_name), join = Env(join_name);
	if (host == join) return Fail("select exactly one co-op role");
	if (!CE_CompatibilityLockstepLevel(level)) return Fail("lockstep co-op admits the defence levels 856 and 858 and the tutorial (100)");
	const char *disabled[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_RECORD", "BEA_PORT_PROBES", "BEA_RECORD", "BEA_NET_HOST", "BEA_NET_JOIN"};
	for (const char *name : disabled) {
		if (admitted && (!strcmp(name, "BEA_NET_HOST") || !strcmp(name, "BEA_NET_JOIN"))) continue;	// the lobby's own roles
		if (Env(name)) return Fail("co-op is unavailable during replay, recording, probes or another network mode");
	}
	L = Lockstep(); L.stage = CONNECTING; L.host = host; L.level = level; L.admission_required = admitted;
#ifdef CE_NET_TEST_AUTOMATION
	L.delay = EnvU("BEA_LOCKSTEP_DELAY", 3); L.hash_every = EnvU("BEA_LOCKSTEP_HASH_EVERY", 20); L.skew_at = EnvU("BEA_LOCKSTEP_SKEW_AT", 0); L.quit_at = EnvU("BEA_LOCKSTEP_QUIT_AT", 0); L.stall_every = EnvU("BEA_LOCKSTEP_STALL_EVERY", 0); L.stall_at = EnvU("BEA_LOCKSTEP_STALL_AT", 0); L.stall_from = EnvU("BEA_LOCKSTEP_STALL_FROM", 0); L.flip_cw_at = EnvU("BEA_LOCKSTEP_FLIP_CW_AT", 0); L.stall_ms = EnvU("BEA_LOCKSTEP_STALL_MS", 300);
	L.poison_stack = Env("BEA_LOCKSTEP_POISON_STACK") ? (int)(EnvU("BEA_LOCKSTEP_POISON_STACK", 0) & 255) : -1;
	L.test_uninit_read = Env("BEA_LOCKSTEP_TEST_UNINIT_READ");
#endif
	if (L.delay < 1 || L.delay > 16 || L.hash_every < 1 || L.hash_every > 600) return Fail("co-op delay or hash spacing is out of range");
	CE_NetConfig &config = L.config;
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
	const int native_secret = CE_LobbySignalingGameSecret(host ? CE_NET_HOST : CE_NET_GUEST, level, config.secret);
    if (native_secret < 0) return Fail("Native relay game authority is unavailable; leave and choose a new invitation.");
	if (!Endpoint(getenv(host ? host_name : join_name), L.ip, config.port) || !(native_secret > 0 || (native_secret == 0 && Hex(getenv(secret_name), config.secret))))
#else
	if (!Endpoint(getenv(host ? host_name : join_name), L.ip, config.port) || !Hex(getenv(secret_name), config.secret))
#endif
#else
	if (!Endpoint(getenv(host ? host_name : join_name), L.ip, config.port) || !Hex(getenv(secret_name), config.secret))
#endif
		return Fail("co-op address or private invite is missing or invalid");
	if (admitted) {
		CE_CompatibilityResult admission = {}; char why[160] = {};
		if (CE_BootPrepared(&admission, why, sizeof why) != 1) {
			char text[200]; snprintf(text, sizeof text, "co-op content admission unavailable: %.140s", why); return Fail(text);
		}
		memcpy(config.compatibility, admission.digest, sizeof config.compatibility);
		snprintf(config.version, sizeof config.version, "%s", kVersionLabel);
	} else {
#ifdef CE_NET_TEST_AUTOMATION
		if (!CE_CompatibilityLockstepDigest(level, config.compatibility)) return Fail("this build has no simulation identity for co-op");
		snprintf(config.version, sizeof config.version, "lockstep prototype");
#else
		return Fail("co-op needs the lobby's admission");
#endif
	}
	config.role = host ? CE_NET_HOST : CE_NET_GUEST; config.ipv4 = L.ip;
	config.handshake_ms = (uint32_t)kStartMs; config.silence_ms = (uint32_t)kGiveUpMs;
#ifdef CE_NET_TEST_AUTOMATION
	if (const char *path = getenv("BEA_LOCKSTEP_DUMP")) L.dump = fopen(path, "w");
#endif
	// Both peers load and pre-run the level from the same C runtime state, whatever this process drew before (its
	// front end, its sound start-up, a sound device that was or was not there).
	PortCrtRandSet(0, 1);
#ifdef CE_NET_TEST_AUTOMATION
	fprintf(stderr, "onslaught-ce: lockstep co-op prototype, level %u, %s, input delay %u ticks, hash every %u ticks\n",
		level, host ? "host (player 1)" : "joining (player 2)", L.delay, L.hash_every);
#else
	fprintf(stderr, "onslaught-ce: co-op (experimental), level %u, %s, input delay %u ticks, hash every %u ticks\n",
		level, host ? "host (player 1)" : "joining (player 2)", L.delay, L.hash_every);
#endif
	if (!CampaignConfigure()) return 0;
    return 1;
}
int CE_LockstepAfterLoad(void) {
	if (L.stage != CONNECTING) return 0;
	if (!AdmissionCurrent()) return 0;
	if (CE_SessionGameQuitCode()) return 0;
	if (GAME.GetNPlayers() != 2 || !GAME.GetPlayer(0) || !GAME.GetPlayer(1)) return Fail("the level did not load two players");
	L.stage = LOADED; return 1;
}
int CE_LockstepPreparePlay(void) {
	if (L.stage != LOADED) return 0;
	if (!AdmissionCurrent()) return 0;
	// Compare script-visible flags before pre-run. Only their digest is exchanged; no save file is sent.
	if (!CapturePreRunSlots()) return Fail("co-op pre-run flags could not be captured");
	unsigned pre_calls=0,pre_hold=0; int pre_value=0,pre_seed=0;
	PortCrtRandState(&pre_calls,&pre_hold); CE_WorldLockstepRandom(1,&pre_value,&pre_seed);
	// Block re-entrant updates while the message pump connects the peers and compares their initial state.
	L.stage = STARTING;
	// The connection opens once this peer has loaded: nothing polls it while a level loads. The two peers can finish
	// loading far apart (a slow disk, a software renderer), so a joining peer whose attempt gave up tries again until
	// the start deadline; the invite secret is wiped once connected.
	L.net = CE_NetCreateGame(&L.config);
	if (!L.net) return Fail(CE_NetGameProblem(NULL)[0] ? CE_NetGameProblem(NULL) : "the co-op connection could not be created");
	// What a player is told when nobody connects. A wrong invite secret looks like an absent peer: the host answers the guest's hello, the guest cannot verify the answer (it holds
	// another secret) and refuses it, and the host never receives the guest's proof, so the host refuses nothing and only the guest has anything to say. The guest's text
	// therefore names the likely causes; the host adds a count only when it did refuse packets (a stranger's, damaged, or sealed with another secret).
	snprintf(connect_why, sizeof connect_why, "%s", L.host ? "the other player did not connect in time" :
		"the other player did not connect in time (no answer from the host: check the invite code and that the host is waiting)");
	if (!Block([] {
			CE_NetStatus st; CE_NetGetStatus(L.net, &st);
			if (L.host && st.rejected_packets)
				snprintf(connect_why, sizeof connect_why, "the other player did not connect in time (%u packets were refused: check the invite code)", (unsigned)st.rejected_packets);
			if (L.host && st.error == CE_NET_INCOMPATIBLE)	// the transport refused a hello from a game that differs (files, build or level): it is told to that player, and here
				snprintf(connect_why, sizeof connect_why, "the other player did not connect in time (a game whose files, Onslaught CE build or level differ tried to join)");
			if (st.phase == CE_NET_FAILED && !L.host && st.error != CE_NET_AUTH && st.error != CE_NET_INCOMPATIBLE && !CE_NetGameIceRequested()) {
				(ChatRetire(), CE_NetDestroy(L.net)); L.net = CE_NetCreateGame(&L.config);
				if (!L.net) { Fail(CE_NetGameProblem(NULL)[0] ? CE_NetGameProblem(NULL) : "the co-op connection could not be created"); return false; }
				return false;
			}
			return st.phase == CE_NET_ACTIVE;
		}, kStartMs, connect_why)) return 0;
	L.connection_established = true;
    if (L.campaign && ce_network_campaign_begin(&L.campaign_state, L.campaign_identity) != CE_CAMPAIGN_ACCEPTED)
        return Fail("The campaign lifecycle could not begin.");

	{ volatile uint8_t *secret = L.config.secret; for (unsigned i = 0; i < sizeof L.config.secret; ++i) secret[i] = 0; }
	if (!SendReady()) return 0;
	if (!Block([] { return L.peer_ready; }, kStartMs, "the other player did not finish loading in time")) return 0;
	if (!PreRunSlotsUnchanged()) return Fail("local career flags changed while waiting for pre-run admission");
	if (!SendPreRunAck()) return 0;
	L.prerun_ack_sent = true;
	if (!Block([] { return L.prerun_ack_received; }, kStartMs,
		"the other player did not acknowledge pre-run admission in time")) return 0;
	if (!PreRunSlotsUnchanged()) return Fail("local career flags changed while waiting for pre-run admission");
	// Transport and message pumping may consume random values; restore the pre-connection simulation streams.
	PortCrtRandSet(pre_calls,pre_hold); CE_WorldLockstepRandom(0,&pre_value,&pre_seed);
	L.stage = PRERUN;
	CE_SessionGamePreRun();
	if (CE_SessionGameQuitCode()) return 0;
	if (!AdmissionCurrent()) return 0;
	L.prerun_finished = true; L.stage = STARTING;
	if (!SaveRandom()) return 0;
	uint64_t start = StateHash(0);
	if (L.stage == FAILED) return 0;
	HashSlot &h = L.hashes[0]; if (h.tick != 0) h = HashSlot{0, false, false, 0, 0};
	h.local = true; h.mine = start;
	if (!SendHash(0, start) || !Compare(h)) return 0;
	// Compare is performed exactly once by whichever side of this slot arrives second. Wait for both before play.
	if (!Block([] { return L.hashes[0].local && L.hashes[0].remote; }, kStartMs,
		"the other player did not confirm the co-op start in time")) return 0;
	// Each conforming peer sends this after comparing both selected start hashes. The acknowledgement indicates that
	// comparison; it does not prove delivery of our acknowledgement, complete state equality or consensus under loss.
	if (!SendStartAck(start)) return 0;
	if (!Block([] { return L.start_ack_received; }, kStartMs,
		"the other player did not acknowledge the co-op start in time")) return 0;
	if (!AdmissionCurrent()) return 0;
	L.stage = RUNNING; L.tick = 0; L.waiting_since = 0; L.shown = true;
	CE_SessionGameRebaseRenderClock();
	fprintf(stderr, "onslaught-ce: co-op started (you are player %d); start state %016llx\n", L.host ? 1 : 2, (unsigned long long)start);
	return 1;
}
#ifdef CE_NET_TEST_AUTOMATION
// Test (BEA_LOCKSTEP_POISON_STACK): the port's hook, first in every CGame::Update (updatedump.cpp), fills the stack below it with one byte, so the update's own callees
// start from it instead of what the draw loop and the last frames left there; the first time, it says what a local no one wrote reads from a frame at that depth.
void CE_LockstepUpdateEntry(void) {
	if (L.poison_stack < 0 || !CE_LockstepSharing()) return;
	PoisonStack((unsigned char)L.poison_stack);
	static bool probed;
	if (!probed) { probed = true; unsigned seen = StackProbe(); fprintf(stderr, "onslaught-ce: co-op test: stack poison %02x, a local no one wrote reads %02x in the update's first frame\n", (unsigned)L.poison_stack, seen); }
	// The deliberate bug: a decision made on a local no one wrote. Two peers poisoned alike decide alike; poisoned differently, one of them draws a value the other does not,
	// and the generator, so every hash after it, differs.
	if (L.test_uninit_read && StackProbe() >= 0x80) (void)GAME.Random();
}
#endif
int CE_LockstepBeforeUpdate(void) {
	if (!AdmissionCurrent()) return 0;
	if (L.stage == STARTING || L.stage == LOADED) return 0; // no simulation update may re-enter a waiting stage
	if (L.stage == STOPPING) {
		if (CE_SessionGameQuitCode()) return 0;
		if (!Poll()) return 0;
		if (L.stage == STOPPING && CE_NetNowMs() - L.final_started >= kGiveUpMs) return Fail("the final co-op confirmation timed out");
		return 0;
	}
	if (L.stage == CONNECTING || L.stage == PRERUN) return 1;	// loading and the admitted pre-run
	if (L.stage == FINISHED) return Poll() ? 1 : 0;	// each peer's own end screen; stop if a late message fails
	if (L.stage != RUNNING) return 0;
	if (CE_SessionGameQuitCode()) return 0;
	if (!Poll()) return 0;
	const uint32_t t = L.tick;
	bool have = t < L.delay || (L.theirs[t % kRing].present && L.theirs[t % kRing].tick == t);
	if (!have) {
		uint64_t now = CE_NetNowMs();
		if (!L.waiting_since) L.waiting_since = now;
		uint64_t waited = now - L.waiting_since;
		if (waited >= kGiveUpMs) { Fail("the other player stopped sending controls"); return 0; }
		if (waited >= kWaitNoticeMs) {
			uint32_t left = (uint32_t)((kGiveUpMs - waited + 999) / 1000);
			bool first = !L.notice[0];
			snprintf(L.notice, sizeof L.notice, "Waiting for the other player... (%u s)", left);
			if (first) fprintf(stderr, "onslaught-ce: waiting for the other player at tick %u\n", t);
		}
		return 0;
	}
	if (L.notice[0]) { fprintf(stderr, "onslaught-ce: the other player caught up\n"); L.notice[0] = 0; }
	L.waiting_since = 0;
	RestoreRandom();
	L.applied[0] = L.applied[1] = false; L.sampled = false; L.ran = true;
	// A library call or an exception path between ticks can leave the x87 control word at a wider precision, which changes every product the tick computes without
	// touching a generator: each tick starts from the word the port's maths needs, and says so (the first few times) when it was not that.
	tick_cw = ControlWord();
	if (tick_cw != kControlWord) {
		static unsigned reported; if (reported++ < 8) fprintf(stderr, "onslaught-ce: co-op: the x87 control word was %04x at the start of tick %u (set to %04x)\n", tick_cw, L.tick, kControlWord);
#ifdef CE_NET_TEST_AUTOMATION
		static const bool unfenced = Env("BEA_LOCKSTEP_NO_CW_FENCE");	// test: the negative control of BEA_LOCKSTEP_FLIP_CW_AT
		if (!unfenced)
#endif
			SetControlWord(kControlWord);
	}
	if (L.dump) gPortCrtTrace = TraceRand;
	return 1;
}
void CE_LockstepAfterUpdate(void) {
	if (L.stage != RUNNING || !L.ran) return;
	L.ran = false; gPortCrtTrace = NULL;
	if (!AdmissionLive()) return;
	if (L.flip_cw_at && L.tick + 1 == L.flip_cw_at) SetControlWord(0x027f);	// test: a wider precision left behind between two ticks, as a library call could
	if (L.dump) { unsigned c, h; PortCrtRandState(&c, &h); fprintf(L.dump, "c %u %u %08x cw %04x %04x\n", L.tick, c, h, tick_cw, ControlWord()); }
	const uint32_t t = L.tick;
	if (!L.sampled) {	// no controller flush this tick (a paused or finished level): an empty sample keeps the stream whole
		CE_WorldInput empty = {}; Slot &s = L.mine[(t + L.delay) % kRing]; s.tick = t + L.delay; s.present = true; s.input = empty;
		if (!SendInput(empty, t + L.delay)) return;
	}
	L.theirs[t % kRing].present = false; L.mine[t % kRing].present = false;
	if (!SaveRandom()) return;
	L.tick = t + 1;
	uint64_t final_hash = 0;
	if (L.tick % L.hash_every == 0) {
		uint64_t hash = StateHash(L.tick);
		if (L.stage == FAILED) return;
		final_hash = hash; // reuse this exact pre-presentation hash if either end occurs on the scheduled tick
		HashSlot &h = L.hashes[(L.tick / L.hash_every) % kRing];
		if (h.tick != L.tick) {
			if (h.local != h.remote) { Fail("a local state hash would replace an unmatched comparison"); return; }
			h = HashSlot{L.tick, false, false, 0, 0};
		}
		h.local = true; h.mine = hash;
		if (!SendHash(L.tick, hash) || !Compare(h)) return;
		if (L.tick % 1000 == 0) fprintf(stderr, "onslaught-ce: co-op tick %u in step (%u hashes compared)\n", L.tick, L.hashes_compared);
	}
	bool natural_end = GAME.GetGameState() > GAME_STATE_PLAYING && GAME.GetGameState() < GAME_STATE_QUIT;
	if (L.final_peer && L.final_peer_mission && L.tick >= L.final_peer_tick && !natural_end) {
		Fail("the other player ended the mission at a different simulation state"); return;
	}
	// Both kinds of end use the same pre-presentation snapshot as scheduled hashes, even off-interval.
	if ((natural_end || L.quit_at == L.tick) && L.tick % L.hash_every) final_hash = StateHash(L.tick);
	if (L.stage == FAILED) return;
	if ((L.stall_every && L.tick % L.stall_every == 0 && L.tick >= L.stall_from) || (L.stall_at && L.tick == L.stall_at)) {	// test: this peer stalls (300 ms unless
		// BEA_LOCKSTEP_STALL_MS says otherwise), as a slow frame, a disk or a starved machine would; the other peer keeps drawing meanwhile
		uint64_t until = CE_NetNowMs() + L.stall_ms; while (CE_NetNowMs() < until) {}
	}
	if (deferred_count) {	// after the snapshot and the hash: what placing touches is put back at the next tick's start
		placing = true; CE_WorldLockstepPlaceEmitters(deferred_weapons, deferred_orientate, deferred_count); placing = false;
		deferred_count = 0;
	}
	if (natural_end) {
		// A naturally ended mission needs its own final state proof, even between scheduled hashes. Further game
		// updates wait until both peers acknowledge the same selected state and ending tick.
		L.final_tick = L.tick; L.final_mission = true; L.final_hash = final_hash; L.final_local = true;
		L.stage = STOPPING; L.final_started = CE_NetNowMs();
		if (SendFinal(MSG_MISSION_FINAL, final_hash)) (CampaignTerminal() && FinalProgress());
		return;
	}
	if (L.quit_at && L.tick == L.quit_at && !CE_SessionGameQuitCode()) {
		// Do not close on a queued final hash: wait for both comparisons and an application-level receipt.
		L.final_tick = L.tick; L.final_mission = false; L.final_hash = final_hash; L.final_local = true;
		L.stage = STOPPING; L.final_started = CE_NetNowMs();
		if (SendFinal(MSG_FINAL, final_hash)) (CampaignTerminal() && FinalProgress());
	}
}
void CE_LockstepBeforeRender(void) {
	if (L.stage == RUNNING || L.stage == STOPPING) Poll();
}
int CE_LockstepBeginTeardown(int quit) {
    CampaignCaptureReturn();
    if (L.campaign && !L.final_confirmed) CampaignStop(CE_CAMPAIGN_STOP_CANCEL);

	if (quit == QT_RESTART_LEVEL && L.stage != OFF) {	// a retry would restart this peer alone
		fprintf(stderr, "onslaught-ce: retrying a co-op level is not available yet; returning to the menu\n");
		quit = QT_QUIT_TO_FRONTEND;
	}
	if (L.stage == FINISHED) {
		if (!L.final_confirmed) { fprintf(stderr, "onslaught-ce: co-op mission ended without final confirmation\n"); L.outcome_error = true; }
		ReportNetworkCounts(L.net);
		if (L.net) (ChatRetire(), CE_NetClose(L.net)); L.stage = ENDED; return quit;
	}
	bool incomplete = !L.final_confirmed && (L.stage == STOPPING || (L.stage == RUNNING && (L.quit_at || L.final_peer)));
	if (incomplete) {
		fprintf(stderr, "onslaught-ce: co-op stopped before final confirmation\n"); L.outcome_error = true;
	}
	if (L.stage == RUNNING || L.stage == STOPPING || L.stage == PRERUN || L.stage == STARTING || L.stage == LOADED || L.stage == CONNECTING) {
		if (L.final_confirmed)
			fprintf(stderr, "onslaught-ce: co-op ended at tick %u, %u state hashes compared, all equal\n", L.tick, L.hashes_compared);
		else if (!incomplete)
			fprintf(stderr, "onslaught-ce: co-op ended at tick %u; final state was not confirmed\n", L.tick);
		ReportNetworkCounts(L.net);
		if (L.net) (ChatRetire(), CE_NetClose(L.net));
		L.stage = ENDED;
	}
	return quit;
}
void CE_LockstepAfterWorldShutdown(void) {
    CampaignCaptureReturn();
	L.shown = false;
	ReportNetworkCounts(L.net);
	(ChatRetire(), CE_NetDestroy(L.net)); L.net = NULL;
	if (L.dump) { fclose(L.dump); L.dump = NULL; }

    CE_BootCampaignClear();
    L.campaign = false;
}
const char *CE_LockstepNotice(void) { return L.notice; }
const char *CE_LockstepError(void) { return last_error; }
int CE_LockstepExitCode(int native_code, int native_quit_seen) {
	// A native WM_QUIT can arrive while a terminal error is being reported. Keep a native nonzero code, but never
	// allow native zero to turn a failed co-op session into a successful process result.
	if (L.outcome_error) return native_quit_seen && native_code ? native_code : 1;
	if (native_quit_seen) return native_code;
	return L.final_confirmed ? 0 : native_code;
}
int CE_LockstepControllerFlush(uint32_t controller) {
	if (L.stage == FINISHED) return 0;	// the end screen's menu takes this peer's own controls natively
	if (L.stage != RUNNING || controller > 1) return 1;
	if (!AdmissionLive()) return 1;
	const uint32_t t = L.tick, local = L.host ? 0 : 1;
	if (controller == 0 && !L.sampled) {
		L.sampled = true;
		Slot &s = L.mine[(t + L.delay) % kRing];
		CE_WorldInput sample = {}; CE_WorldLockstepSample(local, &sample);

#if defined(CE_NETWORK_TEXT)
    if (PortCeChatCapturingInput()) std::memset(&sample, 0, sizeof(sample));
#endif
		if (!CE_WorldLockstepValidInput(&sample)) { Fail("the local co-op control sample was invalid"); return 1; }
		s.input = sample; s.tick = t + L.delay; s.present = true;
		if (!SendInput(s.input, t + L.delay)) return 1;
	}
	// player p's input for tick t: the joining player's own and the host's own both travel; the first ticks are empty
	const uint32_t p = controller;
	if (L.applied[p]) return 1;
	L.applied[p] = true;
	CE_WorldInput in = {};
	if (t >= L.delay) {
		uint32_t source = t;
		if (p == local && L.skew_at && t >= L.skew_at) source = t - 1;	// test mutant: our own input one tick late
		Slot &s = p == local ? L.mine[source % kRing] : L.theirs[t % kRing];
		if (s.tick != source) { Fail("a control sample is missing at its tick"); return 1; }
		in = s.input;
	}
	L.current[p] = in;
	CE_WorldLockstepApply(p, &in);
	return 1;
}
int CE_LockstepCaptureAction(int, float, int) { return 1; }
// Ogg Vorbis length: the last page's granule position (samples) over the identification header's sample rate.
static float voice_seconds;
static void VoiceLength(const uint8_t *b, int size) {
	if (!b || size < 64) return;
	uint32_t rate = 0;
	for (int i = 0; i + 16 <= size && i < 4096; ++i)
		if (b[i] == 1 && !memcmp(b + i + 1, "vorbis", 6)) { rate = b[i + 12] | b[i + 13] << 8 | b[i + 14] << 16 | (uint32_t)b[i + 15] << 24; break; }
	if (!rate || rate > 200000) return;
	for (int i = size - 27; i >= 0; --i)
		if (!memcmp(b + i, "OggS", 4)) {
			uint64_t granule = 0;
			for (int k = 7; k >= 0; --k) granule = granule << 8 | b[i + 6 + k];
			if (granule && granule != ~0ull) voice_seconds = (float)((double)granule / rate);
			return;
		}
}
void CE_LockstepVoiceFile(const char *path) {
	voice_seconds = 0.f;
	if (!path || !CE_LockstepActive()) return;
	FILE *f = GameOpen(path, "rb");
	if (!f) { fprintf(stderr, "onslaught-ce: co-op: no voice file %s; the message ends by its text\n", path); return; }
	static uint8_t buffer[1 << 21];
	int size = (int)fread(buffer, 1, sizeof buffer, f);
	fclose(f);
	if (size == (int)sizeof buffer) { fprintf(stderr, "onslaught-ce: co-op: voice file %s too long; the message ends by its text\n", path); return; }
	VoiceLength(buffer, size);
}
// The outermost hold swaps this peer's own presentation streams in (so a burst of particles still varies) and the
// shared ones back out when it ends; an inner hold changes nothing.
static unsigned hold_depth, own_calls, own_hold = 0x2545f491u; static int own_value = 1, own_seed = 123457;
void CE_LockstepCrtEnter(unsigned *s) {
	if (hold_depth++) return;
	PortCrtRandState(&s[0], &s[1]); CE_WorldLockstepRandom(1, (int *)&s[2], (int *)&s[3]);
	PortCrtRandSet(own_calls, own_hold); CE_WorldLockstepRandom(0, &own_value, &own_seed);
}
void CE_LockstepCrtLeave(const unsigned *s) {
	if (--hold_depth) return;
	PortCrtRandState(&own_calls, &own_hold); CE_WorldLockstepRandom(1, &own_value, &own_seed);
	PortCrtRandSet(s[0], s[1]); int v = (int)s[2], seed = (int)s[3]; CE_WorldLockstepRandom(0, &v, &seed);
}
float CE_LockstepVoiceSeconds(void) { return voice_seconds; }
int CE_LockstepCoopPlayers(void) { return CE_LockstepSharing() && L.level == 100; }
int CE_LockstepDeferEmitter(void *weapon, int orientate) {
	if (L.stage != RUNNING || !L.ran || placing) return 0;
	for (unsigned i = 0; i < deferred_count; ++i)
		if (deferred_weapons[i] == weapon) { deferred_orientate[i] = orientate; return 1; }
	if (deferred_count == 64) return 0;
	deferred_weapons[deferred_count] = weapon; deferred_orientate[deferred_count++] = orientate; return 1;
}
void CE_LockstepPartnerStart(int player_index, void *init) {
	if (player_index == 1 && CE_LockstepCoopPlayers()) CE_WorldLockstepPartnerStart(init);
}
int CE_LockstepSoundRand(void) {
	if (!CE_LockstepSharing()) return rand();
	static unsigned hold = 1;	// the runtime's generator, on this peer's own state
	hold = hold * 214013u + 2531011u;
	return (int)((hold >> 16) & 0x7fff);
}
#ifdef CE_NET_TEST_AUTOMATION
int CE_LockstepTestNoLoss(void) { static int no_loss = Env("BEA_LOCKSTEP_NO_LOSS") ? 1 : 0; return L.stage == RUNNING && no_loss; }
int CE_LockstepTestNoParticles(void) { static int none = Env("BEA_LOCKSTEP_NO_PARTICLES") ? 1 : 0; return CE_LockstepSharing() && none; }
int CE_LockstepTestExtraRender(int rendered) { static uint32_t extra = EnvU("BEA_LOCKSTEP_EXTRA_RENDERS", 0); return CE_LockstepSharing() && extra && rendered < (int)(extra > 200 ? 200 : extra) + 1; }
void CE_LockstepTraceCollision(const float *a, const float *b, float ra, float rb, float speed, float time) {
	if (!L.dump || L.stage != RUNNING || !L.ran) return;
	float v[10] = {a[0], a[1], a[2], b[0], b[1], b[2], ra, rb, speed, time}; uint32_t bits[10]; memcpy(bits, v, sizeof bits);
	fprintf(L.dump, "k %u %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", L.tick, bits[0], bits[1], bits[2], bits[3], bits[4], bits[5], bits[6], bits[7], bits[8], bits[9]);
}
void CE_LockstepTraceSpawn(const float *pos, void *caller) {
	if (!L.dump || L.stage != RUNNING || !L.ran) return;
	uint32_t bits[3]; memcpy(bits, pos, sizeof bits);
	int value = 0, seed = 0; CE_WorldLockstepRandom(1, &value, &seed);	// reads the game generator, changes nothing
	fprintf(L.dump, "x %u %08x %08x %08x %d cw %04x\ny %u %p\n", L.tick, bits[0], bits[1], bits[2], value, ControlWord(), L.tick, caller);
}
void CE_LockstepTraceSurface(const float *vertex, int part, const float *ori9, const float *offset, const float *result) {
	if (!L.dump || L.stage != RUNNING || !L.ran) return;
	uint32_t bits[18]; memcpy(bits, vertex, 12); memcpy(bits + 3, ori9, 36); memcpy(bits + 12, offset, 12); memcpy(bits + 15, result, 12);
	fprintf(L.dump, "s %u %d", L.tick, part);
	for (unsigned i = 0; i < 18; ++i) fprintf(L.dump, " %08x", bits[i]);
	fprintf(L.dump, " cw %04x\n", ControlWord());
}
void CE_LockstepTraceSurfaceInputs(const float *pos3, const float *ori9, float render_frame, int controller) {
	if (!L.dump || L.stage != RUNNING || !L.ran) return;
	float fraction = GAME.GetFrameRenderFraction();
	uint32_t bits[14]; memcpy(bits, pos3, 12); memcpy(bits + 3, ori9, 36); memcpy(bits + 12, &fraction, 4); memcpy(bits + 13, &render_frame, 4);
	fprintf(L.dump, "i %u", L.tick);
	for (unsigned i = 0; i < 14; ++i) fprintf(L.dump, " %08x", bits[i]);
	fprintf(L.dump, " mc %d\n", controller);
}
void CE_LockstepTraceEvent(int event_num, void *listener, float time) {
	if (!L.dump || L.stage != RUNNING || !L.ran) return;
	IListener *to = (IListener *)listener;
	uint32_t bits; memcpy(&bits, &time, 4);
	fprintf(L.dump, "e %u %d %s %08x\n", L.tick, event_num, to ? typeid(*to).name() : "-", bits);
}
#endif
int CE_LockstepInvertPitch(uint32_t player, int grounded, int original) {
	if (player > 1) return original;
	return grounded ? (int)L.current[player].invert_ground : (int)L.current[player].invert_jet;
}
int CE_LockstepWalkerAny(uint32_t player, int original) {
	if (player > 1) return original != 0;
	return (int)L.current[player].walker_mouse_any;
}
}
#ifndef CE_NET_TEST_AUTOMATION
extern "C" {
// The test-only hooks of the patched game code and the port's update hook are empty outside a test-automation build.
void CE_LockstepUpdateEntry(void) {}
void CE_LockstepTraceEvent(int, void *, float) {}
void CE_LockstepTraceCollision(const float *, const float *, float, float, float, float) {}
void CE_LockstepTraceSpawn(const float *, void *) {}
void CE_LockstepTraceSurface(const float *, int, const float *, const float *, const float *) {}
void CE_LockstepTraceSurfaceInputs(const float *, const float *, float, int) {}
int CE_LockstepTestNoLoss(void) { return 0; }
int CE_LockstepTestNoParticles(void) { return 0; }
int CE_LockstepTestExtraRender(int) { return 0; }
}
#endif


void CE_LockstepCampaignOutcome(CeCampaignOutcome outcome) {
    if (!L.campaign || !L.connection_established || L.stage != RUNNING) return;
    if (outcome != CE_CAMPAIGN_OUTCOME_WIN && outcome != CE_CAMPAIGN_OUTCOME_LOSS) {
        Fail("The campaign outcome is not a terminal gameplay result.");
        return;
    }
    CeCampaignAction action = ce_network_campaign_report(&L.campaign_state,
        L.campaign_identity, CampaignRole(), outcome);
    if (action != CE_CAMPAIGN_ACCEPTED && action != CE_CAMPAIGN_DUPLICATE) {
        Fail("The campaign gameplay outcome changed.");
        return;
    }
    L.campaign_outcome = outcome;
}

int CE_LockstepCampaignActive(void) {
    return L.campaign && L.connection_established &&
        L.stage != OFF && L.stage != FAILED && L.stage != ENDED;
}

int CE_LockstepCampaignAgreedWin(void) {
    return L.campaign && L.final_confirmed && !L.outcome_error &&
        L.stage != FAILED && L.campaign_outcome == CE_CAMPAIGN_OUTCOME_WIN &&
        CampaignReady() && ce_network_campaign_agreed_win(&L.campaign_state);
}


#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_TEXT)
extern "C" int CE_LockstepChatAvailable(void) {
    if (L.stage != RUNNING || !L.net || chat_session != L.net || !chat.State().open) return 0;
    CE_NetStatus status{}; CE_NetGetStatus(L.net, &status);
    return status.phase == CE_NET_ACTIVE && CE_NetTextMaxFrame(L.net) > CeTextChat::HeaderBytes;
}
extern "C" int CE_LockstepChatComposing(void) {
    return CE_LockstepChatAvailable() && chat.State().composing;
}
extern "C" int CE_LockstepChatBegin(void) {
    if(!CE_LockstepChatAvailable())return CE_CHAT_CLOSED;
    return ChatResult(chat.BeginCompose());
}
extern "C" uint32_t CE_LockstepChatAppend(const uint8_t *text, uint32_t size) {
    if (!CE_LockstepChatAvailable()) return CE_CHAT_CLOSED;
    return ChatResult(chat.Append(text, size));
}
extern "C" uint32_t CE_LockstepChatBackspace(void) {
    if (!CE_LockstepChatAvailable()) return CE_CHAT_CLOSED;
    return ChatResult(chat.Backspace());
}
extern "C" uint32_t CE_LockstepChatSubmit(void) {
    if (!CE_LockstepChatAvailable()) return CE_CHAT_CLOSED;
    CeTextChat::Decoded draft{};
    const auto copied = chat.CopyDraft(draft);
    if (copied != CeTextChat::Result::Ok) return ChatResult(copied);
    const uint32_t maximum = CE_NetTextMaxFrame(L.net);
    const bool too_large = maximum < CeTextChat::HeaderBytes ||
                          draft.size > maximum - CeTextChat::HeaderBytes;
    CeTextChat::Clear(draft);
    if (too_large) return CE_CHAT_TOO_LARGE;
    return ChatResult(chat.SubmitDraft(CE_NetNowMs()));
}
extern "C" void CE_LockstepChatCancel(void) { chat.CancelCompose(); }
extern "C" void CE_LockstepChatMute(int muted) { chat.SetMuted(muted != 0); }
extern "C" void CE_LockstepChatClear(void) { chat.ClearHistory(); }
extern "C" void CE_LockstepChatCopyView(CE_ChatView *out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    if (!CE_LockstepChatAvailable()) return;
    const auto state = chat.State();
    out->available = 1; out->composing = state.composing; out->muted = state.muted;
    out->queued = static_cast<uint32_t>(state.outgoing);
    const uint32_t maximum = CE_NetTextMaxFrame(L.net);
    out->text_limit = maximum > CeTextChat::HeaderBytes ?
                      (std::min<uint32_t>)(CeTextChat::MaxTextBytes, maximum - CeTextChat::HeaderBytes) : 0;
    CeTextChat::Decoded draft{};
    if (chat.CopyDraft(draft) == CeTextChat::Result::Ok) {
        out->draft_size = draft.size; std::memcpy(out->draft, draft.utf8, draft.size);
    }
    CeTextChat::Clear(draft);
    for (unsigned i = 0; i < state.history && i < CE_CHAT_HISTORY; ++i) {
        CeTextChat::HistoryEntry entry{};
        if (chat.HistoryAt(i, entry) == CeTextChat::Result::Ok) {
            CE_ChatLine &line = out->history[out->history_count++];
            line.local_transport_accepted = entry.origin == CeTextChat::Origin::LocalTransportAccepted;
            line.size = entry.message.text.size;
            std::memcpy(line.utf8, entry.message.text.utf8, line.size);
        }
        CeTextChat::Clear(entry);
    }
}
#else
extern "C" int CE_LockstepChatAvailable(void) { return 0; }
extern "C" int CE_LockstepChatComposing(void) { return 0; }
extern "C" int CE_LockstepChatBegin(void) { return 0; }
extern "C" uint32_t CE_LockstepChatAppend(const uint8_t*, uint32_t) { return CE_CHAT_CLOSED; }
extern "C" uint32_t CE_LockstepChatBackspace(void) { return CE_CHAT_CLOSED; }
extern "C" uint32_t CE_LockstepChatSubmit(void) { return CE_CHAT_CLOSED; }
extern "C" void CE_LockstepChatCancel(void) {}
extern "C" void CE_LockstepChatMute(int) {}
extern "C" void CE_LockstepChatClear(void) {}
extern "C" void CE_LockstepChatCopyView(CE_ChatView *out) {
    if (out) {
        volatile uint8_t *bytes = reinterpret_cast<volatile uint8_t*>(out);
        for (unsigned i = 0; i < sizeof(*out); ++i) bytes[i] = 0;
    }
}
#endif
#endif
