// DXShadows.cpp: reconstructed from DXShadows.obj (00551d70..00554e5f); Stuart's release has no copy.
// The nearest units each get a shadow texture rendered from the light (a pool of 16, 32 in high detail) that
// fades in and out as units come and go; the landscape is then drawn again under each with the texture. Other
// things get a blob shadow, batched into one vertex buffer.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include	"Common.h"
class CShadows;class DeviceObject;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include	"shadows.h"
#include	"UMTexture.h"
#include	"Texture.h"
#include	"VBufTexture.h"
#include	"ltshell.h"
#include	"console.h"
#include	"CLIParams.h"
#include	"engine.h"
#include	"Camera.h"
#include	"map.h"
#include	"thing.h"
#include	"unit.h"
#include	"tweak.h"
#include	"DebugLog.h"
#include	"renderinfo.h"
#include	"state.h"
#include	"PauseMenu.h"
#include	"Platform.h"
#include	"FastVB.h"
#include	"mathsconst.h"	// FUNCTION: 0x00551d70 init:ZERO_FVECTOR
#include	"meshrenderer.h"
						// FUNCTION: 0x00551d90 init:ID_FMATRIX
CTweakSLONG		DXSHADOWS_INVERSE("DXSHADOWS_INVERSE", 0);		// 009c7c80: render with a depth surface
CTweakSLONG		SRT_ENABLE("SRT_ENABLE", 1);					// 009c7c70: shadow textures allowed
CRenderMethod	SHADOW_METHOD("shadow_method", 4);				// 009c7c58
// FUNCTION: 0x00551e60 init:DXSHADOWS_INVERSE
// FUNCTION: 0x00551e80 exit:DXSHADOWS_INVERSE
// FUNCTION: 0x00551e90 init:SRT_ENABLE
// FUNCTION: 0x00551eb0 exit:SRT_ENABLE
// FUNCTION: 0x00551ec0 init:SHADOW_METHOD
// FUNCTION: 0x00551ee0 exit:SHADOW_METHOD

CShadows		SHADOWS;
// FUNCTION: 0x00551ef0 init:SHADOWS
// FUNCTION: 0x00551f10 exit:SHADOWS

static SINT		sFadeStep = 1;			// 00652230: fade steps per frame (RenderTextures sets it from the frame time)
// Sixteen opaque colours (red, green, blue, yellow, magenta, cyan, then paler and darker ones): retail's object defines
// them right after sFadeStep (00652234) and nothing in the executable reads them. The name is ours.
DWORD	SHADOW_DEBUG_COLOURS[16] =							// 00652234 (name ours)
{
	0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffff00,
	0xffff00ff, 0xff00ffff, 0xff8000ff, 0xff8080ff,
	0xff80ff80, 0xffff8080, 0xffff80ff, 0xffffff80,
	0xff08ffff, 0xff804000, 0xff408000, 0xff400080,
};
#line 46
extern D3DMATRIX	gTextureMatrix;		// 00628258 (D3DUtils.cpp): identity at first; DXLandscape's passes use it too

//******************************************************************************************
CShadows::CShadows()	// FUNCTION: 0x00551f20
{
	mShadows = TRUE;
	mBlobShadows = TRUE;
}

// FUNCTION: 0x00551fb0 ??_GCShadows@@UAEPAXI@Z
// FUNCTION: 0x00551fd0 ??0SShadowRequest@@QAE@XZ

//******************************************************************************************
CShadows::~CShadows()	// FUNCTION: 0x00551fe0
{
}

//******************************************************************************************
void CShadows::ShutDown()	// FUNCTION: 0x00552060
{
	for (SINT i = 0; i < mNumTextures; i++)
		SAFE_DELETE(mTextures[i]);

	if (mBlobTexture)
	{
		mBlobTexture->Release();
		mBlobTexture = NULL;
	}
	if (mBlobVB)
	{
		mBlobVB->Release();
		mBlobVB = NULL;
	}

	InvalidateDeviceObjects();
	LT.RemoveDeviceObject(this);
}

//******************************************************************************************
void CShadows::Init()	// FUNCTION: 0x005520f0
{
	LT.AddDeviceObject(this);

	if (CLIPARAMS.mHiDetailMode)
		mNumTextures = 32;
	else
		mNumTextures = 16;

	SINT	i;
	for (i = 0; i < mNumTextures; i++)
		mSizes[i] = 64;

	for (i = 0; i < mNumTextures; i++)
	{
#line 105
		mTextures[i] = new(MEMTYPE_TEXTURE) CUMTexture;
		HRESULT	hr = mTextures[i]->Init(mSizes[i], 0, TRUE);
		if (FAILED(hr))
		{
			SETUP_HISTORY.AddMessage("Shadow texture creation failed for %s", DXGetErrorString9(hr));
			for (; i >= 0; i--)
				SAFE_DELETE(mTextures[i]);
			break;
		}
	}

	ResetShads();

	mShowShadowMap = FALSE;
	mShowShadowExtents = FALSE;
	mBlobShadowFadeDist = 30.0f;
	CONSOLE.RegisterVariable("cg_ShowShadowMap", "Enables the display of the shadow map contents", CVar_bool, &mShowShadowMap);
	CONSOLE.RegisterVariable("cg_ShowShadowExtents", "Enables the display of the shadow extents", CVar_bool, &mShowShadowExtents);
	CONSOLE.RegisterVariable("cg_Shadows", "Enables or disables shadows", CVar_bool, &mShadows);
	CONSOLE.RegisterVariable("cg_BlobShadows", "Enables or disables blob shadows", CVar_bool, &mBlobShadows);
	CONSOLE.RegisterVariable("cg_BlobShadowFadeDist", "The distance at which blob shadows fade out", CVar_float, &mBlobShadowFadeDist);

	RestoreDeviceObjects();
}

//******************************************************************************************
void CShadows::InitResources()	// FUNCTION: 0x00552330
{
	SINT	oldmode = LT.m_bTextureCompressionDisabled;
	LT.m_bTextureCompressionDisabled = 1;
	mBlobTexture = CDXTexture::GetTextureByName("shadowblob.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1, 1, 1);
	LT.m_bTextureCompressionDisabled = oldmode;

#line 151
	mBlobVB = new(MEMTYPE_VBUFTEXTURE) CVBufTexture(mBlobTexture);
	mBlobVB->SetVBFormat(FVF_GTEX_FLAGS, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, sizeof(CFVF_GTEX), D3DPT_TRIANGLELIST, D3DPOOL_DEFAULT);
	mBlobVB->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 2, D3DPOOL_DEFAULT);
}

//******************************************************************************************
HRESULT CShadows::RestoreDeviceObjects()	// FUNCTION: 0x00552410
{
	if (!SRT_ENABLE.mValue)
	{
		mShadows = FALSE;
		mBlobShadows = TRUE;
	}

	SAFE_RELEASE(mDepthSurface);

	if (DXSHADOWS_INVERSE.mValue)
		LT.m_pd3dDevice->CreateDepthStencilSurface(64, 64, D3DFMT_D16, D3DMULTISAMPLE_NONE, 0, TRUE, &mDepthSurface, NULL);

	return S_OK;
}

//******************************************************************************************
HRESULT CShadows::InvalidateDeviceObjects()	// FUNCTION: 0x00552470
{
	SAFE_RELEASE(mDepthSurface);
	return S_OK;
}

//******************************************************************************************
// The light's direction; a shadow falls (mShadowDX, mShadowDY) along the ground per unit of height, at most 2.
void CShadows::MakeView(FVector light)	// FUNCTION: 0x005524a0
{
	if (gMirrorPass)
		return;

	mShadowDX = -(light.X / light.Z);
	mShadowDY = -(light.Y / light.Z);
	float	m = (float)sqrt(mShadowDY * mShadowDY + mShadowDX * mShadowDX);
	if (m > 2.0f)
	{
		mShadowDX = 2.0f / m * mShadowDX;
		mShadowDY = 2.0f / m * mShadowDY;
	}

	mShadowMax = (float)fabs(mShadowDX);
	float	ay = (float)fabs(mShadowDY);
	if (ay > mShadowMax)
		mShadowMax = ay;

	light.Normalise();

	// The scalar matrix stores interleave with the light copy; retail writes _42 before _41 (00552625).
	D3DMATRIX	m2;
	m2._11 = 100.0f; m2._12 = 0; m2._13 = 0; m2._14 = 0;
	m2._21 = 0; m2._22 = 100.0f; m2._23 = 0; m2._24 = 0;
	m2._31 = mShadowDX * 100.0f; m2._32 = mShadowDY * 100.0f; m2._33 = 1.0f; m2._34 = 0;
	m2._42 = 0; m2._41 = 0; m2._43 = 0; m2._44 = 100.0f;
	mLight = light;
	mProjection = m2;
}

//******************************************************************************************
void CShadows::ResetShads()	// FUNCTION: 0x00552660
{
	if (gMirrorPass)
		return;

	if (mShadows == TRUE)
	{
		for (SINT i = 0; i < mNumTextures; i++)
		{
			mRequests[i].mUnit.SetReader(NULL);
			mRequests[i].mDist = 16384.0f;
		}
	}
	else
		mRequests[mNumTextures - 1].mDist = -1.0f;
}

//******************************************************************************************
// Keeps the nearest units: an insertion sort on distance.
void CShadows::AddShadow(CUnit *unit, float dist)	// FUNCTION: 0x005526c0
{
	if (gMirrorPass)
		return;

	SINT	i;
	for (i = 0; i < mNumTextures; i++)
	{
		if (mRequests[i].mDist > dist)
			break;
	}

	for (SINT j = mNumTextures - 1; j > i; j--)
	{
		mRequests[j].mDist = mRequests[j - 1].mDist;
		mRequests[j].mUnit.SetReader(mRequests[j - 1].mUnit.ToRead());
	}

	mRequests[i].mDist = dist;
	mRequests[i].mUnit.SetReader(unit);
}

//******************************************************************************************
// Fades the textures in and out, and lists the free ones.
void CShadows::UpdateTextures()	// FUNCTION: 0x00552740
{
	if (gMirrorPass)
		return;

	SShadowTexInfo	*t = mTexInfo;
	SShadowTexInfo	**f = mFree;
	for (SINT n = mNumTextures; n; n--)
	{
		if (t->mUnit.ToRead() == NULL)
			*f++ = t;
		else
		{
			t->mUnused = TRUE;
			switch (t->mState)
			{
			case 0:
				t->mFade = 32;
				break;
			case 1:
				t->mFade -= 2;
				if (t->mFade < 2)
				{
					t->mUnit.SetReader(NULL);
					*f++ = t;
				}
				break;
			case 2:
				{
					SINT	fade = t->mFade + sFadeStep * 2;
					if (fade >= 32)
					{
						t->mFade = 32;
						t->mState = 0;
					}
					else
						t->mFade = fade;
				}
				break;
			}
		}
		t++;
	}
	mNumFree = f - mFree;
}

//******************************************************************************************
// Gives each requesting unit a texture: its own if it has one, else a free one (fading in).
void CShadows::AssignTextures()	// FUNCTION: 0x00552800
{
	if (gMirrorPass)
		return;

	SShadowRequest	*r = mRequests;
	SShadowTexInfo	**f = mFree;
	for (SINT n = mNumTextures; n; n--)
	{
		CUnit	*u = r->mUnit.ToRead();
		if (u)
		{
			SShadowTexInfo	*t = mTexInfo;
			BOOL			found = FALSE;
			for (SINT c = mNumTextures; c; c--)
			{
				if (t->mUnit == u)
				{
					t->mUnused = FALSE;
					if (t->mState == 1)
						t->mState = 2;
					found = TRUE;
					break;
				}
				t++;
			}

			if ((!found) && (mNumFree > 0))
			{
				t = *f;
				t->mFade = 2;
				t->mState = 2;
				t->mUnused = FALSE;
				t->mUnit.SetReader(r->mUnit.ToRead());
				t->mUnknown08 = 0;
				f++;
				mNumFree--;
			}
		}
		r++;
	}
}

//******************************************************************************************
// A shadow texture's border, drawn as one strip (the texture clamps to it).
struct SShadowVertex
{
	D3DXVECTOR4	mPos;
	DWORD		mColour;
	float		mU, mV;
};

// D3D 8's SetRenderTarget(target, depth) in two D3D 9 calls; the result is the first failure, or the target's.
static inline HRESULT SetTargets(IDirect3DSurface9 *target, IDirect3DSurface9 *depth)
{
	LPDIRECT3DDEVICE9	dev = LT.m_pd3dDevice;
	HRESULT	depthResult = dev->SetDepthStencilSurface(depth);
	HRESULT	result;
	if (FAILED(depthResult))
	{
		dev->SetRenderTarget(0, target);
		result = depthResult;
	}
	else
		result = dev->SetRenderTarget(0, target);
	return result;
}

static float	sLastFadeTime = 0.0f;			// 009c7cd0

// Puts the saved matrices back and marks them changed.
static inline void RestoreMatrices(const SRenderMatrix &view, const SRenderMatrix &projection, const SRenderMatrix &world)
{
	RENDERINFO.mView = view;
	RENDERINFO.mViewChanged = true;
	RENDERINFO.mProjection = projection;
	RENDERINFO.mProjectionChanged = true;
	RENDERINFO.mWorld = world;
	RENDERINFO.mWorldChanged = true;
}

//******************************************************************************************
// Renders each assigned unit, flat, into its shadow texture from the light, then restores the targets and state.
// Size the assigned shadow texture and set its camera from the caster's world bounds.
static inline void PrepareShadowCamera(CShadows *shadows, const FVector &centre, const CBoundingBox *box, SShadowTexInfo *info)
{
	float	r = box->mAxes.X;
	if (r < box->mAxes.Y)
		r = box->mAxes.Y;
	float	size = shadows->mShadowMax * box->mAxes.Z + r;
	info->mRadius = size;
	if (!gMirrorPass)
	{
		RENDERINFO.mProjection = *(SRenderMatrix *)&SHADOWS.mProjection;
		RENDERINFO.mProjectionChanged = true;
		FMatrix	view(FVector(size, 0.f, 0.f), FVector(0.f, size, 0.f), FVector(0.f, 0.f, size));
		FVector	eye = centre;
		eye.Z = centre.Z - size;
		eye.X += SHADOWS.mShadowDX * size;
		eye.Y += SHADOWS.mShadowDY * size;
		RENDERINFO.SetView(&view, &eye);
	}
}

// STAND-IN: 160 unused locals, for RenderTextures' symbol ids only; see the comment there. Ours.
static __forceinline void IdPadding()
{
	SINT	p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0;
	SINT	p10 = 0, p11 = 0, p12 = 0, p13 = 0, p14 = 0, p15 = 0, p16 = 0, p17 = 0, p18 = 0, p19 = 0;
	SINT	p20 = 0, p21 = 0, p22 = 0, p23 = 0, p24 = 0, p25 = 0, p26 = 0, p27 = 0, p28 = 0, p29 = 0;
	SINT	p30 = 0, p31 = 0, p32 = 0, p33 = 0, p34 = 0, p35 = 0, p36 = 0, p37 = 0, p38 = 0, p39 = 0;
	SINT	p40 = 0, p41 = 0, p42 = 0, p43 = 0, p44 = 0, p45 = 0, p46 = 0, p47 = 0, p48 = 0, p49 = 0;
	SINT	p50 = 0, p51 = 0, p52 = 0, p53 = 0, p54 = 0, p55 = 0, p56 = 0, p57 = 0, p58 = 0, p59 = 0;
	SINT	p60 = 0, p61 = 0, p62 = 0, p63 = 0, p64 = 0, p65 = 0, p66 = 0, p67 = 0, p68 = 0, p69 = 0;
	SINT	p70 = 0, p71 = 0, p72 = 0, p73 = 0, p74 = 0, p75 = 0, p76 = 0, p77 = 0, p78 = 0, p79 = 0;
	SINT	p80 = 0, p81 = 0, p82 = 0, p83 = 0, p84 = 0, p85 = 0, p86 = 0, p87 = 0, p88 = 0, p89 = 0;
	SINT	p90 = 0, p91 = 0, p92 = 0, p93 = 0, p94 = 0, p95 = 0, p96 = 0, p97 = 0, p98 = 0, p99 = 0;
	SINT	p100 = 0, p101 = 0, p102 = 0, p103 = 0, p104 = 0, p105 = 0, p106 = 0, p107 = 0, p108 = 0, p109 = 0;
	SINT	p110 = 0, p111 = 0, p112 = 0, p113 = 0, p114 = 0, p115 = 0, p116 = 0, p117 = 0, p118 = 0, p119 = 0;
	SINT	p120 = 0, p121 = 0, p122 = 0, p123 = 0, p124 = 0, p125 = 0, p126 = 0, p127 = 0, p128 = 0, p129 = 0;
	SINT	p130 = 0, p131 = 0, p132 = 0, p133 = 0, p134 = 0, p135 = 0, p136 = 0, p137 = 0, p138 = 0, p139 = 0;
	SINT	p140 = 0, p141 = 0, p142 = 0, p143 = 0, p144 = 0, p145 = 0, p146 = 0, p147 = 0, p148 = 0, p149 = 0;
	SINT	p150 = 0, p151 = 0, p152 = 0, p153 = 0, p154 = 0, p155 = 0, p156 = 0, p157 = 0, p158 = 0, p159 = 0;
}

// STAND-IN: RenderTextures ends with a call to IdPadding, a __forceinline helper of 160 unused locals
// (name and body ours). It emits no code: the dead stores are removed and a forced inline spends none of the inline budget.
// What it does is take symbol ids. C2 numbers its optimizer temporaries after every symbol block, so the 160 symbols
// (five 32-id blocks) move the two 10-vertex loops' induction temporaries from t79d/t7b7 to about t83d/t857 (measured
// 32 ids per 32 locals up to 96, t7fd/t817; the IR dump fails beyond 98 locals). VC6 orders the
// operands of each `[v + t]` store by rank key, a temporary's key being its id << 6 in 16 bits, and only past 0x800 does
// the vertex pointer outrank it: retail's `[ecx+eax-0xc]`, base v first (00552eb1..). The fade's Magnitude products wrap
// with them and keep retail's order at 160 (at 128 its X * X goes last; 96 fixes the second loop only).
// Plain form (no padding): 12 instructions, 8 bytes. Retail's ids say Stuart's text had about 100 more symbols or
// temporaries before these loops; which is unknown. The natural steps are kept: the restores as one inline
// (RestoreMatrices, which puts `SHADOWS.mShadowDX * size` in retail's order) and Stuart's GetView / GetProjection /
// GetWorld for the saves (+21 ids). Dead locals in the body itself break the code at 48 (they raise the inline budget),
// and the header's D3DMATRIX * overloads reach +80 ids but flip the products back.
void CShadows::RenderTextures()	// FUNCTION: 0x005528b0
{
	float	now = PLATFORM.GetSysTimeFloat();
	sFadeStep = 1;
	if (now > sLastFadeTime)
	{
		float	dt = now - sLastFadeTime;
		if (dt < 2.f)
		{
			dt *= 20.f;
			if (!(dt < 2.f))
				sFadeStep = (UWORD)dt;
		}
	}
	sLastFadeTime = now;

	if (gMirrorPass)
		return;
	if (!mShadows)
		return;

	FASTVB.Create();
	CMeshRenderer::SetRenderMode((ERenderMode)4);
	UpdateTextures();
	AssignTextures();

	// textures nobody asked for fade out
	if (!gMirrorPass)
	{
		SShadowTexInfo	*t = mTexInfo;
		for (SINT n = mNumTextures; n; n--, t++)
		{
			if (t->mUnused == 1)
			{
				t->mState = 1;
				t->mFade -= 2;
			}
		}
	}

	if (!DXSHADOWS_INVERSE.mValue)
	{
		LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		LT.SRS(D3DRS_ZENABLE, FALSE);
	}
	for (SINT l = 0; l < 8; l++)
		RENDERINFO.SetLightEnable(l, false);
	if (DXSHADOWS_INVERSE.mValue)
		RENDERINFO.SetAmbient(0xffffffff);
	else
		RENDERINFO.SetAmbient(0);
	RENDERINFO.EnableLighting(true);

	SRenderMatrix	oldView, oldProjection, oldWorld;
	RENDERINFO.GetView((D3DMATRIX *)&oldView);
	RENDERINFO.GetProjection((D3DMATRIX *)&oldProjection);
	RENDERINFO.GetWorld((D3DMATRIX *)&oldWorld);

	IDirect3DSurface9	*target, *depth;
	if (FAILED(LT.GetDevice()->GetRenderTarget(0, &target)))
	{
		mShadows = FALSE;
		mBlobShadows = TRUE;
		SetSetupMessage(0xe7);
		return;
	}
	if (FAILED(LT.GetDevice()->GetDepthStencilSurface(&depth)))
	{
		mShadows = FALSE;
		mBlobShadows = TRUE;
		SetSetupMessage(0xe7);
		return;
	}

	D3DXMATRIX	id(1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f);
	RENDERINFO.mWorld = *(SRenderMatrix *)&id;
	RENDERINFO.mWorldChanged = true;
	LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);

	float	water = MAP.GetWaterLevel();
	FVector	campos = ENGINE.GetCamera()->GetPos();

	SShadowTexInfo	*info = mTexInfo;
	for (SINT i = 0; i < mNumTextures; info++, i++)
	{
		if (!mTextures[i])
		{
			mShadows = FALSE;
			SetSetupMessage(0xe7);
			return;
		}

		CUnit	*unit = info->mUnit.ToRead();
		if (!unit)
			continue;

		// the shadow falls from the unit's box along the light, onto the ground or the water
		FVector			pos = unit->GetRenderPos();
		CBoundingBox	*box = unit->GetBoundingBox();
		float	ground = MAP.Collide(pos);
		if (ground > water)
			ground = water;
		float	h = pos.Z - ground;
		FVector	sp = pos;
		if (h < 0.f)
		{
			sp.X = pos.X + h * mShadowDX;
			sp.Y = sp.Y + h * mShadowDY;
		}
		sp.X = sp.X - mShadowDX * box->mAxes.Z;
		sp.Y = sp.Y - mShadowDY * box->mAxes.Z;
		info->mPos = sp;

		FVector	distance(sp.X, sp.Y, ground);
		distance -= campos;
		SINT	fade = 128 - SINT(distance.Magnitude() - h * 0.5f);
		if (fade < 0)
			fade = 0;
		else if (fade > 128)
			fade = 128;
		info->mAlpha = (info->mFade * fade) >> 5;

		if (!info->mUnknown08)
		{
			SINT	shift = SHADOW_METHOD.GetMethod() - 1;
			DWORD	clear;
			if (DXSHADOWS_INVERSE.mValue)
				clear = 0;
			else
			{
				SINT	g = (0xff - (UBYTE)info->mAlpha) >> shift;
				clear = 0xff000000 + g * 0x10101;
			}

			IDirect3DSurface9	*surface;
			if (SUCCEEDED(mTextures[i]->mTexture->GetSurfaceLevel(0, &surface)))
			{
				HRESULT	hr;
				if (DXSHADOWS_INVERSE.mValue)
					hr = SetTargets(surface, mDepthSurface);
				else
					hr = SetTargets(surface, NULL);
				surface->Release();

				if (SUCCEEDED(hr))
				{
					LT.GetDevice()->BeginScene();
					if (DXSHADOWS_INVERSE.mValue)
						hr = LT.GetDevice()->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear, 1.f, 0);
					else
						hr = LT.GetDevice()->Clear(0, NULL, D3DCLEAR_TARGET, clear, 1.f, 0);
					if (SUCCEEDED(hr))
					{
						if (DXSHADOWS_INVERSE.mValue)
						{
							SShadowVertex	*v;
							if (FASTVB.LockAligned((void **)&v, 10) != 0xffff)
							{
								LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
								LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
								for (SINT k = 0; k < 10; k++)
								{
									v[k].mColour = 0;
									v[k].mU = 0;
									v[k].mV = 0;
								}
								v[0].mPos = D3DXVECTOR4(0.f, 0.f, 0.f, 1.f);
								v[1].mPos = D3DXVECTOR4(2.f, 2.f, 0.f, 1.f);
								v[2].mPos = D3DXVECTOR4(64.f, 0.f, 0.f, 1.f);
								v[3].mPos = D3DXVECTOR4(62.f, 2.f, 0.f, 1.f);
								v[4].mPos = D3DXVECTOR4(64.f, 64.f, 0.f, 1.f);
								v[5].mPos = D3DXVECTOR4(62.f, 62.f, 0.f, 1.f);
								v[6].mPos = D3DXVECTOR4(0.f, 64.f, 0.f, 1.f);
								v[7].mPos = D3DXVECTOR4(2.f, 62.f, 0.f, 1.f);
								v[8].mPos = D3DXVECTOR4(0.f, 0.f, 0.f, 1.f);
								v[9].mPos = D3DXVECTOR4(2.f, 2.f, 0.f, 1.f);
								LT.D3D_SetTexture(0, NULL);
								FASTVB.RenderStrip();
							}
						}

						// the unit, seen from the light and scaled to fill the texture
						FVector	centre = pos + box->mOrigin;
						PrepareShadowCamera(this, centre, box, info);
						unit->Render(0);

						if (!DXSHADOWS_INVERSE.mValue)
						{
							SShadowVertex	*v;
							if (FASTVB.LockAligned((void **)&v, 10) != 0xffff)
							{
								for (SINT k = 0; k < 10; k++)
								{
									v[k].mColour = 0xffffffff;
									v[k].mU = 0;
									v[k].mV = 0;
								}
								v[0].mPos = D3DXVECTOR4(0.f, 0.f, 0.f, 1.f);
								v[1].mPos = D3DXVECTOR4(2.f, 2.f, 0.f, 1.f);
								v[2].mPos = D3DXVECTOR4(64.f, 0.f, 0.f, 1.f);
								v[3].mPos = D3DXVECTOR4(62.f, 2.f, 0.f, 1.f);
								v[4].mPos = D3DXVECTOR4(64.f, 64.f, 0.f, 1.f);
								v[5].mPos = D3DXVECTOR4(62.f, 62.f, 0.f, 1.f);
								v[6].mPos = D3DXVECTOR4(0.f, 64.f, 0.f, 1.f);
								v[7].mPos = D3DXVECTOR4(2.f, 62.f, 0.f, 1.f);
								v[8].mPos = D3DXVECTOR4(0.f, 0.f, 0.f, 1.f);
								v[9].mPos = D3DXVECTOR4(2.f, 2.f, 0.f, 1.f);
								LT.D3D_SetTexture(0, NULL);
								FASTVB.RenderStrip();
							}
						}
					}
					LT.GetDevice()->EndScene();
				}
			}
			info->mUnknown08 = 2;
		}
		info->mUnknown08--;
	}

	SetTargets(target, depth);
	target->Release();
	depth->Release();

	RestoreMatrices(oldView, oldProjection, oldWorld);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	RENDERINFO.SetFogEnabled(true);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	RENDERINFO.EnableLighting(true);
	CMeshRenderer::SetRenderMode(R_Normal);
	IdPadding();
}

//******************************************************************************************
// The landscape again under each shadowed unit, its shadow texture projected straight down and blurred by four jittered
// taps, darkened by the texture's fade (or, with DXSHADOWS_INVERSE, lightening what a depth pass left).
// The negative Y scale keeps the last SetTransform's device push in its scheduling region (00553e31).
void CShadows::RenderLandscapeShadows()	// FUNCTION: 0x00553960
{
	if (gMirrorPass)
		return;
	if (!mShadows)
		return;

	if ((!SHADOW_METHOD.GetMethod()) || (!SRT_ENABLE.mValue))
	{
		mShadows = FALSE;
		mBlobShadows = TRUE;
		SetSetupMessage(0xe7);
		return;
	}

	LT.GetDevice()->SetMaterial(STATE.GetDefaultMaterial());
	if (SHADOWS.mShowShadowExtents)
	{
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	}
	else if (DXSHADOWS_INVERSE.mValue)
	{
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
	}
	else
	{
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	}
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	RENDERINFO.EnableLighting(false);
	RENDERINFO.SetFogEnabled(false);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
	LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	LT.SSS_NoCache(0, D3DSAMP_BORDERCOLOR, 0xffffffff);
	LT.SRS(D3DRS_TEXTUREFACTOR, 0xff808080);
	RENDERINFO.SetWorld(FVector(0, 0, 0), ID_FMATRIX);

	if (DXSHADOWS_INVERSE.mValue)
	{
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
	}
	else
	{
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_ADD);
		if (SHADOW_METHOD.GetMethod() > 1)
		{
			LT.SSS_NoCache(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			LT.SSS_NoCache(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
			LT.STS(1, D3DTSS_COLOROP, D3DTOP_ADD);
			LT.STS(1, D3DTSS_TEXCOORDINDEX, 0);
			LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		}
		else
		{
			LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
		if (SHADOW_METHOD.GetMethod() > 2)
		{
			LT.SSS_NoCache(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			LT.SSS_NoCache(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			LT.STS(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			LT.STS(2, D3DTSS_COLORARG2, D3DTA_CURRENT);
			LT.STS(2, D3DTSS_COLOROP, D3DTOP_ADD);
			LT.STS(2, D3DTSS_TEXCOORDINDEX, 0);
			LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
			LT.SSS_NoCache(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			LT.SSS_NoCache(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			LT.STS(3, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			LT.STS(3, D3DTSS_COLORARG2, D3DTA_CURRENT);
			LT.STS(3, D3DTSS_COLOROP, D3DTOP_ADD);
			LT.STS(3, D3DTSS_TEXCOORDINDEX, 0);
			LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		}
		else
		{
			LT.STS(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
			LT.STS(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
	}

	SShadowTexInfo	*t = mTexInfo;
	for (SINT i = 0; i < mNumTextures; t++, i++)
	{
		if (!mTextures[i])
		{
			SetSetupMessage(0xe7);
			mShadows = FALSE;
			break;
		}
		if (!t->mUnit.ToRead())
			continue;

		FVector	pos = t->mPos;
		float	radius = t->mRadius;
		float	scale = 1.0f / (radius + radius);
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = scale;
		gTextureMatrix._22 = -scale;
		float	u = (radius - pos.X) * scale;
		float	v = (-radius - pos.Y) * -scale;
		gTextureMatrix._31 = u + 1.0f / 192;
		gTextureMatrix._32 = v - 1.0f / 64;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		gTextureMatrix._31 = u + 1.0f / 64;
		gTextureMatrix._32 = v + 1.0f / 192;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);
		gTextureMatrix._31 = u - 1.0f / 192;
		gTextureMatrix._32 = v + 1.0f / 64;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE2, &gTextureMatrix);
		gTextureMatrix._31 = u - 1.0f / 64;
		gTextureMatrix._32 = v - 1.0f / 192;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE3, &gTextureMatrix);
		LT.D3D_SetTexture(0, SHADOWS.mTextures[i]->mTexture);
		LT.D3D_SetTexture(1, SHADOWS.mTextures[i]->mTexture);
		LT.D3D_SetTexture(2, SHADOWS.mTextures[i]->mTexture);
		LT.D3D_SetTexture(3, SHADOWS.mTextures[i]->mTexture);

		SINT	x0 = SINT(floor(pos.X - radius));
		SINT	y0 = SINT(floor(pos.Y - radius));
		SINT	x1 = SINT(floor(pos.X + radius + 1.0f));
		SINT	y1 = SINT(floor(pos.Y + radius + 1.0f));
		DWORD	alpha = (UBYTE)t->mAlpha;
		DWORD	c = (((alpha << 8) + alpha) << 8) + alpha;
		if (DXSHADOWS_INVERSE.mValue)
			LT.SRS(D3DRS_TEXTUREFACTOR, c);
		else
			LT.SRS(D3DRS_TEXTUREFACTOR, 0xffffffff - c);
		ENGINE.GetLandscape()->RenderTiles(x0, x1, y0, y1);

		if (SHADOW_METHOD.GetMethod() < 1)
			break;
	}

	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
	LT.STS(2, D3DTSS_TEXCOORDINDEX, 2);
	LT.STS(3, D3DTSS_TEXCOORDINDEX, 3);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(2, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(2, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(3, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(3, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	RENDERINFO.EnableLighting(true);
	RENDERINFO.SetFogEnabled(true);
}

//******************************************************************************************
// cg_ShowShadowMap: each shadow texture drawn along the screen, blurred by four jittered taps.

struct SShadowMapVertex
{
	float	mX, mY, mZ;
	DWORD	mDiffuse;
	float	mU, mV;
};

void CShadows::TestDraw()	// FUNCTION: 0x00554170
{
	if (gMirrorPass)
		return;
	if (!mShadows)
		return;
	if (!mShowShadowMap)
		return;

	LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_ADD);
	LT.STS(2, D3DTSS_COLOROP, D3DTOP_ADD);
	LT.STS(3, D3DTSS_COLOROP, D3DTOP_ADD);
	LT.STS(1, D3DTSS_TEXCOORDINDEX, 0);
	LT.STS(2, D3DTSS_TEXCOORDINDEX, 0);
	LT.STS(3, D3DTSS_TEXCOORDINDEX, 0);
	LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

	gTextureMatrix._21 = 0;
	gTextureMatrix._12 = 0;
	gTextureMatrix._11 = 1.0f;
	gTextureMatrix._22 = 1.0f;
	gTextureMatrix._31 = 1.0f / 192;
	gTextureMatrix._32 = -1.0f / 64;
	LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
	gTextureMatrix._31 = 1.0f / 64;
	gTextureMatrix._32 = 1.0f / 192;
	LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);
	gTextureMatrix._31 = -1.0f / 192;
	gTextureMatrix._32 = 1.0f / 64;
	LT.GetDevice()->SetTransform(D3DTS_TEXTURE2, &gTextureMatrix);
	gTextureMatrix._31 = -1.0f / 64;
	gTextureMatrix._32 = -1.0f / 192;
	LT.GetDevice()->SetTransform(D3DTS_TEXTURE3, &gTextureMatrix);

	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.EnableLighting(false);

	float	x = -2.0f;
	{
		D3DXMATRIX	id;
		D3DXMatrixIdentity(&id);
		RENDERINFO.mWorld = *(SRenderMatrix *)&id;
		RENDERINFO.mWorldChanged = true;
		RENDERINFO.mView = *(SRenderMatrix *)&id;
		RENDERINFO.mViewChanged = true;
	}

	for (SINT i = 0; i < mNumTextures; i++)
	{
		if (!mTextures[i])
		{
			SetSetupMessage(0xe7);
			mShadows = FALSE;
			break;
		}

		SShadowMapVertex	v[4] = {{x, 1.0f, 2.0f, 0xffffffff, 0, 0},
									{x + 1, 1.0f, 2.0f, 0xffffffff, 1.0f, 0},
									{x + 1, 0, 2.0f, 0xffffffff, 1.0f, 1.0f},
									{x, 0, 2.0f, 0xffffffff, 0, 1.0f}};
		x += 1;
		LT.D3D_SetTexture(0, mTextures[i]->mTexture);
		LT.D3D_SetTexture(1, mTextures[i]->mTexture);
		LT.D3D_SetTexture(2, mTextures[i]->mTexture);
		LT.D3D_SetTexture(3, mTextures[i]->mTexture);
		RENDERINFO.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		RENDERINFO.Apply();
		LT.GetDevice()->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(v[0]));
	}

	LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
	LT.STS(2, D3DTSS_TEXCOORDINDEX, 2);
	LT.STS(3, D3DTSS_TEXCOORDINDEX, 3);
	RENDERINFO.SetFogEnabled(true);
	RENDERINFO.EnableLighting(true);
}

//******************************************************************************************
// A blob under a thing: smaller the higher it is (trees' half again), fading out with distance from the camera, and
// none for a unit that already has a shadow texture.
// Keep the height query in one expression: its two returned positions occupy separate temporaries (005545ea,
// 00554607). The distance accumulates Z squared, then X squared, then Y squared (005546a2..005546b5).
void CShadows::AddBlobShadow(CThing *thing)	// FUNCTION: 0x005545d0
{
	if (gMirrorPass)
		return;

	float	height = (float)fabs(thing->GetRenderPos().Z - MAP.Collide(thing->GetRenderPos()));
	if (height <= 1.0f)
		height = 1.0f;
	float	size = thing->GetRadius() / height * 0.5f;
	if (thing->IsA(THING_TYPE_TREE))
		size *= 0.5f;
	if (size > 0.1f)
	{
		FVector	delta = thing->GetRenderPos() - ENGINE.GetCamera()->GetPos();
		float	distanceSq = delta.Z * delta.Z;
		distanceSq += delta.X * delta.X;
		distanceSq += delta.Y * delta.Y;
		SINT	alpha = 255 - SINT(sqrtf(distanceSq) * 255.0f / mBlobShadowFadeDist);
		if (alpha > 255)
			alpha = 255;
		else if (alpha <= 0)
			return;

		if (mShadows)
		{
			SShadowTexInfo	*tex = mTexInfo;
			for (SINT i = 0; i < mNumTextures; tex++, i++)
			{
				if (tex->mUnit.ToRead() == thing)
					return;
			}
		}
		DrawBlobShadow(thing->GetRenderPos(), size, alpha);
	}
}

//******************************************************************************************
// The blob's quads: one per landscape cell under it, lying on the ground, the cells stretched away from the light
// and the texture's footprint stretched with them; darker as alpha grows, in the level's sun and ambient average.
// Vertices 2 and 3 take their stretch from vertex 1's Y (00554c0c, 00554cfd), as retail does.
// The stretch reads the offsets through d while the texture coordinates write them out again, so d's fields are copies
// of the shared offsets: the X offset stays on the FPU with its copy made where it is defined (00554a28) and exchanged
// before the X product (00554a42), and the Y offset, which outlives the X copy beneath it, is stored (00554a38). The
// half size written out at each use is a shared temporary that sorts ahead of pos.Y in y1's sum (00554913), where a
// half local sorted after it. Vertex 0's colour is stored before p is built (005549cd).
void CShadows::DrawBlobShadow(FVector pos, float size, SINT alpha)	// FUNCTION: 0x00554750
{
	if (gMirrorPass)
		return;

	if (size > 2.0f)
		size = 2.0f;

	DWORD	c = ((MAP.GetAmbient() & 0xfefefe) + (MAP.GetSun() & 0xfefefe)) >> 1;
	SINT	k = 255 - alpha;
	SINT	r = SINT((c >> 16) & 0xff) * k >> 8;
	SINT	g = SINT((c >> 8) & 0xff) * k >> 8;
	SINT	bl = SINT(c & 0xff) * k >> 8;
	DWORD	colour = (0xff << 24) | (r << 16) | (g << 8) | bl;

	float	border = 1.0f;
	if (size > 1.0f)
		border = size;

	SINT	x0, x1, y0, y1;
	if (mLight.X > 0)
	{
		x0 = SINT(floor(pos.X - size - border));
		x1 = SINT(ceil(size * 0.5f + pos.X + border));
	}
	else
	{
		x0 = SINT(floor(pos.X - size * 0.5f - border));
		x1 = SINT(ceil(pos.X + border + size));
	}
	if (mLight.Y > 0)
	{
		y0 = SINT(floor(pos.Y - size - border));
		y1 = SINT(ceil(size * 0.5f + pos.Y + border));
	}
	else
	{
		y0 = SINT(floor(pos.Y - size * 0.5f - border));
		y1 = SINT(ceil(pos.Y + border + size));
	}

	for (SINT x = x0; x < x1; x++)
	{
		for (SINT y = y0; y < y1; y++)
		{
			CFVF_GTEX	v[4];
			float		f, scale;
			FVector		d;

			v[0].mDiffuse = colour;
			FVector	p((float)x, (float)y, 0);
			v[0].mPos = p;
			v[0].mPos.Z = MAP.Collide(p) - 0.01f;
			d.X = v[0].mPos.X - pos.X;
			d.Y = v[0].mPos.Y - pos.Y;
			f = 1.0f - (d.X * mLight.X + d.Y * mLight.Y) * 0.5f;
			if (f < 1.0f)
				f = 1.0f;
			scale = f * size * 2;
			v[0].mU = (v[0].mPos.X - pos.X) / scale + 0.5f;
			v[0].mV = (v[0].mPos.Y - pos.Y) / scale + 0.5f;

			p = FVector((float)x + 1, (float)y, 0);
			v[1].mDiffuse = colour;
			v[1].mPos = p;
			v[1].mPos.Z = MAP.Collide(p) - 0.01f;
			d.X = v[1].mPos.X - pos.X;
			d.Y = v[1].mPos.Y - pos.Y;
			f = 1.0f - (d.X * mLight.X + d.Y * mLight.Y) * 0.5f;
			if (f < 1.0f)
				f = 1.0f;
			scale = f * size * 2;
			v[1].mU = (v[1].mPos.X - pos.X) / scale + 0.5f;
			v[1].mV = (v[1].mPos.Y - pos.Y) / scale + 0.5f;

			p = FVector((float)x + 1, (float)y + 1, 0);
			v[2].mDiffuse = colour;
			v[2].mPos = p;
			v[2].mPos.Z = MAP.Collide(p) - 0.01f;
			d.X = v[2].mPos.X - pos.X;
			d.Y = v[1].mPos.Y - pos.Y;
			f = 1.0f - (d.X * mLight.X + d.Y * mLight.Y) * 0.5f;
			if (f < 1.0f)
				f = 1.0f;
			scale = f * size * 2;
			v[2].mU = (v[2].mPos.X - pos.X) / scale + 0.5f;
			v[2].mV = (v[2].mPos.Y - pos.Y) / scale + 0.5f;

			p = FVector((float)x, (float)y + 1, 0);
			v[3].mDiffuse = colour;
			v[3].mPos = p;
			v[3].mPos.Z = MAP.Collide(p) - 0.01f;
			d.X = v[3].mPos.X - pos.X;
			d.Y = v[1].mPos.Y - pos.Y;
			f = 1.0f - (d.X * mLight.X + d.Y * mLight.Y) * 0.5f;
			if (f < 1.0f)
				f = 1.0f;
			scale = f * size * 2;
			v[3].mU = (v[3].mPos.X - pos.X) / scale + 0.5f;
			v[3].mV = (v[3].mPos.Y - pos.Y) / scale + 0.5f;

			SINT	b = mBlobVB->AddVertices(v, 4);
			WORD	idx[6];
			idx[0] = b + 2;
			idx[1] = b + 1;
			idx[2] = b;
			idx[3] = b;
			idx[4] = b + 3;
			idx[5] = b + 2;
			mBlobVB->AddIndices(idx, 6);
		}
	}
}

//******************************************************************************************
void CShadows::DrawAllBlobShadows()	// FUNCTION: 0x00554df0
{
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	mBlobVB->Draw(true);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
}
