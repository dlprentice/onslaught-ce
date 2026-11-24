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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

const char *PortUserDir();
extern "C" int CE_SettingGetText(const char *key, char *out, int n) __attribute__((weak));
extern "C" int CE_LobbyQueueOnlineLaunch(const CE_OnlineMatchTicket *ticket) __attribute__((weak));
extern "C" int CE_LobbyRestartDue(void) __attribute__((weak));
extern "C" int CE_CheatOn(int n) __attribute__((weak));

#ifdef CE_VERSION
static const char kOnlineBuildId[] = CE_VERSION;
#else
static const char kOnlineBuildId[] = "0.1.16";
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

std::string ComputeContentId() {
    std::string canonical = "onslaught-ce-content-v1\n";
    std::string raw_mods;
    if (CE_SettingGetText) {
        char mods_buf[1024] = {};
        if (CE_SettingGetText("mods", mods_buf, static_cast<int>(sizeof(mods_buf))) && mods_buf[0]) {
            raw_mods = mods_buf;
        }
    }
    std::string cleaned_mods;
    size_t pos = 0;
    while (pos < raw_mods.size()) {
        size_t comma = raw_mods.find(',', pos);
        if (comma == std::string::npos) comma = raw_mods.size();
        size_t a = pos, b = comma;
        while (a < b && (raw_mods[a] == ' ' || raw_mods[a] == '\t' || raw_mods[a] == '\r' || raw_mods[a] == '\n')) ++a;
        while (b > a && (raw_mods[b - 1] == ' ' || raw_mods[b - 1] == '\t' || raw_mods[b - 1] == '\r' || raw_mods[b - 1] == '\n')) --b;
        if (b > a) {
            if (!cleaned_mods.empty()) cleaned_mods.push_back(',');
            cleaned_mods.append(raw_mods, a, b - a);
        }
        pos = (comma < raw_mods.size()) ? comma + 1 : comma;
    }
    if (cleaned_mods.empty()) {
        canonical += "retail\n";
    } else {
        canonical += "mods:";
        canonical += cleaned_mods;
        canonical += "\n";
    }
    uint8_t digest[32] = {};
    crypto_blake2b(digest, sizeof(digest),
                   reinterpret_cast<const uint8_t *>(canonical.data()), canonical.size());
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (size_t i = 0; i < sizeof(digest); ++i) {
        hex.push_back(kHex[digest[i] >> 4]);
        hex.push_back(kHex[digest[i] & 0x0Fu]);
    }
    return hex;
}

bool AnyCheatEnabled() {
    if (!CE_CheatOn) return false;
    for (int i = 0; i < 6; ++i) {
        if (CE_CheatOn(i)) return true;
    }
    return false;
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
// Rejects TCP/TLS TURN URLs since libjuice speaks UDP TURN only.
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
    if (q != std::string::npos) {
        const std::string query = url.substr(q + 1);
        if (*out_is_turn && query != "transport=udp") return false;
    }
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

    ~ParsedJuiceConfig() {
        WipeTurns();
    }

    void WipeTurns() {
        for (auto &t : turns) {
            if (!t.user.empty()) crypto_wipe(&t.user[0], t.user.size());
            if (!t.pass.empty()) crypto_wipe(&t.pass[0], t.pass.size());
        }
        turns.clear();
        turn_structs.clear();
    }

    bool BuildStunOnly() {
        WipeTurns();
        stun_host = "stun.cloudflare.com";
        stun_port = 3478;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
        cfg.stun_server_host = stun_host.c_str();
        cfg.stun_server_port = stun_port;
        return true;
    }

    bool Build(const CeOnlineTurnBundle &bundle, bool require_relay) {
        WipeTurns();
        stun_host = "stun.cloudflare.com";
        stun_port = 3478;
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
                    bool dup = false;
                    for (const auto &ex : turns) {
                        if (ex.host == host && ex.port == port) { dup = true; break; }
                    }
                    if (!dup) {
                        TurnEntry e;
                        e.host = host;
                        e.port = port;
                        e.user = srv.username;
                        e.pass = srv.credential;
                        turns.push_back(std::move(e));
                    }
                }
            }
        }
        auto port_rank = [](uint16_t p) -> int {
            if (p == 3478) return 0;
            if (p == 53) return 1;
            if (p == 443) return 2;
            return 3;
        };
        std::stable_sort(turns.begin(), turns.end(), [&](const TurnEntry &a, const TurnEntry &b) {
            return port_rank(a.port) < port_rank(b.port);
        });
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
        if (require_relay) {
            // Relay Only mode: never send STUN binding requests outside TURN.
            if (turns.empty()) return false;
            cfg.stun_server_host = nullptr;
            cfg.stun_server_port = 0;
        } else {
            cfg.stun_server_host = stun_host.c_str();
            cfg.stun_server_port = stun_port;
        }
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
        content_id_ = ComputeContentId();
        origin_ = ResolveConfiguredLobbyOrigin(profile_);
        RebuildClient();
    }

    void RebuildClient() {
        ++room_generation_;
        op_room_generation_ = 0;
        client_.reset();
        pending_queue_.clear();
        active_room_id_.clear();
        cached_room_ = CeSvcRoom{};
        last_room_rev_ = 0;
        room_active_ = false;
        leave_pending_ = false;
        launch_queued_ = false;
        cancelled_inflight_entry_ = false;
        has_active_action_ = false;
        if (origin_.empty()) {
            conn_status_ = 0;
            status_detail_ = "Online lobby service is currently unavailable.";
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
        if (now_ms > last_now_ms_) last_now_ms_ = now_ms;
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
        if (room_active_ && !leave_pending_ && !active_room_id_.empty() && now_ms >= last_room_poll_ms_ + 1000u) {
            last_room_poll_ms_ = now_ms;
            uint32_t rtt = ping_ms_ >= 0 ? static_cast<uint32_t>(ping_ms_) : 0u;
            if (client_->BeginPollRoom(active_room_id_, last_room_rev_, rtt, now_ms)) {
                op_room_generation_ = room_generation_;
            }
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

    void ResetRoomLifecycle(bool cancel_all_ops) {
        ++room_generation_;
        op_room_generation_ = 0;
        has_active_action_ = false;
        cancelled_inflight_entry_ = false;
        resolve_ready_ = false;
        launch_queued_ = false;
        if (cancel_all_ops) {
            lobbies_loading_ = false;
            pending_queue_.clear();
        } else {
            for (auto it = pending_queue_.begin(); it != pending_queue_.end();) {
                if (it->kind == ActionKind::CreateRoom || it->kind == ActionKind::ResolveCode ||
                    it->kind == ActionKind::JoinRoom || it->kind == ActionKind::LeaveRoom ||
                    it->kind == ActionKind::UpdateSelf || it->kind == ActionKind::HostUpdate ||
                    it->kind == ActionKind::SendChat || it->kind == ActionKind::StartMatch) {
                    it = pending_queue_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        if (client_ && client_->Busy()) {
            const CeOnlineOpKind cur = client_->ActiveOp();
            if (cancel_all_ops || cur == CeOnlineOpKind::ResolveCode ||
                cur == CeOnlineOpKind::PollRoom || cur == CeOnlineOpKind::UpdateRoom ||
                cur == CeOnlineOpKind::SendChat || cur == CeOnlineOpKind::StartMatch ||
                cur == CeOnlineOpKind::CreateLobby || cur == CeOnlineOpKind::JoinRoom ||
                cur == CeOnlineOpKind::LeaveRoom) {
                client_->Cancel();
            }
        }
        room_active_ = false;
        active_room_id_.clear();
        cached_room_ = CeSvcRoom{};
        last_room_rev_ = 0;
        last_room_poll_ms_ = 0;
        RefreshLeavePending();
    }

    bool SaveCallsign(const std::string &callsign, std::string *err_out) override {
        EnsureInit();
        if (!profile_.Loaded()) {
            if (err_out) *err_out = "Online profile unavailable.";
            return false;
        }
        if (!profile_.SetCallsign(callsign, true, err_out)) return false;
        // Cancel active room/lobby ops, clear room state/generation, and re-authenticate on next poll.
        ResetRoomLifecycle(true);
        if (client_) {
            client_->ClearSession();
            want_connect_ = true;
            conn_status_ = 1;
            status_detail_ = "Connecting to lobby server...";
        }
        return true;
    }

    void RequestReconnect() override {
        EnsureInit();
        const char *user_dir = PortUserDir();
        if (user_dir && user_dir[0]) {
            std::string err;
            profile_.LoadOrCreate(user_dir, &err);
        }
        content_id_ = ComputeContentId();
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
        if (AnyCheatEnabled()) {
            if (err_out) *err_out = "Turn off cheats in Onslaught CE > Cheats to play online.";
            return false;
        }
        if (!client_ || !client_->Authenticated()) {
            if (err_out) *err_out = "Not connected to lobby server.";
            return false;
        }
        ++room_generation_;
        leave_pending_ = false;
        cancelled_inflight_entry_ = false;
        PendingAction act;
        act.kind = ActionKind::CreateRoom;
        act.generation = room_generation_;
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
        act.generation = room_generation_;
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
        if (AnyCheatEnabled()) {
            if (err_out) *err_out = "Turn off cheats in Onslaught CE > Cheats to play online.";
            return false;
        }
        if (!client_ || !client_->Authenticated()) {
            if (err_out) *err_out = "Not connected to lobby server.";
            return false;
        }
        ++room_generation_;
        leave_pending_ = false;
        cancelled_inflight_entry_ = false;
        PendingAction act;
        act.kind = ActionKind::JoinRoom;
        act.generation = room_generation_;
        act.room_id = room_id;
        act.password = password;
        act.be_cfg = be_cfg;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool IsLaunchPreserved() const {
        if (launch_queued_) return true;
        if (CE_LobbyRestartDue && CE_LobbyRestartDue() != 0) return true;
        if (room_active_ && (cached_room_.state == "starting" || cached_room_.state == "in_match")) return true;
        return false;
    }

    void RequestLeaveRoom() override {
        EnsureInit();
        if (IsLaunchPreserved()) return;
        ++room_generation_;
        resolve_ready_ = false;
        // Purge queued room-entry and in-room mutations so a cancelled flow never fires later.
        for (auto it = pending_queue_.begin(); it != pending_queue_.end();) {
            if (it->kind == ActionKind::CreateRoom || it->kind == ActionKind::ResolveCode ||
                it->kind == ActionKind::JoinRoom || it->kind == ActionKind::UpdateSelf ||
                it->kind == ActionKind::HostUpdate || it->kind == ActionKind::SendChat ||
                it->kind == ActionKind::StartMatch) {
                it = pending_queue_.erase(it);
            } else {
                ++it;
            }
        }
        if (client_ && client_->Busy()) {
            const CeOnlineOpKind cur = client_->ActiveOp();
            if (cur == CeOnlineOpKind::ResolveCode || cur == CeOnlineOpKind::PollRoom ||
                cur == CeOnlineOpKind::UpdateRoom || cur == CeOnlineOpKind::SendChat ||
                cur == CeOnlineOpKind::StartMatch) {
                client_->Cancel();
                has_active_action_ = false;
                op_room_generation_ = 0;
            } else if (cur == CeOnlineOpKind::CreateLobby || cur == CeOnlineOpKind::JoinRoom) {
                // Keep HTTP completion tracked so if the server created/joined a room we cleanly /leave it.
                cancelled_inflight_entry_ = true;
            }
        }
        if (client_ && client_->Authenticated() && !active_room_id_.empty()) {
            PendingAction act;
            act.kind = ActionKind::LeaveRoom;
            act.generation = room_generation_;
            act.room_id = active_room_id_;
            pending_queue_.push_back(std::move(act));
        }
        RefreshLeavePending();
        room_active_ = false;
        active_room_id_.clear();
        cached_room_ = CeSvcRoom{};
        last_room_rev_ = 0;
        launch_queued_ = false;
    }

    bool GetRoom(CeSvcRoom *out_room) override {
        EnsureInit();
        if (!out_room) return false;
        if (!room_active_ || leave_pending_) {
            *out_room = CeSvcRoom{};
            return false;
        }
        *out_room = cached_room_;
        return true;
    }

    bool RequestUpdateSelf(int team, const std::string &be_cfg, bool ready,
                           std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated() || leave_pending_ || !room_active_ || active_room_id_.empty()) {
            if (err_out) *err_out = "Not in a room.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::UpdateSelf;
        act.generation = room_generation_;
        act.room_id = active_room_id_;
        act.team = team;
        act.be_cfg = be_cfg;
        act.ready = ready;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool RequestHostUpdate(const CeSvcHostPatch &patch, std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated() || leave_pending_ || !room_active_ || active_room_id_.empty()) {
            if (err_out) *err_out = "Not in a room.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::HostUpdate;
        act.generation = room_generation_;
        act.room_id = active_room_id_;
        act.patch = patch;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool RequestSendChat(const std::string &text, std::string *err_out) override {
        EnsureInit();
        if (!client_ || !client_->Authenticated() || leave_pending_ || !room_active_ || active_room_id_.empty()) {
            if (err_out) *err_out = "Not in a room.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::SendChat;
        act.generation = room_generation_;
        act.room_id = active_room_id_;
        act.text = text;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    bool RequestStartMatch(std::string *err_out) override {
        EnsureInit();
        if (AnyCheatEnabled()) {
            if (err_out) *err_out = "Turn off cheats in Onslaught CE > Cheats to play online.";
            return false;
        }
        if (!client_ || !client_->Authenticated() || leave_pending_ || !room_active_ || active_room_id_.empty()) {
            if (err_out) *err_out = "Not in a room.";
            return false;
        }
        PendingAction act;
        act.kind = ActionKind::StartMatch;
        act.generation = room_generation_;
        act.room_id = active_room_id_;
        pending_queue_.push_back(std::move(act));
        return true;
    }

    void NotifyMatchTicketReady() override {
        TryQueueMatchLaunch();
    }

    bool TakeOpError(std::string *out) override {
        if (op_error_.empty()) return false;
        if (out) *out = op_error_;
        op_error_.clear();
        return true;
    }

    void FlushLeave(uint64_t now_ms) override {
        if (IsLaunchPreserved()) return;
        if (!client_ || !client_->Authenticated()) return;
        // Pump any queued or in-flight LeaveRoom (or cancelled entry op) using a monotonic clock base.
        const auto wall_start = std::chrono::steady_clock::now();
        uint64_t base_ms = std::max(now_ms, last_now_ms_);
        if (base_ms == 0u) {
            base_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    wall_start.time_since_epoch()).count());
            if (base_ms == 0u) base_ms = 1u;
        }
        for (int step = 0; step < 35; ++step) {
            bool has_leave = client_->Busy() &&
                             (client_->ActiveOp() == CeOnlineOpKind::LeaveRoom || cancelled_inflight_entry_);
            for (const auto &a : pending_queue_) {
                if (a.kind == ActionKind::LeaveRoom) { has_leave = true; break; }
            }
            if (!has_leave) break;
            const uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - wall_start).count());
            const uint64_t tick_ms = base_ms + std::max<uint64_t>(elapsed, static_cast<uint64_t>(step) * 10u);
            Poll(tick_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
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
        uint64_t generation = 0;
        bool include_incompatible = false;
        bool retried_401 = false;
        CeSvcCreateParams create;
        std::string room_id;
        std::string password;
        std::string be_cfg;
        std::string text;
        int team = 0;
        bool ready = false;
        CeSvcHostPatch patch;
    };

    void RefreshLeavePending() {
        leave_pending_ = cancelled_inflight_entry_ ||
                         (client_ && client_->Busy() && client_->ActiveOp() == CeOnlineOpKind::LeaveRoom);
        if (!leave_pending_) {
            for (const auto &a : pending_queue_) {
                if (a.kind == ActionKind::LeaveRoom) {
                    leave_pending_ = true;
                    break;
                }
            }
        }
    }

    void DispatchAction(const PendingAction &act, uint64_t now_ms) {
        if (!client_) return;
        if (act.kind != ActionKind::ListLobbies && act.kind != ActionKind::ResolveCode &&
            act.kind != ActionKind::LeaveRoom && act.generation != room_generation_) {
            return;
        }
        bool started = false;
        switch (act.kind) {
        case ActionKind::ListLobbies: {
            CeOnlineListLobbiesReq req;
            req.cursor = 0;
            req.limit = 32;
            req.build_id = kOnlineBuildId;
            req.content_id = content_id_;
            req.include_incompatible = act.include_incompatible;
            started = client_->BeginListLobbies(req, now_ms);
            if (!started) {
                lobbies_loading_ = false;
                lobbies_err_ = client_->LastErrorMessage().empty()
                    ? "Could not start lobby list request."
                    : client_->LastErrorMessage();
            }
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
            started = client_->BeginCreateLobby(req, now_ms);
            if (!started) {
                op_error_ = client_->LastErrorMessage().empty()
                    ? "Could not start lobby creation request."
                    : client_->LastErrorMessage();
            }
            break;
        }
        case ActionKind::ResolveCode:
            started = client_->BeginResolveCode(act.text, now_ms);
            if (!started) {
                last_resolve_ = CeSvcResolveResult{};
                last_resolve_.resolved = true;
                last_resolve_.found = false;
                last_resolve_.error = client_->LastErrorMessage().empty()
                    ? "Could not resolve room code."
                    : client_->LastErrorMessage();
                resolve_ready_ = true;
            }
            break;
        case ActionKind::JoinRoom: {
            CeOnlineJoinRoomReq req;
            req.password = act.password;
            req.build_id = kOnlineBuildId;
            req.content_id = content_id_;
            req.be_cfg = act.be_cfg;
            started = client_->BeginJoinRoom(act.room_id, req, now_ms);
            if (!started) {
                op_error_ = client_->LastErrorMessage().empty()
                    ? "Could not start join room request."
                    : client_->LastErrorMessage();
            }
            break;
        }
        case ActionKind::LeaveRoom:
            started = client_->BeginLeaveRoom(act.room_id, now_ms);
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
            started = client_->BeginUpdateRoom(act.room_id, req, now_ms);
            if (!started) {
                op_error_ = client_->LastErrorMessage().empty()
                    ? "Could not update player seat."
                    : client_->LastErrorMessage();
            }
            break;
        }
        case ActionKind::HostUpdate: {
            CeOnlineUpdateRoomReq req;
            if (act.patch.has_map) { req.has_map = true; req.map = static_cast<uint32_t>(act.patch.map); }
            if (act.patch.has_mode) { req.has_mode = true; req.mode = act.patch.mode; }
            if (act.patch.has_max_seats) { req.has_max_seats = true; req.max_seats = act.patch.max_seats; }
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
            started = client_->BeginUpdateRoom(act.room_id, req, now_ms);
            if (!started) {
                op_error_ = client_->LastErrorMessage().empty()
                    ? "Could not update lobby settings."
                    : client_->LastErrorMessage();
            }
            break;
        }
        case ActionKind::SendChat:
            started = client_->BeginSendChat(act.room_id, act.text, now_ms);
            if (!started) {
                op_error_ = client_->LastErrorMessage().empty()
                    ? "Could not send lobby chat message."
                    : client_->LastErrorMessage();
            }
            break;
        case ActionKind::StartMatch:
            started = client_->BeginStartMatch(act.room_id, now_ms);
            if (!started) {
                op_error_ = client_->LastErrorMessage().empty()
                    ? "Could not start online match."
                    : client_->LastErrorMessage();
            }
            break;
        }
        if (started) {
            active_action_ = act;
            has_active_action_ = true;
            op_room_generation_ = act.generation;
        }
        RefreshLeavePending();
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
        auto abort_launch = [&](const char *err_msg) {
            crypto_wipe(&mt, sizeof(mt));
            op_error_ = err_msg;
            ++room_generation_;
            const std::string room_id = !active_room_id_.empty() ? active_room_id_ : r.room_id;
            if (!room_id.empty()) {
                PendingAction leave_act;
                leave_act.kind = ActionKind::LeaveRoom;
                leave_act.generation = room_generation_;
                leave_act.room_id = room_id;
                pending_queue_.push_front(std::move(leave_act));
            }
            room_active_ = false;
            active_room_id_.clear();
            cached_room_ = CeSvcRoom{};
            RefreshLeavePending();
        };

        mt.proto = CE_ONLINE_PROTO_V1;
        CopyBounded(mt.match_id, sizeof(mt.match_id), r.start_ticket.match_id);
        CopyBounded(mt.room_id, sizeof(mt.room_id), r.room_id);
        if (origin_.size() >= sizeof(mt.lobby_origin)) {
            abort_launch("Lobby origin URL exceeds match launch ticket limit.");
            return;
        }
        CopyBounded(mt.lobby_origin, sizeof(mt.lobby_origin), origin_);
        if (client_->Session().session_token.size() >= sizeof(mt.session_token)) {
            abort_launch("Session token exceeds match launch ticket limit.");
            return;
        }
        CopyBounded(mt.session_token, sizeof(mt.session_token), client_->Session().session_token);
        mt.seed = r.start_ticket.seed;
        mt.level = r.start_ticket.level;
        CopyBounded(mt.mode, sizeof(mt.mode), r.start_ticket.mode);
        CopyBounded(mt.topology, sizeof(mt.topology), r.start_ticket.topology);
        mt.require_relay = r.start_ticket.require_relay ? 1u : 0u;
        if (!DecodeHexBytes(r.start_ticket.roster_hash, mt.roster_hash, sizeof(mt.roster_hash)) ||
            !DecodeHexBytes(r.start_ticket.match_secret, mt.match_secret, sizeof(mt.match_secret))) {
            abort_launch("Received a malformed match security ticket from the lobby.");
            return;
        }

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
            abort_launch("Received an invalid match start ticket from the lobby.");
            return;
        }
        if (CE_LobbyQueueOnlineLaunch && CE_LobbyQueueOnlineLaunch(&mt)) {
            launch_queued_ = true;
            crypto_wipe(&mt, sizeof(mt));
        } else {
            abort_launch("Could not launch online match. Start Onslaught CE via its launcher.");
        }
    }

    void HandleOpComplete(CeOnlineOpKind op, CeOnlineOpStatus st, uint64_t now_ms) {
        PendingAction finished_action = active_action_;
        const bool had_action = has_active_action_;
        const uint64_t finished_gen = op_room_generation_;
        has_active_action_ = false;
        op_room_generation_ = 0;

        if (cancelled_inflight_entry_ &&
            (op == CeOnlineOpKind::CreateLobby || op == CeOnlineOpKind::JoinRoom)) {
            cancelled_inflight_entry_ = false;
            if (st == CeOnlineOpStatus::Completed && !client_->CurrentRoom().room_id.empty()) {
                PendingAction leave_act;
                leave_act.kind = ActionKind::LeaveRoom;
                leave_act.generation = room_generation_;
                leave_act.room_id = client_->CurrentRoom().room_id;
                pending_queue_.push_front(std::move(leave_act));
            }
            RefreshLeavePending();
            return;
        }

        if (op == CeOnlineOpKind::LeaveRoom) {
            if (st == CeOnlineOpStatus::Failed && client_->LastHttpCode() == 401u &&
                had_action && !finished_action.retried_401) {
                client_->ClearSession();
                want_connect_ = true;
                conn_status_ = 1;
                status_detail_ = "Session expired, re-authenticating...";
                finished_action.retried_401 = true;
                pending_queue_.push_front(std::move(finished_action));
            }
            RefreshLeavePending();
            return;
        }

        const bool is_room_op = (op == CeOnlineOpKind::CreateLobby || op == CeOnlineOpKind::JoinRoom ||
                                 op == CeOnlineOpKind::UpdateRoom || op == CeOnlineOpKind::SendChat ||
                                 op == CeOnlineOpKind::StartMatch || op == CeOnlineOpKind::PollRoom);
        if (is_room_op && (leave_pending_ || finished_gen != room_generation_)) {
            if (st == CeOnlineOpStatus::Completed &&
                (op == CeOnlineOpKind::CreateLobby || op == CeOnlineOpKind::JoinRoom) &&
                !client_->CurrentRoom().room_id.empty()) {
                PendingAction leave_act;
                leave_act.kind = ActionKind::LeaveRoom;
                leave_act.generation = room_generation_;
                leave_act.room_id = client_->CurrentRoom().room_id;
                pending_queue_.push_front(std::move(leave_act));
            }
            RefreshLeavePending();
            return;
        }

        if (st == CeOnlineOpStatus::Failed) {
            if (op != CeOnlineOpKind::Health && op != CeOnlineOpKind::Authenticate &&
                client_->LastHttpCode() == 401u) {
                client_->ClearSession();
                want_connect_ = true;
                conn_status_ = 1;
                status_detail_ = "Session expired, re-authenticating...";
                if (had_action && !finished_action.retried_401) {
                    finished_action.retried_401 = true;
                    pending_queue_.push_front(std::move(finished_action));
                    return;
                }
                if (op == CeOnlineOpKind::PollRoom) {
                    return;
                }
            }
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
                for (const auto &queued : pending_queue_) {
                    if (queued.kind == ActionKind::ResolveCode) {
                        last_resolve_ = CeSvcResolveResult{};
                        last_resolve_.resolved = true;
                        last_resolve_.found = false;
                        last_resolve_.error = msg;
                        resolve_ready_ = true;
                    } else if (queued.kind == ActionKind::CreateRoom ||
                               queued.kind == ActionKind::JoinRoom ||
                               queued.kind == ActionKind::UpdateSelf ||
                               queued.kind == ActionKind::HostUpdate ||
                               queued.kind == ActionKind::SendChat ||
                               queued.kind == ActionKind::StartMatch) {
                        op_error_ = msg;
                    }
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
            } else if (op == CeOnlineOpKind::PollRoom) {
                const unsigned code = client_->LastHttpCode();
                if (code == 403) {
                    ResetRoomLifecycle(false);
                    op_error_ = "You were removed from the lobby.";
                } else if (code == 404) {
                    ResetRoomLifecycle(false);
                    op_error_ = "The lobby room was closed.";
                }
            } else if (op == CeOnlineOpKind::CreateLobby || op == CeOnlineOpKind::JoinRoom ||
                       op == CeOnlineOpKind::UpdateRoom || op == CeOnlineOpKind::SendChat ||
                       op == CeOnlineOpKind::StartMatch) {
                op_error_ = msg;
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
    bool leave_pending_ = false;
    bool resolve_ready_ = false;
    bool launch_queued_ = false;
    bool cancelled_inflight_entry_ = false;
    bool has_active_action_ = false;
    int conn_status_ = 0;
    int ping_ms_ = -1;
    uint64_t room_generation_ = 1;
    uint64_t op_room_generation_ = 0;
    uint64_t last_room_rev_ = 0;
    uint64_t last_room_poll_ms_ = 0;
    uint64_t last_now_ms_ = 0;
    std::string origin_;
    std::string content_id_;
    std::string status_detail_;
    std::string lobbies_err_;
    std::string op_error_;
    std::string active_room_id_;
    CeOnlineProfile profile_;
    std::unique_ptr<CeOnlineLobbyClient> client_;
    std::vector<CeSvcRoomSummary> cached_lobbies_;
    CeSvcRoom cached_room_;
    CeSvcResolveResult last_resolve_;
    PendingAction active_action_;
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
        host_seat_ = ticket_.slots[0].seat;
        guest_require_relay_ = (ticket_.require_relay != 0);
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
        if (now_ms > last_poll_ms_) last_poll_ms_ = now_ms;
        if (!transport_) return;
        if (!direct_mode_) {
            if (finish_requested_) {
                PumpFinishMatch(now_ms);
            } else {
                PumpSignalingAndIce(now_ms);
            }
        }
        transport_->Poll(now_ms, 64);
        DrainTransportEvents();
    }

    bool IsConnected() const override {
        if (!transport_) return false;
        if (is_host_) {
            size_t active_expected = 0;
            for (const auto &g : expected_guests_) {
                if (g.disconnected) continue;
                ++active_expected;
                CeStarPeerInfo pinfo{};
                if (!transport_->GetPeerInfo(g.sim_slot, pinfo) || !pinfo.authenticated)
                    return false;
            }
            return active_expected > 0;
        }
        return transport_->IsAuthenticated() && !transport_->IsHostLost();
    }

    void RequestFinishMatch() {
        if (finish_requested_) return;
        finish_requested_ = true;
        if (is_host_ && !direct_mode_ && lobby_ && !finish_sent_) {
            if (lobby_->Busy() && lobby_->ActiveOp() != CeOnlineOpKind::FinishMatch) {
                lobby_->Cancel();
            }
            PumpFinishMatch(last_poll_ms_ ? last_poll_ms_ : 1u);
        }
    }

    void PollFinished(uint64_t now_ms) {
        if (now_ms > last_poll_ms_) last_poll_ms_ = now_ms;
        if (!direct_mode_ && finish_requested_) {
            PumpFinishMatch(now_ms);
        }
        if (transport_) {
            transport_->Poll(now_ms, 64);
            DrainTransportEvents();
        }
    }

    void FlushFinishMatch() {
        if (!is_host_ || direct_mode_ || !lobby_ || !finish_requested_ || finish_sent_) return;
        const auto wall_start = std::chrono::steady_clock::now();
        uint64_t base_ms = last_poll_ms_;
        if (base_ms == 0u) {
            base_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    wall_start.time_since_epoch()).count());
            if (base_ms == 0u) base_ms = 1u;
        }
        for (int step = 0; step < 35 && !finish_sent_; ++step) {
            const uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - wall_start).count());
            const uint64_t tick_ms = base_ms + std::max<uint64_t>(elapsed, static_cast<uint64_t>(step) * 10u);
            PumpFinishMatch(tick_ms);
            if (finish_sent_ || (!lobby_->Busy() && finish_started_)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
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

    bool BridgeRelaysGuestChat() const override { return true; }

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
        bool disconnected = false;
        bool pending_answer_tried = false;
        uint32_t consecutive_failures = 0;
        uint64_t retry_after_ms = 0;
        uint64_t offer_seq = 0;
        uint64_t answer_seq = 0;
        uint64_t bridge_attached_ms = 0;
        std::string pending_answer_sdp;
        uint64_t pending_answer_seq = 0;
    };

    void PumpFinishMatch(uint64_t now_ms) {
        if (!is_host_ || direct_mode_ || !lobby_ || finish_sent_) return;
        if (lobby_->Busy()) {
            if (lobby_->ActiveOp() != CeOnlineOpKind::FinishMatch) {
                lobby_->Cancel();
                active_op_guest_index_ = -1;
            } else {
                const CeOnlineOpStatus st = lobby_->Poll(now_ms);
                if (st != CeOnlineOpStatus::Pending) {
                    finish_sent_ = true;
                }
                return;
            }
        }
        if (!finish_started_) {
            finish_started_ = true;
            if (!lobby_->BeginFinishMatch(ticket_.room_id, now_ms)) {
                finish_sent_ = true;
            }
        }
    }

#if defined(CE_NETWORK_ICE)
    void HandleHostGuestFatalIce(GuestLink &g) {
        if (g.disconnected) return;
        transport_->DetachGuestIceBridge(g.sim_slot);
        g.disconnected = true;
        g.bridge_attached = false;
        g.offer_sent = false;
        g.answer_applied = false;
        g.pending_answer_sdp.clear();
        g.pending_answer_seq = 0;
        g.pending_answer_tried = false;

        CeOnlineStarEvent disc = {};
        disc.kind = CE_STAR_EVT_PEER_DISCONNECT;
        disc.from_sim_slot = g.sim_slot;
        events_.push_back(disc);

        size_t remaining = 0;
        for (const auto &other : expected_guests_) {
            if (!other.disconnected) ++remaining;
        }
        if (remaining == 0) {
            failed_ = true;
            transport_->FailConnection(CeStarError::Io);
        }
    }
#endif

    void RecordGuestSignalFailure(size_t guest_idx, uint64_t now_ms, const char *stage = "signal") {
        if (guest_idx >= expected_guests_.size()) {
            RecordSignalFailure(now_ms, stage);
            return;
        }
        GuestLink &g = expected_guests_[guest_idx];
        if (g.disconnected) return;
        ++g.consecutive_failures;
        const unsigned http_code = lobby_ ? lobby_->LastHttpCode() : 0u;
        const char *err_code = (lobby_ && !lobby_->LastErrorCode().empty()) ? lobby_->LastErrorCode().c_str() : "none";
        fprintf(stderr, "onslaught-ce: online match guest slot %u %s failure #%u (http=%u code=%s)\n",
                (unsigned)g.sim_slot, stage ? stage : "signal", (unsigned)g.consecutive_failures, http_code, err_code);
        const uint32_t shift = (g.consecutive_failures > 1u)
            ? std::min<uint32_t>(g.consecutive_failures - 1u, 2u) : 0u;
        g.retry_after_ms = now_ms + (1000ULL << shift);
        if (g.consecutive_failures >= 4u) {
#if defined(CE_NETWORK_ICE)
            HandleHostGuestFatalIce(g);
#endif
        }
    }

    void RecordSignalFailure(uint64_t now_ms, const char *stage = "signal") {
        ++consecutive_failures_;
        const unsigned http_code = lobby_ ? lobby_->LastHttpCode() : 0u;
        const char *err_code = (lobby_ && !lobby_->LastErrorCode().empty()) ? lobby_->LastErrorCode().c_str() : "none";
        fprintf(stderr, "onslaught-ce: online match %s failure #%u (http=%u code=%s)\n",
                stage ? stage : "signal", (unsigned)consecutive_failures_, http_code, err_code);
        const uint32_t shift = (consecutive_failures_ > 1u)
            ? std::min<uint32_t>(consecutive_failures_ - 1u, 2u) : 0u;
        retry_after_ms_ = now_ms + (1000ULL << shift);
        if (consecutive_failures_ >= 4u && !failed_) {
            failed_ = true;
            if (transport_) transport_->FailConnection(CeStarError::Io);
        }
    }

    void PumpSignalingAndIce(uint64_t now_ms) {
#if defined(CE_NETWORK_ICE)
        if (!lobby_ || !transport_ || failed_) return;
        if (lobby_->Busy()) {
            CeOnlineOpStatus st = lobby_->Poll(now_ms);
            if (st == CeOnlineOpStatus::Pending) return;
            HandleSignalOpDone(lobby_->ActiveOp(), st, now_ms);
            if (lobby_->Busy() || failed_) return;
        }
        if (!is_host_ && retry_after_ms_ && now_ms < retry_after_ms_) return;

        if (is_host_) {
            // 1. Attach per-guest ICE bridge (STUN-only first when !require_relay; TURN on Relay Only or STUN fallback).
            bool any_unauthenticated = false;
            for (size_t i = 0; i < expected_guests_.size(); ++i) {
                GuestLink &g = expected_guests_[i];
                if (g.disconnected) continue;
                CeStarPeerInfo pinfo{};
                const bool authed = transport_->GetPeerInfo(g.sim_slot, pinfo) && pinfo.authenticated;
                if (!authed) any_unauthenticated = true;

                if (g.bridge_attached && !authed && transport_->IsGuestIceFailed(g.sim_slot)) {
                    if (!g.retried_relay && !g.require_relay) {
                        // Per-guest automatic TURN fallback when STUN P2P fails.
                        transport_->DetachGuestIceBridge(g.sim_slot);
                        g.require_relay = true;
                        g.retried_relay = true;
                        g.turn_fetched = false;
                        g.bridge_attached = false;
                        g.offer_sent = false;
                        g.answer_applied = false;
                        g.pending_answer_sdp.clear();
                        g.pending_answer_seq = 0;
                        g.pending_answer_tried = false;
                    } else {
                        HandleHostGuestFatalIce(g);
                        if (failed_) return;
                        continue;
                    }
                }
                if (g.retry_after_ms && now_ms < g.retry_after_ms) continue;

                if (!g.bridge_attached) {
                    if (!g.require_relay) {
                        ParsedJuiceConfig stun_cfg;
                        stun_cfg.BuildStunOnly();
                        g.bridge_attached = transport_->AttachGuestIceBridge(g.sim_slot, stun_cfg.cfg, false);
                        if (!g.bridge_attached) {
                            g.require_relay = true;
                            g.retried_relay = true;
                            RecordGuestSignalFailure(i, now_ms, "attach_stun_bridge");
                            if (failed_) return;
                            continue;
                        }
                        g.bridge_attached_ms = now_ms;
                        g.pending_answer_tried = false;
                    } else if (!g.turn_fetched) {
                        active_guest_index_ = i;
                        active_op_guest_index_ = static_cast<int>(i);
                        if (!lobby_->BeginFetchTurn(ticket_.room_id, ticket_.match_id, g.seat, now_ms)) {
                            active_op_guest_index_ = -1;
                            RecordGuestSignalFailure(i, now_ms, "begin_fetch_turn");
                            if (failed_) return;
                            continue;
                        }
                        return;
                    }
                }
                if (g.bridge_attached && !g.offer_sent) {
                    std::string sdp;
                    bool have_sdp = transport_->GetGuestLocalIceDescription(g.sim_slot, sdp);
                    // When a secondary UDP TURN port (e.g. 443) is blackholed while primary (3478)
                    // already allocated a relay candidate, publish early instead of waiting 23.5s.
                    if (!have_sdp && g.require_relay && now_ms >= g.bridge_attached_ms + 300u) {
                        have_sdp = transport_->GetGuestEarlyRelayIceDescription(g.sim_slot, sdp);
                    }
                    if (have_sdp) {
                        active_guest_index_ = i;
                        active_op_guest_index_ = static_cast<int>(i);
                        g.offer_seq = std::max(g.offer_seq, std::max(signal_seq_, g.answer_seq));
                        if (!lobby_->BeginSendSignal(ticket_.room_id, ticket_.match_id,
                                                     ticket_.local_seat, g.seat, "offer", sdp, now_ms)) {
                            active_op_guest_index_ = -1;
                            RecordGuestSignalFailure(i, now_ms, "begin_send_offer");
                            if (failed_) return;
                            continue;
                        }
                        return;
                    } else if (transport_->IsGuestIceGatheringDone(g.sim_slot)) {
                        if (!g.retried_relay && !g.require_relay) {
                            transport_->DetachGuestIceBridge(g.sim_slot);
                            g.require_relay = true;
                            g.retried_relay = true;
                            g.turn_fetched = false;
                            g.bridge_attached = false;
                            g.offer_sent = false;
                            g.answer_applied = false;
                            g.pending_answer_sdp.clear();
                            g.pending_answer_seq = 0;
                            g.pending_answer_tried = false;
                        } else {
                            fprintf(stderr, "onslaught-ce: online match host: ICE gathering finished with no usable relay candidate for slot %u\n",
                                    (unsigned)g.sim_slot);
                            HandleHostGuestFatalIce(g);
                            if (failed_) return;
                            continue;
                        }
                    }
                }
                if (g.bridge_attached && g.offer_sent && !g.answer_applied &&
                    !g.pending_answer_sdp.empty() && !g.pending_answer_tried) {
                    if (g.pending_answer_seq > g.offer_seq && g.pending_answer_seq > g.answer_seq) {
                        if (transport_->SetGuestRemoteIceDescription(g.sim_slot, g.pending_answer_sdp)) {
                            g.answer_applied = true;
                            g.answer_seq = g.pending_answer_seq;
                            g.pending_answer_sdp.clear();
                            g.pending_answer_seq = 0;
                            g.pending_answer_tried = false;
                        } else {
                            g.pending_answer_tried = true;
                        }
                    } else {
                        g.pending_answer_sdp.clear();
                        g.pending_answer_seq = 0;
                        g.pending_answer_tried = false;
                    }
                }
            }
            // 2. Poll guest answers while any guest is not authenticated yet (catches relay re-answers too).
            if ((!retry_after_ms_ || now_ms >= retry_after_ms_) &&
                any_unauthenticated && now_ms >= last_sig_poll_ms_ + 500u) {
                last_sig_poll_ms_ = now_ms;
                active_op_guest_index_ = -1;
                if (!lobby_->BeginPollSignal(ticket_.room_id, ticket_.match_id, signal_seq_, now_ms)) {
                    RecordSignalFailure(now_ms, "begin_poll_signal");
                }
            }
        } else {
            // Guest: STUN-first when !require_relay (zero TURN burn); Relay Only or STUN fallback fetches TURN.
            if (guest_bridge_attached_ && !IsConnected() && transport_->IsIceFailed()) {
                if (!guest_retried_relay_ && !guest_require_relay_) {
                    transport_->DetachHostIceBridge();
                    guest_require_relay_ = true;
                    guest_retried_relay_ = true;
                    guest_turn_fetched_ = false;
                    guest_bridge_attached_ = false;
                    guest_offer_applied_ = false;
                    guest_answer_sent_ = false;
                    pending_offer_tried_ = false;
                } else {
                    failed_ = true;
                    transport_->FailConnection(CeStarError::Io);
                    return;
                }
            }
            if (!guest_bridge_attached_) {
                if (!guest_require_relay_) {
                    ParsedJuiceConfig stun_cfg;
                    stun_cfg.BuildStunOnly();
                    guest_bridge_attached_ = transport_->AttachHostIceBridge(stun_cfg.cfg, false);
                    if (!guest_bridge_attached_) {
                        guest_require_relay_ = true;
                        guest_retried_relay_ = true;
                        RecordSignalFailure(now_ms, "attach_stun_bridge");
                        return;
                    }
                    guest_bridge_attached_ms_ = now_ms;
                    pending_offer_tried_ = false;
                } else if (!guest_turn_fetched_) {
                    active_op_guest_index_ = -1;
                    if (!lobby_->BeginFetchTurn(ticket_.room_id, ticket_.match_id, ticket_.local_seat, now_ms)) {
                        RecordSignalFailure(now_ms, "begin_fetch_turn");
                    }
                    return;
                }
            }
            if (guest_bridge_attached_ && !guest_offer_applied_ &&
                !pending_remote_offer_.empty() && !pending_offer_tried_) {
                if (transport_->SetRemoteIceDescription(pending_remote_offer_)) {
                    guest_offer_applied_ = true;
                    pending_remote_offer_.clear();
                    pending_offer_tried_ = false;
                } else {
                    pending_offer_tried_ = true;
                }
            }
            if (guest_bridge_attached_ && guest_offer_applied_ && !guest_answer_sent_) {
                std::string sdp;
                bool have_sdp = transport_->GetLocalIceDescription(sdp);
                if (!have_sdp && guest_require_relay_ && now_ms >= guest_bridge_attached_ms_ + 300u) {
                    have_sdp = transport_->GetEarlyRelayIceDescription(sdp);
                }
                if (have_sdp) {
                    active_op_guest_index_ = -1;
                    if (!lobby_->BeginSendSignal(ticket_.room_id, ticket_.match_id,
                                                 ticket_.local_seat, host_seat_, "answer", sdp, now_ms)) {
                        RecordSignalFailure(now_ms, "begin_send_answer");
                    }
                    return;
                } else if (transport_->IsIceGatheringDone()) {
                    if (!guest_retried_relay_ && !guest_require_relay_) {
                        transport_->DetachHostIceBridge();
                        guest_require_relay_ = true;
                        guest_retried_relay_ = true;
                        guest_turn_fetched_ = false;
                        guest_bridge_attached_ = false;
                        guest_offer_applied_ = false;
                        guest_answer_sent_ = false;
                        pending_offer_tried_ = false;
                    } else {
                        fprintf(stderr, "onslaught-ce: online match guest: ICE gathering finished with no usable relay candidate\n");
                        failed_ = true;
                        transport_->FailConnection(CeStarError::Io);
                        return;
                    }
                }
            }
            // Keep polling while not yet authenticated so Host's TURN relay re-offer is always received.
            if (!IsConnected() && now_ms >= last_sig_poll_ms_ + 500u) {
                last_sig_poll_ms_ = now_ms;
                active_op_guest_index_ = -1;
                if (!lobby_->BeginPollSignal(ticket_.room_id, ticket_.match_id, signal_seq_, now_ms)) {
                    RecordSignalFailure(now_ms, "begin_poll_signal");
                }
                return;
            }
        }
#else
        (void)now_ms;
#endif
    }

    void HandleSignalOpDone(CeOnlineOpKind op, CeOnlineOpStatus st, uint64_t now_ms) {
#if defined(CE_NETWORK_ICE)
        const int op_guest = active_op_guest_index_;
        active_op_guest_index_ = -1;
        if (st != CeOnlineOpStatus::Completed) {
            if (is_host_ && op_guest >= 0) {
                RecordGuestSignalFailure(static_cast<size_t>(op_guest), now_ms, "op_failed");
            } else {
                RecordSignalFailure(now_ms, "op_failed");
            }
            return;
        }
        if (is_host_ && op_guest >= 0 && static_cast<size_t>(op_guest) < expected_guests_.size()) {
            expected_guests_[static_cast<size_t>(op_guest)].consecutive_failures = 0;
            expected_guests_[static_cast<size_t>(op_guest)].retry_after_ms = 0;
        } else {
            consecutive_failures_ = 0;
            retry_after_ms_ = 0;
        }
        if (op == CeOnlineOpKind::FetchTurn) {
            if (is_host_ && active_guest_index_ < expected_guests_.size()) {
                GuestLink &g = expected_guests_[active_guest_index_];
                ParsedJuiceConfig parsed;
                const bool built = parsed.Build(lobby_->LastTurnBundle(), g.require_relay);
                if (!built ||
                    !transport_->AttachGuestIceBridge(g.sim_slot, parsed.cfg, g.require_relay)) {
                    RecordGuestSignalFailure(active_guest_index_, now_ms, "attach_turn_bridge");
                    return;
                }
                g.turn_fetched = true;
                g.bridge_attached = true;
                g.bridge_attached_ms = now_ms;
                g.pending_answer_tried = false;
            } else if (!is_host_) {
                guest_require_relay_ = (ticket_.require_relay != 0) || guest_retried_relay_;
                ParsedJuiceConfig parsed;
                const bool built = parsed.Build(lobby_->LastTurnBundle(), guest_require_relay_);
                if (!built ||
                    !transport_->AttachHostIceBridge(parsed.cfg, guest_require_relay_)) {
                    RecordSignalFailure(now_ms, "attach_turn_bridge");
                    return;
                }
                guest_turn_fetched_ = true;
                guest_bridge_attached_ = true;
                guest_bridge_attached_ms_ = now_ms;
                pending_offer_tried_ = false;
            }
        } else if (op == CeOnlineOpKind::SendSignal) {
            if (is_host_ && active_guest_index_ < expected_guests_.size()) {
                GuestLink &g = expected_guests_[active_guest_index_];
                g.offer_sent = true;
                g.offer_seq = std::max(g.offer_seq, std::max(signal_seq_, g.answer_seq));
            } else if (!is_host_) {
                guest_answer_sent_ = true;
            }
        } else if (op == CeOnlineOpKind::PollSignal) {
            const auto &res = lobby_->LastSignalPoll();
            if (res.seq > signal_seq_) signal_seq_ = res.seq;
            for (const auto &msg : res.messages) {
                if (is_host_ && msg.kind == "answer") {
                    for (auto &g : expected_guests_) {
                        if (g.disconnected || g.seat != msg.from_seat) continue;
                        if (msg.seq <= g.answer_seq || msg.seq <= g.offer_seq) continue;
                        if (g.bridge_attached && g.offer_sent && !g.answer_applied) {
                            if (transport_->SetGuestRemoteIceDescription(g.sim_slot, msg.sdp)) {
                                g.answer_applied = true;
                                g.answer_seq = msg.seq;
                                g.pending_answer_sdp.clear();
                                g.pending_answer_seq = 0;
                                g.pending_answer_tried = false;
                            } else if (msg.seq > g.pending_answer_seq) {
                                // Do not advance answer_seq when SetGuestRemoteIceDescription fails.
                                g.pending_answer_sdp = msg.sdp;
                                g.pending_answer_seq = msg.seq;
                                g.pending_answer_tried = true;
                            }
                        } else if (!g.answer_applied && msg.seq > g.pending_answer_seq) {
                            g.pending_answer_sdp = msg.sdp;
                            g.pending_answer_seq = msg.seq;
                            g.pending_answer_tried = false;
                        }
                    }
                } else if (!is_host_ && msg.kind == "offer" && msg.seq > guest_offer_seq_) {
                    const bool had_offer = (guest_offer_seq_ != 0);
                    guest_offer_seq_ = msg.seq;
                    pending_offer_tried_ = false;
                    if (had_offer && !IsConnected()) {
                        if (guest_require_relay_ && guest_bridge_attached_ && !guest_offer_applied_) {
                            // Reuse fresh unapplied TURN bridge instead of tearing it down twice.
                            guest_offer_applied_ = transport_->SetRemoteIceDescription(msg.sdp);
                            if (guest_offer_applied_) {
                                pending_remote_offer_.clear();
                                pending_offer_tried_ = false;
                            } else {
                                pending_remote_offer_ = msg.sdp;
                                pending_offer_tried_ = true;
                            }
                        } else if (guest_require_relay_ && !guest_bridge_attached_ && !guest_offer_applied_) {
                            pending_remote_offer_ = msg.sdp;
                        } else {
                            // Host initiated a fresh relay offer: detach STUN/applied bridge and switch to TURN Relay.
                            transport_->DetachHostIceBridge();
                            guest_require_relay_ = true;
                            guest_retried_relay_ = true;
                            guest_turn_fetched_ = false;
                            guest_bridge_attached_ = false;
                            guest_offer_applied_ = false;
                            guest_answer_sent_ = false;
                            pending_remote_offer_ = msg.sdp;
                        }
                    } else if (guest_bridge_attached_ && !guest_offer_applied_) {
                        guest_offer_applied_ = transport_->SetRemoteIceDescription(msg.sdp);
                        if (guest_offer_applied_) {
                            pending_remote_offer_.clear();
                            pending_offer_tried_ = false;
                        } else {
                            pending_remote_offer_ = msg.sdp;
                            pending_offer_tried_ = true;
                        }
                    } else {
                        pending_remote_offer_ = msg.sdp;
                    }
                }
            }
        }
#else
        (void)op; (void)st; (void)now_ms;
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
            case CeStarEventKind::PeerAuthenticated: {
                CeStarPeerInfo pi{};
                const char *rname = "unknown";
                if (transport_->GetPeerInfo(ev.sim_slot, pi)) {
                    rname = (pi.route == CeStarRoute::Relay) ? "relay" :
                            (pi.route == CeStarRoute::Direct ? "direct" : "unknown");
                }
                fprintf(stderr, "onslaught-ce: online match guest slot %u authenticated (route=%s)\n",
                        (unsigned)ev.sim_slot, rname);
                break;
            }
            case CeStarEventKind::HostAuthenticated: {
                CeStarPeerInfo pi{};
                const char *rname = "unknown";
                if (transport_->GetPeerInfo(0, pi)) {
                    rname = (pi.route == CeStarRoute::Relay) ? "relay" :
                            (pi.route == CeStarRoute::Direct ? "direct" : "unknown");
                }
                fprintf(stderr, "onslaught-ce: online match host authenticated (route=%s)\n", rname);
                break;
            }
            case CeStarEventKind::PeerDisconnected:
                if (is_host_) {
                    for (auto &g : expected_guests_) {
                        if (g.sim_slot == static_cast<uint8_t>(ev.sim_slot)) {
                            g.disconnected = true;
#if defined(CE_NETWORK_ICE)
                            if (g.bridge_attached && transport_) {
                                transport_->DetachGuestIceBridge(g.sim_slot);
                                g.bridge_attached = false;
                            }
#endif
                        }
                    }
                }
                out.kind = CE_STAR_EVT_PEER_DISCONNECT;
                out.from_sim_slot = static_cast<uint8_t>(ev.sim_slot);
                events_.push_back(out);
                break;
            case CeStarEventKind::HostDisconnected:
                out.kind = CE_STAR_EVT_HOST_LOST;
                out.from_sim_slot = 0;
                events_.push_back(out);
                break;
            case CeStarEventKind::Error:
                out.kind = CE_STAR_EVT_TRANSPORT_ERROR;
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
    bool failed_ = false;
    bool finish_requested_ = false;
    bool finish_started_ = false;
    bool finish_sent_ = false;
    uint16_t host_seat_ = 0;
    std::vector<GuestLink> expected_guests_;
    size_t active_guest_index_ = 0;
    int active_op_guest_index_ = -1;
    bool guest_turn_fetched_ = false;
    bool guest_bridge_attached_ = false;
    bool guest_offer_applied_ = false;
    bool guest_answer_sent_ = false;
    bool guest_require_relay_ = false;
    bool guest_retried_relay_ = false;
    bool pending_offer_tried_ = false;
    uint64_t guest_offer_seq_ = 0;
    uint64_t guest_bridge_attached_ms_ = 0;
    std::string pending_remote_offer_;
    uint64_t signal_seq_ = 0;
    uint64_t last_sig_poll_ms_ = 0;
    uint64_t last_poll_ms_ = 0;
    uint64_t retry_after_ms_ = 0;
    uint32_t consecutive_failures_ = 0;
    std::unique_ptr<CeOnlineLobbyClient> lobby_;
    std::unique_ptr<CeStarTransport> transport_;
    std::deque<CeOnlineStarEvent> events_;
};

CeLiveOnlineService *gOnlineService = nullptr;
std::unique_ptr<CeLiveOnlineStarBridge> gLiveMatchBridge;
bool gMatchResultRecorded = false;

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
    gMatchResultRecorded = false;
    gLiveMatchBridge = std::move(bridge);
    PortCeOnlineMatchSetBridge(gLiveMatchBridge.get());
    return 1;
}

extern "C" void PortCeOnlineServiceFinishMatch(int won) {
    if (!gMatchResultRecorded) {
        gMatchResultRecorded = true;
        const char *user_dir = PortUserDir();
        if (user_dir && user_dir[0]) {
            CeOnlineProfile prof;
            std::string err;
            if (prof.LoadOrCreate(user_dir, &err)) {
                (void)prof.RecordMatchResult(won > 0, true, &err);
            }
        }
    }
    if (gLiveMatchBridge) {
        gLiveMatchBridge->RequestFinishMatch();
    }
}

extern "C" void PortCeOnlineServicePollFinished(uint64_t now_ms) {
    if (gLiveMatchBridge) {
        gLiveMatchBridge->PollFinished(now_ms);
    }
}

extern "C" void PortCeOnlineServiceDetachMatchBridge(void) {
    if (gLiveMatchBridge) {
        gLiveMatchBridge->FlushFinishMatch();
    }
    PortCeOnlineMatchSetBridge(nullptr);
    gLiveMatchBridge.reset();
}

#endif // !CE_NETWORK_OFF
