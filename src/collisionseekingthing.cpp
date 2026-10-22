// Collision-seeker lifecycle reconstructed from the pristine PC executable.
#define COLLISIONSEEKINGTHING_CPP
#include "common.h"
#include "collisionseekingthing.h"
#include "thing.h"
#include "InitThing.h"
#include "MeshCollisionVolume.h"
#include "Sphere.h"
#include "eventmanager.h"
#include "Line.h"
#include "world.h"
#include "DebugLog.h"

// FUNCTION: 0x00426060 init:ZERO_FVECTOR
// FUNCTION: 0x00426080 init:ID_FMATRIX

void CCollisionSeekingThing::Init(CInitCSThing *init)	// FUNCTION: 0x00426150
{
	mForThing = init->mForThing;
	mCollisionFlags = 0;
	mCollisionFlags |= init->mDesiredCollision;
	mCollisionFlags |= init->mMinCollision << 2;
	mCollisionFlags |= init->mMaxCollision << 4;
	mCollisionFlags |= init->mCollisionResponse << 6;
	mCollisionFlags |= 0x400;
	mCollisionFlags |= init->mDoOBBForMeshCol << 9;
	mIgnoreThing = init->mIgnoreThing;
	mNotSeekCollisionWithBF = init->mNotSeekCollisionWithBF;
	mCollisionMeshVolume = NULL;
	float radius = mForThing->GetBoundingRadius();
	if (!init->mShape)
	{
#line 40
		mShape = new(MEMTYPE_GENERAL_VOLUME) CSphere(radius);
		mRadius = radius;
	}
	else
	{
		mShape = init->mShape;
		mRadius = radius;
	}
	FVector centre;
	mForThing->GetCentrePos(centre);
	mShape->mPos = centre - mForThing->GetPos();
	if (GetMaxCollisionLevel() == ECL_MESH)
	{
#line 57
		mCollisionMeshVolume = new(MEMTYPE_GENERAL_VOLUME) CMeshCollisionVolume(mForThing->GetRenderThing(), init->mUseFixedMeshTransforms, init->mIgnoreAnimationCollision);
	}
}

void CCollisionSeekingThing::SetShape(CGeneralVolume *shape)	// FUNCTION: 0x00426370
{
	delete mShape;
	mShape = shape;
	FVector centre;
	mForThing->GetCentrePos(centre);
	mShape->mPos = centre - mForThing->GetPos();
}

CCollisionSeekingThing::~CCollisionSeekingThing()	// FUNCTION: 0x004263f0
{
	delete mShape;
	delete mCollisionMeshVolume;
}

// FUNCTION: 0x00426460 ??_GCCollisionSeekingThing@@UAEPAXI@Z

void CCollisionSeekingThing::SetCollisionMask(ULONG mask)	// FUNCTION: 0x00426480
{
	mNotSeekCollisionWithBF = mask;
	mCollisionFlags |= 0x100;
}

// The clamped other-level working value is reused after its final level test. Keeping the dispatch result
// for both response and callback admission reproduces retail's shared CMP at 004267ce.
void CCollisionSeekingThing::Unknown6(CCollisionSeekingThing *other)	// FUNCTION: 0x004264a0
{
	if (!(mCollisionFlags & 0x400)) return;
	if (mIgnoreThing == other->mForThing || other->mIgnoreThing == mForThing) return;
	if (other->mForThing->GetFlags() & TF_INVISIBLE) return;
	if (mForThing->GetFlags() & TF_INVISIBLE) return;
	if (other->mForThing->IsDying() && other->mForThing->IsA(THING_TYPE_BUILDING) && !other->mForThing->GetMotionController()) return;
	if (mForThing->IsDying() && mForThing->IsA(THING_TYPE_BUILDING) && !mForThing->GetMotionController()) return;

	SINT response = min((SINT)((mCollisionFlags >> 6) & 3), (SINT)((other->mCollisionFlags >> 6) & 3));
	SINT level = other->mCollisionFlags & 3;
	if ((SINT)((mCollisionFlags >> 2) & 3) > level) level = (mCollisionFlags >> 2) & 3;
	if ((SINT)((mCollisionFlags >> 4) & 3) < level) level = (mCollisionFlags >> 4) & 3;
	SINT otherLevel = mCollisionFlags & 3;
	if ((SINT)((other->mCollisionFlags >> 2) & 3) > otherLevel) otherLevel = (other->mCollisionFlags >> 2) & 3;
	if ((SINT)((other->mCollisionFlags >> 4) & 3) < otherLevel) otherLevel = (other->mCollisionFlags >> 4) & 3;

	CGeneralVolume *volume;
	if (level <= ECL_APPROX_GEOMETRY_SHAPES)
		volume = Unknown7();
	else
	{
		volume = mCollisionMeshVolume;
		if ((other->mCollisionFlags & 0x200) == 0x200)
			((CMeshCollisionVolume *)volume)->mUnknown18 = TRUE;
	}
	CGeneralVolume *otherVolume;
	if (otherLevel <= ECL_APPROX_GEOMETRY_SHAPES)
		otherVolume = other->Unknown7();
	else
	{
		otherVolume = other->mCollisionMeshVolume;
		if (mCollisionFlags & 0x200)
			((CMeshCollisionVolume *)otherVolume)->mUnknown18 = TRUE;
	}
	CCollisionMove move;
	move.mStopped = FALSE;
	move.mPos = volume->mPos + mForThing->GetPos();
	move.mMove = mForThing->GetLocalLastFrameMovement();
	CCollisionMove otherMove;
	otherMove.mStopped = FALSE;
	otherMove.mPos = other->mForThing->GetPos() + otherVolume->mPos;
	otherMove.mMove = other->mForThing->GetLocalLastFrameMovement();
	CCollisionReport report;
	report.mUnknown00 = 0;
	report.mNumContacts = 0;
	report.mUnknownA8 = 0;
	report.mStuck = FALSE;
	report.mUnknownD0 = 0;
	report.mTestTriangles = response;
	report.mRelativeMove = otherMove.mMove - move.mMove;
	if (level == ECL_MESH && otherLevel == ECL_MESH)
	{
		if (otherVolume->GetType() != 3 || volume->GetType() != 3)
		{
			LOG.AddMessage("WARNING: expected a mesh collision volume!");
			return;
		}
		// Movement records were already built from the initial volumes; do not recompute after fallback.
		if (GetRadius() > other->GetRadius())
		{
			((CMeshCollisionVolume *)volume)->mUnknown18 = TRUE;
			otherVolume = other->Unknown7();
		}
		else
		{
			((CMeshCollisionVolume *)otherVolume)->mUnknown18 = TRUE;
			volume = Unknown7();
		}
	}
	// The level is no longer needed; retain the collision result across the two admission checks.
	otherLevel = otherVolume->Collide(&otherMove, &move, volume, &report);
	if (otherLevel && response != ECR_PASSIVE)
	{
		if (!mForThing->IsA(THING_TYPE_STATIONARY) && move.mStopped == TRUE)
		{
			mForThing->MoveTo(move.mPos - volume->mPos);
			mForThing->SetVelocity(move.mMove);
		}
		if (!other->mForThing->IsA(THING_TYPE_STATIONARY) && otherMove.mStopped == TRUE)
		{
			other->mForThing->MoveTo(otherMove.mPos - otherVolume->mPos);
			other->mForThing->SetVelocity(otherMove.mMove);
		}
	}
	if (otherLevel)
	{
		// Cache only the second recipient, as retail does before either hit callback.
		CThing *otherThing = other->mForThing;
		report.mUnknown00 = 1;
		mForThing->Hit(otherThing, &report);
		otherThing->Hit(mForThing, &report);
	}
}

BOOL CCollisionSeekingThing::Unknown8(CCollisionSeekingThing *other)	// FUNCTION: 0x00426900
{
	return !(other->mForThing->GetThingType() & mNotSeekCollisionWithBF);
}

SINT CCollisionSeekingThing::Unknown10(const CSector &sector)	// FUNCTION: 0x00426920
{
	CSector other = sector;
	CSector here = mForThing->GetMapWhoEntry()->GetSectorIn();
	while (other.mLayer < here.mLayer)
	{
		here.mLayer--;
		here.mX >>= 1;
		here.mY >>= 1;
	}
	while (other.mLayer > here.mLayer)
	{
		other.mLayer--;
		other.mX >>= 1;
		other.mY >>= 1;
	}
	SINT dx = abs(here.mX - other.mX);
	SINT dy = abs(here.mY - other.mY);
	return max(dx, dy);
}

void CCSPersistentThing::Init(CInitCSThing *init)	// FUNCTION: 0x004269b0
{
	CCollisionSeekingThing::Init(init);
	if (init->mStartCollideOnNextFrame)
	{
		mCollisionFlags &= ~0x400;
		EVENT_MANAGER.AddEvent(init->mTimeBeforeStart, 3000, this);
	}
	mCollisionDetector.Init(this);
}

void CCSPersistentThing::MovedSector(CSector &from, CSector &to)	// FUNCTION: 0x00426a00
{
	mCollisionDetector.MovedSector(from, to);
}

void CCSPersistentThing::HandleEvent(CEvent *event)	// FUNCTION: 0x00426a20
{
	if (event->GetEventNum() == 3000)
		mCollisionFlags |= 0x400;
}

void CCSRay::Init(CInitCSThing *init)	// FUNCTION: 0x00426a40
{
	CCollisionSeekingThing::Init(init);
	CLine line = *(CLine *)init->mShape;
#line 314
	CLine *worldLine = new(MEMTYPE_UNKNOWN) CLine(line.mStart + mForThing->GetPos(), line.mEnd + mForThing->GetPos());
	CWorldLineColReport report;
	WORLD.FindFirstThingToHitLine(*worldLine, init->mIgnoreThing, &report, FALSE, TRUE, 2, 4, TRUE, -1);
	if (report.mHitType == kCollideThing)
	{
		CCSPersistentThing *other = report.mHitThing->GetCSPT();
		if (other)
			Unknown6(other);
	}
	delete worldLine;
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00426300 ??_GCMeshCollisionVolume@@UAEPAXI@Z
// FUNCTION: 0x00426360 ??1CLine@@UAE@XZ
// FUNCTION: 0x00426fd0 ??2@YAPAXI@Z

// Statics that no code reads (nothing else in the program refers to 006631e0..0066331f): an identity matrix and two
// 0xa0-byte records built from it: a flag, a position, a scale, an orientation, a second position and orientation,
// and three words cleared through one register (integers or pointers; floats would be stored as immediates).
// Retail's .CRT$XCU calls the matrix's initializer first, then 00426c30 and 00426d20, though its code follows theirs.
struct SCSFrame
{
	BOOL			mFlag;							// +0x00
	FVector			mPos;							// +0x04
	FVector			mScale;							// +0x14
	FMatrix			mOrientation;					// +0x24
	FVector			mOldPos;						// +0x54
	FMatrix			mOldOrientation;				// +0x64
	SINT			mUnknown94;						// +0x94
	SINT			mUnknown98;
	SINT			mUnknown9C;

	SCSFrame(BOOL flag, const FVector &scale);
	SCSFrame();		// its body builds the scale after the zero, sharing one temporary (00426d20)
};

static const FMatrix sIdentity(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));	// 00663320
// FUNCTION: 0x00426e10 init:sIdentity

inline SCSFrame::SCSFrame(BOOL flag, const FVector &scale)
{
	FVector	zero(0.0f, 0.0f, 0.0f);
	mFlag = flag;
	mPos = zero;
	mScale = scale;
	mOrientation = sIdentity;
	mOldPos = zero;
	mOldOrientation = sIdentity;
	mUnknown94 = 0;
	mUnknown98 = 0;
	mUnknown9C = 0;
}

inline SCSFrame::SCSFrame()
{
	FVector	zero(0.0f, 0.0f, 0.0f);
	mFlag = TRUE;
	mPos = zero;
	mScale = FVector(1.0f, 0.0f, 0.0f);
	mOrientation = sIdentity;
	mOldPos = zero;
	mOldOrientation = sIdentity;
	mUnknown94 = 0;
	mUnknown98 = 0;
	mUnknown9C = 0;
}

static SCSFrame	sFrame0(FALSE, FVector(1.0f, 1.0f, 1.0f));	// 00663280
// FUNCTION: 0x00426c30 init:sFrame0
static SCSFrame	sFrame1;	// 006631e0
// FUNCTION: 0x00426d20 init:sFrame1

// The second pair of maths constants, after the code (see mathsconst2.h). Open question: whether this pair and the
// operator new copy instead open an object of their own before Compass.obj (00426ff0).
#include "mathsconst2.h"	// FUNCTION: 0x00426ee0 init:ZERO_FVECTOR2
						// FUNCTION: 0x00426f00 init:ID_FMATRIX2
