#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""port/platform/common/inventory.cpp against an authored SDL stub (no game, display or device): every inventory line,
the account name written <user> in any case, names cut to printable ASCII and 48 characters, the list bound of 8, the
settings as numbers only, and nothing at all when the session log is off (faithful, replay, recording runs).

    python3 port/tools/inventory_tests.py
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/common/inventory.cpp"

SDL = r"""
#pragma once
#include <stdint.h>
#include <stdlib.h>
typedef uint32_t SDL_DisplayID, SDL_KeyboardID, SDL_MouseID, SDL_JoystickID, SDL_AudioDeviceID;
struct SDL_Window {};
struct SDL_Rect { int x, y, w, h; };
struct SDL_DisplayMode { int w, h; float refresh_rate, pixel_density; };
struct SDL_AudioSpec { int format, channels, freq; };
SDL_DisplayID *SDL_GetDisplays(int *count);
SDL_DisplayID SDL_GetPrimaryDisplay();
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *);
const char *SDL_GetDisplayName(SDL_DisplayID);
bool SDL_GetDisplayBounds(SDL_DisplayID, SDL_Rect *);
bool SDL_GetDisplayUsableBounds(SDL_DisplayID, SDL_Rect *);
const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID);
SDL_DisplayMode **SDL_GetFullscreenDisplayModes(SDL_DisplayID, int *count);
float SDL_GetDisplayContentScale(SDL_DisplayID);
SDL_KeyboardID *SDL_GetKeyboards(int *count);
SDL_MouseID *SDL_GetMice(int *count);
SDL_JoystickID *SDL_GetJoysticks(int *count);
const char *SDL_GetKeyboardNameForID(SDL_KeyboardID);
const char *SDL_GetMouseNameForID(SDL_MouseID);
const char *SDL_GetJoystickNameForID(SDL_JoystickID);
bool SDL_IsGamepad(SDL_JoystickID);
uint16_t SDL_GetJoystickVendorForID(SDL_JoystickID);
uint16_t SDL_GetJoystickProductForID(SDL_JoystickID);
SDL_AudioDeviceID *SDL_GetAudioPlaybackDevices(int *count);
const char *SDL_GetAudioDeviceName(SDL_AudioDeviceID);
bool SDL_GetAudioDeviceFormat(SDL_AudioDeviceID, SDL_AudioSpec *, int *);
const char *SDL_GetCurrentAudioDriver();
void SDL_free(void *);
"""

DRIVER = r"""
#include "portlog.h"
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
bool enabled = true;
std::vector<std::string> lines;
bool PortSessionEnabled() { return enabled; }
void PortSessionLog(PortSessionCategory c, bool error, const char *f, ...)
{
	char b[1024]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
	lines.push_back(std::to_string((int)c) + (error ? "E " : " ") + b);
}
extern "C" int CE_SettingGet(const char *key, int def) { return !strcmp(key, "windowed") ? 1 : !strcmp(key, "wide_mode_w") ? 2560 : def; }
template <class T> T *ids(int n, int *count) { T *p = (T *)malloc(sizeof(T) * (n + 1)); for (int i = 0; i < n; ++i) p[i] = i + 1; p[n] = 0; *count = n; return p; }
SDL_DisplayID *SDL_GetDisplays(int *count) { return ids<SDL_DisplayID>(10, count); }
SDL_DisplayID SDL_GetPrimaryDisplay() { return 2; }
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *) { return 1; }
const char *SDL_GetDisplayName(SDL_DisplayID id) { return id == 1 ? "Dell \xc3\xa9 Monitor JOHNDOE's screen with a very long product name that runs on and on" : "Generic"; }
bool SDL_GetDisplayBounds(SDL_DisplayID id, SDL_Rect *r) { *r = {(int)(id - 1) * 2560, 0, 2560, 1440}; return true; }
bool SDL_GetDisplayUsableBounds(SDL_DisplayID, SDL_Rect *) { return false; }
SDL_DisplayMode mode{2560, 1440, 143.99f, 1.f};
const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID) { return &mode; }
SDL_DisplayMode **SDL_GetFullscreenDisplayModes(SDL_DisplayID, int *count) { *count = 19; return (SDL_DisplayMode **)malloc(8); }
float SDL_GetDisplayContentScale(SDL_DisplayID) { return 1.25f; }
SDL_KeyboardID *SDL_GetKeyboards(int *count) { return ids<SDL_KeyboardID>(2, count); }
SDL_MouseID *SDL_GetMice(int *count) { return ids<SDL_MouseID>(1, count); }
SDL_JoystickID *SDL_GetJoysticks(int *count) { return ids<SDL_JoystickID>(1, count); }
const char *SDL_GetKeyboardNameForID(SDL_KeyboardID id) { return id == 1 ? "AT keyboard" : nullptr; }
const char *SDL_GetMouseNameForID(SDL_MouseID) { return "Gaming\tMouse"; }
const char *SDL_GetJoystickNameForID(SDL_JoystickID) { return "Pad"; }
bool SDL_IsGamepad(SDL_JoystickID) { return true; }
uint16_t SDL_GetJoystickVendorForID(SDL_JoystickID) { return 0x045e; }
uint16_t SDL_GetJoystickProductForID(SDL_JoystickID) { return 0x0b13; }
SDL_AudioDeviceID *SDL_GetAudioPlaybackDevices(int *count) { return ids<SDL_AudioDeviceID>(2, count); }
const char *SDL_GetAudioDeviceName(SDL_AudioDeviceID id) { return id == 1 ? "Speakers (Realtek)" : "johndoe headphones"; }
bool SDL_GetAudioDeviceFormat(SDL_AudioDeviceID id, SDL_AudioSpec *s, int *) { *s = {0, 2, 48000}; return id == 1; }
const char *SDL_GetCurrentAudioDriver() { return "wasapi"; }
void SDL_free(void *p) { free(p); }
int main(int argc, char **)
{
	enabled = argc < 2;
	SDL_Window w;
	PortSessionDisplayInventory(&w);
	PortSessionInputInventory();
	PortSessionAudioInventory();
	PortSessionSettingsInventory();
	for (auto &l : lines) printf("%s\n", l.c_str());
	return 0;
}
"""


class Inventory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = Path(tempfile.mkdtemp(prefix="inventory-tests-"))
        (cls.dir / "SDL3").mkdir()
        (cls.dir / "SDL3/SDL.h").write_text(SDL)
        (cls.dir / "driver.cpp").write_text(DRIVER)
        cls.exe = cls.dir / "driver"
        cmd = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(cls.dir),
               "-I", str(ROOT / "port/platform/common"), str(cls.dir / "driver.cpp"), str(SOURCE), "-o", str(cls.exe)]
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise RuntimeError(built.stdout + built.stderr)

    def run_driver(self, *args, user="JohnDoe"):
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "USER": user, "BEA_USER_DIR": "/x",
               "BEA_PORT_HUD_SCALE": "0"}
        result = subprocess.run([str(self.exe), *args], capture_output=True, text=True, env=env, timeout=10)
        self.assertEqual(0, result.returncode, result.stderr)
        return result.stdout.splitlines()

    def test_every_inventory_line(self):
        lines = self.run_driver()
        text = "\n".join(lines)
        self.assertEqual(8, sum(" display " in f" {l}" and "/10 id=" in l for l in lines), text)
        self.assertIn("display list truncated after 8 of 10", text)
        self.assertIn('display 1/10 id=1 name="Dell ?? Monitor <user>\'s screen with a very long" primary=0 '
                      'game_window=1 bounds=0,0,2560,1440 usable=unavailable:0,0,0,0 mode=2560x1440@143.99Hz '
                      'pixel_density=1.00 content_scale=1.25 fullscreen_modes=19', text)
        self.assertIn("display 2/10 id=2 name=\"Generic\" primary=1 game_window=0", text)
        self.assertIn("input keyboards=2 mice=1 controllers=1", text)
        self.assertIn('input keyboard 1 name="AT keyboard"', text)
        self.assertIn('input keyboard 2 name="-"', text)
        self.assertIn('input mouse 1 name="Gaming?Mouse"', text)
        self.assertIn('input controller 1 name="Pad" gamepad=1 vendor=045e product=0b13', text)
        self.assertIn("playback devices offered=2 driver=wasapi", text)
        self.assertIn('playback device 1 name="Speakers (Realtek)" format=48000 Hz 2 channels', text)
        self.assertIn('playback device 2 name="<user> headphones" format=unavailable:48000 Hz 2 channels', text)
        self.assertIn("settings windowed=1 wide_mode=2560x0 record_sessions=0 god_mode=0 widescreen_env=-1 "
                      "hud_scale_env=0 custom_user_folder=1", text)
        self.assertNotIn("JohnDoe", text)
        self.assertNotIn("johndoe", text.lower().replace("<user>", ""))

    def test_off_session_writes_nothing(self):
        self.assertEqual([], self.run_driver("off"))

    def test_short_account_names_are_not_scrubbed(self):
        lines = self.run_driver(user="D")
        self.assertIn('playback device 2 name="johndoe headphones"', "\n".join(lines))


if __name__ == "__main__":
    sys.exit(unittest.main())
