// cframetimer.h: the frame-rate timer (cframetimer.cpp; not in Stuart's release; 0x38 bytes, from its uses in
// CPCPlatform). It keeps a running frame rate from QueryPerformanceCounter, or timeGetTime where that fails.

#ifndef CFRAMETIMER_H
#define CFRAMETIMER_H

class CFrameTimer
{
public:
			CFrameTimer();						// 00423650
	void	Start(float fps);					// 00423680: the expected rate
	void	Frame();							// 00423720: once a frame
	float	GetFramesPerSec() { return mFramesPerSec; }

	float		mTicksPerSec;					// +0x00: mFrequency as a float
	float		mFramesPerSec;					// +0x04 (CPCPlatform::GetFPS 00515956): a 3:1 running average
	float		mSecsPerFrame;					// +0x08
	UBYTE		mUnknown0C[0x04];
	LONGLONG	mLastTime;						// +0x10
	LONGLONG	mFrequency;						// +0x18: counter ticks a second (1000 for timeGetTime)
	LONGLONG	mFrameTicks;					// +0x20: Start's expected frame, then each measured frame
	UBYTE		mUnknown28[0x38 - 0x28];
};

#endif
