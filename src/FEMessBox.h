// FEMessBox.h: the front end's message box (reconstructed; FEMESSBOX is at 00675688). Create (0044d390) takes eleven
// arguments; it and the inline Kill/BeingDisplayed use the flag at +0x1f8c (00677614).

#ifndef FEMESSBOX_H
#define FEMESSBOX_H

#include "FrontEndPage.h"

class CDXBitmapFont;

// The box's buttons (Create's tenth argument): 0 has none and is dismissed by its timeout (the seventh argument).
enum EFEMBOption
{
	FEMB_OPTION_NONE = 0,
	FEMB_OPTION_OK,
	FEMB_OPTION_YESNO,
	FEMB_OPTION_A_CONTINUE
};

// What the box asks (Create's last argument, read back through GetQuestion at +0x1f9c).
enum
{
	FEMB_Q_OVERWRITE = 3,
	FEMB_Q_NEW_CAREER = 1,			// CFEPMain::DoAction, with a career in progress (00462407)
	FEMB_Q_LOAD_CAREER = 2,			// (00462491)
	FEMB_Q_DELETE_SAVES = 4,
	FEMB_Q_DELETE_SAVE = 6,
	FEMB_Q_NO_AUTOSAVE = 7,			// PC: CFEPDevSelect's question before playing without saving (00458c63)			// PC: CFEPDirectory asks before deleting one save (0051ab88)
	FEMB_Q_LANGUAGE = 8,			// CFEPMain::Process, after the language changes (00462ade)
	FEMB_Q_FORMAT = 9,				// PC: CFEPSaveGame::CreateSave asks it for an unformatted card (00464d29)
	FEMB_Q_QUIT = 11				// CFEPMain::DoAction (004625b7)
};

// The player's answer (GetFinalChoice, +0x1fa4; reading it resets it to -1).
enum
{
	FEMB_NO = 0,
	FEMB_YES = 1
};

class CFEMessBox
{
public:
	void	Init();
	void	Create(float x, float y, float width, float z, WCHAR *text, CDXBitmapFont *font, UINT timeout, UINT fadetime, BOOL a, EFEMBOption option, UINT question);
	void	Process();
	void	Render();
	void	ButtonPressed(SINT button, float val);
	BOOL	IsInteractive();							// displayed with buttons

	void	Kill() { mBeingDisplayed = FALSE; }
	BOOL	BeingDisplayed() { return mBeingDisplayed; }
	UINT	GetQuestion() { return mQuestion; }
	int		GetFinalChoice() { int choice = mFinalChoice; mFinalChoice = -1; return choice; }
	void	SetOnDismiss(EFrontEndPage page, SINT time) { mOnDismissPage = page; mOnDismissTime = time; }
	void	StartFadeOut() { mFadeCount = 0; mFadeState = 2; mOnDismissPage = FEP_NONE; }

	BOOL	mUnknown0000;						// +0x0000 (Init sets it)
	float	mX, mY;								// +0x0004
	float	mWidth;								// +0x000c
	float	mHeight;							// +0x0010: from the wrapped lines and the buttons
	float	mZ;									// +0x0014
	SINT	mAlpha;								// +0x0018
	SWordWrapFill	mLines;						// +0x001c: the wrapped text, 40 lines of 100 characters (Render steps 0xc8, 0044d8fd)
	CDXBitmapFont	*mFont;								// +0x1f5c
	int		mLineHeight;						// +0x1f60
	int		mNumLines;							// +0x1f64
	DWORD	mBackColour;						// +0x1f68
	DWORD	mTextColour;						// +0x1f6c
	DWORD	mHighlightColour;					// +0x1f70
	DWORD	mGreyColour;						// +0x1f74
	float	mFadeTime;							// +0x1f78
	float	mFadeCount;							// +0x1f7c
	int		mFadeState;							// +0x1f80: 0 fading in, 1 shown, 2 fading out
	float	mTimeout;							// +0x1f84
	float	mTime;								// +0x1f88
	BOOL	mBeingDisplayed;					// +0x1f8c
	BOOL	mUnknown1f90;						// +0x1f90 (Create's ninth argument)
	float	mUnknown1f94;						// +0x1f94
	EFEMBOption	mOption;						// +0x1f98
	UINT	mQuestion;							// +0x1f9c
	int		mCursor;							// +0x1fa0
	int		mFinalChoice;						// +0x1fa4
	EFrontEndPage	mOnDismissPage;				// +0x1fa8
	SINT	mOnDismissTime;						// +0x1fac
};

extern CFEMessBox FEMESSBOX;

#endif
