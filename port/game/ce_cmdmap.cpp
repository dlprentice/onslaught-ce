// SPDX-License-Identifier: GPL-3.0-only
// Optional Commander controls: M enters the live view of the battlefield without pausing.
// The pilot remains present and vulnerable. A click picks a squad, a drag picks the
// squads inside its box, Shift adds or takes away, Ctrl and A picks every squad, Ctrl
// and a digit keeps the picked squads as a group and the digit picks them again. A
// right click orders an Advance (it fights on the way), Ctrl and a right click a
// Move (it holds its fire on the way), H holds, B falls back, and Delete gives the
// squads back to their own orders; a Move or a Fall back holds where it arrives. The
// buttons at the foot of the view give the same four orders. Arrows or middle-button
// dragging pan, wheel/R/F zoom, Q/E turn, and PgUp/PgDn tilt. M/Esc return to the
// pilot. V opens the paused tactical map, a tester's page, only with ce.ini's
// commander_tactical_map=1 (TacticalMapAllowed).
//
// The live view renders the native 3D scene from the strategic camera inside
// CeRenderFence (ce_render_fence.h), so the battle gets only the pilot frame's
// render side effects. Friendly unit marks are projected read-only onto the
// captured world view (ce_cmdview.cpp), and the live pointer intersects its
// forward ray with the map's heightfield. Every action refreshes squad identities
// and rechecks the current local camera owner. Orders still pass through
// ce_commander's authority gates. The paused overview retains its own input sink,
// view, zoom and ordering path.
//
// Never in faithful play: off where the CE layer is off, in a replay, a recording, under probes or the virtual clocks, for a test pilot or a
// network game, in multiplayer (but for a Commander match, ce_cmdmatch.h: a versus level one person plays), demos and the front end, and unless ce.ini says commander=1 (a test build's proof switch, BEA_PORT_COMMANDER_PROOF=1
// with BEA_PORT_COMMANDER_MAP=1 and BEA_PORT_CE_LAYER=1, lets it run under a virtual clock: ce_commander.cpp). It draws with the game's own
// sprite and font calls, allocates nothing in the game's heaps but the controller stack entry the
// pause menu also makes and the paused map's texture, and draws no random number. Squads that follow
// mission scripts, aircraft and objectives are drawn but take no orders.
#ifdef CE_VERSION

#include <algorithm>
#include <cmath>
#include <limits>
#include "ce_dodge_binding.h"
#include "ce_world_view.h"
#include "ce_cmdproduction.h"
#include "ce_cmd_hud.h"
#include "ce_cmd_voice.h"

#include "common.h"
#include "game.h"
#include "frontend.h"
#include "GameInterface.h"
#include "PauseMenu.h"
#include "CLIParams.h"
#include "Controller.h"
#include "ltshell.h"
#include "mouse.h"
#include "map.h"
#include "world.h"
#include "unit.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "FastVB.h"
#include "renderinfo.h"
#include "Texture.h"
#include "ce_entity_handles.h"
#include "ce_sandbox_telemetry.h"
#include "ce_commander.h"
#include "ce_cmdorders.h"
#include "ce_command_selection.h"
#include "ce_cmdpointer.h"
#include "ce_commander_camera.h"
#include "ce_cmdview.h"
#include "ce_hud_reservation.h"
#include "ce_commander_transition.h"
#include "ce_cmdmatch.h"
#include "ce_cmdsel.h"
#include "ce_cmdgroup.h"
#include "ce_cmdoutcome.h"
#include "ce_cmdrules.h"
#include "ce_playunit.h"
#include "Camera.h"
#include "DXEngine.h"
#include "ce_render_fence.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
extern "C" int PortCeRecordFeaturesAllowed(void) __attribute__((weak));
#include <string.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error The commander map requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SessionActive(void) __attribute__((weak));
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeEditorActive(void) __attribute__((weak));
extern "C" unsigned PortPresentCount(void);		// d3d9hooks.cpp
bool PortClockVirtual();						// clock.cpp: a virtual clock (a replay, a recording, probes, or a test build's proof run)
extern "C" int PortMouseHeldNow(unsigned button);
extern "C" int PortKeyHeldNow(unsigned dik);	// window.cpp: 1 a key is held, 0 it is not, -1 the platform cannot say
// clock.cpp: the operating system's monotonic clock in nanoseconds. This file is built by the game's rule, whose include list has no SDL
// directory on Windows, so its wall clock comes from the port and not from an SDL header.
unsigned long long PortClockRawNs();
bool PortCeLayer();
bool PortRecording();

#if defined(CE_COMMANDER_TEST) && defined(__linux__)
// A test build keeps this file's own records in sections of their own, which the live view's footprint audit leaves out (ce_cmdview_audit.cpp).
#pragma clang section bss = "cecmdb" data = "cecmdd"
#endif

namespace
{
struct ControllerStackAccess : CController
{
	static SPtrSet<CActiveReader<IController> > CController::*StackField() { return &ControllerStackAccess::mToControlStack; }
};

void RelinquishBuriedController(CCONTROLLER *controller, IController *target)
{
	if (!controller || !target) return;
	SPtrSet<CActiveReader<IController> > &stack = controller->*ControllerStackAccess::StackField();
	for (;;)
	{
		CActiveReader<IController> *found = NULL;
		ListIterator<CActiveReader<IController> > it(&stack);
		for (CActiveReader<IController> *r = it.First(); r; r = it.Next())
		{
			if (r->ToRead() == target) { found = r; break; }
		}
		if (!found) break;
		stack.Remove(found);
		stack.Add(found);
		controller->RelinquishControl();
	}
}

unsigned OpenKey() { return (unsigned)PortCeActionKey(CeControlAction::Commander); }
unsigned gCloseKey = 0;
const int kMouseUp = 3, kMouseDown = 4;			// mouse.cpp: what the key trap is told of a button (key 0 left, 1 middle, 2 right; 3 and 4 the wheel)
const int kMaxSquads = 512, kGrid = 128;
const unsigned kNoteLength = 240;				// Presents a note stays up

// The game's pause flag is protected; the pause menu's UnPause also wakes the menu's fade, which would show for 0.4 s over the battle.
struct GameAccess : CGame
{
	static BOOL CGame::*PauseField() { return &GameAccess::mPause; }
};

// Where the pilot's controls go while the map is open: nowhere.
class CeCmdSink : public IController
{
public:
	virtual void ReceiveButtonAction(CController *, int, float) {}
	virtual BOOL CanBeControlledWhenInPause() { return TRUE; }
	virtual EControlType GetControlType() { return CONTROL_GAME_MENU; }
};
CeCmdSink gSink;		// a global, as the game's own CReconnectInterface objects are: nothing is allocated until a controller stack entry points at it

bool gOpen;
bool gOpening;		// Open() is releasing the mouse (ShutdownMouse seeds the pointer: window pixels while this is set)
bool gArmed;			// A was pressed: the next click is an Advance
bool gShift;
bool gDown[256];		// keys the trap has seen go down and not up (a held key's repeats are not fresh presses)
bool gEscClosed;		// the Esc that closed the map is still held: the game never saw its press, so it must not see its repeats (they would open the pause menu)
int gSelected = -1;		// the picked squad's ordinal: the selection's lead (code that picks one squad sets it, and Refresh makes that the selection)
CeCmdSel gSel;			// the picked squads, the lead first (ce_cmdsel.h)
CeCmdGroups gGroups;	// the numbered groups of this level (Ctrl and a digit keeps the selection, the digit picks it again)
CeCmdMatchState gMatch;	// how a Commander match stands (ce_cmdmatch.h), read once an update while the live view is open (MatchFrame); the view's header shows it
bool gReopen;			// a Commander match: the live view closed when the commander's Battle Engine went down, and opens again when it is back
bool gPlayerClosed;		// the player closed the live view (M or Esc): a Battle Engine going down in the same update does not bring it back
bool gToldInFight;		// this level has said "Your Battle Engine is still in the fight." (once, on the first open)
bool gLiveShown;		// the live view's pointer has moved since the view opened: it is drawn only then (the game's pointer shows once the mouse moves)
bool gLooseBounds;		// the live view: its centre may reach the world's edge and its span half again the world's (it looks at a slant, and the sea is drawn)
CeCmdAlertState gAlert;		// the match's alerts (ce_cmdrules.h), from the updates the live view is open in
int gHover = -1;
CeCmdSquad gSquads[kMaxSquads];
int gCount;
char gNote[112];
unsigned gNoteUntil;
// A refusal of an order given with a click is shown where the click was ("Choose land for ground units." beside the pointer, not at the foot of the
// screen): the note's end Present names which note it belongs to.
unsigned gNoteAtUntil;
float gNoteAtX = -1.f, gNoteAtY = -1.f;
bool gNoteNotYet;			// the note on show is "the battle has not started yet": it goes when the battle does
bool gLayoutLogged;
int gUntracked;				// squads the order layer's table had no room for, or this one: not shown, not commandable
#ifdef CE_COMMANDER_TEST
unsigned long long gPeriodNs[2];	// frame periods, [1] the live view's frames and [0] the level's others, and how many of each (diagnostics)
unsigned gPeriods[2];
unsigned gFrames;			// frames drawn while open, and the milliseconds they took the processor to issue (diagnostics, never read by the game)
double gMsTotal, gMsMax;
#endif

// The map's clock, in nanoseconds, as an integer. Every time the map measures is the difference of two readings of it, taken as integers and only then
// turned into seconds: the game's x87 unit is left at single precision (Direct3D leaves it so, d3d9hooks.cpp), which rounds each double result to a
// 24-bit mantissa, so the clock as a double (milliseconds since boot) cannot hold the difference of two frames. After a day and a half of uptime it is a
// multiple of 16 ms, after three days of 32 ms: the live camera moved in steps of 16 ms of travel on the frames where the reading changed and not at all
// on the others (frame-feel log, 144 Hz: a move every second or third frame, always the same size), and the paused map's pan was quantized the same way.
// Under a virtual clock (the map is off in faithful play; only a test build's proof switch turns it on there) the time is the frames' own, 1/30 s a Present as
// the clock's default is, so that a proof run moves the camera the same way every time.
std::uint64_t Ns()
{
	if (PortClockVirtual()) return static_cast<std::uint64_t>(PortPresentCount()) * 33333333ull;
	return PortClockRawNs();
}

// Seconds from one reading of Ns() to a later one (zero if the clock went backwards).
double SecondsBetween(std::uint64_t from, std::uint64_t to)
{
	return to > from ? double(to - from) * 1e-9 : 0.0;
}

// The world: the occupied cells, and the square window of it the map shows (it opens on the action and zooms and pans).
float gBoundsX0, gBoundsY0, gBoundsX1, gBoundsY1;
float gViewX, gViewY, gViewSpan;
std::uint64_t gMapLastNs;
bool gTerrainDirty;
int gKeptLevel = -1;			// the level the kept view belongs to
float gKeptCentreX, gKeptCentreY, gKeptSpan;
DWORD gTerrain[kGrid * kGrid];
// The terrain is kGrid x kGrid cells whose colours change only with the view. Drawn as 16,384 sprites a frame it took the main thread about
// nine milliseconds of every frame the map was open (frame-feel log, 60 Hz model); as one stretched texture it takes well under one. The texture
// is made when the map first draws and freed when it closes; it is filled again every frame, which is cheap (64 KB) and means a device reset
// that empties it costs one frame. Where it cannot be made the map draws the cells as before.
CDXTexture *gTerrainTexture;
bool gTerrainTextureFailed;
bool gTerrainTexDirty = true;

struct Layout
{
	float w, h, s;				// the window and the interface scale (the window against 640 x 480)
	float mapX, mapY, mapSide;	// the map's square, in window pixels
	float k;					// pixels a world unit takes
	float panelX, panelW;
} L;

CDXBitmapFont *gFont;
CDXTexture *gWhite;

extern "C" int CE_SettingGetBool(const char *key, int def);		// ce.cpp: a ce.ini line read as yes or no

// The paused tactical map is a tester's page (it has neither the live view's look nor all of its orders): V opens it only with ce.ini's
// commander_tactical_map=1, and in a test build with BEA_PORT_COMMANDER_TACTICAL=1 too. Read once, the first time V is pressed in the view.
bool TacticalMapAllowed()
{
	static int allowed = -1;
	if (allowed < 0)
	{
		allowed = CE_SettingGetBool("commander_tactical_map", 1) == 1 ? 1 : 0;
#ifdef CE_COMMANDER_TEST
		if (const char *v = getenv("BEA_PORT_COMMANDER_TACTICAL")) if (!strcmp(v, "1")) allowed = 1;
#endif
	}
	return allowed == 1;
}

// ---- the gate ----------------------------------------------------------------------------------------------------------------------

// Where the map may exist at all: the feature is on, and this is a single-player level of the player's own game (not a recording, a demo, the front end or a network game),
// or a Commander match (a versus level one person plays: ce_cmdmatch.h holds the one admission rule, which the order layer asks too).
bool Eligible()
{
	if (!CeCmdMapOn() || !PortCeLayer() || (PortRecording() && !(PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed()))) return false;
	if ((CE_BootRequested && CE_BootRequested()) || (CE_SessionActive && CE_SessionActive())) return false;
	if ((PortCeMuseumVisitActive && PortCeMuseumVisitActive()) || (PortCeEditorActive && PortCeEditorActive())) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo || CLIPARAMS.mStressTest ||
		CLIPARAMS.mAttractMode || FRONTEND.IsRunning()) return false;
	return CeCmdAdmits(!GAME.IsMultiplayer() && GAME.GetNPlayers() == 1);
}

// The font and the white texture the map draws with, and a window to draw in.
bool DrawingReady()
{
	gFont = PLATFORM.Font(FONT_SMALL);
	gWhite = HUD.GetWhiteTexture();
	return gFont && gFont->GetHeight() > 0 && gWhite && gWhite->GetRealWidth() > 0 && gWhite->GetRealHeight() > 0 &&
		   PLATFORM.GetWindowWidth() > 0 && PLATFORM.GetWindowHeight() > 0;
}

// The level is in a state the map can open in: playing, no menu up, the pilot at the controls.
bool Allowed()
{
	if (!Eligible()) return false;
	if (GAME.GetGameState() != GAME_STATE_PLAYING || GAMEINTERFACE.IsMenuUp() || GAMEINTERFACE.mShowJoypad) return false;
	if (!GAME.GetPlayer(0) || !GAME.GetPlayer(0)->GetBattleEngine() || !GAME.GetController(0) || !GAME.GetCurrentCamera(0) ||
		!GAME.GetPauseMenu() || GAME.GetPauseMenu()->mActive || GAME.IsFreeCameraOn(0)) return false;
	return DrawingReady();
}

// The level has not started playing: the game code's pre-run, or the opening camera's pan over the level (the first seconds). The open key says so, one line, rather
// than do nothing.
bool NotYet()
{
	if (!Eligible()) return false;
	const int state = GAME.GetGameState();
	return state == GAME_STATE_PRE_RUNNING || state == GAME_STATE_PANNING;
}

// The cockpit may show a note: a level the map belongs to, playing, with no menu over it and the drawing ready. The commander's Battle Engine may be down
// (the note that says so, and the open key's answer then, show while it is).
bool NoteMay()
{
	if (!Eligible() || GAME.GetGameState() != GAME_STATE_PLAYING || GAMEINTERFACE.IsMenuUp() || GAMEINTERFACE.mShowJoypad) return false;
	if (!GAME.GetPauseMenu() || GAME.GetPauseMenu()->mActive) return false;
	return DrawingReady();
}

void Note(const char *text)
{
	snprintf(gNote, sizeof(gNote), "%s", text);
	gNoteUntil = PortPresentCount() + kNoteLength;
	gNoteNotYet = false;
	CeCmdNote("map note %s", text);
}

// ---- the squads --------------------------------------------------------------------------------------------------------------------

const CeCmdSquad *ByOrdinal(int ordinal)
{
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].ordinal == ordinal) return &gSquads[i];
	return NULL;
}

// A fresh look at the squads. A squad picked by setting gSelected (Tab, the paused map) becomes the whole selection; squads that are gone or take no orders
// any more leave it; each listed squad says whether it is picked.
void Refresh()
{
	gCount = CeCmdList(gSquads, kMaxSquads);
	gUntracked = CeCmdUntracked();
	if (gSelected != CeSelLead(gSel)) CeSelOnly(gSel, gSelected);
	CeSelPrune(gSel, [](int id) { const CeCmdSquad *s = ByOrdinal(id); return s != NULL && s->commandable; });
	gSelected = CeSelLead(gSel);
	for (int i = 0; i < gCount; ++i) gSquads[i].selected = CeSelHas(gSel, gSquads[i].ordinal);
}

int CommandableCount()
{
	int n = 0;
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].commandable) ++n;
	return n;
}

void SelectOnly(int ordinal);		// below, with the orders

// The next (step +1) or previous (-1) squad that takes orders, after the picked one.
void Cycle(int step)
{
	if (gCount <= 0) return;
	int at = -1;
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].ordinal == gSelected) at = i;
	for (int n = 1; n <= gCount; ++n)
	{
		const int i = ((at < 0 ? (step > 0 ? -1 : 0) : at) + step * n % gCount + gCount) % gCount;
		if (gSquads[i].commandable) { SelectOnly(gSquads[i].ordinal); return; }
	}
}

// What the map says when a click lands on a friendly squad that takes no orders.
const char *WhyText(int why)
{
	switch (why)
	{
	case CE_WHY_SCRIPTED: return "That squad follows a mission script.";
	case CE_WHY_SPAWNED: return "That squad follows its lead unit.";
	case CE_WHY_AIRCRAFT: return "Only ground squads take orders.";
	case CE_WHY_OBJECTIVE: return "That squad is a mission objective.";
	default: return "That squad takes no orders.";
	}
}

const char *OrderName(const CeCmdSquad &s)
{
	// What the squad is doing under the map's order, as the order layer follows it (ce_cmdoutcome.h); then the order's own word.
	switch (s.outcome)
	{
	case CE_OUT_FIGHTING: return "Fighting";
	case CE_OUT_ARRIVED: return "Arrived";
	case CE_OUT_STUCK: return "Stuck";
	default: break;
	}
	switch (s.verb)
	{
	case CE_CMD_MOVE: return s.arrived ? "Arrived" : "Moving";
	case CE_CMD_ADVANCE: return s.arrived ? "Arrived" : "Advancing";
	case CE_CMD_HOLD: return "Holding";
	case CE_CMD_FALLBACK: return s.arrived ? "Arrived" : "Falling back";
	default: return "None";		// no order of the map's: the squad follows its own
	}
}

// ---- the layout and the terrain ----------------------------------------------------------------------------------------------------

float Px(float wx) { return L.mapX + (wx - gViewX) * L.k; }
float Py(float wy) { return L.mapY + (wy - gViewY) * L.k; }

void Lay()
{
	L.w = float(PLATFORM.GetWindowWidth());
	L.h = float(PLATFORM.GetWindowHeight());
	L.s = (L.w / 640.f < L.h / 480.f) ? L.w / 640.f : L.h / 480.f;
	const float margin = 12.f * L.s, top = 34.f * L.s, bottom = 44.f * L.s;
	L.panelW = 196.f * L.s;
	const float availW = L.w - L.panelW - 3.f * margin, availH = L.h - top - bottom;
	L.mapSide = floorf(availW < availH ? availW : availH);
	const float group = L.mapSide + margin + L.panelW;			// the map and its panel sit together in the middle of the window
	L.mapX = floorf((L.w - group) * 0.5f);
	L.mapY = floorf(top + (availH - L.mapSide) * 0.5f);
	L.panelX = L.mapX + L.mapSide + margin;
	L.k = gViewSpan > 0.f ? L.mapSide / gViewSpan : 1.f;
}

void World()
{
	int x0 = 0, y0 = 0, x1 = MAP.mGridWidth, y1 = MAP.mGridHeight;
	if (SExtents *e = MAP.GetExtents())
		if (e->mMaxX > e->mMinX && e->mMaxY > e->mMinY) { x0 = e->mMinX; y0 = e->mMinY; x1 = e->mMaxX; y1 = e->mMaxY; }
	gBoundsX0 = float(x0);
	gBoundsY0 = float(y0);
	gBoundsX1 = float(x1);
	gBoundsY1 = float(y1);
}

float FullSpan()
{
	const float w = gBoundsX1 - gBoundsX0, h = gBoundsY1 - gBoundsY0;
	return w > h ? w : h;
}

// The window onto the world is a square of this span, kept inside the world's own square.
// The live view (gLooseBounds) looks at a slant, so the near part of its square falls off the foot of the window: its centre may come to the world's edge
// and its span reach half again the world's, so that the island's near shore can be brought into sight; the sea beyond the edge is drawn.
void SetView(float centreX, float centreY, float span)
{
	// The live view stops zooming out where the ground still reads (past about 260 units the haze turns it into a dark plan: the R7 'map' again).
	const float full = FullSpan(), most = gLooseBounds ? (std::min)(full * 1.5f, 260.f) : full;
	if (span > most) span = most;
	const float least = gLooseBounds ? 16.f : 48.f;	// the live view may come closer than the paused map (its framing: kLiveOpenSpan)
	if (span < least) span = least;
	const float midX = (gBoundsX0 + gBoundsX1) * 0.5f, midY = (gBoundsY0 + gBoundsY1) * 0.5f;
	const float slack = (span < full ? (full - span) * 0.5f : 0.f) + (gLooseBounds ? (std::min)(span, full) * 0.5f : 0.f);
	if (centreX < midX - slack) centreX = midX - slack;
	if (centreX > midX + slack) centreX = midX + slack;
	if (centreY < midY - slack) centreY = midY - slack;
	if (centreY > midY + slack) centreY = midY + slack;
	gViewSpan = span;
	gViewX = centreX - span * 0.5f;
	gViewY = centreY - span * 0.5f;
	gTerrainDirty = true;
}

// In a Commander match, the rectangle round the commander's base (his side's live buildings): the view opens with it in sight.
bool MatchBase(float *x0, float *y0, float *x1, float *y1)
{
	if (!CeCmdMatch()) return false;
	CeCmdMatchState m;
	CeCmdMatchRead(&m);
	if (m.buildings[0] <= 0) return false;
	*x0 = m.baseX0; *y0 = m.baseY0; *x1 = m.baseX1; *y1 = m.baseY1;
	return true;
}

// The window on the action: the player's squads, the enemy squads within 160 units of them, the pilot when he is that
// near and, in a match, the player's base, biased onto the island's dry land when coastal squads hug one shore, never tighter than 128 units.
void FitView()
{
	float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
	float fx0 = 1e30f, fy0 = 1e30f, fx1 = -1e30f, fy1 = -1e30f;
	auto add = [&](float x, float y) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; };
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].friendly)
		{
			add(gSquads[i].x, gSquads[i].y);
			if (gSquads[i].x < fx0) fx0 = gSquads[i].x;
			if (gSquads[i].x > fx1) fx1 = gSquads[i].x;
			if (gSquads[i].y < fy0) fy0 = gSquads[i].y;
			if (gSquads[i].y > fy1) fy1 = gSquads[i].y;
		}
	if (x1 < x0)
	{
		for (int i = 0; i < gCount; ++i)
			if (gSquads[i].visible) add(gSquads[i].x, gSquads[i].y);
	}
	else
	{
		const float ax0 = x0 - 160.f, ay0 = y0 - 160.f, ax1 = x1 + 160.f, ay1 = y1 + 160.f;
		for (int i = 0; i < gCount; ++i)
			if (!gSquads[i].friendly && gSquads[i].visible && gSquads[i].x >= ax0 && gSquads[i].x <= ax1 && gSquads[i].y >= ay0 && gSquads[i].y <= ay1)
				add(gSquads[i].x, gSquads[i].y);
		if (CBattleEngine *engine = GAME.GetPlayer(0) ? GAME.GetPlayer(0)->GetBattleEngine() : NULL)
		{
			const float ex = engine->GetPos().X, ey = engine->GetPos().Y;
			if (ex >= ax0 && ex <= ax1 && ey >= ay0 && ey <= ay1) add(ex, ey);
		}
		float bx0, by0, bx1, by1;
		if (MatchBase(&bx0, &by0, &bx1, &by1)) { add(bx0, by0); add(bx1, by1); }
	}
	if (x1 < x0) { SetView((gBoundsX0 + gBoundsX1) * 0.5f, (gBoundsY0 + gBoundsY1) * 0.5f, FullSpan()); return; }
	float span = ((x1 - x0) > (y1 - y0) ? (x1 - x0) : (y1 - y0)) * 1.35f + 24.f;
	if (span < 128.f) span = 128.f;
	float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
	// Sample a 24 x 24 heightfield grid across world bounds (Z grows downward: h < water is dry land above sea level).
	const int kLandSamples = 24;
	float lx0 = 1e30f, ly0 = 1e30f, lx1 = -1e30f, ly1 = -1e30f;
	if (gBoundsX1 > gBoundsX0 && gBoundsY1 > gBoundsY0)
	{
		const float water = MAP.GetWaterLevel();
		const float cellX = (gBoundsX1 - gBoundsX0) / float(kLandSamples);
		const float cellY = (gBoundsY1 - gBoundsY0) / float(kLandSamples);
		for (int j = 0; j < kLandSamples; ++j)
		{
			const float wy = gBoundsY0 + (float(j) + 0.5f) * cellY;
			if (wy < 0.f || (MAP.mGridHeight > 1 && wy >= float(MAP.mGridHeight - 1))) continue;
			for (int i = 0; i < kLandSamples; ++i)
			{
				const float wx = gBoundsX0 + (float(i) + 0.5f) * cellX;
				if (wx < 0.f || (MAP.mGridWidth > 1 && wx >= float(MAP.mGridWidth - 1))) continue;
				const float h = MAP.Collide(FVector(wx, wy, 0.f));
				if (std::isfinite(h) && h < water)
				{
					if (wx < lx0) lx0 = wx;
					if (wx > lx1) lx1 = wx;
					if (wy < ly0) ly0 = wy;
					if (wy > ly1) ly1 = wy;
				}
			}
		}
	}
	if (lx0 <= lx1)
	{
		const float padX = (std::max)(24.f, (gBoundsX1 - gBoundsX0) / float(kLandSamples) * 0.6f);
		const float padY = (std::max)(24.f, (gBoundsY1 - gBoundsY0) / float(kLandSamples) * 0.6f);
		const float px0 = lx0 - padX, px1 = lx1 + padX;
		const float py0 = ly0 - padY, py1 = ly1 + padY;
		if (px1 - px0 <= span) cx = (px0 + px1) * 0.5f;
		else cx = (std::max)(px0 + span * 0.5f, (std::min)(px1 - span * 0.5f, cx));
		if (py1 - py0 <= span) cy = (py0 + py1) * 0.5f;
		else cy = (std::max)(py0 + span * 0.5f, (std::min)(py1 - span * 0.5f, cy));
		if (fx0 <= fx1)
		{
			const float keep = 0.38f * span;
			const float minCX = fx1 - keep, maxCX = fx0 + keep;
			const float minCY = fy1 - keep, maxCY = fy0 + keep;
			if (minCX <= maxCX) cx = (std::max)(minCX, (std::min)(maxCX, cx));
			if (minCY <= maxCY) cy = (std::max)(minCY, (std::min)(maxCY, cy));
		}
	}
	SetView(cx, cy, span);
}

// Brings the picked squad into view (Tab can pick one off the edge or off the map): the view recentres on it when it is near or past the edge.
void Reveal(int ordinal)
{
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].ordinal == ordinal)
		{
			const float m = 0.12f * gViewSpan;
			if (gSquads[i].x < gViewX + m || gSquads[i].y < gViewY + m || gSquads[i].x > gViewX + gViewSpan - m || gSquads[i].y > gViewY + gViewSpan - m)
				SetView(gSquads[i].x, gSquads[i].y, gViewSpan);
			return;
		}
}

void ZoomAt(float factor, float px, float py)
{
	const float wx = gViewX + (px - L.mapX) / L.k, wy = gViewY + (py - L.mapY) / L.k;	// the world point under the pointer stays under it
	const float span = gViewSpan * factor;
	const float u = (px - L.mapX) / L.mapSide, v = (py - L.mapY) / L.mapSide;
	SetView(wx - u * span + span * 0.5f, wy - v * span + span * 0.5f, span);
}

void Pan(float dx, float dy) { SetView(gViewX + gViewSpan * 0.5f + dx * gViewSpan, gViewY + gViewSpan * 0.5f + dy * gViewSpan, gViewSpan); }


DWORD Mix(DWORD a, DWORD b, float t)
{
	if (t < 0.f) t = 0.f;
	if (t > 1.f) t = 1.f;
	DWORD out = 0xff000000;
	for (int shift = 0; shift <= 16; shift += 8)
	{
		const float ca = float((a >> shift) & 255), cb = float((b >> shift) & 255);
		int ch = int(ca + (cb - ca) * t + 0.5f);
		if (ch < 0) ch = 0;
		if (ch > 255) ch = 255;
		out |= DWORD(ch) << shift;
	}
	return out;
}

DWORD Shade(DWORD c, float f)
{
	DWORD out = 0xff000000;
	for (int shift = 0; shift <= 16; shift += 8)
	{
		int ch = int(float((c >> shift) & 255) * f);
		if (ch < 0) ch = 0;
		if (ch > 255) ch = 255;
		out |= DWORD(ch) << shift;
	}
	return out;
}

// The ground's colours from its heights over the view window, rebuilt when the view changes: water by depth, land by height, lit from the
// upper left. The map's z grows downward, so the surface of the water lies above every sample larger than its level.
void BuildTerrain()
{
	const float cell = gViewSpan / float(kGrid);
	static float height[kGrid * kGrid];
	static bool inside[kGrid * kGrid];
	float lo = 1e30f, hi = -1e30f;
	for (int j = 0; j < kGrid; ++j)
		for (int i = 0; i < kGrid; ++i)
		{
			const float wx = gViewX + (float(i) + 0.5f) * cell, wy = gViewY + (float(j) + 0.5f) * cell;
			const bool in = wx >= gBoundsX0 && wx < gBoundsX1 && wy >= gBoundsY0 && wy < gBoundsY1;
			inside[j * kGrid + i] = in;
			const float h = in ? MAP.Collide(FVector(wx, wy, 0.f)) : 0.f;
			height[j * kGrid + i] = h;
			if (in && h < lo) lo = h;
			if (in && h > hi) hi = h;
		}
	if (lo > hi) { lo = 0.f; hi = 1.f; }
	const float water = MAP.GetWaterLevel();
	const float toLand = water > lo ? water - lo : 1.f, toSea = hi > water ? hi - water : 1.f;
	for (int j = 0; j < kGrid; ++j)
		for (int i = 0; i < kGrid; ++i)
		{
			if (!inside[j * kGrid + i]) { gTerrain[j * kGrid + i] = 0xff0c1218; continue; }
			const float h = height[j * kGrid + i];
			const float west = height[j * kGrid + (i > 0 ? i - 1 : i)], east = height[j * kGrid + (i < kGrid - 1 ? i + 1 : i)];
			const float north = height[(j > 0 ? j - 1 : j) * kGrid + i], south = height[(j < kGrid - 1 ? j + 1 : j) * kGrid + i];
			// a slope that rises toward the light (upper left) is brighter: a smaller z is higher
			const float slope = ((east - west) + (south - north)) * 0.5f * (6.f / (cell > 0.5f ? cell : 0.5f));
			float light = 1.f + slope / (toLand * 0.18f);
			if (light < 0.7f) light = 0.7f;
			if (light > 1.3f) light = 1.3f;
			DWORD c;
			if (h > water)
				c = Mix(0xff2b5f86, 0xff0f2640, (h - water) / toSea);
			else
			{
				const float up = (water - h) / toLand;
				c = up < 0.5f ? Mix(0xff2f5a2c, 0xff77853f, up * 2.f) : Mix(0xff77853f, 0xffcdc7b4, (up - 0.5f) * 2.f);
				c = Shade(c, light);
			}
			gTerrain[j * kGrid + i] = c;
		}
	gTerrainDirty = false;
	gTerrainTexDirty = true;
}

// ---- drawing -----------------------------------------------------------------------------------------------------------------------

void Box(float x, float y, float w, float h, DWORD colour)
{
	if (w <= 0.f || h <= 0.f) return;
	CSPRITERENDERER::DrawColouredSprite(x, y, 0.001f, gWhite, colour, w / gWhite->GetRealWidth(), h / gWhite->GetRealHeight());
}

void Outline(float x, float y, float w, float h, float t, DWORD colour)
{
	Box(x, y, w, t, colour);
	Box(x, y + h - t, w, t, colour);
	Box(x, y, t, h, colour);
	Box(x + w - t, y, t, h, colour);
}

// The HUD's own capsule (the objective bar the radio messages and the museum's labels use): a dark translucent body between the curved end caps,
// drawn using HUD.mObjectiveLeft/Right/Inner* for both Forseti (native cyan) and Muspell (fiery amber/crimson). Lit draws a brighter body over it.
// Without the HUD's art (host unit-test drivers), falls back cleanly to shaded boxes.
const DWORD kCapsuleBody = 0xb0040a14, kCapsuleCap = 0xff38c8e8, kCapsuleLit = 0x8038c8e8;
void BeginDraw();

bool HasHudCapsuleArt()
{
	return HUD.mObjectiveLeft && HUD.mObjectiveRight && HUD.mObjectiveInnerLeft && HUD.mObjectiveInnerCentre && HUD.mObjectiveInnerRight;
}

void DrawTintedHudCap(float x, float y, CDXTexture *tex, float turn, DWORD colour, float scaleX, float scaleY)
{
	if (!tex) return;
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	CSPRITERENDERER::DrawSprite(x, y, 0.009f, tex, SA_CENTRE, SPT_NONE, 1.f, turn, colour, scaleX, scaleY, 0.f, 1.f, 0.f, 1.f);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

void DrawBeaCurvedPanel(float x, float y, float w, float h, float capMax, DWORD fill, DWORD capTint, bool nativeCyan, float alpha, bool withOuterCaps = true)
{
	if (w <= 0.f || h <= 0.f || alpha <= 0.f) return;
	const float aClamp = alpha > 1.f ? 1.f : alpha;
	const auto fade = [aClamp](DWORD c) {
		DWORD a = DWORD(float(c >> 24) * aClamp + 0.5f);
		if (a > 255u) a = 255u;
		return (a << 24) | (c & 0x00ffffffu);
	};
	if (!HasHudCapsuleArt())
	{
		const float bt = (std::max)(1.f, floorf(L.s));
		Box(x, y, w, h, fade(fill));
		Outline(x, y, w, h, bt, fade(capTint));
		return;
	}
	const float drawH = h < 10.f ? 10.f : h;
	const float capW = (capMax > 0.f && capMax < drawH) ? capMax : drawH;
	const float scaleX = capW * (1.f / 64.f);
	const float scaleY = drawH * (1.f / 64.f);
	const float bodyW = w > capW ? (w - capW) : 0.f;
	const float leftX = x + capW * 0.5f;
	const float rightX = leftX + bodyW;
	const float cy = y + h * 0.5f;

	HUD.SetOverlayRenderStates();
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.Apply();

	if (aClamp >= 1.f || !withOuterCaps)
	{
		const DWORD bodyCol = fade(fill);
		CSPRITERENDERER::DrawSprite(leftX, cy, 0.009f, HUD.mObjectiveInnerLeft, (ESpriteAlignment)7, SPT_NONE, 1.f, 0.f, bodyCol, scaleX, scaleY, 0.f, 1.f, 0.f, 1.f);
		CSPRITERENDERER::DrawSprite(rightX, cy, 0.009f, HUD.mObjectiveInnerRight, SA_8, SPT_NONE, 1.f, 0.f, bodyCol, scaleX, scaleY, 0.f, 1.f, 0.f, 1.f);
		const float seg = scaleX * 64.f;
		if (seg > 0.5f && bodyW > 0.f)
		{
			float n = bodyW / seg;
			float px = leftX;
			while (n >= 1.f)
			{
				CSPRITERENDERER::DrawSprite(px, cy, 0.009f, HUD.mObjectiveInnerCentre, SA_8, SPT_NONE, 1.f, 0.f, bodyCol, scaleX, scaleY, 0.f, 1.f, 0.f, 1.f);
				n -= 1.f;
				px += seg;
			}
			if (n > 0.f)
				CSPRITERENDERER::DrawSprite(px, cy, 0.009f, HUD.mObjectiveInnerCentre, SA_8, SPT_FILL_RIGHT, n, 0.f, bodyCol, scaleX, scaleY, 0.f, 1.f, 0.f, 1.f);
		}
		if (withOuterCaps)
		{
			if (nativeCyan)
			{
				CSPRITERENDERER::DrawSprite(leftX, cy, 0.009f, HUD.mObjectiveLeft, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xffffffff, scaleX, scaleY, 0.f, 1.f, 0.f, 1.f);
				CSPRITERENDERER::DrawSprite(rightX, cy, 0.009f, HUD.mObjectiveRight, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xffffffff, scaleX, scaleY, 0.f, 1.f, 0.f, 1.f);
			}
			else
			{
				const DWORD tint = fade(capTint);
				DrawTintedHudCap(leftX, cy, HUD.mObjectiveLeft, 0.f, tint, scaleX, scaleY);
				DrawTintedHudCap(rightX, cy, HUD.mObjectiveRight, 0.f, tint, scaleX, scaleY);
				// True SRCALPHA/ONE core glow (NOT HUD.SetAdditive ONE/ONE) so edges stay rich crimson-amber while core glows warm orange
				LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
				const DWORD coreGlow = fade(0x4eff9c28);
				DrawTintedHudCap(leftX, cy, HUD.mObjectiveLeft, 0.f, coreGlow, scaleX, scaleY);
				DrawTintedHudCap(rightX, cy, HUD.mObjectiveRight, 0.f, coreGlow, scaleX, scaleY);
				LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			}
		}
	}
	else
	{
		const float open = 0.5f + 0.5f * aClamp;
		const float openSX = scaleX * open;
		const float openSY = scaleY * open;
		const float turn = 1.f - open;
		const float cx = x + w * 0.5f;
		if (nativeCyan)
		{
			const DWORD col = fade(0xffffffff);
			CSPRITERENDERER::DrawSprite(cx, cy, 0.009f, HUD.mObjectiveLeft, SA_CENTRE, SPT_NONE, 1.f, turn, col, openSX, openSY, 0.f, 1.f, 0.f, 1.f);
			CSPRITERENDERER::DrawSprite(cx, cy, 0.009f, HUD.mObjectiveRight, SA_CENTRE, SPT_NONE, 1.f, turn, col, openSX, openSY, 0.f, 1.f, 0.f, 1.f);
		}
		else
		{
			const DWORD col = fade(capTint);
			DrawTintedHudCap(cx, cy, HUD.mObjectiveLeft, turn, col, openSX, openSY);
			DrawTintedHudCap(cx, cy, HUD.mObjectiveRight, turn, col, openSX, openSY);
		}
	}
	BeginDraw();
}

void CapsuleBar(float x, float y, float w, float h, float alpha = 1.f, bool lit = false, int allegiance = 0)
{
	if (w <= 0.f || h <= 0.f || alpha <= 0.f) return;
	const float aClamp = alpha > 1.f ? 1.f : alpha;
	const auto fade = [aClamp](DWORD c) {
		int a = int(float(c >> 24) * aClamp + 0.5f);
		if (a < 0) a = 0;
		if (a > 255) a = 255;
		return (DWORD(a) << 24) | (c & 0xffffff);
	};
	if (HasHudCapsuleArt())
	{
		if (allegiance == 1)
		{
			DrawBeaCurvedPanel(x, y, w, h, 0.f, 0xb41a0b08, 0xffe64416, false, alpha, true);
		}
		else
		{
			HUD.RenderObjectiveBar(x + w * 0.5f, y + h * 0.5f, w > h ? w - h : 0.f, h, alpha >= 1.f ? 1.f : 0.5f + 0.5f * alpha);
			BeginDraw();
		}
	}
	else if (allegiance == 1)
	{
		const CeCmdHudPalette pal = CeCmdHudMuspellPalette();
		const float cap = (std::max)(2.f, 2.5f * L.s);
		const float bt = (std::max)(1.f, floorf(L.s));
		Box(x + cap, y, w - 2.f * cap, h, fade(pal.barFill));
		Outline(x + cap, y, w - 2.f * cap, h, bt, fade(pal.barBorder));
		Box(x + cap + bt, y + bt, w - 2.f * (cap + bt), bt, fade(pal.barTopBevel));
		Box(x, y + h * 0.12f, cap, h * 0.76f, fade(pal.cyanAccent));
		Box(x + w - cap, y + h * 0.12f, cap, h * 0.76f, fade(pal.cyanAccent));
	}
	else
	{
		const float cap = (std::max)(2.f, 2.f * L.s);
		Box(x + h * 0.25f, y, w - h * 0.5f, h, fade(kCapsuleBody));
		Box(x, y + h * 0.15f, cap, h * 0.7f, fade(kCapsuleCap));
		Box(x + w - cap, y + h * 0.15f, cap, h * 0.7f, fade(kCapsuleCap));
	}
	if (lit) Box(x + h * 0.5f, y + h * 0.18f, w - h, h * 0.64f, fade(allegiance == 1 ? 0x80ff8828 : kCapsuleLit));
}

// A solid line t pixels wide from one window point to another: the sprite renderer's quad, turned along the line (the map editor's line).
void Seg(float x0, float y0, float x1, float y1, float t, DWORD colour)
{
	const float dx = x1 - x0, dy = y1 - y0, length = sqrtf(dx * dx + dy * dy);
	if (length <= 0.f) return;
	CSPRITERENDERER::DrawColouredSprite((x0 + x1) * 0.5f - length * 0.5f, (y0 + y1) * 0.5f - t * 0.5f, 0.001f, gWhite, colour, length / gWhite->GetRealWidth(),
									   t / gWhite->GetRealHeight(), SA_TOP_LEFT, SPT_NONE, 1.f, atan2f(dy, dx));
}

// A dotted line from one point to another, square dots t wide every 2.5 t.
void Dots(float x0, float y0, float x1, float y1, float t, DWORD colour)
{
	const float dx = x1 - x0, dy = y1 - y0, length = sqrtf(dx * dx + dy * dy), step = t * 2.5f;
	const int n = int(length / step);
	for (int i = 0; i <= n; ++i)
	{
		const float f = n ? float(i) / float(n) : 0.f;
		Box(floorf(x0 + dx * f - t * 0.5f), floorf(y0 + dy * f - t * 0.5f), t, t, colour);
	}
}

unsigned Convert(const char *text, WCHAR *out, unsigned room)
{
	unsigned n = 0;
	for (const char *c = text; *c && n + 1 < room; ++c)
		out[n++] = gFont->CanDisplay(WCHAR((unsigned char)*c)) ? WCHAR((unsigned char)*c) : L'-';
	out[n] = 0;
	return n;
}

float TextWidth(const char *text, float scale)
{
	WCHAR w[160];
	Convert(text, w, 160);
	SIZE extent;
	gFont->GetTextExtent(w, &extent);
	return float(extent.cx) * scale;
}

// One line of the small font; wider than maxWidth (when given) it is narrowed to fit.
void Text(float x, float y, DWORD colour, const char *text, float maxWidth = 0.f, bool centred = false, float baseScale = 0.f)
{
	WCHAR w[160];
	Convert(text, w, 160);
	SIZE extent;
	gFont->GetTextExtent(w, &extent);
	float scale = baseScale > 0.f ? baseScale : L.s;
	if (maxWidth > 0.f && float(extent.cx) * scale > maxWidth && extent.cx > 0) scale = maxWidth / float(extent.cx);
	if (centred) x -= float(extent.cx) * scale * 0.5f;
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	gFont->DrawTextWithShadow(x, y, colour, w, 0, 0.f, scale, scale);
	LT.SetModulate(0);
}

float LineHeight() { return float(gFont->GetHeight()) * L.s; }

void BeginDraw()
{
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.SetModulate(0);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ALPHAREF, 8);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.Apply();
}

void EndDraw()
{
	LT.SetModulate(0);
	LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
}

// The colours, each with one meaning (the interface guide's palette): the friendly side in the green and the enemy in the red the live picture marks
// their units with (ce_cmdview.cpp), white for content and for what is picked, yellow for the one thing under the pointer or armed (focus), grey for notes and for what cannot be used.
// An order's line and goal are one quiet white: the squad's card says which order it is.
const DWORD kFriend = 0xff62d862, kFriendDim = 0xff3f7a3f, kEnemy = 0xffe45656, kPilot = 0xff70e0ff, kPick = 0xffffffff, kFocus = 0xffffff00, kText = 0xffeef2f6,
			kQuiet = 0xffa8a8a8, kOrderLine = 0x99ffffff, kMoveColour = kOrderLine, kAdvanceColour = kOrderLine, kFallbackColour = kOrderLine;
// The game's message box panel and its edge (CFEMessBox's colours), for every panel the view draws.
const DWORD kPanel = 0xaf000000, kEdge = 0xff7f7f7f;

float BoxHalf(const CeCmdSquad &s)
{
	float half = 5.f + sqrtf(float(s.members)) * 1.1f;
	return floorf(half * L.s + 0.5f);
}

DWORD OrderColour(int verb)
{
	return verb == CE_CMD_ADVANCE ? kAdvanceColour : verb == CE_CMD_FALLBACK ? kFallbackColour : verb ? kMoveColour : kQuiet;
}

// The letter a squad's box wears for its standing order.
const char *OrderLetter(int verb)
{
	switch (verb)
	{
	case CE_CMD_MOVE: return "M";
	case CE_CMD_ADVANCE: return "A";
	case CE_CMD_HOLD: return "H";
	case CE_CMD_FALLBACK: return "F";
	default: return NULL;
	}
}

bool OnMap(float px, float py) { return px >= L.mapX && py >= L.mapY && px < L.mapX + L.mapSide && py < L.mapY + L.mapSide; }
// A mark of half-size h centred on the point, all of it inside the map's square.
bool InsideMap(float px, float py, float h) { return px - h >= L.mapX && py - h >= L.mapY && px + h < L.mapX + L.mapSide && py + h < L.mapY + L.mapSide; }

// The part of a segment inside the map's square (Liang and Barsky); false when none of it is.
bool ClipToMap(float &x0, float &y0, float &x1, float &y1)
{
	const float left = L.mapX, right = L.mapX + L.mapSide, top = L.mapY, bottom = L.mapY + L.mapSide, dx = x1 - x0, dy = y1 - y0;
	float t0 = 0.f, t1 = 1.f;
	const float p[4] = {-dx, dx, -dy, dy}, q[4] = {x0 - left, right - x0, y0 - top, bottom - y0};
	for (int i = 0; i < 4; ++i)
	{
		if (p[i] == 0.f) { if (q[i] < 0.f) return false; continue; }
		const float r = q[i] / p[i];
		if (p[i] < 0.f) { if (r > t1) return false; if (r > t0) t0 = r; }
		else { if (r < t0) return false; if (r < t1) t1 = r; }
	}
	const float nx0 = x0 + t0 * dx, ny0 = y0 + t0 * dy, nx1 = x0 + t1 * dx, ny1 = y0 + t1 * dy;
	x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
	return true;
}

// One frame's terrain from the texture. False when it cannot be made or filled: the caller draws the cells.
IDirect3DTexture9 *gTerrainTexNative = NULL;
template <class T>
bool TerrainTextureIs32Bit(const T *t, decltype(t->mTexFormat) * = 0) { return t->mTexFormat == TEXFMT_A8R8G8B8; }
bool TerrainTextureIs32Bit(const void *) { return true; }

bool DrawTerrainFromTexture()
{
	// BEA_PORT_COMMANDER_TERRAIN=cells draws the cells one by one as before (a test switch, to compare the two on one build).
	static int cells = -1;
	if (cells < 0) { const char *mode = getenv("BEA_PORT_COMMANDER_TERRAIN"); cells = mode && !strcmp(mode, "cells"); }
	if (cells || gTerrainTextureFailed) return false;
	if (!gTerrainTexture)
	{
		CDXTexture *texture = new (MEMTYPE_TEXTURE) CDXTexture;
		texture->InitEmpty(kGrid, kGrid, TEXFMT_A8R8G8B8, 1, 0, 1);
		if (!texture->GetTexture() || texture->GetRealWidth() != kGrid || texture->GetRealHeight() != kGrid || !TerrainTextureIs32Bit(texture))
		{
			texture->Release();
			texture->ForceDelete();
			gTerrainTextureFailed = true;
			return false;
		}
		gTerrainTexture = texture;
		gTerrainTexNative = NULL;
		gTerrainTexDirty = true;
	}
	IDirect3DTexture9 *native = gTerrainTexture->GetTexture();
	if (!native || gTerrainTexture->GetRealWidth() != kGrid || gTerrainTexture->GetRealHeight() != kGrid || !TerrainTextureIs32Bit(gTerrainTexture))
		return false;
	if (native != gTerrainTexNative)
		gTerrainTexDirty = true;
	if (gTerrainTexDirty)
	{
		D3DLOCKED_RECT lr;
		if (FAILED(gTerrainTexture->GetTexture()->LockRect(0, &lr, NULL, D3DLOCK_NOSYSLOCK)))
			return false;
		if (!lr.pBits || lr.Pitch < int(sizeof(DWORD) * kGrid))
		{
			native->UnlockRect(0);
			return false;
		}
		for (int j = 0; j < kGrid; ++j)
		{
			memcpy((unsigned char *)lr.pBits + j * lr.Pitch, &gTerrain[j * kGrid], sizeof(DWORD) * kGrid);
		}
		gTerrainTexture->GetTexture()->UnlockRect(0);
		gTerrainTexNative = native;
		gTerrainTexDirty = false;
	}
	// Nearest-neighbour: the map is a grid of flat cells, not a smoothed picture.
	IDirect3DDevice9 *device = LT.GetDevice();
	DWORD magnify = D3DTEXF_LINEAR, minify = D3DTEXF_LINEAR;
	if (device)
	{
		device->GetSamplerState(0, D3DSAMP_MAGFILTER, &magnify);
		device->GetSamplerState(0, D3DSAMP_MINFILTER, &minify);
		device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	}
	// The cells' edges are the floor of their positions. A quad covers the pixels whose centres it holds, but a pixel samples the texture at the position of its
	// corner, so the texel boundaries of a plain quad fall on the first pixel at or past them (the ceiling): every edge a pixel too far right and down (frame
	// by frame against the cells, 99,676 of 307,200 pixels differed). The quad stays on the map's pixels, and its texture coordinates start a hair under one
	// pixel along (0.999 of a pixel, in the units of the map's side) so that every boundary falls on the floor, as the cells' do.
	const float shiftU = 0.999f / L.mapSide, shiftV = 0.999f / L.mapSide;
	LT.SetModulate(0);
	CSPRITERENDERER::mBatch = TRUE;
	CSPRITERENDERER::DrawSprite(L.mapX, L.mapY, 0.001f, gTerrainTexture, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, 0xffffffff, L.mapSide / gTerrainTexture->GetRealWidth(),
								L.mapSide / gTerrainTexture->GetRealHeight(), shiftU, 1.f + shiftU, shiftV, 1.f + shiftV);
	FASTVB.Render();
	CSPRITERENDERER::mBatch = FALSE;
	if (device)
	{
		device->SetSamplerState(0, D3DSAMP_MAGFILTER, magnify);
		device->SetSamplerState(0, D3DSAMP_MINFILTER, minify);
	}
	return true;
}

void FreeTerrainTexture()
{
	if (gTerrainTexture)
	{
		gTerrainTexture->Release();
		gTerrainTexture->ForceDelete();
		gTerrainTexture = NULL;
	}
	gTerrainTexNative = NULL;
	gTerrainTextureFailed = false;
	gTerrainTexDirty = true;
}

// One batch through the game's fast vertex buffer, as the compass draws its markers (the buffer flushes itself when it fills).
void DrawTerrain()
{
	if (DrawTerrainFromTexture()) return;
	const float cell = L.mapSide / float(kGrid);
	LT.SetModulate(0);
	CSPRITERENDERER::mBatch = TRUE;
	for (int j = 0; j < kGrid; ++j)
	{
		const float y0 = floorf(L.mapY + float(j) * cell), y1 = floorf(L.mapY + float(j + 1) * cell);
		for (int i = 0; i < kGrid; ++i)
		{
			const float x0 = floorf(L.mapX + float(i) * cell), x1 = floorf(L.mapX + float(i + 1) * cell);
			Box(x0, y0, x1 - x0, y1 - y0, gTerrain[j * kGrid + i]);
		}
	}
	FASTVB.Render();
	CSPRITERENDERER::mBatch = FALSE;
}

void DrawGrid()
{
	// a line every 16, 32, 64 or 128 world units (about 40 pixels apart or more), and the frame
	const float t = floorf(L.s) > 1.f ? floorf(L.s) : 1.f;
	float step = 16.f;
	while (step * L.k < 40.f * L.s && step < 256.f) step *= 2.f;
	for (float wx = ceilf(gViewX / step) * step; wx < gViewX + gViewSpan; wx += step) Box(floorf(Px(wx)), L.mapY, t, L.mapSide, 0x30ffffff);
	for (float wy = ceilf(gViewY / step) * step; wy < gViewY + gViewSpan; wy += step) Box(L.mapX, floorf(Py(wy)), L.mapSide, t, 0x30ffffff);
	Outline(L.mapX - t, L.mapY - t, L.mapSide + 2.f * t, L.mapSide + 2.f * t, t, 0xff7a8794);
}

std::uint64_t MapPulseStartNs();

void DrawSquads()
{
	const float t = floorf(L.s) > 1.f ? floorf(L.s) : 1.f;
	float pulse = 0.f;
	const std::uint64_t pulseStart = MapPulseStartNs();
	if (pulseStart != 0 && gSelected < 0)
	{
		const double elapsed = SecondsBetween(pulseStart, Ns());
		if (elapsed < 1.5)
		{
			const float fade = 1.f - float(elapsed / 1.5);
			const float wave = 0.5f - 0.5f * cosf(float(elapsed) * (4.f * 3.14159265f / 1.5f));
			pulse = wave * fade;
		}
	}
	// the enemy squads, below everything of ours
	for (int i = 0; i < gCount; ++i)
	{
		const CeCmdSquad &s = gSquads[i];
		const float h = floorf(2.5f * L.s + 0.5f + sqrtf(float(s.members)) * 0.5f * L.s);
		if (s.friendly || !s.visible || !InsideMap(Px(s.x), Py(s.y), h)) continue;
		Box(floorf(Px(s.x) - h), floorf(Py(s.y) - h), 2.f * h, 2.f * h, kEnemy);
		Outline(floorf(Px(s.x) - h), floorf(Py(s.y) - h), 2.f * h, 2.f * h, t, 0xff3a0e0e);
	}
	// the standing orders: a dotted line to the goal and a mark on it, clipped to the map
	for (int i = 0; i < gCount; ++i)
	{
		const CeCmdSquad &s = gSquads[i];
		if (!s.friendly || (s.verb != CE_CMD_MOVE && s.verb != CE_CMD_ADVANCE && s.verb != CE_CMD_FALLBACK)) continue;
		const DWORD colour = OrderColour(s.verb);
		const float gx = Px(s.gx), gy = Py(s.gy), m = 5.f * L.s;
		float x0 = Px(s.x), y0 = Py(s.y), x1 = gx, y1 = gy;
		if (!s.arrived && ClipToMap(x0, y0, x1, y1)) Dots(x0, y0, x1, y1, t * 1.5f, colour);
		if (OnMap(gx, gy))
		{
			Box(floorf(gx - m), floorf(gy - t), 2.f * m, 2.f * t, colour);
			Box(floorf(gx - t), floorf(gy - m), 2.f * t, 2.f * m, colour);
		}
	}
	// our squads: the boxes, then their counts, so a neighbour's box never hides a count
	for (int i = 0; i < gCount; ++i)
	{
		const CeCmdSquad &s = gSquads[i];
		if (!s.friendly || !InsideMap(Px(s.x), Py(s.y), BoxHalf(s) + 3.f * t)) continue;
		const float h = BoxHalf(s), x = floorf(Px(s.x)), y = floorf(Py(s.y));
		const DWORD colour = s.commandable ? kFriend : kFriendDim;
		const DWORD edge = (s.commandable && pulse > 0.f) ? Mix(colour, 0xffd8ffd8, 0.55f * pulse) : colour;
		if (s.commandable) Box(x - h, y - h, 2.f * h, 2.f * h, Shade(colour, 0.55f + 0.25f * pulse));
		Outline(x - h, y - h, 2.f * h, 2.f * h, 2.f * t, edge);
		if (s.commandable && pulse > 0.01f)
		{
			const DWORD halo = (DWORD(pulse * 150.f + 0.5f) << 24) | (kFriend & 0x00ffffffu);
			Outline(x - h - 2.f * t, y - h - 2.f * t, 2.f * (h + 2.f * t), 2.f * (h + 2.f * t), t, halo);
		}
		if (const char *letter = OrderLetter(s.verb)) Text(x, y - LineHeight() * 0.5f, OrderColour(s.verb), letter, 0.f, true);
		else if (s.vehicles) Box(x - h * 0.45f, y - t, h * 0.9f, 2.f * t, edge);	// a bar across a vehicle squad, a dot for infantry
		else Box(x - t, y - t, 2.f * t, 2.f * t, edge);
		if (s.selected) Outline(x - h - 3.f * t, y - h - 3.f * t, 2.f * (h + 3.f * t), 2.f * (h + 3.f * t), 2.f * t, kPick);		// every picked squad, not the lead alone
		if (s.ordinal == gHover) Outline(x - h - 5.f * t, y - h - 5.f * t, 2.f * (h + 5.f * t), 2.f * (h + 5.f * t), t, kFocus);
	}
	// the counts, each where it covers neither a box nor another count (the places are tried in the order below)
	struct Rect { float x0, y0, x1, y1; } taken[2 * kMaxSquads];
	int nTaken = 0;
	for (int i = 0; i < gCount; ++i)
	{
		const CeCmdSquad &s = gSquads[i];
		if (!s.friendly || !InsideMap(Px(s.x), Py(s.y), BoxHalf(s) + 3.f * t) || nTaken >= 2 * kMaxSquads - 2) continue;
		const float h = BoxHalf(s) + 3.f * t, x = floorf(Px(s.x)), y = floorf(Py(s.y));
		taken[nTaken++] = {x - h, y - h, x + h, y + h};
	}
	for (int i = 0; i < gCount; ++i)
	{
		const CeCmdSquad &s = gSquads[i];
		if (!s.friendly || !InsideMap(Px(s.x), Py(s.y), BoxHalf(s) + 3.f * t)) continue;
		char count[16];
		snprintf(count, sizeof(count), "%d", s.members);
		const float tw = TextWidth(count, L.s), th = LineHeight(), h = BoxHalf(s), x = floorf(Px(s.x)), y = floorf(Py(s.y)), gap = 2.f * t;
		// below, right, above and left of the box, then its four corners, then a line further out; the first that is clear, else the one that covers least
		const float spots[12][2] = {
			{x - tw * 0.5f, y + h + gap}, {x + h + gap + t, y - th * 0.5f}, {x - tw * 0.5f, y - h - gap - th}, {x - h - gap - t - tw, y - th * 0.5f},
			{x + h + gap, y + h + gap}, {x + h + gap, y - h - gap - th}, {x - h - gap - tw, y - h - gap - th}, {x - h - gap - tw, y + h + gap},
			{x - tw * 0.5f, y + h + gap + th}, {x - tw * 0.5f, y - h - gap - 2.f * th}, {x + h + gap + t + tw * 0.25f, y + th * 0.5f}, {x - h - gap - t - tw * 1.25f, y + th * 0.5f}};
		int pick = -1, fallback = 0;
		float least = 1e30f;
		for (int c = 0; c < 12; ++c)
		{
			float covered = 0.f;
			for (int k = 0; k < nTaken; ++k)
			{
				const float w = fminf(spots[c][0] + tw, taken[k].x1) - fmaxf(spots[c][0], taken[k].x0), v = fminf(spots[c][1] + th, taken[k].y1) - fmaxf(spots[c][1], taken[k].y0);
				if (w > 0.f && v > 0.f) covered += w * v;
			}
			if (covered == 0.f) { pick = c; break; }
			if (covered < least) { least = covered; fallback = c; }
		}
		if (pick < 0) pick = fallback;
		if (nTaken < 2 * kMaxSquads) taken[nTaken++] = {spots[pick][0], spots[pick][1], spots[pick][0] + tw, spots[pick][1] + th};
		Text(spots[pick][0], spots[pick][1], s.commandable ? kText : kQuiet, count);
	}
	// the pilot's engine
	if (CBattleEngine *engine = GAME.GetPlayer(0) ? GAME.GetPlayer(0)->GetBattleEngine() : NULL)
	{
		const float x = floorf(Px(engine->GetPos().X)), y = floorf(Py(engine->GetPos().Y)), m = 6.f * L.s;
		if (OnMap(x, y))
		{
			Box(x - m, y - t, 2.f * m, 2.f * t, kPilot);
			Box(x - t, y - m, 2.f * t, 2.f * m, kPilot);
			Outline(x - m * 0.6f, y - m * 0.6f, m * 1.2f, m * 1.2f, t, kPilot);
		}
	}
}

void DrawPanel()
{
	const float x = L.panelX, pad = 8.f * L.s, line = LineHeight() + 3.f * L.s;
	float y = L.mapY;
	Box(x, y, L.panelW, L.mapSide, kPanel);
	Outline(x, y, L.panelW, L.mapSide, floorf(L.s) > 1.f ? floorf(L.s) : 1.f, kEdge);
	y += pad;
	const float room = L.panelW - 2.f * pad;
	const CeCmdSquad *s = ByOrdinal(gSelected);
	char text[96];
	if (!s)
	{
		Text(x + pad, y, kText, "No Squad Picked", room);
		y += line * 1.5f;
		snprintf(text, sizeof(text), "%d squads take orders.", CommandableCount());
		Text(x + pad, y, kQuiet, text, room);
		y += line * 1.5f;
		Text(x + pad, y, kQuiet, "Click a squad of yours,", room);
		y += line;
		Text(x + pad, y, kQuiet, "or press Tab.", room);
		y += line * 2.f;
		if (const CeCmdSquad *h = ByOrdinal(gHover))
		{
			Text(x + pad, y, kText, h->unit[0] ? h->unit : (h->vehicles ? "Vehicles" : "Infantry"), room);
			y += line;
			snprintf(text, sizeof(text), "%d unit%s%s", h->members, h->members == 1 ? "" : "s", h->commandable ? "" : ", no orders");
			Text(x + pad, y, h->commandable ? kQuiet : kEnemy, text, room);
		}
		return;
	}
	// the picked squads: how many, then the lead's card (the squads ordered together are the selection, as in the live view)
	if (gSel.n > 1)
	{
		snprintf(text, sizeof(text), "%d squads picked", gSel.n);
		Text(x + pad, y, kText, text, room);
		y += line * 1.4f;
	}
	Text(x + pad, y, kText, s->unit[0] ? s->unit : (s->vehicles ? "Vehicles" : "Infantry"), room);
	y += line;
	if (s->peak > s->members) snprintf(text, sizeof(text), "%s, %d of %d units", s->vehicles ? "Vehicles" : "Infantry", s->members, s->peak);
	else snprintf(text, sizeof(text), "%s, %d unit%s", s->vehicles ? "Vehicles" : "Infantry", s->members, s->members == 1 ? "" : "s");
	Text(x + pad, y, kQuiet, text, room);
	y += line * 1.4f;
	snprintf(text, sizeof(text), "Order: %s", OrderName(*s));
	Text(x + pad, y, kText, text, room);
	y += line;
	if (s->deploys) Text(x + pad, y, kQuiet, "Hold only", room);
}

void DrawLegend()
{
	const float t = floorf(L.s) > 1.f ? floorf(L.s) : 1.f, line = LineHeight() + 3.f * L.s, x = L.panelX + 8.f * L.s, room = L.panelW - 16.f * L.s;
	float y = L.mapY + L.mapSide - 8.f * line - 8.f * L.s;
	const float m = 4.f * L.s;
	if (gUntracked > 0)		// the order layer's table is full: say so rather than leave squads off the map without a word
	{
		char text[48];
		snprintf(text, sizeof(text), "%d more squads are not shown", gUntracked);
		Text(x, y - line * 1.4f, kEnemy, text, room);
	}
	Outline(x, y + 1.f, 2.f * m, 2.f * m, t, kFriend);
	Text(x + 3.f * m, y, kQuiet, "your squad, units", room - 3.f * m);
	y += line;
	// what a box without an order wears (a bar for a vehicle squad, a dot for infantry), and what the letters in the others mean
	const float x2 = x + room * 0.5f;
	Outline(x, y + 1.f, 2.f * m, 2.f * m, t, kFriend);
	Box(x + m * 0.3f, y + 1.f + m - t, m * 1.4f, 2.f * t, kFriend);
	Text(x + 3.f * m, y, kQuiet, "vehicles", room * 0.5f - 3.f * m);
	Outline(x2, y + 1.f, 2.f * m, 2.f * m, t, kFriend);
	Box(x2 + m - t, y + 1.f + m - t, 2.f * t, 2.f * t, kFriend);
	Text(x2 + 3.f * m, y, kQuiet, "infantry", room * 0.5f - 3.f * m);
	y += line;
	Text(x, y, kQuiet, "M move  A advance", room);
	y += line;
	Text(x, y, kQuiet, "H hold  F fall back", room);
	y += line;
	Box(x, y + 1.f, 2.f * m, 2.f * m, kEnemy);
	Text(x + 3.f * m, y, kQuiet, "enemy squad", room - 3.f * m);
	y += line;
	Box(x, y + m, 2.f * m, t, kPilot);
	Box(x + m - t * 0.5f, y + 1.f, t, 2.f * m, kPilot);
	Text(x + 3.f * m, y, kQuiet, "you", room - 3.f * m);
	y += line;
	Dots(x, y + m, x + 2.f * m, y + m, t * 1.5f, kMoveColour);
	Text(x + 3.f * m, y, kQuiet, "a squad's standing order", room - 3.f * m);
}

void DrawHints()
{
	const float line = LineHeight() + 2.f * L.s, y = L.h - 40.f * L.s;
	const float room = L.w - 24.f * L.s;
	// A tester's page (TacticalMapAllowed): the same orders and keys as the live view, listed in full.
	Text(12.f * L.s, y, kQuiet, "Right click: advance, fights   Ctrl right click: move   A, then click: advance   H, X: hold, fires   B: fall back   Delete: release   V: live view", room);
	Text(12.f * L.s, y + line, kQuiet, "Click, Shift click: pick   Ctrl A: all   Ctrl 1 to 9: keep a group   Tab: next   Wheel, Q, E, arrows: zoom, pan   Space: to you   Esc, M: close", room);
}

// The last thing the map told the player, in the title bar, to the right.
void DrawNote()
{
	if (!gNote[0] || PortPresentCount() >= gNoteUntil) return;
	const float room = L.w * 0.5f, w = TextWidth(gNote, L.s) < room ? TextWidth(gNote, L.s) : room;		// half the width: the title bar's text ends at 43 percent of it at 640 x 480
	Text(L.w - 12.f * L.s - w, 8.f * L.s, kPick, gNote, room);
}

// The pointer: an arrow whose tip is the point a click acts on, white with a dark edge, yellow while an order waits for its click. (The game's own ship pointer is a
// sprite; the live view draws only through the device, so its shape is drawn here in rows, as a sprite of it would be.)
bool StrategicLive();			// the live view is open (below)
bool UnitHasArmedTarget();		// per-unit order waiting for a target click (below)
int gHudFollowSquad = -1;
void DrawCursor()
{
	if (StrategicLive() && !gLiveShown) return;		// the live view's pointer shows once the mouse has moved (after a reopen it would sit in a corner)
	const float p = L.s > 1.f ? L.s : 1.f, x = float(MOUSE_X), y = float(MOUSE_Y);
	const DWORD fill = (gArmed || UnitHasArmedTarget()) ? kFocus : 0xffffffff;
	const int rows = 14;
	for (int r = 0; r < rows; ++r)
	{
		const float w = r < 10 ? float(r) * 0.6f : float(rows - r) * 1.2f + 1.f;		// widening to the shoulder, then the notch towards the tail
		Box(x - p, y + float(r) * p - p, (w + 2.f) * p, 3.f * p, 0xc0000000);
	}
	for (int r = 1; r < rows - 1; ++r)
	{
		const float w = r < 10 ? float(r) * 0.6f : float(rows - r) * 1.2f + 1.f;
		Box(x, y + float(r) * p, w * p, p, fill);
	}
	Box(x + 3.f * p, y + 10.f * p, 2.f * p, 7.f * p, 0xc0000000);		// the tail
	Box(x + 3.5f * p, y + 10.f * p, p, 6.f * p, fill);
}

// ---- the pointer ---------------------------------------------------------------------------------------------------------------------

float WorldX(float px) { return gViewX + (px - L.mapX) / L.k; }
float WorldY(float py) { return gViewY + (py - L.mapY) / L.k; }

// The friendly squad whose box the pointer is on or nearest to (within a few boxes' reach), or -1.
int PickAt(float px, float py)
{
	int best = -1;
	float bestD = 1e30f;
	for (int i = 0; i < gCount; ++i)
	{
		const CeCmdSquad &s = gSquads[i];
		if (!s.friendly) continue;
		const float reach = BoxHalf(s) + 5.f * L.s, dx = Px(s.x) - px, dy = Py(s.y) - py, d = sqrtf(dx * dx + dy * dy);
		if (d <= reach && d < bestD) { bestD = d; best = s.ordinal; }
	}
	return best;
}

// The map's order for every picked squad. A march sends each squad to its own place round the point (ce_cmdgroup.h: a cluster kSpread apart, the farthest
// squad choosing first, the whole cluster kept inside the map), so that they do not stand on each other; Hold and Release go to each squad alone. A Fall back
// goes to each squad's own nearest safe point, which the order layer chooses: the squads that chose the same one are then given places round it the same way.
std::uint64_t gOrderNs;		// when the last accepted order was given (Ns): the view draws its line growing and its goal settling from then (M5)

// Several squads sent to one point: each to its own place round it, and true; false (each to the point itself) when they cannot be placed inside the map.
bool SpreadRound(int n, const float *sx, const float *sy, float wx, float wy, float *tx, float *ty)
{
	const float kSpread = 20.f;		// world units between two squads' places
	float gx[kCeSelMax], gy[kCeSelMax];
	int slotOf[kCeSelMax];
	if (!CeCmdSlotsInBounds(n, wx, wy, kSpread, gBoundsX0, gBoundsY0, gBoundsX1, gBoundsY1, gx, gy) || !CeCmdAssign(n, sx, sy, gx, gy, wx, wy, slotOf)) return false;
	for (int k = 0; k < n; ++k) { tx[k] = gx[slotOf[k]]; ty[k] = gy[slotOf[k]]; }
	return true;
}

bool Order(int verb, float wx, float wy, CeCmdVoiceCue voiceOverride = CE_VOICE_NONE)
{
	if (!ByOrdinal(gSelected)) { Note("Pick a squad first."); return false; }
	int ids[kCeSelMax], n = 0;
	float sx[kCeSelMax], sy[kCeSelMax], tx[kCeSelMax], ty[kCeSelMax];
	bool landOnly[kCeSelMax], anyGround = false;
	for (int i = 0; i < gSel.n && n < kCeSelMax; ++i)
		if (const CeCmdSquad *s = ByOrdinal(gSel.ids[i])) { landOnly[n] = !s->crossesWater; anyGround |= landOnly[n]; ids[n] = s->ordinal; sx[n] = s->x; sy[n] = s->y; tx[n] = wx; ty[n] = wy; ++n; }
	if (verb != CE_CMD_FALLBACK && CeCmdSpreads(verb, n)) {
		const bool dryClick = !anyGround || CeCmdMarchLand(wx, wy);
		SpreadRound(n, sx, sy, wx, wy, tx, ty);
		for (int k = 0; k < n; ++k) {
			if (landOnly[k] && (!dryClick || !CeCmdMarchLand(tx[k], ty[k]))) { tx[k] = wx; ty[k] = wy; }
		}
	}
	int given = 0, refused = 0;
	bool ok[kCeSelMax];
	const char *refusal = NULL;
	for (int k = 0; k < n; ++k)
	{
		const char *why = NULL;
		ok[k] = CeCmdIssue(ids[k], verb, tx[k], ty[k], &why);
		if (!ok[k])
		{
			++refused;
			if (!refusal) refusal = why ? why : "Refused.";
			CeCmdNote("map refused %d %d %s", ids[k], verb, why ? why : "-");
			continue;
		}
		++given;
		CeCmdNote("map order %d %d %.1f %.1f", ids[k], verb, tx[k], ty[k]);
	}
	Refresh();
	if (verb == CE_CMD_FALLBACK && CeCmdSpreads(verb, given))
	{
		// The squads that fell back to one safe point: each to its own place round it (the order layer keeps a finite point a Fall back is given).
		bool done[kCeSelMax] = {};
		for (int a = 0; a < n; ++a)
		{
			const CeCmdSquad *lead = ok[a] && !done[a] ? ByOrdinal(ids[a]) : NULL;
			if (!lead) continue;
			int members[kCeSelMax], m = 0;
			float mx[kCeSelMax], my[kCeSelMax], px[kCeSelMax], py[kCeSelMax];
			for (int b = a; b < n; ++b)
			{
				const CeCmdSquad *s = ok[b] && !done[b] ? ByOrdinal(ids[b]) : NULL;
				if (!s || s->verb != CE_CMD_FALLBACK || fabsf(s->gx - lead->gx) > 0.5f || fabsf(s->gy - lead->gy) > 0.5f) continue;
				done[b] = true;
				members[m] = ids[b]; mx[m] = sx[b]; my[m] = sy[b]; ++m;
			}
			if (m < 2 || !SpreadRound(m, mx, my, lead->gx, lead->gy, px, py)) continue;
			for (int k = 0; k < m; ++k)
				if (CeCmdIssue(members[k], CE_CMD_FALLBACK, px[k], py[k], NULL)) CeCmdNote("map order %d %d %.1f %.1f", members[k], verb, px[k], py[k]);
		}
		Refresh();
	}
	const bool already = verb == CE_CMD_RELEASE && refusal && !strcmp(refusal, "Already on its own orders.");	// Delete on squads under no order of the map's
	if (!given)
	{
		Note(already && refused > 1 ? "Already on their own orders." : refusal ? refusal : "Refused.");
		if (StrategicLive() && (verb == CE_CMD_MOVE || verb == CE_CMD_ADVANCE)) { gNoteAtX = float(MOUSE_X); gNoteAtY = float(MOUSE_Y); gNoteAtUntil = gNoteUntil; }
		return false;
	}
	gOrderNs = Ns();
	if (voiceOverride != CE_VOICE_FOLLOW && gHudFollowSquad >= 0)
	{
		for (int k = 0; k < n; ++k)
			if (ok[k] && ids[k] == gHudFollowSquad) { gHudFollowSquad = -1; break; }
	}
	if (StrategicLive())
	{
		const CeCmdVoiceCue cue = voiceOverride != CE_VOICE_NONE ? voiceOverride :
		                          verb == CE_CMD_ADVANCE ? CE_VOICE_ATTACK :
		                          verb == CE_CMD_MOVE ? CE_VOICE_MOVE :
		                          verb == CE_CMD_HOLD ? CE_VOICE_HOLD :
		                          verb == CE_CMD_FALLBACK ? CE_VOICE_FALLBACK : CE_VOICE_CANCEL;
		CeCmdVoicePlay(cue, gOrderNs);
	}
	static const char *const one[] = {"", "Moving.", "Advancing.", "Holding.", "Falling back.", "Released."};
	static const char *const several[] = {"", "moving", "advancing", "holding", "falling back", "released"};
	char text[112];
	if (given == 1) snprintf(text, sizeof(text), "%s", one[verb]);
	else snprintf(text, sizeof(text), "%d squads %s.", given, several[verb]);
	if (refused && !already) snprintf(text + strlen(text), sizeof(text) - strlen(text), " %s", refusal);
	Note(text);
	return true;
}

// The selection as one squad, or with one squad more or less (a Shift click), or as every squad that takes orders (Ctrl and A).
void SelectOnly(int ordinal)
{
	CeSelOnly(gSel, ordinal);
	gSelected = CeSelLead(gSel);
	Refresh();
	CeCmdNote("map select %d", gSelected);
}

void SelectToggle(int ordinal)
{
	CeSelToggle(gSel, ordinal);
	gSelected = CeSelLead(gSel);
	Refresh();
	CeCmdNote("map select %d toggle %d count %d", gSelected, ordinal, gSel.n);
}

void SelectAll()
{
	CeSelClear(gSel);
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].commandable) CeSelAdd(gSel, gSquads[i].ordinal);
	gSelected = CeSelLead(gSel);
	Refresh();
	CeCmdNote("map select all %d", gSel.n);
	char text[64];
	if (gSel.n) snprintf(text, sizeof(text), "%d squads picked.", gSel.n);
	else snprintf(text, sizeof(text), "There are no squads here to command.");
	Note(text);
}

// A digit: with Ctrl the selection is kept under it, alone it picks that group again, with Shift it adds the group to the selection.
void ClearUnitGroupSlot(int number);
void GroupKey(int number, bool keep, bool add)
{
	char text[64];
	if (keep)
	{
		CeGroupStore(gGroups, number, gSel);
		ClearUnitGroupSlot(number);
		CeCmdNote("map group %d keep %d", number, gSel.n);
		if (gSel.n) snprintf(text, sizeof(text), "Group %d: %d squad%s.", number, gSel.n, gSel.n == 1 ? "" : "s");
		else snprintf(text, sizeof(text), "Group %d cleared.", number);
		Note(text);
		return;
	}
	CeCmdSel group;
	if (!CeGroupRecall(gGroups, number, group))
	{
		snprintf(text, sizeof(text), "Group %d is empty. Ctrl and %d keeps the squads you picked.", number, number);
		Note(text);
		return;
	}
	if (!add) CeSelClear(gSel);
	CeFactoryLeaveSelection();
	for (int i = 0; i < group.n; ++i) CeSelAdd(gSel, group.ids[i]);
	gSelected = CeSelLead(gSel);
	Refresh();
	CeCmdNote("map group %d pick %d", number, gSel.n);
	if (gSel.n) snprintf(text, sizeof(text), "Group %d: %d squad%s.", number, gSel.n, gSel.n == 1 ? "" : "s");
	else snprintf(text, sizeof(text), "Group %d has no squads left.", number);
	Note(text);
}

// ---- the live view's pointer ---------------------------------------------------------------------------------------------------------
//
// In a running level the game takes MOUSE_X and MOUSE_Y as a movement: each frame it turns the pilot's walker by their distance from the window's
// centre and puts them back on the centre (the walker's mouse look, called by the game's render). With the mouse released, as it is while the view
// is open, a window message would write the pointer's place there: the pilot would be turned by wherever the pointer is, and the view's own
// pointer would be drawn at the centre however the mouse moved (a scripted click asked at one point landed at another). So the live view keeps a
// pointer of its own, from the window layer's record of the points it mapped (PortPointerNow), and the game is told its own mouse is active for
// as long as the view is open: window messages then leave MOUSE_X alone, and it stays on the centre, where it turns nothing.
// The view's code reads MOUSE_X and MOUSE_Y as before: a LivePointer in scope holds them at the view's pointer for the length of one entry (a
// key or button, the frame's drawing, the camera's update) and puts back what the game had.
extern "C" int PortPointerNow(int *x, int *y);		// window.cpp

int gLiveX, gLiveY;				// the live view's pointer, in window pixels
int gLiveMapped;				// how many points the window layer had mapped when the pointer was last taken from it
bool gLiveForced;				// a test tape's ui verb holds the pointer where its action needs it

// As the live view opens, after the mouse was released (ShutdownMouse left the released pointer's place in MOUSE_X and MOUSE_Y).
void LivePointerStart()
{
	int x = 0, y = 0;
	gLiveMapped = PortPointerNow(&x, &y);
	gLiveShown = false;
	gLiveX = MOUSE_X;
	gLiveY = MOUSE_Y;
	SetMouseInputActive(TRUE);
	MOUSE_X = PLATFORM.GetWindowWidth() / 2;
	MOUSE_Y = PLATFORM.GetWindowHeight() / 2;
}

bool StrategicLive();			// the live view is open (below)

struct LivePointer
{
	bool on;
	int keptX, keptY;
	LivePointer() : on(StrategicLive()), keptX(MOUSE_X), keptY(MOUSE_Y)
	{
		if (!on) return;
		int x = 0, y = 0;
		const int mapped = PortPointerNow(&x, &y);
		if (!gLiveForced && mapped && mapped != gLiveMapped) { gLiveX = x; gLiveY = y; gLiveShown = true; }
		gLiveMapped = mapped;
		MOUSE_X = gLiveX;
		MOUSE_Y = gLiveY;
	}
	// A view that closed, or changed to the paused map, inside the scope set the game's pointer itself: that stands.
	~LivePointer() { if (on && StrategicLive()) { MOUSE_X = keptX; MOUSE_Y = keptY; } }
};

// ---- opening and closing ------------------------------------------------------------------------------------------------------------

bool Trap(WORD key, int event);

#ifdef CE_COMMANDER_TEST
// BEA_PORT_COMMANDER_DUMP=PREFIX (proof runs): the world's things in PREFIX-open.txt and PREFIX-close.txt, for the frozen-state check.
void DumpWorld(const char *when)
{
	const char *prefix = getenv("BEA_PORT_COMMANDER_DUMP");
	if (!prefix || !*prefix) return;
	char path[600];
	snprintf(path, sizeof(path), "%s-%s.txt", prefix, when);
	CeCmdWorldDump(path);
}
#endif


// CE_STRATEGIC_CAMERA_BEGIN: this complete block is exercised with authored consumers.
void Close();
void Open(bool strategic = false);
bool StrategicTrap(WORD key, int event);

bool gStrategic = false;
bool gStrategicRender = false;
// The strategic camera is the Commander's own: the engine is never given it (the game renders the pilot's frame from the pilot's camera while the
// view is open, exactly as it does when it is closed). The live view's picture is drawn from it, over that frame (PortCeCmdRender).
CGenericCamera gStrategicCamera;
CGenericCamera gStrategicPresentedCamera;
CeCommanderPresentedPose gNativeEntryFrom = {}, gNativeLastPose = {}, gNativeReturnFrom = {};
CeCommanderCameraBinding gNativeReturnBinding = {};
CCamera* gNativeReturnOriginal = NULL;
std::uint64_t gNativeEntryStartNs = 0, gNativeReturnStartNs = 0;
bool gNativeEntrySeeded = false, gNativeEntering = false;
bool gNativePresented = false, gNativeReturning = false;

CCamera* gStrategicOriginal = NULL;
CPlayer* gObservedPlayer = NULL;
CBattleEngine* gObservedPilot = NULL;
CeEntityHandle gObservedPilotHandle = kCeEntityHandleInvalid;
CCamera* gObservedCamera = NULL;
std::uint64_t gMissionLifetime = 1, gPilotLifetime = 1, gCameraLifetime = 1;
CeCommanderCameraBinding gStrategicBinding;
// The live view's battlefield framing (R7: the view read as a map). Pitch 0.85 (about 49 degrees down) shows the ground in perspective with the
// horizon's haze at the top of the window instead of a plan from above; 30 world units of span puts the camera about 24 units from what it looks at,
// where a tank is about thirty pixels long at 1280 x 720 and infantry can be told from vehicles (56 left tanks at fifteen, 96 at five). The live
// view may come in to 16 (the paused map keeps its 48). The wheel and R/F still zoom out to the whole island.
const float kLivePitch = 0.85f, kLiveOpenSpan = 30.f, kLiveWideSpan = 120.f, kMaxDrySnapDist = 250.f;
int gKeptMapLevel = -1;
float gKeptMapCentreX = 0.f, gKeptMapCentreY = 0.f, gKeptMapSpan = 0.f;
std::uint64_t gKeptMission = 0, gKeptMapMission = 0;
float gKeptYaw = 0.f;
bool gHasKeptYaw = false;
float gStrategicInitialYaw = 0.f;
bool gHasStrategicInitialYaw = false;
std::uint64_t gMapPulseStartNs = 0;
std::uint64_t MapPulseStartNs() { return gMapPulseStartNs; }
// Z: one step between the battlefield and the wide view, eased (the wheel and R/F still zoom freely). The close span Z goes back to is the one it left.
struct StrategicZoomTrip { bool on = false; float from = 0, to = 0; std::uint64_t start = 0; } gZoomTrip;
float gZoomBack = 30.f;
float gStrategicYaw = 0, gStrategicPitch = kLivePitch, gStrategicFocusZ = 0;
std::uint64_t gStrategicLastNs = 0;
// C frames one squad. Normal controls immediately take the camera back; this is not a follow mode.
struct StrategicFocusTrip {
    int squad = -1;
    float fromX = 0, fromY = 0, fromZ = 0;
    std::uint64_t start = 0;
} gFocusTrip;
std::uint64_t gStrategicViewSession = 1;
bool gStrategicDragging = false;
float gStrategicDragX = 0, gStrategicDragY = 0;
// The left button held on the battlefield: a click when it is let go where it went down, a box of squads when it was dragged at least kBoxSlack pixels.
bool gBoxing = false;
float gBoxX = 0, gBoxY = 0;
const float kBoxSlack = 6.f;
namespace UnitPanel = ce_command_panel;
namespace UnitControl = ce_command_selection;
UnitControl::SelectionController gUnitUi;
bool UnitHasArmedTarget() { return gUnitUi.HasArmedTarget(); }
bool gUnitGroupSaved[UnitPanel::kMaxSavedSets] = {};
void ClearUnitGroupSlot(int number) {
	const int slot = number == 0 ? 9 : number - 1;
	gUnitUi.ClearSet(slot);
	if (slot >= 0 && slot < UnitPanel::kMaxSavedSets) gUnitGroupSaved[slot] = false;
}
CeCmdUnitSnapshot gUnitNative[UnitControl::kMaxUnits];
UnitControl::UnitSnapshot gUnitRows[UnitControl::kMaxUnits];
int gUnitNativeCount = 0;
int gUnitHiddenCount = 0;
bool gUnitSnapshotComplete = false;
uint32_t gUnitSnapshotGeneration = 0, gUnitContextGeneration = 1;
CeCommandLifetimeKey gUnitFocusKeys[UnitControl::kMaxRequestKeys];
unsigned gUnitFocusCount = 0;
constexpr int kUnitFocus = 0x3ffffffe;
CeCmdPointerFence gUnitPointerFence;
bool gUnitPointerAvailable = false;
void RefreshUnitFrame();
bool UnitPanelAt(float, float);
bool UnitFocusPosition(float*, float*);
void UnitPointerObserve();

int gStrategicWheel = 0;
int gStrategicClickOrder = CE_CMD_ADVANCE;

// The view the live picture was last drawn with: the pointer picks from it. It is the Commander's own record (no world-view capture of the game's
// render serves this view: there is no render of the game's from this camera). Refused once it is older than a Present, from another entry into
// the view, or from a window of another size.
CeWorldViewSample gStrategicSample;
bool gStrategicSampleValid = false;
std::uint64_t gStrategicSampleSession = 0;
float gStrategicSampleWidth = 0, gStrategicSampleHeight = 0;
struct StrategicUiFrame {
    bool valid = false;
    bool hasDialogue = false;
    unsigned present = 0;
    std::uint64_t session = 0;
    int width = 0, height = 0;
    float bottom = 0;
    CeHudReservation dialogue{};
};
StrategicUiFrame gStrategicUi;

void StrategicUiPrepare() {
    gStrategicUi = StrategicUiFrame{};
    gStrategicUi.present = PortPresentCount();
    gStrategicUi.session = gStrategicViewSession;
    gStrategicUi.width = L.w; gStrategicUi.height = L.h;
    gStrategicUi.bottom = float(L.h);
    gStrategicUi.hasDialogue = CeHudReservationRead(gStrategicViewSession,
        gStrategicUi.present, L.w, L.h, &gStrategicUi.dialogue);
    if (gStrategicUi.hasDialogue)
        gStrategicUi.bottom = (std::max)(0.0f, gStrategicUi.dialogue.top - 8.0f * L.s);
    gStrategicUi.valid = true;
}

bool StrategicUiReady() {
    return gStrategicUi.valid && gStrategicUi.session == gStrategicViewSession &&
        gStrategicUi.width == L.w && gStrategicUi.height == L.h &&
        gStrategicUi.width == PLATFORM.GetWindowWidth() &&
        gStrategicUi.height == PLATFORM.GetWindowHeight() &&
        unsigned(PortPresentCount() - gStrategicUi.present) <= 1;
}

bool StrategicDialogueAt(float x, float y) {
    return gStrategicUi.hasDialogue &&
        x >= gStrategicUi.dialogue.left && x <= gStrategicUi.dialogue.right &&
        y >= gStrategicUi.dialogue.top && y <= gStrategicUi.dialogue.bottom;
}

float StrategicUiBottom() { return gStrategicUi.bottom; }

void StrategicViewInvalidate() {
    gStrategicUi.valid = false; gStrategicSampleValid = false; }
void StrategicViewSet(const CeWorldViewSample& view) {
    gStrategicSample = view; gStrategicSample.present = PortPresentCount();
    gStrategicSampleSession = gStrategicViewSession; gStrategicSampleWidth = L.w; gStrategicSampleHeight = L.h;
    gStrategicSampleValid = true;
    StrategicUiPrepare();
}
bool StrategicViewRead(CeWorldViewSample* out) {
    if (!out || !gStrategicSampleValid || !gStrategicSampleSession || gStrategicSampleSession != gStrategicViewSession ||
        PortPresentCount() - gStrategicSample.present > 1 || gStrategicSampleWidth != L.w || gStrategicSampleHeight != L.h) return false;
    *out = gStrategicSample; return true;
}
void StrategicEntered();        // the eased entry starts from the view the pilot had (below the block)
void StrategicLeft();           // the picture's device objects are freed (below the block)

bool StrategicCanAct();

// The view's furniture, in the layer's units (L.s, one unit a pixel at 640 x 480): compact top title capsule,
// cohesive bottom Forseti RTS Command Bar (Unit/Squad/Structure placards + right-docked C&C Command Card),
// and compact top-right help hint. Drawing and the pointer share gHudLayout.
const float kCardW = 440.f, kCardH = 46.f, kRowH = 20.f;
const float kButtonW[4] = {60.f, 82.f, 60.f, 98.f}, kButtonGap = 18.f;
constexpr int kFactoryFocus = 0x3fffffff;
constexpr int kStructureFocus = 0x3ffffffd;
CeCmdPlacard gHudPlacards[kCeHudMaxPlacards];
int gHudPlacardCount = 0;
int gHudPage = 0;
CeCmdHudLayout gHudLayout = {};
CeCmdCardModel gHudCard = {};
CeCmdClickTracker gHudClick = {};
std::uint64_t gHudBtnFlashNs[kCeHudCardButtons] = {};
CeEntityHandle gStructFocusHandle = kCeEntityHandleInvalid;
struct StrategicHudRect { float x0, y0, x1, y1; };
StrategicHudRect gActiveNoticeRect = {};
bool gActiveNoticeRectValid = false;
StrategicHudRect gActiveHelpRect = {};
bool gActiveHelpRectValid = false;
int PilotAllegiance(const void** pilot);

bool CmdAsciiContainsNoCase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) return false;
    for (const char* h = haystack; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b) {
            char ca = *a;
            char cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
            if (ca != cb) break;
            ++a; ++b;
        }
        if (*b == '\0') return true;
    }
    return false;
}

// Preserves descriptive Muspell & Forseti unit archetypes on squad cards so distinct tank/walker
// classes never collapse into identical "Tanks / Tanks / Tank" labels.
void FormatSquadCardShortName(const char* rawName, bool plural, int allegiance, char* out, int room) {
    if (!out || room <= 0) return;
    out[0] = 0;
    const char* src = (rawName && rawName[0]) ? rawName : "Squad";
    const char* picked = NULL;
    if (CmdAsciiContainsNoCase(src, "broadsword"))
        picked = plural ? "Broadswords" : "Broadsword";
    else if (CmdAsciiContainsNoCase(src, "crab") && CmdAsciiContainsNoCase(src, "mech"))
        picked = plural ? "Crab Walkers" : "Crab Walker";
    else if (CmdAsciiContainsNoCase(src, "arachn"))
        picked = plural ? "Arachnodrones" : "Arachnodrone";
    else if (CmdAsciiContainsNoCase(src, "gun walker") || CmdAsciiContainsNoCase(src, "walker"))
        picked = plural ? "Gun Walkers" : "Gun Walker";
    else if (CmdAsciiContainsNoCase(src, "beam") && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Beam Tanks" : "Beam Tank";
    else if (CmdAsciiContainsNoCase(src, "mortar") && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Mortar Tanks" : "Mortar Tank";
    else if (CmdAsciiContainsNoCase(src, "heavy") && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Heavy Tanks" : "Heavy Tank";
    else if (CmdAsciiContainsNoCase(src, "light") && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Light Tanks" : "Light Tank";
    else if (CmdAsciiContainsNoCase(src, "gun") && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Gun Tanks" : "Gun Tank";
    else if (CmdAsciiContainsNoCase(src, "assault") && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Assault Tanks" : "Assault Tank";
    else if ((CmdAsciiContainsNoCase(src, "flak") || CmdAsciiContainsNoCase(src, "anti-air")) && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Flak Tanks" : "Flak Tank";
    else if (CmdAsciiContainsNoCase(src, "flame") && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Flame Tanks" : "Flame Tank";
    else if ((CmdAsciiContainsNoCase(src, "missile") || CmdAsciiContainsNoCase(src, "rocket")) && CmdAsciiContainsNoCase(src, "tank"))
        picked = plural ? "Rocket Tanks" : "Rocket Tank";
    else if (CmdAsciiContainsNoCase(src, "hover"))
        picked = plural ? "Hover Tanks" : "Hover Tank";
    else if (CmdAsciiContainsNoCase(src, "sabre") || CmdAsciiContainsNoCase(src, "pulse tank"))
        picked = plural ? "Sabre Tanks" : "Sabre Tank";
    else if (CmdAsciiContainsNoCase(src, "spider"))
        picked = plural ? "Spiders" : "Spider";
    else if (CmdAsciiContainsNoCase(src, "tank") && allegiance == 1)
        picked = plural ? "Muspell Tanks" : "Muspell Tank";

    if (picked) {
        CeCmdHudSanitizeText(picked, out, room);
        return;
    }
    CeCmdHudShortName(src, plural, false, out, room);
}

void RtsHudBox(float x, float y, float w, float h, uint32_t c) { Box(x, y, w, h, c); }
void RtsHudOutline(float x, float y, float w, float h, float t, uint32_t c) { Outline(x, y, w, h, t, c); }
void RtsHudText(float x, float y, uint32_t c, const char* s, float maxW, bool centred, float sc) {
    Text(x, y, c, s, maxW, centred, sc);
}
float RtsHudTextWidth(const char* s, float sc) { return TextWidth(s, sc); }
float RtsHudLineHeight(float sc) { return L.s > 0.f ? (LineHeight() / L.s) * sc : CeCmdViewTextHeight() * sc; }
void RtsHudCurvedPanel(float x, float y, float w, float h, float capMax, uint32_t fill, uint32_t capTint, bool nativeCyan, float alpha, bool withOuterCaps) {
    DrawBeaCurvedPanel(x, y, w, h, capMax, fill, capTint, nativeCyan, alpha, withOuterCaps);
}
bool UnitWholeSquad(int ordinal, const UnitControl::Request* request = NULL);

int SquadGroupTag(int ordinal) {
    for (int k = 1; k <= 9; ++k) if (CeSelHas(gGroups.group[k], ordinal)) return k;
    if (CeSelHas(gGroups.group[0], ordinal)) return 0;
    return -1;
}

int UnitGroupTag(const CeCommandLifetimeKey& nativeKey) {
    UnitPanel::EntityKey key = {};
    if (!UnitControl::ToPanelKey(nativeKey, &key)) return -1;
    for (int s = 0; s < 9; ++s)
        if (gUnitUi.Panel().SelectionSetContains(s, key)) return s + 1;
    if (gUnitUi.Panel().SelectionSetContains(9, key)) return 0;
    return -1;
}

bool CanTakeSelectedSquadUnit(CeCommandLifetimeKey* outKey) {
    if (gSel.n != 1 || gSelected < 0) return false;
    for (int i = 0; i < gUnitNativeCount; ++i) {
        if (gUnitNative[i].ordinal != gSelected || !gUnitNative[i].friendly) continue;
        struct TakeCheck { const char* reason; } chk = {};
        if (CeCmdUnitVisit(gUnitNative[i].key, [](CUnit* unit, int number, void* user) -> bool {
            TakeCheck* c = static_cast<TakeCheck*>(user);
            return CePlayUnitCanTakeSelection(CePlayUnitSelectionWorld(), unit, number, &c->reason);
        }, &chk)) {
            if (outKey) *outKey = gUnitNative[i].key;
            return true;
        }
    }
    return false;
}

void BuildRtsHudModel(char* summaryOut, unsigned summaryRoom) {
    gHudPlacardCount = 0;
    CeCmdStructureView structs[kCeMaxStructures];
    const int structCount = CeCmdStructureList(structs, kCeMaxStructures);

    // 1. Currently selected individual units (when picked directly in 3D).
    const int selUnits = gUnitUi.SelectionCount();
    int addedUnits = 0;
    for (int i = 0; i < gUnitNativeCount && addedUnits < 4 && gHudPlacardCount < kCeHudMaxPlacards; ++i) {
        if (!gUnitUi.IsSelected(gUnitNative[i].key)) continue;
        CeCmdPlacard& pl = gHudPlacards[gHudPlacardCount++];
        pl = CeCmdPlacard{};
        pl.kind = CE_PLACARD_UNIT;
        pl.id = int(gUnitNative[i].key.id);
        pl.hasValidId = true;
        snprintf(pl.name, sizeof(pl.name), "%s", gUnitNative[i].name);
        CeCmdHudShortName(gUnitNative[i].name, false, false, pl.shortName, sizeof(pl.shortName));
        pl.count = 1;
        pl.armourRatio = CeCmdHudArmourRatio(gUnitNative[i].life, gUnitNative[i].maxLife, 1, 1);
        CeCmdUnitOrderView ov = {};
        if (CeCmdUnitOrderRead(gUnitNative[i].key, &ov) && ov.owned) {
            if (ov.order == CE_UNIT_ORDER_MOVE) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "MOVE"); pl.orderTone = CE_TONE_ACTIVE; }
            else if (ov.order == CE_UNIT_ORDER_HOLD) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "HOLD"); pl.orderTone = CE_TONE_HOLD; }
            else { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "AI"); pl.orderTone = CE_TONE_NEUTRAL; }
        } else {
            snprintf(pl.orderBadge, sizeof(pl.orderBadge), "AI");
            pl.orderTone = CE_TONE_NEUTRAL;
        }
        pl.groupTag = UnitGroupTag(gUnitNative[i].key);
        pl.selected = true;
        pl.commandable = gUnitNative[i].commandable;
        pl.worldX = gUnitNative[i].position[0];
        pl.worldY = gUnitNative[i].position[1];
        ++addedUnits;
    }

    // 2. Friendly production structures/factories (always immediately visible on page 1).
    for (int i = 0; i < structCount && gHudPlacardCount < kCeHudMaxPlacards; ++i) {
        const CeCmdStructureView& s = structs[i];
        CeCmdPlacard& pl = gHudPlacards[gHudPlacardCount++];
        pl = CeCmdPlacard{};
        pl.kind = CE_PLACARD_STRUCTURE;
        pl.id = s.thingNumber;
        snprintf(pl.name, sizeof(pl.name), "%s", s.name);
        CeCmdHudShortName(s.name, false, true, pl.shortName, sizeof(pl.shortName));
        pl.count = 0;
        pl.armourRatio = CeCmdHudArmourRatio(s.life, s.maxLife, 1, 1);
        pl.groupTag = -1;
        pl.selected = s.selected;
        pl.commandable = true;
        pl.worldX = s.x;
        pl.worldY = s.y;
        if (s.stage == CE_FACTORY_QUEUED) {
            snprintf(pl.orderBadge, sizeof(pl.orderBadge), "BUILD %d%%", s.percent);
            pl.orderTone = CE_TONE_ACTIVE;
            pl.isBuilding = true;
            pl.buildPercent = s.percent;
        } else if (s.stage == CE_FACTORY_LAUNCHING) {
            snprintf(pl.orderBadge, sizeof(pl.orderBadge), "LAUNCH");
            pl.orderTone = CE_TONE_ACTIVE;
            pl.isBuilding = true;
            pl.buildPercent = 100;
        } else if (s.stage == CE_FACTORY_READY) {
            snprintf(pl.orderBadge, sizeof(pl.orderBadge), "READY");
            pl.orderTone = CE_TONE_HOLD;
        } else if (s.stage == CE_FACTORY_BLOCKED) {
            snprintf(pl.orderBadge, sizeof(pl.orderBadge), "BLOCKED");
            pl.orderTone = CE_TONE_ALERT;
        } else {
            bool anyReady = false, anySpawning = false, anyEnabled = false;
            for (int k = 0; k < s.optionCount; ++k) {
                if (s.options[k].ready) anyReady = true;
                if (s.options[k].spawning) anySpawning = true;
                if (s.options[k].enabled) anyEnabled = true;
            }
            if (anyReady) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "READY"); pl.orderTone = CE_TONE_HOLD; }
            else if (anySpawning) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "ACTIVE"); pl.orderTone = CE_TONE_ACTIVE; }
            else if (anyEnabled) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "EMPTY"); pl.orderTone = CE_TONE_NEUTRAL; }
            else { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "OFFLINE"); pl.orderTone = CE_TONE_NEUTRAL; }
        }
    }

    // 3. Friendly squads (commandable first, then other friendly squads).
    const void* squadPilot = NULL;
    const int squadAllegiance = PilotAllegiance(&squadPilot);
    const int squadStart = gHudPlacardCount;
    int friendlySquads = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < gCount && gHudPlacardCount < kCeHudMaxPlacards; ++i) {
            const CeCmdSquad& sq = gSquads[i];
            if (!sq.friendly || sq.members <= 0) continue;
            if (pass == 0) ++friendlySquads;
            if ((pass == 0) != sq.commandable) continue;
            CeCmdPlacard& pl = gHudPlacards[gHudPlacardCount++];
            pl = CeCmdPlacard{};
            pl.kind = CE_PLACARD_SQUAD;
            pl.id = sq.ordinal;
            snprintf(pl.name, sizeof(pl.name), "%s", sq.unit[0] ? sq.unit : (sq.vehicles ? "Vehicles" : "Infantry"));
            FormatSquadCardShortName(pl.name, sq.members != 1, squadAllegiance, pl.shortName, sizeof(pl.shortName));
            pl.count = sq.members;
            pl.armourRatio = CeCmdHudArmourRatio(sq.life, sq.maxLife, sq.members, sq.peak);
            pl.groupTag = SquadGroupTag(sq.ordinal);
            pl.selected = sq.selected;
            pl.commandable = sq.commandable;
            pl.worldX = sq.x;
            pl.worldY = sq.y;
            if (sq.outcome == CE_OUT_FIGHTING) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "FIGHT"); pl.orderTone = CE_TONE_ALERT; }
            else if (sq.outcome == CE_OUT_STUCK) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "STUCK"); pl.orderTone = CE_TONE_ALERT; }
            else if (sq.outcome == CE_OUT_ARRIVED) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "ARRIVED"); pl.orderTone = CE_TONE_HOLD; }
            else if (sq.verb == CE_CMD_ADVANCE && gHudFollowSquad == sq.ordinal && !sq.arrived) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "FOLLOW"); pl.orderTone = CE_TONE_ACTIVE; }
            else if (sq.verb == CE_CMD_ADVANCE) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "ATTACK"); pl.orderTone = CE_TONE_ACTIVE; }
            else if (sq.verb == CE_CMD_MOVE) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "MOVE"); pl.orderTone = CE_TONE_ACTIVE; }
            else if (sq.verb == CE_CMD_HOLD) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "HOLD"); pl.orderTone = CE_TONE_HOLD; }
            else if (sq.verb == CE_CMD_FALLBACK) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "FALLBACK"); pl.orderTone = CE_TONE_ACTIVE; }
            else if (!sq.commandable) { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "SCRIPT"); pl.orderTone = CE_TONE_NEUTRAL; }
            else { snprintf(pl.orderBadge, sizeof(pl.orderBadge), "AI"); pl.orderTone = CE_TONE_NEUTRAL; }
        }
    }
    // Number duplicate squad labels ("Muspell Tanks 1", "Muspell Tanks 2", "Gun Walkers 1", ...)
    // so every squad card in the bottom bar is immediately distinguishable.
    {
        char baseNames[kCeHudMaxPlacards][sizeof(gHudPlacards[0].shortName)] = {};
        for (int i = squadStart; i < gHudPlacardCount; ++i)
            snprintf(baseNames[i], sizeof(baseNames[i]), "%s", gHudPlacards[i].shortName);
        for (int i = squadStart; i < gHudPlacardCount; ++i) {
            int total = 0, rank = 0;
            for (int j = squadStart; j < gHudPlacardCount; ++j) {
                if (strcmp(baseNames[i], baseNames[j]) == 0) {
                    ++total;
                    if (j <= i) ++rank;
                }
            }
            if (total > 1) {
                char numbered[sizeof(gHudPlacards[0].shortName)];
                char suffix[12];
                snprintf(suffix, sizeof(suffix), " %d", rank);
                const size_t sufLen = strlen(suffix);
                const size_t maxBase = sizeof(numbered) > sufLen + 1 ? (sizeof(numbered) - sufLen - 1) : 0;
                snprintf(numbered, sizeof(numbered), "%.*s%s", int(maxBase), baseNames[i], suffix);
                CeCmdHudSanitizeText(numbered, gHudPlacards[i].shortName, sizeof(gHudPlacards[i].shortName));
            }
        }
    }

    if (summaryOut && summaryRoom > 0) {
        if (selUnits > 0)
            snprintf(summaryOut, summaryRoom, "%d unit%s selected", selUnits, selUnits == 1 ? "" : "s");
        else if (structCount > 0)
            snprintf(summaryOut, summaryRoom, "%d squad%s  %d factor%s", friendlySquads, friendlySquads == 1 ? "" : "s",
                     structCount, structCount == 1 ? "y" : "ies");
        else
            snprintf(summaryOut, summaryRoom, "%d squad%s", friendlySquads, friendlySquads == 1 ? "" : "s");
        if (gUnitHiddenCount > 0) {
            const size_t used = strlen(summaryOut);
            if (used + 16 < summaryRoom)
                snprintf(summaryOut + used, summaryRoom - used, "  +%d hidden", gUnitHiddenCount);
        }
    }

    gHudLayout = CeCmdHudComputeLayout(L.w, L.h, L.s, StrategicUiBottom(), gHudPlacardCount, gHudPage);
    gHudPage = gHudLayout.pageIndex;

    // ---- Right Dock Command Card Model ----
    gHudCard = CeCmdCardModel{};
    CeCmdStructureView selStruct = {};
    if (CeCmdStructureSelected(&selStruct)) {
        gHudCard.hasSelection = true;
        gHudCard.isStructure = true;
        gHudCard.hasArmour = true;
        gHudCard.armourRatio = CeCmdHudArmourRatio(selStruct.life, selStruct.maxLife, 1, 1);
        CeCmdHudShortName(selStruct.name, false, true, gHudCard.title, sizeof(gHudCard.title));
        char prodShort[20] = "Unit";
        if (selStruct.queuedOption >= 0 && selStruct.queuedOption < selStruct.optionCount)
            CeCmdHudShortName(selStruct.options[selStruct.queuedOption].unitName, false, false, prodShort, sizeof(prodShort));
        else if (selStruct.lastProductName[0])
            CeCmdHudShortName(selStruct.lastProductName, false, false, prodShort, sizeof(prodShort));
        if (selStruct.stage == CE_FACTORY_QUEUED) {
            gHudCard.showProgress = true;
            gHudCard.progressPercent = selStruct.percent;
            snprintf(gHudCard.progressLabel, sizeof(gHudCard.progressLabel), "Building %s %d%%", prodShort, selStruct.percent);
        } else if (selStruct.stage == CE_FACTORY_LAUNCHING) {
            gHudCard.showProgress = true;
            gHudCard.progressPercent = 100;
            snprintf(gHudCard.progressLabel, sizeof(gHudCard.progressLabel), "Launching %s", prodShort);
        } else if (selStruct.stage == CE_FACTORY_READY) {
            snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Ready - %s deployed", prodShort);
        } else if (selStruct.stage == CE_FACTORY_CANCELLED) {
            snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Build order cancelled");
        } else if (selStruct.stage == CE_FACTORY_BLOCKED) {
            snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Blocked or offline");
        } else {
            bool anyReady = false, anySpawning = false, anyEnabled = false;
            int firstRem = -1;
            for (int k = 0; k < selStruct.optionCount; ++k) {
                if (selStruct.options[k].ready) {
                    if (!anyReady) firstRem = selStruct.options[k].remaining;
                    anyReady = true;
                }
                if (selStruct.options[k].spawning) anySpawning = true;
                if (selStruct.options[k].enabled) anyEnabled = true;
            }
            if (anyReady && firstRem > 0)
                snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Ready - %d batch%s available", firstRem, firstRem == 1 ? "" : "es");
            else if (anyReady)
                snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Idle - Select build order");
            else if (anySpawning)
                snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Deploying active wave");
            else if (anyEnabled)
                snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Reserve depleted");
            else
                snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Factory offline");
        }
        const bool busy = selStruct.stage == CE_FACTORY_QUEUED || selStruct.stage == CE_FACTORY_LAUNCHING ||
                          (selStruct.isExercise && selStruct.stage == CE_FACTORY_READY);
        for (int k = 0; k < selStruct.optionCount && k < 4; ++k) {
            CeCmdCardButton& b = gHudCard.buttons[k];
            b.action = CeCmdCardAction(CE_CARD_BUILD_0 + k);
            char unitShort[14];
            CeCmdHudShortName(selStruct.options[k].unitName, false, false, unitShort, sizeof(unitShort));
            snprintf(b.label, sizeof(b.label), "Build %s", unitShort);
            snprintf(b.hotkey, sizeof(b.hotkey), k == 0 ? "U" : "");
            b.enabled = !busy && selStruct.options[k].ready;
            b.active = (selStruct.stage == CE_FACTORY_QUEUED || selStruct.stage == CE_FACTORY_LAUNCHING) && selStruct.queuedOption == k;
        }
        if (selStruct.isExercise && selStruct.stage == CE_FACTORY_READY) {
            const int delOrd = CeCmdDeliveredSquad();
            gHudCard.buttons[4].action = CE_CARD_SELECT_DELIVERED;
            snprintf(gHudCard.buttons[4].label, sizeof(gHudCard.buttons[4].label), "Select Tank");
            gHudCard.buttons[4].enabled = delOrd >= 0;
            gHudCard.buttons[5].action = CE_CARD_TAKE_DELIVERED;
            snprintf(gHudCard.buttons[5].label, sizeof(gHudCard.buttons[5].label), "Take Ctrl");
            snprintf(gHudCard.buttons[5].hotkey, sizeof(gHudCard.buttons[5].hotkey), "Ent");
            gHudCard.buttons[5].enabled = delOrd >= 0 && CePlayUnitCanTakeDelivery();
        } else {
            gHudCard.buttons[5].action = CE_CARD_CANCEL_BUILD;
            snprintf(gHudCard.buttons[5].label, sizeof(gHudCard.buttons[5].label), "Cancel");
            gHudCard.buttons[5].hotkey[0] = 0;
            gHudCard.buttons[5].enabled = selStruct.stage == CE_FACTORY_QUEUED;
        }
        return;
    }

    // Unit or Squad Command Card
    const CeCmdSquad* sq = ByOrdinal(gSelected);
    bool canMove = false, canAttack = false, canHold = false, canFollow = false, canTake = false, canReturn = false;
    bool armedMove = false, armedAttack = false;
    if (selUnits > 0) {
        gHudCard.hasSelection = true;
        gHudCard.isMulti = selUnits > 1;
        float lifeSum = 0.f, maxSum = 0.f;
        const CeCmdUnitSnapshot* firstUnit = NULL;
        int ownedCount = 0;
        for (int i = 0; i < gUnitNativeCount; ++i) {
            if (!gUnitUi.IsSelected(gUnitNative[i].key)) continue;
            if (!firstUnit) firstUnit = &gUnitNative[i];
            if (gUnitNative[i].life > 0.f) lifeSum += gUnitNative[i].life;
            if (gUnitNative[i].maxLife > 0.f) maxSum += gUnitNative[i].maxLife;
            const CeCmdSquad* usq = ByOrdinal(gUnitNative[i].ordinal);
            const bool deploys = (usq && usq->deploys) || strstr(gUnitNative[i].name, "Artillery") != NULL;
            if (gUnitNative[i].commandable && gUnitNative[i].groundPoint) {
                canHold = true;
                if (!deploys) { canMove = true; canFollow = true; }
            }
            if (gUnitNative[i].orderOwned) { ++ownedCount; canReturn = true; }
            if (UnitWholeSquad(gUnitNative[i].ordinal)) {
                if (usq && !usq->deploys) { canMove = true; canAttack = true; canFollow = true; }
                if (usq) canHold = true;
                if (usq && usq->verb != CE_CMD_NONE) canReturn = true;
            }
        }
        gHudCard.hasArmour = true;
        gHudCard.armourRatio = CeCmdHudArmourRatio(lifeSum, maxSum, selUnits, selUnits);
        if (selUnits == 1 && firstUnit) {
            CeCmdHudShortName(firstUnit->name, false, false, gHudCard.title, sizeof(gHudCard.title));
            CeCmdUnitOrderView ov = {};
            const char* ordStr = "AI";
            if (CeCmdUnitOrderRead(firstUnit->key, &ov) && ov.owned)
                ordStr = ov.order == CE_UNIT_ORDER_MOVE ? "Moving" : ov.order == CE_UNIT_ORDER_HOLD ? "Holding" : "AI";
            snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Order: %s", ordStr);
            struct TakeCheck { const char* reason; } chk = {};
            canTake = CeCmdUnitVisit(firstUnit->key, [](CUnit* unit, int number, void* user) -> bool {
                TakeCheck* c = static_cast<TakeCheck*>(user);
                return CePlayUnitCanTakeSelection(CePlayUnitSelectionWorld(), unit, number, &c->reason);
            }, &chk);
        } else {
            snprintf(gHudCard.title, sizeof(gHudCard.title), "%d Units Selected", selUnits);
            snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), ownedCount ? "Order: Active" : "Order: Squad AI");
        }
        UnitPanel::Action armedAct = UnitPanel::Action::Move;
        if (gUnitUi.ArmedAction(&armedAct)) {
            armedMove = armedAct == UnitPanel::Action::Move;
            armedAttack = armedAct == UnitPanel::Action::Advance;
        }
    } else if (sq) {
        gHudCard.hasSelection = true;
        gHudCard.isMulti = gSel.n > 1;
        gHudCard.hasArmour = true;
        gHudCard.armourRatio = CeCmdHudArmourRatio(sq->life, sq->maxLife, sq->members, sq->peak);
        if (gSel.n > 1)
            snprintf(gHudCard.title, sizeof(gHudCard.title), "%.24s   %d units +%d", sq->unit[0] ? sq->unit : "Squad", sq->members, gSel.n - 1);
        else
            snprintf(gHudCard.title, sizeof(gHudCard.title), "%.28s   %d unit%s", sq->unit[0] ? sq->unit : (sq->vehicles ? "Vehicles" : "Infantry"), sq->members, sq->members == 1 ? "" : "s");
        snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Order: %s%s", OrderName(*sq), sq->deploys ? "   Hold only" : "");
        canMove = !sq->deploys;
        canAttack = !sq->deploys;
        canHold = true;
        canFollow = !sq->deploys;
        canTake = CanTakeSelectedSquadUnit(NULL);
        canReturn = sq->verb != CE_CMD_NONE;
        if (!canReturn) {
            for (int i = 0; i < gSel.n; ++i)
                if (const CeCmdSquad* ms = ByOrdinal(gSel.ids[i])) if (ms->verb != CE_CMD_NONE) canReturn = true;
        }
        armedMove = gArmed && gStrategicClickOrder == CE_CMD_MOVE;
        armedAttack = gArmed && gStrategicClickOrder == CE_CMD_ADVANCE;
    } else {
        snprintf(gHudCard.title, sizeof(gHudCard.title), "No Unit or Squad Picked");
        snprintf(gHudCard.subtitle, sizeof(gHudCard.subtitle), "Click a unit, squad or placard");
    }

    static const char* const labels[] = {"Move", "Advance", "Hold", "Fall Back"};
    (void)labels;
    gHudCard.buttons[0] = {CE_CARD_MOVE,         "Move",      "RMB", canMove,   armedMove};
    gHudCard.buttons[1] = {CE_CARD_ATTACK,       "Attack",    "G",   canAttack, armedAttack};
    gHudCard.buttons[2] = {CE_CARD_HOLD,         "Hold",      "H",   canHold,   false};
    gHudCard.buttons[3] = {CE_CARD_FOLLOW_BE,    "Follow BE", "Spc", canFollow, false};
    gHudCard.buttons[4] = {CE_CARD_TAKE_CONTROL, "Take Ctrl", "Ent", canTake,   false};
    gHudCard.buttons[5] = {CE_CARD_RETURN_AI,    "Return AI", "Del", canReturn, false};
}

void StrategicTitle(char* out, unsigned room) {
    if (gMatch.nodes > 0)
        snprintf(out, room, "Commander   Battle Line %d of %d   Bases %d to %d", gMatch.held, gMatch.nodes, gMatch.buildings[0], gMatch.buildings[1]);
    else if (gMatch.buildings[0] > 0 || gMatch.buildings[1] > 0)
        snprintf(out, room, "Commander   Bases %d to %d", gMatch.buildings[0], gMatch.buildings[1]);
    else snprintf(out, room, "Commander");
}
void StrategicTitleDisplay(char* out, unsigned room) {
    if (gMatch.nodes > 0)
        snprintf(out, room, "Commander   Battle Line %d of %d   Bases %d - %d", gMatch.held, gMatch.nodes, gMatch.buildings[0], gMatch.buildings[1]);
    else if (gMatch.buildings[0] > 0 || gMatch.buildings[1] > 0)
        snprintf(out, room, "Commander   Bases %d - %d", gMatch.buildings[0], gMatch.buildings[1]);
    else snprintf(out, room, "Commander");
}
// Compact top-right utility prompt (keeps full bottom bar clear for RTS placards and command card).
void StrategicPrompt(char* out, unsigned room) {
    const char* const action[5] = {"Right Click Move", "H Hold", "C Focus", "F1 Help", "Esc Cockpit"};
    (void)action;
    snprintf(out, room, "F1 Help   Esc Cockpit");
}
// Legacy two-line squad summary kept for callers and tests.
void StrategicCardLines(char* first, char* second, unsigned room) {
    const CeCmdSquad* squad = ByOrdinal(gSelected);
    if (!squad) {
        snprintf(first, room, "No Unit or Squad Picked");
        snprintf(second, room, "Click a unit, squad or placard");
        return;
    }
    snprintf(first, room, "%s   %d unit%s", squad->unit[0] ? squad->unit : (squad->vehicles ? "Vehicles" : "Infantry"), squad->members, squad->members == 1 ? "" : "s");
    if (gSel.n > 1) snprintf(first + strlen(first), room - strlen(first), "   +%d squad%s", gSel.n - 1, gSel.n == 2 ? "" : "s");
    snprintf(second, room, "Order: %s%s", OrderName(*squad), squad->deploys ? "   Hold only" : "");
}
float StrategicCardWidth() {
    return gHudLayout.valid ? gHudLayout.barRect.w : kCardW * L.s;
}
int FactoryCardAt(float x, float y) {
    if (gDown[DIK_F1]) return -1;
    CeFactoryView f;
    if (!CeFactoryRead(f)) return -1;
    const float u=L.s, left=8*u, top=56*u, width=196*u;
    const int height = !f.selected ? 24 : (f.stage == CE_FACTORY_READY ? 122 : 99);
    if (x<left || x>=left+width || y<top || y>=top+height*u) return -1;
    if (!f.selected) return 1;
    const float row=(y-top)/u;
    if (row >= 52 && row < 73) return f.stage == CE_FACTORY_READY ? 5 : 2;
    if (row >= 74 && row < 96) return f.stage == CE_FACTORY_READY ? 6 : f.stage == CE_FACTORY_QUEUED ? 3 : 4;
    if (f.stage == CE_FACTORY_READY && row >= 97 && row < 119) return 4;
    return 0;
}
bool StrategicPanelAt(float x, float y) {
    if (gDown[DIK_F1] && gActiveHelpRectValid &&
        x >= gActiveHelpRect.x0 && x < gActiveHelpRect.x1 &&
        y >= gActiveHelpRect.y0 && y < gActiveHelpRect.y1) return true;
    if (FactoryCardAt(x,y) >= 0 || UnitPanelAt(x,y)) return true;
    const float u = L.s;
    char text[160];
    StrategicTitleDisplay(text, sizeof text);
    if (y < 30.f * u && x < 48.f * u + TextWidth(text, u)) return true;
    StrategicPrompt(text, sizeof text);
    if (y < 30.f * u && x >= L.w - (44.f * u + TextWidth(text, u))) return text[0] != 0;
    const CeCmdHudLayout lay = CeCmdHudComputeLayout(L.w, L.h, L.s, StrategicUiBottom(), gHudPlacardCount, gHudPage);
    if (CeCmdHudHitTest(lay, x, y).kind != CE_HUD_HIT_NONE) return true;
    return false;
}
bool StrategicRectsOverlap(float ax0, float ay0, float ax1, float ay1,
                           float bx0, float by0, float bx1, float by1) {
    return ax0 < bx1 && ax1 > bx0 && ay0 < by1 && ay1 > by0;
}

// True only when a world-space overlay rect lies inside the viewport and clears every
// active HUD panel (top title/help capsules, bottom RTS bar, notice toast, F1 help,
// factory card, and dialogue reservation) so world markers never bleed over HUD text.
bool StrategicRectClearsHud(float x0, float y0, float x1, float y1) {
    if (x0 < 0.f || y0 < 0.f || x1 > L.w || y1 > L.h) return false;
    const float u = L.s;
    const float pad = 2.f * u;
    char text[160];
    StrategicTitleDisplay(text, sizeof text);
    if (StrategicRectsOverlap(x0, y0, x1, y1, 16.f * u - pad, 4.f * u, 48.f * u + TextWidth(text, u) + pad, 30.f * u + pad))
        return false;
    StrategicPrompt(text, sizeof text);
    if (text[0] && StrategicRectsOverlap(x0, y0, x1, y1, L.w - (44.f * u + TextWidth(text, u)) - pad, 4.f * u, L.w, 30.f * u + pad))
        return false;
    const CeCmdHudLayout lay = gHudLayout.valid
        ? gHudLayout
        : CeCmdHudComputeLayout(L.w, L.h, L.s, StrategicUiBottom(), gHudPlacardCount, gHudPage);
    if (lay.valid && StrategicRectsOverlap(x0, y0, x1, y1,
            lay.barRect.x - 16.f * u, lay.barRect.y - pad,
            lay.barRect.x + lay.barRect.w + 16.f * u, lay.barRect.y + lay.barRect.h + pad))
        return false;
    if (gActiveNoticeRectValid && StrategicRectsOverlap(x0, y0, x1, y1,
            gActiveNoticeRect.x0 - pad, gActiveNoticeRect.y0 - pad,
            gActiveNoticeRect.x1 + pad, gActiveNoticeRect.y1 + pad))
        return false;
    if (gActiveHelpRectValid && StrategicRectsOverlap(x0, y0, x1, y1,
            gActiveHelpRect.x0 - pad, gActiveHelpRect.y0 - pad,
            gActiveHelpRect.x1 + pad, gActiveHelpRect.y1 + pad))
        return false;
    CeFactoryView f;
    if (!gDown[DIK_F1] && CeFactoryRead(f)) {
        const float fx = 8.f * u, fy = 56.f * u, fw = 196.f * u;
        const float fh = (f.selected ? (f.stage == CE_FACTORY_READY ? 122.f : 99.f) : 24.f) * u;
        if (StrategicRectsOverlap(x0, y0, x1, y1, fx - pad, fy - pad, fx + fw + pad, fy + fh + pad))
            return false;
    }
    const UnitPanel::PanelLayout& up = gUnitUi.Panel().Layout();
    if (up.visible && StrategicRectsOverlap(x0, y0, x1, y1, up.panel.x, up.panel.y, up.panel.x + up.panel.w, up.panel.y + up.panel.h))
        return false;
    if (StrategicDialogueAt(x0, y0) || StrategicDialogueAt(x1, y0) ||
        StrategicDialogueAt(x0, y1) || StrategicDialogueAt(x1, y1))
        return false;
    return true;
}

bool StrategicWorldArea(float x, float y) {
    return x >= 0 && y >= 0 && x < L.w && y < L.h && !StrategicPanelAt(x,y) && !StrategicDialogueAt(x,y);
}

bool StrategicHeight(float x, float y, float* height) {
    if (!height || !std::isfinite(x) || !std::isfinite(y) || x < gBoundsX0 || y < gBoundsY0 ||
        x >= gBoundsX1 || y >= gBoundsY1 || x < 0 || y < 0 ||
        x >= float(MAP.mGridWidth - 1) || y >= float(MAP.mGridHeight - 1)) return false;
    const float h = MAP.Collide(FVector(x, y, 0));
    if (!std::isfinite(h)) return false;
    *height = h; return true;
}

bool IsDryGroundPoint(float x, float y) {
    float h = 0.f;
    return StrategicHeight(x, y, &h) && h < MAP.GetWaterLevel();
}

// Compute initial battle-facing yaw from (fx, fy) toward the visible enemy centroid (or dry-land
// centroid when no enemy is visible yet). In FMatrix::MakeRotationYawF, forward (0, 1, 0) maps to
// (-sinf(yaw), cosf(yaw), 0), so yaw = atan2f(-dx, dy) aims directly along (dx, dy).
bool ComputeStrategicInitialYaw(float fx, float fy, float* outYaw) {
    if (!outYaw) return false;
    double exSum = 0.0, eySum = 0.0;
    int enemyCount = 0;
    for (int i = 0; i < gCount; ++i) {
        if (gSquads[i].friendly || !gSquads[i].visible || gSquads[i].members <= 0) continue;
        exSum += double(gSquads[i].x);
        eySum += double(gSquads[i].y);
        ++enemyCount;
    }
    if (enemyCount > 0) {
        const float dx = float(exSum / double(enemyCount)) - fx;
        const float dy = float(eySum / double(enemyCount)) - fy;
        if (dx * dx + dy * dy > 1.f) { *outYaw = atan2f(-dx, dy); return true; }
    }
    if (gBoundsX1 > gBoundsX0 && gBoundsY1 > gBoundsY0) {
        const int kSamples = 24;
        const float cellX = (gBoundsX1 - gBoundsX0) / float(kSamples);
        const float cellY = (gBoundsY1 - gBoundsY0) / float(kSamples);
        double lxSum = 0.0, lySum = 0.0;
        int dryCount = 0;
        for (int j = 0; j < kSamples; ++j) {
            const float wy = gBoundsY0 + (float(j) + 0.5f) * cellY;
            for (int i = 0; i < kSamples; ++i) {
                const float wx = gBoundsX0 + (float(i) + 0.5f) * cellX;
                if (IsDryGroundPoint(wx, wy)) { lxSum += double(wx); lySum += double(wy); ++dryCount; }
            }
        }
        if (dryCount > 0) {
            const float dx = float(lxSum / double(dryCount)) - fx;
            const float dy = float(lySum / double(dryCount)) - fy;
            if (dx * dx + dy * dy > 1.f) { *outYaw = atan2f(-dx, dy); return true; }
        }
    }
    return false;
}

void StartStrategicFocus(const CeCmdSquad &squad) {
    gFocusTrip.squad = squad.ordinal;
    gFocusTrip.fromX = gViewX + gViewSpan * 0.5f;
    gFocusTrip.fromY = gViewY + gViewSpan * 0.5f;
    gFocusTrip.fromZ = gStrategicFocusZ;
    gFocusTrip.start = Ns();
}

void StepStrategicFocus(const CeCommanderCameraInput &input, float &centreX, float &centreY) {
    if (gFocusTrip.squad < 0) return;
    const CeCmdSquad *squad = ByOrdinal(gFocusTrip.squad);
    CeFactoryView factory;
    const bool focusFactory = gFocusTrip.squad == kFactoryFocus && CeFactoryRead(factory) && factory.selected;
    CeCmdStructureView focusStructData = {};
    bool focusStruct = false;
    if (gFocusTrip.squad == kStructureFocus && gStructFocusHandle != kCeEntityHandleInvalid) {
        CeCmdStructureView allStructs[kCeMaxStructures];
        const int nStructs = CeCmdStructureList(allStructs, kCeMaxStructures);
        for (int i = 0; i < nStructs; ++i)
            if (static_cast<CeEntityHandle>(allStructs[i].thingNumber) == gStructFocusHandle) { focusStructData = allStructs[i]; focusStruct = true; break; }
    }
    float unitX = 0, unitY = 0;
    const bool focusUnit = gFocusTrip.squad == kUnitFocus && UnitFocusPosition(&unitX, &unitY);
    if (input.pan_horizontal || input.pan_vertical || input.orbit_horizontal || input.orbit_vertical || input.zoom ||
        (!focusFactory && !focusUnit && !focusStruct && (gSelected != gFocusTrip.squad || !squad || !squad->friendly || !squad->members))) {
        gFocusTrip.squad = -1;
        return;
    }
    const float targetX = focusUnit ? unitX : focusStruct ? focusStructData.x : focusFactory ? factory.x : squad->x;
    const float targetY = focusUnit ? unitY : focusStruct ? focusStructData.y : focusFactory ? factory.y : squad->y;
    const float phase = static_cast<float>((std::min)(1.0, SecondsBetween(gFocusTrip.start, Ns()) / 0.32));
    const float ease = phase * phase * (3.f - 2.f * phase);
    float height;
    if (!StrategicHeight(targetX, targetY, &height)) height = gFocusTrip.fromZ;
    centreX = gFocusTrip.fromX + (targetX - gFocusTrip.fromX) * ease;
    centreY = gFocusTrip.fromY + (targetY - gFocusTrip.fromY) * ease;
    gStrategicFocusZ = gFocusTrip.fromZ + (height - gFocusTrip.fromZ) * ease;
    if (phase >= 1.f) gFocusTrip.squad = -1;
}

bool StrategicPixel(const CeWorldViewSample& view, float x, float y, CeViewPoint* pixel) {
    float height;
    if (!StrategicHeight(x, y, &height)) return false;
    const CeViewPoint world = {x, y, height};
    return CeViewProject(view.view, view.projection, view.viewport, world, pixel);
}

// Keep a live order readable when its destination leaves the picture. Only the
// visible squad's forward segment can supply a bearing; never mirror a point
// behind the camera onto the opposite side of the screen.
bool StrategicRouteBearing(const CeWorldViewSample &view, const CeCmdSquad &squad,
                          const CeViewPoint &from, float t, CeViewPoint &tip) {
    if (!StrategicWorldArea(from.x, from.y)) return false;
    const float arm = 8.0f * t;
    const float inset = 3.0f + arm;
    const float left = inset, right = L.w - inset;
    const float top = 32.0f * L.s + inset, bottom = L.h - 42.0f * L.s - inset;
    if (from.x < left || from.x > right || from.y < top || from.y > bottom) return false;
    float firstZ, lastZ;
    if (!StrategicHeight(squad.x, squad.y, &firstZ) ||
        !StrategicHeight(squad.gx, squad.gy, &lastZ)) return false;
    float low = 0, high = 1;
    tip = from;
    for (int step = 0; step < 14; ++step) {
        const float f = (low + high) * 0.5f;
        const CeViewPoint p = {squad.x + (squad.gx - squad.x) * f,
                               squad.y + (squad.gy - squad.y) * f,
                               firstZ + (lastZ - firstZ) * f};
        CeViewPoint out;
        if (CeViewProject(view.view, view.projection, view.viewport, p, &out) &&
            out.x >= left && out.x <= right && out.y >= top && out.y <= bottom) {
            low = f;
            tip = out;
        } else high = f;
    }
    // A depth boundary with no nearby screen edge is not an off-screen bearing.
    const float edgeTolerance = 2.0f * t;
    if (tip.x > left + edgeTolerance && tip.x < right - edgeTolerance &&
        tip.y > top + edgeTolerance && tip.y < bottom - edgeTolerance) return false;
    const float dx = tip.x - from.x, dy = tip.y - from.y;
    return dx*dx + dy*dy >= (4.0f * arm) * (4.0f * arm);
}

void DrawRouteArrow(const CeViewPoint &from, const CeViewPoint &tip, float t) {
    const float dx = tip.x - from.x, dy = tip.y - from.y;
    const float len = sqrtf(dx*dx + dy*dy);
    if (!(len > 0)) return;
    const float ux = dx / len, uy = dy / len, wing = 4.5f * t, back = 7.0f * t;
    Dots(from.x, from.y, tip.x - ux * back, tip.y - uy * back, t, kOrderLine);
    // Short solid strokes read as an open arrowhead, unlike the square goal pin.
    const int steps = int(ceilf(back));
    for (int side = -1; side <= 1; side += 2) {
        for (int j = 0; j <= steps; ++j) {
            if (side == 1 && j == 0) continue;
            const float f = float(j) / float(steps);
            const float x = tip.x + (-ux * back - side * uy * wing) * f;
            const float y = tip.y + (-uy * back + side * ux * wing) * f;
            Box(floorf(x + 0.5f), floorf(y + 0.5f), t, t, kOrderLine);
        }
    }
}


bool StrategicClipAxis(double start, double delta, double lo, double hi, double* first, double* last) {
    if (std::fabs(delta) < 1e-12) return start >= lo && start <= hi;
    double a = (lo - start) / delta, b = (hi - start) / delta;
    if (a > b) { const double swap = a; a = b; b = swap; }
    *first = (std::max)(*first, a); *last = (std::min)(*last, b);
    return *first <= *last;
}

// Intersect the rendered forward ray with the real heightfield. Limit sampling
// to the map rectangle; reject sky, missing/stale views and below-ground starts.
// The first sampled crossing is refined, never guessed from a flat pilot plane.
// This is ground picking, not arbitrary structure/mesh collision.
bool StrategicGround(const CeWorldViewSample& view, float px, float py, CeViewPoint* hit) {
    CeViewPoint nearPoint, farPoint;
    if (!hit || !CeViewRay(view.view, view.projection, view.viewport, px, py, &nearPoint, &farPoint)) return false;
    const double dx = double(farPoint.x) - nearPoint.x, dy = double(farPoint.y) - nearPoint.y,
                 dz = double(farPoint.z) - nearPoint.z;
    double first = 0, last = 1;
    const double hiX = (std::min)(double(gBoundsX1) - .001, double(MAP.mGridWidth - 1) - .001);
    const double hiY = (std::min)(double(gBoundsY1) - .001, double(MAP.mGridHeight - 1) - .001);
    if (!StrategicClipAxis(nearPoint.x, dx, (std::max)(0.0, double(gBoundsX0)), hiX, &first, &last) ||
        !StrategicClipAxis(nearPoint.y, dy, (std::max)(0.0, double(gBoundsY0)), hiY, &first, &last)) return false;
    auto gap = [&](double t, double* difference) {
        const float x = float(nearPoint.x + dx * t), y = float(nearPoint.y + dy * t);
        float height;
        if (!StrategicHeight(x, y, &height)) return false;
        *difference = nearPoint.z + dz * t - height;
        return std::isfinite(*difference);
    };
    double previousGap;
    if (!gap(first, &previousGap) || previousGap >= 0) return false;
    const double span = (std::max)(std::fabs(dx), std::fabs(dy)) * (last - first);
    const int steps = (std::min)(1024, (std::max)(2, int(std::ceil(span))));
    double previous = first;
    for (int i = 1; i <= steps; ++i) {
        const double t = first + (last - first) * double(i) / steps;
        double difference;
        if (!gap(t, &difference)) return false;
        if (difference >= 0) {
            double lo = previous, hi = t;
            for (int n = 0; n < 18; ++n) {
                const double mid = (lo + hi) * .5;
                if (!gap(mid, &difference)) return false;
                if (difference >= 0) hi = mid; else lo = mid;
            }
            CeViewPoint point = {float(nearPoint.x + dx * hi), float(nearPoint.y + dy * hi), 0};
            if (!StrategicHeight(point.x, point.y, &point.z)) return false;
            *hit = point; return true;
        }
        previous = t;
    }
    return false;
}

// The panel owns intent and exact keys; only these native callbacks issue orders.

bool UnitRequestHas(const UnitControl::Request* request, const CeCommandLifetimeKey& key) {
    if (!request) return gUnitUi.IsSelected(key);
    for (unsigned i = 0; i < request->key_count; ++i)
        if (UnitControl::SameKey(request->keys[i], key)) return true;
    return false;
}

bool UnitWholeSquad(int ordinal, const UnitControl::Request* request) {
    const CeCmdSquad* squad = ByOrdinal(ordinal);
    if (!gUnitSnapshotComplete || !squad || !squad->commandable || squad->members <= 0) return false;
    int members = 0;
    for (int i = 0; i < gUnitNativeCount; ++i) {
        const CeCmdUnitSnapshot& unit = gUnitNative[i];
        if (unit.ordinal != ordinal) continue;
        if (!unit.commandable || !UnitRequestHas(request, unit.key)) return false;
        ++members;
    }
    // A filtered, missing, transferred or new native member cannot be ordered
    // merely because all of an older UI selection is still present.
    return members == squad->members;
}

struct UnitTakeContext { const char* reason; };
bool UnitCanTake(CUnit* unit, int number, void* user) {
    UnitTakeContext* context = static_cast<UnitTakeContext*>(user);
    return CePlayUnitCanTakeSelection(CePlayUnitSelectionWorld(), unit, number, &context->reason);
}
bool UnitTake(CUnit* unit, int number, void* user) {
    UnitTakeContext* context = static_cast<UnitTakeContext*>(user);
    return CePlayUnitTakeSelection(CePlayUnitSelectionWorld(), unit, number, &context->reason);
}

UnitPanel::TextSize UnitMeasure(void*, const char* text, float scale) {
    return {TextWidth(text, scale), (L.s > 0.f ? LineHeight() / L.s : CeCmdViewTextHeight()) * scale};
}

void RefreshUnitFrame() {
    bool collectedAll = false;
    const int rawCount = CeCmdUnitCollect(gUnitNative, UnitControl::kMaxUnits, &collectedAll);
    // Count live friendly/total units via the order layer (wrap-safe via live pointers)
    // so overflow beyond collector or panel limits reports accurate "+N hidden" without
    // freezing Commander picking or orders.
    int liveFriendly = 0, liveTotal = 0;
    const void* framePilot = NULL;
    const int pilotAlleg = PilotAllegiance(&framePilot);
    CeCmdCountLiveUnits(pilotAlleg, &liveFriendly, &liveTotal);
    // Prioritize units (selected -> player faction -> nearest to camera focus) into bounded slice.
    gUnitHiddenCount = 0;
    if (rawCount > 0) {
        static UnitControl::PrioritizeCandidate candidates[UnitControl::kMaxUnits];
        static int ordered[UnitControl::kMaxUnits];
        static CeCmdUnitSnapshot reordered[UnitControl::kMaxUnits];
        const float focusX = gViewX + gViewSpan * 0.5f;
        const float focusY = gViewY + gViewSpan * 0.5f;
        for (int i = 0; i < rawCount; ++i) {
            const CeCmdUnitSnapshot& u = gUnitNative[i];
            const CeCmdSquad* sq = ByOrdinal(u.ordinal);
            const float dx = u.position[0] - focusX;
            const float dy = u.position[1] - focusY;
            candidates[i].source_index = i;
            candidates[i].selected = gUnitUi.IsSelected(u.key) || (sq && sq->selected);
            candidates[i].player_faction = u.friendly;
            candidates[i].dist_sq = dx * dx + dy * dy;
        }
        int droppedHere = 0;
        const int kept = UnitControl::PrioritizeUnits(candidates, rawCount, UnitControl::kMaxUnits, ordered, &droppedHere);
        for (int i = 0; i < kept; ++i) reordered[i] = gUnitNative[ordered[i]];
        for (int i = 0; i < kept; ++i) gUnitNative[i] = reordered[i];
        gUnitNativeCount = kept;
        gUnitHiddenCount = droppedHere;
    } else {
        gUnitNativeCount = 0;
    }
    if (!collectedAll && gUnitNativeCount > 0) {
        const int extraHidden = liveFriendly > gUnitNativeCount ? (liveFriendly - gUnitNativeCount)
                              : (liveTotal > gUnitNativeCount ? (liveTotal - gUnitNativeCount) : 1);
        if (extraHidden > gUnitHiddenCount) gUnitHiddenCount = extraHidden;
    }
    // Capacity truncation no longer locks out picking/orders: keep the prioritized live slice usable.
    gUnitSnapshotComplete = collectedAll || (gUnitNativeCount > 0 && CeCmdUnitOrdersEligible());
    unsigned markCount = 0;
    const CeCmdViewUnitMark* marks = CeCmdViewUnitMarks(markCount);
    for (int i = 0; i < gUnitNativeCount; ++i) {
        const CeCmdUnitSnapshot& native = gUnitNative[i];
        UnitControl::UnitSnapshot& row = gUnitRows[i];
        row = UnitControl::UnitSnapshot{};
        row.key = native.key;
        UnitControl::UnitFacts& facts = row.facts;
        facts.kind = UnitPanel::EntityKind::Unit;
        facts.relation = native.friendly ? UnitPanel::TeamRelation::Friendly : UnitPanel::TeamRelation::Unknown;
        facts.control = native.commandable ? UnitPanel::ControlOwner::CommandAI :
            native.scripted ? UnitPanel::ControlOwner::Scripted : UnitPanel::ControlOwner::IndependentAI;
        facts.availability = UnitPanel::Availability::Available;
        facts.selectable = native.friendly;
        facts.capabilities = UnitPanel::ActionBit(UnitPanel::Action::Focus);
        const CeCmdSquad* squad = ByOrdinal(native.ordinal);
        const bool deploys = (squad && squad->deploys) || strstr(native.name, "Artillery") != NULL;
        if (native.commandable && native.groundPoint) {
            facts.capabilities |= UnitPanel::ActionBit(UnitPanel::Action::Hold);
            if (!deploys)
                facts.capabilities |= UnitPanel::ActionBit(UnitPanel::Action::Move);
        }
        if (native.orderOwned) facts.capabilities |= UnitPanel::ActionBit(UnitPanel::Action::ReturnToAI);
        if (squad && UnitWholeSquad(native.ordinal)) {
            facts.capabilities |= UnitPanel::ActionBit(UnitPanel::Action::Hold);
            if (!squad->deploys)
                facts.capabilities |= UnitPanel::ActionBit(UnitPanel::Action::Move) |
                    UnitPanel::ActionBit(UnitPanel::Action::Advance) | UnitPanel::ActionBit(UnitPanel::Action::FallBack);
            if (squad->verb != CE_CMD_NONE)
                facts.capabilities |= UnitPanel::ActionBit(UnitPanel::Action::ReturnToAI);
        }
        if (gUnitUi.SelectionCount() == 1 && gUnitUi.IsSelected(native.key)) {
            UnitTakeContext take = {};
            if (CeCmdUnitVisit(native.key, UnitCanTake, &take))
                facts.capabilities |= UnitPanel::ActionBit(UnitPanel::Action::TakeControl);
        }
        UnitPanel::EntityKey squadKey = {};
        if (!UnitControl::ToPanelKey(native.squadKey, &squadKey)) gUnitSnapshotComplete = false;
        else {
            facts.squad.present = true;
            facts.squad.squad_id = squadKey.id;
            facts.squad.squad_generation = squadKey.generation;
            facts.squad.has_member_count = native.members >= 0 && native.members <= 65535;
            facts.squad.member_count = facts.squad.has_member_count ? uint16_t(native.members) : 0;
            snprintf(facts.squad.name, sizeof(facts.squad.name), "Squad %d", native.ordinal + 1);
        }
        snprintf(facts.name, sizeof(facts.name), "%s", native.name);
        for (unsigned m = 0; m < markCount; ++m) {
            if (!UnitControl::SameKey(marks[m].key, native.key)) continue;
            row.screen = {marks[m].x, marks[m].y, marks[m].radius + 4.f * L.s,
                marks[m].x >= 0 && marks[m].x < L.w && marks[m].y >= 30.f * L.s &&
                marks[m].y < StrategicUiBottom() && !StrategicDialogueAt(marks[m].x, marks[m].y) &&
                !StrategicPanelAt(marks[m].x, marks[m].y)};
            break;
        }
    }
    UnitControl::Frame frame = {};
    frame.world = CeCmdUnitWorld();
    frame.snapshot_generation = ++gUnitSnapshotGeneration;
    frame.context_generation = gUnitContextGeneration;
    frame.targeted_actions = UnitPanel::ActionBit(UnitPanel::Action::Move) | UnitPanel::ActionBit(UnitPanel::Action::Advance);
    frame.complete = gUnitSnapshotComplete && StrategicUiReady() && gUnitPointerAvailable && !gDown[DIK_F1];
    frame.units = gUnitRows;
    frame.unit_count = gUnitNativeCount;
    gUnitUi.BeginFrame(frame);
    gUnitUi.SetDragThreshold(kBoxSlack * L.s);
    // The unified bottom Forseti RTS bar owns all panel/card hit-testing; keep the legacy
    // UnitPanel layout empty so invisible legacy buttons never intercept battlefield drag releases.
    UnitPanel::LayoutInput layout = {};
    layout.scale = L.s;
    layout.safe = {0.f, 0.f, 0.f, 0.f};
    layout.measure = UnitMeasure;
    gUnitUi.UpdateLayout(layout);
}

bool UnitPanelAt(float x, float y) {
    const UnitPanel::PanelLayout& layout = gUnitUi.Panel().Layout();
    return layout.visible && x >= layout.panel.x && x < layout.panel.x + layout.panel.w &&
        y >= layout.panel.y && y < layout.panel.y + layout.panel.h;
}

bool UnitFocusPosition(float* x, float* y) {
    static CeCmdUnitSnapshot current[UnitControl::kMaxUnits];
    bool complete = false;
    const int count = CeCmdUnitCollect(current, UnitControl::kMaxUnits, &complete);
    if ((!complete && count == 0) || !gUnitFocusCount) return false;
    double totalX = 0, totalY = 0;
    for (unsigned k = 0; k < gUnitFocusCount; ++k) {
        bool found = false;
        if (!gUnitUi.IsSelected(gUnitFocusKeys[k])) return false;
        for (int i = 0; i < count; ++i) {
            if (!UnitControl::SameKey(current[i].key, gUnitFocusKeys[k])) continue;
            totalX += current[i].position[0]; totalY += current[i].position[1]; found = true; break;
        }
        if (!found) return false;
    }
    *x = float(totalX / gUnitFocusCount); *y = float(totalY / gUnitFocusCount);
    return std::isfinite(*x) && std::isfinite(*y);
}

// A refusal of a click on the battlefield, shown beside the pointer where the click was.
void NoteHere(const char* text, float x, float y) {
    Note(text);
    gNoteAtX = x; gNoteAtY = y; gNoteAtUntil = gNoteUntil;
}

bool DispatchUnitRequest(const UnitControl::Request& request) {
    if (!StrategicCanAct() || !CeCmdUnitOrdersEligible() || request.world != CeCmdUnitWorld() ||
        request.context_generation != gUnitContextGeneration ||
        request.selection_revision != gUnitUi.Panel().SelectionRevision() || !request.key_count ||
        request.key_count > UnitControl::kMaxRequestKeys) { Note("Unit command is no longer available."); return false; }
    if (request.has_point && (!std::isfinite(request.point.x) || !std::isfinite(request.point.y) || !std::isfinite(request.point.z))) {
        Note("Point at the ground."); return false;
    }
    // Refresh both native cohort membership and each unit's lifetime before any
    // command. Every low-level dispatch below resolves its key again as well.
    Refresh();
    RefreshUnitFrame();
    if (!gUnitSnapshotComplete || !gUnitUi.FrameUsable() || request.world != CeCmdUnitWorld() ||
        request.selection_revision != gUnitUi.Panel().SelectionRevision()) {
        Note("Unit selection is unavailable. No order issued."); return false;
    }
    if (request.action == UnitPanel::Action::TakeControl) {
        if (request.key_count != 1 || request.selected_count != 1) return false;
        UnitTakeContext take = {};
        if (!CeCmdUnitVisit(request.keys[0], UnitCanTake, &take)) {
            Note(take.reason ? take.reason : "Unit is no longer available."); return false;
        }
        Close();
        if (!PortCeCmdMapOpen() && CeCmdUnitVisit(request.keys[0], UnitTake, &take)) return true;
        Note(take.reason ? take.reason : "Unit control is no longer available.");
        return false;
    }
    if (request.action == UnitPanel::Action::Focus) {
        gUnitFocusCount = request.key_count;
        for (unsigned i = 0; i < request.key_count; ++i) gUnitFocusKeys[i] = request.keys[i];
        float x, y;
        if (!UnitFocusPosition(&x, &y)) { gUnitFocusCount = 0; return false; }
        gFocusTrip.squad = kUnitFocus;
        gFocusTrip.fromX = gViewX + gViewSpan * .5f;
        gFocusTrip.fromY = gViewY + gViewSpan * .5f;
        gFocusTrip.fromZ = gStrategicFocusZ;
        gFocusTrip.start = Ns();
        return true;
    }
    int applied = 0, refused = 0, units = 0;      // units: the selected units whose order (their own or their squad's) was taken
    const char* refusalWhy = NULL;
    int issued[UnitControl::kMaxRequestKeys], issuedCount = 0;
    bool issuedOk[UnitControl::kMaxRequestKeys] = {};
    for (unsigned i = 0; i < request.key_count; ++i) {
        if (!StrategicCanAct() || !CeCmdUnitOrdersEligible() || request.world != CeCmdUnitWorld() ||
            request.context_generation != gUnitContextGeneration ||
            request.selection_revision != gUnitUi.Panel().SelectionRevision()) { ++refused; break; }
        const CeCommandLifetimeKey& key = request.keys[i];
        const CeCmdUnitSnapshot* native = NULL;
        for (int n = 0; n < gUnitNativeCount; ++n)
            if (UnitControl::SameKey(gUnitNative[n].key, key)) { native = &gUnitNative[n]; break; }
        if (!native || !native->commandable) { ++refused; continue; }
        const CeCmdSquad* squad = ByOrdinal(native->ordinal);
        const bool deploys = (squad && squad->deploys) || strstr(native->name, "Artillery") != NULL;
        const bool whole = UnitWholeSquad(native->ordinal, &request);
        const bool squadAction = request.action == UnitPanel::Action::Advance ||
            request.action == UnitPanel::Action::FallBack ||
            (whole && (request.action == UnitPanel::Action::Move || request.action == UnitPanel::Action::Hold)) ||
            (request.action == UnitPanel::Action::ReturnToAI && whole && squad && squad->verb != CE_CMD_NONE);
        if (squadAction) {
            bool already = false;
            for (int n = 0; n < issuedCount; ++n) if (issued[n] == native->ordinal) { already = true; if (issuedOk[n]) ++units; }
            if (already) continue;
            issuedOk[issuedCount] = false;
            issued[issuedCount++] = native->ordinal;
            if (!whole || !squad || ((request.action == UnitPanel::Action::Advance || request.action == UnitPanel::Action::Move) && !request.has_point)) { ++refused; continue; }
            const int ordinal = native->ordinal;
            const int verb = request.action == UnitPanel::Action::Move ? CE_CMD_MOVE :
                request.action == UnitPanel::Action::Hold ? CE_CMD_HOLD :
                request.action == UnitPanel::Action::Advance ? CE_CMD_ADVANCE :
                request.action == UnitPanel::Action::FallBack ? CE_CMD_FALLBACK : CE_CMD_RELEASE;
            // An earlier native callback may change another cohort or the world.
            Refresh(); RefreshUnitFrame();
            if (!StrategicCanAct() || request.world != CeCmdUnitWorld() || !CeCmdUnitOrdersEligible() ||
                !gUnitUi.FrameUsable() || request.context_generation != gUnitContextGeneration ||
                request.selection_revision != gUnitUi.Panel().SelectionRevision() ||
                !UnitWholeSquad(ordinal, &request)) { ++refused; continue; }
            const char* why = NULL;
            if (CeCmdIssue(ordinal, verb, request.point.x, request.point.y, &why)) {
                ++applied; ++units; issuedOk[issuedCount - 1] = true;
            } else {
                ++refused;
                if (!refusalWhy && why) refusalWhy = why;
            }
            continue;
        }
        bool accepted = false;
        if (request.action == UnitPanel::Action::Move && request.has_point) {
            if (deploys) { if (!refusalWhy) refusalWhy = "Artillery can only hold."; }
            else accepted = CeCmdUnitMove(key, request.point.x, request.point.y);
        }
        else if (request.action == UnitPanel::Action::Hold) accepted = CeCmdUnitHold(key);
        else if (request.action == UnitPanel::Action::ReturnToAI) accepted = CeCmdUnitReturnAI(key);
        if (accepted) {
            ++applied; ++units;
        } else ++refused;
    }
    // A failed callback may have superseded an earlier order; display the
    // observed current owner, never an optimistic request result.
    Refresh(); RefreshUnitFrame();
    // What happened, in the player's words: "9 units moving.", not how many commands were accepted.
    const char* doing = request.action == UnitPanel::Action::Move ? "moving" : request.action == UnitPanel::Action::Hold ? "holding" :
        request.action == UnitPanel::Action::Advance ? "advancing" : request.action == UnitPanel::Action::FallBack ? "falling back" :
        request.action == UnitPanel::Action::ReturnToAI ? "back on their own orders" : "ordered";
    char message[96];
    if (!applied) { Note(refusalWhy ? refusalWhy : "No selected unit can take that order."); return false; }
    if (units == 1) snprintf(message, sizeof(message), "1 unit %s.", doing);
    else snprintf(message, sizeof(message), "%d units %s.", units, doing);
    if (refused) snprintf(message + strlen(message), sizeof(message) - strlen(message), " %d could not.", refused);
    Note(message);
    return true;
}

bool DrainUnitRequest() {
    UnitControl::Request request = {};
    if (!gUnitUi.TakeRequest(&request) || !DispatchUnitRequest(request)) return false;
    if (request.action != UnitPanel::Action::Focus && request.action != UnitPanel::Action::TakeControl) {
        gOrderNs = Ns();
        gHudFollowSquad = -1;
    }
    return true;
}

bool UnitTargetAt(float x, float y, uint64_t targetToken) {
    CeWorldViewSample view;
    CeViewPoint target;
    if (!StrategicWorldArea(x, y) || !StrategicViewRead(&view) || !StrategicGround(view, x, y, &target)) {
        NoteHere("Point at the ground.", x, y); return false;
    }
    UnitControl::Request request = {};
    if (gUnitUi.SubmitTarget(targetToken, {target.x, target.y, target.z}, &request) == UnitControl::TargetStatus::Submitted &&
        DispatchUnitRequest(request)) {
        gOrderNs = Ns();
        gHudFollowSquad = -1;
        return true;
    }
    return false;
}

bool UnitAction(UnitPanel::Action action) {
    const UnitControl::ArmStatus status = gUnitUi.ArmAction(action);
    if (status == UnitControl::ArmStatus::Queued) return DrainUnitRequest();
    else if (status == UnitControl::ArmStatus::Armed) Note("Choose a point on the ground.");
    else Note("That action is unavailable for this selection.");
    return false;
}

// A ring on the ground round a world point (x, y at height z), r world units across its radius: what is selected and where an order goes stand on the
// battlefield, not in the window. Drawn in pieces; a piece with an end off the picture or under a panel is left out.
void GroundRing(const CeWorldViewSample& view, float x, float y, float z, float r, float t, DWORD colour) {
    const int n = 28; CeViewPoint prev = {}; bool have = false;
    for (int i = 0; i <= n; ++i) {
        const float a = 6.2831853f * float(i) / float(n);
        CeViewPoint p = {};
        const CeViewPoint world = {x + r * cosf(a), y + r * sinf(a), z};
        const bool ok = CeViewProject(view.view, view.projection, view.viewport, world, &p) &&
                        StrategicRectClearsHud(p.x - t, p.y - t, p.x + t, p.y + t);
        if (ok && have && StrategicRectClearsHud((std::min)(prev.x, p.x) - t, (std::min)(prev.y, p.y) - t,
                                                 (std::max)(prev.x, p.x) + t, (std::max)(prev.y, p.y) + t))
            Seg(prev.x, prev.y, p.x, p.y, t, colour);
        prev = p; have = ok;
    }
}
// The height an order's goal stands at: the ground, or the water over the sea.
float GoalHeight(float x, float y, float fallback) {
    float z;
    if (!StrategicHeight(x, y, &z)) return fallback;
    return (std::min)(z, MAP.GetWaterLevel());     // the game's Z grows downward: the higher of ground and water is the smaller
}
const DWORD kRingPicked = 0xffffffff, kRingSquad = 0xc0ffffff, kGoalDim = 0x60ffffff;

void DrawUnitSelection() {
    gUnitUi.PointerMove(float(MOUSE_X), float(MOUSE_Y));
    unsigned count = 0;
    const CeCmdViewUnitMark* marks = CeCmdViewUnitMarks(count);
    CeWorldViewSample ringView;
    const bool haveRingView = StrategicViewRead(&ringView);
    const float rt = (std::max)(1.5f, 1.5f * L.s);
    // Unit orders on the ground: a dotted line from each unit to where it was sent and one ring at each goal, until the unit is there.
    float goals[64][2]; int nGoals = 0;
    for (unsigned i = 0; i < count && haveRingView; ++i) {
        CeCmdUnitOrderView order = {};
        if (!CeCmdUnitOrderRead(marks[i].key, &order) || order.order != CE_UNIT_ORDER_MOVE || !order.owned) continue;
        const float gx = order.destination[0], gy = order.destination[1], dx = gx - marks[i].wx, dy = gy - marks[i].wy;
        if (dx * dx + dy * dy < 16.f) continue;      // there (within four units)
        const float gz = GoalHeight(gx, gy, order.destination[2]);
        const DWORD colour = gUnitUi.IsSelected(marks[i].key) ? kOrderLine : kGoalDim;
        CeViewPoint goal = {};
        const CeViewPoint world = {gx, gy, gz};
        if (CeViewProject(ringView.view, ringView.projection, ringView.viewport, world, &goal) && StrategicWorldArea(marks[i].x, marks[i].y) &&
            StrategicWorldArea(goal.x, goal.y)) Dots(marks[i].x, marks[i].y, goal.x, goal.y, rt, colour);
        bool known = false;
        for (int k = 0; k < nGoals && !known; ++k) known = fabsf(goals[k][0] - gx) < 1.5f && fabsf(goals[k][1] - gy) < 1.5f;
        if (known || nGoals >= 64) continue;
        goals[nGoals][0] = gx; goals[nGoals][1] = gy; ++nGoals;
        GroundRing(ringView, gx, gy, gz, 1.8f, rt, colour);
    }
    // What is selected: a ring on the ground under each unit, sized to its kind; the members of a picked squad wear a fainter one.
    for (unsigned i = 0; i < count; ++i) {
        const CeCmdViewUnitMark& m = marks[i];
        const CeCmdSquad* squad = ByOrdinal(m.squad);
        const bool unitPicked = gUnitUi.IsSelected(m.key), squadPicked = squad && squad->selected;
        if (!unitPicked && !squadPicked) continue;
        if (haveRingView && m.worldRadius > 0.f) GroundRing(ringView, m.wx, m.wy, m.wz, m.worldRadius, rt, unitPicked ? kRingPicked : kRingSquad);
        else if (unitPicked) {
            const float r = m.radius + 5.f*L.s;
            if (StrategicRectClearsHud(m.x - r, m.y - r, m.x + r, m.y + r))
                Outline(m.x-r, m.y-r, 2*r, 2*r, (std::max)(1.f,L.s), kPick);
        }
    }
    UnitPanel::Rect drag = {};
    if (gUnitUi.DragRect(&drag)) Outline(drag.x, drag.y, drag.w, drag.h, (std::max)(1.f,L.s), kPick);
}

int StrategicPick(const CeWorldViewSample& view, float px, float py) {
    int best = -1; float distance = 1e30f;
    for (int i = 0; i < gCount; ++i) {
        const CeCmdSquad& squad = gSquads[i];
        if (!squad.friendly) continue;
        CeViewPoint pixel;
        if (!StrategicPixel(view, squad.x, squad.y, &pixel) || !StrategicWorldArea(pixel.x, pixel.y)) continue;
        const float dx = pixel.x - px, dy = pixel.y - py, d = dx * dx + dy * dy;
        const float radius = 18.f * L.s;
        if (d <= radius * radius && d < distance) { best = squad.ordinal; distance = d; }
    }
    // Retain the generous target around a squad centre. A closer drawn member
    // wins where the player is pointing; selecting either always picks the squad.
    unsigned count;
    const CeCmdViewUnitMark *marks = CeCmdViewUnitMarks(count);
    for (unsigned i = 0; i < count; ++i) {
        const CeCmdViewUnitMark &mark = marks[i];
        const float dx = mark.x - px, dy = mark.y - py, d = dx * dx + dy * dy;
        const float radius = mark.radius + 3.f * L.s;
        if (d > radius * radius || (best >= 0 && d >= distance) || !StrategicWorldArea(mark.x, mark.y)) continue;
        const CeCmdSquad *squad = ByOrdinal(mark.squad);
        if (squad && squad->friendly) { best = mark.squad; distance = d; }
    }
    return best;
}

// The squads that take orders whose marks lie inside the dragged rectangle: the selection, or added to it with Shift.
struct StrategicBoxPoint { float x, y; };
void StrategicBoxMembers(const CeWorldViewSample& view, float x0, float y0, float x1, float y1, CeCmdSel& result, StrategicBoxPoint* hits = nullptr) {
    for (int i = 0; i < gCount; ++i) {
        const CeCmdSquad& squad = gSquads[i];
        CeViewPoint pixel;
        if (!squad.commandable || !StrategicPixel(view, squad.x, squad.y, &pixel) || !StrategicWorldArea(pixel.x, pixel.y)) continue;
        if (CeBoxHas(x0, y0, x1, y1, pixel.x, pixel.y) && CeSelAdd(result, squad.ordinal) && hits)
            hits[result.n-1] = {pixel.x, pixel.y};
    }
    unsigned count;
    const CeCmdViewUnitMark *marks = CeCmdViewUnitMarks(count);
    for (unsigned i = 0; i < count; ++i) {
        const CeCmdViewUnitMark &mark = marks[i];
        if (!CeBoxHas(x0, y0, x1, y1, mark.x, mark.y) || !StrategicWorldArea(mark.x, mark.y)) continue;
        const CeCmdSquad *squad = ByOrdinal(mark.squad);
        if (squad && squad->commandable && CeSelAdd(result, mark.squad) && hits)
            hits[result.n-1] = {mark.x, mark.y};
    }
}

void StrategicBox(const CeWorldViewSample& view, float x0, float y0, float x1, float y1, bool add) {
    if (!add) CeSelClear(gSel);
    StrategicBoxMembers(view, x0, y0, x1, y1, gSel);
    gSelected = CeSelLead(gSel);
    Refresh();
    CeCmdNote("map drag %d %d %d %d count %d", int(x0), int(y0), int(x1), int(y1), gSel.n);
    char text[64];
    if (gSel.n > 1) snprintf(text, sizeof(text), "%d squads picked.", gSel.n);
    else if (gSel.n == 0) snprintf(text, sizeof(text), "No squad of yours there.");
    else return;		// one squad: its card says so
    Note(text);
}

std::uint64_t gButtonPressedNs[4];		// when each order button was last clicked: it shows pressed for a moment (DrawStrategicHints)

void StrategicArm(int verb) {
    if (!ByOrdinal(gSelected)) { Note("Pick a squad first."); return; }
    gArmed = true; gStrategicClickOrder = verb;
    Note(verb == CE_CMD_ADVANCE ? "Advance: click the ground." : "Move: click the ground.");
}

void RtsHudResetState() {
    gHudLayout = CeCmdHudLayout{};
    gHudClick.Reset();
    gHudFollowSquad = -1;
    gStructFocusHandle = kCeEntityHandleInvalid;
    gActiveNoticeRectValid = false;
    gActiveHelpRectValid = false;
    for (int i = 0; i < kCeHudCardButtons; ++i) gHudBtnFlashNs[i] = 0;
}

bool RtsHudFindBattleEngineRally(float* outX, float* outY) {
    CBattleEngine* engine = GAME.GetPlayer(0) ? GAME.GetPlayer(0)->GetBattleEngine() : NULL;
    if (!engine) return false;
    const float ex = engine->GetPos().X, ey = engine->GetPos().Y;
    if (CeCmdMarchLand(ex, ey)) { *outX = ex; *outY = ey; return true; }
    static const float kDirs[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{0.707f,0.707f},{-0.707f,0.707f},{0.707f,-0.707f},{-0.707f,-0.707f}};
    for (float rad = 12.f; rad <= 36.f; rad += 12.f) {
        for (int d = 0; d < 8; ++d) {
            const float cx = ex + kDirs[d][0] * rad, cy = ey + kDirs[d][1] * rad;
            if (CeCmdMarchLand(cx, cy)) { *outX = cx; *outY = cy; return true; }
        }
    }
    return false;
}

void RtsHudOrderFollowBe() {
    float rx = 0.f, ry = 0.f;
    if (!RtsHudFindBattleEngineRally(&rx, &ry)) { Note("Fly near land so ground units can follow."); return; }
    if (gUnitUi.SelectionCount()) {
        if (gUnitUi.ArmAction(UnitPanel::Action::Move) == UnitControl::ArmStatus::Armed) {
            float rz = 0.f;
            if (!StrategicHeight(rx, ry, &rz)) rz = 0.f;
            UnitControl::Request req = {};
            if (gUnitUi.SubmitTarget(gUnitUi.TargetToken(), {rx, ry, rz}, &req) == UnitControl::TargetStatus::Submitted) {
                if (DispatchUnitRequest(req)) {
                    gOrderNs = Ns();
                    CeCmdVoicePlay(CE_VOICE_FOLLOW, gOrderNs);
                }
                return;
            }
            gUnitUi.CancelArmedTarget();
        }
        Note("Follow is unavailable for this selection.");
        return;
    }
    if (!ByOrdinal(gSelected)) { Note("Pick a squad first."); return; }
    const int prevFollow = gHudFollowSquad;
    gHudFollowSquad = gSelected;
    if (!Order(CE_CMD_ADVANCE, rx, ry, CE_VOICE_FOLLOW))
        gHudFollowSquad = prevFollow;
}

void RtsHudTakeControl() {
    CeCmdStructureView selStruct = {};
    if (CeCmdStructureSelected(&selStruct) && selStruct.isExercise && selStruct.stage == CE_FACTORY_READY) {
        const int ordinal = CeCmdDeliveredSquad();
        if (ordinal < 0) Note("Tank unavailable.");
        else if (!CePlayUnitCanTakeDelivery()) Note("Unit control is unavailable.");
        else { CeCmdStructureClearSelection(); SelectOnly(ordinal); Close(); CePlayUnitTakeDelivery(); }
        return;
    }
    if (gUnitUi.SelectionCount() == 1) {
        UnitAction(UnitPanel::Action::TakeControl);
        return;
    }
    CeCommandLifetimeKey key = {};
    if (CanTakeSelectedSquadUnit(&key)) {
        UnitTakeContext take = {};
        if (!CeCmdUnitVisit(key, UnitCanTake, &take)) {
            Note(take.reason ? take.reason : "Unit control is unavailable.");
            return;
        }
        Close();
        if (!PortCeCmdMapOpen() && CeCmdUnitVisit(key, UnitTake, &take)) return;
        Note(take.reason ? take.reason : "Unit control is unavailable.");
        return;
    }
    Note("Select an eligible friendly unit to take control.");
}

void RtsHudReturnAi() {
    gHudFollowSquad = -1;
    if (gUnitUi.SelectionCount()) {
        if (UnitAction(UnitPanel::Action::ReturnToAI))
            CeCmdVoicePlay(CE_VOICE_CANCEL, Ns());
        return;
    }
    if (!ByOrdinal(gSelected)) { Note("Pick a squad first."); return; }
    Order(CE_CMD_RELEASE, 0.f, 0.f);
}

void RtsHudExecuteCardAction(int btnIdx) {
    if (btnIdx < 0 || btnIdx >= kCeHudCardButtons) return;
    const CeCmdCardButton& btn = gHudCard.buttons[btnIdx];
    if (btn.action == CE_CARD_NONE) return;
    if (!btn.enabled) {
        if (gHudCard.isStructure) Note("Build order is currently unavailable.");
        else Note("Select a commandable unit or squad first.");
        return;
    }
    gHudBtnFlashNs[btnIdx] = Ns();
    CeCmdStructureView selStruct = {};
    if (btn.action >= CE_CARD_BUILD_0 && btn.action <= CE_CARD_BUILD_3) {
        const int opt = int(btn.action - CE_CARD_BUILD_0);
        if (CeCmdStructureSelected(&selStruct) && CeCmdStructureQueue(selStruct.thingNumber, opt)) {
            char unitShort[20];
            CeCmdHudShortName(selStruct.options[opt].unitName, false, false, unitShort, sizeof(unitShort));
            char msg[64];
            snprintf(msg, sizeof(msg), "Building %s.", unitShort);
            Note(msg);
            CeCmdVoicePlay(CE_VOICE_BUILD_QUEUE, Ns());
        } else {
            Note("Factory cannot start that build now.");
        }
        return;
    }
    if (btn.action == CE_CARD_CANCEL_BUILD) {
        if (CeCmdStructureSelected(&selStruct) && CeCmdStructureCancel(selStruct.thingNumber)) {
            Note("Build order cancelled.");
            CeCmdVoicePlay(CE_VOICE_CANCEL, Ns());
        }
        return;
    }
    if (btn.action == CE_CARD_SELECT_DELIVERED) {
        const int ordinal = CeCmdDeliveredSquad();
        const CeCmdSquad* tank = ordinal >= 0 ? ByOrdinal(ordinal) : NULL;
        if (tank) {
            CeCmdStructureClearSelection();
            SelectOnly(ordinal);
            StartStrategicFocus(*tank);
            Note("Your tank selected.");
            CeCmdVoicePlay(CE_VOICE_SELECT_UNIT, Ns());
        } else Note("Tank unavailable. No order issued.");
        return;
    }
    if (btn.action == CE_CARD_TAKE_DELIVERED) {
        const int ordinal = CeCmdDeliveredSquad();
        if (ordinal < 0) Note("Tank unavailable.");
        else if (!CePlayUnitCanTakeDelivery()) Note("Unit control is unavailable.");
        else { CeCmdStructureClearSelection(); SelectOnly(ordinal); Close(); CePlayUnitTakeDelivery(); }
        return;
    }
    if (btn.action == CE_CARD_MOVE) {
        if (gUnitUi.SelectionCount()) UnitAction(UnitPanel::Action::Move);
        else StrategicArm(CE_CMD_MOVE);
        return;
    }
    if (btn.action == CE_CARD_ATTACK) {
        if (gUnitUi.SelectionCount()) UnitAction(UnitPanel::Action::Advance);
        else StrategicArm(CE_CMD_ADVANCE);
        return;
    }
    if (btn.action == CE_CARD_HOLD) {
        gArmed = false;
        gHudFollowSquad = -1;
        if (gUnitUi.SelectionCount()) {
            if (UnitAction(UnitPanel::Action::Hold))
                CeCmdVoicePlay(CE_VOICE_HOLD, Ns());
        } else Order(CE_CMD_HOLD, 0.f, 0.f);
        return;
    }
    if (btn.action == CE_CARD_FOLLOW_BE) {
        gArmed = false;
        RtsHudOrderFollowBe();
        return;
    }
    if (btn.action == CE_CARD_TAKE_CONTROL) {
        gArmed = false;
        RtsHudTakeControl();
        return;
    }
    if (btn.action == CE_CARD_RETURN_AI) {
        gArmed = false;
        RtsHudReturnAi();
        return;
    }
}

void RtsHudFocusStructure(int thingNumber) {
    gStructFocusHandle = thingNumber > 0 ? static_cast<CeEntityHandle>(thingNumber) : kCeEntityHandleInvalid;
    CeCmdSquad focus = {};
    focus.ordinal = kStructureFocus;
    StartStrategicFocus(focus);
}

bool RtsHudPointerDown(float px, float py, bool shift) {
    BuildRtsHudModel(NULL, 0);
    const CeCmdHudHit hit = CeCmdHudHitTest(gHudLayout, px, py);
    if (hit.kind == CE_HUD_HIT_NONE) return false;
    if (hit.kind == CE_HUD_HIT_PREV_PAGE) {
        if (gHudPage > 0) --gHudPage;
        return true;
    }
    if (hit.kind == CE_HUD_HIT_NEXT_PAGE) {
        if (gHudPage + 1 < gHudLayout.totalPages) ++gHudPage;
        return true;
    }
    if (hit.kind == CE_HUD_HIT_CARD_BUTTON) {
        RtsHudExecuteCardAction(hit.index);
        return true;
    }
    if (hit.kind == CE_HUD_HIT_PLACARD && hit.index >= 0 && hit.index < gHudPlacardCount) {
        const CeCmdPlacard& pl = gHudPlacards[hit.index];
        const std::uint64_t now = Ns();
        const bool dbl = gHudClick.RegisterClick(pl.kind, pl.id, now);
        gArmed = false;
        if (pl.kind == CE_PLACARD_STRUCTURE) {
            gUnitUi.ClearSelection(); gUnitUi.Cancel();
            SelectOnly(-1);
            if (CeCmdStructureSelect(pl.id)) {
                CeCmdVoicePlay(CE_VOICE_SELECT_STRUCTURE, now);
                if (dbl) RtsHudFocusStructure(pl.id);
            }
            return true;
        }
        if (pl.kind == CE_PLACARD_SQUAD) {
            const CeCmdSquad* sq = ByOrdinal(pl.id);
            if (!sq) return true;
            if (!sq->commandable) {
                Note(WhyText(sq->why));
                if (dbl) StartStrategicFocus(*sq);
                return true;
            }
            gUnitUi.ClearSelection(); gUnitUi.Cancel();
            CeFactoryLeaveSelection();
            if (shift) SelectToggle(pl.id);
            else SelectOnly(pl.id);
            CeCmdVoicePlay(CE_VOICE_SELECT_UNIT, now);
            if (dbl) StartStrategicFocus(*sq);
            return true;
        }
        if (pl.kind == CE_PLACARD_UNIT && (pl.hasValidId || uint32_t(pl.id) != 0u)) {
            int foundIdx = -1;
            for (int i = 0; i < gUnitNativeCount; ++i) {
                if (uint32_t(gUnitNative[i].key.id) == uint32_t(pl.id)) { foundIdx = i; break; }
            }
            if (foundIdx >= 0) {
                if (dbl) {
                    gUnitFocusCount = 1;
                    gUnitFocusKeys[0] = gUnitNative[foundIdx].key;
                    CeCmdSquad focus = {};
                    focus.ordinal = kUnitFocus;
                    StartStrategicFocus(focus);
                } else {
                    CeCmdVoicePlay(CE_VOICE_SELECT_UNIT, now);
                }
            }
            return true;
        }
    }
    return true;
}

bool RtsHudPickStructureWorld(float px, float py) {
    CeWorldViewSample view;
    if (!StrategicViewRead(&view)) return false;
    CeCmdStructureView structs[kCeMaxStructures];
    const int count = CeCmdStructureList(structs, kCeMaxStructures);
    bool foundBest = false;
    int bestThing = 0;
    float bestDist = 1e30f;
    const float reach = 18.f * L.s;
    for (int i = 0; i < count; ++i) {
        CeViewPoint pt;
        if (!StrategicPixel(view, structs[i].x, structs[i].y, &pt) || !StrategicWorldArea(pt.x, pt.y)) continue;
        const float dx = pt.x - px, dy = pt.y - py, d = dx * dx + dy * dy;
        if (d <= reach * reach && d < bestDist) { bestDist = d; bestThing = structs[i].thingNumber; foundBest = true; }
    }
    if (foundBest && CeCmdStructureSelect(bestThing)) {
        gUnitUi.ClearSelection();
        SelectOnly(-1);
        CeCmdVoicePlay(CE_VOICE_SELECT_STRUCTURE, Ns());
        return true;
    }
    return false;
}

// The bottom RTS command card owns its clicks; no click on a button also reaches
// the battlefield. Button geometry is shared by drawing and hit testing.
int StrategicButtonAt(float x, float y) {
    const CeCmdHudLayout lay = CeCmdHudComputeLayout(L.w, L.h, L.s, StrategicUiBottom(), gHudPlacardCount, gHudPage);
    const CeCmdHudHit hit = CeCmdHudHitTest(lay, x, y);
    if (hit.kind == CE_HUD_HIT_CARD_BUTTON && hit.index >= 0 && hit.index < 4) return hit.index;
    return -1;
}

void UnitPointerSeed() {
    const int held[3] = {PortMouseHeldNow(0), PortMouseHeldNow(1), PortMouseHeldNow(2)};
    gUnitPointerFence.Begin(held);
    gUnitPointerAvailable = held[0] >= 0 && held[1] >= 0 && held[2] >= 0;
    if (gUnitPointerFence.Blocked(0)) gUnitUi.SuppressUntilRelease();
}
void UnitPointerObserve() {
    const int held[3] = {PortMouseHeldNow(0), PortMouseHeldNow(1), PortMouseHeldNow(2)};
    const CeCmdPointerObservation result = gUnitPointerFence.Observe(held);
    gUnitPointerAvailable = result.available;
    if (!result.available) {
        gUnitUi.Cancel(); gUnitUi.SuppressUntilRelease();
        gBoxing = false; gStrategicDragging = false; gStrategicWheel = 0;
    } else if (result.released & 1u) {
        // A positively observed release ends suppression even when focus loss
        // prevented the native trap from delivering its release message.
        gUnitUi.PointerUp(float(MOUSE_X), float(MOUSE_Y), UnitControl::ClickModifiers{});
    }
}

void StrategicPointer(WORD key, int event) {
    const float px = float(MOUSE_X), py = float(MOUSE_Y);
    if (key <= 2 && event == kMouseDown && !gUnitPointerFence.Press(key)) return;
    if (key <= 2 && event == kMouseUp && !gUnitPointerFence.Release(key)) {
        if (key == 0) { gUnitUi.Cancel(); gUnitUi.PointerUp(px, py, UnitControl::ClickModifiers{}); }
        if (key == 1) gStrategicDragging = false;
        return;
    }
    if (!StrategicUiReady() || !gUnitPointerAvailable) {
        gUnitUi.Cancel(); gBoxing = false; gStrategicDragging = false;
        gStrategicDragX = gStrategicDragY = gStrategicWheel = 0;
        return;
    }
    if (!StrategicCanAct()) { gUnitUi.Cancel(); return; }
    if (event == kMouseUp && key == 1) { gStrategicDragging = false; return; }
    if (event == kMouseUp && (key == 3 || key == 4)) {
        if (StrategicWorldArea(px, py)) gStrategicWheel += key == 3 ? 1 : -1;
        return;
    }
    const bool shift = gDown[DIK_LSHIFT] || gDown[DIK_RSHIFT];
    const bool ctrl = gDown[DIK_LCONTROL] || gDown[DIK_RCONTROL];
    const bool leftUp = event == kMouseUp && key == 0;
    if (!leftUp && (event != kMouseDown || key > 2)) return;
    Refresh(); RefreshUnitFrame();
    const int factoryButton = gDown[DIK_F1] ? -1 : FactoryCardAt(px, py);
    if (key == 0 && factoryButton >= 0 && event == kMouseDown) {
        if (event == kMouseDown) {
            gUnitUi.ClearSelection(); gUnitUi.Cancel();
            if (factoryButton == 1 && CeFactorySelect()) {
                SelectOnly(-1);
                CeCmdSquad focus = {}; focus.ordinal=kFactoryFocus; StartStrategicFocus(focus);
            } else if (factoryButton == 2) CeFactoryQueue();
            else if (factoryButton == 3) CeFactoryCancel();
            else if (factoryButton == 4) CeFactoryLeaveSelection();
            else if (factoryButton == 5) {
                Refresh();
                const int ordinal = CeCmdDeliveredSquad();
                const CeCmdSquad *tank = ordinal >= 0 ? ByOrdinal(ordinal) : nullptr;
                if (tank) {
                    SelectOnly(ordinal);
                    StartStrategicFocus(*tank);
                    Note("Your tank selected.");
                } else Note("Tank unavailable. No order issued.");
            } else if (factoryButton == 6) {
                const int ordinal = CeCmdDeliveredSquad();
                if (ordinal < 0) Note("Tank unavailable.");
                else if (gSel.n != 1 || gSelected != ordinal) Note("Select the tank first.");
                else if (!CePlayUnitCanTakeDelivery()) Note("Unit control is unavailable.");
                else { Close(); CePlayUnitTakeDelivery(); }
            }
        }
        return;
    }
    if (leftUp) {
        if (!StrategicWorldArea(px, py) && !UnitPanelAt(px, py)) {
            const float invalid = (std::numeric_limits<float>::quiet_NaN)();
            gUnitUi.PointerUp(invalid, invalid, {shift, ctrl});
            return;
        }
        UnitPanel::Action armedAct = UnitPanel::Action::Move;
        const bool hadArmedAct = gUnitUi.ArmedAction(&armedAct);
        const UnitControl::GestureResult gesture = gUnitUi.PointerUp(px, py, {shift, ctrl});
        if (gesture.outcome == UnitControl::GestureOutcome::TargetPoint) {
            if (UnitTargetAt(gesture.x, gesture.y, gesture.target_token))
                CeCmdVoicePlay((hadArmedAct && armedAct == UnitPanel::Action::Advance) ? CE_VOICE_ATTACK : CE_VOICE_MOVE, Ns());
        }
        else if (gesture.outcome == UnitControl::GestureOutcome::Click || gesture.outcome == UnitControl::GestureOutcome::Box) {
            SelectOnly(-1); gArmed = false; CeFactoryLeaveSelection();
            if (gesture.selection.picked || gUnitUi.SelectionCount() > 0)
                CeCmdVoicePlay(CE_VOICE_SELECT_UNIT, Ns());
            if (gesture.outcome == UnitControl::GestureOutcome::Click && !gesture.selection.picked && !shift) {
                CeWorldViewSample view; CeFactoryView factory; CeViewPoint point;
                if (StrategicViewRead(&view) && CeFactoryRead(factory) && StrategicPixel(view, factory.x, factory.y, &point) &&
                    std::fabs(point.x-px) <= 14*L.s && std::fabs(point.y-py) <= 14*L.s) {
                    gUnitUi.ClearSelection(); CeFactorySelect();
                    CeCmdVoicePlay(CE_VOICE_SELECT_STRUCTURE, Ns());
                } else {
                    RtsHudPickStructureWorld(px, py);
                }
            }
            RefreshUnitFrame();
        }
        DrainUnitRequest();
        return;
    }
    if (key == 0 && RtsHudPointerDown(px, py, shift)) return;
    if (key == 0 && UnitPanelAt(px, py)) { gUnitUi.PointerDown(px, py); return; }
    const int button = key == 0 && !gUnitUi.SelectionCount() ? StrategicButtonAt(px, py) : -1;
    if (button >= 0) {
        gButtonPressedNs[button] = Ns();
        if (button < 2) StrategicArm(button == 0 ? CE_CMD_MOVE : CE_CMD_ADVANCE);
        else { gArmed = false; Order(button == 2 ? CE_CMD_HOLD : CE_CMD_FALLBACK, 0, 0); }
        return;
    }
    if (!StrategicWorldArea(px, py)) return;
    if (key == 1) { gStrategicDragging = true; gStrategicDragX = px; gStrategicDragY = py; return; }
    if (key == 0 && !gArmed) { gUnitUi.PointerDown(px, py); return; }
    if (key == 2 && gUnitUi.SelectionCount()) {
        if (gUnitUi.ArmAction(UnitPanel::Action::Move) == UnitControl::ArmStatus::Armed) {
            if (UnitTargetAt(px, py, gUnitUi.TargetToken()))
                CeCmdVoicePlay(CE_VOICE_MOVE, Ns());
            else
                gUnitUi.CancelArmedTarget();
        } else {
            bool anyArtillery = false, anyNonArtillery = false;
            for (int i = 0; i < gUnitNativeCount; ++i) {
                if (!gUnitUi.IsSelected(gUnitNative[i].key)) continue;
                const CeCmdSquad* usq = ByOrdinal(gUnitNative[i].ordinal);
                const bool deploys = (usq && usq->deploys) || strstr(gUnitNative[i].name, "Artillery") != NULL;
                if (deploys) anyArtillery = true;
                else anyNonArtillery = true;
            }
            Note(anyArtillery && !anyNonArtillery ? "Artillery can only hold." : "Move is unavailable for this selection.");
        }
        return;
    }
    CeWorldViewSample view; CeViewPoint target;
    if (!StrategicViewRead(&view) || !StrategicGround(view, px, py, &target)) { NoteHere("Point at the ground.", px, py); return; }
    const int verb = key == 0 && gArmed ? gStrategicClickOrder : (ctrl ? CE_CMD_MOVE : CE_CMD_ADVANCE);
    gArmed = false; Order(verb, target.x, target.y);
}


void AdvanceCameraLifetime(std::uint64_t& lifetime) {
    if (lifetime != 0) ++lifetime; // Overflow stays refused; never reuse an identity.
}

bool SameCameraBinding(const CeCommanderCameraBinding& a, const CeCommanderCameraBinding& b) {
    return a.mission == b.mission && a.pilot == b.pilot && a.camera == b.camera;
}

CeCommanderCameraContext StrategicContext() {
    CeCommanderCameraContext c;
    c.binding.mission = gMissionLifetime; c.binding.pilot = gPilotLifetime;
    c.binding.camera = gCameraLifetime;
    // Refuse fenced/teardown paths before reading a player, actor or camera.
    if (!Allowed() || GAME.IsPaused()) return c;
    CPlayer* player = GAME.GetPlayer(0);
    CBattleEngine* pilot = player ? player->GetBattleEngine() : NULL;
    const CeEntityHandle pilotHandle = CeEntityHandleLookup(pilot);
    CCamera* camera = GAME.GetCurrentCamera(0);
    // Track pilot identity via monotonic 32-bit CE entity handles so reused storage
    // and 16-bit CThing::sThingCount wrap-around are always distinguished cleanly.
    if (player != gObservedPlayer || pilot != gObservedPilot || pilotHandle != gObservedPilotHandle) {
        AdvanceCameraLifetime(gPilotLifetime); gObservedPlayer = player; gObservedPilot = pilot;
        gObservedPilotHandle = pilotHandle;
    }
    if (camera != gObservedCamera) {
        AdvanceCameraLifetime(gCameraLifetime); gObservedCamera = camera;
    }
    c.binding.mission = gMissionLifetime; c.binding.pilot = gPilotLifetime;
    c.binding.camera = gCameraLifetime;
    c.admitted_ordinary_solo = true;
    c.live_pilot = pilot && pilot->GetLife() > 0;
    c.camera_available = camera != NULL;
    c.competing_camera_owner = LT.GetKeytrap() != (pKeyTrapper)Trap;
    return c;
}

class StrategicCameraConsumer : public CeCommanderCameraConsumer {
public:
    bool Capture(const CeCommanderCameraBinding& binding) noexcept override {
        CeCommanderCameraContext c = StrategicContext();
        if (!c.admitted_ordinary_solo || !c.live_pilot || !c.camera_available ||
            c.competing_camera_owner || !SameCameraBinding(c.binding, binding)) return false;
        gFocusTrip.squad = -1;
        AdvanceCameraLifetime(gStrategicViewSession);
        if (!gStrategicViewSession) return false;
        StrategicViewInvalidate();
        gStrategicDragging = false; gStrategicWheel = 0; gBoxing = false;
        gStrategicBinding = binding;
        gStrategicOriginal = GAME.GetCurrentCamera(0);
        gStrategicCamera.mZoom = gStrategicOriginal->GetZoom();
        if (gHasStrategicInitialYaw) {
            gStrategicYaw = gStrategicInitialYaw;
            gHasStrategicInitialYaw = false;
        } else if (gHasKeptYaw) {
            gStrategicYaw = gKeptYaw;
        } else {
            gStrategicYaw = gStrategicOriginal->GetYaw();
        }
        gStrategicPitch = kLivePitch;
        gStrategicFocusZ = GAME.GetPlayer(0)->GetBattleEngine()->GetPos().Z;
        float terrainHeight = 0.0f;
        if (StrategicHeight(gViewX + gViewSpan * 0.5f,
                            gViewY + gViewSpan * 0.5f, &terrainHeight))
            gStrategicFocusZ = terrainHeight;
        StrategicEntered();
        return true;
    }
    bool BeginStrategicView(const CeCommanderCameraBinding& binding) noexcept override {
        gStrategicRender = true;
        return UpdateStrategicView(binding, CeCommanderCameraInput());
    }
    bool UpdateStrategicView(const CeCommanderCameraBinding& binding,
                             const CeCommanderCameraInput& input) noexcept override {
        CeCommanderCameraContext c = StrategicContext();
        if (!c.admitted_ordinary_solo || !c.live_pilot || c.competing_camera_owner ||
            !SameCameraBinding(c.binding, binding) || !gStrategicOriginal) return false;
    const float dt = static_cast<float>((std::min)(input.elapsed_seconds, 0.1));
        gStrategicYaw += static_cast<float>(input.orbit_horizontal) * dt;
    gStrategicPitch = (std::max)(0.35f, (std::min)(1.5f,
            gStrategicPitch + static_cast<float>(input.orbit_vertical) * dt));
        FMatrix flat; flat.MakeRotationYawF(gStrategicYaw);
        FVector step = flat * FVector(static_cast<float>(input.pan_horizontal),
                                      static_cast<float>(input.pan_vertical), 0);
        const float speed = gViewSpan * dt;
        // The map stores its lower-left corner; SetView accepts its centre.
        float centreX = gViewX + gViewSpan * 0.5f;
        float centreY = gViewY + gViewSpan * 0.5f;
        StepStrategicFocus(input, centreX, centreY);
        float span = gViewSpan * std::exp(-static_cast<float>(input.zoom) * dt);
        if (gZoomTrip.on) {
            if (input.zoom != 0.0) gZoomTrip.on = false;     // the player's own zoom takes over
            else {
                const float phase = static_cast<float>((std::min)(1.0, SecondsBetween(gZoomTrip.start, Ns()) / 0.4));
                const float ease = phase * phase * (3.f - 2.f * phase);
                span = gZoomTrip.from * powf(gZoomTrip.to / gZoomTrip.from, ease);
                if (phase >= 1.f) gZoomTrip.on = false;
            }
        }
        SetView(centreX + step.X * speed, centreY + step.Y * speed, span);
        // The point looked at rides on the ground under the centre, eased over about a fifth of a second, so a pan across a ridge or down to the
        // shore keeps the same distance to the ground (a camera held at the height it opened at went into the hills when close) and never jumps.
        // A focus trip sets the height itself.
        float ground;
        if (gFocusTrip.squad < 0 && dt > 0.f && StrategicHeight(gViewX + gViewSpan * 0.5f, gViewY + gViewSpan * 0.5f, &ground)) {
            ground = (std::min)(ground, MAP.GetWaterLevel());     // over the sea the point looked at is the water, not the sea bed (Z grows downward)
            gStrategicFocusZ += (ground - gStrategicFocusZ) * (std::min)(1.f, dt * 5.f);
        }
        gStrategicCamera.mOrientation.MakeRotationF(gStrategicYaw, gStrategicPitch, 0);
        const FVector forward = gStrategicCamera.mOrientation * FVector(0, 1, 0);
        gStrategicCamera.mPos = FVector(gViewX + gViewSpan * 0.5f,
                                      gViewY + gViewSpan * 0.5f, gStrategicFocusZ) -
                                   forward * (gViewSpan * 0.8f);
        return true;
    }
    bool Restore(const CeCommanderCameraBinding& binding) noexcept override {
        const CeCommanderCameraContext c = StrategicContext();
        if (!c.admitted_ordinary_solo || !c.live_pilot || !c.camera_available ||
            c.competing_camera_owner || !SameCameraBinding(c.binding, binding)) return false;
        Abandon(binding); // Nothing of the game's was changed: there is nothing to put back.
        return true;
    }
    void Abandon(const CeCommanderCameraBinding&) noexcept override {
        gFocusTrip.squad = -1;
        StrategicViewInvalidate();
        gStrategicDragging = false; gStrategicWheel = 0; gBoxing = false;
        gStrategicRender = false; gStrategicOriginal = NULL;
        StrategicLeft();
    }
};
StrategicCameraConsumer gStrategicConsumer;
CeCommanderCameraOwner gStrategicOwner(gStrategicConsumer);

// A key that moves the camera, as it is held now. The key trap's record (gDown) is made by the game's message pump, which runs once for every update (20 a
// second), so a key's press and release reach it up to 50 ms after they happened, and the camera ran on for that long after the key was let go. The
// platform's own record of the keyboard is refreshed with every frame (PortKeyHeldNow): the camera reads that where the platform can say, and the trap's
// record where it cannot. BEA_PORT_COMMANDER_VIEW=ticks or =messages reads the trap's record, to compare the two.
bool gStrategicMessageKeys = false;
bool CameraKeyDown(WORD key) {
    if (!gStrategicMessageKeys) {
        const int held = PortKeyHeldNow(key);
        if (held >= 0) return held != 0;
    }
    return gDown[key];
}

// The camera's input for the time since it last moved: the keys that are down, the wheel and the grabbed pointer, scaled by that time.
CeCommanderCameraInput StrategicInput() {
    const std::uint64_t now = Ns();
    CeCommanderCameraInput input;
    input.elapsed_seconds = SecondsBetween(gStrategicLastNs, now);
    gStrategicLastNs = now;
    input.pan_horizontal = double((CameraKeyDown(DIK_RIGHT) || CameraKeyDown(DIK_D))) - double((CameraKeyDown(DIK_LEFT) || (CameraKeyDown(DIK_A) && !CameraKeyDown(DIK_LCONTROL) && !CameraKeyDown(DIK_RCONTROL))));
    input.pan_vertical = double(CameraKeyDown(DIK_UP) || CameraKeyDown(DIK_W)) - double(CameraKeyDown(DIK_DOWN) || CameraKeyDown(DIK_S));
    input.orbit_horizontal = double(CameraKeyDown(DIK_E)) - double(CameraKeyDown(DIK_Q));
    input.orbit_vertical = double(CameraKeyDown(DIK_NEXT)) - double(CameraKeyDown(DIK_PRIOR));
    input.zoom = double(CameraKeyDown(DIK_R)) - double(CameraKeyDown(DIK_F));
    // Shift+A/E/F issue RTS orders (Attack / Take Control / Follow BE) and must not move the camera.
    if (CameraKeyDown(DIK_LSHIFT) || CameraKeyDown(DIK_RSHIFT)) {
        if (CameraKeyDown(DIK_A) && !CameraKeyDown(DIK_LEFT) && !CameraKeyDown(DIK_RIGHT) && !CameraKeyDown(DIK_D))
            input.pan_horizontal = 0.0;
        if (CameraKeyDown(DIK_E) && !CameraKeyDown(DIK_Q))
            input.orbit_horizontal = 0.0;
        if (CameraKeyDown(DIK_F) && !CameraKeyDown(DIK_R))
            input.zoom = 0.0;
    }
    const double bounded = (std::min)(input.elapsed_seconds, .1);
    if (bounded > 0) {
        input.zoom += double(gStrategicWheel) * std::log(1.25) / bounded;
        gStrategicWheel = 0;
        if (gStrategicDragging && L.h > 0) {
            // A grabbed view follows the pointer. Pixel deltas do not acquire a
            // second frame-time multiplier; keyboard motion remains timed.
            input.pan_horizontal += (gStrategicDragX - float(MOUSE_X)) / L.h / bounded;
            input.pan_vertical += (float(MOUSE_Y) - gStrategicDragY) / L.h / bounded;
            gStrategicDragX = float(MOUSE_X); gStrategicDragY = float(MOUSE_Y);
        }
    }
    return input;
}

void StrategicFrame() {
    const CeCommanderCameraResult result = gStrategicOwner.Update(StrategicContext(), StrategicInput());
    if (result == CeCommanderCameraResult::ContextLost || result == CeCommanderCameraResult::ConsumerFailed)
        Close();
    else if (result == CeCommanderCameraResult::Updated)
        Refresh(); // The battle runs here: a displayed squad may have died or changed allegiance.
}

// Once for each rendered frame, as the frame's picture is drawn (StrategicFrameCamera): the camera moves by the time since it last moved, whichever
// call that was. StrategicFrame runs once for each game update, and on its own it moved the camera in steps of one update, 20 a second: the frame-feel
// log showed the world view change on 37 of the 40 frames that held an update and on none of the others, at 60 and at 144 frames a second alike. A
// lost context or a failed update is left to the next update, which closes the view. BEA_PORT_COMMANDER_VIEW=ticks (read by PortCeCmdRender)
// gives the update-stepped camera back, to compare the two.
bool gStrategicTickOnly = false;
unsigned gStrategicAdvancedAt = ~0u;
void StrategicAdvance() {
    const unsigned frame = PortPresentCount();
    if (gStrategicTickOnly || frame == gStrategicAdvancedAt) return;		// a frame's other views (a reflection) keep the camera the first one used
    gStrategicAdvancedAt = frame;
    gStrategicOwner.Update(StrategicContext(), StrategicInput());
}

bool StrategicCanAct() {
    const CeCommanderCameraContext c = StrategicContext();
    if (!gOpen || !gStrategic || !gStrategicOwner.Active() ||
        !c.admitted_ordinary_solo || !c.live_pilot || !c.camera_available ||
        c.competing_camera_owner || !SameCameraBinding(c.binding, gStrategicBinding)) {
        Close(); return false;
    }
    return true;
}

// Recheck the live context and squad identities at the key event, not merely at
// the previous rendered frame. The order layer remains the authority for Hold.
void StrategicSquadAction(WORD key) {
    if (!StrategicUiReady()) {
        Note("Commander view is not ready");
        return;
    }

    const CeCommanderCameraContext c = StrategicContext();
    if (!gOpen || !gStrategic || !gStrategicOwner.Active() ||
        !c.admitted_ordinary_solo || !c.live_pilot || !c.camera_available ||
        c.competing_camera_owner || !SameCameraBinding(c.binding, gStrategicBinding)) {
        Close(); return;
    }
    Refresh();
    const bool shift = gDown[DIK_LSHIFT] || gDown[DIK_RSHIFT], ctrl = gDown[DIK_LCONTROL] || gDown[DIK_RCONTROL];
    RefreshUnitFrame();
    if (key == DIK_A && ctrl) {
        gArmed = false; SelectOnly(-1); CeFactoryLeaveSelection();
        const auto selected = gUnitUi.SelectAll();
        RefreshUnitFrame();
        Note(selected.result.rejected_capacity ? "Selection limit reached. Select a smaller group for more units." : "Units selected.");
        return;
    }
    if (key >= DIK_1 && key <= DIK_0) {
        const int number = (key == DIK_0) ? 0 : int(key - DIK_1) + 1;
        const int slot = (key == DIK_0) ? 9 : int(key - DIK_1);
        if (ctrl && gUnitUi.SelectionCount()) {
            if (gUnitUi.SaveSet(slot))
                gUnitGroupSaved[slot] = true;
            CeCmdSel empty = {};
            CeGroupStore(gGroups, number, empty);
            Note("Unit selection saved."); return;
        }
        if (!ctrl && (gUnitUi.SetCount(slot) || gUnitGroupSaved[slot])) {
            if (gUnitUi.SetLiveCount(slot) == 0) {
                Note("Group has no units left.");
                return;
            }
            gUnitUi.RecallSet(slot, shift); SelectOnly(-1); CeFactoryLeaveSelection();
            RefreshUnitFrame(); return;
        }
        if (!ctrl && gUnitUi.SelectionCount()) {
            CeCmdSel squadGroup;
            if (CeGroupRecall(gGroups, number, squadGroup)) {
                gUnitUi.ClearSelection(); gUnitUi.Cancel(); CeFactoryLeaveSelection();
                gArmed = false;
                GroupKey(number, false, shift);
                RefreshUnitFrame();
                return;
            }
            GroupKey(number, false, false);
            return;
        }
    }
    if (key == DIK_TAB) { gUnitUi.ClearSelection(); gUnitUi.Cancel(); CeFactoryLeaveSelection(); }
    else if (gUnitUi.SelectionCount()) {
        gArmed = false;
        if (key == DIK_C) UnitAction(UnitPanel::Action::Focus);
        else if (key == DIK_H || key == DIK_X) UnitAction(UnitPanel::Action::Hold);
        else if (key == DIK_B) UnitAction(UnitPanel::Action::FallBack);
        else if (key == DIK_DELETE) UnitAction(UnitPanel::Action::ReturnToAI);
        return;
    }
    if (key == DIK_TAB) {
        Cycle(shift ? -1 : 1);
        if (gSelected < 0) Note("There are no squads here to command.");
        else if (const CeCmdSquad *s = ByOrdinal(gSelected)) {
            // A squad picked off the picture is brought into it with the eased move C makes (the 2D map's Reveal moves a view this camera does not use).
            CeWorldViewSample view; CeViewPoint pixel;
            if (!StrategicViewRead(&view) || !StrategicPixel(view, s->x, s->y, &pixel) || !StrategicWorldArea(pixel.x, pixel.y)) StartStrategicFocus(*s);
        }
    } else if (key == DIK_C) {
        if (const CeCmdSquad *s = ByOrdinal(gSelected)) StartStrategicFocus(*s);
        else Note("Pick a squad first.");
    } else if (key == DIK_A && ctrl) {
        gArmed = false; SelectAll();
    } else if (key == DIK_A) {
        StrategicArm(CE_CMD_ADVANCE);
    } else if (key == DIK_H || key == DIK_X) {
        gArmed = false; Order(CE_CMD_HOLD, 0.f, 0.f);
    } else if (key == DIK_B) {
        gArmed = false; Order(CE_CMD_FALLBACK, 0.f, 0.f);
    } else if (key == DIK_DELETE) {
        gArmed = false; Order(CE_CMD_RELEASE, 0.f, 0.f);
    } else if (key >= DIK_1 && key <= DIK_0) {
        gArmed = false; GroupKey(key == DIK_0 ? 0 : int(key - DIK_1) + 1, ctrl, shift && !ctrl);
    }
}

// V: from the paused map to the live view (strategic) or back. The trap's record of the keys that are down goes across the switch.
// Open() alone would build a new record from the game's key state, which a key trap stops updating, and would mark M held, as it is
// for the press that opens the map from the battle: the first M after a V would then do nothing, and a key held across the V would
// repeat as a press.
void ResetStrategicGesture() {
    gUnitUi.Cancel(); gUnitPointerFence.Cancel(); gUnitPointerAvailable = false;
    if (++gUnitContextGeneration == 0) ++gUnitContextGeneration;
    gUnitFocusCount = 0;
    gStrategicUi.valid = false;
    CeHudReservationReset();
    gBoxing = false;
    gBoxX = gBoxY = 0;
    gStrategicDragging = false;
    gStrategicDragX = gStrategicDragY = 0;
    gStrategicWheel = 0;
    gArmed = false;
    gStrategicClickOrder = CE_CMD_MOVE;
    RtsHudResetState();
}

void SwitchView(bool strategic) {
    bool held[sizeof(gDown)];
    memcpy(held, gDown, sizeof(gDown));
    Close();
    if (!gOpen) Open(strategic);
    memcpy(gDown, held, sizeof(gDown));
    gShift = gDown[DIK_LSHIFT] || gDown[DIK_RSHIFT];
}

bool StrategicTrap(WORD key, int event) {
    if (event == kMouseDown || event == kMouseUp) { StrategicPointer(key, event); return true; }
    if (key < sizeof(gDown) / sizeof(gDown[0])) {
        if (event == KEY_EVENT_UP) { gDown[key] = false; return true; }
        if (event == KEY_EVENT_DOWN) {
            const bool repeat = gDown[key]; gDown[key] = true;
            if (!repeat && key == DIK_ESCAPE && (gArmed || gUnitUi.HasArmedTarget())) {
                // an order that waits for its click is called off first, as on the paused map; the next Esc goes back to the cockpit
                UnitPanel::Action unitAction = UnitPanel::Action::Move;
                const bool unitArmed = gUnitUi.ArmedAction(&unitAction);
                const bool isAdvance = unitArmed ? (unitAction == UnitPanel::Action::Advance) : (gStrategicClickOrder == CE_CMD_ADVANCE);
                gArmed = false; gUnitUi.CancelArmedTarget();
                Note(isAdvance ? "Advance cancelled." : "Move cancelled.");
            } else if (!repeat && (key == DIK_ESCAPE || key == gCloseKey)) {
                if (key == DIK_ESCAPE) gEscClosed = true;
                gPlayerClosed = true;
                Close();
            } else if (!repeat && key == DIK_Z) {
                const bool wide = gViewSpan > (kLiveOpenSpan + kLiveWideSpan) * 0.5f;
                if (!wide) gZoomBack = gViewSpan;
                gZoomTrip.from = gViewSpan; gZoomTrip.to = wide ? gZoomBack : kLiveWideSpan; gZoomTrip.start = Ns(); gZoomTrip.on = true;
            } else if (!repeat && key == DIK_V && TacticalMapAllowed()) {
                SwitchView(false);
            } else if (!repeat && (key == DIK_TAB || key == DIK_C || key == DIK_H || key == DIK_X || key == DIK_A || key == DIK_B || key == DIK_DELETE ||
                                   (key >= DIK_1 && key <= DIK_0))) {
    // Ctrl+A remains select-all; plain A/D belong only to camera pan.
    if (key == DIK_A && (gDown[DIK_LCONTROL] || gDown[DIK_RCONTROL])) {
        StrategicSquadAction(key);
        return true;
    }
    if (key == DIK_A && (gDown[DIK_LSHIFT] || gDown[DIK_RSHIFT])) {
        if (StrategicCanAct() && StrategicUiReady()) {
            Refresh(); RefreshUnitFrame();
            if (gUnitUi.SelectionCount()) UnitAction(UnitPanel::Action::Advance);
            else StrategicArm(CE_CMD_ADVANCE);
        }
        return true;
    }
    if (key == DIK_X && (gDown[DIK_LSHIFT] || gDown[DIK_RSHIFT])) {
        if (StrategicCanAct() && StrategicUiReady()) { Refresh(); RefreshUnitFrame(); RtsHudReturnAi(); }
        return true;
    }
    if (key == DIK_A || key == DIK_D) return true;
                StrategicSquadAction(key);
            } else if (!repeat && (key == DIK_G || key == DIK_SPACE || key == DIK_RETURN ||
                                   ((gDown[DIK_LSHIFT] || gDown[DIK_RSHIFT]) && (key == DIK_F || key == DIK_E)) ||
                                   (key == DIK_U && !(gDown[DIK_LSHIFT] || gDown[DIK_RSHIFT])))) {
                if (StrategicCanAct() && StrategicUiReady()) {
                    Refresh(); RefreshUnitFrame();
                    if (key == DIK_G) {
                        if (gUnitUi.SelectionCount()) UnitAction(UnitPanel::Action::Advance);
                        else StrategicArm(CE_CMD_ADVANCE);
                    } else if (key == DIK_SPACE || key == DIK_F) {
                        gArmed = false; RtsHudOrderFollowBe();
                    } else if (key == DIK_RETURN || key == DIK_E) {
                        gArmed = false; RtsHudTakeControl();
                    } else if (key == DIK_U) {
                        CeCmdStructureView selStruct = {};
                        if (CeCmdStructureSelected(&selStruct) && selStruct.optionCount > 0) {
                            BuildRtsHudModel(NULL, 0);
                            RtsHudExecuteCardAction(0);
                        }
                    }
                }
            }
        }
    }
    return true;
}

// The camera this frame's picture is drawn from, moved by the time since it last moved; NULL when the view is not live. It is read by the
// Commander's own drawing only: nothing hands it to the engine.
const CGenericCamera* StrategicFrameCamera() {
    if (!gOpen || !gStrategic || !gStrategicRender || !gStrategicOwner.Active()) return NULL;
    const CeCommanderCameraContext c = StrategicContext();
    if (!c.admitted_ordinary_solo || !c.live_pilot || !c.camera_available ||
        c.competing_camera_owner || !SameCameraBinding(c.binding, gStrategicBinding)) {
        gStrategicOwner.Reset(); return NULL;
    }
    StrategicAdvance();
    return &gStrategicCamera;
}

CeCommanderPresentedPose NativeCameraPose(CCamera* camera)
{
    CeCommanderPresentedPose pose = {};
    if (!camera) return pose;
    const FVector position = camera->GetPos();
    const FMatrix orientation = camera->GetOrientation();
    pose.pos[0] = position.X; pose.pos[1] = position.Y; pose.pos[2] = position.Z;
    for (int row = 0; row < 3; ++row) {
        pose.rows[row*3] = orientation.Row[row].X;
        pose.rows[row*3+1] = orientation.Row[row].Y;
        pose.rows[row*3+2] = orientation.Row[row].Z;
    }
    pose.zoom = camera->GetZoom();
    return pose;
}

void NativePresentPose(const CeCommanderPresentedPose& pose)
{
    gStrategicPresentedCamera.mPos = FVector(pose.pos[0], pose.pos[1], pose.pos[2]);
    for (int row = 0; row < 3; ++row)
        gStrategicPresentedCamera.mOrientation.Row[row] =
            FVector(pose.rows[row*3], pose.rows[row*3+1], pose.rows[row*3+2]);
    gStrategicPresentedCamera.mZoom = pose.zoom;
    gNativeLastPose = pose;
}

void CancelNativeReturn()
{
    gNativeReturning = false;
    gNativeReturnOriginal = NULL;
}

bool NativeReturnEligible()
{
    if (!gNativeReturning) return false;
    const CeCommanderCameraContext context = StrategicContext();
    if (!context.admitted_ordinary_solo || !context.live_pilot ||
        !context.camera_available || LT.GetKeytrap() != NULL ||
        CePlayUnitOwnsPilot() || !SameCameraBinding(context.binding, gNativeReturnBinding) ||
        GAME.GetCurrentCamera(0) != gNativeReturnOriginal) {
        CancelNativeReturn();
        return false;
    }
    return true;
}

void PrepareNativeEntry()
{
    gNativeEntrySeeded = NativeReturnEligible();
    if (gNativeEntrySeeded) gNativeEntryFrom = gNativeLastPose;
    CancelNativeReturn();
    gNativeEntering = false;
    gNativePresented = false;
}

void BeginNativeReturn()
{
    if (!gOpen || !gStrategic || !gStrategicOwner.Active() || !gNativePresented) return;
    const CeCommanderCameraContext context = StrategicContext();
    if (!context.admitted_ordinary_solo || !context.live_pilot ||
        !context.camera_available || context.competing_camera_owner || CePlayUnitOwnsPilot() ||
        !SameCameraBinding(context.binding, gStrategicBinding)) return;
    gNativeReturnFrom = gNativeLastPose;
    gNativeReturnBinding = gStrategicBinding;
    gNativeReturnOriginal = gStrategicOriginal;
    gNativeReturnStartNs = Ns();
    gNativeEntering = false;
    gNativeReturning = true;
}

CCamera* NativeStrategicPresentation(CCamera* original)
{
    const std::uint64_t now = Ns();
    if (!gNativePresented) {
        if (!gNativeEntrySeeded) gNativeEntryFrom = NativeCameraPose(original);
        gNativeEntryStartNs = now;
        gNativeEntering = true;
    }
    const CeCommanderPresentedPose target = NativeCameraPose(&gStrategicCamera);
    CeCommanderPresentedPose shown = target;
    const double weight = gNativeEntering ?
        CeCommanderCameraEase(SecondsBetween(gNativeEntryStartNs, now), 0.35) : 1.0;
    if (!CeCommanderBlendPresentedPose(gNativeEntryFrom, target, weight, &shown)) {
        // An unavailable transition must not present the pilot under world controls.
        if (!ce_commander_transition_detail::Valid(target)) {
            gStrategicOwner.Reset();
            gNativePresented = false;
            return original;
        }
        shown = target;
        gNativeEntering = false;
    }
    NativePresentPose(shown);
    gNativePresented = true;
    if (weight >= 1.0) gNativeEntering = false;
    return &gStrategicPresentedCamera;
}

CCamera* NativeReturnPresentation(CCamera* original, CPlayer* player)
{
    if (!NativeReturnEligible() || original != gNativeReturnOriginal ||
        player != GAME.GetPlayer(0)) return NULL;
    const double weight = CeCommanderCameraEase(SecondsBetween(gNativeReturnStartNs, Ns()), 0.35);
    if (weight >= 1.0) { CancelNativeReturn(); return NULL; }
    const CeCommanderPresentedPose target = NativeCameraPose(original);
    CeCommanderPresentedPose shown = target;
    if (!CeCommanderBlendPresentedPose(gNativeReturnFrom, target, weight, &shown)) {
        CancelNativeReturn();
        return NULL;
    }
    NativePresentPose(shown);
    return &gStrategicPresentedCamera;
}


// A colour with its alpha scaled (0 to 1).
// The game's x87 unit may round a conversion to the nearest integer rather than cut it: 255.5 becomes 256, which shifted into the top byte is 0, a
// colour with no alpha at all (a note's text and edge vanished at full strength, frame by frame). So the result is held to 255.
DWORD Faded(DWORD colour, float alpha) {
    alpha = alpha < 0.f ? 0.f : (alpha > 1.f ? 1.f : alpha);
    DWORD a = DWORD(float(colour >> 24) * alpha + 0.5f);
    if (a > 255) a = 255;
    return (a << 24) | (colour & 0x00ffffff);
}

// A curved BEA panel using HUD's own crescent end-caps + translucent dark glass, with clean box fallback.
void StrategicPanel(float x, float y, float w, float h, float t, float alpha) {
    if (HasHudCapsuleArt()) {
        DrawBeaCurvedPanel(x, y, w, h, (std::min)(h, 22.f * L.s), 0xb406101c, kCapsuleCap, true, alpha);
        return;
    }
    Box(x, y, w, h, Faded(kPanel, alpha));
    Outline(x, y, w, h, (std::max)(1.f, floorf(t * L.s)), Faded(kEdge, alpha));
    const float cap = (std::max)(2.f, 2.f * L.s), inset = h > 12.f * L.s ? 4.f * L.s : h * 0.2f;
    Box(x + cap, y + inset, cap, h - 2.f * inset, Faded(kCapsuleCap, alpha));
    Box(x + w - 2.f * cap, y + inset, cap, h - 2.f * inset, Faded(kCapsuleCap, alpha));
}

// How far a note is into its time on screen, as the alpha it is drawn with: it fades in and out over 12 Presents (0.2 s at 60 a second).
float NoteAlpha() {
    const unsigned now = PortPresentCount();
    if (!gNote[0] || now >= gNoteUntil) return 0.f;
    const unsigned shown = now + kNoteLength - gNoteUntil, left = gNoteUntil - now;
    const float in = float(shown) / 12.f, out = float(left) / 12.f;
    return (std::min)(1.f, (std::min)(in, out));
}

#ifdef CE_COMMANDER_TEST
// Optional readout of the same projection the selected mark uses, once per drawn view.
void TraceStrategicFocus(const CeWorldViewSample &view) {
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("BEA_PORT_COMMANDER_CAMERA_TRACE") ? 1 : 0;
    if (!enabled) return;
    const CeCmdSquad *squad = ByOrdinal(gSelected);
    CeViewPoint pixel;
    if (squad && StrategicPixel(view, squad->x, squad->y, &pixel)) {
        CeCmdNote("map camera-pick %d %.2f %.2f %.3f %.3f %.3f %.3f %.3f %.3f", squad->ordinal,
                  pixel.x, pixel.y, gViewX + gViewSpan * .5f, gViewY + gViewSpan * .5f,
                  gStrategicFocusZ, gViewSpan, gStrategicYaw, gStrategicPitch);
    }
}
#endif

void DrawStrategicHints() {
    Refresh(); RefreshUnitFrame();
    CeWorldViewSample view;
    const bool haveView = StrategicViewRead(&view);
#ifdef CE_COMMANDER_TEST
    if (haveView) TraceStrategicFocus(view);
#endif
    const float u = L.s, t = (std::max)(1.f, floorf(u));
    // Build current frame HUD model and active toast/help rects BEFORE drawing any
    // world-space markers so selection boxes, rings, and structure bars never bleed over HUD panels.
    char forcesSummary[96] = "";
    BuildRtsHudModel(forcesSummary, sizeof forcesSummary);
    const float cardY = gHudLayout.valid ? gHudLayout.barRect.y : (StrategicUiBottom() - (40.f + kCardH) * u);

    gActiveHelpRectValid = false;
    gActiveNoticeRectValid = false;
    const float noticeAlpha = gDown[DIK_F1] ? 0.f : NoteAlpha();
    float noticeX = 0.f, noticeY = 0.f, noticeW = 0.f;
    if (noticeAlpha > 0.f) {
        noticeW = (std::min)(TextWidth(gNote, u), L.w - 32.f * u);
        if (gNoteAtUntil == gNoteUntil && gNoteAtX >= 0.f) {
            noticeX = floorf((std::min)((std::max)(16.f * u, gNoteAtX + 18.f * u), L.w - noticeW - 16.f * u));
            noticeY = floorf((std::min)((std::max)(36.f * u, gNoteAtY - LineHeight() - 18.f * u), cardY - LineHeight() - 16.f * u));
        } else {
#ifdef CE_CMD_HUD_HAS_SUBTITLE_SAFE_RECT
            if (gHudLayout.valid && gHudLayout.subtitleSafeRect.w > 16.f && gHudLayout.subtitleSafeRect.h > 8.f) {
                noticeX = floorf(gHudLayout.subtitleSafeRect.x + (gHudLayout.subtitleSafeRect.w - noticeW) * 0.5f);
                noticeY = floorf(gHudLayout.subtitleSafeRect.y + (gHudLayout.subtitleSafeRect.h - LineHeight()) * 0.5f);
            } else
#endif
            {
                // Dock global commander notices cleanly below the top capsules so center combat stays unobstructed.
                noticeX = floorf((L.w - noticeW) * 0.5f);
                noticeY = floorf(42.f * u);
            }
        }
        gActiveNoticeRect = {noticeX - 16.f * u, noticeY - 6.f * u,
                             noticeX + noticeW + 16.f * u, noticeY + LineHeight() + 6.f * u};
        gActiveNoticeRectValid = true;
    }
    if (gDown[DIK_F1]) {
        gActiveHelpRect = {8.f * u, 34.f * u, L.w - 8.f * u, (std::max)(34.f * u, cardY - 6.f * u)};
        gActiveHelpRectValid = true;
    }

    const float dragX = float(MOUSE_X), dragY = float(MOUSE_Y);
    const bool showBox = gBoxing && CeBoxIsDrag(gBoxX, gBoxY, dragX, dragY, kBoxSlack * u);
    CeCmdSel inBox = {};
    StrategicBoxPoint boxHits[kCeSelMax];
    if (showBox && haveView) StrategicBoxMembers(view, gBoxX, gBoxY, dragX, dragY, inBox, boxHits);
    gHover = haveView && StrategicWorldArea(float(MOUSE_X), float(MOUSE_Y))
        ? StrategicPick(view, float(MOUSE_X), float(MOUSE_Y)) : -1;
    // The squads: one mark each, the same size for all; a picked squad wears a white ring outside its mark, the one under the pointer a yellow one. The
    // orders of the picked squads are drawn first, under the marks: a line that grows from the squad to its goal and a goal that settles as it arrives.
    const float sinceOrder = float(SecondsBetween(gOrderNs, Ns()));
    const float grow = (std::min)(1.f, sinceOrder / 0.2f), settle = (std::min)(1.f, sinceOrder / 0.3f);
    if (haveView) for (int i = 0; i < gCount; ++i) {
        const CeCmdSquad& squad = gSquads[i];
        if (!squad.friendly || !CeCmdMarches(squad.verb) || squad.arrived) continue;     // every march on the map until it arrives; the unpicked ones fainter
        const DWORD lineColour = squad.selected ? kOrderLine : kGoalDim;
        CeViewPoint pixel, target;
        if (!StrategicPixel(view, squad.x, squad.y, &pixel) || !StrategicWorldArea(pixel.x, pixel.y)) {
            // The squad is off the picture but its goal may not be: the goal's ring, and its line from the goal toward the squad as far as the picture
            // goes (walked in world steps, so it leaves the window the way the squad lies).
            if (!StrategicPixel(view, squad.gx, squad.gy, &target) || !StrategicWorldArea(target.x, target.y)) continue;
            CeViewPoint last = target;
            for (int k = 1; k <= 40; ++k) {
                const float f = float(k) / 40.f;
                CeViewPoint p;
                if (!StrategicPixel(view, squad.gx + (squad.x - squad.gx) * f, squad.gy + (squad.y - squad.gy) * f, &p) || !StrategicWorldArea(p.x, p.y)) break;
                last = p;
            }
            Dots(target.x, target.y, last.x, last.y, t, lineColour);
            GroundRing(view, squad.gx, squad.gy, GoalHeight(squad.gx, squad.gy, gStrategicFocusZ), 3.f, (std::max)(1.5f, t), lineColour);
            continue;
        }
        if (!StrategicPixel(view, squad.gx, squad.gy, &target) || !StrategicWorldArea(target.x, target.y)) {
            if (StrategicRouteBearing(view, squad, pixel, t, target)) DrawRouteArrow(pixel, target, t);
            continue;
        }
        const float tx = pixel.x + (target.x - pixel.x) * grow, ty = pixel.y + (target.y - pixel.y) * grow, g = (12.f + 6.f * (1.f - settle)) * u;
        Dots(pixel.x, pixel.y, tx, ty, t, lineColour);
        (void)g;
        GroundRing(view, squad.gx, squad.gy, GoalHeight(squad.gx, squad.gy, gStrategicFocusZ), 3.f + 2.f * (1.f - settle), (std::max)(1.5f, t), lineColour);
        Box(target.x - u, target.y - u, 2.f * u, 2.f * u, lineColour);
    }
    // The marks first (the picked and the hovered last, on top), then the counts: a count is printed right of its mark only where it touches no other mark
    // and stands at least a space clear of every count already printed (the picked and the hovered print theirs first), so that two never read as one
    // number.
    struct Rect { float x0, y0, x1, y1; } marks[kMaxSquads], taken[kMaxSquads];
    int who[kMaxSquads], nMarks = 0, nTaken = 0;
    const float half = 8.f * u, gap = 4.f * u;
    for (int pass = 0; pass < 2 && haveView; ++pass) for (int i = 0; i < gCount && nMarks < kMaxSquads; ++i) {
        const CeCmdSquad& squad = gSquads[i];
        if (!squad.visible && !squad.friendly) continue;
        const bool first = squad.selected || squad.ordinal == gHover;
        if (!first) continue; // Native models carry the scene; marks explain selection and hover.
        if (first != (pass == 1)) continue;
        CeViewPoint pixel;
        const float outer = half + 5.f * u;
        if (!StrategicPixel(view, squad.x, squad.y, &pixel) ||
            !StrategicRectClearsHud(pixel.x - outer, pixel.y - outer, pixel.x + outer, pixel.y + outer)) continue;
        const DWORD colour = squad.friendly ? (squad.commandable ? kFriend : kFriendDim) : kEnemy;
        Box(pixel.x - half, pixel.y - half, half * 2, half * 2, Faded(colour, 0.35f));
        Outline(pixel.x - half, pixel.y - half, half * 2, half * 2, t, colour);
        if (squad.selected) Outline(pixel.x - half - 3.f * u, pixel.y - half - 3.f * u, (half + 3.f * u) * 2, (half + 3.f * u) * 2, t, kPick);
        if (squad.ordinal == gHover) Outline(pixel.x - half - 5.f * u, pixel.y - half - 5.f * u, (half + 5.f * u) * 2, (half + 5.f * u) * 2, t, kFocus);
        marks[nMarks] = {pixel.x - half, pixel.y - half, pixel.x + half, pixel.y + half};
        who[nMarks++] = i;
    }
    for (int pass = 0; pass < 2; ++pass) for (int m = nMarks - 1; m >= 0; --m) {
        const CeCmdSquad& squad = gSquads[who[m]];
        const bool first = squad.selected || squad.ordinal == gHover;
        if (first != (pass == 0)) continue;
        char count[24]; snprintf(count, sizeof(count), "%d", squad.members);
        const float cx = marks[m].x1 + 6.f * u, cy = (marks[m].y0 + marks[m].y1) * 0.5f - LineHeight() * 0.5f, cw = TextWidth(count, u), ch = LineHeight();
        bool clear = nTaken < kMaxSquads && StrategicRectClearsHud(cx, cy, cx + cw, cy + ch);
        for (int k = 0; k < nMarks && clear; ++k)
            if (k != m && cx < marks[k].x1 && cx + cw > marks[k].x0 && cy < marks[k].y1 && cy + ch > marks[k].y0) clear = false;
        for (int k = 0; k < nTaken && clear; ++k)
            if (cx - gap < taken[k].x1 && cx + cw + gap > taken[k].x0 && cy < taken[k].y1 && cy + ch > taken[k].y0) clear = false;
        if (!clear) continue;		// a neighbour's mark or count is there: this count is left out rather than printed against it
        taken[nTaken++] = {cx, cy, cx + cw, cy + ch};
        Text(cx, cy, kText, count);
    }
    // Mark one enclosed point per squad. A visible member may be inside while its centre is not.
    for (int i = 0; i < inBox.n; ++i) {
        const StrategicBoxPoint& hit = boxHits[i];
        const float r = half + 6.0f * u, leg = (std::max)(3.0f, 4.0f * u);
        const float stroke = (std::max)(2.0f, 1.5f*u);
        Box(hit.x-r, hit.y-r, leg, stroke, kPick);
        Box(hit.x-r, hit.y-r, stroke, leg, kPick);
        Box(hit.x+r-leg, hit.y+r-stroke, leg, stroke, kPick);
        Box(hit.x+r-stroke, hit.y+r-leg, stroke, leg, kPick);
    }
    if (showBox)
        Outline((std::min)(gBoxX, float(MOUSE_X)), (std::min)(gBoxY, float(MOUSE_Y)), std::fabs(float(MOUSE_X) - gBoxX), std::fabs(float(MOUSE_Y) - gBoxY), t, kPick);

    const void* hudPilotPtr = NULL;
    const int hudAllegiance = PilotAllegiance(&hudPilotPtr);

    // the title panel, top left
    char text[160];
    StrategicTitleDisplay(text, sizeof text);
    // the title in the HUD's capsule, top left: "Commander" in white, Battle Line in grey, and "Bases X - Y" in faction colours
    CapsuleBar(16.f * u, 6.f * u, TextWidth(text, u) + 32.f * u, 22.f * u, 1.f, false, hudAllegiance);
    {
        float px = 32.f * u;
        const float ty = 10.f * u;
        Text(px, ty, kText, "Commander");
        px += TextWidth("Commander", u);
        if (gMatch.nodes > 0) {
            char lineSeg[64];
            snprintf(lineSeg, sizeof lineSeg, "   Battle Line %d of %d", gMatch.held, gMatch.nodes);
            Text(px, ty, kQuiet, lineSeg);
            px += TextWidth(lineSeg, u);
        }
        if (gMatch.nodes > 0 || gMatch.buildings[0] > 0 || gMatch.buildings[1] > 0) {
            char b0[16], b1[16];
            snprintf(b0, sizeof b0, "%d", gMatch.buildings[0]);
            snprintf(b1, sizeof b1, "%d", gMatch.buildings[1]);
            Text(px, ty, kQuiet, "   Bases ");
            px += TextWidth("   Bases ", u);
            Text(px, ty, 0xff76e6ffu, b0);
            px += TextWidth(b0, u);
            Text(px, ty, kQuiet, " - ");
            px += TextWidth(" - ", u);
            Text(px, ty, 0xffff8c66u, b1);
        }
    }

    // compact top-right utility hint (F1 Help / Esc Cockpit)
    StrategicPrompt(text, sizeof text);
    if (text[0]) {
        const float w = TextWidth(text, u);
        CapsuleBar(L.w - w - 42.f * u, 6.f * u, w + 26.f * u, 22.f * u, 1.f, false, hudAllegiance);
        float px = L.w - w - 29.f * u;
        for (char* item = text; *item; ) {
            char* end = strstr(item, "   ");
            char one[64];
            const size_t n = end ? size_t(end - item) : strlen(item);
            snprintf(one, sizeof one, "%.*s", int(n < sizeof one ? n : sizeof one - 1), item);
            char* space = strrchr(one, ' ');
            if (space) {
                *space = 0;
                Text(px, 10.f * u, kText, one); px += TextWidth(one, u);
                Text(px, 10.f * u, kQuiet, " "); px += TextWidth(" ", u);
                Text(px, 10.f * u, kQuiet, space + 1); px += TextWidth(space + 1, u);
            } else { Text(px, 10.f * u, kText, one); px += TextWidth(one, u); }
            if (!end) break;
            Text(px, 10.f * u, kQuiet, "   "); px += TextWidth("   ", u);
            item = end + 3;
        }
    }

    DrawUnitSelection();

    // Friendly production structures: tactical 3D battlefield markers with armour and build progress bars
    if (CeCmdStructureConsumeReady()) {
        CeCmdVoicePlay(CE_VOICE_BUILD_READY, Ns());
        Note("Unit production complete.");
    }
    {
        CeCmdStructureView structs[kCeMaxStructures];
        const int structCount = CeCmdStructureList(structs, kCeMaxStructures);
        const CeCmdHudPalette hudPal = CeCmdHudPaletteForAllegiance(hudAllegiance);
        for (int i = 0; i < structCount && haveView; ++i) {
            const CeCmdStructureView& s = structs[i];
            CeViewPoint pos;
            const float r = 11.f * u;
            const float barW = 24.f * u, barH = (3.f * u) > 2.f ? floorf(3.f * u) : 2.f;
            const float halfW = (std::max)(r + 2.f * t, barW * 0.5f);
            const float topPad = r + barH + 2.f * u;
            const float botPad = (s.stage == CE_FACTORY_QUEUED || s.stage == CE_FACTORY_LAUNCHING) ? (r + barH + 2.f * u) : (r + 2.f * t);
            if (!StrategicPixel(view, s.x, s.y, &pos) ||
                !StrategicRectClearsHud(pos.x - halfW, pos.y - topPad, pos.x + halfW, pos.y + botPad)) continue;
            const DWORD edge = s.selected ? hudPal.goldAccent : hudPal.cyanAccent;
            Outline(pos.x - r, pos.y - r, 2.f * r, 2.f * r, s.selected ? 2.f * t : t, edge);
            const float ratio = CeCmdHudArmourRatio(s.life, s.maxLife, 1, 1);
            Box(pos.x - barW * 0.5f, pos.y - r - barH - 2.f * u, barW, barH, hudPal.armourTrack);
            if (barW * ratio > 0.5f)
                Box(pos.x - barW * 0.5f, pos.y - r - barH - 2.f * u, barW * ratio, barH, CeCmdHudArmourColor(ratio, hudPal));
            if (s.stage == CE_FACTORY_QUEUED || s.stage == CE_FACTORY_LAUNCHING) {
                Box(pos.x - barW * 0.5f, pos.y + r + 2.f * u, barW, barH, hudPal.armourTrack);
                const float pctW = barW * (float(s.percent) / 100.f);
                if (pctW > 0.5f) Box(pos.x - barW * 0.5f, pos.y + r + 2.f * u, pctW, barH, hudPal.cyanAccent);
            }
        }
    }

    // Unified Faction-Symmetric Bottom RTS Command Bar (Unit/Squad/Structure Placards + Right-Docked Command Card)
    {
        const CeCmdHudDrawOps ops = {RtsHudBox, RtsHudOutline, RtsHudText, RtsHudTextWidth, RtsHudLineHeight, RtsHudCurvedPanel};
        CeCmdHudDrawBar(ops, gHudLayout, gHudPlacards, gHudPlacardCount, gHudCard, forcesSummary,
                        float(MOUSE_X), float(MOUSE_Y), gHudBtnFlashNs, Ns(), hudAllegiance);
    }

    // the full list of keys, while F1 is held (compact two-column Forseti reference panel above the RTS bar)
    if (gDown[DIK_F1]) {
        static const char* const leftKeys[][2] = {
            {"Click, Shift Click", "Select unit, squad or placard"},
            {"Double Click Card", "Center camera on unit or factory"},
            {"Drag Box", "Select units in box"},
            {"Ctrl A", "Select all available units"},
            {"Ctrl 1 to 0 / 1 to 0", "Save or recall control group"},
            {"Right Click", "Move or Attack selected forces"},
            {"G / Shift A", "Attack-move to ground point"},
            {"H", "Hold current position"},
            {"Space / Shift F", "Follow Battle Engine"}};
        static const char* const rightKeys[][2] = {
            {"Enter / Shift E", "Take control of selected unit"},
            {"Delete", "Back to their own orders"},
            {"B", "Fall back to safe point"},
            {"U (Factory)", "Build primary unit"},
            {"Tab / C / Z", "Cycle squad / Focus / Wide zoom"},
            {"WASD, Arrows, MMB", "Pan battlefield camera"},
            {"Q, E / PgUp, PgDn", "Orbit / Tilt camera"},
            {"Wheel, R, F", "Zoom camera"},
            {"M, Esc", "Close Commander view"}};
        const int rows = int(sizeof leftKeys / sizeof leftKeys[0]);
        float lKeyW = 0.f, lWhatW = 0.f, rKeyW = 0.f, rWhatW = 0.f;
        for (int i = 0; i < rows; ++i) {
            lKeyW = (std::max)(lKeyW, TextWidth(leftKeys[i][0], u));
            lWhatW = (std::max)(lWhatW, TextWidth(leftKeys[i][1], u));
            rKeyW = (std::max)(rKeyW, TextWidth(rightKeys[i][0], u));
            rWhatW = (std::max)(rWhatW, TextWidth(rightKeys[i][1], u));
        }
        const float colGap = 24.f * u, keyGap = 12.f * u, padX = 12.f * u;
        const float helpLine = LineHeight() + 1.5f * u;
        const float naturalW = lKeyW + keyGap + lWhatW + colGap + rKeyW + keyGap + rWhatW + 2.f * padX;
        const float maxW = (std::max)(160.f * u, L.w - 16.f * u);
        const float fit = naturalW > maxW ? maxW / naturalW : 1.f;
        const float w = floorf(naturalW * fit), h = floorf(rows * helpLine + 14.f * u);
        const float safeTop = 34.f * u;
        const float lowerClear = cardY - 6.f * u;
        const float safeBot = lowerClear - safeTop >= h ? lowerClear : (std::max)(safeTop + h, StrategicUiBottom() - 38.f * u);
        const float x0 = floorf((L.w - w) * 0.5f);
        const float y0 = floorf((std::max)(safeTop, safeTop + ((safeBot - safeTop) - h) * 0.5f));
        const CeCmdHudPalette helpPal = CeCmdHudPaletteForAllegiance(hudAllegiance);
        const float bt = floorf(u) > 1.f ? floorf(u) : 1.f;
        const float helpCap = floorf(26.f * u);
        DrawBeaCurvedPanel(x0, y0, w, h, helpCap, (helpPal.barFill & 0x00ffffffu) | 0xcc000000u,
                           hudAllegiance == 1 ? 0xffe64416u : helpPal.cyanAccent, hudAllegiance != 1, 1.f, true);
        if (w > helpCap + 4.f)
            Box(x0 + helpCap * 0.55f, y0 + bt, w - helpCap * 1.1f, bt, (helpPal.barTopBevel & 0x00ffffffu) | 0x68000000u);
        const DWORD helpDesc = helpPal.textBright;
        const float rx0 = x0 + (padX + lKeyW + keyGap + lWhatW + colGap) * fit;
        for (int i = 0; i < rows; ++i) {
            const float ry = y0 + 7.f * u + i * helpLine;
            Text(x0 + padX * fit, ry, helpPal.textCyan, leftKeys[i][0], lKeyW * fit);
            Text(x0 + (padX + lKeyW + keyGap) * fit, ry, helpDesc, leftKeys[i][1], lWhatW * fit);
            Text(rx0, ry, helpPal.textCyan, rightKeys[i][0], rKeyW * fit);
            Text(rx0 + (rKeyW + keyGap) * fit, ry, helpDesc, rightKeys[i][1], rWhatW * fit);
        }
    }

    // the last note, in a panel of its own (docked clear of center combat and bottom RTS bar), fading in and out
    if (noticeAlpha > 0.f) {
        // the HUD's capsule opens and closes with the note; its words show only once it is nearly open (they would spill past a narrow one)
        CapsuleBar(noticeX - 16.f * u, noticeY - 6.f * u, noticeW + 32.f * u, LineHeight() + 12.f * u, noticeAlpha, false, hudAllegiance);
        const float words = (std::max)(0.f, (noticeAlpha - 0.8f) / 0.2f);
        if (words > 0.f) Text(noticeX, noticeY, Faded(kText, words), gNote, noticeW);
    }
    CeFactoryView factory;
    if (!gDown[DIK_F1] && CeFactoryRead(factory)) {
        // The generalized structure loop above already draws the 3D marker, armour bar,
        // and build progress strip for this building; keep the Level 231 exercise card only.
        const float x=8*u, y=56*u, width=196*u;
        const bool delivered = factory.stage == CE_FACTORY_READY;
        const int deliveredSquad = factory.selected && delivered ? CeCmdDeliveredSquad() : -1;
        const bool selectedTank = deliveredSquad >= 0 && gSel.n == 1 && gSelected == deliveredSquad;
        StrategicPanel(x,y,width,(factory.selected?(delivered?122:99):24)*u,2*u,1);
        Text(x+8*u,y+5*u,kFriend,"Friendly vehicle factory",width-16*u);
        if (factory.selected) {
            const char* status="Ready to build";
            char progress[64];
            if (factory.stage == CE_FACTORY_QUEUED) {
                snprintf(progress,sizeof(progress),"Building tank: %d%%",factory.percent); status=progress;
            } else if (factory.stage == CE_FACTORY_LAUNCHING) status="Waiting for a clear exit";
            else if (factory.stage==CE_FACTORY_READY) status=deliveredSquad<0 ? "Tank unavailable" : selectedTank ? "Your tank selected" : "Friendly tank produced";
            else if (factory.stage == CE_FACTORY_CANCELLED) status="Request cancelled";
            else if (factory.stage == CE_FACTORY_BLOCKED || !factory.available) status="Factory unavailable";
            Text(x+8*u,y+24*u,kText,status,width-16*u);
            if (factory.stage == CE_FACTORY_QUEUED) {
                Box(x+8*u,y+44*u,width-16*u,3*u,kQuiet);
                Box(x+8*u,y+44*u,(width-16*u)*factory.percent/100.0f,3*u,kFriend);
            }
            const bool canRequest=factory.available && (factory.stage==CE_FACTORY_IDLE || factory.stage==CE_FACTORY_CANCELLED || factory.stage==CE_FACTORY_BLOCKED);
            const bool firstEnabled = delivered ? deliveredSquad >= 0 : canRequest;
            StrategicPanel(x+6*u,y+52*u,width-12*u,21*u,u,firstEnabled?1:.5f);
            Text(x+12*u,y+55*u,firstEnabled?kText:kQuiet,delivered?"Select tank":"Request vehicle",width-24*u);
            if (delivered) {
                const bool cockpit = selectedTank && CePlayUnitCanTakeDelivery();
                StrategicPanel(x+6*u,y+74*u,width-12*u,21*u,u,cockpit?1:.5f);
                Text(x+12*u,y+77*u,cockpit?kText:kQuiet,"Take cockpit",width-24*u);
                Text(x+12*u,y+100*u,kText,"Close card",width-24*u);
            } else Text(x+12*u,y+77*u,kText,factory.stage==CE_FACTORY_QUEUED?"Cancel request":"Close card",width-24*u);
        }
    }
    DrawCursor();
}
// CE_STRATEGIC_CAMERA_END

bool StrategicLive() { return gOpen && gStrategic; }

// CE_STRATEGIC_PICTURE_BEGIN
// The live view's overlay over the native 3D scene rendered inside CeRenderFence (ce_render_fence.h).
// Native entry and return easing live in NativeStrategicPresentation/NativeReturnPresentation.
void StrategicEntered() {}
void StrategicLeft() { CeCmdViewRelease(); }

int PilotAllegiance(const void** pilot) {
    CPlayer* player = GAME.GetPlayer(0);
    CBattleEngine* engine = player ? player->GetBattleEngine() : NULL;
    if (pilot) *pilot = engine;
    return engine ? (int)engine->GetAllegiance() : -1;
}

// Prepare one frame's live overlay from the captured native world view: project friendly unit marks and set up 2D drawing state.
// Returns the captured view for the pointer; false when no snapshot or font is ready.
bool StrategicPictureBegin(const CGenericCamera& camera, CeWorldViewSample* view) {
    (void)camera;
    if(!CeWorldViewRead(CeWorldViewOwner::Commander,gStrategicViewSession,view) || !DrawingReady()) return false;
    const void* pilot=NULL;
    const int friendly=PilotAllegiance(&pilot);
    CeCmdViewCollectUnits(*view,friendly,pilot);
    BeginDraw();
    return true;
}

void StrategicPictureEnd() {
    EndDraw();
}
// CE_STRATEGIC_PICTURE_END

void PanMapFrame()
{
	if (!gOpen || gStrategic) return;
	const std::uint64_t now = Ns();
	const double elapsed = (std::min)(0.1, SecondsBetween(gMapLastNs, now));
	// Idle frames count too; a new press must not inherit time spent waiting.
	gMapLastNs = now;
	// Keep the old distance at 60 FPS, without making slower frames pan more slowly.
	const float distance = static_cast<float>(elapsed * (0.012 * 60.0));
	const bool left = CameraKeyDown(DIK_LEFT), right = CameraKeyDown(DIK_RIGHT), up = CameraKeyDown(DIK_UP), down = CameraKeyDown(DIK_DOWN);
	if (left || right || up || down)
		Pan((float(right) - float(left)) * distance, (float(down) - float(up)) * distance);
}

#ifdef CE_COMMANDER_TEST
unsigned gTestFrameCount = 0, gTestOpenedAt = 0;		// frames seen by PortCeCmdFrame, and the frame the view last opened at
#endif
bool gDeferredMapUnpause = false;
void Close()
{
    const bool wasStrategic = gStrategic;
    const pKeyTrapper currentTrap = LT.GetKeytrap();
    const bool ownedStrategicInput = currentTrap == (pKeyTrapper)Trap;
    const bool competingOverlay = currentTrap != NULL && !ownedStrategicInput;
    BeginNativeReturn();
    if (gOpen && wasStrategic) {
        if (gStrategicOwner.Leave(StrategicContext()) == CeCommanderCameraResult::RestorePending) {
            CancelNativeReturn();
            Note("Press Esc again."); return;
        }
    }
    ResetStrategicGesture();
    CeFactoryLeaveSelection();
    CeCmdVoiceStop();

	if (!gOpen) return;
	CePlayUnitMapBoundary();
	gOpen = false;
	gNote[0] = 0;		// a note about the view goes with it: the cockpit shows only the notes given after the close (the Battle Engine down, the battle not started)
	if (wasStrategic)
	{
		gKeptLevel = GAME.GetCurrentLevel();
		gKeptMission = gMissionLifetime;
		gKeptCentreX = gViewX + gViewSpan * 0.5f;
		gKeptCentreY = gViewY + gViewSpan * 0.5f;
		gKeptSpan = gViewSpan;
		gKeptYaw = gStrategicYaw;
		gHasKeptYaw = true;
	}
	else
	{
		gKeptMapLevel = GAME.GetCurrentLevel();
		gKeptMapMission = gMissionLifetime;
		gKeptMapCentreX = gViewX + gViewSpan * 0.5f;
		gKeptMapCentreY = gViewY + gViewSpan * 0.5f;
		gKeptMapSpan = gViewSpan;
	}
	gArmed = false;
#ifdef CE_COMMANDER_TEST
	// The proof's mutant (BEA_PORT_COMMANDER_MUTANT_KEEP_TRAP): a view open for under forty frames leaves its key trap set when it closes;
	// the rapid arm's close judge must fail it.
	static int keepTrap = -1;
	if (keepTrap < 0) keepTrap = getenv("BEA_PORT_COMMANDER_MUTANT_KEEP_TRAP") ? 1 : 0;
	if (!(keepTrap && gTestFrameCount - gTestOpenedAt < 40))
#endif
	if (LT.GetKeytrap() == (pKeyTrapper)Trap) LT.SetKeytrap(NULL);
	if (CCONTROLLER *controller = GAME.GetController(0))
	{
		RelinquishBuriedController(controller, &gSink);
	}
	if (!wasStrategic)
	{
		if (competingOverlay)
		{
			// Another overlay (e.g. Console or Photo Mode) took the keytrap while paused in 2D map:
			// keep combat paused behind the overlay and do not steal mouse capture until it closes.
			gDeferredMapUnpause = true;
		}
		else
		{
			gDeferredMapUnpause = false;
			GAME.*GameAccess::PauseField() = FALSE;
		}
	}
	if (ownedStrategicInput || (!wasStrategic && !competingOverlay)) { InitMouse(); }
#ifdef CE_COMMANDER_TEST
	{
		CeCmdHash h;
		CeCmdWorldHash(&h);
		CeCmdNote("map close world %08x solid %08x things %u components %u time %08x frames %u avg %.2f ms max %.2f ms", h.all, h.withoutComponents,
				  h.things, h.components, h.eventTime, gFrames, gFrames ? gMsTotal / double(gFrames) : 0.0, gMsMax);
		CeCmdViewStats view;
		CeCmdViewGetStats(&view);
		CeCmdNote("view picture frames %u pieces %u marks %u vertices %u texels %u mixer %d", view.frames, view.terrainChunks, view.markers, view.vertices,
				  view.texels, view.mixer ? 1 : 0);
		CeCmdNote("view periods live %u avg %.3f ms other %u avg %.3f ms", gPeriods[1], gPeriods[1] ? double(gPeriodNs[1]) / gPeriods[1] * 1e-6 : 0.0,
				  gPeriods[0], gPeriods[0] ? double(gPeriodNs[0]) / gPeriods[0] * 1e-6 : 0.0);
		CeCmdViewAuditReport();
		DumpWorld("close");
	}
#endif
    gStrategic = false;
}

void Open(bool strategic)
{
#ifdef CE_COMMANDER_TEST
	gTestOpenedAt = gTestFrameCount;
#endif
    ResetStrategicGesture();
    PrepareNativeEntry();
	if (GAME.IsPaused() || CePlayUnitOwnsPilot()) return;
	Refresh();
	if (!CommandableCount())
	{
		int why[8] = {0, 0, 0, 0, 0, 0, 0, 0}, friendly = 0;
		for (int i = 0; i < gCount; ++i)
			if (gSquads[i].friendly && gSquads[i].why >= 0 && gSquads[i].why < 8) { ++friendly; ++why[gSquads[i].why]; }
		CeCmdNote("map none friendly %d scripted %d spawned %d aircraft %d objective %d", friendly, why[CE_WHY_SCRIPTED], why[CE_WHY_SPAWNED], why[CE_WHY_AIRCRAFT], why[CE_WHY_OBJECTIVE]);
		Note("There are no squads here to command.");
		return;
	}
	if (!strategic) { GAME.Pause(); }
	if (!strategic) { if (!GAME.IsPaused()) return; }
	gDeferredMapUnpause = false;
    if (strategic) UnitPointerSeed();
	gOpening = true;
	ShutdownMouse();		// the pointer it starts from stays in window pixels, which the map reads (PortCeCmdMapOpen)
	if (strategic) LivePointerStart();	// the live view keeps its own pointer; the game's stays on the centre
	gOpening = false;
	if (CCONTROLLER *controller = GAME.GetController(0)) controller->SetToControl(&gSink);
	LT.SetKeytrap((pKeyTrapper)Trap);
	// A key that is already held when the map opens is not a new press when it repeats (the window layer posts every auto-repeat as a key-down): the game's own
	// key state knows which ones those are, because it is not updated while a key trap is installed but cleared by every key-up.
	for (int k = 0; k < 256; ++k) gDown[k] = LT.KeyDown[k] != 0;
	for (int k = 0; k < 256; ++k) LT.KeyDown[k] = 0;
	gCloseKey = OpenKey();
	gDown[gCloseKey] = true;		// the press that opened the map may repeat; its release clears this
	gShift = false;
	gArmed = false;
	gPlayerClosed = false;
	gOpen = true;
	CePlayUnitMapBoundary();
	gLayoutLogged = false;
#ifdef CE_COMMANDER_TEST
	gFrames = 0;
	gMsTotal = gMsMax = 0.0;
#endif
	gNote[0] = 0;
	gLooseBounds = strategic;		// the live view's own bounds (SetView); the 2D map resets to false
	World();
	const bool fitted = strategic
		? (gKeptLevel != GAME.GetCurrentLevel() || gKeptMission != gMissionLifetime)
		: (gKeptMapLevel != GAME.GetCurrentLevel() || gKeptMapMission != gMissionLifetime);
	if (!fitted) SetView(strategic ? gKeptCentreX : gKeptMapCentreX,
						 strategic ? gKeptCentreY : gKeptMapCentreY,
						 strategic ? gKeptSpan : gKeptMapSpan);
	else FitView();
	Lay();		// the layout exists before the first frame draws it: a zoom or a pan in the same batch of messages as the open key works on it
	gMapLastNs = Ns(); // Reopening or leaving the live view starts a fresh pan interval.
	gMapPulseStartNs = (!strategic && gSelected < 0 && CommandableCount() > 0) ? Ns() : 0;
#ifdef CE_COMMANDER_TEST
	{
		CeCmdHash h;
		CeCmdWorldHash(&h);
		CeCmdNote("map open world %08x solid %08x things %u components %u time %08x", h.all, h.withoutComponents, h.things, h.components, h.eventTime);
		DumpWorld("open");
	}
#endif
    gStrategic = strategic;
    if (strategic && fitted) {
        // The first live view of a level opens on the battlefield the player commands, close enough to tell what things are (R7): the dry-land
        // commandable squad nearest the pilot's Battle Engine (within kMaxDrySnapDist so the camera never teleports across the whole map), or the
        // nearest dry friendly squad/structure when the pilot is over water, facing from that focus toward the enemy battle line.
        const float maxSnapSq = kMaxDrySnapDist * kMaxDrySnapDist;
        float fx = gViewX + gViewSpan * 0.5f, fy = gViewY + gViewSpan * 0.5f;
        CBattleEngine *engine = GAME.GetPlayer(0) ? GAME.GetPlayer(0)->GetBattleEngine() : NULL;
        const float ex = engine ? engine->GetPos().X : fx, ey = engine ? engine->GetPos().Y : fy;
        if (engine) { fx = ex; fy = ey; }
        float bestDry = maxSnapSq;
        bool pickedSquad = false;
        for (int i = 0; i < gCount; ++i) {
            if (!gSquads[i].commandable || gSquads[i].members <= 0) continue;
            if (!IsDryGroundPoint(gSquads[i].x, gSquads[i].y)) continue;
            const float d = (gSquads[i].x - ex) * (gSquads[i].x - ex) + (gSquads[i].y - ey) * (gSquads[i].y - ey);
            if (d <= bestDry) { bestDry = d; fx = gSquads[i].x; fy = gSquads[i].y; pickedSquad = true; }
        }
        if (!pickedSquad) {
            float bestAny = maxSnapSq;
            for (int i = 0; i < gCount; ++i) {
                if (!gSquads[i].commandable || gSquads[i].members <= 0) continue;
                const float d = (gSquads[i].x - ex) * (gSquads[i].x - ex) + (gSquads[i].y - ey) * (gSquads[i].y - ey);
                if (d <= bestAny) { bestAny = d; fx = gSquads[i].x; fy = gSquads[i].y; pickedSquad = true; }
            }
            if (!IsDryGroundPoint(fx, fy)) {
                float bestDryAlly = maxSnapSq;
                for (int i = 0; i < gCount; ++i) {
                    if (!gSquads[i].friendly || gSquads[i].members <= 0 || !IsDryGroundPoint(gSquads[i].x, gSquads[i].y)) continue;
                    const float d = (gSquads[i].x - ex) * (gSquads[i].x - ex) + (gSquads[i].y - ey) * (gSquads[i].y - ey);
                    if (d <= bestDryAlly) { bestDryAlly = d; fx = gSquads[i].x; fy = gSquads[i].y; pickedSquad = true; }
                }
                CeCmdStructureView structs[kCeMaxStructures];
                const int nStructs = CeCmdStructureList(structs, kCeMaxStructures);
                for (int i = 0; i < nStructs; ++i) {
                    if (!IsDryGroundPoint(structs[i].x, structs[i].y)) continue;
                    const float d = (structs[i].x - ex) * (structs[i].x - ex) + (structs[i].y - ey) * (structs[i].y - ey);
                    if (d <= bestDryAlly) { bestDryAlly = d; fx = structs[i].x; fy = structs[i].y; }
                }
            }
        }
        SetView(fx, fy, kLiveOpenSpan);
        const float focusX = gViewX + gViewSpan * 0.5f, focusY = gViewY + gViewSpan * 0.5f;
        gHasStrategicInitialYaw = ComputeStrategicInitialYaw(focusX, focusY, &gStrategicInitialYaw);
        gHasKeptYaw = false;
        gZoomTrip.on = false; gZoomBack = kLiveOpenSpan;
        CeCmdNote("map live focus %s %.1f %.1f", pickedSquad ? "squad" : engine ? "engine" : "view", fx, fy);
    }
    if (strategic) {
        const CeCommanderCameraResult result = gStrategicOwner.Enter(StrategicContext());
        if (result != CeCommanderCameraResult::Entered) {
            Close();
            if (fitted) { gKeptLevel = -1; gKeptMission = 0; gHasKeptYaw = false; gHasStrategicInitialYaw = false; }
            Note("The view could not open."); return;
        }
        gStrategicLastNs = Ns();
    }
}

// Keys and mouse buttons while the map is open. The mouse module reads a trap's return value (true: the button was taken).
bool Trap(WORD key, int event)
{
	LivePointer pointer;	// the live view's keys and buttons read its own pointer
    // CE_STRATEGIC_TRAP_BEGIN
    if (gStrategic) return StrategicTrap(key, event);
    if (gOpen && key == DIK_V && event == KEY_EVENT_DOWN) {
        const bool repeat = gDown[key]; gDown[key] = true;
        if (!repeat) SwitchView(true);
        return true;
    }
    // CE_STRATEGIC_TRAP_END

	if (!gOpen || LT.GetKeytrap() != (pKeyTrapper)Trap) return false;
	if (event == kMouseDown && key <= 2)
	{
		const float px = float(MOUSE_X), py = float(MOUSE_Y);
		// the live view's orders and picking (ce_cmdsel.h's selection is the one both views order)
		const bool ctrl = gDown[DIK_LCONTROL] || gDown[DIK_RCONTROL];
		if (key == 0 && gArmed && OnMap(px, py)) { gArmed = false; Order(CE_CMD_ADVANCE, WorldX(px), WorldY(py)); }
		else if (key == 0)
		{
			const int hit = PickAt(px, py);
			const CeCmdSquad *s = ByOrdinal(hit);
			if (s && !s->commandable) Note(WhyText(s->why));
			else if (s && gShift) SelectToggle(hit);
			else if (s) SelectOnly(hit);
			else if (OnMap(px, py) && !gShift) SelectOnly(-1);
		}
		else if (key == 2 && OnMap(px, py))
		{
			gArmed = false;
			Order(ctrl ? CE_CMD_MOVE : CE_CMD_ADVANCE, WorldX(px), WorldY(py));
		}
		return true;
	}
	if (event == kMouseUp && (key == 3 || key == 4)) { ZoomAt(key == 3 ? 0.8f : 1.25f, float(MOUSE_X), float(MOUSE_Y)); return true; }
	if (event == kMouseUp) return true;		// a button's release
	if (event == KEY_EVENT_UP) { if (key < 256) gDown[key] = false; if (key == DIK_LSHIFT || key == DIK_RSHIFT) gShift = false; return true; }
	if (event != KEY_EVENT_DOWN || key >= 256) return true;
	if (key == DIK_LSHIFT || key == DIK_RSHIFT) { gShift = true; gDown[key] = true; return true; }
	if (gDown[key]) return true;
	gDown[key] = true;
	if (key == gCloseKey) { Close(); return true; }
	switch (key)
	{
	case DIK_ESCAPE:
		if (gArmed) { gArmed = false; Note("Advance cancelled."); }
		else if (gSelected >= 0) SelectOnly(-1);
		else { gEscClosed = true; Close(); }
		break;
	case DIK_TAB: Cycle(gShift ? -1 : 1); Reveal(gSelected); break;
	case DIK_Q: ZoomAt(1.25f, L.mapX + L.mapSide * 0.5f, L.mapY + L.mapSide * 0.5f); break;
	case DIK_E: ZoomAt(0.8f, L.mapX + L.mapSide * 0.5f, L.mapY + L.mapSide * 0.5f); break;
	case DIK_SPACE:
		if (CBattleEngine *engine = GAME.GetPlayer(0) ? GAME.GetPlayer(0)->GetBattleEngine() : NULL) SetView(engine->GetPos().X, engine->GetPos().Y, gViewSpan);
		break;
	case DIK_HOME: FitView(); break;
	case DIK_C:
		if (const CeCmdSquad *c = ByOrdinal(gSelected)) SetView(c->x, c->y, gViewSpan);
		break;
	case DIK_A:
		if (gDown[DIK_LCONTROL] || gDown[DIK_RCONTROL]) { gArmed = false; SelectAll(); }
		else if (ByOrdinal(gSelected)) { gArmed = true; Note("Advance: click the ground."); }
		else Note("Pick a squad first.");
		break;
	case DIK_H: Order(CE_CMD_HOLD, 0.f, 0.f); break;
	case DIK_X: Order(CE_CMD_HOLD, 0.f, 0.f); break;
	case DIK_B: gArmed = false; Order(CE_CMD_FALLBACK, 0.f, 0.f); break;
	case DIK_DELETE: gArmed = false; Order(CE_CMD_RELEASE, 0.f, 0.f); break;
	default:
		if (key >= DIK_1 && key <= DIK_0)
		{
			const bool ctrl = gDown[DIK_LCONTROL] || gDown[DIK_RCONTROL];
			gArmed = false;
			GroupKey(key == DIK_0 ? 0 : int(key - DIK_1) + 1, ctrl, gShift && !ctrl);
		}
		break;
	}
	return true;
}

void LogLayout()
{
	CeCmdNote("map layout window %.0f %.0f scale %.3f map %.0f %.0f %.0f k %.4f view %.1f %.1f %.1f", L.w, L.h, L.s, L.mapX, L.mapY, L.mapSide, L.k, gViewX, gViewY, gViewSpan);
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].friendly)
			CeCmdNote("map box %d %.0f %.0f %d %d why %d deploys %d %c %s", gSquads[i].ordinal, Px(gSquads[i].x), Py(gSquads[i].y), gSquads[i].members,
					  gSquads[i].commandable ? 1 : 0, gSquads[i].why, gSquads[i].deploys ? 1 : 0, gSquads[i].vehicles ? 'N' : 'R', gSquads[i].unit[0] ? gSquads[i].unit : "-");
}

#ifdef CE_COMMANDER_TEST
// ---- the order tape's ui verb -------------------------------------------------------------------------------------------------------------
// What the player does on the open map, done through the map's own trap with the pointer held exactly where the action needs it: a script cannot
// know beforehand where a squad's mark will be drawn, and the map can. The window point comes from the same view the picking uses.

// The window point of a world point on the open map; false when the map cannot say (no fresh view, or the point is off the screen).
bool UiPoint(float wx, float wy, float *px, float *py)
{
	if (!gStrategic) { *px = Px(wx); *py = Py(wy); return OnMap(*px, *py); }
	CeWorldViewSample view;
	CeViewPoint pixel;
	if (!StrategicViewRead(&view) || !StrategicPixel(view, wx, wy, &pixel) || !StrategicWorldArea(pixel.x, pixel.y)) return false;
	*px = pixel.x;
	*py = pixel.y;
	return true;
}

// One button press and release at a window point, as the mouse module would hand them to the trap.
void UiPress(int button, float px, float py)
{
	const int x = int(px + 0.5f), y = int(py + 0.5f);
	if (gStrategic) { gLiveX = x; gLiveY = y; gLiveForced = true; gLiveShown = true; }
	else { MOUSE_X = x; MOUSE_Y = y; }
	CeCmdNote("map pointer %d %d %d", button, x, y);
	Trap(WORD(button), kMouseDown);
	Trap(WORD(button), kMouseUp);
	gLiveForced = false;
}

void UiKey(WORD key)
{
	Trap(key, KEY_EVENT_DOWN);
	Trap(key, KEY_EVENT_UP);
}

// The left button down at one window point and up at another: a box on the live view.
void UiDrag(float x0, float y0, float x1, float y1)
{
	if (!gStrategic) return;
	gLiveX = int(x0 + 0.5f); gLiveY = int(y0 + 0.5f); gLiveForced = true;
	CeCmdNote("map pointer 0 %d %d", gLiveX, gLiveY);
	Trap(0, kMouseDown);
	gLiveX = int(x1 + 0.5f); gLiveY = int(y1 + 0.5f);
	CeCmdNote("map pointer 0 %d %d up", gLiveX, gLiveY);
	Trap(0, kMouseUp);
	gLiveForced = false;
}
#endif
}

#ifdef CE_COMMANDER_TEST
void CeCmdTestUi(const char *word, const float *n, int count, char *said, unsigned room)
{
	if (!gOpen || LT.GetKeytrap() != (pKeyTrapper)Trap) { snprintf(said, room, "refused: the map is not open"); return; }
	Lay();
	Refresh();
	float px = 0, py = 0;
	if (!strcmp(word, "marks"))
	{
		CeWorldViewSample view;
		if (!StrategicViewRead(&view)) { snprintf(said, room, "refused: no fresh view"); return; }
		unsigned count;
		const CeCmdViewUnitMark *marks = CeCmdViewUnitMarks(count);
		for (unsigned i = 0; i < count; ++i) {
			const CeCmdViewUnitMark &m = marks[i];
			const CeCmdSquad *s = ByOrdinal(m.squad);
			if (s && StrategicWorldArea(m.x, m.y))
				CeCmdNote("map unit-mark %d %.1f %.1f %.1f %d", m.squad, m.x, m.y, m.radius, int(s->commandable));
		}
		snprintf(said, room, "read %u drawn members", count);
		return;
	}
	if (!strcmp(word, "click") || !strcmp(word, "shiftclick"))
	{
		const CeCmdSquad *s = count >= 1 ? ByOrdinal(int(n[0])) : NULL;
		if (!s) { snprintf(said, room, "refused: no such squad"); return; }
		if (!UiPoint(s->x, s->y, &px, &py)) { snprintf(said, room, "refused: the squad is not on the screen"); return; }
		const bool shift = word[0] == 's';
		if (shift) { gDown[DIK_LSHIFT] = true; gShift = true; }
		UiPress(0, px, py);
		if (shift) { gDown[DIK_LSHIFT] = false; gShift = false; }
	}
	else if (!strcmp(word, "order") || !strcmp(word, "advance") || !strcmp(word, "move"))
	{
		if (count < 2) { snprintf(said, room, "refused: arguments"); return; }
		if (!UiPoint(n[0], n[1], &px, &py)) { snprintf(said, room, "refused: the point is not on the screen"); return; }
		if (word[0] == 'a') { UiKey(DIK_A); UiPress(0, px, py); }
		else if (word[0] == 'm') { gDown[DIK_LCONTROL] = true; UiPress(2, px, py); gDown[DIK_LCONTROL] = false; }	// Ctrl and a right click
		else UiPress(2, px, py);
	}
	else if (!strcmp(word, "box"))
	{
		// Two world corners; the box is dragged between the window points they are drawn at.
		float qx = 0, qy = 0;
		if (count < 4) { snprintf(said, room, "refused: arguments"); return; }
		if (!UiPoint(n[0], n[1], &px, &py) || !UiPoint(n[2], n[3], &qx, &qy)) { snprintf(said, room, "refused: the box is not on the screen"); return; }
		UiDrag(px, py, qx, qy);
		snprintf(said, room, "ok at %d %d to %d %d", int(px + 0.5f), int(py + 0.5f), int(qx + 0.5f), int(qy + 0.5f));
		return;
	}
	else if (!strcmp(word, "at"))
	{
		if (count < 3 || n[2] < 0 || n[2] > 2) { snprintf(said, room, "refused: arguments"); return; }
		px = n[0];
		py = n[1];
		UiPress(int(n[2]), px, py);
	}
	else { snprintf(said, room, "refused: no such action"); return; }
	snprintf(said, room, "ok at %d %d", int(px + 0.5f), int(py + 0.5f));
}
#endif

// The world pass the battle would have drawn (PortCeCmdRenderWorld, below): while it runs, the cockpit's camera and cockpit go to the engine untouched.
static bool gCockpitPass = false;

extern "C" bool PortCeWorldViewCaptureSuspended()
{
    return gCockpitPass;
}

CCamera* PortCeCmdRenderCamera(CCamera* camera, CPlayer* player)
{
    if (gCockpitPass) return camera;
    if (!gOpen && gNativeReturning) {
        CCamera* returning = NativeReturnPresentation(camera, player);
        if (returning) return returning;
    }
#ifdef CE_COMMANDER_TEST
    if(camera!=gStrategicOriginal || !gOpen || !gStrategic || !gStrategicRender || !gStrategicOwner.Active()) return camera;
#else
    if(camera!=gStrategicOriginal || !gOpen || !gStrategic || !gStrategicRender || !gStrategicOwner.Active() || player!=GAME.GetPlayer(0)) return camera;
#endif
    LivePointer pointer;
    if(!StrategicFrameCamera()) return camera;
    return NativeStrategicPresentation(camera);
}

uint64_t PortCeCmdWorldViewToken()
{
    return gOpen && gStrategic && gStrategicRender &&
        gStrategicOwner.Active() ? gStrategicViewSession : 0;
}

bool PortCeCmdSuppressFlightHud()
{
    return false;
}

bool PortCeCmdHideCockpit()
{
    return !gCockpitPass && (PortCeCmdWorldViewToken() != 0 || NativeReturnEligible());
}

// ---- Drawing that leaves the battle alone ----------------------------------------------------------------------------------------------------
// In the original, drawing writes simulation state: CComponent::RefreshCachedTransform updates a component's cached position and orientation from
// its parent's mesh at most once a render frame, drawing rebuilds mesh pose caches and advances motion-controller poses, and SHADOWS picks the
// nearest units to the drawing camera so the next frame's SHADOWS.RenderTextures refreshes those units. While the Commander view is open or easing
// back to the cockpit, draw the cockpit's own pass first (suspending world-view snapshot capture so the displayed Commander snapshot stays intact)
// so every simulation-facing render side-effect runs identically to ordinary play, then clear colour and depth and draw the Commander view's pass
// last inside the render fence (components, motion controllers, mesh pose caches and shadow manager saved and restored, with the battle marked
// paused so once-a-frame battle side-effects do not double-run).
bool PortCeCmdRenderWorld(int viewpoint)
{
    static bool inside = false;
    if (inside || gMirrorPass || viewpoint != 0 || (PortCeCmdWorldViewToken() == 0 && !NativeReturnEligible())) return false;
    CPlayer* player = GAME.GetPlayer(0);
    CCamera* cockpit = GAME.GetCamera(0);
    if (!player || !cockpit) return false;
    inside = true;
    CViewport viewport = *ENGINE.GetViewportForViewpoint(0);
    gCockpitPass = true;
    ENGINE.SetViewpoint(0, cockpit, &viewport, player);
    ENGINE.Render(0);
    gCockpitPass = false;
    if (CeRenderFenceSave()) {
        LT.D3D_Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x00607080, 1.0f, 0L);
        ENGINE.SetViewpoint(0, cockpit, &viewport, player);
        const BOOL paused = GAME.*GameAccess::PauseField();
        GAME.*GameAccess::PauseField() = TRUE;
        ENGINE.Render(0);
        GAME.*GameAccess::PauseField() = paused;
        CeRenderFenceRestore();
    } else {
        CeWorldViewInvalidate(CeWorldViewOwner::Commander);
    }
    inside = false;
    return true;
}

// Whether the map is open (or is opening: the pointer ShutdownMouse starts it from is a window pixel too): while it is, the pointer stays in window pixels (the map reads them), not the virtual screen's that
// the game's in-game menus use (widescreen.cpp).
bool PortCeCmdMapOpen() { return gOpen || gOpening; }

// The paused map's window on the world (lower-left corner and span), for the frame-feel log (feel.h) at each Present: reads fields only. 0 when the map is
// not the paused one.
extern "C" int CeCmdFeelView(float *out)
{
	if (!gOpen || gStrategic) return 0;
	out[0] = gViewX; out[1] = gViewY; out[2] = gViewSpan;
	return 1;
}

// ---- the hooks (patch 0162) ----------------------------------------------------------------------------------------------------------

// The end of the CE key chain in the window procedure (ltshell.cpp), after photo mode's and the console's keys and before the key trap: the open key, in a level
// where the map may open; it stands aside for any key trap that is installed (the console typing, the pause menu).
bool PilotUp();		// below, with the match's frame

bool PortCeCmdKey(unsigned key, bool repeat)
{
	if (key == DIK_ESCAPE && gEscClosed)	// the press that closed the map was the map's; its auto-repeats are too, until a fresh press (the key-down message's repeat flag is clear)
	{
		if (!repeat) gEscClosed = false;
		else if (!LT.GetKeytrap()) return true;
	}
	if (LT.GetKeytrap() || !CeCmdMapOn() || key != OpenKey() || PortCeActionConflict(CeControlAction::Commander, (int)key)) return false;
	if (CePlayUnitOwnsPilot() && NoteMay())
	{
		if (!repeat) Note("Return to your Battle Engine to command.");
		// Commander stays closed; this key can be bound to a unit action.
		return false;
	}
	// In a match, while the commander's Battle Engine is down, the key answers: it asks for the view when the engine is back, or stops asking (ce_cmdrules.h).
	if (CeCmdMatch() && GAME.GetGameState() == GAME_STATE_PLAYING && NoteMay() && !PilotUp())
	{
		if (!repeat && CeCmdReopenKey(gReopen, true, PilotUp()))
		{
			Note(gReopen ? "The view opens when your Battle Engine is back." : "The view stays closed.");
			CeCmdNote("map reopen ask %d", gReopen ? 1 : 0);
		}
		return true;
	}
	if (NotYet())
	{
		if (!repeat) { Note("The battle has not started yet."); gNoteNotYet = true; }
		return true;
	}
	if (!Allowed()) return false;
	if (repeat) return true;
	Open(true);
	if (gOpen && !gToldInFight) { gToldInFight = true; Note("Your Battle Engine is still in the fight."); }		// the one line of guidance, on the level's first open
	return true;
}

#ifdef CE_COMMANDER_TEST
// Two proof-run controls for the frozen-state check, both logged as `map hashat`: BEA_PORT_COMMANDER_HASHAT=N[,M...] logs the world's hash at those Presents
// (a world that runs differs, a paused one does not), and BEA_PORT_COMMANDER_PAUSEAT=N,M pauses the game as the map does, with no map, from Present N to
// Present M (what a bare pause leaves alone).
static void HashAt(const char *what, unsigned present)
{
	CeCmdHash h;
	CeCmdWorldHash(&h);
	CeCmdNote("map hashat %u %s world %08x solid %08x things %u components %u time %08x paused %d", present, what, h.all, h.withoutComponents, h.things,
			  h.components, h.eventTime, GAME.IsPaused() ? 1 : 0);
}

static void TestFrame()
{
	if (!CeCmdMapOn() || GAME.GetGameState() != GAME_STATE_PLAYING) return;
	static const char *at = getenv("BEA_PORT_COMMANDER_HASHAT");
	static const char *pause = getenv("BEA_PORT_COMMANDER_PAUSEAT");
	static bool paused;
	const unsigned present = PortPresentCount();
	if (at && *at)
	{
		const unsigned want = (unsigned)strtoul(at, NULL, 10);
		if (want && present >= want)
		{
			HashAt("probe", present);
			const char *comma = strchr(at, ',');
			at = comma ? comma + 1 : "";
		}
	}
	if (pause && *pause)
	{
		const unsigned from = (unsigned)strtoul(pause, NULL, 10), to = (unsigned)strtoul(strchr(pause, ',') ? strchr(pause, ',') + 1 : "0", NULL, 10);
		if (!paused && from && present >= from && present < to && !gOpen && !GAME.IsPaused())
		{
			GAME.Pause();
			paused = GAME.IsPaused() != FALSE;
			HashAt("pause", present);
		}
		else if (paused && present >= to)
		{
			HashAt("unpause", present);
			GAME.*GameAccess::PauseField() = FALSE;
			paused = false;
			pause = "";
		}
	}
}
#endif

// A Commander match, once an update (from PortCeCmdFrame): how the battle stands while the live view is open, and the view again after the commander's
// Battle Engine is back in the field (a versus level gives unlimited lives) when it closed because the engine went down. It opens only where the open
// key could open it and nothing else holds the keys (the pause menu, the console).
bool PilotUp()
{
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
	return engine && engine->GetLife() > 0.f && !engine->IsDying();
}

bool gLiveLast;			// the live view was open at the end of the last update
void MatchFrame()
{
	if (!CeCmdMapOn()) return;		// the Commander is off: nothing here runs
	const bool match = CeCmdMatch() && GAME.GetGameState() == GAME_STATE_PLAYING;
	// the view's return after the engine went down (ce_cmdrules.h: CeCmdReopenStep decides; this does what it says)
	const bool up = PilotUp();
	switch (CeCmdReopenStep(gReopen, gLiveLast, gOpen, match, up, gPlayerClosed, !gOpen && up && !LT.GetKeytrap() && Allowed()))
	{
	case CE_REOPEN_WAIT:
		Note("Your Battle Engine is down. The view comes back with it.");
		CeCmdNote("map reopen wait");
		break;
	case CE_REOPEN_OPEN:
		Open(true);
		if (gOpen) { Note("Your Battle Engine is back in the field."); CeCmdNote("map reopen"); }
		break;
	default: break;
	}
	if (gOpen && gStrategic && GAME.GetGameState() == GAME_STATE_PLAYING)
	{
		CeCmdMatchRead(&gMatch);
		if (match)
		{
			static unsigned updates;
			static const char *const said[] = {"", "A building of your base is down.", "Your Battle Engine is badly hurt. Esc takes you back.",
											   "Your base is under attack.", "Your Battle Engine is hurt."};
			const int alert = CeCmdAlertStep(gAlert, gMatch.buildings[0], gMatch.baseLife, gMatch.life, gMatch.lifeFull, ++updates);	// the quiet time is the rule's
			if (alert > CE_ALERT_NONE && alert <= CE_ALERT_ENGINE_HURT) { Note(said[alert]); CeCmdNote("map alert %d", alert); }
		}
		else { CeCmdAlertReset(gAlert); }
	}
	else { gMatch = CeCmdMatchState{}; CeCmdAlertReset(gAlert); }
	gLiveLast = gOpen && gStrategic;
}

// Between frames (CGame::MainLoop): the map closes where it is no longer allowed.
void PortCeCmdFrame()
{
	LivePointer pointer;	// the camera's update reads the pointer (a grabbed view follows it)
    if (gOpen && gStrategic) { UnitPointerObserve(); StrategicFrame(); }
	if (gOpen && (!Allowed() || LT.GetKeytrap() != (pKeyTrapper)Trap)) Close();
	if (!gOpen && gDeferredMapUnpause && !LT.GetKeytrap())
	{
		gDeferredMapUnpause = false;
		const bool menuOpen = GAMEINTERFACE.IsMenuUp() || (GAME.GetPauseMenu() && GAME.GetPauseMenu()->mActive);
		if (!menuOpen && GAME.IsPaused())
		{
			GAME.*GameAccess::PauseField() = FALSE;
			InitMouse();
		}
	}
	MatchFrame();
#ifdef CE_COMMANDER_TEST
	TestFrame();
	// Nothing of the view's is left once it has closed (the isolation proof's toggle and rapid arms read it). Every close is judged at the first
	// frame the view is shut ("now": the cockpit's camera is the game's, no view token or key trap is held, and the cockpit and HUD are shown
	// unless the eased return to the cockpit that a close begins is still running); a close that stays shut is judged again thirty frames on
	// ("settled": the return has finished too). The settled note is skipped when the pilot has handed over to a taken unit in the meantime.
	{
		static bool wasOpen = false;
		static CCamera* openedFrom = NULL;
		static int sinceClose = -1;
		++gTestFrameCount;
		if (gOpen && !wasOpen) { openedFrom = GAME.GetCurrentCamera(0); sinceClose = -1; }
		const char* when = NULL;
		if (!gOpen && wasOpen) { sinceClose = 0; when = "now"; }
		else if (!gOpen && sinceClose >= 0 && ++sinceClose == 30) { when = CePlayUnitOwnsPilot() ? NULL : "settled"; sinceClose = -1; }
		wasOpen = gOpen;
		if (when)
			CeCmdNote("map closed %s camera %d cockpit %d token %d keytrap %d squads %d units %d returning %d", when,
					  GAME.GetCurrentCamera(0) == openedFrom ? 1 : 0, PortCeCmdHideCockpit() ? 0 : 1, PortCeCmdWorldViewToken() == 0 ? 0 : 1,
					  LT.GetKeytrap() == (pKeyTrapper)Trap ? 1 : 0, gSel.n, gUnitUi.SelectionCount(), gNativeReturning ? 1 : 0);
	}
#endif
}

void CeCommanderLevelEnd();

// The level is ending (CGame::ShutdownRestartLoop): put back what opening changed.
void PortCeCmdLevelEnd()
{
    CeCmdVoiceStop();
    FreeTerrainTexture();
    CeFactoryReset();
    gUnitUi.Reset();
    memset(gUnitGroupSaved, 0, sizeof(gUnitGroupSaved));
    gUnitHiddenCount = 0;
    gHudFollowSquad = -1;
    gStructFocusHandle = kCeEntityHandleInvalid;
    gObservedPilotHandle = kCeEntityHandleInvalid;
    gActiveNoticeRectValid = false;
    gActiveHelpRectValid = false;
    CeCmdUnitOrdersWorldEnd();
    CeCommanderLevelEnd();
    CePlayUnitLevelEnd();
    gStrategicOwner.Reset();
	Close();
    gDeferredMapUnpause = false;
    CancelNativeReturn();
    gNativePresented = false;
    gNativeEntering = false;
	CeCmdMatchLevelEnd();	// a Commander match gives the screen back as it found it
	gKeptLevel = -1;
	gKeptMapLevel = -1;
	gKeptMission = 0;
	gKeptMapMission = 0;
	gHasKeptYaw = false;
	gHasStrategicInitialYaw = false;
	gMapPulseStartNs = 0;
	gSelected = -1;
	CeSelClear(gSel);
	CeGroupsClear(gGroups);
	gReopen = false;
	gLiveLast = false;
	gToldInFight = false;
	gMatch.match = false;
	gNote[0] = 0;
    AdvanceCameraLifetime(gMissionLifetime);
    gObservedPlayer = NULL; gObservedPilot = NULL; gObservedCamera = NULL;
}

// After the console and the photo note (CDXEngine::PostRender): the map when it is open, else the note that says why it did not open.
void PortCeCmdRender()
{
#ifdef CE_COMMANDER_TEST
	// Frame periods by the processor's own clock (never the game's), apart for frames of the live view and for the level's other frames: a proof
	// run made without the frame sleep reads the cost of the open view from them (the map's close note).
	{
		static unsigned long long last = 0;
		const unsigned long long now = PortClockRawNs();
		const bool playing = Allowed();
		if (last && playing) { const int live = gOpen && gStrategic ? 1 : 0; gPeriodNs[live] += now - last; ++gPeriods[live]; }
		last = playing ? now : 0;
	}
#endif
	// The paused map's terrain texture lives only while the map is open.
	if (!gOpen) FreeTerrainTexture();
	static bool viewSwitchRead = false;
	if (!viewSwitchRead)
	{
		viewSwitchRead = true;
		const char *mode = getenv("BEA_PORT_COMMANDER_VIEW");
		gStrategicTickOnly = mode && !strcmp(mode, "ticks");
		gStrategicMessageKeys = mode && (!strcmp(mode, "ticks") || !strcmp(mode, "messages"));
	}
	LivePointer pointer;	// the live frame's hover and its drawn pointer
    // CE_STRATEGIC_RENDER_BEGIN
    if (gOpen && gStrategic) {
        if (!Allowed() || !gStrategicOwner.Active()) { Close(); return; }
#ifdef CE_COMMANDER_TEST
        // The test build's footprint audit: the game's heaps and data copied before the view's own work of this frame (its camera's step when the
        // frame's render did not take it, its overlay) and compared after it (ce_cmdview_audit.cpp; BEA_PORT_COMMANDER_AUDIT=1). The isolation proof's
        // control (BEA_PORT_COMMANDER_CONTROL=1) changes the battle once, on purpose, while the view is open: the pilot's Battle Engine moved a hundredth
        // of a unit, so the comparison is shown to see a view that changes the battle.
        CeCmdViewAuditBefore();
        {
            static int control = -1;
            static bool controlled = false;
            if (control < 0) control = getenv("BEA_PORT_COMMANDER_CONTROL") ? 1 : 0;
            CBattleEngine *engine = GAME.GetPlayer(0) ? GAME.GetPlayer(0)->GetBattleEngine() : NULL;
            if (control && !controlled && engine) { FVector moved = engine->GetPos(); moved.X += 0.01f; engine->SetPos(moved); controlled = true; }
        }
#endif
        Lay();
        const CGenericCamera* camera = StrategicFrameCamera();
        CeWorldViewSample view;
        if (camera && StrategicPictureBegin(*camera, &view)) {
            StrategicViewSet(view);
            DrawStrategicHints();
            StrategicPictureEnd();
#ifdef CE_COMMANDER_TEST
            CeCmdViewCountFrame();		// a frame of the native picture: the isolation proof's evidence that the view was open
#endif
        } else StrategicViewInvalidate();
#ifdef CE_COMMANDER_TEST
        CeCmdViewAuditAfter();
#endif
        return;
    }
    // CE_STRATEGIC_RENDER_END

	if (gNoteNotYet && !NotYet()) { gNote[0] = 0; gNoteNotYet = false; }	// the battle has started: the note that it had not is stale
	if (!gOpen && !(gNote[0] && PortPresentCount() < gNoteUntil)) return;
	// the note that the battle has not started is drawn before the level can host the map, and the cockpit's notes while the Battle Engine is down
	if (!Allowed() && !(!gOpen && ((NotYet() && DrawingReady()) || NoteMay()))) { Close(); return; }
	Lay();
	BeginDraw();
	if (!gOpen)
	{
		// The note on the cockpit: the message box's panel and edge, above the HUD's message bar (whose top is about 89 units above the foot of the screen).
		const float w = TextWidth(gNote, L.s), pad = 8.f * L.s, x = floorf((L.w - w) * 0.5f), y = L.h - 89.f * L.s - LineHeight() - pad;
		const float edge = floorf(2.f * L.s) > 1.f ? floorf(2.f * L.s) : 1.f;
		Box(x - pad, y - pad, w + 2.f * pad, LineHeight() + 2.f * pad, kPanel);
		Outline(x - pad, y - pad, w + 2.f * pad, LineHeight() + 2.f * pad, edge, kEdge);
		Text(x, y, 0xffffffff, gNote);
		EndDraw();
		return;
	}
#ifdef CE_COMMANDER_TEST
	const std::uint64_t began = Ns();
#endif
	Refresh();
	// CE_MAP_PAN_BEGIN
	PanMapFrame();
	// CE_MAP_PAN_END
	gHover = OnMap(float(MOUSE_X), float(MOUSE_Y)) ? PickAt(float(MOUSE_X), float(MOUSE_Y)) : -1;
	if (!gLayoutLogged) { LogLayout(); gLayoutLogged = true; }
	if (gTerrainDirty) BuildTerrain();
	Box(0.f, 0.f, L.w, L.h, 0xfa0a1018);
	char title[64];
	snprintf(title, sizeof(title), "Commander   Paused");
	Text(12.f * L.s, 8.f * L.s, kText, title, L.w * 0.42f);
	DrawTerrain();
	DrawGrid();
	DrawSquads();
	DrawPanel();
	DrawLegend();
	DrawHints();
	DrawNote();
	DrawCursor();
	EndDraw();
#ifdef CE_COMMANDER_TEST
	const double took = SecondsBetween(began, Ns()) * 1000.0;
	++gFrames;
	gMsTotal += took;
	if (took > gMsMax) gMsMax = took;
#endif
}

bool CeCmdUnitOrdersEligible() {
    return Eligible() && GAME.GetGameState() == GAME_STATE_PLAYING;
}

#endif
