// InfluenceMap.cpp: reconstructed from InfluenceMap.obj (0048aec0..0048c55f); Stuart's release has no copy. The
// influence map's nodes, their links and the residual threat units leave behind. Three events drive it: 1000
// rebuilds the threat sums, 1001 eases every node (every quarter second) and 1002 fades the residuals (every half
// second). The allocations' __LINE__ values (70, 116, 422) are placed with #line.
#include "common.h"
#include "InfluenceMap.h"
#include "InitThing.h"
#include "DXMemBuffer.h"
#include "eventmanager.h"
#include "console.h"
#include "world.h"
#include "unit.h"
#include "game.h"

// FUNCTION: 0x0048aec0 init:ZERO_FVECTOR
// FUNCTION: 0x0048aee0 init:ID_FMATRIX

CInfluenceMap	*INFLUENCEMAP;		// 0067a748, just before this object's ID_FMATRIX (0067a750)

// FUNCTION: 0x0048c300 ??1CInfluenceNode@@UAE@XZ

#define INFLUENCE_BUILD		1000
#define INFLUENCE_UPDATE	1001
#define INFLUENCE_DECAY		1002

//******************************************************************************************
// FUNCTION: 0x0048afb0
void	CInfluenceMap::Clear()
{
	CInfluenceNode	*n;
	while (n = mNodes.First())
	{
		mNodes.Remove(n);
		n->Shutdown();
	}
	CInfluenceResidual	*r;
	while (r = mResiduals.First())
	{
		mResiduals.Remove(r);
		delete r;
	}
}

//******************************************************************************************
// Matched against pristine 74154bfa…: all 1504 section bytes and 77 relocations. The fourth node word uses a
// float argument/storage model so all four values survive the constructor calls as retail snapshots. Coordinate
// body assignments also preserve the retail vtable/field store order. Its purpose and unique original source
// type remain unknown; the node word is not interpreted elsewhere in the reconstructed code.
// FUNCTION: 0x0048b010
void	CInfluenceMap::Load(CDXMemBuffer *file)
{
	CInfluenceNode	*n;
	SINT	count, links;
	Clear();

	CONSOLE.Status("Loading influence map");
	UWORD	version;
	file->Read(&version, 2);
	switch (version)
	{
	case 0:
		file->Read(&count, 4);
		for (; count; count--)
		{
			float	x, y, radius;
			file->Read(&x, 4);
			file->Read(&y, 4);
			file->Read(&radius, 4);
#line 70
			n = new(MEMTYPE_INFLUENCEMAP) CInfluenceNode(x, y, radius, 0);
			CInitThing	init;
			init.mPos.X = x;
			init.mPos.Y = y;
			init.mPos.Z = 0;
			n->Init(&init);
			mNodes.Append(n);
		}
		file->Read(&links, 4);
		for (; links; links--)
		{
			SINT	a, b;
			file->Read(&a, 4);
			file->Read(&b, 4);
			CInfluenceNode	*na = GetNode(a);
			CInfluenceNode	*nb = GetNode(b);
			na->AddLink(nb, 1);
			nb->AddLink(na, 1);
		}
		break;
	case 1:
		file->Read(&count, 4);
		for (; count; count--)
		{
			float	x, y, radius;
			float	weight;
			file->Read(&x, 4);
			file->Read(&y, 4);
			file->Read(&radius, 4);
			file->Read(&weight, 4);
#line 116
			n = new(MEMTYPE_INFLUENCEMAP) CInfluenceNode(x, y, radius, weight);
			CInitThing	init;
			init.mPos.X = x;
			init.mPos.Y = y;
			init.mPos.Z = 0;
			n->Init(&init);
			mNodes.Append(n);
		}
		file->Read(&links, 4);
		for (; links; links--)
		{
			SINT	a, b, weight;
			file->Read(&a, 4);
			file->Read(&b, 4);
			file->Read(&weight, 4);
			CInfluenceNode	*na = GetNode(a);
			CInfluenceNode	*nb = GetNode(b);
			na->AddLink(nb, weight);
			nb->AddLink(na, weight);
		}
		break;
	}

	Build(FALSE);
	Decay();
	CONSOLE.StatusDone("Loading influence map", TRUE);
}

//******************************************************************************************
// FUNCTION: 0x0048b620
void	CInfluenceNode::ResetInfluence()
{
	mThreat[0] = 0;
	mThreat[1] = 0;
	mThreatB[0] = 0;
	mThreatB[1] = 0;
	mTarget = 0;
	mDistance[0] = 99999;
	mDistance[1] = 99999;
}

//******************************************************************************************
// Reads past a map that is not wanted.
// FUNCTION: 0x0048b660
void	CInfluenceMap::Skip(CDXMemBuffer *file)
{
	UWORD	version;
	file->Read(&version, 2);
	switch (version)
	{
	case 0:
		{
			SINT	count;
			file->Read(&count, 4);
			for (; count; count--)
			{
				float	x, y, radius;
				file->Read(&x, 4);
				file->Read(&y, 4);
				file->Read(&radius, 4);
			}
			SINT	links;
			file->Read(&links, 4);
			for (; links; links--)
			{
				SINT	a, b;
				file->Read(&a, 4);
				file->Read(&b, 4);
			}
		}
		break;
	case 1:
		{
			SINT	count;
			file->Read(&count, 4);
			for (; count; count--)
			{
				float	x, y, radius;
				SINT	weight;
				file->Read(&x, 4);
				file->Read(&y, 4);
				file->Read(&radius, 4);
				file->Read(&weight, 4);
			}
			SINT	links;
			file->Read(&links, 4);
			for (; links; links--)
			{
				SINT	a, b, weight;
				file->Read(&a, 4);
				file->Read(&b, 4);
				file->Read(&weight, 4);
			}
		}
		break;
	}
}

//******************************************************************************************
// One pass of a node's distances across its links. Written apart so Update stays small enough for HandleEvent to
// expand it (retail inlines Update there and calls this walk's First and Next out of line).
static inline void SpreadDistances(CInfluenceNode *n)
{
	for (CInfluenceLink *l = n->mLinks.First(); l; l = n->mLinks.Next())
	{
		if (n->mDistance[1] > l->mNode->mDistance[1] + 1)
			n->mDistance[1] = l->mNode->mDistance[1] + 1;
		if (n->mDistance[0] > l->mNode->mDistance[0] + 1)
			n->mDistance[0] = l->mNode->mDistance[0] + 1;
	}
}

//******************************************************************************************
// Eases every node, then spreads each side's distance twenty times across the links.
// FUNCTION: 0x0048b7d0
void	CInfluenceMap::Update()
{
	CInfluenceNode	*n;
	for (n = mNodes.First(); n; n = mNodes.Next())
		n->CalculateInfluence(TRUE);

	for (SINT i = 20; i; i--)
	{
		for (n = mNodes.First(); n; n = mNodes.Next())
			SpreadDistances(n);
	}
	EVENT_MANAGER.AddEvent(INFLUENCE_UPDATE, this, EVENT_MANAGER.GetTime() + 0.25f);
}

//******************************************************************************************
// Event 1000: sums each side's threat on the nodes nearest its units and squads (ground units only) and its
// residuals, spreads the threat over ten passes into empty nodes from their links, marks the held nodes, spreads the
// distances twenty times, sets each node's influence, and comes round again in a second or two.
// Matches together with InfluenceMap.h's GetNearestNode declaring best_dist before best (the header half, proposed
// separately; without it the two initialisations swap, 40 bytes).
// STAND-IN (call-list fit, consumed values): retail's inline budget runs out sooner, calling the node reset
// loop's, both thing lists' and the ten spreading passes' link walks' First and Next out of line (0048b8ef, 0048b941,
// 0048bdbe). The file-local accessors below (real field reads) and the SumLinks helper give the body the extra sites
// that put every call where retail's is. Which real accesses were accessors is open.
// STAND-IN (spelling, no code): the bracket in `(ThreatOf(n, 0)) + ThreatBOf(n, 0)` is an x87 node that keeps
// mThreat[0] the first operand of the empty-node test (fld [edi+0x9c] at 0048bd90).
// Before these changes the direct-field main form c3b9d8f2 compared 1792 bytes with 1611 nonrelocated differences;
// this is the whole previous form, not an isolated accessor ablation. Reversing only the proposed header order
// removes the match. The original source spelling and helper ownership remain unknown. Replace the accessors,
// SumLinks boundary and grouping when a source-grounded form preserves the complete section and relocations.
// Each thing loop tests the component/naval/air types around the nearest-node block instead of with `continue`. Both
// forms emit the same code, but they give the latch block its predecessors in another order. The register allocator's
// split of the zero web (C2 0x107216f1, which groups blocks that share a free register) then puts each latch in a region
// of its own, so retail's `xor ebp,ebp` goes onto the loop-exit edges (0048bae1, 0048bc92) and both latches test the
// next thing with `test edi,edi`. With `continue` the latch joins the type test's region, and the zero is reloaded
// before its next use instead (after it2.First, and in the second latch).
static inline float ThreatOf(CInfluenceNode *n, SINT i) { return n->mThreat[i]; }
static inline float ThreatBOf(CInfluenceNode *n, SINT i) { return n->mThreatB[i]; }
static inline SINT DistanceOf(CInfluenceNode *n, SINT i) { return n->mDistance[i]; }
static inline CInfluenceNode *NodeOf(CInfluenceResidual *r) { return r->mNode; }
static inline float ResidualThreatOf(CInfluenceResidual *r) { return r->mThreat; }
static inline SINT AllegianceOf(CInfluenceResidual *r) { return r->mAllegiance; }

static inline void SumLinks(CInfluenceNode *n, float &sum, float &sumB)
{
	for (CInfluenceLink *l = n->mLinks.First(); l; l = n->mLinks.Next())
	{
		sum += ThreatOf(l->mNode, 0);
		sumB += ThreatBOf(l->mNode, 0);
	}
}

// FUNCTION: 0x0048b8e0
void	CInfluenceMap::Build(BOOL a)
{
	CInfluenceNode	*n;
	for (n = mNodes.First(); n; n = mNodes.Next())
		n->ResetInfluence();

	ListIterator<CThing>	it(&WORLD.mSet20);
	CThing	*thing;
	for (thing = it.First(); thing; thing = it.Next())
	{
		if (thing->IsDying())
			continue;
		if (thing->GetThingType() & THING_TYPE_UNIT)
		{
			if (!(thing->GetThingType() & (THING_TYPE_COMPONENT | THING_TYPE_NAVAL | THING_TYPE_AIR_UNIT)))
			{
				n = GetNearestNode(thing->GetPos().X, thing->GetPos().Y);
				if (n)
				{
					n->mThreat[0] += ((CUnit *)thing)->GetThreat();
					n->mThreat[1] += ((CUnit *)thing)->GetImportance();
				}
			}
		}
		else if (thing->GetThingType() & THING_TYPE_UNKNOWN_20000000)
		{
			n = GetNearestNode(thing->GetPos().X, thing->GetPos().Y);
			if (n)
			{
				n->mThreat[0] += ((CUnit *)thing)->GetThreat();
				n->mThreat[1] += ((CUnit *)thing)->GetImportance();
			}
		}
	}

	ListIterator<CThing>	it2(&WORLD.mSet30);
	for (thing = it2.First(); thing; thing = it2.Next())
	{
		if (thing->IsDying())
			continue;
		if (thing->GetThingType() & THING_TYPE_UNIT)
		{
			if (!(thing->GetThingType() & (THING_TYPE_COMPONENT | THING_TYPE_NAVAL | THING_TYPE_AIR_UNIT)))
			{
				n = GetNearestNode(thing->GetPos().X, thing->GetPos().Y);
				if (n)
				{
					n->mThreatB[0] += ((CUnit *)thing)->GetThreat();
					n->mThreatB[1] += ((CUnit *)thing)->GetImportance();
				}
			}
		}
		else if (thing->GetThingType() & THING_TYPE_UNKNOWN_20000000)
		{
			n = GetNearestNode(thing->GetPos().X, thing->GetPos().Y);
			if (n)
			{
				n->mThreatB[0] += ((CUnit *)thing)->GetThreat();
				n->mThreatB[1] += ((CUnit *)thing)->GetImportance();
			}
		}
	}

	ListIterator<CInfluenceResidual>	rit(&mResiduals);
	for (CInfluenceResidual *r = rit.First(); r; r = rit.Next())
	{
		if (!NodeOf(r))
			continue;
		if (AllegianceOf(r) == 0)
		{
			r->mNode->mThreat[0] += ResidualThreatOf(r);
			r->mNode->mThreat[1] += ResidualThreatOf(r);
		}
		else if (AllegianceOf(r) == 1)
		{
			r->mNode->mThreatB[0] += ResidualThreatOf(r);
			r->mNode->mThreatB[1] += ResidualThreatOf(r);
		}
	}

	for (n = mNodes.First(); n; n = mNodes.Next())
	{
		if (ThreatOf(n, 0) != 0)
			n->mDistance[1] = 0;
		if (ThreatBOf(n, 0) != 0)
			n->mDistance[0] = 0;
	}

	SINT	i;
	for (i = 10; i; i--)
	{
		for (n = mNodes.First(); n; n = mNodes.Next())
		{
			if ((ThreatOf(n, 0)) + ThreatBOf(n, 0) == 0)
			{
				float	sum = 0, sumB = 0;
				SumLinks(n, sum, sumB);
				n->mThreat[0] = sum * 0.1f;
				n->mThreatB[0] = sumB * 0.1f;
			}
		}
	}

	for (i = 20; i; i--)
	{
		for (n = mNodes.First(); n; n = mNodes.Next())
		{
			for (CInfluenceLink *l = n->mLinks.First(); l; l = n->mLinks.Next())
			{
				if (DistanceOf(n, 1) > DistanceOf(l->mNode, 1) + 1)
					n->mDistance[1] = DistanceOf(l->mNode, 1) + 1;
				if (DistanceOf(n, 0) > DistanceOf(l->mNode, 0) + 1)
					n->mDistance[0] = DistanceOf(l->mNode, 0) + 1;
			}
		}
	}

	for (n = mNodes.First(); n; n = mNodes.Next())
		n->CalculateInfluence(a);

	EVENT_MANAGER.AddEvent(INFLUENCE_BUILD, this, GAME.FloatRandom() + EVENT_MANAGER.GetTime() + 1.0f);
}

//******************************************************************************************
// The residuals fade; one that is gone ends this pass (retail).
// FUNCTION: 0x0048bf70
void	CInfluenceMap::Decay()
{
	ListIterator<CInfluenceResidual>	it(&mResiduals);
	for (CInfluenceResidual *r = it.First(); r; r = it.Next())
	{
		r->mThreat -= 0.2f;
		if (r->mThreat <= 0.f)
		{
			mResiduals.Remove(r);
			delete r;
			break;
		}
	}
	EVENT_MANAGER.AddEvent(INFLUENCE_DECAY, this, EVENT_MANAGER.GetTime() + 0.5f);
}

//******************************************************************************************
// FUNCTION: 0x0048c000
void	CInfluenceMap::AddResidual(float x, float y, float threat, SINT allegiance)
{
#line 422
	mResiduals.Add(new(MEMTYPE_INFLUENCEMAP) CInfluenceResidual(x, y, threat, allegiance));
}

//******************************************************************************************
// FUNCTION: 0x0048c120
void	CInfluenceMap::HandleEvent(CEvent *event)
{
	switch (event->GetEventNum())
	{
	case INFLUENCE_BUILD:
		Build(TRUE);
		break;
	case INFLUENCE_UPDATE:
		Update();
		break;
	case INFLUENCE_DECAY:
		Decay();
		break;
	}
}

// FUNCTION: 0x0048c2d0
BOOL	CInfluenceMap::IsEmpty()
{
	return mNodes.Size() <= 0;
}

//******************************************************************************************
// FUNCTION: 0x0048c350
void	CInfluenceNode::Shutdown()
{
	CInfluenceLink	*l;
	while (l = mLinks.First())
	{
		mLinks.Remove(l);
		delete l;
	}
	SUPERTYPE::Shutdown();
}

// FUNCTION: 0x0048c390
void	CInfluenceNode::Init(CInitThing *init)
{
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	mFlags &= ~TF_IN_MAP_WHO;
	SUPERTYPE::Init(init);
}

//******************************************************************************************
// The target is the balance of the two sides' threat here, or, with none, a lean towards the side with more
// around; the influence moves 0.05 towards it per step when easing.
// FUNCTION: 0x0048c3b0
void	CInfluenceNode::CalculateInfluence(BOOL ease)
{
	float	total = mThreat[0] + mThreatB[0];
	if (total > 0.f)
	{
		mTarget = (mThreat[0] - mThreatB[0]) / total;
		if (mInfluence < 0.f)
			mSide = 1;
		else if (mInfluence > 0.f)
			mSide = 2;
		else
			mSide = 0;
	}
	else
	{
		mTarget = 0;
		float	a = 0.f, b = 0.f;
		for (CInfluenceLink *l = mLinks.First(); l; l = mLinks.Next())
		{
			a += (l->mNode->mThreat[0]);
			b += l->mNode->mThreatB[0];
		}
		if (!(a < b))
			mSide = 2;
		else
			mSide = 1;
	}

	if (mTarget == 0.f)
	{
		switch (mSide)
		{
		case 1:
			mTarget = -1.f;
			break;
		case 2:
			mTarget = 1.f;
			break;
		}
	}

	if (ease)
	{
		if (mInfluence + 0.05f < mTarget)
			mInfluence += 0.05f;
		else if (mInfluence - 0.05f > mTarget)
			mInfluence -= 0.05f;
		else
			mInfluence = mTarget;
	}
	else
		mInfluence = mTarget;
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x0048c2e0 ??_GCInfluenceNode@@UAEPAXI@Z
