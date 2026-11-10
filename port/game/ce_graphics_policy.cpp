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
#include <stdlib.h>
#include <errno.h>

extern CTweakSLONG GEFORCE_FX_POWER, EXTRA_GEFORCE_FX_FEATURES, R32F_WORKS, MY_READABLE_Z_BUFFER;
bool PortCeLayer();
extern "C" int CE_SettingGetBool(const char *, int);
extern "C" int CE_SettingSet(const char *, int);
extern "C" int CE_FullModelsRequest(int);	// platform/common/model_quality.cpp
extern "C" int CE_TerrainDetailRequest(int);	// platform/common/terrain_quality.cpp
int PortUserFileState(const char *rel);	// port/platform/<target>: 1 the writable folder has its own copy of the file, 0 none, -1 not known

namespace
{
bool initialized, requested, capable, freshOptionsPending;
unsigned seenResources, failedResources;

// Video Options also holds two beyond-retail quality rows whose choices live in ce.ini, not in the options file: Full models and
// Terrain detail distance. A new profile starts Full models one choice below its maximum (Off): it is almost all of the frame-rate cost of the
// maximums (PARITY). Terrain detail distance starts at its maximum (Extended) like every other row: the 0.1.14 measurement found no failed terrain
// allocation on levels 100 and 200 with the terrain reserve. Since the 2026-11-08 correction Extended reserves all three terrain tiers, and
// bounded split-screen controls found no failed request after it (PARITY, "Terrain reserve correction"). The maintainer chose Extended for new profiles; kFreshTerrainDetail is the
// one value to change (1 = Extended, 0 = Normal). A valid 0 or 1
// already in ce.ini is a saved choice and is requested as it stands. With no such line the fresh choice is requested and written once,
// so the profile keeps showing it after it has saved options of its own (the options file does not hold these rows); an unwritable
// ce.ini leaves a request for this session only. A request the layer refuses (a faithful run) writes nothing.
const int kFreshTerrainDetail = 1;

void FreshBeyondRetailRow(const char *key, int (*request)(int), int fresh)
{
    const int saved = CE_SettingGetBool(key, -1);
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
    const char *names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};
    for (const char *name : names)
    {
        const char *value = getenv(name);
        if (value && *value) return false;
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
    FreshBeyondRetailRow("full_models", CE_FullModelsRequest, 0);
    FreshBeyondRetailRow("terrain_detail_distance", CE_TerrainDetailRequest, kFreshTerrainDetail);
    return true;
}

void PortCeGraphicsBeginDevice(unsigned pixelVersion, unsigned vertexVersion, bool formats, bool float32)
{
    if (!PortCeGraphicsPolicyActive()) return;
    if (!initialized)
    {
        requested = CE_SettingGetBool("extra_graphics", freshOptionsPending || EXTRA_GEFORCE_FX_FEATURES.mValue != 0) != 0;
        initialized = true;
    }
    // These are the shader models used by the existing optional shaders; format support is queried on the
    // actual provider and adapter. Resource creation can still fail and is reported independently below.
    capable = (pixelVersion & 0xffffu) >= 0x0200u && (vertexVersion & 0xffffu) >= 0x0101u && formats;
    seenResources = failedResources = 0;
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
        GEFORCE_FX_POWER.Set(0.f);
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
    if ((enabled != 0 && enabled != 1) || !CE_SettingSet("extra_graphics", enabled)) return false;
    requested = enabled != 0;
    initialized = true;
    return true;
}
