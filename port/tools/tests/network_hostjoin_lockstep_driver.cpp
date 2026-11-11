// SPDX-License-Identifier: GPL-3.0-only
// Extracted production menu and restart-scrub functions; authored environment only, no game or network.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../../platform/common/ce_netcode.h"
#define LOBBY_LOG "authored lobby fixture: "
#define CE_NETWORK_ENABLED 1	// authored network-capable build; no transport is created
static bool menus=true;
static bool PortCeMenus() { return menus; }
struct Parameters { bool mRecordDemo=false, mPlaybackDemo=false, mAutoConfigTest=false; } CLIPARAMS;
static const char *CE_NETWORK_SIMULATION_ID="authored-test-build";
enum { RESTART_NONE=0, RESTART_LAUNCH=1, RESTART_RETURN=2 };
static int gRestart=RESTART_NONE,gShellCode=0,gShellNativeQuit=0;
static unsigned gStartLevel=856;	// the map the lobby was told to start (the restart's -level)
static bool gShellExited=false,gLaunched=false,gLevelToSystem=false;
static int calls_to_exec=0,calls_to_return=0;
static int owned_clear_calls=0;
static bool own_args_available=false,owned_clear_saw_identity=false;
static char gHandoffName[128]={},gHandoffPath[4096]={},gHandoffReceipt[512]={};
static CE_HandoffOwner gHandoffOwner={};
extern "C" void CE_CodeWipe(void *out,size_t size) { std::memset(out,0,size); }
// No file is opened or cleared: observe the real caller before it wipes its owner.
extern "C" int CE_HandoffOwnedClear(const char *path,const CE_HandoffOwner *owner,const char **) {
    ++owned_clear_calls;
    owned_clear_saw_identity=!std::strcmp(path,"authored-pending") && owner->version==CE_HANDOFF_OWNER_VERSION;
    return 1;
}
static bool OwnArgs(char *out,size_t size) { std::snprintf(out,size,"authored-args");return own_args_available; }
static void Rebuild(const char *,const char *,char *,size_t) {}
static void Exec(const char *) { ++calls_to_exec; }
static bool PortCeQuitRequested() { return false; }
static const char *CE_SessionError() { return ""; }
static const char *CE_BootError() { return ""; }
static void ReturnToFrontEnd(bool,const char *) { ++calls_to_return; }
#include ACTUAL_OFFER
#include ACTUAL_SETENV
#include ACTUAL_SCRUB
#include ACTUAL_CLEAR_PENDING
#include ACTUAL_USERDIR
#include ACTUAL_AVAILABLE
#include ACTUAL_LOBBYSHELL
#include ACTUAL_PORTSHELL
#include ACTUAL_PORTQUIT
#include ACTUAL_AFTEREXIT
static int failures=0;
static void check(bool ok,const char *label) { std::printf("%s %s\n",ok?"PASS":"FAIL",label);failures+=!ok; }
int main(int argc,char **argv) {
    if (argc>1 && !std::strcmp(argv[1],"failed-launch")) {
        own_args_available=true;gRestart=RESTART_LAUNCH;
        std::snprintf(gHandoffName,sizeof gHandoffName,"authored-name");
        std::snprintf(gHandoffPath,sizeof gHandoffPath,"authored-pending");
        std::snprintf(gHandoffReceipt,sizeof gHandoffReceipt,"authored-owner");
        gHandoffOwner.version=CE_HANDOFF_OWNER_VERSION;
        CE_LobbyAfterExit();
        check(calls_to_exec==1 && owned_clear_calls==1 && owned_clear_saw_identity,
              "failed launch dispatch clears the pending owned record");
        check(!gHandoffOwner.version && !gHandoffReceipt[0],"failed launch clears pending owner and receipt buffers");
        check(!gHandoffName[0] && !gHandoffPath[0] && !getenv("BEA_NET_HANDOFF_FILE") && !getenv("BEA_NET_HANDOFF_OWNER"),
              "failed launch clears pending handoff names and environment");
        return failures?1:0;
    }
    const char *blocked[]={"BEA_PORT_CLOCK","BEA_PORT_TAPE","BEA_PORT_PROBES","BEA_PORT_RECORD",
        "BEA_PORT_PILOT","BEA_PORT_AUTOPILOT","BEA_RECORD","BEA_NET_HOST","BEA_NET_JOIN",
        "BEA_LOCKSTEP_HOST","BEA_LOCKSTEP_JOIN"};
    for (const char *name:blocked) unsetenv(name);
    check(PortCeNetOffered(),"ordinary single-player frontend retains the network entry");
    menus=false;check(!PortCeNetOffered(),"disabled CE menus do not offer networking");menus=true;
    CLIPARAMS.mRecordDemo=true;check(!PortCeNetOffered(),"demo recording does not offer networking");CLIPARAMS.mRecordDemo=false;
    CLIPARAMS.mPlaybackDemo=true;check(!PortCeNetOffered(),"demo playback does not offer networking");CLIPARAMS.mPlaybackDemo=false;
    CLIPARAMS.mAutoConfigTest=true;check(!PortCeNetOffered(),"autoconfig does not offer networking");CLIPARAMS.mAutoConfigTest=false;
    for (const char *name:blocked) {
        setenv(name,"authored",1);char label[160];std::snprintf(label,sizeof label,"configured %s hides the network entry",name);
        check(!PortCeNetOffered(),label);setenv(name,"",1);
        std::snprintf(label,sizeof label,"empty %s does not hide the network entry",name);
        check(PortCeNetOffered(),label);unsetenv(name);
    }
    setenv("BEA_USER_DIR","private-fixture",1);setenv("BEA_GAME_DIR","private-fixture",1);
    char why[256];check(CE_LobbyAvailable(why,sizeof why),"ordinary frontend can ask whether the lobby is available");
    for (const char *name:{"BEA_LOCKSTEP_HOST","BEA_LOCKSTEP_JOIN"}) {
        setenv(name,"authored",1);char label[160];std::snprintf(label,sizeof label,"configured %s refuses direct lobby admission",name);
        check(!CE_LobbyAvailable(why,sizeof why)&&why[0],label);unsetenv(name);
    }
    unsetenv("BEA_USER_DIR");check(!CE_LobbyAvailable(why,sizeof why)&&why[0],"lobby still requires a selected user folder");
    const char *scrubbed[]={"BEA_PORT_FAKE_KEYS","BEA_PORT_FAKE_CLICK","BEA_PORT_SHOT","BEA_PORT_QUIT",
        "BEA_NET_LOBBY_CODE_OUT","BEA_NET_HOST","BEA_NET_JOIN","BEA_NET_SECRET","BEA_NET_COMPAT",
        "BEA_LOCKSTEP_HOST","BEA_LOCKSTEP_JOIN","BEA_LOCKSTEP_SECRET","BEA_LOCKSTEP_DELAY","BEA_LOCKSTEP_HASH_EVERY",
        "BEA_LOCKSTEP_SKEW_AT","BEA_LOCKSTEP_DUMP","BEA_LOCKSTEP_QUIT_AT","BEA_LOCKSTEP_STALL_EVERY","BEA_LOCKSTEP_STALL_AT",
        "BEA_LOCKSTEP_STALL_MS","BEA_LOCKSTEP_NO_LOSS","BEA_LOCKSTEP_NO_PARTICLES","BEA_LOCKSTEP_EXTRA_RENDERS",
        "BEA_LOCKSTEP_STALL_FROM","BEA_LOCKSTEP_FLIP_CW_AT","BEA_LOCKSTEP_NO_CW_FENCE","BEA_LOCKSTEP_POISON_STACK",
        "BEA_LOCKSTEP_TEST_UNINIT_READ"};
    for (const char *name:scrubbed) setenv(name,"authored",1);
    setenv("BEA_KEEP_TEST","unchanged",1);setenv("BEA_USER_DIR","private-fixture",1);setenv("BEA_NET_HANDOFF","old",1);
    std::snprintf(gHandoffName,sizeof gHandoffName,"authored-name");
    std::snprintf(gHandoffReceipt,sizeof gHandoffReceipt,"authored-owner");
    ScrubEnvironment("launch");
    for (const char *name:scrubbed) { char label[128];std::snprintf(label,sizeof label,"restart removes %s",name);check(!getenv(name),label); }
    check(getenv("BEA_KEEP_TEST")&&!std::strcmp(getenv("BEA_KEEP_TEST"),"unchanged"),"restart preserves unrelated variable");
    check(getenv("BEA_USER_DIR")&&!std::strcmp(getenv("BEA_USER_DIR"),"private-fixture"),"restart preserves selected user folder");
    check(getenv("BEA_NET_HANDOFF")&&!std::strcmp(getenv("BEA_NET_HANDOFF"),"launch"),"restart sets its requested handoff kind");
    check(getenv("BEA_NET_HANDOFF_FILE")&&!std::strcmp(getenv("BEA_NET_HANDOFF_FILE"),"authored-name"),"restart publishes the pending handoff name");
    check(getenv("BEA_NET_HANDOFF_OWNER")&&!std::strcmp(getenv("BEA_NET_HANDOFF_OWNER"),"authored-owner"),"restart publishes the pending handoff owner");
    ScrubEnvironment(nullptr);check(!getenv("BEA_NET_HANDOFF"),"null handoff removes the previous handoff kind");
    check(PortLobbyShellExit(27,1)==27&&gShellExited&&gShellCode==27&&gShellNativeQuit==1,"shell wrapper preserves return code and records the native quit");
    check(PortLobbyLevelQuit(13,99)==13&&!gLevelToSystem,"level wrapper preserves ordinary quit");
    check(PortLobbyLevelQuit(99,99)==99&&gLevelToSystem,"level wrapper preserves system quit");
    gRestart=RESTART_NONE;gLaunched=false;gShellExited=false;gShellNativeQuit=0;gLevelToSystem=false;
    CE_LobbyAfterExit();check(!calls_to_exec&&!calls_to_return,"ordinary single-player exit does not restart or hand off");
    return failures?1:0;
}
