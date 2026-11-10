// SPDX-License-Identifier: GPL-3.0-only
// The CE Settings page's table and the lookup of its item (port/game/ce_frontend.cpp: the text of kSettings and of
// CCeSettingItem::Lookup, taken whole by ce_settings_tests.py) next to the actual ce.ini readers the game's consumers use
// (port/platform/common/ce.cpp), on scratch user folders. No game, window or device: the SDL calls the file mentions are
// authored stubs.
// Usage: ce_settings_driver table
//        | ce_settings_driver page DIR
//        | ce_settings_driver bool DIR KEY DEFAULT
//        | ce_settings_driver is DIR KEY VALUE
//        | ce_settings_driver set DIR KEY VALUE
// table prints a line per row of the compiled table: key, default choice, the choices' values, when the change applies and the
// action after a write. page prints, per row, the choice the page shows for the ce.ini in DIR (the item's own Lookup). bool and
// is print what CE_SettingGetBool and CE_SettingIs answer for it: the questions the consumers ask. set writes the line as the page
// does (CE_SettingSetText) and prints 1 when the write was made.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef wchar_t WCHAR;

#include "settings_hint.inc"
#include "settings_table.inc"

extern "C" int CE_SettingIs(const char *key, const char *value);
extern "C" int CE_SettingLauncherIs(const char *key, const char *value);
extern "C" int CE_SettingGetBool(const char *key, int def);
extern "C" int CE_SettingSetText(const char *key, const char *value);

extern "C" Uint64 SDL_GetTicks(void) { return 0; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }	// ce.cpp's cheat gate: no network session in this fixture

struct Item
{
	const SCeSetting *mSetting;
#include "settings_lookup.inc"
};

static void Folder(const char *dir) { setenv("BEA_USER_DIR", dir, 1); }

int main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	const char *what = argv[1];
	if (!strcmp(what, "table") && argc == 2)
	{
		for (int i = 0; i < CE_NUM_SETTINGS; i++)
		{
			const SCeSetting &s = kSettings[i];
			printf("%s\t%s\t%d\t", s.mKey, s.mChoices ? "choices" : "shown", s.mDefault);
			for (int c = 0; c < s.mCount; c++)
				printf("%s%s", c ? "," : "", s.mChoices[c].mValue);
			printf("\t%s\t%s\n", s.mHint.mExtra == kNextLevel ? "next-level" : s.mHint.mExtra == kNextStart ? "next-start" :
				   s.mHint.mExtra == kNextConsole ? "console-open" : "other",
				   s.mAfter == CEAF_HUD ? "hud" : s.mAfter == CEAF_RECORD ? "record" : "none");
		}
		return 0;
	}
	if (!strcmp(what, "page") && argc == 3)
	{
		Folder(argv[2]);
		for (int i = 0; i < CE_NUM_SETTINGS; i++)
		{
			if (!kSettings[i].mChoices)
				continue;
			Item item{&kSettings[i]};
			printf("%s\t%d\n", kSettings[i].mKey, item.Lookup());
		}
		return 0;
	}
	if (!strcmp(what, "bool") && argc == 5)
	{
		Folder(argv[2]);
		printf("%d\n", CE_SettingGetBool(argv[3], atoi(argv[4])));
		return 0;
	}
	if (!strcmp(what, "is") && argc == 5)
	{
		Folder(argv[2]);
		printf("%d\n", CE_SettingIs(argv[3], argv[4]) ? 1 : 0);
		return 0;
	}
	if (!strcmp(what, "set") && argc == 5)
	{
		Folder(argv[2]);
		printf("%d\n", CE_SettingSetText(argv[3], argv[4]) ? 1 : 0);
		return 0;
	}
	return 2;
}
