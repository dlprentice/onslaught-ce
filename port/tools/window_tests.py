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
#include "WINDOW_CPP"
#include "PORTLOG_CPP"
#include "INVENTORY_CPP"
extern "C" { char LT[0x331e4]; unsigned PortPresentCount(void) { return 0; }
int PortMapMouse(float *, float *) { return 0; } }
bool recording;
struct RecordedKey { bool down; unsigned vk, scan; } recorded[16];
unsigned recordedCount, directDown, directUp, sizes;
bool active = true;
void PortMouseKeepRelative() {}
bool PortDinputOwnsMouseEvent(Uint64) { return false; }
bool PortRecording() { return recording; }
bool PortRecordInputSpace(unsigned) { return true; }
void PortRecordKey(bool down, unsigned vk, unsigned scan) {
    if (recordedCount >= 16) exit(10);
    recorded[recordedCount++] = {down, vk, scan};
}
void PortRecordWheel(float) {} void PortRecordMotion(float, float) {}
void PortRecordButton(int, bool) {} void PortRecordWindowMouse(UINT, LPARAM, int) {}
void PortRecordChar(unsigned, unsigned) {} bool PortTapeCursor(POINT *) { return false; }
int PortWideModes() { return 0; } void PortVideoInit() {} bool PortDevLog() { return false; }
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s at %d\n", #expr, __LINE__); return 1; } } while (0)
static LRESULT WINAPI Proc(HWND, UINT msg, WPARAM wp, LPARAM) {
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
    if (!strcmp(argv[1], "restore")) {
        Event(SDL_EVENT_WINDOW_MINIMIZED); CHECK(!active);
        Event(SDL_EVENT_WINDOW_FOCUS_GAINED); CHECK(!active);
        unsigned before = sizes;
        Event(SDL_EVENT_WINDOW_RESTORED); CHECK(active && sizes == before + 1);
        Event(SDL_EVENT_WINDOW_MINIMIZED); CHECK(!active);
        before = sizes;
        Event(SDL_EVENT_WINDOW_MAXIMIZED); CHECK(active && sizes == before + 1);
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
        build = Path(cls.temp.name)
        source = build / "driver.cpp"
        source.write_text(DRIVER.replace("WINDOW_CPP", str(deps.PORT / "platform/common/window.cpp"))
                          .replace("PORTLOG_CPP", str(deps.PORT / "platform/common/portlog.cpp"))
                          .replace("INVENTORY_CPP", str(deps.PORT / "platform/common/inventory.cpp")))
        cls.driver = build / "driver"
        subprocess.run([
            str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar",
            "-fms-extensions", "-fno-autolink", "-w", "-O0",
            f"-I{deps.PORT / 'include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
            f"-I{deps.PREFIX / 'include'}", str(source), f"-L{deps.PREFIX / 'lib'}", "-lSDL3",
            f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(cls.driver),
        ], check=True, capture_output=True, text=True)

    def check_case(self, case):
        env = {key: value for key, value in os.environ.items() if not key.startswith("BEA_PORT_")}
        env.update(SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")
        result = subprocess.run([str(self.driver), case], env=env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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


if __name__ == "__main__":
    unittest.main()
