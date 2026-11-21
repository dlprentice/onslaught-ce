// SPDX-License-Identifier: GPL-3.0-only
// Commander mode: where several squads sent to one place stand. The goal points are a hexagonal cluster around the place (the centre first, then rings of six, twelve, eighteen...
// squads' worth of points, one `spacing` apart), so squads do not queue on one point, and each squad takes the nearest point still free, the squad farthest from the place choosing
// first, so that squads on the way do not cross more than they must. Pure geometry: no game types and no random number; port/tools/ce_cmdgroup_driver.cpp runs it.
#ifndef PORT_CE_CMDGROUP_H
#define PORT_CE_CMDGROUP_H

#include <float.h>
#include <math.h>

// The first n points of the cluster around (cx, cy): returns n. The point k of ring r (r >= 1) is on the hexagon of side r.
inline int CeCmdSlots(int n, float cx, float cy, float spacing, float *xs, float *ys)
{
	if (n <= 0) return 0;
	static const float dx[6] = {1.f, 0.5f, -0.5f, -1.f, -0.5f, 0.5f}, dy[6] = {0.f, 0.8660254f, 0.8660254f, 0.f, -0.8660254f, -0.8660254f};
	int made = 0;
	xs[made] = cx;
	ys[made] = cy;
	++made;
	for (int ring = 1; made < n; ++ring)
	{
		float px = cx + dx[4] * float(ring) * spacing, py = cy + dy[4] * float(ring) * spacing;	// the ring's first corner
		for (int side = 0; side < 6 && made < n; ++side)
			for (int i = 0; i < ring && made < n; ++i)
			{
				xs[made] = px;
				ys[made] = py;
				++made;
				px += dx[side] * spacing;
				py += dy[side] * spacing;
			}
	}
	return made;
}

// Keep the whole formation inside the half-open map bounds. Translate it rather than clamp individual points, so squads keep their spacing. A map too small to fit the formation is
// refused; issuing off-map or stacked goals would leave some squads with an order they cannot reasonably follow.
inline bool CeCmdSlotsInBounds(int n, float cx, float cy, float spacing, float x0, float y0, float x1, float y1, float *xs, float *ys)
{
	if (n <= 0 || n > 512 || !xs || !ys || !(spacing > 0.f && spacing <= FLT_MAX) ||
		!(x0 >= -FLT_MAX && x1 <= FLT_MAX && y0 >= -FLT_MAX && y1 <= FLT_MAX) ||
		!(x0 <= cx && cx < x1 && y0 <= cy && cy < y1)) return false;
	CeCmdSlots(n, cx, cy, spacing, xs, ys);
	float minX = xs[0], maxX = xs[0], minY = ys[0], maxY = ys[0];
	for (int i = 1; i < n; ++i)
	{
		if (!(xs[i] >= -FLT_MAX && xs[i] <= FLT_MAX && ys[i] >= -FLT_MAX && ys[i] <= FLT_MAX)) return false;
		if (xs[i] < minX) minX = xs[i];
		if (xs[i] > maxX) maxX = xs[i];
		if (ys[i] < minY) minY = ys[i];
		if (ys[i] > maxY) maxY = ys[i];
	}
	const float highX = nextafterf(x1, x0), highY = nextafterf(y1, y0);
	const float lowDx = x0 - minX, highDx = highX - maxX, lowDy = y0 - minY, highDy = highY - maxY;
	if (!(lowDx <= highDx && lowDy <= highDy)) return false;
	const float dx = lowDx > 0.f ? lowDx : highDx < 0.f ? highDx : 0.f;
	const float dy = lowDy > 0.f ? lowDy : highDy < 0.f ? highDy : 0.f;
	for (int i = 0; i < n; ++i)
	{
		xs[i] += dx; ys[i] += dy;
		// Adding a translation can round one point across an edge by one float step.
		if (xs[i] < x0) xs[i] = x0; else if (xs[i] > highX) xs[i] = highX;
		if (ys[i] < y0) ys[i] = y0; else if (ys[i] > highY) ys[i] = highY;
	}
	return true;
}

// slotOf[i] is the point (of n points, slotX and slotY) the squad i (at sx[i], sy[i]) takes: the squads in the order of their distance from (cx, cy), the farthest first (ties
// by index), each the nearest free point (ties by the lower index).
inline bool CeCmdAssign(int n, const float *sx, const float *sy, const float *slotX, const float *slotY, float cx, float cy, int *slotOf)
{
	bool used[512];
	bool done[512];
	if (n <= 0 || n > 512 || !sx || !sy || !slotX || !slotY || !slotOf ||
		!(cx >= -FLT_MAX && cx <= FLT_MAX && cy >= -FLT_MAX && cy <= FLT_MAX)) return false;
	for (int i = 0; i < n; ++i)
		if (!(sx[i] >= -FLT_MAX && sx[i] <= FLT_MAX && sy[i] >= -FLT_MAX && sy[i] <= FLT_MAX &&
			  slotX[i] >= -FLT_MAX && slotX[i] <= FLT_MAX && slotY[i] >= -FLT_MAX && slotY[i] <= FLT_MAX)) return false;
	for (int i = 0; i < n; ++i) { used[i] = false; done[i] = false; slotOf[i] = -1; }
	for (int round = 0; round < n; ++round)
	{
		int who = -1;
		float farthest = -1.f;
		for (int i = 0; i < n; ++i)
		{
			if (done[i]) continue;
			const float d = (sx[i] - cx) * (sx[i] - cx) + (sy[i] - cy) * (sy[i] - cy);
			if (d > farthest) { farthest = d; who = i; }
		}
		if (who < 0) return false;
		int best = -1;
		float bestD = 0.f;
		for (int k = 0; k < n; ++k)
		{
			if (used[k]) continue;
			const float d = (slotX[k] - sx[who]) * (slotX[k] - sx[who]) + (slotY[k] - sy[who]) * (slotY[k] - sy[who]);
			if (best < 0 || d < bestD) { best = k; bestD = d; }
		}
		if (best < 0) return false;
		slotOf[who] = best;
		used[best] = true;
		done[who] = true;
	}
	return true;
}

#endif
