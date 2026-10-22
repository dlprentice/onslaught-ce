#ifdef EDITORBUILD
#include	"stdafx.h"
#endif

#include	"Common.h"	// FUNCTION: 0x0048c560 init:ZERO_FVECTOR
class CUnitInitThing; class CTreeInitThing; class CSpawnerInitThing; class CSquadInitThing; class CWallInitThing; class CFeatureInitThing; class CCutsceneInitThing; class CHazardInitThing; class CSphereTriggerInitThing; class CStartInitThing;	// retail's ??_R4 order implies these classes were created in this order before InitThing.h's definitions (a header mention we haven't identified); placement evidence. All ten are needed: dropping any one loses the order.
#include	"InitThing.h"	// FUNCTION: 0x0048c580 init:ID_FMATRIX
#include	"Oids.h"
#include	"WorldPhysicsManager.h"
CInitThing *SpawnInitThing(SINT inID,BOOL inReportErrors)	// FUNCTION: 0x0048c650
{
	switch (inID)
	{
		case OID_CUnit:
			return new( MT_INIT_THING ) CUnitInitThing;
			break;

		case OID_CBuilding:
			return new( MT_INIT_THING ) CUnitInitThing;
			break;

		case OID_CSquad:
			return new( MT_INIT_THING ) CSquadInitThing;
			break;

		case OID_CSpawnerThing:
			return new( MT_INIT_THING ) CSpawnerInitThing;
			break;

		case OID_CCutscene:
			return new( MT_INIT_THING ) CCutsceneInitThing;
			break;

		case OID_CStart:
			return new( MT_INIT_THING ) CStartInitThing;
			break;

		case OID_CSpawnPoint:
			return new( MT_INIT_THING ) CStartInitThing;
			break;

		case OID_CTree:
			return new( MT_INIT_THING ) CTreeInitThing;
			break;

		case OID_CWall:
			return new( MT_INIT_THING ) CWallInitThing;
			break;

		case OID_CFeature:
			return new( MT_INIT_THING ) CFeatureInitThing;
			break;

		case OID_CHazard:
			return new( MT_INIT_THING ) CHazardInitThing;
			break;

		case OID_CSphereTrigger:
			return new( MT_INIT_THING ) CSphereTriggerInitThing;
			break;

		default:
			return new( MT_INIT_THING ) CInitThing;
			break;
	}
}

CInitThing::CInitThing() :	// FUNCTION: 0x0048dcf0
	mOrientation(ID_FMATRIX),
	mYaw(0),
	mPitch(0),
	mRoll(0),
	mVelocity(ZERO_FVECTOR),
	mOrientationType(EULER_ANGLES),
	mMeshNo(0),
	mAllegiance(kNeutralAllegiance),
	mTarget(NULL),
	mActive(TRUE),
	mSpawnedBy(NULL),
	mAttachScriptsToUnits(FALSE)
{
	mScript[0]=0;
	mName[0]=0;
	mSpawnScript[0]=0;
	mWaypointPath=kNullEmitter;
	mForceRadius=-1;
}

#ifdef EDITORBUILD
void CInitThing::Save(
	CArchive		&ar)
{
	ar << mPos.X << mPos.Y << mPos.Z;
	ar << mYaw << mPitch << mRoll;
	ar << mMeshNo;
	ar << mAllegiance;
	ar << mTarget;

	char n=-1;
	do
	{
		n++;
		ar << mScript[n];
	}
	while (mScript[n]);

	n=-1;
	do
	{
		n++;
		ar << mName[n];
	}
	while (mName[n]);

	n=-1;
	do
	{
		n++;
		ar << mSpawnScript[n];
	}
	while (mSpawnScript[n]);

	ar << mActive << mAttachScriptsToUnits;
}

void CInitThing::Load(
	CArchive	&ar,
	short		inVersion)
{
	long	tempLong;

	if (inVersion<=16)
	{
		ar >> mPos.X >> mPos.Y >> mPos.Z;
		
		ar >> mOrientation.Row[0].X >> mOrientation.Row[0].Y >> mOrientation.Row[0].Z;
		ar >> mOrientation.Row[1].X >> mOrientation.Row[1].Y >> mOrientation.Row[1].Z;
		ar >> mOrientation.Row[2].X >> mOrientation.Row[2].Y >> mOrientation.Row[2].Z;
		
		ar >> mYaw >> mPitch >> mRoll;
		ar >> mVelocity.X >> mVelocity.Y >> mVelocity.Z;
		ar >> tempLong;
		
		ar >> tempLong;
		mOrientationType=(EOrientationType)tempLong;
		
		ar >> tempLong;
		ar >> tempLong;

		ar >> mMeshNo;

		ar >> tempLong;
		mAllegiance=(EAllegiance)tempLong;

		ar >> mTarget;
	}
	else if (inVersion<=19)
	{
		ar >> mPos.X >> mPos.Y >> mPos.Z;
		ar >> mYaw >> mPitch >> mRoll;
		ar >> mMeshNo;

		ar >> tempLong;
		mAllegiance=(EAllegiance)tempLong;

		ar >> mTarget;
		
		char n=-1;
		do
		{
			n++;
			ar >> mScript[n];
		}
		while (mScript[n]);
	}
	else if (inVersion<=27)
	{
		ar >> mPos.X >> mPos.Y >> mPos.Z;
		ar >> mYaw >> mPitch >> mRoll;
		ar >> mMeshNo;

		ar >> tempLong;
		mAllegiance=(EAllegiance)tempLong;

		ar >> mTarget;
		
		char n=-1;
		do
		{
			n++;
			ar >> mScript[n];
		}
		while (mScript[n]);

		n=-1;
		do
		{
			n++;
			ar >> mName[n];
		}
		while (mName[n]);
	}
	else if (inVersion<=33)
	{
		ar >> mPos.X >> mPos.Y >> mPos.Z;
		ar >> mYaw >> mPitch >> mRoll;
		ar >> mMeshNo;

		ar >> tempLong;
		mAllegiance=(EAllegiance)tempLong;

		ar >> mTarget;
		
		char n=-1;
		do
		{
			n++;
			ar >> mScript[n];
		}
		while (mScript[n]);

		n=-1;
		do
		{
			n++;
			ar >> mName[n];
		}
		while (mName[n]);

		n=-1;
		do
		{
			n++;
			ar >> mSpawnScript[n];
		}
		while (mSpawnScript[n]);
	}
	else if (inVersion<=45)
	{
		ar >> mPos.X >> mPos.Y >> mPos.Z;
		ar >> mYaw >> mPitch >> mRoll;
		ar >> mMeshNo;

		ar >> tempLong;
		mAllegiance=(EAllegiance)tempLong;

		ar >> mTarget;
		
		char n=-1;
		do
		{
			n++;
			ar >> mScript[n];
		}
		while (mScript[n]);

		n=-1;
		do
		{
			n++;
			ar >> mName[n];
		}
		while (mName[n]);

		n=-1;
		do
		{
			n++;
			ar >> mSpawnScript[n];
		}
		while (mSpawnScript[n]);

		ar >> mActive;
	}
	else
	{
		ar >> mPos.X >> mPos.Y >> mPos.Z;
		ar >> mYaw >> mPitch >> mRoll;
		ar >> mMeshNo;

		ar >> tempLong;
		mAllegiance=(EAllegiance)tempLong;

		ar >> mTarget;
		
		char n=-1;
		do
		{
			n++;
			ar >> mScript[n];
		}
		while (mScript[n]);

		n=-1;
		do
		{
			n++;
			ar >> mName[n];
		}
		while (mName[n]);

		n=-1;
		do
		{
			n++;
			ar >> mSpawnScript[n];
		}
		while (mSpawnScript[n]);

		ar >> mActive >> mAttachScriptsToUnits;
	}
}

void CSpawnerInitThing::Save(
	CArchive	&ar)
{
	CInitThing::Save(ar);

	ar << mAmount;
	ar << mDelay;
	ar << mSquadDelay;
	ar << mInitialDelay;
	ar << mSquadSize;

	char n=-1;
	do
	{
		n++;
		ar << mSpawnUnit[n];
	}
	while (mSpawnUnit[n]);

	n=-1;
	do
	{
		n++;
		ar << mSpawnerSpawnScript[n];
	}
	while (mSpawnerSpawnScript[n]);
}

void CSpawnerInitThing::Load(
	CArchive	&ar,
	short		inVersion)
{
	CInitThing::Load(ar,inVersion);

	if (inVersion<=16)
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mInitialDelay;

		ar.Read(mSpawnUnit,256);
	}
	else if (inVersion<=20)
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mInitialDelay;

		char n=-1;
		do
		{
			n++;
			ar >> mSpawnUnit[n];
		}
		while (mSpawnUnit[n]);
	}
	else if (inVersion<=21)
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mInitialDelay;
		ar >> mSquadSize;

		char n=-1;
		do
		{
			n++;
			ar >> mSpawnUnit[n];
		}
		while (mSpawnUnit[n]);
	}
	else if (inVersion<=22)
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mInitialDelay;
		ar >> mSquadSize;
		ar >> mActive;

		char n=-1;
		do
		{
			n++;
			ar >> mSpawnUnit[n];
		}
		while (mSpawnUnit[n]);
	}
	else if (inVersion<=24)
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mInitialDelay;
		ar >> mSquadSize;
		ar >> mActive;

		char n=-1;
		do
		{
			n++;
			ar >> mSpawnUnit[n];
		}
		while (mSpawnUnit[n]);

		n=-1;
		do
		{
			n++;
			ar >> mSpawnScript[n];
		}
		while (mSpawnScript[n]);
	}
	else if (inVersion<=27)
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mSquadDelay;
		ar >> mInitialDelay;
		ar >> mSquadSize;
		ar >> mActive;

		char n=-1;
		do
		{
			n++;
			ar >> mSpawnUnit[n];
		}
		while (mSpawnUnit[n]);

		n=-1;
		do
		{
			n++;
			ar >> mSpawnScript[n];
		}
		while (mSpawnScript[n]);
	}
	else if (inVersion<=33)
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mSquadDelay;
		ar >> mInitialDelay;
		ar >> mSquadSize;
		ar >> mActive;

		char n=-1;
		do
		{
			n++;
			ar >> mSpawnUnit[n];
		}
		while (mSpawnUnit[n]);
	}
	else if (inVersion<=43)
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mSquadDelay;
		ar >> mInitialDelay;
		ar >> mSquadSize;

		char n=-1;
		do
		{
			n++;
			ar >> mSpawnUnit[n];
		}
		while (mSpawnUnit[n]);
	}
	else
	{
		ar >> mAmount;
		ar >> mDelay;
		ar >> mSquadDelay;
		ar >> mInitialDelay;
		ar >> mSquadSize;

		char n=-1;
		do
		{
			n++;
			ar >> mSpawnUnit[n];
		}
		while (mSpawnUnit[n]);

		n=-1;
		do
		{
			n++;
			ar >> mSpawnerSpawnScript[n];
		}
		while (mSpawnerSpawnScript[n]);
	}
}

void CCutsceneInitThing::Save(
	CArchive	&ar)
{
	CInitThing::Save(ar);

	char n=-1;
	do
	{
		n++;
		ar << mFile[n];
	}
	while (mFile[n]);

	n=-1;
	do
	{
		n++;
		ar << mLinkTo[n];
	}
	while (mLinkTo[n]);
}

void CCutsceneInitThing::Load(
	CArchive	&ar,
	short		inVersion)
{
	CInitThing::Load(ar,inVersion);

	if (inVersion<=16)
	{
		char	length;

		ar >> length;

		for (int n=0; n<length; n++)
			ar >> mFile[n];
	}
	else
	{
		char n=-1;
		do
		{
			n++;
			ar >> mFile[n];
		}
		while (mFile[n]);

		if (inVersion>=32)
		{
			n=-1;
			do
			{
				n++;
				ar >> mLinkTo[n];
			}
			while (mLinkTo[n]);
		}
	}
}

void CStartInitThing::Save(
	CArchive	&ar)
{
	CInitThing::Save(ar);

	ar << mPlaneMode;
	ar << mPlayerNumber;
}

void CStartInitThing::Load(
	CArchive	&ar,
	short		inVersion)
{
	CInitThing::Load(ar,inVersion);

	if (inVersion>14)
		ar >> mPlaneMode;
	
	if (inVersion>25)
		ar >> mPlayerNumber;
}

void CTreeInitThing::Save(
	CArchive	&ar)
{
	CInitThing::Save(ar);

	char n=-1;
	do
	{
		n++;
		ar << mTreeType[n];
	}
	while (mTreeType[n]);
}

void CTreeInitThing::Load(
	CArchive	&ar,
	short		inVersion)
{
	CInitThing::Load(ar,inVersion);

	if (inVersion>17)
	{
		char n=-1;
		do
		{
			n++;
			ar >> mTreeType[n];
		}
		while (mTreeType[n]);
	}
}

void CSquadInitThing::Save(
	CArchive	&ar)
{
	CInitThing::Save(ar);

	ar << mAmount;
	ar << mMode;
}

void CSquadInitThing::Load(
	CArchive	&ar,
	short		inVersion)
{
	CInitThing::Load(ar,inVersion);

	ar >> mAmount;

	if (inVersion>28)
		ar >> mMode;
}

void CWallInitThing::Save(
	CArchive	&ar)
{
	CInitThing::Save(ar);

	ar << mLength;
	ar << mLife;

	int n=-1;
	do
	{
		n++;
		ar << mWallType[n];
	}
	while (mWallType[n]);
}

void CWallInitThing::Load(
	CArchive	&ar,
	short		inVersion)
{
	CInitThing::Load(ar,inVersion);

	ar >> mLength;

	if (inVersion>=31)
	{
		ar >> mLife;

		int n=-1;
		do
		{
			n++;
			ar >> mWallType[n];
		}
		while (mWallType[n]);
	}
}

void CSphereTriggerInitThing::Save(
	CArchive	&ar)
{
	CInitThing::Save(ar);

	ar << mRadius;
}

void CSphereTriggerInitThing::Load(
	CArchive	&ar,
	short		inVersion)
{
	CInitThing::Load(ar,inVersion);

	ar >> mRadius;
}
#endif

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x0048dbe0 ?Copy@CSphereTriggerInitThing@@UAEXPAVCInitThing@@@Z
