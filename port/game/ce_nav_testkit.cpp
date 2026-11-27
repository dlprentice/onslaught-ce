// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_nav_testkit.cpp: Autonomous in-game pathfinding, combat, unit-takeover,
// driving, and live F9 bug-clip verbs + read-only telemetry for Onslaught CE (--test-kit only).
//
// Strict architecture:
// - 100% genuine player input (CE_TEST_INPUT): every action flows through PortTestKitKey
//   and PortTestKitMouse, exercising real physics, collision, weapon heat, morphing, and HUD.
// - Zero teleports, zero godmode, zero direct game-state writes.
// - Zero heap allocations (fixed-size CeNav structs) so qcxx allocator renames never link-fail.
// - Self-cleaning NavInputState releases every held key/mouse button on completion, failure,
//   timeout, or journey exit via CeTestKitRegisterCleanup.
#include "common.h"
#include "game.h"
#include "player.h"
#include "BattleEngine.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "world.h"
#include "unit.h"
#include "thing.h"
#include "map.h"
#include "Line.h"
#include "Camera.h"
#include "Career.h"
#include "eventmanager.h"
#include "ce_museum_hall.h"
#include "ce_sanctuary_citadel.h"
#include "ce_playunit.h"
#include "ce_nav_planner.h"
#include "../platform/common/ce_testkit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern float gMouseSensitivity;	// src/Controller.cpp

extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" int PortCeBugClipActive(void) __attribute__((weak));
extern "C" int PortCeBugClipStatus(char *out, int n, int *outRecording, float *outFade) __attribute__((weak));

namespace
{

// ---- Self-cleaning player input ownership ------------------------------------------------------------------------------

enum NavKeySlot
{
	kKeyW = 0,
	kKeyA,
	kKeyS,
	kKeyD,
	kKeySpace,
	kKeyLCtrl,
	kKeyE,
	kKeyF9,
	kKeyCount
};

struct KeyDef
{
	unsigned sdl;
	unsigned raw;
};

const KeyDef kNavKeys[kKeyCount] = {
	{26, 0x11},		// W
	{4, 0x1e},		// A
	{22, 0x1f},		// S
	{7, 0x20},		// D
	{44, 0x39},		// SPACE
	{224, 0x1d},	// LCTRL
	{8, 0x12},		// E
	{66, 0x43},		// F9
};

enum NavMouseBtn
{
	kBtnLeft = 0,
	kBtnRight,
	kBtnMiddle,
	kBtnCount
};

struct NavInputState
{
	bool keys[kKeyCount];
	bool btns[kBtnCount];

	void SetKey(NavKeySlot slot, bool down)
	{
		if (keys[slot] == down) return;
		keys[slot] = down;
		PortTestKitKey(kNavKeys[slot].sdl, kNavKeys[slot].raw, down);
	}

	void SetBtn(NavMouseBtn btn, bool down)
	{
		if (btns[btn] == down) return;
		btns[btn] = down;
		static const int kDownKind[kBtnCount] = {4, 8, 10};
		static const int kUpKind[kBtnCount] = {5, 9, 11};
		PortTestKitMouse(down ? kDownKind[btn] : kUpKind[btn], 0.0f, 0.0f);
	}

	void MoveMouse(float dx, float dy)
	{
		const float rdx = floorf(dx + (dx >= 0.0f ? 0.5f : -0.5f));
		const float rdy = floorf(dy + (dy >= 0.0f ? 0.5f : -0.5f));
		if (rdx != 0.0f || rdy != 0.0f)
			PortTestKitMouse(3, rdx, rdy);
	}

	void ReleaseAll()
	{
		for (int i = 0; i < kKeyCount; ++i)
		{
			if (keys[i])
			{
				keys[i] = false;
				PortTestKitKey(kNavKeys[i].sdl, kNavKeys[i].raw, false);
			}
		}
		for (int i = 0; i < kBtnCount; ++i)
		{
			if (btns[i])
			{
				btns[i] = false;
				static const int kUpKind[kBtnCount] = {5, 9, 11};
				PortTestKitMouse(kUpKind[i], 0.0f, 0.0f);
			}
		}
		PortTestKitMouse(12, 0.0f, 0.0f);
	}
};

NavInputState gInput = {};

// ---- Persistent autonomous nav workspace & run-wide telemetry ----------------------------------------------------------

CeNav::NavGrid gGrid;
CeNav::AStarWorkspace gAStar;
CeNav::NavController gNav;

enum { kMaxPenalties = 32, kMaxExploreTargets = 8 };
CeNav::Vec2 gPenalties[kMaxPenalties];
int gPenaltyCount = 0;

// Cumulative run telemetry exposed via CE_TEST_STATE.
CeNav::NavStatus gTelemetryStatus = CeNav::kNavIdle;
float gTelemetryDist = 0.0f;
int gTelemetryReplans = 0;
int gTelemetryRecoveries = 0;
float gTelemetryMoved = 0.0f;
float gStepMovedBase = 0.0f;

// Multi-step verb state (zeroed on new step).
struct ExploreState
{
	CeNav::Vec2 targets[kMaxExploreTargets];
	int count;
	int current;
	CeNav::NavMode mode;
	float tol;
	bool active;
};
ExploreState gExplore = {};

struct CombatState
{
	int targetThing;
	int fireCycle;
	int strafeDir;
	int strafeTimer;
	int dodgeCooldown;
	int dodgeHold;
	int replanCooldown;
	float prevLife;
	float maxRange;
	float startSimTime;
	float durationSec;
	float lastSimTime;
	bool navPlanned;
};
CombatState gCombat = {};

struct TakeUnitState
{
	int targetThing;
	int phase;			// 0 = approach, 1 = aim & tap E
	int eHold;
	int eWait;
	int attempts;
	int replanCooldown;
	float maxDist;
	float lastSimTime;
	bool navPlanned;
};
TakeUnitState gTake = {};

struct MorphState
{
	bool wantJet;
	int tapHold;
	int retryWait;
	float lastSimTime;
};
MorphState gMorph = {};

struct DodgeState
{
	bool goRight;
	int phase;			// 0 = hold LCTRL+dir, 1 = verify dash / lateral velocity
	int ticks;
	float startSpeedLat;
	float lastSimTime;
	bool sawDash;
	float peakLat;
};
DodgeState gDodge = {};

struct ClipState
{
	int phase;			// 0 = press F9 start, 1 = confirm active & wait N, 2 = press F9 stop, 3 = wait finalize
	int keyHold;
	unsigned recStartedTick;
	unsigned recTicks;
	float recStartSim;
	float lastSimTime;
	int startRetries;
	int postStopTicks;
};
ClipState gClip = {};

float gLastNavSimTime = -1.0f;
int gNavMorphTapHold = 0;
int gNavMorphRetryWait = 0;

bool AdvanceSimTick(float &lastSimTime)
{
	const float now = EVENT_MANAGER.GetTime();
	if (lastSimTime >= 0.0f && now <= lastSimTime + 1e-4f)
		return false;
	lastSimTime = now;
	return true;
}

void NavCleanupCallback()
{
	gInput.ReleaseAll();
	gExplore.active = false;
	gLastNavSimTime = -1.0f;
	gNavMorphTapHold = 0;
	gNavMorphRetryWait = 0;
	if (gTelemetryStatus == CeNav::kNavMoving || gTelemetryStatus == CeNav::kNavRecovering)
		gTelemetryStatus = CeNav::kNavIdle;
}

struct NavCleanupRegistrar
{
	NavCleanupRegistrar() { CeTestKitRegisterCleanup(NavCleanupCallback); }
} sNavCleanupRegistrar;

// ---- World Inspection Helpers (Read-Only) ------------------------------------------------------------------------------

bool InPlay()
{
	return GAME.GetGameState() == GAME_STATE_PLAYING && !GAME.IsPaused();
}

CBattleEngine *GetBE()
{
	if (!InPlay()) return NULL;
	CPlayer *p = GAME.GetPlayer(0);
	return p ? p->GetBattleEngine() : NULL;
}

bool DrivingPlayUnit()
{
#ifdef CE_VERSION
	return InPlay() && CePlayUnitDrivingNow();
#else
	return false;
#endif
}

bool InMuseum()
{
	return PortCeMuseumVisitActive && PortCeMuseumVisitActive();
}

bool InSanctuary(float x, float y)
{
	return PortCeSanctuaryActive() && CeSanctuaryCitadel::IsOnTerrace(x, y);
}

bool IsIndoorAt(float x, float y)
{
	return InMuseum() || InSanctuary(x, y);
}

float MouseSens()
{
	return gMouseSensitivity > 0.5f ? gMouseSensitivity : 7.0f;
}

bool GetPilotPose(CeNav::Vec3 *outPos, float *outYaw, float *outPitch, CeNav::Vec3 *outVel, bool *outInJet, bool *outPlayUnit)
{
#ifdef CE_VERSION
	if (DrivingPlayUnit())
	{
		float p[3] = {0.0f, 0.0f, 0.0f};
		float headingDeg = 0.0f, speedUps = 0.0f;
		if (CePlayUnitDrivenPose(p, &headingDeg, &speedUps))
		{
			if (outPos) *outPos = CeNav::Vec3{p[0], p[1], p[2]};
			if (outYaw) *outYaw = headingDeg * CeNav::kDegToRad;
			if (outPitch) *outPitch = 0.0f;
			if (outVel) *outVel = CeNav::Vec3{speedUps, 0.0f, 0.0f};
			if (outInJet) *outInJet = false;
			if (outPlayUnit) *outPlayUnit = true;
			return true;
		}
	}
#endif
	CBattleEngine *be = GetBE();
	if (!be) return false;
	const FVector p = be->GetPos();
	const FVector v = be->GetVelocity();
	const CEulerAngles &e = be->GetCurrentEulerOrientation();
	if (outPos) *outPos = CeNav::Vec3{p.X, p.Y, p.Z};
	if (outYaw) *outYaw = e.mYaw;
	if (outPitch) *outPitch = e.mPitch;
	if (outVel) *outVel = CeNav::Vec3{v.X, v.Y, v.Z};
	if (outInJet) *outInJet = (be->GetState() == BATTLE_ENGINE_STATE_JET);
	if (outPlayUnit) *outPlayUnit = false;
	return true;
}

bool GetAimOrigin(CeNav::Vec3 *outPos, float *outYaw, float *outPitch)
{
	CBattleEngine *be = GetBE();
	if (!be) return false;
	CCamera *cam = GAME.GetCurrentCamera(0);
	const FVector p = cam ? cam->GetPos() : (be->GetPos() + FVector(0.0f, 0.0f, -be->COfGHeight()));
	const CEulerAngles &e = be->GetCurrentEulerOrientation();
	if (outPos) *outPos = CeNav::Vec3{p.X, p.Y, p.Z};
	if (outYaw) *outYaw = e.mYaw;
	if (outPitch) *outPitch = e.mPitch;
	return true;
}

bool IsHostileUnit(CUnit *u, CBattleEngine *be)
{
	if (!u || !be || (CThing *)u == (CThing *)be) return false;
	if (u->IsDying() || u->IsShuttingDown() || !u->Active()) return false;
	if (!(u->GetLife() > 0.0f)) return false;
	if (u->GetThingType() & (THING_TYPE_AMMUNITION | THING_TYPE_EXPLOSION | THING_TYPE_CUTSCENE)) return false;
	const char *cls = u->_GetClassName();
	if (cls && !strcmp(cls, "CMine")) return false;
	const EAllegiance alg = u->GetAllegiance();
	if (alg != kForsetiAllegiance && alg != kMuspellAllegiance) return false;
	if (alg == be->GetAllegiance()) return false;
	return be->IsTargetAlligence(SINT(alg)) != FALSE;
}

bool IsFriendlyUnit(CUnit *u, CBattleEngine *be)
{
	if (!u || !be || (CThing *)u == (CThing *)be) return false;
	if (u->IsDying() || u->IsShuttingDown() || !u->Active()) return false;
	if (!(u->GetLife() > 0.0f)) return false;
	return u->GetAllegiance() == be->GetAllegiance();
}

CUnit *FindUnitByNumber(int thingNumber)
{
	if (thingNumber < 0) return NULL;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
		if (u->GetThingNumber() == thingNumber) return u;
	return NULL;
}

CUnit *FindNearestHostile(CBattleEngine *be, float *outDist = NULL)
{
	if (!be) return NULL;
	const FVector myPos = be->GetPos();
	CUnit *best = NULL;
	float bestD2 = 1e30f;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
	{
		if (!IsHostileUnit(u, be)) continue;
		const FVector up = u->GetPos();
		const float dx = up.X - myPos.X;
		const float dy = up.Y - myPos.Y;
		const float d2 = dx * dx + dy * dy;
		if (d2 < bestD2)
		{
			bestD2 = d2;
			best = u;
		}
	}
	if (best && outDist) *outDist = sqrtf(bestD2);
	return best;
}

CUnit *FindNearestTakeableFriendly(CBattleEngine *be, float maxDist, float *outDist = NULL)
{
	if (!be) return NULL;
	const FVector myPos = be->GetPos();
	CUnit *bestVehicle = NULL;
	float bestVehicleD2 = maxDist * maxDist;
	CUnit *bestAny = NULL;
	float bestAnyD2 = maxDist * maxDist;
#ifdef CE_VERSION
	const uint64_t selWorld = CePlayUnitSelectionWorld();
#endif
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
	{
		if (!IsFriendlyUnit(u, be)) continue;
#ifdef CE_VERSION
		if (!CePlayUnitCanTakeSelection(selWorld, u, u->GetThingNumber(), NULL)) continue;
#else
		if (!(u->GetThingType() & THING_TYPE_VEHICLE)) continue;
#endif
		const FVector up = u->GetPos();
		const float dx = up.X - myPos.X;
		const float dy = up.Y - myPos.Y;
		const float d2 = dx * dx + dy * dy;
		const bool isGroundVehicle = (u->GetThingType() & THING_TYPE_VEHICLE) && !(u->GetThingType() & THING_TYPE_AIR_UNIT);
		if (isGroundVehicle && d2 < bestVehicleD2)
		{
			bestVehicleD2 = d2;
			bestVehicle = u;
		}
		if (d2 < bestAnyD2)
		{
			bestAnyD2 = d2;
			bestAny = u;
		}
	}
	if (bestVehicle)
	{
		if (outDist) *outDist = sqrtf(bestVehicleD2);
		return bestVehicle;
	}
	if (bestAny)
	{
		if (outDist) *outDist = sqrtf(bestAnyD2);
		return bestAny;
	}
	return NULL;
}

CThing *FindSelfThing(CBattleEngine *be, CeNav::Vec3 pos)
{
	if (DrivingPlayUnit())
	{
		CUnit *best = NULL;
		float bestD2 = 9.0f;	// within 3m of CePlayUnitDrivenPose()
		ListIterator<CUnit> it(&WORLD.GetUnitNB());
		for (CUnit *u = it.First(); u; u = it.Next())
		{
			if (!u || (CThing *)u == (CThing *)be || u->IsDying() || u->IsShuttingDown()) continue;
			const FVector up = u->GetPos();
			const float dx = up.X - pos.x;
			const float dy = up.Y - pos.y;
			const float d2 = dx * dx + dy * dy;
			if (d2 < bestD2)
			{
				bestD2 = d2;
				best = u;
			}
		}
		if (best) return best;
	}
	return be;
}

bool HasClearWorldLOS(CThing *ignoreSelf, const FVector &from, const FVector &to, CThing *targetThing = NULL)
{
	const FVector d = to - from;
	const float dist = d.Magnitude();
	if (dist < 1.0f) return true;
	CWorldLineColReport rep;
	WORLD.FindFirstThingToHitLine(
		CLine(from, to), ignoreSelf, &rep, FALSE, TRUE, 2,
		THING_TYPE_AMMUNITION | THING_TYPE_EXPLOSION | THING_TYPE_TREE, TRUE);
	if (rep.mHitType == kCollideNothing) return true;
	if (targetThing && rep.mHitThing == targetThing) return true;
	return rep.mDistToImpact >= dist - 2.5f;
}

bool PointDryAndWalkable(float wx, float wy, bool allowWater, bool indoorMuseum, const CeSanctuaryCitadel::SolidLayout *citadel)
{
	const float mapW = MAP.mGridWidth > 0 ? float(MAP.mGridWidth) : 512.0f;
	const float mapH = MAP.mGridHeight > 0 ? float(MAP.mGridHeight) : 512.0f;
	if (wx < 4.0f || wy < 4.0f || wx > mapW - 4.0f || wy > mapH - 4.0f) return false;

	if (indoorMuseum)
	{
		if (!CeMuseumHallAllowsVisitor(wx, wy)) return false;
		// Keep shoulder clearance around Museum plinths & walls.
		static const float kOff[4][2] = {{0.48f, 0.0f}, {-0.48f, 0.0f}, {0.0f, 0.48f}, {0.0f, -0.48f}};
		for (int i = 0; i < 4; ++i)
			if (!CeMuseumHallAllowsVisitor(wx + kOff[i][0], wy + kOff[i][1]))
				return false;
		return true;
	}

	if (citadel && PortCeSanctuaryActive() && CeSanctuaryCitadel::IsOnTerrace(wx, wy))
	{
		if (!CeSanctuaryCitadel::IsInteriorPointClear(
				*citadel, wx, wy, CeSanctuaryCitadel::kFloorZ - 1.5f,
				CeSanctuaryCitadel::kFloorZ, 1.45f))
			return false;
	}

	if (!allowWater)
	{
		const FVector probe(wx, wy, 0.0f);
		const float gz = MAP.Collide(probe);
		const float wz = MAP.GetWaterLevel();
		// Engine Z grows DOWN: terrain above water has gz < wz.
		if (gz >= wz - 0.35f) return false;
		const FVector n = MAP.Normal(probe);
		if (fabsf(n.Z) < 0.68f) return false;
	}
	return true;
}

bool SnapPointWalkable(CeNav::Vec2 desired, bool allowWater, float maxSearchRadius, CeNav::Vec2 *outPt)
{
	const bool museum = InMuseum();
	const CeSanctuaryCitadel::SolidLayout citadel = CeSanctuaryCitadel::MakeInteriorSolidLayout();
	const CeSanctuaryCitadel::SolidLayout *citPtr = PortCeSanctuaryActive() ? &citadel : NULL;
	if (PointDryAndWalkable(desired.x, desired.y, allowWater, museum, citPtr))
	{
		if (outPt) *outPt = desired;
		return true;
	}
	const float step = museum ? 1.0f : 3.0f;
	for (float r = step; r <= maxSearchRadius; r += step)
	{
		for (int k = 0; k < 16; ++k)
		{
			const float a = float(k) * (CeNav::kTwoPi / 16.0f);
			const float cx = desired.x + cosf(a) * r;
			const float cy = desired.y + sinf(a) * r;
			if (PointDryAndWalkable(cx, cy, allowWater, museum, citPtr))
			{
				if (outPt) *outPt = CeNav::Vec2{cx, cy};
				return true;
			}
		}
	}
	if (outPt) *outPt = desired;
	return false;
}

void BuildLiveNavGrid(CeNav::Vec2 start, CeNav::Vec2 goal, CeNav::NavMode mode, bool indoor,
                      CBattleEngine *be, CThing *ignoreGoalThing = NULL)
{
	const bool museum = InMuseum();
	const bool allowWater = (mode == CeNav::kModeJet);
	const float minCell = museum ? 0.9f : (indoor ? 1.1f : 2.5f);
	const float margin = museum ? 14.0f : (indoor ? 18.0f : 40.0f);
	gGrid.InitAround(start.x, start.y, goal.x, goal.y, minCell, margin, CeNav::kMaxGridW);

	const CeSanctuaryCitadel::SolidLayout citadel = CeSanctuaryCitadel::MakeInteriorSolidLayout();
	const CeSanctuaryCitadel::SolidLayout *citPtr = PortCeSanctuaryActive() ? &citadel : NULL;

	for (int cy = 0; cy < gGrid.height; ++cy)
	{
		for (int cx = 0; cx < gGrid.width; ++cx)
		{
			const CeNav::Vec2 c = gGrid.CellCenter(cx, cy);
			if (!PointDryAndWalkable(c.x, c.y, allowWater, museum, citPtr))
				gGrid.SetBlocked(cx, cy, true);
		}
	}

	// Mark static buildings, emplacements, and ground units on outdoor maps.
	if (!museum && !allowWater)
	{
		CThing *selfThing = FindSelfThing(be, CeNav::Vec3{start.x, start.y, 0.0f});
		ListIterator<CThing> it(&WORLD.GetThingNB());
		for (CThing *t = it.First(); t; t = it.Next())
		{
			if (!t || t == (CThing *)be || t == selfThing || t == ignoreGoalThing) continue;
			if (t->IsDying() || t->IsShuttingDown()) continue;
			const ULONG tt = t->GetThingType();
			if (tt & (THING_TYPE_AMMUNITION | THING_TYPE_EXPLOSION | THING_TYPE_CUTSCENE | THING_TYPE_TREE | THING_TYPE_AIR_UNIT))
				continue;
			if (tt & THING_TYPE_CAN_BE_WALKED_ON) continue;
			const bool isObstacle = (tt & (THING_TYPE_BUILDING | THING_TYPE_EMPLACEMENT | THING_TYPE_VEHICLE |
			                               THING_TYPE_MECH | THING_TYPE_GROUND_UNIT | THING_TYPE_FEATURE)) != 0;
			if (!isObstacle) continue;

			const FVector tp = t->GetPos();
			// Skip self if driving a possessed unit near start.
			if (CeNav::Dist2D(start, CeNav::Vec2{tp.X, tp.Y}) < 3.5f) continue;
			float rad = t->GetRadius();
			if (!(rad > 0.5f)) rad = 2.0f;
			if (rad > 20.0f) rad = 20.0f;
			gGrid.MarkBlockedDisk(tp.X, tp.Y, rad + (indoor ? 0.6f : 1.6f));
		}
	}

	gGrid.InflateSoftMargin();

	// Re-apply dynamic stuck-recovery penalties so replans steer around unmapped obstacles.
	const float penRad = indoor ? 2.2f : 5.5f;
	for (int i = 0; i < gPenaltyCount; ++i)
		gGrid.PenalizeDisk(gPenalties[i].x, gPenalties[i].y, penRad, 95);
}

bool PlanLivePath(CeNav::Vec2 start, CeNav::Vec2 goal, CeNav::NavMode mode, bool indoor,
                  CBattleEngine *be, CeNav::NavPath &outPath, bool allowPartial, CThing *ignoreGoalThing = NULL)
{
	BuildLiveNavGrid(start, goal, mode, indoor, be, ignoreGoalThing);

	CeNav::NavPath rawPath;
	if (!CeNav::FindPathAStar(gGrid, gAStar, start, goal, rawPath, allowPartial, indoor ? 4 : 6))
		return false;

	const bool museum = InMuseum();
	const float clr = museum ? 0.40f : (gGrid.cellSize * 0.45f);
	const bool checkWorldLos = (!museum && mode != CeNav::kModeJet && be != NULL);
	CThing *selfThing = FindSelfThing(be, CeNav::Vec3{start.x, start.y, 0.0f});

	CeNav::StringPullPath(
		gAStar.rawPts, gAStar.rawCount, outPath,
		[clr, checkWorldLos, selfThing, ignoreGoalThing](CeNav::Vec2 a, CeNav::Vec2 b) -> bool
		{
			if (!CeNav::GridLineClear(gGrid, a, b, clr, true)) return false;
			if (!checkWorldLos) return true;
			const float za = MAP.Collide(FVector(a.x, a.y, 0.0f)) - 2.2f;
			const float zb = MAP.Collide(FVector(b.x, b.y, 0.0f)) - 2.2f;
			return HasClearWorldLOS(selfThing, FVector(a.x, a.y, za), FVector(b.x, b.y, zb), ignoreGoalThing);
		});
	return outPath.count > 0;
}

void SampleLiveFeelers(CeNav::Vec3 pos, float yaw, float range, bool inJet, CThing *selfThing,
                       float *outCenter, float *outLeft, float *outRight)
{
	static const float kAngles[3] = {0.0f, -0.56f, 0.56f};	// center, left (+yaw), right (-yaw)
	float results[3] = {range, range, range};
	const bool museum = InMuseum();

	for (int i = 0; i < 3; ++i)
	{
		// Note: positive right angle decreases engine yaw.
		const float rayYaw = yaw - kAngles[i];
		const CeNav::Vec2 dir = CeNav::ForwardXY(rayYaw);
		float best = range;
		if (!inJet)
		{
			const float gd = CeNav::GridRayFeeler(gGrid, CeNav::Vec2{pos.x, pos.y}, dir.x, dir.y, range);
			if (gd < best) best = gd;
		}
		if (!museum && !inJet && selfThing)
		{
			const float z0 = pos.z - 1.8f;
			const float p1x = pos.x + dir.x * range;
			const float p1y = pos.y + dir.y * range;
			const float z1 = MAP.Collide(FVector(p1x, p1y, 0.0f)) - 1.8f;
			const FVector p0(pos.x, pos.y, z0);
			const FVector p1(p1x, p1y, z1);
			CWorldLineColReport rep;
			WORLD.FindFirstThingToHitLine(
				CLine(p0, p1), selfThing, &rep, FALSE, TRUE, 2,
				THING_TYPE_AMMUNITION | THING_TYPE_EXPLOSION | THING_TYPE_TREE, TRUE);
			if (rep.mHitType == kCollideThing && isfinite(rep.mDistToImpact) && rep.mDistToImpact >= 0.0f)
			{
				if (rep.mDistToImpact < best) best = rep.mDistToImpact;
			}
		}
		results[i] = best;
	}
	*outCenter = results[0];
	*outLeft = results[1];
	*outRight = results[2];
}

void ApplyDriveOutput(const CeNav::NavDriveOutput &out, bool allowMorph)
{
	gInput.SetKey(kKeyW, out.forward);
	gInput.SetKey(kKeyS, out.back);
	gInput.SetKey(kKeyA, out.strafeLeft);
	gInput.SetKey(kKeyD, out.strafeRight);
	gInput.SetKey(kKeySpace, allowMorph && out.wantMorphJump);
	gInput.MoveMouse(out.mouseDx, out.mouseDy);
}

void RecordPenalty(CeNav::Vec2 pt)
{
	if (gPenaltyCount < kMaxPenalties)
		gPenalties[gPenaltyCount++] = pt;
	else
		gPenalties[gPenaltyCount - 1] = pt;
}

void SyncNavTelemetry()
{
	gTelemetryStatus = gNav.status;
	gTelemetryDist = gNav.status == CeNav::kNavArrived ? 0.0f : CeNav::Dist2D(gNav.lastPos, gNav.goal);
	gTelemetryMoved = gStepMovedBase + gNav.totalMoved;
}

CeNav::NavMode ParseMode(const char *word)
{
	if (!word) return CeNav::kModeAuto;
	if (!strcasecmp(word, "walker")) return CeNav::kModeWalker;
	if (!strcasecmp(word, "jet")) return CeNav::kModeJet;
	return CeNav::kModeAuto;
}

float ComputeJetDesiredPitch(CeNav::Vec3 pos, float yaw)
{
	// Keep Jet cruising safely ~24m above terrain/water ahead (Z grows DOWN, so smaller Z = higher).
	const CeNav::Vec2 fwd = CeNav::ForwardXY(yaw);
	const float gHere = MAP.Collide(FVector(pos.x, pos.y, 0.0f));
	const float gAhead = MAP.Collide(FVector(pos.x + fwd.x * 24.0f, pos.y + fwd.y * 24.0f, 0.0f));
	const float wz = MAP.GetWaterLevel();
	float surfaceZ = gHere < gAhead ? gHere : gAhead;
	if (wz < surfaceZ) surfaceZ = wz;
	const float targetZ = surfaceZ - 24.0f;
	const float dz = targetZ - pos.z;	// negative means we should climb (negative pitch)
	return CeNav::ClampF(dz * 0.025f, -0.35f, 0.25f);
}

// Shared per-tick closed-loop navigation step (used by nav-to, nav-offset, nav-explore, nav-nearest-enemy, nav-drive-offset).
CeTestResult StepActiveNav(CBattleEngine *be, bool drivingPlayUnit, CeNav::NavMode mode,
                           CThing *ignoreGoalThing, CeTestReply &reply)
{
	CeNav::Vec3 pos{0, 0, 0}, vel{0, 0, 0};
	float yaw = 0.0f, pitch = 0.0f;
	bool inJet = false, isPU = false;
	if (!GetPilotPose(&pos, &yaw, &pitch, &vel, &inJet, &isPU))
	{
		gInput.ReleaseAll();
		CeTestReplyf(reply, "waiting for pilot pose");
		return CE_TEST_PENDING;
	}

	// Check arrival immediately on any Present, and advance the closed-loop controller strictly once per 20 Hz sim tick
	// so stall timers, recovery phases, morph taps, and steering behave identically at 20 FPS, 60 FPS, and 400 FPS.
	const float curDist = CeNav::Dist2D(CeNav::Vec2{pos.x, pos.y}, gNav.goal);
	if (curDist <= gNav.cfg.goalTol)
	{
		gInput.ReleaseAll();
		gStepMovedBase += gNav.totalMoved;
		gTelemetryMoved = gStepMovedBase;
		gTelemetryStatus = CeNav::kNavArrived;
		gTelemetryDist = 0.0f;
		CeTestReplyf(reply, "arrived at (%.1f,%.1f) dist=%.1fm moved=%.1fm replans=%d rec=%d",
		             pos.x, pos.y, curDist, gNav.totalMoved, gNav.replans, gNav.recoveries);
		return CE_TEST_DONE;
	}
	if (!AdvanceSimTick(gLastNavSimTime))
		return CE_TEST_PENDING;

	// If explicit walker/jet mode requested and we're in the wrong mode, pulse SPACE on sim ticks to morph cleanly
	// (BUTTON_MECH_MORPH is KEY_ONCE, so releasing SPACE between retries lets low-energy/special-move rejections recover).
	// Gated after AdvanceSimTick so a wantMorphJump SPACE press from the previous sim tick is never cancelled mid-tick.
	if (!drivingPlayUnit && be && !gNav.cfg.indoor)
	{
		const EBattleEngineState st = be->GetState();
		const bool needMorph = (mode == CeNav::kModeJet && st != BATTLE_ENGINE_STATE_JET) ||
		                       (mode == CeNav::kModeWalker && st != BATTLE_ENGINE_STATE_WALKER);
		if (st == BATTLE_ENGINE_STATE_MORPHING_INTO_JET || st == BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER)
		{
			gNavMorphTapHold = 0;
			gNavMorphRetryWait = 0;
			gInput.SetKey(kKeySpace, false);
			return CE_TEST_PENDING;
		}
		if (needMorph)
		{
			if (gNavMorphTapHold > 0)
			{
				--gNavMorphTapHold;
				if (gNavMorphTapHold == 0) gInput.SetKey(kKeySpace, false);
				return CE_TEST_PENDING;
			}
			if (gNavMorphRetryWait > 0)
			{
				--gNavMorphRetryWait;
				gInput.SetKey(kKeySpace, false);
				return CE_TEST_PENDING;
			}
			gInput.SetKey(kKeySpace, true);
			gNavMorphTapHold = 3;
			gNavMorphRetryWait = 15;
			return CE_TEST_PENDING;
		}
		gNavMorphTapHold = 0;
		gNavMorphRetryWait = 0;
		gInput.SetKey(kKeySpace, false);
	}

	CeNav::NavTickInput ti;
	ti.pos = pos;
	ti.yaw = yaw;
	ti.pitch = pitch;
	ti.vel = vel;
	ti.inJet = inJet;
	ti.energyFrac = (be && be->GetMaxEnergy() > 0.0f) ? (be->GetEnergy() / be->GetMaxEnergy()) : 0.0f;
	ti.desiredPitch = inJet ? ComputeJetDesiredPitch(pos, yaw) : 0.0f;

	SampleLiveFeelers(pos, yaw, gNav.cfg.feelerRange, inJet, FindSelfThing(be, pos),
	                  &ti.feelerCenter, &ti.feelerLeft, &ti.feelerRight);

	const CeNav::NavDriveOutput out = gNav.Step(ti);
	SyncNavTelemetry();

	if (out.status == CeNav::kNavArrived)
	{
		gInput.ReleaseAll();
		gStepMovedBase += gNav.totalMoved;
		gTelemetryMoved = gStepMovedBase;
		gTelemetryStatus = CeNav::kNavArrived;
		gTelemetryDist = 0.0f;
		CeTestReplyf(reply, "arrived at (%.1f,%.1f) dist=%.1fm moved=%.1fm replans=%d rec=%d",
		             pos.x, pos.y, out.distToGoal, gNav.totalMoved, gNav.replans, gNav.recoveries);
		return CE_TEST_DONE;
	}

	if (out.wantReplan)
	{
		RecordPenalty(out.penalizePos);
		++gTelemetryReplans;
		++gTelemetryRecoveries;
		if (gNav.replans >= gNav.cfg.maxReplans)
		{
			gInput.ReleaseAll();
			gTelemetryStatus = CeNav::kNavFailed;
			CeTestReplyf(reply, "nav replan limit (%d) reached at (%.1f,%.1f), dist=%.1fm",
			             gNav.cfg.maxReplans, pos.x, pos.y, out.distToGoal);
			return CE_TEST_FAILED;
		}
		CeNav::NavPath newPath;
		const CeNav::NavMode planMode = inJet ? CeNav::kModeJet : mode;
		if (PlanLivePath(CeNav::Vec2{pos.x, pos.y}, gNav.goal, planMode, gNav.cfg.indoor,
		                 be, newPath, true, ignoreGoalThing))
		{
			gNav.SetPath(newPath, true);
		}
		else
		{
			// Fallback direct waypoint so Jet-hop or recovery can still progress.
			newPath.Clear();
			newPath.Push(CeNav::Vec2{pos.x, pos.y});
			newPath.Push(gNav.goal);
			gNav.SetPath(newPath, true);
		}
	}

	ApplyDriveOutput(out, !drivingPlayUnit && !gNav.cfg.indoor);
	CeTestReplyf(reply, "moving dist=%.1fm wp=%d/%d rec=%d", out.distToGoal, gNav.wpIndex, gNav.path.count, gNav.recoveries);
	return CE_TEST_PENDING;
}

bool InitNavStep(CeNav::Vec2 start, CeNav::Vec2 goal, float tol, CeNav::NavMode mode,
                 bool playUnitTank, CBattleEngine *be, CThing *ignoreGoalThing = NULL)
{
	gInput.ReleaseAll();
	gPenaltyCount = 0;
	gLastNavSimTime = -1.0f;
	const bool indoor = IsIndoorAt(start.x, start.y) || IsIndoorAt(goal.x, goal.y);
	CeNav::NavConfig cfg = CeNav::NavConfig::Default(indoor, mode);
	if (tol > 0.0f) cfg.goalTol = tol;
	cfg.mouseSens = MouseSens();
	cfg.invertY = CAREER.GetInvertYAxis(0) != FALSE;
	cfg.invertJetY = CAREER.GetInvertJetYAxis(0) != FALSE;
	cfg.playUnitTank = playUnitTank;
	if (playUnitTank)
	{
		cfg.stallDist = 0.85f;
		cfg.stallTicks = 70;
	}

	gNav.Reset(cfg, goal);
	CeNav::NavPath path;
	if (!PlanLivePath(start, goal, mode, indoor, be, path, true, ignoreGoalThing))
	{
		// If walker route is separated by water and mode == auto, fallback to direct jet hop!
		if (mode == CeNav::kModeAuto && !indoor && !playUnitTank)
		{
			path.Clear();
			path.Push(start);
			path.Push(goal);
		}
		else
		{
			gTelemetryStatus = CeNav::kNavFailed;
			return false;
		}
	}
	gNav.SetPath(path, false);
	SyncNavTelemetry();
	return true;
}

// ---- Concrete Autonomous Playtest Verbs (CE_TEST_INPUT) ---------------------------------------------------------------

// 1. nav-to X Y [tol R] [mode walker|jet|auto]
CeTestResult VerbNavTo(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3) { CeTestReplyf(r, "usage: nav-to X Y [tol R] [mode walker|jet|auto]"); return CE_TEST_FAILED; }
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }

	CBattleEngine *be = GetBE();
	const bool pu = DrivingPlayUnit();
	if (!be && !pu) { CeTestReplyf(r, "no active Battle Engine or driven unit"); return CE_TEST_PENDING; }

	CeNav::NavMode mode = CeNav::kModeAuto;
	float tol = 0.0f;
	for (int i = 3; i + 1 < a.count; i += 2)
	{
		if (!strcmp(a.word[i], "tol")) tol = float(atof(a.word[i + 1]));
		else if (!strcmp(a.word[i], "mode")) mode = ParseMode(a.word[i + 1]);
	}

	if (!a.scratch[0])
	{
		CeNav::Vec3 pos{0, 0, 0};
		if (!GetPilotPose(&pos, NULL, NULL, NULL, NULL, NULL))
			return CE_TEST_PENDING;
		const CeNav::Vec2 goal{float(atof(a.word[1])), float(atof(a.word[2]))};
		if (!InitNavStep(CeNav::Vec2{pos.x, pos.y}, goal, tol, mode, pu, be))
		{
			CeTestReplyf(r, "no path from (%.1f,%.1f) to (%.1f,%.1f)", pos.x, pos.y, goal.x, goal.y);
			return CE_TEST_FAILED;
		}
		a.scratch[0] = 1;
	}
	return StepActiveNav(be, pu, mode, NULL, r);
}

// 2. nav-offset DX DY [tol R] [mode walker|jet|auto]
CeTestResult VerbNavOffset(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3) { CeTestReplyf(r, "usage: nav-offset DX DY [tol R] [mode walker|jet|auto]"); return CE_TEST_FAILED; }
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }

	CBattleEngine *be = GetBE();
	const bool pu = DrivingPlayUnit();
	if (!be && !pu) { CeTestReplyf(r, "no active pilot"); return CE_TEST_PENDING; }

	CeNav::NavMode mode = CeNav::kModeAuto;
	float tol = 0.0f;
	for (int i = 3; i + 1 < a.count; i += 2)
	{
		if (!strcmp(a.word[i], "tol")) tol = float(atof(a.word[i + 1]));
		else if (!strcmp(a.word[i], "mode")) mode = ParseMode(a.word[i + 1]);
	}

	if (!a.scratch[0])
	{
		CeNav::Vec3 pos{0, 0, 0};
		if (!GetPilotPose(&pos, NULL, NULL, NULL, NULL, NULL))
			return CE_TEST_PENDING;
		CeNav::Vec2 goal{pos.x + float(atof(a.word[1])), pos.y + float(atof(a.word[2]))};
		if (mode != CeNav::kModeJet)
			SnapPointWalkable(goal, false, IsIndoorAt(pos.x, pos.y) ? 8.0f : 28.0f, &goal);
		if (!InitNavStep(CeNav::Vec2{pos.x, pos.y}, goal, tol, mode, pu, be))
		{
			CeTestReplyf(r, "no path for offset (%.1f,%.1f) -> (%.1f,%.1f)",
			             atof(a.word[1]), atof(a.word[2]), goal.x, goal.y);
			return CE_TEST_FAILED;
		}
		a.scratch[0] = 1;
	}
	return StepActiveNav(be, pu, mode, NULL, r);
}

// 3. nav-explore [COUNT] [radius R] [mode walker|jet|auto]
CeTestResult VerbNavExplore(CeTestArgs &a, CeTestReply &r)
{
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }
	CBattleEngine *be = GetBE();
	const bool pu = DrivingPlayUnit();
	if (!be && !pu) { CeTestReplyf(r, "no active pilot"); return CE_TEST_PENDING; }

	if (!a.scratch[0])
	{
		CeNav::Vec3 pos{0, 0, 0};
		float yaw = 0.0f;
		if (!GetPilotPose(&pos, &yaw, NULL, NULL, NULL, NULL))
			return CE_TEST_PENDING;
		const bool indoor = IsIndoorAt(pos.x, pos.y);
		int wantCount = 3;
		float radius = indoor ? 18.0f : 80.0f;
		CeNav::NavMode mode = CeNav::kModeAuto;
		int idx = 1;
		if (idx < a.count && a.word[idx][0] >= '0' && a.word[idx][0] <= '9')
		{
			wantCount = CeNav::ClampI(atoi(a.word[idx]), 1, kMaxExploreTargets);
			++idx;
		}
		for (; idx + 1 < a.count; idx += 2)
		{
			if (!strcmp(a.word[idx], "radius")) radius = float(atof(a.word[idx + 1]));
			else if (!strcmp(a.word[idx], "mode")) mode = ParseMode(a.word[idx + 1]);
		}

		memset(&gExplore, 0, sizeof(gExplore));
		gExplore.mode = mode;
		gExplore.tol = indoor ? 3.5f : 8.5f;
		const bool museum = InMuseum();
		const CeSanctuaryCitadel::SolidLayout citadel = CeSanctuaryCitadel::MakeInteriorSolidLayout();
		const CeSanctuaryCitadel::SolidLayout *citPtr = PortCeSanctuaryActive() ? &citadel : NULL;

		// Sample compass sectors starting near current forward heading to find well-spaced dry waypoints.
		const float baseAngle = atan2f(cosf(yaw), -sinf(yaw));
		for (int s = 0; s < 16 && gExplore.count < wantCount; ++s)
		{
			// Golden-angle spread around the circle so consecutive waypoints explore distinct directions.
			const float ang = baseAngle + float(s) * 2.399963f;
			for (float frac = 0.85f; frac >= 0.35f; frac -= 0.25f)
			{
				const float dist = radius * frac;
				const float wx = pos.x + cosf(ang) * dist;
				const float wy = pos.y + sinf(ang) * dist;
				if (!PointDryAndWalkable(wx, wy, mode == CeNav::kModeJet, museum, citPtr)) continue;
				bool tooClose = false;
				for (int j = 0; j < gExplore.count; ++j)
					if (CeNav::Dist2D(gExplore.targets[j], CeNav::Vec2{wx, wy}) < radius * 0.35f)
						tooClose = true;
				if (tooClose) continue;
				// Confirm reachability with a quick A* check.
				CeNav::NavPath testPath;
				if (PlanLivePath(CeNav::Vec2{pos.x, pos.y}, CeNav::Vec2{wx, wy}, mode, indoor, be, testPath, false))
				{
					gExplore.targets[gExplore.count++] = CeNav::Vec2{wx, wy};
					break;
				}
			}
		}
		if (gExplore.count == 0)
		{
			CeTestReplyf(r, "no reachable explore waypoints around (%.1f,%.1f) within %.1fm", pos.x, pos.y, radius);
			return CE_TEST_FAILED;
		}
		gExplore.current = 0;
		gExplore.active = true;
		if (!InitNavStep(CeNav::Vec2{pos.x, pos.y}, gExplore.targets[0], gExplore.tol, mode, pu, be))
		{
			CeTestReplyf(r, "failed to plan first explore leg");
			return CE_TEST_FAILED;
		}
		a.scratch[0] = 1;
	}

	CeTestReply legReply{};
	const CeTestResult leg = StepActiveNav(be, pu, gExplore.mode, NULL, legReply);
	if (leg == CE_TEST_FAILED)
	{
		gExplore.active = false;
		CeTestReplyf(r, "explore leg %d/%d failed: %s", gExplore.current + 1, gExplore.count, legReply.text);
		return CE_TEST_FAILED;
	}
	if (leg == CE_TEST_DONE)
	{
		++gExplore.current;
		if (gExplore.current >= gExplore.count)
		{
			gExplore.active = false;
			gInput.ReleaseAll();
			CeTestReplyf(r, "explored %d waypoints, total moved=%.1fm", gExplore.count, gTelemetryMoved);
			return CE_TEST_DONE;
		}
		CeNav::Vec3 pos{0, 0, 0};
		GetPilotPose(&pos, NULL, NULL, NULL, NULL, NULL);
		if (!InitNavStep(CeNav::Vec2{pos.x, pos.y}, gExplore.targets[gExplore.current], gExplore.tol, gExplore.mode, pu, be))
		{
			gExplore.active = false;
			CeTestReplyf(r, "explore leg %d/%d unplannable", gExplore.current + 1, gExplore.count);
			return CE_TEST_FAILED;
		}
	}
	CeTestReplyf(r, "explore %d/%d: %s", gExplore.current + 1, gExplore.count, legReply.text);
	return CE_TEST_PENDING;
}

// 4. nav-nearest-enemy [range R] [mode walker|jet|auto]
CeTestResult VerbNavNearestEnemy(CeTestArgs &a, CeTestReply &r)
{
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }
	CBattleEngine *be = GetBE();
	if (!be) { CeTestReplyf(r, "no active Battle Engine"); return CE_TEST_PENDING; }

	float wantRange = 90.0f;
	CeNav::NavMode mode = CeNav::kModeAuto;
	for (int i = 1; i + 1 < a.count; i += 2)
	{
		if (!strcmp(a.word[i], "range")) wantRange = float(atof(a.word[i + 1]));
		else if (!strcmp(a.word[i], "mode")) mode = ParseMode(a.word[i + 1]);
	}

	float dist = 0.0f;
	CUnit *enemy = FindNearestHostile(be, &dist);
	if (!enemy)
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "no live hostile units");
		return CE_TEST_FAILED;
	}

	FVector ePos;
	enemy->GetTargetablePos(&ePos);
	const FVector eye = be->GetPos() + FVector(0.0f, 0.0f, -be->COfGHeight());
	if (dist <= wantRange && HasClearWorldLOS(be, eye, ePos, enemy))
	{
		gInput.ReleaseAll();
		gTelemetryStatus = CeNav::kNavArrived;
		gTelemetryDist = dist;
		CeTestReplyf(r, "in range %.1fm with LOS to %s #%d", dist, enemy->_GetClassName(), enemy->GetThingNumber());
		return CE_TEST_DONE;
	}

	// Plan initially and refresh every 80 sim ticks (~4s) when not mid stuck-recovery, so stallTicks (45) can trigger.
	static float sNearestSimTime = -1.0f;
	if (!a.scratch[0])
	{
		sNearestSimTime = -1.0f;
		a.scratch[1] = 0;
	}
	if (AdvanceSimTick(sNearestSimTime))
		++a.scratch[1];
	if (!a.scratch[0] || (gNav.recPhase == CeNav::kRecNone && a.scratch[1] >= 80))
	{
		const FVector bp = be->GetPos();
		CeNav::Vec2 goal{ePos.X, ePos.Y};
		if (mode != CeNav::kModeJet)
			SnapPointWalkable(goal, false, 24.0f, &goal);
		if (!InitNavStep(CeNav::Vec2{bp.X, bp.Y}, goal, CeNav::MaxF(12.0f, wantRange * 0.65f), mode, false, be, enemy))
		{
			CeTestReplyf(r, "no path to hostile %s at (%.1f,%.1f)", enemy->_GetClassName(), ePos.X, ePos.Y);
			return CE_TEST_FAILED;
		}
		a.scratch[0] = 1;
		a.scratch[1] = 0;
	}

	return StepActiveNav(be, false, mode, enemy, r);
}

// Helper: smoothly aim toward 3D world point (tx, ty, tz). Returns true when aligned within tolDeg.
bool AimStepToward(CBattleEngine *be, float tx, float ty, float tz, float tolDeg, float *outErrDeg)
{
	CeNav::Vec3 eye{0, 0, 0};
	float yaw = 0.0f, pitch = 0.0f;
	if (!GetAimOrigin(&eye, &yaw, &pitch)) return false;

	const float dx = tx - eye.x;
	const float dy = ty - eye.y;
	const float dz = tz - eye.z;
	const float yawErr = CeNav::SignedRightYawError(yaw, dx, dy);
	const float desPitch = CeNav::DesiredPitch(dx, dy, dz);
	const float clampedPitch = CeNav::ClampF(desPitch, -0.85f, 0.55f);
	const float err3d = CeNav::AngularError3D(yaw, pitch, dx, dy, dz) * CeNav::kRadToDeg;
	const float yawDeg = CeNav::AbsF(yawErr) * CeNav::kRadToDeg;
	const float pitchErrDeg = CeNav::AbsF(clampedPitch - pitch) * CeNav::kRadToDeg;
	if (outErrDeg) *outErrDeg = err3d;

	const bool isJet = (be->GetState() == BATTLE_ENGINE_STATE_JET);
	const bool invY = (isJet ? CAREER.GetInvertJetYAxis(0) : CAREER.GetInvertYAxis(0)) != FALSE;
	const float sens = MouseSens();

	const float mdx = CeNav::YawMouseDelta(yawErr, sens, 0.50f, 55.0f);
	const float mdy = CeNav::PitchMouseDelta(pitch, clampedPitch, invY, sens, 0.45f, 38.0f);
	gInput.MoveMouse(mdx, mdy);

	return (err3d <= tolDeg) || (yawDeg <= tolDeg && pitchErrDeg <= tolDeg);
}

// 5. aim-at X Y Z [tol DEG]
CeTestResult VerbAimAt(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 4) { CeTestReplyf(r, "usage: aim-at X Y Z [tol DEG]"); return CE_TEST_FAILED; }
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }
	CBattleEngine *be = GetBE();
	if (!be) { CeTestReplyf(r, "no active Battle Engine"); return CE_TEST_PENDING; }

	const float tx = float(atof(a.word[1]));
	const float ty = float(atof(a.word[2]));
	const float tz = float(atof(a.word[3]));
	float tolDeg = 6.0f;
	for (int i = 4; i + 1 < a.count; i += 2)
		if (!strcmp(a.word[i], "tol")) tolDeg = float(atof(a.word[i + 1]));

	float errDeg = 180.0f;
	if (AimStepToward(be, tx, ty, tz, tolDeg, &errDeg))
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "aimed at (%.1f,%.1f,%.1f) err=%.2f deg", tx, ty, tz, errDeg);
		return CE_TEST_DONE;
	}
	CeTestReplyf(r, "aiming err=%.1f deg", errDeg);
	return CE_TEST_PENDING;
}

// 6. aim-nearest-enemy [tol DEG]
CeTestResult VerbAimNearestEnemy(CeTestArgs &a, CeTestReply &r)
{
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }
	CBattleEngine *be = GetBE();
	if (!be) { CeTestReplyf(r, "no active Battle Engine"); return CE_TEST_PENDING; }

	float tolDeg = 6.0f;
	for (int i = 1; i + 1 < a.count; i += 2)
		if (!strcmp(a.word[i], "tol")) tolDeg = float(atof(a.word[i + 1]));

	float dist = 0.0f;
	CUnit *enemy = FindNearestHostile(be, &dist);
	if (!enemy)
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "no live hostile units");
		return CE_TEST_FAILED;
	}
	FVector ep;
	enemy->GetTargetablePos(&ep);
	float errDeg = 180.0f;
	if (AimStepToward(be, ep.X, ep.Y, ep.Z, tolDeg, &errDeg))
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "aimed at %s #%d dist=%.1fm err=%.2f deg",
		             enemy->_GetClassName(), enemy->GetThingNumber(), dist, errDeg);
		return CE_TEST_DONE;
	}
	CeTestReplyf(r, "tracking %s err=%.1f deg", enemy->_GetClassName(), errDeg);
	return CE_TEST_PENDING;
}

// 7. engage-enemy [ticks N] [maxrange R]
CeTestResult VerbEngageEnemy(CeTestArgs &a, CeTestReply &r)
{
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }
	CBattleEngine *be = GetBE();
	if (!be) { CeTestReplyf(r, "no active Battle Engine"); return CE_TEST_PENDING; }

	if (!a.scratch[0])
	{
		memset(&gCombat, 0, sizeof(gCombat));
		gCombat.targetThing = -1;
		gCombat.strafeDir = 1;
		gCombat.strafeTimer = 18;
		gCombat.maxRange = 110.0f;
		gCombat.durationSec = 3.0f;
		gCombat.startSimTime = EVENT_MANAGER.GetTime();
		gCombat.lastSimTime = -1.0f;
		gCombat.prevLife = be->GetLife();
		for (int i = 1; i + 1 < a.count; i += 2)
		{
			if (!strcmp(a.word[i], "ticks")) gCombat.durationSec = float(CeNav::MaxI(10, atoi(a.word[i + 1]))) / 60.0f;
			else if (!strcmp(a.word[i], "maxrange")) gCombat.maxRange = float(atof(a.word[i + 1]));
		}
		a.scratch[0] = 1;
	}

	const float elapsedSec = EVENT_MANAGER.GetTime() - gCombat.startSimTime;
	if (elapsedSec >= gCombat.durationSec)
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "engaged for %.2fs (life=%.0f/%.0f)", elapsedSec, be->GetLife(), be->GetMaxLife());
		return CE_TEST_DONE;
	}

	// Track locked target; if destroyed, succeed cleanly!
	CUnit *target = (gCombat.targetThing >= 0) ? FindUnitByNumber(gCombat.targetThing) : NULL;
	if (gCombat.targetThing >= 0 && (!target || !IsHostileUnit(target, be)))
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "target #%d destroyed after %.2fs", gCombat.targetThing, elapsedSec);
		return CE_TEST_DONE;
	}
	if (!target)
	{
		target = FindNearestHostile(be);
		if (!target)
		{
			gInput.ReleaseAll();
			CeTestReplyf(r, "no live hostile units remaining");
			return CE_TEST_DONE;
		}
		gCombat.targetThing = target->GetThingNumber();
	}

	const FVector myPos = be->GetPos();
	FVector tPos;
	target->GetTargetablePos(&tPos);
	const float dist = CeNav::Dist2D(CeNav::Vec2{myPos.X, myPos.Y}, CeNav::Vec2{tPos.X, tPos.Y});
	const FVector eye = myPos + FVector(0.0f, 0.0f, -be->COfGHeight());
	const bool hasLos = HasClearWorldLOS(be, eye, tPos, target);

	// Approach via A* if out of range or LOS blocked.
	if (dist > gCombat.maxRange || !hasLos)
	{
		gInput.SetBtn(kBtnLeft, false);
		gInput.SetKey(kKeyLCtrl, false);
		if (!gCombat.navPlanned || (gNav.recPhase == CeNav::kRecNone && gCombat.replanCooldown <= 0))
		{
			CeNav::Vec2 goal{tPos.X, tPos.Y};
			SnapPointWalkable(goal, false, 24.0f, &goal);
			InitNavStep(CeNav::Vec2{myPos.X, myPos.Y}, goal, CeNav::MaxF(18.0f, gCombat.maxRange * 0.7f),
			            CeNav::kModeAuto, false, be, target);
			gCombat.navPlanned = true;
			gCombat.replanCooldown = 80;
		}
		if (AdvanceSimTick(gCombat.lastSimTime))
			--gCombat.replanCooldown;
		CeTestReply navR{};
		StepActiveNav(be, false, CeNav::kModeAuto, target, navR);
		CeTestReplyf(r, "closing on %s dist=%.1fm", target->_GetClassName(), dist);
		return CE_TEST_PENDING;
	}

	gCombat.navPlanned = false;
	if (!AdvanceSimTick(gCombat.lastSimTime))
		return CE_TEST_PENDING;

	// Aim smoothly at target.
	float errDeg = 180.0f;
	const bool aligned = AimStepToward(be, tPos.X, tPos.Y, tPos.Z, 9.0f, &errDeg);

	// Overheat-aware burst fire on LMB.
	const bool overheated = be->IsWeaponOverheated() != FALSE;
	const float energyFrac = be->GetMaxEnergy() > 0.0f ? (be->GetEnergy() / be->GetMaxEnergy()) : 1.0f;
	const bool lowEnergy = be->IsEnergyWeapon() && energyFrac < 0.18f;
	++gCombat.fireCycle;
	const bool inBurstWindow = (gCombat.fireCycle % 16) < 11;
	const bool wantFire = aligned && !overheated && !lowEnergy && inBurstWindow;
	gInput.SetBtn(kBtnLeft, wantFire);

	// Detect incoming damage -> trigger Walker dodge sidestep.
	const float curLife = be->GetLife();
	const bool tookHit = (curLife < gCombat.prevLife - 0.5f);
	gCombat.prevLife = curLife;
	if (gCombat.dodgeCooldown > 0) --gCombat.dodgeCooldown;
	if (tookHit && gCombat.dodgeCooldown <= 0 && be->GetState() == BATTLE_ENGINE_STATE_WALKER)
	{
		gCombat.strafeDir = -gCombat.strafeDir;
		gCombat.dodgeHold = 4;
		gCombat.dodgeCooldown = 25;
	}

	// Strafe / evade while fighting, maintaining comfortable range.
	if (--gCombat.strafeTimer <= 0)
	{
		gCombat.strafeDir = -gCombat.strafeDir;
		gCombat.strafeTimer = 20;
	}
	gInput.SetKey(kKeyW, dist > gCombat.maxRange * 0.75f);
	gInput.SetKey(kKeyS, dist < 24.0f);
	gInput.SetKey(kKeyA, gCombat.strafeDir < 0);
	gInput.SetKey(kKeyD, gCombat.strafeDir > 0);
	if (gCombat.dodgeHold > 0)
	{
		gInput.SetKey(kKeyLCtrl, true);
		--gCombat.dodgeHold;
	}
	else
	{
		gInput.SetKey(kKeyLCtrl, false);
	}

	CeTestReplyf(r, "engaging %s dist=%.1fm err=%.1f fire=%d", target->_GetClassName(), dist, errDeg, wantFire ? 1 : 0);
	return CE_TEST_PENDING;
}

// 8. morph-to walker|jet
CeTestResult VerbMorphTo(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 2 || (strcasecmp(a.word[1], "walker") && strcasecmp(a.word[1], "jet")))
	{
		CeTestReplyf(r, "usage: morph-to walker|jet");
		return CE_TEST_FAILED;
	}
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }
	CBattleEngine *be = GetBE();
	if (!be) { CeTestReplyf(r, "no active Battle Engine"); return CE_TEST_PENDING; }

	if (!a.scratch[0])
	{
		gMorph.wantJet = (strcasecmp(a.word[1], "jet") == 0);
		gMorph.tapHold = 0;
		gMorph.retryWait = 0;
		gMorph.lastSimTime = -1.0f;
		a.scratch[0] = 1;
	}

	const EBattleEngineState st = be->GetState();
	const EBattleEngineState targetState = gMorph.wantJet ? BATTLE_ENGINE_STATE_JET : BATTLE_ENGINE_STATE_WALKER;
	if (st == targetState)
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "morphed to %s", gMorph.wantJet ? "jet" : "walker");
		return CE_TEST_DONE;
	}
	if (!AdvanceSimTick(gMorph.lastSimTime))
		return CE_TEST_PENDING;

	if (gMorph.tapHold > 0)
	{
		--gMorph.tapHold;
		if (gMorph.tapHold == 0) gInput.SetKey(kKeySpace, false);
		return CE_TEST_PENDING;
	}

	if (st == BATTLE_ENGINE_STATE_MORPHING_INTO_JET || st == BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER)
	{
		CeTestReplyf(r, "morphing...");
		return CE_TEST_PENDING;
	}

	if (gMorph.retryWait > 0)
	{
		--gMorph.retryWait;
		return CE_TEST_PENDING;
	}

	// Tap SPACE to initiate morph.
	gInput.SetKey(kKeySpace, true);
	gMorph.tapHold = 3;
	gMorph.retryWait = 20;
	CeTestReplyf(r, "tapped SPACE to morph to %s", gMorph.wantJet ? "jet" : "walker");
	return CE_TEST_PENDING;
}

// 9. dodge-step left|right
CeTestResult VerbDodgeStep(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 2 || (strcasecmp(a.word[1], "left") && strcasecmp(a.word[1], "right")))
	{
		CeTestReplyf(r, "usage: dodge-step left|right");
		return CE_TEST_FAILED;
	}
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }
	CBattleEngine *be = GetBE();
	if (!be || DrivingPlayUnit())
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "dodge-step requires active Walker Battle Engine");
		return CE_TEST_FAILED;
	}
	if (be->GetState() != BATTLE_ENGINE_STATE_WALKER)
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "Battle Engine must be in walker mode to dodge");
		return CE_TEST_FAILED;
	}

	if (!a.scratch[0])
	{
		memset(&gDodge, 0, sizeof(gDodge));
		gDodge.goRight = (strcasecmp(a.word[1], "right") == 0);
		gDodge.lastSimTime = -1.0f;
		a.scratch[0] = 1;
	}

	if (!AdvanceSimTick(gDodge.lastSimTime))
		return CE_TEST_PENDING;

	++gDodge.ticks;
	const NavKeySlot dirKey = gDodge.goRight ? kKeyD : kKeyA;

	if (gDodge.ticks <= 5)
	{
		gInput.SetKey(kKeyLCtrl, true);
		gInput.SetKey(dirKey, true);
	}
	else if (gDodge.ticks <= 10)
	{
		gInput.SetKey(kKeyLCtrl, false);
		gInput.SetKey(dirKey, true);
	}
	else
	{
		gInput.SetKey(kKeyLCtrl, false);
		gInput.SetKey(dirKey, false);
	}

	// Measure lateral velocity along RightXY(yaw) and dash state.
	const FVector vel = be->GetVelocity();
	const CeNav::Vec2 right = CeNav::RightXY(be->GetCurrentEulerOrientation().mYaw);
	const float latSpeed = (vel.X * right.x + vel.Y * right.y) * GAME_FR;
	const float signedLat = gDodge.goRight ? latSpeed : -latSpeed;
	if (signedLat > gDodge.peakLat) gDodge.peakLat = signedLat;
	if (be->GetWalkerPart() && be->GetWalkerPart()->GetDashCount() > 0)
		gDodge.sawDash = true;

	if (gDodge.ticks >= 16)
	{
		gInput.ReleaseAll();
		if (gDodge.sawDash || gDodge.peakLat > 1.5f)
		{
			CeTestReplyf(r, "dodge %s ok (dash=%d peak_lat=%.2f u/s)",
			             gDodge.goRight ? "right" : "left", gDodge.sawDash ? 1 : 0, gDodge.peakLat);
			return CE_TEST_DONE;
		}
		CeTestReplyf(r, "dodge %s produced insufficient lateral motion (peak_lat=%.2f u/s)",
		             gDodge.goRight ? "right" : "left", gDodge.peakLat);
		return CE_TEST_FAILED;
	}
	return CE_TEST_PENDING;
}

// 10. take-nearest-friendly [maxdist D]
CeTestResult VerbTakeNearestFriendly(CeTestArgs &a, CeTestReply &r)
{
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }

	if (DrivingPlayUnit())
	{
		gInput.ReleaseAll();
#ifdef CE_VERSION
		CeTestReplyf(r, "possessed %s (thing #%d)", CePlayUnitDrivenClass(), gTake.targetThing);
#else
		CeTestReplyf(r, "possessed unit");
#endif
		return CE_TEST_DONE;
	}

	CBattleEngine *be = GetBE();
	if (!be) { CeTestReplyf(r, "no active Battle Engine"); return CE_TEST_PENDING; }

	if (!a.scratch[0])
	{
		memset(&gTake, 0, sizeof(gTake));
		gTake.targetThing = -1;
		gTake.maxDist = 120.0f;
		for (int i = 1; i + 1 < a.count; i += 2)
			if (!strcmp(a.word[i], "maxdist")) gTake.maxDist = float(atof(a.word[i + 1]));
		a.scratch[0] = 1;
	}

	CUnit *target = (gTake.targetThing >= 0) ? FindUnitByNumber(gTake.targetThing) : NULL;
	if (!target || !IsFriendlyUnit(target, be))
	{
		target = FindNearestTakeableFriendly(be, gTake.maxDist);
		if (!target)
		{
			gInput.ReleaseAll();
			CeTestReplyf(r, "no eligible friendly unit within %.1fm", gTake.maxDist);
			return CE_TEST_FAILED;
		}
		gTake.targetThing = target->GetThingNumber();
		gTake.navPlanned = false;
	}

	const FVector myPos = be->GetPos();
	FVector tPos;
	target->GetTargetablePos(&tPos);
	const float dist = CeNav::Dist2D(CeNav::Vec2{myPos.X, myPos.Y}, CeNav::Vec2{tPos.X, tPos.Y});
	const FVector eye = myPos + FVector(0.0f, 0.0f, -be->COfGHeight());
	const bool hasLos = HasClearWorldLOS(be, eye, tPos, target);

	// Phase 0: navigate close enough (~32m) with clear LOS so reticle cleanly acquires the unit.
	if (dist > 34.0f || !hasLos)
	{
		gInput.SetKey(kKeyE, false);
		if (!gTake.navPlanned || (gNav.recPhase == CeNav::kRecNone && gTake.replanCooldown <= 0))
		{
			CeNav::Vec2 goal{tPos.X, tPos.Y};
			SnapPointWalkable(goal, false, 18.0f, &goal);
			InitNavStep(CeNav::Vec2{myPos.X, myPos.Y}, goal, 22.0f, CeNav::kModeAuto, false, be, target);
			gTake.navPlanned = true;
			gTake.replanCooldown = 80;
		}
		if (AdvanceSimTick(gTake.lastSimTime))
			--gTake.replanCooldown;
		CeTestReply navR{};
		StepActiveNav(be, false, CeNav::kModeAuto, target, navR);
		CeTestReplyf(r, "approaching friendly %s #%d dist=%.1fm", target->_GetClassName(), target->GetThingNumber(), dist);
		return CE_TEST_PENDING;
	}

	// Phase 1: stop moving, aim directly at unit (with slight vertical/lateral offset retries if needed), tap E.
	gInput.SetKey(kKeyW, false);
	gInput.SetKey(kKeyS, false);
	gInput.SetKey(kKeyA, false);
	gInput.SetKey(kKeyD, false);

	// Slight deterministic refinement offsets on retry attempts in case unit center != hull hitbox.
	static const float kJitterZ[5] = {0.0f, -0.8f, 0.6f, -1.5f, 0.0f};
	const float aimZ = tPos.Z + kJitterZ[gTake.attempts % 5];
	float errDeg = 180.0f;
	const bool aimed = AimStepToward(be, tPos.X, tPos.Y, aimZ, 4.5f, &errDeg);

	const bool simTick = AdvanceSimTick(gTake.lastSimTime);
	if (gTake.eHold > 0)
	{
		if (simTick && --gTake.eHold == 0)
			gInput.SetKey(kKeyE, false);
		return CE_TEST_PENDING;
	}
	if (gTake.eWait > 0)
	{
		if (simTick)
			--gTake.eWait;
		return CE_TEST_PENDING;
	}
	if (aimed)
	{
		gInput.SetKey(kKeyE, true);
		gTake.eHold = 3;
		gTake.eWait = 8;
		++gTake.attempts;
		CeTestReplyf(r, "pressing E on %s #%d (attempt %d, dist=%.1fm, err=%.1f)",
		             target->_GetClassName(), target->GetThingNumber(), gTake.attempts, dist, errDeg);
	}
	else
	{
		CeTestReplyf(r, "aiming at friendly %s #%d err=%.1f deg", target->_GetClassName(), target->GetThingNumber(), errDeg);
	}
	return CE_TEST_PENDING;
}

// 11. nav-drive-offset DX DY [tol R]
CeTestResult VerbNavDriveOffset(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3) { CeTestReplyf(r, "usage: nav-drive-offset DX DY [tol R]"); return CE_TEST_FAILED; }
	if (!InPlay()) { CeTestReplyf(r, "waiting for gameplay"); return CE_TEST_PENDING; }
	if (!DrivingPlayUnit())
	{
		gInput.ReleaseAll();
		CeTestReplyf(r, "nav-drive-offset requires an active possessed PlayUnit");
		return CE_TEST_FAILED;
	}

	float tol = 8.0f;
	for (int i = 3; i + 1 < a.count; i += 2)
		if (!strcmp(a.word[i], "tol")) tol = float(atof(a.word[i + 1]));

	if (!a.scratch[0])
	{
		CeNav::Vec3 pos{0, 0, 0};
		if (!GetPilotPose(&pos, NULL, NULL, NULL, NULL, NULL))
			return CE_TEST_PENDING;
		CeNav::Vec2 goal{pos.x + float(atof(a.word[1])), pos.y + float(atof(a.word[2]))};
		SnapPointWalkable(goal, false, 24.0f, &goal);
		if (!InitNavStep(CeNav::Vec2{pos.x, pos.y}, goal, tol, CeNav::kModeWalker, true, GetBE()))
		{
			CeTestReplyf(r, "no drivable path for offset (%.1f,%.1f)", atof(a.word[1]), atof(a.word[2]));
			return CE_TEST_FAILED;
		}
		a.scratch[0] = 1;
	}
	return StepActiveNav(GetBE(), true, CeNav::kModeWalker, NULL, r);
}

// 12. f9-clip [ticks N]
CeTestResult VerbF9Clip(CeTestArgs &a, CeTestReply &r)
{
	if (!PortCeBugClipActive || !PortCeBugClipStatus)
	{
		CeTestReplyf(r, "bug-clip recorder not linked in this build");
		return CE_TEST_FAILED;
	}

	if (!a.scratch[0])
	{
		memset(&gClip, 0, sizeof(gClip));
		gClip.recTicks = (a.count >= 2 && a.word[1][0] >= '0' && a.word[1][0] <= '9')
		                     ? unsigned(CeNav::MaxI(15, atoi(a.word[1])))
		                     : 90u;
		for (int i = 1; i + 1 < a.count; i += 2)
			if (!strcmp(a.word[i], "ticks")) gClip.recTicks = unsigned(CeNav::MaxI(15, atoi(a.word[i + 1])));
		gClip.lastSimTime = -1.0f;
		a.scratch[0] = 1;
	}

	if (InPlay() && !AdvanceSimTick(gClip.lastSimTime))
		return CE_TEST_PENDING;

	// Manage clean key pulse on F9.
	if (gClip.keyHold > 0)
	{
		--gClip.keyHold;
		if (gClip.keyHold == 0) gInput.SetKey(kKeyF9, false);
		return CE_TEST_PENDING;
	}

	if (gClip.phase == 0)
	{
		if (PortCeBugClipActive())
		{
			gClip.recStartedTick = a.tick;
			gClip.recStartSim = EVENT_MANAGER.GetTime();
			gClip.phase = 1;
			return CE_TEST_PENDING;
		}
		gInput.SetKey(kKeyF9, true);
		gClip.keyHold = 2;
		gClip.phase = 1;
		gClip.recStartedTick = a.tick;
		gClip.recStartSim = EVENT_MANAGER.GetTime();
		return CE_TEST_PENDING;
	}

	if (gClip.phase == 1)
	{
		if (!PortCeBugClipActive())
		{
			if (++gClip.startRetries > 12)
			{
				gInput.ReleaseAll();
				CeTestReplyf(r, "F9 did not start bug-clip recording (check CE mode / fences)");
				return CE_TEST_FAILED;
			}
			if ((gClip.startRetries % 4) == 0)
			{
				gInput.SetKey(kKeyF9, true);
				gClip.keyHold = 2;
			}
			return CE_TEST_PENDING;
		}
		const float wantSec = float(gClip.recTicks) / 60.0f;
		const bool doneRec = InPlay() ? ((EVENT_MANAGER.GetTime() - gClip.recStartSim) >= wantSec)
		                              : ((a.tick - gClip.recStartedTick) >= gClip.recTicks);
		if (!doneRec)
		{
			CeTestReplyf(r, "recording bug clip %.2f/%.2fs",
			             InPlay() ? (EVENT_MANAGER.GetTime() - gClip.recStartSim) : float(a.tick - gClip.recStartedTick) / 60.0f,
			             wantSec);
			return CE_TEST_PENDING;
		}
		// Stop recording with real F9 press.
		gInput.SetKey(kKeyF9, true);
		gClip.keyHold = 2;
		gClip.phase = 2;
		return CE_TEST_PENDING;
	}

	// Phase 2: wait for worker thread to finish encoding + zipping!
	++gClip.postStopTicks;
	char status[160] = "";
	int rec = 0;
	float fade = 0.0f;
	const int hasStatus = PortCeBugClipStatus(status, int(sizeof(status)), &rec, &fade);
	if (PortCeBugClipActive() || rec)
	{
		if (gClip.postStopTicks == 15)
		{
			gInput.SetKey(kKeyF9, true);
			gClip.keyHold = 3;
		}
		return CE_TEST_PENDING;
	}
	if (hasStatus)
	{
		if (strstr(status, "Saving bug clip"))
		{
			CeTestReplyf(r, "finalizing zip...");
			return CE_TEST_PENDING;
		}
		if (strstr(status, "Bug clip saved:"))
		{
			gInput.ReleaseAll();
			CeTestReplyf(r, "%s", status);
			return CE_TEST_DONE;
		}
		if (strstr(status, "Bug clip not saved:"))
		{
			gInput.ReleaseAll();
			CeTestReplyf(r, "%s", status);
			return CE_TEST_FAILED;
		}
	}
	if (gClip.postStopTicks > 30)
	{
		// Toast already faded after saving cleanly.
		gInput.ReleaseAll();
		CeTestReplyf(r, "bug clip finalized");
		return CE_TEST_DONE;
	}
	return CE_TEST_PENDING;
}

// ---- Read-Only Telemetry State Keys (CE_TEST_STATE) --------------------------------------------------------------------

bool StatePilotPos(char *out, size_t size)
{
	CeNav::Vec3 pos{0, 0, 0};
	if (!GetPilotPose(&pos, NULL, NULL, NULL, NULL, NULL)) return false;
	snprintf(out, size, "%.2f,%.2f,%.2f", pos.x, pos.y, pos.z);
	return true;
}

bool StatePilotYaw(char *out, size_t size)
{
	float yaw = 0.0f;
	if (!GetPilotPose(NULL, &yaw, NULL, NULL, NULL, NULL)) return false;
	snprintf(out, size, "%.2f", yaw * CeNav::kRadToDeg);
	return true;
}

bool StatePilotMode(char *out, size_t size)
{
	bool inJet = false, isPU = false;
	if (!GetPilotPose(NULL, NULL, NULL, NULL, &inJet, &isPU)) return false;
	snprintf(out, size, "%s", isPU ? "playunit" : (inJet ? "jet" : "walker"));
	return true;
}

bool StatePilotLife(char *out, size_t size)
{
	CBattleEngine *be = GetBE();
	if (!be) return false;
	snprintf(out, size, "%.1f,%.1f", be->GetLife(), be->GetMaxLife());
	return true;
}

bool StatePilotEnergy(char *out, size_t size)
{
	CBattleEngine *be = GetBE();
	if (!be) return false;
	snprintf(out, size, "%.1f,%.1f", be->GetEnergy(), be->GetMaxEnergy());
	return true;
}

bool StatePilotSpeed(char *out, size_t size)
{
	CeNav::Vec3 vel{0, 0, 0};
	bool isPU = false;
	if (!GetPilotPose(NULL, NULL, NULL, &vel, NULL, &isPU)) return false;
	const float speed = isPU ? vel.x : (sqrtf(vel.x * vel.x + vel.y * vel.y) * GAME_FR);
	snprintf(out, size, "%.2f", speed);
	return true;
}

bool StateWorldEnemies(char *out, size_t size)
{
	CBattleEngine *be = GetBE();
	if (!be) return false;
	int count = 0;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
		if (IsHostileUnit(u, be)) ++count;
	snprintf(out, size, "%d", count);
	return true;
}

bool StateWorldNearestEnemy(char *out, size_t size)
{
	CBattleEngine *be = GetBE();
	if (!be) return false;
	float dist = 0.0f;
	CUnit *enemy = FindNearestHostile(be, &dist);
	if (!enemy)
	{
		snprintf(out, size, "none");
		return true;
	}
	FVector ep;
	enemy->GetTargetablePos(&ep);
	snprintf(out, size, "%.2f,%.2f,%.2f,%.2f,%s", dist, ep.X, ep.Y, ep.Z, enemy->_GetClassName());
	return true;
}

bool StateWorldFriendlies(char *out, size_t size)
{
	CBattleEngine *be = GetBE();
	if (!be) return false;
	int count = 0;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
		if (IsFriendlyUnit(u, be)) ++count;
	snprintf(out, size, "%d", count);
	return true;
}

bool StateNavStatus(char *out, size_t size)
{
	static const char *const kNames[5] = {"idle", "moving", "recovering", "arrived", "failed"};
	const int idx = int(gTelemetryStatus);
	snprintf(out, size, "%s", (idx >= 0 && idx < 5) ? kNames[idx] : "idle");
	return true;
}

bool StateNavDist(char *out, size_t size)
{
	snprintf(out, size, "%.2f", gTelemetryDist);
	return true;
}

bool StateNavReplans(char *out, size_t size)
{
	snprintf(out, size, "%d", gTelemetryReplans);
	return true;
}

bool StateNavRecoveries(char *out, size_t size)
{
	snprintf(out, size, "%d", gTelemetryRecoveries);
	return true;
}

bool StateNavMoved(char *out, size_t size)
{
	snprintf(out, size, "%.2f", gTelemetryMoved);
	return true;
}

} // namespace

// ---- Registrations -----------------------------------------------------------------------------------------------------

CE_TEST_VERB("nav-to", "nav-to X Y [tol R] [mode walker|jet|auto]: A* + feeler closed-loop navigation", CE_TEST_INPUT, VerbNavTo);
CE_TEST_VERB("nav-offset", "nav-offset DX DY [tol R] [mode walker|jet|auto]: A* navigate relative to current position", CE_TEST_INPUT, VerbNavOffset);
CE_TEST_VERB("nav-explore", "nav-explore [COUNT] [radius R] [mode walker|jet|auto]: autonomously patrol reachable waypoints", CE_TEST_INPUT, VerbNavExplore);
CE_TEST_VERB("nav-nearest-enemy", "nav-nearest-enemy [range R] [mode walker|jet|auto]: A* approach nearest hostile with LOS", CE_TEST_INPUT, VerbNavNearestEnemy);
CE_TEST_VERB("aim-at", "aim-at X Y Z [tol DEG]: smoothly aim relative mouse at 3D point", CE_TEST_INPUT, VerbAimAt);
CE_TEST_VERB("aim-nearest-enemy", "aim-nearest-enemy [tol DEG]: smoothly aim at nearest live hostile unit", CE_TEST_INPUT, VerbAimNearestEnemy);
CE_TEST_VERB("engage-enemy", "engage-enemy [ticks N] [maxrange R]: closed-loop approach, aim, burst-fire, strafe & dodge", CE_TEST_INPUT, VerbEngageEnemy);
CE_TEST_VERB("morph-to", "morph-to walker|jet: morph Battle Engine cleanly to target mode", CE_TEST_INPUT, VerbMorphTo);
CE_TEST_VERB("dodge-step", "dodge-step left|right: execute Walker LCTRL+A/D dodge maneuver and verify dash", CE_TEST_INPUT, VerbDodgeStep);
CE_TEST_VERB("take-nearest-friendly", "take-nearest-friendly [maxdist D]: approach, aim at, and possess nearest eligible friendly unit", CE_TEST_INPUT, VerbTakeNearestFriendly);
CE_TEST_VERB("nav-drive-offset", "nav-drive-offset DX DY [tol R]: drive possessed PlayUnit smoothly with obstacle avoidance", CE_TEST_INPUT, VerbNavDriveOffset);
CE_TEST_VERB("f9-clip", "f9-clip [ticks N]: record a real F9 bug clip and wait until the zip finishes finalizing", CE_TEST_INPUT, VerbF9Clip);

CE_TEST_STATE("pilot.pos", StatePilotPos);
CE_TEST_STATE("pilot.yaw", StatePilotYaw);
CE_TEST_STATE("pilot.mode", StatePilotMode);
CE_TEST_STATE("pilot.life", StatePilotLife);
CE_TEST_STATE("pilot.energy", StatePilotEnergy);
CE_TEST_STATE("pilot.speed", StatePilotSpeed);
CE_TEST_STATE("world.enemies", StateWorldEnemies);
CE_TEST_STATE("world.nearest_enemy", StateWorldNearestEnemy);
CE_TEST_STATE("world.friendlies", StateWorldFriendlies);
CE_TEST_STATE("nav.status", StateNavStatus);
CE_TEST_STATE("nav.dist", StateNavDist);
CE_TEST_STATE("nav.replans", StateNavReplans);
CE_TEST_STATE("nav.recoveries", StateNavRecoveries);
CE_TEST_STATE("nav.moved", StateNavMoved);
