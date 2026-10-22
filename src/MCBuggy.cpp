// MCBuggy.cpp: reconstructed from MCBuggy.obj (00492f30..00494b6f); Stuart's release has no copy (mcbuggy.h). A
// wheeled vehicle's motion controller. This file was built with the profiler on: UpdateWheel keeps its profile.
// The allocations' __LINE__s are the retail ones; the #lines place them.


#include "common.h"
#define ENABLE_PROFILER 1
#include "profile.h"
#include "mcbuggy.h"
#include "mesh.h"
#include "map.h"
#include "renderablething.h"
#include "engine.h"
#include "game.h"
#include "eventmanager.h"
#include "DebugLog.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

// FUNCTION: 0x00492f30 init:ZERO_FVECTOR
// FUNCTION: 0x00492f50 init:ID_FMATRIX

//******************************************************************************************
CMCBuggy::CMCBuggy(IRenderableThing *thing)	// FUNCTION: 0x00493020
{
	mThing = thing;
	mWheelPos = NULL;
	mWheelSpin = NULL;
	mPartPos = NULL;
	mPartOri = NULL;
	mPartFrame = NULL;
	mOldPartOri = NULL;
	mOldPartPos = NULL;
	mOldPartFrame = NULL;
	mLastTime = -1.0f;
	mSmoothNormal.Set(0, 0, 0);
	mBodyMotion = 0;
	mNumFrames = 0;
}

// FUNCTION: 0x00493080 ??_GCMCBuggy@@UAEPAXI@Z

//******************************************************************************************
CMCBuggy::~CMCBuggy()	// FUNCTION: 0x004930a0
{
	if (mWheelPos)
		delete [] mWheelPos;
	if (mWheelSpin)
		delete [] mWheelSpin;
	if (mPartPos)
		delete [] mPartPos;
	if (mPartOri)
		delete [] mPartOri;
	if (mPartFrame)
		delete [] mPartFrame;
	if (mOldPartPos)
		delete [] mOldPartPos;
	if (mOldPartOri)
		delete [] mOldPartOri;
	if (mOldPartFrame)
		delete [] mOldPartFrame;
}

//******************************************************************************************
void CMCBuggy::SetBuggyParameters(float body_motion)	// FUNCTION: 0x00493180
{
	mBodyMotion = body_motion;
}

//******************************************************************************************
// Sizes everything from the mesh the first time the wheels are updated.
void CMCBuggy::Init(CMesh *mesh)	// FUNCTION: 0x00493190
{
	mNumWheels = 1;
	while (mesh->GetEmitterSubpart("WheelBase", mNumWheels))
		mNumWheels++;
	mNumWheels--;

#line 68
	mWheelPos = new(MEMTYPE_MOTIONCONTROLLER) FVector[mNumWheels];
	mWheelSpin = new(MEMTYPE_MOTIONCONTROLLER) float[mNumWheels];
	for (SINT i = 0; i < mNumWheels; i++)
	{
		mWheelSpin[i] = 0;
		mWheelPos[i].Set(0, 0, 0);
	}

#line 77
	mPartPos = new(MEMTYPE_MOTIONCONTROLLER) FVector[mesh->mNumParts];
	mPartOri = new(MEMTYPE_MOTIONCONTROLLER) FMatrix[mesh->mNumParts];
	SINT	parts = mesh->mNumParts;	mPartFrame = new(MEMTYPE_MOTIONCONTROLLER) float[parts];	// a value local on its own line would move __LINE__

	mOldPartPos = new(MEMTYPE_MOTIONCONTROLLER) FVector[mesh->mNumParts];
	mOldPartOri = new(MEMTYPE_MOTIONCONTROLLER) FMatrix[mesh->mNumParts];
	mOldPartFrame = new(MEMTYPE_MOTIONCONTROLLER) float[mesh->mNumParts];
	for (SINT p = 0; p < mesh->mNumParts; p++)
	{
		mPartFrame[p] = -1.0f;
		mOldPartFrame[p] = -1.0f;
	}

	CAnimMode	*anim = mesh->GetAnimModePtr(mesh->GetAnimModeByName("WheelMotion"));
	// The first frame is read once and stored after the count: retail loads mUnknown18 and subtracts before the
	// mFirstFrame store (00493401..00493406), and the count stays in eax past the product built in ebp.
	SINT	first = anim->mStart;
	mNumFrames = anim->mLast - first + 1;
	mFirstFrame = first;

#line 102
	mWheelOffsets = new(MEMTYPE_MOTIONCONTROLLER) FVector[mNumWheels * mNumFrames];
	for (SINT w = 0; w < mNumWheels; w++)
	{
		CMeshPart	*base = mesh->GetEmitterSubpart("WheelBase", w + 1);
		for (SINT f = 0; f < mNumFrames; f++)
		{
			FVector	pos;
			FMatrix	ori;
			base->GetWorldTransforms(mFirstFrame + f, 0, 0, mThing, &ori, &pos, true, 0, 0);
			mWheelOffsets[w * mNumFrames + f] = pos;
		}
	}
}

#line 126
//******************************************************************************************
// The body's basis from the ground normal: down is the negated normal, side and forward come from crosses with the back
// direction, and all three are normalised into the matrix's columns. A reconstructed boundary: the name and the signature
// are ours. Retail calls NegateToOut, Cross (twice) and NormalizeInPlace (three times) out of line here, but later expands
// FMatrix::operator* and calls FVector's constructor out of line only with a budget of 594 or more; the inline budget only
// falls, so this code is nested in a helper's expansion, where its share is small. The back direction comes in as an
// argument (the caller builds (0, -1, 0)); its constructor site and the binding are part of the inline budget retail's
// decisions need (the wheel's first operator- at 189..203). down negates the normal directly: with a copy of it in a
// local first (one more inline local, which moves the later ids) the Axle's world product's C20 loses retail's
// association (00493a1a).
static inline void GroundBasis(FMatrix &m, FVector normal, const FVector &axis)
{
	FVector	down = -normal;
	FVector	back = axis;
	FVector	side = down ^ back;
	FVector	forward = down ^ side;
	side.Normalise();
	forward.Normalise();
	down.Normalise();
	m.Row[0].X = side.X;	m.Row[0].Y = forward.X;	m.Row[0].Z = down.X;
	m.Row[1].X = side.Y;	m.Row[1].Y = forward.Y;	m.Row[1].Z = down.Y;
	m.Row[2].X = side.Z;	m.Row[2].Y = forward.Z;	m.Row[2].Z = down.Z;
}

//******************************************************************************************
// The identity, row by row through FVector, as mesh.cpp and MeshPart.cpp also write it (name ours).
static inline void SetIdentity(FMatrix &m)
{
	m.Row[0] = FVector(1, 0, 0);
	m.Row[1] = FVector(0, 1, 0);
	m.Row[2] = FVector(0, 0, 1);
}

//******************************************************************************************
// STAND-IN (product rank keys and scheduler nodes): six of UpdateWheel's inline expansions go
// through file-local copies of maths.h's members, reached by static_cast, and one redundant bracket pair in the function's
// text keeps their accounting. What the copies change:
// - (double) conversions on single factors: the body's ground point (FBuggyGround: row 0 X, row 2 X and Z), the Axle's
//   world product (FBuggyWorld: C00 term 0), the Axle loop's product (FBuggyLoop: rows 1 and 2 X), the wheel position
//   (FBuggyWheelPos: row 0 Y) and the wheel orientation (FBuggyWheelOri: C21 term 0). VC6 orders a sum's products by
//   rank key (the rank tool); a converted product's factors become conversion temporaries, so its key comes from
//   those instead of the field symbols' ids, and the conversions also renumber later symbols; the x87 multiplies and
//   adds in its register precision either way, so no instruction changes.
// - Explicit brackets where the keys do not give retail's association (retail's association, not its operand order):
//   the loop's rows X + Z first (00493b5a, 00493b7d, 00493ba3) and the wheel orientation's C00 Y + Z first and C22
//   X + Y first (00493df3, 00493d14).
// - FBuggyNormal, FVector::Normalise: its magnitude is one flat sum, X * X + (Y * Y) + (Z * Z).
//   The bracketed products are larger rank keys, so the sum runs (Z*Z + Y*Y) + X*X, retail's association and order
//   (004937be..004937e4; maths.h's three brackets give X first with these ids), and their two scheduler nodes keep the
//   copies of normal.Z into mSmoothNormal and normal after X*X's multiply. With no bracket on the inner sum (the
//   X*X + (Y*Y + Z*Z) had one) two cycles stay free before the last faddp, so the Pad copy also goes before it, as
//   retail's (004937de). The brackets around the initializer and around 1.0f / m are CSE tuples that replace the two
//   the sum dropped, so the later address temporaries keep their numbers (mod 4).
// - The loop's copy reads v through a local pointer: read through the parameter, v's X (offset 0) becomes a field
//   symbol at once, with an early id; through the pointer it is folded later like v's other fields, and the loop's rows
//   2 and 0 load v's X first, as retail's (00493b2b, 00493b80).
// - Brackets are also CSE tuples (each renumbers the later address temporaries, whose rank keys use the number mod 4)
//   and scheduler nodes (81-node regions). The wheel position's row 0 X and Z products drop maths.h's brackets and the
//   loop's rows are bracketed three times over as a whole: the wheel block's and the loop body's region cuts then fall
//   where retail's do (the wheel orientation's last argument push after its second product, 00493de9; the four copies
//   of the loop's result into v in the region of the distance's call, 00493bd1..00493bee). The redundant bracket pair
//   on the Axle loop's distance, `fabsf(((MAP.Collide(v) - v.Z)))` (STAND-IN, labelled in place), restores the CSE
//   numbering after them.
// FP ledger (commutation-insensitive, against main): all 62 of retail's trees, none of main's 53 lost;
// the forwarding ledger agrees, and every x87 expression is retail's in operand order too. Retail's orders come from
// symbol ids no natural spelling has reproduced; each conversion, bracket and the pointer is needed (dropping one costs
// 4 to 186 raw differences, most also lose trees). Each copy's static_cast adds 2 own size and each conversion spends 4
// to 5 budget units at its site; see UpdateWheel's comment for the braces that keep retail's inline decisions. Original
// spelling unknown.
struct FBuggyNormal : public FVector
{
	void Normalise() { float m = (sqrtf(X * X + (Y * Y) + (Z * Z))); if (m) { m = (1.0f / m); X *= m; Y *= m; Z *= m; } }
};

struct FBuggyGround : public FMatrix
{
	FVector operator*(const FVector &v) const { return FVector(((double)Row[0].X * v.X) + (Row[0].Y * v.Y) + (Row[0].Z * v.Z), (Row[1].X * v.X) + (Row[1].Y * v.Y) + (Row[1].Z * v.Z), ((double)Row[2].X * v.X) + (Row[2].Y * v.Y) + ((double)Row[2].Z * v.Z)); }
};

struct FBuggyWorld : public FMatrix
{
	FMatrix operator*(const FMatrix &m) const
	{
		return FMatrix(
			FVector((double)Row[0].X * m.Row[0].X + Row[0].Y * m.Row[1].X + Row[0].Z * m.Row[2].X,
				Row[0].X * m.Row[0].Y + Row[0].Y * m.Row[1].Y + Row[0].Z * m.Row[2].Y,
				Row[0].X * m.Row[0].Z + Row[0].Y * m.Row[1].Z + Row[0].Z * m.Row[2].Z),
			FVector(Row[1].X * m.Row[0].X + Row[1].Y * m.Row[1].X + Row[1].Z * m.Row[2].X,
				Row[1].X * m.Row[0].Y + Row[1].Y * m.Row[1].Y + Row[1].Z * m.Row[2].Y,
				Row[1].X * m.Row[0].Z + Row[1].Y * m.Row[1].Z + Row[1].Z * m.Row[2].Z),
			FVector(Row[2].X * m.Row[0].X + Row[2].Y * m.Row[1].X + Row[2].Z * m.Row[2].X,
				Row[2].X * m.Row[0].Y + Row[2].Y * m.Row[1].Y + Row[2].Z * m.Row[2].Y,
				Row[2].X * m.Row[0].Z + Row[2].Y * m.Row[1].Z + Row[2].Z * m.Row[2].Z));
	}
};

struct FBuggyLoop : public FMatrix
{
	FVector operator*(const FVector &v) const { const FVector *p = &v; return FVector((((((Row[0].X * p->X) + (Row[0].Z * p->Z)) + (Row[0].Y * p->Y)))), ((((((double)Row[1].X * p->X) + (Row[1].Z * p->Z)) + (Row[1].Y * p->Y)))), ((((((double)Row[2].X * p->X) + (Row[2].Z * p->Z)) + (Row[2].Y * p->Y))))); }
};

struct FBuggyWheelPos : public FMatrix
{
	FVector operator*(const FVector &v) const { return FVector(Row[0].X * v.X + ((double)Row[0].Y * v.Y) + Row[0].Z * v.Z, (Row[1].X * v.X) + (Row[1].Y * v.Y) + (Row[1].Z * v.Z), (Row[2].X * v.X) + (Row[2].Y * v.Y) + (Row[2].Z * v.Z)); }
};

struct FBuggyWheelOri : public FMatrix
{
	FMatrix operator*(const FMatrix &m) const
	{
		return FMatrix(
			FVector((Row[0].Y * m.Row[1].X + Row[0].Z * m.Row[2].X) + Row[0].X * m.Row[0].X,
				Row[0].X * m.Row[0].Y + Row[0].Y * m.Row[1].Y + Row[0].Z * m.Row[2].Y,
				Row[0].X * m.Row[0].Z + Row[0].Y * m.Row[1].Z + Row[0].Z * m.Row[2].Z),
			FVector(Row[1].X * m.Row[0].X + Row[1].Y * m.Row[1].X + Row[1].Z * m.Row[2].X,
				Row[1].X * m.Row[0].Y + Row[1].Y * m.Row[1].Y + Row[1].Z * m.Row[2].Y,
				Row[1].X * m.Row[0].Z + Row[1].Y * m.Row[1].Z + Row[1].Z * m.Row[2].Z),
			FVector(Row[2].X * m.Row[0].X + Row[2].Y * m.Row[1].X + Row[2].Z * m.Row[2].X,
				(double)Row[2].X * m.Row[0].Y + Row[2].Y * m.Row[1].Y + Row[2].Z * m.Row[2].Y,
				(Row[2].X * m.Row[0].Z + Row[2].Y * m.Row[1].Z) + Row[2].Z * m.Row[2].Z));
	}
};

// STAND-IN (scheduler node): the wheel's spin angle goes through a file-local copy of math.h's
// atanf without that body's redundant outer brackets. math.h's `return ((float)atan((double)_X));` adds one no-code
// scheduler node (as its cosf does; maths.h's MakeRotationF note), and the spin sits in the wheel block's fourth
// 81-node region: one node fewer ends that region after the push of spin's address instead of before it, so the copy
// into m and the next product's setup take retail's order (00493fe9 `mov esi,eax` before `mov ecx,0xc`, 00493ffe
// `push eax` before `lea ecx`). Same instructions and x87 trees either way; math.h's fabsf has the same brackets and
// a copy of it does the same. Original spelling unknown.
static inline float BuggyAtanf(float x) { return (float)atan((double)x); }

//******************************************************************************************
// A part and its children, from its parent's position and orientation. wheel is the axle's wheel number below an
// axle, -1 above one; frame is then the axle's WheelMotion frame.
// The wheel block's matrices are plain locals: retail builds each one's rows with the vector constructor iterator
// (00493e1d, 00493e32, 00493fb2: three FVectors), and fills the spin matrix with the out-of-line MakeRotationF
// (004062d0, called at 00493fc9).
// The identity's rows come from SetIdentity, so they sit at a nested level: the first FVector is expanded and the
// other two are called (00493599, 004935cd), as a simulation required (inside FMatrix(9 floats) all three
// were called and the vector constructor iterator took the level's budget). The key reads go through CMeshPart's own
// accessors, as GetTransforms (004b0d00) reads them: those free sites deny GroundBasis's unary minus (NegateToOut,
// 004404f0 at 00493849) and the world product's first row (all three rows through FVector's constructor, 00493a1d,
// 00493a73, 00493ac9), and the wheel's inverse stays a call (00494350).
// The profile's destructor is free (profile.h), so it is expanded at the exit as retail's is (004942d2..0049430a, reading
// the start time back from the constructor's t at [ebp-0xd8]); the tail's first `+=` is expanded before it, as in
// retail (pos += the thing's position: fld, fadd, fstp on pos). Every out-of-line call is then retail's. rotated is set
// before the identity, as retail stores it before the second row's call (00493589). The ground point is written plainly,
// `pos + ori * key`: maths.h's operator brackets each product, and those bracket nodes put the 81-node scheduler region
// cut of the Body block where retail's falls, so the smoothing's copies into normal and mSmoothNormal interleave with its
// sums as in retail (0049378c..004937de; the reconstructed helper with one bracket per row ended the region 12 nodes
// later and scheduled every copy after the x87 work). The Axle block is declared like the wheel block (an unused
// FMatrix array and a position; the base part's search, which retail keeps, 00493995, 004939a6, sets the function's one
// `base`, see below), origin is assigned from
// GetRenderPos's temporary (00493ae5..00493b00) and the axle vector from the subtraction. The Axle loop's vector v is
// declared at the Axle block's level: an address-taken local is packed for its whole scope, so v's slot class then
// conflicts with the wheel block's base_pos, which packs with GroundBasis's down as retail's (-0x8c) instead of with
// normal and v, and the classes below follow retail's (the 14-reference temporary with back at -0x4c; normal and v
// below thing and the anim key; slot pack trace, a slot-packer): 139 of our slot references paired with
// different retail offsets before it, 23 after. The smoothing adds the scaled mSmoothNormal to the scaled normal,
// which retail computes first (operator+'s argument is evaluated first, 0049373c..00493784), and assigns the sum to
// normal, then normal to mSmoothNormal: retail stores normal.X before mSmoothNormal.X (0049379a). The wheel's spin
// divides in double, rolled.Y converted over fabsf's result: retail loads rolled.Y before the fabs and swaps the stack
// twice before the division (00493f79..00493f88), which `(double)rolled.Y / fabsf(axle.Z)` reproduces (fabs in double
// and a float conversion of the quotient do not). The key frame floors through floorf and the spin's angle comes from
// atanf (through its bracket-free copy, STAND-IN above), the float functions Stuart uses; the Axle loop's distance goes
// through fabsf. The tail indexes the part arrays by part->mPartNumber at each use, as retail re-reads it
// (004941c0..00494258).
// The loop's sum is written product first (`world * v + origin`): with the ids that follow, origin's components are
// loaded first in its sums, as retail's (00493bab..00493bc2).
// The part's mesh is read through a reference declared before the profile (a spelling found by
// measurement): it reloads part->mBaseMesh at each use, as retail does ([edi+0x128] at 00493544, 0049360c, 00493624,
// 0049399a and 00493e3a), and puts that member's address first in C2's CSE value numbering (pass 0x107130cb, ids
// base + walk position), ahead of the profile's two values and the frame test's four. The address temporary the anim
// pointer reads the mesh through is then numbered 0 mod 4 (read first in Init's call it was 2 mod 4), its IND key
// (7 + (number mod 4) << 14) sorts below the index product's, and `mAnims + a * 36` evaluates the product first,
// giving retail's registers (00493624..00493637; MCTentacle's UpdateBone, without a profile, gets 0 mod 4 from Init's
// call). Shifting only that number (a diagnostic C2 that burns value ids) has the same effect and nothing else. The
// reference is one more front-end symbol, which moves every later symbol id and with them the stand-ins' rank keys;
// the two wheel-base searches share one function-scope `base` (one symbol fewer), which restores them (with two block
// locals: the ground point's rows 0 and 2, the world product's C10 and the loop's X products differ, 28 instructions).
// Budget: the operator copies (STAND-IN, above) spend more than maths.h's operators, so three single statements are
// braced (Init's call, the anim key's frame and the children's call; no code, 2 own size each). With them the wheel's
// first operator- has 192 (retail's window 189..203), the inverse is denied and the tail's first `+=` is expanded, as
// in retail. Matched: every instruction, register and slot is retail's.
void CMCBuggy::UpdateWheel(FVector pos, FMatrix ori, CMeshPart *part, SINT wheel, SINT frame, float dt)	// FUNCTION: 0x004934f0
{
	CMesh		*&mesh = part->mBaseMesh;
	CMeshPart	*base;
	PROFILE_FN(BuggyUpdateWheel);

	if (!mNumFrames)
	{
		Init(mesh);
	}

	bool				rotated = false;
	FMatrix				m;
	SetIdentity(m);
	IRenderableThing	*thing = mThing;

	// the part's key for the thing's animation frame
	SINT	f;
	UBYTE	key;
	SINT	a;
	if ((thing) && (part->mNumVFrames > 1) && (mesh->mNumAnimModes) && ((a = thing->GetRealAnimIndex()) >= 0))
	{
		CAnimMode	*anim = &mesh->mAnimModes[a];
		f = (SINT(floorf(thing->GetRenderFrame() * anim->mFrameScaler)) + anim->mStart) % part->mNumVFrames;
		if (wheel != -1)
		{
			f = frame;
		}
		key = part->mVHFrameMap[f];
	}
	else
	{
		key = 0;
		f = 0;
		if (wheel != -1)
		{
			f = frame;
			key = part->mVHFrameMap[f];
		}
	}

	// the body settles onto the ground under it
	if (!stricmp(part->mName, "Body"))
	{
		FVector	ground = pos + static_cast<const FBuggyGround &>(ori) * part->GetKeyPosition(0);
		FVector	normal = MAP.Normal(ground);
		normal = normal * 0.01f + mSmoothNormal * 0.99f;
		mSmoothNormal = normal;
		static_cast<FBuggyNormal &>(normal).Normalise();

		GroundBasis(m, normal, FVector(0, -1, 0));

		mBodyOri = m;
		mBodyParentOri = ori;
		mBodyPos = pos;
	}

	// an axle takes the WheelMotion frame that puts its wheel nearest the ground
	if (!strnicmp(part->mName, "Axle", 4))
	{
		wheel = atoi(part->mName + 4);
		FMatrix	base_ori[1];
		FVector	base_pos;
		base = mesh->GetEmitterSubpart("WheelBase", wheel);
		SINT	best = 0;
		FVector	v;
		{
			float	best_dist = 0;
			FMatrix	world = static_cast<const FBuggyWorld &>(mBodyOri) * mThing->GetRenderOrientation();
			FVector	origin;
			origin = mThing->GetRenderPos();
			for (SINT i = 0; i < mNumFrames; i++)
			{
				v = mWheelOffsets[(wheel - 1) * mNumFrames + i];
				v = static_cast<const FBuggyLoop &>(world) * v + origin;
				float	d = fabsf(((MAP.Collide(v) - v.Z)));	// STAND-IN (brackets; see the operator copies' note)
				if ((best_dist == 0) || (d < best_dist))
				{
					best_dist = d;
					best = i;
				}
			}
		}
		frame = mFirstFrame + best;
	}

	// a wheel spins by how far it has rolled
	if (!strnicmp(part->mName, "Wheel", 5))
	{
		FVector	wp = pos + static_cast<const FBuggyWheelPos &>(ori) * part->GetKeyPosition(key);
		FMatrix	wo = static_cast<const FBuggyWheelOri &>(ori) * part->GetHFrameOrientation(key);

		FMatrix	base_ori, part_ori;
		FVector	base_pos, part_pos;
		base = mesh->GetEmitterSubpart("WheelBase", wheel);
		base->GetWorldTransforms(0, 0, 0, mThing, &base_ori, &base_pos, true, 0, 0);
		part->GetWorldTransforms(0, 0, 0, mThing, &part_ori, &part_pos, true, 0, 0);
		FVector	axle;
		axle = base_pos - part_pos;
		base_pos = axle;

		FVector	rolled = wp - mWheelPos[wheel - 1];
		mWheelPos[wheel - 1] = wp;
		rolled = wo.Inverse() * rolled;
		mWheelSpin[wheel - 1] += BuggyAtanf((double)rolled.Y / fabsf(axle.Z));	// STAND-IN (atanf's copy, above)

		FMatrix	spin;
		spin.MakeRotationF(0, mWheelSpin[wheel - 1], 0);
		m = ori * part->GetHFrameOrientation(key);
		m = m * spin;
		rotated = true;
	}

	if (wheel != -1)
	{
		f = frame;
		key = part->mVHFrameMap[f];
	}

	if ((thing) && (!part->mPartNumber))
	{
		pos += thing->GetRenderPos();
		ori = ori * thing->GetRenderOrientation();
	}
	if ((thing) && (!thing->RenderUseHierarchy(part->mPartNumber)))
	{
		pos += part->GetKeyPosition(key);
		if (!rotated)
		{
			ori = part->GetHFrameOrientation(key);
			ori = m * ori;
		}
		else
			ori = m;
	}
	else
	{
		pos += ori * part->GetKeyPosition(key);
		if (!rotated)
		{
			ori = ori * part->GetHFrameOrientation(key);
			ori = m * ori;
		}
		else
			ori = m;
	}

	mOldPartPos[part->mPartNumber] = mPartPos[part->mPartNumber];
	mOldPartOri[part->mPartNumber] = mPartOri[part->mPartNumber];
	mOldPartFrame[part->mPartNumber] = mPartFrame[part->mPartNumber];
	mPartPos[part->mPartNumber] = pos;
	mPartOri[part->mPartNumber] = ori;
	mPartFrame[part->mPartNumber] = (float)f;

	for (SINT c = 0; c < part->mNumChildren; c++)
	{
		UpdateWheel(pos, ori, part->mChildren[c], wheel, frame, dt);
	}
}

// The profile's destructor, the matrix inverse and FVector's division, emitted here out of line.
// FUNCTION: 0x00494310 ??1CProfileFn@@QAE@XZ
// FUNCTION: 0x00494350 ?Inverse@FMatrix@@QBE?AV1@XZ
// FUNCTION: 0x004944b0 ??_0FVector@@QAEXM@Z

//******************************************************************************************
// Initialise the root basis separately from construction. The first row expands and the other two remain
// calls here, as in retail; the origin uses Set so the later position interpolation also expands completely.
// This helper name and source boundary are reconstructed.
static inline void InitBuggyRootOrientation(FMatrix &orientation)
{
	orientation.Row[0] = FVector(1, 0, 0);
	orientation.Row[1] = FVector(0, 1, 0);
	orientation.Row[2] = FVector(0, 0, 1);
}

void CMCBuggy::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x004944d0
{
	if (EVENT_MANAGER.GetTime() != mLastTime)
	{
		FVector		origin;
		origin.Set(0, 0, 0);
		FMatrix		m;
		InitBuggyRootOrientation(m);
		CMeshPart	*root = part->mBaseMesh->mParts[0];
		UpdateWheel(origin, m, root, -1, 0, ENGINE.GetRenderTime() - mLastTime);
		mLastTime = ENGINE.GetRenderTime();
	}

	if (mOldPartFrame[part->mPartNumber] != -1.0f)
	{
		*pos = mOldPartPos[part->mPartNumber] + (mPartPos[part->mPartNumber] - mOldPartPos[part->mPartNumber]) * GAME.GetFrameRenderFraction();
		*ori = mOldPartOri[part->mPartNumber] + (mPartOri[part->mPartNumber] - mOldPartOri[part->mPartNumber]) * GAME.GetFrameRenderFraction();
		ori->Normalise();
	}
	else
	{
		*pos = mPartPos[part->mPartNumber];
		*ori = mPartOri[part->mPartNumber];
	}

	if (world)
	{
		*pos -= mThing->GetRenderPos();
		*pos = mThing->GetRenderOrientation().Inverse() * *pos;
		*ori = mThing->GetRenderOrientation().Inverse() * *ori;
	}
}

//******************************************************************************************
void CMCBuggy::AdjustFrame(CMeshPart *part, float *frame)	// FUNCTION: 0x00494940
{
	if (EVENT_MANAGER.GetTime() != mLastTime)
	{
		FVector		origin(0, 0, 0);
		FMatrix		m(1, 0, 0, 0, 1, 0, 0, 0, 1);
		CMeshPart	*root = part->mBaseMesh->mParts[0];
		UpdateWheel(origin, m, root, -1, 0, ENGINE.GetRenderTime() - mLastTime);
		mLastTime = ENGINE.GetRenderTime();
	}

	if (mOldPartFrame[part->mPartNumber] != -1.0f)
		*frame = (mPartFrame[part->mPartNumber] - mOldPartFrame[part->mPartNumber]) * GAME.GetFrameRenderFraction() + mOldPartFrame[part->mPartNumber];
	else
		*frame = mPartFrame[part->mPartNumber];
}

//******************************************************************************************
BOOL CMCBuggy::Unknown8()	// FUNCTION: 0x00494ae0
{
	return EVENT_MANAGER.GetTime() != mLastTime;
}

//******************************************************************************************
BOOL CMCBuggy::CanMergePart(CMeshPart *part)	// FUNCTION: 0x00494b00
{
	if (!stricmp(part->mName, "Body"))
		return FALSE;
	if (!strnicmp(part->mName, "Axle", 4))
		return FALSE;
	return strnicmp(part->mName, "Wheel", 5) != 0;
}

//******************************************************************************************
BOOL CMCBuggy::HasWheelMotion(CMesh *mesh)	// FUNCTION: 0x00494b50
{
	return mesh->GetAnimModeByName("WheelMotion") != AM_INVALID;
}
