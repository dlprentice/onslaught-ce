#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for port/network/online_lobby_client.{h,cpp} and CeOnlineHttps in signaling_https.{h,cpp}:
bounded JSON parser/encoder, strict origin/path validation, full PROTOCOL.md client state machine
against mock CeOnlineHttps, real loopback HTTP roundtrip over libcurl-gnutls, Linux32 & Windows32
cross-builds, and causal mutants.
"""
from __future__ import annotations

import http.server
import json
import os
from pathlib import Path
import shutil
import socketserver
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
DATA = Path(os.environ.get("BEA_PORT_DATA", ROOT / "local-data/port"))
MONO_SRC = DATA / "src/monocypher-4.0.2/src"
LLVM_BIN = DATA / "llvm/bin"
SYSROOT = DATA / "sysroot-sniper"
MINGW_BIN = Path(os.environ.get("BEA_LLVM_MINGW", DATA / "llvm-mingw/bin"))

MOCK_DRIVER_CPP = r"""
// SPDX-License-Identifier: GPL-3.0-only
#include "online_lobby_client.h"
#include "online_profile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace {
int gChecks = 0;
int gFails = 0;

void Check(bool cond, const char* msg) {
    ++gChecks;
    if (!cond) {
        ++gFails;
        std::fprintf(stderr, "FAIL: %s\n", msg);
    }
}

struct MockStep {
    CeOnlineHttpMethod expect_method = CeOnlineHttpMethod::Get;
    std::string expect_path;
    bool expect_bearer = false;
    std::string expect_bearer_val;
    std::string expect_body_contains;
    unsigned reply_code = 200;
    std::string reply_body;
    bool fail_transport = false;
    bool hold = false;
};

class MockOnlineHttps final : public CeOnlineHttps {
public:
    std::deque<MockStep> steps;
    std::string last_body;
    std::string last_path;
    bool active = false;
    bool broken = false;
    unsigned cancels = 0;
    uint64_t active_deadline = 0;

    bool Begin(CeOnlineHttpMethod method, const char* pathAndQuery,
               const char* bearer, size_t bearerSize,
               const std::string& body, uint64_t nowMs, uint64_t deadlineMs) noexcept override {
        if (active || steps.empty() || !pathAndQuery || deadlineMs <= nowMs) {
            broken = true;
            return false;
        }
        const auto& s = steps.front();
        if (method != s.expect_method || std::string(pathAndQuery) != s.expect_path) {
            std::fprintf(stderr, "MOCK MISMATCH: got path=%s expected=%s\n", pathAndQuery, s.expect_path.c_str());
            broken = true;
            return false;
        }
        if (s.expect_bearer) {
            if (!bearer || std::string(bearer, bearerSize) != s.expect_bearer_val) {
                std::fprintf(stderr, "MOCK BEARER MISMATCH\n");
                broken = true;
                return false;
            }
        } else if (bearerSize != 0) {
            broken = true;
            return false;
        }
        if (!s.expect_body_contains.empty() && body.find(s.expect_body_contains) == std::string::npos) {
            std::fprintf(stderr, "MOCK BODY MISMATCH: %s missing %s\n", body.c_str(), s.expect_body_contains.c_str());
            broken = true;
            return false;
        }
        last_body = body;
        last_path = pathAndQuery;
        active_deadline = deadlineMs;
        active = true;
        return true;
    }

    CeSignalHttpsState Poll(uint64_t nowMs, CeSignalHttpsReply* reply) noexcept override {
        if (!active || !reply || steps.empty()) return CeSignalHttpsState::Failed;
        if (nowMs >= active_deadline) { active = false; steps.pop_front(); return CeSignalHttpsState::Failed; }
        const auto& s = steps.front();
        if (s.hold) return CeSignalHttpsState::Pending;
        active = false;
        if (s.fail_transport) {
            steps.pop_front();
            return CeSignalHttpsState::Failed;
        }
        reply->code = s.reply_code;
        reply->body = s.reply_body;
        steps.pop_front();
        return CeSignalHttpsState::Ready;
    }

    void Cancel() noexcept override {
        if (active) ++cancels;
        active = false;
    }
};

std::string MakeRoomJson(uint64_t rev, const std::string& state, const std::string& ticket_json,
                         uint16_t max_seats = 4, bool fill_64 = false) {
    std::string seats = "[";
    const uint16_t count = fill_64 ? 64 : max_seats;
    for (uint16_t i = 0; i < count; ++i) {
        if (i) seats += ",";
        seats += "{\"seat\":" + std::to_string(i) + ",\"kind\":\"" + (i == 0 ? "human" : (i == 1 ? "human" : "ai")) +
                 "\",\"player_id\":\"" + (i < 2 ? "0123456789abcdefghjkmnpqrs" : "") +
                 "\",\"tag\":\"" + (i < 2 ? "4F9A" : "") +
                 "\",\"callsign\":\"" + (i == 0 ? "HostPilot" : (i == 1 ? "GuestPilot" : "")) +
                 "\",\"team\":" + std::to_string(i & 1u) +
                 ",\"be_cfg\":\"Aquila\",\"ready\":" + (i < 2 ? "true" : "false") +
                 ",\"rtt_ms\":24,\"route\":\"direct\"}";
    }
    seats += "]";
    return "{\"proto\":1,\"room_id\":\"rm_0123456789abcdef\",\"code\":\"ABCD12\",\"rev\":" +
           std::to_string(rev) + ",\"state\":\"" + state + "\",\"name\":\"Forseti Skirmish\","
           "\"topology\":\"star_lockstep\",\"mode\":\"versus_2v2\",\"map\":850,\"max_seats\":" +
           std::to_string(count) + ",\"host_seat\":0,\"visibility\":\"public\",\"has_password\":false,"
           "\"require_relay\":false,\"ai_fill\":true,\"build_id\":\"0.1.14\",\"content_id\":\"\","
           "\"server_time_ms\":1763900000123,\"teams\":[{\"team\":0,\"side\":\"forseti\",\"cap\":2},"
           "{\"team\":1,\"side\":\"muspell\",\"cap\":2}],\"seats\":" + seats +
           ",\"chat\":[{\"seq\":1,\"seat\":0,\"player_id\":\"0123456789abcdefghjkmnpqrs\","
           "\"callsign\":\"HostPilot\",\"tag\":\"4F9A\",\"text\":\"gl hf\",\"ts\":1763900000}],"
           "\"start_ticket\":" + ticket_json + "}";
}

void TestJsonBounds() {
    CeOnlineJson::Value v;
    Check(CeOnlineJson::Parse("{\"a\":1,\"b\":true,\"c\":null,\"d\":\"hi\\n\\\"x\\\"\",\"e\":[1,2,-3]}", v),
          "valid JSON parses");
    Check(v.IsObject() && v.Get("a")->uint_val == 1 && v.Get("b")->bool_val &&
          v.Get("c")->IsNull() && v.Get("d")->str_val == "hi\n\"x\"" &&
          v.Get("e")->arr_val[2].int_val == -3, "parsed JSON values match");

    // Duplicate key rejected.
    Check(!CeOnlineJson::Parse("{\"k\":1,\"k\":2}", v), "duplicate JSON key rejected");
    // Depth > 12 rejected.
    std::string deep(14, '[');
    deep += "1";
    deep += std::string(14, ']');
    Check(!CeOnlineJson::Parse(deep, v), "excessive JSON depth rejected");
    // Oversize > 65536 rejected.
    std::string huge = "{\"s\":\"" + std::string(70000, 'a') + "\"}";
    Check(!CeOnlineJson::Parse(huge, v), "oversized JSON rejected");
    // Leading zero rejected.
    Check(!CeOnlineJson::Parse("{\"n\":01}", v), "leading zero integer rejected");
    // Float rejected.
    Check(!CeOnlineJson::Parse("{\"n\":1.5}", v), "float number rejected");
    // Integer overflow rejected.
    Check(!CeOnlineJson::Parse("{\"n\":99999999999999999999999999}", v), "overflow integer rejected");
    // Trailing garbage rejected.
    Check(!CeOnlineJson::Parse("{\"a\":1} trailing", v), "trailing garbage rejected");
    // Unescaped control char rejected.
    Check(!CeOnlineJson::Parse(std::string("{\"a\":\"x") + '\x01' + "\"}", v), "control char rejected");
    // Encoder roundtrip with escaping.
    CeOnlineJson::ObjectBuilder jb;
    jb.AddString("msg", "hello \"world\"\nline2\\end");
    jb.AddUInt("num", 1763900000123ULL);
    jb.AddBool("flag", true);
    const std::string encoded = jb.Finish();
    Check(CeOnlineJson::Parse(encoded, v) && v.Get("msg")->str_val == "hello \"world\"\nline2\\end" &&
          v.Get("num")->uint_val == 1763900000123ULL && v.Get("flag")->bool_val,
          "ObjectBuilder roundtrip preserves special chars and 64-bit timestamps");
}

void TestClientFlow() {
    const uint8_t seed[32] = {
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,
        0x12,0x23,0x34,0x45,0x56,0x67,0x78,0x89,0x9a,0xab,0xbc,0xcd,0xde,0xef,0xf0,0x02
    };
    CeOnlineProfile profile;
    Check(profile.InitFromSeed(seed, "HostPilot"), "profile init from seed");

    auto mock_u = std::make_unique<MockOnlineHttps>();
    auto* mock = mock_u.get();
    CeOnlineLobbyClient client(std::move(mock_u));

    // 1. Health + RTT measurement.
    mock->steps.push_back({CeOnlineHttpMethod::Get, "/ce-lobby/v1/health", false, "", "", 200,
                           "{\"ok\":true,\"enabled\":true,\"proto\":1,\"max_match_slots\":4,"
                           "\"wire_max_slots\":64,\"server_time_ms\":1763900000000,\"active_rooms\":3}",
                           false, false});
    Check(client.BeginHealth(1000), "BeginHealth");
    Check(client.Poll(1037) == CeOnlineOpStatus::Completed, "Health completed");
    Check(client.LastHealth().ok && client.LastHealth().enabled &&
          client.LastHealth().max_match_slots == 4 && client.LastHealth().wire_max_slots == 64 &&
          client.LastHealth().active_rooms == 3 && client.LastHealth().rtt_ms == 37,
          "Health fields and measured RTT ms match");

    // 2. Two-step Challenge -> Ed25519 Sign -> Session.
    const std::string nonce = "00112233445566778899aabbccddeeff";
    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/auth/challenge", false, "",
                           "\"pubkey\":\"" + profile.PubkeyHex() + "\"", 200,
                           "{\"nonce\":\"" + nonce + "\",\"issued_at\":1763900000,"
                           "\"expires_at\":1763900120,\"server_time_ms\":1763900000000}", false, false});
    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/auth/session", false, "",
                           "\"signature\":\"", 200,
                           "{\"session_token\":\"tok_abc123_XYZ\",\"player_id\":\"" + profile.PlayerId() +
                           "\",\"tag\":\"" + profile.Tag() + "\",\"callsign\":\"HostPilot\","
                           "\"expires_at\":1763907200,\"server_time_ms\":1763900000500}", false, false});
    Check(client.BeginAuthenticate(profile, "0.1.14", "", 2000), "BeginAuthenticate");
    Check(client.Poll(2020) == CeOnlineOpStatus::Pending, "Auth step 1 transitions to step 2");
    // Verify the signature the client actually put into the /auth/session request body!
    {
        CeOnlineJson::Value reqv;
        Check(CeOnlineJson::Parse(mock->last_body, reqv), "parse outgoing auth/session body");
        const auto* sig = reqv.Get("signature");
        Check(sig && sig->IsString() &&
              CeOnlineProfile::VerifyAuthChallenge(profile.PubkeyHex(), nonce, 1763900000ULL,
                                                   profile.Callsign(), sig->str_val),
              "client signed auth challenge with valid RFC 8032 Ed25519 signature");
    }
    Check(client.Poll(2045) == CeOnlineOpStatus::Completed, "Auth step 2 completed");
    Check(client.Authenticated() && client.Session().session_token == "tok_abc123_XYZ" &&
          client.Session().player_id == profile.PlayerId(), "Session established");

    // 3. List Lobbies.
    mock->steps.push_back({CeOnlineHttpMethod::Get,
                           "/ce-lobby/v1/lobbies?cursor=0&limit=16&build_id=0.1.14&content_id=&include_incompatible=0",
                           true, "tok_abc123_XYZ", "", 200,
                           "{\"proto\":1,\"server_time_ms\":1763900001000,\"next_cursor\":16,\"rooms\":[{"
                           "\"room_id\":\"rm_0123456789abcdef\",\"code\":\"ABCD12\",\"name\":\"Forseti Skirmish\","
                           "\"host_callsign\":\"HostPilot\",\"host_tag\":\"4F9A\",\"topology\":\"star_lockstep\","
                           "\"mode\":\"versus_2v2\",\"map\":850,\"used_seats\":2,\"human_seats\":2,\"max_seats\":4,"
                           "\"has_password\":false,\"require_relay\":false,\"state\":\"lobby\",\"proto\":1,"
                           "\"build_id\":\"0.1.14\",\"content_id\":\"\",\"compatible\":true,\"updated_at\":1763900001}]}",
                           false, false});
    CeOnlineListLobbiesReq lreq;
    lreq.build_id = "0.1.14";
    Check(client.BeginListLobbies(lreq, 3000), "BeginListLobbies");
    Check(client.Poll(3010) == CeOnlineOpStatus::Completed &&
          client.LastLobbyList().rooms.size() == 1 &&
          client.LastLobbyList().rooms[0].code == "ABCD12" &&
          client.LastLobbyList().next_cursor == 16, "ListLobbies parsed");

    // 4. Create Lobby (plus invalid map/mode/length refusal aligned with worker.mjs).
    CeOnlineCreateLobbyReq creq;
    creq.name = "Forseti Skirmish";
    creq.build_id = "0.1.14";
    creq.map = 999; // invalid
    Check(!client.BeginCreateLobby(creq, 3100), "invalid map refused locally");
    creq.map = 100; // SP tutorial map excluded by worker.mjs
    Check(!client.BeginCreateLobby(creq, 3100), "map 100 refused locally");
    creq.map = 858; // duplicate map excluded by worker.mjs
    Check(!client.BeginCreateLobby(creq, 3100), "map 858 refused locally");
    creq.map = 850;
    creq.mode = "invalid_mode";
    Check(!client.BeginCreateLobby(creq, 3100), "invalid mode refused locally");
    creq.mode = "tutorial";
    Check(!client.BeginCreateLobby(creq, 3100), "tutorial mode refused locally");
    creq.mode = "versus_2v2";
    creq.name = std::string(41, 'A');
    Check(!client.BeginCreateLobby(creq, 3100), "room name > 40 chars refused locally");
    creq.name = "Forseti Skirmish";
    creq.build_id = std::string(33, '1');
    Check(!client.BeginCreateLobby(creq, 3100), "build_id > 32 chars refused locally");
    creq.build_id = "0.1.14";
    creq.be_cfg = std::string(33, 'S');
    Check(!client.BeginCreateLobby(creq, 3100), "be_cfg > 32 chars refused locally");
    creq.be_cfg = "";
    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/lobbies", true, "tok_abc123_XYZ",
                           "\"map\":850", 200, MakeRoomJson(1, "lobby", "null"), false, false});
    Check(client.BeginCreateLobby(creq, 3100), "BeginCreateLobby");
    Check(client.Poll(3110) == CeOnlineOpStatus::Completed &&
          client.CurrentRoom().room_id == "rm_0123456789abcdef" &&
          client.CurrentRoom().seats.size() == 4, "CreateLobby parsed RoomState");

    // 5. Resolve code (case normalization + 404).
    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/lobbies/resolve", true, "tok_abc123_XYZ",
                           "\"code\":\"ABCD12\"", 200,
                           "{\"room_id\":\"rm_0123456789abcdef\",\"has_password\":true,\"state\":\"lobby\"}",
                           false, false});
    Check(client.BeginResolveCode("abcd12", 3200), "BeginResolveCode lowercases to upper");
    Check(client.Poll(3210) == CeOnlineOpStatus::Completed && client.LastResolve().has_password,
          "ResolveCode parsed");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/lobbies/resolve", true, "tok_abc123_XYZ",
                           "\"code\":\"ZZZZ99\"", 404,
                           "{\"error\":\"not_found\",\"message\":\"Room not found\"}", false, false});
    Check(client.BeginResolveCode("ZZZZ99", 3300), "BeginResolveCode 404");
    Check(client.Poll(3310) == CeOnlineOpStatus::Failed && client.LastHttpCode() == 404 &&
          client.LastErrorCode() == "not_found", "404 error parsed cleanly");

    // 6. PollRoom unchanged + full 64-slot RoomState!
    mock->steps.push_back({CeOnlineHttpMethod::Get, "/ce-lobby/v1/rooms/rm_0123456789abcdef?since=1&rtt_ms=28",
                           true, "tok_abc123_XYZ", "", 200,
                           "{\"unchanged\":true,\"rev\":1,\"server_time_ms\":1763900002000}", false, false});
    Check(client.BeginPollRoom("rm_0123456789abcdef", 1, 28, 3400), "BeginPollRoom unchanged");
    Check(client.Poll(3410) == CeOnlineOpStatus::Completed && client.LastPollUnchanged() &&
          client.CurrentRoom().seats.size() == 4, "unchanged poll retains room");

    mock->steps.push_back({CeOnlineHttpMethod::Get, "/ce-lobby/v1/rooms/rm_0123456789abcdef?since=1&rtt_ms=28",
                           true, "tok_abc123_XYZ", "", 200,
                           MakeRoomJson(2, "lobby", "null", 64, true), false, false});
    Check(client.BeginPollRoom("rm_0123456789abcdef", 1, 28, 3500), "BeginPollRoom 64-slot");
    Check(client.Poll(3510) == CeOnlineOpStatus::Completed && !client.LastPollUnchanged() &&
          client.CurrentRoom().seats.size() == 64 && client.CurrentRoom().max_seats == 64,
          "64-ready RoomState parses cleanly within JSON node bounds");

    // 7. Join (409 conflict) + Update + Chat + Start (ticket) + Turn + Signal Send/Poll + Finish + Leave.
    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/join",
                           true, "tok_abc123_XYZ", "\"build_id\":\"0.1.14\"", 409,
                           "{\"error\":\"incompatible_build\",\"message\":\"Build mismatch\"}", false, false});
    CeOnlineJoinRoomReq jreq{"", "0.1.14", "", "Aquila"};
    Check(client.BeginJoinRoom("rm_0123456789abcdef", jreq, 3600), "BeginJoinRoom");
    Check(client.Poll(3610) == CeOnlineOpStatus::Failed && client.LastHttpCode() == 409 &&
          client.LastErrorCode() == "incompatible_build", "409 conflict parsed");

    CeOnlineUpdateRoomReq ureq;
    ureq.has_ready = true; ureq.ready = true;
    ureq.has_set_seat_kind = true; ureq.set_seat_kind_seat = 2; ureq.set_seat_kind = "ai";
    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/update",
                           true, "tok_abc123_XYZ", "\"set_seat_kind\":{\"seat\":2,\"kind\":\"ai\"}", 200,
                           MakeRoomJson(3, "lobby", "null"), false, false});
    Check(client.BeginUpdateRoom("rm_0123456789abcdef", ureq, 3700), "BeginUpdateRoom");
    Check(client.Poll(3710) == CeOnlineOpStatus::Completed && client.CurrentRoom().rev == 3, "UpdateRoom ok");

    CeOnlineUpdateRoomReq bad_state_req;
    bad_state_req.has_state = true;
    bad_state_req.state = "lobby";
    Check(!client.BeginUpdateRoom("rm_0123456789abcdef", bad_state_req, 3720),
          "BeginUpdateRoom rejects non-in_match state");

    CeOnlineUpdateRoomReq in_match_req;
    in_match_req.has_state = true;
    in_match_req.state = "in_match";
    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/update",
                           true, "tok_abc123_XYZ", "\"state\":\"in_match\"", 200,
                           MakeRoomJson(4, "in_match", "null"), false, false});
    Check(client.BeginUpdateRoom("rm_0123456789abcdef", in_match_req, 3730),
          "BeginUpdateRoom accepts state=in_match");
    Check(client.Poll(3740) == CeOnlineOpStatus::Completed &&
          client.CurrentRoom().state == "in_match" && client.CurrentRoom().rev == 4,
          "UpdateRoom(in_match) ok");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/chat",
                           true, "tok_abc123_XYZ", "\"text\":\"Ready!\"", 200,
                           MakeRoomJson(4, "lobby", "null"), false, false});
    Check(client.BeginSendChat("rm_0123456789abcdef", "Ready!", 3800), "BeginSendChat");
    Check(client.Poll(3810) == CeOnlineOpStatus::Completed, "SendChat ok");

    const std::string ticket =
        "{\"match_id\":\"mt_0123456789abcdef\",\"seed\":1234567890,\"level\":850,\"mode\":\"versus_2v2\","
        "\"topology\":\"star_lockstep\",\"require_relay\":false,"
        "\"roster_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"match_secret\":\"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\","
        "\"issued_at\":1763900000,\"expires_at\":1763907200,\"slots\":["
        "{\"seat\":0,\"sim_slot\":0,\"kind\":\"human\",\"player_id\":\"0123456789abcdefghjkmnpqrs\","
        "\"callsign\":\"HostPilot\",\"tag\":\"4F9A\",\"team\":0,\"be_cfg\":\"Aquila\"}]}";
    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/start",
                           true, "tok_abc123_XYZ", "{}", 200, MakeRoomJson(5, "starting", ticket), false, false});
    Check(client.BeginStartMatch("rm_0123456789abcdef", 3900), "BeginStartMatch");
    Check(client.Poll(3910) == CeOnlineOpStatus::Completed &&
          client.CurrentRoom().start_ticket.valid &&
          client.CurrentRoom().start_ticket.match_id == "mt_0123456789abcdef" &&
          client.CurrentRoom().start_ticket.seed == 1234567890u &&
          client.CurrentRoom().start_ticket.slots.size() == 1, "StartMatch ticket parsed");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/turn",
                           true, "tok_abc123_XYZ", "\"guest_seat\":1", 200,
                           "{\"iceServers\":[{\"urls\":[\"stun:stun.cloudflare.com:3478\"]},"
                           "{\"urls\":[\"turn:turn.cloudflare.com:3478?transport=udp\"],"
                           "\"username\":\"u1\",\"credential\":\"c1\"}],\"ttl\":3600}", false, false});
    Check(client.BeginFetchTurn("rm_0123456789abcdef", "mt_0123456789abcdef", 1, 4000), "BeginFetchTurn");
    Check(client.Poll(4010) == CeOnlineOpStatus::Completed &&
          client.LastTurnBundle().ttl == 3600 &&
          client.LastTurnBundle().ice_servers.size() == 2 &&
          client.LastTurnBundle().ice_servers[1].credential == "c1", "FetchTurn parsed");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/signal",
                           true, "tok_abc123_XYZ", "\"kind\":\"offer\"", 200, "{\"ok\":true}", false, false});
    Check(client.BeginSendSignal("rm_0123456789abcdef", "mt_0123456789abcdef", 0, 1, "offer",
                                 "a=ice-ufrag:abcd\r\na=candidate:1 1 UDP 1 127.0.0.1 9999 typ host\r\n", 4100),
          "BeginSendSignal");
    Check(client.Poll(4110) == CeOnlineOpStatus::Completed && client.LastSentSignalSeq() == 0,
          "SendSignal backward-compatible when mock omits seq");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/signal",
                           true, "tok_abc123_XYZ", "\"kind\":\"offer\"", 200,
                           "{\"ok\":true,\"seq\":42,\"server_time_ms\":1763900003000}", false, false});
    Check(client.BeginSendSignal("rm_0123456789abcdef", "mt_0123456789abcdef", 0, 1, "offer",
                                 "a=ice-ufrag:abcd2\r\n", 4120),
          "BeginSendSignal with seq");
    Check(client.Poll(4130) == CeOnlineOpStatus::Completed && client.LastSentSignalSeq() == 42,
          "SendSignal parses acknowledged seq");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/signal",
                           true, "tok_abc123_XYZ", "\"kind\":\"offer\"", 200, "{\"ok\":false,\"seq\":43}", false, false});
    Check(client.BeginSendSignal("rm_0123456789abcdef", "mt_0123456789abcdef", 0, 1, "offer",
                                 "a=ice-ufrag:abcd3\r\n", 4140),
          "BeginSendSignal ok:false");
    Check(client.Poll(4150) == CeOnlineOpStatus::Failed && client.LastSentSignalSeq() == 0,
          "SendSignal rejects ok:false and resets LastSentSignalSeq");

    mock->steps.push_back({CeOnlineHttpMethod::Get,
                           "/ce-lobby/v1/rooms/rm_0123456789abcdef/signal?match_id=mt_0123456789abcdef&since=0",
                           true, "tok_abc123_XYZ", "", 200,
                           "{\"seq\":1,\"messages\":[{\"seq\":1,\"from_seat\":1,\"to_seat\":0,"
                           "\"kind\":\"answer\",\"sdp\":\"a=ice-ufrag:efgh\\r\\n\"}]}", false, false});
    Check(client.BeginPollSignal("rm_0123456789abcdef", "mt_0123456789abcdef", 0, 4200), "BeginPollSignal");
    Check(client.Poll(4210) == CeOnlineOpStatus::Completed &&
          client.LastSignalPoll().seq == 1 && client.LastSignalPoll().messages.size() == 1 &&
          client.LastSignalPoll().messages[0].kind == "answer", "PollSignal parsed");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/finish",
                           true, "tok_abc123_XYZ", "{}", 200, MakeRoomJson(6, "lobby", "null"), false, false});
    Check(client.BeginFinishMatch("rm_0123456789abcdef", 4300), "BeginFinishMatch empty body");
    Check(client.Poll(4310) == CeOnlineOpStatus::Completed &&
          !client.CurrentRoom().start_ticket.valid, "FinishMatch clears ticket");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/finish",
                           true, "tok_abc123_XYZ", "\"match_id\":\"mt_0123456789abcdef\"", 200,
                           MakeRoomJson(7, "lobby", "null"), false, false});
    Check(client.BeginFinishMatch("rm_0123456789abcdef", 4320, 10000, "mt_0123456789abcdef"),
          "BeginFinishMatch with scoped match_id");
    Check(client.Poll(4330) == CeOnlineOpStatus::Completed, "FinishMatch with match_id ok");

    mock->steps.push_back({CeOnlineHttpMethod::Post, "/ce-lobby/v1/rooms/rm_0123456789abcdef/leave",
                           true, "tok_abc123_XYZ", "{}", 200, "{\"ok\":true}", false, false});
    Check(client.BeginLeaveRoom("rm_0123456789abcdef", 4400), "BeginLeaveRoom");
    Check(client.Poll(4410) == CeOnlineOpStatus::Completed && client.CurrentRoom().room_id.empty(),
          "LeaveRoom clears room");

    // Path traversal / injection refused at client layer.
    Check(!client.BeginLeaveRoom("../escape", 4500), "path traversal room_id refused");
    Check(!mock->broken && mock->steps.empty(), "all mock steps matched cleanly");
}

} // namespace

int main() {
    TestJsonBounds();
    TestClientFlow();
    std::printf("ONLINE_LOBBY_CLIENT_TESTS checks=%d failures=%d\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
"""

LOOPBACK_HTTPS_DRIVER_CPP = r"""
// SPDX-License-Identifier: GPL-3.0-only
#include "online_lobby_client.h"
#include "signaling_https.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {
int gChecks = 0;
int gFails = 0;
void Check(bool ok, const char* msg) {
    ++gChecks;
    if (!ok) { ++gFails; std::fprintf(stderr, "FAIL: %s\n", msg); }
}
uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
} // namespace

int main(int argc, char** argv) {
    // Pure origin & path validation checks.
    Check(CeOnlineHttpsOriginValid("https://lobby.example.invalid"), "valid https origin");
    Check(!CeOnlineHttpsOriginValid("http://127.0.0.1:8787", false), "loopback http refused by default");
    Check(CeOnlineHttpsOriginValid("http://127.0.0.1:8787", true), "loopback http allowed explicitly");
    Check(!CeOnlineHttpsOriginValid("http://localhost:8787", true), "localhost http refused even when loopback on");
    Check(!CeOnlineHttpsOriginValid("http://127.0.0.2:8787", true), "non-127.0.0.1 http refused");
    Check(!CeOnlineHttpsOriginValid("http://127.0.0.1:0", true), "port 0 refused");
    Check(!CeOnlineHttpsOriginValid("http://127.0.0.1:08787", true), "leading-zero port refused");

    Check(CeOnlineHttpsPathValid("/ce-lobby/v1/health"), "valid health path");
    Check(CeOnlineHttpsPathValid("/ce-lobby/v1/rooms/rm_0123456789abcdef?since=0&rtt_ms=15"), "valid query path");
    Check(!CeOnlineHttpsPathValid("/ce-signaling/v1/pair/current"), "outside /ce-lobby/v1/ refused");
    Check(!CeOnlineHttpsPathValid("/ce-lobby/v1/../secret"), "traversal .. refused");
    Check(!CeOnlineHttpsPathValid("/ce-lobby/v1/%2e%2e/secret"), "percent traversal in path refused");
    Check(!CeOnlineHttpsPathValid("/ce-lobby/v1/health with space"), "whitespace refused");
    Check(!CeOnlineHttpsPathValid("/ce-lobby/v1/health\r\nX-Inject: 1"), "CRLF refused");
    Check(!CeOnlineHttpsPathValid("/ce-lobby/v1/health#frag"), "fragment refused");

    Check(CeInitializeSignalHttps(), "init libcurl/WinHTTP");
    if (argc >= 2) {
        const std::string origin = argv[1];
        auto http = CeCreateOnlineHttps(origin.c_str(), true);
        Check(bool(http), "CeCreateOnlineHttps loopback created");
        if (http) {
            CeOnlineLobbyClient client(std::move(http));
            const uint64_t t0 = NowMs();
            Check(client.BeginHealth(t0, 5000), "real loopback GET /ce-lobby/v1/health");
            CeOnlineOpStatus st = CeOnlineOpStatus::Pending;
            while (st == CeOnlineOpStatus::Pending) {
                st = client.Poll(NowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Check(st == CeOnlineOpStatus::Completed && client.LastHealth().ok &&
                  client.LastHealth().active_rooms == 7, "real loopback GET health succeeded");

            // Test 404 status code over real HTTP.
            CeOnlineSession fake_sess;
            fake_sess.session_token = "real_loopback_bearer_token";
            client.SetSessionForTest(fake_sess);
            Check(client.BeginResolveCode("NOPE99", NowMs(), 5000), "real loopback POST resolve 404");
            st = CeOnlineOpStatus::Pending;
            while (st == CeOnlineOpStatus::Pending) {
                st = client.Poll(NowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Check(st == CeOnlineOpStatus::Failed && client.LastHttpCode() == 404 &&
                  client.LastErrorCode() == "not_found", "real loopback POST 404 accepted and parsed");

            // Test 409 status code over real HTTP.
            CeOnlineJoinRoomReq jreq{"", "0.1.14", "", ""};
            Check(client.BeginJoinRoom("rm_0123456789abcdef", jreq, NowMs(), 5000), "real loopback POST join 409");
            st = CeOnlineOpStatus::Pending;
            while (st == CeOnlineOpStatus::Pending) {
                st = client.Poll(NowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Check(st == CeOnlineOpStatus::Failed && client.LastHttpCode() == 409 &&
                  client.LastErrorCode() == "room_full", "real loopback POST 409 accepted and parsed");

            // Test missing security headers MUST be rejected!
            Check(client.BeginLeaveRoom("rm_badheaders000001", NowMs(), 5000), "real loopback bad headers");
            st = CeOnlineOpStatus::Pending;
            while (st == CeOnlineOpStatus::Pending) {
                st = client.Poll(NowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Check(st == CeOnlineOpStatus::Failed && client.LastHttpCode() == 0,
                  "missing security headers rejected by transport");

            // Test edge headers (#33): Set-Cookie, Content-Type with charset, and duplicate non-security headers MUST succeed!
            Check(client.BeginLeaveRoom("rm_edgeheaders00001", NowMs(), 5000), "real loopback edge headers");
            st = CeOnlineOpStatus::Pending;
            while (st == CeOnlineOpStatus::Pending) {
                st = client.Poll(NowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Check(st == CeOnlineOpStatus::Completed,
                  "Set-Cookie + charset Content-Type + duplicate non-security headers accepted");

            // Test duplicate Content-Type MUST still be rejected!
            Check(client.BeginLeaveRoom("rm_dupct00000000001", NowMs(), 5000), "real loopback dup content-type");
            st = CeOnlineOpStatus::Pending;
            while (st == CeOnlineOpStatus::Pending) {
                st = client.Poll(NowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Check(st == CeOnlineOpStatus::Failed && client.LastHttpCode() == 0,
                  "duplicate Content-Type rejected by transport");
        }
    }
    Check(CeShutdownSignalHttps(), "shutdown libcurl/WinHTTP");
    std::printf("ONLINE_HTTPS_LOOPBACK_TESTS checks=%d failures=%d\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
"""


class _LobbyHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def _send_strict(self, code: int, body: bytes, omit_security: bool = False):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        if not omit_security:
            self.send_header("Cache-Control", "no-store")
            self.send_header("Pragma", "no-cache")
            self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/ce-lobby/v1/health":
            payload = json.dumps({
                "ok": True, "enabled": True, "proto": 1, "max_match_slots": 4,
                "wire_max_slots": 64, "server_time_ms": 1763900000000, "active_rooms": 7,
            }).encode()
            self._send_strict(200, payload)
        else:
            self._send_strict(404, b'{"error":"not_found","message":"unknown"}')

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        _ = self.rfile.read(n) if n > 0 else b""
        if self.path == "/ce-lobby/v1/lobbies/resolve":
            self._send_strict(404, b'{"error":"not_found","message":"code missing"}')
        elif self.path == "/ce-lobby/v1/rooms/rm_0123456789abcdef/join":
            self._send_strict(409, b'{"error":"room_full","message":"full"}')
        elif self.path == "/ce-lobby/v1/rooms/rm_badheaders000001/leave":
            self._send_strict(200, b'{"ok":true}', omit_security=True)
        elif self.path == "/ce-lobby/v1/rooms/rm_edgeheaders00001/leave":
            body = b'{"ok":true}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Pragma", "no-cache")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Set-Cookie", "__cf_bm=" + ("a" * 1200) + "; path=/; HttpOnly; Secure")
            self.send_header("Vary", "Origin")
            self.send_header("Vary", "Accept-Encoding")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/ce-lobby/v1/rooms/rm_dupct00000000001/leave":
            body = b'{"ok":true}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Pragma", "no-cache")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self._send_strict(400, b'{"error":"bad_request","message":"unexpected"}')


class OnlineLobbyClientTests(unittest.TestCase):
    def _build_mock_native(self, tmp: Path, client_cpp: Path, asan: bool = True) -> Path:
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        self.assertTrue(cc and cxx)
        san = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if asan else []
        objs = []
        for src, name in [
            (MONO_SRC / "monocypher.c", "mono.o"),
            (MONO_SRC / "optional/monocypher-ed25519.c", "ed25519.o"),
            (PORT / "updater/sha256.c", "sha256.o"),
        ]:
            obj = tmp / name
            subprocess.run(
                [cc, "-std=gnu11", "-O2", *san, "-I", str(MONO_SRC), "-c", str(src), "-o", str(obj)],
                check=True, capture_output=True, text=True, timeout=60,
            )
            objs.append(obj)
        driver = tmp / "mock_driver.cpp"
        driver.write_text(MOCK_DRIVER_CPP)
        exe = tmp / "mock_tests"
        subprocess.run(
            [
                cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", *san,
                "-I", str(PORT / "network"), "-I", str(MONO_SRC),
                str(PORT / "network/online_profile.cpp"), str(client_cpp), str(driver),
                *[str(o) for o in objs], "-o", str(exe),
            ],
            check=True, capture_output=True, text=True, timeout=90,
        )
        return exe

    def test_mock_client_and_json_asan(self):
        with tempfile.TemporaryDirectory(prefix="ce-lobby-client-") as td:
            tmp = Path(td)
            exe = self._build_mock_native(tmp, PORT / "network/online_lobby_client.cpp", asan=True)
            res = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertRegex(res.stdout, r"ONLINE_LOBBY_CLIENT_TESTS checks=[1-9][0-9]* failures=0")

    def test_causal_mutant_duplicate_json_key_caught(self):
        src = (PORT / "network/online_lobby_client.cpp").read_text()
        target = "if (existing.first == key) return false; // reject duplicate keys"
        self.assertEqual(src.count(target), 1)
        mutated = src.replace(target, "(void)existing;", 1)
        with tempfile.TemporaryDirectory(prefix="ce-lobby-client-mut-") as td:
            tmp = Path(td)
            mut_cpp = tmp / "client_mut.cpp"
            mut_cpp.write_text(mutated)
            exe = self._build_mock_native(tmp, mut_cpp, asan=False)
            res = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertNotEqual(res.returncode, 0, "duplicate JSON key mutant survived")
            self.assertIn("duplicate JSON key rejected", res.stderr)

    def test_linux32_real_curl_loopback_http(self):
        cc = LLVM_BIN / "clang"
        cxx = LLVM_BIN / "clang++"
        if not (cc.is_file() and cxx.is_file() and SYSROOT.is_dir()):
            self.skipTest("pinned Linux32 sniper toolchain unavailable")
        with tempfile.TemporaryDirectory(prefix="ce-lobby-l32-") as td:
            tmp = Path(td)
            flags = ["-m32", f"--sysroot={SYSROOT}"]
            objs = []
            for src, name in [
                (MONO_SRC / "monocypher.c", "mono.o"),
                (MONO_SRC / "optional/monocypher-ed25519.c", "ed25519.o"),
                (PORT / "updater/sha256.c", "sha256.o"),
            ]:
                obj = tmp / name
                subprocess.run(
                    [str(cc), *flags, "-std=gnu11", "-O2", "-I", str(MONO_SRC), "-c", str(src), "-o", str(obj)],
                    check=True, capture_output=True, text=True, timeout=60,
                )
                objs.append(obj)
            driver = tmp / "loopback_driver.cpp"
            driver.write_text(LOOPBACK_HTTPS_DRIVER_CPP)
            exe = tmp / "loopback_l32"
            subprocess.run(
                [
                    str(cxx), *flags, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DCE_NETWORK_ICE=1",
                    "-I", str(PORT / "network"), "-I", str(MONO_SRC),
                    str(PORT / "network/signaling_https.cpp"),
                    str(PORT / "network/online_profile.cpp"),
                    str(PORT / "network/online_lobby_client.cpp"),
                    str(driver), *[str(o) for o in objs],
                    f"--ld-path={MINGW_BIN / 'ld.lld'}",
                    f"-L{SYSROOT / 'usr/lib/i386-linux-gnu'}", "-lcurl-gnutls", "-pthread",
                    "-o", str(exe),
                ],
                check=True, capture_output=True, text=True, timeout=90,
            )

            httpd = socketserver.TCPServer(("127.0.0.1", 0), _LobbyHandler)
            port = httpd.server_address[1]
            thread = threading.Thread(target=httpd.serve_forever, daemon=True)
            thread.start()
            try:
                loader = SYSROOT / "lib/ld-linux.so.2"
                libs = f"{SYSROOT / 'lib/i386-linux-gnu'}:{SYSROOT / 'usr/lib/i386-linux-gnu'}"
                res = subprocess.run(
                    [str(loader), "--library-path", libs, str(exe), f"http://127.0.0.1:{port}"],
                    capture_output=True, text=True, timeout=30,
                )
            finally:
                httpd.shutdown()
                httpd.server_close()
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertRegex(res.stdout, r"ONLINE_HTTPS_LOOPBACK_TESTS checks=[1-9][0-9]* failures=0")

    def test_windows32_cross_compile(self):
        cc = MINGW_BIN / "i686-w64-mingw32-clang"
        cxx = MINGW_BIN / "i686-w64-mingw32-clang++"
        if not (cc.is_file() and cxx.is_file()):
            self.skipTest("pinned llvm-mingw toolchain unavailable")
        with tempfile.TemporaryDirectory(prefix="ce-lobby-w32-") as td:
            tmp = Path(td)
            objs = []
            for src, name in [
                (MONO_SRC / "monocypher.c", "mono.o"),
                (MONO_SRC / "optional/monocypher-ed25519.c", "ed25519.o"),
                (PORT / "updater/sha256.c", "sha256.o"),
            ]:
                obj = tmp / name
                subprocess.run(
                    [str(cc), "-std=gnu11", "-O2", "-I", str(MONO_SRC), "-c", str(src), "-o", str(obj)],
                    check=True, capture_output=True, text=True, timeout=60,
                )
                objs.append(obj)
            driver = tmp / "loopback_driver.cpp"
            driver.write_text(LOOPBACK_HTTPS_DRIVER_CPP)
            exe = tmp / "loopback_w32.exe"
            subprocess.run(
                [
                    str(cxx), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DCE_NETWORK_ICE=1",
                    "-I", str(PORT / "network"), "-I", str(MONO_SRC),
                    str(PORT / "network/signaling_https.cpp"),
                    str(PORT / "network/online_profile.cpp"),
                    str(PORT / "network/online_lobby_client.cpp"),
                    str(driver), *[str(o) for o in objs],
                    "-lwinhttp", "-lbcrypt", "-ladvapi32", "-static-libstdc++", "-static-libgcc",
                    "-o", str(exe),
                ],
                check=True, capture_output=True, text=True, timeout=90,
            )
            self.assertTrue(exe.is_file())

    def test_winhttp_tls12_tls13_fallback_and_legacy_header_guard(self):
        cc = MINGW_BIN / "i686-w64-mingw32-clang"
        cxx = MINGW_BIN / "i686-w64-mingw32-clang++"
        if not (cc.is_file() and cxx.is_file()):
            self.skipTest("pinned llvm-mingw toolchain unavailable")

        shim_winhttp_h = """\
#pragma once
#include <windows.h>
#pragma push_macro("DECLSPEC_IMPORT")
#undef DECLSPEC_IMPORT
#define DECLSPEC_IMPORT
#include_next <winhttp.h>
#pragma pop_macro("DECLSPEC_IMPORT")
#ifdef CE_TEST_LEGACY_NO_TLS13
#undef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#endif
"""
        winhttp_driver_cpp = r"""
// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <winhttp.h>
#include "signaling_https.h"

#include <cstdio>
#include <vector>

namespace {
struct FakeHandle {
    int kind = 0; // 1=session, 2=conn, 3=req
    WINHTTP_STATUS_CALLBACK cb = nullptr;
    DWORD_PTR ctx = 0;
};

int gTlsMode = 0; // 0=accept all, 1=reject TLS1.3 with ERROR_INVALID_PARAMETER, 2=reject with ERROR_OUTOFMEMORY
std::vector<DWORD> gObservedProtocols;
int gChecks = 0;
int gFails = 0;
bool gInCallback = false;
bool gClosedParentInCallback = false;
int gOpenHandles = 0;

void Check(bool ok, const char* msg) {
    ++gChecks;
    if (!ok) {
        ++gFails;
        std::fprintf(stderr, "FAIL: %s\n", msg);
    }
}
} // namespace

extern "C" {
HINTERNET WINAPI WinHttpOpen(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) {
    ++gOpenHandles;
    auto* h = new FakeHandle{1, nullptr, 0};
    return static_cast<HINTERNET>(h);
}

BOOL WINAPI WinHttpCloseHandle(HINTERNET hInternet) {
    if (!hInternet) return TRUE;
    auto* h = static_cast<FakeHandle*>(hInternet);
    if ((h->kind == 1 || h->kind == 2) && gInCallback) {
        gClosedParentInCallback = true;
    }
    if (h->kind == 3 && h->cb) {
        HINTERNET raw = hInternet;
        gInCallback = true;
        h->cb(hInternet, h->ctx, WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING, &raw, sizeof(raw));
        gInCallback = false;
    }
    --gOpenHandles;
    delete h;
    return TRUE;
}

BOOL WINAPI WinHttpSetTimeouts(HINTERNET, int, int, int, int) { return TRUE; }

BOOL WINAPI WinHttpSetOption(HINTERNET hInternet, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength) {
    auto* h = static_cast<FakeHandle*>(hInternet);
    if (dwOption == WINHTTP_OPTION_SECURE_PROTOCOLS && lpBuffer && dwBufferLength == sizeof(DWORD)) {
        const DWORD proto = *static_cast<const DWORD*>(lpBuffer);
        gObservedProtocols.push_back(proto);
        if (gTlsMode == 1 && (proto & 0x00002000u) != 0u) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (gTlsMode == 2) {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        return TRUE;
    }
    if (dwOption == WINHTTP_OPTION_CONTEXT_VALUE && h && lpBuffer && dwBufferLength == sizeof(DWORD_PTR)) {
        h->ctx = *static_cast<const DWORD_PTR*>(lpBuffer);
    }
    return TRUE;
}

HINTERNET WINAPI WinHttpConnect(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD) {
    ++gOpenHandles;
    return static_cast<HINTERNET>(new FakeHandle{2, nullptr, 0});
}

HINTERNET WINAPI WinHttpOpenRequest(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD) {
    ++gOpenHandles;
    return static_cast<HINTERNET>(new FakeHandle{3, nullptr, 0});
}

WINHTTP_STATUS_CALLBACK WINAPI WinHttpSetStatusCallback(HINTERNET hInternet, WINHTTP_STATUS_CALLBACK lpfn, DWORD, DWORD_PTR) {
    auto* h = static_cast<FakeHandle*>(hInternet);
    WINHTTP_STATUS_CALLBACK prev = h ? h->cb : nullptr;
    if (h) h->cb = lpfn;
    return prev;
}

BOOL WINAPI WinHttpSendRequest(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR) { return TRUE; }
BOOL WINAPI WinHttpReceiveResponse(HINTERNET, LPVOID) { return TRUE; }
BOOL WINAPI WinHttpQueryHeaders(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD) {
    SetLastError(ERROR_WINHTTP_HEADER_NOT_FOUND);
    return FALSE;
}
BOOL WINAPI WinHttpReadData(HINTERNET, LPVOID, DWORD, LPDWORD) { return TRUE; }
} // extern "C"

int main() {
    const DWORD kTls12 = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef CE_TEST_LEGACY_NO_TLS13
    const DWORD kPreferred = kTls12;
#else
    const DWORD kPreferred = kTls12 | 0x00002000u;
#endif

    // 1. Preferred TLS flags succeed on first try for both WinHttps and WinOnlineHttps.
    gTlsMode = 0;
    gObservedProtocols.clear();
    auto sig = CeCreateSignalHttps("https://lobby.example.invalid");
    Check(bool(sig) && sig->Begin("pair/current", "tok123", 6, "{}", 100, 5000), "WinHttps::Begin preferred TLS");
    sig->Cancel();
    auto onl = CeCreateOnlineHttps("https://lobby.example.invalid", false);
    Check(bool(onl) && onl->Begin(CeOnlineHttpMethod::Get, "/ce-lobby/v1/health", nullptr, 0, "", 100, 5000),
          "WinOnlineHttps::Begin preferred TLS");
    onl->Cancel();
    Check(gObservedProtocols.size() == 2 &&
          gObservedProtocols[0] == kPreferred &&
          gObservedProtocols[1] == kPreferred,
          "both WinHTTP transports request preferred TLS flags first");

#ifndef CE_TEST_LEGACY_NO_TLS13
    // 2. ERROR_INVALID_PARAMETER on TLS1_2|TLS1_3 falls back once to TLS1_2 cleanly!
    gTlsMode = 1;
    gObservedProtocols.clear();
    Check(sig->Begin("pair/current", "tok123", 6, "{}", 200, 5000), "WinHttps::Begin fallback to TLS 1.2");
    sig->Cancel();
    Check(onl->Begin(CeOnlineHttpMethod::Get, "/ce-lobby/v1/health", nullptr, 0, "", 200, 5000),
          "WinOnlineHttps::Begin fallback to TLS 1.2");
    onl->Cancel();
    Check(gObservedProtocols.size() == 4 &&
          gObservedProtocols[0] == kPreferred && gObservedProtocols[1] == kTls12 &&
          gObservedProtocols[2] == kPreferred && gObservedProtocols[3] == kTls12,
          "ERROR_INVALID_PARAMETER triggers single fallback to TLS 1.2");

    // 3. Non-ERROR_INVALID_PARAMETER failure fails closed without retrying!
    gTlsMode = 2;
    gObservedProtocols.clear();
    Check(!sig->Begin("pair/current", "tok123", 6, "{}", 300, 5000), "WinHttps fails closed on non-invalid-param");
    Check(!onl->Begin(CeOnlineHttpMethod::Get, "/ce-lobby/v1/health", nullptr, 0, "", 300, 5000),
          "WinOnlineHttps fails closed on non-invalid-param");
    Check(gObservedProtocols.size() == 2 &&
          gObservedProtocols[0] == kPreferred && gObservedProtocols[1] == kPreferred,
          "non-ERROR_INVALID_PARAMETER does NOT retry TLS 1.2");
#endif

    // 4. Loopback plain HTTP skips WINHTTP_OPTION_SECURE_PROTOCOLS entirely.
    gTlsMode = 2;
    gObservedProtocols.clear();
    auto loop_http = CeCreateOnlineHttps("http://127.0.0.1:8787", true);
    Check(bool(loop_http) &&
          loop_http->Begin(CeOnlineHttpMethod::Get, "/ce-lobby/v1/health", nullptr, 0, "", 400, 5000) &&
          gObservedProtocols.empty(),
          "loopback http skips TLS protocol option");
    loop_http->Cancel();

    Check(!gClosedParentInCallback, "connection/session handles never closed inside WinHTTP callback");
    Check(gOpenHandles == 0, "all WinHTTP handles reaped cleanly on caller thread");

    std::printf("WINHTTP_TLS_TESTS checks=%d failures=%d\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
"""
        with tempfile.TemporaryDirectory(prefix="ce-winhttp-tls-") as td:
            tmp = Path(td)
            shim_dir = tmp / "shim"
            shim_dir.mkdir(mode=0o700)
            (shim_dir / "winhttp.h").write_text(shim_winhttp_h)
            driver = tmp / "winhttp_tls_driver.cpp"
            driver.write_text(winhttp_driver_cpp)
            mono_obj = tmp / "mono.o"
            subprocess.run(
                [str(cc), "-std=gnu11", "-O2", "-I", str(MONO_SRC),
                 "-c", str(MONO_SRC / "monocypher.c"), "-o", str(mono_obj)],
                check=True, capture_output=True, text=True, timeout=60,
            )

            exes: list[Path] = []
            for suffix, extra_defs in (("modern", []), ("legacy", ["-DCE_TEST_LEGACY_NO_TLS13=1"])):
                exe = tmp / f"winhttp_tls_{suffix}.exe"
                subprocess.run(
                    [
                        str(cxx), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-DCE_NETWORK_ICE=1", *extra_defs,
                        "-I", str(shim_dir), "-I", str(PORT / "network"), "-I", str(MONO_SRC),
                        str(PORT / "network/signaling_https.cpp"),
                        str(driver), str(mono_obj),
                        "-static-libstdc++", "-static-libgcc",
                        "-o", str(exe),
                    ],
                    check=True, capture_output=True, text=True, timeout=90,
                )
                self.assertTrue(exe.is_file())
                exes.append(exe)

            wine = shutil.which("wine")
            if not wine:
                return
            prefix = tmp / "wineprefix"
            prefix.mkdir(mode=0o700)
            env = {k: v for k, v in os.environ.items()
                   if k not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
            env.update(
                WINEPREFIX=str(prefix),
                WINEDEBUG="-all",
                WINEDLLOVERRIDES="mscoree,mshtml,winemenubuilder.exe,winepulse.drv,winealsa.drv,wineoss.drv=",
            )
            for exe in exes:
                res = subprocess.run([wine, str(exe)], capture_output=True, text=True, env=env, timeout=90)
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                self.assertRegex(res.stdout, r"WINHTTP_TLS_TESTS checks=[1-9][0-9]* failures=0")


if __name__ == "__main__":
    unittest.main(verbosity=2)
