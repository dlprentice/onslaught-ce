// SPDX-License-Identifier: GPL-3.0-only
// Entire production recorder/parser and window queue; fake SDL input/clock only. All files belong to this fixture.
#include <vector>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <string>
#include <unistd.h>
#include <sys/prctl.h>
#include <windows.h>
#include <SDL3/SDL.h>
static Uint64 ticks = 5000000000ULL, delayed;
static unsigned backWidth = 640, backHeight = 480;
static unsigned present;
static Uint64 TestTicks() { return ticks; }
static Uint64 TestTicksMs() { return ticks / 1000000; }
static void TestDelay(Uint64 value) { ticks += value; delayed += value; }
static std::deque<SDL_Event> input;
static bool sdkKeys[SDL_SCANCODE_COUNT];
static bool TestPoll(SDL_Event *event) { if (input.empty()) return false; *event = input.front(); input.pop_front(); return true; }
static const bool *TestKeyboard(int *size) { *size = SDL_SCANCODE_COUNT; return sdkKeys; }
static SDL_Keycode TestKey(SDL_Scancode sc, SDL_Keymod, bool) {
    return sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z ? 'a' + sc - SDL_SCANCODE_A : 0;
}
static bool failWrite, failFlush, failClose, failPublish, failOpen, failRegistration, failStream, failProbeRegistration;
static std::vector<std::vector<long long>> mouseSamples;
static FILE *recordTarget;
static int TestRegister(void (*)(void));
static int TestError(FILE *);
static int TestWrite(FILE *, const char *, va_list);
static int TestPrint(FILE *, const char *, ...);
static int TestFlush(FILE *);
static int TestClose(FILE *);
static int TestRename(int, const char *, int, const char *, unsigned);
static FILE *TestOpen(const char *, const char *);
#define SDL_GetTicksNS TestTicks
#define SDL_GetTicks TestTicksMs
#define SDL_DelayNS TestDelay
#define SDL_PollEvent TestPoll
#define SDL_GetKeyboardState TestKeyboard
#define SDL_GetKeyFromScancode TestKey
#include "WINDOW_CPP"
#define atexit TestRegister
#define ferror TestError
#define vfprintf TestWrite
#define fprintf TestPrint
#define fflush TestFlush
#define fclose TestClose
#define renameat2 TestRename
#define fopen TestOpen
#include "REPLAY_CPP"
#undef atexit
#undef ferror
#undef vfprintf
#undef fprintf
#undef fflush
#undef fclose
#undef renameat2
#undef fopen
static int TestRegister(void (*function)(void)) {
    return failRegistration || (failProbeRegistration && function == EldAtExit) ? 1 : atexit(function);
}
static int TestError(FILE *stream) {
    if (failStream && stream == recordTarget) { failStream = false; return 1; }
    return ferror(stream);
}
static int TestWrite(FILE *stream, const char *format, va_list args) {
    if (failWrite && stream != stderr && stream != stdout) { failWrite = false; errno = ENOSPC; return -1; }
    return vfprintf(stream, format, args);
}
static int TestPrint(FILE *stream, const char *format, ...) {
    va_list args; va_start(args, format); int result = TestWrite(stream, format, args); va_end(args); return result;
}
static int TestFlush(FILE *stream) {
    if (failFlush && stream == recordTarget) { failFlush = false; errno = EIO; return EOF; }
    return fflush(stream);
}
static int TestClose(FILE *stream) {
    bool fail = failClose && stream == recordTarget;
    if (fail) failClose = false;
    int result = fclose(stream);
    if (fail) { errno = EIO; return EOF; }
    return result;
}
static int TestRename(int olddir, const char *oldpath, int newdir, const char *newpath, unsigned flags) {
    if (failPublish) { errno = EACCES; return -1; }
    return renameat2(olddir, oldpath, newdir, newpath, flags);
}
static FILE *TestOpen(const char *path, const char *mode) {
    if (failOpen && strchr(mode, 'x')) { errno = EACCES; return nullptr; }
    return fopen(path, mode);
}
extern "C" {
char LT[0x331e4], GAME[0x400], EVENT_MANAGER[0x20], END_LEVEL_DATA[0x5cc], FRONTEND[0xbe20];
float gFrontEndFrameStep;
bool gInFrontEnd;
unsigned PortPresentCount() { return present; }
void PortCrtRandState(unsigned *calls, unsigned *hold) { *calls = *hold = 0; }
int PortBackBuffer(unsigned *w, unsigned *h) { *w = backWidth; *h = backHeight; return 1; }
int PortMapMouse(float *, float *) { return 0; }
}
static bool acquired;
bool PortDinputMouseAcquired() { return acquired; }
bool PortDinputOwnsMouseEvent(Uint64) { return acquired; }
int PortUiMouseMenu(int) { return 0; }		// the CE layer's menu pointer: off here (a recording and its replay are the faithful path), so the queue ends no batch at a button release
void PortUiMouseMenuPoint(int *, int *) {}
void PortMouseKeepRelative() {}
int PortWideModes() { return 0; }
void PortVideoInit() {}
bool PortDevLog() { return false; }
bool PortSessionEnabled() { return false; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
// The session inventories (inventory.cpp) write only through the session log, which is off here.
void PortSessionDisplayInventory(SDL_Window *) {}
void PortSessionInputInventory() {}
void PortSessionAudioInventory() {}
void PortSessionSettingsInventory() {}
struct Message { UINT type; WPARAM w; LPARAM l; };
static std::vector<Message> messages;
static unsigned keyDown, keyUp, chars, buttons, moves, wUp;
static bool keyHeld[256];
static LRESULT WINAPI Proc(HWND, UINT message, WPARAM w, LPARAM l) {
    messages.push_back({message, w, l});
    if (message == WM_KEYDOWN) { if (w < 256) keyHeld[w] = true; ++keyDown; }
    if (message == WM_KEYUP) { if (w < 256) keyHeld[w] = false; ++keyUp; if (w == 'W') ++wUp; }
    if (message == WM_CHAR) ++chars;
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) ++buttons;
    if (message == WM_MOUSEMOVE) ++moves;
    return 0;
}
static void Drain() { MSG message; while (Pop(&message, true)) PortDispatchMessageA(&message); }
static void Frame(unsigned number) { present = number; PortReplayFrame(number, reinterpret_cast<HWND>(1)); Drain(); }
static void Key(SDL_Scancode sc, bool down) {
    SDL_Event event{}; event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = sc; event.key.key = TestKey(sc, SDL_KMOD_NONE, false); event.key.down = down;
    sdkKeys[sc] = down; input.push_back(event);
}
static void Mouse(Uint32 type, float x = 100, float y = 200) {
    SDL_Event event{}; event.type = type; event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x; event.button.y = y; input.push_back(event);
}
static void Fill(size_t count) { for (size_t i = 0; i < count; ++i) PortRecordChar(0x100 + i, 0x2d); }
static void Exhaust(unsigned from = 3) {
    for (unsigned frame = from; frame < from + 24; ++frame) {
        Pump();
        if (!gPendingCount && input.empty() && !gReleasePending && (!gPilot || !*gPilot) && !gPilotW) return;
        Frame(frame);
    }
}
static void Normal() {
    PortRecordKey(true, 'W', 0x11); PortRecordChar('w', 0x11);
    PortRecordWindowMouse(WM_MOUSEMOVE, MAKELPARAM(100, 200), 0);
    PortRecordWindowMouse(WM_LBUTTONDOWN, MAKELPARAM(100, 200), 0);
    PortRecordWindowMouse(WM_LBUTTONUP, MAKELPARAM(100, 200), 0);
    Frame(1);
    acquired = true;
    PortRecordKey(false, 'W', 0x11); PortRecordMotion(3.25f, -2.75f); PortRecordWheel(1); PortRecordButton(0, true);
    Frame(2);
    long dx, dy, dz; int level;
    if (!PortReplayMouse(&dx, &dy, &dz, &level) || dx != 3 || dy != -2 || dz != 120 || level != 1) exit(4);
    if (!PortReplayMouse(&dx, &dy, &dz, &level) || dx || dy || dz || level != 1) exit(4);
    PortRecordMotion(.75f, .75f); Frame(3);
    PortRecordButton(0, false); Frame(4);
}
static void Summary(unsigned heldBefore = 0) {
    printf("{\"messages\":[");
    for (size_t i = 0; i < messages.size(); ++i)
        printf("%s[%u,%u,%d]", i ? "," : "", messages[i].type, (unsigned)messages[i].w, (int)messages[i].l);
    printf("],\"key_down\":%u,\"key_up\":%u,\"w_up\":%u,\"w_held\":%s,\"chars\":%u,\"buttons\":%u,\"moves\":%u,"
           "\"delayed_ns\":%llu,\"pending\":%zu,\"held_before\":%u,\"held_after\":%d,\"sdl_backlog\":%zu,\"mouse\":[",
           keyDown, keyUp, wUp, keyHeld['W'] ? "true" : "false", chars, buttons, moves,
           (unsigned long long)delayed, gPendingCount, heldBefore, gHeld, input.size());
    for (size_t i = 0; i < mouseSamples.size(); ++i) {
        printf("%s[", i ? "," : "");
        for (size_t j = 0; j < mouseSamples[i].size(); ++j) printf("%s%lld", j ? "," : "", mouseSamples[i][j]);
        printf("]");
    }
    printf("]}\n");
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const char *test = argv[1];
    gNumWindows = 1; gWindows[0].sdl = reinterpret_cast<SDL_Window *>(1);
    gWindows[0].handle = reinterpret_cast<HWND>(1); gWindows[0].proc = Proc;
    if (!strcmp(test, "tape-missing") || !strcmp(test, "tape-missing-stdout") || !strcmp(test, "tape-and-record") || !strcmp(test, "tape-ok") ||
        !strcmp(test, "probes-unwritable") || !strcmp(test, "probes-unwritable-recording")) {
        // A tape or a probe file that cannot be started, found at the first Present while the real game has its device and dxvk's worker
        // threads running (replay_stop_tests.py): the process must stop at once with status 3. This exit handler stands in for a library
        // destructor that crashes under a running worker thread: it is reached only through exit().
        // The crash is on purpose and must be silent: not dumpable, so the kernel runs no core handler (no core file, no coredump record in
        // the journal, no "process crashed" notification on the desktop; RLIMIT_CORE alone stops only the file).
        atexit([] { prctl(PR_SET_DUMPABLE, 0); raise(SIGSEGV); });
        const bool probes = !strncmp(test, "probes-", 7);
        if (!probes) setenv("BEA_PORT_TAPE", argv[2], 1);          // for tape-missing the path names nothing
        if (!probes && strcmp(test, "tape-missing") && strcmp(test, "tape-missing-stdout")) { FILE *tape = fopen(argv[2], "w"); fputs("# onslaught tape 2\n", tape); fclose(tape); }
        if (!strcmp(test, "tape-and-record")) { std::string rec = std::string(argv[2]) + ".recording"; setenv("BEA_PORT_RECORD", rec.c_str(), 1); }
        if (probes) { std::string where = std::string(argv[2]) + ".no-such-folder/probes.txt"; setenv("BEA_PORT_PROBES", where.c_str(), 1); }
        if (!strcmp(test, "probes-unwritable-recording")) setenv("BEA_PORT_RECORD", argv[2], 1);    // a recording is open when the probe file fails
        if (!strcmp(test, "tape-missing-stdout")) fputs("STDOUT-PENDING-AT-THE-STOP", stdout);      // a pipe: fully buffered, lost unless the stop flushes
        Frame(1);
        if (!strcmp(test, "tape-ok")) _exit(0);                    // the tape started: leave without the crashing handler
        return 0;                                                  // not reached: the start failed and stopped the process
    }
    if (!strcmp(test, "existing-destination")) { FILE *old = fopen(argv[2], "wb"); fputs("OLD RECORDING\n", old); fclose(old); }
    if (!strcmp(test, "stage-collision")) {
        char path[4096]; snprintf(path, sizeof path, "%s.recording-%lu-0.incomplete", argv[2], (unsigned long)getpid());
        FILE *old = fopen(path, "wb"); fputs("OLD INCOMPLETE\n", old); fclose(old);
    }
    failRegistration = !strcmp(test, "registration-error");
    failOpen = !strcmp(test, "open-error");
    failWrite = !strcmp(test, "header-write-error");
    if (!strcmp(test, "probe-open-error")) {
        char path[4096]; snprintf(path, sizeof path, "%s.missing-parent/probe.out", argv[2]); setenv("BEA_PORT_PROBES", path, 1);
    }
    if (!strcmp(test, "probe-finalizer-error")) {
        failProbeRegistration = true;
        char path[4096]; snprintf(path, sizeof path, "%s.probe.out", argv[2]); setenv("BEA_PORT_PROBES", path, 1);
    }
    bool load = !strcmp(test, "load") || !strcmp(test, "load-resized");
    if (!strcmp(test, "load-resized")) { backWidth = 800; backHeight = 600; }
    if (PortRecording() == load) return 2;
    recordTarget = gRecord;
    unsigned heldBefore = 0;
    if (load) {
        for (unsigned frame = 1; frame <= 4; ++frame) {
            Frame(frame);
            for (unsigned read = 0; read < 2; ++read) {
                long x, y, z; int level; bool active = PortReplayMouse(&x, &y, &z, &level);
                mouseSamples.push_back({frame, read, active, x, y, z, level});
            }
        }
    }
    else if (!strcmp(test, "normal") || !strcmp(test, "atexit") || strstr(test, "error") ||
             !strcmp(test, "existing-destination") || !strcmp(test, "publish-collision") || !strcmp(test, "stage-collision") || !strcmp(test, "environment-change")) {
        failWrite = failWrite || !strcmp(test, "write-error");
        failStream = !strcmp(test, "stream-error");
        failFlush = !strcmp(test, "flush-error"); failClose = !strcmp(test, "close-error");
        failPublish = !strcmp(test, "publish-error");
        Normal();
    } else if (!strcmp(test, "interrupted")) {
        PortRecordKey(true, 'W', 0x11); Frame(1); Summary(); fflush(stdout); _Exit(0);
    } else if (!strcmp(test, "deferred-mouse-coalesce") || !strcmp(test, "deferred-mouse-mode") || !strcmp(test, "deferred-mouse-resize")) {
        Fill(255);
        PortRecordWindowMouse(WM_MOUSEMOVE, MAKELPARAM(10, 20), 0);
        if (strcmp(test, "deferred-mouse-coalesce")) PortRecordWindowMouse(WM_LBUTTONDOWN, MAKELPARAM(10, 20), 0);
        Frame(1);
        if (gCurX != 0 || gCurY != 0) return 4;
        if (!strcmp(test, "deferred-mouse-coalesce")) {
            PortRecordWindowMouse(WM_MOUSEMOVE, MAKELPARAM(30, 40), 0);
            PortRecordWindowMouse(WM_LBUTTONDOWN, MAKELPARAM(30, 40), 0);
            PortRecordWindowMouse(WM_LBUTTONUP, MAKELPARAM(30, 40), 0);
        } else if (!strcmp(test, "deferred-mouse-resize")) { backWidth = 800; backHeight = 600; }
        else acquired = true;
        Frame(2); Exhaust();
    } else if (!strcmp(test, "pilot-normal")) {
        gPilot = "key:41,1e;wait:2"; gPilotStart = 1;
        for (unsigned frame = 1; frame <= 4; ++frame) Frame(frame);
    } else {
        if (!strcmp(test, "focus-overflow")) {
            for (int i = 0; i < 26; ++i) Key(static_cast<SDL_Scancode>(SDL_SCANCODE_A + i), true);
        } else Key(SDL_SCANCODE_W, true);
        Pump(); Frame(1);
        size_t count = !strcmp(test, "mouse-button-overflow") || !strcmp(test, "window-ring-overflow") ? 1023 : 1024;
        Fill(count);
        if (!strcmp(test, "key-up-overflow") || !strcmp(test, "window-ring-overflow")) Key(SDL_SCANCODE_W, false);
        else if (!strcmp(test, "char-overflow")) { Key(SDL_SCANCODE_Y, true); Key(SDL_SCANCODE_Y, false); Key(SDL_SCANCODE_W, false); }
        else if (!strcmp(test, "mouse-button-overflow")) { Mouse(SDL_EVENT_MOUSE_BUTTON_DOWN); Mouse(SDL_EVENT_MOUSE_BUTTON_UP); Key(SDL_SCANCODE_W, false); }
        else if (!strcmp(test, "mouse-move-overflow")) { Mouse(SDL_EVENT_MOUSE_MOTION); Key(SDL_SCANCODE_W, false); }
        else if (!strcmp(test, "pilot-overflow")) { gPilot = "key:41,1e"; gPilotStart = 2; }
        else if (!strcmp(test, "focus-overflow")) {
            memset(sdkKeys, 0, sizeof sdkKeys); SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; input.push_back(event);
        } else return 2;
        Pump(); heldBefore = gHeld; Frame(2);
        if (!strcmp(test, "pilot-overflow")) Key(SDL_SCANCODE_W, false);
        Exhaust();
    }
    if (!strcmp(test, "final-flush-error")) failFlush = true;
    if (!strcmp(test, "environment-change")) setenv("BEA_PORT_RECORD", "not-the-selected-recording", 1);
    if (!strcmp(test, "publish-collision")) { FILE *old = fopen(argv[2], "wb"); fputs("FOREIGN RECORDING\n", old); fclose(old); }
    if (strcmp(test, "atexit")) {
        PortReplayAtExit(); PortReplayAtExit();
        // The original source has no recorder finalizer; model the CRT's final close so its return is observable.
        if (gRecord) { TestClose(gRecord); gRecord = nullptr; }
    }
    Summary(heldBefore);
    return 0;
}
