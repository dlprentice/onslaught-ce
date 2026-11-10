// SPDX-License-Identifier: GPL-3.0-only
// Authored loopback fixture: no game data or retail behavior is involved.
#include "network.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

static bool Exists(const std::string& path) {
    return static_cast<bool>(std::ifstream(path.c_str()));
}

static void Mark(const std::string& path) {
    std::ofstream(path.c_str()).close();
}

static void Idle() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

int main(int argc, char** argv) {
    if (argc != 7) return 90;
    const bool host = std::strcmp(argv[1], "host") == 0;
    const bool closer = std::strcmp(argv[3], "closer") == 0;
    const std::string folder = argv[4];
    const std::string own = folder + (host ? "/host.ready" : "/guest.ready");
    const std::string other = folder + (host ? "/guest.ready" : "/host.ready");
    CE_NetConfig config;
    std::memset(&config, 0, sizeof(config));
    config.role = host ? CE_NET_HOST : CE_NET_GUEST;
    config.ipv4 = "127.0.0.1";
    config.port = static_cast<unsigned short>(std::strtoul(argv[2], nullptr, 10));
    config.handshake_ms = 8000;
    config.silence_ms = 4000;
    const bool partial = std::strcmp(argv[3], "partial") == 0;
    const bool failed = std::strcmp(argv[3], "failed") == 0;
    if (failed) config.handshake_ms = 100;
    FILE* secret = std::fopen(argv[5], "rb");
    if (!secret) return 91;
    const bool secret_ok = std::fread(config.secret, sizeof(config.secret), 1, secret) == 1;
    std::fclose(secret);
    if (!secret_ok) return 92;
    CE_NetSession* session = CE_NetCreate(&config);
    if (!session) return 93;
    CE_NetStatus status;
    if (partial || failed) {
        bool failure_seen = !failed;
        if (failed) {
            for (unsigned step = 0; step < 1000; ++step) {
                CE_NetPoll(session, CE_NetNowMs(), 32);
                CE_NetGetStatus(session, &status);
                if (status.phase == CE_NET_FAILED) {
                    failure_seen = true;
                    break;
                }
                Idle();
            }
        }
        const auto begin = std::chrono::steady_clock::now();
        if (partial) {
            CE_NetClose(session);
            CE_NetClose(session);
        }
        CE_NetDestroy(session);
        CE_NetDestroy(nullptr);
        if (!failure_seen || std::chrono::steady_clock::now() - begin > std::chrono::milliseconds(1600)) return 99;
        Mark(folder + "/single.bounded");
        return 0;
    }
    bool connected = false;
    for (unsigned step = 0; step < 8000; ++step) {
        CE_NetPoll(session, CE_NetNowMs(), 32);
        CE_NetGetStatus(session, &status);
        if (status.phase == CE_NET_ACTIVE) {
            connected = true;
            break;
        }
        if (status.phase == CE_NET_FAILED || status.phase == CE_NET_CLOSED) break;
        Idle();
    }
    if (!connected) {
        CE_NetDestroy(session);
        return 94;
    }
    Mark(own);
    for (unsigned step = 0; step < 4000 && !Exists(other); ++step) {
        CE_NetPoll(session, CE_NetNowMs(), 32);
        Idle();
    }
    if (!Exists(other)) {
        CE_NetDestroy(session);
        return 95;
    }
    if (closer) {
        Mark(folder + "/drop.arm");
        for (unsigned step = 0; step < 4000 && !Exists(folder + "/drop.ready"); ++step) Idle();
        if (!Exists(folder + "/drop.ready")) {
            CE_NetDestroy(session);
            return 96;
        }
        const auto begin = std::chrono::steady_clock::now();
        CE_NetClose(session);
        CE_NetGetStatus(session, &status);
        if (status.phase == CE_NET_CLOSED && status.error == CE_NET_OK) Mark(folder + "/closer.local");
        if (std::strcmp(argv[6], "repeat") == 0) CE_NetClose(session);
        CE_NetDestroy(session);
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        if (elapsed > std::chrono::milliseconds(1600)) return 97;
        Mark(folder + "/closer.bounded");
        return 0;
    }
    bool departed = false;
    bool late_sent = false;
    for (unsigned step = 0; step < 7000; ++step) {
        CE_NetPoll(session, CE_NetNowMs(), 32);
        CE_NetGetStatus(session, &status);
        if (!late_sent && std::strcmp(argv[6], "late") == 0 && Exists(folder + "/drop.ready")) {
            const unsigned char payload = 'x';
            if (CE_NetSend(session, CE_NET_CONTROL, 0, &payload, sizeof(payload))) {
                late_sent = true;
                Mark(folder + "/observer.late-sent");
            }
        }
        if (status.phase == CE_NET_CLOSED && status.error == CE_NET_PEER_LEFT) {
            departed = true;
            break;
        }
        if (status.phase == CE_NET_FAILED) break;
        Idle();
    }
    if (status.phase == CE_NET_FAILED) Mark(folder + "/observer.failed");
    if (status.error == CE_NET_PEER_LEFT) Mark(folder + "/observer.peer-left");
    if (status.error == CE_NET_TIMEOUT) Mark(folder + "/observer.timeout");
    if (status.error == CE_NET_AUTH) Mark(folder + "/observer.auth");
    if (status.error == CE_NET_PROTOCOL) Mark(folder + "/observer.protocol");
    CE_NetDestroy(session);
    return departed ? 0 : 98;
}
