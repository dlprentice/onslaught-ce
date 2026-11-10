// SPDX-License-Identifier: GPL-3.0-only
// Explicit engineering admission for the bounded two-peer level-852 route. No menu/discovery or campaign support.
#ifndef CE_NETWORK_SESSION_H
#define CE_NETWORK_SESSION_H
#include <stdint.h>
#include "ce_network_lockstep.h"
#ifdef __cplusplus
extern "C" {
#endif
int CE_SessionConfigureLevel(uint32_t level);
int CE_SessionAfterLoad(void);
int CE_SessionPreparePlay(void); // calls genuine PreRun offline and on the host; never on a replica
// Schema-5 controls fence CSD1 STATE application behind its admitted lifecycle prefix and exact whole-Apply ACKs.
// Repairs use an acknowledged quiet shared-WAIT transaction, a READY receipt and a separate reliable raw image.
// Lifecycle Send0 retains the exact front; neither lifecycle nor state bases retire merely because time elapsed.
int CE_SessionBeforeUpdate(void);
void CE_SessionAfterFlush(void);
void CE_SessionAfterUpdate(void); // MainLoop, after its one Update (lockstep co-op ends the tick here)
void CE_SessionBeforeRender(void);
int CE_SessionBeginTeardown(int original_quit);
void CE_SessionAfterWorldShutdown(void);
const char *CE_SessionError(void);
// The current player-facing connection notice ("Connection lost, waiting... (N s)", short enough for the game screen), or "".
const char *CE_SessionNotice(void);
int CE_SessionActive(void);
// Validated local JOIN role, from before world load through native world destruction; never runs admission.
int CE_SessionMissionVMBlocked(void);
// Retained terminal network outcome after world retirement; ordinary/offline exits keep their code. A failed
// lockstep outcome cannot be converted to success by a concurrent native WM_QUIT with code zero.
int CE_SessionExitCode(int native_code, int native_quit_seen);
// Window service never applies/captures a world or lifecycle record, or runs a controller/event/render callback.
// Both peers report local unavailability; the host's acknowledged shared wait owns release of simulation.
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
#ifdef __cplusplus
}
#endif
#endif
