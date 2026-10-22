// PCMusic.cpp: the PC's music device (not in Stuart's release; rebuilt from the retail object, 005151b0..005154a8).

#include "common.h"
class CPCMusic;class CMusicMenu;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include "music.h"
#include "Platform.h"
#include "mathsconst.h"		// FUNCTION: 0x005151d0 init:ZERO_FVECTOR
							// FUNCTION: 0x005151f0 init:ID_FMATRIX
// the streaming player (PCPlatform's async music stream, 005282b0..00528680)
void	InitMusicStream();							// 005282b0
void	ShutdownMusicStream();						// 00528460
void	PlayMusicStream(char *filename);			// 00528540
void	StopMusicStream();							// 005285b0
void	SetMusicStreamVolume(float volume);			// 005285e0
extern bool	MUSIC_STREAM_FINISHED;				// 0089beac

// FUNCTION: 0x005152c0 init:MUSIC
CPCMusic	MUSIC;										// 00889a48

void CPCMusic::DeviceInitialise()	// FUNCTION: 0x00515320
{
	InitMusicStream();
	AddDirectoryToPlaylist("data\\music");
}

void CPCMusic::DeviceShutdown()	// FUNCTION: 0x00515340
{
	ShutdownMusicStream();
}

void CPCMusic::DevicePlay(char *filename)	// FUNCTION: 0x00515350
{
	PlayMusicStream(filename);
}

void CPCMusic::DeviceStop()	// FUNCTION: 0x00515360
{
	StopMusicStream();
}

void CPCMusic::DeviceSetVolume(int vol)	// FUNCTION: 0x00515370
{
	SetMusicStreamVolume(vol * (1.0f / 127.0f));	// spelling chosen to give retail's own float-folded 1/127 (Music.obj's is the division in music.h): placement evidence; Stuart's release has no such line
}

BOOL CPCMusic::DeviceGetTrackFinished()	// FUNCTION: 0x00515390
{
	return MUSIC_STREAM_FINISHED;
}

void CPCMusic::DeviceAddDirectoryExtsToPlaylist(char *dir,char *ext)	// FUNCTION: 0x005153a0
{
	char			search[MAX_PATH];
	WIN32_FIND_DATA	find;
	char			path[MAX_PATH];

	strcpy(search, dir);
	strcat(search, "\\*.");
	strcat(search, ext);
	HANDLE h = FindFirstFile(ToTCHAR(search), &find);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			sprintf(path, "%s\\%s", dir, find.cFileName);
			AddToPlayList(path);
		} while (FindNextFile(h, &find));
	}
	FindClose(h);
}
// STAND-IN (link layout): retail's 1488 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_PCMusic[1488] = { 0 };
