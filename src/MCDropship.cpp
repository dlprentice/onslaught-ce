// MCDropship.cpp: CMCDropship, a dropship's motion controller (reconstructed; Stuart's file is not in his release).
// It runs the doors' opening or closing animation by how far open the dropship's doors are.

#include	"common.h"

#define MCDROPSHIP_CPP
#include	"Motioncontroller.h"
#include	"mesh.h"
#include	"unit.h"
#include	"game.h"

#include	<string.h>

// The per-file maths constants from maths.h:
// FUNCTION: 0x00495fa0 init:ZERO_FVECTOR
// FUNCTION: 0x00495fc0 init:ID_FMATRIX

//*********************************************************************************
CMCDropship::CMCDropship(IRenderableThing *thing)	// FUNCTION: 0x00496090
{
	mThing = thing;
	mLastDoorOpen = -999.f;
	mLastDoorState = -1;
}

// FUNCTION: 0x004960c0 ??_GCMCDropship@@UAEPAXI@Z

//*********************************************************************************
CMCDropship::~CMCDropship()	// FUNCTION: 0x004960e0
{
	mThing = NULL;
}

//*********************************************************************************
static inline void	SetDoorFrame(float *frame, CDropship *dropship, CAnimMode *anim)
{
	*frame = dropship->mUnknown2A8 + (dropship->mUnknown2A4 - dropship->mUnknown2A8) * GAME.GetFrameRenderFraction();
	*frame *= anim->mFrameScaler;
	*frame += anim->mStart;
}

//*********************************************************************************
// The door's frame is set once, after the search (retail's single copy at 004961ac): computing it inside both
// loops weights the dropship's uses and gives it ebx, where retail keeps the part in ebx.
void	CMCDropship::AdjustFrame(CMeshPart *part, float *frame)	// FUNCTION: 0x00496100
{
	if (strnicmp(part->mName, "door", 4))
		return;

	CDropship	*dropship = (CDropship *)mThing;
	CAnimMode	*anim = part->mBaseMesh->mAnimModes;
	SINT		n;
	BOOL		found = FALSE;
	if ((dropship->mDoorState == 7) || (dropship->mDoorState == 5))
	{
		for (n = 0; n < part->mBaseMesh->mNumAnimModes; anim++, n++)
			if (!stricmp("doorclosing", anim->mModeName))
			{
				found = TRUE;
				break;
			}
	}
	else
	{
		for (n = 0; n < part->mBaseMesh->mNumAnimModes; anim++, n++)
			if (!stricmp("dooropening", anim->mModeName))
			{
				found = TRUE;
				break;
			}
	}
	if (found)
		SetDoorFrame(frame, dropship, anim);
	mLastDoorState = dropship->mDoorState;
	mLastDoorOpen = dropship->mUnknown2A4;
}

//*********************************************************************************
BOOL	CMCDropship::Unknown8()	// FUNCTION: 0x00496200
{
	CDropship	*dropship = (CDropship *)mThing;
	if ((mLastDoorState == dropship->mDoorState) && (dropship->mUnknown2A8 == dropship->mUnknown2A4) && (mLastDoorOpen == dropship->mUnknown2A4))
		return FALSE;
	return TRUE;
}

//*********************************************************************************
BOOL	CMCDropship::CanMergePart(CMeshPart *part)	// FUNCTION: 0x00496250
{
	return strnicmp(part->mName, "Door", 4) != 0;
}

//*********************************************************************************
BOOL	CMCDropship::HasDoorAnimation(CMesh *mesh)	// FUNCTION: 0x00496270
{
	if (mesh->GetAnimModeByName("DoorOpening") != AM_INVALID)
		return TRUE;
	return mesh->GetAnimModeByName("DoorClosing") != AM_INVALID;
}
