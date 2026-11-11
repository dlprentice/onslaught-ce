// SPDX-License-Identifier: GPL-3.0-only
// The menu source is compiled unchanged, with in-process platform and lobby providers.
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
#include "ce_netlobby.h"
#include "ce_netcode.h"
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
enum { MEMTYPE_MENUITEM, PORT_CE_NO_LISTS, FES_BACK, FES_SELECT, FEP_LANGUAGE_TEST,
       BUTTON_FRONTEND_MENU_UP, BUTTON_FRONTEND_MENU_DOWN, BUTTON_PAUSE_MENU_SELECT,
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
  int WordWrap(SWordWrapFill* fill, const WCHAR* text, float) {
    std::wcsncpy(fill->c[0], text, 1023); fill->c[0][1023] = 0; return text[0] ? 1 : 0;
  }
  void GetTextExtent(const WCHAR* text, SIZE* size) { size->cx = int(std::wcslen(text))*8; size->cy = 16; }
  void DrawTextWithShadow(float, float, DWORD, const WCHAR*, int, int, float, float) {}
};
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
struct CFrontEnd { static void PlaySound(int) {} };
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
static CE_LobbyStatus status{};
static std::vector<std::string> addresses = {"192.0.2.10", "192.0.2.11"};
static unsigned hostAutomatic, hostSelected, leaveCalls, startCalls, joinCalls;
static bool fakeConnection;
static unsigned campaignCalls, campaignAccepted;
static uint32_t campaignRequested;
static bool campaignAvailable = true, campaignFailure;
static CE_LobbyCampaignStartResult campaignResult = CE_LOBBY_CAMPAIGN_START_QUEUED;

static std::string selectedAddress, joinedCode, joinedAddress;
extern "C" {
int CE_LobbyAvailable(char* why, size_t size) { if (why && size) why[0] = 0; return 1; }
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
int CE_LobbyTakeOutcome(char*, size_t, int*) { return 0; }
int CE_LobbyOutcomePending() { return 0; }
void CE_LobbyShellExit(int, int) {}
void CE_LobbyAfterExit() {}
int CE_CodeDecode(const char* text, CE_JoinCode* code) { std::memset(code, 0, sizeof(*code)); return text && text[0] ? CE_CODE_OK : CE_CODE_EMPTY; }
const char* CE_CodeProblem(int) { return "fixture input required"; }
void CE_CodeWipe(void* p, size_t n) { std::memset(p, 0, n); }
}
#include "menu.inc"
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::printf("MENU_ASSERTION %u\n", checks); return 1; } } while (0)
static void Button(int button) { testTime += 1.f; PortCeNetButton(button, 1.f); }
static void Select() { Button(BUTTON_PAUSE_MENU_SELECT); }
static void Down() { Button(BUTTON_FRONTEND_MENU_DOWN); }
static void BackButton() { Button(BUTTON_FRONTEND_MENU_BACK); }
static void OpenHost() { PortCeNetOpen(); Select(); }
static void ConfirmHost() { if (sMenu->mList[NP_HOST_CHOOSE]->mSelected == 0) Down(); Select(); }
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
    PortCeNetProcess();
    if (render) PortCeNetDraw();
}
static int RunCampaign(const std::string &name) {
    OpenHost();
    ConfirmHost();
    status.level = 10001;
    status.state = CE_LOBBY_READY;
    status.host = true;
    if (name == "campaign-guest-disabled") status.host = false;
    if (name == "campaign-not-ready-disabled") status.state = CE_LOBBY_HOST_WAITING;
    MenuFrame(false);
    if (name == "campaign-not-ready-disabled") {
        CHECK(sMenu->mPage == NP_HOST);
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
    renderedItems.clear();
    PortCeNetDraw();
    const uint32_t displayed = status.level;
    CHECK(sMenu->mCampaignMissionKnown);
    CHECK(std::find(renderedItems.begin(), renderedItems.end(), sMenu->mMap) != renderedItems.end());
    if (name == "campaign-guest-disabled") {
        CHECK(!sMenu->mCampaignStart->mEnabled);
        CHECK(static_cast<CNetItem *>(sMenu->mList[sMenu->mPage]->mItems.At(sMenu->mList[sMenu->mPage]->mSelected))->mAction == NA_LEAVE);
        CHECK(SelectAction(NA_CAMPAIGN_START));
        CHECK(campaignCalls == 0 && startCalls == 0 && postClose == 0);
        return 0;
    }
    if (name == "campaign-late-back") {
        BackButton();
        CHECK(sMenu->mPage == NP_MENU && leaveCalls == 1);
        sMenu->Choose(sMenu->mCampaignStart);
        CHECK(campaignCalls == 0 && startCalls == 0 && postClose == 0);
        return 0;
    }
    if (name == "campaign-unrendered-reentry") {
        BackButton();
        OpenHost();
        ConfirmHost();
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
static int Run(const std::string& name) {
  if (name == "offered-guard") {
    menusOff = true; PortCeNetOpen(); CHECK(!PortCeNetActive() && !sMenu); return 0;
  }
  if (name == "single-address") addresses = {"192.0.2.10"};
  if (name == "no-address" || name == "address-appeared") addresses.clear();
  if (name == "join-input" || name == "join-back") {
    PortCeNetOpen(); Down(); Select(); CHECK(sMenu->mPage == NP_JOIN && LT.trap);
    if (name == "join-back") { BackButton(); CHECK(sMenu->mPage == NP_MENU && !LT.trap); CHECK(leaveCalls == 1); return 0; }
    NetKeyTrap('A', KEY_EVENT_CHAR); NetKeyTrap(DIK_DOWN, KEY_EVENT_DOWN);
    for (const char c : std::string("192.0.2.55")) NetKeyTrap(WORD(c), KEY_EVENT_CHAR);
    NetKeyTrap(DIK_RETURN, KEY_EVENT_DOWN);
    CHECK(joinCalls == 1 && joinedCode == "A" && joinedAddress == "192.0.2.55"); CHECK(hostSelected == 0 && hostAutomatic == 0); return 0;
  }
  if (name.rfind("campaign-", 0) == 0) return RunCampaign(name);
  OpenHost();
  CHECK(sMenu->mPage == NP_HOST_CHOOSE && hostSelected == 0 && hostAutomatic == 0);
  CHECK(sMenu->mHostAddressCount == int(addresses.size()));
  if (name == "chooser-back") {
    Select(); CHECK(sMenu->mHostAddressIndex == 1); BackButton();
    CHECK(sMenu->mPage == NP_MENU && leaveCalls == 0 && hostSelected == 0 && hostAutomatic == 0);
    sMenu->Choose(sMenu->mHostBegin); CHECK(hostSelected == 0 && hostAutomatic == 0);
    CHECK(sMenu->mHostAddressCount == 0); Select(); CHECK(sMenu->mHostAddressIndex == 0); return 0;
  }
  if (name == "address-appeared") {
    addresses = {"192.0.2.12"}; ConfirmHost();
    CHECK(sMenu->mPage == NP_HOST_CHOOSE && hostSelected == 0 && hostAutomatic == 0);
    CHECK(sMenu->mHostAddressCount == 1 && sMenu->mNotice[0]);
    ConfirmHost(); CHECK(sMenu->mPage == NP_HOST && hostSelected == 1 && selectedAddress == "192.0.2.12"); return 0;
  }
  if (name == "stale-choice" || name == "stale-to-none") {
    Select(); CHECK(sMenu->mHostAddressIndex == 1);
    addresses = name == "stale-to-none" ? std::vector<std::string>{} : std::vector<std::string>{"192.0.2.10", "192.0.2.12"};
    fakeConnection = true; ConfirmHost();
    CHECK(hostSelected == 1 && selectedAddress == "192.0.2.11" && hostAutomatic == 0);
    CHECK(sMenu->mPage == NP_HOST_CHOOSE && fakeConnection && leaveCalls == 0 && sMenu->mNotice[0]);
    CHECK(sMenu->mHostAddressCount == int(addresses.size()));
    ConfirmHost(); CHECK(sMenu->mPage == NP_HOST);
    CHECK(name == "stale-to-none" ? hostAutomatic == 1 : hostSelected == 2 && selectedAddress == "192.0.2.10"); return 0;
  }
  if (name == "multi-address") { Select(); CHECK(sMenu->mHostAddressIndex == 1); }
  ConfirmHost(); CHECK(sMenu->mPage == NP_HOST);
  if (name == "no-address") { CHECK(hostAutomatic == 1 && hostSelected == 0); return 0; }
  CHECK(hostSelected == 1 && hostAutomatic == 0 && selectedAddress == (name == "multi-address" ? "192.0.2.11" : "192.0.2.10"));
  if (name == "waiting-back") { BackButton(); CHECK(sMenu->mPage == NP_MENU && leaveCalls == 1 && !fakeConnection); return 0; }
  if (name == "start-not-ready") { PortCeNetProcess(); sMenu->Choose(sMenu->mStart); CHECK(startCalls == 0 && postClose == 0); return 0; }
  if (name == "ready-start") {
    status.state = CE_LOBBY_READY; PortCeNetProcess(); CHECK(sMenu->mPage == NP_LOBBY);
    Down(); Select(); CHECK(startCalls == 1 && status.state == CE_LOBBY_STARTING && campaignCalls == 0);
    PortCeNetProcess(); testTime += 2.f; PortCeNetProcess(); PortCeNetProcess();
    CHECK(postClose == 1 && sMenu->mQuitPosted); return 0;
  }
  CHECK(name == "multi-address" || name == "single-address"); return 0;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  int result = Run(argv[1]);
  PortCeNetShutdown();
  std::printf("MENU_RESULT {\"checks\":%u,\"failures\":%u,\"skipped\":0}\n", checks, failures);
  return result;
}
