// Building.cpp: reconstructed from Building.obj (004170a0..0041829f); Stuart's release has no copy. The object
// starts with its maths constants and the retail copies of CUnit's inline members (004175b0..00417630).

#include "common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include "unit.h"
#include "world.h"
#include "mesh.h"
#include "staticshadows.h"
#include "Script.h"
#include "DestructableSegmentsController.h"
#include "map.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "ParticleManager.h"
#include "explosion.h"
#include "InitThing.h"
#include "engine.h"
#include "renderinfo.h"
#include "DebugLog.h"
// FUNCTION: 0x004170a0 init:ZERO_FVECTOR
// FUNCTION: 0x004170c0 init:ID_FMATRIX
// FUNCTION: 0x004175b0 ?_GetClassID@CUnit@@UAEHXZ
// FUNCTION: 0x004175c0 ?GetThreat@CUnit@@UAEMXZ
// FUNCTION: 0x004175d0 ?GetImportance@CUnit@@UAEMXZ
// FUNCTION: 0x004175e0 ?GetCurrentTarget@CUnit@@UAEPAV1@XZ
// FUNCTION: 0x00417600 ?SetInfinateEnergy@CUnit@@UAEXH@Z
// FUNCTION: 0x00417610 ?GetSoundMaterial@CUnit@@UAEHXZ
// FUNCTION: 0x00417620 ?BounceFactor@CUnit@@UAEMXZ
// FUNCTION: 0x00417630 ?CanBeLocked@CUnit@@UAEHXZ
// FUNCTION: 0x00417650 ?_GetClassName@CBuilding@@UAEPADXZ
// FUNCTION: 0x004176a0 ??_GCBuilding@@UAEPAXI@Z

SINT CBuilding::sDrawDebugCount = 0;

//******************************************************************************************
void CBuilding::Init(CInitThing *init)	// FUNCTION: 0x00417190
{
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY | THING_TYPE_UNKNOWN_08000000;
	init->mInitCST.mUseFixedMeshTransforms = (((CUnitInitThing *)init)->mStats->mUnknown13C == 0);

	mUnknown258 = -1.0f;
	mStaticShadow = NULL;

#line 50
	mSegments = new(MEMTYPE_UNKNOWN) CDestructableSegmentsController(this, 2.0f, TRUE, 0);
	mMotionController = new(MEMTYPE_UNKNOWN) CDestructableSegmentsMotionController(mSegments);

	SUPERTYPE::Init(init);

	if (mSegments->mUnknown20 <= 0)
	{
		delete mSegments;
		mSegments = NULL;
		delete mMotionController;
		mMotionController = NULL;
	}

	EAnimMode	anim = mRenderThing->GetRTMesh()->GetAnimModeByName("closed");
	if (anim != AM_INVALID)
		SetAnimMode(anim, TRUE, TRUE);

	mUnknown254 = 3;
	mUnknown25C = 0;

	anim = mRenderThing->GetRTMesh()->GetAnimModeByName("notshut");
	if (anim != AM_INVALID)
	{
		SetAnimMode(anim, TRUE, TRUE);
		mUnknown260 = TRUE;
	}
	else
		mUnknown260 = FALSE;

	mUnknown264 = 3;
	mUnknown268 = -100.0f;

	StickToGround();
	CreateRepairPadAI(init);
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Idle"), TRUE, TRUE);
	WORLD.AddToOccupancy(this);
}

//******************************************************************************************
void CBuilding::CreateRepairPadAI(CInitThing *init)	// FUNCTION: 0x00417390
{
	if (!stricmp(mUnitData->mName, "Forseti Repair Pad"))
	{
#line 100
		mAI = new(MEMTYPE_AI) CRepairPadAI(this, init);
		mUnknown1F4 = (void *)1;
	}
	else
#line 104
		mAI = new(MEMTYPE_AI) CUnitAI(this, init);
}

// FUNCTION: 0x00417480 ??_GCRepairPadAI@@UAEPAXI@Z
// FUNCTION: 0x004174a0 ??1CRepairPadAI@@UAE@XZ

//******************************************************************************************
void CBuilding::Render(DWORD flags)	// FUNCTION: 0x00417540
{
	if ((!mSegments) && (mFlags & TF_DYING))
		return;

	SUPERTYPE::Render(flags);
	STATICSHADOWS.UpdateVisibility(this, FALSE);
	if ((sDrawDebugCount > 0) && (--sDrawDebugCount > 0))
		DrawDebugStuff3d();
}

//******************************************************************************************
CBuilding::~CBuilding()	// FUNCTION: 0x00417590
{
}

//******************************************************************************************
void CBuilding::SetThingType(ULONG t)	// FUNCTION: 0x00417660
{
	SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_40000000 | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_BUILDING | THING_TYPE_STATIONARY);
}

//******************************************************************************************
CStaticShadow *CBuilding::GetStaticShadow()	// FUNCTION: 0x00417680
{
	return mStaticShadow;
}

//******************************************************************************************
void CBuilding::SetStaticShadow(CStaticShadow *shadow)	// FUNCTION: 0x00417690
{
	mStaticShadow = shadow;
}

//******************************************************************************************
// The two mesh names are read into locals first: retail's argument order and scratch registers around the two sprintfs
// (README, how a value is read); with the reads inside the calls the function is 22 instructions off.
void CBuilding::InitRenderThing(CInitThing *init)	// FUNCTION: 0x004176c0
{
	CInitRenderThing	irt;
	char				name[400];

	irt.mThing = this;

	char	*mesh = ((CUnitInitThing *)init)->mStats->mUnknown2c;
	sprintf(name, "%s.msh", mesh);
	strcpy(irt.mName, name);
	char	*lod = ((CUnitInitThing *)init)->mStats->mUnknown30;
	sprintf(name, "%s.msh", lod);
	strcpy(irt.mUnknown100, name);

	if ((mRenderThing = CreateRenderThing(4)))
		mRenderThing->Init(&irt);
}

//******************************************************************************************
void CBuilding::Shutdown()	// FUNCTION: 0x00417870
{
	WORLD.RemoveFromOccupancy(this);
	STATICSHADOWS.UpdateVisibility(this, TRUE);
	SUPERTYPE::Shutdown();
}

//******************************************************************************************
void CBuilding::Move()	// FUNCTION: 0x004178a0
{
	UpdateTimer();

	if (mUnknown254 == 2)
	{
		if (!AnySpawnerUnknown4e4420())
		{
			if ((mUnknown25C < EVENT_MANAGER.GetTime()) && (mUnknown254 == 2))
			{
				EAnimMode	anim = mRenderThing->GetRTMesh()->GetAnimModeByName("closing");
				if (anim == AM_INVALID)
					mUnknown254 = 3;
				else
				{
					mUnknown254 = 1;
					SetAnimMode(anim, TRUE, FALSE);
				}
			}
		}
		else
			mUnknown25C = EVENT_MANAGER.GetTime() + 8.0f;
	}
	else if (AnySpawnerUnknown4e4420())
		Unknown115();

	if ((mUnknown260) && (mUnknown264 == 1) && (mUnknown268 + 15.0f < EVENT_MANAGER.GetTime()))
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("unshutting"), TRUE, FALSE);
		mUnknown264 = 2;
	}
}

//******************************************************************************************
void CBuilding::Damage(float amount, CThing *inByThis, BOOL inDamageShields, int mesh_part_no)	// FUNCTION: 0x004179a0
{
	mUnknown268 = EVENT_MANAGER.GetTime();
	if ((mUnknown260) && (mUnknown264 == 3))
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("shutting"), TRUE, FALSE);
		mUnknown264 = 0;
	}
	if ((mSegments) || (!(mFlags & TF_DYING)))
		SUPERTYPE::Damage(amount, inByThis, inDamageShields, mesh_part_no);
}

//******************************************************************************************
void CBuilding::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x00417a20
{
	SUPERTYPE::Hit(other_thing, report);
}

//******************************************************************************************
// Set the initial motion and mesh of a newly created debris particle.
static inline void InitialiseBuildingDebris(CParticleMesh *particle, FVector &velocity, CBuilding *building)
{
	velocity.Z = velocity.Z * 2 - 0.04f;
	particle->mVelocity = velocity;
	CRenderThing *renderThing = building->GetRenderThing();
	if (renderThing)
	{
		CMESH *mesh = renderThing->Unknown10();
		particle->mMesh = mesh;
		particle->mRadius = mesh->mRadius;
	}
}

// Every inline decision and the frame follow the weapons reconstruction (kept from its frozen evidence):
// GetMissionScript stands for the real read of mMissionScript (its expansion spends the budget that sends
// GenericSPtrSet::First out of line, 00417afc), the debris helper, link.Full(), GetAllegiance() and the explosion id
// local are the sites that put SetPos out of line (00417d20). The debris origin is the position one unit down,
// `mPos - FVector(0, 0, 1.0f)`: VC6 orders each commutative add by its operands' symbol ids (the larger first), and the
// subtraction's operator- site is what leaves the origin's X and Y with fresh ids above the displacement's, so the
// adds come out position-first for X and Y and with faddp for Z as in retail (00417cfb..00417d09; an id-simulation model).
BOOL CBuilding::StartDieProcess()	// FUNCTION: 0x00417a40
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;

	ReleaseChildren();

	if (!mSegments)
	{
		SINT explosionId = mUnitData->mExplosion;
		if (CExplosion *explosion = UPhysicsManager::SpawnExplosion(explosionId))
		{
			CExplosionInitThing	init;
			init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mExplosion);
			init.mPos = mPos;
			init.mAllegiance = GetAllegiance();
			explosion->Init(&init);
		}
		mLife = -1.0f;

		CParticleDescriptor	*pd;
		if (mUnitData->mExplosion != -1)
		{
			pd = PARTICLE_SET.FindByName(UPhysicsManager::GetExplosionData(mUnitData->mExplosion)->mName);
			PARTICLE_MANAGER.CreateEffect(pd, NULL, mPos, 0, 0);
		}

		// the debris
		pd = PARTICLE_SET.FindByName("Generic Mesh");
		for (SINT i = 0; i < 40; i++)
		{
			CParticleEffectLink	link;
			PARTICLE_MANAGER.CreateEffect(pd, &link, ZERO_FVECTOR, 0, 0);

			SINT	a = (rand() & 0xff) - 128;
			SINT	b = (rand() & 0xff) - 128;
			SINT	c = (rand() & 0xff) - 128;
			FVector	vel;
			vel.Set(float(c) * 0.0003125f, float(b) * 0.0003125f, float(a) * 0.0003125f);
			FVector	displacement = vel * 20.0f;
			FVector	pos = displacement + (mPos - FVector(0, 0, 1.0f));
			link.SetPos(pos);
			if (link.Full())
			{
				CParticleMesh *particle = link.mEffect->mParticle;
				if (particle)
					InitialiseBuildingDebris(particle, vel, this);
			}
		}
	}

	if (GetMissionScript())
		EVENT_MANAGER.AddEvent(5000, this, NEXT_FRAME);

	return TRUE;
}

//******************************************************************************************
void CBuilding::HandleEvent(CEvent *event)	// FUNCTION: 0x00417df0
{
	if ((event->GetEventNum() == 5000) && (mMissionScript))
	{
		mMissionScript->Died();
		delete mMissionScript;
		mMissionScript = NULL;
	}
	else
		SUPERTYPE::HandleEvent(event);
}

//******************************************************************************************
// The by-value copies of origin and ori that SetWorld takes are written out as locals. They emit no code; they change
// the frame, so the part's transform sits in the slots retail has (found by a readform pass).
void CBuilding::DrawDebugStuff3d()	// FUNCTION: 0x00417e30
{
	CRenderThing	*rt = mRenderThing;
	if (!rt)
		return;

	FMatrix	ori = ID_FMATRIX;
	FVector	origin(0, 0, 0);
	FVector	worldPos = origin;
	FMatrix	worldOri = ori;
	RENDERINFO.SetWorld(worldPos, worldOri);

	CMESH	*mesh = rt->GetRTMesh();
	if (!mesh)
		return;

	// each part's collision box: grey for its own, green for a separate collision part
	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		CMeshPart	*part = mesh->mParts[i];
		CMeshPart	*cp = part->GetMeshDataPart();
		if (!cp)
			continue;

		CBoundingBox	*box = part->mBoundingBox;
		DWORD			col = 0xff7f7f7f;
		if (part != cp)
		{
			box = cp->mBoundingBox;
			col = 0xff007f00;
		}

		if ((part) && (!part->mIsChained))
		{
			FVector	pos;
			FMatrix	m;
			part->GetWorldTransforms(0, 0, 0, mRenderThing->mThing, &m, &pos, false, 0, 0);
			RENDERINFO.SetWorld(pos + m * box->mOrigin, ori);
			ENGINE.DrawDebugCuboid(col, box->mAxes, FVector(0, 0, 0), mOrientation, ENGINE.GetOutlineTexture());
		}
	}
}

//******************************************************************************************
BOOL CBuilding::Unknown115()	// FUNCTION: 0x00418090
{
	if (mUnknown254 == 2)
		return TRUE;

	if (mUnknown254 == 3)
	{
		EAnimMode	anim = mRenderThing->GetRTMesh()->GetAnimModeByName("opening");
		if (anim == AM_INVALID)
			mUnknown254 = 2;
		else
		{
			mUnknown254 = 0;
			SetAnimMode(anim, TRUE, FALSE);
		}
		mUnknown25C = EVENT_MANAGER.GetTime() + 8.0f;
	}
	return FALSE;
}

//******************************************************************************************
void CBuilding::RenderImposter()	// FUNCTION: 0x00418100
{
	if ((!mSegments) || (mSegments->mUnknown2C != 1))
		SUPERTYPE::RenderImposter();
}

//******************************************************************************************
BOOL CBuilding::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x00418120
{
	if (GetRenderAnimation() != AM_INVALID)
	{
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("opening"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("open"), TRUE, TRUE);
			mUnknown254 = 2;
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("closing"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("closed"), TRUE, TRUE);
			mUnknown254 = 3;
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("unshutting"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("notshut"), TRUE, TRUE);
			mUnknown264 = 3;
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("shutting"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("shut"), TRUE, TRUE);
			mUnknown264 = 1;
			return FALSE;
		}
		return SUPERTYPE::FinishedPlayingCurrentAnimation();
	}
	return TRUE;
}

//******************************************************************************************
// The building's grounding (as CSimpleBuilding's, SimpleBuilding.cpp). It compiles to CSimpleBuilding's body, so the
// linker folds it into that one (/OPT:ICF).

// FUNCTION: 0x004dfd10
void CBuilding::StickToGround()
{
	CActor::StickToGround();
	if (MAP.GetWaterLevel() < mPos.Z)
	{
		mPos.Z = MAP.GetWaterLevel();
		mOldPos.Z = MAP.GetWaterLevel();
	}
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00415060 ??_GCUnitAI@@UAEPAXI@Z
