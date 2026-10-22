// binkapi.h: the few RAD Game Tools Bink entry points the front-end video calls (binkw32.dll imports; declared from
// the import names and the retail calls, not from RAD's header, which is not part of this project).

#ifndef BINKAPI_H
#define BINKAPI_H

typedef unsigned long	U32;				// RAD's integer types
typedef long			S32;

// a playing video: only what the callers read
struct BINK
{
	U32		Width;							// +0x00
	U32		Height;							// +0x04
	U32		Frames;							// +0x08
	U32		FrameNum;						// +0x0c: the current frame, from 1
};
typedef BINK	*HBINK;

// BinkGetSummary's figures (only what the callers read)
struct BINKSUMMARY
{
	U32		Unknown00[9];
	U32		TotalPlayedFrames;				// +0x24
	U32		SkippedFrames;					// +0x28
	U32		Unknown2c[20];					// 0x7c bytes (CDXFrontEndVideo::CloseVideo's frame)
};

#define BINKPRELOADALL		0x00002000		// BinkOpen's flags
#define BINKSNDTRACK		0x00004000		// the sound track BinkSetSoundTrack chose
#define BINKSURFACE32A		5				// BinkCopyToBuffer's destination formats
#define BINKSURFACE565		10
#define BINKCOPYNOSCALING	0x70000000
#define BINKCOPYALL			0x80000000

typedef void	*(__stdcall *RADMEMALLOC)(U32 bytes);
typedef void	(__stdcall *RADMEMFREE)(void *ptr);
typedef void	*(__stdcall *BINKSNDSYSOPEN)(U32 param);

extern "C"
{
__declspec(dllimport) void	__stdcall RADSetMemory(RADMEMALLOC a, RADMEMFREE f);
__declspec(dllimport) void	*__stdcall BinkOpenDirectSound(U32 param);
__declspec(dllimport) S32	__stdcall BinkSetSoundSystem(BINKSNDSYSOPEN open, U32 param);
__declspec(dllimport) void	__stdcall BinkSetSoundTrack(U32 total_tracks, U32 *tracks);
__declspec(dllimport) HBINK	__stdcall BinkOpen(const char *name, U32 flags);
__declspec(dllimport) void	__stdcall BinkSetVolume(HBINK bink, U32 track, S32 volume);
__declspec(dllimport) S32	__stdcall BinkDoFrame(HBINK bink);
__declspec(dllimport) void	__stdcall BinkNextFrame(HBINK bink);
__declspec(dllimport) S32	__stdcall BinkWait(HBINK bink);
__declspec(dllimport) S32	__stdcall BinkCopyToBuffer(HBINK bink, void *dest, S32 pitch, U32 height, U32 x, U32 y, U32 flags);
__declspec(dllimport) void	__stdcall BinkGetSummary(HBINK bink, BINKSUMMARY *summary);
__declspec(dllimport) void	__stdcall BinkClose(HBINK bink);
}

#endif
