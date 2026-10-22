// DXClouds.cpp: reconstructed from DXClouds.obj (0053b810..0053ba5c); Stuart's release has no copy. The clouds: an
// atmospherics effect with a texture and a vertex buffer that this build never draws. The allocation's __LINE__ is
// the retail one; the #line places it.

#include "common.h"
#define ATMOSPHERICS_FULL
#include "mathsconst.h"
#include "atmospherics.h"
#include "DXClouds.h"
#include "Texture.h"
#include "VBufTexture.h"
#include "console.h"

// FUNCTION: 0x0053b810 init:ZERO_FVECTOR
// FUNCTION: 0x0053b830 init:ID_FMATRIX

//******************************************************************************************
CClouds::CClouds()	// FUNCTION: 0x0053b900
{
	mTexture = CDXTexture::GetTextureByName("Atmospherics\\Clouds\\Cloud.tga");
#line 31
	mVBuf = new (MEMTYPE_ATMOSPHERICS) CVBufTexture(mTexture);
	mVBuf->SetVBFormat(FVF_GTEX_FLAGS, D3DUSAGE_WRITEONLY, sizeof(CFVF_GTEX), D3DPT_TRIANGLELIST, D3DPOOL_MANAGED);
	mVBuf->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY, 2, D3DPOOL_MANAGED);
	CONSOLE.RegisterVariable("cg_cloudwidth", "Width of in-flight clouds", CVar_float, &mWidth);
}

// FUNCTION: 0x0053b9e0 ?GetName@CClouds@@UAEPADXZ

//******************************************************************************************
void CClouds::Init()	// FUNCTION: 0x0053b9f0
{
	mWidth = 0.5f;
	for (int i = 0; i < NUM_CLOUDS; i++)
	{
		mClouds[i].X = -1.f;
		mClouds[i].Y = -1.f;
		mClouds[i].Z = -1.f;
	}
}

//******************************************************************************************
void CClouds::Shutdown()	// FUNCTION: 0x0053ba20
{
	if (mTexture)
	{
		mTexture->CTexture::Release();
		mTexture = NULL;
	}
	SAFE_DELETE(mVBuf);
}
