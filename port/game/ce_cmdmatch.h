// SPDX-License-Identifier: GPL-3.0-only
// Commander match (port/game/ce_cmdmatch.cpp): where the Commander is admitted, and what a match looks like from above.
//
// A Commander match is a versus level (the game's levels 850 to 899: two Battle Engines, two sides, the level's own script decides the winner) played by ONE
// person, who is player 1 and commands player 1's side. The second Battle Engine stands in the world with nobody at its controls and belongs to the other side.
// The match is asked for when the game is started straight into such a level (the command line's -level): nobody else is at the controls then. A versus
// level chosen in the game's own Multiplayer menu is two people's game and is never a Commander match.
//
// Like the rest of the Commander it exists in every CE build and does nothing unless the Commander is on (ce.ini, [Experimental], commander=1): the callers ask
// only after the order layer's own switch and fence have passed.
#ifndef PORT_CE_CMDMATCH_H
#define PORT_CE_CMDMATCH_H

#ifdef CE_VERSION

// Whether the level that is running is a Commander match.
bool	CeCmdMatch();

// The Commander's one admission rule, asked by the order layer's update and by the map's gate (and so by the live camera's context, which follows that gate):
// an ordinary single-player level, which the caller states in the game's own words, or a Commander match.
inline bool	CeCmdAdmits(bool ordinarySolo) { return ordinarySolo || CeCmdMatch(); }

// Once for every update of a level the order layer runs in (ce_commander.cpp), and when the level ends (ce_cmdmap.cpp's PortCeCmdLevelEnd): a Commander match
// shows player 1's view alone, on the whole screen (the game's own "player 1 fullscreen" switch), and the switch is put back as it was when the level ends.
void	CeCmdMatchUpdate();
void	CeCmdMatchLevelEnd();
// Patch 0235: true while a Commander match shows its one screen, so that the HUD's drawing and the end screen take the layout they have for one player
// (the radar, the aiming ring, the battle-line dial, the radio, one Victory or Defeat). False everywhere else: the game's own test decides as it always did.
bool	PortCeOneScreenMatch();

// What the battle looks like from above, read from the game and never written: for the map's own lines and for a test build's probe.
enum CeCmdMatchResult { CE_MATCH_RUNNING = 0, CE_MATCH_WON, CE_MATCH_LOST, CE_MATCH_DRAWN, CE_MATCH_OVER };
struct CeCmdMatchState
{
	bool match;					// a Commander match (else an ordinary level)
	int gameState;				// the game's own state number (GAME_STATE_PLAYING while the battle runs)
	int result;					// CeCmdMatchResult: the level's verdict as the commander sees it
	int side;					// the commander's allegiance, -1 without a Battle Engine
	int nodes, held, lost;		// the battle line's nodes: all, the ones the commander's side holds, the ones the other side holds
	int buildings[2];			// live buildings: [0] the commander's side, [1] every other side
	float baseX0, baseY0, baseX1, baseY1;	// the rectangle round the commander's side's live buildings (when it has any)
	int guns[2];				// live gun emplacements, the same way
	int units[2];				// live mobile units (ground vehicles, infantry, aircraft), the same way
	float baseLife;				// the life of the commander's side's live buildings, summed (the match's alerts: ce_cmdrules.h)
	bool pilot;					// the commander's Battle Engine is in the world and alive
	float life;					// its life, 0 without one
	float lifeFull;				// the life it started with
	float x, y;					// where it stands
	float headingX, headingY;	// the way it faces (its forward axis on the ground plane)
};
void	CeCmdMatchRead(CeCmdMatchState *out);

#endif

#endif
