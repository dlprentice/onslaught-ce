#include	"Common.h"
#include	"FEPSaveGame.h"

#include	"Frontend.h"
#include	"FEMessBox.h"
#include	"Renderinfo.h"
#include	"SpriteRenderer.h"
#include	"MemoryCard.h"
#include	"career.h"
#include	"cliparams.h"
char			gSaveFromOptions = 0;			// 006799d0: set by CFEPOptions::SaveDefaultOptions; CreateSave shows text 0xd6
//*********************************************************************************

BOOL	CFEPSaveGame::Init()	// FUNCTION: 0x00464620
{
	mCX = 0;
	mCY = 0;

	return TRUE;
}

//*********************************************************************************
void	CFEPSaveGame::Shutdown()
{
}

//*********************************************************************************
void	CFEPSaveGame::ButtonPressed(SINT button,float val)	// FUNCTION: 0x00464630
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

void	CFEPSaveGame::Process(EFEPState state)	// FUNCTION: 0x00464730
{
	if (state != FEPS_INACTIVE)
	{
	MEMORYCARD.Update();
		
	if ((state==FEPS_ACTIVE) && (!FEMESSBOX.BeingDisplayed()))
	{		
		CreateSave();
	}

	// Are there any pending results from messagebox questions that we care about?
	if ( FEMESSBOX.BeingDisplayed() )
	{
		switch(FEMESSBOX.GetQuestion())
		{
		default:
			break;

		case FEMB_Q_OVERWRITE:
			// we're asking if they want to overwrite an existing savegame.
			switch (FEMESSBOX.GetFinalChoice())
			{
			case FEMB_YES:
				// Well, so much for that savegame! Delete it and try saving again.

				if (mExistingSave!=-1)
				{
					// ***TEXT
					FRONTEND.StartMCOperation(FRONTEND.mFrontEndText.GetText(FEX_SAVING,FRONTEND.GetMemoryCardNumber()));
					ASSERT(mExistingSaveName[0]);
					if (MEMORYCARD.DeleteSave(FRONTEND.GetMemoryCardNumber(),mExistingSave,mExistingSaveName) != MCE_SUCCESS)
					{
						FRONTEND.EndMCOperation();

						// Couldn't delete it: go back to choosing a name.
						mExistingSave=-1;
						mExistingSaveName[0] = 0;
						FRONTEND.SetSaveMode(TRUE);
						FRONTEND.SetPage(FEP_VIRTUAL_KEYBOARD, 20);
						FEMESSBOX.Kill();
						FEMESSBOX.Create(320.f, 240.f, 400, FO(100), Localization_GetStringById(0x76), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
						FEMESSBOX.SetOnDismiss(FEP_VIRTUAL_KEYBOARD, 0);
						break;
					}
					FRONTEND.EndMCOperation();

					mExistingSave=-1;					
				}
				else
				{
					ASSERT(!mExistingSaveName[0]);
				}

				// and kick the process off again.
				CreateSave();
				break;

			case FEMB_NO:
				// Go back to choosing a save game name.
				mExistingSave=-1;
				mExistingSaveName[0] = 0;
				FRONTEND.SetSaveMode(TRUE);
				FRONTEND.SetPage(FEP_VIRTUAL_KEYBOARD, 20);
				break;
			}
			break;

		case FEMB_Q_FORMAT:
			// we're asking if they want to format the card.
			switch (FEMESSBOX.GetFinalChoice())
			{
			case FEMB_YES:
				{
					TRACE("Format\n");
					BOOL present,formatted;
					MEMORYCARD.GetCardInfo(FRONTEND.GetMemoryCardNumber(),&present,&formatted,NULL,NULL);

					if (!present)
					{
						RemovedMUWhinge(FEX_REMOVEDSAVE);
					}
					else if (formatted)
					{
						RemovedMUWhinge(FEX_REMOVEDSAVE);
					}
					else
					{
						FRONTEND.StartMCOperation(FET.GetText(FEX_FORMATTING, FRONTEND.GetMemoryCardNumber()));
						int res = MEMORYCARD.Format(FRONTEND.GetMemoryCardNumber());
						FRONTEND.EndMCOperation();

						if (res != MCE_SUCCESS)
						{
							MEMORYCARD.Update();
							FEMESSBOX.Kill();
							FEMESSBOX.Create(320.0f, 240.0f, 400.0f, FO(100), FET.GetText(FEX_FORMAT_FAILED, FRONTEND.GetMemoryCardNumber()+1), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
							FEMESSBOX.SetOnDismiss(FEP_MAIN, 0);
						}
						else
						{
							MEMORYCARD.Update();
							FEMESSBOX.Kill();
						}
					}
				}
				break;

			case FEMB_NO:
				FRONTEND.SetPage(FEP_MAIN, 20);
				break;
			}
			break;
		}
	}

	RENDERINFO.SetFogEnabled(false);
	}
}

//*********************************************************************************
void	CFEPSaveGame::RenderPreCommon(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}
//*********************************************************************************
void	CFEPSaveGame::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00464a80
{
	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	//*************************************************************************
	// Load game rendering, selection of device, etc.

	//***********************************************
	//** Help Text. But this page has nothing on it but the "save succeeded" dialog box. So no help text.

	//***********************************************
	//** Title Bar
	FRONTEND.DrawTitleBar(FET.GetText(FEX_SAVEGAME2), transition, dest);

	//*************************************************************************
	// BEA logo
	float tr = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(tr);

	FRONTEND.GetCommonPage()->RenderHelpText(FEH_CONTINUE, transition);
}

//*********************************************************************************

void	CFEPSaveGame::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x00464b10
{
	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;
}

//*********************************************************************************

void CFEPSaveGame::RemovedMUWhinge(EFETextHack reason)	// FUNCTION: 0x00464b30
{
	WCHAR whinge[200];
	WCHAR name[200];
	wcscpy( whinge, FET.GetText(reason, FRONTEND.GetMemoryCardNumber()
#if TARGET == PS2
		+1
#endif
		));

	// create the message. Luckily it takes a local copy of the text
	FEMESSBOX.Kill();
	FEMESSBOX.Create(320.0f, 240.0f, 400.0f, FO(100), whinge, PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
	FEMESSBOX.SetOnDismiss(FEP_MAIN, 0);
}

//*********************************************************************************
//*********************************************************************************

// Initialise the data and variables required to save the game, and kick off the
// save process

static void ask_if_you_want_to_overwrite()
{
	FEMESSBOX.Create(320.0f, 240.0f, 300.0f, FO(100), FET.GetText(FEX_OVERWRITE_SAVEGAME), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_YESNO, FEMB_Q_OVERWRITE);
}

//*********************************************************************************

void CFEPSaveGame::AskIfYouWantToDelete(BOOL career_in_progress, BOOL because_4096, BOOL no_space_for_bea)	// FUNCTION: 0x00464bc0
{
	UINT question;
	WCHAR total_message[256];
	total_message[0] = 0;

	// We're only allowed to go to the dashboard if we've got no savegame info.
	// Though in truth, if we've got savegame info the memory card really shouldn't be full, because we're autosaving.
	// This could happen in you switch mem cards during a game i guess

#if TARGET == XBOX
	// even though it's fucking obvious because of the big red box round the currently highlighted device, I'll even say
	// what card it is.
	MEMORYCARD.GetCardName2(FRONTEND.GetMemoryCardNumber(), total_message);
	if (!total_message[0])
	{
		// it doesn't have a cool name. Give it a crap one.
		MEMORYCARD.GetCardName(FRONTEND.GetMemoryCardNumber(), total_message);
	}

	wcscat(total_message, ToWCHAR("                                           ")); // word wrap doesn't understand \n but does swallow spaces

	// Oh dear it all gets very complex.
	if (no_space_for_bea)
	{
		// The card's full, but there's no bea savegames to delete. So the only option is to go to the dashboard.
		// BUT, of course, that's not the end of the story, if we've got a career going to the dashboard would be bad.

		wcscat(total_message, FET.GetText(FEX_NO_SPACE_DASH));
		question = FEMB_Q_DASHBOARD;

		if (career_in_progress)
		{
			wcscat(total_message, ToWCHAR(" "));
			wcscat(total_message, FET.GetText(FEX_THAT_WILL_KILL_CAREER));
		}
		else
		{
			// no career in progress, so going to the dashboard is no problem.
		}
	}
	else
	{
		// we've got some of our own savegames to delete. Let's give them the option to delete these savegames.
		// But how many?
		if (because_4096)
		{
			wcscat(total_message, FET.GetText(FEX_4096_DELETE));
		}
		else
		{
			wcscat(total_message, FET.GetText(FEX_NO_SPACE_THERE));
		}

		question = FEMB_Q_DELETE_SAVES;
	}
#else
	if (because_4096) wcscat(total_message, FET.GetText(FEX_TOOMANYSAVES_PS2,FRONTEND.GetMemoryCardNumber()+1));
	else			  wcscat(total_message, FET.GetText(FEX_NOSPACE_WITHSAVES_PS2,FRONTEND.GetMemoryCardNumber()+1));

	question = FEMB_Q_DELETE_SAVES;
#endif

	FEMESSBOX.Create(320.0f, 240.0f, 300.0f, FO(100), total_message, PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_YESNO, question);
}

//*********************************************************************************

void	CFEPSaveGame::CreateSave()	// FUNCTION: 0x00464c50
{
	ASSERT(mSaveGameName[0]);

	BOOL present,formatted;
	int bytesfree,bytestotal;
	int numsaves;

#if TARGET == XBOX
	// let's get the name of the card to which we're trying to save
	MEMORYCARD.GetCardOrPreviousCardName2(FRONTEND.GetMemoryCardNumber(), mSavedToCardName);
#endif
	
	MEMORYCARD.Update();

	MEMORYCARD.GetCardInfo(FRONTEND.GetMemoryCardNumber(),&present,&formatted,&bytesfree,&bytestotal);
	MEMORYCARD.GetNumSaves(FRONTEND.GetMemoryCardNumber(),&numsaves);

	if (!present)
	{
		// Card removed or invalid, so we'll return to device selection. But whinge first.
		RemovedMUWhinge(FEX_REMOVEDSAVE);
		return;
	}

	if (!formatted)
	{
		// Offer to format it.
		FEMESSBOX.Create(320.0f, 240.0f, 300.0f, FO(100), FET.GetText(FEX_UNFORMATTED, FRONTEND.GetMemoryCardNumber()+1), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_YESNO, FEMB_Q_FORMAT);
		return;
	}

	// Are we allowed to overwrite an existing savegame? This will happily reset the counter if we're not autosaving as well.
	bool memory_card_changed = MEMORYCARD.CardBeenUnpluggedSinceLastTimeIAsked(FRONTEND.GetMemoryCardNumber());

	if (memory_card_changed && FRONTEND.GetAutoSave() == FRONTEND.AUTO_SAVE_NORMAL)
	{
		// Oh dear, the memory card has changed. Refuse all this saving business, and go to selecting location.
		FEMESSBOX.Create(320.0f, 240.0f, 300.0f, FO(100), FET.GetText(FEX_AUTOSAVE_SCARED), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_OK, FALSE);
		FRONTEND.SetAutoSave(FRONTEND.AUTO_SAVE_NOT);
		FEMESSBOX.SetOnDismiss(FEP_MAIN, 50);
		return;
	}

	bool allowed_overwrite;
	
	ASSERT(FRONTEND.GetAutoSave() != FRONTEND.AUTO_SAVE_PRETEND && "Jan says if we're pretending to autosave we shouldn't be saving.");

	if (FRONTEND.GetAutoSave() == FRONTEND.AUTO_SAVE_NORMAL)
	{
		// we're only allowed to overwrite if the memory card hasn't been unplugged
		allowed_overwrite = !memory_card_changed;
	}
	else
	{
		// it's not an autosave, so overwriting is naughty.
		ASSERT(FRONTEND.GetAutoSave() == FRONTEND.AUTO_SAVE_NOT);
		allowed_overwrite = FALSE;
	}

	// Check for available space (again)

	int savesize;

	MEMORYCARD.GetSaveSize(CAREER.SizeOfSaveGame(),&savesize);

	if (((bytesfree<savesize) || (numsaves>4095)) && (!allowed_overwrite))
	{
		// No space available - complain

		// If there's no saves but no space, it must be other people's saves that are taking up the space
		AskIfYouWantToDelete(CAREER.InProgress(), (numsaves>4095), numsaves == 0);
		return;
	}

	// Let's remember if this game actually had any game in it.
	// We have to remember it now because it's edited by the act of saving the game.
	mCareerWasInProgress = CAREER.InProgress();
	
	// Check to see if the save already exists

	int savenumber=-1;

	MEMORYCARD.GetNumSaves(FRONTEND.GetMemoryCardNumber(),&numsaves);

	for (int i=0;i<numsaves;i++)
	{
		WCHAR savename[256];

		MEMORYCARD.GetSaveName(FRONTEND.GetMemoryCardNumber(),i,savename);
			
		if (_wcsicmp(mSaveGameName,savename)==0)	// retail: 00464ee6 calls _wcsicmp (005d04ec)
		{
			savenumber=i;
		}
	}

	if ((savenumber!=-1) && (!allowed_overwrite))
	{
		// File already exists and we weren't allowed to overwrite.
		
		FEMESSBOX.Kill();
		
		mExistingSave=savenumber;
		wcscpy(mExistingSaveName, mSaveGameName);
		
		ask_if_you_want_to_overwrite();

		return;
	}

	// ***TEXT
	EFETextHack text = FRONTEND.GetMemoryCardNumber() == -1? FEX_SAVING_HD: FEX_SAVING;

	FRONTEND.StartMCOperation(FRONTEND.mFrontEndText.GetText(text,FRONTEND.GetMemoryCardNumber()
#if TARGET == PS2
		+1
#endif
		));
	
	int res=MEMORYCARD.CreateSave(FRONTEND.GetMemoryCardNumber(),mSaveGameName,&savenumber, allowed_overwrite);

	FRONTEND.EndMCOperation();

	if (res==MCE_FILEEXISTS)
	{
		// File already exists and we weren't allowed to overwrite.

		FEMESSBOX.Kill();

		mExistingSave=savenumber;
		wcscpy(mExistingSaveName, mSaveGameName);

		ask_if_you_want_to_overwrite();
	}
	else if (res==MCE_SUCCESS)
	{

		// Save file creation succeeded - write data

		int buffersize=CAREER.SizeOfSaveGame();
#line 535
		BYTE *buffer=new (MEMTYPE_SCRATCHPAD) BYTE[buffersize];
		
		CAREER.SaveAndMarkInProgress((char *)buffer);

		res=MEMORYCARD.WriteSave(FRONTEND.GetMemoryCardNumber(),savenumber,mSaveGameName,buffer,buffersize);

		FEMESSBOX.Kill();		

		if (res==MCE_SUCCESS)
		{
			// Save complete!

			// Did we save a career in progress or just the blank start?
			WCHAR *text;
			if (mCareerWasInProgress) text = FET.GetText(FEX_SAVEDONE);
			else					  text = FET.GetText(FEX_FRESHSAVEDONE);
			if (gSaveFromOptions) text = Localization_GetStringById(0xd6);
			
			FEMESSBOX.Kill();
			FEMESSBOX.Create(320.f, 240.f, 400, FO(100), text, PLATFORM.Font(FONT_SMALL), 60, 10, FALSE, FEMB_OPTION_NONE, FALSE);			
			FRONTEND.SetPage(FRONTEND.GetSuccessPage(), FRONTEND.GetSuccessTransTime());

			// and from now on we can use this slot.
			FRONTEND.SetAutoSave(CFrontEnd::AUTO_SAVE_NORMAL);

			// and this means that we've finally used the data in the virtual keyboard.
			FRONTEND.mFEPVirtualKeyboard.m_Fresh = false;
		}
		else if (res==MCE_NOFILE)
		{
			// No file (card removed)

			FEMESSBOX.Kill();
			RemovedMUWhinge(FEX_REMOVEDSAVE);
		}
		else if (res == MCE_CARDFULL)
		{
			// oh dear there goes the card space. We'd better lose the partial save though.
			MEMORYCARD.DeleteSave(FRONTEND.GetMemoryCardNumber(),savenumber,mSaveGameName);
			FEMESSBOX.Kill();
			int num_saves;
			MEMORYCARD.GetNumSaves(FRONTEND.GetMemoryCardNumber(), &num_saves);
			AskIfYouWantToDelete(mCareerWasInProgress, FALSE, num_saves == 0);
		}
		else
		{
			// Generic save error

			WCHAR msg[256];

			wcscpy( msg, FRONTEND.mFrontEndText.GetText(FEX_SAVEERROR,FRONTEND.GetMemoryCardNumber()
#if TARGET == PS2
				+1
#endif
				));
			
			FEMESSBOX.Kill();
			FEMESSBOX.Create(320.f, 240.f, 400, FO(100),msg, PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
			FRONTEND.SetPage(FEP_MAIN, 0);
		}

		SAFE_DELETE(buffer);
	}
	else if (res==MCE_CARDFULL || res==MCE_TOO_MANY_SAVES)
	{
		// We're out of space! Better get someone to clear the card.
		if (savenumber != -1) 
			MEMORYCARD.DeleteSave(FRONTEND.GetMemoryCardNumber(),savenumber,mSaveGameName);
		FEMESSBOX.Kill();
		int num_saves;
		MEMORYCARD.GetNumSaves(FRONTEND.GetMemoryCardNumber(), &num_saves);
		AskIfYouWantToDelete(mCareerWasInProgress, res == MCE_TOO_MANY_SAVES, num_saves == 0);
	}
	else
	{
		// Save failed for some reason

		FEMESSBOX.Kill();

		MEMORYCARD.Update();

		// Was the card removed?

		MEMORYCARD.GetCardInfo(FRONTEND.GetMemoryCardNumber(),&present,&formatted,NULL,NULL);

		if ((!present) || (!formatted))
		{
			RemovedMUWhinge(FEX_REMOVEDSAVE);
		}
		else
		{
			WCHAR msg[256];
			
			wcscpy( msg, FRONTEND.mFrontEndText.GetText(FEX_SAVEERROR,FRONTEND.GetMemoryCardNumber()
#if TARGET == PS2
				+1
#endif
				));

			FEMESSBOX.Kill();
			FEMESSBOX.Create(320.f, 240.f, 400, FO(100), msg, PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
			FRONTEND.SetPage(FEP_MAIN, 0);
		}
	}

	gSaveFromOptions = 0;
}

// The six cheat codes, each XORed with "HELP ME!!" (the table at 00629464, 0x100 bytes a code).
char cheatname[6][256] = {
	{ 0x05, 0x04, 0x00, 0x1c, 0x6f, 0x14, 0x45, 0x13, 0x21 },		// MALLOY
	{ 0x1c, 0x10, 0x1e, 0x1b, 0x65, 0x14, 0x45 },					// TURKEY
	{ 0x1e, 0x76, 0x1e, 0x65, 0x69, 0x02, 0x03, 0x21 },				// V3R5IOF
	{ 0x05, 0x24, 0x20, 0x31, 0x44, 0x24, 0x28, 0x21 },				// Maladim
	{ 0x09, 0x30, 0x3e, 0x3f, 0x52, 0x28, 0x45, 0x13, 0x21 },		// Aurore
	{ 0x24, 0x24, 0x38, (char)0xba, 0x54, 0x28, 0x45, 0x13, 0x21 },	// lat\xeate
};
char cheatkey[] = "HELP ME!!";									// 00629a64

extern char gAllCheatsEnabled;									// 00679ec1 (CGame::Update sets it)

BOOL CFEPSaveGame::IsCheatActive(int cheatno) 	// FUNCTION: 0x00465490
{
	char mungedname[256];

	if (CLIPARAMS.mAutoConfigTest || gAllCheatsEnabled)
		return(TRUE);

	for (unsigned int i = 0; i < strlen(cheatkey); i++)
		mungedname[i] = cheatname[cheatno][i] ^ cheatkey[i];

	char *savename = FromWCHAR(mSaveGameName);

	if (strstr(savename,mungedname)!=NULL)
		return(TRUE);
	else
		return(FALSE);
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x00464530 init:ZERO_FVECTOR
// FUNCTION: 0x00464550 init:ID_FMATRIX
