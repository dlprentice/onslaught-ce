// frontend.h: the front end (reconstructed; Stuart's Frontend.h is the model). FRONTEND is a CDXFrontEnd at 0089d758
// (its constructor 00540bf0 is called on it, and calls CFrontEnd's, 00465f10; vtables CFrontEnd 005db75c,
// CDXFrontEnd 005e5054). The layout comes from CFrontEnd's constructor, which sets each page's vtable at its offset,
// and CFrontEnd::Init (004662a0), which stores each page's address in mPages; it is Stuart's member order with the
// PC's pages.

#ifndef FRONTEND_H
#define FRONTEND_H

#include	"game.h"

// windows.h (mmsystem.h) renames PlaySound
#ifdef PlaySound
#undef PlaySound
#endif
#include	"mathsconst.h"
#include	"text.h"

#include	"FrontEndPage.h"
#include	"FrontEndText.h"
#include	"FEMessBox.h"

#include	"FEPCommon.h"
#include	"FEPIntro.h"
#include	"FEPDevelopment.h"
#include	"FEPBriefing.h"
#include	"FEPMain.h"
#include	"FEPDemoMain.h"
#include	"FEPDevSelect.h"
#include	"FEPLoadGame.h"
#include	"FEPSaveGame.h"
#include	"FEPOptions.h"
#include	"FEPScreenPos.h"
#include	"FEPCredits.h"
#include	"FEPVirtualKeyboard.h"
#include	"FEPgoodies.h"
#include	"FEPLanguageTest.h"
#include	"FEPDirectory.h"
#include	"FEPMultiplayer.h"
#include	"FEPMultiplayerStart.h"
#include	"FEPBEConfig.h"
#include	"FEPWingmen.h"
#include	"FEPDebriefing.h"
#include	"FEPE3LevelSelect.h"
#include	"FEPLevelSelect.h"

#include	"TransitionHelpers.h"

class CDXTexture;
#ifndef CTEXTURE
#define CTEXTURE CDXTexture
#endif
class CController;

#define FED	(FRONTEND.mFrontEndData)
#define FET (FRONTEND.mFrontEndText)
#define FEV	(FRONTEND.mFrontEndVideo)

//*********************************************************************************
enum	EFrontEndEntry
{
	FEE_START = 0,
	FEE_FROM_ATTRACT,
	FEE_TITLE_SCREEN
};

//*********************************************************************************
enum	EFrontEndSound
{
	FES_MOVE	 = 0,
	FES_SELECT,
	FES_BACK,

	FES_NUM_SOUNDS
};

// Stuart's camera classes (CCamera's FVector is 16 bytes: GetPos 00466140 copies four dwords from +0x34)
#include "Camera.h"

// The front end's textures (CFrontEndData::GetTexture): the ids its callers use.
enum EFrontEndTexture
{
	FET_LEVEL_LINK = 0,	// CFrontEnd::DrawLine (00466e1a)
	FET_BARL,	// CFrontEnd::DrawBar (00467b61)
	FET_BARC,
	FET_BARR,
	FET_GLARE_ALPHA,
	FET_LOST_TOYS,
	FET_RANKING_S,
	FET_RANKING_A,
	FET_RANKING_B,
	FET_RANKING_C,
	FET_RANKING_D,
	FET_RANKING_E,
	FET_RANKING_SMASK,
	FET_RANKING_AMASK,
	FET_RANKING_BMASK,
	FET_RANKING_CMASK,
	FET_RANKING_DMASK,
	FET_RANKING_EMASK,
	FET_FORSETI_TEXT,	// CFEPCommon::RenderSmallForsetiText (0045301d)
	FET_FORSETI_WRITING_SMALL,
	FET_WHITE_RING,
	FET_LOGO_SHINE,	// CFEPCommon::RenderSmallBEALogo (00452f36)
	FET_SELECT_LEVEL_RING_BRACKET01,
	FET_SELECT_LEVEL_RING_BRACKET02,
	FET_LEVEL_SELECT_MASK,
	FET_METAL_RING_TRANS_FROM_LEVSEL1,
	FET_METAL_RING_TRANS_FROM_LEVSEL2,
	FET_LEVEL_SELECT_GLOW_BLUE,
	FET_LEVEL_SELECT_GLOW_ORANGE,
	FET_RANKING_SMALL_MASK_A,
	FET_RANKING_SMALL_MASK_B,
	FET_RANKING_SMALL_MASK_C,
	FET_RANKING_SMALL_MASK_D,
	FET_RANKING_SMALL_MASK_E,
	FET_RANKING_SMALL_MASK_S,
	FET2_WHITE,	// CFrontEnd::DrawSlidingTextBordersAndMask (004678dd)
	FET_SELECTION_MARKER,	// CFEPCommon::RenderSelectionMarker (004530ff)
	FET_AVI_BOX,
	FET_HELP_ARROW,	// CFEPCommon::RenderHelpText (004532e3)
	FET_GOODIES1,
	FET_GOODIES2,
	FET_GOODIES3,
	FET_GOODIES4,
	FET_FLAG_UK,
	FET_FLAG_FR,
	FET_FLAG_GR,
	FET_FLAG_SP,
	FET_FLAG_IT,
	FET_I_TROOP,
	FET_I_TANK,
	FET_I_PLANE,
	FET_I_BILLY,
	FET_I_LORENZO,
	FET_I_TARA,
	FET_FE_SPLASH1,
	FET_FE_INFOGRAMES,
	FET_SELECT_LEVEL_EPISODE_CURVE,
	FET_BEA_TITLE2,
	FET_SMALL_BEA_LOGO,	// CFEPCommon::RenderSmallBEALogo (00452e13)
	FET_BEA_TITLE_BRACKET01,
	FET_BEA_TITLE_BRACKET02,
	FET_BEA_TITLE_TEXT_BOX,
	FET_BEA_TITLE_SYMBOL_BRACKET01,
	FET_BEA_TITLE_SYMBOL_BRACKET02,
	FET_BEA_TITLE_NAV_SYMBOL_NEWGAME01,
	FET_BEA_TITLE_NAV_SYMBOL_CONTINUEGAME01,
	FET_BEA_TITLE_NAV_SYMBOL_SAVEGAME01,
	FET_BEA_TITLE_NAV_SYMBOL_LOADGAME01,
	FET_BEA_TITLE_NAV_SYMBOL_MULTIPLAYER01,
	FET_BEA_TITLE_NAV_SYMBOL_GOODIES01,
	FET_BEA_TITLE_NAV_SYMBOL_OPTIONS01,
	FET_BEA_TITLE_NAV_SYMBOL_LANGUAGE01,
	FET_BEA_TITLE_NAV_SYMBOL_QUITGAME01,
	FET3_HEADER_BRACKET1,	// CFrontEnd::DrawTitleBar (00467e0f)
	FET3_HEADER_BRACKET2,	// (00468026)
	FET_BEA_HEADER_BRACKET03,
	FET3_HEADER_TEXT_BOX,	// (00467cb4)
	FET3_SELECT_BRACKET1,	// (004675b6)
	FET3_SELECT_BRACKET2,	// (0046779b)
	FET3_SELECT_BRACKET_MASK,	// (004678c1)
	FET_SMALL_STAR,
	FET2_BLANK,	// CFrontEnd::DrawPanel (0046705e)
	FET_VK_KEY,
	FET_XB_XBOX,
	FET_XB_MU,
	FET_XB_MU_CORRUPT,

	FET_NUM							// 86: CFrontEndData::Init loads them in this order (004688a3..0046910e)
};

// The front end's meshes (CFrontEndData::GetMesh), loaded after the textures unless PLAYABLE_DEMO (0046913e).
enum EFrontEndMesh
{
	FEM_BE_TRANS = 0,				// the configuration and two-player pages' Battle Engines (CFEPBEConfig::Init
	FEM_BE_TRANSM,					// 0044fb4b; CFEPMultiplayerStart::Init 0051dc3f, 0051dd18)
	FEM_WILDCARD_T,
	FEM_WILDCARD_B,
	FEM_WILDCARD_J,
	FEM_PS2_LOGO,

	FEM_NUM
};

// The front end's shared data (FED, FRONTEND + 8): the camera, the textures (FET_LEVEL_LINK is the first,
// 0089d7a8) and the meshes (+0x1a0). Init and Shutdown are 004687e0 and 004691c0.
class CFrontEndData
{
public:
	BOOL			Init();
	void			Shutdown();
	CTEXTURE		*GetTexture(int n) { return mTextures[n]; }
	CMESH			*GetMesh(int n) { return mMeshes[n]; }

	CGenericCamera	mCamera;						// +0x00
	CTEXTURE		*mTextures[FET_NUM];			// +0x48
	CMESH			*mMeshes[FEM_NUM];				// +0x1a0
};

// The front-end video (FEV, FRONTEND + 0x1c4; 0x30 bytes, a CDXFrontEndVideo, constructor 00541200).
#if defined(DXFRONTENDVIDEO_CPP) || defined(FRONTEND_CPP) || defined(DXFMV_CPP) || defined(DXFRONTEND_CPP)	// the full view (FrontEnd.cpp
// constructs and destroys the video, 00465f56/0046625b; ~CDXFrontEnd destroys it too, 00540bbb); the pages' objects keep
// their declaration counts
#include "deviceobject.h"

struct BINK;
class CUMTexture;

// the platform-neutral part (RTTI: at +8 in CDXFrontEndVideo)
class CFrontEndVideo
{
public:
	CFrontEndVideo() { mBink = NULL; mFadeAlpha = 0; }

	BINK			*mBink;						// +0x08
	CUMTexture		*mTextures[2];				// +0x0c: filled in turn
	int				mCurrentTexture;			// +0x14
	BOOL			mFrameDecoded;				// +0x18: BinkDoFrame has run for the frame waiting
	BOOL			mFrameCopied;				// +0x1c: the frame is in a texture; NextFrame waits for the next
	int				mWidth;						// +0x20: the textures' size (the video's before it opens)
	int				mHeight;					// +0x24
	int				mSoundTrack;				// +0x28
	int				mFadeAlpha;					// +0x2c: 0..255, fading in once a video starts
};

class CDXFrontEndVideo : public DeviceObject, public CFrontEndVideo	// vtable 005e5084
{
public:
	CDXFrontEndVideo();									// 00541200
	// Retail 005e5084 slot 2 uses this zero-return body (the non-inline fold 005019c0), distinct from the base identity.
	virtual HRESULT	RestoreDeviceObjects();				// 2 (005019c0)
	BOOL			Init();								// 00541240
	void			Shutdown();							// 00541260

	// the Bink file, the texture size, whether to preload it, whether to open it on the open thread, and the sound
	// track (005412e0)
	void			Open(char *name, int width, int height, BOOL preload, BOOL async, int sound_track);
	void			InitVideo();						// 00541430: once the open thread has the file
	void			CloseVideo();						// 00541650
	int				GetWidth();							// 00541770
	int				GetHeight();						// 00541780
	bool			Render(float x, float y, float z, float xscale, float yscale, DWORD colour, BOOL centred);	// 00541790
	int				NextFrame(bool wait);				// 00541d30: -1 still waiting, 1 at the end
};
#else
class CDXFrontEndVideo
{
public:
	CDXFrontEndVideo();
	BOOL			Init();						// 00541240
	void			Shutdown();					// 00541260
	void			NextFrame(bool wait);		// 00541d30
	void			Open(char *name, int width, int height, int a, BOOL b, int c);		// 005412e0
	void			CloseVideo();				// 00541650
	bool			Render(float cx, float cy, float z, float xscale, float yscale, DWORD colour, BOOL a);	// 00541790

	DWORD			mUnknown00[0x30 / 4];		// (a DeviceObject first: dword-aligned)
};
#endif
#define CFRONTENDVIDEO CDXFrontEndVideo

//*********************************************************************************
// The pages without their own headers yet, as their sizes (from CFrontEnd's layout).
#define FE_PAGE(name, size) class name : public CFrontEndPage { public: UBYTE mUnknown04[(size) - 4]; };
class CFEPUnknown : public CFrontEndPage
{
public:
	// Retail 005db780 slot 5 is ret 8 at 00452da0, not the base's 00405db0.
	void Render(float transition, EFrontEndPage dest);
};

class	CFrontEnd : public IController
{
public:
	enum {NUM_CONTROLLER_PORTS = 2};

	CFrontEnd();													// 00465f10
#if !defined(FRONTEND_CPP) && !defined(DXFRONTEND_CPP)	// FrontEnd.cpp generates it (00466200: no vtable store of its
	virtual			~CFrontEnd();	// own, so it is the implicit one); DXFrontend.obj inlines the same body into ~CDXFrontEnd (00540b60)
#endif

	// external funcions
	SINT			Run(EFrontEndEntry	entry,BOOL inLoadedSystem);	// 004684d0
	bool			IsRunning();									// 00465f00: gInFrontEnd (CDXBitmapFont::DrawTextScaled 00540061; name open)
	// Three of Stuart's CFrontEnd members (his Frontend.h:130, 194, 196), declared only, never defined or called.
	// DECLARATION-COUNT CARRIERS: CSphere::CollideWithMesh's inline body (Sphere.h) flipped the
	// witness CGame::DrawDebugStuff (00470650; game.cpp reaches Sphere.h through cylinder.h); three more declarations
	// in game.cpp's closure restore it (one or two do not), and d3dapp/ParticleDescriptor's witnesses stay matched.
	void			RenderSlidingScreen(CTEXTURE *screen, int alpha, float offy, DWORD col = 0xffffffff);
	bool			SerialiseState(FILE *dest, bool saving);
	void			ForceDisplay();

	virtual void	ReceiveButtonAction(CController* from_controller, int button, float val);	// 004669a0
	virtual BOOL	CanBeControlledWhenInPause() { return(TRUE); };
	virtual EControlType  GetControlType() { return CONTROL_FRONTEND ; }

	float			GetCounter() {return mCounter;};
	void			SetQuit(SINT val) {mQuit = val;};

	void			SetPage(EFrontEndPage page, SINT time = 10);		// 00466ae0
	EFrontEndPage	GetCurrentPage() {return mActivePage;};
	EFrontEndPage	GetTransitionTo() {return mTransitionTo;};
	EFrontEndPage	GetTransitionFrom() {return mTransitionFrom;};

	CFEPCommon		*GetCommonPage() {return &mFEPCommon;};

	// graphical stuff
	void			DrawLine(float sx, float sy, float ex, float ey, DWORD col, float width, float depth, float perc = 1.f);
	void			DrawBox(float tlx, float tly, float brx, float bry, DWORD col, float width, float depth);
	void			DrawBar(float sx, float sy, float z, SINT segs, DWORD col, float scale = 1.f);
	void			DrawPanel(float tlx, float tly, float brx, float bry, float z, DWORD col);
	void			DrawBarGraph(float tlx, float tly, float brx, float bry, float num, float max, float z, SINT bordercol, SINT backcol, SINT forecol);

	void			DrawTitleBar(WCHAR *text, float transition, EFrontEndPage dest);		// 00467bd0
	void			DrawSlidingTextBordersAndMask(float transition, EFrontEndPage dest);	// 00467200
	void			DrawStandardVideoBackground(float transition, DWORD alpha, EFrontEndPage dest);	// 004679e0

	// autosaving has 3 states:
	enum AutoSaveMode
	{
		AUTO_SAVE_NOT, // it's an explicit save, ie the first save of the career
		AUTO_SAVE_NORMAL, // the usual, we autosave when the career changes ie at the end of levels.
		AUTO_SAVE_PRETEND, // as normal, we're autosaving, but the actual write to disk doesn't happen.
	};

	AutoSaveMode	GetAutoSave() { return(mAutoSave); }
	void			SetAutoSave(AutoSaveMode as) { mAutoSave = as; }

	int				GetMemoryCardNumber() { return(mMemoryCardNumber); }
	BOOL			GetSaveMode() { return(mSaveMode); }		// Stuart's
	void			SetSaveMode(BOOL mode) { mSaveMode = mode; }
	void			SetSuccessFEP(EFrontEndPage page, UINT time)	{ mOnSuccessFEP = page; mFEPTransTime = time; }
	EFrontEndPage	GetSuccessPage() { return(mOnSuccessFEP); }
	UINT			GetSuccessTransTime() { return(mFEPTransTime); }

	BOOL			AllLevelsCheatActive()							{ return mAllLevelsCheatActive; }

	// the mouse, each FALSE while the message box takes input (FrontEndData.obj 00469390..00469430): a click in the
	// rectangle acts as the button; the cursor's state in it; a click in it; the hover, consumed
	static bool		ProcessMouseClickInRect(float tlx, float tly, float brx, float bry, SINT button);
	static bool		GetCursorStateInRect(float tlx, float tly, float brx, float bry);
	static bool		MouseClickedInRect(float tlx, float tly, float brx, float bry);
	static bool		MouseOverRect(float tlx, float tly, float brx, float bry);

	// sound (called without an object: 00468770)
	static void		PlaySound(EFrontEndSound sound);

	CFrontEndData	mFrontEndData;					// +0x008: begins with a CGenericCamera (00466130)
	CFrontEndText	mFrontEndText;					// +0x1c0 (inferred: an empty class between the data and the video)
	CFRONTENDVIDEO	mFrontEndVideo;					// +0x1c4

	// Selected stuff
	SINT			mSelectedLevel;					// +0x1f4

	void			EnableAdditiveAlpha();
	void			EnableModulateAlpha();

	virtual BOOL	RenderStart();
	virtual void	RenderEnd(BOOL started);
	void			UpdateCamera();

	float			GetShadowOffsetX();
	float			GetShadowOffsetY();

	virtual void	Shutdown();

	void			SetLanguage(SINT l);			// 00466ab0

	// which port is player 0 connected to?
	int				GetPlayer0ControllerPort();

	CController		*GetController(int which) { return mControllers[which]; }	// Stuart's
	int				NumControllersPresent();		// 00466990

	CText			*GetTextSet(SINT n) {return &mTextSets[n];};

	// on reboot the platform wants to pretend that this isn't the first run
	void			ClearFirstRun() {mFirstRun = FALSE;}

	void			StartMCOperation(WCHAR *message);
	void			EndMCOperation();

protected:
	BOOL	Init(EFrontEndEntry	entry,BOOL inLoadedSystem);	// 004662a0

	void	Process();									// 00466ba0
	BOOL	Render(BOOL forcerender=FALSE);				// 00468200

	// Internal State
	EFrontEndPage	mActivePage;					// +0x1f8
	EFrontEndPage	mTransitionFrom, mTransitionTo;	// +0x1fc, +0x200
	float			mTransitionCount, mTransitionTime;	// +0x204, +0x208: frames, counted in time on the PC (00466d2d)
	EFrontEndPage	mDevReturnPage;					// +0x20c

	float			mTimerStart;					// +0x210

	// Page Pointers
	CFrontEndPage	*mPages[FEP_NUM_PAGES];			// +0x214

public:
	// controller who's who.
	int	mPlayer0ControllerPort;						// +0x274

	// Pages (the ones not yet reconstructed are their sizes)
	CFEPMain				mFEPMain;						// +0x278
	CFEPDevelopment			mFEPDevelopment;				// +0x29c
	CFEPCommon				mFEPCommon;						// +0x2b0
	CFEPBEConfig			mFEPBEConfig;					// +0x2bc
	CFEPWingmen				mFEPWingmen;					// +0x2ec
	CFEPBriefing			mFEPBriefing;					// +0x324
	CFEPDebriefing			mFEPDebriefing;					// +0x338
	CFEPLevelSelect			mFEPLevelSelect;				// +0x360
	CFEPGoodies				mFEPGoodies;					// +0x37dc
	CFEPDevSelect			mFEPDevSelect;					// +0x39b8
	CFEPLoadGame			mFEPLoadGame;					// +0x39d0
	CFEPSaveGame			mFEPSaveGame;					// +0x3c1c
	CFEPIntro				mFEPIntro;						// +0x4034
	CFEPMultiplayer			mFEPMultiplayer;				// +0x4050
	CFEPMultiplayerStart	mFEPMultiplayerStart;			// +0x40b8
	CFEPOptions				mFEPOptions;					// +0x40ec
	CFEPCredits				mFEPCredits;					// +0x4118
	CFEPScreenPos			mFEPScreenPos;					// +0x4124

	CFEPVirtualKeyboard		mFEPVirtualKeyboard;			// +0x413c
	CFEPDirectory			mFEPDirectory;					// +0x4834
	CFEPE3LevelSelect		mFEPE3LevelSelect;				// +0x8848
	CFEPLanguageTest		mFEPLanguageTest;				// +0xbcc8

	CFEPDemoMain			mFEPDemoMain;					// +0xbde0

	CFEPUnknown				mFEPUnknown;					// +0xbe04

	int						mReceivingButtonActionPort;		// +0xbe08

	friend class CControllerDefinition;		// retail PC: the controls page reads the pad's controller (004560ef)

protected:

	// Other Stuff
	CController			*mControllers[2];			// +0xbe0c (CFrontEnd::Init creates two CPCControllers)

	int					GetPortFromController(CController *controller);

public:
	SINT				mQuit;						// +0xbe14 (PCLTShell::TriggerQuit 00513730)
	float				mCounter;					// +0xbe18: advanced by the frame time (00466bb4)

	BOOL				mFirstRun;					// +0xbe1c
	float				mLastRenderTime;

	BOOL				mAllLevelsCheatActive;		// +0xbe24

	// Save Game Stuff
	BOOL				mSaveMode;					// +0xbe28
	AutoSaveMode		mAutoSave;					// +0xbe2c
	char				mSaveFilename[MAX_PATH];	// +0xbe30
	EFrontEndPage		mOnSuccessFEP;				// +0xbf34
	UINT				mFEPTransTime;				// +0xbf38
	int					mMemoryCardNumber;			// +0xbf3c

	// Language selection stuff
	CText				mTextSets[NUM_LANGUAGES];	// +0xbf40
};

class CDXFrontEnd : public CFrontEnd
{
public:
	virtual BOOL	RenderStart();					// 00540f70
	virtual void	RenderEnd(BOOL started);		// 00540fb0

	void			RenderParticles();				// 00540c30 (Stuart's DXFrontend.cpp)
	void			RenderStaticScreen(CTEXTURE *screen,int alpha, DWORD col);
};

extern CDXFrontEnd FRONTEND;

extern float	gFrontEndFrameStep;				// 00679af8: CFrontEnd::Process's filtered frame time
extern int		gAmericanText;					// 0083d990: American English text; the language row is not offered

//#define	FO(x) ((sqrtf(float(x) / 1000.f)))
#define	FO(x) (((float(x) / 1000.f)))

#endif
