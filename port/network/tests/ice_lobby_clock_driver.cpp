// SPDX-License-Identifier: GPL-3.0-only
// The real signaling provider (signaling_provider.cpp) behind an authored HTTPS transport, under production
// CreateGameSession (ice_game.cpp), with the consumer fixture's engine, exchange and credential stand-ins. The status
// page's pump runs the way the game runs it: CE_NetSignalingPreflight once a frame, and a menu button that runs in a
// later frame, before that frame's pump. No socket, HTTP, credential, record, game or service is used.
#define main ConsumerRegressionMain
#include "signaling_consumer_fixture.inc"
#undef main
#include "signaling_provider.h"
#include <deque>
#include <vector>

// The provider is made with an injected transport; the platform factory is never reached.
std::unique_ptr<CeSignalHttps> CeCreateSignalHttps(const char*) noexcept { return nullptr; }

namespace {
constexpr uint64_t kWall = 2000000000, kMono = 100000, kFrameMs = 16;
std::string sessionDirectory;

struct PairAuthority final : CeIceSignalAuthority {
    CE_NetRole role; uint64_t end;
    PairAuthority(CE_NetRole r, uint64_t e) : role(r), end(e) {}
    bool ExpectedPair(CeIceSignalPairBinding* out) const noexcept override
    { if (!out) return false; std::memset(out->approval, 'a', 32); out->approval[32] = 0;
      out->role = role; out->expiresAt = end; return true; }
    bool AllowsGame(const CeIceSignalRequest&) const noexcept override { return false; }
private:
    bool CopyBearer(char* bytes, size_t capacity, size_t* size) const noexcept override
    { const char value[] = "authored.bearer"; if (!bytes || !size || capacity < sizeof value - 1) return false;
      std::memcpy(bytes, value, sizeof value - 1); *size = sizeof value - 1; return true; }
};

struct Wire { std::vector<std::string> routes, bodies; };
// One scripted reply per request, in order; a request off the script is refused, which fails the provider.
struct Transport final : CeSignalHttps {
    struct Step { std::string route, body; };
    std::deque<Step> script; Wire* wire = nullptr; bool active = false, hold = false;
    bool Begin(const char* route, const char*, size_t, const std::string& body, uint64_t now, uint64_t end) noexcept override
    { if (active || !route || end <= now || script.empty() || script.front().route != route) return false;
      wire->routes.push_back(route); wire->bodies.push_back(body); active = true; return true; }
    CeSignalHttpsState Poll(uint64_t, CeSignalHttpsReply* out) noexcept override
    { if (!active || !out) return CeSignalHttpsState::Failed;
      if (hold) return CeSignalHttpsState::Pending;
      out->code = 200; out->body = script.front().body; script.pop_front(); active = false;
      return CeSignalHttpsState::Ready; }
    void Cancel() noexcept override { active = false; }
};

std::string Reply(const char* status, CE_NetRole role, uint64_t end)
{
    return std::string("{\"schema\":1,\"status\":\"") + status + "\",\"role\":\"" + (role == CE_NET_HOST ? "host" : "guest") +
        "\",\"approval\":\"" + std::string(32, 'a') + "\",\"pairExpiresAt\":" + std::to_string(end) + "}";
}

// The status page's pump, once a frame (ce_netpages.cpp), with the frame's own clock readings.
void Frames(int count)
{
    for (int i = 0; i < count; ++i) {
        signalFixture.monotonic += kFrameMs;
        signalFixture.wall = kWall + (signalFixture.monotonic - kMono) / 1000;
        CE_NetSignalingPreflight(signalFixture.wall, signalFixture.monotonic);
    }
}

enum class Consent { None, Own, Both };

// One player's pair through the real provider, from the first pump to the menu button.
class Pair {
public:
    Pair(CE_NetRole role, uint64_t end, Consent consent) : role_(role)
    {
        Reset(false);
        setenv("BEA_NET_ICE_LIFETIME_SECONDS", "180", 1);
        signalFixture.wall = kWall; signalFixture.monotonic = kMono;
        auto transport = std::make_unique<Transport>(); transport_ = transport.get(); transport_->wire = &wire_;
        transport_->script = {{"pair/current", Reply("awaiting_pair_consent", role, end)}};
        if (consent == Consent::Both) transport_->script.push_back({"pair/accept", Reply("admitted", role, end)});
        if (consent == Consent::Own) {
            // Our consent is recorded and the peer's is not yet: the page then polls pair/current once a second.
            transport_->script.push_back({"pair/accept", Reply("awaiting_pair_consent", role, end)});
            for (int i = 0; i < 8; ++i) transport_->script.push_back({"pair/current", Reply("awaiting_pair_consent", role, end)});
        }
        transport_->script.push_back({"stage/request", Reply("awaiting_peer_request", role, end)});
        provider_ = CeCreateSignalingProviderWithTransport(std::move(transport), std::make_shared<PairAuthority>(role, end));
        Check(provider_ && CE_NetSelectSignaling(provider_) && CE_NetSelectSignalingDirectory(sessionDirectory.c_str()),
              "the real provider and a private session directory are selected");
        Frames(4);   // Continue on the disclosure: the pump binds the pair (pair/current)
        Check(CE_NetSignalingView().status == CeIceSignalStatus::NeedsConsent, "the bound pair needs consent");
        if (consent != Consent::None) {
            Check(CE_NetSignalingAccept(), "explicit Accept is queued");
            Frames(80);   // the pump sends it after the request spacing
        }
        const CeIceSignalStatus expected = consent == Consent::Both ? CeIceSignalStatus::Admitted :
            consent == Consent::Own ? CeIceSignalStatus::AwaitingPairConsent : CeIceSignalStatus::NeedsConsent;
        Check(CE_NetSignalingView().status == expected, "the pair reaches the consent state under test");
        const size_t sent = wire_.routes.size();
        Frames(60);   // the status page stays up for a second
        if (consent != Consent::Own)
            Check(wire_.routes.size() == sent, "the status page's pump sends nothing while the pair is admitted or needs consent");
    }
    ~Pair()
    {
        if (session_) CE_NetDestroy(session_);
        CE_NetSelectSignaling({});
        CE_NetSelectSignalingDirectory("");
    }
    // The button runs gapMs and wallStep seconds after the status page's last pump.
    void Press(uint64_t gapMs, int64_t wallStep)
    {
        signalFixture.monotonic += gapMs;
        signalFixture.wall = uint64_t(int64_t(signalFixture.wall) + wallStep);
        utc_ = signalFixture.wall; mono_ = signalFixture.monotonic;
        const size_t sent = wire_.routes.size();
        CE_NetConfig config{}; config.role = role_; config.secret[0] = 42;
        session_ = CE_NetCreateLobby(&config);
        created_ = wire_.routes.size() - sent;
    }
    void RequireSession(const char* label)
    {
        Check(session_ != nullptr, label);
        if (!session_) return;
        Check(!created_, "creating the lobby session sends no request");
        Check(CE_NetSignalingView().status == CeIceSignalStatus::AwaitingPeerRequest && signalFixture.pendingTimeout == 120000,
              "the lobby stage is prepared with the full setup window");
        CE_NetPoll(session_);   // the stage's own pump
        const bool sent = !wire_.routes.empty() && wire_.routes.back() == "stage/request";
        Check(sent && wire_.routes.size() == 3, "the stage's first pump sends the fixed request, the first request since admission");
        if (sent) {
            const std::string& body = wire_.bodies.back();
            const std::string role = std::string("\"role\":\"") + (role_ == CE_NET_HOST ? "host" : "guest") + "\"";
            Check(body.find("\"stage\":\"lobby\"") != std::string::npos && body.find(role) != std::string::npos &&
                  body.find("\"afterAuthenticatedStart\":false") != std::string::npos, "the request is this role's lobby stage");
            Check(body.find("\"requestedAt\":" + std::to_string(utc_) + ",") != std::string::npos &&
                  body.find("\"requestExpiresAt\":" + std::to_string(utc_ + 120) + ",") != std::string::npos,
                  "the request is stamped with the button's UTC reading");
        }
        // The setup deadline is the button's monotonic reading plus 120 s: the stage lives 1 ms before it and ends at it.
        transport_->hold = true;
        signalFixture.monotonic = mono_ + 120000 - 1 - 1001; CE_NetPoll(session_);
        Check(CE_NetSignalingView().status == CeIceSignalStatus::AwaitingPeerRequest, "the stage is alive 1 ms before its fixed deadline");
        signalFixture.monotonic = mono_ + 120000 - 1001; CE_NetPoll(session_);
        Check(CE_NetSignalingView().status == CeIceSignalStatus::SetupExpired, "the stage expires at the button's reading plus 120 s, no later");
    }
    void RequireRefusal(const char* label, CeIceSignalStatus status)
    {
        Check(!session_ && !created_ && CE_NetSignalingView().status == status, label);
        Check(std::find(wire_.routes.begin(), wire_.routes.end(), "stage/request") == wire_.routes.end(),
              "a refused button sends no stage request");
    }
private:
    CE_NetRole role_;
    Wire wire_;
    Transport* transport_ = nullptr;
    std::shared_ptr<CeIceSignalProvider> provider_;
    CE_NetSession* session_ = nullptr;
    size_t created_ = 0;
    uint64_t utc_ = 0, mono_ = 0;
};

struct Button { const char* label; CE_NetRole role; uint64_t gapMs; int64_t wallStep; };
}

int main(int argc, char** argv)
{
    if (argc != 2 || argv[1][0] != '/') return 2;
    sessionDirectory = std::string(argv[1]) + "/signal-session";
    unsetenv("BEA_NET_ICE_FIXTURE");
    const uint64_t open = kWall + 3600;
    // The game's own routes: a menu button runs at least a frame after the status page's last pump.
    for (const Button& button : {
             Button{"host Proceed one frame after the status page's last pump", CE_NET_HOST, kFrameMs, 0},
             Button{"host Proceed 1 ms after the last pump", CE_NET_HOST, 1, 0},
             Button{"host Proceed in the last pump's millisecond, after the UTC second rolled", CE_NET_HOST, 0, 1},
             Button{"host Proceed 1 ms and one UTC second after the last pump", CE_NET_HOST, 1, 1},
             Button{"guest Connect 5 s after leaving the status page", CE_NET_GUEST, 5000, 5},
             Button{"host Proceed in the last pump's millisecond and second", CE_NET_HOST, 0, 0}}) {
        Pair pair(button.role, open, Consent::Both);
        pair.Press(button.gapMs, button.wallStep);
        pair.RequireSession(button.label);
    }
    // Refusals the pump must not weaken; none of these buttons sends a request.
    {
        Pair pair(CE_NET_HOST, open, Consent::None); pair.Press(kFrameMs, 1);
        pair.RequireRefusal("a pair that was never accepted is refused", CeIceSignalStatus::NeedsConsent);
    }
    {
        // An unadmitted pair is not pumped: here a pump would have polled pair/current.
        Pair pair(CE_NET_HOST, open, Consent::Own); pair.Press(1100, 1);
        pair.RequireRefusal("a pair still waiting for the peer's consent is refused", CeIceSignalStatus::AwaitingPairConsent);
    }
    {
        Pair pair(CE_NET_HOST, kWall + 100, Consent::Both); pair.Press(kFrameMs, 1);
        pair.RequireRefusal("a pair that ends inside the 120 s setup window is refused", CeIceSignalStatus::Admitted);
    }
    {
        Pair pair(CE_NET_HOST, kWall + 3, Consent::Both); pair.Press(kFrameMs, 1);
        pair.RequireRefusal("a pair that ended by the button is refused and expires", CeIceSignalStatus::Expired);
    }
    {
        Pair pair(CE_NET_HOST, open, Consent::Both); pair.Press(kFrameMs, -1);
        pair.RequireRefusal("a wall clock that stepped back since the last pump fails closed", CeIceSignalStatus::InvalidRequest);
    }
    CE_NetSelectSignaling({});
    std::printf("%d assertions, %d failures\n", assertions, failures);
    return failures ? 1 : 0;
}
