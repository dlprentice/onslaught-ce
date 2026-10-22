// cframetimer.cpp: the frame-rate timer (CFrameTimer). Not in Stuart's release; reconstructed from the retail code
// (00423650..004237c7). The object was built without /QIfist (config/file-flags.txt): Start converts the rate with
// __ftol (0042369f), the only game-code call to it.

#include "common.h"
#include "cframetimer.h"
#include <mmsystem.h>

// the counter's time, never zero; the millisecond clock where the counter fails
static inline LONGLONG	GetTime()
{
	LONGLONG	t;
	if (QueryPerformanceCounter((LARGE_INTEGER *)&t))
	{
		while (t == 0)
			QueryPerformanceCounter((LARGE_INTEGER *)&t);
	}
	else
		t = timeGetTime();
	return t;
}

//******************************************************************************************
CFrameTimer::CFrameTimer()	// FUNCTION: 0x00423650
{
	if (!QueryPerformanceFrequency((LARGE_INTEGER *)&mFrequency))
		mFrequency = 1000;
	mTicksPerSec = float(mFrequency);
}

//******************************************************************************************
void	CFrameTimer::Start(float fps)	// FUNCTION: 0x00423680
{
	mFramesPerSec = fps;
	mSecsPerFrame = 1.f / fps;
	mFrameTicks = mFrequency / (ULONGLONG)fps;
	mLastTime = GetTime();
}

//******************************************************************************************
void	CFrameTimer::Frame()	// FUNCTION: 0x00423720
{
	LONGLONG	t = GetTime();
	if (t != mLastTime)
	{
		mFrameTicks = t - mLastTime;
		mLastTime = t;
		mFramesPerSec = mTicksPerSec / float(mFrameTicks) * 0.25f + mFramesPerSec * 0.75f;
		if (mFramesPerSec < 1.f)
			mFramesPerSec = 1.f;
		mSecsPerFrame = 1.f / mFramesPerSec;
	}
}
