#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The game's window is handed back to the desktop: PortSetWindowPos's own text over authored SDL and Win32 ends.

After a windowed device reset the graphics host calls SetWindowPos(window, HWND_NOTOPMOST, old bounds, SWP_SHOWWINDOW),
as the original framework does, because Direct3D keeps an exclusive-fullscreen window in front of everything. The port's
wrapper used to drop the not-topmost part for the game's own window, and SDL never changes a window's place in front of
or behind others. This suite compiles the wrapper's text twice, with the Windows branch and with the branch every other
target has, and requires: the not-topmost request reaches the window itself on Windows, moving nothing; a plain move or
a request that says to leave the z-order makes no such call; a window the port does not own goes to the system whole;
during a live Window mode switch a window with a border, put back at the display's corner, is moved down until its
title bar is on the display, and not otherwise; and the other targets' branch makes no native call and moves nothing.

--negative-root DIR runs the Windows cases over DIR's window.cpp and requires the hand-back case to fail there.
No window, desktop, game or Windows process is used: what Direct3D and Windows then do is not shown here.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
import public_d3dapp_tests as helpers  # noqa: E402  (its brace-balancing function extractor)

CASES = ("hand-back", "plain-move", "other-window", "switch-corner", "switch-clear", "no-switch", "switch-borderless", "switch-unknown")


def build(source, out, windows):
    """The wrapper's text and what it uses of the file, with _WIN32's branches chosen by the caller."""
    text = source.read_text()
    pieces = []
    helper = re.search(r"^static bool gWindowModeSwitch;", text, re.M)
    if helper:
        pieces.append("static bool gWindowModeSwitch;")
        pieces.append(helpers.function(text, r"^static void KeepTitleBarOnDisplay\("))
    else:
        pieces.append("static bool gWindowModeSwitch;	// absent from this window.cpp")
    wrapper = helpers.function(text, r'^extern "C" BOOL WINAPI PortSetWindowPos\(')
    if helper and "KeepTitleBarOnDisplay" not in wrapper:
        raise AssertionError("the wrapper does not use its title bar helper")
    pieces.append(wrapper.replace("#if defined(_WIN32)", "#if WINDOWS_BRANCH /* _WIN32 in window.cpp */"))
    if helper:
        hold = helpers.function(text, r"^void PortHoldPointerForWindowChange\(bool after\)")
        first = hold.index("{") + 1
        pieces.append("void PortHoldPointerForWindowChange(bool after)\n{\n\tgWindowModeSwitch = !after;\n}\n"
                      if "gWindowModeSwitch = !after;" in hold[first:] else "void PortHoldPointerForWindowChange(bool) {}\n")
        if "gWindowModeSwitch = !after;" not in hold:
            raise AssertionError("the switch's two calls do not mark the switch")
    else:
        pieces.append("void PortHoldPointerForWindowChange(bool) {}\n")
    out.mkdir(parents=True, exist_ok=True)
    (out / "window_setpos.inc").write_text("\n".join(pieces).replace('extern "C" BOOL', "BOOL") + "\n")
    binary = out / ("windows" if windows else "other")
    done = subprocess.run(["clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", f"-DWINDOWS_BRANCH={int(windows)}",
                           "-I", str(out), str(ROOT / "port/tools/tests/window_topmost_driver.cpp"), "-o", str(binary)],
                          text=True, capture_output=True, timeout=120)
    if done.returncode:
        raise RuntimeError(done.stdout + done.stderr)
    return binary


def run(binary, case):
    return subprocess.run([str(binary), case], text=True, capture_output=True, timeout=20)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--negative-root", type=Path)
    args = parser.parse_args()
    scratch = ROOT / "local-data/window-topmost-tests"
    source = ROOT / "port/platform/common/window.cpp"
    for windows in (True, False):
        binary = build(source, scratch / "current", windows)
        for case in CASES:
            done = run(binary, case)
            if done.returncode or done.stdout.strip() != "window position pass":
                raise AssertionError(f"{'windows' if windows else 'other'} {case}: {done.stdout}{done.stderr}")
    print(f"window hand-back: {len(CASES)} cases pass with the Windows branch and {len(CASES)} with the other targets' branch")
    # controls: the two Windows statements removed one at a time
    text = source.read_text()
    controls = (("z-order-dropped", "\t\tSetWindowPos(h, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);\n", "\t\t;\n", "hand-back"),
                ("title-bar-left-above", "\t\tKeepTitleBarOnDisplay(Sdl(h), &y);\n", "\t\t(void)&KeepTitleBarOnDisplay;\n", "switch-corner"),
                ("moved-outside-a-switch", "if (gWindowModeSwitch && !(flags & SWP_NOMOVE))", "if (!(flags & SWP_NOMOVE))", "no-switch"))
    for label, old, new, case in controls:
        if text.count(old) != 1:
            raise AssertionError(f"control anchor: {label}")
        folder = scratch / label
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "window.cpp").write_text(text.replace(old, new))
        done = run(build(folder / "window.cpp", folder, True), case)
        if done.returncode != 2 or "FAIL:" not in done.stderr:
            raise AssertionError(f"control survived: {label}\n{done.stdout}{done.stderr}")
    print(f"controls: {len(controls)} removed statements each fail their case")
    if args.negative_root:
        old = args.negative_root / "port/platform/common/window.cpp"
        done = run(build(old, scratch / "negative", True), "hand-back")
        if done.returncode != 2 or "the not-topmost request reaches the window itself" not in done.stderr:
            raise AssertionError(f"expected the dropped not-topmost request on {old}\n{done.stdout}{done.stderr}")
        print(f"{args.negative_root}: the hand-back case fails there ({done.stderr.strip()})")


if __name__ == "__main__":
    main()
