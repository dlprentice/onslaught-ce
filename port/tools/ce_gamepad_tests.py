#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""port/game/ce_gamepad.cpp's IsStick, compiled from the source against a fake LT: a slot's device type is the device
in it now, so a flight stick plugged into a slot that was empty or held a gamepad gets the flight-stick defaults (the
0.1.9 review: a type cached from the empty slot gave a late flight stick gamepad controls). A mutant that caches the
first answer per slot must fail.

    python3 port/tools/ce_gamepad_tests.py
"""
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_gamepad.cpp"

DRIVER = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
typedef unsigned long DWORD; typedef long HRESULT;
#define SUCCEEDED(h) ((h) >= 0)
#define MAX_JOYPADS 4
#define DI8DEVTYPE_JOYSTICK 0x14
#define DI8DEVTYPE_GAMEPAD 0x15
struct DIDEVCAPS { DWORD dwSize, dwFlags, dwDevType; };
struct FakeDevice {
    DWORD type;
    HRESULT GetCapabilities(DIDEVCAPS *c) { c->dwDevType = type; return 0; }
};
struct { FakeDevice *pJoystick[MAX_JOYPADS]; } LT;
#include "is_stick.inc"
static void check(bool c, const char *m) { if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(2); } }
int main() {
    check(!IsStick(0), "an empty slot is not a flight stick");
    FakeDevice stick{DI8DEVTYPE_JOYSTICK | 0x100};
    LT.pJoystick[0] = &stick;
    check(IsStick(0), "a flight stick plugged into the empty slot is a flight stick");
    FakeDevice pad{DI8DEVTYPE_GAMEPAD};
    LT.pJoystick[1] = &pad;
    check(!IsStick(1), "a gamepad is not");
    LT.pJoystick[1] = &stick;
    check(IsStick(1), "a flight stick replacing the gamepad in that slot is");
    std::puts("flight stick type follows the device");
}
'''


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += {"{": 1, "}": -1}.get(source[end], 0)
        end += 1
    return source[start:end]


class IsStickTests(unittest.TestCase):
    def run_driver(self, body):
        with tempfile.TemporaryDirectory(prefix="ce-gamepad-tests-") as d:
            out = Path(d)
            (out / "is_stick.inc").write_text(body)
            (out / "driver.cpp").write_text(DRIVER)
            built = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-I", str(out),
                                    str(out / "driver.cpp"), "-o", str(out / "driver")], capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stderr)
            return subprocess.run([str(out / "driver")], capture_output=True, text=True, timeout=10)

    def body(self):
        return function(SOURCE.read_text(), "bool IsStick(int pad)")

    def test_type_follows_the_device(self):
        ran = self.run_driver(self.body())
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("flight stick type follows the device", ran.stdout)

    def test_mutant_caching_the_first_answer_fails(self):
        cached = ("bool IsStick(int pad)\n{\n\tstatic signed char known[MAX_JOYPADS] = {-1, -1, -1, -1};\n\tif (known[pad] < 0)\n"
                  "\t\tknown[pad] = IsStickNow(pad);\n\treturn known[pad] > 0;\n}\n")
        mutant = self.body().replace("bool IsStick(int pad)", "static bool IsStickNow(int pad)", 1) + "\n" + cached
        ran = self.run_driver(mutant)
        self.assertNotEqual(ran.returncode, 0, "mutant survived: " + ran.stdout)
        self.assertIn("a flight stick plugged into the empty slot is a flight stick", ran.stderr)


class StickButtons(unittest.TestCase):
    """No flight-stick play control shares a button with the menu table's Pause, which applies to every pad (the 0.1.9
    docs review: Zoom out on button 6 also paused)."""

    @staticmethod
    def table(source, name):
        body = source[source.index(f"const Default {name}[] = {{"):]
        return body[:body.index("};")]

    def stick_keys(self, source):
        enum = source[source.index("enum { STICK_THUMB"):]
        values = dict(re.findall(r"(STICK_\w+) = (\d+)", enum[:enum.index("};")]))
        return {int(values[k]) for k in re.findall(r",\s*(STICK_\w+)\}", self.table(source, "kStick"))}

    def pause_key(self, source):
        pads = dict(re.findall(r"(PAD_\w+) = (\d+)", source[source.index("enum { PAD_B"):]))
        return int(pads[re.search(r"\{BUTTON_PAUSE, BUTTON_ONCE, (PAD_\w+)\}", self.table(source, "kMenu")).group(1)])

    def test_pause_button_is_free_on_a_flight_stick(self):
        source = SOURCE.read_text()
        self.assertNotIn(self.pause_key(source), self.stick_keys(source))

    def test_mutant_zoom_out_on_the_pause_button_fails(self):
        source = SOURCE.read_text().replace("BUTTON_MECH_CHANGE_ZOOM_OUT, BUTTON_ONCE, STICK_7}", "BUTTON_MECH_CHANGE_ZOOM_OUT, BUTTON_ONCE, STICK_6}")
        self.assertIn(self.pause_key(source), self.stick_keys(source))


if __name__ == "__main__":
    unittest.main()
