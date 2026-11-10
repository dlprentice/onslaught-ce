// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_ICE_SESSION_H
#define CE_ICE_SESSION_H
#if defined(CE_NETWORK_ICE)
#include "network.h"
#include <juice/juice.h>
#ifdef __cplusplus
extern "C" {
#endif

// Explicit optional transport path. The existing CE_NetCreate remains direct-IP.
// The config's IPv4/port are ignored: each ENet endpoint binds only to loopback,
// on a fresh port. libjuice owns the explicitly supplied ICE/TURN configuration.
// Relay candidates are required. No signaling service or credential lookup runs.
// ICE timeout: zero selects 30000 ms, otherwise 100..180000 ms. The CE handshake
// starts only after ICE is ready; all normal admission/compatibility checks apply.
CE_NetSession *CE_NetCreateIce(const CE_NetConfig *config, const juice_config_t *ice,
                             uint32_t timeout_ms);

// Reserve the exact CE identity and loopback endpoint while an explicit provider
// prepares its bound grant. No libjuice agent, ENet connect or CE handshake starts
// until Start succeeds. The original preparation deadline is not reset by Start.
CE_NetSession *CE_NetCreateIcePending(const CE_NetConfig *, uint32_t timeout_ms);
int CE_NetIceStart(CE_NetSession *, const juice_config_t *);

// Complete descriptions contain temporary credentials. Exchange them privately
// through the intended session's provider; never put them in logs or join codes.
// Local: 1 copied including NUL, 0 not ready/not an ICE session, -1 invalid or too
// small output (cleared when possible). Remote: 1 accepted, 0 refused. Size does
// not include a terminating NUL. Exactly one remote description is accepted.
int CE_NetIceLocalDescription(const CE_NetSession *, char *out, uint32_t capacity);
int CE_NetIceSetRemoteDescription(CE_NetSession *, const char *, uint32_t size);

// ICE readiness is not CE admission. Only CE_NET_ACTIVE admits application data.
int CE_NetIceReady(const CE_NetSession *);
int CE_NetIceUsesRelay(const CE_NetSession *);

// Read-only progress of the session's ICE agent, for the exchange's private trace: gathering finished (0 or 1),
// the number of relay candidates in the local description so far, and the agent's state index (libjuice's
// juice_state_t: 0 disconnected, 1 gathering, 2 connecting, 3 connected, 4 completed, 5 failed). Counts and
// indexes only; no description, address or credential. Returns 1 with the fields set while an agent exists,
// otherwise 0 with every field zero.
typedef struct CE_NetIceProgressInfo {
    uint32_t gathered;
    uint32_t relay_candidates;
    uint32_t state;
} CE_NetIceProgressInfo;
int CE_NetIceProgress(const CE_NetSession *, CE_NetIceProgressInfo *);

// Explicit adapters may own bounded signaling alongside the session. Ownership
// transfers on successful attachment. Poll returns a CE_NetError (CE_NET_OK while
// pending/ready); it must not close/destroy the session or replace callbacks.
// Error returns fixed text only, never paths, descriptions or credentials.
typedef struct CE_NetIceExchangeCallbacks {
    uint32_t (*poll)(void *context, CE_NetSession *, uint64_t now_ms);
    const char *(*error)(void *context);
    void (*destroy)(void *context);
} CE_NetIceExchangeCallbacks;
int CE_NetIceAttachExchange(CE_NetSession *, void *context, const CE_NetIceExchangeCallbacks *);
const char *CE_NetIceProblem(const CE_NetSession *);

// Use normal CE_NetPoll/Close/Destroy on the caller's ENet thread. Cancellation,
// failure and destruction retire ICE before its host; reconnect creates a new
// session and new descriptions. There is no direct-IP fallback for an ICE session.
#ifdef __cplusplus
}
#endif
#endif
#endif
