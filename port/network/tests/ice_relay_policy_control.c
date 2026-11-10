// SPDX-License-Identifier: GPL-3.0-only
// Authored selected-entry controls. No fixture application send reaches a socket.
#include "agent.h"
#include <string.h>

static juice_agent_t *controlled_agent;
static unsigned attempted_sends;

int __real_conn_send(juice_agent_t *, const addr_record_t *, const char *, size_t, int);
int __wrap_conn_send(juice_agent_t *agent, const addr_record_t *destination,
                     const char *data, size_t size, int ds) {
    if (agent == controlled_agent) {
        ++attempted_sends;
        return (int)size;
    }
    return __real_conn_send(agent, destination, data, size, ds);
}

int ce_test_relay_policy_control(void) {
    juice_config_t config = {0};
    config.bind_address = "127.0.0.1";
    juice_agent_t *agent = juice_create(&config);
    if (!agent) return 1;
    controlled_agent = agent;
    ice_candidate_t local = {0}, remote = {0};
    local.type = ICE_CANDIDATE_TYPE_HOST;
    remote.type = ICE_CANDIDATE_TYPE_HOST;
    remote.transport = ICE_CANDIDATE_TRANSPORT_UDP;
    ice_candidate_pair_t pair = {0};
    pair.local = &local; pair.remote = &remote;
    agent_stun_entry_t entry = {0};
    entry.pair = &pair;
    const char payload[] = "owned policy control";
    int failed = 0;

    // The original default still permits a selected direct application path.
    atomic_store(&agent->selected_entry, &entry);
    attempted_sends = 0;
    if (juice_send(agent, payload, sizeof(payload)) != JUICE_ERR_SUCCESS || attempted_sends != 1) failed = 2;
    atomic_store(&agent->selected_entry, NULL);
    if (juice_set_relay_only(agent, true) != JUICE_ERR_SUCCESS) failed = 3;

    // Inject a forbidden selection independently of candidate filtering. The
    // send policy itself must stop it before the intercepted socket boundary.
    atomic_store(&agent->selected_entry, &entry);
    attempted_sends = 0;
    if (juice_send(agent, payload, sizeof(payload)) == JUICE_ERR_SUCCESS || attempted_sends) failed = 4;
    remote.type = ICE_CANDIDATE_TYPE_RELAYED;
    if (juice_send(agent, payload, sizeof(payload)) != JUICE_ERR_SUCCESS || attempted_sends != 1) failed = 5;
    remote.type = ICE_CANDIDATE_TYPE_HOST;
    local.type = ICE_CANDIDATE_TYPE_RELAYED;
    attempted_sends = 0;
    if (juice_send(agent, payload, sizeof(payload)) == JUICE_ERR_SUCCESS || attempted_sends) failed = 6;
    atomic_store(&agent->selected_entry, NULL);

    // No-relay pairs never enter the checklist, including a reflexive remote.
    remote.type = ICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
    if (agent_add_candidate_pair(agent, NULL, &remote) != 0 || agent->candidate_pairs_count) failed = 7;
    remote.type = ICE_CANDIDATE_TYPE_RELAYED;
    if (agent_add_candidate_pair(agent, NULL, &remote) != 0 || agent->candidate_pairs_count != 1) failed = 8;
    if (juice_set_relay_only(agent, false) == JUICE_ERR_SUCCESS) failed = 9;
    juice_destroy(agent);
    controlled_agent = NULL;
    return failed;
}
