// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/main.cpp: the Linux entry point. It sets up the path layer and SDL, then runs the game's own
// WinMain (src/ltshell.cpp) with the command line rebuilt from argv, as Windows would pass it.
#include "handles.h"
#include "../common/platform.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

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
	fprintf(stderr, PORT_LOG_PREFIX "game folder %s, writable folder %s\n", PortGameDir(), PortUserDir());
	int rc = WinMain(GetModuleHandleA(NULL), NULL, cmdline, SW_SHOWNORMAL);
	SDL_Quit();
	return rc;
}
