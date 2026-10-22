// MCSentinel.cpp: reconstructed from MCSentinel.obj (0049c4e0..0049c9df); Stuart's release has no copy. A
// sentinel's motion controller: its turret follows the unit's yaw and its barrel the unit's pitch.

#include "common.h"
#include "Motioncontroller.h"
#include "mesh.h"
#include "unit.h"

#include <string.h>

// FUNCTION: 0x0049c4e0 init:ZERO_FVECTOR
// FUNCTION: 0x0049c500 init:ID_FMATRIX

//******************************************************************************************
CMCSentinel::CMCSentinel(IRenderableThing *thing)	// FUNCTION: 0x0049c5d0
{
	mThing = thing;
	mLastPitch = mLastYaw = -999.0f;
}

// FUNCTION: 0x0049c600 ??_GCMCSentinel@@UAEPAXI@Z

//******************************************************************************************
CMCSentinel::~CMCSentinel()	// FUNCTION: 0x0049c620
{
	mThing = NULL;
}

//******************************************************************************************
void CMCSentinel::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x0049c640
{
	CSentinel	*sentinel = (CSentinel *)mThing;
	float		frac = sentinel->GetFractionTime();

	if (!stricmp(part->mName, "X1 turret"))
	{
		FMatrix	old_ori(sentinel->mUnknownE4 + PI / 2 + 0.3f, 0, 0);
		FMatrix	new_ori(sentinel->mUnknownE0 + PI / 2 + 0.3f, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * frac);
		ori->Normalise();
	}
	else if (!strnicmp(part->mName, "X1 barrel", 9))
	{
		FMatrix	old_ori(0, 0, -sentinel->mUnknownF0);
		FMatrix	new_ori(0, 0, -sentinel->mUnknownE8);
		*ori = *ori * (old_ori + (new_ori - old_ori) * frac);
		ori->Normalise();
	}

	mLastYaw = sentinel->mUnknownE0;
	mLastPitch = sentinel->mUnknownE8;
}

//******************************************************************************************
// The same code as CMCComponent's; the linker keeps that copy (00495770).
BOOL CMCSentinel::Unknown8()
{
	CUnit	*unit = (CUnit *)mThing;
	if ((unit->mUnknownE4 != unit->mUnknownE0) || (unit->mUnknownE0 != mLastYaw) ||
		(unit->mUnknownF0 != unit->mUnknownE8) || (unit->mUnknownE8 != mLastPitch))
		return TRUE;
	return FALSE;
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004956a0 ??HFMatrix@@QBE?AV0@ABV0@@Z
// FUNCTION: 0x00495770 ?Unknown8@CMCSentinel@@UAEHXZ
