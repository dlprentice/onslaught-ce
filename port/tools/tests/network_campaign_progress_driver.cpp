// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_campaign.h"

#include <cstdio>
#include <cstdlib>

namespace {

unsigned checks = 0;

void check(bool ok, const char *label) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "campaign check failed: %s\n", label);
        std::exit(1);
    }
}

bool same_state(const CeNetworkCampaign &a, const CeNetworkCampaign &b) {
    return a.identity.session == b.identity.session &&
           a.identity.attempt == b.identity.attempt &&
           a.identity.mission == b.identity.mission && a.phase == b.phase &&
           a.host_outcome == b.host_outcome && a.guest_outcome == b.guest_outcome;
}

CeCampaignIdentity identity() {
    const CeCampaignIdentity value = {11, 23, 37};
    return value;
}

void begin(CeNetworkCampaign &state, CeCampaignIdentity id) {
    ce_network_campaign_init(&state);
    check(ce_network_campaign_begin(&state, id) == CE_CAMPAIGN_ACCEPTED, "begin");
    check(state.phase == CE_CAMPAIGN_COLLECTING, "collecting");
}

void basics() {
    CeNetworkCampaign s;
    CeCampaignIdentity id = identity();
    ce_network_campaign_init(0);
    check(ce_network_campaign_begin(0, id) == CE_CAMPAIGN_INVALID, "null-begin");
    check(ce_network_campaign_report(0, id, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_INVALID, "null-report");
    check(ce_network_campaign_stop(0, id, CE_CAMPAIGN_STOP_FAILURE) ==
          CE_CAMPAIGN_INVALID, "null-stop");
    check(!ce_network_campaign_agreed_win(0), "null-query");
    ce_network_campaign_init(&s);
    check(s.phase == CE_CAMPAIGN_IDLE, "initial-phase");
    check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_CLOSED, "report-before-begin");
    check(ce_network_campaign_stop(&s, id, CE_CAMPAIGN_STOP_CANCEL) ==
          CE_CAMPAIGN_CLOSED, "stop-before-begin");
    check(ce_network_campaign_begin(&s, id) == CE_CAMPAIGN_ACCEPTED, "first-begin");
    CeNetworkCampaign before = s;
    check(ce_network_campaign_begin(&s, id) == CE_CAMPAIGN_CLOSED, "second-begin");
    check(same_state(before, s), "begin-no-change");

    CeNetworkCampaign zero_identity;
    const CeCampaignIdentity zero = {0, 0, 0};
    begin(zero_identity, zero);
    check(ce_network_campaign_report(&zero_identity, zero, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "zero-identity-valid");
}

void matching_results() {
    const CeCampaignRole firsts[] = {CE_CAMPAIGN_HOST, CE_CAMPAIGN_GUEST};
    const CeCampaignOutcome outcomes[] = {CE_CAMPAIGN_OUTCOME_WIN,
                                           CE_CAMPAIGN_OUTCOME_LOSS};
    for (unsigned i = 0; i != 2; ++i) {
        for (unsigned j = 0; j != 2; ++j) {
            CeNetworkCampaign s;
            const CeCampaignIdentity id = identity();
            begin(s, id);
            CeCampaignRole first = firsts[i];
            CeCampaignRole second = first == CE_CAMPAIGN_HOST
                ? CE_CAMPAIGN_GUEST : CE_CAMPAIGN_HOST;
            CeCampaignOutcome result = outcomes[j];
            check(ce_network_campaign_report(&s, id, first, result) ==
                  CE_CAMPAIGN_ACCEPTED, "first-result");
            check(s.phase == CE_CAMPAIGN_COLLECTING &&
                  !ce_network_campaign_agreed_win(&s), "one-is-not-agreement");
            CeNetworkCampaign before = s;
            check(ce_network_campaign_report(&s, id, first, result) ==
                  CE_CAMPAIGN_DUPLICATE, "early-duplicate");
            check(same_state(before, s), "early-duplicate-no-change");
            check(ce_network_campaign_report(&s, id, second, result) ==
                  CE_CAMPAIGN_ACCEPTED, "second-result");
            check(s.phase == (result == CE_CAMPAIGN_OUTCOME_WIN
                  ? CE_CAMPAIGN_AGREED_WIN : CE_CAMPAIGN_AGREED_LOSS), "matched-phase");
            check(!!ce_network_campaign_agreed_win(&s) ==
                  (result == CE_CAMPAIGN_OUTCOME_WIN), "matched-query");
            before = s;
            check(ce_network_campaign_report(&s, id, first, result) ==
                  CE_CAMPAIGN_DUPLICATE, "late-duplicate-first");
            check(ce_network_campaign_report(&s, id, second, result) ==
                  CE_CAMPAIGN_DUPLICATE, "late-duplicate-second");
            check(same_state(before, s), "late-duplicate-no-change");
        }
    }
}

void stale_and_invalid() {
    CeNetworkCampaign s;
    const CeCampaignIdentity id = identity();
    begin(s, id);
    CeNetworkCampaign before = s;
    CeCampaignIdentity stale = id;
    ++stale.session;
    check(ce_network_campaign_report(&s, stale, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_STALE, "stale-session");
    check(ce_network_campaign_stop(&s, stale, CE_CAMPAIGN_STOP_FAILURE) ==
          CE_CAMPAIGN_STALE, "stale-stop");
    stale = id;
    ++stale.attempt;
    check(ce_network_campaign_report(&s, stale, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_STALE, "stale-attempt");
    stale = id;
    ++stale.mission;
    check(ce_network_campaign_report(&s, stale, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_STALE, "stale-mission");
    check(ce_network_campaign_report(&s, id, static_cast<CeCampaignRole>(0),
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_INVALID, "invalid-role");
    check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_UNKNOWN) == CE_CAMPAIGN_INVALID, "invalid-outcome");
    check(ce_network_campaign_stop(&s, id, static_cast<CeCampaignStop>(0)) ==
          CE_CAMPAIGN_INVALID, "invalid-stop");
    check(same_state(before, s), "stale-invalid-no-change");

    CeNetworkCampaign replacement;
    CeCampaignIdentity next = id;
    ++next.session;
    begin(replacement, next);
    before = replacement;
    check(ce_network_campaign_report(&replacement, id, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_STALE, "old-session-after-new");
    check(same_state(before, replacement), "old-session-no-change");

    check(ce_network_campaign_report(&replacement, next, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "new-session-host");
    check(ce_network_campaign_report(&replacement, next, CE_CAMPAIGN_GUEST,
          CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "new-session-guest");
    before = replacement;
    check(ce_network_campaign_stop(&replacement, id, CE_CAMPAIGN_STOP_DISCONNECT) ==
          CE_CAMPAIGN_STALE, "old-disconnect-after-agreement");
    check(same_state(before, replacement) && ce_network_campaign_agreed_win(&replacement),
          "old-disconnect-no-change");
}

void conflicting_results() {
    const CeCampaignRole firsts[] = {CE_CAMPAIGN_HOST, CE_CAMPAIGN_GUEST};
    for (unsigned i = 0; i != 2; ++i) {
        const CeCampaignIdentity id = identity();
        CeNetworkCampaign s;
        begin(s, id);
        CeCampaignRole first = firsts[i];
        CeCampaignRole other = first == CE_CAMPAIGN_HOST
            ? CE_CAMPAIGN_GUEST : CE_CAMPAIGN_HOST;
        check(ce_network_campaign_report(&s, id, first, CE_CAMPAIGN_OUTCOME_WIN) ==
              CE_CAMPAIGN_ACCEPTED, "conflict-first");
        check(ce_network_campaign_report(&s, id, other, CE_CAMPAIGN_OUTCOME_LOSS) ==
              CE_CAMPAIGN_CONFLICTING, "conflict-between-peers");
        check(s.phase == CE_CAMPAIGN_CONFLICT && !ce_network_campaign_agreed_win(&s),
              "conflict-no-win");
        CeNetworkCampaign before = s;
        check(ce_network_campaign_report(&s, id, other, CE_CAMPAIGN_OUTCOME_WIN) ==
              CE_CAMPAIGN_CLOSED, "conflict-is-terminal");
        check(ce_network_campaign_stop(&s, id, CE_CAMPAIGN_STOP_FAILURE) ==
              CE_CAMPAIGN_CLOSED, "conflict-stop-is-terminal");
        check(same_state(before, s), "conflict-terminal-no-change");

        begin(s, id);
        check(ce_network_campaign_report(&s, id, first, CE_CAMPAIGN_OUTCOME_WIN) ==
              CE_CAMPAIGN_ACCEPTED, "changed-result-first");
        check(ce_network_campaign_report(&s, id, first, CE_CAMPAIGN_OUTCOME_LOSS) ==
              CE_CAMPAIGN_CONFLICTING, "conflict-same-peer");
        check(s.phase == CE_CAMPAIGN_CONFLICT && !ce_network_campaign_agreed_win(&s),
              "changed-result-no-win");

        begin(s, id);
        check(ce_network_campaign_report(&s, id, first, CE_CAMPAIGN_OUTCOME_WIN) ==
              CE_CAMPAIGN_ACCEPTED, "confirmed-first");
        check(ce_network_campaign_report(&s, id, other, CE_CAMPAIGN_OUTCOME_WIN) ==
              CE_CAMPAIGN_ACCEPTED, "confirmed-second");
        check(ce_network_campaign_report(&s, id, first, CE_CAMPAIGN_OUTCOME_LOSS) ==
              CE_CAMPAIGN_CONFLICTING, "conflict-after-match");
        check(s.phase == CE_CAMPAIGN_CONFLICT && !ce_network_campaign_agreed_win(&s),
              "conflict-revokes-match");
    }
}

void stopped_attempts() {
    const CeCampaignStop reasons[] = {CE_CAMPAIGN_STOP_CANCEL,
                                       CE_CAMPAIGN_STOP_DISCONNECT,
                                       CE_CAMPAIGN_STOP_FAILURE};
    const CeCampaignPhase phases[] = {CE_CAMPAIGN_CANCELLED,
                                       CE_CAMPAIGN_DISCONNECTED,
                                       CE_CAMPAIGN_FAILED};
    for (unsigned i = 0; i != 3; ++i) {
        for (unsigned reports = 0; reports != 3; ++reports) {
            const CeCampaignIdentity id = identity();
            CeNetworkCampaign s;
            begin(s, id);
            if (reports >= 1) {
                check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_HOST,
                      CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "stop-host");
            }
            if (reports >= 2) {
                check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_GUEST,
                      CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "stop-guest");
                check(ce_network_campaign_agreed_win(&s), "stop-before-query");
            }
            check(ce_network_campaign_stop(&s, id, reasons[i]) == CE_CAMPAIGN_ACCEPTED,
                  "stop-accepted");
            check(s.phase == phases[i] && !ce_network_campaign_agreed_win(&s),
                  "stop-no-win");
            CeNetworkCampaign before = s;
            check(ce_network_campaign_stop(&s, id, reasons[i]) == CE_CAMPAIGN_DUPLICATE,
                  "stop-duplicate");
            check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_GUEST,
                  CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_CLOSED, "stop-no-revival");
            check(same_state(before, s), "stop-no-change");
        }
    }
}

/* The component has no persistence or transition caller. These controls cover
 * only the revocable local observation, not an external commit guard. */
void provisional_result_revocation() {
    const CeCampaignIdentity id = identity();
    const CeCampaignRole roles[] = {CE_CAMPAIGN_HOST, CE_CAMPAIGN_GUEST};
    for (unsigned i = 0; i != 2; ++i) {
        CeNetworkCampaign s;
        begin(s, id);
        check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_HOST,
              CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "provisional-host");
        check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_GUEST,
              CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "provisional-guest");
        check(ce_network_campaign_agreed_win(&s), "provisional-before-conflict");

        CeCampaignIdentity old = id;
        ++old.attempt;
        CeNetworkCampaign before = s;
        check(ce_network_campaign_report(&s, old, roles[i], CE_CAMPAIGN_OUTCOME_LOSS) ==
              CE_CAMPAIGN_STALE, "old-conflict-ignored");
        check(same_state(before, s) && ce_network_campaign_agreed_win(&s),
              "old-conflict-no-change");
        check(ce_network_campaign_report(&s, id, roles[i], CE_CAMPAIGN_OUTCOME_LOSS) ==
              CE_CAMPAIGN_CONFLICTING, "provisional-contradiction");
        check(s.phase == CE_CAMPAIGN_CONFLICT && !ce_network_campaign_agreed_win(&s),
              "provisional-conflict-revokes");
        check(ce_network_campaign_report(&s, id, roles[i], CE_CAMPAIGN_OUTCOME_WIN) ==
              CE_CAMPAIGN_CLOSED, "provisional-conflict-no-revival");
        check(!ce_network_campaign_agreed_win(&s), "conflict-still-not-agreed");
    }

    const CeCampaignStop reasons[] = {CE_CAMPAIGN_STOP_CANCEL,
                                       CE_CAMPAIGN_STOP_DISCONNECT,
                                       CE_CAMPAIGN_STOP_FAILURE};
    const CeCampaignPhase phases[] = {CE_CAMPAIGN_CANCELLED,
                                       CE_CAMPAIGN_DISCONNECTED,
                                       CE_CAMPAIGN_FAILED};
    for (unsigned i = 0; i != 3; ++i) {
        CeNetworkCampaign s;
        begin(s, id);
        check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_GUEST,
              CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "stop-provisional-guest");
        check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_HOST,
              CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_ACCEPTED, "stop-provisional-host");
        check(ce_network_campaign_agreed_win(&s), "provisional-before-stop");
        check(ce_network_campaign_stop(&s, id, reasons[i]) == CE_CAMPAIGN_ACCEPTED,
              "provisional-stop");
        check(s.phase == phases[i] && !ce_network_campaign_agreed_win(&s),
              "provisional-stop-revokes");
        CeNetworkCampaign before = s;
        check(ce_network_campaign_stop(&s, id, reasons[i]) == CE_CAMPAIGN_DUPLICATE,
              "provisional-stop-idempotent");
        check(ce_network_campaign_report(&s, id, CE_CAMPAIGN_HOST,
              CE_CAMPAIGN_OUTCOME_WIN) == CE_CAMPAIGN_CLOSED, "provisional-stop-no-revival");
        check(same_state(before, s) && !ce_network_campaign_agreed_win(&s),
              "provisional-stays-revoked");
    }
}

}  // namespace

int main() {
    basics();
    matching_results();
    stale_and_invalid();
    conflicting_results();
    stopped_attempts();
    provisional_result_revocation();
    std::printf("campaign component: %u synthetic checks passed\n", checks);
    return 0;
}
