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
	bool vehicle;		// a ground vehicle (not infantry, a mech, a turret, a building or an aircraft)
	bool groundUnit;	// the ground-unit behaviour (tanks, carriers of troops), not a jeep
	bool scripted;		// the unit or its squad runs a mission script
	bool objective;		// marked as a mission objective
	bool deploys;		// deploys before it fires (artillery)
	bool normalSquad;	// belongs to a normal (vehicle) squad, which the module can take it out of
	bool spawned;		// its squad was made around one placed unit or by a script: the squad follows its lead unit every tick
	bool armed;			// has a weapon
	int family;
	bool ordinaryFoot;	// ordinary foot soldier (CInfantryUnit, independent or in a relaxed squad)
	bool turret;		// stationary gun emplacement or armed carrier/structure turret
	bool aircraft;		// friendly aircraft (fighter, bomber, dropship, carrier)
	bool naval;			// friendly naval vessel or landing craft
	bool transport;		// carries unspawned embarked cargo (landing craft, dropship)
};

// How a class of unit is driven: the control law (what a go-to-point looks like, how fast the asked heading turns) and what the held fire does. The module reads every number
// from here, so that the next class is one row and one control function, and the tank's numbers are the ones the first experiment measured.
struct CePlayUnitLaw
{
	float reach;			// how far ahead the point is, in world units
	float pivotReach;		// the point when the player only turns: near enough that the unit turns and stops
	float turnRate;			// radians a second the asked heading turns at full deflection
	float maxLead;			// radians the asked heading may lead the unit's own
	float nearRadius;		// a go-to-point is renewed when the unit is nearer to the last one than this
	float pivotNear;		// the same while pivoting
	unsigned reissue;		// ticks between go-to-points while driving
	unsigned pivotReissue;	// the same while pivoting
	bool freeFire;			// held fire aims at the ground point under the middle of the view (otherwise the stock handler's own target, as the first experiment did)
	float viewReach;		// how far the view's ray is followed to the ground, in world units
	float viewStep;			// the step of that search
	int lineSamples;		// the terrain samples that test the line of fire
	float lineTolerance;	// how far the terrain may rise into the line before it is blocked
	float muzzleLift;		// how far above the unit's place the line of fire starts
	float steerLead;		// the direct law (CePlayUnitTankMotionOf): how far the asked heading leads the unit's own at full steering, radians
	float reverseShare;		// the share of the top speed the unit backs up at
	float turnSlow;			// the share of the speed a full turn takes away while moving
};

// A class of unit the option can take. shape says what makes a unit one of the class; it never says anything of what every class refuses, which is CePlayUnitCommonEligible's:
// a row cannot loosen it.
struct CePlayUnitClassRow
{
	const char *name;
	bool enabled;
	bool (*shape)(const CePlayUnitFacts &f);
	CePlayUnitLaw law;
};

enum CePlayUnitClassId
{
	CE_PLAYUNIT_CLASS_TANK,
	CE_PLAYUNIT_CLASS_JEEP,		// off: no jeep or armed truck has been driven, so its law is the tank's, unmeasured
	CE_PLAYUNIT_CLASS_INFANTRY,
	CE_PLAYUNIT_CLASS_ARTILLERY,
	CE_PLAYUNIT_CLASS_TURRET,
	CE_PLAYUNIT_CLASS_AIRCRAFT,
	CE_PLAYUNIT_CLASS_NAVAL,
	CE_PLAYUNIT_CLASS_COUNT
};

inline bool CePlayUnitTankShape(const CePlayUnitFacts &f) { return f.vehicle && f.groundUnit && !f.deploys; }
inline bool CePlayUnitJeepShape(const CePlayUnitFacts &f) { return f.vehicle && !f.groundUnit && !f.deploys; }
inline bool CePlayUnitFootShape(const CePlayUnitFacts &f) { return f.ordinaryFoot && !f.vehicle; }
inline bool CePlayUnitArtilleryShape(const CePlayUnitFacts &f) { return f.vehicle && f.groundUnit && f.deploys; }
inline bool CePlayUnitTurretShape(const CePlayUnitFacts &f) { return f.turret && !f.vehicle && !f.aircraft && !f.naval; }
inline bool CePlayUnitAircraftShape(const CePlayUnitFacts &f) { return f.aircraft; }
inline bool CePlayUnitNavalShape(const CePlayUnitFacts &f) { return f.naval && !f.aircraft; }

inline const CePlayUnitClassRow *CePlayUnitClasses()
{
	static const CePlayUnitClassRow rows[CE_PLAYUNIT_CLASS_COUNT] = {
		{"tank", true, CePlayUnitTankShape, {40.f, 6.f, 1.4f, 1.2f, 8.f, 2.f, 6u, 2u, true, 400.f, 1.5f, 24, 0.6f, 1.f, 0.5f, 0.6f, 0.25f}},
		{"jeep", false, CePlayUnitJeepShape, {40.f, 6.f, 1.4f, 1.2f, 8.f, 2.f, 6u, 2u, false, 400.f, 1.5f, 24, 0.6f, 1.f, 0.5f, 0.6f, 0.25f}},
		{"infantry", true, CePlayUnitFootShape, {18.f, 5.f, 2.f, 0.5f, 3.f, 2.f, 12, 6, true, 480.f, 4.f, 24, 0.1f, 1.2f, 0.2f, 1.f, 0.f}},
		{"artillery", true, CePlayUnitArtilleryShape, {40.f, 6.f, 1.0f, 1.0f, 8.f, 2.f, 6u, 2u, true, 560.f, 2.f, 24, 2.5f, 2.2f, 0.4f, 0.5f, 0.3f}},
		{"turret", true, CePlayUnitTurretShape, {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 6u, 2u, true, 520.f, 2.f, 24, 0.8f, 2.5f, 0.f, 0.f, 0.f}},
		{"aircraft", true, CePlayUnitAircraftShape, {60.f, 12.f, 1.8f, 1.4f, 12.f, 4.f, 4u, 2u, true, 640.f, 3.f, 16, 4.0f, 0.f, 0.6f, 0.f, 0.15f}},
		{"naval", true, CePlayUnitNavalShape, {48.f, 8.f, 0.9f, 1.0f, 10.f, 3.f, 6u, 2u, true, 460.f, 2.f, 24, 1.0f, 2.0f, 0.35f, 0.45f, 0.2f}},
	};
	return rows;
}

inline bool CePlayUnitNeedsNormalSquad(const CePlayUnitFacts &f)
{
	return f.vehicle && !f.deploys && !f.ordinaryFoot && !f.turret && !f.aircraft && !f.naval;
}

// What no class loosens: a friendly, living, armed-or-transport unit that no script, objective or lead unit owns
// (and, for ordinary non-deploying vehicles, in a normal squad).
inline bool CePlayUnitCommonEligible(const CePlayUnitFacts &f)
{
	return f.friendly && f.alive && !f.scripted && !f.objective && (!CePlayUnitNeedsNormalSquad(f) || f.normalSquad) && !f.spawned && (f.armed || f.transport);
}

// Whether a unit may be taken as the class of row rows[row] (n rows): the row is on, the unit is what every class needs and has the row's shape. The table is a parameter so that a
// test can add a row of its own and see that every class still refuses the same units.
inline bool CePlayUnitEligibleIn(const CePlayUnitClassRow *rows, int n, int row, const CePlayUnitFacts &f)
{
	if (!rows || row < 0 || row >= n || !rows[row].enabled || !rows[row].shape) return false;
	return CePlayUnitCommonEligible(f) && rows[row].shape(f);
}

// The first enabled row the unit fits, or -1.
inline int CePlayUnitClassIn(const CePlayUnitClassRow *rows, int n, const CePlayUnitFacts &f)
{
	for (int i = 0; i < n; ++i)
		if (CePlayUnitEligibleIn(rows, n, i, f)) return i;
	return -1;
}

inline int CePlayUnitClassFor(const CePlayUnitFacts &f) { return CePlayUnitClassIn(CePlayUnitClasses(), CE_PLAYUNIT_CLASS_COUNT, f); }

inline bool CePlayUnitTankEligible(const CePlayUnitFacts &f) { return CePlayUnitEligibleIn(CePlayUnitClasses(), CE_PLAYUNIT_CLASS_COUNT, CE_PLAYUNIT_CLASS_TANK, f); }

// The player's wishes for a driven unit, reduced from the button actions of one tick: forward and back are 0 to 1, turn is -1 (left) to 1 (right).
struct CePlayUnitDrive
{
	float forward, back, turn;
	bool fire, leave;
	float look;			// the yaw actions while the look is the player's (a stick's deflection, -1 left to 1 right)
	float pitch;		// the pitch actions while the look is the player's (a stick's deflection, -1 up to 1 down, the player's invert setting applied)
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

// The direct law (the drive guide in ce_playunit.cpp writes its answer into the unit each update in place of the stock guide's path following): the heading the unit
// turns toward, in the game's yaw (turning right lowers it), and the share of its top speed it drives at, forward positive. W drives forward and S backs up at
// reverseShare; both together, or neither, hold still. A and D turn the hull at the unit's own turn rate, while it moves as well as standing still: the heading asked
// leads the unit's own by steerLead at full steering, and a full turn while moving takes turnSlow of the speed away. Inputs that are not numbers are none.
struct CePlayUnitTankMotion
{
	float yaw;			// the heading to turn toward
	float share;		// -reverseShare to 1
};

inline CePlayUnitTankMotion CePlayUnitTankMotionOf(float yaw, float forward, float back, float turn, float steerLead, float reverseShare, float turnSlow)
{
	CePlayUnitTankMotion m;
	const float f = CePlayUnitClamp(forward, 0.f, 1.f), b = CePlayUnitClamp(back, 0.f, 1.f), s = CePlayUnitClamp(turn, -1.f, 1.f);
	float share = f - b * CePlayUnitClamp(reverseShare, 0.f, 1.f);
	if (f > 0.f && b > 0.f) share = 0.f;
	share *= 1.f - CePlayUnitClamp(turnSlow, 0.f, 1.f) * fabsf(s);
	m.yaw = CePlayUnitWrap((isfinite(yaw) ? yaw : 0.f) - s * CePlayUnitClamp(steerLead, 0.f, 1.5f));
	m.share = share;
	return m;
}

// How a driven tank eases into and out of what the keys ask, a tick at a time. Speed is along the hull's facing (world units a tick, forward positive) and turn is
// the hull's yaw change (radians a tick, left positive). Speed rises toward a larger ask by speedRise of the gap a tick (the Battle Engine walker's own start:
// most of its speed in two ticks, all of it in three) and falls toward a smaller one, or through zero into reverse, by speedFall (the walker's glide when W is let
// go: 0.7 of the speed kept a tick). The turn rate eases in by turnRise and out by turnFall of the gap, as the game's own tanks settle on a heading. A gap smaller
// than settle is closed, so the tank stands still and stops turning exactly.
struct CePlayUnitTankEase
{
	float speedRise, speedFall, turnRise, turnFall, settle;
};

inline const CePlayUnitTankEase &CePlayUnitTankEaseLaw()
{
	static const CePlayUnitTankEase law = {0.6f, 0.3f, 0.5f, 0.45f, 1e-4f};
	return law;
}

inline float CePlayUnitEaseToward(float now, float ask, float rise, float fall, float settle)
{
	if (!isfinite(now)) now = 0.f;
	if (!isfinite(ask)) ask = 0.f;
	const bool gaining = (ask > 0.f && now >= 0.f && ask > now) || (ask < 0.f && now <= 0.f && ask < now);
	float next = now + (ask - now) * CePlayUnitClamp(gaining ? rise : fall, 0.f, 1.f);
	if (fabsf(ask - next) < settle) next = ask;
	return next;
}

// The request the ground mover needs for the unit's velocity to be want after its next move. The mover (CGroundUnit::Move), on the ground, does steps times
// v = (v + 0.4 * request) * friction, then holds v to the top speed; solved for the request, one component at a time. false when friction leaves no solution
// (the caller keeps the stock request then).
inline bool CePlayUnitMoverRequest(float v, float want, float friction, int steps, float &request)
{
	if (!(friction > 0.f && friction < 1.f) || steps < 1 || !isfinite(v) || !isfinite(want)) return false;
	float fn = 1.f;
	for (int i = 0; i < steps; ++i) fn *= friction;
	request = (want - v * fn) * (1.f - friction) / (0.4f * friction * (1.f - fn));
	return isfinite(request);
}

// The speed the mover settles at under a constant request (the stock guide's, four times the top speed a tick, gives the class's own cruising speed), held to
// the top speed; the top speed itself when friction leaves no steady state.
inline float CePlayUnitMoverCruise(float request, float friction, float top)
{
	if (!(friction > 0.f && friction < 1.f)) return top;
	const float steady = 0.4f * request * friction / (1.f - friction);
	return steady < top ? steady : top;
}

// The direct law has no path finder: like the stock guide it does not drive from a cell its grid lets it stand on into one the grid does not (water, a cliff, a
// building's footprint), in the way it is going (ahead forward, behind in reverse). Turning on the spot is never stopped.
inline float CePlayUnitTankGuard(float share, bool hereOpen, bool aheadOpen, bool behindOpen)
{
	if (!hereOpen) return share;
	if (share > 0.f && !aheadOpen) return 0.f;
	if (share < 0.f && !behindOpen) return 0.f;
	return share;
}

// A notice put up while one is shown keeps the capsule where its opening is: one that is open stays open, one opening goes on opening, one closing opens again from
// the width it had. A key pressed again and again then never shows an empty capsule starting over. The age the new notice starts at (0 with none shown).
inline unsigned long long CePlayUnitNoticeAgeOnReplace(unsigned long long age, unsigned long long shown, unsigned long long opening)
{
	if (age >= shown) return 0;
	const unsigned long long left = shown - age;
	const unsigned long long phase = age < left ? age : left;
	return phase > opening ? opening : phase;
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

// ---- free fire --------------------------------------------------------------------------------------------------------------------------------------------------
// Held fire at the ground point under the middle of the view, with no enemy needed. The game's z axis points down, so a point is above the ground when its z is lower than the
// ground's. Points are three floats (x, y, z).

// Whether free fire may act: the class says so, the player's wish is allowed (the option's own gate, and not under a test build's proof switch, which lifts the replay fences, so that
// nothing here ever runs under a tape, a recording, probes or a virtual clock), and the Commander view does not own the controls.
inline bool CePlayUnitFreeFireAllowed(const CePlayUnitGate &g, bool classFreeFire, bool commanderOpen)
{
	if (!classFreeFire || commanderOpen || g.proof) return false;
	return CePlayUnitAllowed(g);
}

// The ground's height (z) at x, y, given by the module from the map.
typedef float (*CePlayUnitHeightFn)(void *context, float x, float y);

inline bool CePlayUnitFinite3(const float *v) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }

// Where the ray from eye along look (a unit vector) first reaches the ground: searched in steps of step up to maxDist, then narrowed by halving. False when the eye is under the
// ground, the ray stays above it as far as it is followed (the sky, or ground beyond the reach), or a number is not finite. hit[2] is the ground's height at hit.
inline bool CePlayUnitViewGround(const float *eye, const float *look, float maxDist, float step, CePlayUnitHeightFn height, void *context, float *hit)
{
	if (!height || !eye || !look || !hit || !CePlayUnitFinite3(eye) || !CePlayUnitFinite3(look) || !(maxDist > 0.f) || !(step > 0.f)) return false;
	const float start = height(context, eye[0], eye[1]) - eye[2];
	if (!isfinite(start) || start <= 0.f) return false;
	float before = 0.f;
	for (float t = step;; t += step)
	{
		if (t > maxDist) t = maxDist;
		const float above = height(context, eye[0] + look[0] * t, eye[1] + look[1] * t) - (eye[2] + look[2] * t);
		if (!isfinite(above)) return false;
		if (above <= 0.f)
		{
			float lo = before, hi = t;
			for (int i = 0; i < 12; ++i)
			{
				const float mid = 0.5f * (lo + hi);
				if (height(context, eye[0] + look[0] * mid, eye[1] + look[1] * mid) - (eye[2] + look[2] * mid) > 0.f) lo = mid; else hi = mid;
			}
			hit[0] = eye[0] + look[0] * hi;
			hit[1] = eye[1] + look[1] * hi;
			hit[2] = height(context, hit[0], hit[1]);
			return isfinite(hit[2]);
		}
		if (t >= maxDist) return false;
		before = t;
	}
}

enum CePlayUnitRangeKind
{
	CE_PLAYUNIT_RANGE_OK,
	CE_PLAYUNIT_RANGE_TOO_FAR,
	CE_PLAYUNIT_RANGE_TOO_NEAR,
	CE_PLAYUNIT_RANGE_NONE		// no range to judge by: the weapon's range is not a positive number, the point is the unit's own place, or a number is not finite
};

// Whether the weapon reaches the point: from is where it shoots, minRange and maxRange the weapon's own. Beyond them, clamped is the point on the same line at the range that was
// passed, which is where the gun is turned; in range it is the point itself.
inline int CePlayUnitRange(const float *from, const float *to, float minRange, float maxRange, float *clamped)
{
	if (!from || !to || !clamped || !CePlayUnitFinite3(from) || !CePlayUnitFinite3(to) || !isfinite(minRange) || !isfinite(maxRange) || !(maxRange > 0.f) || minRange > maxRange)
		return CE_PLAYUNIT_RANGE_NONE;
	const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
	const float d = sqrtf(dx * dx + dy * dy + dz * dz);
	clamped[0] = to[0]; clamped[1] = to[1]; clamped[2] = to[2];
	if (!(d > 0.f)) return CE_PLAYUNIT_RANGE_NONE;
	if (d > maxRange)
	{
		const float k = maxRange / d;
		clamped[0] = from[0] + dx * k; clamped[1] = from[1] + dy * k; clamped[2] = from[2] + dz * k;
		return CE_PLAYUNIT_RANGE_TOO_FAR;
	}
	if (d < minRange)
	{
		const float k = minRange / d;
		clamped[0] = from[0] + dx * k; clamped[1] = from[1] + dy * k; clamped[2] = from[2] + dz * k;
		return CE_PLAYUNIT_RANGE_TOO_NEAR;
	}
	return CE_PLAYUNIT_RANGE_OK;
}

// Whether the ground leaves the line from to open: at samples points between them (neither end) the ground may not rise more than tolerance into the line. This is the terrain
// alone; a unit or a building in the way is not tested here (the shell meets it).
inline bool CePlayUnitLineClear(const float *from, const float *to, int samples, float tolerance, CePlayUnitHeightFn height, void *context)
{
	if (!height || !from || !to || !CePlayUnitFinite3(from) || !CePlayUnitFinite3(to) || samples < 0 || !(tolerance >= 0.f)) return false;
	for (int i = 1; i <= samples; ++i)
	{
		const float t = float(i) / float(samples + 1);
		const float x = from[0] + (to[0] - from[0]) * t, y = from[1] + (to[1] - from[1]) * t, z = from[2] + (to[2] - from[2]) * t;
		const float ground = height(context, x, y);
		if (!isfinite(ground) || ground + tolerance < z) return false;
	}
	return true;
}

// What the reticle says.
enum CePlayUnitFireState
{
	CE_PLAYUNIT_FIRE_NONE,			// nothing to say: no weapon
	CE_PLAYUNIT_FIRE_READY,
	CE_PLAYUNIT_FIRE_ARMING,
	CE_PLAYUNIT_FIRE_RECOVERING,
	CE_PLAYUNIT_FIRE_OUT_OF_RANGE,	// no ground under the view, or it is nearer or farther than the weapon reaches
	CE_PLAYUNIT_FIRE_BLOCKED		// the ground rises into the line of fire
};

// haveGround: the view's ray has a point (the ground it reaches, or a point along it where it reaches none). range: what CePlayUnitRange said of that point: beyond the
// weapon's reach the shot goes as far as the weapon reaches on the same line, so only a point inside its minimum, or no range at all, is out of range. weaponReady: the weapon is not between shots. armed: the unit stands ready to
// discharge (the game's own second phase). arming: the unit has begun the arming that the game's first phase asks for.
inline int CePlayUnitFireStateOf(bool haveWeapon, bool haveGround, int range, bool lineClear, bool weaponReady, bool armed, bool arming)
{
	if (!haveWeapon) return CE_PLAYUNIT_FIRE_NONE;
	if (!haveGround || range == CE_PLAYUNIT_RANGE_NONE || range == CE_PLAYUNIT_RANGE_TOO_NEAR) return CE_PLAYUNIT_FIRE_OUT_OF_RANGE;
	if (!lineClear) return CE_PLAYUNIT_FIRE_BLOCKED;
	if (arming && !armed) return CE_PLAYUNIT_FIRE_ARMING;
	if (!weaponReady) return CE_PLAYUNIT_FIRE_RECOVERING;
	return CE_PLAYUNIT_FIRE_READY;
}

// Whether the held fire gives the unit's own arm and fire calls this tick: only while the player holds fire, and only where there is something native to ask. Out of range or
// blocked, holding fire only turns the gun.
inline bool CePlayUnitFireActs(bool held, int state)
{
	return held && (state == CE_PLAYUNIT_FIRE_READY || state == CE_PLAYUNIT_FIRE_ARMING || state == CE_PLAYUNIT_FIRE_RECOVERING);
}

// Whether the gun is turned toward the point (the clamped one when it is out of range): while fire is held and there is a weapon.
inline bool CePlayUnitFireAims(bool held, int state)
{
	return held && state != CE_PLAYUNIT_FIRE_NONE;
}

// ---- taking a unit by key ----------------------------------------------------------------------------------------------------------------
// The player looks at one of the units of their side and presses the take key: the unit nearest the middle of the view, within reach, is taken if a class
// that is on accepts it; otherwise the player is told in one sentence why not.

// What kind of thing a unit is, from the game's thing-type bits (the module reads them): only the wording of a refusal depends on it.
enum CePlayUnitFamily
{
	CE_PLAYUNIT_FAMILY_OTHER,			// a building, a mine, a pod or anything else that is no vehicle, soldier, aircraft, boat or walker
	CE_PLAYUNIT_FAMILY_GROUND_VEHICLE,
	CE_PLAYUNIT_FAMILY_INFANTRY,
	CE_PLAYUNIT_FAMILY_AIRCRAFT,
	CE_PLAYUNIT_FAMILY_NAVAL,
	CE_PLAYUNIT_FAMILY_WALKER,
	CE_PLAYUNIT_FAMILY_EMPLACEMENT,
	CE_PLAYUNIT_FAMILY_COUNT
};

// Why a unit may not be taken (CE_PLAYUNIT_TAKE_OK: it may). The order is the order of the questions: the first that applies is the one the player is told.
enum CePlayUnitRefusal
{
	CE_PLAYUNIT_TAKE_OK,
	CE_PLAYUNIT_REFUSE_ENEMY,			// not on the player's side
	CE_PLAYUNIT_REFUSE_DYING,
	CE_PLAYUNIT_REFUSE_MISSION,			// a mission script runs it, or it is an objective
	CE_PLAYUNIT_REFUSE_DEPLOYS,			// artillery
	CE_PLAYUNIT_REFUSE_KIND,			// no class that is on has its shape (a soldier, an aircraft, a boat...)
	CE_PLAYUNIT_REFUSE_SQUAD,			// not in a squad it can leave, or its squad follows it
	CE_PLAYUNIT_REFUSE_UNARMED,
	CE_PLAYUNIT_REFUSE_COUNT
};

// The reason, for a table of classes (CePlayUnitClassIn's): CE_PLAYUNIT_TAKE_OK exactly when CePlayUnitClassIn finds a row. The kind is asked before the squad, because
// a soldier has no squad a vehicle has and the true reason is what it is.
inline int CePlayUnitRefusalIn(const CePlayUnitClassRow *rows, int n, const CePlayUnitFacts &f)
{
	if (CePlayUnitClassIn(rows, n, f) >= 0) return CE_PLAYUNIT_TAKE_OK;
	if (!f.friendly) return CE_PLAYUNIT_REFUSE_ENEMY;
	if (!f.alive) return CE_PLAYUNIT_REFUSE_DYING;
	if (f.scripted || f.objective) return CE_PLAYUNIT_REFUSE_MISSION;
	bool shaped = false;
	for (int i = 0; rows && i < n; ++i)
		if (rows[i].enabled && rows[i].shape && rows[i].shape(f)) shaped = true;
	if (!shaped) return f.deploys ? CE_PLAYUNIT_REFUSE_DEPLOYS : CE_PLAYUNIT_REFUSE_KIND;
	if ((CePlayUnitNeedsNormalSquad(f) && !f.normalSquad) || f.spawned) return CE_PLAYUNIT_REFUSE_SQUAD;
	if (!f.armed && !f.transport) return CE_PLAYUNIT_REFUSE_UNARMED;
	return CE_PLAYUNIT_REFUSE_KIND;
}

inline int CePlayUnitRefusalFor(const CePlayUnitFacts &f) { return CePlayUnitRefusalIn(CePlayUnitClasses(), CE_PLAYUNIT_CLASS_COUNT, f); }

// A short name of each reason, for the probe and the census (never shown to the player).
inline const char *CePlayUnitRefusalName(int r)
{
	static const char *const names[CE_PLAYUNIT_REFUSE_COUNT] = {"ok", "enemy", "dying", "mission", "deploys", "kind", "squad", "unarmed"};
	return r >= 0 && r < CE_PLAYUNIT_REFUSE_COUNT ? names[r] : "?";
}

// The sentence the player is shown for a refused unit: what happened, in the game's own plain words (no file, key-code or internal names).
inline const char *CePlayUnitRefusalSentence(int r, int family)
{
	switch (r)
	{
	case CE_PLAYUNIT_REFUSE_ENEMY: return "That unit is not on your side.";
	case CE_PLAYUNIT_REFUSE_DYING: return "That unit is going down.";
	case CE_PLAYUNIT_REFUSE_MISSION: return "The mission needs that unit.";
	case CE_PLAYUNIT_REFUSE_DEPLOYS: return "Artillery cannot be taken.";
	case CE_PLAYUNIT_REFUSE_SQUAD: return "That unit cannot leave its squad.";
	case CE_PLAYUNIT_REFUSE_UNARMED: return "That unit has no weapon.";
	case CE_PLAYUNIT_REFUSE_KIND:
		switch (family)
		{
		case CE_PLAYUNIT_FAMILY_INFANTRY: return "This soldier cannot be taken.";
		case CE_PLAYUNIT_FAMILY_AIRCRAFT: return "Aircraft cannot be taken.";
		case CE_PLAYUNIT_FAMILY_NAVAL: return "Boats cannot be taken.";
		case CE_PLAYUNIT_FAMILY_WALKER: return "Walkers cannot be taken.";
		case CE_PLAYUNIT_FAMILY_EMPLACEMENT: return "Gun turrets cannot be taken.";
		case CE_PLAYUNIT_FAMILY_GROUND_VEHICLE: return "That vehicle cannot be taken.";
		default: return "That unit cannot be taken.";
		}
	default: return "That unit cannot be taken.";
	}
}

// Where a unit is on the screen the player saw: its place in pixels of the frame (x right, y down, as the HUD's own projection gives it, CRenderInfo::Project with the
// matrices the engine kept from that frame), the radius of its body there in pixels, its depth in front of the eye (world units along the view; nothing at or behind the
// near limit is drawn) and its distance from the eye.
struct CePlayUnitScreenPlace
{
	float x, y, radius, depth, distance;
};

// The near limit of the sight test (world units in front of the eye): nothing nearer than the camera's own near plane is drawn.
const float kCePlayUnitSightNear = 1.f;

// Whether a unit is in the picture of a w x h frame: in front of the eye, within reach and inside the frame. When it is, *score is how far the edge of its body is from the
// middle of the screen, where the reticle is, in half-widths of the 4:3 frame the HUD is laid out on (the smaller of w and h * 4/3), and 0 when the body covers the
// middle. A unit that is not in the picture can never be taken, however near the eye it is.
inline bool CePlayUnitAtScreen(const CePlayUnitScreenPlace &p, float w, float h, float reach, float *score)
{
	if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.radius) || !isfinite(p.depth) || !isfinite(p.distance)) return false;
	if (!(w > 0.f) || !(h > 0.f) || !(reach >= 1.f)) return false;
	if (!(p.depth > kCePlayUnitSightNear) || p.distance > reach) return false;
	if (p.x < 0.f || p.x > w || p.y < 0.f || p.y > h) return false;
	const float half = (w < h * 4.f / 3.f ? w : h * 4.f / 3.f) * 0.5f;
	const float dx = p.x - w * 0.5f, dy = p.y - h * 0.5f;
	const float r = (sqrtf(dx * dx + dy * dy) - (p.radius > 0.f ? p.radius : 0.f)) / half;
	*score = r > 0.f ? r : 0.f;
	return true;
}

// One unit the view could mean: how far it is from the reticle (CePlayUnitAtScreen's score, -1 when it is not in the picture or not at the reticle) and its refusal
// (CE_PLAYUNIT_TAKE_OK if it may be taken).
struct CePlayUnitSighted
{
	float angle;
	int refusal;
};

// Which of n sighted units the take key means: the one nearest the middle that may be taken, and -1 when none may; then *why is the refusal of the unit nearest the middle
// (so the player hears about the unit they looked at), or CE_PLAYUNIT_REFUSE_COUNT when no unit was in sight at all. A unit that may be taken always wins over a nearer one
// that may not: looking at a squad takes the tank in it, not the soldier in front of it.
inline int CePlayUnitChoose(const CePlayUnitSighted *s, int n, int *why)
{
	int best = -1, nearest = -1;
	for (int i = 0; s && i < n; ++i)
	{
		if (!(s[i].angle >= 0.f)) continue;
		if (nearest < 0 || s[i].angle < s[nearest].angle) nearest = i;
		if (s[i].refusal == CE_PLAYUNIT_TAKE_OK && (best < 0 || s[i].angle < s[best].angle)) best = i;
	}
	*why = best >= 0 ? CE_PLAYUNIT_TAKE_OK : nearest >= 0 ? s[nearest].refusal : CE_PLAYUNIT_REFUSE_COUNT;
	return best;
}

// One tick of a switch from one unit to the next (the old one is let go; the new one is taken as soon as the Battle Engine's own view is back). *wait is the ticks the
// switch may still wait, 0 when there is none. found: the unit is still in the world; ready: the controls may be taken now; takeable: a class still takes the unit.
enum CePlayUnitSwitchStepResult
{
	CE_PLAYUNIT_SWITCH_IDLE,			// no switch
	CE_PLAYUNIT_SWITCH_TAKE,			// take the unit now
	CE_PLAYUNIT_SWITCH_WAIT,			// not yet: ask again next tick
	CE_PLAYUNIT_SWITCH_GIVE_UP			// the unit is gone, no longer takeable, or the wait is over: say so
};

inline int CePlayUnitSwitchStep(unsigned *wait, bool found, bool ready, bool takeable)
{
	if (!*wait) return CE_PLAYUNIT_SWITCH_IDLE;
	--*wait;
	if (found && ready && takeable) { *wait = 0; return CE_PLAYUNIT_SWITCH_TAKE; }
	if (!found || (ready && !takeable) || !*wait) { *wait = 0; return CE_PLAYUNIT_SWITCH_GIVE_UP; }
	return CE_PLAYUNIT_SWITCH_WAIT;
}

// The take key as the frames see it: true once for each press (the frame it goes down), never for a key held across frames, and never while the press may not count (a menu,
// the pause menu, a console or a map holds the keys): a key that went down then is not taken when that ends, until it is let go and pressed again.
struct CePlayUnitKey
{
	bool wasDown;
};

inline bool CePlayUnitPressed(CePlayUnitKey *k, bool down, bool mayCount)
{
	const bool edge = down && !k->wasDown;
	k->wasDown = down;
	return edge && mayCount;
}

// Where the take key's search reaches and how wide it looks (world units and radians), and how long a press waits for the Battle Engine's own view to come back after a
// switch from one unit to the next (ticks).
const float kCePlayUnitSightReach = 320.f;
// How far from the middle of the screen a unit may be and still be the one the reticle means (half-widths of the 4:3 frame: 0.12 is 38 pixels of a 640x480 screen, a
// little outside the ring at the middle of the Battle Engine's HUD).
const float kCePlayUnitSightReticle = 0.12f;
// How much of a unit's bounding radius counts as its body on the screen (the bounding sphere holds the whole model, turret and barrel included).
const float kCePlayUnitSightBody = 0.5f;
const unsigned kCePlayUnitSwitchWait = 10;
// The sight line to a unit: how far above its place it aims (world units, up is negative z), the terrain samples along it and how far the ground may rise into it.
const float kCePlayUnitSightLift = 1.5f;
const int kCePlayUnitSightSamples = 32;
const float kCePlayUnitSightTolerance = 0.5f;

#endif
