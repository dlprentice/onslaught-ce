// Hud.cpp: the in-game HUD (reconstructed from Hud.obj, 00481310..004881fb; Stuart's release has no copy).
// Functions in retail order.

#include	"Common.h"

#include	"Hud.h"
#include	"DebugLog.h"
#include	"game.h"
#include	"engine.h"
#include	"eventmanager.h"
#define	DXCOMPASS_CPP	// CDXCompass::Render: retail's markers pass calls it (00484340 -> 0053cd30), not CCompass's
#include	"Compass.h"
#undef	DXCOMPASS_CPP
#include	"BattleLine.h"
#include	"ScreenOverlay.h"
#include	"MessageBox.h"
#include	"HelpText.h"
#include	"BattleEngine.h"
#include	"player.h"
#include	"InfluenceMap.h"
#include	"unit.h"
#include	"WorldPhysicsManager.h"
#include	"Camera.h"
#include	"Platform.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"Texture.h"
#include	"text.h"
#include	"DX.h"
#include	"DXEngine.h"
#include	"MessageBox.h"
#include	"TransitionHelpers.h"
#include	"VBufTexture.h"
#include	"world.h"
#include	"RadarWarningReceiver.h"
#include	"triangulate.h"
#include	"feature.h"
#include	"component.h"
#include	"rtmesh.h"
#define IMPOSTER_FULL	// the imposter's frames (RenderTargetIndicator 0048303f)
#include	"imposter.h"
#include	"meshrenderer.h"
#include	"mesh.h"
#include	"viewport.h"
#include	"Lights.h"

extern UINT		MESH_SURFACE_DETAIL;			// 00630130 (MeshRenderer.cpp; 100 unless a mesh is being drawn)
#include	<math.h>

// FUNCTION: 0x00481310 init:ZERO_FVECTOR
// FUNCTION: 0x00481330 init:ID_FMATRIX

// The message panel's layout (0062ce5c..0062ce6c; names ours; used by MessageBox.cpp), then the safe area (0062ce74).
// PLACEMENT BY ADJACENCY (retail address order, not a known original file): these five words sit directly before HUD_SAFE_AREA
// in retail, so the link puts them in this object; MessageBox.cpp's data would land after the memory manager's.
int		gMessageWrapWidth = 26;
float	gMessageBoxY = 23.0f;
float	gMessageBoxX = 10.0f;
float	gMessageBoxWidth = 22.0f;
float	gMessageTextY = 27.0f;

// A non-const float: retail keeps it in .data (a const would be .rdata).
extern float HUD_SAFE_AREA;
float	HUD_SAFE_AREA = 0.85f;		// 0062ce74

float	HUD_OFFSET_X = 0.f;
float	HUD_OFFSET_Y = 0.f;

//******************************************************************************************
CHud::CHud()	// FUNCTION: 0x00481400
{
	mUnknown05c = FALSE;
	mOverlay = NULL;
	mNextOverlay = NULL;
	mCompass = NULL;
	for (int i = 0; i < HUD_NUM_COMPONENTS; i ++)
		mPartState[i] = HCS_ON;
}

//******************************************************************************************
void	CHud::Init()	// FUNCTION: 0x00481450
{
	mUnknown05c = TRUE;
#line 93
	mCompass = new (MEMTYPE_HUD) CDXCompass;
	mCompass->Init();
	mBattleLine = new (MEMTYPE_HUD) CDXBattleLine;
	mBattleLine->Init(-7, 14);

	mText0b4 = TEXT.GetStringById(0x072a0936);
	mText0b8 = TEXT.GetStringById(0x06b91062);
	mText0bc = TEXT.GetStringById(0x085574fc);
	mText0c4 = TEXT.GetStringById(0x002b617f);
	mText0c8 = TEXT.GetStringById(0x002b4ce2);
	mText0c0 = TEXT.GetStringById(0x000396c8);
	mText0cc = TEXT.GetStringById(0x31e12896);
	mText0d0 = TEXT.GetStringById(0x227a52ff);
}

//******************************************************************************************
void	CHud::Reset()	// FUNCTION: 0x004815c0
{
	int i;
	for (i = 0; i < HUD_NUM_COMPONENTS; i ++)
		mPartState[i] = HCS_ON;

	for (i = 0; i < 4; i ++)
	{
		mUnknown064[i].mUnknown0 = -10.f;
		mUnknown064[i].mFadeIn = 0;
	}

	mObjectivePanelOpening = 0;
	mUnknown0a4[0] = -10000.f;
	mUnknown0a4[1] = -10000.f;
	mObjectivePanelWidth = 0;
	mPanelFlash = 0;

	for (i = 0; i < 4; i ++)
	{
		mUnknown0d4[i] = 1.f;
		mUnknown0e4[i] = 1.f;
		mUnknown0f4[i] = 0.f;
		mUnknown190[i] = 0.f;
		mUnknown1b8[i] = 0.f;
		mUnknown1ec[i] = 0.f;
		mUnknown1dc[i] = 0.f;
	}
}

//******************************************************************************************
void	CHud::InitResources()	// FUNCTION: 0x00481650
{
	mCrosshairOutline = CTEXTURE::GetTextureByName("hud\\Crosshair Outline.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mCrosshairEnemy = CTEXTURE::GetTextureByName("hud\\Crosshair Enemy.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mCrosshairFriend = CTEXTURE::GetTextureByName("hud\\Crosshair Friend.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mCrosshairPredictor = CTEXTURE::GetTextureByName("hud\\Crosshair Predictor.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);

	for (int i = 0; i < HUD_NUM_SPEAKERS; i ++)
	{
		char	name[20];
		sprintf(name, "hud\\speaker%1d.tga", i);
		mSpeakers[i] = CTEXTURE::GetTextureByName(name, TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	}

	mScannerBlobSmall = CTEXTURE::GetTextureByName("hud\\ScannerBlobSmall.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mScannerBlobMedium = CTEXTURE::GetTextureByName("hud\\ScannerBlobMedium.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mScannerBlobLarge = CTEXTURE::GetTextureByName("hud\\ScannerBlobLarge.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mScannerBlobRepairPad = CTEXTURE::GetTextureByName("hud\\ScannerBlobRepairPad.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mTargetSighted = CTEXTURE::GetTextureByName("hud\\v3\\hud_target_sighted.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mRadarOutline = CTEXTURE::GetTextureByName("hud\\v2\\RadarOutline.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mBattleLineOutline = CTEXTURE::GetTextureByName("hud\\v2\\BattleLineOutline.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mCircleDarkener = CTEXTURE::GetTextureByName("hud\\v2\\CircleDarkener.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mWeaponOutline = CTEXTURE::GetTextureByName("hud\\v2\\WeaponOutline.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mWeaponFill = CTEXTURE::GetTextureByName("hud\\v2\\WeaponFill.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mCircleMask = CTEXTURE::GetTextureByName("hud\\v2\\CircleMask.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mGunsDarken = CTEXTURE::GetTextureByName("hud\\v2\\GunsDarken.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mGunsOutline = CTEXTURE::GetTextureByName("hud\\v2\\GunsOutline.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mGunsTop = CTEXTURE::GetTextureByName("hud\\v2\\GunsTop.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mGunsFront = CTEXTURE::GetTextureByName("hud\\v2\\GunsFront.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mGunsSide = CTEXTURE::GetTextureByName("hud\\v2\\GunsSide.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mRadioNorth = CTEXTURE::GetTextureByName("hud\\v2\\RadioNorth.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mRadioView = CTEXTURE::GetTextureByName("hud\\v2\\RadioView.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mMenuBackground = CTEXTURE::GetTextureByName("hud\\Menu Background.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mObjectiveLeft = CTEXTURE::GetTextureByName("hud\\v2\\ObjectiveLeft.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mObjectiveRight = CTEXTURE::GetTextureByName("hud\\v2\\ObjectiveRight.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mObjectiveInnerLeft = CTEXTURE::GetTextureByName("hud\\v2\\ObjectiveInnerLeft.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mObjectiveInnerCentre = CTEXTURE::GetTextureByName("hud\\v2\\ObjectiveInnerCentre.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mObjectiveInnerRight = CTEXTURE::GetTextureByName("hud\\v2\\ObjectiveInnerRight.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mWhiteTexture = CTEXTURE::GetTextureByName("hud\\v2\\white.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mCrosshairPrimary = CTEXTURE::GetTextureByName("hud\\v3\\hud_crosshair_primary.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mCrosshairSecondary = CTEXTURE::GetTextureByName("hud\\v3\\hud_crosshair_secondary.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mCrosshairDot = CTEXTURE::GetTextureByName("hud\\v3\\hud_crosshair_dot.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mScreenMarker = CTEXTURE::GetTextureByName("hud\\v3\\ScreenMarker.tga");
	mLockOnPiecesAll = CTEXTURE::GetTextureByName("hud\\v3\\hud_lock_on_pieces_all.tga");
	mTargetLockedOnRed = CTEXTURE::GetTextureByName("hud\\v3\\hud_target_lockedon_red.tga");
	mLockOnPiece1 = CTEXTURE::GetTextureByName("hud\\v3\\hud_lock_on_piece1.tga");
	mLockOnPiece2 = CTEXTURE::GetTextureByName("hud\\v3\\hud_lock_on_piece2.tga");
	mLockOnPiece3 = CTEXTURE::GetTextureByName("hud\\v3\\hud_lock_on_piece3.tga");
	mForsetiIcon = CTEXTURE::GetTextureByName("hud\\ForsetiIcon.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);
	mOffscreenArrow = CTEXTURE::GetTextureByName("hud\\v3\\offscreenarrow.tga", TEXFMT_UNKNOWN, TEX_NORMAL, 1);

	if (mCompass)
		mCompass->LoadTextures();
	if (mBattleLine)
		mBattleLine->InitResources();
}

//******************************************************************************************
BOOL	CHud::PostLoadProcess()	// FUNCTION: 0x00481af0
{
	return mBattleLine->PostLoadProcess();
}

#define SAFE_RELEASE_TEXTURE(t)	if (t) { t->Release(); t = NULL; }

//******************************************************************************************
void	CHud::ShutDown()	// FUNCTION: 0x00481b00
{
	if (mBattleLine)
		mBattleLine->ShutDown();
	if (mCompass)
		mCompass->ReleaseTextures();

	CDXCompass *compass = mCompass;		// deleted as SAFE_DELETE would with an inline destructor calling ReleaseResources
	if (compass)
	{
		compass->ReleaseResources();
		delete compass;
		mCompass = NULL;
	}
	SAFE_DELETE(mBattleLine);

	SAFE_RELEASE_TEXTURE(mCrosshairOutline);
	SAFE_RELEASE_TEXTURE(mCrosshairEnemy);
	SAFE_RELEASE_TEXTURE(mCrosshairFriend);
	SAFE_RELEASE_TEXTURE(mCrosshairPredictor);
	SAFE_RELEASE_TEXTURE(mTargetSighted);
	SAFE_RELEASE_TEXTURE(mRadarOutline);
	SAFE_RELEASE_TEXTURE(mBattleLineOutline);
	SAFE_RELEASE_TEXTURE(mCircleDarkener);
	SAFE_RELEASE_TEXTURE(mWeaponOutline);
	SAFE_RELEASE_TEXTURE(mWeaponFill);
	SAFE_RELEASE_TEXTURE(mCircleMask);
	SAFE_RELEASE_TEXTURE(mGunsDarken);
	SAFE_RELEASE_TEXTURE(mGunsOutline);
	SAFE_RELEASE_TEXTURE(mGunsTop);
	SAFE_RELEASE_TEXTURE(mGunsFront);
	SAFE_RELEASE_TEXTURE(mGunsSide);
	SAFE_RELEASE_TEXTURE(mRadioNorth);
	SAFE_RELEASE_TEXTURE(mRadioView);
	SAFE_RELEASE_TEXTURE(mMenuBackground);
	SAFE_RELEASE_TEXTURE(mObjectiveLeft);
	SAFE_RELEASE_TEXTURE(mObjectiveRight);
	SAFE_RELEASE_TEXTURE(mObjectiveInnerLeft);
	SAFE_RELEASE_TEXTURE(mObjectiveInnerCentre);
	SAFE_RELEASE_TEXTURE(mObjectiveInnerRight);
	SAFE_RELEASE_TEXTURE(mWhiteTexture);
	SAFE_RELEASE_TEXTURE(mCrosshairPrimary);
	SAFE_RELEASE_TEXTURE(mCrosshairSecondary);
	SAFE_RELEASE_TEXTURE(mCrosshairDot);
	SAFE_RELEASE_TEXTURE(mScreenMarker);
	SAFE_RELEASE_TEXTURE(mScannerBlobSmall);
	SAFE_RELEASE_TEXTURE(mScannerBlobMedium);
	SAFE_RELEASE_TEXTURE(mScannerBlobLarge);
	SAFE_RELEASE_TEXTURE(mScannerBlobRepairPad);
	SAFE_RELEASE_TEXTURE(mLockOnPiecesAll);
	SAFE_RELEASE_TEXTURE(mTargetLockedOnRed);
	SAFE_RELEASE_TEXTURE(mLockOnPiece1);
	SAFE_RELEASE_TEXTURE(mLockOnPiece2);
	SAFE_RELEASE_TEXTURE(mLockOnPiece3);
	SAFE_RELEASE_TEXTURE(mForsetiIcon);
	SAFE_RELEASE_TEXTURE(mOffscreenArrow);

	for (int i = 0; i < HUD_NUM_SPEAKERS; i ++)
		SAFE_RELEASE_TEXTURE(mSpeakers[i]);
}

//******************************************************************************************
void	CHud::SetHudComponent(char *name, bool now)	// FUNCTION: 0x00481f40
{
	if (mNextOverlay)
	{
		mNextOverlay->Finish();
		mNextOverlay = NULL;
	}

	if (now)
	{
		if (mOverlay)
		{
			mOverlay->Finish();
			mOverlay = NULL;
		}
#line 311
		mOverlay = new (MEMTYPE_HUD) CScreenOverlay(name);
	}
	else
	{
#line 315
		mNextOverlay = new (MEMTYPE_HUD) CScreenOverlay(name);
	}
}

//******************************************************************************************
void	CHud::SwitchInOverlay()	// FUNCTION: 0x00482050
{
	if (mNextOverlay)
	{
		delete mOverlay;
		mOverlay = mNextOverlay;
		mNextOverlay = NULL;
	}
}

//******************************************************************************************
void	CHud::SetOverlayRenderStates()	// FUNCTION: 0x00482090
{
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	SetMipFilterNone(0);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ALPHAREF, 8);
	RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, FALSE);
	RENDERINFO.SRS(D3DRS_ZENABLE, FALSE);
	RENDERINFO.Apply();
}

//******************************************************************************************
void	CHud::SetAdditive()	// FUNCTION: 0x004821b0
{
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
	RENDERINFO.Apply();
}

//******************************************************************************************
void	CHud::SetAlphaBlend()	// FUNCTION: 0x004821e0
{
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.Apply();
}

//******************************************************************************************
// a framed bar centred on (x, y): the ends, then the middle tiled; while it opens (0.5 < open < 1) only the ends
// show, turning and fading in
// The frame is retail's through these spellings: `left` and the running position `px` are two variables (two stores of
// x - half; a chain would copy px from left and the later reads of px would become reads of left), px also carries the
// right ends' position and is set back to left after n, and each branch's `scale` is its own variable (the second one
// shares height's home, the first sits in a slot of its own).
void	CHud::RenderObjectiveBar(float x, float y, float width, float height, float open)	// FUNCTION: 0x00482210
{
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.Apply();

	if (height < 10.f)
		height = 10.f;

	if (open == 1.f)
	{
		float scale = height * (1.f / 64.f);
		float half = width * 0.5f;
		float left = x - half;
		float px = x - half;
		CSPRITERENDERER::DrawSprite(px, y, 0.009f, mObjectiveLeft, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xffffffff, scale, scale, 0.f, 1.f, 0.f, 1.f);
		CSPRITERENDERER::DrawSprite(px, y, 0.009f, mObjectiveInnerLeft, (ESpriteAlignment)7, SPT_NONE, 1.f, 0.f, 0x90000000, scale, scale, 0.f, 1.f, 0.f, 1.f);
		px = half + x;
		CSPRITERENDERER::DrawSprite(px, y, 0.009f, mObjectiveRight, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xffffffff, scale, scale, 0.f, 1.f, 0.f, 1.f);
		CSPRITERENDERER::DrawSprite(px, y, 0.009f, mObjectiveInnerRight, SA_8, SPT_NONE, 1.f, 0.f, 0x90000000, scale, scale, 0.f, 1.f, 0.f, 1.f);
		float segment = scale * 64.f;
		float n = width / segment;
		px = left;
		while (n >= 1.f)
		{
			CSPRITERENDERER::DrawSprite(px, y, 0.009f, mObjectiveInnerCentre, SA_8, SPT_NONE, 1.f, 0.f, 0x90000000, scale, scale, 0.f, 1.f, 0.f, 1.f);
			n -= 1.f;
			px += segment;
		}
		if (n > 0.f)
			CSPRITERENDERER::DrawSprite(px, y, 0.009f, mObjectiveInnerCentre, SA_8, SPT_FILL_RIGHT, n, 0.f, 0x90000000, scale, scale, 0.f, 1.f, 0.f, 1.f);
	}
	else if ((open > 0.5f) && (open < 1.f))
	{
		float scale = height * open * (1.f / 64.f);
		int alpha = int(open * 255.f);
		if (alpha < 0)
			alpha = 0;
		else if (alpha > 255)
			alpha = 255;
		DWORD col = ((alpha * 255) << 16) | 0xffffff;
		float turn = 1.f - open;
		CSPRITERENDERER::DrawSprite(x, y, 0.009f, mObjectiveLeft, SA_CENTRE, SPT_NONE, 1.f, turn, col, scale, scale, 0.f, 1.f, 0.f, 1.f);
		CSPRITERENDERER::DrawSprite(x, y, 0.009f, mObjectiveRight, SA_CENTRE, SPT_NONE, 1.f, turn, col, scale, scale, 0.f, 1.f, 0.f, 1.f);
	}
}

//******************************************************************************************
// the target panel's height: the second player's sits half way down the screen (retail falls through to the first
// player's, 00482698..004826b0)
static inline float	TargetPanelY(BOOL second)
{
	if (!second)
		return HUD_OFFSET_Y + 42.f;
	return PLATFORM.GetWindowHeight() * 0.5f + HUD_OFFSET_Y + 40.f;
}

//******************************************************************************************
// maths.h's FMatrix::MakeRotationF as a file-local copy, Row[0].X's brackets cut to one and two products bracketed:
// see the stand-in note on CHud::RenderTargetIndicator
static inline void	MakeRotF(FMatrix &m, float yaw, float pitch, float roll)
{
	float cy = cosf(yaw), sy = sinf(yaw);
	float cr = cosf(roll), sr = sinf(roll);
	float cp = cosf(pitch), sp = sinf(pitch);
	m.Row[0].X = (cr * cy - sp * sr * sy);	m.Row[0].Y = (((-cp * sy)));	m.Row[0].Z = ((sp * cr * sy + (sr * cy)));
	m.Row[1].X = ((sp * sr * cy + (cr * sy)));	m.Row[1].Y = (((cp * cy)));		m.Row[1].Z = (sr * sy - sp * cr * cy);
	m.Row[2].X = (-cp * sr);				m.Row[2].Y = sp;			m.Row[2].Z = (cp * cr);
}

//******************************************************************************************
// The target view's tilt: an FMatrix whose MakeRot is maths.h's MakeRotationF with Row[1].Z's first product bracketed
// (a labelled stand-in, names ours; see the stand-in note on CHud::RenderTargetIndicator).
struct FHudMatrix : public FMatrix
{
	void	MakeRot(float yaw, float pitch, float roll)
	{
		float cy = cosf(yaw), sy = sinf(yaw);
		float cr = cosf(roll), sr = sinf(roll);
		float cp = cosf(pitch), sp = sinf(pitch);
		Row[0].X = (((cr * cy - sp * sr * sy)));	Row[0].Y = (((-cp * sy)));	Row[0].Z = ((sp * cr * sy + sr * cy));
		Row[1].X = ((sp * sr * cy + cr * sy));	Row[1].Y = (((cp * cy)));		Row[1].Z = ((sr * sy) - sp * cr * cy);
		Row[2].X = (-cp * sr);				Row[2].Y = sp;			Row[2].Z = (cp * cr);
	}
};

//******************************************************************************************
// the saved matrices go back directly (as CFEPGoodies::Render's projection, 0045f746)
static inline void	RestoreView(D3DMATRIX *m)
{
	RENDERINFO.mView = *(SRenderMatrix *)m;
	RENDERINFO.mViewChanged = true;
}
static inline void	RestoreProjection(D3DMATRIX *m)
{
	RENDERINFO.mProjection = *(SRenderMatrix *)m;
	RENDERINFO.mProjectionChanged = true;
}

//******************************************************************************************
// the target panel: the unit under the crosshair (or last targeted, for two seconds) on a backing of its side's
// colour, turning slowly in its own lit viewport (the Thunderhead drawn from its imposter instead), with its health
// and, for a component, the component's
// The sprites after the model are drawn by one call per player: retail pushes the other arguments, then tests second
// and computes y in each arm (the imposter 00483115/0048315a, the health bar 004832e1/00483345), and VC6 evaluates a
// conditional argument (TargetPanelY's if, or a ?:) before any push, so one shared call cannot give that order.
// The unit found this frame and the target shown are two variables: the found unit is remembered, then copied
// (retail's `mov ebx,esi` after the remembering call, 004825f4). With one variable VC6 split it at the inner tests,
// kept a zero register (esi) for the null tests and put second in ebx. The side is chosen with else-if, so VC6
// threads both constant arms past the jump table's range test (00482717). The lights' directions are set with Set and
// normalised with Normalise (the folded sqrt(3.0) is shared, 00482825; the first works on a copy of it, 004828f9), and
// their colours are set red, green, blue. The viewport's position is set per player and then halved with -=
// (cdq/sar/neg/add, as CGame::Render's `viewport.X -= fullscreen.Width/2`); VC6 hoists the shared fld/fadd ahead of
// the test (00482a0c). The matrices are saved with RENDERINFO's GetView/GetProjection and put back directly (the
// helpers above, as CFEPGoodies::Render's projection). The imposter's texture is a local loaded with the frame (edx,
// 00483050), so both arms pick the same scratch registers and VC6's head merge hoists their 13 shared pushes
// (004830b9..004830ee). The bars' heights are bracketed, `(HUD_OFFSET_Y + 42.f) + 28.f`, which keeps the +28 a
// separate add (the merged tail at 004831bf).
// STAND-IN (budget): the second player's calls and the viewport read mWhiteTexture, the
// first player's calls and the backing read GetWhiteTexture(). An inline accessor in a statement that also calls
// GetWindowHeight is evaluated at the head of that arm, which stops the head merge (retail hoists 6, 11 and 8 shared
// pushes at the health and component bars, 00483283, 0048335d, 0048345d; checked on a synthetic copy), so the second
// player's calls cannot use it. The first player's four accessor sites and the two restore helpers are inline sites
// after the view product: with them its nested budget is 197 (FMatrix(row, row, row) stays out of line below 213,
// 00482dc8) and CLight's is 138 (its three Sets inline from 132). Every read is used; the split is chosen for the
// budget.
// STAND-IN (scheduling): the turn matrix is built by MakeRotF above, maths.h's
// MakeRotationF with Row[0].X's brackets cut to one and two products bracketed (as CFEPWingmen::Render's copy). The
// brackets emit no code: their no-code x87 nodes move the 81-node scheduler region cuts, so the alpha-blend SRS's
// pushes follow the rotation's first sum (00482ee0) and the x87 temporaries match. Against maths.h's body the
// permuter shape goes 34/34/1217 -> 20/20/49.
// STAND-IN (scheduling): ori is an FHudMatrix (above), whose MakeRot is
// maths.h's MakeRotationF with Row[1].Z's first product bracketed, `((sr * sy) - sp * cr * cy)`. The bracket emits no
// code: its no-code x87 node takes cycle 4 of scheduler region 61, so the transpose's Row[0].Y = Row[1].X load moves to
// 5 and its store ties with Row[1].Z's fsubp at 6 and loses on priority, retail's order (00482ca2). In maths.h the
// bracket costs 13 other matches. As a member the copy keeps the member body's operand ranks; a free-function copy
// (as MakeRotF) flips two of the view product's sums (00482cf3, 00482d4d).
// Reconstruction (name ours): 0067a634 is a bss word that nothing in retail's .text writes; this function is its only
// reader and drops all three loads (0048259c, 004825dd, 00482646). A volatile read is the only construct that keeps a
// discarded load.
static volatile SINT	sUnknown67a634 = 0;

void	CHud::RenderTargetIndicator()	// FUNCTION: 0x00482590
{
	sUnknown67a634;
	CUnit	*unit = (CUnit *)mBattleEngine->GetAutoAimTarget();
	if ((!unit) || (!(unit->GetThingType() & THING_TYPE_UNIT)))
	{
		unit = mBattleEngine->GetUnitOverCrossHair();
		if ((unit) && (!(unit->GetThingType() & THING_TYPE_UNIT)))
			unit = NULL;
	}
	BOOL	second = (mBattleEngine->GetPlayer()->GetNumber() != 1);
	sUnknown67a634;
	CUnit	*target;
	if (unit)
	{
		mUnknown09c[second] = unit;
		target = unit;
		mUnknown0a4[second] = PLATFORM.GetSysTimeFloat();
	}
	else
	{
		if (PLATFORM.GetSysTimeFloat() - mUnknown0a4[second] > 2.f)
			mUnknown09c[second] = NULL;
		target = mUnknown09c[second].ToRead();
	}
	sUnknown67a634;
	if (!target)
		return;
	CUnit	*component = NULL;
	if (target->GetThingType() & THING_TYPE_COMPONENT)
	{
		component = target;
		target = ((CComponent *)target)->mParent.ToRead();
	}
	if (!target)
		return;
	CRenderThing	*rt = target->GetRenderThing();
	if ((!rt) || (!rt->GetRTMesh()))
		return;

	DWORD	col = 0x27ffffff;
	float	y = TargetPanelY(second);
	SINT	side;
	if ((!target->GetUnitData()) || ((side = target->GetUnitData()->mIcon) == -1))
	{
		if (target->GetAllegiance() == kForsetiAllegiance)
			side = 1;
		else if (target->GetAllegiance() == kMuspellAllegiance)
			side = 0;
		else
			side = 2;
	}
	DWORD	back;
	switch (side)
	{
	case 0:	back = 0x3fff0000;	break;
	case 1:	back = 0x3f0000ff;	break;
	case 2:	back = 0x3f444444;	break;
	case 3:	back = 0x3f00ff00;	break;
	}
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, TRUE);
	CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, y, 1.f, GetWhiteTexture(), SA_CENTRE, SPT_NONE, 1.f, 0.f, back, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	CDXImposter	*imposter = ((CRTMesh *)rt)->mImposter;
	CMesh		*mesh = rt->GetRTMesh();
	if (stricmp(mesh->GetName(), "m_thunderhead.msh"))	// every mesh but the Thunderhead's (004827f2..00482809)
	{
		SINT	n;
		for (n = 0; n < 8; n++)
			RENDERINFO.SetLightEnable(n, false);
		RENDERINFO.SetAmbient(0xff606060);

		CLight	light;
		light.mType = LIGHT_DIRECTIONAL;
		light.mDir.Set(1.f, 1.f, -1.f);
		light.mDir.Normalise();
		light.mDiffuseR = 40.f / 255.f;
		light.mDiffuseG = 40.f / 255.f;
		light.mDiffuseB = 40.f / 255.f;
		RENDERINFO.SetLight(0, &light);
		RENDERINFO.SetLightEnable(0, true);
		light.mType = LIGHT_DIRECTIONAL;
		light.mDir.Set(-1.f, -1.f, 1.f);
		light.mDir.Normalise();
		light.mDiffuseR = 0;
		light.mDiffuseG = 0;
		light.mDiffuseB = 0;
		RENDERINFO.SetLight(1, &light);
		RENDERINFO.SetLightEnable(1, true);
		RENDERINFO.EnableLighting(true);

		CViewport	old = ENGINE.GetCurrentViewport();
		CViewport	vp;
		vp.Width = mWhiteTexture->mWidth;
		vp.Height = mWhiteTexture->GetHeight();
		if (!second)
		{
			vp.X = (SINT)(HUD_OFFSET_X + 42.f);
			vp.Y = (SINT)(HUD_OFFSET_Y + 42.f);
		}
		else
		{
			vp.X = (SINT)(HUD_OFFSET_X + 42.f);
			vp.Y = (SINT)(PLATFORM.GetWindowHeight() * 0.5f + HUD_OFFSET_Y + 40.f);
		}
		vp.X -= vp.Width / 2;
		vp.Y -= vp.Height / 2;
		vp.MinZ = 0;
		vp.MaxZ = 1.f;
		PLATFORM.SetViewport(&vp);
		D3DMATRIX	projection;
		RENDERINFO.GetProjection(&projection);
		RENDERINFO.SetProjection(0.2f, 100.f, 0.2f, 0.2f);

		float	r = mesh->mBoundingBox->mRadius;
		if (r < 0.5f)
			r = 0.5f;
		FVector	eye(0, r * -1.5f, r * -0.4f);
		D3DMATRIX	view;
		RENDERINFO.GetView(&view);
		FMatrix	pitch;
		pitch.MakeRotationPitchF(1.570796f);
		FHudMatrix	ori;
		ori.MakeRot(0.f, 0.5f, 0.f);
		ori.TransposeInPlace();
		FMatrix	look = pitch * ori;
		RENDERINFO.SetView(&look, &eye);

		FVector	pos = ZERO_FVECTOR;
		pos.X = -mesh->mBoundingBox->mOrigin.X;
		pos.Y = -mesh->mBoundingBox->mOrigin.Y;
		pos.Z = -mesh->mBoundingBox->mOrigin.Z;
		FMatrix	turn = ID_FMATRIX;
		MakeRotF(turn, ENGINE.GetRenderTime() / 10.0f, 0.f, 0.f);
		RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
		MESH_SURFACE_DETAIL = 0;
		CMeshRenderer::SetRenderMode(R_Normal);
		CMeshRenderer::Render(&pos, &turn, mesh, NULL, NULL, 0, false, 0);
		MESH_SURFACE_DETAIL = 100;
		RestoreView(&view);
		PLATFORM.SetViewport(&old);
		RestoreProjection(&projection);
		RENDERINFO.EnableLighting(false);
		RENDERINFO.SetFogEnabled(false);
		RENDERINFO.SRS(D3DRS_ZENABLE, TRUE);
	}
	else if ((imposter) && (((CDXImposter *)imposter)->mFrames))
	{
		SImposterFrame	*frame = &((CDXImposter *)imposter)->mFrames[1];
		if (frame)
		{
			CDXTexture	*tex = CDXImposter::sTexture;
			float	u1 = frame->mU1, u0 = frame->mU0, v1 = frame->mV1, v0 = frame->mV0;
			float	sx = u1 - u0;
			float	sy = v1 - v0;
			if (frame->mWidth > frame->mHeight)
				sy *= frame->mHeight / frame->mWidth;
			else
				sx *= frame->mWidth / frame->mHeight;
			col = 0x3fffffff;
			if (!second)
				CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, HUD_OFFSET_Y + 42.f, 0.011f, tex, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, sx, sy, u0, u1, v0, v1);
			else
				CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, PLATFORM.GetWindowHeight() * 0.5f + HUD_OFFSET_Y + 40.f, 0.011f, tex, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, sx, sy, u0, u1, v0, v1);
		}
	}

	if (!second)
		CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, (HUD_OFFSET_Y + 42.f) + 28.f, 0.011f, GetWhiteTexture(), SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xff000000, 0.9f, 0.07f, 0.f, 1.f, 0.f, 1.f);
	else
		CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, (PLATFORM.GetWindowHeight() * 0.5f + HUD_OFFSET_Y + 40.f) + 28.f, 0.011f, mWhiteTexture, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xff000000, 0.9f, 0.07f, 0.f, 1.f, 0.f, 1.f);
	if (!(target->GetMaxLife() > 0.0001f))
		return;
	float	health = target->GetLifeUnknown4f9a40() / target->GetMaxLife();
	if (health < 0)
		health = 0;
	else if (health > 1.f)
		health = 1.f;
	switch (side)
	{
	case 0:	col = 0xffcf5555;	break;
	case 1:	col = 0xff5555cf;	break;
	case 2:	col = 0xff777777;	break;
	case 3:	col = 0xff55cf55;	break;
	}
	if (!second)
		CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, (HUD_OFFSET_Y + 42.f) + 28.f, 0.011f, GetWhiteTexture(), SA_CENTRE, SPT_FILL_RIGHT, health, 0.f, col, 0.9f, 0.07f, 0.f, 1.f, 0.f, 1.f);
	else
		CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, (PLATFORM.GetWindowHeight() * 0.5f + HUD_OFFSET_Y + 40.f) + 28.f, 0.011f, mWhiteTexture, SA_CENTRE, SPT_FILL_RIGHT, health, 0.f, col, 0.9f, 0.07f, 0.f, 1.f, 0.f, 1.f);
	if (!component)
		return;
	if (!second)
		CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, (HUD_OFFSET_Y + 42.f) + 21.f, 0.011f, GetWhiteTexture(), SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xff000000, 0.9f, 0.07f, 0.f, 1.f, 0.f, 1.f);
	else
		CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, (PLATFORM.GetWindowHeight() * 0.5f + HUD_OFFSET_Y + 40.f) + 21.f, 0.011f, mWhiteTexture, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xff000000, 0.9f, 0.07f, 0.f, 1.f, 0.f, 1.f);
	if (!(component->GetMaxLife() > 0.0001f))
		return;
	health = component->GetLife() / component->GetMaxLife();
	if (health < 0)
		health = 0;
	else if (health > 1.f)
		health = 1.f;
	if (!second)
		CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, (HUD_OFFSET_Y + 42.f) + 21.f, 0.011f, GetWhiteTexture(), SA_CENTRE, SPT_FILL_RIGHT, health, 0.f, col, 0.9f, 0.07f, 0.f, 1.f, 0.f, 1.f);
	else
		CSPRITERENDERER::DrawSprite(HUD_OFFSET_X + 42.f, (PLATFORM.GetWindowHeight() * 0.5f + HUD_OFFSET_Y + 40.f) + 21.f, 0.011f, mWhiteTexture, SA_CENTRE, SPT_FILL_RIGHT, health, 0.f, col, 0.9f, 0.07f, 0.f, 1.f, 0.f, 1.f);
}

// STAND-IN (accessor view): retail reads the world's text slots through inline CWorld member
// accessors. Its slot loops step the slot's byte offset with &WORLD as the displacement (0048355a `cmp
// [eax+0x855090],ebx`; the final loop's offset runs to 0x25c, 004842ee), which only an inlined member accessor gives:
// direct members, pointer or reference locals and free helpers walk an absolute pointer instead. Stuart's world.h
// probably declared them; this file-local view supplies GetTextState, GetText and a by-reference GetTextDuration (by
// value its temporaries grow the frame) without changing the shared header. Each call reads the member it names.
struct FHudWorld : public CWorld
{
	SINT	GetTextState(SINT slot) { return mTextState[slot]; }
	WCHAR	*GetText(SINT slot) { return mText[slot]; }
	float	&GetTextDuration(SINT slot) { return mTextDuration[slot]; }
};

//******************************************************************************************
// the objective panel: it opens while any world text slot is in use and closes (narrowing first) when none is; each
// slot fades in and out, and a full slot shows its label and value (a count, a percentage, a clock, or a count and its
// target), fading from white as the value changes and flashing when a target is (nearly) reached
// Matched with the two labelled stand-ins (the view above, the zero below). What shapes it:
// - a holds the alpha already shifted: retail shifts it as soon as it is converted (`mov edi,[esp+0x10]; shl edi,8`,
//   00483a96, 00483f50) and keeps it in edi until col3 (00484218). Shifted at each use, `a << 8` is a CSE temporary
//   that VC6 splits into a frame slot (frame 0x338).
// - col2's flash arms recompute a and gg in place. In the 2-or-5 branch retail stores the new green into gg's home
//   (0048409f) and leaves the new alpha in edi for col3's other arm (00484082, 00484218). With those definitions a's
//   priority (198) puts it before both col2s and col3, so it takes edi; the 2-or-5 col2 and col3 take ebp, the other
//   col2 edi, and the slot offset is left only ebx. That colouring splits y, this, gg, r and the slot pointer as retail
//   (y loaded into ebp at 00483af9, this and the slot pointer reloaded at 004842c8). The other branch's arm is the same
//   text; its values are dead after it.
// - The bar's height reuses total (`total += 5.f`): retail keeps total, the height and f in one frame slot (00483774,
//   004838b2..004838bc, 00483a80). A separate height local joins r's slot instead and moves five others (the packer
//   replay of a slot-packer builds retail's classes and layout exactly with height merged into total).
// - The flashing colours reuse t for mPanelFlash: retail copies it into t's memory home with integer moves and reloads
//   it per channel (00483f18..00483f4c).
// - The target is (float)(W - (W - 75)), retail's (75 - W1) + W2 (0048381c); the bar's x is a local before the height;
//   the final loop steps a slot pointer beside i (00483954); case 5 reads the time into a local before the test, which
//   compares the gap with a tenth of the target as fabsf(time - target) <= ... (fcompp and jne, 00483f0b). Signed font
//   heights and active-slot measurement follow retail.
// STAND-IN (late zero): `float zero = 0;` at the top, added to two kinds of operand. It emits no code:
// C2 folds `x + zero` only after its global optimizer (RenderTargetMarkers below).
// - The text width at the four DrawTextDynamic calls, `(float)(W - k) - (size.cx + zero)`: retail converts the width
//   on its own (`fild [size.cx]; fsubp`, 00483b35, 00484174, 004842ab) where `- size.cx` fuses into fisub; casts,
//   doubles, operand orders, width locals and helpers all fuse.
// - Case 5's duration, read twice as `(GetTextDuration(i) + zero)`: the two sums become one CSE temporary that the x87
//   allocator cannot keep, so it is copied to a frame temporary with integer moves and read twice, as retail
//   (00483ef1..00483f05); with GetTextTime inside the expression the copy is scheduled before the call.
// Plain source (no zero): 41/261 shape/instructions in mvar; without the size.cx zeros 22/22, without case 5's 32/252.
// Selector correction (2026-10-08): pristine BEA.exe.original.backup, SHA-256
// 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750.
// DEC EAX / CMP EAX,5 / JA at 00483a3d..00483a41 guards the six entries at 00484324:
// states 1..6 are integer, absolute count/target, clock, percentage, relative count/target,
// fractional clock.
void	CHud::Render00483530()	// FUNCTION: 0x00483530
{
	float	zero = 0;	// STAND-IN (see above)
	SINT	i;
	BOOL	narrowing = FALSE;
	BOOL	done = FALSE;
	if (mObjectivePanelOpening != 1.f)
	{
		for (i = 0; i < MAX_WORLD_TEXT_SLOTS; i++)
			if (((FHudWorld &)WORLD).GetTextState(i))
			{
				mObjectivePanelOpening += 0.1f;
				if (mObjectivePanelOpening > 1.f)
					mObjectivePanelOpening = 1.f;
				done = TRUE;
				break;
			}
	}
	if ((!done) && (mObjectivePanelOpening != 0))
	{
		BOOL	active = FALSE;
		for (i = 0; i < MAX_WORLD_TEXT_SLOTS; i++)
			if (((FHudWorld &)WORLD).GetTextState(i))
			{
				active = TRUE;
				break;
			}
		if (!active)
		{
			if (mObjectivePanelWidth > 0)
			{
				mObjectivePanelWidth += (0 - mObjectivePanelWidth) * 0.3f;
				if (fabsf(0 - mObjectivePanelWidth) < 0.5f)
					mObjectivePanelWidth = 0;
				narrowing = TRUE;
			}
			else
			{
				mObjectivePanelOpening -= 0.1f;
				if (mObjectivePanelOpening < 0)
					mObjectivePanelOpening = 0;
				done = TRUE;
			}
		}
	}
	if (!done)
	{
		for (i = 0; i < MAX_WORLD_TEXT_SLOTS; i++)
			if ((!((FHudWorld &)WORLD).GetTextState(i)) && (mUnknown064[i].mFadeIn != 0))
			{
				mUnknown064[i].mFadeIn -= GAME.GetRenderFrameLength() / 5.0f;
				if (mUnknown064[i].mFadeIn < 0)
					mUnknown064[i].mFadeIn = 0;
				done = TRUE;
				break;
			}
		if (!done)
		{
			for (i = 0; i < MAX_WORLD_TEXT_SLOTS; i++)
				if ((((FHudWorld &)WORLD).GetTextState(i)) && (mUnknown064[i].mFadeIn < 1.f))
				{
					mUnknown064[i].mFadeIn += GAME.GetRenderFrameLength() / 5.0f;
					if (mUnknown064[i].mFadeIn > 1.f)
					{
						mUnknown064[i].mFadeIn = 1.f;
						mUnknown064[i].mLastValue = -1.f;
					}
					break;
				}
		}
	}
	if (mObjectivePanelOpening != 0)
	{
		if (mObjectivePanelOpening < 1.f)
			RenderObjectiveBar((float)(PLATFORM.GetWindowWidth() - 90), 40.f, 0, 0, mObjectivePanelOpening);
		else
		{
			float	y = 43.f;
			float	total = 0;
			float	widest = 0;
			SIZE	size;
			for (i = 0; i < MAX_WORLD_TEXT_SLOTS; i++)
			{
				total += PLATFORM.Font(FONT_SMALL)->GetHeight() * mUnknown064[i].mFadeIn;
				if (((FHudWorld &)WORLD).GetTextState(i))
				{
					PLATFORM.Font(FONT_SMALL)->GetTextExtent(((FHudWorld &)WORLD).GetText(i), &size);
					if (size.cx > widest)
						widest = (float)size.cx;
				}
			}
			if ((total != 0) && (!narrowing))
			{
				float	target = (float)(PLATFORM.GetWindowWidth() - (PLATFORM.GetWindowWidth() - 75)) + widest - 5.f;
				mObjectivePanelWidth += (target - mObjectivePanelWidth) * 0.3f;
				if (fabsf(target - mObjectivePanelWidth) < 0.5f)
					mObjectivePanelWidth = target;
			}
			float	width = mObjectivePanelWidth;
			float	x = (float)(PLATFORM.GetWindowWidth() - 90) - width * 0.5f;
			total += 5.f;
			RenderObjectiveBar(x, total * 0.6f + 40.f, width, total, mObjectivePanelOpening);

			float	step = GAME.GetRenderFrameLength() * 0.1f;
			if (mPanelBrightening)
			{
				mPanelFlash += step;
				if (mPanelFlash > 1.f)
				{
					mPanelFlash = 1.f;
					mPanelBrightening = FALSE;
				}
			}
			else
			{
				mPanelFlash -= step;
				if (mPanelFlash < 0.4f)
				{
					mPanelFlash = 0.4f;
					mPanelBrightening = TRUE;
				}
			}

			SHudPlayerMarker	*slot;
			for (i = 0, slot = mUnknown064; i < MAX_WORLD_TEXT_SLOTS; i++, slot++)
			{
				if ((slot->mFadeIn == 1.f) && (((FHudWorld &)WORLD).GetTextState(i)))
				{
					if (WORLD.GetTextTime(i) != slot->mLastValue)
					{
						slot->mLastValue = WORLD.GetTextTime(i);
						slot->mUnknown0 = EVENT_MANAGER.GetTime();
					}
					float	t = EVENT_MANAGER.GetTime() - slot->mUnknown0;
					if (t < 0)
						t = 0;
					else if (t > 1.f)
						t = 1.f;
					if (((FHudWorld &)WORLD).GetTextState(i) == 3)
						t = 1.f;
					PLATFORM.Font(FONT_SMALL)->GetTextExtent(((FHudWorld &)WORLD).GetText(i), &size);
					if (!(size.cx - 5.f > mObjectivePanelWidth))
					{
						char	value[256], count[256], goal[256];
						BOOL	flash = FALSE;
						switch (((FHudWorld &)WORLD).GetTextState(i))
						{
						case 1:
							sprintf(value, "%d", (SINT)WORLD.GetTextTime(i));
							break;
						case 4:
							sprintf(value, "%d%", (SINT)WORLD.GetTextTime(i));
							break;
						case 3:
							sprintf(value, "%d%d:%d%d", (SINT)WORLD.GetTextTime(i) / 600, ((SINT)WORLD.GetTextTime(i) / 60) % 10, ((SINT)WORLD.GetTextTime(i) / 10) % 6, (SINT)WORLD.GetTextTime(i) % 10);
							break;
						case 6:
							sprintf(value, "%d%d:%d%d.%d%d", (SINT)WORLD.GetTextTime(i) / 600, ((SINT)WORLD.GetTextTime(i) / 60) % 10, ((SINT)WORLD.GetTextTime(i) / 10) % 6, (SINT)WORLD.GetTextTime(i) % 10, (SINT)(WORLD.GetTextTime(i) * 10.f) % 10, (SINT)(WORLD.GetTextTime(i) * 100.f) % 10);
							break;
						case 2:
							sprintf(value, "%d (%d)", (SINT)WORLD.GetTextTime(i), (SINT)((FHudWorld &)WORLD).GetTextDuration(i));
							sprintf(count, "%d ", (SINT)WORLD.GetTextTime(i));
							sprintf(goal, "(%d)", (SINT)((FHudWorld &)WORLD).GetTextDuration(i));
							if (fabsf(WORLD.GetTextTime(i) - ((FHudWorld &)WORLD).GetTextDuration(i)) <= 1.f)
								flash = TRUE;
							break;
						case 5:
							sprintf(value, "%d (%d)", (SINT)WORLD.GetTextTime(i), (SINT)((FHudWorld &)WORLD).GetTextDuration(i));
							sprintf(count, "%d ", (SINT)WORLD.GetTextTime(i));
							sprintf(goal, "(%d)", (SINT)((FHudWorld &)WORLD).GetTextDuration(i));
							float	time = WORLD.GetTextTime(i);
							if (fabsf(time - (((FHudWorld &)WORLD).GetTextDuration(i) + zero)) <= (((FHudWorld &)WORLD).GetTextDuration(i) + zero) / 10.0f)	// STAND-IN (see above)
								flash = TRUE;
							break;
						}

						DWORD	col;
						float	f;
						SINT	a, r, gg;
						if (flash)
						{
							t = mPanelFlash;
							f = (1.f - t) * 255.f;
							a = (SINT)(t * 255.f + f) << 8;
							r = (SINT)(t * 95.f + f);
							gg = (SINT)(t * 64.f + f);
							col = ((((a + r) << 8) + gg) << 8) - (SINT)(t * -48.f);
						}
						else
						{
							f = (1.f - t) * 255.f;
							a = (SINT)(t * 255.f + f) << 8;
							r = (SINT)(t * 95.f + f);
							gg = (SINT)(t * 64.f + f);
							col = (((a + r) << 8) + gg) << 8;
							col += (SINT)(t * 48.f + f);
						}
						PLATFORM.Font(FONT_SMALL)->DrawTextDynamic((float)(PLATFORM.GetWindowWidth() - 165) - (size.cx + zero), y, 0.008f, 1.f, 1.f, col, ((FHudWorld &)WORLD).GetText(i), 1.f, 1, 0);	// STAND-IN (see above)

						if ((((FHudWorld &)WORLD).GetTextState(i) == 2) || (((FHudWorld &)WORLD).GetTextState(i) == 5))
						{
							DWORD	col2;
							if (flash)
							{
								t = mPanelFlash;
								f = (1.f - t) * 255.f;
								a = (SINT)(t * 255.f + f) << 8;
								gg = (SINT)(t * 64.f + f);
								col2 = ((((a + gg) << 8) + (SINT)(t * 95.f + f)) << 8) - (SINT)(t * -32.f);
							}
							else
								col2 = ((((a + gg) << 8) + r) << 8) + (SINT)(t * 32.f + f);
							PLATFORM.Font(FONT_SMALL)->GetTextExtent(ToWCHAR(value), &size);
							PLATFORM.Font(FONT_SMALL)->DrawTextDynamic((float)(PLATFORM.GetWindowWidth() - 85) - (size.cx + zero), y, 0.008f, 1.f, 1.f, col2, ToWCHAR(count), 1.f, 1, 0);	// STAND-IN (see above)
							DWORD	col3;
							if (flash)
							{
								t = mPanelFlash;
								f = (1.f - t) * 255.f;
								col3 = ((((((SINT)(t * 255.f + f) << 8) + (SINT)(t * 111.f + f)) << 8) + (SINT)(t * 64.f + f)) << 8) - (SINT)(t * -32.f);
							}
							else
								col3 = (((((SINT)(t * 111.f + f) + a) << 8) + gg) << 8) + (SINT)(t * 32.f + f);
							PLATFORM.Font(FONT_SMALL)->GetTextExtent(ToWCHAR(goal), &size);
							PLATFORM.Font(FONT_SMALL)->DrawTextDynamic((float)(PLATFORM.GetWindowWidth() - 85) - (size.cx + zero), y, 0.008f, 1.f, 1.f, col3, ToWCHAR(goal), 1.f, 1, 0);	// STAND-IN (see above)
						}
						else
						{
							DWORD	col2;
							if (flash)
							{
								t = mPanelFlash;
								f = (1.f - t) * 255.f;
								a = (SINT)(t * 255.f + f) << 8;
								gg = (SINT)(t * 64.f + f);
								col2 = ((((a + gg) << 8) + (SINT)(t * 95.f + f)) << 8) - (SINT)(t * -32.f);
							}
							else
								col2 = ((((a + gg) << 8) + r) << 8) + (SINT)(t * 32.f + f);
							PLATFORM.Font(FONT_SMALL)->GetTextExtent(ToWCHAR(value), &size);
							PLATFORM.Font(FONT_SMALL)->DrawTextDynamic((float)(PLATFORM.GetWindowWidth() - 85) - (size.cx + zero), y, 0.008f, 1.f, 1.f, col2, ToWCHAR(value), 1.f, 1, 0);	// STAND-IN (see above)
						}
					}
				}
				y += PLATFORM.Font(FONT_SMALL)->GetHeight() * slot->mFadeIn;
			}
		}
	}
	RENDERINFO.SetFogEnabled(false);
}

//******************************************************************************************
// eases a value towards a target, settling on it within snap
static inline void	Ease(float &value, float target, const float &rate, float snap)
{
	value += rate * (target - value);
	if (fabsf(target - value) < snap)
		value = target;
}

// a colour between two, channel by channel
static inline DWORD	LerpColour(DWORD from, DWORD to, float t)
{
	return (((((((SINT)((((from >> 24) & 0xff) * (1.f - t)) + (((to >> 24) & 0xff) * t))) << 8) +
		(SINT)((((from >> 16) & 0xff) * (1.f - t)) + (((to >> 16) & 0xff) * t))) << 8) +
		(SINT)((((from >> 8) & 0xff) * (1.f - t)) + (((to >> 8) & 0xff) * t))) << 8) +
		(SINT)(((from & 0xff) * (1.f - t)) + ((to & 0xff) * t));
}

//******************************************************************************************
// the crosshair in the world: the tracking predictor, the crosshair turning and tinting as it passes over a friend or
// settles on an enemy (and pulsing while the aug is on), and a marker over whoever is speaking
// Matched with one labelled stand-in (below). The colour lerp's and pulse's parentheses keep retail's separate
// multiplies (004846aa..004846c4, 004848a7, and rate's 0.5 and 0.6 here). The frame and every slot are retail's: one
// pos variable serves the tracking, aim and speaker positions, and `screen` is 16 bytes (above pos). `marked` reads
// the auto-aim target through its accessor again, which is retail's copy before the test (`mov ecx,edi; test ecx,ecx`
// at 004844ef): a plain `marked = target` is forwarded and tested in edi. The sprite alpha reads the member
// (004849b9: fld 1.0; fsub [member]) while the sprite scale is copied by integer moves just after it (004849cd),
// so `a` is written from the member and the scale is assigned before the clamp. One local serves both sprite scales,
// retail's one slot at +0xc with the lerps' complement (a second local ties with the lerps' t*255 temps at weight 3 and
// lands beside them at +0x10). The speaker marker's aspect is a local of its own (name ours): retail pushes a copy of
// LT's value and stores the value to a frame slot beside the lerps' temps (00484c0e, the dead store a named local
// leaves). Its frame index goes through floorf, as Stuart floors a float (Career.cpp: `SINT(floorf(f * 4.f))`): the
// inline wrapper puts the viewpoint index load ahead of the aspect copy in IL order, so the chooser gives retail's
// registers (ecx for the index at 00484be0, edx for the copy at 00484bea); with floor the copy came first and the two
// swapped (RenderRadar's blob matches with either). The lerp writes its complement
// out: with a local u (numbered after its t binding) every lerp added the u term first, while retail adds the t term
// first at the first two lerps (004846b4, 00484734). The complement's temporaries there (0x427, 0x441) wrap past 0x400
// and rank below t; the pulse is a named float, whose low front-end number puts the complement first at the third, as
// retail. Ease takes its rate by const reference, as Stuart passes floats to CEventManager::AddEvent and
// CScheduledEvent::Set: the locked branches then multiply by the rate first (00484623, 0048465d) as retail does,
// where a rate taken by value, or copied, ranks below target - value and costs an fxch.
// STAND-IN: a propagated zero, `float zero = 0;` at the top, added to two kinds of operand that
// retail schedules late. It emits no code: C2 folds `x + zero` only after its global optimizer (as RenderMeshCore's
// `t * zero`, DXMeshRenderer.cpp), and the folded sum stays in the scheduler's graph as a copy node (op 0x162) between
// the load and its use, which carries no memory-operand bonus.
// - Switch cases 0 and 1, `(GAME.GetRenderFrameLength() + zero) * 0.5f`: retail loads the frame length before the
//   viewpoint index (`fld [frame length]; mov ecx,[ebp+0x58]`, 0048456c and 004845b2). Plain, both loads have
//   scheduler depth 40 and IL order puts the += target's address first; Ease's parameter orders and plain-assignment
//   forms of its update change nothing, and a bracket around the call alone does what the zero does.
// - The three sprite scales, `scale * (LT.mScreenShapeScale + zero)`: retail multiplies after three argument pushes
//   (00484972, 00484a20, 00484a6f). Plain, `scale` is a memory variable (a float passed bare to a real call is no x87
//   candidate: measured priority 0 for both its webs), so the multiply reads a memory operand and its +8 bonus
//   puts it before the pushes; behind the copy node (depth 50, no bonus) it waits for them. Forcing scale's x87
//   priority (a diagnostic C2) gives register multiplies instead, and `(float)(scale * (double)LT.mScreenShapeScale)`
//   fixes only the third call. Product helpers by value or const reference and reversed operands change nothing.
// Plain source (no zero): 12/12 shape/instructions in mvar.
void	CHud::RenderTargetMarkers()	// FUNCTION: 0x00484340
{
	float	zero = 0;	// STAND-IN (see above)
	SetOverlayRenderStates();
	mCompass->Render(mBattleEngine);

	*(SRenderMatrix *)&RENDERINFO.mWorld = *(SRenderMatrix *)&ENGINE.mWorldMatrices[mViewpoint];
	RENDERINFO.mWorldChanged = true;
	*(SRenderMatrix *)&RENDERINFO.mView = *(SRenderMatrix *)&ENGINE.mViewMatrices[mViewpoint];
	RENDERINFO.mViewChanged = true;
	*(SRenderMatrix *)&RENDERINFO.mProjection = *(SRenderMatrix *)&ENGINE.mProjectionMatrices[mViewpoint];
	RENDERINFO.mProjectionChanged = true;
	SetOverlayRenderStates();

	FVector	screen;
	FVector	pos;
	if (mBattleEngine->TrackingActive())
	{
		pos = mBattleEngine->GetInterpolatedTrackingPos();
		RENDERINFO.Project((D3DXVECTOR3 *)&screen, (D3DXVECTOR3 *)&pos, mViewport);
		CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.01f, mCrosshairPredictor, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xffffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	}

	int		over = 0;
	pos = mBattleEngine->GetInterpolatedAutoAimPos();
	RENDERINFO.Project((D3DXVECTOR3 *)&screen, (D3DXVECTOR3 *)&pos, mViewport);
	CThing	*target = mBattleEngine->GetAutoAimTarget();
	if ((target) && (!mBattleEngine->TrackingActive()))
		over = 2;
	else
	{
		CUnit	*unit = mBattleEngine->GetUnitOverCrossHairRegardlessOfRange();
		if ((unit) && (unit->GetThingType() & THING_TYPE_UNIT))
		{
			SINT	side = mBattleEngine->GetAllegiance();
			SINT	other = unit->GetAllegiance();
			if (side == 0)
			{
				if (other == 0)
					over = 1;
				else if (other == 1)
					over = 2;
			}
			else if (side == 1)
			{
				if (other == side)
					over = 1;
				else if (other == 0)
					over = 2;
			}
		}
	}
	CThing	*marked = mBattleEngine->GetAutoAimTarget();
	if (!marked)
		marked = mBattleEngine->GetUnitOverCrossHair();
	BOOL	locked = (!mBattleEngine->TrackingActive()) && (marked) && (over == 2);

	switch (over)
	{
	case 0:	Ease(mUnknown0d4[mViewpoint], 1.f, (GAME.GetRenderFrameLength() + zero) * 0.5f, 0.05f);	break;	// STAND-IN (see above)
	case 1:	Ease(mUnknown0d4[mViewpoint], 4.f, (GAME.GetRenderFrameLength() + zero) * 0.5f, 0.05f);	break;	// STAND-IN (see above)
	case 2:	Ease(mUnknown0d4[mViewpoint], 0.f, GAME.GetRenderFrameLength() * 0.5f, 0.05f);	break;
	}
	float	rate = (GAME.GetRenderFrameLength() * 0.5f) * 0.6f;
	if (locked)
		Ease(mUnknown0e4[mViewpoint], 0.5f, rate, 0.01f);
	else
		Ease(mUnknown0e4[mViewpoint], 1.f, rate, 0.01f);

	DWORD	col;
	if (mUnknown0d4[mViewpoint] < 1.f)
		col = LerpColour(0xafff7f7f, 0xafffffff, mUnknown0d4[mViewpoint]);
	else
		col = LerpColour(0xaf9fff9f, 0xafffffff, (4.f - mUnknown0d4[mViewpoint]) / 3.f);
	float	scale = (mUnknown0d4[mViewpoint] * 0.5f) + 0.5f;
	DWORD	col2 = col;
	if (mUnknown0d4[mViewpoint] > 1.f)
	{
		SINT	a = SINT((4.f - mUnknown0d4[mViewpoint]) * 68.f + 51.f);
		if (a < 0)
			a = 0;
		else if (a > 0xff)
			a = 0xff;
		col2 = BlendAlpha2(col, a);
	}

	if ((mUnknown0f4[mViewpoint] != 0) || (mBattleEngine->IsAugActive()))
		mUnknown0f4[mViewpoint] += GAME.GetRenderFrameLength() * 0.25f;
	if (mUnknown0f4[mViewpoint] > PI_M2)
	{
		if (mBattleEngine->IsAugActive())
			mUnknown0f4[mViewpoint] -= PI_M2;
		else
			mUnknown0f4[mViewpoint] = 0;
	}
	if (mBattleEngine->IsAugActive())
	{
		float	pulse = ((float)sin(mUnknown0f4[mViewpoint] * 2) * 0.5f) + 0.5f;
		col2 = LerpColour(0xaf1fafff, 0xafffcf7f, pulse);
	}

	float	turn = 0;
	if (mUnknown0d4[mViewpoint] < 1.f)
		turn = (1.f - mUnknown0d4[mViewpoint]) * 0.5f;
	CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.011f, mCrosshairPrimary, SA_CENTRE, SPT_NONE, 1.f, turn + mUnknown0f4[mViewpoint], col2, scale, scale * (LT.mScreenShapeScale + zero), 0.f, 1.f, 0.f, 1.f);	// STAND-IN (see above)

	scale = mUnknown0e4[mViewpoint];
	SINT	a = SINT(((1.f - mUnknown0e4[mViewpoint]) + 1.f) * 127.5f);
	if (a < 0)
		a = 0;
	else if (a > 0xff)
		a = 0xff;
	DWORD	col3 = BlendAlpha2(col, a);
	CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.011f, mCrosshairSecondary, SA_CENTRE, SPT_NONE, 1.f, 0.f, col3, scale, scale * (LT.mScreenShapeScale + zero), 0.f, 1.f, 0.f, 1.f);	// STAND-IN (see above)
	CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.011f, mCrosshairDot, SA_CENTRE, SPT_NONE, 1.f, 0.f, col3, scale, scale * (LT.mScreenShapeScale + zero), 0.f, 1.f, 0.f, 1.f);	// STAND-IN (see above)

	mUnknown190[mViewpoint] += GAME.GetRenderFrameLength() * 0.4f;
	if (mUnknown190[mViewpoint] >= 6.f)
		mUnknown190[mViewpoint] -= 6.f;

	CMessage	*message = GAME.GetMessageBox()->mCurrent;
	if ((message) && (message->mSpeaker.ToRead()))
	{
		CThing	*speaker = message->mSpeaker.ToRead();
		if (speaker->GetThingType() & THING_TYPE_UNIT)
			((CUnit *)speaker)->GetRenderUnknown90(&pos);
		else
			pos = speaker->GetRenderPos();
		float	yaw = -(float)atan2(pos.X - mBattleEngine->GetPos().X, pos.Y - mBattleEngine->GetPos().Y);
		if (fabsf(AngleDifference(yaw, mBattleEngine->GetCurrentOrientation().mYaw)) < PI / 2)
		{
			RENDERINFO.Project((D3DXVECTOR3 *)&screen, (D3DXVECTOR3 *)&pos, mViewport);
			float	aspect = LT.mScreenShapeScale;
			CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.011f, mSpeakers[(SINT)floorf(mUnknown190[mViewpoint])], SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xffffff00, 1.f, aspect, 0.f, 1.f, 0.f, 1.f);
		}
	}
}

//******************************************************************************************
// a centred marker sprite on the radar
// RenderRadar calls it out of line (00484f28).
#pragma auto_inline(off)
static void	DrawRadarBlob(float x, float y, CDXTexture *tex, DWORD col)	// FUNCTION: 0x004857e0
{
	CSPRITERENDERER::DrawSprite(x, y, 0.011f, tex, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
}
#pragma auto_inline(on)

//******************************************************************************************
// buffers the radar flushes after its blobs (nothing in the program adds to them)
static CVBufTexture	*sRadarBuffers[40];							// 0067a548 (retail's slot is 160 bytes)
static CVBufTexture	**sRadarBuffersEnd = sRadarBuffers;			// 0062ce70

//******************************************************************************************
// the radar: every unit it shows around the engine, turned with it, coloured by side (brighter when flying);
// repair pads, objectives, battle engines and whoever is speaking on top
// Each blob is worked out by the same few lines, written out in full at the seven sites (the unit scan, the two side
// lists, the pads, the objectives, the engines and the speaker). What the bytes need, each from retail's code:
// - The offset from the engine (delta) and the turned offset (turned) are 2D vectors (FVector2, 8 bytes), filled field by
//   field: their two components are the 4-byte slots at the base and base + 4 of retail's frame classes that also hold the
//   12-byte Euler temporaries and the 8-byte conversion temporaries (frame offsets 12/16, 28/32 and 36/40), where separate
//   floats gave four-byte classes that cannot sit inside those. turned.X stays on the FPU stack (the range test, the x
//   sum), delta and turned.Y live in memory (00484e20, 00484e30, 00484e64).
// - Both screen coordinates are worked out before GetRadarBlob is called (the x sum from 00484eda, the y sum from
//   00484f00, the call at 00484f18), so they are named floats.
// - The centre is one term, `(HUD.mViewportX + HUD_OFFSET_X + 69.f) + turned.X - 1.f`: written flat, the sum puts the
//   turned x first or third where retail adds it last (00484ef2); Stuart brackets terms of a sum (Career.cpp:897
//   `new_goodie_count += (nc - oc) + mPendingExtraGoodies;`, FEPGoodies.cpp:1737 `x = (PLATFORM.GetScreenWidth() / 2.f) +
//   (magic_x);`). The terms are written HUD.mViewportX first and HUD.mViewportHeight before HUD_OFFSET_Y: the order
//   they are first met decides which global loads first, and retail loads HUD_OFFSET_Y (00484c8a) and HUD_OFFSET_X
//   (00484eda) first.
// - The offsets are bracketed (`delta.X = (pos.X - centre.X);`, as Stuart brackets a whole right-hand side in
//   DXMemBuffer.cpp:363 `size = ( mData + mDataSize - mPtr );`): each bracket adds a no-code x87 node to the scheduling
//   region, which moves the unit blobs' colour instructions into retail's slots (00484e17..00484e4a). The colour is
//   chosen after the turned vector, and the fade's alpha goes through a named int (retail's shifted value is a
//   temporary in eax, copied to edx: 00484ecb..00484ed2).
// - The pads' colour starts at 0 and the switch has no default (004852dd: xor eax,eax; sub ecx,eax). The engines' blob is
//   the medium one (0048553b: mov ecx,[ebp+0x1a4]); the earlier reconstruction drew the small one, so that change alters
//   behaviour.
void	CHud::RenderRadar(float size)	// FUNCTION: 0x00484c50
{
	SetOverlayRenderStates();
	float	cy = (((GAME.IsMultiplayer()) && (mViewpoint == 0)) ? HUD.mViewportHeight + HUD_OFFSET_Y : HUD.mViewportHeight) + HUD.mViewportY - 44.f - HUD_OFFSET_Y;

	FVector	centre = mBattleEngine->GetRenderPos();
	float	yaw = -mBattleEngine->GetInterpolatedEulerOrientation().mYaw;
	float	s = (float)sin(yaw) * 40.f / size;
	float	c = (float)cos(yaw) * 40.f / size;

	SPtrSet<CUnit>	engines;
	SPtrSet<CUnit>	pads;
	SPtrSet<CUnit>	side1;
	SPtrSet<CUnit>	side0;

	ListIterator<CUnit>	it(&WORLD.mUnitNB);
	CUnit	*unit;
	for (unit = it.First(); unit; unit = it.Next())
	{
		if ((!unit->OnScanner()) || (unit->IsDying()))
			continue;
		if (unit->GetThingType() & THING_TYPE_BATTLE_ENGINE)
			engines.Append(unit);
		else if (unit->mUnknown1F4)
			pads.Append(unit);
		else if (unit->GetAllegiance() == kMuspellAllegiance)
			side1.Append(unit);
		else if (unit->GetAllegiance() == kForsetiAllegiance)
			side0.Append(unit);
		else if ((!unit->IsObjective()) && (!(unit->GetFlags() & TF_INVISIBLE)) && (unit->GetRenderThing()))
		{
			FVector	pos = unit->GetRenderPos();
			FVector2	delta;
			delta.X = (pos.X - centre.X);
			delta.Y = (pos.Y - centre.Y);
			FVector2	turned;
			turned.X = delta.X * c - delta.Y * s;
			turned.Y = delta.Y * c + delta.X * s;
			DWORD	col = (unit->GetThingType() & THING_TYPE_AIR_UNIT) ? 0x707070 : 0x606060;
			float	d2 = turned.Y * turned.Y + turned.X * turned.X;
			if (d2 < 8464.f)
			{
				float	d = (float)sqrt(d2);
				DWORD	alpha = 0xff000000;
				float	k = 46.f / d;
				if (k < 1.f)
				{
					turned.X *= k;
					turned.Y *= k;
					SINT	a = (SINT)((1.f - (d - 46.f) * (1.f / 46.f)) * 255.f);
					alpha = a << 24;
				}
				float	bx = (HUD.mViewportX + HUD_OFFSET_X + 69.f) + turned.X - 1.f;
				float	by = cy - 20.f - turned.Y + 1.f;
				DrawRadarBlob(bx, by, GetRadarBlob(unit), alpha + col);
			}
		}
	}
	for (unit = side0.First(); unit; unit = side0.Next())
	{
		FVector	pos = unit->GetRenderPos();
		FVector2	delta;
		delta.X = (pos.X - centre.X);
		delta.Y = (pos.Y - centre.Y);
		FVector2	turned;
		turned.X = delta.X * c - delta.Y * s;
		turned.Y = delta.Y * c + delta.X * s;
		DWORD	col = (unit->GetThingType() & THING_TYPE_AIR_UNIT) ? 0x9090ff : 0x5050af;
		float	d2 = turned.Y * turned.Y + turned.X * turned.X;
		if (d2 < 8464.f)
		{
			float	d = (float)sqrt(d2);
			DWORD	alpha = 0xff000000;
			float	k = 46.f / d;
			if (k < 1.f)
			{
				turned.X *= k;
				turned.Y *= k;
				SINT	a = (SINT)((1.f - (d - 46.f) * (1.f / 46.f)) * 255.f);
				alpha = a << 24;
			}
			float	bx = (HUD.mViewportX + HUD_OFFSET_X + 69.f) + turned.X - 1.f;
			float	by = cy - 20.f - turned.Y + 1.f;
			DrawRadarBlob(bx, by, GetRadarBlob(unit), alpha + col);
		}
	}
	for (unit = side1.First(); unit; unit = side1.Next())
	{
		FVector	pos = unit->GetRenderPos();
		FVector2	delta;
		delta.X = (pos.X - centre.X);
		delta.Y = (pos.Y - centre.Y);
		FVector2	turned;
		turned.X = delta.X * c - delta.Y * s;
		turned.Y = delta.Y * c + delta.X * s;
		DWORD	col = (unit->GetThingType() & THING_TYPE_AIR_UNIT) ? 0xff5050 : 0xaf0808;
		float	d2 = turned.Y * turned.Y + turned.X * turned.X;
		if (d2 < 8464.f)
		{
			float	d = (float)sqrt(d2);
			DWORD	alpha = 0xff000000;
			float	k = 46.f / d;
			if (k < 1.f)
			{
				turned.X *= k;
				turned.Y *= k;
				SINT	a = (SINT)((1.f - (d - 46.f) * (1.f / 46.f)) * 255.f);
				alpha = a << 24;
			}
			float	bx = (HUD.mViewportX + HUD_OFFSET_X + 69.f) + turned.X - 1.f;
			float	by = cy - 20.f - turned.Y + 1.f;
			DrawRadarBlob(bx, by, GetRadarBlob(unit), alpha + col);
		}
	}

	for (unit = pads.First(); unit; unit = pads.Next())
	{
		FVector	pos = unit->GetRenderPos();
		FVector2	delta;
		delta.X = (pos.X - centre.X);
		delta.Y = (pos.Y - centre.Y);
		FVector2	turned;
		turned.X = delta.X * c - delta.Y * s;
		turned.Y = delta.Y * c + delta.X * s;
		float	d = (float)sqrt(turned.Y * turned.Y + turned.X * turned.X);
		float	k = 46.f / d;
		if (k < 1.f)
		{
			turned.X *= k;
			turned.Y *= k;
		}
		DWORD	col = 0;
		switch (unit->GetAllegiance())
		{
		case 0:		col = 0x5050af;	break;
		case 1:		col = 0xaf0808;	break;
		case 2:		col = 0x606060;	break;
		}
		float	bx = (HUD.mViewportX + HUD_OFFSET_X + 69.f) + turned.X - 1.f;
		float	by = cy - 20.f - turned.Y + 1.f;
		DrawRadarBlob(bx, by, mScannerBlobRepairPad, col + 0xff000000);
	}

	CThing	*thing;
	for (thing = WORLD.mObjectiveThings.First(); thing; thing = WORLD.mObjectiveThings.Next())
	{
		FVector	pos = thing->GetRenderPos();
		FVector2	delta;
		delta.X = (pos.X - centre.X);
		delta.Y = (pos.Y - centre.Y);
		FVector2	turned;
		turned.X = delta.X * c - delta.Y * s;
		turned.Y = delta.Y * c + delta.X * s;
		float	d = (float)sqrt(turned.Y * turned.Y + turned.X * turned.X);
		float	k = 46.f / d;
		if (k < 1.f)
		{
			turned.X *= k;
			turned.Y *= k;
		}
		float	bx = (HUD.mViewportX + HUD_OFFSET_X + 69.f) + turned.X - 1.f;
		float	by = cy - 20.f - turned.Y + 1.f;
		DrawRadarBlob(bx, by, GetRadarBlob(thing), 0xffffff00);
	}

	for (unit = engines.First(); unit; unit = engines.Next())
	{
		FVector	pos = unit->GetRenderPos();
		FVector2	delta;
		delta.X = (pos.X - centre.X);
		delta.Y = (pos.Y - centre.Y);
		FVector2	turned;
		turned.X = delta.X * c - delta.Y * s;
		turned.Y = delta.Y * c + delta.X * s;
		float	d = (float)sqrt(turned.Y * turned.Y + turned.X * turned.X);
		float	k = 46.f / d;
		if (k < 1.f)
		{
			turned.X *= k;
			turned.Y *= k;
		}
		float	bx = (HUD.mViewportX + HUD_OFFSET_X + 69.f) + turned.X - 1.f;
		float	by = cy - 20.f - turned.Y + 1.f;
		DrawRadarBlob(bx, by, mScannerBlobMedium, 0xffffffff);
	}

	CMessage	*message = GAME.GetMessageBox()->mCurrent;
	if ((message) && (message->mSpeaker.ToRead()))
	{
		CThing	*speaker = message->mSpeaker.ToRead();
		FVector	here = mBattleEngine->GetRenderPos();
		float	yaw2 = -mBattleEngine->GetInterpolatedEulerOrientation().mYaw;
		float	s2 = (float)sin(yaw2) * 40.f / size;
		float	c2 = (float)cos(yaw2) * 40.f / size;
		FVector	pos = speaker->GetRenderPos();
		FVector2	delta;
		delta.X = (pos.X - here.X);
		delta.Y = (pos.Y - here.Y);
		FVector2	turned;
		turned.X = delta.X * c2 - delta.Y * s2;
		turned.Y = delta.Y * c2 + delta.X * s2;
		float	d = (float)sqrt(turned.Y * turned.Y + turned.X * turned.X);
		float	k = 46.f / d;
		if (k < 1.f)
		{
			turned.X *= k;
			turned.Y *= k;
		}
		float	bx = (HUD.mViewportX + HUD_OFFSET_X + 69.f) + turned.X - 1.f;
		float	by = cy - 20.f - turned.Y + 1.f;
		DrawRadarBlob(bx, by, mSpeakers[(SINT)floor(mUnknown190[mViewpoint])], 0xffffff00);
	}

	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.Apply();
	for (CVBufTexture **p = sRadarBuffers; p < sRadarBuffersEnd; p++)
	{
		CVBufTexture	*buffer = *p;
		buffer->Draw(true);
		if (buffer)
			buffer->Release();
	}
	LT.SetModulate(0);
	RENDERINFO.Apply();
	sRadarBuffersEnd = sRadarBuffers;
}


//******************************************************************************************
// the radar blob for a thing's size
CDXTexture	*CHud::GetRadarBlob(CThing *thing)	// FUNCTION: 0x00485830
{
	ULONG type = thing->GetThingType();

	if (type & THING_TYPE_UNKNOWN_08000000)
		return mScannerBlobLarge;

	if (type & THING_TYPE_UNIT)
	{
		CUnitData *data = ((CUnit *)thing)->mUnitData;
		if (data && data->mUnknown124)
			return mScannerBlobLarge;
		data = ((CUnit *)thing)->mUnitData;		// read again: retail tests it twice (0048585b)
		if (data)
		{
			switch (data->mBehaviour)
			{
			case UB_BOMBER:
			case UB_THUNDERHEAD:
			case UB_WARSPITE:
			case UB_WARSPITE_DOME:
				return mScannerBlobLarge;
			}
		}
	}

	if (type & THING_TYPE_INFANTRY)
		return mScannerBlobSmall;

	if ((type & (THING_TYPE_BUILDING | THING_TYPE_NAVAL | THING_TYPE_UNKNOWN_04000000)) || (type & THING_TYPE_ANIMAL))
		return mScannerBlobLarge;

	return mScannerBlobMedium;
}

// the HUD's bottom edge plus an offset; the second player's view in a split screen ignores HUD_OFFSET_Y
inline float	CHud::GetBottom(float offset)
{
	if (GAME.IsMultiplayer())
	{
		if (mBattleEngine->GetPlayer()->GetNumber() == 1)
			return (HUD.mViewportBottom + offset);
		return (HUD.mViewportBottom + offset - HUD_OFFSET_Y);
	}
	return (HUD.mViewportBottom + offset - HUD_OFFSET_Y);
}

//******************************************************************************************
// the weapons panel: the radio's backing and outline, the weapon charge, and the north pointer
// The sums' written order is retail's (HUD_OFFSET_X loads first, 00485969), and the two fill sprites' bracketed x's put
// the outline sprite's scheduling-region edge where retail's is. The pointer's offset is a rotation of (0, -45), which
// gives retail's frame: the cosine in a slot of its own below the CEulerAngles temporary and the 12-byte offset in the
// temporary's space. The last sprite's y argument is a named float with a bracketed initialiser: the y chain's last
// fadd then issues before the pushes of the alignment and the texture, as in retail (00485d07); bare it sat after them.
void	CHud::RenderWeaponPanel()	// FUNCTION: 0x004858d0
{
	SetOverlayRenderStates();
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.Apply();

	float y = GetBottom(16.f);
	CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 17.f, y, 0.001f, mRadioView, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0x6fffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);

	if (HUD.IsComponentVisible(HP_RADAR))
	{
		SetAdditive();
		CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 17.f, y, 0.001f, mRadarOutline, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0xff6f8faf, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	}

	if (HUD.IsComponentVisible(HP_CURRENT_WEAPON))
	{
		float wy = GetBottom(-13.f);
		SetAlphaBlend();
		CSPRITERENDERER::DrawSprite((HUD.mViewportX + HUD_OFFSET_X + 9.f), wy, 0.001f, mWeaponFill, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0x3f000000, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
		float charge = mBattleEngine->GetWeaponCharge();
		CSPRITERENDERER::DrawSprite((HUD.mViewportX + HUD_OFFSET_X + 9.f), wy, 0.001f, mWeaponFill, (ESpriteAlignment)2, SPT_FILL_RIGHT, charge, 0.f, 0x7f7fff3f, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
		SetAdditive();
		CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 9.f, wy, 0.001f, mWeaponOutline, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0xffaf8f6f, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	}

	SetAlphaBlend();
	float yaw = mBattleEngine->GetInterpolatedEulerOrientation().mYaw;
	// The north pointer's offset is the vector (0, -45) turned by the heading: sine and cosine are both computed first and
	// the cosine waits in a slot of its own while the sine's product is stored (00485c5f..00485c77).
	float vx = 0.f, vy = -45.f;
	float sn = sinf(yaw);
	float cs = cosf(yaw);
	FVector3 direction(cs * vx - sn * vy, sn * vx + cs * vy, 0.f);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.SRS(D3DRS_ZENABLE, FALSE);
	float ny = GetBottom(16.f);
	// Keep the fixed compass offsets together before adding the heading displacement.
	float sy = ((ny - 128.f + 48.f) + direction.Y);
	CSPRITERENDERER::DrawSprite((((HUD.mViewportX + HUD_OFFSET_X) + 17.f) + 48.f) + direction.X, sy, 0.001f, mRadioNorth, SA_CENTRE, SPT_NONE, 1.f, yaw, 0xff5f7fff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
}

//******************************************************************************************
// the weapons panel's guns (the one fitted, or all three, pulsing), the weapon's name and a player's lives; then the
// battle engine's warnings, the first of each group still showing drawn over the others
// The sums' written order is retail's: their operands are first read here, so it sets VC6's ranks (HUD_OFFSET_X loads
// before HUD.mViewportX, 00485e02; mViewportHeight before mViewportY). The weapon name's x is bracketed, as the lives
// text's is: retail stores it before loading PLATFORM for Font (004860ee).
void	CHud::Render00485d50()	// FUNCTION: 0x00485d50
{
	SIZE	size;
	SetOverlayRenderStates();
	if (mBattleEngine->CountActiveWeapons() > 0)
	{
		if (HUD.IsComponentVisible(HP_CURRENT_WEAPON))
		{
			float	y = GetBottom(-74.f);
			SetAlphaBlend();
			CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 112.f, y, 0.001f, mGunsDarken, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, 0x5f000000, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			SetAdditive();
			CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 112.f, y, 0.001f, mGunsOutline, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, 0xff7f7f7f, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			CDXTexture	*gun;
			switch (mBattleEngine->WhereIsCurrentWeaponAttached())
			{
			case 0:		gun = mGunsSide;	break;
			case 1:		gun = mGunsFront;	break;
			case 2:		gun = mGunsTop;		break;
			default:	gun = NULL;			break;
			}
			DWORD	col = 0xff7f3f2f - ((SINT)(((float)sin(EVENT_MANAGER.GetTime() * 4.f) + 1.f) * -64.f) << 16);
			if (gun)
				CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 112.f, y, 0.001f, gun, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			else
			{
				CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 112.f, y, 0.001f, mGunsSide, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
				CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 112.f, y, 0.001f, mGunsFront, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
				CSPRITERENDERER::DrawSprite(HUD.mViewportX + HUD_OFFSET_X + 112.f, y, 0.001f, mGunsTop, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			}
		}
		SetAlphaBlend();

		float	changed = (EVENT_MANAGER.GetTime() - mBattleEngine->GetChangedWeaponTime()) * 5.f;
		WCHAR	*name = mBattleEngine->GetWeaponName();
		if (name)
		{
			float	y = GetBottom(-35.f);
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic((HUD.mViewportX + HUD_OFFSET_X + 165.f), y, 0.008f, 0.6f, 0.6f, 0xffff6040, name, changed, 1, 0);
		}

		if (GAME.IsMultiplayer())
		{
			SINT	lives = GAME.GetPlayerLives(mBattleEngine->GetPlayer()->GetNumber());
			if (lives >= 0)
			{
				float	y;
				if (mBattleEngine->GetPlayer()->GetNumber() == 1)
					y = (mViewportBottom - 77.f);
				else
					y = (mViewportBottom - 77.f - HUD_OFFSET_Y);
				char	text[4];
				sprintf(text, "x%d", lives);
				WCHAR	*wtext = ToWCHAR(text);
				PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic((mViewportX + 177.f), y, 0.008f, 0.8f, 0.8f, 0xff00ff00, wtext, 4.f, 1, 0);
			}
		}
	}

	if (GAME.GetGameState() <= 3)
	{
		float	t0 = 999.f;
		CRadarWarningReceiver	*rwr = mBattleEngine->GetRadarWarningReceiver();
		if (rwr->mUnknown1C.Size())
		{
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mText0b4, &size);
			t0 = (EVENT_MANAGER.GetTime() - rwr->GetNewThreatTime()) * 3.f;
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - size.cx * 0.5f, HUD.mViewportY + HUD.mViewportHeight - 142.f, 0.008f, 1.f, 1.f, 0xffff3020, mText0b4, t0, 1, 0);
		}
		float	t1 = 999.f;
		if ((t0 > 15.f) || (t0 < 0.f))
		{
			mBattleEngine->TestDanger();
			t1 = (EVENT_MANAGER.GetTime() - mBattleEngine->GetDangerStartTime()) * 3.f;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mText0b8, &size);
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - size.cx * 0.5f, HUD.mViewportY + HUD.mViewportHeight - 142.f, 0.008f, 1.f, 1.f, 0xffff3020, mText0b8, t1, 1, 0);
		}
		if (((t0 > 15.f) || (t0 < 0.f)) && ((t1 > 15.f) || (t1 < 0.f)))
		{
			float	t = (EVENT_MANAGER.GetTime() - mBattleEngine->GetAugmentedTime()) * 3.f;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mText0bc, &size);
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - size.cx * 0.5f, HUD.mViewportY + HUD.mViewportHeight - 142.f, 0.008f, 1.f, 1.f, 0xff30ff20, mText0bc, t, 1, 0);
		}

		float	a = (EVENT_MANAGER.GetTime() - mBattleEngine->GetLowArmourStartTime()) * 3.f;
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mText0c4, &size);
		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - size.cx * 0.5f, HUD.mViewportY + HUD.mViewportHeight - 120.f, 0.008f, 1.f, 1.f, 0xffff3020, mText0c4, a, 1, 0);
		float	b = 999.f;
		if ((a > 15.f) || (a < 0.f))
		{
			b = (EVENT_MANAGER.GetTime() - mBattleEngine->GetLowEnergyStartTime()) * 3.f;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mText0c8, &size);
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - size.cx * 0.5f, HUD.mViewportY + HUD.mViewportHeight - 120.f, 0.008f, 1.f, 1.f, 0xff30ff20, mText0c8, b, 1, 0);
		}
		float	c = 999.f;
		if (((a > 15.f) || (a < 0.f)) && ((b > 15.f) || (b < 0.f)))
		{
			c = (EVENT_MANAGER.GetTime() - mBattleEngine->GetStallTime()) * 3.f;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mText0c0, &size);
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - size.cx * 0.5f, HUD.mViewportY + HUD.mViewportHeight - 120.f, 0.008f, 1.f, 1.f, 0xffff3020, mText0c0, c, 1, 0);
		}
		float	d = 999.f;
		if (((a > 15.f) || (a < 0.f)) && ((b > 15.f) || (b < 0.f)) && ((c > 15.f) || (c < 0.f)))
		{
			d = (EVENT_MANAGER.GetTime() - mBattleEngine->GetAmmoDepletedTime()) * 3.f;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mText0cc, &size);
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - size.cx * 0.5f, HUD.mViewportY + HUD.mViewportHeight - 120.f, 0.008f, 1.f, 1.f, 0xffff3020, mText0cc, d, 1, 0);
		}
		if (((a > 15.f) || (a < 0.f)) && ((b > 15.f) || (b < 0.f)) && ((c > 15.f) || (c < 0.f)) && ((d > 15.f) || (d < 0.f)))
		{
			float	e = (EVENT_MANAGER.GetTime() - mBattleEngine->GetWeaponOverheatedTime()) * 3.f;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mText0d0, &size);
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - size.cx * 0.5f, HUD.mViewportY + HUD.mViewportHeight - 120.f, 0.008f, 1.f, 1.f, 0xffff3020, mText0d0, e, 1, 0);
		}
		RENDERINFO.SetFogEnabled(false);
	}
}

//******************************************************************************************
// the current weapon: an energy weapon's charge, coloured from green to red as it empties and fading in and out while
// it is overheated; any other weapon's ammunition count
void	CHud::Render00486940()	// FUNCTION: 0x00486940
{
	SetOverlayRenderStates();
	float	v0 = 0.f;
	float	v1 = 1.f;
	if (mBattleEngine->IsEnergyWeapon())
	{
		float	y;
		if (GAME.IsMultiplayer())
		{
			if (mBattleEngine->GetPlayer()->GetNumber() == 1)
				y = HUD.mViewportBottom + 45.f;
			else
			{
				y = HUD.mViewportY + 82.f;
				v0 = 1.f;
				v1 = 0.f;
			}
		}
		else
			y = HUD.mViewportBottom - 13.f - HUD_OFFSET_Y;

		if (HUD.IsComponentVisible(HP_CURRENT_WEAPON))
		{
			SetAlphaBlend();
			CSPRITERENDERER::DrawSprite(HUD.mViewportRight - 122.f + 9.f - 28.f - HUD_OFFSET_X, y, 0.001f, mWeaponFill, (ESpriteAlignment)2, SPT_FILL_LEFT, 1.f, 0.f, 0x3f000000, 1.f, 1.f, 0.f, 1.f, v0, v1);

			float	ammo = mBattleEngine->GetWeaponAmmoPercentage();
			float	empty = 1.f - ammo;
			float	a63 = ammo * 63.f;
			DWORD	col = ((((SINT)(empty * 127.f + ammo * 255.f) << 8) + (SINT)(empty * 255.f + a63)) << 8) + (SINT)(empty * 63.f + a63);

			if (mBattleEngine->IsWeaponOverheated())
			{
				if (mUnknown1dc[mViewpoint] == mUnknown1ec[mViewpoint])
				{
					if (mUnknown1ec[mViewpoint] == 0.8f)
						mUnknown1ec[mViewpoint] = 0.1f;
					else
						mUnknown1ec[mViewpoint] = 0.8f;
				}
			}
			else
				mUnknown1ec[mViewpoint] = 0.8f;

			if (mUnknown1dc[mViewpoint] + GAME.GetRenderFrameLength() * 0.1f < mUnknown1ec[mViewpoint])
				mUnknown1dc[mViewpoint] += GAME.GetRenderFrameLength() * 0.1f;
			else if (mUnknown1dc[mViewpoint] - GAME.GetRenderFrameLength() * 0.1f > mUnknown1ec[mViewpoint])
				mUnknown1dc[mViewpoint] -= GAME.GetRenderFrameLength() * 0.1f;
			else
				mUnknown1dc[mViewpoint] = mUnknown1ec[mViewpoint];

			col += (SINT)(mUnknown1dc[mViewpoint] * 255.f) << 24;
			CSPRITERENDERER::DrawSprite(HUD.mViewportRight - 122.f + 9.f - 28.f - HUD_OFFSET_X, y, 0.001f, mWeaponFill, (ESpriteAlignment)2, SPT_FILL_LEFT, ammo, 0.f, col, 1.f, 1.f, 0.f, 1.f, v0, v1);
			SetAdditive();
			CSPRITERENDERER::DrawSprite(HUD.mViewportRight - 122.f + 9.f - 28.f - HUD_OFFSET_X, y, 0.001f, mWeaponOutline, (ESpriteAlignment)2, SPT_FILL_LEFT, 1.f, 0.f, 0xffaf8f6f, 1.f, 1.f, 0.f, 1.f, v0, v1);
		}
		SetAlphaBlend();
	}
	else
	{
		float	y;
		if (GAME.IsMultiplayer())
		{
			if (mBattleEngine->GetPlayer()->GetNumber() == 1)
				y = PLATFORM.GetWindowHeight() * 0.5f - 80.f;
			else
				y = PLATFORM.GetWindowHeight() * 0.5f + 45.f;
		}
		else
			y = PLATFORM.GetWindowHeight() - HUD_OFFSET_Y - 140.f;

		char	text[8];
		sprintf(text, "%d", mBattleEngine->GetWeaponAmmoCount());
		WCHAR	*wtext = ToWCHAR(text);
		SIZE	size;
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(wtext, &size);
		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(PLATFORM.GetWindowWidth() - HUD_OFFSET_X - 73.f - size.cx * 0.5f, y, 0.008f, 1.f, 1.f, 0xff30ff30, wtext, 5.f, 1, 0);
	}
}

//******************************************************************************************
// a radar warning receiver's threat (RadarWarningReceiver.cpp's SRadarThreat)
struct SHudThreat
{
	float	mYaw;						// +0x00
	float	mDistance;					// +0x04
	SINT	mNumber;					// +0x08
	CThing	*mRound;					// +0x0c: the reader's thing
	float	mTime;						// +0x10
};

// a colour with its red, green and blue scaled by a / 256
static inline DWORD	DimColour(DWORD col, SINT a)
{
	DWORD	r = ((col >> 16) & 0xff) * a >> 8;
	DWORD	g = ((col >> 8) & 0xff) * a >> 8;
	DWORD	b = (col & 0xff) * a >> 8;
	return (col & 0xff000000) + (((r << 8) + g) << 8) + b;
}

//******************************************************************************************
// the targets in the world: each lock closing in (its three pieces) or made (the spinning ring), each fired lock,
// each incoming round the warning receiver has seen, and a marker on each objective in front of the camera
// Retail keeps each lock list's address in ebx for the loop (00486f07: mov ebx,[ebp+0x50]; add ebx,0x294), so the loops
// read the lists through a reference. The frame is retail's 0xb4 with its sixteen-byte classes in retail's order: the lock
// loops each declare their own pos and screen (retail's pos at +0x44 shares its slot with the objective loop's first d,
// 00487726; the lock and fired loops' screens share +0x24 with a rounding temporary, the threat loop's screen sits with
// the first d), the objective loop has its own pos and screen (its screen with the GetPos temporaries, 004876e2), and
// screen is an FVector (a 16-byte class; 004876e2 and 00486fc3 take its address as a D3DXVECTOR3 *). Two address-taken
// locals of one loop conflict, which is why the objective loop's two d's sit in different classes. The objective's second
// distance is read through a reference, `sqrtf(d * d)`: retail stores d's three components to consecutive slots (00487845),
// calls GetRadius and reloads Z, Y, X (0048784c..48786a) where Magnitude() forwards them and spills two to other slots;
// that makes d a 9-weight 16-byte symbol and joins it to the other loops' d's. `size` is one variable accumulating (the
// distance, radius / distance * 5, then / zoom): its packer weight of 8 puts it ahead of col, which gives retail's two
// small classes ({col, p, off, dy, rwr} at +0, {size, behind, dx} at +8). The lock-on pieces form an upright triangle: the
// first at (x, y - off) (the raw x is pushed last, 0048705b), the others at (x +- sin * off, y - cos * off) with the cosine
// computed first (00487061) and screen.Y - cos * off the first argument computed (004870b5). The fired loop's alpha is
// `((p + 1.f) * 0.5f) * -255.f`, kept as two multiplies (the bracketed product stops the constant folding, 004872ee..
// 004872f7). The products that lay the pieces out are bracketed initialisers, which gives the fired loop its schedule; the
// lock loop's cosine is cosf and its r unbracketed (together they put the first piece's squaring after three pushes, as
// retail does at 00487046; with the bracketed r and (float)cos it is 2 instructions early, r alone leaves 3 differing
// blocks and cosf alone 5); the minification filter is set before the magnification one; `behind` is the test's value.
// The level checks read the running level (GAME+0x2a0, 008a9d38), not the career's (+0x30).
void	CHud::RenderWorldTargets()	// FUNCTION: 0x00486e00
{
	*(SRenderMatrix *)&RENDERINFO.mWorld = *(SRenderMatrix *)&ENGINE.mWorldMatrices[mViewpoint];
	RENDERINFO.mWorldChanged = true;
	*(SRenderMatrix *)&RENDERINFO.mView = *(SRenderMatrix *)&ENGINE.mViewMatrices[mViewpoint];
	RENDERINFO.mViewChanged = true;
	*(SRenderMatrix *)&RENDERINFO.mProjection = *(SRenderMatrix *)&ENGINE.mProjectionMatrices[mViewpoint];
	RENDERINFO.mProjectionChanged = true;
	SetOverlayRenderStates();
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);

	mUnknown1b8[mViewpoint] += GAME.GetRenderFrameLength() * 0.05f;
	if (mUnknown1b8[mViewpoint] >= PI_M2)
		mUnknown1b8[mViewpoint] -= PI_M2;
	RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);

	CCamera		*camera = ENGINE.GetCamera();
	CLockInfo	*lock;
	SPtrSet<CLockInfo>	&locks = mBattleEngine->GetLockList();
	for (lock = locks.First(); lock; lock = locks.Next())
	{
		FVector	pos;
		FVector	screen;
		if (!lock->mUnit.ToRead())
			continue;
		lock->mUnit->GetRenderUnknown90(&pos);
		if (!camera)
			continue;
		FVector	d = pos - camera->GetPos();
		if (!(camera->GetOrientation().GetColumnY() * d > 0.001f))
			continue;
		RENDERINFO.Project((D3DXVECTOR3 *)&screen, (D3DXVECTOR3 *)&pos, mViewport);
		float	p = lock->GetLockPercentage();
		if (p < 1.f)
		{
			DWORD	col = 0xffffff - ((SINT)(p * -200.f) << 24);
			float	r = (1.f - p) * 10.f;
			float	off = (r * r);
			CSPRITERENDERER::DrawSprite(screen.X, screen.Y - off, 0.013f, mLockOnPiece1, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			float	dy = (cosf(PI_M2 / 3) * off);
			float	dx = ((float)sin(PI_M2 / 3) * off);
			CSPRITERENDERER::DrawSprite(screen.X + dx, screen.Y - dy, 0.013f, mLockOnPiece2, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			CSPRITERENDERER::DrawSprite(screen.X - dx, screen.Y - dy, 0.013f, mLockOnPiece3, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
		}
		else
		{
			CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.013f, mLockOnPiecesAll, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xc8ffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.013f, mTargetLockedOnRed, SA_CENTRE, SPT_NONE, 1.f, mUnknown1b8[mViewpoint], 0x80ff5555, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
		}
	}

	SPtrSet<CLockInfo>	&fired = mBattleEngine->GetFiredLockList();
	for (lock = fired.First(); lock; lock = fired.Next())
	{
		FVector	pos;
		FVector	screen;
		if (!lock->mUnit.ToRead())
			continue;
		lock->mUnit->GetRenderUnknown90(&pos);
		if (!camera)
			continue;
		FVector	d = pos - camera->GetPos();
		if (!(camera->GetOrientation().GetColumnY() * d > 0.001f))
			continue;
		RENDERINFO.Project((D3DXVECTOR3 *)&screen, (D3DXVECTOR3 *)&pos, mViewport);
		if (!(screen.Z > 0.f))
			continue;
		float	p = lock->GetLockPercentage();
		float	scale = 1.f - p * 0.5f;
		DWORD	col = 0xff5555 - ((SINT)(((p + 1.f) * 0.5f) * -255.f) << 24);
		CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.013f, mTargetLockedOnRed, SA_CENTRE, SPT_NONE, 1.f, mUnknown1b8[mViewpoint], col, scale, scale, 0.f, 1.f, 0.f, 1.f);
		if (p < 1.f)
		{
			col = 0xffffff - ((SINT)((1.f - p) * -255.f) << 24);
			float	r = (p * 10.f);
			float	off = (r * r);
			CSPRITERENDERER::DrawSprite(screen.X, screen.Y - off, 0.013f, mLockOnPiece1, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			float	dy = ((float)cos(PI_M2 / 3) * off);
			float	dx = ((float)sin(PI_M2 / 3) * off);
			CSPRITERENDERER::DrawSprite(screen.X + dx, screen.Y - dy, 0.013f, mLockOnPiece2, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
			CSPRITERENDERER::DrawSprite(screen.X - dx, screen.Y - dy, 0.013f, mLockOnPiece3, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
		}
	}

	CRadarWarningReceiver	*rwr = mBattleEngine->GetRadarWarningReceiver();
	if (rwr)
	{
		for (SHudThreat *t = (SHudThreat *)rwr->mUnknown1C.First(); t; t = (SHudThreat *)rwr->mUnknown1C.Next())
		{
			FVector	screen;
			CThing	*round = t->mRound;
			if ((!round) || (!camera))
				continue;
			FVector	d = round->GetPos() - camera->GetPos();
			if (!(camera->GetOrientation().GetColumnY() * d > 0.001f))
				continue;
			FVector	rp = round->GetRenderPos();
			RENDERINFO.Project((D3DXVECTOR3 *)&screen, (D3DXVECTOR3 *)&rp, mViewport);
			if (!(screen.Z > 0.f))
				continue;
			float	age = GAME.GetFrameRenderFraction() * 0.05f + EVENT_MANAGER.GetTime() - t->mTime;
			if (age > 0.5f)
				age = 0.5f;
			age += age;
			float	scale = 5.f - age * 4.5f;
			DWORD	col = 0xffffff - ((SINT)(age * -255.f) << 24);
			CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.013f, mTargetSighted, SA_CENTRE, SPT_NONE, 1.f, 0.f, col, scale, scale, 0.f, 1.f, 0.f, 1.f);
		}
	}

	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	SetMipFilterLinear(0);
	SetAdditive();

	for (CThing *thing = WORLD.mObjectiveThings.First(); thing; thing = WORLD.mObjectiveThings.Next())
	{
		FVector	pos;
		FVector	screen;
		if (thing->GetThingType() & THING_TYPE_UNIT)
			((CUnit *)thing)->GetRenderUnknown90(&pos);
		else
			thing->GetCentrePos(pos);
		RENDERINFO.Project((D3DXVECTOR3 *)&screen, (D3DXVECTOR3 *)&pos, mViewport);
		PLATFORM.GetWindowWidth();
		PLATFORM.GetWindowHeight();
		BOOL	behind = FALSE;
		if (camera)
		{
			FVector	d = pos - camera->GetPos();
			behind = (camera->GetOrientation().GetColumnY() * d < 0.001f);
		}
		DWORD	col = 0xff3fafff;
		if (thing->GetThingType() & THING_TYPE_UNIT)
		{
			switch (((CUnit *)thing)->GetAllegiance())
			{
			case 0:
				col = 0xff6fff2f;
				break;
			case 1:
				col = 0xffff6f2f;
				break;
			case 2:
				if (((GAME.GetCurrentlyRunningLevelNum() == 710) || (GAME.GetCurrentlyRunningLevelNum() == 720)) && (!strcmp(thing->GetName(), "Prison")))
					col = 0xff6fff2f;
				break;
			}
		}
		else if (thing->GetThingType() & THING_TYPE_FEATURE)
		{
			switch (((CFeature *)thing)->mAllegiance)
			{
			case 0:
				col = 0xff6fff2f;
				break;
			case 1:
				col = 0xffff6f2f;
				break;
			}
		}
		if ((!camera) || (behind))
			continue;
		FVector	d = pos - camera->GetPos();
		float	size = sqrtf(d * d);
		size = thing->GetRadius() / size * 5.f;
		size /= camera->GetZoom();
		if (size < 0.2f)
			size = 0.2f;
		else if (size > 0.5f)
		{
			SINT	a = 255 - (SINT)((size - 0.5f) * 85.f);
			if (a < 0)
				a = 0;
			col = DimColour(col, a);
		}
		CSPRITERENDERER::DrawSprite(screen.X, screen.Y, 0.013f, mScreenMarker, SA_CENTRE, SPT_NONE, 1.f, PI / 4, col, size, size, 0.f, 1.f, 0.f, 1.f);
	}
	SetAlphaBlend();
}

//******************************************************************************************
void	CHud::RenderOverlayForViewpoint(CPlayer *player, int viewpoint)	// FUNCTION: 0x004879e0
{
	CBattleEngine *be = player->GetBattleEngine();
	if ((!be) || be->IsDying())
		return;

	ENGINE.SelectViewpoint(viewpoint);
	CViewport *vp = ENGINE.GetViewportForViewpoint(viewpoint);

	mViewportWidth = float(vp->Width);
	mViewportHeight = float(vp->Height);
	mViewportX = float(vp->X);
	mViewportY = float(vp->Y);

	// the safe area: the viewport, kept inside the middle 85% of the window
	mSafeWidth = mViewportWidth;
	mSafeHeight = mViewportWidth;
	mSafeX = mViewportX;
	mSafeY = mViewportY;

	float sw = float(PLATFORM.GetWindowWidth());
	float sh = float(PLATFORM.GetWindowHeight());
	float aw = HUD_SAFE_AREA * sw;
	float ah = HUD_SAFE_AREA * sh;
	float left = (sw - aw) * 0.5f;
	float top = (sh - ah) * 0.5f;

	if (mSafeX < left)
	{
		mSafeWidth -= left - mSafeX;
		mSafeX = left;
	}
	if (mSafeY < top)
	{
		mSafeHeight -= top - mSafeY;
		mSafeY = left;					// sic: retail stores the left edge (00487add)
	}
	if (mSafeX + mSafeWidth > left + aw)
		mSafeWidth = left + aw - mSafeX;
	if (mSafeY + mSafeHeight > top + ah)
		mSafeHeight = top + ah - mSafeY;

	mBattleEngine = be;
	mViewport = vp;
	mViewpoint = viewpoint;

	mViewportBottom = mViewportY + mViewportHeight;
	mViewportRight = mViewportX + mViewportWidth;
	mSafeBottom = mSafeY + mSafeHeight;
	mSafeRight = mSafeX + mSafeWidth;

	SetOverlayRenderStates();
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, FALSE);

	RenderWorldTargets();
	RenderTargetIndicator();
	if (GAME.GetHelpTextDisplay())
		GAME.GetHelpTextDisplay()->Render();
	Render00483530();
	RenderTargetMarkers();
	RenderWeaponPanel();
	Render00485d50();
	Render00486940();
	RenderRadar(96.f);
}

//******************************************************************************************
void	CHud::Render()	// FUNCTION: 0x00487bc0
{
	float	t = EVENT_MANAGER.GetTime();
	int		n = ENGINE.GetNumViewpoints();

	mFlash = int((t - floorf(t)) * 4.f) & 1;

	if (!(ENGINE.GetDrawDebugStuff() & DRAW_COCKPIT))
		return;

	if (mPartState[HP_BATTLE_LINE_MAP] != HCS_FLASHING)
		mPartState[HP_BATTLE_LINE_MAP] = (GAME.GetMessageBox()->mCurrent == 0);

	for (int i = 0; i < n; i ++)
	{
		CPlayer *player = ENGINE.GetPlayerForViewpoint(i);
		if (player && GAME.GetCamera(i)->GetShowHUD())
			RenderOverlayForViewpoint(player, i);
	}

	RENDERINFO.SetLightingEnabled(true);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	RENDERINFO.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.SetFogEnabled(true);
	LT.SetModulate(0);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	SetMipFilterLinear(0);
}

//******************************************************************************************
// The bracketed positions reproduce retail's IL tuple counts, which place its stores and pushes across VC6's
// scheduling regions (README); they may stand for bracketed position macros in the original.
void	CHud::RenderBattleline(CViewport *viewport)	// FUNCTION: 0x00487d10
{
	SetOverlayRenderStates();

	mViewportWidth = float(viewport->Width);
	mViewportHeight = float(viewport->Height);
	mViewportX = float(viewport->X);
	mViewportY = float(viewport->Y);

	float y, y2;
	if (GAME.IsMultiplayer())
	{
		y = PLATFORM.GetWindowHeight() * 0.5f + 78.f;
		y2 = (PLATFORM.GetWindowHeight() * 0.5f + 78.f - 16.f);
	}
	else
	{
		y = HUD.mViewportBottom + 16.f - HUD_OFFSET_Y;
		y2 = (y - 16.f);
	}

	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	GAME.GetMessageBox()->RenderOverlay(7);

	SetOverlayRenderStates();
	SetAlphaBlend();
	CSPRITERENDERER::DrawSprite((HUD.mViewportRight - 121.f - HUD_OFFSET_X), y, 0.001f, mCircleDarkener, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0x7fffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);

	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
	CSPRITERENDERER::DrawSprite(HUD.mViewportRight - 137.f - HUD_OFFSET_X + 64.f, (y2 - 64.f), 0.005f, mCircleMask, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xffffffff, 1.5f, 1.5f, -0.25f, 1.25f, -0.25f, 1.25f);

	SetOverlayRenderStates();
	RENDERINFO.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
	GAME.GetMessageBox()->RenderBattleLinePulse((HUD.mViewportRight - 137.f - HUD_OFFSET_X), y2, 7);

	SetOverlayRenderStates();
	SetAdditive();
	CSPRITERENDERER::DrawSprite((HUD.mViewportRight - 121.f - HUD_OFFSET_X), y, 0.001f, mBattleLineOutline, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0xff6f8faf, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);

	if (INFLUENCEMAP->IsEmpty())
	{
		if (!GAME.GetMessageBox()->mCurrent)
			CSPRITERENDERER::DrawSprite(HUD.mViewportRight - 104.f - HUD_OFFSET_X, y2 - 32.f, 0.001f, mForsetiIcon, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0xffffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	}
	else
	{
		mBattleLine->Update();
		mBattleLine->Render();
	}
}

//******************************************************************************************
void	CHud::RenderOverlay()	// FUNCTION: 0x00488090
{
	if (!mOverlay)
		return;

	ENGINE.EnableAlpha();
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, FALSE);
	RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);
	RENDERINFO.Apply();

	mOverlay->Render();
	if (mOverlay->IsFinished())
	{
		delete mOverlay;
		mOverlay = NULL;
	}

	RENDERINFO.SetFogEnabled(true);
	RENDERINFO.SetLightingEnabled(true);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, TRUE);
}

//******************************************************************************************
void	CHud::SetMessageSpeaker(CActiveReader<CThing> &speaker)
{
}

//******************************************************************************************
BOOL	CHud::IsComponentVisible(int n)	// FUNCTION: 0x004881e0
{
	switch (mPartState[n])
	{
	case HCS_OFF:		return FALSE;
	case HCS_ON:		return TRUE;
	case HCS_FLASHING:	return mFlash;
	}
	return FALSE;
}
