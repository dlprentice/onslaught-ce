// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit (port/game/ce_playunit.cpp): the decisions that need no game object, so that a test can compile and run them on their own
// (port/tools/ce_playunit_tests.py). Nothing here reads the game, the environment or ce.ini: the module answers each question and these functions decide.
#ifndef PORT_CE_PLAYUNIT_RULES_H
#define PORT_CE_PLAYUNIT_RULES_H

#include <math.h>

// Whether the option may act in this session. The module fills each field from the game and the environment; the answer is "no" unless the player wanted it and none of
// the faithful modes is in force: the CE layer is on (it is off under a tape, probes, a recording and the virtual clocks), nothing records, none of the replay, test-pilot
// or network variables is set, no network session is starting, no demo or test run is going, and one player plays alone. A test build's proof switch lifts the fences that
// belong to replay and recording only: the option still refuses a network session, a demo and a second player.
struct CePlayUnitGate
{
	bool optionOn;		// ce.ini says play_unit=1 (or the older play_unit=tank)
	bool ceLayer;		// the CE layer is on
	bool recording;		// the game is recording a session
	bool fencedEnv;		// one of the replay, recording, probe, test-pilot or network variables is set
	bool networkBoot;	// a network session is starting or running
	bool demo;			// a demo, an automatic configuration test, a stress test or attract mode
	bool multiplayer;	// the level is a multiplayer level
	int players;		// the number of players
	bool proof;			// test builds only: the proof switch
};

inline bool CePlayUnitAllowed(const CePlayUnitGate &g)
{
	if (!g.optionOn) return false;
	if (g.networkBoot || g.demo || g.multiplayer || g.players != 1) return false;
	if (g.proof) return true;
	return g.ceLayer && !g.recording && !g.fencedEnv;
}

// What the game knows of a unit the option may take. The module reads each fact from the unit; "tank" is a ground vehicle of the game's ground-unit behaviour.
struct CePlayUnitFacts
{
	bool friendly;		// on the player's side
	bool alive;			// not dying, with life left
	bool vehicle;		// a ground vehicle (not infantry, a mech, a jeep, a turret, a building or an aircraft)
	bool groundUnit;	// the ground-unit behaviour (tanks, carriers of troops), not a jeep
	bool scripted;		// the unit or its squad runs a mission script
	bool objective;		// marked as a mission objective
	bool deploys;		// deploys before it fires (artillery)
	bool normalSquad;	// belongs to a normal (vehicle) squad, which the module can take it out of
	bool spawned;		// its squad was made around one placed unit or by a script: the squad follows its lead unit every tick
	bool armed;			// has a weapon
};

inline bool CePlayUnitTankEligible(const CePlayUnitFacts &f)
{
	return f.friendly && f.alive && f.vehicle && f.groundUnit && !f.scripted && !f.objective && !f.deploys && f.normalSquad && !f.spawned && f.armed;
}

// The player's wishes for a driven unit, reduced from the button actions of one tick: forward and back are 0 to 1, turn is -1 (left) to 1 (right).
struct CePlayUnitDrive
{
	float forward, back, turn;
	bool fire, leave;
};

inline float CePlayUnitClamp(float v, float lo, float hi)
{
	if (!(v == v)) return 0.f;		// not a number: no input
	return v < lo ? lo : v > hi ? hi : v;
}

// Angles are in radians from the x axis, counter-clockwise seen from above (x east, y north), as atan2(y, x) gives them; turning right is clockwise, so it lowers the angle.
inline float CePlayUnitWrap(float a)
{
	if (!isfinite(a)) return 0.f;
	const float pi = 3.14159265358979f;
	while (a > pi) a -= 2.f * pi;
	while (a <= -pi) a += 2.f * pi;
	return a;
}

// The heading the player asks for: the one asked for so far, turned by the steering (turn -1 left to 1 right) for dt seconds at rate radians a second, and never more than maxLead
// radians from the unit's actual heading, so that a unit that cannot turn does not wind the request up.
inline float CePlayUnitSteer(float desired, float actual, float turn, float dt, float rate, float maxLead)
{
	if (!isfinite(dt) || dt < 0.f) dt = 0.f;
	float lead = CePlayUnitWrap(desired - CePlayUnitClamp(turn, -1.f, 1.f) * rate * dt - actual);
	if (lead > maxLead) lead = maxLead;
	if (lead < -maxLead) lead = -maxLead;
	return CePlayUnitWrap(actual + lead);
}

// Where a unit is sent: reach units from (x, y) along the angle. False, with the unit's own place as the answer, when the numbers are not finite or the reach is not positive.
inline bool CePlayUnitPoint(float x, float y, float angle, float reach, float *tx, float *ty)
{
	*tx = x;
	*ty = y;
	if (!isfinite(x) || !isfinite(y) || !isfinite(angle) || !(reach > 0.f)) return false;
	*tx = x + cosf(angle) * reach;
	*ty = y + sinf(angle) * reach;
	return true;
}

// Whether a go-to-point is given again this tick: the player's wishes changed, or the interval since the last one is over, or the unit has come near the last point.
inline bool CePlayUnitReissue(bool changed, unsigned tick, unsigned lastTick, unsigned interval, float distanceToPoint, float nearRadius)
{
	return changed || tick - lastTick >= interval || distanceToPoint < nearRadius;
}

// A possession lease never overrides a replacement owner. Object identities and the current
// squad's members are resolved by the game adapter immediately before this decision.
struct CePlayUnitReturnFacts
{
    bool sameLevel;
    bool unitLive;
    bool sameTeam;
    bool currentSquad;
    bool aiStillOwned;
    bool originalSquadLive;
    bool originalSquadCompatible;
};

enum CePlayUnitReturnKind
{
    CE_PLAYUNIT_RETURN_GONE,
    CE_PLAYUNIT_RETURN_EXTERNAL,
    CE_PLAYUNIT_RETURN_SQUAD,
    CE_PLAYUNIT_RETURN_INDEPENDENT
};

inline CePlayUnitReturnKind CePlayUnitChooseReturn(const CePlayUnitReturnFacts &f)
{
    if (!f.sameLevel || !f.unitLive) return CE_PLAYUNIT_RETURN_GONE;
    if (!f.sameTeam || f.currentSquad || !f.aiStillOwned) return CE_PLAYUNIT_RETURN_EXTERNAL;
    if (f.originalSquadLive && f.originalSquadCompatible) return CE_PLAYUNIT_RETURN_SQUAD;
    return CE_PLAYUNIT_RETURN_INDEPENDENT;
}

#endif
