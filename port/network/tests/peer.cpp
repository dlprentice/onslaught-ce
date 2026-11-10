// SPDX-License-Identifier: GPL-3.0-only
// Standalone actual-process transport fixture. Fixed secret/compatibility are authored test values, never real invites.
#include "network.h"
#include "fixture_guard.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static void Pattern(uint8_t *out, uint32_t n, uint32_t seed)
{ for (uint32_t i = 0; i < n; ++i) out[i] = uint8_t((i * 37 + seed) ^ (i >> 8)); }
static bool IsPattern(const uint8_t *p, uint32_t n, uint32_t seed)
{ for (uint32_t i = 0; i < n; ++i) if (p[i] != uint8_t((i * 37 + seed) ^ (i >> 8))) return false; return true; }
int main(int argc, char **argv)
{
    if (!FixtureGuard()) return 2;
    if (argc < 4) return 2;
    bool host = std::strcmp(argv[1], "host") == 0;
    const char *scenario = argv[3];
    CE_NetConfig c{};
    c.role = host ? CE_NET_HOST : CE_NET_GUEST; c.ipv4 = "127.0.0.1"; c.port = uint16_t(std::strtoul(argv[2], nullptr, 10));
    for (int i = 0; i < 32; ++i) { c.secret[i] = uint8_t(i + 1); c.compatibility[i] = uint8_t(i ^ 0xa5); }
    if (!host && std::strcmp(scenario, "bad-key") == 0) c.secret[0] ^= 0x80;
    if (!host && std::strcmp(scenario, "incompatible") == 0) c.compatibility[0] ^= 0x80;
    c.handshake_ms = 1800; c.silence_ms = 1800;
    std::snprintf(c.version, sizeof c.version, "%s", host ? "9.8.7-host" : "9.8.6-guest");
    CE_NetSession *s = CE_NetCreate(&c);
    if (!s) return 3;
    CE_NetStatus status{}; CE_NetGetStatus(s, &status);
    std::printf("BOUND %u\n", unsigned(status.port)); std::fflush(stdout);
    uint64_t start = CE_NetNowMs(), activeAt = 0;
    bool announced = false, baseline = false, echoed = false, epochSent = false, finalSent = false;
    bool stopped = false, finalReceived = false, controlsAcked = false;
    uint32_t inputCount = 0, controls = 0, states = 0, lastState = 0, nextControl = 0, nextState = 0, nextInput = 0;
    bool inputBackpressure = false;
    bool stallDone = false; uint64_t lastFill = 0;
    uint8_t data[CE_NET_MAX_MESSAGE];
    int rc = 4;
    while (CE_NetNowMs() - start < 14000) {
        uint64_t now = CE_NetNowMs();
        if (!stopped) CE_NetPoll(s, now, 64);
        CE_NetGetStatus(s, &status);
        if (status.phase == CE_NET_FAILED || status.phase == CE_NET_CLOSED) {
            std::printf("END %u %u\n", status.phase, status.error);
            char peerVersion[CE_NET_VERSION_BYTES + 1] = {};
            if (CE_NetPeerVersion(s, peerVersion, sizeof peerVersion)) std::printf("PEER_VERSION %s\n", peerVersion);
            if (!std::strcmp(scenario, "bad-key")) rc = (!announced && status.error == CE_NET_AUTH) ? 0 : 5;
            else if (!std::strcmp(scenario, "incompatible")) rc = (!announced && status.error == CE_NET_INCOMPATIBLE) ? 0 : 6;
            else if (!std::strcmp(scenario, "timeout")) rc = (announced && (status.error == CE_NET_TIMEOUT || status.error == CE_NET_PEER_LEFT)) ? 0 : 7;
            else if (!std::strcmp(scenario, "backpressure")) rc = announced ? 0 : 8;
            else if (finalReceived || (host && finalSent)) rc = 0;
            break;
        }
        if (status.phase == CE_NET_ACTIVE && !announced) {
            std::puts("ACTIVE"); std::fflush(stdout); announced = true; activeAt = now;
            // Direction and size controls are rejected before queuing.
            assert(CE_NetSend(s, host ? CE_NET_INPUT : CE_NET_STATE, 0, nullptr, 0) == -1);
            assert(CE_NetSend(s, CE_NET_CONTROL, 0, data, CE_NET_MAX_INPUT + 1) == -1);
            assert(CE_NetSend(s, CE_NET_CONTROL, 0, nullptr, 1) == -1);
            assert(CE_NetSend(s, 255, 0, nullptr, 0) == -1);
        }
        if (announced && !std::strcmp(scenario, "timeout")) {
            if (!host && !stopped && now - activeAt >= 200) {
                std::puts("STOPPED"); std::fflush(stdout); stopped = true;
            }
            if (stopped && now - activeAt > 2600) { rc = 0; break; }
        }
        if (status.phase == CE_NET_ACTIVE && !std::strcmp(scenario, "stall-resend")) {
            // The host blocks like a level load while the guest keeps sending; the guest's ENet then measures the
            // stall as RTT. After it, the proxy drops the guest's marker once and the resend must still be prompt.
            if (host && !stallDone && now - activeAt >= 300) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1400)); stallDone = true;
            }
            if (!host && now - activeAt >= 200 && now - activeAt < 1700 && now - lastFill >= 100) {
                uint8_t x = 1; CE_NetSend(s, CE_NET_CONTROL, 7, &x, 1); lastFill = now;
            }
            if (!host && !finalSent && now - activeAt >= 2400) {
                uint8_t x = 0x42;
                if (CE_NetSend(s, CE_NET_CONTROL, 4242, &x, 1) == 1) { std::printf("MARKER_SENT %llu\n", (unsigned long long) now); finalSent = true; }
            }
        }
        if (status.phase == CE_NET_ACTIVE && !std::strcmp(scenario, "backpressure")) {
            if (host) {
                Pattern(data, sizeof(data), 99);
                int sends = 0;
                while (CE_NetSend(s, CE_NET_BASELINE, 0, data, sizeof(data)) == 1) { ++sends; assert(sends < 6); }
                assert(sends >= 1 && sends <= 3);
                std::printf("BOUNDED %d\n", sends); rc = 0; break;
            }
        }
        CE_NetMessage m{};
        while (!stopped && CE_NetReceive(s, &m, data, sizeof(data)) == 1) {
            assert(m.epoch == status.epoch);
            if (m.kind == CE_NET_BASELINE) {
                assert(!host && m.size == CE_NET_MAX_MESSAGE && IsPattern(data, m.size, 99));
                baseline = true; uint8_t ok = 0x7a; assert(CE_NetSend(s, CE_NET_CONTROL, 100, &ok, 1) == 1);
            } else if (m.kind == CE_NET_INPUT) {
                assert(host && m.size == 32 && IsPattern(data, m.size, m.tick));
                if (!std::strcmp(scenario, "ordered-input")) assert(m.tick == inputCount);
                ++inputCount;
            } else if (m.kind == CE_NET_STATE) {
                assert(!host && m.size == 2400 && IsPattern(data, m.size, m.tick));
                assert(m.tick > lastState); lastState = m.tick; ++states;
            } else if (m.kind == CE_NET_CONTROL && host && !std::strcmp(scenario, "stall-resend")) {
                if (m.tick == 4242) {
                    std::printf("MARKER_RECEIVED %llu\n", (unsigned long long) now);
                    uint8_t x = 0x6b; assert(CE_NetSend(s, CE_NET_CONTROL, 999, &x, 1) == 1); finalSent = true;
                }
                if (m.tick == 999) { finalReceived = true; rc = 0; }
            } else if (m.kind == CE_NET_CONTROL && !host && !std::strcmp(scenario, "stall-resend")) {
                if (m.tick == 999) { assert(CE_NetSend(s, CE_NET_CONTROL, 999, data, 1) == 1); finalReceived = true; rc = 0; }
            } else if (m.kind == CE_NET_CONTROL && host) {
                if (m.tick == 100) { assert(m.size == 1 && data[0] == 0x7a); echoed = true; }
                if (m.tick == 101) controlsAcked = true;
                if (m.tick == 999) { assert(m.size == 1 && data[0] == 0x6b); finalReceived = true; rc = 0; }
            } else if (m.kind == CE_NET_CONTROL) {
                if (!std::strcmp(scenario, "ordered-input") && m.tick == 1001) {
                    uint8_t ok = 0x6b; assert(CE_NetSend(s, CE_NET_CONTROL, 999, &ok, 1) == 1); finalReceived = true;
                    assert(inputBackpressure && nextInput == 300);
                } else if (m.tick == 999) {
                    assert(m.epoch == 2 && m.size == 1 && data[0] == 0x6b && controls == 12 && baseline && states > 0);
                    assert(CE_NetSend(s, CE_NET_CONTROL, 999, data, 1) == 1); finalReceived = true;
                } else {
                    assert(m.size == 1 && data[0] == controls && m.tick == controls); ++controls;
                    if (controls == 12) assert(CE_NetSend(s, CE_NET_CONTROL, 101, nullptr, 0) == 1);
                }
            }
        }
        if (finalReceived && !host && now - activeAt > 2000) { rc = 0; break; }
        if (finalReceived && host) { rc = 0; break; }
        if (status.phase == CE_NET_ACTIVE && !std::strcmp(scenario, "ordered-input")) {
            if (!host) {
                while (nextInput < 300) {
                    Pattern(data, 32, nextInput);
                    int queued = CE_NetSend(s, CE_NET_INPUT, nextInput, data, 32);
                    assert(queued >= 0);
                    if (!queued) { inputBackpressure = true; break; }
                    ++nextInput;
                }
            } else if (inputCount == 300 && !finalSent) {
                assert(CE_NetSend(s, CE_NET_CONTROL, 1001, nullptr, 0) == 1); finalSent = true;
            }
        }
        if (status.phase == CE_NET_ACTIVE && !stopped && !std::strcmp(scenario, "normal")) {
            if (host) {
                if (!baseline) {
                    Pattern(data, sizeof(data), 99);
                    if (CE_NetSend(s, CE_NET_BASELINE, 0, data, sizeof(data)) == 1) baseline = true;
                }
                // Controls and state use independent channels; loss in one must not block the other.
                if (nextControl < 12 && now - activeAt >= nextControl * 35) {
                    uint8_t x = uint8_t(nextControl);
                    if (CE_NetSend(s, CE_NET_CONTROL, nextControl, &x, 1) == 1) ++nextControl;
                }
                if (nextState < 100 && now - activeAt >= nextState * 12) {
                    Pattern(data, 2400, nextState + 1);
                    if (CE_NetSend(s, CE_NET_STATE, nextState + 1, data, 2400) == 1) ++nextState;
                }
                if (echoed && controlsAcked && inputCount > 0 && now - activeAt >= 1600 && !epochSent) {
                    assert(CE_NetBeginEpoch(s, 2) == 1); epochSent = true;
                    assert(CE_NetSend(s, CE_NET_CONTROL, 0, nullptr, 0) == 0);
                    assert(CE_NetBeginEpoch(s, 3) == 0);
                }
                if (epochSent && status.epoch == 2 && !finalSent) {
                    uint8_t x = 0x6b;
                    if (CE_NetSend(s, CE_NET_CONTROL, 999, &x, 1) == 1) finalSent = true;
                }
            } else if (status.epoch == 1 && now - activeAt < 1500) {
                uint32_t tick = uint32_t((now - activeAt) / 12) + 1;
                if (tick != nextState) { Pattern(data, 32, tick); CE_NetSend(s, CE_NET_INPUT, tick, data, 32); nextState = tick; }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::printf("RESULT %d role=%u control=%u state=%u input=%u epoch=%u rejected=%u backpressure=%u\n", rc, c.role,
                controls, states, inputCount, status.epoch, status.rejected_packets, unsigned(inputBackpressure));
    CE_NetClose(s); CE_NetDestroy(s);
    return rc;
}
