// SPDX-License-Identifier: GPL-3.0-only
// port/game/ai/ce_ai_tactical_l2.h: Toggle-gated Layer-2 deterministic tactical commander & possession AI.
// Strictly dormant (0 CPU) whenever neither CE_TOGGLE_COMMANDER nor CE_TOGGLE_PLAY_UNIT is active.
#ifndef PORT_CE_AI_TACTICAL_L2_H
#define PORT_CE_AI_TACTICAL_L2_H

#include "ce_player_hooks.h"

struct CeAiTacticalL2State
{
	uint32_t		lastCmdTick;
	uint32_t		lastPossessTick;
	CeEntityHandle	possessedUnit;
};

void CeAiTacticalL2Reset(CeAiTacticalL2State *state);

// Pure deterministic Layer-2 step (runs at 1-2 Hz cadence; zero RNG, zero libm, zero heap allocation).
void CeAiTacticalL2Decide(CeAiTacticalL2State *state,
                          const CePlayerObs &obs,
                          const CeStrategicPrior *prior,
                          CeCommanderIntent *outCmd,
                          CePossessIntent *outPossess);

#endif
