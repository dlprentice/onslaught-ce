// MCTentacle.cpp: reconstructed from MCTentacle.obj (0049c9e0..0049ed7f); Stuart's release has no copy. A
// tentacle's motion controller: the bones follow a cubic Bezier curve from the tether to the head.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "MCTentacle.h"
#include "mesh.h"
#include "renderablething.h"
#include "engine.h"
#include "game.h"
#include "eventmanager.h"
#include "component.h"
#include "DebugLog.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// FUNCTION: 0x0049c9e0 init:ZERO_FVECTOR
// FUNCTION: 0x0049ca00 init:ID_FMATRIX

//******************************************************************************************
CMCTentacle::CMCTentacle(IRenderableThing *thing)	// FUNCTION: 0x0049cad0
{
	mThing = thing;
	mPartPos = NULL;
	mPartOri = NULL;
	mPartFrame = NULL;
	mOldPartOri = NULL;
	mOldPartPos = NULL;
	mOldPartFrame = NULL;
	mBones = NULL;
	mBoneOri = NULL;
	mBonePos = NULL;
	mInitialised = FALSE;
	mLastTime = -1.0f;
}

// FUNCTION: 0x0049cb20 ??_GCMCTentacle@@UAEPAXI@Z

//******************************************************************************************
CMCTentacle::~CMCTentacle()	// FUNCTION: 0x0049cb40
{
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
	if (mBonePos)
		delete [] mBonePos;
	if (mBoneOri)
		delete [] mBoneOri;
	if (mBones)
		delete [] mBones;
}

//******************************************************************************************
// A part's name and a mesh's part count through accessors (names and signatures ours: retail's mesh.h probably had
// CMeshPart::GetName and CMesh::GetNumParts beside GetPart, but two more inline members there move
// CD3DApplication::BuildDeviceList 00529350 out of its symbol-count window, so they are file-local here).
static inline char *NameOf(CMeshPart *part) { return part->mName; }
static inline SINT NumParts(CMesh *mesh) { return mesh->mNumParts; }

// Sizes everything from the mesh and finds the named parts, the first time a bone is updated. What each piece of the
// source answers in retail:
//  - every part fetch is CMesh::GetPart(i): the inline's result is a named web, so each `mParts[i]->mName` chain keeps
//    eax and does not move the scratch rotation (a direct `mParts[i]` takes the rotation: strnicmp/atoi/sprintf registers
//    all off by one);
//  - the last loop is an empty `for (i = 0; i < mNumBones; i++)` (an assertion, compiled away in a release build): retail
//    stores `i = mNumBones` on both exits;
//  - pos is live from function scope, so the p-loop's FVector temp shares `name`'s frame slot instead of pos's;
//  - the names and the part count read through accessors: their expansions cost the ids (about four a site) that put
//    the strength-reduced `i * 4` temp at 1037 instead of 975, so its key wraps (0x0340 against 0xf3c0) below the IND of
//    `mesh->mParts` (0x8007) and the bone scan's address is `[ecx+ebx]`, as in retail.
void CMCTentacle::Init(CMesh *mesh)	// FUNCTION: 0x0049cc40
{
#line 68
	mPartPos = new(MEMTYPE_MOTIONCONTROLLER) FVector[NumParts(mesh)];
	mPartOri = new(MEMTYPE_MOTIONCONTROLLER) FMatrix[NumParts(mesh)];
	SINT	parts = NumParts(mesh);	mPartFrame = new(MEMTYPE_MOTIONCONTROLLER) float[parts];

	mOldPartPos = new(MEMTYPE_MOTIONCONTROLLER) FVector[NumParts(mesh)];
	mOldPartOri = new(MEMTYPE_MOTIONCONTROLLER) FMatrix[NumParts(mesh)];
	SINT	oldparts = NumParts(mesh);	mOldPartFrame = new(MEMTYPE_MOTIONCONTROLLER) float[oldparts];
	for (SINT p = 0; p < NumParts(mesh); p++)
	{
		mPartFrame[p] = -1.0f;
		mOldPartFrame[p] = -1.0f;
		mOldPartPos[p] = FVector(0, 0, 0);
	}

	mTether = NULL;
	mHead = NULL;
	mHeadCP = NULL;
	mTetherCP = NULL;
	mTentacle = NULL;
	mNumBones = 0;

	SINT	i;
	FVector	pos;
	FMatrix	ori;
	for (i = 0; i < NumParts(mesh); i++)
	{
		if (!strnicmp(NameOf(mesh->GetPart(i)), "Bone", 4))
		{
			SINT	n = atoi(NameOf(mesh->GetPart(i)) + 4);
			if (n > mNumBones)
				mNumBones = n;
		}
	}

	char	text[256];
	sprintf(text, "Got %d bones in tentacle\n", mNumBones);
	TRACE(text);

#line 108
	mBonePos = new(MEMTYPE_MOTIONCONTROLLER) FVector[mNumBones];
	mBoneOri = new(MEMTYPE_MOTIONCONTROLLER) FMatrix[mNumBones];
	mBones = new(MEMTYPE_MOTIONCONTROLLER) CMeshPart *[mNumBones];

	for (i = 0; i < NumParts(mesh); i++)
	{
		if (!stricmp(NameOf(mesh->GetPart(i)), "tether"))
		{
			mesh->GetPart(i)->GetWorldTransforms(0, 0, 0, mThing, &ori, &pos, false, 0, 0);
			mTetherPos = pos;
			mTether = mesh->GetPart(i);
		}
		if (!stricmp(NameOf(mesh->GetPart(i)), "head"))
			mHead = mesh->GetPart(i);
		if (!stricmp(NameOf(mesh->GetPart(i)), "tentacle"))
		{
			mesh->GetPart(i)->GetWorldTransforms(0, 0, 0, mThing, &ori, &pos, false, 0, 0);
			mTentacle = mesh->GetPart(i);
			mTentaclePos = pos;
			sprintf(text, "TPos at %f,%f,%f\n", pos.X, pos.Y, pos.Z);
			TRACE(text);
		}
		if (!stricmp(NameOf(mesh->GetPart(i)), "tethercp"))
		{
			mesh->GetPart(i)->GetWorldTransforms(0, 0, 0, mThing, &ori, &pos, false, 0, 0);
			mTetherCPPos = pos;
			mTetherCP = mesh->GetPart(i);
		}
		if (!stricmp(NameOf(mesh->GetPart(i)), "headcp"))
			mHeadCP = mesh->GetPart(i);

		for (SINT b = 0; b < mNumBones; b++)
		{
			char	name[32];
			if (b < 9)
				sprintf(name, "bone0%d", b + 1);
			else
				sprintf(name, "bone%d", b + 1);
			if (!stricmp(NameOf(mesh->GetPart(i)), name))
				mBones[b] = mesh->GetPart(i);
		}
	}

	for (i = 0; i < mNumBones; i++)
		ASSERT(mBones[i]);

	mInitialised = TRUE;
}

// STAND-IN (ids) for CMCTentacle::UpdateBone below: the bone's quarter turn through a file-local copy of maths.h's
// MakeRotationF whose sines take their angles as three more parameters, and whose roll goes through a copy. Retail's
// inline products of the turn (0049d538, 0049d560, 0049d5b6..0049d5cc) need the trig values' rank keys, which follow
// symbol ids, as {cp, sp} above {cr, sr} above {cy, sy}. With maths.h's MakeRotationF the locals take ids the identity
// `m` freed (cy 0x14c, sy 0x143, cr 0x156, sr 0x157, cp 0x151, sp 0x155: cr and sr rank above cp and sp). Here the six
// angle parameters and the copy of the roll take the free list's top eight ids first, so the locals get 0x151, 0x155
// and four fresh ids in declaration order (cy 0x151, sy 0x155, cr 0x15a, sr 0x15b, cp 0x15c, sp 0x15d), the order
// retail's keys need. The parameters are bound to constants: they are substituted and their ids are freed when the
// expansion ends, while the copy keeps its id. That is the flow measured with four unused parameters (four
// more ids at the start, three freed and one kept), which also gives retail's later rows (0049d6c1 and
// 0049d912..0049d937).
// Here every value is consumed: each parameter feeds one cosf or sinf, and the copy feeds the roll's cosf. Measured,
// with the rest of UpdateBone as committed: without the copy of the roll 94/198; maths.h's MakeRotationF 10/18 (budget
// held); four copies with maths.h's signature 70/224 (own size 986); one more parameter and three copies also match
// (own size 988, the window's edge); any one angle can be the copied one. The copy is one inline site, as MakeRotationF
// is, and spends 268 of the budget where MakeRotationF spends 260, which changes no decision; it emits MakeRotationF's
// code and nothing more. Original spelling unknown.
struct FTentacleRotation : public FMatrix
{
	void	MakeRotationF(float yaw, float pitch, float roll, float syaw, float spitch, float sroll)
	{
		float	r = roll;
		float cy = cosf(yaw), sy = sinf(syaw);
		float cr = cosf(r), sr = sinf(sroll);
		float cp = cosf(pitch), sp = sinf(spitch);
		Row[0].X = (((cr * cy - sp * sr * sy)));	Row[0].Y = (((-cp * sy)));	Row[0].Z = ((sp * cr * sy + sr * cy));
		Row[1].X = ((sp * sr * cy + cr * sy));	Row[1].Y = (((cp * cy)));		Row[1].Z = (sr * sy - sp * cr * cy);
		Row[2].X = (-cp * sr);				Row[2].Y = sp;			Row[2].Z = (cp * cr);
	}
};

//******************************************************************************************
// Places a part: the tether and the head where the curve's ends are, each bone on the curve turned along it, and
// every other part from its keys; then its children.
// Matched (on a measured budget window, verbose forms and id
// measurements) with code-neutral STAND-INs. Specimen: BEA.exe.original.backup, SHA-256
// 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750.
// - The written-out tests and braces (`mInitialised == FALSE`, `thing != NULL`, braced single-statement bodies) are
//   Stuart's style (eventmanager.cpp:174 `if (mValid == FALSE)`, activereader.cpp:13 braces), not a concession. They also
//   set the budget: retail expands the identity's second row FVector and the hierarchical `pos +=` and calls the flat
//   case's (the out-of-line AddInPlace), which needs own size 988..1001. This source measures 992 (decisions
//   EEEEEEEDEDDDD, retail's); with the terse forms 952 (143/301). Relabelled in the 2026-10-14 natural-form review.
// - STAND-IN (ids): the children are updated with snapshots of `unused` and `dt` (`SINT u = unused; float t = dt;`),
//   and the two flags are declared before `m`. Both are new or reordered front-end symbols, which move the ids that
//   later inline sites take. With the budget held: without the snapshots 18/22, with one of them 12/14, with `m`
//   declared first 10/12.
// - The bone's matrix is read through a named const reference, `const FMatrix &bone = mBoneOri[b];`, as Stuart names
//   them (DXEngine.cpp:458 `const SHeightFieldProperties &hfp = ...`); it also sets ids: without it, budget held, 6/8.
// - STAND-IN (ids): FTentacleRotation, the turn's MakeRotationF copy above (see there).
// The struct, the reference, the snapshots and the verbose forms emit no code of their own.
void CMCTentacle::UpdateBone(FVector pos, FMatrix ori, CMeshPart *part, SINT unused, float dt)	// FUNCTION: 0x0049d280
{
	if (mInitialised == FALSE)
	{
		Init(part->mBaseMesh);
	}

	bool				rotated = false;
	bool				moved = false;
	FMatrix				m(1, 0, 0, 0, 1, 0, 0, 0, 1);
	FVector				offset(0, 0, 0);
	IRenderableThing	*thing = mThing;

	// the part's key for the thing's animation frame
	SINT	f;
	UBYTE	key;
	SINT	a;
	if ((thing != NULL) && (part->mNumVFrames > 1) && (part->mBaseMesh->mNumAnimModes != 0) && ((a = thing->GetRealAnimIndex()) >= 0))
	{
		CAnimMode	*anim = &part->mBaseMesh->mAnimModes[a];
		f = (SINT(floor(thing->GetRenderFrame() * anim->mFrameScaler)) + anim->mStart) % part->mNumVFrames;
		key = part->mVHFrameMap[f];
	}
	else
	{
		key = 0;
		f = 0;
	}

	if (part == mTether)
	{
		moved = true;
		offset = mTetherPos;
	}
	else if (part != mTentacle)
	{
		if (part == mHead)
		{
			moved = true;
			rotated = true;
			offset = mHeadPos;
			m = mHeadOri;
		}
		else
		{
			for (SINT b = 0; b < mNumBones; b++)
			{
				if (part == mBones[b])
				{
					moved = true;
					offset = mBonePos[b];
					// a separate matrix built by MakeRotationF, which the inliner then expands (retail computes the cosines and sines of 0 and
					// -1.57 inline); as the temporary FMatrix(0, 0, -1.57f) the constructor's nested budget refuses MakeRotationF
					FTentacleRotation	rot;
					rot.MakeRotationF(0, 0, -1.57f, 0, 0, -1.57f);
					const FMatrix	&bone = mBoneOri[b];
					m = bone * rot;
					rotated = true;
				}
			}
		}
	}

	if ((thing != NULL) && (part->mPartNumber == 0))
	{
		pos += thing->GetRenderPos();
		ori = ori * thing->GetRenderOrientation();
	}
	// The hierarchical case comes first in retail's inline order: its `pos += ori * key` expands while the flat case's `pos += key`
	// is the out-of-line AddInPlace (the budget is nearly spent by then).
	if ((thing == NULL) || (thing->RenderUseHierarchy(part->mPartNumber)))
	{
		if (moved == false)
		{
			pos += ori * part->mHFramePositions[key];
		}
		else
		{
			pos = offset;
		}
		if (rotated == false)
		{
			ori = ori * part->mHFrameOrientations[key];
			ori = m * ori;
		}
		else
		{
			ori = m;
		}
	}
	else
	{
		if (moved == false)
		{
			pos += part->mHFramePositions[key];
		}
		else
		{
			pos = offset;
		}
		if (rotated == false)
		{
			ori = part->mHFrameOrientations[key];
			ori = m * ori;
		}
		else
		{
			ori = m;
		}
	}

	mOldPartPos[part->mPartNumber] = mPartPos[part->mPartNumber];
	mOldPartOri[part->mPartNumber] = mPartOri[part->mPartNumber];
	mOldPartFrame[part->mPartNumber] = mPartFrame[part->mPartNumber];
	mPartPos[part->mPartNumber] = pos;
	mPartOri[part->mPartNumber] = ori;
	mPartFrame[part->mPartNumber] = (float)f;

	SINT	u = unused;
	float	t = dt;
	for (SINT c = 0; c < part->mNumChildren; c++)
	{
		UpdateBone(pos, ori, part->mChildren[c], u, t);
	}
}

//******************************************************************************************
SINT Factorial(SINT n)	// FUNCTION: 0x0049dc90
{
	SINT	r = 1;
	for (SINT i = 2; i <= n; i++)
		r *= i;
	return r;
}

//******************************************************************************************
float Power(float x, SINT n)	// FUNCTION: 0x0049dcb0
{
	float	r = 1.0f;
	for (; n > 0; n--)
		r *= x;
	return r;
}

//******************************************************************************************
// The curve's control points: the tether and its control point where the mesh has them, the head where the unit
// puts it, and the head's control point carried with the head. Each bone takes its point on the curve and faces the
// next one.
void CMCTentacle::UpdateSpline(CMesh *mesh)	// FUNCTION: 0x0049dcd0
{
	if (!mInitialised)
		Init(mesh);

	FMatrix	ori[1];
	FVector	pos;
	mHead->GetWorldTransforms(0, 0, 0, mThing, &ori[0], &pos, false, 0, 0);
	FVector	head_pos; head_pos = pos;
	FMatrix	head_ori = ori[0];

	CTentacle	*tentacle = (CTentacle *)mThing;
	tentacle->GetHeadPos(&mHeadPos);
	tentacle->GetHeadOri(&mHeadOri);

	mHeadCP->GetWorldTransforms(0, 0, 0, mThing, &ori[0], &pos, false, 0, 0);
	FVector	offset = pos - head_pos;
	FMatrix	inverse = head_ori.Inverse();
	pos = inverse * offset;
	ori[0] = ori[0] * head_ori.Inverse();
	pos = mHeadOri * pos + mHeadPos;
	ori[0] = ori[0] * mHeadOri;
	mHeadCPPos = pos;

	for (SINT i = 0; i < mNumBones; i++)
	{
		float	t = float(i);
		t /= (mNumBones - 1);
		FVector	p(0, 0, 0);
		float	b[4];
		b[0] = t;
		b[1] = 0;
		b[2] = 0;
		float	u = 1.0f - t;
		b[3] = u;

		for (SINT k = 0; k < 4; k++)
		{
			float	c = float(Factorial(3) / (Factorial(k) * Factorial(3 - k)));
			b[k] = (c * Power(u, 3 - k)) * Power(t, k);
		}

		p += mHeadPos * b[3];
		p += mHeadCPPos * b[2];
		p += mTetherCPPos * b[1];
		p += mTetherPos * b[0];
		mBonePos[i] = p;

		if (i)
			mBoneOri[i - 1].MakeFromDirection(mBonePos[i - 1] - mBonePos[i], FVector(1, 0, 0));
	}
	mBoneOri[mNumBones - 1].MakeFromDirection(mHeadCPPos - mHeadPos, FVector(1, 0, 0));
}

// FUNCTION: 0x0049e4b0 ?MakeFromDirection@FMatrix@@QAEXVFVector@@0@Z

//******************************************************************************************
// Initialise the root basis separately from construction. The first row expands and the other two remain
// calls here, as in retail; the origin uses Set so the later position interpolation also expands completely.
// This helper name and source boundary are reconstructed.
static inline void InitTentacleRootOrientation(FMatrix &orientation)
{
	orientation.Row[0] = FVector(1, 0, 0);
	orientation.Row[1] = FVector(0, 1, 0);
	orientation.Row[2] = FVector(0, 0, 1);
}

void CMCTentacle::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x0049e660
{
	if (EVENT_MANAGER.GetTime() != mLastTime)
	{
		FVector		origin;
		origin.Set(0, 0, 0);
		FMatrix		m;
		InitTentacleRootOrientation(m);
		UpdateSpline(part->mBaseMesh);
		CMeshPart	*root = part->mBaseMesh->mParts[0];
		UpdateBone(origin, m, root, 0, ENGINE.GetRenderTime() - mLastTime);
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
void CMCTentacle::AdjustFrame(CMeshPart *part, float *frame)	// FUNCTION: 0x0049ead0
{
	if (EVENT_MANAGER.GetTime() != mLastTime)
	{
		FVector		origin(0, 0, 0);
		FMatrix		m(1, 0, 0, 0, 1, 0, 0, 0, 1);
		UpdateSpline(part->mBaseMesh);
		CMeshPart	*root = part->mBaseMesh->mParts[0];
		UpdateBone(origin, m, root, 0, ENGINE.GetRenderTime() - mLastTime);
		mLastTime = ENGINE.GetRenderTime();
	}

	if (mOldPartFrame[part->mPartNumber] != -1.0f)
		*frame = (mPartFrame[part->mPartNumber] - mOldPartFrame[part->mPartNumber]) * GAME.GetFrameRenderFraction() + mOldPartFrame[part->mPartNumber];
	else
		*frame = mPartFrame[part->mPartNumber];
}

//******************************************************************************************
BOOL CMCTentacle::Unknown8()	// FUNCTION: 0x0049ec80
{
	return EVENT_MANAGER.GetTime() != mLastTime;
}

//******************************************************************************************
BOOL CMCTentacle::CanMergePart(CMeshPart *part)	// FUNCTION: 0x0049eca0
{
	if (!stricmp(part->mName, "tether"))
		return FALSE;
	if (!stricmp(part->mName, "head"))
		return FALSE;
	if (!stricmp(part->mName, "tethercp"))
		return FALSE;
	if (!stricmp(part->mName, "headcp"))
		return FALSE;
	if (!stricmp(part->mName, "tentacle"))
		return FALSE;
	return strnicmp(part->mName, "bone", 4) != 0;
}

//******************************************************************************************
BOOL CMCTentacle::HasTentacle(CMesh *mesh)	// FUNCTION: 0x0049ed30
{
	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		CMeshPart	*part = mesh->mParts[i];
		if (!stricmp(part->mName, "tentacle"))
			return TRUE;
	}
	return FALSE;
}
