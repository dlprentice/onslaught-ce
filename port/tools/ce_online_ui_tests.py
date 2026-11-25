#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host ASan/UBSan test suite + causal mutants for port/game/ce_online_ui_model.h.

    python3 -B port/tools/ce_online_ui_tests.py

Compiles and runs a standalone C++14/17 test driver with -fsanitize=address,undefined
exercising every page, transition, pagination path (20+ & 64+ rooms, 4-slot & 64-slot
roster), validation rule, mute/chat ring, host guardrail, BEA bitmap-font whitelist,
and 640x480 safe-band non-overlap geometry invariant. Also runs 8 causal mutants
against copies of ce_online_ui_model.h and verifies every mutant is caught.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MODEL_H = ROOT / "port/game/ce_online_ui_model.h"
UI_H = ROOT / "port/game/ce_online_ui.h"
BRIDGE_CPP = ROOT / "port/game/ce_online_ui_bridge.cpp"
UI_CPP = ROOT / "port/game/ce_online_ui.cpp"

DRIVER_CPP = r'''// SPDX-License-Identifier: GPL-3.0-only
#include "ce_online_ui_model.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

static int gChecks = 0;
static int gFails = 0;

static void Check(bool cond, const char *what, int line) {
    ++gChecks;
    if (!cond) {
        ++gFails;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    }
}
#define CHECK(c, w) Check((c), (w), __LINE__)

class FakeOnlineBackend : public ICeOnlineUiBackend {
public:
    CeOnlineProfileView profile;
    std::vector<CeOnlineUiRoomSummary> lobbies;
    bool lobbiesLoading = false;
    std::string lobbiesErr;
    CeOnlineRoomView room;
    CeOnlineUiResolveResult pendingResolve;
    bool hasPendingResolve = false;

    int pollCalls = 0;
    int saveCallsignCalls = 0;
    int reconnectCalls = 0;
    int refreshCalls = 0;
    bool lastIncludeIncompatible = false;
    int createCalls = 0;
    CeOnlineCreateParams lastCreate;
    int resolveCalls = 0;
    std::string lastResolvedCode;
    int joinCalls = 0;
    std::string lastJoinRoomId, lastJoinPassword, lastJoinBeCfg;
    int leaveCalls = 0;
    int flushLeaveCalls = 0;
    uint64_t lastFlushLeaveNowMs = 0;
    int updateSelfCalls = 0;
    int hostUpdateCalls = 0;
    CeOnlineHostPatch lastHostPatch;
    int sendChatCalls = 0;
    std::string lastChatSent;
    int startCalls = 0;
    int directLanCalls = 0;
    int ticketReadyCalls = 0;
    std::string lastTicketMatchId;

    std::string clipboard;
    bool clipboardWorks = true;
    std::vector<std::string> mapBeConfigs;
    bool usePerMapDefaultBeConfigs = false;
    bool deferHostUpdates = false;
    int lastHostPatchMap = -1;

    FakeOnlineBackend() {
        profile.playerId = "0123456789abcdefghjkmnpqrs";
        profile.callsign = "AcePilot";
        profile.tag = "4F9A";
        profile.matchesPlayed = 14;
        profile.matchesWon = 9;
        profile.serverUrl = "https://onslaught-ce-lobby.operator-demo.workers.dev";
        profile.connStatus = CE_ONLINE_CONN_ONLINE;
        profile.pingMs = 28;
        mapBeConfigs.push_back("Standard");
        mapBeConfigs.push_back("Sniper");
        mapBeConfigs.push_back("Laser");
        mapBeConfigs.push_back("Blaster");
    }

    virtual void Poll(uint64_t) { ++pollCalls; }

    virtual bool GetProfile(CeOnlineProfileView *out) {
        *out = profile;
        return true;
    }

    virtual bool SaveCallsign(const std::string &callsign, std::string *errOut) {
        ++saveCallsignCalls;
        std::string norm;
        if (!CeOnlineValidateCallsign(callsign, &norm, errOut)) return false;
        profile.callsign = norm;
        return true;
    }

    virtual void RequestReconnect() { ++reconnectCalls; }

    virtual void RequestRefreshLobbies(bool includeIncompatible) {
        ++refreshCalls;
        lastIncludeIncompatible = includeIncompatible;
    }

    virtual bool GetLobbies(std::vector<CeOnlineUiRoomSummary> *outRooms,
                            bool *loadingOut, std::string *errOut) {
        if (outRooms) *outRooms = lobbies;
        if (loadingOut) *loadingOut = lobbiesLoading;
        if (errOut) *errOut = lobbiesErr;
        return true;
    }

    virtual bool RequestCreateRoom(const CeOnlineCreateParams &params, std::string *errOut) {
        (void)errOut;
        ++createCalls;
        lastCreate = params;
        room = CeOnlineRoomView();
        room.active = true;
        room.roomId = "rm_created001";
        room.code = "XY789Z";
        room.rev = 1;
        room.state = "lobby";
        room.name = params.name;
        room.topology = params.topology;
        room.mode = params.mode;
        room.map = params.map;
        room.maxSeats = params.maxSeats;
        room.hostSeat = 0;
        room.localSeat = 0;
        room.visibility = params.visibility;
        room.hasPassword = !params.password.empty();
        room.requireRelay = params.requireRelay;
        room.aiFill = params.aiFill;
        room.seats.clear();
        for (uint16_t i = 0; i < params.maxSeats; ++i) {
            CeOnlineSeatView s;
            s.seat = i;
            if (i == 0) {
                s.kind = CE_ONLINE_SEAT_HUMAN;
                s.playerId = profile.playerId;
                s.callsign = profile.callsign;
                s.tag = profile.tag;
                s.team = 0;
                s.beCfg = params.beCfg;
                s.ready = false;
                s.rttMs = 28;
                s.route = params.requireRelay ? CE_ONLINE_ROUTE_RELAY : CE_ONLINE_ROUTE_DIRECT;
            } else {
                s.kind = params.aiFill ? CE_ONLINE_SEAT_AI : CE_ONLINE_SEAT_OPEN;
                s.team = (i % 2);
                s.beCfg = params.beCfg;
            }
            room.seats.push_back(s);
        }
        return true;
    }

    virtual bool RequestResolveCode(const std::string &code, std::string *errOut) {
        (void)errOut;
        ++resolveCalls;
        lastResolvedCode = code;
        return true;
    }

    virtual bool TakeResolveResult(CeOnlineUiResolveResult *out) {
        if (!hasPendingResolve) return false;
        hasPendingResolve = false;
        *out = pendingResolve;
        return true;
    }

    virtual bool RequestJoinRoom(const std::string &roomId, const std::string &password,
                                 const std::string &beCfg, std::string *errOut) {
        (void)errOut;
        ++joinCalls;
        lastJoinRoomId = roomId;
        lastJoinPassword = password;
        lastJoinBeCfg = beCfg;
        room.active = true;
        room.roomId = roomId;
        room.state = "lobby";
        return true;
    }

    virtual void RequestLeaveRoom() {
        ++leaveCalls;
        room.active = false;
    }

    virtual bool GetRoom(CeOnlineRoomView *outRoom) {
        if (!room.active) return false;
        *outRoom = room;
        return true;
    }

    virtual bool RequestUpdateSelf(int team, const std::string &beCfg, bool ready, std::string *errOut) {
        (void)errOut;
        ++updateSelfCalls;
        for (size_t i = 0; i < room.seats.size(); ++i) {
            if (room.seats[i].seat == room.localSeat) {
                room.seats[i].team = team;
                room.seats[i].beCfg = beCfg;
                room.seats[i].ready = ready;
            }
        }
        ++room.rev;
        return true;
    }

    virtual bool RequestHostUpdate(const CeOnlineHostPatch &patch, std::string *errOut) {
        (void)errOut;
        ++hostUpdateCalls;
        lastHostPatch = patch;
        if (patch.hasMap) lastHostPatchMap = patch.map;
        if (deferHostUpdates) {
            return true;
        }
        if (patch.hasMap) room.map = patch.map;
        if (patch.hasMode) room.mode = patch.mode;
        if (patch.hasMaxSeats) {
            if (patch.maxSeats < room.maxSeats) {
                room.seats.resize(patch.maxSeats);
            } else {
                for (uint16_t i = static_cast<uint16_t>(room.seats.size()); i < patch.maxSeats; ++i) {
                    CeOnlineSeatView s;
                    s.seat = i;
                    s.kind = room.aiFill ? CE_ONLINE_SEAT_AI : CE_ONLINE_SEAT_OPEN;
                    s.team = (room.mode == "versus_2v2") ? (i % 2) : 0;
                    room.seats.push_back(s);
                }
            }
            room.maxSeats = patch.maxSeats;
        }
        if (patch.hasSeatKind && patch.targetSeat < room.seats.size()) {
            room.seats[patch.targetSeat].kind = patch.seatKind;
        }
        if (patch.hasKickSeat && patch.kickSeat < room.seats.size()) {
            room.seats[patch.kickSeat].kind = CE_ONLINE_SEAT_OPEN;
            room.seats[patch.kickSeat].playerId.clear();
            room.seats[patch.kickSeat].callsign.clear();
            room.seats[patch.kickSeat].ready = false;
        }
        ++room.rev;
        return true;
    }

    virtual bool RequestSendChat(const std::string &text, std::string *errOut) {
        (void)errOut;
        ++sendChatCalls;
        lastChatSent = text;
        CeOnlineUiChatEntry e;
        e.seq = static_cast<uint32_t>(room.chat.size() + 1);
        e.seat = room.localSeat;
        e.playerId = profile.playerId;
        e.callsign = profile.callsign;
        e.tag = profile.tag;
        e.text = text;
        room.chat.push_back(e);
        return true;
    }

    virtual bool RequestStartMatch(std::string *errOut) {
        (void)errOut;
        ++startCalls;
        room.state = "starting";
        room.startTicket.valid = true;
        room.startTicket.matchId = "mt_0001000200030004";
        room.startTicket.seed = 424242u;
        room.startTicket.level = room.map;
        room.startTicket.mode = room.mode;
        room.startTicket.topology = room.topology;
        room.startTicket.requireRelay = room.requireRelay;
        return true;
    }

    virtual bool CopyToClipboard(const std::string &text) {
        if (!clipboardWorks) return false;
        clipboard = text;
        return true;
    }

    virtual bool PasteFromClipboard(std::string *out) {
        if (!clipboardWorks || clipboard.empty()) return false;
        *out = clipboard;
        return true;
    }

    virtual void GetBeConfigsForMap(int level, std::vector<std::string> *out) {
        if (usePerMapDefaultBeConfigs) {
            CeOnlineDefaultBeConfigsForMap(level, out);
            return;
        }
        *out = mapBeConfigs;
    }

    virtual void OnOpenDirectLan() { ++directLanCalls; }
    virtual void OnMatchTicketReady(const CeOnlineStartTicketView &ticket) {
        ++ticketReadyCalls;
        lastTicketMatchId = ticket.matchId;
    }
    virtual void FlushLeave(uint64_t nowMs) {
        ++flushLeaveCalls;
        lastFlushLeaveNowMs = nowMs;
    }
    std::string pendingOpError;
    virtual bool TakeOpError(std::string *out) {
        if (pendingOpError.empty()) return false;
        if (out) *out = pendingOpError;
        pendingOpError.clear();
        return true;
    }
};

static void CheckNonOverlappingSet(const std::vector<CeOnlineRect> &rects, const char *pageName) {
    for (size_t i = 0; i < rects.size(); ++i) {
        CHECK(rects[i].InSafeBand(), pageName);
        for (size_t j = i + 1; j < rects.size(); ++j) {
            CHECK(!CeOnlineRect::Overlaps(rects[i], rects[j]), pageName);
        }
    }
}

static void TestFontSafety() {
    const char *forbidden = "$@[\\]^_`{|}~";
    for (const char *p = forbidden; *p; ++p) {
        CHECK(!CeOnlineFontCharSafe(static_cast<unsigned char>(*p)), "forbidden BEA font glyph rejected");
    }
    CHECK(!CeOnlineFontCharSafe(0x00), "NUL rejected");
    CHECK(!CeOnlineFontCharSafe(0x1f), "control char rejected");
    CHECK(!CeOnlineFontCharSafe(0x7f), "DEL rejected");
    CHECK(!CeOnlineFontCharSafe(0x80), "high byte rejected");
    CHECK(CeOnlineFontCharSafe('#'), "# allowed for Callsign#TAG");
    CHECK(CeOnlineFontCharSafe(' '), "space allowed");
    CHECK(CeOnlineFontCharSafe('-'), "hyphen allowed");

    std::string dirty = "Ace_Pilot@100% [Test] {a|b}~\\^$`";
    std::string clean = CeOnlineSanitizeForFont(dirty);
    CHECK(CeOnlineStringIsFontSafe(clean), "sanitized string is 100% font-safe");
    CHECK(clean.find("Ace-Pilot") != std::string::npos, "underscore maps cleanly to hyphen");

    // Verify all static labels, hints, titles, modes, maps, and privacy notice are font-safe
    CHECK(CeOnlineStringIsFontSafe(kCeOnlinePrivacyNotice), "privacy notice is font-safe");
    for (int p = 0; p <= static_cast<int>(CE_ONLINE_PAGE_CONNECTING); ++p) {
        CHECK(CeOnlineStringIsFontSafe(CeOnlinePageTitle(static_cast<CeOnlinePage>(p))), "page title font-safe");
    }
    for (int h = 0; h < CeOnlineLayout::kHomeRowCount; ++h) {
        CHECK(CeOnlineStringIsFontSafe(CeOnlineUiModel::HomeRowLabel(h)), "home label font-safe");
        CHECK(CeOnlineStringIsFontSafe(CeOnlineUiModel::HomeRowHint(h)), "home hint font-safe");
    }
    for (int m = 0; m < CE_ONLINE_MODE_COUNT; ++m) {
        CHECK(CeOnlineStringIsFontSafe(CeOnlineModeAt(m).label), "mode label font-safe");
    }
    for (int i = 0; i < CE_ONLINE_MAP_COUNT; ++i) {
        CHECK(CeOnlineStringIsFontSafe(CeOnlineMapAt(i).name), "map name font-safe");
    }
}

static void TestGeometryBounds() {
    // Shared header & footer
    CHECK(CeOnlineLayout::HeaderBar().InSafeBand(), "header in safe band");
    CHECK(CeOnlineLayout::FooterBar().InSafeBand(), "footer in safe band");
    CHECK(!CeOnlineRect::Overlaps(CeOnlineLayout::HeaderBar(), CeOnlineLayout::FooterBar()), "header/footer disjoint");

    // Home page
    {
        std::vector<CeOnlineRect> r;
        r.push_back(CeOnlineLayout::HeaderBar());
        r.push_back(CeOnlineLayout::FooterBar());
        for (int i = 0; i < CeOnlineLayout::kHomeRowCount; ++i) r.push_back(CeOnlineLayout::HomeRow(i));
        CheckNonOverlappingSet(r, "Home layout in 640x480 safe band without overlaps");
    }
    // Browser page (main controls + 6 rows)
    {
        std::vector<CeOnlineRect> r;
        r.push_back(CeOnlineLayout::HeaderBar());
        r.push_back(CeOnlineLayout::FooterBar());
        r.push_back(CeOnlineLayout::BrowserFilterBtn());
        r.push_back(CeOnlineLayout::BrowserRefreshBtn());
        r.push_back(CeOnlineLayout::BrowserCreateBtn());
        r.push_back(CeOnlineLayout::BrowserJoinCodeBtn());
        r.push_back(CeOnlineLayout::BrowserTableHeader());
        for (int i = 0; i < CE_ONLINE_UI_BROWSER_PAGE_SIZE; ++i) r.push_back(CeOnlineLayout::BrowserRow(i));
        r.push_back(CeOnlineLayout::BrowserPrevBtn());
        r.push_back(CeOnlineLayout::BrowserPageBadge());
        r.push_back(CeOnlineLayout::BrowserNextBtn());
        r.push_back(CeOnlineLayout::BrowserJoinBtn());
        r.push_back(CeOnlineLayout::BrowserProfileBtn());
        r.push_back(CeOnlineLayout::BrowserLanBtn());
        r.push_back(CeOnlineLayout::BrowserBackBtn());
        CheckNonOverlappingSet(r, "Browser layout in 640x480 safe band without overlaps");
        CHECK(CeOnlineLayout::BrowserPwdModal().InSafeBand(), "password modal in safe band");
        CHECK(CeOnlineLayout::BrowserPwdSubmit().InSafeBand(), "password submit in safe band");
        CHECK(CeOnlineLayout::BrowserPwdCancel().InSafeBand(), "password cancel in safe band");
        CHECK(!CeOnlineRect::Overlaps(CeOnlineLayout::BrowserPwdSubmit(), CeOnlineLayout::BrowserPwdCancel()), "pwd buttons disjoint");
    }
    // Create page
    {
        std::vector<CeOnlineRect> r;
        r.push_back(CeOnlineLayout::HeaderBar());
        r.push_back(CeOnlineLayout::FooterBar());
        for (int i = 0; i < CeOnlineLayout::kCreateFieldCount; ++i) r.push_back(CeOnlineLayout::CreateFieldRow(i));
        r.push_back(CeOnlineLayout::CreatePrivacyBox());
        r.push_back(CeOnlineLayout::CreateSubmitBtn());
        r.push_back(CeOnlineLayout::CreateBackBtn());
        CheckNonOverlappingSet(r, "Create layout in 640x480 safe band without overlaps");
    }
    // Join Code page
    {
        std::vector<CeOnlineRect> r;
        r.push_back(CeOnlineLayout::HeaderBar());
        r.push_back(CeOnlineLayout::FooterBar());
        r.push_back(CeOnlineLayout::JoinCodeField());
        r.push_back(CeOnlineLayout::JoinPwdField());
        r.push_back(CeOnlineLayout::JoinPasteBtn());
        r.push_back(CeOnlineLayout::JoinSubmitBtn());
        r.push_back(CeOnlineLayout::JoinBackBtn());
        CheckNonOverlappingSet(r, "JoinCode layout in 640x480 safe band without overlaps");
    }
    // Room page
    {
        std::vector<CeOnlineRect> r;
        r.push_back(CeOnlineLayout::HeaderBar());
        r.push_back(CeOnlineLayout::RoomFooterBar());
        r.push_back(CeOnlineLayout::RoomInfoBar());
        r.push_back(CeOnlineLayout::RoomCopyCodeBtn());
        r.push_back(CeOnlineLayout::RoomMapBar());
        r.push_back(CeOnlineLayout::RoomRosterHeader());
        r.push_back(CeOnlineLayout::RoomPagePrevBtn());
        r.push_back(CeOnlineLayout::RoomPageNextBtn());
        for (int i = 0; i < CE_ONLINE_UI_ROSTER_PAGE_SIZE; ++i) r.push_back(CeOnlineLayout::RoomSeatRow(i));
        r.push_back(CeOnlineLayout::RoomReadyBtn());
        r.push_back(CeOnlineLayout::RoomSideBtn());
        r.push_back(CeOnlineLayout::RoomBeBtn());
        r.push_back(CeOnlineLayout::RoomMuteBtn());
        r.push_back(CeOnlineLayout::RoomLeaveBtn());
        r.push_back(CeOnlineLayout::RoomMapBtn());
        r.push_back(CeOnlineLayout::RoomModeBtn());
        r.push_back(CeOnlineLayout::RoomSlotBtn());
        r.push_back(CeOnlineLayout::RoomKickBtn());
        r.push_back(CeOnlineLayout::RoomStartBtn());
        r.push_back(CeOnlineLayout::RoomChatLog());
        r.push_back(CeOnlineLayout::RoomChatCompose());
        r.push_back(CeOnlineLayout::RoomChatSendBtn());
        CheckNonOverlappingSet(r, "Room layout in 640x480 safe band without overlaps");
        CHECK(CeOnlineLayout::RoomRosterHeader().x0 == 84.f && CeOnlineLayout::RoomRosterHeader().x1 == 556.f,
              "Room roster header spans full 84..556 width");
        CHECK(CeOnlineLayout::RoomMapBtn().x1 == CeOnlineLayout::RoomReadyBtn().x1 &&
              CeOnlineLayout::RoomModeBtn().x1 == CeOnlineLayout::RoomSideBtn().x1 &&
              CeOnlineLayout::RoomSlotBtn().x1 == CeOnlineLayout::RoomBeBtn().x1,
              "Player and Host button rows share first three column stops");
    }
    // Profile page
    {
        std::vector<CeOnlineRect> r;
        r.push_back(CeOnlineLayout::HeaderBar());
        r.push_back(CeOnlineLayout::FooterBar());
        r.push_back(CeOnlineLayout::ProfileCallsignBox());
        r.push_back(CeOnlineLayout::ProfileStatsPanel());
        r.push_back(CeOnlineLayout::ProfileSaveBtn());
        r.push_back(CeOnlineLayout::ProfileReconnectBtn());
        r.push_back(CeOnlineLayout::ProfileBackBtn());
        CheckNonOverlappingSet(r, "Profile layout in 640x480 safe band without overlaps");
    }
    // Connecting page
    {
        std::vector<CeOnlineRect> r;
        r.push_back(CeOnlineLayout::HeaderBar());
        r.push_back(CeOnlineLayout::FooterBar());
        r.push_back(CeOnlineLayout::ConnectingPanel());
        r.push_back(CeOnlineLayout::ConnectingCancelBtn());
        CheckNonOverlappingSet(r, "Connecting layout in 640x480 safe band without overlaps");
    }
    // Multiplayer entry bar clears level grid (y<=400) and help text (y>=455)
    CeOnlineRect mpBar = CeOnlineLayout::MultiplayerEntryBar();
    CHECK(mpBar.x0 == 125.f && mpBar.x1 == 515.f && mpBar.y0 == 412.f && mpBar.y1 == 442.f,
          "Multiplayer entry bar aligned to 125..515 x 412..442");
}

static void TestProfileAndHome() {
    FakeOnlineBackend be;
    CeOnlineUiModel ui;
    ui.SetBackend(&be);

    // Open from Network Game -> Home
    ui.Open(false);
    CHECK(ui.Active() && ui.Page() == CE_ONLINE_PAGE_HOME && !ui.ReturnToMultiplayer(), "opens on Home");

    // Enter Profile
    ui.HomeActivate(CeOnlineUiModel::HOME_PROFILE);
    CHECK(ui.Page() == CE_ONLINE_PAGE_PROFILE, "home navigates to Profile");
    CHECK(ui.Profile().DisplayIdentity() == "AcePilot#4F9A", "profile identity formatted");
    CHECK(ui.Profile().PingLabel() == "28ms", "profile ping formatted");
    CHECK(ui.Profile().matchesPlayed == 14 && ui.Profile().matchesWon == 9, "profile stats loaded");

    // Invalid char rejection on type & save
    CHECK(!ui.ProfileTypeChar('@'), "invalid char @ rejected on typing");
    CHECK(!ui.ProfileTypeChar('$'), "invalid char $ rejected on typing");
    CHECK(!ui.ProfileTypeChar('!'), "invalid char ! rejected on typing");
    ui.SetProfileCallsignDraft("Bad@Name");
    CHECK(!ui.ProfileSave() && be.saveCallsignCalls == 0, "invalid char @ rejected on save");
    ui.SetProfileCallsignDraft("ab");
    CHECK(!ui.ProfileSave() && be.saveCallsignCalls == 0, "too-short callsign rejected");
    ui.SetProfileCallsignDraft("123456789012345678901");
    CHECK(!ui.ProfileSave() && be.saveCallsignCalls == 0, "too-long callsign rejected");

    // Valid callsign with underscore + spaces (collapses spaces, renders font-safe without '_')
    ui.SetProfileCallsignDraft("  Nova_Wing  7 ");
    CHECK(ui.ProfileSave() && be.saveCallsignCalls == 1, "valid callsign saved");
    CHECK(be.profile.callsign == "Nova_Wing 7", "callsign normalized preserving underscore for wire");
    CHECK(CeOnlineStringIsFontSafe(ui.Profile().DisplayIdentity()), "DisplayIdentity sanitizes underscore for BEA font");
    CHECK(ui.Profile().DisplayIdentity() == "Nova-Wing 7#4F9A", "underscore shown as hyphen");

    ui.ProfileReconnect();
    CHECK(be.reconnectCalls == 1, "reconnect invoked");

    ui.NavigateBack();
    CHECK(ui.Page() == CE_ONLINE_PAGE_HOME, "back from profile returns to Home");

    // Direct LAN button
    ui.HomeActivate(CeOnlineUiModel::HOME_DIRECT_LAN);
    CHECK(!ui.Active() && ui.TakeRequestedDirectLan() && be.directLanCalls == 1, "Direct LAN closes online UI and signals LAN");

    // Open from Multiplayer -> jumps directly to Browser, Back closes back to Multiplayer
    ui.Open(true);
    CHECK(ui.Active() && ui.Page() == CE_ONLINE_PAGE_BROWSER && ui.ReturnToMultiplayer(), "multiplayer entry opens Browser");
    ui.NavigateBack();
    CHECK(!ui.Active() && ui.TakeRequestedClose(), "back from multiplayer-opened Browser requests close back to Multiplayer");
}

static void TestBrowserPagingFilterPassword() {
    FakeOnlineBackend be;
    for (int i = 0; i < 20; ++i) {
        CeOnlineUiRoomSummary r;
        char id[32], code[16], name[48];
        snprintf(id, sizeof(id), "rm_%04d", i);
        snprintf(code, sizeof(code), "RM%04d", i);
        snprintf(name, sizeof(name), "Lobby Room %02d", i + 1);
        r.roomId = id;
        r.code = code;
        r.name = name;
        r.map = CeOnlineMapAt(i % CE_ONLINE_MAP_COUNT).level;
        r.mode = "versus_2v2";
        r.humanSeats = (i == 5) ? 4 : 1;
        r.usedSeats = r.humanSeats;
        r.maxSeats = 4;
        r.hasPassword = (i == 2);
        r.requireRelay = (i % 3 == 0);
        r.compatible = (i < 18); // 18 compatible, 2 incompatible
        r.state = "lobby";
        be.lobbies.push_back(r);
    }

    CeOnlineUiModel ui;
    ui.SetBackend(&be);
    ui.Open(false);
    ui.HomeActivate(CeOnlineUiModel::HOME_BROWSE);
    CHECK(ui.Page() == CE_ONLINE_PAGE_BROWSER, "entered browser");
    CHECK(ui.CompatibleOnly(), "compatible filter defaults on");
    CHECK(ui.FilteredRooms().size() == 18, "compatible filter hides 2 incompatible rooms");
    CHECK(ui.BrowserPageCount() == 3 && ui.BrowserCurrentPage() == 1, "18 rooms = 3 pages of 6");

    // Paging forward and backward across 3 pages
    ui.BrowserNextPage();
    CHECK(ui.BrowserCurrentPage() == 2 && ui.BrowserSelectedIndex() == 6, "page 2 starts at index 6");
    ui.BrowserNextPage();
    CHECK(ui.BrowserCurrentPage() == 3 && ui.BrowserSelectedIndex() == 12, "page 3 starts at index 12");
    ui.BrowserNextPage(); // clamp at last page
    CHECK(ui.BrowserCurrentPage() == 3, "next page clamps at page 3");
    ui.BrowserPrevPage();
    CHECK(ui.BrowserCurrentPage() == 2 && ui.BrowserSelectedIndex() == 6, "prev page returns to page 2");
    ui.BrowserPrevPage();
    CHECK(ui.BrowserCurrentPage() == 1 && ui.BrowserSelectedIndex() == 0, "prev page returns to page 1");

    // Arrow move crosses page boundary
    ui.BrowserSelectIndex(5);
    ui.BrowserMoveSelection(1);
    CHECK(ui.BrowserSelectedIndex() == 6 && ui.BrowserCurrentPage() == 2, "down from index 5 advances to page 2");

    // Full room refused
    ui.BrowserSelectIndex(5);
    CHECK(!ui.BrowserJoinSelected() && be.joinCalls == 0, "full room refused");

    // Password-protected room opens inline prompt
    ui.BrowserSelectIndex(2);
    CHECK(ui.BrowserJoinSelected() && ui.PasswordPromptOpen() && be.joinCalls == 0, "password room opens prompt");
    CHECK(ui.WantsTextInput(), "password prompt requests text input");
    ui.BrowserPasswordTypeChar('s');
    ui.BrowserPasswordTypeChar('e');
    ui.BrowserPasswordTypeChar('c');
    ui.BrowserPasswordBackspace();
    ui.BrowserPasswordTypeChar('c');
    ui.BrowserPasswordTypeChar('r');
    ui.BrowserPasswordTypeChar('e');
    ui.BrowserPasswordTypeChar('t');
    // Cancel works cleanly without leaving browser
    ui.NavigateBack();
    CHECK(!ui.PasswordPromptOpen() && ui.Page() == CE_ONLINE_PAGE_BROWSER, "Esc closes password prompt cleanly");

    // Re-open and submit password
    CHECK(ui.BrowserJoinSelected() && ui.PasswordPromptOpen(), "re-opened password prompt");
    const char *pw = "pass42";
    for (const char *p = pw; *p; ++p) ui.BrowserPasswordTypeChar(*p);
    CHECK(ui.BrowserPasswordSubmit() && be.joinCalls == 1, "password submit calls RequestJoinRoom");
    CHECK(be.lastJoinRoomId == "rm_0002" && be.lastJoinPassword == "pass42", "correct roomId and password sent");
    CHECK(ui.Page() == CE_ONLINE_PAGE_CONNECTING, "transitions to Connecting");
    ui.SyncFromBackend(100);
    CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM, "sync transitions to Room");

    // Leave back to browser & toggle compatible filter off -> 20 rooms (4 pages), incompatible join refused
    ui.RoomLeave();
    CHECK(ui.Page() == CE_ONLINE_PAGE_BROWSER, "leave returns to browser");
    ui.BrowserToggleCompatibleFilter();
    CHECK(!ui.CompatibleOnly() && ui.FilteredRooms().size() == 20 && ui.BrowserPageCount() == 4, "all 20 rooms across 4 pages");
    ui.BrowserSelectIndex(18);
    CHECK(!ui.BrowserJoinSelected() && be.joinCalls == 1, "incompatible room refused");

    // Scale test: 64 rooms paginate across 11 pages
    be.lobbies.clear();
    for (int i = 0; i < 64; ++i) {
        CeOnlineUiRoomSummary r;
        r.roomId = "rm_big";
        r.name = "Assault Room";
        r.maxSeats = 64;
        r.humanSeats = 12;
        r.compatible = true;
        r.state = "lobby";
        be.lobbies.push_back(r);
    }
    ui.BrowserRefresh();
    CHECK(ui.FilteredRooms().size() == 64 && ui.BrowserPageCount() == 11, "64 rooms paginate across 11 pages");
    ui.BrowserSelectIndex(63);
    CHECK(ui.BrowserCurrentPage() == 11, "64th room is on page 11");
    ui.SetBrowserPagerFocus(CeOnlineUiModel::BROWSER_PG_PREV);
    ui.BrowserPrevPage();
    CHECK(ui.BrowserPagerFocus() == CeOnlineUiModel::BROWSER_PG_PREV,
          "pager keeps focus while the previous button remains enabled");
    be.lobbies.resize(1);
    ui.SyncFromBackend(110);
    CHECK(ui.BrowserCurrentPage() == 1 &&
          ui.BrowserPagerFocus() == CeOnlineUiModel::BROWSER_PG_JOIN,
          "a shrinking browser moves focus to an enabled pager button");
    be.lobbies.clear();
    ui.SyncFromBackend(120);
    CHECK(ui.BrowserZoneFocus() == CeOnlineUiModel::BROWSER_ZONE_TOOLBAR,
          "an empty browser moves focus off the disabled pager");
}

static void TestBrowserErrorsAndDraftLifetime() {
    FakeOnlineBackend be;
    CeOnlineUiModel ui;
    ui.SetBackend(&be);
    be.lobbiesErr = "Could not fetch lobbies.";
    ui.Open(true);
    CHECK(ui.StatusIsError() && ui.StatusNotice() == be.lobbiesErr,
          "browser reports a fetch error");
    be.lobbiesErr.clear();
    ui.SyncFromBackend(100);
    CHECK(!ui.StatusIsError() && ui.StatusNotice().empty(),
          "a successful fetch clears the fetch error");
    be.pendingOpError = "Incorrect lobby password.";
    be.lobbiesErr = "Could not fetch lobbies.";
    ui.SyncFromBackend(110);
    CHECK(ui.StatusIsError() && ui.StatusNotice() == "Incorrect lobby password.",
          "a fetch error cannot overwrite an action error");
    be.lobbiesErr.clear();
    ui.SyncFromBackend(120);
    ui.BrowserRefresh();
    CHECK(ui.StatusIsError() && ui.StatusNotice() == "Incorrect lobby password.",
          "polling and manual refresh preserve the action error");

    ui.EnterJoinCode(CE_ONLINE_PAGE_BROWSER);
    ui.SetJoinCodeFocus(CeOnlineUiModel::JOIN_PASTE_BTN);
    be.clipboard = "\r\nab-cd12\t";
    CHECK(ui.CanPasteIntoActiveField() && ui.PasteIntoActiveField() && ui.JoinCodeInput() == "ABCD12",
          "Ctrl+V works on the Paste Code button");
    be.clipboard = "$";
    CHECK(!ui.PasteIntoActiveField() && ui.JoinCodeInput() == "ABCD12",
          "invalid code paste reports failure and preserves the previous code");
    ui.SetJoinCodeFocus(CeOnlineUiModel::JOIN_PASSWORD_INPUT);
    be.clipboard = "sec\r\nret\t";
    CHECK(ui.PasteIntoActiveField() && ui.JoinPasswordInput() == "secret",
          "password paste strips clipboard line breaks and tabs");

    for (int exitKind = 0; exitKind < 3; ++exitKind) {
        ui.Open(false);
        ui.EnterCreate(CE_ONLINE_PAGE_HOME);
        ui.SetCreateRoomName("Draft lifetime");
        CHECK(ui.CreateSubmit(), "created a room for the draft lifetime check");
        ui.SyncFromBackend(130 + 20 * exitKind);
        ui.RoomOpenChat();
        be.clipboard = "\r\n\t";
        CHECK(!ui.PasteIntoActiveField() && ui.ChatDraft().empty(),
              "control-only chat paste accepts no text");
        be.clipboard = "$@";
        CHECK(!ui.PasteIntoActiveField() && ui.ChatDraft().empty(),
              "unsupported chat paste accepts no text");
        be.clipboard = "hello\r\nworld\t";
        CHECK(ui.PasteIntoActiveField() && ui.ChatDraft() == "hello world",
              "chat paste normalizes embedded clipboard whitespace");
        be.clipboard.assign(CE_ONLINE_UI_CHAT_MAX, 'a');
        CHECK(ui.PasteIntoActiveField() && ui.ChatDraft().size() == CE_ONLINE_UI_CHAT_MAX &&
              !ui.PasteIntoActiveField(), "paste reports whether the draft had any remaining space");
        if (exitKind == 0) {
            be.room.state = "closed";
        } else if (exitKind == 1) {
            be.room.active = false;
        } else {
            ui.Open(false);
        }
        ui.SyncFromBackend(140 + 20 * exitKind);
        CHECK(!ui.ChatComposing() && ui.ChatDraft().empty(),
              "room closure, disconnect, and Online open discard the prior draft");
        if (exitKind == 1) {
            ui.SyncFromBackend(175);
            CHECK(ui.StatusIsError() && ui.StatusNotice() == "Disconnected from lobby.",
                  "disconnect feedback persists across the next browser update");
        }
    }
}

static void TestCreateLobbyAndJoinCode() {
    FakeOnlineBackend be;
    CeOnlineUiModel ui;
    ui.SetBackend(&be);
    ui.Open(false);
    ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
    CHECK(ui.Page() == CE_ONLINE_PAGE_CREATE, "entered Create Lobby");

    // Verify all 16 verified retail online maps (strict single-mode mapping; 100 and 858 excluded!)
    std::set<int> seenLevels;
    std::set<std::string> seenNames;
    std::set<std::string> seenModes;
    for (int i = 0; i < CE_ONLINE_MAP_COUNT; ++i) {
        seenLevels.insert(ui.CreateMapLevel());
        seenNames.insert(ui.CreateMapName());
        seenModes.insert(ui.CreateModeWire());
        CHECK(CeOnlineMapSupportsMode(ui.CreateMapLevel(),
                                      static_cast<CeOnlineMode>(ui.CreateModeIndex())),
              "CreateCycleMap always keeps map+mode valid");
        ui.CreateCycleMap(1);
    }
    CHECK(seenLevels.size() == 16 && seenNames.size() == 16, "all 16 retail online maps present with unique names");
    CHECK(seenLevels.count(100) == 0 && !CeOnlineMapIsKnown(100), "broken SP map 100 (Tutorial) excluded");
    CHECK(seenLevels.count(858) == 0 && !CeOnlineMapIsKnown(858), "duplicate map 858 excluded");
    CHECK(seenModes.size() == 3, "all 3 active online modes covered across map catalog");

    // Verify strict Map <-> Mode compatibility contract (exact match with fix15-online-core)
    const int kCoopSkirmishMaps[] = { 850, 853, 859, 864, 865, 866 };
    for (size_t i = 0; i < sizeof(kCoopSkirmishMaps) / sizeof(kCoopSkirmishMaps[0]); ++i) {
        const int lvl = kCoopSkirmishMaps[i];
        CHECK(CeOnlineMapSupportsMode(lvl, CE_ONLINE_MODE_COOP_SKIRMISH), "skirmish map allows coop_skirmish");
        CHECK(!CeOnlineMapSupportsMode(lvl, CE_ONLINE_MODE_VERSUS_2V2), "skirmish map refuses versus_2v2");
        CHECK(!CeOnlineMapSupportsMode(lvl, CE_ONLINE_MODE_COOP_DEFENCE), "skirmish map refuses coop_defence");
    }
    const int kVersusMaps[] = { 851, 855, 860, 861, 862, 863, 852, 854, 857 };
    for (size_t i = 0; i < sizeof(kVersusMaps) / sizeof(kVersusMaps[0]); ++i) {
        const int lvl = kVersusMaps[i];
        CHECK(CeOnlineMapSupportsMode(lvl, CE_ONLINE_MODE_VERSUS_2V2), "versus map allows versus_2v2");
        CHECK(!CeOnlineMapSupportsMode(lvl, CE_ONLINE_MODE_COOP_SKIRMISH), "versus map refuses coop_skirmish");
        CHECK(!CeOnlineMapSupportsMode(lvl, CE_ONLINE_MODE_COOP_DEFENCE), "versus map refuses coop_defence");
    }
    CHECK(CeOnlineMapSupportsMode(856, CE_ONLINE_MODE_COOP_DEFENCE), "856 allows coop_defence");
    CHECK(!CeOnlineMapSupportsMode(856, CE_ONLINE_MODE_COOP_SKIRMISH), "856 refuses coop_skirmish");
    CHECK(!CeOnlineMapSupportsMode(856, CE_ONLINE_MODE_VERSUS_2V2), "856 refuses versus_2v2");
    for (int m = 0; m < CE_ONLINE_MODE_COUNT; ++m) {
        CHECK(!CeOnlineMapSupportsMode(100, static_cast<CeOnlineMode>(m)), "100 refuses all online modes");
        CHECK(!CeOnlineMapSupportsMode(858, static_cast<CeOnlineMode>(m)), "858 refuses all online modes");
    }

    // Skirmish 850 stays locked on coop_skirmish
    CHECK(ui.CreateMapLevel() == 850 && std::string(ui.CreateModeWire()) == "coop_skirmish", "default map 850 is coop_skirmish");
    ui.CreateCycleMode(1);
    CHECK(std::string(ui.CreateModeWire()) == "coop_skirmish", "Skirmish map refuses invalid modes");

    // 3-seat Co-op auto-clamps to 4 balanced seats when switching onto a Versus map, and toggles 2v2 <-> 1v1!
    ui.CreateCycleMaxSeats(-1); // 4 -> 3 on Co-op Skirmish
    CHECK(ui.CreateMaxSeats() == 3, "Co-op Skirmish allows 3 seats");
    while (ui.CreateMapLevel() != 851) ui.CreateCycleMap(1);
    CHECK(std::string(ui.CreateModeWire()) == "versus_2v2", "Versus map auto-syncs to versus_2v2");
    CHECK(ui.CreateMaxSeats() == 4 && std::string(ui.CreateModeLabel()) == "2v2 Versus",
          "switching from 3-seat Co-op to Versus clamps to 4 seats (2v2 Versus)");
    ui.CreateCycleMode(1);
    CHECK(ui.CreateMaxSeats() == 2 && std::string(ui.CreateModeLabel()) == "1v1 Versus",
          "CreateCycleMode on Versus toggles 2v2 Versus -> 1v1 Versus (2 seats)");
    ui.CreateCycleMaxSeats(1);
    CHECK(ui.CreateMaxSeats() == 4 && std::string(ui.CreateModeLabel()) == "2v2 Versus",
          "CreateCycleMaxSeats on Versus cycles 2 -> 4 (never odd 3)");
    ui.CreateCycleMaxSeats(1);
    CHECK(ui.CreateMaxSeats() == 2 && std::string(ui.CreateModeLabel()) == "1v1 Versus",
          "CreateCycleMaxSeats on Versus cycles 4 -> 2 (never odd 3)");
    // Building Defence (856) only allows coop_defence mode
    while (ui.CreateMapLevel() != 856) ui.CreateCycleMap(1);
    CHECK(std::string(ui.CreateModeWire()) == "coop_defence" &&
          std::string(ui.CreateModeLabel()) == "Co-op Defence",
          "Building Defence auto-syncs to coop_defence");
    ui.CreateCycleMode(1);
    CHECK(std::string(ui.CreateModeWire()) == "coop_defence", "Building Defence refuses incompatible modes");
    // Return to map 850 and 4 seats for subsequent checks
    while (ui.CreateMapLevel() != 850) ui.CreateCycleMap(1);
    while (ui.CreateMaxSeats() != 4) ui.CreateCycleMaxSeats(1);

    // Verify max players cap (2..4 default, and 64-ready parameterized)
    CHECK(ui.CreateMaxSeats() == 4, "default max seats is 4");
    ui.CreateCycleMaxSeats(1);
    CHECK(ui.CreateMaxSeats() == 2, "wraps 4 -> 2");
    ui.CreateCycleMaxSeats(1);
    CHECK(ui.CreateMaxSeats() == 3, "2 -> 3");
    ui.CreateCycleMaxSeats(1);
    CHECK(ui.CreateMaxSeats() == 4, "3 -> 4");
    ui.SetMaxSeatsCap(64);
    ui.CreateCycleMaxSeats(1);
    CHECK(ui.CreateMaxSeats() == 5, "parameterized cap allows > 4 seats");
    ui.SetMaxSeatsCap(4);
    CHECK(ui.CreateMaxSeats() == 4, "restoring 0.1.14 cap clamps back to 4");

    // Privacy notice + Relay Only toggle
    CHECK(!ui.CreateRelayOnly(), "Relay Only defaults Off");
    CHECK(std::string(ui.CreateRelayOnlyLabel()) == "Off (Direct P2P + TURN)", "Off label matches");
    ui.CreateToggleRelayOnly();
    CHECK(ui.CreateRelayOnly(), "Relay Only toggled On");
    CHECK(std::string(ui.CreateRelayOnlyLabel()) == "On (Relay Only)", "On label matches");
    CHECK(std::string(CeOnlineUiModel::CreatePrivacyNotice()) ==
          "Direct P2P shows your IP to players who join. Relay Only hides your IP via TURN.",
          "exact privacy notice present");

    // Visibility & AI Fill toggles
    ui.CreateToggleVisibility();
    CHECK(!ui.CreateIsPublic() && std::string(ui.CreateVisibilityLabel()) == "Unlisted Code", "visibility unlisted");
    ui.CreateToggleAiFill();
    CHECK(!ui.CreateAiFill() && std::string(ui.CreateAiFillLabel()) == "Off", "AI fill off");

    // Reject bad room name
    ui.SetCreateRoomName("ab");
    CHECK(!ui.CreateSubmit() && be.createCalls == 0, "short room name rejected");
    ui.SetCreateRoomName("Bad_Name");
    CHECK(!ui.CreateSubmit() && be.createCalls == 0, "unsafe font char rejected in room name");

    // Submit valid room
    ui.SetCreateRoomName("Forseti Relay Arena");
    ui.SetCreatePassword("pw99");
    CHECK(ui.CreateSubmit() && be.createCalls == 1, "valid room created");
    CHECK(be.lastCreate.requireRelay && !be.lastCreate.aiFill && be.lastCreate.visibility == "unlisted" &&
          be.lastCreate.password == "pw99", "all create params propagated");
    ui.SyncFromBackend(200);
    CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM && ui.IsLocalHost(), "creator enters Room as host");

    // Leave and test Join by Code + clipboard paste + password resolve
    ui.RoomLeave();
    ui.EnterJoinCode(CE_ONLINE_PAGE_BROWSER);
    CHECK(ui.Page() == CE_ONLINE_PAGE_JOIN_CODE, "entered Join by Code");
    be.clipboard = " ab-cd12 ";
    CHECK(ui.PasteIntoActiveField() && ui.JoinCodeInput() == "ABCD12", "Ctrl+V paste cleans & uppercases 6-char code");
    CHECK(ui.JoinCodeSubmit() && be.resolveCalls == 1 && be.lastResolvedCode == "ABCD12", "resolve requested");
    CHECK(ui.Page() == CE_ONLINE_PAGE_CONNECTING, "resolving shows Connecting");

    // Backend answers: password required
    be.pendingResolve = CeOnlineUiResolveResult();
    be.pendingResolve.resolved = true;
    be.pendingResolve.found = true;
    be.pendingResolve.roomId = "rm_secret";
    be.pendingResolve.hasPassword = true;
    be.pendingResolve.state = "lobby";
    be.hasPendingResolve = true;
    ui.SyncFromBackend(300);
    CHECK(ui.Page() == CE_ONLINE_PAGE_JOIN_CODE && ui.JoinCodeFocus() == CeOnlineUiModel::JOIN_PASSWORD_INPUT,
          "passworded resolved room returns to password field");
}

static void TestRoom4SlotAnd64SlotAndGuardrails() {
    FakeOnlineBackend be;
    CeOnlineUiModel ui;
    ui.SetBackend(&be);
    ui.Open(false);
    ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
    ui.SetCreateRoomName("Squad Room");
    CHECK(ui.CreateSubmit(), "create 4-slot room");
    ui.SyncFromBackend(100);
    CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM && ui.RosterPageCount() == 1, "4-slot room fits on 1 page");

    // On host room entry, default roster selection is Slot #2 (index 1) so Cycle Slot works immediately!
    CHECK(ui.RosterSelectedIndex() == 1, "host entry defaults roster selection to Slot #2 (index 1)");

    // Solo host cannot start match (even when Ready!)
    std::string reason;
    CHECK(!ui.RoomCanStartMatch(&reason), "cannot start with 1 human");
    CHECK(reason.find("2 human") != std::string::npos, "reason explains >= 2 humans required");
    CHECK(!ui.RoomHostStartMatch() && be.startCalls == 0, "Start Match blocked with 1 human");

    // Toggle Ready, Side (refused in co-op, allowed on Versus map), Cycle BE config
    CHECK(ui.RoomToggleReady() && be.room.seats[0].ready, "host toggled Ready");
    CHECK(!ui.RoomCycleSide(1) && be.room.seats[0].team == 0, "side cycle refused in co-op mode");
    CHECK(!ui.RoomHostCanCycleMode() && !ui.RoomHostCycleMode(1), "Co-op map mode is locked and refuses cross-mode switch");
    // Cycle map from Skirmish 1 (index 0) -> Versus 1 (index 6, map 851)
    for (int k = 0; k < 6; ++k) {
        CHECK(ui.RoomHostCycleMap(1), "host advances map");
        ui.SyncFromBackend(105 + k * 2);
    }
    CHECK(be.room.map == 851 && be.room.mode == "versus_2v2", "switching to map 851 auto-switches to versus_2v2");
    CHECK(ui.RoomHostCanCycleMode(), "Versus map enables 2v2 <-> 1v1 mode toggle");
    CHECK(ui.RoomCycleSide(1) && be.room.seats[0].team == 1, "side cycled Forseti -> Muspell in versus_2v2");
    CHECK(ui.RoomCycleBeConfig(1) && be.room.seats[0].beCfg == "Sniper", "BE config cycled to Sniper");
    CHECK(ui.RoomCycleBeConfig(1) && be.room.seats[0].beCfg == "Laser", "BE config cycled to Laser");
    ui.SyncFromBackend(150);

    // Still blocked with 1 human even when host is Ready
    CHECK(!ui.RoomCanStartMatch(&reason) && !ui.RoomHostStartMatch() && be.startCalls == 0, "still blocked with 1 ready human");

    // Host toggles non-human seat 2: AI -> Closed -> Open -> AI (with crisp status notice!)
    ui.RosterSelectIndex(2);
    CHECK(ui.RoomHostCycleSelectedSeatKind() && be.room.seats[2].kind == CE_ONLINE_SEAT_CLOSED, "seat 2 AI -> Closed");
    CHECK(ui.StatusNotice() == "Slot 3 set to Closed.", "Slot 3 Closed notice emitted");
    ui.SyncFromBackend(160);
    CHECK(ui.RoomHostCycleSelectedSeatKind() && be.room.seats[2].kind == CE_ONLINE_SEAT_OPEN, "seat 2 Closed -> Open");
    CHECK(ui.StatusNotice() == "Slot 3 set to Open.", "Slot 3 Open notice emitted");
    ui.SyncFromBackend(170);
    CHECK(ui.RoomHostCycleSelectedSeatKind() && be.room.seats[2].kind == CE_ONLINE_SEAT_AI, "seat 2 Open -> AI");
    CHECK(ui.StatusNotice() == "Slot 3 set to AI Bot.", "Slot 3 AI Bot notice emitted");
    ui.SyncFromBackend(180);

    // Host cannot overwrite human seat 0 or kick self
    ui.RosterSelectIndex(0);
    CHECK(!ui.RoomHostCycleSelectedSeatKind(), "cannot overwrite human seat");
    CHECK(!ui.RoomHostKickSelected(), "cannot kick self");

    // Copy room code
    CHECK(ui.RoomCopyCode() && be.clipboard == "XY789Z", "Copy Room Code copies exact code");

    // Guest joins seat 1 (not ready yet)
    be.room.seats[1].kind = CE_ONLINE_SEAT_HUMAN;
    be.room.seats[1].playerId = "guest_player_1111111111111";
    be.room.seats[1].callsign = "Wing_Two";
    be.room.seats[1].tag = "BEEF";
    be.room.seats[1].ready = false;
    be.room.seats[1].rttMs = 42;
    be.room.seats[1].route = CE_ONLINE_ROUTE_RELAY;
    ui.SyncFromBackend(200);

    CHECK(ui.FormatSeatName(ui.Room().seats[1]) == "Wing-Two#BEEF", "guest name sanitized for BEA font");
    CHECK(ui.FormatSeatPing(ui.Room().seats[1]) == "42ms", "guest ping formatted");
    CHECK(ui.FormatSeatReady(ui.Room().seats[1]) == "WAIT", "guest ready WAIT");

    // Start still blocked because guest is not Ready
    CHECK(!ui.RoomCanStartMatch(&reason), "blocked while guest not Ready");
    CHECK(reason.find("Wing-Two") != std::string::npos, "reason names unready guest");
    CHECK(!ui.RoomHostStartMatch() && be.startCalls == 0, "Start Match refused while guest unready");

    // Lobby chat (35 messages -> bounded ring 32 -> last 6 visible) + Local Mute
    be.room.chat.clear();
    for (int i = 0; i < 35; ++i) {
        CeOnlineUiChatEntry m;
        m.seq = static_cast<uint32_t>(i + 1);
        m.seat = (i % 2 == 0) ? 0 : 1;
        m.playerId = (i % 2 == 0) ? be.profile.playerId : be.room.seats[1].playerId;
        m.callsign = (i % 2 == 0) ? be.profile.callsign : be.room.seats[1].callsign;
        char txt[64];
        snprintf(txt, sizeof(txt), "Message_%d @test", i + 1);
        m.text = txt;
        be.room.chat.push_back(m);
    }
    ui.SyncFromBackend(210);
    CHECK(ui.Room().chat.size() == CE_ONLINE_UI_CHAT_RING, "chat ring bounded to 32");
    std::vector<std::string> vis = ui.VisibleChatLines();
    CHECK(vis.size() == CE_ONLINE_UI_CHAT_VISIBLE, "6 visible chat lines");
    for (size_t i = 0; i < vis.size(); ++i) {
        CHECK(CeOnlineStringIsFontSafe(vis[i]), "every visible chat line is BEA font-safe");
    }

    // Inline chat compose + submit
    ui.RoomOpenChat();
    CHECK(ui.ChatComposing() && ui.WantsTextInput(), "chat compose active");
    const char *msg = "gl hf!";
    for (const char *p = msg; *p; ++p) ui.RoomChatTypeChar(*p);
    CHECK(ui.RoomChatSubmit() && be.sendChatCalls == 1 && be.lastChatSent == "gl hf!", "chat submitted");
    CHECK(!ui.ChatComposing(), "chat compose closed after submit");

    // Mute guest locally -> hides guest messages & adds (Muted) badge
    ui.RosterSelectIndex(1);
    CHECK(ui.RoomToggleMuteSelected(), "muted guest");
    CHECK(ui.FormatSeatName(ui.Room().seats[1]).find("(Muted)") != std::string::npos, "muted badge shown");
    std::vector<std::string> visMuted = ui.VisibleChatLines();
    for (size_t i = 0; i < visMuted.size(); ++i) {
        CHECK(visMuted[i].find("Wing-Two:") == std::string::npos, "muted player messages hidden");
    }
    CHECK(ui.RoomToggleMuteSelected(), "unmuted guest");

    // Host kicks guest -> seat becomes Open -> cannot start again
    CHECK(ui.RoomHostKickSelected() && be.room.seats[1].kind == CE_ONLINE_SEAT_OPEN, "host kicked guest");
    ui.SyncFromBackend(220);
    CHECK(!ui.RoomCanStartMatch(&reason), "after kick, back to 1 human -> cannot start");

    // Re-add guest as READY, also verify guest perspective cannot use host controls
    be.room.seats[1].kind = CE_ONLINE_SEAT_HUMAN;
    be.room.seats[1].playerId = "guest_player_1111111111111";
    be.room.seats[1].callsign = "WingTwo";
    be.room.seats[1].tag = "BEEF";
    be.room.seats[1].ready = true;
    be.room.localSeat = 1; // temporarily inspect as guest
    ui.SyncFromBackend(230);
    CHECK(!ui.IsLocalHost(), "guest recognized as non-host");
    CHECK(!ui.RoomHostCycleMap(1) && !ui.RoomHostCycleMode(1) && !ui.RoomHostStartMatch(),
          "guest refused all host controls");

    // Switch back to host: all 2 humans READY -> Start Match succeeds & emits ticket once!
    be.room.localSeat = 0;
    ui.SyncFromBackend(240);
    CHECK(ui.RoomCanStartMatch(&reason), "2 ready humans can start");
    CHECK(ui.RoomHostStartMatch() && be.startCalls == 1, "host starts match");
    CHECK(ui.Page() == CE_ONLINE_PAGE_CONNECTING && !ui.ConnectingCancellable(), "launching is non-cancellable");
    ui.SyncFromBackend(250);
    ui.SyncFromBackend(260);
    CHECK(be.ticketReadyCalls == 1 && be.lastTicketMatchId == "mt_0001000200030004",
          "OnMatchTicketReady fires exactly once per match_id");
    ui.NavigateBack();
    CHECK(ui.Page() == CE_ONLINE_PAGE_CONNECTING, "Esc cannot cancel active launch handoff");

    // 64-slot paginated Assault-mode room test!
    be.room = CeOnlineRoomView();
    be.room.active = true;
    be.room.roomId = "rm_assault64";
    be.room.code = "ASLT64";
    be.room.state = "lobby";
    be.room.name = "Grand Assault 32v32";
    be.room.topology = "authoritative";
    be.room.maxSeats = 64;
    be.room.hostSeat = 0;
    be.room.localSeat = 0;
    for (uint16_t s = 0; s < 64; ++s) {
        CeOnlineSeatView seat;
        seat.seat = s;
        seat.kind = (s < 32) ? CE_ONLINE_SEAT_HUMAN : CE_ONLINE_SEAT_AI;
        char pid[32], cs[32];
        snprintf(pid, sizeof(pid), "player_%03u", unsigned(s));
        snprintf(cs, sizeof(cs), "Pilot%02u", unsigned(s + 1));
        seat.playerId = pid;
        seat.callsign = cs;
        seat.tag = "AA01";
        seat.team = (s < 32) ? 0 : 1;
        seat.ready = true;
        seat.rttMs = 20 + int(s);
        be.room.seats.push_back(seat);
    }
    ui.Open(false);
    ui.HomeActivate(CeOnlineUiModel::HOME_BROWSE);
    // Simulate entering the 64-slot room directly via connecting sync
    CeOnlineUiRoomSummary sum;
    sum.roomId = "rm_assault64";
    sum.name = "Grand Assault 32v32";
    sum.humanSeats = 32;
    sum.maxSeats = 64;
    sum.compatible = true;
    sum.state = "lobby";
    be.lobbies.clear();
    be.lobbies.push_back(sum);
    ui.BrowserRefresh();
    CHECK(ui.BrowserJoinSelected(), "join 64-slot room");
    ui.SyncFromBackend(500);
    CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM, "entered 64-slot room");
    CHECK(ui.Room().seats.size() == 64 && ui.RosterPageCount() == 16 && ui.RosterCurrentPage() == 1,
          "64 seats paginate cleanly across 16 pages");
    ui.RosterNextPage();
    CHECK(ui.RosterCurrentPage() == 2 && ui.RosterSelectedIndex() == 4, "roster page 2 starts at seat 4");
    ui.RosterSelectIndex(63);
    CHECK(ui.RosterCurrentPage() == 16 && ui.RosterSelectedIndex() == 63, "seat 64 is on page 16");
    CHECK(ui.RoomHostCycleSelectedSeatKind() && be.room.seats[63].kind == CE_ONLINE_SEAT_CLOSED,
          "host can toggle 64th AI seat");
    ui.RosterPrevPage();
    CHECK(ui.RosterCurrentPage() == 15 && ui.RosterSelectedIndex() == 56, "prev page returns to page 15");
}

static void TestLaunchClosePreservationAndGamepadNav() {
    // 1. Closing UI during match launch handoff MUST NOT enqueue POST /leave! (#12)
    {
        FakeOnlineBackend be;
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
        ui.SetCreateRoomName("Launch Protect");
        CHECK(ui.CreateSubmit(), "create room for launch preservation test");
        ui.SyncFromBackend(1000);
        be.room.seats[1].kind = CE_ONLINE_SEAT_HUMAN;
        be.room.seats[1].playerId = "guest_p2_11111111111111111";
        be.room.seats[1].callsign = "GuestTwo";
        be.room.seats[1].tag = "1234";
        be.room.seats[1].ready = true;
        ui.RoomToggleReady();
        ui.SyncFromBackend(1100);
        CHECK(ui.RoomHostStartMatch(), "host starts match");
        ui.SyncFromBackend(1200);
        CHECK(ui.Page() == CE_ONLINE_PAGE_CONNECTING && be.ticketReadyCalls == 1, "launch ticket active");

        // Close(false) while room.state == "starting" must NOT leave!
        ui.Close(false);
        CHECK(be.leaveCalls == 0 && be.flushLeaveCalls == 0,
              "Close() while room state is starting does NOT leave or flush-leave");

        // Re-open in lobby state and test Close(true) (preserveLaunch) vs Close(false)
        be.room.state = "lobby";
        be.room.startTicket = CeOnlineStartTicketView();
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
        ui.SetCreateRoomName("Lobby Leave Test");
        CHECK(ui.CreateSubmit(), "create room for close-leave check");
        ui.SyncFromBackend(2345);
        CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM, "entered room");
        ui.Close(true);
        CHECK(be.leaveCalls == 0 && be.flushLeaveCalls == 0,
              "Close(preserveLaunch=true) never calls RequestLeaveRoom or FlushLeave");

        // Normal Close(false) while still in lobby MUST leave and pass monotonic nowMs (2345) to FlushLeave
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
        ui.SetCreateRoomName("Normal Close Test");
        CHECK(ui.CreateSubmit(), "create room for normal close");
        ui.SyncFromBackend(4567);
        ui.Close(false);
        CHECK(be.leaveCalls == 1 && be.flushLeaveCalls == 1 && be.lastFlushLeaveNowMs == 4567u,
              "normal Close() in lobby leaves room and forwards monotonic timestamp to FlushLeave");
    }

    // 2. Non-cancellable Connecting recovers cleanly if room disappears without launch ticket (#16)
    {
        FakeOnlineBackend be;
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
        ui.SetCreateRoomName("Softlock Guard");
        CHECK(ui.CreateSubmit(), "create room");
        ui.SyncFromBackend(100);
        be.room.seats[1].kind = CE_ONLINE_SEAT_HUMAN;
        be.room.seats[1].playerId = "guest_p2_22222222222222222";
        be.room.seats[1].callsign = "GuestTwo";
        be.room.seats[1].ready = true;
        ui.RoomToggleReady();
        ui.SyncFromBackend(120);
        CHECK(ui.RoomHostStartMatch(), "enter non-cancellable Connecting");
        CHECK(ui.Page() == CE_ONLINE_PAGE_CONNECTING && !ui.ConnectingCancellable(), "in non-cancellable Connecting");
        // Backend drops room before start ticket arrives (no launch queued)
        be.room.active = false;
        ui.SyncFromBackend(200);
        CHECK(ui.Page() == CE_ONLINE_PAGE_BROWSER && ui.ConnectingCancellable(),
              "recovers from non-cancellable Connecting if room disappears without launch ticket");
    }

    // 3. Full Browser & Room keyboard/gamepad zone navigation (#35)
    {
        FakeOnlineBackend be;
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_BROWSE);
        CHECK(ui.Page() == CE_ONLINE_PAGE_BROWSER, "browser opened");
        // Empty browser focuses toolbar (Create Lobby) so gamepad can immediately create or refresh
        CHECK(ui.BrowserZoneFocus() == CeOnlineUiModel::BROWSER_ZONE_TOOLBAR &&
              ui.BrowserToolbarFocus() == CeOnlineUiModel::BROWSER_TB_CREATE,
              "empty browser focuses toolbar Create Lobby");
        ui.BrowserNavSide(-1);
        CHECK(ui.BrowserToolbarFocus() == CeOnlineUiModel::BROWSER_TB_REFRESH, "Left moves to Refresh");
        const int prevRefresh = be.refreshCalls;
        ui.BrowserNavActivate();
        CHECK(be.refreshCalls == prevRefresh + 1, "activating Refresh refreshes lobbies");
        ui.BrowserNavSide(1);
        CHECK(ui.BrowserToolbarFocus() == CeOnlineUiModel::BROWSER_TB_CREATE, "Right moves to Create Lobby");
        ui.BrowserNavActivate();
        CHECK(ui.Page() == CE_ONLINE_PAGE_CREATE, "gamepad activates Create Lobby from Browser toolbar");

        // Create room and verify Room focus zones (Player -> Host -> Chat -> Header -> Roster)
        ui.SetCreateRoomName("Pad Nav Room");
        CHECK(ui.CreateSubmit(), "create room");
        ui.SyncFromBackend(300);
        CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM, "entered room");
        CHECK(ui.RoomZoneFocus() == CeOnlineUiModel::ROOM_ZONE_PLAYER &&
              ui.RoomPlayerFocus() == CeOnlineUiModel::ROOM_PL_READY,
              "room defaults focus to Ready button");
        CHECK(ui.RoomNavActivate(), "gamepad toggles Ready");
        CHECK(be.room.seats[0].ready, "gamepad toggles Ready");
        // In coop_skirmish (map 850) Side is disabled and Slot #2 is AI (Mute disabled):
        // Right from Ready MUST skip disabled Side to BE, then skip disabled Mute to Leave!
        ui.RoomNavSide(1);
        CHECK(ui.RoomPlayerFocus() == CeOnlineUiModel::ROOM_PL_BE, "Right skips disabled Side in coop_skirmish");
        ui.RoomNavSide(1);
        CHECK(ui.RoomPlayerFocus() == CeOnlineUiModel::ROOM_PL_LEAVE, "Right skips disabled Mute for AI seat");
        // Manually parking on disabled Side refuses activation cleanly (suppressing FES_SELECT)
        ui.SetRoomPlayerFocus(CeOnlineUiModel::ROOM_PL_SIDE);
        CHECK(!ui.RoomNavActivate() && be.room.seats[0].team == 0, "disabled Side does not activate in coop_skirmish");
        // Advance to Versus 1 (851) so Side switching is valid under strict map-mode contract
        for (int k = 0; k < 6; ++k) {
            CHECK(ui.RoomHostCycleMap(1), "advance to Versus map 851");
            ui.SyncFromBackend(302 + k);
        }
        CHECK(be.room.map == 851 && be.room.mode == "versus_2v2", "reached Versus map 851");
        ui.SetRoomPlayerFocus(CeOnlineUiModel::ROOM_PL_READY);
        ui.RoomNavSide(1);
        CHECK(ui.RoomPlayerFocus() == CeOnlineUiModel::ROOM_PL_SIDE, "Right lands on enabled Side in versus_2v2");
        CHECK(ui.RoomNavActivate(), "gamepad activates enabled Side");
        CHECK(be.room.seats[0].team == 1, "gamepad switches Side in versus_2v2");
        ui.RoomNavMove(1);
        CHECK(ui.RoomZoneFocus() == CeOnlineUiModel::ROOM_ZONE_HOST, "Down moves to Host strip");
        // Host strip on Versus 851 with AI seat selected: Kick is disabled, Mode & Cycle Slot are enabled
        ui.SetRoomHostFocus(CeOnlineUiModel::ROOM_HS_SLOT);
        ui.RoomNavSide(1);
        CHECK(ui.RoomHostFocus() == CeOnlineUiModel::ROOM_HS_START, "Host Right skips disabled Kick for AI seat");
        ui.SetRoomHostFocus(CeOnlineUiModel::ROOM_HS_KICK);
        CHECK(!ui.RoomNavActivate(), "disabled Kick refuses activation");
        ui.SetRoomHostFocus(CeOnlineUiModel::ROOM_HS_MAP);
        const int oldMap = be.room.map;
        CHECK(ui.RoomNavActivate(), "gamepad changes Map from Host strip");
        CHECK(be.room.map != oldMap, "gamepad changes Map from Host strip");
        ui.RoomNavMove(1);
        CHECK(ui.RoomZoneFocus() == CeOnlineUiModel::ROOM_ZONE_CHAT, "Down from Host strip moves to Chat");
        CHECK(ui.RoomNavActivate(), "gamepad opens Chat compose");
        CHECK(ui.ChatComposing(), "gamepad opens Chat compose");
        ui.RoomCloseChat();
        ui.RoomNavMove(1);
        CHECK(ui.RoomZoneFocus() == CeOnlineUiModel::ROOM_ZONE_HEADER, "Down from Chat wraps to Header");
        // Single-page room: < and > are disabled so Header Side stays on Copy Code
        ui.RoomNavSide(1);
        CHECK(ui.RoomHeaderFocus() == CeOnlineUiModel::ROOM_HD_COPY, "Header skips disabled < and > on single page");
        CHECK(ui.RoomNavActivate(), "gamepad activates Copy Code");
        CHECK(be.clipboard == "XY789Z", "gamepad activates Copy Code");
    }

    // 4. Browser Pager skips disabled < Prev / Next > on single page & refuses disabled activation
    {
        FakeOnlineBackend be;
        CeOnlineUiRoomSummary sum;
        sum.roomId = "rm_single";
        sum.name = "Single Page Room";
        sum.humanSeats = 1;
        sum.maxSeats = 4;
        sum.compatible = true;
        sum.state = "lobby";
        be.lobbies.push_back(sum);
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_BROWSE);
        CHECK(ui.BrowserZoneFocus() == CeOnlineUiModel::BROWSER_ZONE_LIST, "browser starts on list");
        ui.BrowserNavMove(1);
        CHECK(ui.BrowserZoneFocus() == CeOnlineUiModel::BROWSER_ZONE_PAGER &&
              ui.BrowserPagerFocus() == CeOnlineUiModel::BROWSER_PG_JOIN,
              "moving to single-page pager focuses enabled Join Selected");
        ui.BrowserNavSide(1);
        CHECK(ui.BrowserPagerFocus() == CeOnlineUiModel::BROWSER_PG_JOIN, "single-page pager skips disabled Next >");
        ui.BrowserNavSide(-1);
        CHECK(ui.BrowserPagerFocus() == CeOnlineUiModel::BROWSER_PG_JOIN, "single-page pager skips disabled < Prev");
        ui.SetBrowserPagerFocus(CeOnlineUiModel::BROWSER_PG_PREV);
        CHECK(!ui.BrowserNavActivate(), "disabled < Prev refuses activation");
    }
}

static void TestBackNavigationAndStartMatchSnapback() {
    // 1. Create Lobby -> Submit -> Cancel -> Back must pop to HOME / BROWSER (no self-loop!)
    {
        FakeOnlineBackend be;
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
        CHECK(ui.Page() == CE_ONLINE_PAGE_CREATE, "entered Create from Home");
        ui.SetCreateRoomName("Back Loop Test");
        CHECK(ui.CreateSubmit() && ui.Page() == CE_ONLINE_PAGE_CONNECTING, "submitting Create enters Connecting");
        ui.ConnectingCancel();
        CHECK(ui.Page() == CE_ONLINE_PAGE_CREATE, "cancel returns to Create form");
        ui.NavigateBack();
        CHECK(ui.Page() == CE_ONLINE_PAGE_HOME, "Back from Create after cancel returns to Home (no self-loop)");

        // Same from Browser with opError failure instead of manual cancel
        ui.HomeActivate(CeOnlineUiModel::HOME_BROWSE);
        ui.EnterCreate(CE_ONLINE_PAGE_BROWSER);
        CHECK(ui.CreateSubmit() && ui.Page() == CE_ONLINE_PAGE_CONNECTING, "Create from Browser enters Connecting");
        be.room.active = false;
        be.pendingOpError = "Lobby server busy.";
        ui.SyncFromBackend(100);
        CHECK(ui.Page() == CE_ONLINE_PAGE_CREATE, "opError returns to Create form");
        ui.NavigateBack();
        CHECK(ui.Page() == CE_ONLINE_PAGE_BROWSER, "Back from Create after opError returns to Browser");
    }

    // 2. Join by Code -> Submit -> Cancel / Resolve Failure / Password Needed -> Back pops cleanly
    {
        FakeOnlineBackend be;
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_JOIN_CODE);
        be.clipboard = "AB12CD";
        CHECK(ui.PasteIntoActiveField(), "pasted join code");
        CHECK(ui.JoinCodeSubmit() && ui.Page() == CE_ONLINE_PAGE_CONNECTING, "JoinCode enters Connecting");
        ui.ConnectingCancel();
        CHECK(ui.Page() == CE_ONLINE_PAGE_JOIN_CODE, "cancel returns to JoinCode");
        ui.NavigateBack();
        CHECK(ui.Page() == CE_ONLINE_PAGE_HOME, "Back from JoinCode after cancel returns to Home");

        ui.HomeActivate(CeOnlineUiModel::HOME_BROWSE);
        ui.EnterJoinCode(CE_ONLINE_PAGE_BROWSER);
        be.clipboard = "EF34GH";
        CHECK(ui.PasteIntoActiveField(), "pasted second join code");
        CHECK(ui.JoinCodeSubmit() && ui.Page() == CE_ONLINE_PAGE_CONNECTING, "JoinCode from Browser enters Connecting");
        be.pendingResolve = CeOnlineUiResolveResult();
        be.pendingResolve.resolved = true;
        be.pendingResolve.found = false;
        be.pendingResolve.error = "No lobby found.";
        be.hasPendingResolve = true;
        ui.SyncFromBackend(200);
        CHECK(ui.Page() == CE_ONLINE_PAGE_JOIN_CODE, "failed resolve returns to JoinCode");
        ui.NavigateBack();
        CHECK(ui.Page() == CE_ONLINE_PAGE_BROWSER, "Back from JoinCode after failed resolve returns to Browser");
    }

    // 3. StartMatch HTTP in-flight must NOT snap CONNECTING back to ROOM on stale "lobby" poll
    {
        FakeOnlineBackend be;
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
        ui.SetCreateRoomName("Start Snapback");
        CHECK(ui.CreateSubmit(), "create room");
        ui.SyncFromBackend(100);
        be.room.seats[1].kind = CE_ONLINE_SEAT_HUMAN;
        be.room.seats[1].playerId = "guest_p2_33333333333333333";
        be.room.seats[1].callsign = "WingTwo";
        be.room.seats[1].ready = true;
        ui.RoomToggleReady();
        ui.SyncFromBackend(120);
        CHECK(ui.RoomHostStartMatch(), "host initiates StartMatch");
        CHECK(ui.Page() == CE_ONLINE_PAGE_CONNECTING, "entered Connecting for StartMatch");
        // Backend still reports stale room.state == "lobby" while POST /start is in flight!
        be.room.state = "lobby";
        ui.SyncFromBackend(140);
        CHECK(ui.Page() == CE_ONLINE_PAGE_CONNECTING, "stale lobby state does NOT snap Connecting back to Room");
        // If StartMatch later fails with an opError, it cleanly returns to Room
        be.pendingOpError = "Guest unready.";
        ui.SyncFromBackend(160);
        CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM, "StartMatch opError returns cleanly to Room");
    }
}

static void TestV0115OnlineUxCraftAndPrivacy() {
    // 1. Slot 2/3 click -> move focus down to Cycle Slot / Kick without Slot-4 hijack + rapid Slot cycle
    {
        FakeOnlineBackend be;
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
        ui.SetCreateRoomName("Slot Focus Test");
        CHECK(ui.CreateSubmit(), "create room for slot focus test");
        ui.SyncFromBackend(100);
        CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM, "entered room");
        CHECK(ui.RosterSelectedIndex() == 1, "host entry defaults to Slot #2");

        // Click Slot 2 (idx=1), move focus through Player strip down to Host Cycle Slot:
        // selection MUST stay on Slot #2 (index 1), never hijacked to Slot #4 (index 3)!
        ui.RosterSelectIndex(1);
        ui.SetRoomPlayerFocus(CeOnlineUiModel::ROOM_PL_MUTE);
        ui.SetRoomHostFocus(CeOnlineUiModel::ROOM_HS_SLOT);
        CHECK(ui.RosterSelectedIndex() == 1, "moving focus down to Cycle Slot preserves Slot #2");
        ui.RoomNavActivate(); // cycles Slot #2 AI -> Closed
        CHECK(be.room.seats[1].kind == CE_ONLINE_SEAT_CLOSED, "Cycle Slot updated Slot #2 (not Slot #4)");
        CHECK(be.room.seats[3].kind == CE_ONLINE_SEAT_AI, "Slot #4 untouched");
        CHECK(ui.StatusNotice() == "Slot 2 set to Closed.", "crisp Slot #2 status notice");

        // Click Slot 3 (idx=2), move focus down to Host Cycle Slot, rapid-click twice without server sync!
        be.deferHostUpdates = true;
        ui.RosterSelectIndex(2);
        ui.SetRoomHostFocus(CeOnlineUiModel::ROOM_HS_SLOT);
        CHECK(ui.RosterSelectedIndex() == 2, "moving focus down preserves Slot #3");
        CHECK(ui.RoomHostCycleSelectedSeatKind(), "Slot 3 click 1 (AI -> Closed)");
        CHECK(ui.StatusNotice() == "Slot 3 set to Closed.", "Slot 3 Closed notice");
        CHECK(ui.RoomHostCycleSelectedSeatKind(), "Slot 3 click 2 (Closed -> Open)");
        CHECK(ui.StatusNotice() == "Slot 3 set to Open.", "Slot 3 Open notice");
        CHECK(be.lastHostPatch.targetSeat == 2 && be.lastHostPatch.seatKind == CE_ONLINE_SEAT_OPEN,
              "rapid Cycle Slot clicks advance cleanly on Slot #3");
    }

    // 2. Next Map optimistic progression (1 -> 2 -> 3 before server sync) + rollback on opError + 1v1/2v2 toggle
    {
        FakeOnlineBackend be;
        be.usePerMapDefaultBeConfigs = true;
        CeOnlineUiModel ui;
        ui.SetBackend(&be);
        ui.Open(false);
        ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
        ui.SetCreateRoomName("Map Optimistic Test");
        // Start with 3 seats on Skirmish 1 (850) to verify auto-clamp when hitting Versus!
        ui.CreateCycleMaxSeats(-1);
        CHECK(ui.CreateMaxSeats() == 3, "created 3-seat Co-op Skirmish");
        CHECK(ui.CreateSubmit(), "create 3-seat room");
        ui.SyncFromBackend(1000);
        CHECK(ui.Page() == CE_ONLINE_PAGE_ROOM && ui.EffectiveRoomMap() == 850, "room starts on 850");

        // Defer server updates (simulating async HTTP latency) and click Next Map 3 times rapidly!
        be.deferHostUpdates = true;
        CHECK(ui.RoomHostCycleMap(1), "rapid Next Map click 1");
        CHECK(ui.EffectiveRoomMap() == 853, "click 1 advances optimistically to 853");
        CHECK(ui.StatusNotice() == "Map: Skirmish 2 - Canyon Pass (Co-op Skirmish)", "click 1 full map notice");

        CHECK(ui.RoomHostCycleMap(1), "rapid Next Map click 2");
        CHECK(ui.EffectiveRoomMap() == 859, "click 2 advances optimistically to 859 (not collapsed onto 853)");
        CHECK(ui.StatusNotice() == "Map: Skirmish 3 - Twin Islands (Co-op Skirmish)", "click 2 full map notice");

        CHECK(ui.RoomHostCycleMap(1), "rapid Next Map click 3");
        CHECK(ui.EffectiveRoomMap() == 864, "click 3 advances optimistically to 864");
        CHECK(ui.StatusNotice() == "Map: Skirmish 4 - Ridge Assault (Co-op Skirmish)", "click 3 full map notice");
        CHECK(be.lastHostPatchMap == 864, "third PATCH targeted 864");

        // Stale intermediate poll (server still at 853) MUST NOT snap optimistic UI backwards!
        be.room.map = 853;
        be.room.rev = 2;
        ui.SyncFromBackend(1100);
        CHECK(ui.EffectiveRoomMap() == 864, "intermediate stale server poll preserves optimistic 864");

        // opError MUST roll optimistic map cleanly back to server state!
        be.pendingOpError = "Rate limited.";
        ui.SyncFromBackend(1200);
        CHECK(ui.EffectiveRoomMap() == 853, "opError rolls optimistic map back to server map 853");

        // Now advance synchronously from 853 (index 1, 3 seats) to Versus 1 (851, index 6)
        be.deferHostUpdates = false;
        for (int k = 0; k < 5; ++k) {
            CHECK(ui.RoomHostCycleMap(1), "advance toward Versus 1");
            ui.SyncFromBackend(1300 + k * 20);
        }
        CHECK(be.room.map == 851 && be.room.mode == "versus_2v2" && be.room.maxSeats == 4,
              "switching from 3-seat Co-op to Versus 851 auto-clamps maxSeats 3 -> 4");
        CHECK(std::string(ui.EffectiveRoomModeLabel()) == "2v2 Versus", "4-seat Versus labeled 2v2 Versus");

        // Toggle 2v2 Versus <-> 1v1 Versus in Room host controls
        CHECK(ui.RoomHostCycleMode(1), "host toggles 2v2 -> 1v1 Versus");
        CHECK(be.lastHostPatch.hasMaxSeats && be.lastHostPatch.maxSeats == 2, "1v1 patch sends maxSeats=2");
        CHECK(std::string(ui.EffectiveRoomModeLabel()) == "1v1 Versus", "2-seat Versus labeled 1v1 Versus");
        ui.SyncFromBackend(1500);
        CHECK(ui.Room().maxSeats == 2 && ui.Room().seats.size() == 2, "room shrunk to 2 seats");
        CHECK(ui.RoomHostCycleMode(1), "host toggles 1v1 -> 2v2 Versus");
        ui.SyncFromBackend(1550);
        CHECK(ui.Room().maxSeats == 4 && ui.Room().seats.size() == 4, "room expanded back to 4 seats");

        // Verify shrinking 2v2 -> 1v1 is refused when a human occupies Seat 3 (index 2)
        be.room.seats[2].kind = CE_ONLINE_SEAT_HUMAN;
        be.room.seats[2].playerId = "human_seat3";
        be.room.seats[2].callsign = "PilotThree";
        ui.SyncFromBackend(1600);
        CHECK(!ui.RoomHostCycleMode(1) && ui.StatusIsError(),
              "1v1 shrink refused when seat 3 holds a human player");
        be.room.seats[2].kind = CE_ONLINE_SEAT_AI;
        ui.SyncFromBackend(1620);

        // Real retail BE config revalidation: select Sniper on 851, then switch to 856 (Building Defence)
        CHECK(ui.RoomCycleBeConfig(1) && be.room.seats[0].beCfg == "Sniper", "selected Sniper on 851");
        ui.SyncFromBackend(1650);
        const int prevUpdateSelf = be.updateSelfCalls;
        while (be.room.map != 856) {
            CHECK(ui.RoomHostCycleMap(1), "advance to 856 Building Defence");
            ui.SyncFromBackend(1700 + be.hostUpdateCalls * 10);
        }
        CHECK(be.room.map == 856 && be.room.mode == "coop_defence", "reached 856 Building Defence");
        CHECK(be.room.seats[0].beCfg == "Standard" && be.updateSelfCalls > prevUpdateSelf,
              "Sniper auto-revalidated to Standard on 856 (no Sniper in Building Defence)");
        for (size_t i = 0; i < ui.AvailableBeConfigs().size(); ++i) {
            const std::string &cfg = ui.AvailableBeConfigs()[i];
            CHECK(cfg != "Sniper" && cfg != "Vulcan" && cfg != "Pulse" && cfg != "Blazer",
                  "856 BE configs exclude Sniper and all fake names");
        }
    }

    // 3. Pixel-width truncation / ellipsizing (CeOnlineFitTextToWidth never overflows maxW at scale >= 0.65f)
    {
        auto measure75 = [](const std::string &s) -> float {
            return static_cast<float>(s.size()) * 7.5f;
        };
        std::string fitted;
        float sc = 1.f, dw = 0.f;

        // Long player name in tight 90px column MUST ellipsize and NEVER overflow 90px
        CeOnlineFitTextToWidth("Pilot4E6A#4E6A (Host)", 90.f, 0.65f, measure75, &fitted, &sc, &dw, false);
        CHECK(sc >= 0.65f && dw <= 90.01f, "tight player column stays <= maxW at scale >= 0.65f");
        CHECK(fitted.size() >= 3 && fitted.substr(fitted.size() - 3) == "...", "overflowing player name ellipsized");
        CHECK(CeOnlineStringIsFontSafe(fitted), "ellipsized player name is font-safe");

        // Same player name in widened 152px Player column fits WITHOUT ellipsizing!
        CeOnlineFitTextToWidth("Pilot4E6A#4E6A (Host)", 152.f, 0.65f, measure75, &fitted, &sc, &dw, false);
        CHECK(fitted == "Pilot4E6A#4E6A (Host)" && sc >= 0.65f && dw <= 152.01f,
              "widened 152px Player column fits Pilot4E6A#4E6A (Host) cleanly");

        // 160-char chat line fits inside 456px ChatLog without bleeding past right border
        std::string longChat(160, 'W');
        CeOnlineFitTextToWidth(longChat, 456.f, 0.65f, measure75, &fitted, &sc, &dw, false);
        CHECK(sc >= 0.65f && dw <= 456.01f && fitted.substr(fitted.size() - 3) == "...",
              "160-char chat line ellipsizes strictly within 456px");

        // Active input field (keepTail=true) keeps the live typed suffix visible with leading "..."
        CeOnlineFitTextToWidth("Say: hello world this is a very long chat draft ending in XYZ123",
                               180.f, 0.65f, measure75, &fitted, &sc, &dw, true);
        CHECK(sc >= 0.65f && dw <= 180.01f, "active cursor input fits within maxW");
        CHECK(fitted.substr(0, 3) == "..." && fitted.find("XYZ123") != std::string::npos,
              "active cursor input preserves typed tail");
    }

    // 4. Profile URL privacy redaction (NEVER expose raw workers.dev / personal subdomain!)
    {
        std::string dispDefault = CeOnlineFormatServerDisplay(
            "https://onslaught-ce-lobby.operator-demo.workers.dev", false);
        CHECK(dispDefault == "Official Online Service", "built-in default displays Official Online Service");
        CHECK(dispDefault.find("operator-demo") == std::string::npos &&
              dispDefault.find("workers.dev") == std::string::npos &&
              dispDefault.find("https://") == std::string::npos,
              "built-in default never exposes personal workers.dev URL");

        std::string dispCustom = CeOnlineFormatServerDisplay(
            "https://my-custom-lobby.example.com", true);
        CHECK(dispCustom == "Custom Server (ce.ini)", "ce.ini override displays Custom Server (ce.ini)");
        CHECK(dispCustom.find("https://") == std::string::npos, "custom display stays clean");

        std::string dispEmpty = CeOnlineFormatServerDisplay("", false);
        CHECK(dispEmpty == "Not configured", "empty url displays Not configured");
        CHECK(CeOnlineStringIsFontSafe(dispDefault) &&
              CeOnlineStringIsFontSafe(dispCustom) &&
              CeOnlineStringIsFontSafe(dispEmpty),
              "all server display strings are BEA font-safe");
    }
}

int main() {
    TestFontSafety();
    TestGeometryBounds();
    TestProfileAndHome();
    TestBrowserPagingFilterPassword();
    TestBrowserErrorsAndDraftLifetime();
    TestCreateLobbyAndJoinCode();
    TestRoom4SlotAnd64SlotAndGuardrails();
    TestLaunchClosePreservationAndGamepadNav();
    TestBackNavigationAndStartMatchSnapback();
    TestV0115OnlineUxCraftAndPrivacy();
    std::printf("ce_online_ui_model checks=%d failures=%d\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
'''

MUTANTS = [
    ("font-allows-underscore",
     "case '_': case '`': case '{': case '|': case '}': case '~':",
     "case '`': case '{': case '|': case '}': case '~':"),
    ("start-allows-solo",
     "if (humanCount < 2) {",
     "if (humanCount < 1) {"),
    ("start-ignores-unready",
     "if (notReadyCount > 0) {",
     "if (false && notReadyCount > 0) {"),
    ("kick-allows-self",
     "if (sel->seat == mRoom.hostSeat) {",
     "if (false && sel->seat == mRoom.hostSeat) {"),
    ("privacy-notice-altered",
     '"Direct P2P shows your IP to players who join. Relay Only hides your IP via TURN.";',
     '"Direct P2P hides your IP automatically.";'),
    ("browser-filter-ignored",
     "if (mCompatibleOnly && !mRooms[i].compatible) continue;",
     "if (false && mCompatibleOnly && !mRooms[i].compatible) continue;"),
    ("callsign-allows-at",
     "c == ' ' || c == '_' || c == '-';",
     "c == ' ' || c == '_' || c == '-' || c == '@';"),
    ("layout-out-of-bounds",
     "static CeOnlineRect RoomChatCompose()    { return { 84.f,  400.f, 486.f, 420.f }; }",
     "static CeOnlineRect RoomChatCompose()    { return { 84.f,  400.f, 486.f, 460.f }; }"),
    ("map-allows-cross-mode",
     "return CeOnlineMapAt(i).defaultMode == mode;",
     "return true;"),
    ("host-default-slot0",
     "if (IsLocalHost() && mRoom.seats.size() > 1) {",
     "if (false && IsLocalHost() && mRoom.seats.size() > 1) {"),
    ("optimistic-map-ignored",
     "const int cur = mHasOptimisticMap ? mOptimisticMapIndex : CeOnlineFindMapIndex(mRoom.map);",
     "const int cur = CeOnlineFindMapIndex(mRoom.map);"),
    ("profile-leaks-raw-url",
     'return "Official Online Service";',
     "return serverUrl;"),
]


def check_ui_source_guards(report):
    model_txt = MODEL_H.read_text(encoding="utf-8")
    bridge_txt = BRIDGE_CPP.read_text(encoding="utf-8")
    ui_txt = UI_CPP.read_text(encoding="utf-8")

    # 1. Zero fake BE config names across all 3 files
    for fake in ('"Vulcan"', '"Pulse"', '"Blazer"'):
        clean = (fake not in model_txt) and (fake not in bridge_txt) and (fake not in ui_txt)
        report(clean, f"source guard: fake BE config {fake} removed from Online UI")

    # 2. Hover never mutates roster or browser selection in ce_online_ui_bridge.cpp
    no_roster_hover = "if (!composing && mouseMoved && sr.Contains(mx, my))" not in bridge_txt
    no_browser_hover = "if (!modal && mouseMoved && rr.Contains(mx, my))" not in bridge_txt
    report(no_roster_hover and no_browser_hover,
           "source guard: RenderRoom/RenderBrowser hover never overwrites selection")

    # 3. RenderProfile redacts raw serverUrl via CeOnlineFormatServerDisplay
    profile_redacted = ("CeOnlineFormatServerDisplay(prof.serverUrl" in bridge_txt and
                        "CeOnlineTruncateFontSafe(prof.serverUrl" not in bridge_txt)
    report(profile_redacted, "source guard: RenderProfile uses CeOnlineFormatServerDisplay (no raw URL)")

    # 4. ce_online_ui.cpp has pixel-width FitWideTextToWidth, DIK_SPACE handling, and transition guard
    ui_guards = ("FitWideTextToWidth(" in ui_txt and
                 "DIK_SPACE" in ui_txt and
                 "GetTransitionTo() == FEP_MULTIPLAYER" in ui_txt and
                 "GetTransitionFrom() == FEP_MULTIPLAYER" in ui_txt and
                 '"Play Online (2-4 Players)  O key / Y pad"' in ui_txt)
    report(ui_guards, "source guard: ce_online_ui.cpp has pixel-width ellipsizer, DIK_SPACE, MP entry label, and MP transition guards")

    # 5. Domain 3 visual QA guards: zone-aware selection borders, real Start Match enabled state, Next Mode label
    d3_guards = ("rsZone ? kBorderSel : kBorderHover" in bridge_txt and
                 "listZone ? kBorderSel : kBorderHover" in bridge_txt and
                 "(void)rsZone" not in bridge_txt and
                 "(void)listZone" not in bridge_txt and
                 '"Next Mode"' in bridge_txt and
                 "canStart, mx, my, canClick, canStart ? kReady : 0, canStart" in bridge_txt)
    report(d3_guards, "source guard: bridge uses zone-aware selection borders, real canStart enabled state, and Next Mode label")


def compile_and_run(work_dir: Path, header_text: str, std_flag: str, sanitize: bool,
                    ui_header_text: str) -> tuple:
    work_dir.mkdir(parents=True, exist_ok=True)
    (work_dir / "ce_online_ui_model.h").write_text(header_text, encoding="utf-8")
    (work_dir / "ce_online_ui.h").write_text(ui_header_text, encoding="utf-8")
    drv = work_dir / "driver.cpp"
    drv.write_text(DRIVER_CPP, encoding="utf-8")
    exe = work_dir / "driver"
    cxx = shutil.which("g++") or shutil.which("clang++") or "c++"
    cmd = [cxx, std_flag, "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-I", str(work_dir)]
    if sanitize:
        cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    cmd += [str(drv), "-o", str(exe)]
    comp = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if comp.returncode != 0:
        return False, "compile failed: " + comp.stderr
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=1"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20, env=env)
    return run.returncode == 0, (run.stdout + run.stderr).strip()


def main() -> int:
    header = MODEL_H.read_text(encoding="utf-8")
    ui_header = UI_H.read_text(encoding="utf-8")
    ok = True

    def report(passed: bool, label: str, detail: str = ""):
        nonlocal ok
        ok &= bool(passed)
        line = ("PASS " if passed else "FAIL ") + label
        if detail and not passed:
            line += "\n  " + detail
        print(line, flush=True)

    with tempfile.TemporaryDirectory(prefix="ce-online-ui-tests-") as tmp:
        root = Path(tmp)
        # 1. C++14 + ASan/UBSan
        p14, out14 = compile_and_run(root / "cpp14", header, "-std=c++14", True, ui_header)
        report(p14, f"C++14 ASan/UBSan full model/geometry/font suite ({out14})", out14)

        # 2. C++17 + ASan/UBSan
        p17, out17 = compile_and_run(root / "cpp17", header, "-std=c++17", True, ui_header)
        report(p17, f"C++17 ASan/UBSan full model/geometry/font suite ({out17})", out17)

        # 3. Causal mutants (each must fail)
        for name, old, new in MUTANTS:
            count = header.count(old) + ui_header.count(old)
            if count != 1:
                report(False, f"mutant {name}: anchor count={count} (expected 1)")
                continue
            mpass, mout = compile_and_run(root / ("mut-" + name), header.replace(old, new, 1),
                                         "-std=c++14", False, ui_header.replace(old, new, 1))
            report(not mpass, f"mutant {name} is caught", mout if mpass else "")

        # 4. Bridge/UI source-level regression guards
        check_ui_source_guards(report)

    print("ce_online_ui_tests: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
