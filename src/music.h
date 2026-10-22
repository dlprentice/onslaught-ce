// music.h: music (only what matched files need). MUSIC is a CPCMusic at 00889a48 (vtable 005e4934, set by its
// initializer 005152c0); SetVolume is 004bba10. It includes the sound manager's header, since CCareer::Load (00421200) and others
// reach SOUND through this one.

#ifndef MUSIC_H
#define MUSIC_H

// Stuart's Music.h
enum	EMusicSelection
{
	MUS_FRONTEND,
	MUS_CREDITS,
	MUS_TUTORIAL,
	MUS_STEALTH,
	MUS_INGAME
};

#include "console.h"

enum EMusicPlayType
{
	MPT_SINGLE,
	MPT_LINEAR,
	MPT_RANDOM,
	MPT_SELECTION,
};

class CSong
{
public:
	CSong()
	{
		mIndex=-1;
		mNext=NULL;
	}

	char					mFilename[MAX_PATH];
	CSong					*mNext;					// +0x104 (CMusic::Shutdown 004bb422)
	int						mIndex;
};

class CMusicMenu : public CConsoleMenu
{
	virtual void	GetName(char *name)				{ strcpy(name,"Music"); };	// FUNCTION: 0x005152f0
	virtual int		GetNumEntries();											// 004bba60
	virtual bool	GetShowSubmenus()				{ return(true); };
	virtual void	GetEntry(int num,char *text);								// 004bba80
	virtual void	OnClick(int num);											// 004bbb70
};

class CMusic
{
public:
							CMusic()
							{
								mInitialised=FALSE;
							}
	void					Initialise();											// 004bb380
	void					Shutdown();												// 004bb400
	void					Play(char *filename);									// inlined everywhere
	void					WipePlayList();
	void					PlayFromList(CSong *s,BOOL fade=TRUE);					// 004bb7e0
	int						GetTrackForFrontEnd(); // returns which track number the front end will use.
	int						GetTrackForCredits(); // and same for credits.
	float					GetVolume() { return ((float)mSetVolume)/127.0f; };
	int						GetCurrentVolume() { return mVolume; };
	CSong					*GetRandomSong();
	CSong					*GetSong(int which);
	void					DeviceWarnOfStop() {} // this is the game telling you that soon it will want to stop
	void					SetVolume(float vol);
	void					PlaySelection(EMusicSelection sel,BOOL fade=FALSE);	// 004bb8c0
	void					Stop();													// 004bb490
	void					UpdateStatus();											// 004bb530
	void					AddDirectoryToPlaylist(char *dir);						// 004bb7c0
	void					AddToPlayList(char *name);								// 004bb6b0

	EMusicPlayType			mPlayType;				// +0x04
	bool					mPlaying;				// +0x08
	CSong					*mFirstSong;			// +0x0c
	CSong					*mCurrentSong;			// +0x10

protected:
	CMusicMenu				mMenu;					// +0x14
	int						mTargetVolume;			// +0x28
	int						mSetVolume;				// +0x2c

	CSong					*mQueuedSong;			// +0x30

	void					AddDirectoryExtsToPlaylist(char *dir,char *ext);
	void					FadeVolumes();											// 004bb4b0

	// Platform-specific interface

	virtual void			DeviceInitialise()=0;
	virtual void			DeviceShutdown()=0;
	virtual void			DevicePlay(char *filename)=0;
	virtual void			DeviceStop()=0;
	virtual	BOOL			DeviceGetTrackFinished()=0;
	virtual void			DeviceSetVolume(int vol)=0;
	virtual void			DeviceAddDirectoryExtsToPlaylist(char *dir,char *ext)=0;
	virtual void			DeviceChangeTrack(char *filename);						// 004bb450

	// this one's non-pure because it's OK to have nothing in it.
	virtual void			DeviceUpdateStatus() {}
protected:
	int						mVolume;				// +0x34
	bool					mInitialised;			// +0x38
	EMusicSelection			mSelection;				// +0x3c
};

// retail: the PC's music plays through the streaming Ogg player (PCPlatform's async music stream)
class CPCMusic : public CMusic
{
protected:
	virtual void			DeviceInitialise();
	virtual void			DeviceShutdown();
	virtual void			DevicePlay(char *filename);
	virtual void			DeviceStop();
	virtual	BOOL			DeviceGetTrackFinished();
	virtual void			DeviceSetVolume(int vol);
	virtual void			DeviceAddDirectoryExtsToPlaylist(char *dir,char *ext);
};

extern CPCMusic MUSIC;

// The sound manager SOUND (00896988) and its types: Stuart's SoundManager.h, with the PC's changes.
#include "SoundManager.h"

#endif
