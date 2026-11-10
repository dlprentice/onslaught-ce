// SPDX-License-Identifier: GPL-3.0-only
#include "player_connection_entry_stubs.h"
#include "ce_netcode.h"
#include "ce_netlobby.h"

#include <algorithm>
#include "ice_signaling.h"
#include "ce_network_invitation.h"

namespace {
int checks = 0, failures = 0;
int fixture_host_calls = 0, fixture_join_calls = 0, fixture_leave_calls = 0;
int fixture_clipboard_reads = 0;
bool fixture_available = true;
bool fixture_join_ok = true;
bool fixture_clear_notice_on_leave = false;
std::string fixture_clipboard, fixture_join_code, fixture_join_address;
CE_LobbyStatus fixture_status = {};



void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL %s\n", name); }
}
std::string FixtureCode(unsigned key) {
    CE_JoinCode code = {key, 0x7f000001u, 27652};
    char text[CE_CODE_TEXT] = {};
    if (!CE_CodeEncode(&code, text, sizeof text)) std::abort();
    return text;
}
} // namespace

#if defined(CE_NETWORK_ICE)
bool fixture_signal_selected = false, fixture_signal_begin_ok = true;
bool fixture_signal_accept_ok = true, fixture_signal_retry_ok = true;
int fixture_signal_begins = 0, fixture_signal_pumps = 0, fixture_signal_accepts = 0;
int fixture_signal_retries = 0, fixture_signal_closes = 0;
std::string fixture_signal_origin = "https://signal.invalid";
CeIceSignalStatus fixture_signal_status = CeIceSignalStatus::Unavailable;
uint64_t fixture_signal_pair_expiry = 0, fixture_signal_setup_expiry = 0;
CE_InvitationView fixture_invitation{};
int fixture_invitation_begins = 0, fixture_invitation_installs = 0, fixture_invitation_cancels = 0;
bool fixture_invitation_install_ok = true;
std::string fixture_invitation_requested_origin;
uint32_t fixture_invitation_expected_role = CE_NET_HOST;
extern "C" int CE_NetSignalingDirectorySelected(void) { return fixture_signal_selected; }
bool CE_LobbySignalingSelected() noexcept { return fixture_signal_selected; }
const char* CE_LobbySignalingOrigin() noexcept
{ return fixture_signal_selected ? fixture_signal_origin.c_str() : ""; }
bool CE_LobbySignalingBeginPreflight() noexcept
{
    ++fixture_signal_begins;
    if (!fixture_signal_selected || !fixture_signal_begin_ok) return false;
    fixture_signal_status = CeIceSignalStatus::NeedsConsent; return true;
}
void CE_LobbySignalingPump() noexcept { ++fixture_signal_pumps; }
CeIceSignalView CE_LobbySignalingView() noexcept
{
    CeIceSignalView view;
    if (fixture_signal_selected) {
        view.status = fixture_signal_status;
        view.pairExpiresAt = fixture_signal_pair_expiry;
        view.setupExpiresAt = fixture_signal_setup_expiry;
    }
    return view;
}
bool CE_LobbySignalingAccept() noexcept
{
    ++fixture_signal_accepts;
    if (!fixture_signal_accept_ok) return false;
    fixture_signal_status = CeIceSignalStatus::AwaitingPairConsent; return true;
}
bool CE_LobbySignalingRetry() noexcept
{ ++fixture_signal_retries; return fixture_signal_retry_ok; }
void CE_LobbySignalingClear() noexcept
{ CE_LobbyLeave(); fixture_signal_selected = false; }
extern "C" int CE_InvitationBeginImport(const char* origin, uint32_t role)
{
    ++fixture_invitation_begins; fixture_invitation_expected_role = role;
    fixture_invitation_requested_origin = origin;
    memset(&fixture_invitation, 0, sizeof fixture_invitation);
    fixture_invitation.state = CE_INVITATION_CHOOSING;
    fixture_invitation.role = role; fixture_invitation.expires_at = CE_HandoffNow() + 600;
    snprintf(fixture_invitation.origin, sizeof fixture_invitation.origin, "%s", origin);
    return 1;
}
extern "C" void CE_InvitationPoll(uint64_t, CE_InvitationView* view) { *view = fixture_invitation; }
extern "C" int CE_InvitationInstall(uint64_t now)
{
    ++fixture_invitation_installs;
    if (fixture_signal_selected || !fixture_invitation_install_ok ||
        fixture_invitation.state != CE_INVITATION_PREVIEW_READY || fixture_invitation.expires_at <= now) {
        fixture_invitation.state = CE_INVITATION_REFUSED;
        fixture_invitation.error = CE_INVITATION_ERROR_SELECTION; return 0;
    }
    fixture_invitation.state = CE_INVITATION_SELECTED;
    fixture_signal_selected = true; fixture_signal_origin = fixture_invitation.origin;
    fixture_signal_status = CeIceSignalStatus::Unavailable;
    return 1;
}
extern "C" void CE_InvitationCancel(void)
{ ++fixture_invitation_cancels; fixture_invitation.state = CE_INVITATION_CANCELLED; }
extern "C" const char* CE_InvitationErrorText(uint32_t)
{ return "Authored invitation selection refused."; }

#endif
extern "C" {
int CE_NetGenerateSecret(unsigned char* out) { std::memset(out, 1, 32); return 1; }
int CE_LobbyAvailable(char* why, size_t size) {
    std::snprintf(why, size, "%s", fixture_available ? "" : "Fixture transport unavailable.");
    return fixture_available ? 1 : 0;
}
int CE_LobbyAddresses(char (*out)[16], int max) {
    if (max <= 0) return 0;
    std::snprintf(out[0], 16, "%s", "127.0.0.1"); return 1;
}
uint16_t CE_LobbyPort(void) { return 27652; }
int CE_LobbyHost(void) {
    ++fixture_host_calls; fixture_status.state = CE_LOBBY_HOST_WAITING;
    fixture_status.host = 1; return 1;
}
int CE_LobbyHostAt(const char*) { return CE_LobbyHost(); }
int CE_LobbyJoin(const char* code, const char* address, const char** problem) {
    ++fixture_join_calls; fixture_join_code = code; fixture_join_address = address;
    if (!fixture_join_ok) { *problem = "Fixture join refused."; return 0; }
    fixture_status.state = CE_LOBBY_JOIN_CONNECTING; fixture_status.host = 0;
    *problem = nullptr; return 1;
}
void CE_LobbyUpdate(void) {}
void CE_LobbyStatusGet(CE_LobbyStatus* out) { *out = fixture_status; }
int CE_LobbyNextLevel(void) { return 1; }
int CE_LobbyStart(void) { return 1; }
void CE_LobbyLeave(void) {
#if defined(CE_NETWORK_ICE)
    if (fixture_signal_selected) { ++fixture_signal_closes; fixture_signal_status = CeIceSignalStatus::Closed; }
#endif
 ++fixture_leave_calls; fixture_status.state = CE_LOBBY_OFF;  if (fixture_clear_notice_on_leave) fixture_status.notice[0] = 0; }
void CE_LobbyShutdown() { fixture_status.state = CE_LOBBY_OFF; }
int CE_LobbyRestartDue(void) { return 0; }
int CE_LobbyCopy(const char* text) { fixture_clipboard = text; return 1; }
int CE_LobbyPaste(char* out, size_t size) {
    ++fixture_clipboard_reads;
    std::snprintf(out, size, "%s", fixture_clipboard.c_str());
    return *out != 0;
}
int CE_LobbyCtrlHeld(void) { return 0; }
int CE_LobbyTakeOutcome(char*, size_t, int*) { return 0; }
int CE_LobbyOutcomePending(void) { return 0; }
CE_LobbyCampaignStartResult CE_LobbyCampaignStart(uint32_t) {
    return CE_LOBBY_CAMPAIGN_START_QUEUED;
}
}

bool PortCeMenus() { return true; }
#include CE_NETPAGES_SOURCE

namespace {
static void ChooseAction(EAction action)
{
    CNetItem item("authored action", action);
    sMenu->Choose(&item);
}

void OpenEntry(bool relay = false) {
    PortCeNetShutdown();
    fixture_status = {};
    fixture_status.level = CE_LOBBY_DEFAULT_LEVEL;
    fixture_available = true; fixture_join_ok = true;
    if (relay) setenv("BEA_NET_ICE_DIRECTORY", "authored-private-exchange", 1);
    else unsetenv("BEA_NET_ICE_DIRECTORY");
    fixture_host_calls = fixture_join_calls = fixture_leave_calls = 0;
    fixture_clipboard_reads = 0;
    fixture_clipboard.clear(); fixture_join_code.clear(); fixture_join_address.clear();
    PortCeNetOpen();
    sMenu->Update();
}

static void OpenJoin(bool relay=false)
{
    OpenEntry(relay);
    sMenu->Choose(relay ? sMenu->mOnlineItem : sMenu->mLocalItem);
    sMenu->Update();
    sMenu->Choose(sMenu->mJoinItem);
    sMenu->Update();
}

void ExplicitPaste() {
    OpenJoin();
    Check(sMenu && sMenu->mPage == NP_JOIN && fixture_clipboard_reads == 0,
          "opening-real-join-page-does-not-read-clipboard");
    Check(sMenu->mPasteCode->mAction == NA_PASTE_CODE &&
          sMenu->mPasteAddress->mAction == NA_PASTE_ADDRESS &&
          sMenu->mClearFields->mAction == NA_CLEAR_FIELDS,
          "real-menu-has-selectable-paste-and-clear-actions");
    const auto first = FixtureCode(1), second = FixtureCode(2);
    sMenu->mCode->Add(first.c_str());
    fixture_clipboard = second;
    std::transform(fixture_clipboard.begin(), fixture_clipboard.end(), fixture_clipboard.begin(),
                   [](unsigned char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : char(c); });
    fixture_clipboard = "\n" + fixture_clipboard + "\r\n";
    sMenu->Choose(sMenu->mPasteCode);
    Check(std::string(sMenu->mCode->mValue) == second && fixture_clipboard_reads == 1,
          "paste-replaces-old-code-and-uses-real-codec-canonical-form");
    Check(fixture_join_calls == 0 && sMenu->mList[NP_JOIN]->mSelected == 2,
          "successful-paste-still-requires-connect-confirmation");
    fixture_clipboard = " 127.0.0.1:34567\r\n";
    sMenu->Choose(sMenu->mPasteAddress);
    Check(std::string(sMenu->mAddress->mValue) == "127.0.0.1:34567" && fixture_join_calls == 0,
          "address-paste-preserves-complete-trimmed-endpoint-without-connecting");
    sMenu->Choose(sMenu->mConnect);
    Check(fixture_join_calls == 1 && fixture_join_code == second &&
          fixture_join_address == "127.0.0.1:34567" && !sMenu->mNotice[0],
          "explicit-connect-calls-existing-lobby-owner-with-entered-fields");
}

void FailedPastePreservesFields() {
    const auto valid = FixtureCode(3);
    for (const auto& bad : {std::string(), std::string("not a join code"),
                           std::string(150, 'A'), valid + "A"}) {
        OpenJoin();
        sMenu->mCode->Add(valid.c_str());
        fixture_clipboard = bad;
        sMenu->Choose(sMenu->mPasteCode);
        Check(std::string(sMenu->mCode->mValue) == valid && fixture_join_calls == 0 && sMenu->mNotice[0],
              "empty-malformed-or-truncated-code-paste-preserves-previous-code");
    }
    for (const auto& bad : {"prefix127.0.0.1", "127.0.0.1:+34567", "127.0. 0.1", "127.0.0.1:34567123456789"}) {
        OpenJoin();
        sMenu->mAddress->Add("127.0.0.1:12345");
        fixture_clipboard = bad;
        sMenu->Choose(sMenu->mPasteAddress);
        Check(std::string(sMenu->mAddress->mValue) == "127.0.0.1:12345" &&
              fixture_join_calls == 0 && sMenu->mNotice[0],
              "address-paste-rejects-whole-value-instead-of-filtering-or-truncating");
    }
}

void KeyboardAndConnecting() {
    OpenJoin();
    const auto first = FixtureCode(4), second = FixtureCode(5);
    sMenu->mCode->Add(first.c_str());
    sMenu->mList[NP_JOIN]->mSelected = 0;
    fixture_clipboard = second;
    NetKeyTrap(DIK_LCONTROL, KEY_EVENT_DOWN);
    NetKeyTrap(DIK_V, KEY_EVENT_DOWN);
    NetKeyTrap(DIK_LCONTROL, KEY_EVENT_UP);
    Check(std::string(sMenu->mCode->mValue) == second && fixture_join_calls == 0,
          "ctrl-v-uses-same-atomic-replacement-route");
    sMenu->Choose(sMenu->mConnect);
    const int immediate_reads = fixture_clipboard_reads;
    fixture_clipboard = first;
    sMenu->Choose(sMenu->mConnect);
    sMenu->Choose(sMenu->mPasteCode);
    sMenu->Choose(sMenu->mClearFields);
    sMenu->mList[NP_JOIN]->mSelected = 0;
    NetKeyTrap('Z', KEY_EVENT_CHAR);
    Check(fixture_join_calls == 1 && fixture_clipboard_reads == immediate_reads &&
          std::string(sMenu->mCode->mValue) == second,
          "pending-backend-blocks-same-frame-repeat-connect-and-input-before-update");
    sMenu->Update();
    Check(!sMenu->mCode->IsEnabled() && !sMenu->mAddress->IsEnabled() &&
          !sMenu->mPasteCode->IsEnabled() && !sMenu->mPasteAddress->IsEnabled() &&
          !sMenu->mClearFields->IsEnabled() && !sMenu->mConnect->IsEnabled(),
          "pending-real-join-disables-all-entry-mutations");
    const int reads = fixture_clipboard_reads;
    fixture_clipboard = first;
    sMenu->Choose(sMenu->mPasteCode); sMenu->Choose(sMenu->mClearFields);
    sMenu->mList[NP_JOIN]->mSelected = 0;
    NetKeyTrap('Z', KEY_EVENT_CHAR);
    NetKeyTrap(DIK_LCONTROL, KEY_EVENT_DOWN); NetKeyTrap(DIK_V, KEY_EVENT_DOWN);
    NetKeyTrap(DIK_LCONTROL, KEY_EVENT_UP);
    Check(fixture_clipboard_reads == reads && std::string(sMenu->mCode->mValue) == second &&
          fixture_join_calls == 1, "keyboard-and-menu-cannot-replace-pending-join-input");
    fixture_status.state = CE_LOBBY_FAILED;
    std::snprintf(fixture_status.notice, sizeof fixture_status.notice, "%s", "Fixture peer unavailable.");
    sMenu->Update();
    Check(sMenu->mCode->IsEnabled() && sMenu->mPasteCode->IsEnabled() &&
          std::string(sMenu->mCode->mValue) == second,
          "failed-attempt-reenables-entry-and-preserves-code-for-explicit-retry");
}

void ClearCloseAndAdmission() {
    OpenJoin();
    const auto code = FixtureCode(6);
    sMenu->mCode->Add(code.c_str()); sMenu->mAddress->Add("127.0.0.1");
    sMenu->Choose(sMenu->mClearFields);
    Check(!sMenu->mCode->mValue[0] && !sMenu->mAddress->mValue[0] &&
          sMenu->mList[NP_JOIN]->mSelected == 0 && fixture_join_calls == 0,
          "clear-fields-is-explicit-and-never-connects");
    sMenu->mCode->Add(code.c_str()); sMenu->mAddress->Add("127.0.0.1");
    sMenu->Close();
    Check(!sMenu->mCode->mValue[0] && !sMenu->mAddress->mValue[0] && !PortCeNetActive() && !LT.GetKeytrap(),
          "closing-menu-clears-entered-code-and-removes-key-trap");
    OpenJoin();
    sMenu->mCode->Add(code.c_str());
    sMenu->Choose(sMenu->mConnect);
    sMenu->Update();
    Check(sMenu->mPage == NP_JOIN, "connection-request-does-not-claim-lobby-admission");
    fixture_status.state = CE_LOBBY_READY;
    sMenu->Update();
    Check(sMenu->mPage == NP_LOBBY && !sMenu->mCampaignStart->IsEnabled(),
          "only-existing-backend-ready-state-enters-guest-lobby");
    OpenJoin(); fixture_join_ok = false;
    sMenu->mCode->Add(code.c_str()); sMenu->Choose(sMenu->mConnect);
    Check(sMenu->mPage == NP_JOIN && std::string(sMenu->mNotice) == "Fixture join refused.",
          "actual-join-failure-is-shown-without-synthetic-membership");
}

bool DrawnContains(const std::wstring& value) {
    for (const auto& text : fixture_drawn)
        if (text.find(value) != std::wstring::npos) return true;
    return false;
}

void ConnectAvailability()
{
    OpenJoin();
    const std::string code=FixtureCode(93);
    sMenu->mCode->Add(code.c_str());
    fixture_available=false;
    sMenu->Choose(sMenu->mConnect);
    Check(fixture_join_calls==0 && sMenu->mPage==NP_JOIN &&
        std::string(sMenu->mCode->mValue)==code && sMenu->mNotice[0],
        "connect-rechecks-route-availability-before-backend-without-losing-code");
    sMenu->mList[NP_JOIN]->mSelected=0;
    NetKeyTrap(DIK_RETURN,KEY_EVENT_DOWN);
    Check(fixture_join_calls==0,
        "keyboard-connect-cannot-bypass-unavailable-route");
    fixture_available=true;
    sMenu->Choose(sMenu->mConnect);
    Check(fixture_join_calls==1 && fixture_join_code==code,
        "availability-recovery-keeps-code-for-explicit-connect");
}

void RequestedRelayPresentation() {
    OpenJoin(true);
#if defined(CE_NETWORK_ICE)
    fixture_drawn.clear(); sMenu->DrawText();
    Check(DrawnContains(L"Two-player relay pilot") && DrawnContains(L"lobby"),
        "join-page-identifies-two-player-relay-pilot-and-manual-lobby-stage");
    Check(sMenu->mPage == NP_JOIN && !sMenu->mAddress->IsEnabled() &&
          !sMenu->mPasteAddress->IsEnabled(), "requested-relay-hides-unused-direct-address-input");
    const auto code = FixtureCode(8);
    sMenu->mCode->Add(code.c_str());
    sMenu->mAddress->Add("127.0.0.1:34567"); // Retained direct-mode field must not select a relay endpoint.
    sMenu->Choose(sMenu->mConnect);
    Check(fixture_join_calls == 1 && fixture_join_code == code && fixture_join_address.empty(),
          "relay-still-uses-real-code-owner-and-ignores-direct-address-field");
    std::snprintf(fixture_status.endpoint, sizeof fixture_status.endpoint, "%s", "127.0.0.1:34567");
    fixture_drawn.clear(); sMenu->DrawText();
    Check(DrawnContains(L"lobby files") && !DrawnContains(L"Connecting to ") &&
          !DrawnContains(L"public address"), "relay-pending-page-does-not-claim-direct-endpoint-or-forwarding");
    OpenJoin(true); sMenu->Back();
    sMenu->Choose(sMenu->mHostItem);
    Check(fixture_host_calls == 1 && sMenu->mPage == NP_HOST,
          "requested-relay-host-uses-existing-lobby-owner-without-unused-address-picker");
    std::snprintf(fixture_status.code, sizeof fixture_status.code, "%s", code.c_str());
    fixture_drawn.clear(); sMenu->DrawText();
    Check(DrawnContains(L"Two-player relay pilot") && !DrawnContains(L"This PC:") &&
          !DrawnContains(L"forward UDP"), "relay-host-page-shows-real-code-without-direct-routing-instructions");
    sMenu->Back();
    sMenu->Choose(sMenu->mList[NP_CONNECTION]->mItems.At(2));
    fixture_drawn.clear(); sMenu->DrawText();
    Check(sMenu->mPage == NP_HELP && DrawnContains(L"Copy each file completely") &&
          DrawnContains(L"matching game folders") && DrawnContains(L"Keep filenames and files private") &&
          !DrawnContains(L"Forward UDP port"), "relay-help-explains-two-fresh-stages-complete-files-and-relaunch");
    Check(DrawnContains(L"operator") && DrawnContains(L"two different, empty private folders") &&
        DrawnContains(L"Another connection attempt or mission") && DrawnContains(L"fresh folders") &&
        DrawnContains(L"No files are sent automatically") &&
        DrawnContains(L"has not been verified"),
        "pilot-help-requires-operator-setup-and-keeps-internet-acceptance-unverified");
#else
    Check(sMenu->mPage == NP_MENU && !sMenu->mHostItem->IsEnabled() &&
          !sMenu->mJoinItem->IsEnabled() && fixture_host_calls == 0 && fixture_join_calls == 0,
          "disabled-build-refuses-explicit-relay-request-without-direct-fallback");
    CNetItem host("Fixture host action", NA_HOST), join("Fixture join action", NA_JOIN);
    sMenu->Choose(&host); sMenu->Choose(&join);
    Check(fixture_host_calls == 0 && fixture_join_calls == 0 && sMenu->mPage == NP_MENU,
          "relay-unavailability-is-rechecked-at-action-time");
    fixture_drawn.clear(); sMenu->DrawText();
    Check(DrawnContains(L"unavailable in this build") && !DrawnContains(L"public address"),
          "disabled-relay-request-has-truthful-user-facing-explanation");
#endif
}
} // namespace

static void ConnectionModeEntry()
{
    OpenEntry(false);
    Check(sMenu->mPage == NP_MENU && !sMenu->mConnectionSelected, "mode entry starts without membership");
    Check(sMenu->mLocalItem->IsEnabled() && !sMenu->mOnlineItem->IsEnabled()
        && !sMenu->mBrowseItem->IsEnabled(), "only configured direct route is enabled");
    ChooseAction(NA_HELP);
    sMenu->Back();
    Check(sMenu->mPage == NP_MENU && !sMenu->mConnectionSelected,
        "help from mode entry returns to mode entry");
    ChooseAction(NA_ONLINE);
    Check(sMenu->mPage == NP_MENU && fixture_host_calls == 0 && fixture_join_calls == 0,
        "unconfigured online route cannot fall back");
    ChooseAction(NA_BROWSE);
    Check(sMenu->mPage == NP_MENU && fixture_host_calls == 0 && fixture_join_calls == 0
        && std::string(sMenu->mNotice).find("unavailable") != std::string::npos,
        "unavailable public browser makes no request");
    ChooseAction(NA_HOST);
    Check(sMenu->mPage == NP_MENU && fixture_host_calls == 0, "stale host action requires mode selection");
    ChooseAction(NA_LOCAL);
    Check(sMenu->mPage == NP_CONNECTION && fixture_host_calls == 0 && fixture_join_calls == 0,
        "mode selection alone starts no provider operation");
    ChooseAction(NA_HELP);
    sMenu->Back();
    Check(sMenu->mPage == NP_CONNECTION && sMenu->mConnectionSelected,
        "help from Host Join returns to the selected connection");
    ChooseAction(NA_JOIN);
    const std::string code = FixtureCode(17);
    sMenu->mCode->Add(code.c_str());
    ChooseAction(NA_LOCAL);
    Check(sMenu->mPage == NP_JOIN && std::string(sMenu->mCode->mValue) == code,
        "stale mode action cannot abandon the current join page");
    ChooseAction(NA_HOST_BEGIN);
    Check(fixture_host_calls == 0 && sMenu->mPage == NP_JOIN,
        "host submission cannot arrive from another page");
    fixture_available = false;
    ChooseAction(NA_CONNECT);
    Check(fixture_join_calls == 0 && std::string(sMenu->mCode->mValue) == code,
        "final join availability refusal preserves entered code");
    fixture_available = true;
    setenv("BEA_NET_ICE_DIRECTORY", "1", 1);
    ChooseAction(NA_CONNECT);
    Check(fixture_join_calls == 0 && std::string(sMenu->mCode->mValue) == code,
        "changed startup route cannot silently join another transport");
    unsetenv("BEA_NET_ICE_DIRECTORY");
    fixture_join_ok = false;
    sMenu->Update();
    Check(fixture_join_calls == 0, "availability recovery does not retry automatically");
    ChooseAction(NA_CONNECT);
    Check(fixture_join_calls == 1 && std::string(sMenu->mCode->mValue) == code,
        "explicit retry submits preserved code once");

    OpenEntry(false);
    ChooseAction(NA_LOCAL); ChooseAction(NA_HOST);
    Check(sMenu->mPage == NP_HOST_CHOOSE, "direct hosting still offers its address choice");
    fixture_available = false;
    ChooseAction(NA_HOST_BEGIN);
    Check(fixture_host_calls == 0 && sMenu->mPage == NP_HOST_CHOOSE,
        "final host availability is rechecked");
    fixture_available = true;
    setenv("BEA_NET_ICE_DIRECTORY", "1", 1);
    ChooseAction(NA_HOST_BEGIN);
    Check(fixture_host_calls == 0, "host submission rejects a changed startup route");
    unsetenv("BEA_NET_ICE_DIRECTORY");
    ChooseAction(NA_HOST_BEGIN);
    Check(fixture_host_calls == 1, "explicit host retry starts once");

    OpenEntry(true);
#if defined(CE_NETWORK_ICE)
    Check(!sMenu->mLocalItem->IsEnabled() && sMenu->mOnlineItem->IsEnabled(),
        "relay launch enables only online entry");
    ChooseAction(NA_LOCAL);
    Check(sMenu->mPage == NP_MENU && fixture_host_calls == 0, "relay selection cannot start direct hosting");
    ChooseAction(NA_ONLINE);
    Check(sMenu->mPage == NP_CONNECTION && fixture_host_calls == 0, "online entry is separate from hosting");
    ChooseAction(NA_HOST);
    Check(sMenu->mPage == NP_HOST && fixture_host_calls == 1, "online host uses existing manual relay owner");
#else
    Check(!sMenu->mLocalItem->IsEnabled() && !sMenu->mOnlineItem->IsEnabled(),
        "missing relay capability cannot enable a direct fallback");
    ChooseAction(NA_ONLINE);
    Check(sMenu->mPage == NP_MENU && fixture_host_calls == 0 && fixture_join_calls == 0,
        "missing relay capability starts no owner operation");
#endif
    sMenu->Back();
    if (sMenu->mPage == NP_CONNECTION) sMenu->Back();
    Check(sMenu->mPage == NP_MENU && !sMenu->mConnectionSelected,
        "returning to mode entry clears the previous selection");
}


#if defined(CE_NETWORK_ICE)
void NativeEntry(bool join = false)
{
    fixture_signal_selected = false;
    OpenEntry(true);
    fixture_signal_selected = true;
    fixture_signal_begin_ok = fixture_signal_accept_ok = fixture_signal_retry_ok = true;
    fixture_signal_begins = fixture_signal_pumps = fixture_signal_accepts = 0;
    fixture_signal_retries = fixture_signal_closes = 0;
    fixture_signal_origin = "https://signal.invalid";
    fixture_signal_status = CeIceSignalStatus::Unavailable;
    fixture_signal_pair_expiry = CE_HandoffNow() + 600;
    fixture_signal_setup_expiry = CE_HandoffNow() + 120;
    ChooseAction(NA_ONLINE); sMenu->Update();
    if (join) {
        ChooseAction(NA_JOIN); sMenu->Update();
        const std::string code = FixtureCode(77);
        snprintf(sMenu->mCode->mValue, sizeof sMenu->mCode->mValue, "%s", code.c_str());
        sMenu->mCode->Refresh();
    }
}
void InvitationEntry(bool paste = true)
{
    fixture_signal_selected = false; OpenEntry(false);
    fixture_signal_begins = fixture_signal_pumps = fixture_signal_accepts = fixture_signal_retries = 0;
    fixture_signal_closes = 0; fixture_signal_status = CeIceSignalStatus::Unavailable;
    fixture_signal_pair_expiry = CE_HandoffNow() + 600; fixture_signal_setup_expiry = CE_HandoffNow() + 120;
    fixture_invitation_begins = fixture_invitation_installs = fixture_invitation_cancels = 0;
    fixture_invitation_install_ok = true; memset(&fixture_invitation, 0, sizeof fixture_invitation);
    ChooseAction(NA_IMPORT_OPEN); sMenu->Update();
    if (paste) { fixture_clipboard = "https://signal.invalid"; ChooseAction(NA_IMPORT_PASTE); }
}
void InvitationMenuControls()
{
    InvitationEntry(false);
    Check(sMenu->mPage == NP_INVITATION_INPUT && !RelayRequested(), "import reachable before native relay selection");
    Check(!sMenu->mImportAddressLabel->IsEnabled() && !sMenu->mImportOrigin->mLabel8[0] &&
          std::wstring(sMenu->mImportAddressLabel->GetText()) == L"Service address" &&
          std::wstring(sMenu->mImportOrigin->GetText()) == L"https://",
          "address label and editable value occupy separate rows");
    Check(sMenu->mList[NP_INVITATION_INPUT]->mItems.At(sMenu->mList[NP_INVITATION_INPUT]->mSelected) == sMenu->mImportOrigin,
          "import opens with the editable address selected rather than its disabled label");
    sMenu->mImportOrigin->Clear(); NetKeyTrap('h', KEY_EVENT_CHAR);
    Check(std::string(sMenu->mImportOrigin->mValue) == "h" && !fixture_invitation_begins && !fixture_signal_begins,
          "initial keyboard focus edits the address without opening or contacting anything");
    fixture_clipboard = "https://signal.invalid"; ChooseAction(NA_IMPORT_PASTE);
    Check(sMenu->mList[NP_INVITATION_INPUT]->mItems.At(sMenu->mList[NP_INVITATION_INPUT]->mSelected) == sMenu->mImportOrigin,
          "paste returns focus to the editable address row");
    Check(std::string(sMenu->mImportOrigin->mValue) == "https://signal.invalid", "public Origin paste preserves exact recipient");
    fixture_clipboard = "private-admission-is-not-an-origin"; ChooseAction(NA_IMPORT_PASTE);
    Check(std::string(sMenu->mImportOrigin->mValue) == "https://signal.invalid", "non-origin clipboard input is not displayed");
    ChooseAction(NA_IMPORT_HOST);
    Check(fixture_invitation_begins == 1 && fixture_invitation_expected_role == CE_NET_HOST &&
          !fixture_invitation_installs && !fixture_signal_begins, "explicit Host import opens only local metadata choice");
    Check(!sMenu->mImportOrigin->IsEnabled() && !sMenu->mImportJoin->IsEnabled(), "recipient and role fixed during chooser");
    fixture_invitation.state = CE_INVITATION_PREVIEW_READY; sMenu->Update();
    Check(sMenu->mPage == NP_INVITATION_PREVIEW && sMenu->mImportUse->IsEnabled() && !fixture_invitation_installs,
          "metadata preview requires separate Use confirmation");
    fixture_drawn.clear(); sMenu->DrawText();
    Check(DrawnContains(L"Role: Host") && DrawnContains(L"https://signal.invalid") && DrawnContains(L"expires"),
          "preview exposes only role recipient and expiry");
    ChooseAction(NA_IMPORT_USE);
    Check(fixture_invitation_installs == 1 && fixture_signal_selected && sMenu->mPage == NP_SIGNAL_DISCLOSURE &&
          !fixture_signal_begins && !fixture_host_calls, "Use selects Host locally before service disclosure");
    ChooseAction(NA_SIGNAL_DISCLOSE); ChooseAction(NA_SIGNAL_ACCEPT);
    fixture_signal_status = CeIceSignalStatus::Admitted; sMenu->Update(); ChooseAction(NA_SIGNAL_PROCEED);
    Check(fixture_signal_begins == 1 && fixture_signal_accepts == 1 && fixture_host_calls == 1,
          "imported Host reaches connection only after Continue Accept and deliberate preparation");

    InvitationEntry(); ChooseAction(NA_IMPORT_JOIN);
    Check(fixture_invitation_expected_role == CE_NET_GUEST, "Join import fixes expected guest role");
    fixture_invitation.state = CE_INVITATION_PREVIEW_READY; sMenu->Update(); ChooseAction(NA_IMPORT_USE);
    Check(sMenu->mPage == NP_JOIN && fixture_signal_selected && !fixture_signal_begins && !fixture_join_calls,
          "Use selects Join without contacting service or connecting");
    const std::string code = FixtureCode(78);
    snprintf(sMenu->mCode->mValue, sizeof sMenu->mCode->mValue, "%s", code.c_str());
    sMenu->mCode->Refresh(); ChooseAction(NA_CONNECT);
    Check(sMenu->mPage == NP_SIGNAL_DISCLOSURE && !fixture_join_calls, "imported Join retains explicit network disclosure");
    ChooseAction(NA_SIGNAL_DISCLOSE); ChooseAction(NA_SIGNAL_ACCEPT);
    fixture_signal_status = CeIceSignalStatus::Admitted; sMenu->Update(); ChooseAction(NA_SIGNAL_PROCEED);
    Check(fixture_join_calls == 1 && fixture_join_code == code, "imported Join preserves host code through consent");

    InvitationEntry(); ChooseAction(NA_IMPORT_HOST);
    const int cancels = fixture_invitation_cancels; ChooseAction(NA_BACK);
    Check(fixture_invitation_cancels == cancels + 1 && !fixture_signal_selected && !fixture_invitation_installs &&
          !fixture_signal_begins, "cancel chooser cannot install or contact service");
    InvitationEntry(); ChooseAction(NA_IMPORT_HOST);
    fixture_invitation.state = CE_INVITATION_PREVIEW_READY; sMenu->Update(); ChooseAction(NA_BACK);
    Check(sMenu->mPage == NP_INVITATION_INPUT &&
          sMenu->mList[NP_INVITATION_INPUT]->mItems.At(sMenu->mList[NP_INVITATION_INPUT]->mSelected) == sMenu->mImportOrigin &&
          !fixture_invitation_installs && !fixture_signal_begins,
          "returning from preview restores the address focus without using the invitation");
    InvitationEntry(); ChooseAction(NA_IMPORT_HOST);
    fixture_invitation.state = CE_INVITATION_PREVIEW_READY; fixture_invitation.expires_at = CE_HandoffNow() - 1;
    sMenu->Update(); Check(!sMenu->mImportUse->IsEnabled(), "expired preview cannot install");
    InvitationEntry(); ChooseAction(NA_IMPORT_HOST);
    fixture_invitation.state = CE_INVITATION_PREVIEW_READY; fixture_invitation.role = CE_NET_GUEST;
    sMenu->Update(); Check(!sMenu->mImportUse->IsEnabled(), "wrong role preview cannot install");

    InvitationEntry(); ChooseAction(NA_IMPORT_HOST);
    fixture_invitation.state = CE_INVITATION_PREVIEW_READY; fixture_signal_selected = true; sMenu->Update();
    sMenu->mImportUse->mEnabled = TRUE; ChooseAction(NA_IMPORT_USE);
    Check(!fixture_invitation_installs && fixture_signal_selected && !fixture_signal_closes,
          "replacement cannot silently clear selected pair");
    ChooseAction(NA_IMPORT_CLEAR); sMenu->Update();
    Check(!fixture_signal_selected && fixture_signal_closes == 1 && sMenu->mImportUse->IsEnabled(),
          "explicit Clear permits a separately confirmed replacement");
    ChooseAction(NA_IMPORT_USE);
    Check(fixture_invitation_installs == 1 && fixture_signal_selected && !fixture_signal_begins,
          "replacement still waits for network disclosure");
    fixture_signal_selected = false;
}
void SignalMenuControls()
{
    NativeEntry();
    Check(!fixture_signal_begins && !fixture_signal_pumps && !fixture_signal_accepts,
          "native selection sends no request or consent");
    ChooseAction(NA_HOST); sMenu->Update();
    Check(sMenu->mPage == NP_SIGNAL_DISCLOSURE && !fixture_host_calls && !fixture_signal_begins && !fixture_signal_pumps,
          "Host disclosure precedes all preflight work");
    fixture_drawn.clear(); sMenu->DrawText();
    Check(DrawnContains(L"https://signal.invalid") && DrawnContains(L"Cloudflare TURN"),
          "disclosure names exact signaling origin and fixed relay recipient");
    ChooseAction(NA_SIGNAL_DISCLOSE);
    Check(sMenu->mPage == NP_SIGNAL_STATUS && fixture_signal_begins == 1 && !fixture_host_calls && !fixture_signal_accepts,
          "Continue starts only preflight");
    sMenu->Update();
    Check(fixture_signal_pumps == 1 && !fixture_signal_accepts, "only begun status page pumps preflight");
    fixture_drawn.clear(); sMenu->DrawText();
    Check(DrawnContains(L"Pair approval:") && DrawnContains(L"Setup window:"), "approval and setup expiry visible before consent");
    ChooseAction(NA_SIGNAL_ACCEPT); sMenu->Update();
    Check(fixture_signal_accepts == 1 && fixture_signal_status == CeIceSignalStatus::AwaitingPairConsent && !fixture_host_calls,
          "Accept is separate and waits for peer");
    Check(!sMenu->mSignalProceed->IsEnabled(), "waiting pair cannot connect");
    fixture_signal_status = CeIceSignalStatus::Admitted; sMenu->Update();
    Check(!fixture_host_calls && sMenu->mSignalProceed->IsEnabled(), "admission never auto-connects");
    ChooseAction(NA_SIGNAL_PROCEED);
    Check(fixture_host_calls == 1 && sMenu->mPage == NP_HOST, "deliberate admitted Continue reaches existing Host");
    const int pumps = fixture_signal_pumps; sMenu->Update();
    Check(fixture_signal_pumps == pumps, "post-Host session owns stage polling");

    NativeEntry(true);
    const std::string retained = sMenu->mCode->mValue;
    ChooseAction(NA_CONNECT);
    Check(sMenu->mPage == NP_SIGNAL_DISCLOSURE && !fixture_join_calls, "Join disclosure precedes connection");
    ChooseAction(NA_SIGNAL_DISCLOSE); ChooseAction(NA_SIGNAL_ACCEPT);
    fixture_signal_status = CeIceSignalStatus::Admitted; sMenu->Update();
    ChooseAction(NA_SIGNAL_PROCEED);
    Check(fixture_join_calls == 1 && fixture_join_code == retained, "Join preserves typed code through consent");

    NativeEntry(); ChooseAction(NA_HOST); ChooseAction(NA_SIGNAL_DISCLOSE); ChooseAction(NA_SIGNAL_ACCEPT);
    fixture_signal_status = CeIceSignalStatus::Admitted; sMenu->Update();
    fixture_available = false; sMenu->mHostItem->mEnabled = TRUE;
    ChooseAction(NA_SIGNAL_PROCEED);
    Check(!fixture_host_calls && fixture_signal_selected, "final Host submission rechecks availability without fallback");

    NativeEntry(); ChooseAction(NA_HOST); ChooseAction(NA_BACK);
    Check(!fixture_signal_begins && !fixture_signal_pumps && fixture_signal_closes == 1 && fixture_signal_selected,
          "declining disclosure closes pair without contacting service or clearing selection");
    ChooseAction(NA_HOST); sMenu->Update();
    Check(sMenu->mPage == NP_SIGNAL_STATUS && !sMenu->mSignalProceed->IsEnabled() && !sMenu->mSignalRetry->IsEnabled(),
          "closed selected pair remains refused without manual fallback");

    NativeEntry(); ChooseAction(NA_HOST); ChooseAction(NA_SIGNAL_DISCLOSE);
    fixture_signal_status = CeIceSignalStatus::Unavailable; sMenu->Update();
    const int retryPumps = fixture_signal_pumps; sMenu->Update();
    Check(!fixture_signal_retries && fixture_signal_pumps == retryPumps + 1, "status pump never retries automatically");
    ChooseAction(NA_SIGNAL_RETRY);
    Check(fixture_signal_retries == 1 && !fixture_host_calls && !fixture_join_calls,
          "transient retry is deliberate and does not connect");
    fixture_signal_status = CeIceSignalStatus::IssuanceUnknown; sMenu->Update();
    Check(!sMenu->mSignalRetry->IsEnabled() && !sMenu->mSignalProceed->IsEnabled(), "uncertain issuance cannot retry or connect");
    ChooseAction(NA_SIGNAL_CLOSE); fixture_drawn.clear(); sMenu->DrawText();
    Check(fixture_signal_closes == 1 && fixture_signal_selected && DrawnContains(L"Approved pair closed"),
          "explicit close exposes terminal state without clearing selected route");

    NativeEntry(); ChooseAction(NA_HOST); ChooseAction(NA_SIGNAL_DISCLOSE);
    fixture_signal_origin = "https://next.invalid"; const int beforeChange = fixture_signal_pumps;
    sMenu->Update();
    Check(sMenu->mPage == NP_SIGNAL_DISCLOSURE && fixture_signal_pumps == beforeChange &&
          fixture_signal_begins == 1, "changed recipient stops pumping and requires fresh disclosure");
    ChooseAction(NA_SIGNAL_DISCLOSE);
    Check(fixture_signal_begins == 2, "new recipient contacted only after another Continue");

    NativeEntry(); fixture_signal_origin.clear(); ChooseAction(NA_HOST); sMenu->Update();
    Check(sMenu->mPage == NP_SIGNAL_DISCLOSURE && !sMenu->mSignalContinue->IsEnabled() && !fixture_signal_begins,
          "missing validated origin stays unavailable without network work");
    fixture_signal_selected = false;
}
#endif

void FieldTextBounds()
{
    struct GuardedText { char before; char text[160]; char after; } guarded;
    const std::string labels[] = {"Service address: ", std::string(511, 'L'), ""};
    const std::string values[] = {"https://signal.invalid", "short", std::string(511, 'V')};
    for (unsigned i = 0; i != 3; ++i) {
        guarded.before = 'B'; guarded.after = 'A';
        memset(guarded.text, 'X', sizeof guarded.text);
        FieldText(guarded.text, sizeof guarded.text, labels[i].c_str(), values[i].c_str());
        const std::string expected = (labels[i] + values[i]).substr(0, sizeof guarded.text - 1);
        Check(guarded.before == 'B' && guarded.after == 'A' &&
              guarded.text[expected.size()] == 0 &&
              memcmp(guarded.text, expected.data(), expected.size()) == 0,
              "field display preserves the exact bounded label/value prefix and guard bytes");
    }
    char one = 'X'; FieldText(&one, 1, "label", "value");
    Check(one == 0, "one-byte field display is an empty terminated string");
    char untouched = 'X'; FieldText(&untouched, 0, "label", "value");
    Check(untouched == 'X', "zero-capacity field display writes nothing");
}

int main() {
    FieldTextBounds();
    ExplicitPaste(); FailedPastePreservesFields(); KeyboardAndConnecting(); ClearCloseAndAdmission();
    ConnectAvailability(); RequestedRelayPresentation();
    ConnectionModeEntry();
#if defined(CE_NETWORK_ICE)
    SignalMenuControls();
    InvitationMenuControls();
#endif
    PortCeNetShutdown();
    std::printf("{\"checks\":%d,\"failures\":%d}\n", checks, failures);
    return failures ? 1 : 0;
}
