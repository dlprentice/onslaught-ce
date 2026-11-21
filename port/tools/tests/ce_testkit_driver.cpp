// SPDX-License-Identifier: GPL-3.0-only
// Host driver for the test kit's core (port/platform/common/ce_testkit.cpp), compiled with CE_TEST_KIT=1 and the platform's
// three calls stubbed: the Present count (this driver advances it), the key path (it feeds an authored two-page menu) and
// the user folder (a scratch folder). It checks the step grammar, select by text with real key presses, go, waits, asserts,
// state keys, budgets and the failure dump, as ce_testkit_tests.py runs it: argv[1] is the step file's scenario name.
#include "../../platform/common/ce_testkit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static unsigned gPresent = 1000;
extern "C" unsigned PortPresentCount(void) { return gPresent; }
static std::string gUser;
const char *PortUserDir() { return gUser.c_str(); }

// An authored menu: page "menu" with five rows, Return on "Settings" opens page "settings" (two rows), Esc goes back.
static int gPage = 0, gFocus = 0, gKeysDown = 0;
static const char *const kMenu[] = {"New Game", "Load Game", "Settings", "Settings Extra", "Quit"};
static const char *const kSettings[] = {"Sound", "Back"};
static int Count() { return gPage == 0 ? 5 : 2; }
void PortTestKitKey(unsigned sdl, unsigned raw, bool press)
{
	(void)raw;
	if (!press) return;
	++gKeysDown;
	if (sdl == 81 && gFocus + 1 < Count()) ++gFocus;			// Down
	else if (sdl == 82 && gFocus > 0) --gFocus;				// Up
	else if (sdl == 40 && gPage == 0 && gFocus == 2) { gPage = 1; gFocus = 0; }	// Return on Settings
	else if (sdl == 41 && gPage == 1) { gPage = 0; gFocus = 2; }	// Esc
}
static int gClicks = 0;
static float gRelX = 0;
void PortTestKitMouse(int kind, float x, float y) { (void)y; if (kind == 2 || kind == 5) ++gClicks; if (kind == 3) gRelX += x; }
static bool StateRel(char *out, size_t n) { snprintf(out, n, "%.0f", gRelX); return true; }
CE_TEST_STATE("test.relx", StateRel);
static bool StatePose(char *out, size_t n) { snprintf(out, n, "1.50,2.25,90.0,%d.5", gClicks); return true; }
CE_TEST_STATE("test.pose", StatePose);
static bool StateClicks(char *out, size_t n) { snprintf(out, n, "%d", gClicks); return true; }
CE_TEST_STATE("test.clicks", StateClicks);
static bool Active() { return true; }
static void Id(char *out, size_t n) { snprintf(out, n, "%s", gPage == 0 ? "t:menu" : "t:settings"); }
static void Title(char *out, size_t n) { snprintf(out, n, "%s", gPage == 0 ? "Main" : "Settings"); }
static int Rows(CeTestRow *out, int max)
{
	int n = 0;
	for (int i = 0; i < Count() && n < max; ++i, ++n)
	{
		snprintf(out[n].text, sizeof out[n].text, "%s", gPage == 0 ? kMenu[i] : kSettings[i]);
		out[n].enabled = true;
	}
	return n;
}
static int Focused() { return gFocus; }
static void Box(char *out, size_t n) { snprintf(out, n, "%s", gPage == 1 ? "Settings are saved." : ""); }
CE_TEST_PAGE("t", 1, Active, Id, Title, Rows, Focused, Box);
static bool Volume(char *out, size_t n) { snprintf(out, n, "%d", gPage == 1 ? 7 : 0); return true; }
CE_TEST_STATE("test.volume", Volume);
// A feature's own verb, registered from its own file, tagged TESTWRITE.
static CeTestResult Poke(CeTestArgs &, CeTestReply &r) { CeTestReplyf(r, "poked"); return CE_TEST_DONE; }
CE_TEST_VERB("poke", "poke: a direct state write (a test of the TESTWRITE mark)", CE_TEST_TESTWRITE, Poke);

int main(int argc, char **argv)
{
	if (argc != 3) return 80;
	gUser = argv[2];
	for (int frame = 0; frame < 20000 && !CeTestKitWantsQuit(); ++frame)
	{
		CeTestKitFrame();
		CeTestKitFrame();		// a second pump in the same Present does nothing
		++gPresent;
	}
	printf("quit=%d keys=%d page=%d focus=%d\n", CeTestKitWantsQuit() ? 1 : 0, gKeysDown, gPage, gFocus);
	return 0;
}
