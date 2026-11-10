#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual mouse event/message/device callers with privacy-bounded ordinary-session diagnostics.

Uses the existing input-lifetime caller fixture: complete production window.cpp, dinput.cpp, portlog.cpp and
patched game input callers, with authored SDL mouse/window boundaries and no game data, display or GPU. Tests
observe route counters AND the unchanged delivered input, admission, limits and error preservation. They do not
establish physical-device behavior, Windows driver acceptance, game response or a player's mouse feel.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

import deps
import input_lifetime_tests as lifetime
import menu_driver_markers
from window_tests import private_ui_compile_args

ROOT = Path(__file__).resolve().parents[2]
CASES = ("menu", "capture", "dequeue", "drop", "drop-payload", "limit", "errors", "saturate", "final-budget", "disabled")

EXERCISE = r'''
static void Motion(float x, float y) {
    SDL_Event event{}; event.type = SDL_EVENT_MOUSE_MOTION; event.motion.windowID = 17;
    event.motion.x = x; event.motion.y = y; event.motion.xrel = 7; event.motion.yrel = -8;
    if (!SDL_PushEvent(&event)) exit(7);
}
static int ActivityCase(const char *test) {
    using namespace PortInputActivity;
    Current() = State{};
    if (!strcmp(test,"disabled")) {
        unsigned ticks = diagnosticTickReads, getters = diagnosticGetterReads;
        Count(MOTION); Acquired(true); Pulse(testWindow,true);
        CHECK(diagnosticTickReads == ticks && diagnosticGetterReads == getters);
        CHECK(Current().count[MOTION] == 0 && !Current().acquired && !Current().observed);
        Motion(12345,23456); Drain();
        CHECK(windowMouse == 1 && Current().count[QUEUED] == 0 && Current().count[DISPATCHED] == 0);
        Device mouse(MOUSE,0); mouse.SetCooperativeLevel(MainWindow(),DISCL_EXCLUSIVE|DISCL_FOREGROUND);
        CHECK(SUCCEEDED(mouse.Acquire())); relativeX = 7; relativeY = -8;
        DIMOUSESTATE2 output{}; CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(output),&output)));
        CHECK(output.lX == 7 && output.lY == -8 && Current().count[POLL] == 0);
    } else if (!strcmp(test,"menu")) {
        Motion(12345,23456); Motion(12346,23457);
        MouseEvent(SDL_EVENT_MOUSE_BUTTON_DOWN); MouseEvent(SDL_EVENT_MOUSE_BUTTON_UP); Wheel(1); Drain();
        CHECK(Current().count[MOTION] == 2 && Current().count[BUTTON] == 2 && Current().count[WHEEL] == 1);
        CHECK(Current().count[QUEUED] == 4 && Current().count[COALESCED] == 1);
        CHECK(Current().count[DEQUEUED] == 4 && Current().count[DISPATCHED] == 4 && windowMouse == 4);
        CHECK(Current().count[CAPTURE_SUPPRESSED] == 0 && Current().count[POLL] == 0);
        Pulse(testWindow,true);
    } else if (!strcmp(test,"capture")) {
        Device mouse(MOUSE,0); mouse.SetCooperativeLevel(MainWindow(),DISCL_EXCLUSIVE|DISCL_FOREGROUND);
        CHECK(SUCCEEDED(mouse.Acquire())); Motion(12345,23456); MouseEvent(SDL_EVENT_MOUSE_BUTTON_DOWN); Wheel(1); Drain();
        CHECK(Current().acquired && Current().count[CAPTURE_SUPPRESSED] == 3 && windowMouse == 0);
        CHECK(Current().count[QUEUED] == 0 && Current().count[MOTION] == 1 && Current().count[BUTTON] == 1 && Current().count[WHEEL] == 1);
        relativeX = 7; relativeY = -8; DIMOUSESTATE2 output{};
        CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(output),&output)));
        CHECK(output.lX == 7 && output.lY == -8 && output.lZ == 120);
        CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(output),&output)) && output.lX == 0 && output.lY == 0 && output.lZ == 0);
        CHECK(Current().count[POLL] == 2 && Current().count[NONZERO_POLL] == 1);
        CHECK(SUCCEEDED(mouse.Unacquire()) && !Current().acquired);
        CHECK(mouse.GetDeviceState(sizeof(output),&output) == DIERR_NOTACQUIRED && Current().count[UNACQUIRED_POLL] == 1);
        Pulse(testWindow,true);
    } else if (!strcmp(test,"dequeue")) {
        PostMouseMove(MainWindow(),0,MAKELPARAM(100,200)); PostMouseMove(MainWindow(),0,MAKELPARAM(101,201));
        MSG message{}; CHECK(Pop(&message,false) && Pop(&message,false) && Current().count[DEQUEUED] == 0);
        CHECK(Pop(&message,true) && !Pop(&message,true) && Current().count[DEQUEUED] == 1);
        CHECK(Current().count[DISPATCHED] == 0); PortDispatchMessageA(&message);
        CHECK(windowMouse == 1 && Current().count[DISPATCHED] == 1 && Current().count[COALESCED] == 1);
        CHECK(MOUSE_X == 101 && MOUSE_Y == 201);
        Post(MainWindow(),WM_KEYDOWN,'Z',0); Drain();
        CHECK(directDown == 1 && Current().count[QUEUED] == 1 && Current().count[DISPATCHED] == 1);
        Pulse(testWindow,true);
    } else if (!strcmp(test,"drop")) {
        while (Free()) CHECK(Post(MainWindow(),WM_USER,0,0));
        CHECK(!Post(MainWindow(),WM_MOUSEMOVE,0,0));
        CHECK(Current().count[DROPPED] == 1 && Current().count[QUEUED] == 0 && Free() == 0);
        Pulse(testWindow,true);
    } else if (!strcmp(test,"drop-payload")) {
        while (Free()) CHECK(Post(MainWindow(),WM_USER,0,0));
        SDL_KeyboardEvent key{}; key.type = SDL_EVENT_KEY_DOWN; key.scancode = SDL_SCANCODE_Q;
        key.key = SDLK_Q; key.down = true;
        CHECK(KeyChar('Q',SDL_SCANCODE_Q,SDL_KMOD_NONE) == 'q');
        KeyEvent(key,MainWindow()); // Actual WM_KEYDOWN and WM_CHAR producers both meet the full queue.
        CHECK(gHeld == 1 && gReleasePending && Free() == 0 && directDown == 0);
        CHECK(!Post(MainWindow(),WM_MOUSEMOVE,MK_LBUTTON,MouseLParam(12345,23456)));
        CHECK(!Post(MainWindow(),WM_LBUTTONDOWN,MK_LBUTTON,MouseLParam(12345,23456)));
        CHECK(!Post(MainWindow(),WM_RBUTTONDOWN,MK_RBUTTON,MouseLParam(12345,23456)));
        CHECK(Current().count[DROPPED] == 3 && Current().count[QUEUED] == 0);
        Pulse(testWindow,true);
    } else if (!strcmp(test,"limit")) {
        for (unsigned i = 0; i < 300; ++i) { diagnosticNow += 10000; Pulse(testWindow); }
        CHECK(Current().summaries == 257);
        Count(MOTION); Pulse(testWindow,true); CHECK(Current().summaries == 258 && Current().count[MOTION] == 1);
    } else if (!strcmp(test,"errors")) {
        diagnosticMutateErrors = true; errno = 123;
        Pulse(testWindow); CHECK(errno == 123);
        unsigned before = Current().summaries; Pulse(testWindow); CHECK(errno == 123 && Current().summaries == before);
        diagnosticNow += 10000; Pulse(testWindow); CHECK(errno == 123 && Current().summaries == before + 1);
        focused = false; Pulse(testWindow); CHECK(Current().summaries == before + 2 && !relativeMode);
        focused = true; relativeMode = true; Pulse(testWindow); CHECK(Current().summaries == before + 3);
        diagnosticMutateErrors = false;
    } else if (!strcmp(test,"final-budget")) {
        for (unsigned i = 0; i < 2100; ++i) PortSessionLog(PORT_SESSION_GAME,false,"authored normal budget saturation");
        unsigned normal = gNormal, errors = gErrors;
        CHECK(normal >= 2048 && errors == 0);
        Count(MOTION);
        for (unsigned i = 0; i < 20; ++i) {
            gNumWindows = 1; gWindows[0].sdl = testWindow; gWindows[0].handle = reinterpret_cast<HWND>(1); gWindows[0].proc = Proc;
            CHECK(PortDestroyWindow(MainWindow())); // Actual final-summary caller, authored SDL destruction only.
        }
        CHECK(gNormal == normal && gErrors == errors && gWindowFinals == 8 && diagnosticDestroyCalls == 20);
    } else if (!strcmp(test,"saturate")) {
        Current().count[MOTION] = UINT_MAX - 1; Count(MOTION); Count(MOTION);
        CHECK(Current().count[MOTION] == UINT_MAX); Pulse(testWindow,true);
    } else return 2;
    puts("PASS actual input routes"); return 0;
}
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(out):
    sources = {name: (ROOT / "src" / name).read_bytes() for name in lifetime.SOURCES}
    for name, data in sources.items():
        (out / name).write_bytes(data)
    patches = []
    for path in sorted((ROOT / "port/patches").glob("*.patch")):
        data = path.read_bytes()
        sections = [section for section in re.split(br"(?=^--- a/)", data, flags=re.MULTILINE)
                    if any(section.startswith(f"--- a/{name}\n+++ b/{name}\n".encode()) for name in lifetime.SOURCES)]
        if sections:
            result = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(out), "-r", "-"],
                                    input=b"".join(sections), capture_output=True, timeout=10)
            patches.append(dict(name=path.name, sha256=digest(path), exit=result.returncode))
            if result.returncode:
                raise RuntimeError(result.stdout.decode() + result.stderr.decode())
    callers = out / "callers.inc"
    callers.write_text("\n".join(lifetime.definition((out / filename).read_text(encoding="latin-1"), signature)
                                for filename, signature in lifetime.FUNCTIONS.values()))
    for name in ("window.cpp", "dinput.cpp", "portlog.cpp", "inventory.cpp", "input_activity.h", "portlog.h"):
        (out / name).write_bytes((ROOT / "port/platform/common" / name).read_bytes())
    (out / "ce_tutorial_pad.h").write_bytes((ROOT / "port/include/ce_tutorial_pad.h").read_bytes())    # dinput.cpp's tutorial pad query types
    # The shell's inline joystick accessors, as input_lifetime_tests takes them from ltshell.h for the shared driver's JOY_BUTTON_ACCESSORS.
    header = (ROOT / "src/ltshell.h").read_text(encoding="latin-1")
    accessors = []
    for name in ("JoyButtonOnce", "JoyButtonOn", "JoyButtonRelease"):
        found = re.findall(r"\bBOOL\s+" + name + r"\([^)]*\)\s*\{[^}]*\}", header, re.DOTALL)
        if len(found) != 1:
            raise RuntimeError(f"expected one inline input accessor: {name}")
        accessors.append(found[0])
    source = menu_driver_markers.expand((ROOT / "port/tools/tests/input_lifetime_driver.cpp").read_text().replace("JOY_BUTTON_ACCESSORS", "\n".join(accessors)), ROOT)
    source = source.replace("static bool focused", "static Uint64 diagnosticNow = 1000; static unsigned diagnosticTickReads,diagnosticGetterReads;\nstatic bool diagnosticMutateErrors;\nstatic Uint64 DiagnosticTicks() { ++diagnosticTickReads; if(diagnosticMutateErrors) errno=456; return diagnosticNow; }\n#define SDL_GetTicks DiagnosticTicks\nstatic bool focused", 1)
    source = source.replace("SDL_WindowFlags captured =", "++diagnosticGetterReads; if(diagnosticMutateErrors) errno=456;\n    SDL_WindowFlags captured =", 1)
    source = source.replace("static bool TestGetRelative(SDL_Window *) {", "static bool TestGetRelative(SDL_Window *) { ++diagnosticGetterReads; if(diagnosticMutateErrors) errno=456;", 1)
    source = source.replace("#include <cassert>", "#include <cassert>\n#include <cerrno>", 1)
    source = source.replace("#define SDL_GetWindowFlags TestWindowFlags", "static unsigned diagnosticDestroyCalls;\nstatic void DiagnosticDestroyWindow(SDL_Window *) { ++diagnosticDestroyCalls; }\n#define SDL_DestroyWindow DiagnosticDestroyWindow\n#define SDL_GetWindowFlags TestWindowFlags", 1)
    source = source.replace("#define SDL_GetWindowFlags TestWindowFlags", "static SDL_Keycode DiagnosticKeyFromScancode(SDL_Scancode scan,SDL_Keymod mod,bool event) { return scan==SDL_SCANCODE_Q ? SDLK_Q : SDL_GetKeyFromScancode(scan,mod,event); }\n#define SDL_GetKeyFromScancode DiagnosticKeyFromScancode\n#define SDL_GetWindowFlags TestWindowFlags", 1)
    assert source.count("bool PortCeLayer() { return ceLayer; }") == 1, "the shared driver's PortCeLayer changed"
    source = source.replace("bool PortCeLayer() { return ceLayer; }", lifetime.definition(
        (ROOT / "port/platform/common/ce.cpp").read_text(), r"bool PortCeLayer\(\)"), 1)
    source = source.replace("int main(int argc, char **argv) {", EXERCISE + "\nint main(int argc, char **argv) {", 1)
    source = source.replace('    if (!strncmp(test, "pad-", 4)) {', '    if (!strncmp(test,"activity-",9)) return ActivityCase(test+9);\n    if (!strncmp(test, "pad-", 4)) {', 1)
    for name, path in {"WINDOW_CPP": out / "window.cpp", "DINPUT_CPP": out / "dinput.cpp",
                       "PORTLOG_CPP": out / "portlog.cpp", "INVENTORY_CPP": out / "inventory.cpp", "CALLERS_INC": callers}.items():
        source = source.replace(name, str(path))
    cpp = out / "driver.cpp"
    cpp.write_text(source)
    return cpp, patches


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--syntax-targets", action="store_true", help="also compile complete changed TUs with both pinned target compilers")
    args = parser.parse_args()
    parent = ROOT / "local-data/port/mouse-activity-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = dict(source_root=str(ROOT), sources={str(p.relative_to(ROOT)): digest(p) for p in
                    [*(ROOT / "port/platform/common" / name for name in ("window.cpp", "dinput.cpp", "input_activity.h", "portlog.cpp", "portlog.h")),
                     ROOT / "port/platform/common/ce.cpp", ROOT / "port/tools/mouse_activity_tests.py", ROOT / "port/tools/tests/input_lifetime_driver.cpp"]}, cases=[], commands=[], mutations=[])
    try:
        cpp, receipt["patches"] = prepare(out)
        binary = out / "driver"
        command = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
                   "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar", "-fms-extensions",
                   "-fno-autolink", "-O0", "-w", '-DCE_VERSION="input-diagnostic-test"',
                   f"-I{deps.PORT / 'include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
                   f"-I{deps.PREFIX / 'include'}", f"-I{ROOT / 'port/platform/common'}", str(cpp),
                   f"-L{deps.PREFIX / 'lib'}", "-lSDL3", f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(binary)]
        def compile_fixture(label):
            result = subprocess.run(command + private_ui_compile_args(ROOT / "port"), capture_output=True, text=True, timeout=60, env=dict(os.environ, TMPDIR=str(out)))
            (out / f"{label}-compile.log").write_text(result.stdout + result.stderr)
            receipt["commands"].append(dict(label=label, command=command, exit=result.returncode))
            if result.returncode:
                raise RuntimeError(f"fixture compile failed: {label}")
        env = {key: value for key, value in os.environ.items() if not key.startswith(("BEA_", "SDL_"))}
        env.update(SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
        def run(case, **extra):
            return subprocess.run([str(binary), "activity-" + case], env=dict(env, **extra), capture_output=True, text=True, timeout=10)
        compile_fixture("actual")
        for case in CASES[:-1]:
            result = run(case)
            receipt["cases"].append(dict(case=case, exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
            if result.returncode or "PASS actual input routes" not in result.stdout:
                raise RuntimeError(f"actual caller failed: {case}")
            if case == "menu" and any(x in result.stderr for x in ("12345", "23456", "12346", "23457")):
                raise RuntimeError("raw mouse coordinate entered ordinary log")
            if case == "drop-payload":
                drops = [line for line in result.stderr.splitlines() if "input: queue full" in line]
                if len(drops) != 5 or any(not line.endswith(f"message dropped (drop={n})") for n,line in enumerate(drops,1)):
                    raise RuntimeError("ordinary queue drop disclosed an input identity/payload or lost count context")
            if case == "limit" and result.stderr.count("mouse activity cumulative") != 257:
                raise RuntimeError("diagnostic interval bound/final summary differed")
            if case == "final-budget":
                final = [line for line in result.stderr.splitlines() if "mouse activity cumulative final=1" in line]
                if len(final) != 8 or any("window:" not in line or "sdl_motion=1" not in line for line in final):
                    raise RuntimeError("normal-log saturation hid or unbounded the final input summaries")
        for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_CE_LAYER"):
            result = run("disabled", **{name: "0" if name == "BEA_PORT_CE_LAYER" else "1"})
            receipt["cases"].append(dict(case="disabled-" + name, exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
            if result.returncode or result.stderr:
                raise RuntimeError(f"disabled route changed observation/input: {name}")
        public_command = list(command)
        command.remove('-DCE_VERSION="input-diagnostic-test"')
        compile_fixture("private")
        result = run("disabled")
        receipt["cases"].append(dict(case="private-disabled", exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
        if result.returncode or result.stderr:
            raise RuntimeError("private diagnostic admission failed")
        command[:] = public_command
        mutants = [("missed-motion", "window.cpp", "PortInputActivity::Count(PortInputActivity::MOTION);", "(void)0;", "menu"),
                   ("missed-capture", "window.cpp", "PortInputActivity::Count(PortInputActivity::CAPTURE_SUPPRESSED);", "(void)0;", "capture"),
                   ("double-input-read", "dinput.cpp", "SDL_MouseButtonFlags b = SDL_GetRelativeMouseState(&dx, &dy);", "SDL_GetRelativeMouseState(NULL,NULL); SDL_MouseButtonFlags b = SDL_GetRelativeMouseState(&dx, &dy);", "capture"),
                   ("missing-count-admission", "input_activity.h", "if (!PortSessionEnabled() || event < 0 || event >= EVENTS) return;", "if (event < 0 || event >= EVENTS) return;", "disabled"),
                   ("raw-drop-payload", "window.cpp", '"input: queue full, message dropped (drop=%u)\\n", drops + 1u', '"input: queue full, message %04x (%08lx, %08lx) dropped\\n", msg, (unsigned long)w, (unsigned long)l', "drop-payload"),
                   ("raw-drop-identity", "window.cpp", '"input: queue full, message dropped (drop=%u)\\n", drops + 1u', '"input: queue full, message %04x dropped (drop=%u)\\n", msg, drops + 1u', "drop-payload"),
                   ("final-normal-budget", "portlog.cpp", "if (final)\n\t{", "if (false && final)\n\t{", "final-budget")]
        for label, filename, before, after, case in mutants:
            path = out / filename; original = path.read_text()
            if before not in original:
                raise RuntimeError(f"mutation drift: {label}")
            path.write_text(original.replace(before, after))
            compile_fixture(label)
            result = run(case, **({"BEA_PORT_CLOCK": "1"} if case == "disabled" else {}))
            privacy_failure = case == "drop-payload" and (("(00000071," in result.stderr and "5ba03039" in result.stderr)
                or "message 0201 dropped" in result.stderr or "message 0204 dropped" in result.stderr)
            final_failure = case == "final-budget" and result.stderr.count("mouse activity cumulative final=1") != 8
            receipt["mutations"].append(dict(label=label, exit=result.returncode, stderr=result.stderr,
                                              privacy_failure=privacy_failure, final_summary_failure=final_failure))
            path.write_text(original)
            if not result.returncode and not privacy_failure and not final_failure:
                raise RuntimeError(f"causal mutant survived: {label}")
        if args.syntax_targets:
            import sys
            sys.path.insert(0, str(ROOT / "port"))
            import build
            for target in ("linux", "windows"):
                config = build.TARGETS[target]
                prefix = deps.PREFIX if target == "linux" else deps.PREFIX_WIN
                for name in ("window.cpp", "dinput.cpp", "portlog.cpp"):
                    includes = [ROOT / "port/d3dx/include", ROOT / "port/include", *config["includes"], prefix / "include"]
                    cmd = [*config["cxx"], "-std=gnu++17", "-DNOMINMAX", "-fms-extensions", "-fno-autolink", "-fno-strict-aliasing",
                           "-fwrapv", "-mms-bitfields", "-fshort-wchar", "-w", '-DCE_VERSION="input-diagnostic-test"',
                           *(f"-I{p}" for p in includes), "-fsyntax-only", str(ROOT / "port/platform/common" / name)]
                    result = subprocess.run(cmd + private_ui_compile_args(ROOT / "port", link=False), capture_output=True, text=True, timeout=60)
                    (out / f"{target}-{name}.log").write_text(result.stdout + result.stderr)
                    receipt["commands"].append(dict(label=target + "-" + name, command=cmd, exit=result.returncode))
                    if result.returncode:
                        raise RuntimeError("complete target syntax failed")
        receipt["pass"] = True
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt.update(error=str(error), **{"pass": False})
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({k: receipt[k] for k in ("pass", "error") if k in receipt}))
    print(out / "result.json")
    return 0 if receipt["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
