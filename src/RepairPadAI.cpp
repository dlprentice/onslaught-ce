// RepairPadAI.cpp: reconstructed from RepairPadAI.obj (004d6c20..004d6e76); Stuart's release has no copy. A repair
// pad's AI "targets" a battle engine standing on it that needs repair or rearming and is on a side it serves.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "BattleEngine.h"
#include "mapwho.h"
#include "mapwhoentry.h"

// FUNCTION: 0x004d6c20 init:ZERO_FVECTOR
// FUNCTION: 0x004d6c40 init:ID_FMATRIX

//******************************************************************************************
// A battle engine within 3 across and up to 5 below the pad.
CUnit *CRepairPadAI::Unknown11()	// FUNCTION: 0x004d6d10
{
	mTarget = NULL;
	mCanFireAtTarget = 0;
	mTargetInReach = 0;
	for (CMapWhoEntry *e = MAP_WHO.GetFirstEntryWithinRadius(mForUnit->GetPos(), 8.0f); e; e = MAP_WHO.GetNextEntryWithinRadius())
	{
		CThing	*thing = e->GetThing();
		if ((thing) && (thing->GetThingType() & THING_TYPE_BATTLE_ENGINE) && (CanService((CBattleEngine *)thing)))
		{
			if (((mForUnit->GetPos() - thing->GetPos()).MagnitudeXY() < 3.0f) && (mForUnit->GetPos().Z > thing->GetPos().Z) && (thing->GetPos().Z + 5.0f > mForUnit->GetPos().Z))
			{
				mTarget = (CUnit *)thing;
				mCanFireAtTarget = mTargetInReach = TRUE;
			}
		}
	}
	mUnknown10 = 0;
	return mTarget.ToRead();
}

//******************************************************************************************
// Written braced throughout, with the health test spelled `== FALSE`: VC6 offers a plain function for inlining only up
// to 174 size units, and this text is 180, so slot 11 calls it out of line (004d6d77) as retail does; the compact form
// (164) was inlined.
BOOL CRepairPadAI::CanService(CBattleEngine *be)	// FUNCTION: 0x004d6e00
{
	if ((be->FullHealth() == FALSE) || (be->NeedsRearm()))
	{
		switch (be->mAllegiance)
		{
			case kForsetiAllegiance:
			{
				if ((mForUnit->mAllegiance == kForsetiAllegiance) || (mForUnit->mAllegiance == kIndependentAllegiance))
				{
					return TRUE;
				}
				break;
			}
			case kMuspellAllegiance:
			{
				if ((mForUnit->mAllegiance == kMuspellAllegiance) || (mForUnit->mAllegiance == kIndependentAllegiance))
				{
					return TRUE;
				}
				break;
			}
			case kIndependentAllegiance:
			{
				if ((mForUnit->mAllegiance == kMuspellAllegiance) || (mForUnit->mAllegiance == kForsetiAllegiance))
				{
					return TRUE;
				}
				break;
			}
			default:
				break;
		}
	}
	return FALSE;
}
