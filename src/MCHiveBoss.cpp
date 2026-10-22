// MCHiveBoss.cpp: CMCHiveBoss, the hive boss's motion controller (reconstructed; Stuart's file is not in his release).
// On top of the segments' own motion, each of the boss's six rings of four cylinders (top, middle and bottom, outer
// and inner; north, south, east and west) turns by its core's angle, blended to the render frame.

#include	"common.h"

#define MCHIVEBOSS_CPP
#define MCDESTRUCTABLESEGMENTS_CPP	// the segments controller's own slots (GetPartTransform)
#include	"Motioncontroller.h"
#include	"DestructableSegmentsController.h"
#include	"mesh.h"
#include	"unit.h"
#include	"game.h"

#include	<string.h>

// The per-file maths constants from maths.h:
// FUNCTION: 0x00496fa0 init:ZERO_FVECTOR
// FUNCTION: 0x00496fc0 init:ID_FMATRIX

// The hive boss's motion controller (0x78 bytes, vtable 005dc388).
class CMCHiveBoss : public CDestructableSegmentsMotionController
{
public:
	CMCHiveBoss(CHiveBoss *boss);												// 00497090
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (004976d0)
	virtual BOOL	Unknown8() { return TRUE; }									// 8 (the shared return-1, 004014a0)

	void			FindCylinders(CMesh *mesh);									// 00497140
	static BOOL		CanMergePart(CMeshPart *part);								// 00498080: not a cylinder
	static BOOL		HasCylinders(CMesh *mesh);									// 00498270: a "Nmidoutcyl" part

	CHiveBoss	*mBoss;															// +0x10
	BOOL		mFound;															// +0x14: the cylinders have been looked up
	CMeshPart	*mNmidoutcyl;						// +0x18
	CMeshPart	*mSmidoutcyl;						// +0x1c
	CMeshPart	*mEmidoutcyl;						// +0x20
	CMeshPart	*mWmidoutcyl;						// +0x24
	CMeshPart	*mNmidincyl;							// +0x28
	CMeshPart	*mSmidincyl;							// +0x2c
	CMeshPart	*mEmidincyl;							// +0x30
	CMeshPart	*mWmidincyl;							// +0x34
	CMeshPart	*mNtopoutcyl;						// +0x38
	CMeshPart	*mStopoutcyl;						// +0x3c
	CMeshPart	*mEtopoutcyl;						// +0x40
	CMeshPart	*mWtopoutcyl;						// +0x44
	CMeshPart	*mNtopincyl;							// +0x48
	CMeshPart	*mStopincyl;							// +0x4c
	CMeshPart	*mEtopincyl;							// +0x50
	CMeshPart	*mWtopincyl;							// +0x54
	CMeshPart	*mNbotoutcyl;						// +0x58
	CMeshPart	*mSbotoutcyl;						// +0x5c
	CMeshPart	*mEbotoutcyl;						// +0x60
	CMeshPart	*mWbotoutcyl;						// +0x64
	CMeshPart	*mNbotincyl;							// +0x68
	CMeshPart	*mSbotincyl;							// +0x6c
	CMeshPart	*mEbotincyl;							// +0x70
	CMeshPart	*mWbotincyl;							// +0x74
};

//*********************************************************************************
CMCHiveBoss::CMCHiveBoss(CHiveBoss *boss)	// FUNCTION: 0x00497090
	: CDestructableSegmentsMotionController(boss->mSegments),
	  mBoss(boss),
	  mFound(FALSE),
	  mNmidoutcyl(NULL),
	  mSmidoutcyl(NULL),
	  mEmidoutcyl(NULL),
	  mWmidoutcyl(NULL),
	  mNmidincyl(NULL),
	  mSmidincyl(NULL),
	  mEmidincyl(NULL),
	  mWmidincyl(NULL),
	  mNtopoutcyl(NULL),
	  mStopoutcyl(NULL),
	  mEtopoutcyl(NULL),
	  mWtopoutcyl(NULL),
	  mNtopincyl(NULL),
	  mStopincyl(NULL),
	  mEtopincyl(NULL),
	  mWtopincyl(NULL),
	  mNbotoutcyl(NULL),
	  mSbotoutcyl(NULL),
	  mEbotoutcyl(NULL),
	  mWbotoutcyl(NULL),
	  mNbotincyl(NULL),
	  mSbotincyl(NULL),
	  mEbotincyl(NULL),
	  mWbotincyl(NULL)
{
}

// FUNCTION: 0x00497110 ??_GCMCHiveBoss@@UAEPAXI@Z
// FUNCTION: 0x00497130 ??1CMCHiveBoss@@UAE@XZ

//*********************************************************************************
void	CMCHiveBoss::FindCylinders(CMesh *mesh)	// FUNCTION: 0x00497140
{
	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		CMeshPart	*part = mesh->mParts[i];
		if (!strcmp(part->mName, "Nmidoutcyl"))
			mNmidoutcyl = part;
		if (!strcmp(part->mName, "Smidoutcyl"))
			mSmidoutcyl = part;
		if (!strcmp(part->mName, "Emidoutcyl"))
			mEmidoutcyl = part;
		if (!strcmp(part->mName, "Wmidoutcyl"))
			mWmidoutcyl = part;
		if (!strcmp(part->mName, "Nmidincyl"))
			mNmidincyl = part;
		if (!strcmp(part->mName, "Smidincyl"))
			mSmidincyl = part;
		if (!strcmp(part->mName, "Emidincyl"))
			mEmidincyl = part;
		if (!strcmp(part->mName, "Wmidincyl"))
			mWmidincyl = part;
		if (!strcmp(part->mName, "Ntopoutcyl"))
			mNtopoutcyl = part;
		if (!strcmp(part->mName, "Stopoutcyl"))
			mStopoutcyl = part;
		if (!strcmp(part->mName, "Etopoutcyl"))
			mEtopoutcyl = part;
		if (!strcmp(part->mName, "Wtopoutcyl"))
			mWtopoutcyl = part;
		if (!strcmp(part->mName, "Ntopincyl"))
			mNtopincyl = part;
		if (!strcmp(part->mName, "Stopincyl"))
			mStopincyl = part;
		if (!strcmp(part->mName, "Etopincyl"))
			mEtopincyl = part;
		if (!strcmp(part->mName, "Wtopincyl"))
			mWtopincyl = part;
		if (!strcmp(part->mName, "Nbotoutcyl"))
			mNbotoutcyl = part;
		if (!strcmp(part->mName, "Sbotoutcyl"))
			mSbotoutcyl = part;
		if (!strcmp(part->mName, "Ebotoutcyl"))
			mEbotoutcyl = part;
		if (!strcmp(part->mName, "Wbotoutcyl"))
			mWbotoutcyl = part;
		if (!strcmp(part->mName, "Nbotincyl"))
			mNbotincyl = part;
		if (!strcmp(part->mName, "Sbotincyl"))
			mSbotincyl = part;
		if (!strcmp(part->mName, "Ebotincyl"))
			mEbotincyl = part;
		if (!strcmp(part->mName, "Wbotincyl"))
			mWbotincyl = part;
	}
	mFound = TRUE;
}

//*********************************************************************************
void	CMCHiveBoss::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x004976d0
{
	if (!mFound)
		FindCylinders(part->mBaseMesh);

	CDestructableSegmentsMotionController::GetPartTransform(part, pos, ori, world);

	if ((part == mNmidoutcyl) || (part == mSmidoutcyl) || (part == mEmidoutcyl) || (part == mWmidoutcyl))
	{
		float	old_angle = mBoss->mOldCoreAngles[2];
		float	new_angle = mBoss->mCoreAngles[2];
		FMatrix	old_ori = FMatrix(old_angle, 0, 0);
		FMatrix	new_ori = FMatrix(new_angle, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
	if ((part == mNmidincyl) || (part == mSmidincyl) || (part == mEmidincyl) || (part == mWmidincyl))
	{
		float	old_angle = mBoss->mOldCoreAngles[3];
		float	new_angle = mBoss->mCoreAngles[3];
		FMatrix	old_ori = FMatrix(old_angle, 0, 0);
		FMatrix	new_ori = FMatrix(new_angle, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
	if ((part == mNtopoutcyl) || (part == mStopoutcyl) || (part == mEtopoutcyl) || (part == mWtopoutcyl))
	{
		float	old_angle = mBoss->mOldCoreAngles[0];
		float	new_angle = mBoss->mCoreAngles[0];
		FMatrix	old_ori = FMatrix(old_angle, 0, 0);
		FMatrix	new_ori = FMatrix(new_angle, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
	if ((part == mNtopincyl) || (part == mStopincyl) || (part == mEtopincyl) || (part == mWtopincyl))
	{
		float	old_angle = mBoss->mOldCoreAngles[1];
		float	new_angle = mBoss->mCoreAngles[1];
		FMatrix	old_ori = FMatrix(old_angle, 0, 0);
		FMatrix	new_ori = FMatrix(new_angle, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
	if ((part == mNbotoutcyl) || (part == mSbotoutcyl) || (part == mEbotoutcyl) || (part == mWbotoutcyl))
	{
		float	old_angle = mBoss->mOldCoreAngles[4];
		float	new_angle = mBoss->mCoreAngles[4];
		FMatrix	old_ori = FMatrix(old_angle, 0, 0);
		FMatrix	new_ori = FMatrix(new_angle, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
	if ((part == mNbotincyl) || (part == mSbotincyl) || (part == mEbotincyl) || (part == mWbotincyl))
	{
		float	old_angle = mBoss->mOldCoreAngles[5];
		float	new_angle = mBoss->mCoreAngles[5];
		FMatrix	old_ori = FMatrix(old_angle, 0, 0);
		FMatrix	new_ori = FMatrix(new_angle, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
}

//*********************************************************************************
BOOL	CMCHiveBoss::CanMergePart(CMeshPart *part)	// FUNCTION: 0x00498080
{
	if (!stricmp(part->mName, "Nmidoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Emidoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Wmidoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Smidoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Nmidincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Emidincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Wmidincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Smidincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Ntopoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Etopoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Wtopoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Stopoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Ntopincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Etopincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Wtopincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Stopincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Nbotoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Ebotoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Wbotoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Sbotoutcyl"))
		return FALSE;
	if (!stricmp(part->mName, "Nbotincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Ebotincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Wbotincyl"))
		return FALSE;
	if (!stricmp(part->mName, "Sbotincyl"))
		return FALSE;
	return TRUE;
}

//*********************************************************************************
BOOL	CMCHiveBoss::HasCylinders(CMesh *mesh)	// FUNCTION: 0x00498270
{
	for (SINT i = 0; i < mesh->mNumParts; i++)
		if (!stricmp(mesh->GetPart(i)->mName, "Nmidoutcyl"))
			return TRUE;
	return FALSE;
}
