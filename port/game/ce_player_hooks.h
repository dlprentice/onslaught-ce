// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_player_hooks.h: Network-neutral player observation & AI action surface.
// Strictly zero common.h and zero ce_network_*.h dependencies so both CE_NETWORK_OFF public
// builds, host ASan/UBSan tests, and host-only Layer-3 modules compile against this header directly.
#ifndef PORT_CE_PLAYER_HOOKS_H
#define PORT_CE_PLAYER_HOOKS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ce_entity_handles.h"

static constexpr int CE_PLAYER_MAX_SLOTS = 4;
static constexpr int CE_PLAYER_OBS_MAX_ENTITIES = 64;
static constexpr int CE_PLAYER_OBS_MAX_SQUADS = 32;
static constexpr int CE_PLAYER_OBS_MAX_TAKEABLE = 16;

// Portable SplitMix64 deterministic AI PRNG (zero CRT rand(), bit-identical across Linux & Windows).
inline uint32_t CePlayerAiSeed(uint32_t matchSeed, uint8_t slot, uint32_t tick)
{
	uint64_t x = (uint64_t(matchSeed) << 32) ^
	             (uint64_t(tick) * 0x9E3779B97F4A7C15ULL) ^
	             (uint64_t(slot + 1u) * 0xBF58476D1CE4E5B9ULL) ^
	             0x4345504C41594552ULL;
	x ^= x >> 30;
	x *= 0xBF58476D1CE4E5B9ULL;
	x ^= x >> 27;
	x *= 0x94D049BB133111EBULL;
	x ^= x >> 31;
	return uint32_t(x ^ (x >> 32));
}

// Dry-ground probe bitmask packed inside CePlayerObs::dryMask.
enum CePlayerDryBits : uint32_t
{
	CE_DRY_HERE      = 1u << 0,
	CE_DRY_FWD       = 1u << 1,
	CE_DRY_BACK      = 1u << 2,
	CE_DRY_LEFT      = 1u << 3,
	CE_DRY_RIGHT     = 1u << 4,
	CE_DRY_DASH_L    = 1u << 5,
	CE_DRY_DASH_R    = 1u << 6,
	CE_DRY_AHEAD_28  = 1u << 7,
};

// Pointer-free per-entity observation record.
// Sorted strictly by (distSqQuantized, spawnSeq) — NEVER by CeEntityHandle numeric bits or worldEpoch!
struct CePlayerEntityObs
{
	CeEntityHandle	handle;
	uint32_t		spawnSeq;
	uint32_t		distSqQuantized;	// uint32_t(distSq * 16.0f), clamped
	int32_t			allegiance;
	uint8_t			targetClass;		// CeAiTargetClass
	uint8_t			isFriendly;
	uint8_t			airborne;
	uint8_t			inWater;
	uint8_t			isBattleEngine;
	uint8_t			isBaseBuilding;
	uint8_t			isRepairPad;
	uint8_t			reserved;
	float			pos[3];
	float			vel[3];
	float			lifeRatio;
};

// Pointer-free squad observation (zeroed whenever CE_TOGGLE_COMMANDER is OFF).
struct CePlayerSquadObs
{
	CeEntityHandle	squadHandle;
	int32_t			ordinal;
	uint8_t			friendly;
	uint8_t			commandable;
	uint8_t			deploys;
	uint8_t			crossesWater;
	int32_t			members;
	int32_t			verb;
	float			x;
	float			y;
	float			lifeRatio;
};

// Pointer-free takeable unit observation (zeroed whenever CE_TOGGLE_PLAY_UNIT is OFF).
struct CePlayerTakeableObs
{
	CeEntityHandle	unitHandle;
	uint32_t		spawnSeq;
	int32_t			classId;
	float			x;
	float			y;
	float			z;
	float			lifeRatio;
};

// Complete read-only observation snapshot for one Battle Engine slot (~8 KB; stored in static BSS, never stack).
struct CePlayerObs
{
	uint32_t			simTick;
	uint32_t			matchSeed;
	uint8_t				slot;
	uint8_t				isJet;
	uint8_t				isOnGround;
	uint8_t				isMorphing;
	int32_t				allegiance;
	uint32_t			activeToggles;		// CeMatchToggleMask
	uint32_t			dryMask;			// CePlayerDryBits
	float				lifeRatio;
	float				energyRatio;
	float				pos[3];
	float				vel[3];
	float				yaw;
	float				pitch;
	float				groundZ;
	float				waterZ;
	float				altitude;
	float				aheadGroundZ;
	int32_t				currentWeaponSlot;	// read directly from mWalkerPart->mCurrentWeapon / mJetPart->mCurrentWeapon
	int32_t				weaponCount;
	uint8_t				weaponReady;
	uint8_t				weaponOverheated;
	uint8_t				weaponCanCharge;
	uint8_t				weaponFullyCharged;
	uint8_t				weaponIsEnergy;
	uint8_t				weaponIsLock;
	uint8_t				weaponLocksFinished;
	uint8_t				reservedWep;
	int32_t				weaponAmmoCount;
	int32_t				weaponActiveLocks;
	float				weaponChargeRatio;
	float				weaponHeatOrAmmo;
	float				weaponLaunchSpeed;
	float				weaponMinRange;
	float				weaponMaxRange;
	uint32_t			entityCount;
	CePlayerEntityObs	entities[CE_PLAYER_OBS_MAX_ENTITIES];
	uint32_t			squadCount;
	CePlayerSquadObs	squads[CE_PLAYER_OBS_MAX_SQUADS];
	uint32_t			takeableCount;
	CePlayerTakeableObs	takeable[CE_PLAYER_OBS_MAX_TAKEABLE];
};

// Low-level Battle Engine pilot action (AI/bot slots only; human lockstep peers use CE_WorldInput exclusively).
struct CePilotInput
{
	float		forward;			// [-1, +1] Walker forward/back
	float		strafe;				// [-1, +1] Walker strafe left/right
	float		yawRate;			// [-1, +1] Walker rotate / Jet turn
	float		pitchRate;			// [-1, +1] Walker/Jet pitch
	float		thrustVy;			// Jet thrust
	int32_t		desiredWeaponSlot;	// -1 = keep current weapon, else 0..11
	uint8_t		fire;
	uint8_t		charge;
	uint8_t		abortCharge;
	uint8_t		morph;				// gated by CE_TOGGLE_FLIGHT
	uint8_t		landingJets;
	uint8_t		cloak;
	uint8_t		resetStrafeAxis;
	uint8_t		updateLocks;
};

struct CeCommanderIntent
{
	uint8_t			valid;
	int32_t			verb;			// CeCmdVerb
	CeEntityHandle	squadHandle;
	float			goalX;
	float			goalY;
};

struct CePossessIntent
{
	uint8_t			valid;
	uint8_t			release;
	CeEntityHandle	unitHandle;
	float			goalX;
	float			goalY;
	uint8_t			fire;
	float			aimX;
	float			aimY;
	float			aimZ;
};

struct CePlayerAction
{
	CePilotInput		pilot;
	CeCommanderIntent	cmd;
	CePossessIntent		possess;
};

// Optional high-level strategic prior produced asynchronously by Layer 3 (NULL when absent or in online lockstep v1).
enum CeStrategicPosture : uint8_t
{
	CE_STRAT_BALANCED = 0,
	CE_STRAT_PUSH     = 1,
	CE_STRAT_DEFEND   = 2,
	CE_STRAT_FLANK    = 3,
	CE_STRAT_REPAIR   = 4
};

struct CeStrategicPrior
{
	uint32_t			generation;
	uint32_t			sourceTick;
	CeStrategicPosture	posture;
	uint8_t				aggressionQ8;	// 0..255 (128 = neutral)
	uint8_t				airBiasQ8;		// 0..255 (128 = neutral)
	uint8_t				hasFocusPoint;
	float				focusX;
	float				focusY;
	char				hint[164];		// sanitized printable ASCII (<= 160 chars + NUL)
};

class ICePlayerAgent
{
public:
	virtual ~ICePlayerAgent() {}
	virtual void Reset(uint8_t slot, uint32_t matchSeed) = 0;
	virtual void Decide(const CePlayerObs &obs, const CeStrategicPrior *prior, CePlayerAction *out) = 0;
};

// Pure deterministic fog-of-war predicate (identical for humans & AI).
inline bool CePlayerObsEntityVisible(bool isFriendly, bool onScanner, float distSq)
{
	return isFriendly || onScanner || (distSq <= 90.0f * 90.0f);
}

// Quantizes squared distance into a deterministic integer sort key.
inline uint32_t CePlayerObsQuantizeDistSq(float distSq)
{
	if (!(distSq > 0.0f)) return 0u;
	const float scaled = distSq * 16.0f;
	if (scaled >= 4294967040.0f) return 0xFFFFFFFFu;
	return static_cast<uint32_t>(scaled);
}

// Pure deterministic insertion sort on (distSqQuantized, spawnSeq).
// Strictly independent of CeEntityHandle numeric bits, worldEpoch, and raw pointer addresses!
inline void CePlayerObsSortEntities(CePlayerEntityObs *items, uint32_t count)
{
	if (!items || count <= 1u) return;
	for (uint32_t i = 1u; i < count; ++i)
	{
		const CePlayerEntityObs key = items[i];
		uint32_t j = i;
		while (j > 0u)
		{
			const CePlayerEntityObs &prev = items[j - 1u];
			const bool less = (key.distSqQuantized < prev.distSqQuantized) ||
			                  (key.distSqQuantized == prev.distSqQuantized && key.spawnSeq < prev.spawnSeq);
			if (!less) break;
			items[j] = prev;
			--j;
		}
		items[j] = key;
	}
}

// Pure toggle-aware action sanitizer (masks disallowed capabilities deterministically before execution).
inline void CePlayerSanitizeAction(uint32_t activeToggles, CePlayerAction *act)
{
	if (!act) return;
	if (!(activeToggles & (1u << 6))) // CE_TOGGLE_FLIGHT
		act->pilot.morph = 0u;
	if (!(activeToggles & (1u << 2))) // CE_TOGGLE_COMMANDER
		memset(&act->cmd, 0, sizeof(act->cmd));
	if (!(activeToggles & (1u << 3))) // CE_TOGGLE_PLAY_UNIT
		memset(&act->possess, 0, sizeof(act->possess));
}

// Read-only accessor safe for RENDER overlays & debug HUDs.
const CePlayerObs *CePlayerLastObs(uint8_t slot);

// Registers an optional observation sink invoked synchronously inside SIM player_act (used by Layer-3 double buffer).
typedef void (*CePlayerObsSinkFn)(uint8_t slot, const CePlayerObs *obs);
void CePlayerHooksRegisterObsSink(CePlayerObsSinkFn sink);

// Hook entrypoint registered on kPlayerAct slot 10 (ce_hooks.cpp).
extern "C" void PortCePlayerHooksAct(uint32_t sim_tick);
extern "C" void PortCePlayerHooksLevelReset(void);

#endif
