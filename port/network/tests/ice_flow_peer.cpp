// SPDX-License-Identifier: GPL-3.0-only
// Authored Linux process fixture. No game, retail data or account credentials.
#include "../ice_game.h"
#include "../ice_session.h"
#include "fixture_guard.h"
#include <juice/juice.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <poll.h>
#include <unistd.h>

extern "C" juice_agent_t *__real_juice_create(const juice_config_t *);
extern "C" juice_agent_t *__wrap_juice_create(const juice_config_t *config) {
    std::puts("AGENT_CREATED");
    return __real_juice_create(config);
}

namespace {
constexpr const char *kUser = "invented-ice-flow-user";
constexpr const char *kPassword = "invented-ice-flow-password";
constexpr uint16_t kDirectPort = 39079;
using Session = std::unique_ptr<CE_NetSession, decltype(&CE_NetDestroy)>;
void Require(bool okay) {
    if (!okay) { std::puts("FIXTURE_FAILED"); std::exit(1); }
}
std::string Command() {
    pollfd input{STDIN_FILENO, POLLIN, 0};
    if (poll(&input, 1, 0) <= 0 || !(input.revents & (POLLIN | POLLHUP))) return {};
    char bytes[32]{};
    const auto size = read(STDIN_FILENO, bytes, sizeof(bytes) - 1);
    if (size <= 0) return "stop";
    std::string value(bytes, size);
    if (!value.empty() && value.back() == '\n') value.pop_back();
    return value;
}
int Relay() {
    juice_set_log_level(JUICE_LOG_LEVEL_NONE);
    juice_server_credentials_t credentials{kUser, kPassword, 16};
    juice_server_config_t config{};
    config.bind_address = config.external_address = "127.0.0.1";
    config.port = 3478;
    config.realm = "invented-ice-flow";
    config.credentials = &credentials; config.credentials_count = 1;
    config.max_allocations = config.max_peers = 16;
    auto *server = juice_server_create(&config);
    Require(server && juice_server_get_port(server) == 3478);
    std::puts("RELAY_READY");
    const auto until = CE_NetNowMs() + 90000;
    while (CE_NetNowMs() < until && Command() != "stop")
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    juice_server_destroy(server);
    std::puts("RELAY_STOPPED");
    return 0;
}
CE_NetConfig Config(unsigned role, unsigned stage, bool mismatch) {
    CE_NetConfig config{};
    config.role = role; config.ipv4 = "127.0.0.1"; config.port = kDirectPort;
    config.handshake_ms = 1500; config.silence_ms = 4000;
    for (unsigned i = 0; i < 32; ++i) {
        config.secret[i] = uint8_t(i + stage + (mismatch ? 11 : 0));
        config.compatibility[i] = uint8_t(i * 7 + 3);
    }
    std::strcpy(config.version, "authored-flow-fixture");
    return config;
}
std::array<uint8_t, 16> Payload(unsigned role, unsigned stage) {
    std::array<uint8_t, 16> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i * 3 + role + stage);
    return bytes;
}
int Peer(int argc, char **argv) {
    Require(argc == 7);
    const std::string role = argv[1], stage = argv[2], mode = argv[5];
    Require((role == "host" || role == "guest") && (stage == "lobby" || stage == "game"));
    Require(mode == "ice" || mode == "context-mismatch" || mode == "direct" || mode == "trap");
    Require(std::string(argv[6]) == "authored-only");
    const bool direct = mode == "direct" || mode == "trap";
    const auto localRole = role == "host" ? CE_NET_HOST : CE_NET_GUEST;
    const unsigned phase = stage == "lobby" ? 1 : 2;
    auto config = Config(localRole, phase, mode == "context-mismatch");
    Require(unsetenv("BEA_NET_ICE_FIXTURE") == 0);
    Require(unsetenv("BEA_NET_ICE_LIFETIME_SECONDS") == 0);
    Require(unsetenv("BEA_NET_ICE_DIRECTORY") == 0);
    Require(unsetenv("BEA_NET_ICE_GAME_DIRECTORY") == 0);
    if (!direct) {
        Require(setenv("BEA_NET_ICE_DIRECTORY", argv[3], 1) == 0);
        Require(setenv("BEA_NET_ICE_GAME_DIRECTORY", argv[4], 1) == 0);
        Require(CE_NetGameIceRequested());
    }
    Session session(direct ? CE_NetCreate(&config) : stage == "lobby" ?
                    CE_NetCreateLobby(&config) : CE_NetCreateGame(&config), CE_NetDestroy);
    Require(bool(session));
    std::puts(direct ? "DIRECT_READY" : "PEER_READY");
    const auto start = CE_NetNowMs();
    uint64_t activeAt = 0, nextSend = 0;
    unsigned received = 0;
    bool active = false, traffic = false, terminal = false;
    const auto outgoing = Payload(localRole, phase);
    const auto incoming = Payload(localRole == CE_NET_HOST ? CE_NET_GUEST : CE_NET_HOST, phase);
    while (CE_NetNowMs() - start < 35000) {
        const auto command = Command();
        if (command == "stop") break;
        if (command == "close") CE_NetClose(session.get());
        const auto now = CE_NetNowMs();
        CE_NetPoll(session.get(), command == "expire" ? start + 120001 : now, 64);
        CE_NetStatus status{}; CE_NetGetStatus(session.get(), &status);
        if (status.phase == CE_NET_ACTIVE) {
            Require(direct || (CE_NetIceReady(session.get()) && CE_NetIceUsesRelay(session.get())));
            if (!active) {
                active = true; activeAt = now;
                std::puts(direct ? "ACTIVE_DIRECT" : "ACTIVE_RELAY");
            }
            if (mode != "trap" && now >= nextSend) {
                const auto kind = localRole == CE_NET_HOST ? CE_NET_CONTROL : CE_NET_INPUT;
                Require(CE_NetSend(session.get(), kind, 71, outgoing.data(), outgoing.size()) == 1);
                nextSend = now + 15;
            }
            CE_NetMessage message{}; uint8_t bytes[64]{};
            while (CE_NetReceive(session.get(), &message, bytes, sizeof(bytes)) == 1) {
                const auto kind = localRole == CE_NET_HOST ? CE_NET_INPUT : CE_NET_CONTROL;
                Require(message.kind == unsigned(kind) && message.tick == 71 && message.size == incoming.size());
                Require(std::memcmp(bytes, incoming.data(), incoming.size()) == 0);
                ++received;
            }
            if (!traffic && received >= 24 && now - activeAt >= 1000) {
                traffic = true; std::puts(direct ? "TRAFFIC_DIRECT" : "TRAFFIC_RELAY");
            }
        }
        if (!terminal && (status.phase == CE_NET_CLOSED || status.phase == CE_NET_FAILED)) {
            terminal = true;
            if (status.error == CE_NET_PEER_LEFT) std::puts("TERMINAL_PEER_LEFT");
            else if (status.error == CE_NET_TIMEOUT) std::puts("TERMINAL_TIMEOUT");
            else std::puts("TERMINAL_REFUSED");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    session.reset();
    std::puts("PEER_STOPPED");
    return 0;
}
}
int main(int argc, char **argv) {
    Require(FixtureGuard());
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 2 && std::string(argv[1]) == "relay") return Relay();
    return Peer(argc, argv);
}
