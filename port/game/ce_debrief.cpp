// SPDX-License-Identifier: GPL-3.0-only
// The CE end-of-level statistics panel (patch 0104 in CGame's end screen, game.cpp): the six rows Stuart's PC layer drew
// (PCGame.cpp) and his DX layer, like retail, keeps commented out (game.cpp, after the label-width loop that still runs):
// mission time, time in jet and walker mode, rounds fired, accuracy and damage taken, from the stats the game still
// counts, with the game's own strings, and "CHEATER!" when god mode was used. On in CE (ce.ini debrief_stats=0 hides
// it), never in faithful play: off in a replay, a recording, under probes or the virtual clocks, for a test pilot or a
// network game. BEA_PORT_DEBRIEF_PROOF=1 with BEA_PORT_CE_LAYER=1 is a test switch that draws it under a tape, so a
// replay can show that drawing it changes nothing.
#ifdef CE_VERSION

#include "common.h"
#include "CLIParams.h"
#include "ce_debrief.h"

#include <stdlib.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error The CE debrief panel requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SettingGetBool(const char *key, int def);
bool PortCeLayer();
bool PortRecording();

namespace {
const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};
int ini = -1;									// debrief_stats, read once

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}
}

bool PortCeDebriefStatsOn(bool singlePlayer)
{
	if (!PortCeLayer() || !singlePlayer) return false;
	const bool proof = Present("BEA_PORT_DEBRIEF_PROOF") && Present("BEA_PORT_CE_LAYER");
	if (!proof)
	{
		if (PortRecording()) return false;
		for (const char *name : kFenced) if (Present(name)) return false;
	}
	if (CE_BootRequested && CE_BootRequested()) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
		CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode) return false;
	if (ini < 0) ini = CE_SettingGetBool("debrief_stats", 1) ? 1 : 0;
	return ini != 0;
}

// Places the panel's rows in the 640 x 480 screen the end screen is laid out in. Rows go down one column from y, as the
// original's did (x, x + cc for the colon, x + nc for the value; rowStep apart). When they would run past bottom (a level
// with many objectives, or a lost level's reason text), the panel first rises into the gap above it, by rise at most;
// then the rows go in two columns side by side if both fit inside right; else the rows that fit are placed and the
// rest are not drawn. The CHEATER! line (cheatHeight tall) goes under the last row. Returns the rows placed (the
// CHEATER! line, when placed, is the last).
int PortCeDebriefLayout(const CeDebriefPanel &in, float outX[], float outY[])
{
	CeDebriefPanel panel = in;
	const int rows = panel.rows < 0 ? 0 : panel.rows > CE_DEBRIEF_MAX_ROWS ? CE_DEBRIEF_MAX_ROWS : panel.rows;
	float width = 0.f;
	for (int i = 0; i < rows; ++i)
		if (panel.nc + panel.valueWidth[i] > width) width = panel.nc + panel.valueWidth[i];
	const float tall = float(rows) * panel.rowStep + (panel.cheat ? panel.cheatHeight : 0.f);
	const float over = panel.y + tall - panel.bottom;
	if (over > 0.f && over <= panel.rise) panel.y -= over;
	int placed = 0;
	if (panel.y + tall <= panel.bottom || rows < 2)
	{
		for (int i = 0; i < rows && panel.y + float(i + 1) * panel.rowStep <= panel.bottom; ++i, ++placed)
		{
			outX[placed] = panel.x;
			outY[placed] = panel.y + float(i) * panel.rowStep;
		}
	}
	else
	{
		const int first = (rows + 1) / 2;
		const float gap = 30.f, second = panel.x + width + gap;
		const bool sideBySide = second + width <= panel.right;
		for (int i = 0; i < rows; ++i)
		{
			const bool right = sideBySide && i >= first;
			const int row = right ? i - first : i;
			const float y = panel.y + float(row) * panel.rowStep;
			if (y + panel.rowStep > panel.bottom) { if (right) continue; break; }
			outX[placed] = right ? second : panel.x;
			outY[placed] = y;
			++placed;
		}
	}
	if (panel.cheat && placed == rows)
	{
		float y = panel.y;
		for (int i = 0; i < placed; ++i) if (outX[i] == panel.x && outY[i] + panel.rowStep > y) y = outY[i] + panel.rowStep;
		if (y + panel.cheatHeight <= panel.bottom)
		{
			outX[placed] = panel.x;
			outY[placed] = y;
			++placed;
		}
	}
	return placed;
}

#endif
