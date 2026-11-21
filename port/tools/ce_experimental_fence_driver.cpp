// SPDX-License-Identifier: GPL-3.0-only
// port/tools/ce_experimental_fence_driver.cpp: the first main menu's question, PortCeMenus (extracted whole from port/game/ce_frontend.cpp into
// port_ce_menus.inc by port/tools/ce_experimental_fence_tests.py), with the actual port/platform/common/ce.cpp behind it: PortCeLayer, the
// [Experimental] block's writer and the ce.ini readers. Authored here: the two game switches PortCeMenus tests and the SDL calls ce.cpp names.
// Usage: driver DIR [autoconfig|demo]   (the environment is the caller's)   prints "menus 0|1"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int BOOL;
struct { BOOL mAutoConfigTest; } CLIPARAMS;
BOOL PLAYABLE_DEMO;
bool PortCeLayer();
bool PortCeExperimental(const char *mode);
void PortCeExperimentalBlock();

extern "C" Uint64 SDL_GetTicks(void) { return 0; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }

#include "port_ce_menus.inc"

int main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	setenv("BEA_USER_DIR", argv[1], 1);
	if (argc > 2 && !strcmp(argv[2], "autoconfig"))
		CLIPARAMS.mAutoConfigTest = 1;
	if (argc > 2 && !strcmp(argv[2], "demo"))
		PLAYABLE_DEMO = 1;
	const int first = PortCeMenus() ? 1 : 0;
	const int again = PortCeMenus() ? 1 : 0;		// every later menu asks again: the answer and the file stay
	if (first != again)
		return 3;
	printf("menus %d\n", first);
	return 0;
}
