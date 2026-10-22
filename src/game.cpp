#include	"Common.h"
			
#include	"Game.h"
#include	"Engine.h"
#include	"Map.h"
#include	"World.h"
#include	"EventManager.h"
#include	"Profile.h"
#include	"Imposter.h"
#include	"DebugLog.h"
#include	"Atmospherics.h"
#include	"Console.h"
#include	"Hud.h"
#include	"renderqueue.h"
#include	"SoundManager.h"
#include	"Music.h"
#include	"Camera.h"
#include	"System.h"
#include	"ParticleManager.h"
#include	"ParticleSet.h"
#include	"FearGrid.h"
#include	"Player.h"
#include	"Cutscene.h"
#include	"GameInterface.h"
#include	"CLIParams.h"
#include	"Start.h"
#include	"SpawnPoint.h"
#include	"MessageBox.h" 
//#include	"frontend.h"
#include	"ResourceAccumulator.h"
#include    "MissionScript/scripteventnb.h"
#include    "EndLevelData.h"
#include	"staticshadows.h"
#include	"debugmarker.h"
#include	"state.h"
#include	"fmv.h"
#include	"NavigationMap.h"
#include    "text.h"
#include    "data\missionscripts\text\text.stf"
#include    "HelpText.h"
#include	"messagebox.h"
#include	"visibilitytester.h"
#include	"frontend.h"
#include	"hudpositions.h"
#include    "pausemenu.h"
#include    "data\missionscripts\onsldef.msl"
#include	"Credits.h"
#include    "thingtype.h"
#include	"reconnectinterface.h"

// sorry
#include "tree.h"
#include "CollisionSeekingThing.h"

#include	"SpriteRenderer.h"
#include	"DebugText.h"
#include	"Renderinfo.h"


#include	"memorycard.h"
#include	"Trees.h"


#if TARGET==PS2
#include	"PS2Scene.h"
#include	"PS2Display.h"
#endif

#include	<string.h>
#include	<stdio.h>
// FUNCTION: 0x0046bd20 init:ZERO_FVECTOR
bool		CGame::mFirstInit = true;	WCHAR gWatermarkCoverA[50] = L"Your graphics card is not fully supported, please"; WCHAR gWatermarkCoverB[50] = L"Your graphics card is not fully supported, please";	// retail: 0062ba48, 0062baac (the watermark's covers; on one line to keep Stuart's line numbers)
// FUNCTION: 0x0046bd40 init:ID_FMATRIX

#define GAME_COUNT_WHEN_LOST_OR_DRAW 2.0f	// retail: Stuart had 5.0f (0046f417, 0046f4a8)
#define GAME_COUNT_WHEN_WON 5.0f ;

extern SINT	performing_stress_test;

//******************************************************************************************
// Console commands
//******************************************************************************************

void con_map(char *cmd)	// FUNCTION: 0x0046be10
{
	char mapno[16];

	if (sscanf(cmd,"%*s %s",mapno)!=1)
	{
		CONSOLE.Print("Syntax : map <map number>\n");
		return;
	}

	SYSTEM.mNextLevel = atoi(mapno);
	GAME.SetQuit( QT_QUIT_TO_FRONTEND );
}

//******************************************************************************************

void con_resetmemsizes(char *cmd)	// FUNCTION: 0x0046be80
{
	for (int i=0;i<MEMTYPE_LIMIT;i++)
	{
		GAME.mLastSize[i]=MEM_MANAGER.GetDefaultHeap()->GetTypeSize((EMemoryType) i);
	}
}

//******************************************************************************************
void con_dumptextures(char *cmd)	// FUNCTION: 0x0046bea0
{
	CONSOLE.Print("Dumping textures to TextureDump directory...\n");
	CTEXTURE::DumpInterestingTextures();
	CONSOLE.Print("...done!\n");
}

//******************************************************************************************
void con_dumptimerecords(char *cmd)	// FUNCTION: 0x0046bed0
{
#ifdef DEBUG_TIMERECORDS
	CONSOLE.Print("Dumping time records... ");
	GAME.DumpTimeRecords();
	CONSOLE.Print("...Done!\n");
#else
	CONSOLE.Print("Time record support not compiled in!\n");
#endif
}

//******************************************************************************************
void con_remotecameraon(char *cmd)	// FUNCTION: 0x0046bef0
{
	CThing *what=(CThing *) CONSOLE.GetPlayer()->GetBattleEngine()->GetUnitOverCrossHair();
	if (!what)
	{
		CONSOLE.Print("There is nothing under the crosshair!\n");
		return;
	}
	if (!GAME.mOldRemoteCamera)
	{
		GAME.mOldRemoteCamera=GAME.GetCamera(0);
	}
	CCamera *prev=GAME.mRemoteCamera;
#line 264
	GAME.mRemoteCamera=new( MEMTYPE_CAMERA ) CThingCamera(what);
	GAME.SetCamera(0,GAME.mRemoteCamera);
	if (prev)
		delete prev;
}

//******************************************************************************************
// FUNCTION: 0x0046c030 ??_GCThingCamera@@UAEPAXI@Z
// FUNCTION: 0x0046c050 ??1CThingCamera@@UAE@XZ
void con_remotecameraoff(char *cmd)	// FUNCTION: 0x0046c0b0
{
	if (!GAME.mRemoteCamera)
	{
		CONSOLE.Print("Remote camera not active!\n");
		return;
	}
	if (!GAME.mOldRemoteCamera)
	{
		CONSOLE.Print("No camera to return to!\n");
		return;
	}

	GAME.SetCamera(0,GAME.mOldRemoteCamera);

	GAME.mOldRemoteCamera=NULL;
	delete GAME.mRemoteCamera;
	GAME.mRemoteCamera=NULL;
}

//******************************************************************************************
void con_navmapon(char *cmd)	// FUNCTION: 0x0046c120
{
	ENGINE.LandscapeNavDisplay(TRUE);
	ENGINE.UpdateArea(0,0,512,512);
}

//******************************************************************************************
void con_navmapoff(char *cmd)	// FUNCTION: 0x0046c150
{
	ENGINE.LandscapeNavDisplay(FALSE);
	ENGINE.UpdateArea(0,0,512,512);
}
//******************************************************************************************
void con_win(char *cmd)	// FUNCTION: 0x0046c180
{
	GAME.DeclareLevelWon();
}

//******************************************************************************************
void con_lose(char *cmd)	// FUNCTION: 0x0046c200
{
	GAME.DeclareLevelLost();
}
#ifdef NOT_YET_RECONSTRUCTED
//******************************************************************************************
#ifdef RESBUILDER
void con_buildresources(char *cmd)
{
	if (stricmp(cmd,"buildresources pc")==0)
		GAME.BuildResources(GAME.GetCurrentLevel(),PC);
	else if (stricmp(cmd,"buildresources xbox")==0)
		GAME.BuildResources(GAME.GetCurrentLevel(),XBOX);
	else if (stricmp(cmd,"buildresources ps2")==0)
		GAME.BuildResources(GAME.GetCurrentLevel(),PS2);
	else
		CONSOLE.Print("Unknown target platform!");
}
#endif



//******************************************************************************************
// CGame methods
//******************************************************************************************
#endif	// NOT_YET_RECONSTRUCTED
CGame::CGame()	// FUNCTION: 0x0046c210
{
	mSettings.mPlayer1Configuration=0;
	mSettings.mPlayer2Configuration=0;
	mSettings.mWingman=kBillyWingman;
	mSettings.mWingmanMesh[0]=0;
	mSettings.mPlayer1CockpitMesh[0]=0;
	mSettings.mPlayer2CockpitMesh[0]=0;
	mSettings.mInvertSides=FALSE;
	mCurrentlyRunningLevel =-1;
	mRenderFrameNumber = 0;

	mSupervisorMode = FALSE;

	mShowDataSizes=FALSE;
	mShowMemDeltas=FALSE;

	mLevelStartTime=0.0f;
}
// FUNCTION: 0x0046c2b0 ??_GCGame@@UAEPAXI@Z
//******************************************************************************************
CGame::~CGame()	// FUNCTION: 0x0046c2d0
{
}

#include "cylinder.h"



//******************************************************************************************
BOOL CGame::Init()	// FUNCTION: 0x0046c360
{	
	mCurrentlyRunningLevel =-1;
	ENGINE.SetNumViewpoints(0);
	mRestarting = FALSE ; 
	
	// Initialise console

	CONSOLE.InitDefaultCommands();

	// Initialise Map
	if(!(MAP.Init())) return FALSE;

	// Initialise Engine
	if(!(ENGINE.Init())) return FALSE;

	// Initialise Imposters
	if (!(CIMPOSTER::InitAll()))
		return FALSE;

	// Initialise rendering queue
	RENDERQUEUE.Init();

	// Initialise static shadows
	STATICSHADOWS.Initialise();		

	// Initialise atmospherics
	// JCL - moved to after resource file is loaded...
//	CAtmospherics::InitialiseAll();

	GAMEINTERFACE.Init();

	// Initialise HUD
	HUD.Init();

	CONSOLE.RegisterVariable("cg_showmemdeltas","Should memory deltas be shown?",CVar_bool,&mShowMemDeltas);
	CONSOLE.RegisterVariable("cg_showdatasizes","Should level data sizes be shown?",CVar_bool,&mShowDataSizes);	

	mRetryCount = 0;

	return TRUE;

}
//******************************************************************************************
BOOL CGame::InitRestartLoop()	// FUNCTION: 0x0046c430
{	
	InitMouse();	// retail (0046c44e)

#ifdef _DIRECTX
	// Let's force the landscape to refresh to the right LOD right now.
	ENGINE.GetLandscape()->MakeNextUpdateBeFast();
#endif

	HUD.Reset();

	mScore=0;
	mPreRunTime = 3.0f;
	mPanTime = 3.0f;
	mControlMode = 0 ;
	mPausedAllGameSounds = FALSE ;

	mFixedFrameRate = FALSE ;
	mLastUpdateTime = -1.0f ;
	mFrameLength = 0.05f;
	mRenderFrameNumber = 0;
	mEndLevelCount = -1.0f ;
	mHackCurrentGameMasterVolume = 1.0f;
	mHorizontalSplitscreen=true;
	mAllowedAutoAim =TRUE;
	mFadeOut = FALSE ;

	mForsetiFearGrid = NULL;
	mMuspellFearGrid = NULL;
	mSlots = CAREER.GetSlots() ;
	int i = 0 ;
	for( i=0; i<MAX_PLAYERS; i++ )
	{
		mCurrentCamera[ i ] = NULL;
		mPlayer[ i ] = NULL ;
		mFreeCameraOn[ i ] = FALSE ;
		mController[ i ] = NULL;
	}


	mRemoteCamera = NULL;
	mOldRemoteCamera = NULL;
	mRandomStream = NULL;
	mInterleavedSplitscreen=FALSE;
	mFullscreenMultiplayer=FALSE;
	mLevelLostReason = 0;
	mCurrentInterleavedScreen=0;
	mPrimaryObjectives.SetAll(CMissionObjective());
	mSecondaryObjectives.SetAll(CMissionObjective()) ;

	mQuit = QT_NONE;

	DEBUGMARKERS.Initialise();

	EVENT_MANAGER.Init();

	// Initialise Particles
	PARTICLE_MANAGER.Init();

	MAP_WHO.Create();
	
	// Initialise the game interface

	GAMEINTERFACE.Reset();

	// Initialise rendering queue
	RENDERQUEUE.Init();
	
#if TARGET==PS2
	// Clear imposters
	CIMPOSTER::ClearAll();
#endif
	
	// Clear scripts
	SCRIPT_EVENT_NB.Init();

	#if ENABLE_PROFILER==1
	CProfiler::Init();
	#endif

	mPause  = FALSE;
	mGameState = GAME_STATE_PRE_RUNNING;

	EVENT_MANAGER.AddEvent(mPreRunTime, FINISHED_PRE_RUN, this) ;

	mAdvanceFrame = FALSE;
	mCurrentDebugSquadNum = -1; 
	mCurrentDebugUnitNum = -1;

#line 505
	mForsetiFearGrid=new( MEMTYPE_FEARGRID ) CFearGrid(kForsetiAllegiance);
	mMuspellFearGrid=new( MEMTYPE_FEARGRID ) CFearGrid(kMuspellAllegiance);
	mMessageBox = new( MEMTYPE_MESSAGEBOX ) CMESSAGEBOX() ;
	mMessageLog = new( MEMTYPE_MESSAGELOG ) CMessageLog ;
	mPauseMenu = new (MEMTYPE_MESSAGELOG) CPauseMenu ;

	mHelpTextDisplay = new (MEMTYPE_HELP_TEXT_DISPLAY) CHelpTextDisplay ;
	mLevelBriefingLog = new (MEMTYPE_MESSAGELOG ) CLevelBriefingLog ;
	mRandomStream=new( MEMTYPE_UNKNOWN ) CSeedUniformRandomNumberStream(123456);

	CONSOLE.RegisterCommand("RemoteCameraOn","Set a remote camera on the thing under the crosshairs",&con_remotecameraon);
	CONSOLE.RegisterCommand("RemoteCameraOff","Turn off the remote camera",&con_remotecameraoff);
	CONSOLE.RegisterCommand("Win","Win this level",&con_win);
	CONSOLE.RegisterCommand("Lose","Lose this level",&con_lose);
	CONSOLE.RegisterCommand("Map","Change map",con_map);
	CONSOLE.RegisterCommand("DumpTextures","Dumps all the dynamic textures to disc",con_dumptextures);
#ifdef RESBUILDER
	CONSOLE.RegisterCommand("BuildResources","Builds the level resources",con_buildresources);
#endif
	CONSOLE.RegisterCommand("NavMapOn","Turn the navigation map on",&con_navmapon);
	CONSOLE.RegisterCommand("NavMapOff","Turn the navigation map off",&con_navmapoff);
	CONSOLE.RegisterCommand("ResetMemSizes","Resets the baseline for the memory counters",&con_resetmemsizes);	
	
	CONSOLE.RegisterVariable("cg_horizontalsplitscreen","Is split-screen mode horizontal or vertical",CVar_bool,&mHorizontalSplitscreen);
	CONSOLE.RegisterVariable("cg_interleavedsplitscreen","Is split-screen mode interleaved",CVar_bool,&mInterleavedSplitscreen);
	CONSOLE.RegisterVariable("cg_fullscreenmultiplayer","Show only player 1 fullscreen",CVar_bool,&mFullscreenMultiplayer);
	CONSOLE.RegisterVariable("g_framelength","Game frame/tick length (seconds)",CVar_float,&mFrameLength);
	
	con_resetmemsizes(NULL);

	UpdateMouseCentre(true);	// retail (0046c96f)

	return	TRUE;
}
//******************************************************************************************
void CGame::Shutdown()	// FUNCTION: 0x0046c990
{

	// Stop the music
	if (CLIPARAMS.mMusic)
		MUSIC.Stop();

	HUD.ShutDown();
	GAMEINTERFACE.Shutdown();
	
	PARTICLE_SET.Shutdown();

	STATICSHADOWS.Shutdown();

	// Ensure that we're not about to delete a texture that doesn't exist...
#ifdef _DIRECTX
	SINT	c0;
	for (c0 = 0; c0 < 4; c0 ++)
		LT.D3D_SetTexture(c0, NULL);
//	LT.D3D_SetTexture(0, NULL);
#endif

//	SAFE_RELEASE(mWinScreen);
//	SAFE_RELEASE(mLoseScreen);

//	CONSOLE.SetLoadingFraction(0.2f);

	GenericSPtrSet::ClearAnyDynamicCreatedNodes() ;

//	CONSOLE.SetLoadingFraction(0.3f);

	MEM_MANAGER.SetMerge( FALSE );

	CIMPOSTER::ShutdownAll();

//	CONSOLE.SetLoadingFraction(0.4f);

	ENGINE.ShutDown();

//	CONSOLE.SetLoadingFraction(0.5f);

	MAP.Shutdown();

//	CONSOLE.SetLoadingFraction(0.6f);

	CMESH::FreeLevelResources();
#if TARGET!=PS2
#ifndef EDITORBUILD2
	CVBufTexture::FreeLevelResources();
#endif
#endif

//	CONSOLE.SetLoadingFraction(0.7f);

	//!JCL - why not?
	CTEXTURE::FreeLevelResources();
//	CONSOLE.SetLoadingFraction(0.8f);

	//int bs = MEM_MANAGER.GetDefaultHeap()->FindLargestFree() ;
	//LOG.AddMessage("biggest block = %d", bs) ;

	MEM_MANAGER.SetMerge( TRUE );
	MEM_MANAGER.Cleanup();
//	CONSOLE.SetLoadingFraction(1.0f);

#if TARGET != XBOX
	// Sensible place to do the outro.
	RunOutroFMV();
	
	// and only now do we admit we're loading.
//	CONSOLE.SetLoading(TRUE);
	
//	CONSOLE.SetLoadingFraction(0.0f);	
#endif

	//bs = MEM_MANAGER.GetDefaultHeap()->FindLargestFree() ;
	//LOG.AddMessage("biggest block = %d", bs) ;

	//MEM_MANAGER.OutputBlocks( "sysmem.csv" );
	//MEM_MANAGER.OutputStats( "memstats.txt" );
  //MEM_MANAGER.OutputMap( "memmap.txt" );

	CONSOLE.StatusDone("Freeing Up Level Resources...");
	CONSOLE.Status("Exiting Level...");

	CONSOLE.ClearCommandsAndVariables();

#if TARGET == XBOX
	// This has to happen after the engine shutdown otherwise we won't have texture RAM for it.

	// we'll probably simply take the piss and reboot.
	extern int num_times_cycled;
	if (++num_times_cycled >= CLIPARAMS.mCyclesBeforeReboot)
	{
		// It sometimes takes some time to kill the music loader, so let's ask for it now.
		extern void kill_music_loader();
		kill_music_loader();
		
		// Run the outro right now as it turns out.
		RunOutroFMV();

		// then reboot.
		PLATFORM.Reboot(true);
	}

	// We didn't want to do a reboot, so we'll do a normal level end.
	RunOutroFMV();
	MEM_MANAGER.Cleanup(); // just in case.

	// We admit we're loading here.
	CONSOLE.SetLoading(TRUE);
#endif
}
//******************************************************************************************
void	CGame::ShutdownRestartLoop()	// FUNCTION: 0x0046ca70
{	
	extern bool pause_for_showing_controls;
	if (mQuit == QT_RESTART_LEVEL && PLAYABLE_DEMO)
		pause_for_showing_controls = true;

	// Stop the music
	if (CLIPARAMS.mMusic)
		MUSIC.Stop();

	// we don't need to do this but this makes sure there are no memory leaks incase something did go wrong.
	MISSION_SCRIPT_VM.Clean() ;

	// moving setloading until after the FMV
	// unless we're restarting the level.
	if (mQuit == QT_RESTART_LEVEL) 
		CONSOLE.SetLoading(true);

	pause_for_showing_controls = false;

	CONSOLE.RenderLoadingScreen();

	CONSOLE.Status("Freeing Up Level Resources...");

	IController::Shutdown();

	SAFE_DELETE(mForsetiFearGrid);
	SAFE_DELETE(mMuspellFearGrid);

	for( int i=0; i<MAX_PLAYERS; i++)
		SAFE_DELETE(mController[i]);

	SAFE_DELETE(mMessageLog);
	SAFE_DELETE(mMessageBox);
	SAFE_DELETE(mPauseMenu);
	SAFE_DELETE(mLevelBriefingLog);
	SAFE_DELETE(mRandomStream);
	SAFE_DELETE(mHelpTextDisplay);

#if TARGET!=PS2
	ENGINE.ShutdownRestartLoop();
#endif

#if TARGET==PS2
	ENGINE.ShutdownLevelSpecifics();
#endif

	mLevelLostReason = 0;

	CONSOLE.SetLoadingFraction(0.1f);

	DEBUGMARKERS.Shutdown();

	CONSOLE.SetLoadingFraction(0.2f);

	WORLD.Shutdown();
	SOUND.SetGameSoundsMasterVolume(1.0f);

	CONSOLE.SetLoadingFraction(0.3f);

	// have to do this after the world, because tree destructors edit the structures killed by this call.
	TREES.Shutdown(); // this could probably avoid being called but it's confusing the search for leaks.
	
	CONSOLE.SetLoadingFraction(0.4f);

	MAP_WHO.Clear();

	CONSOLE.SetLoadingFraction(0.5f);

#ifndef EDITORBUILD2
	CAtmospherics::ShutdownAll();
#endif
	
	CONSOLE.SetLoadingFraction(0.6f);

	UPhysicsManager::ClearCachedEmitters();

	CONSOLE.SetLoadingFraction(0.7f);

	PARTICLE_MANAGER.Shutdown();

	CONSOLE.SetLoadingFraction(0.8f);

	SCRIPT_EVENT_NB.Shutdown();

	CONSOLE.SetLoadingFraction(0.9f);

	EVENT_MANAGER.Shutdown();

	CONSOLE.SetLoadingFraction(1.0f);

	ShutdownMouse();	// retail (0046cd1f)
}
//******************************************************************************************
BOOL CGame::LoadResources(	// FUNCTION: 0x0046cd30
	SINT		aLevel,
	BOOL		inLoadedSounds)
{
	CONSOLE.SetLoading( true );
	CONSOLE.RenderLoadingScreen( false );

	if (inLoadedSounds)
		CONSOLE.SetLoadingRange( 50.f, 65.f );
	else
		CONSOLE.SetLoadingRange( 0.f, 30.f );

	// load resource files: 
	if (!CLIPARAMS.mBuildResources)
	{
		// Only load resource files if we're not compiling them!
		CResourceAccumulator::ReadResources(aLevel);
	}
 
	if (inLoadedSounds)
		CONSOLE.SetLoadingRange( 65.f, 75.f );
	else
		CONSOLE.SetLoadingRange( 30.f, 50.f );

	CTEXTURE::LoadLevelResources( aLevel );
	CMESH::LoadLevelResources( aLevel );

	if (!(PARTICLE_SET.LoadAllFromDisk(PARTSET_INGAME)))
		return FALSE;

	UPhysicsManager::InitialiseEffects();

	return TRUE ;
}

//******************************************************************************************
// inits all the resources
void	CGame::InitOneOffResources()
{
	ENGINE.InitResources();
	CONSOLE.SetLoadingFraction(0.33f);
	GAMEINTERFACE.InitResources();
	CONSOLE.SetLoadingFraction(0.66f);
	HUD.InitResources();
	CONSOLE.SetLoadingFraction(1.0f);
}

//******************************************************************************************
// inits all the resources
void	CGame::InitRestartResources()
{
	mMessageBox->InitResources();
	mMessageLog->InitResources() ;
	mPauseMenu->InitResources() ;

}


//******************************************************************************************
BOOL CGame::LoadLevel( SINT aLevel )	// FUNCTION: 0x0046cdf0
{
	SETUP_HISTORY.AddMessage("Game::LoadLevel %d", aLevel);	mCurrentLevel = aLevel;	// retail: logged
	mScorePercentage=0.5f;
	mScore=0;

	CThing::ResetThingCounter();


	LOG.AddMessage("Size of tree = %d", sizeof(CTree));
	LOG.AddMessage("Size of thing = %d", sizeof(CThing));
	LOG.AddMessage("Size of complex thing = %d", sizeof(CComplexThing));
	LOG.AddMessage("Size of CST thing = %d", sizeof(CCollisionSeekingThing));
	LOG.AddMessage("Size of CST Persistent thing = %d", sizeof(CCSPersistentThing));

	if( !WORLD.Load( aLevel ) )
	{
		return FALSE;
	}

	if (WORLD.IsMultiplayer())
		mPlayers=2;
	else
		mPlayers=1;

	SINT	i;

	// just in case there's only one player.
	mController[1] = 0;

	for( i=0; i<mPlayers; i++ )
	{
#line 851
		mPlayer[ i ] = new( MEMTYPE_PLAYER ) CPlayer( i+1 );

		// give each player a controller, as suggested by the frontend
		int port;

		if (i == 0) port = FRONTEND.GetPlayer0ControllerPort();
		else
		{
			// I don't support more than 2 players.
			ASSERT(i == 1);
			port = FRONTEND.mFEPMultiplayerStart.GetPlayer1ControllerPort();

			// this is a hack for people who've gone straight into multiplayer
			// without going via the front end.
			if (port == FRONTEND.GetPlayer0ControllerPort())
			{
				port = FRONTEND.GetPlayer0ControllerPort() ^ 1;
			}
		}
		mController[ i ] = new( MEMTYPE_CONTROLLER ) CCONTROLLER(mPlayer[ i ], port, CAREER.GetControllerConfigurationNum(i));	// retail: no invert-Y argument
	}

	CONSOLE.SetPlayer( mPlayer[ 0 ] ); // Console always tracks player 0

	TREES.Build();

	CONSOLE.RenderLoadingScreen( true ); // Render again to grab screen into fade buffer
	ENGINE.SetBlurAlpha( 255, true );
	
	PLATFORM.FlushInputBuffers(); // clear up keystrokes pressed while loading
/*
	for (int i=0;i<40;i++)
	{
		CInitThing i;
		i.mPos.X=((float) (rand()*100 % 51200))/100.0f;
		i.mPos.Y=((float) (rand()*100 % 51200))/100.0f;
		i.mPos.Z=0;
		CThing *t=SpawnThing(OID_CDebris);
		if (t)
			t->Init(&i);
	}	
*/
	SETUP_HISTORY.AddMessage("G::LL succeeded");	return TRUE;	// retail: logged
}

//******************************************************************************************
BOOL CGame::PostLoadProcess()	// FUNCTION: 0x0046d040
{
#if TARGET == XBOX
	ENGINE.InitDamageSystem();
#endif

	if(!HUD.PostLoadProcess())
		return FALSE;

	CONSOLE.SetLoadingFraction(0.2f);

	// Initialise atmospherics
	CAtmospherics::InitialiseAll();
	CAtmospherics::SetupAll(); // Needs to be done after level load

	CONSOLE.SetLoadingFraction(0.4f);

	// set start positions of players:
	for (int i=0; i<mPlayers; i++ )
	{
		// ok locate players start position
		SPtrSet<CStart>& startNB = WORLD.GetStartNB();
		BOOL			 found = FALSE;

		for( CStart* item = startNB.First();
					 item != NULL;
					 item = startNB.Next() )
		{

			if( item->GetPlayerNumber() == mPlayer[i]->GetNumber() )
			{
				found = TRUE ;
				// assign this player a battle engine
				mPlayer[ i ]->AssignBattleEngine( item->GetPlayerObject() );
			}
		}

		if( found == FALSE )
		{
			LOG.AddMessage( "No start position for player - creating a default one" );
			
			// ok no start position create a default one
			if (CStart *start=(CStart*)SpawnThing(OID_CStart))
			{
				CStartInitThing		it;
				float				xpos=256.0f;
				float				ypos=256.0f;
				
				it.mPos = FVector( xpos, ypos, 0.0f );
				it.mPlayerNumber = mPlayer[ i ]->GetNumber();
				
				start->Init( &it );
				mPlayer[ i ]->AssignBattleEngine( start->GetPlayerObject() );
			}
		}	

		mPlayer[i]->Init() ;
	//	SetCurrentCamera( i, new( MEMTYPE_CAMERA ) CThingCamera( mPlayer[ i ]->GetBattleEngine() ) );
	}

	CONSOLE.SetLoadingFraction(0.6f);

	mPlayer1Lives=2;
	mPlayer2Lives=2;

	// Record/playback only works for player 0 ATM - fixme!
	if (CLIPARAMS.mPlaybackDemo)
		mController[ 0 ]->StartPlayback( CLIPARAMS.mDemoFilename );
	if (CLIPARAMS.mRecordDemo)
		mController[ 0 ]->StartRecording( CLIPARAMS.mDemoFilename );
	
	// Play FMV

#if TARGET==PS2
//	FMV.PlayFullscreen("test");
#endif

	CONSOLE.SetLoadingFraction(0.8f);

	// sort map who for gamut friendly-ness
	MAP_WHO.SortEntries() ;
	MAP.InitQuickCollisionMap();

	CONSOLE.SetLoadingFraction(1.0f);

	return TRUE;
}

//******************************************************************************************	v
void	CGame::SetSlot(int num, BOOL val)	// FUNCTION: 0x0046d3a0
{
	if (num <0 || num >= MAX_CAREER_SLOTS*8)
	{
		LOG.AddMessage("Error: Outside slot range (%d) in call to SetSlot", num) ;
		return ;
	}

	int i = num >> 5 ;
	int b = num & 31 ;

	int m = 1 ;
	if  (b > 0)
	{
		m = m << b;
	}

	if (val == TRUE)
	{
		mSlots[i] = mSlots[i] | m ;
	}
	else
	{
		int temp = 0xffffffff ;
		int temp1 = (~m) & temp ;

		mSlots[i] = mSlots[i] & temp1 ;
	}

}
//******************************************************************************************
BOOL	CGame::GetSlot(int num)	// FUNCTION: 0x0046d410
{
	if (num <0 || num >= MAX_CAREER_SLOTS*8)
	{
		LOG.AddMessage("Error: Outside slot range (%d) in call to GetSlot", num) ;
		return FALSE ;
	}
	int i = num >> 5 ;
	int b = num & 31 ;

	int m = 1 ;
	if  (b > 0)
	{
		m = m << b;
	}

	return ((mSlots[i] & m) != 0) ;

}
//******************************************************************************************
void	CGame::FillOutEndLevelData()	// FUNCTION: 0x0046d470
{
	SPtrSet<CActiveReader<CThing> >& list = WORLD.GetBaseWorldThingNB() ;

	if (list.Size() > BASE_THINGS_EXISTS_SIZE)
	{
		LOG.AddMessage("FATAL ERROR: two many base things trying to be saved (size = %d) max size = %d ", list.Size(), BASE_THINGS_EXISTS_SIZE) ;
	}
	else
	{
	//	LOG.AddMessage("Num base word objs  = %d", list.Size());
		SINT index = 0 ;
		for (index = 0; index < list.Size(); index++)
		{
			CActiveReader<CThing>* item = list.At(index) ;
			BOOL dead = FALSE ;
			if (item->ToRead() && item->ToRead()->IsDying() == FALSE)
			{			
				dead = FALSE ;
				END_LEVEL_DATA.mBaseThingsLeft[index] = TRUE ;
			}
			else
			{
				dead = TRUE ;
				END_LEVEL_DATA.mBaseThingsLeft[index] = FALSE ;
			}

		//	index++;

			char name[256] ;
			strcpy(name,"dead") ;
			if (item->ToRead())
			{
				strcpy(name, item->ToRead()->_GetClassName()) ;
			}

			CThing* t = item->ToRead(); 

			EThingType ty = (EThingType)THING_TYPE_UNIT;
		

			if (dead)
			{
			//	LOG.AddMessage("base world object %d: = %s is DEAD ****", index+1, name) ;
			}
			else
			{
			//	LOG.AddMessage("base world object %d: = %s is alive", index+1, name) ;
			}
		}
	}


	END_LEVEL_DATA.mWorldFinished = mCurrentLevel ;
	END_LEVEL_DATA.mFinalState  = mGameState ;
	END_LEVEL_DATA.mPrimaryObjectives = mPrimaryObjectives;
	END_LEVEL_DATA.mSecondaryObjectives = mSecondaryObjectives;
	END_LEVEL_DATA.mRanking=1.0f;
	END_LEVEL_DATA.mScore=mScore;
	END_LEVEL_DATA.mTimeTaken=EVENT_MANAGER.GetTime();
	END_LEVEL_DATA.mLevelLostReason = mLevelLostReason;
	END_LEVEL_DATA.mSlots = mSlots ;

	int i=0 ;
	CPlayer* player = GetPlayer(0) ;

	if (player)
	{
		for (i=0;i< TK_TOTAL;i++)
		{
			END_LEVEL_DATA.mThingsKilled[i] = player->GetNumEnemyThingKilled((EKilledType)i);
		}
	}
	else
	{
		END_LEVEL_DATA.mThingsKilled.SetAll(0) ;
	}
		
	if ((mPercentageScoreTime-mFullScoreTime)>0)
	{
		// Calculate the time multiplier
		float	scoreTimeMultiplier;

		if (EVENT_MANAGER.GetTime()<mFullScoreTime)
			scoreTimeMultiplier=1.0f;
		else if (EVENT_MANAGER.GetTime()<mPercentageScoreTime)
			scoreTimeMultiplier=-((EVENT_MANAGER.GetTime()-mPercentageScoreTime)/(mPercentageScoreTime-mFullScoreTime))*(1-mScorePercentage)+mScorePercentage;
		else
			scoreTimeMultiplier=mScorePercentage;

		// Clamp the time multiplier to between 1 and 0
		if (scoreTimeMultiplier>1.0f)
			scoreTimeMultiplier=1.0f;
		else if (scoreTimeMultiplier<0.0f)
			scoreTimeMultiplier=0.0f;

		// Calculate the score
		mScore=(SINT)((float)mScore*scoreTimeMultiplier);

		if (mScore>=mSGradeScore)
			END_LEVEL_DATA.mRanking=1.0f;
		else if (mScore<mDGradeScore)
			END_LEVEL_DATA.mRanking=-1.0f;
		else
			END_LEVEL_DATA.mRanking=((float)mScore-(float)mDGradeScore)/((float)mSGradeScore-(float)mDGradeScore);

		// Clamp the ranking to between 0 and 1
		if (END_LEVEL_DATA.mRanking>1.0f)
			END_LEVEL_DATA.mRanking=1.0f;
		else if (END_LEVEL_DATA.mRanking<0.0f)
			END_LEVEL_DATA.mRanking=0.0f;
		else if (END_LEVEL_DATA.mRanking==0.0f)
		{
			// If they are borderline on the last grade then we'll give it to them anyway. 
			END_LEVEL_DATA.mRanking=0.001f;
		}
	}

	if (GetNumSecondaryObjectives())
	{
		if (END_LEVEL_DATA.IsAllSecondaryObjectivesComplete())
		{
			// Don't let them get below a C
			if (END_LEVEL_DATA.mRanking<0.4f)
				END_LEVEL_DATA.mRanking=0.4f;
		}
		else
		{
			// Don't let them get above a B
			if (END_LEVEL_DATA.mRanking>0.6f)
				END_LEVEL_DATA.mRanking=0.6f;
		}
	}
}
// retail: the cutscene's file name, falling back to the small version when the full one is not installed
void MakeCutsceneName(char *name, SINT fmv)	// FUNCTION: 0x0046d810
{
	char vid[300];

	sprintf(name, "cutscenes\\%02d", fmv);
	sprintf(vid, "data\\video\\%s.vid", name);
	FILE *f = fopen(vid, "rb");
	if (f)
		fclose(f);
	else
		sprintf(name, "cutscenes\\%02d_small", fmv);
}
//******************************************************************************************
SINT	CGame::GetIntroFMV()
{
	if (!mFirstTimeRound)
		return -1;

	if (CLIPARAMS.mSkipFMV)
		return -1;

	if (PLAYABLE_DEMO == 1)	// retail: no cutscenes in the playable demo (Stuart: E3BUILD)
		return -1;


	if ((performing_stress_test == 1) || (performing_stress_test == 2))
		return -1;

	return lookup_FMV(mCurrentLevel, 0);
}

//******************************************************************************************
void	CGame::RunIntroFMV()	// FUNCTION: 0x0046d890
{
	SINT	fmv = GetIntroFMV();

	if (fmv != -1)
	{
		char foo[300];

		MakeCutsceneName(foo, fmv);	// retail

		// Set this FMV as being unlocked in the goodies

		int fmvgn=fmv+200;

		if (fmv==33)
			fmvgn=232;

		if ((CAREER.GetGoodieState(fmvgn)!=GS_OLD) && (CAREER.GetGoodieState(fmvgn)!=GS_NEW))
		{
			CAREER.SetGoodieState(fmvgn,GS_NEW);
			CAREER.mPendingExtraGoodies ++;
		}

		BOOL localise=TRUE;

		if ((fmv==9) || (fmv==12) || (fmv==13) || (fmv==14) || (fmv==15) || (fmv==16) || (fmv==28) || (fmv==31))
			localise=FALSE; // These need no localisation (retail: 28 and 31 too)

		FMV.PlayFullscreen(foo, FALSE, localise);
		PLATFORM.FlushInputBuffers(); // clear up keystrokes pressed while loading
	}
}

//******************************************************************************************
void	CGame::RunOutroFMV()	// FUNCTION: 0x0046d9f0
{
#ifdef E3BUILD
	return;
#endif

	if ((performing_stress_test == 1) || (performing_stress_test == 2))
		return;

	// did I win?
	if ((mQuit == QT_QUIT_TO_FRONTEND) && (END_LEVEL_DATA.mFinalState == GAME_STATE_LEVEL_WON))
	{
		SINT	fmv_a = lookup_FMV(mCurrentLevel, 1);
		SINT	fmv_b = lookup_FMV(mCurrentLevel, 2);

		SINT	fmv = fmv_a;

		if (mCurrentLevel == 500)
		{
			// play fmv b when the rocket has been blown up
			if (GetSlot(SLOT_500_ROCKET) == TRUE)
			{
				fmv = fmv_b;
			}
		}

		if (mCurrentLevel == 720)
		{
			if (END_LEVEL_DATA.IsAllSecondaryObjectivesComplete())
			{
				fmv = fmv_b;
			}
		}


		if (fmv != -1)
		{
			char foo[300];

			MakeCutsceneName(foo, fmv);	// retail

			int fmvgn=fmv+200;
			
			if (fmv==33)
				fmvgn=232;			

			if ((CAREER.GetGoodieState(fmvgn)!=GS_OLD) && (CAREER.GetGoodieState(fmvgn)!=GS_NEW))
			{
				CAREER.SetGoodieState(fmvgn,GS_NEW);
				CAREER.mPendingExtraGoodies ++;
			}

			BOOL localise=TRUE;
			
			if ((fmv==9) || (fmv==12) || (fmv==13) || (fmv==14) || (fmv==15) || (fmv==16))
				localise=FALSE; // These need no localisation			

			FMV.PlayFullscreen(foo, FALSE, localise);
			PLATFORM.FlushInputBuffers(); // clear up keystrokes pressed while loading
		}

/*		if (mCurrentLevel == 741)
		{
			// play the outro too
			if (fmv_b != -1)
			{
				char foo[300];

				sprintf(foo, "cutscenes\\%02d", fmv_b);

				FMV.PlayFullscreen(foo, FALSE, TRUE);
				PLATFORM.FlushInputBuffers(); // clear up keystrokes pressed while loading
			}
		}*/

		if ((mCurrentLevel == 741) || (mCurrentLevel == 800))
			RollCredits();
	}
}

//******************************************************************************************

class CWaitForStart: public IController	// FUNCTION: 0x0046dbd0 ??0CWaitForStart@@QAE@XZ
{
	void ReceiveButtonAction(CController* from_controller, int button, float val)	// FUNCTION: 0x0046dbe0
	{
#if TARGET==XBOX
		if (button == BUTTON_FRONTEND_MENU_SELECT)
#else
		if ((button == BUTTON_START) || (button == BUTTON_MECH_FIRE_GUN_POD))	// retail: 0046dbe6 cmp 0x40, 0046dbeb cmp 0x12
#endif
		{
			// He's happy to start off again, because otherwise we wouldn't get this button event would we?
			from_controller->RelinquishControl();
		}
	}
	
	BOOL	CanBeControlledWhenInPause() {return TRUE;}
	EControlType    GetControlType() {return CONTROL_MECH;}
	
} wait_for_start;
// FUNCTION: 0x0046db90 init:wait_for_start
// FUNCTION: 0x0046dbb0 exit:wait_for_start
// FUNCTION: 0x0046dbc0 ??1CWaitForStart@@UAE@XZ
char	gShowUnfinishedWatermark = 0;						// 00679ec0; = 0 and defined here, before gAllCheatsEnabled: retail's .bss order (placement evidence)
//******************************************************************************************
// Retail addition (the sound-option pages restart the level's music through it: 004cf0d2 and three more)
char	gAllCheatsEnabled = 0;			// 00679ec1: retail; -autoconfigtest's end turns every cheat on (FrontEnd.cpp reads it)
void	CGame::PlayMusicForCurrentLevel()	// FUNCTION: 0x0046dc00
{
	if (CLIPARAMS.mMusic)
	{
		switch (mCurrentLevel)
		{
		case 100:
			MUSIC.PlaySelection(MUS_TUTORIAL);
			break;
		default:
			MUSIC.PlaySelection(MUS_INGAME);
			break;
		};
	}
}

//******************************************************************************************

EQuitType	CGame::RestartLoopRunLevel(SINT aLevel)	// FUNCTION: 0x0046dc30
{
	if( !mFirstTimeRound )
		CONSOLE.SetLoadingRange( 0.f, 80.f );

	// load world file
	if( !LoadLevel( aLevel ) )
	{
		ShutdownRestartLoop();
		return QT_LOAD_ERROR;
	}

	if ((GetIntroFMV()==-1) || PLAYABLE_DEMO)	// retail: the demo shows the controls screen instead (0046dccd)
		CONSOLE.SetLoadingRange( 80.f, 90.f );
	else
		CONSOLE.SetLoadingRange( 80.f, 100.f );

	if( !PostLoadProcess() )
	{
		ShutdownRestartLoop();
		return QT_LOAD_ERROR;
	}


	static int lTime = 0;
	char buf[256];
	sprintf(buf,"Post Load %d",lTime++);
	MEM_MANAGER.DumpMemory(buf);	// retail (0046dd37)

	if (GetIntroFMV()!=-1)
	{
		CONSOLE.SetLoadingFraction(1.f);
		CONSOLE.SetLoading(FALSE);

		RunIntroFMV();

#if TARGET==XBOX
		// and if we're here, we haven't initted the landscape, because we
		// put it in the memory freed after FMV playing.
		ENGINE.GetLandscape()->Init(ENGINE.GetMapTex());
#endif

		CONSOLE.SetLoadingRange( 0.f, 100.f );	// retail: no SetLoading(TRUE) here (0046dd9a)
	}
	else if (!PLAYABLE_DEMO)
		CONSOLE.SetLoadingRange( 90.f, 100.f );
	else
		CONSOLE.SetLoadingRange( 100.f, 100.f );

	STATICSHADOWS.Reattach();
	
	CONSOLE.SetLoadingFraction(0.2f);

#if TARGET==PS2
	ENGINE.BuildLevelSpecifics();
#else
	if (mFirstTimeRound)
	{
		ENGINE.BuildLevelSpecifics();
	}
#endif

	CONSOLE.SetLoadingFraction(0.4f);

	mBaseTime = PLATFORM.GetSysTimeFloat(); 

	CalcRenderFrameFraction();

	// Hackish
#ifdef DEBUG_TIMERECORDS
	mCurrentTR=0;
	mCurrentRR=0;
#endif
	mLastRenderLength=0;
	mEndLevelCount = -1.0f;
	
	mLastRenderFrameTime=-1.0f;
	mRenderFrameLength = 1.f;
	mUpdateFrameNumber=0;

	CONSOLE.RegisterCommand("dumptimerecords","Dump the game time records",&con_dumptimerecords);

	mWinLoseScreenAlpha=0;

	if (mFirstInit)
	{
		mFirstInit=false;
	}	

	CONSOLE.SetLoadingFraction(0.6f);

	// Execute startup script (on every level load... ^-^;;)
	CONSOLE.Exec("autoexec.con");	

	CONSOLE.SetLoadingFraction(0.7f);

	// clean up from horible load process
	MEM_MANAGER.Cleanup();
	SASSERT( MEM_MANAGER.Validate(), "Invalid Memory Manager!" );

	CONSOLE.SetLoadingFraction(0.8f);

	if (PLAYABLE_DEMO && !CLIPARAMS.mBuildResources)
	{
		// Let's show the loading screen forever, because it's actually the controls screen.
		extern bool pause_for_showing_controls;
		pause_for_showing_controls = true;

		CONSOLE.SetLoadingRange( 100.f, 100.f );	// retail: Stuart's Xbox line (0046df3d)
		
		CONSOLE.RenderLoadingScreen(false, true);

		// And we have to wait for someone to press START.
		CWaitForStart wait_for_start;

		mController[0]->SetToControl(&wait_for_start);

		float old_time = PLATFORM.GetSysTimeFloat();

		extern bool draw_string_at_bottom;

		// we're back in an interactive bit (though we're loading, or something)
		CController::SetNonInteractiveSection(false);

		do
		{
			PLATFORM.Process();
			mController[0]->Flush();

			if (old_time < PLATFORM.GetSysTimeFloat() - 1.f)
			{
				draw_string_at_bottom ^= true;
				CONSOLE.RenderLoadingScreen(false, true);

				old_time = PLATFORM.GetSysTimeFloat();
			}
		}
#if TARGET==PS2
		while (mController[0]->GetToControl() == &wait_for_start && !CCONTROLLER::InactivityMeansQuitGame() && (!PLATFORM.HasTimeoutExpired()));
#else
		while (mController[0]->GetToControl() == &wait_for_start && !CCONTROLLER::InactivityMeansQuitGame());
#endif

		// and make sure we end up drawing the string again, and the right one at that.
		draw_string_at_bottom = true;
		pause_for_showing_controls = false;
		mController[0]->Flush();	// retail (0046e021)
	}

	PreRun() ;

	if (!PLAYABLE_DEMO)
		CONSOLE.SetLoadingFraction(1.0f);

	// massive hack to play looping samples after we pre run (can't do this in event manager cos when we init the
	// unit we haven't loaded in the time for pre-run yet !!

	ListIterator<CUnit> unit_iterater(&WORLD.GetUnitNB());
	CUnit* unit;
	FOR_ALL_ITEMS_IN(unit_iterater,unit)
	{
		unit->StartPlayingInitNoise() ;
	}

/*
	static int lRun = 0;
	char buf[16];
	sprintf(buf,"Run %d",lRun);
//	MEM_MANAGER.DumpMemory(buf);
	lRun++;
	MEM_MANAGER.DumpMemory(buf);
	lRun++;*/

	PlayMusicForCurrentLevel();	// retail (inlined, 0046e09e)

	CONSOLE.SetLoading(FALSE);

	while( mQuit == QT_NONE )
	{
		MainLoop();

		//JCL - stress test
		if (performing_stress_test > 0)
		{
			for (int i=0;i<mPlayers;i++)
				mPlayer[i]->SetIsGod(TRUE);

			switch (performing_stress_test)
			{
			default:
			case 1:
				if (EVENT_MANAGER.GetTime() > 30.f)
				{
					mQuit = QT_QUIT_TO_FRONTEND;
					mGameState = GAME_STATE_LEVEL_WON;
				}
				break;
			case 2:
				if (EVENT_MANAGER.GetTime() > 2.f)
				{
					mQuit = QT_QUIT_TO_FRONTEND;
					mGameState = GAME_STATE_LEVEL_WON;
				}
				break;
			case 3:
				if ((EVENT_MANAGER.GetTime() > 120.f) || (mGameState == GAME_STATE_LEVEL_LOST))
				{
					mQuit = QT_QUIT_TO_FRONTEND;
					mGameState = GAME_STATE_LEVEL_WON;
				}
				break;
			case 4:
				if ((EVENT_MANAGER.GetTime() > 120.f) || (mGameState == GAME_STATE_LEVEL_LOST))
				{
					mQuit = QT_QUIT_TO_FRONTEND;
					mGameState = GAME_STATE_LEVEL_WON;
				}
				break;
			case 5:
				if ((EVENT_MANAGER.GetTime() > 5.0f) || (mGameState == GAME_STATE_LEVEL_LOST))
				{
					mQuit = QT_QUIT_TO_FRONTEND;
					mGameState = GAME_STATE_LEVEL_WON;
				}
				break;			
			};
		}

		// JCL - timeout?
#ifdef E3BUILD
		if (PLATFORM.GetSysTimeFloat() - mPlayer[0]->GetTimeoutTime() > 120.f)
		{
			mQuit = QT_QUIT_TIMEOUT;
			mGameState = GAME_STATE_LEVEL_LOST;
		}

#endif


	}

#if TARGET == XBOX 
	
	int jj =0;
	//finish playing select sound
	for (jj=0;jj<10;jj++)
	{
		#if TARGET==PS2
			PLATFORM.Sleep(100);
		#else
			Sleep(100);
		#endif
		SOUND.UpdateStatus() ;
		MUSIC.UpdateStatus() ;
	}
	SOUND.KillAllSamples();



#else

	SOUND.KillAllSamples();

	if (mQuit == QT_USER_QUIT_TO_FRONTEND || mQuit == QT_RESTART_LEVEL || mQuit == QT_USER_QUIT_TO_TITLE_SCREEN)
	{
		CFrontEnd::PlaySound(FES_SELECT);	// retail (0046e1b7)

		// Just to ensure it gets allocated a channel OK

		SOUND.UpdateStatus();	// retail: once (0046e1c4)
	//	SOUND.HandleStreams();
	//	PLATFORM.Sleep(10);
	}
#endif

	// out fill out structure before we start deleting things
	FillOutEndLevelData() ;

	// let's clear a few things out.
	CAtmospherics::ShutdownAll();

	// get rid of dynamic textures so we know what's going on.
#if TARGET != PS2
	CVBufTexture::ClearOut();
#endif

	for( int i=0; i<MAX_PLAYERS; i++)
	{
		SAFE_DELETE(mPlayer[i]);
		SAFE_DELETE(mCurrentCamera[i]);
	}

	return mQuit ;
}


//******************************************************************************************
EQuitType	CGame::RunLevel(SINT aLevel)	// FUNCTION: 0x0046e240
{
	mRestarting = FALSE ;
	extern bool pause_for_showing_controls;
	if (PLAYABLE_DEMO)
		pause_for_showing_controls = true;

	CONSOLE.SetLoading(true);

	pause_for_showing_controls = false;

	// Stop the music
	if (CLIPARAMS.mMusic)
		MUSIC.Stop();	

	BOOL	reloadedSounds=FALSE;

#if TARGET == PC
	// retail: the PC reloads the language's sample bank (0046e283)
	if (CLIPARAMS.mSound)
		SOUND.ReloadLanguageSampleBank();
#else
	// Let's get the sounds for the level
	if (CLIPARAMS.mSound)
	{
#if TARGET==XBOX
		SOUND.LoadXAPFile();
#elif TARGET==PS2
		if (!SOUND.LoadedCurrentLanguage())
		{
			reloadedSounds=TRUE;
			CONSOLE.SetLoadingRange(0.0f,50.0);
			SOUND.LoadAllSounds();
		}
#endif
	}
#endif

	mFirstTimeRound = TRUE;
	
	if( !Init() )
	{
		Shutdown();
		return QT_LOAD_ERROR;
	}	

	mCurrentlyRunningLevel = aLevel ;
	//!JCL??!!
//	LoadResources(aLevel) ;

	if (!InitRestartLoop())
	{
		ShutdownRestartLoop();
		return QT_LOAD_ERROR;
	}
	
	WORLD.ResetLoadedState();

	if (!LoadResources(aLevel,reloadedSounds))
	{
		// Oh dear, something's wrong.
		CONSOLE.FatalFileError("game resources", -1);	// retail (0046e30c)
	}

	if (reloadedSounds)
		CONSOLE.SetLoadingRange(75.0f, 77.5f);
	else
		CONSOLE.SetLoadingRange(50.0f, 65.0f);

	InitOneOffResources();

#ifdef E3BUILD
	static int hack_to_avoid_vb_leaks_on_retry_level;

	hack_to_avoid_vb_leaks_on_retry_level = 0;
#endif

	BOOL play_level = TRUE ;

	if (reloadedSounds)
		CONSOLE.SetLoadingRange(77.5f, 80.0f);
	else
		CONSOLE.SetLoadingRange(65.0f, 80.0f);

	SINT		restartStressTestCount=0;

	while (play_level == TRUE)
	{
		InitRestartResources();
		mQuit = RestartLoopRunLevel(aLevel) ;
		ShutdownRestartLoop();

		if (performing_stress_test==4)
		{
			if (restartStressTestCount<5)
			{
				mQuit = QT_RESTART_LEVEL;
				restartStressTestCount++;
			}
		}

		if (mQuit == QT_RESTART_LEVEL)
		{
			mRestarting = TRUE ;
			MUSIC.Stop();
#ifdef E3BUILD
			hack_to_avoid_vb_leaks_on_retry_level ++;
			if (hack_to_avoid_vb_leaks_on_retry_level > 10)
				play_level = FALSE;
			else
#endif
			if (!InitRestartLoop())
			{
				ShutdownRestartLoop();
				play_level = FALSE ;
			}
		}
		else if (mQuit == QT_LOAD_ERROR)
		{
			CONSOLE.FatalError(FALSE, 0xf5, -1);	// retail (0046e443)
		}
		else
		{
			play_level=FALSE;
		}
		
		mFirstTimeRound=FALSE;
	} 

	mRestarting=FALSE;
	CONSOLE.SetLoadingRange(0.0f,50.0f);
	Shutdown();

	return mQuit;
}
void CGame::Render( SINT num_renders )	// FUNCTION: 0x0046e460
{
	// retail: with the extra GeForce FX features on, a single-player frame first renders the landscape's reflection
	// into its texture at 256x256, then renders again to the screen (0046e4bc..0046e4f2, 0046e8c5)
	CRenderTargetSwap	mirror;

	// Update time
	float new_time=(EVENT_MANAGER.GetTime()+(GetFrameRenderFraction()*CLOCK_TICK))/CLOCK_TICK;

	if (!IsMultiplayer() && EXTRA_GEFORCE_FX_FEATURES.mValue && ENGINE.GetLandscape() &&
		ENGINE.GetLandscape()->mMirrorTexture && !gMirrorPass)
	{
		if (mirror.Begin(ENGINE.GetLandscape()->mMirrorTexture))
		{
			gMirrorPass = true;
			FORCE_OBJECT_IMPOSTERS = true;
		}
	}

	if (mLastRenderFrameTime != -1.f)
		mRenderFrameLength = new_time - mLastRenderFrameTime;
	else
		mRenderFrameLength = 1.f;

	mLastRenderFrameTime = new_time;

	BOOL	again;
	do
	{
		again = FALSE;

		// Update particle system
		// Moved to engine.
	/*	if (!IsPaused())
		{
			if (!ENGINE.DebugNoParticles())
				PARTICLE_MANAGER.Process(mRenderFrameLength,(num_renders==0));
		}
	*/
		CViewport fullscreen;
		CViewport viewport;

		fullscreen.Width=gMirrorPass ? 256 : PLATFORM.GetWindowWidth();
		fullscreen.Height=gMirrorPass ? 256 : PLATFORM.GetWindowHeight();
		fullscreen.X=0;
		fullscreen.Y=0;
		fullscreen.MinZ=0.0f;
		fullscreen.MaxZ=1.0f;

		// Allow game to do pre-rendering stuff
		mRenderFrameNumber++ ;

		mIsFirstFrame = (num_renders == 0);

		GAME.PreRender();

	#ifdef DEBUG_TIMERECORDS
		mRenderRecord[mCurrentRR].renderstarttime=PLATFORM.GetSysTimeFloat();			
		mRenderRecord[mCurrentRR].renderframetime=mFrameTime;
		mRenderRecord[mCurrentRR].rendernumber=num_renders;
	#endif

		if (!mFullscreenMultiplayer)
			ENGINE.SetNumViewpoints(mPlayers);
		else
			ENGINE.SetNumViewpoints(1);

		ENGINE.PreRender(&fullscreen);

		if (mWinLoseScreenAlpha != 255)	// retail: nothing behind an opaque win/lose screen (0046e5c8)
		{
		if ((mPlayers==1) || (mFullscreenMultiplayer))
		{
			// Fullscreen singleplayer
			viewport.Width=fullscreen.Width;
			viewport.Height=fullscreen.Height;
			viewport.X=0;
			viewport.Y=0;
			viewport.MinZ=0.0f;
			viewport.MaxZ=1.0f;
			// retail: the mouse steers the walker's look (0046e848)
			PollMouseMotion();
			if (mPlayer[0]->GetBattleEngine())
				mPlayer[0]->GetBattleEngine()->UpdateMouseLookAngles();
			ENGINE.SetViewpoint(0,GetCamera(0),&viewport,GetPlayer(0));
			ENGINE.Render(0);
		}
		else if (mPlayers==2)
		{
			// Twoplayer splitscreen

			if (mHorizontalSplitscreen)
			{
				viewport.Width=fullscreen.Width;
				viewport.Height=fullscreen.Height/2;
			}
			else
			{
				viewport.Width=fullscreen.Width/2;
				viewport.Height=fullscreen.Height;
			}
			viewport.X=0;
			viewport.Y=0;
			viewport.MinZ=0.0f;
			viewport.MaxZ=1.0f;
			PollMouseMotion();	// retail (0046e63e)
			if (mPlayer[0]->GetBattleEngine())
				mPlayer[0]->GetBattleEngine()->UpdateMouseLookAngles();
			if (mPlayer[1]->GetBattleEngine())
				mPlayer[1]->GetBattleEngine()->UpdateMouseLookAngles();
			ENGINE.SetViewpoint(0,GetCamera(0),&viewport,GetPlayer(0));
			ENGINE.Render(0);
			if (mHorizontalSplitscreen)
				viewport.Y+=fullscreen.Height/2;
			else
				viewport.X+=fullscreen.Width/2;
			ENGINE.SetViewpoint(1,GetCamera(1),&viewport,GetPlayer(1));
			ENGINE.Render(1);
		}
		else if ((mPlayers==3) || (mPlayers==4))
		{
			// 3/4 player quad split
		
			viewport.Width=fullscreen.Width/2;
			viewport.Height=fullscreen.Height/2;
			viewport.X=0;
			viewport.Y=0;
			viewport.MinZ=0.0f;
			viewport.MaxZ=1.0f;
			ENGINE.SetViewpoint(0,GetCamera(0),&viewport,GetPlayer(0));				
			ENGINE.Render(0);
			viewport.X+=fullscreen.Width/2;
			ENGINE.SetViewpoint(1,GetCamera(1),&viewport,GetPlayer(1));				
			ENGINE.Render(1);
			viewport.X-=fullscreen.Width/2;
			viewport.Y+=fullscreen.Height/2;
			ENGINE.SetViewpoint(2,GetCamera(2),&viewport,GetPlayer(2));				
			ENGINE.Render(2);
			if (mPlayers==4)
			{
				viewport.X+=fullscreen.Width/2;
				ENGINE.SetViewpoint(3,GetCamera(3),&viewport,GetPlayer(3));					
				ENGINE.Render(3);
			}
		}
		else
		{
			ASSERT(0); // Eek! Unknown number of players!
		}
		}

		ENGINE.PostRender(&fullscreen);

		// We have to draw the "help your controller's unplugged" messages on top of everything.
		RECONNECT_INTERFACE[0].Render(ENGINE.GetViewportForViewpoint(0), mController[0], FALSE);
		RECONNECT_INTERFACE[1].Render(ENGINE.GetViewportForViewpoint(1), mController[1], FALSE);

		if (gMirrorPass)
		{
			gMirrorPass = false;
			FORCE_OBJECT_IMPOSTERS = false;
			mirror.End();
			again = TRUE;
		}
	} while (again);

#if TARGET == PS2
	PLATFORM.EndScene();
#endif
}

//******************************************************************************************


void CGame::Update()	// FUNCTION: 0x0046e910
{
/*#if TARGET == PS2
#ifndef _DEBUG
	extern UINT g_OldSP;
	extern UINT g_OldFP;
	extern void * g_pSPR;

	g_pSPR = SCRATCHPADMANAGER.Alloc( 12 * 1024 );
	UBYTE * lpSPREnd = (UBYTE*)g_pSPR + 12*1024 - 0x240;

	asm __volatile__
	(
	"
		sw		$sp,0x0(%0)		\n
		sw		$fp,0x0(%1)		\n
		addiu	$sp,%2,0		\n
		addiu	$fp,%2,0		\n
	"
	: 
	: "r" (&g_OldSP), "r" (&g_OldFP), "r" (lpSPREnd) 
	: "$sp", "$fp"
	);
#endif
#endif*/

	mUpdateFrameNumber++;

	// retail: -autoconfigtest drives the pause menu and takes screen dumps on fixed frames (0046e921)
	if (CLIPARAMS.mAutoConfigTest)
	{
		static int lFrameOffset = 0;

		switch (mUpdateFrameNumber + lFrameOffset)
		{
		case 240:
			RequestScreenDump(NULL, false);
		case 10100:
			mPlayer[0]->ReceiveButtonAction(mController[0], BUTTON_PAUSE, 1.f);
		case 260:
		case 270:
		case 10110:
			mPauseMenu->ReceiveButtonAction(mController[0], BUTTON_FRONTEND_MENU_UP, 1.f);
			break;
		case 10150:
			{
				RECT	area;
				area.top = 112;
				area.bottom = 368;
				area.left = 192;
				area.right = 448;
				RequestScreenDump(&area, false);
			}
		case 290:
			mPauseMenu->ReceiveButtonAction(mController[0], BUTTON_PAUSE_MENU_SELECT, 1.f);
			break;
		case 300:
		case 10160:
			mPauseMenu->mConfirm->ReceiveButtonAction(mController[0], BUTTON_FRONTEND_MENU_DOWN, 1.f);
			break;
		case 310:
		case 10170:
			mPauseMenu->mConfirm->ReceiveButtonAction(mController[0], BUTTON_PAUSE_MENU_SELECT, 1.f);
			lFrameOffset += 10000;
			break;
		case 20100:
			CLIPARAMS.mAutoConfigTest = FALSE;
			gAllCheatsEnabled = TRUE;
			break;
		}
	}

	// JCL - don't ask.
	// Oh, okay - it's to do with trying to make the free-cam interpolate properly...
	CControllableCamera::UpdateFrameCount();

	mMessageBox->PlayPendingSamples();

	// If it weren't for this "if" statement, pulling out a controller during the load screen
	// would put up the "please reinsert" interface but wouldn't pause the game, because pausing
	// is disabled during PRE_RUNNING
	if (mGameState > GAME_STATE_PRE_RUNNING)
	{
		bool something_reconnected = false;

		for (int i=0;i<mPlayers;i++)
		{
			if (!mController[i]) continue;

			if (RECONNECT_INTERFACE[i].Process(mController[i], true))
				something_reconnected = true;
		}

		// If we've just reconnected something, we could unpause the game. Unless we're in the pause menu of course.
		if (something_reconnected && mController[0]->GetToControl() == mPlayer[0] && IsPaused() &&
			RECONNECT_INTERFACE[0].GetState() == CReconnectInterface::OK &&
			RECONNECT_INTERFACE[1].GetState() == CReconnectInterface::OK)
		{
			if (GAME.IsMultiplayer() )
			{
				if (mController[1]->GetToControl() == mPlayer[1])
				{
					// there you go!
					mPause = FALSE;
				}
			}
			else
			{
				mPause = FALSE;
			}
		}
		
		// Update the unit guide update counter
		CUnit::mGuideUpdateCounter=mUpdateFrameNumber % 8;
	}

	if (((mPause == FALSE || mAdvanceFrame == TRUE)) && (!CLIPARAMS.mBuildResources))
	{
/*		static bool onlyonce = true;
		if (EVENT_MANAGER.GetTime() > 15.f && onlyonce)
		{
			Pause();
			onlyonce = false;
		}*/


		PROFILE_START(TotalEventManager);
		EVENT_MANAGER.AdvanceTime() ;

		PollMouseState();	// retail (0046eb62)

		// ensures that control events to battle engine happen at the start of the frame
		for (int i=0;i<mPlayers;i++)
		{
			if (mController[i])
			{
				// retail: a walker on the ground locks the mouse's pitch axes (0046eb83)
				if (mPlayer[i]->GetBattleEngine())
					gMouseAxisLocked[i] = mPlayer[i]->GetBattleEngine()->IsWalkerOnGroundOrObject();
				mController[i]->Flush();
			}
		}

		// retail (0046ebac)
		if (!GAMEINTERFACE.IsMenuUp())
			UpdateMouseCentre(false);
		LT.ClearKeyWasUp();

		EVENT_MANAGER.Flush() ;
		PROFILE_END(TotalEventManager) ;
		CAtmospherics::ProcessAll();
	}
	else if (mGameState <= GAME_STATE_QUIT)
	{
		for (int i=0;i<mPlayers;i++)
		{
			if (mController[i])
				mController[i]->Flush();
		}
	}

	if (mPausedAllGameSounds && IsPaused()== FALSE)
	{
		SOUND.UnPauseAllSamples() ;
		mPausedAllGameSounds= FALSE ;
	}

	if (mPausedAllGameSounds==FALSE && IsPaused()== TRUE)
	{
		SOUND.PauseAllSamples() ;
		mPausedAllGameSounds = TRUE ;
	}
	
	/*
#if TARGET==XBOX
	if (mRenderFrameNumber==5) // Wait to ensure imposters have been created, etc
	{
		TRACE("Doing memory dump!\n");
		MEM_MANAGER.DumpMemory("XBTest");
		TRACE("...done\n");
	}
#endif
	*/
#ifdef RESBUILDER
	
	if (CLIPARAMS.mBuildResources)
	{
		if (mRenderFrameNumber>4) // Wait to ensure imposters have been created, etc
		{
			CONSOLE.SetLoading(TRUE);
			CONSOLE.Status("Building resource files");			
			if (CLIPARAMS.mBuildPCResources)
			{
				CONSOLE.StatusProgress("Building resource files","PC");
				BuildResources(mCurrentLevel,PC);
			}
			if (CLIPARAMS.mBuildPS2Resources)
			{
				CONSOLE.StatusProgress("Building resource files","PS2");
				BuildResources(mCurrentLevel,PS2);
			}
			if (CLIPARAMS.mBuildXBOXResources)
			{
				CONSOLE.StatusProgress("Building resource files","XBOX");
				BuildResources(mCurrentLevel,XBOX);
			}
			CONSOLE.StatusDone("Building resource files");
			CONSOLE.SetLoading(FALSE);
			SetQuit(QT_QUIT_TO_SYSTEM);
		}
	}
#endif	
	mAdvanceFrame = FALSE ;

	// can't use event manager to wait 10 seconds becuase we are paused when level is finished
	if (mGameState == GAME_STATE_LEVEL_WON && mEndLevelCount >=0)
	{
		mEndLevelCount-=CLOCK_TICK ;

		// retail: the count holds while a controller is disconnected (0046ec88)
		if ((mEndLevelCount <= CLOCK_TICK) &&
			((RECONNECT_INTERFACE[0].GetState() != CReconnectInterface::OK) ||
			 (GAME.IsMultiplayer() && (RECONNECT_INTERFACE[1].GetState() != CReconnectInterface::OK))))
			mEndLevelCount = CLOCK_TICK;

		if (mCurrentLevel == 741 || mCurrentLevel == 742 || mEndLevelCount < 0)
		{
			SetQuit( QT_QUIT_TO_FRONTEND );
		}
	}

		// can't use event manager to wait 10 seconds becuase we are paused when level is finished
	if (mGameState == GAME_STATE_LEVEL_LOST || 
		mGameState == GAME_STATE_PLAYER_1_WON ||
		mGameState == GAME_STATE_PLAYER_2_WON ||
		mGameState == GAME_STATE_GAME_DRAWN)
	{
		if (mEndLevelCount > 0.15f)
		{
			// retail: the count follows the system clock, a step of under 5 seconds at a time, stopping a tick
			// above the fade (0046ecfe; the constant at 005dbbf4 is 0.15f - CLOCK_TICK folded in float)
			static float lLastTime = 0.f;
			float	time = PLATFORM.GetSysTimeFloat();

			if (lLastTime < time)
			{
				float	delta = time - lLastTime;

				if ((delta < 5.f) && (mEndLevelCount - delta > 0.15f - CLOCK_TICK))
					mEndLevelCount -= delta;
				else
					mEndLevelCount-=CLOCK_TICK ;
			}
			else
				mEndLevelCount-=CLOCK_TICK ;

			lLastTime = time;
		}
		else
		{
			// only do when when not paused ?
			if (mPause == FALSE)
			{
				mFadeOut = TRUE ;
			}
		}

		if (mEndLevelCount <=(GAME_COUNT_WHEN_LOST_OR_DRAW- 0.5f))
		{
			if (!GAMEINTERFACE.IsMenuUp())
			{
				GAMEINTERFACE.SetNotAvailable(0);
				GAMEINTERFACE.SetNotAvailable(1);
				GAMEINTERFACE.SetNotAvailable(2);
				GAMEINTERFACE.SetNotAvailable(5);
				GAMEINTERFACE.SetRenderBackgroud(false);
				GAMEINTERFACE.SetYBoxoffset(70);
				if (IsFreeCameraOn(0)==TRUE) ToggleFreeCameraOff(0) ;
				// retail: a reconnection prompt stays above the menu (0046edf3)
				if (mController[0]->GetToControl() == &RECONNECT_INTERFACE[0])
				{
					mController[0]->RelinquishControl();
					mController[0]->SetToControl(&GAMEINTERFACE) ;
					mController[0]->SetToControl(&RECONNECT_INTERFACE[0]);
				}
				else if (mController[0]->GetToControl() == &RECONNECT_INTERFACE[1])
				{
					mController[0]->RelinquishControl();
					mController[0]->SetToControl(&GAMEINTERFACE) ;
					mController[0]->SetToControl(&RECONNECT_INTERFACE[1]);
				}
				else
					mController[0]->SetToControl(&GAMEINTERFACE) ;
				GAMEINTERFACE.ToggleMenuDisplay() ;
			}
		}
	}

/*#if TARGET == PS2
#ifndef _DEBUG		
	asm __volatile__
	(
	"
		addiu	$sp,%0,0		\n
		addiu	$fp,%1,0		\n
	"
	: 
	: "r" (g_OldSP), "r" (g_OldFP)
	: "$sp", "$fp"
	);

	SCRATCHPADMANAGER.Free( g_pSPR );
	g_pSPR = 0;
#endif
#endif*/
}

//******************************************************************************************
void	CGame::PreRun()
{
	if (!CLIPARAMS.mBuildResources)
	{
		// runs the game for a defined time ( in mPreRunTime attribute )
		while (GAME.GetGameState() == GAME_STATE_PRE_RUNNING)
		{
			Update() ;
		}
	}
}

//******************************************************************************************
void	CGame::MainLoop()	// FUNCTION: 0x0046eee0
{

#if ENABLE_PROFILER==1
	CProfiler::ResetAll();
#endif
	ResetMouseTransientState();	// retail (0046eee9)

	EQuitType qt = PLATFORM.Process();

	// We need to check if we have to quit due to user inactivity (will only happen in playable demo.
	if (CController::InactivityMeansQuitGame())
	{
		// retail: the PS2's attract-mode return on the PC too (0046ef05)
		CLIPARAMS.mAttractMode=TRUE;
		qt = QT_QUIT_TO_FRONTEND;
	}

#if TARGET==PS2
	if (PLATFORM.HasTimeoutExpired())
	{
		qt=QT_QUIT_TO_SYSTEM;
	}
#endif

	if (qt != QT_NONE)
		SetQuit(qt);

	// random key presses etc...
//	ProcessDebug();	// retail: not called (0046ef16..0046ef32)
	//quit early???
	if (qt != QT_NONE)
		return;

//	mBaseTime = PLATFORM.GetSysTimeFloat();

#ifdef DEBUG_TIMERECORDS
	mTimeRecord[mCurrentTR].status[0]='\0';
#endif

	float	start_time = PLATFORM.GetSysTimeFloat();

	//******************************
	// Model
	{
		PROFILE_FN(TotalGameCode);

		mFrameTime  = mBaseTime+mFrameLength;
		mFrameTime2 = mBaseTime+mFrameLength;

		CalcRenderFrameFraction();
		
		Update();
		
		// incase something went wrong in update (like reset a level) 
		if (mQuit == QT_QUIT_TO_FRONTEND || mQuit == QT_USER_QUIT_TO_FRONTEND)
		{
			return ;
		}
	}
	// Process Sounds
	SOUND.UpdateStatus();
	MUSIC.UpdateStatus();

	// Update landscape (done here to prevent the speed hit interfering with rendering
	//                   unpredictably)

	int i;
	for (i=0;i<mPlayers;i++)
	{
		if (GetCamera(i))
		{
			ENGINE.mCurrentViewpoint=i;
			ENGINE.UpdatePos(GetCamera(i));
		}
	}

	//******************************
	// Render

	float	after_model_time = PLATFORM.GetSysTimeFloat();
	if (after_model_time < start_time)
	{
		// oh dear - timer wrapped)
		after_model_time = start_time + 0.01f;  // approximate model time...
#ifdef DEBUG_TIMERECORDS
		strcat(mTimeRecord[mCurrentTR].status,"after_model_time wrapped ");
#endif
	}

	SINT	num_renders = 0;

#ifdef DEBUG_TIMERECORDS
	for (i=0;i<8;i++)
		mTimeRecord[mCurrentTR].renderfraction[i]=0;
#endif

	// Render Loop

	do
	{
		// Paused??
		if ((!mPause) || (mFreeCameraOn[0]))
			mFrameTime2 = PLATFORM.GetSysTimeFloat() - (after_model_time - start_time);
		else
			mFrameTime2 = mBaseTime;		
	
		if (!mPause)
			mFrameTime = mFrameTime2;
		else
			mFrameTime = mBaseTime;	
		
		CalcRenderFrameFraction();

#ifdef DEBUG_TIMERECORDS
		if (num_renders<8)
			mTimeRecord[mCurrentTR].renderfraction[num_renders]=GetFrameRenderFraction();
#endif


		if (mQuit == QT_NONE)
		{
			GAME.Render( num_renders );
		}

		num_renders++;

#ifdef DEBUG_TIMERECORDS
		mRenderRecord[mCurrentRR].renderendtime=PLATFORM.GetSysTimeFloat();
		mRenderRecord[mCurrentRR].renderlength=mRenderRecord[mCurrentRR].renderendtime-mRenderRecord[mCurrentRR].renderstarttime;

		mLastRenderLength=mRenderRecord[mCurrentRR].renderlength;

		mCurrentRR++;
		if (mCurrentRR>=TIMERECORDS)
			mCurrentRR=0;
#endif

		{
			PROFILE_FN(RenderFlip);

			// we care whether we're in the game or not (well, the Xbox does anyway)
			PLATFORM.Flip(TRUE);
		}
	}
	while ((PLATFORM.GetSysTimeFloat() - mBaseTime) < mFrameLength);
	
/*
#if TARGET == XBOX
	// let's sort out the rendering speed
	// !JS!
	extern float back_buffer_scale;
	if (PLATFORM.GetFPS() < 45.f && back_buffer_scale > 0.5f)
	{
		back_buffer_scale *= .96f;
		if (back_buffer_scale < .5f) back_buffer_scale = .5f;
	}
	else
	if (PLATFORM.GetFPS() > 55.f && back_buffer_scale < 1.0f)
	{
		back_buffer_scale *= 1.04f;
		if (back_buffer_scale > 1.f) back_buffer_scale = 1.f;
	}
#endif
*/


#ifdef DEBUG_TIMERECORDS
	mTimeRecord[mCurrentTR].renders=num_renders;
	mTimeRecord[mCurrentTR].basetime=mBaseTime;
	mTimeRecord[mCurrentTR].timetaken=((PLATFORM.GetSysTimeFloat() - start_time));
	mTimeRecord[mCurrentTR].starttimeoffset=start_time-mBaseTime;
	mTimeRecord[mCurrentTR].framelength=mFrameLength;
	mTimeRecord[mCurrentTR].fps=num_renders/mTimeRecord[mCurrentTR].timetaken;
#endif
		
	// now adjust the base time
	if ((num_renders == 1) && ((PLATFORM.GetSysTimeFloat() - start_time) > mFrameLength))
	{
		mBaseTime += (PLATFORM.GetSysTimeFloat() - start_time); 	// running slowly...
		if ((PLATFORM.GetSysTimeFloat() - start_time) > mFrameLength * 1.3f)
		{
#ifdef DEBUG_TIMERECORDS
			strcat(mTimeRecord[mCurrentTR].status,"Very Slow ");
#endif
			mBaseTime = PLATFORM.GetSysTimeFloat(); // running *very* slowly...
		}
		else
		{
#ifdef DEBUG_TIMERECORDS
			strcat(mTimeRecord[mCurrentTR].status,"Slow ");
#endif
		}
	}
	else
		mBaseTime += mFrameLength;

	if (PLATFORM.GetSysTimeFloat() < mBaseTime)
	{
		// oh dear - timer wrapped..
		mBaseTime = PLATFORM.GetSysTimeFloat() - (after_model_time - start_time);
#ifdef DEBUG_TIMERECORDS
		strcat(mTimeRecord[mCurrentTR].status,"mBaseTime wrapped ");
#endif
	}	

	CalcRenderFrameFraction();

#ifdef DEBUG_TIMERECORDS
	mCurrentTR++;	
	if (mCurrentTR>=TIMERECORDS)
		mCurrentTR=0;
#endif

//	mBaseTime = PLATFORM.GetSysTimeFloat();
}
#ifdef NOT_YET_RECONSTRUCTED
//******************************************************************************************
int currentcaptureline=0;

void	CGame::ProcessDebug()
{
	// Hack to enable cloaking - should be moved somewhere sensible when
	// somewhere sensible presents itself (and we've decided what we want
	// to do with cloaking)

	// ***CHANGEME

/*	static	float cloaktime=0;
	static	bool captured=false;

	CMeshRenderer::mCloakAlpha=200;//abs((int) (sin(cloaktime/10.0f)*200));
	cloaktime+=1.0f;
	if ((CMeshRenderer::mCloakAlpha<10) & (!captured))
	{
		captured=true;
		ENGINE.TriggerScreenCapture();
	}

	ENGINE.TriggerPartialScreenCapture(currentcaptureline,currentcaptureline+80);
	currentcaptureline+=80;
	if (currentcaptureline>=1000)
		currentcaptureline=0;

	if (CMeshRenderer::mCloakAlpha>150)
		captured=false;*/

	// End hack

	if(PLATFORM.KeyOnce(223)) CONSOLE.Toggle();
#if TARGET == XBOX
	if(PLATFORM.KeyOnce(192)) CONSOLE.Toggle();
#endif
	if(PLATFORM.KeyOnce('T')) ENGINE.ToggleDebugDraw(DRAW_MAP_WHO) ;
	if(PLATFORM.KeyOnce('C')) ENGINE.ToggleDebugDraw(DRAW_COCKPIT) ;
	if(PLATFORM.KeyOnce('Q')) ENGINE.ToggleDebugDraw(DRAW_OBJECTS_AS_CUBOIDS);
	if(PLATFORM.KeyOnce('P')) ENGINE.ToggleDebugDraw(DRAW_PROFILE) ;
	if(PLATFORM.KeyOnce('S')) ENGINE.ToggleDebugDraw(DRAW_SKELETAL_ACCURATELY) ;
	if(PLATFORM.KeyOnce('W')) ENGINE.ToggleDebugDraw(DRAW_OUTER_RADIUS);
	if(PLATFORM.KeyOnce('L')) EVENT_MANAGER.LogEventManager() ;
	if(PLATFORM.KeyOnce('J')) mFixedFrameRate ^= TRUE ;
	if(PLATFORM.KeyOnce('G')) ENGINE.TriggerScreenCapture();
	if(PLATFORM.KeyOnce('N')) LOG.ToggleOutput();
	if(PLATFORM.KeyOnce('A')) ENGINE.ToggleHudAlphaMode();
	if(PLATFORM.KeyOnce('M')) ENGINE.ToggleDebugDraw(DRAW_MEM_MANAGER);
	if(PLATFORM.KeyOnce('H')) ENGINE.ToggleParticles() ;


	if(PLATFORM.KeyOnce('R'))
	{
		SetQuit( QT_RESTART_LEVEL );
	}

#if ENABLE_PROFILER == 1
	if(PLATFORM.KeyOnce('1')) CProfiler::PrevPage() ;
	if(PLATFORM.KeyOnce('2')) CProfiler::NextPage() ;
	if(PLATFORM.KeyOnce('3')) CProfiler::SelectionUp();
	if(PLATFORM.KeyOnce('4')) CProfiler::SelectionDown();
#endif
	
	//! should not be here
	if(PLATFORM.KeyOnce(KEYCODE_ESCAPE))
	{
		SetQuit(QT_QUIT_TO_FRONTEND);
	}

	// 
/*	if (WORLD.GetBaseWorldThingNB().Size() >= (BASE_THINGS_EXISTS_SIZE-1) )
	{
		LOG.AddMessage("FATAL ERROR: TOO INFO IN BASE WORLD (%d) max = %d", WORLD.GetBaseWorldThingNB().Size, BASE_THINGS_EXISTS_SIZE-1) ;
	}*/
}
#endif	// NOT_YET_RECONSTRUCTED
//******************************************************************************************
CCamera	*CGame::GetCamera(int number)	// FUNCTION: 0x0046f2c0
{
	return GetCurrentCamera(number);
}

//******************************************************************************************
void CGame::SetCamera(int number,CCamera *cam)	// FUNCTION: 0x0046f2d0
{
	SetCurrentCamera(number,cam,false);
}

//******************************************************************************************
void CGame::DeclareLevelWon()	// FUNCTION: 0x0046f2f0
{
	if (mGameState > GAME_STATE_PLAYING) return ;
	
	// Turn off the rumble on all the controllers
	SINT	n;

	for (n=0; n<MAX_PLAYERS; n++)
	{
		if (mController[n])
		{
			if (mController[n])
				mController[n]->SetVibration(0, n);
		}
	}

	mGameState = GAME_STATE_LEVEL_WON ;

	if (mCurrentLevel != 741 && mCurrentLevel != 742 )
	{
		mEndLevelCount = GAME_COUNT_WHEN_WON;
	}
	else
	{
		mEndLevelCount = 0.0f ;
	}
	
	Pause();
}
//******************************************************************************************
// for multiplayer
void	CGame::MPDeclarePlayerWon(int number)	// FUNCTION: 0x0046f360
{
	if (mGameState > GAME_STATE_PLAYING) return ; 

	if (number > 2 || number < 1) 
	{
		LOG.AddMessage("ERROR: invalid player num (%d) to call to MPDeclarePlayerWon", number) ;
		return ;
	}

	if (number==1) mGameState = GAME_STATE_PLAYER_1_WON ;
	if (number==2) mGameState = GAME_STATE_PLAYER_2_WON ;

	// Turn off the rumble on all the controllers
	SINT	n;

	for (n=0; n<MAX_PLAYERS; n++)
	{
		if (mController[n])
			mController[n]->SetVibration(0, n);
	}

	mEndLevelCount = GAME_COUNT_WHEN_WON;
	Pause();
}
//******************************************************************************************
// for multiplayer
void	CGame::MPDeclareGameDrawn()	// FUNCTION: 0x0046f3e0
{
	if (mGameState > GAME_STATE_PLAYING) return ; 

	// Turn off the rumble on all the controllers
	SINT	n;

	for (n=0; n<MAX_PLAYERS; n++)
	{
		if (mController[n])
			mController[n]->SetVibration(0, n);
	}

	mGameState = GAME_STATE_GAME_DRAWN ;
	mEndLevelCount = GAME_COUNT_WHEN_LOST_OR_DRAW;
	Pause();
}
//******************************************************************************************
void CGame::DeclareLevelLost(int message, BOOL player_died)	// FUNCTION: 0x0046f430
{
	mLevelLostReason = message ;

	if (message)
	{
		char c[256];
		strcpy(c, FromWCHAR(TEXT_DB.GetString(message)) );
		LOG.AddMessage("Level lost message = '%s'", c ) ;
	}

	if (mGameState > GAME_STATE_PLAYING) return ;
	
	mGameState = GAME_STATE_LEVEL_LOST ;
	mEndLevelCount = GAME_COUNT_WHEN_LOST_OR_DRAW ;

	// Turn off the rumble on all the controllers
	SINT	n;

	for (n=0; n<MAX_PLAYERS; n++)
	{
		if (mController[n])
			mController[n]->SetVibration(0, n);
	}

	if (player_died == FALSE)
	{
		Pause() ;
	}
	else
	{
		EVENT_MANAGER.AddEvent(15.0f, PAUSE_GAME, this) ;
		EVENT_MANAGER.AddEvent(CONTINUE_FADE_OUT_GAME_SOUNDS, this, NEXT_FRAME) ;
	}
}

//******************************************************************************************
void CGame::DeclarePlayerDead(int number)	// FUNCTION: 0x0046f550
{
	CBattleEngine* engine = mPlayer[number-1]->GetBattleEngine() ;
	FVector pos = engine->GetPos() ;

	// get pos of player
	if (mFreeCameraOn[number])
		ToggleFreeCameraOff(number);

#line 2889
	SetCurrentCamera(number-1,new( MEMTYPE_CAMERA ) CViewPointCamera(pos, 0.5f, 0.0f, 6.0f, 3.0f));

	if (GAME.IsMultiplayer() == FALSE)
	{
		if (mMessageBox) mMessageBox->StopCurrentPlayingSound();
	}

	switch (WORLD.GetType())
	{
		case CWorld::kSinglePlayer:
		{
			if (engine->IsInWater())
			{
				DeclareLevelLost(GAME_OVER_WATER,TRUE);
				return ;
			}

			DeclareLevelLost(GAME_OVER_DEATH,TRUE);
		}
		break;

		case CWorld::kCooperativeMultiplayer:
		{
			switch (number)
			{
				case 1:
					EVENT_MANAGER.AddEvent(5.0f,(int)RESPAWN_PLAYER_1,this,START_OF_FRAME,NULL);
					break;

				case 2:
					EVENT_MANAGER.AddEvent(5.0f,(int)RESPAWN_PLAYER_2,this,START_OF_FRAME,NULL);
					break;
			}
		}
		break;
		
		case CWorld::kVersusMultiplayer:
		{
			switch (number)
			{
				case 1:
					EVENT_MANAGER.AddEvent(5.0f,(int)RESPAWN_PLAYER_1,this,START_OF_FRAME,NULL);
					break;

				case 2:
					EVENT_MANAGER.AddEvent(5.0f,(int)RESPAWN_PLAYER_2,this,START_OF_FRAME,NULL);
					break;
			}
		}
		break;
	}
}

//******************************************************************************************
void CGame::ReceiveButtonAction(CController* from_controller, int button, float val)	// FUNCTION: 0x0046f7e0
{
	SINT	n;

	switch (button)
	{
		case BUTTON_TOGGLE_GOD_MODE:
		{
			for (n=0; n<MAX_PLAYERS; n++)
			{
				if (mPlayer[n])
				{
					if (mPlayer[n]->IsGod())
						mPlayer[n]->SetIsGod(FALSE);
					else
						mPlayer[n]->SetIsGod(TRUE);
				}
			}
			break;
		}

		case BUTTON_TOGGLE_FREE_CAMERA:
		{
//			if (PLAYABLE_DEMO == 0)
			if (FRONTEND.mFEPSaveGame.IsCheatActive(4))	// retail: the free camera is a cheat (0046f82e)
			{
#ifdef E3BUILD
			if (mSupervisorMode)
			{
#endif
				for (n=0; n<MAX_PLAYERS; n++)
				{
					if (mPlayer[n])
					{
						if (mFreeCameraOn[n] == FALSE)
						{
							ToggleFreeCameraOn(n) ;
						}
						else
						{
							ToggleFreeCameraOff(n) ;
						}
					}
				}
#ifdef E3BUILD
			}
#endif
			}
			break ;
		}
		case BUTTON_ADVANCE_ONE_FRAME:
		{
			mAdvanceFrame = TRUE ;
			break ;
		}

		case BUTTON_SKIP_CUTSCENE:
		{
			CCutscene::SkipCutscene();
			break;
		}
		
		case BUTTON_CONSOLE_MENU_UP:
		{
			CONSOLE.MenuUp();
			break;
		}

		case BUTTON_CONSOLE_MENU_DOWN:
		{
			CONSOLE.MenuDown();
			break;
		}

		case BUTTON_CONSOLE_MENU_SELECT:
		{
			CONSOLE.MenuSelect();			
			break;
		}		

		case BUTTON_TOGGLE_DEBUG_SQUAD_FORWARD:
		{
			ToggleDebugSquadForward();
			break ;
		}

		case BUTTON_TOGGLE_DEBUG_SQUAD_BACKWARD:
		{
			ToggleDebugSquadBackward();
			break ;
		}
		case BUTTON_TOGGLE_DEBUG_UNIT_FORWARD:
		{
			ToggleDebugUnitForward();
			break ;
		}

		case BUTTON_TOGGLE_DEBUG_UNIT_BACKWARD:
		{
			ToggleDebugUnitBackward();
			break ;
		}

		case BUTTON_WIN_LEVEL:
		{
			
			if (CLIPARAMS.mDeveloperMode == TRUE)
			{
				int range = mSGradeScore - mDGradeScore;
				if (range > 0)
				{
					int val = mRandomStream->Next() % range ;
					val += mDGradeScore;
					mScore = val ;
				}
				DeclareLevelWon() ;
			}
		
			break ;
		}


		case BUTTON_COMPLETE_ALL_OBJECTIVES:
		{
			LOG.AddMessage("Completing all Objectives") ;
			int i;
			for (i=0 ; i < MAX_PRIMARY_OBJECTIVES; i++)
			{
				SetPrimaryObjectiveComplete(i, 10) ;
			}

			for (i=0 ; i < MAX_SECONDARY_OBJECTIVES; i++)
			{
				SetSecondaryObjectiveComplete(i, 10) ;
			}

			break ;

		}


		
		case BUTTON_LOOSE_LEVEL:
		{
			if (CLIPARAMS.mDeveloperMode == TRUE &&
				mGameState >= GAME_STATE_PLAYING)
			{
				int range = mSGradeScore - mDGradeScore;
				if (range > 0)
				{
					int val = (mRandomStream->Next()) % range ;
					val += mDGradeScore;
					mScore = val ;
				}
				DeclareLevelLost() ;
			}
			break ;
		}
		default:
		{
			LOG.AddMessage("ERROR: Unknown command sent to onslaught demo") ;
		}
	}
}

//******************************************************************************************
void    CGame::UnPause()	// FUNCTION: 0x0046fae0
{
	mPause = FALSE; 

	if (IsFreeCameraOn(0) == FALSE) 
	{
		mPauseMenu->DeActivate(TRUE);
	}
}

//******************************************************************************************
void	CGame::Pause(BOOL toggle_pause_menu, CController* from_controller)	// FUNCTION: 0x0046fb00
{
	if (GAME.GetGameState() > GAME_STATE_PRE_RUNNING)
	{
		printf("Pause() called\n");

		if (mPause == FALSE)
		{
			mPause = TRUE; 

			// Turn off the rumble on all the controllers
			SINT	n;

			for (n=0; n<MAX_PLAYERS; n++)
			{
				if (mController[n])
					mController[n]->SetVibration(0, n);
			}

			if (toggle_pause_menu && from_controller)
			{
				mPauseMenu->Activate(TRUE) ;

#if TARGET == XBOX
				mController[0]->SetToControl(mPauseMenu) ;

				// and player 1 gets to control it as well
				if (mController[1]) mController[1]->SetToControl(mPauseMenu);
#else
				ShutdownMouse();	from_controller->SetToControl(mPauseMenu) ;	// retail: the PC releases the mouse
#endif

			}
		}
	}
}

//******************************************************************************************
void	CGame::ToggleDebugUnitForward()	// FUNCTION: 0x0046fb80
{
	if (mCurrentDebugSquad == NULL) return ;
	mCurrentDebugUnitNum++ ;
	SPtrSet<CUnit>* unit_in_squad = mCurrentDebugSquad->GetUnits() ;

//	LOG.AddMessage("num units = %d", unit_in_squad->Size()) ;

	CUnit*	unit_to_debug = unit_in_squad->First() ;

	if (unit_to_debug == NULL)
	{
		mCurrentDebugUnitNum = -1 ;
		mCurrentDebugUnit = NULL ;
	}
	else
	{

		mCurrentDebugUnit = unit_to_debug ;
		
		int i = mCurrentDebugUnitNum ;

		while (i> 0 && unit_to_debug != NULL)
		{
			CUnit*	next = unit_in_squad->Next() ;
			if (next == NULL)
			{
				mCurrentDebugUnitNum = -1 ;
				unit_to_debug = NULL ;
				i=-1 ;
			}
			else
			{
				unit_to_debug = next ;
				i-- ;
			}
		
		}
		
		mCurrentDebugUnit = unit_to_debug ;
	//	LOG.AddMessage("Num Unit to debug = %d  %08x", mCurrentDebugUnitNum, mCurrentDebugUnit.ToRead() ) ;
	}
	delete unit_in_squad ;

}

//******************************************************************************************
void	CGame::ToggleDebugUnitBackward()	// FUNCTION: 0x0046fc40
{
	if (mCurrentDebugSquad == NULL) return ;
	mCurrentDebugUnitNum-- ;

	SPtrSet<CUnit>* unit_in_squad = mCurrentDebugSquad->GetUnits() ;

	if (mCurrentDebugUnitNum == -1 )
	{
		mCurrentDebugUnitNum = -1 ;
		mCurrentDebugUnit = NULL ;
		delete unit_in_squad ;
		return ;
	}

	if (mCurrentDebugUnitNum < 0 ) mCurrentDebugUnitNum = unit_in_squad->Size() -1 ;

	CUnit*	unit_to_debug = unit_in_squad->First() ;

	if (unit_to_debug == NULL)
	{
		mCurrentDebugUnitNum = -1 ;
		mCurrentDebugUnit = NULL ;
	}
	else
	{
		int i = mCurrentDebugUnitNum ;

		while (i> 0 && unit_to_debug != NULL)
		{
			CUnit*	next = unit_in_squad->Next() ;
			if (next == NULL)
			{
				LOG.AddMessage("Warning: can't find unit %d to debug", mCurrentDebugUnitNum) ;
				mCurrentDebugUnitNum = -1 ;
				unit_to_debug = NULL ;
				i=-1 ;
			}
			else
			{
				unit_to_debug = next ;
				i-- ;
			}
		
		}
		mCurrentDebugUnit = unit_to_debug ;
	//	LOG.AddMessage("Num Unit to debug = %d  %08x", mCurrentDebugUnitNum, mCurrentDebugUnit.ToRead() ) ;
	}
	delete unit_in_squad ;
}

//******************************************************************************************
void CGame::ToggleDebugSquadBackward()	// FUNCTION: 0x0046fd40
{
//	LOG.AddMessage("1");
	mCurrentDebugUnit = NULL ;
	mCurrentDebugUnitNum = -1 ;
	SPtrSet<CSquad>& unit_in_world = WORLD.GetSquadNB() ;
	mCurrentDebugSquadNum-- ;
	if (mCurrentDebugSquadNum < -1) mCurrentDebugSquadNum = unit_in_world.Size() -1 ;
	if (mCurrentDebugSquadNum == -1 )
	{
		mCurrentDebugSquadNum = -1 ;
		mCurrentDebugSquad = NULL ;
		return ;
	}

//	LOG.AddMessage("num of units = %d", unit_in_world.Size() ) ;
	CSquad*	squad_to_debug = unit_in_world.First() ;

	if (squad_to_debug == NULL)
	{
		mCurrentDebugSquadNum = -1 ;
		mCurrentDebugSquad = NULL ;
	}
	else
	{
		int i = mCurrentDebugSquadNum ;

		while (i>0 && squad_to_debug != NULL)
		{
			CSquad*	next = unit_in_world.Next() ;
			if (next == NULL)
			{
				LOG.AddMessage("Warning: can't find squad %d to debug", mCurrentDebugSquadNum) ;
				mCurrentDebugSquadNum = -1 ;
				squad_to_debug = NULL ;
				i=-1 ;
			}
			else
			{
				squad_to_debug = next ;
				i-- ;
			}
		
		}
		mCurrentDebugSquad = squad_to_debug ;
	//	LOG.AddMessage("Num squad to debug = %d  %08x", mCurrentDebugSquadNum, mCurrentDebugSquad.ToRead() ) ;
	}
//	LOG.AddMessage("2");
}

//******************************************************************************************
void CGame::ToggleDebugSquadForward()	// FUNCTION: 0x0046fe20
{
//	LOG.AddMessage("1");
	mCurrentDebugUnit = NULL ;
	mCurrentDebugUnitNum = -1 ;
	SPtrSet<CSquad>& unit_in_world = WORLD.GetSquadNB() ;
	mCurrentDebugSquadNum++ ;

//	LOG.AddMessage("num of units = %d", unit_in_world.Size() ) ;
	CSquad*	squad_to_debug = unit_in_world.First() ;

	if (squad_to_debug == NULL)
	{
		mCurrentDebugSquadNum = -1 ;
		mCurrentDebugSquad = NULL ;
	}
	else
	{
		int i = mCurrentDebugSquadNum ;

		while (i>0 && squad_to_debug != NULL)
		{
			CSquad*	next = unit_in_world.Next() ;
			if (next == NULL)
			{
				mCurrentDebugSquadNum = -1 ;
				squad_to_debug = NULL ;
				i=-1 ;
			}
			else
			{
				squad_to_debug = next ;
				i-- ;
			}
		
		}
		mCurrentDebugSquad = squad_to_debug ;
	//	LOG.AddMessage("Num squad to debug = %d  %08x", mCurrentDebugSquadNum, mCurrentDebugSquad.ToRead() ) ;
	}
//	LOG.AddMessage("2");
}
//******************************************************************************************
void CGame::PreRender()
{
	// This contains stuff to do before rendering

	// DO NOT rely on things in here getting called at a fixed rate,
	// because they won't be as they get called for every frame that is
	// rendered!
	
	// This enables free camera control to run independently of the 
	// game speed
	// This obviously violates rule (1) above, but as freecam is a debugging
	// feature this isn't a huge problem.

//	for (int i=0;i<mPlayers;i++)
//	{
//		if (mFreeCameraOn[i])
//		{
//			PLATFORM.UpdateJoystick(i);
//			mController[i]->Flush(); 
//		}
//	}

}
//******************************************************************************************
void	CGame::StartPanState()
{
	if (mGameState == GAME_STATE_PANNING) return ; 
	if (mPanTime <= 0.0f) 
	{
		StartPlayingState() ;
		return ;
	}

	mGameState = GAME_STATE_PANNING ;
	int i = 0 ;
	for(i=0; i<mPlayers; i++ )
	{
		if (mPlayer[i] != NULL)
		{
			mPlayer[i]->GotoPanView(mPanTime) ;
		}
	}
	EVENT_MANAGER.AddEvent(mPanTime, FINISHED_PANNING, this) ;
}
//******************************************************************************************
void	CGame::StartPlayingState()	// FUNCTION: 0x0046fec0
{
	if (mGameState == GAME_STATE_PLAYING) return ; 
	mGameState = GAME_STATE_PLAYING ;
	SCRIPT_EVENT_NB.PostEvent("game playing");
	EVENT_MANAGER.AddEvent(ALLOWED_TO_PLAY_MESSAGES, mMessageBox, NEXT_FRAME) ;
}
//******************************************************************************************
void CGame::HandleEvent(CEvent* event)	// FUNCTION: 0x0046ff10
{
	switch ((EGameEvent)event->GetEventNum())
	{
		case DEMO_RESTART_LEVEL:
		{
		//	SYSTEM.mNextLevel = GetCurrentLevel();
			GAME.SetQuit( QT_QUIT_TO_FRONTEND );
			break ;
		}

		case FINISHED_PRE_RUN:
		{
			StartPanState() ;
			break ;
		}
		case FINISHED_PANNING:
		{
			StartPlayingState() ;
			break ;
		}

		case RESPAWN_PLAYER_1:
		{
			RespawnPlayer(0);
			break;
		}

		case RESPAWN_PLAYER_2:
		{
			RespawnPlayer(1);
			break;
		}
		case PAUSE_GAME:
		{
			Pause() ;
			break ;
		}
		case CONTINUE_FADE_OUT_GAME_SOUNDS:
		{
			mHackCurrentGameMasterVolume -=0.003f; 

			if (mHackCurrentGameMasterVolume <0)
			{
				mHackCurrentGameMasterVolume = 0.0f ;
			}
			else
			{
				EVENT_MANAGER.AddEvent(CONTINUE_FADE_OUT_GAME_SOUNDS, this, NEXT_FRAME) ;
			}
	
			SOUND.SetGameSoundsMasterVolume(mHackCurrentGameMasterVolume) ;
			SOUND.UpdateVolumeForAllSoundEvents();
			break ;
		}
	}
}

//******************************************************************************************
void CGame::RespawnPlayer(
	SINT	inNumber)	// FUNCTION: 0x00470120 ?RespawnPlayer@CGame@@QAEXH@Z
{
#ifdef _DIRECTX
	// Let's force the landscape to refresh to the right LOD right now.
	ENGINE.GetLandscape()->MakeNextUpdateBeFast();
#endif

	BOOL	spawn=TRUE;

	switch (WORLD.GetType())
	{
		case CWorld::kCooperativeMultiplayer:
		{
			switch (inNumber)
			{
				case 0:
					if (mPlayer1Lives==0)
					{
						DeclareLevelLost(P1_OUT_OF_LIVES,TRUE);
						spawn=FALSE;
					}
					break;

				case 1:
					if (mPlayer2Lives==0)
					{
						DeclareLevelLost(P2_OUT_OF_LIVES,TRUE);
						spawn=FALSE;
					}
					break;
			}
		}
		break;

		case CWorld::kVersusMultiplayer:
		{
			switch (inNumber)
			{
				case 0:
					if (mPlayer1Lives==0)
					{
						MPDeclarePlayerWon(2);
						spawn=FALSE;
					}
					break;

				case 1:
					if (mPlayer2Lives==0)
					{
						MPDeclarePlayerWon(1);
						spawn=FALSE;
					}
					break;
			}
		}
	}

	if (spawn)
	{
		if (inNumber==0)
		{
			if (mPlayer1Lives>0)
				mPlayer1Lives--;
		}
		else if (inNumber==1)
		{
			if (mPlayer2Lives>0)
				mPlayer2Lives--;
		}

		// Count the respawn points
		SPtrSet<CSpawnPoint>		&spawnPointNB=WORLD.GetSpawnPointNB();
		ListIterator<CSpawnPoint>	spawnIterator(&spawnPointNB);
		CSpawnPoint					*spawnPoint;
		SINT						spawnPoints=0;

		for (spawnPoint=spawnIterator.First(); spawnPoint; spawnPoint=spawnIterator.Next())
		{
			if ((spawnPoint->GetPlayerNumber()==inNumber+1) &&
				(spawnPoint->Available()))
			{
				spawnPoints++;
			}
		}

		// If a respawn point is found then spawn from there, else just spawn from a start point
		if (spawnPoints)
		{
			SINT	chosen=(SINT)(FloatRandom()*(spawnPoints-1))+1;
			SINT	index=1;

			for (spawnPoint=spawnIterator.First(); spawnPoint; spawnPoint=spawnIterator.Next())
			{
				if ((spawnPoint->GetPlayerNumber()==inNumber+1) &&
					(spawnPoint->Available()))
				{
					if (index==chosen)
					{
						if (CBattleEngine *newEngine=spawnPoint->SpawnBattleEngine(TRUE))
						{
							mPlayer[inNumber]->AssignBattleEngine(newEngine);
							mPlayer[inNumber]->GotoFPView();
							return;
						}
					}

					index++;
				}
			}
		}
		else
		{
			SPtrSet<CStart>			&startNB=WORLD.GetStartNB();
			ListIterator<CStart>	iterator(&startNB);

			for (CStart* start=iterator.First(); start; start=iterator.Next())
			{
				if ((start->GetPlayerNumber()==inNumber+1) &&
					(start->Available()))
				{
					if (CBattleEngine *newEngine=start->SpawnBattleEngine(TRUE))
					{
						mPlayer[inNumber]->AssignBattleEngine(newEngine);
						mPlayer[inNumber]->GotoFPView();
						return;
					}
				}
			}
		}

		// If we have got here then we have failed to spawn, try again later
		if (inNumber==0)
		{
			mPlayer1Lives++;
			EVENT_MANAGER.AddEvent(1.0f,(int)RESPAWN_PLAYER_1,this,START_OF_FRAME,NULL);
		}
		else if (inNumber==1)
		{
			mPlayer2Lives++;
			EVENT_MANAGER.AddEvent(1.0f,(int)RESPAWN_PLAYER_2,this,START_OF_FRAME,NULL);
		}
	}
}

//******************************************************************************************
void CGame::ToggleFreeCameraOn(int playernumber)	// FUNCTION: 0x00470430
{ 
	mOldCamera[playernumber] = GetCurrentCamera(playernumber);
	
	// copy current cameras orientation except remove the roll from it
	FVector s = mOldCamera[playernumber]->GetOrientation() * FVector(0.0f,1.0f,0.0f) ;
	FMatrix m = FMatrix(s.Azimuth(),s.Elevation(),0.0f) ;

#line 3631
	CControllableCamera* c = new( MEMTYPE_CAMERA ) CControllableCamera(mOldCamera[playernumber]->GetPos(), m) ;
	mController[playernumber]->SetToControl(c) ;

	SetCurrentCamera(playernumber,c,false);
	mFreeCameraOn[playernumber] = TRUE ;

	if (mPause == TRUE)
	{
		mPauseOnWhenStartedFreeCam[playernumber] = TRUE ;
	}
	else
	{
		mPauseOnWhenStartedFreeCam[playernumber] = FALSE ;
	}

#if TARGET == XBOX
	// disable visibility testing
	CXBOXVisibilityTester::DisableTesting = true;
#endif
}

//******************************************************************************************
void CGame::ToggleFreeCameraOff(int playernumber)
{
	if (GAME.GetGameState() > GAME_STATE_PRE_RUNNING)
	{
		mFreeCameraOn[playernumber] = FALSE;
		SetCurrentCamera(playernumber,mOldCamera[playernumber]) ;
		mController[playernumber]->RelinquishControl() ;
		
		if (mPauseOnWhenStartedFreeCam[playernumber] == TRUE)
		{
			mPause = TRUE ;
		}
		else
		{
			mPause = FALSE ;
		}
	}


#if TARGET == XBOX
	// enable visibility testing. Of course, this is broken logic in multiplayer,
	// but then visibility testing is off anyway.
	CXBOXVisibilityTester::DisableTesting = false;
#endif

#if TARGET != PS2
	// stop the silly LOD
	ENGINE.GetLandscape()->MakeNextUpdateBeFast();
#endif
}

//******************************************************************************************
CCONTROLLER *CGame::GetController(int number)	// FUNCTION: 0x004705d0
{
	return mController[number];
}

//******************************************************************************************
void CGame::SetCurrentCamera(int number,CCamera *c,bool releaseoldcamera)	// FUNCTION: 0x004705e0
{ 
	ASSERT(number>=0);
	ASSERT(number<MAX_PLAYERS);
	ASSERT(c);

	// ok if we change camera and we are in free camera mode then make sure we change old camera
	// rather than current camera ;
	if (mFreeCameraOn[number] == TRUE)
	{
		if (releaseoldcamera)
			if (mOldCamera[number])
				delete mOldCamera[number];
		mOldCamera[number] = c ;
	}
	else
	{
		if (releaseoldcamera)
			if (mCurrentCamera[number])
				delete mCurrentCamera[number];
		mCurrentCamera[number]=c;
	}
}

#if TARGET == PC

#include	"state.h"

#endif

//******************************************************************************************
void CGame::DrawDebugStuff()	// FUNCTION: 0x00470650
{

//#if TARGET == PC
#ifdef _DIRECTX

	STATE.UseDefault() ;

#endif

	if (mCurrentDebugUnit.ToRead())
	{
		mCurrentDebugUnit->DrawDebugStuff3d() ;
	}
	if (mCurrentDebugSquad.ToRead())
	{
		mCurrentDebugSquad->DrawDebugStuff3d() ;
	}

	float	yOffset=110;
	
	if (CLIPARAMS.mDeveloperMode)
	{
		int i;

		char buf[256];

#ifdef USE_THING_HEAP
		sprintf(buf,"%dK/%dK of thing heap free",MEM_MANAGER.GetThingHeap()->GetFreeSize()/1024,MEM_MANAGER.GetThingHeap()->GetSize()/1024);
		PLATFORM.Font(FONT_DEBUG )->DrawText(64, yOffset, 0xffffff00,ToWCHAR(buf)) ;
		yOffset+=10;

		if (MEM_MANAGER.GetThingHeap()->GetFreeSize()<THING_HEAP_NEARLY_FULL_THRESHOLD)
		{
			yOffset+=10;
			sprintf(buf,"WARNING : THING HEAP NEARLY FULL!");
			if (MEM_MANAGER.GetThingHeap()->GetFreeSize()<THING_HEAP_FULL_THRESHOLD)
			{
				sprintf(buf,"ERROR : THING HEAP FULL! THIS LEVEL IS _TOO BIG_!");
			}
			PLATFORM.Font(FONT_DEBUG )->DrawText(64, yOffset, 0xffff0000,ToWCHAR(buf)) ;
			yOffset+=10;
			yOffset+=10;
		}
#endif
		sprintf(buf,"%dK/%dK of default heap free",MEM_MANAGER.GetDefaultHeap()->GetFreeSize()/1024,MEM_MANAGER.GetDefaultHeap()->GetSize()/1024);
		PLATFORM.Font(FONT_DEBUG )->DrawText(64, yOffset, 0xffffff00,ToWCHAR(buf)) ;
		yOffset+=10;

		// Calculate total level data sizes

		int totalmeshdatasize=	MEM_MANAGER.GetDefaultHeap()->GetTypeSize(MEMTYPE_MESH)+
								MEM_MANAGER.GetDefaultHeap()->GetTypeSize(MEMTYPE_MESHCACHE)+
								MEM_MANAGER.GetDefaultHeap()->GetTypeSize(MEMTYPE_POLYBUCKET)+
								MEM_MANAGER.GetDefaultHeap()->GetTypeSize(MEMTYPE_POLYBUCKET_ENTRY)+
								MEM_MANAGER.GetDefaultHeap()->GetTypeSize(MEMTYPE_ARRAY);
		
		int totalstaticshadowsize=MEM_MANAGER.GetDefaultHeap()->GetTypeSize(MEMTYPE_STATICSHADOW);

		int maxmeshdatasize=25*1024*1024; // Hmm....? There must be a better way to handle this, but it doesn't spring to mind :-(
		int maxstaticshadowdatasize=1024*1024;

		if (mShowDataSizes)
		{
			sprintf(buf,"%dK of mesh data",totalmeshdatasize/1024);
			PLATFORM.Font(FONT_DEBUG )->DrawText(64, yOffset, 0xffffff00,ToWCHAR(buf)) ;
			yOffset+=10;

			sprintf(buf,"%dK of static shadow data",totalstaticshadowsize/1024);
			PLATFORM.Font(FONT_DEBUG )->DrawText(64, yOffset, 0xffffff00,ToWCHAR(buf)) ;
			yOffset+=10;			
		}

#ifndef _DEBUG
		if (totalmeshdatasize>maxmeshdatasize)
		{
			yOffset+=10;
			DWORD col=rand() % 255;
			col=0xffff0000 | (col<<8);
			sprintf(buf,"**TOO MUCH MESH DATA** (%dK/%dK)",totalmeshdatasize/1024,maxmeshdatasize/1024);
			PLATFORM.Font(FONT_DEBUG )->DrawText(64, yOffset, col, ToWCHAR(buf)) ;
			yOffset+=10;
		}

		if (totalstaticshadowsize>maxstaticshadowdatasize)
		{
			yOffset+=10;
			DWORD col=rand() % 255;
			col=0xffff0000 | (col<<8);
			sprintf(buf,"**TOO MUCH STATIC SHADOW DATA** (%dK/%dK)",totalstaticshadowsize/1024,maxstaticshadowdatasize/1024);
			PLATFORM.Font(FONT_DEBUG )->DrawText(64, yOffset, col, ToWCHAR(buf)) ;
			yOffset+=10;
		}	
#endif

		if (mShowMemDeltas)
		{
			yOffset+=10;
			
			for (i=0;i<MEMTYPE_LIMIT;i++)
			{
				mUsedMem[i]=FALSE;
				UINT size=MEM_MANAGER.GetDefaultHeap()->GetTypeSize((EMemoryType) i);
				mSizeDelta[i]=size-mLastSize[i];
			}
			
			for (i=0;i<8;i++)
			{
				int best=0;
				int bestn=-1;
				
				for (int j=0;j<MEMTYPE_LIMIT;j++)
				{				
					if ((mSizeDelta[j]>best) && (!mUsedMem[j]))
					{
						best=mSizeDelta[j];
						bestn=j;
					}
				}
				
				if (bestn!=-1)
				{
					mUsedMem[bestn]=TRUE;
					
					if (mSizeDelta[bestn]>0)
					{
						UINT size=MEM_MANAGER.GetDefaultHeap()->GetTypeSize((EMemoryType) bestn);
						DWORD col;
						char buf[256];
						if (mSizeDelta[bestn]>0)
						{
							sprintf( buf, "%s : %dK->%dK (%dK)",CMEMORYMANAGER::mTypeName[bestn],mLastSize[bestn]/1024,size/1024,mSizeDelta[bestn]/1024);
							col=0xFFFF0000;
						}
						else
						{
							sprintf( buf, "%s : %dK->%dK (%dK)",CMEMORYMANAGER::mTypeName[bestn],mLastSize[bestn]/1024,size/1024,-mSizeDelta[bestn]/1024);
							col=0xFF00FF00;
						}
						
						PLATFORM.Font( FONT_DEBUG )->DrawText( 32-2, yOffset-2, 0xff000000, ToWCHAR(buf));				
						PLATFORM.Font( FONT_DEBUG )->DrawText( 32, yOffset, col, ToWCHAR(buf));
						
						yOffset+=10;		
					}
				}
			}
		}		
	}
	
	if (mCurrentDebugSquad.ToRead())
	{
		PLATFORM.Font( FONT_DEBUG )->DrawText(4, yOffset, 0xffff0000,ToWCHAR("SQUAD INFO")) ;
		yOffset+=10;

		mCurrentDebugSquad->DrawDebugStuff(yOffset) ;
		yOffset+=10;
	}

	if (mCurrentDebugUnit.ToRead())
	{
		PLATFORM.Font( FONT_DEBUG )->DrawText(4, yOffset, 0xffff0000,ToWCHAR("UNIT INFO")) ;
		yOffset+=10;
		mCurrentDebugUnit->DrawDebugStuff(yOffset) ;
		yOffset+=10;
	}
}

//*******************************************************************************
// Retail: a dormant "unfinished version" watermark, shown only XORed back from a key built at startup (the keys'
// initializers 00470cc0, 004710c0, which index the three wide literals: retail keeps them among game's literals; the
// unscramble 00472270). Nothing sets gShowUnfinishedWatermark (00679ec0), so the release never draws it.
#define AUTORUNNING_TEXT	L"AUTORUNNING UNFINISHED RELQ VERSION              "	// 0062c3a8
#define COVER_TEXT	L"Your graphics card is not fully supported, please"		// 0062c40c
#define UNFINISHED_TEXT	L"UNFINISHED RELQ VERSION                          "	// 0062c470

WCHAR	gAutoRunningKey[50] = { AUTORUNNING_TEXT[0] ^ COVER_TEXT[0], AUTORUNNING_TEXT[1] ^ COVER_TEXT[1], AUTORUNNING_TEXT[2] ^ COVER_TEXT[2], AUTORUNNING_TEXT[3] ^ COVER_TEXT[3], AUTORUNNING_TEXT[4] ^ COVER_TEXT[4], AUTORUNNING_TEXT[5] ^ COVER_TEXT[5], AUTORUNNING_TEXT[6] ^ COVER_TEXT[6], AUTORUNNING_TEXT[7] ^ COVER_TEXT[7], AUTORUNNING_TEXT[8] ^ COVER_TEXT[8], AUTORUNNING_TEXT[9] ^ COVER_TEXT[9], AUTORUNNING_TEXT[10] ^ COVER_TEXT[10], AUTORUNNING_TEXT[11] ^ COVER_TEXT[11], AUTORUNNING_TEXT[12] ^ COVER_TEXT[12], AUTORUNNING_TEXT[13] ^ COVER_TEXT[13], AUTORUNNING_TEXT[14] ^ COVER_TEXT[14], AUTORUNNING_TEXT[15] ^ COVER_TEXT[15], AUTORUNNING_TEXT[16] ^ COVER_TEXT[16], AUTORUNNING_TEXT[17] ^ COVER_TEXT[17], AUTORUNNING_TEXT[18] ^ COVER_TEXT[18], AUTORUNNING_TEXT[19] ^ COVER_TEXT[19], AUTORUNNING_TEXT[20] ^ COVER_TEXT[20], AUTORUNNING_TEXT[21] ^ COVER_TEXT[21], AUTORUNNING_TEXT[22] ^ COVER_TEXT[22], AUTORUNNING_TEXT[23] ^ COVER_TEXT[23], AUTORUNNING_TEXT[24] ^ COVER_TEXT[24], AUTORUNNING_TEXT[25] ^ COVER_TEXT[25], AUTORUNNING_TEXT[26] ^ COVER_TEXT[26], AUTORUNNING_TEXT[27] ^ COVER_TEXT[27], AUTORUNNING_TEXT[28] ^ COVER_TEXT[28], AUTORUNNING_TEXT[29] ^ COVER_TEXT[29], AUTORUNNING_TEXT[30] ^ COVER_TEXT[30], AUTORUNNING_TEXT[31] ^ COVER_TEXT[31], AUTORUNNING_TEXT[32] ^ COVER_TEXT[32], AUTORUNNING_TEXT[33] ^ COVER_TEXT[33], AUTORUNNING_TEXT[34] ^ COVER_TEXT[34], AUTORUNNING_TEXT[35] ^ COVER_TEXT[35], AUTORUNNING_TEXT[36] ^ COVER_TEXT[36], AUTORUNNING_TEXT[37] ^ COVER_TEXT[37], AUTORUNNING_TEXT[38] ^ COVER_TEXT[38], AUTORUNNING_TEXT[39] ^ COVER_TEXT[39], AUTORUNNING_TEXT[40] ^ COVER_TEXT[40], AUTORUNNING_TEXT[41] ^ COVER_TEXT[41], AUTORUNNING_TEXT[42] ^ COVER_TEXT[42], AUTORUNNING_TEXT[43] ^ COVER_TEXT[43], AUTORUNNING_TEXT[44] ^ COVER_TEXT[44], AUTORUNNING_TEXT[45] ^ COVER_TEXT[45], AUTORUNNING_TEXT[46] ^ COVER_TEXT[46], AUTORUNNING_TEXT[47] ^ COVER_TEXT[47], AUTORUNNING_TEXT[48] ^ COVER_TEXT[48], AUTORUNNING_TEXT[49] ^ COVER_TEXT[49] };
// FUNCTION: 0x00470cc0 init:gAutoRunningKey
WCHAR	gUnfinishedKey[50] = { UNFINISHED_TEXT[0] ^ COVER_TEXT[0], UNFINISHED_TEXT[1] ^ COVER_TEXT[1], UNFINISHED_TEXT[2] ^ COVER_TEXT[2], UNFINISHED_TEXT[3] ^ COVER_TEXT[3], UNFINISHED_TEXT[4] ^ COVER_TEXT[4], UNFINISHED_TEXT[5] ^ COVER_TEXT[5], UNFINISHED_TEXT[6] ^ COVER_TEXT[6], UNFINISHED_TEXT[7] ^ COVER_TEXT[7], UNFINISHED_TEXT[8] ^ COVER_TEXT[8], UNFINISHED_TEXT[9] ^ COVER_TEXT[9], UNFINISHED_TEXT[10] ^ COVER_TEXT[10], UNFINISHED_TEXT[11] ^ COVER_TEXT[11], UNFINISHED_TEXT[12] ^ COVER_TEXT[12], UNFINISHED_TEXT[13] ^ COVER_TEXT[13], UNFINISHED_TEXT[14] ^ COVER_TEXT[14], UNFINISHED_TEXT[15] ^ COVER_TEXT[15], UNFINISHED_TEXT[16] ^ COVER_TEXT[16], UNFINISHED_TEXT[17] ^ COVER_TEXT[17], UNFINISHED_TEXT[18] ^ COVER_TEXT[18], UNFINISHED_TEXT[19] ^ COVER_TEXT[19], UNFINISHED_TEXT[20] ^ COVER_TEXT[20], UNFINISHED_TEXT[21] ^ COVER_TEXT[21], UNFINISHED_TEXT[22] ^ COVER_TEXT[22], UNFINISHED_TEXT[23] ^ COVER_TEXT[23], UNFINISHED_TEXT[24] ^ COVER_TEXT[24], UNFINISHED_TEXT[25] ^ COVER_TEXT[25], UNFINISHED_TEXT[26] ^ COVER_TEXT[26], UNFINISHED_TEXT[27] ^ COVER_TEXT[27], UNFINISHED_TEXT[28] ^ COVER_TEXT[28], UNFINISHED_TEXT[29] ^ COVER_TEXT[29], UNFINISHED_TEXT[30] ^ COVER_TEXT[30], UNFINISHED_TEXT[31] ^ COVER_TEXT[31], UNFINISHED_TEXT[32] ^ COVER_TEXT[32], UNFINISHED_TEXT[33] ^ COVER_TEXT[33], UNFINISHED_TEXT[34] ^ COVER_TEXT[34], UNFINISHED_TEXT[35] ^ COVER_TEXT[35], UNFINISHED_TEXT[36] ^ COVER_TEXT[36], UNFINISHED_TEXT[37] ^ COVER_TEXT[37], UNFINISHED_TEXT[38] ^ COVER_TEXT[38], UNFINISHED_TEXT[39] ^ COVER_TEXT[39], UNFINISHED_TEXT[40] ^ COVER_TEXT[40], UNFINISHED_TEXT[41] ^ COVER_TEXT[41], UNFINISHED_TEXT[42] ^ COVER_TEXT[42], UNFINISHED_TEXT[43] ^ COVER_TEXT[43], UNFINISHED_TEXT[44] ^ COVER_TEXT[44], UNFINISHED_TEXT[45] ^ COVER_TEXT[45], UNFINISHED_TEXT[46] ^ COVER_TEXT[46], UNFINISHED_TEXT[47] ^ COVER_TEXT[47], UNFINISHED_TEXT[48] ^ COVER_TEXT[48], UNFINISHED_TEXT[49] ^ COVER_TEXT[49] };
// FUNCTION: 0x004710c0 init:gUnfinishedKey


extern bool	gInFrontEnd;					// 00679b40, defined in FrontEnd.cpp; also set while drawing the end screen

WCHAR	*UnscrambleText(WCHAR *key, WCHAR *text);	// defined after DrawGameStuff, out of line in retail

//*******************************************************************************
void	draw_end_level_darkener(SINT alpha)
{
/*#if TARGET == PS2
	RENDERINFO.SetReorder( FALSE );
	RENDERINFO.SRS( RS_ZFUNC, ZFUNC_ALWAYS );
	RENDERINFO.SRS( RS_ALPHABLENDENABLE, TRUE );
#endif*/
	// retail: 20 pixels bigger each way (00471915: -10, -10, 680/64, 520/64)
	CSPRITERENDERER::DrawColouredSprite(-10,-10,0.001f,HUD.GetWhiteTexture(), alpha, (680.0f/64.0f), (520.0f/64.0f));
/*#if TARGET == PS2
	RENDERINFO.SetReorder( TRUE );
	RENDERINFO.SRS( RS_ZFUNC, ZFUNC_LEQUAL );
#endif*/
}

//*******************************************************************************

void	CGame::DrawGameStuff()	// FUNCTION: 0x004714c0
{
	static int drawcount=0;
	//*****************************************************
	//** Screen Grab? (PC only)
#if TARGET==PC
	// retail: B grabs a single frame under -testeur; under -autoconfigtest or with every cheat on, B asks for a
	// screen dump instead (004714d0..004715a4)
	if ((!CLIPARAMS.mAutoConfigTest) && (!gAllCheatsEnabled))
	{
	if(CLIPARAMS.mTestEur && PLATFORM.KeyOnce('B'))
	{
		static int frameno=0;
		if(frameno==0) CreateDirectory(ToTCHAR("grabs"),NULL);

		// Skip existing files

		bool framenumok=false;

		while (!framenumok)
		{
			char name[32];
			sprintf(name,"grabs\\scr%.4d.tga",frameno);
			FILE *f=fopen(name,"r");
			if (f)
			{
				fclose(f);
				frameno++;
			}
			else
				framenumok=TRUE;
		}

		LT.DumpScreen(frameno++);		
	}
	}
	else if (PLATFORM.KeyOnce('B'))
		RequestScreenDump(NULL, true);
#endif

	drawcount++;

//#if TARGET != XBOX
	if (drawcount>100)	// retail (004715ad)
	{
		char buf[256];
		sprintf(buf,"FPS : %2.2f\n",PLATFORM.GetFPS());
		TRACE(buf);
		if (CLIPARAMS.mAutoConfigTest || gAllCheatsEnabled)	// retail (004715e3)
			SETUP_HISTORY.AddMessage(buf);

		/*
#if TARGET==PS2
		sprintf(buf,"Draw Cycles : %d\n",SCENE.mDrawCycles);
		TRACE(buf);
		sprintf(buf,"Render Cycles : %d\n",SCENE.mRenderCycles);
		TRACE(buf);
		sprintf(buf,"Tris : %d\n",SCENE.mPrevTriCount);
		TRACE(buf);
		sprintf(buf,"\n");
		TRACE(buf);
#endif
		*/

		drawcount=0;
	}
//#endif


	//*****************************************************
	//** Assorted Debug stuff 

	if (CLIPARAMS.mDeveloperMode)
	{
		DBT.Out("World %d,  Time = %2.2f, Event list size = %d(%d) (proc last frame=%d), Num Things = %d, ",GetCurrentLevel(), EVENT_MANAGER.GetTime(), EVENT_MANAGER.TotalEvents(), CScheduledEvent::GetNumCreated(), EVENT_MANAGER.GetNumEventsProcessedInLastUpdate(), WORLD.GetThingNB().Size());

		if (mFixedFrameRate)
		{
			DBT.Out(", (fixed rate gc) %2.2f FPS ", PLATFORM.GetFPS());
		}
		else
		{
			DBT.Out("\n %2.2f FPS ", PLATFORM.GetFPS());
//			DBT.Out(" ");
		}

		CPlayer* p = GetPlayer(0) ;
		if (p)
		{
			if (p->IsGod())
			{
				DBT.Out("\nGOD mode Score = %0d", mScore);
			}
			else
			{
				CBattleEngine* player_unit = p->GetBattleEngine() ;
				{
					if(player_unit)
					{
						DBT.Out("\n Health = %2.2f Energy = %2.2f Score = %0d", player_unit->GetLife() , player_unit->GetEnergy(), mScore);
						
						if (player_unit->IsAugActive())
							DBT.Out("\n Aug Bar = %2.2f Aug Active", player_unit->GetAugValue());
						else
							DBT.Out("\n Aug Bar = %2.2f", player_unit->GetAugValue());
					}
				}
			}
		}
	}
	else
	{
//		DBT.Out("%2.2f FPS ", PLATFORM.GetFPS());
		DBT.Out(" ");
		CPlayer* p = GetPlayer(0) ;
		if (p)
		{
		
			if (p->IsGod())
			{
//				DBT.Out("\nGOD mode Score = %0d", mScore);
//				DBT.Out("\nGOD mode");
			}
		}
	}

	if (IsFreeCameraOn(0) && GetCamera(0) && GetPlayer(0))
	{
		CCamera* cam = GetCamera(0) ;
		FVector cam_pos = cam->GetPos() ;
		CEulerAngles cam_ori = CEulerAngles(cam->GetOrientation());
//		DBT.Out("\nx = %2.4f, y = %2.4f, z = %2.4f", cam_pos.X, cam_pos.Y, cam_pos.Z);
//		DBT.Out("\nyaw = %2.4f, pitch = %2.4f, roll = %2.4f", cam_ori.mYaw, cam_ori.mPitch, cam_ori.mRoll);
	}

#if TARGET == XBOX
	CXBOXVisibilityTester::DebugText();

#if _DEBUG
	// Display real time memory stats for XBOX
	DBT.Out( "\nDEFAULT Heap - Size: %0dMB / Peak: %0d", MEM_MANAGER.GetDefaultHeapSize() / (1024 * 1024), MEM_MANAGER.GetDefaultPeakSize() );
	DBT.Out( "\n                Used: %0d / Free: %0d", MEM_MANAGER.GetDefaultUsedSize(), MEM_MANAGER.GetDefaultHeapSize() - MEM_MANAGER.GetDefaultUsedSize() );
	DBT.Out( "\nTEXDATA Heap - Size: %0dMB / Peak: %0d", MEM_MANAGER.GetTexDataHeapSize() / (1024 * 1024), MEM_MANAGER.GetTexDataPeakSize() );
	DBT.Out( "\n                Used: %0d / Free: %0d", MEM_MANAGER.GetTexDataUsedSize(), MEM_MANAGER.GetTexDataHeapSize() - MEM_MANAGER.GetTexDataUsedSize() );
	DBT.Out( "\nVBDATA  Heap - Size: %0dMB / Peak: %0d", MEM_MANAGER.GetVBDataHeapSize() / (1024 * 1024), MEM_MANAGER.GetVBDataPeakSize() );
	DBT.Out( "\n                Used: %0d / Free: %0d", MEM_MANAGER.GetVBDataUsedSize(), MEM_MANAGER.GetVBDataHeapSize() - MEM_MANAGER.GetVBDataUsedSize() );
#endif

	WCHAR *w = ToWCHAR(DBT.GetPtrAndReset());

	if (ENGINE.GetDrawDebugStuff() & DRAW_MEM_MANAGER)
	{
		// Don't draw the debug text, it's over the top of the damn memory manager stats.
	}
	else
	{
		// so we can see the damn font.
		extern float back_buffer_scale;
		float scale;
		
		if (LT.GetFlipMethod() == CXBOXDX::FULL_SYNC_LATE_HUD || LT.GetFlipMethod() == CXBOXDX::FULL_SYNC_LATE_HUD_MY_COPY)
		{
			scale = 1.f;
		}
		else
		{
			scale = 1.f / back_buffer_scale;
		}

		PLATFORM.Font( FONT_DEBUG )->DrawTextScaled(41,31,0.0f,scale,scale,0xff000000,w,0L);
		PLATFORM.Font( FONT_DEBUG )->DrawTextScaled(40,30,0.0f,scale,scale,0xffffff80,w,0L);
	}

#else
	// retail: no debug text; the unfinished-version watermark blinks instead (0047179d)
	if (gShowUnfinishedWatermark && ((float)fmod(PLATFORM.GetSysTimeFloat(), 2.0) < 1.3f))
	{
		PLATFORM.Font( FONT_NORMAL )->DrawTextShadowed(50, 50, 0xffffffff,
			CLIPARAMS.mAutoConfigTest ? UnscrambleText(gAutoRunningKey, gWatermarkCoverA)
									  : UnscrambleText(gUnfinishedKey, gWatermarkCoverB));
	}
#endif

	DBT.GetPtrAndReset();

#if TARGET == PC
	LOG.Render();
#endif
 
	//!JCL-MAP
/*	if (!(MAP.GetGenerator()->AreShadowsValid()))
	{
		PLATFORM.DrawDebugText( 451, 461, 0xff000000, "Shadows out of date.", 0L );
		PLATFORM.DrawDebugText( 450, 460, 0xff7f7f7f, "Shadows out of date.", 0L );
	}*/

	// more hacks

	if (mGameState > GAME_STATE_PLAYING && mGameState < GAME_STATE_QUIT && mEndLevelCount >= 0.049f)
	{
		gInFrontEnd = true;	// retail (0047185a)

		int step = 16 ;
		int max = 0xA0 ;

		if (mFadeOut)
		{
			step = 1 ;
			max = 0xff ;
		}

		// retail: the fade keeps pace with the clock when frames are slow (00471884)
		static float lLastTime = 0.f;
		float	time = PLATFORM.GetSysTimeFloat();

		if (time > lLastTime)
		{
			float	delta = time - lLastTime;

			if (delta < 5.f)
			{
				int	frames = (int)(delta / 0.05f);

				if (frames > 1)
					step *= frames;
			}
		}
		lLastTime = time;

		mWinLoseScreenAlpha+=step;
		if (mWinLoseScreenAlpha>max) mWinLoseScreenAlpha=max;
	
		DWORD alpha=mWinLoseScreenAlpha<<24;

		// for debug purpose
		if (IsFreeCameraOn(0)==TRUE) 
		{
			alpha = 0;
		}


		RENDERINFO.SetFogEnabled(false);

		// erghh this is a hack.. what do we want?????
		if (mGameState == GAME_STATE_PLAYER_1_WON)
		{
			draw_end_level_darkener(alpha);

			SIZE	s;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT_DB.GetString(FETX_VICTORY), &s);
			PLATFORM.Font(FONT_NORMAL)->DrawTextShadowed(320.f - float(s.cx) * 0.5f, 50, 0xffffffff, TEXT_DB.GetString(FETX_VICTORY));
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT_DB.GetString(FETX_DEFEAT), &s);
			PLATFORM.Font(FONT_NORMAL)->DrawTextShadowed(320.f - float(s.cx) * 0.5f, 245, 0xffffffff, TEXT_DB.GetString(FETX_DEFEAT));
		}

		if (mGameState == GAME_STATE_PLAYER_2_WON)
		{
			draw_end_level_darkener(alpha);

			SIZE	s;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT_DB.GetString(FETX_DEFEAT), &s);
			PLATFORM.Font(FONT_NORMAL)->DrawTextShadowed(320.f - float(s.cx) * 0.5f, 50, 0xffffffff, TEXT_DB.GetString(FETX_DEFEAT));
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT_DB.GetString(FETX_VICTORY), &s);
			PLATFORM.Font(FONT_NORMAL)->DrawTextShadowed(320.f - float(s.cx) * 0.5f, 245, 0xffffffff, TEXT_DB.GetString(FETX_VICTORY));
		}

		if (mGameState == GAME_STATE_GAME_DRAWN)
		{
			draw_end_level_darkener(alpha);

			SIZE	s;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT_DB.GetString(JCL_DRAW), &s);
			PLATFORM.Font(FONT_NORMAL)->DrawTextShadowed(320.f - float(s.cx) * 0.5f, 50, 0xffffffff, TEXT_DB.GetString(FETX_DEFEAT));
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT_DB.GetString(JCL_DRAW), &s);
			PLATFORM.Font(FONT_NORMAL)->DrawTextShadowed(320.f - float(s.cx) * 0.5f, 245, 0xffffffff, TEXT_DB.GetString(FETX_VICTORY));
		}
	
		if (((mGameState==GAME_STATE_LEVEL_LOST) || (mGameState==GAME_STATE_LEVEL_WON)))
		{
			RENDERINFO.SetFogEnabled(false);
			if (mGameState==GAME_STATE_LEVEL_WON)
			{
				draw_end_level_darkener(alpha);

				SIZE	s;
				PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT_DB.GetString(FETX_VICTORY), &s);
				PLATFORM.Font(FONT_NORMAL)->DrawTextShadowed(320.f - float(s.cx) * 0.5f, 50, 0xffffffff, TEXT_DB.GetString(FETX_VICTORY));
			}
			else
			{
				draw_end_level_darkener(alpha);

				SIZE	s;
				PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT_DB.GetString(FETX_DEFEAT), &s);
				PLATFORM.Font(FONT_NORMAL)->DrawTextShadowed(320.f - float(s.cx) * 0.5f, 50, 0xffffffff, TEXT_DB.GetString(FETX_DEFEAT));
			}

			int x,y;
			char buffer[1024];

//			x=90;
			x=65;
			y=90;
			CPlayer *p=GetPlayer(0);

			if (mGameState==GAME_STATE_LEVEL_LOST)
			{
				SWordWrapFill	fill;
				int num_lines =0;
				if (mLevelLostReason == 0)
				{
					sprintf(buffer,"(ERROR) unknown game over reason");
					num_lines = PLATFORM.Font( FONT_SMALL )->WordWrap(&fill, ToWCHAR(buffer), 500.0f);
				}
				else
				{
					num_lines = PLATFORM.Font( FONT_SMALL )->WordWrap(&fill, TEXT_DB.GetString(mLevelLostReason), 500.0f);
				}

				SINT	c1;

				for (c1 = 0; c1 < num_lines; c1 ++)
				{
					SIZE s;
					PLATFORM.Font(FONT_SMALL)->GetTextExtent(fill.c[c1], &s);

					PLATFORM.Font(FONT_SMALL)->DrawTextShadowed((float)x, (float)y,  0xffFFFFFF,fill.c[c1]);
					y+=16 ;
				}
			
				y+=20;
			}

		
			SINT c0;
			BOOL found_p = FALSE ;
			for (c0 = 0; c0 < GAME.GetMaxPrimaryObjectives(); c0 ++)
			{
				if (GAME.GetPrimaryObjective(c0)->GetStatus() != MOS_NOT_DEFINED)
				{
					if (found_p==FALSE)
					{
						PLATFORM.Font( FONT_SMALL)->DrawTextShadowed((float)x,(float)y,0xffffa040, TEXT_DB.GetString(FETX_PRIMARY_OBJECTIVES));
						y+=16;
						found_p=TRUE ;
					}

					WCHAR* line_text = TEXT_DB.GetString(GAME.GetPrimaryObjective(c0)->GetStringID());

					SIZE	s;
					PLATFORM.Font( FONT_SMALL)->GetTextExtent(line_text, &s);
					PLATFORM.Font( FONT_SMALL)->DrawTextShadowed((float)x, (float)y, 0xffffffff,line_text);
			
					if (s.cx > 322)  // prevent overlap
						y += 16;

					BOOL complete = GAME.GetPrimaryObjective(c0)->IsComplete() ;

					WCHAR* mess = TEXT_DB.GetString(FETX_INCOMPLETE);
					DWORD col = 0xffff1f1f ;
			
					if (complete)
					{
						mess = TEXT_DB.GetString(FETX_COMPLETE);
						col = 0xff10ff1f ;
					}

				
					PLATFORM.Font( FONT_SMALL)->DrawTextShadowed((float)x+330, (float)y, col ,mess);
					y+=16;
				}
			}

			if (found_p) y+=8;

			BOOL found_s = FALSE ;
			for (c0 = 0; c0 < GAME.GetMaxSecondaryObjectives(); c0 ++)
			{
				if (GAME.GetSecondaryObjective(c0)->GetStatus() != MOS_NOT_DEFINED)
				{
					if (found_s==FALSE)
					{
						PLATFORM.Font( FONT_SMALL)->DrawTextShadowed((float)x,(float)y,0xffffa040,TEXT_DB.GetString(FETX_SECONDARY_OBJECTIVES));
						y+=16;
						found_s=TRUE;
					}
			
					WCHAR* line_text = TEXT_DB.GetString(GAME.GetSecondaryObjective(c0)->GetStringID());

					SIZE	s;
					PLATFORM.Font( FONT_SMALL)->GetTextExtent(line_text, &s);
					PLATFORM.Font( FONT_SMALL)->DrawTextShadowed((float)x, (float)y, 0xffffffff,line_text);
					
					if (s.cx > 322.f)  // prevent overlap (retail: a float compare, 004720d2)
						y += 16;
			
					BOOL complete = GAME.GetSecondaryObjective(c0)->IsComplete() ;

					WCHAR* mess = TEXT_DB.GetString(FETX_INCOMPLETE);
					DWORD col = 0xffff1f1f ;
			
					if (complete)
					{
						mess = TEXT_DB.GetString(FETX_COMPLETE);
						col = 0xff10ff1f ;
					}

					
					PLATFORM.Font( FONT_SMALL)->DrawTextShadowed((float)x+330, (float)y, col ,mess);
					y+=16;
				}
			}

			if (PLAYABLE_DEMO == 0)
			{
				DWORD my_c = 0xffffa040 ;

			//	x+=150;
				y+=30;

				float cc = 180 ;
				float nc = 200;

				// scan for longer strings
				for (c0 = 0; c0 < 6; c0 ++)
				{
					WCHAR	*str;

					switch (c0)
					{
					case 0:	str = TEXT_DB.GetString(TOTAL_MISSION_TIME);	break;
					case 1:	str = TEXT_DB.GetString(TIME_IN_JET_MODE);	break;
					case 2:	str = TEXT_DB.GetString(TIME_IN_WALKER_MODE);	break;
					case 3:	str = TEXT_DB.GetString(ROUNDS_FIRED);		break;
					case 4:	str = TEXT_DB.GetString(ACCURACY);			break;
					case 5:	str = TEXT_DB.GetString(DAMAGE_TAKEN);		break;
					};

					SIZE s;
					PLATFORM.Font(FONT_SMALL)->GetTextExtent(str, &s);

					if (s.cx > cc - 10)
					{
						cc = float(s.cx) + 10;
						nc = cc + 20;
					}
				}

/*				PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x,(float) y, my_c, TEXT_DB.GetString(TOTAL_MISSION_TIME));
				PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+cc,(float) y, my_c,ToWCHAR(":"));
				sprintf(buffer,"%.2fs",((float) p->GetStat(PS_TIMEASJET)+p->GetStat(PS_TIMEASWALKER))/20.0f);
				PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+nc,(float) y, my_c,ToWCHAR(buffer));
				y+=16;		
*/
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x,(float) y, my_c, TEXT_DB.GetString(TIME_IN_JET_MODE));
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+cc,(float) y, my_c,ToWCHAR(":"));
			//	sprintf(buffer,"%.2fs",((float) p->GetStat(PS_TIMEASJET))/20.0f);
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+nc,(float) y, my_c,ToWCHAR(buffer));
			//	y+=16;

			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x,(float) y, my_c, TEXT_DB.GetString(TIME_IN_WALKER_MODE));
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+cc,(float) y, my_c,ToWCHAR(":"));
			//	sprintf(buffer,"%.2fs",((float) p->GetStat(PS_TIMEASWALKER))/20.0f);
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+nc,(float) y, my_c,ToWCHAR(buffer));
			//	y+=16;

			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x,(float) y, my_c, TEXT_DB.GetString(ROUNDS_FIRED));
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+cc,(float) y, my_c,ToWCHAR(":"));
			//	sprintf(buffer,"%d",p->GetStat(PS_ROUNDSFIRED));
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+nc,(float) y, my_c,ToWCHAR(buffer));
			//	y+=16;

			//	if (p->GetStat(PS_ROUNDSFIRED)>0)
			//	{
			//		PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x,(float) y, my_c, TEXT_DB.GetString(ACCURACY));
			//		PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+cc,(float) y, my_c,ToWCHAR(":"));
			//		sprintf(buffer,"%.2f%%",((float) p->GetStat(PS_ROUNDSHIT)*100.0f) / ((float) p->GetStat(PS_ROUNDSFIRED)));
			//		PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+nc,(float) y, my_c,ToWCHAR(buffer));
			//		y+=16;
			//	}

			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x,(float) y, my_c, TEXT_DB.GetString(DAMAGE_TAKEN));
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+cc,(float) y, my_c,ToWCHAR(":"));
			//	sprintf(buffer,"%.2f",((float) p->GetStat(PS_DAMAGETAKEN)) / 256.0f);
			//	PLATFORM.Font( FONT_SMALL )->DrawTextShadowed((float) x+nc,(float) y, my_c,ToWCHAR(buffer));
			//	y+=16;

			//	if (p->GetStat(PS_CHEATED)>0)
			//	{
			//		PLATFORM.Font( FONT_NORMAL )->DrawTextShadowed((float) x,(float) y, 0x00FF0000,ToWCHAR("CHEATER!"));
			//		y+=16;
			//	}		
			}
		}
			
		RENDERINFO.SetFogEnabled(true);		

		gInFrontEnd = false;
	}

//	DBT.Out("\nFract=%f - time=%f, basetime=%f, emt=%f\n",GetFrameRenderFraction(),mFrameTime,mBaseTime,EVENT_MANAGER.GetTime());
}

//*******************************************************************************
WCHAR	*UnscrambleText(WCHAR *key, WCHAR *text)	// FUNCTION: 0x00472270 ?UnscrambleText@@YAPAGPAG0@Z
{
	static WCHAR	text_out[50];

	text_out[0] = key[0] ^ text[0];
	text_out[1] = key[1] ^ text[1];
	text_out[2] = key[2] ^ text[2];
	text_out[3] = key[3] ^ text[3];
	text_out[4] = key[4] ^ text[4];
	text_out[5] = key[5] ^ text[5];
	text_out[6] = key[6] ^ text[6];
	text_out[7] = key[7] ^ text[7];
	text_out[8] = key[8] ^ text[8];
	text_out[9] = key[9] ^ text[9];
	text_out[10] = key[10] ^ text[10];
	text_out[11] = key[11] ^ text[11];
	text_out[12] = key[12] ^ text[12];
	text_out[13] = key[13] ^ text[13];
	text_out[14] = key[14] ^ text[14];
	text_out[15] = key[15] ^ text[15];
	text_out[16] = key[16] ^ text[16];
	text_out[17] = key[17] ^ text[17];
	text_out[18] = key[18] ^ text[18];
	text_out[19] = key[19] ^ text[19];
	text_out[20] = key[20] ^ text[20];
	text_out[21] = key[21] ^ text[21];
	text_out[22] = key[22] ^ text[22];
	text_out[23] = key[23] ^ text[23];
	text_out[24] = key[24] ^ text[24];
	text_out[25] = key[25] ^ text[25];
	text_out[26] = key[26] ^ text[26];
	text_out[27] = key[27] ^ text[27];
	text_out[28] = key[28] ^ text[28];
	text_out[29] = key[29] ^ text[29];
	text_out[30] = key[30] ^ text[30];
	text_out[31] = key[31] ^ text[31];
	text_out[32] = key[32] ^ text[32];
	text_out[33] = key[33] ^ text[33];
	text_out[34] = key[34] ^ text[34];
	text_out[35] = key[35] ^ text[35];
	text_out[36] = key[36] ^ text[36];
	text_out[37] = key[37] ^ text[37];
	text_out[38] = key[38] ^ text[38];
	text_out[39] = key[39] ^ text[39];
	text_out[40] = key[40] ^ text[40];
	text_out[41] = key[41] ^ text[41];
	text_out[42] = key[42] ^ text[42];
	text_out[43] = key[43] ^ text[43];
	text_out[44] = key[44] ^ text[44];
	text_out[45] = key[45] ^ text[45];
	text_out[46] = key[46] ^ text[46];
	text_out[47] = key[47] ^ text[47];
	text_out[48] = key[48] ^ text[48];
	text_out[49] = key[49] ^ text[49];

	return text_out;
}

//*******************************************************************************
BOOL CGame::DoWeWantMesh(char *mesh)	// FUNCTION: 0x00472570
{
	if (stricmp(mesh,mSettings.mPlayer1CockpitMesh)==0)
		return(TRUE);
	if (stricmp(mesh,mSettings.mPlayer2CockpitMesh)==0)
		return(TRUE);
	if (stricmp(mesh,mSettings.mWingmanMesh)==0)
		return(TRUE);

	return(FALSE);
}

//*******************************************************************************
BOOL CGame::IsMultiplayer()	// FUNCTION: 0x004725d0
{
	// SRG changed cos world is loaded after we have set most things up
	if (mCurrentlyRunningLevel >849 && mCurrentlyRunningLevel < 900)return TRUE  ;

	return FALSE;

}

//*******************************************************************************
SINT CGame::GetPlayerLives(	// FUNCTION: 0x004725f0
	SINT		inPlayer)
{
	if (inPlayer==1)
		return mPlayer1Lives;
	else if (inPlayer==2)
		return mPlayer2Lives;

	return 0;
}

//*******************************************************************************
void CGame::SetPlayerLives(	// FUNCTION: 0x00472620
	SINT		inPlayer,
	SINT		inLives)
{
	if (inPlayer==1)
		mPlayer1Lives=inLives;
	else if (inPlayer==2)
		mPlayer2Lives=inLives;
}

//*******************************************************************************
BOOL CGame::IsRunningResources()	// FUNCTION: 0x00472650
{
#if TARGET == PS2
	return TRUE;
#else
	if (CResourceAccumulator::GetLastGameLevelLoaded()==mCurrentLevel)
		return TRUE;
#endif

	return FALSE;
}
//*******************************************************************************
int CGame::GetNumPrimaryObjectives()	// FUNCTION: 0x00472670
{
	int i = 0 ;
	int found = 0 ;
	for (i=0 ;i < mPrimaryObjectives.Size();i++)
	{
		if (mPrimaryObjectives[i].GetStatus() != MOS_NOT_DEFINED) found++;
	}
	return found ;
}


//*******************************************************************************
int CGame::GetNumSecondaryObjectives()	// FUNCTION: 0x00472690
{
	int i = 0 ;
	int found = 0;
	for (i=0 ;i < mSecondaryObjectives.Size();i++)
	{
		if (mSecondaryObjectives[i].GetStatus() != MOS_NOT_DEFINED) found++;
	}
	return found ;
}
//******************************************************************************************
//******************************************************************************************
//** JCL Credits stuff.

// sorry this is all a bit of a hack...

class	CGameCreditControlHandler : public IController
{
public:
	CGameCreditControlHandler() : mWantsToQuit(FALSE) {};

	void	ResetQuitFlag() {mWantsToQuit = FALSE;};
	BOOL	WantToQuit() {return mWantsToQuit;};

	void ReceiveButtonAction(CController* from_controller, int button, float ana_val)
	{
		if (button==BUTTON_SKIP_CUTSCENE)
			mWantsToQuit = TRUE;
	};
	virtual BOOL			CanBeControlledWhenInPause() {return TRUE;}
	virtual EControlType    GetControlType() {return CONTROL_FRONTEND;}

protected:
	BOOL	mWantsToQuit;

};

void	CGame::RollCredits()	// FUNCTION: 0x004726b0
{
	float	start_time = PLATFORM.GetSysTimeFloat();
	
	CGameCreditControlHandler gcch;

	// retail: two controllers (00472701, 00472742)
#line 4542
	CCONTROLLER	* pController0 = new(MEMTYPE_CONTROLLER) CCONTROLLER(&gcch,0);
	CCONTROLLER	* pController1 = new(MEMTYPE_CONTROLLER) CCONTROLLER(&gcch,1);

	pController0->Flush();
	pController1->Flush();

	gcch.ResetQuitFlag();

	BOOL	mQuit = FALSE;

	if (CLIPARAMS.mMusic)
		MUSIC.PlaySelection(MUS_CREDITS);

	while (!mQuit)
	{
		PLATFORM.Process();

		pController0->Flush();
		pController1->Flush();

		if (PLATFORM.BeginScene())
		{
			PLATFORM.ClearScreen(0x00000000);
			LT.SetModulate(0);	// retail (004727cf)
			RENDERINFO.Apply();

			if (!(CREDITS.RenderCredits(PLATFORM.GetSysTimeFloat() - start_time, 255)))
				mQuit = TRUE;

			MUSIC.UpdateStatus();

			PLATFORM.EndScene();
			PLATFORM.Flip();

		}
		mQuit |= gcch.WantToQuit();
	}

	gcch.Shutdown();

	delete pController0;
	delete pController1;

	if (CLIPARAMS.mMusic)
		MUSIC.Stop();
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00472890 ?ReceiveButtonAction@CGameCreditControlHandler@@UAEXPAVCController@@HM@Z
