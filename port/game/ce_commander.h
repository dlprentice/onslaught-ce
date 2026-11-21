// SPDX-License-Identifier: GPL-3.0-only
// Commander mode (port/game/ce_commander.cpp, the order layer; port/game/ce_cmdmap.cpp, the map; patches 0160 to 0162).
// The order layer and the map are part of every CE build (CE_VERSION) and do nothing unless ce.ini says commander=1; what a test build adds (the order tape,
// the probe, the proof switch and the diagnostics) exists only with -DCE_COMMANDER_TEST (port/build.py --commander-test).
#ifndef PORT_CE_COMMANDER_H
#define PORT_CE_COMMANDER_H

#ifdef CE_VERSION

class FVector;

// Once at the top of every CGame::Update (port/game/updatedump.cpp, PortUpdateHook): keeps the table of the level's squads current while the map is on.
void	CeCommanderUpdate();

// The paused map's window on the world, for the frame-feel log (feel.h): out[0..2] = the lower-left corner and the span; 1 while the paused map is open, else 0.
extern "C" int	CeCmdFeelView(float *out);

// Patch 0161, CNormalSquad::CanMerge: true for a squad that has an order from the map (and for the squad it would merge into), so it is not dissolved into
// another squad.
bool	CeCommanderNoMerge(const void *squad);

// Patch 0160, CRelaxedSquad::Unknown66 (every 5 to 10 seconds, after the squad moved its centre): 0 = no order, retail's path;
// 1 = *pos is the commanded goal (members without a target walk to it); 2 = leave the members where they are.
int		CeCommanderRelaxed(void *squad, FVector *pos);

// What the commander map (ce_cmdmap.cpp) asks of the order layer. The verbs are the ones measured to hold against the squads' own AI on the game's levels: Move and Fall
// back keep their goal and stay passive on the way, Advance fights on the way, Hold stays and fires. Every march the map gives ends in Hold: a squad that has
// arrived stays where it is and defends itself. Release gives the squad back to its own AI.
enum CeCmdVerb
{
	CE_CMD_NONE = 0,
	CE_CMD_MOVE,		// passive march: AI_OFF and a go-to-point; Hold once it has arrived
	CE_CMD_ADVANCE,		// the march that fights on the way: AI_ONF and a go-to-point; it holds its goal once there
	CE_CMD_HOLD,		// stay where the members are and fire: AI_ONF at the members' centre
	CE_CMD_FALLBACK,	// a passive march to the nearest own-side safe point; Hold once it has arrived
	CE_CMD_RELEASE		// forget the map's order: the squad's own AI state and goal from before its first order are put back
};

// Why a friendly squad takes no orders from the map (CE_WHY_NONE: it does).
enum CeCmdWhy
{
	CE_WHY_NONE = 0,
	CE_WHY_ENEMY,		// not on the player's side
	CE_WHY_SCRIPTED,	// a unit of it runs a mission script (or the squad itself does)
	CE_WHY_SPAWNED,		// a squad made around one placed unit or by a script's SpawnThing: its state follows its lead unit's every tick
	CE_WHY_AIRCRAFT,	// a member is an aircraft, or is not a ground unit the map can march (a boat, a turret, a building)
	CE_WHY_OBJECTIVE	// a member is marked as a mission objective
};

struct CeCmdSquad
{
	int ordinal;			// the order layer's number for the squad (the order in which it first saw it)
	bool vehicles;			// a vehicle squad; infantry otherwise
	bool friendly;			// on the player's side
	bool commandable;		// friendly, with live units, and why == CE_WHY_NONE
	int why;				// CeCmdWhy
	bool deploys;			// its units deploy before they fire (artillery): they take Hold only
	bool crossesWater;     // Every live member can march over water.
	char unit[28];			// the first live unit's type name
	bool visible;			// a friendly squad, or an enemy one with a unit the game reports as detectable (CUnit::OnScanner: most units; the HUD radar adds a range of about 220 world units, this does not)
	int members;			// live units
	int peak;				// the most live units the order layer has seen it with
	float x, y;				// the members' centre
	int verb;				// the standing order (CeCmdVerb), CE_CMD_NONE when it has none
	bool arrived;			// a march whose members' centre has come to the goal
	float gx, gy;			// the goal of a standing march
	int outcome;			// what it is doing under the map's order (CeCmdOutcome, ce_cmdoutcome.h): CE_OUT_NONE without one
	bool selected;			// the map's own mark (ce_cmdmap.cpp fills it): the squad is in the selection; the order layer leaves it false
	float life;				// summed live members' armour/health
	float maxLife;			// summed live members' maximum armour/health
};

// Whether the map may be offered here: the ce.ini key "commander" is on and none of the replay, recording, probe or network switches is set (a test build's
// proof switch is the one way past those).
bool	CeCmdMapOn();
// The current map owns input while open or opening; enabling its option does not reserve the pilot.
bool PortCeCmdMapOpen();
// Every squad with live units (up to max), after a fresh look at the world's squad list; returns how many. The order layer's table holds a fixed number of
// squads in a level: CeCmdUntracked says how many it had to leave out (they are neither listed nor given orders).
int		CeCmdList(CeCmdSquad *out, int max);
// Valid only for the single unshared, live squad of the exercise delivery.
int CeCmdDeliveredSquad();
int		CeCmdUntracked();

// Resolve a drawn member without keeping a live pointer in the battlefield picture.
class CSquad;
int CeCmdDrawnSquad(const CSquad *squad);
// One order for a squad. x and y are the goal of a march. False, with the reason in *why, when it is refused.
// Ground squads can march only to land within the battlefield.
bool	CeCmdMarchLand(float x, float y);
bool	CeCmdIssue(int ordinal, int verb, float x, float y, const char **why);

#ifdef CE_COMMANDER_TEST
// A hash of every thing's class, number, position and (for units) life, over all things and over all but the components (the parts a unit wears on its
// mounts, which follow their parents' animations as the picture is drawn), with the counts and the event manager's time: what the map's pause has to leave alone.
struct CeCmdHash { unsigned all, withoutComponents, things, components, eventTime; };
void	CeCmdWorldHash(CeCmdHash *out);
// Every thing's number, class, position and life, one line each, in a file (the frozen-state check's diagnostic).
void	CeCmdWorldDump(const char *path);
// A line in the probe (when there is one) naming what the player did on the map.
void	CeCmdNote(const char *fmt, ...);
// The order tape's `ui` verb (ce_cmdmap.cpp): what the player does on the open map, done with an exact pointer. word and its count numbers are the tape's;
// said receives one word for the probe's O line ("ok ..." or "refused: ...").
void	CeCmdTestUi(const char *word, const float *numbers, int count, char *said, unsigned room);
#else
inline void	CeCmdNote(const char *, ...) {}
#endif

class CUnit;
struct CeCmdUnitAuthority;
struct CeCmdUnitNativeRecord;
// Fresh native membership is resolved before any candidate pointer is read.
bool CeCmdUnitAuthorityNow(CUnit*, CeCmdUnitAuthority*);
// Pointer-only invalidation before native storage can be reused.
void CeCmdSquadGone(const void*);
int CeCmdUnitCollectNative(CeCmdUnitNativeRecord*, int capacity, bool* complete);
bool CeCmdUnitGroundPoint(float x, float y, float out[3]);

#endif

#endif
