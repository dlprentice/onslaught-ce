// SPDX-License-Identifier: GPL-3.0-only
// Admit the existing optional render paths by capabilities and observed resources, never by a GPU name.
#ifdef BEA_GRAPHICS_POLICY_TEST
#include "ce_graphics_policy_support.h"
#else
#include "common.h"
#include "tweak.h"
#include "../platform/common/portlog.h"
#endif
#include "ce_graphics_policy.h"
extern "C" int PortCeRecordFeaturesAllowed(void) __attribute__((weak));
#include <stdlib.h>
#include <errno.h>

extern CTweakSLONG GEFORCE_FX_POWER, EXTRA_GEFORCE_FX_FEATURES, R32F_WORKS, MY_READABLE_Z_BUFFER;
bool PortCeLayer();
extern "C" int CE_SettingGetBool(const char *, int);
extern "C" int CE_SettingGetText(const char *, char *, int);
extern "C" int CE_SettingSet(const char *, int);
extern "C" int CE_FullUnitModelsRequest(int);	// platform/common/model_quality.cpp
extern "C" int CE_FullTreeModelsRequest(int);	// platform/common/model_quality.cpp
extern "C" int CE_TerrainDetailRequest(int);	// platform/common/terrain_quality.cpp
extern "C" int CE_AnisoRequest(int);	// platform/common/aniso_quality.cpp
extern "C" unsigned CE_AnisoHardwareMax();	// platform/common/aniso_quality.cpp
extern "C" int CE_WaterReflectionRequest(int);	// platform/common/reflection_quality.cpp
extern "C" int CE_DynamicShadowRequest(int);	// platform/common/reflection_quality.cpp
extern "C" unsigned CeRenderTargetQualityHardwareMaxDim();	// platform/common/reflection_quality.cpp
int PortUserFileState(const char *rel);	// port/platform/<target>: 1 the writable folder has its own copy of the file, 0 none, -1 not known

namespace
{
bool initialized, requested, capable, freshOptionsPending;
unsigned seenResources, failedResources;
PortCeDetailPresetStager detailPresetStager;

int ClampPresetAniso(int level)
{
    const unsigned maxHw = CE_AnisoHardwareMax();
    if (maxHw < 2u) return 0;
    if (level > 8 && maxHw < 16u) level = 8;
    if (level > 4 && maxHw < 8u) level = 4;
    return level;
}

int ClampPresetWaterRes(int res)
{
    const unsigned maxDim = CeRenderTargetQualityHardwareMaxDim();
    if (res > 512 && maxDim < 1024u) res = 512;
    if (res > 256 && maxDim < 512u) res = 256;
    return res;
}

int ClampPresetShadowRes(int res)
{
    const unsigned maxDim = CeRenderTargetQualityHardwareMaxDim();
    if (res > 128 && maxDim < 256u) res = 128;
    if (res > 64 && maxDim < 128u) res = 64;
    return res;
}

// Video Options holds six beyond-retail quality rows whose choices live in ce.ini, not in the options file:
// High-detail units (full_unit_models=1), 3D distant trees (full_tree_models=0, opt-in because unbatched 3D
// trees dominate frame cost on forest levels), Terrain detail distance (terrain_detail_distance=1),
// Anisotropic filtering (anisotropic_filtering=16), Water reflections (water_reflection_res=512), and
// Dynamic shadows (dynamic_shadow_res=128). A valid value already in ce.ini is a saved choice and is requested
// as it stands; legacy full_models=1 enables both unit and tree models when neither split key is saved. With
// no saved line the fresh choice is requested and written once; an unwritable ce.ini leaves a session-only
// request, and a refused request (faithful run) writes nothing.
const int kFreshUnitModels = 1;
const int kFreshTreeModels = 0;
const int kFreshTerrainDetail = 1;
const int kFreshAnisotropicFiltering = 16;
const int kFreshWaterReflectionRes = 512;
const int kFreshDynamicShadowRes = 128;

void FreshBeyondRetailRow(const char *key, int (*request)(int), int fresh)
{
    const int saved = CE_SettingGetBool(key, -1);
    if (saved >= 0) request(saved);
    else if (request(fresh)) CE_SettingSet(key, fresh);
}

int SavedIntSetting(const char *key)
{
    char text[32] = {};
    if (!CE_SettingGetText(key, text, int(sizeof(text)))) return -1;
    const char *p = text;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) return -1;
    char *end = NULL;
    const long parsed = strtol(p, &end, 10);
    if (end == p) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
    if (*end) return -1;
    return int(parsed);
}

int SavedAnisoSetting(const char *key)
{
    const int value = SavedIntSetting(key);
    return (value == 0 || value == 4 || value == 8 || value == 16) ? value : -1;
}

void FreshAnisoRow(const char *key, int (*request)(int), int fresh)
{
    const int saved = SavedAnisoSetting(key);
    if (saved >= 0) request(saved);
    else if (request(fresh)) CE_SettingSet(key, fresh);
}

int SavedWaterReflectionSetting(const char *key)
{
    const int value = SavedIntSetting(key);
    return (value == 256 || value == 512 || value == 1024) ? value : -1;
}

void FreshWaterReflectionRow(const char *key, int (*request)(int), int fresh)
{
    const int saved = SavedWaterReflectionSetting(key);
    if (saved >= 0) request(saved);
    else if (request(fresh)) CE_SettingSet(key, fresh);
}

int SavedDynamicShadowSetting(const char *key)
{
    const int value = SavedIntSetting(key);
    return (value == 64 || value == 128 || value == 256) ? value : -1;
}

void FreshDynamicShadowRow(const char *key, int (*request)(int), int fresh)
{
    const int saved = SavedDynamicShadowSetting(key);
    if (saved >= 0) request(saved);
    else if (request(fresh)) CE_SettingSet(key, fresh);
}

void Report(const char *event)
{
    PortSessionLog(PORT_SESSION_VIDEO, failedResources != 0,
        "extra graphics event=%s capable=%d requested=%d active=%d checked_resources=%u failed_resources=%u",
        event, capable ? 1 : 0, requested ? 1 : 0,
        GEFORCE_FX_POWER.mValue && EXTRA_GEFORCE_FX_FEATURES.mValue ? 1 : 0,
        seenResources, failedResources);
}
}

bool PortCeGraphicsPolicyActive()
{
#ifdef CE_VERSION
    if (!(PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed()))
    {
        const char *names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};
        for (const char *name : names)
        {
            const char *value = getenv(name);
            if (value && *value) return false;
        }
    }
    return PortCeLayer();
#else
    return false;
#endif
}

int PortCeDetailLandscape(int preset, int choices, int high)
{
    if (!PortCeGraphicsPolicyActive() || choices < 1) return preset;
    // The row's setter saturates a selection beyond its last choice, so applying a preset that names one leaves the last choice.
    if (high) return choices - 1;
    return preset < choices ? preset : choices - 1;
}

bool PortCeDetailAntiAliasingHigh(int selection, int gameHigh)
{
    return selection == gameHigh || (selection > gameHigh && PortCeGraphicsPolicyActive());
}

bool PortCeGraphicsResolveDetailPreset(int detailLevel, CeDetailPreset *out)
{
    if (!out || !PortCeGraphicsPolicyActive()) return false;
    CeDetailPreset preset = { 1, 0, 1, 16, 512, 128 };
    if (detailLevel == 1)
    {
        preset.unitModels = 0; preset.treeModels = 0; preset.terrainDetail = 0;
        preset.aniso = 0; preset.waterRes = 256; preset.shadowRes = 64;
    }
    else if (detailLevel == 2)
    {
        preset.unitModels = 1; preset.treeModels = 0; preset.terrainDetail = 0;
        preset.aniso = 4; preset.waterRes = 512; preset.shadowRes = 128;
    }
    else if (detailLevel != 3)
    {
        return false;
    }
    preset.aniso = ClampPresetAniso(preset.aniso);
    preset.waterRes = ClampPresetWaterRes(preset.waterRes);
    preset.shadowRes = ClampPresetShadowRes(preset.shadowRes);
    *out = preset;
    return true;
}

void PortCeGraphicsRegisterDetailPresetStager(PortCeDetailPresetStager stager)
{
    detailPresetStager = stager;
}

void PortCeGraphicsStageDetailPreset(int detailLevel)
{
    CeDetailPreset preset;
    if (!PortCeGraphicsResolveDetailPreset(detailLevel, &preset)) return;
    if (detailPresetStager) detailPresetStager(preset);
}

void PortCeGraphicsApplyDetailPreset(int detailLevel)
{
    CeDetailPreset preset;
    if (!PortCeGraphicsResolveDetailPreset(detailLevel, &preset)) return;
    if (CE_FullUnitModelsRequest(preset.unitModels)) CE_SettingSet("full_unit_models", preset.unitModels);
    if (CE_FullTreeModelsRequest(preset.treeModels)) CE_SettingSet("full_tree_models", preset.treeModels);
    if (CE_TerrainDetailRequest(preset.terrainDetail)) CE_SettingSet("terrain_detail_distance", preset.terrainDetail);
    if (CE_AnisoRequest(preset.aniso)) CE_SettingSet("anisotropic_filtering", preset.aniso);
    if (CE_WaterReflectionRequest(preset.waterRes)) CE_SettingSet("water_reflection_res", preset.waterRes);
    if (CE_DynamicShadowRequest(preset.shadowRes)) CE_SettingSet("dynamic_shadow_res", preset.shadowRes);
}

bool PortCeGraphicsFloatTargetRequired()
{
    return PortCeGraphicsPolicyActive() && MY_READABLE_Z_BUFFER.mValue != 0;
}

void PortCeGraphicsOptionsOpened(bool opened, int error)
{
    if (!PortCeGraphicsPolicyActive()) return;
    // A read that finds no options file of the CE profile falls back to the game folder's (the path layer: PortUserFileState), which is
    // whatever the retail game left there; PARITY ("Why a fresh profile read Geometry detail Medium") measured that the open then
    // reports a file and CCareer::Load reads its options from it. That file is not a choice made in CE, so
    // only the profile's own copy counts as saved options: absent when nothing opened (ENOENT) or when the open was served by
    // the game folder (the profile has none). An unreadable file, an unknown state and every other error keep the profile as it is.
    // The name is the game's DEFAULT_OPTIONS_NAME; ce_graphics_defaults_tests.py checks they stay equal.
    freshOptionsPending = opened ? PortUserFileState("defaultoptions.bea") == 0 : error == ENOENT;
}

bool PortCeGraphicsTakeFreshDefaults()
{
    if (!PortCeGraphicsPolicyActive() || !freshOptionsPending) return false;
    freshOptionsPending = false;
    const int savedUnit = CE_SettingGetBool("full_unit_models", -1);
    const int savedTree = CE_SettingGetBool("full_tree_models", -1);
    const int savedLegacy = (savedUnit < 0 && savedTree < 0) ? CE_SettingGetBool("full_models", -1) : -1;
    if (savedLegacy == 1)
    {
        CE_FullUnitModelsRequest(1);
        CE_FullTreeModelsRequest(1);
    }
    else
    {
        if (savedUnit >= 0) CE_FullUnitModelsRequest(savedUnit);
        else if (CE_FullUnitModelsRequest(kFreshUnitModels)) CE_SettingSet("full_unit_models", kFreshUnitModels);
        if (savedTree >= 0) CE_FullTreeModelsRequest(savedTree);
        else if (CE_FullTreeModelsRequest(kFreshTreeModels)) CE_SettingSet("full_tree_models", kFreshTreeModels);
    }
    FreshBeyondRetailRow("terrain_detail_distance", CE_TerrainDetailRequest, kFreshTerrainDetail);
    FreshAnisoRow("anisotropic_filtering", CE_AnisoRequest, kFreshAnisotropicFiltering);
    FreshWaterReflectionRow("water_reflection_res", CE_WaterReflectionRequest, kFreshWaterReflectionRes);
    FreshDynamicShadowRow("dynamic_shadow_res", CE_DynamicShadowRequest, kFreshDynamicShadowRes);
    return true;
}

void PortCeGraphicsBeginDevice(unsigned pixelVersion, unsigned vertexVersion, bool formats, bool float32)
{
    if (!PortCeGraphicsPolicyActive()) return;
    if (!initialized)
    {
        // A valid extra_graphics line is the player's saved choice. With none, only a new profile asks for the extra paths: a profile that
        // already has options of its own keeps them off until the player chooses Yes, as it was before this row could be offered. The game's
        // own tweak is no evidence either way (it starts set, the options file does not hold it, and the game clears it only after the first
        // device exists). The options file does not hold the row, so the new profile's choice is written once, like the rows above, or its
        // next start would find saved options and no line; an unwritable ce.ini leaves the choice for this session only.
        const int saved = CE_SettingGetBool("extra_graphics", -1);
        requested = saved >= 0 ? saved != 0 : freshOptionsPending;
        if (saved < 0 && freshOptionsPending) CE_SettingSet("extra_graphics", 1);
        initialized = true;
    }
    // These are the shader models used by the existing optional shaders; format support is queried on the
    // actual provider and adapter. Resource creation can still fail and is reported independently below.
    capable = (pixelVersion & 0xffffu) >= 0x0200u && (vertexVersion & 0xffffu) >= 0x0101u && formats;
    seenResources = failedResources = 0;
    // The capability tweak follows the device alone, whether or not the extra paths are asked for: the row is offered by it, and the game
    // lays the shore surf out by it when a level loads, so it has to hold one value from a level's load to its end.
    GEFORCE_FX_POWER.Set(capable ? 1.f : 0.f);
    if (capable) R32F_WORKS.Set(float32 ? 1.f : 0.f);
    EXTRA_GEFORCE_FX_FEATURES.Set(capable && requested ? 1.f : 0.f);
    // Native setters retain their quality side effects. They run once, after real device capabilities exist,
    // and before any resource initialization. Saved profiles and failed opens never enter this path.
#ifdef CE_VERSION
    PortCeGraphicsApplyFreshDefaults();
#endif
    Report("device-capabilities");
}

bool PortCeGraphicsRequested()
{
    return PortCeGraphicsPolicyActive() && initialized && capable && requested;
}

bool PortCeGraphicsRequestFailed()
{
    return PortCeGraphicsRequested() && failedResources != 0;
}

void PortCeGraphicsResourceOutcome(unsigned resource, bool ready)
{
    if (!PortCeGraphicsRequested()) return;
    const unsigned known = CE_GRAPHICS_WATER | CE_GRAPHICS_LANDSCAPE | CE_GRAPHICS_PARTICLES |
                           CE_GRAPHICS_LANDSCAPE_VERTEX | CE_GRAPHICS_DEVICE;
    if (!resource || (resource & ~known)) return;
    const unsigned previousSeen = seenResources, previousFailed = failedResources;
    seenResources |= resource;
    if (!ready)
    {
        failedResources |= resource;
        // As the original does on a refused resource: the feature tweak alone. The capability tweak stays as the device set it (above).
        EXTRA_GEFORCE_FX_FEATURES.Set(0.f);
    }
    // A later success does not revive an incomplete generation. Retry only after a complete device rebuild.
    if (seenResources != previousSeen || failedResources != previousFailed) Report("resource-outcome");
}

void PortCeGraphicsFinishDevice(bool ready)
{
    if (!PortCeGraphicsPolicyActive()) return;
    PortCeGraphicsResourceOutcome(CE_GRAPHICS_DEVICE, ready);
    Report(ready ? "device-ready" : "device-failed");
}

bool PortCeGraphicsStoreRequest(int enabled)
{
    if (!PortCeGraphicsPolicyActive()) return true;
    if (enabled != 0 && enabled != 1) return false;
    // The choice holds for this session whether or not ce.ini can keep it, as the beyond-retail quality rows do; false tells the
    // row that it was not saved.
    requested = enabled != 0;
    initialized = true;
    return CE_SettingSet("extra_graphics", enabled) != 0;
}
