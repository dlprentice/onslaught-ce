// SPDX-License-Identifier: GPL-3.0-only
// Read-only long-session telemetry & Sandbox Launch Profile gate.
//
// Zero new BEA_* env var names (passes env_allowlist_tests.py --require-binary).
// Zero src/ edits, zero game-heap allocations, zero faithful-replay side effects.

#include "ce_sandbox_telemetry.h"
extern "C" int PortCeRecordFeaturesAllowed(void) __attribute__((weak));

#include <cstring>

#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "unit.h"
#include "thing.h"
#include "DXMemoryManager.h"
#include "eventmanager.h"
#include "SPtrSet.h"
#include "CLIParams.h"
#include "ce_entity_handles.h"
#include "ce_command_panel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
extern char** environ;
#endif

bool PortCeLayer();
bool PortRecording();
extern "C" int CE_SettingGetBool(const char* key, int def);
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_LockstepRequested(void) __attribute__((weak));
extern "C" int CE_LockstepSharing(void) __attribute__((weak));
extern "C" int CE_SessionActive(void) __attribute__((weak));
extern "C" bool PortCeLivingSandboxActive(void) __attribute__((weak));
bool PortCeNetActive() __attribute__((weak));

namespace {

bool gSandboxOptIn = false;
bool gSandboxNonCampaignSession = false;
bool gDiagLogConfigured = false;
bool gDiagLogEnabled = false;
unsigned gDiagIntervalTicks = 600u;
int gActiveEventManagerCapacity = MAX_NUM_EVENTS;

// Read-only protected-member accessors (zero src/ edits, zero mutation).
struct ThingCounterAccess : public CThing {
    static short ReadThingCount() { return sThingCount; }
};

struct MemoryHeapReader : public CMemoryHeap {
    static uint32_t LargestFreeBlock(const CMemoryHeap* heap) {
        if (!heap) return 0u;
        const MemoryHeapReader* r = static_cast<const MemoryHeapReader*>(heap);
        uint32_t largest = 0u;
        uint32_t steps = 0u;
        constexpr uint32_t kMaxWalk = 1048576u;
        for (CMemoryBlock* b = r->mpFree; b && steps < kMaxWalk; b = b->mpNext, ++steps) {
            const uint32_t sz = static_cast<uint32_t>(b->GetSize());
            if (sz > largest) largest = sz;
        }
        for (int bucket = 0; bucket < 16; ++bucket) {
            for (CMemoryBlock* b = r->mpSmallFree[bucket]; b && steps < kMaxWalk; b = b->mpNext, ++steps) {
                const uint32_t sz = static_cast<uint32_t>(b->GetSize());
                if (sz > largest) largest = sz;
            }
        }
        return largest;
    }

    static uint32_t TypeBytes(const CMemoryHeap* heap, EMemoryType type) {
        if (!heap) return 0u;
        const MemoryHeapReader* r = static_cast<const MemoryHeapReader*>(heap);
        return r->mTypeSize[type] > 0 ? static_cast<uint32_t>(r->mTypeSize[type]) : 0u;
    }

    static uint32_t TypeBlocks(const CMemoryHeap* heap, EMemoryType type) {
        if (!heap) return 0u;
        const MemoryHeapReader* r = static_cast<const MemoryHeapReader*>(heap);
        return r->mTypeBlocks[type] > 0 ? static_cast<uint32_t>(r->mTypeBlocks[type]) : 0u;
    }
};

bool EnvPresent(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] != '\0';
}

bool FaithfulEnvFenceActive() {
    if ((PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed())) return false;
    return EnvPresent("BEA_PORT_CLOCK") ||
           EnvPresent("BEA_PORT_TAPE") ||
           EnvPresent("BEA_PORT_PROBES") ||
           EnvPresent("BEA_PORT_RECORD") ||
           EnvPresent("BEA_PORT_PILOT") ||
           EnvPresent("BEA_PORT_AUTOPILOT");
}

bool NetworkOrLockstepFenceActive() {
    if (EnvPresent("BEA_NET_HOST") || EnvPresent("BEA_NET_JOIN")) return true;
    if (CE_BootRequested && CE_BootRequested()) return true;
    if (CE_LockstepRequested && CE_LockstepRequested()) return true;
    if (CE_LockstepSharing && CE_LockstepSharing()) return true;
    if (CE_SessionActive && CE_SessionActive()) return true;
    if (PortCeNetActive && PortCeNetActive()) return true;
#if defined(_WIN32)
    char** env = _environ;
#else
    char** env = environ;
#endif
    for (char** entry = env; entry && *entry; ++entry) {
        if (!std::strncmp(*entry, "BEA_NET_", 8) ||
            !std::strncmp(*entry, "BEA_LOCKSTEP_", 13)) {
            return true;
        }
    }
    return false;
}

CeSandboxProfileGate ReadProfileGate(bool forStartup) {
    CeSandboxProfileGate g = {};
    const bool livingSandbox = (PortCeLivingSandboxActive && PortCeLivingSandboxActive());
    g.sandboxOptIn = gSandboxOptIn || (!forStartup && livingSandbox);
    g.ceLayer = PortCeLayer();
    g.recording = PortRecording() && !(PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed());
    g.fencedEnv = FaithfulEnvFenceActive();
    g.networkOrLockstep = NetworkOrLockstepFenceActive() ||
                          (!forStartup && ((GAME.IsMultiplayer() != FALSE) ||
                                           (GAME.GetNPlayers() != 1)));
    g.demo = (PLAYABLE_DEMO != 0) ||
             (CLIPARAMS.mAutoConfigTest != FALSE) ||
             (CLIPARAMS.mRecordDemo != FALSE) ||
             (CLIPARAMS.mPlaybackDemo != FALSE) ||
             (CLIPARAMS.mStressTest != FALSE) ||
             (CLIPARAMS.mAttractMode != FALSE);
    g.replayOrProbe = g.recording || g.fencedEnv || !g.ceLayer;
    g.retailCampaign = !(gSandboxNonCampaignSession || livingSandbox);
    return g;
}

void EnsureDiagConfig() {
    if (gDiagLogConfigured) return;
    gDiagLogConfigured = true;
    if (!gDiagLogEnabled) {
        gDiagLogEnabled = CE_SettingGetBool("sandbox_telemetry", 0) == 1;
    }
}

#if defined(_WIN32)
// Pre-WinMain detection of --sandbox-profile on Windows so CDXMemoryManager::Init
// and CSystem::Init can apply expanded sandbox heaps strictly when opted in.
struct SandboxCliOptInInit {
    SandboxCliOptInInit() {
        for (int i = 1; i < __argc; ++i) {
            if (__argv[i] && std::strcmp(__argv[i], "--sandbox-profile") == 0) {
                gSandboxOptIn = true;
                break;
            }
        }
    }
} gSandboxCliOptInInit;
#endif

}  // namespace

bool CeSandboxProfileActive() {
    return CeSandboxProfileAllowed(ReadProfileGate(false));
}

void CeSandboxProfileSetOptIn(bool enabled) {
    gSandboxOptIn = enabled;
}

void CeSandboxProfileSetNonCampaignSession(bool sandboxSession) {
    gSandboxNonCampaignSession = sandboxSession;
}

extern "C" void PortCeSandboxSetCliOptIn(bool enabled) {
    gSandboxOptIn = enabled;
}

extern "C" bool PortCeSandboxStartupHeapsActive(void) {
    return CeSandboxStartupProfileAllowed(ReadProfileGate(true));
}

extern "C" int PortCeEventManagerPoolCapacity(void) {
    if (CeSandboxProfileActive()) {
        return kCeSandboxEventPoolCapacity;
    }
    if (PortCeLivingSandboxActive && PortCeLivingSandboxActive() &&
        CeSandboxStartupProfileAllowed(ReadProfileGate(true))) {
        return kCeSandboxEventPoolCapacity;
    }
    // Direct Bonus Levels launch of Living Sandbox (without --sandbox-profile)
    // still expands the event pool cleanly when all faithful/network fences are clear.
    if (PortCeLivingSandboxActive && PortCeLivingSandboxActive()) {
        CeSandboxProfileGate g = ReadProfileGate(true);
        g.sandboxOptIn = true;
        if (CeSandboxStartupProfileAllowed(g)) {
            return kCeSandboxEventPoolCapacity;
        }
    }
    return MAX_NUM_EVENTS;
}

extern "C" void PortCeSandboxRecordEventPoolCapacity(int capacity) {
    gActiveEventManagerCapacity = (capacity > 0) ? capacity : MAX_NUM_EVENTS;
}

extern "C" int PortCeSandboxActiveEventPoolCapacity(void) {
    return gActiveEventManagerCapacity;
}

bool CeSandboxTelemetrySample(CeSandboxTelemetrySnapshot* out) {
    if (!out) return false;
    std::memset(out, 0, sizeof(*out));

    CMemoryHeap* thingHeap = MEM_MANAGER.GetThingHeap();
    if (thingHeap) {
        out->thingHeapSize = static_cast<uint32_t>(thingHeap->GetSize());
        out->thingHeapUsedBytes = static_cast<uint32_t>(thingHeap->GetUsedSize());
        out->thingHeapFreeBytes = static_cast<uint32_t>(thingHeap->GetFreeSize());
        out->thingHeapPeakBytes = static_cast<uint32_t>(thingHeap->GetPeakSize());
        out->thingHeapLargestFreeBlock = MemoryHeapReader::LargestFreeBlock(thingHeap);
    }

    CMemoryHeap* defHeap = MEM_MANAGER.GetDefaultHeap();
    if (defHeap) {
        out->defaultHeapSize = static_cast<uint32_t>(defHeap->GetSize());
        out->defaultHeapUsedBytes = static_cast<uint32_t>(defHeap->GetUsedSize());
        out->defaultHeapFreeBytes = static_cast<uint32_t>(defHeap->GetFreeSize());
        out->defaultHeapPeakBytes = static_cast<uint32_t>(defHeap->GetPeakSize());
        out->defaultHeapLargestFreeBlock = MemoryHeapReader::LargestFreeBlock(defHeap);
    }

    const uint32_t defSptrBytes = MemoryHeapReader::TypeBytes(defHeap, MEMTYPE_SPTRSET);
    const uint32_t defSptrBlocks = MemoryHeapReader::TypeBlocks(defHeap, MEMTYPE_SPTRSET);
    const uint32_t thingSptrBytes = MemoryHeapReader::TypeBytes(thingHeap, MEMTYPE_SPTRSET);
    const uint32_t thingSptrBlocks = MemoryHeapReader::TypeBlocks(thingHeap, MEMTYPE_SPTRSET);
    out->sptrSetAllocatedBytes = defSptrBytes + thingSptrBytes;
    out->sptrSetBlockCount = defSptrBlocks + thingSptrBlocks;
    out->sptrSetEstimatedNodes = out->sptrSetAllocatedBytes / static_cast<uint32_t>(sizeof(SPtrSetNode));
    // Initial GenericSPtrSet::Init allocates 1 contiguous pool block; any extra block is overflow.
    out->sptrSetOverflowNodes = out->sptrSetBlockCount > 1u ? (out->sptrSetBlockCount - 1u) : 0u;

    out->eventManagerPending = static_cast<int32_t>(EVENT_MANAGER.TotalEvents());
    out->eventManagerCapacity = static_cast<int32_t>(gActiveEventManagerCapacity);
    out->sThingCount = static_cast<int32_t>(ThingCounterAccess::ReadThingCount());

    uint32_t liveUnits = 0u;
    ListIterator<CUnit> units(&WORLD.GetUnitNB());
    for (CUnit* u = units.First(); u; u = units.Next()) {
        if (!u->IsA(THING_TYPE_COMPONENT) && !u->IsDying() && !u->IsShuttingDown() && u->GetLife() > 0.f) ++liveUnits;
    }
    out->activeLiveUnits = liveUnits;
    out->commanderMaxEntities = static_cast<uint32_t>(ce_command_panel::kMaxEntities);
    out->entityHandleLiveCount = CeEntityHandleLiveCount();
    out->entityHandleTotalSpawns = CeEntityHandleTotalSpawns();
    out->sandboxProfileActive = CeSandboxProfileActive();
    out->valid = true;
    return true;
}

void CeSandboxTelemetrySetDiagnosticLog(bool enabled, unsigned intervalTicks) {
    gDiagLogConfigured = true;
    gDiagLogEnabled = enabled;
    if (intervalTicks > 0u) gDiagIntervalTicks = intervalTicks;
}

bool CeSandboxTelemetryMaybeLog(unsigned tick) {
    EnsureDiagConfig();
    if (!CeSandboxTelemetryShouldEmit(gDiagLogEnabled, PortCeLayer(), tick, gDiagIntervalTicks))
        return false;
    CeSandboxTelemetrySnapshot snap = {};
    if (!CeSandboxTelemetrySample(&snap)) return false;
    char line[320];
    if (CeSandboxTelemetryFormatLine(snap, line, sizeof(line)) > 0) {
        std::fprintf(stderr, "%s\n", line);
        return true;
    }
    return false;
}

#else

bool CeSandboxProfileActive() { return false; }
void CeSandboxProfileSetOptIn(bool) {}
void CeSandboxProfileSetNonCampaignSession(bool) {}
bool CeSandboxTelemetrySample(CeSandboxTelemetrySnapshot* out) {
    if (out) std::memset(out, 0, sizeof(*out));
    return false;
}
void CeSandboxTelemetrySetDiagnosticLog(bool, unsigned) {}
bool CeSandboxTelemetryMaybeLog(unsigned) { return false; }

extern "C" bool PortCeSandboxStartupHeapsActive(void) { return false; }
extern "C" int PortCeEventManagerPoolCapacity(void) { return kCeRetailEventPoolCapacity; }
extern "C" void PortCeSandboxRecordEventPoolCapacity(int) {}
extern "C" int PortCeSandboxActiveEventPoolCapacity(void) { return kCeRetailEventPoolCapacity; }
extern "C" void PortCeSandboxSetCliOptIn(bool) {}

#endif
