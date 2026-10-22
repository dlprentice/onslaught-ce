// MeshCollisionVolume.cpp: reconstructed from MeshCollisionVolume.obj (004abd60..004ade4f); Stuart's release has no
// copy. The unit cube's corners and the eight triangles CMeshCollisionVolume tests against a part's bounding box
// are this file's statics (00704af8..00704c58).
#include "common.h"
#include "MeshCollisionVolume.h"
#include "Sphere.h"
#include "cylinder.h"
#include "mathsconst.h"
#include "mesh.h"
#include "RenderThing.h"
#include "DebugLog.h"
#include "PolyBucket.h"
#include "Motioncontroller.h"
#include "eventmanager.h"
#include "Line.h"

// FUNCTION: 0x004abd60 init:ZERO_FVECTOR
// FUNCTION: 0x004abd80 init:ID_FMATRIX

// Source helper: a part's transform, cached again when it is stale.
static inline CMeshCollisionPartTransform *PartTransform(CMeshCollisionVolume *v, CMesh *mesh, SINT i)
{
	float now = EVENT_MANAGER.GetTime();
	if ((!v->mTransforms) || (v->mTransforms[i].mFrame < 0))
		v->CachePartTransform(mesh, i, now);
	if ((!v->mUnknown20) && (v->mTransforms[i].mFrame < now))
		v->CachePartTransform(mesh, i, now);
	return &v->mTransforms[i];
}


//******************************************************************************************
// A cylinder is tested as the sphere around it: its centre, and the larger of its radius and height.
BOOL CMeshCollisionVolume::CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report)	// FUNCTION: 0x004abe50
{
	float	height = other->mHeight;
	float	radius = other->mRadius;
	if (radius <= height)
		radius = height;
	CSphere	sphere(radius);
	sphere.mPos = other->mPos;
	return CollideWithSphere(a, move, &sphere, report);
}

static FVector	*sBoxTriangles[12][3];		// 00704bf8: room for a box's 12 triangles (retail's slot is 144 bytes); the test uses the first 8
// sBoxCorners' empty initializer (a lone ret: FVector's constructor is empty)
// FUNCTION: 0x004abef0 initorder:3
static FVector	sBoxCorners[8];				// 00704b78
// FUNCTION: 0x004abf00 init:sCorner0
static FVector	sCorner0(-1.0f, -1.0f, -1.0f);	// 00704b68
// FUNCTION: 0x004abf20 init:sCorner1
static FVector	sCorner1(-1.0f,  1.0f, -1.0f);	// 00704b58
// FUNCTION: 0x004abf40 init:sCorner2
static FVector	sCorner2( 1.0f,  1.0f, -1.0f);	// 00704b48
// FUNCTION: 0x004abf60 init:sCorner3
static FVector	sCorner3( 1.0f, -1.0f, -1.0f);	// 00704b38
// FUNCTION: 0x004abf80 init:sCorner4
static FVector	sCorner4(-1.0f, -1.0f,  1.0f);	// 00704b28
// FUNCTION: 0x004abfa0 init:sCorner5
static FVector	sCorner5(-1.0f,  1.0f,  1.0f);	// 00704b18
// FUNCTION: 0x004abfc0 init:sCorner6
static FVector	sCorner6( 1.0f,  1.0f,  1.0f);	// 00704b08
// FUNCTION: 0x004abfe0 init:sCorner7
static FVector	sCorner7( 1.0f, -1.0f,  1.0f);	// 00704af8
static BOOL		sBoxTrianglesReady = FALSE;		// 00704cc8

//******************************************************************************************
// One block per triangle: the blocks' few extra nodes put this just over VC6's plain auto-inline cap, so
// TestSweptSphereAgainstBox calls it (004ac14c) as retail does; the code is the same.
void InitBoxTriangles()	// FUNCTION: 0x004ac000
{
	{
		sBoxTriangles[0][0] = &sCorner4;
		sBoxTriangles[0][1] = &sCorner0;
		sBoxTriangles[0][2] = &sCorner3;
	}
	{
		sBoxTriangles[1][0] = &sCorner3;
		sBoxTriangles[1][1] = &sCorner7;
		sBoxTriangles[1][2] = &sCorner4;
	}
	{
		sBoxTriangles[2][0] = &sCorner5;
		sBoxTriangles[2][1] = &sCorner2;
		sBoxTriangles[2][2] = &sCorner1;
	}
	{
		sBoxTriangles[3][0] = &sCorner2;
		sBoxTriangles[3][1] = &sCorner5;
		sBoxTriangles[3][2] = &sCorner6;
	}
	{
		sBoxTriangles[4][0] = &sCorner1;
		sBoxTriangles[4][1] = &sCorner0;
		sBoxTriangles[4][2] = &sCorner4;
	}
	{
		sBoxTriangles[5][0] = &sCorner4;
		sBoxTriangles[5][1] = &sCorner5;
		sBoxTriangles[5][2] = &sCorner1;
	}
	{
		sBoxTriangles[6][0] = &sCorner2;
		sBoxTriangles[6][1] = &sCorner7;
		sBoxTriangles[6][2] = &sCorner3;
	}
	{
		sBoxTriangles[7][0] = &sCorner7;
		sBoxTriangles[7][1] = &sCorner2;
		sBoxTriangles[7][2] = &sCorner6;
	}
	sBoxTrianglesReady = TRUE;
}

//******************************************************************************************
CMeshCollisionVolume::~CMeshCollisionVolume()	// FUNCTION: 0x004ac0e0
{
	if (mTransforms)
		delete mTransforms;
	mTransforms = NULL;
}

//******************************************************************************************
// A sphere of the given radius moving from pos by move, against a part's bounding box: the sphere around the
// whole move must overlap the box; then either the box itself (its distance outside the box) or, when the
// report asks for it, the eight triangles of the box scaled from the unit cube.
BOOL CMeshCollisionVolume::TestSweptSphereAgainstBox(CMeshPart *part_index, CMeshPart *part, const FVector *pos, const FVector *move, const float *radius, CCollisionReport *report)	// FUNCTION: 0x004ac140
{
	if (!sBoxTrianglesReady)
		InitBoxTriangles();

	BOOL	hit = FALSE;
	FVector	end = *pos + *move;
	// Separate differences retain the retail radius and midpoint temporary boundaries.
	float	r = (end - *pos).Magnitude() * 0.5f + *radius;
	FVector	centre = (end - *pos) * 0.5f + *pos;
	CBoundingBox	*box = part->mBoundingBox;
	FVector	*extent = &box->mAxes;
	centre -= box->mOrigin;

	if ((centre.X - r > extent->X) || (-extent->X > centre.X + r) ||
		(centre.Y - r > extent->Y) || (-extent->Y > centre.Y + r) ||
		(centre.Z - r > extent->Z) || (-extent->Z > centre.Z + r))
		return FALSE;

	if (!report->mTestTriangles)
	{
		float	dist = DistanceOutsideBox(&centre, extent) - r;
		if (dist > 0)
			return FALSE;
		report->mDistance = dist;
		report->mPart = part_index;
		report->mUnknownA8 = TRUE;
		report->mUnknown94 = TRUE;
		return TRUE;
	}

	for (SINT i = 0; i < 8; i++)
	{
		FVector	a = *sBoxTriangles[i][0];
		FVector	b = *sBoxTriangles[i][1];
		FVector	c = *sBoxTriangles[i][2];
		a.X *= extent->X;
		a.Y *= extent->Y;
		a.Z *= extent->Z;
		b.X *= extent->X;
		b.Y *= extent->Y;
		b.Z *= extent->Z;
		c.X *= extent->X;
		c.Y *= extent->Y;
		c.Z *= extent->Z;
		a += box->mOrigin;
		b += box->mOrigin;
		c += box->mOrigin;
		if (TestSweptSphereAgainstTriangle(&a, &b, &c, pos, move, *radius, report))
			hit = TRUE;
	}

	if (hit == TRUE)
		report->mPart = part_index;
	return hit;
}

//******************************************************************************************
// The same swept sphere against the part's own triangles, from its poly bucket.
BOOL CMeshCollisionVolume::TestSweptSphereAgainstPart(CMeshPart *part_index, CMeshPart *part, const FVector *pos, const FVector *move, const float *radius, CCollisionReport *report)	// FUNCTION: 0x004ac4a0
{
	BOOL				hit = FALSE;
	CPolyBucketVertex	*tri[3];
	FVector	end = *pos + *move;
	FVector	centre = (end + *pos) * 0.5f;
	float	r = (end - *pos).Magnitude() * 0.5f + *radius;

	BOOL	found = part->GetFirstPoly(&centre, r, tri);
	CPolyBucketPacking	*packing = &part->mPolyBucket->mPacking;
	if (found == TRUE)
	{
		do
		{
			FVector	a, b, c;
			tri[0]->Unpack(&a, packing);
			tri[1]->Unpack(&b, packing);
			tri[2]->Unpack(&c, packing);
			if (TestSweptSphereAgainstTriangle(&a, &b, &c, pos, move, *radius, report))
			{
				hit = TRUE;
				report->mPart = part_index;
			}
		}
		while (part->GetNextPoly(tri) == TRUE);
	}
	return hit;
}

//******************************************************************************************
// A sphere swept along the move against the render thing's mesh: every collision part (its current damage
// level) is tested in its own frame (the cached transforms), by its box when mode 1 is set (+0x18, used once)
// or by its triangles; each contact is recorded, up to six, and the move is then resolved against it, again
// while the tests report a hit. The passes are counted at the loop's foot with the hit flag tested at its head
// (004ac7cc), each test sets hit in its own arm before the shared `if (!hit) continue;` (004aca08..004aca51), the end
// returns through one shared `return hit;`, and the contact count is incremented before `hit = FALSE;` (004acab8).
// The render thing is named for the motion controller, as in CollideWithLine: retail reloads it into eax after storing
// mesh (004ac72c). The contact position is named for the transform back to the world: with the rt local, that keeps
// the product's rows and terms in retail's order (004acbeb..004acc4b).
BOOL CMeshCollisionVolume::CollideWithSphere(void *a, CCollisionMove *move, CSphere *sphere, CCollisionReport *report)	// FUNCTION: 0x004ac6e0
{
	SINT	passes = 0;
	SINT	mode = mUnknown18;
	mUnknown18 = 0;
	if ((!mRenderThing) || (!mRenderThing->GetRTMesh()))
		return FALSE;

	report->mNumContacts = 0;
	CMesh				*mesh = mRenderThing->GetRTMesh();
	CRenderThing		*rt = mRenderThing;
	CMotionController	*mc = rt->mThing->GetMotionController();
	BOOL				hit = FALSE;
	report->mUnknownA8 = TRUE;
	FVector		start = move->mPos - move->mMove;
	FVector		pos; pos = start;
	FVector		dir = move->mMove;
	FVector		travel = move->mMove;

	while (passes < 6)
	{
		if (report->mUnknownA8 != TRUE)
			break;
		report->mUnknownA8 = FALSE;
		report->mStuck = FALSE;
		for (SINT i = 0; i < mesh->mNumParts; i++)
		{
			CMeshPart	*part = mesh->mParts[i];
			if ((!part->GetMeshDataPart()) || (part->GetMeshDataPart()->mPartType != 1))
				continue;

			SINT	flags = 0;
			if (mc)
			{
				mc->Unknown6(part, &flags);
				SINT	level = mc->Unknown7(part);
				if ((level) && (level > 0))
				{
					for (; level; level--)
						if (part)
							part = part->mNextMeshPartInChain;
				}
			}
			if ((flags & 1) || (!part))
				continue;

			CMeshCollisionPartTransform	*t = PartTransform(this, mesh, i);
			FVector	lstart = t->mInverse * (start - t->mPos);
			FVector	lmove = t->mInverse * travel;
			CMeshPart	*collision = part->GetMeshDataPart();
			if (mode == 1)
			{
				if (TestSweptSphereAgainstBox(part, collision, &lstart, &lmove, &sphere->mRadius, report) == TRUE)
					hit = TRUE;
			}
			else
			{
				if (TestSweptSphereAgainstPart(part, collision, &lstart, &lmove, &sphere->mRadius, report) == TRUE)
					hit = TRUE;
			}
			if (!hit)
				continue;

			if (report->mUnknownD0)
				return TRUE;
			if (!report->mTestTriangles)
			{
				report->mContactParts[report->mNumContacts] = report->mPart->mPartNumber;
				report->mContactDistances[report->mNumContacts] = report->mDistance;
				report->mContactPos = pos;
				report->mNumContacts++;
				hit = FALSE;
				if (report->mNumContacts >= 6)
					return TRUE;
			}
		}

		if ((report->mUnknownA8 == TRUE) || (report->mStuck == TRUE))
		{
			if (!report->mTestTriangles)
				return TRUE;
			report->mUnknownA8 = TRUE;
			report->mContactParts[report->mNumContacts] = report->mPart->mPartNumber;
			report->mContactDistances[report->mNumContacts] = report->mDistance;

			CMeshCollisionPartTransform	*t = PartTransform(this, mesh, report->mPart->mPartNumber);
			FMatrix	&o = t->mOrientation;
			FVector	&c = report->mContactPos;
			report->mContactPos = o * c + t->mPos;
			if (!ResolveContact(report, start, travel, sphere->mRadius, &start, &travel))
			{
				move->mPos = pos;
				move->mMove = FVector(0, 0, 0);
				move->mStopped = TRUE;
				return TRUE;
			}
			if (dir * travel < 0)
			{
				move->mPos = pos;
				move->mMove = FVector(0, 0, 0);
				move->mStopped = TRUE;
				return TRUE;
			}
			report->mNumContacts++;
			// Retail 004acd55..004acd64 retains the accumulated hit after resolving a contact.
			// A later miss still returns TRUE and publishes the shortened movement.
		}

		passes++;
	}

	if (hit)
	{
		if (report->mStuck == TRUE)
		{
			move->mPos = pos;
			move->mStopped = TRUE;
			return TRUE;
		}
		move->mPos = travel + start;
		move->mMove = travel;
		move->mStopped = TRUE;
	}
	return hit;
}

//******************************************************************************************
// STAND-IN (operand order, ResolveContact only): FContactVector's members are file-local copies of FVector's dot
// product, sum and scale, spelled to reproduce retail's operand ranks.
// - Dot names a.Z before a.Y, which gives the leading length retail's y, z, x sum (004acf33..004acf51; with maths.h's
//   operator the sum is z, y, x: 0/4).
// - operator*(float), used by the advance, brackets the Y and Z products. The bracket nodes move the later CSE
//   temporaries of normal.Y and normal.Z into the next 32-id block, where their squares' rank keys wrap below X's,
//   so both later Normalise calls sum x, z, y as retail (004ad2b0..004ad2c2, 004ad3d2..004ad3e6; maths.h's operator:
//   15/15, both sums z, y, x).
// - AddZ adds correction's X through the late zero (see ResolveContact): the add node outranks the pos.X leaf, so the
//   X sum of pos + correction loads correction first as retail (004ad1b1; operator+: 0/4).
struct FContactVector : public FVector
{
	float Dot(const FVector &a) const { return (X * a.X) + (Z * a.Z) + (Y * a.Y); }
	FVector AddZ(const FVector &a, float zero) const { return FVector(X + (a.X + zero), Y + a.Y, Z + a.Z); }
	FVector operator*(float f) const { return FVector(X * f, ((Y)) * f, ((Z)) * f); }
};

// STAND-IN (operand order and inline budget): the triangle block's dot product with late zeros on its X and Y
// products. The adds outrank the Z product, so the sum is y, x, then z as retail (004ad32b..004ad346; with maths.h's
// operator the sum is y, z, x: 22/40). The cast on p.Z emits nothing and costs one inline-size unit, which keeps the
// final dot product out of line as retail calls it (004ad5b3, budget 56 against its 57; without the cast 22/32).
static inline float DotZ(const FVector &p, const FVector &a, float zero)
{
	return ((p.X + zero) * a.X) + ((p.Y + zero) * a.Y) + ((float)p.Z * a.Z);
}

// STAND-IN (inline budget): a plane's normal through a file-static accessor (name and signature ours). The j loop
// reads the j-th normal through it twice; the two offered sites (size 24, no spending) raise the earlier sites'
// counts of sites left, which keeps the stuck and non-stuck correction constructors out of line (004ad09c, 004ad107,
// 004ad170) while the stuck arm's difference stays inline. Without them 27/47; one read through it is not enough.
static inline FVector &NormalOf(CCollisionReport *report, SINT n)
{
	return report->mNormals[n];
}

// STAND-IN (inline budget): the new contact's normal is stored through this file-local helper (normal by value). Its
// size is spent at the store, which keeps the final dot product out of line as retail calls it (004ad5b3, 0040d180);
// stored directly the dot product is expanded (29/49).
static inline void AddContactNormal(CCollisionReport *report, FVector normal)
{
	report->mNormals[report->mNumContacts] = normal;
}

//******************************************************************************************
// Move to the contact, then try a slide against each recorded plane. Two incompatible planes leave only their
// intersection direction; three or more return FALSE. The radius argument is present but unused in retail.
// direction (the normalised travel, used only by the non-stuck arm) and normal are separate locals, and normal is
// assigned only on the paths that read it: retail copies travel's pad into direction's home at once (004acf64) and
// loads normal's pad, still unassigned on the non-stuck path, into ebp at that arm's end (004ad195); the two share a
// frame slot. As one variable the pad stayed in ebp throughout. The non-stuck arm names its distance inside the
// advance's argument: retail loads the element once, keeps it on the FPU for the products and stores it with fst
// (004ad0dd..004ad0e4), then reads the element again for the test after the constructor call (004ad126). The plane
// loop names its normal as a reference (the slide product reads it twice). `if (length)` rather than `!= 0`: two
// inline-size units less, without which the final dot product expands (22/32).
// STAND-IN (late zero): zero is a local the global optimizer propagates after the commutative operands are ranked
// (C2 0x107130cb, as in Hud RenderTargetMarkers 00484340), so `x + zero` ranks as an add node and then folds to x: it
// orders the X sum of the start (AddZ) and the triangle block's dot product (DotZ) and emits nothing.
BOOL CMeshCollisionVolume::ResolveContact(CCollisionReport *report, FVector pos, FVector travel, float radius, FVector *start, FVector *move)	// FUNCTION: 0x004acf30
{
	FVector correction(0, 0, 0);
	FVector direction = travel;
	FVector normal;
	FVector advance;
	BOOL stuck = FALSE;
	float zero = 0;
	float length = sqrtf(static_cast<const FContactVector &>(travel).Dot(travel));
	if (length)
	{
		length = 1.0f / length;
		direction *= length;
	}
	if (report->mStuck == TRUE)
	{
		normal = pos - report->mContactPos;
		normal.Normalise();
		stuck = TRUE;
		correction = normal * report->mContactDistances[report->mNumContacts];
		advance = correction;
	}
	else
	{
		float distance;
		advance = static_cast<const FContactVector &>(direction) * (distance = report->mContactDistances[report->mNumContacts]);
		if (report->mContactDistances[report->mNumContacts] > 0.01)
			correction = direction * (distance * 0.9f);
	}
	*start = static_cast<const FContactVector &>(pos).AddZ(correction, zero);
	if (report->mTestTriangles == 1)
	{
		*move = ZERO_FVECTOR;
		return TRUE;
	}
	if (!stuck)
		normal = (pos + advance) - report->mContactPos;
	normal.Normalise();
	FVector slide;
	if (report->mTriangleNormal.Z > -0.89f && report->mTriangleNormal.Z < -0.01f)
	{
		float dot = DotZ(normal, travel, zero);
		slide = travel - normal * dot;
		if (slide.Z <= 0)
		{
			normal.Z = 0;
			normal.Normalise();
		}
	}
	AddContactNormal(report, normal);
	SINT numPlanes = report->mNumContacts + 1;
	SINT i;
	for (i = 0; i < numPlanes; i++)
	{
		const FVector &n = report->mNormals[i];
		slide = travel - n * (n * travel);
		SINT j;
		for (j = 0; j < numPlanes; j++)
			if (j != i && report->mNormals[i] != NormalOf(report, j) && slide * NormalOf(report, j) < 0)
				break;
		if (j == numPlanes)
			break;
	}
	if (i != numPlanes)
	{
		*move = slide;
		return TRUE;
	}
	if (numPlanes != 2)
		return FALSE;
	{
		FVector dir = report->mNormals[0] ^ report->mNormals[1];
		dir.Normalise();
		float distance = dir * travel;
		*move = dir * distance;
	}
	return TRUE;
}

//******************************************************************************************
#line 549
void CMeshCollisionVolume::CachePartTransform(CMesh *mesh, SINT part, float frame)	// FUNCTION: 0x004ad600
{
	if (!mTransforms)
	{
		mTransforms = new(MEMTYPE_COLLISION) CMeshCollisionPartTransform[mesh->mNumParts];
		for (SINT i = 0; i < mesh->mNumParts; i++)
			mTransforms[i].mFrame = -1.0f;
	}

	CMeshPart	*p = mesh->mParts[part];
	if (p)
	{
		FVector	pos;
		FMatrix	ori;
		if (!mUnknown1C)
			CMeshPart::EvaluatePose(mRenderThing, mRenderThing->mThing, p, &pos, &ori, 0, 0);
		else
			p->GetWorldTransforms(0, 0, 0, mRenderThing->mThing, &ori, &pos, 0, TRUE, 0);

		mTransforms[part].mOrientation = ori;
		ori.TransposeInPlace();
		mTransforms[part].mInverse = ori;
		mTransforms[part].mPos = pos;
		mTransforms[part].mFrame = frame;
	}
	else
		LOG.AddMessage("Error: Can't find mesh part in");
}

//******************************************************************************************
void CMeshCollisionVolume::SetRenderThing(CRenderThing *rt)	// FUNCTION: 0x004ad7f0
{
	if (mTransforms)
		delete mTransforms;
	mTransforms = NULL;
	mRenderThing = rt;
}


//******************************************************************************************
// Test the line in each collision part's frame, retaining the closest triangle hit. A response moves the line's
// movement record to that contact; the report receives the contact and triangle normal in world coordinates.
// STAND-IN (spelling), two parts. (a) The triangles are unpacked by
// UnpackLineVertex, CPolyBucketPacking::Unpack spelt with brackets and a named X chosen by measurement: their no-code
// x87 nodes bring the do-while body past the 81-node scheduler cap so its cut falls inside the third unpack, and the
// report push is issued there as in retail (004adc15); with the header's Unpack the block is exactly 81 nodes and the
// push goes first. (b) The hit block names the hit part, the orientation and the contact (hitPart, ori, contact), all
// consumed: locals first referenced last shift only the inliner's ids, by three, so end's operator+ binds this and its
// parameter at 7 and 0 mod 8 (IND keys 0xe007, 0x0007) and loads move->mPos.X first (004ad8b3). Stuart's spelling is
// unknown.
static inline void UnpackLineVertex(CPolyBucketPacking *packing, CPolyBucketVertex *in, FVector &out)
{
	float X0 = (((in->X * packing->mScale) * (1.0f / 32767.0f)) + packing->mOrigin.X);
	out.X = X0;
	out.Y = ((in->Y * packing->mScale) * (1.0f / 32767.0f)) + packing->mOrigin.Y;
	out.Z = (((in->Z * packing->mScale) * (1.0f / 32767.0f)) + packing->mOrigin.Z);
}

BOOL CMeshCollisionVolume::CollideWithLine(void *a, CCollisionMove *move, CLine *line, CCollisionReport *report)	// FUNCTION: 0x004ad830
{
	mUnknown18 = 0;
	if ((mRenderThing == NULL) || (mRenderThing->GetRTMesh() == NULL))
		return FALSE;
	CMesh *mesh = mRenderThing->GetRTMesh();
	CRenderThing *rt = mRenderThing;
	CMotionController *mc = rt->mThing->GetMotionController();
	BOOL hit = FALSE;
	FVector start = move->mPos + line->mStart;
	FVector end = move->mPos + line->mEnd;
	report->mUnknownA8 = FALSE;
	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		BOOL partHit = FALSE;
		CMeshPart *part = mesh->mParts[i];
		if (!part->GetMeshDataPart())
			continue;
		if (part->GetMeshDataPart()->mPartType != 1)
			continue;
		SINT flags = 0;
		if (mc)
		{
			mc->Unknown6(part, &flags);
			SINT level = mc->Unknown7(part);
			if ((level) && (level > 0))
				for (; level; level--)
					if (part)
						part = part->mNextMeshPartInChain;
		}
		if ((flags & 1) || (!part))
			continue;
		CMeshCollisionPartTransform *t = PartTransform(this, mesh, i);
		FVector localStart = t->mInverse * (start - t->mPos);
		FVector localEnd = t->mInverse * (end - t->mPos);
		CMeshPart *collision = part->GetMeshDataPart();
		CPolyBucketVertex *tri[3];
		BOOL found = collision->GetFirstPolyLine(&localStart, &localEnd, tri);
		CPolyBucketPacking *packing = &collision->mPolyBucket->mPacking;
		if (found == TRUE)
		{
			do
			{
				FVector a, b, c;
				UnpackLineVertex(packing, tri[0], a);
				UnpackLineVertex(packing, tri[1], b);
				UnpackLineVertex(packing, tri[2], c);
				if (TestSegmentAgainstTriangle(&a, &b, &c, &localStart, &localEnd, report))
				{
					if (report->mUnknownD0)
						return TRUE;
					hit = TRUE;
					partHit = TRUE;
					report->mPart = part;
				}
			}
			while (collision->GetNextPolyLine(tri, partHit) == TRUE);
		}
	}
	if (hit)
	{
		CMeshPart *hitPart = report->mPart;
		report->mContactParts[0] = hitPart->mPartNumber;
		report->mContactDistances[0] = report->mDistance;
		report->mNumContacts = 1;
		SINT part = hitPart->mPartNumber;
		CMeshCollisionPartTransform *t = PartTransform(this, mesh, part);
		const FMatrix &ori = t->mOrientation;
		FVector &contact = report->mContactPos;
		contact = ori * contact + t->mPos;
		report->mNormals[0] = ori * report->mTriangleNormal;
		if (report && report->mTestTriangles)
		{
			move->mPos = report->mContactPos;
			move->mStopped = TRUE;
		}
	}
	return hit;
}

// Header inline emitted by ResolveContact; its retail call is at 004ad5b3.
// FUNCTION: 0x0040d180 ??DFVector@@QBEMABV0@@Z

// Two meshes never collide here. Out of line: retail folds it into the shared FALSE answer at 004098b0 with
// CLine::CollideWithLine and CRelaxedSquad::Unknown75, and LINK folds an out-of-line body only with out-of-line ones.
BOOL CMeshCollisionVolume::CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report)
{
	return FALSE;
}
