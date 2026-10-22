// StaticShadows.cpp: reconstructed from StaticShadows.obj (004ebac0..004eeb8f); Stuart's release has no copy.
// Static shadows: each thing that can be statically shadowed gets one shadow map per mesh part, a rectangle of
// 64x64-bit cells on the landscape's 64x64 grid, built by tracing the sun through the part's triangles. The
// visible parts' maps are ORed into STATICSHADOWS' grid, which the landscape reads.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "staticshadows.h"
#include "thing.h"
#include "world.h"
#include "map.h"
#include "engine.h"
#include "landscape.h"
#include "console.h"
#include "CLIParams.h"
#include "eventmanager.h"
#include "Motioncontroller.h"
#include "mesh.h"
#include "chunker.h"
#include "PolyBucket.h"
#include "Line.h"

// FUNCTION: 0x004ebac0 init:ZERO_FVECTOR
// FUNCTION: 0x004ebae0 init:ID_FMATRIX

// STATICSHADOWS itself is defined in DXSurf.cpp (009c8010, whose object registers its empty initializer, 00556420).

//******************************************************************************************
// the console's BuildStaticShadows
void con_buildstaticshadows(char *cmd)	// FUNCTION: 0x004ebbb0
{
	STATICSHADOWS.Rebuild();
}

//******************************************************************************************
void CStaticShadows::Initialise()	// FUNCTION: 0x004ebbc0
{
	CONSOLE.RegisterCommand("BuildStaticShadows", "Force a (re)build of the static shadows", con_buildstaticshadows);
	for (SINT x = 0; x < 64; x++)
		for (SINT y = 0; y < 64; y++)
			mGrid[x][y] = NULL;
	mUnknown4018 = NULL;
	mUnknown04 = 0;
}

//******************************************************************************************
void CStaticShadows::Reattach()	// FUNCTION: 0x004ebc00
{
	CStaticShadow	*s;
	SINT			memory = 0;

	for (s = mFirst; s; s = s->mNext)
	{
		SINT	number = s->mThingNumber;
		if (number != -1)
		{
			CThing	*t = WORLD.GetThingNB().First();
			CThing	*found = NULL;
			for (; t; t = WORLD.GetThingNB().Next())
			{
				if (t->GetThingNumber() == number)
					found = t;
			}
			s->mThing = found;

			char	buf[256];
			sprintf(buf, "Shadow 0x%x has thing %d = 0x%x\n", s, s->mThingNumber, found);
			TRACE(buf);

			if (s->mThing)
				s->mThing->SetStaticShadow(s);
			else
				TRACE("Warning - unattached static shadow\n");
		}

		for (SINT i = 0; i < s->mNumMaps; i++)
		{
			if (s->mMaps[i].mShadowed)
				memory += s->mMaps[i].mW * s->mMaps[i].mH * sizeof(SShadowCell);
		}
	}

	for (s = mFirst; s; s = s->mNext)
		UpdateVisibility(s->mThing, FALSE);
}

//******************************************************************************************
void CStaticShadows::Shutdown()	// FUNCTION: 0x004ebd10
{
	while (mFirst)
	{
		mFirst->mThing = NULL;
		delete mFirst;
	}

	for (SINT x = 0; x < 64; x++)
	{
		for (SINT y = 0; y < 64; y++)
		{
			if (mGrid[x][y])
			{
				delete mGrid[x][y];
				mGrid[x][y] = NULL;
			}
		}
	}

	if (mUnknown4018)
	{
		delete mUnknown4018;
		mUnknown4018 = NULL;
	}
}

// FUNCTION: 0x004ec250 ??_ESShadowMap@@QAEPAXI@Z
// FUNCTION: 0x004ee0c0 ??0SShadowMap@@QAE@XZ

//******************************************************************************************
SShadowMap::~SShadowMap()	// FUNCTION: 0x004ebdf0
{
	if (mCells)
	{
		for (SINT i = 0; i < mW * mH; i++)
			delete mCells[i];
		delete [] mCells;
	}
}

//******************************************************************************************
void CStaticShadows::Rebuild()	// FUNCTION: 0x004ebe40
{
	mLight.X = -MAP.GetHFProperties().SunPosX;
	mLight.Y = -MAP.GetHFProperties().SunPosY;
	mLight.Z = -MAP.GetHFProperties().SunPosZ;

	// no flatter than two across for one down
	float	xy = mLight.MagnitudeXY();
	if (xy > mLight.Z * 2)
	{
		mLight.X *= mLight.Z * 2 / xy;
		mLight.Y *= mLight.Z * 2 / xy;
	}
	mLight.Normalise();

	if (!CLIPARAMS.mNoStaticShadows)
	{
		CThing	*t;
		for (t = WORLD.GetThingNB().First(); t; t = WORLD.GetThingNB().Next())
		{
			if ((t->GetCanBeStaticallyShadowed()) && (!t->GetStaticShadow()))
			{
#line 193
				CStaticShadow	*s = new(MEMTYPE_STATICSHADOW) CStaticShadow;
				s->mThing = t;
				s->mThingNumber = -1;
				t->SetStaticShadow(s);
				s->Build();
			}
		}
	}
}

//******************************************************************************************
// Recomputes which of a thing's parts are shown; when that changed, rebuilds the grid under it. remove clears the
// thing's shadow for good (the thing is going).
void CStaticShadows::UpdateVisibility(CThing *thing, BOOL remove)	// FUNCTION: 0x004ebfb0
{
	static float	last_time = -9999.0f;

	if (!thing)
		return;

	float	now = EVENT_MANAGER.GetTime();
	if ((now > last_time) && (now < last_time + 0.5f) && (!remove))
		return;
	last_time = now;

	CStaticShadow	*s = thing->GetStaticShadow();
	if (!s)
		return;

	BOOL				changed = FALSE;
	CMotionController	*mc = thing->GetMotionController();
	SINT				i;

	for (i = 0; i < s->mNumMaps; i++)
	{
		if (s->mMaps[i].mShadowed)
		{
			BOOL	visible;
			if (remove)
				visible = FALSE;
			else
			{
				CMeshPart	*part = thing->GetRTMesh()->mParts[i];
				SINT		flags = 0;
				if (mc)
					mc->Unknown6(part, &flags);
				if (flags & 1)
					visible = FALSE;
				else
					visible = TRUE;		// a branch that sets 0 or 1 compiles to `not dl; movsx; and 1`, which `!(flags & 1)` does not
			}
			if (visible != s->mMaps[i].mVisible)
			{
				changed = TRUE;
				s->mMaps[i].mVisible = visible;
			}
		}
	}

	if (!changed)
	{
		if (remove)
		{
			s->mThing = NULL;
			thing->SetStaticShadow(NULL);
		}
		return;
	}

	{
		SINT	x = s->mX;
		SINT	y = s->mY;
		SINT	w = s->mW;
		SINT	h = s->mH;

		if (x < 0)
			x = 0;
		if (y < 0)
			y = 0;
		if (x + w >= 64)
			w = 63 - x;
		if (y + h >= 64)
			h = 63 - y;

		SINT	gx, gy;
		for (gx = x; gx < x + w; gx++)
		{
			for (gy = y; gy < y + h; gy++)
			{
				if (mGrid[gx][gy])
				{
					for (SINT r = 0; r < 64; r++)
						for (SINT c = 0; c < 2; c++)
							mGrid[gx][gy]->mBits[r][c] = 0;
				}
			}
		}

		for (CStaticShadow *o = mFirst; o; o = o->mNext)
		{
			if ((o->mX < x + w) && (o->mX + o->mW > x) && (o->mY < y + h) && (o->mY + o->mH > y))
				o->ApplyToGrid(x, y, w, h);
		}

		ENGINE.GetLandscape()->UpdateArea(x * 8, y * 8, (x + w) * 8, (y + h) * 8, FALSE);
	}

	if (remove)
	{
		s->mThing = NULL;
		thing->SetStaticShadow(NULL);
	}
}

//******************************************************************************************
// Defined here, after UpdateVisibility: the vector deleting destructor its delete [] needs (??_ESShadowMap, 004ec250)
// sits between UpdateVisibility and Build in retail, where the front end first meets the delete.
inline CStaticShadow::~CStaticShadow()
{
	if (mThing)
		STATICSHADOWS.UpdateVisibility(mThing, TRUE);

	if (STATICSHADOWS.mFirst == this)
		STATICSHADOWS.mFirst = mNext;
	else
	{
		CStaticShadow	*p = STATICSHADOWS.mFirst;
		while ((p) && (p->mNext != this))
			p = p->mNext;
		p->mNext = mNext;
	}

	if (mMaps)
	{
		delete [] mMaps;
		mMaps = NULL;
	}
}

// STAND-IN (StaticShadows-only helpers; their original owners and spellings are unknown, replace them when a source form
// gives Build the same symbol ids). ShadowTrace forwards each corner's trace to CMap::Unknown490a40 (00490a40) with
// the result in a local: the forwarding inline is evidenced by the inliner's budget (each corner carries one free site
// of size <= 40 between CLine's constructor and destructor), and the local adds eight inline field-pool symbols that
// put corners 4 and 5's first rank-pass fields in retail's windows. ShadowMul, ShadowSub and ShadowScale are copies of
// FMatrix * FVector, FVector - FVector and FVector * float for lstart and lend: they create inv's, lstart's and ldir's
// fields in rank pass 1 instead of the CSE pass (12 symbols before the late field batch). Same code as the operators.
static inline BOOL ShadowTrace(class CLine *line, FVector *hit, SINT includeWater)
{
	BOOL	r = MAP.Unknown490a40(line, hit, includeWater);
	return r;
}
static inline FVector ShadowMul(const FMatrix &m, const FVector &v) { return FVector((m.Row[0].X * v.X) + (m.Row[0].Y * v.Y) + (m.Row[0].Z * v.Z), (m.Row[1].X * v.X) + (m.Row[1].Y * v.Y) + (m.Row[1].Z * v.Z), (m.Row[2].X * v.X) + (m.Row[2].Y * v.Y) + (m.Row[2].Z * v.Z)); }
static inline FVector ShadowSub(const FVector &a, const FVector &b) { return FVector(a.X - b.X, a.Y - b.Y, a.Z - b.Z); }
static inline FVector ShadowScale(const FVector &a, float f) { return FVector(a.X * f, a.Y * f, a.Z * f); }

//******************************************************************************************
// Builds a shadow map for each part that collides: the cells under the part's box, each cell 64x64 points, a
// point shadowed when the line from it towards the light passes through one of the part's triangles.
// Each corner of a part's box throws its shadow on the ground: straight down when it is above the ground, else
// along the light until the line meets the landscape. The eight corners are written out in the body, not through a
// helper; retail expands CLine's constructor for each (a helper's nesting sent it out of line).
// The inliner's budget fixes the rest of the shape (the inline-budget simulator, checked by compiling): each corner's
// trace goes through CMap's forwarding inline (one free site between CLine's constructor and destructor; with
// MAP.Unknown490a40 called directly the sixth corner keeps its constructors inline and the per-pixel matrix's vector
// constructor iterator is called), the light vector is `light * 500.0f`, and the triangle loop is
// MeshCollisionVolume's (the search's result kept, the packing taken before the test as retail does at 004edd6d,
// the same Unpack for the three vertices: two inline, the third called). The corners are assigned to one c:
// retail builds each sum in a temporary and copies its 16 bytes into c (004ec8fb..004ec941). The bit is shifted
// once before the test (004edf32). The centre is `ori * box->mOrigin + pos` (each of its rows adds as retail's).
// `hit` is cleared before the point is built: retail stores the point's Z from that zero (004edad1..004edaec).
// Matched (from field-pool measurements). The corners' product orders and the
// inverse's rows follow C2's kind 4/5 field-pool symbol ids: ori's Row[2]/Row[1] fields must be created at 0x89a (the
// CSE pass's late field batch), corner 4's first rank-pass field below 0x800 and corner 5's at or above 0x808 (a
// diagnostic C2 adding those symbols compiled the earlier source byte-identical). The STAND-INs that supply them, all
// consumed values with no code of their own (measured with the inline budget pinned to retail's own size 3301):
// - per corner, the hit and the line through named pointers (`FVector *hit`, `CLine *pl`): 16 import-time symbols;
// - ShadowTrace's local and the three reference helpers above: 8 inline and 12 rank-pass symbols;
// - in the per-pixel loop, a copy of the inverse for ldir (`FMatrix(inv) * light`, 1 rank-pass and 16 late symbols) and a copy of the
//   point for lstart (`FVector(p) - ppos`, 2 rank-pass and 5 late symbols).
// Without those, with the rest of this source: the corners' first products load the matrix element first where
// retail loads the half-size first (004ec64c, 004ec66f, 004ecabe, 004ecae3), the reverse at 004ed14f..004ed1a2 and
// the inverse's rows differ at 004edc09 (38/154/3604 by shape score on the earlier branch, 7a935363). Two
// spellings set Build's own size to 3300 (retail's window 3298..3301) without moving any id: each corner's CLine is
// copy-initialised (`CLine line = CLine(...)`, -4 each) and so is the per-pixel point (`FVector p = FVector(...)`, -4).
// Replace any of these when a source form gives the same ids and size naturally.
void CStaticShadow::Build()	// FUNCTION: 0x004ec2f0
{
	CMESH	*mesh = mThing->GetRTMesh();
	mNumMaps = mesh->mNumParts;
#line 394
	mMaps = new(MEMTYPE_STATICSHADOW) SShadowMap[mNumMaps];

	FVector	light = STATICSHADOWS.mLight;
	SINT	minx = 0x200, maxx = 0, miny = 0x200, maxy = 0;

	for (SINT i = 0; i < mNumMaps; i++)
	{
		CMeshPart	*part = mesh->mParts[i];
		// Type 6 uses its collision part for triangle searches (retail 004edd47..004edd68).
		CMeshPart	*collisionPart = part->GetMeshDataPart();
		if (collisionPart && ((part->mPartType == 1) || (part->mPartType == 6)))
		{
			mMaps[i].mShadowed = TRUE;

			CPolyBucket	*old = part->mPolyBucket;
#line 423
			part->mPolyBucket = new(MEMTYPE_SCRATCHPAD) CPolyBucket;		// a temporary bucket (retail 0x61)
			if (!part->mPolyBucket->Build(part))
				SAFE_DELETE(part->mPolyBucket);

			FMatrix	ori;
			FVector	pos;
			CBoundingBox	*box = part->mBoundingBox;
			FVector	l500 = light * 500.0f;

			SINT	x0 = 0x200, x1 = 0, y0 = 0x200, y1 = 0;

			part->GetWorldTransforms(0, 0, 0, mThing, &ori, &pos, false, TRUE, 0);
			FVector	centre = ori * box->mOrigin + pos;
			float	ax = box->mAxes.X;
			float	ay = box->mAxes.Y;
			float	az = box->mAxes.Z;

			FVector	hits[8];
			FVector	c;
			c = centre + ori * FVector(-ax, -ay, -az);
			if (MAP.Collide(c) < c.Z)
				hits[0] = FVector(c.X, c.Y, MAP.Collide(c));
			else
			{
				FVector	*hit = hits;				// STAND-IN (ids; every corner, see above Build)
				CLine	line = CLine(c, l500 + c);	// STAND-IN (own size; every corner)
				CLine	*pl = &line;				// STAND-IN (ids; every corner)
				ShadowTrace(pl, hit, FALSE);
			}
			c = centre + ori * FVector( ax, -ay, -az);
			if (MAP.Collide(c) < c.Z)
				hits[1] = FVector(c.X, c.Y, MAP.Collide(c));
			else
			{
				FVector	*hit = &hits[1];
				CLine	line = CLine(c, l500 + c);
				CLine	*pl = &line;
				ShadowTrace(pl, hit, FALSE);
			}
			c = centre + ori * FVector(-ax,  ay, -az);
			if (MAP.Collide(c) < c.Z)
				hits[2] = FVector(c.X, c.Y, MAP.Collide(c));
			else
			{
				FVector	*hit = &hits[2];
				CLine	line = CLine(c, l500 + c);
				CLine	*pl = &line;
				ShadowTrace(pl, hit, FALSE);
			}
			c = centre + ori * FVector( ax,  ay, -az);
			if (MAP.Collide(c) < c.Z)
				hits[3] = FVector(c.X, c.Y, MAP.Collide(c));
			else
			{
				FVector	*hit = &hits[3];
				CLine	line = CLine(c, l500 + c);
				CLine	*pl = &line;
				ShadowTrace(pl, hit, FALSE);
			}
			c = centre + ori * FVector(-ax, -ay,  az);
			if (MAP.Collide(c) < c.Z)
				hits[4] = FVector(c.X, c.Y, MAP.Collide(c));
			else
			{
				FVector	*hit = &hits[4];
				CLine	line = CLine(c, l500 + c);
				CLine	*pl = &line;
				ShadowTrace(pl, hit, FALSE);
			}
			c = centre + ori * FVector( ax, -ay,  az);
			if (MAP.Collide(c) < c.Z)
				hits[5] = FVector(c.X, c.Y, MAP.Collide(c));
			else
			{
				FVector	*hit = &hits[5];
				CLine	line = CLine(c, l500 + c);
				CLine	*pl = &line;
				ShadowTrace(pl, hit, FALSE);
			}
			c = centre + ori * FVector(-ax,  ay,  az);
			if (MAP.Collide(c) < c.Z)
				hits[6] = FVector(c.X, c.Y, MAP.Collide(c));
			else
			{
				FVector	*hit = &hits[6];
				CLine	line = CLine(c, l500 + c);
				CLine	*pl = &line;
				ShadowTrace(pl, hit, FALSE);
			}
			c = centre + ori * FVector( ax,  ay,  az);
			if (MAP.Collide(c) < c.Z)
				hits[7] = FVector(c.X, c.Y, MAP.Collide(c));
			else
			{
				FVector	*hit = &hits[7];
				CLine	line = CLine(c, l500 + c);
				CLine	*pl = &line;
				ShadowTrace(pl, hit, FALSE);
			}
			SINT	k;
			for (k = 0; k < 8; k++)
			{
				SINT	x = (SINT)floor(hits[k].X * 0.125f);
				SINT	y = (SINT)floor(hits[k].Y * 0.125f);
				if (x < x0)
					x0 = x;
				if (x > x1)
					x1 = x;
				if (y < y0)
					y0 = y;
				if (y > y1)
					y1 = y;
			}

			if (x0 < 0)
				x0 = 0;
			if (x1 >= 64)
				x1 = 63;
			if (y0 < 0)
				y0 = 0;
			if (y1 >= 64)
				y1 = 63;
			if (x1 < 0)
			{
				x1 = 0;
				mMaps[i].mShadowed = FALSE;
			}
			if (x0 >= 64)
			{
				x0 = 63;
				mMaps[i].mShadowed = FALSE;
			}
			if (y1 < 0)
			{
				y1 = 0;
				mMaps[i].mShadowed = FALSE;
			}
			if (y0 >= 64)
			{
				y0 = 63;
				mMaps[i].mShadowed = FALSE;
			}

			if (x0 < minx)
				minx = x0;
			if (x1 > maxx)
				maxx = x1;
			if (y0 < miny)
				miny = y0;
			if (y1 > maxy)
				maxy = y1;

			mMaps[i].mX = x0;
			mMaps[i].mY = y0;
			mMaps[i].mW = x1 - x0 + 1;
			mMaps[i].mH = y1 - y0 + 1;
			mMaps[i].mVisible = TRUE;
			mMaps[i].mShadowed = TRUE;
#line 612
			mMaps[i].mCells = new(MEMTYPE_STATICSHADOW) SShadowCell *[mMaps[i].mW * mMaps[i].mH];

			for (SINT gx = x0; gx <= x1; gx++)
			{
				for (SINT gy = y0; gy <= y1; gy++)
				{
					float	fx = gx * 8.0f;
					char	buf[256];
					sprintf(buf, "Building shadow map for %d/%d %d,%d\n", i + 1, mNumMaps, gx, gy);
					TRACE(buf);

#line 623
					SShadowCell	*cell = new(MEMTYPE_STATICSHADOW) SShadowCell;
					BOOL		empty = TRUE;
					float		fy = gy * 8.0f;
					mMaps[i].mCells[(gx - x0) + (gy - y0) * mMaps[i].mW] = cell;

					for (SINT py = 0; py < 64; py++)
					{
						float	y = py * 0.125f + fy;
						SINT	word = 0;
						SINT	bit = 0;
						for (SINT px = 0; px < 64; px++)
						{
							BOOL	hit = FALSE;
							FVector	p = FVector(px * 0.125f + fx, y, 0.0f);	// STAND-IN (own size, see above Build)
							p.Z = MAP.Collide(p);

							FMatrix	pori;
							FVector	ppos;
							part->GetWorldTransforms(0, 0, 0, mThing, &pori, &ppos, false, FALSE, 0);
							FMatrix	inv = pori.Inverse();
							FVector	lstart = ShadowMul(inv, FVector(p) - ppos);	// STAND-IN (ids: the copy of p)
							FVector	ldir = FMatrix(inv) * light;	// STAND-IN (ids: the copy of inv)
							FVector	lend = ShadowSub(lstart, ShadowScale(ldir, 500.0f));

							CPolyBucketVertex	*tri[3];
							BOOL	found = collisionPart->GetFirstPolyLine(&lstart, &lend, tri);
							CPolyBucketPacking	*pk = &collisionPart->mPolyBucket->mPacking;
							if (found)
							{
								do
								{
									FVector	v0, v1, v2;
									tri[0]->Unpack(&v0, pk);
									tri[1]->Unpack(&v1, pk);
									tri[2]->Unpack(&v2, pk);
									if (RayHitsTriangle(&v0, &v1, &v2, lstart, lstart - ldir * 500.0f))
										hit = TRUE;
								} while (collisionPart->GetNextPolyLine(tri, 0));
							}

							DWORD	b;
							if (hit)
							{
								b = 1;
								empty = FALSE;
							}
							else
								b = 0;

							b = b << bit;
							if (!bit)
								cell->mBits[py][word] = b;
							else
								cell->mBits[py][word] |= b;
							if (++bit == 32)
							{
								bit = 0;
								word++;
							}
						}
					}

					if (empty)
					{
						SAFE_DELETE(cell);
						mMaps[i].mCells[(gx - x0) + (gy - y0) * mMaps[i].mW] = NULL;
					}
				}
			}

			SAFE_DELETE(part->mPolyBucket);
			part->mPolyBucket = old;
		}
		else
			mMaps[i].mShadowed = FALSE;
	}

	TRACE("Shadow map done\n");
	mX = minx;
	mY = miny;
	mW = maxx - minx + 1;
	mH = maxy - miny + 1;
	ApplyToGrid(-1, -1, -1, -1);
}

// FUNCTION: 0x004ee0d0 ??_GCPolyBucket@@QAEPAXI@Z

//******************************************************************************************
// ORs the shadow's visible maps into STATICSHADOWS' grid, within x, y, w, h (-1 takes the shadow's own bound).
void CStaticShadow::ApplyToGrid(SINT x, SINT y, SINT w, SINT h)	// FUNCTION: 0x004ee0f0
{
	if (!mThing)
		return;

	if (!mThing->GetRTMesh())
	{
		char	buf[256];
		sprintf(buf, "Thing at 0x%x has no RTMesh when trying to statically shadow!\n", mThing);
		TRACE(buf);
		return;
	}

	if (x == -1)
		x = mX;
	if (y == -1)
		y = mY;
	if (w == -1)
		w = mW;
	if (h == -1)
		h = mH;

	CMESH				*mesh = mThing->GetRTMesh();
	CMotionController	*mc = mThing->GetMotionController();

	for (SINT i = 0; i < mNumMaps; i++)
	{
		if ((mMaps[i].mShadowed) && (mMaps[i].mVisible))
		{
			CMeshPart	*part = mesh->mParts[i];
			if (mc)
			{
				SINT	level = mc->Unknown7(part);
				if ((level) && (level > 0))
				{
					for (; level; level--)
						if (part)
							part = part->mNextMeshPartInChain;
				}
			}

			if (part)
			{
				SINT	x0 = mMaps[i].mX;
				SINT	x1 = mMaps[i].mX + mMaps[i].mW;
				SINT	y0 = mMaps[i].mY;
				SINT	y1 = mMaps[i].mY + mMaps[i].mH;

				if (x > x0)
					x0 = x;
				if (y > y0)
					y0 = y;
				if (x + w < x1)
					x1 = x + w;
				if (y + h < y1)
					y1 = y + h;

				if (x0 < 0)
					x0 = 0;
				if (y0 < 0)
					y0 = 0;
				if (x1 > 63)
					x1 = 63;
				if (y1 > 63)
					y1 = 63;

				if ((x0 < x1) && (y0 < y1))
				{
					for (SINT gx = x0; gx < x1; gx++)
					{
						for (SINT gy = y0; gy < y1; gy++)
						{
							SShadowCell	*cell = STATICSHADOWS.mGrid[gx][gy];
							if (!cell)
							{
#line 839
								STATICSHADOWS.mGrid[gx][gy] = cell = new(MEMTYPE_STATICSHADOW) SShadowCell;
								memset(cell, 0, sizeof(SShadowCell));
							}

							SShadowCell	*src = mMaps[i].mCells[(gx - mMaps[i].mX) + (gy - mMaps[i].mY) * mMaps[i].mW];
							if (src)
							{
								for (SINT r = 0; r < 64; r++)
									for (SINT c = 0; c < 2; c++)
										cell->mBits[r][c] |= src->mBits[r][c];
							}
						}
					}
				}
			}
		}
	}
}

//******************************************************************************************
// Does the segment from start to end pass through the triangle a, b, c? It meets the plane between its ends, and
// the angles the meeting point subtends at the corners add up to a full turn.
// The plane test compares in float (retail fcomp DWORD at 004ee503), the angle total in double (fcomp QWORD at
// 004ee881). The corner dot products are written out: named in the source, the corner vectors' members get their
// symbols before the inlined Normalise's reciprocal, which is what puts the reciprocal first in each Y and Z scaling
// (004ee622, 004ee6b1, 004ee742) and orders every dot product Z, Y, X. t is initialised with the ray's deltas: retail
// loads the delta first in the meeting point's three products (004ee57d..004ee59d).
BOOL CStaticShadow::RayHitsTriangle(FVector *a, FVector *b, FVector *c, FVector start, FVector end)	// FUNCTION: 0x004ee410
{
	// the plane's normal
	FVector	n;
	n.X = (b->Y - a->Y) * (c->Z - a->Z) - (b->Z - a->Z) * (c->Y - a->Y);
	n.Y = (b->Z - a->Z) * (c->X - a->X) - (b->X - a->X) * (c->Z - a->Z);
	n.Z = (b->X - a->X) * (c->Y - a->Y) - (b->Y - a->Y) * (c->X - a->X);
	n.Normalise();

	float	t = 0.f;
	float	dz = end.Z - start.Z;
	float	dy = end.Y - start.Y;
	float	dx = end.X - start.X;
	float	denom = dx * n.X + dy * n.Y + dz * n.Z;
	if ((float)fabs(denom) < 0.005f)
		return FALSE;

	float	D = -(n.X * a->X) - n.Y * a->Y - n.Z * a->Z;
	// Retail 004ee533..004ee549 accumulates Z, Y, then X. Reassociating this sum changes near-plane admission.
	D += start.Z * n.Z;
	D += start.Y * n.Y;
	D += start.X * n.X;
	t = -D / denom;
	if ((t < 0) || (t > 1.0f))
		return FALSE;

	FVector	p;
	p.X = dx * t + start.X;
	p.Y = dy * t + start.Y;
	p.Z = dz * t + start.Z;

	FVector	va;
	va = *a - p;
	va.Normalise();
	FVector	vb;
	vb = *b - p;
	vb.Normalise();
	FVector	vc;
	vc = *c - p;
	vc.Normalise();

	float	ab = va.X * vb.X + va.Y * vb.Y + va.Z * vb.Z;
	float	bc = vb.X * vc.X + vb.Y * vc.Y + vb.Z * vc.Z;
	float	ca = vc.X * va.X + vc.Y * va.Y + vc.Z * va.Z;

	if (ab > 1.0f)
		ab = 1.0f;
	else if (ab < -1.0f)
		ab = -1.0f;
	if (bc > 1.0f)
		bc = 1.0f;
	else if (bc < -1.0f)
		bc = -1.0f;
	if (ca > 1.0f)
		ca = 1.0f;
	else if (ca < -1.0f)
		ca = -1.0f;

	float	total = (float)acos(ab) + (float)acos(bc) + (float)acos(ca);
	if (fabs(total - PI_M2) > 0.005)
		return FALSE;
	return TRUE;
}

//******************************************************************************************
void CStaticShadows::DeserializeAll(CChunkReader *c)	// FUNCTION: 0x004ee8a0
{
	c->GetNext();
	c->GetNext();
	SINT	count;
	c->Read(&count, 4, 1);
	c->GetNext();
	for (SINT i = 0; i < count; i++)
		CStaticShadow::Deserialize(c);
}

//******************************************************************************************
void CStaticShadow::Deserialize(CChunkReader *c)	// FUNCTION: 0x004ee8f0
{
	c->GetNext();
#line 1074
	CStaticShadow	*s = new(MEMTYPE_STATICSHADOW) CStaticShadow;
	c->Read(&s->mX, 4, 1);
	c->Read(&s->mY, 4, 1);
	c->Read(&s->mW, 4, 1);
	c->Read(&s->mH, 4, 1);
	c->Read(&s->mNumMaps, 4, 1);
	c->Read(&s->mThingNumber, 4, 1);
#line 1085
	s->mMaps = new(MEMTYPE_STATICSHADOW) SShadowMap[s->mNumMaps];

	for (SINT i = 0; i < s->mNumMaps; i++)
	{
		c->GetNext();
		c->Read(&s->mMaps[i].mX, 4, 1);
		c->Read(&s->mMaps[i].mY, 4, 1);
		c->Read(&s->mMaps[i].mW, 4, 1);
		c->Read(&s->mMaps[i].mH, 4, 1);
		c->Read(&s->mMaps[i].mShadowed, 4, 1);
		if (s->mMaps[i].mShadowed)
		{
#line 1101
			s->mMaps[i].mCells = new(MEMTYPE_STATICSHADOW) SShadowCell *[s->mMaps[i].mW * s->mMaps[i].mH];
			c->GetNext();
			for (SINT j = 0; j < s->mMaps[i].mW * s->mMaps[i].mH; j++)
			{
				SINT	present = 0;
				c->Read(&present, 4, 1);
				if (present == 1)
				{
#line 1122
					s->mMaps[i].mCells[j] = new(MEMTYPE_STATICSHADOW) SShadowCell;
					c->Read(s->mMaps[i].mCells[j], sizeof(SShadowCell), 1);
				}
				else
					s->mMaps[i].mCells[j] = NULL;
			}
		}
	}
}
