// SPDX-License-Identifier: GPL-3.0-only
// The CE end-of-level statistics panel: the block patch 0104 adds to CGame's end screen (game.cpp after every port
// patch, PANEL_INC), the actual ce_debrief.cpp and ce.cpp's ce.ini reader, against recording fakes of the game's fonts,
// text database and player. One case per process: ce_debrief_tests.py.
#include "ce_debrief_fake.h"
#include <map>
#include <string>
#include <vector>

BOOL PLAYABLE_DEMO = FALSE;
FakeCli CLIPARAMS;
static bool recording = false;
bool PortRecording() { return recording; }
extern "C" bool SDL_OpenURL(const char *) { return false; }	// ce.cpp's report page, never called here
extern "C" unsigned long long SDL_GetTicks(void) { return 0; }	// ce.cpp's settings retry clock, never read here
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }	// ce.cpp's cheat gate: no network session in this fixture

struct Draw { int font; float x, y; DWORD colour; std::wstring text; };
static std::vector<Draw> draws;
struct FakeFont {
	int which; float height;
	void GetTextExtent(const WCHAR *t, SIZE *s) { s->cx = 8 * (long)std::wcslen(t); s->cy = (long)height; }
	void DrawTextShadowed(float x, float y, DWORD c, const WCHAR *t) { draws.push_back({which, x, y, c, t}); }
	int GetHeight() { return (int)height; }
};
static FakeFont small{FONT_SMALL, 16.f}, normal{FONT_NORMAL, 24.f};
struct { FakeFont *Font(int f) { return f == FONT_NORMAL ? &normal : &small; } } PLATFORM;
static const std::map<int, std::wstring> kText = {
	{TOTAL_MISSION_TIME, L"Total mission time"}, {TIME_IN_JET_MODE, L"Time in jet mode"},
	{TIME_IN_WALKER_MODE, L"Time in walker mode"}, {ROUNDS_FIRED, L"Rounds fired"}, {ACCURACY, L"Accuracy"},
	{DAMAGE_TAKEN, L"Damage taken"}};
static std::wstring held[6];
struct { WCHAR *GetString(int id) { held[id] = kText.at(id); return (WCHAR *)held[id].c_str(); } } TEXT_DB;
static WCHAR ring[4][256]; static int ringAt = 0;
WCHAR *ToWCHAR(const char *t) {
	ringAt = (ringAt + 1) % 4;
	size_t i = 0;
	for (; t[i] && i < 255; ++i) ring[ringAt][i] = (unsigned char)t[i];
	ring[ringAt][i] = 0;
	return ring[ringAt];
}
struct CPlayer { int stat[PS_NUM_PLAYERSTATS] = {}; BOOL god = FALSE; int GetStat(int s) { return stat[s]; } BOOL IsGod() { return god; } };

struct Game {
	bool multiplayer = false; int players = 1;
	bool IsMultiplayer() { return multiplayer; }
	int GetNPlayers() { return players; }
	void Panel(CPlayer *p, int x, int y, float cc, float nc, DWORD my_c) {
		(void)cc;
#include "PANEL_INC"
	}
} GAME;

static void check(bool ok, const char *what) {
	if (!ok) { std::fprintf(stderr, "FAIL: %s (%zu draws)\n", what, draws.size()); std::exit(2); }
}
static bool Fits() {
	for (const Draw &d : draws) {
		const float h = d.font == FONT_NORMAL ? 24.f : 16.f;
		// Inside the screen and above the lost level's Retry/Quit menu (its first item's top: 70 - 60 + 240 + 24 + 72).
		if (d.x < 0 || d.y < 0 || d.x + 8.f * d.text.size() > 640.f || d.y + h > 346.f - 4.f) return false;
	}
	return true;
}
static const Draw *Find(const std::wstring &t) { for (const Draw &d : draws) if (d.text == t) return &d; return nullptr; }

int main(int argc, char **argv) {
	if (argc != 2) return 3;
	const std::string mode = argv[1];
	recording = std::getenv("TEST_RECORDING") != nullptr;
	CLIPARAMS.mAttractMode = std::getenv("TEST_ATTRACT") != nullptr;
	PLAYABLE_DEMO = std::getenv("TEST_DEMO") != nullptr;
	GAME.multiplayer = std::getenv("TEST_MULTI") != nullptr;
	CPlayer p;
	p.stat[PS_TIMEASJET] = 200; p.stat[PS_TIMEASWALKER] = 410; p.stat[PS_ROUNDSFIRED] = 48; p.stat[PS_ROUNDSHIT] = 18;
	p.stat[PS_DAMAGETAKEN] = 256 * 7 / 2;
	const DWORD my_c = 0xffffa040;
	const float cc = 170.f, nc = 190.f;
	if (mode == "off") {
		GAME.Panel(&p, 65, 200, cc, nc, my_c);
		check(draws.empty(), "nothing is drawn");
		std::puts("panel off");
		return 0;
	}
	if (mode == "rows") {
		GAME.Panel(&p, 65, 200, cc, nc, my_c);
		const wchar_t *labels[] = {L"Total mission time", L"Time in jet mode", L"Time in walker mode", L"Rounds fired", L"Accuracy", L"Damage taken"};
		const wchar_t *values[] = {L"30.50s", L"10.00s", L"20.50s", L"48", L"37.50%", L"3.50"};
		check(draws.size() == 18, "six rows of label, colon and value");
		for (int i = 0; i < 6; ++i) {
			const Draw &l = draws[3 * i], &c = draws[3 * i + 1], &v = draws[3 * i + 2];
			check(l.text == labels[i] && c.text == L":" && v.text == values[i], "the game's labels, in order, with Stuart's formats");
			check(l.x == 65.f && c.x == 65.f + cc && v.x == 65.f + nc, "label at x, colon at x + cc, value at x + nc");
			check(l.y == 200.f + 16.f * i && c.y == l.y && v.y == l.y, "16 apart from y");
			check(l.colour == my_c && v.colour == my_c && l.font == FONT_SMALL, "my_c in the small font");
		}
		check(!Find(L"CHEATER!"), "no CHEATER! without god mode");
		// No round fired: no accuracy row, no division by zero.
		draws.clear(); p.stat[PS_ROUNDSFIRED] = 0; p.stat[PS_ROUNDSHIT] = 0;
		GAME.Panel(&p, 65, 200, cc, nc, my_c);
		check(draws.size() == 15 && !Find(L"Accuracy") && Find(L"Damage taken")->y == 200.f + 16.f * 4, "accuracy only after a round was fired");
		// God mode was on: CHEATER! in red, the normal font, under the last row.
		draws.clear(); p.stat[PS_CHEATED] = 1; p.stat[PS_ROUNDSFIRED] = 48; p.stat[PS_ROUNDSHIT] = 18;
		GAME.Panel(&p, 65, 200, cc, nc, my_c);
		const Draw *cheat = Find(L"CHEATER!");
		check(cheat && cheat->colour == 0xffff0000 && cheat->font == FONT_NORMAL && cheat->x == 65.f && cheat->y == 200.f + 16.f * 6,
			  "CHEATER! in red under the rows");
		check(Fits(), "inside the screen");
		// God mode brought in from the career or the Cheats page counts no PS_CHEATED; it is still CHEATER!.
		draws.clear(); p.stat[PS_CHEATED] = 0; p.god = TRUE;
		GAME.Panel(&p, 65, 200, cc, nc, my_c);
		check(Find(L"CHEATER!") != nullptr, "CHEATER! while god mode is on");
		p.stat[PS_CHEATED] = 1; p.god = FALSE;
		// A level with many objectives: the rows go in two columns, all inside the screen.
		draws.clear();
		// A lost level's reason text: the rows rise into the gap above them, up to 22, and stay one column.
		draws.clear(); p.stat[PS_CHEATED] = 0;
		GAME.Panel(&p, 65, 258, cc, nc, my_c);
		check(draws.size() == 18 && Fits() && Find(L"Total mission time")->y == 246.f && Find(L"Damage taken")->x == 65.f,
			  "a small overrun rises instead of splitting");
		draws.clear(); p.stat[PS_CHEATED] = 1;
		GAME.Panel(&p, 65, 260, cc, nc, my_c);
		check(draws.size() == 19 && Fits(), "two columns when one would reach the menu");
		check(Find(L"Rounds fired")->x > 65.f + nc && Find(L"Rounds fired")->y == 260.f, "the second column starts level with the first");
		check(Find(L"CHEATER!")->y == 260.f + 16.f * 3, "CHEATER! under the first column");
		// Further down still: only the rows that fit, nothing past the bottom.
		draws.clear();
		GAME.Panel(&p, 65, 320, cc, nc, my_c);
		check(!draws.empty() && draws.size() < 18 && Fits(), "rows that do not fit are left out");
		draws.clear();
		GAME.Panel(&p, 65, 340, cc, nc, my_c);
		check(draws.empty(), "no room, no rows");
		// Long labels (a translation): the retail loop widens nc; values stay inside.
		draws.clear();
		GAME.Panel(&p, 65, 200, 330.f, 350.f, my_c);
		check(Fits(), "a wide label column still fits");
		draws.clear();
		GAME.Panel(nullptr, 65, 200, cc, nc, my_c);
		check(draws.empty(), "no player, no panel");
		std::puts("panel rows passed");
		return 0;
	}
	return 3;
}
