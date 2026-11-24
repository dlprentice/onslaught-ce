#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for port/network/star_transport.{h,cpp} and multi-slot CeIceBridge in ice_bridge.cpp:
real 1 Host + 3 Guests = 4-peer ENet star session (Linux native ASan/UBSan, Linux32 sniper,
Windows32 llvm-mingw + Wine), authenticated AEAD handshake, wrong-secret & wrong-compat rejection,
4-peer CeNPeerLockstepRing input bundle & chat roundtrip, live RTT reporting, guest disconnect
survival, multi-slot CeIceBridge loopback multiplexing on one ENetHost, and causal mutants.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
DATA = Path(os.environ.get("BEA_PORT_DATA", ROOT / "local-data/port"))
MONO_SRC = DATA / "src/monocypher-4.0.2/src"
ENET_SRC = DATA / "src/enet-5a9c537fd464b3c6d3c55e1d3bd47588faf71b42"
LLVM_BIN = DATA / "llvm/bin"
SYSROOT = DATA / "sysroot-sniper"
MINGW_BIN = Path(os.environ.get("BEA_LLVM_MINGW", DATA / "llvm-mingw/bin"))


def find_ice_prefix(target_sub: str) -> Path | None:
    ice_root = DATA / "ice-relay"
    if not ice_root.is_dir():
        return None
    for candidate in sorted(ice_root.glob(f"*/{target_sub}")):
        if (candidate / "lib/libjuice.a").is_file() and (candidate / "include/juice/juice.h").is_file():
            return candidate
    return None


def find_ice_src_root() -> Path | None:
    src_root = DATA / "ice-src-relay"
    if not src_root.is_dir():
        return None
    for candidate in sorted(src_root.glob("*/libjuice-*")):
        if (candidate / "include/juice/juice.h").is_file() and (candidate / "src/juice.c").is_file():
            return candidate
    return None


DRIVER_CPP = r"""
// SPDX-License-Identifier: GPL-3.0-only
#include "star_transport.h"
#include "npeer_ring.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
int gChecks = 0;
int gFails = 0;

void Check(bool ok, const char* msg) {
    ++gChecks;
    if (!ok) {
        ++gFails;
        std::fprintf(stderr, "FAIL: %s\n", msg);
    }
}

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

template <typename Pred>
bool WaitUntil(Pred&& pred, uint32_t max_ms = 5000) {
    const uint64_t end = NowMs() + max_ms;
    while (NowMs() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

std::array<uint8_t, 32> MakeSecret(uint8_t seed) {
    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(seed + i * 7 + 1);
    return out;
}

std::array<uint8_t, 32> MakeCompat(uint8_t seed) {
    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(seed ^ (i * 13 + 3));
    return out;
}

void TestWireAnd64Bitmask() {
    // 64-slot bitmask encode/decode.
    const uint64_t mask64 = (1ULL << 0) | (1ULL << 3) | (1ULL << 31) | (1ULL << 63);
    const CeStarBitmask bm64 = CeStarBitmask::FromMask64(mask64, 64);
    std::vector<uint8_t> buf;
    Check(bm64.Encode(buf) && buf.size() == 2 + 8, "64-slot bitmask encodes to 10 bytes");
    size_t pos = 0;
    CeStarBitmask dec;
    Check(CeStarBitmask::Decode(buf.data(), buf.size(), pos, dec) && pos == buf.size() &&
          dec.nbits == 64 && dec.bits == mask64 && dec.Test(63) && !dec.Test(62),
          "64-slot bitmask decodes identically");

    // 4-slot bitmask interoperability with CeNPeerLockstepRing uint8_t mask.
    const CeStarBitmask bm4 = CeStarBitmask::FromMask8(0x0Fu, 4);
    buf.clear();
    Check(bm4.Encode(buf) && buf.size() == 3, "4-slot bitmask encodes to 3 bytes");
    pos = 0;
    Check(CeStarBitmask::Decode(buf.data(), buf.size(), pos, dec) && dec.ToMask8() == 0x0Fu,
          "4-slot bitmask roundtrips to uint8 mask");
    // Non-zero unused top bits must be rejected.
    buf[2] = 0x1Fu;
    pos = 0;
    Check(!CeStarBitmask::Decode(buf.data(), buf.size(), pos, dec), "non-zero padding bits rejected");

    // Extensible frame header & unknown TLV skipping.
    std::vector<uint8_t> payload = {0xAA, 0xBB};
    Check(CeStarWire::AppendTlv(payload, 999, reinterpret_cast<const uint8_t*>("future"), 6), "append TLV");
    std::vector<uint8_t> frame;
    Check(CeStarWire::EncodeFrame(CeStarMsgType::Leave, payload.data(), payload.size(), frame), "encode frame");
    uint16_t proto = 0, len = 0;
    CeStarMsgType type{};
    const uint8_t* out_p = nullptr;
    Check(CeStarWire::DecodeFrame(frame.data(), frame.size(), proto, type, out_p, len) &&
          proto == CE_STAR_PROTO_VERSION && type == CeStarMsgType::Leave && len == payload.size(),
          "decode frame header");
    size_t tpos = 2;
    Check(CeStarWire::SkipTrailingTlvs(out_p, len, tpos) && tpos == len, "unknown TLV skipped cleanly");
    // Truncated TLV rejected.
    std::vector<uint8_t> bad_tlv = {0x00, 0x01, 0x00, 0x05, 0x11};
    tpos = 0;
    Check(!CeStarWire::SkipTrailingTlvs(bad_tlv.data(), bad_tlv.size(), tpos), "truncated TLV rejected");
}

void TestFourPeerDirectStarSession() {
    const auto secret = MakeSecret(0x42);
    const auto compat = MakeCompat(0xA5);
    const auto roster = MakeCompat(0x5A);

    CeStarConfig hcfg;
    hcfg.role = CeStarRole::Host;
    hcfg.bind_ipv4 = "127.0.0.1";
    hcfg.bind_port = 0;
    hcfg.max_guests = 3;
    hcfg.local_seat = 0;
    hcfg.local_sim_slot = 0;
    hcfg.match_secret = secret;
    hcfg.compatibility = compat;
    hcfg.roster_hash = roster;
    hcfg.ping_report_interval_ms = 200;
    hcfg.expected_guests = {{1, 1}, {2, 2}, {3, 3}};

    CeStarError err{};
    auto host = CeStarTransport::Create(hcfg, &err);
    Check(bool(host) && err == CeStarError::Ok && host->LocalPort() != 0, "Host created on ephemeral port");
    if (!host) return;

    auto make_guest = [&](uint16_t slot, uint16_t seat,
                          std::array<uint8_t, 32> sec, std::array<uint8_t, 32> cmp) {
        CeStarConfig gcfg;
        gcfg.role = CeStarRole::Guest;
        gcfg.remote_ipv4 = "127.0.0.1";
        gcfg.remote_port = host->LocalPort();
        gcfg.max_guests = 3;
        gcfg.local_seat = seat;
        gcfg.local_sim_slot = slot;
        gcfg.match_secret = sec;
        gcfg.compatibility = cmp;
        gcfg.roster_hash = roster;
        gcfg.handshake_timeout_ms = 600;
        return CeStarTransport::Create(gcfg, &err);
    };

    // 1. Wrong secret guest is rejected without affecting host.
    {
        auto bad_sec = make_guest(1, 1, MakeSecret(0x99), compat);
        Check(bool(bad_sec), "wrong-secret guest created");
        Check(WaitUntil([&] {
            const uint64_t now = NowMs();
            host->Poll(now);
            bad_sec->Poll(now);
            return host->RejectedPackets() > 0 && bad_sec->IsHostLost();
        }, 2500), "wrong-secret guest rejected and timed out");
        Check(host->ActiveGuestCount() == 0, "wrong-secret guest never admitted");
    }

    // 2. Wrong compatibility guest is rejected with CeStarError::Incompatible.
    {
        auto bad_cmp = make_guest(1, 1, secret, MakeCompat(0x11));
        Check(bool(bad_cmp), "wrong-compat guest created");
        Check(WaitUntil([&] {
            const uint64_t now = NowMs();
            host->Poll(now);
            bad_cmp->Poll(now);
            return bad_cmp->LastError() == CeStarError::Incompatible;
        }, 2500), "wrong-compat guest received Incompatible refusal");
        Check(host->ActiveGuestCount() == 0, "wrong-compat guest never admitted");
    }

    // 3. Wrong seat binding rejected with SlotRejected.
    {
        auto bad_seat = make_guest(1, 3, secret, compat);
        Check(bool(bad_seat), "wrong-seat guest created");
        Check(WaitUntil([&] {
            const uint64_t now = NowMs();
            host->Poll(now);
            bad_seat->Poll(now);
            return bad_seat->LastError() == CeStarError::SlotRejected;
        }, 2500), "wrong-seat guest received SlotRejected");
    }

    // 4. Real 1 Host + 3 Guests = 4-peer authenticated star session!
    std::unique_ptr<CeStarTransport> guests[3];
    for (uint16_t i = 0; i < 3; ++i) {
        guests[i] = make_guest(static_cast<uint16_t>(i + 1), static_cast<uint16_t>(i + 1), secret, compat);
        Check(bool(guests[i]), "valid guest created");
    }

    auto poll_all = [&]() {
        const uint64_t now = NowMs();
        host->Poll(now);
        for (auto& g : guests) if (g) g->Poll(now);
    };

    const bool all_auth = WaitUntil([&] {
        poll_all();
        return host->ActiveGuestCount() == 3 &&
               guests[0]->IsAuthenticated() && guests[1]->IsAuthenticated() && guests[2]->IsAuthenticated();
    }, 3000);
    Check(all_auth, "all 3 guests authenticate simultaneously (4-peer star)");
    if (!all_auth) return;
    Check(host->ActiveMask().ToMask8() == 0x0Fu, "host active mask has all 4 bits set");

    // 5. Real 4-peer CeNPeerLockstepRing input bundle exchange over StarTransport for 20 ticks!
    CeNPeerLockstepRing<4> host_ring;
    CeNPeerLockstepRing<4> guest_rings[3];
    Check(host_ring.Init(4, 0, 0, 10) == CE_NPEER_OK, "host ring init");
    for (uint8_t i = 0; i < 3; ++i)
        Check(guest_rings[i].Init(4, static_cast<uint8_t>(i + 1), 0, 10) == CE_NPEER_OK, "guest ring init");

    for (uint32_t tick = 0; tick < 20; ++tick) {
        CeNPeerInputPayload h_in{tick + 100u, 1u, static_cast<int16_t>(tick), -1, 5, 0};
        Check(host_ring.SubmitInput(0, tick, h_in) == CE_NPEER_OK, "host local input");
        for (uint16_t i = 0; i < 3; ++i) {
            CeNPeerInputPayload g_in{tick + 200u + i, i + 2u, static_cast<int16_t>(tick * 2),
                                     static_cast<int16_t>(i), 10, -5};
            uint8_t enc[CeNPeerInputPayload::kWireBytes];
            g_in.EncodeBE(enc);
            Check(guests[i]->SendGuestInput(tick, enc, sizeof(enc)), "guest sends STAR_INPUT_GUEST");
        }
        // Host collects all 3 guests' inputs for tick.
        Check(WaitUntil([&] {
            poll_all();
            CeStarEvent ev;
            while (host->PopEvent(ev)) {
                if (ev.kind == CeStarEventKind::InputGuest &&
                    ev.payload.size() == CeNPeerInputPayload::kWireBytes) {
                    CeNPeerInputPayload dec{};
                    dec.DecodeBE(ev.payload.data());
                    host_ring.SubmitInput(static_cast<uint8_t>(ev.sim_slot), ev.tick, dec);
                }
            }
            return host_ring.IsTickReady(tick);
        }, 2000), "host received all 3 guests' inputs");

        uint8_t bundle[256];
        size_t blen = 0;
        Check(host_ring.BuildHostBundle(tick, bundle, sizeof(bundle), &blen) == CE_NPEER_OK,
              "host built CeNPeerLockstepRing bundle");
        Check(host->BroadcastInputBundle(tick, CeStarBitmask::FromMask8(host_ring.ActiveMaskAt(tick), 4),
                                         bundle, blen), "host broadcast STAR_INPUT_BUNDLE");

        // All 3 guests receive bundle and advance lockstep tick identically!
        Check(WaitUntil([&] {
            poll_all();
            for (int i = 0; i < 3; ++i) {
                CeStarEvent ev;
                while (guests[i]->PopEvent(ev)) {
                    if (ev.kind == CeStarEventKind::InputBundle) {
                        guest_rings[i].ApplyHostBundle(ev.payload.data(), ev.payload.size());
                    }
                }
            }
            return guest_rings[0].CanAdvance() && guest_rings[1].CanAdvance() && guest_rings[2].CanAdvance();
        }, 2000), "all 3 guests received and applied STAR_INPUT_BUNDLE");

        CeNPeerInputPayload h_out[4], g_out[4];
        Check(host_ring.ConsumeTick(h_out) == CE_NPEER_OK, "host consume tick");
        for (int i = 0; i < 3; ++i) {
            Check(guest_rings[i].ConsumeTick(g_out) == CE_NPEER_OK, "guest consume tick");
            for (int s = 0; s < 4; ++s) {
                if (h_out[s] != g_out[s]) Check(false, "consumed inputs match across all 4 peers");
            }
        }
    }

    // 6. Chat relay: Guest 1 sends chat -> Host, Guest 2, Guest 3 all receive with authoritative sim_slot=1.
    Check(guests[0]->SendChat("Hello 4-player star!"), "guest 1 sends chat");
    bool host_got_chat = false, g2_got_chat = false, g3_got_chat = false;
    bool g1_got_ping = false;
    Check(WaitUntil([&] {
        poll_all();
        CeStarEvent ev;
        while (host->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::Chat && ev.sim_slot == 1 && ev.text == "Hello 4-player star!")
                host_got_chat = true;
        }
        while (guests[0]->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::PingReport && ev.pings.size() == 4) g1_got_ping = true;
        }
        while (guests[1]->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::Chat && ev.sim_slot == 1 && ev.text == "Hello 4-player star!")
                g2_got_chat = true;
        }
        while (guests[2]->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::Chat && ev.sim_slot == 1 && ev.text == "Hello 4-player star!")
                g3_got_chat = true;
        }
        return host_got_chat && g2_got_chat && g3_got_chat && g1_got_ping;
    }, 3000), "4-peer chat relay and live STAR_PING_REPORT delivered");

    // 7. Hash & Desync roundtrip.
    Check(guests[0]->SendHash(10, 0xCAFEBABE12345678ULL), "guest 1 sends hash");
    bool host_got_hash = false;
    Check(WaitUntil([&] {
        poll_all();
        CeStarEvent ev;
        while (host->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::Hash && ev.sim_slot == 1 && ev.tick == 10 &&
                ev.hash == 0xCAFEBABE12345678ULL)
                host_got_hash = true;
        }
        return host_got_hash;
    }, 2000), "host received guest hash");

    Check(host->BroadcastDesync(10, 0, 2, CeStarBitmask::FromMask8(0x04u, 4), 111ULL, 222ULL),
          "host broadcasts desync");
    bool g1_got_desync = false;
    Check(WaitUntil([&] {
        poll_all();
        CeStarEvent ev;
        while (guests[0]->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::Desync && ev.diverging_slot == 2 && ev.actual_hash == 222ULL)
                g1_got_desync = true;
        }
        return g1_got_desync;
    }, 2000), "guest received STAR_DESYNC");

    // 8. Live per-peer RTT & Direct route check.
    CeStarPeerInfo pinfo{};
    Check(host->GetPeerInfo(1, pinfo) && pinfo.authenticated && pinfo.route == CeStarRoute::Direct,
          "host reports live peer info and Direct route");

    // 9. Clean Guest 2 disconnect survival: remaining guests (1 & 3) stay connected and exchange traffic!
    guests[1]->Leave(7);
    bool host_saw_g2_leave = false;
    Check(WaitUntil([&] {
        poll_all();
        CeStarEvent ev;
        while (host->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::PeerDisconnected && ev.sim_slot == 2 && ev.reason == 7)
                host_saw_g2_leave = true;
        }
        return host_saw_g2_leave;
    }, 2500), "host received Guest 2 STAR_LEAVE cleanly");
    guests[1].reset();
    Check(host->ActiveGuestCount() == 2 && host->ActiveMask().ToMask8() == 0x0Bu,
          "host retains slots 0, 1, 3 after Guest 2 leaves");

    // Verify Guest 1 & Guest 3 still communicate cleanly through Host!
    Check(guests[2]->SendChat("Still alive after G2 left!"), "guest 3 sends chat post-disconnect");
    bool g1_got_post = false;
    Check(WaitUntil([&] {
        poll_all();
        CeStarEvent ev;
        while (guests[0]->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::Chat && ev.sim_slot == 3 &&
                ev.text == "Still alive after G2 left!")
                g1_got_post = true;
        }
        return g1_got_post;
    }, 2500), "remaining peers survive Guest 2 disconnect cleanly");

    // 10. Host disconnects Guest 3 while Guest 1 stays alive, then Host leaves.
    Check(host->DisconnectGuest(3, 9), "host boots guest 3");
    Check(WaitUntil([&] {
        poll_all();
        return guests[2]->IsHostLost();
    }, 2500), "guest 3 sees disconnect");
    Check(host->ActiveGuestCount() == 1 && guests[0]->IsAuthenticated(), "guest 1 unaffected by guest 3 kick");

    host->Leave(0);
    Check(WaitUntil([&] {
        poll_all();
        return guests[0]->IsHostLost();
    }, 2500), "guest 1 sees host leave");
}

void TestStartBarrierSilenceKeepalive() {
    // Regression test: connected guests waiting through long start barriers (> silence_timeout_ms)
    // must stay alive via periodic encrypted keepalives while truly frozen peers still time out.
    const auto secret = MakeSecret(0x5A);
    const auto compat = MakeCompat(0xA5);

    CeStarConfig hcfg;
    hcfg.role = CeStarRole::Host;
    hcfg.bind_ipv4 = "127.0.0.1";
    hcfg.bind_port = 0;
    hcfg.max_guests = 3;
    hcfg.local_seat = 0;
    hcfg.local_sim_slot = 0;
    hcfg.match_secret = secret;
    hcfg.compatibility = compat;
    hcfg.silence_timeout_ms = 600;
    hcfg.ping_report_interval_ms = 0; // Keepalive must work in BOTH directions even without PingReport.

    auto host = CeStarTransport::Create(hcfg);
    Check(bool(host), "keepalive test host created");
    if (!host) return;

    auto make_guest = [&](uint16_t slot) {
        CeStarConfig gcfg;
        gcfg.role = CeStarRole::Guest;
        gcfg.remote_ipv4 = "127.0.0.1";
        gcfg.remote_port = host->LocalPort();
        gcfg.max_guests = 3;
        gcfg.local_seat = slot;
        gcfg.local_sim_slot = slot;
        gcfg.match_secret = secret;
        gcfg.compatibility = compat;
        gcfg.silence_timeout_ms = 600;
        return CeStarTransport::Create(gcfg);
    };

    auto g1 = make_guest(1); // Connected & polling while waiting for slow slot 3.
    auto g2 = make_guest(2); // Connects, then freezes (stops polling) and MUST time out.
    Check(bool(g1) && bool(g2), "keepalive test guests created");
    if (!g1 || !g2) return;

    Check(WaitUntil([&] {
        const uint64_t now = NowMs();
        host->Poll(now);
        g1->Poll(now);
        g2->Poll(now);
        return host->ActiveGuestCount() == 2 && g1->IsAuthenticated() && g2->IsAuthenticated();
    }, 3000), "keepalive test guests 1 and 2 authenticated");

    // Drain initial auth events.
    CeStarEvent ev;
    while (host->PopEvent(ev)) {}
    while (g1->PopEvent(ev)) {}

    // Sit idle (zero inputs/chat, slot 3 never joins) for 1100ms (> 1.8x silence_timeout_ms).
    // Poll Host + Guest 1 only; Guest 2 stops polling.
    bool g1_disconnected = false;
    bool g2_timed_out = false;
    const uint64_t start = NowMs();
    while (NowMs() - start < 1100) {
        const uint64_t now = NowMs();
        host->Poll(now);
        g1->Poll(now);
        while (host->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::PeerDisconnected) {
                if (ev.sim_slot == 1) g1_disconnected = true;
                if (ev.sim_slot == 2 && ev.reason == static_cast<uint16_t>(CeStarError::Timeout))
                    g2_timed_out = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CeStarPeerInfo p1{};
    Check(!g1_disconnected && host->GetPeerInfo(1, p1) && p1.authenticated &&
          g1->IsAuthenticated() && !g1->IsHostLost(),
          "polling guest survives idle start barrier beyond silence_timeout_ms via keepalives");
    Check(g2_timed_out && host->ActiveGuestCount() == 1,
          "frozen non-polling guest still cleanly triggers CeStarError::Timeout");
}

#if defined(CE_NETWORK_ICE)
void TestMultiSlotIceLoopbackMultiplexing() {
    const auto secret = MakeSecret(0x77);
    const auto compat = MakeCompat(0x33);

    CeStarConfig hcfg;
    hcfg.role = CeStarRole::Host;
    hcfg.bind_ipv4 = "127.0.0.1";
    hcfg.bind_port = 0;
    hcfg.use_ice = true;
    hcfg.require_relay = false;
    hcfg.max_guests = 3;
    hcfg.local_seat = 0;
    hcfg.local_sim_slot = 0;
    hcfg.match_secret = secret;
    hcfg.compatibility = compat;

    auto host = CeStarTransport::Create(hcfg);
    Check(bool(host), "ICE star host created with single ENetHost peerCount=3");
    if (!host) return;

    juice_config_t jcfg{};
    jcfg.bind_address = "127.0.0.1";

    std::unique_ptr<CeStarTransport> guests[3];
    for (uint16_t i = 0; i < 3; ++i) {
        const uint16_t slot = static_cast<uint16_t>(i + 1);
        Check(host->AttachGuestIceBridge(slot, jcfg, false),
              "host attached independent CeIceBridge on shared multi-peer ENetHost");

        CeStarConfig gcfg;
        gcfg.role = CeStarRole::Guest;
        gcfg.use_ice = true;
        gcfg.require_relay = false;
        gcfg.max_guests = 3;
        gcfg.local_seat = slot;
        gcfg.local_sim_slot = slot;
        gcfg.match_secret = secret;
        gcfg.compatibility = compat;
        guests[i] = CeStarTransport::Create(gcfg);
        Check(bool(guests[i]) && guests[i]->AttachHostIceBridge(jcfg, false),
              "guest created with CeIceBridge");
    }

    // Gather and exchange all 3 independent ICE SDP pairs.
    std::string h_sdp[3], g_sdp[3];
    Check(WaitUntil([&] {
        bool all = true;
        for (uint16_t i = 0; i < 3; ++i) {
            const uint16_t slot = static_cast<uint16_t>(i + 1);
            if (h_sdp[i].empty()) host->GetGuestLocalIceDescription(slot, h_sdp[i]);
            if (g_sdp[i].empty()) guests[i]->GetLocalIceDescription(g_sdp[i]);
            if (h_sdp[i].empty() || g_sdp[i].empty()) all = false;
        }
        return all;
    }, 5000), "all 3 host and 3 guest ICE descriptions gathered");

    for (uint16_t i = 0; i < 3; ++i) {
        const uint16_t slot = static_cast<uint16_t>(i + 1);
        Check(host->SetGuestRemoteIceDescription(slot, g_sdp[i]) &&
              guests[i]->SetRemoteIceDescription(h_sdp[i]),
              "exchanged ICE descriptions for slot");
    }

    auto poll_all = [&]() {
        const uint64_t now = NowMs();
        host->Poll(now);
        for (auto& g : guests) if (g) g->Poll(now);
    };

    Check(WaitUntil([&] {
        poll_all();
        return host->ActiveGuestCount() == 3 &&
               guests[0]->IsAuthenticated() && guests[1]->IsAuthenticated() && guests[2]->IsAuthenticated();
    }, 6000), "all 3 ICE guests multiplexed through ONE Host ENetHost and authenticated!");

    for (uint16_t slot = 1; slot <= 3; ++slot) {
        Check(host->GuestIceRoute(slot) == CeStarRoute::Direct, "host slot ICE route is Direct");
    }

    // Roundtrip chat over 3-slot ICE!
    Check(guests[0]->SendChat("ICE 4-peer works!"), "guest 1 sends chat over multi-slot ICE");
    bool g3_got = false;
    Check(WaitUntil([&] {
        poll_all();
        CeStarEvent ev;
        while (guests[2]->PopEvent(ev)) {
            if (ev.kind == CeStarEventKind::Chat && ev.sim_slot == 1 && ev.text == "ICE 4-peer works!")
                g3_got = true;
        }
        return g3_got;
    }, 4000), "chat relayed across independent CeIceBridges on shared Host ENetHost");

    // Verify require_relay=true still refuses direct-only SDP!
    CeStarConfig rcfg = hcfg;
    rcfg.require_relay = true;
    auto relay_host = CeStarTransport::Create(rcfg);
    Check(bool(relay_host) && relay_host->AttachGuestIceBridge(1, jcfg, true),
          "relay-required host bridge attached");
    Check(!relay_host->SetGuestRemoteIceDescription(1, g_sdp[0]),
          "require_relay=true refuses direct-only SDP");
}
#endif

} // namespace

int main() {
    TestWireAnd64Bitmask();
    TestFourPeerDirectStarSession();
    TestStartBarrierSilenceKeepalive();
#if defined(CE_NETWORK_ICE)
    TestMultiSlotIceLoopbackMultiplexing();
#endif
    std::printf("STAR_TRANSPORT_TESTS checks=%d failures=%d\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
"""


class StarTransportTests(unittest.TestCase):
    def _compile_enet_native(self, cc: str, out_dir: Path, extra: list[str]) -> list[Path]:
        objs = []
        for name in ("callbacks", "compress", "host", "list", "packet", "peer", "protocol", "unix"):
            obj = out_dir / f"enet-{name}.o"
            subprocess.run(
                [cc, "-std=gnu11", "-O2", *extra,
                 "-DHAS_FCNTL=1", "-DHAS_POLL=1", "-DHAS_GETADDRINFO=1", "-DHAS_GETNAMEINFO=1",
                 "-DHAS_GETHOSTBYNAME_R=1", "-DHAS_GETHOSTBYADDR_R=1", "-DHAS_INET_PTON=1",
                 "-DHAS_INET_NTOP=1", "-DHAS_MSGHDR_FLAGS=1", "-DHAS_SOCKLEN_T=1",
                 "-I", str(ENET_SRC / "include"), "-c", str(ENET_SRC / f"{name}.c"), "-o", str(obj)],
                check=True, capture_output=True, text=True, timeout=60,
            )
            objs.append(obj)
        return objs

    def _compile_juice_native(self, cc: str, juice_src: Path, out_dir: Path, extra: list[str]) -> list[Path]:
        objs = []
        for src in sorted((juice_src / "src").glob("*.c")):
            if src.name == "server.c":
                continue
            obj = out_dir / f"juice-{src.stem}.o"
            subprocess.run(
                [cc, "-std=gnu11", "-O2", *extra, "-DJUICE_STATIC", "-DUSE_NETTLE=0", "-DNO_SERVER=1",
                 "-I", str(juice_src / "include"), "-I", str(juice_src / "include/juice"),
                 "-I", str(juice_src / "src"),
                 "-c", str(src), "-o", str(obj)],
                check=True, capture_output=True, text=True, timeout=60,
            )
            objs.append(obj)
        return objs

    def _build_native(self, tmp: Path, star_cpp: Path, with_ice: bool = True, asan: bool = True) -> Path:
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        self.assertTrue(cc and cxx)
        san = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if asan else []
        objs = self._compile_enet_native(cc, tmp, san)
        mono_obj = tmp / "mono.o"
        subprocess.run(
            [cc, "-std=gnu11", "-O2", *san, "-I", str(MONO_SRC), "-c", str(MONO_SRC / "monocypher.c"), "-o", str(mono_obj)],
            check=True, capture_output=True, text=True, timeout=60,
        )
        objs.append(mono_obj)

        ice_flags = []
        extra_cpp = []
        if with_ice:
            juice_src = find_ice_src_root()
            self.assertIsNotNone(juice_src, "patched libjuice source required under BEA_PORT_DATA/ice-src-relay")
            objs += self._compile_juice_native(cc, juice_src, tmp, san)
            ice_flags = ["-DCE_NETWORK_ICE=1", "-DJUICE_STATIC", "-I", str(juice_src / "include")]
            extra_cpp.append(str(PORT / "network/ice_bridge.cpp"))

        driver = tmp / "driver.cpp"
        driver.write_text(DRIVER_CPP)
        exe = tmp / ("star_ice" if with_ice else "star_direct")
        subprocess.run(
            [
                cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread", *san,
                *ice_flags,
                "-I", str(ENET_SRC / "include"), "-I", str(MONO_SRC), "-I", str(PORT / "network"),
                str(star_cpp), *extra_cpp, str(driver), *[str(o) for o in objs],
                "-pthread", "-o", str(exe),
            ],
            check=True, capture_output=True, text=True, timeout=120,
        )
        return exe

    def test_native_4peer_star_and_multislot_ice_asan(self):
        with tempfile.TemporaryDirectory(prefix="ce-star-native-") as td:
            tmp = Path(td)
            # Also verify non-ICE direct-only build compiles cleanly!
            exe_direct = self._build_native(tmp, PORT / "network/star_transport.cpp", with_ice=False, asan=False)
            r_dir = subprocess.run([str(exe_direct)], capture_output=True, text=True, timeout=30)
            self.assertEqual(r_dir.returncode, 0, r_dir.stdout + r_dir.stderr)
            self.assertRegex(r_dir.stdout, r"STAR_TRANSPORT_TESTS checks=[1-9][0-9]* failures=0")

            # Full ICE + 4-peer star + multi-slot CeIceBridge under ASan/UBSan!
            exe_ice = self._build_native(tmp, PORT / "network/star_transport.cpp", with_ice=True, asan=True)
            r_ice = subprocess.run([str(exe_ice)], capture_output=True, text=True, timeout=45)
            self.assertEqual(r_ice.returncode, 0, r_ice.stdout + r_ice.stderr)
            self.assertRegex(r_ice.stdout, r"STAR_TRANSPORT_TESTS checks=[1-9][0-9]* failures=0")

    def test_causal_mutant_compat_check_bypass_caught(self):
        src = (PORT / "network/star_transport.cpp").read_text()
        target = "crypto_verify32(compat, config_.compatibility.data()) != 0 ||"
        self.assertEqual(src.count(target), 1)
        mutated = src.replace(target, "false ||", 1)
        with tempfile.TemporaryDirectory(prefix="ce-star-mut-") as td:
            tmp = Path(td)
            mut_cpp = tmp / "star_mut.cpp"
            mut_cpp.write_text(mutated)
            exe = self._build_native(tmp, mut_cpp, with_ice=False, asan=False)
            res = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertNotEqual(res.returncode, 0, "compat check bypass mutant survived")
            self.assertIn("wrong-compat guest", res.stderr)

    def test_linux32_sniper_cross_compile_and_run(self):
        cc = LLVM_BIN / "clang"
        cxx = LLVM_BIN / "clang++"
        ice = find_ice_prefix("linux-sniper")
        enet_lib = DATA / "prefix-sniper/lib/libenet.a"
        if not (cc.is_file() and cxx.is_file() and SYSROOT.is_dir() and ice and enet_lib.is_file()):
            self.skipTest("pinned Linux32 sniper + libjuice/libenet unavailable")
        with tempfile.TemporaryDirectory(prefix="ce-star-l32-") as td:
            tmp = Path(td)
            flags = ["-m32", f"--sysroot={SYSROOT}"]
            mono_obj = tmp / "mono.o"
            subprocess.run(
                [str(cc), *flags, "-std=gnu11", "-O2", "-I", str(MONO_SRC), "-c",
                 str(MONO_SRC / "monocypher.c"), "-o", str(mono_obj)],
                check=True, capture_output=True, text=True, timeout=60,
            )
            driver = tmp / "driver.cpp"
            driver.write_text(DRIVER_CPP)
            exe = tmp / "star_l32"
            subprocess.run(
                [
                    str(cxx), *flags, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DCE_NETWORK_ICE=1", "-DJUICE_STATIC",
                    "-I", str(DATA / "prefix-sniper/include"),
                    "-I", str(ice / "include"),
                    "-I", str(MONO_SRC), "-I", str(PORT / "network"),
                    str(PORT / "network/ice_bridge.cpp"),
                    str(PORT / "network/star_transport.cpp"),
                    str(driver), str(mono_obj), str(enet_lib), str(ice / "lib/libjuice.a"),
                    f"--ld-path={MINGW_BIN / 'ld.lld'}", "-pthread", "-o", str(exe),
                ],
                check=True, capture_output=True, text=True, timeout=90,
            )
            loader = SYSROOT / "lib/ld-linux.so.2"
            libs = f"{SYSROOT / 'lib/i386-linux-gnu'}:{SYSROOT / 'usr/lib/i386-linux-gnu'}"
            res = subprocess.run(
                [str(loader), "--library-path", libs, str(exe)],
                capture_output=True, text=True, timeout=45,
            )
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertRegex(res.stdout, r"STAR_TRANSPORT_TESTS checks=[1-9][0-9]* failures=0")

    def test_windows32_cross_compile_and_wine_4peer(self):
        cc = MINGW_BIN / "i686-w64-mingw32-clang"
        cxx = MINGW_BIN / "i686-w64-mingw32-clang++"
        ice = find_ice_prefix("windows")
        enet_lib = DATA / "prefix-win32/lib/libenet.a"
        if not (cc.is_file() and cxx.is_file() and ice and enet_lib.is_file()):
            self.skipTest("pinned Windows32 llvm-mingw + libjuice/libenet unavailable")
        with tempfile.TemporaryDirectory(prefix="ce-star-w32-") as td:
            tmp = Path(td)
            mono_obj = tmp / "mono.o"
            subprocess.run(
                [str(cc), "-std=gnu11", "-O2", "-I", str(MONO_SRC), "-c",
                 str(MONO_SRC / "monocypher.c"), "-o", str(mono_obj)],
                check=True, capture_output=True, text=True, timeout=60,
            )
            driver = tmp / "driver.cpp"
            driver.write_text(DRIVER_CPP)
            exe = tmp / "star_w32.exe"
            subprocess.run(
                [
                    str(cxx), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DCE_NETWORK_ICE=1", "-DJUICE_STATIC",
                    "-I", str(DATA / "prefix-win32/include"),
                    "-I", str(ice / "include"),
                    "-I", str(MONO_SRC), "-I", str(PORT / "network"),
                    str(PORT / "network/ice_bridge.cpp"),
                    str(PORT / "network/star_transport.cpp"),
                    str(driver), str(mono_obj), str(enet_lib), str(ice / "lib/libjuice.a"),
                    "-lws2_32", "-lwinmm", "-lbcrypt", "-static-libstdc++", "-static-libgcc",
                    "-o", str(exe),
                ],
                check=True, capture_output=True, text=True, timeout=90,
            )
            wine = shutil.which("wine")
            if not wine:
                return
            prefix = tmp / "wineprefix"
            prefix.mkdir(mode=0o700)
            env = {k: v for k, v in os.environ.items()
                   if k not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
            env.update(
                WINEPREFIX=str(prefix),
                WINEDEBUG="-all",
                WINEDLLOVERRIDES="mscoree,mshtml,winemenubuilder.exe,winepulse.drv,winealsa.drv,wineoss.drv=",
            )
            res = subprocess.run([wine, str(exe)], capture_output=True, text=True, env=env, timeout=120)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertRegex(res.stdout, r"STAR_TRANSPORT_TESTS checks=[1-9][0-9]* failures=0")


if __name__ == "__main__":
    unittest.main(verbosity=2)
