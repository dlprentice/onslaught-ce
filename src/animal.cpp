// animal.cpp: CAnimal, the birds that flock over a level (reconstructed; Stuart's file is not in his release). Each
// follows another picked at random, turning towards it a little each frame and climbing or diving to stay between
// 30 and 60 metres up.

#include	"Common.h"

#define ANIMAL_CPP
#include	"animal.h"
#include	"InitThing.h"
#include	"RenderThing.h"
#include	"console.h"
#include	"eventmanager.h"
#include	"mapwhoentry.h"

// The per-file maths constants from maths.h:
// FUNCTION: 0x00403c40 init:ZERO_FVECTOR
// FUNCTION: 0x00403c60 init:ID_FMATRIX

#define ANIMAL_EVENT_MOVE	3000

CAnimal	*CAnimal::sFirst = NULL;						// 00660130
SINT	CAnimal::sCount = 0;						// 00660134

//*********************************************************************************
void	CAnimal::Init(CInitThing *init)	// FUNCTION: 0x00403d30
{
	mVelocity = init->mVelocity;
	mOldPos = init->mPos;
	mOldOrientation = init->mOrientation;
	mLeader = NULL;
	mClimb = 0;

	CRenderData	data;
	data.mInit[0].mThing = this;
	mType = ((CAnimalInitThing *)init)->mType;
	mFlags |= TF_IN_MAP_WHO;
	data.mRTID[0] = 1;

	if (mType == 0)
		mFlags |= TF_DONT_RENDER;
	else if (mType == 1)
	{
		strcpy(data.mInit[0].mName, "bird.msh");
		mHeight = -((float)(rand() * 20.f) / RAND_MAX + 40.f);
	}
	else
		CONSOLE.Print("Warning! Unknown animal type %d generated!\n", mType);

	mRenderThing = CreateRenderThing(data.mRTID[0]);
	if (mRenderThing)
		mRenderThing->Init(&data.mInit[0]);

	SUPERTYPE::Init(init);

	mPos.Z = mHeight;
	mNext = sFirst;
	sFirst = this;
	sCount++;

	if (mType)
		EVENT_MANAGER.AddEvent(ANIMAL_EVENT_MOVE, this, NEXT_FRAME);
}

// Init's local CRenderData: its implicit destructor destroys mInit[1] through __ehvec_dtor.
// FUNCTION: 0x00403ff0 ??1CRenderData@@QAE@XZ

//*********************************************************************************
// The traversal pointer is reused to find the unlink predecessor. Retail reloads sFirst even when the first
// traversal starts empty (00404024 -> 00404044).
CAnimal::~CAnimal()	// FUNCTION: 0x00404010
{
	// retail assigns rather than compares here (40401d..404042), so every animal loses its leader
	CAnimal *a;
	for (a = sFirst; a; a = a->mNext)
		if (a->mLeader = this)
			a->mLeader = NULL;

	if (a = sFirst)
	{
		if (a == this)
		{
			sFirst = mNext;
			sCount--;
		}
		else
		{
			while ((a->mNext != this) && (a))
				a = a->mNext;
			if (a)
			{
				a->mNext = mNext;
				sCount--;
			}
		}
	}
}

//*********************************************************************************
void	CAnimal::HandleEvent(CEvent *event)
{
	switch (event->GetEventNum())
	{
	case ANIMAL_EVENT_MOVE:
		Move();
		break;
	default:
		SUPERTYPE::HandleEvent(event);
	}
}

// STAND-IN (ids) for CAnimal::Move below: the yaw gap through a forced-inline helper whose thirteen named locals take
// inline-time symbol ids. Retail's dead identity matrix (Stuart's `FMatrix m(1, 0, 0, 0, 1, 0, 0, 0, 1)`, whose second
// and third rows retail calls out of line at 00404352 and 00404364) frees its constructor's ten parameter ids just
// before MakeRotationF. Retail's rotation locals rank cp/sp above cr/sr above cy/sy, which needs at least thirteen
// inline-time allocations between the two: one helper with N locals here gives 4 -> 12/16, 8 -> 6/8, 12 -> 3/3,
// 13..16 -> exact. Parameters bound to plain variables take no id, so only the locals count. Each local is a copy
// consumed by the next and the last is the gap; forced inlining spends no inline budget (one site in `left`), and the
// copies emit no code. Plain form (the gap written in place, the identity kept): 44/152. Original spelling unknown.
static __forceinline float YawGap(float want, float yaw)
{
	float	g0 = want - yaw;
	float	g1 = g0;
	float	g2 = (float)fabs(g1);
	float	g3 = g2;
	float	g4 = g3;
	float	g5 = g4;
	float	g6 = g5;
	float	g7 = g6;
	float	g8 = g7;
	float	g9 = g8;
	float	g10 = g9;
	float	g11 = g10;
	float	g12 = g11;
	return g12;
}

//*********************************************************************************
// follow the leader: turn towards it, keep between 30 and 60 up, and fly on
// The direction to the leader is a vector of its own, taken before v becomes the heading and turned into an angle
// after: retail keeps dx and dy on the x87 stack across the heading's stores (004042bc..004042df) and reloads the
// heading from v's slot for both angles; with the angle taken first (`(v - mPos).Azimuth()`) the heading stays in
// registers (46/48 -> 16/18). Matched with Stuart's dead identity matrix after the angles, as in
// CMCTentacle::UpdateBone: retail calls its second and third rows' constructor out of line at 00404352 and 00404364
// (the first row and everything else expand), and with the labelled STAND-IN (ids) YawGap above, which supplies the
// inline-time symbol ids retail's rotation locals need after the identity frees its constructor's ten. Specimen:
// BEA.exe.original.backup, SHA-256 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750.
void	CAnimal::Move()	// FUNCTION: 0x00404210
{
	mOldPos = mPos;
	mOldOrientation = mOrientation;

	if ((!mLeader) || (rand() % 50 == 1))
	{
		SINT	n = rand() % sCount;
		CAnimal	*a = sFirst;
		while ((n > 0) && (a))
		{
			a = a->mNext;
			n--;
		}
		if (a != this)
			mLeader = a;
	}

	if (mLeader)
	{
		FVector	v = mLeader->mPos;
		FVector	dir = v - mPos;
		v = mOrientation.GetColumnY();
		float	want_yaw = dir.Azimuth();
		float	yaw = v.Azimuth();
		float	pitch = v.Elevation();

		FMatrix	m(1, 0, 0, 0, 1, 0, 0, 0, 1);

		float	diff = YawGap(want_yaw, yaw);	// STAND-IN (ids), see YawGap
		if (diff < 0.03f)
			yaw = want_yaw;
		else if (yaw < want_yaw)
		{
			if (diff > PI)
				yaw -= 0.03f;
			else
				yaw += 0.03f;
		}
		else
		{
			if (diff > PI)
				yaw += 0.03f;
			else
				yaw -= 0.03f;
		}

		if (rand() % 100 == 0)
			mClimb = (float)(rand() * 0.5f) / RAND_MAX - 0.25f;
		if (mPos.Z > -30.f)
			mClimb = -0.25f;
		if (mPos.Z < -60.f)
			mClimb = 0.25f;

		if (pitch > mClimb)
			pitch -= 0.01f;
		if (pitch < mClimb)
			pitch += 0.01f;

		mOrientation.MakeRotationF(yaw, pitch, 0);
		mVelocity = mOrientation * FVector(0.0f, 0.03f, 0.0f);
		mPos += mVelocity;
	}

	mMapWhoEntry.UpdateEntry(mPos);

	if (!(mFlags & TF_DECLARED_SHUTDOWN))
		EVENT_MANAGER.AddEvent(ANIMAL_EVENT_MOVE, this, NEXT_FRAME);
}

//*********************************************************************************
void	CAnimal::Render(DWORD flags)	// FUNCTION: 0x004045d0
{
	CThing::Render(flags);
}
