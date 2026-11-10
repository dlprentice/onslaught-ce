// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/feel.h: frame-feel measurement, for judging how the game paces and responds on a given machine.
//
// Off unless BEA_PORT_FEEL_LOG names a file, and then only where the CE layer is admitted: never in a replay, a recording,
// probes or a virtual clock (the same fence PortCeLayer gives every CE addition). Off, every entry point returns after one
// test of a cached flag: nothing is allocated, read, written, slept or timed. On, the module only observes: it keeps a
// private list of timestamped events in memory and writes it to the named file at exit (and every
// BEA_PORT_FEEL_FLUSH_S seconds, default 30, when a frame is not being measured for its own sake: the flush is itself
// an event). It never changes a value the game reads. The one thing it can do to timing is the optional display model
// (BEA_PORT_FEEL_PACE_HZ=N), which makes Present return on a fixed grid of N per second the way a vertical sync does; it is
// a measurement aid for machines whose offscreen route has no display, and is off unless asked for.
//
// Environment (all read once):
//   BEA_PORT_FEEL_LOG=PATH        the log (written with the C library's fopen); required
//   BEA_PORT_FEEL_PACE_HZ=N       model a display of N Hz: Present returns at the next grid time (default: not modelled)
//   BEA_PORT_FEEL_GPU=0           leave out the Direct3D timestamp queries (default: on)
//   BEA_PORT_FEEL_HEAP=1          also sample the C library's heap at each Present (default: off; it takes the allocator lock)
//   BEA_PORT_FEEL_FLUSH_S=N       seconds between writes while running (default 30; 0: write only at exit)
//   BEA_PORT_FEEL_MAX_EVENTS=N    stop recording after N events (default 4,000,000)
//   BEA_PORT_FEEL_SCRIPT=PATH     authored input on a wall-clock timeline (below); needs the log
//   BEA_PORT_FEEL_SHOTS=FIRST:COUNT:DIR   write COUNT consecutive frames from Present FIRST as DIR/shot-NNNNN.ppm (looking at motion)
//
// The input script is a text file of lines (blank lines and # comments are ignored); times are milliseconds after the arm:
//   arm present N                 start the timeline when the Nth Present has completed
//   arm state S [K]               start it after K consecutive unpaused updates (default 20) in game phase S
//   key MS d|u SDLSCAN RAWHEX     a key goes down or up (the same events as BEA_PORT_FAKE_KEYS' numeric token, by time)
//   mouse FROM_MS TO_MS VX VY     a relative mouse moving at VX,VY counts per second between the two times, delivered to the
//                                 game's DirectInput mouse polls as the movement since the last poll (a steady stream, as a real
//                                 mouse's many small reports are)
//   shots MS COUNT DIR            write COUNT consecutive frames from the first Present at or after MS as DIR/shot-NNNNN.ppm
//   quit MS                       end the game, as closing its window does
// What the game's own polls received is in the log, so the input a run was given is known exactly.
//
// The log is text, one event per line, fields separated by spaces; port/tools/feel_report.py reads it. Times are
// nanoseconds on the real monotonic clock since the log opened, whatever clock the game reads.
#ifndef PORT_FEEL_H
#define PORT_FEEL_H

// Every entry point is a weak reference, so a program or test that compiles a caller without feel.cpp still links (the
// reference is then null and PORT_FEEL_ON() is false). Callers write `if (PORT_FEEL_ON()) PortFeelXxx(...)`.
#define PORT_FEEL_WEAK __attribute__((weak))
#define PORT_FEEL_ON() (PortFeelOn != 0 && PortFeelOn())

#ifdef __cplusplus
extern "C" {
#endif

// 1 while measurement is on (decided at the first call).
int PortFeelOn(void) PORT_FEEL_WEAK;

// ---- the game and the platform layer ---------------------------------------------------------------------------------------
// A free-text marker in the log (a scene, a step of a script). Ignored when off.
void PortFeelNote(const char *text) PORT_FEEL_WEAK;
// CGame::Update began (port/game/updatedump.cpp, the hook at its top).
void PortFeelTick(int level, int state, int paused, unsigned eventTimeBits) PORT_FEEL_WEAK;
// The game's own sampler of the render fraction and friends; called at each Present (port/game/updatedump.cpp).
// focus: 0 none; 1 a driven unit's pose in pose[0..11] (displayed position, simulated position, displayed heading, simulated heading); 2 the paused
// map's window on the world in pose[0..2] (corner x, corner y, span).
struct PortFeelGameSample { float fraction, fraction2; int level, state, paused; int focus; float pose[12]; };
void PortFeelSetGameSampler(void (*sampler)(struct PortFeelGameSample *)) PORT_FEEL_WEAK;
// The window layer's event pump ran (a pass of the main loop starts with it).
void PortFeelPump(void) PORT_FEEL_WEAK;
// An SDL event reached the pump: its kind, and how long ago the system stamped it (nanoseconds; 0 when unknown).
void PortFeelEvent(unsigned type, unsigned long long ageNs) PORT_FEEL_WEAK;
// The game polled DirectInput (dinput.cpp): a mouse (its movement since the last poll) or a keyboard (keys down).
void PortFeelMousePoll(long dx, long dy, unsigned buttons) PORT_FEEL_WEAK;
// Authored input from the script: the mouse counts due since the last call (added to the poll's own), and the next key step
// that is due, if any (returns 1 and fills the step). Both return nothing until the script's arm has happened.
void PortFeelMouseTake(long *dx, long *dy) PORT_FEEL_WEAK;
int PortFeelKeyDue(unsigned *scancode, unsigned *raw, int *down) PORT_FEEL_WEAK;
// 1 when this Present's frame (Present number `present`, counted from 1) is to be written, from the script's shots verb or BEA_PORT_FEEL_SHOTS, with
// the folder in `dir` (at most `size` bytes).
int PortFeelShotDue(char *dir, unsigned size, unsigned present) PORT_FEEL_WEAK;
void PortFeelKeyPoll(unsigned keysDown, unsigned stateHash) PORT_FEEL_WEAK;

// ---- Direct3D 9 (d3d9hooks.cpp) ---------------------------------------------------------------------------------------------
// Called with the table the device's hooks live in, once the device exists; the module then wraps BeginScene, EndScene,
// SetTransform and the four draws, each calling the entry the table held when it was installed. `install` replaces one entry.
typedef void (*PortFeelInstallFn)(void **table, int slot, void *fn);
void PortFeelDeviceCreated(void *device, void **table, PortFeelInstallFn install) PORT_FEEL_WEAK;
// The device is about to be reset: its timestamp queries are released first, as Direct3D requires, and made again afterwards.
void PortFeelBeforeReset(void) PORT_FEEL_WEAK;
void PortFeelPresentBegin(void *device) PORT_FEEL_WEAK;
void PortFeelPresentEnd(void *device, long result) PORT_FEEL_WEAK;
// Write what is buffered (called at exit and by the module itself).
void PortFeelFlush(void) PORT_FEEL_WEAK;

#ifdef __cplusplus
}
#endif
#endif
