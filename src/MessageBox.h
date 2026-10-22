// MessageBox.h: the in-game message box (only what matched files need so far).

#ifndef MESSAGEBOX_H
#define MESSAGEBOX_H

#include "Monitor.h"
#include "MessageLog.h"
#include "game.h"

#include "AudibleThing.h"
#include "activereader.h"

class CThing;
class CDXTexture;
class CSample;
class CScheduledEvent;

// the message box's own events (HandleEvent's switch, 004b81db)
enum EMessageBoxEvent
{
	MESSAGE_BOX_REVEAL = 3000,					// the next letters of the text (AdvanceReveal)
	MESSAGE_BOX_NEXT = 3001,					// 0.2 seconds after a message ends: the next one
	MESSAGE_BOX_FINISH = 3002,					// the message has been shown
	ALLOWED_TO_PLAY_MESSAGES = 3003,			// CGame::StartPlayingState (0046fef2)
	MESSAGE_BOX_START = 3004					// 0.2 seconds after a message is taken from the queue
};

// RTTI: CMessage (vtable 005dc6b4, 7 slots) is an IAudibleThing: a radio message, queued in the box and kept in the
// log once shown (0x3c bytes: IScript::AddMessage 00537492)
class CMessage : public IAudibleThing
{
public:
	CMessage(WCHAR *name, WCHAR *text, int string_id, float delay, CThing *speaker, CScheduledEvent *event, int priority);	// 004b6e50
	virtual			~CMessage();				// 004b7160

	virtual	FVector	GetSoundPos();										// 004b6f30
	virtual	FMatrix	GetSoundOrientation();								// 004b6f50: inline, MessageBox.cpp
	virtual FVector	GetSoundVelocity() { return FVector(0, 0, 0); }	// the shared 00405e80
	virtual void	SampleFinishedPlaying(CSoundEvent* event);	// 004b71d0 (inline, MessageBox.cpp)

	void			RevealLetter() { if (mRevealed < (int)wcslen(mText)) mRevealed ++; }
	BOOL			SpeakerGone() { return mHasSpeaker && ((!mSpeaker.ToRead()) || mSpeaker->IsDying()); }

	// the text cut into lines of 0x56 characters, the last line ending "..." while it is still revealed
	void			WordWrap(WCHAR *lines, int width, int max_lines);	// 004b6f70

	BOOL			mFinishedPlaying;			// +0x08: its sample has played
	WCHAR			*mText;						// +0x0c
	int				mStringId;					// +0x10
	float			mDuration;					// +0x14: the text's length / 22 + 0.2 seconds
	WCHAR			*mName;						// +0x18: the speaker's name
	int				mRevealed;					// +0x1c: letters shown so far
	float			mDelay;						// +0x20
	float			mEndTime;					// +0x24
	CScheduledEvent	*mEvent;					// +0x28: posted when the message has been shown
	int				mPriority;					// +0x2c: the queue is sorted on it
	CActiveReader<CThing>	mSpeaker;			// +0x30
	int				mDots;						// +0x34: the waiting dots after the text
	BOOL			mHasSpeaker;				// +0x38
};

#define MESSAGE_BOX_SPEAKERS	13				// InitResources' portraits (004b7320)
#define MESSAGE_BOX_FRAMES		6

class CMessageBox : public CMonitor
{
public:
	CMessageBox();								// 004b71e0 (0x2c8 bytes: CGame::InitRestartLoop 0046c686)
	virtual			~CMessageBox();				// 004b7930
	virtual void	HandleEvent(CEvent *event);	// 004b81d0

	void	InitResources();					// 004b7320: the speakers' portraits
	int		GetPortrait(WCHAR *name);			// 004b7ab0: the speaker whose name this is
	void	SwitchMessagesOn();					// 004b7b60 (IScript::SwitchMessagesOn 005342ca)
	void	SwitchMessagesOff();				// 004b7b70 (IScript::SwitchMessagesOff 005342ea)
	void	TryAdvance();						// 004b7b80: takes the next queued message when it may
	void	AddMessage(CMessage *message);		// 004b7ca0: queues a message by priority (CUnit::ConfirmedKill 004fe28d)
	void	PlayPendingSamples();				// 004b7d90
	void	StartMessage();						// 004b7ea0
	void	AdvanceReveal();					// 004b8020
	CDXTexture	*GetPortraitTexture(int n);		// 004b82a0
	void	RenderBattleLinePulse(float x, float y, int a);	// 004b82b0 (CHud::RenderBattleline 00487f66)
	void	StopCurrentPlayingSound();			// 004b8800 (CGame::DeclarePlayerDead 0046f69e)
	void	RenderOverlay(int a);				// 004b8850 (CHud::RenderBattleline passes 7)

	CMessage	*mCurrent;						// +0x008: the message being shown (CHud::Render 00487c1e)
	SPtrSet<CMessage>	mQueue;					// +0x00c
	float		mStartTime;						// +0x01c
	float		mUnknown020;					// +0x020
	BOOL		mUnknown024;					// +0x024
	CSeedUniformRandomNumberStream	mRandom;	// +0x028
	BOOL		mAllowedToPlay;					// +0x030
	CDXTexture	*mPortraits[MESSAGE_BOX_SPEAKERS][MESSAGE_BOX_FRAMES];	// +0x034
	DWORD		mSpeakerIds[MESSAGE_BOX_SPEAKERS];	// +0x16c: the speakers' names' text ids
	CDXTexture	*mUnknown1a0;					// +0x1a0
	CDXTexture	*mUnknown1a4;					// +0x1a4
	int			mPortrait;						// +0x1a8
	int			mUnknown1ac;					// +0x1ac
	CDXTexture	*mPortraitTexture;				// +0x1b0
	float		mUnknown1b4;					// +0x1b4
	int			mCharsPerLine;					// +0x1b8: 8930 / the width of a pangram
	int			mUnknown1bc;					// +0x1bc
	char		mVoiceName[256];				// +0x1c0
	BOOL		mCanAdvance;					// +0x2c0
	float		mOpen;							// +0x2c4: the panel, 0 shut, 1 its frame open, 2 its text shown (RenderOverlay)
};

// RTTI: CLevelBriefingLog (vtable 005dc208, 7 slots) derives from CGameMenu (LevelBriefingLog.cpp)
class CLevelBriefingLog : public CGameMenu
{
public:
	CLevelBriefingLog();						// 0048f540 (0x14 bytes)
	virtual			~CLevelBriefingLog();		// 0048f5c0
	void	Activate();							// 0048ff90: shown from the pause menu (004d0be8)
	void	Render(class CViewport *viewport);	// 0048f620 (CDXEngine::PostRender 0053ee19)
	virtual void	ReceiveButtonAction(CController* from_controller, int button, float val);	// 0048ffa0
	virtual BOOL	CanBeControlledWhenInPause() { return TRUE; }	// the shared 004014a0
	virtual EControlType	GetControlType() { return CONTROL_LEVEL_BRIEFING_LOG; }	// the shared 0052f660

	BOOL		mActive;						// +0x08: cleared when the log is closed
	float		mFade;							// +0x0c: 0..1, the panel's size
	CDXTexture	*mBlank;						// +0x10: darkens the game behind the log
};

#endif
