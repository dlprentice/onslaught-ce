// SPDX-License-Identifier: GPL-3.0-only
// Authored loopback peers only; exercises the public CE session API, without game data.
#include "../network.h"
#include "../ice_session.h"
#include "fixture_guard.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {
unsigned checks;
void Check(bool ok, const char *message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
using Session = std::unique_ptr<CE_NetSession, decltype(&CE_NetDestroy)>;
CE_NetStatus Status(const Session &session) {
    CE_NetStatus status{};
    CE_NetGetStatus(session.get(), &status);
    return status;
}
CE_NetConfig Config(uint32_t role) {
    CE_NetConfig config{};
    config.role = role; config.ipv4 = "127.0.0.1";
    config.handshake_ms = 400; config.silence_ms = 1000;
    for (unsigned i = 0; i != 32; ++i) {
        config.secret[i] = uint8_t(i + 1);
        config.compatibility[i] = uint8_t(i * 7 + 3);
    }
    std::strcpy(config.version, "authored-fixture");
    return config;
}
void Poll(const Session &a, const Session &b) {
    CE_NetPoll(a.get(), CE_NetNowMs(), 64);
    CE_NetPoll(b.get(), CE_NetNowMs(), 64);
}
template<class Predicate> bool Wait(Predicate predicate, unsigned ms = 5000) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < until);
    return false;
}
bool Active(const Session &a, const Session &b) {
    Poll(a, b);
    return Status(a).phase == CE_NET_ACTIVE && Status(b).phase == CE_NET_ACTIVE;
}
void DirectControl() {
    auto host_config = Config(CE_NET_HOST);
    Session host(CE_NetCreate(&host_config), CE_NetDestroy);
    Check(bool(host), "direct host still creates");
    auto guest_config = Config(CE_NET_GUEST);
    guest_config.port = Status(host).port;
    Session guest(CE_NetCreate(&guest_config), CE_NetDestroy);
    Check(bool(guest), "direct guest still creates");
    Check(Wait([&] { return Active(host, guest); }), "direct CE authentication still completes");
    const uint8_t payload[] = {3, 7, 9};
    Check(CE_NetSend(guest.get(), CE_NET_INPUT, 11, payload, sizeof(payload)) == 1,
          "direct input queues");
    CE_NetMessage message{}; uint8_t received[16]{};
    Check(Wait([&] {
        Poll(host, guest);
        return CE_NetReceive(host.get(), &message, received, sizeof(received)) == 1;
    }) && message.kind == CE_NET_INPUT && message.tick == 11 &&
        message.size == sizeof(payload) && !std::memcmp(payload, received, sizeof(payload)),
        "direct CE input payload survives");
#if defined(CE_NETWORK_ICE)
    char description[8] = "old";
    Check(!CE_NetIceReady(host.get()) && !CE_NetIceUsesRelay(host.get()) &&
          !CE_NetIceLocalDescription(host.get(), description, sizeof(description)) && !description[0],
          "direct session has no implicit ICE path");
#endif
}
void AuthenticationCancellation(Session &host, Session &guest, bool host_cancels, bool destroy) {
    Check(Wait([&] {
        // Each side still flushes its ENet acknowledgement. One event per poll
        // lets the caller cancel after connection, before CE authentication.
        CE_NetPoll(guest.get(), CE_NetNowMs(), 1);
        CE_NetPoll(host.get(), CE_NetNowMs(), 1);
        return Status(host).phase == CE_NET_AUTHENTICATING &&
               Status(guest).phase == CE_NET_AUTHENTICATING;
    }), "both peers reach the cancellable authentication phase");
    Session &cancelled = host_cancels ? host : guest;
    Session &remaining = host_cancels ? guest : host;
    Check(!Status(cancelled).epoch && !Status(remaining).epoch,
          "cancellation precedes authenticated admission");
    if (destroy) cancelled.reset();
    else {
        CE_NetClose(cancelled.get());
        Check(Status(cancelled).phase == CE_NET_CLOSED && Status(cancelled).error == CE_NET_OK &&
              !CE_NetSend(cancelled.get(), CE_NET_CONTROL, 1, nullptr, 0),
              "authentication cancellation closes locally and blocks application sends");
    }
    Check(Wait([&] {
        CE_NetPoll(remaining.get(), CE_NetNowMs(), 64);
        return Status(remaining).phase == CE_NET_FAILED;
    }, 750) && Status(remaining).error == CE_NET_PEER_LEFT && !Status(remaining).epoch,
          "authentication cancellation reaches the peer before its handshake deadline");
    if (!destroy) {
        CE_NetClose(cancelled.get());
        CE_NetPoll(cancelled.get(), CE_NetNowMs(), 64);
        Check(Status(cancelled).phase == CE_NET_CLOSED,
              "repeated authentication cancellation stays closed");
    }
}
void DirectAuthenticationCancellation(bool host_cancels, bool destroy) {
    auto h = Config(CE_NET_HOST), g = Config(CE_NET_GUEST);
    h.handshake_ms = g.handshake_ms = 4000;
    Session host(CE_NetCreate(&h), CE_NetDestroy);
    Check(bool(host), "direct cancellation host creates");
    g.port = Status(host).port;
    Session guest(CE_NetCreate(&g), CE_NetDestroy);
    Check(bool(guest), "direct cancellation guest creates");
    AuthenticationCancellation(host, guest, host_cancels, destroy);
}

#if defined(CE_NETWORK_ICE)
struct Relay {
    juice_server_credentials_t credentials{"ce-session-fixture", "authored-session-value", 12};
    juice_server_t *server = nullptr;
    juice_turn_server_t turn{};
    juice_config_t config{};
    Relay() {
        juice_server_config_t input{};
        input.credentials = &credentials; input.credentials_count = 1;
        input.max_allocations = 12; input.max_peers = 12;
        input.bind_address = input.external_address = "127.0.0.1";
        input.realm = "ce-session-fixture";
        server = juice_server_create(&input);
        Check(server != nullptr, "authored loopback TURN server creates");
        turn = {"127.0.0.1", credentials.username, credentials.password, juice_server_get_port(server)};
        config.bind_address = "127.0.0.1";
        config.turn_servers = &turn; config.turn_servers_count = 1;
    }
    ~Relay() { juice_server_destroy(server); }
};
struct Pair {
    Session host{nullptr, CE_NetDestroy}, guest{nullptr, CE_NetDestroy};
    Pair(Relay &relay, bool wrong_secret = false, bool wrong_content = false, uint32_t handshake_ms = 400) {
        auto h = Config(CE_NET_HOST), g = Config(CE_NET_GUEST);
        h.handshake_ms = g.handshake_ms = handshake_ms;
        if (wrong_secret) g.secret[0] ^= 1;
        if (wrong_content) g.compatibility[0] ^= 1;
        // Direct endpoints are not required or used by the ICE constructor.
        h.ipv4 = g.ipv4 = nullptr;
        host.reset(CE_NetCreateIce(&h, &relay.config, 5000));
        guest.reset(CE_NetCreateIce(&g, &relay.config, 5000));
        Check(host && guest, "CE owns both relay sessions and fresh loopback endpoints");
        Check(Status(host).port && Status(guest).port && Status(host).port != Status(guest).port,
              "ICE sessions bind distinct ENet ports");
    }
    void Exchange() {
        std::array<char, JUICE_MAX_SDP_STRING_LEN> a{}, b{};
        Check(Wait([&] {
            return CE_NetIceLocalDescription(host.get(), a.data(), a.size()) == 1 &&
                   CE_NetIceLocalDescription(guest.get(), b.data(), b.size()) == 1;
        }), "complete relay descriptions become available");
        Check(!std::strstr(a.data(), " typ host") && !std::strstr(b.data(), " typ host"),
              "session API exports relay candidates only");
        char short_buffer[2] = {'x', 'x'};
        Check(CE_NetIceLocalDescription(host.get(), short_buffer, sizeof(short_buffer)) == -1 &&
              !short_buffer[0], "small description buffer is cleared and refused");
        Check(!CE_NetIceSetRemoteDescription(host.get(), nullptr, 1) &&
              !CE_NetIceSetRemoteDescription(host.get(), b.data(), 0) &&
              !CE_NetIceSetRemoteDescription(host.get(), b.data(), JUICE_MAX_SDP_STRING_LEN),
              "invalid description arguments are refused");
        Check(CE_NetIceSetRemoteDescription(host.get(), b.data(), std::strlen(b.data())) &&
              CE_NetIceSetRemoteDescription(guest.get(), a.data(), std::strlen(a.data())),
              "private fixture exchange accepts one intended peer");
        Check(!CE_NetIceSetRemoteDescription(host.get(), b.data(), std::strlen(b.data())),
              "remote peer replacement is refused");
        Check(Wait([&] { return CE_NetIceReady(host.get()) && CE_NetIceReady(guest.get()); }),
              "relay paths become ready before CE admission");
        Check(CE_NetIceUsesRelay(host.get()) && CE_NetIceUsesRelay(guest.get()),
              "selected CE session routes include TURN");
    }
};
struct Expected {
    uint32_t kind, tick;
    std::vector<uint8_t> bytes;
    bool received = false;
};
Expected Payload(uint32_t kind, uint32_t tick, unsigned size) {
    Expected item{kind, tick, std::vector<uint8_t>(size), false};
    for (unsigned i = 0; i != size; ++i) item.bytes[i] = uint8_t(i * 13 + tick);
    return item;
}
void Receive(const Session &session, std::vector<Expected> &expected) {
    std::array<uint8_t, CE_NET_MAX_MESSAGE> data{};
    CE_NetMessage message{};
    int result;
    while ((result = CE_NetReceive(session.get(), &message, data.data(), data.size())) == 1) {
        auto item = std::find_if(expected.begin(), expected.end(), [&](const Expected &entry) {
            return entry.kind == message.kind && entry.tick == message.tick;
        });
        Check(item != expected.end() && !item->received && message.epoch == Status(session).epoch &&
              message.size == item->bytes.size() &&
              !std::memcmp(data.data(), item->bytes.data(), message.size), "exact authenticated CE payload received once");
        item->received = true;
    }
    if (result != 0) Check(false, "receive buffer fits CE messages");
}
void Payloads(Pair &pair) {
    std::vector<Expected> to_guest = {Payload(CE_NET_CONTROL, 20, 30), Payload(CE_NET_STATE, 21, 4500),
        Payload(CE_NET_BASELINE, 22, 5000), Payload(CE_NET_LIFECYCLE, 23, 30),
        Payload(CE_NET_STATE_CONTROL, 24, CE_NET_STATE_CONTROL_BYTES), Payload(CE_NET_STATE_REPAIR, 25, 6000)};
    std::vector<Expected> to_host = {Payload(CE_NET_INPUT, 30, 500), Payload(CE_NET_CONTROL, 31, 30),
        Payload(CE_NET_STATE_CONTROL, 32, CE_NET_STATE_CONTROL_BYTES)};
    for (auto &item : to_guest) Check(CE_NetSend(pair.host.get(), item.kind, item.tick,
        item.bytes.data(), item.bytes.size()) == 1, "host application message queues through CE");
    for (auto &item : to_host) Check(CE_NetSend(pair.guest.get(), item.kind, item.tick,
        item.bytes.data(), item.bytes.size()) == 1, "guest application message queues through CE");
    Check(Wait([&] {
        Poll(pair.host, pair.guest); Receive(pair.host, to_host); Receive(pair.guest, to_guest);
        return std::all_of(to_host.begin(), to_host.end(), [](const Expected &v) { return v.received; }) &&
               std::all_of(to_guest.begin(), to_guest.end(), [](const Expected &v) { return v.received; });
    }), "all four CE channels and fragmented application messages cross TURN");
    Check(CE_NetBeginEpoch(pair.host.get(), 2) == 1, "real CE epoch barrier queues");
    Check(Wait([&] { return Active(pair.host, pair.guest) && Status(pair.host).epoch == 2 &&
                          Status(pair.guest).epoch == 2; }), "CE epoch acknowledgement crosses TURN");
}
void SessionLifecycle(Relay &relay) {
    Pair pair(relay);
    const auto until = CE_NetNowMs() + 500;
    Check(Wait([&] { Poll(pair.host, pair.guest); return CE_NetNowMs() >= until; }),
          "poll through more than the CE handshake interval before exchange");
    Check(Status(pair.host).phase == CE_NET_WAITING && Status(pair.guest).phase == CE_NET_CONNECTING,
          "CE handshake timeout has not started while ICE waits");
    pair.Exchange();
    Check(Status(pair.host).epoch == 0 && Status(pair.guest).epoch == 0 &&
          CE_NetSend(pair.guest.get(), CE_NET_INPUT, 1, nullptr, 0) == 0,
          "ICE connectivity alone never admits game messages");
    auto rogue_config = Config(CE_NET_GUEST); rogue_config.port = Status(pair.host).port;
    Session rogue(CE_NetCreate(&rogue_config), CE_NetDestroy);
    Check(bool(rogue), "unrelated local CE endpoint creates");
    Check(Wait([&] {
        Poll(pair.host, rogue);
        return Status(pair.host).rejected_packets != 0;
    }) && Status(pair.host).phase == CE_NET_WAITING && Status(rogue).phase != CE_NET_ACTIVE,
          "ICE host refuses ingress outside its own shuttle");
    rogue.reset();
    Check(Wait([&] { return Active(pair.host, pair.guest); }), "actual CE authenticated admission crosses TURN");
    Payloads(pair);
    CE_NetClose(pair.host.get());
    Check(Status(pair.host).phase == CE_NET_CLOSED && !CE_NetIceReady(pair.host.get()) &&
          CE_NetSend(pair.host.get(), CE_NET_CONTROL, 1, nullptr, 0) == 0, "local close ends admission");
    Check(Wait([&] {
        CE_NetPoll(pair.guest.get(), CE_NetNowMs(), 64);
        return Status(pair.guest).phase == CE_NET_CLOSED;
    }) && Status(pair.guest).error == CE_NET_PEER_LEFT, "CE authenticated leave crosses the shuttle");
    CE_NetClose(pair.host.get());
}
void Refusal(Relay &relay, bool wrong_secret) {
    Pair pair(relay, wrong_secret, !wrong_secret);
    pair.Exchange();
    Check(Wait([&] {
        Poll(pair.host, pair.guest);
        return Status(pair.host).phase == CE_NET_FAILED || Status(pair.guest).phase == CE_NET_FAILED;
    }), "CE admission rejects incompatible relay peer");
    const auto h = Status(pair.host), g = Status(pair.guest);
    Check(h.phase != CE_NET_ACTIVE && g.phase != CE_NET_ACTIVE,
          "ICE never bypasses invite or content identity");
    const uint32_t expected = wrong_secret ? CE_NET_AUTH : CE_NET_INCOMPATIBLE;
    Check(h.error == expected || g.error == expected, "CE keeps its existing admission refusal reason");
}
void CancelAndTimeout(Relay &relay) {
    auto config = Config(CE_NET_GUEST);
    Check(!CE_NetCreateIce(nullptr, &relay.config, 0) && !CE_NetCreateIce(&config, nullptr, 0) &&
          !CE_NetCreateIce(&config, &relay.config, 99) && !CE_NetCreateIce(&config, &relay.config, 180001),
          "invalid ICE creation and timeout are refused");
    Session pending(CE_NetCreateIce(&config, &relay.config, 1000), CE_NetDestroy);
    Check(bool(pending), "pending session creates");
    CE_NetClose(pending.get());
    char out[16] = "old";
    Check(Status(pending).phase == CE_NET_CLOSED && !CE_NetIceReady(pending.get()) &&
          !CE_NetIceLocalDescription(pending.get(), out, sizeof(out)) && !out[0],
          "cancel before exchange retires the bridge");
    pending.reset();
    juice_config_t no_relay{}; no_relay.bind_address = "127.0.0.1";
    Session absent(CE_NetCreateIce(&config, &no_relay, 100), CE_NetDestroy);
    Check(bool(absent), "missing-relay control creates without fallback");
    Check(Wait([&] { CE_NetPoll(absent.get(), CE_NetNowMs(), 64); return Status(absent).phase == CE_NET_FAILED; }) &&
          Status(absent).error == CE_NET_TIMEOUT && !CE_NetIceReady(absent.get()),
          "bounded ICE deadline fails closed when no relay exists");
    absent.reset();
    Pair cancelled(relay); cancelled.Exchange();
    CE_NetClose(cancelled.guest.get());
    Check(Status(cancelled.guest).phase == CE_NET_CLOSED && !CE_NetIceReady(cancelled.guest.get()),
          "cancel after ICE readiness still prevents CE admission");
}
void ReadyWithoutAdmission(Relay &relay) {
    Pair idle(relay); idle.Exchange();
    // The joining side deliberately never polls, so it never starts ENet.
    Check(Wait([&] {
        CE_NetPoll(idle.host.get(), CE_NetNowMs(), 64);
        return Status(idle.host).phase == CE_NET_FAILED;
    }) && Status(idle.host).error == CE_NET_AUTH && !CE_NetIceReady(idle.host.get()),
          "a ready relay cannot keep the host waiting forever without CE admission");
    Pair connecting(relay); connecting.Exchange();
    CE_NetPoll(connecting.guest.get(), CE_NetNowMs(), 64);
    CE_NetClose(connecting.guest.get());
    char out[16] = "old";
    Check(Status(connecting.guest).phase == CE_NET_CLOSED && !CE_NetIceReady(connecting.guest.get()) &&
          !CE_NetIceLocalDescription(connecting.guest.get(), out, sizeof(out)) && !out[0],
          "cancel during ENet connection retires the bridge before admission");
}
void RelayAuthenticationCancellation(Relay &relay, bool host_cancels, bool destroy) {
    Pair pair(relay, false, false, 4000);
    pair.Exchange();
    AuthenticationCancellation(pair.host, pair.guest, host_cancels, destroy);
    Check(!CE_NetIceReady(host_cancels ? pair.host.get() : pair.guest.get()),
          "cancelled authentication cannot retain an admitted relay path");
}
void ReconnectAfterAbort(Relay &relay) {
    {
        Pair pair(relay); pair.Exchange();
        Check(Wait([&] { return Active(pair.host, pair.guest); }), "fresh session admits after earlier cancellations");
        pair.host.reset();
        Check(Wait([&] {
            CE_NetPoll(pair.guest.get(), CE_NetNowMs(), 64);
            return Status(pair.guest).phase == CE_NET_FAILED;
        }) && (Status(pair.guest).error == CE_NET_PEER_LEFT || Status(pair.guest).error == CE_NET_TIMEOUT),
              "active destruction ends remote session without a graceful leave claim");
    }
    Pair fresh(relay); fresh.Exchange();
    Check(Wait([&] { return Active(fresh.host, fresh.guest); }), "new bridge and CE session reconnect after abort");
}
#endif
}
int main() {
    if (!FixtureGuard()) return 2;
#if defined(CE_NETWORK_ICE)
    juice_set_log_level(JUICE_LOG_LEVEL_NONE);
    // TURN allocations can outlive their clients. Keep each scenario's quota and
    // server lifetime separate, including the scenarios that exercise reconnect.
    { Relay relay; SessionLifecycle(relay); }
    { Relay relay; Refusal(relay, true); }
    { Relay relay; Refusal(relay, false); }
    { Relay relay; CancelAndTimeout(relay); }
    { Relay relay; ReadyWithoutAdmission(relay); }
    { Relay relay; RelayAuthenticationCancellation(relay, true, false); }
    { Relay relay; RelayAuthenticationCancellation(relay, false, false); }
    { Relay relay; RelayAuthenticationCancellation(relay, true, true); }
    { Relay relay; RelayAuthenticationCancellation(relay, false, true); }
    { Relay relay; ReconnectAfterAbort(relay); }
#endif
    DirectControl();
    DirectAuthenticationCancellation(true, false);
    DirectAuthenticationCancellation(false, false);
    DirectAuthenticationCancellation(true, true);
    DirectAuthenticationCancellation(false, true);
    std::cout << "PASS " << checks << " authored CE session checks\n";
}
