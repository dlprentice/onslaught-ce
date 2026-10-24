// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/handles.h: the kernel objects behind the port's Win32 HANDLEs on Linux.
#ifndef PORT_PLATFORM_HANDLES_H
#define PORT_PLATFORM_HANDLES_H
#include <windows.h>
#include <pthread.h>
#include <stdio.h>

enum PortHandleKind { PORT_EVENT = 1, PORT_MUTEX, PORT_THREAD, PORT_FILE, PORT_FIND };

struct PortHandle
{
	PortHandleKind kind;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

struct PortEvent
{
	PortHandle h;
	bool manual;
	bool signaled;
};

struct PortMutex
{
	PortHandle h;
	pthread_t owner;
	int count;
};

struct PortThread
{
	PortHandle h;
	pthread_t thread;
	LPTHREAD_START_ROUTINE start;
	LPVOID param;
	DWORD exitCode;
	bool finished;
	bool orphaned;
};

struct PortFile
{
	PortHandle h;
	int fd;
};

PortHandle *PortHandleNew(PortHandleKind kind, size_t size);
void PortHandleFree(PortHandle *h);
// The live handle behind `handle` if it is one of the port's and of `kind` (0: any); otherwise NULL and
// ERROR_INVALID_HANDLE, as Windows answers.
PortHandle *PortCheck(HANDLE handle, int kind);
void PortFileClose(PortHandle *h);
void PortWaitersWake();
void PortSetLastError(DWORD e);

// ---- paths (paths.cpp) --------------------------------------------------------------------------------------------
// The game names files relative to its folder with backslashes and Windows' case-insensitivity ("data\\Resources\\...").
// Reads look in the port's writable folder first, then the player's game folder; writes always go to the writable
// folder, so the player's install is never modified. Resolve returns 0 and fills out (a host path) or -1.
// PORT_PATH_SHADOW: where a write would go, without creating anything (searches use it).
// PORT_PATH_GAME: the game-folder copy only, without creating anything (the other half of a merged search).
enum PortPathMode { PORT_PATH_READ, PORT_PATH_WRITE, PORT_PATH_SHADOW, PORT_PATH_GAME };
int PortResolvePath(const char *game_path, PortPathMode mode, char *out, size_t n);
void PortPathsInit();
const char *PortGameDir();
const char *PortUserDir();
// Writable operations return/retain descriptors beneath the user root; no caller reopens a checked host path.
int PortOpenUserFile(const char *game_path, int flags, bool copy_original);
int PortDeleteUserFile(const char *game_path);
int PortMakeUserDirectory(const char *game_path);

#endif
