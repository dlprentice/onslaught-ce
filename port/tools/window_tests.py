#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production SDL message pump without a display or game data."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import deps

DRIVER = r'''
#include <SDL3/SDL.h>
static SDL_Window *seedFocus;
static float seedX, seedY;
static SDL_Window *SeedMouseFocus() { return seedFocus; }
static SDL_MouseButtonFlags SeedMouseState(float *x, float *y) {
    if (x) *x = seedX;
    if (y) *y = seedY;
    return 0;
}
#define SDL_GetMouseFocus SeedMouseFocus
#define SDL_GetMouseState SeedMouseState
#include "WINDOW_CPP"
#undef SDL_GetMouseFocus
#undef SDL_GetMouseState
#include "PORTLOG_CPP"
#include "INVENTORY_CPP"
extern "C" { char LT[0x331e4]; unsigned PortPresentCount(void) { return 0; }
int PortMapMouse(float *, float *) { return 0; } }
bool recording;
struct RecordedKey { bool down; unsigned vk, scan; } recorded[16];
unsigned recordedCount, directDown, directUp, sizes;
bool active = true;
void PortMouseKeepRelative() {}
unsigned pauseBegins, pauseEnds;
void PortClockPauseBegin() { pauseBegins++; }
void PortClockPauseEnd() { pauseEnds++; }
static bool gameMouseAcquired;
bool PortDinputOwnsMouseEvent(Uint64 timestamp) { return !timestamp && gameMouseAcquired; }	// no timestamp: is the game's mouse acquired now (a real event's timestamp is never 0)
bool PortRecording() { return recording; }
bool PortRecordInputSpace(unsigned) { return true; }
void PortRecordKey(bool down, unsigned vk, unsigned scan) {
    if (recordedCount >= 16) exit(10);
    recorded[recordedCount++] = {down, vk, scan};
}
void PortRecordWheel(float) {} void PortRecordMotion(float, float) {}
void PortRecordButton(int, bool) {} void PortRecordWindowMouse(UINT, LPARAM, int) {}
static bool seedTape, seedMenu;
static int seedMapCalls, seedMenuArg = -1;
void PortRecordChar(unsigned, unsigned) {}
bool PortTapeCursor(POINT *p) { if (!seedTape) return false; if (p) { p->x = 111; p->y = 222; } return true; }
int PortWideModes() { return 0; } void PortVideoInit() {} bool PortDevLog() { return false; }
int PortUiMouseMenu(int active) { seedMenuArg = active; return seedMenu && !active; }
void PortUiMouseMenuPoint(int *x, int *y) { ++seedMapCalls; *x /= 2; *y = *y / 2 + 1; }
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s at %d\n", #expr, __LINE__); return 1; } } while (0)
static UINT mouseSeq[16];
static unsigned mouseSeqN;
static LRESULT WINAPI Proc(HWND, UINT msg, WPARAM wp, LPARAM) {
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSEWHEEL && mouseSeqN < 16) mouseSeq[mouseSeqN++] = msg;
    if (msg == WM_SIZE) { active = wp != SIZE_MINIMIZED; sizes++; }
    if (msg == WM_KEYDOWN) directDown++;
    if (msg == WM_KEYUP) directUp++;
    return 0;
}
static void Drain() {
    MSG msg;
    for (int i = 0; i < 200 && PortPeekMessageA(&msg, NULL, 0, 0, PM_REMOVE); i++)
        PortDispatchMessageA(&msg);
}
static void Event(Uint32 type) {
    SDL_Event e{}; e.type = type; e.window.windowID = SDL_GetWindowID(gWindows[0].sdl);
    if (!SDL_PushEvent(&e)) exit(5);
    Drain();
}
static void PushMotion(float x, float y) {
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.windowID = SDL_GetWindowID(gWindows[0].sdl); e.motion.x = x; e.motion.y = y;
    if (!SDL_PushEvent(&e)) exit(5);
}
static void PushButton(bool down, float x, float y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP; e.button.windowID = SDL_GetWindowID(gWindows[0].sdl);
    e.button.button = button; e.button.down = down; e.button.x = x; e.button.y = y;
    if (!SDL_PushEvent(&e)) exit(5);
}
static void PushKey(bool down) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = SDL_SCANCODE_W; e.key.key = SDLK_W; e.key.down = down;
    if (!SDL_PushEvent(&e)) exit(5);
}
static void Key(bool down) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = SDL_SCANCODE_W; e.key.key = SDLK_W; e.key.down = down;
    if (!SDL_PushEvent(&e)) exit(5);
    Drain();
}
int main(int argc, char **argv) {
    if (argc != 2 || !SDL_Init(SDL_INIT_VIDEO)) return 2;
    SDL_Window *w = SDL_CreateWindow("message-pump-test", 640, 480, 0);
    if (!w) return 3;
    gNumWindows = 1; gWindows[0].sdl = w; gWindows[0].handle = (HWND)w; gWindows[0].proc = Proc;
    Drain();
    if (!strcmp(argv[1], "mouse-seed")) {
        // Exercise the actual window function that ShutdownMouse uses. Fake focus and coordinates isolate this test
        // from the host pointer; the map helper stands in for the separately tested widescreen transform.
        POINT point = {-1, -1};
        seedX = 800; seedY = 400;
        seedFocus = NULL;
        CHECK(!PortGetMouseClientPosition(&point)); CHECK(point.x == -1 && point.y == -1 && seedMapCalls == 0);
        seedFocus = w; seedMenu = false;
        CHECK(PortGetMouseClientPosition(&point)); CHECK(point.x == 800 && point.y == 400 && seedMapCalls == 0);
        seedMenu = true;
        CHECK(PortGetMouseClientPosition(&point)); CHECK(point.x == 400 && point.y == 201 && seedMapCalls == 1 && seedMenuArg == 0);
        seedTape = true;
        CHECK(PortGetMouseClientPosition(&point)); CHECK(point.x == 111 && point.y == 222 && seedMapCalls == 1);
    } else if (!strncmp(argv[1], "release-", 8) || !strcmp(argv[1], "press-batch")) {
        // The game reads every waiting message at the start of a frame and judges a click afterwards, at the pointer's position then. In the CE layer's menus (PortUiMouseMenu) the batch ends at a button
        // release: a movement or a press that follows it waits for the next batch, so the click is judged where the button was released. Elsewhere (a replay, a recording, a private build, the commander
        // map) nothing waits. A press does not end the batch (a drag is a press and then movement), and a key is held back only by the first-in first-out order behind a waiting mouse message.
        const bool menu = strcmp(argv[1], "release-batch-off") != 0;
        gameMouseAcquired = !strcmp(argv[1], "release-batch-acquired");	// the game holds its mouse (gameplay): no menu's click
        seedMenu = menu;
        if (!strcmp(argv[1], "release-look")) {
            // step by step, as a game that peeks before it reads: a look at what waits does not end the batch (only reading past it does), and does not take the message
            PushMotion(100, 100); PushButton(true, 100, 100); PushButton(false, 100, 100); PushMotion(300, 300);
            MSG m;
            for (int i = 0; i < 3; ++i) { CHECK(PortPeekMessageA(&m, NULL, 0, 0, PM_REMOVE)); PortDispatchMessageA(&m); }
            CHECK(mouseSeqN == 3 && mouseSeq[2] == WM_LBUTTONUP);
            CHECK(!PortPeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE));		// the movement waits: nothing to see in this batch
            CHECK(!PortPeekMessageA(&m, NULL, 0, 0, PM_REMOVE));		// reading past it ends the batch (the look above did not)
            CHECK(PortPeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE) && m.message == WM_MOUSEMOVE);	// and it is there for the next one
            CHECK(PortPeekMessageA(&m, NULL, 0, 0, PM_REMOVE)); PortDispatchMessageA(&m);
            CHECK(mouseSeqN == 4 && mouseSeq[3] == WM_MOUSEMOVE);
        } else if (!strcmp(argv[1], "release-buttons")) {
            // a menu takes a click of any button: the right's and the middle's release end the batch as the left's does
            const Uint8 buttons[3] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};
            for (int i = 0; i < 3; ++i) {
                const unsigned before = mouseSeqN;
                PushMotion(100, 100); PushButton(true, 100, 100, buttons[i]); PushButton(false, 100, 100, buttons[i]); PushMotion(300, 300); Drain();
                CHECK(mouseSeqN == before + 3);							// the movement, the press and the release: the later movement waits
                Drain();
                CHECK(mouseSeqN == before + 4);							// and comes with the next batch
            }
        } else if (!strcmp(argv[1], "release-key-motion")) {
            // a key handed out between the release and a movement does not end the wait: the key comes in this batch, the movement in the next
            PushMotion(100, 100); PushButton(true, 100, 100); PushButton(false, 100, 100); PushKey(true); PushMotion(300, 300); Drain();
            CHECK(mouseSeqN == 3 && mouseSeq[2] == WM_LBUTTONUP && directDown == 1);
            Drain();
            CHECK(mouseSeqN == 4 && mouseSeq[3] == WM_MOUSEMOVE && directDown == 1);
        } else if (!strcmp(argv[1], "release-key")) {
            PushMotion(100, 100); PushButton(true, 100, 100); PushButton(false, 100, 100); PushKey(true); Drain();
            CHECK(mouseSeqN == 3 && directDown == 1);					// nothing mouse-related waits behind the release: the key comes in the same batch
        } else if (!strcmp(argv[1], "press-batch")) {
            PushMotion(100, 100); PushButton(true, 100, 100); PushMotion(300, 300); Drain();
            CHECK(mouseSeqN == 3 && mouseSeq[2] == WM_MOUSEMOVE);		// a drag: the movement after a press is delivered at once
        } else if (!strcmp(argv[1], "release-twice")) {
            PushMotion(100, 100); PushButton(true, 100, 100); PushButton(false, 100, 100); PushButton(true, 300, 300); PushButton(false, 300, 300); Drain();
            CHECK(mouseSeqN == 3 && mouseSeq[2] == WM_LBUTTONUP);		// the first click only: the second press waits
            Drain();
            CHECK(mouseSeqN == 5 && mouseSeq[3] == WM_LBUTTONDOWN && mouseSeq[4] == WM_LBUTTONUP);	// the next batch is the second click, press and release, and ends at its release
            Drain();
            CHECK(mouseSeqN == 5);										// and then nothing is left
        } else {
            PushMotion(100, 100); PushButton(true, 100, 100); PushButton(false, 100, 100); PushMotion(300, 300); PushKey(true); Drain();
            if (menu && !gameMouseAcquired) {
                CHECK(mouseSeqN == 3 && mouseSeq[0] == WM_MOUSEMOVE && mouseSeq[1] == WM_LBUTTONDOWN && mouseSeq[2] == WM_LBUTTONUP);
                CHECK(directDown == 0);									// the key stands in line behind the waiting movement
                MSG peeked;
                CHECK(PortPeekMessageA(&peeked, NULL, 0, 0, PM_NOREMOVE) && peeked.message == WM_MOUSEMOVE);	// the batch is over: the waiting movement is the next one's first, and a look leaves it
                Drain();
                CHECK(mouseSeqN == 4 && mouseSeq[3] == WM_MOUSEMOVE && directDown == 1);
                Drain();
                CHECK(mouseSeqN == 4 && directDown == 1);				// nothing is left, nothing was lost
            } else {
                CHECK(mouseSeqN == 4 && mouseSeq[3] == WM_MOUSEMOVE && directDown == 1);
            }
        }
    } else if (!strcmp(argv[1], "restore")) {
        Event(SDL_EVENT_WINDOW_MINIMIZED); CHECK(!active);
        Event(SDL_EVENT_WINDOW_FOCUS_GAINED); CHECK(!active);
        unsigned before = sizes;
        Event(SDL_EVENT_WINDOW_RESTORED); CHECK(active && sizes == before + 1);
        Event(SDL_EVENT_WINDOW_MINIMIZED); CHECK(!active);
        before = sizes;
        Event(SDL_EVENT_WINDOW_MAXIMIZED); CHECK(active && sizes == before + 1);
    } else if (!strcmp(argv[1], "idle-wait")) {
        // BEA_PORT_FAKE_KEYS=h0:0.3 minimizes the window at the first Pump and restores it after 0.3 s of real time, while the game waits in GetMessage (its idle wait, the original's d3dapp
        // made it inactive): the clock must stand still for exactly that wait, begun and ended around each GetMessage.
        Drain(); CHECK(!active);
        MSG msg;
        Uint64 began = SDL_GetTicksNS();
        unsigned guard = 0;
        for (;;) {
            CHECK(PortGetMessageA(&msg, NULL, 0, 0));
            if (msg.message == WM_SIZE && msg.wParam == SIZE_RESTORED) break;
            CHECK(++guard < 100);
        }
        const double waited = (double)(SDL_GetTicksNS() - began) / 1e9;
        CHECK(waited >= 0.25 && waited < 3.0);
        CHECK(pauseBegins >= 1 && pauseBegins == pauseEnds);
        PortDispatchMessageA(&msg); CHECK(active);
    } else if (!strcmp(argv[1], "replay")) {
        // replay.cpp posts its own messages. Physical-state reconciliation must not release the tape's keys.
        PortPostMessageA((HWND)w, WM_KEYDOWN, 'W', KeyLParam(0x11, false, false)); Drain();
        CHECK(directDown == 1 && gHeld == 0);
        Event(SDL_EVENT_WINDOW_FOCUS_LOST); CHECK(directUp == 0);
    } else {
        recording = strcmp(argv[1], "live-focus") != 0;
        gFakeHeld[SDL_SCANCODE_W] = true;
        Key(true); CHECK(gHeld == 1);
        if (!strcmp(argv[1], "record-lost-up")) {
            gFakeHeld[SDL_SCANCODE_W] = false; Drain();
        } else {
            Event(SDL_EVENT_WINDOW_FOCUS_LOST);
        }
        CHECK(gHeld == 0);
        if (recording) {
            CHECK(recordedCount == 2 && recorded[0].down && !recorded[1].down);
            CHECK(recorded[1].vk == 'W' && recorded[1].scan == 0x11);
            CHECK(directDown == 0 && directUp == 0);
        } else {
            CHECK(directDown == 1 && directUp == 1 && recordedCount == 0);
        }
        gFakeHeld[SDL_SCANCODE_W] = false; Key(false);
        CHECK(recording ? recordedCount == 2 : directUp == 1);
    }
    SDL_DestroyWindow(w); SDL_Quit();
    return 0;
}
'''


class MessagePump(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = deps.WORK / "tests"
        scratch.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="window-", dir=scratch)
        cls.addClassCleanup(cls.temp.cleanup)
        cls.build = Path(cls.temp.name)
        cls.driver = cls.compile_driver(deps.PORT / "platform/common/window.cpp", "driver")

    @classmethod
    def compile_driver(cls, window, name):
        source = cls.build / f"{name}.cpp"
        source.write_text(DRIVER.replace("WINDOW_CPP", str(window))
                          .replace("PORTLOG_CPP", str(deps.PORT / "platform/common/portlog.cpp"))
                          .replace("INVENTORY_CPP", str(deps.PORT / "platform/common/inventory.cpp")))
        binary = cls.build / name
        subprocess.run([
            str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar",
            "-fms-extensions", "-fno-autolink", "-w", "-O0",
            f"-I{deps.PORT / 'include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
            f"-I{deps.PREFIX / 'include'}", f"-I{deps.PORT / 'platform/common'}", str(source), f"-L{deps.PREFIX / 'lib'}", "-lSDL3",
            f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(binary),
        ], check=True, capture_output=True, text=True)
        return binary

    def run_case(self, case, extra=None, binary=None):
        env = {key: value for key, value in os.environ.items() if not key.startswith("BEA_PORT_")}
        env.update(SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")
        env.update(extra or {})
        return subprocess.run([str(binary or self.driver), case], env=env, capture_output=True, text=True, timeout=10)

    def check_case(self, case, extra=None):
        result = self.run_case(case, extra)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_released_mouse_seed_uses_menu_coordinates_but_preserves_tape_and_focus(self):
        self.check_case("mouse-seed")

    def test_released_mouse_seed_rejects_a_missing_mapping(self):
        text = (deps.PORT / "platform/common/window.cpp").read_text()
        old = "\t\tPortUiMouseMenuPoint(&ix, &iy);"
        self.assertEqual(text.count(old), 1)
        mutant = self.build / "window-no-seed-map.cpp"
        mutant.write_text(text.replace(old, "\t\t(void)0;"))
        result = self.run_case("mouse-seed", binary=self.compile_driver(mutant, "driver-no-seed-map"))
        self.assertNotEqual(result.returncode, 0, "the seed test must catch the removed mapping")
        self.assertIn("point.x == 400 && point.y == 201", result.stderr)

    def test_same_size_restore_and_maximize_resume_game(self):
        self.check_case("restore")

    def test_recorded_lost_release_uses_tape_once(self):
        self.check_case("record-lost-up")

    def test_recorded_focus_loss_uses_tape_once(self):
        self.check_case("record-focus")

    def test_live_focus_loss_releases_key_once(self):
        self.check_case("live-focus")

    def test_tape_posted_keys_are_not_physical_keys(self):
        self.check_case("replay")

    def test_a_button_release_ends_the_batch_in_the_menus_and_only_there(self):
        for case in ("release-batch", "release-batch-off", "release-batch-acquired", "release-buttons", "release-key", "release-key-motion", "release-look", "press-batch", "release-twice"):
            with self.subTest(case=case):
                self.check_case(case)

    def test_mutants_of_the_batch_rule_are_caught(self):
        text = (deps.PORT / "platform/common/window.cpp").read_text()
        mutants = (
            ("no-wait", "\tif (gMouseAfterReleaseWaits && IsMouseMessage(gQueue[gHead].message))", "\tif (false && IsMouseMessage(gQueue[gHead].message))", "release-batch"),
            ("always-waits", "\t\tif (IsButtonRelease(out->message) && PortUiMouseMenu(PortDinputOwnsMouseEvent(0) ? 1 : 0))\n\t\t\tgMouseAfterReleaseWaits = true;", "\t\tif (IsButtonRelease(out->message))\n\t\t\tgMouseAfterReleaseWaits = true;", "release-batch-off"),
            ("ignores-the-games-mouse", "\t\tif (IsButtonRelease(out->message) && PortUiMouseMenu(PortDinputOwnsMouseEvent(0) ? 1 : 0))\n\t\t\tgMouseAfterReleaseWaits = true;", "\t\tif (IsButtonRelease(out->message) && PortUiMouseMenu(0))\n\t\t\tgMouseAfterReleaseWaits = true;", "release-batch-acquired"),
            ("any-removal-resets", "\t\tif (IsButtonRelease(out->message) && PortUiMouseMenu(PortDinputOwnsMouseEvent(0) ? 1 : 0))\n\t\t\tgMouseAfterReleaseWaits = true;", "\t\tgMouseAfterReleaseWaits = IsButtonRelease(out->message) && PortUiMouseMenu(PortDinputOwnsMouseEvent(0) ? 1 : 0);", "release-key-motion"),
            ("never-ends", "\t\tif (remove)\n\t\t\tgMouseAfterReleaseWaits = false;\t\t// this batch is over: the waiting message opens the next one\n", "", "release-batch"),
            ("keys-wait-too", "bool IsMouseMessage(UINT m) { return m >= WM_MOUSEFIRST && m <= WM_MOUSEWHEEL; }", "bool IsMouseMessage(UINT m) { return true; }", "release-key"),
            ("a-press-ends-it-too", "bool IsButtonRelease(UINT m) { return m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP; }",
             "bool IsButtonRelease(UINT m) { return m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP || m == WM_LBUTTONDOWN; }", "press-batch"),
            ("a-look-ends-it", "\t\tif (remove)\n\t\t\tgMouseAfterReleaseWaits = false;\t\t// this batch is over", "\t\tgMouseAfterReleaseWaits = false;\t\t// this batch is over", "release-look"),
            ("no-release-counts", "bool IsButtonRelease(UINT m) { return m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP; }",
             "bool IsButtonRelease(UINT m) { return false; }", "release-twice"),
            ("only-the-left-button", "bool IsButtonRelease(UINT m) { return m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP; }",
             "bool IsButtonRelease(UINT m) { return m == WM_LBUTTONUP; }", "release-buttons"),
            ("only-the-left-and-right", "bool IsButtonRelease(UINT m) { return m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP; }",
             "bool IsButtonRelease(UINT m) { return m == WM_LBUTTONUP || m == WM_RBUTTONUP; }", "release-buttons"),
        )
        for name, old, new, case in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(text.count(old), 1, name)
                source = self.build / f"window-{name}.cpp"
                source.write_text(text.replace(old, new))
                result = self.run_case(case, binary=self.compile_driver(source, f"driver-{name}"))
                self.assertNotEqual(result.returncode, 0, f"mutant {name} survived case {case}")

    def test_the_idle_wait_of_a_minimized_window_stops_the_clock_and_ends_when_it_is_restored(self):
        self.check_case("idle-wait", {"BEA_PORT_FAKE_KEYS": "h0:0.3"})


if __name__ == "__main__":
    unittest.main()
