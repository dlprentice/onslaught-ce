// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_result_exchange.h"

namespace {

const size_t kEncodedSize = 40;
const uint8_t kPrototypeVersion = 1;

bool same_identity(CeCampaignIdentity a, CeCampaignIdentity b) {
    return a.session == b.session && a.attempt == b.attempt &&
           a.mission == b.mission;
}

bool valid_role(CeCampaignRole role) {
    return role == CE_CAMPAIGN_HOST || role == CE_CAMPAIGN_GUEST;
}

bool valid_outcome(CeCampaignOutcome outcome) {
    return outcome == CE_CAMPAIGN_OUTCOME_WIN || outcome == CE_CAMPAIGN_OUTCOME_LOSS;
}

bool valid_message(const CeResultMessage &message) {
    if (message.outcome != CE_CAMPAIGN_OUTCOME_WIN &&
        message.outcome != CE_CAMPAIGN_OUTCOME_LOSS) return false;
    switch (message.kind) {
    case CE_RESULT_OFFER:
    case CE_RESULT_DECISION:
        return message.sender == CE_CAMPAIGN_HOST;
    case CE_RESULT_ACCEPT:
    case CE_RESULT_RECEIPT:
        return message.sender == CE_CAMPAIGN_GUEST;
    }
    return false;
}

void put_uint(uint8_t *out, uint64_t value, unsigned count) {
    for (unsigned i = 0; i != count; ++i) {
        out[i] = static_cast<uint8_t>(value >> (8u * (count - 1u - i)));
    }
}

uint64_t get_uint(const uint8_t *data, unsigned count) {
    uint64_t value = 0;
    for (unsigned i = 0; i != count; ++i) value = (value << 8) | data[i];
    return value;
}

uint8_t result_conflicts(const CeResultExchange &state, const CeResultMessage &message) {
    uint8_t causes = CE_RESULT_CONFLICT_NONE;
    if (state.local_outcome != message.outcome) causes |= CE_RESULT_CONFLICT_OUTCOME;
    if (state.terminal_tick != message.terminal_tick) causes |= CE_RESULT_CONFLICT_TICK;
    if (state.terminal_fingerprint != message.terminal_fingerprint) {
        causes |= CE_RESULT_CONFLICT_FINGERPRINT;
    }
    return causes;
}

}  // namespace

extern "C" {

void ce_result_exchange_init(CeResultExchange *state) {
    if (state != 0) *state = CeResultExchange{};
}

CeResultAction ce_result_exchange_begin(CeResultExchange *state,
                                         CeCampaignIdentity identity,
                                         CeCampaignRole local_role,
                                         CeCampaignOutcome local_outcome,
                                         uint64_t terminal_tick,
                                         uint64_t terminal_fingerprint) {
    if (state == 0 || !valid_role(local_role) || !valid_outcome(local_outcome)) {
        return CE_RESULT_INVALID;
    }
    if (state->phase != CE_RESULT_IDLE) return CE_RESULT_CLOSED;

    CeResultExchange next = {};
    ce_network_campaign_init(&next.observed);
    if (ce_network_campaign_begin(&next.observed, identity) != CE_CAMPAIGN_ACCEPTED ||
        ce_network_campaign_report(&next.observed, identity, local_role,
                                   local_outcome) != CE_CAMPAIGN_ACCEPTED) {
        return CE_RESULT_INVALID;
    }
    next.identity = identity;
    next.local_outcome = local_outcome;
    next.terminal_tick = terminal_tick;
    next.terminal_fingerprint = terminal_fingerprint;
    next.local_role = local_role;
    next.phase = local_role == CE_CAMPAIGN_HOST ? CE_RESULT_HOST_OFFERING
                                               : CE_RESULT_GUEST_WAITING;
    *state = next;
    return CE_RESULT_ACCEPTED;
}

int ce_result_exchange_pending(const CeResultExchange *state, CeResultMessage *out) {
    if (state == 0 || out == 0) return 0;
    CeResultMessage next = {};
    switch (state->phase) {
    case CE_RESULT_HOST_OFFERING:
        next.kind = CE_RESULT_OFFER;
        break;
    case CE_RESULT_GUEST_PREPARED:
        next.kind = CE_RESULT_ACCEPT;
        break;
    case CE_RESULT_HOST_DECIDED:
        next.kind = CE_RESULT_DECISION;
        break;
    case CE_RESULT_GUEST_DECIDED:
        next.kind = CE_RESULT_RECEIPT;
        break;
    default:
        return 0;
    }
    next.identity = state->identity;
    next.outcome = state->local_outcome;
    next.terminal_tick = state->terminal_tick;
    next.terminal_fingerprint = state->terminal_fingerprint;
    next.sender = state->local_role;
    *out = next;
    return 1;
}

CeResultAction ce_result_exchange_receive(CeResultExchange *state,
                                           const CeResultMessage *message) {
    if (state == 0 || message == 0) return CE_RESULT_INVALID;
    if (state->phase == CE_RESULT_IDLE || state->phase == CE_RESULT_REJECTED ||
        state->phase == CE_RESULT_INDETERMINATE) return CE_RESULT_CLOSED;
    if (!valid_message(*message)) return CE_RESULT_INVALID;
    if (!same_identity(state->identity, message->identity)) return CE_RESULT_STALE;
    if (message->sender == state->local_role) return CE_RESULT_WRONG_PEER;

    bool duplicate = false;
    switch (state->phase) {
    case CE_RESULT_HOST_OFFERING:
        if (message->kind != CE_RESULT_ACCEPT) return CE_RESULT_OUT_OF_ORDER;
        break;
    case CE_RESULT_GUEST_WAITING:
        if (message->kind != CE_RESULT_OFFER) return CE_RESULT_OUT_OF_ORDER;
        break;
    case CE_RESULT_GUEST_PREPARED:
        if (message->kind == CE_RESULT_OFFER) duplicate = true;
        else if (message->kind != CE_RESULT_DECISION) return CE_RESULT_OUT_OF_ORDER;
        break;
    case CE_RESULT_HOST_DECIDED:
        if (message->kind == CE_RESULT_ACCEPT) duplicate = true;
        else if (message->kind != CE_RESULT_RECEIPT) return CE_RESULT_OUT_OF_ORDER;
        break;
    case CE_RESULT_GUEST_DECIDED:
        if (message->kind == CE_RESULT_OFFER || message->kind == CE_RESULT_DECISION) {
            duplicate = true;
        } else {
            return CE_RESULT_OUT_OF_ORDER;
        }
        break;
    case CE_RESULT_HOST_RECEIPTED:
        if (message->kind == CE_RESULT_ACCEPT || message->kind == CE_RESULT_RECEIPT) {
            duplicate = true;
        } else {
            return CE_RESULT_OUT_OF_ORDER;
        }
        break;
    default:
        return CE_RESULT_CLOSED;
    }

    const uint8_t causes = result_conflicts(*state, *message);
    if (causes != CE_RESULT_CONFLICT_NONE) {
        ce_network_campaign_stop(&state->observed, state->identity,
                                 CE_CAMPAIGN_STOP_FAILURE);
        state->conflict_causes |= causes;
        state->phase = CE_RESULT_REJECTED;
        return CE_RESULT_CONFLICTING;
    }
    if (duplicate) return CE_RESULT_DUPLICATE;

    CeResultExchange next = *state;
    if (state->phase == CE_RESULT_GUEST_WAITING ||
        state->phase == CE_RESULT_HOST_OFFERING) {
        if (ce_network_campaign_report(&next.observed, next.identity,
                static_cast<CeCampaignRole>(message->sender),
                static_cast<CeCampaignOutcome>(message->outcome)) != CE_CAMPAIGN_ACCEPTED) {
            return CE_RESULT_INVALID;
        }
    }
    switch (state->phase) {
    case CE_RESULT_HOST_OFFERING: next.phase = CE_RESULT_HOST_DECIDED; break;
    case CE_RESULT_GUEST_WAITING: next.phase = CE_RESULT_GUEST_PREPARED; break;
    case CE_RESULT_GUEST_PREPARED: next.phase = CE_RESULT_GUEST_DECIDED; break;
    case CE_RESULT_HOST_DECIDED: next.phase = CE_RESULT_HOST_RECEIPTED; break;
    default: return CE_RESULT_CLOSED;
    }
    *state = next;
    return CE_RESULT_ACCEPTED;
}

CeResultAction ce_result_exchange_disconnect(CeResultExchange *state,
                                              CeCampaignIdentity identity) {
    if (state == 0) return CE_RESULT_INVALID;
    if (state->phase == CE_RESULT_IDLE) return CE_RESULT_CLOSED;
    if (!same_identity(state->identity, identity)) return CE_RESULT_STALE;
    if (state->phase == CE_RESULT_INDETERMINATE) return CE_RESULT_DUPLICATE;
    ce_network_campaign_stop(&state->observed, identity, CE_CAMPAIGN_STOP_DISCONNECT);
    state->phase = CE_RESULT_INDETERMINATE;
    return CE_RESULT_ACCEPTED;
}

size_t ce_result_message_size(void) { return kEncodedSize; }

int ce_result_message_encode(const CeResultMessage *message, uint8_t *out,
                             size_t capacity, size_t *written) {
    if (written == 0) return 0;
    *written = 0;
    if (message == 0 || out == 0 || capacity < kEncodedSize ||
        !valid_message(*message)) return 0;
    uint8_t data[kEncodedSize] = {};
    data[0] = kPrototypeVersion;
    data[1] = static_cast<uint8_t>(message->kind);
    data[2] = static_cast<uint8_t>(message->sender);
    data[3] = static_cast<uint8_t>(message->outcome);
    put_uint(data + 4, message->identity.session, 8);
    put_uint(data + 12, message->identity.attempt, 8);
    put_uint(data + 20, message->identity.mission, 4);
    put_uint(data + 24, message->terminal_tick, 8);
    put_uint(data + 32, message->terminal_fingerprint, 8);
    for (size_t i = 0; i != kEncodedSize; ++i) out[i] = data[i];
    *written = kEncodedSize;
    return 1;
}

int ce_result_message_decode(CeResultMessage *out, const uint8_t *data, size_t size) {
    if (out == 0 || data == 0 || size != kEncodedSize || data[0] != kPrototypeVersion) {
        return 0;
    }
    if ((data[1] != CE_RESULT_OFFER && data[1] != CE_RESULT_ACCEPT &&
         data[1] != CE_RESULT_DECISION && data[1] != CE_RESULT_RECEIPT) ||
        (data[2] != CE_CAMPAIGN_HOST && data[2] != CE_CAMPAIGN_GUEST) ||
        (data[3] != CE_CAMPAIGN_OUTCOME_WIN && data[3] != CE_CAMPAIGN_OUTCOME_LOSS)) {
        return 0;
    }
    CeResultMessage next = {};
    next.kind = data[1];
    next.sender = data[2];
    next.outcome = data[3];
    next.identity.session = get_uint(data + 4, 8);
    next.identity.attempt = get_uint(data + 12, 8);
    next.identity.mission = static_cast<uint32_t>(get_uint(data + 20, 4));
    next.terminal_tick = get_uint(data + 24, 8);
    next.terminal_fingerprint = get_uint(data + 32, 8);
    if (!valid_message(next)) return 0;
    *out = next;
    return 1;
}

}  // extern "C"
