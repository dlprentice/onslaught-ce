// SoundManager.h: Stuart's, with the PC build's changes marked "PC" and their retail evidence. SOUND is a
// CPCSoundManager at 00896988.

#ifndef SOUND_MANAGER_H
#define SOUND_MANAGER_H

#include "activereader.h"
#include "console.h"
#include "audiblething.h"

class CDebugMarker;
class CSample;
class CCamera;


//---------------------------------------------------------------------------

// Default volume

#define DEFAULT_SOUND_VOLUME (0.7f)

// Sound fade distances

#define FAR_SOUND	50
#define	NEAR_SOUND	3

// Tracking types

enum ESoundTrackingType
{
	ST_NOTRACKING,				// Don't track anything (play at camera)
	ST_SETINITIALPOSITION,		// Set the initial position from an object, but don't move after that
	ST_FOLLOWANDDIE,			// Follow an object, stop when it dies
	ST_FOLLOWDONTDIE			// Follow an object, keep playing at last pos when it dies
};


enum ESoundType
{
	ST_MENU_SOUND,
	ST_GAME_SOUND
};

//---------------------------------------------------------------------------

class	CEffect
{
public:
	CEffect()
	{
		mChainedEffect=NULL;
		mNextEffect=mFirstEffect;
		mFirstEffect=this;
	}

	~CEffect()
	{
		SAFE_DELETE(mChainedEffect);
		CEffect *c=mFirstEffect;
		CEffect *l=NULL;
		while (c)
		{
			if (c==this)
			{
				if (l)
					l->mNextEffect=mNextEffect;
				else
					mFirstEffect=mNextEffect;
			}
			l=c;
			c=c->mNextEffect;
		}
	}

	static void DeleteAllEffects() ;

	char		mName[64];
	char		mSample[64];
	char		mLowpassSample[64];
	int			mVolume;
	int			mFalloff;
	int			mPitchVariance;
	bool		mLanguageDependent;
	BOOL		mLooping;
	CEffect		*mChainedEffect;
	CEffect		*mNextEffect;
	static CEffect *mFirstEffect;
	
	static CEffect *GetEffectByName(char *name,int num);
	static void	LoadSFXFile(char *filename);
	static void	PrecacheAll();
private:
	static void	ReadLine(class CMEMBUFFER *mb,char *line);
};


//---------------------------------------------------------------------------

enum ESampleType 
{
	SAMT_MONO,
	SAMT_LEFT,
	SAMT_RIGHT,
};

class	CSample
{
public:	
	CSample()
	{
		mType=SAMT_MONO;
		mNextSample=NULL;
	}

	virtual ~CSample();
	
	ESampleType	mType;
	char		mName[100];
	BOOL		mMusic;
	BOOL		mLanguageDependent;
	
	CSample	*mNextSample;
};

//---------------------------------------------------------------------------

class CSoundEvent
{
public:
	CSoundEvent();
	~CSoundEvent();

	CActiveReader<IAudibleThing>	mOwner;
	int						mChannel;
	bool					mPlaying;
	CSample					*mSample;
	ESoundTrackingType		mTrack;
	ESoundType				mSoundType;
	bool					mLooping;
	float					mMasterVolume;
	float					mSubVolume;
	float					mFade;
	float					mFadeDest;
	float					mTime;
	float					mStartPoint;
	int						mPan;
	float					mPitchMultiplier;
	float					mDesiredPitchMultiplier;
	int						mPitchFadeTime;
	FVector					mPos;
	FVector					mVelocity;
	int						mCurrentVolume;
	int						mCurrentAttenuatedVolume;
	float					mEndPoint;			// -1 = Keep playing to end
	CDebugMarker			*mDebugMarker;
	CSoundEvent				*mNextEvent;
	CSoundEvent				*mPrevEvent;
	BOOL					mInformOwnerWhenComplete;
	BOOL					mIgnoreOwnerPos;
	BOOL					mPaused;
};

//---------------------------------------------------------------------------

class CSoundManagerDebugMenu : public CConsoleMenu
{
	virtual void	GetName(char *name)		{ strcpy(name,"Sound manager"); };
	virtual int		GetNumEntries();
	virtual bool	GetShowSubmenus()		{ return(true); };
	virtual void	GetEntry(int num, char *name);
	virtual void	OnClick(int num)		{};	
};

//---------------------------------------------------------------------------

class CSoundManager
{
public:
	CSoundManager()
	{
		mInitialised=false;
		mLanguageSampleBank[0]=0;	// PC (SOUND's initializer 005168b1)
	}
			bool	Init();
			void	Shutdown();
			void	Reset();

			// PC: reload finds the sample and loads it again over itself (the language's samples; 004e0a70)
			CSample	*GetSample(char *sample_name,BOOL music=FALSE,bool reload=false);	// 004e0a00

			CEffect	*GetEffectByName(char *name,int num=0);
			void	PlayNamedEffect(char *name, IAudibleThing *thing, float volume = DEFAULT_SOUND_VOLUME, ESoundTrackingType track = ST_NOTRACKING, bool once = false, float fade = 0,float frompoint = 0.0f,float topoint = -1.0f,bool repeat=false,float pitch=1.0f);
	  
			// PC: volume defaults to 1 (CFrontEnd::PlaySound 00468770 pushes it)
			void	PlayEffect(CEffect *effect, IAudibleThing *thing, float volume = 1.0f, ESoundTrackingType track = ST_FOLLOWDONTDIE, bool once = false, float fade = 0,float frompoint = 0.0f,float topoint = -1.0f,bool repeat=false,float pitch=1.0f, ESoundType sound_type =ST_GAME_SOUND,
						  BOOL ignore_owner_pos=FALSE);	// PC: the last argument goes to PlaySample's ignore_owner_pos (004e1a43..004e1a50)
			BOOL	IsEffectPlaying(CEffect *effect ,IAudibleThing *thing);

			// PC: thing may be left out (CFEPLanguageTest::PlaySample 0051c050)
			void	PlayNamedSample(char *sample, IAudibleThing *thing = NULL, float volume = DEFAULT_SOUND_VOLUME, ESoundTrackingType track = ST_FOLLOWDONTDIE, bool once = false, float fade = 0,float frompoint = 0.0f,float topoint = -1.0f,bool repeat=false,float pitch=1.0f, bool inform_owner_when_complete=FALSE, bool ignore_owner_pos = FALSE, ESoundType sound_type =ST_GAME_SOUND);
			void	PlaySample(CSample *sample, IAudibleThing *thing, float volume = DEFAULT_SOUND_VOLUME, ESoundTrackingType track = ST_FOLLOWDONTDIE, bool once = false, float fade = 0,float frompoint = 0.0f,float topoint = -1.0f,bool repeat=false,float pitch=1.0f, bool inform_owner_when_complete=FALSE, bool ignore_owner_pos = FALSE, ESoundType sound_type =ST_GAME_SOUND);

			BOOL	IsSamplePlaying(char *sample, IAudibleThing *thing);
			void	StopSample(char *sample, IAudibleThing *thing = NULL);	// PC: thing may be left out (CFEPLanguageTest::Shutdown)
			
			CSoundEvent *GetSoundEventForThing(char *sample, IAudibleThing *thing);

			CSoundEvent *StartSoundEvent(IAudibleThing *thing,CSample *sample,ESoundTrackingType track,float volume,float fade,float frompoint,float topoint,bool loop,float pitch, bool inform_owner_when_complete = FALSE, bool ignore_owner_pos = FALSE, ESoundType sound_type =ST_GAME_SOUND);
			void	StopSoundEvent(CSoundEvent *event,BOOL blockuntilstopped=FALSE);
			void	SetPitch(CSoundEvent *e,float desiredpitchfactor,float fadetime=0);
	
			void	KillSamplesForThing(IAudibleThing *thing);
			void	KillSample(IAudibleThing *thing, const CSample *sample);
			void	KillAllInstancesOfSample(const CSample *sample);
			void	FadeAllSamples();
			void	KillAllSamples();
			void	PauseAllSamples() ;
			void	UnPauseAllSamples();
	
			void	FadeTo(const CSample *sample, float fadeval, float speed = 0.2f, IAudibleThing *t = NULL);
			void	UpdateVolumeForAllSoundEvents() ;

			void	UpdateStatus();
	
			SINT	GetVolumeForPos(FVector pos, float* the_dist);
			SINT	CalculatePan(FVector p);	// PC: the event's camera-local position (StartSoundEvent 004e0cf1, UpdateSoundPosition 004e177f)

			void	GetDebugMenuText(int num,char *text);

			int		GetNumSoundEvents() { return mSoundEvents; };
			
			CSoundEvent *GetFirstSoundEvent() { return mFirstSoundEvent; };

			void	DeleteSoundEvent(CSoundEvent *s);
			void	SortEventList();

			void	UpdateSoundPosition(CSoundEvent *se,BOOL firsttime);

			CSample *GetFirstSample() { return(mFirstSample); };
			
			BOOL	IsInitialised() { return(mInitialised); };

			void	SetFirstSample(CSample *s) { mFirstSample=s; };

			float   GetMasterVolume() { return mMasterVolume ; }
			void	SetMasterVolume(float val) ;

			float   GetGameSoundsMasterVolume() { return mGameSoundsMasterVolume ; }
			void	SetGameSoundsMasterVolume(float val) { mGameSoundsMasterVolume = val ; }

			float   GetMenuSoundsMasterVolume() { return mGameSoundsMasterVolume ; }
			void	SetMenuSoundsMasterVolume(float val) { mMenuSoundsMasterVolume = val ; }


			float	GetRadioMessageVolume() { return(mRadioMessageVolume); };
			float	GetHUDMessageVolume() { return(mHUDMessageVolume); };

			// PC: the language's sample bank, reloaded when the language changes
			bool	LanguageChanged(char *bank);		// 004e2bb0: true when the language's bank differs; bank gets its name
			void	ReloadLanguageSampleBank();			// 004e2c50

protected:	
	SINT			Fade(SINT v, CSoundEvent *event);
	CSoundEvent*	GetSoundEvent(BOOL insertattop);
	CSample			*CreateSample(char *name,BOOL music,CMEMBUFFER *datastream,bool reload);	// PC: reload (004e0890)

	void			DeleteAllSamples();

	CSample			*mFirstSample;
	bool			mInitialised;
	int				mSoundEvents;
	CSoundEvent		*mFirstSoundEvent;
	CSoundManagerDebugMenu *mDebugMenu;
	float			mLastProcessTime;
	BOOL			mVisibleSounds;
	BOOL			mSoundsFrozen;
	float			mMasterVolume;
	float			mGameSoundsMasterVolume;
	float			mMenuSoundsMasterVolume;
	float			mRadioMessageVolume;
	float			mHUDMessageVolume;
	
	CSoundEvent		*mSoundEventPool;

	FVector			mCameraPosition;
	FVector			mOldCameraPosition;
	FMatrix			mCameraOrientation;

	char			mLanguageSampleBank[0x34];	// PC +0x88: the loaded bank's name (LanguageChanged 004e2bec; size open)
};


#if	TARGET == PC

	#include	"PCSoundManager.h"
	extern class CPCSoundManager	SOUND;

#elif TARGET == PS2

	#include	"PS2SoundManager.h"
	extern class CPS2SoundManager	SOUND;

#elif TARGET == XBOX

#include "XBOXSoundManager.h"
extern class CXBOXSoundManager  SOUND;

#endif

#endif
