// InfluenceMap.h: CInfluenceMap (InfluenceMap.obj, 0048aec0..0048c55f), the map units record threat on. Stuart's
// release has no InfluenceMap.h; his BattleEngine.cpp calls INFLUENCEMAP->AddResidual (CBattleEngine::Morph
// 0040a731 loads the pointer from 0067a748 and calls 0048c000). The map is a graph of nodes loaded with the world;
// each node sums the threat near it by side and eases its influence towards the balance. The link allocation below
// is on this header's line 115 (0x73, CInfluenceMap::Load 0048b2e0).

#ifndef INFLUENCEMAP_H
#define INFLUENCEMAP_H

#include "Monitor.h"
#include "SPtrSet.h"
#include "thing.h"

class CDXMemBuffer;
class CInfluenceNode;

// A link to a neighbouring node (8 bytes).
struct CInfluenceLink
{
	CInfluenceLink(CInfluenceNode *node, SINT weight) : mNode(node), mWeight(weight) {}

	CInfluenceNode	*mNode;
	SINT			mWeight;
};

// A node of the map (vtable 005dc050 with IRenderableThing at +8, 005dbfd8; 0xc4 bytes). Its class id is 30.
class CInfluenceNode : public CComplexThing
{
public:
	typedef CComplexThing SUPERTYPE;
	virtual char	*_GetClassName() { return "CInfluenceNode"; }	// FUNCTION: 0x0048b5f0
	virtual int		_GetClassID() { return 30; }					// FUNCTION: 0x0048b600

	CInfluenceNode(float x, float y, float radius, float weight)
	{
		mX = x;
		mY = y;
		mRadius = radius;
		mWeight = weight;
		mSide = 0;
		mInfluence = 0;
		ResetInfluence();
	}
	// the destructor is the compiler's (no vtable stores, 0048c300)

	virtual void	Shutdown();										// 0048c350
	virtual void	Init(CInitThing *init);							// 0048c390
	virtual float	GetRadius() { return mRadius; }					// FUNCTION: 0x0048b610
	void			ResetInfluence();								// 0048b620
	void			CalculateInfluence(BOOL ease);					// 0048c3b0
	void			AddLink(CInfluenceNode *node, SINT weight)
	{
#line 115
		mLinks.Add(new(MEMTYPE_INFLUENCEMAP) CInfluenceLink(node, weight));
	}

	SPtrSet<CInfluenceLink>	mLinks;					// +0x7c
	float			GetX() { return mX; }			// AddResidual's nearest-node search reads them first (0048c070)
	float			GetY() { return mY; }
	float			mX;								// +0x8c
	float			mY;								// +0x90
	float			mRadius;						// +0x94
	float			mWeight;						// +0x98: fourth serialized node word; purpose unknown
	float			mThreat[2];						// +0x9c: by side (ResetInfluence zeroes four)
	float			mThreatB[2];					// +0xa4
	SINT			mDistance[2];					// +0xac: hops to the nearest node of each side, 99999 for none
	float			mInfluence;						// +0xb4: eased towards mTarget
	float			mTarget;						// +0xb8: -1 .. 1
	SINT			mSide;							// +0xbc: 0, or 1 or 2 when a side holds it (IScript::GetRatioBattleLineNodes 005353d0)
	SINT			mUnknownC0;						// +0xc0
};

// Threat left behind at a place, fading (0xc bytes; AddResidual 0048c000).
struct CInfluenceResidual
{
	CInfluenceResidual(float x, float y, float threat, SINT allegiance);

	CInfluenceNode	*mNode;							// +0x00: the nearest node
	float			mThreat;						// +0x04
	SINT			mAllegiance;					// +0x08
};

class CInfluenceMap : public CMonitor		// vtable 005dfcb4 (HandleEvent 0048c120), 0x28 bytes
{
public:
	virtual void	HandleEvent(CEvent *event);
	~CInfluenceMap() { Clear(); }			// out-of-line copy 0050b950 (world.cpp)
	void			Clear();				// 0048afb0
	void			Load(CDXMemBuffer *file);		// 0048b010 (CWorld::LoadWorld 0050cf38)
	void			Skip(CDXMemBuffer *file);		// 0048b660 (CWorld::LoadWorld 0050cf23)
	void			Update();						// 0048b7d0: event 1001
	void			Build(BOOL a);					// 0048b8e0: event 1000 (CWorld::LoadWorld 0050d43e)
	void			Decay();						// 0048bf70: event 1002
	void			AddResidual(float x, float y, float threat, SINT allegiance);	// 0048c000
	BOOL			IsEmpty();						// 0048c2d0 (CHud::RenderBattleline 00487ff5)
	CInfluenceNode	*GetNode(SINT index)
	{
		SINT	i = 0;
		for (CInfluenceNode *n = mNodes.First(); n; n = mNodes.Next())
		{
			if (i == index)
				return n;
			i++;
		}
		return NULL;
	}
	CInfluenceNode	*GetNearestNode(float x, float y)
	{
		float			best_dist = 999999.f;
		CInfluenceNode	*best = NULL;
		for (CInfluenceNode *n = mNodes.First(); n; n = mNodes.Next())
		{
			float	dx = float(fabs(x - n->GetX()));
			float	dy = float(fabs(y - n->GetY()));
			float	d = dx + dy;
			d = d - n->GetRadius();
			if (d < 0.f)
				return n;
			if (d < best_dist)
			{
				best_dist = d;
				best = n;
			}
		}
		return best;
	}

	SPtrSet<CInfluenceNode>		mNodes;		// +0x08
	SPtrSet<CInfluenceResidual>	mResiduals;	// +0x18
};

extern CInfluenceMap	*INFLUENCEMAP;		// 0067a748

inline CInfluenceResidual::CInfluenceResidual(float x, float y, float threat, SINT allegiance)
{
	mNode = INFLUENCEMAP->GetNearestNode(x, y);
	mThreat = threat;
	mAllegiance = allegiance;
}

#endif
