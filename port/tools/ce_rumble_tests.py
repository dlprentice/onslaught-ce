#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The CE gamepad rumble on the pinned SDL library's virtual gamepads: the production DirectInput shim (dinput.cpp,
window.cpp, portlog.cpp), the game's pad opening (InitDirectInput and its callbacks, ltshell.cpp after the port patches),
the patched CPCPlatform::TriggerRumble, CBattleEngine::AddShockShake and RecoilWeapon (BattleEngine.cpp), the actual
ce_rumble.cpp and ce.cpp's ce.ini reader. Each case is a process; the virtual pad's rumble callback is the observation.

This is what SDL is asked for (strengths, length, which pad), not how a physical controller feels: no motor, driver or
player was involved. Causal mutants of the rumble and its patch must each be caught.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deps  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("ltshell.cpp", "PCPlatform.cpp", "BattleEngine.cpp")
FUNCTIONS = {
    "INIT": ("ltshell.cpp", r"HRESULT PCLTShell::InitDirectInput\( HWND hDlg \)"),
    "ENUM": ("ltshell.cpp", r"BOOL CALLBACK PCLTShell::EnumJoysticksCallback\([^\n]*"),
    "AXES": ("ltshell.cpp", r"BOOL CALLBACK PCLTShell::EnumAxesCallback\([^\n]*"),
    "TRIGGER": ("PCPlatform.cpp", r"void CPCPlatform::TriggerRumble\(int pad\)"),
    "SHAKE": ("BattleEngine.cpp", r"void\tCBattleEngine::AddShockShake\(float amount\)[^\n]*"),
    "RECOIL": ("BattleEngine.cpp", r"void CBattleEngine::RecoilWeapon\([^\n]*\n[^\n]*\)"),
}
FENCES = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT",
          "BEA_NET_HOST", "BEA_NET_JOIN")


def definition(source, signature):
    found = re.findall(r"^" + signature + r"[^\n]*\n\{\n.*?^\}[ \t]*", source, re.MULTILINE | re.DOTALL)
    if len(found) != 1:
        raise AssertionError(f"expected one complete function: {signature}")
    return found[0] + "\n"


def patched(out, patches, override=None):
    for name in SOURCES:
        (out / name).write_bytes((ROOT / "src" / name).read_bytes())
    for path in sorted(patches.glob("*.patch")):
        data = override if override and path.name == override[0] else None
        data = data[1].encode("latin1") if data else path.read_bytes()
        sections = [s for s in re.split(br"(?=^--- a/)", data, flags=re.MULTILINE)
                    if any(s.startswith(f"--- a/{n}\n+++ b/{n}\n".encode()) for n in SOURCES)]
        if sections:
            r = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(out), "-r", "-"],
                               input=b"".join(sections), capture_output=True, timeout=10)
            if r.returncode:
                raise AssertionError(f"patch failed: {path.name}: {r.stdout + r.stderr}")
    return {name: (out / name).read_text(encoding="latin-1") for name in SOURCES}


class RumbleTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/ce-rumble-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, name, rumble=None, dinput=None, patch=None, network=False):
        folder = self.out / name
        (folder / "src").mkdir(parents=True)
        files = patched(folder / "src", ROOT / "port/patches", patch)
        (folder / "callers.inc").write_text("\n".join(definition(files[f], s) for f, s in FUNCTIONS.values()))
        common = folder / "platform/common"
        common.mkdir(parents=True)
        for n in ("window.cpp", "dinput.cpp", "portlog.cpp", "portlog.h", "platform.h", "ce.cpp", "ce_pad_queue.h"):
            shutil.copy(ROOT / "port/platform/common" / n, common)
        if dinput is not None:
            (common / "dinput.cpp").write_text(dinput)
        (folder / "updater").mkdir()
        shutil.copy(ROOT / "port/updater/ce_report.h", folder / "updater")
        game = folder / "game"
        game.mkdir()
        (game / "common.h").write_text("")
        (game / "CLIParams.h").write_text("")
        shutil.copy(ROOT / "port/game/ce_rumble.h", game)
        (game / "ce_rumble.cpp").write_text(rumble if rumble is not None else (ROOT / "port/game/ce_rumble.cpp").read_text())
        driver = (ROOT / "port/tools/tests/ce_rumble_driver.cpp").read_text()
        for key, path in {"WINDOW_CPP": common / "window.cpp", "DINPUT_CPP": common / "dinput.cpp",
                          "PORTLOG_CPP": common / "portlog.cpp", "CALLERS_INC": folder / "callers.inc",
                          "RUMBLE_CPP": game / "ce_rumble.cpp"}.items():
            driver = driver.replace(key, str(path))
        (folder / "driver.cpp").write_text(driver)
        sources = [str(folder / "driver.cpp"), str(common / "ce.cpp")]
        if network:
            (folder / "net.cpp").write_text('#include <stdlib.h>\nextern "C" int CE_BootRequested(void) { return getenv("TEST_NET_BOOT") != 0; }\n')
            sources.append(str(folder / "net.cpp"))
        exe = folder / "driver"
        cmd = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
               "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar",
               "-fms-extensions", "-fno-autolink", "-w", "-O0", '-DCE_VERSION="rumble-test"',
               f"-I{deps.PORT / 'include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
               f"-I{deps.PREFIX / 'include'}", f"-I{common}", f"-I{game}", *sources,
               f"-L{deps.PREFIX / 'lib'}", "-lSDL3", f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(exe)]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120, env=dict(os.environ, TMPDIR=str(folder)))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return exe

    def run_case(self, exe, case, ini="", extra=None):
        user = self.out / f"user-{len(list(self.out.glob('user-*')))}"
        user.mkdir()
        (user / "ce.ini").write_text(ini)
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "SDL_", "TEST_"))}
        env.update(SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS="1",
                   BEA_USER_DIR=str(user))
        env.update(extra or {})
        return subprocess.run([str(exe), case], env=env, capture_output=True, text=True, timeout=20)

    def passes(self, ran, text):
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn(text, ran.stdout)

    def test_rumble(self):
        exe = self.build("actual")
        ran = self.run_case(exe, "hit")
        self.passes(ran, "hit: rumble low=65535 high=0 for")
        print("\n" + ran.stdout.strip())
        self.passes(self.run_case(exe, "hit", "rumble=1\n"), "hit: rumble")
        self.passes(self.run_case(exe, "two-pads"), "pads mapped")
        self.passes(self.run_case(exe, "no-motor"), "left alone")
        self.passes(self.run_case(exe, "fenced", "rumble=0\n"), "no rumble")
        for flag in FENCES:
            with self.subTest(flag=flag):
                self.passes(self.run_case(exe, "fenced", extra={flag: "1"}), "no rumble")
                self.passes(self.run_case(exe, "fenced", extra={flag: "1", "BEA_PORT_CE_LAYER": "1"}), "no rumble")
        self.passes(self.run_case(exe, "fenced", extra={"TEST_RECORDING": "1"}), "no rumble")
        self.passes(self.run_case(exe, "fenced", extra={"BEA_PORT_CE_LAYER": "0"}), "no rumble")
        net = self.build("network", network=True)
        self.passes(self.run_case(net, "fenced", extra={"TEST_NET_BOOT": "1"}), "no rumble")
        self.passes(self.run_case(net, "hit"), "hit: rumble")

    def test_faithful_body_is_empty(self):
        files = patched(self.out, ROOT / "port/patches")
        body = definition(files["PCPlatform.cpp"], FUNCTIONS["TRIGGER"][1])
        outside = re.sub(r"#ifdef CE_VERSION.*?#endif", "", body, flags=re.DOTALL)
        self.assertNotRegex(re.sub(r"//[^\n]*", "", outside), r";", "the faithful build's TriggerRumble stays empty")

    def test_mutants_are_caught(self):
        rumble = (ROOT / "port/game/ce_rumble.cpp").read_text()
        dinput = (ROOT / "port/platform/common/dinput.cpp").read_text()
        patch = (ROOT / "port/patches/0105-ce-rumble.patch").read_text()
        header = (ROOT / "port/game/ce_rumble.h").read_text()
        mutants = [
            ("no-tape-fence", "rumble", '"BEA_PORT_TAPE", "BEA_PORT_PROBES"', '"BEA_UNUSED", "BEA_PORT_PROBES"',
             ("fenced", "", {"BEA_PORT_TAPE": "1", "BEA_PORT_CE_LAYER": "1"})),
            ("no-recording-fence", "rumble", "if (!PortCeLayer() || PortRecording()) return false;", "if (!PortCeLayer()) return false;",
             ("fenced", "", {"TEST_RECORDING": "1"})),
            ("ini-ignored", "rumble", "return ini != 0;", "return true;", ("fenced", "rumble=0\n", None)),
            ("default-off", "rumble", 'CE_SettingGetBool("rumble", 1)', 'CE_SettingGetBool("rumble", 0)', ("hit", "", None)),
            ("first-pad-always", "rumble", "PortRumblePad(pads[pad],", "PortRumblePad(pads[0],", ("two-pads", "", None)),
            ("half-strength", "rumble", "CE_RUMBLE_LOW, CE_RUMBLE_HIGH, CE_RUMBLE_MS", "CE_RUMBLE_LOW / 2, CE_RUMBLE_HIGH, CE_RUMBLE_MS",
             ("hit", "", None)),
            ("long-effect", "rumble", "CE_RUMBLE_LOW, CE_RUMBLE_HIGH, CE_RUMBLE_MS", "CE_RUMBLE_LOW, CE_RUMBLE_HIGH, CE_RUMBLE_MS * 4",
             ("hit", "", None)),
            ("motors-swapped", "dinput", "SDL_RumbleGamepad(d->pad, low, high, ms)", "SDL_RumbleGamepad(d->pad, high, low, ms)",
             ("hit", "", None)),
            ("pad-number", "patch", "+\tPortCeTriggerRumble((void *const *)LT.pJoystick, LT.mJoypads, pad);",
             "+\tPortCeTriggerRumble((void *const *)LT.pJoystick, LT.mJoypads, 0);", ("two-pads", "", None)),
            ("no-call", "patch", "+\tPortCeTriggerRumble((void *const *)LT.pJoystick, LT.mJoypads, pad);", "+\t(void)pad;",
             ("hit", "", None)),
        ]
        caught = []
        for name, where, old, new, (case, ini, extra) in mutants:
            with self.subTest(mutant=name):
                text = {"rumble": rumble, "dinput": dinput, "patch": patch}[where]
                self.assertEqual(text.count(old), 1, name)
                kwargs = {"rumble": {"rumble": rumble.replace(old, new)}, "dinput": {"dinput": dinput.replace(old, new)},
                          "patch": {"patch": ("0105-ce-rumble.patch", patch.replace(old, new))}}[where]
                exe = self.build(name, **kwargs)
                ran = self.run_case(exe, case, ini, extra)
                self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived: {ran.stdout}{ran.stderr}")
                caught.append(name)
        self.assertEqual(len(caught), len(mutants))
        self.assertIn("CE_RUMBLE_LOW = 0xffff, CE_RUMBLE_HIGH = 0, CE_RUMBLE_MS = 250", header)


if __name__ == "__main__":
    unittest.main(verbosity=2)
