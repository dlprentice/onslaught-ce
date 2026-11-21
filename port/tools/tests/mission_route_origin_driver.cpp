// SPDX-License-Identifier: GPL-3.0-only
// Native transport failure origin. No provider, game, admission data or credentials.
#include "../../network/transport.cpp"
#include "../../network/tests/fixture_guard.h"
#include <cstdio>

static unsigned failures;
static uint32_t provider_error;
static void Check(bool ok, const char *label)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static uint32_t ProviderPoll(void *, CE_NetSession *, uint64_t) { return provider_error; }
static const char *ProviderError(void *) { return "test provider ended"; }
static void ProviderDestroy(void *) {}
static void ActiveSession(CE_NetSession &session)
{
    session.status.phase = CE_NET_ACTIVE;
    session.silenceMs = 100;
    session.lastAuthenticated = 100;
    session.host = enet_host_create(nullptr, 1, 1, 0, 0);
}
int main()
{
    if (!FixtureGuard() || enet_initialize() != 0) return 2;
    Check(!CE_NetIceRouteInterrupted(nullptr), "no live handle has no failure cause");
    {
        CE_NetSession s{}; ActiveSession(s);
        if (!s.host) return 2;
        Check(!CE_NetIceRouteInterrupted(&s), "an active transport is not interrupted");
        CE_NetPoll(&s, 201, 1);
        Check(s.status.phase == CE_NET_FAILED && s.status.error == CE_NET_TIMEOUT && CE_NetIceRouteInterrupted(&s),
              "native authenticated-traffic deadline records the interrupted route");
        const auto reason=s.status.error; CE_NetPoll(&s, 202, 1);
        Check(s.status.error==reason && CE_NetIceRouteInterrupted(&s), "later polls preserve the first terminal cause");
        enet_host_destroy(s.host);
    }
    const uint32_t bad_provider = static_cast<uint32_t>(CE_NET_PEER_LEFT) + 100u;
    const uint32_t provider_cases[] = {CE_NET_AUTH, CE_NET_IO, CE_NET_PEER_LEFT, bad_provider};
    for (auto input : provider_cases) {
        CE_NetSession s{}; ActiveSession(s);
        if (!s.host) return 2;
        provider_error = input;
        s.iceWaiting = true; s.iceTimeoutMs = 1000;
        s.iceExchange = &provider_error;
        s.iceCallbacks.poll = ProviderPoll;
        s.iceCallbacks.error = ProviderError;
        s.iceCallbacks.destroy = ProviderDestroy;
        CE_NetPoll(&s, 201, 1);
        Check(s.status.phase == CE_NET_FAILED && s.status.error == (input == bad_provider ? static_cast<uint32_t>(CE_NET_IO) : input) &&
              !CE_NetIceRouteInterrupted(&s), "native provider failure keeps a distinct origin even when its published error overlaps");
        enet_host_destroy(s.host);
    }
    {
        CE_NetSession s{}; s.status.phase = CE_NET_CONNECTING; FailRoute(&s, CE_NET_IO);
        Check(!CE_NetIceRouteInterrupted(&s), "a connection still being established has no in-mission interruption");
    }
    {
        CE_NetSession s{}; s.status.phase = CE_NET_ACTIVE; FailRoute(&s, CE_NET_PROTOCOL);
        Check(!CE_NetIceRouteInterrupted(&s), "protocol failure at socket service is not a broken route");
    }
    {
        CE_NetSession s{}; s.status.phase = CE_NET_ACTIVE; FailRoute(&s, CE_NET_PEER_LEFT);
        Check(CE_NetIceRouteInterrupted(&s), "unannounced transport disconnect records path loss");
    }
    {
        CE_NetSession s{}; s.status.phase = CE_NET_ACTIVE; CE_NetClose(&s);
        Check(s.status.phase == CE_NET_CLOSED && !CE_NetIceRouteInterrupted(&s), "local voluntary close stays a normal closure");
    }
    enet_deinitialize();
    return failures ? 1 : 0;
}
