// SPDX-License-Identifier: GPL-3.0-only
// Authored deterministic I/O: no sockets, dependency agents or credentials.
#include "../ice_bridge.h"
#include "fixture_guard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

struct juice_agent { unsigned fixture = 0; };

namespace {
unsigned checks = 0;
void Check(bool okay, const char* name) {
    ++checks;
    if (!okay) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
}
constexpr ENetSocket kCeSocket = 11, kBridgeSocket = 12;
const ENetAddress kCeAddress{1, 45000}, kBridgeAddress{1, 45001};
juice_agent agent;
juice_config_t callbacks{};
bool alive = false;
unsigned ice_failures = 0, delivery_failures = 0;
struct Outbound { std::string bytes; bool owned = true; };
std::deque<Outbound> outbound;
std::vector<std::string> ice_sent, delivered;

void Receive(const std::string& text) {
    Check(alive && callbacks.cb_recv, "receive-callback-is-owned");
    callbacks.cb_recv(&agent, text.data(), text.size(), callbacks.user_ptr);
}
void SameLosses(const CeIceBridge& bridge, uint64_t queue, uint64_t ice, uint64_t local,
                const char* label) {
    const auto snapshot = bridge.LossSnapshot();
    Check(snapshot.queue_overflow == queue && snapshot.ice_send == ice &&
          snapshot.local_delivery == local, label);
}
}

// These are complete authored substitutes for every external call made by the
// production bridge. No real dependency implementation or OS socket is linked.
extern "C" {
int enet_address_set_host_ip(ENetAddress* address, const char* name) {
    Check(!std::strcmp(name, "127.0.0.1"), "bridge-selects-loopback");
    address->host = kCeAddress.host;
    return 0;
}
ENetSocket enet_socket_create(ENetSocketType type) {
    Check(type == ENET_SOCKET_TYPE_DATAGRAM, "bridge-requests-datagram-socket");
    return kBridgeSocket;
}
int enet_socket_get_address(ENetSocket socket, ENetAddress* address) {
    if (socket != kCeSocket && socket != kBridgeSocket) return -1;
    *address = socket == kCeSocket ? kCeAddress : kBridgeAddress;
    return 0;
}
int enet_socket_bind(ENetSocket socket, const ENetAddress* address) {
    Check(socket == kBridgeSocket && address->host == kCeAddress.host && !address->port,
          "bridge-bind-is-owned-loopback");
    return 0;
}
int enet_socket_set_option(ENetSocket socket, ENetSocketOption option, int value) {
    Check(socket == kBridgeSocket && option == ENET_SOCKOPT_NONBLOCK && value == 1,
          "bridge-socket-is-nonblocking");
    return 0;
}
void enet_socket_destroy(ENetSocket socket) {
    Check(socket == kBridgeSocket && !alive, "agent-retires-before-local-socket");
}
int enet_socket_receive(ENetSocket socket, ENetAddress* sender, ENetBuffer* buffers, size_t count) {
    Check(socket == kBridgeSocket && count == 1, "receive-uses-owned-shuttle");
    if (outbound.empty()) return 0;
    const auto packet = outbound.front(); outbound.pop_front();
    Check(buffers[0].dataLength >= packet.bytes.size(), "authored-outbound-fits-buffer");
    std::memcpy(buffers[0].data, packet.bytes.data(), packet.bytes.size());
    *sender = kCeAddress;
    if (!packet.owned) ++sender->port;
    return int(packet.bytes.size());
}
int enet_socket_send(ENetSocket socket, const ENetAddress* destination,
                     const ENetBuffer* buffers, size_t count) {
    Check(socket == kBridgeSocket && destination->host == kCeAddress.host &&
          destination->port == kCeAddress.port && count == 1,
          "delivery-target-is-only-owned-ce-endpoint");
    if (delivery_failures) { --delivery_failures; return -1; }
    delivered.emplace_back(static_cast<const char*>(buffers[0].data), buffers[0].dataLength);
    return int(buffers[0].dataLength);
}
void juice_set_log_level(juice_log_level_t level) {
    Check(level == JUICE_LOG_LEVEL_NONE, "dependency-logging-remains-disabled");
}
juice_agent_t* juice_create(const juice_config_t* config) {
    Check(!alive, "one-authored-agent-at-a-time");
    callbacks = *config; alive = true;
    return &agent;
}
void juice_destroy(juice_agent_t* selected) {
    Check(selected == &agent && alive, "only-owned-agent-is-destroyed");
    alive = false;
}
int juice_set_relay_only(juice_agent_t* selected, bool required) {
    Check(selected == &agent && !required, "fixture-explicitly-selects-no-network-mode");
    return JUICE_ERR_SUCCESS;
}
int juice_gather_candidates(juice_agent_t* selected) {
    Check(selected == &agent && alive, "gather-targets-owned-agent");
    callbacks.cb_state_changed(&agent, JUICE_STATE_COMPLETED, callbacks.user_ptr);
    callbacks.cb_gathering_done(&agent, callbacks.user_ptr);
    return JUICE_ERR_SUCCESS;
}
int juice_send(juice_agent_t* selected, const char* data, size_t size) {
    Check(selected == &agent && alive, "forward-target-is-only-owned-agent");
    if (ice_failures) { --ice_failures; return JUICE_ERR_FAILED; }
    ice_sent.emplace_back(data, size);
    return JUICE_ERR_SUCCESS;
}
int juice_get_local_description(juice_agent_t*, char*, size_t) { return JUICE_ERR_FAILED; }
int juice_set_remote_description(juice_agent_t*, const char*) { return JUICE_ERR_FAILED; }
int juice_set_remote_gathering_done(juice_agent_t*) { return JUICE_ERR_FAILED; }
int juice_get_selected_candidates(juice_agent_t*, char*, size_t, char*, size_t) { return JUICE_ERR_FAILED; }
}

int main() {
    Check(FixtureGuard(), "fixture-refuses-system-core-dumps");
    {
        ENetHost ce{}; ce.peerCount = 1; ce.socket = kCeSocket;
        juice_config_t config{};
        auto bridge = CeIceBridge::Create(&ce, config, false);
        Check(bool(bridge) && bridge->Ready(), "authored-bridge-is-ready-without-network");
        SameLosses(*bridge, 0, 0, 0, "new-bridge-has-no-losses");

        for (unsigned i = 0; i < 65; ++i) Receive("authored inbound " + std::to_string(i));
        SameLosses(*bridge, 1, 0, 0, "sixty-fifth-callback-counts-one-overflow");
        Check(delivered.empty(), "callback-never-forwards-or-logs-payloads");
        Check(bridge->Pump() && !bridge->Failed(), "overflow-preserves-ordinary-udp-loss-behavior");
        Check(delivered.size() == 64 && delivered.front() == "authored inbound 0" &&
              delivered.back() == "authored inbound 63", "overflow-keeps-earlier-datagrams-in-order");
        SameLosses(*bridge, 1, 0, 0, "draining-retains-overflow-snapshot");
        Receive("authored after overflow");
        Check(bridge->Pump() && delivered.size() == 65 &&
              delivered.back() == "authored after overflow", "overflow-recovers-after-drain");

        delivery_failures = 1;
        Receive("authored failed local delivery");
        Check(bridge->Pump() && bridge->Ready() && !bridge->Failed() && delivered.size() == 65,
              "local-forwarding-loss-is-nonfatal");
        SameLosses(*bridge, 1, 0, 1, "local-forwarding-failure-counted-once");
        Receive("authored local recovery");
        Check(bridge->Pump() && delivered.size() == 66 &&
              delivered.back() == "authored local recovery", "local-forwarding-recovers");
        SameLosses(*bridge, 1, 0, 1, "successful-local-delivery-does-not-count-as-loss");

        ice_failures = 1;
        outbound.push_back({"authored failed ice forwarding"});
        outbound.push_back({"authored ice recovery"});
        Check(bridge->Pump() && bridge->Ready() && !bridge->Failed() && ice_sent.size() == 1 &&
              ice_sent.back() == "authored ice recovery", "ice-forwarding-loss-is-nonfatal-and-recovers");
        SameLosses(*bridge, 1, 1, 1, "ice-forwarding-failure-counted-once");
        Check(bridge->SentDatagrams() == 1 && bridge->DeliveredDatagrams() == 66,
              "successful-datagram-counters-retain-their-meaning");

        outbound.push_back({"authored unrelated local endpoint", false});
        Check(bridge->Pump() && bridge->RejectedLocalDatagrams() == 1,
              "unrelated-local-datagram-is-still-rejected");
        SameLosses(*bridge, 1, 1, 1, "rejected-source-is-separate-from-forwarding-loss");
        Check(bridge->Pump() && delivered.size() == 66 && ice_sent.size() == 1,
              "empty-pump-never-replays-dropped-datagrams");
        SameLosses(*bridge, 1, 1, 1, "snapshots-remain-stable-after-recovery");
    }
    Check(!alive, "all-authored-io-owners-are-retired");
    std::printf("PASS %u authored ICE bridge diagnostic checks\n", checks);
    return 0;
}
