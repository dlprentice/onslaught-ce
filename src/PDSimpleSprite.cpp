// PDSimpleSprite.cpp: the simple sprite particles' drawing (reconstructed from PDSimpleSprite.obj, 004c5b60..; the
// file name is ours: the object has no strings): the sprites' quads, the trails and the mesh particles.

#include	"common.h"
#define PDSIMPLESPRITE_CPP
#include	"mathsconst.h"
#include	"ParticleDescriptor.h"
#include	"ParticleManager.h"
#include	"mesh.h"
#include	"meshrenderer.h"
#include	"engine.h"
#include	"Camera.h"
#include	"ParticleTexture.h"
#include	"Platform.h"
#include	"game.h"
#include	"eventmanager.h"
#include	<math.h>
#include	<stdlib.h>
#include	<string.h>

// FUNCTION: 0x004c5b60 init:ZERO_FVECTOR
// FUNCTION: 0x004c5b80 init:ID_FMATRIX
static float	sNoise[32 * 32]; static bool sNoiseBuilt = false;	// 0082a358, 0082b398: defined first (group order; placement evidence)
// Each frame's corners (u0, v0, u1, v1) in a texture holding 16, 8, 4, 2 or 1 frames across.
static float	sFrameUVs[5][16][4];			// 00829e58
static BOOL		sFrameUVsBuilt = FALSE;					// 0082b39c

//*********************************************************************************
// the drawing builds it once (004c5d63)
void	BuildFrameUVs()	// FUNCTION: 0x004c5c50
{
	float	step;
	SINT	across, shift;
	for (SINT size = 0; size < 5; size++)
	{
		for (SINT n = 0; n < 16; n++)
		{
			switch (size)
			{
			case 0:	across = 16;	shift = 4;	step = 1.f / 16;	break;
			case 1:	across = 8;	shift = 3;	step = 1.f / 8;	break;
			case 2:	across = 4;	shift = 2;	step = 1.f / 4;	break;
			case 3:	across = 2;	shift = 1;	step = 1.f / 2;	break;
			case 4:	across = 1;	shift = 0;	step = 1.f;	break;
			}
			sFrameUVs[size][n][0] = (n & (across - 1)) * step;
			sFrameUVs[size][n][1] = (n >> shift) * step;
			sFrameUVs[size][n][2] = step + sFrameUVs[size][n][0];
			sFrameUVs[size][n][3] = step + sFrameUVs[size][n][1];
		}
	}
	sFrameUVsBuilt = TRUE;
}

// A vertex of a particle texture's buffer (0x18 bytes; name ours).
struct SParticleVertex
{
	FVector3	mPos;
	DWORD		mColour;
	float		mU, mV;
};

// The colour range's colour at f (0 born .. 1 dead), each channel 0..255: through the transition colour if it has
// one, else from the start colour to the end colour, else the start colour (as CPDTrail::GetPointColour, 004c8060).
static inline void	RangeColour(CPDColourRange *cr, float f, CParticle *p, UBYTE &r, UBYTE &g, UBYTE &b)
{
	if (cr->mUseTransition)
	{
		if (f < cr->mTransitionPoint)
		{
			float	t = f / cr->mTransitionPoint;
			float	u = 1.f - t;
			r = UBYTE((cr->mTransitionRed.GetValue(t, p) * t + cr->mStartRed.GetValue(t, p) * u) * 255.f);
			g = UBYTE((cr->mTransitionGreen.GetValue(t, p) * t + cr->mStartGreen.GetValue(t, p) * u) * 255.f);
			b = UBYTE((cr->mTransitionBlue.GetValue(t, p) * t + cr->mStartBlue.GetValue(t, p) * u) * 255.f);
		}
		else
		{
			float	t = (f - cr->mTransitionPoint) / (1.f - cr->mTransitionPoint);
			float	u = 1.f - t;
			r = UBYTE((cr->mTransitionRed.GetValue(t, p) * u + cr->mEndRed.GetValue(t, p) * t) * 255.f);
			g = UBYTE((cr->mTransitionGreen.GetValue(t, p) * u + cr->mEndGreen.GetValue(t, p) * t) * 255.f);
			b = UBYTE((cr->mTransitionBlue.GetValue(t, p) * u + cr->mEndBlue.GetValue(t, p) * t) * 255.f);
		}
	}
	else if (cr->mUseEnd)
	{
		float	u = 1.f - f;
		r = UBYTE((cr->mStartRed.GetValue(f, p) * u + cr->mEndRed.GetValue(f, p) * f) * 255.f);
		g = UBYTE((cr->mStartGreen.GetValue(f, p) * u + cr->mEndGreen.GetValue(f, p) * f) * 255.f);
		b = UBYTE((cr->mStartBlue.GetValue(f, p) * u + cr->mEndBlue.GetValue(f, p) * f) * 255.f);
	}
	else
	{
		r = UBYTE(cr->mStartRed.GetValue(f, p) * 255.f);
		g = UBYTE(cr->mStartGreen.GetValue(f, p) * 255.f);
		b = UBYTE(cr->mStartBlue.GetValue(f, p) * 255.f);
	}
}

// STAND-IN (frame slots, see RenderSprites): RangeColour for RenderSprites' two calls, with t declared once for both
// transition halves. Each expansion's two t's are then one symbol that C2's stack packer (0x1074b617) counts at 12
// references, ahead of g's 7, so b and the t's share a slot and g shares the u's, as in retail (frame slot 0x18: the
// second call's f, the t's and b, 004c64fd, 004c652d, 004c6809, 004c6abc; slot 0x1c: the u's and g, 004c6540,
// 004c66e2, 004c680d, 004c6ab7). The second half's fraction goes through d, a copy consumed by t: the expansion keeps
// RangeColour's five locals, which keeps every later symbol id (the facing normal's and cases 1 and 2's term orders;
// without d those two cases add their X terms last). RangeColour itself keeps the separate t's: the same change there
// also matches RenderSprites and CPDTrail::GetPointColour but moves CPDPMesh::Unknown11's bytes.
static inline void	SpriteRangeColour(CPDColourRange *cr, float f, CParticle *p, UBYTE &r, UBYTE &g, UBYTE &b)
{
	if (cr->mUseTransition)
	{
		float	t;
		if (f < cr->mTransitionPoint)
		{
			t = f / cr->mTransitionPoint;
			float	u = 1.f - t;
			r = UBYTE((cr->mTransitionRed.GetValue(t, p) * t + cr->mStartRed.GetValue(t, p) * u) * 255.f);
			g = UBYTE((cr->mTransitionGreen.GetValue(t, p) * t + cr->mStartGreen.GetValue(t, p) * u) * 255.f);
			b = UBYTE((cr->mTransitionBlue.GetValue(t, p) * t + cr->mStartBlue.GetValue(t, p) * u) * 255.f);
		}
		else
		{
			float	d = (f - cr->mTransitionPoint) / (1.f - cr->mTransitionPoint);
			t = d;
			float	u = 1.f - t;
			r = UBYTE((cr->mTransitionRed.GetValue(t, p) * u + cr->mEndRed.GetValue(t, p) * t) * 255.f);
			g = UBYTE((cr->mTransitionGreen.GetValue(t, p) * u + cr->mEndGreen.GetValue(t, p) * t) * 255.f);
			b = UBYTE((cr->mTransitionBlue.GetValue(t, p) * u + cr->mEndBlue.GetValue(t, p) * t) * 255.f);
		}
	}
	else if (cr->mUseEnd)
	{
		float	u = 1.f - f;
		r = UBYTE((cr->mStartRed.GetValue(f, p) * u + cr->mEndRed.GetValue(f, p) * f) * 255.f);
		g = UBYTE((cr->mStartGreen.GetValue(f, p) * u + cr->mEndGreen.GetValue(f, p) * f) * 255.f);
		b = UBYTE((cr->mStartBlue.GetValue(f, p) * u + cr->mEndBlue.GetValue(f, p) * f) * 255.f);
	}
	else
	{
		r = UBYTE(cr->mStartRed.GetValue(f, p) * 255.f);
		g = UBYTE(cr->mStartGreen.GetValue(f, p) * 255.f);
		b = UBYTE(cr->mStartBlue.GetValue(f, p) * 255.f);
	}
}


// The particle's frame of an animated texture: its corners in the table of frame corners.
// STAND-IN (inline budget): see RenderSprites. The block is written as a helper, which moves its own size out of the
// function and into one inline expansion.
static inline void	SetFrameUVs(CPDSimpleSprite *pd, CParticle *p)
{
	float	*uv = sFrameUVs[pd->mTextureSize][SINT(p->mFrame - 0.5f) + pd->mTextureNumber];
	p->mU0 = uv[0];
	p->mV0 = uv[1];
	p->mU1 = uv[2];
	p->mV1 = uv[3];
}

// STAND-IN (spellings, see RenderSprites): file-local readers of the drawing's matrices, reached through static_cast
// as CCylinder's FOrderVector and CWeapon::SpawnRound's FSpawnOri are. They add no member and compute what maths.h's
// operators compute.
// - ColumnX: the X column with the constructor's arguments read straight from the matrix. maths.h's GetColumnX
//   passes them through float locals (its out-of-line copy 004aa3f0 and Transpose need them), which sends them
//   through the FPU where retail passes them with integer moves (004c6d74..004c6d8b, 004c6e85..004c6e90).
// - ColumnY: the camera's up column with Row[2].Y in parentheses. The no-code tuple lengthens the column copy's path
//   in the prologue's scheduling region by one, so the copy's first load goes ahead of the frame number's
//   (004c5da6, 004c5dad), as in retail; maths.h's GetColumnY loads the frame number first.
// - ScaledColumnX/ScaledColumnZ: a column times a scale, built from the products. Retail reads each column element
//   once and multiplies it at its push (004c70c2..004c7164, 004c71a3..004c71d1): the products are the arguments of
//   the one constructor it calls. FVector(column) * -1.f stores the column first and multiplies the stored copy
//   (two levels of copies, which the global optimizer forwards only one level). ScaledColumnZ's products are in
//   parentheses: each no-code tuple lengthens its argument's path by one cycle, so the copy of the X axis into
//   mAxisX, which fills the idle cycles, keeps its stores of Y, Z and Pad behind the pushes as retail does (004c713f,
//   004c7159, 004c7167).
// - MulRP: FMatrix * FVector for the drawn position. Row 0's X factor is converted to double: the product's factors
//   become conversion temporaries, whose rank key puts it between Y's and Z's, so row 0 adds Y, X, Z as retail does
//   (004c610d..004c6123) with the same instructions and no scheduler node. Y's and Z's products are in parentheses:
//   no-code tuples that keep the later common-subexpression temporaries' numbering, which keys rows 1 and 2 and case
//   1's and case 2's products in retail's order.
struct FSpriteMatrix : public FMatrix
{
	FVector	ColumnX() const { return FVector(Row[0].X, Row[1].X, Row[2].X); }
	FVector	ColumnY() const { return FVector(Row[0].Y, Row[1].Y, (Row[2].Y)); }
	FVector	ScaledColumnX(float f) const { return FVector(Row[0].X * f, Row[1].X * f, Row[2].X * f); }
	FVector	ScaledColumnZ(float f) const { return FVector((Row[0].Z * f), (Row[1].Z * f), (Row[2].Z * f)); }
	FVector	MulRP(const FVector &v) const
	{
		return FVector((double)Row[0].X * v.X + (Row[0].Y * v.Y) + (Row[0].Z * v.Z),
					   (Row[1].X * v.X) + (Row[1].Y * v.Y) + (Row[1].Z * v.Z),
					   (Row[2].X * v.X) + (Row[2].Y * v.Y) + (Row[2].Z * v.Z));
	}
};
#define SPRITE_MATRIX(m)	static_cast<const FSpriteMatrix &>(m)

// STAND-IN (ids), see RenderSprites: case 2's particle orientation passes through 16 chained reference copies. Each
// copy is consumed by the next and the last is returned, so it emits no code; each takes one inline-time symbol id
// (forced inlining spends no inline budget). FMatrix::operator*'s matrix parameter then takes the fresh id 0x6f7
// where it took the recycled 0x40c (C2's id log, an id simulator): m.Row[0].X is read through that symbol (the
// other elements through address temporaries), and its key, 0xe007 for 0x8007, puts the X term first in rows 1 and 2
// of the X column (004c6fc4..004c6fe3, 004c7028..004c7047), as retail adds them, with maths.h's own product. The
// parentheses that did that before were two no-code tuples ahead of case 2's first scheduling-region cut, which then
// fell two nodes early (the push at 004c7086). The later ids move by 16 with it, which the drawn position, case 1 and
// the visibility test keep (8 or 24 copies flip the position's Y sum or more).
static __forceinline const FMatrix	&OriIds(const FMatrix &m)
{
	const FMatrix	&c0 = m, &c1 = c0, &c2 = c1, &c3 = c2, &c4 = c3, &c5 = c4, &c6 = c5, &c7 = c6;
	const FMatrix	&c8 = c7, &c9 = c8, &c10 = c9, &c11 = c10, &c12 = c11, &c13 = c12, &c14 = c13, &c15 = c14;
	return c15;
}

// STAND-IN (spelling, see RenderSprites): the visibility test's squared distance with Y's and Z's squares in
// parentheses and X's bare. Retail adds Z's and Y's squares first and X's last (004c5ff3..004c6005); maths.h's
// MagnitudeSq, all three in parentheses, orders them X, Z, Y by the products' rank keys. The parentheses are no-code
// tuples; with FSpriteMatrix::ColumnY's pair they keep maths.h's count, which numbers the later common-subexpression
// temporaries that key the drawn position's and the quad cases' products.
// MagnitudeSqXZ, for the 2D size's squared distance, has X's and Z's squares in parentheses: the same code as
// maths.h's MagnitudeSq (X, Z, Y; 004c60a4..004c60b2) with one tuple fewer, which gives back the pair that the
// visibility test's r * r adds (see RenderSprites), so the later temporaries keep their numbering.
struct FSpriteVector : public FVector
{
	float	MagnitudeSq() const { return X * X + (Y * Y) + (Z * Z); }
	float	MagnitudeSqXZ() const { return (X * X) + Y * Y + (Z * Z); }
};

// STAND-IN (ids), see RenderSprites: the frame number passes through 142 chained integer copies. Each copy is consumed
// by the next and the last is returned, so it emits no code; each takes one inline-time symbol id, and forced inlining
// spends no inline budget.
static __forceinline SINT	FrameIds(SINT x)
{
	SINT	c0 = x, c1 = c0, c2 = c1, c3 = c2, c4 = c3, c5 = c4, c6 = c5, c7 = c6;
	SINT	c8 = c7, c9 = c8, c10 = c9, c11 = c10, c12 = c11, c13 = c12, c14 = c13, c15 = c14;
	SINT	c16 = c15, c17 = c16, c18 = c17, c19 = c18, c20 = c19, c21 = c20, c22 = c21, c23 = c22;
	SINT	c24 = c23, c25 = c24, c26 = c25, c27 = c26, c28 = c27, c29 = c28, c30 = c29, c31 = c30;
	SINT	c32 = c31, c33 = c32, c34 = c33, c35 = c34, c36 = c35, c37 = c36, c38 = c37, c39 = c38;
	SINT	c40 = c39, c41 = c40, c42 = c41, c43 = c42, c44 = c43, c45 = c44, c46 = c45, c47 = c46;
	SINT	c48 = c47, c49 = c48, c50 = c49, c51 = c50, c52 = c51, c53 = c52, c54 = c53, c55 = c54;
	SINT	c56 = c55, c57 = c56, c58 = c57, c59 = c58, c60 = c59, c61 = c60, c62 = c61, c63 = c62;
	SINT	c64 = c63, c65 = c64, c66 = c65, c67 = c66, c68 = c67, c69 = c68, c70 = c69, c71 = c70;
	SINT	c72 = c71, c73 = c72, c74 = c73, c75 = c74, c76 = c75, c77 = c76, c78 = c77, c79 = c78;
	SINT	c80 = c79, c81 = c80, c82 = c81, c83 = c82, c84 = c83, c85 = c84, c86 = c85, c87 = c86;
	SINT	c88 = c87, c89 = c88, c90 = c89, c91 = c90, c92 = c91, c93 = c92, c94 = c93, c95 = c94;
	SINT	c96 = c95, c97 = c96, c98 = c97, c99 = c98, c100 = c99, c101 = c100, c102 = c101, c103 = c102;
	SINT	c104 = c103, c105 = c104, c106 = c105, c107 = c106, c108 = c107, c109 = c108, c110 = c109, c111 = c110;
	SINT	c112 = c111, c113 = c112, c114 = c113, c115 = c114, c116 = c115, c117 = c116, c118 = c117, c119 = c118;
	SINT	c120 = c119, c121 = c120, c122 = c121, c123 = c122, c124 = c123, c125 = c124, c126 = c125, c127 = c126;
	SINT	c128 = c127, c129 = c128, c130 = c129, c131 = c130, c132 = c131, c133 = c132, c134 = c133, c135 = c134;
	SINT	c136 = c135, c137 = c136, c138 = c137, c139 = c138, c140 = c139, c141 = c140;
	return c141;
}

//*********************************************************************************
// The sprites, oldest first. Each particle's size on screen is measured every 16th frame (the frames staggered by
// the particle's own number; a flag per viewpoint keeps the answer), and one too small to see is skipped.
// The particle loop nests its body under the frame-of-reference age test instead of a second `continue`: the latch
// then forms its own split region (CInfluenceMap::Build's thing loops), which moves the spill and reload code
// towards retail's. The life test compares as a float: retail converts mLife once, tests it against 0 and divides by
// it (004c5de4..004c5e2a).
// The camera's back vector is the negated Z column written out (Stuart's form for the sun sprite, DXEngine.cpp): retail
// negates each element as it loads it (004c5e77..004c5ea0), where -cori.GetColumnZ() keeps the column in a temporary.
// The fade curve subtracts 30 from alpha in place: retail's `sub eax, 0x1e` (004c6b15). Blend mode 0 scales the
// channels into locals as mode 2 does: retail shifts each one down and the red one up again (004c6bdf..004c6be5)
// where one expression folds the pair into a mask.
// Inline budget (measured): the out-of-line calls are retail's, in order. Case 1 and the facing normal read the
// columns through accessors and negate them: retail calls both constructors of each axis (004c6d93..
// 004c6f45). Case 2's one call per axis (004c70fc, 004c716a, 004c71d4) is a nested constructor, so the else arm's
// FVector(0, 0, -1.f) (004c71ed..004c7205) and case 3's constants stay top-level sites that a falling budget expands.
// STAND-IN (spellings): the up and X columns, case 2's scaled columns and the drawn position's matrix product go
// through FSpriteMatrix above (what each reproduces is listed there).
// STAND-IN (ids): case 2's particle orientation goes through OriIds above (16 consumed reference copies), which gives
// maths.h's matrix product retail's term order without a scheduler node.
// STAND-IN (inline budget): the animated frame's corners go through SetFrameUVs above. Retail's one-level reads (the
// back vector, case 2's columns) inline fewer and smaller sites before case 2's matrix product, and the budget left at
// case 3's constants must stay between 47 and 93; the helper moves the block's size into one expansion, and invlife's
// if/else then fits (alpha's and x's conditional expressions emit the same code as if/else).
// STAND-IN (ids): FrameIds above, 142 consumed integer copies of the frame number. The field symbols the global
// optimizer folds late (pass 6 for arguments, 9 for this) number from a counter shared with the earlier symbols:
// retail's facing normal multiplies forori's terms first (004c6843..004c69d5) because its matrix's fields are
// numbered from 0x800 up (wrapped keys below forori's), and the copies put them there (the 27 products of that block
// and the drawn position's and case 1's sums follow; 138..146 copies keep the other blocks).
// STAND-IN (spelling): the visibility test's squared distance goes through FSpriteVector above, and its r * r is in
// parentheses: the no-code tuple waits for the multiply and takes the idle cycle after Y's square, so the reload of
// this fills the one after X's square (004c6001), as in retail. The 2D size's squared distance gives the tuple back
// (FSpriteVector::MagnitudeSqXZ).
// STAND-IN (frame slots): the colour comes from SpriteRangeColour above, RangeColour with one t for both transition
// halves. The stack packer takes its symbols by size, then by counted references: with each expansion's two t's one
// symbol (12 references) the t's come before g (7) and start the class that the second call's f and b join, and g
// then starts the class that the u's join, as retail keeps them (b and the t's at 0x18, g and the u's at 0x1c). With
// four t's of 6 references, g came first and the t's joined g. The else assigns b first (r = g = b = 255, as
// CPDTrail::GetPointColour), which stores the 0x18 slot before the 0x1c slot (004c6809, 004c680d).
void	CPDSimpleSprite::RenderSprites(bool a)	// FUNCTION: 0x004c5d50
{
	InitNoiseTable();
	CParticle	*p = mOldestParticle;
	if (!sFrameUVsBuilt)
		BuildFrameUVs();

	FVector	campos = ENGINE.GetCamera()->GetPos();
	FMatrix	cori = ENGINE.GetCamera()->GetOrientation();
	FVector	up = SPRITE_MATRIX(cori).ColumnY();	// STAND-IN
	SINT	frame = FrameIds(PARTICLE_MANAGER.mFrame & 15);	// STAND-IN (ids)
	SINT	visflag = ENGINE.mCurrentViewpoint ? 0x20 : 0x10;
	SINT	testflag = ENGINE.mCurrentViewpoint ? 0x80 : 0x40;
	float	invlife;
	if (mLife == 0.f)
		invlife = 0;
	else
		invlife = 1.f / mLife;
	SINT	fadescale = SINT(invlife * 65536.f);
	FVector	right = cori.GetColumnX();
	FVector	back(-cori.Row[0].Z, -cori.Row[1].Z, -cori.Row[2].Z);

	for (; p; p = p->mPDNext)
	{
		CFoR	*f = p->mFoR;
		BOOL	hasori = f && f->mHasOrientation;
		FVector	forpos;
		if (f)
			forpos = f->mStartPos;

		SINT	vis = 0;
		if ((p->mLife < 0) && (p->mLife != -2.f))
			continue;
		if (!(f && (f->mUnknownAC != -1.f) && (EVENT_MANAGER.GetTime() - 0.09999f > f->mUnknownAC)))
		{

			// the size on screen, remeasured every 16th frame
			SWORD	flags = p->mUnknown7E;
			if (((flags & 15) != frame) && (!(flags & testflag)))
				vis = flags & visflag;
			else
			{
				FVector	pos = p->mPos;
				if (f)
					pos += forpos;
				FVector	d = campos - pos;
				float	r = p->mRadius;
				if (((r * r) / static_cast<const FSpriteVector &>(d).MagnitudeSq() > 9.7656248e-6f) || m2D)	// STAND-IN
				{
					p->mUnknown7E |= visflag;
					vis = 1;
				}
				else
					p->mUnknown7E &= ~visflag;
				p->mUnknown7E &= ~testflag;
			}
			if (!vis)
				continue;

			FMatrix	forori;
			if (f)
				forori = f->mOrientation;

			float	radius = p->mRadius;
			p->mRenderRadius = radius;
			if (m2D)
			{
				FVector	d = campos - p->mPos;
				float	dd = static_cast<const FSpriteVector &>(d).MagnitudeSqXZ();	// STAND-IN
				if (dd > 1.f)
					p->mRenderRadius = (float)sqrt(dd) * radius;
			}

			p->mRenderPos = p->mPos;
			if (f)
			{
				if (hasori)
					p->mRenderPos = SPRITE_MATRIX(forori).MulRP(p->mRenderPos);	// STAND-IN
				p->mRenderPos += forpos;
			}

			if (mAnimType)
				SetFrameUVs(this, p);	// STAND-IN (inline budget)

			// the colour, by age
			UBYTE	cr, cg, cb;
			SINT	r, g, b;
			CPDColourRange	*range = (CPDColourRange *)mColourRange;
			if (range)
			{
				if (mLife == 0)
					SpriteRangeColour(range, 1.f, p, cr, cg, cb);	// STAND-IN (frame slots)
				else
					SpriteRangeColour(range, (mLife - p->mLife) * invlife, p, cr, cg, cb);	// STAND-IN (frame slots)
				r = cr;
				g = cg;
				b = cb;
			}
			else
				r = g = b = 255;

			// a flat sprite is dimmed as it turns edge on to the view
			if (mAxisAligned == 5)
			{
				FMatrix	ori = p->mOrientation;
				if (hasori)
					ori = forori * ori;
				FVector	normal = -ori.GetColumnY();
				float	facing = normal * up;
				if (facing < 0)
					facing = 0;
				p->mRenderRadius = facing * p->mRadius;
				SINT	s = SINT(facing * 255.f);
				r = (r * s) >> 8;
				g = (g * s) >> 8;
				b = (b * s) >> 8;
			}

			// the colour, faded with life, packed for the blend mode
			if (mFadeCol)
			{
				SINT	alpha = (mLife > 0) ? (SINT(p->mLife) * fadescale * 255) >> 16 : 255;
				if (alpha > 30)
				{
					alpha -= 30;
					alpha = 256 - (51076 - alpha * alpha) / 226;
				}
				switch (mBlendMode)
				{
				case 0:
				{
					SINT	rr = (r * alpha) >> 8;
					SINT	gg = (g * alpha) >> 8;
					SINT	bb = (b * alpha) >> 8;
					p->mColour = (((rr << 8) + gg) << 8) + bb;
					break;
				}
				case 1:
					p->mColour = (((((alpha << 8) + r) << 8) + g) << 8) + b;
					break;
				case 2:
				{
					SINT	rr = (alpha * r) >> 8;
					SINT	gg = (alpha * g) >> 8;
					SINT	bb = (alpha * b) >> 8;
					SINT	x = (alpha > 127) ? (255 - alpha) * 2 : alpha * 8;
					if (x > 255)
						x = 255;
					p->mColour = (((((x << 8) + rr) << 8) + gg) << 8) + bb;
					break;
				}
				}
			}
			else
			{
				if (mBlendMode == 1)
					r += 0xff00;
				p->mColour = (((r << 8) + g) << 8) + b;
			}

			// the quad's half axes
			switch (mAxisAligned)
			{
			case 1:		// the particle's own X and Y
				if (hasori)
				{
					FMatrix	ori = forori * p->mOrientation;
					p->mAxisX = -SPRITE_MATRIX(ori).ColumnX();	// STAND-IN
					p->mAxisY = -ori.GetColumnY();
				}
				else
				{
					p->mAxisX = -SPRITE_MATRIX(p->mOrientation).ColumnX();	// STAND-IN
					p->mAxisY = -p->mOrientation.GetColumnY();
				}
				break;

			case 2:		// its X, and its Z down
				if (hasori)
				{
					FMatrix	ori = forori * OriIds(p->mOrientation);	// STAND-IN (ids)
					p->mAxisX = SPRITE_MATRIX(ori).ScaledColumnX(-1.f);	// STAND-IN
					p->mAxisY = SPRITE_MATRIX(ori).ScaledColumnZ(-1.f);	// STAND-IN
				}
				else
				{
					p->mAxisX = SPRITE_MATRIX(p->mOrientation).ScaledColumnX(-1.f);	// STAND-IN
					p->mAxisY = p->mOrientation * FVector(0, 0, -1.f);
				}
				break;

			case 3:		// its Z, and its Y back
				if (hasori)
				{
					FMatrix	ori = forori * p->mOrientation;
					p->mAxisX = ori * FVector(0, 0, 1.f);
					p->mAxisY = ori * FVector(0, -1.f, 0);
				}
				else
				{
					FMatrix	ori = p->mOrientation;
					p->mAxisX = ori * FVector(0, 0, 1.f);
					p->mAxisY = ori * FVector(0, -1.f, 0);
				}
				break;

			case 4:
			case 5:		// along the view, once it is big enough to show
			{
				FVector	d = p->mRenderPos - campos;
				if (d.InvMagnitude() * p->mRadius > 0.0625f)
				{
					FVector	t = up;
					t.Scale(d * t);
					p->mAxisX = t - d;
					p->mAxisX.NormaliseIfLong();
					p->mAxisY = p->mAxisX ^ up;
					break;
				}
				p->mAxisX = right;
				p->mAxisY = back;
				break;
			}

			case 6:		// a streak along the particle's Y, its length by age
			{
				float	length = mLength.GetValueByAge(p->mLife, (float)mLife, p);
				p->mAxisX = (p->mOrientation * FVector(0, 1.f, 0)) * length;
				if (hasori)
					p->mAxisX = forori * p->mAxisX;
				p->mAxisY = p->mAxisX ^ up;
				float	m = p->mAxisY.Magnitude();
				if (m > 0.001f)
					p->mAxisY /= m;
				p->mRenderPos += p->mAxisX * 0.5f;
				p->mAxisX /= p->mRenderRadius;
				break;
			}

			default:	// facing the view
				p->mAxisX = right;
				p->mAxisY = back;
				break;
			}

			// the quad
			SParticleVertex	*v;
			SINT	first;
			mTexture->GetVertices(4, (void **)&v, &first);
			p->mAxisX.Scale(p->mRenderRadius);
			p->mAxisY.Scale(p->mRenderRadius);
			p->mAxisX.Pad = p->mAxisY.Pad = 1.f;
			v->mPos = p->mRenderPos - p->mAxisX + p->mAxisY;
			v->mU = p->mU0;
			v->mV = p->mV0;
			v->mColour = p->mColour;
			v++;
			v->mPos = p->mRenderPos + p->mAxisX + p->mAxisY;
			v->mU = p->mU1;
			v->mV = p->mV0;
			v->mColour = p->mColour;
			v++;
			v->mPos = p->mRenderPos - p->mAxisX - p->mAxisY;
			v->mU = p->mU0;
			v->mV = p->mV1;
			v->mColour = p->mColour;
			v++;
			v->mPos = p->mRenderPos + p->mAxisX - p->mAxisY;
			v->mU = p->mU1;
			v->mV = p->mV1;
			v->mColour = p->mColour;
			v++;
			UWORD	*idx = mTexture->GetIndices(6);
			idx[0] = first + 1;
			idx[1] = first;
			idx[2] = first + 2;
			idx[3] = first + 1;
			idx[4] = first + 2;
			idx[5] = first + 3;
		}
	}
}

// The vector inlines the drawing calls out of line (maths.h's PDSIMPLESPRITE_CPP view).
// FUNCTION: 0x004c78b0 ?Scale@FVector@@QAEXM@Z
// FUNCTION: 0x004c78d0 ?InvMagnitude@FVector@@QBEMXZ
// FUNCTION: 0x004c7900 ?NormaliseIfLong@FVector@@QAEXXZ
// FUNCTION: 0x004c7d90 ??4FVector3@@QAEAAV0@ABVFVector@@@Z

// FUNCTION: 0x004c7950 ?GetValueByAge@CFloatRef@@QAEMMMPAVCParticle@@@Z

// A wrapping 32 x 32 table of fractal noise in -1..1 (diamond-square midpoints, then normalised).
// sNoise (0082a358) and sNoiseBuilt (0082b398) are defined above, before sFrameUVs
//
#define NOISE(x, y)	(sNoise[(((y) & 31) << 5) + ((x) & 31)])

// a random offset of up to scale / 4096 of a unit, either way
static inline SINT	NoiseOffset(SINT scale) { return ((rand() / 3 - 0x1000) * scale) >> 12; }
#define NOISE_OFFSET(scale)	(NoiseOffset(scale) / 4096.f)

//*********************************************************************************
// half and scale are set inside the inner loop and hoisted by the optimizer (004c7de7): set before the loops, they let
// VC6 rewrite x + half as (x + step) + (half - step) and y + step from y + half, where retail adds each to the counter
// (004c7eba, 004c7e04) and keeps x + step and y + step as the next counters (004c7f61, 004c7f6e). half is halved
// unsigned (shr) while step's own shift is signed (sar, jg at 004c7f7f). The table is cleared through x and y (VC6
// makes the loop the rep stos at 004c7dd7): their symbols then come before step's, which puts step first in the counter
// adds (lea edi,[ecx+ebx] at 004c7e04, lea eax,[edx+esi] at 004c7ea9). NOISE's parentheses make each read its own
// expression, a no-code x87 node: NOISE(x, y + step)'s and NOISE(x, y)'s index temporaries then share a rank class, so
// the first sum keeps source order (004c7e61), and the node after the second sum's first load lets the index reload go
// before its first add (004c7ed7).
// Each midpoint adds NOISE_OFFSET, the random offset in units: the macro's parentheses (as NOISE's) make the offset its
// own expression. As a CSE temporary it puts NOISE(x + step, y)'s index temporary in the rank class that adds it before
// NOISE(x, y + step) and NOISE(x, y) (004c7edb) and loads it before NOISE(x, y) (004c7f31); as an x87 node it moves
// the 81-node scheduling cut between the third sum's loads, so its offset shift and store go between them (004c7f38)
// and its store index add after them (004c7f46).
void	CPDSimpleSprite::InitNoiseTable()	// FUNCTION: 0x004c7db0
{
	if (sNoiseBuilt)
		return;
	sNoiseBuilt = true;
	SINT	x, y;
	for (y = 0; y < 32; y++)
		for (x = 0; x < 32; x++)
			sNoise[(y << 5) + x] = 0;

	SINT	step;
	for (step = 32; step > 1; step >>= 1)
	{
		for (y = 0; y < 32; y += step)
			for (x = 0; x < 32; x += step)
			{
				SINT	half = (UINT)step / 2;
				SINT	scale = half * 80;
				NOISE(x, y + half) = ((NOISE(x, y + step) + NOISE(x, y)) * 0.5f) + NOISE_OFFSET(scale);
				NOISE(x + half, y + half) = ((NOISE(x + step, y + step) + NOISE(x + step, y) + NOISE(x, y + step) +
											  NOISE(x, y)) * 0.25f) + NOISE_OFFSET(scale);
				NOISE(x + half, y) = ((NOISE(x + step, y) + NOISE(x, y)) * 0.5f) + NOISE_OFFSET(scale);
			}
	}

	float	max = 0;
	for (y = 0; y < 32; y += step)
		for (x = 0; x < 32; x += step)
		{
			float	v = (float)fabs(NOISE(x, y));
			if (v > max)
				max = v;
		}
	if (max > 0)
	{
		float	scale = 1.f / max;
		for (y = 0; y < 32; y += step)
			for (x = 0; x < 32; x += step)
				NOISE(x, y) *= scale;
	}
}

//*********************************************************************************
void	CPDSimpleSprite::Unknown23(bool a)	// FUNCTION: 0x004c8040
{
	InitNoiseTable();
	if (mTexture)
		RenderSprites(a);
}

//*********************************************************************************
// A trail point's colour: the colour range along the trail (through its transition colour if it has one), faded
// over the trail's second half (or by the particle's life), packed for the blend mode.
// The switch assigns one result: this keeps the raw channels separate from the scaled channels, as at 004c841c,
// and masks them again in the unscaled blend-mode arm (004c84d2).
DWORD	CPDTrail::GetPointColour(SINT i, float alpha, CParticle *p)	// FUNCTION: 0x004c8060
{
	UBYTE	r, g, b;
	CPDColourRange	*cr = (CPDColourRange *)mColourRange;
	if (cr)
		RangeColour(cr, (float)i / p->mUnknown80, p, r, g, b);
	else
		r = g = b = 255;

	SINT	a;
	if (!mType)
	{
		SINT	half = p->mUnknown80 / 2;
		if (i >= half)
		{
			a = 255 + (half - i) * 285 / half;
			if (a < 0)
				a = 0;
			a = (SINT(alpha * 255.f) * a) >> 8;
		}
		else
			a = 255;
	}
	else if ((p->mLife >= -1.f) && ((float)mFadePoint >= p->mLife))
		a = SINT(p->GetLife() / (float)mFadePoint * 255.f);
	else
		a = 255;

	SINT	rr = (r * a) / 256;
	SINT	gg = (g * a) / 256;
	SINT	bb = (b * a) / 256;
	DWORD	col;
	switch (mBlendMode)
	{
	case 0:
		col = (((rr << 8) + gg) << 8) + bb;
		break;
	case 1:
		col = (((((a << 8) + r) << 8) + g) << 8) + b;
		break;
	case 2:
	{
		SINT	x;
		if (a > 127)
			x = (255 - a) * 2;
		else
			x = a * 8;
		if (x > 255)
			x = 255;
		col = (((((x << 8) + rr) << 8) + gg) << 8) + bb;
		break;
	}
	default:
		col = 0xffffffff;
		break;
	}
	return col;
}

float	TRAIL_DETAIL = 1.f;		// 00630e0c (name ours)

//*********************************************************************************
// A trail: a strip of quads from the newest point back, each edge across the view (or flat), its width tapering
// along the trail or with life. Far trails skip points: the farther, the more (up to every 32nd).
// The newest point has its own copy (`last` is set only once a quad starts): that gives retail's frame and the order of
// the `last + prevoffset` sums. GetFoR() orders the shifted X sum; started declared before prevoffset orders the stores.
void	CPDTrail::Unknown11(CParticle *p)	// FUNCTION: 0x004c8520
{
	if (!mTexture)
		return;
	if (!p->mData)
		return;

	FVector	campos = ENGINE.GetCamera()->GetPos();
	STrailPoint	*points = (STrailPoint *)p->mData;
	FVector	newest = points[(SWORD)p->mUnknown7C].mPos;
	float	dist = (campos - newest).MagnitudeSq();
	float	w = (mWidth > mStartWidth) ? mWidth : mStartWidth;
	float	size = w * w / dist * TRAIL_DETAIL * 102400.f;
	p->mFrame = size;
	if (size < 0.1f)
		return;

	SINT	idx = p->mUnknown7E;
	BOOL	started = FALSE;
	FVector	prevoffset = ZERO_FVECTOR;
	FVector	last;
	float	prevwidth = 0;
	SINT	previ = 0;
	SINT	skip = SINT(1.f / size);
	if (skip > 5)
		skip = 5;
	skip = (1 << skip) - 1;
	for (SINT i = 0; i < p->mUnknown80 - 1; i++)
	{
		STrailPoint	*pt = &points[idx];
		if (!pt->mUnknown24)
		{
			started = FALSE;
			prevoffset = ZERO_FVECTOR;
		}
		else
		{
			if ((!started) || (i == p->mUnknown80 - 2) || (!points[idx ? idx - 1 : p->mUnknown80 - 1].mUnknown24) ||
				(!(skip & idx)))
			{
				FVector	pos = pt->mPos;
				if ((mType || (!i) || (mNumPoints < 4)) && p->GetFoR())
					pos += p->GetFoR()->mStartPos - p->GetFoR()->mPos;

				FVector	off;
				if (!started)
					off = ZERO_FVECTOR;
				else
				{
					FVector	dir = last - pt->mPos;
					FVector	view;
					if (!mFlat)
						view = pt->mPos - campos;
					else
						view = FVector(0, 0, -1.f);
					off = dir ^ view;
					float	m = off.MagnitudeSq();
					if (m > 0)
					{
						float	inv = 1.f / (float)sqrt(m);
						off *= inv;
					}

					float	width = pt->mWidth;
					float	f;
					if (!mWidthWithLife)
						f = (float)i / (float)(p->mUnknown80 - 1);
					else
						f = 1.f - p->mLife / (float)mLife;
					width *= (1.f - f) * mStartWidth + f * mWidth;
					off *= width;

					if ((!mTaperStart) && (prevoffset == ZERO_FVECTOR))
						prevoffset = off;

					SParticleVertex	*v;
					SINT	first;
					mTexture->GetVertices(4, (void **)&v, &first);

					DWORD	col = GetPointColour(previ, prevwidth, p);
					v->mColour = col;
					v->mPos = last + prevoffset;
					v->mU = mU0;
					v->mV = mV0;
					v++;
					v->mColour = col;
					v->mPos = last - prevoffset;
					v->mU = mU0;
					v->mV = mV1;
					v++;

					col = GetPointColour(i, pt->mWidth, p);
					v->mColour = col;
					v->mPos = pos + off;
					v->mU = mU1;
					v->mV = mV0;
					v++;
					v->mColour = col;
					v->mPos = pos - off;
					v->mU = mU1;
					v->mV = mV1;
					v++;

					mTexture->AddTriangle(first + 1, first, first + 2);
					mTexture->AddTriangle(first + 1, first + 2, first + 3);
				}
				last = pos;
				prevoffset = off;
				started = TRUE;
				prevwidth = pt->mWidth;
				previ = i;
			}
		}
		idx--;
		if (idx < 0)
			idx = p->mUnknown80 - 1;
	}

	// Retail calls the case-insensitive CRT entry at 00568390 after this case-sensitive quick filter.
	if ((mName[5] == 'T') && (!stricmp(mName, "Blue Trail Large")))
		CDXParticleTexture::RenderAll();
}

// Defined at the end of the file, as in retail (004cab30..004cac80 follow the particle meshes).
DWORD	LerpColour(DWORD a, DWORD b, float t);
float	ClampedRatio(float a, float lo, float hi);
float	SampleNoise(float u, float v);


//*********************************************************************************
// Draw the centred part pose in the particle's frame. The frame transform and its consumer stay together:
// retail calls the rotated position's constructor (004c8f41) and copies the matrix product from its returned
// pointer (004c8f80), before passing this completed pose to RenderPart.
static inline void RenderParticleMeshPart(CParticle *p, CMeshPart *part, FVector &rpos, FMatrix &ori)
{
	if (p->mFoR)
	{
		if (p->mFoR->mHasOrientation)
		{
			rpos = p->mFoR->mOrientation * rpos;
			ori = ori * p->mFoR->mOrientation;
		}
		rpos += p->mFoR->mStartPos;
	}
	PARTICLE_MANAGER.SetRenderStates();
	CMeshRenderer::SetRenderAlpha(255);
	CMeshRenderer::RenderPart(&rpos, &ori, part, NULL, NULL, 0, 0);
}

// A mesh particle: select its frame part, skip a projection smaller than two pixels, and optionally centre
// its local pose on the part's box before drawing it in the particle frame.
void	CPDMesh::Unknown11(CParticle *p)	// FUNCTION: 0x004c8ce0
{
	CMesh	*mesh = p->mMesh ? p->mMesh : mMesh;
	if (!mesh)
		return;
	SINT	frame = p->mUnknown7E;
	if (frame >= mesh->mNumParts)
		frame = 0;
	CMeshPart	*part = mesh->GetPart(frame);
	if (!part)
		return;

	FVector	pos = p->mPos;
	if (p->GetFoR())
		pos += p->GetFoR()->mPos;
	FVector	d = ENGINE.GetCamera()->GetPos() - pos;
	if (p->mRadius * p->mRadius / d.MagnitudeSq() * 102400.f < 2.f)
		return;

	FVector	rpos = p->mPos;
	FMatrix	ori = p->mOrientation;
	if (mAutoCentre)
	{
		FVector	centre = part->mBoundingBox->mOrigin;
		rpos = rpos - ori * centre;
	}
	RenderParticleMeshPart(p, part, rpos, ori);
}

//*********************************************************************************
// CPDPMesh (the procedural mesh particle): a self-contained block.

// A fraction a / b (name ours).
// STAND-IN (entering main only with a complete match). Plain form: LifeColour's second half
// divides in place (`float d = (f - ...) / (1.f - ...);`). Effect: the helper's expansion adds ids before the frame of
// reference product, which the life fraction's three stores (see Unknown11) otherwise shift: without it six of the
// product's sums take other orders (anchored 31). Original: unknown. Replace it when a form that gives life its
// packer references leaves those ids alone.
static inline float Frac(float a, float b)
{
	return a / b;
}

// The colour range's colour at f (name ours), as the particle's own colour. Retail evaluates the later colour first here
// (004c921b), where the mesh's rows evaluate the earlier (004c9bf6). Past the transition point 1 - t is written at each
// use: those symbols put the frame of reference product's rows in retail's order (004c952a..004c9678), where reusing f
// did not. Its flag tests are written `!= FALSE`, four units of the inline budget that Unknown11 needs (see FewerPoints).
// t is declared once for both transition halves, as in the mesh rows' colour (MeshRangeColour): one symbol, which the
// stack packer places in retail's slot 0 class with the life fraction (its stores 004c9147, 004c9220;
// 2026-10-10).
// STAND-IN (entering main only with a complete match). Plain form: `t = (f - ...) / (1.f - ...)`
// with no d. Effect: d is a copy consumed by t, so the expansion keeps the three locals the separate t's gave it, and
// the FoR product's inline symbols keep their ids: without d the product's sum at 004c964b takes its terms in another
// order (one tree lost, anchored 10). Original: unknown (another symbol may have held the count). Replace it when a
// form with real uses keeps the count.
static inline void LifeColour(CPDColourRange *cr, float f, CParticle *p, UBYTE &r, UBYTE &g, UBYTE &b)
{
	if (cr->mUseTransition != FALSE)
	{
		float	t;
		if (f < cr->mTransitionPoint)
		{
			t = f / cr->mTransitionPoint;
			float	u = 1.f - t;
			r = UBYTE((cr->mTransitionRed.GetValue(t, p) * t + cr->mStartRed.GetValue(t, p) * u) * 255.f);
			g = UBYTE((cr->mTransitionGreen.GetValue(t, p) * t + cr->mStartGreen.GetValue(t, p) * u) * 255.f);
			b = UBYTE((cr->mTransitionBlue.GetValue(t, p) * t + cr->mStartBlue.GetValue(t, p) * u) * 255.f);
		}
		else
		{
			float	d = Frac(f - cr->mTransitionPoint, 1.f - cr->mTransitionPoint);
			t = d;
			r = UBYTE((cr->mEndRed.GetValue(t, p) * t + cr->mTransitionRed.GetValue(t, p) * (1.f - t)) * 255.f);
			g = UBYTE((cr->mEndGreen.GetValue(t, p) * t + cr->mTransitionGreen.GetValue(t, p) * (1.f - t)) * 255.f);
			b = UBYTE((cr->mEndBlue.GetValue(t, p) * t + cr->mTransitionBlue.GetValue(t, p) * (1.f - t)) * 255.f);
		}
	}
	else if (cr->mUseEnd != FALSE)
	{
		r = UBYTE((cr->mEndRed.GetValue(f, p) * f + cr->mStartRed.GetValue(f, p) * (1.f - f)) * 255.f);
		g = UBYTE((cr->mEndGreen.GetValue(f, p) * f + cr->mStartGreen.GetValue(f, p) * (1.f - f)) * 255.f);
		b = UBYTE((cr->mEndBlue.GetValue(f, p) * f + cr->mStartBlue.GetValue(f, p) * (1.f - f)) * 255.f);
	}
	else
	{
		r = UBYTE(cr->mStartRed.GetValue(f, p) * 255.f);
		g = UBYTE(cr->mStartGreen.GetValue(f, p) * 255.f);
		b = UBYTE(cr->mStartBlue.GetValue(f, p) * 255.f);
	}
}

// The colour range's colour at f for the mesh rows (name ours): RangeColour's evaluation order, with t declared once for
// both transition halves, as the colour range in RenderSprites has it (see SpriteRangeColour). Each expansion's t is then
// one symbol, which C2's stack packer (0x1074b617) places as retail's frame has it: the sphere's in slot 4 with the
// sphere's f (its stores 004c9b28, 004c9bfe), the cylinder's in slot 8 with the cylinder's f (004ca5d0, 004ca6a6).
// With RangeColour's two t's per expansion the frame reproduced 34 of retail's 41 slot classes; with this helper and
// LifeColour's single t, 37.
static inline void	MeshRangeColour(CPDColourRange *cr, float f, CParticle *p, UBYTE &r, UBYTE &g, UBYTE &b)
{
	if (cr->mUseTransition)
	{
		float	t;
		if (f < cr->mTransitionPoint)
		{
			t = f / cr->mTransitionPoint;
			float	u = 1.f - t;
			r = UBYTE((cr->mTransitionRed.GetValue(t, p) * t + cr->mStartRed.GetValue(t, p) * u) * 255.f);
			g = UBYTE((cr->mTransitionGreen.GetValue(t, p) * t + cr->mStartGreen.GetValue(t, p) * u) * 255.f);
			b = UBYTE((cr->mTransitionBlue.GetValue(t, p) * t + cr->mStartBlue.GetValue(t, p) * u) * 255.f);
		}
		else
		{
			t = (f - cr->mTransitionPoint) / (1.f - cr->mTransitionPoint);
			float	u = 1.f - t;
			r = UBYTE((cr->mTransitionRed.GetValue(t, p) * u + cr->mEndRed.GetValue(t, p) * t) * 255.f);
			g = UBYTE((cr->mTransitionGreen.GetValue(t, p) * u + cr->mEndGreen.GetValue(t, p) * t) * 255.f);
			b = UBYTE((cr->mTransitionBlue.GetValue(t, p) * u + cr->mEndBlue.GetValue(t, p) * t) * 255.f);
		}
	}
	else if (cr->mUseEnd)
	{
		float	u = 1.f - f;
		r = UBYTE((cr->mStartRed.GetValue(f, p) * u + cr->mEndRed.GetValue(f, p) * f) * 255.f);
		g = UBYTE((cr->mStartGreen.GetValue(f, p) * u + cr->mEndGreen.GetValue(f, p) * f) * 255.f);
		b = UBYTE((cr->mStartBlue.GetValue(f, p) * u + cr->mEndBlue.GetValue(f, p) * f) * 255.f);
	}
	else
	{
		r = UBYTE(cr->mStartRed.GetValue(f, p) * 255.f);
		g = UBYTE(cr->mStartGreen.GetValue(f, p) * 255.f);
		b = UBYTE(cr->mStartBlue.GetValue(f, p) * 255.f);
	}
}

// The ripple test's check of the effect name's initials (name ours).
// STAND-IN (inline budget, entering main only with a complete match). Plain form: the checks
// written out in the sphere's && chain. Effect: the helper's expansion (49 units) spends inline budget before the
// sphere's ori * FVector(w) and takes the checks out of Unknown11's own size; with the sphere's ring point through
// w.Set (44 units), that pays for the life fraction's three stores: the M*V site has 194 left of 186..232 (365 without
// either). The initials read the name through the descriptor, so they index from this as retail's (004c98cf..004c98ea).
// A second helper for the two names (no budget of its own) moved the reloads after the reduction (ecx before esi,
// 004c9a2f). Original: unknown. Replace it when the budget comes out right without it.
static inline bool IsBlastInitials(const CParticleDescriptor *pd)
{
	return (pd->mName[1] == 'e') && (pd->mName[5] == 'P') && ((pd->mName[22] == 'A') || (pd->mName[22] == 'L'));
}

// A point count divided for distance, never under 4 (name ours). Open question: the source's real helpers.
// STAND-IN (inline budget; labelled 2026-10-15): FewerPoints, ModColour and AddGrid below are helpers whose
// role is the budget. Written out in place: FewerPoints score 220/1272/6196 (and 00406d50 lost), ModColour
// 86/630/5755, AddGrid 159/797/5836 (and 00406d50 lost). Replace them when the budget comes out right without them.
// The inline budget (inline logger, 2026-10-10): the sphere's `ori * FVector(w)` (186 units) must expand with
// 186..232 left, so that the Normalise and the FVector constructor after it stay out of line, and the out-of-line calls
// are retail's (FVector's constructor four times, the FMatrix rows, Normalise twice, the dot product, ClampedRatio,
// LerpColour). That site has 194 left: this helper is 55 units (`n = n / d` and the braced clamp; all eight expansions
// come before the site), both meshes take their colour through ModColour, LifeColour's flag tests are written
// `!= FALSE`, and Unknown11 tests its pointers and flags plainly and takes fabsf, sinf and cosf, Stuart's float
// wrappers (none of which spends budget). The counts come by reference, which keeps them in memory as retail's (by
// value they move to registers). A closing `return;` (+1 unit in each expansion) was the budget's last concession
// until the cylinder's colour helper.
static inline void FewerPoints(SINT &n, SINT d)
{
	if (n > 4)
	{
		n = n / d;
		if (n < 4)
		{
			n = 4;
		}
	}
}

// The particle's colour modulated by the colour range's, alpha 0xff, as a vertex colour (name ours). Both meshes take
// it through this helper: each expansion (size 61) spends the inline budget before the sphere's ori * FVector(w), where
// the written-out form is own size (with the sphere's written out that site has 305 left, and the FVector constructor
// and Normalise calls after it change). The code is the same either way.
static inline DWORD ModColour(UBYTE r, UBYTE g, UBYTE b, UBYTE rr, UBYTE rg, UBYTE rb)
{
	return (((((0xff << 8) + (UBYTE)((r * rr) >> 8)) << 8) + (UBYTE)((g * rg) >> 8)) << 8) + (UBYTE)((b * rb) >> 8);
}

// Two triangles for each cell of a grid of vertices, rows by cols cells from the first (name ours). Retail inlines
// it in both meshes; ours spells the sphere's out. The texture comes by reference, so each call reads the mesh's
// texture pointer again as retail's do (004caad1, 004caae0; by value VC6 keeps it in a register), and the indices
// are UWORDs, as AddTriangle takes them (see the sphere's). cols comes by const reference, so b's sum reads numrad
// itself and ranks it above first: first is loaded into a's register and b is numrad + first + 1, as retail's
// (lea esi,[ecx+edi+1], 004caaa1..004caaaf). By value the inliner kept cols as a copy of numrad while it substituted
// first; the sum, narrowed to 16 bits, read that copy, which copy propagation later replaced by a 16-bit view of numrad
// numbered 0x866, whose rank key wraps (rotl16) below first's: b then took first's register and a was copied out of
// it (the rank tool's IR dump).
static inline void AddGrid(CDXParticleTexture *&tex, SINT first, SINT rows, const SINT &cols)
{
	UWORD	a = first;
	UWORD	b = first + cols + 1;
	for (SINT i = 0; i < rows; i++)
	{
		for (SINT j = 0; j < cols; j++)
		{
			tex->AddTriangle(a, a + 1, b);
			tex->AddTriangle(a + 1, b, b + 1);
			a++;
			b++;
		}
		a++;
		b++;
	}
}

//*********************************************************************************
// A procedural mesh particle: a sphere segment (latitude and longitude ranges) or a cylinder, built each frame in the
// particle's frame of reference, coloured by the colour range over the particle's life and along the mesh, fewer
// points the farther it is. The "Mech Pulse Bolt Blast" spheres get 50 x 50 points rippled by the noise table when
// the GeForce FX extras are on; the other spheres fade where they face the camera edge on.
// Each ring loop's modulated channels are masked to a byte, as retail's `sar 8; and 0xff` (004c9e0b, 004ca8ab), and the
// out-of-line calls are retail's, in order (the budget is at FewerPoints).
// Each ring loop tests for the colour range first: with the UWORD triangle indices both colour blocks take retail's
// registers and share retail's and/add tails (004c9dd3..004c9e71, 004ca873..004ca912). The plain-first order an
// arm-swap scan found (2026-10-09) suited the SINT indices.
// The triangle indices are UWORDs, as AddTriangle takes them: VC6 then strength-reduces a + 1 and b + 1 into their
// own induction variables as retail's loops do (004ca1ed..004ca222, 004caac0..004caafb); with SINT indices it
// narrows each sum to the parameter's 16 bits and recomputes it per call. That also gives both point reductions
// retail's register webs (sphere: numax esi, numrad ecx through the join, the first arm's numrad tail cross-jumped
// into the second's at 004c99d2; cylinder: numrad esi, 004ca427) and keeps numax + 1 and numrad + 1 in spill slots
// rather than rematerialized in the ring latches.
// Frame. Our packer, replayed without parameter homes, gives our layout
// exactly. Retail's highest class (slots 288..336) holds the
// vertex and the FoR product's FMatrix temporary together. At function scope vert is in the alias class of every call,
// the FoR product's FMatrix constructor included, and the frame was 0x168; each mesh declaring its own vertex gives
// retail's 0x150. Each case's vertex brings its own field symbols, and every inline symbol's id (the GetValue order of
// LifeColour and both colour ranges, the cylinder's distance squares, the FoR matrix rows) follows the count of the
// function's own symbols: with the counters and row fractions per mesh (two of each in retail's frame), the ring point
// w declared once for both meshes and the sphere ripple's intermediates written inline keep that count where it was.
// The shared w also numbers its fields in the cylinder, before pos's, so the sphere's Y and Z sums load pos first as
// retail's (004ca021, 004ca057).
// Retail's other slot pairings: its slot 0 holds both cases' numax with the sphere's numrad, slot 44 the sphere's
// first with the cylinder's dist and fnumrad, slot 88 the sphere's lat with the cylinder's i.
// The sphere's ripple test is computed into `rippled` before the branch: the rippled arm then jumps
// over the point reduction, which stays in line after it as retail's (004c9961..004c9a2f); written as one if/else, VC6
// laid the reduction out after the ring loop's setup. Its two separate stores of 50 store numax first, as retail's
// (004c996b).
// Each mesh takes its own first vertex index, as retail's frame keeps two (the sphere's at frame offset 0x2c, the
// cylinder's at 0x90): the cylinder's distance then sums its squares in retail's order (004ca2cb..004ca2e1).
// Frame: every slot class is retail's, through each colour range's single t, the life
// fraction's three stores and the sphere's vertex filled v coordinate first.
void	CPDPMesh::Unknown11(CParticle *p)	// FUNCTION: 0x004c9000
{
	CPDSimpleSprite::InitNoiseTable();
	if (!mTexture)
		return;
	if ((p->mFoR) && (p->mFoR->mUnknownAC != -1.f) && (EVENT_MANAGER.GetTime() - 0.09999f > p->mFoR->mUnknownAC))
		return;

	float	tileu = mTileU.GetValueByAge(p->mLife, mLife, p);
	float	tilev = mTileV.GetValueByAge(p->mLife, mLife, p);
	float	scrollu = mScrollU.GetValueByAge(p->mLife, mLife, p);
	float	scrollv = mScrollV.GetValueByAge(p->mLife, mLife, p);

	// the particle's colour; its life fraction is a named local: the front-end symbol replaces LifeColour's
	// fraction parameter, which puts every colour range's GetValue calls in retail's order (the cylinder rows'
	// end colours too, 004ca785..004ca828)
	UBYTE	r = 255, g = 255, b = 255;
	CPDColourRange	*cr = (CPDColourRange *)mColourRange2;
	if (cr)
	{
		// STAND-IN (entering main only with a complete match). Plain form: `float life =
		// (mLife - p->mLife) / mLife;`. Effect: the store is written in three identical arms, which C2 merges after
		// register allocation (0x10758522); the tests branch on the flags of the `cr` test and go with them. The stack
		// packer counts all three stores: life has 12 references and joins retail's slot 0 class with the sphere's numrad
		// and the cylinder's numax, which leaves the sphere's numax with the cylinder's f and t in slot 8 as retail's
		// (004c9147). Each arm divides in place, which keeps the temporaries before the cylinder's symbols inside their
		// 32-id block. Original: unknown; retail's two extra references to life at the packer are not found. Replace it
		// when a form with real uses gives life those references.
		float	life;
		if ((SINT)cr < 0)
		{
			life = mLife - p->mLife;
			life /= mLife;
		}
		else if ((SINT)cr > 0)
		{
			life = mLife - p->mLife;
			life /= mLife;
		}
		else
		{
			life = mLife - p->mLife;
			life /= mLife;
		}
		LifeColour(cr, life, p, r, g, b);
	}

	FVector	pos = p->mPos;
	FMatrix	ori = p->mOrientation;
	if (p->GetFoR())
	{
		if (p->GetFoR()->mHasOrientation)
		{
			pos = p->GetFoR()->mOrientation * pos;
			ori = ori * p->GetFoR()->mOrientation;
		}
		pos += p->GetFoR()->mStartPos;
	}
	FVector	campos = ENGINE.GetCamera()->GetPos();

	FVector	w;
	switch (mType)
	{
	case 0:
		{
			// a cylinder along the local Y axis
			// each mesh declares its own counters, as retail's frame keeps two of each: numax in slots 0 and 8,
			// numrad in 0 and 4, i in 16 and 88, j in 4 and 84 (a variable map on retail)
			SParticleVertex	vert;
			SINT	first = mTexture->GetNextVertex();
			SINT	numax, numrad, i, j;
			float	length = mCylinderLength.GetValueByAge(p->mLife, mLife, p);
			float	radius2 = mCylinderRadius2.GetValueByAge(p->mLife, mLife, p);
			FVector	d = campos - pos;
			float	dist = d.MagnitudeSq();
			float	size = fabsf(mCylinderRadius.GetValue(0.5f, p) * radius2);
			numax = mCylinderNumPtsAxial;
			numrad = mCylinderNumPtsRadial;
			size = (size + fabsf(length)) * 0.5f;
			size = size * size / dist * TRAIL_DETAIL * 102400.f;
			if (size < 30.f)
			{
				FewerPoints(numax, 3);
				FewerPoints(numrad, 3);
			}
			else if (size < 300.f)
			{
				FewerPoints(numax, 2);
				FewerPoints(numrad, 2);
			}

			for (i = 0; i < numax; i++)
			{
				float	f = i / (float)(numax - 1);
				float	l = f * length;
				float	v = f * tilev + scrollv;
				float	radius = mCylinderRadius.GetValue(f, p) * radius2;

				UBYTE	rr, rg, rb;
				DWORD	col;
				cr = (CPDColourRange *)mColourRange;
				if (cr)
				{
					MeshRangeColour(cr, f, p, rr, rg, rb);
					col = ModColour(r, g, b, rr, rg, rb);
				}
				else
					col = (((((0xff << 8) + r) << 8) + g) << 8) + b;

				for (j = 0; j < numrad + 1; j++)
				{
					// STAND-IN (a revisitable compiler concession, entering main only with a
					// complete match). Plain form: none; the fields are first named where they are used, the frame rows in
					// w.Set's arguments and the position in the vertex temporary's (be9a4b8a: score 8/1066/5737,
					// anchored 11). Effect: the dead mention numbers the frame's Y column, then its Row[1].Z, Row[1].X,
					// Row[2].Z, Row[2].X and Row[0].Z, before the ring point's X and Z, so the ring point outranks them and
					// every row product loads its X or Z first, as retail's (004ca99a..004ca9f0), and then pos.X, w.X,
					// w.Y and pos.Y at the ids the head's in-place adds gave them, ahead of the unnamed vertex temporary,
					// whose constructor numbers itself first and its arguments right to left. Each position sum then loads
					// its higher-numbered field first: w.X, pos.Y and pos.Z (004caa00, 004caa0b, 004caa1d). The Y column
					// keeps the ids that named axis-term locals (ax, ay, az) gave it, so the FoR product's operand orders
					// stay retail's (without the Y column: anchored 38, five trees lost). Its own size is paid for by
					// ModColour's two expansions (the sphere's ori * FVector(w) at 194 of 186..232). Without the frame rows
					// (be9a4b8a) anchored 11; without the position fields the FoR product's and the sphere rotation's
					// operand orders are lost (five trees). Original: unknown. Replace it when a form with real uses
					// numbers those fields first at the same budget.
					if (0) ori.Row[0].Y, ori.Row[1].Y, ori.Row[2].Y, ori.Row[1].Z, ori.Row[1].X, ori.Row[2].Z, ori.Row[2].X, ori.Row[0].Z, pos.X, w.X, w.Y, pos.Y;
					float	a = j / (float)numrad;
					// The ring point in the cylinder's own frame, its sine and cosine scaled by the radius, is a vector:
					// retail keeps the sine (stored once, read by three row products) in a 16-byte slot it shares with
					// the sphere's normal and a temporary of the sphere's rotation (frame offset 0xd0, 004ca98d), which a
					// float local cannot take: VC6's packer places every 4-byte local before any 16-byte one, so a float
					// joined a class of the sphere's 4-byte locals instead, and the vertex temporary took the normal's
					// class (slot-packer replay on the real interference graph). The cosine stays on
					// the x87 stack.
					FVector	ring;
					ring.X = sinf(a * PI_M2) * radius;
					ring.Z = cosf(a * PI_M2) * radius;
					// the ring point goes into w through Set, so w's fields are first named after the frame rows (by the
					// mention above and the vertex temporary below): the sphere's rotation, which reads them, then loads w.X
					// before Row[1].X and Row[2].X as retail's (004c9f76, 004c9f9e).
					// STAND-IN (an explicit concession): r0 is read for X only while Z is named
					// directly, an id shift rather than a spelling a programmer would choose; it enters main only as part
					// of a complete match. The plain `ori.Row[0].X * s` (28eded23) measures score 64/1214/5620,
					// anchored 58 (49/1199/5620 and 50 with r0). Through r0 the function's text never names
					// ori.Row[0].X: its symbol is made when the FoR product expands (id 0x446), the product's Row 0 Z sum
					// adds X, Y, Z in retail's order (004c9625), the sphere's rotation takes retail's row 0 order
					// (004c9fbb..004c9fe6) and this X row loads Row[0].X before the ring point's X (004ca99a). The
					// reference's symbol takes the place of the dropped field's, so the count of the function's own
					// symbols stays. Read through r0 as well, Z drops two symbols, the FoR matrix parameter's id becomes
					// 0x2fb and the product's X column loads the Y term first (004c9678).
					// Each row's axis term (its Y times l) is the sum's last term: VC6 hoists those three products, and
					// the radial count's conversion, to the ring loop's preheader, after its guard, where retail computes
					// them (004ca922..004ca959).
					const FVector	&r0 = ori.Row[0];
					w.Set(r0.X * ring.X + ring.Z * ori.Row[0].Z + ori.Row[0].Y * l, ring.Z * ori.Row[1].Z + ring.X * ori.Row[1].X + ori.Row[1].Y * l, ring.Z * ori.Row[2].Z + ring.X * ori.Row[2].X + ori.Row[2].Y * l);
					// The position goes to the vertex through an FVector temporary, as retail's: the temporary's Y and Z
					// are stored and copied into the vertex as integer moves, its X goes from the x87 stack, the vertex
					// address is pushed after Z's copy and the ring point's Z is dropped last (004caa00..004caa58).
					// the vertex is filled in its field order once the position is known: the chooser then picks the
					// position's Y and Z copies before v's, so Y and the vertex address take edx, Z eax and v ecx, and
					// the texture pointer is loaded at the top of the ring as retail's (004ca964..004caa51); with the
					// colour and v stored first, v took edx and the vertex address was pushed early (
					// 2026-10-10, chooser log)
					vert.mPos = FVector(w.X + pos.X, w.Y + pos.Y, w.Z + pos.Z);
					vert.mColour = col;
					vert.mU = v;
					vert.mV = a * tileu + scrollu;
					mTexture->AddVertex(&vert);
				}
			}

			AddGrid(mTexture, first, numax - 1, numrad);
		}
		break;

	case 1:
		{
			// a sphere segment
			SParticleVertex	vert;
			SINT	first = mTexture->GetNextVertex();
			SINT	numax, numrad, i, j;
			// STAND-IN (an explicit concession): each value is bracketed, and inside latstart and
			// latend so is the conversion to radians. The brackets emit no code (the function keeps the same 1875
			// instructions); they add seven IL tuples, which end the scheduler's first 81-tuple region two tuples before
			// lonend's GetValue call (scheduler log, regionprobe-style probes), the same kind of concession as CalcLODs'
			// LandscapeColumnY on main. The point counts and KeyOn's argument then schedule with lonend's store, as retail's
			// (004c97ec..004c981f), and lonend's `this` (lea ecx,[ebx+0xd4]) waits for the argument's x87 stores as
			// retail's (004c97e1). Unbracketed the edge falls after the counts, and the second count and the push wait for
			// the difference's X: score 23/1119/5713, anchored 24 (five brackets 17/1119/5715, anchored 19; with the
			// vertex temporary, five brackets measure anchored 13 and the lea fills the fmul's latency, seven anchored 11).
			// The two inner brackets count two own-size units toward the inline budget (ori * FVector(w) at 194 of 186..232
			// with them). The original source's form is unknown. The counts are read numax first: numrad then takes ecx
			// and is stored before the push, numax eax, stored after `mov ecx, PLATFORM` (004c97f2..004c981b).
			float	radius = (mSphereRadiusTime.GetValueByAge(p->mLife, mLife, p));
			float	latstart = ((mSphereLatitudeStart.GetValueByAge(p->mLife, mLife, p) * (PI / 180.f)) - PI / 2);
			float	latend = ((mSphereLatitudeEnd.GetValueByAge(p->mLife, mLife, p) * (PI / 180.f)) - PI / 2);
			float	lonstart = (mSphereLongitudeStart.GetValueByAge(p->mLife, mLife, p) * (PI / 180.f));
			float	lonend = (mSphereLongitudeEnd.GetValueByAge(p->mLife, mLife, p) * (PI / 180.f));
			numax = mSphereNumPtsAx;
			numrad = mSphereNumPtsRad;
			// The distance block is retail's x87 code (004c9808..004c989f). d is built from the three differences: the two
			// sums then take retail's term order, the distance (Y² + Z²) + X² and the range (Z² + Y²) + X² (with
			// `campos - pos` they swap and d.Z stays on the x87 stack). Retail keeps dist and range in memory: X² is stored
			// once and read by both sums (004c984b), dist is stored after its sum (004c9861) and reloaded for size
			// (004c988f), and range, never read, is still computed and stored (004c987d).
			// STAND-IN: the dead `if (0)` call takes both addresses, which keeps them in memory, and
			// the block, which ends after the ripple test (KeyOn's call is the next real call), limits that to retail's
			// lifetimes: the frame stays 0x150 (escaped at the case's or the function's scope it is 0x154 or 0x158; in a
			// block with no call after it the escape does nothing). No real construct is known: no retail call takes either
			// address, and volatile, which gives the same x87 code and frame, holds KeyOn's `push 0x3b` and `mov ecx,
			// PLATFORM` until after the fdiv, where retail issues them before the subtractions (004c9802, 004c9816).
			// Without the call: score 51/1203/5616, anchored 46 (with it 23/1121/5713, anchored 24); without the call
			// and the block the inline budget moves (anchored 1139).
			float	size;
			bool	rippled;
			{
				FVector	d(campos.X - pos.X, campos.Y - pos.Y, campos.Z - pos.Z);
				float	dist = d.MagnitudeSq();
				float	range = d.Magnitude();
				if (0) ToWCHAR((char *)&dist), ToWCHAR((char *)&range);
				size = radius * radius / dist * TRAIL_DETAIL * 102400.f;
				rippled = (!PLATFORM.KeyOn(DIK_F1)) && (EXTRA_GEFORCE_FX_FEATURES.mValue) && (!GAME.IsMultiplayer()) &&
					IsBlastInitials(this) &&
					((!strcmp(mName, "Mech Pulse Bolt Blast Aug")) || (!strcmp(mName, "Mech Pulse Bolt Blast Large")));
			}
			if (rippled)
			{
				numax = 50;
				numrad = 50;
			}
			else if (size < 10.f)
			{
				FewerPoints(numax, 3);
				FewerPoints(numrad, 3);
			}
			else if (size < 30.f)
			{
				FewerPoints(numax, 2);
				FewerPoints(numrad, 2);
			}

			float	fnumax = (float)numax;
			float	lat;
			float	latstep = (latend - latstart) / fnumax;
			float	fnumrad = (float)numrad;
			float	lonstep = (lonend - lonstart) / fnumrad;
			// lat starts and steps in the for statement, lon likewise in the ring: each preheader then stores the counter's
			// zero before the angle and each latch loads the counter and bound before the angle's fadd, as retail's
			// (004c9a53, 004c9e79, 004ca1a9..004ca1b5)
			for (i = 0, lat = latstart; i < numax + 1; i++, lat += latstep)
			{
				float	f = i / fnumax;
				float	v = f * tilev + scrollv;
				float	ringz = sinf(lat) * radius;
				float	ringr = cosf(lat) * radius;

				UBYTE	rr, rg, rb;
				DWORD	col;
				cr = (CPDColourRange *)mColourRange;
				if (cr)
				{
					MeshRangeColour(cr, f, p, rr, rg, rb);
					col = ModColour(r, g, b, rr, rg, rb);
				}
				else
					col = (((((0xff << 8) + r) << 8) + g) << 8) + b;

				float	lon;
				// lon steps in the for-increment after j: the latch's loads of j and the bound then win the scheduler's
				// priority tie with lon's fadd (IL order) and issue before it, as retail's (004ca189..004ca191); it starts
				// in the for-initializer after j, so j's zero is stored first (004c9e79)
				for (j = 0, lon = lonstart; j < numrad + 1; j++, lon += lonstep)
				{
					float	u = j / fnumrad;
					float	c = cosf(lon);
					float	s = -sinf(lon);
					// The ring point is w, rotated in place: retail stores its X and Y straight into w's slots (004c9ec1,
					// 004c9ed0), copies the product temporary into w and the normal, 16 bytes each, and adds the position
					// to w a field at a time, pos first (004c9fee..004ca07b). Its Z is the ring's height itself: retail
					// never stores it and the ripple multiplies ringz's slot (004c9ed4, 004c9f69). The ripple's latitude
					// factor and the noise product are written inline (VC6 computes each once, 004c9ee0..004c9f4f): with
					// the per-case counters and row fractions, that keeps the count of the function's own symbols, which
					// every inline symbol's id follows (the GetValue order, the FoR rows, the sphere's distance squares).
					// The point goes into w through Set, as the cylinder's does: the same code (VC6 still never stores Z),
					// and 44 units of inline budget spent before the M*V site (see IsBlastInitials)
					w.Set(c * ringr, s * ringr, ringz);
					if (rippled)
					{
						// w *= takes the factor as operator*='s parameter, an inline symbol numbered after w's
						// fields, so each product loads the factor first as retail's (004c9f55..004c9f69)
						// the noise offset radius * 0.2f is written at its three uses (VC6 computes it once): one front-end
						// symbol fewer than a named offset, which with the cylinder's vertex temporary keeps every inline
						// symbol at the head's ids (the FoR product's Row0.X stays 0x446, its operand orders retail's)
						float	nu = (lat * (1.f / PI) + 1.f) * c;
						float	nv = (lat * (1.f / PI) + 1.f) * s - radius * 0.2f;
						w *= SampleNoise(nv, -nu - radius * 0.2f) * SampleNoise(radius * 0.2f + nu, nv) + 1.f;
					}
					// STAND-IN: FVector(w) is a copy whose role is to move compiler ids, not
					// retail ownership proven on its own. VC6 folds the copy away, but its field symbols are numbered
					// after the inlining, so the CSE pass's name for the product's X lands at 0x842 (rank key 0x0841,
					// below pos.X's 0x3420) and the X sum loads pos first as retail's (004c9fee). The plain
					// `w = ori * w;`, everything else unchanged, measures score 65/1065/4407 and anchored 59
					// (65/1063/4408 and 59 with the copy), but its X sum loads the product's X first (renamed 0x69c,
					// key 0xd380) at 004c9fee, against retail's and main's direction. The original source is unknown:
					// replace the copy when a source form gives the same ids naturally.
					w = ori * FVector(w);
					FVector	normal = w;
					w.X += pos.X;
					w.Y += pos.Y;
					w.Z += pos.Z;
					vert.mPos.X = w.X;
					vert.mPos.Y = w.Y;
					vert.mPos.Z = w.Z;
					// the vertex takes its v coordinate before its u (the cylinder's takes u first): the local u is then
					// read before the local v in the stack packer's walk, so among the locals of two references u is
					// placed first, and the cylinder's v joins the fade's slot while the sphere's takes the cylinder
					// ring's last axis term's, as retail's (frame offsets 0x34 and 0x9c; slot-packer replay,
					// 2026-10-10). The scheduler places both stores as before
					vert.mV = u * tileu + scrollu;
					vert.mU = v;
					if ((!rippled) && (!PLATFORM.KeyOn(DIK_F1)) && (EXTRA_GEFORCE_FX_FEATURES.mValue))
					{
						// edge on to the camera fades out
						normal.Normalise();
						FVector	view(campos.X - w.X, campos.Y - w.Y, campos.Z - w.Z);
						view.Normalise();
						// the dot product is named: retail calls it before it pushes ClampedRatio's two constants
						// (004ca137..004ca141); inside the argument list the constants are pushed first. The same
						// local then takes the fade, which retail stores and pushes from memory (004ca151..004ca159). The
						// absolute value is fabsf, the float form Stuart's callers use (references/Onslaught BattleEngine.cpp);
						// here it is the VC6 math.h inline for C++ (`(float)fabs((double)_X)`), the provider of the original
						// is not known. Retail takes it before it reserves the argument slot (004ca146, 004ca148), where
						// (float)fabs was taken after the push
						float	fade = normal * view;
						fade = ClampedRatio(fabsf(fade), 0.2f, 0.4f);
						vert.mColour = LerpColour(col, 0, fade);
					}
					else
						vert.mColour = col;
					mTexture->AddVertex(&vert);
				}
			}

			UWORD	a = first;
			UWORD	b = first + numrad + 1;
			for (i = 0; i < numax; i++)
			{
				for (j = 0; j < numrad; j++)
				{
					mTexture->AddTriangle(a, a + 1, b);
					mTexture->AddTriangle(a + 1, b, b + 1);
					a++;
					b++;
				}
				a++;
				b++;
			}
		}
		break;
	}
}

//*********************************************************************************
// b to a by t, channel by channel (name ours). Each weighted endpoint has its own float lifetime before conversion;
// this preserves the retail saves and temporary slots from 004cab5c through the final channel at 004cac16.
DWORD	LerpColour(DWORD a, DWORD b, float t)	// FUNCTION: 0x004cab30
{
	float	s = 1.f - t;
	float	rStart = ((b >> 16) & 0xff) * s;
	float	rEnd = ((a >> 16) & 0xff) * t;
	DWORD	r = DWORD(rStart + rEnd);
	float	alStart = (b >> 24) * s;
	float	alEnd = (a >> 24) * t;
	DWORD	al = DWORD(alStart + alEnd);
	float	gStart = ((b >> 8) & 0xff) * s;
	float	gEnd = ((a >> 8) & 0xff) * t;
	DWORD	g = DWORD(gStart + gEnd);
	float	blStart = (b & 0xff) * s;
	float	blEnd = (a & 0xff) * t;
	DWORD	bl = DWORD(blStart + blEnd);
	return (((((al << 8) + r) << 8) + g) << 8) + bl;
}

//*********************************************************************************
// where a lies from lo to hi, clamped to 0..1
float	ClampedRatio(float a, float lo, float hi)	// FUNCTION: 0x004cac40
{
	float	r = (a - lo) / (hi - lo);
	if (r < 0)
		return 0;
	if (r > 1)
		return 1;
	return r;
}

//*********************************************************************************
// the noise at (u, v), 1 being the table's width, interpolated between its four neighbours
float	SampleNoise(float u, float v)	// FUNCTION: 0x004cac80
{
	u *= 32.f;
	v *= 32.f;
	SINT	ix = (SINT)floor(u);
	SINT	iy = (SINT)floor(v);
	float	fx = u - ix;
	float	fy = v - iy;
	float	a = NOISE(ix + 1, iy);
	float	b = NOISE(ix, iy);
	float	c = NOISE(ix + 1, iy + 1);
	float	d = NOISE(ix, iy + 1);
	return (a * fx + b * (1 - fx)) * (1 - fy) + (c * fx + d * (1 - fx)) * fy;
}
