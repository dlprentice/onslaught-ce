// SPDX-License-Identifier: GPL-3.0-only
// The unfinished-feature lines (port/platform/common/ce.cpp: PortCeExperimental, PortCeExperimentalBlock, the ce.ini lines under
// [Experimental]) next to the actual ce.ini readers and writer they use, on scratch user folders. No game, window or device: the SDL calls
// the file mentions are authored stubs.
// Usage: ce_experimental_driver ask DIR MODE      prints what PortCeExperimental answers for MODE with the ce.ini in DIR (a fresh process: read at the first ask)
//        ce_experimental_driver all DIR           prints the four answers, map_editor museum commander play_unit, in one process
//        ce_experimental_driver once DIR MODE     asks, rewrites ce.ini to the other value, asks again and prints both answers (read once)
//        ce_experimental_driver block DIR         adds the [Experimental] lines (a second call in the same process changes nothing) and prints 1
//        ce_experimental_driver set DIR KEY N     CE_SettingSet(KEY, N) as the CE Settings page and the cheats call it
//        ce_experimental_driver get DIR KEY       CE_SettingGet(KEY, -1) (the legacy reader), prints the value
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool PortCeExperimental(const char *mode);
void PortCeExperimentalBlock();
extern "C" int CE_SettingSet(const char *key, int value);
extern "C" int CE_SettingGet(const char *key, int def);

extern "C" Uint64 SDL_GetTicks(void) { return 0; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	setenv("BEA_USER_DIR", argv[2], 1);
	if (!strcmp(argv[1], "ask") && argc == 4)
	{
		printf("%d\n", PortCeExperimental(argv[3]) ? 1 : 0);
		return 0;
	}
	if (!strcmp(argv[1], "all") && argc == 3)
	{
		const int editor = PortCeExperimental("map_editor") ? 1 : 0;		// in this order: the arguments of one printf have none
		const int museum = PortCeExperimental("museum") ? 1 : 0;
		const int commander = PortCeExperimental("commander") ? 1 : 0;
		const int unit = PortCeExperimental("play_unit") ? 1 : 0;
		printf("%d %d %d %d\n", editor, museum, commander, unit);
		return 0;
	}
	if (!strcmp(argv[1], "once") && argc == 4)
	{
		const int first = PortCeExperimental(argv[3]) ? 1 : 0;
		char path[1024];
		snprintf(path, sizeof(path), "%s/ce.ini", argv[2]);
		FILE *f = fopen(path, "wb");
		if (!f)
			return 3;
		fprintf(f, "%s=%d\n", argv[3], first ? 0 : 1);
		fclose(f);
		const int second = PortCeExperimental(argv[3]) ? 1 : 0;
		printf("%d %d\n", first, second);
		return 0;
	}
	if (!strcmp(argv[1], "block") && argc == 3)
	{
		PortCeExperimentalBlock();
		PortCeExperimentalBlock();
		printf("1\n");
		return 0;
	}
	if (!strcmp(argv[1], "set") && argc == 5)
	{
		printf("%d\n", CE_SettingSet(argv[3], atoi(argv[4])));
		return 0;
	}
	if (!strcmp(argv[1], "get") && argc == 4)
	{
		printf("%d\n", CE_SettingGet(argv[3], -1));
		return 0;
	}
	return 2;
}
