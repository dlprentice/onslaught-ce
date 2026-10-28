// SPDX-License-Identifier: GPL-3.0-only
// Authored stand-ins for the headers the retail music code (src/Music.cpp, src/PCMusic.cpp after the port patches) and
// the CE playlist (port/game/ce_music.cpp) include, so ce_music_tests.py can run them without a game. The music stream
// below records what it is asked to play; rand() counts the game's draws.
#ifndef CE_MUSIC_FAKE_H
#define CE_MUSIC_FAKE_H
#include <cstdarg>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <strings.h>
#include <vector>

#define PC 1
#define PS2 3
#define XBOX 2
#define TARGET PC
typedef int BOOL;
typedef unsigned long DWORD;
typedef void *HANDLE;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define ASSERT(x)
#define TRACE(x) (void)(x)
#define stricmp strcasecmp
#define _tcsicmp strcasecmp
typedef signed int SINT;
struct WIN32_FIND_DATA { char cFileName[MAX_PATH]; };
inline const char *ToTCHAR(const char *s) { return s; }
HANDLE FindFirstFile(const char *spec, WIN32_FIND_DATA *data);
BOOL FindNextFile(HANDLE h, WIN32_FIND_DATA *data);
BOOL FindClose(HANDLE h);

enum { MEMTYPE_MUSIC = 7 };
inline void *operator new(size_t size, int) { return ::operator new(size); }

extern BOOL PLAYABLE_DEMO;
int CountedRand();
#define rand CountedRand

struct FakeConsole { void Print(const char *fmt, ...) { (void)fmt; } };
extern FakeConsole CONSOLE;
class CConsoleMenu
{
public:
	CConsoleMenu() {}
	virtual ~CConsoleMenu() {}
	virtual void GetName(char *name) = 0;
	virtual int GetNumEntries() = 0;
	virtual void GetEntry(int num, char *text) = 0;
	virtual void OnClick(int num) = 0;
	virtual bool GetShowSubmenus() = 0;
};
struct FakeCareer { float GetMusicVolume() { return 1.f; } void SetMusicVolume(float) {} };
extern FakeCareer CAREER;
struct FakeLog { void AddMessage(const char *, ...) {} };
extern FakeLog LOG;
struct FakeCli { bool mAutoConfigTest = false, mRecordDemo = false, mPlaybackDemo = false, mAttractMode = false; int mStressTest = 0; };
extern FakeCli CLIPARAMS;
#endif
