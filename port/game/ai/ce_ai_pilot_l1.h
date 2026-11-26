// SPDX-License-Identifier: GPL-3.0-only
// port/game/ai/ce_ai_pilot_l1.h: Extracted Layer-1 deterministic Battle Engine combat pilot.
// Drives Walker & Jet flight, velocity-led aiming, weapon selection, fire gating, survival retreat,
// and dry-ground low-altitude landing via ce_multibe_ai.h guardrails + (Phase 4) compact INT8 MLP.
#ifndef PORT_CE_AI_PILOT_L1_H
#define PORT_CE_AI_PILOT_L1_H

#include <stdint.h>
#include "ce_player_hooks.h"
#include "ai/ce_ai_pilot_weights.h"

class CBattleEngine;
class CCamera;

// Canonical feature and output indices shared with port/tools/gen_ai_pilot_weights.py.
enum CeAiMlpFeatIdx : int
{
	CE_AI_FEAT_LIFE_RATIO        = 0,
	CE_AI_FEAT_ENERGY_RATIO      = 1,
	CE_AI_FEAT_IS_JET            = 2,
	CE_AI_FEAT_IS_ON_GROUND      = 3,
	CE_AI_FEAT_ALTITUDE          = 4,
	CE_AI_FEAT_DRY_HERE          = 5,
	CE_AI_FEAT_DRY_FWD           = 6,
	CE_AI_FEAT_DRY_BACK          = 7,
	CE_AI_FEAT_DRY_LEFT          = 8,
	CE_AI_FEAT_DRY_RIGHT         = 9,
	CE_AI_FEAT_DRY_AHEAD_28      = 10,
	CE_AI_FEAT_WEAPON_READY      = 11,
	CE_AI_FEAT_WEAPON_OVERHEATED = 12,
	CE_AI_FEAT_WEAPON_HEAT_AMMO  = 13,
	CE_AI_FEAT_PITCH             = 14,
	CE_AI_FEAT_POS_X             = 15,
	CE_AI_FEAT_POS_Y             = 16,
	CE_AI_FEAT_HAS_HOSTILE       = 17,
	CE_AI_FEAT_HOSTILE_DX        = 18,
	CE_AI_FEAT_HOSTILE_DY        = 19,
	CE_AI_FEAT_HOSTILE_DZ        = 20,
	CE_AI_FEAT_HOSTILE_LIFE      = 21,
	CE_AI_FEAT_HOSTILE_AIRBORNE  = 22,
	CE_AI_FEAT_HOSTILE_IS_BE     = 23,
	CE_AI_FEAT_HOSTILE_CLASS     = 24,
	CE_AI_FEAT_REPAIR_PAD_DX     = 25,
	CE_AI_FEAT_REPAIR_PAD_DY     = 26,
	CE_AI_FEAT_FORCE_BALANCE     = 27,
	CE_AI_FEAT_FLIGHT_ALLOWED    = 28,
	CE_AI_FEAT_PRIOR_AGGRESSION  = 29,
	CE_AI_FEAT_PRIOR_AIR_BIAS    = 30,
	CE_AI_FEAT_PRIOR_POSTURE     = 31,
	CE_AI_FEAT_COUNT             = 32
};

enum CeAiMlpOutIdx : int
{
	CE_AI_OUT_STRAFE_TRIM     = 0,
	CE_AI_OUT_FWD_TRIM        = 1,
	CE_AI_OUT_JET_TURN_TRIM   = 2,
	CE_AI_OUT_JET_THRUST_TRIM = 3,
	CE_AI_OUT_JET_CRUISE_BIAS = 4,
	CE_AI_OUT_RESERVED_5      = 5,
	CE_AI_OUT_RESERVED_6      = 6,
	CE_AI_OUT_RESERVED_7      = 7,
	CE_AI_OUT_COUNT           = 8
};

static_assert(CE_AI_FEAT_COUNT == kCeAiMlpInDim, "CeAiMlpFeatIdx must match kCeAiMlpInDim");
static_assert(CE_AI_OUT_COUNT == kCeAiMlpOutDim, "CeAiMlpOutIdx must match kCeAiMlpOutDim");

// Deterministic Q12 [-4096, +4096] quantization helper (zero libm, identical on Linux & Windows).
inline int16_t CeAiQuantizeQ12(float v)
{
	if (!(v == v)) return 0; // NaN -> 0
	if (v <= -1.0f) return -4096;
	if (v >= 1.0f)  return 4096;
	const float scaled = v * 4096.0f;
	return static_cast<int16_t>(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
}

// Packs a read-only CePlayerObs + optional CeStrategicPrior into 32 Q12 features for the INT8 Layer-1 MLP.
// Strictly zero libm, zero raw pointer dependence, zero worldEpoch dependence.
inline void CeAiPilotExtractFeaturesQ12(const CePlayerObs &obs, const CeStrategicPrior *prior,
                                        int16_t out[kCeAiMlpInDim])
{
	for (int i = 0; i < kCeAiMlpInDim; ++i)
		out[i] = 0;

	out[CE_AI_FEAT_LIFE_RATIO]        = CeAiQuantizeQ12(obs.lifeRatio * 2.0f - 1.0f);
	out[CE_AI_FEAT_ENERGY_RATIO]      = CeAiQuantizeQ12(obs.energyRatio * 2.0f - 1.0f);
	out[CE_AI_FEAT_IS_JET]            = obs.isJet ? 4096 : -4096;
	out[CE_AI_FEAT_IS_ON_GROUND]      = obs.isOnGround ? 4096 : -4096;
	out[CE_AI_FEAT_ALTITUDE]          = CeAiQuantizeQ12(obs.altitude * (1.0f / 40.0f));
	out[CE_AI_FEAT_DRY_HERE]          = (obs.dryMask & CE_DRY_HERE) ? 4096 : -4096;
	out[CE_AI_FEAT_DRY_FWD]           = (obs.dryMask & CE_DRY_FWD) ? 4096 : -4096;
	out[CE_AI_FEAT_DRY_BACK]          = (obs.dryMask & CE_DRY_BACK) ? 4096 : -4096;
	out[CE_AI_FEAT_DRY_LEFT]          = (obs.dryMask & CE_DRY_LEFT) ? 4096 : -4096;
	out[CE_AI_FEAT_DRY_RIGHT]         = (obs.dryMask & CE_DRY_RIGHT) ? 4096 : -4096;
	out[CE_AI_FEAT_DRY_AHEAD_28]      = (obs.dryMask & CE_DRY_AHEAD_28) ? 4096 : -4096;
	out[CE_AI_FEAT_WEAPON_READY]      = obs.weaponReady ? 4096 : -4096;
	out[CE_AI_FEAT_WEAPON_OVERHEATED] = obs.weaponOverheated ? 4096 : -4096;
	out[CE_AI_FEAT_WEAPON_HEAT_AMMO]  = CeAiQuantizeQ12(obs.weaponHeatOrAmmo * 2.0f - 1.0f);
	out[CE_AI_FEAT_PITCH]             = CeAiQuantizeQ12(obs.pitch * (1.0f / 1.5707963f));
	out[CE_AI_FEAT_POS_X]             = CeAiQuantizeQ12((obs.pos[0] - 256.0f) * (1.0f / 256.0f));
	out[CE_AI_FEAT_POS_Y]             = CeAiQuantizeQ12((obs.pos[1] - 256.0f) * (1.0f / 256.0f));

	// Scan sorted entities (strictly ordered by (distSqQuantized, spawnSeq)) for nearest hostile & friendly stats.
	int hostileCount = 0;
	int friendlyCount = 0;
	bool foundHostile = false;
	bool foundRepairPad = false;
	for (uint32_t i = 0u; i < obs.entityCount && i < static_cast<uint32_t>(CE_PLAYER_OBS_MAX_ENTITIES); ++i)
	{
		const CePlayerEntityObs &e = obs.entities[i];
		if (e.isFriendly)
		{
			++friendlyCount;
			if (e.isRepairPad && !foundRepairPad)
			{
				foundRepairPad = true;
				const float rdx = (e.pos[0] - obs.pos[0]) * (1.0f / 256.0f);
				const float rdy = (e.pos[1] - obs.pos[1]) * (1.0f / 256.0f);
				out[CE_AI_FEAT_REPAIR_PAD_DX] = CeAiQuantizeQ12(rdx);
				out[CE_AI_FEAT_REPAIR_PAD_DY] = CeAiQuantizeQ12(rdy);
			}
			continue;
		}
		++hostileCount;
		if (!foundHostile)
		{
			foundHostile = true;
			out[CE_AI_FEAT_HAS_HOSTILE]      = 4096;
			out[CE_AI_FEAT_HOSTILE_DX]       = CeAiQuantizeQ12((e.pos[0] - obs.pos[0]) * (1.0f / 180.0f));
			out[CE_AI_FEAT_HOSTILE_DY]       = CeAiQuantizeQ12((e.pos[1] - obs.pos[1]) * (1.0f / 180.0f));
			out[CE_AI_FEAT_HOSTILE_DZ]       = CeAiQuantizeQ12((e.pos[2] - obs.pos[2]) * (1.0f / 60.0f));
			out[CE_AI_FEAT_HOSTILE_LIFE]     = CeAiQuantizeQ12(e.lifeRatio * 2.0f - 1.0f);
			out[CE_AI_FEAT_HOSTILE_AIRBORNE] = e.airborne ? 4096 : -4096;
			out[CE_AI_FEAT_HOSTILE_IS_BE]    = e.isBattleEngine ? 4096 : -4096;
			out[CE_AI_FEAT_HOSTILE_CLASS]    = CeAiQuantizeQ12((static_cast<float>(e.targetClass) - 3.0f) * 0.25f);
		}
	}
	out[CE_AI_FEAT_FORCE_BALANCE]  = CeAiQuantizeQ12(static_cast<float>(hostileCount - friendlyCount) * 0.125f);
	out[CE_AI_FEAT_FLIGHT_ALLOWED] = (obs.activeToggles & (1u << 6)) ? 4096 : -4096; // CE_TOGGLE_FLIGHT

	if (prior)
	{
		out[CE_AI_FEAT_PRIOR_AGGRESSION] = CeAiQuantizeQ12((static_cast<float>(prior->aggressionQ8) - 128.0f) * (1.0f / 128.0f));
		out[CE_AI_FEAT_PRIOR_AIR_BIAS]   = CeAiQuantizeQ12((static_cast<float>(prior->airBiasQ8) - 128.0f) * (1.0f / 128.0f));
		out[CE_AI_FEAT_PRIOR_POSTURE]    = CeAiQuantizeQ12((static_cast<float>(prior->posture) - 2.0f) * 0.5f);
	}
}

// Resets Layer-1 pilot brain state for one slot (or all slots when slot < 0).
void CeAiPilotL1Reset(int slot, float initialLife = 0.0f, uint32_t matchSeed = 0u, bool onlineSeeded = false);

// Applies a sanitized CePilotInput directly to an AI-controlled Battle Engine slot
// (bypasses human tan() mouse curves and Y-invert; honors CE_TOGGLE_FLIGHT).
void CePilotApplySlot(int slot, const CePilotInput &in, uint32_t activeToggles);

// Engine-integrated Layer-1 step invoked per AI-controlled Battle Engine slot inside player_act.
void CeAiPilotL1TickSlot(int slot, const CePlayerObs &obs, const CeStrategicPrior *prior);

// Narrow host bridge implemented in port/game/ce_multibe.cpp (roster, cameras & telemetry counters).
bool           CeMultiBeAiHostActive();
int            CeMultiBeAiHostSlotCount();
CBattleEngine *CeMultiBeAiHostSlotEngine(int slot);
bool           CeMultiBeAiHostSlotIsAi(int slot);
bool           CeMultiBeAiHostSlotIsAux(int slot);
CCamera       *CeMultiBeAiHostSlotAuxCamera(int slot);
bool           CeMultiBeAiHostSlotStartPos(int slot, float out[3]);
bool           CeMultiBeAiHostIsSoloSkirmish();
bool           CeMultiBeAiHostIsPanoramicCam();
bool           CeMultiBeAiHostIsOnlineMatch();
uint32_t       CeMultiBeAiHostMatchSeed();
int            CeMultiBeAiHostSimTicks();
void           CeMultiBeAiHostNoteWeaponPick();
void           CeMultiBeAiHostNoteEvade();
void           CeMultiBeAiHostNoteRetreat();
void           CeMultiBeAiHostNoteDash();
void           CeMultiBeAiHostNoteMorph(bool wasJet, float altitudeAboveGround);

#endif
