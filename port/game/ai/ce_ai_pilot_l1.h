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

	out[0]  = CeAiQuantizeQ12(obs.lifeRatio * 2.0f - 1.0f);
	out[1]  = CeAiQuantizeQ12(obs.energyRatio * 2.0f - 1.0f);
	out[2]  = obs.isJet ? 4096 : -4096;
	out[3]  = obs.isOnGround ? 4096 : -4096;
	out[4]  = CeAiQuantizeQ12(obs.altitude * (1.0f / 40.0f));
	out[5]  = (obs.dryMask & CE_DRY_HERE) ? 4096 : -4096;
	out[6]  = (obs.dryMask & CE_DRY_FWD) ? 4096 : -4096;
	out[7]  = (obs.dryMask & CE_DRY_BACK) ? 4096 : -4096;
	out[8]  = (obs.dryMask & CE_DRY_LEFT) ? 4096 : -4096;
	out[9]  = (obs.dryMask & CE_DRY_RIGHT) ? 4096 : -4096;
	out[10] = (obs.dryMask & CE_DRY_AHEAD_28) ? 4096 : -4096;
	out[11] = obs.weaponReady ? 4096 : -4096;
	out[12] = obs.weaponOverheated ? 4096 : -4096;
	out[13] = CeAiQuantizeQ12(obs.weaponHeatOrAmmo * 2.0f - 1.0f);
	out[14] = CeAiQuantizeQ12(obs.pitch * (1.0f / 1.5707963f));
	out[15] = CeAiQuantizeQ12((obs.pos[0] - 256.0f) * (1.0f / 256.0f));
	out[16] = CeAiQuantizeQ12((obs.pos[1] - 256.0f) * (1.0f / 256.0f));

	// Scan sorted entities (strictly ordered by (distSqQuantized, spawnSeq)) for nearest hostile & friendly stats.
	int hostileCount = 0;
	int friendlyCount = 0;
	bool foundHostile = false;
	for (uint32_t i = 0u; i < obs.entityCount && i < static_cast<uint32_t>(CE_PLAYER_OBS_MAX_ENTITIES); ++i)
	{
		const CePlayerEntityObs &e = obs.entities[i];
		if (e.isFriendly)
		{
			++friendlyCount;
			if (e.isRepairPad && out[25] == 0)
			{
				const float rdx = (e.pos[0] - obs.pos[0]) * (1.0f / 256.0f);
				const float rdy = (e.pos[1] - obs.pos[1]) * (1.0f / 256.0f);
				out[25] = CeAiQuantizeQ12(rdx);
				out[26] = CeAiQuantizeQ12(rdy);
			}
			continue;
		}
		++hostileCount;
		if (!foundHostile)
		{
			foundHostile = true;
			out[17] = 4096;
			out[18] = CeAiQuantizeQ12((e.pos[0] - obs.pos[0]) * (1.0f / 180.0f));
			out[19] = CeAiQuantizeQ12((e.pos[1] - obs.pos[1]) * (1.0f / 180.0f));
			out[20] = CeAiQuantizeQ12((e.pos[2] - obs.pos[2]) * (1.0f / 60.0f));
			out[21] = CeAiQuantizeQ12(e.lifeRatio * 2.0f - 1.0f);
			out[22] = e.airborne ? 4096 : -4096;
			out[23] = e.isBattleEngine ? 4096 : -4096;
			out[24] = CeAiQuantizeQ12((static_cast<float>(e.targetClass) - 3.0f) * 0.25f);
		}
	}
	out[27] = CeAiQuantizeQ12(static_cast<float>(hostileCount - friendlyCount) * 0.125f);
	out[28] = (obs.activeToggles & (1u << 6)) ? 4096 : -4096; // CE_TOGGLE_FLIGHT

	if (prior)
	{
		out[29] = CeAiQuantizeQ12((static_cast<float>(prior->aggressionQ8) - 128.0f) * (1.0f / 128.0f));
		out[30] = CeAiQuantizeQ12((static_cast<float>(prior->airBiasQ8) - 128.0f) * (1.0f / 128.0f));
		out[31] = CeAiQuantizeQ12((static_cast<float>(prior->posture) - 2.0f) * 0.5f);
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
