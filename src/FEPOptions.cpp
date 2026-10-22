// FEPOptions.cpp: the PC options page (reconstructed from the retail PC executable; not in Stuart's release).

#include	"Common.h"

#include	"FEPOptions.h"
#include	"Frontend.h"
#include	"PauseMenu.h"
#include	"career.h"
#include	"text.h"
#include	"debuglog.h"

extern char			gSaveFromOptions;			// 006799d0: makes the save page's success box show hard-coded text 0xd6

static CPauseMenu	*gOptionsMenu;				// 0089bc30; static: retail keeps Shutdown and Cleanup apart, and LINK never folds code that refers to a static

//*********************************************************************************
void	CScreenPosition::Read()	// FUNCTION: 0x0051f470
{
	mX = CAREER.GetScreenPosX();
	mY = CAREER.GetScreenPosY();
}

//*********************************************************************************
void	CScreenPosition::Write()	// FUNCTION: 0x0051f490
{
	CAREER.SetScreenPosX(mX);
	CAREER.SetScreenPosY(mY);
}

//*********************************************************************************
BOOL	CFEPOptions::Init()	// FUNCTION: 0x0051f4b0
{
	mState = 0;

	return TRUE;
}

//*********************************************************************************
void	CFEPOptions::Shutdown()	// FUNCTION: 0x0051f4c0
{
	SAFE_DELETE(gOptionsMenu);
}

//*********************************************************************************
void	CFEPOptions::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0051f4e0
{
	gOptionsMenu->ReceiveButtonAction(NULL, button, val);
}

//*********************************************************************************
// Leave the options: save the career to its slot when there is one (the save page shows its own success text), and
// write the options to defaultoptions.bea.
void	CFEPOptions::SaveDefaultOptions(bool from_options)	// FUNCTION: 0x0051f500
{
	if ((FRONTEND.mFEPSaveGame.mSaveGameName[0]) && (FRONTEND.GetAutoSave() == CFrontEnd::AUTO_SAVE_NORMAL))
	{
		FRONTEND.SetSaveMode(TRUE);
		FRONTEND.SetPage(FEP_SAVEGAME, 0);
		FRONTEND.SetSuccessFEP(PLAYABLE_DEMO ? FEP_DEMOMAIN : FEP_MAIN, 70);
		gSaveFromOptions = from_options;
	}
	else
	{
		FRONTEND.SetPage(PLAYABLE_DEMO ? FEP_DEMOMAIN : FEP_MAIN, 70);
	}

	int		size = CAREER.SizeOfSaveGame();
#line 250
	char	*buffer = new (MEMTYPE_SCRATCHPAD) char[size];
	CAREER.Save(buffer);
	WriteDefaultOptionsFile(buffer, size);
	delete [] buffer;
}

//*********************************************************************************
BOOL	CFEPOptions::IsStateAvailable(int state)
{
	switch (state)
	{
	case 0:
	case 2:
	case 3:
	case 5:
		return FALSE;

	case 4:
		return FRONTEND.NumControllersPresent() >= 2;
	}

	return TRUE;
}

//*********************************************************************************
void	CFEPOptions::Process(EFEPState state)	// FUNCTION: 0x0051f600
{
	while (!IsStateAvailable(mState))
		mState ++;

	if ((state == FEPS_ACTIVE) && (!gOptionsMenu->mActive))
	{
		if (gOptionsMenu->mResult == 1)
			FRONTEND.SetPage(FEP_CREDITS, 30);
		else
			SaveDefaultOptions(true);
	}
}

//*********************************************************************************
void	CFEPOptions::WriteDefaultOptionsFile(char *data, int size)	// FUNCTION: 0x0051f680
{
	FILE	*f = fopen("defaultoptions.bea", "wb");

	if (f)
	{
		fwrite(data, size, 1, f);
		fclose(f);
	}
	else
	{
		SETUP_HISTORY.AddMessage("Couldn't write defaultoptions");
	}
}

//*********************************************************************************
void	CFEPOptions::RenderPreCommon(float transition, EFrontEndPage dest)	// FUNCTION: 0x0051f6d0
{
	if ((dest == FEP_CREDITS) || (dest == FEP_SCREEN_POS))
		transition = 1.f;

	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
void	CFEPOptions::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x0051f700
{
	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	WCHAR	*title = gOptionsMenu->Render();

	FRONTEND.DrawTitleBar(title ? title : TEXT.GetStringById(0x265233), transition, dest);

	if (title)
		TRACE("%s\n", FromWCHAR(title));

	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);

	if ((dest == FEP_CREDITS) || (dest == FEP_SCREEN_POS))
		transition = 1.f;
	else
		transition = RangeTransition(transition, 0.75f, 1.f);

	FRONTEND.GetCommonPage()->RenderSmallBEALogo(transition);
}

//*********************************************************************************
void	CFEPOptions::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x0051f7e0
{
	if ((from == FEP_DEMOMAIN) || (from == FEP_MAIN))
		mState = 0;

	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;

	if ((from == FEP_DEMOMAIN) || (from == FEP_MAIN))
	{
		mSoundVolume = CAREER.GetSoundVolume();
		mMusicVolume = CAREER.GetMusicVolume();
		mPlayer[0].mVibration = CAREER.GetVibration(0);
		mPlayer[0].mControllerConfigurationNum = CAREER.GetControllerConfigurationNum(0);
		mPlayer[1].mVibration = CAREER.GetVibration(1);
		mPlayer[1].mControllerConfigurationNum = CAREER.GetControllerConfigurationNum(1);
		mScreenPosition.Read();
	}

	if (!gOptionsMenu)
	{
#line 406
		gOptionsMenu = new (MEMTYPE_MESSAGELOG) CPauseMenu(TRUE);
	}

	gOptionsMenu->Activate(0);
}

//*********************************************************************************
void	CFEPOptions::Cleanup()	// FUNCTION: 0x0051f8e0
{
	SAFE_DELETE(gOptionsMenu);
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x0051f380 init:ZERO_FVECTOR
// FUNCTION: 0x0051f3a0 init:ID_FMATRIX
