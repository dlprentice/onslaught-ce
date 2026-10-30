// SPDX-License-Identifier: GPL-3.0-only
// Commander mode, the map: an overlay for single-player levels. The M key opens it: the battle pauses (the game's own pause, without the pause menu),
// the mouse is released as the pause menu releases it, the keyboard and the mouse buttons go to this file through the game's key trap, and the
// pilot's controls are parked on a sink until the map closes. The map shows the level from above, the player's squads as boxes, the enemy squads as red
// marks (every enemy squad with a unit the game reports as detectable: that is most units, with no range, unlike the HUD radar, which stops at about 220 world units) and the pilot's engine; the player picks a squad with a click or Tab and gives it one of the orders of the order layer
// (ce_commander.h):
//   left click           pick the squad under the pointer (a click on nothing picks none)
//   Tab, Shift+Tab       the next and the previous squad
//   right click          Move there (a passive march: the squad holds fire on the way and once there, until its next order)
//   A, then a click      Advance there (a march that fights on the way); Esc cancels
//   H, X, R              Hold (X too: a held squad fights), Fall back
//   wheel, Q, E          zoom out and in (the wheel about the pointer); the arrow keys pan; Space centres on the pilot, C on the picked squad, Home on the action
//                        (the view is kept from one opening to the next within a level)
//   M, Esc               close (Esc first cancels an armed order, then drops the pick)
// Orders go through CeCmdIssue, the order layer of ce_commander.cpp, which applies them to the squads the way the mission scripts do.
//
// Never in faithful play: off where the CE layer is off, in a replay, a recording, under probes or the virtual clocks, for a test pilot or a
// network game, in multiplayer, demos and the front end, and unless ce.ini says commander=1 (a test build's proof switch, BEA_PORT_COMMANDER_PROOF=1
// with BEA_PORT_COMMANDER_MAP=1 and BEA_PORT_CE_LAYER=1, lets it run under a virtual clock: ce_commander.cpp). It draws only with the game's own
// sprite and font calls, allocates nothing but the controller stack entry the pause menu also makes, and draws no random number. Squads that follow
// mission scripts, aircraft and objectives are drawn but take no orders.
#ifdef CE_VERSION

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
#include "unit.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "FastVB.h"
#include "renderinfo.h"
#include "Texture.h"
#include "ce_commander.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef CE_COMMANDER_TEST
#include <time.h>
#endif

#if !defined(__clang__) && !defined(__GNUC__)
#error The commander map requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" unsigned PortPresentCount(void);		// d3d9hooks.cpp
bool PortCeLayer();
bool PortRecording();

namespace
{
const unsigned kOpenKey = DIK_M;
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
int gSelected = -1;		// the picked squad's ordinal
int gHover = -1;
CeCmdSquad gSquads[kMaxSquads];
int gCount;
char gNote[112];
unsigned gNoteUntil;
bool gNoteNotYet;			// the note on show is "the battle has not started yet": it goes when the battle does
bool gLayoutLogged;
int gUntracked;				// squads the order layer's table had no room for, or this one: not shown, not commandable
#ifdef CE_COMMANDER_TEST
unsigned gFrames;			// frames drawn while open, and the milliseconds they took the processor to issue (diagnostics, never read by the game)
double gMsTotal, gMsMax;

double Ms()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return double(t.tv_sec) * 1000.0 + double(t.tv_nsec) * 1e-6;
}
#endif

// The world: the occupied cells, and the square window of it the map shows (it opens on the action and zooms and pans).
float gBoundsX0, gBoundsY0, gBoundsX1, gBoundsY1;
float gViewX, gViewY, gViewSpan;
bool gTerrainDirty;
int gKeptLevel = -1;			// the level the kept view belongs to
float gKeptCentreX, gKeptCentreY, gKeptSpan;
DWORD gTerrain[kGrid * kGrid];

struct Layout
{
	float w, h, s;				// the window and the interface scale (the window against 640 x 480)
	float mapX, mapY, mapSide;	// the map's square, in window pixels
	float k;					// pixels a world unit takes
	float panelX, panelW;
} L;

CDXBitmapFont *gFont;
CDXTexture *gWhite;

// ---- the gate ----------------------------------------------------------------------------------------------------------------------

// Where the map may exist at all: the feature is on, and this is a single-player level of the player's own game (not a recording, a demo, the front end or a network game).
bool Eligible()
{
	if (!CeCmdMapOn() || !PortCeLayer() || PortRecording()) return false;
	if (CE_BootRequested && CE_BootRequested()) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo || CLIPARAMS.mStressTest ||
		CLIPARAMS.mAttractMode || FRONTEND.IsRunning()) return false;
	return !GAME.IsMultiplayer() && GAME.GetNPlayers() == 1;
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

void Refresh()
{
	gCount = CeCmdList(gSquads, kMaxSquads);
	gUntracked = CeCmdUntracked();
	const CeCmdSquad *picked = ByOrdinal(gSelected);
	if (!picked || !picked->commandable) gSelected = -1;
}

int CommandableCount()
{
	int n = 0;
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].commandable) ++n;
	return n;
}

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
		if (gSquads[i].commandable) { gSelected = gSquads[i].ordinal; CeCmdNote("map select %d", gSelected); return; }
	}
}

// What the map says when a click lands on a friendly squad that takes no orders.
const char *WhyText(int why)
{
	switch (why)
	{
	case CE_WHY_SCRIPTED: return "That squad follows a mission script.";
	case CE_WHY_SPAWNED: return "That squad follows its lead unit.";
	case CE_WHY_AIRCRAFT: return "Only ground squads take orders from the map.";
	case CE_WHY_OBJECTIVE: return "That squad is a mission objective.";
	default: return "That squad takes no orders from the map.";
	}
}

const char *OrderName(const CeCmdSquad &s)
{
	switch (s.verb)
	{
	case CE_CMD_MOVE: return s.arrived ? "Moved" : "Moving";
	case CE_CMD_ADVANCE: return s.arrived ? "Advanced" : "Advancing";
	case CE_CMD_HOLD: return "Holding";
	case CE_CMD_FALLBACK: return s.arrived ? "Fell back" : "Falling back";
	default: return "No order";
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
void SetView(float centreX, float centreY, float span)
{
	const float full = FullSpan();
	if (span > full) span = full;
	if (span < 48.f) span = 48.f;
	const float midX = (gBoundsX0 + gBoundsX1) * 0.5f, midY = (gBoundsY0 + gBoundsY1) * 0.5f, slack = (full - span) * 0.5f;
	if (centreX < midX - slack) centreX = midX - slack;
	if (centreX > midX + slack) centreX = midX + slack;
	if (centreY < midY - slack) centreY = midY - slack;
	if (centreY > midY + slack) centreY = midY + slack;
	gViewSpan = span;
	gViewX = centreX - span * 0.5f;
	gViewY = centreY - span * 0.5f;
	gTerrainDirty = true;
}

// The window on the action: the player's squads, the enemy squads within 80 units of them, and the pilot when he is that
// near, with a margin, never tighter than 128 units.
void FitView()
{
	float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
	auto add = [&](float x, float y) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; };
	for (int i = 0; i < gCount; ++i)
		if (gSquads[i].friendly) add(gSquads[i].x, gSquads[i].y);
	if (x1 < x0)
	{
		for (int i = 0; i < gCount; ++i)
			if (gSquads[i].visible) add(gSquads[i].x, gSquads[i].y);
	}
	else
	{
		const float ax0 = x0 - 80.f, ay0 = y0 - 80.f, ax1 = x1 + 80.f, ay1 = y1 + 80.f;
		for (int i = 0; i < gCount; ++i)
			if (!gSquads[i].friendly && gSquads[i].visible && gSquads[i].x >= ax0 && gSquads[i].x <= ax1 && gSquads[i].y >= ay0 && gSquads[i].y <= ay1)
				add(gSquads[i].x, gSquads[i].y);
		if (CBattleEngine *engine = GAME.GetPlayer(0) ? GAME.GetPlayer(0)->GetBattleEngine() : NULL)
		{
			const float ex = engine->GetPos().X, ey = engine->GetPos().Y;
			if (ex >= ax0 && ex <= ax1 && ey >= ay0 && ey <= ay1) add(ex, ey);
		}
	}
	if (x1 < x0) { SetView((gBoundsX0 + gBoundsX1) * 0.5f, (gBoundsY0 + gBoundsY1) * 0.5f, FullSpan()); return; }
	float span = ((x1 - x0) > (y1 - y0) ? (x1 - x0) : (y1 - y0)) * 1.35f + 24.f;
	if (span < 128.f) span = 128.f;
	SetView((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, span);
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
		out |= DWORD(ca + (cb - ca) * t + 0.5f) << shift;
	}
	return out;
}

DWORD Shade(DWORD c, float f)
{
	DWORD out = 0xff000000;
	for (int shift = 0; shift <= 16; shift += 8)
	{
		float v = float((c >> shift) & 255) * f;
		if (v > 255.f) v = 255.f;
		out |= DWORD(v) << shift;
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
void Text(float x, float y, DWORD colour, const char *text, float maxWidth = 0.f, bool centred = false)
{
	WCHAR w[160];
	Convert(text, w, 160);
	SIZE extent;
	gFont->GetTextExtent(w, &extent);
	float scale = L.s;
	if (maxWidth > 0.f && float(extent.cx) * scale > maxWidth && extent.cx > 0) scale = maxWidth / float(extent.cx);
	if (centred) x -= float(extent.cx) * scale * 0.5f;
	gFont->DrawTextWithShadow(x, y, colour, w, 0, 0.f, scale, scale);
}

float LineHeight() { return float(gFont->GetHeight()) * L.s; }

void BeginDraw()
{
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
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
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
}

const DWORD kFriend = 0xff62d862, kFriendDim = 0xff3f7a3f, kEnemy = 0xffe45656, kPilot = 0xff70e0ff, kPick = 0xfffff070, kText = 0xffeef2f6,
			kQuiet = 0xff9aa6b2, kMoveColour = 0xff58f0c0, kAdvanceColour = 0xffffa23c, kFallbackColour = 0xffffe466;

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

// One batch through the game's fast vertex buffer, as the compass draws its markers (the buffer flushes itself when it fills).
void DrawTerrain()
{
	const float cell = L.mapSide / float(kGrid);
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

void DrawSquads()
{
	const float t = floorf(L.s) > 1.f ? floorf(L.s) : 1.f;
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
		if (s.commandable) Box(x - h, y - h, 2.f * h, 2.f * h, Shade(colour, 0.55f));
		Outline(x - h, y - h, 2.f * h, 2.f * h, 2.f * t, colour);
		if (const char *letter = OrderLetter(s.verb)) Text(x, y - LineHeight() * 0.5f, OrderColour(s.verb), letter, 0.f, true);
		else if (s.vehicles) Box(x - h * 0.45f, y - t, h * 0.9f, 2.f * t, colour);	// a bar across a vehicle squad, a dot for infantry
		else Box(x - t, y - t, 2.f * t, 2.f * t, colour);
		if (s.ordinal == gSelected) Outline(x - h - 3.f * t, y - h - 3.f * t, 2.f * (h + 3.f * t), 2.f * (h + 3.f * t), 2.f * t, kPick);
		else if (s.ordinal == gHover) Outline(x - h - 2.f * t, y - h - 2.f * t, 2.f * (h + 2.f * t), 2.f * (h + 2.f * t), t, 0xffffffff);
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
	Box(x, y, L.panelW, L.mapSide, 0xb0101820);
	Outline(x, y, L.panelW, L.mapSide, floorf(L.s) > 1.f ? floorf(L.s) : 1.f, 0xff3a4654);
	y += pad;
	const float room = L.panelW - 2.f * pad;
	const CeCmdSquad *s = ByOrdinal(gSelected);
	if (!s)
	{
		Text(x + pad, y, kText, "No squad picked", room);
		y += line * 1.5f;
		char text[64];
		snprintf(text, sizeof(text), "%d squads take orders", CommandableCount());
		Text(x + pad, y, kQuiet, text, room);
		y += line * 1.5f;
		Text(x + pad, y, kQuiet, "Click a box to pick it,", room);
		y += line;
		Text(x + pad, y, kQuiet, "or press Tab.", room);
		y += line * 2.f;
		if (const CeCmdSquad *h = ByOrdinal(gHover))
		{
			snprintf(text, sizeof(text), "Pointing at squad %d", h->ordinal);
			Text(x + pad, y, kText, text, room);
			y += line;
			if (h->unit[0])
			{
				Text(x + pad, y, kQuiet, h->unit, room);
				y += line;
			}
			snprintf(text, sizeof(text), "%d unit%s%s", h->members, h->members == 1 ? "" : "s", h->commandable ? "" : ", no orders");
			Text(x + pad, y, h->commandable ? kQuiet : kEnemy, text, room);
		}
		return;
	}
	char text[96];
	snprintf(text, sizeof(text), "Squad %d", s->ordinal);
	Text(x + pad, y, kPick, text, room);
	y += line * 1.4f;
	if (s->unit[0])
	{
		Text(x + pad, y, kText, s->unit, room);
		y += line;
	}
	if (s->peak > s->members) snprintf(text, sizeof(text), "%s, %d of %d units", s->vehicles ? "Vehicles" : "Infantry", s->members, s->peak);
	else snprintf(text, sizeof(text), "%s, %d unit%s", s->vehicles ? "Vehicles" : "Infantry", s->members, s->members == 1 ? "" : "s");
	Text(x + pad, y, kText, text, room);
	y += line * 1.4f;
	if (s->deploys)
	{
		Text(x + pad, y, kQuiet, "Deploys: hold only", room);
		y += line * 1.2f;
	}
	Text(x + pad, y, kQuiet, "Order", room);
	y += line;
	Text(x + pad, y, s->verb ? OrderColour(s->verb) : kText, OrderName(*s), room);
	y += line;
	if (s->verb == CE_CMD_MOVE || s->verb == CE_CMD_ADVANCE || s->verb == CE_CMD_FALLBACK)
	{
		snprintf(text, sizeof(text), "to %.0f, %.0f", s->gx, s->gy);
		Text(x + pad, y, kQuiet, text, room);
		y += line;
	}
	if (s->verb == CE_CMD_MOVE || s->verb == CE_CMD_FALLBACK)
	{
		Text(x + pad, y, kQuiet, "Passive: it holds fire", room);
		y += line;
		Text(x + pad, y, kQuiet, "until the next order", room);
		y += line;
	}
	y += line * 0.5f;
	Text(x + pad, y, kQuiet, "Position", room);
	y += line;
	snprintf(text, sizeof(text), "%.0f, %.0f", s->x, s->y);
	Text(x + pad, y, kText, text, room);
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
	Text(x, y, kQuiet, "marks on a box, not keys:", room);
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
	Text(12.f * L.s, y, kQuiet, "Left click: pick   Right click: move, passive   A, then click: advance, fights   H, X: hold, fires   R: fall back, passive", room);
	Text(12.f * L.s, y + line, kQuiet, "Tab: next squad   Wheel, Q, E, arrows: zoom, pan   Space: to you   C: to squad   Home: all   Esc: cancel, close   M: close", room);
}

// The last thing the map told the player, in the title bar, to the right.
void DrawNote()
{
	if (!gNote[0] || PortPresentCount() >= gNoteUntil) return;
	const float room = L.w * 0.5f, w = TextWidth(gNote, L.s) < room ? TextWidth(gNote, L.s) : room;		// half the width: the title bar's text ends at 43 percent of it at 640 x 480
	Text(L.w - 12.f * L.s - w, 8.f * L.s, kPick, gNote, room);
}

void DrawCursor()
{
	const float t = floorf(L.s) > 1.f ? floorf(L.s) : 1.f, a = 7.f * L.s, x = float(MOUSE_X), y = float(MOUSE_Y);
	Box(x - a - t, y - t * 1.5f, 2.f * a + 2.f * t, t * 3.f, 0xff000000);
	Box(x - t * 1.5f, y - a - t, t * 3.f, 2.f * a + 2.f * t, 0xff000000);
	Box(x - a, y - t * 0.5f, 2.f * a, t, gArmed ? kAdvanceColour : 0xffffffff);
	Box(x - t * 0.5f, y - a, t, 2.f * a, gArmed ? kAdvanceColour : 0xffffffff);
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

void Order(int verb, float wx, float wy)
{
	const CeCmdSquad *s = ByOrdinal(gSelected);
	if (!s) { Note("Pick a squad first."); return; }
	const char *why = NULL;
	if (!CeCmdIssue(gSelected, verb, wx, wy, &why)) { Note(why ? why : "Refused."); CeCmdNote("map refused %d %d %s", gSelected, verb, why ? why : "-"); return; }
	CeCmdNote("map order %d %d %.1f %.1f", gSelected, verb, wx, wy);
	Refresh();
	static const char *const word[] = {"", "Moving", "Advancing", "Holding", "Falling back"};
	char text[64];
	// a squad that is moved or falls back holds fire on the way and once it has arrived, until it is given another order (the panel says so in full; the note is short so that it clears the title)
	snprintf(text, sizeof(text), "Squad %d: %s%s.", gSelected, word[verb], (verb == CE_CMD_MOVE || verb == CE_CMD_FALLBACK) ? ", passive" : "");
	Note(text);
}

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

void Close()
{
	if (!gOpen) return;
	gOpen = false;
	gKeptLevel = GAME.GetCurrentLevel();
	gKeptCentreX = gViewX + gViewSpan * 0.5f;
	gKeptCentreY = gViewY + gViewSpan * 0.5f;
	gKeptSpan = gViewSpan;
	gArmed = false;
	if (LT.GetKeytrap() == (pKeyTrapper)Trap) LT.SetKeytrap(NULL);
	if (CCONTROLLER *controller = GAME.GetController(0))
		if (controller->GetToControl() == &gSink) controller->RelinquishControl();
	GAME.*GameAccess::PauseField() = FALSE;
	InitMouse();
#ifdef CE_COMMANDER_TEST
	{
		CeCmdHash h;
		CeCmdWorldHash(&h);
		CeCmdNote("map close world %08x solid %08x things %u components %u time %08x frames %u avg %.2f ms max %.2f ms", h.all, h.withoutComponents,
				  h.things, h.components, h.eventTime, gFrames, gFrames ? gMsTotal / double(gFrames) : 0.0, gMsMax);
		DumpWorld("close");
	}
#endif
}

void Open()
{
	if (GAME.IsPaused()) return;
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
	GAME.Pause();
	if (!GAME.IsPaused()) return;
	gOpening = true;
	ShutdownMouse();		// the pointer it starts from stays in window pixels, which the map reads (PortCeCmdMapOpen)
	gOpening = false;
	GAME.GetController(0)->SetToControl(&gSink);
	LT.SetKeytrap((pKeyTrapper)Trap);
	// A key that is already held when the map opens is not a new press when it repeats (the window layer posts every auto-repeat as a key-down): the game's own
	// key state knows which ones those are, because it is not updated while a key trap is installed but cleared by every key-up.
	for (int k = 0; k < 256; ++k) gDown[k] = LT.KeyDown[k] != 0;
	gDown[kOpenKey] = true;		// the press that opened the map may repeat; its release clears this
	gShift = false;
	gArmed = false;
	gOpen = true;
	gLayoutLogged = false;
#ifdef CE_COMMANDER_TEST
	gFrames = 0;
	gMsTotal = gMsMax = 0.0;
#endif
	gNote[0] = 0;
	World();
	if (gKeptLevel == GAME.GetCurrentLevel()) SetView(gKeptCentreX, gKeptCentreY, gKeptSpan);
	else FitView();
	Lay();		// the layout exists before the first frame draws it: a zoom or a pan in the same batch of messages as the open key works on it
#ifdef CE_COMMANDER_TEST
	{
		CeCmdHash h;
		CeCmdWorldHash(&h);
		CeCmdNote("map open world %08x solid %08x things %u components %u time %08x", h.all, h.withoutComponents, h.things, h.components, h.eventTime);
		DumpWorld("open");
	}
#endif
}

// Keys and mouse buttons while the map is open. The mouse module reads a trap's return value (true: the button was taken).
bool Trap(WORD key, int event)
{
	if (!gOpen || LT.GetKeytrap() != (pKeyTrapper)Trap) return false;
	if (event == kMouseDown && key <= 2)
	{
		const float px = float(MOUSE_X), py = float(MOUSE_Y);
		if (key == 0 && gArmed && OnMap(px, py)) { gArmed = false; Order(CE_CMD_ADVANCE, WorldX(px), WorldY(py)); }
		else if (key == 0)
		{
			const int hit = PickAt(px, py);
			const CeCmdSquad *s = ByOrdinal(hit);
			if (s && !s->commandable) Note(WhyText(s->why));
			else if (s) { gSelected = hit; CeCmdNote("map select %d", hit); }
			else if (OnMap(px, py)) { if (gSelected >= 0) CeCmdNote("map select -1"); gSelected = -1; }
		}
		else if (key == 2 && OnMap(px, py))
		{
			const bool advance = gArmed;
			gArmed = false;
			Order(advance ? CE_CMD_ADVANCE : CE_CMD_MOVE, WorldX(px), WorldY(py));
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
	switch (key)
	{
	case DIK_M: Close(); break;
	case DIK_ESCAPE:
		if (gArmed) { gArmed = false; Note("Advance cancelled."); }
		else if (gSelected >= 0) { gSelected = -1; CeCmdNote("map select -1"); }
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
		if (ByOrdinal(gSelected)) { gArmed = true; Note("Advance: click where the squad is to go."); }
		else Note("Pick a squad first.");
		break;
	case DIK_H: Order(CE_CMD_HOLD, 0.f, 0.f); break;
	case DIK_X: Order(CE_CMD_HOLD, 0.f, 0.f); break;
	case DIK_R: Order(CE_CMD_FALLBACK, 0.f, 0.f); break;
	default: break;
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
}

// Whether the map is open (or is opening: the pointer ShutdownMouse starts it from is a window pixel too): while it is, the pointer stays in window pixels (the map reads them), not the virtual screen's that
// the game's in-game menus use (widescreen.cpp).
bool PortCeCmdMapOpen() { return gOpen || gOpening; }

// ---- the hooks (patch 0162) ----------------------------------------------------------------------------------------------------------

// The end of the CE key chain in the window procedure (ltshell.cpp), after photo mode's and the console's keys and before the key trap: the open key, in a level
// where the map may open; it stands aside for any key trap that is installed (the console typing, the pause menu).
bool PortCeCmdKey(unsigned key, bool repeat)
{
	if (key == DIK_ESCAPE && gEscClosed)	// the press that closed the map was the map's; its auto-repeats are too, until a fresh press (the key-down message's repeat flag is clear)
	{
		if (!repeat) gEscClosed = false;
		else if (!LT.GetKeytrap()) return true;
	}
	if (key != kOpenKey || LT.GetKeytrap() || !CeCmdMapOn()) return false;
	if (NotYet())
	{
		if (!repeat) { Note("The battle has not started yet."); gNoteNotYet = true; }
		return true;
	}
	if (!Allowed()) return false;
	if (repeat) return true;
	Open();
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

// Between frames (CGame::MainLoop): the map closes where it is no longer allowed.
void PortCeCmdFrame()
{
	if (gOpen && (!Allowed() || LT.GetKeytrap() != (pKeyTrapper)Trap)) Close();
#ifdef CE_COMMANDER_TEST
	TestFrame();
#endif
}

// The level is ending (CGame::ShutdownRestartLoop): put back what opening changed.
void PortCeCmdLevelEnd()
{
	Close();
	gKeptLevel = -1;
	gSelected = -1;
	gNote[0] = 0;
}

// After the console and the photo note (CDXEngine::PostRender): the map when it is open, else the note that says why it did not open.
void PortCeCmdRender()
{
	if (gNoteNotYet && !NotYet()) { gNote[0] = 0; gNoteNotYet = false; }	// the battle has started: the note that it had not is stale
	if (!gOpen && !(gNote[0] && PortPresentCount() < gNoteUntil)) return;
	if (!Allowed() && !(!gOpen && NotYet() && DrawingReady())) { Close(); return; }	// the note that the battle has not started is drawn before the level can host the map
	Lay();
	BeginDraw();
	if (!gOpen)
	{
		const float w = TextWidth(gNote, L.s), pad = 6.f * L.s, x = (L.w - w) * 0.5f, y = L.h - 40.f * L.s - LineHeight();
		Box(x - pad, y - pad, w + 2.f * pad, LineHeight() + 2.f * pad, 0xb4000000);
		Text(x, y, 0xffffffff, gNote);
		EndDraw();
		return;
	}
#ifdef CE_COMMANDER_TEST
	const double began = Ms();
#endif
	Refresh();
	if (gDown[DIK_LEFT] || gDown[DIK_RIGHT] || gDown[DIK_UP] || gDown[DIK_DOWN])	// held arrows pan a fraction of the view a frame
		Pan(((gDown[DIK_RIGHT] ? 1.f : 0.f) - (gDown[DIK_LEFT] ? 1.f : 0.f)) * 0.012f, ((gDown[DIK_DOWN] ? 1.f : 0.f) - (gDown[DIK_UP] ? 1.f : 0.f)) * 0.012f);
	gHover = OnMap(float(MOUSE_X), float(MOUSE_Y)) ? PickAt(float(MOUSE_X), float(MOUSE_Y)) : -1;
	if (!gLayoutLogged) { LogLayout(); gLayoutLogged = true; }
	if (gTerrainDirty) BuildTerrain();
	Box(0.f, 0.f, L.w, L.h, 0xfa0a1018);
	char title[64];
	snprintf(title, sizeof(title), "COMMANDER    battle paused    level %d", GAME.GetCurrentLevel());
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
	const double took = Ms() - began;
	++gFrames;
	gMsTotal += took;
	if (took > gMsMax) gMsMax = took;
#endif
}

#endif
