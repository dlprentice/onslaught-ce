#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host C++17 ASan+UBSan unit tests for port/game/ce_nav_planner.h.

Compiles and runs a standalone C++17 test harness with -Wall -Wextra -Werror
-fsanitize=address,undefined covering:
  1. A* routing around L-shaped walls, U-shaped dead-ends, water moats, and narrow
     Museum/Sanctuary doorways (never cutting diagonal corners).
  2. Dynamic obstacle blockage mid-path -> cell penalization -> A* replan around blockage.
  3. LOS string-pulling preserves wall clearance without clipping corners.
  4. Closed-loop stuck-recovery FSM (simulated physics body hits unmapped obstacle ->
     detects stall -> backs up -> strafes toward open feeler -> triggers replan -> arrives).
  5. Battle Engine orientation wraparound across the -pi/+pi seam, pitch sign (Z-down,
     both normal & inverted Y), and 3D angular error math.
  6. Pure C++14 compatibility check (matching the game's qcxx rule!).
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER_DIR = ROOT / "port/game"

HARNESS_CPP = r"""
#include "ce_nav_planner.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

using namespace CeNav;

static void Expect(bool cond, const char *msg)
{
	if (!cond)
	{
		fprintf(stderr, "FAIL: %s\n", msg);
		exit(1);
	}
}

static bool PathHitsAnyBlocked(const NavGrid &grid, const NavPath &path)
{
	for (int i = 0; i < path.count; ++i)
	{
		if (grid.IsWorldBlocked(path.pts[i].x, path.pts[i].y))
			return true;
		if (i > 0 && !GridLineClear(grid, path.pts[i - 1], path.pts[i], 0.0f, false))
			return true;
	}
	return false;
}

static void TestOrientationAndWraparound()
{
	// Yaw = 0 -> Forward = (0, 1), Right = (1, 0)
	Vec2 f0 = ForwardXY(0.0f);
	Vec2 r0 = RightXY(0.0f);
	Expect(AbsF(f0.x) < 1e-5f && AbsF(f0.y - 1.0f) < 1e-5f, "ForwardXY(0) == (0,1)");
	Expect(AbsF(r0.x - 1.0f) < 1e-5f && AbsF(r0.y) < 1e-5f, "RightXY(0) == (1,0)");

	// Target 45 deg to the right at (1, 1) -> SignedRightYawError == +pi/4 -> positive mouseDx
	float errR = SignedRightYawError(0.0f, 1.0f, 1.0f);
	Expect(AbsF(errR - kPi * 0.25f) < 1e-4f, "45 deg right error");
	Expect(YawMouseDelta(errR, 7.0f) > 0.0f, "positive dx turns right");

	// Target 45 deg to the left at (-1, 1) -> SignedRightYawError == -pi/4 -> negative mouseDx
	float errL = SignedRightYawError(0.0f, -1.0f, 1.0f);
	Expect(AbsF(errL + kPi * 0.25f) < 1e-4f, "45 deg left error");
	Expect(YawMouseDelta(errL, 7.0f) < 0.0f, "negative dx turns left");

	// Seam wraparound across -pi / +pi:
	// Facing almost south (slightly west: yaw = +pi - 0.10 -> Forward ~ (-0.10, -0.995), Right ~ (-0.995, +0.10)).
	// Remember: increasing yaw turns LEFT (+0.20 rad crosses +pi -> -pi + 0.10), decreasing yaw turns RIGHT.
	// Target at yaw = -pi + 0.10 (Forward ~ (+0.10, -0.995), slightly east of south) is 0.20 rad to our LEFT!
	float curYaw = kPi - 0.10f;
	float tgtYaw = -kPi + 0.10f;
	Vec2 tgtDir = ForwardXY(tgtYaw);
	float seamErrL = SignedRightYawError(curYaw, tgtDir.x, tgtDir.y);
	Expect(AbsF(seamErrL + 0.20f) < 1e-3f, "seam wraparound left (-0.20 rad)");
	Expect(YawMouseDelta(seamErrL, 7.0f) < 0.0f, "seam left gives negative dx");

	// Reverse across seam: from -pi + 0.10 (slightly east of south) toward +pi - 0.10 (slightly west of south)
	// -> 0.20 rad to our RIGHT (+0.20 rad, positive mouse dx)!
	Vec2 curDir = ForwardXY(curYaw);
	float seamErrR = SignedRightYawError(tgtYaw, curDir.x, curDir.y);
	Expect(AbsF(seamErrR - 0.20f) < 1e-3f, "seam wraparound right (+0.20 rad)");
	Expect(YawMouseDelta(seamErrR, 7.0f) > 0.0f, "seam right gives positive dx");

	// Pitch sign (Z grows DOWN): target ABOVE has dz < 0 -> negative DesiredPitch.
	float pUp = DesiredPitch(10.0f, 0.0f, -10.0f);
	Expect(AbsF(pUp + kPi * 0.25f) < 1e-4f, "target above -> negative pitch");
	float pDown = DesiredPitch(10.0f, 0.0f, 10.0f);
	Expect(AbsF(pDown - kPi * 0.25f) < 1e-4f, "target below -> positive pitch");

	// BattleEngine::UpdateMouseLookAngles:
	// invertY=false -> mPitch -= dy * scale -> to look UP (err < 0), need POSITIVE dy!
	// invertY=true  -> mPitch += dy * scale -> to look UP (err < 0), need NEGATIVE dy!
	Expect(PitchMouseDelta(0.0f, pUp, false, 7.0f) > 0.0f, "look up non-inverted -> +dy");
	Expect(PitchMouseDelta(0.0f, pUp, true, 7.0f) < 0.0f, "look up inverted -> -dy");
	Expect(PitchMouseDelta(0.0f, pDown, false, 7.0f) < 0.0f, "look down non-inverted -> -dy");
	Expect(PitchMouseDelta(0.0f, pDown, true, 7.0f) > 0.0f, "look down inverted -> +dy");

	// 3D angular error zero when aligned, pi/2 when orthogonal.
	Vec3 fAlign = Forward3D(0.7f, -0.3f);
	Expect(AngularError3D(0.7f, -0.3f, fAlign.x, fAlign.y, fAlign.z) < 1e-4f, "aligned 3D error == 0");
	Expect(AbsF(AngularError3D(0.0f, 0.0f, 1.0f, 0.0f, 0.0f) - kPi * 0.5f) < 1e-4f, "orthogonal 3D error == pi/2");
}

static void TestLWallUDeadEndWaterAndDoorway()
{
	NavGrid grid;
	AStarWorkspace ws;
	NavPath path;

	// 1. L-shaped wall + U-shaped dead-end pocket.
	grid.Init(0.0f, 0.0f, 2.0f, 40, 40);
	// U-shaped trap opening to the WEST around start (20, 40), goal to the EAST at (60, 40).
	grid.MarkBlockedRect(28.0f, 26.0f, 32.0f, 54.0f);	// east back wall of U
	grid.MarkBlockedRect(12.0f, 50.0f, 32.0f, 54.0f);	// north arm of U
	grid.MarkBlockedRect(12.0f, 26.0f, 32.0f, 30.0f);	// south arm of U
	// Extra L-shaped baffle beyond the U.
	grid.MarkBlockedRect(44.0f, 10.0f, 48.0f, 46.0f);
	grid.MarkBlockedRect(44.0f, 42.0f, 64.0f, 46.0f);
	grid.InflateSoftMargin();

	Vec2 start{20.0f, 40.0f};
	Vec2 goal{62.0f, 30.0f};
	Expect(PlanPath(grid, ws, start, goal, path, 0.8f), "A* solves U-dead-end + L-wall");
	Expect(path.count >= 4 && path.count < ws.rawCount, "string-pulling reduces waypoint count");
	Expect(!PathHitsAnyBlocked(grid, path), "pulled path never crosses blocked cells");
	Expect(Dist2D(path.pts[path.count - 1], goal) < 0.01f, "ends at exact goal");

	// 2. Water moat with a single narrow 3m causeway bridge (like archipelago/Sanctuary ramp).
	grid.Init(0.0f, 0.0f, 1.0f, 60, 40);
	grid.MarkBlockedRect(24.0f, 0.0f, 34.0f, 40.0f);	// full water moat
	// Carve narrow bridge at y in [18..21]
	for (int y = 18; y <= 20; ++y)
		for (int x = 24; x <= 34; ++x)
			grid.SetBlocked(x, y, false);
	grid.InflateSoftMargin();

	Vec2 mStart{8.0f, 8.0f}, mGoal{50.0f, 32.0f};
	Expect(PlanPath(grid, ws, mStart, mGoal, path, 0.45f), "A* crosses narrow bridge over water moat");
	Expect(!PathHitsAnyBlocked(grid, path), "bridge path avoids water completely");
	// Verify path actually passes through the bridge band y in [17.5..21.5] when crossing x in [24..34].
	bool crossedBridge = false;
	for (int i = 0; i < path.count; ++i)
		if (path.pts[i].x >= 24.0f && path.pts[i].x <= 34.0f && path.pts[i].y >= 17.5f && path.pts[i].y <= 21.5f)
			crossedBridge = true;
	// Also check interpolated samples along pulled segments.
	for (int i = 1; i < path.count; ++i)
	{
		for (int k = 0; k <= 20; ++k)
		{
			float t = float(k) / 20.0f;
			float sx = path.pts[i - 1].x + (path.pts[i].x - path.pts[i - 1].x) * t;
			float sy = path.pts[i - 1].y + (path.pts[i].y - path.pts[i - 1].y) * t;
			if (sx >= 25.0f && sx <= 33.0f)
			{
				Expect(sy >= 17.5f && sy <= 21.5f, "every moat crossing point stays on bridge");
				crossedBridge = true;
			}
		}
	}
	Expect(crossedBridge, "path actually crossed bridge");

	// 3. Narrow 2m Museum/Sanctuary doorway + corner-cutting prevention check!
	grid.Init(0.0f, 0.0f, 1.0f, 30, 30);
	// Diagonal pinch: (10,10) and (11,11) blocked; A* must NOT step diagonally between (10,11) and (11,10)!
	grid.MarkBlockedRect(15.0f, 0.0f, 15.9f, 13.9f);
	grid.MarkBlockedRect(15.0f, 16.0f, 15.9f, 29.9f);	// 2m wide doorway at y=14,15
	grid.InflateSoftMargin();
	Expect(PlanPath(grid, ws, Vec2{5.0f, 5.0f}, Vec2{25.0f, 25.0f}, path, 0.4f), "A* passes 2m doorway");
	Expect(!PathHitsAnyBlocked(grid, path), "doorway path clips zero wall corners");

	// 4. Completely sealed diagonal pinch must be unreachable when allowPartial=false!
	grid.Init(0.0f, 0.0f, 1.0f, 20, 20);
	for (int i = 0; i < 20; ++i) grid.SetBlocked(i, 19 - i, true);	// diagonal wall corner-to-corner
	Expect(!FindPathAStar(grid, ws, Vec2{2.5f, 2.5f}, Vec2{17.5f, 17.5f}, path, false, 0),
	       "diagonal corner-to-corner wall blocks diagonal squeezing");
}

static void TestDynamicObstacleAndPenalizedReplan()
{
	NavGrid grid;
	AStarWorkspace ws;
	NavPath p1, p2;

	// Two parallel corridors: short lower corridor (y=10) and longer upper corridor (y=28).
	grid.Init(0.0f, 0.0f, 2.0f, 40, 24);
	grid.MarkBlockedRect(18.0f, 14.0f, 60.0f, 24.0f);	// central divider
	grid.MarkBlockedRect(0.0f, 0.0f, 80.0f, 4.0f);		// south boundary
	grid.MarkBlockedRect(0.0f, 38.0f, 80.0f, 48.0f);	// north boundary
	grid.InflateSoftMargin();

	Vec2 start{8.0f, 10.0f}, goal{70.0f, 10.0f};
	Expect(PlanPath(grid, ws, start, goal, p1, 0.8f), "initial path uses short lower corridor");
	bool usedLower = true;
	for (int i = 0; i < p1.count; ++i)
		if (p1.pts[i].y > 20.0f) usedLower = false;
	Expect(usedLower, "initial route stays in lower corridor");

	// Mid-path, an unmapped dynamic wreck blocks the lower corridor at x=38, y=10 -> penalize!
	grid.PenalizeDisk(38.0f, 10.0f, 6.0f, 200);
	Expect(PlanPath(grid, ws, start, goal, p2, 0.8f), "replan succeeds around penalized corridor");
	bool reroutedUpper = false;
	for (int i = 0; i < p2.count; ++i)
		if (p2.pts[i].y > 24.0f) reroutedUpper = true;
	Expect(reroutedUpper, "penalized A* reroutes cleanly via upper corridor");
}

static void TestClosedLoopPhysicsStuckRecovery()
{
	// Simulate a Battle Engine Walker driving from (8, 20) to (68, 20).
	// Known map is open, EXCEPT an UNMAPPED physical pillar/wreck sits dead-center at (36, 20), radius 5.5m!
	// The controller must:
	//   1. Drive using real yaw mouse deltas + W/A/S/D.
	//   2. Hit or feel the unmapped pillar -> detect stall / engage recovery (backup + open-side strafe + penalize + replan).
	//   3. Steer around the pillar cleanly and arrive at (68, 20) within tolerance WITHOUT ever getting permanently stuck!
	NavGrid grid;
	AStarWorkspace ws;
	grid.Init(0.0f, 0.0f, 2.0f, 44, 24);
	grid.InflateSoftMargin();

	const Vec2 start{8.0f, 20.0f};
	const Vec2 goal{68.0f, 20.0f};
	const Vec2 hiddenPillar{36.0f, 20.0f};
	const float pillarR = 5.5f;

	NavConfig cfg = NavConfig::Default(false, kModeWalker);
	cfg.goalTol = 4.0f;
	cfg.stallTicks = 30;
	cfg.backupTicks = 12;
	cfg.strafeTicks = 18;

	NavController nav;
	nav.Reset(cfg, goal);
	NavPath initial;
	Expect(PlanPath(grid, ws, start, goal, initial, 0.9f), "initial open-field plan");
	nav.SetPath(initial, false);

	Vec3 pos{start.x, start.y, 0.0f};
	float yaw = -kPi * 0.5f;		// facing east (+X): ForwardXY(-pi/2) == (+1, 0)
	float pitch = 0.0f;
	const float dt = 1.0f / 20.0f;
	const float walkSpeed = 12.0f;

	// Helper raycast against hidden physical pillar (simulating WORLD.FindFirstThingToHitLine)
	auto rayPillar = [&](Vec2 o, Vec2 d, float maxD) -> float {
		float best = GridRayFeeler(grid, o, d.x, d.y, maxD);
		// Ray-circle intersection
		float ox = o.x - hiddenPillar.x;
		float oy = o.y - hiddenPillar.y;
		float b = ox * d.x + oy * d.y;
		float c = ox * ox + oy * oy - (pillarR + 1.0f) * (pillarR + 1.0f);
		float disc = b * b - c;
		if (disc >= 0.0f)
		{
			float t = -b - sqrtf(disc);
			if (t >= 0.0f && t < best) best = t;
		}
		return best;
	};

	bool arrived = false;
	for (int tick = 0; tick < 600; ++tick)
	{
		NavTickInput ti{};
		ti.pos = pos;
		ti.yaw = yaw;
		ti.pitch = pitch;
		ti.inJet = false;
		ti.energyFrac = 1.0f;
		ti.desiredPitch = 0.0f;

		// During first 80 ticks simulate an invisible/low obstacle (no feeler hit) to force full stall->backup->strafe->replan!
		bool blindHit = (tick < 110);
		if (blindHit)
		{
			ti.feelerCenter = cfg.feelerRange;
			ti.feelerLeft = cfg.feelerRange;
			ti.feelerRight = cfg.feelerRange * 0.4f;	// left side more open
		}
		else
		{
			ti.feelerCenter = rayPillar(Vec2{pos.x, pos.y}, ForwardXY(yaw), cfg.feelerRange);
			ti.feelerLeft = rayPillar(Vec2{pos.x, pos.y}, ForwardXY(yaw + 0.56f), cfg.feelerRange);
			ti.feelerRight = rayPillar(Vec2{pos.x, pos.y}, ForwardXY(yaw - 0.56f), cfg.feelerRange);
		}

		NavDriveOutput out = nav.Step(ti);
		if (out.status == kNavArrived)
		{
			arrived = true;
			break;
		}
		if (out.wantReplan)
		{
			grid.PenalizeDisk(out.penalizePos.x, out.penalizePos.y, 7.0f, 180);
			NavPath replanned;
			Expect(PlanPath(grid, ws, Vec2{pos.x, pos.y}, goal, replanned, 0.9f), "stuck replan finds detour");
			nav.SetPath(replanned, true);
		}

		// Apply genuine Battle Engine mouse look & WASD kinematics.
		yaw = WrapAngle(yaw - out.mouseDx * (cfg.mouseSens * 0.002f));
		Vec2 fwd = ForwardXY(yaw);
		Vec2 right = RightXY(yaw);
		float vx = 0.0f, vy = 0.0f;
		if (out.forward) { vx += fwd.x; vy += fwd.y; }
		if (out.back) { vx -= fwd.x; vy -= fwd.y; }
		if (out.strafeLeft) { vx -= right.x; vy -= right.y; }
		if (out.strafeRight) { vx += right.x; vy += right.y; }
		float vlen = sqrtf(vx * vx + vy * vy);
		if (vlen > 1e-4f)
		{
			vx = (vx / vlen) * walkSpeed;
			vy = (vy / vlen) * walkSpeed;
		}
		Vec2 next{pos.x + vx * dt, pos.y + vy * dt};
		// Hard collision against hidden physical pillar (push back out of circle!).
		float pdx = next.x - hiddenPillar.x;
		float pdy = next.y - hiddenPillar.y;
		float pd = sqrtf(pdx * pdx + pdy * pdy);
		if (pd < pillarR + 1.2f)
		{
			float push = (pillarR + 1.2f) / (pd > 1e-4f ? pd : 1.0f);
			next.x = hiddenPillar.x + pdx * push;
			next.y = hiddenPillar.y + pdy * push;
		}
		pos.x = next.x;
		pos.y = next.y;
	}

	Expect(arrived, "Walker recovers from unmapped obstacle and reaches goal");
	Expect(nav.recoveries >= 1, "stuck-recovery FSM triggered on blind collision");
	Expect(nav.replans >= 1, "A* replan executed after recovery");
	Expect(nav.totalMoved > 55.0f, "totalMoved tracked realistic traversed distance");
}

static void TestPlayUnitTankSteering()
{
	NavGrid grid;
	AStarWorkspace ws;
	grid.Init(0.0f, 0.0f, 2.0f, 32, 32);
	grid.InflateSoftMargin();

	NavConfig cfg = NavConfig::Default(false, kModeWalker);
	cfg.playUnitTank = true;
	cfg.goalTol = 3.0f;
	NavController nav;
	nav.Reset(cfg, Vec2{40.0f, 40.0f});
	NavPath path;
	Expect(PlanPath(grid, ws, Vec2{10.0f, 10.0f}, Vec2{40.0f, 40.0f}, path), "tank path");
	nav.SetPath(path, false);

	Vec3 pos{10.0f, 10.0f, 0.0f};
	float yaw = 0.0f;	// facing north (+Y), goal is NE -> needs right turn (D) + forward (W)
	bool arrived = false;
	for (int tick = 0; tick < 400; ++tick)
	{
		NavTickInput ti{};
		ti.pos = pos;
		ti.yaw = yaw;
		ti.feelerCenter = ti.feelerLeft = ti.feelerRight = cfg.feelerRange;
		NavDriveOutput out = nav.Step(ti);
		if (out.status == kNavArrived) { arrived = true; break; }
		if (out.strafeRight) yaw = WrapAngle(yaw - 0.08f);
		if (out.strafeLeft) yaw = WrapAngle(yaw + 0.08f);
		Vec2 f = ForwardXY(yaw);
		if (out.forward) { pos.x += f.x * 0.45f; pos.y += f.y * 0.45f; }
		if (out.back) { pos.x -= f.x * 0.35f; pos.y -= f.y * 0.35f; }
	}
	Expect(arrived, "possessed tank steers smoothly with W/A/S/D to goal");
}

int main()
{
	TestOrientationAndWraparound();
	TestLWallUDeadEndWaterAndDoorway();
	TestDynamicObstacleAndPenalizedReplan();
	TestClosedLoopPhysicsStuckRecovery();
	TestPlayUnitTankSteering();
	printf("ALL CE_NAV_PLANNER TESTS PASSED\n");
	return 0;
}
"""


def main() -> int:
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        sys.exit("a C++ compiler (clang++ or g++) is required")

    with tempfile.TemporaryDirectory(prefix="ce-nav-test-") as td:
        work = Path(td)
        src = work / "ce_nav_harness.cpp"
        src.write_text(HARNESS_CPP)
        exe17 = work / "ce-nav-tests-cpp17"
        exe14 = work / "ce-nav-tests-cpp14"

        # 1. Strict C++17 + ASan + UBSan build & run
        cmd17 = [
            compiler, "-std=c++17", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            f"-I{HEADER_DIR}", str(src), "-o", str(exe17), "-lm",
        ]
        res17 = subprocess.run(cmd17, capture_output=True, text=True)
        if res17.returncode != 0:
            sys.stderr.write(res17.stdout + res17.stderr)
            return res17.returncode

        run17 = subprocess.run([str(exe17)], capture_output=True, text=True, timeout=30)
        if run17.returncode != 0:
            sys.stderr.write(run17.stdout + run17.stderr)
            return run17.returncode

        # 2. Strict C++14 compatibility check (matches port/build.py qcxx rule!)
        cmd14 = [
            compiler, "-std=gnu++14", "-O2",
            "-Wall", "-Wextra", "-Werror",
            f"-I{HEADER_DIR}", str(src), "-o", str(exe14), "-lm",
        ]
        res14 = subprocess.run(cmd14, capture_output=True, text=True)
        if res14.returncode != 0:
            sys.stderr.write(res14.stdout + res14.stderr)
            return res14.returncode

        run14 = subprocess.run([str(exe14)], capture_output=True, text=True, timeout=30)
        if run14.returncode != 0:
            sys.stderr.write(run14.stdout + run14.stderr)
            return run14.returncode

        print(f"PASS ce_nav_testkit_tests ({Path(compiler).name} C++17 ASan+UBSan + gnu++14): {run17.stdout.strip()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
