// DXSurf.cpp: reconstructed from DXSurf.obj (00556420..00556b77); Stuart's release has no copy. The surf: strips of two
// quads along the shore, read with the level, drawn with the animated waves texture. The allocations' __LINE__s are
// the retail ones; the #lines place them.

#include "common.h"
#define DXSURF_CPP
#include "mathsconst.h"
#include "DX.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "surf.h"
#include "vbuffer.h"
#include "Texture.h"
#include "tweak.h"
#include "chunker.h"
#include <math.h>
#include "staticshadows.h"
extern bool		WATER_USE_SHADER;				// 009cc1a0
extern CTweakSLONG	GEFORCE_FX_POWER;			// 0082b498 (PauseMenu.cpp): the GeForce FX path takes the coordinate sets the other way round

CStaticShadows	STATICSHADOWS;		// 009c8010; its empty initializer is a lone ret	// FUNCTION: 0x00556420 initorder:1

CRenderMethod	SURF_METHOD("surf_method", 2);	// 009cc030
// FUNCTION: 0x00556430 init:SURF_METHOD
// FUNCTION: 0x00556450 exit:SURF_METHOD

// a surf vertex: position, colour and two sets of texture coordinates (D3DFVF_XYZ | DIFFUSE | TEX2)
struct SSurfVertex
{
	float	mX, mY, mZ;
	DWORD	mColour;
	float	mU0, mV0;
	float	mU1, mV1;
};

#define SURF_FVF	(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2)

//******************************************************************************************
CDXSurf::CDXSurf()	// FUNCTION: 0x00556460
{
	mLoaded = FALSE;
	mStrips = NULL;
	mWaves = NULL;
	mNumStrips = 0;
}

//******************************************************************************************
CDXSurf::~CDXSurf()
{
}

//******************************************************************************************
void	CDXSurf::LoadWaves()	// FUNCTION: 0x00556470
{
	mWaves = CDXTexture::FindTexture("mixers\\waves.tga", 5, 0, -1, 1, 1);
}

//******************************************************************************************
void	CDXSurf::Deserialize(CChunkReader *c)	// FUNCTION: 0x00556490
{
	if (c->GetSize() < 8)
		return;

	c->GetNext();
	c->Read(&mNumStrips, 4, 1);
	if (mNumStrips)
	{
#line 56
		mStrips = new (MEMTYPE_LANDSCAPE) SSurfStrip[mNumStrips];
	}
	for (SINT c0 = 0; c0 < mNumStrips; c0++)
	{
		c->GetNext();
		c->Read(&mStrips[c0].mCount, 4, 1);
		mStrips[c0].Create(c);
	}
	mLoaded = TRUE;
}

// FUNCTION: 0x005565a0 ??0SSurfStrip@@QAE@XZ

//******************************************************************************************
SSurfStrip::~SSurfStrip()	// FUNCTION: 0x005565b0
{
	if (mVB0)
		delete mVB0;
	if (mVB1)
		delete mVB1;
}

//******************************************************************************************
// The first two vertices of each step go to the second buffer (the wave's face), the next two to the first.
void	SSurfStrip::Create(CChunkReader *c)	// FUNCTION: 0x005565d0
{
#line 168
	mVB0 = new (MEMTYPE_VBUFFER) CVBuffer;
	mVB0->Create(mCount * 2 + 2, sizeof(SSurfVertex), SURF_FVF);
#line 171
	mVB1 = new (MEMTYPE_VBUFFER) CVBuffer;
	mVB1->Create(mCount * 2 + 2, sizeof(SSurfVertex), SURF_FVF);

	SSurfVertex	*v1, *v0;
	mVB0->Lock((void **)&v1);
	mVB1->Lock((void **)&v0);

	float	t = 0;
	for (SINT c0 = mCount; c0 >= 0; c0--)
	{
		float	s = (float)sin(t * 0.5f) * 0.5f;

		c->Read(&v0[0].mX, 4, 1);
		c->Read(&v0[0].mY, 4, 1);
		c->Read(&v0[0].mZ, 4, 1);
		if (GEFORCE_FX_POWER.mValue)
		{
			v0[0].mU1 = v0[0].mX;
			v0[0].mV1 = v0[0].mY;
			v0[0].mU0 = t;
			v0[0].mV0 = s - 0.25f - t * 0.25f;
		}
		else
		{
			v0[0].mU0 = v0[0].mX;
			v0[0].mV0 = v0[0].mY;
			v0[0].mU1 = t;
			v0[0].mV1 = s - 0.25f - t * 0.25f;
		}
		v0[0].mColour = 0x00ffffff;

		c->Read(&v0[1].mX, 4, 1);
		c->Read(&v0[1].mY, 4, 1);
		c->Read(&v0[1].mZ, 4, 1);
		if (GEFORCE_FX_POWER.mValue)
		{
			v0[1].mU1 = v0[1].mX;
			v0[1].mV1 = v0[1].mY;
			v0[1].mU0 = t;
			v0[1].mV0 = s - t * 0.25f;
		}
		else
		{
			v0[1].mU0 = v0[1].mX;
			v0[1].mV0 = v0[1].mY;
			v0[1].mU1 = t;
			v0[1].mV1 = s - t * 0.25f;
		}
		v0[1].mColour = 0xc0ffffff;

		v1[0] = v0[1];
		if (GEFORCE_FX_POWER.mValue)
		{
			v1[0].mU1 = v1[0].mX;
			v1[0].mV1 = v1[0].mY;
			v1[0].mU0 = t;
			v1[0].mV0 = s - t * 0.25f;
		}
		else
		{
			v1[0].mU0 = v1[0].mX;
			v1[0].mV0 = v1[0].mY;
			v1[0].mU1 = t;
			v1[0].mV1 = s - t * 0.25f;
		}
		v1[0].mColour = 0xc0ffffff;

		c->Read(&v1[1].mX, 4, 1);
		c->Read(&v1[1].mY, 4, 1);
		c->Read(&v1[1].mZ, 4, 1);
		if (GEFORCE_FX_POWER.mValue)
		{
			v1[1].mU1 = v1[1].mX;
			v1[1].mV1 = v1[1].mY;
			v1[1].mU0 = t + 0.0625f;
			v1[1].mV0 = s + 2.f - t * 0.25f;
		}
		else
		{
			v1[1].mU0 = v1[1].mX;
			v1[1].mV0 = v1[1].mY;
			v1[1].mU1 = t + 0.0625f;
			v1[1].mV1 = s + 2.f - t * 0.25f;
		}
		v1[1].mColour = 0xff000000;

		t += 0.125f;
		v1 += 2;
		v0 += 2;
	}

	mVB0->Unlock();
	mVB1->Unlock();
}

//******************************************************************************************
void	CDXSurf::Destroy()	// FUNCTION: 0x005569e0
{
	mLoaded = FALSE;
	if (mStrips)
	{
		delete [] mStrips;
		mStrips = NULL;
	}
	if (mWaves)
	{
		mWaves->Release();
		mWaves = NULL;
	}
}

//******************************************************************************************
// The two `goto done` leave the strip loops for the depth-bias reset, skipping Accept, a skip as Stuart's DXEngine.cpp:687
// (`goto dont_do_lods_now`). A return that resets the bias first is 24 bytes off, a failed flag 208.
void	CDXSurf::Render(bool validate)	// FUNCTION: 0x00556a30
{
	if (!mNumStrips)
		return;

	if (!validate || SURF_METHOD.Try(1))
	{
		RENDERINFO.SetDepthBias(4);
		if (validate)
			LT.D3D_SetTexture(3, mWaves->GetTexture());
		else
			LT.D3D_SetTexture(0, mWaves->GetTexture());

		SSurfStrip	*s = mStrips;
		for (SINT c0 = mNumStrips; c0; s++, c0--)
		{
			if (WATER_USE_SHADER)
			{
				s->mVB0->SetStreamSourceOnly(0);
				LT.SetFVF(SURF_FVF);
			}
			else
			{
				s->mVB0->SetStreamSource(0);
				RENDERINFO.Apply(false, true);
			}
			if (validate && !SURF_METHOD.Validate())
				goto done;
			LT.GetDevice()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, s->mCount * 2);

			if (WATER_USE_SHADER)
				s->mVB1->SetStreamSourceOnly(0);
			else
			{
				s->mVB1->SetStreamSource(0);
				RENDERINFO.Apply(false, true);
			}
			if (validate && !SURF_METHOD.Validate())
				goto done;
			LT.GetDevice()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, s->mCount * 2);
		}
	}
	if (validate)
		SURF_METHOD.Accept();
done:
	RENDERINFO.SetDepthBias(0);
}

// the object's maths constant, after its code (initialised between the empty one and SURF_METHOD)
// (00556b80, the zero vector initializer after this code, opens DXTexture.obj: TEXTURE_ZERO)
