// PauseMenu.cpp: the in-game menu, which the PC options page also shows (reconstructed from the retail PC executable;
// Stuart's file is not in his release). The menu items' class names are the retail RTTI's.

#include	"Common.h"
#define	PAUSEMENU_CPP

#include	"PauseMenu.h"
#include	"MenuItem.h"
#include	"MenuOptionsList.h"
#include	"MessageBox.h"
#include	"Frontend.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"music.h"
#include	"rtmesh.h"
#include	"tweak.h"
#include	"text.h"
#include	"CLIParams.h"
#include	"Controller.h"
#include	"mouse.h"
#include	"MessageLog.h"
#include	"SpriteRenderer.h"
#include	"Career.h"
#include	"MemoryCard.h"
#include	"FEPOptions.h"
#include	"player.h"
#include	"DebugLog.h"
#include	"data\MissionScripts\text\text.stf"


// the menus and their items come from the memory type 0x80, beyond Stuart's list (004cdedd)
#define MEMTYPE_MENUITEM	((EMemoryType)0x80)

// FUNCTION: 0x004cdc50 init:ZERO_FVECTOR
// FUNCTION: 0x004cdc70 init:ID_FMATRIX

// the settings the items edit, kept by their own objects
extern float		gMouseSensitivity;				// 006254f4
extern int			gControlPreset;					// 00677d70
extern char			gDisplayResetState;				// 0082b5b0: 1 asks for the display to be reset

extern CTweakSLONG	LANDSCAPE_MAXLEVELS;			// 008aa960
extern CTweakSLONG	LANDSCAPE_MAXLEVELS_USER;		// 008aa950
extern CTweakSLONG	FORCE_VSYNC;					// 0089c070
extern CTweakSLONG	SUGGEST_MULTISAMPLE_TYPE;		// 0089c080
extern CTweakSLONG	USER_TEXTURE_ALLOW_32_BIT;		// 009cc0d8
extern CTweakSLONG	TEXTURE_ALLOW_32_BIT;			// 009cc0e8
extern CTweakSLONG	USER_TEXTURE_RES_LOSS_SHIFT;	// 009cc0f8
extern CTweakSLONG	TEXTURE_RES_LOSS_SHIFT;			// 009cc108

// the controls page (00453460..00456650)
void	ApplyControlPreset(int preset);				// 00453780
bool	AllControlBindingsSet(int index);			// 00456650 (FEPControls.cpp)

// FUNCTION: 0x004cdd40 init:GEFORCE_FX_POWER
// FUNCTION: 0x004cdd60 exit:GEFORCE_FX_POWER
CTweakSLONG	GEFORCE_FX_POWER("GEFORCE_FX_POWER", 0);
static int	sSetupMessage;						// 0082b494; this and the detail items defined here: retail's .bss order (placement evidence)
// FUNCTION: 0x004cdd70
void	CGameMenu::RelinquishControl()
{
	for (int i = 0; i < 2; i++)
	{
		if (GAME.GetController(i) && (GAME.GetController(i)->GetToControl() == this))
			GAME.GetController(i)->RelinquishControl();
	}
}
static CMenuMenuItem	*sShadowDetail;	static CMenuMenuItem	*sTreeDetail;	// 0082b48c, 0082b488 (the constructor sets them)
// FUNCTION: 0x004cddc0 init:SCREEN_SHAPE
// FUNCTION: 0x004cdde0 exit:SCREEN_SHAPE
CTweakSLONG	SCREEN_SHAPE("SCREEN_SHAPE", 0);

// FUNCTION: 0x004cddf0
void	ReinitialiseSound(bool restore_music)
{
	SOUND.Reinitialise();
	if (restore_music)
	{
		if (CLIPARAMS.mMusic)
			MUSIC.PlaySelection(MUS_FRONTEND);
	}
	else
		GAME.PlayMusicForCurrentLevel();
}

// FUNCTION: 0x004cde30 init:RENDERSTATE_DISALLOW_MIPMAPPING
// FUNCTION: 0x004cde50 exit:RENDERSTATE_DISALLOW_MIPMAPPING
CTweakSLONG	RENDERSTATE_DISALLOW_MIPMAPPING("RENDERSTATE_DISALLOW_MIPMAPPING", 0);

CDXTexture	*CPauseMenu::mBlankTexture;
bool		CPauseMenu::mOptionsPage = false;

// the apply count each group of settings last acted on (00704868 counts applies)
static int	sSoundApplied = -1;						// 0063116c
static int	sDisplayApplied = -1;					// 00631170

extern "C" WCHAR	gJoystickText[208];					// 009d3060 (pausecommons.c, a COMMON)
extern "C" WCHAR	gModeText[80];						// 009d3200 (pausecommons.c, a COMMON)

// a sound setting changed: reinitialise the device once per apply
inline void	SoundSettingChanged()
{
	if (sSoundApplied != gMenuApplyCount)
	{
		sSoundApplied = gMenuApplyCount;
		ReinitialiseSound(CPauseMenu::mOptionsPage);
	}
}

// a display setting changed: reset the display once per apply
inline void	DisplaySettingChanged()
{
	if (sDisplayApplied != gMenuApplyCount)
	{
		gDisplayResetState = 1;
		sDisplayApplied = gMenuApplyCount;
	}
}

// CD3DApplication::GetAdapterInfo inlined (its out-of-line copy is Career.obj's 00420cd0): -1 is the current adapter
inline D3DAdapterInfo	*AdapterInfo(int n)
{
	if (n == -1)
		return &LT.m_Adapters[LT.m_dwAdapter];
	return &LT.m_Adapters[n];
}

//******************************************************************************************
// The items

// RTTI: CDetailItem, the base of the two detail items: three levels
class CDetailItem : public CMenuMenuItem
{
public:
	CDetailItem(EPCText text, int id) : CMenuMenuItem(text, id, false) {}

	virtual int		GetNumChoices() { return 3; }
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CShadowDetail (vtable 005de4c4)
class CShadowDetail : public CDetailItem
{
public:
	CShadowDetail(EPCText text, int id) : CDetailItem(text, id) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
};

// RTTI: CTreeDetail (vtable 005de478)
class CTreeDetail : public CDetailItem
{
public:
	CTreeDetail(EPCText text, int id) : CDetailItem(text, id) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
};

// RTTI: CScreenShape (vtable 005de394)
class CScreenShape : public CMenuMenuItem
{
public:
	CScreenShape(EPCText text, int id) : CMenuMenuItem(text, id, false) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices() { return 3; }
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CMouseSensitivityMenuItem (vtable 005de6b8)
class CMouseSensitivityMenuItem : public CScaleMenuItem
{
public:
	CMouseSensitivityMenuItem(EPCText text, int id, float value, CGameMenu *menu, int steps, bool immediate)
		: CScaleMenuItem(text, id, value, menu, steps, immediate) { ReadValue(); }

	virtual void	ValueChanged();
	virtual void	ReadValue();
};

// RTTI: CFlipSpeakers (vtable 005de1c8)
class CFlipSpeakers : public CBOOLMenuItem
{
public:
	CFlipSpeakers(EPCText text, int id) : CBOOLMenuItem(text, id, false) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
};

// RTTI: CSoundRate (vtable 005de130)
class CSoundRate : public CMenuMenuItem
{
public:
	CSoundRate(EPCText text, int id) : CMenuMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices() { return 3; }
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CHWSound (vtable 005de17c)
class CHWSound : public CBOOLMenuItem
{
public:
	CHWSound(EPCText text, int id) : CBOOLMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
};

// RTTI: CSoundDeviceSelect (vtable 005de0e4)
class CSoundDeviceSelect : public CMenuMenuItem
{
public:
	CSoundDeviceSelect(EPCText text, int id) : CMenuMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices();
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: C3DSoundMethod (vtable 005de098)
class C3DSoundMethod : public CMenuMenuItem
{
public:
	C3DSoundMethod(EPCText text, int id) : CMenuMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices() { return 3; }
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CVideoDeviceSelect (vtable 005de3e0)
class CVideoDeviceSelect : public CMenuMenuItem
{
public:
	CVideoDeviceSelect(EPCText text, int id) : CMenuMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices();
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CSCModeMenuItem (vtable 005de510): the screen resolution
class CSCModeMenuItem : public CMenuMenuItem
{
public:
	CSCModeMenuItem(EPCText text, int id) : CMenuMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices();
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CVSync (vtable 005de348)
class CVSync : public CBOOLMenuItem
{
public:
	CVSync(EPCText text, int id) : CBOOLMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices();
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CLandscapeRes (vtable 005de2fc)
class CLandscapeRes : public CMenuMenuItem
{
public:
	CLandscapeRes(EPCText text, int id) : CMenuMenuItem(text, id, false) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices();
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CTrilinear (vtable 005de42c)
class CTrilinear : public CBOOLMenuItem
{
public:
	CTrilinear(EPCText text, int id) : CBOOLMenuItem(text, id, false) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
};

// RTTI: CTextureRes (vtable 005de2b0)
class CTextureRes : public CMenuMenuItem
{
public:
	CTextureRes(EPCText text, int id) : CMenuMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices() { return 3; }
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CThreeWay, three choices, one of them the default
class CThreeWay : public CMenuMenuItem
{
public:
	CThreeWay(EPCText text, int id) : CMenuMenuItem(text, id, true) {}

	virtual int		GetDefault() = 0;
};

// RTTI: CEnable32Bit (vtable 005de260)
class CEnable32Bit : public CThreeWay
{
public:
	CEnable32Bit(EPCText text, int id) : CThreeWay(text, id) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices();
	virtual WCHAR	*GetChoiceText(int n);
	virtual int		GetDefault();
};

// RTTI: CMultiSample (vtable 005de214)
class CMultiSample : public CMenuMenuItem
{
public:
	CMultiSample(EPCText text, int id) : CMenuMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices();
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CnVidia (vtable 005de5a8): the GeForce FX features
class CnVidia : public CBOOLMenuItem
{
public:
	CnVidia(EPCText text, int id) : CBOOLMenuItem(text, id, true) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices();
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CVideoDetailLevel (vtable 005de55c): a preset that sets the other display items
class CVideoDetailLevel : public CMenuMenuItem
{
public:
	CVideoDetailLevel(EPCText text, int id) : CMenuMenuItem(text, id, false)
	{
		mResolution = NULL;
		mShadowDetail = NULL;
		mTreeDetail = NULL;
		mTrilinear = NULL;
		mVSync = NULL;
		mLandscapeRes = NULL;
		mTextureRes = NULL;
		m32Bit = NULL;
		mMultiSample = NULL;
		if (FORCE_VSYNC.mValue)
			mLowVSync = 1;
		else
			mLowVSync = 0;
		if (LANDSCAPE_MAXLEVELS.mValue < 4)
		{
			mMediumLandscapeRes = 0;
			mHighLandscapeRes = 0;
		}
		else if (LANDSCAPE_MAXLEVELS.mValue < 5)
		{
			mMediumLandscapeRes = 0;
			mHighLandscapeRes = 4;
		}
		else
		{
			mMediumLandscapeRes = 4;
			mHighLandscapeRes = 5;
		}
	}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices() { return 4; }
	virtual WCHAR	*GetChoiceText(int n);

	CMenuMenuItem	*mResolution;					// +0x28
	CMenuMenuItem	*mShadowDetail;					// +0x2c
	CMenuMenuItem	*mTreeDetail;					// +0x30
	CMenuMenuItem	*mTrilinear;					// +0x34
	CMenuMenuItem	*mVSync;						// +0x38
	CMenuMenuItem	*mLandscapeRes;					// +0x3c
	CMenuMenuItem	*mTextureRes;					// +0x40
	CThreeWay		*m32Bit;						// +0x44
	CMenuMenuItem	*mMultiSample;					// +0x48
	int				mLowVSync;						// +0x4c: FORCE_VSYNC's choice
	int				mMediumLandscapeRes;			// +0x50
	int				mHighLandscapeRes;				// +0x54
};

// RTTI: CControllerBackMenuItem (vtable 005de5f4): back from the controls, with a warning while a player has no
// free binding
class CControllerBackMenuItem : public CMenuItem
{
public:
	CControllerBackMenuItem(DWORD text, int id) : CMenuItem(text, id) {}

	virtual WCHAR	*GetText();
	virtual void	Render(float x, float y, BOOL selected);
};

// RTTI: CJoystickEnableItem (vtable 005de630): a joystick's name, and whether it is used
class CJoystickEnableItem : public CMenuItem
{
public:
	CJoystickEnableItem(int joystick) : CMenuItem((EPCText)0x5d, 3) { mJoystick = joystick; }

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val);
	virtual WCHAR	*GetText();
	virtual int		GetHeight() { return 17; }

	int				mJoystick;						// +0x1c
};

// RTTI: CConfigName (vtable 005de66c): the controls preset
class CConfigName : public CMenuMenuItem
{
public:
	CConfigName(EPCText text, int id) : CMenuMenuItem(text, id, false) {}

	virtual void	SetValue(int value);
	virtual int		GetValue();
	virtual int		GetNumChoices() { return 2; }
	virtual WCHAR	*GetChoiceText(int n);
};

#include "ControllerDefinition.h"
#include "shadows.h"

// the detail items Render keeps in step with the settings they edit, sShadowDetail and sTreeDetail, are defined above



inline void	SyncDetailItems()
{
	sShadowDetail->SetValue(sShadowDetail->GetValue());
	switch (CRTMesh::GetQualityLevel())
	{
	case 2:
		sTreeDetail->SetValue(2);
		break;
	case 1:
		sTreeDetail->SetValue(1);
		break;
	case 0:
		sTreeDetail->SetValue(0);
		break;
	}
}


extern CTweakSLONG	ALLOW_WIDESCREEN_MODES;			// 0089c0a0

//******************************************************************************************
// FUNCTION: 0x004cde60
CPauseMenu::CPauseMenu(bool options_page) : mActive(FALSE), mPage(0), mCloseTime(-1.f)
{
	mOptionsPage = options_page;

	float	y = 300.f;
	if (!options_page)
		y = 240.f;

	// the menu
#line 1337
	CMenuOptionsList	*list = new (MEMTYPE_MENUITEM) CMenuOptionsList(TEXT.GetString(GI_PAUSED), 320.f, y, FALSE, 0xb0000000);
	if (!options_page)
	{
#line 1341
		list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_CONTINUE, 2));
		list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_MESSAGE_LOG, 2));
		list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_BRIEFING, 2));
	}
#line 1346
	list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((EPCText)3, 3));
	list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((EPCText)1, 3));
	list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((EPCText)2, 3));
	if (options_page)
	{
#line 1353
		list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)JCL_CREDITS, 1));
	}
	else
	{
#line 1358
		list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_RETRY, 2));
		list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_QUIT, 2));
	}
	mLists.Append(list);

	// the controls
#line 1368
	list = new (MEMTYPE_MENUITEM) CMenuOptionsList(Localization_GetStringById(3), 320.f, y, FALSE, 0xb0000000);
#line 1373
	list->AddItem(new (MEMTYPE_MENUITEM) CMouseSensitivityMenuItem((EPCText)0x4e, 3, 0.f, NULL, 20, true));
#line 1378
	list->AddItem(new (MEMTYPE_MENUITEM) CConfigName((EPCText)0xef, 3));
	if (FRONTEND.mFEPSaveGame.IsCheatActive(3))
	{
		if (CAREER.GetIsGod(0) == 1)
		{
#line 1384
			list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_GOD_ON, 3));
		}
		else
		{
#line 1388
			list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_GOD_OFF, 3));
		}
	}
#line 1392
	list->AddItem(new (MEMTYPE_MENUITEM) CControllerDefinition());
#line 1394
	list->AddItem(new (MEMTYPE_MENUITEM) CJoystickEnableItem(0));
	list->AddItem(new (MEMTYPE_MENUITEM) CJoystickEnableItem(1));
	list->AddItem(new (MEMTYPE_MENUITEM) CJoystickEnableItem(2));
	list->AddItem(new (MEMTYPE_MENUITEM) CJoystickEnableItem(3));
#line 1399
	list->AddItem(new (MEMTYPE_MENUITEM) CControllerBackMenuItem((DWORD)GI_BACK, 3));
	mLists.Append(list);

	// the display
#line 1405
	list = new (MEMTYPE_MENUITEM) CMenuOptionsList(Localization_GetStringById(2), 320.f, y, FALSE, 0xb0000000);
#line 1408
	list->AddItem(new (MEMTYPE_MENUITEM) CnVidia((EPCText)0xf7, 3));
#line 1411
	CVideoDetailLevel	*detail = new (MEMTYPE_MENUITEM) CVideoDetailLevel((EPCText)0xdf, 3);
	list->AddItem(detail);
#line 1415
	detail->mResolution = new (MEMTYPE_MENUITEM) CSCModeMenuItem((EPCText)0x13, 3);
	list->AddItem(detail->mResolution);
#line 1416
	detail->mShadowDetail = sShadowDetail = new (MEMTYPE_MENUITEM) CShadowDetail((EPCText)0xe, 3);
	list->AddItem(sShadowDetail);
#line 1417
	detail->mTreeDetail = sTreeDetail = new (MEMTYPE_MENUITEM) CTreeDetail((EPCText)0xf, 3);
	list->AddItem(sTreeDetail);
#line 1418
	detail->mTrilinear = new (MEMTYPE_MENUITEM) CTrilinear((EPCText)0x26, 3);
	list->AddItem(detail->mTrilinear);
#line 1421
	list->AddItem(new (MEMTYPE_MENUITEM) CVideoDeviceSelect((EPCText)0xde, 3));
	if (ALLOW_WIDESCREEN_MODES.mValue)
	{
#line 1427
		list->AddItem(new (MEMTYPE_MENUITEM) CScreenShape((EPCText)0xeb, 3));
	}
#line 1430
	detail->mVSync = new (MEMTYPE_MENUITEM) CVSync((EPCText)0x14, 3);
	list->AddItem(detail->mVSync);
#line 1431
	detail->mLandscapeRes = new (MEMTYPE_MENUITEM) CLandscapeRes((EPCText)0x15, 3);
	list->AddItem(detail->mLandscapeRes);
#line 1432
	detail->mTextureRes = new (MEMTYPE_MENUITEM) CTextureRes((EPCText)0x16, 3);
	list->AddItem(detail->mTextureRes);
#line 1433
	detail->m32Bit = new (MEMTYPE_MENUITEM) CEnable32Bit((EPCText)0x1d, 3);
	list->AddItem(detail->m32Bit);
#line 1434
	detail->mMultiSample = new (MEMTYPE_MENUITEM) CMultiSample((EPCText)0xd3, 3);
	list->AddItem(detail->mMultiSample);
#line 1436
	list->AddItem(new (MEMTYPE_MENUITEM) CApplyMenuItem(list));
	SyncDetailItems();
#line 1441
	list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_BACK, 3));
	mLists.Append(list);

	// the sound
#line 1448
	list = new (MEMTYPE_MENUITEM) CMenuOptionsList(Localization_GetStringById(1), 320.f, y, FALSE, 0xb0000000);
	float	volume;
#line 1450
	list->AddItem(new (MEMTYPE_MENUITEM) CScaleMenuItem((DWORD)SOUND_VOLUME, 3, volume = CAREER.GetSoundVolume(), this, 10, true));
	list->AddItem(new (MEMTYPE_MENUITEM) CScaleMenuItem((DWORD)MUSIC_VOLUME, 3, volume = CAREER.GetMusicVolume(), this, 10, true));
	list->AddItem(new (MEMTYPE_MENUITEM) CFlipSpeakers((EPCText)4, 3));
	list->AddItem(new (MEMTYPE_MENUITEM) CHWSound((EPCText)7, 3));
	list->AddItem(new (MEMTYPE_MENUITEM) CSoundRate((EPCText)0xd8, 3));
#line 1457
	list->AddItem(new (MEMTYPE_MENUITEM) CSoundDeviceSelect((EPCText)9, 3));
#line 1459
	list->AddItem(new (MEMTYPE_MENUITEM) C3DSoundMethod((EPCText)0xa, 3));
#line 1461
	list->AddItem(new (MEMTYPE_MENUITEM) CApplyMenuItem(list));
	list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_BACK, 3));
	mLists.Append(list);

	mCircle1 = NULL;
	mCircle2 = NULL;
	mBlankTexture = NULL;
	mChosen = 0;
	mConfirm = NULL;
	mQuitConfirm = NULL;
	mNoFade = FALSE;
	mFirstRenderTime = 0;
}

//******************************************************************************************
// FUNCTION: 0x004ceed0
WCHAR	*CDetailItem::GetChoiceText(int n)
{
	return Localization_GetStringById(n + 0x10);
}

// FUNCTION: 0x004ceef0
void	CShadowDetail::SetValue(int value)
{
	switch (value)
	{
	default:
		SHADOWS.mShadows = false;
		SHADOWS.mBlobShadows = false;
		break;
	case 2:
		SHADOWS.mShadows = true;
		SHADOWS.mBlobShadows = true;
		break;
	case 1:
		SHADOWS.mShadows = false;
		SHADOWS.mBlobShadows = true;
		break;
	}
}

// FUNCTION: 0x004cef30
int		CShadowDetail::GetValue()
{
	if (SHADOWS.mShadows)
		return 2;
	return SHADOWS.mBlobShadows != 0;
}

// FUNCTION: 0x004cef50
void	CTreeDetail::SetValue(int value)
{
	CRTMesh::SetQualityLevel(value);
}

// FUNCTION: 0x004cef60
int		CTreeDetail::GetValue()
{
	return CRTMesh::GetQualityLevel();
}

// FUNCTION: 0x004cef70
void	CScreenShape::SetValue(int value)
{
	SCREEN_SHAPE.Set(float(value));
	LT.UpdateScreenShape();
}

// FUNCTION: 0x004cef90
int		CScreenShape::GetValue()
{
	return SCREEN_SHAPE.mValue;
}

// FUNCTION: 0x004cefa0
WCHAR	*CScreenShape::GetChoiceText(int n)
{
	switch (n)
	{
	default:
		return Localization_GetStringById(0xec);
	case 2:
		return Localization_GetStringById(0xee);
	case 1:
		return Localization_GetStringById(0xed);
	}
}

// FUNCTION: 0x004cefe0
void	CMouseSensitivityMenuItem::ValueChanged()
{
	gMouseSensitivity = float(mValue + 1) * 3.f;
}

// FUNCTION: 0x004cf000
void	CMouseSensitivityMenuItem::ReadValue()
{
	mCommittedValue = mValue = int(gMouseSensitivity / 3.f + 0.5f) - 1;
}

// FUNCTION: 0x004cf030 ??_GCMouseSensitivityMenuItem@@UAEPAXI@Z
// FUNCTION: 0x004cf050 ??1CMouseSensitivityMenuItem@@UAE@XZ

// FUNCTION: 0x004cf060
void	CFlipSpeakers::SetValue(int value)
{
	CLIPARAMS.mFlipSpeakers = value;
}

// FUNCTION: 0x004cf070
int		CFlipSpeakers::GetValue()
{
	return CLIPARAMS.mFlipSpeakers;
}

// FUNCTION: 0x004cf080
void	CSoundRate::SetValue(int value)
{
	CLIPARAMS.mSoundRate = value;
	SoundSettingChanged();
}

// FUNCTION: 0x004cf0e0
int		CSoundRate::GetValue()
{
	return CLIPARAMS.mSoundRate;
}

// FUNCTION: 0x004cf0f0
WCHAR	*CSoundRate::GetChoiceText(int n)
{
	return Localization_GetStringById(n + 0xd9);
}

// FUNCTION: 0x004cf110
void	CHWSound::SetValue(int value)
{
	CLIPARAMS.mHWSound = value;
	SoundSettingChanged();
}

// FUNCTION: 0x004cf170
int		CHWSound::GetValue()
{
	return CLIPARAMS.mHWSound;
}

// FUNCTION: 0x004cf180
void	CSoundDeviceSelect::SetValue(int value)
{
	CLIPARAMS.mSoundDevice = value;
	SoundSettingChanged();
}

// FUNCTION: 0x004cf1e0
int		CSoundDeviceSelect::GetValue()
{
	return CLIPARAMS.mSoundDevice;
}

// FUNCTION: 0x004cf1f0
WCHAR	*CSoundDeviceSelect::GetChoiceText(int n)
{
	if (!CPCSoundManager::GetNumDevices())
		return Localization_GetStringById(0xd7);
	return CPCSoundManager::GetDeviceName(n);
}

// FUNCTION: 0x004cf220
int		CSoundDeviceSelect::GetNumChoices()
{
	int		n = CPCSoundManager::GetNumDevices();
	if (!n)
		n = 1;
	return n;
}

// FUNCTION: 0x004cf230
void	C3DSoundMethod::SetValue(int value)
{
	CLIPARAMS.m3DSoundMethod = value;
	SoundSettingChanged();
}

// FUNCTION: 0x004cf290
int		C3DSoundMethod::GetValue()
{
	int		method = CLIPARAMS.m3DSoundMethod;
	if (method == -1)
		method = SOUND.m3DSoundMethod;
	return method;
}

// FUNCTION: 0x004cf2a0
WCHAR	*C3DSoundMethod::GetChoiceText(int n)
{
	return Localization_GetStringById(n + 0xb);
}

// FUNCTION: 0x004cf2c0
void	CVideoDeviceSelect::SetValue(int value)
{
	LT.m_dwAdapter = value;
	DisplaySettingChanged();
}

// FUNCTION: 0x004cf2f0
int		CVideoDeviceSelect::GetValue()
{
	return LT.m_dwAdapter;
}

// FUNCTION: 0x004cf300
WCHAR	*CVideoDeviceSelect::GetChoiceText(int n)
{
	return ToWCHAR(AdapterInfo(n)->d3dAdapterIdentifier.Description);
}

// FUNCTION: 0x004cf360
int		CVideoDeviceSelect::GetNumChoices()
{
	return LT.m_dwNumAdapters;
}

// FUNCTION: 0x004cf370
void	CSCModeMenuItem::SetValue(int value)
{
	LT.GetCurrentDevice()->dwCurrentMode = value;
	DisplaySettingChanged();
}

// FUNCTION: 0x004cf3e0
int		CSCModeMenuItem::GetValue()
{
	return LT.GetCurrentDevice()->dwCurrentMode;
}

// FUNCTION: 0x004cf430
WCHAR	*CSCModeMenuItem::GetChoiceText(int n)
{
	D3DDeviceInfo	*device = LT.GetCurrentDevice();
	swprintf(gModeText, L"%ld x %ld", device->modes[n].Width, device->modes[n].Height);
	return gModeText;
}

// FUNCTION: 0x004cf4b0
int		CSCModeMenuItem::GetNumChoices()
{
	return LT.GetCurrentDevice()->dwNumModes;
}

// FUNCTION: 0x004cf500
void	CVSync::SetValue(int value)
{
	CLIPARAMS.mVSync = value;
	DisplaySettingChanged();
}

// FUNCTION: 0x004cf530
int		CVSync::GetValue()
{
	return FORCE_VSYNC.mValue ? 0 : CLIPARAMS.mVSync;
}

// FUNCTION: 0x004cf550
WCHAR	*CVSync::GetChoiceText(int n)
{
	if (FORCE_VSYNC.mValue)
		return Localization_GetStringById(5);
	return CBOOLMenuItem::GetChoiceText(n);
}

// FUNCTION: 0x004cf580
int		CVSync::GetNumChoices()
{
	return FORCE_VSYNC.mValue ? 1 : 2;
}

// FUNCTION: 0x004cf590
WCHAR	*CLandscapeRes::GetChoiceText(int n)
{
	if (n == 0)
		return Localization_GetStringById(0x25);
	if (n == 1)
		return Localization_GetStringById(0x24);
	if (n == 2)
		return Localization_GetStringById(0x23);
	return Localization_GetStringById(0x22);
}

// FUNCTION: 0x004cf5e0
int		CLandscapeRes::GetNumChoices()
{
	switch (LANDSCAPE_MAXLEVELS.mValue)
	{
	case 1:
	case 2:
		return 1;
	case 3:
		return 2;
	case 4:
		return 3;
	default:
		return 4;
	}
}

// FUNCTION: 0x004cf620
void	CLandscapeRes::SetValue(int value)
{
	if (value > LANDSCAPE_MAXLEVELS.mValue)
		value = LANDSCAPE_MAXLEVELS.mValue;

	switch (value)
	{
	case 0:
		LANDSCAPE_MAXLEVELS_USER.Set(1.f);
		break;
	case 1:
		LANDSCAPE_MAXLEVELS_USER.Set(3.f);
		break;
	case 2:
		LANDSCAPE_MAXLEVELS_USER.Set(4.f);
		break;
	default:
		LANDSCAPE_MAXLEVELS_USER.Set(5.f);
		break;
	}
}

// FUNCTION: 0x004cf690
int		CLandscapeRes::GetValue()
{
	int		max = LANDSCAPE_MAXLEVELS.mValue;
	int		user = LANDSCAPE_MAXLEVELS_USER.mValue;
	if (user > max)
		return max;
	if (user == 1)
		return 0;
	if (user == 3)
		return 1;
	return (user != 4) + 2;
}

// FUNCTION: 0x004cf6c0
void	CTrilinear::SetValue(int value)
{
	value = !value;
	RENDERSTATE_DISALLOW_MIPMAPPING.Set(float(value));
}

// FUNCTION: 0x004cf6f0
int		CTrilinear::GetValue()
{
	return !RENDERSTATE_DISALLOW_MIPMAPPING.mValue;
}

// FUNCTION: 0x004cf700
WCHAR	*CTextureRes::GetChoiceText(int n)
{
	if (n == TEXTURE_RES_LOSS_SHIFT.mValue)
	{
		switch (n)
		{
		default:
			return Localization_GetStringById(0x19);
		case 1:
			return Localization_GetStringById(0x18);
		case 0:
			return Localization_GetStringById(0x17);
		}
	}
	switch (n)
	{
	default:
		return Localization_GetStringById(0x1c);
	case 1:
		return Localization_GetStringById(0x1b);
	case 0:
		return Localization_GetStringById(0x1a);
	}
}

// FUNCTION: 0x004cf770
void	CTextureRes::SetValue(int value)
{
	USER_TEXTURE_RES_LOSS_SHIFT.Set(float(value));
	DisplaySettingChanged();
}

// FUNCTION: 0x004cf7a0
int		CTextureRes::GetValue()
{
	if (USER_TEXTURE_RES_LOSS_SHIFT.mValue > -1)
		return USER_TEXTURE_RES_LOSS_SHIFT.mValue;
	return TEXTURE_RES_LOSS_SHIFT.mValue;
}

// FUNCTION: 0x004cf7b0
WCHAR	*CEnable32Bit::GetChoiceText(int n)
{
	if (n == GetDefault())
	{
		switch (n)
		{
		default:
			return Localization_GetStringById(0x1f);
		case 1:
			return Localization_GetStringById(0x21);
		case 0:
			return Localization_GetStringById(0x20);
		}
	}
	switch (n)
	{
	default:
		return Localization_GetStringById(5);
	case 1:
		return Localization_GetStringById(0x1e);
	case 0:
		return Localization_GetStringById(6);
	}
}

// FUNCTION: 0x004cf820
void	CEnable32Bit::SetValue(int value)
{
	USER_TEXTURE_ALLOW_32_BIT.Set(float(value));
	DisplaySettingChanged();
}

// FUNCTION: 0x004cf850
int		CEnable32Bit::GetDefault()
{
	return TEXTURE_ALLOW_32_BIT.mValue;
}

// the current device's current display mode
inline D3DModeInfo	*CurrentMode()
{
	D3DDeviceInfo	*device = LT.GetCurrentDevice();
	return &device->modes[device->dwCurrentMode];
}

// FUNCTION: 0x004cf860
int		CEnable32Bit::GetValue()
{
	if (USER_TEXTURE_ALLOW_32_BIT.IsSet())
		return USER_TEXTURE_ALLOW_32_BIT.mValue;
	return TEXTURE_ALLOW_32_BIT.mValue;
}

// FUNCTION: 0x004cf880
int		CEnable32Bit::GetNumChoices()
{
	if (CurrentMode()->mUnknown10 & 4)
		return 3;
	return 1;
}

// The multisample types the current mode supports, in order: bits 5-9 of its flags are 2, 3, 4, 6 and 8 samples.
// The n-th supported type (1-based; 0 is none), and the reverse. Each test reads the mode's flags again (retail
// keeps the mode pointer in esi though the loads fold, 004cfb44), and none returns n itself (no zero is loaded).
inline int	MultiSampleType(int n)
{
	if (!n)
		return n;

	D3DModeInfo	*mode = CurrentMode();
	if (mode->mUnknown10 & 0x20)
		n--;
	if (!n)
		return 2;
	if (mode->mUnknown10 & 0x40)
		n--;
	if (!n)
		return 3;
	if (mode->mUnknown10 & 0x80)
		n--;
	if (!n)
		return 4;
	if (mode->mUnknown10 & 0x100)
		n--;
	if (!n)
		return 6;
	if (mode->mUnknown10 & 0x200)
		n--;
	return n ? 0 : 8;
}

inline int	MultiSampleIndex(int type)
{
	int		n = 0;
	D3DModeInfo	*mode = CurrentMode();
	if (mode->mUnknown10 & 0x20)
	{
		n++;
		if (type == 2)
			return n;
	}
	if (mode->mUnknown10 & 0x40)
	{
		n++;
		if (type == 3)
			return n;
	}
	if (mode->mUnknown10 & 0x80)
	{
		n++;
		if (type == 4)
			return n;
	}
	if (mode->mUnknown10 & 0x100)
	{
		n++;
		if (type == 6)
			return n;
	}
	if (mode->mUnknown10 & 0x200)
	{
		n++;
		if (type == 8)
			return n;
	}
	return 0;
}

// FUNCTION: 0x004cf8e0
WCHAR	*CMultiSample::GetChoiceText(int n)
{
	switch (MultiSampleType(n))
	{
	case 2:		return L"2";
	case 3:		return L"3";
	case 4:		return L"4";
	case 5:		return L"5";
	case 6:		return L"6";
	case 7:		return L"7";
	case 8:		return L"8";
	case 9:		return L"9";
	case 10:	return L"10";
	case 11:	return L"11";
	case 12:	return L"12";
	case 13:	return L"13";
	case 14:	return L"14";
	case 15:	return L"15";
	case 16:	return L"16";
	default:	return Localization_GetStringById(0xd4);
	}
}

// FUNCTION: 0x004cfa70
int		CMultiSample::GetNumChoices()
{
	D3DModeInfo	*mode = CurrentMode();
	int		n = 1;

	if (mode->mUnknown10 & 0x20)
		n++;
	if (mode->mUnknown10 & 0x40)
		n++;
	if (mode->mUnknown10 & 0x80)
		n++;
	if (mode->mUnknown10 & 0x100)
		n++;
	if (mode->mUnknown10 & 0x200)
		n++;
	return n;
}

// FUNCTION: 0x004cfaf0
void	CMultiSample::SetValue(int value)
{
	D3DDeviceInfo	*device = LT.GetCurrentDevice();
	device->MultiSampleType = (D3DMULTISAMPLE_TYPE)MultiSampleType(value);
	DisplaySettingChanged();
}

// FUNCTION: 0x004cfbd0
int		CMultiSample::GetValue()
{
	int		type = LT.GetCurrentDevice()->MultiSampleType;

	if (type != -1)
	{
		if (!type)
			return 0;
		int		index = MultiSampleIndex(type);
		if (index != -1)
			return index;
	}

	int		suggest = SUGGEST_MULTISAMPLE_TYPE.mValue;
	if (suggest == -1)
		return -1;
	if (!suggest)
		return 0;
	return MultiSampleIndex(suggest);
}

// FUNCTION: 0x004cfd00
void	CnVidia::SetValue(int value)
{
	EXTRA_GEFORCE_FX_FEATURES.Set(float(value));
	DisplaySettingChanged();
}

// FUNCTION: 0x004cfd30
int		CnVidia::GetValue()
{
	return (GetNumChoices() == 2) ? EXTRA_GEFORCE_FX_FEATURES.mValue : 0;
}

// FUNCTION: 0x004cfd50
int		CnVidia::GetNumChoices()
{
	if (GEFORCE_FX_POWER.mValue && (LT.m_d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2, 0)))
		return 2;
	return 1;
}

// FUNCTION: 0x004cfd70
WCHAR	*CnVidia::GetChoiceText(int n)
{
	if (GEFORCE_FX_POWER.mValue && (LT.m_d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2, 0)))
		return CBOOLMenuItem::GetChoiceText(n);
	return Localization_GetStringById(0xf8);
}

// FUNCTION: 0x004cfdb0
void	CVideoDetailLevel::SetValue(int value)
{
	switch (value)
	{
	case 3:
		mResolution->SetSelection(LT.GetCurrentDevice()->dwHighDetailMode);
		mShadowDetail->SetSelection(2);
		mTreeDetail->SetSelection(2);
		mVSync->SetSelection(1);
		mLandscapeRes->SetSelection(mHighLandscapeRes);
		mTrilinear->SetSelection(1);
		mTextureRes->SetSelection(TEXTURE_RES_LOSS_SHIFT.mValue);
		m32Bit->SetSelection(m32Bit->GetDefault());
		if (mMultiSample->GetNumChoices() > 1)
			mMultiSample->SetSelection(1);
		break;
	case 2:
		mResolution->SetSelection(LT.GetCurrentDevice()->dwMediumDetailMode);
		mShadowDetail->SetSelection(1);
		mTreeDetail->SetSelection(1);
		mVSync->SetSelection(1);
		mLandscapeRes->SetSelection(mMediumLandscapeRes);
		mTrilinear->SetSelection(1);
		mTextureRes->SetSelection(TEXTURE_RES_LOSS_SHIFT.mValue);
		m32Bit->SetSelection(m32Bit->GetDefault());
		mMultiSample->SetSelection(0);
		break;
	case 1:
		mResolution->SetSelection(LT.GetCurrentDevice()->dwLowDetailMode);
		mShadowDetail->SetSelection(0);
		mTreeDetail->SetSelection(0);
		mVSync->SetSelection(mLowVSync);
		mLandscapeRes->SetSelection(0);
		mTrilinear->SetSelection(0);
		mTextureRes->SetSelection(2);
		m32Bit->SetSelection(1);
		mMultiSample->SetSelection(0);
		break;
	}
}

// FUNCTION: 0x004cffd0
int		CVideoDetailLevel::GetValue()
{
	if ((mResolution->GetSelection() == LT.GetCurrentDevice()->dwLowDetailMode) && (mShadowDetail->GetSelection() == 0) && (mTreeDetail->GetSelection() == 0) &&
		(mVSync->GetSelection() == mLowVSync) && (mLandscapeRes->GetSelection() == 0) && (mTrilinear->GetSelection() == 0) &&
		(mTextureRes->GetSelection() == 2) && (m32Bit->GetSelection() == 1) && (mMultiSample->GetSelection() == 0))
		return 1;

	if ((mResolution->GetSelection() == LT.GetCurrentDevice()->dwMediumDetailMode) && (mShadowDetail->GetSelection() == 1) && (mTreeDetail->GetSelection() == 1) &&
		(mVSync->GetSelection() == 1) && (mLandscapeRes->GetSelection() == mMediumLandscapeRes) && (mTrilinear->GetSelection() == 1) &&
		(mTextureRes->GetSelection() == TEXTURE_RES_LOSS_SHIFT.mValue) && (m32Bit->GetSelection() == m32Bit->GetDefault()) &&
		(mMultiSample->GetSelection() == 0))
		return 2;

	if ((mResolution->GetSelection() == LT.GetCurrentDevice()->dwHighDetailMode) && (mShadowDetail->GetSelection() == 2) &&
		(mTreeDetail->GetSelection() == 2) && (mVSync->GetSelection() == 1) && (mLandscapeRes->GetSelection() == mHighLandscapeRes) &&
		(mTrilinear->GetSelection() == 1) && (mTextureRes->GetSelection() == TEXTURE_RES_LOSS_SHIFT.mValue) &&
		(m32Bit->GetSelection() == m32Bit->GetDefault()) && ((mMultiSample->GetNumChoices() == 1) || (mMultiSample->GetSelection() == 1)))
		return 3;

	return 0;
}

// FUNCTION: 0x004d01a0
WCHAR	*CVideoDetailLevel::GetChoiceText(int n)
{
	return Localization_GetStringById(n + 0xe0);
}

// the menu items' destructors fold into one (the vtables of C3DSoundMethod .. CConfigName)
// FUNCTION: 0x004d01c0 ??1C3DSoundMethod@@UAE@XZ

// FUNCTION: 0x004d01d0
void	CConfigName::SetValue(int value)
{
	ApplyControlPreset(value);
}

// FUNCTION: 0x004d01e0
int		CConfigName::GetValue()
{
	return gControlPreset;
}

// FUNCTION: 0x004d01f0
WCHAR	*CConfigName::GetChoiceText(int n)
{
	if (!n)
		return Localization_GetStringById(0xf0);
	return Localization_GetStringById(0xf3);
}

// FUNCTION: 0x004d0220
WCHAR	*CControllerBackMenuItem::GetText()
{
	WCHAR	*warning = CPauseMenu::GetBindingWarning();
	if (warning)
		return warning;
	return TEXT.GetString(0x6f9da);
}

// FUNCTION: 0x004d0290
void	CControllerBackMenuItem::Render(float x, float y, BOOL selected)
{
	DWORD	col = 0xffffffff;
	if (CPauseMenu::GetBindingWarning())
		col = 0xffff8080;
	CMenuItem::Render(x, y, selected, col);
}

// FUNCTION: 0x004d0310
WCHAR	*CJoystickEnableItem::GetText()
{
	wcscpy(gJoystickText, Localization_GetStringById(mJoystick + 0x5e));
	wcscat(gJoystickText, L": ");

	DIPROPSTRING	name;
	name.diph.dwSize = sizeof(DIPROPSTRING);
	name.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	name.diph.dwObj = 0;
	name.diph.dwHow = DIPH_DEVICE;

	if ((mJoystick < LT.mJoypads) && (LT.pJoystick[mJoystick]->GetProperty(DIPROP_INSTANCENAME, &name.diph) >= 0))
	{
		wcscat(gJoystickText, name.wsz);
		wcscat(gJoystickText, L": ");
		wcscat(gJoystickText, Localization_GetStringById((LT.mUnknown33474[mJoystick] != 0) + 0x64));
		mEnabled = TRUE;
		return gJoystickText;
	}

	wcscat(gJoystickText, Localization_GetStringById(0x63));
	mEnabled = FALSE;
	return gJoystickText;
}

// FUNCTION: 0x004d0410
void	CJoystickEnableItem::ReceiveButtonAction(CController *from_controller, int button, float val)
{
	if (button == BUTTON_FRONTEND_MENU_SELECT)
	{
		LT.mUnknown33474[mJoystick] ^= 1;
		if (!LT.mUnknown33474[mJoystick])
		{
			// the joystick's bindings go
			for (SControlBinding *binding = CONTROL_BINDINGS; binding->mId != -1; binding++)
			{
				for (int i = 0; i < 2; i++)
				{
					if ((binding->mInput[i].mPad == mJoystick) && (binding->mInput[i].mType >= BUTTON_ON) &&
						(binding->mInput[i].mType <= ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT))
						binding->mInput[i].mPad = -1;
				}
			}
		}
		ApplyControlPreset(gControlPreset);
	}
}

//******************************************************************************************
// The menu

// FUNCTION: 0x004d0490 ??_GC3DSoundMethod@@UAEPAXI@Z

// FUNCTION: 0x004d04b0 ??_GCPauseMenu@@UAEPAXI@Z

// FUNCTION: 0x004d04d0
void	CPauseMenu::LoadBlankTexture()
{
	if (mBlankTexture)
	{
		mBlankTexture->Release();
		mBlankTexture = NULL;
	}
	mBlankTexture = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Blank.tga", 4, 0, 1, 0, 1);
}

// FUNCTION: 0x004d0510
void	CPauseMenu::InitResources()
{
	CMenuOptionsList	*list;
	FOR_ALL_ITEMS_IN(mLists, list)
		list->LoadResources();

	mCircle1 = CDXTexture::FindTexture("pausemenu\\pause_circle01.tga", 4, 0, 1, 0, 1);
	mCircle2 = CDXTexture::FindTexture("pausemenu\\pause_circle02.tga", 4, 0, 1, 0, 1);
	LoadBlankTexture();
}

// FUNCTION: 0x004d05c0
bool	CPauseMenu::IsConfirming()
{
	return mConfirm && mConfirm->mActive;
}

// FUNCTION: 0x004d05e0
CPauseMenu::~CPauseMenu()
{
	mLists.DeleteAll();
	SAFE_DELETE(mConfirm);
	SAFE_DELETE(mQuitConfirm);
	if (mCircle1)
	{
		mCircle1->Release();
		mCircle1 = NULL;
	}
	if (mCircle2)
	{
		mCircle2->Release();
		mCircle2 = NULL;
	}
	if (mBlankTexture)
	{
		mBlankTexture->Release();
		mBlankTexture = NULL;
	}
}

// FUNCTION: 0x004d06e0
void	CPauseMenu::Resume(CController *from_controller)
{
	if (!mOptionsPage)
	{
		RelinquishControl();
		GAME.UnPause();
		InitMouse();

		int		size = CAREER.SizeOfSaveGame();
#line 1547
		char	*buffer = new (MEMTYPE_SCRATCHPAD) char[size];
		CAREER.Save(buffer);

		if (FRONTEND.mFEPSaveGame.mSaveGameName[0])
		{
			int save;
			if (MEMORYCARD.CreateSave(FRONTEND.GetMemoryCardNumber(), FRONTEND.mFEPSaveGame.mSaveGameName, &save, TRUE) == MCE_SUCCESS)
				MEMORYCARD.WriteSave(FRONTEND.GetMemoryCardNumber(), save, FRONTEND.mFEPSaveGame.mSaveGameName, buffer, size);
		}

		CFEPOptions::WriteDefaultOptionsFile(buffer, size);
		delete [] buffer;
	}
	DeActivate(TRUE);
}

// back to the first page, unless a player has no free binding to leave the controls page with
static inline void	BackToFirstPage(CPauseMenu *menu)
{
	if ((menu->mPage == 1) && CPauseMenu::GetBindingWarning())
		return;
	menu->mPage = 0;
	menu->mLists.At(0)->Reset();
	CFrontEnd::PlaySound(FES_BACK);
}

// the controllers controlling one menu go to another
static inline void	HandControl(IController *from, IController *to)
{
	for (int i = 0; i < 2; i++)
	{
		if (GAME.GetController(i) && (GAME.GetController(i)->GetToControl() == from))
			GAME.GetController(i)->SetToControl(to);
	}
}

// Retail initializes CGameMenu before resolving the confirmation title (004d0917, 004d093f).
inline CSimpleGameMenu::CSimpleGameMenu(DWORD title, float x, float y, BOOL panel, DWORD colour)
	: mList(TEXT.GetString(title), x, y, panel, colour)
{
	mList.LoadResources();
}

// STAND-IN: the guard and four consumed getter sites model unknown original source boundaries.
// Pinned VC6 measures ButtonPressed at own 750 / budget 1500: after credits, the constructor gets
// (1344 - 147) / 26 = 46, so typed SPtrSet<CMenuEvent> (size 47) stays called (00505d00 -> 004e5840).
// The guard alone is own 754 / budget 1508, with constructor quota 50; without it the layout differs.
// No extra reads or discarded calls are introduced; the sound outcome shares retail's early epilogue.
// Replace these ownership stand-ins if evidence recovers the original form while preserving the whole match.
static inline bool CanReturnToPauseRoot(CPauseMenu *menu)
{
	return (menu->mPage != 1) || !CPauseMenu::GetBindingWarning();
}

static inline int PauseItemPCText(const CMenuItem *item)
{
	return item->mPCText;
}

static inline int PausePage(const CPauseMenu *menu)
{
	return menu->mPage;
}

static inline CPlayer *PausePlayer(SINT player)
{
	return GAME.GetPlayer(player);
}

// FUNCTION: 0x004d0810
void	CPauseMenu::ButtonPressed(CMenuItem *item, CController *from_controller)
{
	BOOL back_sound = FALSE;
	mChosen = item->mText;

	switch (item->mText)
	{
	case JCL_CREDITS:
		DeActivate(FALSE);
		mResult = 1;
		break;

	case GI_RETRY:
	case GI_QUIT:
#line 1633
		mConfirm = new (MEMTYPE_MENUITEM) CSimpleGameMenu((DWORD)IG_SURE, 320.f, 320.f, TRUE, 0xb000c000);
		mConfirm->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)IG_NO, 3), this, PAUSE_MENU_NO);
		mConfirm->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)IG_YES, 3), this, PAUSE_MENU_YES);
		HandControl(this, mConfirm);
		mConfirm->mActive = true;
		break;

	default:
		break;

	case GI_BACK:
		if (!CanReturnToPauseRoot(this))
			return;
		mPage = 0;
		mLists.At(0)->Reset();
		back_sound = TRUE;
		break;

	case 0:
		switch (PauseItemPCText(item))
		{
		case 2:
			mPage += 2;
			mLists.At(mPage)->Reset();
			break;
		case 1:
			mPage += 3;
			mLists.At(mPage)->Reset();
			break;
		case 3:
			mPage++;
			mLists.At(mPage)->Reset();
			break;
		case 78:
		case 79:
			return;
		}
		break;

	case GI_GOD_OFF:
		item->mText = GI_GOD_ON;
		PausePlayer(0)->SetIsGod(TRUE);
		break;

	case GI_GOD_ON:
		item->mText = GI_GOD_OFF;
		PausePlayer(0)->SetIsGod(FALSE);
		break;

	case GI_BRIEFING:
		{
			DeActivate(FALSE);
			CLevelBriefingLog	*log = GAME.GetLevelBriefingLog();
			HandControl(this, log);
			GAME.GetLevelBriefingLog()->Activate();
		}
		break;

	case FETX_OPTIONS:
		mPage = PausePage(this) + 1;
		mLists.At(mPage)->Reset();
		break;

	case GI_CONTINUE:
		Resume(from_controller);
		back_sound = TRUE;
		break;

	case MUSIC_VOLUME:
	case SOUND_VOLUME:
		return;

	case GI_MESSAGE_LOG:
		{
			DeActivate(FALSE);
			CMessageLog	*log = GAME.GetMessageLog();
			HandControl(this, log);
			GAME.GetMessageLog()->Activate();
		}
		break;
	}
	if (!back_sound)
		CFrontEnd::PlaySound(FES_SELECT);
	else
		CFrontEnd::PlaySound(FES_BACK);
}

// FUNCTION: 0x004d0db0
CMenuEvent::CMenuEvent(CMenuItem *item, CMonitor *to_call, int event)
	: CEvent(event, to_call)
{
	mItem = item;
}

// FUNCTION: 0x004d0de0
WCHAR	*CPauseMenu::GetBindingWarning()
{
	if (!AllControlBindingsSet(0))
		return Localization_GetStringById(0xe8);
	if ((GAME.GetGameState() >= GAME_STATE_PRE_RUNNING) && (GAME.GetGameState() < GAME_STATE_QUIT) && GAME.IsMultiplayer() &&
		(!AllControlBindingsSet(1)))
		return Localization_GetStringById(0xe9);
	return NULL;
}

// FUNCTION: 0x004d0e40
CGameMenu::CGameMenu()
{
}

// a confirmation goes: its controllers come back to the menu that raised it
inline void	CloseConfirmation(CSimpleGameMenu *&confirm)
{
	CSimpleGameMenu	*menu = confirm;
	if (menu)
	{
		menu->mActive = false;
		menu->RelinquishControl();
		SAFE_DELETE(confirm);
	}
}

// FUNCTION: 0x004d0e50
void	CPauseMenu::HandleEvent(CEvent *event)
{
	switch (event->GetEventNum())
	{
	case PAUSE_MENU_NO:
		CFrontEnd::PlaySound(FES_BACK);
		CloseConfirmation(mConfirm);
		CloseConfirmation(mQuitConfirm);
		break;

	case PAUSE_MENU_YES:
		if (mConfirm)
		{
			if (mChosen == GI_QUIT)
				GAME.SetQuit(QT_USER_QUIT_TO_FRONTEND);
			if (mChosen == GI_RETRY)
				GAME.SetQuit(QT_RESTART_LEVEL);
		}
		if (mQuitConfirm)
		{
			if (PLAYABLE_DEMO)
				GAME.SetQuit(QT_USER_QUIT_TO_FRONTEND);
			else
				GAME.SetQuit(QT_USER_QUIT_TO_TITLE_SCREEN);
		}
		break;
	}
}

// FUNCTION: 0x004d0ff0
void	CPauseMenu::Activate(BOOL a)
{
	mResult = 0;
	mActive = TRUE;
	mChosen = 0;
	mActivateTime = PLATFORM.GetSysTimeFloat();
	mOpenTime = PLATFORM.GetSysTimeFloat();
	mPage = 0;
	mLists.At(0)->Reset();
	mNoFade = (a == 0);

	// the briefing only for a level with objectives, or a single-player one
	if (((!GAME.GetNumPrimaryObjectives()) && (!GAME.GetNumSecondaryObjectives()) && GAME.IsMultiplayer()) ||
		(GAME.GetCurrentlyRunningLevelNum() >= 900))
		mLists.At(0)->SetItemEnabled(GI_BRIEFING, FALSE);
	else
		mLists.At(0)->SetItemEnabled(GI_BRIEFING, TRUE);
}

// FUNCTION: 0x004d10b0
void	CPauseMenu::DeActivate(BOOL a)
{
	mActive = FALSE;
	mCloseTime = PLATFORM.GetSysTimeFloat();
	SAFE_DELETE(mConfirm);
	SAFE_DELETE(mQuitConfirm);
	mNoFade = (a == 0);
}

// FUNCTION: 0x004d1110
void	CPauseMenu::MenuItemChanged(CMenuItem *item)
{
	CScaleMenuItem	*scale = (CScaleMenuItem *)item;

	switch (item->mText)
	{
	case GI_GOD_OFF:
		GAME.GetPlayer(0)->SetIsGod(FALSE);
		break;
	case GI_GOD_ON:
		GAME.GetPlayer(0)->SetIsGod(TRUE);
		break;
	case MUSIC_VOLUME:
		{
			float	volume = float(scale->mValue);
			volume /= scale->mSteps;
			LOG.AddMessage("Setting music volume to %.4f", volume);
			MUSIC.SetVolume(volume);
		}
		break;
	case SOUND_VOLUME:
		{
			float	volume = float(scale->mValue);
			volume /= scale->mSteps;
			LOG.AddMessage("Setting sound volume to %.4f", volume);
			SOUND.SetMasterVolume(volume);
		}
		break;
	}
}

// FUNCTION: 0x004d11d0
WCHAR	*CPauseMenu::Render()
{
	WCHAR	*title = NULL;

	SyncDetailItems();

	if (!mActive)
	{
		mFirstRenderTime = 0;
		if (mCloseTime < 0.f)
			return NULL;
		if (PLATFORM.GetSysTimeFloat() - mCloseTime > 0.4f)
			return NULL;
	}

	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetUnknown2ed(false);
	RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
	RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ALPHAREF, 8);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	RENDERINFO.Apply();

	float	fade;
	if (mActive)
		fade = PLATFORM.GetSysTimeFloat() - mActivateTime;
	else
		fade = (mCloseTime + 0.4f) - PLATFORM.GetSysTimeFloat();

	int		alpha = int(fade * 480.f);
	if (alpha > 0xc0)
		alpha = 0xc0;
	if (mNoFade)
		alpha = 0xc0;
	DWORD	col = (alpha << 24) + 0x101010;

	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	if ((!mOptionsPage) && (mActive || (!mNoFade)))
		CSPRITERENDERER::DrawSprite(-10.f, -10.f, 0.01f, mBlankTexture, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, col, 50.f, 40.f, 0.f, 1.f, 0.f, 1.f);

	if (mFirstRenderTime == 0.f)
		mFirstRenderTime = PLATFORM.GetSysTimeFloat();

	// retail reuses fade's slot for the time since opening (004d143f), so while the menu is open the lists below
	// wait for that time, not the fade
	if (mActive)
		fade = PLATFORM.GetSysTimeFloat() - mOpenTime;

	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	float	scale = 1.2f;
	float	spin = 0.f;
	if (fade < 0.2f)
		scale = (fade / 0.2f) * 1.1f + 0.1f;
	else
	{
		float	t = fade;
		if (t > 0.4f)
			t = 0.4f;
		spin = t - 0.2f;
	}

	if ((!mOptionsPage) && (mPage == 0))
	{
		CSPRITERENDERER::DrawSprite(320.f, 240.f, 0.004f, mCircle1, SA_CENTRE, SPT_NONE, 1.f, -spin, 0xffffffff, scale, scale, 0.f, 1.f, 0.f, 1.f);
		CSPRITERENDERER::DrawSprite(320.f, 240.f, 0.004f, mCircle2, SA_CENTRE, SPT_NONE, 1.f, spin, 0xffffffff, scale, scale, 0.f, 1.f, 0.f, 1.f);
	}

	if (fade > 0.4f)
	{
		title = mLists.At(mPage)->Render(this);
		if (mConfirm)
			mConfirm->mList.Render(this);
		if (mQuitConfirm)
			mQuitConfirm->mList.Render(this);
	}

	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	if ((!mOptionsPage) && (alpha == 0xc0))
		RenderMouseCursor();

	return mPage ? title : NULL;
}

// FUNCTION: 0x004d15d0
void	CPauseMenu::ReceiveButtonAction(CController *from_controller, int button, float val)
{
	if (PLATFORM.GetSysTimeFloat() - mOpenTime < 0.4f)
		return;
	if (mLists.At(mPage)->PassToSelected(from_controller, button, val))
		return;

	switch (button)
	{
	default:
		mLists.At(mPage)->ReceiveButtonAction(from_controller, button, val);
		break;
	case BUTTON_PAUSE_MENU_SELECT:
		{
			CMenuOptionsList	*list = mLists.At(mPage);
			CMenuItem			*item = list->mItems.At(list->mSelected);
			ButtonPressed(item, from_controller);
		}
		break;
	case BUTTON_FRONTEND_MENU_BACK:
		if (mPage > 0)
			BackToFirstPage(this);
		else
		{
			Resume(from_controller);
			CFrontEnd::PlaySound(FES_BACK);
		}
		break;
	}
}

// Reconstructed search boundary: with SendEvent, this reproduces the full retail 004d1810 body.
static inline BOOL FindMenuEvent(SPtrSet<CMenuEvent> &events, CMenuItem *item, CMenuEvent *&event)
{
	FOR_ALL_ITEMS_IN(events, event)
	{
		if (event->mItem == item)
			return TRUE;
	}
	return FALSE;
}

inline void CSimpleGameMenu::SendEvent(CMenuItem *item)
{
	CMenuEvent *event;
	if (FindMenuEvent(mEvents, item, event) && event->GetToCall())
		event->GetToCall()->HandleEvent(event);
}

// a hard-coded message the setup code leaves for the player (its readers are open: nothing in the retail
// executable reads these two back)
// sSetupMessage is defined above
static int	sSetupMessageAlpha = 0;					// 0082b4ec

// FUNCTION: 0x004d1710
void	SetSetupMessage(int text)
{
	sSetupMessageAlpha = 0xff;
	sSetupMessage = text;
}

// FUNCTION: 0x004d1730 ??_GCSimpleGameMenu@@UAEPAXI@Z

// FUNCTION: 0x004d1750
CSimpleGameMenu::~CSimpleGameMenu()
{
	mEvents.DeleteAll();
}

// FUNCTION: 0x004d1810
void	CSimpleGameMenu::ReceiveButtonAction(CController *from_controller, int button, float val)
{
	switch (button)
	{
	default:
		mList.ReceiveButtonAction(from_controller, button, val);
		break;
	case BUTTON_PAUSE_MENU_SELECT:
		SendEvent(mList.mItems.At(mList.mSelected));
		break;
	case BUTTON_FRONTEND_MENU_BACK:
		SendEvent(mList.mItems.At(0));
		break;
	}
}

void	CSimpleGameMenu::AddItem(CMenuItem *item, CMonitor *to_call, int event)
{
#line 2290
	mEvents.Add(new (MEMTYPE_MENUITEM) CMenuEvent(item, to_call, event));
	mList.AddItem(item);
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00418410 ?GetHeight@CJoystickEnableItem@@UAEHXZ
// FUNCTION: 0x00466120 ??0CMonitor@@QAE@XZ
// FUNCTION: 0x00505d00 ??0?$SPtrSet@VCMenuEvent@@@@QAE@XZ
