// SPDX-License-Identifier: GPL-3.0-only
// Commander mode: which squads are picked. A selection is a short list of squad ordinals in the order they were picked; the first is the lead, the squad
// whose card the map shows. Ten numbered groups each keep a copy of a selection. A drag on the map picks the squads whose marks lie inside its
// rectangle. Pure bookkeeping and geometry: no game types, nothing allocated; port/tools/ce_cmdorders_driver.cpp runs it.
#ifndef PORT_CE_CMDSEL_H
#define PORT_CE_CMDSEL_H

const int kCeSelMax = 64;		// the most squads one selection holds
const int kCeSelGroups = 10;	// the numbered groups, 0 to 9

struct CeCmdSel
{
	int n;
	int ids[kCeSelMax];
};

inline void CeSelClear(CeCmdSel &s) { s.n = 0; }

inline int CeSelFind(const CeCmdSel &s, int id)
{
	for (int i = 0; i < s.n; ++i)
		if (s.ids[i] == id) return i;
	return -1;
}

inline bool CeSelHas(const CeCmdSel &s, int id) { return CeSelFind(s, id) >= 0; }

// The squad whose card shows: the first one picked, -1 when nothing is.
inline int CeSelLead(const CeCmdSel &s) { return s.n > 0 ? s.ids[0] : -1; }

// Adds a squad at the end. False when it is not a squad's number, is already picked, or the selection is full (nothing changes then).
inline bool CeSelAdd(CeCmdSel &s, int id)
{
	if (id < 0 || s.n < 0 || s.n >= kCeSelMax || CeSelHas(s, id)) return false;
	s.ids[s.n++] = id;
	return true;
}

// Takes a squad out; the others keep their order (so the next one becomes the lead when the lead goes).
inline bool CeSelRemove(CeCmdSel &s, int id)
{
	const int at = CeSelFind(s, id);
	if (at < 0) return false;
	for (int i = at + 1; i < s.n; ++i) s.ids[i - 1] = s.ids[i];
	--s.n;
	return true;
}

// This squad alone (nothing when it is not a squad's number).
inline void CeSelOnly(CeCmdSel &s, int id)
{
	s.n = 0;
	CeSelAdd(s, id);
}

// A Shift click: the squad joins the selection, or leaves it when it was in. Returns whether it is in afterwards.
inline bool CeSelToggle(CeCmdSel &s, int id)
{
	if (CeSelRemove(s, id)) return false;
	return CeSelAdd(s, id);
}

// Keeps the squads `keep` says yes to (after a fresh look at the battle: a squad that is gone or takes no orders any more drops out). Returns how many went.
template <class Keep> inline int CeSelPrune(CeCmdSel &s, Keep keep)
{
	int kept = 0;
	const int before = s.n;
	for (int i = 0; i < before; ++i)
		if (keep(s.ids[i])) s.ids[kept++] = s.ids[i];
	s.n = kept;
	return before - kept;
}

struct CeCmdGroups
{
	CeCmdSel group[kCeSelGroups];
};

inline void CeGroupsClear(CeCmdGroups &g)
{
	for (int k = 0; k < kCeSelGroups; ++k) g.group[k].n = 0;
}

// Keeps the selection under a number. An empty selection empties the group. False when the number is not a group's.
inline bool CeGroupStore(CeCmdGroups &g, int k, const CeCmdSel &s)
{
	if (k < 0 || k >= kCeSelGroups) return false;
	g.group[k] = s;
	return true;
}

// The group as the selection. False, and the selection untouched, when the number is not a group's or the group is empty.
inline bool CeGroupRecall(const CeCmdGroups &g, int k, CeCmdSel &s)
{
	if (k < 0 || k >= kCeSelGroups || g.group[k].n <= 0) return false;
	s = g.group[k];
	return true;
}

// Whether a press at (x0, y0) and a release at (x1, y1) is a drag: the pointer went at least `slack` pixels along one axis. Less is a click.
inline bool CeBoxIsDrag(float x0, float y0, float x1, float y1, float slack)
{
	const float dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
	return dx >= slack || dy >= slack;
}

// Whether a mark at (px, py) lies inside the rectangle the two corners span, edges included, whichever way it was dragged.
inline bool CeBoxHas(float x0, float y0, float x1, float y1, float px, float py)
{
	const float left = x0 < x1 ? x0 : x1, right = x0 < x1 ? x1 : x0, top = y0 < y1 ? y0 : y1, bottom = y0 < y1 ? y1 : y0;
	return px >= left && px <= right && py >= top && py <= bottom;
}

#endif
