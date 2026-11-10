// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit, first experiment: a developer option that hands the local player's controls to a friendly ground tank.
//
// The original wires the player to the Battle Engine in one place: CPlayer::ReceiveButtonAction turns every BUTTON_MECH_* action into a call on the engine. Everything above
// it is generic: the controller keeps a stack of IController objects, sends each button action to the one on top (CController::SendButtonAction), and CGame pushes and pops
// that stack for the pause menu, the free camera and the commander map. This module takes the same road: a small IController (the driver) goes on the stack with
// CController::SetToControl, receives the player's actions, and drives the tank through what every other unit is driven through, an AI state and a guide:
//
// - the tank's own AI is switched off (AI_OFF) and the tank leaves its squad (the squad would send it back to its place and its own AI would pick targets and steer; a unit with
//   no squad and its AI off keeps the destination it is given, as commander's Move order relies on);
// - forward sends it to a point ahead of it along its heading, turned by the steering, with the guide's go-to-point (CUnit::GoToPoint), again every few ticks while the player
//   keeps driving; releasing the key sends it to where it stands;
// - held fire calls the stock ground vehicle AI's targeting/aim/arm/fire handler; its movement AI stays off and the driver retains the guide orders;
// - the Battle Engine stays where it is, powered down (CBattleEngine::Deactivate) so it takes no input; it is still a target;
// - the view uses CThing3rdPersonCamera on the tank, owned by the game's camera slot; the player's preferred control view is recreated when the tank is let go. The slot's camera must be the game's: a camera substituted only where the viewpoint
//   is set (CEngine::SetViewpoint) draws the sky and the distant islands but none of the terrain or units near it (PARITY.md, "Play as a unit"). The game's camera is an input
//   of the simulation (units near it think more often), so the battle runs a little differently while a tank is driven than without. The frame is drawn from another camera, made
//   in the render hook (PortCePlayUnitRenderCamera) from the game's: carried to the unit's displayed pose and hung from the unit's displayed position with its tilt and offset
//   settled over about a fifth of a second (ce_playunit_view.h), which spreads what uneven ground does to the game's camera in each update. The game's own camera is not changed;
// - everything the driver does not use goes on to the player: pause, help, skip, the console and the menus work as before.
// Hand-back rejoins the same surviving compatible squad through its native membership operation and adopts its current AI state. A newer side, squad or AI owner wins; a missing squad gets an independent hold with the prior AI state.
// Morph hands the controls back. Enabling the free camera also leaves possession (use F again, with its cheat enabled, for ordinary free camera). Death, an external view, and level end relinquish control too.
// A cutscene or remote view may borrow the tank camera; cleanup waits for that view to return or destroy it. New possession is refused meanwhile.
//
// It is part of every CE build and off unless ce.ini says play_unit=1 under [Experimental], or the older play_unit=tank (CePlayUnitAllowed, ce_playunit_rules.h): never under a replay, a recording, probes, a virtual
// clock, a test pilot or a network game (the variables below), in a demo or a multiplayer level, with commander enabled, or with a second player. The opted-in firing handler
// can use the game's RNG. The module writes no career/options and directly allocates only a game-owned camera: its driver is built once in static memory and never destroyed, so the
// controller stack can hold it until the process ends.
//
// A test build (-DCE_PLAYUNIT_TEST, build.py --play-unit-test) adds the proof switch (BEA_PORT_PLAYUNIT_PROOF=1 lets the option run under the virtual clock, a tape, probes or a
// recording, as photo mode's and commander's proof switches do), a probe file (BEA_PORT_PLAYUNIT_PROBE=PATH, one line a tick, also when the option is off, so that a run that
// drives can be set beside one that does not), god mode for the Battle Engine (BEA_PORT_PLAYUNIT_GOD=1) and a control (BEA_PORT_PLAYUNIT_KEEPAI=1 leaves the tank's AI and squad
// as they are, to show what drives it then) BEA_PORT_PLAYUNIT_KILL=N, which takes the driven unit's life and starts its death at tick N, and BEA_PORT_PLAYUNIT_FIRE=A,B, which has the driver fire from tick A up to tick B. The probe's lines:
//   # ...                                                                                  notes: the level, the units found, why a take-over did not happen
//   U thing class name behaviour types friendly squad-class scripted spawned objective deploys armed x y z   a friendly unit the option looked at (once, at the first look)
//   P tick present thing x y z fx fy life ai-state in-squad guide-x guide-y moving forward turn fire own-rounds reasserts engine-x engine-y engine-up engine-state engine-grounded rounds
//        the unit the option drives (or would drive: mode O) and the Battle Engine, every tick; mode D when the controls are the tank's, O when the option only observes
//   A tick button value                                                                    a button action the driver received
//   E tick what                                                                            an event: start, release and why, a squad's detach
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "squad.h"
#include "unit.h"
#include "map.h"
#include "thing.h"
#include "player.h"
#include "BattleEngine.h"
#include "Controller.h"
#include "Guide.h"
#include "Camera.h"
#include "frontend.h"
#include "CLIParams.h"
#include "WorldPhysicsManager.h"
#include "eventmanager.h"
#include "ce_playunit.h"
#include "ce_playunit_rules.h"
#include "ce_playunit_view.h"
#include "ce_commander.h"
#include "../platform/common/portlog.h"
#include "../platform/common/feel.h"

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CE_PLAYUNIT_TEST
// The probe is the test build's own file, written where the caller asks: the C library's fopen, not the game's (port.h sends game files' fopen through the path layer, which
// refuses absolute paths).
#undef fopen
#endif

extern "C" int CE_SettingIs(const char *key, const char *value);		// ce.cpp: the ce.ini line
bool PortCeExperimental(const char *mode);	// ce.cpp: the ce.ini [Experimental] lines, each read once
extern "C" int CE_BootRequested(void) __attribute__((weak));			// the network boot boundary (absent from a build without the network)
extern "C" unsigned PortPresentCount(void);								// d3d9hooks.cpp
bool PortClockVirtual();												// clock.cpp: a virtual clock (a replay, a recording, probes, or a test build's proof run)
unsigned long long PortClockRawNs();									// clock.cpp: the operating system's monotonic clock in nanoseconds
bool PortCeLayer();
bool PortRecording();

namespace
{
const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};

const unsigned kFirstLook = 5;			// ticks of play before the first look for a tank (the level has settled)
const unsigned kLookEvery = 30;			// ticks between looks while there is none
const unsigned kMaxLooks = 20;			// looks before the option gives up for the level
const float kReach = 40.f;				// how far ahead the point is, in world units
const float kPivotReach = 6.f;			// the point when the player only turns: near enough that the unit turns and stops
const float kTurnRate = 1.4f;			// radians a second the asked heading turns at full deflection
const float kMaxLead = 1.2f;			// radians the asked heading may lead the unit's own
const unsigned kReissue = 6;			// ticks between go-to-points while driving
const float kNear = 8.f;				// a go-to-point is renewed when the unit is nearer to the last one than this

bool gConfigured, gWanted, gProof;
bool gDriving;							// the controls are the tank's
bool gControllerPending;			// a menu may temporarily sit above the inactive driver
int gLevel = -1;
unsigned gTick;							// updates of this level in which it was playing and not paused
unsigned gLooks, gNextLook;
bool gDone;								// the option has done what it does for this level (took a unit, gave up, or the unit is gone)

CUnit *gUnit;							// the driven (or observed) unit: compared by address with the world's things, never followed until found there
int gThing = -1;
EAIState gPrevAI = AI_ON;
CSquad *gPreviousSquad;
int gPreviousSquadNumber = -1;
int gPossessedAllegiance;
CBattleEngine *gParkedEngine;
int gParkedEngineNumber = -1;
bool gEngineDeactivated;
class CeTankCamera;
CeTankCamera *gCamera;			// the game's own third-person camera on the unit, while the game's camera is the unit's

// How the displayed camera is made (BEA_PORT_TANK_VIEW, read once with the ini): "smooth", the default, hangs it from the unit's displayed position and settles its tilt and
// offset (ce_playunit_view.h); "rigid" fixes it to the unit's displayed pose, as the first version of the render hook did; "stock" shows the game's own camera. All three are
// on one build so that they can be compared; none of them changes what the game computes, only the camera the frame is drawn from.
enum { kViewSmooth, kViewRigid, kViewStock };
int gViewMode = kViewSmooth;
CeUnitViewSmoother gSmoother;			// where the displayed camera's tilt and offset are, settling toward the game's camera
unsigned gShownFrame = ~0u;				// the Present the displayed camera was made for: a frame's other views (a reflection) take the one the first view used
bool gShownMade;
FVector gShownPos;
FMatrix gShownOri;
unsigned long long gShownNs;			// the clock at that frame
void ViewReset();

CePlayUnitDrive gIn;					// the actions of the last flush, reduced
CePlayUnitDrive gApplied;				// the ones the last tick applied (the probe prints them)
bool gMoving;
float gDesired, gLastAngle;				// the heading asked for, and the one the last go-to-point was made for (radians)
float gLastTime;						// the event manager's time at the last tick
bool gHaveTime;
FVector gLastPoint;
unsigned gLastIssue;
unsigned gFired, gReasserts;

#ifdef CE_PLAYUNIT_TEST
bool gGod, gKeepAI, gObserve, gCombat, gReleasePaused;
unsigned gKillAt;			// kills the driven unit at this tick (0: never)
unsigned gFireFrom, gFireTo;	// the driver is told to fire from this tick up to the next (0: never)
unsigned gFireNotes;
unsigned gCameraReplaceAt;
unsigned gBorrowAt, gBorrowUntil, gBorrowDestroy;
unsigned gFreeAt, gFreeAgain;
CCamera *gBorrowView, *gBorrowed;
unsigned gCameraMade, gCameraDestroyed, gCameraRestored;
int gLastFreeCamera = -1;
int gLastPaused = -1;
CSquad *gObservedOriginalSquad;
int gObservedOriginalSquadNumber = -1;
int gObservedAllegiance;
char gHandbackMode[16];
unsigned gHandbackAt;
bool gHandbackFixtureApplied;
CSquad *gExpectedSquad;
EAIState gExpectedAI;
FVector gExpectedDestination;
bool gExpectedGuide;
int gExpectedAllegiance;
const char *gProbePath;
unsigned gProbeEvery = 1;
FILE *gOut;
bool gCensus;
#endif

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}

bool FencedEnv()
{
	for (const char *name : kFenced)
		if (Present(name)) return true;
	return false;
}

void Log(const char *fmt, ...)
{
	char line[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	fprintf(stderr, PORT_LOG_PREFIX "play unit: %s\n", line);
	if (PORT_FEEL_ON()) PortFeelNote(line);		// frame-feel log (feel.h): when possession began and ended, on the same timeline
#ifdef CE_PLAYUNIT_TEST
	if (gOut)
	{
		fprintf(gOut, "E %u %s\n", gTick, line);
		fflush(gOut);
	}
#endif
}

#ifdef CE_PLAYUNIT_TEST
void Note(const char *fmt, ...)
{
	if (!gOut) return;
	va_list args;
	va_start(args, fmt);
	fputs("# ", gOut);
	vfprintf(gOut, fmt, args);
	fputc('\n', gOut);
	va_end(args);
	fflush(gOut);
}
#endif

// ---- the driver on the controller stack --------------------------------------------------------------------------------------------

// Everything but the drive actions goes to the player, who handles pause, help and skipping. The actions of the player's Battle Engine controls (BUTTON_MECH_*) that the driver
// does not use are dropped, not passed on: the engine is powered down while a tank is driven, and a mission that powered it up again must not find it steered by the same keys.
class CePilot : public IController
{
public:
	bool mDriving;

	CePilot() : mDriving(false) {}
	virtual void ReceiveButtonAction(CController *from, int button, float val)
	{
		CPlayer *player = GAME.GetPlayer(0);
		if (!mDriving)
		{
			if (player) player->ReceiveButtonAction(from, button, val);
			return;
		}
		switch (button)
		{
		// The values are signed deflections, as the walker's own controls take them: a held W arrives as -1 on BUTTON_MECH_FORWARD, S as 1 on BUTTON_MECH_BACKWARD, A as -1
		// on BUTTON_MECH_STRAFE_LEFT and D as 1 on BUTTON_MECH_STRAFE_RIGHT; the mouse's sideways axis arrives on both yaw buttons each tick (0 when it is still).
		case BUTTON_MECH_FORWARD: if (-val > gIn.forward) gIn.forward = -val; break;
		case BUTTON_MECH_BACKWARD: if (val > gIn.back) gIn.back = val; break;
		case BUTTON_MECH_STRAFE_LEFT: case BUTTON_MECH_STRAFE_RIGHT: case BUTTON_MECH_YAW_LEFT: case BUTTON_MECH_YAW_RIGHT: gIn.turn += val; break;
		// The mouse button (and a pad's right trigger) is BUTTON_MECH_CHARGE_GUN_POD while it is held and BUTTON_MECH_FIRE_GUN_POD when it is let go: either one fires the tank's gun.
		case BUTTON_MECH_CHARGE_GUN_POD: case BUTTON_MECH_FIRE_GUN_POD: if (val > 0.5f) gIn.fire = true; break;
		case BUTTON_MECH_MORPH: if (val > 0.5f) gIn.leave = true; break;
		default:
			if (player && !IsEngineButton(button)) player->ReceiveButtonAction(from, button, val);
			return;
		}
#ifdef CE_PLAYUNIT_TEST
		if (gOut) fprintf(gOut, "A %u %d %.3f\n", gTick, button, val);
#endif
	}
	virtual BOOL CanBeControlledWhenInPause()
	{
		CPlayer *player = GAME.GetPlayer(0);
		return player ? player->CanBeControlledWhenInPause() : FALSE;
	}
	virtual EControlType GetControlType()
	{
		CPlayer *player = GAME.GetPlayer(0);
		return player ? player->GetControlType() : CONTROL_MECH;
	}

private:
	static bool IsEngineButton(int button)
	{
		switch (button)
		{
		case BUTTON_MECH_CHANGE_ZOOM_IN: case BUTTON_MECH_CHANGE_ZOOM_OUT: case BUTTON_MECH_FIRE_GUN_POD: case BUTTON_MECH_CHARGE_GUN_POD:
		case BUTTON_MECH_CHANGE_WEAPON: case BUTTON_MECH_LANDING_JETS: case BUTTON_MECH_JET_AFTERBURNER: case BUTTON_MECH_YAW_LEFT:
		case BUTTON_MECH_PITCH_UP: case BUTTON_MECH_YAW_RIGHT: case BUTTON_MECH_PITCH_DOWN: case BUTTON_MECH_STRAFE_LEFT:
		case BUTTON_MECH_STRAFE_RIGHT: case BUTTON_MECH_FORWARD: case BUTTON_MECH_BACKWARD: case BUTTON_MECH_MORPH: case BUTTON_MECH_CLOAK:
		case BUTTON_MECH_CONFIGURATION_DOWN: case BUTTON_MECH_CONFIGURATION_UP:
			return true;
		default:
			return false;
		}
	}
};

// One driver for the process, built on first use in static storage and never destroyed: the controller's stack holds monitored readers of it, and a destructor that ran at exit
// while the game's own objects were being torn down could leave them pointing at nothing. (A union member is constructed by the union's own initializer and never destroyed
// by its empty destructor; the game's `new` macro rules out a placement new here.)
union PilotHolder
{
	CePilot pilot;
	PilotHolder() : pilot() {}
	~PilotHolder() {}
};

CePilot *Pilot()
{
	static PilotHolder holder;
	return &holder.pilot;
}

// A pause/menu controller can be above the driver when a camera or level transition relinquishes possession. Pop only our own top entry when that menu returns.
void ReconcileController()
{
	if (gDriving || !gControllerPending) return;
	if (CCONTROLLER *controller = GAME.GetController(0))
		if (controller->GetToControl() == Pilot())
		{
			controller->RelinquishControl();
			gControllerPending = false;
#ifdef CE_PLAYUNIT_TEST
			Note("controller reconciliation: pending=0 player_on_top=%d", controller->GetToControl() == GAME.GetPlayer(0));
#endif
		}
}

// ---- the world ----------------------------------------------------------------------------------------------------------------------

bool IsNormal(CSquad *squad) { return !strcmp(squad->_GetClassName(), "CNormalSquad"); }

FVector Ground(float x, float y)
{
	FVector p(x, y, 0);
	p.Z = MAP.Collide(p);
	return p;
}

// The unit the option holds, found in the world's things by address and number; NULL when it is gone. The world is walked, never the held address followed, so a unit that was
// deleted since the last look is never touched.
CUnit *Held()
{
	if (!gUnit) return NULL;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
		if ((void *)t == (void *)gUnit)
			return (t->GetThingNumber() == gThing && (t->GetThingType() & THING_TYPE_UNIT)) ? (CUnit *)t : NULL;
	return NULL;
}

CePlayUnitFacts Facts(CUnit *u, CBattleEngine *engine)
{
	CePlayUnitFacts f;
	memset(&f, 0, sizeof(f));
	const CUnitData *d = u->GetUnitData();
	CSquad *squad = u->GetSquad();
	f.friendly = u->GetAllegiance() == engine->GetAllegiance();
	f.alive = !u->IsDying() && u->GetLife() > 0.f;
	f.vehicle = (u->GetThingType() & THING_TYPE_VEHICLE) != 0;
	f.groundUnit = d && d->mBehaviour == UB_GROUND_UNIT && (u->GetThingType() & THING_TYPE_GROUND_UNIT) != 0 && !(u->GetThingType() & THING_TYPE_AIR_UNIT);
	f.scripted = u->GetMissionScript() != NULL || (squad && squad->GetMissionScript() != NULL);
	f.objective = u->IsObjective() != FALSE;
	f.deploys = d && (d->mUnknown108 || (d->mName && strstr(d->mName, "Artillery")));
	f.normalSquad = squad && IsNormal(squad);
	f.spawned = squad && squad->mUnknown84;
	f.armed = u->mCurrentWeapon != NULL;
	return f;
}

// The first tank, in the world's creation order, that the option may take.
CUnit *Pick(CBattleEngine *engine, int *friendlies)
{
	CUnit *found = NULL;
	*friendlies = 0;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
	{
		if (!(t->GetThingType() & THING_TYPE_UNIT) || t == (CThing *)engine) continue;
		CUnit *u = (CUnit *)t;
		const CePlayUnitFacts f = Facts(u, engine);
		if (f.friendly) ++*friendlies;
#ifdef CE_PLAYUNIT_TEST
		if (gOut && !gCensus && f.friendly && *friendlies <= 80)
		{
			const CUnitData *d = u->GetUnitData();
			CSquad *squad = u->GetSquad();
			fprintf(gOut, "U %d %s %s %d %08lx %d %s %d %d %d %d %d %.1f %.1f %.1f\n", (int)u->GetThingNumber(), u->_GetClassName(), d && d->mName ? d->mName : "-", d ? (int)d->mBehaviour : -1,
					(unsigned long)u->GetThingType(), f.friendly, squad ? squad->_GetClassName() : "-", f.scripted, f.spawned, f.objective, f.deploys, f.armed,
					u->GetPos().X, u->GetPos().Y, u->GetPos().Z);
		}
#endif
		if (!found && CePlayUnitTankEligible(f))
		{
#ifdef CE_PLAYUNIT_TEST
			if (gCombat)
			{
				CUnit *target = u->mAI ? u->mAI->mTarget.ToRead() : NULL;
				if (!target || target->IsDying() || target->GetLife() <= 0.f || !u->IsTargetAlligence(target->GetAllegiance()) || !u->CanFireAt(target, FALSE)) continue;
			}
#endif
			found = u;
		}
	}
#ifdef CE_PLAYUNIT_TEST
	if (gOut) gCensus = true;
#endif
	return found;
}

// ---- the take-over ------------------------------------------------------------------------------------------------------------------

void ClearInput()
{
	memset(&gIn, 0, sizeof(gIn));
}

// The game can delete a camera when another view replaces it. A virtual destructor notification records that ownership transfer without dereferencing a stale pointer.
class CeTankCamera : public CThing3rdPersonCamera
{
public:
	explicit CeTankCamera(CUnit *unit) : CThing3rdPersonCamera(unit)
	{
#ifdef CE_PLAYUNIT_TEST
		++gCameraMade;
#endif
	}
	virtual ~CeTankCamera()
	{
		if (gCamera == this) gCamera = NULL;
#ifdef CE_PLAYUNIT_TEST
		++gCameraDestroyed;
		Note("camera destruction: made=%u destroyed=%u restored=%u pending=%d", gCameraMade, gCameraDestroyed, gCameraRestored, gCamera != NULL);
#endif
	}
};

#ifdef CE_PLAYUNIT_TEST
void ObserveHandback()
{
	const bool sameLevel = gLevel == GAME.GetCurrentLevel();
	CUnit *unit = sameLevel ? Held() : NULL;
	CSquad *original = NULL;
	if (sameLevel)
	{
		ListIterator<CSquad> squads(&WORLD.GetSquadNB());
		for (CSquad *candidate = squads.First(); candidate; candidate = squads.Next())
			if (candidate == gObservedOriginalSquad && candidate->GetThingNumber() == gObservedOriginalSquadNumber
				&& !candidate->IsShuttingDown() && !candidate->IsDying()) original = candidate;
	}
	bool member = false;
	if (original && unit)
	{
		SPtrSet<CUnit> *members = original->GetUnits();
		ListIterator<CUnit> units(members);
		for (CUnit *candidate = units.First(); candidate; candidate = units.Next()) if (candidate == unit) member = true;
		delete members;
	}
	Note("handback observation: unit_live=%d same_team=%d original_squad_live=%d unit_link_restored=%d member_link_restored=%d ai_resumed=%d squad_ai_current=%d",
		unit && !unit->IsDying() && unit->GetLife() > 0.f, unit && unit->GetAllegiance() == gObservedAllegiance, original != NULL,
		unit && original && unit->GetSquad() == original, member, unit && unit->GetAIState() == gPrevAI,
		unit && original && unit->GetAIState() == original->GetAIState());
	if (gHandbackFixtureApplied && unit)
	{
		const bool destination = !gExpectedGuide || (unit->mGuide && unit->mGuide->mDestination.X == gExpectedDestination.X
			&& unit->mGuide->mDestination.Y == gExpectedDestination.Y && unit->mGuide->mDestination.Z == gExpectedDestination.Z);
		Note("handback fixture after: same_assignment=%d same_ai=%d same_destination=%d same_allegiance=%d",
			unit->GetSquad() == gExpectedSquad, unit->GetAIState() == gExpectedAI, destination, unit->GetAllegiance() == gExpectedAllegiance);
	}
}

// Isolated test worlds only: remove the other squad memberships through the native operation.
void TestLeaveOneMember(CNormalSquad *squad, CUnit *keep)
{
	SPtrSet<CUnit> *members = squad->GetUnits();
	ListIterator<CUnit> units(members);
	unsigned removed = 0;
	for (CUnit *candidate = units.First(); candidate; candidate = units.Next())
		if (candidate != keep && candidate->GetSquad() == squad) { squad->Unknown4e6f70(candidate); ++removed; }
	delete members;
	Note("handback fixture: other memberships removed=%u", removed);
}

#endif

// The ordinary camera slot owns this camera, just as it owns the player's control view. Do not detach a second saved camera: recreating the preferred control view releases ours.
void PutCameraOn(CUnit *u)
{
	gCamera = new (MEMTYPE_CAMERA) CeTankCamera(u);
	GAME.SetCurrentCamera(0, gCamera, TRUE);
}

// A cutscene or remote camera can borrow our still-live camera and restore it later. Wait for that hand-back rather than deleting a pointer the borrower still retains.
void ReconcileCamera()
{
	if (gDriving || !gCamera || GAME.IsFreeCameraOn(0) || GAME.GetCamera(0) != gCamera) return;
	CPlayer *player = GAME.GetPlayer(0);
	if (player && player->GetBattleEngine())
	{
		player->GotoControlView();
#ifdef CE_PLAYUNIT_TEST
		++gCameraRestored;
		Note("camera reconciliation: made=%u destroyed=%u restored=%u pending=%d", gCameraMade, gCameraDestroyed, gCameraRestored, gCamera != NULL);
#endif
	}
}

// Resolve a remembered squad only through this level's live registry. Pointer equality alone
// is not identity, and a former last-member squad may already have been retired.
CNormalSquad *PreviousSquad()
{
	if (!gPreviousSquad || gLevel != GAME.GetCurrentLevel()) return NULL;
	ListIterator<CSquad> squads(&WORLD.GetSquadNB());
	for (CSquad *candidate = squads.First(); candidate; candidate = squads.Next())
		if (candidate == gPreviousSquad && candidate->GetThingNumber() == gPreviousSquadNumber)
			return IsNormal(candidate) && !candidate->IsShuttingDown() && !candidate->IsDying() ? (CNormalSquad *)candidate : NULL;
	return NULL;
}

// Squads derive their allegiance from their members. Never join a surviving squad that has
// acquired a member from another side while the possessed unit was away.
bool SquadCompatible(CNormalSquad *squad)
{
	if (!squad) return false;
	ListIterator<CThing> things(&WORLD.GetThingNB());
	for (CThing *thing = things.First(); thing; thing = things.Next())
		if (thing->GetThingType() & THING_TYPE_UNIT)
		{
			CUnit *unit = (CUnit *)thing;
			if (!unit->IsDying() && unit->GetSquad() == squad && unit->GetAllegiance() != gPossessedAllegiance) return false;
		}
	return true;
}

#ifdef CE_PLAYUNIT_TEST
// Causal ownership controls for isolated test worlds. These controls are absent in shipped builds.
void TestHandback(CUnit *unit)
{
	if (!unit || !gHandbackAt || gTick != gHandbackAt || gHandbackFixtureApplied) return;
	CNormalSquad *previous = PreviousSquad();
	bool changed = false;
	if (!strcmp(gHandbackMode, "orphan") && previous)
	{
		TestLeaveOneMember(previous, unit);
		previous->Shutdown();
		changed = true;
	}
	else if (!strcmp(gHandbackMode, "identity") && previous)
	{
		gPreviousSquadNumber = -1;
		changed = true;
	}
	else if (!strcmp(gHandbackMode, "orders") && previous)
	{
		previous->SetAIState(AI_OFF);
		changed = true;
	}
	else if (!strcmp(gHandbackMode, "squad-side") && previous)
	{
		CUnit *member = NULL, *enemy = NULL;
		ListIterator<CThing> things(&WORLD.GetThingNB());
		for (CThing *thing = things.First(); thing; thing = things.Next())
			if (thing->GetThingType() & THING_TYPE_UNIT)
			{
				CUnit *candidate = (CUnit *)thing;
				if (!candidate->IsDying())
				{
					if (candidate->GetSquad() == previous) member = candidate;
					if (candidate->GetAllegiance() != gPossessedAllegiance) enemy = candidate;
				}
			}
		if (member && enemy) { member->SetAllegiance(enemy->GetAllegiance()); changed = true; }
	}
	else if (!strcmp(gHandbackMode, "reassigned"))
	{
		ListIterator<CThing> things(&WORLD.GetThingNB());
		for (CThing *thing = things.First(); thing; thing = things.Next())
			if (thing->GetThingType() & THING_TYPE_UNIT)
			{
				CUnit *other = (CUnit *)thing;
				CSquad *squad = other->GetSquad();
				if (other != unit && !other->IsDying() && other->GetAllegiance() == gPossessedAllegiance && squad
					&& squad != previous && IsNormal(squad) && !squad->IsShuttingDown() && !squad->IsDying())
				{
					((CNormalSquad *)squad)->Unknown67(unit, 0, 0);
					unit->SetAIState(squad->GetAIState());
					changed = unit->GetSquad() == squad;
					break;
				}
			}
	}
	else if (!strcmp(gHandbackMode, "ai"))
	{
		unit->SetAIState(AI_ON);
		unit->GoToPoint(Ground(unit->GetPos().X + kReach, unit->GetPos().Y), FALSE);
		changed = true;
	}
	else if (!strcmp(gHandbackMode, "side"))
	{
		ListIterator<CThing> things(&WORLD.GetThingNB());
		for (CThing *thing = things.First(); thing; thing = things.Next())
			if (thing->GetThingType() & THING_TYPE_UNIT)
			{
				CUnit *other = (CUnit *)thing;
				if (!other->IsDying() && other->GetAllegiance() != gPossessedAllegiance)
				{
					unit->SetAllegiance(other->GetAllegiance()); changed = true; break;
				}
			}
	}
	else if (!strcmp(gHandbackMode, "removed"))
	{
		unit->Shutdown();
		gHandbackFixtureApplied = true;
		Note("handback fixture: mode=removed applied=1");
		return;
	}
	gHandbackFixtureApplied = changed;
	if (changed)
	{
		gExpectedSquad = unit->GetSquad();
		gExpectedAI = unit->GetAIState();
		gExpectedAllegiance = unit->GetAllegiance();
		gExpectedGuide = unit->mGuide != NULL;
		if (gExpectedGuide) gExpectedDestination = unit->mGuide->mDestination;
	}
	Note("handback fixture: mode=%s applied=%d", gHandbackMode, changed);
}
#endif

void ReturnUnitControl()
{
	const bool sameLevel = gLevel == GAME.GetCurrentLevel();
	CUnit *unit = sameLevel ? Held() : NULL;
	CNormalSquad *squad = PreviousSquad();
	CePlayUnitReturnFacts facts = {};
	facts.sameLevel = sameLevel;
	facts.unitLive = unit && !unit->IsDying() && unit->GetLife() > 0.f;
	facts.sameTeam = unit && unit->GetAllegiance() == gPossessedAllegiance;
	facts.currentSquad = unit && unit->GetSquad();
	facts.aiStillOwned = unit && unit->GetAIState() == AI_OFF;
	facts.originalSquadLive = squad != NULL;
	facts.originalSquadCompatible = SquadCompatible(squad);
	switch (CePlayUnitChooseReturn(facts))
	{
	case CE_PLAYUNIT_RETURN_GONE:
		Log("unit hand-back: the former unit is no longer live here");
		break;
	case CE_PLAYUNIT_RETURN_EXTERNAL:
		Log("unit hand-back: keeping the unit's newer owner or AI orders");
		break;
	case CE_PLAYUNIT_RETURN_SQUAD:
		// Clear the last player destination first. The native membership operation is also
		// used by normal squad merging; the squad now supplies its current formation/orders.
		unit->GoToPoint(unit->GetPos(), FALSE);
		squad->Unknown67(unit, 0, 0);
		if (unit->GetSquad() == squad)
		{
			unit->SetAIState(squad->GetAIState());
			Log("unit hand-back: returned to the original squad's current orders");
			break;
		}
		// A declined native join still gets a valid independent hand-back.
		unit->SetAIState(gPrevAI);
		Log("unit hand-back: squad join declined; holding with the former AI state");
		break;
	case CE_PLAYUNIT_RETURN_INDEPENDENT:
		unit->GoToPoint(unit->GetPos(), FALSE);
		unit->SetAIState(gPrevAI);
		Log("unit hand-back: original squad unavailable; holding with the former AI state");
		break;
	}
	gPreviousSquad = NULL;
	gPreviousSquadNumber = -1;
}

void Release(const char *why)
{
	if (!gDriving) return;
	Log("giving the controls back (%s)", why);
#ifdef CE_PLAYUNIT_TEST
	CCONTROLLER *observedController = GAME.GetController(0);
	Note("release state: paused=%d driver_on_top=%d player_on_top=%d", GAME.IsPaused(),
		observedController && observedController->GetToControl() == Pilot(),
		observedController && observedController->GetToControl() == GAME.GetPlayer(0));
#endif
	gDriving = false;
	Pilot()->mDriving = false;
	ViewReset();
	// Enabling free camera exits this prototype and returns to the normal view. A later free-camera action works normally.
	if (GAME.IsFreeCameraOn(0)) GAME.ToggleFreeCameraOff(0);
	ReconcileController();
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
	if (engine && engine == gParkedEngine && engine->GetThingNumber() == gParkedEngineNumber && gLevel == GAME.GetCurrentLevel()
		&& gEngineDeactivated && !engine->IsDying() && !engine->GetIsPoweredUp()) engine->Activate();
	gEngineDeactivated = false;
#ifdef CE_PLAYUNIT_TEST
	if (!gKeepAI)
#endif
	ReturnUnitControl();
	ReconcileCamera();
#ifdef CE_PLAYUNIT_TEST
	ObserveHandback();
	Note("camera lifetime: made=%u destroyed=%u restored=%u pending=%d engine_up=%d", gCameraMade, gCameraDestroyed,
		gCameraRestored, gCamera != NULL, engine && engine->GetIsPoweredUp());
#endif
	gMoving = false;
	ClearInput();
}

bool Take(CPlayer *player, CBattleEngine *engine, CCONTROLLER *controller, CUnit *u)
{
#ifdef CE_PLAYUNIT_TEST
	if (!strcmp(gHandbackMode, "last") && IsNormal(u->GetSquad()))
		TestLeaveOneMember((CNormalSquad *)u->GetSquad(), u);
#endif
	gUnit = u;
	gThing = u->GetThingNumber();
	gPrevAI = u->GetAIState();
	gPreviousSquad = u->GetSquad();
	gPreviousSquadNumber = gPreviousSquad ? gPreviousSquad->GetThingNumber() : -1;
	gPossessedAllegiance = u->GetAllegiance();
	gParkedEngine = engine;
	gParkedEngineNumber = engine->GetThingNumber();
#ifdef CE_PLAYUNIT_TEST
	gObservedOriginalSquad = u->GetSquad();
	gObservedOriginalSquadNumber = gObservedOriginalSquad ? gObservedOriginalSquad->GetThingNumber() : -1;
	gObservedAllegiance = u->GetAllegiance();
#endif
	ClearInput();
	gMoving = false;
	gDesired = gLastAngle = 0.f;
	gHaveTime = false;
	gLastIssue = gTick;
	gFired = gReasserts = 0;
#ifdef CE_PLAYUNIT_TEST
	if (!gKeepAI)
#endif
	{
		u->SetAIState(AI_OFF);
		if (CSquad *squad = u->GetSquad())
			if (IsNormal(squad)) ((CNormalSquad *)squad)->Unknown4e6f70(u);
		u->GoToPoint(u->GetPos(), FALSE);
	}
	gEngineDeactivated = engine->GetIsPoweredUp() != FALSE;
	if (gEngineDeactivated) engine->Deactivate();
#ifdef CE_PLAYUNIT_TEST
	if (gGod) player->SetIsGod(TRUE);
#endif
	Pilot()->mDriving = true;
	controller->SetToControl(Pilot());
	gControllerPending = true;
	PutCameraOn(u);
	ViewReset();
	gDriving = true;
	Log("driving unit %d, a %s", (int)gThing, u->GetUnitData() && u->GetUnitData()->mName ? u->GetUnitData()->mName : "tank");
	return true;
}

// Whether the driver may take the controls now: the level is playing, the player's own engine is up and standing as a walker with the normal control view, and nothing else
// holds the controller.
bool Ready(CPlayer **playerOut, CBattleEngine **engineOut, CCONTROLLER **controllerOut)
{
	if (gCamera || GAME.GetGameState() != GAME_STATE_PLAYING || GAME.IsPaused()) return false;
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
	CCONTROLLER *controller = GAME.GetController(0);
	if (!engine || !controller || engine->IsDying() || engine->GetLife() <= 0.f || !engine->GetIsPoweredUp()) return false;
	// A powered-down engine that is not standing falls: levels that drop the engine in (level 201 does) are lost when it reaches the sea.
	if (engine->GetState() != BATTLE_ENGINE_STATE_WALKER || !engine->IsWalkerOnGroundOrObject()) return false;
	if (controller->GetToControl() != player || GAME.IsFreeCameraOn(0) || GAME.mRemoteCamera || GAME.mOldRemoteCamera) return false;
	CCamera *camera = GAME.GetCurrentCamera(0);
	if (!camera || !camera->GetShowHUD()) return false;		// the player's own control view (not the opening pan, a cutscene or a mission camera)
	*playerOut = player;
	*engineOut = engine;
	*controllerOut = controller;
	return true;
}

// The gate, filled from the game and the environment.
bool Allowed(bool optionOn)
{
	CePlayUnitGate g;
	memset(&g, 0, sizeof(g));
	g.optionOn = optionOn;
	g.ceLayer = PortCeLayer();
	g.recording = PortRecording();
	g.fencedEnv = FencedEnv();
	g.networkBoot = Present("BEA_NET_HOST") || Present("BEA_NET_JOIN") || (CE_BootRequested && CE_BootRequested());
	g.demo = PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo || CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode;
	g.multiplayer = GAME.IsMultiplayer() != FALSE;
	g.players = GAME.GetNPlayers();
	g.proof = gProof;
	return CePlayUnitAllowed(g);
}

void ReadConfiguration()
{
	gConfigured = true;
#ifdef CE_PLAYUNIT_TEST
	gProof = Present("BEA_PORT_PLAYUNIT_PROOF") && !strcmp(getenv("BEA_PORT_PLAYUNIT_PROOF"), "1");
	gGod = Present("BEA_PORT_PLAYUNIT_GOD") && !strcmp(getenv("BEA_PORT_PLAYUNIT_GOD"), "1");
	gKeepAI = Present("BEA_PORT_PLAYUNIT_KEEPAI") && !strcmp(getenv("BEA_PORT_PLAYUNIT_KEEPAI"), "1");
	gCombat = Present("BEA_PORT_PLAYUNIT_COMBAT") && !strcmp(getenv("BEA_PORT_PLAYUNIT_COMBAT"), "1");
	gReleasePaused = Present("BEA_PORT_PLAYUNIT_RELEASE_PAUSED") && !strcmp(getenv("BEA_PORT_PLAYUNIT_RELEASE_PAUSED"), "1");
	if (const char *v = getenv("BEA_PORT_PLAYUNIT_HANDBACK")) sscanf(v, "%15[^,],%u", gHandbackMode, &gHandbackAt);
	if (const char *v = getenv("BEA_PORT_PLAYUNIT_KILL")) gKillAt = (unsigned)strtoul(v, NULL, 10);
	if (const char *v = getenv("BEA_PORT_PLAYUNIT_CAMERA")) gCameraReplaceAt = (unsigned)strtoul(v, NULL, 10);
	if (const char *v = getenv("BEA_PORT_PLAYUNIT_BORROW")) sscanf(v, "%u,%u,%u", &gBorrowAt, &gBorrowUntil, &gBorrowDestroy);
	if (const char *v = getenv("BEA_PORT_PLAYUNIT_FREE")) sscanf(v, "%u,%u", &gFreeAt, &gFreeAgain);
	if (const char *v = getenv("BEA_PORT_PLAYUNIT_FIRE")) sscanf(v, "%u,%u", &gFireFrom, &gFireTo);
	gProbePath = getenv("BEA_PORT_PLAYUNIT_PROBE");
	if (gProbePath && !*gProbePath) gProbePath = NULL;
	if (const char *v = getenv("BEA_PORT_PLAYUNIT_PROBE_EVERY")) { const long n = strtol(v, NULL, 10); if (n > 0 && n < 1000000) gProbeEvery = unsigned(n); }
#endif
	if (const char *v = getenv("BEA_PORT_TANK_VIEW")) gViewMode = !strcmp(v, "rigid") ? kViewRigid : !strcmp(v, "stock") ? kViewStock : kViewSmooth;
	// Wanted at all, and allowed here: the ini is not even read where a replay path refuses the option (the fences that need no game object are asked first).
	const bool fenced = FencedEnv() && !gProof;
	// One of the unfinished features: ce.ini's play_unit=1 under [Experimental], or the older play_unit=tank (read once in ce.cpp).
	gWanted = !fenced && PortCeExperimental("play_unit") && !CeCmdMapOn();
#ifdef CE_PLAYUNIT_TEST
	gObserve = gProbePath != NULL;
#endif
}

void Reset()
{
	gTick = 0;
	gLooks = 0;
	gNextLook = kFirstLook;
	gDone = false;
	gUnit = NULL;
	gThing = -1;
	gPreviousSquad = NULL;
	gPreviousSquadNumber = -1;
	gParkedEngine = NULL;
	gParkedEngineNumber = -1;
	gMoving = false;
	ClearInput();
#ifdef CE_PLAYUNIT_TEST
	gCensus = false;
#endif
}

#ifdef CE_PLAYUNIT_TEST
// The fixture follows the existing cutscene/remote-camera ownership contract: borrow the current camera without deleting it, and later restore or destroy the borrowed view.
void FinishBorrow(bool destroy)
{
	if (!gBorrowed) return;
	if (destroy)
	{
		if (gBorrowed == gCamera) delete gBorrowed;
		gBorrowed = NULL;
		// The replacement view stays game-owned until ordinary shutdown.
		gBorrowView = NULL;
		Note("borrow fixture: destroyed saved camera");
	}
	else if (GAME.GetCamera(0) == gBorrowView || GAME.IsFreeCameraOn(0))
	{
		GAME.SetCamera(0, gBorrowed);
		delete gBorrowView;
		gBorrowed = gBorrowView = NULL;
		Note("borrow fixture: restored saved camera");
	}
}

void TestBorrow(CBattleEngine *engine)
{
	// Exercise the real camera/controller transition without changing a saved career's cheat flags or inventing a key binding.
	if ((gFreeAt && gTick == gFreeAt) || (gFreeAgain && gTick == gFreeAgain))
	{
		if (GAME.IsFreeCameraOn(0)) GAME.ToggleFreeCameraOff(0);
		else GAME.ToggleFreeCameraOn(0);
		Note("free camera fixture: active=%d", GAME.IsFreeCameraOn(0));
	}
	if (gBorrowAt && gTick == gBorrowAt && gDriving && gCamera && GAME.GetCamera(0) == gCamera && engine)
	{
		gBorrowed = gCamera;
		gBorrowView = new (MEMTYPE_CAMERA) CThing3rdPersonCamera(engine);
		GAME.SetCamera(0, gBorrowView);
		Note("borrow fixture: captured camera");
	}
	if (gBorrowUntil && gTick == gBorrowUntil) FinishBorrow(gBorrowDestroy != 0);
}

unsigned Rounds()
{
	unsigned n = 0;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
		if (t->GetThingType() & THING_TYPE_AMMUNITION) ++n;
	return n;
}

void Probe(CUnit *u, CBattleEngine *engine)
{
	if (!gOut || gTick % gProbeEvery) return;
	const FVector &p = u->GetPos();
	const FVector f = u->GetOrientation().GetColumnY();
	CGuide *guide = u->mGuide;
	const FVector e = engine ? engine->GetPos() : FVector(0, 0, 0);
	fprintf(gOut, "P %u %u %d %.3f %.3f %.3f %.3f %.3f %.1f %d %d %.3f %.3f %d %.2f %.2f %d %u %u %.3f %.3f %d %d %d %u %c\n", gTick, PortPresentCount(), (int)gThing, p.X, p.Y, p.Z,
			f.X, f.Y, u->GetLife(), (int)u->GetAIState(), u->GetSquad() ? 1 : 0, guide ? guide->mDestination.X : -1.f, guide ? guide->mDestination.Y : -1.f, gMoving ? 1 : 0,
			gApplied.forward, gApplied.turn, gApplied.fire ? 1 : 0, gFired, gReasserts, e.X, e.Y, engine && engine->GetIsPoweredUp() ? 1 : 0,
			engine ? (int)engine->GetState() : -1, engine && engine->IsWalkerOnGroundOrObject() ? 1 : 0,
			Rounds(), gDriving ? 'D' : 'O');
	fflush(gOut);
}
#endif

// One tick of driving: the actions the last flush delivered become the unit's orders.
void Drive(CUnit *u, CPlayer *player, CBattleEngine *engine)
{
	(void)player;
	(void)engine;
	CePlayUnitDrive in = gIn;
	ClearInput();
	in.forward = CePlayUnitClamp(in.forward, 0.f, 1.f);
	in.turn = CePlayUnitClamp(in.turn, -1.f, 1.f);
	gApplied = in;
	if (in.leave)
	{
		Release("the Morph key");
		return;
	}
	// Free-camera and external view changes end this first possession experiment. Restore ownership without overriding the external camera or commandeering its input.
	if (GAME.IsFreeCameraOn(0)) { Release("free camera"); return; }
	if (!gCamera || GAME.GetCamera(0) != gCamera) { Release("another camera"); return; }
	// A changed side, a squad assignment, or resumed AI is a newer owner. Relinquish the
	// lease instead of repeatedly detaching the unit or overwriting that owner's orders.
#ifdef CE_PLAYUNIT_TEST
	if (!gKeepAI)
#endif
	{
		if (u->GetAllegiance() != gPossessedAllegiance || u->GetSquad() || u->GetAIState() != AI_OFF)
		{
			Release("another owner or AI took control of the unit");
			return;
		}
	}
	const FVector pos = u->GetPos();
	const FVector heading = u->GetOrientation().GetColumnY();
	const float actual = atan2f(heading.Y, heading.X);
	const float now = EVENT_MANAGER.GetTime();
	float dt = gHaveTime ? now - gLastTime : 0.f;
	if (!(dt > 0.f)) dt = 0.f;
	if (dt > 0.1f) dt = 0.1f;
	gLastTime = now;
	gHaveTime = true;
	const bool pivot = in.forward <= 0.1f && fabsf(in.turn) > 0.05f;
	if (in.forward > 0.1f || pivot)
	{
		gDesired = CePlayUnitSteer(gMoving ? gDesired : actual, actual, in.turn, dt, kTurnRate, kMaxLead);
		float tx, ty;
		if (CePlayUnitPoint(pos.X, pos.Y, gDesired, pivot ? kPivotReach : kReach, &tx, &ty))
		{
			const bool changed = !gMoving || fabsf(CePlayUnitWrap(gDesired - gLastAngle)) > 0.05f;
			const float dx = gLastPoint.X - pos.X, dy = gLastPoint.Y - pos.Y;
			if (CePlayUnitReissue(changed, gTick, gLastIssue, pivot ? 2 : kReissue, sqrtf(dx * dx + dy * dy), pivot ? 2.f : kNear))
			{
				gLastPoint = Ground(tx, ty);
				u->GoToPoint(gLastPoint, FALSE);
				gLastIssue = gTick;
				// Compare future turns with the heading actually issued, so small per-tick turns accumulate.
				gLastAngle = gDesired;
			}
			gMoving = true;
		}
	}
	else
	{
		gDesired = actual;
		if (gMoving)
		{
			u->GoToPoint(pos, FALSE);
			gMoving = false;
		}
	}
#ifdef CE_PLAYUNIT_TEST
	if (gFireFrom && gTick >= gFireFrom && gTick < gFireTo) in.fire = true;
#endif
	gApplied.fire = in.fire;
	if (in.fire && u->mAI)
	{
#ifdef CE_PLAYUNIT_TEST
		const unsigned before = Rounds();
#endif
		// The stock ground vehicle owns a CUnitAI. Reuse its targeting/aim/arm/fire handler, not its movement driver. AI_OFF and our guide orders still own locomotion.
		// This handler applies the game's enemy, weapon-type, range, line-of-fire, aim, warm-up and burst rules; it can consume the game's RNG while this option is active.
		u->mAI->CUnitAI::Unknown3();
#ifdef CE_PLAYUNIT_TEST
		const unsigned after = Rounds();
		const unsigned spawned = after > before ? after - before : 0;
		gFired += spawned;
		if (gFireNotes < 16 && (spawned || gFireNotes < 4))
		{
			++gFireNotes;
			CUnit *target = u->mAI->mTarget.ToRead();
			Note("fire at tick %u: ready %d, target %s, own_rounds=%u", gTick,
				u->mCurrentWeapon ? (int)u->mCurrentWeapon->ReadyToFire() : -1,
				target ? target->_GetClassName() : "none", spawned);
		}
#endif
	}
}

// ---- the displayed camera ---------------------------------------------------------------------------------------------------------------

// The clock the settling reads, in nanoseconds as an integer, so that the time between two frames is exact whatever the machine's uptime (the game's x87 unit is left at single
// precision, which cannot hold the difference of two readings of a clock in milliseconds after a day or two; PARITY.md, "Frame feel"). A virtual clock gives the frames' own 1/30 s.
unsigned long long ViewNs()
{
	if (PortClockVirtual()) return (unsigned long long)PortPresentCount() * 33333333ull;
	return PortClockRawNs();
}

void ViewReset()
{
	memset(&gSmoother, 0, sizeof(gSmoother));
	gShownFrame = ~0u;
	gShownMade = false;
}

// The displayed camera for this frame: the rigid one (pos, ori), hung from the unit's displayed position with its tilt and offset settled (ce_playunit_view.h). The settling moves
// once for each frame, by the time since the last one; a frame's other views are shown the camera the first one was.
void SmoothView(CUnit *unit, FVector *pos, FMatrix *ori)
{
	const unsigned frame = PortPresentCount();
	if (frame == gShownFrame)
	{
		if (gShownMade) { *pos = gShownPos; *ori = gShownOri; }
		return;
	}
	gShownFrame = frame;
	gShownMade = false;
	const unsigned long long now = ViewNs();
	const float dt = gSmoother.valid && now > gShownNs ? (float)((double)(now - gShownNs) * 1e-9) : 0.f;
	gShownNs = now;
	const FVector anchor = unit->GetRenderPos();
	const FVector heading = unit->GetRenderOrientation().GetColumnY();
	const float unitYaw = atan2f(heading.Y, heading.X);
	const FVector right = ori->GetColumnX(), forward = ori->GetColumnY(), offset = *pos - anchor;
	const float r[3] = {right.X, right.Y, right.Z}, f[3] = {forward.X, forward.Y, forward.Z}, o[3] = {offset.X, offset.Y, offset.Z};
	CeUnitView target, shown;
	if (!CeUnitViewDescribe(r, f, o, unitYaw, &target))
	{
		gSmoother.valid = false;
		return;
	}
	CeUnitViewStep(&gSmoother, target, dt, kUnitViewOmega, &shown);
	float sr[3], sf[3], so[3];
	CeUnitViewBuild(shown, unitYaw, sr, sf, so);
	const float su[3] = {sr[1] * sf[2] - sr[2] * sf[1], sr[2] * sf[0] - sr[0] * sf[2], sr[0] * sf[1] - sr[1] * sf[0]};
	// the engine's matrices are built from rows: the camera's axes are the columns
	gShownOri = FMatrix(FVector(sr[0], sf[0], su[0]), FVector(sr[1], sf[1], su[1]), FVector(sr[2], sf[2], su[2]));
	gShownPos = anchor + FVector(so[0], so[1], so[2]);
	gShownMade = true;
	*pos = gShownPos;
	*ori = gShownOri;
}
}

// ---- the hooks ----------------------------------------------------------------------------------------------------------------------


// Rendering may follow an actor's displayed transform without changing the
// logical camera queried by gameplay. The returned camera is never installed
// in GAME and carries no unit pointer or lease across frames. The displayed
// camera is the game's camera carried to the unit's displayed pose; unless
// BEA_PORT_TANK_VIEW says otherwise its tilt and offset are then settled
// (ce_playunit_view.h), which spreads what uneven ground does to the game's
// camera in each update over the frames of about a fifth of a second.
CCamera *PortCePlayUnitRenderCamera(CCamera *original, CPlayer *player) {
    if (!gDriving || !gCamera || original != gCamera ||
        GAME.GetCurrentCamera(0) != gCamera || player != GAME.GetPlayer(0) ||
        GAME.GetCurrentLevel() != gLevel || !Allowed(true)) return original;
    CUnit *unit = Held();
    if (!unit || unit != gUnit || unit->IsDying() || gViewMode == kViewStock) return original;
    const FVector rawPos = unit->GetPos();
    const FMatrix correction = unit->GetRenderOrientation() * unit->GetOrientation().Transpose();
    FVector pos = unit->GetRenderPos() + correction * (original->GetPos() - rawPos);
    FMatrix ori = correction * original->GetOrientation();
    if (gViewMode == kViewSmooth) SmoothView(unit, &pos, &ori);
    static CGenericCamera view;
    view.mPos = pos;
    view.mOrientation = ori;
    view.mZoom = original->GetZoom();
    return &view;
}

extern "C" int CePlayUnitFeelPose(float *out)
{
	if (!out || !gDriving || !gCamera) return 0;
	CUnit *unit = Held();
	if (!unit || unit != gUnit || unit->IsDying()) return 0;
	const FVector renderPos = unit->GetRenderPos(), simPos = unit->GetPos();
	const FVector renderHeading = unit->GetRenderOrientation().GetColumnY(), simHeading = unit->GetOrientation().GetColumnY();
	out[0] = renderPos.X; out[1] = renderPos.Y; out[2] = renderPos.Z;
	out[3] = simPos.X; out[4] = simPos.Y; out[5] = simPos.Z;
	out[6] = renderHeading.X; out[7] = renderHeading.Y; out[8] = renderHeading.Z;
	out[9] = simHeading.X; out[10] = simHeading.Y; out[11] = simHeading.Z;
	return 1;
}

void CePlayUnitUpdate()
{
	if (!gConfigured) ReadConfiguration();
#ifdef CE_PLAYUNIT_TEST
	if (gOut && gLastPaused != (int)GAME.IsPaused())
	{
		gLastPaused = (int)GAME.IsPaused();
		Note("paused observed=%d", gLastPaused);
	}
	// A narrow ownership control: relinquish while the real pause menu owns the top controller, then let the real menu close normally.
	if (gReleasePaused && gDriving && GAME.IsPaused()) Release("paused ownership control");
#endif
	ReconcileController();
	ReconcileCamera();
#ifdef CE_PLAYUNIT_TEST
	const bool interested = gWanted || gObserve;
#else
	const bool interested = gWanted;
#endif
	if (!interested) return;
	const int state = GAME.GetGameState();
	const bool playing = state == GAME_STATE_PLAYING;
	if (state == GAME_STATE_NOT_RUNNING || state == GAME_STATE_PRE_RUNNING || (gLevel != -1 && gLevel != GAME.GetCurrentLevel()))
	{
		if (gDriving) Release("a new level");
		Reset();
		gLevel = state == GAME_STATE_NOT_RUNNING ? -1 : GAME.GetCurrentLevel();
		return;
	}
	gLevel = GAME.GetCurrentLevel();
	if (!playing)
	{
		if (gDriving) Release("the level is not playing");
		return;
	}
	if (GAME.IsPaused()) { ClearInput(); return; }
	++gTick;
#ifdef CE_PLAYUNIT_TEST
	if (!gOut && gProbePath)
	{
		gOut = fopen(gProbePath, "w");
		if (gOut) { fprintf(gOut, "# play unit probe 1\n"); fflush(gOut); }
	}
	if (gOut && gTick == 1) Note("level %d", gLevel);
	if (gOut && gLastFreeCamera != (int)GAME.IsFreeCameraOn(0))
	{
		gLastFreeCamera = (int)GAME.IsFreeCameraOn(0);
		Note("free camera observed=%d", gLastFreeCamera);
	}
#endif
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
#ifdef CE_PLAYUNIT_TEST
	TestBorrow(engine);
	ReconcileCamera();
#endif
	if (gDriving)
	{
		CUnit *u = Held();
#ifdef CE_PLAYUNIT_TEST
		TestHandback(u);
		u = Held();
		if (u && gKillAt && gTick == gKillAt) { u->mLife = 0.f; u->StartDieProcess(); }
		if (gCameraReplaceAt && gTick == gCameraReplaceAt && player) player->GotoControlView();
#endif
		if (!Allowed(true)) Release("the option is no longer allowed here");
		else if (!u || u->IsDying() || u->GetLife() <= 0.f) Release("the unit is gone");
		else if (!engine || engine->IsDying() || engine->GetLife() <= 0.f) Release("the Battle Engine is gone");
		else
		{
			Drive(u, player, engine);
#ifdef CE_PLAYUNIT_TEST
			if (gDriving) Probe(u, engine);
#endif
		}
		return;
	}
#ifdef CE_PLAYUNIT_TEST
	if (!gWanted || !Allowed(true))
	{
		// Observing: the unit the option would take, followed with the same probe, for a run that does not drive.
		if (gObserve && engine && !gDone && !Held())
		{
			if (gTick >= gNextLook)
			{
				int friendlies = 0;
				CUnit *u = Pick(engine, &friendlies);
				++gLooks;
				gNextLook = gTick + kLookEvery;
				if (u) { gUnit = u; gThing = u->GetThingNumber(); Note("observing unit %d at tick %u (%d friendly units)", (int)gThing, gTick, friendlies); }
				else if (gLooks >= kMaxLooks) { gDone = true; Note("no tank to observe"); }
			}
		}
		if (gObserve && gUnit) { if (CUnit *u = Held()) Probe(u, engine); }
		return;
	}
#else
	if (!Allowed(true)) return;
#endif
	if (gDone || gTick < gNextLook) return;
	CBattleEngine *readyEngine = NULL;
	CPlayer *readyPlayer = NULL;
	CCONTROLLER *controller = NULL;
	if (!Ready(&readyPlayer, &readyEngine, &controller))
	{
		gNextLook = gTick + 1;		// the control view is not up yet: look again next tick (not counted as a look)
		return;
	}
	int friendlies = 0;
	CUnit *u = Pick(readyEngine, &friendlies);
	++gLooks;
	gNextLook = gTick + kLookEvery;
	if (!u)
	{
		if (gLooks >= kMaxLooks)
		{
			gDone = true;
			Log("no friendly tank to drive in this level (%d friendly units)", friendlies);
		}
		return;
	}
	gDone = true;
	Take(readyPlayer, readyEngine, controller, u);
}

void CePlayUnitLevelEnd()
{
#ifdef CE_PLAYUNIT_TEST
	FinishBorrow(true);
#endif
	if (gDriving) Release("the level is ending");
	ReconcileController();
	ReconcileCamera();
	Reset();
	gLevel = -1;
#ifdef CE_PLAYUNIT_TEST
	if (gOut)
	{
		Note("level end: camera_made=%u camera_destroyed=%u camera_pending=%d controller_pending=%d", gCameraMade, gCameraDestroyed, gCamera != NULL, gControllerPending);
		fflush(gOut);
	}
#endif
}

#endif
