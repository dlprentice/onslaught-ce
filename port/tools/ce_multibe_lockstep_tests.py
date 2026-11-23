#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""N-Peer Lockstep Ring foundation (port/network/npeer_ring.h) ASan/UBSan + causal mutant suite.

    python3 -B port/tools/ce_multibe_lockstep_tests.py [-v]

Verifies:
  1. 2-peer, 3-peer, and 4-peer lockstep tick gating via readyMask & ActiveMaskAt(tick).
  2. Initial input-delay seeding, conflicting-duplicate rejection, and 220+ tick ring wraparound.
  3. Star host-relay TickBundle big-endian wire framing ("CEN4"): exact roundtrip + atomic rejection of
     truncated, oversized, bad-magic, bad-version, mismatched-mask, invalid-slot, out-of-order-slot,
     conflicting-payload, stale-tick, and out-of-window future-tick frames.
  4. Per-peer 64-bit state hash verification:
     - rejects unaligned ticks (`tick % mHashEvery != 0`), stale out-of-window ticks, and far-future ticks
       BEFORE touching any ring slot so they can NEVER clobber in-progress hashes;
     - identical hashes pass across all 4 peers and repeated CheckHashes calls do NOT double-count `HashesVerified()`;
     - any diverging peer (slot 1, 2, or 3) reports CE_NPEER_HASH_DESYNC with exact tick, referenceSlot,
       divergingSlot, expectedHash, and actualHash.
  5. Disconnect & rebind safety:
     - guest disconnect quarantines slot, scrubs ring, unblocks ticks;
     - RebindPeer(slot, mCurrentTick + inputDelay) honors future `resumeTick` so ticks `mCurrentTick .. resumeTick-1`
       advance cleanly WITHOUT deadlock, pre-resume submissions from `slot` are refused, stale entries >= resumeTick
       are scrubbed, and starting at `resumeTick` `slot` is required again;
     - host disconnect (slot 0) trips HostLost; dropping below 2 active peers returns CE_NPEER_ERR_TOO_FEW_PEERS.
  6. ASan + UBSan clean execution (-fsanitize=address,undefined -Wall -Wextra -Werror) + 19 causal mutants.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
RING_HEADER = PORT / "network/npeer_ring.h"
SCRATCH = ROOT / "local-data/port/ce-multibe-lockstep-tests"

DRIVER = r"""
// Regression check: src/game.h defines MAX_PLAYERS as a macro before including network headers.
#define MAX_PLAYERS 4
#include "npeer_ring.h"
#include <cstdio>
#include <cstring>

static int gFail = 0;
static void Check(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        gFail++;
    }
}

static CeNPeerInputPayload MakeInput(uint32_t tick, uint8_t slot) {
    CeNPeerInputPayload p = {};
    p.buttons0 = (tick << 4) ^ ((uint32_t)(slot + 1) * 0x1021u);
    p.buttons1 = (tick * 2654435761u) ^ ((uint32_t)slot * 0x55AAu);
    p.axisYaw = (int16_t)((int32_t)(tick % 100) - 50 + slot * 7);
    p.axisPitch = (int16_t)(25 - (int32_t)(tick % 50) - slot * 3);
    p.axisMove = (int16_t)(slot * 111 - (int32_t)(tick % 31));
    p.axisStrafe = (int16_t)((int32_t)(tick % 19) * (slot + 1) - 10);
    return p;
}

static void TestPeerCountsAndWraparound() {
    for (uint8_t n = 2; n <= 4; ++n) {
        CeNPeerLockstepRing<4, 64> host;
        Check(host.Init(n, 0, 2, 20) == CE_NPEER_OK, "host init");
        Check(host.IsHost(), "slot 0 is host");
        Check(host.PlayerCount() == n, "player count");
        Check(host.ActiveMask() == (uint8_t)((1u << n) - 1u), "active mask");
        Check(!host.CanAdvance(), "cannot advance before seeding");

        Check(host.SeedInitialDelay() == CE_NPEER_OK, "seed delay");
        Check(host.CanAdvance(), "tick 0 ready after seeding");
        Check(host.IsTickReady(1), "tick 1 ready after seeding");
        Check(!host.IsTickReady(2), "tick 2 not yet ready");

        CeNPeerLockstepRing<4, 64> guest;
        Check(guest.Init(n, (uint8_t)(n - 1), 2, 20) == CE_NPEER_OK, "guest init");
        Check(!guest.IsHost(), "guest is not host");
        Check(guest.SeedInitialDelay() == CE_NPEER_OK, "guest seed delay");

        // Run 220 ticks (>3 full 64-slot ring wraps) with star host-relay bundles.
        for (uint32_t t = 0; t < 220; ++t) {
            const uint32_t inputTick = t + 2;
            for (uint8_t s = 0; s < n; ++s) {
                Check(!host.IsTickReady(inputTick), "not ready until last peer submits");
                Check(host.SubmitInput(s, inputTick, MakeInput(inputTick, s)) == CE_NPEER_OK, "submit input");
            }
            Check(host.IsTickReady(inputTick), "ready once all active peers submitted");

            Check(host.SubmitInput(0, inputTick, MakeInput(inputTick, 0)) == CE_NPEER_OK, "idempotent dup ok");
            CeNPeerInputPayload conflict = MakeInput(inputTick, 0);
            conflict.buttons0 ^= 1u;
            Check(host.SubmitInput(0, inputTick, conflict) == CE_NPEER_ERR_CONFLICT, "conflict dup rejected");

            uint8_t wire[128] = {};
            size_t wireLen = 0;
            Check(host.BuildHostBundle(inputTick, wire, sizeof(wire), &wireLen) == CE_NPEER_OK, "build host bundle");
            Check(wireLen == CeNPeerLockstepRing<4, 64>::kBundleHeaderBytes +
                             (size_t)n * CeNPeerLockstepRing<4, 64>::kBundleEntryBytes,
                  "wire byte count");
            Check(!guest.IsTickReady(inputTick), "guest not ready before bundle");
            uint32_t decodedTick = 999999u;
            Check(guest.ApplyHostBundle(wire, wireLen, &decodedTick) == CE_NPEER_OK, "guest applies bundle");
            Check(decodedTick == inputTick, "decoded tick matches");
            Check(guest.IsTickReady(inputTick), "guest ready after bundle");

            CeNPeerInputPayload hOut[4] = {};
            CeNPeerInputPayload gOut[4] = {};
            uint8_t hMask = 0, gMask = 0;
            Check(host.ConsumeTick(hOut, &hMask) == CE_NPEER_OK, "host consume");
            Check(guest.ConsumeTick(gOut, &gMask) == CE_NPEER_OK, "guest consume");
            Check(hMask == host.ActiveMask() && gMask == guest.ActiveMask(), "consumed mask matches");
            for (uint8_t s = 0; s < n; ++s) {
                Check(hOut[s] == gOut[s], "host/guest consumed inputs identical");
                if (t >= 2) {
                    Check(hOut[s] == MakeInput(t, s), "consumed payload matches submitted");
                }
            }
        }
        Check(host.CurrentTick() == 220 && guest.CurrentTick() == 220, "current tick advanced to 220");
    }
}

static void TestRejectsInvalidInitAndBadWire() {
    CeNPeerLockstepRing<4, 64> r;
    Check(r.Init(1, 0, 2) == CE_NPEER_ERR_INVALID_ARG, "rejects 1 player");
    Check(r.Init(5, 0, 2) == CE_NPEER_ERR_INVALID_ARG, "rejects 5 players");
    Check(r.Init(4, 4, 2) == CE_NPEER_ERR_INVALID_ARG, "rejects out-of-range local slot");
    Check(r.Init(4, 0, 32) == CE_NPEER_ERR_INVALID_ARG, "rejects delay >= RING_SIZE/2");
    Check(r.Init(4, 0, 2, 0) == CE_NPEER_ERR_INVALID_ARG, "rejects hashEvery == 0");

    Check(r.Init(4, 1, 2) == CE_NPEER_OK, "valid 4p guest init");
    CeNPeerLockstepRing<4, 64> host;
    Check(host.Init(4, 0, 2) == CE_NPEER_OK, "valid 4p host init");
    for (uint8_t s = 0; s < 4; ++s)
        Check(host.SubmitInput(s, 0, MakeInput(0, s)) == CE_NPEER_OK, "host submit 0");

    uint8_t smallBuf[8] = {};
    size_t len = 0;
    Check(host.BuildHostBundle(0, smallBuf, sizeof(smallBuf), &len) == CE_NPEER_ERR_BUFFER_TOO_SMALL, "small buf rejected");

    uint8_t wire[128] = {};
    Check(host.BuildHostBundle(0, wire, sizeof(wire), &len) == CE_NPEER_OK, "bundle built");

    Check(r.ApplyHostBundle(wire, len - 1) == CE_NPEER_ERR_MALFORMED_FRAME, "truncated rejected");
    Check(r.ApplyHostBundle(wire, len + 1) == CE_NPEER_ERR_MALFORMED_FRAME, "extra trailing byte rejected");

    uint8_t badMagic[128];
    std::memcpy(badMagic, wire, len);
    badMagic[0] = 'X';
    Check(r.ApplyHostBundle(badMagic, len) == CE_NPEER_ERR_MALFORMED_FRAME, "bad magic rejected");

    uint8_t badVer[128];
    std::memcpy(badVer, wire, len);
    badVer[4] = 99;
    Check(r.ApplyHostBundle(badVer, len) == CE_NPEER_ERR_MALFORMED_FRAME, "bad version rejected");

    uint8_t badSlot[128];
    std::memcpy(badSlot, wire, len);
    badSlot[12] = 7;
    Check(r.ApplyHostBundle(badSlot, len) == CE_NPEER_ERR_MALFORMED_FRAME, "invalid slot rejected");
    Check(r.ReadyMaskForTick(0) == 0, "atomic rejection did not mutate readyMask");

    uint8_t dupSlot[128];
    std::memcpy(dupSlot, wire, len);
    dupSlot[12 + 17] = dupSlot[12];
    Check(r.ApplyHostBundle(dupSlot, len) == CE_NPEER_ERR_MALFORMED_FRAME, "duplicate/unsorted slot rejected");
    Check(r.ReadyMaskForTick(0) == 0, "duplicate slot left readyMask untouched");

    uint8_t farTick[128];
    std::memcpy(farTick, wire, len);
    farTick[8] = 0; farTick[9] = 0; farTick[10] = 0; farTick[11] = 80;
    Check(r.ApplyHostBundle(farTick, len) == CE_NPEER_ERR_FUTURE_TICK, "out-of-window future tick rejected");

    CeNPeerInputPayload badP2 = MakeInput(0, 2);
    badP2.axisYaw ^= 0x1234;
    Check(r.SubmitInput(2, 0, badP2) == CE_NPEER_OK, "pre-seed conflicting slot 2");
    Check(r.ApplyHostBundle(wire, len) == CE_NPEER_ERR_CONFLICT, "conflict bundle rejected atomically");
    Check(r.ReadyMaskForTick(0) == (uint8_t)(1u << 2), "conflict bundle left slots 0/1/3 untouched");

    CeNPeerLockstepRing<4, 64> g2;
    Check(g2.Init(4, 1, 2) == CE_NPEER_OK, "g2 init");
    Check(g2.ApplyHostBundle(wire, len) == CE_NPEER_OK, "valid bundle applies cleanly");
    Check(g2.IsTickReady(0), "g2 tick 0 ready");
}

static void TestPerPeerStateHashAndWindowValidation() {
    for (uint8_t badSlot = 1; badSlot < 4; ++badSlot) {
        CeNPeerLockstepRing<4, 64> r;
        Check(r.Init(4, 0, 1, 20) == CE_NPEER_OK, "init 4p hash test");
        const uint32_t tick = 20u * (uint32_t)badSlot;
        const uint64_t goodHash = 0xCAFEBABE12345678ULL ^ (uint64_t)tick;
        const uint64_t badHash = 0xDEADBEEF87654321ULL + (uint64_t)badSlot;
        for (uint8_t s = 0; s < 3; ++s) {
            Check(r.SubmitHash(s, tick, (s == badSlot) ? badHash : goodHash) == CE_NPEER_OK, "submit hash 0..2");
        }
        // Unaligned tick colliding with the same ring index (tick + 1) MUST be rejected and NOT clobber in-progress slot!
        Check(r.SubmitHash(0, tick + 1u, 0x1111ULL) == CE_NPEER_ERR_INVALID_ARG, "unaligned hash tick rejected");
        Check(r.CheckHashes(tick + 1u) == CE_NPEER_HASH_INVALID, "CheckHashes rejects unaligned tick");
        // Far-future tick aliasing the same ring slot (tick + 64 * 20) MUST be rejected and NOT clobber!
        Check(r.SubmitHash(0, tick + 64u * 20u, 0x2222ULL) == CE_NPEER_ERR_FUTURE_TICK, "aliasing future hash tick rejected");

        Check(r.CheckHashes(tick) == CE_NPEER_HASH_PENDING, "pending until all 4 peers submit hash");
        Check(r.SubmitHash(3, tick, (3 == badSlot) ? badHash : goodHash) == CE_NPEER_OK, "submit hash 3");

        CeNPeerDesyncReport rep = {};
        Check(r.CheckHashes(tick, &rep) == CE_NPEER_HASH_DESYNC, "CheckHashes detects divergence after rejected attacks");
        Check(rep.tick == tick, "desync report names exact tick");
        Check(rep.referenceSlot == 0, "reference slot is host 0");
        Check(rep.divergingSlot == badSlot, "desync report names exact diverging slot");
        Check(rep.divergingMask == (uint8_t)(1u << badSlot), "divergingMask marks exact diverging slot");
        Check(rep.expectedHash == goodHash, "expected hash matches host");
        Check(rep.actualHash == badHash, "actual hash matches diverging peer");
    }

    // Majority voting: in a 3-peer session where HOST slot 0 diverges, slots 1 & 2 outvote slot 0!
    {
        CeNPeerLockstepRing<4, 64> maj;
        Check(maj.Init(3, 0, 0, 20) == CE_NPEER_OK, "init 3p majority vote test");
        Check(maj.SubmitHash(0, 20, 0xBAD00000ULL) == CE_NPEER_OK, "host diverging hash");
        Check(maj.SubmitHash(1, 20, 0xAAAAAAAAULL) == CE_NPEER_OK, "guest 1 consensus hash");
        Check(maj.SubmitHash(2, 20, 0xAAAAAAAAULL) == CE_NPEER_OK, "guest 2 consensus hash");
        CeNPeerDesyncReport rep = {};
        Check(maj.CheckHashes(20, &rep) == CE_NPEER_HASH_DESYNC, "majority vote detects host divergence");
        Check(rep.referenceSlot == 1, "majority consensus reference is slot 1");
        Check(rep.divergingSlot == 0, "majority attributes divergence to host slot 0");
        Check(rep.divergingMask == 0x01u, "divergingMask identifies only host slot 0");
        Check(rep.expectedHash == 0xAAAAAAAAULL && rep.actualHash == 0xBAD00000ULL, "majority hashes accurate");
    }

    // Plurality voting with multiple diverging slots: divergingMask reports ALL mismatching slots!
    {
        CeNPeerLockstepRing<4, 64> multi;
        Check(multi.Init(4, 0, 0, 20) == CE_NPEER_OK, "init 4p multi-diverging test");
        Check(multi.SubmitHash(0, 40, 0x11111111ULL) == CE_NPEER_OK, "slot 0 consensus");
        Check(multi.SubmitHash(1, 40, 0x11111111ULL) == CE_NPEER_OK, "slot 1 consensus");
        Check(multi.SubmitHash(2, 40, 0x22222222ULL) == CE_NPEER_OK, "slot 2 diverges");
        Check(multi.SubmitHash(3, 40, 0x33333333ULL) == CE_NPEER_OK, "slot 3 diverges differently");
        CeNPeerDesyncReport rep = {};
        Check(multi.CheckHashes(40, &rep) == CE_NPEER_HASH_DESYNC, "plurality detects multi-slot divergence");
        Check(rep.referenceSlot == 0 && rep.divergingSlot == 2, "plurality reference=0 first diverging=2");
        Check(rep.divergingMask == 0x0Cu, "divergingMask reports BOTH slot 2 and slot 3");
    }

    // Hash-overdue timeout detector: detects missing peer hash once lag reaches maxLagTicks
    {
        CeNPeerLockstepRing<4, 64> ov;
        Check(ov.Init(4, 0, 0, 20) == CE_NPEER_OK, "init hash-overdue test");
        CeNPeerInputPayload out[4] = {};
        auto AdvanceTo = [&](uint32_t targetTick) {
            while (ov.CurrentTick() < targetTick) {
                const uint32_t t = ov.CurrentTick();
                for (uint8_t s = 0; s < 4; ++s)
                    Check(ov.SubmitInput(s, t, MakeInput(t, s)) == CE_NPEER_OK, "ov submit");
                Check(ov.ConsumeTick(out) == CE_NPEER_OK, "ov consume");
            }
        };
        AdvanceTo(60);
        for (uint8_t s = 0; s < 4; ++s) {
            Check(ov.SubmitHash(s, 0, 0xAA00ULL) == CE_NPEER_OK, "ov hash tick 0");
            Check(ov.SubmitHash(s, 20, 0xAA20ULL) == CE_NPEER_OK, "ov hash tick 20");
        }
        Check(ov.CheckHashes(0) == CE_NPEER_HASH_MATCH, "ov tick 0 verified");
        Check(ov.CheckHashes(20) == CE_NPEER_HASH_MATCH, "ov tick 20 verified");
        // Tick 40: slot 2 fails to submit its hash!
        Check(ov.SubmitHash(0, 40, 0xAA40ULL) == CE_NPEER_OK, "ov hash 40 s0");
        Check(ov.SubmitHash(1, 40, 0xAA40ULL) == CE_NPEER_OK, "ov hash 40 s1");
        Check(ov.SubmitHash(3, 40, 0xAA40ULL) == CE_NPEER_OK, "ov hash 40 s3");
        CeNPeerHashOverdueReport rep = {};
        Check(!ov.CheckHashOverdue(40, &rep), "tick 40 not yet overdue at tick 60 (lag 20 < 40)");
        AdvanceTo(80);
        Check(ov.CheckHashOverdue(40, &rep), "tick 40 is overdue at tick 80 (lag 40 >= 40)");
        Check(rep.tick == 40u && rep.expectedMask == 0x0Fu && rep.presentMask == 0x0Bu && rep.missingMask == 0x04u,
              "overdue report names tick 40 and exact missing slot 2");
        Check(ov.SubmitHash(2, 40, 0xAA40ULL) == CE_NPEER_OK, "late slot 2 submits tick 40");
        Check(ov.CheckHashes(40) == CE_NPEER_HASH_MATCH, "tick 40 verifies");
        Check(!ov.CheckHashOverdue(40, &rep), "no longer overdue once verified");
    }

    // Stale hash from 1 full ring behind (tick 20 arriving at mCurrentTick = 1300) MUST NOT clobber tick 1300!
    CeNPeerLockstepRing<4, 64> adv;
    Check(adv.Init(4, 0, 0, 20) == CE_NPEER_OK, "init advance hash window test");
    CeNPeerInputPayload out[4] = {};
    for (uint32_t t = 0; t < 1300; ++t) {
        for (uint8_t s = 0; s < 4; ++s)
            Check(adv.SubmitInput(s, t, MakeInput(t, s)) == CE_NPEER_OK, "adv submit");
        Check(adv.ConsumeTick(out) == CE_NPEER_OK, "adv consume");
    }
    Check(adv.CurrentTick() == 1300u, "advanced to tick 1300");
    // Submit 3 hashes for tick 1300 (same ring slot as stale tick 20: (1300/20)&63 == (20/20)&63 == 1!)
    Check(adv.SubmitHash(0, 1300, 0xAAAABBBBULL) == CE_NPEER_OK, "1300 hash 0");
    Check(adv.SubmitHash(1, 1300, 0xAAAABBBBULL) == CE_NPEER_OK, "1300 hash 1");
    Check(adv.SubmitHash(2, 1300, 0xAAAABBBBULL) == CE_NPEER_OK, "1300 hash 2");
    // Now stale tick 20 arrives! Must be rejected with CE_NPEER_ERR_STALE_TICK and NOT wipe tick 1300!
    Check(adv.SubmitHash(3, 20, 0x99999999ULL) == CE_NPEER_ERR_STALE_TICK, "stale tick 20 rejected at 1300");
    Check(adv.SubmitHash(3, 1300, 0xDEADBEEFULL) == CE_NPEER_OK, "1300 diverging hash 3");
    CeNPeerDesyncReport staleRep = {};
    Check(adv.CheckHashes(1300, &staleRep) == CE_NPEER_HASH_DESYNC, "tick 1300 desync still caught after stale tick 20");
    Check(staleRep.divergingSlot == 3 && staleRep.tick == 1300u, "stale attack preserved exact slot 3 desync");

    // Matching hashes pass and repeated CheckHashes calls do NOT double-count HashesVerified()!
    CeNPeerLockstepRing<4, 64> clean;
    Check(clean.Init(4, 0, 1, 20) == CE_NPEER_OK, "init clean hash");
    for (uint8_t s = 0; s < 4; ++s)
        Check(clean.SubmitHash(s, 40, 0x1122334455667788ULL) == CE_NPEER_OK, "submit matching hash");
    CeNPeerDesyncReport rep = {};
    Check(clean.CheckHashes(40, &rep) == CE_NPEER_HASH_MATCH, "first CheckHashes returns MATCH");
    Check(clean.HashesVerified() == 1u, "hashes verified == 1");
    Check(clean.CheckHashes(40, &rep) == CE_NPEER_HASH_MATCH, "second CheckHashes returns MATCH");
    Check(clean.HashesVerified() == 1u, "repeated CheckHashes does NOT double-count HashesVerified");
}

static void TestDisconnectAndFutureResumeRebind() {
    // 1. Immediate disconnect at mCurrentTick=0 unblocks tick 0 immediately; future RebindPeer(3, 4) advances ticks 1..3 without deadlock.
    CeNPeerLockstepRing<4, 64> r;
    Check(r.Init(4, 0, 3, 20) == CE_NPEER_OK, "init disconnect/rebind test (delay=3)");
    for (uint8_t s = 0; s < 3; ++s)
        Check(r.SubmitInput(s, 0, MakeInput(0, s)) == CE_NPEER_OK, "submit 0..2");
    Check(!r.CanAdvance(), "blocked while slot 3 missing");

    // Disconnect guest slot 3 at atTick=0 -> tick 0 unblocks immediately!
    Check(r.DisconnectPeer(3, 0) == CE_NPEER_OK, "disconnect slot 3 at tick 0");
    Check(!r.HostLost(), "guest disconnect does not set HostLost");
    Check(r.ActiveMask() == 0x07u && r.DisconnectedMask() == 0x08u, "masks updated after slot 3 disconnect");
    Check(r.SubmitInput(3, 0, MakeInput(0, 3)) == CE_NPEER_ERR_INACTIVE_SLOT, "disconnected slot rejected");
    Check(r.CanAdvance(), "unblocked after disconnecting slot 3");

    CeNPeerInputPayload out[4] = {};
    uint8_t mask = 0;
    Check(r.ConsumeTick(out, &mask) == CE_NPEER_OK, "consume tick 0 with 3 active peers");
    Check(mask == 0x07u, "consumed mask has 3 active peers");

    // Rebind slot 3 at future resumeTick = mCurrentTick(1) + inputDelay(3) = 4!
    Check(r.RebindPeer(3, 0) == CE_NPEER_ERR_STALE_TICK, "rebind refuses stale tick < mCurrentTick");
    Check(r.RebindPeer(3, 1 + 64) == CE_NPEER_ERR_FUTURE_TICK, "rebind refuses out-of-window future tick");
    Check(r.RebindPeer(3, 4) == CE_NPEER_OK, "rebind slot 3 at future resumeTick=4");
    Check(r.ActiveMask() == 0x0Fu && r.PendingRebindMask() == 0x08u, "slot 3 pending rebind");
    Check(r.ActiveMaskAt(1) == 0x07u && r.ActiveMaskAt(3) == 0x07u && r.ActiveMaskAt(4) == 0x0Fu, "ActiveMaskAt honors resumeTick=4");
    Check(r.SubmitInput(3, 2, MakeInput(2, 3)) == CE_NPEER_ERR_INACTIVE_SLOT, "slot 3 cannot submit pre-resume tick 2");

    // Ticks 1, 2, 3 MUST advance cleanly with only slots 0..2 (ZERO deadlock while waiting for resumeTick=4)!
    for (uint32_t t = 1; t < 4; ++t) {
        for (uint8_t s = 0; s < 3; ++s)
            Check(r.SubmitInput(s, t, MakeInput(t, s)) == CE_NPEER_OK, "submit pre-resume tick for 0..2");
        Check(r.CanAdvance(), "pre-resume tick advances without slot 3");
        // Verify host bundle at pre-resume tick carries only 3 peers
        uint8_t wire[128] = {};
        size_t wlen = 0;
        Check(r.BuildHostBundle(t, wire, sizeof(wire), &wlen) == CE_NPEER_OK, "pre-resume bundle builds with 3 peers");
        Check(wire[5] == 0x07u && wire[6] == 3u, "pre-resume bundle mask is 0x07 (3 peers)");
        uint8_t cmask = 0;
        Check(r.ConsumeTick(out, &cmask) == CE_NPEER_OK, "consume pre-resume tick");
        Check(cmask == 0x07u, "pre-resume consumed mask is 0x07");
    }

    // Now at tick 4 == resumeTick: pending flag cleared, slot 3 IS required
    Check(r.CurrentTick() == 4u && r.PendingRebindMask() == 0u, "reached resumeTick=4, pending rebind retired");
    for (uint8_t s = 0; s < 3; ++s)
        Check(r.SubmitInput(s, 4, MakeInput(4, s)) == CE_NPEER_OK, "submit tick 4 for 0..2");
    Check(!r.CanAdvance(), "tick 4 waits for fresh rebound slot 3 input");
    Check(r.SubmitInput(3, 4, MakeInput(4, 3)) == CE_NPEER_OK, "rebound slot 3 submits fresh tick 4");
    Check(r.CanAdvance(), "tick 4 ready after rebound slot 3 submits");
    uint8_t mask4 = 0;
    Check(r.ConsumeTick(out, &mask4) == CE_NPEER_OK, "consume tick 4");
    Check(mask4 == 0x0Fu, "tick 4 consumed mask includes all 4 peers");

    // 2. Scheduled disconnect honors mDisconnectTick in ActiveMaskAt(tick), AND RebindPeer scrubs pre-existing stale tick 4 input!
    {
        CeNPeerLockstepRing<4, 64> sched;
        Check(sched.Init(4, 0, 3, 20) == CE_NPEER_OK, "init scheduled disconnect test");
        Check(sched.SubmitInput(3, 4, MakeInput(4, 3)) == CE_NPEER_OK, "slot 3 pre-submit tick 4");
        Check(sched.DisconnectPeer(3, 5) == CE_NPEER_OK, "schedule slot 3 disconnect at tick 5");
        Check(sched.ActiveMaskAt(0) == 0x0Fu && sched.ActiveMaskAt(4) == 0x0Fu, "ActiveMaskAt includes slot 3 for ticks < mDisconnectTick(5)");
        Check(sched.ActiveMaskAt(5) == 0x07u && sched.ActiveMaskAt(6) == 0x07u, "ActiveMaskAt excludes slot 3 for ticks >= mDisconnectTick(5)");
        Check(sched.SubmitInput(3, 5, MakeInput(5, 3)) == CE_NPEER_ERR_INACTIVE_SLOT, "slot 3 refused at >= mDisconnectTick(5)");
        // Advance tick 0 (with all 4 peers since 0 < 5)
        for (uint8_t s = 0; s < 4; ++s)
            Check(sched.SubmitInput(s, 0, MakeInput(0, s)) == CE_NPEER_OK, "sched tick 0 all 4 peers");
        uint8_t m0 = 0;
        Check(sched.ConsumeTick(out, &m0) == CE_NPEER_OK && m0 == 0x0Fu, "sched tick 0 consumes all 4 peers");
        // Rebind slot 3 at tick 4 MUST scrub pre-existing tick 4 input!
        Check(sched.RebindPeer(3, 4) == CE_NPEER_OK, "sched rebind at tick 4");
        for (uint32_t t = 1; t < 4; ++t) {
            for (uint8_t s = 0; s < 3; ++s)
                Check(sched.SubmitInput(s, t, MakeInput(t, s)) == CE_NPEER_OK, "sched pre-resume submit");
            Check(sched.ConsumeTick(out) == CE_NPEER_OK, "sched pre-resume consume");
        }
        for (uint8_t s = 0; s < 3; ++s)
            Check(sched.SubmitInput(s, 4, MakeInput(4, s)) == CE_NPEER_OK, "sched tick 4 0..2");
        Check(!sched.CanAdvance(), "RebindPeer(3, 4) uniquely scrubbed pre-existing tick 4 input");
    }

    // 3. Authoritative host bundle activeMask: Host disconnects slot 3 mid-stream at tick 5 while Guest still has 0x0F locally!
    {
        CeNPeerLockstepRing<4, 64> host, guest;
        Check(host.Init(4, 0, 0, 20) == CE_NPEER_OK && guest.Init(4, 1, 0, 20) == CE_NPEER_OK, "init mid-stream disconnect pair");
        for (uint32_t t = 0; t < 5; ++t) {
            for (uint8_t s = 0; s < 4; ++s)
                Check(host.SubmitInput(s, t, MakeInput(t, s)) == CE_NPEER_OK, "pair host submit 0..4");
            uint8_t wire[128] = {}; size_t wlen = 0;
            Check(host.BuildHostBundle(t, wire, sizeof(wire), &wlen) == CE_NPEER_OK, "pair bundle 0..4");
            Check(guest.ApplyHostBundle(wire, wlen) == CE_NPEER_OK, "pair guest apply 0..4");
            uint8_t hm = 0, gm = 0;
            CeNPeerInputPayload ho[4] = {}, go[4] = {};
            Check(host.ConsumeTick(ho, &hm) == CE_NPEER_OK && guest.ConsumeTick(go, &gm) == CE_NPEER_OK, "pair consume 0..4");
            Check(hm == 0x0Fu && gm == 0x0Fu, "ticks 0..4 consume 0x0F on both");
        }
        // Host disconnects slot 3 at tick 5; Guest has NOT locally disconnected slot 3 yet!
        Check(host.DisconnectPeer(3, 5) == CE_NPEER_OK, "host disconnects slot 3 at tick 5");
        for (uint8_t s = 0; s < 3; ++s)
            Check(host.SubmitInput(s, 5, MakeInput(5, s)) == CE_NPEER_OK, "host tick 5 active 0..2");
        uint8_t wire5[128] = {}; size_t wlen5 = 0;
        Check(host.BuildHostBundle(5, wire5, sizeof(wire5), &wlen5) == CE_NPEER_OK, "host builds 3-peer bundle for tick 5");
        Check(wire5[5] == 0x07u, "host bundle mask is 0x07");
        Check(guest.ActiveMaskAt(5) == 0x0Fu, "guest local mask is still 0x0F before bundle");
        Check(guest.ApplyHostBundle(wire5, wlen5) == CE_NPEER_OK, "guest accepts authoritative host mask 0x07 without MALFORMED stall");
        Check(guest.IsTickReady(5), "guest tick 5 ready under authoritative host mask");
        // Conflicting host bundle for same tick with different activeMask must be rejected with CONFLICT
        CeNPeerLockstepRing<4, 64> altHost;
        Check(altHost.Init(4, 0, 0, 20) == CE_NPEER_OK, "altHost init");
        for (uint32_t t = 0; t < 5; ++t) {
            for (uint8_t s = 0; s < 4; ++s) altHost.SubmitInput(s, t, MakeInput(t, s));
            altHost.ConsumeTick(out);
        }
        for (uint8_t s = 0; s < 4; ++s) altHost.SubmitInput(s, 5, MakeInput(5, s));
        uint8_t wire5All[128] = {}; size_t wlen5All = 0;
        Check(altHost.BuildHostBundle(5, wire5All, sizeof(wire5All), &wlen5All) == CE_NPEER_OK, "altHost 4-peer bundle");
        Check(guest.ApplyHostBundle(wire5All, wlen5All) == CE_NPEER_ERR_CONFLICT, "conflicting host bundle activeMask rejected");
        uint8_t hm5 = 0, gm5 = 0;
        CeNPeerInputPayload ho5[4] = {}, go5[4] = {};
        Check(host.ConsumeTick(ho5, &hm5) == CE_NPEER_OK && guest.ConsumeTick(go5, &gm5) == CE_NPEER_OK, "tick 5 consumes cleanly on both");
        Check(hm5 == 0x07u && gm5 == 0x07u, "host and guest consume identical mask 0x07 at tick 5");
        for (uint8_t s = 0; s < 3; ++s)
            Check(ho5[s] == go5[s], "tick 5 active peer inputs match identically");
    }

    // 4. Atomic DisconnectPeer: dropping below 2 active peers returns TOO_FEW_PEERS WITHOUT mutating masks or ring!
    Check(r.DisconnectPeer(3, 5) == CE_NPEER_OK, "disconnect 3 (3 left)");
    Check(r.DisconnectPeer(2, 5) == CE_NPEER_OK, "disconnect 2 (2 left)");
    Check(r.SubmitInput(1, 5, MakeInput(5, 1)) == CE_NPEER_OK, "slot 1 submits tick 5 before rejected disconnect");
    Check(r.DisconnectPeer(1, 5) == CE_NPEER_ERR_TOO_FEW_PEERS, "disconnect 1 (<2 left) refused");
    Check(r.ActiveMask() == 0x03u && (r.DisconnectedMask() & 0x02u) == 0u, "atomic TOO_FEW_PEERS leaves masks unchanged");
    Check((r.ReadyMaskForTick(5) & 0x02u) != 0u, "atomic TOO_FEW_PEERS leaves ring inputs unscrubbed");

    // 5. Disconnecting host (slot 0) sets HostLost and refuses advance/submissions
    CeNPeerLockstepRing<4, 64> h;
    Check(h.Init(3, 1, 0, 20) == CE_NPEER_OK, "init host-loss test");
    Check(h.DisconnectPeer(0, 0) == CE_NPEER_ERR_HOST_LOST, "disconnect host returns HOST_LOST");
    Check(h.HostLost(), "HostLost flagged on slot 0 disconnect");
    Check(!h.CanAdvance(), "cannot advance after host disconnect");
    Check(h.SubmitInput(1, 0, MakeInput(0, 1)) == CE_NPEER_ERR_HOST_LOST, "submit returns HOST_LOST");

    // 6. Host ConsumeTick snapshots expectedMask on aligned hash checkpoints across disconnect + rebind:
    // 6a. Disconnect at 15, consume tick 20 (mask 0x03), rebind at 30, advance to 40 (retiring mPendingRebindMask):
    //     late hashes from slots 0 & 1 verify cleanly without false PENDING or false CheckHashOverdue.
    {
        CeNPeerLockstepRing<4, 64> hostA;
        Check(hostA.Init(3, 0, 0, 20) == CE_NPEER_OK, "init hostA hash snapshot test");
        CeNPeerInputPayload aOut[4] = {};
        auto StepHostA = [&](uint32_t targetTick) {
            while (hostA.CurrentTick() < targetTick) {
                const uint32_t t = hostA.CurrentTick();
                const uint8_t m = hostA.ActiveMaskAt(t);
                for (uint8_t s = 0; s < 3; ++s) {
                    if ((m & (1u << s)) != 0u)
                        Check(hostA.SubmitInput(s, t, MakeInput(t, s)) == CE_NPEER_OK, "hostA submit");
                }
                uint8_t cm = 0;
                Check(hostA.ConsumeTick(aOut, &cm) == CE_NPEER_OK && cm == m, "hostA consume");
            }
        };
        StepHostA(15);
        for (uint8_t s = 0; s < 3; ++s)
            Check(hostA.SubmitHash(s, 0, 0xAA00ULL) == CE_NPEER_OK, "hostA hash tick 0");
        Check(hostA.CheckHashes(0) == CE_NPEER_HASH_MATCH, "hostA tick 0 verified");

        Check(hostA.DisconnectPeer(2, 15) == CE_NPEER_OK, "hostA disconnect slot 2 at tick 15");
        StepHostA(26);
        Check(hostA.RebindPeer(2, 30) == CE_NPEER_OK, "hostA rebind slot 2 at tick 30");
        StepHostA(40);
        Check(hostA.PendingRebindMask() == 0u, "hostA pending rebind retired by tick 40");
        Check(hostA.ActiveMaskAt(20) == 0x07u, "hostA ActiveMaskAt(20) reverted to 0x07 after rebind retired");

        // Late hashes for tick 20 arrive from active slots 0 & 1 only (slot 2 was absent at tick 20)
        Check(hostA.SubmitHash(0, 20, 0x2020ULL) == CE_NPEER_OK, "hostA late hash slot 0 tick 20");
        Check(hostA.SubmitHash(1, 20, 0x2020ULL) == CE_NPEER_OK, "hostA late hash slot 1 tick 20");
        Check(hostA.SubmitHash(2, 20, 0x2020ULL) == CE_NPEER_ERR_INACTIVE_SLOT, "hostA rejects absent slot 2 hash for tick 20");
        Check(hostA.CheckHashes(20) == CE_NPEER_HASH_MATCH, "hostA tick 20 verifies with snapshotted mask 0x03");
        CeNPeerHashOverdueReport ovRep = {};
        Check(!hostA.CheckHashOverdue(20, &ovRep), "hostA tick 20 not falsely reported overdue");
    }

    // 6b. Consume tick 20 with all 3 peers (0x07), disconnect slot 2 at 25, rebind slot 2 at 30 (at tick 26):
    //     slot 2's valid in-flight hash for tick 20 MUST be accepted (NOT rejected with INACTIVE_SLOT).
    {
        CeNPeerLockstepRing<4, 64> hostB;
        Check(hostB.Init(3, 0, 0, 20) == CE_NPEER_OK, "init hostB in-flight hash snapshot test");
        CeNPeerInputPayload bOut[4] = {};
        auto StepHostB = [&](uint32_t targetTick) {
            while (hostB.CurrentTick() < targetTick) {
                const uint32_t t = hostB.CurrentTick();
                const uint8_t m = hostB.ActiveMaskAt(t);
                for (uint8_t s = 0; s < 3; ++s) {
                    if ((m & (1u << s)) != 0u)
                        Check(hostB.SubmitInput(s, t, MakeInput(t, s)) == CE_NPEER_OK, "hostB submit");
                }
                uint8_t cm = 0;
                Check(hostB.ConsumeTick(bOut, &cm) == CE_NPEER_OK && cm == m, "hostB consume");
            }
        };
        StepHostB(25);
        Check(hostB.DisconnectPeer(2, 25) == CE_NPEER_OK, "hostB disconnect slot 2 at tick 25");
        StepHostB(26);
        Check(hostB.RebindPeer(2, 30) == CE_NPEER_OK, "hostB rebind slot 2 at tick 30");
        Check(hostB.ActiveMaskAt(20) == 0x03u, "hostB ActiveMaskAt(20) stripped slot 2 due to pending rebind");

        Check(hostB.SubmitHash(0, 20, 0xBB20ULL) == CE_NPEER_OK, "hostB hash slot 0 tick 20");
        Check(hostB.SubmitHash(1, 20, 0xBB20ULL) == CE_NPEER_OK, "hostB hash slot 1 tick 20");
        Check(hostB.CheckHashes(20) == CE_NPEER_HASH_PENDING, "hostB tick 20 still waits for slot 2");
        Check(hostB.SubmitHash(2, 20, 0xBB20ULL) == CE_NPEER_OK, "hostB accepts valid in-flight slot 2 hash for tick 20");
        Check(hostB.CheckHashes(20) == CE_NPEER_HASH_MATCH, "hostB tick 20 verifies with all 3 peers");
    }
}

int main() {
    TestPeerCountsAndWraparound();
    TestRejectsInvalidInitAndBadWire();
    TestPerPeerStateHashAndWindowValidation();
    TestDisconnectAndFutureResumeRebind();
    if (!gFail)
        std::puts("npeer-ring-ok");
    return gFail != 0;
}
"""


class NPeerLockstepRingTests(unittest.TestCase):
    def compile_and_run(self, header_text, tag, sanitize=True):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        safe_tag = tag.replace(" ", "-").replace("/", "-")
        base = Path(tempfile.mkdtemp(prefix=f"npeer-{safe_tag}-", dir=SCRATCH))
        try:
            (base / "npeer_ring.h").write_text(header_text, encoding="utf-8")
            driver = base / "driver.cpp"
            driver.write_text(DRIVER, encoding="utf-8")
            exe = base / "driver"
            cmd = [
                shutil.which("g++") or "c++",
                "-std=gnu++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(base),
                str(driver),
                "-o",
                str(exe),
            ]
            if sanitize:
                cmd.insert(2, "-fsanitize=address,undefined")
                cmd.insert(3, "-fno-omit-frame-pointer")
            cc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            if cc.returncode != 0:
                return cc.returncode, cc.stdout + cc.stderr
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            return run.returncode, run.stdout + run.stderr
        finally:
            shutil.rmtree(base, ignore_errors=True)

    def test_spdx_and_zero_env_vars(self):
        text = RING_HEADER.read_text(encoding="utf-8")
        self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:200])
        self.assertNotIn("BEA_", text)

    def test_npeer_ring_passes_asan_ubsan(self):
        rc, out = self.compile_and_run(RING_HEADER.read_text(encoding="utf-8"), "clean", sanitize=True)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out.strip(), "npeer-ring-ok")

    def test_causal_mutants_are_caught(self):
        src = RING_HEADER.read_text(encoding="utf-8")
        mutants = {
            "readyMask ignores peers 2..3": (
                "return (frame.readyMask & reqMask) == reqMask;",
                "return (frame.readyMask & 0x3u) == 0x3u;",
            ),
            "hash check ignores peers 2..3": (
                "if (hf.hashes[s] != expected)\n\t\t\t{\n\t\t\t\tif (!anyDiverged)",
                "if (s < 2u && hf.hashes[s] != expected)\n\t\t\t{\n\t\t\t\tif (!anyDiverged)",
            ),
            "hash check blindly assumes slot 0 truth": (
                "if (votes > bestVotes)\n\t\t\t{\n\t\t\t\tbestVotes = votes;\n\t\t\t\trefSlot = s;\n\t\t\t}",
                "if (bestVotes == 0u)\n\t\t\t{\n\t\t\t\tbestVotes = votes;\n\t\t\t\trefSlot = s;\n\t\t\t}",
            ),
            "ActiveMaskAt ignores mDisconnectTick": (
                "if ((mDisconnectedMask & bit) != 0u && (uint32_t)(tick - mDisconnectTick[s]) >= 0x80000000u)\n\t\t\t\t\tmask |= bit;",
                "if (false)\n\t\t\t\t\tmask |= bit;",
            ),
            "ConsumeTick ignores authoritative host activeMask": (
                "const uint8_t reqMask = frame.hasHostMask ? frame.activeMask : ActiveMaskAt(mCurrentTick);",
                "const uint8_t reqMask = ActiveMaskAt(mCurrentTick);",
            ),
            "ConsumeTick only snapshots hash expectedMask for guest bundles": (
                "if ((mCurrentTick % mHashEvery) == 0u)",
                "if (frame.hasHostMask && (mCurrentTick % mHashEvery) == 0u)",
            ),
            "DisconnectPeer mutates before checking TOO_FEW_PEERS": (
                "const uint8_t nextActiveMask = (uint8_t)(mActiveMask & ~bit);\n\t\tif (ActivePeerCount(nextActiveMask) < 2u)\n\t\t\treturn CE_NPEER_ERR_TOO_FEW_PEERS;",
                "const uint8_t nextActiveMask = (uint8_t)(mActiveMask & ~bit);\n\t\tmActiveMask = nextActiveMask;\n\t\tif (ActivePeerCount(nextActiveMask) < 2u)\n\t\t\treturn CE_NPEER_ERR_TOO_FEW_PEERS;",
            ),
            "CheckHashOverdue never triggers": (
                "if (missing != 0u)\n\t\t\t{",
                "if (false)\n\t\t\t{",
            ),
            "hash alignment check bypassed": (
                "if ((tick % mHashEvery) != 0u)\n\t\t\treturn CE_NPEER_ERR_INVALID_ARG;",
                "if (false)\n\t\t\treturn CE_NPEER_ERR_INVALID_ARG;",
            ),
            "hash stale window check bypassed": (
                "if ((uint64_t)behind > maxPast64)\n\t\t\t\treturn CE_NPEER_ERR_STALE_TICK;",
                "if (false)\n\t\t\t\treturn CE_NPEER_ERR_STALE_TICK;",
            ),
            "hash future window check bypassed": (
                "else if ((uint64_t)ahead > maxFuture64)\n\t\t{\n\t\t\treturn CE_NPEER_ERR_FUTURE_TICK;\n\t\t}",
                "else if (false)\n\t\t{\n\t\t\treturn CE_NPEER_ERR_FUTURE_TICK;\n\t\t}",
            ),
            "verified flag double-counts": (
                "if (!hf.verified)\n\t\t{\n\t\t\thf.verified = true;\n\t\t\tmHashesVerified++;\n\t\t}",
                "mHashesVerified++;",
            ),
            "rebind ignores future resumeTick": (
                "if (resumeTick != mCurrentTick)\n\t\t\tmPendingRebindMask |= bit;",
                "if (false)\n\t\t\tmPendingRebindMask |= bit;",
            ),
            "magic/version check bypassed": (
                "if (magic != kBundleMagic || buf[4] != kBundleVersion || buf[7] != 0u)\n\t\t\treturn CE_NPEER_ERR_MALFORMED_FRAME;",
                "if (false)\n\t\t\treturn CE_NPEER_ERR_MALFORMED_FRAME;",
            ),
            "slot ordering/dup check bypassed": (
                "if (slot >= mPlayerCount || (int)slot <= prevSlot)\n\t\t\t\treturn CE_NPEER_ERR_MALFORMED_FRAME;",
                "if (slot >= mPlayerCount)\n\t\t\t\treturn CE_NPEER_ERR_MALFORMED_FRAME;",
            ),
            "conflict check bypassed in SubmitInput": (
                "return (frame.inputs[slot] == input) ? CE_NPEER_OK : CE_NPEER_ERR_CONFLICT;",
                "return CE_NPEER_OK;",
            ),
            "atomic conflict check bypassed in ApplyHostBundle": (
                "if ((seenMask & bit) != 0u && (existing.readyMask & bit) != 0u && existing.inputs[s] != decoded[s])\n\t\t\t\t\treturn CE_NPEER_ERR_CONFLICT;",
                "if (false)\n\t\t\t\t\treturn CE_NPEER_ERR_CONFLICT;",
            ),
            "host disconnect skips mHostLost": (
                "mHostLost = true;\n\t\t\tmActiveMask &= (uint8_t)~bit;",
                "mHostLost = false;\n\t\t\tmActiveMask &= (uint8_t)~bit;",
            ),
            "rebind fails to scrub stale inputs": (
                "mInputSlots[r].readyMask &= (uint8_t)~bit;\n\t\t\t\tmInputSlots[r].inputs[slot] = InputT{};",
                "(void)r;",
            ),
        }
        for name, (old, new) in mutants.items():
            with self.subTest(mutant=name):
                self.assertIn(old, src, name)
                rc, _ = self.compile_and_run(src.replace(old, new, 1), name, sanitize=False)
                self.assertNotEqual(rc, 0, f"mutant {name} was not caught")


if __name__ == "__main__":
    SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main()
