// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_NETWORK_CAMPAIGN_H
#define CE_NETWORK_CAMPAIGN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A single, in-memory result agreement. This component does not run missions,
 * send messages, authenticate peers, write careers or advance a campaign.
 * The caller must serialize access, authenticate roles, check compatibility and
 * choose identities that are never reused, including after reinitialization.
 */
typedef struct CeCampaignIdentity {
    uint64_t session;
    uint64_t attempt;
    uint32_t mission;
} CeCampaignIdentity;

typedef enum CeCampaignRole {
    CE_CAMPAIGN_HOST = 1,
    CE_CAMPAIGN_GUEST = 2
} CeCampaignRole;

typedef enum CeCampaignOutcome {
    CE_CAMPAIGN_OUTCOME_UNKNOWN = 0,
    CE_CAMPAIGN_OUTCOME_WIN = 1,
    CE_CAMPAIGN_OUTCOME_LOSS = 2
} CeCampaignOutcome;

typedef enum CeCampaignPhase {
    CE_CAMPAIGN_IDLE = 0,
    CE_CAMPAIGN_COLLECTING,
    CE_CAMPAIGN_AGREED_WIN,
    CE_CAMPAIGN_AGREED_LOSS,
    CE_CAMPAIGN_CONFLICT,
    CE_CAMPAIGN_CANCELLED,
    CE_CAMPAIGN_DISCONNECTED,
    CE_CAMPAIGN_FAILED
} CeCampaignPhase;

typedef enum CeCampaignAction {
    CE_CAMPAIGN_ACCEPTED = 0,
    CE_CAMPAIGN_DUPLICATE,
    CE_CAMPAIGN_STALE,
    CE_CAMPAIGN_INVALID,
    CE_CAMPAIGN_CLOSED,
    CE_CAMPAIGN_CONFLICTING
} CeCampaignAction;

typedef enum CeCampaignStop {
    CE_CAMPAIGN_STOP_CANCEL = 1,
    CE_CAMPAIGN_STOP_DISCONNECT = 2,
    CE_CAMPAIGN_STOP_FAILURE = 3
} CeCampaignStop;

typedef struct CeNetworkCampaign {
    CeCampaignIdentity identity;
    CeCampaignPhase phase;
    CeCampaignOutcome host_outcome;
    CeCampaignOutcome guest_outcome;
} CeNetworkCampaign;

/* Initialize a fresh object before beginning one attempt. Reinitialization does
 * not retain history; it must not be used to accept a reused identity. */
void ce_network_campaign_init(CeNetworkCampaign *state);
CeCampaignAction ce_network_campaign_begin(CeNetworkCampaign *state,
                                            CeCampaignIdentity identity);
CeCampaignAction ce_network_campaign_report(CeNetworkCampaign *state,
                                             CeCampaignIdentity identity,
                                             CeCampaignRole role,
                                             CeCampaignOutcome outcome);
CeCampaignAction ce_network_campaign_stop(CeNetworkCampaign *state,
                                           CeCampaignIdentity identity,
                                           CeCampaignStop reason);

/*
 * This is a provisional observation of two matching local reports. A later
 * conflicting report, cancellation, disconnect or failure can make it false.
 * It is not shared or durable finality and gives no permission to persist
 * progress, unlock content or transition to another mission. A future caller
 * must establish and test a separate, correctly ordered finality/commit rule.
 */
int ce_network_campaign_agreed_win(const CeNetworkCampaign *state);

#ifdef __cplusplus
}
#endif

#endif
