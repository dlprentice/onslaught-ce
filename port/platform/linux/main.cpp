// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/main.cpp: the Linux entry point. It sets up the path layer and SDL, then runs the game's own
// WinMain (src/ltshell.cpp) with the command line rebuilt from argv, as Windows would pass it.
#include "handles.h"
#include "../common/platform.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

int main(int argc, char **argv)
{
#ifdef CE_VERSION
	if (argc > 1 && !strcmp(argv[1], "--version"))	// public builds (build.py --public --ce-version)
	{
		printf("Onslaught CE v%s\n", CE_VERSION);
		return 0;
	}
#endif
	PortPathsInit();
	// dxvk-native finds its window system through this; SDL3 is the port's.
	setenv("DXVK_WSI_DRIVER", "SDL3", 0);
	PortAudioGuardCheck();
	// Video first: the game creates its Direct3D object before its window, and dxvk-native's SDL3 window system asks
	// SDL for Vulkan's instance extensions at that point.
	if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO))
	{
		fprintf(stderr, PORT_LOG_PREFIX "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	size_t len = 1;
	for (int i = 1; i < argc; i++)
		len += strlen(argv[i]) + 1;
	char *cmdline = (char *)calloc(1, len);
	for (int i = 1; i < argc; i++)
	{
		if (i > 1)
			strcat(cmdline, " ");
		strcat(cmdline, argv[i]);
	}
	// The launcher already printed the game folder with the account name shortened. Direct launches keep both paths.
	const char *folderLogged = getenv("BEA_CE_GAME_FOLDER_LOGGED");
	if (folderLogged && !strcmp(folderLogged, "1"))
		fprintf(stderr, PORT_LOG_PREFIX "writable folder %s\n", PortUserDir());
	else
		fprintf(stderr, PORT_LOG_PREFIX "game folder %s, writable folder %s\n", PortGameDir(), PortUserDir());
	unsetenv("BEA_CE_GAME_FOLDER_LOGGED");
	int rc = WinMain(GetModuleHandleA(NULL), NULL, cmdline, SW_SHOWNORMAL);
	SDL_Quit();
	// The process ends as on Windows, where ExitProcess stops every other thread before a DLL's detach code runs:
	// glibc's exit() runs each shared library's destructors with its threads still running. The game creates vertex
	// shaders just before it quits and dxvk-native converts them on its own worker threads, so a worker could meet
	// dxvk's destroyed static tables (its constant-layout set) and abort on lowerLegacyConstInput's dstIndex assertion
	// (the 0.1.2 release candidate's full-boot run). The port's own exit work (the end-level probes) runs here, every
	// stream is flushed (the game's save writes may still be buffered), and _exit skips the library destructors.
	// _exit also skips the game's own atexit(ExitCode) (src/ltshell.cpp), deliberately: on Linux it has nothing to
	// do that outlives the process. It restores the accessibility keys, which the SystemParametersInfoA stub
	// (port/platform/linux/user.cpp) reports as off, so there is nothing to restore; the rest frees PATCHMANAGER data
	// and shuts down RECONNECT_INTERFACE and the memory manager, all in memory with no file writes.
	PortReplayAtExit();
	fflush(NULL);
	_exit(rc);
}
