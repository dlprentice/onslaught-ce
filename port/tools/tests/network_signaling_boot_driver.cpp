// SPDX-License-Identifier: GPL-3.0-only
// Authored component journey: real lobby/consumer/authority control flow with fake transport and clocks.
#if defined(CE_SIGNAL_FIXTURE_STATE) || defined(CE_SIGNAL_FIXTURE_CREDENTIALS) || defined(CE_SIGNAL_FIXTURE_JUICE)
#include "signaling_consumer_fixture.inc"
#else
#include "network.h"
#define main ConsumerRegressionMain
#include "signaling_consumer_fixture.inc"
#undef main
#include "game/ce_network_boot_authority.h"
#include "game/ce_network_boot.h"
#include "platform/common/ce_netlobby.h"
#include <climits>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
unsigned bootChecks, bootFailures, diskReads, diskWrites;
constexpr uint32_t FixtureLevel = 41;
constexpr uint64_t FixtureWall = 1000, FixtureMono = 10000;
const char* deferredOutcome = "ready";
void Require(bool good, const char* label) {
    ++bootChecks;
    if (!good) { ++bootFailures; std::fprintf(stderr, "FAIL %s\n", label); }
}
}

// These adapters do not test cryptography, campaign admission, sockets, HTTP, or game code.
extern "C" void CE_CodeWipe(void* bytes, size_t size) {
    volatile uint8_t* cursor = static_cast<volatile uint8_t*>(bytes);
    while (size--) *cursor++ = 0;
}
extern "C" void CE_CodeStartSecret(const uint8_t lobby[32], const uint8_t nonce[16], uint8_t out[32]) {
    for (size_t i = 0; i < 32; ++i) out[i] = lobby[i] ^ nonce[i % 16];
}
extern "C" int64_t CE_HandoffNow() { return int64_t(signalFixture.wall); }
extern "C" int CE_HandoffCampaignSet(CE_Handoff*) { return 0; }
extern "C" int CE_BootCampaignAdmit(const CE_Handoff*) { return 1; }
extern "C" void CE_NetClose(CE_NetSession*) {}
extern "C" void CE_NetPoll(CE_NetSession* session, uint64_t, uint32_t) { CE_NetPoll(session); }


bool CeIceExchange::ContextForConfig(const CE_NetConfig* config, uint8_t context[32]) noexcept {
    if (!config || !context) return false;
    auto probe = Open(*config, "/authored-context-adapter", 1);
    if (!probe) return false;
    std::memcpy(context, probe->Context(), 32);
    return true;
}

#if defined(CE_NETWORK_SIGNALING)
class OwnedFixtureProvider final : public CeIceSignalProvider {
public:
    explicit OwnedFixtureProvider(std::shared_ptr<const CeIceSignalAuthority> authority, bool pending) : authority_(std::move(authority)), pending_(pending) {}
    void Preflight(uint64_t utc, uint64_t mono) noexcept override {
        delegate_.Preflight(utc, mono);
        if (pending_ && std::strcmp(deferredOutcome, "expiry") != 0 && ++polls_ >= 3) {
            pending_ = false;
            signalFixture.status = std::strcmp(deferredOutcome, "failure") == 0 ? CeIceSignalStatus::Unavailable : CeIceSignalStatus::Admitted;
        }
    }
    bool PreflightPending() const noexcept override { return pending_; }
    CeIceSignalView View() const noexcept override { return delegate_.View(); }
    bool Accept() noexcept override { return delegate_.Accept(); }
    std::unique_ptr<CeIceSignalStage> Prepare(const CeIceSignalRequest& request) noexcept override {
        if (pending_) return {};
        if (request.stage == CeIceSignalStageKind::Game && !authority_->AllowsGame(request)) return {};
        return delegate_.Prepare(request);
    }
    void ClosePair() noexcept override { delegate_.ClosePair(); }
private:
    std::shared_ptr<const CeIceSignalAuthority> authority_;
    AuthoredProvider delegate_;
    bool pending_;
    unsigned polls_ = 0;
};

std::shared_ptr<CeIceSignalProvider> CeCreateSignalingProvider(const char* origin,
    std::shared_ptr<const CeIceSignalAuthority> authority) noexcept {
    CeIceSignalPairBinding pair{};
    if (!origin || !authority || !authority->ExpectedPair(&pair)) return {};
    signalFixture.role = pair.role;
    signalFixture.pairExpiresAt = pair.expiresAt;
    signalFixture.gameAuthorized = true;
    const auto* restored = dynamic_cast<const CeBootSignalAuthority*>(authority.get());
    const bool pending = restored && restored->Started();
    if (pending) signalFixture.status = CeIceSignalStatus::Unavailable;
    return std::make_shared<OwnedFixtureProvider>(std::move(authority), pending);
}

#endif

struct Lobby {
    CE_NetSession* net = nullptr;
    bool host = false, campaign = false, startSent = false;
    bool ackQueued = false, connected = false, gotInfo = false, sentInfo = false;
    int state = CE_LOBBY_READY;
    uint32_t level = FixtureLevel, ip = 0;
    char bind[16]{};
    uint16_t port = 0;
    uint64_t key = 0;
    uint8_t secret[32]{}, nonce[16]{};
    CE_LobbyStatus st{};
} L;
enum { RESTART_NONE, RESTART_LAUNCH };
int gRestart;
uint32_t gStartLevel;
struct { uint32_t version, mission; } gContinuation{};
uint32_t gContinuationAttempt;
bool gLaunched, gOutcomeReady;
CE_Handoff gOutcome{};
char gReturnArgs[1024]{};
char gHandoffName[4096]{}, gHandoffReceipt[128]{};

void SetEnv(const char* name, const char* value) { if (value) setenv(name, value, 1); else unsetenv(name); }
bool OwnArgs(char* out, size_t size) { return std::snprintf(out, size, "%s", "-fixture") > 0; }
bool WriteOwnedHandoff(const char*, const CE_Handoff*) { ++diskWrites; return false; }
template<class... Args> bool ReadOwnedHandoff(Args...) { ++diskReads; return false; }
void ClearPendingHandoff() {}
bool Create() { return false; }
template<class... Args> const char* Dotted(Args...) { return "127.0.0.1"; }
template<class... Args> bool Send(Args...) { return true; }
template<class... Args> void Text(char* out, size_t size, const char*, Args...) { std::snprintf(out, size, "%s", "fixture"); }
void CaptureHandoffEnvironment() {}
template<class... Args> void ReturnToFrontEnd(Args...) { throw std::runtime_error("unexpected frontend return"); }
template<class... Args> bool ResumeCampaignFromOutcome(Args...) { return false; }
void Hex(const uint8_t* bytes, size_t size, char* out) {
    const char* alphabet = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) { out[2 * i] = alphabet[bytes[i] >> 4]; out[2 * i + 1] = alphabet[bytes[i] & 15]; }
    out[2 * size] = 0;
}

#define LOBBY_LOG "fixture: "
#include "production_lobby_core.inc"
#include "production_lobby_actions.inc"
#include "production_boot_gate.inc"

void ResetClock(bool child) {
    Reset();
    signalFixture.wall = FixtureWall + (child ? 1 : 0);
    signalFixture.monotonic = FixtureMono + (child ? 1000 : 0);
    signalFixture.status = child ? CeIceSignalStatus::Admitted : CeIceSignalStatus::NeedsConsent;
    unsetenv("BEA_NET_SECRET");
    unsetenv("BEA_NET_ICE_DIRECTORY");
    unsetenv("BEA_NET_ICE_GAME_DIRECTORY");
    unsetenv("BEA_NET_ICE_RELAY");
}

int ChildJourney(const char* directory, const char* outcome) {
    deferredOutcome = outcome;
    ResetClock(true);
    Boot boot;
    Require(gSignalSelected && gSignalBootRequired && gLaunched, "child consumes selected native launch");
    Require(diskReads == 0 && diskWrites == 0 && !std::getenv("BEA_NET_SECRET"), "native boot has no disk or environment secret");
    const bool nativeIntent = FixtureNativeBootEntry() == 2;
    Require(nativeIntent, "native child crosses actual boot role intent gate before compatibility binding");
    if (!nativeIntent) { std::printf("SIGNAL_BOOT_CHILD checks=%u failures=%u\n", bootChecks, bootFailures); return 1; }
    auto game = Config(); game.role = CE_NET_HOST;
    Require(CE_LobbySignalingBootPrepared(game.compatibility, FixtureLevel) == 1, "child binds actual boot and fixed game plan");
    Require(CE_NetGameIceRequested() && CE_NetSignalingDirectorySelected(), "child restores memory relay request and directory");
    Require(CE_LobbySignalingGameSecret(1, FixtureLevel, game.secret) == 1, "typed game secret reaches configuration");
    Require(CE_LobbySignalingGameSecret(2, FixtureLevel, game.secret) == -1, "wrong role cannot fall back to environment");
    Require(CE_LobbySignalingGameSecret(1, FixtureLevel, game.secret) == 1, "matching reader retains owned secret");
    L.net = CE_NetCreateGame(&game);
    Require(L.net && signalFixture.prepares == 0, "game creation waits for pending pair rebind without preparing");
    Require(signalFixture.accepts == 0 && signalFixture.manualReads == 0, "child pending rebind performs no implicit consent or manual fallback");
    if (std::strcmp(outcome, "expiry") == 0) {
        signalFixture.wall = FixtureWall + 120;
        signalFixture.monotonic = FixtureMono + 120000;
    }
    for (unsigned step = 0; step < 6 && !signalFixture.prepares; ++step) {
        ++signalFixture.wall; signalFixture.monotonic += 1000;
        Poll(L.net);
    }
    if (std::strcmp(outcome, "ready") == 0) {
        Require(L.net && signalFixture.prepares == 1, "child reaches actual game consumer preparation");
        Require(signalFixture.request.stage == CeIceSignalStageKind::Game && signalFixture.directory == directory,
            "game request uses exact authority directory");
        Require(signalFixture.request.requestedAt == FixtureWall && signalFixture.request.setupDeadlineMs == FixtureMono + 120000,
            "exec does not refresh original START deadlines");
    } else {
        Require(signalFixture.prepares == 0 && signalFixture.starts == 0 && signalFixture.manualReads == 0,
            "failed or expired pair rebind cannot prepare, start or fall back");
        const auto attempts = signalFixture.preflights;
        ++signalFixture.wall; signalFixture.monotonic += 1000; Poll(L.net);
        ++signalFixture.wall; signalFixture.monotonic += 1000; Poll(L.net);
        Require(signalFixture.preflights == attempts && signalFixture.accepts == 0,
            "terminal or expired rebind does not implicitly retry or accept");
    }
    CE_LobbyLeave();
    Require(signalFixture.closes == 1 && !CE_NetSignalingDirectorySelected(), "explicit leave closes pair and clears memory directory");
    Require(CE_LobbySignalingSelected() && CE_LobbySignalingGameSecret(1, FixtureLevel, game.secret) == -1,
        "closed native selection cannot silently become manual");
    std::printf("SIGNAL_BOOT_CHILD checks=%u failures=%u\n", bootChecks, bootFailures);
    return bootFailures ? 1 : 0;
}

#if defined(CE_NETWORK_SIGNALING)
int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--child") == 0) return ChildJourney(argv[2], argv[3]);
    if (argc != 2 && argc != 3) return 2;
    const char* outcome = argc == 3 ? argv[2] : "ready";
    ResetClock(false);
    CeIceSignalPairBinding pair{};
    std::memset(pair.approval, 'a', 32); pair.role = CE_NET_HOST; pair.expiresAt = FixtureWall + 7200;
    auto authority = CeBootSignalAuthority::Create("https://signaling.example.invalid", "fixture-token", 13, pair, argv[1]);
    Require(authority && CE_LobbySignalingSelect(authority), "initial selection installs native provider and owned directory");
    Require(signalFixture.preflights == 0 && signalFixture.prepares == 0, "selection sends nothing");
    Require(CE_NetGameIceRequested() && CE_NetSignalingDirectorySelected(), "native request does not require manual environment");
    Require(CE_LobbySignalingBeginPreflight() && CE_LobbySignalingAccept(), "disclosure and explicit pair acceptance are separate calls");
    signalFixture.status = CeIceSignalStatus::Admitted;
    auto lobby = Config(); lobby.role = CE_NET_HOST;
    L.net = CE_NetCreateLobby(&lobby);
    Require(L.net && signalFixture.prepares == 1 && signalFixture.request.stage == CeIceSignalStageKind::Lobby,
        "first selected provider reaches actual lobby consumer");
    Require(signalFixture.directory == argv[1], "lobby request uses exact authority directory");
    L.host = true; L.level = FixtureLevel; L.state = CE_LOBBY_READY;
    std::memcpy(L.secret, lobby.secret, 32); std::memset(L.nonce, 0x35, sizeof L.nonce);
    BeginRestart();
    Require(gRestart == RESTART_LAUNCH && L.state == CE_LOBBY_STARTING && bool(gSignalDescriptor),
        "authenticated START seals restart authority");
    Require(signalFixture.closes == 0 && signalFixture.cancels == 1, "normal lobby close cancels local stage while preserving pair");
    Require(diskWrites == 0 && !std::getenv("BEA_NET_SECRET"), "native START writes no secret handoff or environment");
    setenv("BEA_NET_SECRET", "stale-manual-fixture", 1);
    ScrubEnvironment("launch");
    Require(!std::getenv("BEA_NET_SECRET"), "restart scrubs stale manual secret before exec");
    const int descriptor = gSignalDescriptor.PrepareExec();
    Require(descriptor >= 3, "owned descriptor is explicitly inherited");
    if (descriptor >= 3) {
        const pid_t child = fork();
        if (child == 0) {
            char number[24]; std::snprintf(number, sizeof number, "%d", descriptor);
            setenv("BEA_NET_SIGNAL_AUTHORITY_FD", number, 1); setenv("BEA_NET_HANDOFF", "launch", 1);
            execl(argv[0], argv[0], "--child", argv[1], outcome, static_cast<char*>(nullptr));
            _exit(127);
        }
        int status = 0;
        Require(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "exec child completes the expected native setup outcome");
    }
    gSignalDescriptor.Reset();
    CE_LobbyLeave();
    Require(signalFixture.closes == 1, "parent explicit leave is terminal");
    Require(!CE_LobbySignalingSelect(authority) && CE_LobbySignalingView().status == CeIceSignalStatus::Closed,
        "closed selection requires explicit clear before replacement");
    CE_LobbySignalingClear();
    Require(!CE_LobbySignalingSelected() && !CE_NetSignalingDirectorySelected(), "explicit clear restores manual selection");
    std::printf("SIGNAL_BOOT checks=%u failures=%u\n", bootChecks, bootFailures);
    return bootFailures ? 1 : 0;
}
#else
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    ResetClock(false);
    auto config = Config();
    Require(!CE_LobbySignalingSelected(), "ordinary ICE begins with manual selection");
    Require(CE_LobbySignalingBootPrepared(config.compatibility, FixtureLevel) == 1 &&
        CE_LobbySignalingGameSecret(1, FixtureLevel, config.secret) == 0, "ordinary manual boot remains available");
    auto* direct = CE_NetCreateGame(&config);
    Require(direct && signalFixture.directCreates == 1, "ordinary direct path remains available");
    CE_NetDestroy(direct);
    const std::string lobbyDirectory = std::string(argv[1]) + "/manual-lobby";
    const std::string gameDirectory = std::string(argv[1]) + "/manual-game";
    setenv("BEA_NET_ICE_DIRECTORY", lobbyDirectory.c_str(), 1);
    setenv("BEA_NET_ICE_GAME_DIRECTORY", gameDirectory.c_str(), 1);
    signalFixture.manualReady = true;
    auto* manual = CE_NetCreateGame(&config);
    Require(manual && signalFixture.pendingCreates > 0 && signalFixture.manualReads > 0,
        "ordinary manual ICE uses its existing grant route without native factory");
    CE_NetDestroy(manual);
    unsetenv("BEA_NET_ICE_DIRECTORY"); unsetenv("BEA_NET_ICE_GAME_DIRECTORY");
    CeIceSignalPairBinding pair{};
    std::memset(pair.approval, 'a', 32); pair.role = CE_NET_HOST; pair.expiresAt = FixtureWall + 7200;
    auto authority = CeBootSignalAuthority::Create("https://signaling.example.invalid", "fixture-token", 13, pair, argv[1]);
    Require(authority && !CE_LobbySignalingSelect(authority) && CE_LobbySignalingSelected(),
        "ordinary ICE refuses selected native signaling intent");
    Require(!CE_LobbySignalingBeginPreflight() && !CE_LobbySignalingAccept() && !CE_LobbySignalingRetry(),
        "unavailable native profile cannot contact or accept");
    Require(CE_LobbySignalingBootPrepared(config.compatibility, FixtureLevel) == 0 &&
        CE_LobbySignalingGameSecret(1, FixtureLevel, config.secret) == -1, "native intent cannot fall back to ordinary manual boot");
    Require(signalFixture.preflights == 0 && signalFixture.prepares == 0 && signalFixture.accepts == 0,
        "ordinary profile sends no native operation");
#if defined(CE_SIGNAL_FIXTURE_LEAK_FACTORY)
    auto unsupported = CeCreateSignalingProvider("https://signaling.example.invalid", authority);
    (void)unsupported;
#endif
    CE_LobbySignalingClear();
    Require(!CE_LobbySignalingSelected() && CE_LobbySignalingBootPrepared(config.compatibility, FixtureLevel) == 1,
        "explicit clear restores the ordinary manual path");
    std::printf("SIGNAL_BOOT checks=%u failures=%u\n", bootChecks, bootFailures);
    return bootFailures ? 1 : 0;
}
#endif
#endif
