// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_ui_bridge.cpp: Host-STL (pcxx) owner of CeOnlineUiModel, page layout
// rendering, and ICeOnlineUiBackend -> CeOnlineServiceFacade live adapter.
// Crosses to game-heap qcxx (ce_online_ui.cpp) strictly via plain-C POD callbacks (ce_online_ui_host.h).
#ifndef CE_NETWORK_OFF

#include "ce_online_ui.h"
#include "ce_online_ui_host.h"
#include "ce_online_ui_model.h"
#if defined(CE_NETWORK_SIGNALING)
#include "ce_online_service.h"
#endif

#include <cstdio>
#include <string>
#include <vector>

namespace {

// Palette matching ce_netpages.cpp (plain D3DTOP_MODULATE, full channel strength).
const uint32_t kWhite      = 0xffffffffu;
const uint32_t kCream      = 0xfffdddbdu;
const uint32_t kGrey       = 0xffa8a8a8u;
const uint32_t kDimGrey    = 0xff6e7682u;
const uint32_t kGood       = 0xff2eb030u;
const uint32_t kWarn       = 0xffffcc00u;
const uint32_t kBad        = 0xffdc3c3cu;
const uint32_t kCyan       = 0xff66ccffu;
const uint32_t kMuspell    = 0xffff8866u;
const uint32_t kPanelBg    = 0xc80a1220u;
const uint32_t kHeaderBg   = 0xd80e1a2eu;
const uint32_t kRowBg      = 0x90101a28u;
const uint32_t kRowAltBg   = 0x90142032u;
const uint32_t kRowSelBg   = 0xc8243b5cu;
const uint32_t kBtnBg      = 0xb8162438u;
const uint32_t kBtnHoverBg = 0xd8284060u;
const uint32_t kBtnDisBg   = 0x8010141cu;
const uint32_t kBorder     = 0x90809cb8u;
const uint32_t kBorderSel  = 0xffffcc00u;
const uint32_t kBorderDis  = 0x50505864u;

CeOnlineUiHostOps gOps = {};
bool gHasOps = false;

void DrawPanelBox(const CeOnlineRect &r, uint32_t fillCol, uint32_t borderCol) {
    if (gHasOps && gOps.draw_panel) gOps.draw_panel(r.x0, r.y0, r.x1, r.y1, fillCol, borderCol);
}

void DrawTextLeft(float x, float y, float maxW, uint32_t col, const std::string &text, bool withCursor = false) {
    if (gHasOps && gOps.draw_text_left) gOps.draw_text_left(x, y, maxW, col, text.c_str(), withCursor ? 1 : 0);
}

void DrawTextCenter(const CeOnlineRect &r, uint32_t col, const std::string &text) {
    if (gHasOps && gOps.draw_text_center) gOps.draw_text_center(r.x0, r.y0, r.x1, r.y1, col, text.c_str());
}

void DrawTextRight(float rightX, float y, float maxW, uint32_t col, const std::string &text) {
    if (gHasOps && gOps.draw_text_right) gOps.draw_text_right(rightX, y, maxW, col, text.c_str());
}

bool ClickedRect(const CeOnlineRect &r, bool canClick) {
    return canClick && gHasOps && gOps.mouse_clicked && gOps.mouse_clicked(r.x0, r.y0, r.x1, r.y1) != 0;
}

void PlaySnd(int kind) {
    if (gHasOps && gOps.play_sound) gOps.play_sound(kind);
}

bool DrawButton(const CeOnlineRect &r, const std::string &label, bool selected, bool enabled,
                float mouseX, float mouseY, bool canClick, uint32_t textColOverride = 0) {
    const bool hovered = enabled && r.Contains(mouseX, mouseY);
    const bool lit = enabled && (selected || hovered);
    const uint32_t bg = !enabled ? kBtnDisBg : (lit ? kBtnHoverBg : kBtnBg);
    const uint32_t brd = !enabled ? kBorderDis : (lit ? kBorderSel : kBorder);
    const uint32_t fg = !enabled ? kDimGrey : (textColOverride ? textColOverride : (lit ? kWarn : kWhite));
    DrawPanelBox(r, bg, brd);
    DrawTextCenter(r, fg, label);
    if (enabled && ClickedRect(r, canClick)) {
        PlaySnd(CE_OUI_SND_SELECT);
        return true;
    }
    return false;
}

class CeDefaultOnlineUiBackend : public ICeOnlineUiBackend {
public:
    CeDefaultOnlineUiBackend() {
        mProfile.callsign = "Pilot0000";
        mProfile.tag = "0000";
        mProfile.serverUrl = "Not connected";
        mProfile.connStatus = CE_ONLINE_CONN_OFFLINE;
        mProfile.pingMs = -1;
        mProfile.statusDetail = "Lobby service backend not connected.";
    }

    void Poll(uint64_t) override {}
    bool GetProfile(CeOnlineProfileView *out) override {
        if (!out) return false;
        *out = mProfile;
        return true;
    }
    bool SaveCallsign(const std::string &callsign, std::string *errOut) override {
        std::string norm;
        if (!CeOnlineValidateCallsign(callsign, &norm, errOut)) return false;
        mProfile.callsign = norm;
        return true;
    }
    void RequestReconnect() override {}
    void RequestRefreshLobbies(bool) override {}
    bool GetLobbies(std::vector<CeOnlineUiRoomSummary> *outRooms, bool *loadingOut, std::string *errOut) override {
        if (outRooms) outRooms->clear();
        if (loadingOut) *loadingOut = false;
        if (errOut) errOut->clear();
        return true;
    }
    bool RequestCreateRoom(const CeOnlineCreateParams &, std::string *errOut) override {
        if (errOut) *errOut = "Online lobby service is not connected yet.";
        return false;
    }
    bool RequestResolveCode(const std::string &, std::string *errOut) override {
        if (errOut) *errOut = "Online lobby service is not connected yet.";
        return false;
    }
    bool RequestJoinRoom(const std::string &, const std::string &, const std::string &, std::string *errOut) override {
        if (errOut) *errOut = "Online lobby service is not connected yet.";
        return false;
    }
    void RequestLeaveRoom() override {}
    bool GetRoom(CeOnlineRoomView *outRoom) override {
        if (outRoom) *outRoom = CeOnlineRoomView();
        return false;
    }
    bool RequestUpdateSelf(int, const std::string &, bool, std::string *errOut) override {
        if (errOut) *errOut = "Not connected to a room.";
        return false;
    }
    bool RequestHostUpdate(const CeOnlineHostPatch &, std::string *errOut) override {
        if (errOut) *errOut = "Not connected to a room.";
        return false;
    }
    bool RequestSendChat(const std::string &, std::string *errOut) override {
        if (errOut) *errOut = "Not connected to a room.";
        return false;
    }
    bool RequestStartMatch(std::string *errOut) override {
        if (errOut) *errOut = "Not connected to a room.";
        return false;
    }
    bool CopyToClipboard(const std::string &text) override {
        return gHasOps && gOps.copy_clip && gOps.copy_clip(text.c_str()) != 0;
    }
    bool PasteFromClipboard(std::string *out) override {
        if (!out || !gHasOps || !gOps.paste_clip) return false;
        char buf[256] = {};
        if (!gOps.paste_clip(buf, static_cast<int>(sizeof(buf)))) return false;
        *out = buf;
        return true;
    }
    void GetBeConfigsForMap(int level, std::vector<std::string> *out) override {
        if (!out) return;
        out->clear();
        if (gHasOps && gOps.get_be_config) {
            for (int i = 0; i < 32; ++i) {
                char buf[96] = {};
                if (!gOps.get_be_config(level, i, buf, static_cast<int>(sizeof(buf)))) break;
                if (buf[0]) out->push_back(CeOnlineSanitizeForFont(buf));
            }
        }
        if (out->empty()) {
            ICeOnlineUiBackend::GetBeConfigsForMap(level, out);
        }
    }

private:
    CeOnlineProfileView mProfile;
};

#if defined(CE_NETWORK_SIGNALING)
class CeLiveOnlineUiBackendAdapter final : public ICeOnlineUiBackend {
public:
    void Poll(uint64_t nowMs) override { PortCeOnlineServiceInstance()->Poll(nowMs); }

    bool GetProfile(CeOnlineProfileView *out) override {
        if (!out) return false;
        CeSvcProfile p;
        if (!PortCeOnlineServiceInstance()->GetProfile(&p)) return false;
        out->playerId = p.player_id;
        out->callsign = p.callsign;
        out->tag = p.tag;
        out->matchesPlayed = p.matches_played;
        out->matchesWon = p.matches_won;
        out->serverUrl = p.server_url;
        out->connStatus = static_cast<CeOnlineConnStatus>(p.conn_status);
        out->pingMs = p.ping_ms;
        out->statusDetail = p.status_detail;
        return true;
    }

    bool SaveCallsign(const std::string &callsign, std::string *errOut) override {
        return PortCeOnlineServiceInstance()->SaveCallsign(callsign, errOut);
    }
    void RequestReconnect() override { PortCeOnlineServiceInstance()->RequestReconnect(); }
    void RequestRefreshLobbies(bool inc) override { PortCeOnlineServiceInstance()->RequestRefreshLobbies(inc); }

    bool GetLobbies(std::vector<CeOnlineUiRoomSummary> *outRooms,
                    bool *loadingOut, std::string *errOut) override {
        std::vector<CeSvcRoomSummary> raw;
        if (!PortCeOnlineServiceInstance()->GetLobbies(&raw, loadingOut, errOut)) return false;
        if (outRooms) {
            outRooms->clear();
            outRooms->reserve(raw.size());
            for (const auto &r : raw) {
                CeOnlineUiRoomSummary dst;
                dst.roomId = r.room_id;
                dst.code = r.code;
                dst.name = r.name;
                dst.hostCallsign = r.host_callsign;
                dst.hostTag = r.host_tag;
                dst.topology = r.topology;
                dst.mode = r.mode;
                dst.state = r.state;
                dst.buildId = r.build_id;
                dst.map = r.map;
                dst.usedSeats = r.used_seats;
                dst.humanSeats = r.human_seats;
                dst.maxSeats = r.max_seats;
                dst.hasPassword = r.has_password;
                dst.requireRelay = r.require_relay;
                dst.compatible = r.compatible;
                outRooms->push_back(std::move(dst));
            }
        }
        return true;
    }

    bool RequestCreateRoom(const CeOnlineCreateParams &params, std::string *errOut) override {
        CeSvcCreateParams dst;
        dst.name = params.name;
        dst.mode = params.mode;
        dst.map = params.map;
        dst.max_seats = params.maxSeats;
        dst.topology = params.topology;
        dst.visibility = params.visibility;
        dst.password = params.password;
        dst.require_relay = params.requireRelay;
        dst.ai_fill = params.aiFill;
        dst.be_cfg = params.beCfg;
        return PortCeOnlineServiceInstance()->RequestCreateRoom(dst, errOut);
    }

    bool RequestResolveCode(const std::string &code, std::string *errOut) override {
        return PortCeOnlineServiceInstance()->RequestResolveCode(code, errOut);
    }

    bool TakeResolveResult(CeOnlineUiResolveResult *out) override {
        if (!out) return false;
        CeSvcResolveResult r;
        if (!PortCeOnlineServiceInstance()->TakeResolveResult(&r)) return false;
        out->resolved = r.resolved;
        out->found = r.found;
        out->roomId = r.room_id;
        out->hasPassword = r.has_password;
        out->state = r.state;
        out->error = r.error;
        return true;
    }

    bool RequestJoinRoom(const std::string &roomId, const std::string &password,
                         const std::string &beCfg, std::string *errOut) override {
        return PortCeOnlineServiceInstance()->RequestJoinRoom(roomId, password, beCfg, errOut);
    }

    void RequestLeaveRoom() override { PortCeOnlineServiceInstance()->RequestLeaveRoom(); }

    bool GetRoom(CeOnlineRoomView *outRoom) override {
        if (!outRoom) return false;
        CeSvcRoom r;
        if (!PortCeOnlineServiceInstance()->GetRoom(&r)) {
            *outRoom = CeOnlineRoomView();
            return false;
        }
        outRoom->active = r.active;
        outRoom->roomId = r.room_id;
        outRoom->code = r.code;
        outRoom->rev = r.rev;
        outRoom->state = r.state;
        outRoom->name = r.name;
        outRoom->topology = r.topology;
        outRoom->mode = r.mode;
        outRoom->map = r.map;
        outRoom->maxSeats = r.max_seats;
        outRoom->hostSeat = r.host_seat;
        outRoom->localSeat = r.local_seat;
        outRoom->visibility = r.visibility;
        outRoom->hasPassword = r.has_password;
        outRoom->requireRelay = r.require_relay;
        outRoom->aiFill = r.ai_fill;
        outRoom->buildId = r.build_id;
        outRoom->seats.clear();
        outRoom->seats.reserve(r.seats.size());
        for (const auto &s : r.seats) {
            CeOnlineSeatView sv;
            sv.seat = s.seat;
            sv.kind = static_cast<CeOnlineSeatKind>(s.kind);
            sv.playerId = s.player_id;
            sv.callsign = s.callsign;
            sv.tag = s.tag;
            sv.team = s.team;
            sv.beCfg = s.be_cfg;
            sv.ready = s.ready;
            sv.rttMs = s.rtt_ms;
            sv.route = static_cast<CeOnlineRouteKind>(s.route);
            outRoom->seats.push_back(std::move(sv));
        }
        outRoom->chat.clear();
        outRoom->chat.reserve(r.chat.size());
        for (const auto &c : r.chat) {
            CeOnlineUiChatEntry cm;
            cm.seq = c.seq;
            cm.seat = c.seat;
            cm.playerId = c.player_id;
            cm.callsign = c.callsign;
            cm.tag = c.tag;
            cm.text = c.text;
            cm.ts = c.ts;
            outRoom->chat.push_back(std::move(cm));
        }
        outRoom->startTicket.valid = r.start_ticket.valid;
        outRoom->startTicket.matchId = r.start_ticket.match_id;
        outRoom->startTicket.seed = r.start_ticket.seed;
        outRoom->startTicket.level = r.start_ticket.level;
        outRoom->startTicket.mode = r.start_ticket.mode;
        outRoom->startTicket.topology = r.start_ticket.topology;
        outRoom->startTicket.requireRelay = r.start_ticket.require_relay;
        outRoom->startTicket.rosterHash = r.start_ticket.roster_hash;
        return true;
    }

    bool RequestUpdateSelf(int team, const std::string &beCfg, bool ready, std::string *errOut) override {
        return PortCeOnlineServiceInstance()->RequestUpdateSelf(team, beCfg, ready, errOut);
    }
    bool RequestHostUpdate(const CeOnlineHostPatch &patch, std::string *errOut) override {
        CeSvcHostPatch dst;
        dst.has_map = patch.hasMap;
        dst.map = patch.map;
        dst.has_mode = patch.hasMode;
        dst.mode = patch.mode;
        dst.has_require_relay = patch.hasRequireRelay;
        dst.require_relay = patch.requireRelay;
        dst.has_ai_fill = patch.hasAiFill;
        dst.ai_fill = patch.aiFill;
        dst.has_seat_kind = patch.hasSeatKind;
        dst.target_seat = patch.targetSeat;
        dst.seat_kind = static_cast<int>(patch.seatKind);
        dst.has_kick_seat = patch.hasKickSeat;
        dst.kick_seat = patch.kickSeat;
        return PortCeOnlineServiceInstance()->RequestHostUpdate(dst, errOut);
    }
    bool RequestSendChat(const std::string &text, std::string *errOut) override {
        return PortCeOnlineServiceInstance()->RequestSendChat(text, errOut);
    }
    bool RequestStartMatch(std::string *errOut) override {
        return PortCeOnlineServiceInstance()->RequestStartMatch(errOut);
    }
    void OnMatchTicketReady(const CeOnlineStartTicketView &) override {
        PortCeOnlineServiceInstance()->NotifyMatchTicketReady();
    }
    bool TakeOpError(std::string *out) override {
        return PortCeOnlineServiceInstance()->TakeOpError(out);
    }
    void FlushLeave(uint64_t nowMs) override {
        PortCeOnlineServiceInstance()->FlushLeave(nowMs);
    }
};
#endif

class CeGameBridgeBackend : public ICeOnlineUiBackend {
public:
    CeGameBridgeBackend() : mInner(nullptr) {}
    void SetInner(ICeOnlineUiBackend *inner) { mInner = inner; }
    ICeOnlineUiBackend *ActiveBackend() { return mInner ? mInner : static_cast<ICeOnlineUiBackend *>(&mFallback); }

    void Poll(uint64_t nowMs) override { ActiveBackend()->Poll(nowMs); }
    bool GetProfile(CeOnlineProfileView *out) override { return ActiveBackend()->GetProfile(out); }
    bool SaveCallsign(const std::string &c, std::string *e) override { return ActiveBackend()->SaveCallsign(c, e); }
    void RequestReconnect() override { ActiveBackend()->RequestReconnect(); }
    void RequestRefreshLobbies(bool inc) override { ActiveBackend()->RequestRefreshLobbies(inc); }
    bool GetLobbies(std::vector<CeOnlineUiRoomSummary> *r, bool *l, std::string *e) override {
        return ActiveBackend()->GetLobbies(r, l, e);
    }
    bool RequestCreateRoom(const CeOnlineCreateParams &p, std::string *e) override {
        return ActiveBackend()->RequestCreateRoom(p, e);
    }
    bool RequestResolveCode(const std::string &c, std::string *e) override {
        return ActiveBackend()->RequestResolveCode(c, e);
    }
    bool TakeResolveResult(CeOnlineUiResolveResult *o) override {
        return ActiveBackend()->TakeResolveResult(o);
    }
    bool RequestJoinRoom(const std::string &id, const std::string &pw, const std::string &be, std::string *e) override {
        return ActiveBackend()->RequestJoinRoom(id, pw, be, e);
    }
    void RequestLeaveRoom() override { ActiveBackend()->RequestLeaveRoom(); }
    bool GetRoom(CeOnlineRoomView *r) override { return ActiveBackend()->GetRoom(r); }
    bool RequestUpdateSelf(int t, const std::string &be, bool r, std::string *e) override {
        return ActiveBackend()->RequestUpdateSelf(t, be, r, e);
    }
    bool RequestHostUpdate(const CeOnlineHostPatch &p, std::string *e) override {
        return ActiveBackend()->RequestHostUpdate(p, e);
    }
    bool RequestSendChat(const std::string &t, std::string *e) override {
        return ActiveBackend()->RequestSendChat(t, e);
    }
    bool RequestStartMatch(std::string *e) override { return ActiveBackend()->RequestStartMatch(e); }

    bool CopyToClipboard(const std::string &text) override {
        if (mInner && mInner->CopyToClipboard(text)) return true;
        return mFallback.CopyToClipboard(text);
    }
    bool PasteFromClipboard(std::string *out) override {
        if (mInner && mInner->PasteFromClipboard(out)) return true;
        return mFallback.PasteFromClipboard(out);
    }
    void GetBeConfigsForMap(int level, std::vector<std::string> *out) override {
        mFallback.GetBeConfigsForMap(level, out);
        if (mInner && (!out || out->empty())) {
            mInner->GetBeConfigsForMap(level, out);
        }
    }
    void OnOpenDirectLan() override { if (mInner) mInner->OnOpenDirectLan(); }
    void OnMatchTicketReady(const CeOnlineStartTicketView &t) override { if (mInner) mInner->OnMatchTicketReady(t); }
    bool TakeOpError(std::string *out) override { return mInner ? mInner->TakeOpError(out) : false; }
    void FlushLeave(uint64_t nowMs) override { if (mInner) mInner->FlushLeave(nowMs); }

private:
    CeDefaultOnlineUiBackend mFallback;
    ICeOnlineUiBackend *mInner;
};

CeOnlineUiModel     gModel;
CeGameBridgeBackend gBridge;
bool                gBound = false;
#if defined(CE_NETWORK_SIGNALING)
CeLiveOnlineUiBackendAdapter *gLiveAdapter = nullptr;
#endif

void EnsureBound() {
    if (!gBound) {
        gBound = true;
#if defined(CE_NETWORK_SIGNALING)
        if (!gLiveAdapter) {
            gLiveAdapter = new CeLiveOnlineUiBackendAdapter();
            gBridge.SetInner(gLiveAdapter);
        }
#endif
        gModel.SetBackend(&gBridge);
    }
}

void HandleExitFlags() {
    if (gModel.TakeRequestedDirectLan()) {
        if (gHasOps && gOps.exit_action) {
            gOps.exit_action(gModel.ReturnToMultiplayer() ? CE_OUI_EXIT_DIRECT_LAN : CE_OUI_EXIT_CLOSE_NET);
        }
        return;
    }
    if (gModel.TakeRequestedClose()) {
        if (gHasOps && gOps.exit_action) {
            gOps.exit_action(gModel.ReturnToMultiplayer() ? CE_OUI_EXIT_CLOSE_MP : CE_OUI_EXIT_CLOSE_NET);
        }
    }
}

void DrawHeaderAndFooter(const std::string &hint) {
    const CeOnlineRect hdr = CeOnlineLayout::HeaderBar();
    DrawPanelBox(hdr, kHeaderBg, kBorder);
    const CeOnlineProfileView &prof = gModel.Profile();
    DrawTextLeft(hdr.x0 + 86.f, hdr.y0 + 3.f, 200.f, kCream, prof.DisplayIdentity());

    uint32_t statusCol = kGrey;
    if (prof.connStatus == CE_ONLINE_CONN_ONLINE) statusCol = kGood;
    else if (prof.connStatus == CE_ONLINE_CONN_CONNECTING) statusCol = kWarn;
    else if (prof.connStatus == CE_ONLINE_CONN_ERROR) statusCol = kBad;

    const std::string right = std::string(CeOnlineConnStatusLabel(prof.connStatus)) + " (" + prof.PingLabel() + ")";
    DrawTextRight(hdr.x1 - 8.f, hdr.y0 + 3.f, 180.f, statusCol, right);

    const CeOnlineRect ftr = CeOnlineLayout::FooterBar();
    DrawPanelBox(ftr, kPanelBg, kBorder);
    const std::string &notice = gModel.StatusNotice();
    if (!notice.empty()) {
        DrawTextCenter(ftr, gModel.StatusIsError() ? kBad : kGood, notice);
    } else {
        DrawTextCenter(ftr, kCream, hint);
    }
}

void RenderHome(float mx, float my, bool mouseMoved, bool canClick) {
    const CeOnlineProfileView &prof = gModel.Profile();
    CeOnlineRect infoRect = { 120.f, 134.f, 520.f, 162.f };
    DrawPanelBox(infoRect, kPanelBg, kBorder);
    char stats[128];
    std::snprintf(stats, sizeof(stats), "Pilot: %s   Matches: %u   Wins: %u",
                  prof.DisplayIdentity().c_str(), prof.matchesPlayed, prof.matchesWon);
    DrawTextCenter(infoRect, kCream, stats);

    for (int i = 0; i < CeOnlineLayout::kHomeRowCount; ++i) {
        const CeOnlineRect r = CeOnlineLayout::HomeRow(i);
        if (mouseMoved && r.Contains(mx, my)) gModel.SetHomeFocus(i);
        if (DrawButton(r, CeOnlineUiModel::HomeRowLabel(i), gModel.HomeFocus() == i, true, mx, my, canClick)) {
            gModel.HomeActivate(i);
            HandleExitFlags();
            return;
        }
    }
    DrawHeaderAndFooter(CeOnlineUiModel::HomeRowHint(gModel.HomeFocus()));
}

void RenderBrowser(float mx, float my, bool mouseMoved, int *wheel, bool canClick) {
    const bool modal = gModel.PasswordPromptOpen();
    if (DrawButton(CeOnlineLayout::BrowserFilterBtn(),
                   gModel.CompatibleOnly() ? "Filter: Compatible" : "Filter: All Rooms",
                   false, !modal, mx, my, canClick)) {
        gModel.BrowserToggleCompatibleFilter();
    }
    if (DrawButton(CeOnlineLayout::BrowserRefreshBtn(), "Refresh", false, !modal, mx, my, canClick)) {
        gModel.BrowserRefresh();
    }
    if (DrawButton(CeOnlineLayout::BrowserCreateBtn(), "Create Lobby", false, !modal, mx, my, canClick)) {
        gModel.EnterCreate(CE_ONLINE_PAGE_BROWSER);
        return;
    }
    if (DrawButton(CeOnlineLayout::BrowserJoinCodeBtn(), "Join by Code", false, !modal, mx, my, canClick)) {
        gModel.EnterJoinCode(CE_ONLINE_PAGE_BROWSER);
        return;
    }

    const CeOnlineRect th = CeOnlineLayout::BrowserTableHeader();
    DrawPanelBox(th, kHeaderBg, kBorder);
    DrawTextLeft(th.x0 + 6.f,   th.y0 + 2.f, 150.f, kCream, "Lobby Name");
    DrawTextLeft(th.x0 + 162.f, th.y0 + 2.f, 130.f, kCream, "Map / Mode");
    DrawTextLeft(th.x0 + 300.f, th.y0 + 2.f, 68.f,  kCream, "Players");
    DrawTextLeft(th.x0 + 374.f, th.y0 + 2.f, 92.f,  kCream, "Flags");

    std::vector<CeOnlineUiRoomSummary> rooms = gModel.FilteredRooms();
    if (wheel && *wheel != 0 && !modal) {
        gModel.BrowserMoveSelection(*wheel > 0 ? -1 : 1);
        *wheel = 0;
    }

    const int start = gModel.BrowserPageOffset();
    if (rooms.empty()) {
        CeOnlineRect emptyBox = { 84.f, 180.f, 556.f, 344.f };
        DrawPanelBox(emptyBox, kRowBg, kBorder);
        DrawTextCenter(emptyBox, kGrey,
                       gModel.BrowserLoading() ? "Loading public lobbies..." : "No lobbies found. Choose Create Lobby or Refresh.");
    } else {
        for (int vis = 0; vis < CE_ONLINE_UI_BROWSER_PAGE_SIZE; ++vis) {
            const int idx = start + vis;
            const CeOnlineRect rr = CeOnlineLayout::BrowserRow(vis);
            if (idx >= static_cast<int>(rooms.size())) {
                DrawPanelBox(rr, kRowBg, kBorderDis);
                continue;
            }
            const CeOnlineUiRoomSummary &rm = rooms[static_cast<size_t>(idx)];
            const bool selected = (idx == gModel.BrowserSelectedIndex());
            if (!modal && mouseMoved && rr.Contains(mx, my)) gModel.BrowserSelectIndex(idx);
            DrawPanelBox(rr, selected ? kRowSelBg : ((vis & 1) ? kRowAltBg : kRowBg), selected ? kBorderSel : kBorder);

            const uint32_t nameCol = !rm.compatible ? kDimGrey : (selected ? kWarn : kWhite);
            DrawTextLeft(rr.x0 + 6.f, rr.y0 + 4.f, 150.f, nameCol, CeOnlineTruncateFontSafe(rm.name, 20));
            const std::string mapMode = std::string(CeOnlineMapName(rm.map)) + " / " + CeOnlineModeLabel(rm.mode);
            DrawTextLeft(rr.x0 + 162.f, rr.y0 + 4.f, 132.f, kCream, CeOnlineTruncateFontSafe(mapMode, 22));
            char seatBuf[24];
            std::snprintf(seatBuf, sizeof(seatBuf), "%u/%u", unsigned(rm.humanSeats), unsigned(rm.maxSeats));
            DrawTextLeft(rr.x0 + 306.f, rr.y0 + 4.f, 52.f, (rm.humanSeats >= rm.maxSeats) ? kBad : kGood, seatBuf);
            std::string flags;
            if (rm.hasPassword) flags += "PWD ";
            flags += rm.requireRelay ? "RELAY" : "P2P";
            if (!rm.compatible) flags += " OLD";
            DrawTextLeft(rr.x0 + 374.f, rr.y0 + 4.f, 92.f, rm.requireRelay ? kCyan : kGrey, flags);

            if (!modal && ClickedRect(rr, canClick)) {
                if (selected) {
                    PlaySnd(CE_OUI_SND_SELECT);
                    gModel.BrowserJoinSelected();
                } else {
                    PlaySnd(CE_OUI_SND_MOVE);
                    gModel.BrowserSelectIndex(idx);
                }
            }
        }
    }

    const int curPage = gModel.BrowserCurrentPage();
    const int totPages = gModel.BrowserPageCount();
    if (DrawButton(CeOnlineLayout::BrowserPrevBtn(), "< Prev", false, !modal && curPage > 1, mx, my, canClick)) gModel.BrowserPrevPage();
    char pageBuf[32];
    std::snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", curPage, totPages);
    DrawPanelBox(CeOnlineLayout::BrowserPageBadge(), kPanelBg, kBorder);
    DrawTextCenter(CeOnlineLayout::BrowserPageBadge(), kCream, pageBuf);
    if (DrawButton(CeOnlineLayout::BrowserNextBtn(), "Next >", false, !modal && curPage < totPages, mx, my, canClick)) gModel.BrowserNextPage();
    if (DrawButton(CeOnlineLayout::BrowserJoinBtn(), "Join Selected", false, !modal && !rooms.empty(), mx, my, canClick, kGood)) gModel.BrowserJoinSelected();

    if (DrawButton(CeOnlineLayout::BrowserProfileBtn(), "Online Profile", false, !modal, mx, my, canClick)) {
        gModel.EnterProfile(CE_ONLINE_PAGE_BROWSER);
        return;
    }
    if (DrawButton(CeOnlineLayout::BrowserLanBtn(), "Direct LAN / IP", false, !modal, mx, my, canClick)) {
        gModel.TriggerDirectLan();
        HandleExitFlags();
        return;
    }
    if (DrawButton(CeOnlineLayout::BrowserBackBtn(), "Back", false, !modal, mx, my, canClick)) {
        gModel.NavigateBack();
        HandleExitFlags();
        return;
    }

    if (modal) {
        const CeOnlineRect box = CeOnlineLayout::BrowserPwdModal();
        DrawPanelBox(box, 0xee0a1424u, kBorderSel);
        DrawTextLeft(box.x0 + 12.f, box.y0 + 10.f, 356.f, kWarn, "Password for: " + gModel.PasswordPromptRoomName());
        CeOnlineRect field = { box.x0 + 12.f, box.y0 + 38.f, box.x1 - 12.f, box.y0 + 64.f };
        DrawPanelBox(field, kRowSelBg, kBorderSel);
        std::string masked(gModel.PasswordPromptInput().size(), '*');
        DrawTextLeft(field.x0 + 8.f, field.y0 + 4.f, 340.f, kWhite, masked, true);
        if (DrawButton(CeOnlineLayout::BrowserPwdSubmit(), "Join Lobby", true, true, mx, my, canClick, kGood)) gModel.BrowserPasswordSubmit();
        if (DrawButton(CeOnlineLayout::BrowserPwdCancel(), "Cancel", false, true, mx, my, canClick)) gModel.BrowserPasswordCancel();
    }

    DrawHeaderAndFooter(modal
        ? "Type password and press Enter to join, or Esc to cancel."
        : "Up/Down select room, Left/Right change page, Enter joins.");
}

void RenderCreate(float mx, float my, bool mouseMoved, bool canClick) {
    const char *labels[CeOnlineLayout::kCreateFieldCount] = {
        "Room Name:", "Map:", "Mode:", "Max Players:",
        "Visibility:", "Password (Opt.):", "Relay Only (Hide IP):", "AI Fill Empty Slots:"
    };
    char seatsBuf[16];
    std::snprintf(seatsBuf, sizeof(seatsBuf), "%u", unsigned(gModel.CreateMaxSeats()));
    std::string pwdMasked(gModel.CreatePassword().size(), '*');
    std::string values[CeOnlineLayout::kCreateFieldCount] = {
        gModel.CreateRoomName(), gModel.CreateMapName(), gModel.CreateModeLabel(), seatsBuf,
        gModel.CreateVisibilityLabel(), pwdMasked.empty() ? "None" : pwdMasked,
        gModel.CreateRelayOnlyLabel(), gModel.CreateAiFillLabel()
    };

    for (int i = 0; i < CeOnlineLayout::kCreateFieldCount; ++i) {
        const CeOnlineRect r = CeOnlineLayout::CreateFieldRow(i);
        const bool sel = (gModel.CreateFocus() == i);
        if (mouseMoved && r.Contains(mx, my)) gModel.SetCreateFocus(i);
        DrawPanelBox(r, sel ? kRowSelBg : kRowBg, sel ? kBorderSel : kBorder);
        DrawTextLeft(r.x0 + 8.f, r.y0 + 3.f, 180.f, sel ? kWarn : kCream, labels[i]);
        const bool isText = (i == CeOnlineUiModel::CREATE_NAME || i == CeOnlineUiModel::CREATE_PASSWORD);
        const uint32_t valCol = (i == CeOnlineUiModel::CREATE_RELAY_ONLY && gModel.CreateRelayOnly())
            ? kCyan : (sel ? kWhite : kCream);
        DrawTextLeft(r.x0 + 196.f, r.y0 + 3.f, 256.f, valCol, values[i], sel && isText);

        if (ClickedRect(r, canClick)) {
            gModel.SetCreateFocus(i);
            if (!isText) { PlaySnd(CE_OUI_SND_MOVE); gModel.CreateAdjustFocused(1); }
            else { PlaySnd(CE_OUI_SND_SELECT); }
        }
    }

    const CeOnlineRect priv = CeOnlineLayout::CreatePrivacyBox();
    DrawPanelBox(priv, kHeaderBg, gModel.CreateRelayOnly() ? kCyan : kWarn);
    DrawTextLeft(priv.x0 + 8.f, priv.y0 + 4.f, 448.f, gModel.CreateRelayOnly() ? kCyan : kWarn,
                 "Direct P2P shares your IP with the host.");
    DrawTextLeft(priv.x0 + 8.f, priv.y0 + 19.f, 448.f, kCream,
                 "Relay Only hides your IP via TURN.");

    if (DrawButton(CeOnlineLayout::CreateSubmitBtn(), "Create Lobby",
                   gModel.CreateFocus() == CeOnlineUiModel::CREATE_SUBMIT, true, mx, my, canClick, kGood)) {
        gModel.CreateSubmit();
    }
    if (DrawButton(CeOnlineLayout::CreateBackBtn(), "Back",
                   gModel.CreateFocus() == CeOnlineUiModel::CREATE_BACK, true, mx, my, canClick)) {
        gModel.NavigateBack();
        HandleExitFlags();
        return;
    }

    DrawHeaderAndFooter("Up/Down move, Left/Right change option, type Name/Password, Enter creates.");
}

void RenderJoinCode(float mx, float my, bool mouseMoved, bool canClick) {
    CeOnlineRect intro = { 120.f, 138.f, 520.f, 166.f };
    DrawPanelBox(intro, kPanelBg, kBorder);
    DrawTextCenter(intro, kCream, "Enter the 6-character room code shared by the host.");

    const CeOnlineRect codeBox = CeOnlineLayout::JoinCodeField();
    const bool selCode = (gModel.JoinCodeFocus() == CeOnlineUiModel::JOIN_CODE_INPUT);
    if (mouseMoved && codeBox.Contains(mx, my)) gModel.SetJoinCodeFocus(CeOnlineUiModel::JOIN_CODE_INPUT);
    DrawPanelBox(codeBox, selCode ? kRowSelBg : kRowBg, selCode ? kBorderSel : kBorder);
    DrawTextLeft(codeBox.x0 + 10.f, codeBox.y0 + 6.f, 120.f, selCode ? kWarn : kCream, "Room Code:");
    DrawTextLeft(codeBox.x0 + 136.f, codeBox.y0 + 6.f, 240.f, kWhite, gModel.JoinCodeInput(), selCode);
    if (ClickedRect(codeBox, canClick)) { gModel.SetJoinCodeFocus(CeOnlineUiModel::JOIN_CODE_INPUT); PlaySnd(CE_OUI_SND_SELECT); }

    const CeOnlineRect pwdBox = CeOnlineLayout::JoinPwdField();
    const bool selPwd = (gModel.JoinCodeFocus() == CeOnlineUiModel::JOIN_PASSWORD_INPUT);
    if (mouseMoved && pwdBox.Contains(mx, my)) gModel.SetJoinCodeFocus(CeOnlineUiModel::JOIN_PASSWORD_INPUT);
    DrawPanelBox(pwdBox, selPwd ? kRowSelBg : kRowBg, selPwd ? kBorderSel : kBorder);
    DrawTextLeft(pwdBox.x0 + 10.f, pwdBox.y0 + 6.f, 120.f, selPwd ? kWarn : kCream, "Password:");
    std::string masked(gModel.JoinPasswordInput().size(), '*');
    DrawTextLeft(pwdBox.x0 + 136.f, pwdBox.y0 + 6.f, 240.f, kWhite, masked.empty() ? "Optional" : masked, selPwd);
    if (ClickedRect(pwdBox, canClick)) { gModel.SetJoinCodeFocus(CeOnlineUiModel::JOIN_PASSWORD_INPUT); PlaySnd(CE_OUI_SND_SELECT); }

    if (DrawButton(CeOnlineLayout::JoinPasteBtn(), "Paste Code (Ctrl+V)",
                   gModel.JoinCodeFocus() == CeOnlineUiModel::JOIN_PASTE_BTN, true, mx, my, canClick)) gModel.JoinCodePaste();
    if (DrawButton(CeOnlineLayout::JoinSubmitBtn(), "Join Room",
                   gModel.JoinCodeFocus() == CeOnlineUiModel::JOIN_SUBMIT_BTN, true, mx, my, canClick, kGood)) gModel.JoinCodeSubmit();
    if (DrawButton(CeOnlineLayout::JoinBackBtn(), "Back",
                   gModel.JoinCodeFocus() == CeOnlineUiModel::JOIN_BACK_BTN, true, mx, my, canClick)) {
        gModel.NavigateBack();
        HandleExitFlags();
        return;
    }

    DrawHeaderAndFooter("Type or Ctrl+V paste the 6-character code, then choose Join Room.");
}

void RenderRoom(float mx, float my, bool mouseMoved, int *wheel, bool canClick) {
    const CeOnlineRoomView &room = gModel.Room();
    const bool isHost = gModel.IsLocalHost();
    const CeOnlineSeatView *me = gModel.LocalSeat();

    const CeOnlineRect info = CeOnlineLayout::RoomInfoBar();
    DrawPanelBox(info, kPanelBg, kBorder);
    std::string infoText = CeOnlineTruncateFontSafe(room.name, 18) + "  Code:" +
                           CeOnlineSanitizeForFont(room.code) + "  " +
                           CeOnlineTruncateFontSafe(CeOnlineMapName(room.map), 16) + "  " +
                           (room.requireRelay ? "RELAY" : "P2P");
    DrawTextLeft(info.x0 + 6.f, info.y0 + 2.f, 368.f, kCream, infoText);

    if (DrawButton(CeOnlineLayout::RoomCopyCodeBtn(), "Copy Code", false, !room.code.empty(), mx, my, canClick)) {
        gModel.RoomCopyCode();
    }

    const CeOnlineRect rh = CeOnlineLayout::RoomRosterHeader();
    DrawPanelBox(rh, kHeaderBg, kBorder);
    DrawTextLeft(rh.x0 + 4.f,   rh.y0 + 1.f, 20.f,  kCream, "#");
    DrawTextLeft(rh.x0 + 24.f,  rh.y0 + 1.f, 134.f, kCream, "Player");
    DrawTextLeft(rh.x0 + 160.f, rh.y0 + 1.f, 52.f,  kCream, "Side");
    DrawTextLeft(rh.x0 + 214.f, rh.y0 + 1.f, 68.f,  kCream, "BE");
    DrawTextLeft(rh.x0 + 290.f, rh.y0 + 1.f, 42.f,  kCream, "Ping");
    DrawTextLeft(rh.x0 + 336.f, rh.y0 + 1.f, 46.f,  kCream, "State");

    const int curPg = gModel.RosterCurrentPage();
    const int totPg = gModel.RosterPageCount();
    if (DrawButton(CeOnlineLayout::RoomPagePrevBtn(), "<", false, curPg > 1, mx, my, canClick)) gModel.RosterPrevPage();
    if (DrawButton(CeOnlineLayout::RoomPageNextBtn(), ">", false, curPg < totPg, mx, my, canClick)) gModel.RosterNextPage();

    if (wheel && *wheel != 0) {
        gModel.RosterMoveSelection(*wheel > 0 ? -1 : 1);
        *wheel = 0;
    }

    const int start = gModel.RosterPageOffset();
    for (int vis = 0; vis < CE_ONLINE_UI_ROSTER_PAGE_SIZE; ++vis) {
        const int idx = start + vis;
        const CeOnlineRect sr = CeOnlineLayout::RoomSeatRow(vis);
        if (idx >= static_cast<int>(room.seats.size())) {
            DrawPanelBox(sr, kRowBg, kBorderDis);
            continue;
        }
        const CeOnlineSeatView &s = room.seats[static_cast<size_t>(idx)];
        const bool sel = (idx == gModel.RosterSelectedIndex());
        if (mouseMoved && sr.Contains(mx, my)) gModel.RosterSelectIndex(idx);
        DrawPanelBox(sr, sel ? kRowSelBg : ((vis & 1) ? kRowAltBg : kRowBg), sel ? kBorderSel : kBorder);

        char numBuf[12];
        std::snprintf(numBuf, sizeof(numBuf), "%u", unsigned(s.seat + 1));
        DrawTextLeft(sr.x0 + 4.f, sr.y0 + 1.f, 18.f, kGrey, numBuf);

        const uint32_t nameCol = (s.kind == CE_ONLINE_SEAT_HUMAN)
            ? (s.seat == room.localSeat ? kWarn : kWhite)
            : (s.kind == CE_ONLINE_SEAT_AI ? kCyan : kDimGrey);
        DrawTextLeft(sr.x0 + 24.f, sr.y0 + 1.f, 134.f, nameCol, gModel.FormatSeatName(s));

        const bool hasUnit = (s.kind == CE_ONLINE_SEAT_HUMAN || s.kind == CE_ONLINE_SEAT_AI);
        DrawTextLeft(sr.x0 + 160.f, sr.y0 + 1.f, 52.f, hasUnit ? (s.team == 1 ? kMuspell : kCyan) : kDimGrey,
                     hasUnit ? CeOnlineSideLabel(s.team) : "--");
        DrawTextLeft(sr.x0 + 214.f, sr.y0 + 1.f, 68.f, kCream,
                     hasUnit ? CeOnlineTruncateFontSafe(s.beCfg.empty() ? "Standard" : s.beCfg, 10) : "--");
        DrawTextLeft(sr.x0 + 290.f, sr.y0 + 1.f, 42.f, kGrey, gModel.FormatSeatPing(s));
        DrawTextLeft(sr.x0 + 336.f, sr.y0 + 1.f, 46.f, (s.kind == CE_ONLINE_SEAT_HUMAN && s.ready) ? kGood : kGrey,
                     gModel.FormatSeatReady(s));
        DrawTextLeft(sr.x0 + 388.f, sr.y0 + 1.f, 80.f, s.route == CE_ONLINE_ROUTE_RELAY ? kCyan : kGrey,
                     s.kind == CE_ONLINE_SEAT_HUMAN ? CeOnlineRouteLabel(s.route) : "--");

        if (ClickedRect(sr, canClick)) { gModel.RosterSelectIndex(idx); PlaySnd(CE_OUI_SND_MOVE); }
    }

    const bool ready = (me && me->ready);
    if (DrawButton(CeOnlineLayout::RoomReadyBtn(), ready ? "Ready: ON" : "Ready: OFF",
                   false, me != nullptr, mx, my, canClick, ready ? kGood : kWarn)) gModel.RoomToggleReady();
    const std::string sideBtn = std::string("Side: ") + (me ? CeOnlineSideLabel(me->team) : "--");
    if (DrawButton(CeOnlineLayout::RoomSideBtn(), sideBtn, false, me != nullptr, mx, my, canClick)) gModel.RoomCycleSide(1);
    const std::string beBtn = std::string("BE: ") +
        CeOnlineTruncateFontSafe((me && !me->beCfg.empty()) ? me->beCfg : "Standard", 10);
    if (DrawButton(CeOnlineLayout::RoomBeBtn(), beBtn, false, me != nullptr, mx, my, canClick)) gModel.RoomCycleBeConfig(1);
    const CeOnlineSeatView *selSeat = gModel.SelectedSeat();
    const bool canMute = (selSeat && selSeat->kind == CE_ONLINE_SEAT_HUMAN && selSeat->seat != room.localSeat);
    const bool isMuted = canMute && gModel.IsPlayerMuted(selSeat->playerId);
    if (DrawButton(CeOnlineLayout::RoomMuteBtn(), isMuted ? "Unmute" : "Mute", false, canMute, mx, my, canClick)) gModel.RoomToggleMuteSelected();
    if (DrawButton(CeOnlineLayout::RoomLeaveBtn(), "Leave", false, true, mx, my, canClick, kBad)) {
        gModel.RoomLeave();
        return;
    }

    std::string startReason;
    const bool canStart = gModel.RoomCanStartMatch(&startReason);
    if (isHost) {
        if (DrawButton(CeOnlineLayout::RoomMapBtn(), "Next Map", false, true, mx, my, canClick)) gModel.RoomHostCycleMap(1);
        if (DrawButton(CeOnlineLayout::RoomModeBtn(), CeOnlineModeLabel(room.mode), false, true, mx, my, canClick)) gModel.RoomHostCycleMode(1);
        const bool canToggleSlot = (selSeat && selSeat->kind != CE_ONLINE_SEAT_HUMAN);
        if (DrawButton(CeOnlineLayout::RoomSlotBtn(), "Cycle Slot", false, canToggleSlot, mx, my, canClick)) gModel.RoomHostCycleSelectedSeatKind();
        const bool canKick = (selSeat && selSeat->kind == CE_ONLINE_SEAT_HUMAN && selSeat->seat != room.hostSeat);
        if (DrawButton(CeOnlineLayout::RoomKickBtn(), "Kick", false, canKick, mx, my, canClick, canKick ? kBad : 0)) gModel.RoomHostKickSelected();
        if (DrawButton(CeOnlineLayout::RoomStartBtn(), "Start Match", false, true, mx, my, canClick, canStart ? kGood : kGrey)) gModel.RoomHostStartMatch();
    } else {
        CeOnlineRect guestStatus = { 84.f, 276.f, 556.f, 296.f };
        DrawPanelBox(guestStatus, kPanelBg, kBorder);
        DrawTextCenter(guestStatus, kCream, std::string("Mode: ") + CeOnlineModeLabel(room.mode) + "   " + startReason);
    }

    const CeOnlineRect chatLog = CeOnlineLayout::RoomChatLog();
    DrawPanelBox(chatLog, kPanelBg, kBorder);
    std::vector<std::string> lines = gModel.VisibleChatLines();
    if (lines.empty()) {
        DrawTextLeft(chatLog.x0 + 8.f, chatLog.y0 + 6.f, 450.f, kGrey, "No lobby messages yet.");
    } else {
        for (size_t i = 0; i < lines.size(); ++i) {
            DrawTextLeft(chatLog.x0 + 8.f, chatLog.y0 + 3.f + float(i) * 15.f, 456.f, kWhite, lines[i]);
        }
    }

    const CeOnlineRect compose = CeOnlineLayout::RoomChatCompose();
    const bool composing = gModel.ChatComposing();
    DrawPanelBox(compose, composing ? kRowSelBg : kRowBg, composing ? kBorderSel : kBorder);
    if (composing) DrawTextLeft(compose.x0 + 6.f, compose.y0 + 2.f, 386.f, kWhite, "Say: " + gModel.ChatDraft(), true);
    else DrawTextLeft(compose.x0 + 6.f, compose.y0 + 2.f, 386.f, kGrey, "Press T or click here to chat...");
    if (ClickedRect(compose, canClick)) { gModel.RoomOpenChat(); PlaySnd(CE_OUI_SND_SELECT); }
    if (DrawButton(CeOnlineLayout::RoomChatSendBtn(), "Send", false, composing && !gModel.ChatDraft().empty(), mx, my, canClick, kGood)) {
        gModel.RoomChatSubmit();
    }

    char footer[140];
    if (totPg > 1) std::snprintf(footer, sizeof(footer), "Seats Page %d/%d   %s", curPg, totPg, startReason.c_str());
    else std::snprintf(footer, sizeof(footer), "%s", startReason.c_str());
    DrawHeaderAndFooter(footer);
}

void RenderProfile(float mx, float my, bool mouseMoved, bool canClick) {
    const CeOnlineProfileView &prof = gModel.Profile();
    const CeOnlineRect csBox = CeOnlineLayout::ProfileCallsignBox();
    const bool selCs = (gModel.ProfileFocus() == CeOnlineUiModel::PROFILE_CALLSIGN_INPUT);
    if (mouseMoved && csBox.Contains(mx, my)) gModel.SetProfileFocus(CeOnlineUiModel::PROFILE_CALLSIGN_INPUT);
    DrawPanelBox(csBox, selCs ? kRowSelBg : kRowBg, selCs ? kBorderSel : kBorder);
    DrawTextLeft(csBox.x0 + 10.f, csBox.y0 + 6.f, 110.f, selCs ? kWarn : kCream, "Callsign:");
    DrawTextLeft(csBox.x0 + 124.f, csBox.y0 + 6.f, 220.f, kWhite, gModel.ProfileCallsignDraft(), selCs);
    DrawTextRight(csBox.x1 - 10.f, csBox.y0 + 6.f, 70.f, kCyan, "#" + CeOnlineSanitizeForFont(prof.tag));
    if (ClickedRect(csBox, canClick)) { gModel.SetProfileFocus(CeOnlineUiModel::PROFILE_CALLSIGN_INPUT); PlaySnd(CE_OUI_SND_SELECT); }

    const CeOnlineRect st = CeOnlineLayout::ProfileStatsPanel();
    DrawPanelBox(st, kPanelBg, kBorder);
    char line[128];
    std::snprintf(line, sizeof(line), "Identity: %s", prof.DisplayIdentity().c_str());
    DrawTextLeft(st.x0 + 12.f, st.y0 + 10.f, 396.f, kWhite, line);
    std::snprintf(line, sizeof(line), "Matches Played: %u    Wins: %u", prof.matchesPlayed, prof.matchesWon);
    DrawTextLeft(st.x0 + 12.f, st.y0 + 34.f, 396.f, kCream, line);
    DrawTextLeft(st.x0 + 12.f, st.y0 + 58.f, 396.f, kGrey, "Lobby Server: " + CeOnlineTruncateFontSafe(prof.serverUrl, 40));
    std::snprintf(line, sizeof(line), "Server Status: %s    Ping: %s",
                  CeOnlineConnStatusLabel(prof.connStatus), prof.PingLabel().c_str());
    const uint32_t stCol = (prof.connStatus == CE_ONLINE_CONN_ONLINE) ? kGood
                         : (prof.connStatus == CE_ONLINE_CONN_ERROR ? kBad : kWarn);
    DrawTextLeft(st.x0 + 12.f, st.y0 + 82.f, 396.f, stCol, line);

    if (DrawButton(CeOnlineLayout::ProfileSaveBtn(), "Save Callsign",
                   gModel.ProfileFocus() == CeOnlineUiModel::PROFILE_SAVE_BTN, true, mx, my, canClick, kGood)) gModel.ProfileSave();
    if (DrawButton(CeOnlineLayout::ProfileReconnectBtn(), "Reconnect",
                   gModel.ProfileFocus() == CeOnlineUiModel::PROFILE_RECONNECT_BTN, true, mx, my, canClick)) gModel.ProfileReconnect();
    if (DrawButton(CeOnlineLayout::ProfileBackBtn(), "Back",
                   gModel.ProfileFocus() == CeOnlineUiModel::PROFILE_BACK_BTN, true, mx, my, canClick)) {
        gModel.NavigateBack();
        HandleExitFlags();
        return;
    }

    DrawHeaderAndFooter("Callsign: 3 to 20 letters, digits, spaces or hyphens. Zero account required.");
}

void RenderConnecting(float mx, float my, bool canClick) {
    const CeOnlineRect box = CeOnlineLayout::ConnectingPanel();
    DrawPanelBox(box, kPanelBg, kBorderSel);
    DrawTextCenter({ box.x0, box.y0 + 18.f, box.x1, box.y0 + 48.f }, kWarn, gModel.ConnectingHeading());
    DrawTextCenter({ box.x0 + 10.f, box.y0 + 56.f, box.x1 - 10.f, box.y0 + 96.f }, kCream, gModel.ConnectingDetail());
    if (gModel.ConnectingCancellable()) {
        if (DrawButton(CeOnlineLayout::ConnectingCancelBtn(), "Cancel", true, true, mx, my, canClick)) {
            gModel.ConnectingCancel();
        }
    }
    DrawHeaderAndFooter(gModel.ConnectingCancellable()
        ? "Press Esc or Cancel to stop."
        : "Match launch in progress. Please wait.");
}

} // namespace

extern "C" {

void PortCeOnlineUiSetBackend(ICeOnlineUiBackend *backend) {
    gBridge.SetInner(backend);
    EnsureBound();
}

void PortCeOnlineHostInitOps(const CeOnlineUiHostOps *ops) {
    if (ops) {
        gOps = *ops;
        gHasOps = true;
    }
}

void PortCeOnlineHostOpen(int from_multiplayer) {
    EnsureBound();
    gModel.Open(from_multiplayer != 0);
}

void PortCeOnlineHostClose(void) {
    gModel.Close();
}

int PortCeOnlineHostActive(void) {
    return gModel.Active() ? 1 : 0;
}

const char *PortCeOnlineHostTitleUtf8(void) {
    return CeOnlinePageTitle(gModel.Page());
}

void PortCeOnlineHostUpdate(uint64_t now_ms) {
    if (!gModel.Active()) return;
    EnsureBound();
    gModel.SyncFromBackend(now_ms);
    HandleExitFlags();
}

void PortCeOnlineHostRender(float mouse_x, float mouse_y, int mouse_moved,
                            int *mouse_wheel, int can_click) {
    if (!gModel.Active()) return;
    const bool moved = mouse_moved != 0;
    const bool click = can_click != 0;
    switch (gModel.Page()) {
    case CE_ONLINE_PAGE_HOME:       RenderHome(mouse_x, mouse_y, moved, click); break;
    case CE_ONLINE_PAGE_BROWSER:    RenderBrowser(mouse_x, mouse_y, moved, mouse_wheel, click); break;
    case CE_ONLINE_PAGE_CREATE:     RenderCreate(mouse_x, mouse_y, moved, click); break;
    case CE_ONLINE_PAGE_JOIN_CODE:  RenderJoinCode(mouse_x, mouse_y, moved, click); break;
    case CE_ONLINE_PAGE_ROOM:       RenderRoom(mouse_x, mouse_y, moved, mouse_wheel, click); break;
    case CE_ONLINE_PAGE_PROFILE:    RenderProfile(mouse_x, mouse_y, moved, click); break;
    case CE_ONLINE_PAGE_CONNECTING: RenderConnecting(mouse_x, mouse_y, click); break;
    }
}

void PortCeOnlineHostButton(int btn) {
    if (!gModel.Active()) return;
    if (btn == CE_OUI_BTN_BACK) {
        PlaySnd(CE_OUI_SND_BACK);
        gModel.NavigateBack();
        HandleExitFlags();
        return;
    }
    switch (gModel.Page()) {
    case CE_ONLINE_PAGE_HOME:
        if (btn == CE_OUI_BTN_UP)     { gModel.HomeMove(-1); PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_DOWN)   { gModel.HomeMove(1);  PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_SELECT) { PlaySnd(CE_OUI_SND_SELECT); gModel.HomeActivate(gModel.HomeFocus()); HandleExitFlags(); }
        break;
    case CE_ONLINE_PAGE_BROWSER:
        if (gModel.PasswordPromptOpen()) {
            if (btn == CE_OUI_BTN_SELECT) { PlaySnd(CE_OUI_SND_SELECT); gModel.BrowserPasswordSubmit(); }
        } else {
            if (btn == CE_OUI_BTN_UP)     { gModel.BrowserMoveSelection(-1); PlaySnd(CE_OUI_SND_MOVE); }
            if (btn == CE_OUI_BTN_DOWN)   { gModel.BrowserMoveSelection(1);  PlaySnd(CE_OUI_SND_MOVE); }
            if (btn == CE_OUI_BTN_LEFT)   { gModel.BrowserPrevPage(); PlaySnd(CE_OUI_SND_MOVE); }
            if (btn == CE_OUI_BTN_RIGHT)  { gModel.BrowserNextPage(); PlaySnd(CE_OUI_SND_MOVE); }
            if (btn == CE_OUI_BTN_SELECT) { PlaySnd(CE_OUI_SND_SELECT); gModel.BrowserJoinSelected(); }
        }
        break;
    case CE_ONLINE_PAGE_CREATE:
        if (btn == CE_OUI_BTN_UP)    { gModel.CreateMoveFocus(-1); PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_DOWN)  { gModel.CreateMoveFocus(1);  PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_LEFT)  { gModel.CreateAdjustFocused(-1); PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_RIGHT) { gModel.CreateAdjustFocused(1);  PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_SELECT) {
            PlaySnd(CE_OUI_SND_SELECT);
            if (gModel.CreateFocus() == CeOnlineUiModel::CREATE_SUBMIT ||
                gModel.CreateFocus() == CeOnlineUiModel::CREATE_NAME ||
                gModel.CreateFocus() == CeOnlineUiModel::CREATE_PASSWORD) {
                gModel.CreateSubmit();
            } else if (gModel.CreateFocus() == CeOnlineUiModel::CREATE_BACK) {
                gModel.NavigateBack(); HandleExitFlags();
            } else {
                gModel.CreateAdjustFocused(1);
            }
        }
        break;
    case CE_ONLINE_PAGE_JOIN_CODE:
        if (btn == CE_OUI_BTN_UP)   { gModel.JoinCodeMoveFocus(-1); PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_DOWN) { gModel.JoinCodeMoveFocus(1);  PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_SELECT) {
            PlaySnd(CE_OUI_SND_SELECT);
            if (gModel.JoinCodeFocus() == CeOnlineUiModel::JOIN_PASTE_BTN) gModel.JoinCodePaste();
            else if (gModel.JoinCodeFocus() == CeOnlineUiModel::JOIN_BACK_BTN) { gModel.NavigateBack(); HandleExitFlags(); }
            else gModel.JoinCodeSubmit();
        }
        break;
    case CE_ONLINE_PAGE_ROOM:
        if (btn == CE_OUI_BTN_UP)    { gModel.RosterMoveSelection(-1); PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_DOWN)  { gModel.RosterMoveSelection(1);  PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_LEFT)  { gModel.RosterPrevPage(); PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_RIGHT) { gModel.RosterNextPage(); PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_SELECT) {
            PlaySnd(CE_OUI_SND_SELECT);
            if (gModel.ChatComposing()) gModel.RoomChatSubmit();
            else gModel.RoomToggleReady();
        }
        break;
    case CE_ONLINE_PAGE_PROFILE:
        if (btn == CE_OUI_BTN_UP)   { gModel.ProfileMoveFocus(-1); PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_DOWN) { gModel.ProfileMoveFocus(1);  PlaySnd(CE_OUI_SND_MOVE); }
        if (btn == CE_OUI_BTN_SELECT) {
            PlaySnd(CE_OUI_SND_SELECT);
            if (gModel.ProfileFocus() == CeOnlineUiModel::PROFILE_RECONNECT_BTN) gModel.ProfileReconnect();
            else if (gModel.ProfileFocus() == CeOnlineUiModel::PROFILE_BACK_BTN) { gModel.NavigateBack(); HandleExitFlags(); }
            else gModel.ProfileSave();
        }
        break;
    case CE_ONLINE_PAGE_CONNECTING:
        if (btn == CE_OUI_BTN_SELECT && gModel.ConnectingCancellable()) {
            PlaySnd(CE_OUI_SND_BACK);
            gModel.ConnectingCancel();
        }
        break;
    }
}

int PortCeOnlineHostKey(int kind, char ch) {
    if (!gModel.Active()) return 0;
    switch (kind) {
    case CE_OUI_KEY_ESC:
        PlaySnd(CE_OUI_SND_BACK);
        gModel.NavigateBack();
        HandleExitFlags();
        return 1;
    case CE_OUI_KEY_PASTE:
        if (gModel.WantsTextInput()) {
            if (gModel.PasteIntoActiveField()) PlaySnd(CE_OUI_SND_SELECT);
            return 1;
        }
        return 0;
    case CE_OUI_KEY_BACKSPACE:
        if (!gModel.WantsTextInput()) return 0;
        switch (gModel.Page()) {
        case CE_ONLINE_PAGE_BROWSER:   gModel.BrowserPasswordBackspace(); break;
        case CE_ONLINE_PAGE_CREATE:    gModel.CreateBackspace(); break;
        case CE_ONLINE_PAGE_JOIN_CODE: gModel.JoinCodeBackspace(); break;
        case CE_ONLINE_PAGE_PROFILE:   gModel.ProfileBackspace(); break;
        case CE_ONLINE_PAGE_ROOM:      gModel.RoomChatBackspace(); break;
        default: break;
        }
        return 1;
    case CE_OUI_KEY_ENTER:
        if (gModel.Page() == CE_ONLINE_PAGE_ROOM && gModel.ChatComposing()) {
            PlaySnd(CE_OUI_SND_SELECT);
            gModel.RoomChatSubmit();
            return 1;
        }
        PortCeOnlineHostButton(CE_OUI_BTN_SELECT);
        return 1;
    case CE_OUI_KEY_UP:    PortCeOnlineHostButton(CE_OUI_BTN_UP); return 1;
    case CE_OUI_KEY_DOWN:  PortCeOnlineHostButton(CE_OUI_BTN_DOWN); return 1;
    case CE_OUI_KEY_LEFT:  if (!gModel.WantsTextInput()) { PortCeOnlineHostButton(CE_OUI_BTN_LEFT); return 1; } return 0;
    case CE_OUI_KEY_RIGHT: if (!gModel.WantsTextInput()) { PortCeOnlineHostButton(CE_OUI_BTN_RIGHT); return 1; } return 0;
    case CE_OUI_KEY_PGUP:
        if (gModel.Page() == CE_ONLINE_PAGE_BROWSER) gModel.BrowserPrevPage();
        else if (gModel.Page() == CE_ONLINE_PAGE_ROOM) gModel.RosterPrevPage();
        PlaySnd(CE_OUI_SND_MOVE);
        return 1;
    case CE_OUI_KEY_PGDN:
        if (gModel.Page() == CE_ONLINE_PAGE_BROWSER) gModel.BrowserNextPage();
        else if (gModel.Page() == CE_ONLINE_PAGE_ROOM) gModel.RosterNextPage();
        PlaySnd(CE_OUI_SND_MOVE);
        return 1;
    case CE_OUI_KEY_CHAT:
        if (gModel.Page() == CE_ONLINE_PAGE_ROOM && !gModel.ChatComposing()) {
            gModel.RoomOpenChat();
            PlaySnd(CE_OUI_SND_SELECT);
            return 1;
        }
        return 0;
    case CE_OUI_KEY_CHAR:
        if (gModel.Page() == CE_ONLINE_PAGE_ROOM && !gModel.ChatComposing()) {
            if (ch == 'r' || ch == 'R') { PlaySnd(CE_OUI_SND_SELECT); gModel.RoomToggleReady(); return 1; }
            if (ch == 's' || ch == 'S') { PlaySnd(CE_OUI_SND_MOVE); gModel.RoomCycleSide(1); return 1; }
            if (ch == 'b' || ch == 'B') { PlaySnd(CE_OUI_SND_MOVE); gModel.RoomCycleBeConfig(1); return 1; }
            if (ch == 'c' || ch == 'C') { PlaySnd(CE_OUI_SND_SELECT); gModel.RoomCopyCode(); return 1; }
            if (ch == 'm' || ch == 'M') { if (gModel.IsLocalHost()) { PlaySnd(CE_OUI_SND_MOVE); gModel.RoomHostCycleMap(1); } return 1; }
            if (ch == 'g' || ch == 'G') { if (gModel.IsLocalHost()) { PlaySnd(CE_OUI_SND_MOVE); gModel.RoomHostCycleMode(1); } return 1; }
            return 0;
        }
        if (!gModel.WantsTextInput()) return 0;
        switch (gModel.Page()) {
        case CE_ONLINE_PAGE_BROWSER:   gModel.BrowserPasswordTypeChar(ch); break;
        case CE_ONLINE_PAGE_CREATE:    gModel.CreateTypeChar(ch); break;
        case CE_ONLINE_PAGE_JOIN_CODE: gModel.JoinCodeTypeChar(ch); break;
        case CE_ONLINE_PAGE_PROFILE:   gModel.ProfileTypeChar(ch); break;
        case CE_ONLINE_PAGE_ROOM:      gModel.RoomChatTypeChar(ch); break;
        default: break;
        }
        return 1;
    default:
        return 0;
    }
}

} // extern "C"

#endif // !CE_NETWORK_OFF
