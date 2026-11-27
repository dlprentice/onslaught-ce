// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_nav_planner.h: pure, header-only, engine-independent 2D A* pathfinder,
// LOS string-pulling, 3-ray obstacle feeler + stuck-recovery FSM, and Battle Engine
// orientation/steering math for the autonomous playtest kit (ce_nav_testkit.cpp).
//
// Constraints:
// - Pure C++14/C++17 with zero heap allocations and no STL containers so it compiles
//   identically under the game's qcxx rule (which renames operator new) and under
//   standalone host C++17 ASan/UBSan tests (-Wall -Wextra -Werror).
// - Immune to legacy min/max macros from engine headers.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace CeNav
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kRadToDeg = 57.2957795130823208768f;
constexpr float kDegToRad = 0.01745329251994329577f;

constexpr int kMaxGridW = 96;
constexpr int kMaxGridH = 96;
constexpr int kMaxGridCells = kMaxGridW * kMaxGridH;
constexpr int kMaxWaypoints = 256;

inline float MinF(float a, float b) { return a < b ? a : b; }
inline float MaxF(float a, float b) { return a > b ? a : b; }
inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float AbsF(float v) { return v < 0.0f ? -v : v; }
inline int MinI(int a, int b) { return a < b ? a : b; }
inline int MaxI(int a, int b) { return a > b ? a : b; }
inline int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Vec2
{
	float x;
	float y;
};

struct Vec3
{
	float x;
	float y;
	float z;
};

inline float Dist2D(Vec2 a, Vec2 b)
{
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	return sqrtf(dx * dx + dy * dy);
}

inline float Dist2DSq(Vec2 a, Vec2 b)
{
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	return dx * dx + dy * dy;
}

// ---- Battle Engine / PlayUnit Orientation & Steering Math --------------------------------------------------------------
// Engine axes: X east, Y north, Z grows DOWN (higher elevation = smaller Z).
// FMatrix::MakeRotationF(yaw, pitch, roll) gives:
//   Forward = (-cos(pitch)*sin(yaw), cos(pitch)*cos(yaw), sin(pitch))
//   Right   = ( cos(yaw),            sin(yaw),            0)  (at roll = 0)
// Positive DirectInput mouse dx DECREASES yaw (turns RIGHT).
// Positive DirectInput mouse dy changes pitch by +(invertY ? +1 : -1).

inline float WrapAngle(float rad)
{
	if (!isfinite(rad)) return 0.0f;
	float a = fmodf(rad + kPi, kTwoPi);
	if (a < 0.0f) a += kTwoPi;
	return a - kPi;
}

inline Vec2 ForwardXY(float yaw)
{
	return Vec2{-sinf(yaw), cosf(yaw)};
}

inline Vec2 RightXY(float yaw)
{
	return Vec2{cosf(yaw), sinf(yaw)};
}

inline Vec3 Forward3D(float yaw, float pitch)
{
	const float cp = cosf(pitch);
	return Vec3{-cp * sinf(yaw), cp * cosf(yaw), sinf(pitch)};
}

// Signed horizontal angle from current forward toward (dx, dy), in [-pi, +pi].
// Positive means target is to the RIGHT (requires positive mouse dx).
// Negative means target is to the LEFT (requires negative mouse dx).
inline float SignedRightYawError(float curYaw, float dx, float dy)
{
	if (!isfinite(curYaw) || !isfinite(dx) || !isfinite(dy)) return 0.0f;
	if (dx * dx + dy * dy < 1e-10f) return 0.0f;
	const Vec2 fwd = ForwardXY(curYaw);
	const Vec2 right = RightXY(curYaw);
	const float rightDot = dx * right.x + dy * right.y;
	const float fwdDot = dx * fwd.x + dy * fwd.y;
	return atan2f(rightDot, fwdDot);
}

// Desired engine pitch (radians) to aim along (dx, dy, dz), where Z grows DOWN.
// Negative pitch looks UP (dz < 0); positive pitch looks DOWN (dz > 0).
inline float DesiredPitch(float dx, float dy, float dz)
{
	if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz)) return 0.0f;
	const float horiz = sqrtf(dx * dx + dy * dy);
	if (horiz < 1e-5f && AbsF(dz) < 1e-5f) return 0.0f;
	return atan2f(dz, horiz);
}

// 3D angular error (radians, [0, pi]) between current (yaw, pitch) forward and vector (dx, dy, dz).
inline float AngularError3D(float curYaw, float curPitch, float dx, float dy, float dz)
{
	const float len = sqrtf(dx * dx + dy * dy + dz * dz);
	if (!(len > 1e-5f)) return 0.0f;
	const Vec3 f = Forward3D(curYaw, curPitch);
	const float dot = ClampF((f.x * dx + f.y * dy + f.z * dz) / len, -1.0f, 1.0f);
	return acosf(dot);
}

// Convert desired right-turn error (radians, >0 turns right) into one tick's relative mouse dx.
// Walker UpdateMouseLookAngles uses: deltaYaw = -dx * (sens * 0.002f).
inline float YawMouseDelta(float signedRightErrRad, float mouseSens, float gain = 0.55f, float maxDelta = 65.0f)
{
	if (!isfinite(signedRightErrRad)) return 0.0f;
	const float sens = mouseSens > 0.5f ? mouseSens : 7.0f;
	const float countsPerRad = 1.0f / (sens * 0.002f);
	const float raw = signedRightErrRad * countsPerRad * gain;
	return ClampF(raw, -maxDelta, maxDelta);
}

// Convert desired pitch error (desiredPitch - curPitch) into one tick's relative mouse dy.
// Walker UpdateMouseLookAngles uses: deltaPitch = (invertY ? +1 : -1) * dy * (sens * 0.001f).
inline float PitchMouseDelta(float curPitchRad, float desiredPitchRad, bool invertY,
                             float mouseSens, float gain = 0.50f, float maxDelta = 45.0f)
{
	if (!isfinite(curPitchRad) || !isfinite(desiredPitchRad)) return 0.0f;
	const float err = desiredPitchRad - curPitchRad;
	const float sens = mouseSens > 0.5f ? mouseSens : 7.0f;
	const float countsPerRad = 1.0f / (sens * 0.001f);
	const float sign = invertY ? 1.0f : -1.0f;
	const float raw = sign * err * countsPerRad * gain;
	return ClampF(raw, -maxDelta, maxDelta);
}

// ---- 2D Occupancy Grid & Binary-Heap A* Pathfinder --------------------------------------------------------------------

enum CellFlags
{
	kCellOpen = 0,
	kCellBlocked = 1,
	kCellSoftHazard = 2
};

struct NavGrid
{
	float originX;
	float originY;
	float cellSize;
	int width;
	int height;
	uint8_t flags[kMaxGridCells];
	uint8_t penalty[kMaxGridCells];

	void Init(float ox, float oy, float cs, int w, int h)
	{
		originX = ox;
		originY = oy;
		cellSize = cs > 0.25f ? cs : 1.0f;
		width = ClampI(w, 4, kMaxGridW);
		height = ClampI(h, 4, kMaxGridH);
		const int count = width * height;
		memset(flags, 0, size_t(count));
		memset(penalty, 0, size_t(count));
	}

	// Build a grid covering both start and goal plus margin, scaling cellSize up if needed
	// so the full bounding box always fits inside maxCells x maxCells.
	void InitAround(float sx, float sy, float gx, float gy, float minCellSize, float margin, int maxCells = kMaxGridW)
	{
		const int lim = ClampI(maxCells, 16, kMaxGridW);
		const float loX = MinF(sx, gx) - margin;
		const float hiX = MaxF(sx, gx) + margin;
		const float loY = MinF(sy, gy) - margin;
		const float hiY = MaxF(sy, gy) + margin;
		const float spanX = hiX - loX;
		const float spanY = hiY - loY;
		const float maxSpan = MaxF(spanX, spanY);

		float cs = minCellSize > 0.5f ? minCellSize : 1.0f;
		const float needed = maxSpan / float(lim - 2);
		if (needed > cs) cs = ceilf(needed * 2.0f) * 0.5f;

		int w = int(ceilf(spanX / cs)) + 2;
		int h = int(ceilf(spanY / cs)) + 2;
		w = ClampI(w, 8, lim);
		h = ClampI(h, 8, lim);

		const float cx = 0.5f * (loX + hiX);
		const float cy = 0.5f * (loY + hiY);
		Init(cx - 0.5f * float(w) * cs, cy - 0.5f * float(h) * cs, cs, w, h);
	}

	inline bool InBounds(int cx, int cy) const
	{
		return cx >= 0 && cy >= 0 && cx < width && cy < height;
	}

	inline int Index(int cx, int cy) const { return cy * width + cx; }

	inline bool WorldToCell(float wx, float wy, int *outCx, int *outCy) const
	{
		const int cx = int(floorf((wx - originX) / cellSize));
		const int cy = int(floorf((wy - originY) / cellSize));
		if (outCx) *outCx = cx;
		if (outCy) *outCy = cy;
		return InBounds(cx, cy);
	}

	inline Vec2 CellCenter(int cx, int cy) const
	{
		return Vec2{originX + (float(cx) + 0.5f) * cellSize,
		            originY + (float(cy) + 0.5f) * cellSize};
	}

	inline bool IsBlocked(int cx, int cy) const
	{
		if (!InBounds(cx, cy)) return true;
		return (flags[Index(cx, cy)] & kCellBlocked) != 0;
	}

	inline bool IsWorldBlocked(float wx, float wy) const
	{
		int cx = 0, cy = 0;
		if (!WorldToCell(wx, wy, &cx, &cy)) return true;
		return IsBlocked(cx, cy);
	}

	inline void SetBlocked(int cx, int cy, bool blocked)
	{
		if (!InBounds(cx, cy)) return;
		const int idx = Index(cx, cy);
		if (blocked) flags[idx] |= kCellBlocked;
		else flags[idx] &= uint8_t(~kCellBlocked);
	}

	void MarkBlockedDisk(float wx, float wy, float radius)
	{
		if (!(radius > 0.0f)) return;
		const int minCx = MaxI(0, int(floorf((wx - radius - originX) / cellSize)));
		const int maxCx = MinI(width - 1, int(floorf((wx + radius - originX) / cellSize)));
		const int minCy = MaxI(0, int(floorf((wy - radius - originY) / cellSize)));
		const int maxCy = MinI(height - 1, int(floorf((wy + radius - originY) / cellSize)));
		const float r2 = radius * radius;
		for (int cy = minCy; cy <= maxCy; ++cy)
		{
			for (int cx = minCx; cx <= maxCx; ++cx)
			{
				const Vec2 c = CellCenter(cx, cy);
				const float dx = c.x - wx;
				const float dy = c.y - wy;
				if (dx * dx + dy * dy <= r2)
					flags[Index(cx, cy)] |= kCellBlocked;
			}
		}
	}

	void MarkBlockedRect(float minX, float minY, float maxX, float maxY, float pad = 0.0f)
	{
		const int minCx = MaxI(0, int(floorf((minX - pad - originX) / cellSize)));
		const int maxCx = MinI(width - 1, int(floorf((maxX + pad - originX) / cellSize)));
		const int minCy = MaxI(0, int(floorf((minY - pad - originY) / cellSize)));
		const int maxCy = MinI(height - 1, int(floorf((maxY + pad - originY) / cellSize)));
		for (int cy = minCy; cy <= maxCy; ++cy)
			for (int cx = minCx; cx <= maxCx; ++cx)
				flags[Index(cx, cy)] |= kCellBlocked;
	}

	void PenalizeDisk(float wx, float wy, float radius, uint8_t amount)
	{
		if (!(radius > 0.0f) || amount == 0) return;
		const int minCx = MaxI(0, int(floorf((wx - radius - originX) / cellSize)));
		const int maxCx = MinI(width - 1, int(floorf((wx + radius - originX) / cellSize)));
		const int minCy = MaxI(0, int(floorf((wy - radius - originY) / cellSize)));
		const int maxCy = MinI(height - 1, int(floorf((wy + radius - originY) / cellSize)));
		const float r2 = radius * radius;
		for (int cy = minCy; cy <= maxCy; ++cy)
		{
			for (int cx = minCx; cx <= maxCx; ++cx)
			{
				const Vec2 c = CellCenter(cx, cy);
				const float dx = c.x - wx;
				const float dy = c.y - wy;
				if (dx * dx + dy * dy <= r2)
				{
					const int idx = Index(cx, cy);
					const int sum = int(penalty[idx]) + int(amount);
					penalty[idx] = uint8_t(sum > 250 ? 250 : sum);
				}
			}
		}
	}

	// Mark open cells adjacent to blocked cells with a soft preference cost so A*
	// naturally rides the center of corridors/doorways without sealing narrow portals.
	void InflateSoftMargin()
	{
		for (int cy = 0; cy < height; ++cy)
		{
			for (int cx = 0; cx < width; ++cx)
			{
				const int idx = Index(cx, cy);
				if (flags[idx] & kCellBlocked) continue;
				bool nearWall = false;
				for (int dy = -1; dy <= 1 && !nearWall; ++dy)
				{
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (dx == 0 && dy == 0) continue;
						if (IsBlocked(cx + dx, cy + dy))
						{
							nearWall = true;
							break;
						}
					}
				}
				if (nearWall) flags[idx] |= kCellSoftHazard;
				else flags[idx] &= uint8_t(~kCellSoftHazard);
			}
		}
	}

	bool SnapToNearestOpen(int startCx, int startCy, int maxRadius, int *outCx, int *outCy) const
	{
		const int scx = ClampI(startCx, 0, width - 1);
		const int scy = ClampI(startCy, 0, height - 1);
		if (!IsBlocked(scx, scy))
		{
			if (outCx) *outCx = scx;
			if (outCy) *outCy = scy;
			return true;
		}
		int bestCx = -1, bestCy = -1;
		int bestD2 = 0x7fffffff;
		for (int r = 1; r <= maxRadius; ++r)
		{
			for (int dy = -r; dy <= r; ++dy)
			{
				for (int dx = -r; dx <= r; ++dx)
				{
					if (AbsF(float(dx)) != float(r) && AbsF(float(dy)) != float(r)) continue;
					const int nx = scx + dx;
					const int ny = scy + dy;
					if (!InBounds(nx, ny) || IsBlocked(nx, ny)) continue;
					const int d2 = dx * dx + dy * dy;
					if (d2 < bestD2)
					{
						bestD2 = d2;
						bestCx = nx;
						bestCy = ny;
					}
				}
			}
			if (bestCx >= 0)
			{
				if (outCx) *outCx = bestCx;
				if (outCy) *outCy = bestCy;
				return true;
			}
		}
		return false;
	}
};

struct NavPath
{
	Vec2 pts[kMaxWaypoints];
	int count;

	void Clear() { count = 0; }
	bool Push(Vec2 p)
	{
		if (count >= kMaxWaypoints) return false;
		pts[count++] = p;
		return true;
	}
};

// Reusable static/member scratch workspace for zero-allocation A* searches.
struct AStarWorkspace
{
	float gCost[kMaxGridCells];
	float fCost[kMaxGridCells];
	int16_t parent[kMaxGridCells];
	uint8_t state[kMaxGridCells];		// 0 unseen, 1 open, 2 closed
	uint16_t heap[kMaxGridCells];
	int16_t heapPos[kMaxGridCells];
	int heapSize;
	Vec2 rawPts[kMaxGridCells];
	int rawCount;

	inline void HeapSwap(int a, int b)
	{
		const uint16_t ia = heap[a];
		const uint16_t ib = heap[b];
		heap[a] = ib;
		heap[b] = ia;
		heapPos[ib] = int16_t(a);
		heapPos[ia] = int16_t(b);
	}

	inline void HeapSiftUp(int pos)
	{
		while (pos > 0)
		{
			const int p = (pos - 1) >> 1;
			if (fCost[heap[pos]] >= fCost[heap[p]]) break;
			HeapSwap(pos, p);
			pos = p;
		}
	}

	inline void HeapSiftDown(int pos)
	{
		for (;;)
		{
			const int l = (pos << 1) + 1;
			const int r = l + 1;
			int best = pos;
			if (l < heapSize && fCost[heap[l]] < fCost[heap[best]]) best = l;
			if (r < heapSize && fCost[heap[r]] < fCost[heap[best]]) best = r;
			if (best == pos) break;
			HeapSwap(pos, best);
			pos = best;
		}
	}

	inline void HeapPushOrDecrease(uint16_t node)
	{
		if (heapPos[node] < 0)
		{
			const int pos = heapSize++;
			heap[pos] = node;
			heapPos[node] = int16_t(pos);
			HeapSiftUp(pos);
		}
		else
		{
			HeapSiftUp(int(heapPos[node]));
		}
	}

	inline uint16_t HeapPop()
	{
		const uint16_t top = heap[0];
		--heapSize;
		heapPos[top] = -1;
		if (heapSize > 0)
		{
			heap[0] = heap[heapSize];
			heapPos[heap[0]] = 0;
			HeapSiftDown(0);
		}
		return top;
	}
};

inline float OctileHeuristic(int x0, int y0, int x1, int y1)
{
	const int dx = x0 > x1 ? x0 - x1 : x1 - x0;
	const int dy = y0 > y1 ? y0 - y1 : y1 - y0;
	const int mn = dx < dy ? dx : dy;
	const int mx = dx > dy ? dx : dy;
	return float(mx - mn) + 1.41421356f * float(mn);
}

// Thick-corridor grid line-of-sight check that preserves wall clearance and never clips corners.
inline bool GridLineClear(const NavGrid &grid, Vec2 a, Vec2 b, float clearanceRadius = 0.0f, bool avoidPenalized = true)
{
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	const float dist = sqrtf(dx * dx + dy * dy);
	if (dist < 1e-4f) return !grid.IsWorldBlocked(a.x, a.y);

	const float inv = 1.0f / dist;
	const float px = -dy * inv;
	const float py = dx * inv;
	const float step = MaxF(0.25f, grid.cellSize * 0.35f);
	const int steps = MaxI(1, int(ceilf(dist / step)));
	const bool checkSides = clearanceRadius > 0.05f;

	for (int i = 0; i <= steps; ++i)
	{
		const float t = float(i) / float(steps);
		const float x = a.x + dx * t;
		const float y = a.y + dy * t;
		int cx = 0, cy = 0;
		if (!grid.WorldToCell(x, y, &cx, &cy)) return false;
		if (grid.IsBlocked(cx, cy)) return false;
		if (avoidPenalized && grid.penalty[grid.Index(cx, cy)] >= 20) return false;

		if (checkSides && i > 0 && i < steps)
		{
			const float lx = x + px * clearanceRadius;
			const float ly = y + py * clearanceRadius;
			const float rx = x - px * clearanceRadius;
			const float ry = y - py * clearanceRadius;
			if (grid.IsWorldBlocked(lx, ly) || grid.IsWorldBlocked(rx, ry))
				return false;
		}
	}
	return true;
}

// Grid raycast feeler: marches from origin along unit direction (dirX, dirY) up to maxDist,
// returning clear distance before hitting a blocked cell or heavily penalized cell.
inline float GridRayFeeler(const NavGrid &grid, Vec2 origin, float dirX, float dirY, float maxDist)
{
	const float step = MaxF(0.25f, grid.cellSize * 0.35f);
	for (float d = step; d <= maxDist; d += step)
	{
		const float wx = origin.x + dirX * d;
		const float wy = origin.y + dirY * d;
		int cx = 0, cy = 0;
		if (!grid.WorldToCell(wx, wy, &cx, &cy)) return d;
		if (grid.IsBlocked(cx, cy)) return MaxF(0.0f, d - step);
		if (grid.penalty[grid.Index(cx, cy)] >= 60) return MaxF(0.0f, d - step);
	}
	return maxDist;
}

// 8-connected A* search with corner-cutting prevention, soft wall margin preference, and dynamic penalties.
// Writes full un-pulled path to ws.rawPts[0..ws.rawCount-1] and copies (subsampled if needed) into outPath.
inline bool FindPathAStar(const NavGrid &grid, AStarWorkspace &ws, Vec2 start, Vec2 goal,
                          NavPath &outPath, bool allowPartial = false, int snapRadius = 3)
{
	outPath.Clear();
	ws.rawCount = 0;
	const int total = grid.width * grid.height;
	if (total <= 0 || total > kMaxGridCells) return false;

	int scx = 0, scy = 0, gcx = 0, gcy = 0;
	grid.WorldToCell(start.x, start.y, &scx, &scy);
	grid.WorldToCell(goal.x, goal.y, &gcx, &gcy);
	scx = ClampI(scx, 0, grid.width - 1);
	scy = ClampI(scy, 0, grid.height - 1);
	gcx = ClampI(gcx, 0, grid.width - 1);
	gcy = ClampI(gcy, 0, grid.height - 1);

	if (grid.IsBlocked(scx, scy) && !grid.SnapToNearestOpen(scx, scy, snapRadius, &scx, &scy))
		return false;
	if (grid.IsBlocked(gcx, gcy))
	{
		if (snapRadius <= 0 || !grid.SnapToNearestOpen(gcx, gcy, snapRadius, &gcx, &gcy))
			return false;
	}

	const int startIdx = grid.Index(scx, scy);
	const int goalIdx = grid.Index(gcx, gcy);

	memset(ws.state, 0, size_t(total));
	for (int i = 0; i < total; ++i)
	{
		ws.parent[i] = -1;
		ws.heapPos[i] = -1;
		ws.gCost[i] = 1e30f;
		ws.fCost[i] = 1e30f;
	}
	ws.heapSize = 0;

	ws.gCost[startIdx] = 0.0f;
	ws.fCost[startIdx] = OctileHeuristic(scx, scy, gcx, gcy);
	ws.state[startIdx] = 1;
	ws.HeapPushOrDecrease(uint16_t(startIdx));

	int bestIdx = startIdx;
	float bestH = ws.fCost[startIdx];
	bool reached = false;

	static const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
	static const int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
	static const float kStepCost[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f};

	while (ws.heapSize > 0)
	{
		const int cur = int(ws.HeapPop());
		ws.state[cur] = 2;
		if (cur == goalIdx)
		{
			reached = true;
			bestIdx = cur;
			break;
		}
		const int cx = cur % grid.width;
		const int cy = cur / grid.width;
		const float curH = OctileHeuristic(cx, cy, gcx, gcy);
		if (curH < bestH)
		{
			bestH = curH;
			bestIdx = cur;
		}

		for (int dir = 0; dir < 8; ++dir)
		{
			const int nx = cx + kDx[dir];
			const int ny = cy + kDy[dir];
			if (!grid.InBounds(nx, ny)) continue;
			if (grid.IsBlocked(nx, ny)) continue;
			// Strict corner-cutting prevention for diagonal moves.
			if (dir >= 4)
			{
				if (grid.IsBlocked(cx + kDx[dir], cy) || grid.IsBlocked(cx, cy + kDy[dir]))
					continue;
			}
			const int nIdx = grid.Index(nx, ny);
			if (ws.state[nIdx] == 2) continue;

			float extra = 0.0f;
			if (grid.flags[nIdx] & kCellSoftHazard) extra += 0.85f;
			if (grid.penalty[nIdx] > 0) extra += float(grid.penalty[nIdx]) * 0.40f;

			const float ng = ws.gCost[cur] + kStepCost[dir] + extra;
			if (ng < ws.gCost[nIdx])
			{
				ws.parent[nIdx] = int16_t(cur);
				ws.gCost[nIdx] = ng;
				ws.fCost[nIdx] = ng + OctileHeuristic(nx, ny, gcx, gcy);
				ws.state[nIdx] = 1;
				ws.HeapPushOrDecrease(uint16_t(nIdx));
			}
		}
	}

	if (!reached && !allowPartial) return false;
	if (!reached && bestIdx == startIdx) return false;

	// Trace parent chain backwards into ws.rawPts, then reverse.
	int count = 0;
	int trace = bestIdx;
	while (trace >= 0 && count < kMaxGridCells)
	{
		const int tx = trace % grid.width;
		const int ty = trace / grid.width;
		ws.rawPts[count++] = grid.CellCenter(tx, ty);
		if (trace == startIdx) break;
		trace = int(ws.parent[trace]);
	}
	if (count <= 0) return false;

	for (int i = 0; i < count / 2; ++i)
	{
		const Vec2 tmp = ws.rawPts[i];
		ws.rawPts[i] = ws.rawPts[count - 1 - i];
		ws.rawPts[count - 1 - i] = tmp;
	}
	// Replace endpoints with exact start/goal when unblocked to eliminate half-cell snap offset.
	if (!grid.IsWorldBlocked(start.x, start.y)) ws.rawPts[0] = start;
	if (reached && !grid.IsWorldBlocked(goal.x, goal.y)) ws.rawPts[count - 1] = goal;
	ws.rawCount = count;

	// Copy into outPath (subsampling evenly only if raw path exceeds kMaxWaypoints).
	if (count <= kMaxWaypoints)
	{
		for (int i = 0; i < count; ++i) outPath.Push(ws.rawPts[i]);
	}
	else
	{
		for (int i = 0; i < kMaxWaypoints; ++i)
		{
			const int src = (i == kMaxWaypoints - 1) ? (count - 1) : (i * (count - 1)) / (kMaxWaypoints - 1);
			outPath.Push(ws.rawPts[src]);
		}
	}
	return outPath.count > 0;
}

// Greedy LOS string-pulling over rawPts[0..inCount-1]. Preserves obstacle clearance via caller's
// isSegmentClear(Vec2 a, Vec2 b) predicate.
template <typename SegmentClearFn>
inline void StringPullPath(const Vec2 *inPts, int inCount, NavPath &outPath, SegmentClearFn isSegmentClear)
{
	outPath.Clear();
	if (!inPts || inCount <= 0) return;
	outPath.Push(inPts[0]);
	if (inCount == 1) return;

	int anchor = 0;
	while (anchor < inCount - 1)
	{
		int farthest = anchor + 1;
		// Scan forward from the end for the longest valid shortcut (or step linearly).
		for (int probe = inCount - 1; probe > anchor + 1; --probe)
		{
			if (isSegmentClear(inPts[anchor], inPts[probe]))
			{
				farthest = probe;
				break;
			}
		}
		if (!outPath.Push(inPts[farthest]))
		{
			outPath.pts[kMaxWaypoints - 1] = inPts[inCount - 1];
			break;
		}
		anchor = farthest;
	}
}

// Convenience helper: A* + grid-corridor string-pulling in one call.
inline bool PlanPath(const NavGrid &grid, AStarWorkspace &ws, Vec2 start, Vec2 goal,
                     NavPath &outPath, float clearanceRadius = 0.0f, bool allowPartial = false, int snapRadius = 3)
{
	NavPath rawCopy;
	if (!FindPathAStar(grid, ws, start, goal, rawCopy, allowPartial, snapRadius))
		return false;
	const float clr = clearanceRadius > 0.0f ? clearanceRadius : (grid.cellSize * 0.45f);
	StringPullPath(ws.rawPts, ws.rawCount, outPath,
	               [&grid, clr](Vec2 a, Vec2 b) { return GridLineClear(grid, a, b, clr, true); });
	return outPath.count > 0;
}

// ---- Closed-Loop Navigator: Waypoint Follower + 3-Ray Feelers + Stuck-Recovery FSM -------------------------------------

enum NavMode
{
	kModeWalker = 0,
	kModeJet = 1,
	kModeAuto = 2
};

enum NavStatus
{
	kNavIdle = 0,
	kNavMoving = 1,
	kNavRecovering = 2,
	kNavArrived = 3,
	kNavFailed = 4
};

enum RecoveryPhase
{
	kRecNone = 0,
	kRecBackup = 1,
	kRecStrafe = 2
};

struct NavConfig
{
	NavMode mode;
	float goalTol;
	float wpReachTol;
	float feelerRange;
	float feelerAvoidDist;
	float stallDist;
	int stallTicks;
	int backupTicks;
	int strafeTicks;
	int maxReplans;
	float mouseSens;
	bool invertY;
	bool invertJetY;
	bool indoor;
	bool playUnitTank;

	static NavConfig Default(bool isIndoor = false, NavMode navMode = kModeAuto)
	{
		NavConfig c;
		c.mode = navMode;
		c.goalTol = isIndoor ? 3.5f : 8.0f;
		c.wpReachTol = isIndoor ? 1.8f : 4.5f;
		c.feelerRange = isIndoor ? 4.5f : 12.0f;
		c.feelerAvoidDist = isIndoor ? 1.9f : 4.8f;
		c.stallDist = isIndoor ? 0.8f : 1.5f;
		c.stallTicks = 45;
		c.backupTicks = 18;
		c.strafeTicks = 24;
		c.maxReplans = 10;
		c.mouseSens = 7.0f;
		c.invertY = false;
		c.invertJetY = false;
		c.indoor = isIndoor;
		c.playUnitTank = false;
		return c;
	}
};

struct NavTickInput
{
	Vec3 pos;
	float yaw;
	float pitch;
	Vec3 vel;
	float feelerCenter;
	float feelerLeft;
	float feelerRight;
	bool inJet;
	float energyFrac;
	float desiredPitch;		// 0.0f for level walker, or altitude-hold pitch for jet
};

struct NavDriveOutput
{
	bool forward;
	bool back;
	bool strafeLeft;
	bool strafeRight;
	bool wantMorphJump;
	bool wantReplan;
	Vec2 penalizePos;
	float mouseDx;
	float mouseDy;
	NavStatus status;
	float distToGoal;
};

struct NavController
{
	NavConfig cfg;
	NavPath path;
	int wpIndex;
	Vec2 goal;
	NavStatus status;
	RecoveryPhase recPhase;
	int recTimer;
	int recStrafeDir;		// -1 = left (A), +1 = right (D)
	Vec2 progressAnchor;
	bool hasProgressAnchor;
	int stallCounter;
	int replans;
	int recoveries;
	int consecutiveRecoveries;
	float totalMoved;
	Vec2 lastPos;
	bool hasLastPos;
	float lastYaw;
	bool hasLastYaw;

	void Reset(const NavConfig &config, Vec2 goalPt)
	{
		cfg = config;
		path.Clear();
		wpIndex = 0;
		goal = goalPt;
		status = kNavIdle;
		recPhase = kRecNone;
		recTimer = 0;
		recStrafeDir = 1;
		progressAnchor = Vec2{0.0f, 0.0f};
		hasProgressAnchor = false;
		stallCounter = 0;
		replans = 0;
		recoveries = 0;
		consecutiveRecoveries = 0;
		totalMoved = 0.0f;
		lastPos = Vec2{0.0f, 0.0f};
		hasLastPos = false;
		lastYaw = 0.0f;
		hasLastYaw = false;
	}

	void SetPath(const NavPath &newPath, bool isReplan = false)
	{
		path = newPath;
		wpIndex = (path.count > 1) ? 1 : 0;
		status = path.count > 0 ? kNavMoving : kNavFailed;
		recPhase = kRecNone;
		recTimer = 0;
		stallCounter = 0;
		hasProgressAnchor = false;
		if (isReplan) ++replans;
	}

	NavDriveOutput Step(const NavTickInput &in)
	{
		NavDriveOutput out;
		memset(&out, 0, sizeof(out));
		const Vec2 cur{in.pos.x, in.pos.y};
		out.distToGoal = Dist2D(cur, goal);

		if (hasLastPos)
		{
			const float stepDist = Dist2D(lastPos, cur);
			if (isfinite(stepDist) && stepDist < 50.0f)
				totalMoved += stepDist;
		}
		lastPos = cur;
		hasLastPos = true;

		if (out.distToGoal <= cfg.goalTol)
		{
			status = kNavArrived;
			out.status = kNavArrived;
			return out;
		}

		if (path.count <= 0)
		{
			status = kNavFailed;
			out.status = kNavFailed;
			return out;
		}

		const float yawDelta = hasLastYaw ? AbsF(WrapAngle(in.yaw - lastYaw)) : 0.0f;
		lastYaw = in.yaw;
		hasLastYaw = true;

		// Track horizontal progress for stall detection.
		// Note: a possessed tank pivoting in place toward a waypoint behind it is turning, not stalled.
		const Vec2 curWp = path.pts[ClampI(wpIndex, 0, path.count - 1)];
		const bool tankPivoting = cfg.playUnitTank && recPhase == kRecNone &&
		                          AbsF(SignedRightYawError(in.yaw, curWp.x - cur.x, curWp.y - cur.y)) >= 1.05f &&
		                          yawDelta > 0.004f;
		if (!hasProgressAnchor)
		{
			progressAnchor = cur;
			hasProgressAnchor = true;
			stallCounter = 0;
		}
		else
		{
			if (Dist2D(progressAnchor, cur) >= cfg.stallDist)
			{
				progressAnchor = cur;
				stallCounter = 0;
				consecutiveRecoveries = 0;
			}
			else if (tankPivoting)
			{
				stallCounter = 0;
			}
			else if (recPhase == kRecNone)
			{
				++stallCounter;
			}
		}

		// ---- Active Stuck-Recovery FSM ----
		if (recPhase != kRecNone)
		{
			status = kNavRecovering;
			out.status = kNavRecovering;
			--recTimer;

			if (recPhase == kRecBackup)
			{
				out.back = true;
				if (cfg.playUnitTank)
				{
					// Reverse while steering away from the tighter side.
					out.strafeLeft = (recStrafeDir < 0);
					out.strafeRight = (recStrafeDir > 0);
				}
				else
				{
					out.mouseDx = float(recStrafeDir) * 8.0f;
				}
				if (recTimer <= 0)
				{
					recPhase = kRecStrafe;
					recTimer = cfg.strafeTicks;
				}
				return out;
			}

			// kRecStrafe: sidestep toward open feeler, then request replan with penalty.
			if (cfg.playUnitTank)
			{
				out.forward = true;
				out.strafeLeft = (recStrafeDir < 0);
				out.strafeRight = (recStrafeDir > 0);
			}
			else
			{
				out.strafeLeft = (recStrafeDir < 0);
				out.strafeRight = (recStrafeDir > 0);
				out.forward = (in.feelerCenter > cfg.feelerAvoidDist * 0.65f);
				out.mouseDx = float(recStrafeDir) * 10.0f;
			}
			if (recTimer <= 0)
			{
				recPhase = kRecNone;
				stallCounter = 0;
				hasProgressAnchor = false;
				const Vec2 fwd = ForwardXY(in.yaw);
				const float penDist = cfg.indoor ? 2.0f : 4.5f;
				out.penalizePos = Vec2{cur.x + fwd.x * penDist, cur.y + fwd.y * penDist};
				out.wantReplan = true;
				status = kNavMoving;
				out.status = kNavMoving;
			}
			return out;
		}

		// Trigger stuck recovery if horizontal progress stalled OR nose-pinned directly into obstacle.
		const bool stalled = (stallCounter >= cfg.stallTicks);
		const bool nosePinned = (!in.inJet && in.feelerCenter < (cfg.indoor ? 0.85f : 1.6f) &&
		                         in.feelerLeft < (cfg.indoor ? 1.2f : 2.5f) &&
		                         in.feelerRight < (cfg.indoor ? 1.2f : 2.5f) &&
		                         stallCounter >= 12);
		if (stalled || nosePinned)
		{
			++recoveries;
			++consecutiveRecoveries;
			// Pick the more open side for recovery strafe; alternate if tied.
			if (AbsF(in.feelerLeft - in.feelerRight) > 0.35f)
				recStrafeDir = (in.feelerLeft > in.feelerRight) ? -1 : 1;
			else
				recStrafeDir = (recoveries & 1) ? 1 : -1;

			// In auto mode outdoors, if repeated ground recoveries occur and energy > 35%, request Jet-hop!
			if (cfg.mode == kModeAuto && !cfg.indoor && !cfg.playUnitTank &&
			    !in.inJet && in.energyFrac > 0.35f && consecutiveRecoveries >= 2)
			{
				out.wantMorphJump = true;
			}

			recPhase = kRecBackup;
			recTimer = cfg.backupTicks;
			stallCounter = 0;
			status = kNavRecovering;
			out.status = kNavRecovering;
			out.back = true;
			return out;
		}

		// ---- Advance intermediate waypoints cleanly ----
		while (wpIndex < path.count - 1)
		{
			const float dWp = Dist2D(cur, path.pts[wpIndex]);
			const float reach = MaxF(cfg.wpReachTol, cfg.indoor ? 1.5f : 3.5f);
			if (dWp <= reach)
			{
				++wpIndex;
				continue;
			}
			// Also advance if we overshot wpIndex and are already moving toward wpIndex + 1.
			const Vec2 seg{path.pts[wpIndex + 1].x - path.pts[wpIndex].x,
			               path.pts[wpIndex + 1].y - path.pts[wpIndex].y};
			const Vec2 fromWp{cur.x - path.pts[wpIndex].x, cur.y - path.pts[wpIndex].y};
			const float segLenSq = seg.x * seg.x + seg.y * seg.y;
			if (segLenSq > 1e-4f && (fromWp.x * seg.x + fromWp.y * seg.y) > 0.0f &&
			    Dist2D(cur, path.pts[wpIndex + 1]) < Dist2D(path.pts[wpIndex], path.pts[wpIndex + 1]))
			{
				++wpIndex;
				continue;
			}
			break;
		}

		const Vec2 wp = path.pts[ClampI(wpIndex, 0, path.count - 1)];
		const float dx = wp.x - cur.x;
		const float dy = wp.y - cur.y;
		float yawErr = SignedRightYawError(in.yaw, dx, dy);

		// ---- Reactive 3-ray feeler steering bias (avoids obstacles before touching them) ----
		bool feelerStrafeL = false;
		bool feelerStrafeR = false;
		if (!in.inJet && out.distToGoal > cfg.goalTol * 1.25f)
		{
			if (in.feelerCenter < cfg.feelerAvoidDist)
			{
				const float urgency = 1.0f - ClampF(in.feelerCenter / cfg.feelerAvoidDist, 0.0f, 1.0f);
				const float steerDir = (in.feelerLeft >= in.feelerRight) ? -1.0f : 1.0f;
				yawErr = WrapAngle(yawErr + steerDir * (0.45f + 0.55f * urgency));
				if (steerDir < 0.0f) feelerStrafeL = true;
				else feelerStrafeR = true;
			}
			else
			{
				const float sideThresh = cfg.indoor ? 1.3f : 3.2f;
				if (in.feelerLeft < sideThresh && in.feelerRight > in.feelerLeft + 0.4f)
					feelerStrafeR = true;
				else if (in.feelerRight < sideThresh && in.feelerLeft > in.feelerRight + 0.4f)
					feelerStrafeL = true;
			}
		}

		status = kNavMoving;
		out.status = kNavMoving;

		// ---- Vehicle / Possessed Tank driving law ----
		if (cfg.playUnitTank)
		{
			if (yawErr > 0.14f) out.strafeRight = true;
			else if (yawErr < -0.14f) out.strafeLeft = true;
			if (AbsF(yawErr) < 1.10f) out.forward = true;
			// Gently align look camera with target heading.
			out.mouseDx = YawMouseDelta(yawErr, cfg.mouseSens, 0.25f, 22.0f);
			return out;
		}

		// ---- Jet flight steering law ----
		if (in.inJet)
		{
			out.forward = true;
			out.mouseDx = YawMouseDelta(yawErr, cfg.mouseSens, 0.45f, 40.0f);
			out.mouseDy = PitchMouseDelta(in.pitch, in.desiredPitch, cfg.invertJetY, cfg.mouseSens, 0.35f, 25.0f);
			return out;
		}

		// ---- Walker steering law ----
		const float gain = cfg.indoor ? 0.48f : 0.55f;
		const float maxDx = cfg.indoor ? 48.0f : 65.0f;
		out.mouseDx = YawMouseDelta(yawErr, cfg.mouseSens, gain, maxDx);
		out.mouseDy = PitchMouseDelta(in.pitch, in.desiredPitch, cfg.invertY, cfg.mouseSens, 0.30f, 18.0f);
		out.strafeLeft = feelerStrafeL;
		out.strafeRight = feelerStrafeR;
		// Drive forward whenever heading is reasonably aligned with the next waypoint.
		out.forward = (AbsF(yawErr) < (cfg.indoor ? 0.95f : 1.15f));
		return out;
	}
};

} // namespace CeNav
