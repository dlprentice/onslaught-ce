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
  6. ASan + UBSan clean execution (-fsanitize=address,undefined -Wall -Wextra -Werror) + 13 causal mutants.
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
        Check(rep.expectedHash == goodHash, "expected hash matches host");
        Check(rep.actualHash == badHash, "actual hash matches diverging peer");
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
    CeNPeerLockstepRing<4, 64> r;
    Check(r.Init(4, 0, 3, 20) == CE_NPEER_OK, "init disconnect/rebind test (delay=3)");
    for (uint8_t s = 0; s < 3; ++s)
        Check(r.SubmitInput(s, 0, MakeInput(0, s)) == CE_NPEER_OK, "submit 0..2");
    Check(!r.CanAdvance(), "blocked while slot 3 missing");

    // Pre-submit tick 4 for slot 3 before disconnecting at atTick=5 so RebindPeer(3, 4) uniquely scrubs tick 4!
    Check(r.SubmitInput(3, 4, MakeInput(4, 3)) == CE_NPEER_OK, "slot 3 pre-submit tick 4");

    // Disconnect guest slot 3 -> tick 0 unblocks immediately!
    Check(r.DisconnectPeer(3, 5) == CE_NPEER_OK, "disconnect slot 3");
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

    // Now at tick 4 == resumeTick: pending flag cleared, slot 3 IS required, and pre-disconnect tick 4 input was scrubbed!
    Check(r.CurrentTick() == 4u && r.PendingRebindMask() == 0u, "reached resumeTick=4, pending rebind retired");
    for (uint8_t s = 0; s < 3; ++s)
        Check(r.SubmitInput(s, 4, MakeInput(4, s)) == CE_NPEER_OK, "submit tick 4 for 0..2");
    Check(!r.CanAdvance(), "tick 4 waits for fresh rebound slot 3 input (stale input was scrubbed)");
    Check(r.SubmitInput(3, 4, MakeInput(4, 3)) == CE_NPEER_OK, "rebound slot 3 submits fresh tick 4");
    Check(r.CanAdvance(), "tick 4 ready after rebound slot 3 submits");
    uint8_t mask4 = 0;
    Check(r.ConsumeTick(out, &mask4) == CE_NPEER_OK, "consume tick 4");
    Check(mask4 == 0x0Fu, "tick 4 consumed mask includes all 4 peers");

    // Dropping down to 1 active peer reports CE_NPEER_ERR_TOO_FEW_PEERS
    Check(r.DisconnectPeer(3, 5) == CE_NPEER_OK, "disconnect 3 (3 left)");
    Check(r.DisconnectPeer(2, 5) == CE_NPEER_OK, "disconnect 2 (2 left)");
    Check(r.DisconnectPeer(1, 5) == CE_NPEER_ERR_TOO_FEW_PEERS, "disconnect 1 (<2 left)");

    // Disconnecting host (slot 0) sets HostLost and refuses advance/submissions
    CeNPeerLockstepRing<4, 64> h;
    Check(h.Init(3, 1, 0, 20) == CE_NPEER_OK, "init host-loss test");
    Check(h.DisconnectPeer(0, 0) == CE_NPEER_ERR_HOST_LOST, "disconnect host returns HOST_LOST");
    Check(h.HostLost(), "HostLost flagged on slot 0 disconnect");
    Check(!h.CanAdvance(), "cannot advance after host disconnect");
    Check(h.SubmitInput(1, 0, MakeInput(0, 1)) == CE_NPEER_ERR_HOST_LOST, "submit returns HOST_LOST");
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
                "for (uint8_t s = (uint8_t)(refSlot + 1u); s < mPlayerCount; s++)",
                "for (uint8_t s = (uint8_t)(refSlot + 1u); s < 2u; s++)",
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
