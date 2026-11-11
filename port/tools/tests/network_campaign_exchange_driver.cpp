// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_result_exchange.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

unsigned checks = 0;

void check(bool ok, const char *label) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "exchange check failed: %s\n", label);
        std::exit(1);
    }
}

CeCampaignIdentity identity() {
    const CeCampaignIdentity id = {(UINT64_C(1) << 60) + 12345,
                                    (UINT64_C(1) << 63) + 67890,
                                    (UINT32_C(1) << 28) + 2468};
    return id;
}

bool equal_identity(CeCampaignIdentity a, CeCampaignIdentity b) {
    return a.session == b.session && a.attempt == b.attempt && a.mission == b.mission;
}

bool equal_message(const CeResultMessage &a, const CeResultMessage &b) {
    return equal_identity(a.identity, b.identity) && a.outcome == b.outcome &&
           a.terminal_tick == b.terminal_tick &&
           a.terminal_fingerprint == b.terminal_fingerprint &&
           a.kind == b.kind && a.sender == b.sender;
}

bool equal_exchange(const CeResultExchange &a, const CeResultExchange &b) {
    return equal_identity(a.identity, b.identity) && a.local_outcome == b.local_outcome &&
           a.terminal_tick == b.terminal_tick &&
           a.terminal_fingerprint == b.terminal_fingerprint &&
           a.local_role == b.local_role && a.phase == b.phase &&
           a.conflict_causes == b.conflict_causes &&
           equal_identity(a.observed.identity, b.observed.identity) &&
           a.observed.phase == b.observed.phase &&
           a.observed.host_outcome == b.observed.host_outcome &&
           a.observed.guest_outcome == b.observed.guest_outcome;
}

void start(CeResultExchange &state, CeCampaignRole role, CeCampaignOutcome outcome,
           CeCampaignIdentity id, uint64_t tick, uint64_t fingerprint) {
    ce_result_exchange_init(&state);
    check(ce_result_exchange_begin(&state, id, role, outcome, tick, fingerprint) ==
          CE_RESULT_ACCEPTED, "begin");
}

CeResultMessage pending(const CeResultExchange &state) {
    CeResultMessage message = {};
    check(ce_result_exchange_pending(&state, &message) == 1, "pending");
    return message;
}

CeResultMessage round_trip(const CeResultMessage &message) {
    uint8_t bytes[128] = {};
    size_t used = 0;
    check(ce_result_message_size() <= sizeof(bytes), "size-bounded");
    check(ce_result_message_encode(&message, bytes, sizeof(bytes), &used) == 1,
          "encode");
    check(used == ce_result_message_size(), "encoded-size");
    CeResultMessage decoded = {};
    check(ce_result_message_decode(&decoded, bytes, used) == 1, "decode");
    check(equal_message(message, decoded), "round-trip");
    return decoded;
}

CeResultMessage codec_sample() {
    CeResultMessage original = {};
    original.identity = identity();
    original.outcome = CE_CAMPAIGN_OUTCOME_WIN;
    original.terminal_tick = (UINT64_C(1) << 62) + 12345;
    original.terminal_fingerprint = (UINT64_C(1) << 63) + 67890;
    original.kind = CE_RESULT_OFFER;
    original.sender = CE_CAMPAIGN_HOST;
    return original;
}

void codec_controls() {
    CeResultMessage original = codec_sample();
    round_trip(original);

    uint8_t bytes[128];
    for (unsigned i = 0; i != sizeof(bytes); ++i) bytes[i] = 90;
    size_t used = 7;
    check(!ce_result_message_encode(&original, bytes, ce_result_message_size() - 1, &used)
          && used == 0, "short-encode");
    for (unsigned i = 0; i != sizeof(bytes); ++i) check(bytes[i] == 90, "short-no-write");
    check(!ce_result_message_encode(0, bytes, sizeof(bytes), &used), "null-encode-message");
    check(!ce_result_message_encode(&original, 0, sizeof(bytes), &used), "null-encode-buffer");
    check(!ce_result_message_encode(&original, bytes, sizeof(bytes), 0), "null-encode-size");
    check(ce_result_message_encode(&original, bytes, sizeof(bytes), &used), "encode-fixture");

    CeResultMessage out = original;
    for (size_t i = 0; i != used; ++i) {
        check(!ce_result_message_decode(&out, bytes, i), "truncated-rejected");
        check(equal_message(out, original), "truncated-no-output");
    }
    check(!ce_result_message_decode(&out, bytes, used + 1), "extra-byte-rejected");
    check(equal_message(out, original), "extra-no-output");
    check(!ce_result_message_decode(0, bytes, used), "null-decode-output");
    check(!ce_result_message_decode(&out, 0, used), "null-decode-input");

    for (unsigned index = 0; index != 4; ++index) {
        const uint8_t saved = bytes[index];
        bytes[index] = 0;
        check(!ce_result_message_decode(&out, bytes, used), "invalid-header");
        check(equal_message(out, original), "invalid-header-no-output");
        bytes[index] = saved;
    }
    for (unsigned index = 1; index != 4; ++index) {
        const uint8_t saved = bytes[index];
        bytes[index] = 255;
        check(!ce_result_message_decode(&out, bytes, used), "large-enum-rejected");
        check(equal_message(out, original), "large-enum-no-output");
        bytes[index] = saved;
    }
    const uint8_t saved_version = bytes[0];
    bytes[0] = 255;
    check(!ce_result_message_decode(&out, bytes, used), "unknown-version-rejected");
    check(equal_message(out, original), "unknown-version-no-output");
    bytes[0] = saved_version;
    const uint8_t saved_role = bytes[2];
    bytes[2] = CE_CAMPAIGN_GUEST;
    check(!ce_result_message_decode(&out, bytes, used), "wrong-role-decode");
    check(equal_message(out, original), "wrong-role-decode-no-output");
    bytes[2] = saved_role;
    CeResultMessage wrong_role = original;
    wrong_role.sender = CE_CAMPAIGN_GUEST;
    used = 7;
    check(!ce_result_message_encode(&wrong_role, bytes, sizeof(bytes), &used) && used == 0,
          "wrong-role-encode");
}

struct Pair {
    CeResultExchange host;
    CeResultExchange guest;
};

void start_pair(Pair &pair, CeCampaignOutcome outcome) {
    CeCampaignIdentity id = identity();
    start(pair.host, CE_CAMPAIGN_HOST, outcome, id, 71, 83);
    start(pair.guest, CE_CAMPAIGN_GUEST, outcome, id, 71, 83);
}

/* Fake delivery copies a locally encoded message. No socket or game is used. */
void deliver(CeResultExchange &sender, CeResultExchange &receiver,
             bool drop_once, bool duplicate) {
    CeResultMessage sent = pending(sender);
    CeResultMessage wire_copy = round_trip(sent);
    if (drop_once) {
        CeResultExchange sender_before = sender;
        CeResultExchange receiver_before = receiver;
        CeResultMessage retry = pending(sender);
        check(equal_message(sent, retry), "drop-retry-identical");
        check(equal_exchange(sender_before, sender) && equal_exchange(receiver_before, receiver),
              "drop-no-state-change");
    }
    check(ce_result_exchange_receive(&receiver, &wire_copy) == CE_RESULT_ACCEPTED,
          "deliver-accepted");
    if (duplicate) {
        CeResultExchange before = receiver;
        check(ce_result_exchange_receive(&receiver, &wire_copy) == CE_RESULT_DUPLICATE,
              "duplicate-delivery");
        check(equal_exchange(before, receiver), "duplicate-no-change");
    }
}

void complete_exchange(Pair &pair, unsigned through, bool drop, bool duplicate) {
    if (through >= 1) deliver(pair.host, pair.guest, drop, duplicate);
    if (through >= 2) deliver(pair.guest, pair.host, drop, duplicate);
    if (through >= 3) deliver(pair.host, pair.guest, drop, duplicate);
    if (through >= 4) deliver(pair.guest, pair.host, drop, duplicate);
}

void happy_and_retries() {
    const CeCampaignOutcome outcomes[] = {CE_CAMPAIGN_OUTCOME_WIN,
                                           CE_CAMPAIGN_OUTCOME_LOSS};
    for (unsigned i = 0; i != 2; ++i) {
        Pair pair;
        start_pair(pair, outcomes[i]);
        CeResultMessage unused = {};
        check(!ce_result_exchange_pending(&pair.guest, &unused), "guest-waits");
        complete_exchange(pair, 4, true, true);
        check(pair.host.phase == CE_RESULT_HOST_RECEIPTED, "host-saw-receipt");
        check(pair.guest.phase == CE_RESULT_GUEST_DECIDED, "guest-saw-decision");
        check(!ce_result_exchange_pending(&pair.host, &unused), "host-no-pending");
        check(ce_result_exchange_pending(&pair.guest, &unused), "guest-cannot-know-receipt");
        check(pair.host.observed.phase == (outcomes[i] == CE_CAMPAIGN_OUTCOME_WIN
              ? CE_CAMPAIGN_AGREED_WIN : CE_CAMPAIGN_AGREED_LOSS), "host-observation");
        check(pair.guest.observed.phase == pair.host.observed.phase, "guest-observation");
    }
}

/* These observations are intentionally asymmetric. None is a commit or
 * permission to persist a result or transition the game. */
void asymmetric_observations() {
    Pair pair;
    start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
    deliver(pair.host, pair.guest, false, false);
    check(pair.host.phase == CE_RESULT_HOST_OFFERING &&
          pair.guest.phase == CE_RESULT_GUEST_PREPARED, "accept-not-yet-delivered");
    check(!ce_network_campaign_agreed_win(&pair.host.observed) &&
          ce_network_campaign_agreed_win(&pair.guest.observed), "only-guest-compared");
    deliver(pair.guest, pair.host, false, false);
    check(pair.host.phase == CE_RESULT_HOST_DECIDED &&
          pair.guest.phase == CE_RESULT_GUEST_PREPARED, "decision-not-yet-delivered");
    deliver(pair.host, pair.guest, false, false);
    check(pair.host.phase == CE_RESULT_HOST_DECIDED &&
          pair.guest.phase == CE_RESULT_GUEST_DECIDED, "receipt-not-yet-delivered");
    deliver(pair.guest, pair.host, false, false);
    CeResultMessage receipt = {};
    check(pair.host.phase == CE_RESULT_HOST_RECEIPTED &&
          pair.guest.phase == CE_RESULT_GUEST_DECIDED &&
          ce_result_exchange_pending(&pair.guest, &receipt), "guest-cannot-observe-receipt");
}

void ordering_and_identity() {
    Pair pair;
    start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
    CeResultMessage offer = pending(pair.host);
    CeResultMessage premature_decision = offer;
    premature_decision.kind = CE_RESULT_DECISION;
    CeResultExchange before = pair.guest;
    check(ce_result_exchange_receive(&pair.guest, &premature_decision) ==
          CE_RESULT_OUT_OF_ORDER, "decision-before-offer");
    check(equal_exchange(before, pair.guest), "early-decision-no-change");
    CeResultMessage premature_receipt = offer;
    premature_receipt.kind = CE_RESULT_RECEIPT;
    premature_receipt.sender = CE_CAMPAIGN_GUEST;
    before = pair.host;
    check(ce_result_exchange_receive(&pair.host, &premature_receipt) ==
          CE_RESULT_OUT_OF_ORDER, "receipt-before-accept");
    check(equal_exchange(before, pair.host), "early-receipt-no-change");
    check(ce_result_exchange_receive(&pair.host, &offer) == CE_RESULT_WRONG_PEER,
          "message-from-own-role");
    check(equal_exchange(before, pair.host), "wrong-peer-no-change");

    before = pair.guest;
    CeResultMessage altered = offer;
    altered.sender = CE_CAMPAIGN_GUEST;
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_INVALID,
          "role-kind-mismatch");
    altered = offer;
    altered.kind = static_cast<CeResultMessageKind>(0);
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_INVALID,
          "invalid-kind");
    altered = offer;
    altered.outcome = CE_CAMPAIGN_OUTCOME_UNKNOWN;
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_INVALID,
          "invalid-outcome");
    check(ce_result_exchange_receive(&pair.guest, 0) == CE_RESULT_INVALID, "null-message");
    altered = offer;
    altered.kind = 255;
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_INVALID,
          "large-kind");
    altered = offer;
    altered.sender = 255;
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_INVALID,
          "large-role");
    altered = offer;
    altered.outcome = 255;
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_INVALID,
          "large-outcome");
    check(equal_exchange(before, pair.guest), "invalid-no-change");

    altered = offer;
    ++altered.identity.session;
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_STALE,
          "stale-session");
    altered = offer;
    ++altered.identity.attempt;
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_STALE,
          "stale-attempt");
    altered = offer;
    ++altered.identity.mission;
    check(ce_result_exchange_receive(&pair.guest, &altered) == CE_RESULT_STALE,
          "stale-mission");
    check(equal_exchange(before, pair.guest), "stale-no-change");
}

void delayed_messages() {
    Pair pair;
    start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
    CeResultMessage offer = pending(pair.host);
    check(ce_result_exchange_receive(&pair.guest, &offer) == CE_RESULT_ACCEPTED,
          "delay-first-offer");
    CeResultMessage accept = pending(pair.guest);
    check(ce_result_exchange_receive(&pair.host, &accept) == CE_RESULT_ACCEPTED,
          "delay-first-accept");
    CeResultMessage decision = pending(pair.host);
    check(ce_result_exchange_receive(&pair.guest, &decision) == CE_RESULT_ACCEPTED,
          "delay-first-decision");
    CeResultMessage receipt = pending(pair.guest);
    CeResultExchange before = pair.guest;
    check(ce_result_exchange_receive(&pair.guest, &offer) == CE_RESULT_DUPLICATE,
          "late-offer-duplicate");
    check(equal_exchange(before, pair.guest) && equal_message(receipt, pending(pair.guest)),
          "late-offer-keeps-receipt");
    check(ce_result_exchange_receive(&pair.host, &receipt) == CE_RESULT_ACCEPTED,
          "delay-first-receipt");
    before = pair.host;
    check(ce_result_exchange_receive(&pair.host, &accept) == CE_RESULT_DUPLICATE,
          "late-accept-duplicate");
    check(equal_exchange(before, pair.host), "late-accept-no-change");
    CeResultMessage none = {};
    check(!ce_result_exchange_pending(&pair.host, &none), "late-accept-no-restart");

    offer.outcome = CE_CAMPAIGN_OUTCOME_LOSS;
    check(ce_result_exchange_receive(&pair.guest, &offer) == CE_RESULT_CONFLICTING,
          "late-contradiction-rejected");
    check(pair.guest.phase == CE_RESULT_REJECTED &&
          !ce_network_campaign_agreed_win(&pair.guest.observed), "late-conflict-revokes");
}

void mismatches() {
    const uint8_t causes[] = {CE_RESULT_CONFLICT_OUTCOME, CE_RESULT_CONFLICT_TICK,
                              CE_RESULT_CONFLICT_FINGERPRINT};
    for (unsigned which = 0; which != 3; ++which) {
        Pair pair;
        start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
        CeResultMessage offer = pending(pair.host);
        if (which == 0) offer.outcome = CE_CAMPAIGN_OUTCOME_LOSS;
        if (which == 1) ++offer.terminal_tick;
        if (which == 2) ++offer.terminal_fingerprint;
        check(ce_result_exchange_receive(&pair.guest, &offer) == CE_RESULT_CONFLICTING,
              "result-mismatch");
        check(pair.guest.phase == CE_RESULT_REJECTED &&
              !ce_network_campaign_agreed_win(&pair.guest.observed), "mismatch-no-agreement");
        check(pair.guest.conflict_causes == causes[which], "mismatch-specific-cause");
        CeResultMessage ignored = {};
        check(!ce_result_exchange_pending(&pair.guest, &ignored), "mismatch-no-send");
    }

    Pair pair;
    start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
    deliver(pair.host, pair.guest, false, false);
    CeResultMessage accept = pending(pair.guest);
    ++accept.terminal_fingerprint;
    check(ce_result_exchange_receive(&pair.host, &accept) == CE_RESULT_CONFLICTING,
          "host-rejects-mismatch");
    check(pair.host.phase == CE_RESULT_REJECTED &&
          !ce_network_campaign_agreed_win(&pair.host.observed), "host-mismatch-no-agreement");

    start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
    CeResultMessage offer = pending(pair.host);
    check(ce_result_exchange_receive(&pair.guest, &offer) == CE_RESULT_ACCEPTED,
          "conflicting-retry-first");
    offer.outcome = CE_CAMPAIGN_OUTCOME_LOSS;
    check(ce_result_exchange_receive(&pair.guest, &offer) == CE_RESULT_CONFLICTING,
          "conflicting-retry-rejected");
    check(pair.guest.phase == CE_RESULT_REJECTED &&
          !ce_network_campaign_agreed_win(&pair.guest.observed), "retry-revokes");
}

void disconnect_boundaries() {
    for (unsigned step = 0; step != 5; ++step) {
        Pair pair;
        start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
        complete_exchange(pair, step, false, false);
        CeCampaignIdentity old = identity();
        ++old.session;
        CeResultExchange before = pair.host;
        check(ce_result_exchange_disconnect(&pair.host, old) == CE_RESULT_STALE,
              "old-disconnect");
        check(equal_exchange(before, pair.host), "old-disconnect-no-change");
        check(ce_result_exchange_disconnect(&pair.host, identity()) == CE_RESULT_ACCEPTED,
              "host-disconnect");
        check(ce_result_exchange_disconnect(&pair.guest, identity()) == CE_RESULT_ACCEPTED,
              "guest-disconnect");
        check(pair.host.phase == CE_RESULT_INDETERMINATE &&
              pair.guest.phase == CE_RESULT_INDETERMINATE, "partition-is-indeterminate");
        check(pair.host.conflict_causes == CE_RESULT_CONFLICT_NONE &&
              pair.guest.conflict_causes == CE_RESULT_CONFLICT_NONE, "disconnect-no-conflict");
        check(!ce_network_campaign_agreed_win(&pair.host.observed) &&
              !ce_network_campaign_agreed_win(&pair.guest.observed), "disconnect-revokes");
        CeResultMessage ignored = {};
        check(!ce_result_exchange_pending(&pair.host, &ignored) &&
              !ce_result_exchange_pending(&pair.guest, &ignored), "disconnect-no-send");
        before = pair.host;
        check(ce_result_exchange_disconnect(&pair.host, identity()) == CE_RESULT_DUPLICATE,
              "disconnect-idempotent");
        check(equal_exchange(before, pair.host), "duplicate-disconnect-no-change");
    }

    Pair pair;
    start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
    complete_exchange(pair, 3, false, false);
    CeResultMessage receipt = pending(pair.guest);
    check(pair.host.phase == CE_RESULT_HOST_DECIDED &&
          pair.guest.phase == CE_RESULT_GUEST_DECIDED, "receipt-loss-ambiguous");
    check(ce_result_exchange_disconnect(&pair.host, identity()) == CE_RESULT_ACCEPTED,
          "lost-receipt-disconnect");
    check(ce_result_exchange_receive(&pair.host, &receipt) == CE_RESULT_CLOSED,
          "late-receipt-closed");
    check(pair.host.phase == CE_RESULT_INDETERMINATE, "late-receipt-uncertain");
}

void conflict_then_disconnect() {
    Pair pair;
    start_pair(pair, CE_CAMPAIGN_OUTCOME_WIN);
    CeResultMessage bad = pending(pair.host);
    bad.outcome = CE_CAMPAIGN_OUTCOME_LOSS;
    ++bad.terminal_tick;
    ++bad.terminal_fingerprint;
    const uint8_t all_causes = CE_RESULT_CONFLICT_OUTCOME | CE_RESULT_CONFLICT_TICK |
                               CE_RESULT_CONFLICT_FINGERPRINT;
    check(ce_result_exchange_receive(&pair.guest, &bad) == CE_RESULT_CONFLICTING,
          "combined-mismatch");
    check(pair.guest.phase == CE_RESULT_REJECTED &&
          pair.guest.conflict_causes == all_causes, "combined-mismatch-causes");
    CeResultExchange before = pair.guest;
    CeCampaignIdentity stale = identity();
    ++stale.attempt;
    check(ce_result_exchange_disconnect(&pair.guest, stale) == CE_RESULT_STALE,
          "conflict-stale-disconnect");
    check(equal_exchange(before, pair.guest), "conflict-stale-no-change");
    check(ce_result_exchange_disconnect(&pair.guest, identity()) == CE_RESULT_ACCEPTED,
          "conflict-disconnect");
    check(pair.guest.phase == CE_RESULT_INDETERMINATE &&
          pair.guest.conflict_causes == all_causes &&
          !ce_network_campaign_agreed_win(&pair.guest.observed), "conflict-cause-retained");
    CeResultMessage out = {};
    check(!ce_result_exchange_pending(&pair.guest, &out), "conflict-disconnect-no-message");
    before = pair.guest;
    check(ce_result_exchange_disconnect(&pair.guest, identity()) == CE_RESULT_DUPLICATE,
          "conflict-disconnect-duplicate");
    check(ce_result_exchange_receive(&pair.guest, &bad) == CE_RESULT_CLOSED,
          "conflict-late-message-closed");
    check(equal_exchange(before, pair.guest) && pair.guest.conflict_causes == all_causes,
          "conflict-cause-stays-retained");

    ce_result_exchange_init(&pair.guest);
    check(pair.guest.phase == CE_RESULT_IDLE && pair.guest.observed.phase == CE_CAMPAIGN_IDLE &&
          pair.guest.identity.session == 0 && pair.guest.identity.attempt == 0 &&
          pair.guest.identity.mission == 0 && pair.guest.terminal_tick == 0 &&
          pair.guest.terminal_fingerprint == 0 &&
          pair.guest.local_outcome == CE_CAMPAIGN_OUTCOME_UNKNOWN,
          "reuse-other-fields-reset");
    check(pair.guest.conflict_causes == CE_RESULT_CONFLICT_NONE, "reuse-conflict-cleared");
    CeCampaignIdentity fresh = identity();
    ++fresh.attempt;
    check(ce_result_exchange_begin(&pair.guest, fresh, CE_CAMPAIGN_GUEST,
          CE_CAMPAIGN_OUTCOME_WIN, 71, 83) == CE_RESULT_ACCEPTED, "reuse-begin");
    check(pair.guest.phase == CE_RESULT_GUEST_WAITING &&
          pair.guest.conflict_causes == CE_RESULT_CONFLICT_NONE &&
          !ce_network_campaign_agreed_win(&pair.guest.observed), "reuse-attempt-clean");
    before = pair.guest;
    check(ce_result_exchange_receive(&pair.guest, &bad) == CE_RESULT_STALE,
          "reuse-old-message-stale");
    check(equal_exchange(before, pair.guest), "reuse-old-message-no-change");
}

void invalid_lifecycle() {
    CeResultExchange state;
    CeResultMessage m = {};
    CeCampaignIdentity id = identity();
    ce_result_exchange_init(0);
    ce_result_exchange_init(&state);
    check(ce_result_exchange_begin(0, id, CE_CAMPAIGN_HOST, CE_CAMPAIGN_OUTCOME_WIN,
          0, 0) == CE_RESULT_INVALID, "null-begin");
    check(ce_result_exchange_receive(0, &m) == CE_RESULT_INVALID, "null-receive-state");
    check(ce_result_exchange_disconnect(0, id) == CE_RESULT_INVALID, "null-disconnect");
    check(!ce_result_exchange_pending(0, &m), "null-pending-state");
    check(!ce_result_exchange_pending(&state, 0), "null-pending-output");
    check(ce_result_exchange_disconnect(&state, id) == CE_RESULT_CLOSED, "idle-disconnect");
    check(ce_result_exchange_begin(&state, id, static_cast<CeCampaignRole>(0),
          CE_CAMPAIGN_OUTCOME_WIN, 0, 0) == CE_RESULT_INVALID, "invalid-begin-role");
    check(ce_result_exchange_begin(&state, id, CE_CAMPAIGN_HOST,
          CE_CAMPAIGN_OUTCOME_UNKNOWN, 0, 0) == CE_RESULT_INVALID, "invalid-begin-outcome");
    check(state.phase == CE_RESULT_IDLE, "invalid-begin-no-change");
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--codec-fixture") == 0) {
        const CeResultMessage message = codec_sample();
        uint8_t bytes[128] = {};
        size_t used = 0;
        if (!ce_result_message_encode(&message, bytes, sizeof(bytes), &used)) return 1;
        return std::fwrite(bytes, 1, used, stdout) == used ? 0 : 1;
    }
    if (argc != 1) return 1;
    invalid_lifecycle();
    codec_controls();
    happy_and_retries();
    asymmetric_observations();
    ordering_and_identity();
    delayed_messages();
    mismatches();
    disconnect_boundaries();
    conflict_then_disconnect();
    std::printf("exchange prototype: %u synthetic checks passed\n", checks);
    return 0;
}
