// SPDX-License-Identifier: GPL-3.0-only
// Fresh-process admission and explicit local-file owners for the bounded network route.
#ifndef CE_NETWORK_BOOT_H
#define CE_NETWORK_BOOT_H
#include "../network/compatibility.h"
#include "../platform/common/ce_netcode.h"
// Public builds (build.py --public defines CE_NETWORK_OFF) have no network until it is published: no menu entry, and the
// BEA_NET_HOST / BEA_NET_JOIN roles are not read. Private builds, fixtures and --test-automation builds are unaffected.
#ifdef CE_NETWORK_OFF
#define CE_NETWORK_ENABLED 0
#else
#define CE_NETWORK_ENABLED 1
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Called once by the genuine entry point after command-line parsing, before options/content reads.
// Returns 1 offline or after successful admission, 0 on refusal. No caller-supplied digest is accepted.
int CE_BootStart(void);
int CE_BootRequested(void);
// 1 when the admitted process is for the lockstep co-op (levels 100, 856, 858), 0 for the level-852 route or no admission.
int CE_BootLockstep(void);
int CE_BootPrepared(CE_CompatibilityResult *result, char *error, size_t error_size);
const char *CE_BootError(void);
// 1: run the original offline script caller; 0: verified empty/absent automatic script; -1: refuse.
// This is only for the literal automatic startup-script call, never interactive or nested Exec.
int CE_BootAutomaticScript(void);

typedef enum CE_BootLocalFile {
    CE_BOOT_OPTIONS = 1,
    CE_BOOT_CARD_ID = 2,
    CE_BOOT_SETUP_LOG = 3,
    CE_BOOT_EXCEPTION_LOG = 4
} CE_BootLocalFile;
// Each caller names its actual owner. Armed routes accept only that owner's fixed name/modes.
// There is no ambient scope, environment switch or generic outside-data permission.
FILE *CE_BootOpenLocal(CE_BootLocalFile owner, const char *name, const char *mode);
// Platform/CRT implementation seams, not alternative game-facing readers.
int CE_BootLocalFileAllowed(unsigned owner, const char *name, const char *mode);
FILE *PortOpenLocalFile(unsigned owner, const char *name, const char *mode);
FILE *bea_crt_local_fopen(unsigned owner, const char *name, const char *mode);
/* Admission stores only the non-secret typed identity from the consumed handoff.
 * Any invalid admission clears earlier state. Ordinary launches remain ordinary. */
int CE_BootCampaignAdmit(const CE_Handoff *handoff);
int CE_BootCampaignGet(CE_CampaignLaunch *out, int *host);
void CE_BootCampaignClear(void);

#ifdef __cplusplus
}
#endif
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
#ifdef __cplusplus
extern "C" {
#endif
// Called after ordinary boot compatibility admission; legacy/manual boots return success unchanged.
int CE_LobbySignalingBootPrepared(const uint8_t compatibility[32], uint32_t level);
/* 1 copied from verified native authority; 0 keeps manual configuration; -1 refuses native fallback. */
int CE_LobbySignalingGameSecret(uint32_t role, uint32_t level, uint8_t secret[32]);
#ifdef __cplusplus
}
#endif
#endif
#endif
#endif
