// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/platform.h: helpers the platform layer shares across its files and targets.
#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H
#include <windows.h>

#include "portlog.h"

struct SDL_Window;

// The SDL window behind one of the game's HWNDs (window.cpp): on Linux the HWND is the SDL_Window (dxvk-native's
// convention); on Windows it is that SDL window's Win32 handle.
SDL_Window *PortSdlWindow(HWND hwnd);
void PortMouseKeepRelative();	// dinput.cpp: re-assert relative mode for the acquired mouse each frame
// Posts to the game's message queue (window.cpp; d3d9hooks' BEA_PORT_QUIT).
extern "C" BOOL WINAPI PortPostMessageA(HWND h, UINT m, WPARAM w, LPARAM l);
SDL_Window *PortMainSdlWindow();	// window.cpp: the game's first window (NULL before it exists)
// Widescreen and native resolutions (widescreen.cpp; the game's side is declared in port.h).
void PortVideoInit();				// SDL video with the port's hints, once
void PortWideBackBuffer(unsigned w, unsigned h);	// d3d9hooks.cpp: the device's back buffer at CreateDevice and Reset
int PortWideModes();
int PortUiMouseMenu(int mouseInputActive);		// widescreen.cpp: an in-game menu's pointer is the virtual screen's (the game's mouse released, CE layer, the commander map not open)
void PortUiMouseMenuPoint(int *x, int *y);	// a window position (back buffer pixels) as that pointer
int PortWideDesktopSize(unsigned *w, unsigned *h);
int PortWideDisplaySizes(unsigned (*out)[2], int max);
int PortWideActive();
int PortSharpUi();
float PortUiScale();
float PortUiLeft();
float PortUiTop();
void PortWideViewport(int box);	// d3d9hooks.cpp
// Lets SDL refresh keyboard, mouse and controller state between the game's message pumps.
void PortPumpInput();
// With a test run's dummy-audio requirement: start SDL audio on the dummy driver, log it, exit if it is another
// (audioguard.cpp; Linux's main and PortDirect3DCreate9 call it, before the game opens any sound).
void PortAudioGuardCheck();
// Input tapes and probes (replay.cpp): called after each Present returns, then the frame's probes and events.
void PortReplayFrame(unsigned frame, HWND window);
// The end-level probes written at exit (replay.cpp; Linux's main calls it before _exit, exit() otherwise).
void PortReplayAtExit();
// The tape's mouse since the last call (movement consumed, buttons as a level), or false without a tape.
bool PortReplayMouse(long *dx, long *dy, long *dz, int *buttons);
// Record mode (BEA_PORT_RECORD, replay.cpp): live input queued for the next Present and written as a tape.
bool PortRecording();
void PortRecordKey(bool down, unsigned vk, unsigned scan);
void PortRecordWheel(float steps);
void PortRecordMotion(float dx, float dy);
void PortRecordButton(int bit, bool down);
// Record mode's window mouse: WM_MOUSEMOVE, a button message or WM_MOUSEWHEEL (wheelDelta in WHEEL_DELTA units), with
// the lParam the live port would post; written as W lines while the game does not hold the DirectInput mouse.
void PortRecordWindowMouse(UINT msg, LPARAM l, int wheelDelta);
// Record mode's typed text: the character a key-down makes (window.cpp KeyChar), written as a C line after the key.
void PortRecordChar(unsigned c, unsigned scan);
// A tape-2 replay or a recording: the cursor is the last W position delivered (client pixels), else false.
bool PortTapeCursor(POINT *p);
// dinput.cpp: whether the game holds (has acquired) the DirectInput mouse.
bool PortDinputMouseAcquired();
// The player's game folder (read only) and the port's writable folder (port/platform/<target>/...paths).
const char *PortGameDir();
const char *PortUserDir();

// The clocks (clock.cpp): BEA_PORT_CLOCK = present (deterministic) or shim (the retail replay shim's clock), else real.
bool PortClockVirtual();
bool PortClockStandsBetweenPresents();		// shim clock without BEA_SLEEP_VIRTUAL: films play one frame per Present
bool PortClockFilmFrameWaits();				// patch 0053: the film player's first look after each Present waits (shim)
void PortClockFrame();							// d3d9hooks.cpp, after each Present returns
unsigned long long PortClockFrames();
unsigned long long PortClockPeekNs();				// virtual ns now, not counted as a read (0: real, or shim before its first Present)
long long PortClockQpf();						// virtual counter frequency (0 when real)
long long PortClockQpc();
unsigned long PortClockMs();					// timeGetTime, GetTickCount
unsigned long long PortClockFiletime();			// 100 ns units since 1601 (FILETIME)
unsigned long long PortClockRawNs();			// the operating system's monotonic clock in nanoseconds (the real clock's base)
unsigned long long PortClockShiftNs(unsigned long long rawNs);	// how far the game's real clock is behind it now (0 outside the CE layer's real clock)
unsigned long long PortClockReadBegin();		// bracket the raw reading and its shift; retry if a wait began or ended in between
bool PortClockReadValid(unsigned long long generation);
void PortClockPauseBegin();						// window.cpp: the game starts waiting for its minimized window: the real clock stands still (CE layer, real clock only)
void PortClockPauseEnd();						// and carries on from where it stood when the wait ends, so the game does not run the time it was away as one burst
void PortClockSleep(unsigned long ms);			// a Sleep(ms) happened

#endif
