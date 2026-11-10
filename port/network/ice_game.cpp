// SPDX-License-Identifier: GPL-3.0-only
#if defined(CE_NETWORK_ICE)
#include "ice_game.h"
#include "ice_session.h"
#include "ice_exchange.h"
#include "ice_credentials.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace {
char createProblem[160]{};
std::shared_ptr<CeIceSignalProvider> selectedSignaling;
char selectedSignalingDirectory[4097] = {};
size_t exchangeOwners = 0;
CeIceSignalGamePlan restoredGamePlan;
bool gamePlanRestored = false, gamePlanAvailable = false;

const char* SignalingProblem(CeIceSignalStatus status)
{
    switch (status) {
    case CeIceSignalStatus::AuthenticationRequired:
    case CeIceSignalStatus::NotAdmitted:
        return "Relay signaling needs approved pair admission. Check the setup and retry.";
    case CeIceSignalStatus::IssuanceUnknown:
        return "Relay issuance could not be confirmed. Ask the operator to inspect this setup before retrying.";
    case CeIceSignalStatus::SetupExpired:
        return "Relay setup timed out. Return to the menu and prepare a fresh approved setup.";
    case CeIceSignalStatus::GrantExpired:
        return "Relay credentials expired. Return to the menu and prepare fresh credentials.";
    case CeIceSignalStatus::Closed:
    case CeIceSignalStatus::Expired:
        return "The approved pair ended. Return to the menu and obtain a fresh approved setup.";
    case CeIceSignalStatus::InvalidRequest:
    case CeIceSignalStatus::Unavailable:
        return "Relay signaling is unavailable for this setup. Return to the menu and check the setup.";
    case CeIceSignalStatus::RateLimited:
    case CeIceSignalStatus::NeedsConsent:
    case CeIceSignalStatus::AwaitingPairConsent:
    case CeIceSignalStatus::Admitted:
    case CeIceSignalStatus::AwaitingPeerRequest:
    case CeIceSignalStatus::AwaitingPeerRecord:
    case CeIceSignalStatus::Exchanging:
    case CeIceSignalStatus::ServiceDeliveryComplete:
    case CeIceSignalStatus::Issuing:
        return nullptr;
    }
    return "Relay signaling returned an unsupported state. Return to the menu and check the setup.";
}

struct SignalGrantBytes {
    std::array<char, CE_ICE_SIGNAL_GRANT_MAX> value{};
    ~SignalGrantBytes()
    {
        volatile char* bytes = value.data();
        for (size_t i = 0; i < value.size(); ++i) bytes[i] = 0;
    }
};

constexpr uint32_t kPreparationMs = 120000;

// Private trace of the native relay exchange, on stderr. Fixed labels and small integers only: never a record, grant,
// description, address, path or service text, and the only sentence is the exchange's own fixed problem text. A
// progress line is printed when its fields change (at most kTraceLines of them) and a beat line when nothing else was
// printed for kBeatMs (at most kBeats of them, one setup window), so a silent stall, a stage that is no longer polled
// and a stop each show up; one stopped line carries the problem sentence. Steps are the bits GameExchange::StepBits
// names; signaling is the CeIceSignalStatus index; gathered, relay and state come from CE_NetIceProgress.
constexpr uint32_t kTraceLines = 24, kBeats = 24;
constexpr uint64_t kBeatMs = 5000, kSampleMs = 100;
constexpr const char* kTraceStoppedFallback = "relay exchange failed";

bool CurrentGamePlan(const CeIceSignalGamePlan& plan, uint64_t now, uint64_t monotonic)
{
    if (plan.role != CE_NET_HOST && plan.role != CE_NET_GUEST) return false;
    if (plan.requestedAt > now || plan.requestExpiresAt <= now ||
        plan.grantExpiresAt <= now || plan.requestExpiresAt <= plan.requestedAt ||
        plan.grantExpiresAt < plan.requestExpiresAt) return false;
    const uint64_t setupSeconds = plan.requestExpiresAt - plan.requestedAt;
    const uint64_t grantSeconds = plan.grantExpiresAt - plan.requestedAt;
    return setupSeconds <= kPreparationMs / 1000 && grantSeconds >= kPreparationMs / 1000 &&
        grantSeconds <= CeIceCredentials::MaxLifetimeSeconds && plan.setupDeadlineMs > monotonic &&
        plan.setupDeadlineMs - monotonic <= kPreparationMs;
}

bool RequestedLifetime(uint32_t& seconds)
{
    seconds = CeIceCredentials::DefaultLifetimeSeconds;
    const char *text = std::getenv("BEA_NET_ICE_LIFETIME_SECONDS");
    if (!text) return true;
    if (*text < '1' || *text > '9') return false;
    seconds = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        seconds = seconds * 10 + uint32_t(*text - '0');
        if (seconds > CeIceCredentials::MaxLifetimeSeconds) return false;
    }
    return seconds >= kPreparationMs / 1000;
}
uint64_t UnixNow()
{
    const std::time_t now = std::time(nullptr);
    return now > 0 ? uint64_t(now) : 0;
}
struct GameExchange {
    std::unique_ptr<CeIceExchange> exchange;
    CeIceCredentials credentials;
    std::shared_ptr<CeIceSignalProvider> signalProvider;
    CeIceSignalRequest deferredSignalRequest{};
    std::string deferredSignalDirectory;
    bool deferredSignal = false;
    std::unique_ptr<CeIceSignalStage> signaling;
    std::array<uint8_t, CE_ICE_SIGNAL_RECORD_MAX> signalPeerRecord{};
    std::array<uint8_t, CE_ICE_SIGNAL_DIGEST_SIZE> signalPeerDigest{};
    size_t signalPeerSize = 0;
    bool signalPeerPending = false, signalPeerImported = false;
    bool signalPublished = false, registered = false;

    std::string directory;
    CeIceCredentials::Mode mode = CeIceCredentials::Mode::Cloudflare;
    const char *problem = "";
    uint64_t nextGrantCheck = 0;
    uint64_t grantLimit = 0;
    bool iceStarted = false;
    bool published = false, accepted = false, activeReported = false, trafficReported = false;

    // Trace state (Trace): when the exchange was created, the last sampled ICE progress and printed fields, and the
    // bounds on what is printed.
    uint64_t createdMs = 0, traceSampleAt = 0, traceBeatAt = 0;
    CE_NetIceProgressInfo traceIce{};
    uint32_t traceWord[5]{};
    uint32_t traceLines = 0, traceBeats = 0;
    bool grantParsed = false, traceWordKnown = false, traceStopped = false;

    ~GameExchange()
    {
        if (signaling) signaling->CancelLocal();
        if (registered) --exchangeOwners;
    }

    uint32_t PumpSignaling(uint64_t now, uint64_t monotonic)
    {
        if (!signaling) return 0;
        signaling->Poll(now, monotonic);
        if (const char* failure = SignalingProblem(signaling->View().status)) {
            problem = failure;
            return CE_NET_IO;
        }
        if (!signalPeerImported) {
            if (!signalPeerPending) {
                const CeIceSignalRead read = signaling->TakePeerRecord(signalPeerRecord.data(),
                    signalPeerRecord.size(), &signalPeerSize, signalPeerDigest.data());
                if (read == CeIceSignalRead::Failed) {
                    problem = "Relay signaling could not deliver the peer record. Return to the menu and check the setup.";
                    return CE_NET_IO;
                }
                if (read == CeIceSignalRead::Ready) {
                    if (!signalPeerSize || signalPeerSize > signalPeerRecord.size()) {
                        problem = "Relay signaling returned an invalid peer record. Return to the menu and check the setup.";
                        return CE_NET_IO;
                    }
                    signalPeerPending = true;
                }
            }
            if (signalPeerPending) {
                const CeIceExchangeResult imported = exchange->ImportPeerRecord(
                    signalPeerRecord.data(), signalPeerSize, now);
                if (imported == CeIceExchangeResult::Error) {
                    problem = exchange->Error();
                    return CE_NET_IO;
                }
                if (imported == CeIceExchangeResult::Ready) {
                    signalPeerImported = true;
                    if (!signaling->AcknowledgePeer(signalPeerDigest.data())) {
                        problem = "Relay signaling could not acknowledge the peer record. Return to the menu and check the setup.";
                        return CE_NET_IO;
                    }
                    signalPeerRecord.fill(0); signalPeerDigest.fill(0);
                    signalPeerSize = 0; signalPeerPending = false;
                }
            }
        }
        if (published && !signalPublished) {
            std::array<uint8_t, CE_ICE_SIGNAL_RECORD_MAX> record{};
            size_t size = 0;
            const CeIceExchangeResult exported = exchange->ExportLocalRecord(
                record.data(), record.size(), &size, now);
            if (exported == CeIceExchangeResult::Error) {
                problem = exchange->Error(); return CE_NET_IO;
            }
            if (exported == CeIceExchangeResult::Ready) {
                if (!size || size > record.size() || !signaling->PublishLocal(record.data(), size)) {
                    problem = "Relay signaling could not publish the local record. Return to the menu and check the setup.";
                    return CE_NET_IO;
                }
                signalPublished = true;
            }
        }
        return 0;
    }

    CeIceCredentials::LoadState ReadSignalGrant(uint64_t now)
    {
        if (!signaling) {
            const uint64_t monotonic = CE_NetNowMs();
            const auto& request = deferredSignalRequest;
            if (!signalProvider || !deferredSignal || now < request.requestedAt ||
                now >= request.requestExpiresAt || monotonic >= request.setupDeadlineMs) {
                deferredSignal = false;
                return CeIceCredentials::LoadState::Error;
            }
            signalProvider->Preflight(now, monotonic);
            if (signalProvider->PreflightPending()) return CeIceCredentials::LoadState::Pending;
            deferredSignal = false;
            const auto view = signalProvider->View();
            if (view.status != CeIceSignalStatus::Admitted || view.role != request.role ||
                view.pairExpiresAt < request.grantExpiresAt) return CeIceCredentials::LoadState::Error;
            signaling = signalProvider->Prepare(request);
            if (!signaling) return CeIceCredentials::LoadState::Error;
        }

        // Authenticate and import the host offer before consuming a guest's
        // own grant. Service delivery alone is insufficient.
        if (exchange->Role() == CE_NET_GUEST && !signalPeerImported)
            return CeIceCredentials::LoadState::Pending;
        SignalGrantBytes bytes;
        size_t size = 0;
        const CeIceSignalRead read = signaling->TakeOwnGrant(bytes.value.data(), bytes.value.size(), &size);
        if (read == CeIceSignalRead::Pending) return CeIceCredentials::LoadState::Pending;
        if (read != CeIceSignalRead::Ready || !size || size > bytes.value.size()) {
            problem = "Relay signaling could not deliver this player's credentials. Return to the menu and check the setup.";
            return CeIceCredentials::LoadState::Error;
        }
        if (!credentials.Parse(bytes.value.data(), size, exchange->Role(), exchange->Context(), now, mode)) {
            problem = credentials.Error(); return CeIceCredentials::LoadState::Error;
        }
        grantParsed = true;
        return CeIceCredentials::LoadState::Ready;
    }

    CeIceCredentials::LoadState ReadGrant(uint64_t now)
    {
        if (signalProvider) return ReadSignalGrant(now);
        const auto result = credentials.TryOpen(directory.c_str(), exchange->Role(), exchange->Context(), now, mode);
        if (result == CeIceCredentials::LoadState::Error) problem = credentials.Error();
        return result;
    }
    bool Start(CE_NetSession *session, uint64_t now)
    {
        if (grantLimit && credentials.ExpiresAt() > grantLimit) {
            problem = "relay grant exceeds the preparation lifetime";
            return false;
        }
        juice_config_t ice{};
        if (!credentials.Configure(ice, now)) { problem = credentials.Error(); return false; }
        if (mode == CeIceCredentials::Mode::AuthoredLoopback) ice.bind_address = "127.0.0.1";
        if (!CE_NetIceStart(session, &ice)) {
            problem = "experimental relay transport could not be started";
            return false;
        }
        iceStarted = true;
        return true;
    }
    // What the exchange has done so far, one bit per step: 01 the peer's record imported, 02 this player's own grant
    // taken and parsed, 04 the ICE agent started, 08 the peer's description accepted, 10 the local description
    // published to the private directory, 20 the local record handed to the signaling client.
    uint32_t StepBits() const
    {
        return (signalPeerImported ? 0x01u : 0u) | (grantParsed ? 0x02u : 0u) | (iceStarted ? 0x04u : 0u) |
               (accepted ? 0x08u : 0u) | (published ? 0x10u : 0u) | (signalPublished ? 0x20u : 0u);
    }

    void TraceLine(uint64_t monotonic, const uint32_t (&word)[5], uint32_t beat)
    {
        char label[24] = "";
        if (beat) std::snprintf(label, sizeof label, " beat=%u", unsigned(beat));
        std::fprintf(stderr, "onslaught-ce: relay exchange: %s%s step=%02x signaling=%u gathered=%u relay=%u state=%u after_ms=%llu\n",
                     exchange->Role() == CE_NET_HOST ? "host" : "guest", label, unsigned(word[0]), unsigned(word[1]),
                     unsigned(word[2]), unsigned(word[3]), unsigned(word[4]),
                     static_cast<unsigned long long>(monotonic > createdMs ? monotonic - createdMs : 0));
    }

    // Called after every Poll with its result. Only the native signaling route is traced.
    void Trace(CE_NetSession *session, uint64_t monotonic, uint32_t result)
    {
        if (!signalProvider) return;
        if (!traceBeatAt) traceBeatAt = monotonic + kBeatMs;
        if (iceStarted && (result != CE_NET_OK ||
                           (monotonic >= traceSampleAt && (traceLines < kTraceLines || traceBeats < kBeats)))) {
            CE_NetIceProgress(session, &traceIce);
            traceSampleAt = monotonic + kSampleMs;
        }
        const CeIceSignalView view = signaling ? signaling->View() : signalProvider ? signalProvider->View() : CeIceSignalView{};
        const uint32_t word[5] = {StepBits(), static_cast<uint32_t>(view.status), traceIce.gathered,
                                  traceIce.relay_candidates, traceIce.state};
        bool printed = false;
        if ((!traceWordKnown || std::memcmp(word, traceWord, sizeof word) != 0) && traceLines < kTraceLines) {
            TraceLine(monotonic, word, 0);
            std::memcpy(traceWord, word, sizeof word);
            traceWordKnown = true; ++traceLines; printed = true;
        } else if (monotonic >= traceBeatAt && traceBeats < kBeats) {
            TraceLine(monotonic, word, ++traceBeats);
            printed = true;
        }
        if (printed) traceBeatAt = monotonic + kBeatMs;
        if (result != CE_NET_OK && !traceStopped) {
            traceStopped = true;
            std::fprintf(stderr, "onslaught-ce: relay exchange: %s stopped: %s\n",
                         exchange->Role() == CE_NET_HOST ? "host" : "guest",
                         problem && problem[0] ? problem : kTraceStoppedFallback);
        }
    }

    uint32_t Poll(CE_NetSession *session, uint64_t monotonic)
    {
        const uint32_t result = PollOnce(session, monotonic);
        Trace(session, monotonic, result);
        return result;
    }

    uint32_t PollOnce(CE_NetSession *session, uint64_t monotonic)
    {
        const uint64_t now = UnixNow();
        const uint32_t signalResult = PumpSignaling(now, monotonic);
        if (signalResult) return signalResult;
        if (!iceStarted) {
            if (monotonic < nextGrantCheck) return CE_NET_OK;
            const auto result = ReadGrant(now);
            if (result == CeIceCredentials::LoadState::Error) return CE_NET_IO;
            if (result == CeIceCredentials::LoadState::Pending) {
                nextGrantCheck = monotonic + 100;
                return CE_NET_OK;
            }
            if (!Start(session, now)) return CE_NET_IO;
        }
        if (!now || !credentials.IsCurrent(now)) {
            problem = "relay access or local clock is invalid; relaunch both games with fresh private lobby and game folders and repeat the private code and file exchange";
            return CE_NET_TIMEOUT;
        }
        std::array<char, JUICE_MAX_SDP_STRING_LEN> description{};
        if (!accepted) {
            size_t size = 0;
            const auto result = exchange->PollRemote(description.data(), description.size(), &size, now);
            if (result == CeIceExchangeResult::Error) { problem = exchange->Error(); return CE_NET_IO; }
            if (result == CeIceExchangeResult::Ready) {
                if (!CE_NetIceSetRemoteDescription(session, description.data(), uint32_t(size))) {
                    problem = "relay peer description was refused; begin a fresh network session";
                    return CE_NET_PROTOCOL;
                }
                accepted = true;
            }
        }
        if (!published) {
            const int available = CE_NetIceLocalDescription(session, description.data(), description.size());
            if (available < 0) { problem = "relay description exceeds its bounded buffer"; return CE_NET_CAPACITY; }
            if (available == 1) {
                const auto result = exchange->PublishLocal(description.data(), std::strlen(description.data()), now);
                if (result == CeIceExchangeResult::Error) { problem = exchange->Error(); return CE_NET_IO; }
                published = result == CeIceExchangeResult::Ready;
            }
        }
        CE_NetStatus status{}; CE_NetGetStatus(session, &status);
        if (status.phase == CE_NET_ACTIVE) {
            if (!CE_NetIceUsesRelay(session)) {
                problem = "experimental relay session has no selected relay path";
                return CE_NET_PROTOCOL;
            }
            if (!activeReported) {
                std::fputs("onslaught-ce: experimental relay transport active path=relay\n", stderr);
                activeReported = true;
            }
            if (!trafficReported && status.sent_messages && status.received_messages) {
                std::fputs("onslaught-ce: experimental relay application traffic bidirectional\n", stderr);
                trafficReported = true;
            }
        }
        return CE_NET_OK;
    }
};
uint32_t PumpExchange(void *context, CE_NetSession *session, uint64_t now)
{
    return static_cast<GameExchange *>(context)->Poll(session, now);
}
const char *ExchangeError(void *context) { return static_cast<GameExchange *>(context)->problem; }
void DestroyExchange(void *context) { delete static_cast<GameExchange *>(context); }
CE_NetSession *CreationFailure(const char *problem)
{
    std::snprintf(createProblem, sizeof(createProblem), "%s", problem);
    return nullptr;
}
}

static CE_NetSession *CreateGameSession(const CE_NetConfig *config, bool lobby)
{
    createProblem[0] = 0;

    const std::shared_ptr<CeIceSignalProvider> signalChoice = selectedSignaling;
    const bool memoryDirectory = selectedSignalingDirectory[0] != 0;
    if (memoryDirectory && !signalChoice) return CreationFailure("Native signaling is not configured.");
    if (!CE_NetGameIceRequested()) {
        if (signalChoice) return CreationFailure("Native signaling requires relay transport selected at startup.");
        return CE_NetCreate(config);
    }
#if defined(_WIN32)
    // Grant ACL checks alone do not secure the separate exchange I/O boundary.
    // Keep the whole client route closed before opening either directory.
    return CreationFailure("experimental relay is unavailable on Windows pending private exchange checks");
#endif
    if (!config) return CreationFailure("invalid experimental relay session configuration");
    uint32_t lifetime = 0;
    if (!RequestedLifetime(lifetime)) return CreationFailure("invalid experimental relay lifetime selection");
    const char* base = memoryDirectory ? selectedSignalingDirectory : std::getenv("BEA_NET_ICE_DIRECTORY");
    const char* game = memoryDirectory ? selectedSignalingDirectory : std::getenv("BEA_NET_ICE_GAME_DIRECTORY");
    const bool hasBase = base && base[0], hasGame = game && game[0];
    if (lobby && (!hasBase || !hasGame))
        return CreationFailure("experimental relay lobby needs separate lobby and game directories before launch");
    const char *directory = lobby ? base : (hasGame ? game : base);
    const uint64_t now = UnixNow();
    if (!now || now > std::numeric_limits<uint64_t>::max() - lifetime)
        return CreationFailure("experimental relay session needs a valid clock");
    std::unique_ptr<GameExchange> owner(new(std::nothrow) GameExchange);
    if (!owner) return CreationFailure("experimental relay session allocation failed");
    owner->grantLimit = now + lifetime;
    owner->createdMs = CE_NetNowMs();
    if (!memoryDirectory && hasBase && hasGame && !owner->credentials.RequireDistinctDirectories(base, game))
        return CreationFailure(owner->credentials.Error());
    owner->exchange = CeIceExchange::Open(*config, directory, now);
    if (!owner->exchange) return CreationFailure("private relay exchange is unavailable; use a fresh session directory");
    owner->directory = directory;
    // This explicit diagnostic route can reach only an authored loopback relay.
    // Ordinary grants remain restricted to the fixed service endpoint policy.
    if (const char *fixture = std::getenv("BEA_NET_ICE_FIXTURE")) {
        if (std::strcmp(fixture, "loopback")) return CreationFailure("unsupported experimental relay fixture mode");
        owner->mode = CeIceCredentials::Mode::AuthoredLoopback;
    }

    CE_NetSession* session = nullptr;
    if (signalChoice) {
        if (owner->mode != CeIceCredentials::Mode::Cloudflare)
            return CreationFailure("Native signaling requires the production relay credential profile.");
        const uint64_t monotonic = CE_NetNowMs();
        if (monotonic > std::numeric_limits<uint64_t>::max() - kPreparationMs)
            return CreationFailure("The relay setup clock is invalid. Return to the menu and check the clock.");
        CeIceSignalRequest request;
        request.stage = lobby ? CeIceSignalStageKind::Lobby : CeIceSignalStageKind::Game;
        request.role = static_cast<CE_NetRole>(config->role);
        std::memcpy(request.context, owner->exchange->Context(), sizeof request.context);
        request.directory = owner->directory.c_str();
        uint32_t preparationMs = kPreparationMs;
        if (lobby) {
            // Prepare checks these stamps against the clock the provider read at its last pump, which the status page
            // ran a frame or more before this button. Pump an admitted pair with the same two readings first. With no
            // stage, that pump sends no request and changes no consent or deadline; it applies the pair's expiry and
            // clock checks at these readings. An active owner pumps its own stage, as in CE_NetSignalingPreflight.
            if (!exchangeOwners && signalChoice->View().status == CeIceSignalStatus::Admitted)
                signalChoice->Preflight(now, monotonic);
            request.requestedAt = now;
            request.requestExpiresAt = now + kPreparationMs / 1000;
            const CeIceSignalView pair = signalChoice->View();
            if (pair.status != CeIceSignalStatus::Admitted || pair.role != request.role ||
                pair.pairExpiresAt <= now || pair.pairExpiresAt < request.requestExpiresAt)
                return CreationFailure("The approved relay pair is not ready for this setup window. Check pair consent and expiry, then retry.");
            if (owner->grantLimit > pair.pairExpiresAt) owner->grantLimit = pair.pairExpiresAt;
            request.grantExpiresAt = owner->grantLimit;
            request.setupDeadlineMs = monotonic + kPreparationMs;
        } else {
            if (!gamePlanAvailable)
                return CreationFailure("Authenticated START setup is missing. Return to the menu and prepare the connection again.");
            const CeIceSignalGamePlan plan = restoredGamePlan;
            gamePlanAvailable = false;
            if (!CurrentGamePlan(plan, now, monotonic) || plan.role != request.role ||
                std::memcmp(plan.context, request.context, sizeof request.context) != 0)
                return CreationFailure("Authenticated START setup expired or changed. Return to the menu and prepare the connection again.");
            request.requestedAt = plan.requestedAt;
            request.requestExpiresAt = plan.requestExpiresAt;
            request.grantExpiresAt = plan.grantExpiresAt;
            request.setupDeadlineMs = plan.setupDeadlineMs;
            owner->grantLimit = plan.grantExpiresAt;
            preparationMs = static_cast<uint32_t>(plan.setupDeadlineMs - monotonic);
        }
        session = CE_NetCreateIcePending(config, preparationMs);
        if (!session) return CreationFailure("The relay session could not be prepared. Return to the menu and retry.");
        owner->signalProvider = signalChoice;
        if (request.stage == CeIceSignalStageKind::Game && signalChoice->PreflightPending()) {
            owner->deferredSignalDirectory = request.directory;
            owner->deferredSignalRequest = request;
            owner->deferredSignalRequest.directory = owner->deferredSignalDirectory.c_str();
            owner->deferredSignal = true;
        } else {
        owner->signaling = signalChoice->Prepare(request);
        if (!owner->signaling) {
            CE_NetDestroy(session);
            const char* failure = SignalingProblem(signalChoice->View().status);
            return CreationFailure(failure ? failure : "Accept the approved signaling pair before preparing this connection.");
        }
        }
    } else {
        const auto grant = owner->ReadGrant(now);
        if (grant == CeIceCredentials::LoadState::Error) return CreationFailure(owner->problem);
        session = CE_NetCreateIcePending(config, kPreparationMs);
        if (!session) return CreationFailure("experimental relay transport could not be created");
        if (grant == CeIceCredentials::LoadState::Pending &&
            !owner->credentials.PrepareRequest(directory, config->role, owner->exchange->Context(), now, kPreparationMs / 1000, lifetime)) {
            CE_NetDestroy(session);
            return CreationFailure(owner->credentials.Error());
        }
        if (grant == CeIceCredentials::LoadState::Ready && !owner->Start(session, now)) {
            CE_NetDestroy(session);
            return CreationFailure(owner->problem);
        }
    }
    const CE_NetIceExchangeCallbacks callbacks{PumpExchange, ExchangeError, DestroyExchange};
    if (!CE_NetIceAttachExchange(session, owner.get(), &callbacks)) {
        CE_NetDestroy(session);
        return CreationFailure("experimental relay exchange could not be attached");
    }
    owner->registered = true;
    ++exchangeOwners;
    owner.release();
    std::fputs("onslaught-ce: experimental relay transport selected\n", stderr);
    return session;
}
CE_NetSession *CE_NetCreateGame(const CE_NetConfig *config) { return CreateGameSession(config, false); }
CE_NetSession *CE_NetCreateLobby(const CE_NetConfig *config) { return CreateGameSession(config, true); }
const char *CE_NetGameProblem(const CE_NetSession *session)
{
    return session ? CE_NetIceProblem(session) : createProblem;
}

extern "C" int CE_NetSignalingDirectorySelected(void)
{
    return selectedSignalingDirectory[0] != 0;
}

bool CE_NetSelectSignalingDirectory(const char* directory) noexcept
{
    const char* value = directory ? directory : "";
    size_t size = 0;
    while (size <= 4096 && value[size]) ++size;
    if (size > 4096 || (size && value[0] != '/')) return false;
    if (!std::strcmp(value, selectedSignalingDirectory)) return true;
    if (exchangeOwners || gamePlanRestored) return false;
    // Native records have authenticated context-prefixed names in this fresh private dir;
    // manual grant files retain their separate Lobby/Game directory policy above.
    std::memcpy(selectedSignalingDirectory, value, size + 1);
    return true;
}

bool CE_NetSelectSignaling(std::shared_ptr<CeIceSignalProvider> provider) noexcept
{
    if (exchangeOwners && provider != selectedSignaling) return false;
    if (provider != selectedSignaling) {
        restoredGamePlan = CeIceSignalGamePlan{};
        gamePlanRestored = gamePlanAvailable = false;
    }
    selectedSignaling = std::move(provider);
    return true;
}

bool CE_NetRestoreSignalingGamePlan(const CeIceSignalGamePlan& plan) noexcept
{
    if (!selectedSignaling || exchangeOwners || gamePlanRestored || !CE_NetGameIceRequested() ||
        !CurrentGamePlan(plan, UnixNow(), CE_NetNowMs())) return false;
    restoredGamePlan = plan;
    gamePlanRestored = gamePlanAvailable = true;
    return true;
}

void CE_NetSignalingPreflight(uint64_t utcSeconds, uint64_t monotonicMs) noexcept
{
    if (selectedSignaling && !exchangeOwners && CE_NetGameIceRequested())
        selectedSignaling->Preflight(utcSeconds, monotonicMs);
}

CeIceSignalView CE_NetSignalingView() noexcept
{
    return selectedSignaling && CE_NetGameIceRequested() ? selectedSignaling->View() : CeIceSignalView{};
}

bool CE_NetSignalingAccept() noexcept
{
    return selectedSignaling && CE_NetGameIceRequested() && selectedSignaling->Accept();
}

void CE_NetSignalingClosePair() noexcept
{
    gamePlanAvailable = false;
    if (selectedSignaling) selectedSignaling->ClosePair();
}
#endif
