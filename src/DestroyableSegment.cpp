// DestroyableSegment.cpp: reconstructed from DestroyableSegment.obj (004423b0..00443ecf); Stuart's release has no
// copy. The segments of a building or unit that break separately (see DestroyableSegment.h). The object's first
// initializer (004423b0..004424aa) belongs to the object before it; this one starts with its maths constants.
// Specimen: BEA.exe.original.backup, SHA-256
// 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750.

#include "common.h"
#include "SpawnerThng.h"
#include "DestroyableSegment.h"
#include "DestructableSegmentsController.h"
#include "unit.h"
#include "world.h"
#include "game.h"
#include "engine.h"
#include "landscape.h"
#include "mesh.h"
#include "RenderThing.h"
#include "ParticleManager.h"
#include "ParticleSet.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "InitThing.h"
#include "eventmanager.h"
#include "Event.h"
#include "DebugLog.h"

// FUNCTION: 0x004424b0 init:ZERO_FVECTOR
// FUNCTION: 0x004424d0 init:ID_FMATRIX

//******************************************************************************************
CDestroyableSegment::CDestroyableSegment(CDestructableSegmentsController *controller, SINT part, CDestroyableSegment *parent, float size)	// FUNCTION: 0x004425a0
	: mPart(part), mHealth(1.0f), mMaxHealth(1.0f), mLastDamageTime(-1.0f), mLastDamageAmount(0), mVulnerable(TRUE),
	  mParent(parent), mSize(size), mDestroyed(FALSE), mController(controller)
{
	WORLD.mOwnedF0.Add(this);
}

// FUNCTION: 0x00442640 ??_GCDestroyableSegment@@UAEPAXI@Z

//******************************************************************************************
CDestroyableSegment::~CDestroyableSegment()	// FUNCTION: 0x00442660
{
	WORLD.mOwnedF0.Remove(this);

	ListIterator<CDestroyableSegment>	it(&mChildren);
	for (CDestroyableSegment *child = it.First(); child; child = it.Next())
		delete child;
}

//******************************************************************************************
void CDestroyableSegment::RegisterChild(CDestroyableSegment *child)	// FUNCTION: 0x00442700
{
	mChildren.Add(child);
}

//******************************************************************************************
// init lives in the block after data's definition, so VC6 keeps no memory edges from its stores to the read of
// data->mExplosion, which runs between the base constructor and the member stores as in retail (Round 004db150's lever).
void CDestroyableSegment::SpawnPickup(FVector pos)	// FUNCTION: 0x00442710
{
	CUnitData	*data = mController->mOwner->mUnitData;
	if (data && data->mExplosion)
	{
		CExplosion	*explosion = UPhysicsManager::SpawnExplosion(data->mExplosion);
		CExplosionInitThing	init;
		init.mBehaviour = UPhysicsManager::GetExplosionData(data->mExplosion);
		init.mColType = kCollideGround;
		init.mAllegiance = (EAllegiance)mController->mOwner->mAllegiance;
		if (explosion)
		{
			init.mPos = pos;
			if (mController->mUnknown28 == 1)
				init.mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
			else
				init.mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_BUILDING;
			init.mInitCST.mIgnoreThing = mController->mOwner;
			explosion->Init(&init);
		}
	}
}

//******************************************************************************************
void CDestroyableSegment::SetLife(float life, float total)	// FUNCTION: 0x00442870
{
	mMaxHealth = mHealth = mSize / total * life;
}

//******************************************************************************************
float CDestroyableSegment::GetActiveHealth()	// FUNCTION: 0x00442890
{
	float	health = 0;
	if (mVulnerable && !CanBreak() && mHealth != 0)
		health = mMaxHealth;

	ListIterator<CDestroyableSegment>	it(&mChildren);
	for (CDestroyableSegment *child = it.First(); child; child = it.Next())
		health += child->GetActiveHealth();

	return health;
}

//******************************************************************************************
float CDestroyableSegment::GetTotalHealth()	// FUNCTION: 0x00442900
{
	float	health = 0;
	if (mVulnerable && !CanBreak())
		health = mHealth;

	ListIterator<CDestroyableSegment>	it(&mChildren);
	for (CDestroyableSegment *child = it.First(); child; child = it.Next())
		health += child->GetTotalHealth();

	return health;
}

//******************************************************************************************
void CDestroyableSegment::Damage(float amount, CThing *by)	// FUNCTION: 0x00442960
{
	if (mVulnerable)
	{
		mHealth -= amount;
		mLastDamageTime = EVENT_MANAGER.GetTime();
		mLastDamageAmount = amount;
		if (mHealth < 0)
			mHealth = 0;
	}
}

//******************************************************************************************
void CDestroyableSegment::ScheduleChildBreaks()	// FUNCTION: 0x004429a0
{
	float	time;
	ListIterator<CDestroyableSegment>	it(&mChildren);
	for (CDestroyableSegment *child = it.First(); child; child = it.Next())
	{
		if (!GetType())
			child->Break();
		else if (GetType() == DST_SWAP)
		{
			time = EVENT_MANAGER.GetTime() + (0.5f + float((rand() & 0xff) - 128) * (1.0f / 256.0f));
			EVENT_MANAGER.AddEvent(SEGMENT_BREAK, child, time);
		}
		else
		{
			time = EVENT_MANAGER.GetTime() + (0.1f + float((rand() & 0xff) - 128) * (1.0f / 1280.0f));
			EVENT_MANAGER.AddEvent(SEGMENT_BREAK, child, time);
		}
	}
}

//******************************************************************************************
void CDestroyableSegment::ActivateAll()	// FUNCTION: 0x00442a80
{
	mVulnerable = TRUE;

	ListIterator<CDestroyableSegment>	it(&mChildren);
	for (CDestroyableSegment *child = it.First(); child; child = it.Next())
		child->ActivateAll();
}

//******************************************************************************************
void CDestroyableSegment::DamageChildren(float amount)	// FUNCTION: 0x00442ac0
{
	ListIterator<CDestroyableSegment>	it(&mChildren);
	for (CDestroyableSegment *child = it.First(); child; child = it.Next())
		child->Damage(amount, mController->mOwner);
}

//******************************************************************************************
BOOL CDestroyableSegment::CanBreak()	// FUNCTION: 0x00442b00
{
	if (mParent && !mParent->mDestroyed)
		return mParent->CanBreak();
	return FALSE;
}

//******************************************************************************************
// Retail keeps this mesh-part pointer across effect callbacks (00442b82, 00442b97, 00442c20),
// snapshots the spawner mount before MapMountName (00442c5c), and walks the removal set through
// its own cursor (00442cd0..00442d09). These instruction-order facts do not establish that
// shipping callbacks replace a part.
void CDestroyableSegment::Break()	// FUNCTION: 0x00442b20
{
	mDestroyed = TRUE;
	mHealth = 0;
	mController->mChanged = TRUE;

	CRenderThing	*rt = mController->GetOwner()->GetRenderThing();
	if (!rt)
		return;
	CMesh	*mesh = rt->GetRTMesh();
	if (!mesh)
		return;

	// stop the effects on the part's children
	CMeshPart	*part = mesh->mParts[mPart];
	SINT	numchildren = part->mNumChildren;
	SINT	n;
	for (n = 0; n < numchildren; n++)
	{
		CMeshPart *child = part->mChildren[n];
		SINT effect = rt->FindEffect(child);
		if (effect != -1)
		{
			CParticleEffectLink	*link = (CParticleEffectLink *)rt->GetEffect(effect);
			if (link)
				link->Kill();
		}
	}

	// and the spawners mounted on them
	CUnit *owner = mController->GetOwner();
	SPtrSet<CSpawner>	remove;
	ListIterator<CSpawner>	sit(&owner->mSpawners);
	for (CSpawner *spawner = sit.First(); spawner; spawner = sit.Next())
	{
		BOOL	found = FALSE;
		for (n = 0; n < numchildren; n++)
		{
			CMeshPart	*child = part->mChildren[n];
			for (SINT e = 0; e < mesh->mNumEmitters; e++)
			{
				CEmitter	*emitter = &mesh->mEmitters[e];
				if (!found && emitter && emitter->mMeshPart == child)
				{
					SINT mount = spawner->mMount;
					if (UPhysicsManager::MapMountName(emitter->mName) == mount)
					{
						remove.Add(spawner);
						found = TRUE;
					}
				}
			}
		}
	}

	for (CSpawner *s = remove.First(); s; s = remove.Next())
		mController->GetOwner()->RemoveSpawner(s);

	ScheduleChildBreaks();
}

//******************************************************************************************
void CDestroyableSegment::Update()	// FUNCTION: 0x00442d40
{
	CMeshPart	*part;
	CRenderThing	*rt = mController->mOwner->GetRenderThing();
	if (!rt)
		part = NULL;
	else
	{
		CMesh	*mesh = rt->GetRTMesh();
		if (!mesh)
			part = NULL;
		else
			part = mesh->mParts[mPart];
	}

	if (!mDestroyed && part && GAME.Random() % 60 == 1)
	{
		FMatrix	ori;
		FVector	pos;
		part->GetWorldTransforms(0, 0, 0, mController->mOwner, &ori, &pos, false, TRUE, 0);

		float	x = float(GAME.Random() % 65536) * part->mBoundingBox->mAxes.X * (2.0f / 65536.0f) - part->mBoundingBox->mAxes.X;
		float	y = float(GAME.Random() % 65536) * part->mBoundingBox->mAxes.Y * (2.0f / 65536.0f) - part->mBoundingBox->mAxes.Y;
		float	z = float(GAME.Random() % 65536) * part->mBoundingBox->mAxes.Z * (2.0f / 65536.0f) - part->mBoundingBox->mAxes.Z;
		FVector	offset = part->mBoundingBox->mOrigin + FVector(x, y, z);
		SpawnPickup(ori * offset + pos);
	}

	ListIterator<CDestroyableSegment>	it(&mChildren);
	for (CDestroyableSegment *child = it.First(); child; child = it.Next())
		child->Update();
}

//******************************************************************************************
// STAND-IN (reconstructed helper; review F 2026-10-15): the box point as a helper returning component sums; Update's inline
// spelling (origin + FVector(x, y, z)) loses 00442f60.
static inline FVector GetSegmentBoxPoint(CMeshPart *part)
{
	float x = float(GAME.Random() % 65536) * part->mBoundingBox->mAxes.X * (2.0f / 65536.0f) - part->mBoundingBox->mAxes.X;
	float y = float(GAME.Random() % 65536) * part->mBoundingBox->mAxes.Y * (2.0f / 65536.0f) - part->mBoundingBox->mAxes.Y;
	float z = float(GAME.Random() % 65536) * part->mBoundingBox->mAxes.Z * (2.0f / 65536.0f) - part->mBoundingBox->mAxes.Z;
	return FVector(part->mBoundingBox->mOrigin.X + x, part->mBoundingBox->mOrigin.Y + y, part->mBoundingBox->mOrigin.Z + z);
}

void CDestroyableSegment::SpawnRubble()	// FUNCTION: 0x00442f60
{
	CRenderThing	*rt = mController->GetOwner()->GetRenderThing();
	if (!rt)
		return;
	CMesh	*mesh = rt->GetRTMesh();
	if (!mesh)
		return;
	CMeshPart	*part = mesh->mParts[mPart];
	if (!part)
		return;

	CUnitData	*data = mController->GetOwner()->mUnitData;
	CParticleDescriptor	*pd = PARTICLE_SET.FindByName("Generic Mesh");
	SINT	count = SINT(mSize * 3.0f);

	FMatrix	ori;
	FVector	partpos;
	part->GetWorldTransforms(0, 0, 0, mController->GetOwner(), &ori, &partpos, false, TRUE, 0);

	for (SINT n = 0; n < count; n++)
	{
		FVector pos = ori * GetSegmentBoxPoint(part) + partpos;

		CParticleEffectLink	link;
		if (data->mExplosion && n % 12 == 0)
		{
			ENGINE.AddDamage(pos.X, pos.Y, 6);
			SpawnPickup(pos);
		}
		PARTICLE_MANAGER.CreateEffect(pd, &link, ZERO_FVECTOR, 0, 0);
		link.SetPos(pos);

		FVector	vel = pos - mController->GetOwner()->GetPos();
		vel.Normalise();
		vel *= (float(GAME.Random() % 65536) * (1.0f / 65536.0f)) * 0.07f;
		if (link.mEffect)
		{
			CParticleMesh	*mp = link.mEffect->mParticle;
			if (mp)
			{
				mp->mVelocity = vel;
				CMesh	*rubble = mController->GetOwner()->GetRenderThing()->Unknown10();
				if (!rubble)
				{
					static BOOL	warned = FALSE;
					if (!warned)
					{
						warned = TRUE;
						LOG.AddMessage("WOOPS: You forgot to set rubble for mesh '%s'", mController->GetOwner()->GetRenderThing()->GetRTMesh()->mName);
					}
				}
				else
				{
					mp->mMesh = rubble;
					mp->mRadius = rubble->mRadius;
				}
			}
		}
	}
}

//******************************************************************************************
BOOL CDestroyableCoreSegment::AreChildrenDestroyed()	// FUNCTION: 0x004433f0
{
	if (!mChildren.Size())
	{
		LOG.AddMessage("Warning: First core part has no children");
		return TRUE;
	}

	ListIterator<CDestroyableSegment>	it(&mChildren);
	for (CDestroyableSegment *child = it.First(); child; child = it.Next())
	{
		if (!child->GetType() && !child->mDestroyed)
			return FALSE;
	}
	return TRUE;
}

//******************************************************************************************
void CDestroyableSegment::HandleEvent(CEvent *event)	// FUNCTION: 0x00443460
{
	if (event->GetEventNum() == SEGMENT_BREAK)
		Break();
}

//******************************************************************************************
CDestroyableCoreSegment::CDestroyableCoreSegment(CDestructableSegmentsController *controller, SINT part, CDestroyableSegment *parent, float size, SINT number)	// FUNCTION: 0x00443480
	: CDestroyableSegment(controller, part, parent, size), mNumber(number), mTotalTime(0), mTime(0), mCascading(FALSE)
{
}

// FUNCTION: 0x004434c0 ?GetCoreTime@CDestroyableCoreSegment@@UAEMXZ
// FUNCTION: 0x004434d0 ??_GCDestroyableCoreSegment@@UAEPAXI@Z
// FUNCTION: 0x004434f0 ??1CDestroyableCoreSegment@@UAE@XZ

//******************************************************************************************
void CDestroyableCoreSegment::SetLife(float life, float total)	// FUNCTION: 0x00443590
{
	if (mNumber == 1)
		mMaxHealth = mHealth = 0;
	else
		mMaxHealth = mHealth = mSize / total * life * 5.0f;
}

//******************************************************************************************
BOOL CDestroyableCoreSegment::CanBreak()	// FUNCTION: 0x004435c0
{
	if (mCascading == TRUE)
		return TRUE;
	return CDestroyableSegment::CanBreak();
}

//******************************************************************************************
void CDestroyableCoreSegment::Damage(float amount, CThing *by)	// FUNCTION: 0x004435f0
{
	if (mNumber == 1)
		return;

	CDestroyableSegment::Damage(amount, by);
	if (mHealth <= 0 && !mDestroyed)
	{
		SpawnRubble();
		Break();
	}
}

//******************************************************************************************
void CDestroyableCoreSegment::Break()	// FUNCTION: 0x00443660
{
	if (mDestroyed == TRUE)
		return;

	if (mHealth == 0.0)
	{
		CDestroyableSegment::Break();
		return;
	}

	if (!CanBreak())
	{
		mCascading = TRUE;
		mTime = mParent->GetCoreTime();
		EVENT_MANAGER.AddEvent(SEGMENT_CORE_CASCADE, this, NEXT_FRAME);
	}
}

//******************************************************************************************
void CDestroyableCoreSegment::HandleEvent(CEvent *event)	// FUNCTION: 0x004436d0
{
	SINT	num = event->GetEventNum();
	switch (num)
	{
	case SEGMENT_CORE_CASCADE:
		mController->mChanged = TRUE;
		mTime += mController->mOwner->Gravity();
		mTotalTime += mTime;
		if (mTotalTime < mController->GetUnknown14())
		{
			EVENT_MANAGER.AddEvent(SEGMENT_CORE_CASCADE, this, NEXT_FRAME);
			Update();
		}
		else
		{
			mCascading = FALSE;
			SpawnRubble();
			CDestroyableSegment::Break();
		}
		break;
	default:
		if (num == SEGMENT_BREAK)
			Break();
	}
}

//******************************************************************************************
void CDestroyableSwapSegment::Damage(float amount, CThing *by)	// FUNCTION: 0x00443780
{
	SINT	stage = GetDamageStage();
	CDestroyableSegment::Damage(amount, by);
	if (stage != GetDamageStage())
	{
		SpawnRubble();
		if (!stage)
			ScheduleChildBreaks();
		mSize *= 0.5f;
		mController->mChanged = TRUE;
	}
	if (mHealth == 0)
		Break();
}

//******************************************************************************************
void CDestroyableSwapSegment::Break()	// FUNCTION: 0x00443810
{
	if (!mSwapped)
	{
		SpawnRubble();
		mSwapped = TRUE;
	}
	CDestroyableSegment::Break();
}

//******************************************************************************************
SINT CDestroyableSwapSegment::GetDamageStage()	// FUNCTION: 0x00443830
{
	if (mMaxHealth != 0)
	{
		SINT	stage = SINT(floor((mMaxHealth - mHealth) / mMaxHealth * mNumStages + 0.5f));
		if (stage > mNumStages)
			stage = mNumStages;
		if (stage >= 0)
			return stage;
	}
	return 0;
}

//******************************************************************************************
void CDestroyableExtraSegment::Damage(float amount, CThing *by)	// FUNCTION: 0x00443890
{
	CDestroyableSegment::Damage(amount, by);
	if (mHealth <= 0 && !mDestroyed)
	{
		mDirection = by->GetPos() - by->GetOldPos();
		mDirection.Normalise();
		mDirection *= 0.03f;
		mHasDirection = TRUE;
		Break();
	}
}

//******************************************************************************************
void CDestroyableExtraSegment::Break()	// FUNCTION: 0x004439c0
{
	if (!mDestroyed && mController->mUnknown20 > 0)
		SpawnRubble();
	CDestroyableSegment::Break();
}

//******************************************************************************************
void CDestroyableExtraUseMeshSegment::SetLife(float life, float total)	// FUNCTION: 0x004439f0
{
	if (mController->mUnknown20 > 0)
		mHealth = 0.001f;
	else
		mMaxHealth = mHealth = mSize / total * life;
}

//******************************************************************************************
// The rt -> mesh -> part lookup of a segment, with its two early NULL exits (the sequence is written out in Break,
// Update and both SpawnRubble). Helper name and signature are ours: the evidence is the inline-budget simulator, which gives
// retail's out-of-line order for SpawnRubble 00443a20 at an own size of 554 or less with two more offered sites, and
// this helper supplies the 54 own units those need (594 -> 540). Against it: using it in the ordinary SpawnRubble
// 00442f60 breaks that function's match, so the original did not use one helper everywhere (Update stays matched).
static inline CMeshPart *PartOf(CDestroyableSegment *segment)
{
	CRenderThing	*rt = segment->mController->GetOwner()->GetRenderThing();
	if (!rt)
		return NULL;
	CMesh	*mesh = rt->GetRTMesh();
	if (!mesh)
		return NULL;
	return mesh->mParts[segment->mPart];
}

// The mesh particle shares the sprite tile fields: CPDMesh::Unknown10 passes mU0, mU1, mV0
// to MakeRotationF as yaw, pitch, roll (004c4dc0). In memory those floats are
// yaw at +0xc8, roll at +0xcc and pitch at +0xd0. STAND-IN (declared view; review F 2026-10-15): this local view and helper are
// reconstructed names and source spelling, not a recovered original type; they touch only those three floats.
struct MeshAngularComponents
{
	float yaw, roll, pitch;
};

static inline void PublishMeshMotion(CParticleMesh *mp, const FVector &spin)
{
	MeshAngularComponents &angular = *(MeshAngularComponents *)&mp->mU0;
	angular.yaw = spin.X;
	angular.pitch = spin.Y;
	angular.roll = spin.Z;
}

// The ordinary rubble with no pieces (its size is zeroed first), then the part's own mesh flies off
// as one particle. The transform position is also the translated mesh centre, so its lifetime owns
// both values. Together with the angular-field view this reproduces retail's 0x74 frame, the matrix
// row sums and the spin/velocity publication order (00443e0b..00443e3d): the whole 1152-byte
// section and all 42 relocations match the pristine specimen named at the top of this file.
void CDestroyableExtraUseMeshSegment::SpawnRubble()	// FUNCTION: 0x00443a20
{
	mSize = 0;
	CDestroyableSegment::SpawnRubble();

	CMeshPart	*part = PartOf(this);
	if (!part)
		return;

	CParticleDescriptor	*pd = PARTICLE_SET.FindByName("Generic Mesh");
	CParticleEffectLink	link;
	PARTICLE_MANAGER.CreateEffect(pd, &link, ZERO_FVECTOR, 0, 0);

	FMatrix	ori;
	FVector	pos;
	part->GetWorldTransforms(0, 0, 0, mController->GetOwner(), &ori, &pos, false, TRUE, 0);
	pos = ori * part->mBoundingBox->mOrigin + pos;

	FVector	vel = mDirection;
	if (!mHasDirection)
	{
		vel = pos - mController->GetOwner()->GetPos();
		vel.Normalise();
		vel *= (float(GAME.Random() % 65536) * (1.0f / 65536.0f)) * 0.03f;
	}

	if (mController->GetOwner()->GetUnitData()->mExplosion)
		SpawnPickup(pos);

	if ((link.Full()) && (link.mEffect->mParticle))
	{
		CParticleMesh	*mp = link.mEffect->mParticle;
		link.SetPos(pos);
		link.SetOrientation(ori);
		float	a = float(GAME.Random() % 65536) * (1.0f / 65536.0f);
		float	b = float(GAME.Random() % 65536) * (1.0f / 65536.0f);
		float	c = float(GAME.Random() % 65536) * (1.0f / 65536.0f);
		FVector spin(a * 0.04f, b * 0.04f, c * 0.04f);
		PublishMeshMotion(mp, spin);
		mp->mVelocity = vel;
		mp->mMesh = mController->GetOwner()->GetRenderThing()->GetRTMesh();
		mp->mUnknown7E = (SWORD)mPart;
		mp->mRadius = part->mBoundingBox->mRadius;
	}
}

//******************************************************************************************
void CDestroyableSegmentComponent::Break()	// FUNCTION: 0x00443ea0
{
	if (mDestroyed == TRUE)
		return;

	mDestroyed = TRUE;
	mHealth = 0;
	if (mComponent.ToRead())
		mComponent->StartDieProcess();
}
