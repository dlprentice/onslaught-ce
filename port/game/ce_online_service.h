// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_service.h: Plain-struct C++ facade bridging CeOnlineProfile,
// CeOnlineLobbyClient, and CeStarTransport to the front-end UI adapter and live 2-4P match boot.
// Deliberately avoids including ce_online_ui_model.h or online_lobby_client.h in the header so
// both layers stay decoupled with zero symbol/struct-name collisions.
#ifndef PORT_GAME_CE_ONLINE_SERVICE_H
#define PORT_GAME_CE_ONLINE_SERVICE_H

#include <stdint.h>
#include <string>
#include <vector>

#ifndef CE_ONLINE_LOBBY_DEFAULT_URL
#define CE_ONLINE_LOBBY_DEFAULT_URL ""
#endif

struct CeSvcProfile {
    std::string player_id;
    std::string callsign;
    std::string tag;
    uint32_t matches_played = 0;
    uint32_t matches_won = 0;
    std::string server_url;
    int conn_status = 0; // 0=offline, 1=connecting, 2=online, 3=error
    int ping_ms = -1;
    std::string status_detail;
};

struct CeSvcRoomSummary {
    std::string room_id;
    std::string code;
    std::string name;
    std::string host_callsign;
    std::string host_tag;
    std::string topology;
    std::string mode;
    std::string state;
    std::string build_id;
    int map = 850;
    uint16_t used_seats = 0;
    uint16_t human_seats = 0;
    uint16_t max_seats = 4;
    bool has_password = false;
    bool require_relay = false;
    bool compatible = true;
};

struct CeSvcSeat {
    uint16_t seat = 0;
    int kind = 0; // 0=open, 1=human, 2=ai, 3=closed
    std::string player_id;
    std::string callsign;
    std::string tag;
    int team = 0;
    std::string be_cfg;
    bool ready = false;
    int rtt_ms = -1;
    int route = 0; // 0=unknown, 1=direct, 2=relay
};

struct CeSvcChatMsg {
    uint32_t seq = 0;
    uint16_t seat = 0;
    std::string player_id;
    std::string callsign;
    std::string tag;
    std::string text;
    uint64_t ts = 0;
};

struct CeSvcStartTicket {
    bool valid = false;
    std::string match_id;
    uint32_t seed = 0;
    int level = 850;
    std::string mode;
    std::string topology;
    bool require_relay = false;
    std::string roster_hash;
};

struct CeSvcRoom {
    bool active = false;
    std::string room_id;
    std::string code;
    uint32_t rev = 0;
    std::string state;
    std::string name;
    std::string topology;
    std::string mode;
    int map = 850;
    uint16_t max_seats = 4;
    uint16_t host_seat = 0;
    uint16_t local_seat = 0xFFFFu;
    std::string visibility;
    bool has_password = false;
    bool require_relay = false;
    bool ai_fill = true;
    std::string build_id;
    std::vector<CeSvcSeat> seats;
    std::vector<CeSvcChatMsg> chat;
    CeSvcStartTicket start_ticket;
};

struct CeSvcCreateParams {
    std::string name;
    std::string mode;
    int map = 850;
    uint16_t max_seats = 4;
    std::string topology;
    std::string visibility;
    std::string password;
    bool require_relay = false;
    bool ai_fill = true;
    std::string be_cfg;
};

struct CeSvcHostPatch {
    bool has_map = false;
    int map = 850;
    bool has_mode = false;
    std::string mode;
    bool has_require_relay = false;
    bool require_relay = false;
    bool has_ai_fill = false;
    bool ai_fill = true;
    bool has_seat_kind = false;
    uint16_t target_seat = 0;
    int seat_kind = 0; // 0=open, 2=ai, 3=closed
    bool has_kick_seat = false;
    uint16_t kick_seat = 0;
};

struct CeSvcResolveResult {
    bool resolved = false;
    bool found = false;
    std::string room_id;
    bool has_password = false;
    std::string state;
    std::string error;
};

class CeOnlineServiceFacade {
public:
    virtual ~CeOnlineServiceFacade() = default;

    virtual void Poll(uint64_t now_ms) = 0;
    virtual bool GetProfile(CeSvcProfile *out) = 0;
    virtual bool SaveCallsign(const std::string &callsign, std::string *err_out) = 0;
    virtual void RequestReconnect() = 0;

    virtual void RequestRefreshLobbies(bool include_incompatible) = 0;
    virtual bool GetLobbies(std::vector<CeSvcRoomSummary> *out_rooms,
                            bool *loading_out, std::string *err_out) = 0;

    virtual bool RequestCreateRoom(const CeSvcCreateParams &params, std::string *err_out) = 0;
    virtual bool RequestResolveCode(const std::string &code, std::string *err_out) = 0;
    virtual bool TakeResolveResult(CeSvcResolveResult *out) = 0;
    virtual bool RequestJoinRoom(const std::string &room_id, const std::string &password,
                                 const std::string &be_cfg, std::string *err_out) = 0;
    virtual void RequestLeaveRoom() = 0;

    virtual bool GetRoom(CeSvcRoom *out_room) = 0;
    virtual bool RequestUpdateSelf(int team, const std::string &be_cfg, bool ready,
                                   std::string *err_out) = 0;
    virtual bool RequestHostUpdate(const CeSvcHostPatch &patch, std::string *err_out) = 0;
    virtual bool RequestSendChat(const std::string &text, std::string *err_out) = 0;
    virtual bool RequestStartMatch(std::string *err_out) = 0;
    virtual void NotifyMatchTicketReady() = 0;
    virtual bool TakeOpError(std::string *out) = 0;
    virtual void FlushLeave(uint64_t now_ms) = 0;
};

CeOnlineServiceFacade *PortCeOnlineServiceInstance();

extern "C" {
// Called from CE_BootStart() when an online match ticket was admitted from net-launch.
// Attaches a live CeStarTransport + multi-slot ICE/TURN bridge to PortCeOnlineMatchSetBridge().
int PortCeOnlineServiceAttachMatchBridge(const uint8_t compat_digest[32]);
void PortCeOnlineServiceDetachMatchBridge(void);
}

#endif // PORT_GAME_CE_ONLINE_SERVICE_H
