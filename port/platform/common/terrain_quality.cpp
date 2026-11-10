// SPDX-License-Identifier: GPL-3.0-only
#include "terrain_quality.h"
#include "portlog.h"
#include <algorithm>
#include <chrono>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

extern bool PortCeLayer();
extern "C" int CE_SettingGetBool(const char *key, int fallback);

namespace {
int admitted = -1;
int requested = -1;
int frameRequest = -1;
int frameStatus = CE_TERRAIN_DETAIL_PENDING;
bool extended = false;
bool freshRequest = false;
bool diagnostics = false;
unsigned frames = 0, reports = 0, samples = 0;
unsigned patches[4] = {}, textures[7] = {};
unsigned farCells = 0, allocationFailures = 0;
unsigned allocationAttempts[4] = {}, tierFailures[4] = {};
unsigned reserveGrants[4] = {}, reserveRefusals[4] = {};
// Interval between complete render frames, kept only while the diagnostic is on.
constexpr unsigned frameWindow = 120;
unsigned frameMicros[frameWindow] = {};
unsigned frameIntervals = 0;
std::chrono::steady_clock::time_point previousFrame;
bool havePreviousFrame = false;
unsigned renderSamples = 0;
unsigned long long renderNanoseconds = 0, renderMaximumNanoseconds = 0;
std::chrono::steady_clock::time_point renderStarted;
bool timing = false;
bool (*resourcesUsable)() = nullptr;
bool resourceWarning = false;

bool Allowed()
{
    if (admitted >= 0) return admitted != 0;
    admitted = 0;
#ifdef CE_VERSION
    const char *const fences[] = {
        "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"
    };
    for (const char *name : fences) {
        const char *value = getenv(name);
        if (value && *value) return false;
    }
    if (!PortCeLayer()) return false;
    admitted = 1;
    const char *log = getenv("BEA_PORT_TERRAIN_QUALITY_LOG");
    diagnostics = log && log[0] == '1' && log[1] == 0;
#endif
    return admitted != 0;
}

void InitialChoice()
{
    if (requested < 0)
        requested = CE_SettingGetBool("terrain_detail_distance", 0) ? 1 : 0;
}

void Increment(unsigned &value)
{
    if (diagnostics && reports < 64 && value != UINT_MAX) ++value;
}

struct FrameSummary { unsigned count, mean, p50, p95, p99, max, over16, over33; };

FrameSummary SummarizeFrames()
{
    FrameSummary out = {};
    out.count = frameIntervals;
    if (!frameIntervals) return out;
    unsigned sorted[frameWindow];
    unsigned long long total = 0;
    for (unsigned i = 0; i < frameIntervals; ++i) {
        sorted[i] = frameMicros[i];
        total += frameMicros[i];
        if (frameMicros[i] > 16667) ++out.over16;
        if (frameMicros[i] > 33333) ++out.over33;
    }
    std::sort(sorted, sorted + frameIntervals);
    const auto at = [&](unsigned percent) {
        return sorted[std::min(frameIntervals - 1, frameIntervals * percent / 100)];
    };
    out.mean = static_cast<unsigned>(total / frameIntervals);
    out.p50 = at(50);
    out.p95 = at(95);
    out.p99 = at(99);
    out.max = sorted[frameIntervals - 1];
    return out;
}

void Report()
{
    if (!diagnostics || !samples || reports >= 64) return;
    const FrameSummary frame = SummarizeFrames();
    PortSessionLog(PORT_SESSION_VIDEO, false,
        "terrain-detail extended=%d frame=%u samples=%u patch0=%u patch1=%u "
        "patch2=%u patch_other=%u far=%u allocation_failures=%u "
        "attempt0=%u attempt1=%u attempt2=%u attempt_other=%u "
        "failure0=%u failure1=%u failure2=%u failure_other=%u "
        "cpu_render_samples=%u cpu_render_ns=%llu cpu_render_max_ns=%llu "
        "texture0=%u texture1=%u texture2=%u texture3=%u texture4=%u texture5=%u texture_other=%u "
        "(CPU selections; texture windows and visibility unchanged)",
        extended ? 1 : 0, frames, samples,
        patches[0], patches[1], patches[2], patches[3], farCells, allocationFailures,
        allocationAttempts[0], allocationAttempts[1], allocationAttempts[2], allocationAttempts[3],
        tierFailures[0], tierFailures[1], tierFailures[2], tierFailures[3],
        renderSamples, renderNanoseconds, renderMaximumNanoseconds,
        textures[0], textures[1], textures[2], textures[3], textures[4], textures[5], textures[6]);
    // A second line: the session log keeps 640 characters per line, which the first already fills.
    PortSessionLog(PORT_SESSION_VIDEO, false,
        "terrain-detail window frame=%u reserve_grant0=%u reserve_grant1=%u reserve_grant2=%u reserve_refuse0=%u reserve_refuse1=%u reserve_refuse2=%u "
        "frame_us_n=%u frame_us_mean=%u frame_us_p50=%u frame_us_p95=%u frame_us_p99=%u frame_us_max=%u "
        "frame_over_16ms=%u frame_over_33ms=%u (intervals between complete render frames)",
        frames, reserveGrants[0], reserveGrants[1], reserveGrants[2], reserveRefusals[0], reserveRefusals[1], reserveRefusals[2],
        frame.count, frame.mean, frame.p50, frame.p95, frame.p99, frame.max, frame.over16, frame.over33);
    ++reports;
    samples = 0;
    for (unsigned &value : patches) value = 0;
    for (unsigned &value : textures) value = 0;
    for (unsigned &value : allocationAttempts) value = 0;
    for (unsigned &value : tierFailures) value = 0;
    for (unsigned &value : reserveGrants) value = 0;
    for (unsigned &value : reserveRefusals) value = 0;
    frameIntervals = 0;
    farCells = allocationFailures = 0;
    renderSamples = 0;
    renderNanoseconds = renderMaximumNanoseconds = 0;
}
}

extern "C" int CE_TerrainDetailChoice()
{
    if (!Allowed()) return 0;
    InitialChoice();
    return requested;
}

extern "C" int CE_TerrainDetailRequest(int value)
{
    if ((value != 0 && value != 1) || !Allowed()) return 0;
    requested = value;
    return 1;
}

extern "C" int CE_TerrainDetailStatus()
{
    if (!Allowed()) return CE_TERRAIN_DETAIL_ACTIVE;
    InitialChoice();
    if (requested != frameRequest) return CE_TERRAIN_DETAIL_PENDING;
    return frameStatus;
}

void CeTerrainQualityBeginFrame()
{
    if (!Allowed()) return;
    const bool measure = diagnostics && reports < 64;
    const auto began = measure ? std::chrono::steady_clock::now() :
        std::chrono::steady_clock::time_point{};
    InitialChoice();
    const bool wantsExtension = requested == 1;
    freshRequest = requested != frameRequest;
    const bool resourcesReady = !wantsExtension || (resourcesUsable && resourcesUsable());
    freshRequest = false;
    const bool next = wantsExtension && resourcesReady;
    const bool refused = wantsExtension && resourcesUsable && !resourcesReady;
    if (refused && !resourceWarning)
        PortSessionLog(PORT_SESSION_VIDEO, false,
            "terrain-detail requested Extended; reserve unavailable, using Normal (frame=%u)", frames);
    resourceWarning = refused;
    frameRequest = requested;
    frameStatus = !wantsExtension || resourcesReady ? CE_TERRAIN_DETAIL_ACTIVE :
        resourcesUsable ? CE_TERRAIN_DETAIL_RESOURCE_FALLBACK : CE_TERRAIN_DETAIL_PENDING;
    if (next != extended || samples >= 120) Report();
    if (next != extended) {
        extended = next;
        PortSessionLog(PORT_SESSION_VIDEO, false,
            "terrain-detail extended=%d at frame boundary frame=%u; geometry range only", extended ? 1 : 0, frames);
    }
    if (frames != UINT_MAX) ++frames;
    if (diagnostics && reports < 64 && samples != UINT_MAX) ++samples;
    timing = measure && reports < 64;
    if (timing) renderStarted = began;
    if (timing) {
        if (havePreviousFrame && frameIntervals < frameWindow) {
            const auto gap = std::chrono::duration_cast<std::chrono::microseconds>(began - previousFrame).count();
            frameMicros[frameIntervals++] = gap < 0 ? 0u : gap > static_cast<long long>(UINT_MAX) ? UINT_MAX : static_cast<unsigned>(gap);
        }
        previousFrame = began;
        havePreviousFrame = true;
    }
}

void CeTerrainQualityEndFrame()
{
    if (!timing) return;
    timing = false;
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - renderStarted).count();
    if (elapsed < 0) return;
    const auto duration = static_cast<unsigned long long>(elapsed);
    if (ULLONG_MAX - renderNanoseconds >= duration) renderNanoseconds += duration;
    else renderNanoseconds = ULLONG_MAX;
    if (duration > renderMaximumNanoseconds) renderMaximumNanoseconds = duration;
    Increment(renderSamples);
}

float CeTerrainGeometryDistanceSquared(float distanceSquared)
{
    // A factor of four in squared distance asks for the same geometry detail at
    // twice the distance. Texture selection, visibility and allocation still use
    // their own unchanged inputs and limits. Normal returns the original bits.
    if (!extended || !isfinite(distanceSquared) || distanceSquared < 0)
        return distanceSquared;
    return distanceSquared * 0.25f;
}

bool CeTerrainQualityExtended()
{
    return extended;
}

bool CeTerrainQualityResourcesAllowed()
{
    return Allowed();
}

bool CeTerrainQualityFreshRequest()
{
    return freshRequest;
}

bool CeTerrainQualityDiagnostics()
{
    return Allowed() && diagnostics;
}

void CeTerrainQualityReserveUse(int level, bool granted)
{
    Increment((granted ? reserveGrants : reserveRefusals)[level >= 0 && level < 3 ? level : 3]);
}

void CeTerrainQualityResourceCheck(bool (*usable)())
{
    resourcesUsable = usable;
    frameRequest = -1;
    frameStatus = CE_TERRAIN_DETAIL_PENDING;
    resourceWarning = false;
}

void CeTerrainQualityPatch(int level)
{
    Increment(patches[level >= 0 && level < 3 ? level : 3]);
}

void CeTerrainQualityFarCell()
{
    Increment(farCells);
}

void CeTerrainQualityAllocationAttempt(int level)
{
    Increment(allocationAttempts[level >= 0 && level < 3 ? level : 3]);
}

void CeTerrainQualityAllocationFailure(int level)
{
    Increment(allocationFailures);
    Increment(tierFailures[level >= 0 && level < 3 ? level : 3]);
}

void CeTerrainQualityTexture(int level)
{
    Increment(textures[level >= 0 && level < 6 ? level : 6]);
}
