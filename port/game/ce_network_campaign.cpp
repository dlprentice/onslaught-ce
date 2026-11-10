// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_campaign.h"

namespace {

bool same_identity(CeCampaignIdentity a, CeCampaignIdentity b) {
    return a.session == b.session && a.attempt == b.attempt &&
           a.mission == b.mission;
}

bool valid_role(CeCampaignRole role) {
    return role == CE_CAMPAIGN_HOST || role == CE_CAMPAIGN_GUEST;
}

bool valid_outcome(CeCampaignOutcome outcome) {
    return outcome == CE_CAMPAIGN_OUTCOME_WIN ||
           outcome == CE_CAMPAIGN_OUTCOME_LOSS;
}

bool stopped(CeCampaignPhase phase) {
    return phase == CE_CAMPAIGN_CANCELLED ||
           phase == CE_CAMPAIGN_DISCONNECTED || phase == CE_CAMPAIGN_FAILED;
}

CeCampaignPhase stop_phase(CeCampaignStop reason) {
    switch (reason) {
    case CE_CAMPAIGN_STOP_CANCEL:
        return CE_CAMPAIGN_CANCELLED;
    case CE_CAMPAIGN_STOP_DISCONNECT:
        return CE_CAMPAIGN_DISCONNECTED;
    case CE_CAMPAIGN_STOP_FAILURE:
        return CE_CAMPAIGN_FAILED;
    }
    return CE_CAMPAIGN_IDLE;
}

}  // namespace

extern "C" {

void ce_network_campaign_init(CeNetworkCampaign *state) {
    if (state != 0) {
        *state = CeNetworkCampaign{};
    }
}

CeCampaignAction ce_network_campaign_begin(CeNetworkCampaign *state,
                                            CeCampaignIdentity identity) {
    if (state == 0) return CE_CAMPAIGN_INVALID;
    if (state->phase != CE_CAMPAIGN_IDLE) return CE_CAMPAIGN_CLOSED;
    state->identity = identity;
    state->host_outcome = CE_CAMPAIGN_OUTCOME_UNKNOWN;
    state->guest_outcome = CE_CAMPAIGN_OUTCOME_UNKNOWN;
    state->phase = CE_CAMPAIGN_COLLECTING;
    return CE_CAMPAIGN_ACCEPTED;
}

CeCampaignAction ce_network_campaign_report(CeNetworkCampaign *state,
                                             CeCampaignIdentity identity,
                                             CeCampaignRole role,
                                             CeCampaignOutcome outcome) {
    if (state == 0) return CE_CAMPAIGN_INVALID;
    if (state->phase == CE_CAMPAIGN_IDLE) return CE_CAMPAIGN_CLOSED;
    if (!same_identity(state->identity, identity)) return CE_CAMPAIGN_STALE;
    if (!valid_role(role) || !valid_outcome(outcome)) return CE_CAMPAIGN_INVALID;
    if (state->phase == CE_CAMPAIGN_CONFLICT || stopped(state->phase)) {
        return CE_CAMPAIGN_CLOSED;
    }

    CeCampaignOutcome *mine = role == CE_CAMPAIGN_HOST
        ? &state->host_outcome : &state->guest_outcome;
    CeCampaignOutcome other = role == CE_CAMPAIGN_HOST
        ? state->guest_outcome : state->host_outcome;
    if (*mine == outcome) return CE_CAMPAIGN_DUPLICATE;
    if (*mine != CE_CAMPAIGN_OUTCOME_UNKNOWN) {
        state->phase = CE_CAMPAIGN_CONFLICT;
        return CE_CAMPAIGN_CONFLICTING;
    }
    if (state->phase != CE_CAMPAIGN_COLLECTING) return CE_CAMPAIGN_CLOSED;

    *mine = outcome;
    if (other == CE_CAMPAIGN_OUTCOME_UNKNOWN) return CE_CAMPAIGN_ACCEPTED;
    if (other != outcome) {
        state->phase = CE_CAMPAIGN_CONFLICT;
        return CE_CAMPAIGN_CONFLICTING;
    }
    state->phase = outcome == CE_CAMPAIGN_OUTCOME_WIN
        ? CE_CAMPAIGN_AGREED_WIN : CE_CAMPAIGN_AGREED_LOSS;
    return CE_CAMPAIGN_ACCEPTED;
}

CeCampaignAction ce_network_campaign_stop(CeNetworkCampaign *state,
                                           CeCampaignIdentity identity,
                                           CeCampaignStop reason) {
    if (state == 0) return CE_CAMPAIGN_INVALID;
    if (state->phase == CE_CAMPAIGN_IDLE) return CE_CAMPAIGN_CLOSED;
    if (!same_identity(state->identity, identity)) return CE_CAMPAIGN_STALE;
    CeCampaignPhase target = stop_phase(reason);
    if (target == CE_CAMPAIGN_IDLE) return CE_CAMPAIGN_INVALID;
    if (state->phase == target) return CE_CAMPAIGN_DUPLICATE;
    if (state->phase == CE_CAMPAIGN_CONFLICT || stopped(state->phase)) {
        return CE_CAMPAIGN_CLOSED;
    }
    state->phase = target;
    return CE_CAMPAIGN_ACCEPTED;
}

int ce_network_campaign_agreed_win(const CeNetworkCampaign *state) {
    return state != 0 && state->phase == CE_CAMPAIGN_AGREED_WIN;
}

}  // extern "C"
