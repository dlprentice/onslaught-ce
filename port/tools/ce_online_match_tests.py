#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Online 2-4 Player Match Session, Start-Ticket Handoff & Compatibility ASan/UBSan test suite.

    python3 -B port/tools/ce_online_match_tests.py [-v]

Verifies:
  1. CE_Handoff roundtrip for online_version == 0 (legacy 2P byte-identical) AND online_version == 1
     (4-player start-ticket with mixed human+AI, Forseti/Muspell teams, BE config names, require_relay,
     file write/read unlink-on-read) plus corruption, truncation, forbidden-key, expiry, and schema
     negative controls.
  2. Full 4-peer simulated star lockstep run (1 Host + 3 Guests, 200 ticks) with lossless full
     CE_WorldInput (buttons, bindings, mouse axis, ground yaw/pitch floats, invert flags), periodic
     state hashes across all 4 slots, mixed Human+AI 4-slot match with deterministic CeOnlineMatchAiSeed
     parity, chat sanitization/delivery, and RTT/route telemetry updates.
  3. Intentional 1-slot desync detection & consensus attribution (guest slot diverging, host slot
     diverging outvoted by guests, AND AI-slot divergence caught by CeOnlineMatchHashAllSlots).
  4. Mid-match guest disconnect & deterministic graceful continuation to tick 200 (identical AI-takeover
     tick across surviving peers), <2-human TOO_FEW_PEERS stop, and HOST_LOST detection.
  5. CE_CompatibilityOnlineLevel admitting all 18 maps (100, 850..866) and refusing invalid levels,
     while CE_CompatibilityLockstepLevel stays strictly {100, 856, 858} and CE_COMPAT_ONLINE_MULTIBE_V1 == 5.
  6. C hooks (PortCeOnlineMatchSetTicket/Active/SlotCount/LocalSlot/SlotIsHuman/SlotTeam/SlotBeCfg/Seed/Clear).
  7. ASan + UBSan clean execution (-fsanitize=address,undefined -Wall -Wextra -Werror).
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SCRATCH = ROOT / "local-data/port/ce-online-match-tests"

DRIVER = r"""
#include "ce_online_match.h"
#include "ce_netcode.h"
#include "compatibility.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

static int gFail = 0;

static void Check(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        gFail++;
    }
}

extern "C" int CE_NetGenerateSecret(uint8_t out[32]) {
    for (int i = 0; i < 32; ++i)
        out[i] = (uint8_t)(0xA5u ^ (uint8_t)(i * 13 + 7));
    return 1;
}

static CE_OnlineMatchTicket MakeTicket(uint16_t slotCount, uint16_t humanCount,
                                       uint8_t localSimSlot, uint32_t level,
                                       uint8_t requireRelay) {
    CE_OnlineMatchTicket t = {};
    t.proto = CE_ONLINE_PROTO_V1;
    std::snprintf(t.match_id, sizeof(t.match_id), "match-4p-2026");
    std::snprintf(t.room_id, sizeof(t.room_id), "room-alpha-7");
    std::snprintf(t.lobby_origin, sizeof(t.lobby_origin), "https://lobby.example.test");
    std::snprintf(t.session_token, sizeof(t.session_token), "tok_session_0123456789abcdef");
    t.seed = 0x51A7BEEFu;
    t.level = level;
    std::snprintf(t.mode, sizeof(t.mode), "coop_skirmish");
    std::snprintf(t.topology, sizeof(t.topology), "star_lockstep");
    t.require_relay = requireRelay;
    t.slot_count = slotCount;
    t.human_count = humanCount;
    t.local_seat = localSimSlot;
    t.local_sim_slot = localSimSlot;
    for (size_t i = 0; i < 32; ++i) {
        t.roster_hash[i] = (uint8_t)(0x10u + i);
        t.match_secret[i] = (uint8_t)(0xE0u - i);
    }
    const char *cfgs[4] = {
        "Forseti Standard",
        "Muspell Assault",
        "Forseti Recon",
        "Muspell Heavy"
    };
    const char *names[4] = { "Hawk", "Viper", "Wraith", "Titan" };
    const char *tags[4]  = { "CE", "MSP", "FRS", "AI" };
    for (uint16_t s = 0; s < slotCount; ++s) {
        t.slots[s].seat = s;
        t.slots[s].sim_slot = (uint8_t)s;
        t.slots[s].kind = (s < humanCount) ? CE_ONLINE_SLOT_HUMAN : CE_ONLINE_SLOT_AI;
        t.slots[s].team = (uint8_t)(s & 1u);
        std::snprintf(t.slots[s].be_cfg, sizeof(t.slots[s].be_cfg), "%s", cfgs[s]);
        std::snprintf(t.slots[s].callsign, sizeof(t.slots[s].callsign), "%s", names[s]);
        std::snprintf(t.slots[s].tag, sizeof(t.slots[s].tag), "%s", tags[s]);
        std::snprintf(t.slots[s].player_id, sizeof(t.slots[s].player_id), "pid_%u", (unsigned)s);
    }
    return t;
}

static bool TicketsEqual(const CE_OnlineMatchTicket &a, const CE_OnlineMatchTicket &b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

static bool WorldInputsBitEqual(const CE_WorldInput &a, const CE_WorldInput &b) {
    if (a.invert_ground != b.invert_ground || a.invert_jet != b.invert_jet ||
        a.walker_mouse_any != b.walker_mouse_any || a.mouse_ground_aim != b.mouse_ground_aim ||
        a.count != b.count)
        return false;
    uint32_t ay = 0, by = 0, ap = 0, bp = 0;
    std::memcpy(&ay, &a.ground_yaw, 4);
    std::memcpy(&by, &b.ground_yaw, 4);
    std::memcpy(&ap, &a.ground_pitch, 4);
    std::memcpy(&bp, &b.ground_pitch, 4);
    if (ay != by || ap != bp)
        return false;
    for (uint32_t i = 0; i < a.count; ++i) {
        uint32_t av = 0, bv = 0;
        std::memcpy(&av, &a.actions[i].value, 4);
        std::memcpy(&bv, &b.actions[i].value, 4);
        if (a.actions[i].button != b.actions[i].button ||
            a.actions[i].source != b.actions[i].source ||
            av != bv)
            return false;
    }
    return true;
}

static CE_WorldInput MakeRichWorldInput(uint32_t tick, uint8_t slot) {
    CE_WorldInput in = {};
    in.invert_ground = (tick + slot) & 1u;
    in.invert_jet = ((tick >> 1) + slot) & 1u;
    if ((tick + slot) % 3u == 0u) {
        in.mouse_ground_aim = 1u;
        in.walker_mouse_any = 0u;
        in.ground_yaw = -1.75f + 0.015f * (float)((tick + slot * 11u) % 200u);
        in.ground_pitch = -0.85f + 0.008f * (float)((tick * 3u + slot * 7u) % 180u);
    } else if ((tick + slot) % 3u == 1u) {
        in.mouse_ground_aim = 0u;
        in.walker_mouse_any = 1u;
        in.ground_yaw = 0.0f;
        in.ground_pitch = 0.0f;
    }
    const uint32_t buttons[] = { 25u, 27u, 29u, 31u, 59u, 19u, 16u, 21u };
    uint32_t idx = 0;
    for (uint32_t b = 0; b < 8u; ++b) {
        if (((tick + slot + b) & 1u) != 0u) {
            in.actions[idx].button = buttons[b];
            in.actions[idx].source = (b < 2u) ? CE_WORLD_ACTION_MOUSE_AXIS : CE_WORLD_ACTION_BINDING;
            const int signedStep = (int)((tick * 7u + slot * 13u + b * 5u) % 201u) - 100;
            in.actions[idx].value = (float)signedStep * 0.01f;
            idx++;
        }
    }
    in.count = idx;
    return in;
}

// ---- Test 1: CE_Handoff roundtrip (legacy v0 & online v1) + negative controls ----
static void TestHandoffRoundtripAndNegatives(const char *scratchDir) {
    const int64_t now = CE_HandoffNow();

    // 1a. Legacy online_version == 0 launch byte-identical check
    {
        CE_Handoff h = {};
        h.kind = CE_HANDOFF_LAUNCH;
        h.role = 1;
        std::snprintf(h.endpoint, sizeof(h.endpoint), "127.0.0.1:27600");
        for (int i = 0; i < 32; ++i) h.secret[i] = (uint8_t)(i + 1);
        for (int i = 0; i < 16; ++i) h.nonce[i] = (uint8_t)(0x40 + i);
        h.level = 856;
        h.created = now - 5;
        std::snprintf(h.args, sizeof(h.args), "--windowed");
        h.online_version = 0;

        char buf[CE_HANDOFF_MAX_BYTES] = {};
        const size_t n = CE_HandoffFormat(&h, buf, sizeof(buf));
        Check(n > 0, "legacy v0 format succeeds");
        Check(std::strncmp(buf, "onslaught-ce handoff 1\n", 23) == 0, "legacy v0 header preserved verbatim");
        Check(std::strstr(buf, "online_version") == nullptr, "legacy v0 emits zero online fields");

        CE_Handoff parsed = {};
        const char *err = nullptr;
        Check(CE_HandoffParse(buf, n, now, &parsed, &err) == 1 && err == nullptr, "legacy v0 parse succeeds");
        Check(parsed.online_version == 0, "legacy v0 parsed online_version == 0");
        Check(parsed.kind == CE_HANDOFF_LAUNCH && parsed.role == 1 && parsed.level == 856, "legacy v0 fields match");
        Check(std::memcmp(parsed.secret, h.secret, 32) == 0, "legacy v0 secret matches");
        Check(std::strcmp(parsed.args, "--windowed") == 0, "legacy v0 args match");
    }

    // 1b. Online version == 1 roundtrip (4-slot mixed 3 Human + 1 AI, Forseti/Muspell, require_relay=1 & 0)
    for (uint8_t relay = 0; relay <= 1; ++relay) {
        CE_Handoff h = {};
        h.kind = CE_HANDOFF_LAUNCH;
        h.role = 2;
        h.level = 850;
        h.created = now - 3;
        std::snprintf(h.args, sizeof(h.args), "--windowed --lang=en");
        h.online_version = 1;
        h.online_ticket = MakeTicket(4, 3, 2, 850, relay);

        char buf[CE_HANDOFF_MAX_BYTES] = {};
        const size_t n = CE_HandoffFormat(&h, buf, sizeof(buf));
        Check(n > 0 && n < CE_HANDOFF_MAX_BYTES, "online v1 format fits inside CE_HANDOFF_MAX_BYTES");
        Check(std::strncmp(buf, "onslaught-ce handoff online 1\n", 30) == 0, "online v1 uses versioned header");
        Check(std::strstr(buf, "turn_") == nullptr && std::strstr(buf, "socket") == nullptr,
              "online v1 carries zero TURN credentials or raw sockets");

        CE_Handoff parsed = {};
        const char *err = nullptr;
        Check(CE_HandoffParse(buf, n, now, &parsed, &err) == 1 && err == nullptr, "online v1 parse succeeds");
        Check(parsed.online_version == 1, "online v1 parsed version == 1");
        Check(parsed.kind == CE_HANDOFF_LAUNCH && parsed.role == 2 && parsed.level == 850, "online v1 launch fields");
        Check(std::strcmp(parsed.args, h.args) == 0, "online v1 args preserved");
        Check(TicketsEqual(parsed.online_ticket, h.online_ticket), "online v1 ticket roundtrips bit-for-bit");
        Check(parsed.online_ticket.require_relay == relay, "require_relay preserved");

        // File write/read roundtrip + unlink-on-read check
        std::string path = std::string(scratchDir) + (relay ? "/online_relay.handoff" : "/online_direct.handoff");
        Check(CE_HandoffWrite(path.c_str(), &h) == 1, "CE_HandoffWrite online v1 succeeds");
        CE_Handoff fromFile = {};
        err = nullptr;
        Check(CE_HandoffRead(path.c_str(), now, &fromFile, &err) == 1 && err == nullptr, "CE_HandoffRead online v1 succeeds");
        Check(fromFile.online_version == 1 && TicketsEqual(fromFile.online_ticket, h.online_ticket), "file roundtrip matches");
        // Second read must fail (file unlinked on first read)
        CE_Handoff again = {};
        Check(CE_HandoffRead(path.c_str(), now, &again, &err) == 0, "handoff file unlinked on read");
    }

    // 1c. Online v1 negative controls: truncation, corruption, forbidden keys, invalid levels, expiry, schema
    {
        CE_Handoff h = {};
        h.kind = CE_HANDOFF_LAUNCH;
        h.role = 1;
        h.level = 866;
        h.created = now;
        h.online_version = 1;
        h.online_ticket = MakeTicket(4, 4, 0, 866, 0);

        char buf[CE_HANDOFF_MAX_BYTES] = {};
        const size_t n = CE_HandoffFormat(&h, buf, sizeof(buf));
        Check(n > 100, "valid online v1 handoff formatted");

        // Truncation at multiple offsets
        const size_t cuts[] = { 1, 10, 30, n / 4, n / 2, n - 20, n - 2, n - 1 };
        for (size_t cut : cuts) {
            CE_Handoff out = {};
            const char *err = nullptr;
            Check(CE_HandoffParse(buf, cut, now, &out, &err) == 0 && err != nullptr, "truncated online handoff rejected");
        }

        // Single-byte corruption of hex/fields
        {
            std::string corrupt(buf, n);
            size_t pos = corrupt.find("match_secret=");
            Check(pos != std::string::npos, "match_secret present");
            corrupt[pos + 15] = 'Z'; // non-hex char
            CE_Handoff out = {}; const char *err = nullptr;
            Check(CE_HandoffParse(corrupt.data(), corrupt.size(), now, &out, &err) == 0, "corrupt hex rejected");
        }
        {
            std::string corrupt(buf, n);
            size_t pos = corrupt.find("require_relay=0");
            Check(pos != std::string::npos, "require_relay present");
            corrupt[pos + 14] = '2';
            CE_Handoff out = {}; const char *err = nullptr;
            Check(CE_HandoffParse(corrupt.data(), corrupt.size(), now, &out, &err) == 0, "invalid require_relay=2 rejected");
        }

        // Forbidden / extra lines (TURN credential / socket injection)
        {
            std::string injected(buf, n);
            injected += "turn_password=leaked\n";
            CE_Handoff out = {}; const char *err = nullptr;
            Check(CE_HandoffParse(injected.data(), injected.size(), now, &out, &err) == 0, "injected turn_password rejected");
        }
        {
            std::string injected(buf, n);
            injected += "socket_fd=9\n";
            CE_Handoff out = {}; const char *err = nullptr;
            Check(CE_HandoffParse(injected.data(), injected.size(), now, &out, &err) == 0, "injected socket_fd rejected");
        }

        // Expiry & future clock guards
        {
            CE_Handoff out = {}; const char *err = nullptr;
            Check(CE_HandoffParse(buf, n, now + CE_HANDOFF_MAX_AGE + 1, &out, &err) == 0, "expired online handoff rejected");
            Check(CE_HandoffParse(buf, n, now - 6, &out, &err) == 0, "future online handoff rejected");
        }

        // Invalid levels in format
        const uint32_t badLevels[] = { 0u, 99u, 101u, 110u, 849u, 867u, 999u };
        for (uint32_t badLvl : badLevels) {
            CE_Handoff bad = h;
            bad.level = badLvl;
            bad.online_ticket.level = badLvl;
            char tmp[CE_HANDOFF_MAX_BYTES] = {};
            Check(CE_HandoffFormat(&bad, tmp, sizeof(tmp)) == 0, "format rejects invalid online level");
        }

        // Invalid online_version, mixed campaign+online, AI local slot, duplicate seat, wrong human_count
        {
            CE_Handoff bad = h;
            bad.online_version = 2;
            char tmp[CE_HANDOFF_MAX_BYTES] = {};
            Check(CE_HandoffFormat(&bad, tmp, sizeof(tmp)) == 0, "format rejects unknown online_version=2");

            bad = h;
            bad.campaign.version = 1;
            Check(CE_HandoffFormat(&bad, tmp, sizeof(tmp)) == 0, "format rejects mixed campaign+online");

            bad = h;
            bad.online_ticket = MakeTicket(4, 2, 0, 850, 0);
            bad.online_ticket.local_sim_slot = 3; // slot 3 is AI
            bad.online_ticket.local_seat = 3;
            Check(CE_HandoffFormat(&bad, tmp, sizeof(tmp)) == 0, "format rejects AI local slot");

            bad = h;
            bad.online_ticket.slots[2].seat = bad.online_ticket.slots[1].seat; // dup seat
            Check(CE_HandoffFormat(&bad, tmp, sizeof(tmp)) == 0, "format rejects duplicate seat");

            bad = h;
            bad.online_ticket.human_count = 3; // actually 4 humans
            Check(CE_HandoffFormat(&bad, tmp, sizeof(tmp)) == 0, "format rejects mismatched human_count");

            bad = h;
            bad.online_ticket.slot_count = 1;
            Check(CE_HandoffFormat(&bad, tmp, sizeof(tmp)) == 0, "format rejects slot_count < 2");
        }
    }
}

// ---- Simulated Star Transport implementing ICeOnlineStarBridge ----
class SimStarNetwork {
public:
    struct PeerBridge : public ICeOnlineStarBridge {
        SimStarNetwork *net = nullptr;
        uint8_t simSlot = 0;
        bool connected = true;
        std::deque<CeOnlineStarEvent> inbox;

        void Poll(uint64_t) override {}
        bool IsConnected() const override { return connected; }
        bool SendGuestInput(uint8_t slot, uint32_t tick, const uint8_t *data, size_t len) override {
            if (!connected || len > sizeof(CeOnlineStarEvent::payload)) return false;
            CeOnlineStarEvent ev = {};
            ev.kind = CE_STAR_EVT_GUEST_INPUT;
            ev.from_sim_slot = slot;
            ev.tick = tick;
            ev.payload_len = (uint32_t)len;
            std::memcpy(ev.payload, data, len);
            net->DeliverTo(0, ev);
            return true;
        }
        bool SendHostBundle(uint32_t tick, const uint8_t *bundle, size_t len) override {
            if (!connected || len > sizeof(CeOnlineStarEvent::payload)) return false;
            CeOnlineStarEvent ev = {};
            ev.kind = CE_STAR_EVT_HOST_BUNDLE;
            ev.from_sim_slot = 0;
            ev.tick = tick;
            ev.payload_len = (uint32_t)len;
            std::memcpy(ev.payload, bundle, len);
            net->BroadcastFromHost(ev);
            return true;
        }
        bool SendHash(uint8_t slot, uint32_t tick, uint64_t state_hash) override {
            if (!connected) return false;
            CeOnlineStarEvent ev = {};
            ev.kind = CE_STAR_EVT_HASH;
            ev.from_sim_slot = slot;
            ev.tick = tick;
            ev.hash = state_hash;
            if (simSlot == 0)
                net->BroadcastFromHost(ev);
            else
                net->DeliverTo(0, ev);
            return true;
        }
        bool SendDesync(const CeNPeerDesyncReport &rep) override {
            if (!connected) return false;
            CeOnlineStarEvent ev = {};
            ev.kind = CE_STAR_EVT_DESYNC;
            ev.from_sim_slot = 0;
            ev.tick = rep.tick;
            ev.reference_slot = rep.referenceSlot;
            ev.diverging_slot = rep.divergingSlot;
            ev.diverging_mask = rep.divergingMask;
            ev.expected_hash = rep.expectedHash;
            ev.actual_hash = rep.actualHash;
            net->BroadcastFromHost(ev);
            return true;
        }
        bool SendChat(uint8_t slot, const char *text) override {
            if (!connected || !text) return false;
            CeOnlineStarEvent ev = {};
            ev.kind = CE_STAR_EVT_CHAT;
            ev.from_sim_slot = slot;
            std::snprintf(ev.chat_text, sizeof(ev.chat_text), "%s", text);
            if (simSlot == 0) {
                net->BroadcastFromHost(ev);
            } else {
                net->DeliverTo(0, ev);
            }
            return true;
        }
        bool SendLeave(uint8_t slot) override {
            if (slot == 0) {
                CeOnlineStarEvent ev = {};
                ev.kind = CE_STAR_EVT_HOST_LOST;
                net->BroadcastFromHost(ev);
            } else {
                CeOnlineStarEvent ev = {};
                ev.kind = CE_STAR_EVT_PEER_DISCONNECT;
                ev.from_sim_slot = slot;
                net->DeliverTo(0, ev);
            }
            connected = false;
            return true;
        }
        bool PopEvent(CeOnlineStarEvent *out_event) override {
            if (inbox.empty()) return false;
            *out_event = inbox.front();
            inbox.pop_front();
            return true;
        }
    };

    PeerBridge bridges[CE_ONLINE_MAX_MATCH_SLOTS];
    uint16_t humanCount = 0;

    explicit SimStarNetwork(uint16_t humans) : humanCount(humans) {
        for (uint16_t i = 0; i < CE_ONLINE_MAX_MATCH_SLOTS; ++i) {
            bridges[i].net = this;
            bridges[i].simSlot = (uint8_t)i;
            bridges[i].connected = (i < humans);
        }
    }
    void DeliverTo(uint8_t dst, const CeOnlineStarEvent &ev) {
        if (dst < humanCount && bridges[dst].connected)
            bridges[dst].inbox.push_back(ev);
    }
    void BroadcastFromHost(const CeOnlineStarEvent &ev) {
        for (uint16_t i = 1; i < humanCount; ++i) {
            if (bridges[i].connected)
                bridges[i].inbox.push_back(ev);
        }
    }
    void DisconnectGuest(uint8_t slot) {
        bridges[slot].connected = false;
        CeOnlineStarEvent ev = {};
        ev.kind = CE_STAR_EVT_PEER_DISCONNECT;
        ev.from_sim_slot = slot;
        DeliverTo(0, ev);
    }
    void InjectMetrics(uint8_t dst, uint8_t aboutSlot, uint16_t rttMs, uint8_t route) {
        CeOnlineStarEvent ev = {};
        ev.kind = CE_STAR_EVT_PEER_METRICS;
        ev.from_sim_slot = aboutSlot;
        ev.rtt_ms = rttMs;
        ev.route = route;
        DeliverTo(dst, ev);
    }
};

// Deterministic 4-slot Battle Engine toy world state driven by consumed human inputs + deterministic AI seeds.
struct SimWorld {
    uint16_t slotCount = 4;
    uint32_t matchSeed = 0;
    CeOnlineSlotHashState slots[4] = {};

    void Init(const CE_OnlineMatchTicket &ticket) {
        slotCount = ticket.slot_count;
        matchSeed = ticket.seed;
        for (uint16_t s = 0; s < slotCount; ++s) {
            slots[s] = {};
            slots[s].sim_slot = (uint8_t)s;
            slots[s].kind = ticket.slots[s].kind;
            slots[s].team = ticket.slots[s].team;
            slots[s].alive = 1;
            slots[s].powered_up = 1;
            slots[s].active = 1;
            slots[s].pos_x = 100.0f * (float)(s + 1);
            slots[s].pos_y = 50.0f * (float)(ticket.slots[s].team ? -1 : 1);
            slots[s].pos_z = 10.0f;
            slots[s].life = 100.0f;
            slots[s].energy = 100.0f;
        }
    }

    void Step(uint32_t tick, const CE_WorldInput consumed[4], uint8_t humanMask) {
        for (uint16_t s = 0; s < slotCount; ++s) {
            if ((humanMask & (1u << s)) != 0u) {
                const CE_WorldInput &in = consumed[s];
                slots[s].yaw += in.ground_yaw * 0.25f;
                slots[s].pitch += in.invert_ground ? -in.ground_pitch * 0.25f : in.ground_pitch * 0.25f;
                for (uint32_t a = 0; a < in.count; ++a) {
                    slots[s].vel_x += in.actions[a].value * 0.1f;
                    slots[s].vel_y += (float)(in.actions[a].button & 7u) * 0.05f;
                    if (in.actions[a].button == 59u) slots[s].is_jet ^= 1u;
                }
                slots[s].pos_x += slots[s].vel_x * 0.05f;
                slots[s].pos_y += slots[s].vel_y * 0.05f;
                slots[s].ai_seed = 0u;
            } else {
                // Deterministic AI backfill (either original AI slot OR disconnected human handed over to AI)
                const uint32_t aiSeed = CeOnlineMatchAiSeed(matchSeed, (uint8_t)s, tick);
                slots[s].ai_seed = aiSeed;
                slots[s].yaw += (float)((int)(aiSeed & 0xFFu) - 128) * 0.001f;
                slots[s].pos_x += (float)((int)((aiSeed >> 8) & 0xFFu) - 128) * 0.01f;
                slots[s].pos_y += (float)((int)((aiSeed >> 16) & 0xFFu) - 128) * 0.01f;
                slots[s].weapon_index = (aiSeed >> 24) & 3u;
            }
        }
    }

    uint64_t Hash(uint32_t tick) const {
        return CeOnlineMatchHashAllSlots(tick, matchSeed, slots, slotCount, 0x9E3779B97F4A7C15ULL ^ tick);
    }
};

// ---- Test 2: Full 4-peer 200-tick star lockstep + mixed Human/AI parity + chat + metrics ----
static void TestFourPeerStarLockstep200Ticks() {
    SimStarNetwork net(4);
    CeOnlineMatchSession peers[4];
    SimWorld worlds[4];

    for (uint8_t s = 0; s < 4; ++s) {
        CE_OnlineMatchTicket tk = MakeTicket(4, 4, s, 850, 0);
        Check(peers[s].Init(tk, &net.bridges[s], 3u, 20u), "init 4p session");
        Check(peers[s].IsReady(), "4p session ready");
        worlds[s].Init(tk);
    }

    // Metrics & chat test
    net.InjectMetrics(0, 1, 24u, CE_ONLINE_ROUTE_DIRECT);
    net.InjectMetrics(0, 2, 68u, CE_ONLINE_ROUTE_RELAY);
    Check(peers[2].SendChat("Hello\x07 squad!"), "guest 2 sends chat");
    for (uint8_t s = 0; s < 4; ++s) Check(peers[s].Poll(100), "poll chat step 1");
    for (uint8_t s = 0; s < 4; ++s) Check(peers[s].Poll(105), "poll chat step 2");
    Check(peers[0].SlotRttMs(1) == 24u && peers[0].SlotRoute(1) == CE_ONLINE_ROUTE_DIRECT, "slot 1 direct metrics");
    Check(peers[0].SlotRttMs(2) == 68u && peers[0].SlotRoute(2) == CE_ONLINE_ROUTE_RELAY, "slot 2 relay metrics");
    Check(peers[0].ChatCount() == 1 && peers[1].ChatCount() == 1 && peers[3].ChatCount() == 1, "chat relayed to all peers");
    CeOnlineChatEntry chatEntry = {};
    Check(peers[3].GetChat(0, &chatEntry) && chatEntry.from_sim_slot == 2 &&
          std::strcmp(chatEntry.text, "Hello squad!") == 0, "sanitized chat preserved across relay");

    // Tick 0 start hash
    for (uint8_t s = 0; s < 4; ++s)
        Check(peers[s].SubmitLocalHash(0, worlds[s].Hash(0)), "submit tick 0 hash");
    for (uint8_t s = 0; s < 4; ++s) Check(peers[s].Poll(200), "poll tick 0 hash");
    for (uint8_t s = 0; s < 4; ++s) Check(peers[s].Poll(205), "poll tick 0 confirmation");
    Check(peers[0].HashesVerified() == 1u, "tick 0 hash verified across all 4 peers");
    for (uint8_t s = 0; s < 4; ++s) Check(peers[s].IsStartConfirmed(), "tick 0 start barrier confirmed on all 4 peers");

    // 200 full lockstep ticks
    for (uint32_t t = 0; t < 200; ++t) {
        const uint32_t target = t + peers[0].InputDelay();
        for (uint8_t s = 0; s < 4; ++s) {
            CE_WorldInput sample = MakeRichWorldInput(target, s);
            Check(peers[s].SubmitLocalInput(target, sample), "submit local input");
        }
        // Pump host then guests (star relay)
        Check(peers[0].Poll(1000 + t * 50), "host poll inputs");
        for (uint8_t s = 1; s < 4; ++s)
            Check(peers[s].Poll(1000 + t * 50), "guest poll bundle");

        CE_WorldInput consumed[4][4] = {};
        uint8_t masks[4] = {};
        for (uint8_t s = 0; s < 4; ++s) {
            Check(peers[s].CanAdvance(), "peer can advance");
            Check(peers[s].ConsumeTick(consumed[s], &masks[s]), "peer consumes tick");
            Check(masks[s] == 0x0Fu, "all 4 human slots present");
            worlds[s].Step(t, consumed[s], masks[s]);
        }
        // Verify bit-for-bit identical CE_WorldInput on all 4 peers for all 4 slots!
        for (uint8_t peer = 1; peer < 4; ++peer) {
            for (uint8_t slot = 0; slot < 4; ++slot) {
                Check(WorldInputsBitEqual(consumed[0][slot], consumed[peer][slot]),
                      "lossless CE_WorldInput bit-identical across all 4 peers");
            }
        }
        if (t >= 3) {
            for (uint8_t slot = 0; slot < 4; ++slot) {
                CE_WorldInput expected = MakeRichWorldInput(t, slot);
                Check(WorldInputsBitEqual(consumed[0][slot], expected),
                      "consumed input matches original sampled input bit-for-bit");
            }
        }
        const uint32_t nextTick = t + 1;
        if ((nextTick % 20u) == 0u) {
            for (uint8_t s = 0; s < 4; ++s)
                Check(peers[s].SubmitLocalHash(nextTick, worlds[s].Hash(nextTick)), "submit periodic hash");
            Check(peers[0].Poll(1000 + t * 50 + 10), "host verifies periodic hash");
            for (uint8_t s = 1; s < 4; ++s)
                Check(peers[s].Poll(1000 + t * 50 + 10), "guest polls after hash");
        }
    }

    Check(peers[0].CurrentTick() == 200u, "host reached tick 200");
    for (uint8_t s = 1; s < 4; ++s)
        Check(peers[s].CurrentTick() == 200u && peers[s].State() == CE_ONLINE_STATE_RUNNING, "guest reached tick 200 clean");
    Check(peers[0].HashesVerified() == 11u, "all 11 periodic hashes (0,20..200) verified");
    Check(worlds[0].Hash(200) == worlds[3].Hash(200), "final 4-slot world hashes match identically");

    // Deterministic AI backfill seed parity & mixed 2-Human + 2-AI 120-tick match
    Check(CeOnlineMatchAiSeed(0x12345678u, 2, 40) == CeOnlineMatchAiSeed(0x12345678u, 2, 40), "AI seed deterministic");
    Check(CeOnlineMatchAiSeed(0x12345678u, 2, 40) != CeOnlineMatchAiSeed(0x12345678u, 3, 40), "AI seed differs across slots");
    Check(CeOnlineMatchAiSeed(0x12345678u, 2, 40) != CeOnlineMatchAiSeed(0x12345678u, 2, 41), "AI seed differs across ticks");
    Check(CeOnlineMatchAiSeed(0x12345678u, 2, 40) != CeOnlineMatchAiSeed(0x87654321u, 2, 40), "AI seed differs across match seeds");

    {
        SimStarNetwork mixNet(2);
        CeOnlineMatchSession mixPeers[2];
        SimWorld mixWorlds[2];
        for (uint8_t s = 0; s < 2; ++s) {
            CE_OnlineMatchTicket tk = MakeTicket(4, 2, s, 858, 1);
            Check(mixPeers[s].Init(tk, &mixNet.bridges[s], 2u, 20u), "init mixed 2H+2AI session");
            mixWorlds[s].Init(tk);
        }
        for (uint32_t t = 0; t < 120; ++t) {
            const uint32_t target = t + 2u;
            for (uint8_t s = 0; s < 2; ++s)
                Check(mixPeers[s].SubmitLocalInput(target, MakeRichWorldInput(target, s)), "mix submit");
            Check(mixPeers[0].Poll(2000 + t * 50), "mix host poll");
            Check(mixPeers[1].Poll(2000 + t * 50), "mix guest poll");
            for (uint8_t s = 0; s < 2; ++s) {
                CE_WorldInput out[4] = {};
                uint8_t mask = 0;
                Check(mixPeers[s].ConsumeTick(out, &mask) && mask == 0x03u, "mix consume human mask 0x03");
                mixWorlds[s].Step(t, out, mask);
            }
            if (((t + 1) % 20u) == 0u) {
                for (uint8_t s = 0; s < 2; ++s)
                    Check(mixPeers[s].SubmitLocalHash(t + 1, mixWorlds[s].Hash(t + 1)), "mix submit hash");
                Check(mixPeers[0].Poll(2000 + t * 50 + 5), "mix host verify hash");
            }
        }
        Check(mixPeers[0].HashesVerified() == 6u, "mixed 2H+2AI verified all 6 hashes across all 4 slots");
        Check(mixWorlds[0].slots[2].pos_x == mixWorlds[1].slots[2].pos_x &&
              mixWorlds[0].slots[3].ai_seed == mixWorlds[1].slots[3].ai_seed,
              "AI backfill slots 2 & 3 evolved identically on both peers");
    }
}

// ---- Test 3: Desync detection & consensus attribution (guest, host, AND AI slot) ----
static void TestDesyncDetectionAndAttribution() {
    // 3a. Guest slot 2 diverges at tick 60
    {
        SimStarNetwork net(4);
        CeOnlineMatchSession peers[4];
        SimWorld worlds[4];
        for (uint8_t s = 0; s < 4; ++s) {
            CE_OnlineMatchTicket tk = MakeTicket(4, 4, s, 852, 0);
            Check(peers[s].Init(tk, &net.bridges[s], 2u, 20u), "desync 3a init");
            worlds[s].Init(tk);
        }
        for (uint32_t t = 0; t < 60; ++t) {
            const uint32_t target = t + 2u;
            for (uint8_t s = 0; s < 4; ++s) peers[s].SubmitLocalInput(target, MakeRichWorldInput(target, s));
            peers[0].Poll(t * 50);
            for (uint8_t s = 1; s < 4; ++s) peers[s].Poll(t * 50);
            for (uint8_t s = 0; s < 4; ++s) {
                CE_WorldInput out[4] = {}; uint8_t m = 0;
                peers[s].ConsumeTick(out, &m);
                worlds[s].Step(t, out, m);
            }
        }
        // Inject intentional 1-slot drift on guest 2 only
        worlds[2].slots[2].pos_x += 0.125f;
        for (uint8_t s = 0; s < 4; ++s)
            peers[s].SubmitLocalHash(60, worlds[s].Hash(60));
        Check(!peers[0].Poll(4000), "host detects guest 2 desync");
        Check(peers[0].State() == CE_ONLINE_STATE_DESYNC, "host in DESYNC state");
        Check(peers[0].LastDesync().tick == 60u &&
              peers[0].LastDesync().referenceSlot == 0u &&
              peers[0].LastDesync().divergingSlot == 2u &&
              peers[0].LastDesync().divergingMask == (uint8_t)(1u << 2),
              "host attributes exact desync to sim_slot 2 at tick 60");
        // Guests receive broadcast desync alert
        for (uint8_t s = 1; s < 4; ++s) {
            Check(!peers[s].Poll(4005), "guest receives desync broadcast");
            Check(peers[s].State() == CE_ONLINE_STATE_DESYNC &&
                  peers[s].LastDesync().divergingSlot == 2u,
                  "guest matches exact desync attribution");
        }
    }

    // 3b. Host slot 0 diverges at tick 40 -> outvoted by Guests 1, 2, 3!
    {
        SimStarNetwork net(4);
        CeOnlineMatchSession peers[4];
        SimWorld worlds[4];
        for (uint8_t s = 0; s < 4; ++s) {
            CE_OnlineMatchTicket tk = MakeTicket(4, 4, s, 854, 0);
            Check(peers[s].Init(tk, &net.bridges[s], 2u, 20u), "desync 3b init");
            worlds[s].Init(tk);
        }
        worlds[0].slots[0].life -= 1.0f; // Host drifts!
        for (uint8_t s = 0; s < 4; ++s)
            peers[s].SubmitLocalHash(40, worlds[s].Hash(40));
        Check(!peers[0].Poll(5000), "host detects own divergence via majority vote");
        Check(peers[0].State() == CE_ONLINE_STATE_DESYNC, "3b DESYNC state");
        Check(peers[0].LastDesync().referenceSlot == 1u &&
              peers[0].LastDesync().divergingSlot == 0u &&
              peers[0].LastDesync().divergingMask == 0x01u,
              "majority vote attributes divergence to HOST slot 0");
    }

    // 3c. Mixed 3-Human + 1-AI: AI slot 3 diverges on Guest slot 2 ONLY -> caught!
    {
        SimStarNetwork net(3);
        CeOnlineMatchSession peers[3];
        SimWorld worlds[3];
        for (uint8_t s = 0; s < 3; ++s) {
            CE_OnlineMatchTicket tk = MakeTicket(4, 3, s, 860, 0);
            Check(peers[s].Init(tk, &net.bridges[s], 2u, 20u), "desync 3c init");
            worlds[s].Init(tk);
        }
        worlds[2].slots[3].life -= 5.0f; // Mutate ONLY AI slot 3 on Guest 2!
        for (uint8_t s = 0; s < 3; ++s)
            peers[s].SubmitLocalHash(20, worlds[s].Hash(20));
        Check(!peers[0].Poll(6000), "AI-slot divergence on guest 2 caught");
        Check(peers[0].State() == CE_ONLINE_STATE_DESYNC &&
              peers[0].LastDesync().divergingSlot == 2u,
              "AI-slot divergence attributed to guest 2");
    }
}

// ---- Test 4: Mid-match guest disconnect & graceful continuation + host loss ----
static void TestDisconnectAndHostLoss() {
    // 4a. 4-human match drops guest 3 mid-match at tick 50 -> remaining 3 peers run cleanly to tick 200
    {
        SimStarNetwork net(4);
        CeOnlineMatchSession peers[4];
        SimWorld worlds[4];
        for (uint8_t s = 0; s < 4; ++s) {
            CE_OnlineMatchTicket tk = MakeTicket(4, 4, s, 855, 0);
            Check(peers[s].Init(tk, &net.bridges[s], 3u, 20u), "disc 4a init");
            worlds[s].Init(tk);
        }

        uint32_t takeoverTick[3] = { 9999u, 9999u, 9999u };
        for (uint32_t t = 0; t < 200; ++t) {
            if (t == 50) {
                net.DisconnectGuest(3);
                Check(peers[0].Poll(5000 + t * 50), "host processes slot 3 disconnect cleanly");
            }
            const uint32_t target = t + 3u;
            const uint8_t live = (t < 50) ? 4u : 3u;
            for (uint8_t s = 0; s < live; ++s) {
                Check(peers[s].SubmitLocalInput(target, MakeRichWorldInput(target, s)), "disc submit");
            }
            Check(peers[0].Poll(5000 + t * 50 + 5), "disc host poll");
            for (uint8_t s = 1; s < live; ++s)
                Check(peers[s].Poll(5000 + t * 50 + 5), "disc guest poll");

            uint8_t masks[3] = {};
            for (uint8_t s = 0; s < live; ++s) {
                Check(peers[s].CanAdvance(), "surviving peer can advance");
                CE_WorldInput out[4] = {};
                uint8_t mask = 0;
                Check(peers[s].ConsumeTick(out, &mask), "surviving peer consumes tick");
                if (s < 3) {
                    masks[s] = mask;
                    if (!peers[s].IsSlotHumanNow(3) && takeoverTick[s] == 9999u)
                        takeoverTick[s] = t;
                }
                worlds[s].Step(t, out, mask);
            }
            Check(masks[0] == masks[1] && masks[0] == masks[2],
                  "all surviving peers consume exact same human mask across disconnect transition");

            const uint32_t nextTick = t + 1;
            if ((nextTick % 20u) == 0u) {
                for (uint8_t s = 0; s < live; ++s)
                    Check(peers[s].SubmitLocalHash(nextTick, worlds[s].Hash(nextTick)), "disc submit hash");
                Check(peers[0].Poll(5000 + t * 50 + 10), "disc host verify hash");
            }
        }
        Check(takeoverTick[0] != 9999u &&
              takeoverTick[0] == takeoverTick[1] &&
              takeoverTick[0] == takeoverTick[2],
              "AI takeover of disconnected slot 3 happens on exact same tick on all 3 surviving peers");
        Check(peers[0].CurrentTick() == 200u && peers[1].CurrentTick() == 200u && peers[2].CurrentTick() == 200u,
              "all 3 surviving peers reached tick 200 after guest 3 disconnect");
        Check(peers[0].HashesVerified() == 10u, "periodic hashes verified smoothly across disconnect (20..200)");
        Check(worlds[0].Hash(200) == worlds[1].Hash(200) && worlds[0].Hash(200) == worlds[2].Hash(200),
              "all 3 surviving peers end tick 200 with identical 4-slot world hash (slot 3 AI backfill in sync)");
    }

    // 4b. 2-human + 2-AI match dropping guest 1 -> TOO_FEW_PEERS
    {
        SimStarNetwork net(2);
        CeOnlineMatchSession host;
        CE_OnlineMatchTicket tk = MakeTicket(4, 2, 0, 856, 0);
        Check(host.Init(tk, &net.bridges[0], 3u, 20u), "2H init");
        net.DisconnectGuest(1);
        Check(!host.Poll(1000), "2H host stops when only 1 human remains");
        Check(host.State() == CE_ONLINE_STATE_TOO_FEW_PEERS, "state == TOO_FEW_PEERS");
    }

    // 4c. Host loss on guest
    {
        SimStarNetwork net(3);
        CeOnlineMatchSession guest;
        CE_OnlineMatchTicket tk = MakeTicket(3, 3, 1, 862, 0);
        Check(guest.Init(tk, &net.bridges[1], 3u, 20u), "host-loss guest init");
        net.bridges[0].SendLeave(0);
        Check(!guest.Poll(1000), "guest detects host loss");
        Check(guest.State() == CE_ONLINE_STATE_HOST_LOST, "state == HOST_LOST");
        Check(!guest.CanAdvance(), "guest refuses advance after host loss");
        Check(!guest.SubmitLocalInput(3, MakeRichWorldInput(3, 1)), "guest refuses input after host loss");
    }
}

// ---- Test 5: Compatibility levels & C hooks ----
static void TestCompatibilityAndCHooks() {
    Check(CE_COMPAT_ONLINE_MULTIBE_V1 == 5, "CE_COMPAT_ONLINE_MULTIBE_V1 == 5");
    Check(CE_CompatibilityOnlineLevel(100u) == 1, "online admits Tutorial 100");
    for (uint32_t lvl = 850u; lvl <= 866u; ++lvl) {
        Check(CE_CompatibilityOnlineLevel(lvl) == 1, "online admits MP/BD level 850..866");
    }
    const uint32_t invalidLevels[] = { 0u, 1u, 99u, 101u, 110u, 200u, 849u, 867u, 999u, 0xFFFFFFFFu };
    for (uint32_t lvl : invalidLevels) {
        Check(CE_CompatibilityOnlineLevel(lvl) == 0, "online refuses non-admitted level");
    }
    // Legacy lockstep level set unchanged
    Check(CE_CompatibilityLockstepLevel(100u) == 1 &&
          CE_CompatibilityLockstepLevel(856u) == 1 &&
          CE_CompatibilityLockstepLevel(858u) == 1, "legacy lockstep admits 100, 856, 858");
    Check(CE_CompatibilityLockstepLevel(850u) == 0 &&
          CE_CompatibilityLockstepLevel(852u) == 0 &&
          CE_CompatibilityLockstepLevel(866u) == 0 &&
          CE_CompatibilityLockstepLevel(110u) == 0, "legacy lockstep refuses other maps");

    // C hooks
    PortCeOnlineMatchClear();
    Check(PortCeOnlineMatchActive() == 0, "initially inactive");
    CE_OnlineMatchTicket tk = MakeTicket(4, 3, 2, 864, 1);
    Check(PortCeOnlineMatchSetTicket(&tk) == 1, "set valid ticket");
    Check(PortCeOnlineMatchActive() == 1, "active after set");
    Check(PortCeOnlineMatchSlotCount() == 4, "slot count == 4");
    Check(PortCeOnlineMatchLocalSlot() == 2, "local slot == 2");
    Check(PortCeOnlineMatchLevel() == 864u, "level == 864");
    Check(PortCeOnlineMatchIsHost() == 0, "slot 2 is guest");
    Check(PortCeOnlineMatchSeed() == tk.seed, "seed matches");
    Check(PortCeOnlineMatchSlotIsHuman(0) == 1 &&
          PortCeOnlineMatchSlotIsHuman(1) == 1 &&
          PortCeOnlineMatchSlotIsHuman(2) == 1 &&
          PortCeOnlineMatchSlotIsHuman(3) == 0, "human/AI flags match");
    Check(PortCeOnlineMatchSlotTeam(0) == CE_ONLINE_TEAM_FORSETI &&
          PortCeOnlineMatchSlotTeam(1) == CE_ONLINE_TEAM_MUSPELL &&
          PortCeOnlineMatchSlotTeam(2) == CE_ONLINE_TEAM_FORSETI &&
          PortCeOnlineMatchSlotTeam(3) == CE_ONLINE_TEAM_MUSPELL, "teams match");
    Check(std::strcmp(PortCeOnlineMatchSlotBeCfg(2), "Forseti Recon") == 0, "be_cfg matches");
    Check(std::strcmp(PortCeOnlineMatchSlotBeCfg(99), "") == 0, "out-of-range be_cfg returns empty string");

    // Invalid ticket rejected cleanly without keeping stale state
    CE_OnlineMatchTicket bad = tk;
    bad.level = 110;
    Check(PortCeOnlineMatchSetTicket(&bad) == 0, "invalid ticket refused");
    Check(PortCeOnlineMatchActive() == 0, "failed SetTicket clears active state");

    PortCeOnlineMatchClear();
    Check(PortCeOnlineMatchActive() == 0, "clear resets active");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::puts("usage: driver <scratch-dir>");
        return 2;
    }
    TestHandoffRoundtripAndNegatives(argv[1]);
    TestFourPeerStarLockstep200Ticks();
    TestDesyncDetectionAndAttribution();
    TestDisconnectAndHostLoss();
    TestCompatibilityAndCHooks();
    if (!gFail)
        std::puts("online-match-ok");
    return gFail != 0;
}
"""


class OnlineMatchTests(unittest.TestCase):
    def test_spdx_and_hygiene(self):
        forbidden_home = "/" + "home/"
        forbidden_vartmp = "/" + "var/tmp"
        for rel in (
            "port/game/ce_online_match.h",
            "port/game/ce_online_match.cpp",
            "port/tools/ce_online_match_tests.py",
        ):
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:250], rel)
            self.assertNotIn(forbidden_home, text, rel)
            self.assertNotIn(forbidden_vartmp, text, rel)
        for rel in ("port/game/ce_online_match.h", "port/game/ce_online_match.cpp"):
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertNotIn("BEA_", text, rel)

    def test_online_match_asan_ubsan(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        base = Path(tempfile.mkdtemp(prefix="online-match-", dir=SCRATCH))
        try:
            driver = base / "driver.cpp"
            driver.write_text(DRIVER, encoding="utf-8")
            sha_obj = base / "sha256.o"
            exe = base / "driver"

            cc = shutil.which("clang") or shutil.which("gcc") or "cc"
            cxx = shutil.which("clang++") or shutil.which("g++") or "c++"

            c_cmd = [
                cc, "-std=c99", "-O1", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-c", str(PORT / "updater/sha256.c"), "-o", str(sha_obj),
            ]
            c_res = subprocess.run(c_cmd, capture_output=True, text=True, timeout=60)
            self.assertEqual(c_res.returncode, 0, c_res.stdout + c_res.stderr)

            cxx_cmd = [
                cxx, "-std=c++17", "-O1", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Werror",
                "-I", str(PORT / "game"),
                "-I", str(PORT / "network"),
                "-I", str(PORT / "platform/common"),
                str(driver),
                str(PORT / "game/ce_online_match.cpp"),
                str(PORT / "platform/common/ce_netcode.cpp"),
                str(sha_obj),
                "-o", str(exe),
            ]
            cxx_res = subprocess.run(cxx_cmd, capture_output=True, text=True, timeout=120)
            self.assertEqual(cxx_res.returncode, 0, cxx_res.stdout + cxx_res.stderr)

            env = {
                k: v for k, v in os.environ.items()
                if not k.startswith("BEA_") and k not in ("DISPLAY", "WAYLAND_DISPLAY")
            }
            run_res = subprocess.run([str(exe), str(base)], capture_output=True, text=True, timeout=60, env=env)
            self.assertEqual(run_res.returncode, 0, run_res.stdout + run_res.stderr)
            self.assertEqual(run_res.stdout.strip(), "online-match-ok", run_res.stdout + run_res.stderr)
        finally:
            shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main()
