// SPDX-License-Identifier: GPL-3.0-only
// The developer information text's place and size (PortCeDevStatsDraw, port/game/ce_console_game.cpp): its lines from the window's
// size to its two draws, extracted by devstats_layout_tests.py into devstats_layout.inc, run against these stand-ins. Exit 0 when
// every case holds, 2 (and a FAIL line) at the first that does not.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using DWORD = uint32_t;
using WCHAR = wchar_t;

struct Draw { float x, y, sx, sy; DWORD colour; };
static std::vector<Draw> draws;
struct CDXBitmapFont
{
	void DrawTextScaled(float x, float y, float, float sx, float sy, DWORD colour, WCHAR *, int, DWORD *)
	{ draws.push_back({x, y, sx, sy, colour}); }
};
struct Platform { int w = 640, h = 480; int GetWindowWidth() { return w; } int GetWindowHeight() { return h; } } PLATFORM;
static char sDevStats[8] = "stats";
static WCHAR wideStats[8];
static WCHAR *ToWCHAR(const char *) { return wideStats; }
static bool green = false;
static bool PortCeDevStatsGreen() { return green; }

// The lines under test for a window, given the block's widest line in the font's own pixels.
static void Layout(int windowWidth, int windowHeight, float widest)
{
	PLATFORM.w = windowWidth;
	PLATFORM.h = windowHeight;
	CDXBitmapFont face, *font = &face;
	draws.clear();
#include "devstats_layout.inc"
}

static const DWORD kShadow = 0xff000000, kYellow = 0xffffff80, kGreen = 0xff40ff40;

// main's own lines (7f43a167) for comparison: the block at (40, 30) at the font's size, shrunk only when its widest line is wider
// than the window less 48 pixels, its shadow one pixel down and right.
static std::vector<Draw> MainDraws(int windowWidth, float widest)
{
	const float room = (float)windowWidth - 48.f;
	const float scale = (widest > room && room > 0.f) ? room / widest : 1.f;
	return {{41, 31, scale, scale, kShadow}, {40, 30, scale, scale, green ? kGreen : kYellow}};
}

static void fail(const char *what, int w, int h, float widest)
{
	printf("FAIL: %s at %d x %d with a block %.1f pixels wide\n", what, w, h, widest);
	exit(2);
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main()
{
	int cases = 0;
	// No larger than 640 x 480 (the smaller ratio at most 1): main's draws, exactly.
	const int widths[] = {0, 1, 320, 512, 600, 639, 640, 641, 800, 1280, 3840};
	const int heights[] = {0, 1, 200, 240, 384, 479, 480};
	const int narrow[] = {0, 320, 600, 640};
	const int tall[] = {481, 600, 1080, 2160};
	const float blocks[] = {0.f, 1.f, 100.f, 500.f, 591.f, 592.f, 593.f, 675.f, 2000.f, 10000.f};
	std::vector<std::pair<int, int>> small;
	for (int w : widths)
		for (int h : heights)
			small.push_back({w, h});
	for (int w : narrow)
		for (int h : tall)
			small.push_back({w, h});
	for (int colour = 0; colour < 2; ++colour)
	{
		green = colour == 1;
		for (const auto &size : small)
			for (float widest : blocks)
			{
				Layout(size.first, size.second, widest);
				const std::vector<Draw> want = MainDraws(size.first, widest);
				if (draws.size() != 2)
					fail("not two draws", size.first, size.second, widest);
				for (int i = 0; i < 2; ++i)
					if (draws[i].x != want[i].x || draws[i].y != want[i].y || draws[i].sx != want[i].sx || draws[i].sy != want[i].sy
						|| draws[i].colour != want[i].colour)
						fail(i ? "the text is not main's" : "the shadow is not main's", size.first, size.second, widest);
				++cases;
			}
	}
	// Larger windows, worked by hand: the factor is the smaller of width / 640 and height / 480; the place is (40, 30) times it,
	// the shadow that factor further down and right, and the size the factor unless the block would then pass the window's right
	// edge less 48 times the factor, when it shrinks to fit that room.
	struct Want { int w, h; float widest, factor, x, y, scale; };
	const Want large[] = {
		{3840, 2160, 675.f, 4.5f, 180.f, 135.f, 4.5f},	// 675 x 4.5 = 3037.5 fits in 3840 - 216 = 3624
		{3840, 2160, 820.f, 4.5f, 180.f, 135.f, 3624.f / 820.f},	// 820 x 4.5 = 3690 does not
		{3840, 2160, 1000.f, 4.5f, 180.f, 135.f, 3.624f},
		{1920, 1080, 675.f, 2.25f, 90.f, 67.5f, 2.25f},	// 1518.75 fits in 1920 - 108 = 1812
		{2560, 1440, 675.f, 3.f, 120.f, 90.f, 3.f},
		{1280, 1024, 500.f, 2.f, 80.f, 60.f, 2.f},	// 5:4: the width decides
		{5120, 1440, 675.f, 3.f, 120.f, 90.f, 3.f},	// 32:9: the height decides
		{960, 720, 600.f, 1.5f, 60.f, 45.f, 888.f / 600.f},	// 900 does not fit in 960 - 72 = 888
	};
	for (int colour = 0; colour < 2; ++colour)
	{
		green = colour == 1;
		for (const Want &want : large)
		{
			Layout(want.w, want.h, want.widest);
			if (draws.size() != 2)
				fail("not two draws", want.w, want.h, want.widest);
			const Draw &shadow = draws[0], &text = draws[1];
			if (shadow.colour != kShadow || text.colour != (green ? kGreen : kYellow))
				fail("the shadow and the text are not drawn in that order and colour", want.w, want.h, want.widest);
			if (!near(text.x, want.x) || !near(text.y, want.y))
				fail("the text is not at (40, 30) times the factor", want.w, want.h, want.widest);
			if (!near(shadow.x, want.x + want.factor) || !near(shadow.y, want.y + want.factor))
				fail("the shadow is not the factor down and right of the text", want.w, want.h, want.widest);
			if (!near(text.sx, want.scale) || !near(text.sy, want.scale) || !near(shadow.sx, want.scale) || !near(shadow.sy, want.scale))
				fail("the text's size is not the factor, or the room's", want.w, want.h, want.widest);
			++cases;
		}
	}
	printf("devstats layout: %d cases hold\n", cases);
	return 0;
}
