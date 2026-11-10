// SPDX-License-Identifier: GPL-3.0-only
#include "terrain_quality.h"
#include "portlog.h"
#include <math.h>
#include <initializer_list>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void Check(bool value, int line)
{
    if (value) return;
    fprintf(stderr, "Assertion failed in authored terrain driver, line %d\n", line);
    exit(1);
}
#define CHECK(value) Check((value), __LINE__)

static int stored, reads, layerCalls, logs;
static bool resourcesReady = true;
static bool ResourcesReady() { return resourcesReady; }
bool PortCeLayer()
{
    ++layerCalls;
    return getenv("DRIVER_LAYER_OFF") == NULL;
}
extern "C" int CE_SettingGetBool(const char *key, int fallback)
{
    CHECK(!strcmp(key, "terrain_detail_distance"));
    CHECK(fallback == 0);
    ++reads;
    return stored;
}
static char lastReport[2048];
static size_t longestReport;
void PortSessionLog(PortSessionCategory, bool error, const char *format, ...)
{
    CHECK(!error);
    CHECK(strstr(format, "terrain-detail ") == format);
    ++logs;
    va_list args;
    va_start(args, format);
    vsnprintf(lastReport, sizeof lastReport, format, args);
    va_end(args);
    if (strlen(lastReport) > longestReport) longestReport = strlen(lastReport);
}
// The value of " name=" in the last formatted line, or -1.
static long Field(const char *name)
{
    char key[64];
    snprintf(key, sizeof key, " %s=", name);
    const char *at = strstr(lastReport, key);
    return at ? strtol(at + strlen(key), NULL, 10) : -1;
}

static uint32_t Bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CeTerrainQualityResourceCheck(ResourcesReady);
    const char *mode = argv[1];
    if (!strcmp(mode, "fenced")) {
        stored = 1;
        CHECK(!CE_TerrainDetailRequest(1));
        CHECK(CE_TerrainDetailChoice() == 0);
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE);
        CeTerrainQualityBeginFrame();
        CHECK(CeTerrainGeometryDistanceSquared(100.f) == 100.f);
        CeTerrainQualityFarCell();
        CHECK(reads == 0 && logs == 0);
#ifndef CE_VERSION
        CHECK(layerCalls == 0);
#endif
    } else if (!strcmp(mode, "default")) {
        CHECK(CeTerrainGeometryDistanceSquared(100.f) == 100.f);
        CeTerrainQualityBeginFrame();
        CHECK(CeTerrainGeometryDistanceSquared(100.f) == 100.f);
        CHECK(CE_TerrainDetailChoice() == 0 && reads == 1 && logs == 0);
    } else if (!strcmp(mode, "stored")) {
        stored = 1;
        CHECK(CE_TerrainDetailChoice() == 1);
        CHECK(CeTerrainGeometryDistanceSquared(100.f) == 100.f);
        CeTerrainQualityBeginFrame();
        CHECK(CeTerrainGeometryDistanceSquared(100.f) == 25.f);
        CHECK(reads == 1);
    } else if (!strcmp(mode, "switch")) {
        CeTerrainQualityBeginFrame();
        CHECK(!CE_TerrainDetailRequest(-1) && !CE_TerrainDetailRequest(2));
        CHECK(CE_TerrainDetailRequest(1));
        for (int view=0; view<4; ++view)
            CHECK(CeTerrainGeometryDistanceSquared(256.f) == 256.f);
        CeTerrainQualityBeginFrame();
        CHECK(CeTerrainGeometryDistanceSquared(256.f) == 64.f);
        CHECK(CE_TerrainDetailRequest(0));
        for (int view=0; view<4; ++view)
            CHECK(CeTerrainGeometryDistanceSquared(256.f) == 64.f);
        CeTerrainQualityBeginFrame();
        CHECK(CeTerrainGeometryDistanceSquared(256.f) == 256.f);
    } else if (!strcmp(mode, "queued-first")) {
        CHECK(CE_TerrainDetailRequest(1));
        CeTerrainQualityBeginFrame();
        CHECK(reads == 0 && CeTerrainGeometryDistanceSquared(16.f) == 4.f);
        CHECK(CE_TerrainDetailRequest(0));
        CHECK(CE_TerrainDetailRequest(1));
        CHECK(CE_TerrainDetailRequest(0));
        CeTerrainQualityBeginFrame();
        CHECK(CeTerrainGeometryDistanceSquared(16.f) == 16.f);
    } else if (!strcmp(mode, "resources")) {
        CHECK(CE_TerrainDetailRequest(1));
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING);
        CeTerrainQualityResourceCheck(nullptr);
        CeTerrainQualityBeginFrame();
        CHECK(!CeTerrainQualityExtended());
        CHECK(CE_TerrainDetailChoice() == 1);
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING);
        CHECK(logs == 0);
        CeTerrainQualityResourceCheck(ResourcesReady);
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING);
        CeTerrainQualityBeginFrame();
        CHECK(CeTerrainQualityExtended());
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE);
        resourcesReady = false;
        CHECK(CeTerrainQualityExtended());
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE);
        CeTerrainQualityBeginFrame();
        CHECK(!CeTerrainQualityExtended());
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
        CHECK(CE_TerrainDetailRequest(1));
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
        CHECK(CeTerrainGeometryDistanceSquared(100.f) == 100.f);
        resourcesReady = true;
        CeTerrainQualityBeginFrame();
        CHECK(CeTerrainQualityExtended());
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE);
        CeTerrainQualityResourceCheck(nullptr);
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING);
        CHECK(CeTerrainQualityExtended());
        CeTerrainQualityBeginFrame();
        CHECK(!CeTerrainQualityExtended());
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING);
        CHECK(CE_TerrainDetailRequest(0));
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING);
        CeTerrainQualityBeginFrame();
        CHECK(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE);
    } else if (!strcmp(mode, "domain")) {
        const uint32_t patterns[] = {0, 0x80000000u, 0x3f800000u, 0x7f800000u,
                                    0xff800000u, 0x7fc01234u, 0xbf800000u};
        CeTerrainQualityBeginFrame();
        for (uint32_t bits : patterns) {
            float value;
            memcpy(&value, &bits, sizeof(value));
            CHECK(Bits(CeTerrainGeometryDistanceSquared(value)) == bits);
        }
        CHECK(CE_TerrainDetailRequest(1));
        CeTerrainQualityBeginFrame();
        for (float value : {-10.f, -0.f, INFINITY, -INFINITY})
            CHECK(Bits(CeTerrainGeometryDistanceSquared(value)) == Bits(value));
        CHECK(isnan(CeTerrainGeometryDistanceSquared(NAN)));
        for (float value : {1.f, 16.f, 256.f, 1048576.f})
            CHECK(CeTerrainGeometryDistanceSquared(value) == value * .25f);
    } else if (!strcmp(mode, "diagnostics")) {
        CeTerrainQualityBeginFrame();
        for (int i=0; i<12000; ++i) {
            CeTerrainQualityPatch(0); CeTerrainQualityPatch(2); CeTerrainQualityPatch(-1);
            CeTerrainQualityFarCell(); CeTerrainQualityAllocationFailure();
            CeTerrainQualityTexture(0); CeTerrainQualityTexture(6); CeTerrainQualityTexture(-1);
            CeTerrainQualityBeginFrame();
        }
        CHECK(logs == 128 && reads == 1);   // two lines per window, at most 64 windows
        CHECK(longestReport < 600);         // the session log keeps 640 characters a line
        CHECK(CeTerrainGeometryDistanceSquared(100.f) == 100.f);
    } else if (!strcmp(mode, "fresh-request")) {
        // The signal a readiness check may use: true only while a changed request is being sampled.
        static int calls, fresh;
        calls = fresh = 0;
        static bool seen[16];
        CeTerrainQualityResourceCheck([]() { seen[calls < 16 ? calls : 15] = CeTerrainQualityFreshRequest(); ++calls; return true; });
        CHECK(!CeTerrainQualityFreshRequest());
        CHECK(CE_TerrainDetailRequest(1));
        CeTerrainQualityBeginFrame();             // call 0: a new request
        CeTerrainQualityBeginFrame();             // call 1: the same request again
        CHECK(CE_TerrainDetailRequest(1));
        CeTerrainQualityBeginFrame();             // call 2: repeating the value is not a new request
        CHECK(CE_TerrainDetailRequest(0));
        CeTerrainQualityBeginFrame();             // Normal needs no check
        CHECK(calls == 3);
        CHECK(CE_TerrainDetailRequest(1));
        CeTerrainQualityBeginFrame();             // call 3: chosen again
        CeTerrainQualityBeginFrame();             // call 4
        CHECK(calls == 5 && seen[0] && !seen[1] && !seen[2] && seen[3] && !seen[4]);
        CHECK(!CeTerrainQualityFreshRequest());
        CeTerrainQualityResourceCheck(nullptr);   // a new initialisation samples as fresh
        CHECK(CE_TerrainDetailRequest(1));
        CeTerrainQualityResourceCheck([]() { seen[8] = CeTerrainQualityFreshRequest(); return true; });
        CeTerrainQualityBeginFrame();
        CHECK(seen[8]);
    } else if (!strcmp(mode, "diagnostic-fields")) {
        // One window of frames with known reserve traffic and one deliberately slow frame.
        CeTerrainQualityBeginFrame();
        for (int i=0; i<5; ++i) CeTerrainQualityReserveUse(1, true);
        for (int i=0; i<2; ++i) CeTerrainQualityReserveUse(1, false);
        for (int i=0; i<3; ++i) CeTerrainQualityReserveUse(2, false);
        CeTerrainQualityReserveUse(2, true);
        for (int i=0; i<7; ++i) CeTerrainQualityReserveUse(0, true);
        for (int i=0; i<4; ++i) CeTerrainQualityReserveUse(0, false);
        for (int i=0; i<121; ++i) {
            if (i == 10) usleep(25000);
            CeTerrainQualityBeginFrame();
        }
        CHECK(logs >= 2 && lastReport[0] != 0);
        CHECK(strstr(lastReport, "terrain-detail window frame=") == lastReport);
        CHECK(longestReport < 600);             // the session log keeps 640 characters a line
        CHECK(Field("reserve_grant0") == 7 && Field("reserve_grant1") == 5 && Field("reserve_grant2") == 1);
        CHECK(Field("reserve_refuse0") == 4 && Field("reserve_refuse1") == 2 && Field("reserve_refuse2") == 3);
        const long n = Field("frame_us_n"), mean = Field("frame_us_mean"), p50 = Field("frame_us_p50");
        const long p95 = Field("frame_us_p95"), p99 = Field("frame_us_p99"), max = Field("frame_us_max");
        CHECK(n >= 100 && n <= 120);
        CHECK(p50 >= 0 && p50 <= p95 && p95 <= p99 && p99 <= max && mean <= max);
        CHECK(max >= 25000 && p50 < 25000);
        CHECK(Field("frame_over_16ms") >= 1 && Field("frame_over_16ms") <= n && Field("frame_over_33ms") <= Field("frame_over_16ms"));
        // The next window starts clean: its counters do not carry the previous traffic.
        for (int i=0; i<121; ++i) CeTerrainQualityBeginFrame();
        CHECK(Field("reserve_grant0") == 0 && Field("reserve_grant1") == 0 && Field("reserve_refuse0") == 0 && Field("reserve_refuse2") == 0);
        CHECK(Field("frame_us_max") < 25000);
    } else if (!strcmp(mode, "diagnostic-off")) {
        // Without the diagnostic nothing is counted, timed or logged, and the accessor says so.
        CHECK(!CeTerrainQualityDiagnostics());
        CeTerrainQualityBeginFrame();
        CeTerrainQualityReserveUse(1, true);
        for (int i=0; i<500; ++i) CeTerrainQualityBeginFrame();
        CHECK(logs == 0 && lastReport[0] == 0);
    } else return 2;
    puts("PASS");
}
