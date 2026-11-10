// SPDX-License-Identifier: GPL-3.0-only
// Host checks of port/platform/common/feel_script.h: the authored input script, the mouse stream's integral and the display model's grid.
#include "feel_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int checks, failures;
#define CHECK(cond, label) do { ++checks; if (!(cond)) { ++failures; printf("FAIL %s (line %d)\n", label, __LINE__); } } while (0)

static const uint64_t kMs = 1000000ull;

static void TestPace()
{
	const uint64_t period = 16666667ull, grid = 1000 * kMs;
	CHECK(feel::PaceTarget(500 * kMs, grid, period) == 500 * kMs, "before the anchor nothing is held");
	CHECK(feel::PaceTarget(grid, grid, period) == grid, "exactly on the anchor is not held");
	CHECK(feel::PaceTarget(grid + 1, grid, period) == grid + period, "a nanosecond late waits for the next grid time");
	CHECK(feel::PaceTarget(grid + period, grid, period) == grid + period, "exactly on a grid time is not held");
	CHECK(feel::PaceTarget(grid + period + 5 * kMs, grid, period) == grid + 2 * period, "mid-period waits to the end of it");
	// a frame that took two and a half periods lands on the third grid time: the display shows it a frame late, twice over
	CHECK(feel::PaceTarget(grid + 5 * period / 2, grid, period) == grid + 3 * period, "a long frame misses the vertical sync it missed");
	CHECK(feel::PaceTarget(grid + 10 * period + 1, grid, 0) == grid + 10 * period + 1, "no period, no model");
	// never earlier than now, never more than a period later
	for (uint64_t k = 0; k < 2000; ++k)
	{
		uint64_t now = grid + k * 12345ull;
		uint64_t t = feel::PaceTarget(now, grid, period);
		CHECK(t >= now && t - now < period + 1 && (t - grid) % period == 0, "the target is the next grid time");
	}
}

static void TestParse()
{
	feel::Script s;
	const char *text =
		"# a comment\n\n"
		"arm state 3 12   # trailing comment\r\n"
		"key 500 d 26 11\n"
		"key 100 d 7 20\n"
		"key 900.5 u 26 11\n"
		"mouse 1000 3000 800 -200\n"
		"quit 5000\n"
		"  key 50 u 7 20  \n";
	CHECK(s.Parse(text), "a script with an arm line is usable");
	CHECK(s.armMode == feel::ARM_STATE && s.armState == 3 && s.armTicks == 12, "arm state with a tick count");
	CHECK(s.keys.size() == 4 && s.mouse.size() == 1 && s.quitMs == 5000, "every verb is read");
	CHECK(s.keys[0].ms == 50 && s.keys[1].ms == 100 && s.keys[2].ms == 500 && s.keys[3].ms == 900.5, "key steps are in time order");
	CHECK(s.keys[1].sc == 7 && s.keys[1].raw == 0x20 && s.keys[1].down, "a key step's fields");
	CHECK(s.rejected.empty() && s.accepted.size() == 7, "every line was accepted and kept as text");

	feel::Script d;
	d.Parse("arm state 2\n");
	CHECK(d.armTicks == 20, "arm state defaults to twenty ticks");

	feel::Script bad;
	bad.Parse("arm present 10\nkey -5 d 1 1\nkey 5 x 1 1\nmouse 100 50 1 1\nquit -1\nfly away\nkey 5 d 1\nmouse 1 2 3\narm present 4 extra\n");
	CHECK(bad.armPresent == 10, "a malformed arm line does not replace a good one");
	CHECK(bad.keys.empty() && bad.mouse.empty() && bad.quitMs < 0, "malformed lines add nothing");
	CHECK(bad.rejected.size() == 8, "malformed lines are reported");

	feel::Script none;
	CHECK(!none.Parse("key 5 d 1 1\n") && none.Empty(), "a script without an arm line is not usable");
	CHECK(!none.Armed(), "and is never armed");
}

static void TestArm()
{
	feel::Script s;
	s.Parse("arm present 5\n");
	CHECK(!s.PresentArm(4, 10 * kMs), "not armed before the Present count");
	CHECK(s.PresentArm(5, 20 * kMs), "armed at the Present count");
	CHECK(s.Armed() && s.Ms(30 * kMs) == 10.0, "the timeline counts from the arm");
	CHECK(!s.PresentArm(6, 40 * kMs) && s.Ms(40 * kMs) == 20.0, "arming happens once");

	feel::Script t;
	t.Parse("arm state 2 3\n");
	CHECK(!t.TickArm(2, 0, 1 * kMs) && !t.TickArm(2, 0, 2 * kMs), "two ticks of three");
	CHECK(!t.TickArm(2, 1, 3 * kMs), "a paused tick restarts the count");
	CHECK(!t.TickArm(2, 0, 4 * kMs) && !t.TickArm(1, 0, 5 * kMs), "another phase restarts it");
	CHECK(!t.TickArm(2, 0, 6 * kMs) && !t.TickArm(2, 0, 7 * kMs), "again two of three");
	CHECK(t.TickArm(2, 0, 8 * kMs), "the third consecutive tick arms");
	CHECK(t.Ms(18 * kMs) == 10.0, "from that tick");
	CHECK(!t.PresentArm(100, 9 * kMs), "a state script ignores Present counts");
}

static void TestMouse()
{
	feel::Script s;
	s.Parse("arm present 0\nmouse 100 600 800 -200\nmouse 300 400 100 0\n");
	long dx = 0, dy = 0;
	s.Take(5 * kMs, &dx, &dy);
	CHECK(dx == 0 && dy == 0, "nothing before the arm");
	s.PresentArm(0, 1000 * kMs);
	// Poll at irregular intervals: the total must equal the integral, within the one count the remainder carries.
	uint64_t seed = 12345, now = 1000 * kMs;
	double totalX = 0, totalY = 0;
	for (int i = 0; i < 400; ++i)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
		now += (seed >> 40) % (9 * kMs) + 100000ull;
		long x = 0, y = 0;
		s.Take(now, &x, &y);
		totalX += x;
		totalY += y;
		if (now > 1000 * kMs + 800 * kMs) break;
	}
	// the integral over the full script: 800 * 0.5 + 100 * 0.1 = 410, and -200 * 0.5 = -100
	CHECK(fabs(totalX - 410.0) <= 1.0, "the x counts delivered are the integral");
	CHECK(fabs(totalY + 100.0) <= 1.0, "the y counts delivered are the integral");
	long x = 0, y = 0;
	s.Take(now + 50 * kMs, &x, &y);
	CHECK(x == 0 && y == 0, "nothing after the last segment");
	// a steady stream is delivered in proportion to the time since the last poll
	feel::Script r;
	r.Parse("arm present 0\nmouse 0 1000 1000 0\n");
	r.PresentArm(0, 0);
	long a = 0, b = 0, c = 0, z = 0;
	r.Take(50 * kMs, &a, &z);
	r.Take(66 * kMs, &b, &z);
	r.Take(116 * kMs, &c, &z);
	CHECK(a == 50 && b == 16 && c == 50, "a poll gets the movement since the one before");
	// a clock that goes backwards delivers nothing and loses nothing
	long back = 0;
	r.Take(100 * kMs, &back, &z);
	CHECK(back == 0, "an earlier time delivers nothing");
	long again = 0;
	r.Take(126 * kMs, &again, &z);
	CHECK(again == 10, "and the next poll resumes where the last one stopped");
}

static void TestKeysAndQuit()
{
	feel::Script s;
	s.Parse("arm present 0\nkey 200 u 26 11\nkey 100 d 26 11\nquit 300\n");
	s.PresentArm(0, 1000 * kMs);
	feel::KeyStep k;
	CHECK(!s.KeyDue(1050 * kMs, &k), "no step before the first is due");
	CHECK(s.KeyDue(1100 * kMs, &k) && k.down && k.sc == 26 && k.raw == 0x11, "the first step is the key going down");
	CHECK(!s.KeyDue(1100 * kMs, &k), "and is taken once");
	CHECK(!s.QuitDue(1299 * kMs), "not quitting yet");
	CHECK(s.KeyDue(1250 * kMs, &k) && !k.down, "the key goes up when due, whenever it is read");
	CHECK(s.QuitDue(1300 * kMs) && !s.QuitDue(1400 * kMs), "quit fires once");
	feel::Script u;
	u.Parse("arm present 9\nkey 0 d 1 1\nquit 0\n");
	CHECK(!u.KeyDue(5 * kMs, &k) && !u.QuitDue(5 * kMs), "an unarmed script gives nothing");
}

static void TestShots()
{
	feel::Script s;
	s.Parse("arm present 0\nshots 1000 3 /data/some folder/shots\n");
	std::string dir;
	CHECK(!s.ShotDue(5000 * kMs, &dir), "no shots before the arm");
	s.PresentArm(0, 100 * kMs);
	CHECK(!s.ShotDue(600 * kMs, &dir), "none before their time");
	CHECK(s.ShotDue(1100 * kMs, &dir) && dir == "/data/some folder/shots", "the first frame of the run, with its folder (spaces kept)");
	CHECK(s.ShotDue(1117 * kMs, &dir) && s.ShotDue(1134 * kMs, &dir), "and the next two");
	CHECK(!s.ShotDue(1150 * kMs, &dir) && !s.ShotDue(9000 * kMs, &dir), "and no more than the count");
	feel::Script bad;
	bad.Parse("arm present 0\nshots 1000 0 /x\nshots -5 3 /x\nshots 100 3\nshots 100 3 \n");
	CHECK(bad.rejected.size() == 4 && bad.shotsCount == 0, "malformed shots lines add nothing");
}

int main()
{
	TestShots();
	TestPace();
	TestParse();
	TestArm();
	TestMouse();
	TestKeysAndQuit();
	printf("feel script checks=%d failures=%d\n", checks, failures);
	return failures ? 2 : 0;
}
