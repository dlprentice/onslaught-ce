// The CRT pair at 00477bc0/00477be0 precedes this selector and the next pair at 00478000/00478020.
// Its retail source/class name is open; incoming ECX is unused, and the caller's globals hold one-byte allocations.
#include "common.h"
#include "NormalSquadTargetSelector.h"
#include "squad.h"
#include "unit.h"
#include "world.h"

// FUNCTION: 0x00477bc0 init:ZERO_FVECTOR
// FUNCTION: 0x00477be0 init:ID_FMATRIX

// The squads' two influence buffers (NormalSquadTargetSelector.h): retail keeps them just before this object's
// ID_FMATRIX (0067a080), B at the lower address (defined second, as the PCH order puts it).
char	*INFLUENCE_BUFFER_A;	// 0067a07c
char	*INFLUENCE_BUFFER_B;	// 0067a078

CThing *CNormalSquadTargetSelector::Unknown477cb0(CSquad *squad)	// FUNCTION: 0x00477cb0
{
	SPtrSet<CThing> *things = &WORLD.mThings;
	if (squad->mUnknown7C == 1)
		things = &WORLD.mSet20;
	else if (squad->mUnknown7C == 0)
		things = &WORLD.mSet30;

	CUnit *best = NULL;
	float bestScore = -1.0f;
	FVector position = squad->Unknown72();
	CUnit *first = squad->Unknown73();
	if (first)
	{
		ListIterator<CThing> it(things);
		for (CThing *thing = it.First(); thing; thing = it.Next())
		{
			CUnit *unit = NULL;
			if (thing->GetThingType() & THING_TYPE_UNKNOWN_20000000)
				unit = ((CSquad *)thing)->Unknown75(position);
			else if (thing->GetThingType() & THING_TYPE_UNIT)
				unit = (CUnit *)thing;
			if (!unit)
				continue;

			FVector delta = unit->GetPos() - position;
			float distanceSq = delta.MagnitudeSq();
			BOOL visible = TRUE;
			CUnitData *data = squad->mUnknownA0;
			if (unit->GetStealth() != 0)
			{
				float range = (1.0f - unit->GetStealth() / 100.0f) * data->mMaxTargetRange;
				if (distanceSq > range * range)
					visible = FALSE;
			}
			if (!squad->Unknown4e6680(unit->GetAllegiance()) || !first->Unknown4fb3d0(unit) || !visible)
				continue;

			float priority = 0;
			ULONG type = unit->GetThingType();
			if (type & THING_TYPE_VEHICLE)
				priority = data->mAttackPriority[1];
			else if (type & THING_TYPE_INFANTRY)
				priority = data->mAttackPriority[4];
			else if (type & THING_TYPE_AIR_UNIT)
				priority = data->mAttackPriority[5];
			else if (type & THING_TYPE_COMPONENT)
				priority = data->mAttackPriority[6];
			else if (type & THING_TYPE_EMPLACEMENT)
				priority = data->mAttackPriority[0];
			else if (type & THING_TYPE_BUILDING)
				priority = data->mAttackPriority[2];
			else if (type & THING_TYPE_NAVAL)
				priority = data->mAttackPriority[3];

			float distance = sqrtf(distanceSq);
			float score = 1000.0f - distance + priority * 10.0f;
			first->Unknown4fb840(unit);
			if (first->GetMinRange(unit->GetPos()) <= distance)
			{
				if (first->GetMaxRange(unit->GetPos()) < distance)
					score += 10000.0f;
				else
					score += 100000.0f;
			}
			if (!unit->IsAThreat() && !unit->IsObjective())
				score = 0;
			if (squad->mUnknown9C == 1 && (unit->GetPos() - squad->mUnknown8C).MagnitudeXYSq() > 5625.0f)
				score = -1.0f;
			// Strict comparison retains the first admitted candidate when scores tie.
			if (score > bestScore)
			{
				bestScore = score;
				best = unit;
			}
		}
	}
	CThing *result = best;
	if (best && best->mSquad.ToRead())
		result = best->mSquad.ToRead();
	return result;
}
