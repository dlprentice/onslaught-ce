// DebugLog.h: the game's debug log (the retail object LOG lives at 0066f580).

#ifndef DEBUGLOG_H
#define DEBUGLOG_H

#ifdef DEBUGLOG_CPP	// DebugLog.cpp's view; the other objects keep theirs (declaration counts)
#define DEBUG_LOG_LINES		30

// a text log file and the recent lines shown over the game (vtable 005db01c)
class CDebugLog
{
public:
	CDebugLog(char *name, BOOL enabled) : mFileName(name), mOpened(false), mEnabled(enabled) { Clear(); }
	virtual ~CDebugLog() {}								// 0 (??_G 004416b0)

	void	Clear()
	{
		for (SINT i = 0; i < DEBUG_LOG_LINES; i++)
		{
			mLines[i][0] = 0;
			mLineTimes[i] = -100.f;
		}
		mLast = 0;
		mLastTime = -100.f;
	}
	void	Reset();									// 004416e0
	void	SetFilename(char *name);					// 00441730
	void	AddMessage(char *fmt, ...);					// 00441740: a line, to the file and the overlay
	void	AddString(char *fmt, ...);					// 004418a0: the same without the line end
	void	Render();									// 004419e0: the recent-status overlay (Stuart's PC LOG.Render)

	char	*mFileName;									// +0x004
	bool	mOpened;									// +0x008: the file was made this run
	char	mLines[DEBUG_LOG_LINES][0x50];				// +0x009
	float	mLineTimes[DEBUG_LOG_LINES];				// +0x96c
	SINT	mLast;										// +0x9e4: the newest line
	float	mLastTime;									// +0x9e8
	BOOL	mEnabled;									// +0x9ec
};
#else
class CDebugLog
{
public:
#ifdef SYSTEM_CPP
	void	Reset();						// 004416e0
#endif
	void	AddMessage(char *fmt, ...);
	void	AddString(char *fmt, ...);
	void	Render();						// 004419e0: the recent-status overlay (Stuart's PC LOG.Render)
};
#endif

extern CDebugLog LOG;
extern CDebugLog SETUP_HISTORY;					// 0066eb90: setuphistory.txt

// Asks for a screen dump of an area (NULL: the whole screen) at the next frame (DebugLog.obj; the dump is
// 00441b80's; CGame::Update's -autoconfigtest script, 0046e961; name open).
void	RequestScreenDump(RECT *area, bool a);	// 00441b10

#endif
