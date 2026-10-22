// unit.cpp: reconstructed from unit.obj (004f7da0..004fe61f); Stuart's release has no unit.cpp. CUnit's layout
// and slots are in unit.h.
#include "common.h"
#include "SpawnerThng.h"
#include "unit.h"
#include "Script.h"
#include "weapon.h"
#include "map.h"
#include "component.h"
#include "squad.h"
#include "eventmanager.h"
#include "game.h"
#include "music.h"
#include "mathsconst.h"
#include "world.h"
#include "mesh.h"
#include "GeneralVolume.h"
#include "DebugLog.h"
#include "MessageBox.h"
#include "text.h"
#include "frontend.h"
#include "InitThing.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "UnitAI.h"
#include "DestructableSegmentsController.h"
#include "round.h"
#include "Line.h"
#include "WorldMeshList.h"
#include "BattleEngine.h"
#include "player.h"
#include "maths_integer.h"
#include "Guide.h"

#define UNIT_EVENT_4001	4001	// re-aims the unit (Unknown4fb280); the names are open
#define UNIT_EVENT_4003	4003	// notes whether a player's camera is within 50 units
#define UNIT_EVENT_4004	4004	// explodes and shuts the unit down
#define UNIT_EVENT_4005	4005	// a unit event (Unknown107 schedules it once); the name is open

SINT	CUnit::mGuideUpdateCounter = 0;	// 00854d90 (CGame::Update 0046eb31 writes it)

// FUNCTION: 0x004f7da0 init:ZERO_FVECTOR
// FUNCTION: 0x004f7dc0 init:ID_FMATRIX

// FUNCTION: 0x004f84a0 ?_GetClassName@CUnit@@UAEPADXZ
// FUNCTION: 0x004f84b0 ?GetMaxVelocity@CUnit@@UAEMXZ
// FUNCTION: 0x004f84c0 ??_GCUnit@@UAEPAXI@Z


// The mission script (only its call here).

// A side's safe area (the world's set +0xd0; its object's functions, only the slot used here).
class CSafeSide : public CComplexThing
{
public:
	virtual SINT	GetAllegiance();										// 66 (004bfc10)
};

// The unit's AI (only what these functions touch).


//******************************************************************************************
CUnit::CUnit()	// FUNCTION: 0x004f7e90
{
	mUnknown1F0 = 0;
	mUnknown1E8 = 0;
	mUnknown1EC = 0;
	mUnknown168 = 0;
	mUnknown16C = 0;
	mUnknown10C = 0;
	mLife = 1.0f;
	mEnergy = 0;
	mShields = 0;
	mMaxShields = 0;
	mShieldRegeneration = 0;
	mVulnerable = TRUE;
	mUnknown110 = 0;
	FMatrix	ori;
	mSegments = NULL;
	mAI = NULL;
	mCurrentWeapon = NULL;
	mCurrentSpawner = NULL;
	mSquad = NULL;
	mUnknown1AC = NULL;
	mUnknown174 = 0;
	mUnknown170 = 0;
	mUnknown1EC = 0;
	ori.MakeRotation(0, 0, 0);
	mCurrentOrientation = ori;
	ori.MakeRotation(0, 0, 0);
	mUnknown12C = ori;
	mInfinateEnergy = FALSE;
	mUnitData = NULL;
	mUnknown170 = 0;
	mUnknown174 = 0;
	mUnknown14C = FVector(0, 0, 0);
	mVulnerable = TRUE;
	mAllegiance = kNeutralAllegiance;
	mUnknown1F8 = FVector(0, 0, 0);
	mUnknown20C = 0;
	mUnknownE0 = 0;
	mUnknownE4 = 0;
	mUnknownE8 = 0;
	mUnknownF0 = 0;
	mUnknownEC = 0;
	mUnknownF4 = 0;
	mGuide = NULL;
	mAIState = AI_ON;
	mActive = TRUE;
	mUnknown1F4 = NULL;
	mUnknown220 = 0;
	mThrustersActive = 0;
	mUnknown218 = 0;
	mUnknown21C = 10;
	mUnknown24C = 1;
}

//******************************************************************************************
// Angles in 4096ths of a turn, rolled, then pitched, then yawed (maths.h). Retail's own copy, not a call of
// MakeRotationCore (maths_integer.h, NamedMesh's inline body): an expansion here would spend the core's size (343)
// first and leave 375 for the first product (594), so both products stayed calls (640 bytes); retail expands the
// first and calls the second. The pitch angle is the expression in each use, not a float local: the common
// subexpression is kept on the FPU stack (fst, fcos) where a local is stored and reloaded (fstp, fld).
// STAND-IN (a labelled explicit concession, admitted 2026-10-03; listed in the README under "Matches and
// steps that rely on a labelled stand-in"). What: three trig results are named floats with BRACKETED initialisers,
// `sr` (Row[0]'s sine), `cp` and `sp` (the pitch rows' first cosine and sine). Why: retail's term order in the three
// dot-product sums of the inline pitch product needs three more IL nodes than the plain body has. The plain body
// (each call written in place) compiles to the same instructions with two sums' operands swapped: shape 6, 24
// instructions, 25 bytes differ. How: a bracketed initialiser adds one IL node that emits no code (the brackets change
// nothing the program computes), and the three extra nodes move the operand ranks that order those sums. Of the 27
// plain/named/named-bracketed combinations at the three sites only all-bracketed matches; bare brackets around the
// calls also match (24 of 511 placements), unbracketed named floats and `(float)cos`/`cosf` mixes do not (256 tried).
// Concession: Stuart's released text never brackets a lone call like this, and his source for this function is lost
// (his release has no Unit.cpp and no maths source), so this spelling is ours, chosen for the compiler, not recovered.
void FMatrix::MakeRotation(SINT yaw, SINT pitch, SINT roll)	// FUNCTION: 0x004f8140
{
	float	r = roll * (PI_M2 / 4096);
	float	sr = (sinf(r));		// stand-in brackets (see above)
	Row[0] = FVector(cosf(r), 0.f, sr);
	Row[1] = FVector(0.f, 1.f, 0.f);
	Row[2] = FVector(-sinf(r), 0.f, cosf(r));

	FMatrix	p;
	p.Row[0] = FVector(1.f, 0.f, 0.f);
	float	cp = (cosf(pitch * (PI_M2 / 4096)));		// stand-in brackets
	p.Row[1] = FVector(0.f, cp, -sinf(pitch * (PI_M2 / 4096)));
	float	sp = (sinf(pitch * (PI_M2 / 4096)));		// stand-in brackets
	p.Row[2] = FVector(0.f, sp, cosf(pitch * (PI_M2 / 4096)));
	*this = p * *this;

	float	yw = yaw * (PI_M2 / 4096);
	p.Row[0] = FVector((float)cos(yw), -(float)sin(yw), 0.f);
	p.Row[1] = FVector((float)sin(yw), (float)cos(yw), 0.f);
	p.Row[2] = FVector(0.f, 0.f, 1.f);
	*this = p * *this;
}

//******************************************************************************************
CUnit::~CUnit()	// FUNCTION: 0x004f84e0
{
	if (mAI)
	{
		delete mAI;
		mAI = NULL;
	}

	if (mUnknown1AC)
	{
		mUnknown1AC->Stop(FALSE);
		delete mUnknown1AC;
		mUnknown1AC = NULL;
	}

	CParticleEffectLink	*link;
	ListIterator<CParticleEffectLink>	thrusters(&mThrusters);
	while (link = thrusters.First())
	{
		mThrusters.Remove(link);
		link->Stop(FALSE);
		delete link;
	}
	ListIterator<CParticleEffectLink>	links1B4(&mUnknown1B4);
	while (link = links1B4.First())
	{
		mUnknown1B4.Remove(link);
		link->Stop(FALSE);
		delete link;
	}
	ListIterator<CParticleEffectLink>	links1C4(&mUnknown1C4);
	while (link = links1C4.First())
	{
		mUnknown1C4.Remove(link);
		link->Stop(FALSE);
		delete link;
	}
}

//******************************************************************************************
// Matched with two labelled stand-ins. The barrel and turret loop's registers follow the order the
// allocator colours `this` and the inlined strcmp's two walking pointers (both priority 200): when `this` pops first it
// takes ebx and the walkers esi/edi, part takes ebp, and the name address is formed afresh for each compare (lea
// esi/eax/esi,[ebp+0xdc], 004f8927..004f897f) as in retail. With plain reads `this` is 181 and loses.
// - Natural: the emitter test reads GetUnitData() (+4: the accessor's result is a counted node in that block).
// - STAND-IN 1 (register choice, no code): the emitter type is stored through the file-local reader MountOf. Its
//   result is a coloured web instead of a chooser pick, so the chooser's eax/ecx/edx cursor reaches the next picks as
//   retail's does (eax at 004f8877, edx for the mesh vtable, eax for the unit data), and the weapon block counts one
//   more node (+8 for `this`).
// - STAND-IN 2 (allocation order, no code): the component loop's orientation test reads the init through the
//   file-local reader OrientationTypeOf: one more counted node in a block that references `this` (+8), which puts
//   `this` (201) above the walkers. Without it, or without stand-in 1 or GetUnitData(), the function is 37/105 or
//   37/109 (shape/instructions; main had 37/113).
// Both readers return the field they name and emit nothing of their own; original spellings unknown.
static inline SINT MountOf(CUnitWeapon *uw) { return uw->mMount; }
static inline EOrientationType OrientationTypeOf(CInitThing *init) { return init->mOrientationType; }
void CUnit::Init(CInitThing *init)	// FUNCTION: 0x004f86d0
{
	init->mInitCST.mIgnoreThing = init->mSpawnedBy;
	mUnitData = ((CUnitInitThing *)init)->mStats;
	mUnknown224 = 0;

	if (mUnitData)
	{
		// the mesh
		if (mUnitData->mBehaviour != 7)
		{
#line 192
			CRenderData	*data = new(MEMTYPE_SCRATCHPAD) CRenderData;
			CInitRenderThing	*rinit = &data->mInit[0];
			data->mInit[0].mThing = this;
			data->mRTID[0] = RTID_CRTMesh;
			if (mUnitData->mUnknown2c)
				strcpy(rinit->mName, mUnitData->mUnknown2c);
			if ((mRenderThing = SpawnRenderThing(data->mRTID[0])))
				mRenderThing->Init(rinit);
			delete data;
		}

		// the weapons, and the turret and barrel parts they turn
		ListIterator<CUnitWeapon>	weapons(&mUnitData->mWeapons);
		for (CUnitWeapon *uw = weapons.First(); uw; uw = weapons.Next())
		{
			CWeapon	*weapon = UPhysicsManager::SpawnWeapon(uw->mIndex, uw->mUnknown04);
			if (weapon)
			{
				CInitEquipment	ie;
				ie.mAttachedTo = this;
				ie.mNumber = 1;
				ie.mMount = uw->mMount;
				weapon->Init(ie);
				weapon->mUnknownAC = MountOf(uw);			// STAND-IN 1 (see above)

				CMesh	*mesh;
				if ((mRenderThing) && (mesh = mRenderThing->GetRTMesh()) && (GetUnitData()->mUnknownBC > 0))
				{
					CMeshPart	*part;
					switch (uw->mMount)
					{
						case 1:	part = mesh->GetEmitterSubpart("GunA", 1);	break;
						case 2:	part = mesh->GetEmitterSubpart("GunB", 1);	break;
						case 3:	part = mesh->GetEmitterSubpart("GunC", 1);	break;
						case 4:	part = mesh->GetEmitterSubpart("GunD", 1);	break;
						case 5:	part = mesh->GetEmitterSubpart("GunE", 1);	break;
						case 6:	part = mesh->GetEmitterSubpart("GunF", 1);	break;
						case 7:	part = mesh->GetEmitterSubpart("GunG", 1);	break;
						case 8:	part = mesh->GetEmitterSubpart("GunH", 1);	break;
						case 9:	part = mesh->GetEmitterSubpart("GunI", 1);	break;
					}

					while (part)
					{
						if ((!strcmp(part->mName, "turret")) || (!strnicmp(part->mName, "X1 Turret", 9)))
							weapon->mUnknown94 = TRUE;
						else if ((!strncmp(part->mName, "barrel", 6)) || (!strnicmp(part->mName, "X1 Barrel", 9)))
						{
							mUnknown224 = TRUE;
							weapon->mUnknown98 = TRUE;
							mUnknownF4 = CEulerAngles(part->mOrientation).mPitch;
							mUnknown220 = (SINT)part;
						}
						part = part->mParent;
					}
				}
				mWeapons.Append(weapon);
			}
		}

		// the spawners
		ListIterator<CSpawnerReference>	spawners(&mUnitData->mSpawners);
		for (CSpawnerReference *sr = spawners.First(); sr; sr = spawners.Next())
		{
			CSpawner	*spawner = UPhysicsManager::SpawnSpawner(sr->mSpawnerIndex, sr->mUnknown04);
			if (spawner)
			{
				spawner->mInit.Copy(init);
				spawner->mInit.mTarget = 0;
				spawner->mInit.mOrientationType = EULER_ANGLES;
				spawner->mInit.mRoll = 0;
				spawner->mInit.mActive = TRUE;
				strcpy(spawner->mInit.mScript, "");
				strcpy(spawner->mInit.mName, "");
				SINT	mount = sr->mMount;
				spawner->mOwner = this;
				spawner->mMount = mount;
				mSpawners.Append(spawner);
			}
		}

		mLife = mUnitData->mUnknownC0;
	}

	SUPERTYPE::Init(init);

	if (init->mOrientationType == 0)
	{
		CEulerAngles	ori;
		ori.mYaw = init->mYaw;
		ori.mPitch = init->mPitch;
		ori.mRoll = init->mRoll;
		mCurrentOrientation = ori;
		mUnknown120 = ori;
	}

	if (mUnitData)
	{
		FVector		pos;
		FMatrix		ori;

		// the components, each built at its emitter
		ListIterator<CUnitComponentReference>	components(&mUnitData->mComponents);
		for (CUnitComponentReference *uc = components.First(); uc; uc = components.Next())
		{
			CComponent	*component = UPhysicsManager::SpawnCharacter(uc->mIndex);
			if (component)
			{
#line 309
				CActiveReader<CUnit>	*reader = new(MEMTYPE_ACTIVE_READER) CActiveReader<CUnit>;
				reader->SetReader(component);
#line 313
				CUnitInitThing	*ci = new(MEMTYPE_SCRATCHPAD) CUnitInitThing;

				GetEmitter(20, uc->mUnknown04, pos, ori);
				ci->mPos = pos;
				if (OrientationTypeOf(init) == EULER_ANGLES)		// STAND-IN 2 (see above)
				{
					ci->mOrientationType = EULER_ANGLES;
					CEulerAngles	ea(ori);
					ci->mYaw = ea.mYaw;
					ci->mPitch = ea.mPitch;
					ci->mRoll = ea.mRoll;
				}
				ci->mAllegiance = init->mAllegiance;
				ci->mStats = UPhysicsManager::GetComponentData(uc->mIndex);
				ci->mActive = init->mActive;
				ci->mAttachScriptsToUnits = init->mAttachScriptsToUnits;
				strcpy(ci->mSpawnScript, init->mSpawnScript);

				component->Init(ci);
				component->Unknown428b50(this, uc->mUnknown04);
				mChildren.Append(reader);
				delete ci;
			}
		}

#line 347
		mUnknown1AC = new(MEMTYPE_PARTICLE) CParticleEffectLink;
		mUnknown1AC->mEffect = NULL;

		SINT		n = 1;
		if (mUnitData->mParticles[6])
		{
			while (mRenderThing->GetRTEmitter("Dust", n, pos, ori, TRUE, FALSE), pos != FVector(0, 0, 0))
			{
#line 356
				mUnknown1B4.Append(new(MEMTYPE_PARTICLE) CParticleEffectLink);
				n++;
			}
		}

		n = 1;
		if (mUnitData->mParticles[7])
		{
			while (mRenderThing->GetRTEmitter("Vent", n, pos, ori, TRUE, FALSE), pos != FVector(0, 0, 0))
			{
#line 367
				mUnknown1C4.Append(new(MEMTYPE_PARTICLE) CParticleEffectLink);
				n++;
			}
		}
	}

	WORLD.mUnitNB.Add(this);
	mAllegiance = init->mAllegiance;

	// a turret's pivot (the flag is never set, but it keeps the loop tested at the top as retail's is, 004f8fc2)
	BOOL	done = FALSE;
	SINT	i = 0;
	while ((!done) && (i < mRenderThing->GetRTMesh()->mNumParts))
	{
		if (!strcmp("turret", mRenderThing->GetRTMesh()->mParts[i]->mName))
			mUnknown1F8 = mRenderThing->GetRTMesh()->mParts[2]->mOffsetPos;
		i++;
	}

	if (mSquad.ToRead())
		mSquad->Unknown68(this);
	if (mSegments)
		mSegments->Init();

	if (mUnitData)
	{
		CWorldMeshList::MarkUsed(mUnitData->mName);
		switch (mAllegiance)
		{
		case 0:	WORLD.mCountersA[mUnitData->mBehaviour]++;	break;
		case 1:	WORLD.mCountersB[mUnitData->mBehaviour]++;	break;
		}
	}

	Unknown4fb280(NULL);

	// a nexus or a weak point
	mUnknown228 = FALSE;
	mUnknown22C = FALSE;
	i = 0;
	while ((!done) && (i < mRenderThing->GetRTMesh()->mNumParts))
	{
		CMeshPart	*part = mRenderThing->GetRTMesh()->mParts[i];
		if (part)
		{
			if (!stricmp(part->mName, "nexus"))
				mUnknown228 = TRUE;
			else if (!stricmp(part->mName, "weakpoint"))
				mUnknown22C = TRUE;
		}
		i++;
	}

	mUnknown230 = 0;
	mDeployState = 0;
	mSaidDying = FALSE;
	mSaidQuarterLife = FALSE;
	mSaidHalfLife = FALSE;
	mUnknown240 = 0;
	mUnknown248 = 0;

	if (!mSquad.ToRead())
	{
		if ((mAllegiance == kMuspellAllegiance) || (mAllegiance == kIndependentAllegiance))
			WORLD.mSet30.Append(this);
		if ((mAllegiance == kForsetiAllegiance) || (mAllegiance == kIndependentAllegiance))
			WORLD.mSet20.Append(this);
	}

	EVENT_MANAGER.AddEvent(UNIT_EVENT_4003, this, NEXT_FRAME);
}
//******************************************************************************************
void CUnit::IncreaseThingCounter(CInitThing *inInit)	// FUNCTION: 0x004f9220
{
	ListIterator<CUnitComponentReference>	components(&((CUnitInitThing *)inInit)->mStats->mComponents);
	for (CUnitComponentReference *c = components.First(); c; c = components.Next())
		sThingCount++;
	SUPERTYPE::IncreaseThingCounter(inInit);
}

//******************************************************************************************
// The collision type is one conditional expression: the if form stored mColType before retail's early pop of ebx
// (004f93f9). The surface point's if/else and the spawn's index local (as CFenrir's, 0044e300) settle the rest.
void CUnit::Unknown95()	// FUNCTION: 0x004f9260
{
	if (!mUnitData)
		return;

	CExplosionInitThing	init;
	if (mRenderThing)
		init.mPos = mRenderThing->GetRandomSurfacePoint(0);
	else
		init.mPos = mPos;
	init.mAllegiance = (EAllegiance)mAllegiance;
	if (MAP.GetWaterLevel() + 2.0f > init.mPos.Z)
	{
		FVector	d = init.mPos - mPos;
		if (d * d < GetRadius() * GetRadius())
		{
			SINT	index = mUnitData->mSmallExplosion;
			CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
			if (explosion)
			{
				init.mAttachedTo = this;
				init.mUseAttachedRadius = FALSE;
				init.mAllowVolumetric = FALSE;
				init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mSmallExplosion);
				init.mColType = (MAP.GetWaterLevel() < init.mPos.Z) ? kCollideWater : kCollideNothing;
				explosion->Init(&init);
			}
		}
	}
}

//******************************************************************************************
void CUnit::DamageRandomly()	// FUNCTION: 0x004f9430
{
	if (mSegments)
	{
		mSegments->DamageRandomly();
		return;
	}
	mLife = mLife / (GAME.FloatRandom() * 1.5f + 1.5f);
}

//******************************************************************************************
void CUnit::Unknown96()	// FUNCTION: 0x004f9490
{
	if (!mUnitData)
		return;

	CExplosionInitThing	init;
	if (mRenderThing)
		init.mPos = mRenderThing->GetRandomSurfacePoint(0);
	else
		init.mPos = mPos;
	init.mAllegiance = (EAllegiance)mAllegiance;
	if (init.mPos.Z > MAP.GetWaterLevel())
	{
		SINT	index = mUnitData->mSmallExplosion;
		CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
		if (explosion)
		{
			init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mSmallExplosion);
			init.mColType = kCollideWater;
			explosion->Init(&init);
		}
	}
}

//******************************************************************************************
void CUnit::Shutdown()	// FUNCTION: 0x004f95d0
{
	SOUND.KillSamplesForThing(this);

	mCurrentWeapon = NULL;
	ListIterator<CWeapon>	weapons(&mWeapons);
	CWeapon	*weapon;
	while (weapon = weapons.First())
	{
		mWeapons.Remove(weapon);
		delete weapon;
	}

	mCurrentSpawner = NULL;
	ListIterator<CSpawner>	spawners(&mSpawners);
	CSpawner	*spawner;
	while (spawner = spawners.First())
	{
		mSpawners.Remove(spawner);
		delete spawner;
	}

	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	CActiveReader<CUnit>	*child;
	while (child = children.First())
	{
		if (child->ToRead())
		{
			if (mFlags & TF_DYING)
				child->ToRead()->StartDieProcess();
			else
				child->ToRead()->Shutdown();
		}
		mChildren.Remove(child);
		delete child;
	}

	if (mUnknown1AC)
	{
		mUnknown1AC->Stop(FALSE);
		delete mUnknown1AC;
		mUnknown1AC = NULL;
	}

	CParticleEffectLink	*link;
	ListIterator<CParticleEffectLink>	thrusters(&mThrusters);
	while (link = thrusters.First())
	{
		mThrusters.Remove(link);
		link->Stop(FALSE);
		delete link;
	}
	ListIterator<CParticleEffectLink>	links1B4(&mUnknown1B4);
	while (link = links1B4.First())
	{
		mUnknown1B4.Remove(link);
		link->Stop(FALSE);
		delete link;
	}
	ListIterator<CParticleEffectLink>	links1C4(&mUnknown1C4);
	while (link = links1C4.First())
	{
		mUnknown1C4.Remove(link);
		link->Stop(FALSE);
		delete link;
	}

	WORLD.mUnitNB.Remove(this);

	if (mSegments)
	{
		delete mSegments;
		mSegments = NULL;
	}
	if (mGuide)
	{
		delete mGuide;
		mGuide = NULL;
	}

	if ((mUnitData) && (!(mFlags & TF_REMOVED_UNIT_TYPE)))
	{
		switch (mAllegiance)
		{
		case 0:
			WORLD.mCountersA[mUnitData->mBehaviour]--;
			mFlags |= TF_REMOVED_UNIT_TYPE;
			break;
		case 1:
			WORLD.mCountersB[mUnitData->mBehaviour]--;
			mFlags |= TF_REMOVED_UNIT_TYPE;
			break;
		}
	}

	WORLD.mSet30.Remove(this);
	WORLD.mSet20.Remove(this);

	SUPERTYPE::Shutdown();
}

//******************************************************************************************
void CUnit::HandleEvent(CEvent *event)	// FUNCTION: 0x004f9820
{
	float eventDelay;
	switch (event->GetEventNum())
	{
		case UNIT_EVENT_4001:
			Unknown4fb280((CScheduledEvent *)event);
			break;

		case UNIT_EVENT_4005:
			if (mUnknown218 > 0)
			{
				mUnknown218 -= mUnknown21C;
				eventDelay = NEXT_FRAME;
				EVENT_MANAGER.AddEvent(UNIT_EVENT_4005, this, eventDelay, START_OF_FRAME, NULL, (CScheduledEvent *)event);
			}
			else
				mUnknown218 = 0;
			break;

		case UNIT_EVENT_4003:
		{
			mUnknown110 = FALSE;
			for (SINT i = 0; i < 2; i++)
			{
				if ((GAME.GetPlayer(i)) && (GAME.GetCurrentCamera(i)))
				{
					FVector	d = mPos - GAME.GetCurrentCamera(i)->GetPos();
					if (d.Y * d.Y + d.X * d.X + d.Z * d.Z < 2500.0f)
						mUnknown110 = TRUE;
				}
			}
			eventDelay = GAME.FloatRandom() + 3.0f;
			EVENT_MANAGER.AddEvent(eventDelay, UNIT_EVENT_4003, this, START_OF_FRAME, NULL, (CScheduledEvent *)event);
			break;
		}

		case UNIT_EVENT_4004:
			SpawnExplosion();
			AddShutdownEvent();
			break;

		default:
			SUPERTYPE::HandleEvent(event);
	}
}

//******************************************************************************************
void CUnit::StartPlayingInitNoise()	// FUNCTION: 0x004f99b0
{
	if ((mUnitData) && (mUnitData->mSounds[0]))
		SOUND.PlayEffect(mUnitData->mSounds[0], this, 0.7f, ST_FOLLOWANDDIE, 1, 0, 0, -1.0f, 1);
}

//******************************************************************************************
float CUnit::GetLife()	// FUNCTION: 0x004f99f0
{
	if (mSegments)
		return mSegments->GetLife();
	return mLife;
}

//******************************************************************************************
float CUnit::GetMaxLife()	// FUNCTION: 0x004f9a10
{
	if (mSegments)
		return mSegments->GetLifeUnknown4443b0();
	if (mUnitData)
		return mUnitData->mUnknownC0;
	return 0;
}

//******************************************************************************************
float CUnit::GetLifeUnknown4f9a40()	// FUNCTION: 0x004f9a40
{
	if (!mSegments)
		return mLife;
	return mSegments->GetLifeUnknown444370();
}

//******************************************************************************************
void CUnit::RemoveSpawner(CSpawner *spawner)	// FUNCTION: 0x004f9a60
{
	if (spawner)
	{
		mSpawners.Remove(spawner);
		delete spawner;
	}
}

//******************************************************************************************
void CUnit::Damage(float amount, CThing *inByThis, BOOL inDamageShields, int mesh_part_no)	// FUNCTION: 0x004f9a90
{
	// Ammunition with a unit owner resets the linked squad's cooldown (004f9abc..004f9add).
	if ((mSquad.ToRead()) && (inByThis->IsA(THING_TYPE_AMMUNITION)) && (((CRound *)inByThis)->mUnknownEC.ToRead()) &&
		(((CRound *)inByThis)->mUnknownEC->IsA(THING_TYPE_UNIT)))
		mSquad->Unknown4e6660((CUnit *)((CRound *)inByThis)->mUnknownEC.ToRead());

	if ((mFlags & TF_DYING) && (inByThis->IsA(THING_TYPE_EXPLOSION)) && (mUnitData->mUnknown124))
		return;

	if ((mDeployState == 4) || (mDeployState == 3) || (mDeployState == 5))
		amount *= mUnitData->mUnknown160;

	if (amount > 0)
	{
		if (!mVulnerable)
			return;

		// Retail rejects nexus-unit explosion damage here. The later explosion geometry
		// branch has no ordinary entry: 004f9b69 returns on the same type bit tested
		// again at 004f9b87, with no intervening call or store. Preserve both guards.
		if ((mUnknown228) && (inByThis->IsA(THING_TYPE_EXPLOSION)))
			return;

		if ((mUnknown228) && (mesh_part_no != -1))
		{
			if (inByThis->IsA(THING_TYPE_EXPLOSION))
			{
				CMeshPart	*nexus = NULL;
				BOOL		found = FALSE;
				SINT		i = 0;
				while ((!found) && (i < mRenderThing->GetRTMesh()->mNumParts))
				{
					nexus = mRenderThing->GetRTMesh()->mParts[i];
					if ((nexus) && (!stricmp(nexus->mName, "nexus")))
						found = TRUE;
					else
						i++;
				}

				FVector		pos;
				FMatrix		ori;
				nexus->GetWorldTransforms(0, 0, 0, this, &ori, &pos, false, TRUE, 0);

				CLine		line(pos, inByThis->GetPos());

				if ((pos - inByThis->GetPos()).Magnitude() > ((CExplosion *)inByThis)->GetCurrentRadius())
					return;

				CWorldLineColReport	report;
				WORLD.FindFirstThingToHitLine(line, inByThis, &report, TRUE, TRUE, 1, THING_TYPE_AMMUNITION, TRUE, -1);

				// GetPart evaluates its index before the virtual mesh query: retail saves report+4
				// at 004f9d08, calls slot 9 at 004f9d1e, then uses that saved index.
				if (report.mUnknown04 == -1)
					return;
				if (nexus != mRenderThing->GetRTMesh()->GetPart(report.mUnknown04))
					return;
			}
			else
			{
				CMeshPart	*part = mRenderThing->GetRTMesh()->GetPart(mesh_part_no);
				if ((part) && (stricmp(part->mName, "nexus")))
					return;
			}
		}

		if ((mUnknown22C) && (mesh_part_no != -1))
		{
			CMeshPart	*part = mRenderThing->GetRTMesh()->GetPart(mesh_part_no);
			if ((part) && (stricmp(part->mName, "weakpoint")))		// retail multiplies the hits elsewhere (004f9db2)
				amount *= 5.0f;
		}

		if (mSegments)
		{
			mSegments->Damage(mesh_part_no, amount, inByThis);
			return;
		}

		Unknown107(amount);

		if (inDamageShields)
		{
			if (mShields >= amount)
			{
				mShields -= amount;
				return;
			}
			if (amount > mShields)
			{
				amount -= mShields;
				mShields = 0;
			}
		}

		mLife -= amount;
		if ((mLife < 0) && (!(mFlags & TF_DYING)))
		{
			if ((inByThis->IsA(THING_TYPE_AMMUNITION)) && (((CRound *)inByThis)->mUnknownEC.ToRead()) &&
				(((CRound *)inByThis)->mUnknownEC->IsA(THING_TYPE_BATTLE_ENGINE)))
				((CBattleEngine *)((CRound *)inByThis)->mUnknownEC.ToRead())->GetPlayer()->IncStat(PS_UNITSDESTROYED);

			if (!mUnitData->mUnknown11C)
			{
				StartDieProcess();
				Unknown71(amount, inByThis);
			}
			mUnknown1F0 = 0;
		}

		if ((mShields > 2.0f) && (mUnitData->mParticles[0]))
			PARTICLE_MANAGER.CreateEffect(mUnitData->mParticles[0], NULL, inByThis->GetPos(), 0, 0);
	}
	else
	{
		// repair, up to full life
		if ((mLife < mUnitData->mUnknownC0) && (mLife > 0))
		{
			if (mUnitData->mUnknownC0 - mLife < -amount)
				mLife = mUnitData->mUnknownC0;
			else
				mLife -= amount;
		}
	}

	if ((mUnitData->mUnknown11C) && (mLife < 0))
		Unknown100();

	// a pilot's radio line as the unit's life runs down
	if (mUnitData->mUnknown120)
	{
		WCHAR	*text = NULL;
		SINT	line = GAME.Random() % 3;
		DWORD	id = 0;
		WCHAR	*speaker;

		if (!strcmp(mUnitData->mName, "Tara Fighter"))
			speaker = TEXT.GetString(0x3c345);
		else if (!strcmp(mUnitData->mName, "Billy Fighter"))
			speaker = TEXT.GetString(0xcb1fd);
		else
			speaker = TEXT.GetString(0x19f8cf);

		if ((mLife < 0) && (!mSaidDying))
		{
			if (!strcmp(mUnitData->mName, "Tara Fighter"))
			{
				switch (line)
				{
					case 0:	id = 0x0b2cb1a4;	break;
					case 1:	id = 0x0f20c0e7;	break;
					case 2:	id = 0x1314d02a;	break;
				}
				text = TEXT.GetString(id);
			}
			else if (!strcmp(mUnitData->mName, "Billy Fighter"))
			{
				switch (line)
				{
					case 0:	id = 0x4041921f;	break;
					case 1:	id = 0x4829af02;	break;
					case 2:	id = 0x5011cbe5;	break;
				}
				text = TEXT.GetString(id);
			}
			else
			{
				switch (line)
				{
					case 0:	id = 0x2ff3332c;	break;
					case 1:	id = 0x37db50bd;	break;
					case 2:	id = 0x3fc36e4e;	break;
				}
				text = TEXT.GetString(id);
			}
			mSaidDying = TRUE;
			mSaidQuarterLife = TRUE;
			mSaidHalfLife = TRUE;
		}
		else if ((mUnitData->mUnknownC0 * 0.25f > mLife) && (!mSaidQuarterLife))
		{
			if (!strcmp(mUnitData->mName, "Tara Fighter"))
			{
				switch (line)
				{
					case 0:	id = 0x1604fe0a;	break;
					case 1:	id = 0x1decf40d;	break;
					case 2:	id = 0x25d4ea10;	break;
				}
				text = TEXT.GetString(id);
			}
			else if (!strcmp(mUnitData->mName, "Billy Fighter"))
			{
				switch (line)
				{
					case 0:	id = 0x80188559;	break;
					case 1:	id = 0x8fe86fbc;	break;
					case 2:	id = 0x9fb85a1f;	break;
				}
				text = TEXT.GetString(id);
			}
			else
			{
				switch (line)
				{
					case 0:	id = 0x5f7c0e8e;	break;
					case 1:	id = 0x6f4bf99f;	break;
					case 2:	id = 0x7f1be4b0;	break;
				}
				text = TEXT.GetString(id);
			}
			mSaidQuarterLife = TRUE;
			mSaidHalfLife = TRUE;
		}
		else if ((mUnitData->mUnknownC0 * 0.5f > mLife) && (!mSaidHalfLife))
		{
			if (!strcmp(mUnitData->mName, "Tara Fighter"))
			{
				switch (line)
				{
					case 0:	id = 0x0a896430;	break;
					case 1:	id = 0x0e7cd233;	break;
					case 2:	id = 0x12704036;	break;
				}
				text = TEXT.GetString(id);
			}
			else if (!strcmp(mUnitData->mName, "Billy Fighter"))
			{
				switch (line)
				{
					case 0:	id = 0x3f443bff;	break;
					case 1:	id = 0x472b1662;	break;
					case 2:	id = 0x4f11f0c5;	break;
				}
				text = TEXT.GetString(id);
			}
			else
			{
				switch (line)
				{
					case 0:	id = 0x2ef87634;	break;
					case 1:	id = 0x36df5145;	break;
					case 2:	id = 0x3ec62c56;	break;
				}
				text = TEXT.GetString(id);
			}
			mSaidHalfLife = TRUE;
		}

		CMESSAGEBOX	*box = GAME.GetMessageBox();
		if ((box) && (text))
#line 1101
			box->AddMessage(new(MT_UNIT_THING) CMessage(speaker, text, id, 0, this, 0, 10));
	}
}

//******************************************************************************************
void CUnit::Unknown77(CEulerAngles *cur, CEulerAngles *target, CEulerAngles *rate, FMatrix &out)	// FUNCTION: 0x004fa4b0
{
	float &curPitch = cur->mPitch;
	const float &targetPitch = target->mPitch;
	float	mult = GetMoveMultiplier();

	if (cur->mYaw != target->mYaw)
	{
		float	max_step = mult * rate->mYaw;
		float	t = target->mYaw;
		float	c = cur->mYaw;
		if ((c < -PI / 2) && (t > PI / 2))
			t -= PI_M2;
		else if ((c > PI / 2) && (t < -PI / 2))
			t += PI_M2;
		float	step = fabsf(c - t) * mult * 0.1f;
		if (step > max_step)
			step = max_step;
		if (cur->mYaw < target->mYaw)
		{
			if (target->mYaw - cur->mYaw > PI)
				cur->mYaw -= step;
			else
				cur->mYaw += step;
		}
		else
		{
			if (cur->mYaw - target->mYaw > PI)
				cur->mYaw += step;
			else
				cur->mYaw -= step;
		}
		if (cur->mYaw > PI)
			cur->mYaw -= PI_M2;
		else if (cur->mYaw < -PI)
			cur->mYaw += PI_M2;
	}

	if (curPitch != targetPitch)
	{
		float	max_step = mult * rate->mPitch;
		float	step = fabsf(fabsf(curPitch - targetPitch)) * mult * 0.1f;
		if (step > max_step)
			step = max_step;
		if (curPitch > targetPitch)
			curPitch -= step;
		else
			curPitch += step;
	}

	if (cur->mRoll != target->mRoll)
	{
		float	max_step = mult * rate->mRoll;
		float	t = target->mRoll;
		float	c = cur->mRoll;
		if ((c < -PI / 2) && (t > PI / 2))
			t -= PI_M2;
		else if ((c > PI / 2) && (t < -PI / 2))
			t += PI_M2;
		float	step = fabsf(c - t) * mult * 0.1f;
		if (step > max_step)
			step = max_step;
		if (cur->mRoll < target->mRoll)
		{
			if (target->mRoll - cur->mRoll > PI)
				cur->mRoll -= step;
			else
				cur->mRoll += step;
		}
		else
		{
			if (cur->mRoll - target->mRoll > PI)
				cur->mRoll += step;
			else
				cur->mRoll -= step;
		}
		if (cur->mRoll > PI)
			cur->mRoll -= PI_M2;
		else if (cur->mRoll < -PI)
			cur->mRoll += PI_M2;
	}

	out = FMatrix(cur->mYaw, curPitch, cur->mRoll);
}

//******************************************************************************************
void CUnit::UpdateTimer()	// FUNCTION: 0x004fa800
{
	switch (mUnknown168)
	{
	case 1:
		if (mUnknown16C <= EVENT_MANAGER.GetTime())
		{
			if (mUnknown1EC)
			{
				mUnknown1E8 = 1;
				mUnknown168 = 0;
			}
			else
			{
				Unknown87();
				mUnknown168 = 0;
			}
			return;
		}
		mUnknown1E8 = 0;
		Stop();
		break;
	case 2:
		mUnknown1E8 = 0;
		if (mUnknown16C < EVENT_MANAGER.GetTime())
		{
			mUnknown168 = 0;
			return;
		}
		Stop();
		break;
	case 3:
		if (mUnknown16C < EVENT_MANAGER.GetTime())
			mUnknown168 = 0;
		mOrientation = mOldOrientation;
		break;
	}
}

//******************************************************************************************
// STAND-IN: the two scans as file-local inline bodies. Move and Unknown4fb840 inline them, yet retail emits the
// out-of-line AnyWeaponUnknown50a290/AnySpawnerUnknown4e4420 after Deactivate; VC6 only inlines a body it has
// already seen, so the callers saw the loop before the member definitions. Original spelling is unknown (the loops
// written out in the callers would do the same).
static inline BOOL AnySpawnerBusy(CUnit *unit)
{
	ListIterator<CSpawner>	spawners(&unit->mSpawners);
	for (CSpawner *s = spawners.First(); s; s = spawners.Next())
		if (s->Unknown4e4420())
			return TRUE;
	return FALSE;
}

static inline BOOL AnyWeaponBusy(CUnit *unit)
{
	ListIterator<CWeapon>	weapons(&unit->mWeapons);
	for (CWeapon *w = weapons.First(); w; w = weapons.Next())
		if (w->IsFiring())
			return TRUE;
	return FALSE;
}

//******************************************************************************************
void CUnit::Move()	// FUNCTION: 0x004fa8d0
{
	if ((mGuide) && ((mActive) || IsDying()))
		mGuide->Unknown3();
	else
		Stop();

	mUnknown248 = 0;
	UpdateTimer();

	SUPERTYPE::Move();

	if (Unknown76())
	{
		if (mUnknown120 != mCurrentOrientation)
			Unknown77(&mCurrentOrientation, &mUnknown120, &mUnknown12C, mOrientation);
	}

	if (!IsDying())
	{
		mShields += mShieldRegeneration;
		if (mShields > mMaxShields)
			mShields = mMaxShields;
	}
	else if (GetUnitData()->mUnknown124)
	{
		SINT	n = (SINT)GetMoveMultiplier();
		for (SINT i = 0; i < n; i++)
			Unknown95();
	}

	if (mUnknown170 + 0.1f < mUnknown174)
		mUnknown170 += 0.1f;
	else if (mUnknown170 - 0.1f > mUnknown174)
		mUnknown170 -= 0.1f;
	else
		mUnknown170 = mUnknown174;

	// the vents follow their emitters
	FVector		pos;
	FMatrix		ori;
	SINT		n = 1;
	ListIterator<CParticleEffectLink>	vents(&mUnknown1C4);
	for (CParticleEffectLink *vent = vents.First(); vent; vent = vents.Next())
	{
		if (vent->Full())
		{
			GetEmitter(21, n, pos, ori);
			vent->SetPos(pos);
			vent->SetOrientation(ori);
		}
		n++;
	}

	// the firing effect follows the weapon
	if ((mUnknown168 == 1) && (mUnknown1AC->Full()) && (mCurrentWeapon))
	{
		FVector	wpos = mCurrentWeapon->GetPrimaryPosition();
		FMatrix	wori = mCurrentWeapon->GetPrimaryOrientation();
		mUnknown1AC->SetPos(wpos);
		mUnknown1AC->SetOrientation(wori);
	}

	// dust, while it is on the ground
	if ((GetUnitData()->mUnknown10C) && (IsOnGround()))
	{
		n = 1;
		ListIterator<CParticleEffectLink>	dust(&mUnknown1B4);
		for (CParticleEffectLink *link = dust.First(); link; link = dust.Next())
		{
			if (!link->Full())
				PARTICLE_MANAGER.AddParticle(GetUnitData()->mParticles[6], link);

			if (link->Full())
			{
				GetEmitter(22, n, pos, ori);
				link->SetPos(pos);
				link->SetOrientation(ori);
			}
			n++;
		}
	}

	if ((!IsDying()) && (!(mLife < 0)) && (mUnknown1F0))
	{
		ListIterator<CSpawner>	spawners(&mSpawners);
		for (CSpawner *s = spawners.First(); s; s = spawners.Next())
			s->DoSpawn();
	}

	// turn the turret towards the target
	mUnknownE4 = mUnknownE0;
	mUnknownF0 = mUnknownE8;
	float	mult = GetMoveMultiplier();

	if ((mAI) && (!IsDying()) && (Unknown109()))
	{
		float	delta = mUnknownE0;
		CUnit	*target = mAI->mTarget.ToRead();
		if (target)
		{
			FVector	aim;
			target->GetTargetablePos(&aim);
			FVector	muzzle = (mOrientation * mUnknown1F8) + mPos;
			delta = (aim - muzzle).Azimuth() - Unknown92();
			mUnknown20C = EVENT_MANAGER.GetTime() + 10.0f;
		}
		else if (mUnknown20C < EVENT_MANAGER.GetTime())
			delta = 0;

		if (delta > PI)
			delta -= PI_M2;
		else if (delta < -PI)
			delta += PI_M2;

		float	limit = mult * GetUnitData()->mUnknownDC;
		if (delta > limit)
			delta = limit;
		else if (delta < -limit)
			delta = -limit;

		float	rate = mult * GetUnitData()->mUnknownBC;
		if (rate < fabsf(AngleDifference(mUnknownE0, delta)))
		{
			if (delta > mUnknownE0)
			{
				if (delta - mUnknownE0 > PI)
					mUnknownE0 -= rate;
				else
					mUnknownE0 += rate;
			}
			else
			{
				if (mUnknownE0 - delta > PI)
					mUnknownE0 += rate;
				else
					mUnknownE0 -= rate;
			}

			if (mUnknownE0 > PI)
				mUnknownE0 -= PI_M2;
			else if (mUnknownE0 < -PI)
				mUnknownE0 += PI_M2;
		}
		else
			mUnknownE0 = delta;

		if (mUnknownE0 > GetUnitData()->mUnknownDC)
			mUnknownE0 = GetUnitData()->mUnknownDC;
		else if (mUnknownE0 < -GetUnitData()->mUnknownDC)
			mUnknownE0 = -GetUnitData()->mUnknownDC;

		// and the barrel towards its pitch
		if (mUnknown224)
		{
			float	pitch_rate = mult * GetUnitData()->mUnknownBC;
			if (pitch_rate < fabsf(mUnknownE8 - mUnknownEC))
			{
				if (mUnknownE8 < mUnknownEC)
					mUnknownE8 += pitch_rate;
				else
					mUnknownE8 -= pitch_rate;
			}
			else
				mUnknownE8 = mUnknownEC;
		}
	}

	if (mDeployState == 1)
	{
		// deploy once it reaches its destination
		if (mGuide)
		{
			FVector	dest = mGuide->mDestination;
			FVector	d = mPos - dest;
			if (sqrtf((d.X * d.X) + (d.Y * d.Y)) < 4.0f)
			{
				mDeployState = 2;
				const FMatrix	&heading = mOrientation;
				GoToPoint(FVector((heading.Row[0].Y * 300.0f), (heading.Row[1].Y * 300.0f), 0) + mPos, FALSE);
			}
		}
	}
	else if (mDeployState == 2)
		Unknown116();
	else if (mDeployState == 4)
	{
		if (!GetCurrentTarget())
			StartUndeploying();
	}

	// the spawner's working noise
	if (GetUnitData()->mSounds[1])
	{
		if (AnySpawnerBusy(this))
		{
			if (!mUnknown230)
				SOUND.PlayEffect(GetUnitData()->mSounds[1], this, 0.7f, ST_FOLLOWANDDIE, TRUE, 0, 0, -1.f, TRUE, 1.f, ST_GAME_SOUND, 0);
			mUnknown230 = TRUE;
		}
		else
		{
			if (mUnknown230)
			{
				char	*sample = GetUnitData()->mSounds[1]->mSample;
				SOUND.KillSample(this, SOUND.GetSample(sample));
			}
			mUnknown230 = FALSE;
		}
	}

	UpdatePosition();
}

//******************************************************************************************
float CUnit::Unknown92()	// FUNCTION: 0x004fb270
{
	return mCurrentOrientation.mYaw;
}

//******************************************************************************************
void CUnit::Unknown4fb280(CScheduledEvent *event)	// FUNCTION: 0x004fb280
{
	if ((!mUnknown224) || (mFlags & TF_DYING))
		return;

	CUnit	*target;
	if ((mAI) && (target = mAI->mTarget.ToRead()))
	{
		if ((mCurrentWeapon) && (mCurrentWeapon->mMode) && (mCurrentWeapon->mMode->mRound))
		{
			FVector	aim;
			target->GetTargetablePos(&aim);
			mUnknownEC = mCurrentWeapon->CalculateDesiredPitch(aim);
		}
		mUnknown20C = EVENT_MANAGER.GetTime() + 10.0f;
	}
	else if (mUnknown20C < EVENT_MANAGER.GetTime())
		mUnknownEC = mUnknownF4;

	if (mUnknownEC > PI)
		mUnknownEC = PI;
	else if (mUnknownEC < -PI / 2)
		mUnknownEC = -PI / 2;

	EVENT_MANAGER.AddEvent(UNIT_EVENT_4001, this, GAME.FloatRandom() * 0.1f + EVENT_MANAGER.GetTime(), START_OF_FRAME, NULL, event);
}

//******************************************************************************************
BOOL CUnit::Unknown4fb3d0(CUnit *target)	// FUNCTION: 0x004fb3d0
{
	if ((!target) || (!target->Unknown108()))
		return FALSE;

	ListIterator<CSpawner>	spawners(&mSpawners);
	for (CSpawner *spawner = spawners.First(); spawner; spawner = spawners.Next())
	{
		if (spawner->Unknown4e4480(target))
			return TRUE;
	}

	ListIterator<CWeapon>	weapons(&mWeapons);
	for (CWeapon *weapon = weapons.First(); weapon; weapon = weapons.Next())
	{
		if (weapon->Unknown50a0b0(target))
		{
			float	height = MAP.GetHeightAt(target->GetPos()) - target->GetPos().Z;
			if ((height >= weapon->GetMinTargetHeight()) && (height <= weapon->GetMaxTargetHeight()))
				return TRUE;
			return FALSE;
		}
	}
	return FALSE;
}

//******************************************************************************************
// Whether the current weapon can fire at the target: in range, its height over the ground (or sea) within the
// weapon's band, and the weapon's own test; a spawner answers yes.
BOOL CUnit::CanFireAt(CUnit *target, BOOL a)	// FUNCTION: 0x004fb500
{
	if (Unknown4fb670(target) != 0)
		return FALSE;
	if (mCurrentWeapon)
	{
		float height = MAP.Collide(target->GetPos());
		if (MAP.GetWaterLevel() < height)
			height = MAP.GetWaterLevel();
		height -= target->GetPos().Z;
		if (height >= mCurrentWeapon->GetMinTargetHeight() && height <= mCurrentWeapon->GetMaxTargetHeight())
			return mCurrentWeapon->Unknown507ab0(target, a);
	}
	return mCurrentSpawner.ToRead() != NULL;
}

//******************************************************************************************
BOOL CUnit::Unknown4fb5a0(CUnit *target)	// FUNCTION: 0x004fb5a0
{
	if (Unknown4fb670(target) != 0)
		return FALSE;
	if (mCurrentWeapon)
	{
		float height;
		if (target->IsOnGround())
			height = 0;
		else
		{
			height = MAP.Collide(target->GetPos());
			if (MAP.GetWaterLevel() < height)
				height = MAP.GetWaterLevel();
			height -= target->GetPos().Z;
		}
		if (height >= mCurrentWeapon->GetMinTargetHeight() && height <= mCurrentWeapon->GetMaxTargetHeight())
			return mCurrentWeapon->Unknown5088b0(target);
	}
	return mCurrentSpawner.ToRead() != NULL;
}

//******************************************************************************************
void CUnit::WeaponUnknown509140(SINT a, SINT b)	// FUNCTION: 0x004fb650
{
	if (mCurrentWeapon)
		mCurrentWeapon->Unknown509140(a, b);
}

//******************************************************************************************
SINT CUnit::Unknown4fb670(CThing *target)	// FUNCTION: 0x004fb670
{
	if (!target)
		return 2;

	FVector	offset = target->GetPos() - mPos;
	float	dist = offset.Magnitude();
	if (mCurrentWeapon)
	{
		if (mCurrentWeapon->GetMinRange(target->GetPos()) > dist)
			return 2;
		if (mCurrentWeapon->GetMaxRange(target->GetPos()) < dist)
			return 1;
		return 0;
	}
	if (mCurrentSpawner.ToRead())
	{
		if (mCurrentSpawner->mData->mMinRange > dist)
			return 2;
		if (mCurrentSpawner->mData->mMaxRange < dist)
			return 1;
		return 0;
	}
	return 2;
}

//******************************************************************************************
float CUnit::GetMinRange(FVector target)	// FUNCTION: 0x004fb780
{
	if (mCurrentWeapon)
		return mCurrentWeapon->GetMinRange(target);
	if (mCurrentSpawner.ToRead())
		return mCurrentSpawner->mData->mMinRange;
	return 0;
}

//******************************************************************************************
float CUnit::GetMaxRange(FVector target)	// FUNCTION: 0x004fb7e0
{
	if (mCurrentWeapon)
		return mCurrentWeapon->GetMaxRange(target);
	if (mCurrentSpawner.ToRead())
		return mCurrentSpawner->mData->mMaxRange;
	return 0;
}

//******************************************************************************************
static inline SINT GetSpawnerConditions(CSpawner *spawner)
{
	return spawner->mData->mConditions;
}
static inline float GetSpawnerMinimumRange(CSpawner *spawner)
{
	return spawner->mData->mMinRange;
}
static inline float GetSpawnerMaximumRange(CSpawner *spawner)
{
	return spawner->mData->mMaxRange;
}

void CUnit::Unknown4fb840(CThing *target)	// FUNCTION: 0x004fb840
{
	if (AnyWeaponBusy(this))
		return;
	if (target)
	{
		mCurrentWeapon = NULL;
		mCurrentSpawner = NULL;
		FVector	offset = target->GetPos() - mPos;
		float	dist = offset.Magnitude();
		float	best = -1.0f;

		ListIterator<CSpawner>	spawners(&mSpawners);
		for (CSpawner *spawner = spawners.First(); spawner; spawner = spawners.Next())
		{
			BOOL	can_spawn = FALSE;
			if ((IsOnGround()) && (GetSpawnerConditions(spawner) & 2))
				can_spawn = TRUE;
			else if ((IsInWater()) && (GetSpawnerConditions(spawner) & 1))
				can_spawn = TRUE;
			else if ((!IsOnGround()) && (!IsInWater()) && (GetSpawnerConditions(spawner) & 4))
				can_spawn = TRUE;

			if ((spawner->Unknown4e4480(target)) && (can_spawn))
			{
				float	score;
				if ((GetSpawnerMinimumRange(spawner) < dist) && (GetSpawnerMaximumRange(spawner) > dist))
					score = 1000000.0f;
				else
					score = dist - GetSpawnerMaximumRange(spawner);
				if ((spawner->Unknown4e43d0()) && (!AnySpawnerBusy(this)))
					score += 1000000.0f;
				if (score > best)
				{
					mCurrentSpawner = spawner;
					best = score;
				}
			}
		}

		ListIterator<CWeapon>	weapons(&mWeapons);
		for (CWeapon *weapon = weapons.First(); weapon; weapon = weapons.Next())
		{
			if (weapon->Unknown50a0b0(target))
			{
				float	height = MAP.Collide(target->GetPos());
				if (MAP.GetWaterLevel() < height)
					height = MAP.GetWaterLevel();
				height -= target->GetPos().Z;
				if ((height > weapon->GetMinTargetHeight()) && (height < weapon->GetMaxTargetHeight()))
				{
					float	score = 0;
					if ((target->IsA(THING_TYPE_COMPONENT)) && (weapon->Unknown50a0d0(THING_TYPE_COMPONENT)))
						score = 2000000.0f;
					if (weapon->GetMinRange(FVector(0, 0, 0)) > dist)
						score += weapon->GetMinRange(FVector(0, 0, 0)) - dist;
					else if (weapon->GetMaxRange(FVector(0, 0, 0)) < dist)
						score += dist - weapon->GetMaxRange(FVector(0, 0, 0));
					else
						score += 1000000.0f;
					if (weapon->ReadyToFire())
						score += 1000000.0f;
					if (score > best)
					{
						mCurrentWeapon = weapon;
						mCurrentSpawner = NULL;
						best = score;
					}
				}
			}
		}
	}
}

//******************************************************************************************
float CUnit::GetWeaponUnknownA0_88()	// FUNCTION: 0x004fbc90
{
	if (mCurrentWeapon)
		return mCurrentWeapon->mMode->mUnknown88;
	return 0;
}

//******************************************************************************************
BOOL CUnit::Unknown86()	// FUNCTION: 0x004fbcb0
{
	if ((mUnitData->mUnknown108) && (mDeployState != 4))
	{
		StartDeploying();
		return FALSE;
	}

	if (mCurrentWeapon)
	{
		if (mUnknown168 == 1)
			return FALSE;
		if (mUnknown1E8)
			return FALSE;
		if (!mCurrentWeapon->ReadyToFire())
			return FALSE;

		mUnknown16C = EVENT_MANAGER.GetTime() + GetWeaponUnknownA0_88();
		mUnknown168 = 1;
		mUnknown1EC = 1;
		if (mCurrentWeapon->mMode->mSoundB)
			SOUND.PlayEffect(mCurrentWeapon->mMode->mSoundB, this, 0.7f, ST_SETINITIALPOSITION);

		if (mCurrentWeapon->mMode->mParticleB)
		{
			FVector	pos;
			FMatrix	ori;
			SINT	i = 1;
			do
			{
				GetEmitter(29, i, pos, ori);
				if ((pos == FVector(0, 0, 0)) && (i == 1))
				{
					pos = mCurrentWeapon->GetPrimaryPosition();
					ori = mCurrentWeapon->GetPrimaryOrientation();
				}
				if (!(pos == FVector(0, 0, 0)))
				{
					PARTICLE_MANAGER.CreateEffect(mCurrentWeapon->mMode->mParticleB, mUnknown1AC, ZERO_FVECTOR, 0, 0);
					mUnknown1AC->SetPos(pos);
					mUnknown1AC->SetOrientation(ori);
				}
				i++;
			} while (!(pos == FVector(0, 0, 0)));
		}

		if (mUnknown16C <= EVENT_MANAGER.GetTime())
			mUnknown1E8 = 1;
		return TRUE;
	}

	if (mCurrentSpawner.ToRead())
	{
		if ((mUnknown168 != 1) && (!mUnknown1E8) && (mCurrentSpawner->Unknown4e43d0()))
		{
			mUnknown16C = mCurrentSpawner->GetUnknown34() + EVENT_MANAGER.GetTime();
			mUnknown1E8 = 0;
			mUnknown168 = 1;
		}
		return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
BOOL CUnit::Unknown84()	// FUNCTION: 0x004fc000
{
	if (mCurrentWeapon)
	{
		if ((mUnknown1E8) && (mCurrentWeapon->ReadyToFire()))
			return TRUE;
	}
	else if (mCurrentSpawner.ToRead())
	{
		if (!mUnitData->mUnknown110)
		{
			ListIterator<CSpawner>	spawners(&mSpawners);
			for (CSpawner *s = spawners.First(); s; s = spawners.Next())
				if (s->Unknown4e4420())
					return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
BOOL CUnit::Unknown74()	// FUNCTION: 0x004fc080
{
	BOOL	fired = FALSE;
	if (mCurrentWeapon)
	{
		if ((mUnknown1E8) && (mCurrentWeapon->ReadyToFire()))
		{
			fired = TRUE;
			mCurrentWeapon->Fire();
			Unknown87();
		}
	}
	else if ((mCurrentSpawner.ToRead()) && (Unknown115()))
	{
		fired = mCurrentSpawner->DoSpawn();
		if (mCurrentSpawner->GetDelay() != 0)
		{
			mUnknown16C = mCurrentSpawner->GetDelay() + EVENT_MANAGER.GetTime();
			mUnknown1E8 = 0;
			mUnknown168 = 2;
		}
		else
		{
			mUnknown1E8 = 1;
			mUnknown168 = 0;
		}
	}
	mUnknown1EC = 0;
	mUnknown1E8 = 0;
	return fired;
}

//******************************************************************************************
void CUnit::Unknown87()	// FUNCTION: 0x004fc170
{
	if ((mCurrentWeapon) && (mCurrentWeapon->mMode->mSoundC))
		SOUND.PlayEffect(mCurrentWeapon->mMode->mSoundC, this, 0.7f, ST_SETINITIALPOSITION);
	SpawnComponentEffects();
	mUnknown1E8 = 0;
	if ((mCurrentWeapon) && (mCurrentWeapon->mMode->mUnknown8C != 0))
	{
		mUnknown16C = EVENT_MANAGER.GetTime() + mCurrentWeapon->mMode->mUnknown8C;
		mUnknown168 = 2;
	}
	else
		mUnknown168 = 0;
}

//******************************************************************************************
void CUnit::SpawnComponentEffects()	// FUNCTION: 0x004fc220
{
	if (mUnitData->mParticles[7])
	{
		ListIterator<CParticleEffectLink>	links(&mUnknown1C4);
		for (CParticleEffectLink *link = links.First(); link; link = links.Next())
		{
			FVector	pos;
			FMatrix	ori;
			GetEmitter(21, 1, pos, ori);
			link->mEffect = NULL;
			PARTICLE_MANAGER.CreateEffect(mUnitData->mParticles[7], link, ZERO_FVECTOR, 0, 0);
			link->SetPos(pos);
			link->SetOrientation(ori);
		}
	}

	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *child = children.First(); child; child = children.Next())
	{
		if (child->ToRead())
			child->ToRead()->SpawnComponentEffects();
	}
}

//******************************************************************************************
void CUnit::SetStateTimer(float delay)	// FUNCTION: 0x004fc3a0
{
	mUnknown168 = 3;
	mUnknown16C = EVENT_MANAGER.GetTime() + delay;
}

//******************************************************************************************
void CUnit::GetLaunchPosition(CWeapon *inWeapon, int inIndex, FVector &outPos, FMatrix &outOrientation, BOOL inNeedOrientation)	// FUNCTION: 0x004fc3c0
{
	ListIterator<CWeapon>	weapons(&mWeapons);
	for (CWeapon *weapon = weapons.First(); weapon; weapon = weapons.Next())
	{
		if (weapon == inWeapon)
		{
			if (mUnitData)
				GetEmitter(weapon->mUnknownAC, inIndex, outPos, outOrientation);
			if (outPos == FVector(0, 0, 0))
			{
				outPos = inWeapon->GetPrimaryPosition();
				outOrientation = inWeapon->GetPrimaryOrientation();
			}
			return;
		}
	}
	outPos = inWeapon->GetPrimaryPosition();
	outOrientation = inWeapon->GetPrimaryOrientation();
}

//******************************************************************************************
void CUnit::GetEmitter(SINT type, SINT index, FVector &outPos, FMatrix &outOrientation)	// FUNCTION: 0x004fc4e0
{
	if ((!mUnknown110) && (mUnitData) && (!mUnitData->mUnknown1A0) &&
		(type != 15) && (type != 16) && (type != 17) && (type != 18) && (type != 19))
	{
		CUnitPair	*pair = GetUnitData()->FindPair(type, index);
		if (!pair)
		{
			Unknown4fc6e0(type, index, outPos, outOrientation, TRUE, TRUE);
			if (outPos == FVector(0, 0, 0))
				return;
#line 2160
			pair = new(MT_UNIT_THING) CUnitPair(type, index);
			GetUnitData()->mPairs.Add(pair);
			pair->mPos = outPos;
			pair->mOrientation = outOrientation;
		}
		outPos = (mOrientation * pair->mPos) + mPos;
		outOrientation = mOrientation * pair->mOrientation;
	}
	else
		Unknown4fc6e0(type, index, outPos, outOrientation, FALSE, TRUE);
}

//******************************************************************************************
// The emitter types' mount-point names on the unit's mesh.
void CUnit::Unknown4fc6e0(SINT type, SINT index, FVector &pos, FMatrix &ori, BOOL a, BOOL b)	// FUNCTION: 0x004fc6e0
{
	switch (type)
	{
		case 1:	mRenderThing->GetRTEmitter("GunA", index, pos, ori, a, b);	break;
		case 2:	mRenderThing->GetRTEmitter("GunB", index, pos, ori, a, b);	break;
		case 3:	mRenderThing->GetRTEmitter("GunC", index, pos, ori, a, b);	break;
		case 4:	mRenderThing->GetRTEmitter("GunD", index, pos, ori, a, b);	break;
		case 5:	mRenderThing->GetRTEmitter("GunE", index, pos, ori, a, b);	break;
		case 6:	mRenderThing->GetRTEmitter("GunF", index, pos, ori, a, b);	break;
		case 7:	mRenderThing->GetRTEmitter("GunG", index, pos, ori, a, b);	break;
		case 8:	mRenderThing->GetRTEmitter("GunH", index, pos, ori, a, b);	break;
		case 9:	mRenderThing->GetRTEmitter("GunI", index, pos, ori, a, b);	break;
		case 10:	mRenderThing->GetRTEmitter("SpawnerA", index, pos, ori, a, b);	break;
		case 11:	mRenderThing->GetRTEmitter("SpawnerB", index, pos, ori, a, b);	break;
		case 12:	mRenderThing->GetRTEmitter("SpawnerC", index, pos, ori, a, b);	break;
		case 13:	mRenderThing->GetRTEmitter("SpawnerD", index, pos, ori, a, b);	break;
		case 14:	mRenderThing->GetRTEmitter("SpawnerE", index, pos, ori, a, b);	break;
		case 15:	mRenderThing->GetRTEmitter("WaypointA", index, pos, ori, a, b);	break;
		case 16:	mRenderThing->GetRTEmitter("WaypointB", index, pos, ori, a, b);	break;
		case 17:	mRenderThing->GetRTEmitter("WaypointC", index, pos, ori, a, b);	break;
		case 18:	mRenderThing->GetRTEmitter("WaypointD", index, pos, ori, a, b);	break;
		case 19:	mRenderThing->GetRTEmitter("WaypointE", index, pos, ori, a, b);	break;
		case 20:	mRenderThing->GetRTEmitter("Component", index, pos, ori, a, b);	break;
		case 21:	mRenderThing->GetRTEmitter("Vent", index, pos, ori, a, b);	break;
		case 22:	mRenderThing->GetRTEmitter("Dust", index, pos, ori, a, b);	break;
		case 23:	mRenderThing->GetRTEmitter("Engine", index, pos, ori, a, b);	break;
		case 24:	mRenderThing->GetRTEmitter("Trail", index, pos, ori, a, b);	break;
		case 26:	mRenderThing->GetRTEmitter("Thruster", index, pos, ori, a, b);	break;
		case 27:	mRenderThing->GetRTEmitter("Doorstop", index, pos, ori, a, b);	break;
		case 28:	mRenderThing->GetRTEmitter("Activation", index, pos, ori, a, b);	break;
		case 25:	mRenderThing->GetRTEmitter("Smoke", index, pos, ori, a, b);	break;
		case 29:	mRenderThing->GetRTEmitter("Charge", index, pos, ori, a, b);	break;
		default:
			LOG.AddMessage("Serious problem - unknown emitter type");
			break;
	}
}

//******************************************************************************************
// Keep one downward axis for both slope tests and materialize each initial alignment. This vector lifetime
// preserves the retail summed-Y rounding at 004fcd2c and normalized X/Y stores at 004fcd65/004fcd6f;
// reading only Z let VC6 eliminate those operations and changed threshold decisions.
void CUnit::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x004fcc30
{
	if (mSquad.ToRead())
		mSquad->Unknown4e6640(this, other_thing);

	if (other_thing->IsA(THING_TYPE_UNIT))
		mUnknown248 = 1;

	if ((other_thing->IsA(THING_TYPE_CAN_BE_WALKED_ON)) && (report) && (report->mUnknown00) && (Gravity() > 0))
	{
		const FVector down(0, 0, -1);
		SINT	num = report->mNumContacts;
		BOOL	on_top = FALSE;
		for (SINT i = 0; i < num; i++)
		{
			float alignment = down * report->mNormals[i];
			if (alignment > 0.89f)
				on_top = TRUE;
		}
		if (!on_top)
		{
			if (num > 1)
			{
				FVector	n = report->mNormals[0];
				for (SINT j = 1; j < 3; j++)
					n += report->mNormals[j];
				n.Normalise();
				if ((down * n) > 0.69f)
					on_top = TRUE;
			}
		}
		if (on_top)
		{
			DeclareOnObject(other_thing);
			DeclareOnGround();
		}
	}

	SUPERTYPE::Hit(other_thing, report);
}

//******************************************************************************************
void CUnit::SetThingType(ULONG t)	// FUNCTION: 0x004fcdc0
{
	if ((mUnitData) && (mUnitData->mUnknown104))
		t |= THING_TYPE_CAN_DESTROY_TREES;
	SUPERTYPE::SetThingType(t | THING_TYPE_UNIT);
}

//******************************************************************************************
void CUnit::GoToPoint(FVector point, BOOL inOverride)	// FUNCTION: 0x004fce00
{
	if (mGuide)
		mGuide->GoToPoint(point, inOverride);
}

//******************************************************************************************
void CUnit::GuideUnknown5(FVector v)	// FUNCTION: 0x004fce40
{
	if (mGuide)
		mGuide->Unknown5(v);
}

//******************************************************************************************
void CUnit::GuideUnknown6(FVector v)	// FUNCTION: 0x004fce80
{
	if (mGuide)
		mGuide->Unknown6(v);
}

//******************************************************************************************
void CUnit::GuideUnknown7(FVector v)	// FUNCTION: 0x004fcec0
{
	if (mGuide)
		mGuide->Unknown7(v);
}

//******************************************************************************************
void CUnit::Stop()	// FUNCTION: 0x004fcf00
{
	mUnknown14C = FVector(0, 0, 0);
	mUnknown120.mYaw = mCurrentOrientation.mYaw;
	SUPERTYPE::Stop();
	if (mGuide)
		mGuide->Stop();
}

//******************************************************************************************
void CUnit::ClearSpawners()	// FUNCTION: 0x004fcfa0
{
	mCurrentSpawner = NULL;
	ListIterator<CSpawner>	spawners(&mSpawners);
	CSpawner	*spawner;
	while (spawner = spawners.First())
	{
		mSpawners.Remove(spawner);
		spawner->Shutdown();
	}
}

//******************************************************************************************
void CUnit::ReleaseChildren()	// FUNCTION: 0x004fcfe0
{
	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	CActiveReader<CUnit>	*child;
	while (child = children.First())
	{
		if (child->ToRead())
		{
			if (mFlags & TF_DYING)
				child->ToRead()->StartDieProcess();
			else
				child->ToRead()->Shutdown();
		}
		mChildren.Remove(child);
		delete child;
	}
}

//******************************************************************************************
void CUnit::Remove()	// FUNCTION: 0x004fd040
{
	ReleaseChildren();
	ClearSpawners();
	SpawnExplosion();
	if (mMissionScript)
	{
		mMissionScript->Died();
		delete mMissionScript;
		mMissionScript = NULL;
	}
	EVENT_MANAGER.AddEvent(SHUTDOWN, this, Unknown112() + EVENT_MANAGER.GetTime());
}

//******************************************************************************************
BOOL CUnit::StartDieProcess()	// FUNCTION: 0x004fd140
{
	if (mFlags & TF_DYING)
		return FALSE;

	SOUND.KillSamplesForThing(this);
	mFlags |= TF_DYING;
	if (mUnitData)
	{
		switch (mAllegiance)
		{
		case 0:
			WORLD.mCountersA[mUnitData->mBehaviour]--;
			GAME.IncScore(-UPhysicsManager::GetBehaviourPriority(mUnitData));
			mFlags |= TF_REMOVED_UNIT_TYPE;
			break;
		case 1:
			WORLD.mCountersB[mUnitData->mBehaviour]--;
			GAME.IncScore(UPhysicsManager::GetBehaviourPriority(mUnitData));
			mFlags |= TF_REMOVED_UNIT_TYPE;
			break;
		}
	}
	if (mSegments)
		mSegments->Unknown4443f0();
	if (mMissionScript)
		mMissionScript->StartedDying();
	ClearSpawners();
	return TRUE;
}

//******************************************************************************************
void CUnit::SpawnExplosion()	// FUNCTION: 0x004fd230
{
	if (!mUnitData)
		return;

	SINT	index = mUnitData->mExplosion;
	CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
	CExplosionInitThing	init;
	init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mExplosion);
	init.mAllegiance = (EAllegiance)mAllegiance;
	init.mAttachedTo = this;
	init.mUseAttachedRadius = TRUE;
	init.mAllowVolumetric = TRUE;
	if (IsOnGround())
		init.mColType = kCollideGround;
	else
		init.mColType = (IsInWater()) ? kCollideWater : kCollideNothing;
	if (explosion)
	{
		init.mPos = mPos;
		explosion->Init(&init);
	}
}

//******************************************************************************************
COccupancyGrid *CUnit::GetOccupancyGrid()	// FUNCTION: 0x004fd380
{
	if (mUnitData)
	{
		switch (mUnitData->mUnknownFC)
		{
		case 0:	return NULL;
		case 1:	return WORLD.mOccupancy[0];
		case 2:	return WORLD.mOccupancy[1];
		case 3:
		case 4:	return WORLD.mOccupancy[2];
		}
	}
	return NULL;
}

//******************************************************************************************
// Out of line in retail's ConfirmedKill too (004fe05c); this file's only caller, so the pragma changes nothing else.
#pragma auto_inline(off)
BOOL CUnit::IsTargetAlligence(SINT allegiance)	// FUNCTION: 0x004fd3d0
{
	if ((mUnitData) && (mUnitData->mUnknown128) && (allegiance == 2))
		return TRUE;

	switch (allegiance)
	{
		case 0:
			if ((mAllegiance == kMuspellAllegiance) || (mAllegiance == kIndependentAllegiance))
				return TRUE;
			break;
		case 1:
			if ((mAllegiance == kForsetiAllegiance) || (mAllegiance == kIndependentAllegiance))
				return TRUE;
			break;
		case 6:
			if ((mAllegiance == kMuspellAllegiance) || (mAllegiance == kForsetiAllegiance))
				return TRUE;
			break;
	}
	return FALSE;
}

//******************************************************************************************
BOOL CUnit::IsAThreat()	// FUNCTION: 0x004fd440
{
	ListIterator<CWeapon>	weapons(&mWeapons);
	for (CWeapon *w = weapons.First(); w; w = weapons.Next())
		if ((w->mMode) && (w->mMode->mRound) && (w->mMode->mRound->mUnknown1C > 0))
			return TRUE;

	ListIterator<CSpawner>	spawners(&mSpawners);
	if (spawners.First())
		return TRUE;

	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		if (c->ToRead())
			return TRUE;
	return FALSE;
}

//******************************************************************************************
void CUnit::GetTargetablePos(FVector *out)	// FUNCTION: 0x004fd4d0
{
	if (mSegments)
		mSegments->GetCentre(out);
	else
		GetCentrePos(*out);
}

//******************************************************************************************
void CUnit::GetRenderUnknown90(FVector *out)	// FUNCTION: 0x004fd500
{
	GetTargetablePos(out);
	*out += CActor::GetRenderPos() - GetOldPos();
}

//******************************************************************************************
BOOL CUnit::AnyWeaponUnknown94()	// FUNCTION: 0x004fd570
{
	ListIterator<CWeapon>	weapons(&mWeapons);
	for (CWeapon *w = weapons.First(); w; w = weapons.Next())
		if (w->mUnknown94)
			return TRUE;
	return FALSE;
}

//******************************************************************************************
BOOL CUnit::IsLiveUnit(CUnit *unit)	// FUNCTION: 0x004fd5b0
{
	if ((unit) && (!(unit->mFlags & TF_DYING)) && (unit->mDeployState != 1) && (unit->mDeployState != 2))
		return TRUE;
	return FALSE;
}

//******************************************************************************************
int CUnit::GetShouldDoHitEffect(SINT part)	// FUNCTION: 0x004fd5e0
{
	if (mSegments)
	{
		if (EVENT_MANAGER.GetTime() - mSegments->GetLastDamageTime(part) < 1.0f)
		{
			float	amount = mSegments->GetLastDamageAmount(part);
			Unknown107(amount);
			mUnknown21C = 10;
			SINT	level = mUnknown218 - (SINT)((EVENT_MANAGER.GetTime() - mSegments->GetLastDamageTime(part)) * 20.0f) * mUnknown21C;
			if (level < 0)
				return 0;
			if (level > 100)
				return 100;
			return level;
		}
		return 0;
	}
	return mUnknown218;
}

//******************************************************************************************
void CUnit::Activate()	// FUNCTION: 0x004fd6a0
{
	if (mActive)
		return;
	mActive = TRUE;
	if (mSquad.ToRead())
		mSquad->Activate();
	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		if (c->ToRead())
			c->ToRead()->Activate();
}

//******************************************************************************************
void CUnit::Deactivate()	// FUNCTION: 0x004fd700
{
	if (!mActive)
		return;
	mActive = FALSE;
	if (mSquad.ToRead())
		mSquad->Deactivate();
	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		if (c->ToRead())
			c->ToRead()->Deactivate();
}

//******************************************************************************************
BOOL CUnit::AnyWeaponUnknown50a290()	// FUNCTION: 0x004fd760
{
	return AnyWeaponBusy(this);
}

//******************************************************************************************
BOOL CUnit::AnySpawnerUnknown4e4420()	// FUNCTION: 0x004fd7a0
{
	return AnySpawnerBusy(this);
}

//******************************************************************************************
BOOL CUnit::AllSpawnersReady()	// FUNCTION: 0x004fd7e0
{
	ListIterator<CSpawner>	spawners(&mSpawners);
	for (CSpawner *s = spawners.First(); s; s = spawners.Next())
	{
		if (!s->Unknown4e4430())
			return FALSE;
		if (s->Unknown4e4420())
			return FALSE;
	}
	return TRUE;
}

//******************************************************************************************
void CUnit::SetAllegiance(SINT allegiance)	// FUNCTION: 0x004fd830
{
	mAllegiance = allegiance;
	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		if (c->ToRead())
			c->ToRead()->SetAllegiance(allegiance);

	WORLD.mSet30.Remove(this);
	WORLD.mSet20.Remove(this);
	if (!mSquad.ToRead())
	{
		if ((mAllegiance == kMuspellAllegiance) || (mAllegiance == kIndependentAllegiance))
			WORLD.mSet30.Append(this);
		if ((mAllegiance == kForsetiAllegiance) || (mAllegiance == kIndependentAllegiance))
			WORLD.mSet20.Append(this);
	}
}

//******************************************************************************************
CActiveReader<CUnit> *CUnit::FindChild(SINT key)	// FUNCTION: 0x004fd8d0
{
	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		if ((c->ToRead()) && (((CComponent *)c->ToRead())->mNumber == key))
			return c;
	return NULL;
}
#pragma auto_inline(on)

//******************************************************************************************
FVector CUnit::FindNearestSafeSide()	// FUNCTION: 0x004fd910
{
	FVector	best = FVector(0, 0, 0);
	ListIterator<CThing>	sides(&WORLD.mSetD0);
	for (CSafeSide *side = (CSafeSide *)sides.First(); side; side = (CSafeSide *)sides.Next())
	{
		SINT	allegiance = mAllegiance;
		if (side->GetAllegiance() == allegiance)
		{
			if ((mPos - best).MagnitudeXY() > (mPos - side->GetPos()).MagnitudeXY())
				best = side->GetPos();
		}
	}
	return best;
}

//******************************************************************************************
SINT CUnit::GetUnitDataUnknown120()	// FUNCTION: 0x004fda10
{
	return mUnitData->mUnknown120;
}

//******************************************************************************************
void CUnit::SetSquadTarget(CUnit *unit)	// FUNCTION: 0x004fda20
{
	if ((mSquad.ToRead()) && (unit))
		mSquad->Unknown85((SINT)unit->mSquad.ToRead());
	if (mAI)
		mAI->Unknown4ffdd0(unit, TRUE);
	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		if (c->ToRead())
			c->ToRead()->SetSquadTarget(unit);
}

//******************************************************************************************
BOOL CUnit::Unknown98()	// FUNCTION: 0x004fda90
{
	ListIterator<CSpawner>	spawners(&mSpawners);
	for (CSpawner *s = spawners.First(); s; s = spawners.Next())
		if (s->Unknown4e43d0())
			return TRUE;
	return FALSE;
}

//******************************************************************************************
void CUnit::Unknown4fdad0(CThing *target)	// FUNCTION: 0x004fdad0
{
	if ((!target) || (!Unknown99()))
		return;

	ListIterator<CSpawner>	spawners(&mSpawners);
	for (CSpawner *spawner = spawners.First(); spawner; spawner = spawners.Next())
	{
		if (spawner->Unknown4e43d0())
		{
			BOOL	can_spawn = FALSE;
			if ((IsOnGround()) && (spawner->mData->mConditions & 2))
				can_spawn = TRUE;
			else if ((IsInWater()) && (spawner->mData->mConditions & 1))
				can_spawn = TRUE;
			else if ((!IsOnGround()) && (!IsInWater()) && (spawner->mData->mConditions & 4))
				can_spawn = TRUE;

			float	dist = (target->GetPos() - mPos).Magnitude();
			if ((spawner->Unknown4e4480(target)) && (can_spawn) &&
				(spawner->mData->mMinRange <= dist) && (spawner->mData->mMaxRange >= dist))
				spawner->DoSpawn();
		}
	}
}

//******************************************************************************************
BOOL CUnit::Unknown99()	// FUNCTION: 0x004fdc10
{
	return TRUE;
}

//******************************************************************************************
void CUnit::AccumulateScore()	// FUNCTION: 0x004fdc20
{
	if (!mUnitData)
		return;
	switch (mAllegiance)
	{
	case 0:
		GAME.IncScore(-UPhysicsManager::GetBehaviourPriority(mUnitData));
		break;
	case 1:
		GAME.IncScore(UPhysicsManager::GetBehaviourPriority(mUnitData));
		break;
	}
	ListIterator<CSpawner>	spawners(&mSpawners);
	for (CSpawner *s = spawners.First(); s; s = spawners.Next())
		s->UpdateSpawnCount();
}

//******************************************************************************************
BOOL CUnit::ObeyGravity()	// FUNCTION: 0x004fdc90
{
	if ((mAI) && (mAI->mAIMode == 2))
		return FALSE;
	return TRUE;
}

//******************************************************************************************
void CUnit::SetAIState(EAIState inAIState)	// FUNCTION: 0x004fdcb0
{
	CUnitAI	*ai = mAI;
	if (ai)
	{
		if (inAIState == 1)
		{
			ai->mTarget = NULL;
			ai->mUnknown10 = 0;
		}
	}
	mAIState = inAIState;
}

//******************************************************************************************
void CUnit::Unknown100()	// FUNCTION: 0x004fdd00
{
	if ((mDeployState == 1) || (mDeployState == 2))
		return;
	FVector	side = FindNearestSafeSide();
	GoToPoint(side, FALSE);
	mDeployState = 1;
}

//******************************************************************************************
void CUnit::SetSpawnScript(char *inScript)	// FUNCTION: 0x004fdd60
{
	ListIterator<CSpawner>	spawners(&mSpawners);
	for (CSpawner *s = spawners.First(); s; s = spawners.Next())
	{
		s->mInit.mAttachScriptsToUnits = TRUE;
		strcpy(s->mInit.mSpawnScript, inScript);
	}
	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		if (c->ToRead())
			c->ToRead()->SetSpawnScript(inScript);
}

//******************************************************************************************
BOOL CUnit::IsDeploying()	// FUNCTION: 0x004fde10
{
	if ((mDeployState == 4) || (mDeployState == 3) || (mDeployState == 5))
		return TRUE;
	return FALSE;
}

//******************************************************************************************
void CUnit::StartDeploying()	// FUNCTION: 0x004fde30
{
	if (mDeployState)
		return;
	mDeployState = 3;
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("deploying"), TRUE, FALSE);
}

//******************************************************************************************
void CUnit::StartUndeploying()	// FUNCTION: 0x004fde70
{
	if (mDeployState != 4)
		return;
	mDeployState = 5;
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("undeploying"), TRUE, FALSE);
}

//******************************************************************************************
BOOL CUnit::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x004fdeb0
{
	if (GetRenderAnimation() != AM_INVALID)
	{
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("deploying"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("deployed"), TRUE, TRUE);
			mDeployState = 4;
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("undeploying"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("normal"), TRUE, TRUE);
			mDeployState = 0;
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("prefire"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("prefirehold"), TRUE, TRUE);
			return FALSE;
		}
		if ((GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("firing")) ||
			(GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("postfire")))
		{
			if (!mDeployState)
				SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("normal"), TRUE, TRUE);
			else
				SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("deployed"), TRUE, TRUE);
			return FALSE;
		}
		return SUPERTYPE::FinishedPlayingCurrentAnimation();
	}
	return TRUE;
}

//******************************************************************************************
// The message box is read once into a local: retail loads the pointer before the message is allocated (004fe230).
void CUnit::ConfirmedKill(CUnit *inKilled)	// FUNCTION: 0x004fe030
{
	if (!IsTargetAlligence(inKilled->GetAllegiance()))
		return;

	DWORD	id = 0;
	if ((mUnitData) && (mUnitData->mUnknown120) && (EVENT_MANAGER.GetTime() > mUnknown240))
	{
		mUnknown240 = EVENT_MANAGER.GetTime() + 40.0f;
		SINT	line = GAME.Random() % 3;
		WCHAR	*speaker, *text;
		if (!strcmp(mUnitData->mName, "Tara Fighter"))
		{
			speaker = TEXT.GetString(0x3c345);
			switch (line)
			{
				case 0:	id = 0x4175f6f7;	break;
				case 1:	id = 0x41f4807a;	break;
				case 2:	id = 0x427309fd;	break;
			}
			text = TEXT.GetString(id);
		}
		else if (!strcmp(mUnitData->mName, "Billy Fighter"))
		{
			speaker = TEXT.GetString(0xcb1fd);
			switch (line)
			{
				case 0:	id = 0x881c798e;	break;
				case 1:	id = 0x89198af1;	break;
				case 2:	id = 0x8a169c54;	break;
			}
			text = TEXT.GetString(id);
		}
		else
		{
			speaker = TEXT.GetString(0x19f8cf);
			switch (line)
			{
				case 0:	id = 0x8612e253;	break;
				case 1:	id = 0x870ff464;	break;
				case 2:	id = 0x880d0675;	break;
			}
			text = TEXT.GetString(id);
		}
		CMESSAGEBOX	*box = GAME.GetMessageBox();
		if (box)
#line 3099
			box->AddMessage(new(MT_UNIT_THING) CMessage(speaker, text, id, 0, this, 0, 10));
	}
}

//******************************************************************************************
void CUnit::EnableWeapon(char *name)	// FUNCTION: 0x004fe2b0
{
	ListIterator<CWeapon>	weapons(&mWeapons);
	for (CWeapon *w = weapons.First(); w; w = weapons.Next())
		if (!stricmp(name, w->GetName()))
			w->mActive = TRUE;
}

//******************************************************************************************
void CUnit::DisableWeapon(char *name)	// FUNCTION: 0x004fe310
{
	ListIterator<CWeapon>	weapons(&mWeapons);
	for (CWeapon *w = weapons.First(); w; w = weapons.Next())
	{
		if (!stricmp(name, w->GetName()))
		{
			w->mActive = FALSE;
			if (mCurrentWeapon == w)
				mCurrentWeapon = NULL;
			if (mAI)
				mAI->Unknown4ffdd0(NULL, FALSE);
			return;
		}
	}
}

//******************************************************************************************
void CUnit::EnableSpawner(char *name)	// FUNCTION: 0x004fe390
{
	ListIterator<CSpawner>	spawners(&mSpawners);
	for (CSpawner *s = spawners.First(); s; s = spawners.Next())
		if (!stricmp(name, s->GetDataName()))
			s->mEnabled = TRUE;
}

//******************************************************************************************
void CUnit::DisableSpawner(char *name)	// FUNCTION: 0x004fe3f0
{
	ListIterator<CSpawner>	spawners(&mSpawners);
	for (CSpawner *s = spawners.First(); s; s = spawners.Next())
	{
		if (!stricmp(name, s->GetDataName()))
		{
			s->mEnabled = FALSE;
			if (mCurrentSpawner == s)
				mCurrentSpawner = NULL;
			if (mAI)
				mAI->Unknown4ffdd0(NULL, FALSE);
			return;
		}
	}
}

//******************************************************************************************
CNormalSquadUnknownD4 *CUnit::GuideUnknown9()	// FUNCTION: 0x004fe480
{
	if (mGuide)
		return mGuide->GetPath();
	return NULL;
}

//******************************************************************************************
void CUnit::TeleportOrientation(const FMatrix &orientation)	// FUNCTION: 0x004fe4a0
{
	mCurrentOrientation = CEulerAngles(orientation);
	mUnknown120 = CEulerAngles(orientation);
	SUPERTYPE::TeleportOrientation(orientation);
}

//******************************************************************************************
void CUnit::SetSquad(CSquad *squad)	// FUNCTION: 0x004fe500
{
	mSquad = squad;
	if (squad)
	{
		WORLD.mSet30.Remove(this);
		WORLD.mSet20.Remove(this);
	}
}

//******************************************************************************************
void CUnit::Unknown107(float a)	// FUNCTION: 0x004fe540
{
	if (!mUnknown218)
		EVENT_MANAGER.AddEvent(UNIT_EVENT_4005, this, NEXT_FRAME);
	mUnknown218 += (SINT)(a * 100.0f);
	if (mUnknown218 > 100)
		mUnknown218 = 100;
	mUnknown21C = 10;
}

//******************************************************************************************
float CUnit::Unknown111()	// FUNCTION: 0x004fe5c0
{
	if ((mDeployState == 1) || (mDeployState == 2))
		return mUnitData->mUnknownB4 * 1.5f;
	return mUnitData->mUnknownB4;
}

//******************************************************************************************
void CUnit::Unknown116()	// FUNCTION: 0x004fe5f0
{
	EVENT_MANAGER.AddEvent(SHUTDOWN, this, NEXT_FRAME);
}

//******************************************************************************************
// The unit's default answers that no code above defined. Each compiles to a body another function already
// has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address
// each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x00459990
inline void CUnit::Unknown93()
{
}

// STAND-IN (layout): the unit's default answers that retail links as INLINE functions, defined inline HERE rather than
// in the class body. VC6's /OPT:ICF folds inline and out-of-line functions separately, and retail keeps each of these
// bodies twice (0x004014a0 and 0x004fdc10 both `return TRUE`, 0x004014c0 and 0x00452b60 both `ret 4`), so the slots'
// survivors name the class: these sit in the inline groups (link order notes 3b/3c). This object emits
// CUnit's table, as the class-body form would. That form (unit.h) loses BuildDeviceList 00529350 (3,931 bytes) and
// CPDTimeline 004c2640 (4 bytes) (measured 2026-10-15 on bdebcc70 + this batch). Move them back into the class once the
// witnesses' real header context is known.
// FUNCTION: 0x004bfc60 ?GetStealth@CUnit@@UAEMXZ
inline float CUnit::GetStealth()
{
	return 0;
}

// FUNCTION: 0x004014a0 ?Unknown97@CUnit@@UAEHXZ
inline BOOL CUnit::Unknown97()
{
	return TRUE;
}

// FUNCTION: 0x004014a0 ?OnScanner@CUnit@@UAEHXZ
inline BOOL CUnit::OnScanner()
{
	return TRUE;
}

// FUNCTION: 0x004bfc60 ?Unknown106@CUnit@@UAEMXZ
inline float CUnit::Unknown106()
{
	return 0;
}

// FUNCTION: 0x004014a0 ?Unknown108@CUnit@@UAEHXZ
inline BOOL CUnit::Unknown108()
{
	return TRUE;
}

// FUNCTION: 0x004014c0 ?Unknown114@CUnit@@UAEXM@Z
inline void CUnit::Unknown114(float a)
{
}

// FUNCTION: 0x004014a0 ?Unknown115@CUnit@@UAEHXZ
inline BOOL CUnit::Unknown115()
{
	return TRUE;
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00409760 ?First@GenericListIterator@@QAEPAXXZ
