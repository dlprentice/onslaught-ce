// SPDX-License-Identifier: GPL-3.0-only
#include "model_quality.h"
#include "portlog.h"
#include <SDL3/SDL_timer.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Deliberate mutant failures must not produce multi-gigabyte crash dumps.
static void Check(bool value, int line)
{
    if (value) return;
    fprintf(stderr, "Assertion failed in authored driver, line %d\n", line);
    exit(1);
}
#undef assert
#define assert(value) Check((value), __LINE__)

static int initialUnit = -1, initialTree = -1, initialLegacy = -1;
static int reads, layerCalls, logs;
extern "C" Uint64 SDL_GetTicksNS()
{
    static Uint64 tick = 0;
    tick += 1000000;
    return tick;
}
bool PortCeLayer()
{
    ++layerCalls;
    const char *off = getenv("DRIVER_LAYER_OFF");
    return !off || off[0] != '1';
}
extern "C" int CE_SettingGetBool(const char *key, int fallback)
{
    assert(fallback == -1);
    ++reads;
    if (strcmp(key, "full_unit_models") == 0) return initialUnit >= 0 ? initialUnit : fallback;
    if (strcmp(key, "full_tree_models") == 0) return initialTree >= 0 ? initialTree : fallback;
    if (strcmp(key, "full_models") == 0) return initialLegacy >= 0 ? initialLegacy : fallback;
    assert(false);
    return fallback;
}
void PortSessionLog(PortSessionCategory, bool error, const char *format, ...)
{
    assert(!error);
    assert(strstr(format, "full-models ") == format);
    char message[1024];
    va_list values;
    va_start(values, format);
    vsnprintf(message, sizeof(message), format, values);
    va_end(values);
    if (strstr(format, "dt_samples=")) {
        assert(strstr(message, "units="));
        assert(strstr(message, "trees="));
        assert(strstr(message, "median_us=1000 "));
        assert(strstr(message, "p95_us=1000 "));
        assert(strstr(message, "p99_us=1000 "));
        assert(strstr(message, "max_us=1000 "));
    }
    ++logs;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *scenario = argv[1];
    if (!strcmp(scenario, "fenced")) {
        initialUnit = 1;
        initialTree = 1;
        initialLegacy = 1;
        assert(!CE_FullUnitModelsRequest(1));
        assert(!CE_FullTreeModelsRequest(1));
        assert(!CE_FullModelsRequest(1));
        assert(CE_FullUnitModelsChoice() == 0);
        assert(CE_FullTreeModelsChoice() == 0);
        assert(CE_FullModelsChoice() == 0);
        CeModelQualityBeginFrame();
        assert(!CeUnitModelsQualityEnabled());
        assert(!CeTreeModelsQualityEnabled());
        assert(!CeModelQualityEnabled());
        CeModelQualityCount(CE_MODEL_MESH);
        assert(reads == 0 && logs == 0);
#ifndef CE_VERSION
        assert(layerCalls == 0);
#endif
    } else if (!strcmp(scenario, "default")) {
        assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        CeModelQualityBeginFrame();
        assert(CeUnitModelsQualityEnabled());
        assert(!CeTreeModelsQualityEnabled());
        assert(!CeModelQualityEnabled());
        assert(CE_FullUnitModelsChoice() == 1);
        assert(CE_FullTreeModelsChoice() == 0);
        assert(CE_FullModelsChoice() == 0);
        assert(reads == 3);
        assert(logs == 1);
    } else if (!strcmp(scenario, "legacy-on")) {
        initialLegacy = 1;
        assert(CE_FullUnitModelsChoice() == 1);
        assert(CE_FullTreeModelsChoice() == 1);
        assert(CE_FullModelsChoice() == 1);
        assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        CeModelQualityBeginFrame();
        assert(CeUnitModelsQualityEnabled());
        assert(CeTreeModelsQualityEnabled());
        assert(CeModelQualityEnabled());
        assert(reads == 3);
    } else if (!strcmp(scenario, "legacy-off")) {
        initialLegacy = 0;
        assert(CE_FullUnitModelsChoice() == 1);
        assert(CE_FullTreeModelsChoice() == 0);
        CeModelQualityBeginFrame();
        assert(CeUnitModelsQualityEnabled());
        assert(!CeTreeModelsQualityEnabled());
    } else if (!strcmp(scenario, "stored")) {
        initialUnit = 0;
        initialTree = 1;
        initialLegacy = 1;
        assert(CE_FullUnitModelsChoice() == 0);
        assert(CE_FullTreeModelsChoice() == 1);
        assert(CE_FullModelsChoice() == 0);
        assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        CeModelQualityBeginFrame();
        assert(!CeUnitModelsQualityEnabled());
        assert(CeTreeModelsQualityEnabled());
        initialUnit = 1;
        initialTree = 0;
        for (int i=0; i<5; ++i) CeModelQualityBeginFrame();
        assert(!CeUnitModelsQualityEnabled());
        assert(CeTreeModelsQualityEnabled());
        assert(reads == 2);
    } else if (!strcmp(scenario, "switch")) {
        initialUnit = 0;
        initialTree = 0;
        CeModelQualityBeginFrame();
        assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        assert(!CE_FullUnitModelsRequest(-1));
        assert(!CE_FullUnitModelsRequest(2));
        assert(!CE_FullTreeModelsRequest(-1));
        assert(!CE_FullTreeModelsRequest(2));
        assert(CE_FullUnitModelsRequest(1));
        assert(CE_FullUnitModelsChoice() == 1 && CE_FullTreeModelsChoice() == 0);
        // The outstanding mirror and player views still share the old snapshot.
        for (int i=0; i<4; ++i) assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        CeModelQualityBeginFrame();
        assert(CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        assert(CE_FullTreeModelsRequest(1));
        assert(CE_FullTreeModelsChoice() == 1);
        for (int i=0; i<4; ++i) assert(CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        CeModelQualityBeginFrame();
        assert(CeUnitModelsQualityEnabled() && CeTreeModelsQualityEnabled() && CeModelQualityEnabled());
        assert(CE_FullUnitModelsRequest(0));
        assert(CE_FullTreeModelsRequest(0));
        for (int i=0; i<4; ++i) assert(CeUnitModelsQualityEnabled() && CeTreeModelsQualityEnabled());
        CeModelQualityBeginFrame();
        assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        assert(reads == 2);
    } else if (!strcmp(scenario, "queued-first")) {
        assert(CE_FullUnitModelsRequest(0));
        assert(CE_FullTreeModelsRequest(1));
        assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        assert(CE_FullUnitModelsChoice() == 0 && CE_FullTreeModelsChoice() == 1);
        CeModelQualityBeginFrame();
        assert(!CeUnitModelsQualityEnabled() && CeTreeModelsQualityEnabled());
        assert(reads == 0);
        assert(CE_FullModelsRequest(1));
        assert(CE_FullModelsRequest(0));
        CeModelQualityBeginFrame();
        assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
    } else if (!strcmp(scenario, "diagnostics")) {
        initialUnit = 0;
        initialTree = 0;
        CeModelQualityBeginFrame();
        for (int i=0; i<120*100; ++i) {
            for (int event=0; event<CE_MODEL_EVENTS; ++event)
                CeModelQualityCount(static_cast<CeModelQualityEvent>(event));
            CeModelQualityCount(static_cast<CeModelQualityEvent>(-1));
            CeModelQualityCount(static_cast<CeModelQualityEvent>(CE_MODEL_EVENTS));
            CeModelQualityBeginFrame();
        }
        assert(logs == 64);
        assert(!CeUnitModelsQualityEnabled() && !CeTreeModelsQualityEnabled());
        assert(reads == 2);
    } else {
        return 2;
    }
    puts("PASS");
}
