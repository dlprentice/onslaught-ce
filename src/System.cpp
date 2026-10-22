// System.cpp: reconstructed from System.obj (004ef9c0..004f0535); Stuart's release has no copy. The game's top level:
// start everything, show the opening films, then alternate the front end and levels; and the stress test.

#include "common.h"
#define SYSTEM_CPP
#include "surf.h"

CDXSurf		SURF;								// 0083d3d0: defined first, so its initializer runs first
// FUNCTION: 0x004ef9c0 init:SURF
// FUNCTION: 0x004ef9e0 exit:SURF

#include "mathsconst.h"
// FUNCTION: 0x004ef9f0 init:ZERO_FVECTOR
// FUNCTION: 0x004efa10 init:ID_FMATRIX

#include "System.h"
#include "tweak.h"
#include "CLIParams.h"
#include "Platform.h"
#include "SPtrSet.h"
#include "DebugLog.h"
#include "Controller.h"
#include "PCController.h"
#include "reconnectinterface.h"
#include "console.h"
#include "fmv.h"
#include "SoundManager.h"
#include "music.h"
#include "Texture.h"
#include "text.h"
#include "ResourceAccumulator.h"
#include "MemoryCard.h"
#include "mesh.h"
#include "WorldPhysicsManager.h"
#include "frontend.h"
#include "game.h"
#include "DXEngine.h"
#include "VertexShader.h"
#include "VBufTexture.h"
#include "FastVB.h"
#include "eventmanager.h"
#include "landscape.h"
#include "ParticleTexture.h"

CTweakSLONG	TWIMTBP("TWIMTBP", 0);				// 0083d3f8: show the "The way it's meant to be played" film
// FUNCTION: 0x004efae0 init:TWIMTBP
// FUNCTION: 0x004efb00 exit:TWIMTBP

static CDXTexture	*sLoadingScreen;				// 0083d3e0
static CDXTexture	*sBlank;						// 0083d3e4
static CMesh	*sDefaultMesh;					// 0083d3e8
CDXTexture	*gFMVSplash;							// 0083d3ec: the splash a film fades to (DXFMV.cpp)
bool		gKeyPressed;						// 0083d3f0: set by any key press (ltshell.cpp, mouse.cpp)
SINT		PLAYABLE_DEMO;						// 0083d448
static bool	sFirstFrontEnd;						// 0083d44c (retail .bss: zero, so Run never takes its first-entry branch)
static BOOL	sOpeningFMV;						// 0083d450: the opening film was played through
SINT		performing_stress_test;				// 0083d454
static SINT	sStressIteration;					// 0083d458

extern CTweakSLONG	GEFORCE_FX_POWER;			// 0082b498 (PauseMenu.cpp)
extern bool		OPTIONS_LOADED;					// 008554a4 (FrontEnd.cpp)
extern SINT		FMV_SKIP_FRAMES;				// 00650610: frames before the splash fades in over a film (DXFMV.cpp)
void	ApplyControlPreset(int preset);			// 00453780 (FEPControls.cpp)
void	InitRenderData();						// 00515fb0 (PCRTID.cpp)
void	ShutdownRenderData();					// 00516450 (PCRTID.cpp)
void	ReleaseMouseCursor();					// 00523b30

//******************************************************************************************
BOOL	CSystem::Init()	// FUNCTION: 0x004efb10
{
	if (!GEFORCE_FX_POWER.mValue)
		EXTRA_GEFORCE_FX_FEATURES.Set(0);

	float	start = PLATFORM.GetSysTimeFloat();
	GenericSPtrSet::Init(40000);
	LOG.Reset();
	SETUP_HISTORY.Reset();

	{
		CPCController	c0(&RECONNECT_INTERFACE[0], 1, TRUE);
		CPCController	c1(&RECONNECT_INTERFACE[1], 1, TRUE);
	}

	CONSOLE.Init();
	SETUP_HISTORY.AddMessage("Console initialised");

	if (!OPTIONS_LOADED)
		ApplyControlPreset(1);

	if (!PLATFORM.Init())
		return FALSE;

	TRACE("Platform init took %fs\n", PLATFORM.GetSysTimeFloat() - start);
	start = PLATFORM.GetSysTimeFloat();
	CController::ResetInactivityTimer();
	FMV.Init();
	TRACE("FMV init took %fs\n", PLATFORM.GetSysTimeFloat() - start);
	start = PLATFORM.GetSysTimeFloat();

	if (CLIPARAMS.mSound)
	{
		SETUP_HISTORY.AddMessage("Starting music");
		SETUP_HISTORY.AddMessage("Starting sound");
		if (!SOUND.Init())
		{
			CLIPARAMS.mSound = FALSE;
			CLIPARAMS.mMusic = FALSE;
		}
		MUSIC.Initialise();
		SETUP_HISTORY.AddMessage("Done");
	}

	if (CLIPARAMS.mLevelNo == -1 && CLIPARAMS.mGoStraightToDeviceSelectScreen == -1 && !CLIPARAMS.mSkipFMV)
	{
		bool	attract = CLIPARAMS.mAttractMode != 0;
		CLIPARAMS.mAllowManualTextures = true;
		if (E3_BUILD)
			gFMVSplash = CDXTexture::FindTexture("vectorlosttoyssplash.tga", 0, 0, 1, 1, 1);
		else
		{
			gFMVSplash = CDXTexture::FindTexture("splash.tga", 0, 0, 1, 1, 1);
			CLIPARAMS.mAllowManualTextures = false;
		}
		sOpeningFMV = FALSE;
		TRACE("Considering doing fmvs...\n");

		if (attract)
		{
			TRACE("we're in attract mode");
			if (!FMV.PlayFullscreen("ltlogo", TRUE, FALSE, TRUE, FALSE, FALSE, TRUE))
			{
				if (!TWIMTBP.mValue || !FMV.PlayFullscreen("TWIMTBP_GefFX_640x480_Audio", TRUE, FALSE, TRUE, FALSE, FALSE, TRUE))
				{
					FMV_SKIP_FRAMES = 1934;
					FMV.PlayFullscreen("openingfmv", TRUE, FALSE, TRUE, FALSE, FALSE, TRUE);
					FMV_SKIP_FRAMES = 0x7fffffff;
				}
			}
		}
		else if (!CLIPARAMS.mPal)
		{
			FMV.PlayFullscreen("copyright", FALSE, FALSE, TRUE, FALSE, TRUE, FALSE);
			FMV.PlayFullscreen("atari", FALSE, FALSE, TRUE, TRUE, TRUE, TRUE);
			FMV.PlayFullscreen("lostlogo", FALSE, FALSE, TRUE, TRUE, TRUE, TRUE);
			FMV.PlayFullscreen("introntsc", FALSE, FALSE, TRUE, TRUE, FALSE, TRUE);
		}
		else
		{
			FMV.PlayFullscreen("ltlogo", FALSE, FALSE, TRUE, TRUE, TRUE, TRUE);
			if (!CLIPARAMS.mAutoConfigTest)
			{
				if (TWIMTBP.mValue)
					FMV.PlayFullscreen("TWIMTBP_GefFX_640x480_Audio", FALSE, FALSE, TRUE, TRUE, FALSE, TRUE);
				FMV_SKIP_FRAMES = 1934;
				sOpeningFMV = FMV.PlayFullscreen("openingfmv", FALSE, FALSE, TRUE, TRUE, FALSE, TRUE);
				FMV_SKIP_FRAMES = 0x7fffffff;
			}
		}

		if (gFMVSplash)
		{
			gFMVSplash->CTexture::Release();
			gFMVSplash->ForceDelete();
		}
	}

	TEXT.Init();
	if (gAmericanText)
		TEXT.Load(0);
	else
		TEXT.Load(CLIPARAMS.mLanguage);
	CFrontEndText::Init();

	if (!CLIPARAMS.mBuildResources)
		CResourceAccumulator::ReadResources(-1, NULL, FALSE);

	PLATFORM.InitFonts();
	CONSOLE.ResetLayout();
	CONSOLE.SetLoading(true, TRUE, FALSE);
	CONSOLE.SetLoadingRange(0, 40.f);
	CONSOLE.SetLoadingFraction(0);
	CONSOLE.SetLoadingRange(40.f, 50.f);
	CONSOLE.SetLoadingFraction(0.1f);
	MEMORYCARD.Initialise();
	InitRenderData();
	CONSOLE.SetLoadingFraction(0.2f);
	CONSOLE.SetLoadingFraction(0.3f);

	if (sDefaultMesh)
	{
		sDefaultMesh->Release();
		sDefaultMesh = NULL;
	}
	sDefaultMesh = CMesh::GetMesh("default.msh", 1);
	CONSOLE.SetLoadingFraction(0.4f);

	if (sBlank)
	{
		sBlank->CTexture::Release();
		sBlank = NULL;
	}
	sBlank = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Blank.tga", 0, 0, 1, 1, 1);
	if (sLoadingScreen)
	{
		sLoadingScreen->CTexture::Release();
		sLoadingScreen = NULL;
	}
	sLoadingScreen = CDXTexture::FindTexture("loadingscreen.tga", 0, 0, 1, 1, 1);
	CONSOLE.SetLoadingFraction(0.5f);

	if (!PLAYABLE_DEMO)
		MEMORYCARD.Initialise();
	CMesh::InitAll();
	CONSOLE.SetLoadingFraction(0.6f);
	mNextLevel = -1;
	UPhysicsManager::Reload();
	CONSOLE.SetLoadingFraction(1.f);
	MEM_MANAGER.FlagAsBaseSet();
	return TRUE;
}

//******************************************************************************************
void	CSystem::Shutdown()	// FUNCTION: 0x004f00e0
{
	UPhysicsManager::ClearAndFreeAll();
	CONSOLE.SetLoading(false, TRUE, FALSE);
	TEXT.Shutdown();
	MEM_MANAGER.SetMerge(FALSE);
	ShutdownRenderData();

	if (sDefaultMesh)
	{
		sDefaultMesh->Release();
		sDefaultMesh = NULL;
	}
	if (sBlank)
	{
		sBlank->CTexture::Release();
		sBlank = NULL;
	}
	if (sLoadingScreen)
	{
		sLoadingScreen->CTexture::Release();
		sLoadingScreen = NULL;
	}
	CMesh::ShutdownAll();

	if (CLIPARAMS.mSound)
	{
		MUSIC.Shutdown();
		SOUND.Shutdown();
	}
	MEMORYCARD.Shutdown();
	PLATFORM.Shutdown();
	CONSOLE.Shutdown();
	ReleaseMouseCursor();
	ENGINE.ReleaseDefaults();
	CVertexShader::ClearOut();
	CVBufTexture::FreeAllResources();
	CMesh::ShutdownAll();
	CMesh::ReleaseAll();
	CDXTexture::ClearOut();
	FASTVB.Destroy();
	EVENT_MANAGER.Shutdown();
	CDXLandscape::mDamage.Shutdown();
	GenericSPtrSet::Shutdown();
}

//******************************************************************************************
// the levels in turn, forever, with the memory reported around each
static SINT	sStressLevels[] = { 100, 110, 200, 221, 231, 311, 400, 431, -1 };	// 00632b04

void	StressTest(SINT on)	// FUNCTION: 0x004f0200
{
	char	name[256];
	char	text[256];

	performing_stress_test = on;
	sStressIteration = 0;
	SINT	count = 0;
	while (sStressLevels[count] != -1)
		count++;

	SINT	c0 = 0;
	for (;;)
	{
		sprintf(text, "**** STRESS TEST ****\n** Iteration %d\n", ++sStressIteration);
		TRACE(text);
		SINT	level = sStressLevels[c0];
		FRONTEND.Run(FEE_START, FALSE);
		MEM_MANAGER.LogDebugStats();
		sprintf(name, "Before level %d", level);
		MEM_MANAGER.DumpMemory(name);
		FRONTEND.mFEPMultiplayerStart.mPort[1] = 1;	// player two's controller port again (008a1815, the page at FRONTEND +0x40b8)
		GAME.RunLevel(level);
		sprintf(name, "After level %d", level);
		MEM_MANAGER.DumpMemory(name);
		MEM_MANAGER.LogDebugStats();
		sprintf(text, "Deltas for level %d\n", level);
		TRACE(text);
		MEM_MANAGER.CalcAndShowDeltas();
		if (sStressLevels[++c0] == -1)
			c0 = 0;
	}
}

//******************************************************************************************
SINT	CSystem::Run()	// FUNCTION: 0x004f0330
{
	if (CLIPARAMS.mStressTest > 0)
		StressTest(CLIPARAMS.mStressTest);

	SINT	result = 0;
	if (CLIPARAMS.mLevelNo != -1)
	{
		SINT	level = CLIPARAMS.mLevelNo;
		mNextLevel = level;
		while (1)
		{
			if (mNextLevel == -1)
			{
				mNextLevel = level;
				break;
			}
			level = mNextLevel;
			mNextLevel = -1;
			if (GAME.RunLevel(level) == QT_QUIT_TO_SYSTEM)
				break;
		}
	}
	else
	{
		for (;;)
		{
			SINT	entry;
			if (result != -69)
				entry = (result == -3);
			else
				entry = 2;
			if (sFirstFrontEnd)
			{
				sFirstFrontEnd = false;
				entry = 0;
			}

			MEM_MANAGER.LogDebugStats();
			result = FRONTEND.Run((EFrontEndEntry)entry, TRUE);
			MEM_MANAGER.LogDebugStats();
			MEM_MANAGER.CalcAndShowDeltas();
			if (CLIPARAMS.mStressTest > 0)
				StressTest(CLIPARAMS.mStressTest);

			if (result == -1)
				break;

			if (result == -3)
			{
				CLIPARAMS.mAllowManualTextures = true;
				if (E3_BUILD)
					gFMVSplash = CDXTexture::FindTexture("vectorlosttoyssplash.tga", 0, 0, 1, 1, 1);
				else
				{
					gFMVSplash = CDXTexture::FindTexture("splash.tga", 0, 0, 1, 1, 1);
					CLIPARAMS.mAllowManualTextures = false;
				}
				sOpeningFMV = FALSE;
				TRACE("Considering doing fmvs...\n");
				TRACE("we're in attract mode");
				if (!FMV.PlayFullscreen("ltlogo", TRUE, FALSE, FALSE, FALSE, FALSE, TRUE))
				{
					if (!TWIMTBP.mValue || !FMV.PlayFullscreen("TWIMTBP_GefFX_640x480_Audio", TRUE, FALSE, FALSE, FALSE, FALSE, TRUE))
					{
						FMV_SKIP_FRAMES = 1934;
						FMV.PlayFullscreen("openingfmv", TRUE, FALSE, FALSE, FALSE, FALSE, TRUE);
						FMV_SKIP_FRAMES = 0x7fffffff;
					}
				}
				if (gFMVSplash)
				{
					gFMVSplash->CTexture::Release();
					gFMVSplash->ForceDelete();
				}
			}
			else
			{
				MEM_MANAGER.LogDebugStats();
				EQuitType	quit = GAME.RunLevel(result);
				MEM_MANAGER.LogDebugStats();
				MEM_MANAGER.CalcAndShowDeltas();
				if (quit == QT_QUIT_TIMEOUT)
					result = -3;
				else if (quit == QT_USER_QUIT_TO_TITLE_SCREEN)
					result = -69;
			}
		}
	}
	return result;
}
// STAND-IN (link layout): retail's 16 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_System[16] = { 0 };
