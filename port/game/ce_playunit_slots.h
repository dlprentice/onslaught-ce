// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_playunit_slots.h: Headless multi-slot unit possession port (IUnitPossessionPort).
// Companion TU to ce_playunit.cpp: manages simulation-only unit leases for slots 0..3 with
// zero camera creation, zero controller-stack mutation, zero HUD mutation, and zero heap allocation.
// Leaves ce_playunit.cpp's strict Player-0 camera and allocation invariants completely untouched.
#ifndef PORT_CE_PLAYUNIT_SLOTS_H
#define PORT_CE_PLAYUNIT_SLOTS_H

#include <stdint.h>
#include <string.h>
#include "ce_entity_handles.h"
#include "ce_playunit_rules.h"

static constexpr int CE_PLAYUNIT_MAX_SLOTS = 4;

struct CePlayUnitSlotCandidate
{
	CeEntityHandle	unitHandle;
	uint32_t		spawnSeq;
	int				classId;
	float			x;
	float			y;
	float			z;
	float			lifeRatio;
};

struct CeUnitPossessionSlot
{
	bool			active;
	uint8_t			slot;
	int				allegiance;
	int				classId;
	CeEntityHandle	unitHandle;
	CeEntityHandle	previousSquadHandle;
	int				previousAiState;
	uint32_t		lastOrderTick;
};

// Pure host-testable headless lease manager (zero game-header dependencies).
class CePlayUnitSlotsCore
{
	CeUnitPossessionSlot	slots_[CE_PLAYUNIT_MAX_SLOTS];

public:
	CePlayUnitSlotsCore() { Reset(); }

	void Reset()
	{
		memset(slots_, 0, sizeof(slots_));
		for (int i = 0; i < CE_PLAYUNIT_MAX_SLOTS; ++i)
			slots_[i].slot = static_cast<uint8_t>(i);
	}

	bool IsUnitLeased(CeEntityHandle handle, int *outOwnerSlot = nullptr) const
	{
		if (handle == kCeEntityHandleInvalid) return false;
		for (int i = 0; i < CE_PLAYUNIT_MAX_SLOTS; ++i)
		{
			if (slots_[i].active && slots_[i].unitHandle == handle)
			{
				if (outOwnerSlot) *outOwnerSlot = i;
				return true;
			}
		}
		return false;
	}

	bool Take(uint8_t slot, int allegiance, CeEntityHandle unitHandle,
	          CeEntityHandle previousSquadHandle, int previousAiState,
	          const CePlayUnitFacts &facts, bool toggleAllowed, bool humanOwnsUnit,
	          const char **outReason = nullptr)
	{
		auto refuse = [&](const char *msg) -> bool {
			if (outReason) *outReason = msg;
			return false;
		};
		if (!toggleAllowed) return refuse("play-unit toggle is off");
		if (slot >= CE_PLAYUNIT_MAX_SLOTS) return refuse("invalid slot");
		if (unitHandle == kCeEntityHandleInvalid) return refuse("invalid unit handle");
		if (humanOwnsUnit) return refuse("unit owned by human pilot");
		int owner = -1;
		if (IsUnitLeased(unitHandle, &owner) && owner != (int)slot)
			return refuse("unit already leased by another slot");
		const int classId = CePlayUnitClassFor(facts);
		if (classId < 0)
			return refuse(facts.friendly ? "unit not eligible" : "enemy unit");
		CeUnitPossessionSlot &s = slots_[slot];
		s.active = true;
		s.slot = slot;
		s.allegiance = allegiance;
		s.classId = classId;
		s.unitHandle = unitHandle;
		s.previousSquadHandle = previousSquadHandle;
		s.previousAiState = previousAiState;
		s.lastOrderTick = 0u;
		if (outReason) *outReason = nullptr;
		return true;
	}

	bool Release(uint8_t slot, CeUnitPossessionSlot *outPrev = nullptr)
	{
		if (slot >= CE_PLAYUNIT_MAX_SLOTS || !slots_[slot].active) return false;
		if (outPrev) *outPrev = slots_[slot];
		slots_[slot].active = false;
		slots_[slot].unitHandle = kCeEntityHandleInvalid;
		slots_[slot].previousSquadHandle = kCeEntityHandleInvalid;
		slots_[slot].classId = -1;
		return true;
	}

	const CeUnitPossessionSlot *Get(uint8_t slot) const
	{
		if (slot >= CE_PLAYUNIT_MAX_SLOTS) return nullptr;
		return &slots_[slot];
	}

	CeUnitPossessionSlot *Mutable(uint8_t slot)
	{
		if (slot >= CE_PLAYUNIT_MAX_SLOTS) return nullptr;
		return &slots_[slot];
	}
};

// Engine-integrated headless multi-slot Play-Any-Unit API (ce_playunit_slots.cpp).
class CUnit;
int				CePlayUnitSlotsListTakeable(int allegiance, CePlayUnitSlotCandidate *out, int max);
bool			CePlayUnitSlotTake(uint8_t slot, int allegiance, CeEntityHandle unitHandle, const char **outReason = nullptr);
bool			CePlayUnitSlotDrive(uint8_t slot, float goalX, float goalY, bool fire, float aimX, float aimY, float aimZ, uint32_t simTick);
void			CePlayUnitSlotRelease(uint8_t slot);
CeEntityHandle	CePlayUnitSlotUnit(uint8_t slot);
bool			CePlayUnitSlotsIsLeased(const CUnit *u);
void			CePlayUnitSlotsReleaseUnit(CUnit *u);
void			CePlayUnitSlotsTick(uint32_t simTick);
void			CePlayUnitSlotsReset();

#endif
