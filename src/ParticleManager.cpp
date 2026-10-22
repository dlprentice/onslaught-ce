// ParticleManager.cpp: reconstructed from ParticleManager.obj (004cad60..004cbec4); Stuart's release has no copy. The
// names are ours unless noted; the allocations' __LINE__ values are the retail ones (#line: the layout is unknown).
#include "common.h"
#include "ParticleManager.h"	// FUNCTION: 0x004cad60 init:ZERO_FVECTOR
#include "mathsconst.h"			// FUNCTION: 0x004cad80 init:ID_FMATRIX
#include "ParticleDescriptor.h"
#include "game.h"
#include "engine.h"
#include "Camera.h"
#include "map.h"
#include "ltshell.h"

static SINT					sLoad = 0;			// 0082b3e0: the particles in use, in thousandths of the most there can be (these six initialized: retail's .bss order, placement evidence)
static CFoR					*sFoRs = NULL;			// 0082b3e4
static CParticleEffectLink	*sLinks = NULL;		// 0082b3e8
static UINT					sNumSystems = 0;	// 0082b3ec
static SINT					sNumAllocated = 0;	// 0082b3f0: this frame
static SINT					sAllocations = 0;	// 0082b3f4

//******************************************************************************************
void CParticle::Destroy()	// FUNCTION: 0x004cae50
{
	if (mData && (!mPD || mPD->Unknown14(this)))
		delete [] (char *)mData;

	if (mPD)
	{
		if (mPDPrev && mPDNext)
		{
			mPDPrev->mPDNext = mPDNext;
			mPDNext->mPDPrev = mPDPrev;
			mPDPrev = NULL;
			mPDNext = NULL;
			if (mPD->mNewestAtUpdate == this)
				mPD->mNewestAtUpdate = NULL;
		}
		else
			mPD->RemoveParticle(this);
	}
}

//******************************************************************************************
BOOL CParticle::AllocateData(SINT size)	// FUNCTION: 0x004caed0
{
	if (mData && (!mPD || mPD->Unknown14(this)))
		delete [] (char *)mData;

#line 194
	mData = new(MEMTYPE_PARTICLE) char[size];
	return mData != NULL;
}

//******************************************************************************************
void CParticleManager::ClearOwnerBacklinks()	// FUNCTION: 0x004caf30
{
	for (CFoR *f = sFoRs; f; f = f->mNext)
	{
		f->mInUse = FALSE;
		f->mParticle = NULL;
	}
}

//******************************************************************************************
// Advance each frame's stop state and free the frames no particle uses any more.
void CParticleManager::CleanupHandles()	// FUNCTION: 0x004caf60
{
	CFoR	*f;
	while (f = sFoRs)
	{
		if (f->mState == 1)
			f->mState = 2;
		else if (f->mState == 2)
			f->mState = 3;

		if (f->mInUse)
			break;
		sFoRs = f->mNext;
		delete f;
	}

	if (f)
	while (f->mNext)
	{
		CFoR	*n = f->mNext;
		if (n->mState == 1)
			n->mState = 2;
		else if (n->mState == 2)
			n->mState = 3;

		if (!f->mNext->mInUse)
		{
			n = f->mNext;
			f->mNext = n->mNext;
			delete n;
		}
		else
			f = f->mNext;
	}
}

//******************************************************************************************
void CParticleEffectLink::Link()	// FUNCTION: 0x004cb040
{
	mNext = sLinks;
	sLinks = this;
}

//******************************************************************************************
void CParticleEffectLink::Unlink()	// FUNCTION: 0x004cb050
{
	if (sLinks == this)
	{
		sLinks = mNext;
		return;
	}

	for (CParticleEffectLink *l = sLinks; l; l = l->mNext)
		if (l->mNext == this)
		{
			l->mNext = mNext;
			return;
		}
}

//******************************************************************************************
void CParticleManager::PruneDeadOwnerLinks()	// FUNCTION: 0x004cb080
{
	for (CParticleEffectLink *l = sLinks; l; l = l->mNext)
		if (l->mEffect && !l->mEffect->mInUse)
			l->mEffect = NULL;
}

//******************************************************************************************
void CParticleEffectLink::Stop(BOOL now)	// FUNCTION: 0x004cb0b0
{
	if (mEffect)
		mEffect->Stop(now);
	mEffect = NULL;
}

//******************************************************************************************
CParticleSystem::CParticleSystem()	// FUNCTION: 0x004cb0e0
{
	mParticles = NULL;
	mNext = NULL;
#line 422
	mParticles = new(MEMTYPE_PARTICLE) CParticle[512];
	for (SINT i = 0; i < 512; i++)
	{
		mParticles[i].mNext = PARTICLE_MANAGER.mFreeParticles;
		PARTICLE_MANAGER.mFreeParticles = &mParticles[i];
	}
	mNext = PARTICLE_MANAGER.mSystem;
	PARTICLE_MANAGER.mSystem = this;
	sNumSystems++;
}

//******************************************************************************************
CParticleSystem::~CParticleSystem()	// FUNCTION: 0x004cb1b0
{
	if (mParticles)
		delete [] mParticles;
	if (mNext)
		delete mNext;
	sNumSystems--;
}

//******************************************************************************************
BOOL CParticleManager::Process(float time, BOOL first)	// FUNCTION: 0x004cb210
{
	mFrame++;
	sNumAllocated = 0;

	if (time > 1.f)
		time = 1.f;
	else if (time < 0.f)
		time = 0.f;

	mTime += time;
	mFirstFrame = first;
	mFrameLength = time;

	ClearOwnerBacklinks();
	MoveParticles(mParticles);

	CParticleDescriptor	*pd = (CParticleDescriptor *)PARTICLE_SET.mFirstNode;
	CParticleDescriptor	*first_pd = pd;
	while (pd)
	{
		pd->mNextToUpdate = pd->mUnknown40;
		pd->mNewestAtUpdate = pd->mNewestParticle;
		pd = pd->mUnknown40;
	}
	for (pd = first_pd; pd; )
	{
		pd->Unknown22();
		CParticleDescriptor	*next = pd->mNextToUpdate;
		pd->mNewestAtUpdate = NULL;
		pd->mNextToUpdate = NULL;
		pd = next;
	}

	BOOL	alive = FreeDeadParticles();
	PruneDeadOwnerLinks();
	CleanupHandles();
	return alive;
}

//******************************************************************************************
// Where each effect is drawn this frame, between its last two positions.
void CParticleManager::DoInterpolation()	// FUNCTION: 0x004cb300
{
	for (CParticleEffectLink *l = sLinks; l; l = l->mNext)
	{
		CFoR	*f = l->mEffect;
		if (f)
		{
			if (f->mOldPos.Z != 10000.f)
				f->mStartPos = f->mOldPos + (f->mPos - f->mOldPos) * GAME.GetFrameRenderFraction();
			else
				f->mStartPos = f->mOldPos;
		}
	}
}

//******************************************************************************************
void CParticleManager::CreateEffect(CParticleDescriptor *pd, CParticleEffectLink *link, FVector pos, BOOL a, BOOL important)	// FUNCTION: 0x004cb3d0
{
	if (ENGINE.DebugNoParticles())
		return;

	CParticle	*p = AllocateParticle(pd, important);
	if (p)
	{
		p->mPos = pos;
		if (link)
		{
#line 704
			p->mFoR = new(MEMTYPE_PARTICLE) CFoR[1];
			link->mEffect = p->mFoR;
			if (p->mFoR)
			{
				p->mFoR->Link();
				p->mFoR->mInUse = TRUE;
				p->mFoR->mParticle = p;
				p->mFoR->mUnknownAC = a ? 0.f : -1.f;
				if (important)
					p->mFoR->mFlags |= 1;
			}
		}
		else
			p->mPD->Unknown15(p);
	}
	else if (link)
		link->mEffect = NULL;
}

//******************************************************************************************
void CFoR::Link()
{
	mNext = sFoRs;
	sFoRs = this;
}

//******************************************************************************************
CFoR::CFoR()
{
	mPos = ZERO_FVECTOR;
	mOldPos.Z = 10000.f;
	mUnknown90 = ZERO_FVECTOR;
	mOrientation = ID_FMATRIX;
	mHasOrientation = FALSE;
	mInUse = FALSE;
	mParticle = NULL;
	mUnknownAC = -1.f;
	mState = 0;
	mFlags = 0;
}

//******************************************************************************************
CParticle *CParticleManager::AllocateParticle(CParticleDescriptor *pd, BOOL important)	// FUNCTION: 0x004cb5c0
{
	if (!pd)
		return NULL;

	BOOL	rejected = FALSE;
	if (!mFreeParticles)
	{
		if (sNumSystems < (GAME.IsMultiplayer() ? 12 : 8))
		{
#line 738
			new(MEMTYPE_PARTICLE) CParticleSystem;
		}
		else if (important)
		{
			// STAND-IN (layout; review F 2026-10-15): the separate null test before the loop, and the `if (p)` after the
			// admitted particle's fields; the plain for and the unguarded tail each lose 004cb5c0.
			CParticle	*p;
			p = mParticles;
			if (!p)
				rejected = TRUE;
			else
			{
				// STAND-IN (layout): `goto reclaimed` leaves the loop with the particle just freed; a reclaimed flag with a break is
				// 409 bytes off, and searching first and freeing after the loop 592.
				for (; p; p = p->mNext)
					if (p->mPD)
						switch (p->mPD->GetType())
						{
						case PD_SIMPLE_SPRITE:
							if (!p->mFoR)
							{
								FreeParticle(p);
								goto reclaimed;
							}
							break;
						}
				reclaimed:
				if (!p)
					return NULL;
			}
		}
		else
			rejected = TRUE;

	}

	if (!rejected)
	{
		sLoad = ((mNumLiveParticles + sNumAllocated) * 1000) / ((GAME.IsMultiplayer() ? 12 : 8) * 512);
		sAllocations++;

		if (!important)
		{
			switch (pd->GetType())
			{
			case PD_SIMPLE_SPRITE:
			case PD_MESH:
				if (sLoad > 700)
					rejected = TRUE;
				else if ((sLoad > 400) && ((sAllocations % 300) < (sLoad - 400)))
					return NULL;
				break;
			case PD_TRAIL:
			case PD_PMESH:
				if (sLoad > 800)
					rejected = TRUE;
				else if ((sLoad > 600) && ((sAllocations % 200) < (sLoad - 600)))
					return NULL;
				break;
			case PD_EMITTER:
				// The final admission case shares the hard-limit failure block at 004cb7ce.
				if (sLoad > 900)
					rejected = TRUE;
				else if ((sLoad > 800) && ((sAllocations % 100) < (sLoad - 800)))
					rejected = TRUE;
				break;
			}
		}
	}
	if (rejected)
		return NULL;
	CParticle	*p = mFreeParticles;
	mFreeParticles = p->mNext;

	p->mPos = FVector(0.f, 0.f, 0.f);
	p->mVelocity = ZERO_FVECTOR;
	p->Orientation() = ID_FMATRIX;
	p->mFoR = NULL;
	p->mUnknown7E = 0;
	p->mUnknown80 = 0;
	p->mLife = 100.f;
	p->mRadius = 1.f;
	p->mFrame = 0;
	p->mUnknown7C = 0;
	p->mBirthTime = 0;
	p->mPD = NULL;
	p->mData = NULL;
	p->mMesh = NULL;
	p->mDead = FALSE;
	p->mPDNext = NULL;
	p->mPDPrev = NULL;

	if (p)
	{
		p->mNext = mParticles;
		mParticles = p;
		if (p->mNext)
			p->mNext->mPrev = p;
		p->mPrev = NULL;

		if (p->mPD)
			p->mPD->RemoveParticle(p);
		p->mPD = pd;
		pd->AddParticle(p);
		p->mBirthTime = PARTICLE_MANAGER.GetTime();
		pd->Unknown9(p);
	}

	sNumAllocated++;
	return p;
}

//******************************************************************************************
// Reconstructed update boundaries: the frame step owns motion and owner registration.
// Their nested expansion leaves the displacement constructor at 004cb96a out of line,
// rounding all three coordinates before addition as retail does.
static inline void AdvanceParticlePosition(CParticle *p)
{
	p->mPos += p->mVelocity * PARTICLE_MANAGER.GetFrameLength();
}

static inline void RegisterParticleOwner(CParticle *p)
{
	if (p->mFoR)
	{
		p->mFoR->mInUse = TRUE;
		p->mFoR->mParticle = p;
	}
}

static inline void AdvanceParticleFrame(CParticle *p)
{
	if (p->mLife > -1.f)
		p->mLife -= PARTICLE_MANAGER.GetFrameLength();
	AdvanceParticlePosition(p);
	RegisterParticleOwner(p);
}

void CParticleManager::ProcessParticle(CParticle *p)	// FUNCTION: 0x004cb920
{
	AdvanceParticleFrame(p);

	p->mDead |= (PARTICLE_MANAGER.IsFirstFrame() && (p->mLife > -2.f) && (p->mLife < 0.f));
	p->mPD->Unknown10(p);

	if (p->mDead)
		FreeParticle(p);
}

//******************************************************************************************
// Where a particle of that radius would rest on the ground, if it has reached it.
BOOL CParticleManager::OnGround(const FVector &pos, float radius, FVector &out)	// FUNCTION: 0x004cba30
{
	float	h = MAP.Collide(pos);
	if (pos.Z + radius > h)
	{
		out = pos;
		out.Z = h - radius;
		return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
float CParticleManager::GetDistanceSq(CParticle *p)	// FUNCTION: 0x004cba90
{
	if (!GAME.IsMultiplayer())
	{
		if (ENGINE.GetCamera())
		{
			FVector	cpos = ENGINE.GetCamera()->GetPos();
			FVector	pos = p->mPos;
			if (p->mFoR)
				pos += p->mFoR->mPos;
			return (cpos - pos).MagnitudeSq();
		}
		return 0.f;
	}

	float	best = 10000000.f;
	FVector	pos = p->mPos;
	if (p->mFoR)
		pos += p->mFoR->mPos;

	CCamera	*cam = GAME.GetCamera(0);
	if (cam)
	{
		FVector	cpos = cam->GetPos();
		float	d = (cpos - pos).MagnitudeSq();
		if (d < best)
			best = d;
	}
	cam = GAME.GetCamera(1);
	if (cam)
	{
		FVector	cpos = cam->GetPos();
		float	d = (cpos - pos).MagnitudeSq();
		if (d < best)
			best = d;
	}
	return best;
}

//******************************************************************************************
void CParticleManager::RenderMeshes()	// FUNCTION: 0x004cbc60
{
	for (CParticleDescriptor *pd = (CParticleDescriptor *)PARTICLE_SET.mFirstNode; pd; pd = pd->mUnknown40)
		if (pd->GetType() == PD_MESH)
			pd->Unknown23(FALSE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
}

//******************************************************************************************
void CParticleManager::MoveParticles(CParticle *list)	// FUNCTION: 0x004cbca0
{
	if (PARTICLE_MANAGER.IsFirstFrame())
	{
		float		t = PARTICLE_MANAGER.GetFrameLength();
		for (CParticle *q = list; q; q = q->mNext)
		{
			CFoR	*f = q->mFoR;
			if (f)
			{
				if (f->mState == 2)
					q->mPD->Unknown21(q);
				f->mInUse = TRUE;
				f->mParticle = q;
			}
			if (q->mLife > -1.f)
				q->mLife -= t;
			q->mPos += q->mVelocity * t;
			q->mDead |= ((q->mLife > -2.f) && (q->mLife < 0.f));
		}
	}
	else
	{
		float		t = PARTICLE_MANAGER.GetFrameLength();
		for (CParticle *q = list; q; q = q->mNext)
		{
			CFoR	*f = q->mFoR;
			if (f)
			{
				if (f->mState == 2)
					q->mPD->Unknown21(q);
				f->mInUse = TRUE;
				f->mParticle = q;
			}
			if (q->mLife > -1.f)
				q->mLife -= t;
			q->mPos += q->mVelocity * t;
		}
	}
}

//******************************************************************************************
BOOL CParticleManager::FreeDeadParticles()	// FUNCTION: 0x004cbe30
{
	mNumLiveParticles = 0;
	BOOL		alive = FALSE;
	CParticle	*p = mParticles;
	while (p)
	{
		if (p->mDead)
		{
			if (!p->mPrev)
				mParticles = p->mNext;
			else
				p->mPrev->mNext = p->mNext;
			if (p->mNext)
				p->mNext->mPrev = p->mPrev;
			CParticle	*dead = p;
			p = p->mNext;
			FreeParticle(dead);
		}
		else
		{
			p = p->mNext;
			mNumLiveParticles++;
			alive = TRUE;
		}
	}
	return alive;
}
