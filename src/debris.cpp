// debris.cpp: CDebris, the tufts of grass kept in a square around the camera (reconstructed; Stuart's file is not in
// his release). Each frame CDXEngine::Render wraps the ones that fall out of the square to its far side.

#include	"Common.h"
#include	"maths.h"

// A zero vector from a header not yet known comes first (00441090 initializes 0066eb28), then the maths constants.
static FVector	DEBRIS_ZERO(0.0f, 0.0f, 0.0f);		// 0066eb28
// FUNCTION: 0x00441090 init:DEBRIS_ZERO

#define DEBRIS_CPP
#include	"debris.h"
#include	"InitThing.h"
#include	"RenderThing.h"
#include	"console.h"
#include	"meshrenderer.h"
#include	"map.h"
#include	"mapwhoentry.h"

// The per-file maths constants from maths.h:
// FUNCTION: 0x004410b0 init:ZERO_FVECTOR
// FUNCTION: 0x004410d0 init:ID_FMATRIX

float	gDebrisArea = 20.f;						// 006282fc: half the square's side
float	gDebrisFadeStart = 15.f;				// 00628300
float	gDebrisFadeEnd = 20.f;					// 00628304

CDebris	*CDebris::sFirst;						// 0066eb78

//*********************************************************************************
void	CDebris::Init(CInitThing *init)	// FUNCTION: 0x004411a0
{
	CRenderData	data;
	data.mInit[0].mThing = this;
	data.mRTID[0] = 1;
	strcpy(data.mInit[0].mName, "grs-tuft1.MSH");

	mRenderThing = CreateRenderThing(data.mRTID[0]);
	if (mRenderThing)
		mRenderThing->Init(&data.mInit[0]);

	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	SUPERTYPE::Init(init);
	StickToGround();

	CONSOLE.RegisterVariable("cg_debrisarea", "The size of the debris area", CVar_float, &gDebrisArea);
	CONSOLE.RegisterVariable("cg_debrisfadestart", "The distance at which debris starts to fade", CVar_float, &gDebrisFadeStart);
	CONSOLE.RegisterVariable("cg_debrisfadeend", "The distance at which debris fades completely", CVar_float, &gDebrisFadeEnd);

	mNext = sFirst;
	sFirst = this;
}

//*********************************************************************************
CDebris::~CDebris()	// FUNCTION: 0x00441320
{
	CDebris	*prev = NULL;
	CDebris	*d = sFirst;
	while (d != this)	// STAND-IN (layout; review F 2026-10-15): the null test as a break; `while (d && d != this)` loses 00441320
	{
		if (!d)
			break;
		prev = d;
		d = d->mNext;
	}

	if (prev)
		prev->mNext = mNext;
	else
		sFirst = mNext;
}
// FUNCTION: 0x00441360 ?_GetClassName@CDebris@@UAEPADXZ
// FUNCTION: 0x00441370 ?_GetClassID@CDebris@@UAEHXZ
// FUNCTION: 0x00441380 ??_GCDebris@@UAEPAXI@Z

//*********************************************************************************
void	CDebris::Render(DWORD flags)	// FUNCTION: 0x004413a0
{
	if ((!(mFlags & TF_INVISIBLE)) && (mRenderThing) && (!(mFlags & TF_DONT_RENDER)))
	{
		SINT	alpha = 255 - SINT((mDistance - gDebrisFadeStart) * 255.f / (gDebrisFadeEnd - gDebrisFadeStart));
		if (alpha < 0)
			alpha = 0;
		else if (alpha > 255)
			alpha = 255;

		MESH_ALPHA = alpha;
		mRenderThing->Render(flags);
		MESH_ALPHA = 255;
	}
}

//*********************************************************************************
void	CDebris::RenderImposter()	// FUNCTION: 0x00441420
{
	if ((mRenderThing) && (!(mFlags & TF_DONT_RENDER)))
	{
		SINT	alpha = 255 - SINT((mDistance - gDebrisFadeStart) * 255.f / (gDebrisFadeEnd - gDebrisFadeStart));
		if (alpha < 0)
			alpha = 0;
		else if (alpha > 255)
			alpha = 255;

		MESH_ALPHA = alpha;
		mRenderThing->RenderImposter();
		MESH_ALPHA = 255;
	}
}

//*********************************************************************************
void	CDebris::ShuffleDebris(FVector pos)	// FUNCTION: 0x00441490
{
	for (CDebris *d = sFirst; d; d = d->mNext)
	{
		float	dx = d->mPos.X - pos.X;
		float	dy = d->mPos.Y - pos.Y;
		d->mDistance = (d->mPos - pos).Magnitude();

		bool	moved = false;
		if (dx > gDebrisArea)
		{
			d->mPos.X -= gDebrisArea * 2;
			moved = true;
		}
		if (dx < -gDebrisArea)
		{
			d->mPos.X += gDebrisArea * 2;
			moved = true;
		}
		if (dy > gDebrisArea)
		{
			d->mPos.Y -= gDebrisArea * 2;
			moved = true;
		}
		if (dy < -gDebrisArea)
		{
			d->mPos.Y += gDebrisArea * 2;
			moved = true;
		}

		if (moved)
		{
			d->mPos.Z = MAP.Collide(d->mPos);
			d->Teleport(d->mPos);
			if (CMapWhoEntry *entry = d->GetMapWhoEntry())
				entry->UpdateEntry(d->mPos);
		}
	}
}
// STAND-IN (link layout): retail's 180 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_debris[180] = { 0 };
