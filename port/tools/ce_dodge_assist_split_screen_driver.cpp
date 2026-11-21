// SPDX-License-Identifier: GPL-3.0-only
// port/tools/ce_dodge_assist_split_screen_driver.cpp: how often the Dodge Button reads ce.ini with two controllers, one of them out of the game.
//
// This is the fixture of ce_dodge_assist_driver.cpp (the actual ce_dodge_assist.cpp and patched CController::DoMappings beside the game's own
// strafe code; its main is set aside) with scripts of its own for two players. The fixture's ce.ini is a counter: every CE_SettingGetBool is
// one read of the whole file in the game. A player who waits to come back is played as a controller that drives no player: the fixture's
// player always has an engine, and ce_dodge_assist.cpp sees the same thing either way (no Battle Engine for that controller).
//
// Usage: driver MODE. Prints the mode's lines; port/tools/ce_dodge_assist_split_screen_tests.py compares them.
#define main fixture_main
#include "ce_dodge_assist_driver.cpp"
#undef main

namespace {
// One update of both controllers, in the game's order (controller 0, then controller 1).
struct Pair { bool ctrl = false, d = false; bool padB = false; float stick = 0; };
unsigned gUpdate;
void Both(const Pair &p)
{
	EVENT_MANAGER.time += 1.f / 20; ++EVENT_MANAGER.frame;
	memcpy(gC.keysBefore, gC.keys, sizeof gC.keys); memcpy(gC2.keysBefore, gC2.keys, sizeof gC2.keys);
	gC.keys[DIK_D] = p.d; gC.keys[DIK_LCONTROL] = p.ctrl;
	gPadButtons[0] = 0; gPadAxis[0] = 0; gPadButtons[1] = p.padB ? 1u : 0; gPadAxis[1] = p.stick;
	gC.update = gC2.update = gUpdate++;
	gC.Flush(); gC2.Flush();
	players[0].engine.part.EndOfFrame(); players[1].engine.part.EndOfFrame();
}
void TwoPlayers()
{
	Reset(gC); GAME.n = 2; GAME.controllers[1] = &gC2; gC2 = CController(); gC2.mPadNumber = 1; gC2.player = 1; gC2.toControl = &players[1];
	gUpdate = 0;
}
// The front end: neither controller drives a player.
void FrontEnd(int updates)
{
	gC.toControl = nullptr; gC2.toControl = nullptr;
	for (int i = 0; i < updates; ++i) Both(Pair());
}
void Level()
{
	players[0] = CPlayer(); players[1] = CPlayer(); dashes[0] = Dash(); dashes[1] = Dash(); events.clear();
	gC.toControl = &players[0]; gC2.toControl = &players[1];
}
// Player one dodges right with Left Ctrl and D; player two dodges left with the pad's button and stick, ten updates later.
void BothDodge(int updates)
{
	for (int i = 0; i < updates; ++i)
	{
		Pair p; p.ctrl = i >= 4 && i < 6; p.d = i >= 4 && i < 20; p.padB = i >= 14 && i < 16; p.stick = i >= 14 && i < 30 ? -1.f : 0.f;
		Both(p);
	}
}
}

int main(int argc, char **argv)
{
#ifdef __linux__
	prctl(PR_SET_DUMPABLE, 0);
#endif
	if (argc != 2) return 1;
	const char *mode = argv[1];
	const char *envs[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};
	for (const char *e : envs) unsetenv(e);
	CBattleEngineWalkerPart::mDashStart = 0.9f; CBattleEngineWalkerPart::mDashEnd = 0.8f;

	if (Is(mode, "one-player-waits"))
	{
		// Both in a level; then player two is out of the game for 100 controller updates (the five seconds before a player comes back) while
		// player one plays on, dodging; then player two is back.
		TwoPlayers(); gIni = 1; gIniReads = 0;
		BothDodge(40);
		printf("in the level: %d reads of ce.ini, %d and %d dashes\n", gIniReads, dashes[0].count, dashes[1].count);
		int before = gIniReads, dashed = dashes[0].count;
		gC2.toControl = nullptr;
		for (int i = 0; i < 100; ++i) { Pair p; p.ctrl = i % 50 >= 4 && i % 50 < 6; p.d = i % 50 >= 4 && i % 50 < 20; Both(p); }
		printf("while player two waits, 100 updates: %d reads of ce.ini, %d dashes of player one\n", gIniReads - before, dashes[0].count - dashed);
		before = gIniReads;
		players[1] = CPlayer(); dashes[1] = Dash(); gC2.toControl = &players[1];
		for (int i = 0; i < 40; ++i) { Pair p; p.padB = i >= 4 && i < 6; p.stick = i >= 4 && i < 20 ? -1.f : 0.f; Both(p); }
		printf("player two is back, 40 updates: %d reads of ce.ini, %d dashes of player two\n", gIniReads - before, dashes[1].count);
	}
	else if (Is(mode, "player-one-waits"))
	{
		// The same with the players exchanged: the controller that is updated first is the one that is out.
		TwoPlayers(); gIni = 1; gIniReads = 0;
		BothDodge(40);
		int before = gIniReads, dashed = dashes[1].count;
		gC.toControl = nullptr;
		for (int i = 0; i < 100; ++i) { Pair p; p.padB = i % 50 >= 4 && i % 50 < 6; p.stick = i % 50 >= 4 && i % 50 < 20 ? -1.f : 0.f; Both(p); }
		printf("while player one waits, 100 updates: %d reads of ce.ini, %d dashes of player two\n", gIniReads - before, dashes[1].count - dashed);
	}
	else if (Is(mode, "off-player-one-waits") || Is(mode, "off-player-two-waits"))
	{
		// The row is Off. Then the assist is idle and the filter hook asks as well (for each strafe action of a controller that drives an engine): one
		// player waits while the other strafes, the waiting one's controller updated first or second. The filter hook must name its pad too.
		const bool oneWaits = Is(mode, "off-player-one-waits");
		TwoPlayers(); gIni = 0; gIniReads = 0;
		BothDodge(40);
		const int before = gIniReads;
		(oneWaits ? gC : gC2).toControl = nullptr;
		for (int i = 0; i < 100; ++i)
		{
			Pair p;
			if (oneWaits) p.stick = i % 10 < 5 ? -1.f : 1.f;
			else p.d = i % 10 < 5;
			Both(p);
		}
		printf("off, in the level: %d reads of ce.ini, %d and %d dashes\n", before, dashes[0].count, dashes[1].count);
		printf("off, while player %s waits and player %s strafes, 100 updates: %d reads of ce.ini\n", oneWaits ? "one" : "two", oneWaits ? "two" : "one",
			   gIniReads - before);
	}
	else if (Is(mode, "setting-changed-in-the-front-end"))
	{
		// Off in the front end after a level with it on: neither player has it in the next level; on again: both have it.
		TwoPlayers(); gIni = 1; gIniReads = 0;
		BothDodge(40);
		printf("on: %d and %d dashes\n", dashes[0].count, dashes[1].count);
		FrontEnd(5); gIni = 0; FrontEnd(5);
		int before = gIniReads;
		Level(); BothDodge(40);
		printf("off in the front end: %d and %d dashes, %d reads of ce.ini\n", dashes[0].count, dashes[1].count, gIniReads - before);
		FrontEnd(5); gIni = 1; FrontEnd(5);
		before = gIniReads;
		Level(); BothDodge(40);
		printf("on again: %d and %d dashes, %d reads of ce.ini\n", dashes[0].count, dashes[1].count, gIniReads - before);
	}
	else if (Is(mode, "help-asks-first"))
	{
		// The controls help asks whether the Dodge Button is on before any level (one read); the row is then switched off; the level must still ask.
		TwoPlayers(); gIni = 1; gIniReads = 0;
		FrontEnd(3);
		bool on = PortCeDodgeAssistOn();
		printf("the help, before any level: %s, %d reads of ce.ini\n", on ? "on" : "off", gIniReads);
		FrontEnd(3); gIni = 0; FrontEnd(3);
		int before = gIniReads;
		Level(); BothDodge(40);
		printf("off in the front end: %d and %d dashes, %d reads of ce.ini\n", dashes[0].count, dashes[1].count, gIniReads - before);
	}
	else if (Is(mode, "one-player"))
	{
		// One player, as the other suite plays it: a level asks once, and a second level once more.
		Reset(gC); gIni = 1; gIniReads = 0;
		Play(gC, Press(false, 3, 40, 10, 10, 50));
		printf("one player, a level: %d reads of ce.ini, %d dashes\n", gIniReads, dashes[0].count);
		gC.toControl = nullptr; Play(gC, Frames(5, [](int, Step &) {}));
		gC.toControl = &players[0]; dashes[0] = Dash();
		Play(gC, Press(false, 3, 40, 10, 10, 50));
		printf("one player, the next level: %d reads of ce.ini in all, %d dashes\n", gIniReads, dashes[0].count);
	}
	else
		return 1;
	return 0;
}
