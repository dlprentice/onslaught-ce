#include "../network/network.h"
// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_boot.h"
#include "common.h"
#include "CLIParams.h"
#include "../platform/common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char DEFAULT_OPTIONS_NAME[];
extern int PLAYABLE_DEMO;
extern "C" int CE_CheatOn(int);
extern "C" int CE_CheatCachedStatus(unsigned *);
extern "C" void CE_NetWindowAutomationPrepare(void);
extern "C" void CE_NetUpdateAutomationPrepare(void);

namespace {
int requested;
int started;
int prepared;
int role;
int lockstep;	// admitted for the two-player lockstep co-op (levels 100, 856, 858), not the level-852 engineering route
char failure[192];
bool Present(const char *name) { const char *value = getenv(name); return value && *value; }
int Refuse(const char *message) {
    CE_BootCampaignClear();
    __atomic_store_n(&prepared, 0, __ATOMIC_RELEASE);
    CE_CompatibilityRuntimeMutation(NULL, 0);
    snprintf(failure, sizeof failure, "%s", message);
    fprintf(stderr, "Onslaught CE: network startup refused: %s\n", failure);
    return 0;
}
bool OrdinaryRoute() {
    return (CLIPARAMS.mLevelNo == 852 || CE_CompatibilityLockstepLevel((uint32_t)CLIPARAMS.mLevelNo)) && CLIPARAMS.mConfigurationNo == 0 && !E3_BUILD && !PLAYABLE_DEMO &&
        !CLIPARAMS.mDeveloperMode && !CLIPARAMS.mArtistTest && !CLIPARAMS.mBuildResources &&
        !CLIPARAMS.mBuildPCResources && !CLIPARAMS.mBuildPS2Resources && !CLIPARAMS.mBuildXBOXResources &&
        !CLIPARAMS.mNoBaseResources && !CLIPARAMS.mBuildModelInfo && !CLIPARAMS.mBuildGoodies &&
        !CLIPARAMS.mResBuilderMode && !CLIPARAMS.mAutoConfigTest && !CLIPARAMS.mRecordDemo &&
        !CLIPARAMS.mPlaybackDemo && !CLIPARAMS.mUnknown17d && !CLIPARAMS.mStressTest &&
        !CLIPARAMS.mAttractMode && CLIPARAMS.mGoStraightToDeviceSelectScreen == -1 &&
        !CLIPARAMS.mCardID && !strcmp(DEFAULT_OPTIONS_NAME, "defaultoptions.bea");
}
}

extern "C" int CE_BootRequested(void) { return __atomic_load_n(&requested, __ATOMIC_ACQUIRE); }
extern "C" const char *CE_BootError(void) { return failure; }
// The process was admitted, from its lobby handoff, for the lockstep co-op: the level it was started on is one of the co-op levels.
extern "C" int CE_BootLockstep(void) { return CE_BootRequested() && __atomic_load_n(&prepared, __ATOMIC_ACQUIRE) && lockstep; }

extern "C" int CE_BootStart(void) {
    bool host = CE_NETWORK_ENABLED && Present("BEA_NET_HOST"), guest = CE_NETWORK_ENABLED && Present("BEA_NET_JOIN");
    if (!host && !guest) return 1;
    __atomic_store_n(&requested, 1, __ATOMIC_RELEASE);
    if (started++) return Refuse("network startup cannot be repeated in this process");
    if (host == guest) return Refuse("select exactly one network role");
    role = host ? 1 : 2;
    if (!OrdinaryRoute()) return Refuse("networking requires the ordinary route to level 852 or a co-op level (100, 856, 858) and the default options file");
    lockstep = CLIPARAMS.mLevelNo != 852;
    if (Present("BEA_NET_COMPAT")) return Refuse("network compatibility is computed locally, not supplied by the caller");
    const char *automation[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_RECORD", "BEA_PORT_PROBES", "BEA_PORT_PILOT"};
    for (const char *name : automation)
        if (Present(name)) return Refuse("networking requires ordinary live input and time");
    char error[192] = {};
    if (!CE_CompatibilityValidateLauncherRoots(error, sizeof error)) return Refuse(error);
#ifdef _WIN32
    // Only after validating the original byte strings: legacy initialization has fixed-size caches.
    PortInitializeContentRoots();
#endif
    CE_NetWindowAutomationPrepare();
    CE_NetUpdateAutomationPrepare();
    // The genuine local settings owner performs its normal read now. Later validation reads only this cache.
    CE_CheatOn(0);
    unsigned cheats = 0;
    if (!CE_CheatCachedStatus(&cheats)) return Refuse("Onslaught CE could not read its settings to check that no cheat is on (a settings folder reached through a link is refused; under Wine keep the user folder on C:)");
    if (cheats) return Refuse("disable gameplay cheats before starting a network process");
    CE_CompatibilityResult result = {};
    if (!CE_CompatibilityPrepare(lockstep ? CE_COMPAT_LOCKSTEP_PROTOCOL1 : CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5, (uint32_t)CLIPARAMS.mLevelNo, &result, error, sizeof error))
        return Refuse(error);
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    if (!CE_LobbySignalingBootPrepared(result.digest, uint32_t(CLIPARAMS.mLevelNo)))
        return Refuse("Authenticated online start authority could not be restored.");
#endif
#endif
    __atomic_store_n(&prepared, 1, __ATOMIC_RELEASE);
    return 1;
}

extern "C" int CE_BootPrepared(CE_CompatibilityResult *result, char *error, size_t size) {
    if (CE_BootRequested() && __atomic_load_n(&prepared, __ATOMIC_ACQUIRE) &&
        ((role == 1 ? !Present("BEA_NET_HOST") || Present("BEA_NET_JOIN") :
                      !Present("BEA_NET_JOIN") || Present("BEA_NET_HOST"))))
        Refuse("network role changed after fresh-process admission");
    if (!CE_BootRequested() || !__atomic_load_n(&prepared, __ATOMIC_ACQUIRE)) {
        if (result) memset(result, 0, sizeof *result);
        if (error && size) snprintf(error, size, "%s", failure[0] ? failure : "no fresh network startup admission");
        return 0;
    }
    return CE_CompatibilityPrepared(result, error, size);
}

extern "C" int CE_BootAutomaticScript(void) {
    if (!CE_BootRequested()) return 1;
    char error[192] = {};
    CE_CompatibilityResult result = {};
    // CheckBoundary reopens the real root script and requires it to remain absent or zero bytes.
    if (!CE_BootPrepared(&result, error, sizeof error) || !CE_CompatibilityCheckBoundary(error, sizeof error)) {
        Refuse(error); return -1;
    }
    return 0;
}

extern "C" int CE_BootLocalFileAllowed(unsigned owner, const char *name, const char *mode) {
    if (!name || !mode) return 0;
    switch (owner) {
    case CE_BOOT_OPTIONS:
        return !strcmp(name, "defaultoptions.bea") && (!strcmp(mode, "rb") || !strcmp(mode, "wb"));
    case CE_BOOT_CARD_ID:
        return !strcmp(name, "cardid.txt") && !strcmp(mode, "ra");
    case CE_BOOT_SETUP_LOG:
        return !strcmp(name, "setuphistory.txt") && (!strcmp(mode, "w") || !strcmp(mode, "a"));
    case CE_BOOT_EXCEPTION_LOG:
        return !strcmp(name, "OnslaughtException.txt") && !strcmp(mode, "wt");
    default: return 0;
    }
}
extern "C" FILE *CE_BootOpenLocal(CE_BootLocalFile owner, const char *name, const char *mode) {
    return bea_crt_local_fopen(static_cast<unsigned>(owner), name, mode);
}


namespace {
CE_CampaignLaunch campaign_launch = {};
int campaign_host = 0;
int campaign_admitted = 0;
}

void CE_BootCampaignClear(void) {
    CE_CodeWipe(&campaign_launch, sizeof(campaign_launch));
    campaign_host = 0;
    campaign_admitted = 0;
}

int CE_BootCampaignAdmit(const CE_Handoff *handoff) {
    CE_BootCampaignClear();
    if (!CE_HandoffCampaignValid(handoff)) return 0;
    if (!handoff->campaign.version) return 1;
    campaign_launch = handoff->campaign;
    campaign_host = handoff->role == CE_NET_HOST;
    campaign_admitted = 1;
    return 1;
}

int CE_BootCampaignGet(CE_CampaignLaunch *out, int *host) {
    if (out) memset(out, 0, sizeof(*out));
    if (host) *host = 0;
    if (!campaign_admitted) return 0;
    if (out) *out = campaign_launch;
    if (host) *host = campaign_host;
    return 1;
}
