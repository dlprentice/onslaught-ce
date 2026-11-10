// SPDX-License-Identifier: GPL-3.0-only
// The actual ce.ini settings code (port/platform/common/ce.cpp: CE_SettingSetText and the readers the game's consumers use) on
// the actual ce_ini_write (port/updater/ce_report.h), against the scratch user folder BEA_USER_DIR names. No game, window,
// device or network: the SDL calls the file mentions are authored stubs that do nothing.
// Usage: ce_setting_text_driver set KEY VALUE | set-null-key VALUE | set-null-value KEY
//        | is KEY VALUE | text KEY SIZE | bool KEY DEFAULT
// set exits 0 when CE_SettingSetText wrote the file and 1 when it did not; is exits 0 when CE_SettingIs says yes; text prints
// what CE_SettingGetText reads (empty when it says no); bool prints what CE_SettingGetBool answers.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int CE_SettingSetText(const char *key, const char *value);
extern "C" int CE_SettingIs(const char *key, const char *value);
extern "C" int CE_SettingGetText(const char *key, char *out, int n);
extern "C" int CE_SettingGetBool(const char *key, int def);

extern "C" Uint64 SDL_GetTicks(void) { return 0; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }	// ce.cpp's cheat gate: no network session in this fixture

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	const char *what = argv[1];
	if (!strcmp(what, "set") && argc == 4)
		return CE_SettingSetText(argv[2], argv[3]) ? 0 : 1;
	if (!strcmp(what, "set-null-key") && argc == 3)
		return CE_SettingSetText(NULL, argv[2]) ? 0 : 1;
	if (!strcmp(what, "set-null-value") && argc == 3)
		return CE_SettingSetText(argv[2], NULL) ? 0 : 1;
	if (!strcmp(what, "is") && argc == 4)
		return CE_SettingIs(argv[2], argv[3]) ? 0 : 1;
	if (!strcmp(what, "text") && argc == 4)
	{
		static char out[4096];
		int n = atoi(argv[3]);
		if (n < 1 || n > int(sizeof(out)))
			return 2;
		int found = CE_SettingGetText(argv[2], out, n);
		printf("%s", found ? out : "");
		return found ? 0 : 1;
	}
	if (!strcmp(what, "bool") && argc == 4)
	{
		printf("%d", CE_SettingGetBool(argv[2], atoi(argv[3])));
		return 0;
	}
	return 2;
}
