// SPDX-License-Identifier: GPL-3.0-only
// Authored caller fixtures, not game-process or remote-player acceptance.
#include "../ice_game.h"
#include "../ice_session.h"
#include "fixture_guard.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
#include "../ice_bridge.h"
#include "../ice_exchange.h"
#include "../ice_credentials.h"
#if !defined(_WIN32)
#include <atomic>
#include <unistd.h>
namespace {
std::atomic<std::time_t> clockAdvance{0};
unsigned agentsCreated = 0;
bool discardClose = false, pollingRemote = false;
unsigned discarded = 0, drainCalls = 0;
CE_NetSession *remoteSession = nullptr;
}
extern "C" juice_agent_t *__real_juice_create(const juice_config_t *);
extern "C" juice_agent_t *__wrap_juice_create(const juice_config_t *config) {
    ++agentsCreated;
    return __real_juice_create(config);
}
extern "C" std::time_t __real_time(std::time_t *);
extern "C" std::time_t __wrap_time(std::time_t *out) {
    const auto now = __real_time(nullptr) + clockAdvance.load();
    if (out) *out = now;
    return now;
}
extern "C" int __real_juice_send(juice_agent_t *, const char *, size_t);
extern "C" int __wrap_juice_send(juice_agent_t *agent, const char *data, size_t size) {
    if (discardClose) { ++discarded; return JUICE_ERR_SUCCESS; }
    return __real_juice_send(agent, data, size);
}
extern "C" int __real_enet_host_service(ENetHost *, ENetEvent *, enet_uint32);
extern "C" int __wrap_enet_host_service(ENetHost *host, ENetEvent *event, enet_uint32 timeout) {
    if (timeout == 10 && remoteSession && !pollingRemote) {
        ++drainCalls; pollingRemote = true;
        CE_NetPoll(remoteSession, CE_NetNowMs(), 64);
        pollingRemote = false;
    }
    return __real_enet_host_service(host, event, timeout);
}
#endif
#endif

namespace {
unsigned checks = 0;
using Session = std::unique_ptr<CE_NetSession, decltype(&CE_NetDestroy)>;
namespace fs = std::filesystem;
void Check(bool ok, const char *message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void Env(const char *name, const char *value) {
#if defined(_WIN32)
    Check(_putenv_s(name, value ? value : "") == 0, "fixture environment changes");
#else
    Check((value ? setenv(name, value, 1) : unsetenv(name)) == 0, "fixture environment changes");
#endif
}
CE_NetConfig Config(uint32_t role, unsigned stage = 1) {
    CE_NetConfig config{};
    config.role = role; config.ipv4 = "127.0.0.1";
    config.handshake_ms = 1000; config.silence_ms = 3000;
    for (unsigned i = 0; i < 32; ++i) {
        config.secret[i] = uint8_t(i + stage);
        config.compatibility[i] = uint8_t(i * 7 + 3);
    }
    std::strcpy(config.version, "authored-game-fixture");
    return config;
}
CE_NetStatus Status(const Session &session) {
    CE_NetStatus status{}; CE_NetGetStatus(session.get(), &status); return status;
}
template<class Predicate> bool Wait(Predicate predicate, unsigned ms = 6000) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < until);
    return false;
}
void Poll(const Session &a, const Session &b) {
    CE_NetPoll(a.get(), CE_NetNowMs(), 64); CE_NetPoll(b.get(), CE_NetNowMs(), 64);
}
void DirectAndUnavailable(const fs::path &root) {
    Env("BEA_NET_ICE_DIRECTORY", nullptr); Env("BEA_NET_ICE_FIXTURE", nullptr);
    Env("BEA_NET_ICE_GAME_DIRECTORY", nullptr);
    Env("BEA_NET_ICE_LIFETIME_SECONDS", "invalid-direct-mode-fixture");
    Check(!CE_NetGameIceRequested(), "default caller is direct");
    auto h = Config(CE_NET_HOST), g = Config(CE_NET_GUEST);
    Session host(CE_NetCreateGame(&h), CE_NetDestroy);
    Check(bool(host), "default game caller creates a direct host");
    g.port = Status(host).port;
    Session guest(CE_NetCreateGame(&g), CE_NetDestroy);
    Check(bool(guest), "default game caller creates a direct guest");
    Check(Wait([&] { Poll(host, guest); return Status(host).phase == CE_NET_ACTIVE &&
                    Status(guest).phase == CE_NET_ACTIVE; }), "default game callers retain CE admission");
    guest.reset(); host.reset();
    Session lobby(CE_NetCreateLobby(&h), CE_NetDestroy);
    Check(bool(lobby), "default lobby factory remains direct");
    lobby.reset();
    Env("BEA_NET_ICE_LIFETIME_SECONDS", nullptr);
    Env("BEA_NET_ICE_DIRECTORY", root.string().c_str());
    Check(CE_NetGameIceRequested(), "explicit directory requests experimental relay");
    Session refused(CE_NetCreateGame(&h), CE_NetDestroy);
#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
    Check(refused && !CE_NetIceReady(refused.get()), "missing grant reserves only a pending session");
    CE_NetPoll(refused.get(), CE_NetNowMs() + 120001, 64);
    Check(Status(refused).phase == CE_NET_FAILED && Status(refused).error == CE_NET_TIMEOUT,
          "missing grant cannot outlive the preparation deadline");
#else
    Check(!refused && CE_NetGameProblem(nullptr)[0], "missing grant or disabled build refuses without direct fallback");
#endif
    Env("BEA_NET_ICE_DIRECTORY", nullptr);
}

#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
struct Relay {
    // The limit is deliberately much longer than simple example credentials.
    std::string username = std::string("authored-cloud-shaped-") + std::string(491, 'u');
    std::string password = std::string(513, 'p');
    juice_server_credentials_t credentials{};
    juice_server_t *server = nullptr;
    Relay() {
        Check(username.size() == 513 && password.size() == 513, "long fixture credentials have exact bounded lengths");
        credentials = {username.c_str(), password.c_str(), 12};
        juice_server_config_t config{};
        config.credentials = &credentials; config.credentials_count = 1;
        config.max_allocations = 12; config.max_peers = 12;
        config.bind_address = config.external_address = "127.0.0.1";
        config.realm = "authored-game-fixture";
        server = juice_server_create(&config);
        Check(server != nullptr, "authored loopback relay creates");
    }
    ~Relay() { if (server) juice_server_destroy(server); }
    unsigned Port() const { return juice_server_get_port(server); }
};
std::string Hex(const uint8_t *bytes) {
    const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned i = 0; i < 32; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
}
fs::path Directory(const fs::path &root, const char *name) {
    const auto path = root / name;
    Check(fs::create_directory(path), "new private fixture directory creates");
    fs::permissions(path, fs::perms::owner_all);
    return path;
}
void Grant(const fs::path &directory, unsigned role, const std::string &context, Relay &relay, unsigned port = 0, uint64_t expiry = 0) {
    const auto now = uint64_t(std::time(nullptr));
    const std::string roleName = role == CE_NET_HOST ? "host" : "guest";
    const auto path = directory / ("relay-" + roleName + ".json");
    std::ofstream file(path);
    file << "{\"schema\":1,\"role\":\"" << roleName << "\",\"context\":\"" << context
         << "\",\"issuedAt\":" << now << ",\"expiresAt\":" << (expiry ? expiry : now + 7200)
         << ",\"iceServers\":[{\"urls\":[\"turn:127.0.0.1:" << (port ? port : relay.Port())
         << "?transport=udp\"],\"username\":\"" << relay.username << "\",\"credential\":\"" << relay.password << "\"}]}";
    file.close(); Check(bool(file), "authored grant writes completely");
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write);
}
void Grants(const fs::path &directory, const CE_NetConfig &host, Relay &relay, unsigned port = 0) {
    const auto now = uint64_t(std::time(nullptr));
    auto context = CeIceExchange::Open(host, directory.string().c_str(), now);
    Check(bool(context), "fixture derives context through the real exchange API");
    for (unsigned role = CE_NET_HOST; role <= CE_NET_GUEST; ++role) Grant(directory, role, Hex(context->Context()), relay, port);
}
std::string Request(const fs::path &directory, unsigned role) {
    const auto path = directory / (role == CE_NET_HOST ? "relay-host.request.json" : "relay-guest.request.json");
    Check(fs::is_regular_file(path) && fs::file_size(path) <= 1024, "private preparation request is present and bounded");
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::string RequestContext(const std::string &request) {
    auto at = request.find("\"context\"");
    Check(at != std::string::npos, "preparation request names its opaque context");
    at = request.find(':', at); at = request.find('"', at);
    const auto end = request.find('"', at + 1);
    Check(end == at + 65, "request context has exactly 32 encoded bytes");
    const auto value = request.substr(at + 1, 64);
    Check(value.find_first_not_of("0123456789abcdef") == std::string::npos, "request context uses the canonical encoding");
    return value;
}
uint64_t RequestTime(const std::string &request, const char *field) {
    auto at = request.find(std::string("\"") + field + "\"");
    Check(at != std::string::npos, "request has the required bounded timestamp");
    at = request.find(':', at);
    return std::stoull(request.substr(at + 1));
}
uint64_t RequestGrantExpiry(const std::string &request) { return RequestTime(request, "grantExpiresAt"); }
struct Pair {
    Session host{nullptr, CE_NetDestroy}, guest{nullptr, CE_NetDestroy};
    Pair(const fs::path &directory, unsigned stage = 1) {
        Env("BEA_NET_ICE_DIRECTORY", directory.string().c_str());
        Env("BEA_NET_ICE_FIXTURE", "loopback");
        auto h = Config(CE_NET_HOST, stage), g = Config(CE_NET_GUEST, stage);
        // The relay route must ignore even absent direct addresses.
        h.ipv4 = g.ipv4 = nullptr;
        host.reset(CE_NetCreateGame(&h)); guest.reset(CE_NetCreateGame(&g));
        Check(host && guest, "actual game-facing caller owns two experimental sessions");
    }
    void Admit() {
        Check(Wait([&] { Poll(host, guest); return Status(host).phase == CE_NET_ACTIVE &&
                        Status(guest).phase == CE_NET_ACTIVE; }), "private offer/answer and grants admit both CE peers");
        Check(CE_NetIceUsesRelay(host.get()) && CE_NetIceUsesRelay(guest.get()),
              "both selected candidate pairs really use a relay");
        // The exchange's private trace reads the agent through CE_NetIceProgress: counts and indexes only.
        CE_NetIceProgressInfo hostProgress{}, guestProgress{}, none{7, 7, 7};
        Check(CE_NetIceProgress(host.get(), &hostProgress) == 1 && CE_NetIceProgress(guest.get(), &guestProgress) == 1 &&
              hostProgress.gathered == 1 && guestProgress.gathered == 1 &&
              hostProgress.relay_candidates >= 1 && guestProgress.relay_candidates >= 1 &&
              hostProgress.state >= 3 && hostProgress.state <= 4 && guestProgress.state >= 3 && guestProgress.state <= 4,
              "an admitted pair reports finished gathering, relay candidates and a connected agent");
        Check(CE_NetIceProgress(nullptr, &none) == 0 && !none.gathered && !none.relay_candidates && !none.state &&
              CE_NetIceProgress(host.get(), nullptr) == 0, "progress of no agent is zero");
        Poll(host, guest); // The next poll observes admission for the sanitized milestone.
    }
    void Traffic() {
        const uint8_t bytes[] = {11, 22, 33};
        Check(CE_NetSend(host.get(), CE_NET_CONTROL, 17, bytes, sizeof(bytes)) == 1 &&
              CE_NetSend(guest.get(), CE_NET_INPUT, 19, bytes, sizeof(bytes)) == 1,
              "game-facing peers queue application messages in both directions");
        bool h = false, g = false;
        auto read = [&](const Session &session, bool &received, uint32_t kind, uint32_t tick) {
            if (received) return;
            CE_NetMessage message{}; uint8_t out[8]{};
            if (CE_NetReceive(session.get(), &message, out, sizeof(out)) == 1) {
                Check(message.kind == kind && message.tick == tick && message.size == sizeof(bytes) &&
                      !std::memcmp(out, bytes, sizeof(bytes)), "exact CE application bytes reach their intended role");
                received = true;
            }
        };
        Check(Wait([&] { Poll(host, guest); read(host, h, CE_NET_INPUT, 19); read(guest, g, CE_NET_CONTROL, 17);
                        return h && g; }), "bidirectional application traffic crosses the selected relay");
        Poll(host, guest);
    }
    void Sustained() {
        unsigned sent = 0, hostReceived = 0, guestReceived = 0;
        bool active = true, valid = true;
        const uint8_t payload[] = {4, 8, 15};
        const auto end = CE_NetNowMs() + 2100;
        uint64_t nextSend = 0;
        auto receive = [&](const Session &session, unsigned &count, uint32_t kind) {
            CE_NetMessage message{}; uint8_t bytes[16]{}; int result;
            while ((result = CE_NetReceive(session.get(), &message, bytes, sizeof(bytes))) == 1) {
                valid &= message.kind == kind && message.tick == 1000 + count && message.size == sizeof(payload) &&
                         !std::memcmp(bytes, payload, sizeof(payload));
                ++count;
            }
            valid &= result == 0;
        };
        do {
            const auto now = CE_NetNowMs();
            if (now >= nextSend) {
                valid &= CE_NetSend(host.get(), CE_NET_CONTROL, 1000 + sent, payload, sizeof(payload)) == 1 &&
                         CE_NetSend(guest.get(), CE_NET_INPUT, 1000 + sent, payload, sizeof(payload)) == 1;
                ++sent; nextSend = now + 25;
            }
            Poll(host, guest);
            if (Status(host).phase != CE_NET_ACTIVE || Status(guest).phase != CE_NET_ACTIVE) { active = false; break; }
            receive(host, hostReceived, CE_NET_INPUT); receive(guest, guestReceived, CE_NET_CONTROL);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (CE_NetNowMs() < end);
        Check(active && CE_NetIceUsesRelay(host.get()) && CE_NetIceUsesRelay(guest.get()),
              "selected relay remains active throughout sustained bidirectional traffic");
        Check(Wait([&] {
            Poll(host, guest); receive(host, hostReceived, CE_NET_INPUT); receive(guest, guestReceived, CE_NET_CONTROL);
            return hostReceived == sent && guestReceived == sent;
        }, 500) && valid && sent >= 30, "sustained relay traffic retains ordered exact payloads in both directions");
    }
};
void Selectors(const fs::path &root) {
    auto config = Config(CE_NET_HOST, 7);
    const auto lobby = Directory(root, "selector-lobby"), game = Directory(root, "selector-game");
    Env("BEA_NET_ICE_DIRECTORY", lobby.string().c_str());
    Session incomplete(CE_NetCreateLobby(&config), CE_NetDestroy);
    Check(!incomplete && std::strstr(CE_NetGameProblem(nullptr), "separate"), "menu pilot refuses a missing game directory before lobby creation");
    Env("BEA_NET_ICE_GAME_DIRECTORY", lobby.string().c_str());
    Session same(CE_NetCreateLobby(&config), CE_NetDestroy);
    Check(!same && CE_NetGameProblem(nullptr)[0], "menu pilot refuses the same selected directory for both contexts");
    const auto beforeAliases = agentsCreated;
    Env("BEA_NET_ICE_GAME_DIRECTORY", (lobby.string() + "/").c_str());
    Session alias(CE_NetCreateLobby(&config), CE_NetDestroy);
    Check(!alias && CE_NetGameProblem(nullptr)[0] && !fs::exists(lobby / "relay-host.request.json"),
          "menu pilot refuses a trailing-slash alias before publication");
    Env("BEA_NET_ICE_GAME_DIRECTORY", (lobby.string() + "/./").c_str());
    Session dotted(CE_NetCreateLobby(&config), CE_NetDestroy);
    Check(!dotted && !fs::exists(lobby / "relay-host.request.json"), "dot aliases cannot share a preparation directory");
    const auto link = root / "selector-link";
    fs::create_directory_symlink(lobby, link);
    Env("BEA_NET_ICE_GAME_DIRECTORY", link.string().c_str());
    Session linked(CE_NetCreateLobby(&config), CE_NetDestroy);
    Check(!linked && !fs::exists(lobby / "relay-host.request.json"), "directory identity comparison retains the symlink refusal");
    fs::permissions(game, fs::perms::owner_all | fs::perms::group_read);
    Env("BEA_NET_ICE_GAME_DIRECTORY", game.string().c_str());
    Session publicGame(CE_NetCreateLobby(&config), CE_NetDestroy);
    Check(!publicGame && !fs::exists(lobby / "relay-host.request.json"), "unsafe game directory is refused before publishing a lobby request");
    fs::permissions(game, fs::perms::owner_all);
    Check(agentsCreated == beforeAliases, "rejected directory aliases and permissions create no ICE agent");
    Env("BEA_NET_ICE_GAME_DIRECTORY", game.string().c_str());
    const auto before = agentsCreated;
    Session first(CE_NetCreateLobby(&config), CE_NetDestroy);
    Check(bool(first) && !fs::exists(game / "relay-host.request.json"), "lobby preparation stays in its explicitly selected directory");
    const auto firstContext = RequestContext(Request(lobby, CE_NET_HOST));
    const auto lobbyRequest = Request(lobby, CE_NET_HOST);
    Check(RequestGrantExpiry(lobbyRequest) - RequestTime(lobbyRequest, "requestedAt") == 7200,
          "lobby requests the default two-hour lifetime");
    config = Config(CE_NET_HOST, 8);
    Session second(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(bool(second) && RequestContext(Request(game, CE_NET_HOST)) != firstContext,
          "fresh game identity prepares in the separate selected game directory");
    const auto gameRequest = Request(game, CE_NET_HOST);
    Check(RequestGrantExpiry(gameRequest) - RequestTime(gameRequest, "requestedAt") == 7200,
          "game requests a fresh default two-hour lifetime");
    Check(agentsCreated == before, "two directory selections create no ICE agents without grants");
    first.reset(); second.reset();
    Env("BEA_NET_ICE_DIRECTORY", nullptr);
    Check(CE_NetGameIceRequested(), "game-only selection still explicitly requests ICE");
    Session noLobby(CE_NetCreateLobby(&config), CE_NetDestroy);
    Check(!noLobby && CE_NetGameProblem(nullptr)[0], "game-only selection cannot silently create a direct lobby");
    Env("BEA_NET_ICE_GAME_DIRECTORY", nullptr);
}
void LifetimeSelections(const fs::path &root) {
    const auto before = agentsCreated;
    auto config = Config(CE_NET_HOST);
    for (const char *seconds : {"120", "600", "7200", "14400"}) {
        const auto directory = Directory(root, (std::string("lifetime-") + seconds).c_str());
        Env("BEA_NET_ICE_DIRECTORY", directory.string().c_str());
        Env("BEA_NET_ICE_LIFETIME_SECONDS", seconds);
        Session pending(CE_NetCreateGame(&config), CE_NetDestroy);
        Check(bool(pending), "bounded explicit lifetime creates a pending session");
        const auto request = Request(directory, CE_NET_HOST);
        Check(RequestGrantExpiry(request) - RequestTime(request, "requestedAt") == std::stoul(seconds),
              "explicit lifetime is requested rather than only permitted");
        Check(RequestTime(request, "requestExpiresAt") - RequestTime(request, "requestedAt") == 120,
              "long credential lifetime does not lengthen preparation");
        CE_NetPoll(pending.get(), CE_NetNowMs() + 120001, 64);
        Check(Status(pending).phase == CE_NET_FAILED && Status(pending).error == CE_NET_TIMEOUT,
              "all lifetime selections retain the two-minute preparation deadline");
    }
    unsigned index = 0;
    for (const char *seconds : {"", "0", "119", "14401", "07200", "+7200", "-7200", "7200 ", "7200x", "999999999999999999999"}) {
        const auto directory = Directory(root, (std::string("lifetime-invalid-") + std::to_string(index++)).c_str());
        Env("BEA_NET_ICE_DIRECTORY", directory.string().c_str());
        Env("BEA_NET_ICE_LIFETIME_SECONDS", seconds);
        Session refused(CE_NetCreateGame(&config), CE_NetDestroy);
        Check(!refused && std::strcmp(CE_NetGameProblem(nullptr), "invalid experimental relay lifetime selection") == 0 &&
              fs::is_empty(directory), "invalid lifetime fails before private publication with a fixed diagnostic");
    }
    Check(agentsCreated == before, "lifetime selection and refusal create no dependency agents");
    const auto staged = Directory(root, "lifetime-staged");
    Relay relay;
    Grants(staged, config, relay);
    Env("BEA_NET_ICE_DIRECTORY", staged.string().c_str());
    Env("BEA_NET_ICE_FIXTURE", "loopback");
    Env("BEA_NET_ICE_LIFETIME_SECONDS", "120");
    Session overBudget(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(!overBudget && std::strstr(CE_NetGameProblem(nullptr), "preparation lifetime") && agentsCreated == before,
          "staged grant cannot exceed the explicitly selected lifetime");
    Env("BEA_NET_ICE_LIFETIME_SECONDS", nullptr);
}
void Preparation(const fs::path &root) {
    Relay relay;
    const auto directory = Directory(root, "preparation");
    const auto before = agentsCreated;
    Pair pair(directory);
    const auto hostRequest = Request(directory, CE_NET_HOST), guestRequest = Request(directory, CE_NET_GUEST);
    Check(RequestGrantExpiry(hostRequest) - RequestTime(hostRequest, "requestedAt") == 7200 &&
          RequestGrantExpiry(guestRequest) - RequestTime(guestRequest, "requestedAt") == 7200,
          "both peers actually request a two-hour grant");
    const auto stamp = fs::last_write_time(directory / "relay-host.request.json");
    Check(agentsCreated == before && !CE_NetIceReady(pair.host.get()) && !CE_NetIceReady(pair.guest.get()),
          "no libjuice agent exists before matching grants arrive");
    const auto waitUntil = CE_NetNowMs() + 1100;
    Check(Wait([&] { Poll(pair.host, pair.guest); return CE_NetNowMs() >= waitUntil; }), "operator preparation outlasts the CE handshake interval");
    Check(Status(pair.host).phase == CE_NET_WAITING && Status(pair.guest).phase == CE_NET_CONNECTING && agentsCreated == before,
          "pending grants never begin CE admission or ICE gathering");
    const auto h = RequestContext(hostRequest), g = RequestContext(guestRequest);
    Check(h == g, "both roles request the same intended CE context");
    // The operator consumes only the request; it does not know the CE secret or compatibility digest.
    Grant(directory, CE_NET_HOST, h, relay, 0, RequestGrantExpiry(hostRequest));
    Grant(directory, CE_NET_GUEST, g, relay, 0, RequestGrantExpiry(guestRequest));
    Env("BEA_NET_ICE_LIFETIME_SECONDS", "120");
    Env("BEA_NET_ICE_DIRECTORY", (root / "unselected").string().c_str());
    pair.Admit(); pair.Traffic();
    Env("BEA_NET_ICE_LIFETIME_SECONDS", nullptr);
    Check(agentsCreated == before + 2, "exactly one ICE agent per retained pending session starts after grant arrival");
    Check(Request(directory, CE_NET_HOST) == hostRequest && Request(directory, CE_NET_GUEST) == guestRequest &&
          fs::last_write_time(directory / "relay-host.request.json") == stamp, "polling publishes each preparation request only once");
    const auto cancelled = Directory(root, "pending-cancel");
    Env("BEA_NET_ICE_DIRECTORY", cancelled.string().c_str());
    auto config = Config(CE_NET_HOST);
    const auto cancelledBefore = agentsCreated;
    Session pending(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(bool(pending), "pending cancellation fixture creates");
    const auto context = RequestContext(Request(cancelled, CE_NET_HOST));
    CE_NetClose(pending.get());
    Grant(cancelled, CE_NET_HOST, context, relay);
    CE_NetPoll(pending.get(), CE_NetNowMs(), 64);
    Check(Status(pending).phase == CE_NET_CLOSED && agentsCreated == cancelledBefore,
          "a late grant cannot restart a cancelled session");
    const auto expired = Directory(root, "pending-expired");
    Env("BEA_NET_ICE_DIRECTORY", expired.string().c_str());
    const auto expiredBefore = agentsCreated;
    Session deadline(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(bool(deadline), "pending deadline fixture creates");
    Grant(expired, CE_NET_HOST, RequestContext(Request(expired, CE_NET_HOST)), relay);
    CE_NetPoll(deadline.get(), CE_NetNowMs() + 120001, 64);
    Check(Status(deadline).phase == CE_NET_FAILED && Status(deadline).error == CE_NET_TIMEOUT && agentsCreated == expiredBefore,
          "grant arrival at the deadline cannot start a dependency agent");
    const auto invalid = Directory(root, "pending-invalid");
    Env("BEA_NET_ICE_DIRECTORY", invalid.string().c_str());
    Session malformed(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(bool(malformed), "pending invalid-grant fixture creates");
    auto wrong = RequestContext(Request(invalid, CE_NET_HOST)); wrong[0] = wrong[0] == '0' ? '1' : '0';
    Grant(invalid, CE_NET_HOST, wrong, relay);
    CE_NetPoll(malformed.get(), CE_NetNowMs(), 64);
    Check(Status(malformed).phase == CE_NET_FAILED && std::strstr(CE_NetGameProblem(malformed.get()), "context") && agentsCreated == expiredBefore,
          "a mismatched grant arriving later fails without creating ICE");
    const auto extended = Directory(root, "pending-extended-grant");
    Env("BEA_NET_ICE_DIRECTORY", extended.string().c_str());
    Session tooLong(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(bool(tooLong), "pending lifetime-cap fixture creates");
    const auto request = Request(extended, CE_NET_HOST);
    Env("BEA_NET_ICE_LIFETIME_SECONDS", "14400");
    clockAdvance = 1;
    Grant(extended, CE_NET_HOST, RequestContext(request), relay, 0, RequestGrantExpiry(request) + 1);
    CE_NetPoll(tooLong.get(), CE_NetNowMs(), 64);
    clockAdvance = 0;
    Env("BEA_NET_ICE_LIFETIME_SECONDS", nullptr);
    Check(Status(tooLong).phase == CE_NET_FAILED && std::strstr(CE_NetGameProblem(tooLong.get()), "preparation lifetime") && agentsCreated == expiredBefore,
          "a later grant cannot extend the preparation request's lifetime cap");
    const auto remaining = Directory(root, "pending-remaining-deadline");
    Env("BEA_NET_ICE_DIRECTORY", remaining.string().c_str());
    Session late(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(bool(late), "remaining preparation deadline fixture creates");
    const auto created = CE_NetNowMs();
    const auto remainingRequest = Request(remaining, CE_NET_HOST);
    Relay unavailable;
    const auto unavailablePort = unavailable.Port();
    juice_server_destroy(unavailable.server); unavailable.server = nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Grant(remaining, CE_NET_HOST, RequestContext(remainingRequest), unavailable, unavailablePort, RequestGrantExpiry(remainingRequest));
    CE_NetPoll(late.get(), CE_NetNowMs(), 64);
    Check(agentsCreated == expiredBefore + 1 && !CE_NetIceReady(late.get()), "late matching grant starts gathering but has no available relay");
    CE_NetPoll(late.get(), created + 120001, 64);
    Check(Status(late).phase == CE_NET_FAILED && Status(late).error == CE_NET_TIMEOUT,
          "late grant does not restart the original preparation deadline");
    pair.Sustained();
}
void Lifecycle(const fs::path &root) {
    Relay relay;
    const auto directory = Directory(root, "lifecycle");
    auto config = Config(CE_NET_HOST);
    Grants(directory, config, relay);
    {
        Pair pair(directory); pair.Admit(); pair.Traffic();
        Session stale(CE_NetCreateGame(&config), CE_NetDestroy);
        Check(!stale && CE_NetGameProblem(nullptr)[0], "same-context retry refuses previously published exchange");
#if !defined(_WIN32)
        // Only the test boundary advances wall time; game code reads the real clock.
        clockAdvance = 601;
        Poll(pair.host, pair.guest);
        Check(Status(pair.host).phase == CE_NET_ACTIVE && Status(pair.guest).phase == CE_NET_ACTIVE,
              "long grants remain active beyond the old ten-minute cap");
        pair.Traffic();
        clockAdvance = 7201;
        CE_NetPoll(pair.host.get(), CE_NetNowMs(), 64);
        // One message covers an expired grant and a clock that cannot be read (ice_game.cpp, "Clarify relay access and clock failure"): it names the relay access and the clock, and says what to do.
        Check(Status(pair.host).phase == CE_NET_FAILED && Status(pair.host).error == CE_NET_TIMEOUT &&
              std::strstr(CE_NetGameProblem(pair.host.get()), "relay access or local clock is invalid") &&
              std::strstr(CE_NetGameProblem(pair.host.get()), "relaunch both games"), "active session ends clearly when its grant expires");
        Check(!CE_NetIceReady(pair.host.get()), "expired credentials retire the live bridge");
        clockAdvance = 0;
#endif
    }
    config = Config(CE_NET_HOST, 2); Grants(directory, config, relay);
    Pair fresh(directory, 2); fresh.Admit(); fresh.Traffic();
#if !defined(_WIN32)
    const auto settle = CE_NetNowMs() + 30;
    Check(Wait([&] { Poll(fresh.host, fresh.guest); return CE_NetNowMs() >= settle; }), "fresh stage settles before close");
    discardClose = true; CE_NetClose(fresh.host.get()); discardClose = false;
    Check(discarded != 0, "initial close transmission is deliberately lost");
    remoteSession = fresh.guest.get(); fresh.host.reset(); remoteSession = nullptr;
    Check(Status(fresh.guest).phase == CE_NET_CLOSED && Status(fresh.guest).error == CE_NET_PEER_LEFT,
          "bounded close drain delivers authenticated leave after initial loss");
    Check(drainCalls > 0 && drainCalls <= 100, "close drain remains bounded");
#endif
}
void Refusal(const fs::path &root) {
    Relay relay;
    auto config = Config(CE_NET_HOST);
    const auto directory = Directory(root, "refusal"); Grants(directory, config, relay);
    Env("BEA_NET_ICE_DIRECTORY", directory.string().c_str()); Env("BEA_NET_ICE_FIXTURE", nullptr);
    Session wrongMode(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(!wrongMode && CE_NetGameProblem(nullptr)[0], "loopback grant is refused without the explicit fixture mode");
    Env("BEA_NET_ICE_FIXTURE", "invalid");
    Session invalidMode(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(!invalidMode && CE_NetGameProblem(nullptr)[0], "unknown fixture selection fails closed");
    Env("BEA_NET_ICE_FIXTURE", "loopback");
    config.secret[0] ^= 1;
    Session wrongContext(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(!wrongContext && std::strstr(CE_NetGameProblem(nullptr), "context"), "stale grants cannot authorize a fresh context");
    config = Config(CE_NET_HOST);
    Session waiting(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(bool(waiting), "host may wait for an explicit file exchange");
    CE_NetPoll(waiting.get(), CE_NetNowMs() + 120001, 64);
    Check(Status(waiting).phase == CE_NET_FAILED && Status(waiting).error == CE_NET_TIMEOUT &&
          std::strstr(CE_NetGameProblem(waiting.get()), "preparation timed out"), "missing peer exchange has an explicit preparation deadline");
    const auto noRelay = Directory(root, "no-relay");
    Grants(noRelay, config, relay);
    juice_server_destroy(relay.server); relay.server = nullptr;
    Env("BEA_NET_ICE_DIRECTORY", noRelay.string().c_str());
    Session unavailable(CE_NetCreateGame(&config), CE_NetDestroy);
    Check(bool(unavailable), "unavailable relay creates only a bounded pending session");
    CE_NetPoll(unavailable.get(), CE_NetNowMs() + 120001, 64);
    Check(Status(unavailable).phase == CE_NET_FAILED && Status(unavailable).error == CE_NET_TIMEOUT &&
          !CE_NetIceReady(unavailable.get()), "unavailable relay cannot hang preparation or fall back direct");
}
#if !defined(_WIN32)
int LogControl() {
    Check(enet_initialize() == 0, "logging fixture initializes ENet");
    FILE *capture = std::tmpfile(); Check(capture != nullptr, "private anonymous log capture opens");
    const int stdoutCopy = dup(1), stderrCopy = dup(2);
    Check(stdoutCopy >= 0 && stderrCopy >= 0, "log capture preserves output descriptors");
    std::fflush(nullptr); dup2(fileno(capture), 1); dup2(fileno(capture), 2);
    juice_set_log_level(JUICE_LOG_LEVEL_WARN);
    ENetAddress address{}; enet_address_set_host_ip(&address, "127.0.0.1");
    ENetHost *ha = enet_host_create(&address, 1, 2, 0, 0), *hb = enet_host_create(&address, 1, 2, 0, 0);
    juice_config_t config{}; config.bind_address = "127.0.0.1";
    auto a = CeIceBridge::Create(ha, config, false), b = CeIceBridge::Create(hb, config, false);
    if (!a || !b) return 2;
    std::string da, db;
    if (!Wait([&] { return a->LocalDescription(da) && b->LocalDescription(db); })) return 3;
    auto password = [](const std::string &s) {
        auto at = s.find("a=ice-pwd:"); if (at == std::string::npos) return std::string();
        at += 10; return s.substr(at, s.find_first_of("\r\n", at) - at);
    };
    const std::string pa = password(da), pb = password(db);
    if (pa.empty() || pb.empty()) return 4;
    const auto at = db.find("a=ice-pwd:") + 10;
    db.replace(at, db.find_first_of("\r\n", at) - at, "authored-invalid-local-ice-password");
    if (!a->SetRemoteDescription(db) || !b->SetRemoteDescription(da)) return 5;
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    a.reset(); b.reset(); enet_host_destroy(ha); enet_host_destroy(hb); enet_deinitialize();
    std::fflush(nullptr); std::fseek(capture, 0, SEEK_SET);
    std::string output; char buffer[4096]; size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), capture))) {
        output.append(buffer, count); if (output.size() > 65536) return 6;
    }
    dup2(stdoutCopy, 1); dup2(stderrCopy, 2); close(stdoutCopy); close(stderrCopy); std::fclose(capture);
    const bool leaked = output.find(pa) != std::string::npos || output.find(pb) != std::string::npos;
    std::cout << "authored_dependency_log_bytes=" << output.size() << " credential_exposed=" << (leaked ? "yes" : "no") << '\n';
    return leaked ? 1 : 0;
}
#endif
#endif
}

int main(int argc, char **argv) {
    if (!FixtureGuard() || argc != 2) return 2;
#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
    if (!std::strcmp(argv[1], "log-control")) return LogControl();
#endif
    const auto root = fs::absolute(argv[1]);
    Check(fs::create_directory(root), "fresh fixture root creates");
    fs::permissions(root, fs::perms::owner_all);
    DirectAndUnavailable(root);
#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
    Selectors(root); LifetimeSelections(root); Preparation(root); Lifecycle(root); Refusal(root);
#endif
    Env("BEA_NET_ICE_DIRECTORY", nullptr); Env("BEA_NET_ICE_FIXTURE", nullptr);
    Env("BEA_NET_ICE_GAME_DIRECTORY", nullptr);
    std::cout << "PASS " << checks << " authored game-facing caller checks\n";
}
