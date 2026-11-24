// SPDX-License-Identifier: GPL-3.0-only
#include "reflection_quality.h"
#include "portlog.h"
#include <stdlib.h>

extern bool PortCeLayer();
extern "C" int CE_SettingGetText(const char *key, char *out, int n);

namespace {
int admitted = -1;

unsigned hardwareMaxDim = 1024u;
bool hardwareReported = false;

int waterRequested = -1;
bool waterActive = false;
unsigned waterActiveTarget = 256u;
unsigned waterAllocated = 0u;
unsigned waterAttemptedTarget = 0u;

int shadowRequested = -1;
bool shadowActive = false;
unsigned shadowActiveTarget = 64u;
unsigned shadowAllocated = 0u;
unsigned shadowAttemptedTarget = 0u;

bool Allowed()
{
    if (admitted >= 0) return admitted != 0;
    admitted = 0;
#ifdef CE_VERSION
    // A forced CE menu is never permission to alter faithful reference runs.
    const char *const fences[] = {
        "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"
    };
    for (const char *name : fences) {
        const char *value = getenv(name);
        if (value && *value) return false;
    }
    if (!PortCeLayer()) return false;
    admitted = 1;
#endif
    return admitted != 0;
}

int ParseIntSetting(const char *key)
{
    char text[32] = {};
    if (!CE_SettingGetText(key, text, static_cast<int>(sizeof(text))))
        return -1;
    const char *p = text;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) return -1;
    char *end = nullptr;
    const long parsed = strtol(p, &end, 10);
    if (end == p) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
    if (*end) return -1;
    return static_cast<int>(parsed);
}

bool ValidWaterRes(int res)
{
    return res == 256 || res == 512 || res == 1024;
}

bool ValidShadowRes(int res)
{
    return res == 64 || res == 128 || res == 256;
}

unsigned ClampWaterRes(int res)
{
    const unsigned cap = hardwareReported ? hardwareMaxDim : 1024u;
    unsigned target = ValidWaterRes(res) ? static_cast<unsigned>(res) : 256u;
    if (target > 512u && cap < 1024u) target = 512u;
    if (target > 256u && cap < 512u) target = 256u;
    return target;
}

unsigned ClampShadowRes(int res)
{
    const unsigned cap = hardwareReported ? hardwareMaxDim : 1024u;
    unsigned target = ValidShadowRes(res) ? static_cast<unsigned>(res) : 64u;
    if (target > 128u && cap < 256u) target = 128u;
    if (target > 64u && cap < 128u) target = 64u;
    return target;
}

void InitWaterChoice()
{
    if (waterRequested >= 0) return;
    const int saved = ParseIntSetting("water_reflection_res");
    waterRequested = ValidWaterRes(saved) ? saved : 512;
}

void InitShadowChoice()
{
    if (shadowRequested >= 0) return;
    const int saved = ParseIntSetting("dynamic_shadow_res");
    shadowRequested = ValidShadowRes(saved) ? saved : 128;
}
}

extern "C" void CeRenderTargetQualitySetHardwareCaps(unsigned maxTexWidth, unsigned maxTexHeight)
{
    hardwareReported = true;
    const unsigned minDim = maxTexWidth < maxTexHeight ? maxTexWidth : maxTexHeight;
    hardwareMaxDim = minDim > 0u ? minDim : 256u;
}

extern "C" unsigned CeRenderTargetQualityHardwareMaxDim()
{
    return hardwareReported ? hardwareMaxDim : 1024u;
}

extern "C" int CE_WaterReflectionChoiceToRes(int choice)
{
    switch (choice) {
    case 0: return 256;
    case 1: return 512;
    case 2: return 1024;
    default: return -1;
    }
}

extern "C" int CE_WaterReflectionResToChoice(int res)
{
    switch (res) {
    case 256: return 0;
    case 512: return 1;
    case 1024: return 2;
    default: return -1;
    }
}

extern "C" int CE_WaterReflectionResChoice()
{
    if (!Allowed()) return 256;
    InitWaterChoice();
    return waterRequested;
}

extern "C" int CE_WaterReflectionChoice()
{
    return CE_WaterReflectionResToChoice(CE_WaterReflectionResChoice());
}

extern "C" int CE_WaterReflectionRequest(int res)
{
    if (!ValidWaterRes(res) || !Allowed()) return 0;
    waterRequested = res;
    return 1;
}

extern "C" int CE_WaterReflectionAllocatedRes()
{
    return waterActive ? static_cast<int>(waterAllocated) : 0;
}

void CeWaterReflectionBeginFrame()
{
    if (!Allowed()) {
        waterActive = false;
        waterActiveTarget = 256u;
        return;
    }
    InitWaterChoice();
    waterActive = true;
    waterActiveTarget = ClampWaterRes(waterRequested);
}

bool CeWaterReflectionQualityActive()
{
    return waterActive;
}

unsigned CeWaterReflectionTargetRes()
{
    return waterActive ? waterActiveTarget : 256u;
}

unsigned CeWaterReflectionNextFallback(unsigned failedRes)
{
    if (failedRes > 512u) return 512u;
    if (failedRes > 256u) return 256u;
    return 0u;
}

void CeWaterReflectionSetAllocatedRes(unsigned allocatedRes, unsigned attemptedTarget)
{
    const unsigned prev = waterAllocated;
    waterAllocated = allocatedRes;
    waterAttemptedTarget = attemptedTarget;
    if (waterActive && allocatedRes > 0u && allocatedRes != prev) {
        PortSessionLog(PORT_SESSION_VIDEO, allocatedRes < attemptedTarget,
            "water-reflections active=%u target=%u hw_max=%u",
            allocatedRes, attemptedTarget, CeRenderTargetQualityHardwareMaxDim());
    }
}

void CeWaterReflectionResetAllocated()
{
    waterAllocated = 0u;
    waterAttemptedTarget = 0u;
}

unsigned CeWaterReflectionActiveRes()
{
    if (!waterActive) return 256u;
    return waterAllocated >= 256u ? waterAllocated : 256u;
}

bool CeWaterReflectionNeedsRebuild()
{
    return waterActive && waterAllocated != 0u && waterActiveTarget != waterAttemptedTarget;
}

float CeWaterReflectionTapOffset()
{
    return 2.0f / static_cast<float>(CeWaterReflectionActiveRes());
}

extern "C" int CE_DynamicShadowChoiceToRes(int choice)
{
    switch (choice) {
    case 0: return 64;
    case 1: return 128;
    case 2: return 256;
    default: return -1;
    }
}

extern "C" int CE_DynamicShadowResToChoice(int res)
{
    switch (res) {
    case 64: return 0;
    case 128: return 1;
    case 256: return 2;
    default: return -1;
    }
}

extern "C" int CE_DynamicShadowResChoice()
{
    if (!Allowed()) return 64;
    InitShadowChoice();
    return shadowRequested;
}

extern "C" int CE_DynamicShadowChoice()
{
    return CE_DynamicShadowResToChoice(CE_DynamicShadowResChoice());
}

extern "C" int CE_DynamicShadowRequest(int res)
{
    if (!ValidShadowRes(res) || !Allowed()) return 0;
    shadowRequested = res;
    return 1;
}

extern "C" int CE_DynamicShadowAllocatedRes()
{
    return shadowActive ? static_cast<int>(shadowAllocated) : 0;
}

void CeDynamicShadowBeginFrame()
{
    if (!Allowed()) {
        shadowActive = false;
        shadowActiveTarget = 64u;
        return;
    }
    InitShadowChoice();
    shadowActive = true;
    shadowActiveTarget = ClampShadowRes(shadowRequested);
}

bool CeDynamicShadowQualityActive()
{
    return shadowActive;
}

unsigned CeDynamicShadowTargetRes()
{
    return shadowActive ? shadowActiveTarget : 64u;
}

unsigned CeDynamicShadowNextFallback(unsigned failedRes)
{
    if (failedRes > 128u) return 128u;
    if (failedRes > 64u) return 64u;
    return 0u;
}

void CeDynamicShadowSetAllocatedRes(unsigned allocatedRes, unsigned attemptedTarget)
{
    const unsigned prev = shadowAllocated;
    shadowAllocated = allocatedRes;
    shadowAttemptedTarget = attemptedTarget;
    if (shadowActive && allocatedRes > 0u && allocatedRes != prev) {
        PortSessionLog(PORT_SESSION_VIDEO, allocatedRes < attemptedTarget,
            "dynamic-shadows active=%u target=%u hw_max=%u",
            allocatedRes, attemptedTarget, CeRenderTargetQualityHardwareMaxDim());
    }
}

void CeDynamicShadowResetAllocated()
{
    shadowAllocated = 0u;
    shadowAttemptedTarget = 0u;
}

unsigned CeDynamicShadowActiveRes()
{
    if (!shadowActive) return 64u;
    return shadowAllocated >= 64u ? shadowAllocated : 64u;
}

bool CeDynamicShadowNeedsRebuild()
{
    return shadowActive && shadowAllocated != 0u && shadowActiveTarget != shadowAttemptedTarget;
}

float CeDynamicShadowTapOffset1(int size)
{
    const int res = size > 0 ? size : 64;
    return 1.0f / static_cast<float>(res);
}

float CeDynamicShadowTapOffset3(int size)
{
    const int res = size > 0 ? size : 64;
    return 1.0f / static_cast<float>(res * 3);
}
