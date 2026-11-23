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

    FakeOnlineBackend() {
        profile.playerId = "0123456789abcdefghjkmnpqrs";
        profile.callsign = "AcePilot";
        profile.tag = "4F9A";
        profile.matchesPlayed = 14;
        profile.matchesWon = 9;
        profile.serverUrl = "https://lobby.onslaught-ce.example";
        profile.connStatus = CE_ONLINE_CONN_ONLINE;
        profile.pingMs = 28;
        mapBeConfigs.push_back("Aquila-Mk1");
        mapBeConfigs.push_back("Aquila-Mk2");
        mapBeConfigs.push_back("Valkyrie");
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
        if (patch.hasMap) room.map = patch.map;
        if (patch.hasMode) room.mode = patch.mode;
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
        (void)level;
        *out = mapBeConfigs;
    }

    virtual void OnOpenDirectLan() { ++directLanCalls; }
    virtual void OnMatchTicketReady(const CeOnlineStartTicketView &ticket) {
        ++ticketReadyCalls;
        lastTicketMatchId = ticket.matchId;
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
        r.push_back(CeOnlineLayout::FooterBar());
        r.push_back(CeOnlineLayout::RoomInfoBar());
        r.push_back(CeOnlineLayout::RoomCopyCodeBtn());
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
    CHECK(mpBar.x0 >= 110.f && mpBar.x1 <= 530.f && mpBar.y0 >= 404.f && mpBar.y1 <= 450.f,
          "Multiplayer entry bar clears grid and help bar");
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
}

static void TestCreateLobbyAndJoinCode() {
    FakeOnlineBackend be;
    CeOnlineUiModel ui;
    ui.SetBackend(&be);
    ui.Open(false);
    ui.HomeActivate(CeOnlineUiModel::HOME_CREATE);
    CHECK(ui.Page() == CE_ONLINE_PAGE_CREATE, "entered Create Lobby");

    // Verify all 18 maps (850..866 + 100) are cycled cleanly
    std::set<int> seenLevels;
    std::set<std::string> seenNames;
    for (int i = 0; i < CE_ONLINE_MAP_COUNT; ++i) {
        seenLevels.insert(ui.CreateMapLevel());
        seenNames.insert(ui.CreateMapName());
        ui.CreateCycleMap(1);
    }
    CHECK(seenLevels.size() == 18 && seenNames.size() == 18, "all 18 maps present with unique names");
    CHECK(seenLevels.count(100) == 1, "map 100 (Tutorial) included");
    for (int lvl = 850; lvl <= 866; ++lvl) {
        CHECK(seenLevels.count(lvl) == 1, "maps 850..866 all included");
    }

    // Verify all 4 modes
    std::set<std::string> seenModes;
    for (int i = 0; i < CE_ONLINE_MODE_COUNT; ++i) {
        seenModes.insert(ui.CreateModeWire());
        ui.CreateCycleMode(1);
    }
    CHECK(seenModes.size() == 4, "all 4 protocol modes cycled");

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
          "Direct P2P shares your IP with the host. Relay Only hides your IP via TURN.",
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

    // Solo host cannot start match (even when Ready!)
    std::string reason;
    CHECK(!ui.RoomCanStartMatch(&reason), "cannot start with 1 human");
    CHECK(reason.find("2 human") != std::string::npos, "reason explains >= 2 humans required");
    CHECK(!ui.RoomHostStartMatch() && be.startCalls == 0, "Start Match blocked with 1 human");

    // Toggle Ready, Cycle Side, Cycle BE config (using real map configs from backend)
    CHECK(ui.RoomToggleReady() && be.room.seats[0].ready, "host toggled Ready");
    CHECK(ui.RoomCycleSide(1) && be.room.seats[0].team == 1, "side cycled Forseti -> Muspell");
    CHECK(ui.RoomCycleBeConfig(1) && be.room.seats[0].beCfg == "Aquila-Mk2", "BE config cycled to Aquila-Mk2");
    CHECK(ui.RoomCycleBeConfig(1) && be.room.seats[0].beCfg == "Valkyrie", "BE config cycled to Valkyrie");
    ui.SyncFromBackend(150);

    // Still blocked with 1 human even when host is Ready
    CHECK(!ui.RoomCanStartMatch(&reason) && !ui.RoomHostStartMatch() && be.startCalls == 0, "still blocked with 1 ready human");

    // Host toggles non-human seat 2: AI -> Closed -> Open -> AI
    ui.RosterSelectIndex(2);
    CHECK(ui.RoomHostCycleSelectedSeatKind() && be.room.seats[2].kind == CE_ONLINE_SEAT_CLOSED, "seat 2 AI -> Closed");
    ui.SyncFromBackend(160);
    CHECK(ui.RoomHostCycleSelectedSeatKind() && be.room.seats[2].kind == CE_ONLINE_SEAT_OPEN, "seat 2 Closed -> Open");
    ui.SyncFromBackend(170);
    CHECK(ui.RoomHostCycleSelectedSeatKind() && be.room.seats[2].kind == CE_ONLINE_SEAT_AI, "seat 2 Open -> AI");
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

int main() {
    TestFontSafety();
    TestGeometryBounds();
    TestProfileAndHome();
    TestBrowserPagingFilterPassword();
    TestCreateLobbyAndJoinCode();
    TestRoom4SlotAnd64SlotAndGuardrails();
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
     '"Direct P2P shares your IP with the host. Relay Only hides your IP via TURN.";',
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
]


def compile_and_run(work_dir: Path, header_text: str, std_flag: str, sanitize: bool) -> tuple:
    work_dir.mkdir(parents=True, exist_ok=True)
    (work_dir / "ce_online_ui_model.h").write_text(header_text, encoding="utf-8")
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
        p14, out14 = compile_and_run(root / "cpp14", header, "-std=c++14", True)
        report(p14, f"C++14 ASan/UBSan full model/geometry/font suite ({out14})", out14)

        # 2. C++17 + ASan/UBSan
        p17, out17 = compile_and_run(root / "cpp17", header, "-std=c++17", True)
        report(p17, f"C++17 ASan/UBSan full model/geometry/font suite ({out17})", out17)

        # 3. Causal mutants (each must fail)
        for name, old, new in MUTANTS:
            if header.count(old) != 1:
                report(False, f"mutant {name}: anchor count={header.count(old)} (expected 1)")
                continue
            mutated = header.replace(old, new, 1)
            mpass, mout = compile_and_run(root / ("mut-" + name), mutated, "-std=c++14", False)
            report(not mpass, f"mutant {name} is caught", mout if mpass else "")

    print("ce_online_ui_tests: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
