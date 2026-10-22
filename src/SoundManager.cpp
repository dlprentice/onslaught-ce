// Platform-independant sound manager
#define SOUNDMANAGER_CPP
#include	"Common.h"	// FUNCTION: 0x004dfe40 init:ZERO_FVECTOR
#include	"Game.h"	// FUNCTION: 0x004dfe60 init:ID_FMATRIX
#include	"SoundManager.h"
#include	"audiblething.h"
#include	"profile.h"
#include	"Camera.h"
#include	"console.h"
#include	"debugmarker.h"
#include    "debuglog.h"
#include	"text.h"
#include	"CLIParams.h"	// retail: GetSample (004e0a42)

#include <stdio.h>
#include <string.h>

CEffect *CEffect::mFirstEffect=NULL;


#define	SM_VOLUME				1.0f

#define MAX_SOUND_EVENTS		256

//---------------------------------------------------------------------------

// FUNCTION: 0x004dffc0 ??_GCSample@@UAEPAXI@Z
CSample::~CSample()	// FUNCTION: 0x004dff30
{
	SOUND.KillAllInstancesOfSample(this);

	SOUND.UnloadSample(this);
	
	CSample *current=SOUND.GetFirstSample();
	CSample *last=NULL;
	
	while ((current) && (current!=this))
	{
		last=current;
		current=current->mNextSample;
	}
	
	if (last)
	{
		last->mNextSample=mNextSample;
	}
	else
		SOUND.SetFirstSample(mNextSample);
}

//---------------------------------------------------------------------------

void con_playsnd(char *cmd)	// FUNCTION: 0x004e0040
{
	if (strlen(cmd)>10)
		SOUND.PlayNamedSample(&cmd[10],NULL);	
}


//---------------------------------------------------------------------------

// FUNCTION: 0x004e02d0 ?GetName@CSoundManagerDebugMenu@@EAEXPAD@Z
bool	CSoundManager::Init()	// FUNCTION: 0x004e00d0
{
	mFirstSoundEvent=NULL;

	mFirstSample = NULL;

	mGameSoundsMasterVolume = 1.0f ;
	mMenuSoundsMasterVolume = 1.0f ;

	SetMasterVolume(CAREER.GetSoundVolume());

#if TARGET!=PS2
	CEffect::LoadSFXFile("data\\sounds\\sounds.sfx");
#else
	CEffect::LoadSFXFile("data\\sounds\\PS2sounds.sfx");
#endif

	mSoundEventPool=NULL;

	for (int i=0;i<MAX_SOUND_EVENTS;i++)
	{
#line 81
		CSoundEvent *n=new (MEMTYPE_SOUND) CSoundEvent;

		n->mNextEvent=mSoundEventPool;
		n->mPrevEvent=NULL;
		if (mSoundEventPool)
			mSoundEventPool->mPrevEvent=n;
		mSoundEventPool=n;
	}

	mDebugMenu=new( MEMTYPE_SOUND ) CSoundManagerDebugMenu();
	CONSOLE.GetRootMenu()->AddSubmenu(mDebugMenu);

	mLastProcessTime=PLATFORM.GetSysTimeFloat();
	
	mVisibleSounds=FALSE;
	mSoundsFrozen=FALSE;
	mInitialised = TRUE;

#if TARGET==PS2
	mRadioMessageVolume=0.70f;
	mHUDMessageVolume=0.45f;
#else
	mRadioMessageVolume=0.42f;
	mHUDMessageVolume=0.45f;
#endif

	CONSOLE.RegisterVariable("snd_frozen","Are sounds frozen?",CVar_bool,&mSoundsFrozen);
	CONSOLE.RegisterVariable("snd_visible","Are sounds visible?",CVar_bool,&mVisibleSounds);
	CONSOLE.RegisterVariable("snd_radiomessagevolume","The radio message volume",CVar_float,&mRadioMessageVolume);	
	CONSOLE.RegisterVariable("snd_hudmessagevolume","The HUD message volume",CVar_float,&mHUDMessageVolume);	
	CONSOLE.RegisterCommand("playsound","Play the named sample or stream",&con_playsnd);
	
	if (!SOUND.DeviceInit())
	{
		mInitialised = false;	// retail (004e029c)
		return(false);
	}

	/*	
	PlayNamedSample("FFIX.adpcm",NULL);

	while (1)
	{
		PLATFORM.Flip();
		Sleep(1000);
	}
*/
	return(true);
}


//---------------------------------------------------------------------------
void CSoundManager::UpdateVolumeForAllSoundEvents()	// FUNCTION: 0x004e0300
{
	CSoundEvent *se=mFirstSoundEvent;	
	while (se)
	{
		float the_dist;
		SINT	avol = 127 ;
		if (se->mTrack != ST_NOTRACKING)
		{
			avol = GetVolumeForPos(se->mPos, &the_dist);
		}

		SINT	vol = 127;

		vol = Fade(vol, se);
		avol=Fade(avol,se);
		se->mCurrentVolume=vol;
		se->mCurrentAttenuatedVolume=avol;
		if (se->mPlaying && se->mChannel>=0) // Check we haven't killed it yet
		{
			SOUND.UpdateSound(se);
		}
		se = se->mNextEvent ;
	}
}

//---------------------------------------------------------------------------
void	CSoundManager::SetMasterVolume(float val)	// FUNCTION: 0x004e04c0
{ 
#if 1	// retail (004e04c0): the linear volume; Stuart has #if TARGET==PS2
	mMasterVolume=val;
#else
	// SRG erghh convert 0...1 into non linear sound volume.     
	float	t1 = ((float)tan((1.0-val)*1.38f)) ;
	static float t2 = ((float)tan(1.38f));
	
	mMasterVolume = 1.0f - (t1 / t2);
#endif	 

	LOG.AddMessage("sound master volume = %2.8f", mMasterVolume) ;

	CAREER.SetSoundVolume(val) ; 

	UpdateVolumeForAllSoundEvents();
}


//---------------------------------------------------------------------------

void	CSoundManager::DeleteAllSamples()	// FUNCTION: 0x004e06b0
{
	CSample	*s = mFirstSample;
	
	while (s)
	{
		CSample *next = s->mNextSample;
		delete s;
		s = next;
	};

	mFirstSample = NULL;
}
	
//---------------------------------------------------------------------------

// FUNCTION: 0x004e0820 ??_GCEffect@@QAEPAXI@Z
void	CSoundManager::Shutdown()	// FUNCTION: 0x004e06e0
{

	CSoundEvent *s=mFirstSoundEvent;
	while (s)
	{
		CSoundEvent *next=s->mNextEvent;
		DeleteSoundEvent(s);
		s=next;
	}

	mFirstSoundEvent=NULL;

	CSoundEvent *c=mSoundEventPool;

	while (c)
	{
		CSoundEvent *n=c->mNextEvent;
		
		SAFE_DELETE(c);

		c=n;
	}
	
	DeleteAllSamples();
	
	SOUND.DeviceShutdown();
	
	SAFE_DELETE(mDebugMenu);

	CEffect::DeleteAllEffects() ;

	mInitialised=false;
}

//---------------------------------------------------------------------------

void	CSoundManager::Reset()
{
	CSoundEvent *s=mFirstSoundEvent;
	while (s)
	{
		CSoundEvent *next=s->mNextEvent;
		DeleteSoundEvent(s);
		s=next;
	}

	mFirstSoundEvent=NULL;
	
	SOUND.DeviceReset();
}

//---------------------------------------------------------------------------

CSample *CSoundManager::CreateSample(char *name,BOOL music,CMEMBUFFER *datastream,bool reload)	// FUNCTION: 0x004e0890
{
	CSample *s=NULL;
	CSample *old=NULL;	// PC

	char	fname[256];
	if (!music)
		sprintf(fname, "sounds\\%s", name);
	else
		sprintf(fname, "music\\%s", name);
	
	// PC: a reload replaces the sample of the same name (004e08de..004e0916)
	if (datastream)
	{
		if (reload)
		{
			for (old=mFirstSample; old; old=old->mNextSample)
				if (!stricmp(old->mName,name))
					break;
		}
		s = SOUND.LoadSampleFromBuffer(datastream,music,old);
	}
	else
		s = SOUND.LoadNewSample(fname,music);
	
	if (s)
	{
		s->mType = SAMT_MONO;
		
		s->mMusic=music;
		s->mLanguageDependent=FALSE;
		
		ASSERT(strlen(name) < sizeof(s->mName) - 1 && "You need to make CSample::mName bigger");
		strncpy(s->mName,name, sizeof(s->mName) - 1);
		s->mName[sizeof(s->mName) - 1] = 0;
		if (!old)
		{
			s->mNextSample=mFirstSample;
			mFirstSample=s;
		}
		
		if (strlen(s->mName) > 5)
		{
			if (!stricmp(s->mName + strlen(s->mName) - 2, "_L"))
				s->mType = SAMT_LEFT;
			if (!stricmp(s->mName + strlen(s->mName) - 2, "_R"))
				s->mType = SAMT_RIGHT;
		}
	}
	else
	{
		LOG.AddMessage("ERROR: Failed to create sample for '%s'", name) ;
	}

	return(s);
}

//---------------------------------------------------------------------------

#if TARGET == XBOX
bool allowed_to_load_sample = false;
#else
// PC: samples come from the language's bank unless resources are being built (004e0a42..004e0a52)
#define allowed_to_load_sample	(CLIPARAMS.mBuildResources || (!CLIPARAMS.mUseSampleBank))
#endif

// The PC build restructures Stuart's early returns into nested tests with one return NULL at the end: every failure
// shares that epilogue (004e0a85), and the reload path goes back to the load (004e0a7a). Retail's callers call it out
// of line (PlayEffect 004e1940, PlayNamedSample 004e0a90, 004e0040) where this form would be inlined, so
// auto_inline(off) stands in for whatever kept it out.
#pragma auto_inline(off)
CSample	*CSoundManager::GetSample(char *sample_name,BOOL music,bool reload)	// FUNCTION: 0x004e0a00
{
	if (mInitialised)
	{
		if (strlen(sample_name)>=1)
		{
			// do we have this already?
			CSample	*s = mFirstSample;
			CSample	*old = NULL;

			while (s)
			{
				if (!(stricmp(sample_name, s->mName)))
				{
					if (reload)
					{
						old = s;	// PC: load it again (004e0a70)
						break;
					}
					// Return existing sample
					return(s);
				}
				s = s->mNextSample;
			};

			if (allowed_to_load_sample)
			{
				// Create and load sample

				return CreateSample(sample_name,music,NULL,old != NULL);
			}
		}
	}
	return(NULL);
}
#pragma auto_inline(on)

//---------------------------------------------------------------------------

void	CSoundManager::PlayNamedSample(char *sample_name, IAudibleThing *thing, float volume, ESoundTrackingType track, bool once, float fade, float frompoint, float topoint,bool repeat,float pitch, bool inform_owner_when_complete, bool ignore_owner_pos, ESoundType sound_type)	// FUNCTION: 0x004e0a90
{
	if (!mInitialised)
	{
		LOG.AddMessage("ERROR: Could not play sample '%s' as manager is not initilised");
		return;
	}

	CSample *s = GetSample(sample_name);

	if (s)
	{
		PlaySample(s, thing, volume, track, once, fade,frompoint,topoint,repeat,pitch, inform_owner_when_complete, ignore_owner_pos, sound_type);
	}
	else
	{
		LOG.AddMessage("ERROR: PlayNamedSample failed to find sample for '%s'", sample_name);
	}
}

//---------------------------------------------------------------------------

void CSoundManager::PlaySample(CSample *sample, IAudibleThing *thing, float volume, ESoundTrackingType track, bool once, float fade, float frompoint, float topoint,bool repeat,float pitch, bool inform_owner_when_complete, bool ignore_owner_pos, ESoundType sound_type)	// FUNCTION: 0x004e0b30
{
	PROFILE_FN(PlaySound);
	
	// Plays from frompoint to topoint (both specified in seconds)
	// If topoint==-1, plays to end of sample

	if (!mInitialised)
		return;

	if (mMasterVolume == 0)	// PC (004e0b3e)
		return;
	// if game is pre running then don't generate sounds. But if not running at all (ie in front end), do generate them.
	if (GAME.GetGameState() == GAME_STATE_PRE_RUNNING && repeat == FALSE) return ;

	if (once)
	{
		// check that this sample isn't already being played

		CSoundEvent *s=mFirstSoundEvent;

		while (s)
		{
			if ((s->mPlaying) &&
				(s->mSample == sample) &&
				((s->mOwner.ToRead() == NULL) ||
				(s->mOwner.ToRead() == thing)))
				return;
			s=s->mNextEvent;
		}			
	}

/*	if (thing)
	{
		SINT	v = GetVolumeForThing(thing);
		if (v == 0)
			return; // too far away anyway...
	}*/
	
	StartSoundEvent(thing,sample,track,volume,fade,frompoint,topoint,repeat,pitch, inform_owner_when_complete, ignore_owner_pos, sound_type);
}

//---------------------------------------------------------------------------

SINT CSoundManager::GetVolumeForPos(FVector pos, float* the_dist)
{
	CCamera *cam=GAME.GetCamera(0);

	FVector cpos;
/*	if (cam)
		cpos=cam->GetPos();
	else*/
		cpos.Set(0,0,0);
	
	float	dist = (cpos - pos).Magnitude();

/*	if (GAME.IsMultiplayer())
	{
		// Check if player 2 is closer

		cam=GAME.GetCamera(1);
		
		if (cam)
		{
			cpos=cam->GetPos();

			float	dist2 = (cpos - pos).Magnitude();

			if (dist2<dist)
				dist=dist2;
		}
	}*/
		

/*	SINT	vol = 25 - SINT(dist);
	if (vol > 20) vol = 20;
	if (vol < 0 ) vol = 0;
	vol *= 6;*/

	float	fvol = FAR_SOUND - dist;

	if (fvol > FAR_SOUND)  fvol = FAR_SOUND;
	if (fvol < 0)			fvol = 0;

	SINT	vol = SINT((fvol * 100) / FAR_SOUND);

	*the_dist = dist;

	return vol;
}

//---------------------------------------------------------------------------

CSoundEvent *CSoundManager::StartSoundEvent(IAudibleThing *thing,CSample *sample,ESoundTrackingType track,float volume,float fade,float frompoint,float topoint,bool loop,float pitch, bool inform_owner_when_complete, bool ignore_owner_pos, ESoundType sound_type)	// FUNCTION: 0x004e0bd0
{
	CCamera *cam = GAME.GetCamera(0);

	CSoundEvent *event;
	 
	if ((!thing) || (ignore_owner_pos)) /// ARGH! NOOOoooooo! (to fix messagebox sounds)
		event=GetSoundEvent(TRUE);	// Default cockpit sounds to the top of the effect list
	else
		event=GetSoundEvent(FALSE);
	
	if (event)
	{
		event->mOwner.SetReader(thing);
		event->mSample 	= sample;
		event->mMasterVolume  = volume;
		event->mTime = 0;
		event->mChannel=SOUND.FindFreeChannel();
		event->mLooping=loop;
		event->mPitchFadeTime=0;
		event->mDesiredPitchMultiplier=pitch;
		event->mPitchMultiplier=pitch;
		event->mInformOwnerWhenComplete = inform_owner_when_complete;
		event->mIgnoreOwnerPos = ignore_owner_pos;
		event->mPaused = FALSE ;
		event->mSoundType = sound_type;
		event->mPlaying=TRUE;
		
		if (!thing)
			event->mTrack = ST_NOTRACKING;
		else
			event->mTrack = track;
		
		if (fade == 0)
		{
			event->mFade = 0;
			event->mSubVolume = 1;
		}
		else
		{
			if (fade < 0)
			{
				event->mSubVolume = 1;
				event->mFade = fade;
				event->mFadeDest = 0;
			}
			else
			{
				event->mSubVolume = 0;
				event->mFade = fade;
				event->mFadeDest = 1;
			}
		}
		
		event->mStartPoint=frompoint;
		event->mEndPoint=topoint;
		if (thing && ignore_owner_pos == FALSE)
		{
			event->mPan=CalculatePan(event->mPos);
		}
		else
			event->mPan = 0;

		UpdateSoundPosition(event,true);

		float dist;
		SINT avol = GetVolumeForPos(event->mPos, &dist);
		
		// SRG early out ??
		if ((dist >=  FAR_SOUND) && (!event->mLooping))
		{
			DeleteSoundEvent(event);
			return NULL;
		}

		SINT vol = 127;
		if (event->mTrack==ST_NOTRACKING)
			avol=127;
		vol = Fade(vol, event);
		avol = Fade(avol, event);
		event->mCurrentVolume=vol;
		event->mCurrentAttenuatedVolume=avol;

//		printf("Starting sound event %s on channel %d\n",sample->mName,event->mChannel);

		if (event->mChannel>=0)
		{
			SOUND.PlaySound(event);
		}
	}
	
	return(event);
}

//---------------------------------------------------------------------------

void CSoundManager::StopSoundEvent(CSoundEvent *event,BOOL blockuntilstopped)	// FUNCTION: 0x004e0f70
{
	if (event->mInformOwnerWhenComplete == TRUE &&
		event->mOwner.ToRead() != NULL)
	{
		event->mOwner->SampleFinishedPlaying(event) ;
	}

	if (event->mChannel>=0)
	{
		SOUND.StopSound(event,blockuntilstopped);
	}
	event->mPlaying=false;
	event->mOwner.SetReader(NULL);	
}



//---------------------------------------------------------------------------

SINT CSoundManager::CalculatePan(FVector p)	// PC: the position is passed in, and neither camera nor thing is tested
{
	float	angle, sn;
//		FVector	p = (theThing->GetSoundPos() - theCam->GetPos());

//		p = theCam->GetOrientation().Transpose() * p;

//		FVector p = theThing->GetSoundPos();
	
	angle = atan2f(p.X, p.Y);
	sn = sinf(angle);

	float sign = (sn > 0) ? 1.f : -1.f;

	sn *= 10;
	sn *= sn;
	sn *= sn;
	sn *= sign;

	return SINT(sn);
}

//---------------------------------------------------------------------------

CSoundEvent* CSoundManager::GetSoundEvent(BOOL insertattop)	// FUNCTION: 0x004e0fb0
{
	CSoundEvent *s=mSoundEventPool;

	if (!s)
	{
		TRACE("Warning : out of sound events!\n");	// retail calls the empty trace (004e0fbf); Stuart: printf
		return(NULL);
	}
	
	mSoundEventPool=s->mNextEvent;

	if (mSoundEventPool)
		mSoundEventPool->mPrevEvent=NULL;

	s->mNextEvent=NULL;

	if (mFirstSoundEvent)
	{
		CSoundEvent *current=mFirstSoundEvent;

		if (!insertattop)
		{			
			// Insert new sound events at the boundary between channel-allocated sounds
			// and non-allocated sounds
			
			while ((current->mNextEvent) && (current->mChannel>=0))
				current=current->mNextEvent;
		}

		s->mNextEvent=current->mNextEvent;
		s->mPrevEvent=current;
		current->mNextEvent=s;
		if (s->mNextEvent)
			s->mNextEvent->mPrevEvent=s;
	}
	else
	{
		s->mNextEvent=NULL;
		s->mPrevEvent=NULL;
		mFirstSoundEvent=s;
	}

	mSoundEvents++;

	return(s);
}

//---------------------------------------------------------------------------

void CSoundManager::DeleteSoundEvent(CSoundEvent *s)
{
	if (s->mNextEvent)
		s->mNextEvent->mPrevEvent=s->mPrevEvent;
	
	if (s->mPrevEvent)
		s->mPrevEvent->mNextEvent=s->mNextEvent;
	else
		mFirstSoundEvent=s->mNextEvent;

	mSoundEvents--;
		
	s->mNextEvent=mSoundEventPool;
	s->mPrevEvent=NULL;
	if (mSoundEventPool)
		mSoundEventPool->mPrevEvent=s;
	mSoundEventPool=s;
}

//---------------------------------------------------------------------------

void CSoundManager::SortEventList()	// FUNCTION: 0x004e1040
{
	CSoundEvent *s=mFirstSoundEvent;

	// Priority-sort event list

	while (s)
	{
		// Bubble-sort events (!!)

		CSoundEvent *n=s->mNextEvent;
		if (n)
		{
			if (n->mCurrentAttenuatedVolume>s->mCurrentAttenuatedVolume)
			{
				// Swap

//				printf("Swapping %s (%d) with %s (%d)\n",n->mSample->mName,n->mCurrentAttenuatedVolume,s->mSample->mName,s->mCurrentAttenuatedVolume);

				if (!s->mPrevEvent)
					mFirstSoundEvent=n;
				else
					s->mPrevEvent->mNextEvent=n;
				if (n->mNextEvent)
					n->mNextEvent->mPrevEvent=s;
				s->mNextEvent=n->mNextEvent;
				n->mPrevEvent=s->mPrevEvent;
				s->mPrevEvent=n;
				n->mNextEvent=s;
			}
		}
		s=s->mNextEvent;
	}

	// Remove channels from the lower <n> samples

	int i;

	s=mFirstSoundEvent;

	int tot_channels = SOUND.GetAvailableChannels();
#if TARGET == XBOX
	// let's try to make sure we always have one channel available for new samples so they don't have to busywait.
	tot_channels /= 2;
#else
	tot_channels = (tot_channels*3)/4; // Use 3/4 of the channels for much the same reason
#endif

	for (i=0;(i<tot_channels) && (s);i++)
		s=s->mNextEvent;

	while (s)
	{
		if (s->mChannel!=-1)
		{
//			printf("Stopping sound %s on channel %d\n",s->mSample->mName,s->mChannel);
			SOUND.StopSound(s);
			s->mChannel=-1;
		}
		s=s->mNextEvent;
	}	

	// Allocate channels to the top <n> samples

	s=mFirstSoundEvent;

	bool channelsleft=true;
	
	for (i=0;(i<tot_channels) && (s) && channelsleft;i++)
	{
		// We only care about sounds that want to play
		if (s->mChannel==-1&&!s->mPaused)
		{
			s->mChannel=SOUND.FindFreeChannel();
			if (s->mChannel>=0)
			{
//				printf("Starting sound %s on channel %d\n",s->mSample->mName,s->mChannel);
				SOUND.PlaySound(s);
			}
			else
			{
//				printf("Ran out of free channels at %d\n",i);
				channelsleft=false;
			}
		}
		s=s->mNextEvent;
	}	
}

//---------------------------------------------------------------------------

SINT CSoundManager::Fade(SINT v, CSoundEvent *event)
{
	SINT tv =0 ;

	if (event->mSoundType == ST_GAME_SOUND)
	{
		tv = SINT(float(v) *
	        event->mMasterVolume *
	        event->mSubVolume *
	        mMasterVolume *
			mGameSoundsMasterVolume);
	}
	else
	{
		tv = SINT(float(v) *
	        event->mMasterVolume *
	        event->mSubVolume *
	        mMasterVolume *
			mMenuSoundsMasterVolume);
	}

	tv = tv * 200; //was 350...
	if(tv > 10000)
	{
		tv = 10000;
	}
	tv = ((tv - 10000)/2);
	if(tv < -10000)
	{
		tv = -10000;
	}

	return  tv; 
}

//---------------------------------------------------------------------------

void CSoundManager::KillSamplesForThing(IAudibleThing *thing)	// FUNCTION: 0x004e1130
{
	CSoundEvent *s=mFirstSoundEvent;

	while (s)
	{
		if((s->mOwner.ToRead() == thing) && (s->mPlaying))
		{
			StopSoundEvent(s);
		}
		s=s->mNextEvent;
	}
	
};

//---------------------------------------------------------------------------

void CSoundManager::KillSample(IAudibleThing *thing, const CSample *sample)	// FUNCTION: 0x004e1190
{
	CSoundEvent *s=mFirstSoundEvent;
	
	while (s)
	{
		if ((s->mOwner.ToRead() == thing) &&
			(s->mSample == sample) &&
			(s->mPlaying))
		{
			StopSoundEvent(s);
		}
		s=s->mNextEvent;
	}	
	
};

//---------------------------------------------------------------------------

void CSoundManager::KillAllInstancesOfSample(const CSample *sample)	// FUNCTION: 0x004e1200
{
	CSoundEvent *s=mFirstSoundEvent;
	
	while (s)
	{
		if (s->mSample == sample)
		{
			StopSoundEvent(s,TRUE);
		}
	
		s=s->mNextEvent;
	}	
	
};

//---------------------------------------------------------------------------

void CSoundManager::FadeTo(const CSample *sample, float fadeval, float speed, IAudibleThing *t)	// FUNCTION: 0x004e1260
{
	CSoundEvent *s=mFirstSoundEvent;
	
	while (s)
	{
		if ((s->mSample == sample) && (s->mOwner.ToRead() == t))
		{
			s->mFadeDest = fadeval;
			if (s->mFadeDest < s->mSubVolume)
				s->mFade = -speed;
			else
				s->mFade = speed;
			
			return;
		}

		s=s->mNextEvent;
	}
};

//---------------------------------------------------------------------------

void CSoundManager::FadeAllSamples()
{
	CSoundEvent *s=mFirstSoundEvent;
	
	while (s)
	{
		s->mFadeDest = 0;
		s->mFade = -0.5f;
		s=s->mNextEvent;
	}
};

//---------------------------------------------------------------------------

void CSoundManager::KillAllSamples()	// FUNCTION: 0x004e12b0
{
	CSoundEvent *s=mFirstSoundEvent;
	
	while (s)
	{
		StopSoundEvent(s);
		s=s->mNextEvent;

#if TARGET==PS2
		SOUND.ClearCommandQueue();
#endif
	}
}

//---------------------------------------------------------------------------

void CSoundManager::PauseAllSamples()	// FUNCTION: 0x004e1300
{
	SOUND.DevicePauseAll();
//	LOG.AddMessage("Pausing all samples");
	CSoundEvent *s=mFirstSoundEvent;
	
	while (s)
	{
		if (s->mChannel!=-1)
			SOUND.PauseSound(s);
		s->mPaused = TRUE;
		s=s->mNextEvent;
	}
};

//---------------------------------------------------------------------------

void CSoundManager::UnPauseAllSamples()	// FUNCTION: 0x004e1330
{
	CSoundEvent *s=mFirstSoundEvent;
	
//	LOG.AddMessage("UnPausing all samples");
	while (s)
	{
		if (s->mChannel!=-1)
			SOUND.UnPauseSound(s);
		s->mPaused = FALSE;
		s=s->mNextEvent;
	}
	SOUND.DeviceUnpauseAll();
}

//---------------------------------------------------------------------------

void CSoundManager::UpdateSoundPosition(CSoundEvent *se,BOOL firsttime)	// FUNCTION: 0x004e1360
{
	CCamera *cam = GAME.GetCamera(0);
	
	FVector campos1=ZERO_FVECTOR;
	FMatrix camori1=ID_FMATRIX;
	FVector campos2=ZERO_FVECTOR;
	FMatrix camori2=ID_FMATRIX;

	if (cam)
	{
		campos1=cam->GetPos();
		camori1=cam->GetOrientation();
	}

	if (GAME.IsMultiplayer())
	{
		cam = GAME.GetCamera(1);

		if (cam)
		{
			campos2=cam->GetPos();
			camori2=cam->GetOrientation();
		}		
	}
	
	// Update position
	
	if ((se->mTrack==ST_FOLLOWDONTDIE) || (se->mTrack==ST_FOLLOWANDDIE) || (se->mTrack==ST_NOTRACKING) ||
		((se->mTrack==ST_SETINITIALPOSITION) && (firsttime)))
	{
		if (se->mOwner.ToRead() && se->mIgnoreOwnerPos == FALSE)
		{
			se->mPos=se->mOwner.ToRead()->GetSoundPos();
			se->mVelocity=se->mOwner.ToRead()->GetSoundVelocity();

			FVector rcpos=campos1;
			FMatrix rcori=camori1;

			if (GAME.IsMultiplayer())
			{
				// Make relative to nearest camera
				
				float c1dist=(se->mPos-campos1).MagnitudeSq();
				float c2dist=(se->mPos-campos2).MagnitudeSq();

				if (c2dist<c1dist)
				{
					rcpos=campos2;
					rcori=camori2;
				}
			}

			se->mPos-=rcpos;
			se->mPos=rcori.Transpose() * se->mPos; // Move into camera-local space			

#if TARGET==PC
			if ((se->mPos.X>2048.0f) || (se->mPos.X<-2048.0f) ||
			    (se->mPos.Y>2048.0f) || (se->mPos.Y<-2048.0f) ||
			    (se->mPos.Z>2048.0f) || (se->mPos.Z<-2048.0f))
			{
				SASSERT(0,"Sound effect created outside sane bounds!");
			}
#endif
		}
		else
		{
			if ((firsttime) || (se->mTrack==ST_NOTRACKING))
			{
				se->mPos.Set(0,0,0);//=mCameraPosition;
				se->mVelocity.Set(0,0,0);
			}
		}			
	}
	else
	{
		if (se->mTrack!=ST_SETINITIALPOSITION)
		{
			// Follow camera
			se->mPos.Set(0,0,0);//=mCameraPosition;
			se->mVelocity.Set(0,0,0);

			if (se->mSample->mType == SAMT_LEFT)
			{
				se->mPos += /*mCameraOrientation * */(FVector(-0.1f, 0, 0));
				se->mPan = -10000;
			}
			else if (se->mSample->mType == SAMT_RIGHT)
			{
				se->mPos += /*mCameraOrientation * */(FVector( 0.1f, 0, 0));
				se->mPan = 10000;
			}
		}
	}
	
	// PC: the options can swap the speakers (004e1752)
	if (CLIPARAMS.mFlipSpeakers)
		se->mPos.X = -se->mPos.X;

	// Recalculate pan
	
	if (((se->mOwner.ToRead()) && (se->mIgnoreOwnerPos == FALSE) && ((se->mTrack==ST_FOLLOWANDDIE) || (se->mTrack==ST_FOLLOWDONTDIE))))
	{
		se->mPan = CalculatePan( se->mPos );			
	}
}

//---------------------------------------------------------------------------

BOOL	CSoundManager::IsSamplePlaying(char *sample_name, IAudibleThing *thing)
{	
	if (!mInitialised)
		return(FALSE);

	CSoundEvent *s=mFirstSoundEvent;

	while (s)
	{
		if ((s->mPlaying) &&
			(s->mOwner.ToRead() == thing) &&
			(stricmp(s->mSample->mName,sample_name)==0))
			return(TRUE);
		s=s->mNextEvent;
	}

	return(FALSE);
}

//---------------------------------------------------------------------------

void	CSoundManager::StopSample(char *sample_name, IAudibleThing *thing)	// FUNCTION: 0x004e1800
{
	if (!mInitialised)
		return;

	CSoundEvent *s=mFirstSoundEvent;
	
	while (s)
	{
		if ((s->mPlaying) &&
			(s->mOwner.ToRead() == thing) &&
			(stricmp(s->mSample->mName,sample_name)==0))
		{
			StopSoundEvent(s);
		}
		s=s->mNextEvent;
	}
}

//---------------------------------------------------------------------------
// WARNING : Do not assume that SoundEvents returned from this hang around
//           for any period of time!
//---------------------------------------------------------------------------

CSoundEvent *CSoundManager::GetSoundEventForThing(char *sample_name, IAudibleThing *thing)	// FUNCTION: 0x004e1880
{
	if (!mInitialised)
		return(NULL);

	CSoundEvent *s=mFirstSoundEvent;
	
	while (s)
	{
		if ((s->mPlaying) &&
			(s->mOwner.ToRead() == thing) &&
			(stricmp(s->mSample->mName,sample_name)==0))
			return(s);
		s=s->mNextEvent;
	}

	return(NULL);
}

//---------------------------------------------------------------------------

void	CSoundManager::SetPitch(CSoundEvent *e,float desiredpitchfactor,float fadetime)	// FUNCTION: 0x004e18d0
{
	if (!mInitialised)
		return;

	if (e)
	{
		e->mDesiredPitchMultiplier=desiredpitchfactor;
		e->mPitchFadeTime=(int) (fadetime*20);
	}
}

//---------------------------------------------------------------------------

CEffect	*CSoundManager::GetEffectByName(char *name,int num)	// FUNCTION: 0x004e1910
{
	if (!mInitialised)
		return(NULL);
	
	return(CEffect::GetEffectByName(name,num));
}

//---------------------------------------------------------------------------

void CSoundManager::PlayNamedEffect(char* name,  IAudibleThing *thing, float volume, ESoundTrackingType track, bool once, float fade,float frompoint,float topoint,bool repeat,float pitch)
{
	CEffect* effect = GetEffectByName(name) ;
	if (effect)
	{
		PlayEffect(effect, thing, volume, track, once, fade, frompoint, topoint, repeat, pitch, ST_MENU_SOUND);
	}
	else
	{
		LOG.AddMessage("ERROR: Could not find effect: %s", name) ;
	}

}

bool language_dependent;

void CSoundManager::PlayEffect(CEffect *effect, IAudibleThing *thing, float volume, ESoundTrackingType track, bool once, float fade,float frompoint,float topoint,bool repeat,float pitch,ESoundType sound_type,BOOL ignore_owner_pos)	// FUNCTION: 0x004e1940
{
	if (!mInitialised)
		return;
	
	if (!effect)
		return;

	// Count chained effects
	
	CEffect *current=effect;
	int numchained=0;
	while (current)
	{
		numchained++;
		current=current->mChainedEffect;
	}

	// Pick a random chained effect
	
	int chainnum=rand() % numchained;

	current=effect;
	
	while (chainnum)
	{
		current=current->mChainedEffect;
		chainnum--;
	}

	// Setup effect and play
	
	volume=(volume*current->mVolume)/100;
	if ((current->mLooping) || (!once))
		once=FALSE;
	else
		once=TRUE;

	if ((current->mPitchVariance!=0) && (CPCSoundManager::GetPitchVarianceEnabled()))	// PC (004e19bb)
	{
		float rpitch=((float) (rand() % current->mPitchVariance))/100.0f;
		
		pitch=pitch+rpitch;
		
		if (pitch<0.1f)
			pitch=0.1f;
	}

	language_dependent = current->mLanguageDependent;
	PlayNamedSample(current->mSample,thing,volume,track,once,fade,frompoint,topoint,repeat,pitch,FALSE, ignore_owner_pos, sound_type);
	language_dependent = FALSE;
}

//---------------------------------------------------------------------------

BOOL CSoundManager::IsEffectPlaying(CEffect *effect ,IAudibleThing *thing)	// FUNCTION: 0x004e1ab0
{
	// Check all chained effects

	CEffect *current=effect;

	while (current)
	{
		if (IsSamplePlaying(current->mSample,thing))
			return(TRUE);

		current=current->mChainedEffect;
	}

	return(FALSE);
}

//---------------------------------------------------------------------------

void CSoundManager::UpdateStatus()	// FUNCTION: 0x004e1b20
{
	PROFILE_FN(UpdateSounds);

	CCamera *cam = GAME.GetCamera(0);

	if (cam)
	{
		mCameraPosition=cam->GetPos();
		mOldCameraPosition=cam->GetOldPos();
		mCameraOrientation=cam->GetOrientation();
	}
	else
	{
		mCameraPosition=ZERO_FVECTOR;
		mCameraOrientation=ID_FMATRIX;
		mOldCameraPosition=ZERO_FVECTOR;
	}

	SortEventList();

	SOUND.UpdateGlobals();

	float timedelta=PLATFORM.GetSysTimeFloat()-mLastProcessTime;
	mLastProcessTime=PLATFORM.GetSysTimeFloat();

	CSoundEvent *se=mFirstSoundEvent;

	while (se)
	{
		if (!se->mPaused)
			se->mTime+=timedelta;

		if (!mSoundsFrozen)
			UpdateSoundPosition(se,FALSE);

		if (mVisibleSounds)
		{
			if (!se->mDebugMarker)
			{
				se->mDebugMarker=new CDebugMarker();
				se->mDebugMarker->SetColour(0xFFFF0000);			
			}
			if (se->mPos==mCameraPosition)
				se->mDebugMarker->SetSize(FVector(0.0f,0.0f,0.0f));
			else
				se->mDebugMarker->SetSize(FVector(0.1f,0.1f,0.1f));
			
			se->mDebugMarker->SetPos(se->mPos);
			if (se->mSample)
				se->mDebugMarker->SetText(se->mSample->mName);
			else
				se->mDebugMarker->SetText("No sample!");
		}
		else
			SAFE_DELETE(se->mDebugMarker);
				
		if((se->mTime > 5) && (!se->mPlaying))
		{
			se->mOwner.SetReader(NULL); // ?????!
		}
		else if (se->mPaused == FALSE)
		{
			// Fade pitch

			if (se->mPitchFadeTime>0)
			{
				se->mPitchMultiplier+=(se->mDesiredPitchMultiplier-se->mPitchMultiplier)/se->mPitchFadeTime;
				se->mPitchFadeTime--;
			}
			else
			{
				se->mPitchMultiplier=se->mDesiredPitchMultiplier;
			}

			//if this is a sample that should fade
			if (se->mFade != 0)
			{
				se->mSubVolume += se->mFade;
				if (se->mFade > 0)
				{
					if (se->mSubVolume > se->mFadeDest)
					{
						se->mSubVolume = se->mFadeDest;
						se->mFade = 0;
					}
				}
				else
				{
					
					if (se->mSubVolume < se->mFadeDest)
					{
						se->mSubVolume = se->mFadeDest;
						se->mFade = 0;

						if (se->mFadeDest == 0)
						{
							// done - stop sample
							StopSoundEvent(se);
						}
					}
				}

				if (se->mSubVolume != 0)
				{
					SINT	avol;
					
					if (se->mTrack==ST_NOTRACKING)
						avol=127;
					else
					{
						float the_dist;
						avol= GetVolumeForPos(se->mPos, &the_dist);	
					}

					SINT	vol = 127;
					vol = Fade(vol, se);
					avol = Fade(avol,se);
					se->mCurrentVolume=vol;
					se->mCurrentAttenuatedVolume=avol;

					if (se->mTrack==ST_NOTRACKING)
						se->mCurrentAttenuatedVolume=vol;
				}
			}

			if ((se->mTrack==ST_FOLLOWANDDIE) || (se->mTrack==ST_FOLLOWDONTDIE) || (se->mTrack==ST_SETINITIALPOSITION))
			{
				if ((!(se->mOwner.ToRead())) || (se->mIgnoreOwnerPos ==TRUE))
				{
					if (se->mTrack==ST_FOLLOWANDDIE)
					{
						// he's gone - kill the sample!
						StopSoundEvent(se);
					}
				}
				else
				{
					// still here - track the volume
					float the_dist;
					SINT	avol = GetVolumeForPos(se->mPos, &the_dist);
					SINT	vol = 127;

					vol = Fade(vol, se);
					avol=Fade(avol,se);
					se->mCurrentVolume=vol;
					se->mCurrentAttenuatedVolume=avol;
				}

			}
		}

		// PC: outside the else (Stuart has it inside); the finished event's SetReader shares the tail of the inlined
		// StopSoundEvent's before this test (004e1df3 -> 004e2212)
		if ((se->mPlaying) && (se->mChannel>=0)) // Check we haven't killed it yet
		{
			SOUND.UpdateSound(se);
		}

		// Kill off any samples that aren't actually playing and have expired
		// (as they won't be killed by the sample engine since they aren't playing!)
		if (!se->mLooping)
		{
			float stoptime=SOUND.GetSampleLength(se->mSample);
			if (se->mEndPoint!=-1)
				stoptime=se->mEndPoint;

			if (((se->mChannel<0) || (se->mEndPoint!=-1)) && (!se->mLooping) && (se->mTime>stoptime))
			{
				StopSoundEvent(se);
			}
		}
		else
		{
			// Force time to within loop boundaries
			while (se->mTime>SOUND.GetSampleLength(se->mSample))
			{
				se->mTime-=SOUND.GetSampleLength(se->mSample);
			}
		}

		CSoundEvent *next=se->mNextEvent;

		if ((!se->mPlaying) && (!mSoundsFrozen))
			DeleteSoundEvent(se);

		se=next;
	}

	SOUND.UpdatesDone();
}

//---------------------------------------------------------------------------

void	CSoundManager::GetDebugMenuText(int num,char *text)	// FUNCTION: 0x004e2360
{
	CSoundEvent *s=mFirstSoundEvent;
	
	while ((s) && num>0)
	{
		s=s->mNextEvent;
		num--;
	}

	if ((s) && (s->mPlaying))
	{
		char buffer[256];
		if (s->mChannel>=0)
			sprintf(text,"%s (Channel %d)",s->mSample->mName,s->mChannel);
		else
			sprintf(text,"----- %s (no channel)",s->mSample->mName,s->mChannel);
//		sprintf(buffer,"\n Pan : %d\n",s->mPan);
//		strcat(text,buffer);
		sprintf(buffer," Volume : %d (%d)\n",s->mCurrentVolume,s->mCurrentAttenuatedVolume);
		strcat(text,buffer);
		if (s->mTrack==ST_NOTRACKING)
		{
			sprintf(buffer," No tracking");
			strcat(text,buffer);
		}
		else if (s->mTrack==ST_SETINITIALPOSITION)
		{
			if (s->mOwner.ToRead())
				sprintf(buffer," SIP tracking : %s\n",((CThing *) s->mOwner.ToRead())->GetName());
			else
				sprintf(buffer," SIP tracking : TARGET DEAD\n");			
			strcat(text,buffer);
		}
		else if (s->mTrack==ST_FOLLOWDONTDIE)
		{
			if (s->mOwner.ToRead())
				sprintf(buffer," Follow-don't-die tracking : %s\n",((CThing *) s->mOwner.ToRead())->GetName());
			else
				sprintf(buffer," Follow-don't-die tracking : TARGET DEAD\n");			
			strcat(text,buffer);
		}
		else if (s->mTrack==ST_FOLLOWANDDIE)
		{
			if (s->mOwner.ToRead())
				sprintf(buffer," Follow-and-die tracking : %s\n",((CThing *) s->mOwner.ToRead())->GetName());
			else
				sprintf(buffer," Follow-and-die tracking : TARGET DEAD\n");			
			strcat(text,buffer);
		}		

//		sprintf(buffer," Pitch %f, desired %f, fadetime %d\n",s->mPitchMultiplier,s->mDesiredPitchMultiplier,s->mPitchFadeTime);
//		strcat(text,buffer);

//		sprintf(buffer," Segment : %fs to %fs\n",s->mStartPoint,s->mEndPoint);
//		strcat(text,buffer);
	}
	else
	{
		sprintf(text,"[no sound]");
	}
}

//---------------------------------------------------------------------------

void	CSoundManagerDebugMenu::GetEntry(int num,char *text)	// FUNCTION: 0x004e2500
{
	SOUND.GetDebugMenuText(num,text);
}

//---------------------------------------------------------------------------

int		CSoundManagerDebugMenu::GetNumEntries()	// FUNCTION: 0x004e2520
{
	return(SOUND.GetNumSoundEvents());
}

//---------------------------------------------------------------------------

void	CEffect::ReadLine(CMEMBUFFER *mb,char *line)
{
	line[0]='#';
	
	while ((line[0]=='#') && (!mb->EndOfFile()))
	{
		mb->ReadString(line,256);
		line[strlen(line)-1]='\0'; // Strip CR
	}
}

//---------------------------------------------------------------------------

void	CEffect::LoadSFXFile(char *filename)	// FUNCTION: 0x004e2530
{
	CMEMBUFFER buf;
	
	buf.InitFromFile(filename);
	
	char line[256];

	ReadLine(&buf,line);
	int version=atoi(line);

	SASSERT(version<=103,"SFX file is a newer version than supported");

	ReadLine(&buf,line);
	int numeffects=atoi(line);

	for (int i=0;i<numeffects;i++)
	{
#line 1566
		CEffect *e=new (MEMTYPE_SOUND) CEffect;

		ReadLine(&buf,line);
		strcpy(e->mName,line);

		ReadLine(&buf,line);
		strcpy(e->mSample,line);

		ReadLine(&buf,line);
		strcpy(e->mLowpassSample,line);
		
		ReadLine(&buf,line);
		e->mVolume=atoi(line);

		ReadLine(&buf,line);
		e->mFalloff=atoi(line);

		ReadLine(&buf,line);
		e->mPitchVariance=atoi(line);
		
		if (version>=101)
		{
			ReadLine(&buf,line);
			if (line[0]=='1')
				e->mLooping=TRUE;
			else
				e->mLooping=FALSE;
		}
		else
			e->mLooping=FALSE;

		if (version >= 103)
		{
			// Flag whether the sample is language dependent or not.
			ReadLine(&buf, line);
			if (line[0] == '1')
				e->mLanguageDependent = TRUE;
			else
				e->mLanguageDependent = FALSE;
		}
		else
		{
			e->mLanguageDependent = FALSE;
		}

		// comment line instead.
		ReadLine(&buf, line);

		// Chain to another effect?

		CEffect *current=mFirstEffect;
		CEffect *found=NULL;
		while ((current) && (!found))
		{
			if ((stricmp(current->mName,e->mName)==0) && (current!=e))
			{
				found=current;
			}
			current=current->mNextEffect;
		}

		if (found)
		{
			while (found->mChainedEffect)
				found=found->mChainedEffect;
			
			found->mChainedEffect=e;
			
			// Remove from main list of effects
			
			CEffect *c=mFirstEffect;
			CEffect *l=NULL;
			while (c)
			{
				if (c==e)
				{
					if (l)
						l->mNextEffect=e->mNextEffect;
					else
						mFirstEffect=e->mNextEffect;
				}
				l=c;
				c=c->mNextEffect;
			}		
		}
	}		
	
	buf.Close();	
}

//---------------------------------------------------------------------------

CEffect	*CEffect::GetEffectByName(char *name,int num)	// FUNCTION: 0x004e2a90
{
	if (!SOUND.IsInitialised())
		return(NULL);

	// Strip any double-backslashes from the name

	char sname[256];
	char *i=name;
	char *o=sname;
	BOOL inslash=FALSE;

	while ((*i)!=0)
	{
		if (*i=='\\')
		{
			if (!inslash)
			{
				*o=*i;
				o++;
				inslash=TRUE;
			}
		}
		else
		{
			*o=*i;
			o++;
			inslash=FALSE;
		}
		i++;
	}

	*o='\0';

	CEffect *c=mFirstEffect;

	while (c)
	{
		if (stricmp(c->mName,sname)==0)
		{
			if (num==0)
				return(c);
			else
				num--;
		}

		c=c->mNextEffect;
	}

/*	TRACE("Could not find sound effect : ");
	TRACE(name);
	TRACE(" (");
	TRACE(sname);
	TRACE(")\n");*/

	return(NULL);

}


//---------------------------------------------------------------------------
void CEffect::DeleteAllEffects()
{
	CEffect* current = mFirstEffect ;
	while (current)
	{
		CEffect* to_del = current ;
		current = current->mNextEffect ;
		delete to_del ;
	}
}


//---------------------------------------------------------------------------

CSoundEvent::CSoundEvent()
{
	mDebugMarker=NULL;
}

//---------------------------------------------------------------------------

CSoundEvent::~CSoundEvent()	// FUNCTION: 0x004e2b30
{
	SAFE_DELETE(mDebugMarker);
}

//---------------------------------------------------------------------------

void	CEffect::PrecacheAll()
{
	CEffect *c=mFirstEffect;
	
	while (c)
	{
		CEffect *d=c;
		while (d)
		{
			CSample *s;
			if (strlen(c->mSample)>0)
			{
				language_dependent=c->mLanguageDependent;
				s=SOUND.GetSample(c->mSample);
				language_dependent=FALSE;
				if (s)
					if (c->mLanguageDependent)
						s->mLanguageDependent=TRUE;
			}

			if (strlen(c->mLowpassSample)>0)
			{
				language_dependent=c->mLanguageDependent;				
				s=SOUND.GetSample(c->mLowpassSample);
				language_dependent=FALSE;
				if (s)
					if (c->mLanguageDependent)
						s->mLanguageDependent=TRUE;
			}

			d=d->mChainedEffect;
		}
		
		c=c->mNextEffect;
	}	
}

//---------------------------------------------------------------------------
// PC: the language's sample bank (not in Stuart's release)

extern char	*gSampleBankFormat;			// 0063e2a8: "%s\\data\\sounds\\sounds_%s_pc.xap" (name open)

bool	CSoundManager::LanguageChanged(char *bank)	// FUNCTION: 0x004e2bb0
{
	if (!mInitialised)
		return(false);

	char	name[200];
	sprintf(name, gSampleBankFormat, CLIPARAMS.mSampleBankRoot, TEXT.GetLanguageName());

	if (!stricmp(name, mLanguageSampleBank))
		return(false);

	if (bank)
		strcpy(bank, name);

	return(true);
}

//---------------------------------------------------------------------------

void	CSoundManager::ReloadLanguageSampleBank()	// FUNCTION: 0x004e2c50
{
	if (LanguageChanged(mLanguageSampleBank))
	{
		TRACE("Loading XAP\n");

		Reset();
		DeleteAllSamples();
		MEM_MANAGER.Cleanup();

		SOUND.LoadSampleBank(NULL);
	}
}
