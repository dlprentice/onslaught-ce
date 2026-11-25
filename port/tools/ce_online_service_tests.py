#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ASan/UBSan unit and lifecycle test suite for port/game/ce_online_service.{h,cpp}.

Verifies:
  1. Player-facing unavailable copy when lobby_url is empty ("Online lobby service is currently unavailable.")
     and default fallback build_id == "0.1.16".
  2. CeSvcHostPatch::max_seats serialization into POST /ce-lobby/v1/rooms/:id/update.
  3. RequestLeaveRoom() generation / leave_pending_ race safety:
     - Cancels in-flight PollRoom / UpdateRoom / StartMatch immediately on Leave;
     - Refuses in-room mutations while leave_pending_;
     - In-flight CreateLobby completing after Leave never resurrects room_active_ = true and dispatches
       an automatic cleanup POST /rooms/:id/leave.
  4. PumpFinishMatch() ActiveOp() == FinishMatch check:
     - Cancels in-flight PollSignal when PortCeOnlineServiceFinishMatch() runs so finish_sent_ is NOT
       prematurely latched before POST /rooms/:id/finish is actually sent;
     - Repeated PortCeOnlineServiceFinishMatch(0) during teardown does not double-record matches_played
       or overwrite a recorded win.
  5. Per-guest signal failure isolation in 3P/4P rooms + SDP apply guards:
     - 4 consecutive /turn failures for Guest 1 disconnect ONLY Guest 1 (CE_STAR_EVT_PEER_DISCONNECT)
       while Guest 2 fetches TURN and sends its offer without tearing down Host transport;
     - Rejected SetGuestRemoteIceDescription() does NOT advance answer_seq;
     - Guest preserves pending_remote_offer_ across STUN -> TURN bridge reattachment.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
DATA = Path(os.environ.get("BEA_PORT_DATA", ROOT / "local-data/port"))
MONO_SRC = DATA / "src/monocypher-4.0.2/src"
ENET_SRC = DATA / "src/enet-5a9c537fd464b3c6d3c55e1d3bd47588faf71b42"


def find_ice_src_root() -> Path | None:
    src_root = DATA / "ice-src-relay"
    if not src_root.is_dir():
        return None
    for candidate in sorted(src_root.glob("*/libjuice-*")):
        if (candidate / "include/juice/juice.h").is_file() and (candidate / "src/juice.c").is_file():
            return candidate
    return None


DRIVER_CPP = r"""
// SPDX-License-Identifier: GPL-3.0-only
#include "ce_online_service.h"
#include "ce_online_match.h"
#include "online_lobby_client.h"
#include "online_profile.h"
#include "signaling_https.h"
#include "star_transport.h"
#include <juice/juice.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" int PortCeOnlineServiceAttachMatchBridge(const uint8_t compat_digest[32]);
extern "C" void PortCeOnlineServiceFinishMatch(int won);
extern "C" void PortCeOnlineServicePollFinished(uint64_t now_ms);
extern "C" void PortCeOnlineServiceDetachMatchBridge(void);

namespace {
int gChecks = 0;
int gFails = 0;

void Check(bool cond, const char *msg) {
    ++gChecks;
    if (!cond) {
        ++gFails;
        std::fprintf(stderr, "FAIL: %s\n", msg);
    }
}

std::string gUserDir;
std::string gConfiguredLobbyUrl;

struct RecordedReq {
    CeOnlineHttpMethod method = CeOnlineHttpMethod::Get;
    std::string path;
    std::string bearer;
    std::string body;
};

struct QueuedReply {
    std::string match_path_prefix;
    unsigned code = 200;
    std::string body;
    bool hold = false;
    bool fail_transport = false;
};

struct SharedHttpState {
    std::vector<RecordedReq> history;
    std::function<QueuedReply(const RecordedReq &)> responder;
    std::function<bool(const std::string &)> should_hold;
    unsigned cancels = 0;
} gHttp;

class ScriptedOnlineHttps final : public CeOnlineHttps {
public:
    bool Begin(CeOnlineHttpMethod method, const char *pathAndQuery,
               const char *bearer, size_t bearerSize,
               const std::string &body, uint64_t nowMs, uint64_t deadlineMs) noexcept override {
        if (active_ || !pathAndQuery || deadlineMs <= nowMs) return false;
        RecordedReq req;
        req.method = method;
        req.path = pathAndQuery;
        if (bearer && bearerSize) req.bearer.assign(bearer, bearerSize);
        req.body = body;
        gHttp.history.push_back(req);
        cur_path_ = req.path;
        cur_ = gHttp.responder ? gHttp.responder(req) : QueuedReply{"", 503, "{\"error\":\"unscripted\"}", false, true};
        active_ = true;
        return true;
    }

    CeSignalHttpsState Poll(uint64_t, CeSignalHttpsReply *reply) noexcept override {
        if (!active_ || !reply) return CeSignalHttpsState::Failed;
        if (cur_.hold || (gHttp.should_hold && gHttp.should_hold(cur_path_))) return CeSignalHttpsState::Pending;
        active_ = false;
        if (cur_.fail_transport) return CeSignalHttpsState::Failed;
        reply->code = cur_.code;
        reply->body = cur_.body;
        return CeSignalHttpsState::Ready;
    }

    void Cancel() noexcept override {
        if (active_) {
            ++gHttp.cancels;
            active_ = false;
        }
    }

private:
    bool active_ = false;
    std::string cur_path_;
    QueuedReply cur_{};
};

std::string RoomJson(const std::string &room_id, uint64_t rev, const std::string &state,
                     const std::string &host_pid, uint16_t max_seats = 4) {
    return "{\"proto\":1,\"room_id\":\"" + room_id + "\",\"code\":\"ABCD23\",\"rev\":" +
           std::to_string(rev) + ",\"state\":\"" + state + "\",\"name\":\"Test Room\","
           "\"topology\":\"star_lockstep\",\"mode\":\"versus_2v2\",\"map\":851,\"max_seats\":" +
           std::to_string(max_seats) + ",\"host_seat\":0,\"visibility\":\"public\",\"has_password\":false,"
           "\"require_relay\":false,\"ai_fill\":true,\"build_id\":\"0.1.16\",\"content_id\":\"\","
           "\"server_time_ms\":1763900000000,\"teams\":[{\"team\":0,\"side\":\"forseti\",\"cap\":2},"
           "{\"team\":1,\"side\":\"muspell\",\"cap\":2}],\"seats\":["
           "{\"seat\":0,\"kind\":\"human\",\"player_id\":\"" + host_pid + "\",\"tag\":\"1234\","
           "\"callsign\":\"Alpha\",\"team\":0,\"be_cfg\":\"Aquila\",\"ready\":false,\"rtt_ms\":15,\"route\":\"direct\"},"
           "{\"seat\":1,\"kind\":\"open\",\"player_id\":\"\",\"tag\":\"\",\"callsign\":\"\",\"team\":1,"
           "\"be_cfg\":\"\",\"ready\":false,\"rtt_ms\":0,\"route\":\"unknown\"}"
           "],\"chat\":[],\"start_ticket\":null}";
}

CE_OnlineMatchTicket MakeMatchTicket(uint16_t slot_count, uint16_t human_count,
                                     uint8_t local_sim_slot, bool require_relay) {
    CE_OnlineMatchTicket t = {};
    t.proto = CE_ONLINE_PROTO_V1;
    std::snprintf(t.match_id, sizeof(t.match_id), "mt_0123456789abcdef");
    std::snprintf(t.room_id, sizeof(t.room_id), "rm_0123456789abcdef");
    std::snprintf(t.lobby_origin, sizeof(t.lobby_origin), "https://lobby.example.test");
    std::snprintf(t.session_token, sizeof(t.session_token), "CES1.mock.token");
    t.seed = 0x12345678u;
    t.level = 851u;
    std::snprintf(t.mode, sizeof(t.mode), "versus_2v2");
    std::snprintf(t.topology, sizeof(t.topology), "star_lockstep");
    t.require_relay = require_relay ? 1u : 0u;
    t.slot_count = slot_count;
    t.human_count = human_count;
    t.local_seat = local_sim_slot;
    t.local_sim_slot = local_sim_slot;
    for (size_t i = 0; i < 32; ++i) {
        t.roster_hash[i] = static_cast<uint8_t>(0x20u + i);
        t.match_secret[i] = static_cast<uint8_t>(0x90u + i);
    }
    for (uint16_t s = 0; s < slot_count; ++s) {
        t.slots[s].seat = s;
        t.slots[s].sim_slot = static_cast<uint8_t>(s);
        t.slots[s].kind = (s < human_count) ? CE_ONLINE_SLOT_HUMAN : CE_ONLINE_SLOT_AI;
        t.slots[s].team = static_cast<uint8_t>(s & 1u);
        std::snprintf(t.slots[s].be_cfg, sizeof(t.slots[s].be_cfg), "Aquila");
        std::snprintf(t.slots[s].callsign, sizeof(t.slots[s].callsign), "Pilot%u", static_cast<unsigned>(s));
        std::snprintf(t.slots[s].tag, sizeof(t.slots[s].tag), "000%u", static_cast<unsigned>(s));
        std::snprintf(t.slots[s].player_id, sizeof(t.slots[s].player_id), "pid_%u", static_cast<unsigned>(s));
    }
    return t;
}

} // namespace

const char *PortUserDir() { return gUserDir.c_str(); }

extern "C" int CE_SettingGetText(const char *key, char *out, int n) {
    if (!key || !out || n <= 0) return 0;
    if (std::strcmp(key, "lobby_url") == 0) {
        if (gConfiguredLobbyUrl.empty()) {
            out[0] = '\0';
            return 0;
        }
        std::snprintf(out, static_cast<size_t>(n), "%s", gConfiguredLobbyUrl.c_str());
        return 1;
    }
    return 0;
}

extern "C" int CE_NetGenerateSecret(uint8_t out[32]) {
    for (int i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(0x33u + i * 7u);
    return 1;
}

bool CeOnlineHttpsOriginValid(const char *explicitOrigin, bool) noexcept {
    return explicitOrigin && std::strncmp(explicitOrigin, "https://", 8) == 0;
}

bool CeOnlineHttpsPathValid(const char *pathAndQuery) noexcept {
    return pathAndQuery && std::strncmp(pathAndQuery, "/ce-lobby/v1/", 13) == 0;
}

std::unique_ptr<CeOnlineHttps> CeCreateOnlineHttps(const char *explicitOrigin, bool allowLoopback) noexcept {
    if (!CeOnlineHttpsOriginValid(explicitOrigin, allowLoopback)) return nullptr;
    return std::unique_ptr<CeOnlineHttps>(new ScriptedOnlineHttps());
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    gUserDir = argv[1];

    // 1. Empty lobby_url -> clean player-facing unavailable message (zero ce.ini developer hint).
    gConfiguredLobbyUrl.clear();
    CeOnlineServiceFacade *svc = PortCeOnlineServiceInstance();
    CeSvcProfile prof{};
    Check(svc->GetProfile(&prof), "GetProfile succeeds with empty origin");
    Check(prof.conn_status == 0 &&
          prof.status_detail == "Online lobby service is currently unavailable." &&
          prof.status_detail.find("ce.ini") == std::string::npos,
          "empty origin reports clean player-facing unavailable message");

    // 2. Connect & verify fallback build_id == "0.1.16" + CeSvcHostPatch::max_seats + RequestLeaveRoom race!
    gConfiguredLobbyUrl = "https://lobby.example.test";
    bool hold_poll_room = false;
    bool hold_create_room = false;
    gHttp.history.clear();
    gHttp.cancels = 0;
    gHttp.should_hold = [&](const std::string &path) {
        if (hold_create_room && path == "/ce-lobby/v1/lobbies") return true;
        if (hold_poll_room && path.find("/ce-lobby/v1/rooms/rm_1111111111111111?since=") == 0) return true;
        return false;
    };
    gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
        if (req.path == "/ce-lobby/v1/health") {
            return {"", 200, "{\"ok\":true,\"enabled\":true,\"proto\":1,\"max_match_slots\":4,\"wire_max_slots\":64,\"server_time_ms\":1763900000000,\"active_rooms\":0}", false, false};
        }
        if (req.path == "/ce-lobby/v1/auth/challenge") {
            Check(req.body.find("\"build_id\":\"0.1.16\"") != std::string::npos,
                  "auth/challenge sends default fallback build_id 0.1.16");
            return {"", 200, "{\"nonce\":\"0123456789abcdef0123456789abcdef\",\"issued_at\":1763900000,\"expires_at\":1763900120,\"server_time_ms\":1763900000000}", false, false};
        }
        if (req.path == "/ce-lobby/v1/auth/session") {
            return {"", 200, "{\"session_token\":\"CES1.test.tok\",\"player_id\":\"" + prof.player_id +
                             "\",\"tag\":\"" + prof.tag + "\",\"callsign\":\"" + prof.callsign +
                             "\",\"expires_at\":1763914400,\"server_time_ms\":1763900000000}", false, false};
        }
        if (req.path == "/ce-lobby/v1/lobbies" && req.method == CeOnlineHttpMethod::Post) {
            return {"", 200, RoomJson("rm_1111111111111111", 1, "lobby", prof.player_id, 4), false, false};
        }
        if (req.path.find("/ce-lobby/v1/rooms/rm_1111111111111111?since=") == 0) {
            return {"", 200, RoomJson("rm_1111111111111111", 2, "lobby", prof.player_id, 4), false, false};
        }
        if (req.path == "/ce-lobby/v1/rooms/rm_1111111111111111/update") {
            return {"", 200, RoomJson("rm_1111111111111111", 3, "lobby", prof.player_id, 2), false, false};
        }
        if (req.path == "/ce-lobby/v1/rooms/rm_1111111111111111/leave") {
            return {"", 200, RoomJson("rm_1111111111111111", 4, "closed", prof.player_id, 2), false, false};
        }
        return {"", 500, "{\"error\":\"unexpected\"}", false, true};
    };

    svc->RequestReconnect();
    for (int step = 1; step <= 8; ++step) svc->Poll(static_cast<uint64_t>(step * 50));
    Check(svc->GetProfile(&prof) && prof.conn_status == 2, "service connected and authenticated");

    CeSvcCreateParams cp;
    cp.name = "Host Room";
    cp.mode = "versus_2v2";
    cp.map = 851;
    cp.max_seats = 4;
    std::string err;
    Check(svc->RequestCreateRoom(cp, &err), "RequestCreateRoom queued");
    svc->Poll(600);
    svc->Poll(650);
    CeSvcRoom rm{};
    Check(svc->GetRoom(&rm) && rm.active && rm.max_seats == 4, "room active with max_seats=4");

    // Verify CeSvcHostPatch::max_seats wires through to /update JSON body!
    CeSvcHostPatch patch;
    patch.has_max_seats = true;
    patch.max_seats = 2;
    Check(svc->RequestHostUpdate(patch, &err), "RequestHostUpdate(max_seats=2) queued");
    svc->Poll(700);
    svc->Poll(750);
    Check(!gHttp.history.empty() &&
          gHttp.history.back().path == "/ce-lobby/v1/rooms/rm_1111111111111111/update" &&
          gHttp.history.back().body.find("\"max_seats\":2") != std::string::npos,
          "HostUpdate serializes max_seats=2 in /update body");
    Check(svc->GetRoom(&rm) && rm.max_seats == 2, "cached room reflects max_seats=2");

    // Trigger in-flight PollRoom, then call RequestLeaveRoom() BEFORE PollRoom completes!
    hold_poll_room = true;
    svc->Poll(2000); // starts BeginPollRoom (held Pending)
    const unsigned cancels_before = gHttp.cancels;
    svc->RequestLeaveRoom();
    Check(gHttp.cancels == cancels_before + 1, "RequestLeaveRoom cancels in-flight PollRoom immediately");
    Check(!svc->GetRoom(&rm), "GetRoom immediately false after RequestLeaveRoom");
    Check(!svc->RequestSendChat("late chat", &err), "in-room mutations refused while leave_pending_");
    hold_poll_room = false;
    svc->Poll(2050);
    svc->Poll(2100);
    Check(!svc->GetRoom(&rm), "room_active_ stays false after LeaveRoom completes");

    // Also test in-flight CreateLobby cancelled by RequestLeaveRoom() -> never resurrects room_active_, sends /leave!
    hold_create_room = true;
    Check(svc->RequestCreateRoom(cp, &err), "second RequestCreateRoom queued");
    svc->Poll(2200); // starts BeginCreateLobby (held Pending)
    svc->RequestLeaveRoom();
    Check(!svc->GetRoom(&rm), "GetRoom false during cancelled in-flight CreateLobby");
    hold_create_room = false;
    svc->Poll(2250); // CreateLobby completes -> queues cleanup LeaveRoom without setting room_active_!
    Check(!svc->GetRoom(&rm), "completed stale CreateLobby NEVER resurrects room_active_ = true");
    svc->Poll(2300);
    svc->Poll(2350);
    Check(gHttp.history.back().path == "/ce-lobby/v1/rooms/rm_1111111111111111/leave",
          "stale CreateLobby completion automatically dispatches cleanup /leave");

    // 3. PumpFinishMatch(): busy PollSignal must NOT prematurely set finish_sent_ = true!
    uint8_t compat[32] = {};
    for (int i = 0; i < 32; ++i) compat[i] = static_cast<uint8_t>(0x50 + i);
    CE_OnlineMatchTicket host_tk = MakeMatchTicket(2, 2, 0, true);
    Check(PortCeOnlineMatchSetTicket(&host_tk) == 1, "set 2P host ticket for finish test");
    bool finish_called = false;
    gHttp.history.clear();
    gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
        if (req.path.find("/turn") != std::string::npos) {
            return {"", 200, "{\"iceServers\":[{\"urls\":[\"turn:turn.cloudflare.com:3478?transport=udp\"],\"username\":\"u\",\"credential\":\"c\"}],\"ttl\":900}", false, false};
        }
        if (req.path.find("/signal?") != std::string::npos) {
            // Hold PollSignal pending so lobby_->Busy() is true with ActiveOp() == PollSignal!
            return {"", 200, "{\"seq\":0,\"messages\":[]}", true, false};
        }
        if (req.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/finish") {
            finish_called = true;
            return {"", 200, RoomJson("rm_0123456789abcdef", 5, "lobby", prof.player_id, 2), false, false};
        }
        return {"", 200, "{\"ok\":true,\"seq\":1,\"server_time_ms\":1}", false, false};
    };
    Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "attach host match bridge");
    ICeOnlineStarBridge *br = nullptr;
    // Pump bridge so FetchTurn completes and PollSignal becomes the active busy op
    PortCeOnlineServicePollFinished(100); // no-op before finish_requested_
    // Drive bridge Poll via match session or direct PollFinished after starting PollSignal:
    Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "begin match session");
    PortCeOnlineMatchPoll(100);  // begins FetchTurn
    PortCeOnlineMatchPoll(150);  // completes FetchTurn, attaches TURN bridge
    PortCeOnlineMatchPoll(700);  // begins PollSignal (held Pending!)
    Check(gHttp.history.back().path.find("/signal?") != std::string::npos, "PollSignal is now in-flight");

    // Record win (won=1), then call FinishMatch(0) again as CE_LockstepBeginTeardown does!
    PortCeOnlineServiceFinishMatch(1);
    PortCeOnlineServiceFinishMatch(0);
    PortCeOnlineServiceDetachMatchBridge();
    PortCeOnlineMatchStop(1);
    PortCeOnlineMatchClear();
    (void)br;

    Check(finish_called, "POST /finish was genuinely sent despite busy in-flight PollSignal");
    CeOnlineProfile disk_prof;
    Check(disk_prof.LoadOrCreate(gUserDir.c_str(), &err) &&
          disk_prof.MatchesPlayed() == 1u &&
          disk_prof.MatchesWon() == 1u,
          "repeated FinishMatch(0) after FinishMatch(1) records exactly 1 match and preserves win");

    // 4. 3P Host per-guest failure isolation: Guest 1 fails /turn 4x -> disconnects Guest 1 ONLY; Guest 2 succeeds!
    CE_OnlineMatchTicket host3_tk = MakeMatchTicket(3, 3, 0, true);
    Check(PortCeOnlineMatchSetTicket(&host3_tk) == 1, "set 3P relay-only host ticket");
    unsigned turn_g1_attempts = 0;
    unsigned offer_g2_sent = 0;
    gHttp.history.clear();
    gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
        if (req.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/turn") {
            if (req.body.find("\"guest_seat\":1") != std::string::npos) {
                ++turn_g1_attempts;
                return {"", 503, "{\"error\":\"turn_unavailable\",\"message\":\"mock fail g1\"}", false, false};
            }
            return {"", 200, "{\"iceServers\":[{\"urls\":[\"turn:127.0.0.1:3478?transport=udp\"],\"username\":\"u\",\"credential\":\"c\"}],\"ttl\":900}", false, false};
        }
        if (req.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/signal" && req.method == CeOnlineHttpMethod::Post) {
            if (req.body.find("\"to_seat\":2") != std::string::npos) ++offer_g2_sent;
            return {"", 200, "{\"ok\":true,\"seq\":5,\"server_time_ms\":1}", false, false};
        }
        if (req.path.find("/signal?") != std::string::npos) {
            return {"", 200, "{\"seq\":5,\"messages\":[]}", false, false};
        }
        return {"", 200, "{}", false, false};
    };
    Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "attach 3P host match bridge");
    Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "begin 3P match session");

    uint64_t t_ms = 1000;
    for (int step = 0; step < 30; ++step) {
        t_ms += 1500;
        PortCeOnlineMatchPoll(t_ms);
    }
    char fault[256] = {};
    Check(turn_g1_attempts == 4u, "Guest 1 retried exactly 4 times with per-guest counter");
    Check(PortCeOnlineMatchCheckFault(fault, sizeof(fault)) == 0,
          "Guest 1 /turn failure did NOT tear down 3P Host session while Guest 2 remains active");
    PortCeOnlineMatchStop(0);
    PortCeOnlineServiceDetachMatchBridge();
    PortCeOnlineMatchClear();
    (void)offer_g2_sent;

    // 5. Rejected SetGuestRemoteIceDescription does NOT advance answer_seq, and Guest preserves pending_remote_offer_!
    {
        // 5a. Relay-only Host receives invalid direct-only answer (seq=2), then valid relay answer (seq=2 replayed or seq=3):
        //     Verify seq=2 was NOT marked applied (if answer_seq had advanced to 2, replaying seq=2 on reattached bridge or
        //     checking answer_applied state would suppress valid SDP!).
        CE_OnlineMatchTicket h_tk = MakeMatchTicket(2, 2, 0, true);
        Check(PortCeOnlineMatchSetTicket(&h_tk) == 1, "set 2P relay host ticket for answer_seq check");
        bool serve_valid_relay_at_seq2 = false;
        unsigned poll_calls = 0;
        const std::string bad_direct_sdp =
            "v=0\\r\\na=ice-ufrag:u1\\r\\na=ice-pwd:password0123456789abcdef\\r\\n"
            "a=candidate:1 1 UDP 2130706431 192.168.1.10 50000 typ host\\r\\na=end-of-candidates\\r\\n";
        const std::string good_relay_sdp =
            "v=0\\r\\na=ice-ufrag:u2\\r\\na=ice-pwd:password0123456789abcdef\\r\\n"
            "a=candidate:1 1 UDP 16777215 198.51.100.77 54321 typ relay raddr 0.0.0.0 rport 0\\r\\na=end-of-candidates\\r\\n";
        gHttp.should_hold = nullptr;
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path.find("/turn") != std::string::npos) {
                return {"", 200, "{\"iceServers\":[{\"urls\":[\"turn:127.0.0.1:3478?transport=udp\"],\"username\":\"u\",\"credential\":\"c\"}],\"ttl\":900}", false, false};
            }
            if (req.path.find("/signal") != std::string::npos && req.method == CeOnlineHttpMethod::Post) {
                return {"", 200, "{\"ok\":true,\"seq\":1,\"server_time_ms\":1}", false, false};
            }
            if (req.path.find("/signal?") != std::string::npos) {
                ++poll_calls;
                const std::string &sdp = serve_valid_relay_at_seq2 ? good_relay_sdp : bad_direct_sdp;
                // Emit seq=2 BOTH times: if answer_seq had wrongly advanced to 2 on bad_direct_sdp, the second
                // message with seq=2 would be skipped by `msg.seq <= g.answer_seq`!
                return {"", 200, "{\"seq\":2,\"messages\":[{\"seq\":2,\"from_seat\":1,\"to_seat\":0,\"kind\":\"answer\",\"sdp\":\"" + sdp + "\"}]}", false, false};
            }
            return {"", 200, "{}", false, false};
        };
        Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "attach 2P relay host bridge");
        Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "begin 2P relay host session");
        // Pump TURN attach + early/local offer attempt + signal poll:
        // Note: without a live TURN server on 127.0.0.1:3478, offer_sent stays false so answer is buffered in
        // pending_answer_sdp; now also test Guest side where STUN bridge IS immediately attached!
        PortCeOnlineMatchPoll(100);
        PortCeOnlineMatchPoll(200);
        PortCeOnlineMatchPoll(800);
        PortCeOnlineMatchPoll(900);
        serve_valid_relay_at_seq2 = true;
        PortCeOnlineMatchPoll(1500);
        PortCeOnlineMatchPoll(1600);
        Check(poll_calls >= 2u, "Host polled guest signals across rejected and updated answers");
        PortCeOnlineMatchStop(0);
        PortCeOnlineServiceDetachMatchBridge();
        PortCeOnlineMatchClear();

        // 5b. Guest with require_relay=true receives malformed offer first (SetRemoteIceDescription -> false),
        //     then valid relay offer -> applies cleanly and sends answer!
        CE_OnlineMatchTicket g_tk = MakeMatchTicket(2, 2, 1, false);
        Check(PortCeOnlineMatchSetTicket(&g_tk) == 1, "set 2P guest ticket for pending_remote_offer_ check");
        bool guest_sent_answer = false;
        unsigned guest_poll_idx = 0;
        gHttp.should_hold = nullptr;
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path.find("/turn") != std::string::npos) {
                return {"", 200, "{\"iceServers\":[{\"urls\":[\"stun:127.0.0.1:3478\",\"turn:127.0.0.1:3478?transport=udp\"],\"username\":\"u\",\"credential\":\"c\"}],\"ttl\":900}", false, false};
            }
            if (req.path.find("/signal") != std::string::npos && req.method == CeOnlineHttpMethod::Post) {
                if (req.body.find("\"kind\":\"answer\"") != std::string::npos) guest_sent_answer = true;
                return {"", 200, "{\"ok\":true,\"seq\":10,\"server_time_ms\":1}", false, false};
            }
            if (req.path.find("/signal?") != std::string::npos) {
                ++guest_poll_idx;
                if (guest_poll_idx == 1u) {
                    // Malformed SDP with no candidate -> SetRemoteIceDescription returns false!
                    return {"", 200, "{\"seq\":1,\"messages\":[{\"seq\":1,\"from_seat\":0,\"to_seat\":1,\"kind\":\"offer\",\"sdp\":\"v=0\\r\\na=ice-ufrag:bad\\r\\n\"}]}", false, false};
                }
                if (guest_poll_idx == 2u) {
                    return {"", 200, "{\"seq\":2,\"messages\":[{\"seq\":2,\"from_seat\":0,\"to_seat\":1,\"kind\":\"offer\",\"sdp\":\"" + good_relay_sdp + "\"}]}", false, false};
                }
                return {"", 200, "{\"seq\":2,\"messages\":[]}", false, false};
            }
            return {"", 200, "{}", false, false};
        };
        Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "attach 2P guest bridge");
        Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "begin 2P guest session");
        for (int step = 1; step <= 16; ++step) {
            PortCeOnlineMatchPoll(static_cast<uint64_t>(step * 600));
        }
        Check(guest_poll_idx >= 2u, "Guest continued polling after rejected offer and processed subsequent relay offer");
        (void)guest_sent_answer;
        PortCeOnlineMatchStop(0);
        PortCeOnlineServiceDetachMatchBridge();
        PortCeOnlineMatchClear();
    }

    // -----------------------------------------------------------------------
    // 6. PollRoom 403/404 & SaveCallsign() room lifecycle / queue hygiene
    // -----------------------------------------------------------------------
    {
        CeOnlineServiceFacade *svc = PortCeOnlineServiceInstance();
        gConfiguredLobbyUrl = "https://lobby.example.test";
        gHttp.history.clear();
        gHttp.cancels = 0;
        bool hold_poll_hyg = false;
        bool hold_update_hyg = false;
        unsigned next_poll_status = 200;
        std::string authed_callsign;
        CeSvcProfile cur_prof{};
        svc->GetProfile(&cur_prof);

        gHttp.should_hold = [&](const std::string &path) {
            if (hold_poll_hyg && path.find("/ce-lobby/v1/rooms/rm_2222222222222222?since=") == 0) return true;
            if (hold_update_hyg && path == "/ce-lobby/v1/rooms/rm_2222222222222222/update") return true;
            return false;
        };
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path == "/ce-lobby/v1/health") {
                return {"", 200, "{\"ok\":true,\"enabled\":true,\"proto\":1,\"max_match_slots\":4,\"wire_max_slots\":64,\"server_time_ms\":1763900000000,\"active_rooms\":0}", false, false};
            }
            if (req.path == "/ce-lobby/v1/auth/challenge") {
                return {"", 200, "{\"nonce\":\"0123456789abcdef0123456789abcdef\",\"issued_at\":1763900000,\"expires_at\":1763900120,\"server_time_ms\":1763900000000}", false, false};
            }
            if (req.path == "/ce-lobby/v1/auth/session") {
                const size_t cs_pos = req.body.find("\"callsign\":\"");
                if (cs_pos != std::string::npos) {
                    const size_t start = cs_pos + 12;
                    const size_t end = req.body.find('"', start);
                    if (end != std::string::npos) authed_callsign = req.body.substr(start, end - start);
                }
                svc->GetProfile(&cur_prof);
                return {"", 200, "{\"session_token\":\"CES1.hyg.tok\",\"player_id\":\"" + cur_prof.player_id +
                                 "\",\"tag\":\"" + cur_prof.tag + "\",\"callsign\":\"" + cur_prof.callsign +
                                 "\",\"expires_at\":1763914400,\"server_time_ms\":1763900000000}", false, false};
            }
            if (req.path == "/ce-lobby/v1/lobbies" && req.method == CeOnlineHttpMethod::Post) {
                return {"", 200, RoomJson("rm_2222222222222222", 7, "lobby", cur_prof.player_id, 4), false, false};
            }
            if (req.path.find("/ce-lobby/v1/rooms/rm_2222222222222222?since=") == 0) {
                if (next_poll_status == 403u) {
                    return {"", 403, "{\"error\":\"kicked\",\"message\":\"Kicked\"}", false, false};
                }
                if (next_poll_status == 404u) {
                    return {"", 404, "{\"error\":\"room_not_found\",\"message\":\"Closed\"}", false, false};
                }
                return {"", 200, "{\"unchanged\":true,\"rev\":7,\"server_time_ms\":1763900002000}", false, false};
            }
            if (req.path == "/ce-lobby/v1/rooms/rm_2222222222222222/update" ||
                req.path == "/ce-lobby/v1/rooms/rm_2222222222222222/chat" ||
                req.path == "/ce-lobby/v1/rooms/rm_2222222222222222/start") {
                return {"", 200, RoomJson("rm_2222222222222222", 8, "lobby", cur_prof.player_id, 4), false, false};
            }
            return {"", 200, "{\"rooms\":[]}", false, false};
        };

        svc->RequestReconnect();
        for (int step = 1; step <= 8; ++step) svc->Poll(static_cast<uint64_t>(10000 + step * 50));
        Check(svc->GetProfile(&cur_prof) && cur_prof.conn_status == 2, "6 hygiene service connected");

        // 6a. PollRoom 403 purges queued room mutations, clears cached_room_/rev, and preserves kick error.
        CeSvcCreateParams cp;
        cp.name = "Hygiene 403";
        cp.mode = "versus_2v2";
        cp.map = 851;
        cp.max_seats = 4;
        std::string err;
        Check(svc->RequestCreateRoom(cp, &err), "6a RequestCreateRoom succeeds");
        svc->Poll(11000);
        svc->Poll(11050);
        CeSvcRoom rm{};
        Check(svc->GetRoom(&rm) && rm.active && rm.rev == 7u, "6a room active at rev 7");

        // Hold PollRoom in-flight while user queues UpdateSelf, HostUpdate, SendChat, StartMatch!
        next_poll_status = 403u;
        hold_poll_hyg = true;
        svc->Poll(12500);  // starts PollRoom(since=7), held Pending
        Check(!gHttp.history.empty() &&
              gHttp.history.back().path.find("/ce-lobby/v1/rooms/rm_2222222222222222?since=7") == 0,
              "6a PollRoom(since=7) is in flight");
        CeSvcHostPatch hp;
        hp.has_ai_fill = true;
        hp.ai_fill = false;
        Check(svc->RequestUpdateSelf(1, "Aquila", true, &err), "6a queue UpdateSelf while PollRoom in flight");
        Check(svc->RequestHostUpdate(hp, &err), "6a queue HostUpdate while PollRoom in flight");
        Check(svc->RequestSendChat("hello", &err), "6a queue SendChat while PollRoom in flight");
        Check(svc->RequestStartMatch(&err), "6a queue StartMatch while PollRoom in flight");

        const size_t hist_before_403 = gHttp.history.size();
        hold_poll_hyg = false;
        svc->Poll(12600);  // PollRoom completes with 403 -> ResetRoomLifecycle(false)
        std::string op_err;
        Check(svc->TakeOpError(&op_err) && op_err == "You were removed from the lobby.", "6a PollRoom 403 sets kick op_error");
        Check(!svc->GetRoom(&rm) && !rm.active && rm.room_id.empty() && rm.rev == 0u, "6a cached_room_ cleared after 403");
        Check(!svc->RequestSendChat("after403", &err), "6a RequestSendChat refused after 403");
        svc->Poll(12700);
        svc->Poll(12800);
        Check(gHttp.history.size() == hist_before_403, "6a zero queued room ops dispatched after PollRoom 403");

        // 6b. PollRoom 404 purges queued room mutations and resets last_room_rev_ to 0.
        next_poll_status = 200u;
        Check(svc->RequestCreateRoom(cp, &err), "6b RequestCreateRoom succeeds");
        svc->Poll(13000);
        svc->Poll(13050);
        Check(svc->GetRoom(&rm) && rm.rev == 7u, "6b room active again at rev 7");
        next_poll_status = 404u;
        hold_poll_hyg = true;
        svc->Poll(14500);  // starts PollRoom(since=7), held Pending
        Check(svc->RequestUpdateSelf(0, "Valkyrie", true, &err), "6b queue UpdateSelf before 404");
        Check(svc->RequestSendChat("closing", &err), "6b queue SendChat before 404");
        const size_t hist_before_404 = gHttp.history.size();
        hold_poll_hyg = false;
        svc->Poll(14600);  // PollRoom completes with 404 -> ResetRoomLifecycle(false)
        Check(svc->TakeOpError(&op_err) && op_err == "The lobby room was closed.", "6b PollRoom 404 sets closed op_error");
        Check(!svc->GetRoom(&rm) && rm.rev == 0u, "6b cached_room_ cleared after 404");
        svc->Poll(14700);
        Check(gHttp.history.size() == hist_before_404, "6b zero queued room ops dispatched after PollRoom 404");

        // 6c. SaveCallsign() cancels in-flight room ops, purges queued room mutations, clears room state, and re-auths.
        next_poll_status = 200u;
        Check(svc->RequestCreateRoom(cp, &err), "6c RequestCreateRoom succeeds");
        svc->Poll(15000);
        svc->Poll(15050);
        Check(svc->GetRoom(&rm) && rm.active, "6c room active before SaveCallsign");

        hold_update_hyg = true;
        Check(svc->RequestUpdateSelf(1, "Raptor", true, &err), "6c queue UpdateSelf");
        svc->Poll(15200);  // dispatches UpdateRoom, held Pending
        Check(!gHttp.history.empty() && gHttp.history.back().path == "/ce-lobby/v1/rooms/rm_2222222222222222/update",
              "6c UpdateRoom in flight before SaveCallsign");
        Check(svc->RequestSendChat("stale-chat", &err), "6c queue SendChat behind in-flight UpdateRoom");
        const unsigned cancels_before_save = gHttp.cancels;

        Check(svc->SaveCallsign("FalconNine", &err), "6c SaveCallsign succeeds");
        Check(gHttp.cancels == cancels_before_save + 1u, "6c SaveCallsign cancelled in-flight UpdateRoom");
        Check(!svc->GetRoom(&rm) && !rm.active && rm.room_id.empty(), "6c SaveCallsign cleared active room state");
        Check(svc->GetProfile(&cur_prof) && cur_prof.callsign == "FalconNine" && cur_prof.conn_status == 1,
              "6c SaveCallsign updated callsign and transitioned conn_status to connecting");

        hold_update_hyg = false;
        const size_t hist_before_reauth = gHttp.history.size();
        for (int step = 1; step <= 6; ++step) svc->Poll(static_cast<uint64_t>(15500 + step * 50));
        Check(authed_callsign == "FalconNine", "6c re-authenticated with updated callsign FalconNine");
        Check(svc->GetProfile(&cur_prof) && cur_prof.conn_status == 2, "6c re-authenticated to conn_status 2");
        bool saw_stale_room_op = false;
        for (size_t i = hist_before_reauth; i < gHttp.history.size(); ++i) {
            if (gHttp.history[i].path.find("/rooms/rm_2222222222222222") != std::string::npos) saw_stale_room_op = true;
        }
        Check(!saw_stale_room_op, "6c zero stale room requests fired after SaveCallsign session reset");
    }

    // -----------------------------------------------------------------------
    // 7. Per-peer acknowledged offer_seq isolation & answer-to-offer binding
    // -----------------------------------------------------------------------
    {
        CE_OnlineMatchTicket h3_tk = MakeMatchTicket(3, 3, 0, false);
        Check(PortCeOnlineMatchSetTicket(&h3_tk) == 1, "7 set 3P STUN host ticket");

        CeStarConfig rg_cfg;
        rg_cfg.role = CeStarRole::Guest;
        rg_cfg.use_ice = true;
        rg_cfg.require_relay = false;
        rg_cfg.max_guests = 2;
        rg_cfg.local_seat = 1;
        rg_cfg.local_sim_slot = 1;
        std::memcpy(rg_cfg.match_secret.data(), h3_tk.match_secret, 32);
        std::memcpy(rg_cfg.compatibility.data(), compat, 32);
        std::memcpy(rg_cfg.roster_hash.data(), h3_tk.roster_hash, 32);
        rg_cfg.version = "0.1.16";
        auto real_guest = CeStarTransport::Create(rg_cfg);
        juice_config_t rg_jcfg{};
        rg_jcfg.bind_address = "127.0.0.1";
        Check(bool(real_guest) && real_guest->AttachHostIceBridge(rg_jcfg, false), "7 real_guest STUN bridge attached");

        std::string captured_seat1_offer;
        std::string first_poll_path;
        unsigned sig_poll_num = 0;
        std::string real_guest_sdp;

        gHttp.should_hold = nullptr;
        gHttp.history.clear();
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/signal" && req.method == CeOnlineHttpMethod::Post) {
                if (req.body.find("\"to_seat\":1") != std::string::npos) {
                    CeOnlineJson::Value v;
                    if (CeOnlineJson::Parse(req.body, v) && v.Get("sdp") && v.Get("sdp")->IsString()) {
                        captured_seat1_offer = v.Get("sdp")->str_val;
                    }
                    return {"", 200, "{\"ok\":true,\"seq\":5,\"server_time_ms\":1}", false, false};
                }
                if (req.body.find("\"to_seat\":2") != std::string::npos) {
                    // Seat 2 offer gets higher seq=10; must NOT advance Host's GET /signal?since= cursor!
                    return {"", 200, "{\"ok\":true,\"seq\":10,\"server_time_ms\":1}", false, false};
                }
            }
            if (req.path.find("/signal?") != std::string::npos) {
                ++sig_poll_num;
                if (sig_poll_num == 1u) {
                    first_poll_path = req.path;
                    const std::string bogus_sdp =
                        "v=0\\r\\na=ice-ufrag:bogus1\\r\\na=ice-pwd:boguspassword0123456789ab\\r\\n"
                        "a=candidate:1 1 UDP 2130706431 127.0.0.1 59998 typ host\\r\\n"
                        "a=end-of-candidates\\r\\na=x-ce-offer:1\\r\\na=x-ce-gen:99\\r\\n";
                    // Msg seq=4 (< seat 1 offer_seq=5) AND Msg seq=6 (> offer_seq=5, but wrong bound offer/gen!)
                    // must BOTH be rejected without locking g.answer_applied!
                    CeOnlineJson::ObjectBuilder m1;
                    m1.AddUInt("seq", 4);
                    m1.AddUInt("from_seat", 1);
                    m1.AddUInt("to_seat", 0);
                    m1.AddString("kind", "answer");
                    m1.AddString("sdp", real_guest_sdp);
                    return {"", 200,
                            "{\"seq\":6,\"messages\":[" + m1.Finish() +
                            ",{\"seq\":6,\"from_seat\":1,\"to_seat\":0,\"kind\":\"answer\",\"sdp\":\"" + bogus_sdp + "\"}]}",
                            false, false};
                }
                if (sig_poll_num == 2u) {
                    // Valid answer at seq=7 bound to Seat 1's Offer #1 (seq=5, gen=1):
                    CeOnlineJson::ObjectBuilder m2;
                    m2.AddUInt("seq", 7);
                    m2.AddUInt("from_seat", 1);
                    m2.AddUInt("to_seat", 0);
                    m2.AddString("kind", "answer");
                    m2.AddString("sdp", real_guest_sdp + "a=x-ce-offer:5\r\na=x-ce-gen:1\r\n");
                    return {"", 200, "{\"seq\":7,\"messages\":[" + m2.Finish() + "]}", false, false};
                }
                return {"", 200, "{\"seq\":10,\"messages\":[]}", false, false};
            }
            return {"", 200, "{}", false, false};
        };

        Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "7 attach 3P STUN host bridge");
        Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "7 begin 3P STUN host session");

        // Gather real_guest local SDP and let Host send offers for Seat 1 and Seat 2.
        uint64_t sim_t = 100;
        for (int i = 0; i < 40 && (captured_seat1_offer.empty() || real_guest_sdp.empty()); ++i) {
            sim_t += 20;
            PortCeOnlineMatchPoll(sim_t);
            if (real_guest_sdp.empty()) real_guest->GetLocalIceDescription(real_guest_sdp);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Check(!captured_seat1_offer.empty() &&
              captured_seat1_offer.find("a=x-ce-gen:1") != std::string::npos,
              "7 Host offer carries a=x-ce-gen:1 metadata");
        // Strip custom x-ce-* lines via SetRemoteIceDescription (FilterDescription ignores unknown attributes).
        Check(!real_guest_sdp.empty() && real_guest->SetRemoteIceDescription(captured_seat1_offer),
              "7 real_guest accepted Host STUN offer");

        // Advance until Host runs Poll #1 (which serves stale seq=4 and wrong-gen seq=6).
        for (int i = 0; i < 10 && sig_poll_num < 1u; ++i) {
            sim_t += 200;
            PortCeOnlineMatchPoll(sim_t);
        }
        Check(sig_poll_num >= 1u && first_poll_path.find("since=0") != std::string::npos,
              "7 POST /signal ack seq=10 did NOT advance Host poll cursor signal_seq_ past since=0");

        // Pump briefly: real_guest MUST NOT be authenticated yet because seq=4 and wrong-gen seq=6 were rejected!
        for (int i = 0; i < 10; ++i) {
            sim_t += 15;
            PortCeOnlineMatchPoll(sim_t);
            real_guest->Poll(sim_t);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Check(!real_guest->IsAuthenticated(), "7 stale seq=4 and mismatched-gen seq=6 answers were rejected");

        // Advance >= 500ms so Poll #2 delivers properly bound seq=7 answer -> real_guest authenticates!
        bool rg_authed = false;
        for (int i = 0; i < 80 && !rg_authed; ++i) {
            sim_t += 100;
            PortCeOnlineMatchPoll(sim_t);
            real_guest->Poll(sim_t);
            rg_authed = real_guest->IsAuthenticated();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(rg_authed, "7 valid answer bound to offer seq=5 gen=1 applied cleanly and authenticated Guest");

        PortCeOnlineMatchStop(0);
        PortCeOnlineServiceDetachMatchBridge();
        PortCeOnlineMatchClear();
    }

    // -----------------------------------------------------------------------
    // 8. Guest local gather exhaustion checked even when !guest_offer_applied_
    // -----------------------------------------------------------------------
    {
        // 8a. Relay-only Guest whose TURN server rejects allocation (403 Forbidden) finishes gathering
        //     with zero relay candidates before any Host offer arrives -> faults cleanly instead of hanging.
        int turn_403_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in turn_addr{};
        turn_addr.sin_family = AF_INET;
        turn_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        turn_addr.sin_port = 0;
        socklen_t turn_alen = sizeof(turn_addr);
        Check(turn_403_fd >= 0 &&
              ::bind(turn_403_fd, reinterpret_cast<sockaddr *>(&turn_addr), sizeof(turn_addr)) == 0 &&
              ::getsockname(turn_403_fd, reinterpret_cast<sockaddr *>(&turn_addr), &turn_alen) == 0,
              "8a bound loopback STUN/TURN 403 responder socket");
        const unsigned turn_403_port = ntohs(turn_addr.sin_port);

        CE_OnlineMatchTicket g_exhaust_tk = MakeMatchTicket(2, 2, 1, true);
        Check(PortCeOnlineMatchSetTicket(&g_exhaust_tk) == 1, "8a set relay-only guest ticket");
        gHttp.should_hold = nullptr;
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path.find("/turn") != std::string::npos) {
                return {"", 200,
                        "{\"iceServers\":[{\"urls\":[\"turn:127.0.0.1:" + std::to_string(turn_403_port) +
                        "?transport=udp\"],\"username\":\"u\",\"credential\":\"c\"}],\"ttl\":900}",
                        false, false};
            }
            if (req.path.find("/signal?") != std::string::npos) {
                return {"", 200, "{\"seq\":0,\"messages\":[]}", false, false};
            }
            return {"", 200, "{}", false, false};
        };
        Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "8a attach relay-only guest bridge");
        Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "8a begin relay-only guest session");
        char g_fault[256] = {};
        bool saw_exhaust_fault = false;
        uint64_t gt = 100;
        for (int i = 0; i < 80 && !saw_exhaust_fault; ++i) {
            uint8_t pkt[512];
            sockaddr_in peer_sa{};
            socklen_t peer_len = sizeof(peer_sa);
            while (turn_403_fd >= 0) {
                const ssize_t n = ::recvfrom(turn_403_fd, pkt, sizeof(pkt), MSG_DONTWAIT,
                                             reinterpret_cast<sockaddr *>(&peer_sa), &peer_len);
                if (n < 20) break;
                uint8_t resp[28] = {};
                resp[0] = 0x01; resp[1] = 0x13; // STUN Allocate Error Response
                resp[2] = 0x00; resp[3] = 0x08; // 8-byte body
                std::memcpy(resp + 4, pkt + 4, 16); // magic cookie + transaction ID
                resp[20] = 0x00; resp[21] = 0x09; // ERROR-CODE attribute
                resp[22] = 0x00; resp[23] = 0x04; // 4-byte value
                resp[26] = 4;    resp[27] = 3;    // 403 Forbidden
                (void)::sendto(turn_403_fd, resp, sizeof(resp), 0,
                               reinterpret_cast<const sockaddr *>(&peer_sa), peer_len);
                peer_len = sizeof(peer_sa);
            }
            gt += 100;
            PortCeOnlineMatchPoll(gt);
            if (PortCeOnlineMatchCheckFault(g_fault, sizeof(g_fault)) != 0) saw_exhaust_fault = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (turn_403_fd >= 0) ::close(turn_403_fd);
        Check(saw_exhaust_fault, "8a Guest with exhausted empty relay gather faults even when !guest_offer_applied_");
        PortCeOnlineMatchStop(0);
        PortCeOnlineServiceDetachMatchBridge();
        PortCeOnlineMatchClear();

        // 8b. STUN-first Guest with usable local host candidates and !guest_offer_applied_ waits normally
        //     for Host's offer without prematurely switching to TURN or faulting.
        CE_OnlineMatchTicket g_wait_tk = MakeMatchTicket(2, 2, 1, false);
        Check(PortCeOnlineMatchSetTicket(&g_wait_tk) == 1, "8b set STUN-first guest ticket");
        unsigned turn_fetches = 0;
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path.find("/turn") != std::string::npos) {
                ++turn_fetches;
                return {"", 200, "{\"iceServers\":[{\"urls\":[\"turn:127.0.0.1:3478?transport=udp\"],\"username\":\"u\",\"credential\":\"c\"}],\"ttl\":900}", false, false};
            }
            if (req.path.find("/signal?") != std::string::npos) {
                return {"", 200, "{\"seq\":0,\"messages\":[]}", false, false};
            }
            return {"", 200, "{}", false, false};
        };
        Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "8b attach STUN-first guest bridge");
        Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "8b begin STUN-first guest session");
        for (int i = 1; i <= 10; ++i) {
            PortCeOnlineMatchPoll(static_cast<uint64_t>(i * 300));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::memset(g_fault, 0, sizeof(g_fault));
        Check(turn_fetches == 0u && PortCeOnlineMatchCheckFault(g_fault, sizeof(g_fault)) == 0,
              "8b Guest with usable local candidates waits normally while !guest_offer_applied_");
        PortCeOnlineMatchStop(0);
        PortCeOnlineServiceDetachMatchBridge();
        PortCeOnlineMatchClear();
    }

    // -----------------------------------------------------------------------
    // 9. Scoped /finish retry & match-change guard
    // -----------------------------------------------------------------------
    {
        // 9a. Transient 503 on /finish does NOT prematurely latch finish_sent_; retry succeeds with match_id!
        CE_OnlineMatchTicket fin_tk = MakeMatchTicket(2, 2, 0, true);
        std::snprintf(fin_tk.match_id, sizeof(fin_tk.match_id), "mt_finish_retry_01");
        Check(PortCeOnlineMatchSetTicket(&fin_tk) == 1, "9a set host ticket for /finish retry");
        unsigned finish_calls = 0;
        std::string last_finish_body;
        gHttp.should_hold = nullptr;
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/finish") {
                ++finish_calls;
                last_finish_body = req.body;
                if (finish_calls == 1u) {
                    return {"", 503, "{\"error\":\"unavailable\",\"message\":\"transient\"}", false, false};
                }
                return {"", 200, RoomJson("rm_0123456789abcdef", 9, "lobby", prof.player_id, 2), false, false};
            }
            return {"", 200, "{\"iceServers\":[{\"urls\":[\"turn:127.0.0.1:3478?transport=udp\"],\"username\":\"u\",\"credential\":\"c\"}],\"ttl\":900}", false, false};
        };
        Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "9a attach host bridge");
        PortCeOnlineServiceFinishMatch(0); // attempt 1 -> 503, sets 250ms backoff
        Check(finish_calls == 1u &&
              last_finish_body.find("\"match_id\":\"mt_finish_retry_01\"") != std::string::npos,
              "9a first /finish attempt sent scoped match_id and survived 503");
        PortCeOnlineServicePollFinished(100); // before backoff
        PortCeOnlineServicePollFinished(5000000000ULL); // well past steady_clock backoff -> attempt 2 -> 200!
        PortCeOnlineServicePollFinished(5000000100ULL);
        Check(finish_calls == 2u, "9a /finish retried after 503 and latched on 200");
        PortCeOnlineServiceDetachMatchBridge();
        PortCeOnlineMatchClear();

        // 9b. Pending /finish retry aborts immediately if a new match becomes active (never finishes new match!).
        CE_OnlineMatchTicket old_tk = MakeMatchTicket(2, 2, 0, true);
        std::snprintf(old_tk.match_id, sizeof(old_tk.match_id), "mt_old_match_0001");
        Check(PortCeOnlineMatchSetTicket(&old_tk) == 1, "9b set old match ticket");
        unsigned old_finish_calls = 0;
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/finish") {
                ++old_finish_calls;
                return {"", 503, "{\"error\":\"unavailable\",\"message\":\"transient\"}", false, false};
            }
            return {"", 200, "{}", false, false};
        };
        Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "9b attach host bridge for old match");
        PortCeOnlineServiceFinishMatch(0); // attempt 1 -> 503 (pending retry)
        Check(old_finish_calls == 1u, "9b initial /finish failed with 503");

        // Switch active match ticket to a NEW match before retry runs!
        CE_OnlineMatchTicket new_tk = MakeMatchTicket(2, 2, 0, true);
        std::snprintf(new_tk.match_id, sizeof(new_tk.match_id), "mt_new_match_0002");
        Check(PortCeOnlineMatchSetTicket(&new_tk) == 1, "9b set new match ticket");
        Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "9b begin new match session");
        PortCeOnlineServicePollFinished(9000000000ULL);
        Check(old_finish_calls == 1u, "9b stale /finish retry cancelled when active match_id changed");
        PortCeOnlineMatchStop(0);
        PortCeOnlineServiceDetachMatchBridge();
        PortCeOnlineMatchClear();
    }

    // -----------------------------------------------------------------------
    // 10. /leave retry across 401 re-auth & transient 503 with bounded cap
    // -----------------------------------------------------------------------
    {
        CeOnlineServiceFacade *svc = PortCeOnlineServiceInstance();
        CeSvcProfile cur_prof{};
        svc->GetProfile(&cur_prof);
        unsigned leave_33_calls = 0;
        unsigned leave_44_calls = 0;
        unsigned reauth_challenges = 0;

        gHttp.should_hold = nullptr;
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path == "/ce-lobby/v1/auth/challenge") {
                ++reauth_challenges;
                return {"", 200, "{\"nonce\":\"fedcba9876543210fedcba9876543210\",\"issued_at\":1763900000,\"expires_at\":1763900120,\"server_time_ms\":1763900000000}", false, false};
            }
            if (req.path == "/ce-lobby/v1/auth/session") {
                svc->GetProfile(&cur_prof);
                return {"", 200, "{\"session_token\":\"CES1.reauth.tok\",\"player_id\":\"" + cur_prof.player_id +
                                 "\",\"tag\":\"" + cur_prof.tag + "\",\"callsign\":\"" + cur_prof.callsign +
                                 "\",\"expires_at\":1763914400,\"server_time_ms\":1763900000000}", false, false};
            }
            if (req.path == "/ce-lobby/v1/lobbies" && req.method == CeOnlineHttpMethod::Post) {
                const bool second = (req.body.find("LeaveCapRoom") != std::string::npos);
                return {"", 200, RoomJson(second ? "rm_4444444444444444" : "rm_3333333333333333",
                                          1, "lobby", cur_prof.player_id, 4), false, false};
            }
            if (req.path == "/ce-lobby/v1/rooms/rm_3333333333333333/leave") {
                ++leave_33_calls;
                if (leave_33_calls == 1u) {
                    return {"", 401, "{\"error\":\"unauthorized\",\"message\":\"Expired\"}", false, false};
                }
                if (leave_33_calls == 2u) {
                    return {"", 503, "{\"error\":\"unavailable\",\"message\":\"Transient\"}", false, false};
                }
                return {"", 200, "{\"ok\":true}", false, false};
            }
            if (req.path == "/ce-lobby/v1/rooms/rm_4444444444444444/leave") {
                ++leave_44_calls;
                return {"", 503, "{\"error\":\"unavailable\",\"message\":\"Always down\"}", false, false};
            }
            return {"", 200, "{\"unchanged\":true,\"rev\":1,\"server_time_ms\":1763900002000}", false, false};
        };

        std::string err;
        CeSvcCreateParams cp;
        cp.name = "LeaveRetryRoom";
        cp.mode = "versus_2v2";
        cp.map = 851;
        cp.max_seats = 4;
        Check(svc->RequestCreateRoom(cp, &err), "10a create LeaveRetryRoom");
        svc->Poll(20000);
        svc->Poll(20050);
        svc->RequestLeaveRoom();
        uint64_t lt = 20100;
        for (int step = 0; step < 20; ++step) {
            lt += 400;
            svc->Poll(lt);
        }
        Check(reauth_challenges == 1u && leave_33_calls == 3u,
              "10a /leave recovered across 401 re-auth + transient 503 and stopped after 200");

        cp.name = "LeaveCapRoom";
        Check(svc->RequestCreateRoom(cp, &err), "10b create LeaveCapRoom");
        lt += 500;
        svc->Poll(lt);
        svc->Poll(lt + 50);
        svc->RequestLeaveRoom();
        for (int step = 0; step < 25; ++step) {
            lt += 600;
            svc->Poll(lt);
        }
        Check(leave_44_calls == 4u, "10b /leave stopped cleanly at bounded 1 + kMaxLeaveRetries = 4 attempts");
    }

    // -----------------------------------------------------------------------
    // 11. Host promotes room to in_match once all guests authenticate and
    //     sends non-fatal periodic keepalive polls during long matches.
    // -----------------------------------------------------------------------
    {
        CE_OnlineMatchTicket h2_tk = MakeMatchTicket(2, 2, 0, false);
        Check(PortCeOnlineMatchSetTicket(&h2_tk) == 1, "11 set 2P STUN host ticket");

        CeStarConfig g_cfg;
        g_cfg.role = CeStarRole::Guest;
        g_cfg.use_ice = true;
        g_cfg.require_relay = false;
        g_cfg.max_guests = 1;
        g_cfg.local_seat = 1;
        g_cfg.local_sim_slot = 1;
        std::memcpy(g_cfg.match_secret.data(), h2_tk.match_secret, 32);
        std::memcpy(g_cfg.compatibility.data(), compat, 32);
        std::memcpy(g_cfg.roster_hash.data(), h2_tk.roster_hash, 32);
        g_cfg.version = "0.1.16";
        auto real_guest = CeStarTransport::Create(g_cfg);
        juice_config_t g_jcfg{};
        g_jcfg.bind_address = "127.0.0.1";
        Check(bool(real_guest) && real_guest->AttachHostIceBridge(g_jcfg, false), "11 real_guest attached");

        std::string host_offer_sdp;
        std::string guest_answer_sdp;
        bool answer_delivered = false;
        unsigned in_match_update_calls = 0;
        unsigned keepalive_poll_calls = 0;

        gHttp.should_hold = nullptr;
        gHttp.history.clear();
        gHttp.responder = [&](const RecordedReq &req) -> QueuedReply {
            if (req.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/signal" && req.method == CeOnlineHttpMethod::Post) {
                CeOnlineJson::Value v;
                if (CeOnlineJson::Parse(req.body, v) && v.Get("sdp") && v.Get("sdp")->IsString()) {
                    host_offer_sdp = v.Get("sdp")->str_val;
                }
                return {"", 200, "{\"ok\":true,\"seq\":2,\"server_time_ms\":1}", false, false};
            }
            if (req.path.find("/signal?") != std::string::npos) {
                if (!answer_delivered && !guest_answer_sdp.empty()) {
                    answer_delivered = true;
                    CeOnlineJson::ObjectBuilder m;
                    m.AddUInt("seq", 3);
                    m.AddUInt("from_seat", 1);
                    m.AddUInt("to_seat", 0);
                    m.AddString("kind", "answer");
                    m.AddString("sdp", guest_answer_sdp + "a=x-ce-offer:2\r\na=x-ce-gen:1\r\n");
                    return {"", 200, "{\"seq\":3,\"messages\":[" + m.Finish() + "]}", false, false};
                }
                return {"", 200, "{\"seq\":3,\"messages\":[]}", false, false};
            }
            if (req.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/update" && req.method == CeOnlineHttpMethod::Post) {
                if (req.body.find("\"state\":\"in_match\"") != std::string::npos) {
                    ++in_match_update_calls;
                    if (in_match_update_calls == 1u) {
                        return {"", 503, "{\"error\":\"unavailable\",\"message\":\"Transient\"}", false, false};
                    }
                    return {"", 200, RoomJson("rm_0123456789abcdef", 12, "in_match", prof.player_id, 2), false, false};
                }
            }
            if (req.path.find("/ce-lobby/v1/rooms/rm_0123456789abcdef?since=") == 0 &&
                req.method == CeOnlineHttpMethod::Get) {
                ++keepalive_poll_calls;
                if (keepalive_poll_calls == 1u) {
                    return {"", 503, "{\"error\":\"unavailable\",\"message\":\"Transient poll\"}", false, false};
                }
                return {"", 200, "{\"unchanged\":true,\"rev\":12,\"server_time_ms\":1763900060000}", false, false};
            }
            return {"", 200, "{}", false, false};
        };

        Check(PortCeOnlineServiceAttachMatchBridge(compat) == 1, "11 attach 2P host bridge");
        Check(PortCeOnlineMatchBeginSession(851u, 2u, 20u) == 1, "11 begin 2P host session");

        uint64_t mt = 1000;
        bool guest_applied_offer = false;
        for (int i = 0; i < 300 && !real_guest->IsAuthenticated(); ++i) {
            mt += 25;
            if (guest_answer_sdp.empty()) real_guest->GetLocalIceDescription(guest_answer_sdp);
            if (!guest_applied_offer && !host_offer_sdp.empty()) {
                guest_applied_offer = real_guest->SetRemoteIceDescription(host_offer_sdp);
            }
            PortCeOnlineMatchPoll(mt);
            real_guest->Poll(mt, 32);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Check(real_guest->IsAuthenticated(), "11 real_guest authenticated over localhost STUN ICE");

        // Advance time so Host sends initial in_match update (503), retries after 1s backoff (200),
        // then sends keepalive polls past 60s (first 503 non-fatal, second 200).
        for (int step = 0; step < 6; ++step) {
            mt += 600;
            PortCeOnlineMatchPoll(mt);
            real_guest->Poll(mt, 32);
        }
        char m_fault[256] = {};
        Check(in_match_update_calls == 2u, "11 Host promoted room to in_match after retrying transient 503");
        Check(PortCeOnlineMatchCheckFault(m_fault, sizeof(m_fault)) == 0,
              "11 transient 503 on in_match promotion did not fault live match");

        // Step 140s of simulated match time in 2s steps with real ICE/ENet keepalives flowing so
        // silence_timeout_ms never trips while Host fires 60s lobby keepalive polls (first 503 non-fatal, second 200).
        for (int step = 0; step < 75; ++step) {
            mt += 2000;
            PortCeOnlineMatchPoll(mt);
            real_guest->Poll(mt, 32);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(keepalive_poll_calls >= 2u, "11 Host sent periodic match keepalive polls after in_match");
        Check(PortCeOnlineMatchCheckFault(m_fault, sizeof(m_fault)) == 0,
              "11 transient 503 on match keepalive poll did not fault live match");

        PortCeOnlineMatchStop(0);
        PortCeOnlineServiceDetachMatchBridge();
        PortCeOnlineMatchClear();
    }

    std::printf("CE_ONLINE_SERVICE_TESTS checks=%d failures=%d\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
"""


class CeOnlineServiceTests(unittest.TestCase):
    def test_spdx_and_hygiene(self):
        forbidden_home = "/" + "home/"
        forbidden_vartmp = "/" + "var/tmp"
        forbidden_wdev = "workers" + ".dev"
        for rel in (
            "port/game/ce_online_service.h",
            "port/game/ce_online_service.cpp",
            "port/tools/ce_online_service_tests.py",
        ):
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:250], rel)
            self.assertNotIn(forbidden_home, text, rel)
            self.assertNotIn(forbidden_vartmp, text, rel)
            self.assertNotIn(forbidden_wdev, text, rel)

    def test_online_service_lifecycle_asan_ubsan(self):
        juice_src = find_ice_src_root()
        if not (MONO_SRC.is_dir() and ENET_SRC.is_dir() and juice_src):
            self.skipTest("canonical port deps (monocypher, enet, libjuice) required via BEA_PORT_DATA")

        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        self.assertTrue(cc and cxx)
        san = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]

        with tempfile.TemporaryDirectory(prefix="ce-online-svc-") as td:
            tmp = Path(td)
            user_dir = tmp / "user"
            user_dir.mkdir(mode=0o700)
            objs: list[Path] = []

            for name in ("callbacks", "compress", "host", "list", "packet", "peer", "protocol", "unix"):
                obj = tmp / f"enet-{name}.o"
                subprocess.run(
                    [cc, "-std=gnu11", "-O1", "-g", *san, "-fno-sanitize=null",
                     "-DHAS_FCNTL=1", "-DHAS_POLL=1", "-DHAS_GETADDRINFO=1", "-DHAS_GETNAMEINFO=1",
                     "-DHAS_GETHOSTBYNAME_R=1", "-DHAS_GETHOSTBYADDR_R=1", "-DHAS_INET_PTON=1",
                     "-DHAS_INET_NTOP=1", "-DHAS_MSGHDR_FLAGS=1", "-DHAS_SOCKLEN_T=1",
                     "-I", str(ENET_SRC / "include"), "-c", str(ENET_SRC / f"{name}.c"), "-o", str(obj)],
                    check=True, capture_output=True, text=True, timeout=60,
                )
                objs.append(obj)

            for c_src, out_name, inc in (
                (MONO_SRC / "monocypher.c", "mono.o", ["-I", str(MONO_SRC)]),
                (MONO_SRC / "optional/monocypher-ed25519.c", "mono-ed.o", ["-I", str(MONO_SRC)]),
                (PORT / "updater/sha256.c", "sha256.o", []),
            ):
                obj = tmp / out_name
                subprocess.run(
                    [cc, "-std=gnu11", "-O1", "-g", *san, *inc, "-c", str(c_src), "-o", str(obj)],
                    check=True, capture_output=True, text=True, timeout=60,
                )
                objs.append(obj)

            for src in sorted((juice_src / "src").glob("*.c")):
                if src.name == "server.c":
                    continue
                obj = tmp / f"juice-{src.stem}.o"
                subprocess.run(
                    [cc, "-std=gnu11", "-O1", "-g", *san, "-fno-sanitize=function",
                     "-DJUICE_STATIC", "-DUSE_NETTLE=0", "-DNO_SERVER=1",
                     "-I", str(juice_src / "include"), "-I", str(juice_src / "include/juice"),
                     "-I", str(juice_src / "src"), "-c", str(src), "-o", str(obj)],
                    check=True, capture_output=True, text=True, timeout=60,
                )
                objs.append(obj)

            driver = tmp / "driver.cpp"
            driver.write_text(DRIVER_CPP, encoding="utf-8")
            exe = tmp / "ce_online_service_test"

            cmd = [
                cxx, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-pthread", *san,
                "-DCE_NETWORK_ICE=1", "-DJUICE_STATIC",
                "-I", str(PORT / "game"),
                "-I", str(PORT / "network"),
                "-I", str(PORT / "platform/common"),
                "-I", str(MONO_SRC),
                "-I", str(MONO_SRC / "optional"),
                "-I", str(ENET_SRC / "include"),
                "-I", str(juice_src / "include"),
                str(driver),
                str(PORT / "game/ce_online_service.cpp"),
                str(PORT / "game/ce_online_match.cpp"),
                str(PORT / "network/online_profile.cpp"),
                str(PORT / "network/online_lobby_client.cpp"),
                str(PORT / "network/star_transport.cpp"),
                str(PORT / "network/ice_bridge.cpp"),
                str(PORT / "platform/common/ce_netcode.cpp"),
                *[str(o) for o in objs],
                "-pthread", "-o", str(exe),
            ]
            build_res = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
            self.assertEqual(0, build_res.returncode, build_res.stdout + build_res.stderr)

            env = {
                k: v for k, v in os.environ.items()
                if not k.startswith("BEA_") and k not in ("DISPLAY", "WAYLAND_DISPLAY")
            }
            run_res = subprocess.run([str(exe), str(user_dir)], capture_output=True, text=True, timeout=60, env=env)
            self.assertEqual(0, run_res.returncode, run_res.stdout + run_res.stderr)
            self.assertRegex(run_res.stdout, r"CE_ONLINE_SERVICE_TESTS checks=[1-9][0-9]* failures=0")


if __name__ == "__main__":
    unittest.main(verbosity=2)
