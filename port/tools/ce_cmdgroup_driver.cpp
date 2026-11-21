// SPDX-License-Identifier: GPL-3.0-only
// Runs ce_cmdgroup.h (ce_commander_tests.py compiles and runs this): one squad stands on the place; n squads get n distinct points, none nearer than the spacing to another, the cluster
// centred on the place and no wider than its rings; the same inputs give the same points; every squad gets a point of its own and the squads' paths do not cross when two squads
// stand on opposite sides of the place (each takes the point on its own side).
#include <ce_cmdgroup.h>

#include <stdio.h>

static int failures;

static void expect(bool ok, const char *what, int n = 0)
{
	if (ok) return;
	printf("FAIL %s [n=%d]\n", what, n);
	++failures;
}

int main()
{
	const float spacing = 32.f;
	for (int n = 1; n <= 61; ++n)
	{
		float xs[512], ys[512], xs2[512], ys2[512];
		expect(CeCmdSlots(n, 100.f, 200.f, spacing, xs, ys) == n, "n points are made", n);
		CeCmdSlots(n, 100.f, 200.f, spacing, xs2, ys2);
		bool same = true, spaced = true;
		float farthest = 0.f;
		float mx = 0.f, my = 0.f;
		for (int i = 0; i < n; ++i)
		{
			if (xs[i] != xs2[i] || ys[i] != ys2[i]) same = false;
			mx += xs[i]; my += ys[i];
			const float d = sqrtf((xs[i] - 100.f) * (xs[i] - 100.f) + (ys[i] - 200.f) * (ys[i] - 200.f));
			if (d > farthest) farthest = d;
			for (int j = 0; j < i; ++j)
				if (sqrtf((xs[i] - xs[j]) * (xs[i] - xs[j]) + (ys[i] - ys[j]) * (ys[i] - ys[j])) < spacing - 0.01f) spaced = false;
		}
		expect(same, "the same inputs give the same points", n);
		expect(spaced, "no two points are nearer than the spacing", n);
		expect(xs[0] == 100.f && ys[0] == 200.f, "the first point is the place", n);
		// n points fill rings 0..r completely, r the smallest ring with 1 + 3r(r+1) >= n: nothing lies beyond ring r
		int r = 0;
		while (1 + 3 * r * (r + 1) < n) ++r;
		expect(farthest <= float(r) * spacing + 0.01f, "no point lies beyond the last ring", n);
		if (n == 1 + 3 * r * (r + 1)) expect(fabsf(mx / float(n) - 100.f) < 0.01f && fabsf(my / float(n) - 200.f) < 0.01f, "a full cluster is centred on the place", n);
		// every squad takes one point of its own
		float sx[512], sy[512];
		int slotOf[512];
		for (int i = 0; i < n; ++i) { sx[i] = 50.f + 17.f * float(i % 7); sy[i] = 300.f - 11.f * float(i % 5) + float(i); }
		const bool assigned = CeCmdAssign(n, sx, sy, xs, ys, 100.f, 200.f, slotOf);
		expect(assigned, "valid assignment succeeds", n);
		if (!assigned) continue;
		bool taken[512] = {false}, own = true;
		for (int i = 0; i < n; ++i)
		{
			if (slotOf[i] < 0 || slotOf[i] >= n || taken[slotOf[i]]) own = false;
			else taken[slotOf[i]] = true;
		}
		expect(own, "each squad has a point of its own", n);
	}
	{
		// two squads on opposite sides of the place take the points on their own sides (the paths do not cross)
		float xs[2], ys[2], sx[2] = {20.f, 180.f}, sy[2] = {200.f, 200.f};
		int slotOf[2];
		CeCmdSlots(2, 100.f, 200.f, 32.f, xs, ys);
		const bool assigned = CeCmdAssign(2, sx, sy, xs, ys, 100.f, 200.f, slotOf);
		expect(assigned, "two-squad assignment succeeds");
		if (assigned)
		{
			expect(slotOf[0] != slotOf[1], "two squads, two points");
			// Ties choose the lower index first; it takes the nearer point to itself.
			expect(fabsf(xs[slotOf[0]] - 20.f) < fabsf(xs[slotOf[1]] - 20.f), "the squad on the left takes the left point");
		}
	}
	{
		// the squad farthest from the place chooses first, so it gets the point nearest to it even when nearer squads stand by that point
		float xs[3], ys[3], sx[3] = {120.f, 125.f, 400.f}, sy[3] = {170.f, 175.f, 200.f};
		int slotOf[3];
		CeCmdSlots(3, 100.f, 200.f, 32.f, xs, ys);
		const bool assigned = CeCmdAssign(3, sx, sy, xs, ys, 100.f, 200.f, slotOf);
		expect(assigned, "three-squad assignment succeeds");
		int nearest = 0;
		for (int k = 1; k < 3; ++k)
			if (xs[k] > xs[nearest]) nearest = k;
		if (assigned)
		{
			expect(slotOf[2] == nearest, "the farthest squad takes the point nearest to it");
			expect(slotOf[0] != slotOf[1] && slotOf[0] != slotOf[2] && slotOf[1] != slotOf[2], "three squads, three points");
		}
	}
	{
		// A cluster near every edge stays in bounds without placing squads on top of one another.
		const float centers[5][2] = {{2.f, 2.f}, {510.f, 2.f}, {2.f, 510.f}, {510.f, 510.f}, {256.f, 256.f}};
		const int counts[4] = {2, 7, 19, 61};
		for (int c = 0; c < 5; ++c)
			for (int k = 0; k < 4; ++k)
			{
				const int n = counts[k];
				float xs[512], ys[512];
				const bool ok = CeCmdSlotsInBounds(n, centers[c][0], centers[c][1], spacing, 0.f, 0.f, 512.f, 512.f, xs, ys);
				expect(ok, "a formation fits inside the map", n);
				if (!ok) continue;
				bool inside = true, spaced = true;
				for (int i = 0; i < n; ++i)
				{
					if (!(xs[i] >= 0.f && xs[i] < 512.f && ys[i] >= 0.f && ys[i] < 512.f)) inside = false;
					for (int j = 0; j < i; ++j)
						if (sqrtf((xs[i] - xs[j]) * (xs[i] - xs[j]) + (ys[i] - ys[j]) * (ys[i] - ys[j])) < spacing - 0.01f) spaced = false;
				}
				expect(inside, "every group destination stays inside", n);
				expect(spaced, "moving the cluster preserves spacing", n);
				if (c == 4) expect(xs[0] == centers[c][0] && ys[0] == centers[c][1], "a fitting cluster stays at the chosen point", n);
			}
		float xs[512], ys[512];
		const bool rectOk = CeCmdSlotsInBounds(7, 11.f, 39.f, spacing, 10.f, -50.f, 200.f, 40.f, xs, ys);
		expect(rectOk, "a formation fits inside rectangular bounds");
		bool rectangular = true;
		for (int i = 0; rectOk && i < 7; ++i)
			if (!(xs[i] >= 10.f && xs[i] < 200.f && ys[i] >= -50.f && ys[i] < 40.f)) rectangular = false;
		expect(rectangular, "each destination stays inside rectangular bounds");
		expect(!CeCmdSlotsInBounds(2, 5.f, 5.f, spacing, 0.f, 0.f, 10.f, 10.f, xs, ys), "a map too small for the cluster is refused");
		expect(!CeCmdSlotsInBounds(2, 512.f, 5.f, spacing, 0.f, 0.f, 512.f, 512.f, xs, ys), "a point beyond the map is refused");
		expect(!CeCmdSlotsInBounds(513, 5.f, 5.f, spacing, 0.f, 0.f, 512.f, 512.f, xs, ys), "too many squads are refused");
		expect(!CeCmdSlotsInBounds(2, 5.f, 5.f, INFINITY, 0.f, 0.f, 512.f, 512.f, xs, ys), "non-finite spacing is refused");
		expect(!CeCmdSlotsInBounds(2, NAN, 5.f, spacing, 0.f, 0.f, 512.f, 512.f, xs, ys), "non-finite target is refused");
	}
	{
		float sx[2] = {NAN, 10.f}, sy[2] = {1.f, 1.f}, xs[2] = {2.f, 3.f}, ys[2] = {2.f, 3.f};
		int slotOf[2] = {37, 37};
		expect(!CeCmdAssign(2, sx, sy, xs, ys, 0.f, 0.f, slotOf), "non-finite squad position is refused");
		expect(slotOf[0] == 37 && slotOf[1] == 37, "refusal leaves the assignment untouched");
		sx[0] = 1.f; ys[1] = INFINITY;
		expect(!CeCmdAssign(2, sx, sy, xs, ys, 0.f, 0.f, slotOf), "non-finite destination is refused");
		expect(!CeCmdAssign(513, sx, sy, xs, ys, 0.f, 0.f, slotOf), "too many assignments are refused before reading them");
	}
	printf(failures ? "FAIL\n" : "PASS\n");
	return failures ? 1 : 0;
}
