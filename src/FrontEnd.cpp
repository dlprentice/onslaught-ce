#include	"Common.h"

#define FRONTEND_CPP
#define ASYNCCACHE_FULL	// CFEPGoodies' loader is constructed here (00465ff6)

#include	"FrontEnd.h"
#include	"EventManager.h"
#include	"CLIParams.h"
#include	"Platform.h"
#include	"music.h"
#include    "Career.h"
#include	"ltshell.h"
#include	"RenderInfo.h"
#include	"SpriteRenderer.h"
#include	"engine.h"
#include	"meshrenderer.h"
#include	"FEMessBox.h"
#include	"memorycard.h"
#include	"reconnectinterface.h"
#include	"endleveldata.h"
#include	"console.h"
#include	"PCController.h"
#define CCONTROLLER CPCController
#include	"ParticleManager.h"
#include	"debuglog.h"
#include	"mouse.h"

#include	"Game.h"

//*********************************************************************************
extern SINT performing_stress_test;				// 0083d454
extern char	gAllCheatsEnabled;					// 00679ec1

// a debug request (00441b10)

float	gFrontEndFrameStep;						// 00679af8
void CFEPUnknown::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00452da0
{
}

bool	gInFrontEnd = false;							// 00679b40: set while CFrontEnd::Run runs

//*********************************************************************************

bool	CFrontEnd::IsRunning()	// FUNCTION: 0x00465f00
{
	return gInFrontEnd;
}

//*********************************************************************************
CFrontEnd::CFrontEnd(void)	// FUNCTION: 0x00465f10
:
mAutoSave(CFrontEnd::AUTO_SAVE_NOT),
mFirstRun(TRUE),
mSelectedLevel(-1),
mAllLevelsCheatActive(FALSE)
{
	mSaveFilename[0] = 0;
	mPlayer0ControllerPort = -1;
}
// The constructor brings the out-of-line copies of what it calls and of the destructors its unwinding needs:
// FUNCTION: 0x00466130 ??0CGenericCamera@@QAE@XZ
// FUNCTION: 0x00466140 ?GetPos@CGenericCamera@@UAE?AVFVector@@XZ
// FUNCTION: 0x00466170 ??_GCGenericCamera@@UAEPAXI@Z
// FUNCTION: 0x00466190 ??_GCFrontEnd@@UAEPAXI@Z
// FUNCTION: 0x004661b0 ??1CGenericCamera@@UAE@XZ
// FUNCTION: 0x004661c0 ??1CDXFrontEndVideo@@UAE@XZ
// FUNCTION: 0x004661d0 ??1CFEPBEConfig@@QAE@XZ
// FUNCTION: 0x004661e0 ??1CFEPWingmen@@QAE@XZ
// FUNCTION: 0x004661f0 ??1CFEPGoodies@@QAE@XZ
// FUNCTION: 0x00466200 ??1CFrontEnd@@UAE@XZ
// FUNCTION: 0x00466290 ??1CXBOXAsyncCache@@QAE@XZ

// slightly different loading speeds on the different platforms.
#define LOADING_STEP_1 80
#define LOADING_STEP_2 90
#define LOADING_STEP_3 100

//*********************************************************************************
BOOL	CFrontEnd::Init(EFrontEndEntry entry,BOOL inLoadedSystem)	// FUNCTION: 0x004662a0
{
	CONSOLE.SetLoading(TRUE, TRUE, FALSE);	// retail pushes 1, 1, 0 (the default is 1, 1, 1)

	if (inLoadedSystem)
		CONSOLE.SetLoadingRange( 50.f, 62.5f );
	else
		CONSOLE.SetLoadingRange( 0.f, 25.f );

	if (CLIPARAMS.mSound)
		SOUND.ReloadLanguageSampleBank();

	EVENT_MANAGER.Init() ;
//	CAREER.Test();

	CAREER.Update() ;

	if (inLoadedSystem)
		CONSOLE.SetLoadingRange( 62.5f, LOADING_STEP_1/2+50.0f );
	else
		CONSOLE.SetLoadingRange( 25.f, LOADING_STEP_1 );

	// Initialise Data
	if (!FED.Init())
		return FALSE;

	if (!FET.Init())
		return FALSE;

	if (inLoadedSystem)
		CONSOLE.SetLoadingRange( LOADING_STEP_1/2+50.0f, LOADING_STEP_2/2+50.0f );
	else
		CONSOLE.SetLoadingRange( LOADING_STEP_1, LOADING_STEP_2 );

	if (!FEV.Init())
		return FALSE;

	// Memory cards
	MEMORYCARD.Initialise();

	// Initialise the messagebox class
	FEMESSBOX.Init();

	if (inLoadedSystem)
		CONSOLE.SetLoadingRange( LOADING_STEP_2/2+50.0f, LOADING_STEP_3/2+50.0f );
	else
		CONSOLE.SetLoadingRange( LOADING_STEP_2, LOADING_STEP_3 );

	// Build pointer list for pages
	SINT	c0;

	// fill with unknown to start with.
	for (c0 = 0; c0 < FEP_NUM_PAGES; c0 ++)
		mPages[c0] = &mFEPUnknown; 

	// and the actual pages
	mPages[FEP_MAIN]				= &mFEPMain;
	
	mPages[FEP_DEVELOPMENT]			= &mFEPDevelopment;

	mPages[FEP_COMMON]				= &mFEPCommon;

	mPages[FEP_BE_CONFIG]			= &mFEPBEConfig;
	mPages[FEP_WINGMEN]				= &mFEPWingmen;
	mPages[FEP_BRIEFING]			= &mFEPBriefing;
	mPages[FEP_DEBRIEFING]			= &mFEPDebriefing;
	mPages[FEP_LEVEL_SELECT]		= &mFEPLevelSelect;
	mPages[FEP_GOODIES]				= &mFEPGoodies;
	mPages[FEP_DEVSELECT]			= &mFEPDevSelect;
	mPages[FEP_LOADGAME]			= &mFEPLoadGame;
	mPages[FEP_SAVEGAME]			= &mFEPSaveGame;
	mPages[FEP_INTRO]				= &mFEPIntro;
	mPages[FEP_MULTIPLAYER]			= &mFEPMultiplayer;
	mPages[FEP_MULTIPLAYER_START]	= &mFEPMultiplayerStart;
	mPages[FEP_OPTIONS]				= &mFEPOptions;
	mPages[FEP_CREDITS]				= &mFEPCredits;
	mPages[FEP_SCREEN_POS]			= &mFEPScreenPos;

	mPages[FEP_VIRTUAL_KEYBOARD]	= &mFEPVirtualKeyboard;
	mPages[FEP_DIRECTORY]			= &mFEPDirectory;

	mPages[FEP_E3_LEVEL_SELECT]		= &mFEPE3LevelSelect;
	mPages[FEP_LANGUAGE_TEST]		= &mFEPLanguageTest;
	
	mPages[FEP_DEMOMAIN]			= &mFEPDemoMain;

	mPages[FEP_UNKNOWN]				= &mFEPUnknown;

	// Initialise all pages
	BOOL	ok = TRUE;

	for (c0 = 0; c0 < FEP_NUM_PAGES; c0 ++)
	{
		char buf[20];
		sprintf(buf, "FEP %d...", c0);
		TRACE(buf);
		ok &= mPages[c0]->Init();
		TRACE("done.\n");

		CONSOLE.SetLoadingFraction(float(c0) / float(FEP_NUM_PAGES));
	}

	if (!ok)
		return FALSE;

	GAME.mSupervisorMode = TRUE; // for E3

	// Controller Stuff
	for (int port = 0; port < NUM_CONTROLLER_PORTS; port++)
#line 179
		mControllers[port]=new(MEMTYPE_CONTROLLER) CCONTROLLER(this,port);

	PLATFORM.FlushInputBuffers(); // clear up keystrokes pressed while loading

	// Other Data
	mCounter = 0;
	mQuit = -2;
	mLastRenderTime = -100.f;

	GAME.mRenderFrameNumber=0;

	// start point
	if (CLIPARAMS.mGoStraightToDeviceSelectScreen != -1)
	{
		// We were on the "new career" screen before we went to the xbox dashboard
		mActivePage = FEP_UNKNOWN;
		SetPage(FEP_MAIN, 0);
		FRONTEND.SetSaveMode(1);
		FRONTEND.SetAutoSave(FRONTEND.AUTO_SAVE_NORMAL);
		ASSERT(!CAREER.InProgress());
		mFEPDevSelect.SetCurrentCard(CLIPARAMS.mGoStraightToDeviceSelectScreen);
	}
	else if (mFirstRun || entry == FEE_FROM_ATTRACT)
	{
		mFirstRun = FALSE;

		if (!CLIPARAMS.mDeveloperMode)
		{
			mActivePage = FEP_UNKNOWN;

			if (PLAYABLE_DEMO)
			{
				SetPage(FEP_INTRO, 0);
			}
			else
			{
				SetPage(FEP_INTRO, 0);
			}
		}
		else
		{
			mActivePage = FEP_UNKNOWN;

			if (PLAYABLE_DEMO)
			{
				SetPage(FEP_INTRO, 0);
			}
			else
			{
				if (GAME.mSupervisorMode)
					SetPage(FEP_MAIN, 0);
				else
					SetPage(FEP_INTRO, 0);
			}
		}
	}
	else if (entry == FEE_TITLE_SCREEN)
	{
		mActivePage = FEP_UNKNOWN;
		SetPage(FEP_MAIN, 0);
	}
	else
	{
		if ((mSelectedLevel > 900) && (mSelectedLevel < 906)) // Race levels
		{
			if ((GetAutoSave() != AUTO_SAVE_PRETEND) && (performing_stress_test == 0) && (!CLIPARAMS.mAutoConfigTest) && (!gAllCheatsEnabled))
			{
				// Save the damn thing.
				FRONTEND.SetSaveMode(TRUE);
				mActivePage = FEP_UNKNOWN;
				SetPage(FEP_SAVEGAME, 0);
				SetSuccessFEP(FEP_GOODIES, 50);
			}
			else
			{
				mActivePage = FEP_GOODIES;
			}
		}
		else if ((mSelectedLevel >= 850) && (mSelectedLevel < 880))
			mActivePage = FEP_MULTIPLAYER;
		else
		{
			// If it's not the first run, we should autosave.
			if (PLAYABLE_DEMO)
			{
				if (CLIPARAMS.mAttractMode)
					mActivePage=FEP_INTRO;
				else
					mActivePage = FEP_DEBRIEFING;
			}
			else
			{
				if ((GetAutoSave() != AUTO_SAVE_PRETEND) && (performing_stress_test == 0) && (!CLIPARAMS.mAutoConfigTest) && (!gAllCheatsEnabled))
				{
					// Save the damn thing.
					FRONTEND.SetSaveMode(TRUE);
					mActivePage = FEP_UNKNOWN;
					SetPage(FEP_SAVEGAME, 0);
					SetSuccessFEP(FEP_DEBRIEFING, 50);
				}
				else
				{
					mActivePage = FEP_DEBRIEFING;
				}
			}
		}
	}

	if (mActivePage != FEP_TRANSITION)
	{
		mPages[mActivePage]->TransitionNotification(FEP_COMMON);
		mPages[mActivePage]->ActiveNotification(FEP_COMMON);
	}
	else
	{
		mPages[mTransitionTo]->TransitionNotification(FEP_COMMON);
	}

	// Selected Data
	mSelectedLevel = 100;

	// Load and cache all the language Files
	for (c0 = 0; c0 < NUM_LANGUAGES; c0 ++)
	{
		mTextSets[c0].Init();
		mTextSets[c0].Load(c0);
	}

	if (inLoadedSystem)
		CONSOLE.SetLoadingRange( LOADING_STEP_2/2+50.0f, 100.f );
	else
		CONSOLE.SetLoadingRange( LOADING_STEP_2, 100.f );

	CONSOLE.SetLoading(FALSE, TRUE, FALSE);	// retail pushes 0, 1, 0

	// Start the music
	if (CLIPARAMS.mMusic)
		MUSIC.PlaySelection(MUS_FRONTEND);

	return TRUE;
}	

//*********************************************************************************
void	CFrontEnd::Shutdown()	// FUNCTION: 0x00466880
{	
	// Stop the music
	if (CLIPARAMS.mMusic)
		MUSIC.Stop();

	// Memory cards
	MEMORYCARD.Shutdown();	

	// free up the text
	SINT	c0;
	for (c0 = 0; c0 < NUM_LANGUAGES; c0 ++)
	{
		mTextSets[c0].Shutdown();
	}

	IController::Shutdown();

	// Send final deactivation Notification
	if (mActivePage == FEP_TRANSITION)
	{
		mPages[mTransitionFrom]->DeActiveNotification();
		mPages[mTransitionTo]->DeActiveNotification();
	}
	else
		mPages[mActivePage]->DeActiveNotification();

	// Shutdown all pages
	for (c0 = 0; c0 < FEP_NUM_PAGES; c0 ++)
		mPages[c0]->Shutdown();

	// Shutdown FMV
	FEV.Shutdown();

	FET.Shutdown();

	FED.Shutdown();	

	// Shutdown Controller
	for (int port = 0; port < NUM_CONTROLLER_PORTS; port++)
	{
		delete mControllers[port];
		mControllers[port]=NULL;
	}

	// Shutdown System Stuff
	EVENT_MANAGER.Shutdown();

	CONSOLE.ClearCommandsAndVariables();

	// Clean up memory.
	MEM_MANAGER.SetMerge( TRUE );
	MEM_MANAGER.Cleanup();
}

//*********************************************************************************
int		CFrontEnd::GetPlayer0ControllerPort()	// FUNCTION: 0x00466980
{
	if (mPlayer0ControllerPort == -1) return 0;

	return mPlayer0ControllerPort;
}

//*********************************************************************************
int		CFrontEnd::GetPortFromController(CController *controller)
{
	for (int i = 0; i < NUM_CONTROLLER_PORTS; i++)
	{
		if (controller == mControllers[i]) return i;
	}

	// Not found, what the hell.
	return 0;
}

//*********************************************************************************
int		CFrontEnd::NumControllersPresent()	// FUNCTION: 0x00466990
{
	return 2;
}

//*********************************************************************************
void	CFrontEnd::ReceiveButtonAction(CController* from_controller, int button, float val)	// FUNCTION: 0x004669a0
{
	// We make decisions about which controller is which player.
	// The first person to press anything is player 0.
	if (mPlayer0ControllerPort == -1 && button == BUTTON_FRONTEND_MENU_SELECT)
	{
		// He can be this.
		mPlayer0ControllerPort = GetPortFromController(from_controller);
	}
	else
	if (mPlayer0ControllerPort != -1)
	{
		// There was a player 0 controller already. It may not be this one,
		// in which case we ignore inputs

		// Unless we're in the multiplayer start screen which understands multiple controllers.
		if (mActivePage != FEP_MULTIPLAYER_START && GetPortFromController(from_controller) != mPlayer0ControllerPort) return;
	}

	// globally remember the current controller.
	mReceivingButtonActionPort = GetPortFromController(from_controller);

	if ((button == BUTTON_FRONTEND_CHEAT) &&
		(mActivePage != FEP_TRANSITION))
	{
		mAllLevelsCheatActive=!mAllLevelsCheatActive;
		mPages[mActivePage]->ButtonPressed(button, val);
	}
	else if (FEMESSBOX.IsInteractive())
	{
		// If a message box is being displayed, then redirect the input into that
		FEMESSBOX.ButtonPressed(button, val);
	}
	else
	{
		// Pass Button Action on to page
		if (mActivePage != FEP_TRANSITION)
			mPages[mActivePage]->ButtonPressed(button, val);
	}
}

//*********************************************************************************
void	CFrontEnd::SetLanguage(SINT l)	// FUNCTION: 0x00466ab0
{
	CFEPOptions::Cleanup();
	TEXT_DB.Copy(mTextSets[l]);
}

//*********************************************************************************
void	CFrontEnd::SetPage(EFrontEndPage page, SINT time)	// FUNCTION: 0x00466ae0
{
	// Shouldn't happen...
	if (mActivePage == FEP_TRANSITION)
		mActivePage = mTransitionFrom;

	if (time == 0)
	{
		mPages[mActivePage]->DeActiveNotification();

		// go straight there
		mPages[page]->TransitionNotification(mActivePage);
		mPages[page]->ActiveNotification(mActivePage);

		mActivePage = page;
	}
	else
	{
		// Setup Transition Parameters
		mTransitionFrom = mActivePage;
		mTransitionTo = page;
		mTransitionCount = 0;
		mTransitionTime = time;
		mActivePage = FEP_TRANSITION;

		mPages[mTransitionTo]->TransitionNotification(mTransitionFrom);
	}
}

//*********************************************************************************
void	CFrontEnd::Process()	// FUNCTION: 0x00466ba0
{
	ResetMouseTransientState();
	LT.ClearKeyWasUp();

	mCounter += gFrontEndFrameStep * 50.f;

	//System Stuff
	EQuitType q = PLATFORM.Process();

	// We need to check if we have to quit due to user inactivity (will only happen in playable demo.	
	if (CController::InactivityMeansQuitGame())
		mQuit = -3;

	if (q != QT_NONE)
		mQuit = -1;

	EVENT_MANAGER.Update() ;

	PARTICLE_MANAGER.Process(1.f, TRUE);
	SOUND.UpdateStatus();
	MUSIC.UpdateStatus();

	// Update controller
	for (int port = 0; port < NUM_CONTROLLER_PORTS; port++)
		mControllers[port]->Flush();

	// Update Video, but don't wait for the next frame of the video
	FEV.NextFrame(false);

	// We need to process insertion and removal of controllers
	if (mPlayer0ControllerPort != -1)
	{
		BOOL unconnected = FALSE;

		if (!RECONNECT_INTERFACE[0].Process(mControllers[mPlayer0ControllerPort], false) && RECONNECT_INTERFACE[0].GetState() != CReconnectInterface::OK)
			unconnected = TRUE;

		if (mActivePage == FEP_MULTIPLAYER_START)
		{
			int port = mFEPMultiplayerStart.GetPlayer1ControllerPort();
			if (port != -1)
			{
				if (!RECONNECT_INTERFACE[1].Process(mControllers[port], false) && RECONNECT_INTERFACE[1].GetState() != CReconnectInterface::OK)
					return;
			}
		}

		// we're still unconnected. So no frontend processing thanks.
		if (unconnected)
			return;
	}

	// the frame time, filtered
	static float last_time = -12345.f;
	float time = PLATFORM.GetSysTimeFloat();
	if ((last_time != -12345.f) && (time - last_time < 2.f))
		gFrontEndFrameStep = (time - last_time) * 0.1f + gFrontEndFrameStep * 0.9f;
	else
		gFrontEndFrameStep = 1.f / 60.f;
	last_time = time;

	// deal with transitions
	if (mActivePage == FEP_TRANSITION)
	{
		mTransitionCount += gFrontEndFrameStep * 50.f;

		if (mTransitionCount >= mTransitionTime)
		{
			mPages[mTransitionFrom]->DeActiveNotification();
			mActivePage = mTransitionTo;
			mPages[mActivePage]->ActiveNotification(mTransitionFrom);
		}
	}

	// process pages
	SINT	c0;

	for (c0 = 0; c0 < FEP_NUM_PAGES; c0 ++)
	{
		EFEPState state = FEPS_INACTIVE;

		if (mActivePage == c0)
			state = FEPS_ACTIVE;
		else if (mActivePage == FEP_TRANSITION)
		{
			if (c0 == mTransitionFrom)
				state = FEPS_TRANSITIONING_FROM;
			if (c0 == mTransitionTo)
				state = FEPS_TRANSITIONING_TO;
		}

		mPages[c0]->Process(state);
	}

	// Process the messagebox
	FEMESSBOX.Process();
}

//*********************************************************************************
#define	LINE_SPRITE_SIZE	32.f

void	CFrontEnd::DrawLine(float sx, float sy, float ex, float ey, DWORD col, float width, float depth, float perc)	// FUNCTION: 0x00466de0
{
	// essentially a dodgy line draw
	float	dx = ex - sx;
	float	dy = ey - sy;

	float	cx = (sx + ex) / 2;
	float	cy = (sy + ey) / 2;

	float	yaw = atan2f(dy, dx);

	float	xscale = sqrtf(dx * dx + dy * dy) / LINE_SPRITE_SIZE * perc;
	float	yscale = width / LINE_SPRITE_SIZE / 2.f;

	CSPRITERENDERER::DrawColouredSprite(cx, cy, depth, FED.GetTexture(FET_LEVEL_LINK),col, xscale, yscale, SA_CENTRE, SPT_NONE, 1.f, yaw);
}

//*********************************************************************************

void	CFrontEnd::DrawBox(float tlx, float tly, float brx, float bry, DWORD col, float width, float depth)	// FUNCTION: 0x00466e70
{
	DrawLine( tlx, tly, brx, tly, col, width, depth, 1.0f );
	DrawLine( brx, tly, brx, bry, col, width, depth, 1.0f );
	DrawLine( brx, bry, tlx, bry, col, width, depth, 1.0f );
	DrawLine( tlx, bry, tlx, tly, col, width, depth, 1.0f );
}

//*********************************************************************************

#define BLANK_PANEL_SIZE	( 16.0f )

void	CFrontEnd::DrawPanel(float tlx, float tly, float brx, float bry, float z, DWORD col)	// FUNCTION: 0x00467010
{
	RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
	RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);

	CSPRITERENDERER::DrawColouredSprite(tlx, tly, z, FED.GetTexture(FET2_BLANK), col, ((brx - tlx) / BLANK_PANEL_SIZE), ((bry - tly) / BLANK_PANEL_SIZE), SA_TOP_LEFT);

	RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
	RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
}

//*********************************************************************************

void	CFrontEnd::DrawBarGraph(float tlx, float tly, float brx, float bry, float num, float max, float z, SINT bordercol, SINT backcol, SINT forecol)	// FUNCTION: 0x004670b0
{
	// Draw background
	DrawPanel(tlx, tly, brx, bry, z, backcol);

	// Draw bar
	if (num)
	{
		DrawPanel(tlx, tly, tlx + ((brx - tlx) * (num / max)), bry, z, forecol);
	}

}

//*********************************************************************************

// does this page have the curve on either side?
static BOOL got_standard_SlidingTextBordersAndMask(EFrontEndPage dest)	// FUNCTION: 0x004679a0
{
	switch(dest)
	{
	case FEP_DEVSELECT:
	case FEP_DIRECTORY:
	case FEP_GOODIES:
	case FEP_LEVEL_SELECT:
	case FEP_LOADGAME:
	case FEP_MULTIPLAYER:
//	case FEP_MULTIPLAYER_START:
	case FEP_SAVEGAME:
	case FEP_VIRTUAL_KEYBOARD:
	case FEP_SCREEN_POS:
	case FEP_OPTIONS:
		return TRUE;

	default:
		return FALSE;
	}
}

BOOL	check_draw_twice(EFrontEndPage cp, EFrontEndPage dest)
{
	if (got_standard_SlidingTextBordersAndMask(cp) && got_standard_SlidingTextBordersAndMask(dest))
		return SINT(dest) > SINT(cp); // cheeky!
	else
		return FALSE;
}

void CFrontEnd::DrawSlidingTextBordersAndMask(float transition, EFrontEndPage dest)	// FUNCTION: 0x00467200
{
	EFrontEndPage from = FEP_UNKNOWN;

	// make sure we don't draw it twice...
	if (GetCurrentPage() == FEP_TRANSITION)
	{
		from = GetTransitionTo() == dest ? GetTransitionFrom(): GetTransitionTo();
		if (check_draw_twice(from, dest))
			return;
	}

	float tr, scale, yaw;
	SINT alpha;
	DWORD	col;

	//*************************************************************************
	// Sliding Text
	if ((dest != FEP_BRIEFING) && (dest != FEP_DEBRIEFING) && (dest != FEP_MULTIPLAYER_START))
		tr = RangeTransition(transition, 0.75f, 1.f);
	else
		tr = 1.f;

	FRONTEND.GetCommonPage()->RenderSmallForsetiText(tr);

	//*************************************************************************
	// Metal Borders
#define SELECT_BRACKET_X	328
#define SELECT_BRACKET_Y	343
#define	SELECT_BRACKET_SCALE	1.25f
#define	SELECT_BRACKET_SCALE2	1.4f

	if (got_standard_SlidingTextBordersAndMask(dest) && (from != FEP_VIRTUAL_KEYBOARD) && (from != FEP_OPTIONS) && (dest != FEP_VIRTUAL_KEYBOARD) && (dest != FEP_OPTIONS))
		transition = 1.f;

	// Draw inside bracket
	col = 0xffffffff;
	alpha = 0xff;
	scale = (((from == FEP_VIRTUAL_KEYBOARD) || (from == FEP_OPTIONS) || (dest == FEP_VIRTUAL_KEYBOARD) || (dest == FEP_OPTIONS) || (GetCurrentPage() == FEP_VIRTUAL_KEYBOARD) || (GetCurrentPage() == FEP_OPTIONS)) ? SELECT_BRACKET_SCALE2 : SELECT_BRACKET_SCALE);
	yaw = 0.f;
	if ((transition < 1.f) && (dest == FEP_MAIN))
	{
		if (transition < 0.2f)
		{
			alpha = 0;
		}
		if (transition < 0.5f)
		{
			tr = RangeTransition(transition, 0.2f, 0.5f);
			alpha = MakeAlpha(tr);
			scale = tr * 1.f;
			yaw = -(tr * 0.3f);
		}
		else if (transition < 0.6f)
		{
			scale = 1.f;
		}
		else if (transition < 0.7f)
		{
			tr = RangeTransition(transition, 0.6f, 0.7f);
			scale = Range(tr, 1.f, scale);
		}
	}
	else if ((transition < 1.f) && (from == FEP_VIRTUAL_KEYBOARD))
	{
		scale = CosResize(transition, SELECT_BRACKET_SCALE, SELECT_BRACKET_SCALE2);
	}
	else if (transition < 1.f)
	{
		tr = 1.f - RangeTransition(transition, 0.5f, 1.f);
		alpha = MakeAlpha(1.f - tr);
		scale = SELECT_BRACKET_SCALE + (tr * 1.4f);
		yaw = -tr * 1.0f;
	}

	BlendAlpha(col, alpha);

	CSPRITERENDERER::DrawColouredSprite(SELECT_BRACKET_X + 5 + FRONTEND.GetShadowOffsetX(), SELECT_BRACKET_Y + 10 + FRONTEND.GetShadowOffsetY(), FO(350), FED.GetTexture(FET3_SELECT_BRACKET1), BlendAlpha2(0x3F000000, alpha), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, yaw);
	CSPRITERENDERER::DrawColouredSprite(SELECT_BRACKET_X, SELECT_BRACKET_Y, FO(200), FED.GetTexture(FET3_SELECT_BRACKET1), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);

	float savescale = scale;

	// Draw outside bracket
	col = 0xffffffff;
	alpha = 0xff;
	scale = 1.f;
	yaw = 0.f;
	BOOL draw = FALSE;
	if ((transition < 1.f) && (dest == FEP_MAIN))
	{
		if (transition < 0.2f)
		{
		}
		if (transition < 0.5f)
		{
			tr = RangeTransition(transition, 0.2f, 0.5f);
			alpha = MakeAlpha(tr);
			scale = tr * 1.f;
			yaw = -(tr * 0.3f);
			draw = TRUE;
		}
		else if (transition < 0.7f)
		{
			scale = 1.f;
			draw = TRUE;
		}
		else if (transition < 0.9f)
		{
			tr = 1.f - RangeTransition(transition, 0.7f, 0.9f);
			alpha = MakeAlpha(tr);
			scale = tr * 1.f;
			yaw = -(tr * 0.3f);
			draw = TRUE;
		}
	}

	BlendAlpha(col, alpha);

	if (draw)
	{
		CSPRITERENDERER::DrawColouredSprite(SELECT_BRACKET_X + 5 + FRONTEND.GetShadowOffsetX(), SELECT_BRACKET_Y + 10 + FRONTEND.GetShadowOffsetY(), FO(350), FED.GetTexture(FET3_SELECT_BRACKET2), BlendAlpha2(0x3F000000, alpha), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, yaw);
		CSPRITERENDERER::DrawColouredSprite(SELECT_BRACKET_X, SELECT_BRACKET_Y, FO(200), FED.GetTexture(FET3_SELECT_BRACKET2), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);
	}

	// masks?
	if (transition > 0.75f || (from == FEP_VIRTUAL_KEYBOARD) || (from == FEP_OPTIONS))
	{
		RENDERINFO.SRS(RS_SRCBLEND,	BLEND_ZERO);
		RENDERINFO.SRS(RS_DESTBLEND, BLEND_ONE);
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
		RENDERINFO.Apply();

		CSPRITERENDERER::DrawColouredSprite(SELECT_BRACKET_X, SELECT_BRACKET_Y, FO(100), FED.GetTexture(FET3_SELECT_BRACKET_MASK), 0xffffffff, savescale, savescale, SA_CENTRE);

		// cover left hand edge (doesn't quite reach....
		CSPRITERENDERER::DrawColouredSprite(10, 240, FO(100), FED.GetTexture(FET2_WHITE), 0xffffffff, 2.f, 15.f, SA_CENTRE);
		
		RENDERINFO.SRS(RS_SRCBLEND,	BLEND_SRCALPHA);		
		RENDERINFO.SRS(RS_DESTBLEND,BLEND_INVSRCALPHA);	
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
		RENDERINFO.Apply();
	}
}

//*********************************************************************************
#define HEADER_BAR_SIZE	64

//*********************************************************************************
void	CFrontEnd::DrawStandardVideoBackground(float transition, DWORD colour, EFrontEndPage dest)	// FUNCTION: 0x004679e0
{
	{
		SINT	alpha;
		
		if (dest == FEP_MAIN)
			alpha = MakeAlpha(RangeTransition(transition, 0.f, 0.5f));
		else
			alpha = MakeAlpha(RangeTransition(transition, 0.5f, 1.f));

		FRONTEND.GetCommonPage()->RenderVideoBackground(1.f, BlendAlpha2(colour, alpha));
	}
}

void	CFrontEnd::DrawBar(float sx, float sy, float z, SINT segs, DWORD col, float scale)	// FUNCTION: 0x00467ae0
{
	RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
	RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);

	float y = sy - ((HEADER_BAR_SIZE / 2) * scale);
	float x = sx - (((HEADER_BAR_SIZE / 2) * (segs + 2)) * scale);

	SINT	c0;
	for (c0 = 0; c0 < segs + 2; c0 ++)
	{
		CTEXTURE *tex = FED.GetTexture(FET_BARC);

		if (c0 == 0)		tex = FED.GetTexture(FET_BARL);
		if (c0 == segs + 1) tex = FED.GetTexture(FET_BARR);

		CSPRITERENDERER::DrawColouredSprite(x, y, z, tex,col, scale, scale, SA_TOP_LEFT);

		x += (HEADER_BAR_SIZE) * scale;
	}

	RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
	RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
}

//*********************************************************************************

//#define	TITLE_TEXT_SCALE_X	0.8f
#define	TITLE_TEXT_SCALE_X	1.f

#define HEADER_BAR_X		390.f
#define HEADER_BAR_Y		69.f
#define	HEADER_BAR_SCALE	1.25f

void	CFrontEnd::DrawTitleBar(WCHAR *text, float transition, EFrontEndPage dest)	// FUNCTION: 0x00467bd0
{
	DWORD	col;
	SINT	alpha = 0xff;
	float	tr;
	float	scale, yaw;
	float	yo = 0;

	if ((transition < 1.f) && (dest != FEP_MAIN))
	{
		if (transition < 0.5f)
			return;

		yo = -sinf(transition * PI) * 200.f;
	}

	// Draw Text Box
	scale = 1.f;
	col = 0x7f000000;
	yaw = 0.f;
	tr = RangeTransition(transition, 0.8f, 1.f);
	alpha = MakeAlpha(tr);
	BlendAlpha(col, alpha);


	CSPRITERENDERER::DrawColouredSprite(HEADER_BAR_X, HEADER_BAR_Y + yo + 10, FO(330), FED.GetTexture(FET3_HEADER_TEXT_BOX), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);


	//*************************************************************************
	// Options Brackets #1

	// Draw inside bracket
	col = 0xffffffff;
	alpha = 0xff;
	scale = HEADER_BAR_SCALE;
	yaw = 0.f;
	if ((transition < 1.f) && (dest == FEP_MAIN))
	{
		if (transition < 0.3f)
			alpha = 0;
		else if (transition < 0.7f)
		{
			tr = RangeTransition(transition, 0.3f, 0.7f);
			alpha = MakeAlpha(tr);
			scale = tr * 1.f;
			yaw = -((1.f - tr) * 0.3f);
		}
		else if (transition < 0.8f)
		{
			scale = 1.f;
		}
		else if (transition < 1.f)
		{
			tr = RangeTransition(transition, 0.8f, 1.f);
			scale = Range(tr, 1.f, HEADER_BAR_SCALE);
		}
	}

	BlendAlpha(col, alpha);

	CSPRITERENDERER::DrawColouredSprite(HEADER_BAR_X + 5 + FRONTEND.GetShadowOffsetX(), HEADER_BAR_Y + yo + 5 + FRONTEND.GetShadowOffsetX(), FO(350), FED.GetTexture(FET3_HEADER_BRACKET1), BlendAlpha2(0x3F000000, alpha), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, yaw);
	CSPRITERENDERER::DrawColouredSprite(HEADER_BAR_X, HEADER_BAR_Y + yo, FO(300), FED.GetTexture(FET3_HEADER_BRACKET1), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);

	// Draw outside bracket
	col = 0xffffffff;
	alpha = 0xff;
	scale = 1.f;
	yaw = 0.f;
	BOOL	draw = FALSE;
	if ((transition < 1.f) && (dest == FEP_MAIN))
	{
		if (transition < 0.3f)
		{
		}
		else if (transition < 0.7f)
		{
			tr = RangeTransition(transition, 0.3f, 0.7f);
			alpha = MakeAlpha(tr);
			scale = tr * 1.f;
			yaw = -((1.f - tr) * 0.3f);
			draw = TRUE;
		}
		else if (transition < 0.9f)
		{
			scale = 1.f;
			draw = TRUE;
		}
		else if (transition < 1.f)
		{
			tr = 1.f - RangeTransition(transition, 0.9f, 1.f);
			alpha = MakeAlpha(tr);
			scale = tr * 1.f;
			yaw = -((1.f - tr) * 0.3f);
			draw = TRUE;
		}
	}

	BlendAlpha(col, alpha);

	if (draw)
	{
		CSPRITERENDERER::DrawColouredSprite(HEADER_BAR_X + 5 + FRONTEND.GetShadowOffsetX(), HEADER_BAR_Y + yo + 5 + FRONTEND.GetShadowOffsetX(), FO(350), FED.GetTexture(FET3_HEADER_BRACKET2), BlendAlpha2(0x3F000000, alpha), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, yaw);
		CSPRITERENDERER::DrawColouredSprite(HEADER_BAR_X, HEADER_BAR_Y + yo, FO(300), FED.GetTexture(FET3_HEADER_BRACKET2), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);
	}


	col = 0xff7f7f7f;
	if (dest == FEP_MAIN)
	{
		tr = RangeTransition(transition, 0.9f, 1.f);
		alpha = MakeAlpha(tr);
		BlendAlpha(col, alpha);
	}

	SIZE	s;
	PLATFORM.Font( FONT_NORMAL )->GetTextExtent(text, &s);
	PLATFORM.Font( FONT_NORMAL )->DrawTextDynamic(HEADER_BAR_X - s.cx  * 0.5f * TITLE_TEXT_SCALE_X, HEADER_BAR_Y + yo - 4.f, FO(100), TITLE_TEXT_SCALE_X, 1.f, col, text, 1000.f, FALSE, 0);
}

//*********************************************************************************
void	CFrontEnd::EnableAdditiveAlpha()	// FUNCTION: 0x004681c0
{
	RENDERINFO.SRS(RS_SRCBLEND,   BLEND_ONE );
	RENDERINFO.SRS(RS_DESTBLEND,  BLEND_ONE );
}

//*********************************************************************************
void	CFrontEnd::EnableModulateAlpha()	// FUNCTION: 0x004681e0
{
	RENDERINFO.SRS(RS_SRCBLEND,   BLEND_SRCALPHA );
	RENDERINFO.SRS(RS_DESTBLEND,  BLEND_INVSRCALPHA );
}

//*********************************************************************************
BOOL	CFrontEnd::Render(BOOL forcerender)	// FUNCTION: 0x00468200
{
	// A bit naughty, but necessary otherwise the pose caching never thinks the frame has changed
	GAME.mRenderFrameNumber++;

	mLastRenderTime = PLATFORM.GetSysTimeFloat();

	BOOL	started = FRONTEND.RenderStart();

	// jcl - Sorry..
	LT.SetModulate(0);
	RENDERINFO.Apply();

	RENDERINFO.SRS( RS_ZENABLE, TRUE );
	RENDERINFO.SRS( RS_ZWRITEENABLE, TRUE );
	RENDERINFO.SRS( RS_ZFUNC, ZFUNC_LEQUAL );

	// We're using a virtual 640x480 screen

	RENDERINFO.SetVirtualScreenXSize(640);
	RENDERINFO.SetVirtualScreenYSize(480);
	RENDERINFO.SetVirtualScreenEnabled(true);

	if (started)
	{
		float trans = mTransitionCount / mTransitionTime;
		if (trans > 1.f)
			trans = 1.f;
		else if (trans < 0.f)
			trans = 0.f;
		
		// render pages pre common
		if (mActivePage == FEP_TRANSITION)
		{
			// render highest numbered page first
			if (mTransitionTo > mTransitionFrom)
			{
				mPages[mTransitionTo]->RenderPreCommon(trans, mTransitionFrom);
				mPages[mTransitionFrom]->RenderPreCommon(1.f - trans, mTransitionTo);
			}
			else
			{
				mPages[mTransitionFrom]->RenderPreCommon(1.f - trans, mTransitionTo);
				mPages[mTransitionTo]->RenderPreCommon(trans, mTransitionFrom);
			}
		}
		else
		{
			mPages[mActivePage]->RenderPreCommon(1.f, FEP_NONE);
		}

		// render common page
		mPages[FEP_COMMON]->Render(trans, FEP_NONE);

		// render pages
		if (mActivePage == FEP_TRANSITION)
		{
			// render highest numbered page first
			if (mTransitionTo > mTransitionFrom)
			{
				mPages[mTransitionTo]->Render(trans, mTransitionFrom);
				mPages[mTransitionFrom]->Render(1.f - trans, mTransitionTo);
			}
			else
			{
				mPages[mTransitionFrom]->Render(1.f - trans, mTransitionTo);
				mPages[mTransitionTo]->Render(trans, mTransitionFrom);
			}
		}
		else
		{
			mPages[mActivePage]->Render(1.f, FEP_NONE);
		}

		// render more common page stuff
		mFEPCommon.RenderAfter(1.f, FEP_NONE);

		// Render the message box
		FEMESSBOX.Render();
	}

	// maybe stick some memory output here.
	// draw memory manager
	if (ENGINE.GetDrawDebugStuff() & DRAW_MEM_MANAGER)
	{
		MEM_MANAGER.PrintStats() ;	
	}

	if (mPlayer0ControllerPort != -1)
	{
		BOOL multiplayer = FALSE;
		if (mActivePage == FEP_MULTIPLAYER_START)
			multiplayer = TRUE;
		RECONNECT_INTERFACE[0].Render(NULL, mControllers[mPlayer0ControllerPort], multiplayer);
	}

	FRONTEND.RenderEnd(started);

	RENDERINFO.SetVirtualScreenEnabled(false);

	if ((CLIPARAMS.mAutoConfigTest) || (gAllCheatsEnabled))
	{
		if (PLATFORM.KeyOnce(DIK_F8))
			RequestScreenDump(NULL, true);
	}

	return started;
}

//*********************************************************************************
SINT	CFrontEnd::Run(EFrontEndEntry entry,BOOL inLoadedSystem)	// FUNCTION: 0x004684d0
{
	if (mQuit == -1)
		return -1;

	if (!Init(entry,inLoadedSystem))
		return -1;	

	gInFrontEnd = true;

	while (mQuit == -2)
	{
		Process();

		if (mQuit == -2)
		{
			while( !Render() );
		}

		//JCL - Stress Test
		if (performing_stress_test > 0)
			if (performing_stress_test == 3)
			{
				if (mCounter > 60)
					mQuit = -3;
			}
			else
			{
				if (mCounter > 2)
					mQuit = -3;
			}
	}

	Shutdown();

	gInFrontEnd = false;

	return mQuit;
}

//*********************************************************************************
void	CFrontEnd::UpdateCamera()	// FUNCTION: 0x004685a0
{
	FMatrix	matview;
	FVector	campos = FED.mCamera.GetPos();
	ENGINE.GetViewMatrixFromCamera(&FED.mCamera, &matview);
	RENDERINFO.SetView(&matview,&campos);
}

//*********************************************************************************
BOOL	CFrontEnd::RenderStart()	// FUNCTION: 0x004685f0
{
	BOOL beginsceneok=FALSE;

	beginsceneok=PLATFORM.BeginScene();

	if (beginsceneok)
	{
		float howfar = 700, zoom = 1.f;
		RENDERINFO.SetProjection(ENGINE.GetNearZ(),howfar,ENGINE.GetNearZ()*zoom,ENGINE.GetNearZ()*zoom*0.75f);

		UpdateCamera();

		CMESHRENDERER::SetRenderMode(R_Normal);

		RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);

		ENGINE.SetCamera(&FED.mCamera);

		RENDERINFO.SetFogEnabled(FALSE);

		RENDERINFO.Apply();

		return TRUE;
	}
	else
		return(FALSE);
}

//*********************************************************************************
void	CFrontEnd::RenderEnd(BOOL started)	// FUNCTION: 0x00468700
{
	RenderMouseCursor();

	if (started)
	{
		PLATFORM.EndScene();
	}
	
	PLATFORM.Flip();
}

//*********************************************************************************
#define	SHADOW_PERIOD	100.f
#define	SHADOW_RADIUS_X	6.f
#define	SHADOW_RADIUS_Y	3.f

float	CFrontEnd::GetShadowOffsetX()	// FUNCTION: 0x00468730
{
	return 	sinf(float(FRONTEND.GetCounter()) / SHADOW_PERIOD) * SHADOW_RADIUS_X;
}

//*********************************************************************************
float	CFrontEnd::GetShadowOffsetY()	// FUNCTION: 0x00468750
{
	return 	cosf(float(FRONTEND.GetCounter()) / SHADOW_PERIOD) * SHADOW_RADIUS_Y;
}

//*********************************************************************************
void	CFrontEnd::StartMCOperation(WCHAR *message)
{
}

//*********************************************************************************
void	CFrontEnd::EndMCOperation()
{
}

//*********************************************************************************
static char	*GetSoundName(EFrontEndSound sound)
{
	switch (sound)
	{
	case FES_MOVE:
		return "Front End Move";

	case FES_SELECT:
		return "Front End Select";

	case FES_BACK:
		return "Front End Back";

	default:
		return "";
	};
}

void	CFrontEnd::PlaySound(EFrontEndSound sound)	// FUNCTION: 0x00468770
{
	char	*sample = GetSoundName(sound);

	CEffect	*effect = SOUND.GetEffectByName(sample);

	SOUND.PlayEffect(effect, NULL);
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x00465e10 init:ZERO_FVECTOR
// FUNCTION: 0x00465e30 init:ID_FMATRIX
