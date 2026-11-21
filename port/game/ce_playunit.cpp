// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit: an option that hands the local player's controls to a friendly ground tank the player chooses.
//
// The player looks at one of their side's units and presses the take key (E, read every frame from the platform's record of the keyboard, edge by edge, only while the
// level plays unpaused with no menu, console or map holding the keys): the unit nearest the middle of the view within reach that a class accepts is taken
// (CePlayUnitAtScreen, CePlayUnitChoose in ce_playunit_rules.h). A refused press plays the cancel cue and says why in one sentence (CePlayUnitRefusalSentence); a take
// plays the select cue and names the unit and the way back in the HUD's message capsule (ce_playunit_hud.cpp). Morph returns, E while driving takes the unit in the middle
// of the driven unit's view instead, and E again after a return takes another: there is no limit for a level. Nothing is taken without a press.
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
// - held fire aims at the ground point under the middle of the view and fires through the unit's own aim, arm and discharge calls with no target (free fire, "free fire" below; the class
//   table in ce_playunit_rules.h says which classes have it); where free fire is not allowed, or the class has none, it calls the stock ground vehicle AI's targeting/aim/arm/fire
//   handler as the first experiment did; either way the movement AI stays off and the driver retains the guide orders;
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
// clock, a test pilot or a network game (the variables below), in a demo or a multiplayer level, while the Commander view owns the controls, or with a second player. The opted-in firing handler
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
#ifdef CE_PLAYUNIT_TEST
#include "Line.h"
#include "UnitAI.h"
#include "round.h"
#endif
#include "squad.h"
#include "unit.h"
#include "map.h"
#include "thing.h"
#include "mesh.h"
#include "player.h"
#include "BattleEngine.h"
#include "Controller.h"
#include "Guide.h"
#include "Career.h"
#include "MessageBox.h"
#include "Camera.h"
#include "DXEngine.h"
#include "viewport.h"
#include "Compass.h"
#include "frontend.h"
#include "ltshell.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"
#include "CLIParams.h"
#include "WorldPhysicsManager.h"
#include "eventmanager.h"
#include "ce_playunit.h"
#include "ce_cmdorders.h"
#include "ce_cmdproduction.h"
#include "ce_playunit_rules.h"
#include "ce_dodge_binding.h"
#include "ce_playunit_infantry.h"
#include "ce_playunit_view.h"
#include "ce_playunit_look.h"
#include "ce_playunit_camera.h"
#include "ce_playunit_hud.h"
#include "ce_commander.h"
#include "../platform/common/portlog.h"
#include "../platform/common/feel.h"

#include <d3dx9.h>
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
bool PortCePhotoOn();													// ce_photo.cpp: photo mode hides the HUD
bool PortCePhotoSavedNext();											// ce_photo.cpp: the next frame is saved as a picture
extern "C" int PortKeyHeldNow(unsigned dik);							// window.cpp: 1 a key is held, 0 it is not, -1 the platform cannot say

namespace
{
uint64_t gSelectionWorld = 1;
void SelectionWorldEnded()
{
    if (++gSelectionWorld == 0) ++gSelectionWorld;
}

const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};

#ifdef CE_PLAYUNIT_TEST
const unsigned kFirstLook = 5;			// ticks of play before the census and the observing probe's first look (the level has settled)
const unsigned kLookEvery = 30;			// ticks between the observing probe's looks while there is none
const unsigned kMaxLooks = 20;			// looks before the observing probe gives up for the level
#endif

// The take key: E, polled every frame from the platform's own record of the keyboard (the game binds nothing to it by default; where the player has bound it to one of
// the game's controls the take key is off, so that it never does two things).
unsigned TakeKey() { return (unsigned)PortCeActionKey(CeControlAction::TakeUnit); }

// How the driven class is driven (ce_playunit_rules.h: the table of classes, the tank's the only row that is on), and what the reticle says of the held fire.
const CePlayUnitLaw *gLaw;
int gFireState = CE_PLAYUNIT_FIRE_NONE;
FVector gAimPoint;
static bool gAimValid = false;						// the point the gun is turned toward: the weapon's aim call reads it where it stands

bool gConfigured, gWanted, gProof;
bool gBackPending;						// the unit was given back while another view held the screen: say so when the player's own view is back
bool gDriving;							// the controls are the tank's
bool gControllerPending;			// a menu may temporarily sit above the inactive driver
int gLevel = -1;
unsigned gTick;							// updates of this level in which it was playing and not paused
#ifdef CE_PLAYUNIT_TEST
unsigned gLooks, gNextLook;
CUnit *gFootObservedUnit;
CGuide *gFootObservedGuide;
CSquad *gFootObservedSquad;
bool gObserveDone;						// the observing probe found nothing to follow and stopped looking for this level
#endif
CePlayUnitKey gTakeKey;					// the take key as the frames saw it
bool gTakePressed;						// a press of the take key that the next tick answers
CUnit *gSwitchUnit;						// a unit the player switched to from the one driven: taken as soon as the Battle Engine's own view is back
int gSwitchThing = -1;
unsigned gSwitchWait;					// ticks the switch may still wait

CUnit *gUnit;							// the driven (or observed) unit: compared by address with the world's things, never followed until found there
int gThing = -1;
EAIState gPrevAI = AI_ON;
CSquad *gPreviousSquad;
int gPreviousSquadNumber = -1;
// The sole delivered vehicle must keep its live order owner while the pilot drives.
bool gDeliveryAssigned;
bool gOnFoot;
bool gFootFirstPerson;
const CCamera *gFootRenderedView;
bool gFootZoomIn, gFootZoomOut;
int gPossessedAllegiance;
CBattleEngine *gParkedEngine;
int gParkedEngineNumber = -1;
bool gEngineDeactivated;
class CeTankCamera;
CeTankCamera *gCamera;			// the game's own third-person camera on the unit, while the game's camera is the unit's

// How the camera is made (BEA_PORT_TANK_VIEW, read once with the ini): "look", the default, is the player's look (CeLookCamera, ce_playunit_look.h): the mouse turns and tilts
// it, the ground cannot, and it hangs behind and above the unit; the others are the game's own third-person camera on the unit, "smooth" hung from the unit's displayed
// position with its tilt and offset settled (ce_playunit_view.h), "rigid" fixed to the unit's displayed pose, as the first version of the render hook did, and "stock" as the
// game makes it. All are on one build so that they can be compared.
enum { kViewSmooth, kViewRigid, kViewStock, kViewLook };
int gViewMode = kViewSmooth;
CeUnitViewSmoother gSmoother;			// where the displayed camera's tilt and offset are, settling toward the game's camera
unsigned gShownFrame = ~0u;				// the Present the displayed camera was made for: a frame's other views (a reflection) take the one the first view used
bool gShownMade;
FVector gShownPos;
FMatrix gShownOri;
unsigned long long gShownNs;			// the clock at that frame
void ViewReset();

float gLookYaw, gLookPitch;				// the player's look while a unit is driven (ce_playunit_look.h's angles)
unsigned gMouseQuiet = ~0u;				// ticks since the mouse last moved the look: the yaw actions (a pad's stick, the game's filtered mouse) turn it only when the mouse is still

CePlayUnitDrive gIn;					// the actions of the last flush, reduced
bool gPathDrive;						// BEA_PORT_TANK_DRIVE=path: the first experiment's law (go-to-points through the stock guide) instead of the direct one
CePlayUnitDrive gApplied;				// the ones the last tick applied (the probe prints them)
bool gMoving;
float gDesired, gLastAngle;				// the heading asked for, and the one the last go-to-point was made for (radians)
float gLastTime;						// the event manager's time at the last tick
bool gHaveTime;
FVector gLastPoint;
unsigned gLastIssue;
unsigned gFired, gReasserts;
#ifdef CE_PLAYUNIT_TEST
unsigned gKitRounds;						// rounds launched since the take, the last one's impact and its aim at launch (CePlayUnitRoundsNow)
FVector gKitImpact, gKitLaunchAim;
bool gKitImpactSeen;
#endif
int gLastNotedState = -1;

#ifdef CE_PLAYUNIT_TEST
bool gGod, gKeepAI, gObserve, gCombat, gReleasePaused, gStockFire;
bool gTakeOnce;				// a press while a unit is driven is let go (a scripted scan that presses the take key until something is taken)
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
unsigned gReturnedAt;		// the tick of the last return: the probe notes the radio's capsule for the next ticks (LOOK 9: whose is the capsule at the bottom)
CSquad *gObservedOriginalSquad;
int gObservedOriginalSquadNumber = -1;
int gObservedAllegiance;
char gHandbackMode[16];
unsigned gHandbackAt;
bool gHandbackFixtureApplied;
CSquad *gExpectedSquad;
EAIState gExpectedAI;
FVector gHandbackIssuedDestination;
bool gHandbackIssuedPoint = false;
bool gHandbackExpectStop = false;
CUnit *gReturnedWitness = NULL;
int gReturnedWitnessNumber = -1, gReturnedWitnessTick = 0, gReturnedWitnessLevel = -1;
FVector gReturnedWitnessStart;
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
		case BUTTON_MECH_STRAFE_LEFT: case BUTTON_MECH_STRAFE_RIGHT: gIn.turn += val; break;
		// the yaw actions (a pad's right stick, and the game's own filtered copy of the mouse) turn the look when it is the player's, and steer as before otherwise
		case BUTTON_MECH_YAW_LEFT: case BUTTON_MECH_YAW_RIGHT: if (gViewMode == kViewLook) gIn.look += val; else gIn.turn += val; break;
		// the pitch actions (a pad's right stick up and down, and the game's filtered mouse) tilt the look when it is the player's, with the player's Invert Y as the
		// Battle Engine on the ground takes it (CPlayer::ReceiveButtonAction); dropped otherwise, as before
		case BUTTON_MECH_PITCH_UP: case BUTTON_MECH_PITCH_DOWN: if (gViewMode == kViewLook) gIn.pitch += CAREER.GetInvertYAxis(0) ? -val : val; break;
		// The mouse button (and a pad's right trigger) is BUTTON_MECH_CHARGE_GUN_POD while it is held and BUTTON_MECH_FIRE_GUN_POD when it is let go: either one fires the tank's gun.
		case BUTTON_MECH_CHARGE_GUN_POD: case BUTTON_MECH_FIRE_GUN_POD: if (val > 0.5f) gIn.fire = true; break;
		case BUTTON_MECH_CHANGE_ZOOM_IN:
            case BUTTON_MECH_CHANGE_ZOOM_OUT:
                if (gViewMode == kViewLook)
                {
                    if (val > 0.f) { if (button == BUTTON_MECH_CHANGE_ZOOM_IN) gFootZoomIn = true; else gFootZoomOut = true; }
                }
                // These are engine actions. Outside soldier look, the parked pilot still does not receive them.
                break;
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

bool IsOrdinaryFoot(CUnit *unit, CSquad *squad)
{
	return unit && unit->mGuide && !strcmp(unit->_GetClassName(), "CInfantryUnit")
	    && (!squad || (!strcmp(squad->_GetClassName(), "CRelaxedSquad") && !squad->IsShuttingDown() && !squad->IsDying()));
}

// Keep a lone soldier independent, or keep the original relaxed-squad assignment.
bool FootKeepsAssignment(CUnit *unit)
{
	if ((!gOnFoot && !gDeliveryAssigned) || !unit || unit->GetSquad() != gPreviousSquad || gLevel != GAME.GetCurrentLevel()) return false;
	if (!gPreviousSquad) return true;
	ListIterator<CSquad> squads(&WORLD.GetSquadNB());
	for (CSquad *s = squads.First(); s; s = squads.Next())
		if (s == gPreviousSquad && s->GetThingNumber() == gPreviousSquadNumber)
			return !s->IsShuttingDown() && !s->IsDying();
	return false;
}

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

// What kind of unit the thing-type bits say it is, for the sentence a refused unit gets (CePlayUnitFamily). A gun mounted on a carrier or a ship (a component with a
// weapon) is a turret; a component without one (a carrier's health pad) is not.
int FamilyOf(unsigned long type, bool armed)
{
	if (type & THING_TYPE_INFANTRY) return CE_PLAYUNIT_FAMILY_INFANTRY;
	if (type & THING_TYPE_AIR_UNIT) return CE_PLAYUNIT_FAMILY_AIRCRAFT;
	if (type & THING_TYPE_NAVAL) return CE_PLAYUNIT_FAMILY_NAVAL;
	if (type & THING_TYPE_MECH) return CE_PLAYUNIT_FAMILY_WALKER;
	if (type & THING_TYPE_BUILDING) return CE_PLAYUNIT_FAMILY_OTHER;
	if (type & THING_TYPE_EMPLACEMENT) return CE_PLAYUNIT_FAMILY_EMPLACEMENT;
	if (type & THING_TYPE_COMPONENT) return armed ? CE_PLAYUNIT_FAMILY_EMPLACEMENT : CE_PLAYUNIT_FAMILY_OTHER;
	if (type & THING_TYPE_VEHICLE) return CE_PLAYUNIT_FAMILY_GROUND_VEHICLE;
	return CE_PLAYUNIT_FAMILY_OTHER;
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
	f.ordinaryFoot = IsOrdinaryFoot(u, squad);
	f.spawned = squad && squad->mUnknown84;
	// Armed: a current weapon, or one the unit carries with a mode (the game clears the current weapon between engagements; free fire makes the first ready one current).
	f.armed = u->mCurrentWeapon != NULL;
	if (!f.armed)
	{
		ListIterator<CWeapon> weapons(&u->mWeapons);
		for (CWeapon *w = weapons.First(); w && !f.armed; w = weapons.Next())
			if (w->mMode) f.armed = true;
	}
	f.family = FamilyOf(u->GetThingType(), f.armed);
	return f;
}

#ifdef CE_PLAYUNIT_TEST
// The census (once a level, at the first look): every friendly unit, with its class, its name, its facts and why it may not be taken (or "ok"), then a count. Also the first
// unit in the world's creation order that may be taken, which the observing probe follows.
CUnit *Census(CBattleEngine *engine, int *friendlies)
{
	CUnit *found = NULL;
	*friendlies = 0;
	int takeable = 0;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
	{
		if (!(t->GetThingType() & THING_TYPE_UNIT) || t == (CThing *)engine) continue;
		CUnit *u = (CUnit *)t;
		const CePlayUnitFacts f = Facts(u, engine);
		if (!f.friendly) continue;
		++*friendlies;
		const int refusal = CePlayUnitRefusalFor(f);
		const int kind = CePlayUnitClassFor(f);
		if (refusal == CE_PLAYUNIT_TAKE_OK) ++takeable;
		if (gOut && !gCensus)
		{
			const CUnitData *d = u->GetUnitData();
			CSquad *squad = u->GetSquad();
			fprintf(gOut, "U %d %s %s %d %08lx %d %s %d %d %d %d %d %.1f %.1f %.1f %d %s %s\n", (int)u->GetThingNumber(), u->_GetClassName(), d && d->mName ? d->mName : "-", d ? (int)d->mBehaviour : -1,
					(unsigned long)u->GetThingType(), f.friendly, squad ? squad->_GetClassName() : "-", f.scripted, f.spawned, f.objective, f.deploys, f.armed,
					u->GetPos().X, u->GetPos().Y, u->GetPos().Z, f.family, CePlayUnitRefusalName(refusal), kind >= 0 ? CePlayUnitClasses()[kind].name : "-");
		}
		if (!found && kind >= 0)
		{
			if (gCombat)
			{
				CUnit *target = u->mAI ? u->mAI->mTarget.ToRead() : NULL;
				if (!target || target->IsDying() || target->GetLife() <= 0.f || !u->IsTargetAlligence(target->GetAllegiance()) || !u->CanFireAt(target, FALSE)) continue;
			}
			found = u;
		}
	}
	if (gOut && !gCensus) Note("census: level %d friendly %d takeable %d", gLevel, *friendlies, takeable);
	if (gOut) gCensus = true;
	return found;
}
#endif

float TerrainHeight(void *, float x, float y);

// The picture the player saw at the last frame: the eye of the camera the engine last drew from (CEngine::SetViewpoint keeps it, after the render hook has put the driven
// unit's displayed camera in place of the game's), and a unit's place on that frame's screen, projected as the HUD projects its own reticle and markers (the world, view and
// projection matrices CDXEngine keeps from the frame for the HUD, the frame's viewport, the screen shape; CRenderInfo::Project), so that a unit is where it was drawn.
bool RenderedEye(CCamera *logical, float *eye)
{
	// the eye the frame was drawn from: where the inverse of its world and view matrices puts the origin of the view
	const CViewport *vp = ENGINE.GetViewportForViewpoint(0);
	if (vp && vp->Width && vp->Height)
	{
		D3DXMATRIX wv, inv;
		D3DXMatrixMultiply(&wv, (const D3DXMATRIX *)&ENGINE.mWorldMatrices[0], (const D3DXMATRIX *)&ENGINE.mViewMatrices[0]);
		float det = 0.f;
		if (D3DXMatrixInverse(&inv, &det, &wv) && isfinite(det) && det != 0.f)
		{
			eye[0] = inv._41; eye[1] = inv._42; eye[2] = inv._43;
			if (isfinite(eye[0]) && isfinite(eye[1]) && isfinite(eye[2])) return true;
		}
	}
	// no frame drawn yet: the camera the engine was last given, or the game's own
	CCamera *cam = ENGINE.GetCameraForViewpoint(0);
	if (!cam) cam = logical;
	if (!cam) return false;
	const FVector e = cam->GetPos();
	eye[0] = e.X; eye[1] = e.Y; eye[2] = e.Z;
	return isfinite(e.X) && isfinite(e.Y) && isfinite(e.Z);
}

bool ScreenPlace(CUnit *u, const float *eye, CePlayUnitScreenPlace *out)
{
	const CViewport *vp = ENGINE.GetViewportForViewpoint(0);
	if (!vp || !vp->Width || !vp->Height) return false;
	const FVector p = u->GetPos();
	const D3DXMATRIX *world = (const D3DXMATRIX *)&ENGINE.mWorldMatrices[0], *view = (const D3DXMATRIX *)&ENGINE.mViewMatrices[0];
	const D3DXMATRIX *proj = (const D3DXMATRIX *)&ENGINE.mProjectionMatrices[0];
	D3DXMATRIX wv;
	D3DXMatrixMultiply(&wv, world, view);
	// the depth along the view (the row-vector convention of the matrices: x' = x * m11 + y * m21 + z * m31 + m41, and so on)
	const float depth = p.X * wv._13 + p.Y * wv._23 + p.Z * wv._33 + wv._43;
	D3DVIEWPORT9 v;
	v.X = vp->X; v.Y = vp->Y; v.Width = vp->Width; v.Height = vp->Height; v.MinZ = vp->MinZ; v.MaxZ = vp->MaxZ;
	D3DXVECTOR3 in(p.X, p.Y, p.Z), at;
	D3DXVec3Project(&at, &in, &v, proj, view, world);
	const float centre = float(vp->Height) * 0.5f;
	out->x = at.x;
	out->y = (at.y - centre) * LT.mScreenShapeScale + centre;
	out->depth = depth;
	out->radius = depth > 0.f ? u->GetRadius() * kCePlayUnitSightBody * proj->_11 * float(vp->Width) * 0.5f / depth : 0.f;
	const float dx = p.X - eye[0], dy = p.Y - eye[1], dz = p.Z - eye[2];
	out->distance = sqrtf(dx * dx + dy * dy + dz * dz);
	return true;
}

// ---- the take key's sight (begin): compiled with fake units by ce_playunit_sight_tests.py
// The unit the take key means: of the units in the picture the player saw (not the Battle Engine, not the one driven now, not one the ground hides), the one nearest the
// reticle at the middle of the screen that may be taken (CePlayUnitAtScreen, CePlayUnitChoose). NULL when none may; *why says why not (the refusal of the unit nearest
// the reticle, or CE_PLAYUNIT_REFUSE_COUNT when no unit was at the reticle) and *family what that unit is. The world is read, nothing in it is changed.
CUnit *Sight(CBattleEngine *engine, const float *eye, float w, float h, CUnit *exclude, int *why, int *family, int *kindOut)
{
	static CePlayUnitSighted seen[512];
	static CUnit *units[512];
	static int families[512];
	int n = 0;
	*why = CE_PLAYUNIT_REFUSE_COUNT;
	*family = CE_PLAYUNIT_FAMILY_OTHER;
	*kindOut = -1;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t && n < 512; t = it.Next())
	{
		if (!(t->GetThingType() & THING_TYPE_UNIT) || t == (CThing *)engine || t == (CThing *)exclude) continue;
		CUnit *u = (CUnit *)t;
		CePlayUnitScreenPlace place = {-1.f, -1.f, 0.f, 0.f, 0.f};
		float score = -1.f;
		if (!ScreenPlace(u, eye, &place) || !CePlayUnitAtScreen(place, w, h, kCePlayUnitSightReach, &score) || score > kCePlayUnitSightReticle) continue;
		// a unit the ground hides (behind a hill, over a ridge) is not in sight: the line from the eye to a point a little above its place must clear the terrain
		const FVector p = u->GetPos();
		const float above[3] = {p.X, p.Y, p.Z - kCePlayUnitSightLift};
		if (!CePlayUnitLineClear(eye, above, kCePlayUnitSightSamples, kCePlayUnitSightTolerance, TerrainHeight, NULL)) continue;
		const CePlayUnitFacts f = Facts(u, engine);
		seen[n].angle = score;
		seen[n].refusal = CePlayUnitRefusalFor(f);
		families[n] = f.family;
		units[n] = u;
		++n;
	}
	const int chosen = CePlayUnitChoose(seen, n, why);
	if (chosen >= 0)
	{
		*kindOut = CePlayUnitClassFor(Facts(units[chosen], engine));
		*family = families[chosen];
		return units[chosen];
	}
	// the unit nearest the reticle, whose refusal the player is told
	int nearest = -1;
	for (int i = 0; i < n; ++i)
		if (nearest < 0 || seen[i].angle < seen[nearest].angle) nearest = i;
	if (nearest >= 0) *family = families[nearest];
	return NULL;
}
// ---- the take key's sight (end)

// The frame's size for the sight: the viewport the engine drew the last frame into.
void FrameSize(float *w, float *h)
{
	const CViewport *vp = ENGINE.GetViewportForViewpoint(0);
	*w = vp ? float(vp->Width) : 0.f;
	*h = vp ? float(vp->Height) : 0.f;
}

#ifdef CE_PLAYUNIT_TEST
// The press as the player saw it: the eye of the drawn view, and where the chosen unit and the friendly units nearest the middle are on the screen, in pixels of the frame,
// so that a picture taken at the press can be set beside it.
void NoteSight(const char *what, const float *eye, CUnit *chosen, CBattleEngine *engine)
{
	if (!gOut) return;
	float w = 0.f, h = 0.f;
	FrameSize(&w, &h);
	CCamera *cam = ENGINE.GetCameraForViewpoint(0);
	const FVector ce = cam ? cam->GetPos() : FVector(0.f, 0.f, 0.f);
	const FVector cf = cam ? cam->GetOrientation().GetColumnY() : FVector(0.f, 0.f, 0.f);
	Note("%s view eye %.1f %.1f %.1f frame %.0f %.0f camera %.1f %.1f %.1f forward %.3f %.3f %.3f", what, eye[0], eye[1], eye[2], w, h, ce.X, ce.Y, ce.Z, cf.X, cf.Y, cf.Z);
	int listed = 0;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t && listed < 16; t = it.Next())
	{
		if (!(t->GetThingType() & THING_TYPE_UNIT) || t == (CThing *)engine) continue;
		CUnit *u = (CUnit *)t;
		CePlayUnitScreenPlace place = {-1.f, -1.f, 0.f, 0.f, 0.f};
		float score = -1.f;
		if (!ScreenPlace(u, eye, &place) || !CePlayUnitAtScreen(place, w, h, kCePlayUnitSightReach, &score)) continue;
		if (u != chosen && (score > 0.5f || u->GetAllegiance() != engine->GetAllegiance())) continue;
		const FVector p = u->GetPos();
		Note("%s unit %d%s at %.1f %.1f %.1f screen %.0f %.0f radius %.0f depth %.1f score %.3f", what, (int)u->GetThingNumber(), u == chosen ? " (chosen)" : "", p.X, p.Y, p.Z,
			 place.x, place.y, place.radius, place.depth, score);
		++listed;
	}
}
#endif

// ---- the take-over ------------------------------------------------------------------------------------------------------------------

void ClearInput()
{
	memset(&gIn, 0, sizeof(gIn));
	gFootZoomIn = gFootZoomOut = false;
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

// The look camera's pose: the orientation is the player's look alone; the eye hangs behind and above the unit's place on the look's line (ce_playunit_look.h) and never
// goes into the ground.
const float kLookClearance = 1.f;		// how far above the ground the eye stays (world units)
const float kLookStartPitch = 0.12f;	// a take starts looking a little down, along the unit's heading
const unsigned kLookStickQuiet = 15;	// ticks the mouse must be still before the yaw actions turn the look (the game's filter dies away in about ten)
const float kLookTickSeconds = 0.05f;	// one update of the game

FMatrix LookOrientation()
{
	float r[3], f[3], d[3];
	CePlayUnitLookAxes(gLookYaw, gLookPitch, r, f, d);
	// the engine's matrices are built from rows: the camera's axes are the columns
	return FMatrix(FVector(r[0], f[0], d[0]), FVector(r[1], f[1], d[1]), FVector(r[2], f[2], d[2]));
}

// Model-relative view positions are private to the currently possessed unit.
// The camera rotates directly; only the near/far view rail is eased.
CePlayUnitCameraRig gUnitViewRig = {};
CePlayUnitCameraState gUnitViewState = {};
bool gUnitViewMade = false, gUnitViewHideBody = false;
unsigned long long gUnitViewFrame = 0, gUnitViewNs = 0;
unsigned long long ViewNs();

void ResetUnitView() {
    gUnitViewRig = CePlayUnitCameraRig();
    CePlayUnitCameraReset(&gUnitViewState, 0, kCePlayUnitCameraWide);
    gUnitViewMade = gUnitViewHideBody = false;
    gUnitViewFrame = gUnitViewNs = 0;
}

bool MakeUnitView(CUnit *unit) {
    if (!gDriving || gViewMode != kViewLook || !unit) return false;
    const size_t identity = reinterpret_cast<size_t>(unit);
    if (gUnitViewMade && gUnitViewState.identity == identity) return gUnitViewRig.valid;
    const CBoundingBox *box = unit->GetBoundingBox();
    if (!box || !box->mValid) return false;
    const float scale = unit->GetRenderScale();
    if (!(scale > 0.0f)) return false;
    const float origin[3] = {box->mOrigin.X, box->mOrigin.Y, box->mOrigin.Z};
    const float axes[3] = {box->mAxes.X, box->mAxes.Y, box->mAxes.Z};
    float lo[3], hi[3], eye[3];
    for (int i = 0; i != 3; ++i) {
        lo[i] = (origin[i] - axes[i]) * scale;
        hi[i] = (origin[i] + axes[i]) * scale;
    }
    const bool haveEye = gOnFoot && CePlayUnitCameraFootEye(lo, hi, eye);
    if (!CePlayUnitCameraBuild(&gUnitViewRig, lo, hi,
            gOnFoot ? CePlayUnitCameraFootLaw() : CePlayUnitCameraVehicleLaw(),
            haveEye ? eye : NULL)) return false;
    // every unit starts in the wide view: a soldier whole in the picture, over his right shoulder, the reticle clear beside him (Close cuts him at the knees)
    CePlayUnitCameraReset(&gUnitViewState, identity, kCePlayUnitCameraWide);
    gUnitViewMade = true;
    gUnitViewHideBody = false;
    gUnitViewFrame = gUnitViewNs = 0;
    return true;
}

FVector ClampUnitViewGround(FVector eye) {
    FVector ground = eye;
	const float floor = MAP.Collide(ground) - (gOnFoot ? (gUnitViewRig.span * 0.10f) : kLookClearance);
	if (eye.Z > floor) eye.Z = floor;
	return eye;
}

bool UnitViewPose(const FVector &place, CePlayUnitCameraPose &pose) {
    CUnit *unit = Held();
    if (!MakeUnitView(unit)) return false;
    const unsigned long long frame = PortPresentCount();
    if (frame != gUnitViewFrame || !gUnitViewNs) {
        const unsigned long long now = ViewNs();
        const float dt = gUnitViewNs && now >= gUnitViewNs
            ? float(double(now - gUnitViewNs) * 1.0e-9) : 0.0f;
        CePlayUnitCameraAdvance(&gUnitViewState, gUnitViewRig,
            reinterpret_cast<size_t>(unit), GAME.IsPaused() ? 0.0f : dt);
        gUnitViewFrame = frame;
        gUnitViewNs = now;
    }
    const float at[3] = {place.X, place.Y, place.Z};
    if (!CePlayUnitCameraPoseFor(gUnitViewState, gUnitViewRig, at,
            gLookYaw, gLookPitch, NULL, NULL, &pose)) return false;
    const FVector eye = ClampUnitViewGround(FVector(pose.eye[0], pose.eye[1], pose.eye[2]));
    pose.eye[0] = eye.X; pose.eye[1] = eye.Y; pose.eye[2] = eye.Z;
    gUnitViewHideBody = gOnFoot && pose.hideBody;
    return true;
}

bool UnitViewHidesBody() {
    return gUnitViewMade && gUnitViewRig.valid ? gUnitViewHideBody : gFootFirstPerson;
}

CePlayUnitLookLaw UnitLookLaw() {
    CePlayUnitLookLaw law = gOnFoot ? CePlayUnitFootLook() : CePlayUnitTankLook();
    if (gOnFoot) { law.pitchUp = -1.30f; law.pitchDown = 1.30f; }
    return law;
}

const char *UnitViewName(int view) {
    if (view == kCePlayUnitCameraFirst) return "First person";
    return view == kCePlayUnitCameraClose ? "Close shoulder" : "Wide shoulder";
}

FVector FootAimEye(const FVector &place)
{
    const float at[3] = {place.X, place.Y, place.Z};
    float point[3];
    CePlayUnitFootFirstEye(at, point);
    FVector eye(point[0], point[1], point[2]), ground(eye);
    const float floor = MAP.Collide(ground) - 0.25f;
    if (eye.Z > floor) eye.Z = floor;
    return eye;
}

FVector LookEye(const FVector &place)
{
    CePlayUnitCameraPose pose;
    if (UnitViewPose(place, pose)) return FVector(pose.eye[0], pose.eye[1], pose.eye[2]);
	const float at[3] = {place.X, place.Y, place.Z};
	float eye[3];
	if (gOnFoot && gFootFirstPerson) return FootAimEye(place);
	CePlayUnitLookEye(at, gLookYaw, gLookPitch, (gOnFoot ? CePlayUnitFootLook() : CePlayUnitTankLook()), eye);
	if (gOnFoot) { eye[0] += 0.42f*cosf(gLookYaw); eye[1] += 0.42f*sinf(gLookYaw); }
	FVector p(eye[0], eye[1], eye[2]);
	FVector ground = p;
	const float floor = MAP.Collide(ground) - (gOnFoot ? 0.3f : kLookClearance);
	if (p.Z > floor) p.Z = floor;
	return p;
}

// The driven unit's camera: the player's look while the view is the look (the default), the game's own third-person camera on the unit otherwise. The game's interpolation
// between updates moves only the unit's place (GetOldPos), never the look, which is the player's at every frame.
class CeLookCamera : public CeTankCamera
{
public:
	explicit CeLookCamera(CUnit *unit) : CeTankCamera(unit) {}
	virtual FVector GetPos() { CThing *t = GetThing(); return t && gViewMode == kViewLook ? LookEye(t->GetPos()) : CeTankCamera::GetPos(); }
	virtual FMatrix GetOrientation() { return gViewMode == kViewLook ? LookOrientation() : CeTankCamera::GetOrientation(); }
	virtual FVector GetOldPos() { CThing *t = GetThing(); return t && gViewMode == kViewLook ? LookEye(t->GetOldPos()) : CeTankCamera::GetOldPos(); }
	virtual FMatrix GetOldOrientation() { return gViewMode == kViewLook ? LookOrientation() : CeTankCamera::GetOldOrientation(); }
};

// The frame's camera for the look: hung from the unit's displayed place, the look as it is now.
void LookShown(CUnit *unit, CGenericCamera *view)
{
	view->mPos = LookEye(unit->GetRenderPos());
	view->mOrientation = LookOrientation();
	view->mZoom = 1.f;
}

#ifdef CE_PLAYUNIT_TEST
// Pointers are identity witnesses only; never dereference objects captured before release.
void ObserveSuccessorMotion()
{
	if (!gReturnedWitness || gTick < gReturnedWitnessTick) return;
	CUnit *witness = gReturnedWitness;
	gReturnedWitness = NULL;
	if (gReturnedWitnessLevel != GAME.GetCurrentLevel()) return;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
	{
		if ((void *)t != (void *)witness) continue;
		if (t->GetThingNumber() != gReturnedWitnessNumber || !(t->GetThingType() & THING_TYPE_UNIT)) return;
		CUnit *unit = (CUnit *)t;
		const FVector pos = unit->GetPos();
		const float dx = gHandbackIssuedDestination.X - gReturnedWitnessStart.X;
		const float dy = gHandbackIssuedDestination.Y - gReturnedWitnessStart.Y;
		const float mx = pos.X - gReturnedWitnessStart.X, my = pos.Y - gReturnedWitnessStart.Y;
		const bool advanced = (mx * dx + my * dy > 0.1f) &&
			((dx - mx) * (dx - mx) + (dy - my) * (dy - my) < dx * dx + dy * dy);
		Note("handback successor motion: active=%d advanced=%d", unit->GetAIState() != AI_OFF, advanced);
		return;
	}
}
// After a hand-back, "H tick x y z ai-state speed move-multiplier" for the returned unit every tick for 400 ticks (found by identity each tick, never read once gone): whether
// the AI carries it on.
CThing *gFollow = NULL;
int gFollowNumber = -1, gFollowLevel = -1;
unsigned gFollowUntil = 0;
void FollowReturned()
{
	if (!gFollow || !gOut) return;
	if (gTick > gFollowUntil || gFollowLevel != GAME.GetCurrentLevel()) { gFollow = NULL; return; }
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
	{
		if (t != gFollow || t->GetThingNumber() != gFollowNumber) continue;
		CUnit *u = (CUnit *)t;
		const FVector p = u->GetPos(), v = u->GetVelocity();
		fprintf(gOut, "H %u %.3f %.3f %.3f %d %.3f %.1f\n", gTick, p.X, p.Y, p.Z, (int)u->GetAIState(), sqrtf(v.X * v.X + v.Y * v.Y) * 20.f, u->GetMoveMultiplier());
		return;
	}
	fprintf(gOut, "H %u gone\n", gTick);
	gFollow = NULL;
}
void ObserveHandback()
{
	const bool sameLevel = gLevel == GAME.GetCurrentLevel();
	CUnit *unit = sameLevel ? Held() : NULL;
	if (unit && !unit->IsDying())
	{
		gFollow = unit;
		gFollowNumber = unit->GetThingNumber();
		gFollowLevel = GAME.GetCurrentLevel();
		gFollowUntil = gTick + 400;
	}
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
	if (gOnFoot && unit && unit == gFootObservedUnit)
		Note("foot handback: guide_restored=%d assignment_restored=%d ai_restored=%d",
		    unit->mGuide == gFootObservedGuide, unit->GetSquad() == gFootObservedSquad, unit->GetAIState() == gPrevAI);
	if (gHandbackIssuedPoint && unit)
	{
		const bool kept = unit->mGuide && unit->mGuide->mDestination.X == gHandbackIssuedDestination.X &&
			unit->mGuide->mDestination.Y == gHandbackIssuedDestination.Y && unit->mGuide->mDestination.Z == gHandbackIssuedDestination.Z;
		if (gHandbackExpectStop)
		{
			const FVector at = unit->GetPos();
			const bool stopped = unit->mGuide && unit->mGuide->mDestination.X == at.X &&
				unit->mGuide->mDestination.Y == at.Y && unit->mGuide->mDestination.Z == at.Z;
			Note("handback successor stop: accepted=%d", stopped);
		}
		Note("handback successor point: accepted=%d ai_active=%d", kept, unit->GetAIState() != AI_OFF);
		gReturnedWitness = unit;
		gReturnedWitnessNumber = unit->GetThingNumber();
		gReturnedWitnessLevel = GAME.GetCurrentLevel();
		gReturnedWitnessStart = unit->GetPos();
		gReturnedWitnessTick = gTick + 60;
	}
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

// ---- the soldier's cadence (begin)
// A soldier moves every fourth tick (CInfantryUnit::Init's move multiplier), so a key waited up to two of his moves (eight ticks) before he stepped, and
// as long before he stopped. While he is driven he moves every tick, as a driven tank does (patch 0242); his own cadence comes back at the release.
CInfantryUnit *gFootCadenceUnit = NULL;
float gFootCadence = 0.f;
void FootCadenceTake(CUnit *u)
{
	if (!gOnFoot || !u || !(u->GetThingType() & THING_TYPE_INFANTRY)) return;
	CInfantryUnit *foot = (CInfantryUnit *)u;
	gFootCadenceUnit = foot;
	gFootCadence = foot->mMoveMultiplier;
	foot->mMoveMultiplier = 1.f;
}
void FootCadenceRelease(CUnit *u)
{
	if (gFootCadenceUnit && (CUnit *)gFootCadenceUnit == u && gFootCadence > 0.f) gFootCadenceUnit->mMoveMultiplier = gFootCadence;
	gFootCadenceUnit = NULL;
	gFootCadence = 0.f;
}
// ---- the soldier's cadence (end)

// ---- the direct law's guide (begin) ----------------------------------------------------------------------------------------------------
// While a unit is driven by the direct law its guide is this one; the unit's own guide is kept aside and restored on release. Each update the
// unit's move (CUnit::Move) asks its guide to steer: this one sets the two things the stock guide sets when it follows a path (CGroundVehicleGuide::Unknown3: the
// heading, then a velocity request along its facing), from what the player's keys ask (ce_playunit_rules.h, CePlayUnitTankMotionOf: speed, reverse, steering while
// it moves) eased in and out over a few ticks (CePlayUnitTankEaseLaw). The request is solved from the mover's own rule (CePlayUnitMoverRequest), so the hull moves at
// the eased speed along its facing after its next move; the heading is placed where the hull's own turning (CUnit::Unknown77: a tenth of the gap a move, at most
// its turn rate) gives the eased turn. Like the stock guide
// Player steering sets aside movement orders only while the player still owns the unit.
// weapon's own arming still holds the unit still (CUnit::Unknown76), as the original's units stop to shoot. When the unit is shut down with this guide in it (destroyed,
// the level cleared), the unit deletes it, and it deletes the kept guide with it.

class CeDriveGuide;
CeDriveGuide *gDriveGuide;				// the guide in the driven unit, NULL when there is none (cleared by its destructor)
unsigned gOrdersSetAside;
CePlayUnitTankMotion gMotion;			// what the guide last applied (the probe prints it)
float gFootWalkX = 0.f, gFootWalkY = 0.f;	// the soldier's last walk direction: a glide to a stop keeps it

// Whether the grid lets a unit stand at p: a place off the grid's map is closed.
bool GridOpen(COccupancyGrid *grid, const FVector &p)
{
	if (!grid || !(p.X >= 0.f && p.X < 512.f && p.Y >= 0.f && p.Y < 512.f)) return false;
	return grid->UnknownCell(p) != FALSE;
}

// Walk with the view. The native infantry mover owns terrain and animation. The speed eases toward the soldier's own cruise, the speed the game's own
// infantry guide gives him (CInfantryGuide asks a fifth of his top speed a move, which the mover settles at), with the tank's ease law (CePlayUnitTankEaseLaw):
// most of it in two ticks, the walker's glide when the keys are let go. On the ground the eased speed is this tick's velocity along the walk, and the request
// is the one that holds it through his next move (CePlayUnitMoverRequest).
void WalkFoot(CUnit *unit)
{
	float x, y;
	const bool moving = CePlayUnitFootDirection(gLookYaw, gApplied.forward, gApplied.back, gApplied.turn, x, y);
	unit->mUnknown120.mYaw = gLookYaw;
	const FVector pos = unit->GetPos(), v = unit->GetVelocity();
	COccupancyGrid *grid = unit->GetOccupancyGrid();
	const bool blocked = moving && grid && GridOpen(grid, pos) && !GridOpen(grid, pos + FVector(x, y, 0.f));
	const float now = sqrtf(v.X * v.X + v.Y * v.Y);
	if (moving && !blocked) { gFootWalkX = x; gFootWalkY = y; }
	if (!moving && now < 1e-5f) return;
	const float top = unit->Unknown111() / 20.f, friction = unit->Unknown72();
	const float cruise = CePlayUnitMoverCruise(top * 4.f, friction, top);
	const CePlayUnitTankEase &ease = CePlayUnitTankEaseLaw();
	const float speed = blocked ? 0.f : CePlayUnitEaseToward(now, moving ? cruise : 0.f, ease.speedRise, ease.speedFall, ease.settle * top);
	const FVector want(gFootWalkX * speed, gFootWalkY * speed, 0.f);
	FVector from = v;
	if ((unit->IsOnGround() || unit->IsInWater()) && unit->Unknown76())
	{
		from.X = want.X;
		from.Y = want.Y;
		unit->SetVelocity(from);
	}
	float rx, ry;
	if (CePlayUnitMoverRequest(from.X, want.X, friction, 1, rx) && CePlayUnitMoverRequest(from.Y, want.Y, friction, 1, ry))
		unit->SetUnknown14C(FVector(rx, ry, 0.f));
	else if (moving && !blocked)
		unit->SetUnknown14C(FVector(x * top * 4.f, y * top * 4.f, 0.f));
}

// A new AI state, allegiance, or squad can reclaim the unit before the next input tick.
bool AnotherUnitOwner(CUnit *unit)
{
	return unit->GetAllegiance() != gPossessedAllegiance || unit->GetAIState() != AI_OFF ||
		((gOnFoot || gDeliveryAssigned) ? !FootKeepsAssignment(unit) : unit->GetSquad() != NULL);
}

class CeDriveGuide : public CGuide
{
public:
	CGuide *mStock;
	bool mSuccessorOrder = false;
	float mTurn;	// the eased turn, radians a tick, left positive
	CeDriveGuide(CUnit *unit, CGuide *stock) : CGuide(unit), mStock(stock), mTurn(0.f) {}
	virtual ~CeDriveGuide()
	{
		if (gDriveGuide == this) gDriveGuide = NULL;
		if (gFootCadenceUnit && (CUnit *)gFootCadenceUnit == mUnit) { gFootCadenceUnit = NULL; gFootCadence = 0.f; }
		CGuide *stock = mStock;
		mStock = NULL;
		if (stock) delete stock;
	}
	virtual void Unknown3()
	{
		CUnit *u = GetUnit();
		if (u->IsDying()) { u->Stop(); return; }
		u->SetUnknown14C(FVector(0, 0, 0));
		if (gOnFoot) { WalkFoot(u); return; }
		const CePlayUnitLaw &law = gLaw ? *gLaw : CePlayUnitClasses()[CE_PLAYUNIT_CLASS_TANK].law;
		CePlayUnitTankMotion m = CePlayUnitTankMotionOf(u->GetCurrentOrientation().mYaw, gApplied.forward, gApplied.back, gApplied.turn, law.steerLead, law.reverseShare, law.turnSlow);
		const CePlayUnitTankEase &ease = CePlayUnitTankEaseLaw();
		const float yaw = u->GetCurrentOrientation().mYaw;
		const float mult = u->GetMoveMultiplier() > 1.f ? u->GetMoveMultiplier() : 1.f;
		const float rate = u->mUnknown12C.mYaw > 0.f ? u->mUnknown12C.mYaw : 0.f;
		const float steer = gApplied.turn > 1.f ? 1.f : (gApplied.turn < -1.f ? -1.f : gApplied.turn);
		mTurn = CePlayUnitEaseToward(mTurn, -steer * rate, ease.turnRise, ease.turnFall, ease.settle * rate);
		m.yaw = CePlayUnitWrap(yaw + mTurn / (0.1f * mult));
		u->mUnknown120.mYaw = m.yaw;
		const FVector ahead(-sinf(yaw), cosf(yaw), 0.f);
		bool blocked = false;
		if (m.share != 0.f)
		{
			const FVector pos = u->GetPos();
			COccupancyGrid *grid = u->GetOccupancyGrid();
			if (grid)
			{
				const float guarded = CePlayUnitTankGuard(m.share, GridOpen(grid, pos), GridOpen(grid, pos + ahead), GridOpen(grid, pos - ahead));
				blocked = guarded != m.share;
				m.share = guarded;
			}
		}
		gMotion = m;
		const float top = u->Unknown111() / 20.f;
		const float friction = u->Unknown72();
		const float cruise = CePlayUnitMoverCruise(top * 4.f, friction, top);	// the class's own speed under the stock guide's request
		const FVector v = u->GetVelocity();
		const float along = v.X * ahead.X + v.Y * ahead.Y;
		// a closed cell stops the tank at once, as the stock guide does; otherwise the speed eases from what the hull is doing now toward the share of its cruise
		const float speed = blocked ? 0.f : CePlayUnitEaseToward(along, cruise * m.share, ease.speedRise, ease.speedFall, ease.settle * top);
		const FVector want = u->GetOrientation() * FVector(0.f, speed, 0.f);
		const int steps = (int)mult;
		// The mover has already used last tick's request this tick (CGroundUnit::Move runs before CUnit::Move asks the guide), so on the ground, where that
		// request is all that drives the hull, the eased speed is this tick's velocity, and the request is the one that holds it through the next move. In the
		// air (a bump, a fall) the mover's own physics stand and only the request is set.
		const bool grounded = (u->IsOnGround() || u->IsInWater() || !u->ObeyGravity()) && u->Unknown76();
		FVector from = v;
		if (grounded)
		{
			from.X = want.X;
			from.Y = want.Y;
			u->SetVelocity(from);
		}
		float rx, ry;
		if (CePlayUnitMoverRequest(from.X, want.X, friction, steps, rx) && CePlayUnitMoverRequest(from.Y, want.Y, friction, steps, ry))
			u->SetUnknown14C(FVector(rx, ry, 0.f));
		else if (m.share != 0.f)
			u->SetUnknown14C(u->GetOrientation() * FVector(0.f, top * 4.f * m.share, 0.f));
	}
	virtual void GoToPoint(FVector destination, BOOL how)
	{
		if (mStock && AnotherUnitOwner(mUnit))
		{
			mStock->GoToPoint(destination, how);
			mSuccessorOrder = true;
		}
		else ++gOrdersSetAside;
	}
	virtual void Unknown5(FVector) { ++gOrdersSetAside; }
	virtual void Unknown6(FVector) { ++gOrdersSetAside; }
	virtual void Unknown7(FVector) { ++gOrdersSetAside; }
	virtual void Stop()
	{
		if (mStock && AnotherUnitOwner(mUnit))
		{
			mStock->Stop();
			mSuccessorOrder = true;
		}
		mMode = 0; mDestination = mUnit->GetPos(); mUnit->mUnknown14C = FVector(0, 0, 0);
	}
	virtual CNormalSquadUnknownD4 *GetPath() { return mStock ? mStock->GetPath() : NULL; }
};

// The direct law's guide goes into the unit (it keeps the unit's own); false when the unit has no guide of its own, which leaves the first experiment's law.
bool InstallDriveGuide(CUnit *u)
{
	if (gPathDrive || gDriveGuide || !u->mGuide) return false;
	CeDriveGuide *g = new (MEMTYPE_GUIDE) CeDriveGuide(u, u->mGuide);
	u->mGuide = g;
	gDriveGuide = g;
	FootCadenceTake(u);
	gOrdersSetAside = 0;
	memset(&gMotion, 0, sizeof(gMotion));
	return true;
}

// Restore the stock guide. Stop player motion unless a new AI owner already gave it an order.
void RemoveDriveGuide()
{
	CeDriveGuide *g = gDriveGuide;
	if (!g) return;
	gDriveGuide = NULL;
	CUnit *u = g->GetUnit();
	FootCadenceRelease(u);
	if (u && u->mGuide == g)
	{
		u->mGuide = g->mStock;
		g->mStock = NULL;
		if (u->mGuide && !gOnFoot && !g->mSuccessorOrder) u->mGuide->Stop();
	}
	delete g;
}
// ---- the direct law's guide (end) ------------------------------------------------------------------------------------------------------

// The ordinary camera slot owns this camera, just as it owns the player's control view. Do not detach a second saved camera: recreating the preferred control view releases ours.
void PutCameraOn(CUnit *u)
{
	const FVector heading = u->GetOrientation().GetColumnY();
	gLookYaw = CePlayUnitLookYawOf(heading.X, heading.Y);
	gLookPitch = gOnFoot ? 0.08f : kLookStartPitch;
	gMouseQuiet = ~0u;
	gCamera = new (MEMTYPE_CAMERA) CeLookCamera(u);
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
	else if (!strcmp(gHandbackMode, "reassigned") || !strcmp(gHandbackMode, "squad-point"))
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
					if (!strcmp(gHandbackMode, "squad-point"))
					{
						gHandbackIssuedDestination = Ground(unit->GetPos().X + CePlayUnitClasses()[CE_PLAYUNIT_CLASS_TANK].law.reach, unit->GetPos().Y);
						gHandbackIssuedPoint = true;
						unit->GoToPoint(gHandbackIssuedDestination, TRUE);
					}
					changed = unit->GetSquad() == squad;
					break;
				}
			}
	}
	else if (!strcmp(gHandbackMode, "ai") || !strcmp(gHandbackMode, "ai-stop"))
	{
		unit->SetAIState(AI_ON);
		gHandbackIssuedDestination = Ground(unit->GetPos().X + CePlayUnitClasses()[CE_PLAYUNIT_CLASS_TANK].law.reach, unit->GetPos().Y);
		gHandbackIssuedPoint = true;
		unit->GoToPoint(gHandbackIssuedDestination, FALSE);
		if (!strcmp(gHandbackMode, "ai-stop") && unit->mGuide)
		{
			gHandbackExpectStop = true;
			unit->mGuide->Stop();
		}
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

// Safely resume the current assignment without replaying a saved destination.
void ResumeDeliveryAssignment(CUnit *unit)
{
	if (unit && CeFactoryIsVehicle(unit) && unit->GetAIState() == AI_OFF
		&& FootKeepsAssignment(unit) && unit->GetSquad())
		unit->SetAIState(unit->GetSquad()->GetAIState());
}

void ReturnUnitControl()
{
	const bool sameLevel = gLevel == GAME.GetCurrentLevel();
	CUnit *unit = sameLevel ? Held() : NULL;
	if (gDeliveryAssigned)
	{
		ResumeDeliveryAssignment(unit);
		gPreviousSquad = NULL;
		gDeliveryAssigned = false;
		return;
	}
	if (gOnFoot)
	{
		if (unit && CePlayUnitFootReturnsToAI(!unit->IsDying() && unit->GetLife() > 0.f,
		    unit->GetAllegiance() == gPossessedAllegiance, unit->GetAIState() == AI_OFF,
		    FootKeepsAssignment(unit), !unit->GetSquad()))
		{
			unit->SetUnknown14C(FVector(0.f, 0.f, 0.f));
			unit->SetAIState(gPrevAI);
		}
		gPreviousSquad = NULL;
		gPreviousSquadNumber = 0;
		return;
	}
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

// Gives the controls back. notice, when there is one, is what the player is told (with the return cue), hint a second line under it; a release the player did not cause
// and needs no word on (a new level, the level's end, a switch to the next unit) passes NULL. A release while another view holds the screen (a cutscene, a mission camera)
// says it when the player's own view is back (gBackPending).
void Release(const char *why, const char *notice = NULL, const char *hint = NULL)
{
	if (!gDriving) return;
	Log("giving the controls back (%s)", why);
	CePlayUnitPrompt(NULL);
	if (notice)
	{
		CePlayUnitNotice(notice, hint);
		CFrontEnd::PlaySound(FES_BACK);
	}
#ifdef CE_PLAYUNIT_TEST
	CCONTROLLER *observedController = GAME.GetController(0);
	Note("release state: paused=%d driver_on_top=%d player_on_top=%d", GAME.IsPaused(),
		observedController && observedController->GetToControl() == Pilot(),
		observedController && observedController->GetToControl() == GAME.GetPlayer(0));
#endif
	gDriving = false;
	Pilot()->mDriving = false;
	RemoveDriveGuide();
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
	gReturnedAt = gTick;
	Note("camera lifetime: made=%u destroyed=%u restored=%u pending=%d engine_up=%d", gCameraMade, gCameraDestroyed,
		gCameraRestored, gCamera != NULL, engine && engine->GetIsPoweredUp());
#endif
	gOnFoot = false; gDeliveryAssigned = false;
	gFootFirstPerson = false;
	gFootRenderedView = NULL;
	gMoving = false;
	gFireState = CE_PLAYUNIT_FIRE_NONE;
	ClearInput();
}

// The name of the key the player's Battle Engine morphs with (the one that gives the controls back), as a player reads it on a key: the first keyboard binding of Morph.
void UnitKeyName(char *out, int n, int action, const char *fallback)
{
	snprintf(out, n, "%s", fallback);
	for (SControlBinding *b = CONTROL_BINDINGS; b->mId != -1; b++)
	{
		if (b->mId != action) continue;
		for (int i = 0; i < 2; i++)
		{
			const SControlInput &in = b->mInput[i];
			if (in.mType == MOUSE_BUTTON_ON || in.mType == MOUSE_BUTTON_ONCE ||
            in.mType == MOUSE_BUTTON_RELEASE) {
            const char *mouse[] = {"Left mouse", "Right mouse", "Middle mouse",
                                   "Wheel up", "Wheel down"};
            if (in.mKey >= 0 && in.mKey < (int)(sizeof(mouse) / sizeof(mouse[0]))) {
                snprintf(out, n, "%s", mouse[in.mKey]);
                return;
            }
        }
        if (in.mPad == -1 || (in.mType != KEY_ON && in.mType != KEY_ONCE && in.mType != KEY_RELEASE)) continue;
			const char *name = NULL;
			switch (in.mKey)
			{
			case DIK_SPACE: name = "Space"; break;
        case DIK_UP: name = "Up arrow"; break;
        case DIK_DOWN: name = "Down arrow"; break;
        case DIK_LEFT: name = "Left arrow"; break;
        case DIK_RIGHT: name = "Right arrow"; break;
        case DIK_ESCAPE: name = "Escape"; break;
        case DIK_MINUS: name = "-"; break;
        case DIK_EQUALS: name = "="; break;
        case DIK_COMMA: name = ","; break;
        case DIK_PERIOD: name = "."; break;
        case DIK_SLASH: name = "/"; break;
        case DIK_SEMICOLON: name = ";"; break;
        case DIK_APOSTROPHE: name = "'"; break;
        case DIK_LBRACKET: name = "["; break;
        case DIK_RBRACKET: name = "]"; break;
        case DIK_BACKSLASH: name = "Backslash"; break;
        case DIK_GRAVE: name = "Backtick"; break;
        case DIK_HOME: name = "Home"; break;
        case DIK_END: name = "End"; break;
        case DIK_INSERT: name = "Insert"; break;
        case DIK_DELETE: name = "Delete"; break;
        case DIK_PRIOR: name = "Page Up"; break;
        case DIK_NEXT: name = "Page Down"; break;
			case DIK_TAB: name = "Tab"; break;
			case DIK_RETURN: name = "Enter"; break;
			case DIK_BACK: name = "Backspace"; break;
			case DIK_LSHIFT: name = "Left Shift"; break;
			case DIK_RSHIFT: name = "Right Shift"; break;
			case DIK_LCONTROL: name = "Left Ctrl"; break;
			case DIK_RCONTROL: name = "Right Ctrl"; break;
			case DIK_LMENU: name = "Left Alt"; break;
			case DIK_RMENU: name = "Right Alt"; break;
			default: break;
			}
			if (name) { snprintf(out, n, "%s", name); return; }
			if (in.mVKey >= VK_F1 && in.mVKey <= VK_F12) {
            snprintf(out, n, "F%d", in.mVKey - VK_F1 + 1);
            return;
        }
        if (in.mVKey >= '0' && in.mVKey <= '9') {
            snprintf(out, n, "%c", (char)in.mVKey);
            return;
        }
        if ((in.mVKey >= 'A' && in.mVKey <= 'Z') || (in.mVKey >= '0' && in.mVKey <= '9')) { snprintf(out, n, "%c", (char)in.mVKey); return; }
		}
	}
}
void MorphKeyName(char *out, int n)
{
    UnitKeyName(out, n, BUTTON_MECH_MORPH, "Morph");
}

// Whether the take key is the module's: the player has not bound it to one of the game's own controls.
bool TakeKeyFree()
{
	return !PortCeActionConflict(CeControlAction::TakeUnit, (int)TakeKey());
}
void UnitViewHelp(char *out, int n)
{
    char closer[24], wider[24], back[24];
    UnitKeyName(closer, sizeof(closer), BUTTON_MECH_CHANGE_ZOOM_IN, "Zoom In");
    UnitKeyName(wider, sizeof(wider), BUTTON_MECH_CHANGE_ZOOM_OUT, "Zoom Out");
    MorphKeyName(back, sizeof(back));
    const char *views = gOnFoot ? "Wide/Close/First" : "Wide/Close";
    if (TakeKeyFree())
        snprintf(out, n, "%s/%s: %s | %s: Return | %s: Take", closer, wider,
                 views, back, PortCeActionName(CeControlAction::TakeUnit));
    else
        snprintf(out, n, "%s/%s: %s | %s: Return", closer, wider, views, back);
}

// The standing prompt while a unit is held: "AV-14B Sabre Pulse Tank | Space: Return | Wheel: Wide/Close". The two zoom keys are named once when they are one
// control's two ways (a wheel, a pad's shoulder pair: the same first word), else both.
void UnitPromptLine(char *out, int n, const char *name)
{
    char closer[24], wider[24], back[24], zoom[52];
    UnitKeyName(closer, sizeof(closer), BUTTON_MECH_CHANGE_ZOOM_IN, "Zoom In");
    UnitKeyName(wider, sizeof(wider), BUTTON_MECH_CHANGE_ZOOM_OUT, "Zoom Out");
    MorphKeyName(back, sizeof(back));
    const char *gap = strchr(closer, ' ');
    const size_t word = gap ? (size_t)(gap - closer) : 0;
    if (word && !strncmp(closer, wider, word) && wider[word] == ' ') snprintf(zoom, sizeof(zoom), "%.*s", (int)word, closer);
    else snprintf(zoom, sizeof(zoom), "%s/%s", closer, wider);
    snprintf(out, n, "%s | %s: Return | %s: %s", name, back, zoom, gOnFoot ? "Wide/Close/First" : "Wide/Close");
}

bool Take(CPlayer *player, CBattleEngine *engine, CCONTROLLER *controller, CUnit *u, int kind)
{
#ifdef CE_PLAYUNIT_TEST
	if (!strcmp(gHandbackMode, "last") && IsNormal(u->GetSquad()))
		TestLeaveOneMember((CNormalSquad *)u->GetSquad(), u);
#endif
	if (kind == CE_PLAYUNIT_CLASS_INFANTRY && (gPathDrive || gDriveGuide || !IsOrdinaryFoot(u, u->GetSquad()))) return false;
	CeCmdUnitOrderRelease(u);
	gOnFoot = kind == CE_PLAYUNIT_CLASS_INFANTRY;
	gDeliveryAssigned = !gOnFoot && CeFactoryIsVehicle(u) && CeCmdDeliveredSquad() >= 0;
	gFootFirstPerson = false;
	gFootRenderedView = NULL;
	gLaw = &CePlayUnitClasses()[kind].law;
	gFireState = CE_PLAYUNIT_FIRE_NONE;
	gLastNotedState = -1;
	gUnit = u;
	gThing = u->GetThingNumber();
	gPrevAI = u->GetAIState();
	gPreviousSquad = u->GetSquad();
	gPreviousSquadNumber = gPreviousSquad ? gPreviousSquad->GetThingNumber() : -1;
	gPossessedAllegiance = u->GetAllegiance();
	gParkedEngine = engine;
	gParkedEngineNumber = engine->GetThingNumber();
#ifdef CE_PLAYUNIT_TEST
	gHandbackIssuedPoint = gHandbackExpectStop = false;
	gObservedOriginalSquad = u->GetSquad();
	gObservedOriginalSquadNumber = gObservedOriginalSquad ? gObservedOriginalSquad->GetThingNumber() : -1;
	gObservedAllegiance = u->GetAllegiance();
	if (gOnFoot) { gFootObservedUnit = u; gFootObservedGuide = u->mGuide; gFootObservedSquad = u->GetSquad(); }
#endif
	ClearInput();
	gMoving = false;
	gDesired = gLastAngle = 0.f;
	gHaveTime = false;
	gLastIssue = gTick;
	gFired = gReasserts = 0;
#ifdef CE_PLAYUNIT_TEST
	gKitRounds = 0;
	gKitImpactSeen = false;
#endif
#ifdef CE_PLAYUNIT_TEST
	if (!gKeepAI)
#endif
	{
		u->SetAIState(AI_OFF);
		if (CSquad *squad = gDeliveryAssigned ? NULL : u->GetSquad())
			if (IsNormal(squad)) ((CNormalSquad *)squad)->Unknown4e6f70(u);
		if (!gOnFoot) u->GoToPoint(u->GetPos(), FALSE);
		InstallDriveGuide(u);
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
	const char *name = u->GetUnitData() && u->GetUnitData()->mName ? u->GetUnitData()->mName : (gOnFoot ? "soldier" : "tank");
	Log("driving unit %d, a %s", (int)gThing, name);
	// what the player is told: the unit, and the way back and on (the Battle Engine's own Morph key, as the player has it bound)
	char first[96], second[96], key[24];
	snprintf(first, sizeof(first), gOnFoot ? "On foot: %s" : "You are driving: %s", name);
	MorphKeyName(key, sizeof(key));
	if (gViewMode == kViewLook)
        UnitViewHelp(second, sizeof(second));
    else if (gOnFoot)
        snprintf(second, sizeof(second), "Zoom In: First Person, %s: Return", key);
    else if (!TakeKeyFree())
		snprintf(second, sizeof(second), "%s: Return to Battle Engine", key[0] ? key : "Morph");
	else if (strcmp(key, "Morph")) snprintf(second, sizeof(second), "%s: Return   %s: Take Another", key, PortCeActionName(CeControlAction::TakeUnit));
	else snprintf(second, sizeof(second), "Morph: Return   %s: Take Another", PortCeActionName(CeControlAction::TakeUnit));
	CePlayUnitNotice(first, second, true);
	// the line that stays while the unit is held: what it is first, then the way back, then the views
	char standing[160];
	UnitPromptLine(standing, sizeof(standing), name);
	CePlayUnitPrompt(standing);
	CFrontEnd::PlaySound(FES_SELECT);
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

// Why a press of the take key cannot be answered now, in a sentence for the player, or NULL when nothing needs saying (a cutscene, a mission camera or another view
// holds the screen: the press is let go quietly).
const char *NotReadySentence()
{
	if (GAME.GetGameState() != GAME_STATE_PLAYING || GAME.IsPaused() || GAME.IsFreeCameraOn(0) || GAME.mRemoteCamera || GAME.mOldRemoteCamera) return NULL;
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
	if (!engine || engine->IsDying() || engine->GetLife() <= 0.f) return NULL;
	CCONTROLLER *controller = GAME.GetController(0);
	if (!controller || controller->GetToControl() != player) return NULL;		// a menu (the level's end, a message) has the controls
	CCamera *camera = GAME.GetCurrentCamera(0);
	if (!camera || !camera->GetShowHUD()) return NULL;
	if (engine->GetState() != BATTLE_ENGINE_STATE_WALKER || !engine->IsWalkerOnGroundOrObject()) return "Land your Battle Engine first.";
	if (!engine->GetIsPoweredUp()) return "Your Battle Engine is not ready.";
	return NULL;
}

// The gate, filled from the game and the environment.
CePlayUnitGate FillGate(bool optionOn)
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
	return g;
}

bool Allowed(bool optionOn)
{
	const CePlayUnitGate g = FillGate(optionOn);
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
	gStockFire = Present("BEA_PORT_PLAYUNIT_STOCKFIRE") && !strcmp(getenv("BEA_PORT_PLAYUNIT_STOCKFIRE"), "1");
	gTakeOnce = Present("BEA_PORT_PLAYUNIT_TAKE_ONCE") && !strcmp(getenv("BEA_PORT_PLAYUNIT_TAKE_ONCE"), "1");
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
	gPathDrive = getenv("BEA_PORT_TANK_DRIVE") && !strcmp(getenv("BEA_PORT_TANK_DRIVE"), "path");
	gViewMode = kViewLook;
	if (const char *v = getenv("BEA_PORT_TANK_VIEW"))
		gViewMode = !strcmp(v, "rigid") ? kViewRigid : !strcmp(v, "stock") ? kViewStock : !strcmp(v, "smooth") ? kViewSmooth : kViewLook;
	// Wanted at all, and allowed here: the ini is not even read where a replay path refuses the option (the fences that need no game object are asked first).
	const bool fenced = FencedEnv() && !gProof;
	// One of the unfinished features: ce.ini's play_unit=1 under [Experimental], or the older play_unit=tank (read once in ce.cpp).
	gWanted = !fenced && PortCeExperimental("play_unit");
#ifdef CE_PLAYUNIT_TEST
	gObserve = gProbePath != NULL;
#endif
}

void Reset()
{
	gTick = 0;
	gTakePressed = false;
	gSwitchUnit = NULL;
	gSwitchThing = -1;
	gSwitchWait = 0;
	gBackPending = false;
	gUnit = NULL;
	gOnFoot = false; gDeliveryAssigned = false;
	gFootFirstPerson = false;
	gFootRenderedView = NULL;
	gThing = -1;
	gPreviousSquad = NULL;
	gPreviousSquadNumber = -1;
	gParkedEngine = NULL;
	gParkedEngineNumber = -1;
	gMoving = false;
	ClearInput();
#ifdef CE_PLAYUNIT_TEST
	gLooks = 0;
	gNextLook = kFirstLook;
	gObserveDone = false;
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

#ifdef CE_PLAYUNIT_TEST
// The aim probe (test build): each driving tick, "AIM tick valid ax ay az turret-yaw aim-yaw hull-yaw ai-target fire-state rx ry rz ground" (r: the
// reticle's own point, ground 1 when the view's ray met the terrain), the turret's world heading (the hull's plus
// the turret's own, CUnit::Move's mUnknownE0) beside the heading from the muzzle to the player's aim point. And the unit's rounds, by identity: a round
// whose launcher is the driven unit is followed, "R tick number x y z" each tick, and "RI tick number x y z" gives the last place it
// was seen when it is gone (where it hit). The identities are compared only, never read once gone.
struct ProbeRound { CThing *thing; int number; FVector last; FVector aim; };
FVector gProbeReticle; bool gProbeReticleGround;
ProbeRound gProbeRounds[32];
void ProbeAim(CUnit *u)
{
	const FVector muzzle = (u->GetOrientation() * u->mUnknown1F8) + u->GetPos();
	const float aimYaw = gAimValid ? (gAimPoint - muzzle).Azimuth() : 0.f;
	const bool aiTarget = u->mAI && u->mAI->mTarget.ToRead() != NULL;
	fprintf(gOut, "AIM %u %d %.3f %.3f %.3f %.4f %.4f %.4f %d %d %.3f %.3f %.3f %d\n", gTick, gAimValid ? 1 : 0, gAimPoint.X, gAimPoint.Y, gAimPoint.Z,
			CePlayUnitWrap(u->Unknown92() + u->mUnknownE0), aimYaw, u->Unknown92(), aiTarget ? 1 : 0, gFireState, gProbeReticle.X, gProbeReticle.Y, gProbeReticle.Z,
			gProbeReticleGround ? 1 : 0);
	bool seen[32] = {};
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *th = it.First(); th; th = it.Next())
	{
		if (!(th->GetThingType() & THING_TYPE_AMMUNITION)) continue;
		int slot = -1, empty = -1;
		for (int i = 0; i < 32; ++i)
		{
			if (gProbeRounds[i].thing == th && gProbeRounds[i].number == th->GetThingNumber()) slot = i;
			else if (!gProbeRounds[i].thing && empty < 0) empty = i;
		}
		const FVector pos = th->GetPos();
		if (slot < 0)
		{
			if (empty < 0 || ((CRound *)th)->mUnknownEC.ToRead() != (CThing *)u) continue;
			slot = empty;
			gProbeRounds[slot].thing = th;
			gProbeRounds[slot].number = th->GetThingNumber();
			gProbeRounds[slot].aim = gAimPoint;
			++gKitRounds;
		}
		seen[slot] = true;
		gProbeRounds[slot].last = pos;
		fprintf(gOut, "R %u %d %.3f %.3f %.3f\n", gTick, gProbeRounds[slot].number, pos.X, pos.Y, pos.Z);
	}
	for (int i = 0; i < 32; ++i)
		if (gProbeRounds[i].thing && !seen[i])
		{
			const FVector &l = gProbeRounds[i].last;
			fprintf(gOut, "RI %u %d %.3f %.3f %.3f\n", gTick, gProbeRounds[i].number, l.X, l.Y, l.Z);
			gKitImpact = l;
			gKitLaunchAim = gProbeRounds[i].aim;
			gKitImpactSeen = true;
			gProbeRounds[i].thing = NULL;
		}
}
#endif

unsigned Rounds()
{
	unsigned n = 0;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
		if (t->GetThingType() & THING_TYPE_AMMUNITION) ++n;
	return n;
}

// Units not on the driven unit's side, alive, within radius of it: what a free-fire note says about whether an enemy was there to shoot at.
unsigned EnemiesWithin(CUnit *u, float radius)
{
	unsigned n = 0;
	const FVector p = u->GetPos();
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
	{
		if (!(t->GetThingType() & THING_TYPE_UNIT) || t == (CThing *)u) continue;
		CUnit *o = (CUnit *)t;
		if (o->IsDying() || o->GetLife() <= 0.f || o->GetAllegiance() == gPossessedAllegiance) continue;
		const FVector d = o->GetPos() - p;
		if (d.X * d.X + d.Y * d.Y + d.Z * d.Z < radius * radius) ++n;
	}
	return n;
}

unsigned gHideCalls, gHideTrue, gHideFail[8];	// the probe's "HIDE" line: calls for the driven soldier, true answers, the first clause that failed
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
	if (gDriving)
	{
		const FVector v = u->GetVelocity();
		fprintf(gOut, "V %u %c %.3f %.4f %.4f %.3f %.2f %.2f %.2f %u %d\n", gTick, gDriveGuide && u->mGuide == gDriveGuide ? 'D' : 'S', sqrtf(v.X * v.X + v.Y * v.Y),
				u->GetCurrentOrientation().mYaw, gMotion.yaw, gMotion.share, gApplied.forward, gApplied.back, gApplied.turn, gOrdersSetAside, (int)u->Unknown76());
		ProbeAim(u);
		if (gOnFoot && gTick % 20 == 0)
		{
			fprintf(gOut, "HIDE %u calls %u true %u fail mirror %u body %u cam %u view %u engine %u game %u level %u free %u first %d\n", gTick, gHideCalls, gHideTrue,
				gHideFail[0], gHideFail[1], gHideFail[2], gHideFail[3], gHideFail[4], gHideFail[5], gHideFail[6], gHideFail[7], gFootFirstPerson ? 1 : 0);
			gHideCalls = gHideTrue = 0;
			memset(gHideFail, 0, sizeof(gHideFail));
		}
		if (gOnFoot && gTick % 5 == 0)
		{
			// what stands near a soldier while he is driven (test build): "NEAR tick class type distance dx dy dz", things within 4 units but himself
			const FVector at = u->GetPos();
			ListIterator<CThing> around(&WORLD.GetThingNB());
			for (CThing *th = around.First(); th; th = around.Next())
			{
				if (th == (CThing *)u) continue;
				const FVector d = th->GetPos() - at;
				const float dist = sqrtf(d.X * d.X + d.Y * d.Y + d.Z * d.Z);
				if (dist < 4.f) fprintf(gOut, "NEAR %u %s %08lx %.3f %.2f %.2f %.2f\n", gTick, th->_GetClassName(), (unsigned long)th->GetThingType(), dist, d.X, d.Y, d.Z);
			}
		}
	}
	if (gOnFoot && gDriving && gCamera)
	{
		const FVector at = u->GetPos(), eye = gCamera->GetPos();
		Note("foot view: eye_above=%.4f horizontal=%.4f assignment_kept=%d",
		   at.Z-eye.Z, hypotf(eye.X-at.X,eye.Y-at.Y), FootKeepsAssignment(u));
        if (gFootFirstPerson)
        {
            const FVector dir = gCamera->GetOrientation().GetColumnY();
            const FVector rayEnd = eye + dir * 12.f;
            CLine ray(eye, rayEnd);
            CWorldLineColReport nearby;
            const bool contact = WORLD.FindFirstThingToHitLine(ray, u, &nearby) != kCollideNothing;
            FVector ground = eye;
            const float clearance = MAP.Collide(ground) - eye.Z;
            const FVector aimEye = FootAimEye(at);
            bool nearbySolid = false;
            for (int height = 0; height < 2 && !nearbySolid; ++height)
            {
                FVector sample = eye;
                if (height) sample.Z += 0.85f;
                for (int side = 0; side < 8 && !nearbySolid; ++side)
                {
                    const float bearing = 6.28318531f * float(side)/8.f;
                    CLine around(sample, sample + FVector(8.f*cosf(bearing), 8.f*sinf(bearing), 0.f));
                    CWorldLineColReport found;
                    nearbySolid = WORLD.FindFirstThingToHitLine(around, u, &found) != kCollideNothing &&
                        found.mHitThing && found.mHitThing->GetThingType() != THING_TYPE_UNIT;
                }
            }
            Note("first view: centered=%d clearance=%.4f line_contact=%d world_object=%d ground_contact=%d common_origin=%d solid_nearby=%d",
                hypotf(eye.X - at.X, eye.Y - at.Y) < 0.01f, clearance, contact,
                contact && nearby.mHitThing != NULL, contact && nearby.mHitType == kCollideGround,
                (aimEye - eye).MagnitudeSq() < 0.001f, nearbySolid);
        }
	}
	fflush(gOut);
}
#endif

// ---- free fire (begin) --------------------------------------------------------------------------------------------------------------
// Held fire at the ground point under the middle of the view, with no enemy needed, through the unit's own weapon calls: the aim call the stock handler makes (here with no target
// and the point), then its second phase, arm (the unit stops to shoot, as in the original), or its first, discharge once armed. The weapon's own readiness, recovery and arming
// are the game's; the module adds no rate of fire and no damage. The range is the weapon's own: a point beyond it (or the sky) is fired at as far as the weapon reaches on the
// same line, a point inside its minimum is not fired at; the line of fire is the terrain alone.

float TerrainHeight(void *, float x, float y)
{
	FVector p(x, y, 0);
	return MAP.Collide(p);
}

// The weapon that fires: the unit's current one or, where the game has cleared it, the first that is ready with a range. It is made the current one only when assign says so.
CWeapon *FireWeapon(CUnit *u, bool assign)
{
	if (u->mCurrentWeapon) return u->mCurrentWeapon;
	ListIterator<CWeapon> weapons(&u->mWeapons);
	for (CWeapon *w = weapons.First(); w; w = weapons.Next())
		if (w->mMode && w->ReadyToFire())
		{
			if (assign) u->mCurrentWeapon = w;
			return w;
		}
	return NULL;
}

// What the middle of the view is on and what the unit can do about it: the state the reticle shows, and where the gun is turned (the ground point, or the weapon's range along the
// same line) when there is such a point.
int FireAim(CUnit *u, const FVector &eye, const FVector &look, const CePlayUnitLaw &law, FVector *aim, bool *haveAim)
{
	*haveAim = false;
	CWeapon *w = FireWeapon(u, false);
	if (!w || !w->mMode) return CE_PLAYUNIT_FIRE_NONE;
	const float e[3] = {eye.X, eye.Y, eye.Z}, l[3] = {look.X, look.Y, look.Z};
	const FVector pos = u->GetPos();
	const float at[3] = {pos.X, pos.Y, pos.Z}, muzzle[3] = {pos.X, pos.Y, pos.Z - law.muzzleLift};
	float hit[3] = {0.f, 0.f, 0.f}, shot[3] = {0.f, 0.f, 0.f};
	int range = CE_PLAYUNIT_RANGE_NONE;
	bool line = false;
	const bool ground = CePlayUnitViewGround(e, l, law.viewReach, law.viewStep, TerrainHeight, NULL, hit);
#ifdef CE_PLAYUNIT_TEST
	gProbeReticle = FVector(hit[0], hit[1], hit[2]);
	gProbeReticleGround = ground;
#endif
	// a look that reaches no ground (the sky, the far horizon) has its point along the look, at the view's reach: the weapon's range brings it in on the same line
	if (!ground)
		for (int i = 0; i < 3; ++i) hit[i] = e[i] + l[i] * law.viewReach;
	if (CePlayUnitFinite3(hit))
	{
		const FVector target(hit[0], hit[1], hit[2]);
		range = CePlayUnitRange(at, hit, w->GetMinRange(target), w->GetMaxRange(target), shot);
		line = CePlayUnitLineClear(muzzle, range == CE_PLAYUNIT_RANGE_TOO_FAR ? shot : hit, law.lineSamples, law.lineTolerance, TerrainHeight, NULL);
		if (range != CE_PLAYUNIT_RANGE_NONE)
		{
			*aim = FVector(shot[0], shot[1], shot[2]);
			*haveAim = true;
		}
	}
	return CePlayUnitFireStateOf(true, *haveAim, range, line, w->ReadyToFire() != FALSE, u->Unknown84() != FALSE, u->mUnknown168 != 0);
}

// The native calls of one held tick, in the stock handler's order: aim, then discharge if the unit stands armed, otherwise arm.
void DriveFire(CUnit *u, const FVector &aim, bool haveAim, int state, bool held)
{
    gAimValid = haveAim;
    if (haveAim) gAimPoint = aim;
	if (!haveAim || !CePlayUnitFireAims(held, state)) return;
	if (!FireWeapon(u, true)) return;
	gAimPoint = aim;
	u->WeaponUnknown509140(0, (SINT)&gAimPoint);
	if (!CePlayUnitFireActs(held, state)) return;
	if (u->Unknown84()) u->Unknown74();
	else u->Unknown86();
}
// ---- free fire (end) ----------------------------------------------------------------------------------------------------------------

// One tick of fire. A class with free fire, where the option's gate allows it, aims at the view's ground point; otherwise (and for the fallback a test build can force) the held
// fire is what the first experiment did, the stock ground vehicle handler on the unit's own target.
void FireTick(CUnit *u, bool held)
{
    gAimValid = false;
	gFireState = CE_PLAYUNIT_FIRE_NONE;
#ifdef CE_PLAYUNIT_TEST
	const unsigned before = Rounds();
	const bool stockOnly = gStockFire;
#else
	const bool stockOnly = false;
#endif
	if (!stockOnly && gCamera && CePlayUnitFreeFireAllowed(FillGate(true), gLaw->freeFire, PortCeCmdMapOpen()))
	{
		const FVector eye = (gOnFoot && gViewMode != kViewLook) ? FootAimEye(u->GetPos()) : gCamera->GetPos();
		const FVector look = gCamera->GetOrientation().GetColumnY();
		FVector aim;
		bool haveAim;
		gFireState = FireAim(u, eye, look, *gLaw, &aim, &haveAim);
		DriveFire(u, aim, haveAim, gFireState, held);
#ifdef CE_PLAYUNIT_TEST
        if (gOnFoot && held && haveAim)
            Note("foot aim: eye=%d actor=%d origin=%.4f,%.4f,%.4f marker=%.4f,%.4f,%.4f yaw=%.6f pitch=%.6f",
                gFootFirstPerson, (int)gThing, eye.X, eye.Y, eye.Z, aim.X, aim.Y, aim.Z, gLookYaw, gLookPitch);
#endif
#ifdef CE_PLAYUNIT_TEST
		const unsigned spawned = Rounds() > before ? Rounds() - before : 0;
		gFired += spawned;
		if (gOut && (gFireState != gLastNotedState || spawned))
		{
			gLastNotedState = gFireState;
			const FVector pos = u->GetPos();
			const float dx = aim.X - pos.X, dy = aim.Y - pos.Y, dz = aim.Z - pos.Z;
			Note("free fire at tick %u: state %d held %d point %d dist %.1f own_rounds=%u weapon %d armed %d arming %d ai_target %d enemies_within_300 %u", gTick, gFireState, held ? 1 : 0,
				haveAim ? 1 : 0, haveAim ? sqrtf(dx * dx + dy * dy + dz * dz) : -1.f, spawned, u->mCurrentWeapon ? 1 : 0, (int)(u->Unknown84() != FALSE), (int)(u->mUnknown168 != 0),
				u->mAI ? (u->mAI->mTarget.ToRead() ? 1 : 0) : -1, EnemiesWithin(u, 300.f));
		}
#endif
		return;
	}
	if (held && u->mAI)
	{
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

// One tick of driving: the actions the last flush delivered become the unit's orders.
void Drive(CUnit *u, CPlayer *player, CBattleEngine *engine)
{
	(void)player;
	(void)engine;
	CePlayUnitDrive in = gIn;
	bool zoomIn = gFootZoomIn, zoomOut = gFootZoomOut;
#ifdef CE_PLAYUNIT_TEST
	// BEA_PORT_PLAYUNIT_ZOOM=+T,-T,...: Zoom In (+) or Zoom Out (-) on tick T, as the wheel's press arrives (a feel script has no wheel)
	if (const char *z = getenv("BEA_PORT_PLAYUNIT_ZOOM"))
		for (const char *c = z; *c; )
		{
			const char sign = *c;
			const unsigned at = (unsigned)strtoul(c + 1, NULL, 10);
			if (at == gTick) { if (sign == '+') zoomIn = true; else if (sign == '-') zoomOut = true; }
			const char *comma = strchr(c, ',');
			if (!comma) break;
			c = comma + 1;
		}
#endif
	ClearInput();
	in.forward = CePlayUnitClamp(in.forward, 0.f, 1.f);
	in.back = CePlayUnitClamp(in.back, 0.f, 1.f);
	in.turn = CePlayUnitClamp(in.turn, -1.f, 1.f);
	in.look = CePlayUnitClamp(in.look, -1.f, 1.f);
	in.pitch = CePlayUnitClamp(in.pitch, -1.f, 1.f);
	gApplied = in;
	// The stick turns and tilts the look when the mouse has been still for a moment: the game's own filtered copy of the mouse arrives on the same actions and dies away
	// after it.
	if (gViewMode == kViewLook)
	{
		if (gMouseQuiet < kLookStickQuiet) ++gMouseQuiet;
		if (gMouseQuiet >= kLookStickQuiet && (in.look != 0.f || in.pitch != 0.f)) CePlayUnitLookStick(&gLookYaw, &gLookPitch, in.look, in.pitch, kLookTickSeconds, UnitLookLaw());
	}
	if (in.leave)
	{
		Release("the Morph key", "Back in your Battle Engine.");
		return;
	}
	// Free-camera and external view changes end this first possession experiment. Restore ownership without overriding the external camera or commandeering its input.
	if (GAME.IsFreeCameraOn(0)) { Release("free camera", "Back in your Battle Engine."); return; }
	if (!gCamera || GAME.GetCamera(0) != gCamera) { Release("another camera"); gBackPending = true; return; }
	// A changed side, a squad assignment, or resumed AI is a newer owner. Relinquish the
	// lease instead of repeatedly detaching the unit or overwriting that owner's orders.
#ifdef CE_PLAYUNIT_TEST
	if (!gKeepAI)
#endif
	{
		if (AnotherUnitOwner(u))
		{
			Release("another owner or AI took control of the unit", "Your unit has new orders.");
			return;
		}
	}
    if (gViewMode == kViewLook && zoomIn != zoomOut) {
        const bool ready = MakeUnitView(u);
        const int previous = ready ? gUnitViewState.view :
            (gFootFirstPerson ? kCePlayUnitCameraFirst : kCePlayUnitCameraClose);
        const int view = ready ? CePlayUnitCameraZoom(&gUnitViewState, gUnitViewRig, zoomIn, zoomOut) :
            (gOnFoot ? (zoomIn ? kCePlayUnitCameraFirst : kCePlayUnitCameraClose) : previous);
        if (view != previous) {
            gFootFirstPerson = gOnFoot && view == kCePlayUnitCameraFirst;
            gFootRenderedView = NULL;
            char tip[96];
            UnitViewHelp(tip, sizeof(tip));
            CePlayUnitNotice(UnitViewName(view), tip, true);
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
	// The direct law: the drive guide in the unit reads this tick's wishes (gApplied) when the unit moves (CeDriveGuide).
	if (gDriveGuide && gDriveGuide->GetUnit() == u && u->mGuide == gDriveGuide)
	{
		gMoving = in.forward > 0.f || in.back > 0.f || in.turn != 0.f;
	}
	// The first experiment's law (BEA_PORT_TANK_DRIVE=path, or a unit with no guide of its own): go-to-points through the stock guide.
	else if (in.forward > 0.1f || pivot)
	{
		gDesired = CePlayUnitSteer(gMoving ? gDesired : actual, actual, in.turn, dt, gLaw->turnRate, gLaw->maxLead);
		float tx, ty;
		if (CePlayUnitPoint(pos.X, pos.Y, gDesired, pivot ? gLaw->pivotReach : gLaw->reach, &tx, &ty))
		{
			const bool changed = !gMoving || fabsf(CePlayUnitWrap(gDesired - gLastAngle)) > 0.05f;
			const float dx = gLastPoint.X - pos.X, dy = gLastPoint.Y - pos.Y;
			if (CePlayUnitReissue(changed, gTick, gLastIssue, pivot ? gLaw->pivotReissue : gLaw->reissue, sqrtf(dx * dx + dy * dy), pivot ? gLaw->pivotNear : gLaw->nearRadius))
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
	FireTick(u, in.fire);
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
    ResetUnitView();
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
// The soldier remains a normal world object. Only its own image is omitted
// when it is the live first-person camera's owner.
// The engine draws a viewpoint through its own copy of the camera it was given (CEngine::SetViewpoint makes a CInterpolatedCamera of it), never the one this
// module hands the hook, so the first-person view is recognised by where the drawing camera stands: exactly at the view's eye. (Compared by identity, the hide
// never held in the game: the body was out of sight only because the eye is inside the head, and the raised rifle showed when the soldier stood to fire.)
bool DrawnFromFootEye()
{
	CCamera *drawing = ENGINE.GetCamera();
	if (!drawing || !gFootRenderedView) return false;
	const FVector a = drawing->GetPos(), b = const_cast<CCamera *>(gFootRenderedView)->GetPos();
	return fabsf(a.X - b.X) < 1e-4f && fabsf(a.Y - b.Y) < 1e-4f && fabsf(a.Z - b.Z) < 1e-4f;
}

bool PortCePlayUnitHidesOwnMesh(const CThing *thing)
{
#ifdef CE_PLAYUNIT_TEST
    if (thing && thing == gUnit && gDriving && gOnFoot)
    {
        ++gHideCalls;
        const int fail = gMirrorPass ? 0 : !UnitViewHidesBody() ? 1 : !gCamera ? 2 : !gFootRenderedView ? 3 : !DrawnFromFootEye() ? 4 :
            GAME.GetCamera(0) != gCamera ? 5 : GAME.GetCurrentLevel() != gLevel ? 6 : GAME.IsFreeCameraOn(0) ? 7 : -1;
        if (fail < 0) ++gHideTrue; else ++gHideFail[fail];
    }
#endif
    return !gMirrorPass && gDriving && gOnFoot && UnitViewHidesBody() && thing && thing == gUnit &&
        gCamera && gFootRenderedView && DrawnFromFootEye() &&
	    GAME.GetCamera(0) == gCamera && GAME.GetCurrentLevel() == gLevel && !GAME.IsFreeCameraOn(0);
}

// While a player steers, the tank needs a new heading each simulation tick.
// With the normal ground vehicle cadence the renderer keeps showing the
// old turn until the next full move, even after the keys have reversed.
float CGroundVehicle::GetMoveMultiplier()
{
	return gDriving && !gOnFoot && this == gUnit && gDriveGuide ? 1.f : CGroundUnit::GetMoveMultiplier();
}

CCamera *PortCePlayUnitRenderCamera(CCamera *original, CPlayer *player) {
    if (!gDriving || !gCamera || original != gCamera ||
        GAME.GetCurrentCamera(0) != gCamera || player != GAME.GetPlayer(0) ||
        GAME.GetCurrentLevel() != gLevel || !Allowed(true)) return original;
    CUnit *unit = Held();
    if (!unit || unit != gUnit || unit->IsDying() || gViewMode == kViewStock) return original;
    static CGenericCamera view;
    if (gViewMode == kViewLook)
    {
        LookShown(unit, &view);
        gFootRenderedView = gOnFoot && UnitViewHidesBody() ? &view : NULL;
        return &view;
    }
    const FVector rawPos = unit->GetPos();
    const FMatrix correction = unit->GetRenderOrientation() * unit->GetOrientation().Transpose();
    FVector pos = unit->GetRenderPos() + correction * (original->GetPos() - rawPos);
    FMatrix ori = correction * original->GetOrientation();
    if (gViewMode == kViewSmooth) SmoothView(unit, &pos, &ori);
    view.mPos = pos;
    view.mOrientation = ori;
    view.mZoom = original->GetZoom();
    return &view;
}

// Read-only state for a test kit (ce_playunit.h): no behaviour of their own.
bool CePlayUnitDrivingNow()
{
	return gDriving && Held() != NULL;
}

const char *CePlayUnitDrivenClass()
{
	CUnit *u = gDriving ? Held() : NULL;
	if (!u || !u->GetUnitData() || !u->GetUnitData()->mName) return "";
	return u->GetUnitData()->mName;
}

bool CePlayUnitDrivenPose(float pos[3], float *headingDeg, float *speedUps)
{
	CUnit *u = gDriving ? Held() : NULL;
	if (!u) return false;
	const FVector p = u->GetPos(), v = u->GetVelocity();
	if (pos) { pos[0] = p.X; pos[1] = p.Y; pos[2] = p.Z; }
	if (headingDeg) *headingDeg = u->GetCurrentOrientation().mYaw * (180.f / 3.14159265f);
	if (speedUps) *speedUps = sqrtf(v.X * v.X + v.Y * v.Y) * 20.f;
	return true;
}

bool CePlayUnitAimNow(float point[3], int *fireState)
{
	if (fireState) *fireState = gFireState - CE_PLAYUNIT_FIRE_READY;	// the rules' order: none, ready, arming, recovering, out of range, blocked
	if (point) { point[0] = gAimPoint.X; point[1] = gAimPoint.Y; point[2] = gAimPoint.Z; }
	return gDriving && gAimValid;
}

unsigned CePlayUnitRoundsNow(float lastImpact[3], float lastAim[3])
{
	const float none = NAN;
	for (int i = 0; i < 3; ++i)
	{
		if (lastImpact) lastImpact[i] = none;
		if (lastAim) lastAim[i] = none;
	}
#ifdef CE_PLAYUNIT_TEST
	if (gKitImpactSeen)
	{
		const float a[3] = {gKitImpact.X, gKitImpact.Y, gKitImpact.Z}, b[3] = {gKitLaunchAim.X, gKitLaunchAim.Y, gKitLaunchAim.Z};
		for (int i = 0; i < 3; ++i)
		{
			if (lastImpact) lastImpact[i] = a[i];
			if (lastAim) lastAim[i] = b[i];
		}
	}
	return gKitRounds;
#else
	return 0;
#endif
}

extern float HUD_SAFE_AREA;	// Hud.cpp: the HUD's safe area, as a share of the window
// The HUD's focus and overlay while a unit is driven with the look (ce_playunit.h, patch 0285).
bool CePlayUnitHudFocusOf(CePlayUnitHudFocus *out)
{
	if (!out || !gDriving || !gCamera || gViewMode != kViewLook || GAME.GetCurrentCamera(0) != gCamera || GAME.GetCurrentLevel() != gLevel || !Allowed(true))
		return false;
	CUnit *u = Held();
	if (!u || u != gUnit || u->IsDying() || !(u->GetMaxLife() > 0.0001f)) return false;
	const FVector p = u->GetRenderPos();
	out->x = p.X; out->y = p.Y; out->z = p.Z;
	out->yaw = gLookYaw;
	out->life = u->GetLife() > 0.f ? u->GetLife() : 0.f;
	out->fullLife = u->GetMaxLife();
	return true;
}

bool CePlayUnitHudOverlay(CPlayer *player, int viewpoint)
{
	CePlayUnitHudFocus focus;
	if (viewpoint != 0 || !player || player != GAME.GetPlayer(0) || GAME.GetCamera(0) != gCamera || !CePlayUnitHudFocusOf(&focus)) return false;
	CBattleEngine *be = player->GetBattleEngine();
	if (!be || be->IsDying()) return false;
	// the viewport and its safe area, as CHud::RenderOverlayForViewpoint sets them for the Battle Engine's view
	ENGINE.SelectViewpoint(viewpoint);
	CViewport *vp = ENGINE.GetViewportForViewpoint(viewpoint);
	if (!vp) return false;
	HUD.mViewportWidth = float(vp->Width);
	HUD.mViewportHeight = float(vp->Height);
	HUD.mViewportX = float(vp->X);
	HUD.mViewportY = float(vp->Y);
	HUD.mSafeWidth = HUD.mViewportWidth;
	HUD.mSafeHeight = HUD.mViewportWidth;
	HUD.mSafeX = HUD.mViewportX;
	HUD.mSafeY = HUD.mViewportY;
	const float sw = float(PLATFORM.GetWindowWidth()), sh = float(PLATFORM.GetWindowHeight());
	const float aw = HUD_SAFE_AREA * sw, ah = HUD_SAFE_AREA * sh, left = (sw - aw) * 0.5f, top = (sh - ah) * 0.5f;
	if (HUD.mSafeX < left) { HUD.mSafeWidth -= left - HUD.mSafeX; HUD.mSafeX = left; }
	if (HUD.mSafeY < top) { HUD.mSafeHeight -= top - HUD.mSafeY; HUD.mSafeY = left; }
	if (HUD.mSafeX + HUD.mSafeWidth > left + aw) HUD.mSafeWidth = left + aw - HUD.mSafeX;
	if (HUD.mSafeY + HUD.mSafeHeight > top + ah) HUD.mSafeHeight = top + ah - HUD.mSafeY;
	HUD.mBattleEngine = be;
	HUD.mViewport = vp;
	HUD.mViewpoint = viewpoint;
	HUD.mViewportBottom = HUD.mViewportY + HUD.mViewportHeight;
	HUD.mViewportRight = HUD.mViewportX + HUD.mViewportWidth;
	HUD.mSafeBottom = HUD.mSafeY + HUD.mSafeHeight;
	HUD.mSafeRight = HUD.mSafeX + HUD.mSafeWidth;
	HUD.SetOverlayRenderStates();
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	HUD.Render00483530();				// the objective panel
	HUD.SetOverlayRenderStates();
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);	// the ring is drawn as geometry: over the world, as the Battle Engine's view leaves the state for it
	RENDERINFO.SRS(D3DRS_ZENABLE, FALSE);
	RENDERINFO.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
	RENDERINFO.Apply();
	HUD.mCompass->Render(be);			// the ring round the reticle, its health arc and marker the unit's (the focus)
	// the radar's panel as CHud::RenderWeaponPanel draws it (its backing, outline and north pointer) without the Battle Engine's weapon charge, turned with the look
	HUD.SetOverlayRenderStates();
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.Apply();
	const float x = HUD.mViewportX + HUD_OFFSET_X + 17.f, y = HUD.mViewportBottom + 16.f - HUD_OFFSET_Y;
	CSPRITERENDERER::DrawSprite(x, y, 0.001f, HUD.mRadioView, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0x6fffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	if (HUD.IsComponentVisible(HP_RADAR))
	{
		HUD.SetAdditive();
		CSPRITERENDERER::DrawSprite(x, y, 0.001f, HUD.mRadarOutline, (ESpriteAlignment)2, SPT_NONE, 1.f, 0.f, 0xff6f8faf, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	}
	HUD.SetAlphaBlend();
	const float sn = sinf(focus.yaw), cs = cosf(focus.yaw);
	RENDERINFO.SRS(D3DRS_ZENABLE, FALSE);
	CSPRITERENDERER::DrawSprite(x + 48.f + sn * 45.f, (y - 128.f + 48.f) - cs * 45.f, 0.001f, HUD.mRadioNorth, SA_CENTRE, SPT_NONE, 1.f, focus.yaw, 0xff5f7fff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	HUD.RenderRadar(96.f);				// the radar, centred on the unit and turned with the look (the focus)
	return true;
}

// Each poll of the game's mouse (dinput.cpp, after the movement is known and before the game reads it; weakly, so a program without this module has none): while a unit is
// driven with the look camera, the movement turns the look. The game's own use of the movement is not changed.
// ---- the look's mouse (begin)
extern "C" void PortCePlayUnitMouse(long dx, long dy)
{
	if (!gDriving || !gCamera || gViewMode != kViewLook || GAME.IsPaused() || GAME.GetGameState() != GAME_STATE_PLAYING) return;
	if (!dx && !dy) return;
	// the player's own Invert Y, as the Battle Engine on the ground takes it
	if (CAREER.GetInvertYAxis(0)) dy = -dy;
	CePlayUnitLookTurn(&gLookYaw, &gLookPitch, float(dx), float(dy), UnitLookLaw());
	gMouseQuiet = 0;
}
// ---- the look's mouse (end)

// The engine's battle-line pass (patch 0240, DXEngine.cpp; weakly, so a program without this module has none): a unit's view shows no HUD (the Battle Engine's
// cockpit, its rings and its radar are the engine's), but while the player drives a unit the mission radio's capsule with its words and portrait, and the battle line's
// map, are drawn as they are over the Battle Engine's view. The radio then also opens and closes as the messages come, instead of all at once on the return.
extern "C" int PortCePlayUnitShowsBattleline(int viewpoint)
{
	if (viewpoint != 0 || !gDriving || !gCamera || GAME.GetCamera(0) != gCamera || GAME.GetCurrentLevel() != gLevel) return 0;
	CUnit *unit = Held();
	return unit && unit == gUnit && !unit->IsDying() ? 1 : 0;
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

uint64_t CePlayUnitSelectionWorld()
{
    return gSelectionWorld;
}

static CUnit* SelectionUnit(uint64_t world, CUnit* candidate, int expectedNumber)
{
    if (!candidate || !world || world != gSelectionWorld) return NULL;
    ListIterator<CThing> it(&WORLD.GetThingNB());
    for (CThing* thing = it.First(); thing; thing = it.Next()) {
        if (thing != candidate) continue;
        if (thing->GetThingType() != THING_TYPE_UNIT ||
            thing->GetThingNumber() != expectedNumber) return NULL;
        return static_cast<CUnit*>(thing);
    }
    return NULL;
}

static bool SelectionReady(uint64_t world, CUnit* candidate, int expectedNumber,
    CPlayer** player, CBattleEngine** engine, CCONTROLLER** controller,
    CUnit** unit, int* kind, const char** reason)
{
    if (reason) *reason = "Unit control is unavailable in this mode";
    if (!gConfigured) ReadConfiguration();
    if (!gWanted || !Allowed(gWanted)) return false;
    if (CePlayUnitOwnsPilot()) {
        if (reason) *reason = "Return to your pilot first";
        return false;
    }
    *unit = SelectionUnit(world, candidate, expectedNumber);
    if (!*unit) {
        if (reason) *reason = "Unit is no longer available";
        return false;
    }
    if (!Ready(player, engine, controller)) {
        if (reason) *reason = NotReadySentence();
        return false;
    }
    *kind = CePlayUnitClassFor(Facts(*unit, *engine));
    if (*kind < 0) {
        if (reason) *reason = "This unit cannot be controlled";
        return false;
    }
    if (reason) *reason = NULL;
    return true;
}

bool CePlayUnitCanTakeSelection(uint64_t world, CUnit* candidate,
    int expectedNumber, const char** reason)
{
    CPlayer* player = NULL;
    CBattleEngine* engine = NULL;
    CCONTROLLER* controller = NULL;
    CUnit* unit = NULL;
    int kind = -1;
    return SelectionReady(world, candidate, expectedNumber, &player, &engine,
        &controller, &unit, &kind, reason);
}

bool CePlayUnitTakeSelection(uint64_t world, CUnit* candidate,
    int expectedNumber, const char** reason)
{
    if (PortCeCmdMapOpen()) {
        if (reason) *reason = "Return from Commander before taking control";
        return false;
    }
    CPlayer* player = NULL;
    CBattleEngine* engine = NULL;
    CCONTROLLER* controller = NULL;
    CUnit* unit = NULL;
    int kind = -1;
    if (!SelectionReady(world, candidate, expectedNumber, &player, &engine,
        &controller, &unit, &kind, reason)) return false;
    if (!Take(player, engine, controller, unit, kind)) {
        if (reason) *reason = "Unit control is no longer available";
        return false;
    }
    return true;
}

bool CePlayUnitCanTakeDelivery() {
    if (!gConfigured) ReadConfiguration();
    return gWanted && Allowed(true) && !CePlayUnitOwnsPilot() && CeCmdDeliveredSquad() >= 0;
}

bool CePlayUnitTakeDelivery() {
    // The calling button has surrendered the strategic camera and input trap.
    CPlayer *player = nullptr;
    CBattleEngine *engine = nullptr;
    CCONTROLLER *controller = nullptr;
    if (PortCeCmdMapOpen()) return false;
    if (!CePlayUnitCanTakeDelivery()) {
        CePlayUnitNotice("Tank unavailable.", "You still control your Battle Engine.");
        return false;
    }
    if (!Ready(&player, &engine, &controller)) {
        CePlayUnitNotice(NotReadySentence(), "You still control your Battle Engine.");
        return false;
    }
    CUnit *unit = CeFactoryLiveVehicle();
    if (unit && CePlayUnitClassFor(Facts(unit, engine)) == CE_PLAYUNIT_CLASS_TANK
        && Take(player, engine, controller, unit, CE_PLAYUNIT_CLASS_TANK)) return true;
    CePlayUnitNotice("Tank unavailable.", "You still control your Battle Engine.");
    return false;
}

// Native turret motion and weapon pitch share the possessed player's fire point.
// Keep the unit's own turn limits, interpolation and ballistic pitch calculation.
bool PortCePlayUnitAimPoint(const CUnit* unit, FVector& point) {
    if (!gDriving || gOnFoot || !gAimValid || !unit || unit != Held() || !Allowed(true)) return false;
    point = gAimPoint;
    return true;
}

bool CePlayUnitOwnsPilot()
{
	return gDriving || gCamera || gControllerPending || gSwitchWait;
}

void CePlayUnitMapBoundary()
{
	if (!gConfigured || !gWanted) return;
	gTakePressed = false;
	gTakeKey.wasDown = PortKeyHeldNow(TakeKey()) > 0;
}

// Every frame (ce_notice.cpp's render hook, after the console; weakly, so a program without this module has none): the take key is read, and the reticle and the notice
// are drawn (ce_playunit_hud.cpp). Nothing at all while the option is not wanted: the keyboard is not read and nothing is drawn.
void CePlayUnitRenderState()
{
	if (!gConfigured || !gWanted) return;
	// the take key: a press counts only while the level plays, unpaused, with no menu, console or map holding the keys, and the key is the module's (TakeKeyFree)
	const int held = PortKeyHeldNow(TakeKey());
	const bool mapOpen = PortCeCmdMapOpen();
	const bool mayCount = !mapOpen && GAME.GetGameState() == GAME_STATE_PLAYING && !GAME.IsPaused() && !LT.GetKeytrap() && !GAME.IsFreeCameraOn(0) && TakeKeyFree();
	if (CePlayUnitPressed(&gTakeKey, held > 0, mayCount)) gTakePressed = true;
	if (mapOpen) { gTakePressed = false; return; }
	if (PortCePhotoOn() || PortCePhotoSavedNext() || GAME.IsPaused() || GAME.GetGameState() != GAME_STATE_PLAYING) return;
	CCamera *camera = GAME.GetCurrentCamera(0);
	const bool reticle = gDriving && gCamera && camera == gCamera;
	// with the Battle Engine's HUD up, or the radio over a driven unit's view (PortCePlayUnitShowsBattleline), the notice and the prompt sit at the top, clear of the radio
	CePlayUnitHudRender(reticle, reticle ? gFireState : CE_PLAYUNIT_FIRE_NONE, (camera && camera->GetShowHUD()) || (reticle && PortCePlayUnitShowsBattleline(0)));
}

namespace
{
// A refused press: the cancel cue and one sentence (and, where it helps, a second line saying what to do).
void Refuse(const char *sentence, const char *hint = NULL)
{
	Log("take key refused: %s", sentence);
	CePlayUnitNotice(sentence, hint);
	CFrontEnd::PlaySound(FES_BACK);
}

const char *SightSentence(int why, int family)
{
	return why == CE_PLAYUNIT_REFUSE_COUNT ? "No friendly unit in sight." : CePlayUnitRefusalSentence(why, family);
}

const char *SightHint(int why)
{
	if (why != CE_PLAYUNIT_REFUSE_COUNT || !TakeKeyFree()) return NULL;
	static char hint[80];
	snprintf(hint, sizeof(hint), "Aim at one and press %s.", PortCeActionName(CeControlAction::TakeUnit));
	return hint;
}

// A press of the take key while the Battle Engine is the player's: the unit in the middle of the view is taken, or the player is told why not.
void AnswerTake()
{
	CBattleEngine *engine = NULL;
	CPlayer *player = NULL;
	CCONTROLLER *controller = NULL;
	if (!Ready(&player, &engine, &controller))
	{
		if (const char *sentence = NotReadySentence()) Refuse(sentence);
		return;
	}
	int why = 0, family = 0, kind = -1;
	float eye[3] = {0.f, 0.f, 0.f}, w = 0.f, h = 0.f;
	FrameSize(&w, &h);
	CUnit *u = RenderedEye(GAME.GetCurrentCamera(0), eye) ? Sight(engine, eye, w, h, NULL, &why, &family, &kind) : NULL;
#ifdef CE_PLAYUNIT_TEST
	Note("take key at tick %u: chosen %d why %s family %d", gTick, u ? (int)u->GetThingNumber() : -1, CePlayUnitRefusalName(why), family);
	NoteSight("take", eye, u, engine);
#endif
	if (!u || kind < 0) { Refuse(SightSentence(why, family), SightHint(why)); return; }
	Take(player, engine, controller, u, kind);
}

// A press of the take key while a unit is driven: the unit in the middle of its view is taken instead, or the player is told why not (and keeps the one they have).
void AnswerSwitch(CUnit *held, CBattleEngine *engine)
{
	int why = 0, family = 0, kind = -1;
	float eye[3] = {0.f, 0.f, 0.f}, w = 0.f, h = 0.f;
	FrameSize(&w, &h);
	CUnit *u = RenderedEye(gCamera, eye) ? Sight(engine, eye, w, h, held, &why, &family, &kind) : NULL;
#ifdef CE_PLAYUNIT_TEST
	Note("switch key at tick %u: chosen %d why %s family %d", gTick, u ? (int)u->GetThingNumber() : -1, CePlayUnitRefusalName(why), family);
	NoteSight("switch", eye, u, engine);
#endif
	if (!u || kind < 0) { Refuse(SightSentence(why, family), SightHint(why)); return; }
	Release("switching to another unit");
	gSwitchUnit = u;
	gSwitchThing = u->GetThingNumber();
	gSwitchWait = kCePlayUnitSwitchWait;
}

// ---- the switch (begin): compiled with fake units by ce_playunit_sight_tests.py
// The unit switched to, once the Battle Engine's own view is back: taken if it is still there and still may be; the wait gives up after a few ticks and says so
// (CePlayUnitSwitchStep).
void FinishSwitch()
{
	if (!gSwitchWait) return;
	CUnit *u = NULL;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
		if ((void *)t == (void *)gSwitchUnit && t->GetThingNumber() == gSwitchThing && (t->GetThingType() & THING_TYPE_UNIT)) u = (CUnit *)t;
	CBattleEngine *engine = NULL;
	CPlayer *player = NULL;
	CCONTROLLER *controller = NULL;
	const bool ready = u && Ready(&player, &engine, &controller);
	const int kind = ready ? CePlayUnitClassFor(Facts(u, engine)) : -1;
	switch (CePlayUnitSwitchStep(&gSwitchWait, u != NULL, ready, kind >= 0))
	{
	case CE_PLAYUNIT_SWITCH_TAKE:
		gSwitchUnit = NULL;
		Take(player, engine, controller, u, kind);
		break;
	case CE_PLAYUNIT_SWITCH_GIVE_UP:
		gSwitchUnit = NULL;
		Refuse("That unit cannot be taken now.", "Back in your Battle Engine.");
		break;
	default:
		break;
	}
}
// ---- the switch (end)
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
	if ((!gDriving && gWanted && PortCeCmdMapOpen()) || GAME.IsPaused()) { ClearInput(); gTakePressed = false; return; }
	++gTick;
#ifdef CE_PLAYUNIT_TEST
	ObserveSuccessorMotion();
	FollowReturned();
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
	if (gOut && gReturnedAt && gTick - gReturnedAt <= 12)
	{
		CCamera *view = GAME.GetCurrentCamera(0);
		Note("after return: tick %u radio_message %d hud %d present %u", gTick, GAME.GetMessageBox() && GAME.GetMessageBox()->mCurrent ? 1 : 0, view && view->GetShowHUD() ? 1 : 0, PortPresentCount());
	}
	TestBorrow(engine);
	ReconcileCamera();
	if (gOut && !gCensus && engine && gTick >= kFirstLook)
	{
		int friendlies = 0;
		Census(engine, &friendlies);
	}
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
		if (!Allowed(true)) Release("the option is no longer allowed here", "Back in your Battle Engine.");
		else if (!u || u->IsDying() || u->GetLife() <= 0.f) Release("the unit is gone", "Your unit was destroyed.", "Back in your Battle Engine.");
		else if (!engine || engine->IsDying() || engine->GetLife() <= 0.f) Release("the Battle Engine is gone");
		else
		{
#ifdef CE_PLAYUNIT_TEST
			if (gTakeOnce) gTakePressed = false;
#endif
			// Return belongs to the current lease. A simultaneous Take must neither switch units nor stay pending for the Battle Engine.
			if (gIn.leave) gTakePressed = false;
			if (gTakePressed)
			{
				gTakePressed = false;
				AnswerSwitch(u, engine);
				if (!gDriving) { FinishSwitch(); return; }
			}
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
		if (gObserve && engine && !gObserveDone && !Held())
		{
			if (gTick >= gNextLook)
			{
				int friendlies = 0;
				CUnit *u = Census(engine, &friendlies);
				++gLooks;
				gNextLook = gTick + kLookEvery;
				if (u) { gUnit = u; gThing = u->GetThingNumber(); Note("observing unit %d at tick %u (%d friendly units)", (int)gThing, gTick, friendlies); }
				else if (gLooks >= kMaxLooks) { gObserveDone = true; Note("no tank to observe"); }
			}
		}
		if (gObserve && gUnit) { if (CUnit *u = Held()) Probe(u, engine); }
		gTakePressed = false;
		return;
	}
#else
	if (!Allowed(true)) { gTakePressed = false; return; }
#endif
	// The Battle Engine is the player's: a return made while another view held the screen is said once the player's own view is back, a switch from one unit to the
	// next finishes, and a press of the take key is answered. Nothing is taken without one.
	if (gBackPending)
	{
		CCamera *view = GAME.GetCurrentCamera(0);
		if (view && view->GetShowHUD() && !GAME.IsFreeCameraOn(0)) { gBackPending = false; CePlayUnitNotice("Back in your Battle Engine.", NULL); }
	}
	// a press while a switch waits for the Battle Engine's view is let go: the switch already answers the press that asked for it
	if (gSwitchWait) { gTakePressed = false; FinishSwitch(); return; }
	if (!gTakePressed) return;
	gTakePressed = false;
	AnswerTake();
}

void CePlayUnitLevelEnd()
{
    SelectionWorldEnded();
#ifdef CE_PLAYUNIT_TEST
	FinishBorrow(true);
#endif
	if (gDriving) Release("the level is ending");
	CePlayUnitNoticeClear();
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
