// SPDX-License-Identifier: GPL-3.0-only
// The menu source (port/game/ce_netpages.cpp) is compiled unchanged, with the real lobby, code, ICE, boot and invitation
// headers and in-process platform, lobby, invitation and signaling providers. Built twice by network_player_menu_tests.py:
// without CE_NETWORK_ICE (the --with-network menu: Host Game, Join Game and Connection Help over a direct connection) and
// with it (the --with-ice menu adds the Online invitation import, its preview, the network disclosure and the signaling
// status page). No game, socket, file or service.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <vector>
static std::vector<const void *> renderedItems;
// The headers the menu includes, in its order (the test strips its project include lines).
#include "ce_network_stl.h"
#include "ce_netcode.h"
#include "ce_netlobby.h"
#include "ice_game.h"
#include "ce_network_boot.h"
#include "ce_network_invitation.h"
#include "private_ui_input.h"
#include "ce_menu_fit.h"
using BOOL = int;
using DWORD = uint32_t;
using WORD = uint16_t;
using SINT = int;
using WCHAR = wchar_t;
using HWND = void*;
using EPCText = int;
enum EMemoryType { MENU_MEMORY };
constexpr int FALSE = 0, TRUE = 1;
enum EFontType { FONT_SMALL, FONT_NORMAL };
enum KeyEventType { KEY_EVENT_UP, KEY_EVENT_DOWN, KEY_EVENT_CHAR };
using pKeyTrapper = void (*)(WORD, KeyEventType);
enum { MEMTYPE_MENUITEM, PORT_CE_NO_LISTS, FES_BACK, FES_SELECT, FES_MOVE, FEP_LANGUAGE_TEST,
       BUTTON_FRONTEND_MENU_UP, BUTTON_FRONTEND_MENU_DOWN, BUTTON_FRONTEND_MENU_LEFT, BUTTON_FRONTEND_MENU_RIGHT, BUTTON_PAUSE_MENU_SELECT,
       BUTTON_FRONTEND_MENU_BACK, DIK_UP, DIK_DOWN, DIK_TAB, DIK_ESCAPE, DIK_RETURN,
       DIK_NUMPADENTER, DIK_BACK, DIK_LCONTROL, DIK_RCONTROL, DIK_V, WM_CLOSE };
void* operator new(size_t size, int) { return ::operator new(size); }
void operator delete(void* memory, int) noexcept { ::operator delete(memory); }
#define SAFE_DELETE(value) do { delete (value); (value) = nullptr; } while (0)
struct SIZE { int cx, cy; };
struct SWordWrapFill { WCHAR c[128][1024]; };
static unsigned checks, failures, postClose;
static float testTime = 10.f;
static bool menusOff;
struct CDXBitmapFont {
  bool CanDisplay(WCHAR) { return true; }
  int GetHeight() { return 16; }
  // Greedy by words, 8 pixels a character (GetTextExtent's measure), as the game's font wraps.
  int WordWrap(SWordWrapFill* fill, const WCHAR* text, float width) {
    std::wstring all(text), line;
    int n = 0;
    size_t at = 0;
    while (at <= all.size() && n < 128) {
      size_t end = all.find(L' ', at);
      std::wstring word = all.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
      std::wstring next = line.empty() ? word : line + L" " + word;
      if (!line.empty() && float(next.size() * 8) > width) { std::wcsncpy(fill->c[n], line.c_str(), 1023); fill->c[n++][1023] = 0; line = word; }
      else line = next;
      if (end == std::wstring::npos) break;
      at = end + 1;
    }
    if (!line.empty() && n < 128) { std::wcsncpy(fill->c[n], line.c_str(), 1023); fill->c[n++][1023] = 0; }
    return n;
  }
  void GetTextExtent(const WCHAR* text, SIZE* size) { size->cx = int(std::wcslen(text))*8; size->cy = 16; }
  void DrawTextWithShadow(float, float, DWORD, const WCHAR* text, int, int, float, float);
};
static std::vector<std::wstring> drawnText;
void CDXBitmapFont::DrawTextWithShadow(float, float, DWORD, const WCHAR* text, int, int, float, float) { drawnText.emplace_back(text); }
static bool Drawn(const wchar_t* text) {
  for (const auto& line : drawnText) if (line.find(text) != std::wstring::npos) return true;
  return false;
}
struct Platform {
  CDXBitmapFont font;
  CDXBitmapFont* Font(EFontType) { return &font; }
  float GetSysTimeFloat() { return testTime; }
} PLATFORM;
struct Controller {};
using CController = Controller;
template<class T> struct Items {
  std::vector<T> values;
  int Size() const { return int(values.size()); }
  T& At(int i) { return values.at(size_t(i)); }
  void Append(T value) { values.push_back(value); }
};
struct CMenuItem {
  int mId; BOOL mEnabled = TRUE;
  CMenuItem(EPCText, int id) : mId(id) {}
  virtual ~CMenuItem() = default;
  virtual WCHAR* GetText() { static WCHAR empty[] = L""; return empty; }
  virtual int GetHeight() { return 16; }
  virtual void Render(float, float, BOOL) { renderedItems.push_back(this); }
  virtual BOOL IsEnabled() { return mEnabled; }
};
struct CMenuOptionsList {
  Items<CMenuItem*> mItems;
  int mSelected = 0;
  float mX, mY;
  CMenuOptionsList(WCHAR*, float x, float y, BOOL, DWORD) : mX(x), mY(y) {}
  ~CMenuOptionsList() { for (auto* p : mItems.values) delete p; }
  void AddItem(CMenuItem* p) { mItems.Append(p); }
  void Reset() { mSelected = 0; }
  template<class... T> void Render(T...) { for (int i = 0; i < mItems.Size(); ++i) mItems.At(i)->Render(0.0f, 0.0f, FALSE); }
  BOOL PassToSelected(CController*, int, float) { return FALSE; }
  void ReceiveButtonAction(CController*, int button, float) {
    int step = button == BUTTON_FRONTEND_MENU_UP ? -1 : 1;
    if (button != BUTTON_FRONTEND_MENU_UP && button != BUTTON_FRONTEND_MENU_DOWN) return;
    for (int i = 0; i < mItems.Size(); ++i) {
      mSelected = (mSelected + step + mItems.Size()) % mItems.Size();
      if (mItems.At(mSelected)->IsEnabled()) break;
    }
  }
};
struct CPauseMenu {
  static bool mOptionsPage;
  Items<CMenuOptionsList*> mLists;
  float mOpenTime = 0.f;
  int mPage = 0;
  BOOL mActive = FALSE;
  CPauseMenu(int) {}
  virtual ~CPauseMenu() { for (auto* p : mLists.values) delete p; }
  void Activate(BOOL = FALSE) { mOpenTime = testTime; mActive = TRUE; }
  void DeActivate(BOOL) { mActive = FALSE; }
  void Apply() {}
  void Render() {}
};
bool CPauseMenu::mOptionsPage;
static std::vector<int> sounds;
struct CFrontEnd { static void PlaySound(int cue) { sounds.push_back(cue); } };
// The game's message box: what the pages put up (each text, in order) and whether one is up; Dismiss() is the player's OK.
#define FO(x) (float(x) / 1000.f)
enum { FEMB_OPTION_A_CONTINUE = 3, FEP_NONE = -1 };
static std::vector<std::wstring> boxes;
struct {
  BOOL shown = FALSE; float mY = 0.f, mHeight = 64.f;
  BOOL BeingDisplayed() { return shown; }
  void Kill() { shown = FALSE; }
  void Create(float, float y, float, float, WCHAR* text, CDXBitmapFont*, unsigned, unsigned, BOOL, int, unsigned) {
    mY = y; boxes.emplace_back(text); shown = TRUE;
  }
  void SetOnDismiss(int, int) {}
} FEMESSBOX;
static void Dismiss() { FEMESSBOX.shown = FALSE; }
static bool BoxSays(const char* text) {
  if (boxes.empty()) return false;
  return boxes.back() == std::wstring(text, text + std::strlen(text));
}
// The lobby's player slots (port/game/ce_network_lobby_slots.cpp): how many were drawn.
static unsigned slotsDrawn;
void PortCeNetDrawSlot(float, float, bool) { ++slotsDrawn; }
// The pointer in the front end's 640x480 screen, and a left click waiting to be read (src/mouse.cpp).
int MOUSE_X = 320, MOUSE_Y = 10;
static bool mouseReleased;
bool MouseClickedInRect(float l, float t, float r, float b) {
  if (mouseReleased && MOUSE_X >= l && MOUSE_X < r && MOUSE_Y >= t && MOUSE_Y < b) { mouseReleased = false; return true; }
  return false;
}
struct { BOOL mAutoConfigTest = FALSE, mPlaybackDemo = FALSE, mRecordDemo = FALSE, mAttractMode = FALSE; } CLIPARAMS;
struct { void SetPage(int, BOOL = FALSE) {} void SetUnknown2ed(int) {} } FRONTEND;
enum { D3DBLEND_INVSRCALPHA, D3DBLEND_SRCALPHA, D3DCMP_LESSEQUAL,
       D3DRS_ALPHABLENDENABLE, D3DRS_ALPHAREF, D3DRS_ALPHATESTENABLE,
       D3DRS_DESTBLEND, D3DRS_SRCBLEND, D3DRS_ZENABLE, D3DRS_ZFUNC,
       D3DRS_ZWRITEENABLE, D3DTA_DIFFUSE, D3DTA_TEXTURE, D3DTOP_MODULATE,
       D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTSS_COLOROP,
       TADDRESS_CLAMP, TSS_ADDRESSU, TSS_ADDRESSV };
struct {
  void SetFogEnabled(BOOL) {} void SetUnknown2ed(int) {} void Apply() {}
  template<class... T> void SRS(T...) {}
  template<class... T> void STS(T...) {}
} RENDERINFO;
struct Shell {
  template<class... T> void SRS(T...) {}
  template<class... T> void STS(T...) {}
  pKeyTrapper trap = nullptr;
  void SetKeytrap(pKeyTrapper p) { trap = p; }
  pKeyTrapper GetKeytrap() { return trap; }
  HWND GetHWnd() { return nullptr; }
} LT;
int PostMessage(HWND, unsigned message, uintptr_t, intptr_t) { if (message == WM_CLOSE) ++postClose; return 1; }
bool PortCeMenus() { return !menusOff; }
bool PortRunOffersNetworkPages() { return true; }
static CE_LobbyStatus status{};
static std::vector<std::string> addresses = {"192.0.2.10", "192.0.2.11"};
static unsigned hostAutomatic, hostSelected, leaveCalls, startCalls, joinCalls;
static bool fakeConnection;
static unsigned campaignCalls, campaignAccepted;
static uint32_t campaignRequested;
static bool campaignAvailable = true, campaignFailure;
static CE_LobbyCampaignStartResult campaignResult = CE_LOBBY_CAMPAIGN_START_QUEUED;
static int64_t handoffNow = 1000000;
static std::string selectedAddress, joinedCode, joinedAddress;

// The invitation and signaling providers. Every count is a call the menu made; "contacted" counts the calls that would
// reach the signaling service (preflight, consent, retry): none may happen before the disclosure's Continue.
static CE_InvitationView invitation{};
static unsigned beginCalls, pollCalls, installCalls, cancelCalls, pollsSinceBegin;
static uint32_t beginRole;
static std::string beginOrigin;
static uint32_t previewRole = CE_NET_HOST;
static uint64_t previewLife = 600;
static bool refuseBegin, refuseInstall;
static bool signalingSelected;
static CeIceSignalView signalView{};
static unsigned preflightCalls, pumpCalls, acceptCalls, retryCalls, clearCalls;
static const char* signalOrigin = "https://signal.example.test";

extern "C" {
int CE_LobbyAvailable(char* why, size_t size) { if (why && size) why[0] = 0; return 1; }
static int cheatOn = -1;	// the one Cheats-page switch the case turns on (-1: none)
extern "C" int CE_CheatOn(int n) { return n == cheatOn; }
int CE_LobbyAddresses(char out[][16], int max) {
  int count = std::min(max, int(addresses.size()));
  for (int i = 0; i < count; ++i) std::snprintf(out[i], 16, "%s", addresses[size_t(i)].c_str());
  return count;
}
uint16_t CE_LobbyPort() { return 12345; }
int CE_LobbyHost() { ++hostAutomatic; fakeConnection = true; status.host = 1; status.state = CE_LOBBY_HOST_WAITING; return 1; }
int CE_LobbyHostAt(const char* selected) {
  ++hostSelected; selectedAddress = selected;
  if (std::find(addresses.begin(), addresses.end(), selectedAddress) == addresses.end()) {
    std::snprintf(status.notice, sizeof(status.notice), "%s", "Address no longer available"); return 0;
  }
  fakeConnection = true; status.host = 1; status.state = CE_LOBBY_HOST_WAITING; return 1;
}
int CE_LobbyJoin(const char* code, const char* address, const char** problem) {
  ++joinCalls; joinedCode = code; joinedAddress = address; if (problem) *problem = nullptr;
  status.host = 0; status.state = CE_LOBBY_JOIN_CONNECTING; return 1;
}
void CE_LobbyUpdate() {}
void CE_LobbyStatusGet(CE_LobbyStatus* out) { *out = status; }
int CE_LobbyNextLevel() { return 1; }
int CE_LobbyStart() { ++startCalls; if (status.state != CE_LOBBY_READY) return 0; status.state = CE_LOBBY_STARTING; return 1; }
CE_LobbyCampaignStartResult CE_LobbyCampaignStart(uint32_t selectedMission) {
  ++campaignCalls; campaignRequested = selectedMission;
  if (!campaignAvailable) campaignResult = CE_LOBBY_CAMPAIGN_START_UNAVAILABLE;
  else if (!status.host) campaignResult = CE_LOBBY_CAMPAIGN_START_NOT_HOST;
  else if (status.state != CE_LOBBY_READY) campaignResult = CE_LOBBY_CAMPAIGN_START_NOT_READY;
  else if (selectedMission != status.level) campaignResult = CE_LOBBY_CAMPAIGN_START_STALE_SELECTION;
  else if (campaignFailure) campaignResult = CE_LOBBY_CAMPAIGN_START_FAILED;
  else { ++campaignAccepted; campaignResult = CE_LOBBY_CAMPAIGN_START_QUEUED; }
  return campaignResult;
}
void CE_LobbyLeave() { ++leaveCalls; fakeConnection = false; status.state = CE_LOBBY_OFF; }
void CE_LobbyShutdown() { CE_LobbyLeave(); }
int CE_LobbyRestartDue() { return status.state == CE_LOBBY_STARTING; }
int CE_LobbyCopy(const char*) { return 1; }
int CE_LobbyPaste(char*, size_t) { return 0; }
int CE_LobbyCtrlHeld() { return 0; }
static const char* outcomeText;
static int rematchRole = 0;
int CE_LobbyResumeMatch(void) { return rematchRole; }
int CE_LobbyTakeOutcome(char* out, size_t size, int* good) {
  if (!outcomeText) return 0;
  std::snprintf(out, size, "%s", outcomeText); *good = 1; outcomeText = nullptr; return 1;
}
int CE_LobbyOutcomePending() { return 0; }
void CE_LobbyShellExit(int, int) {}
void CE_LobbyAfterExit() {}
int CE_CodeDecode(const char* text, CE_JoinCode* code) { if (code) std::memset(code, 0, sizeof(*code)); return !text || !text[0] ? CE_CODE_EMPTY : !std::strcmp(text, "X") ? CE_CODE_SHORT : CE_CODE_OK; }
int CE_CodeEncode(const CE_JoinCode*, char* out, size_t size) { if (!out || size < 4) return 0; std::memcpy(out,"ABC",4); return 1; }
const char* CE_CodeProblem(int) { return "fixture input required"; }
void CE_CodeWipe(void* p, size_t n) { std::memset(p, 0, n); }
int64_t CE_HandoffNow(void) { return handoffNow; }
#if defined(CE_NETWORK_ICE)
// Relay is requested by the selected signaling directory (ice_game.h); this fixture's selection stands in for it.
int CE_NetSignalingDirectorySelected(void) { return signalingSelected ? 1 : 0; }
int CE_InvitationProfileAvailable(uint32_t, uint64_t) { return 0; }
int CE_InvitationBeginProfileImport(const char*, uint32_t) { return 0; }
int CE_InvitationBeginImport(const char* origin, uint32_t role) {
  ++beginCalls; pollsSinceBegin = 0; beginOrigin = origin ? origin : ""; beginRole = role;
  invitation = CE_InvitationView{};
  invitation.role = role;
  if (refuseBegin) { invitation.state = CE_INVITATION_REFUSED; invitation.error = CE_INVITATION_ERROR_ORIGIN; return 0; }
  invitation.state = CE_INVITATION_CHOOSING;
  return 1;
}
void CE_InvitationPoll(uint64_t, CE_InvitationView* out) {
  ++pollCalls; ++pollsSinceBegin;
  // The folder chooser answers at the second poll after Begin (the menu polls once inside Begin, then once a frame).
  if (invitation.state == CE_INVITATION_CHOOSING && pollsSinceBegin >= 2) {
    invitation.state = CE_INVITATION_PREVIEW_READY;
    invitation.role = previewRole;
    invitation.expires_at = uint64_t(handoffNow) + previewLife;
    std::snprintf(invitation.origin, sizeof invitation.origin, "%s", beginOrigin.c_str());
  }
  *out = invitation;
}
int CE_InvitationInstall(uint64_t) {
  ++installCalls;
  if (refuseInstall || invitation.state != CE_INVITATION_PREVIEW_READY) {
    invitation.state = CE_INVITATION_REFUSED; invitation.error = CE_INVITATION_ERROR_EXPIRED; return 0;
  }
  invitation.state = CE_INVITATION_SELECTED; signalingSelected = true;
  return 1;
}
void CE_InvitationCancel(void) {
  ++cancelCalls;
  if (invitation.state == CE_INVITATION_CHOOSING || invitation.state == CE_INVITATION_PREVIEW_READY) invitation.state = CE_INVITATION_CANCELLED;
}
const char* CE_InvitationErrorText(uint32_t error) {
  return error == CE_INVITATION_ERROR_ORIGIN ? "fixture: origin refused" : error == CE_INVITATION_ERROR_EXPIRED ? "fixture: expired" : "fixture: refused";
}
#endif
}
#if defined(CE_NETWORK_ICE)
void CE_LobbySignalingClear() noexcept { ++clearCalls; signalingSelected = false; }
bool CE_LobbySignalingSelected() noexcept { return signalingSelected; }
const char* CE_LobbySignalingOrigin() noexcept { return signalingSelected ? signalOrigin : nullptr; }
bool CE_LobbySignalingBeginPreflight() noexcept { ++preflightCalls; return true; }
void CE_LobbySignalingPump() noexcept { ++pumpCalls; }
CeIceSignalView CE_LobbySignalingView() noexcept { return signalingSelected ? signalView : CeIceSignalView{}; }
bool CE_LobbySignalingAccept() noexcept { ++acceptCalls; return true; }
bool CE_LobbySignalingRetry() noexcept { ++retryCalls; return true; }
#endif
#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
static std::string practiceOrigin, practiceCode;
bool PortPrivateUiFieldWaiting(unsigned field) noexcept {
    return field == PORT_PRIVATE_UI_ORIGIN ? !practiceOrigin.empty() :
           field == PORT_PRIVATE_UI_JOIN_CODE && !practiceCode.empty();
}
bool PortPrivateUiTakeField(unsigned field, char *out, unsigned capacity) noexcept {
    if (!PortPrivateUiFieldWaiting(field)) return false;
    auto &value = field == PORT_PRIVATE_UI_ORIGIN ? practiceOrigin : practiceCode;
    if (value.size() >= capacity) return false;
    memcpy(out, value.c_str(), value.size()+1); value.clear(); return true;
}
#endif
#include "menu.inc"
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::printf("MENU_ASSERTION %u line %d\n", checks, __LINE__); return 1; } } while (0)
static void Button(int button) { testTime += 1.f; PortCeNetButton(button, 1.f); }
static void Select() { Button(BUTTON_PAUSE_MENU_SELECT); }
static void Down() { Button(BUTTON_FRONTEND_MENU_DOWN); }
static void BackButton() { Button(BUTTON_FRONTEND_MENU_BACK); }
static int ActionIndex(int action) {
    for (int i = 0; i < sMenu->mList[sMenu->mPage]->mItems.Size(); ++i) {
        CNetItem *item = static_cast<CNetItem *>(sMenu->mList[sMenu->mPage]->mItems.At(i));
        if (item->mAction == action) return i;
    }
    return -1;
}
static bool SelectAction(int action) {
    const int index = ActionIndex(action);
    if (index < 0) return false;
    sMenu->mList[sMenu->mPage]->mSelected = index;
    Select();
    return true;
}
static void MenuFrame(bool render = true) {
    testTime += 0.25f;
    PortCeNetProcess();
    if (render) PortCeNetDraw();
}
// The first page: Host Game hosts at once, Join Game opens the code page, both on the launch's (direct) connection.
static void OpenHost() { PortCeNetOpen(); Select(); }
static void OpenJoin() { PortCeNetOpen(); Down(); Select(); }
static int Focus() { return sMenu->mList[sMenu->mPage]->mSelected; }
static int FocusAction() { return static_cast<CNetItem *>(sMenu->mList[sMenu->mPage]->mItems.At(Focus()))->mAction; }
static void Right() { Button(BUTTON_FRONTEND_MENU_RIGHT); }
static int Contacted() { return int(preflightCalls + acceptCalls + retryCalls); }
static void Type(const char *text) { for (const char *c = text; *c; ++c) NetKeyTrap(WORD(*c), KEY_EVENT_CHAR); }

static int RunCampaign(const std::string &name) {
    // Campaign co-op is not proven: its lobby row exists for testers only (BEA_NET_LOBBY_CAMPAIGN=1).
    if (name == "campaign-row-absent") {
        OpenHost(); status.state = CE_LOBBY_READY; MenuFrame();
        CHECK(sMenu->mPage == NP_LOBBY && sMenu->mCampaignStart->mHidden && !sMenu->mCampaignStart->IsEnabled());
        CHECK(SelectAction(NA_CAMPAIGN_START) && campaignCalls == 0 && startCalls == 0);
        return 0;
    }
    setenv("BEA_NET_LOBBY_CAMPAIGN", "1", 1);
    OpenHost();
    status.level = 10001;
    status.state = CE_LOBBY_READY;
    status.host = true;
    if (name == "campaign-guest-disabled") status.host = false;
    if (name == "campaign-not-ready-disabled") status.state = CE_LOBBY_HOST_WAITING;
    MenuFrame(false);
    if (name == "campaign-not-ready-disabled") {
        CHECK(sMenu->mPage == NP_HOST_ADDRESS);
        CHECK(!sMenu->mCampaignStart->mEnabled);
        CHECK(ActionIndex(NA_CAMPAIGN_START) < 0);
        sMenu->Choose(sMenu->mCampaignStart);
        CHECK(campaignCalls == 0 && startCalls == 0 && postClose == 0);
        return 0;
    }
    CHECK(sMenu->mPage == NP_LOBBY);
    CHECK(ActionIndex(NA_START) >= 0 && ActionIndex(NA_CAMPAIGN_START) >= 0 &&
          ActionIndex(NA_START) != ActionIndex(NA_CAMPAIGN_START));
    if (name == "campaign-unrendered") {
        CHECK(SelectAction(NA_CAMPAIGN_START));
        CHECK(campaignCalls == 0);
        CHECK(startCalls == 0 && postClose == 0 && !sMenu->mQuitPosted);
        return 0;
    }
    renderedItems.clear(); drawnText.clear();
    PortCeNetDraw();
    const uint32_t displayed = status.level;
    CHECK(sMenu->mCampaignMissionKnown);
    // The mission was on the screen: the host's Mission row, or the guest's Mission line.
    CHECK(Drawn(L"Mission"));
    if (name == "campaign-guest-disabled") {
        CHECK(!sMenu->mCampaignStart->mEnabled);
        CHECK(static_cast<CNetItem *>(sMenu->mList[sMenu->mPage]->mItems.At(sMenu->mList[sMenu->mPage]->mSelected))->mAction == NA_LEAVE);
        CHECK(SelectAction(NA_CAMPAIGN_START));
        CHECK(campaignCalls == 0 && startCalls == 0 && postClose == 0);
        return 0;
    }
    if (name == "campaign-late-back") {
        BackButton();
        CHECK(sMenu->mPage == NP_MENU && leaveCalls == 1 && FocusAction() == NA_HOST);
        sMenu->Choose(sMenu->mCampaignStart);
        CHECK(campaignCalls == 0 && startCalls == 0 && postClose == 0);
        return 0;
    }
    if (name == "campaign-unrendered-reentry") {
        BackButton();
        OpenHost();
        status.state = CE_LOBBY_READY;
        MenuFrame(false);
        CHECK(!sMenu->mCampaignMissionKnown);
        CHECK(SelectAction(NA_CAMPAIGN_START));
        CHECK(campaignCalls == 0 && startCalls == 0 && postClose == 0);
        return 0;
    }
    if (name == "campaign-ordinary-start") {
        CHECK(SelectAction(NA_START));
        CHECK(startCalls == 1 && campaignCalls == 0);
        return 0;
    }
    if (name == "campaign-stale") {
        ++status.level;
        MenuFrame(false);
        CHECK(SelectAction(NA_CAMPAIGN_START));
        CHECK(campaignCalls == 1 && campaignRequested == displayed && campaignAccepted == 0);
        CHECK(startCalls == 0 && postClose == 0 && leaveCalls == 0 && !sMenu->mQuitPosted);
        CHECK(campaignResult == CE_LOBBY_CAMPAIGN_START_STALE_SELECTION &&
              std::string(sMenu->mNotice).find("changed") != std::string::npos);
        CHECK(SelectAction(NA_CAMPAIGN_START));
        CHECK(campaignCalls == 2 && campaignRequested == displayed && campaignAccepted == 0);
        MenuFrame();
        CHECK(campaignCalls == 2 && campaignAccepted == 0);
        CHECK(SelectAction(NA_CAMPAIGN_START));
        CHECK(campaignCalls == 3 && campaignRequested == status.level && campaignAccepted == 1 &&
              startCalls == 0 && postClose == 0 && !sMenu->mQuitPosted);
        return 0;
    }
    CE_LobbyCampaignStartResult expected = CE_LOBBY_CAMPAIGN_START_QUEUED;
    const char *notice = "Waiting";
    if (name == "campaign-late-not-ready") {
        status.state = CE_LOBBY_HOST_WAITING;
        expected = CE_LOBBY_CAMPAIGN_START_NOT_READY;
        notice = "ready";
    } else if (name == "campaign-late-not-host") {
        status.host = false;
        expected = CE_LOBBY_CAMPAIGN_START_NOT_HOST;
        notice = "host";
    } else if (name == "campaign-unavailable") {
        campaignAvailable = false;
        expected = CE_LOBBY_CAMPAIGN_START_UNAVAILABLE;
        notice = "unavailable";
    } else if (name == "campaign-failed") {
        campaignFailure = true;
        expected = CE_LOBBY_CAMPAIGN_START_FAILED;
        notice = "retry";
    }
    CHECK(SelectAction(NA_CAMPAIGN_START));
    CHECK(campaignCalls == 1 && campaignRequested == displayed);
    CHECK(startCalls == 0 && postClose == 0 && leaveCalls == 0 && !sMenu->mQuitPosted);
    CHECK(campaignResult == expected && campaignAccepted == (expected == CE_LOBBY_CAMPAIGN_START_QUEUED ? 1u : 0u) &&
          std::string(sMenu->mNotice).find(notice) != std::string::npos);
    if (name == "campaign-queued") {
        MenuFrame();
        CHECK(postClose == 0 && !sMenu->mQuitPosted);
        status.state = CE_LOBBY_STARTING;
        MenuFrame();
        testTime += 100.0f;
        MenuFrame();
        MenuFrame();
        CHECK(postClose == 1 && sMenu->mQuitPosted);
    }
    return 0;
}

// The first page, both builds: its rows, and that nothing connects until Host Game or Join Game is chosen.
static int RunEntry(const std::string& name) {
  if (name == "entry-direct") {
    PortCeNetOpen(); MenuFrame();
    CHECK(sMenu->mPage == NP_MENU && Focus() == 0 && FocusAction() == NA_HOST);
#if defined(CE_NETWORK_ICE)
    CHECK(sMenu->mInvitationEntry && sMenu->mInvitationEntry->IsEnabled() && ActionIndex(NA_IMPORT_OPEN) == 2);
#else
    CHECK(!sMenu->mInvitationEntry && ActionIndex(NA_IMPORT_OPEN) < 0);
#endif
    CHECK(ActionIndex(NA_HOST) == 0 && ActionIndex(NA_JOIN) == 1 && ActionIndex(NA_HELP) >= 2 &&
          ActionIndex(NA_BACK) == sMenu->mList[NP_MENU]->mItems.Size() - 1);
    CHECK(sMenu->mHostItem->IsEnabled() && sMenu->mJoinItem->IsEnabled());
    CHECK(hostAutomatic == 0 && hostSelected == 0 && joinCalls == 0 && leaveCalls == 0 && Contacted() == 0 && !sMenu->mConnectionSelected);
    CHECK(SelectAction(NA_HOST) && sMenu->mConnectionSelected && !sMenu->mConnectionRelay && hostSelected == 1);
    BackButton(); CHECK(sMenu->mPage == NP_MENU && !sMenu->mConnectionSelected && leaveCalls == 1 && FocusAction() == NA_HOST);
    return 0;
  }
  if (name == "relay-unavailable") {
    // A launch prepared for the relay in a build without it: Host and Join say so, nothing is started.
    setenv("BEA_NET_ICE_DIRECTORY", "fixture-relay-directory", 1);
    PortCeNetOpen(); MenuFrame();
    CHECK(SelectAction(NA_HOST));
    CHECK(sMenu->mPage == NP_MENU && std::string(sMenu->mNotice).find("unavailable in this build") != std::string::npos);
    CHECK(SelectAction(NA_JOIN));
    CHECK(sMenu->mPage == NP_MENU && !sMenu->mConnectionSelected && hostAutomatic == 0 && hostSelected == 0 && joinCalls == 0);
    return 0;
  }
  if (name == "cheats-refused") {
    // A cheat on: Host and Join say to turn it off, and nothing is hosted or joined.
    cheatOn = 3;
    PortCeNetOpen(); MenuFrame();
    CHECK(SelectAction(NA_HOST));
    CHECK(sMenu->mPage == NP_MENU && std::string(sMenu->mNotice) == "Network games need every cheat off. Turn them off on the Cheats page, then try again.");
    CHECK(SelectAction(NA_JOIN));
    CHECK(sMenu->mPage == NP_MENU && hostAutomatic == 0 && hostSelected == 0 && joinCalls == 0);
    return 0;
  }
  if (name == "help-back") {
    PortCeNetOpen(); MenuFrame();
    CHECK(SelectAction(NA_HELP) && sMenu->mPage == NP_HELP);
    BackButton();
    CHECK(sMenu->mPage == NP_MENU && FocusAction() == NA_HELP && hostAutomatic == 0 && hostSelected == 0 && joinCalls == 0);
    return 0;
  }
  return 2;
}

#if defined(CE_NETWORK_ICE)
// The Online invitation: import (offline), preview, explicit use, the network disclosure, then consent and the connection.
static int ImportPreview(uint32_t role) {
  PortCeNetOpen(); MenuFrame();
  CHECK(SelectAction(NA_IMPORT_OPEN));
  CHECK(sMenu->mPage == NP_INVITATION_INPUT && LT.trap && sMenu->mList[NP_INVITATION_INPUT]->mSelected == sMenu->mImportOriginRow);
  CHECK(std::string(sMenu->mImportOrigin->mValue) == "https://");
  Type("invite.example.test");
  CHECK(std::string(sMenu->mImportOrigin->mValue) == "https://invite.example.test");
  CHECK(SelectAction(role == CE_NET_HOST ? NA_IMPORT_HOST : NA_IMPORT_JOIN));
  CHECK(beginCalls == 1 && beginRole == role && beginOrigin == "https://invite.example.test");
  if (refuseBegin) return 0;
  CHECK(sMenu->mInvitation.state == CE_INVITATION_CHOOSING && sMenu->mPage == NP_INVITATION_INPUT);
  CHECK(!sMenu->mImportOrigin->IsEnabled() && !sMenu->mImportHost->IsEnabled() && !sMenu->mImportJoin->IsEnabled());
  MenuFrame();
  CHECK(sMenu->mPage == NP_INVITATION_PREVIEW);
  CHECK(installCalls == 0 && !signalingSelected && Contacted() == 0);
  return 0;
}

static int RunIce(const std::string& name) {
    if (name == "ice-practice-fields") {
        practiceOrigin = "https://practice.invalid"; practiceCode = "ABC";
        PortCeNetOpen(); MenuFrame();
        CHECK(Drawn(L"Local practice") && beginCalls == 0 && acceptCalls == 0 && joinCalls == 0);
        CHECK(SelectAction(NA_IMPORT_OPEN)); MenuFrame();
        CHECK(std::string(sMenu->mImportOrigin->mValue) == "https://practice.invalid" && practiceOrigin.empty());
        CHECK(beginCalls == 0 && acceptCalls == 0 && joinCalls == 0 && !practiceCode.empty());
        previewRole = CE_NET_GUEST; CHECK(SelectAction(NA_IMPORT_JOIN)); MenuFrame();
        CHECK(sMenu->mPage == NP_INVITATION_PREVIEW && installCalls == 0 && !Contacted());
        CHECK(SelectAction(NA_IMPORT_USE)); MenuFrame();
        CHECK(sMenu->mPage == NP_JOIN && !sMenu->mCode->mValue[0] && joinCalls == 0);
        CHECK(SelectAction(NA_CONNECT)); MenuFrame();
        CHECK(sMenu->mPage == NP_SIGNAL_DISCLOSURE && acceptCalls == 0 && joinCalls == 0);
        CHECK(SelectAction(NA_SIGNAL_DISCLOSE)); signalView.status = CeIceSignalStatus::NeedsConsent;
        MenuFrame(); MenuFrame(); CHECK(acceptCalls == 0 && joinCalls == 0 && !practiceCode.empty());
        CHECK(SelectAction(NA_SIGNAL_ACCEPT) && acceptCalls == 1);
        signalView.status = CeIceSignalStatus::AwaitingPairConsent; MenuFrame();
        CHECK(sMenu->mPage == NP_SIGNAL_STATUS && !sMenu->mSignalProceed->IsEnabled() && !practiceCode.empty());
        signalView.status = CeIceSignalStatus::Admitted; MenuFrame(); MenuFrame();
        CHECK(sMenu->mPage == NP_SIGNAL_STATUS && !practiceCode.empty() && joinCalls == 0 && acceptCalls == 1);
        CHECK(sMenu->mList[NP_SIGNAL_STATUS]->mSelected == ActionIndex(NA_SIGNAL_PROCEED));
        CHECK(SelectAction(NA_SIGNAL_PROCEED)); MenuFrame(); MenuFrame();
        CHECK(sMenu->mPage == NP_JOIN && practiceCode.empty() && sMenu->mCode->mValue[0] && joinCalls == 0);
        CHECK(SelectAction(NA_CONNECT) && joinCalls == 1 && acceptCalls == 1);
        return 0;
    }

  if (name == "ice-invitation-refused") {
    refuseBegin = true;
    if (ImportPreview(CE_NET_HOST)) return 1;
    CHECK(sMenu->mPage == NP_INVITATION_INPUT && std::string(sMenu->mNotice) == "fixture: origin refused");
    CHECK(installCalls == 0 && !signalingSelected && Contacted() == 0);
    return 0;
  }
  if (name == "ice-wrong-role") {
    previewRole = CE_NET_GUEST;          // a Join folder chosen for Host: never usable
    if (ImportPreview(CE_NET_HOST)) return 1;
    CHECK(!sMenu->mImportUse->IsEnabled());
    CHECK(SelectAction(NA_IMPORT_USE));
    CHECK(installCalls == 0 && !signalingSelected);
    return 0;
  }
  if (name == "ice-expired-at-use") {
    previewLife = 60;
    if (ImportPreview(CE_NET_HOST)) return 1;
    CHECK(sMenu->mImportUse->IsEnabled());
    handoffNow += 120;                   // the preview expires between the last frame and the press
    CHECK(SelectAction(NA_IMPORT_USE));
    CHECK(installCalls == 0 && !signalingSelected && std::string(sMenu->mNotice).find("expired") != std::string::npos);
    return 0;
  }
  if (name == "ice-preview-back") {
    if (ImportPreview(CE_NET_HOST)) return 1;
    BackButton();
    CHECK(sMenu->mPage == NP_INVITATION_INPUT && cancelCalls >= 2 && installCalls == 0);
    BackButton();
    CHECK(sMenu->mPage == NP_MENU && !LT.trap && installCalls == 0 && Contacted() == 0);
    return 0;
  }
  const bool host = name != "ice-join" && name != "ice-guest-awaiting-code";
  previewRole = host ? CE_NET_HOST : CE_NET_GUEST;   // the folder holds the chosen role
  if (ImportPreview(host ? CE_NET_HOST : CE_NET_GUEST)) return 1;
  if (name == "ice-terminal") signalView.status = CeIceSignalStatus::Closed;
  CHECK(sMenu->mImportUse->IsEnabled());
  CHECK(SelectAction(NA_IMPORT_USE));
  CHECK(installCalls == 1 && signalingSelected && sMenu->mConnectionSelected && sMenu->mConnectionRelay);
  if (name == "ice-terminal") {
    // A closed pair goes to the status page itself, where nothing can continue it.
    signalView.status = CeIceSignalStatus::Closed;
    CHECK(sMenu->mPage == NP_SIGNAL_STATUS);
    CHECK(!sMenu->mSignalAccept->IsEnabled() && !sMenu->mSignalProceed->IsEnabled() && !sMenu->mSignalRetry->IsEnabled());
    CHECK(Contacted() == 0 && hostAutomatic == 0 && hostSelected == 0);
    return 0;
  }
  if (!host) {
    CHECK(sMenu->mPage == NP_JOIN && LT.trap && std::string(sMenu->mNotice).find("Join invitation selected") != std::string::npos);
    MenuFrame();
    CHECK(!sMenu->mAddress->IsEnabled() && !sMenu->mPasteAddress->IsEnabled());   // the relay uses no address
    if (name == "ice-guest-awaiting-code") {
      MenuFrame();
      CHECK(Drawn(L"Choose Connect") && !Drawn(L"Enter the approved host"));
      CHECK(sMenu->mCode->mValue[0] == 0 && joinCalls == 0 && Contacted() == 0);
      CHECK(SelectAction(NA_CONNECT));
      CHECK(sMenu->mPage == NP_SIGNAL_DISCLOSURE && joinCalls == 0 && Contacted() == 0);
      CHECK(SelectAction(NA_SIGNAL_DISCLOSE));
      CHECK(sMenu->mPage == NP_SIGNAL_STATUS && preflightCalls == 1 && joinCalls == 0);
      signalView.status = CeIceSignalStatus::NeedsConsent;
      MenuFrame();
      CHECK(SelectAction(NA_SIGNAL_ACCEPT) && acceptCalls == 1 && joinCalls == 0);
      signalView.status = CeIceSignalStatus::Admitted;
      MenuFrame();
      CHECK(SelectAction(NA_SIGNAL_PROCEED));
      MenuFrame();
      CHECK(Drawn(L"Invitation accepted") && Drawn(L"Wait for the host"));
      CHECK(sMenu->mPage == NP_JOIN && sMenu->mCode->mValue[0] == 0 && joinCalls == 0);
      CHECK(SelectAction(NA_CONNECT) && sMenu->mPage == NP_JOIN && joinCalls == 0);
      sMenu->mList[sMenu->mPage]->mSelected = ActionIndex(NA_CODE);
      CHECK(FocusAction() == NA_CODE);
      Type("X");
      NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
      CHECK(sMenu->mPage == NP_JOIN && joinCalls == 0);
      std::memset(sMenu->mCode->mValue, 0, sizeof(sMenu->mCode->mValue));
      sMenu->mList[sMenu->mPage]->mSelected = ActionIndex(NA_CODE);
      CHECK(FocusAction() == NA_CODE);
      Type("ABC");
      NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
      CHECK(joinCalls == 1);
      return 0;
    }
    Type("ABC");
    NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
    CHECK(sMenu->mPage == NP_SIGNAL_DISCLOSURE && joinCalls == 0 && Contacted() == 0);
    return 0;
  }
  // Use invitation leads to Host, and Host to the disclosure: no service contact yet.
  CHECK(sMenu->mPage == NP_SIGNAL_DISCLOSURE);
  CHECK(Contacted() == 0 && hostAutomatic == 0 && hostSelected == 0);
  CHECK(sMenu->mSignalContinue->IsEnabled());
  if (name == "ice-disclosure-cancel") {
    BackButton();
    CHECK(sMenu->mPage == NP_MENU && leaveCalls == 1 && std::string(sMenu->mNotice).find("closed") != std::string::npos);
    CHECK(Contacted() == 0 && hostAutomatic == 0 && hostSelected == 0 && !sMenu->mSignalApproved);
    return 0;
  }
  CHECK(SelectAction(NA_SIGNAL_DISCLOSE));
  CHECK(sMenu->mPage == NP_SIGNAL_STATUS && preflightCalls == 1 && sMenu->mSignalApproved && sMenu->mSignalBegan);
  signalView.status = CeIceSignalStatus::NeedsConsent;
  MenuFrame();
  CHECK(pumpCalls >= 1 && sMenu->mSignalAccept->IsEnabled() && !sMenu->mSignalProceed->IsEnabled());
  CHECK(SelectAction(NA_SIGNAL_PROCEED) && hostAutomatic == 0);   // no connection before consent and admission
  CHECK(SelectAction(NA_SIGNAL_ACCEPT) && acceptCalls == 1);
  signalView.status = CeIceSignalStatus::Admitted;
  MenuFrame();
  CHECK(sMenu->mSignalProceed->IsEnabled() && !sMenu->mSignalAccept->IsEnabled());
  CHECK(sMenu->mList[NP_SIGNAL_STATUS]->mSelected == ActionIndex(NA_SIGNAL_PROCEED));
  CHECK(SelectAction(NA_SIGNAL_PROCEED));
  CHECK(sMenu->mPage == NP_HOST && hostAutomatic == 1 && hostSelected == 0 && preflightCalls == 1);
  return 0;
}
#endif

static int Run(const std::string& name) {
  if (name == "offered-guard") {
    menusOff = true; PortCeNetOpen(); CHECK(!PortCeNetActive() && !sMenu); return 0;
  }
  if (name == "entry-direct" || name == "relay-unavailable" || name == "help-back" || name == "cheats-refused") return RunEntry(name);
#if defined(CE_NETWORK_ICE)
  if (name.rfind("ice-", 0) == 0) return RunIce(name);
#endif
  if (name == "single-address") addresses = {"192.0.2.10"};
  if (name == "no-address" || name == "address-appeared") addresses.clear();
  if (name == "join-input" || name == "join-back") {
    OpenJoin(); CHECK(sMenu->mPage == NP_JOIN && LT.trap && FocusAction() == NA_CODE);
    if (name == "join-back") { BackButton(); CHECK(sMenu->mPage == NP_MENU && !LT.trap && FocusAction() == NA_JOIN); CHECK(leaveCalls == 1); return 0; }
    NetKeyTrap('A', KEY_EVENT_CHAR); NetKeyTrap(DIK_DOWN, KEY_EVENT_DOWN);
    for (const char c : std::string("192.0.2.55")) NetKeyTrap(WORD(c), KEY_EVENT_CHAR);
    NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
    CHECK(joinCalls == 1 && joinedCode == "A" && joinedAddress == "192.0.2.55"); CHECK(hostSelected == 0 && hostAutomatic == 0); return 0;
  }
  if (name == "wrap-no-orphan") {
    // A paragraph whose last line would be one word is wrapped a little narrower, keeping its number of lines.
    static SWordWrapFill fill;
    static WCHAR text[] = L"Look for Port Forwarding in the router's settings.";
    CHECK(PLATFORM.Font(FONT_SMALL)->WordWrap(&fill, text, 392.f) == 2 && std::wstring(fill.c[1]) == L"settings.");
    CHECK(Wrap(PLATFORM.Font(FONT_SMALL), &fill, text, 392.f) == 2 && std::wcschr(fill.c[1], L' ') != nullptr);
    // When only a third line would help, the wrap stays as it was.
    static WCHAR two[] = L"aaaa bbbb";
    CHECK(Wrap(PLATFORM.Font(FONT_SMALL), &fill, two, 48.f) == 2 && std::wstring(fill.c[1]) == L"bbbb");
    return 0;
  }
  if (name == "connect-incomplete-once") {
    // Connect with no code, then with half a code: the line under the introduction says what to do, once; nothing is
    // joined, no notice repeats it, and no sound is played for the error.
    OpenJoin(); CHECK(sMenu->mPage == NP_JOIN && FocusAction() == NA_CODE);
    const size_t before = sounds.size();
    NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
    DWORD colour = 0;
    CHECK(joinCalls == 0 && sMenu->mNotice[0] == 0 && sounds.size() == before);
    CHECK(std::string(sMenu->CodeLine(&colour)) == "Type your friend's code first.");
    Type("A");
    CHECK(std::string(sMenu->CodeLine(&colour)) == "The code is complete.");   // the fixture's codec takes any text
    return 0;
  }
  if (name == "notice-box-join") {
    // A failed join: the lobby's whole notice is in the message box, the page stays the Join page, and the key trap is
    // off while the box is up (so Enter reaches the box) and back on after it.
    OpenJoin(); Type("ABC"); NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
    CHECK(joinCalls == 1 && status.state == CE_LOBBY_JOIN_CONNECTING);
    status.state = CE_LOBBY_FAILED;
    std::snprintf(status.notice, sizeof status.notice, "%s", "No game accepted this code. Check it with your friend, then choose Connect again.");
    MenuFrame();
    CHECK(BoxSays("No game accepted this code. Check it with your friend, then choose Connect again."));
    CHECK(sMenu->mPage == NP_JOIN && sMenu->mNoticeBoxed && !LT.trap);
    Dismiss(); MenuFrame();
    CHECK(LT.trap && boxes.size() == 1);
    return 0;
  }
  if (name == "refused-box-version") {
    // A refused join says both versions in the player's words, or that the downloads differ when both say the same label.
    OpenJoin(); Type("ABC"); NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
    CHECK(joinCalls == 1);
    status.state = CE_LOBBY_REFUSED;
    std::snprintf(status.local_version, sizeof status.local_version, "%s", "0.2.0-coop");
    std::snprintf(status.peer_version, sizeof status.peer_version, "%s", "0.2.0-other");
    MenuFrame();
    CHECK(BoxSays("You have Onslaught CE 0.2.0-coop and your friend has 0.2.0-other. You both need the same version. Update, then join again."));
    Dismiss(); sMenu->Show(NP_JOIN); status.state = CE_LOBBY_JOIN_CONNECTING; MenuFrame();
    status.state = CE_LOBBY_REFUSED;
    std::snprintf(status.peer_version, sizeof status.peer_version, "%s", "0.2.0-coop");
    MenuFrame();
    CHECK(BoxSays("You and your friend have different Onslaught CE downloads. You both need the same one. Update, then join again."));
    return 0;
  }
  if (name == "notice-line-host") {
    // The host's page stays up while it waits: what the lobby says is a line under the code, never a box over it (review
    // R2 finding 1), and the page makes room for it rather than drop it or run into the rows.
    OpenHost(); CHECK(sMenu->mPage == NP_HOST_ADDRESS && boxes.empty());
    std::snprintf(status.code, sizeof status.code, "%s", "BM4J-5QR9-8GEN-R2M0-1E3C-0GY");
    std::snprintf(status.notice, sizeof status.notice, "%s", "Your friend could not join. Check that they have this code.");
    drawnText.clear(); MenuFrame();
    CHECK(boxes.empty() && Drawn(L"Your friend could not join.") && Drawn(L"BM4J-5QR9-8GEN-R2M0-1E3C-0GY"));
    CHECK(sMenu->mHostLineBottom > 0.f && sMenu->mHostLineBottom <= ListTop(sMenu->mList[sMenu->mPage]) - 6.f);
    // A long notice and the page's own note as well: the block moves up, and nothing reaches the rows.
    std::snprintf(status.notice, sizeof status.notice, "%s", "Your friend has Onslaught CE 0.2.0-rc0+a-build-with-a-long-name-that-a-tester-gave-it. "
                  "You both need the same version. Ask your friend which version they have, then both use that one.");
    sMenu->Notice("The code is copied.");
    drawnText.clear(); MenuFrame();
    CHECK(boxes.empty() && Drawn(L"The code is copied.") && sMenu->mHostLineBottom <= ListTop(sMenu->mList[sMenu->mPage]) - 6.f);
    return 0;
  }
  if (name == "code-field-fits") {
    // The Code row takes the code's 23 letters and digits and no more; every value row ends by the arc, however long its
    // value (review R2 finding 2, as a class).
    OpenJoin(); CHECK(sMenu->mPage == NP_JOIN && FocusAction() == NA_CODE);
    Type("ABCDEFGHJKLMNPQRSTVWXYZ0123456789ABCDEFG");
    int symbols = 0;
    for (const char* c = sMenu->mCode->mValue; *c; ++c) symbols += *c != '-';
    CHECK(symbols == CE_CODE_SYMBOLS);
    sMenu->mCode->Render(320.f, 250.f, FALSE);		// where the Join page's list draws it (x 320)
    CHECK(sMenu->mCode->mValueEnd > 322.f && sMenu->mCode->mValueEnd <= kNetValueRight);
    CNetValue row("Mission", NA_MAP), centred("", NA_NONE);
    row.SetValue("A value much longer than the band between the arcs could ever hold at the squeeze the rows allow");
    centred.SetValue("https://a-very-long-origin-name-that-no-row-could-hold-between-the-arcs.example.test:65535");
    row.Render(320.f, 200.f, FALSE); centred.Render(320.f, 220.f, FALSE);
    CHECK(row.mValueEnd <= kNetValueRight && centred.mValueEnd <= kNetValueRight);
    return 0;
  }
  if (name == "address-hint") {
    // An address that can never be one says so in the hint's place while the Address row has the focus; one still being
    // typed does not (review R2 polish 3).
    OpenJoin(); sMenu->mList[NP_JOIN]->mSelected = ActionIndex(NA_ADDRESS);
    Type("192.168."); drawnText.clear(); MenuFrame();
    CHECK(!Drawn(L"That is not an address.") && Drawn(L"Only for play over the internet."));
    Type("999"); drawnText.clear(); MenuFrame();
    CHECK(Drawn(L"That is not an address."));
    sMenu->mAddress->Clear(); Type("10.0.0.1:70000"); drawnText.clear(); MenuFrame();
    CHECK(Drawn(L"That is not an address."));
    sMenu->mAddress->Clear(); Type("10.0.0.1:27652"); drawnText.clear(); MenuFrame();
    CHECK(!Drawn(L"That is not an address."));
    return 0;
  }
  if (name == "box-click-through") {
    // A click on the box's OK closes the box and nothing else: the row under it does not take the click (review R2 note 4).
    OpenJoin(); Type("ABC"); NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
    status.state = CE_LOBBY_FAILED;
    std::snprintf(status.notice, sizeof status.notice, "%s", "No game accepted this code. Check it with your friend, then choose Connect again.");
    MenuFrame(); MenuFrame();
    CHECK(FEMESSBOX.BeingDisplayed() && boxes.size() == 1);
    CMenuOptionsList* list = sMenu->mList[NP_JOIN];
    const float top = list->mY - 16.f * list->mItems.Size() * 0.5f - 15.f;
    MOUSE_Y = int(top + 8.f);			// the Code row, under the box's OK
    Dismiss(); mouseReleased = true;
    const size_t before = sounds.size();
    MenuFrame();
    CHECK(!mouseReleased && sounds.size() == before);
    // The next click, a moment later, is the page's again.
    MenuFrame(); MenuFrame(); mouseReleased = true; MenuFrame();
    CHECK(!mouseReleased && sounds.size() == before + 1);
    return 0;
  }

  if (name == "rematch-host-result" || name == "rematch-guest-result" ||
      name == "rematch-guest-cancel" || name == "rematch-host-lost" || name == "rematch-guest-failed")
  {
    const bool host = name == "rematch-host-result" || name == "rematch-host-lost";
    rematchRole = host ? 1 : 2;
    outcomeText = "The network game has ended.";
    status.host = host;
    status.state = host ? CE_LOBBY_HOST_WAITING : CE_LOBBY_JOIN_CONNECTING;
    PortCeNetOpen();
    CHECK(sMenu && sMenu->mPage == (host ? NP_HOST : NP_JOIN) && boxes.empty());
    drawnText.clear(); MenuFrame();
    CHECK(Drawn(host ? L"Waiting for your friend to return." : L"Rejoining your friend..."));
    CHECK(!Drawn(L"Back in the lobby together.") && !Drawn(L"Your friend starts the next mission.") && !Drawn(L"Your Join Code") && !Drawn(L"Type your friend's code"));
    if (name == "rematch-guest-cancel")
    {
      BackButton(); drawnText.clear(); MenuFrame();
      CHECK(!Drawn(L"Rejoining your friend...") && !Drawn(L"Back in the lobby together.") && !Drawn(L"Your friend starts the next mission."));
      CHECK(Drawn(L"Type your friend's code"));
      return 0;
    }
    if (name == "rematch-guest-failed")
    {
      status.state = CE_LOBBY_FAILED;
      std::snprintf(status.notice, sizeof(status.notice), "%s", "The host left the game.");
      sMenu->Update(); drawnText.clear(); MenuFrame();
      CHECK(BoxSays("The host left the game."));
      CHECK(!Drawn(L"Back in the lobby together.") && !Drawn(L"Your friend starts the next mission.") && !Drawn(L"Rejoining your friend..."));
      return 0;
    }
    status.state = CE_LOBBY_READY;
    sMenu->Update(); drawnText.clear(); MenuFrame();
    CHECK(sMenu->mPage == NP_LOBBY && boxes.empty() && Drawn(host ? L"Back in the lobby together." : L"Your friend starts the next mission."));
    CHECK(!Drawn(host ? L"Your friend starts the next mission." : L"Back in the lobby together."));
    CHECK(!Drawn(L"The network game has ended.") && !Drawn(L"Rejoining your friend...") && !Drawn(L"Waiting for your friend to return."));
    if (name == "rematch-host-lost")
    {
      status.state = CE_LOBBY_HOST_WAITING;
      std::snprintf(status.notice, sizeof(status.notice), "%s", "Your friend left. The same code still works.");
      sMenu->Update(); drawnText.clear(); MenuFrame();
      CHECK(!Drawn(L"Back in the lobby together.") && !Drawn(L"Your friend starts the next mission.") && !Drawn(L"Waiting for your friend to return."));
      CHECK(Drawn(L"Your Join Code"));
      return 0;
    }
    return 0;
  }
  if (name == "notice-box-outcome") {
    // Back from a network game: the first page opens with the outcome in the box.
    outcomeText = "The network game has ended.";
    PortCeNetOpen();
    CHECK(sMenu->mPage == NP_MENU && BoxSays("The network game has ended."));
    return 0;
  }
  if (name == "mouse-join") {
    // The Join page traps the keyboard, which the game also asks about every mouse button: the trap must answer false.
    OpenJoin(); CHECK(LT.trap && FocusAction() == NA_CODE);
    CHECK(!NetKeyTrap(0, KeyEventType(4)) && !NetKeyTrap(0, KeyEventType(3)) && sMenu->mPage == NP_JOIN && FocusAction() == NA_CODE);
    // The pointer moves onto Back (the last row; rows are 16 high in this fixture, centred on the list's y): focus follows.
    CMenuOptionsList* list = sMenu->mList[NP_JOIN];
    const float top = list->mY - 16.f * list->mItems.Size() * 0.5f - 15.f;
    MOUSE_Y = int(top + 16.f * (list->mItems.Size() - 1) + 8.f);
    MenuFrame();
    CHECK(FocusAction() == NA_BACK);
    // A click on the Code row selects it for typing (it does not choose it, which would move the focus on).
    MOUSE_Y = int(top + 8.f); mouseReleased = true;
    MenuFrame();
    CHECK(FocusAction() == NA_CODE && !mouseReleased && joinCalls == 0);
    return 0;
  }
  if (name == "leave-during-starting") {
    // The host's Start is agreed (STARTING): a Back or Leave in that same moment leaves nothing, and the page still closes
    // to start the game, as the other game is already doing.
    OpenHost(); status.state = CE_LOBBY_READY; MenuFrame();
    CHECK(sMenu->mPage == NP_LOBBY && slotsDrawn == 2);
    CHECK(SelectAction(NA_START) && status.state == CE_LOBBY_STARTING);
    BackButton();
    CHECK(leaveCalls == 0 && status.state == CE_LOBBY_STARTING);
    MenuFrame(); testTime += 2.f; MenuFrame(); MenuFrame();
    CHECK(postClose == 1 && sMenu->mQuitPosted);
    return 0;
  }
  if (name == "lobby-back-host" || name == "lobby-back-guest") {
    // Leave from the lobby returns the focus to the row that led there: Host Game for the host, Join Game for the guest.
    const bool host = name == "lobby-back-host";
    if (host) OpenHost(); else { OpenJoin(); Type("ABC"); NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN); }
    status.state = CE_LOBBY_READY; MenuFrame();
    CHECK(sMenu->mPage == NP_LOBBY);
    BackButton();
    CHECK(sMenu->mPage == NP_MENU && leaveCalls == 1 && FocusAction() == (host ? NA_HOST : NA_JOIN));
    return 0;
  }
  if (name.rfind("campaign-", 0) == 0) return RunCampaign(name);
  // Host Game: one press hosts on the first address; the host's page has the address row only when there are several.
  OpenHost();
  CHECK(sMenu->mHostAddressCount == int(addresses.size()));
  if (name == "no-address") { CHECK(sMenu->mPage == NP_HOST && hostAutomatic == 1 && hostSelected == 0); return 0; }
  CHECK(hostSelected == 1 && hostAutomatic == 0 && selectedAddress == "192.0.2.10");
  if (name == "single-address") { CHECK(sMenu->mPage == NP_HOST && ActionIndex(NA_HOST_ADDRESS) < 0 && FocusAction() == NA_COPY); return 0; }
  CHECK(sMenu->mPage == NP_HOST_ADDRESS && ActionIndex(NA_HOST_ADDRESS) == 0 && FocusAction() == NA_COPY);
  if (name == "multi-address") {
    // The address row: Right hosts again on the next address (a new code), and the focus stays on the row.
    sMenu->mList[NP_HOST_ADDRESS]->mSelected = 0;
    Right();
    CHECK(leaveCalls == 1 && hostSelected == 2 && selectedAddress == "192.0.2.11" && fakeConnection);
    CHECK(sMenu->mPage == NP_HOST_ADDRESS && FocusAction() == NA_HOST_ADDRESS && sMenu->mHostAddressIndex == 1);
    Select();
    CHECK(leaveCalls == 2 && hostSelected == 3 && selectedAddress == "192.0.2.10" && sMenu->mHostAddressIndex == 0);
    return 0;
  }
  if (name == "stale-address") {
    // The second address went away before the player changed to it: the lobby refuses it, the page says so.
    addresses = {"192.0.2.10"};
    sMenu->mList[NP_HOST_ADDRESS]->mSelected = 0;
    Right();
    CHECK(hostSelected == 2 && selectedAddress == "192.0.2.11" && !fakeConnection);
    MenuFrame();
    CHECK(sMenu->mPage == NP_MENU && sMenu->mNotice[0] && sMenu->mHostAddressCount == 1);
    return 0;
  }
  if (name == "waiting-back") { BackButton(); CHECK(sMenu->mPage == NP_MENU && leaveCalls == 1 && !fakeConnection && FocusAction() == NA_HOST); return 0; }
  if (name == "start-not-ready") { PortCeNetProcess(); sMenu->Choose(sMenu->mStart); CHECK(startCalls == 0 && postClose == 0); return 0; }
  if (name == "ready-start") {
    status.state = CE_LOBBY_READY; PortCeNetProcess(); CHECK(sMenu->mPage == NP_LOBBY);
    Down(); Select(); CHECK(startCalls == 1 && status.state == CE_LOBBY_STARTING && campaignCalls == 0);
    PortCeNetProcess(); testTime += 2.f; PortCeNetProcess(); PortCeNetProcess();
    CHECK(postClose == 1 && sMenu->mQuitPosted); return 0;
  }
  return 2;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  int result = Run(argv[1]);
  PortCeNetShutdown();
  std::printf("MENU_RESULT {\"checks\":%u,\"failures\":%u,\"skipped\":0}\n", checks, failures);
  return result;
}
