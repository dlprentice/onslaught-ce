// SPDX-License-Identifier: GPL-3.0-only
// Authored data only. Every socket and the TURN server bind to loopback.
#include "../ice_bridge.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

extern "C" int ce_test_relay_policy_control(void);

namespace {
ENetAddress receive_fault_address{};
int receive_fault_remaining = 0;
int receive_fault_value = -2;
}

// A local return-value control, not malformed network traffic. Only the
// selected bridge socket receives the injected ENet result.
extern "C" int __real_enet_socket_receive(ENetSocket, ENetAddress*, ENetBuffer*, size_t);
extern "C" int __wrap_enet_socket_receive(ENetSocket socket, ENetAddress* address,
                                         ENetBuffer* buffers, size_t count) {
    ENetAddress local{};
    if (receive_fault_remaining && enet_socket_get_address(socket, &local) == 0 &&
        local.host == receive_fault_address.host && local.port == receive_fault_address.port) {
        --receive_fault_remaining;
        return receive_fault_value;
    }
    return __real_enet_socket_receive(socket, address, buffers, count);
}

namespace {
int checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<typename Predicate> bool Wait(Predicate predicate, unsigned milliseconds = 5000) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < until);
    return false;
}
ENetHost* Host(size_t peers = 1) {
    ENetAddress address{};
    enet_address_set_host_ip(&address, "127.0.0.1");
    return enet_host_create(&address, peers, 2, 0, 0);
}
struct Pair {
    ENetHost* host = Host();
    ENetHost* join = Host();
    std::unique_ptr<CeIceBridge> a, b;
    ~Pair() {
        a.reset(); b.reset();
        if (host) enet_host_destroy(host);
        if (join) enet_host_destroy(join);
    }
    bool Pump() { return a->Pump() && b->Pump(); }
};
void RelayRoundTrip() {
    // These credentials belong only to this in-process, loopback fixture.
    juice_server_credentials_t credentials{"ce-local-fixture", "authored-local-value", 4};
    juice_server_config_t server_config{};
    server_config.credentials = &credentials;
    server_config.credentials_count = 1;
    server_config.max_allocations = 4;
    server_config.max_peers = 4;
    server_config.bind_address = "127.0.0.1";
    server_config.external_address = "127.0.0.1";
    server_config.realm = "ce-local-fixture";
    juice_server_t* server = juice_server_create(&server_config);
    Check(server != nullptr, "local TURN server created");
    {
        Pair pair;
        Check(pair.host && pair.join, "two CE-owned ENet endpoints created");
        juice_turn_server_t turn{"127.0.0.1", credentials.username, credentials.password,
                                 juice_server_get_port(server)};
        juice_config_t config{};
        config.bind_address = "127.0.0.1";
        config.turn_servers = &turn;
        config.turn_servers_count = 1;
        pair.a = CeIceBridge::Create(pair.host, config);
        pair.b = CeIceBridge::Create(pair.join, config);
        Check(pair.a && pair.b, "both ICE bridges created");
        std::string a, b;
        Check(Wait([&] { return pair.a->LocalDescription(a) && pair.b->LocalDescription(b); }),
              "both relay-only descriptions gathered");
        Check(a.find(" typ host") == std::string::npos && b.find(" typ host") == std::string::npos,
              "host candidates absent from exchanged descriptions");
        Check(!pair.a->SetRemoteDescription(""), "empty description refused");
        Check(pair.a->SetRemoteDescription(b) && pair.b->SetRemoteDescription(a),
              "one peer description accepted per bridge");
        Check(!pair.a->SetRemoteDescription(b), "peer cannot be replaced in a running bridge");
        Check(Wait([&] { return pair.Pump() && pair.a->Ready() && pair.b->Ready(); }),
              "both ICE paths become ready");
        Check(pair.a->UsesRelay() && pair.b->UsesRelay(), "selected paths actually include TURN");

        receive_fault_address = pair.a->LocalAddress();
        receive_fault_remaining = 1;
        Check(pair.Pump() && !pair.a->Failed() && receive_fault_remaining == 0,
              "recoverable receive result keeps the bridge usable");

        ENetPeer* join_peer = enet_host_connect(pair.join, &pair.b->LocalAddress(), 2, 0);
        Check(join_peer != nullptr, "ENet begins through its bridge");
        ENetPeer* host_peer = nullptr;
        int connects = 0;
        Check(Wait([&] {
            if (!pair.Pump()) return false;
            ENetEvent event{};
            while (enet_host_service(pair.host, &event, 0) > 0) {
                if (event.type == ENET_EVENT_TYPE_CONNECT) { host_peer = event.peer; ++connects; }
                if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
            }
            while (enet_host_service(pair.join, &event, 0) > 0) {
                if (event.type == ENET_EVENT_TYPE_CONNECT) ++connects;
                if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
            }
            return connects == 2;
        }), "real ENet connection crosses local TURN");
        const std::string forward(5000, 'C');
        const std::string reply = "CE authored reply";
        Check(enet_peer_send(join_peer, 1, enet_packet_create(forward.data(), forward.size(),
                                                          ENET_PACKET_FLAG_RELIABLE)) == 0,
              "fragmented reliable game-sized message queued");
        Check(enet_peer_send(host_peer, 0, enet_packet_create(reply.data(), reply.size(),
                                                          ENET_PACKET_FLAG_RELIABLE)) == 0,
              "reverse reliable message queued");
        bool got_forward = false, got_reply = false;
        Check(Wait([&] {
            if (!pair.Pump()) return false;
            ENetEvent event{};
            while (enet_host_service(pair.host, &event, 0) > 0)
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    got_forward = event.channelID == 1 && std::string(
                        reinterpret_cast<char*>(event.packet->data), event.packet->dataLength) == forward;
                    enet_packet_destroy(event.packet);
                }
            while (enet_host_service(pair.join, &event, 0) > 0)
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    got_reply = event.channelID == 0 && std::string(
                        reinterpret_cast<char*>(event.packet->data), event.packet->dataLength) == reply;
                    enet_packet_destroy(event.packet);
                }
            return got_forward && got_reply;
        }), "reliable channels and fragmented payload survive relay both ways");

        unsigned sustained_host = 0, sustained_join = 0;
        bool forbidden_after_removal = false;
        const auto start = std::chrono::steady_clock::now();
        auto last_host = start, last_join = start, next_send = start;
        auto drain = [&](ENetHost* endpoint, unsigned& count,
                         std::chrono::steady_clock::time_point& last) {
            ENetEvent event{};
            while (enet_host_service(endpoint, &event, 0) > 0) {
                if (event.type != ENET_EVENT_TYPE_RECEIVE) continue;
                const std::string text(reinterpret_cast<char*>(event.packet->data), event.packet->dataLength);
                if (text == "sustained relay fixture") { ++count; last = std::chrono::steady_clock::now(); }
                if (text == "after relay removal") forbidden_after_removal = true;
                enet_packet_destroy(event.packet);
            }
        };
        auto send_both = [&](const char* text) {
            return enet_peer_send(join_peer, 0, enet_packet_create(text, std::strlen(text), ENET_PACKET_FLAG_RELIABLE)) == 0 &&
                   enet_peer_send(host_peer, 0, enet_packet_create(text, std::strlen(text), ENET_PACKET_FLAG_RELIABLE)) == 0;
        };
        bool sustained = true;
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
            sustained = sustained && pair.Pump() && pair.a->Ready() && pair.b->Ready() &&
                        pair.a->UsesRelay() && pair.b->UsesRelay();
            if (!sustained) break;
            if (std::chrono::steady_clock::now() >= next_send) {
                sustained = send_both("sustained relay fixture");
                next_send += std::chrono::milliseconds(50);
            }
            drain(pair.host, sustained_host, last_host); drain(pair.join, sustained_join, last_join);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(sustained && sustained_host >= 40 && sustained_join >= 40 &&
              last_host - start >= std::chrono::milliseconds(2500) &&
              last_join - start >= std::chrono::milliseconds(2500),
              "selected relay and bidirectional delivery survive three-second nomination window");

        ENetHost* unrelated = Host();
        Check(unrelated != nullptr, "unrelated local fixture endpoint created");
        char ignored[] = "unrelated fixture";
        ENetBuffer buffer{}; buffer.data = ignored; buffer.dataLength = sizeof(ignored);
        const auto rejected = pair.a->RejectedLocalDatagrams();
        enet_socket_send(unrelated->socket, &pair.a->LocalAddress(), &buffer, 1);
        Check(Wait([&] { return pair.Pump() && pair.a->RejectedLocalDatagrams() > rejected; }),
              "bridge accepts only its CE-owned source endpoint");
        enet_host_destroy(unrelated);
        Check(pair.a->SentDatagrams() && pair.b->SentDatagrams() &&
              pair.a->DeliveredDatagrams() && pair.b->DeliveredDatagrams(),
              "both bridge directions carried datagrams");
        juice_server_destroy(server); server = nullptr;
        Check(send_both("after relay removal"), "post-removal application messages queued");
        const auto removed = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - removed < std::chrono::milliseconds(2200)) {
            pair.Pump();
            drain(pair.host, sustained_host, last_host); drain(pair.join, sustained_join, last_join);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(!forbidden_after_removal, "removed relay does not fall back to direct payload delivery");
        std::cout << "Relay window: 3000 ms, " << sustained_host << '/' << sustained_join
                  << " payloads; removal window: 2200 ms, zero post-removal payloads\n";
    }
    if (server) juice_server_destroy(server);
}
void DirectDefaultControl() {
    Pair pair;
    juice_config_t config{}; config.bind_address = "127.0.0.1";
    pair.a = CeIceBridge::Create(pair.host, config, false);
    pair.b = CeIceBridge::Create(pair.join, config, false);
    Check(pair.a && pair.b, "default direct control bridges created");
    std::string a, b;
    Check(Wait([&] { return pair.a->LocalDescription(a) && pair.b->LocalDescription(b); }) &&
          pair.a->SetRemoteDescription(b) && pair.b->SetRemoteDescription(a), "direct descriptions exchanged");
    Check(Wait([&] { return pair.Pump() && pair.a->Ready() && pair.b->Ready(); }) &&
          !pair.a->UsesRelay() && !pair.b->UsesRelay(), "policy-off direct path remains usable");
    char payload[] = "owned direct default";
    ENetBuffer send{}; send.data = payload; send.dataLength = sizeof(payload);
    Check(enet_socket_send(pair.host->socket, &pair.a->LocalAddress(), &send, 1) == static_cast<int>(sizeof(payload)),
          "direct control payload sent");
    Check(Wait([&] {
        pair.Pump(); char data[64]{}; ENetBuffer receive{}; receive.data = data; receive.dataLength = sizeof(data);
        ENetAddress sender{};
        int size = enet_socket_receive(pair.join->socket, &sender, &receive, 1);
        return size == static_cast<int>(sizeof(payload)) && std::memcmp(data, payload, sizeof(payload)) == 0;
    }), "default direct payload delivered");
    receive_fault_address = pair.a->LocalAddress(); receive_fault_value = -1; receive_fault_remaining = 1;
    Check(!pair.a->Pump() && pair.a->Failed() && receive_fault_remaining == 0,
          "fatal receive result still ends the bridge");
}
void RelayRequiredControl() {
    Pair pair;
    Check(pair.host && pair.join, "control endpoints created");
    juice_config_t config{};
    config.bind_address = "127.0.0.1";
    pair.a = CeIceBridge::Create(pair.host, config, false);
    pair.b = CeIceBridge::Create(pair.join, config, true);
    Check(pair.a && pair.b, "direct and relay-required control bridges created");
    std::string direct, relay;
    Check(Wait([&] { return pair.a->LocalDescription(direct); }), "direct-only local description gathered");
    Check(!pair.b->SetRemoteDescription(direct), "relay-required path refuses direct-only description");
    Check(!pair.b->LocalDescription(relay) && !pair.b->Ready(), "missing relay never becomes usable");
    ENetHost* multi = Host(2);
    Check(multi && !CeIceBridge::Create(multi, config), "multi-peer host refused");
    enet_host_destroy(multi);
    Check(!CeIceBridge::Create(nullptr, config), "missing CE endpoint refused");
    ENetAddress other_address{};
    enet_address_set_host_ip(&other_address, "127.0.0.2");
    ENetHost* other = enet_host_create(&other_address, 1, 2, 0, 0);
    Check(other && !CeIceBridge::Create(other, config), "CE socket bound away from chosen loopback refused");
    enet_host_destroy(other);
}
}
int main() {
    Check(enet_initialize() == 0, "ENet initialization");
    juice_set_log_level(JUICE_LOG_LEVEL_NONE);
    Check(ce_test_relay_policy_control() == 0, "candidate and exact-send policy controls preserve default behavior");
    RelayRoundTrip();
    DirectDefaultControl();
    RelayRequiredControl();
    enet_deinitialize();
    std::cout << "PASS " << checks << " authored local ICE/ENet checks\n";
}
