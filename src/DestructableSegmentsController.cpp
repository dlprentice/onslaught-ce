// DestructableSegmentsController.cpp: reconstructed from DestructableSegmentsController.obj (00443ed0..0044515f);
// Stuart's release has no copy. The controller of a segmented unit: one CDestroyableSegment per mesh part, built from
// the mesh's part tree ("core"/"CORE" parts are cores, parts with damage levels swap, "x1"/"X1" parts use their mesh,
// the rest are extras) and the unit's attached components. The allocations' __LINE__s are the retail ones.

#include "common.h"
#include "monitor.h"
#include "DestructableSegmentsController.h"
#include "DestroyableSegment.h"
#include "unit.h"
#include "component.h"
#include "mesh.h"
#include "RenderThing.h"
#include "WorldPhysicsManager.h"
#include "game.h"
#include "DebugLog.h"

// FUNCTION: 0x00443ed0 init:ZERO_FVECTOR
// FUNCTION: 0x00443ef0 init:ID_FMATRIX

static SINT	sDepth = 0;											// 00672768: ProcessNode's recursion depth

//******************************************************************************************
CDestructableSegmentsController::CDestructableSegmentsController(CUnit *owner, float a, BOOL b, SINT c)	// FUNCTION: 0x00443fc0
{
	mOwner = owner;
	mUnknown14 = a;
	mUnknown24 = b;
	mUnknown28 = c;
	mCore = NULL;
	mSegments = NULL;
	mUnknown2C = 0;
	mNumSegments = 0;
	mTotalSize = 0;
	mUnknown20 = 0;
	mTotalHealth = 0;
	mChanged = TRUE;
}

//******************************************************************************************
CDestructableSegmentsController::~CDestructableSegmentsController()	// FUNCTION: 0x00444000
{
	delete [] mSegments;
	delete mCore;
}

//******************************************************************************************
void CDestructableSegmentsController::Damage(SINT part, float amount, CThing *by)	// FUNCTION: 0x00444030
{
	if (part != -1)
	{
		CDestroyableSegment	*segment = mSegments[part];
		if (segment)
		{
			segment->Damage(amount, by);
			if (mCore->AreChildrenDestroyed() == TRUE || mTotalHealth * 0.3 > mCore->GetActiveHealth())
				mOwner->StartDieProcess();
		}
		else
		{
			if (!mOwner->GetRenderThing())
				return;
			CMesh	*mesh = mOwner->GetRenderThing()->GetRTMesh();
			if (!mesh)
				return;
			LOG.AddMessage("WARNING: '%s', building part not found!!!!!! part = %d", mesh->GetName(), part);
		}
	}

	if (mUnknown2C != TRUE)
	{
		if (GetLife() < GetLifeUnknown4443b0() * 0.5f)
			mUnknown2C = TRUE;
	}
}

//******************************************************************************************
void CDestructableSegmentsController::DamageRandomly()	// FUNCTION: 0x00444160
{
	SPtrSet<CDestroyableSegment>	segments;
	SINT	n;
	for (n = 0; n < mNumSegments; n++)
	{
		CDestroyableSegment	*segment = (n == -1) ? NULL : mSegments[n];
		if (segment && !segments.Contains(segment))
			segments.Append(segment);
	}

	for (CDestroyableSegment *segment = segments.First(); segment; segment = segments.Next())
	{
		if (segment->GetType() > DST_SWAP && GAME.Random() % 2 == 0)
			segment->Damage(100000.0f, mOwner);
	}

	if (mUnknown2C != TRUE)
	{
		if (GetLife() < GetLifeUnknown4443b0() * 0.5f)
			mUnknown2C = TRUE;
	}
}

//******************************************************************************************
float CDestructableSegmentsController::GetLastDamageTime(SINT part)	// FUNCTION: 0x004442d0
{
	if (part != -1 && mSegments[part])
		return mSegments[part]->mLastDamageTime;
	return -1.0f;
}

//******************************************************************************************
float CDestructableSegmentsController::GetLastDamageAmount(SINT part)	// FUNCTION: 0x00444300
{
	if (part != -1 && mSegments[part])
		return mSegments[part]->mLastDamageAmount;
	return 0.0f;
}

//******************************************************************************************
float CDestructableSegmentsController::GetLife()	// FUNCTION: 0x00444330
{
	BOOL	none = TRUE;
	for (SINT n = 0; n < mNumSegments; n++)
	{
		if (mSegments[n] && mSegments[n]->mVulnerable == TRUE)
			none = FALSE;
	}
	if (none == TRUE)
		return 1.0f;
	return mCore->GetActiveHealth();
}

//******************************************************************************************
float CDestructableSegmentsController::GetLifeUnknown444370()	// FUNCTION: 0x00444370
{
	BOOL	none = TRUE;
	for (SINT n = 0; n < mNumSegments; n++)
	{
		if (mSegments[n] && mSegments[n]->mVulnerable == TRUE)
			none = FALSE;
	}
	if (none == TRUE)
		return 1.0f;
	return mCore->GetTotalHealth();
}

//******************************************************************************************
float CDestructableSegmentsController::GetLifeUnknown4443b0()	// FUNCTION: 0x004443b0
{
	BOOL	none = TRUE;
	for (SINT n = 0; n < mNumSegments; n++)
	{
		if (mSegments[n] && mSegments[n]->mVulnerable == TRUE)
			none = FALSE;
	}
	if (none == TRUE)
		return 1.0f;
	return mTotalHealth;
}

//******************************************************************************************
void CDestructableSegmentsController::Unknown4443f0()	// FUNCTION: 0x004443f0
{
	if (mCore->AreChildrenDestroyed() == TRUE || mTotalHealth * 0.3 > mCore->GetActiveHealth())
		return;

	mCore->ActivateAll();
	mCore->DamageChildren(100000.0f);
	mUnknown2C = TRUE;
}

//******************************************************************************************
void CDestructableSegmentsController::SetSegmentHealth(char *name, float health)	// FUNCTION: 0x00444450
{
	if (!mOwner->GetRenderThing())
		return;
	CMesh	*mesh = mOwner->GetRenderThing()->GetRTMesh();
	if (!mesh)
		return;

	CMeshPart	*part = mesh->GetMeshSubpart(name);
	if (part)
	{
		CDestroyableSegment	*segment = mSegments[part->mPartNumber];
		if (segment)
		{
			segment->mHealth = health;
			return;
		}
	}
	LOG.AddMessage("FATAL ERROR: Could not find segment for '%s", name);
}

//******************************************************************************************
void CDestructableSegmentsController::ResetSegmentHealth(char *name, float health)	// FUNCTION: 0x004444b0
{
	if (!mOwner->GetRenderThing())
		return;
	CMesh	*mesh = mOwner->GetRenderThing()->GetRTMesh();
	if (!mesh)
		return;

	CMeshPart	*part = mesh->GetMeshSubpart(name);
	if (part)
	{
		CDestroyableSegment	*segment = mSegments[part->mPartNumber];
		if (segment)
		{
			segment->mHealth = health;
			segment->mMaxHealth = health;
			if (mCore)
				mTotalHealth = mCore->GetActiveHealth();
			return;
		}
	}
	LOG.AddMessage("FATAL ERROR: Could not find segment for '%s", name);
}

//******************************************************************************************
CDestroyableSegment *CDestructableSegmentsController::FindSegment(char *name)	// FUNCTION: 0x00444520
{
	if (!mOwner->GetRenderThing())
		return NULL;
	CMesh	*mesh = mOwner->GetRenderThing()->GetRTMesh();
	if (!mesh)
		return NULL;

	CMeshPart	*part = mesh->GetMeshSubpart(name);
	if (part)
		return mSegments[part->mPartNumber];

	LOG.AddMessage("FATAL ERROR: Could not find segment for '%s", name);
	return NULL;
}

//******************************************************************************************
void CDestructableSegmentsController::SetAllSegmentsHealth(float health)	// FUNCTION: 0x00444580
{
	for (SINT n = 0; n < mNumSegments; n++)
	{
		if (mSegments[n])
			mSegments[n]->mHealth = health;
	}
}

//******************************************************************************************
void CDestructableSegmentsController::SetSegmentVulnerable(char *name, BOOL vulnerable)	// FUNCTION: 0x004445b0
{
	if (!mOwner->GetRenderThing())
		return;
	CMesh	*mesh = mOwner->GetRenderThing()->GetRTMesh();
	if (!mesh)
		return;

	CMeshPart	*part = mesh->GetMeshSubpart(name);
	if (part)
	{
		CDestroyableSegment	*segment = mSegments[part->mPartNumber];
		if (segment)
		{
			segment->mVulnerable = vulnerable;
			if (mCore)
				mTotalHealth = mCore->GetActiveHealth();
			return;
		}
	}
	LOG.AddMessage("FATAL ERROR: Could not find segment for '%s", name);
}

//******************************************************************************************
void CDestructableSegmentsController::SetAllSegmentsVulnerable(BOOL vulnerable)	// FUNCTION: 0x00444620
{
	for (SINT n = 0; n < mNumSegments; n++)
	{
		if (mSegments[n])
			mSegments[n]->mVulnerable = vulnerable;
	}
	if (mCore)
		mTotalHealth = mCore->GetActiveHealth();
}

//******************************************************************************************
void CDestructableSegmentsController::Init()	// FUNCTION: 0x00444660
{
	if (!mOwner->GetRenderThing())
	{
		LOG.AddMessage("Warning: No mesh for building");
		return;
	}

	CMesh	*mesh = mOwner->GetRenderThing()->GetRTMesh();
	sDepth = 0;
#line 388
	mSegments = new(MT_DESTROYABLESEGMENT) CDestroyableSegment *[mesh->mNumParts + 1];
	SINT	partCount = mesh->mNumParts;
	mNumSegments = partCount;
	SINT	n;
	for (n = 0; n <= mesh->mNumParts; n++)
		mSegments[n] = NULL;

	ProcessNode(mesh->mParts[0], NULL);

	if (mUnknown20 == 1)
		LOG.AddMessage("Warning: '%s' only has Primary core.  Needs at least one child core segment", mesh->GetName());

	// the components attached to the unit
	for (SINT c = 1; mOwner->FindChild(c); c++)
	{
		CComponent	*component = (CComponent *)mOwner->FindChild(c)->ToRead();
		if (component)
		{
			SINT	emitterNumber = component->mNumber;
			CMeshPart	*part = mesh->GetEmitterSubpart("component", emitterNumber);
			if (part)
			{
				CDestroyableSegment	*parent = mSegments[part->mParent->mPartNumber];
				if (parent)
				{
#line 424
					parent->RegisterChild(new(MT_DESTROYABLESEGMENT) CDestroyableSegmentComponent(this, parent, component));
				}
				else
					LOG.AddMessage("Warning: '%s' Can't find segment component is attached to ( num = %d )", mesh->GetName(), part->mPartNumber);
			}
			else
			{
				SINT	number = component->mNumber;
				LOG.AddMessage("Warning: '%s' Can't find mesh part component os attached to ( emitter num = %d )", mesh->GetName(), number);
			}
		}
	}

	for (n = 0; n <= mesh->mNumParts; n++)
	{
		if (mSegments[n])
		{
			float	life = 100.0f;
			if (!mOwner->mUnitData)
				LOG.AddMessage("ERROR: no behavour for unit");
			else
				life = mOwner->mUnitData->mUnknownC0;
			mSegments[n]->SetLife(life, mTotalSize);
		}
	}

	if (mCore)
		mTotalHealth = mCore->GetTotalHealth();
}

// FUNCTION: 0x00444940 ??_GCDestroyableSegmentComponent@@UAEPAXI@Z
// FUNCTION: 0x00444960 ??1CDestroyableSegmentComponent@@UAE@XZ

//******************************************************************************************
CDestroyableSegment *CDestructableSegmentsController::CreateSegment(SINT type, CMeshPart *part, CDestroyableSegment *parent, float size)	// FUNCTION: 0x004449c0
{
	switch (type)
	{
	case DST_CORE:
		mUnknown20++;
#line 483
		return new(MT_DESTROYABLESEGMENT) CDestroyableCoreSegment(this, part->mPartNumber, parent, size, mUnknown20);
	case DST_SWAP:
#line 488
		return new(MT_DESTROYABLESEGMENT) CDestroyableSwapSegment(this, part->mPartNumber, parent, size, part->mNumChainedParts);
	case DST_EXTRA:
#line 493
		return new(MT_DESTROYABLESEGMENT) CDestroyableExtraSegment(this, part->mPartNumber, parent, size);
	case DST_EXTRA_USE_MESH:
#line 498
		return new(MT_DESTROYABLESEGMENT) CDestroyableExtraUseMeshSegment(this, part->mPartNumber, parent, size);
	default:
		LOG.AddMessage("WARNING: unknown Destroyable segment type");
	}
	return NULL;
}

// FUNCTION: 0x00444be0 ??_GCDestroyableSwapSegment@@UAEPAXI@Z
// FUNCTION: 0x00444c00 ??1CDestroyableSwapSegment@@UAE@XZ

//******************************************************************************************
void CDestructableSegmentsController::ProcessNode(CMeshPart *part, CDestroyableSegment *parent)	// FUNCTION: 0x00444c10
{
	sDepth++;

	CDestroyableSegment	*segment = parent;
	CMeshPart	*collision = part->GetMeshDataPart();
	if (part->GetMeshDataPart() && part->GetMeshDataPart()->mPartType == 1 && !part->mIsChained)
	{
		CBoundingBox	*box = collision->mBoundingBox;
		float	size = box->mAxes.X;
		if (box->mAxes.Y > size)
			size = box->mAxes.Y;
		if (box->mAxes.Z > size)
			size = box->mAxes.Z;

		SINT	type;
		if (!parent)
		{
			if (!strncmp(part->mName, "core", 4) || !strncmp(part->mName, "CORE", 4))
				type = DST_CORE;
			else if (part->mNumChainedParts > 0)
				type = DST_SWAP;
			else if (!strncmp(part->mName, "x1", 2) || !strncmp(part->mName, "X1", 2))
				type = DST_EXTRA_USE_MESH;
			else
				type = DST_EXTRA;

			segment = CreateSegment(type, part, NULL, size);
			if (segment->GetType() == DST_CORE)
			{
				if (mUnknown20 > 1)
				{
					LOG.AddMessage("WARNING '%s' found second start core part '%s' ", part->mBaseMesh->GetName(), part->mName);
					return;
				}
				mCore = (CDestroyableCoreSegment *)segment;
			}
			else
			{
				if (mUnknown20 > 1)
					LOG.AddMessage("Woops: '%s' Looks like you forgot to link '%s' to core", part->mBaseMesh->GetName(), part->mName);
				return;
			}
		}
		else
		{
			mTotalSize += size;
			if (!strncmp(part->mName, "core", 4) || !strncmp(part->mName, "CORE", 4))
				type = DST_CORE;
			else if (part->mNumChainedParts > 0)
				type = DST_SWAP;
			else if (!strncmp(part->mName, "x1", 2) || !strncmp(part->mName, "X1", 2))
				type = DST_EXTRA_USE_MESH;
			else
				type = DST_EXTRA;

			if (type != DST_CORE && parent->GetType() == DST_CORE && ((CDestroyableCoreSegment *)parent)->mNumber == 1)
				LOG.AddMessage("Warning: '%s' Child of CORE1 was not a CORE part (name '%s')", part->mBaseMesh->GetName(), part->mName);
			segment = CreateSegment(type, part, parent, size);
			parent->RegisterChild(segment);
		}

		mSegments[part->mPartNumber] = segment;
		if (part->mNumChainedParts > 0)
		{
			for (CMeshPart *level = part->mNextMeshPartInChain; level; level = level->mNextMeshPartInChain)
				mSegments[level->mPartNumber] = segment;
		}
	}

	for (SINT n = 0; n < part->mNumChildren; n++)
		ProcessNode(part->mChildren[n], segment);

	sDepth--;
}

//******************************************************************************************
SINT CDestructableSegmentsController::GetDamageStage(SINT part)	// FUNCTION: 0x00444f00
{
	CDestroyableSegment	*segment = mSegments[part];
	if (!segment)
		return 0;
	return segment->GetDamageStage();
}

//******************************************************************************************
BOOL CDestructableSegmentsController::CanUse(SINT part)	// FUNCTION: 0x00444f20
{
	CDestroyableSegment	*segment = mSegments[part];
	if (!segment)
	{
		if (!mOwner->GetRenderThing())
			return TRUE;
		CMesh	*mesh = mOwner->GetRenderThing()->GetRTMesh();
		if (!mesh)
			return TRUE;
		CMeshPart	*p = mesh->mParts[part];
		if (p && p->mPartType == 5 && p->mParent)
		{
			segment = mSegments[p->mParent->mPartNumber];
			if (segment)
				return !segment->mDestroyed;
		}
		return TRUE;
	}

	if (!mUnknown24 && segment->GetType() == DST_CORE && ((CDestroyableCoreSegment *)segment)->mNumber == 1)
	{
		if (mCore->AreChildrenDestroyed() == TRUE || mTotalHealth * 0.3 > mCore->GetActiveHealth())
			return FALSE;
	}
	return !segment->mDestroyed;
}

//******************************************************************************************
float CDestructableSegmentsController::GetCoreFall(SINT part)	// FUNCTION: 0x00445010
{
	if (mUnknown20 > 0)
	{
		CDestroyableSegment	*segment = mSegments[part];
		if (!segment)
		{
			if (part && mOwner->GetRenderThing())
			{
				CMesh	*mesh = mOwner->GetRenderThing()->GetRTMesh();
				if (mesh)
				{
					CMeshPart	*p = mesh->GetPart(part);
					if (!p)
						return 0.0f;
				}
			}
		}
		else if (segment->GetType() == DST_CORE)
			return ((CDestroyableCoreSegment *)segment)->mTotalTime;
	}
	return 0.0f;
}

//******************************************************************************************
void CDestructableSegmentsController::GetCentre(FVector *out)	// FUNCTION: 0x00445070
{
	CRenderThing	*rt = mOwner->GetRenderThing();
	CMesh	*mesh = rt->GetRTMesh();
	SINT	best = 0;
	CMeshPart	*bestpart = NULL;
	for (SINT n = 0; n < mesh->mNumParts; n++)
	{
		CMeshPart	*part = mesh->mParts[n];
		if (part)
		{
			CDestroyableSegment	*segment = mSegments[part->mPartNumber];
			if (segment && segment->GetType() == DST_CORE && segment->mHealth > 0 && ((CDestroyableCoreSegment *)segment)->mNumber > best)
			{
				best = ((CDestroyableCoreSegment *)segment)->mNumber;
				bestpart = part;
			}
		}
	}

	if (bestpart)
	{
		FMatrix	ori;
		CMeshPart::EvaluatePose(rt, mOwner, bestpart, out, &ori, FALSE, FALSE);
	}
	else
		mOwner->GetCentrePos(*out);
}
