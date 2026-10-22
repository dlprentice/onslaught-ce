// SpawnerThng.cpp: the CSpawnerThing actor and CSpawner monitor, reconstructed from the pristine PC build.
#include "common.h"
#include "SpawnerThng.h"
#include "squad.h"
#include "eventmanager.h"
#include "WorldMeshList.h"
#include "ThingType.h"
#include "mapwhoentry.h"
#include "mapwho.h"
#include "DebugLog.h"
#include "game.h"
#include "world.h"

// FUNCTION: 0x004e2f20 init:ZERO_FVECTOR
// FUNCTION: 0x004e2f40 init:ID_FMATRIX

// Reconstructed inline interfaces; their original source names are not recovered. They express the by-name
// factory and the squad's +0x80 assembly flag writes without changing either operation. Their call nesting makes
// Update (004e3370) and the emitted GetUnitIndex (004e36c0) match the complete retail sections.
static inline CThing *SpawnThingByName(char *name)
{
	return UPhysicsManager::SpawnThing(UPhysicsManager::GetUnitIndex(name));
}

static inline void SetSquadAssemblyState(CSquad *squad, BOOL assembling)
{
	squad->mUnknown80 = assembling;
}

void CSpawnerThing::Init(CInitThing *init)	// FUNCTION: 0x004e3010
{
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	mFlags &= ~TF_IN_MAP_WHO;
	const FVector &initPos = init->mPos; mInit.mPos = initPos;	// read through a reference: the later loads stay behind this store
	mInit.mOrientationType = init->mOrientationType;
	mInit.mYaw = init->mYaw;
	mInit.mPitch = init->mPitch;
	mInit.mOrientation = init->mOrientation;
	mInit.mRoll = init->mRoll;
	mInit.mAllegiance = init->mAllegiance;
	mInit.mTarget = init->mTarget;
	mInit.mActive = init->mActive;
	mInit.mAttachScriptsToUnits = init->mAttachScriptsToUnits;
	mInit.mActive = TRUE;
	strcpy(mInit.mScript, init->mSpawnScript);
	strcpy(mInit.mSpawnScript, ((CSpawnerInitThing *)init)->mSpawnerSpawnScript);
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	SUPERTYPE::Init(init);
	if (init->mActive)
		mUnknownA4 = FALSE;
	else
		mUnknownA4 = TRUE;
	mInitialDelay = ((CSpawnerInitThing *)init)->mInitialDelay;
	mDelay = ((CSpawnerInitThing *)init)->mDelay;
	mSquadDelay = ((CSpawnerInitThing *)init)->mSquadDelay;
	mAmount = ((CSpawnerInitThing *)init)->mAmount;
	mSquadSize = ((CSpawnerInitThing *)init)->mSquadSize;
	mUnknown98 = 0;
	mUnknown9C = 0;
	mUnknown80 = 0;
	mUnknownA0 = mAmount < 0;
#line 67
	mSpawnUnit = new(MT_SQUAD) char[strlen(((CSpawnerInitThing *)init)->mSpawnUnit) + 1];
	strcpy(mSpawnUnit, ((CSpawnerInitThing *)init)->mSpawnUnit);
	if (!mUnknownA4 && mAmount)
	{
		EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + mInitialDelay);
		mUnknown80 = EVENT_MANAGER.GetTime() + mInitialDelay;
	}
	mSquad = NULL;
	CUnitData *data = UPhysicsManager::GetUnitData(mSpawnUnit);
	if (data && !UPhysicsManager::IsSquadBehaviour(data->mBehaviour))
	{
		mAmount *= mSquadSize;
		mSquadSize = 1;
	}
	CWorldMeshList::Add(mSpawnUnit);
	EVENT_MANAGER.AddEvent(3000, this, (float)NEXT_FRAME);
}

void CSpawnerThing::Shutdown()	// FUNCTION: 0x004e3330
{
	delete [] mSpawnUnit;
	mSpawnUnit = NULL;
	if (mSquad != NULL)
	{
		SetSquadAssemblyState(mSquad.ToRead(), FALSE);
		mSquad = NULL;
	}
	SUPERTYPE::Shutdown();
}

// Event 3000: once its delay has passed, it spawns a unit, gathering units into a squad of mSquadSize before the
// squad delay; it then waits again. The by-name factory and squad-state setter preserve retail's inline nesting,
// including the GetUnitIndex call at 004e33d0 and the two out-of-line lookup Next calls.
void CSpawnerThing::Unknown4e3370()	// FUNCTION: 0x004e3370
{
	if ((!mUnknownA0) && (mUnknown98 >= mAmount))
		return;

	if ((mUnknown80 < EVENT_MANAGER.GetTime()) && (!mUnknownA4))
	{
		CThing	*thing = SpawnThingByName(mSpawnUnit);
		if (thing)
		{
			CUnitInitThing	init;
			init.Copy(&mInit);
			init.mStats = UPhysicsManager::GetUnitData(mSpawnUnit);
			thing->Init(&init);
			mUnknown9C++;
			if (!mSquad.ToRead())
			{
				CSquadInitThing	sinit;
				mSquad = UPhysicsManager::SpawnSquad(UPhysicsManager::GetUnitData(mSpawnUnit)->mBehaviour);
				if (mSquad.ToRead())
				{
					sinit.mPos = init.mPos;
					sinit.mAllegiance = init.mAllegiance;
					strcpy(sinit.mUnitName, mSpawnUnit);
					SetSquadAssemblyState(mSquad.ToRead(), TRUE);
					mSquad->Init(&sinit);
				}
			}
			if (mSquad.ToRead())
				mSquad->Unknown67((CUnit *)thing, 0, 0);
			if (mUnknown9C == mSquadSize)
			{
				mUnknown9C = 0;
				mUnknown98++;
				if (mSquad.ToRead())
				{
					SetSquadAssemblyState(mSquad.ToRead(), FALSE);
					mSquad = NULL;
				}
				mUnknown80 = EVENT_MANAGER.GetTime() + mSquadDelay;
			}
			else
				mUnknown80 = EVENT_MANAGER.GetTime() + mDelay;
		}
	}
	if (!(mUnknown80 > EVENT_MANAGER.GetTime()))
		mUnknown80 = EVENT_MANAGER.GetTime() + 0.1f;
	EVENT_MANAGER.AddEvent(3000, this, mUnknown80, 0, NULL, NULL);
}

// FUNCTION: 0x004e36c0 ?GetUnitIndex@UPhysicsManager@@SAHPAD@Z

void CSpawnerThing::Activate()	// FUNCTION: 0x004e3750
{
	mUnknownA4 = FALSE;
}

void CSpawnerThing::Deactivate()	// FUNCTION: 0x004e3760
{
	mUnknownA4 = TRUE;
	if (mUnknown9C > 0)
	{
		mUnknown9C = 0;
		mUnknown98++;
		if (mSquad != NULL)
		{
			SetSquadAssemblyState(mSquad.ToRead(), FALSE);
			mSquad = NULL;
		}
	}
}

void CSpawnerThing::HandleEvent(CEvent *event)	// FUNCTION: 0x004e37a0
{
	switch (event->GetEventNum())
	{
		case 3000: Unknown4e3370(); break;
		default: SUPERTYPE::HandleEvent(event); break;
	}
}

void CSpawnerThing::SetSpawnScript(char *script)	// FUNCTION: 0x004e37c0
{
	strcpy(mInit.mScript, script);
}

CSpawner::CSpawner(CSpawnerData *data, SINT a)	// FUNCTION: 0x004e37f0
{
	mData = data;
	mUnknown3F0 = a;
	mUnknown3D8 = 0;
	mUnknown3E0 = 0;
	mUnknown3E4 = TRUE;
	mMount = 0;
	mUnitIndex = UPhysicsManager::GetUnitIndex(mData->mThingName);
	mOwner = NULL;
	mSquad = NULL;
	mUnknown0C = 0;
	mUnknown3EC = 0;
	mEnabled = TRUE;
	if (!UPhysicsManager::IsSquadBehaviour(UPhysicsManager::GetUnitData(mData->mThingName)->mBehaviour))
		mData->mSquadSize = 1;
}

// FUNCTION: 0x004e39f0 ??_GCSpawner@@UAEPAXI@Z

CSpawner::~CSpawner()	// FUNCTION: 0x004e3a10
{
	if (mSquad != NULL)
	{
		SetSquadAssemblyState(mSquad.ToRead(), FALSE);
		mSquad = NULL;
	}
}

void CSpawner::Shutdown()	// FUNCTION: 0x004e3aa0
{
	UpdateSpawnCount();
	delete this;
}

// The score the spawner's units are still worth: its unspawned amount by squad size, by the unit's priority, against
// the player's side. Written braced, it is 175 size units, one over the 174 to which VC6 offers a plain function for
// inlining, so Shutdown calls it out of line (004e3aa3) as retail does; the compact form (169) was expanded there.
void CSpawner::UpdateSpawnCount()	// FUNCTION: 0x004e3ac0
{
	if (!mData)
		return;
	SINT	weight = 0;
	switch (mInit.mAllegiance)
	{
	case kForsetiAllegiance:
	{
		weight = -UPhysicsManager::GetBehaviourPriority(UPhysicsManager::GetUnitData(mData->mThingName));
		break;
	}
	case kMuspellAllegiance:
	{
		weight = UPhysicsManager::GetBehaviourPriority(UPhysicsManager::GetUnitData(mData->mThingName));
		break;
	}
	}
	if (mUnknown3D8 > mData->mAmount)
	{
		LOG.AddMessage("Spawned more than we should be able to!!!");
	}
	GAME.IncScore((mData->mAmount - mUnknown3D8) * mData->mSquadSize * weight);
}

// Source-local query and spawn-step interfaces; original helper names are not recovered.
static inline BOOL IsSpawnBehaviour(const CSpawner &spawner, SINT behaviour)
{
	if (UPhysicsManager::GetUnitData(spawner.mUnitIndex)->mBehaviour == behaviour)
		return TRUE;
	return FALSE;
}

static inline FVector GetSquadSpawnOffset(const FMatrix &orientation)
{
	return FVector(orientation.Row[0].Y * 15.f, orientation.Row[1].Y * 15.f, orientation.Row[2].Y * 15.f);
}

static inline FVector GetSquadSpawnOffset(const CSquadInitThing &init)
{
	return GetSquadSpawnOffset(init.mOrientation);
}

static inline CSquad *SpawnSquadByUnitName(char *name)
{
	return UPhysicsManager::SpawnSquad(UPhysicsManager::GetUnitData(name)->mBehaviour);
}

// Starts a wave at the owner's emitter (or the owner itself), with the squad 15 ahead along its facing.
// Returning the offset into this caller preserves the constructor at 004e3e0c and the scalar add order.
// The by-name squad factory preserves both called lookup iterators; the existing reader reloads remain.
BOOL CSpawner::DoSpawn()	// FUNCTION: 0x004e3c60
{
	if ((!mEnabled) || (mUnknown3EC) || (!(EVENT_MANAGER.GetTime() > mUnknown3E0)) || (Unknown4e4430()) ||
		(mUnitIndex == -1) || (!mData))
		return FALSE;

	CSquadInitThing	sinit;
	mOwner->GetEmitter(mMount, mUnknown3E4, sinit.mPos, sinit.mOrientation);
	if (sinit.mPos == FVector(0, 0, 0))
	{
		mUnknown3E4 = TRUE;
		mOwner->GetEmitter(mMount, 1, sinit.mPos, sinit.mOrientation);
		if (sinit.mPos == FVector(0, 0, 0))
		{
			sinit.mPos = mOwner->GetPos();
			sinit.mOrientation = mOwner->GetOrientation();
		}
	}
	sinit.mPos += GetSquadSpawnOffset(sinit);
	sinit.mAllegiance = mInit.mAllegiance;
	strcpy(sinit.mUnitName, mData->mThingName);
	if (!mInit.mAttachScriptsToUnits)
		strcpy(sinit.mScript, mInit.mSpawnScript);
	mSquad = SpawnSquadByUnitName(mData->mThingName);
	if (mSquad.ToRead())
	{
		SetSquadAssemblyState(mSquad.ToRead(), TRUE);
		mSquad->Init(&sinit);
	}
	mUnknown0C = 0;
	mUnknown3EC = 1;
	mUnknown3D8++;
	Unknown4e3f90();
	return TRUE;
}

static inline void SetSpawnUnitStats(CUnitInitThing &init, SINT unitIndex)
{
	init.mStats = UPhysicsManager::GetUnitData(unitIndex);
}

// Event 3000: spawns the wave's next unit at the emitter once the ground there (for a non-ground owner, its grid) and
// the position are clear, adds it to the squad and ends the wave at the squad size; otherwise it waits the delay.
// The stats-assignment interface preserves the called lookup First (004e422e) and inline Next. The factory's
// index is a value argument; the stats lookup independently rereads mUnitIndex after the init copy and angles.
// The grid test reads the emitter position through a reference, as retail does through ebx (004e40b6).
void CSpawner::Unknown4e3f90()	// FUNCTION: 0x004e3f90
{
	if (!mUnknown3EC)
		return;

	mOwner->GetEmitter(mMount, mUnknown3E4, mInit.mPos, mInit.mOrientation);
	if (mInit.mPos == FVector(0, 0, 0))
	{
		mUnknown3E4 = TRUE;
		mOwner->GetEmitter(mMount, 1, mInit.mPos, mInit.mOrientation);
		if (mInit.mPos == FVector(0, 0, 0))
		{
			mInit.mPos = mOwner->GetPos();
			mInit.mOrientation = mOwner->GetOrientation();
		}
	}
	// `goto placed` leaves the two-loop grid scan as Stuart's XBoxMemoryCard.cpp:745 does (`goto save_found`), and `goto wait` skips
	// to the one timer re-arm at the end as his DXEngine.cpp:687 skips (`goto dont_do_lods_now`). Goto-free forms are not better:
	// flags are 3 bytes off (the outer loop's closing test comes out inverted), returning after the AddEvent 626, flags in the
	// loop conditions 751 to 961.
	if (!mOwner->IsOnGround())
	{
		COccupancyGrid	*grid = mOwner->GetOccupancyGrid();
		FVector	&p = mInit.mPos;
		SINT	cx = (SINT)p.X >> 1;
		SINT	cy = (SINT)p.Y >> 1;
		for (SINT y = cy - 1; y < cy + 1; y++)
		{
			if ((y >= 0) && (y < 256))
			{
				for (SINT x = cx - 1; x < cx + 1; x++)
				{
					if ((x >= 0) && (x < 256))
					{
						if (!(grid->mData[(x >> 3) * 256 + y] & (1 << (x % 8))))
						{
							if (UPhysicsManager::CanSpawnerMake(mData->mThingName))
								goto wait;
							goto placed;
						}
					}
				}
			}
		}
	}
placed:
	if (IsSpawnPositionClear(mInit.mPos))
	{
		SINT unitIndex = mUnitIndex;
		CThing	*thing = UPhysicsManager::SpawnThing(unitIndex);
		if (thing)
		{
			mUnknown3E4++;
			CUnitInitThing	init;
			init.Copy(&mInit);
			FVector	dir = init.mOrientation.GetColumnY();
			init.mYaw = dir.Azimuth();
			init.mPitch = dir.Elevation();
			init.mInitCST.mTimeBeforeStart = 1.5f;
			SetSpawnUnitStats(init, mUnitIndex);
			init.mSpawnedBy = mOwner;
			switch (mMount)
			{
			case 10: init.mWaypointPath = (EEmitterType)15; break;
			case 11: init.mWaypointPath = (EEmitterType)16; break;
			case 12: init.mWaypointPath = (EEmitterType)17; break;
			case 13: init.mWaypointPath = (EEmitterType)18; break;
			case 14: init.mWaypointPath = (EEmitterType)19; break;
			}
			if (mInit.mAttachScriptsToUnits)
				strcpy(init.mScript, mInit.mSpawnScript);
			thing->Init(&init);
			((CUnit *)thing)->SetStateTimer(mData->mSeekDelay);
			if (mSquad.ToRead())
				mSquad->Unknown67((CUnit *)thing, 0, 0);
			mUnknown0C++;
			if (mUnknown0C >= mData->mSquadSize)
			{
				if (mSquad.ToRead())
					SetSquadAssemblyState(mSquad.ToRead(), FALSE);
				mSquad = NULL;
				mUnknown3E0 = EVENT_MANAGER.GetTime() + mData->mSquadDelay;
				mUnknown3EC = 0;
				return;
			}
		}
	}
wait:
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + mData->mDelay);
}

BOOL CSpawner::Unknown4e43d0()	// FUNCTION: 0x004e43d0
{
	if (mEnabled && !mUnknown3EC && EVENT_MANAGER.GetTime() > mUnknown3E0 && mData &&
		((mUnknown3D8 < mData->mAmount) || mData->mInfinite))
		return TRUE;
	return FALSE;
}

BOOL CSpawner::Unknown4e4420()	// FUNCTION: 0x004e4420
{
	return mUnknown3EC != 0;
}

// DoSpawn calls it out of line (004e3c9b).
#pragma auto_inline(off)
BOOL CSpawner::Unknown4e4430()	// FUNCTION: 0x004e4430
{
	if (!mData || ((mUnknown3D8 >= mData->mAmount) && !mData->mInfinite))
		return TRUE;
	return FALSE;
}
#pragma auto_inline(on)

void CSpawner::HandleEvent(CEvent *event)	// FUNCTION: 0x004e4460
{
	if (event->GetEventNum() == 3000)
		Unknown4e3f90();
}

BOOL CSpawner::Unknown4e4480(CThing *target)	// FUNCTION: 0x004e4480
{
	if (mEnabled && mData && ((mUnknown3D8 < mData->mAmount) || mData->mInfinite) &&
		(target->GetThingType() & mUnknown3F0))
		return TRUE;
	return FALSE;
}

// The behaviour query owns the index lookup and its comparison. This reconstructed interface preserves
// retail's out-of-line First (004e44e3) while Next expands; the original helper name is unknown.
// Otherwise the spawn position is clear when no other qualifying unit's centre is within 6 of it.
BOOL CSpawner::IsSpawnPositionClear(FVector &pos)	// FUNCTION: 0x004e44d0
{
	if (IsSpawnBehaviour(*this, 3))
		return TRUE;
	for (CMapWhoEntry *entry = MAP_WHO.GetFirstEntryWithinRadius(pos, 1.0f); entry; entry = MAP_WHO.GetNextEntryWithinRadius())
	{
		CThing	*thing = entry->GetThing();
		if ((thing) && (thing->IsA(THING_TYPE_UNIT)) && (!(thing->GetThingType() & (THING_TYPE_COMPONENT | THING_TYPE_EMPLACEMENT | THING_TYPE_INFANTRY | THING_TYPE_MECH | THING_TYPE_BUILDING))) && (thing != mOwner))
		{
			FVector	d = thing->GetPos() - pos;
			if (d.MagnitudeSq() < 36.0f)
				return FALSE;
		}
	}
	return TRUE;
}
