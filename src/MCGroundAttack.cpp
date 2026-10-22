// MCGroundAttack.cpp: CMCGroundAttack, a ground-attack aircraft's motion controller (reconstructed; Stuart's file is
// not in his release). The "turret" part follows the unit's yaw, interpolated to the render frame, and the whole mesh
// bobs with the aircraft.

#include	"common.h"

#define MCGROUNDATTACK_CPP
#define GROUNDATTACK_FULL
#include	"MCGroundAttack.h"
#include	"mesh.h"
#include	"unit.h"
#include	"game.h"

#include	<string.h>

// The per-file maths constants from maths.h:
// FUNCTION: 0x004963e0 init:ZERO_FVECTOR
// FUNCTION: 0x00496400 init:ID_FMATRIX

//*********************************************************************************
CMCGroundAttack::CMCGroundAttack(IRenderableThing *thing)	// FUNCTION: 0x004964d0
{
	mThing = thing;
	mLastBob = mLastYaw = -999.0f;
}

// FUNCTION: 0x00496500 ??_GCMCGroundAttack@@UAEPAXI@Z

//*********************************************************************************
CMCGroundAttack::~CMCGroundAttack()	// FUNCTION: 0x00496520
{
	mThing = NULL;
}

//*********************************************************************************
void	CMCGroundAttack::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x00496540
{
	CGroundAttackAircraft	*unit = (CGroundAttackAircraft *)mThing;

	if (!strcmp(part->mName, "turret"))
	{
		FMatrix	old_ori(unit->mUnknownE4, 0, 0);
		FMatrix	new_ori(unit->mUnknownE0, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}

	if (!part->mPartNumber)
		pos->Z -= unit->mBob;

	mLastYaw = unit->mUnknownE0;
	mLastBob = unit->mBob;
}

//*********************************************************************************
BOOL	CMCGroundAttack::Unknown8()	// FUNCTION: 0x004968a0
{
	CGroundAttackAircraft	*unit = (CGroundAttackAircraft *)mThing;
	if ((unit->mUnknownE0 == unit->mUnknownE4) && (unit->mUnknownE0 == mLastYaw) && (unit->mBob == mLastBob))
		return FALSE;
	return TRUE;
}

//*********************************************************************************
BOOL	CMCGroundAttack::CanMergePart(CMeshPart *part)	// FUNCTION: 0x004968f0
{
	return stricmp(part->mName, "turret") != 0;
}

//*********************************************************************************
BOOL	CMCGroundAttack::HasTurret(CMesh *mesh)	// FUNCTION: 0x00496910
{
	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		CMeshPart	*part = mesh->mParts[i];
		if (!stricmp(part->mName, "turret"))
			return TRUE;
	}
	return FALSE;
}
