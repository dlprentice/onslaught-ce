// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_match.h: Online Match Session & 2-4 Player Star-Relay Lockstep Controller.
//
// Self-contained header with zero game-header or platform-socket dependencies so standalone ASan/UBSan
// host test drivers compile cleanly while coordinator binds StarTransport to ICeOnlineStarBridge on merge.
//
// Schema is 64-ready (uint16_t seats, topology field, wire ceiling CE_ONLINE_WIRE_MAX_SLOTS = 64) with
// active 0.1.14 simulation cap CE_ONLINE_MAX_MATCH_SLOTS = 4 (2-4 players in star host-relay lockstep).
#ifndef PORT_GAME_CE_ONLINE_MATCH_H
#define PORT_GAME_CE_ONLINE_MATCH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ce_network_world.h"
#include "../network/compatibility.h"
#include "../network/npeer_ring.h"

#ifndef CE_ONLINE_MATCH_TICKET_DEFINED
#define CE_ONLINE_MATCH_TICKET_DEFINED
#define CE_ONLINE_PROTO_V1          1u
#define CE_ONLINE_MAX_MATCH_SLOTS   4u
#define CE_ONLINE_WIRE_MAX_SLOTS    64u

enum CE_OnlineSlotKind {
    CE_ONLINE_SLOT_CLOSED = 0,
    CE_ONLINE_SLOT_HUMAN  = 1,
    CE_ONLINE_SLOT_AI     = 2
};

enum CE_OnlineTeam {
    CE_ONLINE_TEAM_FORSETI = 0,
    CE_ONLINE_TEAM_MUSPELL = 1
};

typedef struct CE_OnlineMatchSlot {
    uint16_t seat;
    uint8_t sim_slot;
    uint8_t kind;               // 0=closed, 1=human, 2=ai
    uint8_t team;               // 0=Forseti, 1=Muspell
    char be_cfg[64];
    char callsign[24];
    char tag[8];
    char player_id[32];
} CE_OnlineMatchSlot;

typedef struct CE_OnlineMatchTicket {
    uint16_t proto;
    char match_id[32];
    char room_id[32];
    char lobby_origin[256];
    char session_token[1600];
    uint32_t seed;
    uint32_t level;
    char mode[24];
    char topology[24];
    uint8_t require_relay;
    uint16_t slot_count;
    uint16_t human_count;
    uint16_t local_seat;
    uint8_t local_sim_slot;
    uint8_t roster_hash[32];
    uint8_t match_secret[32];
    CE_OnlineMatchSlot slots[CE_ONLINE_MAX_MATCH_SLOTS];
} CE_OnlineMatchTicket;
#endif

enum CE_OnlineRouteKind {
    CE_ONLINE_ROUTE_UNKNOWN = 0,
    CE_ONLINE_ROUTE_DIRECT  = 1,
    CE_ONLINE_ROUTE_RELAY   = 2
};

enum CE_OnlineMatchState {
    CE_ONLINE_STATE_IDLE = 0,
    CE_ONLINE_STATE_CONNECTING = 1,
    CE_ONLINE_STATE_RUNNING = 2,
    CE_ONLINE_STATE_DESYNC = 3,
    CE_ONLINE_STATE_HOST_LOST = 4,
    CE_ONLINE_STATE_TOO_FEW_PEERS = 5,
    CE_ONLINE_STATE_ERROR = 6,
    CE_ONLINE_STATE_ENDED = 7
};

#ifdef __cplusplus
extern "C" {
#endif

// ---- C hooks for ce_multibe.cpp, ce_network_world.cpp, ce_network_lockstep.cpp ----
int PortCeOnlineMatchActive(void);
int PortCeOnlineMatchSlotCount(void);
int PortCeOnlineMatchLocalSlot(void);
int PortCeOnlineMatchSlotIsHuman(int slot);
int PortCeOnlineMatchSlotTeam(int slot);
const char *PortCeOnlineMatchSlotBeCfg(int slot);
uint32_t PortCeOnlineMatchSeed(void);
int PortCeOnlineMatchSetTicket(const CE_OnlineMatchTicket *ticket);
void PortCeOnlineMatchClear(void);

// Session/lockstep lifecycle hooks (called via weak refs from ce_network_lockstep.cpp).
int PortCeOnlineMatchGetTicket(CE_OnlineMatchTicket *out_ticket);
uint32_t PortCeOnlineMatchLevel(void);
int PortCeOnlineMatchIsHost(void);
int PortCeOnlineMatchBeginSession(uint32_t level, uint32_t input_delay, uint32_t hash_every);
int PortCeOnlineMatchPoll(uint64_t now_ms);
int PortCeOnlineMatchIsReady(void);
int PortCeOnlineMatchCanAdvance(void);
int PortCeOnlineMatchSubmitLocalInput(uint32_t target_tick, const CE_WorldInput *sample);
int PortCeOnlineMatchConsumeTick(CE_WorldInput out_slots[CE_ONLINE_MAX_MATCH_SLOTS], uint8_t *out_human_mask);
int PortCeOnlineMatchSubmitLocalHash(uint32_t tick, uint64_t state_hash);
int PortCeOnlineMatchStartConfirmed(void);
int PortCeOnlineMatchCheckFault(char *out_error, size_t cap);
void PortCeOnlineMatchStop(int graceful);
int PortCeOnlineMatchSendChat(const char *text);
int PortCeOnlineMatchChatCount(void);
int PortCeOnlineMatchGetChat(int index, uint8_t *out_slot, char *out_text, size_t cap);
void PortCeOnlineMatchClearChat(void);
int PortCeOnlineMatchSlotLabel(int sim_slot, char *out, size_t cap);

// Deterministic AI backfill seed (pure, platform-independent).
uint32_t CeOnlineMatchAiSeed(uint32_t matchSeed, uint8_t simSlot, uint32_t tick);

// Pure ticket structural validator.
int CeOnlineMatchTicketValid(const CE_OnlineMatchTicket *ticket);

#ifdef __cplusplus
} // extern "C"

// ---- Lossless big-endian wire input matching full CE_WorldInput ----

struct CeOnlineWireAction {
    uint8_t button;
    uint8_t source;
    uint32_t value_bits;

    bool operator==(const CeOnlineWireAction &o) const {
        return button == o.button && source == o.source && value_bits == o.value_bits;
    }
    bool operator!=(const CeOnlineWireAction &o) const { return !(*this == o); }
};

struct CeOnlineWireInput {
    static constexpr size_t kMaxActions = CE_WORLD_MAX_ACTIONS; // 32
    static constexpr size_t kHeaderBytes = 16u;
    static constexpr size_t kActionBytes = 6u;
    static constexpr size_t kWireBytes = kHeaderBytes + kMaxActions * kActionBytes; // 208 bytes (4 peers = 848B <= 1200B MTU)

    uint8_t invert_ground;
    uint8_t invert_jet;
    uint8_t walker_mouse_any;
    uint8_t mouse_ground_aim;
    uint32_t ground_yaw_bits;
    uint32_t ground_pitch_bits;
    uint8_t count;
    uint8_t reserved[3];
    CeOnlineWireAction actions[kMaxActions];

    CeOnlineWireInput() { Clear(); }

    void Clear() {
        invert_ground = 0;
        invert_jet = 0;
        walker_mouse_any = 0;
        mouse_ground_aim = 0;
        ground_yaw_bits = 0;
        ground_pitch_bits = 0;
        count = 0;
        reserved[0] = reserved[1] = reserved[2] = 0;
        for (size_t i = 0; i < kMaxActions; ++i) {
            actions[i].button = 0;
            actions[i].source = 0;
            actions[i].value_bits = 0;
        }
    }

    bool operator==(const CeOnlineWireInput &o) const {
        if (invert_ground != o.invert_ground || invert_jet != o.invert_jet ||
            walker_mouse_any != o.walker_mouse_any || mouse_ground_aim != o.mouse_ground_aim ||
            ground_yaw_bits != o.ground_yaw_bits || ground_pitch_bits != o.ground_pitch_bits ||
            count != o.count || reserved[0] != o.reserved[0] ||
            reserved[1] != o.reserved[1] || reserved[2] != o.reserved[2])
            return false;
        for (size_t i = 0; i < kMaxActions; ++i)
            if (actions[i] != o.actions[i])
                return false;
        return true;
    }
    bool operator!=(const CeOnlineWireInput &o) const { return !(*this == o); }

    static uint32_t FloatToBits(float v) {
        uint32_t b = 0;
        memcpy(&b, &v, sizeof(b));
        return b;
    }

    static float BitsToFloat(uint32_t b) {
        float v = 0.0f;
        memcpy(&v, &b, sizeof(v));
        return v;
    }

    static bool IsFiniteBits(uint32_t b) {
        return (b & 0x7f800000u) != 0x7f800000u;
    }

    static bool FromWorldInput(const CE_WorldInput &in, CeOnlineWireInput *out) {
        if (!out) return false;
        out->Clear();
        if (in.count > kMaxActions || in.invert_ground > 1u || in.invert_jet > 1u ||
            in.walker_mouse_any > 1u || in.mouse_ground_aim > 1u ||
            (in.mouse_ground_aim && in.walker_mouse_any))
            return false;
        const uint32_t yawBits = FloatToBits(in.ground_yaw);
        const uint32_t pitchBits = FloatToBits(in.ground_pitch);
        if (!IsFiniteBits(yawBits) || !IsFiniteBits(pitchBits))
            return false;
        if (!(in.ground_yaw >= -3.142f && in.ground_yaw <= 3.142f) ||
            !(in.ground_pitch >= -3.142f && in.ground_pitch <= 3.142f))
            return false;
        if (!in.mouse_ground_aim && (in.ground_yaw != 0.0f || in.ground_pitch != 0.0f))
            return false;
        out->invert_ground = (uint8_t)in.invert_ground;
        out->invert_jet = (uint8_t)in.invert_jet;
        out->walker_mouse_any = (uint8_t)in.walker_mouse_any;
        out->mouse_ground_aim = (uint8_t)in.mouse_ground_aim;
        out->ground_yaw_bits = yawBits;
        out->ground_pitch_bits = pitchBits;
        out->count = (uint8_t)in.count;
        for (size_t i = 0; i < in.count; ++i) {
            if (in.actions[i].button > 255u || in.actions[i].source > CE_WORLD_ACTION_MOUSE_AXIS)
                return false;
            const uint32_t vb = FloatToBits(in.actions[i].value);
            if (!IsFiniteBits(vb) || !(in.actions[i].value >= -1.0f && in.actions[i].value <= 1.0f))
                return false;
            out->actions[i].button = (uint8_t)in.actions[i].button;
            out->actions[i].source = (uint8_t)in.actions[i].source;
            out->actions[i].value_bits = vb;
        }
        return true;
    }

    bool IsValid() const {
        if (count > kMaxActions || reserved[0] || reserved[1] || reserved[2] ||
            invert_ground > 1u || invert_jet > 1u ||
            walker_mouse_any > 1u || mouse_ground_aim > 1u ||
            (mouse_ground_aim && walker_mouse_any))
            return false;
        if (!IsFiniteBits(ground_yaw_bits) || !IsFiniteBits(ground_pitch_bits))
            return false;
        const float yaw = BitsToFloat(ground_yaw_bits);
        const float pitch = BitsToFloat(ground_pitch_bits);
        if (!(yaw >= -3.142f && yaw <= 3.142f) || !(pitch >= -3.142f && pitch <= 3.142f))
            return false;
        if (!mouse_ground_aim && (yaw != 0.0f || pitch != 0.0f))
            return false;
        for (size_t i = 0; i < count; ++i) {
            if (actions[i].source > CE_WORLD_ACTION_MOUSE_AXIS || !IsFiniteBits(actions[i].value_bits))
                return false;
            const float val = BitsToFloat(actions[i].value_bits);
            if (!(val >= -1.0f && val <= 1.0f))
                return false;
        }
        for (size_t i = count; i < kMaxActions; ++i) {
            if (actions[i].button != 0u || actions[i].source != 0u || actions[i].value_bits != 0u)
                return false;
        }
        return true;
    }

    void ToWorldInput(CE_WorldInput *out) const {
        if (!out) return;
        memset(out, 0, sizeof(*out));
        out->invert_ground = invert_ground;
        out->invert_jet = invert_jet;
        out->walker_mouse_any = walker_mouse_any;
        out->mouse_ground_aim = mouse_ground_aim;
        out->ground_yaw = BitsToFloat(ground_yaw_bits);
        out->ground_pitch = BitsToFloat(ground_pitch_bits);
        const size_t n = (count <= kMaxActions) ? count : kMaxActions;
        out->count = (uint32_t)n;
        for (size_t i = 0; i < n; ++i) {
            out->actions[i].button = actions[i].button;
            out->actions[i].source = actions[i].source;
            out->actions[i].value = BitsToFloat(actions[i].value_bits);
        }
    }

    // Fixed-width big-endian codec required by CeNPeerLockstepRing<4, 64, CeOnlineWireInput>.
    void EncodeBE(uint8_t out[kWireBytes]) const {
        out[0] = invert_ground;
        out[1] = invert_jet;
        out[2] = walker_mouse_any;
        out[3] = mouse_ground_aim;
        WriteU32BE(out + 4, ground_yaw_bits);
        WriteU32BE(out + 8, ground_pitch_bits);
        out[12] = count;
        out[13] = reserved[0];
        out[14] = reserved[1];
        out[15] = reserved[2];
        size_t pos = kHeaderBytes;
        for (size_t i = 0; i < kMaxActions; ++i) {
            out[pos++] = actions[i].button;
            out[pos++] = actions[i].source;
            WriteU32BE(out + pos, actions[i].value_bits);
            pos += 4u;
        }
    }

    void DecodeBE(const uint8_t in[kWireBytes]) {
        invert_ground = in[0];
        invert_jet = in[1];
        walker_mouse_any = in[2];
        mouse_ground_aim = in[3];
        ground_yaw_bits = ReadU32BE(in + 4);
        ground_pitch_bits = ReadU32BE(in + 8);
        count = in[12];
        reserved[0] = in[13];
        reserved[1] = in[14];
        reserved[2] = in[15];
        size_t pos = kHeaderBytes;
        for (size_t i = 0; i < kMaxActions; ++i) {
            actions[i].button = in[pos++];
            actions[i].source = in[pos++];
            actions[i].value_bits = ReadU32BE(in + pos);
            pos += 4u;
        }
    }

    // Compact variable-length big-endian codec for single-guest STAR_INPUT_GUEST packets.
    bool EncodeCompactBE(uint8_t *out, size_t capacity, size_t *outLen) const {
        if (!out || !outLen || count > kMaxActions || !IsValid())
            return false;
        const size_t needed = kHeaderBytes + (size_t)count * kActionBytes;
        if (capacity < needed)
            return false;
        out[0] = invert_ground;
        out[1] = invert_jet;
        out[2] = walker_mouse_any;
        out[3] = mouse_ground_aim;
        WriteU32BE(out + 4, ground_yaw_bits);
        WriteU32BE(out + 8, ground_pitch_bits);
        out[12] = count;
        out[13] = 0u;
        out[14] = 0u;
        out[15] = 0u;
        size_t pos = kHeaderBytes;
        for (size_t i = 0; i < count; ++i) {
            out[pos++] = actions[i].button;
            out[pos++] = actions[i].source;
            WriteU32BE(out + pos, actions[i].value_bits);
            pos += 4u;
        }
        *outLen = pos;
        return true;
    }

    bool DecodeCompactBE(const uint8_t *in, size_t len) {
        Clear();
        if (!in || len < kHeaderBytes)
            return false;
        invert_ground = in[0];
        invert_jet = in[1];
        walker_mouse_any = in[2];
        mouse_ground_aim = in[3];
        ground_yaw_bits = ReadU32BE(in + 4);
        ground_pitch_bits = ReadU32BE(in + 8);
        count = in[12];
        reserved[0] = in[13];
        reserved[1] = in[14];
        reserved[2] = in[15];
        if (count > kMaxActions)
            return false;
        const size_t expected = kHeaderBytes + (size_t)count * kActionBytes;
        if (len != expected)
            return false;
        size_t pos = kHeaderBytes;
        for (size_t i = 0; i < count; ++i) {
            actions[i].button = in[pos++];
            actions[i].source = in[pos++];
            actions[i].value_bits = ReadU32BE(in + pos);
            pos += 4u;
        }
        return IsValid();
    }

private:
    static void WriteU32BE(uint8_t *p, uint32_t v) {
        p[0] = (uint8_t)((v >> 24) & 0xffu);
        p[1] = (uint8_t)((v >> 16) & 0xffu);
        p[2] = (uint8_t)((v >> 8) & 0xffu);
        p[3] = (uint8_t)(v & 0xffu);
    }
    static uint32_t ReadU32BE(const uint8_t *p) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }
};

// ---- Full 4-slot state hash coverage helper (human AND AI Battle Engines) ----

struct CeOnlineSlotHashState {
    uint8_t sim_slot;
    uint8_t kind;          // CE_ONLINE_SLOT_*
    uint8_t team;          // CE_ONLINE_TEAM_*
    uint8_t alive;
    uint8_t is_jet;
    uint8_t powered_up;
    uint8_t active;
    uint8_t reserved;
    float pos_x;
    float pos_y;
    float pos_z;
    float vel_x;
    float vel_y;
    float vel_z;
    float yaw;
    float pitch;
    float roll;
    float life;
    float energy;
    uint32_t weapon_index;
    uint32_t respawn_ticks;
    uint32_t ai_seed;
};

uint64_t CeOnlineMatchHashAllSlots(uint32_t tick, uint32_t matchSeed,
                                   const CeOnlineSlotHashState *slots, uint16_t slotCount,
                                   uint64_t worldDigest);

// ---- Pure-C++ star-link transport bridge (coordinator binds StarTransport on merge) ----

enum CeOnlineStarEventKind {
    CE_STAR_EVT_NONE = 0,
    CE_STAR_EVT_GUEST_INPUT = 10,     // guest -> host: compact or full wire input
    CE_STAR_EVT_HOST_BUNDLE = 11,     // host -> guest: CeNPeerLockstepRing bundle bytes
    CE_STAR_EVT_HASH = 12,            // peer -> host (and optional relay): periodic 64-bit state hash
    CE_STAR_EVT_DESYNC = 13,          // host -> guests: consensus desync attribution alert
    CE_STAR_EVT_CHAT = 20,            // peer <-> host: bounded sanitized ASCII chat line
    CE_STAR_EVT_PEER_METRICS = 21,    // rtt_ms & route (CE_ONLINE_ROUTE_*) update
    CE_STAR_EVT_PEER_DISCONNECT = 30, // peer disconnected (sim_slot)
    CE_STAR_EVT_HOST_LOST = 31,       // guest lost star host (slot 0)
    CE_STAR_EVT_TRANSPORT_ERROR = 32  // transport / ICE / signaling fatal error (host or guest)
};

struct CeOnlineStarEvent {
    CeOnlineStarEventKind kind;
    uint8_t from_sim_slot;
    uint8_t reference_slot;
    uint8_t diverging_slot;
    uint8_t diverging_mask;
    uint8_t route;
    uint16_t rtt_ms;
    uint32_t tick;
    uint64_t hash;
    uint64_t expected_hash;
    uint64_t actual_hash;
    uint32_t payload_len;
    uint8_t payload[1024];
    char chat_text[164];
};

class ICeOnlineStarBridge {
public:
    virtual ~ICeOnlineStarBridge() = default;
    virtual void Poll(uint64_t now_ms) = 0;
    virtual bool IsConnected() const = 0;
    virtual bool SendGuestInput(uint8_t sim_slot, uint32_t tick, const uint8_t *data, size_t len) = 0;
    virtual bool SendHostBundle(uint32_t tick, const uint8_t *bundle, size_t len) = 0;
    virtual bool SendHash(uint8_t sim_slot, uint32_t tick, uint64_t state_hash) = 0;
    virtual bool SendDesync(const CeNPeerDesyncReport &report) = 0;
    virtual bool SendChat(uint8_t sim_slot, const char *text) = 0;
    virtual bool SendLeave(uint8_t sim_slot) = 0;
    virtual bool PopEvent(CeOnlineStarEvent *out_event) = 0;
    // Returns true when the underlying transport (e.g. CeStarTransport) already relays guest chat.
    virtual bool BridgeRelaysGuestChat() const { return false; }
};

struct CeOnlineChatEntry {
    uint32_t seq;
    uint8_t from_sim_slot;
    char text[164];
};

// ---- 2-4 Player Online Match Lockstep Session ----

class CeOnlineMatchSession {
public:
    static constexpr size_t kRingSize = 64u;
    static constexpr size_t kChatCap = 32u;
    using RingT = CeNPeerLockstepRing<CE_ONLINE_MAX_MATCH_SLOTS, kRingSize, CeOnlineWireInput>;

    CeOnlineMatchSession();

    void Reset();
    bool Init(const CE_OnlineMatchTicket &ticket, ICeOnlineStarBridge *bridge,
              uint32_t input_delay = 3u, uint32_t hash_every = 20u);

    // Pumps bridge events, broadcasts completed host bundles, checks hash consensus/overdue.
    bool Poll(uint64_t now_ms);

    // Local player submits controls for `target_tick` (typically CurrentTick() + InputDelay()).
    bool SubmitLocalInput(uint32_t target_tick, const CE_WorldInput &input);

    // Returns true when all currently active human peers have inputs ready for CurrentTick().
    bool CanAdvance() const;

    // Consumes CurrentTick() and scatters each active human peer's CE_WorldInput into out_slots[sim_slot].
    // out_human_mask receives the bitmask of sim_slots that received human inputs at the consumed tick.
    bool ConsumeTick(CE_WorldInput out_slots[CE_ONLINE_MAX_MATCH_SLOTS], uint8_t *out_human_mask = nullptr);

    // Submits this peer's periodic state hash for `tick` (must be aligned to HashEvery()).
    bool SubmitLocalHash(uint32_t tick, uint64_t state_hash);

    // Sends a sanitized in-match chat message (1..160 printable ASCII chars).
    bool SendChat(const char *text);

    // Gracefully leaves / stops the session.
    void Stop(bool graceful);

    // Introspection
    CE_OnlineMatchState State() const { return mState; }
    bool IsActive() const { return mState == CE_ONLINE_STATE_CONNECTING || mState == CE_ONLINE_STATE_RUNNING; }
    bool IsReady() const;
    bool IsStartConfirmed() const { return mInitialised && mStartConfirmed; }
    bool IsHost() const { return mInitialised && mTicket.local_sim_slot == 0u; }
    uint32_t CurrentTick() const { return mRing.CurrentTick(); }
    uint32_t InputDelay() const { return mRing.InputDelay(); }
    uint32_t HashEvery() const { return mRing.HashEvery(); }
    uint32_t HashesVerified() const { return mRing.HashesVerified(); }
    uint16_t SlotCount() const { return mTicket.slot_count; }
    uint16_t HumanCount() const { return mTicket.human_count; }
    uint8_t LocalSimSlot() const { return mTicket.local_sim_slot; }
    uint32_t MatchSeed() const { return mTicket.seed; }
    uint8_t ActiveHumanSimMask() const;
    bool IsSlotHumanNow(uint8_t sim_slot) const;
    bool IsSlotDisconnected(uint8_t sim_slot) const;
    uint16_t SlotRttMs(uint8_t sim_slot) const;
    uint8_t SlotRoute(uint8_t sim_slot) const;
    const CE_OnlineMatchTicket &Ticket() const { return mTicket; }
    const CeNPeerDesyncReport &LastDesync() const { return mLastDesync; }
    const char *LastError() const { return mError; }
    size_t ChatCount() const { return mChatCount; }
    bool GetChat(size_t index, CeOnlineChatEntry *out) const;
    void ClearChat() { mChatHead = 0; mChatCount = 0; }

    // Maps between sim_slot (0..3) and compact ring peer index (0..human_count-1).
    uint8_t SimSlotToPeer(uint8_t sim_slot) const;
    uint8_t PeerToSimSlot(uint8_t peer_idx) const;

private:
    void SetFault(CE_OnlineMatchState nextState, const char *message);
    void TryBroadcastReadyBundles();
    bool HandleGuestInputEvent(const CeOnlineStarEvent &ev);
    bool HandleHostBundleEvent(const CeOnlineStarEvent &ev);
    bool HandleHashEvent(const CeOnlineStarEvent &ev);
    void HandleDesyncEvent(const CeOnlineStarEvent &ev);
    void HandleDisconnectEvent(uint8_t sim_slot);
    bool TryConfirmStartHash();
    void AppendChat(uint8_t sim_slot, const char *text);
    CeNPeerDesyncReport RemapReportToSimSlots(const CeNPeerDesyncReport &ringReport) const;

    bool mInitialised;
    CE_OnlineMatchState mState;
    CE_OnlineMatchTicket mTicket;
    ICeOnlineStarBridge *mBridge;
    RingT mRing;
    uint8_t mSimToPeer[CE_ONLINE_MAX_MATCH_SLOTS];
    uint8_t mPeerToSim[CE_ONLINE_MAX_MATCH_SLOTS];
    bool mSlotDisconnected[CE_ONLINE_MAX_MATCH_SLOTS];
    uint16_t mSlotRttMs[CE_ONLINE_MAX_MATCH_SLOTS];
    uint8_t mSlotRoute[CE_ONLINE_MAX_MATCH_SLOTS];
    bool mHasBroadcast;
    bool mBroadcastLeadIn;
    uint32_t mLastBroadcastTick;
    uint8_t mLastConsumedPeerMask;
    bool mStartConfirmed;
    bool mLocalStartHashSubmitted;
    uint64_t mLocalStartHash;
    bool mHostStartHashReceived;
    uint64_t mHostStartHash;
    CeNPeerDesyncReport mLastDesync;
    char mError[192];
    CeOnlineChatEntry mChat[kChatCap];
    size_t mChatHead;
    size_t mChatCount;
    uint32_t mChatSeq;
};

// Global session bridge registration (used by coordinator on merge and tests).
void PortCeOnlineMatchSetBridge(ICeOnlineStarBridge *bridge);
CeOnlineMatchSession *PortCeOnlineMatchSession(void);

#endif // __cplusplus
#endif // PORT_GAME_CE_ONLINE_MATCH_H
