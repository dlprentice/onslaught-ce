// SPDX-License-Identifier: GPL-3.0-only
#include "online_lobby_client.h"

#include "monocypher.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

void WipeString(std::string& s) noexcept {
    if (!s.empty()) crypto_wipe(&s[0], s.size());
    s.clear();
}

int HexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool IsLowerHex(const std::string& s, size_t exact_len) noexcept {
    if (s.size() != exact_len) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool IsPrintableAscii(const std::string& s, size_t min_len, size_t max_len) noexcept {
    if (s.size() < min_len || s.size() > max_len) return false;
    for (unsigned char c : s) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

std::string UrlEncodeQueryValue(const std::string& s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0Fu]);
        }
    }
    return out;
}

bool IsSafeQueryParam(const std::string& s, size_t max_len) noexcept {
    if (s.size() > max_len) return false;
    if (s.find("..") != std::string::npos) return false;
    for (unsigned char c : s) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

bool ValidateLobbyPathLocal(const std::string& s) noexcept {
    static constexpr char kPrefix[] = "/ce-lobby/v1/";
    static constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (s.size() <= kPrefixLen || s.size() > 1024) return false;
    if (s.compare(0, kPrefixLen, kPrefix) != 0) return false;
    if (s.find("..") != std::string::npos) return false;
    for (unsigned char c : s) {
        if (c < 0x21 || c > 0x7E || c == '#' || c == '\\') return false;
    }
    const size_t q = s.find('?');
    const std::string path = (q == std::string::npos) ? s : s.substr(0, q);
    if (path.size() <= kPrefixLen || path.back() == '/' ||
        path.find("//") != std::string::npos || path.find('%') != std::string::npos)
        return false;
    return true;
}

// Strict bounded JSON parser.
class JsonParser {
public:
    explicit JsonParser(const std::string& input) : in_(input) {}

    bool Parse(CeOnlineJson::Value& out) {
        if (in_.empty() || in_.size() > CeOnlineJson::kMaxInputBytes) return false;
        if (!ParseValue(out, 0)) return false;
        SkipSpace();
        return pos_ == in_.size();
    }

private:
    void SkipSpace() {
        while (pos_ < in_.size() &&
               (in_[pos_] == ' ' || in_[pos_] == '\t' || in_[pos_] == '\r' || in_[pos_] == '\n'))
            ++pos_;
    }

    bool ParseString(std::string& out, size_t max_bytes) {
        out.clear();
        if (pos_ >= in_.size() || in_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < in_.size()) {
            unsigned char c = static_cast<unsigned char>(in_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20 || c > 0x7E) return false;
            if (c == '\\') {
                if (pos_ >= in_.size()) return false;
                const char esc = in_[pos_++];
                switch (esc) {
                case '"':  c = '"'; break;
                case '\\': c = '\\'; break;
                case '/':  c = '/'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u': {
                    if (pos_ + 4 > in_.size()) return false;
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const int n = HexNibble(in_[pos_++]);
                        if (n < 0) return false;
                        code = (code << 4) | static_cast<unsigned>(n);
                    }
                    if (code > 0x7Fu || (code < 0x20u && code != '\n' && code != '\r' && code != '\t'))
                        return false;
                    c = static_cast<unsigned char>(code);
                    break;
                }
                default:
                    return false;
                }
            }
            if (out.size() >= max_bytes) return false;
            out.push_back(static_cast<char>(c));
        }
        return false;
    }

    bool ParseNumber(CeOnlineJson::Value& out) {
        bool neg = false;
        if (in_[pos_] == '-') {
            neg = true;
            ++pos_;
            if (pos_ >= in_.size()) return false;
        }
        if (in_[pos_] < '0' || in_[pos_] > '9') return false;
        const size_t first = pos_;
        uint64_t val = 0;
        while (pos_ < in_.size() && in_[pos_] >= '0' && in_[pos_] <= '9') {
            const uint64_t d = static_cast<uint64_t>(in_[pos_++] - '0');
            if (pos_ - first > 1 && in_[first] == '0') return false; // leading zero
            if (val > (static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - d) / 10u)
                return false;
            val = val * 10u + d;
        }
        // Reject floats/exponents: protocol v1 numbers are strictly integers.
        if (pos_ < in_.size() && (in_[pos_] == '.' || in_[pos_] == 'e' || in_[pos_] == 'E'))
            return false;
        if (neg) {
            if (val == 0) return false; // reject -0
            out.kind = CeOnlineJson::Value::Kind::Int;
            out.int_val = -static_cast<int64_t>(val);
        } else {
            out.kind = CeOnlineJson::Value::Kind::UInt;
            out.uint_val = val;
            out.int_val = static_cast<int64_t>(val);
        }
        return true;
    }

    bool ParseValue(CeOnlineJson::Value& out, unsigned depth) {
        SkipSpace();
        if (pos_ >= in_.size() || depth > CeOnlineJson::kMaxDepth || ++nodes_ > CeOnlineJson::kMaxNodes)
            return false;
        const char c = in_[pos_];
        if (c == '"') {
            out.kind = CeOnlineJson::Value::Kind::String;
            return ParseString(out.str_val, CeOnlineJson::kMaxStringBytes);
        }
        if (c == '{') {
            out.kind = CeOnlineJson::Value::Kind::Object;
            ++pos_;
            SkipSpace();
            if (pos_ < in_.size() && in_[pos_] == '}') {
                ++pos_;
                return true;
            }
            for (size_t count = 0;; ++count) {
                if (count >= CeOnlineJson::kMaxObjectFields) return false;
                SkipSpace();
                std::string key;
                if (!ParseString(key, CeOnlineJson::kMaxKeyBytes)) return false;
                for (const auto& existing : out.obj_val) {
                    if (existing.first == key) return false; // reject duplicate keys
                }
                SkipSpace();
                if (pos_ >= in_.size() || in_[pos_++] != ':') return false;
                CeOnlineJson::Value child;
                if (!ParseValue(child, depth + 1)) return false;
                out.obj_val.emplace_back(std::move(key), std::move(child));
                SkipSpace();
                if (pos_ >= in_.size()) return false;
                const char sep = in_[pos_++];
                if (sep == '}') return true;
                if (sep != ',') return false;
            }
        }
        if (c == '[') {
            out.kind = CeOnlineJson::Value::Kind::Array;
            ++pos_;
            SkipSpace();
            if (pos_ < in_.size() && in_[pos_] == ']') {
                ++pos_;
                return true;
            }
            for (size_t count = 0;; ++count) {
                if (count >= CeOnlineJson::kMaxArrayItems) return false;
                CeOnlineJson::Value elem;
                if (!ParseValue(elem, depth + 1)) return false;
                out.arr_val.push_back(std::move(elem));
                SkipSpace();
                if (pos_ >= in_.size()) return false;
                const char sep = in_[pos_++];
                if (sep == ']') return true;
                if (sep != ',') return false;
            }
        }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        if (in_.compare(pos_, 4, "true") == 0) {
            out.kind = CeOnlineJson::Value::Kind::Bool;
            out.bool_val = true;
            pos_ += 4;
            return true;
        }
        if (in_.compare(pos_, 5, "false") == 0) {
            out.kind = CeOnlineJson::Value::Kind::Bool;
            out.bool_val = false;
            pos_ += 5;
            return true;
        }
        if (in_.compare(pos_, 4, "null") == 0) {
            out.kind = CeOnlineJson::Value::Kind::Null;
            pos_ += 4;
            return true;
        }
        return false;
    }

    const std::string& in_;
    size_t pos_ = 0;
    size_t nodes_ = 0;
};

// Typed JSON field getters.
bool ReqBool(const CeOnlineJson::Value& obj, const char* key, bool& out) {
    const auto* v = obj.Get(key);
    if (!v || !v->IsBool()) return false;
    out = v->bool_val;
    return true;
}

bool OptBool(const CeOnlineJson::Value& obj, const char* key, bool& out, bool def) {
    const auto* v = obj.Get(key);
    if (!v) { out = def; return true; }
    if (!v->IsBool()) return false;
    out = v->bool_val;
    return true;
}

bool ReqU64(const CeOnlineJson::Value& obj, const char* key, uint64_t& out) {
    const auto* v = obj.Get(key);
    if (!v || !v->IsUInt()) return false;
    out = v->uint_val;
    return true;
}

bool OptU64(const CeOnlineJson::Value& obj, const char* key, uint64_t& out, uint64_t def) {
    const auto* v = obj.Get(key);
    if (!v) { out = def; return true; }
    if (!v->IsUInt()) return false;
    out = v->uint_val;
    return true;
}

bool ReqU32(const CeOnlineJson::Value& obj, const char* key, uint32_t& out) {
    uint64_t v = 0;
    if (!ReqU64(obj, key, v) || v > std::numeric_limits<uint32_t>::max()) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool OptU32(const CeOnlineJson::Value& obj, const char* key, uint32_t& out, uint32_t def) {
    uint64_t v = 0;
    if (!OptU64(obj, key, v, def) || v > std::numeric_limits<uint32_t>::max()) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool ReqU16(const CeOnlineJson::Value& obj, const char* key, uint16_t& out) {
    uint64_t v = 0;
    if (!ReqU64(obj, key, v) || v > std::numeric_limits<uint16_t>::max()) return false;
    out = static_cast<uint16_t>(v);
    return true;
}

bool ReqStr(const CeOnlineJson::Value& obj, const char* key, std::string& out) {
    const auto* v = obj.Get(key);
    if (!v || !v->IsString()) return false;
    out = v->str_val;
    return true;
}

bool OptStr(const CeOnlineJson::Value& obj, const char* key, std::string& out) {
    const auto* v = obj.Get(key);
    if (!v || v->IsNull()) { out.clear(); return true; }
    if (!v->IsString()) return false;
    out = v->str_val;
    return true;
}

bool ParseStartTicket(const CeOnlineJson::Value& v, CeOnlineStartTicket& out) {
    out.Clear();
    if (v.IsNull()) return true;
    if (!v.IsObject()) return false;
    if (!ReqStr(v, "match_id", out.match_id) || !CeOnlineLobbyClient::IsValidSafeId(out.match_id) ||
        !ReqU32(v, "seed", out.seed) ||
        !ReqU32(v, "level", out.level) ||
        !ReqStr(v, "mode", out.mode) ||
        !ReqStr(v, "topology", out.topology) ||
        !ReqBool(v, "require_relay", out.require_relay) ||
        !ReqStr(v, "roster_hash", out.roster_hash) || !IsLowerHex(out.roster_hash, 64) ||
        !ReqStr(v, "match_secret", out.match_secret) || !IsLowerHex(out.match_secret, 64) ||
        !ReqU64(v, "issued_at", out.issued_at) ||
        !ReqU64(v, "expires_at", out.expires_at))
        return false;
    const auto* slots = v.Get("slots");
    if (!slots || !slots->IsArray() || slots->arr_val.size() > CE_ONLINE_WIRE_MAX_SLOTS) return false;
    out.slots.reserve(slots->arr_val.size());
    for (const auto& sv : slots->arr_val) {
        if (!sv.IsObject()) return false;
        CeOnlineStartSlot slot;
        if (!ReqU16(sv, "seat", slot.seat) || slot.seat >= CE_ONLINE_WIRE_MAX_SLOTS ||
            !ReqU16(sv, "sim_slot", slot.sim_slot) || slot.sim_slot >= CE_ONLINE_WIRE_MAX_SLOTS ||
            !ReqStr(sv, "kind", slot.kind) ||
            !OptStr(sv, "player_id", slot.player_id) ||
            !OptStr(sv, "callsign", slot.callsign) ||
            !OptStr(sv, "tag", slot.tag) ||
            !ReqU16(sv, "team", slot.team) ||
            !OptStr(sv, "be_cfg", slot.be_cfg))
            return false;
        out.slots.push_back(std::move(slot));
    }
    out.valid = true;
    return true;
}

bool ParseRoomState(const CeOnlineJson::Value& root, CeOnlineRoomState& out) {
    CeOnlineRoomState parsed;
    if (!root.IsObject()) return false;
    if (!ReqU16(root, "proto", parsed.proto) || parsed.proto != CE_ONLINE_PROTO_VERSION ||
        !ReqStr(root, "room_id", parsed.room_id) || !CeOnlineLobbyClient::IsValidSafeId(parsed.room_id) ||
        !ReqStr(root, "code", parsed.code) ||
        !ReqU64(root, "rev", parsed.rev) ||
        !ReqStr(root, "state", parsed.state) ||
        !ReqStr(root, "name", parsed.name) ||
        !ReqStr(root, "topology", parsed.topology) ||
        !ReqStr(root, "mode", parsed.mode) ||
        !ReqU32(root, "map", parsed.map) ||
        !ReqU16(root, "max_seats", parsed.max_seats) ||
        parsed.max_seats < 2 || parsed.max_seats > CE_ONLINE_WIRE_MAX_SLOTS ||
        !ReqU16(root, "host_seat", parsed.host_seat) || parsed.host_seat >= parsed.max_seats ||
        !ReqStr(root, "visibility", parsed.visibility) ||
        !ReqBool(root, "has_password", parsed.has_password) ||
        !ReqBool(root, "require_relay", parsed.require_relay) ||
        !ReqBool(root, "ai_fill", parsed.ai_fill) ||
        !ReqStr(root, "build_id", parsed.build_id) ||
        !OptStr(root, "content_id", parsed.content_id) ||
        !OptU64(root, "server_time_ms", parsed.server_time_ms, 0))
        return false;
    if (parsed.state != "lobby" && parsed.state != "starting" &&
        parsed.state != "in_match" && parsed.state != "closed")
        return false;

    if (const auto* teams = root.Get("teams")) {
        if (!teams->IsArray() || teams->arr_val.size() > 8) return false;
        for (const auto& tv : teams->arr_val) {
            if (!tv.IsObject()) return false;
            CeOnlineTeamInfo info;
            if (!ReqU16(tv, "team", info.team) || !ReqStr(tv, "side", info.side) ||
                !ReqU16(tv, "cap", info.cap))
                return false;
            parsed.teams.push_back(std::move(info));
        }
    }

    const auto* seats = root.Get("seats");
    if (!seats || !seats->IsArray() || seats->arr_val.size() > CE_ONLINE_WIRE_MAX_SLOTS) return false;
    parsed.seats.reserve(seats->arr_val.size());
    for (const auto& sv : seats->arr_val) {
        if (!sv.IsObject()) return false;
        CeOnlineSeat seat;
        if (!ReqU16(sv, "seat", seat.seat) || seat.seat >= parsed.max_seats ||
            !ReqStr(sv, "kind", seat.kind) ||
            !OptStr(sv, "player_id", seat.player_id) ||
            !OptStr(sv, "tag", seat.tag) ||
            !OptStr(sv, "callsign", seat.callsign) ||
            !ReqU16(sv, "team", seat.team) ||
            !OptStr(sv, "be_cfg", seat.be_cfg) ||
            !OptBool(sv, "ready", seat.ready, false) ||
            !OptU32(sv, "rtt_ms", seat.rtt_ms, 0) ||
            !OptStr(sv, "route", seat.route))
            return false;
        if (seat.kind != "human" && seat.kind != "ai" && seat.kind != "open" && seat.kind != "closed")
            return false;
        if (seat.route.empty()) seat.route = "unknown";
        parsed.seats.push_back(std::move(seat));
    }

    if (const auto* chat = root.Get("chat")) {
        if (!chat->IsArray() || chat->arr_val.size() > CE_ONLINE_MAX_CHAT_HISTORY) return false;
        parsed.chat.reserve(chat->arr_val.size());
        for (const auto& cv : chat->arr_val) {
            if (!cv.IsObject()) return false;
            CeOnlineChatMsg msg;
            if (!ReqU64(cv, "seq", msg.seq) ||
                !ReqU16(cv, "seat", msg.seat) ||
                !OptStr(cv, "player_id", msg.player_id) ||
                !OptStr(cv, "callsign", msg.callsign) ||
                !OptStr(cv, "tag", msg.tag) ||
                !ReqStr(cv, "text", msg.text) ||
                !ReqU64(cv, "ts", msg.ts))
                return false;
            parsed.chat.push_back(std::move(msg));
        }
    }

    if (const auto* ticket = root.Get("start_ticket")) {
        if (!ParseStartTicket(*ticket, parsed.start_ticket)) return false;
    }

    out = std::move(parsed);
    return true;
}

} // namespace

namespace CeOnlineJson {

Value::~Value() {
    WipeString(str_val);
}

const Value* Value::Get(const char* key) const noexcept {
    if (kind != Kind::Object || !key) return nullptr;
    for (const auto& kv : obj_val) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

bool Parse(const std::string& input, Value& out) {
    out = Value{};
    JsonParser parser(input);
    if (!parser.Parse(out)) {
        out = Value{};
        return false;
    }
    return true;
}

std::string EscapeString(const std::string& raw) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() + 2);
    out.push_back('"');
    for (unsigned char c : raw) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c > 0x7E) {
                out += "\\u00";
                out.push_back(kHex[c >> 4]);
                out.push_back(kHex[c & 0x0Fu]);
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

ObjectBuilder::ObjectBuilder() : buf_("{") {}

void ObjectBuilder::Comma() {
    if (!first_) buf_.push_back(',');
    first_ = false;
}

void ObjectBuilder::AddString(const char* key, const std::string& value) {
    Comma();
    buf_ += EscapeString(key ? key : "");
    buf_.push_back(':');
    buf_ += EscapeString(value);
}

void ObjectBuilder::AddUInt(const char* key, uint64_t value) {
    Comma();
    buf_ += EscapeString(key ? key : "");
    buf_.push_back(':');
    buf_ += std::to_string(value);
}

void ObjectBuilder::AddBool(const char* key, bool value) {
    Comma();
    buf_ += EscapeString(key ? key : "");
    buf_.push_back(':');
    buf_ += value ? "true" : "false";
}

void ObjectBuilder::AddRawObject(const char* key, const std::string& raw_json_object) {
    Comma();
    buf_ += EscapeString(key ? key : "");
    buf_.push_back(':');
    buf_ += raw_json_object;
}

std::string ObjectBuilder::Finish() {
    buf_.push_back('}');
    return std::move(buf_);
}

} // namespace CeOnlineJson

CeOnlineSession::~CeOnlineSession() { Clear(); }
void CeOnlineSession::Clear() noexcept {
    WipeString(session_token);
    player_id.clear();
    tag.clear();
    callsign.clear();
    expires_at = 0;
    server_time_ms = 0;
}

CeOnlineStartTicket::~CeOnlineStartTicket() { Clear(); }
void CeOnlineStartTicket::Clear() noexcept {
    valid = false;
    match_id.clear();
    seed = 0;
    level = 0;
    mode.clear();
    topology.clear();
    require_relay = false;
    roster_hash.clear();
    WipeString(match_secret);
    issued_at = 0;
    expires_at = 0;
    slots.clear();
}

void CeOnlineRoomState::Clear() noexcept {
    proto = 0;
    room_id.clear();
    code.clear();
    rev = 0;
    state.clear();
    name.clear();
    topology.clear();
    mode.clear();
    map = 0;
    max_seats = 0;
    host_seat = 0;
    visibility.clear();
    has_password = false;
    require_relay = false;
    ai_fill = false;
    build_id.clear();
    content_id.clear();
    server_time_ms = 0;
    teams.clear();
    seats.clear();
    chat.clear();
    start_ticket.Clear();
}

CeOnlineIceServer::~CeOnlineIceServer() { Clear(); }
void CeOnlineIceServer::Clear() noexcept {
    urls.clear();
    WipeString(username);
    WipeString(credential);
}

void CeOnlineTurnBundle::Clear() noexcept {
    ice_servers.clear();
    ttl = 0;
}

CeOnlineLobbyClient::CeOnlineLobbyClient(std::unique_ptr<CeOnlineHttps> transport)
    : transport_(std::move(transport)) {}

CeOnlineLobbyClient::~CeOnlineLobbyClient() {
    Cancel();
    session_.Clear();
    current_room_.Clear();
    last_turn_.Clear();
}

bool CeOnlineLobbyClient::IsValidMapId(uint32_t map) noexcept {
    return map >= 850 && map <= 866 && map != 858;
}

bool CeOnlineLobbyClient::IsValidMode(const std::string& mode) noexcept {
    return mode == "coop_defence" || mode == "coop_skirmish" ||
           mode == "versus_2v2";
}

bool CeOnlineLobbyClient::IsValidSafeId(const std::string& id) noexcept {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

void CeOnlineLobbyClient::Cancel() {
    if (transport_) transport_->Cancel();
    active_op_ = CeOnlineOpKind::None;
    status_ = CeOnlineOpStatus::Idle;
    auth_profile_ = nullptr;
    auth_stage_session_ = false;
}

bool CeOnlineLobbyClient::FailOp(unsigned http_code, const std::string& code, const std::string& msg) {
    last_http_code_ = http_code;
    last_error_code_ = code;
    last_error_message_ = msg;
    status_ = CeOnlineOpStatus::Failed;
    auth_profile_ = nullptr;
    auth_stage_session_ = false;
    return false;
}

void CeOnlineLobbyClient::ParseErrorBody(const std::string& body, std::string& out_code, std::string& out_msg) {
    out_code = "http_error";
    out_msg = "request failed";
    CeOnlineJson::Value root;
    if (CeOnlineJson::Parse(body, root) && root.IsObject()) {
        std::string ec, em;
        if (OptStr(root, "error", ec) && !ec.empty()) out_code = std::move(ec);
        if (OptStr(root, "message", em) && !em.empty()) out_msg = std::move(em);
    }
}

bool CeOnlineLobbyClient::StartHttp(CeOnlineOpKind op, CeOnlineHttpMethod method,
                                    const std::string& path_and_query, bool with_bearer,
                                    const std::string& body, uint64_t now_ms, uint64_t timeout_ms) {
    if (!transport_ || Busy() || timeout_ms == 0 || timeout_ms > 60000) return false;
    if (with_bearer && !Authenticated()) return false;
    if (!ValidateLobbyPathLocal(path_and_query)) return false;
    const uint64_t deadline = (now_ms <= std::numeric_limits<uint64_t>::max() - timeout_ms)
                                  ? now_ms + timeout_ms
                                  : std::numeric_limits<uint64_t>::max();
    const char* bearer = with_bearer ? session_.session_token.data() : nullptr;
    const size_t bearer_len = with_bearer ? session_.session_token.size() : 0;
    if (!transport_->Begin(method, path_and_query.c_str(), bearer, bearer_len, body, now_ms, deadline))
        return false;
    active_op_ = op;
    status_ = CeOnlineOpStatus::Pending;
    req_started_ms_ = now_ms;
    req_deadline_ms_ = deadline;
    last_http_code_ = 0;
    last_error_code_.clear();
    last_error_message_.clear();
    return true;
}

bool CeOnlineLobbyClient::BeginHealth(uint64_t now_ms, uint64_t timeout_ms) {
    return StartHttp(CeOnlineOpKind::Health, CeOnlineHttpMethod::Get, "/ce-lobby/v1/health",
                     false, "", now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginAuthenticate(const CeOnlineProfile& profile, const std::string& build_id,
                                            const std::string& content_id, uint64_t now_ms,
                                            uint64_t timeout_ms) {
    if (!profile.Loaded() || !CeOnlineProfile::IsValidCallsign(profile.Callsign()) ||
        !IsPrintableAscii(build_id, 1, 32) ||
        (!content_id.empty() && !IsLowerHex(content_id, 64)))
        return false;
    CeOnlineJson::ObjectBuilder jb;
    jb.AddString("pubkey", profile.PubkeyHex());
    jb.AddString("callsign", profile.Callsign());
    jb.AddString("build_id", build_id);
    jb.AddString("content_id", content_id);
    if (!StartHttp(CeOnlineOpKind::Authenticate, CeOnlineHttpMethod::Post,
                   "/ce-lobby/v1/auth/challenge", false, jb.Finish(), now_ms, timeout_ms))
        return false;
    auth_profile_ = &profile;
    auth_build_id_ = build_id;
    auth_content_id_ = content_id;
    auth_stage_session_ = false;
    return true;
}

bool CeOnlineLobbyClient::BeginListLobbies(const CeOnlineListLobbiesReq& req, uint64_t now_ms,
                                           uint64_t timeout_ms) {
    if (req.limit < 1 || req.limit > 32 ||
        !IsSafeQueryParam(req.build_id, 32) ||
        !IsSafeQueryParam(req.content_id, 64))
        return false;
    std::string path = "/ce-lobby/v1/lobbies?cursor=" + std::to_string(req.cursor) +
                       "&limit=" + std::to_string(req.limit) +
                       "&build_id=" + UrlEncodeQueryValue(req.build_id) +
                       "&content_id=" + UrlEncodeQueryValue(req.content_id) +
                       "&include_incompatible=" + (req.include_incompatible ? "1" : "0");
    return StartHttp(CeOnlineOpKind::ListLobbies, CeOnlineHttpMethod::Get, path,
                     true, "", now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginCreateLobby(const CeOnlineCreateLobbyReq& req, uint64_t now_ms,
                                           uint64_t timeout_ms) {
    if (!IsPrintableAscii(req.name, 1, 40) || !IsValidMode(req.mode) || !IsValidMapId(req.map) ||
        req.max_seats < 2 || req.max_seats > CE_ONLINE_WIRE_MAX_SLOTS ||
        (req.topology == "star_lockstep" && req.max_seats > CE_ONLINE_MAX_MATCH_SLOTS) ||
        (req.visibility != "public" && req.visibility != "unlisted") ||
        !IsPrintableAscii(req.password, 0, 64) ||
        !IsPrintableAscii(req.build_id, 1, 32) ||
        (!req.content_id.empty() && !IsLowerHex(req.content_id, 64)) ||
        !IsPrintableAscii(req.be_cfg, 0, 32))
        return false;
    CeOnlineJson::ObjectBuilder jb;
    jb.AddString("name", req.name);
    jb.AddString("mode", req.mode);
    jb.AddUInt("map", req.map);
    jb.AddUInt("max_seats", req.max_seats);
    jb.AddString("topology", req.topology);
    jb.AddString("visibility", req.visibility);
    jb.AddString("password", req.password);
    jb.AddBool("require_relay", req.require_relay);
    jb.AddBool("ai_fill", req.ai_fill);
    jb.AddString("build_id", req.build_id);
    jb.AddString("content_id", req.content_id);
    jb.AddString("be_cfg", req.be_cfg);
    std::string body = jb.Finish();
    const bool ok = StartHttp(CeOnlineOpKind::CreateLobby, CeOnlineHttpMethod::Post,
                              "/ce-lobby/v1/lobbies", true, body, now_ms, timeout_ms);
    WipeString(body);
    return ok;
}

bool CeOnlineLobbyClient::BeginResolveCode(const std::string& code, uint64_t now_ms,
                                           uint64_t timeout_ms) {
    if (code.size() != 6) return false;
    std::string upper;
    upper.reserve(6);
    for (char c : code) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
        upper.push_back(c);
    }
    CeOnlineJson::ObjectBuilder jb;
    jb.AddString("code", upper);
    return StartHttp(CeOnlineOpKind::ResolveCode, CeOnlineHttpMethod::Post,
                     "/ce-lobby/v1/lobbies/resolve", true, jb.Finish(), now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginPollRoom(const std::string& room_id, uint64_t since_rev,
                                        uint32_t rtt_ms, uint64_t now_ms, uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id)) return false;
    const uint32_t clamped_rtt = std::min<uint32_t>(rtt_ms, 9999u);
    const std::string path = "/ce-lobby/v1/rooms/" + room_id +
                             "?since=" + std::to_string(since_rev) +
                             "&rtt_ms=" + std::to_string(clamped_rtt);
    return StartHttp(CeOnlineOpKind::PollRoom, CeOnlineHttpMethod::Get, path,
                     true, "", now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginJoinRoom(const std::string& room_id, const CeOnlineJoinRoomReq& req,
                                        uint64_t now_ms, uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id) || !IsPrintableAscii(req.password, 0, 64) ||
        !IsPrintableAscii(req.build_id, 1, 32) ||
        (!req.content_id.empty() && !IsLowerHex(req.content_id, 64)) ||
        !IsPrintableAscii(req.be_cfg, 0, 32))
        return false;
    CeOnlineJson::ObjectBuilder jb;
    jb.AddString("password", req.password);
    jb.AddString("build_id", req.build_id);
    jb.AddString("content_id", req.content_id);
    jb.AddString("be_cfg", req.be_cfg);
    std::string body = jb.Finish();
    const bool ok = StartHttp(CeOnlineOpKind::JoinRoom, CeOnlineHttpMethod::Post,
                              "/ce-lobby/v1/rooms/" + room_id + "/join", true, body, now_ms, timeout_ms);
    WipeString(body);
    return ok;
}

bool CeOnlineLobbyClient::BeginLeaveRoom(const std::string& room_id, uint64_t now_ms,
                                         uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id)) return false;
    return StartHttp(CeOnlineOpKind::LeaveRoom, CeOnlineHttpMethod::Post,
                     "/ce-lobby/v1/rooms/" + room_id + "/leave", true, "{}", now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginUpdateRoom(const std::string& room_id, const CeOnlineUpdateRoomReq& req,
                                          uint64_t now_ms, uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id)) return false;
    CeOnlineJson::ObjectBuilder jb;
    bool any = false;
    if (req.has_team) {
        if (req.team > 7) return false;
        jb.AddUInt("team", req.team);
        any = true;
    }
    if (req.has_be_cfg) {
        if (!IsPrintableAscii(req.be_cfg, 0, 32)) return false;
        jb.AddString("be_cfg", req.be_cfg);
        any = true;
    }
    if (req.has_ready) {
        jb.AddBool("ready", req.ready);
        any = true;
    }
    if (req.has_rtt_ms) {
        jb.AddUInt("rtt_ms", std::min<uint32_t>(req.rtt_ms, 9999u));
        any = true;
    }
    if (req.has_route) {
        if (req.route != "unknown" && req.route != "direct" && req.route != "relay") return false;
        jb.AddString("route", req.route);
        any = true;
    }
    if (req.has_state) {
        if (req.state != "in_match") return false;
        jb.AddString("state", req.state);
        any = true;
    }
    if (req.has_name) {
        if (!IsPrintableAscii(req.name, 1, 40)) return false;
        jb.AddString("name", req.name);
        any = true;
    }
    if (req.has_map) {
        if (!IsValidMapId(req.map)) return false;
        jb.AddUInt("map", req.map);
        any = true;
    }
    if (req.has_mode) {
        if (!IsValidMode(req.mode)) return false;
        jb.AddString("mode", req.mode);
        any = true;
    }
    if (req.has_max_seats) {
        if (req.max_seats < 2 || req.max_seats > CE_ONLINE_WIRE_MAX_SLOTS) return false;
        jb.AddUInt("max_seats", req.max_seats);
        any = true;
    }
    if (req.has_visibility) {
        if (req.visibility != "public" && req.visibility != "unlisted") return false;
        jb.AddString("visibility", req.visibility);
        any = true;
    }
    if (req.has_require_relay) {
        jb.AddBool("require_relay", req.require_relay);
        any = true;
    }
    if (req.has_ai_fill) {
        jb.AddBool("ai_fill", req.ai_fill);
        any = true;
    }
    if (req.has_set_seat_kind) {
        if (req.set_seat_kind_seat >= CE_ONLINE_WIRE_MAX_SLOTS ||
            (req.set_seat_kind != "ai" && req.set_seat_kind != "open" && req.set_seat_kind != "closed"))
            return false;
        CeOnlineJson::ObjectBuilder sk;
        sk.AddUInt("seat", req.set_seat_kind_seat);
        sk.AddString("kind", req.set_seat_kind);
        jb.AddRawObject("set_seat_kind", sk.Finish());
        any = true;
    }
    if (req.has_kick_seat) {
        if (req.kick_seat >= CE_ONLINE_WIRE_MAX_SLOTS) return false;
        jb.AddUInt("kick_seat", req.kick_seat);
        any = true;
    }
    if (!any) return false;
    return StartHttp(CeOnlineOpKind::UpdateRoom, CeOnlineHttpMethod::Post,
                     "/ce-lobby/v1/rooms/" + room_id + "/update", true, jb.Finish(), now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginSendChat(const std::string& room_id, const std::string& text,
                                        uint64_t now_ms, uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id) || !IsPrintableAscii(text, 1, CE_ONLINE_MAX_CHAT_BYTES)) return false;
    CeOnlineJson::ObjectBuilder jb;
    jb.AddString("text", text);
    return StartHttp(CeOnlineOpKind::SendChat, CeOnlineHttpMethod::Post,
                     "/ce-lobby/v1/rooms/" + room_id + "/chat", true, jb.Finish(), now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginStartMatch(const std::string& room_id, uint64_t now_ms,
                                          uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id)) return false;
    return StartHttp(CeOnlineOpKind::StartMatch, CeOnlineHttpMethod::Post,
                     "/ce-lobby/v1/rooms/" + room_id + "/start", true, "{}", now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginFinishMatch(const std::string& room_id, uint64_t now_ms,
                                           uint64_t timeout_ms, const std::string& match_id) {
    if (!IsValidSafeId(room_id)) return false;
    if (!match_id.empty() && !IsValidSafeId(match_id)) return false;
    std::string body = "{}";
    if (!match_id.empty()) {
        CeOnlineJson::ObjectBuilder jb;
        jb.AddString("match_id", match_id);
        body = jb.Finish();
    }
    return StartHttp(CeOnlineOpKind::FinishMatch, CeOnlineHttpMethod::Post,
                     "/ce-lobby/v1/rooms/" + room_id + "/finish", true, body, now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginFetchTurn(const std::string& room_id, const std::string& match_id,
                                         uint16_t guest_seat, uint64_t now_ms, uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id) || !IsValidSafeId(match_id) || guest_seat >= CE_ONLINE_WIRE_MAX_SLOTS)
        return false;
    CeOnlineJson::ObjectBuilder jb;
    jb.AddString("match_id", match_id);
    jb.AddUInt("guest_seat", guest_seat);
    return StartHttp(CeOnlineOpKind::FetchTurn, CeOnlineHttpMethod::Post,
                     "/ce-lobby/v1/rooms/" + room_id + "/turn", true, jb.Finish(), now_ms, timeout_ms);
}

bool CeOnlineLobbyClient::BeginSendSignal(const std::string& room_id, const std::string& match_id,
                                          uint16_t from_seat, uint16_t to_seat, const std::string& kind,
                                          const std::string& sdp, uint64_t now_ms, uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id) || !IsValidSafeId(match_id) ||
        from_seat >= CE_ONLINE_WIRE_MAX_SLOTS || to_seat >= CE_ONLINE_WIRE_MAX_SLOTS ||
        (kind != "offer" && kind != "answer") || sdp.empty() || sdp.size() > CE_ONLINE_MAX_SDP_BYTES)
        return false;
    for (unsigned char c : sdp) {
        if (c != '\r' && c != '\n' && c != '\t' && (c < 0x20 || c > 0x7E)) return false;
    }
    CeOnlineJson::ObjectBuilder jb;
    jb.AddString("match_id", match_id);
    jb.AddUInt("from_seat", from_seat);
    jb.AddUInt("to_seat", to_seat);
    jb.AddString("kind", kind);
    jb.AddString("sdp", sdp);
    std::string body = jb.Finish();
    last_sent_signal_seq_ = 0;
    const bool ok = StartHttp(CeOnlineOpKind::SendSignal, CeOnlineHttpMethod::Post,
                              "/ce-lobby/v1/rooms/" + room_id + "/signal", true, body, now_ms, timeout_ms);
    WipeString(body);
    return ok;
}

bool CeOnlineLobbyClient::BeginPollSignal(const std::string& room_id, const std::string& match_id,
                                          uint64_t since_seq, uint64_t now_ms, uint64_t timeout_ms) {
    if (!IsValidSafeId(room_id) || !IsValidSafeId(match_id)) return false;
    const std::string path = "/ce-lobby/v1/rooms/" + room_id + "/signal?match_id=" + match_id +
                             "&since=" + std::to_string(since_seq);
    return StartHttp(CeOnlineOpKind::PollSignal, CeOnlineHttpMethod::Get, path,
                     true, "", now_ms, timeout_ms);
}

CeOnlineOpStatus CeOnlineLobbyClient::Poll(uint64_t now_ms) {
    if (status_ != CeOnlineOpStatus::Pending || !transport_) return status_;
    CeSignalHttpsReply reply;
    const CeSignalHttpsState st = transport_->Poll(now_ms, &reply);
    struct ReplyWiper {
        std::string& b;
        ~ReplyWiper() { WipeString(b); }
    } wiper{reply.body};

    if (st == CeSignalHttpsState::Pending) return CeOnlineOpStatus::Pending;
    if (st == CeSignalHttpsState::Failed) {
        FailOp(0, "transport_error", "network request failed or timed out");
        return CeOnlineOpStatus::Failed;
    }
    last_rtt_ms_ = (now_ms >= req_started_ms_)
                       ? static_cast<uint32_t>(std::min<uint64_t>(now_ms - req_started_ms_, 60000u))
                       : 0u;
    status_ = CeOnlineOpStatus::Completed;
    if (!HandleReply(reply, now_ms)) return status_;
    return status_;
}

bool CeOnlineLobbyClient::HandleReply(const CeSignalHttpsReply& reply, uint64_t now_ms) {
    last_http_code_ = reply.code;
    if (reply.code != 200) {
        std::string ec, em;
        ParseErrorBody(reply.body, ec, em);
        return FailOp(reply.code, ec, em);
    }

    CeOnlineJson::Value root;
    if (!CeOnlineJson::Parse(reply.body, root) || !root.IsObject())
        return FailOp(reply.code, "invalid_json", "malformed JSON reply from lobby service");

    switch (active_op_) {
    case CeOnlineOpKind::Health: {
        CeOnlineHealth h;
        if (!ReqBool(root, "ok", h.ok) ||
            !ReqBool(root, "enabled", h.enabled) ||
            !ReqU16(root, "proto", h.proto) ||
            !ReqU16(root, "max_match_slots", h.max_match_slots) ||
            !ReqU16(root, "wire_max_slots", h.wire_max_slots) ||
            !ReqU64(root, "server_time_ms", h.server_time_ms) ||
            !ReqU32(root, "active_rooms", h.active_rooms))
            return FailOp(reply.code, "invalid_response", "invalid health response");
        h.rtt_ms = last_rtt_ms_;
        last_health_ = h;
        return true;
    }

    case CeOnlineOpKind::Authenticate: {
        if (!auth_profile_ || !auth_profile_->Loaded())
            return FailOp(reply.code, "auth_state", "profile unavailable during authentication");
        if (!auth_stage_session_) {
            std::string nonce;
            uint64_t issued_at = 0, expires_at = 0, server_time_ms = 0;
            if (!ReqStr(root, "nonce", nonce) || !CeOnlineProfile::IsValidNonceHex(nonce) ||
                !ReqU64(root, "issued_at", issued_at) || issued_at == 0 ||
                !ReqU64(root, "expires_at", expires_at) || expires_at <= issued_at ||
                !OptU64(root, "server_time_ms", server_time_ms, 0))
                return FailOp(reply.code, "invalid_challenge", "invalid auth challenge");
            std::string sig_hex;
            if (!auth_profile_->SignAuthChallenge(nonce, issued_at, auth_profile_->Callsign(), sig_hex))
                return FailOp(reply.code, "sign_failed", "failed to sign auth challenge");

            CeOnlineJson::ObjectBuilder jb;
            jb.AddString("pubkey", auth_profile_->PubkeyHex());
            jb.AddString("callsign", auth_profile_->Callsign());
            jb.AddString("nonce", nonce);
            jb.AddUInt("issued_at", issued_at);
            jb.AddString("signature", sig_hex);
            jb.AddString("build_id", auth_build_id_);
            jb.AddString("content_id", auth_content_id_);

            const uint64_t rem = (req_deadline_ms_ > now_ms) ? (req_deadline_ms_ - now_ms) : 0;
            if (rem == 0) return FailOp(0, "timeout", "authentication timed out");
            auth_stage_session_ = true;
            req_started_ms_ = now_ms;
            if (!transport_->Begin(CeOnlineHttpMethod::Post, "/ce-lobby/v1/auth/session",
                                   nullptr, 0, jb.Finish(), now_ms, req_deadline_ms_))
                return FailOp(0, "transport_error", "failed to start auth session request");
            status_ = CeOnlineOpStatus::Pending;
            return true;
        }
        CeOnlineSession sess;
        if (!ReqStr(root, "session_token", sess.session_token) || sess.session_token.empty() ||
            !ReqStr(root, "player_id", sess.player_id) ||
            !ReqStr(root, "tag", sess.tag) ||
            !ReqStr(root, "callsign", sess.callsign) ||
            !ReqU64(root, "expires_at", sess.expires_at) ||
            !OptU64(root, "server_time_ms", sess.server_time_ms, 0))
            return FailOp(reply.code, "invalid_session", "invalid auth session reply");
        if (sess.player_id != auth_profile_->PlayerId() || sess.tag != auth_profile_->Tag())
            return FailOp(reply.code, "identity_mismatch", "server identity does not match local keypair");
        session_ = std::move(sess);
        auth_profile_ = nullptr;
        auth_stage_session_ = false;
        return true;
    }

    case CeOnlineOpKind::ListLobbies: {
        CeOnlineLobbyList list;
        if (!ReqU16(root, "proto", list.proto) || list.proto != CE_ONLINE_PROTO_VERSION ||
            !OptU64(root, "server_time_ms", list.server_time_ms, 0) ||
            !ReqU32(root, "next_cursor", list.next_cursor))
            return FailOp(reply.code, "invalid_response", "invalid lobby list envelope");
        const auto* rooms = root.Get("rooms");
        if (!rooms || !rooms->IsArray() || rooms->arr_val.size() > 64)
            return FailOp(reply.code, "invalid_response", "invalid rooms array");
        list.rooms.reserve(rooms->arr_val.size());
        for (const auto& rv : rooms->arr_val) {
            if (!rv.IsObject()) return FailOp(reply.code, "invalid_response", "invalid room summary");
            CeOnlineRoomSummary sum;
            if (!ReqStr(rv, "room_id", sum.room_id) || !IsValidSafeId(sum.room_id) ||
                !ReqStr(rv, "code", sum.code) ||
                !ReqStr(rv, "name", sum.name) ||
                !ReqStr(rv, "host_callsign", sum.host_callsign) ||
                !ReqStr(rv, "host_tag", sum.host_tag) ||
                !ReqStr(rv, "topology", sum.topology) ||
                !ReqStr(rv, "mode", sum.mode) ||
                !ReqU32(rv, "map", sum.map) ||
                !ReqU16(rv, "used_seats", sum.used_seats) ||
                !ReqU16(rv, "human_seats", sum.human_seats) ||
                !ReqU16(rv, "max_seats", sum.max_seats) ||
                !ReqBool(rv, "has_password", sum.has_password) ||
                !ReqBool(rv, "require_relay", sum.require_relay) ||
                !ReqStr(rv, "state", sum.state) ||
                !ReqU16(rv, "proto", sum.proto) ||
                !ReqStr(rv, "build_id", sum.build_id) ||
                !OptStr(rv, "content_id", sum.content_id) ||
                !ReqBool(rv, "compatible", sum.compatible) ||
                !OptU64(rv, "updated_at", sum.updated_at, 0))
                return FailOp(reply.code, "invalid_response", "malformed room summary field");
            list.rooms.push_back(std::move(sum));
        }
        last_lobby_list_ = std::move(list);
        return true;
    }

    case CeOnlineOpKind::ResolveCode: {
        CeOnlineResolveResult res;
        if (!ReqStr(root, "room_id", res.room_id) || !IsValidSafeId(res.room_id) ||
            !ReqBool(root, "has_password", res.has_password) ||
            !ReqStr(root, "state", res.state))
            return FailOp(reply.code, "invalid_response", "invalid resolve response");
        last_resolve_ = std::move(res);
        return true;
    }

    case CeOnlineOpKind::PollRoom: {
        bool unchanged = false;
        if (OptBool(root, "unchanged", unchanged, false) && unchanged) {
            uint64_t rev = 0, st_ms = 0;
            if (!ReqU64(root, "rev", rev) || !OptU64(root, "server_time_ms", st_ms, 0))
                return FailOp(reply.code, "invalid_response", "invalid unchanged poll response");
            last_poll_unchanged_ = true;
            current_room_.rev = rev;
            if (st_ms) current_room_.server_time_ms = st_ms;
            return true;
        }
        if (!ParseRoomState(root, current_room_))
            return FailOp(reply.code, "invalid_response", "invalid room state response");
        last_poll_unchanged_ = false;
        return true;
    }

    case CeOnlineOpKind::CreateLobby:
    case CeOnlineOpKind::JoinRoom:
    case CeOnlineOpKind::UpdateRoom:
    case CeOnlineOpKind::StartMatch:
    case CeOnlineOpKind::FinishMatch: {
        if (!ParseRoomState(root, current_room_))
            return FailOp(reply.code, "invalid_response", "invalid room state response");
        last_poll_unchanged_ = false;
        return true;
    }

    case CeOnlineOpKind::SendChat: {
        if (root.Get("room_id")) {
            if (!ParseRoomState(root, current_room_))
                return FailOp(reply.code, "invalid_response", "invalid room state after chat");
        }
        return true;
    }

    case CeOnlineOpKind::LeaveRoom: {
        current_room_.Clear();
        return true;
    }

    case CeOnlineOpKind::FetchTurn: {
        CeOnlineTurnBundle bundle;
        if (!ReqU32(root, "ttl", bundle.ttl))
            return FailOp(reply.code, "invalid_response", "missing ttl in TURN response");
        const auto* servers = root.Get("iceServers");
        if (!servers || !servers->IsArray() || servers->arr_val.size() > 16)
            return FailOp(reply.code, "invalid_response", "invalid iceServers array");
        bundle.ice_servers.reserve(servers->arr_val.size());
        for (const auto& sv : servers->arr_val) {
            if (!sv.IsObject()) return FailOp(reply.code, "invalid_response", "invalid iceServer object");
            CeOnlineIceServer srv;
            const auto* urls = sv.Get("urls");
            if (!urls) return FailOp(reply.code, "invalid_response", "missing iceServer urls");
            if (urls->IsString()) {
                if (urls->str_val.empty()) return FailOp(reply.code, "invalid_response", "empty iceServer url");
                srv.urls.push_back(urls->str_val);
            } else if (urls->IsArray() && !urls->arr_val.empty() && urls->arr_val.size() <= 8) {
                for (const auto& uv : urls->arr_val) {
                    if (!uv.IsString() || uv.str_val.empty())
                        return FailOp(reply.code, "invalid_response", "invalid iceServer url entry");
                    srv.urls.push_back(uv.str_val);
                }
            } else {
                return FailOp(reply.code, "invalid_response", "malformed iceServer urls");
            }
            if (!OptStr(sv, "username", srv.username) || !OptStr(sv, "credential", srv.credential))
                return FailOp(reply.code, "invalid_response", "invalid iceServer credentials");
            bundle.ice_servers.push_back(std::move(srv));
        }
        last_turn_ = std::move(bundle);
        return true;
    }

    case CeOnlineOpKind::SendSignal: {
        bool ok_val = true;
        uint64_t seq = 0;
        uint64_t srv_ms = 0;
        if (!OptBool(root, "ok", ok_val, true) || !ok_val ||
            !OptU64(root, "seq", seq, 0) ||
            !OptU64(root, "server_time_ms", srv_ms, 0))
            return FailOp(reply.code, "invalid_response", "invalid signal send response");
        last_sent_signal_seq_ = seq;
        return true;
    }

    case CeOnlineOpKind::PollSignal: {
        CeOnlineSignalPollResult poll;
        if (!ReqU64(root, "seq", poll.seq))
            return FailOp(reply.code, "invalid_response", "missing seq in signal poll");
        const auto* msgs = root.Get("messages");
        if (!msgs || !msgs->IsArray() || msgs->arr_val.size() > 64)
            return FailOp(reply.code, "invalid_response", "invalid signal messages array");
        poll.messages.reserve(msgs->arr_val.size());
        for (const auto& mv : msgs->arr_val) {
            if (!mv.IsObject()) return FailOp(reply.code, "invalid_response", "invalid signal message");
            CeOnlineSignalMessage m;
            if (!ReqU64(mv, "seq", m.seq) ||
                !ReqU16(mv, "from_seat", m.from_seat) || m.from_seat >= CE_ONLINE_WIRE_MAX_SLOTS ||
                !ReqU16(mv, "to_seat", m.to_seat) || m.to_seat >= CE_ONLINE_WIRE_MAX_SLOTS ||
                !ReqStr(mv, "kind", m.kind) || (m.kind != "offer" && m.kind != "answer") ||
                !ReqStr(mv, "sdp", m.sdp) || m.sdp.empty() || m.sdp.size() > CE_ONLINE_MAX_SDP_BYTES)
                return FailOp(reply.code, "invalid_response", "malformed signal message entry");
            poll.messages.push_back(std::move(m));
        }
        last_signal_poll_ = std::move(poll);
        return true;
    }

    case CeOnlineOpKind::None:
    default:
        return FailOp(reply.code, "internal_error", "unexpected active op");
    }
}
