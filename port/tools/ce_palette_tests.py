#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual CE palette and patched shell with authored fakes; no game, device or network."""
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "port/patches/0092-ce-safe-palette.patch"
DRIVER = ROOT / "port/tools/ce_palette_driver.cpp"
FAKE = ROOT / "port/tools/ce_palette_fake.h"
PALETTE = ROOT / "port/game/ce_palette.cpp"
FLAGS = ("BEA_PORT_CE_LAYER", "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
         "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN")


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    return source[start:end]


def sections(patch):
    return {m.group(1): m.group(0) for m in re.finditer(
        r"(?ms)^--- a/([^\n]+)\n\+\+\+ b/[^\n]+\n.*?(?=^--- a/|\Z)", patch)}


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class PaletteTests(unittest.TestCase):
    def test_actual_shell_palette_and_modes(self):
        parent = ROOT / "local-data/port/ce-palette-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="run-", dir=parent) as tmp:
            out = Path(tmp)
            for name in ("ltshell.cpp", "game.cpp", "DXEngine.cpp"):
                (out / name).write_bytes((ROOT / "src" / name).read_bytes())
            diagnostic = sections((ROOT / "port/patches/0061-session-diagnostics.patch").read_text())["game.cpp"]
            (out / "diagnostic.patch").write_text(diagnostic)
            lifecycle = sections((ROOT / "port/patches/0059-consume-inactive-ce-quit.patch").read_text())["ltshell.cpp"]
            (out / "lifecycle.patch").write_text(lifecycle)
            for patch in (out / "diagnostic.patch", out / "lifecycle.patch", PATCH):
                applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                                         cwd=out, capture_output=True, text=True, timeout=10, preexec_fn=no_core)
                self.assertEqual(applied.returncode, 0, applied.stdout + applied.stderr)
                self.assertNotIn("fuzz", applied.stdout.lower())
            shell = (out / "ltshell.cpp").read_text(encoding="latin1")
            (out / "msgproc.inc").write_text(function(shell, "LRESULT PCLTShell::MsgProc("))
            (out / "ce_layer.inc").write_text(function((ROOT / "port/platform/common/ce.cpp").read_text(), "bool PortCeLayer()"))
            (out / "ce_palette.cpp").write_bytes(PALETTE.read_bytes())
            (out / "ce_palette_fake.h").write_bytes(FAKE.read_bytes())
            for name in ("common.h", "game.h", "frontend.h", "PauseMenu.h", "CLIParams.h", "ltshell.h", "DXEngine.h",
                         "Hud.h", "SpriteRenderer.h", "renderinfo.h", "Texture.h", "GameInterface.h"):
                (out / name).write_text('#include "ce_palette_fake.h"\n')

            objects = {}

            def build(name, ce, network=False):
                flags = [os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror",
                         *(["-DCE_VERSION=1"] if ce else []), "-I", str(out)]
                # Compile the palette only once per mode. The same object must work before and after linking networking.
                if ce not in objects:
                    objects[ce] = out / ("palette-ce.o" if ce else "palette-faithful.o")
                    built = subprocess.run(flags + ["-c", str(out / "ce_palette.cpp"), "-o", str(objects[ce])],
                                           capture_output=True, text=True, timeout=30, preexec_fn=no_core)
                    self.assertEqual(built.returncode, 0, built.stderr)
                extra = []
                if network:
                    real_network = ROOT / "port/game/ce_network_boot.cpp"
                    if real_network.is_file():
                        boot = real_network.read_text()
                        accessor = function(boot, 'extern "C" int CE_BootRequested(void)')
                        self.assertIn("__atomic_load_n(&requested, __ATOMIC_ACQUIRE)", accessor)
                        self.assertIn("__atomic_store_n(&requested, 1, __ATOMIC_RELEASE)", boot)
                    else:
                        accessor = 'extern "C" int CE_BootRequested(void) { return requested; }'
                    fixture = out / "network_boot.cpp"
                    fixture.write_text('namespace { int requested; }\n' + accessor +
                                       '\nextern "C" void SetBootRequested(bool v) '
                                       '{ __atomic_store_n(&requested, v ? 1 : 0, __ATOMIC_RELEASE); }\n')
                    extra = ["-DFAKE_NETWORK=1", str(fixture)]
                exe = out / name
                cmd = flags + extra + [str(DRIVER), str(objects[ce]), "-o", str(exe)]
                built = subprocess.run(cmd, capture_output=True, text=True, timeout=30, preexec_fn=no_core)
                self.assertEqual(built.returncode, 0, built.stderr)
                return exe

            def run(exe, mode="ordinary", changes=None):
                env = {k: v for k, v in os.environ.items() if k not in FLAGS}
                env.update(changes or {})
                ran = subprocess.run([str(exe), mode], env=env, capture_output=True, text=True,
                                     timeout=8, preexec_fn=no_core)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                return ran.stdout

            plain = build("ce", True)
            self.assertIn("key lifetimes passed", run(plain))
            run(plain, "denied", {"BEA_PORT_CE_LAYER": "0"})
            for flag in FLAGS[1:]:
                with self.subTest(flag=flag):
                    # The CE screenshot override must never enable interactive commands in these modes.
                    run(plain, "denied", {"BEA_PORT_CE_LAYER": "1", flag: "1"})
            network = build("network", True, network=True)
            self.assertIn("key lifetimes passed", run(network))
            faithful = build("faithful", False)
            self.assertIn("faithful message path passed", run(faithful))

            game = (out / "game.cpp").read_text(encoding="latin1")
            shutdown = function(game, "void\tCGame::ShutdownRestartLoop()")
            self.assertLess(shutdown.index("PortCePaletteClose()"), shutdown.index("SAFE_DELETE(mController"))
            engine = (out / "DXEngine.cpp").read_text(encoding="latin1")
            post = function(engine, "HRESULT\tCDXEngine::PostRender(")
            self.assertIn("if (!gMirrorPass)", post)
            self.assertLess(post.index("GAMEINTERFACE.Render()"), post.index("PortCePaletteRender()"))
            self.assertLess(post.index("PortCePaletteRender()"), post.index("PLATFORM.EndScene()"))

    def test_no_retail_console_dispatch_and_discoverable_help(self):
        code = re.sub(r"//[^\n]*|/\*.*?\*/", "", PALETTE.read_text(), flags=re.DOTALL)
        for forbidden in ("CONSOLE.", "ExecuteCommand(", "system(", "fopen(", "popen("):
            self.assertNotIn(forbidden, code)
        self.assertIn("CE_BootRequested()", code)
        self.assertIn("PortRecording()", code)
        self.assertIn("IsCheatActive(4)", code)
        cheat = (ROOT / "port/game/ce_frontend.cpp").read_text()
        self.assertIn("The grave key (left of 1, below Esc) opens CE commands.", cheat)


if __name__ == "__main__":
    unittest.main(verbosity=2)
