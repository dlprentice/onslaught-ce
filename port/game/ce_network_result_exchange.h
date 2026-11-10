// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_NETWORK_RESULT_EXCHANGE_H
#define CE_NETWORK_RESULT_EXCHANGE_H

#include <stddef.h>
#include <stdint.h>

#include "ce_network_campaign.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Disconnected prototype only: these messages are not the game's wire protocol.
 * No phase grants permission to persist, unlock, or start another mission.
 * The caller must serialize access, authenticate the remote role, check content
 * compatibility, supply a meaningful terminal fingerprint, manage timeouts, and
 * never reuse an attempt identity. The component provides none of those
 * guarantees itself. Its fingerprint is only a placeholder; it is not an
 * authenticated or cryptographic integrity check.
 */
typedef enum CeResultMessageKind {
    CE_RESULT_OFFER = 1,
    CE_RESULT_ACCEPT = 2,
    CE_RESULT_DECISION = 3,
    CE_RESULT_RECEIPT = 4
} CeResultMessageKind;

typedef struct CeResultMessage {
    CeCampaignIdentity identity;
    uint8_t outcome;
    uint64_t terminal_tick;
    uint64_t terminal_fingerprint;
    uint8_t kind;
    uint8_t sender;
} CeResultMessage;

typedef enum CeResultPhase {
    CE_RESULT_IDLE = 0,
    CE_RESULT_HOST_OFFERING,
    CE_RESULT_GUEST_WAITING,
    CE_RESULT_GUEST_PREPARED,
    CE_RESULT_HOST_DECIDED,
    CE_RESULT_GUEST_DECIDED,
    CE_RESULT_HOST_RECEIPTED,
    CE_RESULT_REJECTED,
    CE_RESULT_INDETERMINATE
} CeResultPhase;

typedef enum CeResultAction {
    CE_RESULT_ACCEPTED = 0,
    CE_RESULT_DUPLICATE,
    CE_RESULT_STALE,
    CE_RESULT_INVALID,
    CE_RESULT_WRONG_PEER,
    CE_RESULT_OUT_OF_ORDER,
    CE_RESULT_CONFLICTING,
    CE_RESULT_CLOSED
} CeResultAction;

/* Locally observed causes; these are not a shared abort or finality decision. */
typedef enum CeResultConflict {
    CE_RESULT_CONFLICT_NONE = 0,
    CE_RESULT_CONFLICT_OUTCOME = 1,
    CE_RESULT_CONFLICT_TICK = 2,
    CE_RESULT_CONFLICT_FINGERPRINT = 4
} CeResultConflict;

typedef struct CeResultExchange {
    CeNetworkCampaign observed;
    CeCampaignIdentity identity;
    CeCampaignOutcome local_outcome;
    uint64_t terminal_tick;
    uint64_t terminal_fingerprint;
    CeCampaignRole local_role;
    CeResultPhase phase;
    /* Bitwise CeResultConflict causes survive disconnect, but not init. */
    uint8_t conflict_causes;
} CeResultExchange;

void ce_result_exchange_init(CeResultExchange *state);
CeResultAction ce_result_exchange_begin(CeResultExchange *state,
                                         CeCampaignIdentity identity,
                                         CeCampaignRole local_role,
                                         CeCampaignOutcome local_outcome,
                                         uint64_t terminal_tick,
                                         uint64_t terminal_fingerprint);

/* Return the current retransmittable message, if any. The output is unchanged
 * if there is no message. A guest cannot know that its last receipt arrived. */
int ce_result_exchange_pending(const CeResultExchange *state, CeResultMessage *out);
CeResultAction ce_result_exchange_receive(CeResultExchange *state,
                                           const CeResultMessage *message);

/* A disconnect makes the joint outcome indeterminate, even after a decision
 * or receipt: one side cannot know which final message the other observed.
 * It retains locally observed conflict causes. This prototype sends no abort;
 * a future integration must notify the peer or enforce a bounded timeout. */
CeResultAction ce_result_exchange_disconnect(CeResultExchange *state,
                                              CeCampaignIdentity identity);

/* Bounded, fixed-size prototype codec, not a published protocol. On failure,
 * decode leaves its output unchanged; encode sets written to zero. */
size_t ce_result_message_size(void);
int ce_result_message_encode(const CeResultMessage *message, uint8_t *out,
                             size_t capacity, size_t *written);
int ce_result_message_decode(CeResultMessage *out, const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
