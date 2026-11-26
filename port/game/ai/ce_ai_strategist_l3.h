// SPDX-License-Identifier: GPL-3.0-only
// port/game/ai/ce_ai_strategist_l3.h: Optional host-only Layer-3 Strategic AI advisor.
// Compiled strictly as host port code (HOST_BUFFER_GAME_MODULES / pcxx) — ZERO common.h,
// ZERO MEM_MANAGER, ZERO CRT rand(), ZERO weights bundled in base release packages.
// Disabled in online lockstep v1; returns nullptr gracefully when optional user weights are absent.
#ifndef PORT_CE_AI_STRATEGIST_L3_H
#define PORT_CE_AI_STRATEGIST_L3_H

#include <stdint.h>
#include "ce_player_hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wait-free publish from SIM player_act (offline only; no-op when optional external weights are absent).
void PortCeAiStrategistPublishObs(uint8_t slot, const CePlayerObs *obs);

// Wait-free poll of the latest strategic prior for `slot` (returns NULL when absent or not ready).
const CeStrategicPrior *PortCeAiStrategistPoll(uint8_t slot);

// Clears per-match priors and joins the background worker cleanly on level end / reset.
void PortCeAiStrategistLevelReset(void);

// Explicit shutdown hook (safe to call repeatedly).
void PortCeAiStrategistShutdown(void);

// Sanitizes arbitrary untrusted text into printable ASCII (0x20..0x7E, <= 160 chars + NUL).
void CeAiStrategistSanitizeHint(const char *src, char out[164]);

#ifdef __cplusplus
}
#endif

#endif
