// Generic music code

#include "common.h"	// FUNCTION: 0x004bb290 init:ZERO_FVECTOR

#include "music.h"	// FUNCTION: 0x004bb2b0 init:ID_FMATRIX
#include "console.h"
#include "career.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "debuglog.h"
#include "CLIParams.h"	// retail: Play's cheat track (004bb5cb)
//---------------------------------------------------------------------------

void con_m_play(char *cmd)
{
	char name[256];

	if (strlen(cmd)>7)
		strcpy(name,&cmd[7]);
	else
	{
		MUSIC.PlayFromList(NULL);
		return;
	}
	
	MUSIC.Play(name);
}

//---------------------------------------------------------------------------

void con_m_stop(char *cmd)
{
	MUSIC.Stop();
}

//---------------------------------------------------------------------------

void con_m_add(char *cmd)
{
	char name[256];

	if (strlen(cmd)>6)
		strcpy(name,&cmd[6]);
	else
	{
		CONSOLE.Print("Syntax : m_add <filename>\n");
		return;
	}
	
	MUSIC.AddToPlayList(name);
}

//---------------------------------------------------------------------------

void con_m_adddir(char *cmd)
{
	char name[256];
	
	if (strlen(cmd)>9)
		strcpy(name,&cmd[9]);
	else
	{
		CONSOLE.Print("Syntax : m_adddir <directory>\n");
		return;
	}
	
	MUSIC.AddDirectoryToPlaylist(name);
}

//---------------------------------------------------------------------------

void con_m_clear(char *cmd)
{
	MUSIC.WipePlayList();
}

//---------------------------------------------------------------------------

void con_m_playtype(char *cmd)
{
	char t[256];
	
	if (sscanf(cmd,"%*s %s",t)!=1)
	{
		CONSOLE.Print("Syntax : m_playtype [single|linear|random]\n");
		return;
	}

	if (stricmp(t,"single")==0)
	{
		MUSIC.mPlayType=MPT_SINGLE;
	}
	else if (stricmp(t,"linear")==0)
	{
		MUSIC.mPlayType=MPT_LINEAR;
	}
	else if (stricmp(t,"random")==0)
	{
		MUSIC.mPlayType=MPT_RANDOM;
	}
	else
	{
		CONSOLE.Print("Syntax : m_playtype [single|linear|random]\n");
		return;
	}
}

//---------------------------------------------------------------------------

void CMusic::Initialise()	// FUNCTION: 0x004bb380
{
	// Defaults

	mPlaying=false;
	mFirstSong=NULL;
	mPlayType=MPT_LINEAR; 

	// Console variables
#if 0	// retail (004bb380): the PC build registers no console commands, variable or menu here
	CONSOLE.RegisterCommand("m_play","Play a given music file",&con_m_play);
	CONSOLE.RegisterCommand("m_stop","Stop the music",&con_m_stop);
	CONSOLE.RegisterCommand("m_add","Add a file to the playlist",&con_m_add);
	CONSOLE.RegisterCommand("m_adddir","Add a directory to the playlist",&con_m_adddir);
	CONSOLE.RegisterCommand("m_clear","Clear the playlist",&con_m_clear); 
	CONSOLE.RegisterCommand("m_playtype","Set the playback type",&con_m_playtype); 
	CONSOLE.RegisterVariable("m_volume","Music volume (range 0-127)",CVar_int,&mSetVolume);
	CONSOLE.GetRootMenu()->AddSubmenu(&mMenu);
#endif
	DeviceInitialise();

	// Set default volume

	mVolume=127;
	mTargetVolume=127;
	SetVolume(CAREER.GetMusicVolume());
	mQueuedSong=NULL;

	mInitialised=TRUE;
}

//---------------------------------------------------------------------------

void CMusic::Shutdown()	// FUNCTION: 0x004bb400
{
	if (mPlaying)
		Stop();

	DeviceShutdown();

	CSong *c=mFirstSong;
	CSong *next=NULL;

	while (c)
	{
		next=c->mNext;
		delete c;
		c=next;
	}

	mFirstSong=NULL;
}

//---------------------------------------------------------------------------

void CMusic::DeviceChangeTrack(char *filename)	// FUNCTION: 0x004bb450
{
	// default stupid implementation.
	Stop();

	mVolume = mSetVolume;
	mTargetVolume = mSetVolume;
	DeviceSetVolume(mVolume);

	DevicePlay(filename);

	mPlaying = true;
}

//---------------------------------------------------------------------------
extern char	gAllCheatsEnabled;	// 00679ec1 (game.cpp)
void CMusic::Play(char *filename)
{
	if (CLIPARAMS.mAutoConfigTest || gAllCheatsEnabled)	filename = "data\\music\\BEA 08(Master).wma";	// retail (004bb5c9..004bb5dc)
	if (mPlaying)
	{
		DeviceChangeTrack(filename);
	}
	else
	{
		mVolume = mSetVolume;
		mTargetVolume = mSetVolume;
		DeviceSetVolume(mVolume);

		DevicePlay(filename);

		mPlaying=true;
	}
}

//---------------------------------------------------------------------------

void CMusic::Stop()	// FUNCTION: 0x004bb490
{
	if (!mPlaying)
		return;

	DeviceStop();

	mPlaying=false;
}

//---------------------------------------------------------------------------

void CMusic::FadeVolumes()	// FUNCTION: 0x004bb4b0
{
	if (mInitialised)
	{
		if (abs(mVolume-mTargetVolume)<10)
			mVolume=mTargetVolume;

		if (mVolume<mTargetVolume)
			mVolume+=5;
		if (mVolume>mTargetVolume)
			mVolume-=5;

		if ((mVolume==0) && (mQueuedSong))
		{
			DeviceSetVolume(mVolume);
			PlayFromList(mQueuedSong,FALSE);
			mQueuedSong=NULL;
		}

		if (mVolume==mTargetVolume)
		{
			mTargetVolume=mSetVolume;
		}
	}
}

//---------------------------------------------------------------------------

void CMusic::UpdateStatus()	// FUNCTION: 0x004bb530
{
	DeviceUpdateStatus();

	if (mPlaying)
	{
		// Update volumes
		FadeVolumes();		

		// Full volume is 0, and -10,000 is silence; the scale is logarithmic.
		// Multiply the desired decibel level by 100 (for example -10,000 = -100 dB). 		

		if (mInitialised)
		{
			if (mVolume<0)
				mVolume=0;
			if (mVolume>127)
				mVolume=127;			

			DeviceSetVolume(mVolume);
		}

		if (DeviceGetTrackFinished())
		{
			// We've hit the end of the track
			
			if (mPlayType==MPT_SINGLE)
			{
				Stop();
			}
			else if (mPlayType==MPT_LINEAR)
			{
				if (mCurrentSong)
				{
					mCurrentSong=mCurrentSong->mNext;
				}
				if (!mCurrentSong)
					mCurrentSong=mFirstSong;
				
				if (mCurrentSong)
					Play(mCurrentSong->mFilename);
				else
					Stop(); // Nothing in playlist
			}
			else if (mPlayType==MPT_RANDOM)
			{
				if (mFirstSong)
				{
					mCurrentSong=GetRandomSong();
					
					Play(mCurrentSong->mFilename);
				}
				else
					Stop(); // Nothing in playlist
			}
			else if (mPlayType == MPT_SELECTION)
				PlaySelection(mSelection); // play another track from selection
		}
	}
}

//---------------------------------------------------------------------------

void CMusic::AddToPlayList(char *name)	// FUNCTION: 0x004bb6b0
{
	CSong	*current = mFirstSong;
	CSong	*prev;
	
	while (current)
	{
		if (stricmp(name,current->mFilename)==0)
			return; // Already in playlist
		current=current->mNext;
	}

	CSong *n = new( MEMTYPE_MUSIC ) CSong();
	current = mFirstSong;

	if (current)
	{
		// Scan playlist to find either the last entry, or a filename that is alphabetically "greater" than ours
		while ( ( current ) && ( stricmp( current->mFilename, name ) < 0 ) )
		{
			prev = current;			
			current = current->mNext;
		}
		
		if ( current )
		{
			// Found next filename that is alphabetically "greater" than ours so we need to insert ours before it
			if (current == mFirstSong)
			{
				mFirstSong = n;
				n->mNext = current;
			}
			else
			{
				prev->mNext = n;
				n->mNext = current;
			}

		}
		else
		{
			// Reached the last entry, so tag ours to the end
			prev->mNext = n;
			n->mNext = NULL;
		}
	}
	else
	{
		// First song added to the playlist
		mFirstSong = n;
		n->mNext = NULL;
	}

	strcpy(n->mFilename, name);	
	
	CONSOLE.Print("Added %s to playlist\n",name);
}

//---------------------------------------------------------------------------

void CMusic::AddDirectoryExtsToPlaylist(char *dir,char *ext)
{	
	DeviceAddDirectoryExtsToPlaylist(dir,ext);
}

//---------------------------------------------------------------------------

void CMusic::AddDirectoryToPlaylist(char *dir)	// FUNCTION: 0x004bb7c0
{
#if TARGET == XBOX
	AddDirectoryExtsToPlaylist(dir,"wma");
#else
	AddDirectoryExtsToPlaylist(dir,"ogg");	// retail (004bb7c6): the PC plays ogg files
	// (Stuart: "mp3" and "wav")
#endif
}
 
//---------------------------------------------------------------------------

void CMusic::WipePlayList()
{
	CSong *current=mFirstSong;
	CSong *next=NULL;

	while (current)
	{
		next=current->mNext;
		
		delete current;

		current=next;
	}

	mFirstSong=NULL;
	mCurrentSong=NULL;
}

//---------------------------------------------------------------------------

CSong *CMusic::GetRandomSong()
{
	int songs=0;
	CSong *c=mFirstSong;
	while (c)
	{
		songs++;
		c=c->mNext;
	}
	
//	int songnum=(rand()*songs)/RAND_MAX;
	int songnum=(rand() % songs);
	
	c=mFirstSong;
	while (songnum>0)
	{
		songnum--;
		c=c->mNext;
		ASSERT(c);
	}

	return(c);
}

//---------------------------------------------------------------------------
void CMusic::PlayFromList(CSong *s,BOOL fade)	// FUNCTION: 0x004bb7e0
{
	if ((mPlaying) && (fade))
	{
		if (s!=mCurrentSong)
		{
			mQueuedSong=s;
			mTargetVolume=0;
		}
	}
	else
	{
		if (s)
			mCurrentSong=s;
		else
		{
			mCurrentSong=mFirstSong;

			if ((mPlayType=MPT_RANDOM) && (mFirstSong))
				mCurrentSong=GetRandomSong();			
		}

		mTargetVolume=mSetVolume;

		if (mCurrentSong)
			Play(mCurrentSong->mFilename);
	}
}

//---------------------------------------------------------------------------
int CMusic::GetTrackForCredits()
{
	return 7;
}

//---------------------------------------------------------------------------
int CMusic::GetTrackForFrontEnd()
{
	if (PLAYABLE_DEMO)
		return 1;
	else
		return 8;
}

//---------------------------------------------------------------------------

CSong *CMusic::GetSong(int tnum)
{
	CSong	*s = mFirstSong;

	while ((tnum) && (s))
	{
		s = s->mNext;
		tnum --;
	}

	if (!s) 
		s = mFirstSong;

	return s;
}

//---------------------------------------------------------------------------
void CMusic::PlaySelection(EMusicSelection sel,BOOL fade)	// FUNCTION: 0x004bb8c0
{
	SINT tnum;

	switch (sel)
	{
	case MUS_FRONTEND:
		tnum = GetTrackForFrontEnd();
		break;

	case MUS_CREDITS:
		tnum = GetTrackForCredits();
		break;

	case MUS_TUTORIAL:
		tnum = 3;
		break;

	case MUS_STEALTH:
	case MUS_INGAME:
		if (PLAYABLE_DEMO)
		{
			tnum = 0;
		}
		else
		{
			tnum = (rand() >> 8) % 8;
			if (tnum == 7)
				tnum = 9;
		}
		break;
	};

	char	foo[100];
	sprintf(foo, "Playing Track: %d\n", tnum);
	TRACE(foo);

	CSong	*s = GetSong(tnum);

	if ((mPlaying) && (fade))
	{
		if (s != mCurrentSong)
		{
			mQueuedSong=s;
			mTargetVolume=0;
		}
	}
	else
	{
		mTargetVolume=mSetVolume;

		mCurrentSong = s;

		mPlayType = MPT_SELECTION;
		mSelection = sel;

		if (mCurrentSong)
			Play(mCurrentSong->mFilename);

	}
}

//---------------------------------------------------------------------------

void CMusic::SetVolume(float vol)	// FUNCTION: 0x004bba10
{
#if 1	// retail (004bba10): the linear volume; Stuart has #if TARGET==PS2
	mSetVolume=vol*127.0f;
#else
	// SRG erghh convert 0...1 into non linear music volume.     	
	float	t1 = ((float)tan((1.0-vol)*1.38f)) ;
	static float t2 = ((float)tan(1.38f));
			
	// Input volume goes from 0 to 127
	mSetVolume = (int)((1.0f - (t1 / t2))*127.0f);
#endif

	LOG.AddMessage("input vol = %2.8f, master music volume = %d",vol, mSetVolume) ;

	CAREER.SetMusicVolume(vol) ;
}

//---------------------------------------------------------------------------

int CMusicMenu::GetNumEntries()	// FUNCTION: 0x004bba60
{
	CSong *c=MUSIC.mFirstSong;
	int num=0;

	while (c)
	{
		num++;
		c=c->mNext;
	}

	return(num+1);
}

//---------------------------------------------------------------------------

void CMusicMenu::GetEntry(int num,char *text)	// FUNCTION: 0x004bba80
{
	CSong *c=MUSIC.mFirstSong;

	if (num==0)
	{
		if (MUSIC.mPlaying)
		{
			sprintf(text,"--Playing (volume %d, set %d)--",MUSIC.GetCurrentVolume(),MUSIC.GetVolume());
		}
		else
			sprintf(text,"--Stopped--");
		return;
	}

	num--;
	
	while (num>0)
	{
		ASSERT(c);
		num--;
		c=c->mNext;
	}

	char *lastslash=c->mFilename;
	char *p=c->mFilename;

	while (*p!=0)
	{
		if (*p=='\\')
			lastslash=p;
		p++;
	}
	
	strcpy(text,lastslash+1);

	if ((MUSIC.mCurrentSong==c) && (MUSIC.mPlaying))
		strcat(text," (playing)");
}

//---------------------------------------------------------------------------

void CMusicMenu::OnClick(int num)	// FUNCTION: 0x004bbb70
{
	if (num==0)
	{
		if (MUSIC.mPlaying)
			MUSIC.Stop();
		else
			MUSIC.PlayFromList(NULL);
		return;
	}

	CSong *c=MUSIC.mFirstSong;

	num--;
	
	while (num>0)
	{
		ASSERT(c);
		num--;
		c=c->mNext;
	}

	MUSIC.PlayFromList(c);
}
