// DXLandscape.cpp: reconstructed from DXLandscape.obj (00544360..00547d3f); Stuart's release has no copy.
// The PC landscape: a 512-metre height field drawn as 64x64 tiles of 8 metres through one vertex buffer of the
// whole grid, each viewpoint with its own mip levels of the landscape texture and levels of detail. With the
// GeForce FX features it also keeps a reflection target and a readable Z buffer.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include	"Common.h"
#define DAMAGE_FULL
#define DXLANDSCAPE_CPP
#include	"landscape.h"
#include	"DXLandscape.h"
#include	"LandscapeTexture.h"
#include	"engine.h"
#include	"DXEngine.h"
#include	"map.h"
#include	"game.h"
#include	"console.h"
#include	"tweak.h"
#include	"ltshell.h"
#include	"vbuffer.h"
#include	"ibuffer.h"
#include	"VertexShader.h"
#include	"Texture.h"
#include	"DebugLog.h"
#include	"ParticleTexture.h"
#include	"surf.h"
#include	"renderinfo.h"
#include	"LandscapeVB.h"
#include	"landscapeib.h"
#include	"Camera.h"
#include	"water.h"
#include	"maptex.h"
#include	"DXPalletizer.h"
#include	"gcgamut.h"
#include	<stdio.h>
#include	"mathsconst.h"	// the maths pair, defined below and exported (DXKempyCube.obj uses it)
#include	"shadows.h"
#include	"state.h"
const FVector	ZERO_FVECTOR(0.0f, 0.0f, 0.0f);	// 008aa910	// FUNCTION: 0x00544360 init:ZERO_FVECTOR
const FMatrix	ID_FMATRIX(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));	// 008aa8d8	// FUNCTION: 0x00544380 init:ID_FMATRIX
static FVector	sLandscapePos(0.0f, 0.0f, 0.0f);			// 008c0280: the landscape's world transform
static FMatrix	sLandscapeOri(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));	// 008aa9c0
// FUNCTION: 0x00544450 init:sLandscapePos
// FUNCTION: 0x00544470 init:sLandscapeOri

CTweakSLONG	OTHER_ZBUFFER_AS_TEXTURE("OTHER_ZBUFFER_AS_TEXTURE", 0);	// 008aa9b0
CTweakSLONG	MY_READABLE_Z_BUFFER("MY_READABLE_Z_BUFFER", 0);			// 008aa9a0
CTweakSLONG	LANDSCAPE_LOWRES_GEOM("LANDSCAPE_LOWRES_GEOM", 0);			// 008aa990
CTweakSLONG	LANDSCAPE_MINLOD_AT("LANDSCAPE_MINLOD_AT", 0x100);			// 008aa980
CTweakSLONG	R32F_WORKS("R32F_WORKS", 0);								// 008aa970
CTweakSLONG	LANDSCAPE_MAXLEVELS("LANDSCAPE_MAXLEVELS", 5);				// 008aa960
CTweakSLONG	LANDSCAPE_MAXLEVELS_USER("LANDSCAPE_MAXLEVELS_USER", 5);	// 008aa950
CTweakSLONG	LANDSCAPE_LIGHTING("LANDSCAPE_LIGHTING", 1);				// 008aa940
// FUNCTION: 0x00544540 init:OTHER_ZBUFFER_AS_TEXTURE
// FUNCTION: 0x00544560 exit:OTHER_ZBUFFER_AS_TEXTURE
// FUNCTION: 0x00544570 init:MY_READABLE_Z_BUFFER
// FUNCTION: 0x00544590 exit:MY_READABLE_Z_BUFFER
// FUNCTION: 0x005445a0 init:LANDSCAPE_LOWRES_GEOM
// FUNCTION: 0x005445c0 exit:LANDSCAPE_LOWRES_GEOM
// FUNCTION: 0x005445d0 init:LANDSCAPE_MINLOD_AT
// FUNCTION: 0x005445f0 exit:LANDSCAPE_MINLOD_AT
// FUNCTION: 0x00544600 init:R32F_WORKS
// FUNCTION: 0x00544620 exit:R32F_WORKS
// FUNCTION: 0x00544630 init:LANDSCAPE_MAXLEVELS
// FUNCTION: 0x00544650 exit:LANDSCAPE_MAXLEVELS
// FUNCTION: 0x00544660 init:LANDSCAPE_MAXLEVELS_USER
// FUNCTION: 0x00544680 exit:LANDSCAPE_MAXLEVELS_USER
// FUNCTION: 0x00544690 init:LANDSCAPE_LIGHTING
// FUNCTION: 0x005446b0 exit:LANDSCAPE_LIGHTING

CDamage	CDXLandscape::mDamage;
// FUNCTION: 0x005446c0 init:mDamage
// FUNCTION: 0x005446e0 exit:mDamage

extern CTweakSLONG	GEFORCE_FX_POWER;						// 0082b498 (PauseMenu.cpp)

static BOOL		sCoastCalc;								// 008aa938 (xx_coastcalc)
static SINT		sLastNavDisplay;						// 008c0290
static float	sCloudU;								// 008c0294: the cloud shadows' drift
static float	sCloudV;								// 008c0298
extern D3DMATRIX		gTextureMatrix;						// 00628258 (see DXShadows.cpp)
extern CTweakSLONG	USE_MODULATE_2X;					// 008554f0 (ltshell.cpp)

//******************************************************************************************
// the console's BuildLandscapeCache; not static, so it is emitted here (retail 005446f9) rather than after its user
void con_buildlandscapecache(char *cmd)
{
	ENGINE.GetLandscape()->BuildTextureCache();
}

//******************************************************************************************
CLandscapeView::CLandscapeView()
{
	mTextures = NULL;
	mLODs = NULL;
	mTiles = NULL;
	FVector	distant(1234567.0f, 0.0f, 0.0f);
	mUnknown24 = distant;
	mLastPos = distant;
}

//******************************************************************************************
CLandscapeView::~CLandscapeView()	// FUNCTION: 0x00544770
{
	if (mTextures)
	{
		delete [] mTextures;
		mTextures = NULL;
	}
	if (mLODs)
	{
		delete [] mLODs;
		mLODs = NULL;
	}
	if (mTiles)
	{
		delete mTiles;
		mTiles = NULL;
	}
}

// FUNCTION: 0x005447d0 ??1SLandscapeLOD@@QAE@XZ

//******************************************************************************************
// The texture loop counts the detail down from 3 in a variable of its own, not as `3 - i`: with `3 - i` the compiler derives
// the counter from i and keeps a zero in ebp across the first allocation's checks, where retail uses immediates there and a
// scratch zero in the loop's entry (the counter takes ebp).
void CLandscapeView::CreateMipLevels(SINT levels)	// FUNCTION: 0x005447e0
{
	mNumLevels = levels;
	SINT	count = levels - 1;
	SINT	i;
#line 95
	mTextures = new(MEMTYPE_TEXTURE) CLandscapeTexture[count];
	SINT	detail = 3;
	for (i = 0; i < count; i++)
	{
		mTextures[i].Init(i + 1, detail);
		detail--;
	}

#line 109
	mLODs = new(MEMTYPE_LANDSCAPE) SLandscapeLOD[mNumLevels];
	SINT	size = 64;
	for (i = 0; i < mNumLevels; i++)
	{
		mLODs[i].Init(size * size);
		size >>= 1;
	}

#line 115
	mTiles = (SLandscapeCell *)new(MEMTYPE_LANDSCAPE) UBYTE[64 * 64 * sizeof(SLandscapeCell)];

	for (i = 0; i < count; i++)
	{
		CLandscapeTexture	*t = &mTextures[i];
		memset(t->mTileCache, 0xff, t->mTileCacheSize * sizeof(UWORD));
	}
}

// FUNCTION: 0x005449a0 ??_ECLandscapeTexture@@UAEPAXI@Z

//******************************************************************************************
CDXLandscape::CDXLandscape()	// FUNCTION: 0x00544a00
{
	mVB = NULL;
	mIB = NULL;
	mTexture = NULL;
	mViews = NULL;
	mCloudTexture = NULL;
	mUnknown3C = FALSE;
	mUnknown08 = NULL;
	mDepthSurface = NULL;
	mDepthTexture = NULL;
	mMirrorTexture = NULL;
	mVertexShader = NULL;
	mPixelShader = NULL;
}

// FUNCTION: 0x00544a40 ??_GCDXLandscape@@UAEPAXI@Z

//******************************************************************************************
CDXLandscape::~CDXLandscape()	// FUNCTION: 0x00544a60
{
	LT.RemoveDeviceObject(this);
	if (mVertexShader)
	{
		mVertexShader->Release();
		mVertexShader = NULL;
	}
	LT.D3D_Release((IUnknown **)&mPixelShader);
}

//******************************************************************************************
BOOL CDXLandscape::Init(CMapTex *maptexs)	// FUNCTION: 0x00544af0
{
	CLandscapeTexture::ResetUpdateQueue();
	mNumViews = 0;

	SINT	oldmode = LT.m_bTextureCompressionDisabled;
	LT.m_bTextureCompressionDisabled = 1;
	mDamage.Init();

	CONSOLE.RegisterCommand("BuildLandscapeCache", "Build the landscape texture cache", con_buildlandscapecache);
	CONSOLE.RegisterVariable("xx_coastcalc", "coast calculation thingy", CVar_bool, &sCoastCalc);

#line 169
	mTexture = new(MEMTYPE_TEXTURE) CLandscapeTexture;
	mTexture->Init(0, 4);
#line 174
	mVB = new(MEMTYPE_LANDSCAPE) CVBuffer;
	mVB->Create(65 * 65, 0x14, D3DFVF_XYZ | D3DFVF_TEX1);
#line 179
	mIB = new(MEMTYPE_LANDSCAPE) CIBuffer;
	mIB->CreateInternal(40000, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT);

	LT.m_bTextureCompressionDisabled = oldmode;
	LT.AddDeviceObject(this);
	InitDeviceObjects();

	if (mPixelShader && mUnknown08 && mDepthSurface)
		mVertexShader = CVertexShader::Create("LandscapeShader", (DWORD *)
			"vs.1.1\n"
			"\n"
			"dcl_position v0\n"
			"dcl_texcoord v7\n"
			"\n"
			"; world/view/projection\n"
			"\n"
			"dp4 r2.x, v0, c[4]\n"
			"dp4 r2.y, v0, c[5]\n"
			"dp4 r2.z, v0, c[6]\n"
			"dp4 r2.w, v0, c[7]\n"
			"\n"
			"dp4 r3.x, r2, c[8]\n"
			"dp4 r3.y, r2, c[9]\n"
			"dp4 r3.z, r2, c[10]\n"
			"dp4 r3.w, r2, c[11]\n"
			"\n"
			"mov oPos, r3\n"
			"\n"
			"; stick z in the red, green and blue channels\n"
			"mul oT0.xyz, r3.z, c[95].z\n"
			"mov oT0.w, c[94].w\n", 4, NULL, 0, -1);

	return TRUE;
}

//******************************************************************************************
HRESULT CDXLandscape::InitDeviceObjects()	// FUNCTION: 0x00544cc0
{
	if ((GEFORCE_FX_POWER.mValue) && (!GAME.IsMultiplayer()) && (EXTRA_GEFORCE_FX_FEATURES.mValue) &&
		(LT.m_d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2, 0)))
	{
		if (FAILED(LT.D3D_CreatePixelShader("ps_2_0\ndcl t0\nmov oC0, t0\n", &mPixelShader)))
			return S_OK;

		HRESULT	hr = LT.D3D_CreateTextureNoCheck(256, 256, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R5G6B5, D3DPOOL_DEFAULT, &mMirrorTexture);
		if (FAILED(hr))
			SETUP_HISTORY.AddMessage("ReflectionsDestination disallowed for %s", DXGetErrorString9(hr));

		if (MY_READABLE_Z_BUFFER.mValue)
		{
			D3DFORMAT format = R32F_WORKS.mValue ? D3DFMT_R32F : D3DFMT_A32B32G32R32F;
			hr = LT.D3D_CreateTextureNoCheck(512, 512, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &mUnknown08);
			if (FAILED(hr))
				SETUP_HISTORY.AddMessage("FakeBackBuffer disallowed for %s", DXGetErrorString9(hr));

			if (OTHER_ZBUFFER_AS_TEXTURE.mValue)
			{
				hr = LT.D3D_CreateTextureNoCheck(512, 512, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D16, D3DPOOL_DEFAULT, &mDepthTexture);
				if (SUCCEEDED(hr))
					mDepthTexture->GetSurfaceLevel(0, &mDepthSurface);
			}
			else
				hr = LT.GetDevice()->CreateDepthStencilSurface(512, 512, D3DFMT_D16, D3DMULTISAMPLE_NONE, 0, TRUE, &mDepthSurface, NULL);
			if (FAILED(hr))
				SETUP_HISTORY.AddMessage("FakeZBuffer disallowed for %s", DXGetErrorString9(hr));
		}
		else
		{
			if (OTHER_ZBUFFER_AS_TEXTURE.mValue)
			{
				hr = LT.D3D_CreateTextureNoCheck(256, 256, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D16, D3DPOOL_DEFAULT, &mDepthTexture);
				if (SUCCEEDED(hr))
					mDepthTexture->GetSurfaceLevel(0, &mDepthSurface);
			}
			else
				hr = LT.GetDevice()->CreateDepthStencilSurface(256, 256, D3DFMT_D16, D3DMULTISAMPLE_NONE, 0, TRUE, &mDepthSurface, NULL);
			if (FAILED(hr))
				SETUP_HISTORY.AddMessage("FakeZBuffer disallowed for %s", DXGetErrorString9(hr));
		}
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CDXLandscape::DeleteDeviceObjects()	// FUNCTION: 0x00544eb0
{
	SAFE_RELEASE(mUnknown08);
	SAFE_RELEASE(mDepthTexture);
	SAFE_RELEASE(mDepthSurface);
	SAFE_RELEASE(mMirrorTexture);
	LT.D3D_Release((IUnknown **)&mPixelShader);
	return S_OK;
}

//******************************************************************************************
void CDXLandscape::ShutDown()	// FUNCTION: 0x00544f10
{
	DeleteDeviceObjects();
	LT.RemoveDeviceObject(this);
	CLandscapeTexture::ResetUpdateQueue();

	if (mViews)
	{
		delete [] mViews;
		mViews = NULL;
	}
	SAFE_DELETE(mVB);
	SAFE_DELETE(mIB);
	SAFE_DELETE(mTexture);
	SURF.Destroy();
	if (mCloudTexture)
	{
		mCloudTexture->Release();
		mCloudTexture = NULL;
	}
}

//******************************************************************************************
void CDXLandscape::ResetPos(int x, int y)	// FUNCTION: 0x00544fb0
{
	BuildLevelSpecifics();
}

//******************************************************************************************
// The vertex buffer holds the whole 65x65 grid of 8-metre posts; the texture coordinates are the positions.
struct SLandscapeVertex
{
	float	mX, mY, mZ;
	float	mU, mV;
};

void CDXLandscape::BuildVertexBuffer()	// FUNCTION: 0x00544fc0
{
	void	*data;
	mVB->Lock(&data);
	SLandscapeVertex	*v = (SLandscapeVertex *)data;

	SINT	y = 0;
	for (SINT j = 0; j < 65; j++)
	{
		SINT	x = 0;
		float	fy = (float)y;
		for (SINT i = 0; i < 65; i++)
		{
			float	h = MAP.GetHeight(x, y);
			v->mY = fy;
			v->mV = fy;
			v->mZ = h;
			float	fx = (float)x;
			v->mX = fx;
			v->mU = fx;
			x += 8;
			v++;
		}
		y += 8;
	}

	mVB->Unlock();
}

//******************************************************************************************
// Makes the views (two in multiplayer, with fewer levels), the vertex buffer and the tiles' height ranges and
// complexities.
void CDXLandscape::BuildLevelSpecifics()	// FUNCTION: 0x00545070
{
	if (mViews)
	{
		delete [] mViews;
		mViews = NULL;
	}

	CLandscapeTexture::ResetUpdateQueue();
	if (GAME.IsMultiplayer())
	{
#line 434
		mViews = new(MEMTYPE_LANDSCAPE) CLandscapeView[2];
		mNumViews = 2;
		mViews[0].CreateMipLevels(4);
		mViews[1].CreateMipLevels(4);
		SHADOWS.mShadows = false;
	}
	else
	{
#line 443
		mViews = new(MEMTYPE_LANDSCAPE) CLandscapeView[1];
		mNumViews = 1;
		mViews[0].CreateMipLevels(5);
		SHADOWS.mShadows = true;
	}

	BuildVertexBuffer();

	SLandscapeCell	*cell = mViews[0].mTiles;
	SINT			x, y;
	for (y = 0; y < 512; y += 8)
	{
		for (x = 0; x < 512; x += 8)
		{
			float	hi = -10000.0f;
			float	lo = 10000.0f;
			for (SINT j = 0; j <= 8; j++)
			{
				for (SINT i = 0; i <= 8; i++)
				{
					FVector	p;
					p.X = (float)((x + i) & 511);
					p.Y = (float)((y + j) & 511);
					float	h = MAP.Collide(p);
					if (h < lo)
						lo = h;
					if (h > hi)
						hi = h;
				}
			}

			float	f = hi * 127.0f;
			SINT	v = (SINT)(f * (1.0f / 60.0f));
			if (v < -127)
				v = -127;
			else if (v > 127)
				v = 127;
			cell->mMaxHeight = v;
			f = lo * 127.0f;
			v = (SINT)(f * (1.0f / 60.0f));
			if (v < -127)
				v = -127;
			else if (v > 127)
				v = 127;
			cell->mMinHeight = v;
			cell->mUnknown10 = 0x9d;
			cell->mPatch = NULL;
			cell->mMidHeight = (lo + hi) * 0.5f;
			cell++;
		}
	}

	CLandscapeView	*view = mViews;
	float			most = 0;
	SINT			t;
	for (t = 0; t < 64 * 64; t++)
	{
		float	c = TileComplexity(t);
		view->mTiles[t].mComplexity = c;
		if (c > most)
			most = c;
	}
	for (t = 0; t < 64 * 64; t++)
		view->mTiles[t].mComplexity *= 128.0f;

	if (mNumViews == 2)
	{
		SLandscapeCell	*src = mViews[0].mTiles;
		SLandscapeCell	*dst = mViews[1].mTiles;
		for (t = 0; t < 64 * 64; t++)
		{
			dst->mMaxHeight = src->mMaxHeight;
			dst->mMinHeight = src->mMinHeight;
			dst->mMidHeight = src->mMidHeight;
			dst->mUnknown10 = src->mUnknown10;
			dst->mPatch = NULL;
			dst->mComplexity = src->mComplexity;
			src++;
			dst++;
		}
	}

	for (SINT l = 0; l < 3; l++)
		PATCHMANAGER.mLists[l].Reset();

	mTexture->Refresh();
	SURF.LoadWaves();
}

//******************************************************************************************
void CDXLandscape::InitResources()	// FUNCTION: 0x005453d0
{
	mCloudTexture = CDXTexture::FindTexture("clouds\\shadow.tga", 4, 0, -1, 1, 1);
}

//******************************************************************************************
void CDXLandscape::UpdatePos(CCamera *cam, int viewpoint)	// FUNCTION: 0x005453f0
{
	mViews[viewpoint].mCamera = cam;
}

//******************************************************************************************
void CDXLandscape::Render(C3DGamut *gamut, int viewpoint)	// FUNCTION: 0x00545410
{
	SINT	nav = ENGINE.mNavDisplay;
	if (nav != sLastNavDisplay)
	{
		BuildLevelSpecifics();
		CalcLODs(gamut, viewpoint);
		sLastNavDisplay = nav;
	}

	RENDERINFO.SetWorld(sLandscapePos, sLandscapeOri);
	LT.SRS(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
	LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
	LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	LT.SetModulate(0);
	LT.SetModulate(1);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	RENDERINFO.SetAmbient(0);
	LT.m_pd3dDevice->SetMaterial(&STATE.mLandscapeMaterial);
	SINT	i;
	for (i = 0; i < 8; i++)
	{
		if (RENDERINFO.mLightEnable[i])
			RENDERINFO.ApplyLight(i, TRUE);
	}

	RenderTerrain(viewpoint);

	RENDERINFO.SetAmbient(MAP.GetAmbient());
	for (i = 0; i < 8; i++)
	{
		if (RENDERINFO.mLightEnable[i])
			RENDERINFO.ApplyLight(i, FALSE);
	}
}

CRenderMethod	LANDSCAPE_METHOD("landscape_method", 3);	// 008aa920
// FUNCTION: 0x00545560 init:LANDSCAPE_METHOD
// FUNCTION: 0x00545580 exit:LANDSCAPE_METHOD

//******************************************************************************************
// The terrain: the best method the device takes (2: texture, mixer, drifting cloud shadows and a turned mixer over
// four stages; 1: texture and a turned mixer; 0: the texture), then the far ring and each level's patches with their
// own texture at doubling scale; the states are put back and the GeForce FX shadow map drawn.
// The patch count runs down in a for statement and the mixer turns use cosf and sinf (math.h's inline float
// overloads): with both, the count is stored after its test (00545f88) and the view (esi) reloads at the bottom of the
// patch loop (00545ff3), as in retail; either alone leaves one of them out. _11 is set before _22 for the level's
// scale, which gives retail's store order (00545f56, 00545f60).
void CDXLandscape::RenderTerrain(int viewpoint)	// FUNCTION: 0x00545590
{
	if (!LANDSCAPE_LIGHTING.mValue)
		RENDERINFO.EnableLighting(false);

	CLandscapeView	*view = &mViews[viewpoint];
	float	scale = 1.0f / 512;
	if (viewpoint == 0)
	{
		sCloudU += GAME.GetRenderFrameLength() * 0.001f;
		if (sCloudU > 1.0f)
			sCloudU -= 1.0f;
		sCloudV += GAME.GetRenderFrameLength() * 0.0005f;
		if (sCloudV > 1.0f)
			sCloudV -= 1.0f;
	}
	memset(&gTextureMatrix, 0, sizeof(gTextureMatrix));
	if (!USE_MODULATE_2X.mValue)
		LANDSCAPE_METHOD.SetMethod(0);

	if (LANDSCAPE_METHOD.Try(2))
	{
		if (LANDSCAPE_LIGHTING.mValue)
			LT.SetModulate(0);
		else
			LT.ForceTS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.ForceTS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.ForceTS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.ForceTS(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.ForceTS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.ForceTS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		LT.ForceTS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.ForceTS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.ForceTS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.ForceTS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		LT.ForceTS(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.ForceTS(2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.ForceTS(2, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.ForceTS(2, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		LT.ForceTS(3, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.ForceTS(3, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.ForceTS(3, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.ForceTS(3, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		LT.ForceTS(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.SetModulate(2);
		LT.SetModulate(3);

		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
		LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY, 4);
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 1.0f / 512;
		gTextureMatrix._22 = 1.0f / 512;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		LT.ForceTS(0, D3DTSS_TEXCOORDINDEX, 0);
		LT.D3D_SetTexture(0, mTexture->mTexture);

		LT.D3D_SetTexture(1, mMixerTexture->GetTexture());
		SetMipFilterByGlobalToggle(1);
		LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(1, D3DTSS_TEXCOORDINDEX, 0);
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 1.0f;
		gTextureMatrix._22 = 1.0f;
		gTextureMatrix._31 = 0;
		gTextureMatrix._32 = 0;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);

		LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(2, D3DTSS_TEXCOORDINDEX, 0);
		LT.D3D_SetTexture(2, mCloudTexture->GetTexture());
		gTextureMatrix._31 = sCloudU;
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 1.0f / 256;
		gTextureMatrix._22 = 1.0f / 256;
		gTextureMatrix._32 = sCloudV;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE2, &gTextureMatrix);

		LT.D3D_SetTexture(3, mMixerTexture->GetTexture());
		LT.STS(3, D3DTSS_TEXCOORDINDEX, 0);
		LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		float	c = cosf(1.0f);
		gTextureMatrix._31 = 0.3f;
		gTextureMatrix._32 = 0.3f;
		float	sn = sinf(1.0f);
		gTextureMatrix._11 = c * 0.25f;
		gTextureMatrix._12 = sn * 0.25f;
		gTextureMatrix._21 = -(sn * 0.25f);
		gTextureMatrix._22 = c * 0.25f;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE3, &gTextureMatrix);

		RENDERINFO.Apply(false, true);
		LT.GetDevice()->SetIndices(mIB->mIB);
		mVB->SetStreamSource(0);
		if (!LANDSCAPE_METHOD.Validate())
		{
			LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			LT.STS(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
			LT.STS(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			LT.STS(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
			LT.STS(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
			LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
			LT.STS(2, D3DTSS_TEXCOORDINDEX, 2);
			LT.STS(3, D3DTSS_TEXCOORDINDEX, 3);
		}
		else
			LANDSCAPE_METHOD.Accept();
	}

	if (LANDSCAPE_METHOD.Try(1))
	{
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
		LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY, 4);
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 1.0f / 512;
		gTextureMatrix._22 = 1.0f / 512;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		LT.D3D_SetTexture(0, mTexture->mTexture);
		LT.D3D_SetTexture(1, mMixerTexture->GetTexture());
		LT.SetModulate(0);
		LT.ForceTS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.ForceTS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.ForceTS(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.ForceTS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.ForceTS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		LT.ForceTS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.ForceTS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.ForceTS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.ForceTS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		SetMipFilterByGlobalToggle(1);
		LT.SetModulate(1);
		LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(1, D3DTSS_TEXCOORDINDEX, 0);
		float	c = cosf(1.0f);
		gTextureMatrix._31 = 0.3f;
		gTextureMatrix._32 = 0.3f;
		float	sn = sinf(1.0f);
		gTextureMatrix._11 = c * 0.7f;
		gTextureMatrix._12 = sn * 0.7f;
		gTextureMatrix._21 = -(sn * 0.7f);
		gTextureMatrix._22 = c * 0.7f;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);
		mVB->SetStreamSource(0);
		RENDERINFO.Apply(false, true);
		LT.GetDevice()->SetIndices(mIB->mIB);
		if (!LANDSCAPE_METHOD.Validate())
		{
			LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
			LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
			LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
		else
			LANDSCAPE_METHOD.Accept();
	}

	if (LANDSCAPE_METHOD.Try(0))
	{
		LT.SetModulate(0);
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
		LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY, 4);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 1.0f / 512;
		gTextureMatrix._22 = 1.0f / 512;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		LT.D3D_SetTexture(0, mTexture->mTexture);
		mVB->SetStreamSource(0);
		RENDERINFO.Apply(false, true);
		LT.GetDevice()->SetIndices(mIB->mIB);
		if (LANDSCAPE_METHOD.Validate())
			LANDSCAPE_METHOD.Accept();
	}

	gTextureMatrix._31 = 0;
	gTextureMatrix._32 = 0;
	if (mUnknown34 > 2)
	{
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 1.0f / 512;
		gTextureMatrix._22 = 1.0f / 512;
		scale = 1.0f / 512;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 65 * 65, 0, mUnknown34 - 2);
	}

	for (SINT l = 0; l < view->mNumLevels; l++)
	{
		if (l)
			LT.D3D_SetTexture(0, view->mTextures[l - 1].mTexture);
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = scale;
		gTextureMatrix._22 = scale;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);

		UWORD	*tile = view->mLODs[l].mData;
		for (SINT n = view->mLODs[l].mUnknown08; n; n--)
		{
			SLandscapeCell	*cell = &view->mTiles[*tile];
			CLandscapeIB	*ib = &PATCHMANAGER.mIBs[cell->mUnknown10 * 16 + cell->mEdges];
			CLandscapeVB	*vb = (CLandscapeVB *)cell->mPatch;
			LT.GetDevice()->SetIndices(ib->mIB);
			vb->SetStreamSourceOnly(0);
			LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, vb->mNumVerts, 0, ib->mCount);
			tile++;
		}
		scale += scale;
	}

	if (LANDSCAPE_METHOD.GetMethod() >= 2)
	{
		LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(2, D3DTSS_TEXCOORDINDEX, 2);
		LT.STS(3, D3DTSS_TEXCOORDINDEX, 3);
		LT.SSS_NoCache(2, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		LT.STS(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(2, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.STS(2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(2, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		LT.STS(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	}
	if (LANDSCAPE_METHOD.GetMethod() >= 1)
	{
		LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	}
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY, 1);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	if (!gMirrorPass)
		RenderShadowMap(0);
	if (!LANDSCAPE_LIGHTING.mValue)
		RENDERINFO.EnableLighting(true);
}

//******************************************************************************************
static inline void RestoreTargets(IDirect3DSurface9 *rt, IDirect3DSurface9 *ds)
{
	LPDIRECT3DDEVICE9	dev = LT.m_pd3dDevice;
	HRESULT hr = dev->SetDepthStencilSurface(ds);
	if (FAILED(hr))
		dev->SetRenderTarget(0, rt);
	else
		dev->SetRenderTarget(0, rt);
}

//******************************************************************************************
// Renders into a texture: saves the render target and depth surface, sets the texture's top level with the
// landscape's fake Z buffer.
bool CRenderTargetSwap::Begin(IDirect3DTexture9 *target)	// FUNCTION: 0x00546220
{
	if (!ENGINE.GetLandscape())
		return false;

	if (FAILED(LT.m_pd3dDevice->GetRenderTarget(0, &mRenderTarget)))
	{
		EXTRA_GEFORCE_FX_FEATURES.Set(0);
		return false;
	}
	if (FAILED(LT.m_pd3dDevice->GetDepthStencilSurface(&mDepthStencil)))
	{
		EXTRA_GEFORCE_FX_FEATURES.Set(0);
		return false;
	}

	IDirect3DSurface9	*surface;
	if (FAILED(target->GetSurfaceLevel(0, &surface)))
	{
		EXTRA_GEFORCE_FX_FEATURES.Set(0);
		return false;
	}

	HRESULT	hr = LT.m_pd3dDevice->SetRenderTarget(0, surface);
	if (FAILED(hr))
	{
		SETUP_HISTORY.AddMessage("Failed SRT in for %s", DXGetErrorString9(hr));
		RestoreTargets(mRenderTarget, mDepthStencil);
		EXTRA_GEFORCE_FX_FEATURES.Set(0);
		return false;
	}
	hr = LT.GetDevice()->SetDepthStencilSurface(ENGINE.GetLandscape()->mDepthSurface);
	if (FAILED(hr))
	{
		SETUP_HISTORY.AddMessage("Failed SDSS in for %s", DXGetErrorString9(hr));
		RestoreTargets(mRenderTarget, mDepthStencil);
		EXTRA_GEFORCE_FX_FEATURES.Set(0);
		return false;
	}

	surface->Release();
	return true;
}

//******************************************************************************************
void CRenderTargetSwap::End()	// FUNCTION: 0x005463f0
{
	RestoreTargets(mRenderTarget, mDepthStencil);

	SAFE_RELEASE(mRenderTarget);
	SAFE_RELEASE(mDepthStencil);
}

//******************************************************************************************
CRenderTargetSwap::~CRenderTargetSwap()	// FUNCTION: 0x00546460
{
	SAFE_RELEASE(mRenderTarget);
	SAFE_RELEASE(mDepthStencil);
}

// the shadow map's pixel-shader constants (c0..c2)
static float	SHADOW_PS_CONSTANTS[12] =
{
	2048.0f, 32.0f, 1.0f, 1.0f,
	1.0f, 1.0f, 1.0f, 1.0f,
	-0.0014f, -0.0002f, -0.00140625f, 0.0f
};

//******************************************************************************************
// The GeForce FX shadow map: the whole landscape and the water drawn into mUnknown08 with the shadow shaders.
// Retail keeps the whole-grid index buffer as a pointer before binding its native resource. The two index bindings
// follow the source shell's D3D_SetIndices operation, adapted locally to D3D 9. The patch loop reads view->mLODs[l] twice;
// the draw counts remain separate values. RestoreTargets retains the HRESULT from SetDepthStencilSurface so the
// FAILED test uses the zero already in edi. Together these value and call boundaries reproduce the entire routine,
// including exception metadata; they do not establish the original source spelling or rendered acceptance.
static inline HRESULT SetLandscapeIndices(PCLTShell *shell, IDirect3DIndexBuffer9 *indices)
{
	return shell->m_pd3dDevice->SetIndices(indices);
}

bool CDXLandscape::RenderShadowMap(int viewpoint)	// FUNCTION: 0x00546490
{
	if (!EXTRA_GEFORCE_FX_FEATURES.mValue || GAME.IsMultiplayer() || !mUnknown08 || !mDepthSurface || !mPixelShader ||
		!mVertexShader || !EXTRA_GEFORCE_FX_FEATURES.mValue)
		return false;

	CLandscapeView	*view = &mViews[viewpoint];

	LT.SetModulate(0);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
	LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY, 4);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.D3D_SetTexture(0, NULL);
	mVB->SetStreamSource(0);
	RENDERINFO.Apply(false, true);
	CIBuffer *wholeIB = mIB;
	SetLandscapeIndices(&LT, wholeIB->mIB);
	LT.SetVertexShader(mVertexShader);
	mVertexShader->SetConstants();
	if (!mPixelShader || FAILED(LT.GetDevice()->SetPixelShader(mPixelShader)))
	{
		EXTRA_GEFORCE_FX_FEATURES.Set(0);
		return false;
	}
	LT.GetDevice()->SetPixelShaderConstantF(0, SHADOW_PS_CONSTANTS, 3);

	CRenderTargetSwap	swap;
	if (!swap.Begin(mUnknown08))
		return false;

	LT.GetDevice()->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffffffff, 1.f, 0);
	DWORD	dither = LT.mRenderStates[D3DRS_DITHERENABLE];
	LT.SRS(D3DRS_DITHERENABLE, FALSE);

	// the whole grid at the coarsest level, then every level of detail's patches
	if (mUnknown34 > 2)
		LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 0x1081, 0, mUnknown34 - 2);
	for (SINT l = 0; l < view->mNumLevels; l++)
	{
		UWORD			*cells = view->mLODs[l].mData;
		for (SINT n = view->mLODs[l].mUnknown08; n; n--)
		{
			SLandscapeCell	*cell = &view->mTiles[*cells];
			CLandscapeIB	*ib = &PATCHMANAGER.mIBs[cell->mUnknown10 * 16 + cell->mEdges];
			CLandscapeVB	*vb = (CLandscapeVB *)cell->mPatch;
			SetLandscapeIndices(&LT, ib->mIB);
			vb->SetStreamSourceOnly(0);
			SINT			verts = vb->mNumVerts;
			SINT			count = ib->mCount;
			LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, verts, 0, count);
			cells++;
		}
	}
	ENGINE.GetWater()->RenderShadow();

	swap.End();
	LT.SRS(D3DRS_DITHERENABLE, dither);
	LT.GetDevice()->SetPixelShader(NULL);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY, 1);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	return true;
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00544710 ??0CLandscapeView@@QAE@XZ

extern CRenderMethod	SHADOW_METHOD;			// 009c7c58 (DXShadows.cpp)

//******************************************************************************************
// The tiles under a rectangle of the shadow pass (CDXShadows 00553f54), every patch drawn at its own level of detail;
// a device that rejects the shadow method ends the pass.
void CDXLandscape::RenderTiles(SINT x0, SINT x1, SINT y0, SINT y1)	// FUNCTION: 0x00546900
{
	CLandscapeView	*view = mViews;
	FVector	centre;
	centre.X = (float(x0) + x1) * 0.5f;
	centre.Y = (float(y0) + y1) * 0.5f;
	centre.Z = MAP.Collide(centre);
	centre -= view->mCamera->GetPos();

	y0 /= 8;
	x0 /= 8;
	x1 /= 8;
	y1 /= 8;
	if (y0 < 0)
		y0 = 0;
	if (x0 < 0)
		x0 = 0;
	if (x1 > 63)
		x1 = 63;
	if (y1 > 63)
		y1 = 63;

	for (SINT y = y0; y <= y1; y++)
		for (SINT x = x0; x <= x1; x++)
		{
			SLandscapeCell	*cell = &view->mTiles[y * 64 + x];
			if (cell->mUnknown10 >= 0)
			{
				CLandscapeIB	*ib = &PATCHMANAGER.mIBs[cell->mUnknown10 * 16 + cell->mEdges];
				CLandscapeVB	*vb = (CLandscapeVB *)cell->mPatch;
				vb->SetStreamSourceOnly(0);
				LT.GetDevice()->SetIndices(ib->mIB);
				RENDERINFO.SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
				if (CVertexShader::mSoftware)
					LT.SetSoftwareVertexProcessing(false);
				RENDERINFO.Apply(false, true);
				if (!SHADOW_METHOD.Validate())
					return;
				SHADOW_METHOD.Accept();
				LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, vb->mNumVerts, 0, ib->mCount);
			}
		}
}

//******************************************************************************************
void CDXLandscape::MakeNextUpdateBeFast()	// FUNCTION: 0x00546b10
{
	if (mViews)
	{
		mViews[0].mLastPos.X = 1234567.0f;
		if (GAME.IsMultiplayer())
			mViews[1].mLastPos.X = 1234567.0f;
	}
}

// the patch level for each (whole) level of detail
static SINT		sPatchLevel[9] = { -1, -1, 0, 0, 1, 1, 1, 1, 2 };	// 00650ae8

// How far the LOD centre follows the camera each frame (005e50c8): a real constant, since retail subtracts it from 1
// at run time instead of folding it.
extern const float CAMERA_LAG = 0.03f;

// Takes a free patch from the level's list; FALSE when the list is full. The slot comes in as a UWORD and Allocate
// takes an int: retail widens it once before the search (`and edi,0xffff`, 00546f96). Both types are inferred from that
// widening; the original declarations are unknown.
static inline BOOL AllocatePatch(SINT level, UWORD slot, CLandscapeVB *&patch)
{
	patch = PATCHMANAGER.mLists[level].Allocate(slot);
	if (patch)
		return TRUE;
	return FALSE;
}

// The camera position's lagged share, written out per component: retail multiplies each component by CAMERA_LAG from
// memory (00546c54, 00546c64, 00546c6c), where the vector operator* keeps the factor on the FPU.
static inline FVector PositionLagTerm(const FVector &p)
{
	return FVector(p.X * CAMERA_LAG, p.Y * CAMERA_LAG, p.Z * CAMERA_LAG);
}

// The view's smoothed position and direction: each moves CAMERA_LAG of the way to the camera's.
static inline void SmoothLandscapeView(CLandscapeView *view, const FVector &p, const FVector &dir)
{
	view->mLastPos = PositionLagTerm(p) + view->mLastPos * (1.0f - CAMERA_LAG);
	view->mUnknown24 = dir * CAMERA_LAG + view->mUnknown24 * (1.0f - CAMERA_LAG);
}

// Frees the tile's patch slot.
static inline void RetireLandscapePatch(SLandscapeCell *tile)
{
	tile->mPatch->mSlot = 0xffff;
	tile->mPatch = NULL;
}

// Ends the far ring's strip with a degenerate pair at vertex vert - back. The chained assignment stores right to
// left: retail writes idx[1] before idx[0] (00546f2d).
static inline void CloseLandscapeStrip(UWORD *&idx, BOOL &joined, SINT vert, SINT back)
{
	idx[0] = idx[1] = vert - back;
	joined = TRUE;
	idx += 2;
}

// The tile's texture level by its distance ahead of the view: 0 for the far tiles, then 1 to 4 nearer in. dz2 is the
// caller's, by reference: retail lowers it in place in the slot it was computed in (00546ea7, 0054701d).
static inline SINT ChooseLandscapeTexture(CLandscapeView *view, const FVector &cam, const FVector &look, float dist2,
	float &dz2)
{
	SINT	tex;
	if ((LANDSCAPE_MAXLEVELS_USER.mValue >= 3) && (dist2 <= 16384.0f))
	{
		dz2 -= 64.0f;
		if (dz2 < 0)
			dz2 = 0;
		float	ax = cam.X - look.X * 60.0f;
		float	ay = cam.Y - look.Y * 60.0f;
		if (ax * ax + ay * ay + dz2 < 4096.0f)
		{
			if ((LANDSCAPE_MAXLEVELS_USER.mValue > 3) && (view->mNumLevels > 3))
			{
				float	bx = cam.X - look.X * 28.0f;
				float	by = cam.Y - look.Y * 28.0f;
				if (bx * bx + by * by + dz2 < 1024.0f)
				{
					if ((LANDSCAPE_MAXLEVELS_USER.mValue > 4) && (view->mNumLevels > 4))
					{
						float	cx = cam.X - look.X * 12.0f;
						float	cy = cam.Y - look.Y * 12.0f;
						if (cx * cx + cy * cy + dz2 < 256.0f)
							tex = 4;
						else
							tex = 3;
					}
					else
						tex = 3;
				}
				else
					tex = 2;
			}
			else
				tex = 2;
		}
		else
			tex = 1;
	}
	else
		tex = 0;
	return tex;
}

// Stitches the tile's edges to coarser neighbours.
static inline void StitchLandscapeCell(SLandscapeCell *tile, SINT x, SINT y)
{
	tile->mEdges = 6;
	if (x)
	{
		SLandscapeCell	*left = tile - 1;
		SINT	d = tile->mUnknown10 - left->mUnknown10;
		if (d >= 0)
		{
			left->mEdges &= ~2;
			if (d)
				tile->mEdges |= 8;
		}
	}
	if (y)
	{
		SINT	d = tile->mUnknown10 - tile[-64].mUnknown10;
		if (d >= 0)
		{
			tile[-64].mEdges &= ~4;
			if (d)
				tile->mEdges |= 1;
		}
	}
}

// Strings a far-ring tile into the strip (a degenerate pair first when the strip was closed); joined is the caller's,
// by reference: retail stores it straight to its memory home (00547321). Retail stores the degenerate triple idx[2],
// idx[1], idx[0] (00547307..0054730f). idx[3] is written before idx[5]: below's reload is still live when below - 1 is
// formed, so the difference takes a register of its own (`lea ebx,[eax-1]`, 0054731e) instead of decrementing below's
// eax, and the scheduler stores it after the step as retail does (00547329). Written after the step (idx[-3]) it is
// `dec eax`.
static inline void AppendLandscapeFarCell(UWORD *&idx, BOOL &joined, SINT vert, SINT below)
{
	switch (joined)
	{
	case 0:
		idx[0] = vert;
		idx[1] = below;
		idx += 2;
		break;
	case 1:
		idx[4] = vert;
		idx[0] = idx[1] = idx[2] = vert - 1;
		idx[3] = below - 1;
		idx[5] = below;
		idx += 6;
		joined = FALSE;
		break;
	}
}

//******************************************************************************************
// STAND-IN (CalcLODs 00546b40): the actual FMatrix column read through a free function, with its second and third
// values bracketed. The brackets add two no-code x87 nodes (C2's 0x162) to the column's store chain. They fill the two
// idle cycles between its stores (scheduler trace, CalcLODs' region 2), so `lea esi,[edi+0x14]` and `patches = 0` issue
// after the folded load of mLastPos.X, as retail's do (00546be3..00546bf4), not in the first gap. The shared GetColumnY
// stays unbracketed; the original helper ownership and spelling are unknown.
// This valid const FMatrix reference replaces the former wrong-type FLandscapeOri derived cast. The pinned VC6
// produces identical section bytes and relocation meanings; only generated exception-label names change. The two
// bracket tuples remain an explicit compiler stand-in, not recovered source or a portable scheduling guarantee.
static inline FVector LandscapeColumnY(const FMatrix &orientation)
{
 return FVector(orientation.Row[0].Y, (orientation.Row[1].Y), (orientation.Row[2].Y));
}

// Each tile's level of detail from the viewpoint's (smoothed) position: tiles out of view drop their patches, far or
// plain tiles are strung into the far ring's strip (the vertex grid, degenerate pairs where it breaks), and near ones
// take a patch at their level and a texture level by distance along the view.
// The smoothing adds the lagged term first: retail's operator+ calls (00546c9a, 00546cf8) take the `* CAMERA_LAG`
// product as this and evaluate the `* (1 - CAMERA_LAG)` argument before it (00546c4b, 00546cd4).
// Near miss. The helpers above are an earlier reconstruction (its far-strip-transaction candidate); their
// original ownership is unknown. With them retail's 13 calls come in retail's order: the smoothing in a helper calls
// its vector operators and constructors out of line (00546c4b..00546cf8) where this body expanded them. Measured
// without each (shape/instructions, 159/463 with all): the smoothing helper 327/667, the
// texture level 234/538, the far-ring step 224/516, the tile taken by index (`&tiles[t]`, not stepped) 260/554, the
// patch release or the strip close 193/499 each, the written-out lag term 169/479; the LOD queue and the stitch are
// neutral alone but not together (329 aligned differences against 287). An out-of-view test helper gives the same
// calls at 175/475. The patch search tests AllocatePatch's if/return value, which VC6 jump-threads: the loop ends
// JS far ring, JMP back (00546fb0) as retail's does, where a bare for (;;) is rotated.
// Retail keeps the tile index t in memory: stored once (00546e3a) and re-read at each use, the slot (00546f62), the
// queue's 16-bit read (`mov bx,WORD PTR [esp+0x3c]`, 00547130) and twice in RequestTile: at 00547157, after the
// queue's stores, and again at 0054718b with no store or call between the two (why retail reads it a second time is
// unknown). t is declared at function scope. STAND-IN (dead escape, see the end of the lock block): `if (0)
// ToWCHAR((char *)&t);` never runs and emits nothing; taking t's address keeps it in memory in VC6's intermediate
// representation, which gives the store, the slot's reload in retail's order, the queue's low-word read and
// RequestTile's first read (the strip-close stores through idx kill t's value because t's scope encloses idx's origin).
// Plain form (t a register local) 23/155/1873 at that step, 41/133/1332 without the escape on the match; volatile t
// (the former stand-in) reads the whole word for the queue. What keeps retail's t in memory is unknown; replace it when a real use of t's address is found.
// With t in memory come these forms: the tile stepped in the for-increment, SetOrigin's x scaled by a
// shift (VC6 strength-reduces x * 8 but not x << 3; retail keeps x, `inc; cmp 0x40`) and a named left neighbour in the
// stitch (retail's IV esi = tile - 3 beside the spilled tile, 00546e2b). Measured (raw / register- /
// register+slot-masked against retail's full body): 463/307/159 -> 423/247/79, and all 30 of retail's x87 expression
// trees in retail's association (27 before; a ledger diagnostic). With t in registers the three
// forms are worse (162/528 by mvar, frame 0xd4). These compile identically: the queue's index by reference, the store
// without its cast,
// `mData[mUnknown08++]`, `view->mLODs + tex`, a named texture pointer, the level loop's counter shared with x or y.
// Then: the strip's chained stores in retail's order (-> 409/237/69); the patch slot widened from a
// UWORD for an int Allocate parameter (retail's `and edi,0xffff` before the search, 00546f96) and RequestTile's flag
// kept in memory as retail's byte (005471a4) give the frame retail's 0xd8 (-> 361/215/67; alone 379/239/73 and
// 383/211/63); the texture level lowering the caller's dz2 in place (by reference) -> 317/169/67; t volatile
// (254/118/54) with the far ring's flag passed by reference, which retail stores straight to its memory home (00546de2,
// 00547321) -> 182/100/38, 610 instructions as retail's; the LOD queue written out in the loop instead of the earlier
// QueueLandscapeLOD helper (its parameter bound one load of t for the store and RequestTile) -> 157/87/37. Still all
// 30 x87 trees. The x loop's increments in retail's latch order (vert, below, span, tile, cam.X, then x beside its
// test, 00547236..0054724e) -> 143/83/33. The request through the texture's entry lookup and a request taking that
// entry, each given t, so t is read once for each as retail reads it twice (00547157, 0054718b) -> 103/41/37: t's fifth
// reference gives the frame packer retail's whole order (a slot-packer replay: t at weight 5 sorts tile, t,
// below, span as retail's 0x38..0x44), and every stack offset now agrees. Both reads come before the index here, where
// retail reads the second one after it. The queue's low-word view of t -> 95/37/33. The y loop's span computed before
// below (retail's span loads and below's store come in that order around the dy2 product, 00546dfc..00546e15; below's
// lea is scheduled first) -> 85/37/33; below still takes edx where retail's takes esi.
// The earlier, now-superseded FLandscapeOri column adapter gave 23/77 by score (29/83 before). LandscapeColumnY
// uses the actual FMatrix receiver and preserves that compiled result; this does not add a function match.
// Then: the x loop's for-init sets x, the row's first tile (y * 64) and below together, in that
// order, and t is row + x. At below's definition row is live in eax and x in edx, so below falls back to esi as
// retail's `lea esi,[ecx+0x41]` (00546df5) and the tile IV's load follows below's store: the whole y-loop head is
// retail's -> 15/69/582 (shape score; 23/77/623 before). A row computed before the loop is 23/79/627, and
// below alone in the for-init 21/75/620.
// Then: the request's stale flag is the whole urgent predicate, `(tile != old) && ((old & 0x7fff) ==
// tile)` (a suggested form), which gives retail's 16-bit `and ax,0x7fff` (00547197; see LandscapeTexture.h) ->
// 15/69/543 (the function one byte shorter; the register is still cx). Then: the far ring writes
// idx[3] = below - 1 before idx[5] = below (see AppendLandscapeFarCell), retail's `lea ebx,[eax-1]` -> 13/65/546.
// STAND-IN (admitted only with the match): x initialised before vert, the latch's reloads in retail's order (15/69/543
// -> 15/65/547 before the far ring).
// MATCH (whole function, byte check 61/61): t at function scope with the dead escape and
// the request as RequestTile(second read of t, TileEntry(t)) give the slot block and frame (27/191/1321 without the
// arms): the pinned VC6 evaluates this call's arguments right to left (observed for this call; C++ leaves the order
// unspecified), so the entry is formed before t is read again, retail's 0054718b. Then three self-copy arms (STAND-INs,
// LandscapeTexture.h: TileEntry's row and index, RequestTile's old) set the request's registers: the texture pointer
// edx, the masked tile edi, old ax, the second read ecx, the index eax.
// Without the row arm 0/50/518, without the index arm 0/6/16, without old's arm 0/30/509, without x's initialiser
// 0/4/21, without the dead escape 41/133/1332, without the volatile view 4/86/565, without both 2/34/486 (shape
// score, on the match). Every stand-in is listed with its plain form at its site.
void CDXLandscape::CalcLODs(C3DGamut *gamut, int viewpoint)	// FUNCTION: 0x00546b40
{
	SINT	t;
	SINT	maxlevels = LANDSCAPE_MAXLEVELS.mValue;
	if (LANDSCAPE_MAXLEVELS_USER.mValue > maxlevels)
		LANDSCAPE_MAXLEVELS_USER.Set((float)maxlevels);

	CLandscapeView	*view = &mViews[viewpoint];
	FVector	pos = view->mCamera->GetPos();
	FVector	dir = LandscapeColumnY(view->mCamera->GetOrientation());
	FVector	p = -pos;
	p.X += 4.0f;
	p.Y += 4.0f;
	SINT	patches = 0;
	if ((view->mLastPos.X == 1234567.0f) || (GAME.IsFreeCameraOn(viewpoint)))
	{
		view->mLastPos = p;
		view->mUnknown24 = dir;
	}
	else
	{
		SmoothLandscapeView(view, p, dir);
	}
	FVector	cam = view->mLastPos;
	FVector	look = view->mUnknown24;

	UWORD	*start;
	if (mIB->LockDynamic((void **)&start) >= 0)
	{
		UWORD	*idx = start;
		idx[1] = 0;
		idx[0] = 0;
		idx += 2;
		// STAND-IN (kept private, admitted only with a complete match): x is declared
		// here with an initialiser, before vert, though every use follows the x loop's for-init. Plain form: x declared
		// bare in the y loop. Effect: x's first definition precedes vert's, which numbers its web first, so the latch and
		// the far ring's entry reload vert before x as retail's (0054721c..00547220, 005472e8..005472ec; split pieces are
		// reloaded in descending web index, the DXTrees rule): 15/69/543 -> 15/65/547. Original: unknown. Replace it
		// when a real use of x (or a consumed definition) numbers it first. A bare declaration, x as the level loop's
		// counter, or vert defined after that loop do not.
		SINT	x = 0;
		SINT	vert = 1;
		SINT	l;
		for (l = 0; l < view->mNumLevels; l++)
			view->mLODs[l].mUnknown08 = 0;

		SLandscapeCell	*tile = view->mTiles;
		BOOL	joined = TRUE;
		for (SINT y = 0; y < 64; y++)
		{
			char	*span = gamut->mSpans + gamut->mGridSize * y * 2;
			float	dy2 = cam.Y * cam.Y;
			SINT	below;
			SINT	row;
			for (x = 0, row = y * 64, below = vert + 65; x < 64; vert++, below++, span += 2, tile++, cam.X += 8.0f, x++)
			{
				t = row + x;
				if ((x < gamut->mMinX) || (x > gamut->mMaxX) || (y < gamut->mMinY) || (y > gamut->mMaxY) ||
					(span[0] < tile->mMinHeight) || (span[1] > tile->mMaxHeight))
				{
					// out of view
					if (tile->mPatch)
					{
						RetireLandscapePatch(tile);
					}
					tile->mUnknown10 = (signed char)0x9d;
					if (!joined)
					{
						CloseLandscapeStrip(idx, joined, vert, 1);
					}
					continue;
				}

				float	dz = cam.Z + tile->mMidHeight;
				float	dz2 = dz * dz;
				SINT	minlod = LANDSCAPE_MINLOD_AT.mValue;
				SINT	near8 = minlod >> 3;
				near8 = near8 * near8;
				float	dist2 = cam.X * cam.X + dz2 + dy2;
				BOOL	inFarRing = FALSE;
				float	lod;
				if (near8 > dist2)
					lod = 8.0f;
				else
				{
					float	complexity = LANDSCAPE_LOWRES_GEOM.mValue ? 0.0f : tile->mComplexity;
					lod = (1.0f / (float)sqrt(dist2)) * (minlod + complexity);
					if (lod < 2.0f)
						inFarRing = TRUE;
				}
				if (!inFarRing)
				{
					if (!joined)
					{
						CloseLandscapeStrip(idx, joined, vert, 1);
					}

					// a patch at the tile's level
					SINT	level = (SINT)lod;
					if (level > 8)
						level = 8;
					level = sPatchLevel[level];
					SINT	slot = (viewpoint << 12) + t;
					if ((tile->mUnknown10 != level) || (tile->mPatch->mSlot != slot))
					{
						if (tile->mPatch)
						{
							RetireLandscapePatch(tile);
							tile->mUnknown10 = (signed char)0x9d;
						}
						CLandscapeVB	*patch;
						for (;;)
						{
							if (AllocatePatch(level, slot, patch))
								break;
							if (--level < 0)
							{
								inFarRing = TRUE;
								break;
							}
						}
						if (!inFarRing)
						{
							tile->mPatch = (SLandscapePatch *)patch;
							tile->mUnknown10 = level;
							patch->SetOrigin(x << 3, y << 3, 4 >> level, tile->mComplexity);
							patches++;
						}
					}
					if (!inFarRing)
					{

						// its texture level, by distance ahead of the view, and the tile listed at that level
						SINT	tex = ChooseLandscapeTexture(view, cam, look, dist2, dz2);
						SLandscapeLOD	*lod2 = &view->mLODs[tex];
						lod2->mData[lod2->mUnknown08] = t;	// t's low word (`mov bx,WORD PTR`, 00547130), as the escaped t gives it
						lod2->mUnknown08++;
						if (tex)
						{
							// STAND-IN: the request's tile is read through a volatile view of t. Plain form (`RequestTile(t, ...)`): t's
							// value from the entry's read is reused (4/86/565 on the match, 23/155/1873 at the step that added the view).
							// Unlike the other stand-ins this one emits code, the native second load of t from memory (0054718b); it is
							// also t's fifth reference, which sorts the frame as retail's. Why retail reads t a second time is unknown;
							// replace it when that cause is found.
							view->mTextures[tex - 1].RequestTile(*(volatile SINT *)&t, view->mTextures[tex - 1].TileEntry(t));
						}

						// stitch its edges to coarser neighbours
						StitchLandscapeCell(tile, x, y);
						continue;
					}
				}

				// in the far ring: strung into its strip
				AppendLandscapeFarCell(idx, joined, vert, below);
				tile->mUnknown10 = -1;
				if (tile->mPatch)
				{
					RetireLandscapePatch(tile);
					tile->mUnknown10 = (signed char)0x9d;
				}
			}
			if (!joined)
			{
				CloseLandscapeStrip(idx, joined, vert, 2);
			}
			vert++;
			cam.Y += 8.0f;
			cam.X -= 512.0f;
		}
		// STAND-IN (see above): a dead call taking t's address. It never runs and emits no code; it keeps t in memory.
		if (0) ToWCHAR((char *)&t);
		mUnknown34 = idx - start;
		mIB->Unlock();
	}
	CLandscapeTexture::FlushUpdateQueue();
}

//******************************************************************************************
// Marks a tile stale in every view's mip levels. The tile arrives as a SINT: AddDamage sign-extends its SWORD cell into
// it (movsx at 0054774f) and UpdateArea zero-extends its UWORD tile once per column (and esi,0xffff at 005474cc).
inline void CDXLandscape::InvalidateTile(SINT tile)
{
	if (mNumViews)
	{
		for (SINT v = 0; v < mNumViews; v++)
		{
			for (SINT l = 1; l < mViews[v].mNumLevels; l++)
				mViews[v].mTextures[l - 1].InvalidateTile(tile);
		}
	}
}

//******************************************************************************************
// Marks the tiles over an area stale in every view, or rebuilds the geometry.
// The geometry loop reads each cell's patch once into its own variable (retail keeps it in ecx across the stores, which
// gives the counter edx and the byte constant bl), and the four coordinates are shifted in the order x0, y0, x1, y1.
void CDXLandscape::UpdateArea(SINT x0, SINT y0, SINT x1, SINT y1, BOOL geometry)	// FUNCTION: 0x005473b0
{
	CLandscapeView	*views = mViews;
	if (geometry)
	{
		BuildVertexBuffer();
		for (SINT l = 0; l < 3; l++)
			PATCHMANAGER.mLists[l].Reset();

		SLandscapeCell	*cell = views->mTiles;
		for (SINT i = 0; i < 64 * 64; i++)
		{
			SLandscapePatch	*patch = cell->mPatch;
			if (patch)
			{
				patch->mSlot = 0xffff;
				cell->mPatch = NULL;
				cell->mUnknown10 = 0x9d;
			}
			cell++;
		}
		return;
	}

	SINT	w = x1 - x0;
	SINT	h = y1 - y0;
	x0 = (x0 - 1) >> 3;
	y0 = (y0 - 1) >> 3;
	x1 >>= 3;
	y1 >>= 3;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x0 > 63)
		x0 = 63;
	if (y0 > 63)
		y0 = 63;
	if (x1 > 63)
		x1 = 63;
	if (y1 > 63)
		y1 = 63;

	for (SINT y = y0; y <= y1; y++)
	{
		for (SINT x = x0; x <= x1; x++)
		{
			UWORD	tile = (UWORD)((y << 6) + x);
			InvalidateTile(tile);
		}
	}

	if ((w >= 2) && (h >= 2))
		mTexture->UpdateTileRange(x0, y0, x1, y1);
}

//******************************************************************************************
// Stamps a damage mark (a square of 2^|size| sixteenths of a metre) into the damage map, or removes one, and marks
// the tiles it touches stale. The corner (fx, fy) in sixteenths gives the first tile (cx, cy) and the offsets in it, and
// the mark covers one or two tiles each way. Retail rereads size from its argument (00547716) and keeps oy in edi,
// pushed only after the early exits (005476a9); a parameter takes the last callee-saved register when oy does not
// want it, so the y offset is its own variable from the start (it is also what the row test reads, 005476d8), the row
// is counted in cy itself, the corner values are short-lived and the row loop is bottom-tested (an entry test would be
// an exit after edi's first use).
// The drag deltas are built in place (dx = x; dx -= last_x;), which gives retail's InvalidateTile sum order
// ([edi+edx], 005477a8..005477b1) (a measured spelling).
// cy and px start as copies of the corner and are shifted and masked in place, and each row restarts its column from
// cx. cy, px and cx are then first referenced in that order, and the column is the named cx. That gives retail's
// row-end reloads (px, cy, column, nx at 00547827..00547833) and, through them, its spill slots (px lower, 0054782b).
// The allocator's rebuilds re-create spilled values in ascending node index from a LIFO free list (each later rebuild
// that holds two of them reverses their order). So the reloads keep the order the values got in the first rebuild
// that split them, which pops the free list in their symbol-number order. Restarting each row from fx >> 7 made the
// column a CSE temporary, numbered after every named local (nx came before the column); a cx referenced before cy and
// px put the column first.
void CDXLandscape::AddDamage(float x, float y, int size)	// FUNCTION: 0x005475d0
{
	static float	last_x = -1.0f;			// 00650ab0 (retail .data: 0xbf800000)
	static float	last_y = -1.0f;			// 00650ab4 (retail .data: 0xbf800000)

	float	dx = x;
	dx -= last_x;
	float	dy = y;
	dy -= last_y;
	if (dx * dx + dy * dy < 0.1f * 0.1f)
		return;
	last_x = x;
	last_y = y;

	SINT	s = size;
	if (s < 0)
		s = -s;
	SINT	d = 1 << s;
	SINT	half = d >> 1;
	SINT	fx = (SINT)floor(x * 16.0f) - half;
	SINT	fy = (SINT)floor(y * 16.0f) - half;
	SINT	cy = fy, px = fx;
	SINT	cx = px >> 7;
	cy >>= 7;
	if ((cx < 0) || (cy < 0) || (cx >= 64) || (cy >= 64))
		return;

	px &= 0x7f;
	SINT	oy = fy & 0x7f;
	SINT	nx = 1;
	if ((px + d > 128) && (cx < 63))
		nx = 2;
	SINT	ny = 1;
	if ((oy + d > 128) && (cy < 63))
		ny = 2;
	SINT	j = ny;
	do
	{
		SINT	ox = px;
		SINT	tx = cx;
		for (SINT i = nx; i; i--)
		{
			SWORD	cell = (SWORD)(cy * 64 + tx);
			if (size > 0)
				mDamage.Add(cell, (UBYTE)ox, (UBYTE)oy, (UBYTE)size);
			else
				mDamage.Remove(cell, (UBYTE)ox, (UBYTE)oy);

			if (ENGINE.GetLandscape()->mTexture)
				ENGINE.GetLandscape()->InvalidateTile(cell);
			ox -= 128;
			tx++;
		}
		cy++;
		oy -= 128;
		j--;
	} while (j);
}

//******************************************************************************************
// The console's BuildLandscapeCache: palettises every tile at three mip shifts into the PS2's texture cache.
// The line counter is zeroed before the mip's table and area (005478fa: xor esi,esi beside the first mov), and the
// line's texel row is named after the trace: retail advances the counter at the sprintf and keeps it in esi.
void CDXLandscape::BuildTextureCache()	// FUNCTION: 0x00547860
{
	TRACE("Building texture cache...\n");

	CDXPalletizer	pal;
	CMapTex			*maptexs = ENGINE.GetMapTex();
	char			name[256];
	sprintf(name, "ps2data\\LandscapeTextureCache\\texcache.tex");
	FILE			*tex = fopen(name, "wb");
	FILE			*dir = fopen("ps2data\\LandscapeTextureCache\\texcache.dir", "wb");

	DWORD			texels[128 * 128];
	SINT			size;
	for (SINT shift = 2; shift <= 4; shift++)
	{
		if (shift == 4)
			size = 128;
		else if (shift == 3)
			size = 64;
		else if (shift == 2)
			size = 32;

		SINT	line = 0;
		CMapTex	*mt = &maptexs[4 - shift];
		SINT	area = size * size;
		for (; line < 64; line++)
		{
			char	buf[256];
			sprintf(buf, "Shift %d/4 Line %d/64\n", shift, line + 1);
			TRACE(buf);
			SINT	y = line * 8;
			for (SINT x = 0; x < 512; x += 8)
			{
				MAP.GetCacheTexels(shift, mt, texels, y, x, 0, 0, 8, 8, size);

				UBYTE		*pixels;
				CPalColour	*palette;
				pal.Palletize((UBYTE *)texels, size, size, 256, &pixels, &palette, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE);
				SINT	offset = ftell(tex);
				fwrite(&offset, 4, 1, dir);
				fwrite(pixels, area, 1, tex);
				fwrite(palette, 256, 4, tex);
				delete pixels;
				delete palette;
			}
		}
	}

	fclose(tex);
	fclose(dir);
	TRACE("...done!\n");
}

//******************************************************************************************
// How far a tile's heights stray from flat at the three levels of detail below the finest: the mean error of the
// midpoints, the worst of the three, in metres.
// The far corners are two halves away (half * 2), not step: retail builds their offsets from half (half * 18 and
// half * 40 at 00547ad0..00547b31, 2 * half at 00547ccc), which VC6 can't derive from step = 1 << l.
float CDXLandscape::TileComplexity(SINT tile)	// FUNCTION: 0x00547a60
{
	SINT	n;
	SWORD	*h = &MAP.mHeightData[(((tile & 63) << 6) + (tile >> 6)) * 81];
	SINT	worst = 0;
	n = 4;
	for (SINT l = 1; l <= 3; l++)
	{
		SINT	total = 0;
		SINT	step = 1 << l;
		SINT	half = step >> 1;
		SINT	x, y;
		for (y = 0; y < 8; y += step)
		{
			for (x = 0; x < 8; x += step)
			{
				total += abs((h[(y + half * 2) * 9 + x + half * 2] + h[y * 9 + x]) / 2 - h[(y + half) * 9 + x + half]) +
						 abs((h[(y + half * 2) * 9 + x] + h[y * 9 + x]) / 2 - h[(y + half) * 9 + x]) +
						 abs((h[y * 9 + x + half * 2] + h[y * 9 + x]) / 2 - h[y * 9 + x + half]);
			}
			total += abs((h[(y + half * 2) * 9 + x] + h[y * 9 + x]) / 2 - h[(y + half) * 9 + x]);
		}
		total += abs((h[y * 9 + x] + h[y * 9 + x + half * 2]) / 2 - h[y * 9 + x + half]);

		SINT	mean = total / (n * (n * 3 + 2));
		if (mean > worst)
			worst = mean;
		n >>= 1;
	}
	return MAP.GetHeightScale() * worst;
}
// STAND-IN (link layout): retail's 32 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_DXLandscape[32] = { 0 };
