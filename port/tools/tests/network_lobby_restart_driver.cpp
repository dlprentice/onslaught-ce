// SPDX-License-Identifier: GPL-3.0-only
// The lobby's restart writes the private start file (ce_netlobby.cpp BeginRestart, extracted unchanged); the real reader (ce_netcode.cpp CE_HandoffRead) must accept what it wrote, for
// each role and each co-op map, and every field must come back as the lobby meant it. Authored scratch folder only: no game, window or network.
// Run by port/tools/network_lobby_restart_handoff_tests.py, which also kills mutants of the extracted function and of the reader.
#include "ce_netcode.h"
#include "ce_netlobby.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include "../../game/ce_network_campaign_return.h"
#include "../../network/compatibility.h"
#include "../../network/network.h"
#include "../../network/ice_game.h"

#define LOBBY_LOG "authored lobby fixture: "

extern "C" int CE_NetGenerateSecret(uint8_t out[32])
{
    static unsigned sequence = 0;
    ++sequence;
    for (unsigned i = 0; i < 32; ++i) out[i] = (uint8_t)(sequence + i);
    return 1;
}

// The fields of the lobby's state that the restart reads and writes (the extracted code fails to compile if it starts using another).
static constexpr uint8_t MSG_CAMPAIGN_GO = 103;
struct Lobby {
    uint64_t key = 0;
    uint64_t startAt = 0;
    void *net = NULL;
    bool campaign = false;
	int state = CE_LOBBY_OFF;
	bool host = false;
	bool startSent = false;
	uint8_t secret[32] = {};
	uint8_t nonce[16] = {};
	uint32_t ip = 0;
	uint16_t port = 0;
	char bind[16] = {};
	uint32_t level = CE_LOBBY_DEFAULT_LEVEL;
	CE_LobbyStatus st = {};
};
static Lobby L;
enum { RESTART_NONE = 0, RESTART_LAUNCH, RESTART_RETURN };
enum { MSG_GO = 4 };
static int gRestart = RESTART_NONE;
static uint32_t gStartLevel = CE_LOBBY_DEFAULT_LEVEL;
static const char kOwnArgs[] = "-skipfmv -forcewindowed -res 640 480";
static int gClosed, gCreated, gGoSent;
static bool gCreateSucceeds = true;

static bool OwnArgs(char *out, size_t n) { snprintf(out, n, "%s", kOwnArgs); return true; }
static void Close(bool) { gClosed++; L.net = NULL; L.startSent = false; }
// the host's GO (the lobby's own Send is exercised by network_lobby_start_tests.py): counted, and the host's start file is already written when it is queued
static bool Send(uint8_t type, const uint8_t *, uint32_t) { gGoSent += type == MSG_GO || type == MSG_CAMPAIGN_GO; return true; }
static bool Create() { gCreated++; L.net = gCreateSucceeds ? (void *)1 : NULL; return gCreateSucceeds; }
static void Text(char *out, size_t n, const char *fmt, const char *a = "", const char *b = "") { snprintf(out, n, fmt, a, b); }

#include ACTUAL_USERDIR
#include ACTUAL_HANDOFFPATH
#include ACTUAL_DOTTED
static const uint8_t MSG_START = 2;
static const uint8_t MSG_CAMPAIGN_START = 101;
extern "C" uint64_t CE_NetNowMs(void) { return 1000; }
#include ACTUAL_TRANSFER
#include ACTUAL_BEGINRESTART
#include ACTUAL_LOBBYSTART

static int failures;
static void check(bool ok, const char *label)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", label);
	failures += !ok;
}


CE_Handoff gOutcome = {};
bool gOutcomeReady = false;
bool gLaunched = false;
char gReturnArgs[4096] = {};
static std::string executedPath;
static bool executedWithOwner = false;
static bool simulateReturnBoot = false;
void TestBoot();
static int campaignAdmissions = 0;

#include ACTUAL_SETENV
#include ACTUAL_HEX
#include ACTUAL_SCRUBENVIRONMENT

static void Rebuild(const char *args, const char *, char *out, size_t n) {
    snprintf(out, n, "%s", args);
}
static void Exec(char *) {
    executedPath = gHandoffPath;
    executedWithOwner = getenv("BEA_NET_HANDOFF_FILE") && getenv("BEA_NET_HANDOFF_OWNER");
    if (simulateReturnBoot) TestBoot();
}
extern "C" int CE_BootCampaignAdmit(const CE_Handoff *h) {
    ++campaignAdmissions;
    return CE_HandoffCampaignValid(h);
}
#include ACTUAL_RETURNTOFRONTEND
static void TestExit(int) { throw 1; }
#define _exit TestExit
#include ACTUAL_BOOT
#undef _exit

static bool sActive = false, menuTrapped = false;
static unsigned menuDeleted = 0, menuDeactivated = 0;
enum { NA_NONE = 0 };
struct TestField {
    bool populated = true;
    unsigned clears = 0;
    void Clear() { populated = false; ++clears; }
};
struct TestMenu {
    TestField code, address;
    TestField *mCode = &code, *mAddress = &address;
    bool mSignalApproved = true, mSignalBegan = true;
    int mSignalAction = 7;
    char mSignalOrigin[64] = "authored-origin";
    void Trap(bool trapped) { menuTrapped = trapped; }
    void DeActivate(bool) { ++menuDeactivated; }
    ~TestMenu() { ++menuDeleted; }
#define FALSE false
#include ACTUAL_MENUCLEARSIGNAL
#include ACTUAL_MENUCLOSE
#undef FALSE
};
static TestMenu *sMenu = NULL;
#define SAFE_DELETE(pointer) do { delete pointer; pointer = NULL; } while (false)
#include ACTUAL_MENUSHUTDOWN
#undef SAFE_DELETE


#include ACTUAL_FORGET
#include ACTUAL_LEAVE
#include ACTUAL_SHUTDOWN
bool gShellExited = true;
bool gShellNativeQuit = false;
bool gLevelToSystem = false;
int gShellCode = 0;
static int PortCeQuitRequested() { return 0; }
static const char *CE_SessionError() { return ""; }
static const char *CE_BootError() { return ""; }
static void SDL_Quit() {}
#include ACTUAL_AFTEREXIT

static long file_size(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? (long)st.st_size : -1;
}

static void fixture(bool host) {
    ce_campaign_return_clear();
    gContinuation = {};
    gContinuationAttempt = 0;
    ClearPendingHandoff();
    L = Lobby();
    L.host = host;
    L.campaign = true;
    L.level = CE_LOBBY_DEFAULT_LEVEL;
    L.port = 27652;
    snprintf(L.bind, sizeof(L.bind), "%s", "127.0.0.1");
    L.ip = 0x7f000001;
    memset(L.nonce, 73, sizeof(L.nonce));
    for (unsigned i = 0; i < 32; ++i) L.secret[i] = (uint8_t)(i + 5);
    gRestart = RESTART_NONE;
    gLaunched = false;
    gOutcomeReady = false;
    campaignAdmissions = 0;
    gClosed = gCreated = gGoSent = false;
    gCreateSucceeds = true;
    SetEnv("BEA_NET_HOST", NULL);
    SetEnv("BEA_NET_JOIN", NULL);
    SetEnv("BEA_NET_SECRET", NULL);
    SetEnv("BEA_NET_ICE_DIRECTORY", NULL);
    SetEnv("BEA_NET_ICE_GAME_DIRECTORY", NULL);
}

static int handoff_write_failure_case(const char *mode, const char *scratch) {
    const bool host = strstr(mode, "guest") == NULL;
    const bool relay = strstr(mode, "relay") != NULL;
    const bool retry_fails = strstr(mode, "listener-failed") != NULL;
    fixture(host);
    L.state = CE_LOBBY_READY;
    L.net = (void *)1;
    L.startSent = true;
    gCreateSucceeds = !retry_fails;
    if (relay) {
        // Only the real route selector reads these authored labels; no ICE factory or I/O runs.
        SetEnv("BEA_NET_ICE_DIRECTORY", "authored-unused-lobby-directory");
        SetEnv("BEA_NET_ICE_GAME_DIRECTORY", "authored-unused-game-directory");
    }
    // A missing authored parent makes the real owned handoff writer fail before publication.
    const std::string missing = std::string(scratch) + "/missing-handoff-parent";
    SetEnv("BEA_USER_DIR", missing.c_str());
    BeginRestart();
    check(gClosed == 1 && !L.startSent, "handoff write failure closes the old lobby");
    check(gRestart == RESTART_NONE && !gHandoffPath[0] && !gHandoffReceipt[0],
          "handoff write failure cannot queue a launch");
    check(gGoSent == 0, "handoff write failure cannot send GO");
    if (relay) {
        check(L.state == CE_LOBBY_FAILED, "relay handoff write failure is terminal");
        check(gCreated == 0 && !L.net, "relay handoff write failure never recreates its exchange");
        check(strstr(L.st.notice, "private start file") &&
              strstr(L.st.notice, "Relaunch both games") &&
              strstr(L.st.notice, "fresh private lobby and game folders") &&
              strstr(L.st.notice, "code and file exchange"),
              "relay handoff write failure explains fresh setup recovery");
    } else if (host && retry_fails) {
        check(gCreated == 1 && !L.net, "failed direct retry leaves no listener");
        check(L.state == CE_LOBBY_FAILED, "failed direct retry is terminal");
        check(strstr(L.st.notice, "could not listen again") != NULL,
              "failed direct retry retains the listen failure notice");
    } else if (host) {
        check(gCreated == 1 && L.net, "direct host handoff write failure retries listening");
        check(L.state == CE_LOBBY_HOST_WAITING, "direct host retry returns to its waiting page");
        check(strstr(L.st.notice, "private start file") != NULL,
              "direct host retains the handoff failure notice");
    } else {
        check(gCreated == 0 && !L.net, "direct guest handoff write failure does not listen");
        check(L.state == CE_LOBBY_FAILED, "direct guest handoff write failure stays failed");
        check(strstr(L.st.notice, "private start file") != NULL,
              "direct guest retains the handoff failure notice");
    }
    SetEnv("BEA_USER_DIR", scratch);
    return failures ? 1 : 0;
}

static void explicit_leave_context(const CE_Handoff &prototype) {
    fixture(true);
    CE_Handoff returned = {};
    returned.kind = CE_HANDOFF_OUTCOME;
    returned.campaign.version = 1;
    returned.campaign.session = 7001;
    returned.campaign.attempt = 9;
    returned.campaign.mission = CE_LOBBY_DEFAULT_LEVEL;
    ResumeCampaignFromOutcome(returned);
    L.state = CE_LOBBY_STARTING;
    CE_LobbyLeave();
    CE_Handoff queued = prototype; queued.kind = CE_HANDOFF_LAUNCH; queued.level = CE_LOBBY_DEFAULT_LEVEL;
    for (unsigned i = 0; i < sizeof queued.nonce; ++i) queued.nonce[i] = (uint8_t)(20 + i);
    const bool queued_nonce = ContinueCampaignNonce(queued.nonce);
    check(queued_nonce && CE_HandoffCampaignSet(&queued) &&
        queued.campaign.session == returned.campaign.session && queued.campaign.attempt == 10,
        "starting leave preserves campaign retry context");

    L.state = CE_LOBBY_HOST_WAITING;
    CE_LobbyLeave();
    CE_Handoff fresh = prototype; fresh.kind = CE_HANDOFF_LAUNCH; fresh.level = CE_LOBBY_DEFAULT_LEVEL;
    uint8_t expected[sizeof fresh.nonce];
    for (unsigned i = 0; i < sizeof fresh.nonce; ++i) expected[i] = fresh.nonce[i] = (uint8_t)(40 + i);
    check(ContinueCampaignNonce(fresh.nonce) && !memcmp(fresh.nonce, expected, sizeof expected),
        "explicit leave releases the abandoned campaign nonce context");
    check(CE_HandoffCampaignSet(&fresh) &&
        fresh.campaign.session != returned.campaign.session,
        "campaign started after leaving has a fresh session identity");

    returned.campaign.session = 8002; returned.campaign.attempt = 13;
    ResumeCampaignFromOutcome(returned);
    CE_Handoff resumed = prototype; resumed.kind = CE_HANDOFF_LAUNCH; resumed.level = CE_LOBBY_DEFAULT_LEVEL;
    check(ContinueCampaignNonce(resumed.nonce) && CE_HandoffCampaignSet(&resumed) &&
        resumed.campaign.session == returned.campaign.session && resumed.campaign.attempt == 14,
        "a later returned campaign can establish its own continuation");
}

static int menu_shutdown_case(const char *mode) {
    const bool host = strstr(mode, "guest") == NULL;
    const bool cancel = strncmp(mode, "cancel-", 7) == 0;
    const bool idle = strcmp(mode, "shutdown-idle") == 0;
    fixture(host);
    CE_Handoff returned = {};
    returned.kind = CE_HANDOFF_OUTCOME;
    returned.good = true;
    returned.campaign.version = 1;
    returned.campaign.session = 9001;
    returned.campaign.attempt = 11;
    returned.campaign.mission = CE_LOBBY_DEFAULT_LEVEL;
    ResumeCampaignFromOutcome(returned);
    sMenu = new TestMenu;
    sActive = menuTrapped = true;
    if (idle) {
        L.state = CE_LOBBY_HOST_WAITING;
        PortCeNetShutdown();
        check(gClosed && !gContinuation.version && !gContinuationAttempt,
              "ordinary shutdown releases lobby and continuation");
        check(!sMenu && !sActive && !menuTrapped && menuDeleted == 1,
              "ordinary shutdown completes actual menu cleanup");
        CE_LobbyAfterExit();
        check(executedPath.empty(), "ordinary shutdown cannot dispatch a launch");
        return failures ? 1 : 0;
    }
    BeginRestart();
    const std::string path = gHandoffPath;
    const std::string owner = gHandoffReceipt;
    check(gRestart == RESTART_LAUNCH && file_size(path) > 0 && !owner.empty(),
          "shutdown case begins with an owned queued launch");
    if (cancel) {
        sMenu->Close();
        check(gRestart == RESTART_NONE && file_size(path) == 0,
              "explicit menu close cancels its queued launch");
        check(menuDeactivated == 1 && !sActive && !menuTrapped,
              "explicit menu close completes its actual UI cleanup");
        check(!sMenu->mSignalApproved && !sMenu->mSignalBegan && sMenu->mSignalAction == NA_NONE && !sMenu->mSignalOrigin[0],
              "explicit menu close clears signaling approval");
        check(!sMenu->code.populated && sMenu->code.clears == 1,
              "explicit menu close clears the code field");
        check(!sMenu->address.populated && sMenu->address.clears == 1,
              "explicit menu close clears the address field");
    }
    PortCeNetShutdown();
    check(!sMenu && !sActive && !menuTrapped && menuDeleted == 1,
          "automatic shutdown completes actual menu cleanup");
    if (!cancel) {
        check(gRestart == RESTART_LAUNCH && path == gHandoffPath && owner == gHandoffReceipt && file_size(path) > 0,
              "automatic shutdown preserves the queued launch");
        check(gContinuation.session == returned.campaign.session && gContinuationAttempt == returned.campaign.attempt,
              "automatic shutdown preserves campaign continuation");
    }
    PortCeNetShutdown();
    check(menuDeleted == 1, "repeated menu shutdown is harmless");
    CE_LobbyAfterExit();
    if (cancel)
        check(executedPath.empty(), "explicit cancellation prevents exit dispatch");
    else
        check(executedWithOwner && executedPath == path,
              "automatic shutdown reaches the actual owned exit dispatcher");
    check(file_size(path) == 0, "returned dispatch leaves no owned pending launch");
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    setenv("BEA_USER_DIR", argv[1], 1);
    if (argc == 3) {
        if (strncmp(argv[2], "write-failure-", 14) == 0)
            return handoff_write_failure_case(argv[2], argv[1]);
        return menu_shutdown_case(argv[2]);
    }
    for (int host = 0; host != 2; ++host) {
        fixture(host != 0);
        BeginRestart();
        check(gRestart == RESTART_LAUNCH, "restart is queued with an owned handoff");
        std::string path = gHandoffPath;
        check(file_size(path) > 0, "restart creates a complete private record");
        check(gHandoffReceipt[0] != 0, "restart retains a serializable owner receipt");
        ScrubEnvironment("launch");
        check(getenv("BEA_NET_HANDOFF_FILE") && getenv("BEA_NET_HANDOFF_OWNER"),
              "restart publishes both ownership inputs");
        bool refused = false;
        try { TestBoot(); } catch (int) { refused = true; }
        check(!refused && gLaunched, "boot consumes owned launch");
        check(campaignAdmissions == 1, "launch admits its typed campaign before lockstep");
        check(file_size(path) == 0, "boot clears the admitted owned record");
        check(!getenv("BEA_NET_HANDOFF_FILE") && !getenv("BEA_NET_HANDOFF_OWNER"),
              "boot scrubs the incoming ownership environment");
        check(getenv(host ? "BEA_NET_HOST" : "BEA_NET_JOIN") != NULL,
              "boot selects the participant lockstep endpoint");
        check(getenv("BEA_NET_SECRET") != NULL, "boot passes the start secret to lockstep");
        check(gReturnArgs[0] != 0, "boot preserves return arguments");
    }

    fixture(true);
    BeginRestart();
    std::string cancelled = gHandoffPath;
    CE_LobbyLeave();
    check(file_size(cancelled) == 0, "cancellation clears only its owned entry");
    check(gRestart == RESTART_NONE, "cancellation withdraws the queued restart");
    fixture(true);
    BeginRestart();
    std::string retry = gHandoffPath;
    check(gRestart == RESTART_LAUNCH && retry != cancelled && file_size(retry) > 0,
          "retry allocates a fresh owned entry");

    ScrubEnvironment("launch");
    setenv("BEA_NET_HANDOFF_OWNER", "invalid-owner", 1);
    CaptureHandoffEnvironment();
    CE_Handoff denied = {};
    const char *why = NULL;
    long before = file_size(retry);
    check(!ReadOwnedHandoff("launch", CE_HandoffNow(), &denied, &why),
          "missing ownership is refused");
    check(file_size(retry) == before && before > 0,
          "refused ownership preserves the pending record");

    fixture(true);
    BeginRestart();
    std::string wrongKind = gHandoffPath;
    ScrubEnvironment("launch");
    CaptureHandoffEnvironment();
    why = NULL;
    check(!ReadOwnedHandoff("outcome", CE_HandoffNow(), &denied, &why),
          "launch cannot masquerade as an outcome");
    check(file_size(wrongKind) > 0, "wrong-kind refusal retains the authored record");

    fixture(true);
    BeginRestart();
    std::string launchExec = gHandoffPath;
    CE_LobbyAfterExit();
    check(executedWithOwner, "launch execution receives its owned record");
    check(file_size(launchExec) == 0, "failed launch execution clears its owned record");

    fixture(true);
    snprintf(gReturnArgs, sizeof(gReturnArgs), "%s", "authored-return");
    ReturnToFrontEnd(true, "Authored campaign return");
    check(executedWithOwner, "return execution receives its owned outcome");
    check(!executedPath.empty() && file_size(executedPath) == 0,
          "failed execution clears its owned outcome");
    check(!gHandoffPath[0] && !getenv("BEA_NET_HANDOFF_OWNER"),
          "failed execution resets pending ownership");

    CE_Handoff outcome = {};
    outcome.kind = CE_HANDOFF_OUTCOME;
    outcome.created = CE_HandoffNow();
    outcome.good = 1;
    snprintf(outcome.text, sizeof(outcome.text), "%s", "Authored result");
    why = NULL;
    check(WriteOwnedHandoff("outcome", &outcome, &why), "owned outcome is prepared");
    std::string outcomePath = gHandoffPath;
    ScrubEnvironment("outcome");
    TestBoot();
    check(gOutcomeReady && gOutcome.good, "boot admits the owned outcome");
    check(campaignAdmissions == 0, "outcome does not admit a campaign launch");
    check(file_size(outcomePath) == 0, "outcome consumption clears its owned entry");
    ClearPendingHandoff();
    fixture(true);
    CeNetworkCampaign completed = {};
    uint32_t returnedMission = CE_LOBBY_DEFAULT_LEVEL;
    for (uint32_t candidate = CE_LOBBY_DEFAULT_LEVEL + 1; candidate < 10000; ++candidate)
        if (CE_CompatibilityLockstepLevel(candidate)) { returnedMission = candidate; break; }
    check(returnedMission != CE_LOBBY_DEFAULT_LEVEL, "a distinct supported mission is available");
    completed.identity = {37, 49, returnedMission};
    completed.phase = CE_CAMPAIGN_AGREED_WIN;
    completed.host_outcome = completed.guest_outcome = CE_CAMPAIGN_OUTCOME_WIN;
    check(ce_campaign_return_capture(&completed, true, false), "confirmed runtime result is available");
    simulateReturnBoot = true;
    ReturnToFrontEnd(true, "Authored completed mission");
    simulateReturnBoot = false;
    check(gContinuation.session == 37 && gContinuation.attempt == 49 &&
          gContinuation.mission == returnedMission,
          "confirmed return keeps the campaign identity");
    check(gOutcomeReady && gOutcome.good, "confirmed return preserves agreed victory");
    Forget();
    check(L.level == returnedMission, "return restores mission selection for retry");
    L.host = true;
    L.net = (void *)1;
    L.state = CE_LOBBY_READY;
    L.startSent = false;
    LobbyStart(true);
    CE_Handoff continued = {};
    continued.kind = CE_HANDOFF_LAUNCH;
    continued.role = CE_NET_HOST;
    continued.level = L.level;
    memcpy(continued.nonce, L.nonce, sizeof(continued.nonce));
    check(CE_HandoffCampaignSet(&continued), "continued nonce remains valid launch intent");
    check(continued.campaign.session == 37, "host continuation reuses the completed session");
    check(continued.campaign.attempt == 50, "host continuation allocates the next attempt");
    check(continued.campaign.mission == completed.identity.mission,
          "continuation leaves mission selection explicit");
    L.startSent = false;
    L.state = CE_LOBBY_READY;
    LobbyStart(true);
    memcpy(continued.nonce, L.nonce, sizeof(continued.nonce));
    check(CE_HandoffCampaignSet(&continued) && continued.campaign.attempt == 51,
          "a refused or retried start cannot reuse an attempt");
    gContinuationAttempt = UINT64_MAX;
    uint8_t frozenNonce[16];
    memcpy(frozenNonce, L.nonce, sizeof(frozenNonce));
    check(!ContinueCampaignNonce(L.nonce) && memcmp(frozenNonce, L.nonce, sizeof(frozenNonce)) == 0,
          "attempt exhaustion fails without wrapping or changing the nonce");

    fixture(true);
    completed.phase = CE_CAMPAIGN_AGREED_LOSS;
    completed.host_outcome = completed.guest_outcome = CE_CAMPAIGN_OUTCOME_LOSS;
    ce_campaign_return_capture(&completed, true, false);
    simulateReturnBoot = true;
    ReturnToFrontEnd(true, "Authored agreed defeat");
    simulateReturnBoot = false;
    check(gContinuation.session == 37 && !gOutcome.good,
          "agreed defeat retains context for an explicit retry");

    fixture(true);
    ce_campaign_return_capture(&completed, true, false);
    simulateReturnBoot = true;
    ReturnToFrontEnd(false, "Authored runtime failure");
    simulateReturnBoot = false;
    check(!gContinuation.version && !gOutcome.good,
          "runtime failure cannot carry earlier confirmed continuation");
    ClearPendingHandoff();
    explicit_leave_context(continued);
    printf("checks completed; failures=%d\n", failures);
    return failures ? 1 : 0;
}
