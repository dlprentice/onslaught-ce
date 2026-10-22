// DXParticleTexture.cpp: reconstructed from DXParticleTexture.obj (0054fa70..005502c4); Stuart's release has no copy.
// The particle textures of the PC build: each keeps a dynamic vertex buffer the particles fill each frame; with the
// extra GeForce FX features on, they draw through a shader that fades them against the landscape's depth.
#include "common.h"
#include "ParticleTexture.h"	// FUNCTION: 0x0054fa70 init:ZERO_FVECTOR
#include "mathsconst.h"			// FUNCTION: 0x0054fa90 init:ID_FMATRIX
#include "Texture.h"
#include "VBufTexture.h"
#include "VertexShader.h"
#include "ltshell.h"
#include "engine.h"
#include "game.h"
#include "Platform.h"
#include "DebugLog.h"

// FUNCTION: 0x0054fb60 init:EXTRA_GEFORCE_FX_FEATURES
// FUNCTION: 0x0054fb80 exit:EXTRA_GEFORCE_FX_FEATURES
CTweakSLONG	EXTRA_GEFORCE_FX_FEATURES("EXTRA_GEFORCE_FX_FEATURES", 1);
// FUNCTION: 0x0054fb90 init:GEFORCE_PARTICLE_FOG
// FUNCTION: 0x0054fbb0 exit:GEFORCE_PARTICLE_FOG
CTweakSLONG	GEFORCE_PARTICLE_FOG("GEFORCE_PARTICLE_FOG", 1);

static CVertexShader			*sVertexShader;		// 009c646c; defined first, and sFirst/sShaderActive initialized: retail's .bss order (placement evidence)
static IDirect3DPixelShader9	*sPixelShader;		// 009c6468
static CDXParticleTexture		*sFirst = NULL;		// 009c64d0
static bool						sShaderActive = false;	// 009c64d4

static float		sPixelShaderConstant[4] = { 0.48828125f, 31.25f, 1000.f, 0.f };	// 00651d88 (.data: retail keeps it writable)

#define PARTICLE_VERTEX_SHADER_TEXT	\
	"vs.1.1\n" \
	"\n" \
	"dcl_position v0\n" \
	"dcl_texcoord v7\n" \
	"dcl_color v5\n" \
	"\n" \
	"; world/view/projection\n" \
	"\n" \
	"dp4 r2.x, v0, c[4]\n" \
	"dp4 r2.y, v0, c[5]\n" \
	"dp4 r2.z, v0, c[6]\n" \
	"dp4 r2.w, v0, c[7]\n" \
	"\n" \
	"dp4 r3.x, r2, c[8]\n" \
	"dp4 r3.y, r2, c[9]\n" \
	"dp4 r3.z, r2, c[10]\n" \
	"dp4 r3.w, r2, c[11]\n" \
	"\n" \
	"; colour\n" \
	"mov oD0, v5\n" \
	"\n" \
	"mov oPos, r3\n" \
	"\n" \
	"; texture\n" \
	"\n" \
	"; somehow we have to get screen coords into texture coords\n" \
	"\n" \
	"; so...\n" \
	"; perspective divide\n" \
	"rcp r1.w, r3.z\n" \
	"mul r1.w, r1.w, c[95].w\n" \
	"\n" \
	"; and save\n" \
	"mad oT1.x, r3.x, r1.w, c[95].w\n" \
	"mad oT1.y, -r3.y, r1.w, c[95].w\n" \
	"\n" \
	"mul oT2.xyzw, r3.z, c[94].z\n" \
	"\n" \
	"; normal texture\n" \
	"mov oT0, v7\n" \
	"mov oFog, r2.z\n"

#define PARTICLE_PIXEL_SHADER_TEXT	\
	"ps_2_0\n" \
	"dcl_2d s0\n" \
	"dcl_2d s1\n" \
	"dcl v0\n" \
	"dcl t0\n" \
	"dcl t1\n" \
	"dcl t2\n" \
	"texld r0, t0, s0\n" \
	"texld r2, t1, s1\n" \
	"mul r2.a, r2.r, c0.z\n" \
	"mul_pp r0, v0, r0\n" \
	"sub_sat r1.a, r2.a, t2.a\n" \
	"mul_pp_sat r0.rgba, r0.rgba, r1.aaaa\n" \
	"mov oC0, r0"

// The texture format a blend mode loads with.
static inline ETextureFormat TextureFormat(SINT mode)
{
	return ((mode >= 2) && (mode <= 3)) ? TEXFMT_A4R4G4B4 : TEXFMT_R5G6B5;
}

//******************************************************************************************
CDXParticleTexture *CDXParticleTexture::Get(char *name, SINT mode)	// FUNCTION: 0x0054fbc0
{
	char	path[200];

	CDXParticleTexture	*t;
	for (t = sFirst; t; t = t->mNext)
		if (!stricmp(name, t->mName) && (mode == t->mMode))
			break;
	if (t)
		return t;

#line 81
	t = new(MEMTYPE_JCLTEXTURE) CDXParticleTexture;
	if (t)
	{
		strcpy(path, name);
		t->mMode = mode;
		t->mTexture = CDXTexture::FindTexture(path + 32, TextureFormat(mode), 0, 1, 1, 1);
		t->mVBuf = CVBufTexture::GetOrCreate(t->mTexture, 0);
		BOOL	managed = !CLAIM_WRITEONLY.mValue;
		t->mVBuf->SetVBFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DUSAGE_WRITEONLY, 0x18, D3DPT_TRIANGLELIST, managed);
		t->mVBuf->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY, 2, managed);
		strcpy(t->mName, name);

		SINT	i;
		for (i = strlen(name) - 1; i >= 0; i--)
			if (name[i] == '\\')
				break;
		strcpy(t->mShortName, name + i + 1);

		t->mNext = sFirst;
		sFirst = t;
	}
	return t;
}

//******************************************************************************************
void CDXParticleTexture::FreeAllSurfaces()	// FUNCTION: 0x0054fd80
{
	for (CDXParticleTexture *t = sFirst; t; t = t->mNext)
	{
		if (t->mTexture)
		{
			t->mTexture->Release();
			t->mTexture = NULL;
		}
		if (t->mVBuf)
		{
			t->mVBuf->Release();
			t->mVBuf = NULL;
		}
	}
	LT.D3D_Release((IUnknown **)&sPixelShader);
}

//******************************************************************************************
void CDXParticleTexture::RecreateAllSurfaces()	// FUNCTION: 0x0054fde0
{
	SETUP_HISTORY.AddMessage("CPT::RAS()");
	for (CDXParticleTexture *t = sFirst; t; t = t->mNext)
	{
		t->mTexture = CDXTexture::FindTexture(t->mName + 32, TextureFormat(t->mMode), 0, 1, 1, 1);
		t->mVBuf = CVBufTexture::GetOrCreate(t->mTexture, 0);
		t->mVBuf->SetVBFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0x18, D3DPT_TRIANGLELIST, 0);
		t->mVBuf->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 2, 0);
	}

	if (EXTRA_GEFORCE_FX_FEATURES.mValue && (LT.m_d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2, 0)))
	{
		sVertexShader = CVertexShader::Create("particleshader", (DWORD *)PARTICLE_VERTEX_SHADER_TEXT, 6, NULL, 0, -1);
		LT.D3D_CreatePixelShader(PARTICLE_PIXEL_SHADER_TEXT, &sPixelShader);
	}
	SETUP_HISTORY.AddMessage("CPT::RAS() end");
}

//******************************************************************************************
// STAND-IN: the delete reads its operand through a reference. Retail tests sFirst, then copies it (mov esi,eax) and tests
// the copy for the delete (0054fef1, 0054fef7), where a direct read folds the second test away: `delete sFirst`, the
// same under `if (sFirst)` and a plain local copy are each 7/21/43 off.
void CDXParticleTexture::DestroyAll()	// FUNCTION: 0x0054fee0
{
	while (sFirst)
	{
		CDXParticleTexture	*next = sFirst->mNext;
		if (sFirst)
		{
			CDXParticleTexture	*const &texture = sFirst;
			delete texture;
		}
		sFirst = next;
	}
}

//******************************************************************************************
void CDXParticleTexture::RenderAll()	// FUNCTION: 0x0054ff20
{
	CDXParticleTexture	*t = sFirst;

	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	sShaderActive = false;

	if (!GAME.IsMultiplayer() && !PLATFORM.KeyOn(DIK_F1) && EXTRA_GEFORCE_FX_FEATURES.mValue && ENGINE.GetLandscape() &&
		ENGINE.GetLandscape()->mUnknown08 && sPixelShader && sVertexShader)
	{
		if (SUCCEEDED(LT.GetDevice()->SetPixelShader(sPixelShader)))
		{
			LT.SetVertexShader(sVertexShader);
			sVertexShader->SetConstants();
			LT.D3D_SetTexture(1, ENGINE.GetLandscape()->mUnknown08);
			LT.GetDevice()->SetPixelShaderConstantF(0, sPixelShaderConstant, 1);
			LT.SSS_NoCache(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			LT.SSS_NoCache(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			LT.SSS_NoCache(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			LT.SSS_NoCache(1, D3DSAMP_MAXANISOTROPY, 1);
			if (!GEFORCE_PARTICLE_FOG.mValue)
				LT.SRS(D3DRS_FOGENABLE, FALSE);
			sShaderActive = true;
		}
		else
		{
			LT.D3D_Release((IUnknown **)&sPixelShader);
			EXTRA_GEFORCE_FX_FEATURES.Set(0);
		}
	}

	for (; t; t = t->mNext)
		t->Render();

	if (sShaderActive)
	{
		LT.SSS_NoCache(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		LT.SSS_NoCache(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		if (!GEFORCE_PARTICLE_FOG.mValue)
			LT.SRS(D3DRS_FOGENABLE, TRUE);
		LT.D3D_SetTexture(1, NULL);
		LT.GetDevice()->SetPixelShader(NULL);
		sShaderActive = false;
	}
	LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
}

//******************************************************************************************
CDXParticleTexture::~CDXParticleTexture()	// FUNCTION: 0x00550110
{
	if (mTexture)
	{
		mTexture->Release();
		mTexture = NULL;
	}
	if (mVBuf)
	{
		mVBuf->Release();
		mVBuf = NULL;
	}
}

//******************************************************************************************
void CDXParticleTexture::AddTriangle(UWORD a, UWORD b, UWORD c)	// FUNCTION: 0x00550180
{
	UWORD	*i = mVBuf->GetIndexPtr(3);
	i[0] = a;
	i[1] = b;
	i[2] = c;
}

//******************************************************************************************
UWORD *CDXParticleTexture::GetIndices(SINT count)	// FUNCTION: 0x005501b0
{
	return mVBuf->GetIndexPtr(count);
}

//******************************************************************************************
SINT CDXParticleTexture::GetNextVertex()	// FUNCTION: 0x005501d0
{
	return mVertex + 1;
}

//******************************************************************************************
void CDXParticleTexture::AddVertex(void *vertex)	// FUNCTION: 0x005501e0
{
	mVertex = mVBuf->AddVertices(vertex, 1);
}

//******************************************************************************************
void CDXParticleTexture::GetVertices(SINT count, void **verts, SINT *first)	// FUNCTION: 0x00550200
{
	*first = mVBuf->GetVertexPtr(verts, count);
}

//******************************************************************************************
void CDXParticleTexture::Render()	// FUNCTION: 0x00550220
{
	if (!mVBuf)
		return;

	LT.D3D_SetTexture(0, mTexture->GetTexture());
	switch (mMode)
	{
	case 1:
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
		break;
	case 2:
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		break;
	case 3:
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		break;
	}

	mVertex = -1;
	if (sShaderActive)
		mVBuf->DrawIndexed(true, 0, 0);
	else
		mVBuf->DWATS(true, 0, 0);
}
