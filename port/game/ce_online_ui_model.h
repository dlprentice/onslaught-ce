// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_ui_model.h: Pure C++14/17 UI state machine, 640x480 layout engine,
// BEA bitmap-font sanitizer, and abstract backend interface for Onslaught CE Online Multiplayer.
//
// Zero game headers so host ASan/UBSan tests compile and verify every page, transition,
// guardrail, pagination path (4-slot active cap + 64-slot Assault-ready paging), and
// 640x480 geometry invariant directly.
#ifndef CE_ONLINE_UI_MODEL_H
#define CE_ONLINE_UI_MODEL_H

#include "ce_online_ui.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#pragma push_macro("min")
#pragma push_macro("max")
#pragma push_macro("new")
#undef min
#undef max
#undef new
#include <algorithm>
#include <set>
#include <string>
#include <vector>
#pragma pop_macro("new")
#pragma pop_macro("max")
#pragma pop_macro("min")

// Protocol & UI capacity constants (0.1.14 active cap = 4; wire/UI pagination = 64+ ready).
static const uint16_t CE_ONLINE_UI_MIN_SLOTS = 2;
static const uint16_t CE_ONLINE_UI_ACTIVE_MAX_SLOTS = 4;
static const uint16_t CE_ONLINE_UI_WIRE_MAX_SLOTS = 64;
static const size_t   CE_ONLINE_UI_CHAT_RING = 32;
static const size_t   CE_ONLINE_UI_CHAT_VISIBLE = 6;
static const int      CE_ONLINE_UI_BROWSER_PAGE_SIZE = 6;
static const int      CE_ONLINE_UI_ROSTER_PAGE_SIZE = 4;
static const size_t   CE_ONLINE_UI_CALLSIGN_MIN = 3;
static const size_t   CE_ONLINE_UI_CALLSIGN_MAX = 20;
static const size_t   CE_ONLINE_UI_ROOM_NAME_MIN = 3;
static const size_t   CE_ONLINE_UI_ROOM_NAME_MAX = 28;
static const size_t   CE_ONLINE_UI_CODE_LEN = 6;
static const size_t   CE_ONLINE_UI_PASSWORD_MAX = 24;
static const size_t   CE_ONLINE_UI_CHAT_MAX = 160;

// Virtual 640x480 front-end safe band (between the title bar and bottom help bar, inside side arcs).
static const float CE_ONLINE_SAFE_X0 = 80.f;
static const float CE_ONLINE_SAFE_X1 = 560.f;
static const float CE_ONLINE_SAFE_Y0 = 104.f;
static const float CE_ONLINE_SAFE_Y1 = 448.f;

// Required privacy notice for Direct P2P vs Relay Only (TURN).
static const char *const kCeOnlinePrivacyNotice =
    "Direct P2P shows your IP to players who join. Relay Only hides your IP via TURN.";

// ---------------------------------------------------------------------------
// BEA Bitmap Font Safety Whitelist
// CeOnlineFontCharSafe is shared with the game adapter in ce_online_ui.h.
// ---------------------------------------------------------------------------
inline bool CeOnlineStringIsFontSafe(const std::string &s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (!CeOnlineFontCharSafe(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

inline std::string CeOnlineSanitizeForFont(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (CeOnlineFontCharSafe(c)) {
            out.push_back(static_cast<char>(c));
        } else if (c == '_') {
            out.push_back('-');
        } else if (c == '\\') {
            out.push_back('/');
        } else if (c == '\r' || c == '\n' || c == '\t') {
            out.push_back(' ');
        } else {
            out.push_back(' ');
        }
    }
    return out;
}

inline std::string CeOnlineTruncateFontSafe(const std::string &in, size_t maxChars) {
    std::string safe = CeOnlineSanitizeForFont(in);
    if (safe.size() <= maxChars) return safe;
    if (maxChars <= 3) return safe.substr(0, maxChars);
    return safe.substr(0, maxChars - 3) + "...";
}

// Formats the lobby server display line without ever exposing raw default hostnames/URLs.
inline std::string CeOnlineFormatServerDisplay(const std::string &serverUrl, bool hasCustomIniOverride = false) {
    if (hasCustomIniOverride) {
        return "Custom Server (ce.ini)";
    }
    if (serverUrl.empty() || serverUrl == "Not configured" || serverUrl == "Not connected") {
        return "Not configured";
    }
    return "Official Online Service";
}

// Fits sanitized text into a maximum pixel width at scale >= minScale (0.65f) by scaling down
// first and ellipsizing with "..." when even minScale would exceed maxW (preventing column bleed).
// When keepTail is true (active text inputs with cursor), trims from the left so live typing stays visible.
template <typename MeasureFn>
inline void CeOnlineFitTextToWidth(const std::string &in, float maxW, float minScale,
                                   MeasureFn measure, std::string *outText,
                                   float *outScale, float *outDrawW, bool keepTail = false) {
    std::string safe = CeOnlineSanitizeForFont(in);
    if (maxW <= 0.f || safe.empty()) {
        if (outText) outText->clear();
        if (outScale) *outScale = 1.f;
        if (outDrawW) *outDrawW = 0.f;
        return;
    }
    if (minScale <= 0.1f) minScale = 0.65f;
    if (minScale > 1.f) minScale = 1.f;

    float w = measure(safe);
    if (w <= maxW) {
        if (outText) *outText = safe;
        if (outScale) *outScale = 1.f;
        if (outDrawW) *outDrawW = w;
        return;
    }
    if (w * minScale <= maxW && w > 0.f) {
        const float sc = maxW / w;
        if (outText) *outText = safe;
        if (outScale) *outScale = sc;
        if (outDrawW) *outDrawW = w * sc;
        return;
    }

    std::string best;
    const size_t len = safe.size();
    for (size_t keep = len; keep > 0; --keep) {
        const size_t n = keep - 1;
        std::string cand;
        if (keepTail) {
            cand = (n > 0) ? (std::string("...") + safe.substr(len - n)) : std::string("...");
        } else {
            cand = (n > 0) ? (safe.substr(0, n) + "...") : std::string("...");
        }
        const float cw = measure(cand);
        if (cw * minScale <= maxW) {
            best = cand;
            w = cw;
            break;
        }
    }
    if (best.empty()) {
        static const char *const kDots[] = { "..", "." };
        for (int i = 0; i < 2; ++i) {
            const float cw = measure(std::string(kDots[i]));
            if (cw * minScale <= maxW) {
                best = kDots[i];
                w = cw;
                break;
            }
        }
        if (best.empty()) w = 0.f;
    }

    float sc = 1.f;
    if (w > maxW && w > 0.f) {
        sc = maxW / w;
        if (sc < minScale) sc = minScale;
    }
    if (outText) *outText = best;
    if (outScale) *outScale = sc;
    if (outDrawW) *outDrawW = w * sc;
}

// ---------------------------------------------------------------------------
// Input Validation Helpers (matching port/services/online_lobby/PROTOCOL.md)
// ---------------------------------------------------------------------------
inline bool CeOnlineIsCallsignChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}

inline bool CeOnlineValidateCallsign(const std::string &raw, std::string *outNormalized, std::string *outError) {
    std::string collapsed;
    collapsed.reserve(raw.size());
    bool prevSpace = true;
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (!CeOnlineIsCallsignChar(c)) {
            if (outError) {
                *outError = "Callsign may use only letters, digits, spaces, hyphens and underscores.";
            }
            return false;
        }
        if (c == ' ') {
            if (!prevSpace) {
                collapsed.push_back(' ');
                prevSpace = true;
            }
        } else {
            collapsed.push_back(static_cast<char>(c));
            prevSpace = false;
        }
    }
    while (!collapsed.empty() && collapsed[collapsed.size() - 1] == ' ') {
        collapsed.erase(collapsed.size() - 1);
    }
    if (collapsed.size() < CE_ONLINE_UI_CALLSIGN_MIN) {
        if (outError) *outError = "Callsign must be at least 3 characters.";
        return false;
    }
    if (collapsed.size() > CE_ONLINE_UI_CALLSIGN_MAX) {
        if (outError) *outError = "Callsign must be 20 characters or fewer.";
        return false;
    }
    if (outNormalized) *outNormalized = collapsed;
    if (outError) outError->clear();
    return true;
}

inline bool CeOnlineValidateRoomName(const std::string &raw, std::string *outNormalized, std::string *outError) {
    std::string collapsed;
    collapsed.reserve(raw.size());
    bool prevSpace = true;
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (!CeOnlineFontCharSafe(c)) {
            if (outError) *outError = "Lobby name contains an unsupported character.";
            return false;
        }
        if (c == ' ') {
            if (!prevSpace) {
                collapsed.push_back(' ');
                prevSpace = true;
            }
        } else {
            collapsed.push_back(static_cast<char>(c));
            prevSpace = false;
        }
    }
    while (!collapsed.empty() && collapsed[collapsed.size() - 1] == ' ') {
        collapsed.erase(collapsed.size() - 1);
    }
    if (collapsed.size() < CE_ONLINE_UI_ROOM_NAME_MIN) {
        if (outError) *outError = "Lobby name must be at least 3 characters.";
        return false;
    }
    if (collapsed.size() > CE_ONLINE_UI_ROOM_NAME_MAX) {
        if (outError) *outError = "Lobby name must be 28 characters or fewer.";
        return false;
    }
    if (outNormalized) *outNormalized = collapsed;
    if (outError) outError->clear();
    return true;
}

inline bool CeOnlineValidateRoomCode(const std::string &raw, std::string *outCode, std::string *outError) {
    std::string cleaned;
    cleaned.reserve(CE_ONLINE_UI_CODE_LEN);
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c == ' ' || c == '-' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            if (outError) *outError = "Lobby codes use only letters and digits.";
            return false;
        }
        cleaned.push_back(static_cast<char>(c));
    }
    if (cleaned.size() != CE_ONLINE_UI_CODE_LEN) {
        if (outError) *outError = "Enter a 6-character lobby code.";
        return false;
    }
    if (outCode) *outCode = cleaned;
    if (outError) outError->clear();
    return true;
}

// ---------------------------------------------------------------------------
// Modes & 16 Verified Retail Online Maps Catalog (strict map <-> mode contract)
// ---------------------------------------------------------------------------
enum CeOnlineMode {
    CE_ONLINE_MODE_VERSUS_2V2 = 0,
    CE_ONLINE_MODE_COOP_SKIRMISH = 1,
    CE_ONLINE_MODE_COOP_DEFENCE = 2,
    CE_ONLINE_MODE_COUNT = 3
};

struct CeOnlineModeInfo {
    CeOnlineMode id;
    const char *wire;
    const char *label;
};

inline const CeOnlineModeInfo &CeOnlineModeAt(int index) {
    static const CeOnlineModeInfo kModes[CE_ONLINE_MODE_COUNT] = {
        { CE_ONLINE_MODE_VERSUS_2V2,    "versus_2v2",    "2v2 Versus" },
        { CE_ONLINE_MODE_COOP_SKIRMISH, "coop_skirmish", "Co-op Skirmish" },
        { CE_ONLINE_MODE_COOP_DEFENCE,  "coop_defence",  "Co-op Defence" },
    };
    if (index < 0 || index >= CE_ONLINE_MODE_COUNT) return kModes[0];
    return kModes[index];
}

inline int CeOnlineFindModeIndex(const std::string &wire) {
    for (int i = 0; i < CE_ONLINE_MODE_COUNT; ++i) {
        if (wire == CeOnlineModeAt(i).wire) return i;
    }
    return 0;
}

inline const char *CeOnlineModeLabel(const std::string &wire) {
    return CeOnlineModeAt(CeOnlineFindModeIndex(wire)).label;
}

inline const char *CeOnlineModeDisplayLabel(const std::string &wire, uint16_t maxSeats) {
    if (wire == "versus_2v2") {
        return (maxSeats == 2) ? "1v1 Versus" : "2v2 Versus";
    }
    return CeOnlineModeLabel(wire);
}

inline uint16_t CeOnlineClampMaxSeatsForMode(CeOnlineMode mode, uint16_t seats, uint16_t cap) {
    if (cap < CE_ONLINE_UI_MIN_SLOTS) cap = CE_ONLINE_UI_MIN_SLOTS;
    if (cap > CE_ONLINE_UI_WIRE_MAX_SLOTS) cap = CE_ONLINE_UI_WIRE_MAX_SLOTS;
    if (seats < CE_ONLINE_UI_MIN_SLOTS) seats = CE_ONLINE_UI_MIN_SLOTS;
    if (seats > cap) seats = cap;
    if (mode == CE_ONLINE_MODE_VERSUS_2V2) {
        // Versus maps require even balanced teams (2 = 1v1, 4 = 2v2, etc.).
        if ((seats & 1u) != 0u) {
            if (seats + 1u <= cap) seats = static_cast<uint16_t>(seats + 1u);
            else if (seats > CE_ONLINE_UI_MIN_SLOTS) seats = static_cast<uint16_t>(seats - 1u);
            else seats = CE_ONLINE_UI_MIN_SLOTS;
        }
    }
    return seats;
}

inline void CeOnlineDefaultBeConfigsForMap(int level, std::vector<std::string> *out) {
    if (!out) return;
    out->clear();
    out->push_back("Standard");
    if (level != 856) {
        out->push_back("Sniper");
    }
    out->push_back("Laser");
    out->push_back("Blaster");
}

struct CeOnlineMapInfo {
    int level;
    const char *name;
    CeOnlineMode defaultMode;
};

static const int CE_ONLINE_MAP_COUNT = 16;

inline const CeOnlineMapInfo &CeOnlineMapAt(int index) {
    static const CeOnlineMapInfo kMaps[CE_ONLINE_MAP_COUNT] = {
        // Co-op Skirmish ONLY (retail wtype=1, 2-4 Forseti seats)
        { 850, "Skirmish 1 - Highlands",         CE_ONLINE_MODE_COOP_SKIRMISH },
        { 853, "Skirmish 2 - Canyon Pass",       CE_ONLINE_MODE_COOP_SKIRMISH },
        { 859, "Skirmish 3 - Twin Islands",      CE_ONLINE_MODE_COOP_SKIRMISH },
        { 864, "Skirmish 4 - Ridge Assault",     CE_ONLINE_MODE_COOP_SKIRMISH },
        { 865, "Skirmish 5 - Basin Outpost",     CE_ONLINE_MODE_COOP_SKIRMISH },
        { 866, "Skirmish 6 - Coastal Stronghold",CE_ONLINE_MODE_COOP_SKIRMISH },
        // Versus ONLY (retail wtype=2, 2 or 4 balanced seats: 1v1 or 2v2)
        { 851, "Versus 1 - Island Clash",        CE_ONLINE_MODE_VERSUS_2V2 },
        { 855, "Versus 2 - Volcano Arena",       CE_ONLINE_MODE_VERSUS_2V2 },
        { 860, "Versus 3 - Fortress Valley",     CE_ONLINE_MODE_VERSUS_2V2 },
        { 861, "Versus 4 - Harbor Front",        CE_ONLINE_MODE_VERSUS_2V2 },
        { 862, "Versus 5 - Desert Flats",        CE_ONLINE_MODE_VERSUS_2V2 },
        { 863, "Versus 6 - Archipelago",         CE_ONLINE_MODE_VERSUS_2V2 },
        { 852, "Versus 7 - Forward Base",        CE_ONLINE_MODE_VERSUS_2V2 },
        { 854, "Versus 8 - Valley Assault",      CE_ONLINE_MODE_VERSUS_2V2 },
        { 857, "Versus 9 - Citadel Strike",      CE_ONLINE_MODE_VERSUS_2V2 },
        // Co-op Defence ONLY (retail wtype=1, 2-4 Forseti seats, [Standard, Laser, Blaster])
        { 856, "Building Defence",               CE_ONLINE_MODE_COOP_DEFENCE },
    };
    if (index < 0 || index >= CE_ONLINE_MAP_COUNT) return kMaps[0];
    return kMaps[index];
}

inline int CeOnlineFindMapIndex(int level) {
    for (int i = 0; i < CE_ONLINE_MAP_COUNT; ++i) {
        if (CeOnlineMapAt(i).level == level) return i;
    }
    return 0;
}

inline bool CeOnlineMapIsKnown(int level) {
    for (int i = 0; i < CE_ONLINE_MAP_COUNT; ++i) {
        if (CeOnlineMapAt(i).level == level) return true;
    }
    return false;
}

inline const char *CeOnlineMapName(int level) {
    for (int i = 0; i < CE_ONLINE_MAP_COUNT; ++i) {
        if (CeOnlineMapAt(i).level == level) return CeOnlineMapAt(i).name;
    }
    return "Custom Mission";
}

inline bool CeOnlineMapSupportsMode(int level, CeOnlineMode mode) {
    for (int i = 0; i < CE_ONLINE_MAP_COUNT; ++i) {
        if (CeOnlineMapAt(i).level == level) {
            return CeOnlineMapAt(i).defaultMode == mode;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Pages & Data Views (Zero dependency on OnlineLobbyClient / OnlineProfile)
// ---------------------------------------------------------------------------
enum CeOnlinePage {
    CE_ONLINE_PAGE_HOME = 0,
    CE_ONLINE_PAGE_BROWSER,
    CE_ONLINE_PAGE_CREATE,
    CE_ONLINE_PAGE_JOIN_CODE,
    CE_ONLINE_PAGE_ROOM,
    CE_ONLINE_PAGE_PROFILE,
    CE_ONLINE_PAGE_CONNECTING
};

inline const char *CeOnlinePageTitle(CeOnlinePage page) {
    switch (page) {
    case CE_ONLINE_PAGE_HOME:       return "Play Online";
    case CE_ONLINE_PAGE_BROWSER:    return "Online Lobbies";
    case CE_ONLINE_PAGE_CREATE:     return "Create Lobby";
    case CE_ONLINE_PAGE_JOIN_CODE:  return "Join by Code";
    case CE_ONLINE_PAGE_ROOM:       return "Online Lobby";
    case CE_ONLINE_PAGE_PROFILE:    return "Online Profile";
    case CE_ONLINE_PAGE_CONNECTING: return "Connecting";
    default:                        return "Play Online";
    }
}

enum CeOnlineConnStatus {
    CE_ONLINE_CONN_OFFLINE = 0,
    CE_ONLINE_CONN_CONNECTING,
    CE_ONLINE_CONN_ONLINE,
    CE_ONLINE_CONN_ERROR
};

inline const char *CeOnlineConnStatusLabel(CeOnlineConnStatus st) {
    switch (st) {
    case CE_ONLINE_CONN_ONLINE:     return "Online";
    case CE_ONLINE_CONN_CONNECTING: return "Connecting...";
    case CE_ONLINE_CONN_ERROR:      return "Error";
    case CE_ONLINE_CONN_OFFLINE:
    default:                        return "Offline";
    }
}

struct CeOnlineProfileView {
    std::string playerId;
    std::string callsign;
    std::string tag;
    uint32_t matchesPlayed;
    uint32_t matchesWon;
    std::string serverUrl;
    CeOnlineConnStatus connStatus;
    int pingMs;
    std::string statusDetail;

    CeOnlineProfileView()
        : callsign("Pilot0000"), tag("0000"), matchesPlayed(0), matchesWon(0),
          serverUrl("Not configured"), connStatus(CE_ONLINE_CONN_OFFLINE), pingMs(-1) {}

    std::string DisplayIdentity() const {
        std::string safeName = CeOnlineSanitizeForFont(callsign.empty() ? std::string("Pilot") : callsign);
        std::string safeTag = CeOnlineSanitizeForFont(tag);
        if (safeTag.empty()) return safeName;
        return safeName + "#" + safeTag;
    }

    std::string PingLabel() const {
        if (pingMs < 0) return "--";
        char buf[24];
        snprintf(buf, sizeof(buf), "%dms", pingMs);
        return buf;
    }
};

struct CeOnlineUiRoomSummary {
    std::string roomId;
    std::string code;
    std::string name;
    std::string hostCallsign;
    std::string hostTag;
    std::string topology;
    std::string mode;
    std::string state;
    std::string buildId;
    int map;
    uint16_t usedSeats;
    uint16_t humanSeats;
    uint16_t maxSeats;
    bool hasPassword;
    bool requireRelay;
    bool compatible;

    CeOnlineUiRoomSummary()
        : topology("star_lockstep"), mode("coop_skirmish"), state("lobby"),
          map(850), usedSeats(1), humanSeats(1), maxSeats(4),
          hasPassword(false), requireRelay(false), compatible(true) {}
};

enum CeOnlineSeatKind {
    CE_ONLINE_SEAT_OPEN = 0,
    CE_ONLINE_SEAT_HUMAN,
    CE_ONLINE_SEAT_AI,
    CE_ONLINE_SEAT_CLOSED
};

inline const char *CeOnlineSeatKindWire(CeOnlineSeatKind k) {
    switch (k) {
    case CE_ONLINE_SEAT_HUMAN:  return "human";
    case CE_ONLINE_SEAT_AI:     return "ai";
    case CE_ONLINE_SEAT_CLOSED: return "closed";
    case CE_ONLINE_SEAT_OPEN:
    default:                    return "open";
    }
}

enum CeOnlineRouteKind {
    CE_ONLINE_ROUTE_UNKNOWN = 0,
    CE_ONLINE_ROUTE_DIRECT,
    CE_ONLINE_ROUTE_RELAY
};

inline const char *CeOnlineRouteLabel(CeOnlineRouteKind r) {
    switch (r) {
    case CE_ONLINE_ROUTE_DIRECT: return "P2P";
    case CE_ONLINE_ROUTE_RELAY:  return "RELAY";
    case CE_ONLINE_ROUTE_UNKNOWN:
    default:                     return "--";
    }
}

inline const char *CeOnlineSideLabel(int team) {
    return (team == 1) ? "Muspell" : "Forseti";
}

struct CeOnlineSeatView {
    uint16_t seat;
    CeOnlineSeatKind kind;
    std::string playerId;
    std::string callsign;
    std::string tag;
    int team;
    std::string beCfg;
    bool ready;
    int rttMs;
    CeOnlineRouteKind route;

    CeOnlineSeatView()
        : seat(0), kind(CE_ONLINE_SEAT_OPEN), team(0), ready(false),
          rttMs(-1), route(CE_ONLINE_ROUTE_UNKNOWN) {}
};

struct CeOnlineUiChatEntry {
    uint32_t seq;
    uint16_t seat;
    std::string playerId;
    std::string callsign;
    std::string tag;
    std::string text;
    uint64_t ts;

    CeOnlineUiChatEntry() : seq(0), seat(0), ts(0) {}
};

struct CeOnlineStartTicketView {
    bool valid;
    std::string matchId;
    uint32_t seed;
    int level;
    std::string mode;
    std::string topology;
    bool requireRelay;
    std::string rosterHash;

    CeOnlineStartTicketView()
        : valid(false), seed(0), level(850), mode("coop_skirmish"),
          topology("star_lockstep"), requireRelay(false) {}
};

struct CeOnlineRoomView {
    bool active;
    std::string roomId;
    std::string code;
    uint32_t rev;
    std::string state;
    std::string name;
    std::string topology;
    std::string mode;
    int map;
    uint16_t maxSeats;
    uint16_t hostSeat;
    uint16_t localSeat;
    std::string visibility;
    bool hasPassword;
    bool requireRelay;
    bool aiFill;
    std::string buildId;
    std::vector<CeOnlineSeatView> seats;
    std::vector<CeOnlineUiChatEntry> chat;
    CeOnlineStartTicketView startTicket;

    CeOnlineRoomView()
        : active(false), rev(0), state("lobby"), topology("star_lockstep"),
          mode("coop_skirmish"), map(850), maxSeats(4), hostSeat(0),
          localSeat(0xFFFFu), visibility("public"), hasPassword(false),
          requireRelay(false), aiFill(true) {}
};

struct CeOnlineCreateParams {
    std::string name;
    std::string mode;
    int map;
    uint16_t maxSeats;
    std::string topology;
    std::string visibility;
    std::string password;
    bool requireRelay;
    bool aiFill;
    std::string beCfg;

    CeOnlineCreateParams()
        : mode("coop_skirmish"), map(850), maxSeats(CE_ONLINE_UI_ACTIVE_MAX_SLOTS),
          topology("star_lockstep"), visibility("public"),
          requireRelay(false), aiFill(true) {}
};

struct CeOnlineHostPatch {
    bool hasMap;
    int map;
    bool hasMode;
    std::string mode;
    bool hasMaxSeats;
    uint16_t maxSeats;
    bool hasRequireRelay;
    bool requireRelay;
    bool hasAiFill;
    bool aiFill;
    bool hasSeatKind;
    uint16_t targetSeat;
    CeOnlineSeatKind seatKind;
    bool hasKickSeat;
    uint16_t kickSeat;

    CeOnlineHostPatch()
        : hasMap(false), map(850), hasMode(false), hasMaxSeats(false), maxSeats(4),
          hasRequireRelay(false), requireRelay(false), hasAiFill(false), aiFill(true),
          hasSeatKind(false), targetSeat(0), seatKind(CE_ONLINE_SEAT_OPEN),
          hasKickSeat(false), kickSeat(0) {}
};

struct CeOnlineUiResolveResult {
    bool resolved;
    bool found;
    std::string roomId;
    bool hasPassword;
    std::string state;
    std::string error;

    CeOnlineUiResolveResult() : resolved(false), found(false), hasPassword(false) {}
};

// ---------------------------------------------------------------------------
// Abstract Backend Interface (zero dependency on OnlineLobbyClient/Profile)
// ---------------------------------------------------------------------------
class ICeOnlineUiBackend {
public:
    virtual ~ICeOnlineUiBackend() {}

    virtual void Poll(uint64_t nowMs) = 0;

    virtual bool GetProfile(CeOnlineProfileView *out) = 0;
    virtual bool SaveCallsign(const std::string &callsign, std::string *errOut) = 0;
    virtual void RequestReconnect() = 0;

    virtual void RequestRefreshLobbies(bool includeIncompatible) = 0;
    virtual bool GetLobbies(std::vector<CeOnlineUiRoomSummary> *outRooms,
                            bool *loadingOut, std::string *errOut) = 0;

    virtual bool RequestCreateRoom(const CeOnlineCreateParams &params, std::string *errOut) = 0;
    virtual bool RequestResolveCode(const std::string &code, std::string *errOut) = 0;
    virtual bool TakeResolveResult(CeOnlineUiResolveResult *out) {
        (void)out;
        return false;
    }
    virtual bool RequestJoinRoom(const std::string &roomId, const std::string &password,
                                 const std::string &beCfg, std::string *errOut) = 0;
    virtual void RequestLeaveRoom() = 0;

    virtual bool GetRoom(CeOnlineRoomView *outRoom) = 0;
    virtual bool RequestUpdateSelf(int team, const std::string &beCfg, bool ready,
                                   std::string *errOut) = 0;
    virtual bool RequestHostUpdate(const CeOnlineHostPatch &patch, std::string *errOut) = 0;
    virtual bool RequestSendChat(const std::string &text, std::string *errOut) = 0;
    virtual bool RequestStartMatch(std::string *errOut) = 0;

    virtual bool CopyToClipboard(const std::string &text) { (void)text; return false; }
    virtual bool PasteFromClipboard(std::string *out) { (void)out; return false; }
    virtual void GetBeConfigsForMap(int level, std::vector<std::string> *out) {
        CeOnlineDefaultBeConfigsForMap(level, out);
    }
    virtual void OnOpenDirectLan() {}
    virtual void OnMatchTicketReady(const CeOnlineStartTicketView &ticket) { (void)ticket; }
    virtual bool TakeOpError(std::string *out) { (void)out; return false; }
    virtual void FlushLeave(uint64_t nowMs) { (void)nowMs; }
};

// ---------------------------------------------------------------------------
// 640x480 Safe-Band Geometry Primitives & Per-Page Layouts
// ---------------------------------------------------------------------------
struct CeOnlineRect {
    float x0, y0, x1, y1;

    bool Contains(float x, float y) const {
        return x >= x0 && x < x1 && y >= y0 && y < y1;
    }
    bool InSafeBand() const {
        return x0 >= CE_ONLINE_SAFE_X0 && x1 <= CE_ONLINE_SAFE_X1 &&
               y0 >= CE_ONLINE_SAFE_Y0 && y1 <= CE_ONLINE_SAFE_Y1 &&
               x0 < x1 && y0 < y1;
    }
    static bool Overlaps(const CeOnlineRect &a, const CeOnlineRect &b) {
        return (a.x0 < b.x1) && (b.x0 < a.x1) && (a.y0 < b.y1) && (b.y0 < a.y1);
    }
};

struct CeOnlineLayout {
    // Shared header & footer bands on every screen
    static CeOnlineRect HeaderBar() { return { 84.f, 106.f, 556.f, 126.f }; }
    static CeOnlineRect FooterBar() { return { 84.f, 424.f, 556.f, 446.f }; }

    // Home page: 6 stacked menu rows
    static const int kHomeRowCount = 6;
    static CeOnlineRect HomeRow(int idx) {
        const float y0 = 172.f + float(idx) * 32.f;
        return { 150.f, y0, 490.f, y0 + 26.f };
    }

    // Browser page: toolbar (top) + room list panel (6 rows) + pager + bottom bar
    static CeOnlineRect BrowserFilterBtn()   { return { 84.f,  132.f, 236.f, 152.f }; }
    static CeOnlineRect BrowserRefreshBtn()  { return { 242.f, 132.f, 320.f, 152.f }; }
    static CeOnlineRect BrowserCreateBtn()   { return { 326.f, 132.f, 438.f, 152.f }; }
    static CeOnlineRect BrowserJoinCodeBtn() { return { 444.f, 132.f, 556.f, 152.f }; }
    static CeOnlineRect BrowserTableHeader() { return { 84.f,  158.f, 556.f, 176.f }; }
    static CeOnlineRect BrowserRow(int visIdx) {
        const float y0 = 178.f + float(visIdx) * 28.f;
        return { 84.f, y0, 556.f, y0 + 25.f };
    }
    static CeOnlineRect BrowserPrevBtn()     { return { 84.f,  352.f, 164.f, 372.f }; }
    static CeOnlineRect BrowserPageBadge()   { return { 170.f, 352.f, 286.f, 372.f }; }
    static CeOnlineRect BrowserNextBtn()     { return { 292.f, 352.f, 372.f, 372.f }; }
    static CeOnlineRect BrowserJoinBtn()     { return { 378.f, 352.f, 556.f, 372.f }; }
    static CeOnlineRect BrowserProfileBtn()  { return { 84.f,  378.f, 228.f, 398.f }; }
    static CeOnlineRect BrowserLanBtn()      { return { 234.f, 378.f, 406.f, 398.f }; }
    static CeOnlineRect BrowserBackBtn()     { return { 412.f, 378.f, 556.f, 398.f }; }
    static CeOnlineRect BrowserPwdModal()    { return { 130.f, 206.f, 510.f, 314.f }; }
    static CeOnlineRect BrowserPwdSubmit()   { return { 150.f, 282.f, 310.f, 304.f }; }
    static CeOnlineRect BrowserPwdCancel()   { return { 330.f, 282.f, 490.f, 304.f }; }

    // Create Lobby page: 8 config rows + privacy notice + Create/Back
    static const int kCreateFieldCount = 8;
    static CeOnlineRect CreateFieldRow(int idx) {
        const float y0 = 132.f + float(idx) * 26.f;
        return { 84.f, y0, 556.f, y0 + 22.f };
    }
    static CeOnlineRect CreatePrivacyBox()   { return { 84.f, 344.f, 556.f, 380.f }; }
    static CeOnlineRect CreateSubmitBtn()    { return { 120.f, 388.f, 312.f, 414.f }; }
    static CeOnlineRect CreateBackBtn()      { return { 328.f, 388.f, 520.f, 414.f }; }

    // Join by Code page
    static CeOnlineRect JoinCodeField()      { return { 120.f, 178.f, 520.f, 206.f }; }
    static CeOnlineRect JoinPwdField()       { return { 120.f, 218.f, 520.f, 246.f }; }
    static CeOnlineRect JoinPasteBtn()       { return { 120.f, 266.f, 310.f, 294.f }; }
    static CeOnlineRect JoinSubmitBtn()      { return { 330.f, 266.f, 520.f, 294.f }; }
    static CeOnlineRect JoinBackBtn()        { return { 220.f, 312.f, 420.f, 340.f }; }

    // Room page: 2-line room+map header + 4-row paginated roster + player/host controls + 6-line chat + compose
    static CeOnlineRect RoomInfoBar()        { return { 84.f,  128.f, 464.f, 146.f }; }
    static CeOnlineRect RoomCopyCodeBtn()    { return { 468.f, 128.f, 556.f, 146.f }; }
    static CeOnlineRect RoomMapBar()         { return { 84.f,  148.f, 556.f, 166.f }; }
    static CeOnlineRect RoomRosterHeader()   { return { 84.f,  168.f, 556.f, 184.f }; }
    static CeOnlineRect RoomFooterBar()      { return { 84.f,  424.f, 508.f, 446.f }; }
    static CeOnlineRect RoomPagePrevBtn()    { return { 512.f, 424.f, 532.f, 446.f }; }
    static CeOnlineRect RoomPageNextBtn()    { return { 536.f, 424.f, 556.f, 446.f }; }
    static CeOnlineRect RoomSeatRow(int visIdx) {
        const float y0 = 186.f + float(visIdx) * 19.f;
        return { 84.f, y0, 556.f, y0 + 18.f };
    }
    // Player action row (y = 264..284)
    static CeOnlineRect RoomReadyBtn()       { return { 84.f,  264.f, 172.f, 284.f }; }
    static CeOnlineRect RoomSideBtn()        { return { 176.f, 264.f, 274.f, 284.f }; }
    static CeOnlineRect RoomBeBtn()          { return { 278.f, 264.f, 392.f, 284.f }; }
    static CeOnlineRect RoomMuteBtn()        { return { 396.f, 264.f, 474.f, 284.f }; }
    static CeOnlineRect RoomLeaveBtn()       { return { 478.f, 264.f, 556.f, 284.f }; }
    // Host action row (y = 288..308)
    static CeOnlineRect RoomMapBtn()         { return { 84.f,  288.f, 172.f, 308.f }; }
    static CeOnlineRect RoomModeBtn()        { return { 176.f, 288.f, 274.f, 308.f }; }
    static CeOnlineRect RoomSlotBtn()        { return { 278.f, 288.f, 392.f, 308.f }; }
    static CeOnlineRect RoomKickBtn()        { return { 396.f, 288.f, 450.f, 308.f }; }
    static CeOnlineRect RoomStartBtn()       { return { 454.f, 288.f, 556.f, 308.f }; }
    // Chat box & compose bar (y = 312..420)
    static CeOnlineRect RoomChatLog()        { return { 84.f,  312.f, 556.f, 398.f }; }
    static CeOnlineRect RoomChatCompose()    { return { 84.f,  400.f, 486.f, 420.f }; }
    static CeOnlineRect RoomChatSendBtn()    { return { 490.f, 400.f, 556.f, 420.f }; }

    // Profile page
    static CeOnlineRect ProfileCallsignBox() { return { 110.f, 152.f, 530.f, 180.f }; }
    static CeOnlineRect ProfileStatsPanel()  { return { 110.f, 192.f, 530.f, 316.f }; }
    static CeOnlineRect ProfileSaveBtn()     { return { 110.f, 334.f, 244.f, 362.f }; }
    static CeOnlineRect ProfileReconnectBtn(){ return { 254.f, 334.f, 394.f, 362.f }; }
    static CeOnlineRect ProfileBackBtn()     { return { 404.f, 334.f, 530.f, 362.f }; }

    // Connecting page
    static CeOnlineRect ConnectingPanel()    { return { 120.f, 182.f, 520.f, 310.f }; }
    static CeOnlineRect ConnectingCancelBtn(){ return { 220.f, 332.f, 420.f, 360.f }; }

    // Multiplayer entry bar (drawn on FEP_MULTIPLAYER, clear of grid y=240..400 and help y=455)
    static CeOnlineRect MultiplayerEntryBar(){ return { 125.f, 412.f, 515.f, 442.f }; }
};

// ---------------------------------------------------------------------------
// Complete Online UI State Machine
// ---------------------------------------------------------------------------
class CeOnlineUiModel {
public:
    enum HomeAction {
        HOME_BROWSE = 0,
        HOME_CREATE = 1,
        HOME_JOIN_CODE = 2,
        HOME_PROFILE = 3,
        HOME_DIRECT_LAN = 4,
        HOME_BACK = 5
    };

    enum CreateField {
        CREATE_NAME = 0,
        CREATE_MAP = 1,
        CREATE_MODE = 2,
        CREATE_MAX_PLAYERS = 3,
        CREATE_VISIBILITY = 4,
        CREATE_PASSWORD = 5,
        CREATE_RELAY_ONLY = 6,
        CREATE_AI_FILL = 7,
        CREATE_SUBMIT = 8,
        CREATE_BACK = 9,
        CREATE_FOCUS_COUNT = 10
    };

    enum JoinCodeField {
        JOIN_CODE_INPUT = 0,
        JOIN_PASSWORD_INPUT = 1,
        JOIN_PASTE_BTN = 2,
        JOIN_SUBMIT_BTN = 3,
        JOIN_BACK_BTN = 4,
        JOIN_FOCUS_COUNT = 5
    };

    enum ProfileField {
        PROFILE_CALLSIGN_INPUT = 0,
        PROFILE_SAVE_BTN = 1,
        PROFILE_RECONNECT_BTN = 2,
        PROFILE_BACK_BTN = 3,
        PROFILE_FOCUS_COUNT = 4
    };

    enum BrowserZone {
        BROWSER_ZONE_TOOLBAR = 0,
        BROWSER_ZONE_LIST = 1,
        BROWSER_ZONE_PAGER = 2,
        BROWSER_ZONE_FOOTER = 3
    };

    enum BrowserToolbarItem {
        BROWSER_TB_FILTER = 0,
        BROWSER_TB_REFRESH = 1,
        BROWSER_TB_CREATE = 2,
        BROWSER_TB_JOIN_CODE = 3,
        BROWSER_TB_COUNT = 4
    };

    enum BrowserPagerItem {
        BROWSER_PG_PREV = 0,
        BROWSER_PG_NEXT = 1,
        BROWSER_PG_JOIN = 2,
        BROWSER_PG_COUNT = 3
    };

    enum BrowserFooterItem {
        BROWSER_FT_PROFILE = 0,
        BROWSER_FT_LAN = 1,
        BROWSER_FT_BACK = 2,
        BROWSER_FT_COUNT = 3
    };

    enum RoomZone {
        ROOM_ZONE_HEADER = 0,
        ROOM_ZONE_ROSTER = 1,
        ROOM_ZONE_PLAYER = 2,
        ROOM_ZONE_HOST = 3,
        ROOM_ZONE_CHAT = 4
    };

    enum RoomHeaderItem {
        ROOM_HD_COPY = 0,
        ROOM_HD_PREV = 1,
        ROOM_HD_NEXT = 2,
        ROOM_HD_COUNT = 3
    };

    enum RoomPlayerItem {
        ROOM_PL_READY = 0,
        ROOM_PL_SIDE = 1,
        ROOM_PL_BE = 2,
        ROOM_PL_MUTE = 3,
        ROOM_PL_LEAVE = 4,
        ROOM_PL_COUNT = 5
    };

    enum RoomHostItem {
        ROOM_HS_MAP = 0,
        ROOM_HS_MODE = 1,
        ROOM_HS_SLOT = 2,
        ROOM_HS_KICK = 3,
        ROOM_HS_START = 4,
        ROOM_HS_COUNT = 5
    };

    CeOnlineUiModel()
        : mBackend(NULL),
          mActive(false),
          mReturnToMultiplayer(false),
          mRequestedClose(false),
          mRequestedDirectLan(false),
          mPage(CE_ONLINE_PAGE_HOME),
          mFormReturnPage(CE_ONLINE_PAGE_HOME),
          mConnectReturnPage(CE_ONLINE_PAGE_HOME),
          mHomeFocus(0),
          mMaxSeatsCap(CE_ONLINE_UI_ACTIVE_MAX_SLOTS),
          mStatusIsError(false),
          mBrowserLoading(false),
          mCompatibleOnly(true),
          mBrowserSelected(0),
          mBrowserPageOffset(0),
          mBrowserZone(BROWSER_ZONE_LIST),
          mBrowserToolbarFocus(BROWSER_TB_CREATE),
          mBrowserPagerFocus(BROWSER_PG_JOIN),
          mBrowserFooterFocus(BROWSER_FT_BACK),
          mPasswordPromptOpen(false),
          mPasswordPromptMap(850),
          mPasswordModalFocus(0),
          mCreateMapIndex(0),
          mCreateModeIndex(static_cast<int>(CeOnlineMapAt(0).defaultMode)),
          mCreateMaxSeats(CE_ONLINE_UI_ACTIVE_MAX_SLOTS),
          mCreatePublic(true),
          mCreateRelayOnly(false),
          mCreateAiFill(true),
          mCreateFocus(0),
          mJoinCodeFocus(0),
          mJoinPendingResolve(false),
          mRosterSelected(0),
          mRosterPageOffset(0),
          mRoomZone(ROOM_ZONE_PLAYER),
          mRoomHeaderFocus(ROOM_HD_COPY),
          mRoomPlayerFocus(ROOM_PL_READY),
          mRoomHostFocus(ROOM_HS_START),
          mChatComposing(false),
          mProfileFocus(0),
          mConnectingCancellable(true),
          mPendingStartMatch(false),
          mLastRoomMap(-1),
          mLastNowMs(0),
          mHasOptimisticMap(false),
          mOptimisticMapIndex(0),
          mOptimisticMapLevel(850),
          mOptimisticMaxSeats(CE_ONLINE_UI_ACTIVE_MAX_SLOTS),
          mOptimisticBaseRev(0),
          mOptimisticSetMs(0) {
        CeOnlineDefaultBeConfigsForMap(CeOnlineMapAt(0).level, &mAvailableBeConfigs);
    }

    void SetBackend(ICeOnlineUiBackend *backend) {
        mBackend = backend;
        RefreshProfileFromBackend();
    }
    ICeOnlineUiBackend *Backend() const { return mBackend; }

    // Parameterizes maximum selectable seats in Create Lobby (4 for 0.1.14, up to 64 for Assault).
    void SetMaxSeatsCap(uint16_t cap) {
        if (cap < CE_ONLINE_UI_MIN_SLOTS) cap = CE_ONLINE_UI_MIN_SLOTS;
        if (cap > CE_ONLINE_UI_WIRE_MAX_SLOTS) cap = CE_ONLINE_UI_WIRE_MAX_SLOTS;
        mMaxSeatsCap = cap;
        mCreateMaxSeats = CeOnlineClampMaxSeatsForMode(
            static_cast<CeOnlineMode>(mCreateModeIndex), mCreateMaxSeats, mMaxSeatsCap);
    }
    uint16_t MaxSeatsCap() const { return mMaxSeatsCap; }

    void Open(bool fromMultiplayer) {
        mActive = true;
        mReturnToMultiplayer = fromMultiplayer;
        mRequestedClose = false;
        mRequestedDirectLan = false;
        mStatusNotice.clear();
        mStatusIsError = false;
        mBrowserFetchError.clear();
        mPasswordPromptOpen = false;
        mChatComposing = false;
        mChatDraft.clear();
        mJoinPendingResolve = false;
        mPendingStartMatch = false;
        mHomeFocus = 0;
        RefreshProfileFromBackend();
        if (fromMultiplayer) {
            EnterBrowser(CE_ONLINE_PAGE_HOME);
        } else {
            mPage = CE_ONLINE_PAGE_HOME;
        }
    }

    void Close(bool preserveLaunch = false) {
        const bool launching = preserveLaunch ||
                               mRoom.state == "starting" ||
                               mRoom.state == "in_match" ||
                               (mRoom.startTicket.valid && !mLastTicketMatchId.empty());
        if (mRoom.active && mBackend && !launching) {
            mBackend->RequestLeaveRoom();
            mBackend->FlushLeave(mLastNowMs);
        }
        mRoom = CeOnlineRoomView();
        mActive = false;
        mPasswordPromptOpen = false;
        mChatComposing = false;
        mChatDraft.clear();
        mJoinPendingResolve = false;
        mPendingStartMatch = false;
        mLastRoomMap = -1;
        ClearOptimisticRoomState();
        mLastTicketMatchId.clear();
    }

    bool Active() const { return mActive; }
    bool ReturnToMultiplayer() const { return mReturnToMultiplayer; }
    bool TakeRequestedClose() {
        bool v = mRequestedClose;
        mRequestedClose = false;
        return v;
    }
    bool TakeRequestedDirectLan() {
        bool v = mRequestedDirectLan;
        mRequestedDirectLan = false;
        return v;
    }

    CeOnlinePage Page() const { return mPage; }
    const CeOnlineProfileView &Profile() const { return mProfile; }
    const CeOnlineRoomView &Room() const { return mRoom; }
    const std::string &StatusNotice() const {
        return mPage == CE_ONLINE_PAGE_BROWSER && mStatusNotice.empty()
            ? mBrowserFetchError : mStatusNotice;
    }
    bool StatusIsError() const {
        return mStatusNotice.empty() ? !StatusNotice().empty() : mStatusIsError;
    }
    void SetStatusNotice(const std::string &msg, bool isError = false) {
        mStatusNotice = CeOnlineSanitizeForFont(msg);
        mStatusIsError = isError;
    }

    // -----------------------------------------------------------------------
    // Periodic Backend Sync
    // -----------------------------------------------------------------------
    void SyncFromBackend(uint64_t nowMs) {
        if (!mActive || !mBackend) return;
        mLastNowMs = nowMs;
        mBackend->Poll(nowMs);
        RefreshProfileFromBackend();

        std::string opErr;
        if (mBackend->TakeOpError(&opErr) && !opErr.empty()) {
            ClearOptimisticRoomState();
            if (mPage == CE_ONLINE_PAGE_CONNECTING) {
                mJoinPendingResolve = false;
                mPendingStartMatch = false;
                mConnectingCancellable = true;
                mLastTicketMatchId.clear();
                mPage = (mConnectReturnPage == CE_ONLINE_PAGE_CONNECTING) ? CE_ONLINE_PAGE_BROWSER : mConnectReturnPage;
                if (mPage == CE_ONLINE_PAGE_ROOM && !mRoom.active) {
                    mPage = CE_ONLINE_PAGE_BROWSER;
                }
            }
            SetStatusNotice(opErr, true);
        }

        if (mPage == CE_ONLINE_PAGE_BROWSER) {
            UpdateBrowserResults();
        }

        if (mJoinPendingResolve) {
            CeOnlineUiResolveResult res;
            if (mBackend->TakeResolveResult(&res) && res.resolved) {
                mJoinPendingResolve = false;
                if (!res.found) {
                    mPage = CE_ONLINE_PAGE_JOIN_CODE;
                    SetStatusNotice(res.error.empty() ? "No lobby found for that code." : res.error, true);
                } else if (res.hasPassword && mJoinPassword.empty()) {
                    mPage = CE_ONLINE_PAGE_JOIN_CODE;
                    mJoinCodeFocus = JOIN_PASSWORD_INPUT;
                    SetStatusNotice("This lobby requires a password. Enter password and choose Join.", false);
                } else {
                    std::string err;
                    if (mBackend->RequestJoinRoom(res.roomId, mJoinPassword, "", &err)) {
                        BeginConnecting("Joining Lobby", "Connecting to lobby...", true);
                    } else {
                        mPage = CE_ONLINE_PAGE_JOIN_CODE;
                        SetStatusNotice(err.empty() ? "Could not join lobby." : err, true);
                    }
                }
            }
        }

        if (mPage == CE_ONLINE_PAGE_ROOM || mPage == CE_ONLINE_PAGE_CONNECTING) {
            CeOnlineRoomView latestRoom;
            const bool gotRoom = mBackend->GetRoom(&latestRoom);
            if (gotRoom && latestRoom.active) {
                const bool closed = (latestRoom.state == "closed");
                if (closed) {
                    mRoom = CeOnlineRoomView();
                    mChatComposing = false;
                    mChatDraft.clear();
                    mLastRoomMap = -1;
                    ClearOptimisticRoomState();
                    mLastTicketMatchId.clear();
                    EnterBrowser(CE_ONLINE_PAGE_HOME);
                    SetStatusNotice("The lobby was closed.", false);
                    return;
                }
                if (latestRoom.chat.size() > CE_ONLINE_UI_CHAT_RING) {
                    latestRoom.chat.erase(
                        latestRoom.chat.begin(),
                        latestRoom.chat.begin() + static_cast<ptrdiff_t>(latestRoom.chat.size() - CE_ONLINE_UI_CHAT_RING));
                }
                mRoom = latestRoom;
                if (mHasOptimisticMap) {
                    if (!IsLocalHost() ||
                        (mRoom.map == mOptimisticMapLevel &&
                         mRoom.mode == mOptimisticMode &&
                         mRoom.maxSeats == mOptimisticMaxSeats) ||
                        (nowMs >= mOptimisticSetMs + 4000u)) {
                        ClearOptimisticRoomState();
                    }
                }
                if (mRoom.map != mLastRoomMap) {
                    RefreshBeConfigsForMap(mRoom.map);
                    RevalidateLocalBeConfig();
                }
                ClampRosterSelection();

                if ((mRoom.state == "starting" || mRoom.state == "in_match") && mRoom.startTicket.valid) {
                    mPendingStartMatch = false;
                    ClearOptimisticRoomState();
                    if (mLastTicketMatchId != mRoom.startTicket.matchId) {
                        mLastTicketMatchId = mRoom.startTicket.matchId;
                        mBackend->OnMatchTicketReady(mRoom.startTicket);
                    }
                    mPage = CE_ONLINE_PAGE_CONNECTING;
                    mConnectingHeading = "Starting Match";
                    mConnectingDetail = mRoom.startTicket.requireRelay
                        ? "Connecting all players via TURN Relay..."
                        : "Establishing direct P2P / TURN match links...";
                    mConnectingCancellable = false;
                } else if (mPage == CE_ONLINE_PAGE_CONNECTING && mRoom.state == "lobby" && !mPendingStartMatch) {
                    mPage = CE_ONLINE_PAGE_ROOM;
                    mRoomZone = ROOM_ZONE_PLAYER;
                    mRoomPlayerFocus = ROOM_PL_READY;
                    mChatComposing = false;
                    ClearOptimisticRoomState();
                    SelectDefaultRosterSlot();
                    if (opErr.empty()) {
                        mStatusNotice.clear();
                        mStatusIsError = false;
                    }
                }
                if (!IsLocalHost() && mRoomZone == ROOM_ZONE_HOST) {
                    mRoomZone = ROOM_ZONE_PLAYER;
                }
                NormalizeRoomFocus();
            } else if (mPage == CE_ONLINE_PAGE_ROOM) {
                mRoom = CeOnlineRoomView();
                mChatComposing = false;
                mChatDraft.clear();
                mPendingStartMatch = false;
                mLastRoomMap = -1;
                ClearOptimisticRoomState();
                EnterBrowser(CE_ONLINE_PAGE_HOME);
                SetStatusNotice(opErr.empty() ? "Disconnected from lobby." : opErr, true);
            } else if (mPage == CE_ONLINE_PAGE_CONNECTING && !mConnectingCancellable &&
                       mLastTicketMatchId.empty()) {
                mRoom = CeOnlineRoomView();
                mPendingStartMatch = false;
                mConnectingCancellable = true;
                mLastRoomMap = -1;
                ClearOptimisticRoomState();
                EnterBrowser(CE_ONLINE_PAGE_HOME);
                SetStatusNotice(opErr.empty() ? "Match launch could not complete." : opErr, true);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Home Page Actions
    // -----------------------------------------------------------------------
    int HomeFocus() const { return mHomeFocus; }
    void SetHomeFocus(int idx) {
        if (idx < 0) idx = CeOnlineLayout::kHomeRowCount - 1;
        if (idx >= CeOnlineLayout::kHomeRowCount) idx = 0;
        mHomeFocus = idx;
    }
    void HomeMove(int delta) { SetHomeFocus(mHomeFocus + delta); }

    static const char *HomeRowLabel(int idx) {
        switch (idx) {
        case HOME_BROWSE:     return "Browse Lobbies";
        case HOME_CREATE:     return "Create Lobby";
        case HOME_JOIN_CODE:  return "Join by Code";
        case HOME_PROFILE:    return "Online Profile";
        case HOME_DIRECT_LAN: return "Direct LAN / IP";
        case HOME_BACK:       return "Back";
        default:              return "";
        }
    }

    static const char *HomeRowHint(int idx) {
        switch (idx) {
        case HOME_BROWSE:     return "Browse public 2-4 player lobbies and join.";
        case HOME_CREATE:     return "Host a new public or unlisted online lobby.";
        case HOME_JOIN_CODE:  return "Enter a 6-character code to join a lobby.";
        case HOME_PROFILE:    return "Edit your callsign and check server status.";
        case HOME_DIRECT_LAN: return "Host or join directly by LAN or IP.";
        case HOME_BACK:       return "Return to the previous menu.";
        default:              return "";
        }
    }

    void HomeActivate(int idx) {
        SetHomeFocus(idx);
        switch (idx) {
        case HOME_BROWSE:     EnterBrowser(CE_ONLINE_PAGE_HOME); break;
        case HOME_CREATE:     EnterCreate(CE_ONLINE_PAGE_HOME); break;
        case HOME_JOIN_CODE:  EnterJoinCode(CE_ONLINE_PAGE_HOME); break;
        case HOME_PROFILE:    EnterProfile(CE_ONLINE_PAGE_HOME); break;
        case HOME_DIRECT_LAN: TriggerDirectLan(); break;
        case HOME_BACK:       NavigateBack(); break;
        default: break;
        }
    }

    // -----------------------------------------------------------------------
    // Browser Page State & Pagination (supports 20..64+ rooms)
    // -----------------------------------------------------------------------
    void EnterBrowser(CeOnlinePage returnTo) {
        mFormReturnPage = returnTo;
        mPendingStartMatch = false;
        mPage = CE_ONLINE_PAGE_BROWSER;
        mPasswordPromptOpen = false;
        mPasswordPromptInput.clear();
        mPasswordModalFocus = 0;
        mStatusNotice.clear();
        mStatusIsError = false;
        BrowserRefresh();
        mBrowserZone = FilteredRooms().empty() ? BROWSER_ZONE_TOOLBAR : BROWSER_ZONE_LIST;
        mBrowserToolbarFocus = BROWSER_TB_CREATE;
        mBrowserPagerFocus = BROWSER_PG_JOIN;
        mBrowserFooterFocus = BROWSER_FT_BACK;
    }

    void BrowserRefresh() {
        mBrowserLoading = true;
        if (mBackend) {
            mBackend->RequestRefreshLobbies(!mCompatibleOnly);
            UpdateBrowserResults();
        }
    }

    bool BrowserLoading() const { return mBrowserLoading; }
    bool CompatibleOnly() const { return mCompatibleOnly; }

    void BrowserToggleCompatibleFilter() {
        mCompatibleOnly = !mCompatibleOnly;
        mBrowserSelected = 0;
        mBrowserPageOffset = 0;
        BrowserRefresh();
        ClampBrowserSelection();
        NormalizeBrowserPagerFocus();
    }

    std::vector<CeOnlineUiRoomSummary> FilteredRooms() const {
        std::vector<CeOnlineUiRoomSummary> out;
        for (size_t i = 0; i < mRooms.size(); ++i) {
            if (mCompatibleOnly && !mRooms[i].compatible) continue;
            out.push_back(mRooms[i]);
        }
        return out;
    }

    int BrowserSelectedIndex() const { return mBrowserSelected; }
    int BrowserPageOffset() const { return mBrowserPageOffset; }

    int BrowserPageCount() const {
        const int n = static_cast<int>(FilteredRooms().size());
        if (n <= 0) return 1;
        return (n + CE_ONLINE_UI_BROWSER_PAGE_SIZE - 1) / CE_ONLINE_UI_BROWSER_PAGE_SIZE;
    }

    int BrowserCurrentPage() const {
        return (mBrowserPageOffset / CE_ONLINE_UI_BROWSER_PAGE_SIZE) + 1;
    }

    void BrowserSelectIndex(int filteredIdx) {
        const int n = static_cast<int>(FilteredRooms().size());
        if (n <= 0) {
            mBrowserSelected = 0;
            mBrowserPageOffset = 0;
            return;
        }
        if (filteredIdx < 0) filteredIdx = 0;
        if (filteredIdx >= n) filteredIdx = n - 1;
        mBrowserSelected = filteredIdx;
        mBrowserPageOffset = (mBrowserSelected / CE_ONLINE_UI_BROWSER_PAGE_SIZE) * CE_ONLINE_UI_BROWSER_PAGE_SIZE;
    }

    void BrowserMoveSelection(int delta) {
        const int n = static_cast<int>(FilteredRooms().size());
        if (n <= 0) return;
        int next = mBrowserSelected + delta;
        if (next < 0) next = 0;
        if (next >= n) next = n - 1;
        BrowserSelectIndex(next);
    }

    void BrowserNextPage() {
        const int n = static_cast<int>(FilteredRooms().size());
        if (mBrowserPageOffset + CE_ONLINE_UI_BROWSER_PAGE_SIZE < n) {
            mBrowserPageOffset += CE_ONLINE_UI_BROWSER_PAGE_SIZE;
            mBrowserSelected = mBrowserPageOffset;
            NormalizeBrowserPagerFocus();
        }
    }

    void BrowserPrevPage() {
        if (mBrowserPageOffset >= CE_ONLINE_UI_BROWSER_PAGE_SIZE) {
            mBrowserPageOffset -= CE_ONLINE_UI_BROWSER_PAGE_SIZE;
            mBrowserSelected = mBrowserPageOffset;
            NormalizeBrowserPagerFocus();
        }
    }

    bool PasswordPromptOpen() const { return mPasswordPromptOpen; }
    const std::string &PasswordPromptRoomName() const { return mPasswordPromptRoomName; }
    const std::string &PasswordPromptInput() const { return mPasswordPromptInput; }

    int BrowserZoneFocus() const { return mBrowserZone; }
    void SetBrowserZoneFocus(int zone) {
        if (zone < BROWSER_ZONE_TOOLBAR) zone = BROWSER_ZONE_TOOLBAR;
        if (zone > BROWSER_ZONE_FOOTER) zone = BROWSER_ZONE_FOOTER;
        mBrowserZone = zone;
    }
    int BrowserToolbarFocus() const { return mBrowserToolbarFocus; }
    void SetBrowserToolbarFocus(int idx) {
        mBrowserZone = BROWSER_ZONE_TOOLBAR;
        mBrowserToolbarFocus = (idx % BROWSER_TB_COUNT + BROWSER_TB_COUNT) % BROWSER_TB_COUNT;
    }
    int BrowserPagerFocus() const { return mBrowserPagerFocus; }
    void SetBrowserPagerFocus(int idx) {
        mBrowserZone = BROWSER_ZONE_PAGER;
        mBrowserPagerFocus = (idx % BROWSER_PG_COUNT + BROWSER_PG_COUNT) % BROWSER_PG_COUNT;
    }
    int BrowserFooterFocus() const { return mBrowserFooterFocus; }
    void SetBrowserFooterFocus(int idx) {
        mBrowserZone = BROWSER_ZONE_FOOTER;
        mBrowserFooterFocus = (idx % BROWSER_FT_COUNT + BROWSER_FT_COUNT) % BROWSER_FT_COUNT;
    }
    int PasswordModalFocus() const { return mPasswordModalFocus; }
    void SetPasswordModalFocus(int idx) { mPasswordModalFocus = (idx != 0) ? 1 : 0; }

    bool BrowserPagerButtonEnabled(int idx) const {
        if (idx == BROWSER_PG_PREV) return BrowserCurrentPage() > 1;
        if (idx == BROWSER_PG_NEXT) return BrowserCurrentPage() < BrowserPageCount();
        if (idx == BROWSER_PG_JOIN) return !FilteredRooms().empty();
        return false;
    }

    bool BrowserHasEnabledPagerButton() const {
        for (int i = 0; i < BROWSER_PG_COUNT; ++i) {
            if (BrowserPagerButtonEnabled(i)) return true;
        }
        return false;
    }

    void NormalizeBrowserPagerFocus() {
        if (BrowserPagerButtonEnabled(mBrowserPagerFocus)) return;
        if (BrowserPagerButtonEnabled(BROWSER_PG_JOIN)) {
            mBrowserPagerFocus = BROWSER_PG_JOIN;
            return;
        }
        for (int i = 0; i < BROWSER_PG_COUNT; ++i) {
            if (BrowserPagerButtonEnabled(i)) {
                mBrowserPagerFocus = i;
                return;
            }
        }
        if (mBrowserZone == BROWSER_ZONE_PAGER || mBrowserZone == BROWSER_ZONE_LIST) {
            mBrowserZone = BROWSER_ZONE_TOOLBAR;
        }
    }

    void BrowserNavMove(int delta) {
        if (mPasswordPromptOpen) {
            mPasswordModalFocus = (mPasswordModalFocus == 0) ? 1 : 0;
            return;
        }
        const int n = static_cast<int>(FilteredRooms().size());
        const bool hasPager = BrowserHasEnabledPagerButton();
        if (delta < 0) {
            if (mBrowserZone == BROWSER_ZONE_LIST) {
                if (n > 0 && mBrowserSelected > 0) BrowserMoveSelection(-1);
                else mBrowserZone = BROWSER_ZONE_TOOLBAR;
            } else if (mBrowserZone == BROWSER_ZONE_TOOLBAR) {
                mBrowserZone = BROWSER_ZONE_FOOTER;
            } else if (mBrowserZone == BROWSER_ZONE_FOOTER) {
                mBrowserZone = hasPager ? BROWSER_ZONE_PAGER : (n > 0 ? BROWSER_ZONE_LIST : BROWSER_ZONE_TOOLBAR);
            } else {
                mBrowserZone = (n > 0) ? BROWSER_ZONE_LIST : BROWSER_ZONE_TOOLBAR;
            }
        } else if (delta > 0) {
            if (mBrowserZone == BROWSER_ZONE_TOOLBAR) {
                mBrowserZone = (n > 0) ? BROWSER_ZONE_LIST : BROWSER_ZONE_FOOTER;
            } else if (mBrowserZone == BROWSER_ZONE_LIST) {
                if (n > 0 && mBrowserSelected + 1 < n) BrowserMoveSelection(1);
                else mBrowserZone = hasPager ? BROWSER_ZONE_PAGER : BROWSER_ZONE_FOOTER;
            } else if (mBrowserZone == BROWSER_ZONE_PAGER) {
                mBrowserZone = BROWSER_ZONE_FOOTER;
            } else {
                mBrowserZone = BROWSER_ZONE_TOOLBAR;
            }
        }
        if (mBrowserZone == BROWSER_ZONE_PAGER) {
            NormalizeBrowserPagerFocus();
        }
    }

    void BrowserNavSide(int delta) {
        if (mPasswordPromptOpen) {
            mPasswordModalFocus = (mPasswordModalFocus == 0) ? 1 : 0;
            return;
        }
        const int step = (delta < 0) ? -1 : 1;
        if (mBrowserZone == BROWSER_ZONE_TOOLBAR) {
            mBrowserToolbarFocus = (mBrowserToolbarFocus + step + BROWSER_TB_COUNT) % BROWSER_TB_COUNT;
        } else if (mBrowserZone == BROWSER_ZONE_LIST) {
            if (delta < 0) BrowserPrevPage();
            else BrowserNextPage();
        } else if (mBrowserZone == BROWSER_ZONE_PAGER) {
            for (int tries = 1; tries <= BROWSER_PG_COUNT; ++tries) {
                const int cand = (mBrowserPagerFocus + step * tries + BROWSER_PG_COUNT * 4) % BROWSER_PG_COUNT;
                if (BrowserPagerButtonEnabled(cand)) {
                    mBrowserPagerFocus = cand;
                    return;
                }
            }
            NormalizeBrowserPagerFocus();
        } else if (mBrowserZone == BROWSER_ZONE_FOOTER) {
            mBrowserFooterFocus = (mBrowserFooterFocus + step + BROWSER_FT_COUNT) % BROWSER_FT_COUNT;
        }
    }

    bool BrowserNavActivate() {
        if (mPasswordPromptOpen) {
            if (mPasswordModalFocus == 1) {
                BrowserPasswordCancel();
                return true;
            }
            return BrowserPasswordSubmit();
        }
        if (mBrowserZone == BROWSER_ZONE_TOOLBAR) {
            switch (mBrowserToolbarFocus) {
            case BROWSER_TB_FILTER:    BrowserToggleCompatibleFilter(); return true;
            case BROWSER_TB_REFRESH:   BrowserRefresh(); return true;
            case BROWSER_TB_CREATE:    EnterCreate(CE_ONLINE_PAGE_BROWSER); return true;
            case BROWSER_TB_JOIN_CODE: EnterJoinCode(CE_ONLINE_PAGE_BROWSER); return true;
            default: return false;
            }
        } else if (mBrowserZone == BROWSER_ZONE_LIST) {
            if (FilteredRooms().empty()) return false;
            BrowserJoinSelected();
            return true;
        } else if (mBrowserZone == BROWSER_ZONE_PAGER) {
            if (!BrowserPagerButtonEnabled(mBrowserPagerFocus)) return false;
            switch (mBrowserPagerFocus) {
            case BROWSER_PG_PREV: BrowserPrevPage(); return true;
            case BROWSER_PG_NEXT: BrowserNextPage(); return true;
            case BROWSER_PG_JOIN: BrowserJoinSelected(); return true;
            default: return false;
            }
        } else if (mBrowserZone == BROWSER_ZONE_FOOTER) {
            switch (mBrowserFooterFocus) {
            case BROWSER_FT_PROFILE: EnterProfile(CE_ONLINE_PAGE_BROWSER); return true;
            case BROWSER_FT_LAN:     TriggerDirectLan(); return true;
            case BROWSER_FT_BACK:    NavigateBack(); return true;
            default: return false;
            }
        }
        return false;
    }

    bool BrowserJoinSelected() {
        std::vector<CeOnlineUiRoomSummary> list = FilteredRooms();
        if (list.empty() || mBrowserSelected < 0 || mBrowserSelected >= static_cast<int>(list.size())) {
            SetStatusNotice("No lobby selected.", true);
            return false;
        }
        const CeOnlineUiRoomSummary &room = list[static_cast<size_t>(mBrowserSelected)];
        if (!room.compatible) {
            SetStatusNotice("This lobby uses a different version or content.", true);
            return false;
        }
        if (room.state != "lobby") {
            SetStatusNotice("That match has already started.", true);
            return false;
        }
        if (room.humanSeats >= room.maxSeats) {
            SetStatusNotice("This lobby is full.", true);
            return false;
        }
        if (room.hasPassword) {
            mPasswordPromptOpen = true;
            mPasswordModalFocus = 0;
            mPasswordPromptRoomId = room.roomId;
            mPasswordPromptRoomName = CeOnlineSanitizeForFont(room.name);
            mPasswordPromptMap = room.map;
            mPasswordPromptInput.clear();
            SetStatusNotice("Enter the lobby password to join.", false);
            return true;
        }
        return SubmitJoinRoom(room.roomId, "", room.map);
    }

    bool BrowserPasswordTypeChar(char c) {
        if (!mPasswordPromptOpen) return false;
        if (!CeOnlineFontCharSafe(static_cast<unsigned char>(c))) return false;
        if (mPasswordPromptInput.size() >= CE_ONLINE_UI_PASSWORD_MAX) return false;
        mPasswordPromptInput.push_back(c);
        return true;
    }

    void BrowserPasswordBackspace() {
        if (mPasswordPromptOpen && !mPasswordPromptInput.empty()) {
            mPasswordPromptInput.erase(mPasswordPromptInput.size() - 1);
        }
    }

    void BrowserPasswordCancel() {
        mPasswordPromptOpen = false;
        mPasswordPromptInput.clear();
        mStatusNotice.clear();
        mStatusIsError = false;
    }

    bool BrowserPasswordSubmit() {
        if (!mPasswordPromptOpen) return false;
        if (mPasswordPromptInput.empty()) {
            SetStatusNotice("Enter the lobby password first.", true);
            return false;
        }
        const std::string roomId = mPasswordPromptRoomId;
        const std::string pwd = mPasswordPromptInput;
        const int map = mPasswordPromptMap;
        mPasswordPromptOpen = false;
        mPasswordPromptInput.clear();
        return SubmitJoinRoom(roomId, pwd, map);
    }

    // -----------------------------------------------------------------------
    // Create Lobby Page
    // -----------------------------------------------------------------------
    void EnterCreate(CeOnlinePage returnTo) {
        mFormReturnPage = returnTo;
        mPage = CE_ONLINE_PAGE_CREATE;
        mCreateFocus = CREATE_NAME;
        if (mCreateRoomName.empty()) {
            std::string base = CeOnlineSanitizeForFont(mProfile.callsign);
            if (base.size() > 18) base.resize(18);
            mCreateRoomName = base.empty() ? "Forseti Skirmish" : (base + " Lobby");
        }
        mStatusNotice.clear();
        mStatusIsError = false;
    }

    int CreateFocus() const { return mCreateFocus; }
    void SetCreateFocus(int idx) {
        if (idx < 0) idx = CREATE_FOCUS_COUNT - 1;
        if (idx >= CREATE_FOCUS_COUNT) idx = 0;
        mCreateFocus = idx;
    }
    void CreateMoveFocus(int delta) { SetCreateFocus(mCreateFocus + delta); }

    const std::string &CreateRoomName() const { return mCreateRoomName; }
    void SetCreateRoomName(const std::string &name) { mCreateRoomName = name; }
    int CreateMapIndex() const { return mCreateMapIndex; }
    int CreateMapLevel() const { return CeOnlineMapAt(mCreateMapIndex).level; }
    const char *CreateMapName() const { return CeOnlineMapAt(mCreateMapIndex).name; }
    int CreateModeIndex() const { return mCreateModeIndex; }
    const char *CreateModeWire() const { return CeOnlineModeAt(mCreateModeIndex).wire; }
    const char *CreateModeLabel() const { return CeOnlineModeDisplayLabel(CreateModeWire(), mCreateMaxSeats); }
    uint16_t CreateMaxSeats() const { return mCreateMaxSeats; }
    bool CreateIsPublic() const { return mCreatePublic; }
    const char *CreateVisibilityLabel() const { return mCreatePublic ? "Public" : "Unlisted Code"; }
    const std::string &CreatePassword() const { return mCreatePassword; }
    void SetCreatePassword(const std::string &pwd) { mCreatePassword = pwd; }
    bool CreateRelayOnly() const { return mCreateRelayOnly; }
    const char *CreateRelayOnlyLabel() const {
        return mCreateRelayOnly ? "On (Relay Only)" : "Off (Direct P2P + TURN)";
    }
    static const char *CreatePrivacyNotice() { return kCeOnlinePrivacyNotice; }
    bool CreateAiFill() const { return mCreateAiFill; }
    const char *CreateAiFillLabel() const { return mCreateAiFill ? "On" : "Off"; }

    void CreateCycleMap(int delta) {
        const int step = delta % CE_ONLINE_MAP_COUNT;
        mCreateMapIndex = (mCreateMapIndex + step + CE_ONLINE_MAP_COUNT) % CE_ONLINE_MAP_COUNT;
        const CeOnlineMapInfo &nextMap = CeOnlineMapAt(mCreateMapIndex);
        const CeOnlineMode curMode = static_cast<CeOnlineMode>(mCreateModeIndex);
        if (!CeOnlineMapSupportsMode(nextMap.level, curMode)) {
            mCreateModeIndex = static_cast<int>(nextMap.defaultMode);
        }
        mCreateMaxSeats = CeOnlineClampMaxSeatsForMode(
            static_cast<CeOnlineMode>(mCreateModeIndex), mCreateMaxSeats, mMaxSeatsCap);
    }

    void CreateCycleMode(int delta) {
        const int level = CreateMapLevel();
        if (mCreateModeIndex == static_cast<int>(CE_ONLINE_MODE_VERSUS_2V2) &&
            CeOnlineMapSupportsMode(level, CE_ONLINE_MODE_VERSUS_2V2)) {
            // On Versus maps, cycling Mode toggles 2v2 Versus (4 seats) <-> 1v1 Versus (2 seats).
            if (mMaxSeatsCap >= 4) {
                mCreateMaxSeats = (mCreateMaxSeats == 2) ? 4 : 2;
            }
            return;
        }
        const int step = (delta < 0) ? -1 : 1;
        for (int tries = 1; tries <= CE_ONLINE_MODE_COUNT; ++tries) {
            const int cand = (mCreateModeIndex + step * tries + CE_ONLINE_MODE_COUNT * 4) % CE_ONLINE_MODE_COUNT;
            if (CeOnlineMapSupportsMode(level, static_cast<CeOnlineMode>(cand))) {
                mCreateModeIndex = cand;
                mCreateMaxSeats = CeOnlineClampMaxSeatsForMode(
                    static_cast<CeOnlineMode>(mCreateModeIndex), mCreateMaxSeats, mMaxSeatsCap);
                return;
            }
        }
    }

    void CreateCycleMaxSeats(int delta) {
        const CeOnlineMode mode = static_cast<CeOnlineMode>(mCreateModeIndex);
        if (mode == CE_ONLINE_MODE_VERSUS_2V2) {
            uint16_t evenCap = mMaxSeatsCap;
            if ((evenCap & 1u) != 0u && evenCap > CE_ONLINE_UI_MIN_SLOTS) --evenCap;
            if (evenCap < CE_ONLINE_UI_MIN_SLOTS) evenCap = CE_ONLINE_UI_MIN_SLOTS;
            const int count = static_cast<int>((evenCap - CE_ONLINE_UI_MIN_SLOTS) / 2u) + 1;
            int cur = static_cast<int>((CeOnlineClampMaxSeatsForMode(mode, mCreateMaxSeats, evenCap) - CE_ONLINE_UI_MIN_SLOTS) / 2u);
            const int step = (delta % count + count) % count;
            cur = (cur + step) % count;
            mCreateMaxSeats = static_cast<uint16_t>(CE_ONLINE_UI_MIN_SLOTS + cur * 2);
            return;
        }
        const int range = static_cast<int>(mMaxSeatsCap - CE_ONLINE_UI_MIN_SLOTS) + 1;
        int cur = static_cast<int>(mCreateMaxSeats - CE_ONLINE_UI_MIN_SLOTS);
        const int step = (delta % range + range) % range;
        cur = (cur + step) % range;
        mCreateMaxSeats = static_cast<uint16_t>(CE_ONLINE_UI_MIN_SLOTS + cur);
    }

    void CreateToggleVisibility() { mCreatePublic = !mCreatePublic; }
    void CreateToggleRelayOnly() { mCreateRelayOnly = !mCreateRelayOnly; }
    void CreateToggleAiFill() { mCreateAiFill = !mCreateAiFill; }

    void CreateAdjustFocused(int delta) {
        switch (mCreateFocus) {
        case CREATE_MAP:         CreateCycleMap(delta); break;
        case CREATE_MODE:        CreateCycleMode(delta); break;
        case CREATE_MAX_PLAYERS: CreateCycleMaxSeats(delta); break;
        case CREATE_VISIBILITY:  CreateToggleVisibility(); break;
        case CREATE_RELAY_ONLY:  CreateToggleRelayOnly(); break;
        case CREATE_AI_FILL:     CreateToggleAiFill(); break;
        default: break;
        }
    }

    bool CreateTypeChar(char c) {
        if (!CeOnlineFontCharSafe(static_cast<unsigned char>(c))) return false;
        if (mCreateFocus == CREATE_NAME) {
            if (mCreateRoomName.size() >= CE_ONLINE_UI_ROOM_NAME_MAX) return false;
            mCreateRoomName.push_back(c);
            return true;
        }
        if (mCreateFocus == CREATE_PASSWORD) {
            if (mCreatePassword.size() >= CE_ONLINE_UI_PASSWORD_MAX) return false;
            mCreatePassword.push_back(c);
            return true;
        }
        return false;
    }

    void CreateBackspace() {
        if (mCreateFocus == CREATE_NAME && !mCreateRoomName.empty()) {
            mCreateRoomName.erase(mCreateRoomName.size() - 1);
        } else if (mCreateFocus == CREATE_PASSWORD && !mCreatePassword.empty()) {
            mCreatePassword.erase(mCreatePassword.size() - 1);
        }
    }

    bool CreateSubmit() {
        std::string normName, err;
        if (!CeOnlineValidateRoomName(mCreateRoomName, &normName, &err)) {
            SetStatusNotice(err, true);
            return false;
        }
        mCreateRoomName = normName;
        if (!mBackend) {
            SetStatusNotice("Lobby service is not connected.", true);
            return false;
        }
        CeOnlineCreateParams params;
        params.name = normName;
        params.map = CreateMapLevel();
        params.mode = CreateModeWire();
        params.maxSeats = mCreateMaxSeats;
        params.topology = (mCreateMaxSeats <= CE_ONLINE_UI_ACTIVE_MAX_SLOTS) ? "star_lockstep" : "authoritative";
        params.visibility = mCreatePublic ? "public" : "unlisted";
        params.password = mCreatePassword;
        params.requireRelay = mCreateRelayOnly;
        params.aiFill = mCreateAiFill;
        params.beCfg = DefaultBeConfigForMap(params.map);
        if (!mBackend->RequestCreateRoom(params, &err)) {
            SetStatusNotice(err.empty() ? "Could not create lobby." : err, true);
            return false;
        }
        BeginConnecting("Creating Lobby", "Registering lobby with server...", true);
        return true;
    }

    // -----------------------------------------------------------------------
    // Join by Code Page
    // -----------------------------------------------------------------------
    void EnterJoinCode(CeOnlinePage returnTo) {
        mFormReturnPage = returnTo;
        mPage = CE_ONLINE_PAGE_JOIN_CODE;
        mJoinCodeFocus = JOIN_CODE_INPUT;
        mJoinPendingResolve = false;
        mStatusNotice.clear();
        mStatusIsError = false;
    }

    int JoinCodeFocus() const { return mJoinCodeFocus; }
    void SetJoinCodeFocus(int idx) {
        if (idx < 0) idx = JOIN_FOCUS_COUNT - 1;
        if (idx >= JOIN_FOCUS_COUNT) idx = 0;
        mJoinCodeFocus = idx;
    }
    void JoinCodeMoveFocus(int delta) { SetJoinCodeFocus(mJoinCodeFocus + delta); }

    const std::string &JoinCodeInput() const { return mJoinCode; }
    const std::string &JoinPasswordInput() const { return mJoinPassword; }
    void SetJoinCodeInput(const std::string &s) { mJoinCode = s; }
    void SetJoinPasswordInput(const std::string &s) { mJoinPassword = s; }

    bool JoinCodeTypeChar(char c) {
        if (mJoinCodeFocus == JOIN_CODE_INPUT) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc >= 'a' && uc <= 'z') uc = static_cast<unsigned char>(uc - 'a' + 'A');
            if (!((uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9'))) return false;
            if (mJoinCode.size() >= CE_ONLINE_UI_CODE_LEN) return false;
            mJoinCode.push_back(static_cast<char>(uc));
            return true;
        }
        if (mJoinCodeFocus == JOIN_PASSWORD_INPUT) {
            if (!CeOnlineFontCharSafe(static_cast<unsigned char>(c))) return false;
            if (mJoinPassword.size() >= CE_ONLINE_UI_PASSWORD_MAX) return false;
            mJoinPassword.push_back(c);
            return true;
        }
        return false;
    }

    void JoinCodeBackspace() {
        if (mJoinCodeFocus == JOIN_CODE_INPUT && !mJoinCode.empty()) {
            mJoinCode.erase(mJoinCode.size() - 1);
        } else if (mJoinCodeFocus == JOIN_PASSWORD_INPUT && !mJoinPassword.empty()) {
            mJoinPassword.erase(mJoinPassword.size() - 1);
        }
    }

    bool JoinCodePaste() {
        if (!mBackend) return false;
        std::string clip;
        if (!mBackend->PasteFromClipboard(&clip) || clip.empty()) {
            SetStatusNotice("Nothing to paste from clipboard.", false);
            return false;
        }
        std::string cleaned, err;
        if (!CeOnlineValidateRoomCode(clip, &cleaned, &err)) {
            SetStatusNotice(err, true);
            return false;
        }
        mJoinCode = cleaned;
        SetStatusNotice("Lobby code pasted.", false);
        return true;
    }

    bool JoinCodeSubmit() {
        std::string code, err;
        if (!CeOnlineValidateRoomCode(mJoinCode, &code, &err)) {
            SetStatusNotice(err, true);
            return false;
        }
        mJoinCode = code;
        if (!mBackend) {
            SetStatusNotice("Lobby service is not connected.", true);
            return false;
        }
        if (!mBackend->RequestResolveCode(code, &err)) {
            SetStatusNotice(err.empty() ? "Could not resolve lobby code." : err, true);
            return false;
        }
        mJoinPendingResolve = true;
        BeginConnecting("Resolving Code", "Looking up lobby " + code + "...", true);
        return true;
    }

    // -----------------------------------------------------------------------
    // Room Page (4-slot default + 64-slot paginated Assault-ready!)
    // -----------------------------------------------------------------------
    bool IsLocalHost() const {
        return mRoom.active && mRoom.localSeat == mRoom.hostSeat;
    }

    const CeOnlineSeatView *LocalSeat() const {
        if (!mRoom.active) return NULL;
        for (size_t i = 0; i < mRoom.seats.size(); ++i) {
            if (mRoom.seats[i].seat == mRoom.localSeat) return &mRoom.seats[i];
        }
        return NULL;
    }

    CeOnlineSeatView *LocalSeatMutable() {
        if (!mRoom.active) return NULL;
        for (size_t i = 0; i < mRoom.seats.size(); ++i) {
            if (mRoom.seats[i].seat == mRoom.localSeat) return &mRoom.seats[i];
        }
        return NULL;
    }

    const CeOnlineSeatView *SelectedSeat() const {
        if (!mRoom.active || mRoom.seats.empty()) return NULL;
        if (mRosterSelected < 0 || mRosterSelected >= static_cast<int>(mRoom.seats.size())) return NULL;
        return &mRoom.seats[static_cast<size_t>(mRosterSelected)];
    }

    int RosterSelectedIndex() const { return mRosterSelected; }
    int RosterPageOffset() const { return mRosterPageOffset; }

    int RosterPageCount() const {
        const int n = static_cast<int>(mRoom.seats.size());
        if (n <= 0) return 1;
        return (n + CE_ONLINE_UI_ROSTER_PAGE_SIZE - 1) / CE_ONLINE_UI_ROSTER_PAGE_SIZE;
    }

    int RosterCurrentPage() const {
        return (mRosterPageOffset / CE_ONLINE_UI_ROSTER_PAGE_SIZE) + 1;
    }

    void RosterSelectIndex(int idx) {
        const int n = static_cast<int>(mRoom.seats.size());
        if (n <= 0) {
            mRosterSelected = 0;
            mRosterPageOffset = 0;
            return;
        }
        if (idx < 0) idx = 0;
        if (idx >= n) idx = n - 1;
        mRosterSelected = idx;
        mRosterPageOffset = (mRosterSelected / CE_ONLINE_UI_ROSTER_PAGE_SIZE) * CE_ONLINE_UI_ROSTER_PAGE_SIZE;
    }

    void RosterMoveSelection(int delta) {
        const int n = static_cast<int>(mRoom.seats.size());
        if (n <= 0) return;
        int next = mRosterSelected + delta;
        if (next < 0) next = 0;
        if (next >= n) next = n - 1;
        RosterSelectIndex(next);
    }

    void RosterNextPage() {
        const int n = static_cast<int>(mRoom.seats.size());
        if (mRosterPageOffset + CE_ONLINE_UI_ROSTER_PAGE_SIZE < n) {
            mRosterPageOffset += CE_ONLINE_UI_ROSTER_PAGE_SIZE;
            mRosterSelected = mRosterPageOffset;
            NormalizeRoomFocus();
        }
    }

    void RosterPrevPage() {
        if (mRosterPageOffset >= CE_ONLINE_UI_ROSTER_PAGE_SIZE) {
            mRosterPageOffset -= CE_ONLINE_UI_ROSTER_PAGE_SIZE;
            mRosterSelected = mRosterPageOffset;
            NormalizeRoomFocus();
        }
    }

    int RoomZoneFocus() const { return mRoomZone; }
    void SetRoomZoneFocus(int zone) {
        if (zone == ROOM_ZONE_HOST && !IsLocalHost()) zone = ROOM_ZONE_PLAYER;
        if (zone < ROOM_ZONE_HEADER) zone = ROOM_ZONE_HEADER;
        if (zone > ROOM_ZONE_CHAT) zone = ROOM_ZONE_CHAT;
        mRoomZone = zone;
    }
    int RoomHeaderFocus() const { return mRoomHeaderFocus; }
    void SetRoomHeaderFocus(int idx) {
        mRoomZone = ROOM_ZONE_HEADER;
        mRoomHeaderFocus = (idx % ROOM_HD_COUNT + ROOM_HD_COUNT) % ROOM_HD_COUNT;
    }
    int RoomPlayerFocus() const { return mRoomPlayerFocus; }
    void SetRoomPlayerFocus(int idx) {
        mRoomZone = ROOM_ZONE_PLAYER;
        mRoomPlayerFocus = (idx % ROOM_PL_COUNT + ROOM_PL_COUNT) % ROOM_PL_COUNT;
    }
    int RoomHostFocus() const { return mRoomHostFocus; }
    void SetRoomHostFocus(int idx) {
        if (!IsLocalHost()) return;
        mRoomZone = ROOM_ZONE_HOST;
        mRoomHostFocus = (idx % ROOM_HS_COUNT + ROOM_HS_COUNT) % ROOM_HS_COUNT;
    }

    bool RoomHeaderButtonEnabled(int idx) const {
        if (idx == ROOM_HD_COPY) return !mRoom.code.empty();
        if (idx == ROOM_HD_PREV) return RosterCurrentPage() > 1;
        if (idx == ROOM_HD_NEXT) return RosterCurrentPage() < RosterPageCount();
        return false;
    }

    bool RoomHasEnabledHeaderButton() const {
        for (int i = 0; i < ROOM_HD_COUNT; ++i) {
            if (RoomHeaderButtonEnabled(i)) return true;
        }
        return false;
    }

    bool RoomPlayerButtonEnabled(int idx) const {
        const CeOnlineSeatView *me = LocalSeat();
        const CeOnlineSeatView *sel = SelectedSeat();
        switch (idx) {
        case ROOM_PL_READY: return me != NULL;
        case ROOM_PL_SIDE:  return me != NULL && EffectiveRoomMode() == "versus_2v2";
        case ROOM_PL_BE:    return me != NULL;
        case ROOM_PL_MUTE:  return sel != NULL && sel->kind == CE_ONLINE_SEAT_HUMAN && sel->seat != mRoom.localSeat;
        case ROOM_PL_LEAVE: return true;
        default:            return false;
        }
    }

    bool RoomHostButtonEnabled(int idx) const {
        if (!IsLocalHost()) return false;
        const CeOnlineSeatView *sel = SelectedSeat();
        switch (idx) {
        case ROOM_HS_MAP:   return true;
        case ROOM_HS_MODE:  return RoomHostCanCycleMode();
        case ROOM_HS_SLOT:  return sel != NULL && sel->kind != CE_ONLINE_SEAT_HUMAN;
        case ROOM_HS_KICK:  return sel != NULL && sel->kind == CE_ONLINE_SEAT_HUMAN && sel->seat != mRoom.hostSeat;
        case ROOM_HS_START: return true;
        default:            return false;
        }
    }

    void NormalizeRoomFocus() {
        if (mRoomZone == ROOM_ZONE_HOST && !IsLocalHost()) {
            mRoomZone = ROOM_ZONE_PLAYER;
        }
        if (mRoomZone == ROOM_ZONE_HEADER && !RoomHeaderButtonEnabled(mRoomHeaderFocus)) {
            for (int i = 0; i < ROOM_HD_COUNT; ++i) {
                if (RoomHeaderButtonEnabled(i)) {
                    mRoomHeaderFocus = i;
                    break;
                }
            }
        } else if (mRoomZone == ROOM_ZONE_PLAYER && !RoomPlayerButtonEnabled(mRoomPlayerFocus)) {
            for (int i = 0; i < ROOM_PL_COUNT; ++i) {
                if (RoomPlayerButtonEnabled(i)) {
                    mRoomPlayerFocus = i;
                    break;
                }
            }
        } else if (mRoomZone == ROOM_ZONE_HOST && !RoomHostButtonEnabled(mRoomHostFocus)) {
            for (int i = 0; i < ROOM_HS_COUNT; ++i) {
                if (RoomHostButtonEnabled(i)) {
                    mRoomHostFocus = i;
                    break;
                }
            }
        }
    }

    void RoomNavMove(int delta) {
        if (mChatComposing) return;
        const int seats = static_cast<int>(mRoom.seats.size());
        const bool hasHeader = RoomHasEnabledHeaderButton();
        if (delta < 0) {
            if (mRoomZone == ROOM_ZONE_CHAT) {
                mRoomZone = IsLocalHost() ? ROOM_ZONE_HOST : ROOM_ZONE_PLAYER;
            } else if (mRoomZone == ROOM_ZONE_HOST) {
                mRoomZone = ROOM_ZONE_PLAYER;
            } else if (mRoomZone == ROOM_ZONE_PLAYER) {
                mRoomZone = (seats > 0) ? ROOM_ZONE_ROSTER : (hasHeader ? ROOM_ZONE_HEADER : ROOM_ZONE_CHAT);
            } else if (mRoomZone == ROOM_ZONE_ROSTER) {
                if (seats > 0 && mRosterSelected > 0) RosterMoveSelection(-1);
                else mRoomZone = hasHeader ? ROOM_ZONE_HEADER : ROOM_ZONE_CHAT;
            } else {
                mRoomZone = ROOM_ZONE_CHAT;
            }
        } else if (delta > 0) {
            if (mRoomZone == ROOM_ZONE_HEADER) {
                mRoomZone = (seats > 0) ? ROOM_ZONE_ROSTER : ROOM_ZONE_PLAYER;
            } else if (mRoomZone == ROOM_ZONE_ROSTER) {
                if (seats > 0 && mRosterSelected + 1 < seats) RosterMoveSelection(1);
                else mRoomZone = ROOM_ZONE_PLAYER;
            } else if (mRoomZone == ROOM_ZONE_PLAYER) {
                mRoomZone = IsLocalHost() ? ROOM_ZONE_HOST : ROOM_ZONE_CHAT;
            } else if (mRoomZone == ROOM_ZONE_HOST) {
                mRoomZone = ROOM_ZONE_CHAT;
            } else {
                mRoomZone = hasHeader ? ROOM_ZONE_HEADER : ((seats > 0) ? ROOM_ZONE_ROSTER : ROOM_ZONE_PLAYER);
            }
        }
        NormalizeRoomFocus();
    }

    void RoomNavSide(int delta) {
        if (mChatComposing) return;
        const int step = (delta < 0) ? -1 : 1;
        if (mRoomZone == ROOM_ZONE_HEADER) {
            for (int tries = 1; tries <= ROOM_HD_COUNT; ++tries) {
                const int cand = (mRoomHeaderFocus + step * tries + ROOM_HD_COUNT * 4) % ROOM_HD_COUNT;
                if (RoomHeaderButtonEnabled(cand)) {
                    mRoomHeaderFocus = cand;
                    return;
                }
            }
            NormalizeRoomFocus();
        } else if (mRoomZone == ROOM_ZONE_ROSTER) {
            if (delta < 0) RosterPrevPage();
            else RosterNextPage();
        } else if (mRoomZone == ROOM_ZONE_PLAYER) {
            for (int tries = 1; tries <= ROOM_PL_COUNT; ++tries) {
                const int cand = (mRoomPlayerFocus + step * tries + ROOM_PL_COUNT * 4) % ROOM_PL_COUNT;
                if (RoomPlayerButtonEnabled(cand)) {
                    mRoomPlayerFocus = cand;
                    return;
                }
            }
            NormalizeRoomFocus();
        } else if (mRoomZone == ROOM_ZONE_HOST && IsLocalHost()) {
            for (int tries = 1; tries <= ROOM_HS_COUNT; ++tries) {
                const int cand = (mRoomHostFocus + step * tries + ROOM_HS_COUNT * 4) % ROOM_HS_COUNT;
                if (RoomHostButtonEnabled(cand)) {
                    mRoomHostFocus = cand;
                    return;
                }
            }
            NormalizeRoomFocus();
        }
    }

    bool RoomNavActivate() {
        if (mChatComposing) {
            return RoomChatSubmit();
        }
        if (mRoomZone == ROOM_ZONE_HEADER) {
            if (!RoomHeaderButtonEnabled(mRoomHeaderFocus)) return false;
            switch (mRoomHeaderFocus) {
            case ROOM_HD_COPY: RoomCopyCode(); return true;
            case ROOM_HD_PREV: RosterPrevPage(); return true;
            case ROOM_HD_NEXT: RosterNextPage(); return true;
            default: return false;
            }
        } else if (mRoomZone == ROOM_ZONE_ROSTER) {
            const CeOnlineSeatView *sel = SelectedSeat();
            if (!sel) return false;
            if (sel->seat == mRoom.localSeat) {
                RoomToggleReady();
                return true;
            } else if (sel->kind != CE_ONLINE_SEAT_HUMAN && IsLocalHost()) {
                RoomHostCycleSelectedSeatKind();
                return true;
            } else if (sel->kind == CE_ONLINE_SEAT_HUMAN) {
                RoomToggleMuteSelected();
                return true;
            }
            return false;
        } else if (mRoomZone == ROOM_ZONE_PLAYER) {
            if (!RoomPlayerButtonEnabled(mRoomPlayerFocus)) return false;
            switch (mRoomPlayerFocus) {
            case ROOM_PL_READY: RoomToggleReady(); return true;
            case ROOM_PL_SIDE:  RoomCycleSide(1); return true;
            case ROOM_PL_BE:    RoomCycleBeConfig(1); return true;
            case ROOM_PL_MUTE:  RoomToggleMuteSelected(); return true;
            case ROOM_PL_LEAVE: RoomLeave(); return true;
            default: return false;
            }
        } else if (mRoomZone == ROOM_ZONE_HOST) {
            if (!IsLocalHost()) {
                mRoomZone = ROOM_ZONE_PLAYER;
                NormalizeRoomFocus();
                return false;
            }
            if (!RoomHostButtonEnabled(mRoomHostFocus)) return false;
            switch (mRoomHostFocus) {
            case ROOM_HS_MAP:   RoomHostCycleMap(1); NormalizeRoomFocus(); return true;
            case ROOM_HS_MODE:  RoomHostCycleMode(1); NormalizeRoomFocus(); return true;
            case ROOM_HS_SLOT:  RoomHostCycleSelectedSeatKind(); NormalizeRoomFocus(); return true;
            case ROOM_HS_KICK:  RoomHostKickSelected(); NormalizeRoomFocus(); return true;
            case ROOM_HS_START: RoomHostStartMatch(); return true;
            default: return false;
            }
        } else if (mRoomZone == ROOM_ZONE_CHAT) {
            RoomOpenChat();
            return true;
        }
        return false;
    }

    const std::vector<std::string> &AvailableBeConfigs() const { return mAvailableBeConfigs; }

    bool IsPlayerMuted(const std::string &playerId) const {
        return !playerId.empty() && mMutedPlayers.find(playerId) != mMutedPlayers.end();
    }

    std::string FormatSeatName(const CeOnlineSeatView &seat) const {
        if (seat.kind == CE_ONLINE_SEAT_AI) return "AI Bot";
        if (seat.kind == CE_ONLINE_SEAT_OPEN) return "Open";
        if (seat.kind == CE_ONLINE_SEAT_CLOSED) return "Closed";
        std::string name = CeOnlineSanitizeForFont(seat.callsign.empty() ? std::string("Pilot") : seat.callsign);
        if (!seat.tag.empty()) {
            name += "#";
            name += CeOnlineSanitizeForFont(seat.tag);
        }
        if (IsPlayerMuted(seat.playerId)) {
            return CeOnlineTruncateFontSafe(name, 18) + " (Muted)";
        }
        return CeOnlineTruncateFontSafe(name, 25);
    }

    std::string FormatSeatPing(const CeOnlineSeatView &seat) const {
        if (seat.kind != CE_ONLINE_SEAT_HUMAN || seat.rttMs < 0) return "--";
        char buf[24];
        snprintf(buf, sizeof(buf), "%dms", seat.rttMs);
        return buf;
    }

    std::string FormatSeatReady(const CeOnlineSeatView &seat) const {
        if (seat.kind != CE_ONLINE_SEAT_HUMAN) return "--";
        return seat.ready ? "READY" : "WAIT";
    }

    // Player controls
    bool RoomToggleReady() {
        CeOnlineSeatView *me = LocalSeatMutable();
        if (!me || !mBackend) return false;
        const bool nextReady = !me->ready;
        std::string err;
        if (!mBackend->RequestUpdateSelf(me->team, me->beCfg, nextReady, &err)) {
            SetStatusNotice(err.empty() ? "Could not update ready state." : err, true);
            return false;
        }
        me->ready = nextReady;
        return true;
    }

    bool RoomCycleSide(int delta) {
        CeOnlineSeatView *me = LocalSeatMutable();
        if (!me || !mBackend) return false;
        (void)delta;
        if (EffectiveRoomMode() != "versus_2v2") {
            SetStatusNotice("Side selection is only available in Versus mode.", false);
            return false;
        }
        const int nextTeam = (me->team == 0) ? 1 : 0;
        std::string err;
        if (!mBackend->RequestUpdateSelf(nextTeam, me->beCfg, me->ready, &err)) {
            SetStatusNotice(err.empty() ? "Could not switch side." : err, true);
            return false;
        }
        me->team = nextTeam;
        return true;
    }

    bool RoomCycleBeConfig(int delta) {
        CeOnlineSeatView *me = LocalSeatMutable();
        if (!me || !mBackend || mAvailableBeConfigs.empty()) return false;
        int idx = 0;
        for (size_t i = 0; i < mAvailableBeConfigs.size(); ++i) {
            if (mAvailableBeConfigs[i] == me->beCfg) {
                idx = static_cast<int>(i);
                break;
            }
        }
        const int n = static_cast<int>(mAvailableBeConfigs.size());
        idx = (idx + delta + n) % n;
        const std::string nextBe = mAvailableBeConfigs[static_cast<size_t>(idx)];
        std::string err;
        if (!mBackend->RequestUpdateSelf(me->team, nextBe, me->ready, &err)) {
            SetStatusNotice(err.empty() ? "Could not change Battle Engine." : err, true);
            return false;
        }
        me->beCfg = nextBe;
        return true;
    }

    bool RoomToggleMuteSelected() {
        const CeOnlineSeatView *sel = SelectedSeat();
        if (!sel || sel->kind != CE_ONLINE_SEAT_HUMAN) {
            SetStatusNotice("Select a human player to mute or unmute.", false);
            return false;
        }
        if (sel->seat == mRoom.localSeat || sel->playerId.empty()) {
            SetStatusNotice("You cannot mute yourself.", false);
            return false;
        }
        if (mMutedPlayers.find(sel->playerId) != mMutedPlayers.end()) {
            mMutedPlayers.erase(sel->playerId);
            SetStatusNotice("Unmuted " + CeOnlineSanitizeForFont(sel->callsign) + ".", false);
        } else {
            mMutedPlayers.insert(sel->playerId);
            SetStatusNotice("Muted " + CeOnlineSanitizeForFont(sel->callsign) + " locally.", false);
        }
        return true;
    }

    // Chat compose & display (last 6 visible non-muted messages from 32-msg ring)
    bool ChatComposing() const { return mChatComposing; }
    const std::string &ChatDraft() const { return mChatDraft; }
    void RoomOpenChat() { mChatComposing = true; }
    void RoomCloseChat() { mChatComposing = false; }

    bool RoomChatTypeChar(char c) {
        if (!mChatComposing) return false;
        if (!CeOnlineFontCharSafe(static_cast<unsigned char>(c))) return false;
        if (mChatDraft.size() >= CE_ONLINE_UI_CHAT_MAX) return false;
        mChatDraft.push_back(c);
        return true;
    }

    void RoomChatBackspace() {
        if (mChatComposing && !mChatDraft.empty()) {
            mChatDraft.erase(mChatDraft.size() - 1);
        }
    }

    bool RoomChatSubmit() {
        if (!mBackend || !mRoom.active) return false;
        std::string trimmed = CeOnlineSanitizeForFont(mChatDraft);
        while (!trimmed.empty() && trimmed[0] == ' ') trimmed.erase(0, 1);
        while (!trimmed.empty() && trimmed[trimmed.size() - 1] == ' ') trimmed.erase(trimmed.size() - 1);
        mChatDraft.clear();
        mChatComposing = false;
        if (trimmed.empty()) return false;
        if (trimmed.size() > CE_ONLINE_UI_CHAT_MAX) trimmed.resize(CE_ONLINE_UI_CHAT_MAX);
        std::string err;
        if (!mBackend->RequestSendChat(trimmed, &err)) {
            SetStatusNotice(err.empty() ? "Could not send chat message." : err, true);
            return false;
        }
        return true;
    }

    std::vector<std::string> VisibleChatLines() const {
        std::vector<std::string> filtered;
        for (size_t i = 0; i < mRoom.chat.size(); ++i) {
            const CeOnlineUiChatEntry &msg = mRoom.chat[i];
            if (IsPlayerMuted(msg.playerId)) continue;
            std::string who = CeOnlineSanitizeForFont(msg.callsign.empty() ? std::string("Pilot") : msg.callsign);
            std::string body = CeOnlineSanitizeForFont(msg.text);
            filtered.push_back(CeOnlineTruncateFontSafe(who + ": " + body, 72));
        }
        if (filtered.size() > CE_ONLINE_UI_CHAT_VISIBLE) {
            filtered.erase(
                filtered.begin(),
                filtered.begin() + static_cast<ptrdiff_t>(filtered.size() - CE_ONLINE_UI_CHAT_VISIBLE));
        }
        return filtered;
    }

    // Optimistic-aware Room display accessors (keep UI responsive across rapid host clicks).
    int EffectiveRoomMap() const { return mHasOptimisticMap ? mOptimisticMapLevel : mRoom.map; }
    std::string EffectiveRoomMode() const { return mHasOptimisticMap ? mOptimisticMode : mRoom.mode; }
    uint16_t EffectiveRoomMaxSeats() const { return mHasOptimisticMap ? mOptimisticMaxSeats : mRoom.maxSeats; }
    const char *EffectiveRoomModeLabel() const {
        return CeOnlineModeDisplayLabel(EffectiveRoomMode(), EffectiveRoomMaxSeats());
    }
    bool RoomHostCanCycleMode() const {
        if (!IsLocalHost()) return false;
        if (EffectiveRoomMode() == "versus_2v2") {
            return mMaxSeatsCap >= 4;
        }
        const int mapLvl = EffectiveRoomMap();
        int supported = 0;
        for (int i = 0; i < CE_ONLINE_MODE_COUNT; ++i) {
            if (CeOnlineMapSupportsMode(mapLvl, static_cast<CeOnlineMode>(i))) ++supported;
        }
        return supported > 1;
    }

    // Host controls
    bool RoomHostCycleMap(int delta) {
        if (!IsLocalHost() || !mBackend) {
            SetStatusNotice("Only the host can change the map.", true);
            return false;
        }
        const int cur = mHasOptimisticMap ? mOptimisticMapIndex : CeOnlineFindMapIndex(mRoom.map);
        const int step = delta % CE_ONLINE_MAP_COUNT;
        const int next = (cur + step + CE_ONLINE_MAP_COUNT) % CE_ONLINE_MAP_COUNT;
        const CeOnlineMapInfo &nextMap = CeOnlineMapAt(next);

        const std::string curModeWire = EffectiveRoomMode();
        const CeOnlineMode curMode = static_cast<CeOnlineMode>(CeOnlineFindModeIndex(curModeWire));
        const CeOnlineMode nextMode = CeOnlineMapSupportsMode(nextMap.level, curMode)
                                          ? curMode
                                          : nextMap.defaultMode;
        const std::string nextModeWire = CeOnlineModeAt(static_cast<int>(nextMode)).wire;

        const uint16_t curSeats = EffectiveRoomMaxSeats();
        const uint16_t cap = (mRoom.topology == "authoritative") ? CE_ONLINE_UI_WIRE_MAX_SLOTS : mMaxSeatsCap;
        const uint16_t nextSeats = CeOnlineClampMaxSeatsForMode(nextMode, curSeats, cap);

        CeOnlineHostPatch patch;
        patch.hasMap = true;
        patch.map = nextMap.level;
        if (nextMode != curMode || nextModeWire != mRoom.mode) {
            patch.hasMode = true;
            patch.mode = nextModeWire;
        }
        if (nextSeats != curSeats || nextSeats != mRoom.maxSeats) {
            patch.hasMaxSeats = true;
            patch.maxSeats = nextSeats;
        }
        std::string err;
        if (!mBackend->RequestHostUpdate(patch, &err)) {
            SetStatusNotice(err.empty() ? "Could not change map." : err, true);
            return false;
        }
        mHasOptimisticMap = true;
        mOptimisticMapIndex = next;
        mOptimisticMapLevel = nextMap.level;
        mOptimisticMode = nextModeWire;
        mOptimisticMaxSeats = nextSeats;
        mOptimisticBaseRev = mRoom.rev;
        mOptimisticSetMs = mLastNowMs;

        SetStatusNotice(std::string("Map: ") + nextMap.name + " (" +
                            CeOnlineModeDisplayLabel(nextModeWire, nextSeats) + ")",
                        false);
        return true;
    }

    bool RoomHostCycleMode(int delta) {
        if (!IsLocalHost() || !mBackend) {
            SetStatusNotice("Only the host can change the mode.", true);
            return false;
        }
        const int curMap = EffectiveRoomMap();
        const std::string curModeWire = EffectiveRoomMode();
        const uint16_t curSeats = EffectiveRoomMaxSeats();

        // On Versus maps, toggle cleanly between 2v2 Versus (4 slots) and 1v1 Versus (2 slots).
        if (curModeWire == "versus_2v2") {
            const uint16_t nextSeats = (curSeats == 2) ? 4 : 2;
            if (nextSeats > mMaxSeatsCap) {
                SetStatusNotice("Only 1v1 Versus is available with the current slot limit.", false);
                return false;
            }
            if (nextSeats < curSeats) {
                for (size_t i = 0; i < mRoom.seats.size(); ++i) {
                    if (mRoom.seats[i].seat >= nextSeats && mRoom.seats[i].kind == CE_ONLINE_SEAT_HUMAN) {
                        SetStatusNotice("Cannot switch to 1v1 Versus while seats 3-4 have human players.", true);
                        return false;
                    }
                }
            }
            CeOnlineHostPatch patch;
            if (curMap != mRoom.map) {
                patch.hasMap = true;
                patch.map = curMap;
            }
            if (mRoom.mode != "versus_2v2") {
                patch.hasMode = true;
                patch.mode = "versus_2v2";
            }
            patch.hasMaxSeats = true;
            patch.maxSeats = nextSeats;
            std::string err;
            if (!mBackend->RequestHostUpdate(patch, &err)) {
                SetStatusNotice(err.empty() ? "Could not change mode." : err, true);
                return false;
            }
            mHasOptimisticMap = true;
            mOptimisticMapIndex = CeOnlineFindMapIndex(curMap);
            mOptimisticMapLevel = curMap;
            mOptimisticMode = "versus_2v2";
            mOptimisticMaxSeats = nextSeats;
            mOptimisticBaseRev = mRoom.rev;
            mOptimisticSetMs = mLastNowMs;

            SetStatusNotice(std::string("Mode: ") + CeOnlineModeDisplayLabel("versus_2v2", nextSeats) +
                                (nextSeats == 2 ? " (2 slots)." : " (4 slots)."),
                            false);
            return true;
        }

        const int cur = CeOnlineFindModeIndex(curModeWire);
        const int step = (delta < 0) ? -1 : 1;
        int next = cur;
        for (int tries = 1; tries <= CE_ONLINE_MODE_COUNT; ++tries) {
            const int cand = (cur + step * tries + CE_ONLINE_MODE_COUNT * 4) % CE_ONLINE_MODE_COUNT;
            if (CeOnlineMapSupportsMode(curMap, static_cast<CeOnlineMode>(cand))) {
                next = cand;
                break;
            }
        }
        if (next == cur) {
            SetStatusNotice(std::string("Only ") + CeOnlineModeAt(cur).label + " mode is supported on this map.", false);
            return false;
        }
        CeOnlineHostPatch patch;
        patch.hasMode = true;
        patch.mode = CeOnlineModeAt(next).wire;
        std::string err;
        if (!mBackend->RequestHostUpdate(patch, &err)) {
            SetStatusNotice(err.empty() ? "Could not change mode." : err, true);
            return false;
        }
        SetStatusNotice(std::string("Mode: ") + CeOnlineModeAt(next).label + ".", false);
        return true;
    }

    bool RoomHostCycleSelectedSeatKind() {
        if (!IsLocalHost() || !mBackend) {
            SetStatusNotice("Only the host can change seats.", true);
            return false;
        }
        const CeOnlineSeatView *sel = SelectedSeat();
        if (!sel) return false;
        if (sel->kind == CE_ONLINE_SEAT_HUMAN) {
            SetStatusNotice("Seat has a human player. Kick player first.", true);
            return false;
        }
        CeOnlineSeatKind nextKind = CE_ONLINE_SEAT_AI;
        if (sel->kind == CE_ONLINE_SEAT_OPEN) nextKind = CE_ONLINE_SEAT_AI;
        else if (sel->kind == CE_ONLINE_SEAT_AI) nextKind = CE_ONLINE_SEAT_CLOSED;
        else nextKind = CE_ONLINE_SEAT_OPEN;

        CeOnlineHostPatch patch;
        patch.hasSeatKind = true;
        patch.targetSeat = sel->seat;
        patch.seatKind = nextKind;
        std::string err;
        if (!mBackend->RequestHostUpdate(patch, &err)) {
            SetStatusNotice(err.empty() ? "Could not update seat." : err, true);
            return false;
        }
        const uint16_t slotNum = static_cast<uint16_t>(sel->seat + 1);
        if (mRosterSelected >= 0 && mRosterSelected < static_cast<int>(mRoom.seats.size())) {
            mRoom.seats[static_cast<size_t>(mRosterSelected)].kind = nextKind;
        }
        const char *kindLabel = (nextKind == CE_ONLINE_SEAT_AI) ? "AI Bot"
                              : (nextKind == CE_ONLINE_SEAT_CLOSED) ? "Closed" : "Open";
        char msg[64];
        snprintf(msg, sizeof(msg), "Slot %u set to %s.", unsigned(slotNum), kindLabel);
        SetStatusNotice(msg, false);
        return true;
    }

    bool RoomHostKickSelected() {
        if (!IsLocalHost() || !mBackend) {
            SetStatusNotice("Only the host can kick players.", true);
            return false;
        }
        const CeOnlineSeatView *sel = SelectedSeat();
        if (!sel || sel->kind != CE_ONLINE_SEAT_HUMAN) {
            SetStatusNotice("Select a human guest seat to kick.", true);
            return false;
        }
        if (sel->seat == mRoom.hostSeat) {
            SetStatusNotice("You cannot kick yourself.", true);
            return false;
        }
        const uint16_t slotNum = static_cast<uint16_t>(sel->seat + 1);
        const std::string kickedName = CeOnlineSanitizeForFont(sel->callsign.empty() ? std::string("player") : sel->callsign);
        CeOnlineHostPatch patch;
        patch.hasKickSeat = true;
        patch.kickSeat = sel->seat;
        std::string err;
        if (!mBackend->RequestHostUpdate(patch, &err)) {
            SetStatusNotice(err.empty() ? "Could not kick player." : err, true);
            return false;
        }
        char msg[96];
        snprintf(msg, sizeof(msg), "Kicked %s from Slot %u.", kickedName.c_str(), unsigned(slotNum));
        SetStatusNotice(msg, false);
        return true;
    }

    bool RoomCopyCode() {
        if (!mRoom.active || mRoom.code.empty()) return false;
        if (mBackend && mBackend->CopyToClipboard(mRoom.code)) {
            SetStatusNotice("Lobby code copied: " + CeOnlineSanitizeForFont(mRoom.code), false);
            return true;
        }
        SetStatusNotice("Share code: " + CeOnlineSanitizeForFont(mRoom.code), false);
        return false;
    }

    bool RoomCanStartMatch(std::string *reasonOut) const {
        if (!mRoom.active) {
            if (reasonOut) *reasonOut = "Not in an online lobby.";
            return false;
        }
        if (!IsLocalHost()) {
            if (reasonOut) *reasonOut = "Waiting for the host to start the match.";
            return false;
        }
        if (mRoom.state != "lobby") {
            if (reasonOut) *reasonOut = "Match is already starting.";
            return false;
        }
        int humanCount = 0;
        int notReadyCount = 0;
        std::string firstNotReady;
        for (size_t i = 0; i < mRoom.seats.size(); ++i) {
            const CeOnlineSeatView &s = mRoom.seats[i];
            if (s.kind == CE_ONLINE_SEAT_HUMAN) {
                ++humanCount;
                if (!s.ready) {
                    ++notReadyCount;
                    if (firstNotReady.empty()) {
                        firstNotReady = CeOnlineSanitizeForFont(s.callsign.empty() ? std::string("a player") : s.callsign);
                    }
                }
            }
        }
        if (humanCount < 2) {
            if (reasonOut) *reasonOut = "Need at least 2 human players to start.";
            return false;
        }
        if (notReadyCount > 0) {
            if (reasonOut) {
                if (notReadyCount == 1) {
                    *reasonOut = "Waiting for " + firstNotReady + " to be Ready.";
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Waiting for %d players to be Ready.", notReadyCount);
                    *reasonOut = buf;
                }
            }
            return false;
        }
        if (mRoom.mode == "versus_2v2") {
            int active0 = 0, active1 = 0;
            for (size_t i = 0; i < mRoom.seats.size(); ++i) {
                const CeOnlineSeatView &s = mRoom.seats[i];
                if (s.kind == CE_ONLINE_SEAT_HUMAN || s.kind == CE_ONLINE_SEAT_AI) {
                    if (s.team == 0) ++active0;
                    else if (s.team == 1) ++active1;
                }
            }
            if (active0 < 1 || active1 < 1) {
                if (reasonOut) {
                    *reasonOut = std::string("Both sides need at least one pilot in ") +
                                 CeOnlineModeDisplayLabel(mRoom.mode, mRoom.maxSeats) + ".";
                }
                return false;
            }
        }
        if (reasonOut) *reasonOut = "All players ready. Choose Start Match.";
        return true;
    }

    bool RoomHostStartMatch() {
        std::string reason;
        if (!RoomCanStartMatch(&reason)) {
            SetStatusNotice(reason, true);
            return false;
        }
        if (!mBackend) {
            SetStatusNotice("Lobby service is not connected.", true);
            return false;
        }
        std::string err;
        if (!mBackend->RequestStartMatch(&err)) {
            SetStatusNotice(err.empty() ? "Could not start match." : err, true);
            return false;
        }
        mPendingStartMatch = true;
        BeginConnecting("Starting Match", "Preparing match ticket and peer links...", false);
        return true;
    }

    void RoomLeave() {
        if (mBackend) mBackend->RequestLeaveRoom();
        mRoom = CeOnlineRoomView();
        mChatComposing = false;
        mChatDraft.clear();
        mLastTicketMatchId.clear();
        mPendingStartMatch = false;
        mLastRoomMap = -1;
        ClearOptimisticRoomState();
        EnterBrowser(CE_ONLINE_PAGE_HOME);
    }

    // -----------------------------------------------------------------------
    // Profile Page
    // -----------------------------------------------------------------------
    void EnterProfile(CeOnlinePage returnTo) {
        mFormReturnPage = returnTo;
        mPage = CE_ONLINE_PAGE_PROFILE;
        mProfileFocus = PROFILE_CALLSIGN_INPUT;
        RefreshProfileFromBackend();
        mProfileCallsignDraft = mProfile.callsign;
        mStatusNotice.clear();
        mStatusIsError = false;
    }

    int ProfileFocus() const { return mProfileFocus; }
    void SetProfileFocus(int idx) {
        if (idx < 0) idx = PROFILE_FOCUS_COUNT - 1;
        if (idx >= PROFILE_FOCUS_COUNT) idx = 0;
        mProfileFocus = idx;
    }
    void ProfileMoveFocus(int delta) { SetProfileFocus(mProfileFocus + delta); }

    const std::string &ProfileCallsignDraft() const { return mProfileCallsignDraft; }
    void SetProfileCallsignDraft(const std::string &s) { mProfileCallsignDraft = s; }

    bool ProfileTypeChar(char c) {
        if (mProfileFocus != PROFILE_CALLSIGN_INPUT) return false;
        if (!CeOnlineIsCallsignChar(static_cast<unsigned char>(c))) {
            SetStatusNotice("Callsign may use only letters, digits, spaces, hyphens and underscores.", true);
            return false;
        }
        if (mProfileCallsignDraft.size() >= CE_ONLINE_UI_CALLSIGN_MAX) {
            SetStatusNotice("Callsign must be 20 characters or fewer.", true);
            return false;
        }
        mProfileCallsignDraft.push_back(c);
        mStatusNotice.clear();
        mStatusIsError = false;
        return true;
    }

    void ProfileBackspace() {
        if (mProfileFocus == PROFILE_CALLSIGN_INPUT && !mProfileCallsignDraft.empty()) {
            mProfileCallsignDraft.erase(mProfileCallsignDraft.size() - 1);
            mStatusNotice.clear();
            mStatusIsError = false;
        }
    }

    bool ProfileSave() {
        std::string normalized, err;
        if (!CeOnlineValidateCallsign(mProfileCallsignDraft, &normalized, &err)) {
            SetStatusNotice(err, true);
            return false;
        }
        if (!mBackend) {
            mProfile.callsign = normalized;
            mProfileCallsignDraft = normalized;
            SetStatusNotice("Callsign updated locally.", false);
            return true;
        }
        if (!mBackend->SaveCallsign(normalized, &err)) {
            SetStatusNotice(err.empty() ? "Could not save profile." : err, true);
            return false;
        }
        mProfile.callsign = normalized;
        mProfileCallsignDraft = normalized;
        SetStatusNotice("Profile saved.", false);
        return true;
    }

    void ProfileReconnect() {
        if (mBackend) {
            mBackend->RequestReconnect();
            SetStatusNotice("Reconnecting to lobby server...", false);
        } else {
            SetStatusNotice("Lobby server is not configured.", true);
        }
    }

    // -----------------------------------------------------------------------
    // Connecting Page
    // -----------------------------------------------------------------------
    const std::string &ConnectingHeading() const { return mConnectingHeading; }
    const std::string &ConnectingDetail() const { return mConnectingDetail; }
    bool ConnectingCancellable() const { return mConnectingCancellable; }

    void ConnectingCancel() {
        if (!mConnectingCancellable) return;
        mJoinPendingResolve = false;
        mPendingStartMatch = false;
        if (mBackend) mBackend->RequestLeaveRoom();
        mRoom = CeOnlineRoomView();
        mPage = mConnectReturnPage;
        SetStatusNotice("Cancelled.", false);
    }

    // -----------------------------------------------------------------------
    // Global Navigation & Clipboard Paste
    // -----------------------------------------------------------------------
    void TriggerDirectLan() {
        mRequestedDirectLan = true;
        if (mBackend) mBackend->OnOpenDirectLan();
        Close();
    }

    bool PasteIntoActiveField() {
        if (!mBackend || !CanPasteIntoActiveField()) return false;
        if (mPage == CE_ONLINE_PAGE_JOIN_CODE &&
            (mJoinCodeFocus == JOIN_CODE_INPUT || mJoinCodeFocus == JOIN_PASTE_BTN)) {
            return JoinCodePaste();
        }
        std::string clip;
        if (!mBackend->PasteFromClipboard(&clip) || clip.empty()) return false;
        const bool password = mPage == CE_ONLINE_PAGE_BROWSER ||
            mPage == CE_ONLINE_PAGE_JOIN_CODE ||
            (mPage == CE_ONLINE_PAGE_CREATE && mCreateFocus == CREATE_PASSWORD);
        bool accepted = false;
        bool separator = false;
        for (size_t i = 0; i < clip.size(); ++i) {
            const char c = clip[i];
            if (c == '\r' || c == '\n' || c == '\t') {
                separator = !password && accepted;
                continue;
            }
            if (separator) PasteChar(' ');
            separator = false;
            const bool inserted = PasteChar(c);
            if (!inserted && mPage == CE_ONLINE_PAGE_PROFILE) break;
            accepted = accepted || inserted;
        }
        return accepted;
    }

    bool CanPasteIntoActiveField() const {
        return WantsTextInput() || (mActive && mPage == CE_ONLINE_PAGE_JOIN_CODE &&
                                    mJoinCodeFocus == JOIN_PASTE_BTN);
    }

    bool WantsTextInput() const {
        if (!mActive) return false;
        if (mPage == CE_ONLINE_PAGE_BROWSER) return mPasswordPromptOpen;
        if (mPage == CE_ONLINE_PAGE_CREATE) return (mCreateFocus == CREATE_NAME || mCreateFocus == CREATE_PASSWORD);
        if (mPage == CE_ONLINE_PAGE_JOIN_CODE) return (mJoinCodeFocus == JOIN_CODE_INPUT || mJoinCodeFocus == JOIN_PASSWORD_INPUT);
        if (mPage == CE_ONLINE_PAGE_PROFILE) return (mProfileFocus == PROFILE_CALLSIGN_INPUT);
        if (mPage == CE_ONLINE_PAGE_ROOM) return mChatComposing;
        return false;
    }

    void NavigateBack() {
        if (mPage == CE_ONLINE_PAGE_BROWSER && mPasswordPromptOpen) {
            BrowserPasswordCancel();
            return;
        }
        if (mPage == CE_ONLINE_PAGE_ROOM && mChatComposing) {
            RoomCloseChat();
            return;
        }
        switch (mPage) {
        case CE_ONLINE_PAGE_HOME:
            mRequestedClose = true;
            Close();
            break;
        case CE_ONLINE_PAGE_BROWSER:
            if (mReturnToMultiplayer) {
                mRequestedClose = true;
                Close();
            } else {
                mPage = CE_ONLINE_PAGE_HOME;
                mStatusNotice.clear();
                mStatusIsError = false;
            }
            break;
        case CE_ONLINE_PAGE_CREATE:
        case CE_ONLINE_PAGE_JOIN_CODE:
        case CE_ONLINE_PAGE_PROFILE:
            mPage = mFormReturnPage;
            mStatusNotice.clear();
            mStatusIsError = false;
            break;
        case CE_ONLINE_PAGE_ROOM:
            RoomLeave();
            break;
        case CE_ONLINE_PAGE_CONNECTING:
            ConnectingCancel();
            break;
        }
    }

private:
    void UpdateBrowserResults() {
        std::string err;
        std::vector<CeOnlineUiRoomSummary> latest;
        bool loading = false;
        if (mBackend->GetLobbies(&latest, &loading, &err)) {
            mRooms.swap(latest);
            ClampBrowserSelection();
            NormalizeBrowserPagerFocus();
        }
        mBrowserLoading = loading;
        if (!err.empty() || !loading) mBrowserFetchError = CeOnlineSanitizeForFont(err);
    }

    bool PasteChar(char c) {
        switch (mPage) {
        case CE_ONLINE_PAGE_BROWSER:   return BrowserPasswordTypeChar(c);
        case CE_ONLINE_PAGE_CREATE:    return CreateTypeChar(c);
        case CE_ONLINE_PAGE_JOIN_CODE: return JoinCodeTypeChar(c);
        case CE_ONLINE_PAGE_PROFILE:   return ProfileTypeChar(c);
        case CE_ONLINE_PAGE_ROOM:      return RoomChatTypeChar(c);
        default: return false;
        }
    }

    void RefreshProfileFromBackend() {
        if (mBackend) {
            CeOnlineProfileView p;
            if (mBackend->GetProfile(&p)) mProfile = p;
        }
    }

    void ClearOptimisticRoomState() {
        mHasOptimisticMap = false;
        mOptimisticMapIndex = 0;
        mOptimisticMapLevel = 850;
        mOptimisticMode.clear();
        mOptimisticMaxSeats = CE_ONLINE_UI_ACTIVE_MAX_SLOTS;
        mOptimisticBaseRev = 0;
        mOptimisticSetMs = 0;
    }

    void SelectDefaultRosterSlot() {
        if (IsLocalHost() && mRoom.seats.size() > 1) {
            int chosen = 1;
            for (size_t i = 0; i < mRoom.seats.size(); ++i) {
                if (mRoom.seats[i].seat != mRoom.hostSeat) {
                    chosen = static_cast<int>(i);
                    break;
                }
            }
            mRosterSelected = chosen;
        } else {
            mRosterSelected = 0;
        }
        ClampRosterSelection();
    }

    void RefreshBeConfigsForMap(int level) {
        mLastRoomMap = level;
        std::vector<std::string> cfgs;
        if (mBackend) mBackend->GetBeConfigsForMap(level, &cfgs);
        if (cfgs.empty()) {
            CeOnlineDefaultBeConfigsForMap(level, &cfgs);
        }
        mAvailableBeConfigs.swap(cfgs);
    }

    void RevalidateLocalBeConfig() {
        CeOnlineSeatView *me = LocalSeatMutable();
        if (!me || mAvailableBeConfigs.empty() || me->beCfg.empty()) return;
        for (size_t i = 0; i < mAvailableBeConfigs.size(); ++i) {
            if (mAvailableBeConfigs[i] == me->beCfg) return;
        }
        const std::string oldBe = me->beCfg;
        const std::string fallbackBe = mAvailableBeConfigs[0];
        me->beCfg = fallbackBe;
        if (mBackend && mRoom.state == "lobby") {
            std::string err;
            mBackend->RequestUpdateSelf(me->team, fallbackBe, me->ready, &err);
        }
        if (mStatusNotice.empty() || !mStatusIsError) {
            SetStatusNotice("BE config " + CeOnlineSanitizeForFont(oldBe) +
                            " unavailable on this map; switched to " + fallbackBe + ".", false);
        }
    }

    std::string DefaultBeConfigForMap(int level) {
        RefreshBeConfigsForMap(level);
        return mAvailableBeConfigs.empty() ? std::string("Standard") : mAvailableBeConfigs[0];
    }

    void BeginConnecting(const std::string &heading, const std::string &detail, bool cancellable) {
        mConnectReturnPage = (mPage == CE_ONLINE_PAGE_CONNECTING) ? mConnectReturnPage : mPage;
        mPage = CE_ONLINE_PAGE_CONNECTING;
        mConnectingHeading = CeOnlineSanitizeForFont(heading);
        mConnectingDetail = CeOnlineSanitizeForFont(detail);
        mConnectingCancellable = cancellable;
    }

    bool SubmitJoinRoom(const std::string &roomId, const std::string &password, int map) {
        if (!mBackend) {
            SetStatusNotice("Lobby service is not connected.", true);
            return false;
        }
        std::string err;
        if (!mBackend->RequestJoinRoom(roomId, password, DefaultBeConfigForMap(map), &err)) {
            SetStatusNotice(err.empty() ? "Could not join lobby." : err, true);
            return false;
        }
        BeginConnecting("Joining Lobby", "Connecting to lobby...", true);
        return true;
    }

    void ClampBrowserSelection() {
        const int n = static_cast<int>(FilteredRooms().size());
        if (n <= 0) {
            mBrowserSelected = 0;
            mBrowserPageOffset = 0;
            return;
        }
        if (mBrowserSelected >= n) mBrowserSelected = n - 1;
        if (mBrowserSelected < 0) mBrowserSelected = 0;
        mBrowserPageOffset = (mBrowserSelected / CE_ONLINE_UI_BROWSER_PAGE_SIZE) * CE_ONLINE_UI_BROWSER_PAGE_SIZE;
    }

    void ClampRosterSelection() {
        const int n = static_cast<int>(mRoom.seats.size());
        if (n <= 0) {
            mRosterSelected = 0;
            mRosterPageOffset = 0;
            return;
        }
        if (mRosterSelected >= n) mRosterSelected = n - 1;
        if (mRosterSelected < 0) mRosterSelected = 0;
        mRosterPageOffset = (mRosterSelected / CE_ONLINE_UI_ROSTER_PAGE_SIZE) * CE_ONLINE_UI_ROSTER_PAGE_SIZE;
    }

    ICeOnlineUiBackend *mBackend;
    bool mActive;
    bool mReturnToMultiplayer;
    bool mRequestedClose;
    bool mRequestedDirectLan;

    CeOnlinePage mPage;
    CeOnlinePage mFormReturnPage;
    CeOnlinePage mConnectReturnPage;
    int mHomeFocus;
    uint16_t mMaxSeatsCap;

    CeOnlineProfileView mProfile;
    std::string mStatusNotice;
    bool mStatusIsError;

    // Browser
    std::vector<CeOnlineUiRoomSummary> mRooms;
    std::string mBrowserFetchError;
    bool mBrowserLoading;
    bool mCompatibleOnly;
    int mBrowserSelected;
    int mBrowserPageOffset;
    int mBrowserZone;
    int mBrowserToolbarFocus;
    int mBrowserPagerFocus;
    int mBrowserFooterFocus;
    bool mPasswordPromptOpen;
    std::string mPasswordPromptRoomId;
    std::string mPasswordPromptRoomName;
    int mPasswordPromptMap;
    int mPasswordModalFocus;
    std::string mPasswordPromptInput;

    // Create Lobby
    std::string mCreateRoomName;
    int mCreateMapIndex;
    int mCreateModeIndex;
    uint16_t mCreateMaxSeats;
    bool mCreatePublic;
    std::string mCreatePassword;
    bool mCreateRelayOnly;
    bool mCreateAiFill;
    int mCreateFocus;

    // Join by Code
    std::string mJoinCode;
    std::string mJoinPassword;
    int mJoinCodeFocus;
    bool mJoinPendingResolve;

    // Room
    CeOnlineRoomView mRoom;
    int mRosterSelected;
    int mRosterPageOffset;
    int mRoomZone;
    int mRoomHeaderFocus;
    int mRoomPlayerFocus;
    int mRoomHostFocus;
    bool mChatComposing;
    std::string mChatDraft;
    std::set<std::string> mMutedPlayers;
    std::vector<std::string> mAvailableBeConfigs;
    std::string mLastTicketMatchId;

    // Profile
    std::string mProfileCallsignDraft;
    int mProfileFocus;

    // Connecting
    std::string mConnectingHeading;
    std::string mConnectingDetail;
    bool mConnectingCancellable;
    bool mPendingStartMatch;
    int mLastRoomMap;
    uint64_t mLastNowMs;

    // Optimistic host map/mode/maxSeats state for rapid Next Map / Mode clicks
    bool mHasOptimisticMap;
    int mOptimisticMapIndex;
    int mOptimisticMapLevel;
    std::string mOptimisticMode;
    uint16_t mOptimisticMaxSeats;
    uint32_t mOptimisticBaseRev;
    uint64_t mOptimisticSetMs;
};

#endif // CE_ONLINE_UI_MODEL_H
