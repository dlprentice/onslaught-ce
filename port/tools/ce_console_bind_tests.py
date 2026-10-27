#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual patched bind and CE admission callers with authored data; no game or display."""
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "port/patches/0099-ce-console-bind-bounds.patch"
DRIVER = ROOT / "port/tools/ce_console_bind_driver.cpp"
ENV_FLAGS = ("BEA_PORT_CE_LAYER", "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD")
# The command palette's fences (port/game/ce_palette.cpp Allowed): none of them may let a forced CE layer through.
PALETTE_FENCES = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT",
                  "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN")


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class ConsoleBindTests(unittest.TestCase):
    def test_grave_and_lock_keys_reach_the_bind_caller(self):
        window = (ROOT / "port/platform/common/window.cpp").read_text()
        header = (ROOT / "src/console.h").read_text()
        self.assertRegex(header, r"#define\s+CONSOLE_KEYS\s+256\b")
        for name, key in (("GRAVE", "0xC0"), ("NUMLOCKCLEAR", "VK_NUMLOCK"), ("SCROLLLOCK", "VK_SCROLL")):
            self.assertRegex(window, rf"case SDL_SCANCODE_{name}:\s*return {key};")
        key_event = function(window, "void KeyEvent(")
        self.assertIn("VirtualKey(e.key, e.scancode)", key_event)
        self.assertIn("Post(h, WM_KEYDOWN, vk, l)", key_event)
        shell = function((ROOT / "src/ltshell.cpp").read_text(), "LRESULT PCLTShell::MsgProc(")
        self.assertIn("CONSOLE.HandleBind(wParam,KEY_EVENT_DOWN)", shell)
        self.assertIn("CONSOLE.HandleBind(wParam,KEY_EVENT_UP)", shell)

    def test_dormant_console_is_not_enabled_by_this_change(self):
        source = (ROOT / "src/console.cpp").read_text()
        render = function(source, "void CConsole::Render()")
        self.assertRegex(render, r"Render\(\)\s*\{\s*\}")
        loop = function((ROOT / "src/game.cpp").read_text(), "void\tCGame::MainLoop()")
        loop = re.sub(r"//[^\n]*|/\*.*?\*/", "", loop, flags=re.DOTALL)
        self.assertNotIn("ProcessDebug()", loop)

    def test_ce_bounds_and_faithful_recording_modes(self):
        parent = ROOT / "local-data/port/ce-console-bind-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="run-", dir=parent) as tmp:
            out = Path(tmp)
            source = out / "console.cpp"
            source.write_bytes((ROOT / "src/console.cpp").read_bytes())
            applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(PATCH)],
                                     cwd=out, capture_output=True, text=True, timeout=10, preexec_fn=no_core)
            self.assertEqual(applied.returncode, 0, applied.stdout + applied.stderr)
            self.assertNotIn("fuzz", applied.stdout.lower())
            # The CE admission helper sits inside the patch's CE_VERSION block, so the faithful driver never sees it.
            handler = "#ifdef CE_VERSION\n" + function(source.read_text(), "static bool PortCeConsoleBindKeys()") + \
                "\n#endif\n" + function(source.read_text(), "void CConsole::HandleBind(")
            layer = function((ROOT / "port/platform/common/ce.cpp").read_text(), "bool PortCeLayer()")
            (out / "ce_layer.inc").write_text(layer)
            (out / "handle_bind.inc").write_text(handler)

            def compile_driver(name, ce):
                exe = out / name
                cmd = [os.environ.get("CXX", "g++"), "-std=c++14", "-fsigned-char", "-Wall", "-Wextra", "-Werror",
                       *(["-DCE_VERSION=1"] if ce else []), "-I", str(out), str(DRIVER), "-o", str(exe)]
                built = subprocess.run(cmd, capture_output=True, text=True, timeout=20, preexec_fn=no_core)
                self.assertEqual(built.returncode, 0, built.stderr)
                return exe

            def run(exe, mode, changes=None, success=True):
                env = {k: v for k, v in os.environ.items() if k not in ENV_FLAGS + PALETTE_FENCES + ("FAKE_RECORDING",)}
                env.update(changes or {})
                ran = subprocess.run([str(exe), mode], env=env, capture_output=True, text=True,
                                     timeout=5, preexec_fn=no_core)
                if success:
                    self.assertEqual(ran.returncode, 0, ran.stderr)
                else:
                    self.assertNotEqual(ran.returncode, 0, "mutant unexpectedly passed")
                return ran

            ce = compile_driver("ce", True)
            faithful = compile_driver("faithful", False)
            run(ce, "corrected")
            run(faithful, "faithful")
            for name in ENV_FLAGS[1:]:
                with self.subTest(faithful_mode=name):
                    run(ce, "faithful", {name: "1"})
            run(ce, "faithful", {"BEA_PORT_CE_LAYER": "0"})
            run(ce, "corrected", {"BEA_PORT_CE_LAYER": "1"})
            # Forcing the CE layer on (screenshots of CE during a replay) never reaches player binds: the palette's
            # fences hold the faithful index under every tape, recording, probe, clock, pilot and network run.
            for name in PALETTE_FENCES:
                with self.subTest(forced_layer_fence=name):
                    run(ce, "faithful", {"BEA_PORT_CE_LAYER": "1", name: "1"})
            run(ce, "faithful", {"BEA_PORT_CE_LAYER": "1", "FAKE_RECORDING": "1"})
            run(ce, "corrected", {"BEA_PORT_CE_LAYER": "1", "BEA_PORT_TAPE": ""})

            mutant = handler.replace("bindKey = key;", "bindKey = (char)key;")
            self.assertNotEqual(mutant, handler)
            (out / "handle_bind.inc").write_text(mutant)
            run(compile_driver("signed-mutant", True), "corrected", success=False)
            # Each fence is load-bearing: the layer switch alone lets a forced layer through a tape or network run.
            for name, old, new, changes in (
                    ("layer-only", "if (!PortCeLayer() || PortRecording())", "if (!PortCeLayer())",
                     {"BEA_PORT_CE_LAYER": "1", "FAKE_RECORDING": "1"}),
                    ("no-net-join", '"BEA_NET_JOIN"}', '"BEA_UNUSED"}', {"BEA_PORT_CE_LAYER": "1", "BEA_NET_JOIN": "1"}),
                    ("no-tape", '"BEA_PORT_TAPE"', '"BEA_UNUSED"', {"BEA_PORT_CE_LAYER": "1", "BEA_PORT_TAPE": "1"})):
                with self.subTest(fence_mutant=name):
                    mutant = handler.replace(old, new)
                    self.assertNotEqual(mutant, handler)
                    (out / "handle_bind.inc").write_text(mutant)
                    run(compile_driver(name, True), "faithful", changes, success=False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
