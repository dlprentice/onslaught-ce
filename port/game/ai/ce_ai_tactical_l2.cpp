// SPDX-License-Identifier: GPL-3.0-only
// port/game/ai/ce_ai_tactical_l2.cpp: Toggle-gated Layer-2 deterministic tactical commander & possession AI.
// Zero common.h dependency: pure integer & basic float deterministic decisions over CePlayerObs.
#include "ai/ce_ai_tactical_l2.h"
#include "ce_policy.h"

#include <string.h>

void CeAiTacticalL2Reset(CeAiTacticalL2State *state)
{
	if (!state) return;
	memset(state, 0, sizeof(*state));
}

void CeAiTacticalL2Decide(CeAiTacticalL2State *state,
                          const CePlayerObs &obs,
                          const CeStrategicPrior *prior,
                          CeCommanderIntent *outCmd,
                          CePossessIntent *outPossess)
{
	if (outCmd) memset(outCmd, 0, sizeof(*outCmd));
	if (outPossess) memset(outPossess, 0, sizeof(*outPossess));
	if (!state) return;

	const bool cmdOn = (obs.activeToggles & CE_TOGGLE_COMMANDER) != 0u;
	const bool possessOn = (obs.activeToggles & CE_TOGGLE_PLAY_UNIT) != 0u;
	if (!cmdOn && !possessOn)
		return; // Strictly 0 CPU when both toggles are OFF.

	// Find highest-priority visible hostile anchor (closest hostile battle engine or base building, else closest hostile).
	bool hasTarget = false;
	float tgtX = obs.pos[0];
	float tgtY = obs.pos[1];
	float tgtZ = obs.pos[2];
	uint32_t bestDist = 0xFFFFFFFFu;
	for (uint32_t i = 0u; i < obs.entityCount; ++i)
	{
		const CePlayerEntityObs &e = obs.entities[i];
		if (e.isFriendly) continue;
		uint32_t key = e.distSqQuantized;
		if (e.isBaseBuilding || e.isBattleEngine)
			key = key / 2u;
		if (!hasTarget || key < bestDist)
		{
			hasTarget = true;
			bestDist = key;
			tgtX = e.pos[0];
			tgtY = e.pos[1];
			tgtZ = e.pos[2];
		}
	}

	if (prior && prior->hasFocusPoint)
	{
		tgtX = prior->focusX;
		tgtY = prior->focusY;
		hasTarget = true;
	}

	// ---- Commander Squad Orders (1.3 Hz cadence, staggered per slot) ----
	if (cmdOn && outCmd && obs.squadCount > 0u &&
	    (obs.simTick >= state->lastCmdTick + 15u) && ((obs.simTick + obs.slot * 3u) % 15u == 0u))
	{
		const bool defendPosture = (prior && (prior->posture == CE_STRAT_DEFEND || prior->posture == CE_STRAT_REPAIR));
		for (uint32_t i = 0u; i < obs.squadCount; ++i)
		{
			const CePlayerSquadObs &sq = obs.squads[i];
			if (!sq.friendly || !sq.commandable || sq.members <= 0 || sq.squadHandle == kCeEntityHandleInvalid)
				continue;
			// Low-health squad falls back to preserve forces.
			if (sq.lifeRatio < 0.30f && sq.verb != 4 /* CE_CMD_FALLBACK */)
			{
				outCmd->valid = 1u;
				outCmd->verb = 4; // CE_CMD_FALLBACK
				outCmd->squadHandle = sq.squadHandle;
				outCmd->goalX = 0.0f;
				outCmd->goalY = 0.0f;
				state->lastCmdTick = obs.simTick;
				break;
			}
			// Artillery holds position.
			if (sq.deploys)
			{
				if (sq.verb != 3 /* CE_CMD_HOLD */)
				{
					outCmd->valid = 1u;
					outCmd->verb = 3; // CE_CMD_HOLD
					outCmd->squadHandle = sq.squadHandle;
					outCmd->goalX = sq.x;
					outCmd->goalY = sq.y;
					state->lastCmdTick = obs.simTick;
					break;
				}
				continue;
			}
			// Idle or outdated march order advances toward target / strategic focus.
			if (hasTarget && (sq.verb == 0 /* CE_CMD_NONE */ || defendPosture))
			{
				outCmd->valid = 1u;
				outCmd->verb = defendPosture ? 3 /* CE_CMD_HOLD */ : 2 /* CE_CMD_ADVANCE */;
				outCmd->squadHandle = sq.squadHandle;
				outCmd->goalX = defendPosture ? sq.x : tgtX;
				outCmd->goalY = defendPosture ? sq.y : tgtY;
				state->lastCmdTick = obs.simTick;
				break;
			}
		}
	}

	// ---- Headless Unit Possession (1 Hz cadence, only when Battle Engine is safe/healthy) ----
	if (possessOn && outPossess && (obs.simTick >= state->lastPossessTick + 20u))
	{
		if (state->possessedUnit != kCeEntityHandleInvalid)
		{
			if (obs.lifeRatio < 0.45f || !hasTarget)
			{
				outPossess->valid = 1u;
				outPossess->release = 1u;
				outPossess->unitHandle = state->possessedUnit;
				state->possessedUnit = kCeEntityHandleInvalid;
				state->lastPossessTick = obs.simTick;
			}
			else
			{
				outPossess->valid = 1u;
				outPossess->release = 0u;
				outPossess->unitHandle = state->possessedUnit;
				outPossess->goalX = tgtX;
				outPossess->goalY = tgtY;
				outPossess->fire = 1u;
				outPossess->aimX = tgtX;
				outPossess->aimY = tgtY;
				outPossess->aimZ = tgtZ;
				state->lastPossessTick = obs.simTick;
			}
		}
		else if (hasTarget && obs.lifeRatio >= 0.75f && obs.takeableCount > 0u)
		{
			// Pick first healthy tank/ground candidate in deterministic spawnSeq order.
			const CePlayerTakeableObs *best = nullptr;
			for (uint32_t i = 0u; i < obs.takeableCount; ++i)
			{
				const CePlayerTakeableObs &cand = obs.takeable[i];
				if (cand.unitHandle == kCeEntityHandleInvalid || cand.lifeRatio < 0.50f)
					continue;
				if (!best || cand.spawnSeq < best->spawnSeq)
					best = &cand;
			}
			if (best)
			{
				outPossess->valid = 1u;
				outPossess->release = 0u;
				outPossess->unitHandle = best->unitHandle;
				outPossess->goalX = tgtX;
				outPossess->goalY = tgtY;
				outPossess->fire = 1u;
				outPossess->aimX = tgtX;
				outPossess->aimY = tgtY;
				outPossess->aimZ = tgtZ;
				state->possessedUnit = best->unitHandle;
				state->lastPossessTick = obs.simTick;
			}
		}
	}
}
