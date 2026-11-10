// SPDX-License-Identifier: GPL-3.0-only
// Authored loopback fixture: a host queues one terminal control and immediately closes the real transport.
#include "network.h"
#include "fixture_guard.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char **argv) {
    if (!FixtureGuard() || argc != 3) return 2;
    const bool host = std::strcmp(argv[1], "host") == 0;
    CE_NetConfig c{};
    c.role = host ? CE_NET_HOST : CE_NET_GUEST;
    c.ipv4 = "127.0.0.1";
    c.port = uint16_t(std::strtoul(argv[2], nullptr, 10));
    for (unsigned i = 0; i < 32; ++i) { c.secret[i] = uint8_t(i + 1); c.compatibility[i] = uint8_t(i ^ 0xa5); }
    c.handshake_ms = 4000; c.silence_ms = 2500;
    std::snprintf(c.version, sizeof c.version, "fixture");
    CE_NetSession *s = CE_NetCreate(&c);
    if (!s) return 3;
    CE_NetStatus status{}; CE_NetGetStatus(s, &status);
    std::printf("BOUND %u\n", unsigned(status.port)); std::fflush(stdout);
    const uint64_t began = CE_NetNowMs(); bool ready_sent = false;
    uint8_t payload[CE_NET_MAX_INPUT];
    while (CE_NetNowMs() - began < 8000) {
        CE_NetPoll(s, CE_NetNowMs(), 64);
        CE_NetGetStatus(s, &status);
        if (!host && status.phase == CE_NET_ACTIVE && !ready_sent) {
            const uint8_t ready = 1;
            if (CE_NetSend(s, CE_NET_CONTROL, 1, &ready, 1) == 1) ready_sent = true;
        }
        CE_NetMessage msg{};
        while (CE_NetReceive(s, &msg, payload, sizeof payload) == 1) {
            if (msg.kind != CE_NET_CONTROL || msg.size != 1 || payload[0] != 1) continue;
            if (host && msg.tick == 1) {
                const uint8_t terminal = 1;
                if (CE_NetSend(s, CE_NET_CONTROL, 4242, &terminal, 1) != 1) return 4;
                // This is the game controller's close sequence. Destruction prevents future retransmission.
                CE_NetClose(s); CE_NetDestroy(s);
                std::puts("HOST_TERMINAL_QUEUED_AND_CLOSED");
                return 0;
            }
            if (!host && msg.tick == 4242) {
                std::puts("GUEST_TERMINAL_RECEIVED");
                CE_NetClose(s); CE_NetDestroy(s);
                return 0;
            }
        }
        if (status.phase == CE_NET_CLOSED || status.phase == CE_NET_FAILED) {
            std::printf("INCOMPLETE phase=%u error=%u\n", status.phase, status.error);
            CE_NetDestroy(s); return 5;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::puts("INCOMPLETE timeout");
    CE_NetClose(s); CE_NetDestroy(s); return 6;
}
