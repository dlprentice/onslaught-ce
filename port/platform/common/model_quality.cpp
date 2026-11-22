// SPDX-License-Identifier: GPL-3.0-only
#include "model_quality.h"
#include "portlog.h"
#include <SDL3/SDL_timer.h>
#include <limits.h>
#include <stdlib.h>

extern bool PortCeLayer();
extern "C" int CE_SettingGetBool(const char *key, int fallback);

namespace {
int admitted = -1;
int unitRequested = -1;
int treeRequested = -1;
bool unitActive = false;
bool treeActive = false;
bool diagnostics = false;
unsigned frames = 0, reports = 0, samples = 0;
unsigned counts[CE_MODEL_EVENTS] = {};
Uint64 previousTick = 0, intervals[120] = {};
unsigned intervalCount = 0;

bool Allowed()
{
    if (admitted >= 0) return admitted != 0;
    admitted = 0;
#ifdef CE_VERSION
    // A forced CE menu is never permission to alter faithful reference input.
    const char *const fences[] = {
        "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"
    };
    for (const char *name : fences) {
        const char *value = getenv(name);
        if (value && *value) return false;
    }
    if (!PortCeLayer()) return false;
    admitted = 1;
    const char *log = getenv("BEA_PORT_MODEL_QUALITY_LOG");
    diagnostics = log && log[0] == '1' && log[1] == 0;
#endif
    return admitted != 0;
}

void InitialChoices()
{
    if (unitRequested >= 0 && treeRequested >= 0) return;
    const int savedUnit = unitRequested < 0 ? CE_SettingGetBool("full_unit_models", -1) : unitRequested;
    const int savedTree = treeRequested < 0 ? CE_SettingGetBool("full_tree_models", -1) : treeRequested;
    // Backward compatibility: honor legacy full_models=1 as enabling both when neither split key is set.
    const int savedLegacy = (unitRequested < 0 && treeRequested < 0 && savedUnit < 0 && savedTree < 0)
        ? CE_SettingGetBool("full_models", -1)
        : -1;
    if (unitRequested < 0)
        unitRequested = savedUnit >= 0 ? (savedUnit ? 1 : 0) : 1;
    if (treeRequested < 0)
        treeRequested = savedTree >= 0 ? (savedTree ? 1 : 0) : (savedLegacy == 1 ? 1 : 0);
}

void Report()
{
    if (!diagnostics || !samples || reports >= 64) return;
    // This measures complete game-render intervals, including presentation and
    // scheduling. It is not a GPU timestamp or evidence of uncapped throughput.
    Uint64 ordered[120];
    for (unsigned i=0; i<intervalCount; ++i) {
        unsigned at = i;
        while (at && ordered[at-1] > intervals[i]) {
            ordered[at] = ordered[at-1];
            --at;
        }
        ordered[at] = intervals[i];
    }
    const unsigned median = intervalCount ? intervalCount/2 : 0;
    const unsigned p95 = intervalCount ? (intervalCount*95+99)/100-1 : 0;
    const unsigned p99 = intervalCount ? (intervalCount*99+99)/100-1 : 0;
    PortSessionLog(PORT_SESSION_VIDEO, false,
        "full-models active=%d units=%d trees=%d frame=%u samples=%u mesh=%u tree=%u impostor=%u "
        "tree_impostor=%u tree_batch=%u reduced_mesh=%u dt_samples=%u "
        "median_us=%llu p95_us=%llu p99_us=%llu max_us=%llu "
        "(renderer submissions; whole-render intervals include presentation and scheduling)",
        (unitActive && treeActive) ? 1 : 0, unitActive ? 1 : 0, treeActive ? 1 : 0,
        frames, samples, counts[CE_MODEL_MESH], counts[CE_MODEL_TREE],
        counts[CE_MODEL_IMPOSTOR], counts[CE_MODEL_TREE_IMPOSTOR], counts[CE_MODEL_TREE_BATCH],
        counts[CE_MODEL_REDUCED_MESH], intervalCount,
        (unsigned long long)(intervalCount ? ordered[median]/1000 : 0),
        (unsigned long long)(intervalCount ? ordered[p95]/1000 : 0),
        (unsigned long long)(intervalCount ? ordered[p99]/1000 : 0),
        (unsigned long long)(intervalCount ? ordered[intervalCount-1]/1000 : 0));
    ++reports;
    samples = 0;
    intervalCount = 0;
    for (unsigned &count : counts) count = 0;
}
}

extern "C" int CE_FullUnitModelsChoice()
{
    if (!Allowed()) return 0;
    InitialChoices();
    return unitRequested;
}

extern "C" int CE_FullUnitModelsRequest(int value)
{
    if ((value != 0 && value != 1) || !Allowed()) return 0;
    unitRequested = value;
    return 1;
}

extern "C" int CE_FullTreeModelsChoice()
{
    if (!Allowed()) return 0;
    InitialChoices();
    return treeRequested;
}

extern "C" int CE_FullTreeModelsRequest(int value)
{
    if ((value != 0 && value != 1) || !Allowed()) return 0;
    treeRequested = value;
    return 1;
}

extern "C" int CE_FullModelsChoice()
{
    if (!Allowed()) return 0;
    InitialChoices();
    return (unitRequested == 1 && treeRequested == 1) ? 1 : 0;
}

extern "C" int CE_FullModelsRequest(int value)
{
    if ((value != 0 && value != 1) || !Allowed()) return 0;
    unitRequested = value;
    treeRequested = value;
    return 1;
}

void CeModelQualityBeginFrame()
{
    if (!Allowed()) return;
    InitialChoices();
    const bool nextUnits = unitRequested == 1;
    const bool nextTrees = treeRequested == 1;
    if (diagnostics && reports < 64) {
        const Uint64 tick = SDL_GetTicksNS();
        if (previousTick && tick >= previousTick && intervalCount < 120)
            intervals[intervalCount++] = tick-previousTick;
        previousTick = tick;
    }
    if (nextUnits != unitActive || nextTrees != treeActive || samples >= 120) Report();
    if (nextUnits != unitActive || nextTrees != treeActive) {
        unitActive = nextUnits;
        treeActive = nextTrees;
        PortSessionLog(PORT_SESSION_VIDEO, false,
            "full-models units=%d trees=%d at frame boundary; visibility and fog unchanged",
            unitActive ? 1 : 0, treeActive ? 1 : 0);
    }
    if (frames != UINT_MAX) ++frames;
    if (diagnostics && samples != UINT_MAX) ++samples;
}

bool CeUnitModelsQualityEnabled()
{
    // The render hooks only read this snapshot, with no filesystem or environment access.
    return unitActive;
}

bool CeTreeModelsQualityEnabled()
{
    return treeActive;
}

bool CeModelQualityEnabled()
{
    return unitActive && treeActive;
}

void CeModelQualityCount(CeModelQualityEvent event)
{
    if (!diagnostics || event < 0 || event >= CE_MODEL_EVENTS) return;
    unsigned &value = counts[event];
    if (value != UINT_MAX) ++value;
}
