// screenfx.cpp: reconstructed from screenfx.obj (00551ba0..00551d68); Stuart's release has no copy. On the PC the
// zoom overlay's two textures are loaded here and drawn by the HUD; Render is empty.
#include "common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include "console.h"
#include "ltshell.h"
#include "VBufTexture.h"
#include "screenfx.h"			// FUNCTION: 0x00551ba0 init:ZERO_FVECTOR
#include "mathsconst.h"			// FUNCTION: 0x00551bc0 init:ID_FMATRIX
CScreenFX	SCREENFX;						// 009c74f8, just before this object's ID_FMATRIX (009c7510)

//******************************************************************************************
void CScreenFX::Init()	// FUNCTION: 0x00551c90
{
	mZoomLines = NULL;
	mReticle = NULL;
	mZoomEffectEnabled = true;
	CONSOLE.RegisterVariable("cg_zoomeffectenabled", "Is the visual effect for zooming enabled?", CVar_bool, &mZoomEffectEnabled);
}

//******************************************************************************************
void CScreenFX::InitResources()	// FUNCTION: 0x00551cc0
{
	mZoomLines = CVBufTexture::GetByName("screenfx\\zoomlines.tga", 1);
	mZoomLines->SetVBFormat(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 0x1c, D3DPT_TRIANGLELIST, D3DPOOL_DEFAULT);
	mZoomLines->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 2, D3DPOOL_DEFAULT);
	mReticle = CVBufTexture::GetByName("screenfx\\reticle.tga", 1);
	mReticle->SetVBFormat(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 0x1c, D3DPT_TRIANGLELIST, D3DPOOL_DEFAULT);
	mReticle->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 2, D3DPOOL_DEFAULT);
}

//******************************************************************************************
void CScreenFX::Shutdown()	// FUNCTION: 0x00551d40
{
	if (mZoomLines)
	{
		mZoomLines->Release();
		mZoomLines = NULL;
	}
	if (mReticle)
	{
		mReticle->Release();
		mReticle = NULL;
	}
}

// CDXEngine::Render calls the empty retail body at 0053ec8b; original object attribution is folded.
void CScreenFX::Render()	// FUNCTION: 0x0040c640
{
}
