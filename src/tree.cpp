// tree.cpp: reconstructed from tree.obj (004f5e40..004f707f); Stuart's release has no copy. A tree takes one of 32
// yaw orientations, sits on the terrain and, once hit hard or damaged to nothing, falls away from what hit it and
// fades out. The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#define TREE_CPP
#include "tree.h"
#include "game.h"
#include "map.h"
#include "cylinder.h"
#include "collisionseekingthing.h"
#include "eventmanager.h"
#include "Event.h"
#include "InitThing.h"
#include "RenderThing.h"
#include "meshrenderer.h"
#include "explosion.h"
#include "Line.h"
#include "ParticleManager.h"
#include "ParticleSet.h"
#include "imposter.h"

// FUNCTION: 0x004f5e40 init:ZERO_FVECTOR
// FUNCTION: 0x004f5e60 init:ID_FMATRIX

#define TREE_FALL		3000	// the falling tree's updates (HandleEvent 004f7058)
#define TREE_COLLIDE	3001	// half a second after it starts to fall, it collides with everything (004f706b)

static FMatrix	sOrientations[32];			// 008406b8: yaws of i * PI/16 (Init)	// FUNCTION: 0x004f5f30 init:sOrientations
static BOOL		sOrientationsReady = FALSE;	// 00840cf8

//******************************************************************************************
CFallingTree::CFallingTree(const FMatrix &ori, float height, const FVector &dir)	// FUNCTION: 0x004f5f60
{
	mStartOrientation = ori;
	mAngle = 0;
	mSpeed = 0;
	mOrientation = ori;
	mUnknown8C = ori;
	mHeight = height;
	mDir = dir;
	mAxis = dir ^ FVector(0, 0, -1.0f);
	mAxis.Normalise();
	mSpeed = 0.02f;
}

//******************************************************************************************
void CTree::Init(CInitThing *init)	// FUNCTION: 0x004f6080
{
	CTreeInitThing	*ti = (CTreeInitThing *)init;

	if (!sOrientationsReady)
	{
		for (SINT n = 0; n < 32; n++)
			sOrientations[n].MakeRotationYawF(float(n) * (PI / 16.0f));
		sOrientationsReady = TRUE;
	}

	mVariation = SINT(float(GAME.Random() % 65536) * (1.0f / 2048.0f));
	init->mPos.Z = MAP.Collide(init->mPos);
	init->mOrientation = ID_FMATRIX;
	init->mOrientationType = DIRECTION_COSINE_MATRIX;

	CRenderData	rd;
	rd.mInit[0].mThing = this;
	rd.mRTID[0] = 2;
	strcpy(rd.mInit[0].mName, ti->mTreeType);
	if (!ti->mTreeTypeComplete)
	{
		char	num[256];
		sprintf(num, "%d", GAME.Random() % 4);
		strcat(rd.mInit[0].mName, num);
	}
	strcat(rd.mInit[0].mName, ".MSH");

	mRenderThing = CreateRenderThing(rd.mRTID[0]);
	if (mRenderThing)
		mRenderThing->Init(&rd.mInit[0]);

	if (!_strnicmp(rd.mInit[0].mName, "fern", 4) || !_strnicmp(rd.mInit[0].mName, "bush", 4))
		init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;

	CThing::Init(init);
	mFalling = NULL;
	mLife = 1.5f;
	mUnknown3C = 0;
}

//******************************************************************************************
CTree::~CTree()	// FUNCTION: 0x004f63c0
{
	SAFE_DELETE(mFalling);
}

//******************************************************************************************
UBYTE CTree::GetShadowSize()	// FUNCTION: 0x004f6430
{
	float	size = GetBoundingBox()->mAxes.X;
	float	y = GetBoundingBox()->mAxes.Y;
	if (y > size)
		size = y;
	UBYTE	s = UBYTE(SINT(size * 6.0f));
	if (s > 6)
		s = 6;
	return s;
}

//******************************************************************************************
void CTree::InitCollisionSeekingThing(CInitCSThing *init)	// FUNCTION: 0x004f6480
{
	if (init->mNotSeekCollisionWithBF != 0xffffffff)
	{
#line 143
		init->mShape = new(MEMTYPE_GENERAL_VOLUME) CCylinder(0.2f, GetRadius() * 0.5f);
		init->mNotSeekCollisionWithBF = THING_TYPE_STATIONARY;
		init->mMaxCollision = ECL_APPROX_GEOMETRY_SHAPES;
		CThing::InitCollisionSeekingThing(init);
	}
}

//******************************************************************************************
int CTree::GetRenderImposterNo()	// FUNCTION: 0x004f6540
{
	mUnknown3C = 0;
	return ((DWORD)this >> 4) & 3;
}

//******************************************************************************************
// The orientation it is drawn with. The first call picks its variation from its address and the imposters'
// angle; a falling tree is drawn between its last two orientations by the frame's render fraction.
FMatrix CTree::GetRenderOrientation()	// FUNCTION: 0x004f6560
{
	if (!mUnknown3C)
	{
		mVariation = SINT((float(SINT(((DWORD)this >> 4) & 3)) * (PI / 2.0f) + CDXImposter::sAngle) * 32.0f);
		if (mVariation >= 32)
			mVariation -= (mVariation / 32) * 32;
		else if (mVariation < 0)
			mVariation += (1 - mVariation / 32) * 32;
		mUnknown3C = 1;
	}
	else if (mFalling)
	{
		FMatrix	ori;
		ori = mFalling->mUnknown8C + (mFalling->mStartOrientation - mFalling->mUnknown8C) * GAME.GetFrameRenderFraction();
		ori.Normalise();
		return ori;
	}
	return sOrientations[mVariation];
}

//******************************************************************************************
void CTree::Damage(float amount, CThing *inByThis, BOOL inDamageShields, int mesh_part_no)	// FUNCTION: 0x004f68e0
{
	if (mFalling || mLife == 0)
		return;

	mLife -= amount;
	if (mLife <= 0)
	{
		mLife = 0;
		FVector	dir = mPos - inByThis->GetPos();
		dir.Normalise();
		CreateFallingTree(dir);
	}
}

//******************************************************************************************
void CTree::CreateFallingTree(const FVector &dir)	// FUNCTION: 0x004f69b0
{
	if (mFalling)
		return;

	FMatrix	ori = sOrientations[mVariation];
#line 240
	mFalling = new(MT_TREE_THING) CFallingTree(ori, GetRadius() * 2, dir);
	mFalling->mStartTime = -1.0f;
	EVENT_MANAGER.AddEvent(0.5f, TREE_COLLIDE, this);
	UpdateFallingTree();
}

//******************************************************************************************
void CTree::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x004f6aa0
{
	if (!other_thing->IsA(THING_TYPE_CAN_DESTROY_TREES) || mFalling)
		return;

	FVector	dir = mPos - other_thing->GetPos();
	if (other_thing->IsA(THING_TYPE_EXPLOSION) && !(dir.Magnitude() <= ((CExplosion *)other_thing)->mCurrentRadius))
		return;
	dir.Normalise();
	CreateFallingTree(dir);
}

//******************************************************************************************
// STAND-IN (address form; labelled 2026-10-15, UpdateFallingTree is a near miss: relocations only). The falling
// tree's direction, as a reference. Retail forms its address once and reads both components through it
// (add eax,0xc; fld [eax]; fld [eax+4], 004f6d6e..): a reference whose definition contains the load of mFalling stays a
// variable, where one built from GetFalling()'s result folds into displacements ([eax+0xc], [eax+0x10]). Name ours.
// Plain `GetFalling()->mDir.Azimuth()` (or mFalling->mDir): score 5/5/514 against 0/0/12 with the helper. Original: unknown.
static inline FVector &FallingDir(CTree *t)
{
	return t->mFalling->mDir;
}

//******************************************************************************************
// STAND-IN (inline budget; labelled 2026-10-15): stands in for the depth of the falling tree's turn: retail
// calls FMatrix::operator* out of line here (0040d320), which a top-level expansion never does (48 statements of padding before it leave it inline). It reads the member at
// each use, as retail does: the product's argument comes from the pointer loaded for the angle block (004f6e2b), and the
// destination is loaded again after the call (004f6f48); a CFallingTree parameter would be one variable live across the
// call, held in esi.
// Written in place (`mFalling->mStartOrientation = r * mFalling->mOrientation;` or through GetFalling()): score
// 127..136/305..312/1533..1553, and FMatrix::operator*'s out-of-line copy 0040d320 is no longer emitted. Original: unknown.
static inline void SetStartOrientation(CTree *t, const FMatrix &r)
{
	t->mFalling->mStartOrientation = r * t->mFalling->mOrientation;
}

//******************************************************************************************
// The ground test's far end: retail scales the start orientation's Z column by -height first, Z row first
// (004f6c0b..004f6c20), then adds the position member by member through the same pointer it copied the start from
// (004f6c2a..004f6c42), so the scaled column is a vector of its own and the sums go into the end point's members.
// The effect's descriptor is looked up before the effect's arguments are built (004f6d1d). The turn's nine elements
// come from named products, defined as retail computes them (004f6e5a..004f6eb8): the three squares and xy stay on the
// x87 stack, xz and yz and the three axis-times-sine terms go to the frame, then the elements in order (004f6ebc..).
// The trig is sinf and cosf with the cosine in a named float: VC6 schedules this block in a region capped at 81 nodes,
// and three more nodes before the cap (an inline sinf, an inline cosf, the named float) put the last element's multiply
// after the product call's argument setup (004f6f24..004f6f35) as in retail; the spelling reproduces the count.
void CTree::UpdateFallingTree()	// FUNCTION: 0x004f6b80
{
	BOOL	carry_on = TRUE;
	if (!mFalling)
		return;

	GetFalling()->mUnknown8C = GetFalling()->mStartOrientation;
	if (GetFalling()->mSpeed > 0)
	{
		FVector	&pos = GetPos();
		FVector	start; start = pos;
		start.Z -= 0.2f;
		float	h = -GetFalling()->mHeight;
		FMatrix	&o = GetFalling()->mStartOrientation;
		FVector	d(o.Row[0].Z * h, o.Row[1].Z * h, o.Row[2].Z * h);
		FVector	end;
		end.X = d.X + pos.X;
		end.Y = d.Y + pos.Y;
		end.Z = d.Z + pos.Z + 0.5f;
		CLine	line(start, end);
		FVector	hitpos;
		BOOL	hit = MAP.Unknown490a40(&line, &hitpos, 0);
		if (hit)
		{
			if (GetFalling()->mSpeed > 0.05f && GetFalling()->mHeight > 3.0f)
			{
				CParticleEffectLink	link;
				CParticleDescriptor	*pd = PARTICLE_SET.FindByName("Tree Ground Hit Effect");
				PARTICLE_MANAGER.CreateEffect(pd, &link, ZERO_FVECTOR, 0, 0);
				link.SetPos(GetPos());
				FMatrix	m(FallingDir(this).Azimuth(), 0, 0);
				link.SetOrientation(m);
			}
			GetFalling()->mAngle -= GetFalling()->mSpeed * 0.5f;
			GetFalling()->mSpeed *= -0.2f;
		}
		if (fabs(GetFalling()->mSpeed) < 0.01 && hit)
			carry_on = FALSE;
	}

	if (carry_on)
	{
		GetFalling()->mAngle += GetFalling()->mSpeed;
		GetFalling()->mSpeed += 0.005f;

		float	a = GetFalling()->mAngle;
		float	s = sinf(a);
		float	c = cosf(a);
		float	t = 1.0f - c;
		FVector	ax = GetFalling()->mAxis;
		FMatrix	r;
		float	xx = ax.X * ax.X, yy = ax.Y * ax.Y, zz = ax.Z * ax.Z;
		float	xy = ax.X * ax.Y, xz = ax.X * ax.Z, yz = ax.Y * ax.Z;
		float	xs = ax.X * s, ys = ax.Y * s, zs = ax.Z * s;
		r.Row[0].X = 1.0f - (yy + zz) * t;
		r.Row[0].Y = xy * t + zs;
		r.Row[0].Z = xz * t - ys;
		r.Row[1].X = xy * t - zs;
		r.Row[1].Y = 1.0f - (xx + zz) * t;
		r.Row[1].Z = yz * t + xs;
		r.Row[2].X = xz * t + ys;
		r.Row[2].Y = yz * t - xs;
		r.Row[2].Z = 1.0f - (xx + yy) * t;
		SetStartOrientation(this, r);
		EVENT_MANAGER.AddEvent(TREE_FALL, this, NEXT_FRAME);
	}
	else
	{
		GetFalling()->mStartTime = EVENT_MANAGER.GetTime();
		EVENT_MANAGER.AddEvent(5.0f, START_DIE_PROCESS, this);
	}
}

//******************************************************************************************
bool CTree::RenderFalling(DWORD flags)	// FUNCTION: 0x004f6fd0
{
	if (mFalling->mStartTime > 0)
	{
		float	t = mFalling->mStartTime + 5.0f - EVENT_MANAGER.GetTime();
		if (t < 0)
			t = 0;
		MESH_ALPHA = SINT((t / 5.0f) * 255.0f);
		CThing::Render(flags);
		MESH_ALPHA = 0xff;
		return true;
	}
	return false;
}

//******************************************************************************************
void CTree::HandleEvent(CEvent *event)	// FUNCTION: 0x004f7050
{
	switch (event->GetEventNum())
	{
	case TREE_FALL:
		UpdateFallingTree();
		break;
	case TREE_COLLIDE:
		mCollisionSeekingThing->SetCollisionMask(0xffffffff);
		break;
	default:
		CThing::HandleEvent(event);
	}
}
