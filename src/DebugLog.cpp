// DebugLog.cpp: the debug logs (LOG and SETUP_HISTORY), their on-screen status lines, and the debug screen dump
// (reconstructed; Stuart's file is not in his release). A log appends each message to its file, keeps the last 30 as
// status lines, and CDebugLog::Render shows the newest six rising over the game for a while.

#include	"Common.h"

#define DEBUGLOG_CPP
#include	"DebugLog.h"
#include	"eventmanager.h"
#include	"Platform.h"
#include	"ltshell.h"
#include	"text.h"
#include	"WarnOnce.h"
#include	"CLIParams.h"
#include	<d3dx9.h>

char	*StrCopyN(char *dst, char *src, SINT n);		// 004d6240 (PolyBucket.cpp)

// FUNCTION: 0x004415b0 init:LOG
// FUNCTION: 0x00441620 exit:LOG
CDebugLog	LOG("c:\\debug.log", FALSE);			// 0066f580
// FUNCTION: 0x00441630 init:SETUP_HISTORY
// FUNCTION: 0x004416a0 exit:SETUP_HISTORY
CDebugLog	SETUP_HISTORY("setuphistory.txt", TRUE);	// 0066eb90
// FUNCTION: 0x004416b0 ??_GCDebugLog@@UAEPAXI@Z

// where a log goes when its own file cannot be made
static char	*sFallbackName = "c:\\setuphistory.txt";	// 006283e8

//*********************************************************************************
void	CDebugLog::Reset()	// FUNCTION: 0x004416e0
{
	Clear();
	if (CLIPARAMS.mDeveloperMode == 1)
		mEnabled = TRUE;
}

//*********************************************************************************
void	CDebugLog::SetFilename(char *name)	// FUNCTION: 0x00441730
{
	mFileName = name;
}

//*********************************************************************************
void	CDebugLog::AddMessage(char *fmt, ...)	// FUNCTION: 0x00441740
{
	char	text[700];
	va_list	args;
	va_start(args, fmt);
	vsprintf(text, fmt, args);

	TRACE(text);
	TRACE("\n");

	if (mEnabled)
	{
		FILE	*f;
		if (!mOpened)
		{
			f = fopen(mFileName, "w");
			if ((!f) && (mFileName[strlen(mFileName) - 5] == 'p'))
			{
				mFileName = sFallbackName;
				f = fopen(sFallbackName, "w");
			}
			mOpened = true;
		}
		else
			f = fopen(mFileName, "a");

		if (f)
		{
			fprintf(f, text);
			fprintf(f, "\n");
			fclose(f);

			mLast++;
			if (mLast > DEBUG_LOG_LINES - 1)
				mLast = 0;
			StrCopyN(mLines[mLast], text, 0x50);
			mLines[mLast][0x4f] = 0;
			mLastTime = EVENT_MANAGER.GetTime();
			mLineTimes[mLast] = EVENT_MANAGER.GetTime();
		}
	}
}

//*********************************************************************************
void	CDebugLog::AddString(char *fmt, ...)	// FUNCTION: 0x004418a0
{
	if (mEnabled)
	{
		FILE	*f;
		if (!mOpened)
		{
			f = fopen(mFileName, "w");
			if ((!f) && (mFileName[strlen(mFileName) - 5] == 'p'))
			{
				mFileName = sFallbackName;
				f = fopen(sFallbackName, "w");
			}
			mOpened = true;
		}
		else
			f = fopen(mFileName, "a");

		if (f)
		{
			char	text[256];
			va_list	args;
			va_start(args, fmt);
			vsprintf(text, fmt, args);

			fprintf(f, text);
			fclose(f);

			mLast++;
			if (mLast > DEBUG_LOG_LINES - 1)
				mLast = 0;
			StrCopyN(mLines[mLast], text, 0x50);
			mLines[mLast][0x4f] = 0;
			mLastTime = EVENT_MANAGER.GetTime();
			mLineTimes[mLast] = EVENT_MANAGER.GetTime();
		}
	}
}

//*********************************************************************************
static inline SINT	LineIndex(SINT last, SINT back)
{
	SINT	n = last - back;
	while (n < 0)
		n += DEBUG_LOG_LINES;
	return n;
}

// the six newest lines, rising as they age; gone 16 seconds after the last
// STAND-IN (two labelled explicit concessions). Retail stores the unwrapped
// index mLast - i once to a frame slot that nothing reads (00441a24, an 8-byte frame) and tests the first wrap with
// `test esi,esi; jge` instead of reusing the subtraction's flags. Here `line` holds that value: (1) the unreachable
// ToWCHAR call takes its address, so the optimizer keeps it in memory with its otherwise-unused store, as with
// RenderPart2D's fraction; (2) its identical arms make a branch that C2 merges only after register allocation, and
// with it the first wrap's sign test is a separate test as in retail (without the arms ours branches on the
// subtraction's flags, jns). The original operations are unknown: without both, the function differs only in the
// frame (one slot), that store and the jns.
void	CDebugLog::Render()	// FUNCTION: 0x004419e0
{
	if (EVENT_MANAGER.GetTime() - 16.f > mLastTime)
	{
		mLastTime = -100.f;
		return;
	}

	float	y = 100.f;
	for (SINT i = 0; i < 6; i++)
	{
		SINT	n = mLast - i;
		SINT	line = (n < 0) ? (mLast - i) : (mLast - i);	// STAND-IN: identical arms (see above)
		if (0) ToWCHAR((char *)&line);					// STAND-IN: an unreachable address escape (see above)
		while (n < 0)
			n += DEBUG_LOG_LINES;
		float	t = mLineTimes[n];
		float	rise = (EVENT_MANAGER.GetTime() - t) * 3.75f;
		if (rise < 100.f - y)
			rise = 100.f - y;
		y = 100.f - rise;

		PLATFORM.Font(FONT_DEBUG)->DrawText(2.f, y, 0xff3eff84, ToWCHAR(mLines[LineIndex(mLast, i)]), 0, 0, 1.f);
		y -= 15.f;
	}
}

//*********************************************************************************
// the screen dump the game asks for, made when the frame is shown (names ours). Under the precompiled header the uninitialised
// statics come out in reverse definition order and the initialised ones after them in order (retail 0066eb80..0066ff78), so
// sScreenDumpFullSize sits off a 4-byte boundary and is loaded as a zero-extended byte (retail: 00441de6).
RECT	sScreenDumpArea;						// 0066eb80: top -1 for the whole screen
bool	sScreenDumpPending = false;					// 0066ff74
bool	sScreenDumpFullSize = false;				// 0066ff75
SINT	sScreenDumpCount = 0;					// 0066ff78

void	RequestScreenDump(RECT *area, bool full_size)	// FUNCTION: 0x00441b10
{
	if (area)
	{
		sScreenDumpArea = *area;
		sScreenDumpPending = true;
		sScreenDumpFullSize = full_size;
	}
	else
	{
		sScreenDumpArea.top = -1;
		sScreenDumpArea.left = 0;
		sScreenDumpPending = true;
		sScreenDumpFullSize = full_size;
	}
}

//*********************************************************************************
// the back buffer to a .dds (or, full size, a .bmp); the small one is reloaded into a 256 (or 128) texture and saved
void	ProcessScreenDump()	// FUNCTION: 0x00441b80
{
	if (!sScreenDumpPending)
		return;

	char	name[MAX_PATH];
	sprintf(name, "%s%04d.dds", CLIPARAMS.mAutoConfigTestPath, sScreenDumpCount);
	RECT	*area = (sScreenDumpArea.top != -1) ? &sScreenDumpArea : NULL;

	IDirect3DSurface9	*back;
	if (FAILED(LT.GetDevice()->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
	{
		SETUP_HISTORY.AddMessage("Screen dump: Couldn't get back buffer %s", name);
		return;
	}

	if (!sScreenDumpFullSize)
	{
		if (FAILED(D3DXSaveSurfaceToFile(name, D3DXIFF_DDS, back, NULL, area)))
		{
			SETUP_HISTORY.AddMessage("Screen dump: Couldn't save raw surface %s", name);
			return;
		}
	}
	else
	{
		sprintf(name, "%s%04d.bmp", CLIPARAMS.mAutoConfigTestPath, sScreenDumpCount);
		if (FAILED(D3DXSaveSurfaceToFile(name, D3DXIFF_BMP, back, NULL, area)))
		{
			SETUP_HISTORY.AddMessage("Screen dump: Couldn't save raw surface %s", name);
			return;
		}
	}

	if (!sScreenDumpFullSize)
	{
		IDirect3DTexture9	*texture;
		if ((FAILED(LT.GetDevice()->CreateTexture(256, 256, 1, 0, D3DFMT_DXT1, D3DPOOL_SYSTEMMEM, &texture, NULL))) &&
			(FAILED(LT.GetDevice()->CreateTexture(128, 128, 1, 0, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, &texture, NULL))))
		{
			SETUP_HISTORY.AddMessage("Screen dump: Couldn't write %s", name);
			return;
		}

		IDirect3DSurface9	*surface;
		if (FAILED(texture->GetSurfaceLevel(0, &surface)))
		{
			SETUP_HISTORY.AddMessage("Screen dump: Couldn't get surface for %s", name);
			texture->Release();
			return;
		}

		if (FAILED(D3DXLoadSurfaceFromFile(surface, NULL, NULL, name, NULL, D3DX_DEFAULT, 0, NULL)))
		{
			SETUP_HISTORY.AddMessage("Screen dump: Couldn't reload %s", name);
			surface->Release();
			texture->Release();
			return;
		}

		if (FAILED(D3DXSaveSurfaceToFile(name, D3DXIFF_DDS, surface, NULL, NULL)))
		{
			SETUP_HISTORY.AddMessage("Screen dump: Couldn't save smaller surface %s", name);
			surface->Release();
			texture->Release();
			return;
		}

		surface->Release();
		texture->Release();
	}

	SETUP_HISTORY.AddMessage("Screen dump: saved %s, fullsize? %d", name, sScreenDumpFullSize);
	sScreenDumpPending = false;
	sScreenDumpCount++;
}

//*********************************************************************************
CWarnOnce::CWarnOnce()	// FUNCTION: 0x00441e20
{
	mWarned = false;
}

//*********************************************************************************
// true once it has warned
bool	CWarnOnce::Warn()	// FUNCTION: 0x00441e30
{
	bool	warned = mWarned;
	mWarned = true;
	return warned;
}

// Release trace calls reach the folded one-byte ret. Original source/object ownership is unknown;
// retain an out-of-line variadic function so argument evaluation and caller cleanup are preserved.
void DebugTrace(char *fmt, ...)	// FUNCTION: 0x0040c640
{
}
// STAND-IN (link layout): retail's 4 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_DebugLog[4] = { 0 };
