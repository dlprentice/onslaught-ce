// SPDX-License-Identifier: GPL-3.0-only
// Non-blocking JSON REST client for the Onslaught CE Online Lobby Service (/ce-lobby/v1/).
#ifndef CE_ONLINE_LOBBY_CLIENT_H
#define CE_ONLINE_LOBBY_CLIENT_H

#include "online_profile.h"
#include "signaling_https.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

constexpr uint16_t CE_ONLINE_PROTO_VERSION = 1;
#ifndef CE_ONLINE_MAX_MATCH_SLOTS
constexpr uint16_t CE_ONLINE_MAX_MATCH_SLOTS = 4;
#endif
#ifndef CE_ONLINE_WIRE_MAX_SLOTS
constexpr uint16_t CE_ONLINE_WIRE_MAX_SLOTS = 64;
#endif
constexpr size_t CE_ONLINE_MAX_CHAT_HISTORY = 32;
constexpr size_t CE_ONLINE_MAX_CHAT_BYTES = 160;
constexpr size_t CE_ONLINE_MAX_SDP_BYTES = 4096;

// Bounded, self-contained JSON DOM parser and encoder (zero third-party JSON dependencies).
namespace CeOnlineJson {

constexpr size_t kMaxInputBytes = 65536;
constexpr unsigned kMaxDepth = 12;
constexpr size_t kMaxNodes = 4096;
constexpr size_t kMaxArrayItems = 128;
constexpr size_t kMaxObjectFields = 64;
constexpr size_t kMaxKeyBytes = 64;
constexpr size_t kMaxStringBytes = 8192;

struct Value {
    enum class Kind { Null, Bool, UInt, Int, String, Array, Object };
    Kind kind = Kind::Null;
    bool bool_val = false;
    uint64_t uint_val = 0;
    int64_t int_val = 0;
    std::string str_val;
    std::vector<Value> arr_val;
    std::vector<std::pair<std::string, Value>> obj_val;

    Value() = default;
    Value(Value&&) noexcept = default;
    Value& operator=(Value&&) noexcept = default;
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    ~Value();

    const Value* Get(const char* key) const noexcept;
    bool IsNull() const noexcept { return kind == Kind::Null; }
    bool IsBool() const noexcept { return kind == Kind::Bool; }
    bool IsUInt() const noexcept { return kind == Kind::UInt; }
    bool IsInt() const noexcept { return kind == Kind::Int || kind == Kind::UInt; }
    bool IsString() const noexcept { return kind == Kind::String; }
    bool IsArray() const noexcept { return kind == Kind::Array; }
    bool IsObject() const noexcept { return kind == Kind::Object; }
};

bool Parse(const std::string& input, Value& out);
std::string EscapeString(const std::string& raw);

class ObjectBuilder {
public:
    ObjectBuilder();
    void AddString(const char* key, const std::string& value);
    void AddUInt(const char* key, uint64_t value);
    void AddBool(const char* key, bool value);
    void AddRawObject(const char* key, const std::string& raw_json_object);
    std::string Finish();

private:
    void Comma();
    std::string buf_;
    bool first_ = true;
};

} // namespace CeOnlineJson

struct CeOnlineHealth {
    bool ok = false;
    bool enabled = false;
    uint16_t proto = 0;
    uint16_t max_match_slots = 0;
    uint16_t wire_max_slots = 0;
    uint64_t server_time_ms = 0;
    uint32_t active_rooms = 0;
    uint32_t rtt_ms = 0;
};

struct CeOnlineSession {
    std::string session_token;
    std::string player_id;
    std::string tag;
    std::string callsign;
    uint64_t expires_at = 0;
    uint64_t server_time_ms = 0;

    ~CeOnlineSession();
    CeOnlineSession() = default;
    CeOnlineSession(const CeOnlineSession&) = default;
    CeOnlineSession& operator=(const CeOnlineSession&) = default;
    CeOnlineSession(CeOnlineSession&&) noexcept = default;
    CeOnlineSession& operator=(CeOnlineSession&&) noexcept = default;
    void Clear() noexcept;
};

struct CeOnlineRoomSummary {
    std::string room_id;
    std::string code;
    std::string name;
    std::string host_callsign;
    std::string host_tag;
    std::string topology;
    std::string mode;
    uint32_t map = 0;
    uint16_t used_seats = 0;
    uint16_t human_seats = 0;
    uint16_t max_seats = 0;
    bool has_password = false;
    bool require_relay = false;
    std::string state;
    uint16_t proto = 0;
    std::string build_id;
    std::string content_id;
    bool compatible = false;
    uint64_t updated_at = 0;
};

struct CeOnlineLobbyList {
    uint16_t proto = 0;
    uint64_t server_time_ms = 0;
    uint32_t next_cursor = 0;
    std::vector<CeOnlineRoomSummary> rooms;
};

struct CeOnlineResolveResult {
    std::string room_id;
    bool has_password = false;
    std::string state;
};

struct CeOnlineTeamInfo {
    uint16_t team = 0;
    std::string side;
    uint16_t cap = 0;
};

struct CeOnlineSeat {
    uint16_t seat = 0;
    std::string kind; // "human" | "ai" | "open" | "closed"
    std::string player_id;
    std::string tag;
    std::string callsign;
    uint16_t team = 0;
    std::string be_cfg;
    bool ready = false;
    uint32_t rtt_ms = 0;
    std::string route; // "unknown" | "direct" | "relay"
};

struct CeOnlineChatMsg {
    uint64_t seq = 0;
    uint16_t seat = 0;
    std::string player_id;
    std::string callsign;
    std::string tag;
    std::string text;
    uint64_t ts = 0;
};

struct CeOnlineStartSlot {
    uint16_t seat = 0;
    uint16_t sim_slot = 0;
    std::string kind;
    std::string player_id;
    std::string callsign;
    std::string tag;
    uint16_t team = 0;
    std::string be_cfg;
};

struct CeOnlineStartTicket {
    bool valid = false;
    std::string match_id;
    uint32_t seed = 0;
    uint32_t level = 0;
    std::string mode;
    std::string topology;
    bool require_relay = false;
    std::string roster_hash;
    std::string match_secret; // wiped on Clear/destruction
    uint64_t issued_at = 0;
    uint64_t expires_at = 0;
    std::vector<CeOnlineStartSlot> slots;

    ~CeOnlineStartTicket();
    CeOnlineStartTicket() = default;
    CeOnlineStartTicket(const CeOnlineStartTicket&) = default;
    CeOnlineStartTicket& operator=(const CeOnlineStartTicket&) = default;
    CeOnlineStartTicket(CeOnlineStartTicket&&) noexcept = default;
    CeOnlineStartTicket& operator=(CeOnlineStartTicket&&) noexcept = default;
    void Clear() noexcept;
};

struct CeOnlineRoomState {
    uint16_t proto = 0;
    std::string room_id;
    std::string code;
    uint64_t rev = 0;
    std::string state; // "lobby" | "starting" | "in_match" | "closed"
    std::string name;
    std::string topology;
    std::string mode;
    uint32_t map = 0;
    uint16_t max_seats = 0;
    uint16_t host_seat = 0;
    std::string visibility;
    bool has_password = false;
    bool require_relay = false;
    bool ai_fill = false;
    std::string build_id;
    std::string content_id;
    uint64_t server_time_ms = 0;
    std::vector<CeOnlineTeamInfo> teams;
    std::vector<CeOnlineSeat> seats;
    std::vector<CeOnlineChatMsg> chat;
    CeOnlineStartTicket start_ticket;

    void Clear() noexcept;
};

struct CeOnlineIceServer {
    std::vector<std::string> urls;
    std::string username;
    std::string credential;

    ~CeOnlineIceServer();
    CeOnlineIceServer() = default;
    CeOnlineIceServer(const CeOnlineIceServer&) = default;
    CeOnlineIceServer& operator=(const CeOnlineIceServer&) = default;
    CeOnlineIceServer(CeOnlineIceServer&&) noexcept = default;
    CeOnlineIceServer& operator=(CeOnlineIceServer&&) noexcept = default;
    void Clear() noexcept;
};

struct CeOnlineTurnBundle {
    std::vector<CeOnlineIceServer> ice_servers;
    uint32_t ttl = 0;
    void Clear() noexcept;
};

struct CeOnlineSignalMessage {
    uint64_t seq = 0;
    uint16_t from_seat = 0;
    uint16_t to_seat = 0;
    std::string kind; // "offer" | "answer"
    std::string sdp;
};

struct CeOnlineSignalPollResult {
    uint64_t seq = 0;
    std::vector<CeOnlineSignalMessage> messages;
};

struct CeOnlineListLobbiesReq {
    uint32_t cursor = 0;
    uint16_t limit = 16; // 1..32
    std::string build_id;
    std::string content_id;
    bool include_incompatible = false;
};

struct CeOnlineCreateLobbyReq {
    std::string name;
    std::string mode = "versus_2v2";
    uint32_t map = 850;
    uint16_t max_seats = 4;
    std::string topology = "star_lockstep";
    std::string visibility = "public";
    std::string password;
    bool require_relay = false;
    bool ai_fill = true;
    std::string build_id;
    std::string content_id;
    std::string be_cfg;
};

struct CeOnlineJoinRoomReq {
    std::string password;
    std::string build_id;
    std::string content_id;
    std::string be_cfg;
};

struct CeOnlineUpdateRoomReq {
    // Caller seat fields
    bool has_team = false;
    uint16_t team = 0;
    bool has_be_cfg = false;
    std::string be_cfg;
    bool has_ready = false;
    bool ready = false;
    bool has_rtt_ms = false;
    uint32_t rtt_ms = 0;
    bool has_route = false;
    std::string route; // "unknown" | "direct" | "relay"

    // Host-only room fields
    bool has_name = false;
    std::string name;
    bool has_map = false;
    uint32_t map = 0;
    bool has_mode = false;
    std::string mode;
    bool has_max_seats = false;
    uint16_t max_seats = 0;
    bool has_visibility = false;
    std::string visibility;
    bool has_require_relay = false;
    bool require_relay = false;
    bool has_ai_fill = false;
    bool ai_fill = false;
    bool has_set_seat_kind = false;
    uint16_t set_seat_kind_seat = 0;
    std::string set_seat_kind; // "ai" | "open" | "closed"
    bool has_kick_seat = false;
    uint16_t kick_seat = 0;
};

enum class CeOnlineOpKind {
    None,
    Health,
    Authenticate,
    ListLobbies,
    CreateLobby,
    ResolveCode,
    PollRoom,
    JoinRoom,
    LeaveRoom,
    UpdateRoom,
    SendChat,
    StartMatch,
    FinishMatch,
    FetchTurn,
    SendSignal,
    PollSignal
};

enum class CeOnlineOpStatus {
    Idle,
    Pending,
    Completed,
    Failed
};

class CeOnlineLobbyClient {
public:
    static constexpr uint64_t kDefaultTimeoutMs = 10000;

    explicit CeOnlineLobbyClient(std::unique_ptr<CeOnlineHttps> transport);
    ~CeOnlineLobbyClient();
    CeOnlineLobbyClient(const CeOnlineLobbyClient&) = delete;
    CeOnlineLobbyClient& operator=(const CeOnlineLobbyClient&) = delete;

    // Non-blocking request starters (one active operation at a time).
    bool BeginHealth(uint64_t now_ms, uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginAuthenticate(const CeOnlineProfile& profile, const std::string& build_id,
                           const std::string& content_id, uint64_t now_ms,
                           uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginListLobbies(const CeOnlineListLobbiesReq& req, uint64_t now_ms,
                          uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginCreateLobby(const CeOnlineCreateLobbyReq& req, uint64_t now_ms,
                          uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginResolveCode(const std::string& code, uint64_t now_ms,
                          uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginPollRoom(const std::string& room_id, uint64_t since_rev, uint32_t rtt_ms,
                       uint64_t now_ms, uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginJoinRoom(const std::string& room_id, const CeOnlineJoinRoomReq& req,
                       uint64_t now_ms, uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginLeaveRoom(const std::string& room_id, uint64_t now_ms,
                        uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginUpdateRoom(const std::string& room_id, const CeOnlineUpdateRoomReq& req,
                         uint64_t now_ms, uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginSendChat(const std::string& room_id, const std::string& text,
                       uint64_t now_ms, uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginStartMatch(const std::string& room_id, uint64_t now_ms,
                         uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginFinishMatch(const std::string& room_id, uint64_t now_ms,
                          uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginFetchTurn(const std::string& room_id, const std::string& match_id,
                        uint16_t guest_seat, uint64_t now_ms,
                        uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginSendSignal(const std::string& room_id, const std::string& match_id,
                         uint16_t from_seat, uint16_t to_seat, const std::string& kind,
                         const std::string& sdp, uint64_t now_ms,
                         uint64_t timeout_ms = kDefaultTimeoutMs);
    bool BeginPollSignal(const std::string& room_id, const std::string& match_id,
                         uint64_t since_seq, uint64_t now_ms,
                         uint64_t timeout_ms = kDefaultTimeoutMs);

    CeOnlineOpStatus Poll(uint64_t now_ms);
    void Cancel();

    bool Busy() const noexcept { return status_ == CeOnlineOpStatus::Pending; }
    bool Authenticated() const noexcept { return !session_.session_token.empty(); }
    void ClearSession() noexcept { session_.Clear(); }
    void SetSessionForTest(const CeOnlineSession& s) { session_ = s; }

    CeOnlineOpKind ActiveOp() const noexcept { return active_op_; }
    CeOnlineOpStatus Status() const noexcept { return status_; }
    unsigned LastHttpCode() const noexcept { return last_http_code_; }
    const std::string& LastErrorCode() const noexcept { return last_error_code_; }
    const std::string& LastErrorMessage() const noexcept { return last_error_message_; }
    uint32_t LastRttMs() const noexcept { return last_rtt_ms_; }
    bool LastPollUnchanged() const noexcept { return last_poll_unchanged_; }

    const CeOnlineHealth& LastHealth() const noexcept { return last_health_; }
    const CeOnlineSession& Session() const noexcept { return session_; }
    const CeOnlineLobbyList& LastLobbyList() const noexcept { return last_lobby_list_; }
    const CeOnlineResolveResult& LastResolve() const noexcept { return last_resolve_; }
    const CeOnlineRoomState& CurrentRoom() const noexcept { return current_room_; }
    const CeOnlineTurnBundle& LastTurnBundle() const noexcept { return last_turn_; }
    const CeOnlineSignalPollResult& LastSignalPoll() const noexcept { return last_signal_poll_; }

    static bool IsValidMapId(uint32_t map) noexcept;
    static bool IsValidMode(const std::string& mode) noexcept;
    static bool IsValidSafeId(const std::string& id) noexcept;

private:
    bool StartHttp(CeOnlineOpKind op, CeOnlineHttpMethod method, const std::string& path_and_query,
                   bool with_bearer, const std::string& body, uint64_t now_ms, uint64_t timeout_ms);
    bool HandleReply(const CeSignalHttpsReply& reply, uint64_t now_ms);
    bool FailOp(unsigned http_code, const std::string& code, const std::string& msg);
    void ParseErrorBody(const std::string& body, std::string& out_code, std::string& out_msg);

    std::unique_ptr<CeOnlineHttps> transport_;
    CeOnlineOpKind active_op_ = CeOnlineOpKind::None;
    CeOnlineOpStatus status_ = CeOnlineOpStatus::Idle;
    uint64_t req_started_ms_ = 0;
    uint64_t req_deadline_ms_ = 0;
    unsigned last_http_code_ = 0;
    std::string last_error_code_;
    std::string last_error_message_;
    uint32_t last_rtt_ms_ = 0;
    bool last_poll_unchanged_ = false;

    // Two-step challenge-response authentication state (non-owning pointer to caller's profile).
    const CeOnlineProfile* auth_profile_ = nullptr;
    std::string auth_build_id_;
    std::string auth_content_id_;
    bool auth_stage_session_ = false;

    CeOnlineHealth last_health_;
    CeOnlineSession session_;
    CeOnlineLobbyList last_lobby_list_;
    CeOnlineResolveResult last_resolve_;
    CeOnlineRoomState current_room_;
    CeOnlineTurnBundle last_turn_;
    CeOnlineSignalPollResult last_signal_poll_;
};

#endif // CE_ONLINE_LOBBY_CLIENT_H
