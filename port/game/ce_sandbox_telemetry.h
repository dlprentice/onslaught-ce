// SPDX-License-Identifier: GPL-3.0-only
// Read-only long-session telemetry & inert Phase 0 Sandbox Launch Profile gate.
//
// Strictly read-only: sampling never allocates from game heaps, never mutates
// simulation state, and never emits logs unless diagnostic logging is opted in.
// CeSandboxProfileActive() is strictly FALSE in faithful replays, recordings,
// demos, determinism probes, lockstep/network sessions, and retail campaigns.

#ifndef CE_SANDBOX_TELEMETRY_H
#define CE_SANDBOX_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct CeSandboxProfileGate {
    bool sandboxOptIn;       // explicit sandbox launch profile requested
    bool ceLayer;            // PortCeLayer() active (false under faithful fences)
    bool recording;          // PortRecording() active
    bool fencedEnv;          // clock/tape/probes/record/pilot/autopilot env fence
    bool networkOrLockstep;  // BEA_NET_*, BEA_LOCKSTEP_*, or active session
    bool demo;               // playable demo / attract / stress / record / playback
    bool replayOrProbe;      // replay or determinism probe mode
    bool retailCampaign;     // retail campaign / non-sandbox level
};

// Pure admission law for the Sandbox Launch Profile.
// Phase 0 plumbing: returns true ONLY when explicitly opted into a sandbox
// launch profile AND zero faithful/replay/network/demo/campaign fences apply.
inline bool CeSandboxProfileAllowed(const CeSandboxProfileGate& g) {
    return g.sandboxOptIn &&
           g.ceLayer &&
           !g.recording &&
           !g.fencedEnv &&
           !g.networkOrLockstep &&
           !g.demo &&
           !g.replayOrProbe &&
           !g.retailCampaign;
}

struct CeSandboxTelemetrySnapshot {
    uint32_t thingHeapSize;
    uint32_t thingHeapUsedBytes;
    uint32_t thingHeapFreeBytes;
    uint32_t thingHeapPeakBytes;
    uint32_t thingHeapLargestFreeBlock;

    uint32_t defaultHeapSize;
    uint32_t defaultHeapUsedBytes;
    uint32_t defaultHeapFreeBytes;
    uint32_t defaultHeapPeakBytes;
    uint32_t defaultHeapLargestFreeBlock;

    uint32_t sptrSetAllocatedBytes;
    uint32_t sptrSetBlockCount;
    uint32_t sptrSetEstimatedNodes;
    uint32_t sptrSetOverflowNodes;

    int32_t eventManagerPending;
    int32_t eventManagerCapacity;
    int32_t sThingCount;

    uint32_t activeLiveUnits;
    uint32_t commanderMaxEntities;
    uint32_t entityHandleLiveCount;
    uint64_t entityHandleTotalSpawns;

    bool sandboxProfileActive;
    bool valid;
};

inline bool CeSandboxTelemetryShouldEmit(bool diagnosticEnabled, bool ceLayer,
                                         unsigned tick, unsigned intervalTicks) {
    if (!diagnosticEnabled || !ceLayer || intervalTicks == 0u) return false;
    return (tick % intervalTicks) == 0u;
}

inline int CeSandboxTelemetryFormatLine(const CeSandboxTelemetrySnapshot& s,
                                        char* out, size_t capacity) {
    if (!out || capacity == 0u) return 0;
    return snprintf(
        out, capacity,
        "ce-telemetry thing_free=%u thing_maxblk=%u def_free=%u def_peak=%u "
        "sptr_nodes=%u sptr_overflow=%u events=%d/%d sthing=%d units=%u/%u "
        "handles=%u spawns=%llu sandbox=%d",
        s.thingHeapFreeBytes,
        s.thingHeapLargestFreeBlock,
        s.defaultHeapFreeBytes,
        s.defaultHeapPeakBytes,
        s.sptrSetEstimatedNodes,
        s.sptrSetOverflowNodes,
        static_cast<int>(s.eventManagerPending),
        static_cast<int>(s.eventManagerCapacity),
        static_cast<int>(s.sThingCount),
        s.activeLiveUnits,
        s.commanderMaxEntities,
        s.entityHandleLiveCount,
        static_cast<unsigned long long>(s.entityHandleTotalSpawns),
        s.sandboxProfileActive ? 1 : 0);
}

// Engine-integrated read-only API (implemented in port/game/ce_sandbox_telemetry.cpp).
bool CeSandboxProfileActive();
void CeSandboxProfileSetOptIn(bool enabled);
void CeSandboxProfileSetNonCampaignSession(bool sandboxSession);
bool CeSandboxTelemetrySample(CeSandboxTelemetrySnapshot* out);
void CeSandboxTelemetrySetDiagnosticLog(bool enabled, unsigned intervalTicks = 600u);
bool CeSandboxTelemetryMaybeLog(unsigned tick);

#endif
