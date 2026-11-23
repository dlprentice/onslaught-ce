// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_service.cpp: Live integration glue connecting CeOnlineProfile,
// CeOnlineLobbyClient, CeStarTransport + multi-slot CeIceBridge, and CE_LobbyQueueOnlineLaunch.
#ifndef CE_NETWORK_OFF

#include "ce_online_service.h"
#include "ce_online_match.h"

#include "../network/online_profile.h"
#include "../network/online_lobby_client.h"
#include "../network/star_transport.h"
#include "../network/signaling_https.h"
#include "../platform/common/ce_netlobby.h"

#include <monocypher.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

const char *PortUserDir();
extern "C" int CE_SettingGetText(const char *key, char *out, int n) __attribute__((weak));
extern "C" int CE_LobbyQueueOnlineLaunch(const CE_OnlineMatchTicket *ticket) __attribute__((weak));

#ifdef CE_VERSION
static const char kOnlineBuildId[] = CE_VERSION;
#else
static const char kOnlineBuildId[] = "0.1.14";
#endif

namespace {

void CopyBounded(char *dst, size_t cap, const std::string &src) {
    if (!dst || cap == 0) return;
    size_t n = std::min(src.size(), cap - 1);
    if (n > 0) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

bool DecodeHexBytes(const std::string &hex, uint8_t *out, size_t len) {
    if (hex.size() != len * 2) return false;
    for (size_t i = 0; i < len; ++i) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };
        int hi = nibble(hex[i * 2]);
        int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

int ParseSeatKind(const std::string &k) {
    if (k == "human") return 1;
    if (k == "ai") return 2;
    if (k == "closed") return 3;
    return 0;
}

int ParseRouteKind(const std::string &r) {
    if (r == "direct") return 1;
    if (r == "relay") return 2;
    return 0;
}

bool IsLoopbackHttpOrigin(const std::string &origin) {
    return origin.compare(0, 17, "http://127.0.0.1:") == 0;
}

std::string ResolveConfiguredLobbyOrigin(const CeOnlineProfile &profile) {
    if (CE_SettingGetText) {
        char ini_url[256] = {};
        if (CE_SettingGetText("lobby_url", ini_url, static_cast<int>(sizeof(ini_url))) && ini_url[0]) {
            return std::string(ini_url);
        }
    }
    if (!profile.LobbyUrl().empty()) {
        return profile.LobbyUrl();
    }
    return std::string(CE_ONLINE_LOBBY_DEFAULT_URL);
}

#if defined(CE_NETWORK_ICE)
// Parse "stun:host:port" or "turn:host:port?transport=udp" into host + port for juice_config_t.
bool ParseIceUrl(const std::string &url, bool *out_is_turn, std::string *out_host, uint16_t *out_port) {
    if (!out_is_turn || !out_host || !out_port) return false;
    size_t pos = 0;
    if (url.compare(0, 5, "turn:") == 0) {
        *out_is_turn = true;
        pos = 5;
    } else if (url.compare(0, 5, "stun:") == 0) {
        *out_is_turn = false;
        pos = 5;
    } else {
        return false;
    }
    size_t q = url.find('?', pos);
    std::string hostport = (q == std::string::npos) ? url.substr(pos) : url.substr(pos, q - pos);
    size_t colon = hostport.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= hostport.size()) return false;
    std::string host = hostport.substr(0, colon);
    unsigned long p = std::strtoul(hostport.substr(colon + 1).c_str(), nullptr, 10);
    if (p == 0 || p > 65535) return false;
    *out_host = host;
    *out_port = static_cast<uint16_t>(p);
    return true;
}

struct ParsedJuiceConfig {
    std::string stun_host;
    uint16_t stun_port = 3478;
    struct TurnEntry {
        std::string host;
        std::string user;
        std::string pass;
        uint16_t port = 3478;
    };
    std::vector<TurnEntry> turns;
    std::vector<juice_turn_server_t> turn_structs;
    juice_config_t cfg{};

    bool Build(const CeOnlineTurnBundle &bundle) {
        stun_host = "stun.cloudflare.com";
        stun_port = 3478;
        turns.clear();
        turn_structs.clear();
        for (const auto &srv : bundle.ice_servers) {
            for (const auto &u : srv.urls) {
                bool is_turn = false;
                std::string host;
                uint16_t port = 0;
                if (!ParseIceUrl(u, &is_turn, &host, &port)) continue;
                if (!is_turn) {
                    stun_host = host;
                    stun_port = port;
                } else if (!srv.username.empty() && !srv.credential.empty()) {
                    TurnEntry e;
                    e.host = host;
                    e.port = port;
                    e.user = srv.username;
                    e.pass = srv.credential;
                    turns.push_back(std::move(e));
                }
            }
        }
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;
        cfg.stun_server_host = stun_host.c_str();
        cfg.stun_server_port = stun_port;
        turn_structs.reserve(turns.size());
        for (const auto &t : turns) {
            juice_turn_server_t jt{};
            jt.host = t.host.c_str();
            jt.port = t.port;
            jt.username = t.user.c_str();
            jt.password = t.pass.c_str();
            turn_structs.push_back(jt);
        }
        if (!turn_structs.empty()) {
            cfg.turn_servers = turn_structs.data();
            cfg.turn_servers_count = static_cast<int>(turn_structs.size());
        }
        return true;
    }
};
#endif

// ---------------------------------------------------------------------------
// Live Frontend Lobby Service (runs in frontend process)
// ---------------------------------------------------------------------------
class CeLiveOnlineService final : public CeOnlineServiceFacade {
public:
    CeLiveOnlineService() = default;
    ~CeLiveOnlineService() override = default;

    void EnsureInit() {
        if (inited_) return;
        inited_ = true;
        const char *user_dir = PortUserDir();
        std::string err;
        if (user_dir && user_dir[0]) {
            if (!profile_.LoadOrCreate(user_dir, &err)) {
                conn_status_ = 3;
                status_detail_ = err.empty() ? "Could not load online profile." : err;
            }
        }
        origin_ = ResolveConfiguredLobbyOrigin(profile_);
        RebuildClient();
    }

    void RebuildClient() {
        client_.reset();
        pending_queue_.clear();
        active_room_id_.clear();
        last_room_rev_ = 0;
        room_active_ = false;
        launch_queued_ = false;
        if (origin_.empty()) {
            conn_status_ = 0;
            status_detail_ = "Set lobby_url in ce.ini or deploy lobby service.";
            return;
        }
        const bool allow_loopback = IsLoopbackHttpOrigin(origin_);
        auto transport = CeCreateOnlineHttps(origin_.c_str(), allow_loopback);
        if (!transport) {
            conn_status_ = 3;
            status_detail_ = "Invalid lobby server URL (requires https:// origin).";
            return;
        }
        client_.reset(new CeOnlineLobbyClient(std::move(transport)));
        conn_status_ = 1;
        status_detail_ = "Connecting to lobby server...";
        want_connect_ = true;
    }

    void Poll(uint64_t now_ms) override {
        EnsureInit();
        if (!client_) return;

        if (client_->Busy()) {
            CeOnlineOpStatus st = client_->Poll(now_ms);
            if (st == CeOnlineOpStatus::Pending) return;
            HandleOpComplete(client_->ActiveOp(), st, now_ms);
        }

        // Connection / authentication sequence
        if (want_connect_) {
            if (!health_ok_) {
                if (client_->BeginHealth(now_ms)) {
                    conn_status_ = 1;
                    status_detail_ = "Checking lobby server health...";
                }
                return;
            }
            if (!client_->Authenticated()) {
                if (!profile_.Loaded()) {
                    conn_status_ = 3;
                    status_detail_ = "Online profile keypair unavailable.";
                    want_connect_ = false;
                    return;
                }
                if (client_->BeginAuthenticate(profile_, kOnlineBuildId, content_id_, now_ms)) {
                    conn_status_ = 1;
                    status_detail_ = "Authenticating Ed25519 profile...";
                }
                return;
            }
            want_connect_ = false;
            conn_status_ = 2;
            status_detail_ = "Connected";
        }

        if (!client_->Authenticated()) return;

        // Dispatch next queued user request
        if (!pending_queue_.empty()) {
            PendingAction act = std::move(pending_queue_.front());
            pending_queue_.pop_front();
            DispatchAction(act, now_ms);
            return;
        }

        // Periodic active-room polling (?since=rev every 1000 ms)
        if (room_active_ && !active_room_id_.empty() && now_ms >= last_room_poll_ms_ + 1000u) {
            last_room_poll_ms_ = now_ms;
            uint32_t rtt = ping_ms_ >= 0 ? static_cast<uint32_t>(ping_ms_) : 0u;
            client_->BeginPollRoom(active_room_id_, last_room_rev_, rtt, now_ms);
        }
    }

    bool GetProfile(CeSvcProfile *out) override {
        EnsureInit();
        if (!out) return false;
        out->player_id = profile_.PlayerId();
        out->callsign = profile_.Loaded() ? profile_.Callsign() : "Pilot0000";
        out->tag = profile_.Loaded() ? profile_.Tag() : "0000";
        out->matches_played = profile_.MatchesPlayed();
        out->matches_won = profile_.MatchesWon();
        out->server_url = origin_.empty() ? "Not configured" : origin_;
        out->conn_status = conn_status_;
        out->ping_ms = ping_ms_;
        out->status_detail = status_detail_;
        return true;
    }

    bool SaveCallsign(const std::string &callsign, std::string *err_out) override {
        EnsureInit();
        if (!profile_.Loaded()) {
            if (err_out) *err_out = "Online profile unavailable.";
            return false;
        }
        if (!profile_.SetCallsign(callsign, true, err_out)) return false;
        // Re-authenticate on next poll so server sees updated callsign.
        if (client_) {
            client_->ClearSession();
            want_connect_ = true;
        }
        return true;
    }

    void RequestReconnect() override {
        EnsureInit();
        origin_ = ResolveConfiguredLobbyOrigin(profile_);
        health_ok_ = false;
        ping_ms_ = -1;
        RebuildClient();
    }

    void RequestRefreshLobbies(bool include_incompatible) override {
        EnsureInit();
        if (!client_ || (!client_->Authenticated() && !want_connect_)) {
            lobbies_loading_ = false;
            lobbies_err_ = status_detail_.empty() ? "Not connected to lobby server." : status_detail_;
            return;
        }
        lobbies_loading_ = true;
        lobbies_err_.clear();
        for (auto &existing : pending_queue_) {
            if (existing.kind == ActionKind::ListLobbies) {
                existing.include_incompatible = include_incompatible;
                return;
            }
        }
        PendingAction act;
        act.kind = ActionKind::ListLobbies;
        act.include_incompatible = include_incompatible;
        pending_queue_.push_back(std::move(act));
    }

    bool GetLobbies(std::vector<CeSvcRoomSummary> *out_rooms,
                    bool *loading_out, std::string *err_out) override {
        EnsureInit();
        if (out_rooms) *out_rooms = cached_lobbies_;
        if (loading_out) *loading_out = lobbies_loading_;
        if (err_out) *err_out = lobbies_err_;
        return true;
    }

    bool RequestCreateRoom(const CeSvcCreateParams &params, std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated()) {
            if (err_out) *err_out = "Not connected to lobby server.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::CreateRoom;
        act.create = params;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool RequestResolveCode(const std::string &code, std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated()) {
            if (err_out) *err_out = "Not connected to lobby server.";
            return false;
        }
        resolve_ready_ = false;
        PendingAction act;
        act.kind = ActionKind::ResolveCode;
        act.text = code;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool TakeResolveResult(CeSvcResolveResult *out) override {
        if (!resolve_ready_ || !out) return false;
        *out = last_resolve_;
        resolve_ready_ = false;
        return true;
    }

    bool RequestJoinRoom(const std::string &room_id, const std::string &password,
                         const std::string &be_cfg, std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated()) {
            if (err_out) *err_out = "Not connected to lobby server.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::JoinRoom;
        act.room_id = room_id;
        act.password = password;
        act.be_cfg = be_cfg;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    void RequestLeaveRoom() override {
        EnsureInit();
        if (client_ && client_->Authenticated() && !active_room_id_.empty()) {
            PendingAction act;
            act.kind = ActionKind::LeaveRoom;
            act.room_id = active_room_id_;
            pending_queue_.push_back(std::move(act));
        }
        room_active_ = false;
        active_room_id_.clear();
        last_room_rev_ = 0;
        launch_queued_ = false;
    }

    bool GetRoom(CeSvcRoom *out_room) override {
        EnsureInit();
        if (!out_room) return false;
        if (!room_active_) {
            *out_room = CeSvcRoom{};
            return false;
        }
        *out_room = cached_room_;
        return true;
    }

    bool RequestUpdateSelf(int team, const std::string &be_cfg, bool ready,
                           std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated() || active_room_id_.empty()) {
            if (err_out) *err_out = "Not in a room.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::UpdateSelf;
        act.room_id = active_room_id_;
        act.team = team;
        act.be_cfg = be_cfg;
        act.ready = ready;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool RequestHostUpdate(const CeSvcHostPatch &patch, std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated() || active_room_id_.empty()) {
            if (err_out) *err_out = "Not in a room.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::HostUpdate;
        act.room_id = active_room_id_;
        act.patch = patch;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool RequestSendChat(const std::string &text, std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated() || active_room_id_.empty()) {
            if (err_out) *err_out = "Not in a room.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::SendChat;
        act.room_id = active_room_id_;
        act.text = text;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool RequestStartMatch(std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated() || active_room_id_.empty()) {
            if (err_out) *err_out = "Not in a room.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::StartMatch;
        act.room_id = active_room_id_;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    void NotifyMatchTicketReady() override {
        TryQueueMatchLaunch();
    }

private:
    enum class ActionKind {
        ListLobbies,
        CreateRoom,
        ResolveCode,
        JoinRoom,
        LeaveRoom,
        UpdateSelf,
        HostUpdate,
        SendChat,
        StartMatch
    };

    struct PendingAction {
        ActionKind kind = ActionKind::ListLobbies;
        bool include_incompatible = false;
        CeSvcCreateParams create;
        std::string room_id;
        std::string password;
        std::string be_cfg;
        std::string text;
        int team = 0;
        bool ready = false;
        CeSvcHostPatch patch;
    };

    void DispatchAction(const PendingAction &act, uint64_t now_ms) {
        if (!client_) return;
        switch (act.kind) {
        case ActionKind::ListLobbies: {
            CeOnlineListLobbiesReq req;
            req.cursor = 0;
            req.limit = 32;
            req.build_id = kOnlineBuildId;
            req.content_id = content_id_;
            req.include_incompatible = act.include_incompatible;
            client_->BeginListLobbies(req, now_ms);
            break;
        }
        case ActionKind::CreateRoom: {
            CeOnlineCreateLobbyReq req;
            req.name = act.create.name;
            req.mode = act.create.mode;
            req.map = static_cast<uint32_t>(act.create.map);
            req.max_seats = act.create.max_seats;
            req.topology = act.create.topology.empty() ? "star_lockstep" : act.create.topology;
            req.visibility = act.create.visibility.empty() ? "public" : act.create.visibility;
            req.password = act.create.password;
            req.require_relay = act.create.require_relay;
            req.ai_fill = act.create.ai_fill;
            req.build_id = kOnlineBuildId;
            req.content_id = content_id_;
            req.be_cfg = act.create.be_cfg;
            client_->BeginCreateLobby(req, now_ms);
            break;
        }
        case ActionKind::ResolveCode:
            client_->BeginResolveCode(act.text, now_ms);
            break;
        case ActionKind::JoinRoom: {
            CeOnlineJoinRoomReq req;
            req.password = act.password;
            req.build_id = kOnlineBuildId;
            req.content_id = content_id_;
            req.be_cfg = act.be_cfg;
            client_->BeginJoinRoom(act.room_id, req, now_ms);
            break;
        }
        case ActionKind::LeaveRoom:
            client_->BeginLeaveRoom(act.room_id, now_ms);
            break;
        case ActionKind::UpdateSelf: {
            CeOnlineUpdateRoomReq req;
            req.has_team = true;
            req.team = static_cast<uint16_t>(act.team);
            req.has_be_cfg = !act.be_cfg.empty();
            req.be_cfg = act.be_cfg;
            req.has_ready = true;
            req.ready = act.ready;
            if (ping_ms_ >= 0) {
                req.has_rtt_ms = true;
                req.rtt_ms = static_cast<uint32_t>(ping_ms_);
            }
            client_->BeginUpdateRoom(act.room_id, req, now_ms);
            break;
        }
        case ActionKind::HostUpdate: {
            CeOnlineUpdateRoomReq req;
            if (act.patch.has_map) { req.has_map = true; req.map = static_cast<uint32_t>(act.patch.map); }
            if (act.patch.has_mode) { req.has_mode = true; req.mode = act.patch.mode; }
            if (act.patch.has_require_relay) { req.has_require_relay = true; req.require_relay = act.patch.require_relay; }
            if (act.patch.has_ai_fill) { req.has_ai_fill = true; req.ai_fill = act.patch.ai_fill; }
            if (act.patch.has_seat_kind) {
                req.has_set_seat_kind = true;
                req.set_seat_kind_seat = act.patch.target_seat;
                req.set_seat_kind = (act.patch.seat_kind == 2) ? "ai" :
                                    (act.patch.seat_kind == 3) ? "closed" : "open";
            }
            if (act.patch.has_kick_seat) {
                req.has_kick_seat = true;
                req.kick_seat = act.patch.kick_seat;
            }
            client_->BeginUpdateRoom(act.room_id, req, now_ms);
            break;
        }
        case ActionKind::SendChat:
            client_->BeginSendChat(act.room_id, act.text, now_ms);
            break;
        case ActionKind::StartMatch:
            client_->BeginStartMatch(act.room_id, now_ms);
            break;
        }
    }

    void SyncRoomFromClient() {
        const CeOnlineRoomState &r = client_->CurrentRoom();
        if (r.room_id.empty()) return;
        active_room_id_ = r.room_id;
        last_room_rev_ = r.rev;
        room_active_ = (r.state != "closed");

        CeSvcRoom dst;
        dst.active = room_active_;
        dst.room_id = r.room_id;
        dst.code = r.code;
        dst.rev = static_cast<uint32_t>(r.rev);
        dst.state = r.state;
        dst.name = r.name;
        dst.topology = r.topology;
        dst.mode = r.mode;
        dst.map = static_cast<int>(r.map);
        dst.max_seats = r.max_seats;
        dst.host_seat = r.host_seat;
        dst.visibility = r.visibility;
        dst.has_password = r.has_password;
        dst.require_relay = r.require_relay;
        dst.ai_fill = r.ai_fill;
        dst.build_id = r.build_id;
        dst.local_seat = 0xFFFFu;

        dst.seats.reserve(r.seats.size());
        for (const auto &s : r.seats) {
            CeSvcSeat sv;
            sv.seat = s.seat;
            sv.kind = ParseSeatKind(s.kind);
            sv.player_id = s.player_id;
            sv.callsign = s.callsign;
            sv.tag = s.tag;
            sv.team = static_cast<int>(s.team);
            sv.be_cfg = s.be_cfg;
            sv.ready = s.ready;
            sv.rtt_ms = s.rtt_ms ? static_cast<int>(s.rtt_ms) : -1;
            sv.route = ParseRouteKind(s.route);
            if (sv.kind == 1 && sv.player_id == profile_.PlayerId()) {
                dst.local_seat = sv.seat;
            }
            dst.seats.push_back(std::move(sv));
        }

        dst.chat.reserve(r.chat.size());
        for (const auto &c : r.chat) {
            CeSvcChatMsg cm;
            cm.seq = static_cast<uint32_t>(c.seq);
            cm.seat = c.seat;
            cm.player_id = c.player_id;
            cm.callsign = c.callsign;
            cm.tag = c.tag;
            cm.text = c.text;
            cm.ts = c.ts;
            dst.chat.push_back(std::move(cm));
        }

        if (r.start_ticket.valid) {
            dst.start_ticket.valid = true;
            dst.start_ticket.match_id = r.start_ticket.match_id;
            dst.start_ticket.seed = r.start_ticket.seed;
            dst.start_ticket.level = static_cast<int>(r.start_ticket.level);
            dst.start_ticket.mode = r.start_ticket.mode;
            dst.start_ticket.topology = r.start_ticket.topology;
            dst.start_ticket.require_relay = r.start_ticket.require_relay;
            dst.start_ticket.roster_hash = r.start_ticket.roster_hash;
        }
        cached_room_ = std::move(dst);
        if (r.start_ticket.valid) {
            TryQueueMatchLaunch();
        }
    }

    void TryQueueMatchLaunch() {
        if (launch_queued_ || !client_) return;
        const CeOnlineRoomState &r = client_->CurrentRoom();
        if (!r.start_ticket.valid || r.start_ticket.slots.empty()) return;

        CE_OnlineMatchTicket mt = {};
        mt.proto = CE_ONLINE_PROTO_V1;
        CopyBounded(mt.match_id, sizeof(mt.match_id), r.start_ticket.match_id);
        CopyBounded(mt.room_id, sizeof(mt.room_id), r.room_id);
        CopyBounded(mt.lobby_origin, sizeof(mt.lobby_origin), origin_);
        CopyBounded(mt.session_token, sizeof(mt.session_token), client_->Session().session_token);
        mt.seed = r.start_ticket.seed;
        mt.level = r.start_ticket.level;
        CopyBounded(mt.mode, sizeof(mt.mode), r.start_ticket.mode);
        CopyBounded(mt.topology, sizeof(mt.topology), r.start_ticket.topology);
        mt.require_relay = r.start_ticket.require_relay ? 1u : 0u;
        if (!DecodeHexBytes(r.start_ticket.roster_hash, mt.roster_hash, sizeof(mt.roster_hash))) return;
        if (!DecodeHexBytes(r.start_ticket.match_secret, mt.match_secret, sizeof(mt.match_secret))) return;

        uint16_t slot_count = 0;
        uint16_t human_count = 0;
        bool found_local = false;
        for (const auto &s : r.start_ticket.slots) {
            if (s.sim_slot >= CE_ONLINE_MAX_MATCH_SLOTS) continue;
            CE_OnlineMatchSlot &dst = mt.slots[s.sim_slot];
            dst.seat = s.seat;
            dst.sim_slot = static_cast<uint8_t>(s.sim_slot);
            dst.kind = static_cast<uint8_t>(ParseSeatKind(s.kind));
            dst.team = static_cast<uint8_t>(s.team);
            CopyBounded(dst.be_cfg, sizeof(dst.be_cfg), s.be_cfg);
            CopyBounded(dst.callsign, sizeof(dst.callsign), s.callsign);
            CopyBounded(dst.tag, sizeof(dst.tag), s.tag);
            CopyBounded(dst.player_id, sizeof(dst.player_id), s.player_id);
            if (dst.sim_slot + 1u > slot_count) slot_count = static_cast<uint16_t>(dst.sim_slot + 1u);
            if (dst.kind == CE_ONLINE_SLOT_HUMAN) {
                ++human_count;
                if (s.player_id == profile_.PlayerId()) {
                    mt.local_seat = s.seat;
                    mt.local_sim_slot = dst.sim_slot;
                    found_local = true;
                }
            }
        }
        mt.slot_count = slot_count;
        mt.human_count = human_count;
        if (!found_local || !CeOnlineMatchTicketValid(&mt)) {
            crypto_wipe(&mt, sizeof(mt));
            return;
        }
        if (CE_LobbyQueueOnlineLaunch && CE_LobbyQueueOnlineLaunch(&mt)) {
            launch_queued_ = true;
        }
        crypto_wipe(&mt, sizeof(mt));
    }

    void HandleOpComplete(CeOnlineOpKind op, CeOnlineOpStatus st, uint64_t now_ms) {
        if (st == CeOnlineOpStatus::Failed) {
            std::string msg = client_->LastErrorMessage().empty()
                                  ? "Lobby server request failed."
                                  : client_->LastErrorMessage();
            if (op == CeOnlineOpKind::Health || op == CeOnlineOpKind::Authenticate) {
                conn_status_ = 3;
                status_detail_ = msg;
                want_connect_ = false;
                if (lobbies_loading_) {
                    lobbies_loading_ = false;
                    lobbies_err_ = msg;
                }
                pending_queue_.clear();
            } else if (op == CeOnlineOpKind::ListLobbies) {
                lobbies_loading_ = false;
                lobbies_err_ = msg;
            } else if (op == CeOnlineOpKind::ResolveCode) {
                last_resolve_ = CeSvcResolveResult{};
                last_resolve_.resolved = true;
                last_resolve_.found = false;
                last_resolve_.error = msg;
                resolve_ready_ = true;
            } else if (op == CeOnlineOpKind::PollRoom && client_->LastHttpCode() == 404) {
                room_active_ = false;
                active_room_id_.clear();
            }
            return;
        }

        if (client_->LastRttMs() > 0) {
            ping_ms_ = static_cast<int>(client_->LastRttMs());
        }

        switch (op) {
        case CeOnlineOpKind::Health:
            health_ok_ = client_->LastHealth().ok && client_->LastHealth().enabled;
            if (!health_ok_) {
                conn_status_ = 3;
                status_detail_ = "Lobby service is currently disabled.";
                want_connect_ = false;
            }
            break;
        case CeOnlineOpKind::Authenticate:
            conn_status_ = 2;
            status_detail_ = "Connected";
            break;
        case CeOnlineOpKind::ListLobbies: {
            lobbies_loading_ = false;
            lobbies_err_.clear();
            cached_lobbies_.clear();
            for (const auto &rm : client_->LastLobbyList().rooms) {
                CeSvcRoomSummary s;
                s.room_id = rm.room_id;
                s.code = rm.code;
                s.name = rm.name;
                s.host_callsign = rm.host_callsign;
                s.host_tag = rm.host_tag;
                s.topology = rm.topology;
                s.mode = rm.mode;
                s.state = rm.state;
                s.build_id = rm.build_id;
                s.map = static_cast<int>(rm.map);
                s.used_seats = rm.used_seats;
                s.human_seats = rm.human_seats;
                s.max_seats = rm.max_seats;
                s.has_password = rm.has_password;
                s.require_relay = rm.require_relay;
                s.compatible = rm.compatible;
                cached_lobbies_.push_back(std::move(s));
            }
            break;
        }
        case CeOnlineOpKind::ResolveCode: {
            last_resolve_ = CeSvcResolveResult{};
            last_resolve_.resolved = true;
            last_resolve_.found = !client_->LastResolve().room_id.empty();
            last_resolve_.room_id = client_->LastResolve().room_id;
            last_resolve_.has_password = client_->LastResolve().has_password;
            last_resolve_.state = client_->LastResolve().state;
            resolve_ready_ = true;
            break;
        }
        case CeOnlineOpKind::CreateLobby:
        case CeOnlineOpKind::JoinRoom:
        case CeOnlineOpKind::UpdateRoom:
        case CeOnlineOpKind::SendChat:
        case CeOnlineOpKind::StartMatch:
            last_room_poll_ms_ = now_ms;
            SyncRoomFromClient();
            break;
        case CeOnlineOpKind::PollRoom:
            if (!client_->LastPollUnchanged()) {
                SyncRoomFromClient();
            }
            break;
        default:
            break;
        }
    }

    bool inited_ = false;
    bool want_connect_ = false;
    bool health_ok_ = false;
    bool lobbies_loading_ = false;
    bool room_active_ = false;
    bool resolve_ready_ = false;
    bool launch_queued_ = false;
    int conn_status_ = 0;
    int ping_ms_ = -1;
    uint64_t last_room_rev_ = 0;
    uint64_t last_room_poll_ms_ = 0;
    std::string origin_;
    std::string content_id_;
    std::string status_detail_;
    std::string lobbies_err_;
    std::string active_room_id_;
    CeOnlineProfile profile_;
    std::unique_ptr<CeOnlineLobbyClient> client_;
    std::vector<CeSvcRoomSummary> cached_lobbies_;
    CeSvcRoom cached_room_;
    CeSvcResolveResult last_resolve_;
    std::deque<PendingAction> pending_queue_;
};

// ---------------------------------------------------------------------------
// Live Fresh-Process Star Match Bridge (runs inside restarted match process)
// ---------------------------------------------------------------------------
class CeLiveOnlineStarBridge final : public ICeOnlineStarBridge {
public:
    CeLiveOnlineStarBridge() = default;
    ~CeLiveOnlineStarBridge() override {
        if (transport_) transport_->Leave(0);
    }

    bool Init(const CE_OnlineMatchTicket &ticket, const uint8_t compat_digest[32]) {
        ticket_ = ticket;
        is_host_ = (ticket_.local_sim_slot == 0);
        expected_guests_.clear();
        for (uint16_t i = 0; i < ticket_.slot_count && i < CE_ONLINE_MAX_MATCH_SLOTS; ++i) {
            if (ticket_.slots[i].kind == CE_ONLINE_SLOT_HUMAN && ticket_.slots[i].sim_slot != 0) {
                GuestLink gl;
                gl.seat = ticket_.slots[i].seat;
                gl.sim_slot = ticket_.slots[i].sim_slot;
                gl.require_relay = (ticket_.require_relay != 0);
                expected_guests_.push_back(gl);
            }
        }
        if (expected_guests_.empty()) return false;

        const std::string origin(ticket_.lobby_origin);
        direct_mode_ = (origin.compare(0, 9, "direct://") == 0);

        CeStarConfig cfg;
        cfg.role = is_host_ ? CeStarRole::Host : CeStarRole::Guest;
        cfg.use_ice = !direct_mode_;
        cfg.require_relay = (ticket_.require_relay != 0);
        cfg.max_guests = static_cast<uint16_t>(ticket_.slot_count > 1 ? ticket_.slot_count - 1 : 1);
        cfg.local_seat = ticket_.local_seat;
        cfg.local_sim_slot = ticket_.local_sim_slot;
        std::memcpy(cfg.match_secret.data(), ticket_.match_secret, CE_STAR_SECRET_BYTES);
        std::memcpy(cfg.compatibility.data(), compat_digest, CE_STAR_DIGEST_BYTES);
        std::memcpy(cfg.roster_hash.data(), ticket_.roster_hash, CE_STAR_DIGEST_BYTES);
        cfg.version = kOnlineBuildId;
        for (const auto &g : expected_guests_) {
            cfg.expected_guests.push_back({g.seat, g.sim_slot});
        }

        if (direct_mode_) {
            std::string hp = origin.substr(9);
            size_t colon = hp.rfind(':');
            if (colon == std::string::npos) return false;
            std::string ip = hp.substr(0, colon);
            uint16_t port = static_cast<uint16_t>(std::strtoul(hp.substr(colon + 1).c_str(), nullptr, 10));
            if (port == 0) return false;
            if (is_host_) {
                cfg.bind_ipv4 = ip;
                cfg.bind_port = port;
            } else {
                cfg.remote_ipv4 = ip;
                cfg.remote_port = port;
            }
        } else {
            cfg.bind_ipv4 = "127.0.0.1";
            cfg.bind_port = 0;
            const bool allow_loopback = IsLoopbackHttpOrigin(origin);
            auto https = CeCreateOnlineHttps(origin.c_str(), allow_loopback);
            if (!https) return false;
            lobby_.reset(new CeOnlineLobbyClient(std::move(https)));
            CeOnlineSession sess;
            sess.session_token = ticket_.session_token;
            lobby_->SetSessionForTest(sess);
        }

        CeStarError err = CeStarError::Ok;
        transport_ = CeStarTransport::Create(cfg, &err);
        return transport_ != nullptr;
    }

    void Poll(uint64_t now_ms) override {
        if (!transport_) return;
        if (!direct_mode_) {
            PumpSignalingAndIce(now_ms);
        }
        transport_->Poll(now_ms, 64);
        DrainTransportEvents();
    }

    bool IsConnected() const override {
        if (!transport_) return false;
        if (is_host_) {
            return transport_->ActiveGuestCount() >= static_cast<uint16_t>(expected_guests_.size());
        }
        return transport_->IsAuthenticated() && !transport_->IsHostLost();
    }

    bool SendGuestInput(uint8_t, uint32_t tick, const uint8_t *data, size_t len) override {
        return transport_ && transport_->SendGuestInput(tick, data, len);
    }

    bool SendHostBundle(uint32_t tick, const uint8_t *bundle, size_t len) override {
        if (!transport_) return false;
        CeStarBitmask mask = transport_->ActiveMask();
        return transport_->BroadcastInputBundle(tick, mask, bundle, len);
    }

    bool SendHash(uint8_t, uint32_t tick, uint64_t state_hash) override {
        return transport_ && transport_->SendHash(tick, state_hash);
    }

    bool SendDesync(const CeNPeerDesyncReport &report) override {
        if (!transport_) return false;
        CeStarBitmask mask = CeStarBitmask::FromMask8(report.divergingMask, CE_ONLINE_MAX_MATCH_SLOTS);
        return transport_->BroadcastDesync(report.tick, report.referenceSlot, report.divergingSlot,
                                           mask, report.expectedHash, report.actualHash);
    }

    bool SendChat(uint8_t, const char *text) override {
        return transport_ && text && transport_->SendChat(text);
    }

    bool SendLeave(uint8_t) override {
        if (!transport_) return false;
        transport_->Leave(0);
        return true;
    }

    bool PopEvent(CeOnlineStarEvent *out_event) override {
        if (!out_event || events_.empty()) return false;
        *out_event = events_.front();
        events_.pop_front();
        return true;
    }

private:
    struct GuestLink {
        uint16_t seat = 0;
        uint8_t sim_slot = 0;
        bool require_relay = false;
        bool turn_fetched = false;
        bool bridge_attached = false;
        bool offer_sent = false;
        bool answer_applied = false;
        bool retried_relay = false;
    };

    void PumpSignalingAndIce(uint64_t now_ms) {
#if defined(CE_NETWORK_ICE)
        if (!lobby_ || !transport_) return;
        if (lobby_->Busy()) {
            CeOnlineOpStatus st = lobby_->Poll(now_ms);
            if (st == CeOnlineOpStatus::Pending) return;
            HandleSignalOpDone(lobby_->ActiveOp(), st);
            if (lobby_->Busy()) return;
        }

        if (is_host_) {
            // 1. Fetch per-guest TURN leases & attach independent CeIceBridge per guest.
            for (size_t i = 0; i < expected_guests_.size(); ++i) {
                GuestLink &g = expected_guests_[i];
                if (g.bridge_attached && !g.retried_relay && !g.require_relay &&
                    transport_->IsGuestIceFailed(g.sim_slot)) {
                    // Per-guest automatic TURN fallback if STUN direct fails.
                    transport_->DetachGuestIceBridge(g.sim_slot);
                    g.require_relay = true;
                    g.retried_relay = true;
                    g.turn_fetched = false;
                    g.bridge_attached = false;
                    g.offer_sent = false;
                    g.answer_applied = false;
                }
                if (!g.turn_fetched) {
                    active_guest_index_ = i;
                    lobby_->BeginFetchTurn(ticket_.room_id, ticket_.match_id, g.seat, now_ms);
                    return;
                }
                if (g.bridge_attached && !g.offer_sent) {
                    std::string sdp;
                    if (transport_->GetGuestLocalIceDescription(g.sim_slot, sdp)) {
                        active_guest_index_ = i;
                        lobby_->BeginSendSignal(ticket_.room_id, ticket_.match_id,
                                                ticket_.local_seat, g.seat, "offer", sdp, now_ms);
                        return;
                    }
                }
            }
            // 2. Poll guest answers while any guest has not applied answer yet.
            bool need_answers = false;
            for (const auto &g : expected_guests_) {
                if (g.offer_sent && !g.answer_applied) { need_answers = true; break; }
            }
            if (need_answers && now_ms >= last_sig_poll_ms_ + 500u) {
                last_sig_poll_ms_ = now_ms;
                lobby_->BeginPollSignal(ticket_.room_id, ticket_.match_id, signal_seq_, now_ms);
            }
        } else {
            // Guest: fetch own TURN -> attach bridge -> poll Host offer -> send answer.
            if (guest_bridge_attached_ && !guest_retried_relay_ && !guest_require_relay_ &&
                transport_->IsIceFailed()) {
                guest_require_relay_ = true;
                guest_retried_relay_ = true;
                guest_turn_fetched_ = false;
                guest_bridge_attached_ = false;
                guest_offer_applied_ = false;
                guest_answer_sent_ = false;
            }
            if (!guest_turn_fetched_) {
                lobby_->BeginFetchTurn(ticket_.room_id, ticket_.match_id, ticket_.local_seat, now_ms);
                return;
            }
            if (guest_bridge_attached_ && !guest_offer_applied_ && now_ms >= last_sig_poll_ms_ + 500u) {
                last_sig_poll_ms_ = now_ms;
                lobby_->BeginPollSignal(ticket_.room_id, ticket_.match_id, signal_seq_, now_ms);
                return;
            }
            if (guest_offer_applied_ && !guest_answer_sent_) {
                std::string sdp;
                if (transport_->GetLocalIceDescription(sdp)) {
                    lobby_->BeginSendSignal(ticket_.room_id, ticket_.match_id,
                                            ticket_.local_seat, 0, "answer", sdp, now_ms);
                    return;
                }
            }
        }
#else
        (void)now_ms;
#endif
    }

    void HandleSignalOpDone(CeOnlineOpKind op, CeOnlineOpStatus st) {
#if defined(CE_NETWORK_ICE)
        if (st != CeOnlineOpStatus::Completed) return;
        if (op == CeOnlineOpKind::FetchTurn) {
            ParsedJuiceConfig parsed;
            parsed.Build(lobby_->LastTurnBundle());
            if (is_host_ && active_guest_index_ < expected_guests_.size()) {
                GuestLink &g = expected_guests_[active_guest_index_];
                g.turn_fetched = true;
                g.bridge_attached = transport_->AttachGuestIceBridge(g.sim_slot, parsed.cfg, g.require_relay);
            } else if (!is_host_) {
                guest_turn_fetched_ = true;
                guest_require_relay_ = (ticket_.require_relay != 0) || guest_retried_relay_;
                guest_bridge_attached_ = transport_->AttachHostIceBridge(parsed.cfg, guest_require_relay_);
            }
        } else if (op == CeOnlineOpKind::SendSignal) {
            if (is_host_ && active_guest_index_ < expected_guests_.size()) {
                expected_guests_[active_guest_index_].offer_sent = true;
            } else if (!is_host_) {
                guest_answer_sent_ = true;
            }
        } else if (op == CeOnlineOpKind::PollSignal) {
            const auto &res = lobby_->LastSignalPoll();
            if (res.seq > signal_seq_) signal_seq_ = res.seq;
            for (const auto &msg : res.messages) {
                if (is_host_ && msg.kind == "answer") {
                    for (auto &g : expected_guests_) {
                        if (g.seat == msg.from_seat && g.bridge_attached && !g.answer_applied) {
                            g.answer_applied = transport_->SetGuestRemoteIceDescription(g.sim_slot, msg.sdp);
                        }
                    }
                } else if (!is_host_ && msg.kind == "offer" && guest_bridge_attached_ && !guest_offer_applied_) {
                    guest_offer_applied_ = transport_->SetRemoteIceDescription(msg.sdp);
                }
            }
        }
#else
        (void)op; (void)st;
#endif
    }

    void DrainTransportEvents() {
        CeStarEvent ev;
        while (transport_->PopEvent(ev)) {
            CeOnlineStarEvent out = {};
            switch (ev.kind) {
            case CeStarEventKind::InputGuest:
                out.kind = CE_STAR_EVT_GUEST_INPUT;
                out.from_sim_slot = static_cast<uint8_t>(ev.sim_slot);
                out.tick = ev.tick;
                out.payload_len = static_cast<uint32_t>(std::min(ev.payload.size(), sizeof(out.payload)));
                if (out.payload_len) std::memcpy(out.payload, ev.payload.data(), out.payload_len);
                events_.push_back(out);
                break;
            case CeStarEventKind::InputBundle:
                out.kind = CE_STAR_EVT_HOST_BUNDLE;
                out.from_sim_slot = 0;
                out.tick = ev.tick;
                out.payload_len = static_cast<uint32_t>(std::min(ev.payload.size(), sizeof(out.payload)));
                if (out.payload_len) std::memcpy(out.payload, ev.payload.data(), out.payload_len);
                events_.push_back(out);
                break;
            case CeStarEventKind::Hash:
                out.kind = CE_STAR_EVT_HASH;
                out.from_sim_slot = static_cast<uint8_t>(ev.sim_slot);
                out.tick = ev.tick;
                out.hash = ev.hash;
                events_.push_back(out);
                break;
            case CeStarEventKind::Desync:
                out.kind = CE_STAR_EVT_DESYNC;
                out.tick = ev.tick;
                out.reference_slot = static_cast<uint8_t>(ev.ref_slot);
                out.diverging_slot = static_cast<uint8_t>(ev.diverging_slot);
                out.diverging_mask = ev.mask.ToMask8();
                out.expected_hash = ev.expected_hash;
                out.actual_hash = ev.actual_hash;
                events_.push_back(out);
                break;
            case CeStarEventKind::Chat:
                out.kind = CE_STAR_EVT_CHAT;
                out.from_sim_slot = static_cast<uint8_t>(ev.sim_slot);
                CopyBounded(out.chat_text, sizeof(out.chat_text), ev.text);
                events_.push_back(out);
                break;
            case CeStarEventKind::PingReport:
                for (const auto &p : ev.pings) {
                    CeOnlineStarEvent pm = {};
                    pm.kind = CE_STAR_EVT_PEER_METRICS;
                    pm.from_sim_slot = static_cast<uint8_t>(p.sim_slot);
                    pm.rtt_ms = p.rtt_ms;
                    pm.route = static_cast<uint8_t>(p.route);
                    events_.push_back(pm);
                }
                break;
            case CeStarEventKind::PeerDisconnected:
                out.kind = CE_STAR_EVT_PEER_DISCONNECT;
                out.from_sim_slot = static_cast<uint8_t>(ev.sim_slot);
                events_.push_back(out);
                break;
            case CeStarEventKind::HostDisconnected:
                out.kind = CE_STAR_EVT_HOST_LOST;
                out.from_sim_slot = 0;
                events_.push_back(out);
                break;
            default:
                break;
            }
        }
    }

    CE_OnlineMatchTicket ticket_{};
    bool is_host_ = false;
    bool direct_mode_ = false;
    std::vector<GuestLink> expected_guests_;
    size_t active_guest_index_ = 0;
    bool guest_turn_fetched_ = false;
    bool guest_bridge_attached_ = false;
    bool guest_offer_applied_ = false;
    bool guest_answer_sent_ = false;
    bool guest_require_relay_ = false;
    bool guest_retried_relay_ = false;
    uint64_t signal_seq_ = 0;
    uint64_t last_sig_poll_ms_ = 0;
    std::unique_ptr<CeOnlineLobbyClient> lobby_;
    std::unique_ptr<CeStarTransport> transport_;
    std::deque<CeOnlineStarEvent> events_;
};

CeLiveOnlineService *gOnlineService = nullptr;
std::unique_ptr<CeLiveOnlineStarBridge> gLiveMatchBridge;

} // namespace

CeOnlineServiceFacade *PortCeOnlineServiceInstance() {
    if (!gOnlineService) {
        gOnlineService = new CeLiveOnlineService();
    }
    return gOnlineService;
}

extern "C" int PortCeOnlineServiceAttachMatchBridge(const uint8_t compat_digest[32]) {
    CE_OnlineMatchTicket ticket = {};
    if (!PortCeOnlineMatchGetTicket(&ticket) || !compat_digest) return 0;
    auto bridge = std::unique_ptr<CeLiveOnlineStarBridge>(new CeLiveOnlineStarBridge());
    if (!bridge->Init(ticket, compat_digest)) {
        crypto_wipe(&ticket, sizeof(ticket));
        return 0;
    }
    crypto_wipe(&ticket, sizeof(ticket));
    gLiveMatchBridge = std::move(bridge);
    PortCeOnlineMatchSetBridge(gLiveMatchBridge.get());
    return 1;
}

extern "C" void PortCeOnlineServiceDetachMatchBridge(void) {
    PortCeOnlineMatchSetBridge(nullptr);
    gLiveMatchBridge.reset();
}

#endif // !CE_NETWORK_OFF
