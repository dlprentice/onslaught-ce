// SPDX-License-Identifier: GPL-3.0-only
// The Extras pages' two game-free parts, run as the game compiles them (port/tools/ce_career_view_tests.py, ce_extras_tests.py):
//   ce_extras_driver view FILE      the Service Record's reading of a career file (port/game/ce_career_view.h), as text
//   ce_extras_driver rank HEXBITS   the letter for a stored rank
//   ce_extras_driver ogg FILE       the length the Music Player reads from a file's first and last bytes (port/game/ce_ogg_length.h)
//   ce_extras_driver jukebox TRACKS OP...   the Music Player's state machine (ce_jukebox_state.h) against a fake device that logs its calls:
//                                   sN (select row N) e (the track ended) x (leave) o (open) rN (the next random number) a0 a1 (music allowed or not)
//                                   fN (row N cannot be played) h (every row can)
//   ce_extras_driver start FILE     whether the music worker could start a file (CeOggStarts), from its first CE_OGG_START bytes
//   ce_extras_driver name HEX...    each argument's bytes (hex) as the characters a list shows (ce_name_text.h), UTF-8 and ANSI, as code points
//   ce_extras_driver balance FACE TEXT   where a sentence is broken in two (CeBreakAt, ce_name_text.h) by a stand-in face's widths: the
//                                   break's index and the wider line's width; FACE is even (9 a character), mixed or wide
//   ce_extras_driver read PITCH COUNT OP...   a reading page's state after each step (port/game/ce_reading.h): u d (a line), U D (a page), wN
//                                   (the view by N lines), gY (the pointer held in the track at y), cN (the article has N lines now)
//   ce_extras_driver unlock RULE    the Goodie slot a rule begins by unlocking and the length of its name in the rule (CeUnlockedFirst), or -1
//   ce_extras_driver width FACE     the width of each line read from standard input, one per line, in that face
//   ce_extras_driver plausible FILE a career file's record, then whether the page would show it (CeCareerViewPlausible)
//   ce_extras_driver list COUNT OP...   the list box's state after each step (port/game/ce_scroll_list.h): u d (a row), U D (a page),
//                                   w+N w-N (the view by N rows), gY (the pointer held in the track at y), cN (the cursor to N),
//                                   nN (the list now has N rows, as a page whose rows changed)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ce_career_view.h"
#include "ce_scroll_list.h"
#include "ce_reading.h"
#include "ce_jukebox_state.h"
#include "ce_ogg_length.h"
#include "ce_name_text.h"

static int View(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 2;
	std::vector<unsigned char> bytes(1 << 16);
	const size_t size = fread(bytes.data(), 1, bytes.size(), f);
	fclose(f);
	static SCeCareerView view;
	if (!CeCareerViewBuild(bytes.data(), int(size), &view))
	{
		// a refusal leaves nothing behind
		static const SCeCareerView zero = {};
		printf("refused %d\n", memcmp(&view, &zero, sizeof(view)) ? 1 : 0);
		return 0;
	}
	printf("missions %d complete %d rank_s %d\n", view.mMissionCount, view.mMissionsComplete, view.mRankS);
	for (int i = 0; i < view.mMissionCount; i++)
		printf("m %d %d %c\n", view.mMissions[i].mWorld, int(view.mMissions[i].mState),
			   view.mMissions[i].mRank ? view.mMissions[i].mRank : '-');
	printf("goodies %d all %d\ng ", view.mGoodiesFound, view.mGoodiesAll);
	for (int g = 0; g < CE_CAREER_GOODIE_SLOTS; g++)
		putchar(view.mGoodie[g] <= 3 ? '0' + view.mGoodie[g] : 'x');
	printf("\nshown ");
	for (int g = 0; g < CE_CAREER_GOODIE_SLOTS; g++)
		putchar(CeGoodieShown(g) ? '1' : '0');
	printf("\nkills %d %d %d %d %d\n", view.mKills[0], view.mKills[1], view.mKills[2], view.mKills[3], view.mKills[4]);
	printf("settings %d %d %d %d\n", CeStoredVolumePercent(view.mSoundBits), CeStoredVolumePercent(view.mMusicBits),
		int(view.mInvincible[0]), int(view.mInvincible[1]));
	return 0;
}

static int Ogg(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 2;
	std::vector<unsigned char> head(CE_OGG_HEAD), tail(CE_OGG_TAIL);
	const size_t got = fread(head.data(), 1, head.size(), f);
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, size > long(tail.size()) ? size - long(tail.size()) : 0, SEEK_SET);
	const size_t end = fread(tail.data(), 1, tail.size(), f);
	fclose(f);
	printf("rate %u samples %llu seconds %d\n", CeOggSampleRate(head.data(), int(got)), CeOggSamples(tail.data(), int(end)),
		   CeOggSeconds(head.data(), int(got), tail.data(), int(end)));
	return 0;
}

namespace fake
{
static int tracks;
static bool playing, allowed = true;
static unsigned next_random;
static std::vector<std::string> calls;
static int Count() { return tracks + 1; }
static std::vector<int> bad;
static bool Unplayable(int row)
{
	for (int b : bad)
		if (b == row)
			return true;
	return false;
}
static bool Play(int row)
{
	if (Unplayable(row))
	{
		calls.push_back("refuse " + std::to_string(row));
		return false;
	}
	calls.push_back("play " + std::to_string(row));
	playing = true;
	return true;
}
static void Stop() { calls.push_back("stop"); playing = false; }
static void Restore() { calls.push_back("restore"); playing = true; }
static bool Playing() { return playing; }
static unsigned Random() { calls.push_back("random"); return next_random; }
static bool Allowed() { return allowed; }
}

static int Jukebox(int tracks, int argc, char **argv)
{
	fake::tracks = tracks;
	fake::playing = true;			// the menu theme
	const SCeJukeboxDevice device = {fake::Count, fake::Play, fake::Stop, fake::Restore, fake::Playing, fake::Random, fake::Allowed};
	SCeJukebox box;
	CeJukeboxOpen(&box);
	for (int i = 0; i < argc; i++)
	{
		const char *op = argv[i];
		fake::calls.clear();
		bool result = false;
		switch (op[0])
		{
		case 's': result = CeJukeboxSelect(&box, &device, atoi(op + 1)); break;
		case 'e': fake::playing = false; CeJukeboxProcess(&box, &device); break;
		case 'p': CeJukeboxProcess(&box, &device); break;
		case 'x': CeJukeboxClose(&box, &device); break;
		case 'o': CeJukeboxOpen(&box); break;
		case 'r': fake::next_random = unsigned(atoi(op + 1)); break;
		case 'a': fake::allowed = op[1] != '0'; break;
		case 'f': fake::bad.push_back(atoi(op + 1)); break;
		case 'h': fake::bad.clear(); break;
		default: return 2;
		}
		printf("%s %d now %d order %d touched %d failed %d calls", op, result ? 1 : 0, box.mNow, box.mOrder, box.mTouched ? 1 : 0, box.mFailed);
		for (const std::string &call : fake::calls)
			printf(" [%s]", call.c_str());
		printf("\n");
	}
	return 0;
}

static int Reading(float pitch, int count, int argc, char **argv)
{
	SCeReading page;
	CeReadOpen(&page, count);
	for (int i = -1; i < argc; i++)
	{
		if (i >= 0)
		{
			const char *op = argv[i];
			switch (op[0])
			{
			case 'u': CeReadMove(&page, -1); break;
			case 'd': CeReadMove(&page, 1); break;
			case 'U': CeReadPage(&page, -1); break;
			case 'D': CeReadPage(&page, 1); break;
			case 'w': CeReadMove(&page, atoi(op + 1)); break;
			case 'g': CeReadDrag(&page, pitch, float(atof(op + 1))); break;
			case 'c': page.mCount = atoi(op + 1); CeReadClamp(&page); break;
			default: return 2;
			}
		}
		float top, bottom;
		CeReadBar(&page, pitch, &top, &bottom);
		printf("%d %d %.3f %.3f %d %.3f\n", page.mCount, page.mTop, top, bottom, CeReadPercent(&page), CeReadBottom(pitch));
	}
	return 0;
}

static int Starts(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 2;
	std::vector<unsigned char> head(CE_OGG_START);
	const size_t got = fread(head.data(), 1, head.size(), f);
	fclose(f);
	printf("%d\n", CeOggStarts(head.data(), int(got), got < head.size()) ? 1 : 0);
	return 0;
}

static int Names(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
	{
		std::string bytes;
		for (const char *h = argv[i]; h[0] && h[1]; h += 2)
		{
			char pair[3] = {h[0], h[1], 0};
			bytes.push_back(char(strtol(pair, NULL, 16)));
		}
		for (int utf8 = 1; utf8 >= 0; utf8--)
		{
			unsigned short out[64];
			const int n = CeDecodeName(out, 64, bytes.c_str(), utf8 != 0);
			printf(utf8 ? "utf8" : "ansi");
			for (int c = 0; c < n; c++)
				printf(" %04x", out[c]);
			printf("\n");
		}
	}
	return 0;
}

static int Plausible(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 2;
	std::vector<unsigned char> bytes(1 << 16);
	const size_t size = fread(bytes.data(), 1, bytes.size(), f);
	fclose(f);
	static SCeCareerView view;
	if (!CeCareerViewBuild(bytes.data(), int(size), &view))
		printf("refused\n");
	else
		printf("%s\n", CeCareerViewPlausible(view) ? "shown" : "implausible");
	return 0;
}

static void Print(const SCeScrollList &list)
{
	float top, bottom;
	CeListBar(&list, &top, &bottom);
	printf("%d %d %d %.3f %.3f %d %d %d\n", list.mCount, list.mCursor, list.mTop, top, bottom,
		   CeListRowAt(&list, kCeListTop + 1.f), CeListRowAt(&list, CeListBottom() - 8.f), CeListRowAt(&list, 250.f));
}

// Stand-in faces for the hint area's text: proportional widths in pixels. The real face is the game's (retail art, not held here), so the
// hint area's check on it is a log line of the evidence runs; these faces make the break's rule testable by the host.
static int	Face(const char *name)
{
	return !strcmp(name, "even") ? 0 : !strcmp(name, "mixed") ? 1 : 2;
}

static float	Width(int face, char c)
{
	if (face == 0)
		return 9.f;
	const bool narrow = strchr("ijl.,';:!|", c) != NULL, mid = strchr("frtI -()", c) != NULL, big = strchr("mwMWQOGDHNU", c) != NULL;
	if (face == 1)
		return narrow ? 4.f : mid ? 6.f : big ? 13.f : (c >= 'A' && c <= 'Z') ? 10.f : 8.f;
	return narrow ? 6.f : mid ? 8.f : big ? 15.f : (c >= 'A' && c <= 'Z') ? 12.f : 10.f;
}

struct Measure
{
	int	mFace;
	float	operator()(const char *first, int count) const
	{
		float w = 0.f;
		for (int i = 0; i < count; i++)
			w += Width(mFace, first[i]);
		return w;
	}
};

// A small wrapped article whose last two headings share the final screen.
static int Contents(int argc, char **argv)
{
	const SCeReadSection sections[] = {{0, "First", 5}, {5, "Second", 6}, {20, "Third", 5}, {34, "Fourth", 6}, {39, "Last", 4}};
	SCeReading page;
	CeReadOpen(&page, 40);
	int chosen = -1;
	for (int i = 0; i < argc; ++i)
	{
		const char *op = argv[i];
		int value = atoi(op + 1);
		bool accepted = false;
		if (*op == 'j')
		{
			accepted = CeReadJump(&page, sections, 5, value);
			if (accepted) chosen = value;
		}
		else if (*op == 'w') CeReadMove(&page, value);
		else if (*op == 'e') accepted = CeReadJump(&page, NULL, 0, value);
		else if (*op != 'o') return 2;
		printf("%d %d %d\n", accepted ? 1 : 0, page.mTop, CeReadSectionAt(&page, sections, 5, chosen));
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "contents"))
		return Contents(argc - 2, argv + 2);
	if (argc == 3 && !strcmp(argv[1], "view"))
		return View(argv[2]);
	if (argc >= 3 && !strcmp(argv[1], "jukebox"))
		return Jukebox(atoi(argv[2]), argc - 3, argv + 3);
	if (argc == 3 && !strcmp(argv[1], "ogg"))
		return Ogg(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "start"))
		return Starts(argv[2]);
	if (argc >= 3 && !strcmp(argv[1], "name"))
		return Names(argc - 2, argv + 2);
	if (argc == 4 && !strcmp(argv[1], "balance"))
	{
		const int face = Face(argv[2]);
		const Measure measure = {face};
		float widest = 0.f;
		const int at = CeBreakAt(argv[3], measure, &widest);
		printf("%d %.1f\n", at, widest);
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[1], "read"))
		return Reading(float(atof(argv[2])), atoi(argv[3]), argc - 4, argv + 4);
	if (argc == 3 && !strcmp(argv[1], "unlock"))
	{
		int length = 0;
		const int slot = CeUnlockedFirst(argv[2], &length);
		printf("%d %d\n", slot, slot < 0 ? 0 : length);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "width"))
	{
		const int face = Face(argv[2]);
		char line[512];
		while (fgets(line, sizeof(line), stdin))
		{
			int n = int(strlen(line));
			while (n > 0 && (line[n - 1] == '\n'))
				line[--n] = 0;
			const Measure measure = {face};
			printf("%.1f\n", measure(line, n));
		}
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "plausible"))
		return Plausible(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "rank"))
	{
		const char letter = CeRankLetter(unsigned(strtoul(argv[2], NULL, 16)));
		printf("%c\n", letter ? letter : '-');
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "list"))
	{
		SCeScrollList list = {};
		CeListReset(&list, atoi(argv[2]));
		Print(list);
		for (int i = 3; i < argc; i++)
		{
			const char *op = argv[i];
			bool moved = false;
			switch (op[0])
			{
			case 'u': moved = CeListMove(&list, -1); break;
			case 'd': moved = CeListMove(&list, 1); break;
			case 'U': moved = CeListPage(&list, -1); break;
			case 'D': moved = CeListPage(&list, 1); break;
			case 'w': moved = CeListScroll(&list, atoi(op + 1)); break;
			case 'g': CeListDrag(&list, float(atof(op + 1))); break;
			case 'c': list.mCursor = atoi(op + 1); CeListClamp(&list); break;
			case 'n': list.mCount = atoi(op + 1); CeListClamp(&list); break;
			default: return 2;
			}
			printf("%s %d ", op, moved ? 1 : 0);
			Print(list);
		}
		return 0;
	}
	printf("geometry %.1f %.1f %.1f %.1f %.1f %d %.1f\n", kCeListLeft, kCeListTop, kCeListPitch, kCeListTrack, kCeListRight, kCeListRows,
		   CeListBottom());
	return 0;
}
