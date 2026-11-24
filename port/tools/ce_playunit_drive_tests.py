#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Play as a unit, the production wiring of the look's mouse and of the direct law (port/game/ce_playunit.cpp, port/platform/common/dinput.cpp): compiled with fakes.

Three pieces are taken from the sources as they stand and compiled against fake game objects:
- the look's mouse (PortCePlayUnitMouse, between its "(begin)" and "(end)" lines): the mouse turns and tilts the look only while a unit is driven with the look
  camera, the level plays and the game is not paused; right turns right, down tilts down, and the player's Invert Y turns that around;
- the call in dinput.cpp's mouse poll that feeds it: the movement the game is given, with its signs, after the feel script's movement is added;
- the direct law's guide (CeDriveGuide, InstallDriveGuide, RemoveDriveGuide, between its "(begin)" and "(end)" lines): a driven tank's guide while it is held. It
  keeps the unit's own guide aside, turns the unit toward the heading the keys ask for and sets the velocity target along its facing (forward, reverse, steering
  while moving), does not drive into a cell its grid closes, sets every order aside, puts the unit's own guide back stopped at the release, and when the unit is
  shut down with it inside it deletes the kept guide once.
Causal controls change the production text (the pause guard dropped, the tilt inverted, Invert Y ignored, the poll's movement negated, the grid guard dropped, the
velocity along the world's axis, an order passed on, the kept guide not put back or not deleted) and each must make the fixture fail. How it feels to drive is the
test build's probe ("V" lines: speed, heading, the share asked) in an offscreen run, not this file.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
MODULE = PORT / "game/ce_playunit.cpp"
DINPUT = PORT / "platform/common/dinput.cpp"
MOUSE = ("// ---- the look's mouse (begin)", "// ---- the look's mouse (end)")
GUIDE = ("// ---- the direct law's guide (begin)", "// ---- the direct law's guide (end)")
HOOK = "if (PortCePlayUnitMouse) PortCePlayUnitMouse(m->lX, m->lY);"


def region(text, marks):
    start = text.index(marks[0])
    if marks == MOUSE:
        law = region(text, ("CePlayUnitLookLaw UnitLookLaw()", "\nconst char *UnitViewName"))
        return law + "\n" + text[start:text.index(marks[1], start)]
    return text[start:text.index(marks[1], start)]


def poll_hook():
    """The play-unit call in dinput.cpp's mouse poll, and whether it comes after the feel script's movement is added and before the game's own use of it."""
    text = DINPUT.read_text()
    calls = re.findall(r"if \(PortCePlayUnitMouse\) PortCePlayUnitMouse\([^;]*\);", text)
    if len(calls) != 1:
        raise AssertionError("dinput.cpp has %d play-unit calls, not one" % len(calls))
    at = text.index(calls[0])
    added = text.rfind("m->lY += ay;", 0, at)
    ordered = added != -1 and at - added < 1200 and text.rfind("PortFeelMouseTake(&ax, &ay);", 0, at) != -1
    return calls[0], ordered


DRIVER = r'''
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <new>
#include "ce_playunit_rules.h"
#include "ce_playunit_look.h"
#include "ce_playunit_infantry.h"
typedef int BOOL;
#define FALSE 0
#define TRUE 1
enum { MEMTYPE_GUIDE = 7 };
void *operator new(size_t n, int) { return ::operator new(n); }
struct FVector { float X, Y, Z; FVector(float x = 0, float y = 0, float z = 0): X(x), Y(y), Z(z) {}
    FVector operator+(const FVector &o) const { return FVector(X + o.X, Y + o.Y, Z + o.Z); }
    FVector operator-(const FVector &o) const { return FVector(X - o.X, Y - o.Y, Z - o.Z); } };
// the unit's matrix: its yaw alone (forward (-sin, cos, 0) as the game's yaw turns (0, 1, 0)); the fixture can make it the world's to test that the velocity is the unit's
bool gWorldMatrix;
struct FMatrix { float yaw; FVector operator*(const FVector &v) const { float y = gWorldMatrix ? 0.f : yaw; return FVector(v.X * cosf(y) - v.Y * sinf(y), v.X * sinf(y) + v.Y * cosf(y), v.Z); } };
struct CEulerAngles { float mYaw = 0, mPitch = 0, mRoll = 0; };
struct CNormalSquadUnknownD4 { int unused; };
struct COccupancyGrid { float closedFromY = 1e9f, closedBelowY = -1e9f;
    BOOL UnknownCell(const FVector &p) { return (p.Y < closedFromY && p.Y > closedBelowY) ? TRUE : FALSE; } };
class CUnit;
class CGuide {
public:
    CGuide(CUnit *unit);
    virtual ~CGuide() {}
    virtual void Unknown3() = 0;
    virtual void GoToPoint(FVector point, BOOL) { mMode = 1; mDestination = point; }
    virtual void Unknown5(FVector v) { mMode = 2; mDestination = v; }
    virtual void Unknown6(FVector v) { mMode = 3; mDestination = v; }
    virtual void Unknown7(FVector v) { mMode = 3; mDestination = v; }
    virtual void Stop();
    virtual CNormalSquadUnknownD4 *GetPath() { return nullptr; }
    CUnit *GetUnit() { return mUnit; }
    FVector mDestination; CUnit *mUnit; int mMode;
};
int gStockOrders, gStockStops, gStockSteps, gStockDeleted;
CNormalSquadUnknownD4 gStockPath;
class StockGuide : public CGuide {
public:
    StockGuide(CUnit *u): CGuide(u) {}
    ~StockGuide() { ++gStockDeleted; }
    void Unknown3() { ++gStockSteps; }
    void GoToPoint(FVector p, BOOL o) { ++gStockOrders; CGuide::GoToPoint(p, o); }
    void Unknown7(FVector v) { ++gStockOrders; CGuide::Unknown7(v); }
    void Stop() { ++gStockStops; CGuide::Stop(); }
    CNormalSquadUnknownD4 *GetPath() { return &gStockPath; }
};
enum { AI_OFF, AI_ON };
class CUnit {
public:
    int ai = AI_OFF, team = 0; void *squad = nullptr;
    int GetAIState() { return ai; }
    int GetAllegiance() { return team; }
    void *GetSquad() { return squad; }
    CGuide *mGuide = nullptr; FVector mPos = FVector(100, 100, 0), mUnknown14C; CEulerAngles mCurrentOrientation, mUnknown120;
    bool dying = false; COccupancyGrid grid; bool hasGrid = true; float top = 2.f; int stops = 0;
    bool IsDying() { return dying; }
    void Stop() { ++stops; mUnknown14C = FVector(); mUnknown120.mYaw = mCurrentOrientation.mYaw; if (mGuide) mGuide->Stop(); }
    void SetUnknown14C(const FVector &v) { mUnknown14C = v; }
    CEulerAngles &GetCurrentOrientation() { return mCurrentOrientation; }
    FVector GetPos() { return mPos; }
    COccupancyGrid *GetOccupancyGrid() { return hasGrid ? &grid : nullptr; }
    FMatrix GetOrientation() { FMatrix m; m.yaw = mCurrentOrientation.mYaw; return m; }
    float Unknown111() { return top; }
    // the mover's side: velocity, ground friction, the hull's turn rate a move, the move cadence (CGroundUnit::Move and CUnit::Unknown77, below in Move)
    FVector mVelocity; CEulerAngles mUnknown12C = MakeRate(0.0436f); float friction = 0.25f, mult = 1.f;
    static CEulerAngles MakeRate(float r) { CEulerAngles a; a.mYaw = r; return a; }
    FVector GetVelocity() { return mVelocity; }
    void SetVelocity(const FVector &v) { mVelocity = v; }
    bool onGround = true;
    BOOL IsOnGround() { return onGround; } BOOL IsInWater() { return FALSE; } BOOL ObeyGravity() { return TRUE; } BOOL Unknown76() { return TRUE; }
    float Unknown72() { return friction; }
    float GetMoveMultiplier() { return mult; }
    void GoToPoint(FVector p, BOOL o) { if (mGuide) mGuide->GoToPoint(p, o); }
    void GuideUnknown7(FVector v) { if (mGuide) mGuide->Unknown7(v); }
    void Shutdown() { if (mGuide) { delete mGuide; mGuide = nullptr; } }
};
CGuide::CGuide(CUnit *unit): mDestination(unit->GetPos()), mUnit(unit), mMode(0) {}
void CGuide::Stop() { mMode = 0; mDestination = mUnit->GetPos(); mUnit->mUnknown14C = FVector(); }

// the module's state the regions read
const CePlayUnitLaw *gLaw = &CePlayUnitClasses()[CE_PLAYUNIT_CLASS_TANK].law;
CePlayUnitDrive gApplied;
bool gOnFoot; extern float gLookYaw;
bool gDeliveryAssigned = false;
int gPossessedAllegiance = 0;
bool FootKeepsAssignment(CUnit *unit) { return unit->GetSquad() == nullptr; }
bool gPathDrive;
// the soldier's move cadence while driven (outside the region): the guide only calls it
void *gFootCadenceUnit; float gFootCadence; int gCadenceTakes, gCadenceReleases;
void FootCadenceTake(CUnit *) { ++gCadenceTakes; } void FootCadenceRelease(CUnit *) { ++gCadenceReleases; }
// the mouse's world
bool gDriving = true; void *gCamera = &gDriving; enum { kViewSmooth, kViewRigid, kViewStock, kViewLook }; int gViewMode = kViewLook;
int gDrivenClass = CE_PLAYUNIT_CLASS_TANK;
float gMouseSensitivity = 7.0f;
float gLookYaw, gLookPitch; unsigned gMouseQuiet = ~0u;
enum { GAME_STATE_PLAYING = 2, GAME_STATE_PAUSED_MENU = 3 };
struct Game { bool paused = false; bool freeCam = false; int state = GAME_STATE_PLAYING; bool IsPaused() { return paused; } bool IsFreeCameraOn(int) { return freeCam; } int GetGameState() { return state; } } GAME;
struct Career { bool invert = false; bool invertJet = false; BOOL GetInvertYAxis(int) { return invert ? TRUE : FALSE; } BOOL GetInvertJetYAxis(int) { return invertJet ? TRUE : FALSE; } } CAREER;
struct LtStub { bool keytrap = false; void *GetKeytrap() { return keytrap ? (void *)1 : nullptr; } } LT;
bool gCmdMapOpen = false;
bool PortCeCmdMapOpen() { return gCmdMapOpen; }

@MOUSE@
@GUIDE@

// the poll: the play-unit call as dinput.cpp holds it, over a fake mouse state
struct MouseState { long lX, lY; };
void Poll(long x, long y) { MouseState state = {x, y}; MouseState *m = &state; @HOOK@ }

static int checks, failed;
static void Check(bool good, const char *name) { ++checks; if (good) printf("ok %s\n", name); else { ++failed; printf("FAIL %s\n", name); } }
static bool Near(float a, float b, float e = 1e-4f) { return fabsf(a - b) <= e; }
static void LookAt(float y, float p) { gLookYaw = y; gLookPitch = p; gMouseQuiet = 99; }

static void Mouse()
{
    const CePlayUnitLookLaw &law = CePlayUnitTankLook();
    gDrivenClass = CE_PLAYUNIT_CLASS_TANK;
    gMouseSensitivity = 7.0f;
    LookAt(0.f, 0.f);
    PortCePlayUnitMouse(40, 0);
    Check(Near(gLookYaw, -40.f * law.perCount) && gMouseQuiet == 0, "mouse: right turns the look right, and the stick waits");
    LookAt(0.f, 0.f);
    PortCePlayUnitMouse(0, 30);
    Check(Near(gLookPitch, -30.f * law.perCount), "mouse: default Invert Y matches Battle Engine ground aim");
    LookAt(0.f, 0.f);
    CAREER.invert = true;
    PortCePlayUnitMouse(0, 30);
    CAREER.invert = false;
    Check(Near(gLookPitch, 30.f * law.perCount), "mouse: the player's Invert Y turns the tilt around");
    LookAt(0.f, 0.f);
    gDrivenClass = CE_PLAYUNIT_CLASS_AIRCRAFT;
    CAREER.invertJet = true;
    PortCePlayUnitMouse(0, 30);
    CAREER.invertJet = false;
    gDrivenClass = CE_PLAYUNIT_CLASS_TANK;
    Check(Near(gLookPitch, 30.f * law.perCount), "mouse: aircraft uses Invert Jet Y");
    LookAt(0.f, 0.f);
    gMouseSensitivity = 14.0f;
    PortCePlayUnitMouse(20, 0);
    gMouseSensitivity = 7.0f;
    Check(Near(gLookYaw, -40.f * law.perCount), "mouse: scales with gMouseSensitivity");
    LookAt(0.3f, 0.1f);
    LT.keytrap = true;
    PortCePlayUnitMouse(50, 50);
    LT.keytrap = false;
    Check(Near(gLookYaw, 0.3f) && Near(gLookPitch, 0.1f) && gMouseQuiet == 99, "mouse: nothing turns while a keytrap is active");
    GAME.freeCam = true;
    PortCePlayUnitMouse(50, 50);
    GAME.freeCam = false;
    Check(Near(gLookYaw, 0.3f) && Near(gLookPitch, 0.1f), "mouse: nothing turns while free camera is on");
    gCmdMapOpen = true;
    PortCePlayUnitMouse(50, 50);
    gCmdMapOpen = false;
    Check(Near(gLookYaw, 0.3f) && Near(gLookPitch, 0.1f), "mouse: nothing turns while Commander view is open");
    LookAt(0.3f, 0.1f);
    GAME.paused = true;
    PortCePlayUnitMouse(50, 50);
    GAME.paused = false;
    Check(Near(gLookYaw, 0.3f) && Near(gLookPitch, 0.1f) && gMouseQuiet == 99, "mouse: nothing turns while the game is paused");
    GAME.state = GAME_STATE_PAUSED_MENU;
    PortCePlayUnitMouse(50, 50);
    GAME.state = GAME_STATE_PLAYING;
    Check(Near(gLookYaw, 0.3f) && Near(gLookPitch, 0.1f), "mouse: nothing turns while the level is not playing");
    gDriving = false;
    PortCePlayUnitMouse(50, 50);
    gDriving = true;
    Check(Near(gLookYaw, 0.3f), "mouse: nothing turns while no unit is driven");
    gViewMode = kViewSmooth;
    PortCePlayUnitMouse(50, 50);
    gViewMode = kViewLook;
    Check(Near(gLookYaw, 0.3f), "mouse: nothing turns while the view is the game's own camera");
    PortCePlayUnitMouse(0, 0);
    Check(gMouseQuiet == 99, "mouse: a still mouse leaves the stick its turn");
    LookAt(0.f, 0.f);
    Poll(-20, 12);
    Check(Near(gLookYaw, 20.f * law.perCount) && Near(gLookPitch, -12.f * law.perCount), "poll: the look gets the movement the game is given, with its signs");
}

// One move of the game's ground mover and hull turn, as CGroundUnit::Move and CUnit::Unknown77 do them on the ground, then the guide's next answer.
static void Integrate(CUnit &u)
{
    const int steps = (int)u.mult;
    for (int i = 0; i < steps; ++i) { u.mVelocity.X = (u.mVelocity.X + 0.4f * u.mUnknown14C.X) * u.friction; u.mVelocity.Y = (u.mVelocity.Y + 0.4f * u.mUnknown14C.Y) * u.friction; }
    const float top = u.top / 20.f, s = hypotf(u.mVelocity.X, u.mVelocity.Y);
    if (s > top) { u.mVelocity.X *= top / s; u.mVelocity.Y *= top / s; }
}
static void Advance(CUnit &u)
{
    float gap = u.mUnknown120.mYaw - u.mCurrentOrientation.mYaw;
    float step = fabsf(gap) * u.mult * 0.1f;
    if (step > u.mult * u.mUnknown12C.mYaw) step = u.mult * u.mUnknown12C.mYaw;
    u.mCurrentOrientation.mYaw += gap < 0 ? -step : step;
    u.mPos = u.mPos + u.mVelocity;
}
// one tick as the game orders it: CGroundUnit::Move integrates, CUnit::Move asks the guide, then the actor moves and the hull turns
static void Move(CUnit &u) { Integrate(u); u.mGuide->Unknown3(); Advance(u); }
static float Along(CUnit &u) { const float y = u.mCurrentOrientation.mYaw; return -sinf(y) * u.mVelocity.X + cosf(y) * u.mVelocity.Y; }
// n moves under the keys; the speed along the facing and the hull's yaw change of each
static void Drive(CUnit &u, int n, float *speed, float *turn)
{
    for (int i = 0; i < n; ++i) { const float before = u.mCurrentOrientation.mYaw; Move(u); speed[i] = Along(u); turn[i] = u.mCurrentOrientation.mYaw - before; }
}

static StockGuide *Fresh(CUnit *u) { StockGuide *s = new StockGuide(u); u->mGuide = s; gStockOrders = gStockStops = gStockSteps = gStockDeleted = 0; return s; }
static void Keys(float f, float b, float t) { memset(&gApplied, 0, sizeof(gApplied)); gApplied.forward = f; gApplied.back = b; gApplied.turn = t; }

static void Guide()
{
    const CePlayUnitLaw &law = *gLaw;
    CUnit u;
    u.mCurrentOrientation.mYaw = 0.f;
    StockGuide *stock = Fresh(&u);
    Check(InstallDriveGuide(&u) && u.mGuide != stock && gDriveGuide == u.mGuide && gDriveGuide->mStock == stock, "guide: the drive guide goes in, the unit's own kept");
    Check(!InstallDriveGuide(&u) && gDriveGuide->mStock == stock, "guide: a second install changes nothing");
    const float clamp = u.top / 20.f, top = CePlayUnitMoverCruise(clamp * 4.f, u.friction, clamp), rate = u.mUnknown12C.mYaw;
    Check(top < clamp && Near(top, clamp * 1.6f / 3.f), "guide: the tank cruises at the class's own speed under the stock request, below the mover's clamp");
    float sp[40], tr[40];
    Keys(1, 0, 0);
    Drive(u, 12, sp, tr);
    Check(gStockSteps == 0 && Near(u.mCurrentOrientation.mYaw, 0.f) && fabsf(u.mVelocity.X) < 1e-5f, "guide: W drives straight on along the facing; the kept guide does not steer");
    Check(sp[0] > 0.5f * top && sp[0] < 0.7f * top && sp[2] > 0.9f * top && Near(sp[11], top, 1e-3f * top), "guide: W eases in: over half the top speed after one move, nine tenths after three, all of it after twelve");
    bool rising = true;
    for (int i = 1; i < 12; ++i) rising = rising && sp[i] >= sp[i - 1] - 1e-6f && sp[i] <= top + 1e-6f;
    Check(rising, "guide: the speed rises without overshoot");
    Keys(0, 0, 0);
    Drive(u, 30, sp, tr);
    Check(sp[0] > 0.6f * top && sp[0] < 0.8f * top && sp[4] > 0.1f * top && sp[29] == 0.f && Near(u.mUnknown14C.Y, 0.f), "guide: letting go of W glides to a stop: seven tenths kept a move, standing still at the end, the request none");
    bool gliding = true;
    for (int i = 1; i < 30; ++i) gliding = gliding && sp[i] <= sp[i - 1] + 1e-6f && sp[i] >= 0.f;
    Check(gliding, "guide: the glide never backs up");
    u.mCurrentOrientation.mYaw = 1.5707963f; u.mUnknown120.mYaw = u.mCurrentOrientation.mYaw;
    Keys(1, 0, 0);
    Drive(u, 12, sp, tr);
    Check(Near(u.mVelocity.X, -top, 1e-3f) && Near(u.mVelocity.Y, 0.f, 1e-3f), "guide: the velocity follows the unit's facing");
    Keys(0, 0, 0);
    Drive(u, 30, sp, tr);
    u.mCurrentOrientation.mYaw = 0.f; u.mUnknown120.mYaw = 0.f;
    Keys(0, 1, 0);
    Drive(u, 14, sp, tr);
    Check(sp[0] < 0.f && Near(sp[13], -top * law.reverseShare, 1e-3f * top), "guide: S backs up, easing to its share of the top speed");
    Keys(1, 0, 0);
    Drive(u, 14, sp, tr);
    Check(sp[0] < 0.f && sp[0] > -top * law.reverseShare && sp[13] > 0.9f * top, "guide: W after S brakes through zero, then drives forward");
    Keys(0, 0, 0);
    Drive(u, 30, sp, tr);
    Keys(0, 0, 1);
    Drive(u, 10, sp, tr);
    Check(tr[0] < -0.4f * rate && tr[0] > -0.6f * rate && tr[4] < -0.9f * rate && tr[9] >= -rate * 1.0001f, "guide: D turns right at once, half the turn rate in the first move and nine tenths by the fifth, never past it");
    Keys(0, 0, -1);
    Drive(u, 10, sp, tr);
    Check(tr[0] < 0.f && tr[0] > -rate && tr[2] > 0.f && tr[9] > 0.9f * rate, "guide: A after D eases out of the right turn into the left");
    Keys(0, 0, 0);
    Drive(u, 30, sp, tr);
    bool settling = tr[0] > 0.f && tr[0] < rate;
    for (int i = 1; i < 30; ++i) settling = settling && tr[i] >= 0.f && tr[i] <= tr[i - 1] + 1e-7f;
    Check(settling && tr[29] == 0.f, "guide: letting go of a turn eases it out without turning back");
    Keys(1, 0, 1);
    Drive(u, 14, sp, tr);
    Check(tr[13] < -0.9f * rate && sp[13] > 0.5f * top && sp[13] < top * (1.f - 0.5f * law.turnSlow), "guide: W and D turn while driving, a full turn taking its share of the speed away");
    Keys(0, 0, 0);
    Drive(u, 30, sp, tr);
    Check(Near(u.mUnknown14C.Y, 0.f) && Near(u.mUnknown14C.X, 0.f) && Near(u.mUnknown120.mYaw, u.mCurrentOrientation.mYaw), "guide: nothing held, the tank stands: no request, no turn");
    Keys(1, 0, 0);
    Move(u);
    Check(Along(u) > 0.5f * top, "guide: W moves the hull on the tick the guide first sees it, as the walker's first step does");
    Keys(0, 0, 0);
    Drive(u, 30, sp, tr);
    u.onGround = false;
    u.mVelocity = FVector(0.f, 0.f, -0.3f);
    Keys(1, 0, 0);
    u.mGuide->Unknown3();
    Check(Near(u.mVelocity.Y, 0.f) && Near(u.mVelocity.Z, -0.3f) && u.mUnknown14C.Y > 0.f, "guide: in the air the mover's own velocity stands; only the request is set");
    u.onGround = true; u.mVelocity = FVector();
    Keys(0, 0, 0);
    Drive(u, 30, sp, tr);
    u.friction = 1.f;
    Keys(1, 0, 0);
    u.mGuide->Unknown3();
    Check(Near(u.mUnknown14C.X * -sinf(u.mCurrentOrientation.mYaw) + u.mUnknown14C.Y * cosf(u.mCurrentOrientation.mYaw), clamp * 4.f), "guide: a friction with no solution keeps the stock request");
    u.friction = 0.25f; u.mult = 4.f; u.mVelocity = FVector(); u.mUnknown14C = FVector();
    Drive(u, 6, sp, tr);
    const float held = Along(u);
    Integrate(u);
    Check(sp[0] > 0.5f * top && sp[0] < 0.7f * top && sp[5] > 0.95f * top && Near(Along(u), held, 1e-4f * top), "guide: at the stock cadence of four steps a move the eased speed still holds");
    u.mult = 1.f; u.mVelocity = FVector(); u.mCurrentOrientation.mYaw = 0.f; u.mUnknown120.mYaw = 0.f; u.mPos = FVector(100, 100, 0);
    u.grid.closedFromY = 100.5f;
    Keys(1, 0, 0);
    u.mVelocity = FVector(0.f, top, 0.f);
    Move(u);
    Check(Near(u.mVelocity.Y, 0.f), "guide: no driving into a closed cell ahead: the tank stops at its edge");
    u.mVelocity = FVector(); u.mPos = FVector(100, 100, 0);
    Keys(0, 1, 0);
    u.mGuide->Unknown3();
    Check(u.mUnknown14C.Y < 0.f, "guide: backing away from it goes");
    u.grid.closedFromY = 1e9f; u.grid.closedBelowY = 99.5f;
    u.mGuide->Unknown3();
    Check(Near(u.mUnknown14C.Y, 0.f), "guide: no backing into a closed cell behind");
    u.grid.closedBelowY = -1e9f;
    u.mPos = FVector(-5, 100, 0);
    Keys(1, 0, 0);
    u.mGuide->Unknown3();
    Check(u.mUnknown14C.Y > 0.f, "guide: a unit off the grid's map is not held there");
    u.mPos = FVector(100, 100, 0);
    u.GoToPoint(FVector(7, 7, 0), FALSE);
    u.GuideUnknown7(FVector(8, 8, 0));
    Check(gOrdersSetAside == 2 && gStockOrders == 0, "guide: orders while driven are set aside, none passed on");
    u.dying = true;
    const int stops = u.stops;
    u.mGuide->Unknown3();
    u.dying = false;
    Check(u.stops == stops + 1 && Near(u.mUnknown14C.Y, 0.f), "guide: a dying unit is stopped");
    RemoveDriveGuide();
    Check(u.mGuide == stock && gDriveGuide == nullptr && gStockDeleted == 0 && gStockStops >= 1 && stock->mMode == 0, "guide: the release puts the unit's own guide back, stopped");
    RemoveDriveGuide();
    Check(u.mGuide == stock && gStockDeleted == 0, "guide: a second release changes nothing");
    // the unit shut down with the drive guide in it
    InstallDriveGuide(&u);
    u.Shutdown();
    Check(gDriveGuide == nullptr && gStockDeleted == 1 && u.mGuide == nullptr, "guide: a unit shut down with it inside deletes it, and it the kept guide, once");
    RemoveDriveGuide();
    Check(gStockDeleted == 1, "guide: a release after that touches nothing");
    // no guide of its own, or the first experiment's law asked for
    CUnit bare;
    Check(!InstallDriveGuide(&bare) && gDriveGuide == nullptr, "guide: a unit with no guide keeps the first experiment's law");
    CUnit other;
    Fresh(&other);
    gPathDrive = true;
    Check(!InstallDriveGuide(&other) && gDriveGuide == nullptr, "guide: BEA_PORT_TANK_DRIVE=path keeps the first experiment's law");
    gPathDrive = false;
    delete other.mGuide;
}

static void SuccessorOrders()
{
    for (int ownership = 0; ownership != 3; ++ownership)
    {
        CUnit u;
        StockGuide *stock = Fresh(&u);
        const FVector point(180.f, 140.f, 0.f), later(150.f, 175.f, 0.f);
        const int oldOrders = gStockOrders;
        Check(InstallDriveGuide(&u), "guide: installed for the handback");
        u.GoToPoint(point, TRUE);
        Check(gStockOrders == oldOrders, "guide: a player-held tank ignores an unsolicited order");
        if (ownership == 0) u.ai = AI_ON;
        if (ownership == 1) u.squad = &u;
        if (ownership == 2) u.team = gPossessedAllegiance + 1;
        u.GoToPoint(point, FALSE);
        u.GoToPoint(later, TRUE);
        const bool delivered = gStockOrders == oldOrders + 2 && stock->mDestination.X == later.X &&
                              stock->mDestination.Y == later.Y;
        Check(delivered, "guide: the new AI owner receives both orders on the stock guide");
        const int beforeStop = gStockStops;
        if (ownership == 1) u.mGuide->Stop();
        RemoveDriveGuide();
        Check(u.mGuide == stock, "guide: successor gets the stock guide");
        if (ownership == 1)
            Check(gStockStops == beforeStop + 1 && stock->mMode == 0,
                  "guide: a successor stop cancels its point once");
        else
            Check(stock->mMode != 0 && stock->mDestination.X == later.X && stock->mDestination.Y == later.Y,
                  "guide: release keeps the latest successor destination");
        delete u.mGuide;
        u.mGuide = nullptr;
    }
}

static void OnFoot()
{
    CUnit u;
    StockGuide *stock = Fresh(&u);
    gPathDrive = false;
    gOnFoot = true;
    gLookYaw = 0.f;
    const int takes = gCadenceTakes, releases = gCadenceReleases;
    Check(InstallDriveGuide(&u), "foot: the unit keeps the existing guide in reserve");
    Check(gCadenceTakes == takes + 1, "foot: taking a soldier sets his move cadence to every tick");
    const float clamp = u.top / 20.f, cruise = CePlayUnitMoverCruise(clamp * 4.f, u.friction, clamp);
    auto walk = [&](int n) { for (int i = 0; i < n; ++i) Move(u); return FVector(u.mVelocity.X, u.mVelocity.Y, 0.f); };
    Keys(1.f, 0.f, 0.f);
    FVector first = walk(1);
    Check(hypotf(first.X, first.Y) > 0.5f * cruise && hypotf(first.X, first.Y) < 0.7f * cruise, "foot: W starts at once and eases in: over half his cruise on the first tick");
    const FVector forward = walk(12);
    Check(Near(hypotf(forward.X, forward.Y), cruise, 1e-3f * clamp), "foot: walks at the speed the game's own infantry guide gives him");
    Keys(0.f, 0.f, 0.f);
    FVector glide = walk(1);
    Check(hypotf(glide.X, glide.Y) > 0.6f * cruise && hypotf(glide.X, glide.Y) < 0.8f * cruise && glide.X * forward.X + glide.Y * forward.Y > 0.f, "foot: letting go glides on the same way");
    glide = walk(30);
    Check(hypotf(glide.X, glide.Y) == 0.f, "foot: and comes to a stand");
    Keys(0.f, 1.f, 0.f);
    FVector back = walk(12);
    Check(Near(back.X, -forward.X, 1e-3f * clamp) && Near(back.Y, -forward.Y, 1e-3f * clamp), "foot: back reverses without turning around");
    Keys(0.f, 0.f, 0.f); walk(30);
    Keys(0.f, 0.f, 1.f);
    const FVector side = walk(12);
    Check(Near(side.X*forward.X + side.Y*forward.Y, 0.f, 1e-4f) && Near(hypotf(side.X, side.Y), cruise, 1e-3f * clamp), "foot: strafes perpendicular to the view");
    Keys(1.f, 0.f, 1.f);
    const FVector diagonal = walk(12);
    Check(Near(hypotf(diagonal.X, diagonal.Y), cruise, 1e-3f * clamp), "foot: diagonal movement does not go faster");
    gLookYaw = 1.57079632679f;
    Keys(1.f, 0.f, 0.f);
    const FVector turned = walk(12);
    Check(Near(turned.X*forward.X + turned.Y*forward.Y, 0.f, 1e-4f) && Near(u.mUnknown120.mYaw, gLookYaw), "foot: turning the view changes the walk direction");
    Keys(1.f, 1.f, 0.f);
    const FVector held = walk(30);
    Check(Near(hypotf(held.X, held.Y), 0.f), "foot: opposing input stops the unit");
    Check(gStockSteps == 0, "foot: the parked guide never competes with the player's movement");
    RemoveDriveGuide();
    Check(u.mGuide == stock && gStockStops == 0, "foot: return restores the same guide without cancelling its orders");
    Check(gCadenceReleases == releases + 1, "foot: the release gives him his own cadence back");
    Check(CePlayUnitFootReturnsToAI(true, true, true, true, true), "foot: an independent unit resumes its own AI");
    Check(CePlayUnitFootReturnsToAI(true, true, true, true, false), "foot: an unchanged squad keeps its member");
    Check(CePlayUnitFootReturnsToAI(true, true, true, false, true), "foot: a surviving unit orphaned by its squad can move again");
    Check(!CePlayUnitFootReturnsToAI(true, true, true, false, false), "foot: a different squad keeps its new orders");
    Check(!CePlayUnitFootReturnsToAI(true, true, false, true, false), "foot: a new AI owner keeps its settings");
    Check(!CePlayUnitFootReturnsToAI(true, false, true, true, false), "foot: a changed team is left alone");
    Check(!CePlayUnitFootReturnsToAI(false, true, true, true, false), "foot: a dying unit gets no fresh order");
    gOnFoot = false;
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);	// a control that frees too soon may crash: what was said before it stays said
    Mouse();
    Guide(); SuccessorOrders();
    OnFoot();
    printf("checks %d failed %d\n", checks, failed);
    return failed ? 1 : 0;
}
'''


class DriveWiring(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data" / "ce-playunit-drive-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(dir=parent)
        cls.scratch = Path(cls.tmp.name)
        text = MODULE.read_text()
        cls.mouse, cls.guide = region(text, MOUSE), region(text, GUIDE)
        cls.hook, cls.ordered = poll_hook()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_control(self, name, mouse, guide, hook):
        folder = self.scratch / name
        folder.mkdir()
        cpp = folder / "driver.cpp"
        cpp.write_text(DRIVER.replace("@MOUSE@", mouse).replace("@GUIDE@", guide).replace("@HOOK@", hook))
        exe = folder / "driver"
        built = subprocess.run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-address", "-I", str(PORT / "game"), str(cpp), "-o", str(exe)],
                               capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, "the fixture did not compile:\n" + built.stdout[-1500:] + built.stderr[-1500:])
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        return ran.returncode, ran.stdout.splitlines()

    def test_full_moves_belong_to_the_possessed_tank_only(self):
        text = MODULE.read_text()
        start = text.index("float CGroundVehicle::GetMoveMultiplier()")
        implementation = text[start:text.index("\n}", start) + 2]
        driver = r'''
struct CGroundUnit { virtual float GetMoveMultiplier() { return 7.f; } };
struct CGroundVehicle : CGroundUnit { float GetMoveMultiplier() override; };
CGroundUnit *gUnit;
bool gDriving, gOnFoot, gDriveGuide;
bool gDeliveryAssigned = false;
@IMPLEMENTATION@
int main() {
    CGroundVehicle player, other;
    CGroundUnit *tank = &player, *neighbour = &other;
    gUnit = tank;
    gDriving = gDriveGuide = true;
    if (tank->GetMoveMultiplier() != 1.f || neighbour->GetMoveMultiplier() != 7.f) return 1;
    gOnFoot = true;
    if (tank->GetMoveMultiplier() != 7.f) return 2;
    gOnFoot = false; gDriveGuide = false;
    if (tank->GetMoveMultiplier() != 7.f) return 3;
    gDriveGuide = true; gDriving = false;
    return tank->GetMoveMultiplier() == 7.f ? 0 : 4;
}
'''
        for name, body, expected in [
                ("scope", implementation, 0),
                ("all_tanks", implementation.replace("this == gUnit && ", ""), 1),
                ("stock_cadence", implementation.replace("? 1.f :", "? 7.f :"), 1),
                ("after_release", implementation.replace("gDriving && ", ""), 4)]:
            with self.subTest(name=name):
                src, exe = self.scratch / (name + ".cpp"), self.scratch / name
                src.write_text(driver.replace("@IMPLEMENTATION@", body))
                subprocess.run(["g++", "-std=c++14", "-Werror", str(src), "-o", str(exe)], check=True)
                self.assertEqual(subprocess.run([str(exe)]).returncode, expected)

    def test_a_driven_soldier_moves_every_tick_and_gets_his_own_cadence_back(self):
        text = MODULE.read_text()
        start = text.index("// ---- the soldier's cadence (begin)")
        region = text[start:text.index("// ---- the soldier's cadence (end)", start)]
        driver = r"""
#include <cstddef>
enum { THING_TYPE_INFANTRY = 0x4000, THING_TYPE_VEHICLE = 0x10 };
struct CUnit { unsigned type; unsigned GetThingType() { return type; } virtual ~CUnit() {} };
struct CInfantryUnit : CUnit { float mMoveMultiplier = 4.f; };
bool gOnFoot = true;
@REGION@
int main() {
    CInfantryUnit soldier, other; soldier.type = other.type = THING_TYPE_INFANTRY;
    FootCadenceTake(&soldier);
    if (soldier.mMoveMultiplier != 1.f || other.mMoveMultiplier != 4.f) return 1;		// driven: every tick; nobody else
    FootCadenceRelease(&other);
    if (other.mMoveMultiplier != 4.f) return 2;
    CInfantryUnit next; next.type = THING_TYPE_INFANTRY;
    FootCadenceTake(&next); FootCadenceRelease(&next);
    if (next.mMoveMultiplier != 4.f) return 3;						// handed back: his own cadence again, as an undriven trooper's
    CInfantryUnit para; para.type = THING_TYPE_INFANTRY; para.mMoveMultiplier = 1.f;	// a parachutist's own cadence is 1: kept
    FootCadenceTake(&para); FootCadenceRelease(&para);
    if (para.mMoveMultiplier != 1.f) return 4;
    CInfantryUnit tank; tank.type = THING_TYPE_VEHICLE;
    FootCadenceTake(&tank);
    if (tank.mMoveMultiplier != 4.f) return 5;						// not a soldier: untouched
    FootCadenceRelease(&tank);
    gOnFoot = false; CInfantryUnit later; later.type = THING_TYPE_INFANTRY;
    FootCadenceTake(&later);
    if (later.mMoveMultiplier != 4.f) return 6;						// not on foot: untouched
    return 0;
}
"""
        for name, body, expected in [
                ("native", region, 0),
                ("no_restore", region.replace("gFootCadenceUnit->mMoveMultiplier = gFootCadence;", "(void)0;"), 3),
                ("no_every_tick", region.replace("	foot->mMoveMultiplier = 1.f;\n", ""), 1)]:
            with self.subTest(name=name):
                self.assertTrue(name == "native" or body != region, "the control changed nothing")
                src, exe = self.scratch / ("cadence_" + name + ".cpp"), self.scratch / ("cadence_" + name)
                src.write_text(driver.replace("@REGION@", body))
                subprocess.run(["g++", "-std=c++14", "-Werror", str(src), "-o", str(exe)], check=True)
                self.assertEqual(subprocess.run([str(exe)]).returncode, expected)

    def test_the_poll_calls_the_look_once_after_the_feel_movement(self):
        self.assertEqual(self.hook, HOOK)
        self.assertTrue(self.ordered, "the play-unit call is not right after the feel script's movement is added")

    def test_the_production_regions_hold(self):
        status, lines = self.run_control("native", self.mouse, self.guide, self.hook)
        self.assertEqual([l for l in lines if l.startswith("FAIL")], [])
        self.assertEqual(status, 0)
        summary = [l for l in lines if l.startswith("checks ")]
        self.assertEqual(len(summary), 1)
        self.assertGreaterEqual(int(summary[0].split()[1]), 43, "too few checks ran")

    def test_causal_controls(self):
        controls = {
            # the round-2 review's surviving mutants: the pause guard removed, and the tilt inverted in the poll
            "the_pause_is_not_asked": ("mouse", " || GAME.IsPaused()", "", "mouse: nothing turns while the game is paused"),
            "the_poll_inverts_the_tilt": ("hook", "PortCePlayUnitMouse(m->lX, m->lY);", "PortCePlayUnitMouse(m->lX, -m->lY);", "poll: the look gets the movement the game is given, with its signs"),
            "the_poll_mirrors_the_turn": ("hook", "PortCePlayUnitMouse(m->lX, m->lY);", "PortCePlayUnitMouse(-m->lX, m->lY);", "poll: the look gets the movement the game is given, with its signs"),
            "the_level_state_is_not_asked": ("mouse", " || GAME.GetGameState() != GAME_STATE_PLAYING", "", "mouse: nothing turns while the level is not playing"),
            "the_view_is_not_asked": ("mouse", " || gViewMode != kViewLook", "", "mouse: nothing turns while the view is the game's own camera"),
            "invert_is_ignored": ("mouse", "\tif (!invY) dy = -dy;\n", "\t(void)invY; dy = -dy;\n", "mouse: the player's Invert Y turns the tilt around"),
            "keytrap_is_ignored": ("mouse", "LT.GetKeytrap() || ", "", "mouse: nothing turns while a keytrap is active"),
            "freecam_is_ignored": ("mouse", "GAME.IsFreeCameraOn(0) || ", "", "mouse: nothing turns while free camera is on"),
            "cmdmap_is_ignored": ("mouse", " || PortCeCmdMapOpen()", "", "mouse: nothing turns while Commander view is open"),
            "sensitivity_is_ignored": ("mouse", "const float sens = (gMouseSensitivity > 0.f ? gMouseSensitivity : 7.0f) / 7.0f;", "const float sens = 1.0f;", "mouse: scales with gMouseSensitivity"),
            "the_stick_never_waits": ("mouse", "	gMouseQuiet = 0;\n", "", "mouse: right turns the look right, and the stick waits"),
            "no_grid_guard": ("guide", "			if (grid)\n			{\n				const float guarded", "			if (false)\n			{\n				const float guarded", "guide: no driving into a closed cell ahead: the tank stops at its edge"),
            "the_guard_looks_the_wrong_way": ("guide", "GridOpen(grid, pos + ahead), GridOpen(grid, pos - ahead)", "GridOpen(grid, pos - ahead), GridOpen(grid, pos + ahead)", "guide: no driving into a closed cell ahead: the tank stops at its edge"),
            "the_velocity_is_the_world_s": ("guide", "const FVector want = u->GetOrientation() * FVector(", "gWorldMatrix = true; const FVector want = u->GetOrientation() * FVector(", "guide: the velocity follows the unit's facing"),
            "the_speed_snaps_on": ("guide", "cruise * m.share, ease.speedRise, ease.speedFall", "cruise * m.share, 1.f, ease.speedFall", "guide: W eases in: over half the top speed after one move, nine tenths after three, all of it after twelve"),
            "the_speed_snaps_off": ("guide", "cruise * m.share, ease.speedRise, ease.speedFall", "cruise * m.share, ease.speedRise, 1.f", "guide: letting go of W glides to a stop: seven tenths kept a move, standing still at the end, the request none"),
            "the_turn_snaps": ("guide", "ease.turnRise, ease.turnFall", "1.f, 1.f", "guide: D turns right at once, half the turn rate in the first move and nine tenths by the fifth, never past it"),
            "the_turn_is_the_lead_s": ("guide", "m.yaw = CePlayUnitWrap(yaw + mTurn / (0.1f * mult));", "m.yaw = CePlayUnitWrap(yaw + mTurn);", "guide: D turns right at once, half the turn rate in the first move and nine tenths by the fifth, never past it"),
            "the_speed_is_the_clamp_s": ("guide", "CePlayUnitEaseToward(along, cruise * m.share,", "CePlayUnitEaseToward(along, (cruise > 0.f ? top : top) * m.share,", "guide: W eases in: over half the top speed after one move, nine tenths after three, all of it after twelve"),
            "the_speed_waits_a_tick": ("guide", "		if (grounded)\n		{", "		if (grounded && false)\n		{", "guide: W moves the hull on the tick the guide first sees it, as the walker's first step does"),
            "the_air_is_overridden": ("guide", "const bool grounded = (u->IsOnGround() || u->IsInWater() || !u->ObeyGravity()) && u->Unknown76();", "const bool grounded = true;", "guide: in the air the mover's own velocity stands; only the request is set"),
            "the_soldier_runs_flat_out": ("guide", "moving ? cruise : 0.f", "moving ? (cruise > 0.f ? top : top) : 0.f", "foot: walks at the speed the game's own infantry guide gives him"),
            "the_soldier_snaps_on": ("guide", "moving ? cruise : 0.f, ease.speedRise", "moving ? cruise : 0.f, 1.f", "foot: W starts at once and eases in: over half his cruise on the first tick"),
            "the_soldier_stops_dead": ("guide", "ease.speedRise, ease.speedFall, ease.settle * top);\n	const FVector want(gFootWalkX", "ease.speedRise, 1.f, ease.settle * top);\n	const FVector want(gFootWalkX", "foot: letting go glides on the same way"),
            "the_cadence_is_not_taken": ("guide", "	FootCadenceTake(u);\n", "", "foot: taking a soldier sets his move cadence to every tick"),
            "the_cadence_is_not_given_back": ("guide", "	FootCadenceRelease(u);\n", "", "foot: the release gives him his own cadence back"),
            "the_stock_request_is_lost": ("guide", "		else if (m.share != 0.f)\n", "		else if (false)\n", "guide: a friction with no solution keeps the stock request"),
            "reverse_is_lost": ("guide", "gApplied.forward, gApplied.back, gApplied.turn, law.steerLead", "gApplied.forward, 0.f, gApplied.turn, law.steerLead", "guide: S backs up, easing to its share of the top speed"),
            "an_order_is_passed_on": ("guide", "else ++gOrdersSetAside;", "else { ++gOrdersSetAside; mStock->GoToPoint(destination, how); }", "guide: orders while driven are set aside, none passed on"),
            "the_kept_guide_is_not_put_back": ("guide", "		u->mGuide = g->mStock;\n		g->mStock = NULL;\n", "		u->mGuide = NULL;\n", "guide: the release puts the unit's own guide back, stopped"),
            "the_kept_guide_is_not_stopped": ("guide", "		if (u->mGuide && !gOnFoot && !g->mSuccessorOrder) u->mGuide->Stop();\n", "", "guide: the release puts the unit's own guide back, stopped"),
            "drop_successor_waypoint": ("guide", "mStock->GoToPoint(destination, how);", "/* discarded */", "guide: the new AI owner receives both orders on the stock guide"),
            "erase_successor_destination": ("guide", "!gOnFoot && !g->mSuccessorOrder", "!gOnFoot", "guide: release keeps the latest successor destination"),
            "lose_successor_stop": ("guide", "mStock->Stop();", "/* discarded */", "guide: a successor stop cancels its point once"),
            "the_kept_guide_leaks_at_shutdown": ("guide", "		if (stock) delete stock;\n", "		(void)stock;\n", "guide: a unit shut down with it inside deletes it, and it the kept guide, once"),
            "the_pointer_outlives_the_guide": ("guide", "		if (gDriveGuide == this) gDriveGuide = NULL;\n", "", "guide: a unit shut down with it inside deletes it, and it the kept guide, once"),
            "a_dying_unit_drives_on": ("guide", "		if (u->IsDying()) { u->Stop(); return; }\n", "", "guide: a dying unit is stopped"),
            "the_path_law_is_not_asked": ("guide", "if (gPathDrive || gDriveGuide || !u->mGuide) return false;", "if (gDriveGuide || !u->mGuide) return false;", "guide: BEA_PORT_TANK_DRIVE=path keeps the first experiment's law"),
        }
        for name, (which, before, after, expected) in controls.items():
            with self.subTest(control=name):
                pieces = {"mouse": self.mouse, "guide": self.guide, "hook": self.hook}
                self.assertEqual(pieces[which].count(before), 1, f"the text a control changes is not unique: {before[:60]!r}")
                pieces[which] = pieces[which].replace(before, after)
                status, lines = self.run_control("control-" + name, pieces["mouse"], pieces["guide"], pieces["hook"])
                self.assertNotEqual(status, 0, f"control escaped: {name}")
                self.assertIn("FAIL " + expected, lines, f"the fixture did not say {expected!r}: {[l for l in lines if l.startswith('FAIL')][:4]}")

    def test_take_installs_and_release_removes_before_the_hand_back(self):
        code = "\n".join(l for l in MODULE.read_text().split("\n") if not l.strip().startswith("//"))
        take = code[code.index("bool Take(CPlayer *player"):code.index("bool Ready(CPlayer **playerOut")]
        self.assertEqual(take.count("InstallDriveGuide(u);"), 1)
        self.assertLess(take.index("u->SetAIState(AI_OFF);"), take.index("InstallDriveGuide(u);"), "the unit leaves its squad's orders before the guide goes in")
        release = code[code.index("void Release(const char *why"):code.index("void UnitKeyName(")]
        self.assertEqual(release.count("RemoveDriveGuide();"), 1)
        self.assertLess(release.index("RemoveDriveGuide();"), release.index("ReturnUnitControl();"), "the unit's own guide is back before its squad's orders are")
        # the drive runs the direct law when its guide is in the unit and the first experiment's law only otherwise
        drive = code[code.index('void Drive(CUnit *u'):code.index('unsigned long long ViewNs()', code.index('void Drive(CUnit *u'))]
        self.assertIn("if (gDriveGuide && gDriveGuide->GetUnit() == u && u->mGuide == gDriveGuide)", drive)
        self.assertLess(drive.index("if (gDriveGuide && gDriveGuide->GetUnit() == u"), drive.index("else if (in.forward > 0.1f || pivot)"))

    def test_return_and_take_on_one_tick(self):
        # Compile the actual input dispatch and return branch against a switchable
        # target and with no available target. Calls are the observable
        # result: a failed switch would show a refusal even if Drive later returns.
        module = MODULE.read_text()
        dispatch = module.split("if (gTakeOnce) gTakePressed = false;", 1)[1].split("#endif", 1)[1]
        dispatch = dispatch.split("Drive(u, player, engine);", 1)[0] + "Drive(u, player, engine);"
        leave = module.split("if (in.leave)", 1)[1].split("}", 1)[0] + "}"
        driver = r'''
struct Inputs { bool leave; } gIn;
bool gDriving, gTakePressed, target;
int switches, returns, refusals, drivingTicks;
void Release(const char *, const char *) { ++returns; gDriving = false; }
void AnswerSwitch(int, int) {
    ++switches;
    if (target) gDriving = false;
    else ++refusals;
}
void FinishSwitch() { gDriving = true; }
void Drive(int, int, int) {
    Inputs in = gIn;
    gIn.leave = false;
    if (in.leave) RETURN_BRANCH
    ++drivingTicks;
}
void tick() {
    int u = 1, player = 1, engine = 1;
    INPUT_DISPATCH
}
int main() {
    for (int available = 0; available <= 1; ++available)
        for (int take = 0; take <= 1; ++take)
            for (int back = 0; back <= 1; ++back) {
                gDriving = true; gIn.leave = back; gTakePressed = take; target = available;
                switches = returns = refusals = drivingTicks = 0;
                tick();
                if (returns != back || switches != (take && !back) || refusals != (take && !back && !available)
                    || gTakePressed || gDriving != !back) return 1;
                if (drivingTicks != (!back && !(take && available))) return 2;
            }
    return 0;
}
'''.replace("RETURN_BRANCH", leave).replace("INPUT_DISPATCH", dispatch)
        folder = self.scratch / "return-and-take"
        folder.mkdir()
        cpp, exe = folder / "fixture.cpp", folder / "fixture"
        cpp.write_text(driver)
        built = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(exe)],
                               capture_output=True, text=True, timeout=30)
        self.assertEqual(built.returncode, 0, built.stderr)
        self.assertEqual(subprocess.run([str(exe)], timeout=10).returncode, 0,
                         "Return must hand back this unit once and consume the competing Take press")

    def test_a_press_during_a_switch_wait_is_let_go(self):
        code = MODULE.read_text()
        self.assertEqual(code.count("if (gSwitchWait) { gTakePressed = false; FinishSwitch(); return; }"), 1, "a press while the switch waits would be answered as a further switch after it")


if __name__ == "__main__":
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    unittest.main(verbosity=2)
