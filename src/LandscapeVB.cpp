// LandscapeVB.cpp: reconstructed from LandscapeVB.obj (0048f1e0..0048f35e); Stuart's release has no copy.
#include "common.h"
#define LANDSCAPEVB_CPP
#include "maths.h"
#include "DX.h"
#include "LandscapeVB.h"
#include "map.h"

// a vertex: the position and the same x, y again as texture coordinates
struct SLandscapeVBVertex
{
	float	mX, mY, mZ;
	float	mU, mV;
};

//******************************************************************************************
void	CLandscapeVB::Create(SINT size)	// FUNCTION: 0x0048f1e0
{
	mCells = size;
	mY = 0;
	mX = 0;
	mSlot = 0xffff;
	mBuilt = FALSE;
	mNumVerts = (size + 1) * (size + 1);
	CVBuffer::Create(mNumVerts, sizeof(SLandscapeVBVertex), D3DFVF_XYZ | D3DFVF_TEX1);
}

//******************************************************************************************
void	CLandscapeVB::Rebuild()	// FUNCTION: 0x0048f210
{
	SLandscapeVBVertex	*v;
	Lock((void **)&v);

	SINT	n = mCells + 1;
	SINT	y = mY;
	for (SINT j = n; j; j--)
	{
		SINT	x = mX;
		for (SINT i = n; i; i--)
		{
			v->mX = (float)x;
			v->mY = (float)y;
			v->mZ = MAP.GetHeight(x, y);
			v->mU = v->mX;
			v->mV = v->mY;
			v++;
			x += mStep;
		}
		y += mStep;
	}

	Unlock();
	mBuilt = TRUE;
}

//******************************************************************************************
void	CLandscapeVB::SetOrigin(SINT x, SINT y, SINT step, float complexity)	// FUNCTION: 0x0048f2f0
{
	mComplexity = complexity;
	mX = x;
	mY = y;
	mStep = step;
	Rebuild();
}

//******************************************************************************************
HRESULT	CLandscapeVB::InitDeviceObjects()	// FUNCTION: 0x0048f320
{
	HRESULT	hr = CVBuffer::InitDeviceObjects();
	if (mBuilt)
		Rebuild();
	return hr;
}

#include "mathsconst.h"	// the maths constant's include, where retail's initializer falls (placement evidence)	// FUNCTION: 0x0048f340 init:ZERO_FVECTOR

// The second pair of maths constants, after the code (see mathsconst2.h).
#include "mathsconst2.h"	// FUNCTION: 0x0048f360 init:ZERO_FVECTOR2
						// FUNCTION: 0x0048f380 init:ID_FMATRIX2
