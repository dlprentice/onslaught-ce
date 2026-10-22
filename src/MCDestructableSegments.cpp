// MCDestructableSegments.cpp: CDestructableSegmentsMotionController, the motion controller of a unit built of
// destroyable segments (reconstructed; Stuart's file is not in his release). A falling core drops and tilts; broken
// parts are hidden from collision, and each part draws its damage stage.

#include	"common.h"

#define MCDESTRUCTABLESEGMENTS_CPP
#include	"Motioncontroller.h"
#include	"DestructableSegmentsController.h"
#include	"mesh.h"
#include	"unit.h"

#include	<string.h>

// The per-file maths constants from maths.h:
// FUNCTION: 0x00494b70 init:ZERO_FVECTOR
// FUNCTION: 0x00494b90 init:ID_FMATRIX

//*********************************************************************************
CDestructableSegmentsMotionController::CDestructableSegmentsMotionController(CDestructableSegmentsController *segments)	// FUNCTION: 0x00494c60
{
	mSegments = segments;
	mThing = segments->mOwner;
}

// FUNCTION: 0x00494ca0 ??_GCDestructableSegmentsMotionController@@UAEPAXI@Z

//*********************************************************************************
CDestructableSegmentsMotionController::~CDestructableSegmentsMotionController()	// FUNCTION: 0x00494cc0
{
	mThing = NULL;
	mSegments = NULL;
}

//*********************************************************************************
// a falling core drops, and leans the further it has fallen
void	CDestructableSegmentsMotionController::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x00494ce0
{
	float	fall = mSegments->GetCoreFall(part->mPartNumber);
	pos->Z += fall;

	if (fall > 0.0)
	{
		float	lean = fall;
		if (lean > 2.f)
			lean = 2.f;
		FMatrix	tilt(0, lean / 25.0f, lean / 14.f);
		*ori = *ori * tilt;
	}

	mSegments->mChanged = FALSE;
}

//*********************************************************************************
void	CDestructableSegmentsMotionController::Unknown6(CMeshPart *part, SINT *flags)	// FUNCTION: 0x00494fa0
{
	if (part->mIsChained)
	{
		*flags |= 1;
		return;
	}
	if (!mSegments->CanUse(part->mPartNumber))
		*flags |= 1;
	else
		*flags &= ~1;
}

//*********************************************************************************
SINT	CDestructableSegmentsMotionController::Unknown7(CMeshPart *part)	// FUNCTION: 0x00494ff0
{
	if (part->mIsChained)
		return 0;
	return mSegments->GetDamageStage(part->mPartNumber);
}

//*********************************************************************************
BOOL	CDestructableSegmentsMotionController::Unknown9()	// FUNCTION: 0x004fdc10
{
	return TRUE;
}

//******************************************************************************************
BOOL	CDestructableSegmentsMotionController::Unknown8()	// FUNCTION: 0x00495020
{
	return mSegments->mChanged;
}

//*********************************************************************************
BOOL	CDestructableSegmentsMotionController::CanOptimisePart(CMeshPart *part)	// FUNCTION: 0x00495030
{
	if (!strnicmp(part->mName, "CORE", 4))
		return FALSE;
	if (part->mNumChainedParts > 0)
		return FALSE;
	if (!strnicmp(part->mName, "x1", 2))
		return FALSE;
	if ((part->mPartType == 1) && (part->mNumTriangles > 20))
		return FALSE;
	return TRUE;
}

//*********************************************************************************
BOOL	CDestructableSegmentsMotionController::CanMergePart(CMeshPart *part)	// FUNCTION: 0x00495090
{
	if (!strnicmp(part->mName, "CORE", 4))
		return FALSE;
	if (!strnicmp(part->mName, "x1", 2))
		return FALSE;
	if (part->mNumChainedParts > 0)
		return FALSE;
	if ((part->mPartType == 1) && (part->mNumTriangles > 20))
		return FALSE;
	return TRUE;
}

//*********************************************************************************
BOOL	CDestructableSegmentsMotionController::HasCore(CMesh *mesh)	// FUNCTION: 0x004950f0
{
	for (SINT i = 0; i < mesh->mNumParts; i++)
		if (!strnicmp(mesh->GetPart(i)->mName, "CORE", 4))
			return TRUE;
	return FALSE;
}
