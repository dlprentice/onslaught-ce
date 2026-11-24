// SPDX-License-Identifier: GPL-3.0-only
// Commander match (ce_cmdmatch.h): the admission of a versus level one person plays, its one screen, and the battle as the commander reads it.
//
// It reads the game and writes one thing of the game's: the switch that shows player 1's view alone on the whole screen (the game's own console variable
// cg_fullscreenmultiplayer, "Show only player 1 fullscreen"), set for a Commander match and put back when the level ends. A versus level always loads two
// Battle Engines and would draw two half screens; one person needs one. Nothing the level's script reads changes with it, but the game's render does more
// than draw one view under it: as it does for one player, it reads the mouse each frame and turns player 1's walker by it, and it gives the engine one
// viewpoint. With the switch set the HUD and the end screen take their one-player layout too (PortCeOneScreenMatch, patch 0235).
// It allocates nothing, draws no random number, and walks the game's sets with iterator objects that keep their place on the stack.
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "unit.h"
#include "thing.h"
#include "CLIParams.h"
#include "InfluenceMap.h"
#include "ce_commander.h"
#include "ce_cmdmatch.h"
#include "ce_cmdrules.h"

#include <string.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error The commander match requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SessionActive(void) __attribute__((weak));
extern "C" int PortCeSoloSkirmishActive(void) __attribute__((weak));

namespace
{
// The game's "player 1 fullscreen" switch is protected: named through a pointer to the member, by a struct derived from the game (as the map names the pause flag).
struct GameAccess : CGame
{
	static BOOL CGame::*Fullscreen() { return &GameAccess::mFullscreenMultiplayer; }
};

bool gScreenSet;		// this level's match set the switch
BOOL gScreenWas;		// what it was before
int gLastSide = -1;		// last known valid allegiance (0 or 1) for the current level
int gLastSideLevel = -1;
CeCmdCliLevelLatch gCliLatch = {false, false, -1};

CBattleEngine *Engine()
{
	CPlayer *player = GAME.GetPlayer(0);
	return player ? player->GetBattleEngine() : NULL;
}
}

bool CeCmdMatch()
{
	// A versus/skirmish level with its two Battle Engines, played by one person (either via -level or Solo Skirmish vs AI; the rule: ce_cmdrules.h).
	const bool solo = (PortCeSoloSkirmishActive && PortCeSoloSkirmishActive()) != 0;
	const int curLevel = GAME.GetCurrentLevel();
	const bool cliConsumed = CeCmdCliLevelCheck(gCliLatch, CLIPARAMS.mLevelNo, curLevel);
	const bool netActive = (CE_BootRequested && CE_BootRequested()) || (CE_SessionActive && CE_SessionActive());
	return CeCmdMatchRule(netActive, GAME.IsMultiplayer() != FALSE, GAME.GetNPlayers(), CLIPARAMS.mLevelNo, curLevel, solo, cliConsumed);
}

void CeCmdMatchUpdate()
{
	if (gScreenSet || !CeCmdMatch()) return;
	gScreenWas = GAME.*GameAccess::Fullscreen();
	GAME.*GameAccess::Fullscreen() = TRUE;
	gScreenSet = true;
}

bool PortCeOneScreenMatch() { return gScreenSet; }

bool PortCeAudioSingleListener()
{
	return gScreenSet || !GAME.IsMultiplayer() || (CE_BootRequested && CE_BootRequested()) || (CE_SessionActive && CE_SessionActive());
}

void CeCmdMatchLevelEnd()
{
	const bool hadScreen = gScreenSet;
	if (gScreenSet)
	{
		GAME.*GameAccess::Fullscreen() = gScreenWas;
		gScreenSet = false;
	}
	gLastSide = -1;
	gLastSideLevel = -1;
	if (hadScreen)
		CeCmdCliLevelConsume(gCliLatch);
}

void CeCmdMatchRead(CeCmdMatchState *out)
{
	memset(out, 0, sizeof(*out));
	out->side = -1;
	out->match = CeCmdMatch();
	out->gameState = (int)GAME.GetGameState();
	switch (GAME.GetGameState())
	{
	case GAME_STATE_NOT_RUNNING: case GAME_STATE_PRE_RUNNING: case GAME_STATE_PANNING: case GAME_STATE_PLAYING: out->result = CE_MATCH_RUNNING; break;
	case GAME_STATE_LEVEL_WON: case GAME_STATE_PLAYER_1_WON: out->result = CE_MATCH_WON; break;		// the commander is player 1
	case GAME_STATE_LEVEL_LOST: case GAME_STATE_PLAYER_2_WON: out->result = CE_MATCH_LOST; break;
	case GAME_STATE_GAME_DRAWN: out->result = CE_MATCH_DRAWN; break;
	default: out->result = CE_MATCH_OVER; break;
	}
	CBattleEngine *engine = Engine();
	out->side = CeCmdResolveAllegiance(gLastSide, gLastSideLevel, GAME.GetCurrentLevel(), engine != NULL, engine ? (int)engine->GetAllegiance() : -1);
	if (engine)
	{
		out->life = engine->GetLife();
		out->lifeFull = engine->GetInitialLife();
		out->pilot = !engine->IsDying() && out->life > 0.f;
		out->x = engine->GetPos().X;
		out->y = engine->GetPos().Y;
		const FVector forward = engine->GetOrientation() * FVector(0, 1, 0);
		out->headingX = forward.X;
		out->headingY = forward.Y;
	}
	// The battle line as the level's script counts it (IScript::GetRatioBattleLineNodes): a node's side is 2 where the first allegiance holds it and 1
	// where the second does.
	if (INFLUENCEMAP)
	{
		ListIterator<CInfluenceNode> it(&INFLUENCEMAP->mNodes);
		for (CInfluenceNode *node = it.First(); node; node = it.Next())
		{
			++out->nodes;
			if (out->side != 0 && out->side != 1) continue;
			const int holder = node->mSide == 2 ? 0 : node->mSide == 1 ? 1 : -1;
			if (holder == out->side) ++out->held;
			else if (holder >= 0) ++out->lost;
		}
	}
	ListIterator<CThing> things(&WORLD.GetThingNB());
	for (CThing *t = things.First(); t; t = things.Next())
	{
		const ULONG type = t->GetThingType();
		if (!(type & THING_TYPE_UNIT) || (type & THING_TYPE_BATTLE_ENGINE)) continue;
		CUnit *unit = (CUnit *)t;
		if (unit->IsDying()) continue;
		const int allegiance = (int)unit->GetAllegiance();
		if (allegiance != 0 && allegiance != 1) continue;		// neutral things are no side's strength
		const int who = allegiance == out->side ? 0 : 1;
		if (type & THING_TYPE_BUILDING)
		{
			if (who == 0)
			{
				const float x = unit->GetPos().X, y = unit->GetPos().Y;
				if (!out->buildings[0]) { out->baseX0 = out->baseX1 = x; out->baseY0 = out->baseY1 = y; }
				if (x < out->baseX0) out->baseX0 = x;
				if (x > out->baseX1) out->baseX1 = x;
				if (y < out->baseY0) out->baseY0 = y;
				if (y > out->baseY1) out->baseY1 = y;
				out->baseLife += unit->GetLife();
			}
			++out->buildings[who];
		}
		else if (type & THING_TYPE_EMPLACEMENT) ++out->guns[who];
		else if (type & (THING_TYPE_GROUND_UNIT | THING_TYPE_AIR_UNIT | THING_TYPE_INFANTRY | THING_TYPE_MECH | THING_TYPE_VEHICLE | THING_TYPE_NAVAL)) ++out->units[who];
	}
}

#endif
