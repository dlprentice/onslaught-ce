// game.h: the game object, GAME, a CDXGame at 008a9a98 (its constructor 00541f10 calls CGame's, 0046c210; vtables
// CGame 005dbbb4 and CDXGame 005e509c). Stuart's game.h: its layout is the retail one (MAX_PLAYERS 4, the settings at
// +0x11c with the meshes at +0x12c/+0x22c/+0x25e, the players at +0x2a4, controllers +0x2b4, cameras +0x2c4, the
// random stream +0x304, the render fraction +0x3ac, mLastSize +0x3c0).

#ifndef	GAME_H
#define	GAME_H

//*******************************************************************************

#include	"Platform.h"
#include	"Texture.h"
#include	"Controller.h"
#include	"activereader.h"

#include    "career.h"
#include    "player.h"
#include	"MissionObjective.h"

class CHelpTextDisplay ;
class CPauseMenu ;
class CSquad ;
class CUnit ;
class CMessageBox ;
class CMessageLog ;
class CLevelBriefingLog ;
class CEvent ;
class CPCController ;
#define CMESSAGEBOX CMessageBox
#define CCONTROLLER CPCController

#include "SeedUniformRandomNumberStream.h"

#define MAX_PLAYERS 4
#define MAX_PRIMARY_OBJECTIVES 10
#define MAX_SECONDARY_OBJECTIVES 10

enum EGameEvent
{
	DEMO_RESTART_LEVEL = 2000,
	FINISHED_PRE_RUN,
	FINISHED_PANNING,
	RESPAWN_PLAYER_1,
	RESPAWN_PLAYER_2,
	PAUSE_GAME,
	CONTINUE_FADE_OUT_GAME_SOUNDS
};

enum EGameState
{
	GAME_STATE_NOT_RUNNING,		// you're in the frontend or something.
	GAME_STATE_PRE_RUNNING,		// game code runs a bit before the visuals start
	GAME_STATE_PANNING,			// initial panning
	GAME_STATE_PLAYING,			// game is playing
	GAME_STATE_LEVEL_LOST,		// level lost	(single player mode or co op multy player mode
	GAME_STATE_LEVEL_WON,		// level won	(single player mode or co op multy player mode)
	GAME_STATE_PLAYER_1_WON,	// multi player
	GAME_STATE_PLAYER_2_WON,	// multi player
	GAME_STATE_GAME_DRAWN,		// multi player
	GAME_STATE_QUIT
};

enum EWingmanType
{
	kTaraWingman=0,
	kBillyWingman,
	kJasonWingman,
};

struct SFrontEndSettings
{
	BOOL			mInvertSides;

	SINT			mPlayer1Configuration,mPlayer2Configuration;
	EWingmanType	mWingman;

	char			mWingmanMesh[256];
	char			mPlayer1CockpitMesh[50],mPlayer2CockpitMesh[50];
};

class	CCamera;
class	CPlayer;
class	CFearGrid;

#define MAX_UPDATE_TIME 1.0f / 40.0f  // i.e. max 40 fps from game code

//*******************************************************************************

class	CGame : public IController
{
	//***************************************************************************
	// CGame methods
	//***************************************************************************

public:
	CGame();
	~CGame();
					
	// main entrypoint for game:
	EQuitType		RunLevel(SINT level);

	void			ToggleControlMethod() { mControlMode++ ; if (mControlMode > 1 ) mControlMode = 0 ;} 
	CCamera		  *	GetCamera(int number);
	void			SetCamera(int number,CCamera *cam);
	
//	void			SetQuit(EQuitType quit) {mQuit = quit; if (mQuit > QT_NONE) mGameState = GAME_STATE_QUIT ;};
	void			SetQuit(EQuitType quit) {mQuit = quit; if ((mQuit > QT_NONE) && (mGameState <= GAME_STATE_PLAYING)) mGameState = GAME_STATE_QUIT ;};


	CFearGrid	  *	GetForsetiFearGrid()		{ return mForsetiFearGrid; }
	CFearGrid	  *	GetMuspellFearGrid()		{ return mMuspellFearGrid; }

	void			CalcRenderFrameFraction();
	float			GetFrameRenderFraction() { return mFraction; }	// retail: inline (+0x3ac, 008a9e44)
	float			GetFrameRenderFraction2();

	SINT			Random() { return mRandomStream->Next(); }
	float			FloatRandom() { return ((float)(Random() % 65536)) / 65536.0f ; } // returns number between 0..1


	CPlayer		  *	GetPlayer(SINT no) { return mPlayer[no];} 
	CCamera		  *	GetCurrentCamera(int number) { return mCurrentCamera[number]; }
	void			SetCurrentCamera(int number,CCamera *c,bool releaseoldcamera = true);
	void			ToggleFreeCameraOn(int playernumber);
	void			ToggleFreeCameraOff(int playernumber);
	int				GetCurrentLevel() { return mCurrentLevel ; }
	void			DeclarePlayerDead(int number);
	void			DeclareLevelWon() ;
	void			MPDeclarePlayerWon(int number); // for multiplayer
	void			MPDeclareGameDrawn() ;// for multiplayer
	void			DeclareLevelLost(int reason = 0, BOOL player_dead = FALSE) ;
	virtual void	ReceiveButtonAction(CController* from_controller, int button, float val) ;	// 0046f7e0
	virtual BOOL	CanBeControlledWhenInPause() { return TRUE ; }
	virtual		    EControlType  GetControlType() { return CONTROL_GAME ; }
	void			Pause(BOOL toggle_pause_menu = FALSE, CController* from_controller = NULL); 
	void			UnPause() ; 
	BOOL			IsPaused() { return mPause; }
	CMESSAGEBOX	  *	GetMessageBox() { return mMessageBox; } 
	CMessageLog	  *	GetMessageLog() { return mMessageLog; } 
	CHelpTextDisplay*	GetHelpTextDisplay() { return mHelpTextDisplay; } 
	CLevelBriefingLog* GetLevelBriefingLog() { return mLevelBriefingLog ; }
	CPauseMenu*		GetPauseMenu() { return mPauseMenu ; }
	CCONTROLLER	  *	GetController(int number);
	void			DrawDebugStuff();
	void			PreRender();
	void			HandleEvent(CEvent* event);
	int				GetNPlayers() { return mPlayers; }	
	void			Render( SINT num_renders );

	EGameState		GetGameState() { return mGameState ; }
	BOOL			IsFreeCameraOn(int player) { return mFreeCameraOn[player]; }
	BOOL			IsGameFinished() { return mGameState > GAME_STATE_PLAYING ; }
	void			StartPanState();
	void			StartPlayingState();
	
	ULONG			GetRenderFrameNumber() { return mRenderFrameNumber; }
	void			SetPrimaryObjectiveComplete(int num, int string_id) { mPrimaryObjectives[num].Set(MOS_COMPLETE, string_id); }
	void			SetSecondaryObjectiveComplete(int num, int string_id) { mSecondaryObjectives[num].Set(MOS_COMPLETE, string_id);}
	void			SetPrimaryObjectiveFailed(int num, int string_id) { mPrimaryObjectives[num].Set(MOS_FAILED, string_id); }
	void			SetSecondaryObjectiveFailed(int num, int string_id) { mSecondaryObjectives[num].Set(MOS_FAILED, string_id);}

	CMissionObjective*	GetPrimaryObjective(int num) { return &mPrimaryObjectives[num] ; }
	CMissionObjective*	GetSecondaryObjective(int num) { return &mSecondaryObjectives[num] ; }
	int				GetMaxPrimaryObjectives() { return mPrimaryObjectives.Size() ; }
	int				GetMaxSecondaryObjectives() { return mSecondaryObjectives.Size() ; }
	int				GetNumPrimaryObjectives() ;
	int				GetNumSecondaryObjectives() ;

	void			SetPanTime(float time)			{ mPanTime = time ; }
	void			SetPreRunTime(float time)		{ mPreRunTime = time ; }

	float			GetPreRunTime()					{ return mPreRunTime; }
	float			GetPanTime()					{ return mPanTime; }

	void			SetSGradeScore(SINT score)		{ mSGradeScore = score; }
	void			SetDGradeScore(SINT score)		{ mDGradeScore = score; }

	void			SetTimeLimit(float time)				{ mTimeLimit = time ; }
	void			SetFullScoreTime(float time)			{ mFullScoreTime = time ; }
	void			SetPercentageScoreTime(float time)		{ mPercentageScoreTime = time; }
	void			SetScorePercentage(float percentage)	{ mScorePercentage=percentage; }

	SINT			GetPlayerLives(SINT inPlayer);
	void			SetPlayerLives(
						SINT		inPlayer,
						SINT		inLives);

	void			IncScore(SINT inScore)			{ mScore+=inScore; }
	BOOL			GetAllowedAutoAim()				{ return mAllowedAutoAim ; }
	void			SetAllowedAutoAim(BOOL val)		{ mAllowedAutoAim = val ;  } 
	
	BOOL			IsMultiplayer();						// 004725d0
	void			PlayMusicForCurrentLevel();				// 0046dc00, retail addition (public: ReinitialiseSound 004cde1f calls it)
	BOOL			InvertSides()					{ return mSettings.mInvertSides; }

	BOOL			IsRunningResources();					// 00472650

	BOOL			DoWeWantMesh(char *mesh);

	void			RespawnPlayer(SINT inNumber);
	CSArray<int, MAX_CAREER_SLOTS>&	GetSlots() {return 	mSlots; }
	void			SetSlot(int num, BOOL val);
	BOOL			GetSlot(int num);

	float			GetLevelStartTime()				{ return mLevelStartTime; }
	void			SetLevelStartTime(float inTime)	{ mLevelStartTime=inTime; }
	BOOL			IsRestarting() { return mRestarting ; }

protected:


	BOOL			InitRestartLoop();
	void			ShutdownRestartLoop();
	BOOL			LoadResources(SINT aLevel,BOOL inLoadedSounds);
	void			InitOneOffResources();
	void			InitRestartResources();
	void			PreRun() ;
	EQuitType		RestartLoopRunLevel(SINT alevel);

	BOOL			Init();
	void			FillOutEndLevelData() ;
	BOOL			LoadLevel( SINT level );
	BOOL			PostLoadProcess();
	void			MainLoop();
	void			Update();
	void			ProcessDebug();
public:
	void			Shutdown();
	void			InitResources();
	void			DrawGameStuff();

	//***************************************************************************
	// CGame members
	//***************************************************************************

public:
	BOOL			mFixedFrameRate;
	float			mLastUpdateTime;
	int				mControlMode ;

	ULONG			mRenderFrameNumber ;
	ULONG			mUpdateFrameNumber;
	float			GetRenderFrameLength() {return mRenderFrameLength;};
	int				GetCurrentInterleavedScreen() { return mCurrentInterleavedScreen; };
	BOOL			IsFirstFrame() {return mIsFirstFrame;}; // of the interpolated render sequence

	SFrontEndSettings*	GetFrontEndSettings()		{ return &mSettings; }
	int				GetCurrentlyRunningLevelNum() { return mCurrentlyRunningLevel ; }
	float			GetFrameTime()	{ return mFrameTime ; }
	float			GetBaseTime() { return mBaseTime; }
	BOOL			mSupervisorMode;
protected:
	BOOL			mAllowedAutoAim;

	// Game state:
	BOOL			mRestarting ;
	EGameState		mGameState;
	BOOL			mPause;
	int				mCurrentLevel;
	EQuitType		mQuit;
	static bool		mFirstInit;
	bool			mHorizontalSplitscreen;
	BOOL			mInterleavedSplitscreen;
	BOOL			mFullscreenMultiplayer;
	int				mCurrentInterleavedScreen;
	float			mEndLevelCount; 
	CSArray<CMissionObjective, MAX_PRIMARY_OBJECTIVES> mPrimaryObjectives ;
	CSArray<CMissionObjective, MAX_SECONDARY_OBJECTIVES> mSecondaryObjectives ;

	float			mPreRunTime,mPanTime;
	SINT			mScore,mSGradeScore,mDGradeScore;
	BOOL			mPausedAllGameSounds;
	float			mTimeLimit,mFullScoreTime,mPercentageScoreTime,mScorePercentage;
	int				mLevelLostReason;
	float			mLevelStartTime;

	SFrontEndSettings	mSettings;

	SINT			mPlayer1Lives,mPlayer2Lives;
	float			mHackCurrentGameMasterVolume;// used to fade out game sounds when you die

	// Game objects:
	int				mPlayers;
	int				mCurrentlyRunningLevel ;
	CPlayer		  *	mPlayer[ MAX_PLAYERS ];
	CCONTROLLER	  *	mController[ MAX_PLAYERS ];
	CCamera		  *	mCurrentCamera[ MAX_PLAYERS ];
	CCamera		  *	mOldCamera[ MAX_PLAYERS ];
	CFearGrid	  *	mForsetiFearGrid, * mMuspellFearGrid;
	CMESSAGEBOX	  *	mMessageBox;
	CMessageLog	  * mMessageLog;
	CPauseMenu	  * mPauseMenu;
	CHelpTextDisplay* mHelpTextDisplay;
	CLevelBriefingLog * mLevelBriefingLog;
//	CTEXTURE	  *	mWinScreen;
//	CTEXTURE	  *	mLoseScreen;
	int				mWinLoseScreenAlpha;
	CSeedUniformRandomNumberStream		*mRandomStream;
	CSArray<int, MAX_CAREER_SLOTS>	mSlots;

	float			mRenderFrameLength;   // JCL - how long this this render frame?
	float			mLastRenderFrameTime;
	BOOL			mIsFirstFrame;
	BOOL			mFadeOut;

	// Performance stats:
	float			mFrameTime, mFrameTime2, mBaseTime;
	float			mLastRenderLength;
	float			mFrameLength;

	float			mFraction, mFraction2;

	int				mRetryCount;	// used by stress test

	// Data recording:
#ifdef DEBUG_TIMERECORDS
	TimeRecord		mTimeRecord[ TIMERECORDS ];
	int				mCurrentTR;
	RenderRecord	mRenderRecord[ TIMERECORDS ];
	int				mCurrentRR;
#endif

	//***************************************************************************
	// CGame debug
	//***************************************************************************

public:
	CCamera *mRemoteCamera;
	CCamera *mOldRemoteCamera;
	
	UINT		mLastSize[MEMTYPE_LIMIT];
	BOOL		mUsedMem[MEMTYPE_LIMIT];
	int			mSizeDelta[MEMTYPE_LIMIT];

protected:
	BOOL			mAdvanceFrame;
	BOOL			mFreeCameraOn[MAX_PLAYERS];
	BOOL			mPauseOnWhenStartedFreeCam[MAX_PLAYERS];
	BOOL			mShowDataSizes;
	BOOL			mShowMemDeltas;

	void			ToggleDebugSquadForward() ;
	void			ToggleDebugSquadBackward() ;
	void			ToggleDebugUnitForward() ;
	void			ToggleDebugUnitBackward();

	CActiveReader<CSquad>	mCurrentDebugSquad ;
	int						mCurrentDebugSquadNum ;
	int						mCurrentDebugUnitNum ;
	CActiveReader<CUnit>	mCurrentDebugUnit ;


	friend class CPCCutsceneEditor;
	friend class CEditorRenderer;

	BOOL			mFirstTimeRound;
	SINT			GetIntroFMV();
	void			RunIntroFMV();
	void			RunOutroFMV();
	void			RollCredits();
};

//******************************************************************************************

inline void		CGame::CalcRenderFrameFraction()
{
	mFraction=(mFrameTime - mBaseTime) / mFrameLength;

	if (mFraction<0.0f)
		mFraction=0.0f;
	if (mFraction>1.0f)
		mFraction=1.0f;

	mFraction2=(mFrameTime2 - mBaseTime) / mFrameLength;

	if (mFraction2<0.0f)
		mFraction2=0.0f;
	if (mFraction2>1.0f)
		mFraction2=1.0f;
}

//******************************************************************************************
inline float	CGame::GetFrameRenderFraction2() 
{
	return mFraction2;
}

//*******************************************************************************

class CDXGame : public CGame
{
};

extern CDXGame GAME;

#endif
