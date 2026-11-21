// SPDX-License-Identifier: GPL-3.0-only
// Commander mode: what a squad with a standing order is doing, as one word the map can show. The order layer gives it, once a tick, how far the squad's
// centre is from its goal and how many of its members can fire; it answers Moving, Fighting, Arrived, Stuck or Holding. Arrived stays once reached (a
// squad that drifts after it got there has still arrived). Stuck is a march that came no nearer its goal for `stuckTicks` ticks while nobody in it could
// fire; a fight is never Stuck, and the clock starts again after one. Pure arithmetic: no game types; port/tools/ce_cmdorders_driver.cpp runs it.
#ifndef PORT_CE_CMDOUTCOME_H
#define PORT_CE_CMDOUTCOME_H

enum CeCmdOutcome
{
	CE_OUT_NONE = 0,	// no standing order
	CE_OUT_MOVING,		// on the march, getting nearer
	CE_OUT_FIGHTING,	// members can fire at a target (on the march, at the goal, or holding)
	CE_OUT_ARRIVED,		// a march that reached its goal, quiet
	CE_OUT_STUCK,		// a march that has stopped getting nearer, quiet
	CE_OUT_HOLDING		// holding a place, quiet
};

const float kCeTrackProgress = 1.0f;	// world units nearer the goal that count as progress

struct CeCmdTrack
{
	float best;			// the nearest the squad's centre has been to its goal under this order
	unsigned since;		// the tick of that, or of the last fight
	bool arrived;
};

// A new order: the clock and the distance start here.
inline void CeTrackStart(CeCmdTrack &t, float distance, unsigned tick)
{
	t.best = distance;
	t.since = tick;
	t.arrived = false;
}

// One tick of a standing order. march: the order has a goal to reach (Move, Advance, Fall back); otherwise the squad holds. distance: its centre to the goal.
// radius: how near counts as there. engaged: how many members can fire now.
inline int CeTrackStep(CeCmdTrack &t, bool march, float distance, float radius, int engaged, unsigned tick, unsigned stuckTicks)
{
	if (!march) return engaged > 0 ? CE_OUT_FIGHTING : CE_OUT_HOLDING;
	if (!t.arrived && distance < radius) t.arrived = true;
	if (t.arrived) return engaged > 0 ? CE_OUT_FIGHTING : CE_OUT_ARRIVED;
	if (distance <= t.best - kCeTrackProgress) { t.best = distance; t.since = tick; }
	if (engaged > 0) { t.since = tick; return CE_OUT_FIGHTING; }
	return tick - t.since >= stuckTicks ? CE_OUT_STUCK : CE_OUT_MOVING;
}

#endif
