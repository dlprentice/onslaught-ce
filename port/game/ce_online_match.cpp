// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_match.cpp: Online Match Session, 2-4 Player Star-Relay Lockstep Controller,
// deterministic AI backfill seeding, full 4-slot state hash coverage, and C hooks.
#include "ce_online_match.h"
#include "../platform/common/ce_netcode.h"

#include <stdio.h>
#include <string.h>

namespace {

void WipeBytes(void *p, size_t n) {
    volatile uint8_t *b = static_cast<volatile uint8_t *>(p);
    while (n--) *b++ = 0;
}

bool PrintableBounded(const char *s, size_t cap, bool requireNonEmpty) {
    if (!s || cap == 0) return false;
    const void *nul = memchr(s, 0, cap);
    if (!nul) return false;
    const size_t len = size_t(static_cast<const char *>(nul) - s);
    if (requireNonEmpty && len == 0) return false;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20u || c == 0x7fu)
            return false;
    }
    return true;
}

const char *ExpectedModeForOnlineLevel(uint32_t level) {
    switch (level) {
    case 850u:
    case 853u:
    case 859u:
    case 864u:
    case 865u:
    case 866u:
        return "coop_skirmish";
    case 851u:
    case 852u:
    case 854u:
    case 855u:
    case 857u:
    case 860u:
    case 861u:
    case 862u:
    case 863u:
        return "versus_2v2";
    case 856u:
        return "coop_defence";
    default:
        return nullptr;
    }
}

size_t SanitizeChatText(const char *in, char *out, size_t cap) {
    if (!in || !out || cap < 2u) {
        if (out && cap) out[0] = 0;
        return 0;
    }
    const size_t maxChars = (cap - 1u < 160u) ? (cap - 1u) : 160u;
    size_t n = 0;
    for (const char *p = in; *p && n < maxChars; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c >= 0x20u && c <= 0x7eu)
            out[n++] = static_cast<char>(c);
    }
    while (n > 0 && out[n - 1] == ' ')
        --n;
    out[n] = 0;
    return n;
}

struct Fnv64 {
    uint64_t h = 14695981039346656037ULL;
    void Bytes(const void *p, size_t n) {
        const uint8_t *b = static_cast<const uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    }
    void U8(uint8_t v) { Bytes(&v, 1); }
    void U16(uint16_t v) {
        uint8_t b[2] = { uint8_t(v >> 8), uint8_t(v) };
        Bytes(b, 2);
    }
    void U32(uint32_t v) {
        uint8_t b[4] = { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
        Bytes(b, 4);
    }
    void U64(uint64_t v) {
        uint8_t b[8] = {
            uint8_t(v >> 56), uint8_t(v >> 48), uint8_t(v >> 40), uint8_t(v >> 32),
            uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)
        };
        Bytes(b, 8);
    }
    void F32(float v) {
        if (v == 0.0f) v = 0.0f; // canonicalize -0.0f to +0.0f
        uint32_t bits = 0;
        memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }
};

// Global active online ticket + session owned by ce_online_match.cpp.
bool gHasTicket = false;
CE_OnlineMatchTicket gTicket = {};
ICeOnlineStarBridge *gBridge = nullptr;
CeOnlineMatchSession gSession;

} // namespace

// ---- Deterministic AI backfill seed ----

extern "C" uint32_t CeOnlineMatchAiSeed(uint32_t matchSeed, uint8_t simSlot, uint32_t tick) {
    uint64_t x = (uint64_t(matchSeed) << 32) ^
                 (uint64_t(tick) * 0x9E3779B97F4A7C15ULL) ^
                 (uint64_t(simSlot + 1u) * 0xBF58476D1CE4E5B9ULL) ^
                 0x43454F4E4C494E45ULL;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return uint32_t(x ^ (x >> 32));
}

// ---- Structural ticket validation ----

extern "C" int CeOnlineMatchTicketValid(const CE_OnlineMatchTicket *ticket) {
    if (!ticket) return 0;
    if (ticket->proto != CE_ONLINE_PROTO_V1) return 0;
    if (!CE_CompatibilityOnlineLevel(ticket->level)) return 0;
    if (!PrintableBounded(ticket->match_id, sizeof(ticket->match_id), true) ||
        !PrintableBounded(ticket->room_id, sizeof(ticket->room_id), true) ||
        !PrintableBounded(ticket->lobby_origin, sizeof(ticket->lobby_origin), false) ||
        !PrintableBounded(ticket->session_token, sizeof(ticket->session_token), false) ||
        !PrintableBounded(ticket->mode, sizeof(ticket->mode), true) ||
        !PrintableBounded(ticket->topology, sizeof(ticket->topology), true))
        return 0;
    const char *expectedMode = ExpectedModeForOnlineLevel(ticket->level);
    if (!expectedMode || strcmp(ticket->mode, expectedMode) != 0 ||
        strcmp(ticket->topology, "star_lockstep") != 0)
        return 0;
    if (ticket->require_relay > 1u) return 0;
    if (ticket->slot_count < 2u || ticket->slot_count > CE_ONLINE_MAX_MATCH_SLOTS) return 0;
    if (ticket->human_count < 2u || ticket->human_count > ticket->slot_count) return 0;
    if (ticket->local_sim_slot >= ticket->slot_count) return 0;
    if (ticket->slots[0].kind != CE_ONLINE_SLOT_HUMAN) return 0;
    if (ticket->slots[ticket->local_sim_slot].kind != CE_ONLINE_SLOT_HUMAN ||
        ticket->slots[ticket->local_sim_slot].seat != ticket->local_seat)
        return 0;

    uint16_t humans = 0;
    for (uint16_t i = 0; i < ticket->slot_count; ++i) {
        const CE_OnlineMatchSlot &sl = ticket->slots[i];
        if (sl.sim_slot != i || sl.seat >= CE_ONLINE_WIRE_MAX_SLOTS) return 0;
        if (sl.kind != CE_ONLINE_SLOT_HUMAN && sl.kind != CE_ONLINE_SLOT_AI) return 0;
        if (sl.team > CE_ONLINE_TEAM_MUSPELL) return 0;
        if (!PrintableBounded(sl.be_cfg, sizeof(sl.be_cfg), false) ||
            !PrintableBounded(sl.callsign, sizeof(sl.callsign), false) ||
            !PrintableBounded(sl.tag, sizeof(sl.tag), false) ||
            !PrintableBounded(sl.player_id, sizeof(sl.player_id), false))
            return 0;
        for (uint16_t j = 0; j < i; ++j) {
            if (ticket->slots[j].seat == sl.seat)
                return 0;
        }
        if (sl.kind == CE_ONLINE_SLOT_HUMAN)
            ++humans;
    }
    if (humans != ticket->human_count) return 0;
    for (uint16_t i = ticket->slot_count; i < CE_ONLINE_MAX_MATCH_SLOTS; ++i) {
        const CE_OnlineMatchSlot &sl = ticket->slots[i];
        if (sl.kind != CE_ONLINE_SLOT_CLOSED || sl.seat != 0 || sl.sim_slot != 0 || sl.team != 0 ||
            sl.be_cfg[0] || sl.callsign[0] || sl.tag[0] || sl.player_id[0])
            return 0;
    }
    return 1;
}

// ---- Full 4-slot state hash coverage helper ----

uint64_t CeOnlineMatchHashAllSlots(uint32_t tick, uint32_t matchSeed,
                                   const CeOnlineSlotHashState *slots, uint16_t slotCount,
                                   uint64_t worldDigest) {
    Fnv64 h;
    static const char kDomain[] = "ce-online-4p-hash-v1";
    h.Bytes(kDomain, sizeof(kDomain));
    h.U32(tick);
    h.U32(matchSeed);
    h.U16(slotCount);
    h.U64(worldDigest);
    if (slots) {
        const uint16_t bounded = (slotCount <= CE_ONLINE_MAX_MATCH_SLOTS) ? slotCount : CE_ONLINE_MAX_MATCH_SLOTS;
        for (uint16_t i = 0; i < bounded; ++i) {
            const CeOnlineSlotHashState &s = slots[i];
            h.U8(s.sim_slot);
            h.U8(s.kind);
            h.U8(s.team);
            h.U8(s.alive);
            h.U8(s.is_jet);
            h.U8(s.powered_up);
            h.U8(s.active);
            h.F32(s.pos_x);
            h.F32(s.pos_y);
            h.F32(s.pos_z);
            h.F32(s.vel_x);
            h.F32(s.vel_y);
            h.F32(s.vel_z);
            h.F32(s.yaw);
            h.F32(s.pitch);
            h.F32(s.roll);
            h.F32(s.life);
            h.F32(s.energy);
            h.U32(s.weapon_index);
            h.U32(s.respawn_ticks);
            h.U32(s.ai_seed);
        }
    }
    return h.h;
}

// ---- CeOnlineMatchSession implementation ----

CeOnlineMatchSession::CeOnlineMatchSession() {
    Reset();
}

void CeOnlineMatchSession::Reset() {
    mInitialised = false;
    mState = CE_ONLINE_STATE_IDLE;
    WipeBytes(&mTicket, sizeof(mTicket));
    mBridge = nullptr;
    mRing.Reset();
    for (size_t i = 0; i < CE_ONLINE_MAX_MATCH_SLOTS; ++i) {
        mSimToPeer[i] = 0xffu;
        mPeerToSim[i] = 0xffu;
        mSlotDisconnected[i] = false;
        mSlotRttMs[i] = 0u;
        mSlotRoute[i] = CE_ONLINE_ROUTE_UNKNOWN;
    }
    mHasBroadcast = false;
    mBroadcastLeadIn = false;
    mLastBroadcastTick = 0u;
    mLastConsumedPeerMask = 0u;
    mStartConfirmed = false;
    mLocalStartHashSubmitted = false;
    mLocalStartHash = 0u;
    mHostStartHashReceived = false;
    mHostStartHash = 0u;
    memset(&mLastDesync, 0, sizeof(mLastDesync));
    mError[0] = 0;
    memset(mChat, 0, sizeof(mChat));
    mChatHead = 0;
    mChatCount = 0;
    mChatSeq = 0;
}

bool CeOnlineMatchSession::Init(const CE_OnlineMatchTicket &ticket, ICeOnlineStarBridge *bridge,
                                uint32_t input_delay, uint32_t hash_every) {
    Reset();
    if (!CeOnlineMatchTicketValid(&ticket)) {
        SetFault(CE_ONLINE_STATE_ERROR, "online match ticket is invalid");
        return false;
    }
    if (input_delay < 1u || input_delay > 16u || hash_every < 1u || hash_every > 600u) {
        SetFault(CE_ONLINE_STATE_ERROR, "online lockstep delay or hash spacing is out of range");
        return false;
    }

    mTicket = ticket;
    mBridge = bridge;

    // Build compact bidirectional mapping between sim_slot (0..slot_count-1) and ring peer_idx (0..human_count-1).
    uint8_t nextPeer = 0;
    for (uint16_t s = 0; s < mTicket.slot_count; ++s) {
        if (mTicket.slots[s].kind == CE_ONLINE_SLOT_HUMAN) {
            mSimToPeer[s] = nextPeer;
            mPeerToSim[nextPeer] = (uint8_t)s;
            ++nextPeer;
        }
    }
    const uint8_t localPeer = SimSlotToPeer(mTicket.local_sim_slot);
    if (nextPeer != mTicket.human_count || localPeer >= nextPeer) {
        SetFault(CE_ONLINE_STATE_ERROR, "online match human slot mapping failed");
        return false;
    }

    if (mRing.Init(nextPeer, localPeer, input_delay, hash_every) != CE_NPEER_OK) {
        SetFault(CE_ONLINE_STATE_ERROR, "online lockstep ring initialization failed");
        return false;
    }
    CeOnlineWireInput neutral;
    if (mRing.SeedInitialDelay(neutral) != CE_NPEER_OK) {
        SetFault(CE_ONLINE_STATE_ERROR, "online lockstep delay seeding failed");
        return false;
    }

    mLastConsumedPeerMask = mRing.ActiveMask();
    mInitialised = true;
    mState = (mBridge && mBridge->IsConnected()) ? CE_ONLINE_STATE_RUNNING : CE_ONLINE_STATE_CONNECTING;
    return true;
}

void CeOnlineMatchSession::SetFault(CE_OnlineMatchState nextState, const char *message) {
    mState = nextState;
    if (!mError[0] && message)
        snprintf(mError, sizeof(mError), "%s", message);
}

uint8_t CeOnlineMatchSession::SimSlotToPeer(uint8_t sim_slot) const {
    if (sim_slot >= CE_ONLINE_MAX_MATCH_SLOTS) return 0xffu;
    return mSimToPeer[sim_slot];
}

uint8_t CeOnlineMatchSession::PeerToSimSlot(uint8_t peer_idx) const {
    if (peer_idx >= CE_ONLINE_MAX_MATCH_SLOTS) return 0xffu;
    return mPeerToSim[peer_idx];
}

bool CeOnlineMatchSession::IsReady() const {
    if (!mInitialised) return false;
    if (mState == CE_ONLINE_STATE_RUNNING) return true;
    if (mState == CE_ONLINE_STATE_CONNECTING && mBridge && mBridge->IsConnected()) return true;
    return false;
}

uint8_t CeOnlineMatchSession::ActiveHumanSimMask() const {
    if (!mInitialised) return 0u;
    const uint8_t peerMask = mRing.ActiveMask();
    uint8_t simMask = 0u;
    for (uint8_t p = 0; p < mRing.PlayerCount(); ++p) {
        if ((peerMask & (uint8_t)(1u << p)) != 0u) {
            const uint8_t s = PeerToSimSlot(p);
            if (s < mTicket.slot_count)
                simMask |= (uint8_t)(1u << s);
        }
    }
    return simMask;
}

bool CeOnlineMatchSession::IsSlotHumanNow(uint8_t sim_slot) const {
    if (!mInitialised || sim_slot >= mTicket.slot_count)
        return false;
    return mTicket.slots[sim_slot].kind == CE_ONLINE_SLOT_HUMAN && !mSlotDisconnected[sim_slot];
}

bool CeOnlineMatchSession::IsSlotDisconnected(uint8_t sim_slot) const {
    if (sim_slot >= CE_ONLINE_MAX_MATCH_SLOTS)
        return false;
    return mSlotDisconnected[sim_slot];
}

uint16_t CeOnlineMatchSession::SlotRttMs(uint8_t sim_slot) const {
    if (sim_slot >= CE_ONLINE_MAX_MATCH_SLOTS) return 0u;
    return mSlotRttMs[sim_slot];
}

uint8_t CeOnlineMatchSession::SlotRoute(uint8_t sim_slot) const {
    if (sim_slot >= CE_ONLINE_MAX_MATCH_SLOTS) return CE_ONLINE_ROUTE_UNKNOWN;
    return mSlotRoute[sim_slot];
}

CeNPeerDesyncReport CeOnlineMatchSession::RemapReportToSimSlots(const CeNPeerDesyncReport &ringReport) const {
    CeNPeerDesyncReport out = ringReport;
    const uint8_t refSim = PeerToSimSlot(ringReport.referenceSlot);
    const uint8_t divSim = PeerToSimSlot(ringReport.divergingSlot);
    if (refSim < CE_ONLINE_MAX_MATCH_SLOTS) out.referenceSlot = refSim;
    if (divSim < CE_ONLINE_MAX_MATCH_SLOTS) out.divergingSlot = divSim;
    uint8_t simMask = 0u;
    for (uint8_t p = 0; p < mRing.PlayerCount(); ++p) {
        if ((ringReport.divergingMask & (uint8_t)(1u << p)) != 0u) {
            const uint8_t s = PeerToSimSlot(p);
            if (s < CE_ONLINE_MAX_MATCH_SLOTS)
                simMask |= (uint8_t)(1u << s);
        }
    }
    out.divergingMask = simMask;
    return out;
}

void CeOnlineMatchSession::TryBroadcastReadyBundles() {
    if (!mInitialised || !IsHost() || !mBridge || mState != CE_ONLINE_STATE_RUNNING)
        return;
    const uint32_t cur = mRing.CurrentTick();
    uint32_t startTick = mHasBroadcast ? (mLastBroadcastTick + 1u)
                                       : (mBroadcastLeadIn ? cur : mRing.InputDelay());
    if (startTick < cur)
        startTick = cur;
    const uint32_t limit = cur + (uint32_t)kRingSize;
    uint8_t wire[RingT::kBundleHeaderBytes + CE_ONLINE_MAX_MATCH_SLOTS * RingT::kBundleEntryBytes];
    for (uint32_t t = startTick; t < limit; ++t) {
        if (!mRing.IsTickReady(t))
            break;
        size_t len = 0;
        const CeNPeerStatus st = mRing.BuildHostBundle(t, wire, sizeof(wire), &len);
        if (st != CE_NPEER_OK) {
            SetFault(CE_ONLINE_STATE_ERROR, "host failed to build lockstep bundle");
            return;
        }
        if (!mBridge->SendHostBundle(t, wire, len)) {
            SetFault(CE_ONLINE_STATE_ERROR, "host failed to transmit lockstep bundle");
            return;
        }
        mHasBroadcast = true;
        mLastBroadcastTick = t;
    }
}

bool CeOnlineMatchSession::TryConfirmStartHash() {
    if (!mInitialised || !IsHost() || mStartConfirmed || !mLocalStartHashSubmitted ||
        mState != CE_ONLINE_STATE_RUNNING || mRing.CurrentTick() != 0u)
        return true;
    CeNPeerDesyncReport rep = {};
    const CeNPeerHashResult hr = mRing.CheckHashes(0u, &rep);
    if (hr == CE_NPEER_HASH_DESYNC) {
        mLastDesync = RemapReportToSimSlots(rep);
        if (mBridge)
            mBridge->SendDesync(mLastDesync);
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "online match went out of step at tick %u (slot %u diverged from slot %u, mask 0x%02x)",
                 mLastDesync.tick, (unsigned)mLastDesync.divergingSlot,
                 (unsigned)mLastDesync.referenceSlot, (unsigned)mLastDesync.divergingMask);
        SetFault(CE_ONLINE_STATE_DESYNC, msg);
        return false;
    }
    if (hr == CE_NPEER_HASH_MATCH) {
        TryBroadcastReadyBundles();
        mStartConfirmed = true;
        if (mBridge)
            mBridge->SendHash(0u, 0u, mLocalStartHash);
    }
    return true;
}

bool CeOnlineMatchSession::HandleGuestInputEvent(const CeOnlineStarEvent &ev) {
    if (!IsHost())
        return true; // Guests ignore stray guest-input packets; only host builds bundles.
    const uint8_t peerIdx = SimSlotToPeer(ev.from_sim_slot);
    if (peerIdx == 0u || peerIdx >= mRing.PlayerCount()) {
        SetFault(CE_ONLINE_STATE_ERROR, "guest input arrived from an invalid slot");
        return false;
    }
    CeOnlineWireInput wire;
    if (ev.payload_len == CeOnlineWireInput::kWireBytes) {
        wire.DecodeBE(ev.payload);
        if (!wire.IsValid()) {
            SetFault(CE_ONLINE_STATE_ERROR, "malformed guest control sample arrived");
            return false;
        }
    } else {
        if (!wire.DecodeCompactBE(ev.payload, ev.payload_len)) {
            SetFault(CE_ONLINE_STATE_ERROR, "malformed compact guest control sample arrived");
            return false;
        }
    }
    const CeNPeerStatus st = mRing.SubmitInput(peerIdx, ev.tick, wire);
    if (st == CE_NPEER_ERR_INACTIVE_SLOT || st == CE_NPEER_ERR_STALE_TICK) {
        // Ignore late packets from a recently or scheduled-disconnected peer.
        if (mSlotDisconnected[ev.from_sim_slot] || (mRing.DisconnectedMask() & (uint8_t)(1u << peerIdx)) != 0u)
            return true;
    }
    if (st != CE_NPEER_OK) {
        SetFault(CE_ONLINE_STATE_ERROR, "conflicting or out-of-window guest control sample arrived");
        return false;
    }
    TryBroadcastReadyBundles();
    return mState == CE_ONLINE_STATE_CONNECTING || mState == CE_ONLINE_STATE_RUNNING;
}

bool CeOnlineMatchSession::HandleHostBundleEvent(const CeOnlineStarEvent &ev) {
    if (IsHost())
        return true;
    uint32_t decodedTick = 0;
    const CeNPeerStatus st = mRing.ApplyHostBundle(ev.payload, ev.payload_len, &decodedTick);
    if (st != CE_NPEER_OK) {
        SetFault(CE_ONLINE_STATE_ERROR, "malformed or conflicting host lockstep bundle arrived");
        return false;
    }
    // Validate every decoded slot payload inside the bundle!
    if (ev.tick != 0u && decodedTick != ev.tick) {
        SetFault(CE_ONLINE_STATE_ERROR, "host lockstep bundle tick header mismatch");
        return false;
    }
    return true;
}

bool CeOnlineMatchSession::HandleHashEvent(const CeOnlineStarEvent &ev) {
    const uint8_t peerIdx = SimSlotToPeer(ev.from_sim_slot);
    if (peerIdx >= mRing.PlayerCount())
        return true;
    const CeNPeerStatus st = mRing.SubmitHash(peerIdx, ev.tick, ev.hash);
    if (st == CE_NPEER_ERR_INACTIVE_SLOT &&
        (mSlotDisconnected[ev.from_sim_slot] || (mRing.DisconnectedMask() & (uint8_t)(1u << peerIdx)) != 0u))
        return true;
    if (st != CE_NPEER_OK) {
        SetFault(CE_ONLINE_STATE_ERROR, "conflicting or malformed peer state hash arrived");
        return false;
    }
    if (!IsHost() && ev.tick == 0u && ev.from_sim_slot == 0u) {
        mHostStartHashReceived = true;
        mHostStartHash = ev.hash;
        if (mLocalStartHashSubmitted) {
            if (mLocalStartHash != mHostStartHash) {
                mLastDesync.tick = 0u;
                mLastDesync.referenceSlot = 0u;
                mLastDesync.divergingSlot = mTicket.local_sim_slot;
                mLastDesync.divergingMask = (uint8_t)(1u << mTicket.local_sim_slot);
                mLastDesync.expectedHash = mHostStartHash;
                mLastDesync.actualHash = mLocalStartHash;
                SetFault(CE_ONLINE_STATE_DESYNC, "online match start state disagreed with the host");
                return false;
            }
            mStartConfirmed = true;
        }
    }
    if (IsHost() && ev.tick == 0u) {
        return TryConfirmStartHash();
    }
    CeNPeerDesyncReport rep = {};
    const CeNPeerHashResult hr = mRing.CheckHashes(ev.tick, &rep);
    if (hr == CE_NPEER_HASH_DESYNC) {
        mLastDesync = RemapReportToSimSlots(rep);
        if (IsHost() && mBridge)
            mBridge->SendDesync(mLastDesync);
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "online match went out of step at tick %u (slot %u diverged from slot %u, mask 0x%02x)",
                 mLastDesync.tick, (unsigned)mLastDesync.divergingSlot,
                 (unsigned)mLastDesync.referenceSlot, (unsigned)mLastDesync.divergingMask);
        SetFault(CE_ONLINE_STATE_DESYNC, msg);
        return false;
    }
    return true;
}

void CeOnlineMatchSession::HandleDesyncEvent(const CeOnlineStarEvent &ev) {
    mLastDesync.tick = ev.tick;
    mLastDesync.referenceSlot = ev.reference_slot;
    mLastDesync.divergingSlot = ev.diverging_slot;
    mLastDesync.divergingMask = ev.diverging_mask;
    mLastDesync.expectedHash = ev.expected_hash;
    mLastDesync.actualHash = ev.actual_hash;
    char msg[192];
    snprintf(msg, sizeof(msg),
             "online match went out of step at tick %u (slot %u diverged from slot %u, mask 0x%02x)",
             ev.tick, (unsigned)ev.diverging_slot,
             (unsigned)ev.reference_slot, (unsigned)ev.diverging_mask);
    SetFault(CE_ONLINE_STATE_DESYNC, msg);
}

void CeOnlineMatchSession::HandleDisconnectEvent(uint8_t sim_slot) {
    const uint8_t peerIdx = SimSlotToPeer(sim_slot);
    if (peerIdx >= mRing.PlayerCount())
        return;
    // Choose disconnect boundary cleanly: never contradict an already-broadcast tick or
    // locally-seeded lead-in ticks once the tick-0 start barrier has released.
    uint32_t atTick = mRing.CurrentTick();
    if (IsHost()) {
        if (mHasBroadcast && mLastBroadcastTick >= atTick)
            atTick = mLastBroadcastTick + 1u;
        else if (mStartConfirmed && atTick < mRing.InputDelay())
            atTick = mRing.InputDelay();
    }
    const CeNPeerStatus st = mRing.DisconnectPeer(peerIdx, atTick);
    if (st == CE_NPEER_ERR_HOST_LOST) {
        mSlotDisconnected[0] = true;
        SetFault(CE_ONLINE_STATE_HOST_LOST,
                 IsHost() ? "online match host transport stopped"
                          : "connection to the match host was lost");
        return;
    }
    if (st == CE_NPEER_ERR_TOO_FEW_PEERS) {
        mSlotDisconnected[sim_slot] = true;
        SetFault(CE_ONLINE_STATE_TOO_FEW_PEERS, "too few human players remain to continue the online match");
        return;
    }
    if (st == CE_NPEER_OK) {
        // Flip mSlotDisconnected exclusively inside ConsumeTick() so Host and Guests
        // transition the dropped slot to AI backfill on the exact same simulation tick.
        if (IsHost() && !mStartConfirmed && atTick == 0u)
            mBroadcastLeadIn = true;
        TryBroadcastReadyBundles();
        if (!TryConfirmStartHash())
            return;
    }
}

void CeOnlineMatchSession::AppendChat(uint8_t sim_slot, const char *text) {
    char clean[164] = {};
    if (!SanitizeChatText(text, clean, sizeof(clean)))
        return;
    const size_t pos = (mChatHead + mChatCount) % kChatCap;
    if (mChatCount == kChatCap)
        mChatHead = (mChatHead + 1u) % kChatCap;
    else
        ++mChatCount;
    mChat[pos].seq = ++mChatSeq;
    mChat[pos].from_sim_slot = sim_slot;
    snprintf(mChat[pos].text, sizeof(mChat[pos].text), "%s", clean);
}

bool CeOnlineMatchSession::Poll(uint64_t now_ms) {
    if (!mInitialised)
        return false;
    if (mState != CE_ONLINE_STATE_CONNECTING && mState != CE_ONLINE_STATE_RUNNING)
        return false;
    if (mBridge) {
        mBridge->Poll(now_ms);
        if (mState == CE_ONLINE_STATE_CONNECTING && mBridge->IsConnected())
            mState = CE_ONLINE_STATE_RUNNING;
        CeOnlineStarEvent ev = {};
        while ((mState == CE_ONLINE_STATE_CONNECTING || mState == CE_ONLINE_STATE_RUNNING) && mBridge->PopEvent(&ev)) {
            switch (ev.kind) {
            case CE_STAR_EVT_GUEST_INPUT:
                if (!HandleGuestInputEvent(ev))
                    return false;
                break;
            case CE_STAR_EVT_HOST_BUNDLE:
                if (!HandleHostBundleEvent(ev))
                    return false;
                break;
            case CE_STAR_EVT_HASH:
                if (!HandleHashEvent(ev))
                    return false;
                break;
            case CE_STAR_EVT_DESYNC:
                HandleDesyncEvent(ev);
                return false;
            case CE_STAR_EVT_CHAT:
                if (ev.from_sim_slot < mTicket.slot_count) {
                    if (!IsHost() && ev.from_sim_slot == mTicket.local_sim_slot)
                        break;
                    char clean[164] = {};
                    if (SanitizeChatText(ev.chat_text, clean, sizeof(clean))) {
                        AppendChat(ev.from_sim_slot, clean);
                        if (IsHost() && ev.from_sim_slot != 0u && mBridge && !mBridge->BridgeRelaysGuestChat())
                            mBridge->SendChat(ev.from_sim_slot, clean);
                    }
                }
                break;
            case CE_STAR_EVT_PEER_METRICS:
                if (ev.from_sim_slot < mTicket.slot_count) {
                    mSlotRttMs[ev.from_sim_slot] = ev.rtt_ms;
                    mSlotRoute[ev.from_sim_slot] = (ev.route <= CE_ONLINE_ROUTE_RELAY) ? ev.route : CE_ONLINE_ROUTE_UNKNOWN;
                }
                break;
            case CE_STAR_EVT_PEER_DISCONNECT:
                HandleDisconnectEvent(ev.from_sim_slot);
                if (mState != CE_ONLINE_STATE_CONNECTING && mState != CE_ONLINE_STATE_RUNNING)
                    return false;
                if (mState == CE_ONLINE_STATE_CONNECTING && mBridge->IsConnected())
                    mState = CE_ONLINE_STATE_RUNNING;
                break;
            case CE_STAR_EVT_HOST_LOST:
                HandleDisconnectEvent(0u);
                return false;
            case CE_STAR_EVT_TRANSPORT_ERROR:
                SetFault(CE_ONLINE_STATE_ERROR,
                         IsHost() ? "online match host transport failed to connect peers"
                                  : "online match transport connection failed");
                return false;
            default:
                break;
            }
            memset(&ev, 0, sizeof(ev));
        }
        if (mState == CE_ONLINE_STATE_CONNECTING && mBridge->IsConnected())
            mState = CE_ONLINE_STATE_RUNNING;
    }
    if (IsHost() && mState == CE_ONLINE_STATE_RUNNING) {
        TryBroadcastReadyBundles();
        if (!TryConfirmStartHash())
            return false;
    }
    return mState == CE_ONLINE_STATE_CONNECTING || mState == CE_ONLINE_STATE_RUNNING;
}

bool CeOnlineMatchSession::SubmitLocalInput(uint32_t target_tick, const CE_WorldInput &input) {
    if (!mInitialised || mState != CE_ONLINE_STATE_RUNNING)
        return false;
    CeOnlineWireInput wire;
    if (!CeOnlineWireInput::FromWorldInput(input, &wire)) {
        SetFault(CE_ONLINE_STATE_ERROR, "local online control sample was invalid");
        return false;
    }
    if (IsHost()) {
        const CeNPeerStatus st = mRing.SubmitInput(0u, target_tick, wire);
        if (st != CE_NPEER_OK) {
            SetFault(CE_ONLINE_STATE_ERROR, "host could not queue local control sample");
            return false;
        }
        TryBroadcastReadyBundles();
        return mState == CE_ONLINE_STATE_RUNNING;
    }
    if (!mBridge) {
        SetFault(CE_ONLINE_STATE_ERROR, "guest has no online transport bridge");
        return false;
    }
    const uint8_t localPeer = SimSlotToPeer(mTicket.local_sim_slot);
    if (mRing.SubmitInput(localPeer, target_tick, wire) != CE_NPEER_OK) {
        SetFault(CE_ONLINE_STATE_ERROR, "guest could not record local control sample");
        return false;
    }
    uint8_t compact[CeOnlineWireInput::kWireBytes];
    size_t compactLen = 0;
    if (!wire.EncodeCompactBE(compact, sizeof(compact), &compactLen)) {
        SetFault(CE_ONLINE_STATE_ERROR, "guest could not encode control sample");
        return false;
    }
    if (!mBridge->SendGuestInput(mTicket.local_sim_slot, target_tick, compact, compactLen)) {
        SetFault(CE_ONLINE_STATE_ERROR, "guest could not send control sample to host");
        return false;
    }
    return true;
}

bool CeOnlineMatchSession::CanAdvance() const {
    return mInitialised && mState == CE_ONLINE_STATE_RUNNING && mRing.CanAdvance();
}

bool CeOnlineMatchSession::ConsumeTick(CE_WorldInput out_slots[CE_ONLINE_MAX_MATCH_SLOTS], uint8_t *out_human_mask) {
    if (!out_slots) return false;
    for (size_t i = 0; i < CE_ONLINE_MAX_MATCH_SLOTS; ++i)
        memset(&out_slots[i], 0, sizeof(out_slots[i]));
    if (out_human_mask) *out_human_mask = 0u;
    if (!CanAdvance())
        return false;

    CeOnlineWireInput raw[CE_ONLINE_MAX_MATCH_SLOTS];
    uint8_t peerMask = 0u;
    const CeNPeerStatus st = mRing.ConsumeTick(raw, &peerMask);
    if (st != CE_NPEER_OK) {
        SetFault(CE_ONLINE_STATE_ERROR, "online lockstep tick consumption failed");
        return false;
    }

    // Observe peer disconnect transitions on the EXACT consumed tick across Host and all Guests!
    const uint8_t droppedPeers = (uint8_t)(mLastConsumedPeerMask & ~peerMask);
    if (droppedPeers != 0u) {
        for (uint8_t p = 0; p < mRing.PlayerCount(); ++p) {
            if ((droppedPeers & (uint8_t)(1u << p)) != 0u) {
                const uint8_t sim = PeerToSimSlot(p);
                if (sim < CE_ONLINE_MAX_MATCH_SLOTS)
                    mSlotDisconnected[sim] = true;
            }
        }
    }
    mLastConsumedPeerMask = peerMask;

    uint8_t humanSimMask = 0u;
    for (uint8_t p = 0; p < mRing.PlayerCount(); ++p) {
        if ((peerMask & (uint8_t)(1u << p)) == 0u)
            continue;
        if (!raw[p].IsValid()) {
            SetFault(CE_ONLINE_STATE_ERROR, "decoded online control sample failed validation");
            return false;
        }
        const uint8_t sim = PeerToSimSlot(p);
        if (sim < mTicket.slot_count) {
            raw[p].ToWorldInput(&out_slots[sim]);
            out_slots[sim].player = sim;
            humanSimMask |= (uint8_t)(1u << sim);
        }
    }
    if (out_human_mask)
        *out_human_mask = humanSimMask;
    return true;
}

bool CeOnlineMatchSession::SubmitLocalHash(uint32_t tick, uint64_t state_hash) {
    if (!mInitialised || mState != CE_ONLINE_STATE_RUNNING)
        return false;
    const uint8_t localPeer = SimSlotToPeer(mTicket.local_sim_slot);
    const CeNPeerStatus st = mRing.SubmitHash(localPeer, tick, state_hash);
    if (st != CE_NPEER_OK) {
        SetFault(CE_ONLINE_STATE_ERROR, "could not record local periodic state hash");
        return false;
    }
    if (tick == 0u) {
        mLocalStartHashSubmitted = true;
        mLocalStartHash = state_hash;
    }
    if (mBridge && !IsHost()) {
        if (!mBridge->SendHash(mTicket.local_sim_slot, tick, state_hash)) {
            SetFault(CE_ONLINE_STATE_ERROR, "could not send periodic state hash to host");
            return false;
        }
    }
    if (!IsHost() && tick == 0u && mHostStartHashReceived) {
        if (mLocalStartHash != mHostStartHash) {
            mLastDesync.tick = 0u;
            mLastDesync.referenceSlot = 0u;
            mLastDesync.divergingSlot = mTicket.local_sim_slot;
            mLastDesync.divergingMask = (uint8_t)(1u << mTicket.local_sim_slot);
            mLastDesync.expectedHash = mHostStartHash;
            mLastDesync.actualHash = mLocalStartHash;
            SetFault(CE_ONLINE_STATE_DESYNC, "online match start state disagreed with the host");
            return false;
        }
        mStartConfirmed = true;
    }
    if (IsHost()) {
        if (tick == 0u) {
            return TryConfirmStartHash();
        }
        CeNPeerDesyncReport rep = {};
        const CeNPeerHashResult hr = mRing.CheckHashes(tick, &rep);
        if (hr == CE_NPEER_HASH_DESYNC) {
            mLastDesync = RemapReportToSimSlots(rep);
            if (mBridge)
                mBridge->SendDesync(mLastDesync);
            char msg[192];
            snprintf(msg, sizeof(msg),
                     "online match went out of step at tick %u (slot %u diverged from slot %u, mask 0x%02x)",
                     mLastDesync.tick, (unsigned)mLastDesync.divergingSlot,
                     (unsigned)mLastDesync.referenceSlot, (unsigned)mLastDesync.divergingMask);
            SetFault(CE_ONLINE_STATE_DESYNC, msg);
            return false;
        }
    }
    return true;
}

bool CeOnlineMatchSession::SendChat(const char *text) {
    if (!mInitialised || mState != CE_ONLINE_STATE_RUNNING)
        return false;
    char clean[164] = {};
    if (!SanitizeChatText(text, clean, sizeof(clean)))
        return false;
    AppendChat(mTicket.local_sim_slot, clean);
    if (mBridge)
        return mBridge->SendChat(mTicket.local_sim_slot, clean);
    return true;
}

bool CeOnlineMatchSession::GetChat(size_t index, CeOnlineChatEntry *out) const {
    if (!out || index >= mChatCount)
        return false;
    *out = mChat[(mChatHead + index) % kChatCap];
    return true;
}

void CeOnlineMatchSession::Stop(bool graceful) {
    if (mInitialised && graceful && mBridge)
        mBridge->SendLeave(mTicket.local_sim_slot);
    if (mState == CE_ONLINE_STATE_CONNECTING || mState == CE_ONLINE_STATE_RUNNING)
        mState = CE_ONLINE_STATE_ENDED;
    WipeBytes(mTicket.match_secret, sizeof(mTicket.match_secret));
    WipeBytes(mTicket.session_token, sizeof(mTicket.session_token));
}

// ---- Global C hooks ----

void PortCeOnlineMatchSetBridge(ICeOnlineStarBridge *bridge) {
    gBridge = bridge;
    if (!bridge)
        gSession.DetachBridge();
}

CeOnlineMatchSession *PortCeOnlineMatchSession(void) {
    return &gSession;
}

extern "C" {

int PortCeOnlineMatchActive(void) {
    return gHasTicket ? 1 : 0;
}

int PortCeOnlineMatchSlotCount(void) {
    return gHasTicket ? (int)gTicket.slot_count : 0;
}

int PortCeOnlineMatchLocalSlot(void) {
    return gHasTicket ? (int)gTicket.local_sim_slot : 0;
}

int PortCeOnlineMatchSlotIsHuman(int slot) {
    if (!gHasTicket || slot < 0 || slot >= (int)gTicket.slot_count)
        return 0;
    if (gSession.IsActive() || gSession.State() != CE_ONLINE_STATE_IDLE)
        return gSession.IsSlotHumanNow((uint8_t)slot) ? 1 : 0;
    return (gTicket.slots[slot].kind == CE_ONLINE_SLOT_HUMAN) ? 1 : 0;
}

int PortCeOnlineMatchSlotTeam(int slot) {
    if (!gHasTicket || slot < 0 || slot >= (int)gTicket.slot_count)
        return CE_ONLINE_TEAM_FORSETI;
    return (gTicket.slots[slot].team == CE_ONLINE_TEAM_MUSPELL) ? CE_ONLINE_TEAM_MUSPELL : CE_ONLINE_TEAM_FORSETI;
}

const char *PortCeOnlineMatchSlotBeCfg(int slot) {
    if (!gHasTicket || slot < 0 || slot >= (int)gTicket.slot_count)
        return "";
    return gTicket.slots[slot].be_cfg;
}

uint32_t PortCeOnlineMatchSeed(void) {
    return gHasTicket ? gTicket.seed : 0u;
}

int PortCeOnlineMatchSetTicket(const CE_OnlineMatchTicket *ticket) {
    PortCeOnlineMatchClear();
    if (!ticket || !CeOnlineMatchTicketValid(ticket))
        return 0;
    gTicket = *ticket;
    gHasTicket = true;
    return 1;
}

void PortCeOnlineMatchClear(void) {
    gSession.Reset();
    WipeBytes(&gTicket, sizeof(gTicket));
    gHasTicket = false;
}

int PortCeOnlineMatchGetTicket(CE_OnlineMatchTicket *out_ticket) {
    if (!out_ticket) return 0;
    if (!gHasTicket) {
        memset(out_ticket, 0, sizeof(*out_ticket));
        return 0;
    }
    *out_ticket = gTicket;
    return 1;
}

uint32_t PortCeOnlineMatchLevel(void) {
    return gHasTicket ? gTicket.level : 0u;
}

int PortCeOnlineMatchIsHost(void) {
    return (gHasTicket && gTicket.local_sim_slot == 0u) ? 1 : 0;
}

int PortCeOnlineMatchBeginSession(uint32_t level, uint32_t input_delay, uint32_t hash_every) {
    if (!gHasTicket || gTicket.level != level)
        return 0;
    return gSession.Init(gTicket, gBridge, input_delay, hash_every) ? 1 : 0;
}

int PortCeOnlineMatchPoll(uint64_t now_ms) {
    return gSession.Poll(now_ms) ? 1 : 0;
}

int PortCeOnlineMatchIsReady(void) {
    return gSession.IsReady() ? 1 : 0;
}

int PortCeOnlineMatchCanAdvance(void) {
    return gSession.CanAdvance() ? 1 : 0;
}

int PortCeOnlineMatchSubmitLocalInput(uint32_t target_tick, const CE_WorldInput *sample) {
    if (!sample) return 0;
    return gSession.SubmitLocalInput(target_tick, *sample) ? 1 : 0;
}

int PortCeOnlineMatchConsumeTick(CE_WorldInput out_slots[CE_ONLINE_MAX_MATCH_SLOTS], uint8_t *out_human_mask) {
    return gSession.ConsumeTick(out_slots, out_human_mask) ? 1 : 0;
}

int PortCeOnlineMatchSubmitLocalHash(uint32_t tick, uint64_t state_hash) {
    return gSession.SubmitLocalHash(tick, state_hash) ? 1 : 0;
}

int PortCeOnlineMatchStartConfirmed(void) {
    return gSession.IsStartConfirmed() ? 1 : 0;
}

int PortCeOnlineMatchCheckFault(char *out_error, size_t cap) {
    const CE_OnlineMatchState st = gSession.State();
    if (st == CE_ONLINE_STATE_DESYNC || st == CE_ONLINE_STATE_HOST_LOST ||
        st == CE_ONLINE_STATE_TOO_FEW_PEERS || st == CE_ONLINE_STATE_ERROR) {
        if (out_error && cap)
            snprintf(out_error, cap, "%s", gSession.LastError()[0] ? gSession.LastError() : "online match stopped");
        return 1;
    }
    if (out_error && cap) out_error[0] = 0;
    return 0;
}

void PortCeOnlineMatchStop(int graceful) {
    gSession.Stop(graceful != 0);
}

int PortCeOnlineMatchSendChat(const char *text) {
    return gSession.SendChat(text) ? 1 : 0;
}

int PortCeOnlineMatchChatCount(void) {
    return (int)gSession.ChatCount();
}

int PortCeOnlineMatchGetChat(int index, uint8_t *out_slot, char *out_text, size_t cap) {
    if (index < 0) return 0;
    CeOnlineChatEntry entry = {};
    if (!gSession.GetChat((size_t)index, &entry)) return 0;
    if (out_slot) *out_slot = entry.from_sim_slot;
    if (out_text && cap > 0) snprintf(out_text, cap, "%s", entry.text);
    return 1;
}

void PortCeOnlineMatchClearChat(void) {
    gSession.ClearChat();
}

int PortCeOnlineMatchSlotLabel(int sim_slot, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (!gHasTicket || sim_slot < 0 || sim_slot >= (int)gTicket.slot_count) return 0;
    if (gTicket.slots[sim_slot].callsign[0])
        snprintf(out, cap, "%s", gTicket.slots[sim_slot].callsign);
    else
        snprintf(out, cap, "P%d", sim_slot + 1);
    return 1;
}

} // extern "C"
