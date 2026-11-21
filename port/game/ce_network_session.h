// SPDX-License-Identifier: GPL-3.0-only
// Game-loop session bridge for two-player lockstep co-op.
#ifndef CE_NETWORK_SESSION_H
#define CE_NETWORK_SESSION_H
#include <stdint.h>
#include "ce_network_lockstep.h"
#ifdef __cplusplus
extern "C" {
#endif
int CE_SessionConfigureLevel(uint32_t level);
int CE_SessionAfterLoad(void);
int CE_SessionPreparePlay(void); // calls genuine PreRun offline and after lockstep start barriers
int CE_SessionBeforeUpdate(void);
void CE_SessionAfterUpdate(void); // MainLoop, after its one Update (lockstep co-op ends the tick here)
void CE_SessionBeforeRender(void);
int CE_SessionBeginTeardown(int original_quit);
void CE_SessionAfterWorldShutdown(void);
const char *CE_SessionError(void);
// The current player-facing connection notice ("Waiting for your friend... N", short enough for the game screen), or "".
const char *CE_SessionNotice(void);
int CE_SessionActive(void);
// Retained terminal network outcome after world retirement; ordinary/offline exits keep their code. A failed
// lockstep outcome cannot be converted to success by a concurrent native WM_QUIT with code zero.
int CE_SessionExitCode(int native_code, int native_quit_seen);
// Window service never applies/captures a world record, or runs a controller/event/render callback.
// Return EXIT only to let the genuine caller consume its already-set quit code; never synthesize WM_QUIT.
enum CE_SessionSystemResult { CE_SESSION_NATIVE = 0, CE_SESSION_WAIT = 1, CE_SESSION_EXIT = 2 };
enum CE_SessionWindowReason { CE_SESSION_UNFOCUSED = 1, CE_SESSION_MINIMIZED = 2,
    CE_SESSION_HIDDEN = 4, CE_SESSION_NO_WINDOW = 8 };
uint32_t CE_SessionWindowStatus(void); // owned SDL window only, no process/window enumeration
int CE_SessionSystemService(int native_ready, int device_state); // device_state: 0 unknown, 1 ready, 2 unavailable

// Fixed bridge implemented beside the game's genuine callers by the generated-mirror patch.
void CE_SessionGamePreRun(void);
int CE_SessionGamePump(void);
void CE_SessionGameIdle(void);
int CE_SessionGameQuitCode(void);
void CE_SessionGameSetQuit(int quit);
uint32_t CE_SessionGameTick(void);
void CE_SessionGameRebaseRenderClock(void);
int CE_SessionGameAbortLoad(void);
// The co-op start's waiting screen (ce_netpages.cpp): one frame of it while a start barrier waits, at most 30 a second.
// FRIEND: the other player is not connected yet; STARTING: connected, the start is being agreed (both offer Leave, and
// return 1 once when the player chose it); PRERUN: one frame without Leave, drawn before this game's own pre-run;
// END: the wait is over (the screen's key trap is removed).
// PREPARE: before the level loads, the lobby's kept frame is read for the screen's background.
enum CE_SessionWaitMode { CE_WAIT_END = 0, CE_WAIT_FRIEND = 1, CE_WAIT_STARTING = 2, CE_WAIT_PRERUN = 3, CE_WAIT_PREPARE = 4 };
int CE_SessionGameWaitFrame(int mode);
#ifdef __cplusplus
}
#endif
#endif
