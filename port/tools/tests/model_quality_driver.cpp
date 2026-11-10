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

static int initialChoice, reads, layerCalls, logs;
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
    assert(strcmp(key, "full_models") == 0);
    assert(fallback == 0);
    ++reads;
    return initialChoice;
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
        initialChoice = 1;
        assert(!CE_FullModelsRequest(1));
        assert(CE_FullModelsChoice() == 0);
        CeModelQualityBeginFrame();
        assert(!CeModelQualityEnabled());
        CeModelQualityCount(CE_MODEL_MESH);
        assert(reads == 0 && logs == 0);
#ifndef CE_VERSION
        assert(layerCalls == 0);
#endif
    } else if (!strcmp(scenario, "default")) {
        assert(!CeModelQualityEnabled());
        CeModelQualityBeginFrame();
        assert(!CeModelQualityEnabled());
        assert(CE_FullModelsChoice() == 0);
        assert(reads == 1);
        assert(logs == 0);
    } else if (!strcmp(scenario, "stored")) {
        initialChoice = 1;
        assert(CE_FullModelsChoice() == 1);
        assert(!CeModelQualityEnabled());
        CeModelQualityBeginFrame();
        assert(CeModelQualityEnabled());
        initialChoice = 0;
        for (int i=0; i<5; ++i) CeModelQualityBeginFrame();
        assert(CeModelQualityEnabled());
        assert(reads == 1);
    } else if (!strcmp(scenario, "switch")) {
        CeModelQualityBeginFrame();
        assert(!CeModelQualityEnabled());
        assert(!CE_FullModelsRequest(-1));
        assert(!CE_FullModelsRequest(2));
        assert(CE_FullModelsRequest(1));
        assert(CE_FullModelsChoice() == 1);
        // The outstanding mirror and player views still share the old snapshot.
        for (int i=0; i<4; ++i) assert(!CeModelQualityEnabled());
        CeModelQualityBeginFrame();
        assert(CeModelQualityEnabled());
        assert(CE_FullModelsRequest(0));
        assert(CE_FullModelsChoice() == 0);
        for (int i=0; i<4; ++i) assert(CeModelQualityEnabled());
        CeModelQualityBeginFrame();
        assert(!CeModelQualityEnabled());
        assert(reads == 1);
    } else if (!strcmp(scenario, "queued-first")) {
        assert(CE_FullModelsRequest(1));
        assert(!CeModelQualityEnabled());
        assert(CE_FullModelsChoice() == 1);
        CeModelQualityBeginFrame();
        assert(CeModelQualityEnabled());
        assert(reads == 0);
        assert(CE_FullModelsRequest(0));
        assert(CE_FullModelsRequest(1));
        assert(CE_FullModelsRequest(0));
        CeModelQualityBeginFrame();
        assert(!CeModelQualityEnabled());
    } else if (!strcmp(scenario, "diagnostics")) {
        CeModelQualityBeginFrame();
        for (int i=0; i<120*100; ++i) {
            for (int event=0; event<CE_MODEL_EVENTS; ++event)
                CeModelQualityCount(static_cast<CeModelQualityEvent>(event));
            CeModelQualityCount(static_cast<CeModelQualityEvent>(-1));
            CeModelQualityCount(static_cast<CeModelQualityEvent>(CE_MODEL_EVENTS));
            CeModelQualityBeginFrame();
        }
        assert(logs == 64);
        assert(!CeModelQualityEnabled());
        assert(reads == 1);
    } else {
        return 2;
    }
    puts("PASS");
}
