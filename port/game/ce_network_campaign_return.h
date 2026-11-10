#ifndef CE_NETWORK_CAMPAIGN_RETURN_H
#define CE_NETWORK_CAMPAIGN_RETURN_H

#include "ce_network_campaign.h"

// A completed exchange may outlive the lockstep owner during process teardown.
// This context authorizes a next/retry attempt, not an automatic mission advance.
struct CeCampaignReturn {
    CeCampaignIdentity identity;
    CeCampaignOutcome outcome;
};

inline CeCampaignReturn &ce_campaign_return_storage() {
    static CeCampaignReturn value = {};
    return value;
}

inline void ce_campaign_return_clear() {
    ce_campaign_return_storage() = {};
}

inline bool ce_campaign_return_capture(const CeNetworkCampaign *campaign,
                                       bool exchange_ready, bool failed) {
    ce_campaign_return_clear();
    if (!campaign || !exchange_ready || failed)
        return false;
    if (!campaign->identity.session || !campaign->identity.attempt ||
        !campaign->identity.mission)
        return false;
    bool win = campaign->phase == CE_CAMPAIGN_AGREED_WIN &&
        campaign->host_outcome == CE_CAMPAIGN_OUTCOME_WIN &&
        campaign->guest_outcome == CE_CAMPAIGN_OUTCOME_WIN;
    bool loss = campaign->phase == CE_CAMPAIGN_AGREED_LOSS &&
        campaign->host_outcome == CE_CAMPAIGN_OUTCOME_LOSS &&
        campaign->guest_outcome == CE_CAMPAIGN_OUTCOME_LOSS;
    if (!win && !loss)
        return false;
    CeCampaignReturn &value = ce_campaign_return_storage();
    value.identity = campaign->identity;
    value.outcome = win ? CE_CAMPAIGN_OUTCOME_WIN : CE_CAMPAIGN_OUTCOME_LOSS;
    return true;
}

inline bool ce_campaign_return_get(CeCampaignReturn *out) {
    if (!out) return false;
    *out = ce_campaign_return_storage();
    return out->identity.session && out->identity.attempt && out->identity.mission &&
        (out->outcome == CE_CAMPAIGN_OUTCOME_WIN ||
         out->outcome == CE_CAMPAIGN_OUTCOME_LOSS);
}

#endif
