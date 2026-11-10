// SPDX-License-Identifier: GPL-3.0-only
// Consumer controls use production declarations with authored engine,
// credential and record-owner stand-ins. They establish no connection or
// HTTP, credential-parser, cryptographic, platform or player acceptance.
#if defined(CE_SIGNAL_FIXTURE_STATE)
#pragma once
#include "ice_signaling.h"
#include <string>
#include <vector>
struct SignalFixture {
    uint64_t monotonic = 100;
    uint64_t wall = 2000000000, pairExpiresAt = 0;
    uint64_t preflightUtc = 0, preflightMono = 0;
    CE_NetRole role = CE_NET_HOST;
    CeIceSignalRequest request{};
    std::string directory;
    CeIceSignalStatus status = CeIceSignalStatus::Admitted;
    int preflights = 0, accepts = 0, prepares = 0, closes = 0, cancels = 0;
    int directCreates = 0, pendingCreates = 0, destroyed = 0, polls = 0;
    uint32_t pendingTimeout = 0;
    int manualReads = 0, manualRequests = 0, parses = 0, configures = 0;
    int starts = 0, localWrites = 0, exports = 0, imports = 0, remotes = 0;
    int grantReads = 0, peerReads = 0, publishes = 0, acks = 0, importPending = 0;
    bool manualReady = false, grantAvailable = true, peerAvailable = false;
    bool grantTaken = false, peerTaken = false, imported = false;
    bool invalidGrant = false, invalidPeer = false, refuse = false;
    bool gameAuthorized = false, localPublished = false, useRelay = true;
    // What CE_NetIceProgress reports once the agent has started (the exchange's private trace reads it), how often
    // it was read, and whether the agent has no local description yet or refuses the peer's.
    uint32_t iceGathered = 0, iceRelays = 0, iceState = 0;
    int progressReads = 0;
    bool noLocalDescription = false, refuseRemote = false, preflightPending = false;
    size_t grantSize = 14, peerSize = 100;
    std::vector<std::string> events;
};
extern SignalFixture signalFixture;
#elif defined(CE_SIGNAL_FIXTURE_JUICE)
#pragma once
typedef struct juice_config { const char* bind_address; } juice_config_t;
#define JUICE_MAX_SDP_STRING_LEN 512
#elif defined(CE_SIGNAL_FIXTURE_CREDENTIALS)
#pragma once
#include "fixture_state.h"
#include "juice/juice.h"
#include <cstring>
#include <ctime>
class CeIceCredentials {
public:
    static constexpr uint32_t DefaultLifetimeSeconds = 7200;
    static constexpr uint32_t MaxLifetimeSeconds = 14400;
    enum class Mode { Cloudflare, AuthoredLoopback };
    enum class LoadState { Pending, Ready, Error };
    bool RequireDistinctDirectories(const char* a, const char* b)
    { return !a || !b || std::strcmp(a, b) != 0; }
    LoadState TryOpen(const char*, uint32_t, const uint8_t*, uint64_t now, Mode)
    { ++signalFixture.manualReads; expires = now + 7200;
      return signalFixture.manualReady ? LoadState::Ready : LoadState::Pending; }
    bool PrepareRequest(const char*, uint32_t, const uint8_t*, uint64_t, uint32_t, uint32_t)
    { ++signalFixture.manualRequests; return true; }
    bool Parse(const char* bytes, size_t size, uint32_t role, const uint8_t* context, uint64_t, Mode mode)
    {
        ++signalFixture.parses; signalFixture.events.emplace_back("parse");
        expires = signalFixture.request.grantExpiresAt;
        return !signalFixture.invalidGrant && mode == Mode::Cloudflare && size == 14 &&
            std::memcmp(bytes, "authored-grant!", 14) == 0 && role == signalFixture.request.role &&
            std::memcmp(context, signalFixture.request.context, CE_ICE_SIGNAL_CONTEXT_SIZE) == 0;
    }
    bool Configure(juice_config_t&, uint64_t now)
    { ++signalFixture.configures; return IsCurrent(now); }
    bool IsCurrent(uint64_t now) const { return now < expires; }
    uint64_t ExpiresAt() const { return expires; }
    const char* Error() const { return "Authored credential refusal."; }
private:
    uint64_t expires = 0;
};
#else
#include "ice_game.h"
#if !defined(CE_NETWORK_ICE)
#define CE_SIGNAL_DISABLED_BUILD
#define CE_NETWORK_ICE 1
#endif
#include "ice_exchange.h"
#include "ice_session.h"
#if defined(CE_SIGNAL_DISABLED_BUILD)
#undef CE_NETWORK_ICE
#endif
#include "fixture_state.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

SignalFixture signalFixture;
extern "C" std::time_t time(std::time_t* out) noexcept
{
    const std::time_t value = static_cast<std::time_t>(signalFixture.wall);
    if (out) *out = value;
    return value;
}
struct CE_NetSession {
    CE_NetStatus status{};
    CE_NetIceExchangeCallbacks callbacks{};
    void* owner = nullptr;
    bool started = false;
};
CE_NetSession* CE_NetCreate(const CE_NetConfig*)
{ ++signalFixture.directCreates; return new CE_NetSession; }
CE_NetSession* CE_NetCreateIcePending(const CE_NetConfig*, uint32_t timeout)
{ ++signalFixture.pendingCreates; signalFixture.pendingTimeout = timeout; return new CE_NetSession; }
void CE_NetDestroy(CE_NetSession* session)
{ if (!session) return; if (session->owner) session->callbacks.destroy(session->owner);
  ++signalFixture.destroyed; delete session; }
void CE_NetGetStatus(const CE_NetSession* session, CE_NetStatus* out)
{ *out = session->status; out->phase = CE_NET_ACTIVE + 1; }
uint64_t CE_NetNowMs(void) { return signalFixture.monotonic; }
void CE_NetPoll(CE_NetSession* session)
{ signalFixture.monotonic += 1001;
  if (session->owner) session->status.error = session->callbacks.poll(session->owner, session, signalFixture.monotonic); }
int CE_NetIceStart(CE_NetSession* session, const juice_config_t*)
{ ++signalFixture.starts; session->started = true; return 1; }
int CE_NetIceAttachExchange(CE_NetSession* session, void* owner, const CE_NetIceExchangeCallbacks* callbacks)
{ session->owner = owner; session->callbacks = *callbacks; return 1; }
int CE_NetIceLocalDescription(const CE_NetSession* session, char* out, uint32_t capacity)
{ if (!session->started || capacity < 15 || signalFixture.noLocalDescription) return 0;
  std::memcpy(out, "authored-local", 15); return 1; }
int CE_NetIceSetRemoteDescription(CE_NetSession*, const char*, uint32_t)
{ ++signalFixture.remotes; return signalFixture.refuseRemote ? 0 : 1; }
int CE_NetIceUsesRelay(const CE_NetSession*) { return signalFixture.useRelay; }
int CE_NetIceProgress(const CE_NetSession* session, CE_NetIceProgressInfo* out)
{ *out = CE_NetIceProgressInfo{}; ++signalFixture.progressReads; if (!session->started) return 0;
  out->gathered = signalFixture.iceGathered; out->relay_candidates = signalFixture.iceRelays;
  out->state = signalFixture.iceState; return 1; }
const char* CE_NetIceProblem(const CE_NetSession* session)
{ return session && session->owner ? session->callbacks.error(session->owner) : nullptr; }

struct CeIceExchange::Impl { CE_NetRole role{}; std::array<uint8_t, CE_ICE_SIGNAL_CONTEXT_SIZE> context{}; };
CeIceExchange::CeIceExchange(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CeIceExchange::~CeIceExchange() = default;
std::unique_ptr<CeIceExchange> CeIceExchange::Open(const CE_NetConfig& config, const char*, uint64_t)
{ auto impl = std::make_unique<Impl>(); impl->role = static_cast<CE_NetRole>(config.role);
  impl->context.fill(config.secret[0]); return std::unique_ptr<CeIceExchange>(new CeIceExchange(std::move(impl))); }
CeIceExchangeResult CeIceExchange::PublishLocal(const char*, size_t, uint64_t)
{ ++signalFixture.localWrites; signalFixture.localPublished = true; return CeIceExchangeResult::Ready; }
CeIceExchangeResult CeIceExchange::ExportLocalRecord(uint8_t* bytes, size_t capacity, size_t* size, uint64_t)
{ ++signalFixture.exports; if (!signalFixture.localPublished) return CeIceExchangeResult::Pending;
  if (capacity < 100) return CeIceExchangeResult::Error;
  std::fill(bytes, bytes + 100, 7); *size = 100; return CeIceExchangeResult::Ready; }
CeIceExchangeResult CeIceExchange::ImportPeerRecord(const uint8_t* bytes, size_t size, uint64_t)
{ ++signalFixture.imports;
  if (signalFixture.invalidPeer || size != 100 || bytes[0] != 8) return CeIceExchangeResult::Error;
  if (signalFixture.importPending-- > 0) return CeIceExchangeResult::Pending;
  signalFixture.imported = true; signalFixture.events.emplace_back("import"); return CeIceExchangeResult::Ready; }
CeIceExchangeResult CeIceExchange::PollRemote(char* bytes, size_t capacity, size_t* size, uint64_t)
{ if (!signalFixture.imported) return CeIceExchangeResult::Pending;
  if (capacity < 14) return CeIceExchangeResult::Error;
  std::memcpy(bytes, "authored-peer", 14); *size = 13; return CeIceExchangeResult::Ready; }
const char* CeIceExchange::Error() const { return "Authored record refusal."; }
const uint8_t* CeIceExchange::Context() const { return impl_->context.data(); }
CE_NetRole CeIceExchange::Role() const { return impl_->role; }

class AuthoredStage final : public CeIceSignalStage {
public:
    bool Accept() noexcept override { ++signalFixture.accepts; return true; }
    void Poll(uint64_t, uint64_t) noexcept override { ++signalFixture.polls; }
    bool PublishLocal(const uint8_t* bytes, size_t size) noexcept override
    { ++signalFixture.publishes; return size == 100 && bytes[0] == 7; }
    CeIceSignalRead TakeOwnGrant(char* bytes, size_t capacity, size_t* size) noexcept override
    { ++signalFixture.grantReads;
      if (!signalFixture.grantAvailable || signalFixture.grantTaken) return CeIceSignalRead::Pending;
      signalFixture.grantTaken = true; *size = signalFixture.grantSize;
      if (*size <= capacity && *size == 14) std::memcpy(bytes, "authored-grant!", 14);
      return CeIceSignalRead::Ready; }
    CeIceSignalRead TakePeerRecord(uint8_t* bytes, size_t capacity, size_t* size,
                                   uint8_t digest[CE_ICE_SIGNAL_DIGEST_SIZE]) noexcept override
    { ++signalFixture.peerReads;
      if (!signalFixture.peerAvailable || signalFixture.peerTaken) return CeIceSignalRead::Pending;
      signalFixture.peerTaken = true; *size = signalFixture.peerSize;
      if (*size <= capacity) std::fill(bytes, bytes + *size, 8);
      std::fill(digest, digest + CE_ICE_SIGNAL_DIGEST_SIZE, 9); return CeIceSignalRead::Ready; }
    bool AcknowledgePeer(const uint8_t digest[CE_ICE_SIGNAL_DIGEST_SIZE]) noexcept override
    { ++signalFixture.acks; signalFixture.events.emplace_back("ack");
      return signalFixture.imported && digest[0] == 9; }
    CeIceSignalView View() const noexcept override
    { CeIceSignalView view; view.status = signalFixture.status; view.role = signalFixture.role;
      view.pairExpiresAt = signalFixture.pairExpiresAt; return view; }
    void CancelLocal() noexcept override { ++signalFixture.cancels; }
};
class AuthoredProvider final : public CeIceSignalProvider {
public:
    // The first pump binds the pair, which then needs consent. Like the real provider's idle pump, later pumps change
    // no status: never an implicit consent, never a lost admission.
    void Preflight(uint64_t utc, uint64_t mono) noexcept override
    { ++signalFixture.preflights; signalFixture.preflightUtc = utc; signalFixture.preflightMono = mono;
      if (signalFixture.status == CeIceSignalStatus::Unavailable) signalFixture.status = CeIceSignalStatus::NeedsConsent; }
    CeIceSignalView View() const noexcept override
    { CeIceSignalView view; view.status = signalFixture.status; view.role = signalFixture.role;
      view.pairExpiresAt = signalFixture.pairExpiresAt; return view; }
    bool Accept() noexcept override
    { ++signalFixture.accepts; signalFixture.status = CeIceSignalStatus::Admitted; return true; }
    std::unique_ptr<CeIceSignalStage> Prepare(const CeIceSignalRequest& request) noexcept override
    { ++signalFixture.prepares; signalFixture.request = request; signalFixture.directory = request.directory;
      if (signalFixture.refuse || (request.stage == CeIceSignalStageKind::Game && !signalFixture.gameAuthorized)) return nullptr;
      return std::unique_ptr<CeIceSignalStage>(new AuthoredStage); }
    void ClosePair() noexcept override
    { ++signalFixture.closes; signalFixture.status = CeIceSignalStatus::Closed; }
    bool PreflightPending() const noexcept override { return signalFixture.preflightPending; }
};
static int assertions = 0, failures = 0;
static void Check(bool condition, const char* label)
{ ++assertions; if (!condition) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); } }
static std::string lobbyDirectory, gameDirectory;
static void Reset(bool relay = true)
{ Check(CE_NetSelectSignaling({}) || CE_NetSignalingView().status == CeIceSignalStatus::Unavailable, "owner released before reset");
  signalFixture = SignalFixture{};
  signalFixture.pairExpiresAt = signalFixture.wall - 30 + 7200;
  if (relay) { setenv("BEA_NET_ICE_DIRECTORY", lobbyDirectory.c_str(), 1); setenv("BEA_NET_ICE_GAME_DIRECTORY", gameDirectory.c_str(), 1); }
  else { unsetenv("BEA_NET_ICE_DIRECTORY"); unsetenv("BEA_NET_ICE_GAME_DIRECTORY"); }
  unsetenv("BEA_NET_ICE_FIXTURE"); unsetenv("BEA_NET_ICE_LIFETIME_SECONDS"); }
static CE_NetConfig Config(CE_NetRole role = CE_NET_HOST)
{ CE_NetConfig config{}; config.role = role; config.secret[0] = 42; return config; }
static CE_NetSession* Native(CE_NetRole role = CE_NET_HOST)
{ Check(CE_NetSelectSignaling(std::make_shared<AuthoredProvider>()), "explicit provider selection");
  signalFixture.role = role; CE_NetConfig config = Config(role); return CE_NetCreateLobby(&config); }
static void Poll(CE_NetSession* session) { if (session) CE_NetPoll(session); }
static CeIceSignalGamePlan GamePlan()
{
    CeIceSignalGamePlan plan;
    plan.role = CE_NET_HOST;
    std::fill(plan.context, plan.context + CE_ICE_SIGNAL_CONTEXT_SIZE, 42);
    plan.requestedAt = static_cast<uint64_t>(std::time(nullptr)) - 10;
    plan.requestExpiresAt = plan.requestedAt + 120;
    plan.grantExpiresAt = plan.requestedAt + 7200;
    plan.setupDeadlineMs = signalFixture.monotonic + 45000;
    return plan;
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    lobbyDirectory = std::string(argv[1]) + "/lobby";
    gameDirectory = std::string(argv[1]) + "/game";
    Reset(false);
    CE_NetConfig config = Config();
#if !defined(CE_NETWORK_ICE)
    Check(!CE_NetSelectSignaling(std::make_shared<AuthoredProvider>()), "disabled build rejects provider");
    CE_NetSignalingPreflight(1, 1);
    Check(!CE_NetSignalingAccept(), "disabled build cannot consent");
    Check(CE_NetSignalingView().status == CeIceSignalStatus::Unavailable, "disabled view unavailable");
    CE_NetSignalingClosePair();
    Check(!signalFixture.preflights && !signalFixture.accepts && !signalFixture.closes, "disabled build sends no work");
    CE_NetSession* direct = CE_NetCreateLobby(&config);
    Check(direct && signalFixture.directCreates == 1, "direct reference route preserved");
    CE_NetDestroy(direct);
#else
    auto provider = std::make_shared<AuthoredProvider>();
    Check(CE_NetSelectSignaling(provider), "selection accepted");
    Check(!signalFixture.preflights && !signalFixture.prepares, "selection alone does no work");
    CE_NetSignalingPreflight(1, 1);
    Check(!signalFixture.preflights, "direct transport cannot preflight");
    Check(!CE_NetCreateLobby(&config) && !signalFixture.directCreates, "native selection never falls back to direct");
    Reset(); signalFixture.status = CeIceSignalStatus::Unavailable;   // a fresh provider: no pair bound yet
    Check(CE_NetSelectSignaling(provider), "preflight provider selected");
    CE_NetSignalingPreflight(10, 10);
    Check(signalFixture.preflights == 1 && !signalFixture.prepares, "preflight creates no stage request");
    Check(CE_NetSignalingView().status == CeIceSignalStatus::NeedsConsent, "safe consent view");
    Check(CE_NetSignalingAccept() && signalFixture.accepts == 1, "consent is explicit");
    CE_NetSession* session = CE_NetCreateLobby(&config);
    Check(session && signalFixture.prepares == 1 && !signalFixture.starts, "native stage starts pending");
    Check(signalFixture.preflights == 2 && signalFixture.preflightUtc == signalFixture.request.requestedAt &&
          signalFixture.preflightMono + 120000 == signalFixture.request.setupDeadlineMs,
          "lobby pumps the admitted pair once with the readings its fixed request is stamped from");
    Check(!signalFixture.manualReads && !signalFixture.manualRequests, "native stage ignores manual grants");
    Check(signalFixture.request.stage == CeIceSignalStageKind::Lobby && signalFixture.request.role == CE_NET_HOST &&
          signalFixture.request.context[0] == 42 && signalFixture.directory == lobbyDirectory, "fixed binding forwarded");
    Check(signalFixture.request.requestExpiresAt - signalFixture.request.requestedAt == 120 &&
          signalFixture.request.grantExpiresAt == signalFixture.pairExpiresAt &&
          signalFixture.request.setupDeadlineMs == 120100, "immutable bounded deadlines forwarded");
    Check(!CE_NetSelectSignaling({}), "active owner prevents provider switch");
    CE_NetSignalingPreflight(11, 11);
    Check(signalFixture.preflights == 2, "active stage owns pumping");
    Poll(session); Poll(session);
    Check(signalFixture.parses == 1 && signalFixture.configures == 1 && signalFixture.starts == 1, "grant passes existing owner validator");
    Check(signalFixture.localWrites == 1 && signalFixture.publishes == 1, "local record published once");
    signalFixture.peerAvailable = true; signalFixture.status = CeIceSignalStatus::ServiceDeliveryComplete;
    Poll(session);
    Check(signalFixture.imports == 1 && signalFixture.acks == 1 && signalFixture.remotes == 1, "authenticated record reaches ICE");
    CE_NetStatus status{}; CE_NetGetStatus(session, &status);
    Check(status.phase != CE_NET_ACTIVE, "service delivery is not ICE readiness");
    CE_NetDestroy(session);
    Check(signalFixture.cancels == 1 && !signalFixture.closes, "destruction preserves pair for START");
    CE_NetSignalingClosePair(); Check(signalFixture.closes == 1, "terminal close is explicit");

    Reset(); session = Native(CE_NET_GUEST); Poll(session);
    Check(session && !signalFixture.grantReads && !signalFixture.starts, "guest waits for host offer before grant");
    signalFixture.peerAvailable = true; signalFixture.importPending = 2;
    Poll(session); Poll(session);
    Check(signalFixture.peerReads == 2 && !signalFixture.acks && !signalFixture.grantReads,
          "pending import retains record without early grant or ACK");
    Poll(session); Poll(session);
    Check(signalFixture.imports == 3 && signalFixture.acks == 1 && signalFixture.parses == 1 && signalFixture.starts == 1,
          "guest resumes after authenticated import");
    Check(signalFixture.publishes == 1, "guest answer published");
    Check(signalFixture.events.size() >= 3 && signalFixture.events[0] == "import" && signalFixture.events[1] == "ack" &&
          signalFixture.events[2] == "parse", "guest import ACK grant ordering");
    CE_NetDestroy(session);

    Reset(); signalFixture.peerAvailable = true; signalFixture.invalidPeer = true;
    session = Native(CE_NET_GUEST); Poll(session);
    Check(session && session->status.error != CE_NET_OK && !signalFixture.acks && !signalFixture.grantReads,
          "bad peer record blocks ACK and guest grant"); CE_NetDestroy(session);
    Reset(); signalFixture.invalidGrant = true; session = Native(); Poll(session);
    Check(session && session->status.error != CE_NET_OK && signalFixture.parses == 1 && !signalFixture.starts,
          "credential refusal blocks ICE start"); CE_NetDestroy(session);
    for (bool peer : {false, true}) for (size_t invalid : {size_t(0),
            (peer ? CE_ICE_SIGNAL_RECORD_MAX : CE_ICE_SIGNAL_GRANT_MAX) + 1}) {
        Reset(); signalFixture.peerAvailable = peer;
        if (peer) signalFixture.peerSize = invalid; else signalFixture.grantSize = invalid;
        session = Native(peer ? CE_NET_GUEST : CE_NET_HOST); Poll(session);
        Check(session && session->status.error != CE_NET_OK && !signalFixture.acks && !signalFixture.starts,
              "invalid bounded event size refused"); CE_NetDestroy(session);
    }
    Reset(); signalFixture.refuse = true; session = Native();
    Check(!session && !signalFixture.directCreates && !signalFixture.manualReads && !signalFixture.manualRequests,
          "provider refusal has no fallback");
    Reset(); session = Native(); signalFixture.status = CeIceSignalStatus::IssuanceUnknown;
    Poll(session); Poll(session);
    Check(session && session->status.error != CE_NET_OK && signalFixture.prepares == 1 &&
          !signalFixture.grantReads && !signalFixture.starts && !signalFixture.manualReads,
          "unknown issuance fails without remint or fallback"); CE_NetDestroy(session);
    Reset();
    const uint64_t preparedPairEnd = signalFixture.pairExpiresAt;
    signalFixture.wall += 45;
    session = Native();
    Check(session && signalFixture.request.requestedAt == signalFixture.wall &&
          signalFixture.request.grantExpiresAt == preparedPairEnd &&
          signalFixture.request.grantExpiresAt < signalFixture.request.requestedAt + 7200,
          "launch delay preserves prepared pair grant ceiling");
    CE_NetDestroy(session);
    for (int remaining : {-1, 0, 119}) {
        Reset(); signalFixture.pairExpiresAt = remaining < 0 ? signalFixture.wall - 1 : signalFixture.wall + remaining;
        session = Native();
        Check(!session && !signalFixture.prepares && !signalFixture.pendingCreates,
              "pair expiry must cover complete fixed setup interval before issuance");
    }
    Reset(); signalFixture.pairExpiresAt = signalFixture.wall + 120;
    session = Native();
    Check(session && signalFixture.request.grantExpiresAt == signalFixture.request.requestExpiresAt &&
          signalFixture.request.grantExpiresAt == signalFixture.pairExpiresAt,
          "pair ceiling at exact setup boundary remains valid");
    CE_NetDestroy(session);
    Reset(); Check(CE_NetSelectSignaling(provider), "unadmitted pair selected");
    signalFixture.status = CeIceSignalStatus::NeedsConsent;
    session = CE_NetCreateLobby(&config);
    Check(!session && !signalFixture.prepares, "pair requires explicit admission before request freeze");
    Check(!signalFixture.preflights, "lobby creation does not pump a pair that is not admitted");
    Reset(); Check(CE_NetSelectSignaling(provider), "wrong role pair selected");
    signalFixture.role = CE_NET_GUEST;
    session = CE_NetCreateLobby(&config);
    Check(!session && !signalFixture.prepares, "admitted role must match lobby role before request freeze");
    Reset(); Check(CE_NetSelectSignaling(provider), "game provider selected");
    session = CE_NetCreateGame(&config);
    Check(!session && !signalFixture.prepares, "missing Game plan refuses before provider issuance");
    CeIceSignalGamePlan plan = GamePlan();
    Check(CE_NetRestoreSignalingGamePlan(plan), "verified boot restores typed nonsecret plan");
    Check(!CE_NetRestoreSignalingGamePlan(plan), "restore is one-shot per selected provider");
    session = CE_NetCreateGame(&config);
    Check(!session && signalFixture.prepares == 1, "plan alone does not authorize provider Game request");
    session = CE_NetCreateGame(&config);
    Check(!session && signalFixture.prepares == 1, "failed Game attempt cannot remint with consumed plan");
    Reset(); Check(CE_NetSelectSignaling(provider), "authorized Game provider selected");
    signalFixture.gameAuthorized = true; plan = GamePlan();
    Check(CE_NetRestoreSignalingGamePlan(plan), "authorized Game plan restored");
    signalFixture.monotonic += 5000;
    setenv("BEA_NET_ICE_LIFETIME_SECONDS", "3600", 1);
    session = CE_NetCreateGame(&config);
    Check(session && signalFixture.request.stage == CeIceSignalStageKind::Game && signalFixture.directory == gameDirectory,
          "authorized Game uses fixed directory and stage");
    Check(signalFixture.request.requestedAt == plan.requestedAt && signalFixture.request.requestExpiresAt == plan.requestExpiresAt &&
          signalFixture.request.grantExpiresAt == plan.grantExpiresAt && signalFixture.request.setupDeadlineMs == plan.setupDeadlineMs,
          "restart and changed environment do not refresh approved Game fields");
    Check(signalFixture.pendingTimeout == 40000, "pending ICE uses remaining monotonic limit");
    Check(!signalFixture.preflights, "Game creation takes its fixed plan without pumping the provider");
    CE_NetDestroy(session);
    Check(!CE_NetRestoreSignalingGamePlan(plan), "destroying Game cannot reset plan restore gate");
    Reset(); Check(CE_NetSelectSignaling(provider), "mismatch Game provider selected");
    signalFixture.gameAuthorized = true; plan = GamePlan(); plan.context[0] = 43;
    Check(CE_NetRestoreSignalingGamePlan(plan), "boot plan keeps original fixed context");
    session = CE_NetCreateGame(&config);
    Check(!session && !signalFixture.prepares, "actual config context mismatch refuses before issuance");
    Reset(); Check(CE_NetSelectSignaling(provider), "expired Game provider selected");
    plan = GamePlan(); plan.requestExpiresAt = plan.requestedAt;
    Check(!CE_NetRestoreSignalingGamePlan(plan), "expired UTC plan refused");
    plan = GamePlan(); plan.setupDeadlineMs = signalFixture.monotonic;
    Check(!CE_NetRestoreSignalingGamePlan(plan), "expired monotonic plan refused");
    plan = GamePlan(); plan.setupDeadlineMs = signalFixture.monotonic + 120001;
    Check(!CE_NetRestoreSignalingGamePlan(plan), "extended setup plan refused");
    plan = GamePlan(); plan.grantExpiresAt = plan.requestedAt + 14401;
    Check(!CE_NetRestoreSignalingGamePlan(plan), "extended grant plan refused");
    Reset(); signalFixture.monotonic = std::numeric_limits<uint64_t>::max() - 1; session = Native();
    Check(!session && !signalFixture.prepares, "overflow refused before issuance");
    Reset(); signalFixture.manualReady = true; session = CE_NetCreateLobby(&config);
    Check(session && signalFixture.manualReads == 1 && !signalFixture.prepares && signalFixture.starts == 1,
          "unselected manual grant route preserved"); CE_NetDestroy(session);
    Reset(); session = CE_NetCreateLobby(&config);
    Check(session && signalFixture.manualRequests == 1 && !signalFixture.prepares,
          "unselected manual request route preserved"); CE_NetDestroy(session);
#endif
    CE_NetSelectSignaling({});
    std::printf("%d assertions, %d failures\n", assertions, failures);
    return failures ? 1 : 0;
}

#endif
