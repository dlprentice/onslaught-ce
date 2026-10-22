// DXCompass.cpp: the compass's PC part: its ring textures and vertex buffers and the byte-sprite dial. Not in
// Stuart's release: reconstructed from retail (0053bc40..0053d21f; the object starts with mathsconst.h's two
// initializers, and its ID_FMATRIX at 0089c928 is what the render's view starts from, 0053cdbb).

#include "common.h"
#define DXCOMPASS_CPP

#if TARGET == PC

#include	"DX.h"
#include	"ltshell.h"
#include	"Compass.h"
#include	"Texture.h"
#include	"vbuffer.h"
#include	"bytesprite.h"
#include	"tweak.h"
#include	"console.h"
#include	"BattleEngine.h"
#include	"RadarWarningReceiver.h"
#include	"renderinfo.h"
#include	"Platform.h"
#include	"game.h"
#include	"Hud.h"
#include	"engine.h"
#include	"mathsconst.h"	// FUNCTION: 0x0053bc40 init:ZERO_FVECTOR
						// FUNCTION: 0x0053bc60 init:ID_FMATRIX

// FUNCTION: 0x0053bd30 init:SQUARE_TEXTURES_ONLY
// FUNCTION: 0x0053bd50 exit:SQUARE_TEXTURES_ONLY
CTweakSLONG	SQUARE_TEXTURES_ONLY("SQUARE_TEXTURES_ONLY", 0);		// 0089c918

// The ring textures' sizes, reduced to what the device allows (00650424..00650430). Compass.cpp reads the inner
// width (CCompass::Render 00427706), so they are not static.
UINT	gCompassOuterWidth = 512;
UINT	gCompassOuterHeight = 32;
UINT	gCompassInnerWidth = 256;
UINT	gCompassInnerHeight = 8;

// FUNCTION: 0x0053bd60
CDXCompass::CDXCompass()
{
	for (int i = 0; i < 2; i++)
	{
		mRingTexture[i] = NULL;
		mRingTexture2[i] = NULL;
	}
	mOuterRing = NULL;
	mByteSprite = NULL;
	mInnerRing = NULL;
}

// FUNCTION: 0x0053bda0
void CDXCompass::ReleaseResources()
{
	for (int i = 0; i < 2; i++)
	{
		SAFE_RELEASE(mRingTexture[i]);
		SAFE_RELEASE(mRingTexture2[i]);
	}
	SAFE_DELETE(mOuterRing);
	SAFE_DELETE(mByteSprite);
	SAFE_DELETE(mInnerRing);
}

// FUNCTION: 0x0053be40
void CCompass::PlatformInit()
{
	CDXCompass *dx = (CDXCompass *)this;

#line 67
	mByteSprite = new (MEMTYPE_BYTESPRITE) CByteSprite;
	mByteSprite->Load("dial", 16, 16, 20, 4);

	if ((LT.m_d3dCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) || SQUARE_TEXTURES_ONLY.mValue)
		gCompassOuterHeight = gCompassOuterWidth;
	while (gCompassOuterWidth > LT.m_d3dCaps.MaxTextureWidth)
		gCompassOuterWidth >>= 1;
	while (gCompassOuterHeight > LT.m_d3dCaps.MaxTextureHeight)
		gCompassOuterHeight >>= 1;
	CONSOLE.FatalError(gCompassOuterHeight >= 32, 0xc5, -1);

	if ((LT.m_d3dCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) || SQUARE_TEXTURES_ONLY.mValue)
		gCompassInnerHeight = gCompassInnerWidth;
	while (gCompassInnerWidth > LT.m_d3dCaps.MaxTextureWidth)
		gCompassInnerWidth >>= 1;
	while (gCompassInnerHeight > LT.m_d3dCaps.MaxTextureHeight)
		gCompassInnerHeight >>= 1;

	for (int i = 0; i < 2; i++)
	{
#line 91
		mRingTexture[i] = new (MEMTYPE_TEXTURE) CDXTexture;
		dx->mRingTexture2[i] = new (MEMTYPE_TEXTURE) CDXTexture;
		CONSOLE.FatalError(mRingTexture[i]->InitEmpty(gCompassOuterWidth, gCompassOuterHeight, TEXFMT_A4R4G4B4, 1, 0, 1) != 0, 0xc6, -1);
		CONSOLE.FatalError(dx->mRingTexture2[i]->InitEmpty(gCompassInnerWidth, gCompassInnerHeight, TEXFMT_A4R4G4B4, 1, 0, 1) != 0, 0xc6, -1);
	}

	void *verts;
#line 98
	dx->mOuterRing = new (MEMTYPE_VBUFFER) CVBuffer;
	dx->mOuterRing->Create(102, 20, D3DFVF_XYZ | D3DFVF_TEX1);
	dx->mOuterRing->Lock(&verts);
	dx->BuildRing(verts, gCompassOuterWidth, gCompassOuterHeight, 50, 31, 32.f / (float)gCompassOuterHeight);
	dx->mOuterRing->Unlock();

#line 104
	dx->mInnerRing = new (MEMTYPE_VBUFFER) CVBuffer;
	dx->mInnerRing->Create(82, 20, D3DFVF_XYZ | D3DFVF_TEX1);
	dx->mInnerRing->Lock(&verts);
	dx->BuildRing(verts, gCompassInnerWidth, gCompassInnerHeight, 40, 27, 8.f / (float)gCompassInnerHeight);
	dx->mInnerRing->Unlock();

	mByteSprite->SetTarget(this, 512, 512, 30, 1);
}

// A textured ring of segments quads as a strip: inner edge at v, outer edge at 0, closed by repeating the first pair.
// FUNCTION: 0x0053c1d0
void CDXCompass::BuildRing(void *verts, int width, int height, int segments, int radius, float v)
{
	struct SVert { float x, y, z, u, v; };
	SVert *p = (SVert *)verts;

	float angle = 0;
	float n = (float)segments;
	float step = PI_M2 / n;
	float u = 0;
	float r = radius * 0.01f;
	float t = (height * v * PI) / width;
	float outer = (t + 1.f) * r;
	float inner = (1.f - t) * r;

	for (int i = segments; i > 0; i--)
	{
		float du = 1.f / n;
		float s = (float)sin(angle);
		p[0].x = s * inner;
		float c = (float)cos(angle);
		p[0].y = c * inner;
		p[0].z = 1.f;
		p[0].u = u;
		p[0].v = v;
		p[1].x = s * outer;
		p[1].y = c * outer;
		p[1].z = 1.f;
		p[1].u = u;
		p[1].v = 0;
		p += 2;
		angle += step;
		u += du;
	}
	p[0] = ((SVert *)verts)[0];
	p[1] = ((SVert *)verts)[1];
	p[0].u = 1.f;
	p[1].u = 1.f;
}

// A threat the radar warning receiver reports (name ours).
struct SCompassThreat
{
	float	mYaw;				// +0x00
	float	mHeight;			// +0x04
	int		mId;				// +0x08
};

// FUNCTION: 0x0053c2e0
void CDXCompass::DrawThreats(CBattleEngine *be)
{
	WORD	palette[4];

	memset(this, 0, 0x3c00);
	memset((UBYTE *)this + 0x1a00, 2, 0xa00);
	palette[0] = 0;
	palette[1] = 0x2222;
	palette[2] = 0x2444;
	palette[3] = 0x2666;
	mByteSprite->DrawFrame(0, 0, 15);

	CRadarWarningReceiver *rwr = be->GetRadarWarningReceiver();
	if (rwr)
	{
		for (SCompassThreat *t = (SCompassThreat *)rwr->mUnknown1C.First(); t; t = (SCompassThreat *)rwr->mUnknown1C.Next())
		{
			float yaw = -t->mYaw;
			while (yaw < 0)
				yaw += PI_M2;
			while (yaw > PI_M2)
				yaw -= PI_M2;
			int x = (int)((yaw / PI_M2) * 512.f);
			mByteSprite->DrawFrame(1, x, 15);
			mByteSprite->DrawFrame(2, x, 15 - (int)(t->mHeight / 1.5f));
		}
	}

	D3DLOCKED_RECT	lr;
	if (FAILED(mRingTexture[mUnknown3c10]->GetTexture()->LockRect(0, &lr, NULL, D3DLOCK_NOSYSLOCK)))
		return;

	UBYTE *dst = (UBYTE *)lr.pBits;
	UBYTE *src = (UBYTE *)this;
	memset(dst, 0, gCompassOuterWidth * 2);
	dst += lr.Pitch;

	int step = (gCompassOuterWidth >= 512) ? 1 : 512 / gCompassOuterWidth;
	for (int y = 30; y; y--)
	{
		WORD *d = (WORD *)dst;
		for (UINT x = gCompassOuterWidth; x; x--)
		{
			*d++ = palette[*src];
			src += step;
		}
		dst += lr.Pitch;
	}

	for (UINT n = gCompassOuterHeight - 31; n; n--)
	{
		memset(dst, 0, gCompassOuterWidth * 2);
		dst += lr.Pitch;
	}

	mRingTexture[mUnknown3c10]->GetTexture()->UnlockRect(0);
}

//*********************************************************************************
// The inner ring's texture: the health bar from 60 to 150 degrees (green, its lost part grey, red while it falls) and
// the energy bar from 225 to 360 (blue, its spent part grey, red just after damage and brighter while it refills), each
// fading out down its rows.
struct SBarColours { int r0, g0, b0, r, g, b; };
struct SBarColour { int r, g, b; };
void CDXCompass::Unknown53c510(CBattleEngine *be)	// FUNCTION: 0x0053c510
{
	int		health = gCompassInnerWidth * 150 / 360 - SINT(be->GetLife() * (gCompassInnerWidth * 150 / 360 - gCompassInnerWidth * 60 / 360) / be->GetInitialLife());
	int		energy = gCompassInnerWidth * 225 / 360 + SINT(be->GetEnergy() * (gCompassInnerWidth * 360 / 360 - gCompassInnerWidth * 225 / 360) / be->GetMaxEnergy());

	D3DLOCKED_RECT	lr;
	if (FAILED(mRingTexture2[mUnknown3c10]->GetTexture()->LockRect(0, &lr, NULL, D3DLOCK_NOSYSLOCK)))
		return;

	UBYTE	*dst = (UBYTE *)lr.pBits;
	memset(dst, 0, gCompassInnerWidth * 2);
	dst += lr.Pitch;

	SBarColour	h = { 1, 8, 1 };
	SBarColour	h0 = { 3, 3, 3 };
	SBarColour	e0 = { 3, 3, 3 };
	SBarColour	e = { 1, 1, 8 };

	int		view = (be->GetPlayer()->GetNumber() != 1);
	if (health < mLastHealthEnd[view])
	{
		h.r = 15;
		h.g = 1;
		h.b = 1;
	}

	float	t = EVENT_MANAGER.GetTime() - be->GetLastDamageTime();
	if (t > 2.0f)
		t = 2.0f;
	float	f = 1.0f - t * 0.5f;
	if (t < 2.0f)
	{
		float	g = 1.0f - f;
		e.r = SINT(f * 15.0f + g);
		e.g = SINT(g + f);
		e.b = SINT((1.0f - f) * 8.0f + f);
	}
	else if (energy > mLastEnergyEnd[view])
	{
		e.r = 1;
		e.g = 5;
		e.b = 8;
	}
	mLastEnergyEnd[view] = energy;
	mLastHealthEnd[view] = health;

	if (!HUD.IsComponentVisible(0))
		h0.r = h0.g = h0.b = h.r = h.g = h.b = 0;
	if (!HUD.IsComponentVisible(1))
		e0.r = e0.g = e0.b = e.r = e.g = e.b = 0;

	for (int n = gCompassInnerHeight - 2; n; n--)
	{
		WORD	alpha = (4 * n / (gCompassInnerHeight - 2)) << 12;
		WORD	hbg = ((((h0.r * n / (gCompassInnerHeight - 2)) << 4) + h0.g * n / (gCompassInnerHeight - 2)) << 4) + h0.b * n / (gCompassInnerHeight - 2);
		WORD	hbar = ((((h.r * n / (gCompassInnerHeight - 2)) << 4) + h.g * n / (gCompassInnerHeight - 2)) << 4) + h.b * n / (gCompassInnerHeight - 2);
		WORD	ebg = ((((e0.r * n / (gCompassInnerHeight - 2)) << 4) + e0.g * n / (gCompassInnerHeight - 2)) << 4) + e0.b * n / (gCompassInnerHeight - 2);
		WORD	ebar = ((((e.r * n / (gCompassInnerHeight - 2)) << 4) + e.g * n / (gCompassInnerHeight - 2)) << 4) + e.b * n / (gCompassInnerHeight - 2);
		if (HUD.IsComponentVisible(0))
		{
			hbg += alpha;
			hbar += alpha;
		}
		if (HUD.IsComponentVisible(1))
		{
			ebg += alpha;
			ebar += alpha;
		}

		WORD	*p = (WORD *)dst;
		int		x = 0;
		for (; x < gCompassInnerWidth * 60 / 360; x++)
			*p++ = 0;
		for (; x < health; x++)
			*p++ = hbg;
		for (; x < gCompassInnerWidth * 150 / 360; x++)
			*p++ = hbar;
		for (; x < gCompassInnerWidth * 225 / 360; x++)
			*p++ = 0;
		for (; x < energy; x++)
			*p++ = ebar;
		for (; x < gCompassInnerWidth * 360 / 360; x++)
			*p++ = ebg;
		dst += lr.Pitch;
	}

	memset(dst, 0, gCompassInnerWidth * 2);
	mRingTexture2[mUnknown3c10]->GetTexture()->UnlockRect(0);
}

//*********************************************************************************
// The compass: the inner ring fixed, the outer ring turned against the battle engine's yaw, each from the texture
// the frame before drew (the pair flips every frame outside split screen), in a small projection of their own;
// then CCompass's markers. The view is ID_FMATRIX with its second row scaled by the screen shape.
// FUNCTION: 0x0053cd30
void CDXCompass::Render(CBattleEngine *be)
{
	float	scale = LT.mScreenShapeScale;

	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	DrawThreats(be);
	Unknown53c510(be);

	D3DXMATRIX	view = *(D3DXMATRIX *)&RENDERINFO.mView;
	D3DXMATRIX	proj = *(D3DXMATRIX *)&RENDERINFO.mProjection;
	D3DXMATRIX	world = *(D3DXMATRIX *)&RENDERINFO.mWorld;

	FVector	pos(0, 0, 0);
	FMatrix	m = ID_FMATRIX;
	RENDERINFO.SetView(&m, &pos);
	m.Row[1].X *= scale;
	m.Row[1].Y *= scale;
	m.Row[1].Z *= scale;
	RENDERINFO.SetWorld(pos, m);

	if (GAME.IsMultiplayer())
		RENDERINFO.SetProjection(0.5f, 100.0f, PLATFORM.GetWindowWidth() * 0.00093750004f, PLATFORM.GetWindowHeight() * 0.00046666668f);
	else
		RENDERINFO.SetProjection(0.5f, 100.0f, PLATFORM.GetWindowWidth() * 0.00078125f, PLATFORM.GetWindowHeight() * 0.00078125f);

	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);

	int		which;
	if (GAME.IsMultiplayer())
		which = 0;
	else
		which = (mUnknown3c10 + 1) % 2;

	LT.D3D_SetTexture(0, mRingTexture2[which]->GetTexture());
	mInnerRing->SetStreamSource(0);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.Apply();
	LT.GetDevice()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 80);

	float	yaw = -be->GetInterpolatedEulerOrientation().mYaw;
	m.Row[0] = FVector((float)cos(yaw), -(float)sin(yaw), 0);
	m.Row[1] = FVector((float)sin(yaw), (float)cos(yaw), 0);
	m.Row[2] = FVector(0, 0, 1);
	m.Row[1].X *= scale;
	m.Row[1].Y *= scale;
	m.Row[1].Z *= scale;
	RENDERINFO.SetWorld(pos, m);

	LT.D3D_SetTexture(0, mRingTexture[which]->GetTexture());
	mOuterRing->SetStreamSource(0);
	RENDERINFO.Apply();
	if (HUD.IsComponentVisible(2))
		LT.GetDevice()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 100);

	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	if (ENGINE.HudAditive())
		LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

	*(D3DXMATRIX *)&RENDERINFO.mView = view;
	RENDERINFO.mViewChanged = true;
	*(D3DXMATRIX *)&RENDERINFO.mWorld = world;
	RENDERINFO.mWorldChanged = true;
	*(D3DXMATRIX *)&RENDERINFO.mProjection = proj;
	RENDERINFO.mProjectionChanged = true;

	CCompass::Render(be);

	if (GAME.IsMultiplayer())
		mUnknown3c10 = 0;
	else
		mUnknown3c10 = (mUnknown3c10 + 1) % 2;
}

#endif

