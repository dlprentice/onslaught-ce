// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_hooks_dispatch.h: the rows of the CE hook table and the ways a point runs them (ce_hooks.cpp). Kept apart
// from the table so host tests drive each way with tables of their own (port/tools/ce_hooks_tests.py).
//
// A row's tag says what its handler itself may write: SIM (game state) or RENDER (presentation only). It must equal its
// point's tag. Test builds put a guard around every handler (CeHookGuardBefore/After, ce_hooks_check.cpp): the guard is
// told the point, the handler and its tag, and may fail the run when a RENDER handler changed the simulation.
//
// Dispatch, one per point:
//   ORDERED_BROADCAST  every open row, in table order (boot, exit, level_*, tick_pre, input_actions, draw_*, audio_clock,
//                      unit_spawn, unit_destroyed, weapon_fire, render_pass)
//   FIRST_MATCH        rows in order until one answers true (key_capture: the first handler that consumes the key)
//   POSITIONAL_INSERT  every open row in order; each handler names the position of what it adds (menu_rows,
//                      CeHookPlace), so table order decides only between equal positions
//   FILTER             rows in order, each working on the previous one's output (save_gate: the same buffer;
//                      spawner_trigger: the same CeSpawnerTrigger decision struct)
//   COMMAND_APPLY      queued commands (CeHookCommandPush), oldest first, each given to every open row in order, then the
//                      queue is emptied (tick_post: what input and presentation asked for reaches the simulation here)
//   OVERRIDE_CHAIN     each open row takes the value the previous one returned (render_camera, hud_focus)
#ifndef PORT_CE_HOOKS_DISPATCH_H
#define PORT_CE_HOOKS_DISPATCH_H

#include "ce_modes.h"

enum CeHookTag { CE_HOOK_SIM, CE_HOOK_RENDER };
enum CeHookGate { CE_GATE_VERSION, CE_GATE_LAYER };
enum CeHookDispatch { CE_DISPATCH_ORDERED_BROADCAST, CE_DISPATCH_FIRST_MATCH, CE_DISPATCH_POSITIONAL_INSERT, CE_DISPATCH_FILTER,
                      CE_DISPATCH_COMMAND_APPLY, CE_DISPATCH_OVERRIDE_CHAIN };

template <class Fn> struct CeHookRow
{
	int			slot;		// the order the row runs in: unique and ascending within one point
	const char	*name;		// NULL ends a table
	CeHookTag	tag;		// what the handler itself may write; must equal the point's tag
	CeHookGate	gate;
	CeMode		mode;		// CE_MODE_NONE, or the mode that must be active
	Fn			fn;
};
#define CE_HOOK_END {0, nullptr, CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, nullptr}

template <class Fn, unsigned N> constexpr bool CeHookEnded(const CeHookRow<Fn> (&rows)[N])
{
	for (unsigned i = 0; i + 1 < N; i++)
		if (!rows[i].name || !rows[i].fn)
			return false;
	return rows[N - 1].name == nullptr;
}
template <class Fn, unsigned N> constexpr bool CeHookSlotsAscend(const CeHookRow<Fn> (&rows)[N])
{
	for (unsigned i = 1; i + 1 < N; i++)
		if (rows[i].slot <= rows[i - 1].slot)
			return false;
	return true;
}
template <class Fn, unsigned N> constexpr bool CeHookTagsAre(const CeHookRow<Fn> (&rows)[N], CeHookTag tag)
{
	for (unsigned i = 0; i + 1 < N; i++)
		if (rows[i].tag != tag)
			return false;
	return true;
}
template <class Fn, unsigned N> constexpr bool CeHookWorkerSafe(const CeHookRow<Fn> (&rows)[N])
{
	for (unsigned i = 0; i + 1 < N; i++)
		if (rows[i].gate != CE_GATE_VERSION || rows[i].mode != CE_MODE_NONE)
			return false;
	return true;
}
// A point's table: its rows, its tag and its dispatch, checked while compiling.
#define CE_HOOK_CHECK(table, tag, dispatch, point) \
	static_assert(CeHookEnded(table), point ": a row without a name or a function, or a table without its end"); \
	static_assert(CeHookSlotsAscend(table), point ": two handlers claim one slot, or the slots are out of order"); \
	static_assert(CeHookTagsAre(table, tag), point ": a handler's SIM/RENDER tag is not the point's"); \
	static constexpr CeHookDispatch table##Dispatch = dispatch

bool PortCeLayer();		// port/platform/common/ce.cpp

// The guard test builds put around each handler; other builds compile it away.
struct CeHookGuard
{
	const char			*point;
	const char			*handler;
	CeHookTag			tag;
	unsigned long long	state;		// the guard's own (the stub keeps a digest of simulation state here)
};
#if defined(CE_VERSION) && defined(CE_TEST_BUILD)
#define CE_HOOKS_GUARDED 1
void CeHookGuardBefore(CeHookGuard *guard);
void CeHookGuardAfter(CeHookGuard *guard);
#else
#define CE_HOOKS_GUARDED 0
inline void CeHookGuardBefore(CeHookGuard *) {}
inline void CeHookGuardAfter(CeHookGuard *) {}
#endif

template <class Fn> inline bool CeHookOpen(const CeHookRow<Fn> &row)
{
	return (row.gate == CE_GATE_VERSION || PortCeLayer()) && CeModeActive(row.mode);
}

// Calls row's handler through the guard; `call` takes the handler and returns its answer (void for none).
template <class Fn, class Call> inline auto CeHookRun(const char *point, const CeHookRow<Fn> &row, Call call) -> decltype(call(row.fn))
{
	struct Scope
	{
		CeHookGuard guard;
		~Scope() { CeHookGuardAfter(&guard); }
	} scope = {{point, row.name, row.tag, 0}};
	CeHookGuardBefore(&scope.guard);
	return call(row.fn);
}

template <class Fn, class... A> inline void CeHookBroadcast(const char *point, const CeHookRow<Fn> *rows, A... args)
{
	for (const CeHookRow<Fn> *r = rows; r->name; ++r)
		if (CeHookOpen(*r))
			CeHookRun(point, *r, [&](Fn fn) { fn(args...); });
}
template <class Fn, class... A> inline bool CeHookFirstMatch(const char *point, const CeHookRow<Fn> *rows, A... args)
{
	for (const CeHookRow<Fn> *r = rows; r->name; ++r)
		if (CeHookOpen(*r) && CeHookRun(point, *r, [&](Fn fn) { return fn(args...); }))
			return true;
	return false;
}
// FILTER: the handlers share the buffer the arguments point at, so each sees what the one before it wrote.
template <class Fn, class... A> inline void CeHookFilter(const char *point, const CeHookRow<Fn> *rows, A... args)
{
	CeHookBroadcast(point, rows, args...);
}
template <class Fn, class T, class... A> inline T CeHookOverride(const char *point, const CeHookRow<Fn> *rows, T value, A... args)
{
	for (const CeHookRow<Fn> *r = rows; r->name; ++r)
		if (CeHookOpen(*r))
			value = CeHookRun(point, *r, [&](Fn fn) { return fn(value, args...); });
	return value;
}

// POSITIONAL_INSERT: where a row asked for `position` lands in a list of `count` rows (-1 or past the end appends).
inline int CeHookPlace(int count, int position)
{
	return (position < 0 || position > count) ? count : position;
}

// COMMAND_APPLY: a fixed queue (main thread), applied and emptied at tick_post.
struct CeHookCommand
{
	int		kind;		// the owner's own numbering; 0 is never used
	int		a, b;
	float	x, y, z;
};
enum { CE_HOOK_COMMANDS = 64 };
struct CeHookCommandQueue
{
	CeHookCommand	items[CE_HOOK_COMMANDS];
	int				count;
	bool Push(const CeHookCommand &command)
	{
		if (count >= CE_HOOK_COMMANDS || command.kind == 0)
			return false;
		items[count++] = command;
		return true;
	}
};
template <class Fn> inline void CeHookApply(const char *point, const CeHookRow<Fn> *rows, CeHookCommandQueue *queue)
{
	if (!queue || queue->count <= 0)
		return;
	const int n = (queue->count > CE_HOOK_COMMANDS) ? CE_HOOK_COMMANDS : queue->count;
	CeHookCommand snapshot[CE_HOOK_COMMANDS];
	for (int i = 0; i < n; i++)
		snapshot[i] = queue->items[i];
	queue->count = 0;
	for (int i = 0; i < n; i++)
		CeHookBroadcast(point, rows, (const CeHookCommand *)&snapshot[i]);
}

#endif
