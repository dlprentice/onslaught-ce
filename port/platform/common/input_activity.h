// SPDX-License-Identifier: GPL-3.0-only
// Main-thread input-route observations only: counts, not key text, coordinates, button identities or device IDs.
// This header's inline state is shared by window.cpp and dinput.cpp; neither event watches nor callbacks enter it.
#ifndef PORT_INPUT_ACTIVITY_H
#define PORT_INPUT_ACTIVITY_H
#include "portlog.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <limits.h>

namespace PortInputActivity
{
enum Event { MOTION, BUTTON, WHEEL, CAPTURE_SUPPRESSED, QUEUED, COALESCED, DEQUEUED, DISPATCHED, DROPPED,
	POLL, NONZERO_POLL, UNACQUIRED_POLL, EVENTS };
struct State
{
	unsigned count[EVENTS] = {};
	unsigned summaries = 0;
	Uint64 lastMs = 0;
	unsigned lastFlags = 0;
	bool acquired = false, lastAcquired = false, lastRelative = false, observed = false;
};
inline State &Current() { static State state; return state; }
inline void Count(Event event)
{
	if (!PortSessionEnabled() || event < 0 || event >= EVENTS) return;
	unsigned &value = Current().count[event];
	if (value < UINT_MAX) ++value;
}
inline void Acquired(bool acquired)
{
	if (PortSessionEnabled()) Current().acquired = acquired;
}
inline bool MouseMessage(unsigned message)
{
	return message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
		message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_MBUTTONDOWN ||
		message == WM_MBUTTONUP || message == WM_MOUSEWHEEL;
}
inline void Pulse(SDL_Window *window, bool final = false)
{
	if (!PortSessionEnabled()) return; // No getters, clock reads or state changes on faithful/private/tape routes.
	int savedErrno = errno;
#ifdef _WIN32
	DWORD savedLastError = GetLastError();
#endif
	State &state = Current();
	const unsigned mask = SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN;
	unsigned flags = window ? unsigned(SDL_GetWindowFlags(window)) & mask : 0;
	bool relative = window && SDL_GetWindowRelativeMouseMode(window);
	Uint64 now = SDL_GetTicks();
	bool changed = !state.observed || state.lastFlags != flags || state.lastRelative != relative ||
		state.lastAcquired != state.acquired;
	if (final || changed || now - state.lastMs >= 10000)
	{
		state.observed = true; state.lastMs = now; state.lastFlags = flags;
		state.lastRelative = relative; state.lastAcquired = state.acquired;
		// At most 256 interval/state lines plus one suppression notice; the logger separately admits at most
		// eight final owned-window summaries/process, even if its ordinary-event budget is exhausted.
		if (final || state.summaries < 256)
		{
			++state.summaries;
			const unsigned *c = state.count;
			PortSessionWindowSummary(final,
				"mouse activity cumulative final=%d window=%d focus=%d pointer_focus=%d minimized=%d hidden=%d acquired=%d relative=%d "
				"sdl_motion=%u sdl_buttons=%u sdl_wheel=%u capture_suppressed=%u menu_queued=%u menu_coalesced=%u "
				"menu_dequeued=%u menu_dispatched=%u menu_dropped=%u di_polls=%u di_nonzero=%u di_unacquired=%u "
				"(route counts only; not proof of game response or device health)",
				final, window != NULL, !!(flags & SDL_WINDOW_INPUT_FOCUS), !!(flags & SDL_WINDOW_MOUSE_FOCUS),
				!!(flags & SDL_WINDOW_MINIMIZED), !!(flags & SDL_WINDOW_HIDDEN), state.acquired, relative,
				c[MOTION], c[BUTTON], c[WHEEL], c[CAPTURE_SUPPRESSED], c[QUEUED], c[COALESCED], c[DEQUEUED],
				c[DISPATCHED], c[DROPPED], c[POLL], c[NONZERO_POLL], c[UNACQUIRED_POLL]);
		}
		else if (state.summaries == 256)
		{
			++state.summaries;
			PortSessionLog(PORT_SESSION_WINDOW, false, "mouse activity interval limit reached; cumulative counters continue until window close");
		}
	}
	errno = savedErrno;
#ifdef _WIN32
	SetLastError(savedLastError);
#endif
}
}
#endif
