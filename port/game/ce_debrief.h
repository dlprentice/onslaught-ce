// SPDX-License-Identifier: GPL-3.0-only
// The CE end-of-level statistics panel (port/game/ce_debrief.cpp), drawn by patch 0104 in CGame's end screen.
#ifndef PORT_CE_DEBRIEF_H
#define PORT_CE_DEBRIEF_H

enum { CE_DEBRIEF_MAX_ROWS = 6 };

// One panel in the end screen's 640 x 480 layout: where it starts, the edges it must stay inside, how far it may rise
// into the gap above it, the value column (nc, measured by the retail label loop), each row's value width, and the
// CHEATER! line.
struct CeDebriefPanel
{
	float	x, y, bottom, right, rise, nc, rowStep, cheatHeight;
	float	valueWidth[CE_DEBRIEF_MAX_ROWS];
	int		rows;
	bool	cheat;
};

// Whether the panel is drawn: the CE layer and its fences, one player, and ce.ini's debrief_stats (default on).
bool	PortCeDebriefStatsOn(bool singlePlayer);
// The rows' positions (outX/outY hold CE_DEBRIEF_MAX_ROWS + 1); returns how many were placed, CHEATER! last if placed.
int		PortCeDebriefLayout(const CeDebriefPanel &panel, float outX[], float outY[]);

#endif
