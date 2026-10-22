#line 1 "C:\\dev\\ONSLAUGHT2\\MCMech.cpp"
// MCMech.cpp: reconstructed from MCMech.obj (004982c0..0049c2ef); Stuart's release has no copy (his BattleEngine.cpp
// uses CMCBattleEngine, a CMCMech). A walker's motion controller: it finds the mesh's legs ("Footbase" emitters),
// feet and toes, keeps each leg's length and orientation for every LegMotion frame (shared by every mech on the
// same mesh), and each frame plants the feet on the ground. It keeps its profile like MCBuggy.obj (UpdateBone opens
// profile 73). The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#define ENABLE_PROFILER 1
#define MCMECH_CPP	// maths.h's private Mech members (maths.h is not in the PCH)

#include "game.h"
#include "BattleEngine.h"
#include "MCBattleEngine.h"
#include "mesh.h"
#include "map.h"
#include "world.h"
#include "thing.h"
#include "Line.h"
#include "renderablething.h"
#include "RenderThing.h"
#include "game.h"
#include "eventmanager.h"
#include "profile.h"
#include "DebugLog.h"

#include <string.h>
#include <stdlib.h>

// FUNCTION: 0x004982c0 init:ZERO_FVECTOR
// FUNCTION: 0x004982e0 init:ID_FMATRIX

CMCMech	*CMCMech::sFirst = NULL;

//******************************************************************************************
CMCMech::CMCMech(IRenderableThing *thing)	// FUNCTION: 0x004983b0
{
	mParamD = 0.4f;
	mThing = thing;
	mNumFrames = 0;
	mLegPos = NULL;
	mLegA = NULL;
	mLegB = NULL;
	mLegC = NULL;
	mLegTarget = NULL;
	mPartPos = NULL;
	mPartOri = NULL;
	mPartFrame = NULL;
	mOldPartOri = NULL;
	mOldPartPos = NULL;
	mOldPartFrame = NULL;
	mLastTime = -1.0f;
	mUnknown88.Set(0, 0, 0);
	mParamA = 0;
	mParamB = 200.0f;
	mParamC = 1.0f;
	mParamF = 0.05f;
	mToeFirstFrame = 0;
	mToeLastFrame = 0;
	mNumToes = 0;
	mParamE = 0.9f;
	mToeFrames = NULL;
	mAnimationBlend = 0;
	mUsed = FALSE;
	mWasUsed = FALSE;
	mMesh = NULL;
	mNumLegs = 0;
	mProcessCount = 0;
	mUnknownD0 = 0;
	mUnknownD4 = FVector(0, 0, 0);
	EVENT_MANAGER.AddEvent(3000, this, NEXT_FRAME);
	mShared = NULL;
	mNeedsUpdate = TRUE;
	mNext = sFirst;
	sFirst = this;
}

// FUNCTION: 0x00498510 ??_GCMCMech@@UAEPAXI@Z

//******************************************************************************************
CMCMech::~CMCMech()	// FUNCTION: 0x00498530
{
	CMCMech	*m;
	for (m = sFirst; m; m = m->mNext)
		if (m->mNext == this)
			break;
	if (m)
		m->mNext = mNext;
	else
		sFirst = mNext;

	// the last mech on its mesh frees what they shared
	BOOL	last = TRUE;
	for (m = sFirst; m; m = m->mNext)
		if (m->mShared == mShared)
			last = FALSE;
	if (last)
	{
		SINT	i;
		if (mNumFrames)
		{
			for (i = 0; i < mNumFrames; i++)
				SAFE_DELETE_ARRAY(mShared->mLegFrames[i]);
			SAFE_DELETE_ARRAY(mShared->mLegFrames);
			for (i = 0; i < mNumFrames; i++)
				SAFE_DELETE_ARRAY(mShared->mLegOris[i]);
			SAFE_DELETE_ARRAY(mShared->mLegOris);
		}
		if (mShared)
		{
			if (mShared->mToeOffsets)
			{
				for (i = 0; i < mNumLegs * mNumToes; i++)
					SAFE_DELETE_ARRAY(mShared->mToeOffsets[i]);
				SAFE_DELETE_ARRAY(mShared->mToeOffsets);
			}
			SAFE_DELETE_ARRAY(mShared->mPartTypes);
			SAFE_DELETE_ARRAY(mShared->mPartLegs);
		}
		SAFE_DELETE(mShared);
	}

	if (mLegPos)
		delete [] mLegPos;
	if (mLegA)
		delete [] mLegA;
	if (mLegB)
		delete [] mLegB;
	if (mLegC)
		delete [] mLegC;
	if (mLegTarget)
		delete [] mLegTarget;
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
	if (mToeFrames)
		delete [] mToeFrames;
}

//******************************************************************************************
void CMCMech::HandleEvent(CEvent *event)	// FUNCTION: 0x00498870
{
	if (event->GetEventNum() == 3000)
	{
		ProcessMovement(FALSE, FALSE);
		EVENT_MANAGER.AddEvent(3000, this, NEXT_FRAME, START_OF_FRAME, NULL, (CScheduledEvent *)event);
	}
}

//******************************************************************************************
// Rebuild the bones: each frame when something asked for a transform, else every few frames.
void CMCMech::ProcessMovement(BOOL force, BOOL fixed_step)	// FUNCTION: 0x004988b0
{
	mWasUsed = mUsed;
	if ((!mUsed) && (GAME.GetGameState() >= GAME_STATE_PANNING) && (!force) && (mProcessCount >= 5))
		return;
	if (!mThing)
		return;
	CRenderThing	*rt = ((CThing *)mThing)->GetRenderThing();
	if (!rt)
		return;
	CMESH	*mesh = rt->GetRTMesh();
	if (!mesh)
		return;
	CMeshPart	*root = mesh->mParts[0];
	if (!root)
		return;

	mProcessCount++;

	FVector	pos(0, 0, 0);
	FMatrix	ori;
	ori.Row[0] = FVector(1, 0, 0);
	ori.Row[1] = FVector(0, 1, 0);
	ori.Row[2] = FVector(0, 0, 1);
	if (!force)	// force again (edi at 004989bf): retail never reads fixed_step
		UpdateBoneHierarchyRecursive(pos, ori, root, -1, 0, EVENT_MANAGER.GetTime() - mLastTime, 0);
	else
		UpdateBoneHierarchyRecursive(pos, ori, root, -1, 0, 0.05f, 0);

	mLastTime = EVENT_MANAGER.GetTime();
	mUsed = FALSE;
	mNeedsUpdate = TRUE;
}

//******************************************************************************************
void CMCMech::SetMechParameters(float a, float b, float c, float d, float e, float f, int g)	// FUNCTION: 0x00498bf0
{
	mParamA = a;
	mParamB = b;
	mParamC = c;
	mParamF = f;
	mParamD = d;
	mParamE = e;
	mParamG = g;
}

//******************************************************************************************
// A mesh's part count through an accessor, as in CMCTentacle::Init (name and signature ours: retail's mesh.h probably has
// CMesh::GetNumParts beside GetPart, but a header member moves CD3DApplication::BuildDeviceList 00529350 out of its
// symbol-count window, so it is file-local). Its expansions at the eight allocation reads of the count give the first
// part-type array retail's `lea edx,[eax*4]` count form (004990ad); the two loop bounds read the member directly
// (measured: reading the member at the first two allocations as well loses Init, 1369 bytes).
static inline SINT NumParts(CMesh *mesh) { return mesh->mNumParts; }

//******************************************************************************************
// STAND-IN: MCMech is absent from Stuart's released source. These CPP-local copies of the
// existing inverse and matrix/vector operations, and their parentheses, reproduce the compiled
// retail operation; their original owners and spelling remain unknown. No arithmetic association,
// precision, callback, allocation metadata or runtime effect is added or changed.
// Plain form: spos = lori.Inverse() * rel; then publish spos into mToeOffsets. Reusing the already
// consumed i counter for both frame and toe walks leaves 141 byte differences. The plain copied
// inverse member is graph-neutral to its control; the plain ToeOffsetProduct owner is graph-neutral
// to the later 14-byte control. The following grammar is the deliberate fallback:
// - move whole-product groups onto retail's first-loaded fields in four cofactor/determinant products;
// - move an existing row-1 group to its second product, and retain only whole minors in row 2;
// - group the complete X and Y product sums once each. Associations stay unchanged.
// VC6's field ranking and 81-node scheduling regions then reproduce the cofactor copy, the index
// calculation during the fourth division, and the final four-word publication order. The entire
// 3968-byte section and all 121 relocations match; all 50 prior sibling callables are unchanged.
// Replace this stand-in when original source or a better-supported natural form passes that same
// complete section/relocation and sibling gate. The retained controls are in the ignored matching owner.
struct MechToeMatrix : public FMatrix
{
	FMatrix	Cofactors() const
	{
		FMatrix	c;
		c.Row[0].X =  (Row[1].Y * (Row[2].Z) - (Row[1].Z * Row[2].Y));
		c.Row[0].Y = -(Row[1].X * (Row[2].Z) - Row[2].X * (Row[1].Z));
		c.Row[0].Z =  ((Row[1].X * Row[2].Y) - Row[2].X * (Row[1].Y));
		c.Row[1].X = -((Row[0].Y * Row[2].Z) - (Row[0].Z * Row[2].Y));
		c.Row[1].Y =  (Row[0].X * Row[2].Z - (Row[2].X * Row[0].Z));
		c.Row[1].Z = -((Row[0].X * Row[2].Y) - ((Row[2].X * Row[0].Y)));
		c.Row[2].X =  (Row[1].Z * Row[0].Y - Row[0].Z * Row[1].Y);
		c.Row[2].Y = -(Row[1].Z * Row[0].X - Row[1].X * Row[0].Z);
		c.Row[2].Z =  (Row[1].Y * Row[0].X - Row[1].X * Row[0].Y);
		return c;
	}
	// Keep the existing named minor, which separates determinant and cofactor CSE.
	float	Determinant() const
	{
		float	m0 = Row[1].Y * (Row[2].Z) - (Row[1].Z * Row[2].Y);
		return (m0 * Row[0].X)
			 - ((Row[1].X * (Row[2].Z) - Row[2].X * (Row[1].Z)) * Row[0].Y)
			 + (((Row[1].X * Row[2].Y) - Row[2].X * (Row[1].Y)) * Row[0].Z);
	}
	// Copy the consumed inverse operation; its result still uses the existing FMatrix row division.
	FMatrix	Inverse() const
	{
		FMatrix	r = Cofactors();
		r.TransposeInPlace();
		r /= Determinant();
		return r;
	}
};

static inline FVector ToeOffsetProduct(const FMatrix &m, const FVector &v)
{
	return FVector(
		((m.Row[0].X * v.X) + (m.Row[0].Y * v.Y) + (m.Row[0].Z * v.Z)),
		((m.Row[1].X * v.X) + (m.Row[1].Y * v.Y) + (m.Row[1].Z * v.Z)),
		(m.Row[2].X * v.X) + (m.Row[2].Y * v.Y) + (m.Row[2].Z * v.Z));
}

// Sizes everything from the mesh the first time the bones are updated.

void CMCMech::Init(CMesh *mesh)	// FUNCTION: 0x00498c40
{
	mMesh = mesh;

	// share with another mech on the same mesh
	for (CMCMech *m = sFirst; m; m = m->mNext)
		if ((m->mMesh == mesh) && (m != this))
			mShared = m->mShared;
	if (!mShared)
#line 249
		mShared = new(MEMTYPE_MOTIONCONTROLLER) SMechShared;

	mNumLegs = 1;
	while (mesh->GetEmitterSubpart("Footbase", mNumLegs))
		mNumLegs++;
	mNumLegs--;
	mNumToes = 1;
	while (mesh->GetEmitterSubpart("toestop", mNumToes))
		mNumToes++;
	mNumToes = (mNumToes - 1) / mNumLegs;
	if (!mNumToes)
		mNumToes = 1;

#line 267
	mLegPos = new(MEMTYPE_MOTIONCONTROLLER) FVector[mNumLegs];
	mLegA = new(MEMTYPE_MOTIONCONTROLLER) BOOL[mNumLegs];
	mLegB = new(MEMTYPE_MOTIONCONTROLLER) float[mNumLegs];
	mLegC = new(MEMTYPE_MOTIONCONTROLLER) SINT[mNumLegs];
	mLegTarget = new(MEMTYPE_MOTIONCONTROLLER) FVector[mNumLegs];

#line 273
	mToeFrames = new(MEMTYPE_MOTIONCONTROLLER) SINT[mNumLegs * mNumToes];
	CAnimMode	*anim = mesh->GetAnimModePtr(mesh->GetAnimModeByName("ToeMotion"));
	if (anim)
	{
		mToeFirstFrame = anim->mStart;
		mToeLastFrame = anim->mLast;
	}

	SINT	i, j;
	for (i = 0; i < mNumLegs; i++)
	{
		mLegB[i] = 0;
		mLegPos[i].Set(0, 0, 0);
		mLegC[i] = 0;
		mLegTarget[i].Set(0, 0, 0);
		for (j = 0; j < mNumToes; j++)
			mToeFrames[i * mNumToes + j] = mToeFirstFrame;
	}

#line 304
	mPartPos = new(MEMTYPE_MOTIONCONTROLLER) FVector[NumParts(mesh)];
	mPartOri = new(MEMTYPE_MOTIONCONTROLLER) FMatrix[NumParts(mesh)];
	mPartFrame = new(MEMTYPE_MOTIONCONTROLLER) float[NumParts(mesh)];

#line 308
	mOldPartPos = new(MEMTYPE_MOTIONCONTROLLER) FVector[NumParts(mesh)];
	mOldPartOri = new(MEMTYPE_MOTIONCONTROLLER) FMatrix[NumParts(mesh)];
	mOldPartFrame = new(MEMTYPE_MOTIONCONTROLLER) float[NumParts(mesh)];

	if (!mShared->mPartTypes)
#line 313
		mShared->mPartTypes = new(MEMTYPE_MOTIONCONTROLLER) SINT[NumParts(mesh)];
	if (!mShared->mPartLegs)
#line 315
		mShared->mPartLegs = new(MEMTYPE_MOTIONCONTROLLER) SINT[NumParts(mesh)];

	// what each part is
	for (i = 0; i < mesh->mNumParts; i++)
	{
		mPartFrame[i] = -1.0f;
		mOldPartFrame[i] = -1.0f;
		mOldPartPos[i] = FVector(0, 0, 0);

		CMeshPart	*part = mesh->mParts[i];
		if (!stricmp(part->mName, "Body"))
			mShared->mPartTypes[i] = 1;
		else if (!strnicmp(part->mName, "Leg", 3))
		{
			mShared->mPartTypes[i] = 2;
			mShared->mPartLegs[i] = atoi(part->mName + 3);
		}
		else if (!strnicmp(part->mName, "Foot", 4))
			mShared->mPartTypes[i] = 3;
		else if (!strnicmp(part->mName, "Toe", 3))
		{
			mShared->mPartTypes[i] = 4;
			mShared->mPartLegs[i] = atoi(part->mName + 3) - 1;
		}
		else if (!stricmp(part->mName, "turret"))
			mShared->mPartTypes[i] = 5;
		else
			mShared->mPartTypes[i] = 0;
	}

	anim = mesh->GetAnimModePtr(mesh->GetAnimModeByName("LegMotion"));
	SINT	first = anim->mStart;
	SINT	last = anim->mLast;
	mFirstFrame = first;
	mNumFrames = last - first + 1;

	// each leg's length and orientation for each LegMotion frame
	if (!mShared->mLegFrames)
	{
#line 365
		mShared->mLegFrames = new(MEMTYPE_MOTIONCONTROLLER) float *[mNumFrames];
		for (i = 0; i < mNumFrames; i++)
			mShared->mLegFrames[i] = NULL;
	}
	if (!mShared->mLegOris)
	{
#line 371
		mShared->mLegOris = new(MEMTYPE_MOTIONCONTROLLER) FMatrix *[mNumFrames];
		for (i = 0; i < mNumFrames; i++)
			mShared->mLegOris[i] = NULL;
	}
	for (i = 0; i < mNumFrames; i++)
	{
		if (!mShared->mLegFrames[i])
#line 379
			mShared->mLegFrames[i] = new(MEMTYPE_MOTIONCONTROLLER) float[mNumLegs];
		if (!mShared->mLegOris[i])
#line 381
			mShared->mLegOris[i] = new(MEMTYPE_MOTIONCONTROLLER) FMatrix[mNumLegs];

		for (SINT l = 0; l < mNumLegs; l++)
		{
			CMeshPart	*base = mesh->GetEmitterSubpart("Footbase", l + 1);
			mesh->GetAnimModeByName("LegMotion");
			FVector	bpos;
			FMatrix	bori;
			base->GetWorldTransforms(mFirstFrame + i, 0, 0, mThing, &bori, &bpos, true, 0, 0);

			// CMesh::GetPart (mesh.h): its result is a named web, which keeps the name-pointer chains in eax as retail does
			// (as in CMCTentacle::Init, where every part fetch is GetPart)
			float	length = 0;
			for (SINT p = 0; p < mesh->mNumParts; p++)
			{
				if ((!strnicmp(mesh->GetPart(p)->mName, "Leg", 3)) && (atoi(mesh->GetPart(p)->mName + 3) == l + 1))
				{
					FVector	lpos;
					FMatrix	lori;
					mesh->GetPart(p)->GetWorldTransforms(mFirstFrame + i, 0, 0, mThing, &lori, &lpos, true, 0, 0);
					FVector	d = lpos - bpos;
					length = d.Magnitude();
					mShared->mLegOris[i][l].MakeFromDirection(d, FVector(0, -1, 0));
				}
			}
			mShared->mLegFrames[i][l] = length;
		}
	}

	// each toe's offset from its leg for each ToeMotion frame
	if (!mShared->mToeOffsets)
	{
#line 439
		mShared->mToeOffsets = new(MEMTYPE_MOTIONCONTROLLER) FVector *[mNumLegs * mNumToes];
		for (i = 0; i < mNumLegs * mNumToes; i++)
			mShared->mToeOffsets[i] = NULL;
	}
	for (i = 0; i < mNumLegs * mNumToes; i++)
	{
		CMeshPart	*stop = mesh->GetEmitterSubpart("Toestop", i + 1);
		if (stop)
		{
			if (!mShared->mToeOffsets[i])
#line 452
				mShared->mToeOffsets[i] = new(MEMTYPE_MOTIONCONTROLLER) FVector[mToeLastFrame - mToeFirstFrame + 1];
			for (SINT fr = mToeFirstFrame; fr <= mToeLastFrame; fr++)
			{
				FVector	spos, lpos;
				FMatrix	sori;
				MechToeMatrix lori;
				CMeshPart	*leg = stop->mParent->mParent;
				stop->GetWorldTransforms(fr, fr, 0, NULL, &sori, &spos, true, 0, 0);
				leg->GetWorldTransforms(fr, fr, 0, NULL, &lori, &lpos, true, 0, 0);
				// declared, then assigned, as UpdateSpline's head_pos is: the default constructor is an inline site after
				// MakeFromDirection, which lowers its nested budget so that retail's three Normalise calls stay out of line.
				// The offset goes back into spos: retail stores it at spos's frame slot (00499aee..00499b2f), and the stores
				// stay because &spos escaped to GetWorldTransforms
				FVector	rel;
				rel = spos - lpos;
				spos = ToeOffsetProduct(lori.Inverse(), rel);
				mShared->mToeOffsets[i][fr - mToeFirstFrame] = spos;
			}
		}
		else
			mShared->mToeOffsets[i] = NULL;
	}
}

//******************************************************************************************
// The ground under a foot; with slot 14 set, the first thing a line down through the foot hits, if higher.
float CMCMech::GetFootHeight(FVector *pos, SINT leg, BOOL no_trace)	// FUNCTION: 0x00499bc0
{
	float	ground = MAP.Collide(*pos);
	if ((Unknown14()) && (!no_trace))
	{
		FVector	ends[2];			// from 2 above the foot to 10 below; one array, so neither end shares the report's slot
		ends[1] = *pos;
		ends[1].Z += 10.0f;
		ends[0] = *pos;
		ends[0].Z -= 2.0f;
		CLine	line(ends[0], ends[1]);
		CWorldLineColReport	report;
		CThing	*ignore = (CThing *)mThing;
		float	h;
		if (WORLD.FindFirstThingToHitLine(line, ignore, &report, FALSE, TRUE, 2, 8, TRUE, -1) == 3)
			h = report.mDistToImpact + ends[0].Z;
		else
			h = ((CThing *)mThing)->GetPos().Z + 2.5f;
		if (h > ground)
			return ground;
		return h;
	}
	return ground;
}

//******************************************************************************************
void CMCMech::AddMovement(const FVector &movement)	// FUNCTION: 0x00499d60
{
	SINT	i;
	for (i = 0; i < mNumLegs; i++)
	{
		if (mLegPos)
			mLegPos[i] += movement;
		if (mLegTarget)
			mLegTarget[i] += movement;
	}
	if (mMesh)
	{
		for (i = 0; i < mMesh->mNumParts; i++)
			if (mPartPos)
				mPartPos[i] += movement;
	}
}

//******************************************************************************************
// A part's position and orientation this frame. The body leans with the ground (or the thing's pitch and roll) and
// with its stepping legs; a leg steps when its foot has fallen too far behind, and takes the LegMotion frame whose
// length reaches its foot; a foot lies along the ground; a toe bends down to it; the turret turns.
// The Footbase part is found into a local before its transform is read (retail calls GetEmitterSubpart before it
// pushes GetWorldTransforms's arguments, 0049a279, 0049a805), and the stepping count tests the neighbour's counter as a
// member before reading it into c (retail compares the counter it read before Unknown13 and tests c again after
// n++, 0049aab3..0049aaea).
// STAND-IN (spelling): the literal Roll row. Retail constructs (0, 1, 0) into a temporary and then stores all four words;
// this writer does exactly that constructor and result transfer: no other operation and no Pad initialisation.
static inline void PublishMechRollMiddle(FVector &destination)
{
    destination = FVector(0.f, 1.f, 0.f);
}

static inline void PublishMechRollTrailing(FVector &destination, float angle)
{
    destination = FVector(-(float)sin(angle), 0.f, (float)cos(angle));
}

static inline void MakeMechRollRows(FMatrix &matrix, float roll)
{
    matrix.Row[0] = FVector((float)cos(roll), 0.f, (float)sin(roll));
    PublishMechRollMiddle(matrix.Row[1]);
    PublishMechRollTrailing(matrix.Row[2], roll);
}

// STAND-IN: original declaring type and constructor spelling are unknown.
// One existing FVector base, no extra state, Pad untouched.
class FMechXYZRow : public FVector
{
public:
    // FUNCTION: 0x00401ec0 ??0FMechXYZRow@@QAE@MMM@Z
    FMechXYZRow(float x, float y, float z) : FVector()
    {
        X = x;
        Y = y;
        Z = z;
    }
};

// STAND-IN (spelling, no code): a non-allocating placement array new[] for the three animation-orientation rows. The base
// class already default-initialises those rows; the placement initialisation below is disabled so that it emits no work
// and only retail's one initialiser remains.
inline void *operator new[](size_t, void *storage) { return storage; }

// Preserve the existing tagged allocation API for allocating arrays.
inline void *operator new[](size_t size, EMemoryType type, char *file, UINT line)
{
    return MEM_MANAGER.Alloc(size, type, file, line);
}

// STAND-IN (spelling): a matrix with no state of its own that constructs and queries its own base. Original declaring type
// unknown; nothing is assumed about copying or eliding the returned object.
class FMechAnimationOrientation : public FMatrix
{
public:
    FMechAnimationOrientation(CMeshPart *part, SINT f1, SINT f2, float fraction,
                              IRenderableThing *const &thingField, FVector &animationPosition)
        : FMatrix()
    {
// The project new(type) macro tags allocating calls. This is ordinary placement.
#undef new
        // STAND-IN (spelling, no code): a disabled repeated row initialisation; original source unknown.
        if (0) new (Row) FVector[3];
#define new(type) new(type, __FILE__, __LINE__)
        part->GetWorldTransforms(f1, f2, fraction, thingField, this,
                           &animationPosition, false, false, false);
    }
};

// The orientation's last row is read through a plain pointer: loads through it are indirect, not the
// local's field symbols, and their rank keys put orientation.Row[2].Z ahead of the pitch factor as retail's products do
// (0049a4b5..0049a50f); only the Y column keeps a bracket. The two brackets this replaces were scheduler nodes, and
// without them the pitch/roll regions cut where retail's do (0049a730). Original spelling unknown.
static inline FMatrix ProduceMechPitchOrientation(const FMatrix &orientation, const FMatrix &pitch)
	{
		const FMatrix	*po = &orientation;
		return FMatrix(
					   FMechXYZRow(orientation.Row[0].X * pitch.Row[0].X + orientation.Row[0].Y * pitch.Row[1].X + orientation.Row[0].Z * pitch.Row[2].X,
							   orientation.Row[0].X * pitch.Row[0].Y + orientation.Row[0].Y * pitch.Row[1].Y + orientation.Row[0].Z * pitch.Row[2].Y,
							   orientation.Row[0].X * pitch.Row[0].Z + orientation.Row[0].Y * pitch.Row[1].Z + orientation.Row[0].Z * pitch.Row[2].Z),
					   FMechXYZRow(orientation.Row[1].X * pitch.Row[0].X + orientation.Row[1].Y * pitch.Row[1].X + orientation.Row[1].Z * pitch.Row[2].X,
							   orientation.Row[1].X * pitch.Row[0].Y + orientation.Row[1].Y * pitch.Row[1].Y + orientation.Row[1].Z * pitch.Row[2].Y,
							   orientation.Row[1].X * pitch.Row[0].Z + orientation.Row[1].Y * pitch.Row[1].Z + orientation.Row[1].Z * pitch.Row[2].Z),
					   FMechXYZRow(po->Row[2].X * pitch.Row[0].X + po->Row[2].Y * pitch.Row[1].X + po->Row[2].Z * pitch.Row[2].X,
							   po->Row[2].X * pitch.Row[0].Y + po->Row[2].Y * pitch.Row[1].Y + (po->Row[2].Z) * pitch.Row[2].Y,
							   po->Row[2].X * pitch.Row[0].Z + po->Row[2].Y * pitch.Row[1].Z + po->Row[2].Z * pitch.Row[2].Z));
	}

static inline void PublishMechBlendPosition(FVector &output, const FVector &animation,
                                          const float &actualBlendField)
{
    FMechXYZRow oldPosition(output.X * (1.0f - actualBlendField),
                            output.Y * (1.0f - actualBlendField),
                            output.Z * (1.0f - actualBlendField));
    float animationWeight = actualBlendField;
    FMechXYZRow animationPosition(animation.X * animationWeight,
                                  animation.Y * animationWeight,
                                  animation.Z * animationWeight);
    FMechXYZRow blendedPosition((animationPosition.X + oldPosition.X),
                                animationPosition.Y + oldPosition.Y,
                                animationPosition.Z + oldPosition.Z);
    output = blendedPosition;
}

// STAND-IN (product rank keys): maths.h's FMatrix::operator* written out, with three (double) conversions:
// Row[0].X converts left.Row[0].X (its roll-row product first), Row[1].Z converts its Y and Z rows' left factors (the
// roll-row temporary's product last), and Row[2].Z converts all three and is written Y, Z, X (retail's Y + X first,
// 0049a626, once the pitch product reads its last row through a pointer). Conversion temporaries move only the products' rank keys; the
// x87 instructions are unchanged. The member operator gives retail's region cuts (no bracket nodes) but ranks the
// temporary's products from its own ids; no natural spelling reorders them.
static inline FMatrix ProductMechBodyRoll(const FMatrix &left, const FMatrix &m)
{
		return FMatrix(
					   FVector((double)left.Row[0].X * m.Row[0].X + left.Row[0].Y * m.Row[1].X + left.Row[0].Z * m.Row[2].X,
							   left.Row[0].X * m.Row[0].Y + left.Row[0].Y * m.Row[1].Y + left.Row[0].Z * m.Row[2].Y,
							   left.Row[0].X * m.Row[0].Z + left.Row[0].Y * m.Row[1].Z + left.Row[0].Z * m.Row[2].Z),
					   FVector(left.Row[1].X * m.Row[0].X + left.Row[1].Y * m.Row[1].X + left.Row[1].Z * m.Row[2].X,
							   left.Row[1].X * m.Row[0].Y + left.Row[1].Y * m.Row[1].Y + left.Row[1].Z * m.Row[2].Y,
							   left.Row[1].X * m.Row[0].Z + (double)left.Row[1].Y * m.Row[1].Z + (double)left.Row[1].Z * m.Row[2].Z),
					   FVector(left.Row[2].X * m.Row[0].X + left.Row[2].Y * m.Row[1].X + left.Row[2].Z * m.Row[2].X,
							   left.Row[2].X * m.Row[0].Y + left.Row[2].Y * m.Row[1].Y + left.Row[2].Z * m.Row[2].Y,
							   (double)left.Row[2].Y * m.Row[1].Z + (double)left.Row[2].Z * m.Row[2].Z + (double)left.Row[2].X * m.Row[0].Z));
}

class CMechEntryProfile
{
public:
    CMechEntryProfile(SINT id) { mId = id; __int64 t = ReadTimeStampCounter(); mStart = (DWORD)t; }
    // FUNCTION: 0x00494310 ??1CMechEntryProfile@@QAE@XZ
    ~CMechEntryProfile()
    {
        ProfileAdd(mId, (DWORD)ReadTimeStampCounter() - mStart);
    }
    SINT mId;
    DWORD mStart;
};

// STAND-IN (ids, scheduling): the three toe position adds, in retail's operand order; original owner unknown. The Y and Z
// adds are bracketed whole: their nodes leave the operand chains (0049b46c). Y's (fpos.Y) operand bracket is gone: one CSE
// value fewer, which brings the value ids after the foot height's two (UpdateBone) back to a multiple of four (0049acc7).
// No intermediate or Pad store; the XYZ publication order is kept.
static inline void AccumulateMechToePosition(FVector &tip, const FVector &fpos)
{
    tip.X = tip.X + fpos.X;
    tip.Y = (fpos.Y + tip.Y);
    tip.Z = (fpos.Z + tip.Z);
}

// STAND-IN (spelling, no code): the toe point product, with one bracketed inner sum and one bracketed whole product per row,
// which keeps retail's measured operation tree. Same FVector output and no intermediate stores; original owner unknown.
static inline FVector MechToePointProduct(const FMatrix &matrix, const FVector &point)
{
	return FVector(
		((matrix.Row[0].X * point.X) + (matrix.Row[0].Y) * point.Y) + (matrix.Row[0].Z) * point.Z,
		(matrix.Row[1].X * point.X + matrix.Row[1].Y * point.Y) + (matrix.Row[1].Z) * point.Z,
		(matrix.Row[2].X * point.X + matrix.Row[2].Y * point.Y) + (matrix.Row[2].Z) * point.Z);
}

// STAND-IN (spelling, no code): the hierarchy point product, with bracket groups that keep retail's measured operation tree.
// Same FVector output and no intermediate stores; original owner unknown. MechHierarchyOrientationX only reads
// orientation.Row[0].X through a function; it has no identity in the original source.
static inline float MechHierarchyOrientationX(const FMatrix &orientation)
{
    return orientation.Row[0].X;
}

static inline FVector MechHierarchyPointProduct(const FMatrix &matrix, const FVector &point)
{
	return FVector(
		((matrix.Row[0].Z * point.Z) + matrix.Row[0].Y * point.Y) + (MechHierarchyOrientationX(matrix) * point.X),
		((matrix.Row[1].Z * point.Z) + matrix.Row[1].X * point.X) + matrix.Row[1].Y * point.Y,
		((matrix.Row[2].Z * point.Z) + matrix.Row[2].X * point.X) + matrix.Row[2].Y * point.Y);
}

static inline FVector MechLegPointProduct(const FMatrix &m, const FVector &v)
{
    return FVector(
        ((v.Y) * m.Row[0].Y) + (v.Z) * m.Row[0].Z + m.Row[0].X * v.X,
        (m.Row[1].Y) * v.Y + (m.Row[1].X) * v.X + (m.Row[1].Z) * v.Z,
        ((v.Z) * m.Row[2].Z + v.Y * m.Row[2].Y) + (m.Row[2].X) * v.X);
}

static inline void PublishMechTurretOrientation(FMatrix &out, const FMatrix &unitOri, const FMatrix &r)
{
    out = unitOri * r;
}

// STAND-IN (spelling): the body's up column: only the three products for X, Y and Z are published; scaledBodyUp's Pad stays
// untouched for the whole-vector copy that follows. Original owner unknown.
static inline FVector PublishMechBodyUp(const FMatrix &r)
{
	return FVector(r.Row[0].Z * -1.0f,
	               r.Row[1].Z * -1.0f,
	               r.Row[2].Z * -1.0f);
}

// The leg's up column: only the three float products are published and the Pad stays untouched. Original helper owner
// unknown.
// STAND-IN (named locals): x, y, z hold the consumed products. Each named float adds a no-code node to the leg tail's
// 81-node scheduling region, so the region ends before the matrix copy's last row as retail's does (0049adfb..0049ae1c);
// no natural spelling adds nodes without inline cost. Original spelling unknown.
static inline void PublishMechLegUp(FVector &up, const FMatrix &body)
{
	float	x = (body.Row[0].Y * -1.0f);
	up.X = x;
	float	y = (body.Row[1].Y * -1.0f);
	up.Y = y;
	float	z = (body.Row[2].Y * -1.0f);
	up.Z = z;
}

// STAND-IN (spelling): MakeFromDirection's operations copied in place; the required copy of up (XYZ and Pad) follows the
// construction of back. Original owner unknown.
static inline void MakeMechLegFromDirection(FMatrix &matrix, const FVector &direction, const FVector &inputUp)
{
		FVector	back = -direction;
		FVector up = inputUp;
		FVector	side = back ^ up;
		FVector	top = back ^ side;
		side.Normalise();
		top.Normalise();
		back.Normalise();
		matrix.Row[0].X = side.X;	matrix.Row[0].Y = top.X;	matrix.Row[0].Z = back.X;
		matrix.Row[1].X = side.Y;	matrix.Row[1].Y = top.Y;	matrix.Row[1].Z = back.Y;
		matrix.Row[2].X = side.Z;	matrix.Row[2].Y = top.Z;	matrix.Row[2].Z = back.Z;
	}

// STAND-IN (spelling): the no-hierarchy key copy and the matrix product after it, nothing else. Original helper owner
// unknown; no extra tests, outputs or forced inlining.
static inline void PublishMechNoHierarchyKeyOrientation(FMatrix &out, const FMatrix &m, CMeshPart *part, UBYTE key)
{
	out = part->GetHFrameOrientation(key);
	out = m * out;
}

// STAND-IN (spelling): retail copies all three scalar arguments before the norm, in Z, Y, X order, while X remains the
// first vector field. A reference copy folds away, and reversing the fields adds another reload of the original X. This
// scalar snapshot keeps those component roles with no extra fields, Pad operation, arithmetic or register directive.
// Original owner unknown.
struct MechNormSnapshot : public FVector
{
    MechNormSnapshot(float x, float y, float z) : FVector()
    {
        Z = z;
        Y = y;
        X = x;
    }
};
typedef char MechNormSnapshotSize[(sizeof(MechNormSnapshot)==sizeof(FVector))?1:-1];
// STAND-IN (product rank keys): the first-leg matrix product. Each inner sum and its whole product have one bracket group,
// which keeps the measured operation tree through C2's ranking; no partial result stores or extra products. Original
// source unknown.
// STAND-IN (product rank keys): Row[2].X converts inverse.Row[2].Y, and Row[2].Y converts inverse.Row[2].Z, to double.
// Each conversion temporary ranks its product first (retail's Y, X, Z order in Row[2].X; Z first in Row[2].Y) without
// the scheduler node a bracket costs (0049ae7f, 0049aeac); x87 computes in register precision either way, so the
// instructions are unchanged. No natural spelling gives these orders: the keys come from the _ori parameter's symbol id.
// STAND-IN (scheduling regions): Row[2].Z, the first value computed, carries four nested
// whole-value brackets: four no-code x87 nodes that move the leg products' 81-node scheduling region cuts, so the
// push for Row[1].Y's argument fills the multiply gap before its first faddp as retail's (0049af14). Same products
// and associations; the original source of these nodes is unknown.
static inline FMatrix ProductMechFirstLeg(const FMatrix &inverse, const FMatrix &orientation)
{
	return FMatrix(
		FVector((inverse.Row[0].X * orientation.Row[0].X + inverse.Row[0].Y * orientation.Row[1].X) + inverse.Row[0].Z * orientation.Row[2].X,
		        (inverse.Row[0].Z * orientation.Row[2].Y) + inverse.Row[0].Y * orientation.Row[1].Y + inverse.Row[0].X * orientation.Row[0].Y,
		        (inverse.Row[0].Z * orientation.Row[2].Z + inverse.Row[0].Y * orientation.Row[1].Z) + inverse.Row[0].X * orientation.Row[0].Z),
		FVector((inverse.Row[1].Z * orientation.Row[2].X + inverse.Row[1].Y * orientation.Row[1].X) + inverse.Row[1].X * orientation.Row[0].X,
		        inverse.Row[1].Y * orientation.Row[1].Y + inverse.Row[1].X * orientation.Row[0].Y + inverse.Row[1].Z * orientation.Row[2].Y,
		        (inverse.Row[1].X * orientation.Row[0].Z + inverse.Row[1].Y * orientation.Row[1].Z) + inverse.Row[1].Z * orientation.Row[2].Z),
		FVector((((double)inverse.Row[2].Y) * orientation.Row[1].X + inverse.Row[2].X * orientation.Row[0].X + inverse.Row[2].Z * orientation.Row[2].X),
		        (((double)inverse.Row[2].Z) * orientation.Row[2].Y + inverse.Row[2].Y * orientation.Row[1].Y) + inverse.Row[2].X * orientation.Row[0].Y,
		        ((((inverse.Row[2].X * orientation.Row[0].Z + inverse.Row[2].Z * orientation.Row[2].Z + inverse.Row[2].Y * orientation.Row[1].Z))))));
}

// STAND-IN (scheduling regions): Row[1].Y carries five nested whole-value brackets (no-code x87
// nodes). With the first leg's four they put the following region's cut where retail's is: the row's lea ecx then
// issues before its first faddp (0049b10f). Same products and associations; original source unknown.
// STAND-IN (spelling): the second-leg matrix product, with its rows built through FMechXYZRow (the 00401ec0 alias).
// Original helper owner unknown.
static inline FMatrix ProductMechSecondLeg(const FMatrix &left, const FMatrix &right)
{
		return FMatrix(
					   FMechXYZRow(((left.Row[0].Z * right.Row[2].X) + left.Row[0].Y * right.Row[1].X) + left.Row[0].X * right.Row[0].X,
							   left.Row[0].X * right.Row[0].Y + left.Row[0].Y * right.Row[1].Y + left.Row[0].Z * right.Row[2].Y,
							   left.Row[0].X * right.Row[0].Z + left.Row[0].Y * right.Row[1].Z + left.Row[0].Z * right.Row[2].Z),
					   FMechXYZRow((left.Row[1].Y * right.Row[1].X) + left.Row[1].X * right.Row[0].X + left.Row[1].Z * right.Row[2].X,
							   (((((((left.Row[1].X * right.Row[0].Y) + left.Row[1].Y * right.Row[1].Y) + left.Row[1].Z * right.Row[2].Y))))),
							   ((left.Row[1].X * right.Row[0].Z) + left.Row[1].Y * right.Row[1].Z) + left.Row[1].Z * right.Row[2].Z),
					   FMechXYZRow(((left.Row[2].X * right.Row[0].X) + left.Row[2].Z * right.Row[2].X) + left.Row[2].Y * right.Row[1].X,
							   ((left.Row[2].X * right.Row[0].Y) + left.Row[2].Z * right.Row[2].Y) + left.Row[2].Y * right.Row[1].Y,
							   ((left.Row[2].X * right.Row[0].Z) + left.Row[2].Z * right.Row[2].Z) + left.Row[2].Y * right.Row[1].Z));
}

// STAND-IN (spelling): the leg length: the norm with the X square held in a local. Reconstructed owner.
static inline float MechLegLength(const FVector &working, const FVector &original)
{
    float squaredX = working.X * original.X;
    return sqrtf((squaredX + (working.Y * working.Y) + working.Z * working.Z));
}

// UpdateBone state (2026-10-13): matches retail (byte check: match).
// Stand-ins in this candidate, each labelled where it is written:
//   - PublishMechLegUp's named floats x, y, z: no-code nodes that end the leg tail's 81-node scheduling region before
//     the matrix copy's last row (0049adfb..0049ae1c).
//   - ProductMechFirstLeg's bracketed (double) conversions in Row[2].X and Row[2].Y: rank keys for retail's product
//     orders without a bracket node (0049ae7f, 0049aeac).
//   - ProductMechBodyRoll = FMatrix::operator*'s body with three (double) conversions (Row[0].X, Row[1].Z, Row[2].Z):
//     rank keys only; no bracket nodes, so the pitch/roll regions cut where retail's do (0049a722).
//   - MechLegPointProduct's rows without their order-neutral brackets: the leg's target address is materialised
//     before its first use as retail's (0049a831).
//   - The dead escape of foot in the toe loop (0049b3a9) and the Toe Z add's whole-sum bracket (its node moved out of
//     the fpos.Z chain, so the toe copy loads Z before storing Y, 0049b46c).
//   - The donor's helpers (MakeMechRollRows, ProduceMechPitchOrientation, ProductMechSecondLeg, MechToePointProduct,
//     PublishMechBlendPosition and the others), each with its own label; the private maths.h members are under
//     MCMECH_CPP.
// Resolved sites (2026-10-13; UpdateBone matches retail):
//   - 0049a730: the pitch product's row pointer (above).
//   - 0049af14: ProductMechFirstLeg/ProductMechSecondLeg's whole-value brackets, which move the leg products'
//     81-node scheduling region cuts.
//   - 0049a9e8: the order of the leg-lift store's address operands is decided by CSE value ids. The address adds the value
//     *leg*4 to IND(this+0x1c), and the IND ranks by 7 + ((its value id mod 4) << 14). Retail's order needs both
//     values (first seen in `mLegB[*leg - 1] = 0;`) two ids later, and every later value back at a multiple of four
//     (0049acc7). Measured with a diagnostic C2 that burns value ids. Source: the foot height's two bracket groups
//     (+2 values before the store, labelled there), with the order-neutral (fpos.Y) and (matrix.Row[2].X * point.X)
//     brackets removed from the toe helpers.
// Inline budget: own size 3864, budget 7728; the second leg's Inverse() operator/= is expanded with 27 budget units of
// slack (a separate dead-escape statement costs 28), so any stand-in may add at most 13 own-size units.
void CMCMech::UpdateBone(FVector *pos, FMatrix *ori, CMeshPart *part, SINT *leg, SINT *frame, float dt, SINT flags)	// FUNCTION: 0x00499e30
{
	// STAND-IN (spelling): a reference to the part's mesh pointer, taken before the profiler object is built; every use
	// still reloads it. A plain pointer copy is 6382 bytes off. Original source spelling unknown.
	CMesh *&mesh = part->mBaseMesh;
	CMechEntryProfile __profile(PROFILE_MechUpdateBone);

	if (!mNumFrames)
		Init(mesh);

	bool				rotated = false;
	bool				moved = false;
	FMatrix				m(1, 0, 0, 0, 1, 0, 0, 0, 1);
	FVector				offset(0, 0, 0);
	SINT				f = 0;
	IRenderableThing	*thing = mThing;
	UBYTE				key = part->mVHFrameMap[0];

	const SINT partNumber = part->mPartNumber;
	SINT	type = mShared->mPartTypes[partNumber];
	if (type == 1)
	{
		// the body leans with the ground, and with the legs that are stepping
		float	pitch = 0;
		float	roll = 0;
		float	lift = 0;
		FVector foot;
        const FVector &point = part->mHFramePositions[key];
        // STAND-IN (spelling): the body's ground projection. Retail's code at 00499fa4..0049a02e (pristine
        // BEA.exe.original.backup, SHA-256 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750) groups each row
        // as (Z*pz + Y*py) + X*px, keeps X wide while Y and Z round to float, then adds the position and stores the foot
        // as three floats. This spelling reproduces all 34 floating-point instructions in VC6; the reference to the key
        // position is part of it (a value copy is 6278 bytes off), and the spelling is reconstructed, not the original
        // source. `projected` is an FVector used for its XYZ only: its Pad (the fourth field) is neither read nor written.
        // The leg projection (MechLegPointProduct) associates differently and keeps its own code.
        FVector projected;
        projected.X = ((ori->Row[0].Z * point.Z) + (ori->Row[0].Y * point.Y)) + ori->Row[0].X * point.X;
        projected.Y = ((ori->Row[1].Z * point.Z) + (ori->Row[1].Y * point.Y)) + ori->Row[1].X * point.X;
        projected.Z = ((ori->Row[2].Z * point.Z) + (ori->Row[2].Y * point.Y)) + ori->Row[2].X * point.X;
        foot.X = pos->X + projected.X;
        foot.Y = pos->Y + projected.Y;
        foot.Z = pos->Z + projected.Z;
		FVector	up;
		if (Unknown10())
			up = MAP.Normal(foot);
		else
		{
			float	yaw = ((CUnit *)thing)->GetCurrentOrientation().mYaw;
			float	pitch = Unknown11();
			float	roll = Unknown12();
			FMatrix	r(yaw, pitch, roll);
			FVector scaledBodyUp = PublishMechBodyUp(r);

			up = scaledBodyUp;
		}
		up = up * (1.0f - mParamE) + mUnknown88 * mParamE;
		mUnknown88 = up;
		m.MakeBodyDirectionWithLateUp(up, FVector(0, -1, 0));

		for (SINT i = 0; i < mNumLegs; i++)
		{
			if (mLegC[i] > 0)
			{
				FMatrix	o;
				FVector	p;
				CMeshPart	*footbase = mesh->GetEmitterSubpart("Footbase", i + 1);
				footbase->GetWorldTransforms(0, 0, 0, mThing, &o, &p, false, false, false);
				FVector	d = p - foot;

				d *= (float)sin((mLegC[i] * 3.14f) / 180.0f);
				if (d.X > 5.0f)
					d.X = 5.0f;
				if (d.Y > 5.0f)
					d.Y = 5.0f;
				if (d.X < -5.0f)
					d.X = -5.0f;
				if (d.Y < -5.0f)
					d.Y = -5.0f;
				pitch += d.Y * 2;
				roll += d.X * 2;
				lift -= (float)sin((mLegC[i] * 3.14f) / 180.0f) * 0.1f;
			}
		}
		pitch *= mParamA;
		roll *= mParamA;
		FMatrix	pm, rm;
		pm.MakeRotationPitchF((pitch * 3.14f) / 180.0f);
		MakeMechRollRows(rm, (roll * 3.14f) / 180.0f);

		if (((CUnit *)thing)->IsDying())
		{
			moved = true;
			offset = *pos;
			offset.Z = ((CUnit *)thing)->Unknown106();
		}
		mBodyOri = m = ProductMechBodyRoll(ProduceMechPitchOrientation(m, pm), rm);
	}
	else if (type == 2)
	{
		// a leg steps when its foot has fallen behind, then reaches down to it
		*leg = mShared->mPartLegs[partNumber];
		FVector	foot = *pos + MechLegPointProduct(*ori, part->mHFramePositions[key]);
		FMatrix	o;
		FVector	base;
		CMeshPart	*footbase = mesh->GetEmitterSubpart("Footbase", *leg);
		footbase->GetWorldTransforms(0, 0, 0, mThing, &o, &base, false, false, false);
		FVector	d = foot - mLegTarget[*leg - 1];
		mLegTarget[*leg - 1] = foot;
		FVector	p = base;
		// STAND-IN (CSE value ids, 2026-10-13): two bracket groups around the foot height add two CSE values before
		// `mLegB[*leg - 1] = 0;` first sees *leg*4 and this+0x1c. The lift store's IND then outranks the index, so its
		// address operands are in retail's order (0049a9e8). The original source of these values is unknown.
		p.Z = ((GetFootHeight(&base, *leg, flags)));
		p += d;
		if (dt > 2.0f)
			mLegPos[*leg - 1] = p;
		FVector	v = p - mLegPos[*leg - 1];
		mLegB[*leg - 1] = 0;
		if (mLegC[*leg - 1] > 0)
		{
			mLegC[*leg - 1] += SINT(dt * mParamB);
			if (mLegC[*leg - 1] > 180)
				mLegC[*leg - 1] = 0;
			else
			{
				mLegPos[*leg - 1] += v * (float)mLegC[*leg - 1] / 180.0f;
				mLegA[*leg - 1] = 0;
				mLegB[*leg - 1] = -(float)sin((mLegC[*leg - 1] * 3.14f) / 180.0f) * mParamD;
			}
		}
		else
		{
			float	limit;
			float	distance;
			if (d.MagnitudeYZ() > 0.01f)
			{
				limit = mParamC;
				distance = v.MagnitudeZY();
			}
			else
			{
				limit = mParamF;
				distance = v.MagnitudeXYLate();
			}
			if (distance > limit)
			{
				// step, unless too many legs are already stepping or a neighbour is
				SINT	n = 0;
				bool	odd = false;
				bool	even = false;
				bool	ok = true;
				for (SINT j = 0; j < mNumLegs; j++)
				{
					if ((mLegC[j]) && (mLegC[j] <= Unknown13()))
						n++;
					SINT	c = mLegC[j];
					if (((c == 0) || (c > 90)) && (j != *leg - 1))
					{
						if ((j & 1) == 1)
							odd = true;
						else
							even = true;
					}
					else if (j == *leg - 2)
						ok = false;
				}
				bool	can = ((((*leg - 1) & 1) == 1) ? odd : even) && ok;
				if (n >= mParamG)
					can = false;
				if ((distance > limit * 2) || (can))
					mLegC[*leg - 1] = 1;
			}
		}

		GetFootHeight(&base, *leg, flags);
		FVector	q = mLegPos[*leg - 1];
		q.Z = base.Z;
		mLegPos[*leg - 1].Z = GetFootHeight(&q, *leg, flags);
		FVector	r = mLegPos[*leg - 1];
		r.Z += mLegB[*leg - 1];
		const FVector &diff = foot - r;	// STAND-IN (spelling): a value copy is 30 bytes off
		if ((!mLegA[*leg - 1]) && (mLegB[*leg - 1] > -0.01f))
		{
			mUnknownD0 = 1;
			mUnknownD4 = r;
			mLegA[*leg - 1] = 1;
		}

		// the LegMotion frame whose length reaches the foot
		MechNormSnapshot working(diff.X, diff.Y, diff.Z);
		float	len = MechLegLength(working, diff);
		SINT	best = 0;
		float	best_dist = 0;
		for (SINT i = 0; i < mNumFrames; i++)
		{
			float	e = (float)fabs(mShared->mLegFrames[i][*leg - 1] - len);
			if ((best_dist == 0) || (e < best_dist))
			{
				best_dist = e;
				best = i;
			}
		}
		*frame = mFirstFrame + best;

		FVector scaledUp;
		PublishMechLegUp(scaledUp, mBodyOri);
		moved = true;
		offset = foot;
		MakeMechLegFromDirection(m, diff, scaledUp);
		*ori = ProductMechFirstLeg(mBodyOri.Inverse(), *ori);
		*ori = ProductMechSecondLeg(*ori, mShared->mLegOris[best][*leg - 1].Inverse());
	}
	else if (type == 3)
	{
		// a foot lies along the ground
		FVector normal = MAP.Normal(mLegPos[*leg - 1]);
		m.MakeBodyDirectionWithLateUp(normal, FVector(0, -1, 0));
		rotated = true;
	}
	else if (type == 4)
	{
		// a toe takes the ToeMotion frame that puts its tip on the ground
		SINT		t = mShared->mPartLegs[partNumber];
		CMeshPart	*stop = (CMeshPart *)mesh->GetEmitterSubpart("Toestop", t + 1);
		if (stop)
		{
			SINT	idx = (*leg - 1) * mNumToes + t % 3;
			if (idx < mNumLegs * mNumToes)
			{
				CMeshPart	*foot = stop->mParent->mParent;
				// STAND-IN (dead escape): foot's address taken in unreachable code keeps foot an
				// address-taken local, so the scheduler keeps the tip copy's Pad store ahead of the foot reload and
				// foot->mPartNumber load as retail (0049b3a9). Consumed value; no code. Original source unknown.
				if (0) ToWCHAR((char *)&foot);
				FMatrix		fori;
				bool continuation;
				do
				{
					continuation = false;
					FVector	tip = mShared->mToeOffsets[t][mToeFrames[idx] - mToeFirstFrame];
					FVector	fpos = mPartPos[foot->mPartNumber];
					fori = mPartOri[foot->mPartNumber];
					tip = MechToePointProduct(fori, tip);
					AccumulateMechToePosition(tip, fpos);
					float	h = MAP.Collide(tip);
					if (h + 0.1f < tip.Z)
					{
						mToeFrames[idx]--;
						if (mToeFrames[idx] < mToeFirstFrame)
							mToeFrames[idx] = mToeFirstFrame;
						else
							continuation = true;
					}
					else
					{
						if (h - 0.1f > tip.Z)
						{
							mToeFrames[idx]++;
							if (mToeFrames[idx] > mToeLastFrame)
								mToeFrames[idx] = mToeLastFrame;
						}
					}
				} while (continuation);
				*frame = mToeFrames[idx];
			}
		}
	}
	else if (type == 5)
	{
		// the turret turns to the unit's aim
		rotated = true;
		// STAND-IN (spelling): a reference to the unit's yaw field comes first and the basis owner second; the yaw is loaded only
		// at the rotation argument (reading the member there is 1450 bytes off).
		const float &yaw = ((CUnit *)thing)->mUnknownE0;
		CUnit *orientationOwner = (CUnit *)thing;
		FMatrix r(yaw, 0, 0);
		PublishMechTurretOrientation(m, orientationOwner->GetOrientation(), r);
	}

	if (*leg != -1)
	{
		f = *frame;
		key = part->mVHFrameMap[f];
	}

	if ((thing) && (!part->mPartNumber))
	{
		*pos += thing->GetRenderPos();
		PublishMechTurretOrientation(*ori, *ori, thing->GetRenderOrientation());
	}

	if ((thing) && (!thing->RenderUseHierarchy(part->mPartNumber)))
	{
		if (!moved)
			*pos += part->GetKeyPosition(key);
		else
			*pos = offset;
		if (!rotated)
		{
			PublishMechNoHierarchyKeyOrientation(*ori, m, part, key);
		}
		else
			*ori = m;
	}
	else
	{
		if (!moved)
			*pos += MechHierarchyPointProduct(*ori, part->GetKeyPosition(key));
		else
			*pos = offset;
		if (!rotated)
		{
			*ori = *ori * part->GetHFrameOrientation(key);
			*ori = m * *ori;
		}
		else
			*ori = m;
	}

	// blended towards the thing's own animation
	if (mAnimationBlend != 0)
	{
		float	fraction = 0;
		SINT	a = -1;
		if (mThing)
			a = mThing->GetRealAnimIndex();
		SINT	f1, f2;
		if ((part->mNumVFrames > 1) && (mesh->mNumAnimModes) && (a >= 0))
		{
			if (mThing)
				fraction = mThing->GetRenderFrame();
			CAnimMode	*anim = &mesh->mAnimModes[a];
			float		af = anim->mFrameScaler * fraction + anim->mStart;
			f1 = SINT(floor(af)) % part->mNumVFrames;
			f2 = f1 + 1;
			if (f2 > anim->mLast)
				f2 = anim->mStart;
			fraction = af - (float)floor(af);
			// Open question: retail then calls floor(0) three more times (0049b8f2..0049b91f), as if a second
			// pair of frames were taken from a zero time and dropped; CMeshRenderer's position sets have the
			// same sequence (005496a0..005496d6). The cheapest falsifier is the source of that renderer block.
			SINT	g1 = SINT(floor(0.0f));
			SINT	g2 = g1 + 1;
			if (g2 > SINT(floor(0.0f)))
				g2 = SINT(floor(0.0f));
		}
		else
		{
			f1 = 0;
			f2 = 0;
			SINT	g1 = SINT(floor(0.0f));		// and once more here (0049b91f)
		}
        FVector ap;
        FMechAnimationOrientation ao(part, f1, f2, fraction, mThing, ap);
		PublishMechBlendPosition(*pos, ap, mAnimationBlend);
		*ori = ao * mAnimationBlend + *ori * (1.0f - mAnimationBlend);
	}

	if (mProcessCount < 3)
	{
		mOldPartPos[part->mPartNumber] = *pos;
		mOldPartOri[part->mPartNumber] = *ori;
		mOldPartFrame[part->mPartNumber] = (float)f;
	}
	else
	{
		mOldPartPos[part->mPartNumber] = mPartPos[part->mPartNumber];
		mOldPartOri[part->mPartNumber] = mPartOri[part->mPartNumber];
		mOldPartFrame[part->mPartNumber] = mPartFrame[part->mPartNumber];
	}
	mPartPos[part->mPartNumber] = *pos;
	mPartOri[part->mPartNumber] = *ori;
	mPartFrame[part->mPartNumber] = (float)f;
}

// FMatrix's inverse's parts, emitted here out of line.
// FUNCTION: 0x0049bbb0 ??_0FMatrix@@QAEXM@Z
// FUNCTION: 0x0049bc10 ?TransposeInPlace@FMatrix@@QAEXXZ
// FUNCTION: 0x0049bc40 ?Determinant@FMatrix@@QBEMXZ
// FUNCTION: 0x0049bc80 ?Cofactors@FMatrix@@QBE?AV1@XZ

//******************************************************************************************
void CMCMech::UpdateBoneHierarchyRecursive(FVector pos, FMatrix ori, CMeshPart *part, SINT leg, SINT unused, float dt, SINT flags)	// FUNCTION: 0x0049bd50
{
	UpdateBone(&pos, &ori, part, &leg, &unused, dt, flags);
	for (SINT i = 0; i < part->mNumChildren; i++)
		UpdateBoneHierarchyRecursive(pos, ori, part->mChildren[i], leg, unused, dt, flags);
}

//******************************************************************************************
void CMCMech::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x0049be00
{
	if (!mWasUsed)
		ProcessMovement(TRUE, FALSE);
	mUsed = TRUE;
	mNeedsUpdate = FALSE;

	if (mOldPartFrame)
	{
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
}

//******************************************************************************************
void CMCMech::AdjustFrame(CMeshPart *part, float *frame)	// FUNCTION: 0x0049c1d0
{
	if (!mWasUsed)
		ProcessMovement(TRUE, FALSE);
	mUsed = TRUE;
	mNeedsUpdate = FALSE;

	if (mOldPartFrame)
	{
		if (mOldPartFrame[part->mPartNumber] != -1.0f)
			*frame = (mPartFrame[part->mPartNumber] - mOldPartFrame[part->mPartNumber]) * GAME.GetFrameRenderFraction() + mOldPartFrame[part->mPartNumber];
		else
			*frame = mPartFrame[part->mPartNumber];
	}
}

//******************************************************************************************
BOOL CMCMech::Unknown8()	// FUNCTION: 0x0049c240
{
	return mNeedsUpdate;
}

//******************************************************************************************
BOOL CMCMech::CanMergePart(CMeshPart *part)	// FUNCTION: 0x0049c250
{
	if (!stricmp(part->mName, "Body"))
		return FALSE;
	if (!strnicmp(part->mName, "Leg", 3))
		return FALSE;
	if (!strnicmp(part->mName, "Foot", 4))
		return FALSE;
	if (!strnicmp(part->mName, "Toe", 3))
		return FALSE;
	return stricmp(part->mName, "turret") != 0;
}

//******************************************************************************************
BOOL CMCMech::HasLegMotion(CMesh *mesh)	// FUNCTION: 0x0049c2d0
{
	return mesh->GetAnimModeByName("LegMotion") != AM_INVALID;
}
