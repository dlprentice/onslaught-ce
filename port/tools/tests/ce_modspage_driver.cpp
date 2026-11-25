// SPDX-License-Identifier: GPL-3.0-only
// port/tools/tests/ce_modspage_driver.cpp: the Mods page's model (port/game/ce_modspage_model.h, the very file the game compiles) on the
// host for ce_modspage_tests.py, over the actual mod loader (port/platform/common/mods.cpp) and the actual ce.ini reader and writer
// (ce.cpp). No game, window, device or network. BEA_USER_DIR is the user folder, BEA_GAME_DIR the game folder whose files a mod may
// replace. Commands on standard input, one a line; every answer is one line (a text's line breaks are shown as "\n"):
//   read             reads the mods again; "ROWS <n> off=<0|1|2> folder=<0|1> more=<n> refused=<n> loaded=<id,id|->"
//   row <i>          "ROW <id|->|<name>|<state>|<why number>|<action label,...>|<loaded 0|1>"
//   hint <i>         "HINT <description>|<by line>|<short clash sentence>|<why sentence>|<long clash sentence>"
//   order            "ORDER <id or name>:<state>,..." for every row, in the list's order
//   cut <n> <text>   "CUT <CeModCut of text at n characters>"
//   detail <i>       "DETAIL <the loader's own words>"
//   details <i> [n]  "DETAILS <the Details text>" (what it says of itself cut to n characters, 400 by default)
//   contents <i>     "CONTENTS <what it changes>"
//   toggle <i>       "RESULT <ECeModResult>"
//   move <i> <d>     "RESULT <ECeModResult>"
//   play <i> <k>     "PLAY <the level the k-th Play action of the row starts, or 0>"
//   at               "AT <the row the last change left its mod in>"
//   pages <n>        "PAGES <pages> <rows on each page> for n mods"
//   folder           "FOLDER <1|0>" Open Mods Folder's making of the mods folder (1: a folder is there afterwards)
//   note             "NOTE <the page's note or ->"
//   line             "LINE <ce.ini's mods= value as a reader takes it, or ->"
//   text <utf-8>     "TEXT <CeModText of it>"
//   sentences        "SENTENCE <text>" for every sentence, label and word the page can show, then "END"
// The answers are Latin-1 as the model makes them, printed as UTF-8.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "ce_modspage_model.h"

extern "C" int CE_SettingGetText(const char *key, char *out, int n);
extern "C" int CE_MakeModsFolder(char *dir, size_t n);			// ce.cpp
extern "C" unsigned long long SDL_GetTicks(void) { return 0; }	// ce.cpp's settings cache clock, bypassed here

const char *PortUserDir()
{
	const char *u = getenv("BEA_USER_DIR");
	return u ? u : ".";
}

bool PortGameHasFile(const char *rel)
{
	const char *g = getenv("BEA_GAME_DIR");
	std::string p = std::string(g ? g : ".") + "/" + rel;
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void PortPathsNamesOnly() {}

// The characters the game's small face has, of Latin-1: all the printable ones but these (src/Font.cpp, as ce_frontend.cpp lists them).
static int Shown(int c)
{
	if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0) || c > 0xff)
		return 0;
	return strchr("$@[\\]^_`{|}~", c) == NULL;
}

// Latin-1 out as UTF-8, a line break as the two characters \n.
static void Put(const char *text)
{
	for (const unsigned char *p = (const unsigned char *)text; *p; p++)
	{
		if (*p == '\n')
			fputs("\\n", stdout);
		else if (*p < 0x80)
			fputc(*p, stdout);
		else
		{
			fputc(0xc0 | (*p >> 6), stdout);
			fputc(0x80 | (*p & 0x3f), stdout);
		}
	}
}

static CeModsModel sModel;

int main()
{
	char line[8192];
	while (fgets(line, sizeof line, stdin))
	{
		line[strcspn(line, "\r\n")] = 0;
		int i = 0, d = 0;
		if (!strcmp(line, "read"))
		{
			sModel.Read();
			printf("ROWS %d off=%d folder=%d more=%d refused=%d loaded=", sModel.mCount, sModel.mRun.off, sModel.mRun.folder, sModel.mRun.more,
				   sModel.mRun.refused_at_start);
			for (int n = 0; n < sModel.mRun.loaded_count; n++)
				printf("%s%s", n ? "," : "", sModel.mRun.loaded[n]);
			printf("%s\n", sModel.mRun.loaded_count ? "" : "-");
		}
		else if (sscanf(line, "row %d", &i) == 1 && i >= 0 && i < sModel.mCount)
		{
			char name[80];
			CeModName(name, sizeof name, sModel.Mod(i), Shown);
			printf("ROW %s|", sModel.Mod(i).id[0] ? sModel.Mod(i).id : "-");
			Put(name);
			printf("|%s|%d|", sModel.State(i), sModel.Mod(i).why);
			int actions[kCeModsMaxActions];
			const int count = sModel.Actions(i, actions);
			for (int n = 0; n < count; n++)
				printf("%s%s", n ? "," : "", sModel.ActionLabel(i, actions[n]));
			printf("|%d\n", sModel.Mod(i).loaded);
		}
		else if (sscanf(line, "hint %d", &i) == 1 && i >= 0 && i < sModel.mCount)
		{
			static char text[1100], by[200], clash[320], why[200], longer[400];
			sModel.Description(i, text, sizeof text, Shown);
			sModel.ByLine(i, by, sizeof by, Shown);
			sModel.Clash(i, clash, sizeof clash, Shown);
			sModel.Why(i, why, sizeof why, Shown);
			sModel.ClashLong(i, longer, sizeof longer, Shown);
			printf("HINT ");
			Put(text);
			printf("|");
			Put(by);
			printf("|");
			Put(clash);
			printf("|");
			Put(why);
			printf("|");
			Put(longer);
			printf("\n");
		}
		else if (sscanf(line, "details %d", &i) >= 1 && i >= 0 && i < sModel.mCount)
		{
			static char text[2400];
			int chars = 400;
			sscanf(line, "details %d %d", &i, &chars);
			sModel.Details(i, text, sizeof text, Shown, chars);
			printf("DETAILS ");
			Put(text);
			printf("\n");
		}
		else if (sscanf(line, "detail %d", &i) == 1 && i >= 0 && i < sModel.mCount)
			printf("DETAIL %s\n", sModel.Mod(i).detail);
		else if (sscanf(line, "contents %d", &i) == 1 && i >= 0 && i < sModel.mCount)
		{
			char text[160];
			sModel.Contents(i, text, sizeof text);
			printf("CONTENTS %s\n", text);
		}
		else if (sscanf(line, "toggle %d", &i) == 1)
			printf("RESULT %d\n", sModel.Toggle(i));
		else if (sscanf(line, "move %d %d", &i, &d) == 2)
			printf("RESULT %d\n", sModel.Move(i, d));
		else if (!strcmp(line, "order"))
		{
			printf("ORDER ");
			for (int n = 0; n < sModel.mCount; n++)
			{
				const char *name = sModel.Mod(n).id[0] ? sModel.Mod(n).id : sModel.Mod(n).folder;
				printf("%s%s:%s", n ? "," : "", name, sModel.State(n));
			}
			printf("%s\n", sModel.mCount ? "" : "-");
		}
		else if (!strncmp(line, "cut ", 4))
		{
			int length = 0, at = 0;
			if (sscanf(line + 4, "%d %n", &length, &at) >= 1)
			{
				static char text[512];
				CeModCut(text, sizeof text, line + 4 + at, length);
				printf("CUT ");
				Put(text);
				printf("\n");
			}
			else
				printf("?\n");
		}
		else if (sscanf(line, "play %d %d", &i, &d) == 2 && i >= 0 && i < sModel.mCount)
			printf("PLAY %d\n", sModel.PlayLevel(i, CEMA_PLAY + d));
		else if (sscanf(line, "return %d", &i) == 1)
		{
			const int row = sModel.ReturnRow(i);
			printf("RETURN %s\n", row < 0 ? "-" : sModel.Mod(row).id);
		}
		else if (!strcmp(line, "folder"))
		{
			char dir[4200];
			printf("FOLDER %d\n", CE_MakeModsFolder(dir, sizeof dir));
		}
		else if (!strcmp(line, "at"))
			printf("AT %d\n", sModel.mAt);
		else if (sscanf(line, "pages %d", &i) == 1)
			printf("PAGES %d %d\n", CeModsPages(i), CeModsPerPage(i));
		else if (!strcmp(line, "note"))
		{
			const char *note = sModel.Note();
			printf("NOTE %s\n", note ? note : "-");
		}
		else if (!strncmp(line, "write", 5))
		{
			// the loader's own writer, as the page calls it: "write" and then the ids
			const char *ids[PORT_MODS_ON + 4];
			int n = 0;
			for (char *id = strtok(line + 5, " "); id && n < PORT_MODS_ON + 4; id = strtok(NULL, " "))
				ids[n++] = id;
			printf("WROTE %d\n", PortModsWriteLine(ids, n) ? 1 : 0);
		}
		else if (!strcmp(line, "line"))
		{
			char value[2048];
			printf("LINE %s\n", CE_SettingGetText("mods", value, sizeof value) ? value : "-");
		}
		else if (!strncmp(line, "text ", 5))
		{
			static char text[4096];
			CeModText(text, sizeof text, line + 5, Shown);
			printf("TEXT ");
			Put(text);
			printf("\n");
		}
		else if (!strcmp(line, "sentences"))
		{
			for (int why = 1; why < PORT_MOD_WHY_COUNT; why++)
				printf("SENTENCE %s\n", kCeModWhy[why]);
			char renamed[200];
			snprintf(renamed, sizeof renamed, kCeModRenamedTo, "my-mod");
			char many[200];
			snprintf(many, sizeof many, kCeModsNoticeMany, 3);
			char next[100], first[100];
			snprintf(next, sizeof next, kCeModsPageNext, 1, 3);
			snprintf(first, sizeof first, kCeModsPageFirst, 3, 3);
			for (const char *text : {(const char *)renamed, kCeModModsFolderCase, (const char *)many, kCeModsNoticeOne, kCeModsEmpty, kCeModsEmptyWhere, kCeModsNextStart, kCeModsOff, kCeModsMore, kCeModsFull, kCeModsNotSaved,
									 kCeModsOpenHint, kCeModsPs2ImportRow, kCeModsPs2ImportHint, kCeModsPs2Picking, kCeModsPs2Imported, kCeModsPs2ImportedFull,
									 (const char *)next, (const char *)first, kCeModsFolderOpen, kCeModsFolderBlocked, kCeModsNoLevel, kCeModsNoFileManager,
									 "On", "Off", "Can't Load", "Turn On", "Turn Off", "Move Up", "Move Down", "Details", "Play", "Play Level 1", "Play Level 2", "Play Level 3", "More Mods", "Open Mods Folder"})
			{
				printf("SENTENCE ");
				Put(text);
				printf("\n");
			}
			printf("END\n");
		}
		else
			printf("?\n");
		fflush(stdout);
	}
	return 0;
}
