// Hud.h: the in-game HUD (reconstructed; Hud.obj, 00481310..004881fb; Stuart's release has no copy). The PC's HUD
// is a CDXHud (DXHud.obj builds it, 00542740, on CHud's constructor).

#ifndef HUD_H
#define HUD_H

#include "activereader.h"

class CDXTexture;
class CDXCompass;
class CDXBattleLine;
class CScreenOverlay;
class CUnit;
class CViewport;

// the HUD components a cutscene can switch off or flash (IsComponentVisible's switch, 004881e0)
enum EHudComponentState
{
	HCS_OFF = 0,
	HCS_ON,
	HCS_FLASHING
};

// the parts a script can highlight (onsldef.msl's HUD_HEALTH_BAR..HUD_CURRENT_WEAPON; IScript::HighlightHudPart)
enum EHudPart
{
	HP_HEALTH_BAR = 0,
	HP_ENERGY_BAR,
	HP_COMPASS,
	HP_BATTLE_LINE_MAP,						// off while a message is shown (CHud::Render 00487c26)
	HP_RADAR,								// the radar outline (RenderWeaponPanel 004859a9)
	HP_CURRENT_WEAPON						// the weapon charge (004859ad)
};

#define HUD_NUM_COMPONENTS	6					// Reset's fill (004815c4)
#define HUD_NUM_SPEAKERS	6					// InitResources' "hud\\speaker%1d.tga" loop (004816fb)

// something drawn for each of up to four players (Reset, 004815e2)
struct SHudPlayerMarker
{
	float		mUnknown0;						// +0x0: -10 after Reset
	float		mFadeIn;						// +0x4: the slot's fade in (Render00483530 004836af)
	float		mLastValue;						// +0x8: the value last shown
};

class CHud
{
public:
	CHud();										// 00481400 (CDXHud's constructor 00542743)
	void	Init();								// 00481450
	void	Reset();							// 004815c0
	void	InitResources();					// 00481650: the HUD's textures
	BOOL	PostLoadProcess();					// 00481af0
	void	ShutDown();							// 00481b00
	// a cutscene's overlay mesh: shown now, or queued until SwitchInOverlay (CCutscene::Start 0043f3e8)
	void	SetHudComponent(char *name, bool now);	// 00481f40
	void	SwitchInOverlay();					// 00482050
	// 00482090: the 2D overlay states; identical-code folding shares it with a CMessageLog member (004b9406 calls it
	// with the log's this)
	void	SetOverlayRenderStates();
	void	SetAdditive();						// 004821b0 (CCompass's render 0042722c)
	void	SetAlphaBlend();					// 004821e0 (CCompass's render 00427911)
	// 00482210: the objective textures' framed bar (also CMessageBox::RenderOverlay 004b8ad0)
	void	RenderObjectiveBar(float x, float y, float width, float height, float open);
	void	RenderTargetIndicator();			// 00482590 (name open)
	void	Render00483530();					// 00483530
	void	RenderTargetMarkers();				// 00484340 (name open)
	void	RenderRadar(float size);			// 00484c50 (name open; RenderOverlayForViewpoint passes 96)
	CDXTexture	*GetRadarBlob(class CThing *thing);	// 00485830: the blob texture for a thing's size
	float	GetBottom(float offset);
	void	RenderWeaponPanel();				// 004858d0 (name ours)
	void	Render00485d50();					// 00485d50
	void	Render00486940();					// 00486940
	void	RenderWorldTargets();				// 00486e00 (name open)
	void	RenderOverlayForViewpoint(class CPlayer *player, int viewpoint);	// 004879e0
	void	Render();							// 00487bc0 (CDXEngine::PostRender 0053ed01)
	void	RenderBattleline(CViewport *viewport);	// 00487d10
	void	RenderOverlay();					// 00488090
	BOOL	IsComponentVisible(int n);			// 004881e0 (CCompass's render 004276b3)
	void	SetMessageSpeaker(CActiveReader<class CThing> &speaker);	// empty, folded into 00452b60 (CMessageBox::TryAdvance 004b7c2b)
	CDXTexture	*GetWhiteTexture() { return mWhiteTexture; }	// Stuart's (CGame::DrawGameStuff 00471915)

	// the viewpoint's viewport as floats (RenderOverlayForViewpoint 00487a16, from ENGINE's viewport)
	float		mViewportWidth;				// +0x000
	float		mViewportHeight;			// +0x004
	float		mViewportX;					// +0x008
	float		mViewportY;					// +0x00c
	float		mViewportRight;				// +0x010: X + Width (00487b3c)
	float		mViewportBottom;			// +0x014: Y + Height
	// the safe area: the viewport kept inside the middle 85% of the window (RenderOverlayForViewpoint 00487a3c..)
	float		mSafeWidth;					// +0x018
	float		mSafeHeight;				// +0x01c: set from the viewport's width, retail's slip (00487a4d)
	float		mSafeX;						// +0x020
	float		mSafeY;						// +0x024
	float		mSafeRight;					// +0x028
	float		mSafeBottom;				// +0x02c
	CDXBattleLine	*mBattleLine;			// +0x030
	int			mPartState[HUD_NUM_COMPONENTS];	// +0x034: EHudComponentState
	BOOL		mFlash;						// +0x04c: the flashing components' phase (Render 00487c01)
	class CBattleEngine	*mBattleEngine;		// +0x050: the viewpoint being drawn (RenderOverlayForViewpoint 00487b30)
	CViewport	*mViewport;					// +0x054
	int			mViewpoint;					// +0x058
	BOOL		mUnknown05c;				// +0x05c: set by Init
	CDXCompass	*mCompass;					// +0x060
	SHudPlayerMarker	mUnknown064[4];		// +0x064
	float		mObjectivePanelOpening;				// +0x094: the objective panel's opening (Render00483530)
	float		mObjectivePanelWidth;				// +0x098: its width
	CActiveReader<CUnit>	mUnknown09c[2];	// +0x09c
	float		mUnknown0a4[2];				// +0x0a4: when each viewpoint's target was last seen (-10000 after Reset)
	float		mPanelFlash;				// +0x0ac: the panel's flash
	BOOL		mPanelBrightening;				// +0x0b0: brightening
	WCHAR		*mText0b4;					// +0x0b4: the texts Init looks up
	WCHAR		*mText0b8;					// +0x0b8
	WCHAR		*mText0bc;					// +0x0bc
	WCHAR		*mText0c0;					// +0x0c0
	WCHAR		*mText0c4;					// +0x0c4
	WCHAR		*mText0c8;					// +0x0c8
	WCHAR		*mText0cc;					// +0x0cc
	WCHAR		*mText0d0;					// +0x0d0
	float		mUnknown0d4[4];				// +0x0d4: 1 after Reset
	float		mUnknown0e4[4];				// +0x0e4: 1 after Reset
	float		mUnknown0f4[4];				// +0x0f4
	CDXTexture	*mCrosshairOutline;			// +0x104
	CDXTexture	*mCrosshairEnemy;			// +0x108
	CDXTexture	*mCrosshairFriend;			// +0x10c
	CDXTexture	*mCrosshairPredictor;		// +0x110
	CDXTexture	*mTargetSighted;			// +0x114
	CDXTexture	*mRadarOutline;				// +0x118
	CDXTexture	*mBattleLineOutline;		// +0x11c
	CDXTexture	*mCircleDarkener;			// +0x120
	CDXTexture	*mWeaponOutline;			// +0x124
	CDXTexture	*mWeaponFill;				// +0x128
	CDXTexture	*mCircleMask;				// +0x12c
	CDXTexture	*mGunsDarken;				// +0x130
	CDXTexture	*mGunsOutline;				// +0x134
	CDXTexture	*mGunsTop;					// +0x138
	CDXTexture	*mGunsFront;				// +0x13c
	CDXTexture	*mGunsSide;					// +0x140
	CDXTexture	*mScreenMarker;				// +0x144
	CDXTexture	*mRadioNorth;				// +0x148
	CDXTexture	*mRadioView;				// +0x14c
	CDXTexture	*mMenuBackground;			// +0x150
	CDXTexture	*mObjectiveLeft;			// +0x154
	CDXTexture	*mObjectiveRight;			// +0x158
	CDXTexture	*mObjectiveInnerLeft;		// +0x15c
	CDXTexture	*mObjectiveInnerCentre;		// +0x160
	CDXTexture	*mObjectiveInnerRight;		// +0x164
	CDXTexture	*mWhiteTexture;				// +0x168
	CDXTexture	*mCrosshairPrimary;			// +0x16c
	CDXTexture	*mCrosshairSecondary;		// +0x170
	CDXTexture	*mCrosshairDot;				// +0x174
	CDXTexture	*mSpeakers[HUD_NUM_SPEAKERS];	// +0x178
	float		mUnknown190[4];				// +0x190
	CDXTexture	*mScannerBlobSmall;			// +0x1a0
	CDXTexture	*mScannerBlobMedium;		// +0x1a4
	CDXTexture	*mScannerBlobLarge;			// +0x1a8
	CDXTexture	*mScannerBlobRepairPad;		// +0x1ac
	CDXTexture	*mLockOnPiecesAll;			// +0x1b0
	CDXTexture	*mTargetLockedOnRed;		// +0x1b4
	float		mUnknown1b8[4];				// +0x1b8
	CDXTexture	*mLockOnPiece1;				// +0x1c8
	CDXTexture	*mLockOnPiece2;				// +0x1cc
	CDXTexture	*mLockOnPiece3;				// +0x1d0
	CDXTexture	*mForsetiIcon;				// +0x1d4
	CDXTexture	*mOffscreenArrow;			// +0x1d8
	float		mUnknown1dc[4];				// +0x1dc
	float		mUnknown1ec[4];				// +0x1ec
	CScreenOverlay	*mOverlay;				// +0x1fc
	CScreenOverlay	*mNextOverlay;			// +0x200
};

// the HUD's offset (read, never written; also CDXBattleLine and CMessageBox::RenderOverlay)
extern float	HUD_OFFSET_X;					// 0067a628
extern float	HUD_OFFSET_Y;					// 0067a62c

// HUD is the PC's CDXHud (DXHud.h; DXHud.cpp defines it)
#include "DXHud.h"
class CDXHud;
extern CDXHud HUD;								// 008aa4e8

#endif
