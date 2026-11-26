// SPDX-License-Identifier: GPL-3.0-only
// Typed game boundary for two-player lockstep co-op inputs, views and tick state fences.
#ifndef CE_NETWORK_WORLD_H
#define CE_NETWORK_WORLD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CE_WORLD_INPUT_SCHEMA 5u
#define CE_WORLD_MAX_ACTIONS 32u
enum CE_WorldActionSource { CE_WORLD_ACTION_BINDING = 0, CE_WORLD_ACTION_MOUSE_AXIS = 1 };
typedef struct CE_WorldAction { uint32_t button, source; float value; } CE_WorldAction;
typedef struct CE_WorldInput {
    uint32_t epoch, sequence, tick, player, count, last_applied_birth_serial;
    uint32_t invert_ground, invert_jet, mouse_ground_aim, walker_mouse_any;
    // Scaled angular deltas in radians, BEFORE inversion. Each peer applies the player's explicit preferences once.
    float ground_yaw, ground_pitch;
    CE_WorldAction actions[CE_WORLD_MAX_ACTIONS];
} CE_WorldInput;

int CE_WorldActive(void);
int CE_WorldCanRender(void);
uint32_t CE_WorldLocalPlayer(void);

// Mirror hooks: all return their original/offline behaviour while no co-op session is active.
int CE_WorldControllerFlush(uint32_t player); // returns 1 when the lockstep session handled/suppressed this controller
int CE_WorldViewPlayer(int original_player);
int CE_WorldLocalWon(int game_state);
float CE_WorldViewAspect(float original_aspect);
int CE_WorldInvertPitch(uint32_t player, int grounded, int original);
int CE_WorldWalkerBindingType(uint32_t player, int original_type);
int CE_WorldMouseGroundAim(uint32_t player, int original);
int CE_WorldLockstepValidInput(const CE_WorldInput *input);
void CE_WorldMouseAxis(int on);
// Clock and automation policy checked by admitted co-op starts: NULL when ordinary, else a fixed local reason.
// Call only after the platform owners have seen a Present (CE_NetAutomationIdle answers "not initialized" before that).
const char *CE_NetAutomationRefusal(void);
#ifdef __cplusplus
}
class CBattleEngine;
class CCamera;
class CController;
class CPlayer;
CCamera *CE_WorldViewCamera(int original_player);
CPlayer *CE_WorldViewPlayerPtr(int original_player);
int CE_WorldViewShowHUD(int original_player, CPlayer *player);
int CE_WorldBypassMouse(CBattleEngine *);
int CE_WorldCaptureAction(CController *, int button, float value);
void CE_WorldLifecycleDestroyed(const void *);
#endif
#endif
