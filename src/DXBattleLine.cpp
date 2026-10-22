// DXBattleLine.cpp: the PC HUD's battle line (CDXBattleLine; CHud keeps one at +0x30). Not in Stuart's release;
// reconstructed from the retail code. The ground inside the radar circle is drawn into the height texture, a mesh
// triangulated round the influence map's points carries the influence texture's colour ramp into the radar disc,
// and BattleLine.obj's Update adds the units as coloured overlay points.

#include	"Common.h"
#include	"BattleLine.h"
#include	"triangulate.h"
#include	"vbuffer.h"
#include	"ibuffer.h"
#include	"Hud.h"
#include	"game.h"
#include	"player.h"
#include	"map.h"
#include	"renderinfo.h"
#include	"InfluenceMap.h"
#include	"MessageBox.h"
#include	"tweak.h"
#include	"SpriteRenderer.h"
#include	"eventmanager.h"
#include	"mathsconst.h"	// FUNCTION: 0x00539e10 init:ZERO_FVECTOR
						// FUNCTION: 0x00539e30 init:ID_FMATRIX

extern CTweakSLONG	SQUARE_TEXTURES_ONLY;		// 0089c918 (DXCompass.cpp)

// an influence map point as the battle line reads it (the set's items; names open)
struct SInfluencePoint
{
	UBYTE	mUnknown00[0x8c];
	float	mX;							// +0x8c
	float	mY;							// +0x90
	UBYTE	mUnknown94[0xb4 - 0x94];
	float	mInfluence;					// +0xb4: -1..1
};

// the radar keeps a map point within its 44-pixel circle (CDXBattleLine::Render, AddOverlayPoint); it works on the
// caller's point in place
static inline FVector2	RadarClamp(FVector2 &p)
{
	float	dx = p.X - 44.f;
	float	dy = (p.Y -= 44.f);
	float	k = 44.f / sqrtf(dy * dy + dx * dx);
	if (k < 1.f)
	{
		dx *= k;
		p.Y *= k;
	}
	return FVector2(dx + 44.f, p.Y + 44.f);
}

//******************************************************************************************
CDXBattleLine::CDXBattleLine()	// FUNCTION: 0x00539f00
{
	mNumOverlayPoints = 0;
	mTriangulate = NULL;
	mInfluenceTexture = NULL;
	mHeightTexture = NULL;
	mVB = NULL;
	mIB = NULL;
	mPoints = NULL;
	mMarkerTexture = NULL;
	mOverlayVB = NULL;
	mUnknown10 = NULL;
	mEngineMarkerTexture = NULL;
}

//******************************************************************************************
CDXBattleLine::~CDXBattleLine()	// FUNCTION: 0x00539f30
{
	ShutDown();
}

//******************************************************************************************
void	CDXBattleLine::ShutDown()	// FUNCTION: 0x00539f40
{
	SAFE_DELETE(mTriangulate);
	SAFE_RELEASE(mInfluenceTexture);
	SAFE_RELEASE(mHeightTexture);
	SAFE_DELETE(mVB);
	SAFE_DELETE(mIB);
	SAFE_DELETE(mPoints);
	SAFE_RELEASE(mMarkerTexture);
	SAFE_DELETE(mOverlayVB);
	SAFE_RELEASE(mUnknown10);
	SAFE_RELEASE(mEngineMarkerTexture);
}

//******************************************************************************************
HRESULT	CBLTexture::InitDeviceObjects()	// FUNCTION: 0x0053a010
{
	HRESULT	hr = CDXTexture::InitDeviceObjects();
	if (SUCCEEDED(hr))
	{
		HUD.mBattleLine->InitMipLevels();
		HUD.mBattleLine->UpdateHeightmap();
	}
	return hr;
}

//******************************************************************************************
HRESULT	CBLTexture::RestoreDeviceObjects()	// FUNCTION: 0x0053a040
{
	return CDXTexture::RestoreDeviceObjects();
}

//******************************************************************************************
void	CDXBattleLine::Init(int a, int b)	// FUNCTION: 0x0053a050
{
	mUnknown64 = a;
	mUnknown68 = b;
#line 112
	mInfluenceTexture = new(MEMTYPE_TEXTURE) CBLTexture;
	if (!(LT.m_d3dCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) && !SQUARE_TEXTURES_ONLY.mValue)
		mInfluenceTexture->InitEmpty(128, 1, TEXFMT_A4R4G4B4, 0, 0, 1);
	else
		mInfluenceTexture->InitEmpty(128, 128, TEXFMT_A4R4G4B4, 0, 0, 1);
	InitMipLevels();
}

// FUNCTION: 0x0053a120 ??_GCBLTexture@@UAEPAXI@Z
// FUNCTION: 0x0053a140 ??1CBLTexture@@UAE@XZ

//******************************************************************************************
void	CDXBattleLine::InitResources()	// FUNCTION: 0x0053a150
{
	mMarkerTexture = CDXTexture::GetTextureByName("hud\\marker.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1, 1, 1);
#line 132
	mOverlayVB = new(MEMTYPE_VBUFFER) CVBuffer;
	mOverlayVB->CreateDynamic(500, sizeof(SBattleLinePoint), D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	mOverlayLocked = FALSE;

	int	oldmode = LT.m_bTextureCompressionDisabled;
	LT.m_bTextureCompressionDisabled = 1;
	mEngineMarkerTexture = CDXTexture::GetTextureByName("hud\\V2\\BattleEngineMarker.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1, 1, 1);
	LT.m_bTextureCompressionDisabled = oldmode;

#line 151
	mHeightTexture = new(MEMTYPE_TEXTURE) CDXTexture;
	mHeightTexture->InitEmpty(128, 128, TEXFMT_A4R4G4B4, 1, 0, 1);
}

//******************************************************************************************
BOOL	CDXBattleLine::PostLoadProcess()	// FUNCTION: 0x0053a280
{
	int	oldmode = LT.m_bTextureCompressionDisabled;
	LT.m_bTextureCompressionDisabled = 1;
	mUnknown24 = 2;
	BuildMesh();
	LT.m_bTextureCompressionDisabled = oldmode;
	return TRUE;
}

// the influence map's area and the mesh's (names open)
FVector2	BL_MAP_MIN(0.f, 0.f);						// FUNCTION: 0x0053a2b0 init:BL_MAP_MIN
FVector2	BL_MAP_MAX(96.f, 96.f);						// FUNCTION: 0x0053a2d0 init:BL_MAP_MAX
FVector2	BL_MESH_MIN(-4.f, -4.f);					// FUNCTION: 0x0053a2f0 init:BL_MESH_MIN
FVector2	BL_MESH_MAX(132.f, 132.f);					// FUNCTION: 0x0053a310 init:BL_MESH_MAX
FVector2	BL_MAP_SIZE(BL_MAP_MAX.X - BL_MAP_MIN.X, BL_MAP_MAX.Y - BL_MAP_MIN.Y);		// FUNCTION: 0x0053a330 init:BL_MAP_SIZE
FVector2	BL_MESH_SIZE(BL_MESH_MAX.X - BL_MESH_MIN.X, BL_MESH_MAX.Y - BL_MESH_MIN.Y);	// FUNCTION: 0x0053a360 init:BL_MESH_SIZE

//******************************************************************************************
// The loops count from 0 and the circle test offsets them by 48, so the optimizer makes x - 48 and y - 48 the induction
// variables and sets them after each loop's guard (0053a4be, 0053a4df), with y in ebp and the row pointer split as in
// retail. The centre is (lo + hi) * 0.5f and the colour index sums n.X + n.Y: each field's scalar takes its id at its
// first access and the later id loads first (0053a403, 0053a524).
// lo and hi start as the area's minimum and maximum and are overwritten with the bounds once the size is known: one
// variable each. As separate mn/mx variables the maximum (weight 3) sorts ahead of the sum temporary in the packer's list
// and takes half's slot (7 of retail's 10 frame classes, 268 bytes off); as lo (weight 6) and hi (weight 5) they come
// first and start the classes retail has, the sum temporary joins row and half, and the normal joins hi.
// STAND-IN (spelling, no code): the upper X bound is built as BuildMesh builds it, `hi.X = size; hi.X += centre.X;`, which
// makes retail's `fld st(0); fadd`; `hi.X = centre.X + size` is 5 bytes off.
void	CDXBattleLine::UpdateHeightmap()	// FUNCTION: 0x0053a390
{
	if (!mHeightTexture)
		return;

	SExtents	*area = MAP.GetExtents();

	D3DSURFACE_DESC	desc;
	mHeightTexture->GetTexture()->GetLevelDesc(0, &desc);
	D3DLOCKED_RECT	lr;
	if (FAILED(mHeightTexture->GetTexture()->LockRect(0, &lr, NULL, 0)))
		return;

	FVector2	lo((float)area->mMinX, (float)area->mMinY);
	FVector2	hi((float)area->mMaxX, (float)area->mMaxY);
	FVector2	centre = (lo + hi) * 0.5f;
	FVector2	half = (hi - lo) * 0.5f;
	float		size = half.X;
	if (size < half.Y)
		size = half.Y;
	size *= 1.21f;
	UBYTE		*row = (UBYTE *)lr.pBits;
	lo.X = centre.X - size;
	hi.X = size;		// STAND-IN (spelling, no code), see above
	hi.X += centre.X;
	lo.Y = centre.Y - size;
	hi.Y = centre.Y + size;
	FVector2	range = hi - lo;
	float		scale = range.X / BL_MAP_SIZE.X;

	FVector		pos;
	pos.Y = lo.Y - BL_MAP_MIN.Y * scale;
	for (int y = 0; y < (int)desc.Height; y++)
	{
		pos.X = lo.X - BL_MAP_MIN.X * scale;
		WORD	*p = (WORD *)row;
		for (int x = 0; x < (int)desc.Width; x++)
		{
			if ((x - 48) * (x - 48) + (y - 48) * (y - 48) < 46 * 46)
			{
				if (MAP.Collide(pos) < area->mWaterLevel)
				{
					FVector	n = MAP.Normal(pos);
					int		c = int((n.X + n.Y) * 4.5f + 1.5f);
					if (c < 0)
						c = 0;
					else if (c > 3)
						c = 3;
					*p = 0xf000 + c * 0x111;
				}
				else
					*p = 0xf000;
			}
			else
				*p = 0;
			p++;
			pos.X += scale;
		}
		row += lr.Pitch;
		pos.Y += scale;
	}

	mHeightTexture->GetTexture()->UnlockRect(0);
}

//******************************************************************************************
// Three STAND-INs (spelling, no code), each measured against the plain form (bytes off retail):
// - `hi.X = size; hi.X += centre.X;` makes the bound retail's `fld st(0); fadd` (adding centre.X to a copy of size);
//   `centre.X + size`, `size + centre.X` and bracketed forms all load centre.X first (5 bytes).
// - The influence point `p` is declared with lo and hi: retail gives hi.X half's dead frame slot, which a point declared
//   inside the loop does not (5 bytes).
// - The two counts are read into locals and then stored (`tris`, `verts`); read straight into the members, the three
//   scratch picks of the allocation lines (mNumTris, CreateDynamic's count, the new size) come one step off retail's
//   (6 bytes).
// The Y offset is scaled by mScaleX, as retail's code reads.
void	CDXBattleLine::BuildMesh()	// FUNCTION: 0x0053a5e0
{
	SExtents	*area = MAP.GetExtents();
	UpdateHeightmap();

	FVector2	mn((float)area->mMinX, (float)area->mMinY);
	FVector2	mx((float)area->mMaxX, (float)area->mMaxY);
	FVector2	centre = (mx + mn) * 0.5f;
	FVector2	half = (mx - mn) * 0.5f;
	float		size = half.X;
	if (size < half.Y)
		size = half.Y;
	size *= 1.21f;
	FVector2	lo, hi, p;	// STAND-IN (spelling, no code): p is declared here, see above
	lo.X = centre.X - size;
	hi.X = size;		// STAND-IN (spelling, no code), see above
	hi.X += centre.X;
	lo.Y = centre.Y - size;
	hi.Y = centre.Y + size;
	FVector2	range = hi - lo;

#line 325
	mTriangulate = new(MEMTYPE_UNKNOWN) CTriangulate(1024, BL_MESH_MIN, BL_MESH_MAX, 1);

	mScaleX = BL_MAP_SIZE.X / range.X;
	mScaleY = BL_MAP_SIZE.Y / range.Y;
	mOffsetX = BL_MAP_MIN.X - lo.X * mScaleX;
	mOffsetY = BL_MAP_MIN.Y - lo.Y * mScaleX;

	if (INFLUENCEMAP)
	{
		SPtrSet<CInfluenceNode>	*points = &INFLUENCEMAP->mNodes;
		SInfluencePoint	*ip;
		for (ip = (SInfluencePoint *)points->First(); ip; ip = (SInfluencePoint *)points->Next())
		{
			p.X = ip->mX * mScaleX + mOffsetX;
			p.Y = ip->mY * mScaleY + mOffsetY;
			mTriangulate->AddPoint(p);
		}
	}
	mTriangulate->Relax();

	SINT		tris = mTriangulate->mNumTris;	// STAND-IN (spelling, no code), see above
	mNumTriangles = tris;
	SINT		verts = mTriangulate->mNumVerts;
	mNumPoints = verts;

	if (!mVB)
	{
#line 353
		mVB = new(MEMTYPE_VBUFFER) CVBuffer;
		mVB->CreateDynamic(mNumPoints, sizeof(SBattleLineVertex), D3DFVF_XYZRHW | D3DFVF_TEX2);
	}

#line 357
	mPoints = new(MEMTYPE_UNKNOWN) FVector2[mNumPoints];
	FVector2	*src = mTriangulate->mVerts;
	FVector2	*dst = mPoints;
	for (int n = mNumPoints; n; n--)
		*dst++ = *src++;

#line 368
	mIB = new(MEMTYPE_IBUFFER) CIBuffer;
	mIB->Create(mNumTriangles * 3);
	WORD	*indices;
	mIB->Lock(&indices);
	STriangle	*t = mTriangulate->mTris;
	for (int m = mNumTriangles; m; m--)
	{
		indices[0] = t->mA;
		indices[1] = t->mC;
		indices[2] = t->mB;
		indices += 3;
		t++;
	}
	mIB->Unlock();
}

//******************************************************************************************
// The influence colours: red to the left of the middle, a green centre line, blue to the right.
void	CDXBattleLine::InitMipLevels()	// FUNCTION: 0x0053a930
{
	int	levels = mInfluenceTexture->GetTexture()->GetLevelCount();
	for (int l = 0; l < levels; l++)
	{
		D3DSURFACE_DESC	desc;
		mInfluenceTexture->GetTexture()->GetLevelDesc(l, &desc);
		D3DLOCKED_RECT	lr;
		if (FAILED(mInfluenceTexture->GetTexture()->LockRect(l, &lr, NULL, D3DLOCK_NOSYSLOCK)))
			return;

		UINT	half = desc.Width >> 1;
		UBYTE	*row = (UBYTE *)lr.pBits;
		for (UINT y = 0; y < desc.Height; y++)
		{
			WORD	*p = (WORD *)row;
			for (UINT x = 0; x < desc.Width; x++)
			{
				if (x < half)
					*p = 0xfc00;
				else
					*p = (half < x) ? 0xf00c : 0xf090;
				p++;
			}
			row += lr.Pitch;
		}
		mInfluenceTexture->GetTexture()->UnlockRect(l);
	}
}

CRenderMethod	BATTLELINE_METHOD("battleline_method", 3);	// 0089c810		// FUNCTION: 0x0053aa10 init:BATTLELINE_METHOD
																			// FUNCTION: 0x0053aa30 exit:BATTLELINE_METHOD

static void	SetupVertex(SBattleLineVertex *v, float y, float xoffset, float yoffset, FVector2 *p, float u, bool colours);

//******************************************************************************************
void	CDXBattleLine::UpdateVertexBuffer(float y, bool colours)	// FUNCTION: 0x0053aa40
{
	SBattleLineVertex	*v;
	if (FAILED(mVB->mVB->Lock(0, 0, (void **)&v, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK)))
		return;

	FVector2	*p = mPoints;
	for (int i = 0; i < 8; i++)
	{
		SetupVertex(v, y, (float)mUnknown64, (float)mUnknown68, p, 0.50390625f, colours);
		p++;
		v++;
	}
	SPtrSet<CInfluenceNode>	*points = &INFLUENCEMAP->mNodes;
	SInfluencePoint	*ip;
	for (ip = (SInfluencePoint *)points->First(); ip; ip = (SInfluencePoint *)points->Next())
	{
		float	influence = ip->mInfluence;
		SetupVertex(v, y, (float)mUnknown64, (float)mUnknown68, p, influence * 0.49f + 0.50390625f, colours);
		p++;
		v++;
	}

	mVB->Unlock();
}

//******************************************************************************************
static void	SetupVertex(SBattleLineVertex *v, float y, float xoffset, float yoffset, FVector2 *p, float u, bool colours)	// FUNCTION: 0x0053ab40
{
	float	ty = -yoffset - p->Y;
	float	u0 = p->X * (1.f / 128.f);
	float	v0 = p->Y * (1.f / 128.f);
	v->X = HUD.mViewportX + HUD.mViewportWidth - 128.f - HUD_OFFSET_X + (p->X - xoffset);
	v->Z = 0.9f;
	v->RHW = 0.5f;
	v->Y = ty + y;
	if (!colours)
	{
		v->U0 = u0;
		v->V0 = v0;
		v->U1 = u;
		v->V1 = 0.f;
	}
	else
	{
		v->U0 = u;
		v->V0 = 0.f;
		v->U1 = 0.f;
		v->V1 = 0.f;
	}
}

//******************************************************************************************
void	CDXBattleLine::Render()	// FUNCTION: 0x0053abe0
{
	if (!INFLUENCEMAP)
		return;

	RENDERINFO.SetFogEnabled(false);
	DWORD	oldz = PCLTShell::mRenderStates[D3DRS_ZENABLE];
	LT.SRS(D3DRS_ZENABLE, FALSE);

	float	y;
	if (GAME.IsMultiplayer())
		y = PLATFORM.GetWindowHeight() * 0.5f + 62.f;
	else
		y = HUD.mViewportY + HUD.mViewportHeight - HUD_OFFSET_Y;

	UpdateVertexBuffer(y, false);

	float	bias = 0.5f;

	if (BATTLELINE_METHOD.Try(2))
	{
		RENDERINFO.SetFogEnabled(false);
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		LT.SSS_NoCache(1, D3DSAMP_MIPMAPLODBIAS, *(DWORD *)&bias);
		LT.STS(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
		LT.STS(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_MODULATEALPHA_ADDCOLOR);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		LT.SSS_NoCache(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		LT.SSS_NoCache(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		SetMipFilterByGlobalToggle(1);
		LT.D3D_SetTexture(0, mHeightTexture->GetTexture());
		LT.D3D_SetTexture(1, mInfluenceTexture->GetTexture());
		mVB->SetStreamSource(0);
		RENDERINFO.Apply(0, 1);
		LT.GetDevice()->SetIndices(mIB->mIB);
		if (BATTLELINE_METHOD.Validate())
		{
			BATTLELINE_METHOD.Accept();
			if (!GAME.GetMessageBox()->mCurrent)
				LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, mNumPoints, 0, mNumTriangles);
		}
		else
		{
			LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
	}

	if (BATTLELINE_METHOD.Try(1))
	{
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		LT.D3D_SetTexture(0, mHeightTexture->GetTexture());
		mVB->SetStreamSource(0);
		RENDERINFO.Apply();
		LT.GetDevice()->SetIndices(mIB->mIB);
		LT.SRS(D3DRS_ZENABLE, TRUE);
		LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
		if (!GAME.GetMessageBox()->mCurrent)
			LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, mNumPoints, 0, mNumTriangles);
		LT.SSS_NoCache(0, D3DSAMP_MIPMAPLODBIAS, *(DWORD *)&bias);
		SetMipFilterByGlobalToggle(0);
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 1);
		LT.D3D_SetTexture(0, mInfluenceTexture->GetTexture());
		LT.SRS(D3DRS_ZENABLE, TRUE);
		LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		if (!GAME.GetMessageBox()->mCurrent)
			LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, mNumPoints, 0, mNumTriangles);
		if (BATTLELINE_METHOD.Validate())
			BATTLELINE_METHOD.Accept();
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
	}

	if (BATTLELINE_METHOD.Try(0))
	{
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		LT.D3D_SetTexture(0, mHeightTexture->GetTexture());
		mVB->SetStreamSource(0);
		RENDERINFO.Apply();
		LT.GetDevice()->SetIndices(mIB->mIB);
		LT.SRS(D3DRS_ZENABLE, TRUE);
		LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
		if (!GAME.GetMessageBox()->mCurrent)
			LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, mNumPoints, 0, mNumTriangles);
		SetMipFilterNone(0);
		UpdateVertexBuffer(y, true);
		LT.D3D_SetTexture(0, mInfluenceTexture->GetTexture());
		LT.SRS(D3DRS_ZENABLE, TRUE);
		LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		if (!GAME.GetMessageBox()->mCurrent)
			LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, mNumPoints, 0, mNumTriangles);
		if (BATTLELINE_METHOD.Validate())
			BATTLELINE_METHOD.Accept();
		SetMipFilterByGlobalToggle(0);
	}

	LT.SRS(D3DRS_ZENABLE, oldz);
	LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	bias = 0.f;
	LT.SSS_NoCache(1, D3DSAMP_MIPMAPLODBIAS, *(DWORD *)&bias);
	LT.SSS_NoCache(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	SetMipFilterLinear(0);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	if (!GAME.GetMessageBox()->mCurrent)
		RenderOverlay();
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	RENDERINFO.Apply();

	if (!GAME.GetMessageBox()->mCurrent)
	{
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			CPlayer	*player = GAME.GetPlayer(i);
			if (player)
			{
				LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
				CBattleEngine	*be = player->GetBattleEngine();
				if (be)
				{
					FVector	fwd = be->GetOldOrientation().Row[1];
					float	angle = fwd.Azimuth();
					FVector2	s = RadarClamp(FVector2(be->GetPos().X * mScaleX + mOffsetX, be->GetPos().Y * mScaleY + mOffsetY));
					CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD.mViewportWidth - 128.f - HUD_OFFSET_X + s.X + 12.f,
												y - 18.f - s.Y, 0.007f, mEngineMarkerTexture, SA_CENTRE, SPT_NONE,
												1.f, angle, 0xffffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
				}
			}
		}
	}

	RENDERINFO.SetFogEnabled(true);
}

//******************************************************************************************
void	CDXBattleLine::RenderOverlay()	// FUNCTION: 0x0053b470
{
	if (mOverlayLocked && mOverlayVB)
	{
		mOverlayVB->Unlock();
		mOverlayLocked = FALSE;
	}
	if (!mNumOverlayPoints)
		return;

	LT.D3D_SetTexture(0, mMarkerTexture->GetTexture());
	mOverlayVB->SetStreamSource(0);
	LT.SRS(D3DRS_POINTSPRITEENABLE, TRUE);
	LT.SRS(D3DRS_POINTSCALEENABLE, FALSE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
	LT.SRS(D3DRS_TEXTUREFACTOR, 0);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	RENDERINFO.Apply();
	LT.GetDevice()->DrawPrimitive(D3DPT_POINTLIST, 0, mNumOverlayPoints);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	RENDERINFO.Apply();
	LT.GetDevice()->DrawPrimitive(D3DPT_POINTLIST, 0, mNumOverlayPoints);
	LT.SRS(D3DRS_POINTSPRITEENABLE, FALSE);
}

//******************************************************************************************
void	CDXBattleLine::AddOverlayPoint(float x, float y, DWORD colour)	// FUNCTION: 0x0053b5f0
{
	if (GAME.GetMessageBox()->mCurrent)
		return;

	if (!mOverlayLocked && mOverlayVB)
	{
		mOverlayVB->LockRange(0, 0, (void **)&mOverlayData, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK);
		mNumOverlayPoints = 0;
		mOverlayLocked = TRUE;
		float	t = EVENT_MANAGER.GetTime();
		mPulse = int(((float)cos((t - floorf(t)) * PI_M2) + 1.f) * 96.f);
	}
	if (mNumOverlayPoints == 500)
		return;

	int	r = (mPulse << 16) + (colour & 0xff0000);
	if (r > 0xff0000)
		r = 0xff0000;
	int	g = (colour & 0xff00) + (mPulse << 8);
	if (g > 0xff00)
		g = 0xff00;
	int	b = (colour & 0xff) + mPulse;
	if (b > 0xff)
		b = 0xff;

	LT.SRS(D3DRS_POINTSCALEENABLE, FALSE);

	float	top;
	if (GAME.IsMultiplayer())
		top = PLATFORM.GetWindowHeight() * 0.5f + 62.f;
	else
		top = HUD.mViewportY + HUD.mViewportHeight - HUD_OFFSET_Y;

	FVector2	s = RadarClamp(FVector2(x * mScaleX + mOffsetX, y * mScaleY + mOffsetY));

	mOverlayData->X = HUD.mViewportX + HUD.mViewportWidth - 128.f - HUD_OFFSET_X + s.X + 12.f;
	mOverlayData->Y = (top - 18.f) - s.Y;
	mOverlayData->Z = 0.006f;
	mOverlayData->RHW = 0.5f;
	mOverlayData->Colour = b + g + r;
	mOverlayData++;
	mNumOverlayPoints++;
}
