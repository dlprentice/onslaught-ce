#include	"Common.h"

#include	"FEPLoadGame.h"
#include	"Frontend.h"
#include	"Renderinfo.h"
#include	"SpriteRenderer.h"
#include	"MemoryCard.h"
#include	"career.h"
extern char		gDisplayResetState;			// 0082b5b0
//*********************************************************************************

BOOL	CFEPLoadGame::Init()	// FUNCTION: 0x00461c40
{
	mCX = 0;
	mCY = 0;

	mSaveGameNumber=-1;
	mSaveGameName[0] = 0;

	return TRUE;
}

//*********************************************************************************
void	CFEPLoadGame::Shutdown()
{
}

//*********************************************************************************
void	CFEPLoadGame::ButtonPressed(SINT button,float val)	// FUNCTION: 0x00461c60
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_UP:
		mCY --;
		if (mCY < 0)
			mCY = 0;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		mCY ++;
		if (mCY > 0xFF)
			mCY = 0xFF;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_LEFT:
		mCX --;
		if (mCX < 0)
			mCX = 0;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		mCX ++;
		if (mCX > 0xFF)
			mCX = 0xFF;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		FRONTEND.PlaySound(FES_SELECT);
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_MAIN, 0);
		break;
	};
}

//*********************************************************************************
void	CFEPLoadGame::Process(EFEPState state)	// FUNCTION: 0x00461d60
{
	// Update hard drive and Memory Unit status
	
	if (state != FEPS_INACTIVE)
		MEMORYCARD.Update();
	
	if ((state==FEPS_ACTIVE) && (!FEMESSBOX.BeingDisplayed()))
	{
		// Attempt to kick off the save game process
		StartLoad();
	}
}

//*********************************************************************************
void	CFEPLoadGame::RenderPreCommon(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}
//*********************************************************************************
void	CFEPLoadGame::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00461d90
{
	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	//*************************************************************************
	// Load game rendering, selection of device, etc.

	//***********************************************
	//** Help Text. None here.

	//***********************************************
	//** Title Bar

	FRONTEND.DrawTitleBar(FET.GetText(FEX_LOADGAME2), transition, dest);

	//*************************************************************************
	// BEA logo
	float tr = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(tr);

	FRONTEND.GetCommonPage()->RenderHelpText(FEH_CONTINUE, transition);
}

//*********************************************************************************

void	CFEPLoadGame::TransitionNotification(EFrontEndPage from)
{
	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;
}

//*********************************************************************************

// Initialise the data and variables required to load the game, and kick off the
// load process

void	CFEPLoadGame::StartLoad()	// FUNCTION: 0x00461e20
{
	if (mSaveGameNumber==-1)
	{
		ASSERT(mSaveGameName[0] == 0);
		TRACE("Error - no slot number to load from!\n");
		return;
	}
	else
	{
		ASSERT(mSaveGameName[0] != 0);
	}

#if TARGET == XBOX
	// let's get the name of the card from which we're trying to load
	MEMORYCARD.GetCardOrPreviousCardName2(FRONTEND.GetMemoryCardNumber(), FRONTEND.mFEPSaveGame.mSavedToCardName);
#endif
	
	// copy across to the save game name because we'll be saving soon.
	wcscpy(FRONTEND.mFEPSaveGame.mSaveGameName, mSaveGameName);

	// and into the virtual keyboard just in case it needs it.
	wcscpy(FRONTEND.mFEPVirtualKeyboard.m_strData, mSaveGameName);

	// and put the cursor at the end.
	FRONTEND.mFEPVirtualKeyboard.m_iPos = wcslen(mSaveGameName);

	// Make sure the current device is present

	BOOL present,formatted;

	MEMORYCARD.Update();

	MEMORYCARD.GetCardInfo(FRONTEND.GetMemoryCardNumber(),&present,&formatted,NULL,NULL);

	if ((!present) || (!formatted))
	{
		// Erk! Whinge to the player. Share code with savegame.
		CFEPSaveGame::RemovedMUWhinge(FEX_REMOVEDLOAD);
		return;
	}

	// let's ask whether the memory card has changed. We don't actually care, but this call resets the bool as well.
	MEMORYCARD.CardBeenUnpluggedSinceLastTimeIAsked(FRONTEND.GetMemoryCardNumber());

	// Load savegame

	int buffersize,bytesread;

	buffersize=CAREER.SizeOfSaveGame();

	BYTE *buffer=new (MEMTYPE_UNKNOWN) BYTE[buffersize];

	// ***TEXT
	EFETextHack text = FRONTEND.GetMemoryCardNumber() == -1? FEX_LOADING_HD: FEX_LOADING;

	FRONTEND.StartMCOperation(FRONTEND.mFrontEndText.GetText(text,FRONTEND.GetMemoryCardNumber()
#if TARGET == PS2
		+1
#endif
		));

	int res=MEMORYCARD.ReadSave(FRONTEND.GetMemoryCardNumber(),mSaveGameNumber,mSaveGameName,buffer,buffersize,&bytesread);

	FRONTEND.EndMCOperation();

	if (res==MCE_SUCCESS)
	{
		if (!CAREER.Load((char *)buffer, true))
		{
			// Version number mismatch
			FEMESSBOX.Kill();
			FEMESSBOX.Create(320.f, 240.f, 400, FO(100), FET.GetText(FEX_BADSAVEVERSION), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
			FRONTEND.SetPage(FEP_MAIN, 0);
		}
		else
		{
			// Loaded OK

			FEMESSBOX.Kill();
			FEMESSBOX.Create(320.f, 240.f, 400, FO(100), FET.GetText(FEX_LOADDONE), PLATFORM.Font(FONT_SMALL), 60, 10, FALSE, FEMB_OPTION_NONE, FALSE);			
			FRONTEND.SetPage(FRONTEND.GetSuccessPage(), FRONTEND.GetSuccessTransTime());

			// remember that from now on we can autosave to this location.
			FRONTEND.SetAutoSave(CFrontEnd::AUTO_SAVE_NORMAL);
			if (!gDisplayResetState) CFEPOptions::WriteDefaultOptionsFile((char *)buffer, buffersize);
		}
	}
	else
	{
		// All errors go through the same system.

		// Has he whipped out the MU card while loading?
		BOOL present,formatted;
		MEMORYCARD.Update();
		MEMORYCARD.GetCardInfo(FRONTEND.GetMemoryCardNumber(),&present,&formatted,NULL,NULL);

		if (!present)
		{
			// gosh he's been very silly. Let's complain.
			CFEPSaveGame::RemovedMUWhinge(FEX_REMOVEDLOAD);
		}
		else
		{
			// otherwise, general whinge.
			FEMESSBOX.Kill();

			WCHAR whinge[256];

			wcscpy( whinge, FRONTEND.mFrontEndText.GetText(FEX_LOADINVALID,FRONTEND.GetMemoryCardNumber()
#if TARGET == PS2
				+1
#endif
				));

			FEMESSBOX.Create(320.f, 240.f, 400, FO(100), whinge, PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
		}

		FRONTEND.SetPage(FEP_MAIN, 0);
	}

	SAFE_DELETE_ARRAY(buffer);
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x00461b50 init:ZERO_FVECTOR
// FUNCTION: 0x00461b70 init:ID_FMATRIX
