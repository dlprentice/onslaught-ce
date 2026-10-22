// State.cpp: the PC render-state defaults (CState, STATE). Not in Stuart's release; reconstructed from the retail
// code (004eb1d0..004ebab0). UseDefault forces every render, texture-stage and sampler state the game relies on;
// 0xf0ccface marks a state the game always sets before use.

#include "common.h"
#include "DX.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "VertexShader.h"
#include "engine.h"
#include "map.h"
#include "state.h"

// FUNCTION: 0x004eb1d0 init:STATE
CState	STATE;

static inline DWORD	FtoDW(FLOAT f) { return *((DWORD *)&f); }

//******************************************************************************************
void	CState::UseDefault()	// FUNCTION: 0x004eb1e0
{
	LT.InvalidateAllStates();
	if (CVertexShader::mSoftware)
		LT.SetSoftwareVertexProcessing(false);

	LT.ForceRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.ForceRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.ForceRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.ForceRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.ForceAlphaRef(8);
	LT.ForceRS(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	LT.ForceRS(D3DRS_FILLMODE, D3DFILL_SOLID);
	LT.ForceRS(D3DRS_CULLMODE, D3DCULL_CCW);
	LT.ForceRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.ForceRS(D3DRS_ZENABLE, D3DZB_TRUE);
	LT.ForceRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.ForceRS(D3DRS_COLORWRITEENABLE, 0xf);
	LT.ForceRS(D3DRS_STENCILENABLE, FALSE);
	LT.ForceRS(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
	if (gMirrorPass)
		LT.ForceRS(D3DRS_FOGENABLE, FALSE);
	else
		LT.ForceRS(D3DRS_FOGENABLE, TRUE);
	LT.ForceRS(D3DRS_SPECULARENABLE, FALSE);
	LT.SetFogMode(true);
	LT.ForceRS(D3DRS_LIGHTING, TRUE);
	LT.ForceRS(D3DRS_DITHERENABLE, TRUE);
	LT.ForceRS(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	LT.ForceRS(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	LT.ForceRS(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
	LT.ForceRS(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
	LT.ForceRS(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	LT.ForceRS(D3DRS_NORMALIZENORMALS, FALSE);
	LT.ForceRS(D3DRS_AMBIENT, 0xf0ccface);
	LT.ForceRS(D3DRS_FOGCOLOR, 0xf0ccface);
	LT.ForceRS(D3DRS_FOGDENSITY, 0xf0ccface);
	LT.ForceRS(D3DRS_FOGSTART, 0xf0ccface);
	LT.ForceRS(D3DRS_FOGEND, 0xf0ccface);
	LT.ForceRS(D3DRS_TEXTUREFACTOR, 0xf0ccface);
	LT.ForceRS(D3DRS_POINTSPRITEENABLE, FALSE);
	LT.ForceRS(D3DRS_POINTSCALEENABLE, FALSE);

	float	maxsize = LT.m_d3dCaps.MaxPointSize;
	if (maxsize > 1.f)
	{
		float	size = (maxsize < 8.f) ? maxsize : 8.f;
		float	maxs = (maxsize < 32.f) ? maxsize : 32.f;
		LT.ForceRS(D3DRS_POINTSIZE, FtoDW(size));
		LT.ForceRS(D3DRS_POINTSIZE_MIN, FtoDW(size));
		LT.ForceRS(D3DRS_POINTSCALE_A, FtoDW(0.f));
		LT.ForceRS(D3DRS_POINTSCALE_B, FtoDW(0.f));
		LT.ForceRS(D3DRS_POINTSCALE_C, FtoDW(1.f));
		LT.ForceRS(D3DRS_POINTSIZE_MAX, FtoDW(maxs));
	}

	LT.ForceRS(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	LT.ForceRS(D3DRS_STENCILENABLE, FALSE);
	LT.ForceRS(D3DRS_STENCILFUNC, D3DCMP_NEVER);
	LT.ForceRS(D3DRS_STENCILREF, 0);
	RENDERINFO.SetDepthBias(0);
	LT.ForceRS(D3DRS_CLIPPING, TRUE);
	UpdateClipPlane(true);
	LT.ForceRS(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);

	// stage 0 modulates the texture by the diffuse colour
	LT.SSS(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	SetMipFilterLinear(0);
	LT.SSS(0, D3DSAMP_MAXANISOTROPY, 1);
	LT.SSS(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.ForceTS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.ForceTS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.ForceTS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.ForceTS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.ForceTS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.ForceTS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.ForceTS(0, D3DTSS_TEXCOORDINDEX, 0);
	LT.ForceTS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.SSS(0, D3DSAMP_MIPMAPLODBIAS, 0);
	LT.SSS(0, D3DSAMP_BORDERCOLOR, 0xffffffff);

	// stages 1 to 3 are off
	LT.SSS(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	SetMipFilterByGlobalToggle(1);
	LT.SSS(1, D3DSAMP_MAXANISOTROPY, 1);
	LT.SSS(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.ForceTS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.ForceTS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	LT.ForceTS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.ForceTS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
	LT.ForceTS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.ForceTS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.ForceTS(1, D3DTSS_TEXCOORDINDEX, 1);
	LT.ForceTS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.SSS(1, D3DSAMP_MIPMAPLODBIAS, 0);
	LT.ForceTS(1, D3DTSS_COLORARG0, D3DTA_TFACTOR);
	LT.SSS(1, D3DSAMP_BORDERCOLOR, 0xffffffff);

	LT.SSS(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	SetMipFilterByGlobalToggle(2);
	LT.SSS(2, D3DSAMP_MAXANISOTROPY, 1);
	LT.SSS(2, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS(2, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.ForceTS(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.ForceTS(2, D3DTSS_COLORARG2, D3DTA_CURRENT);
	LT.ForceTS(2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.ForceTS(2, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
	LT.ForceTS(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.ForceTS(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.ForceTS(2, D3DTSS_TEXCOORDINDEX, 2);
	LT.ForceTS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.SSS(2, D3DSAMP_MIPMAPLODBIAS, 0);
	LT.ForceTS(2, D3DTSS_COLORARG0, D3DTA_TFACTOR);
	LT.SSS(2, D3DSAMP_BORDERCOLOR, 0xffffffff);

	LT.SSS(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	SetMipFilterByGlobalToggle(3);
	LT.SSS(3, D3DSAMP_MAXANISOTROPY, 1);
	LT.SSS(3, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS(3, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.ForceTS(3, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.ForceTS(3, D3DTSS_COLORARG2, D3DTA_CURRENT);
	LT.ForceTS(3, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.ForceTS(3, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
	LT.ForceTS(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.ForceTS(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.ForceTS(3, D3DTSS_TEXCOORDINDEX, 3);
	LT.ForceTS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.SSS(3, D3DSAMP_MIPMAPLODBIAS, 0);
	LT.ForceTS(3, D3DTSS_COLORARG0, D3DTA_TFACTOR);
	LT.SSS(3, D3DSAMP_BORDERCOLOR, 0xffffffff);
}

//******************************************************************************************
CState::CState()	// FUNCTION: 0x004eb9a0
{
	mDefaultMaterial.Diffuse.r = 1.f;
	mDefaultMaterial.Diffuse.g = 1.f;
	mDefaultMaterial.Diffuse.b = 1.f;
	mDefaultMaterial.Diffuse.a = 1.f;
	mDefaultMaterial.Ambient.r = 0.f;
	mDefaultMaterial.Ambient.g = 0.f;
	mDefaultMaterial.Ambient.b = 0.f;
	mDefaultMaterial.Ambient.a = 1.f;
	mDefaultMaterial.Specular.r = 0.f;
	mDefaultMaterial.Specular.g = 0.f;
	mDefaultMaterial.Specular.b = 0.f;
	mDefaultMaterial.Specular.a = 0.f;
	mDefaultMaterial.Emissive.r = 0.f;
	mDefaultMaterial.Emissive.g = 0.f;
	mDefaultMaterial.Emissive.b = 0.f;
	mDefaultMaterial.Emissive.a = 0.f;
	mDefaultMaterial.Power = 0.1f;

	mLandscapeMaterial.Diffuse.r = 0.f;
	mLandscapeMaterial.Diffuse.g = 0.f;
	mLandscapeMaterial.Diffuse.b = 0.f;
	mLandscapeMaterial.Diffuse.a = 1.f;
	mLandscapeMaterial.Ambient.r = 0.8f;
	mLandscapeMaterial.Ambient.g = 0.8f;
	mLandscapeMaterial.Ambient.b = 0.8f;
	mLandscapeMaterial.Ambient.a = 1.f;
	mLandscapeMaterial.Specular.r = 0.f;
	mLandscapeMaterial.Specular.g = 0.f;
	mLandscapeMaterial.Specular.b = 0.f;
	mLandscapeMaterial.Specular.a = 0.f;
	mLandscapeMaterial.Emissive.r = 0.f;
	mLandscapeMaterial.Emissive.g = 0.f;
	mLandscapeMaterial.Emissive.b = 0.f;
	mLandscapeMaterial.Emissive.a = 0.f;
	mLandscapeMaterial.Power = 0.f;
}

//******************************************************************************************
// UseDefault calls it out of line (004eb56a): at 175 units (the braced else) VC6 no longer offers it for inlining.
void	CState::UpdateClipPlane(bool force)	// FUNCTION: 0x004eba30
{
	if (gMirrorPass && LT.mCurrentFVF != 0xfedcba98)
	{
		float	plane[4];
		plane[0] = 0.f;
		plane[1] = 0.f;
		plane[2] = -1.f;
		plane[3] = MAP.GetWaterLevel();
		LT.m_pd3dDevice->SetClipPlane(0, plane);
		if (force)
		{
			LT.ForceRS(D3DRS_CLIPPLANEENABLE, 1);
		}
		else
		{
			LT.SRS(D3DRS_CLIPPLANEENABLE, 1);
		}
	}
	else
	{
		if (force)	LT.ForceRS(D3DRS_CLIPPLANEENABLE, 0);
		else		LT.SRS(D3DRS_CLIPPLANEENABLE, 0);
	}
}
