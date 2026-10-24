#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the actual DesktopFullscreen callback with scripted SDL windows.

Requires Python and a host C++ compiler; no SDL installation, game data, display or GPU. Both platform branches are
compiled from the unmodified function extracted from d3d9hooks.cpp. Windows exclusive presentation must prepare the
owned SDL client size without changing Direct3D's parameters; Linux retains its desktop-fullscreen conversion.
The fake SDL applies pending dimensions at SyncWindow to detect missing synchronization. This is instrumentation,
not a model of Win32 message timing: real SDL client rectangles, native hit testing, mouse delivery and Direct3D
CreateDevice/Reset behavior still require a native runtime check. Each run retains its source hash and results.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path("port/platform/common/d3d9hooks.cpp")

HARNESS = r"""
#include <cstdio>
#include <cstdlib>
#include <cstring>
using UINT = unsigned;
using HWND = void *;
#define TRUE 1
#define PORT_LOG_PREFIX "[fullscreen-test] "
struct D3DPRESENT_PARAMETERS
{
    UINT BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount;
    UINT MultiSampleType, MultiSampleQuality, SwapEffect;
    HWND hDeviceWindow;
    int Windowed, EnableAutoDepthStencil;
    UINT AutoDepthStencilFormat, Flags, FullScreen_RefreshRateInHz, PresentationInterval;
};
struct SDL_Window
{
    int width = 640, height = 480, pending_width = 0, pending_height = 0;
    unsigned resize_calls = 0, sync_calls = 0, mode_calls = 0, fullscreen_calls = 0;
    bool desktop_mode = false, fullscreen_enabled = false;
};
SDL_Window windows[2];
HWND handles[] = {&windows[0], &windows[1]};
static int alien;
static const char *driver = "x11", *exclusive;
static unsigned hide_calls;
UINT gBoxW = 31, gBoxH = 37;
SDL_Window *gBoxWindow = &windows[0];
SDL_Window *PortSdlWindow(HWND handle)
{
    for (unsigned i = 0; i < 2; ++i) if (handle == handles[i]) return &windows[i];
    return nullptr;
}
bool SDL_GetWindowSizeInPixels(SDL_Window *w, int *width, int *height)
{
    *width = w->width; *height = w->height; return true;
}
bool SDL_SetWindowSize(SDL_Window *w, int width, int height)
{
    ++w->resize_calls; w->pending_width = width; w->pending_height = height; return true;
}
bool SDL_SyncWindow(SDL_Window *w)
{
    ++w->sync_calls;
    if (w->pending_width && w->pending_height)
    {
        w->width = w->pending_width; w->height = w->pending_height;
        w->pending_width = w->pending_height = 0;
    }
    return true;
}
bool SDL_SetWindowFullscreenMode(SDL_Window *w, const void *mode)
{
    ++w->mode_calls; w->desktop_mode = mode == nullptr; return true;
}
bool SDL_SetWindowFullscreen(SDL_Window *w, bool enabled)
{
    ++w->fullscreen_calls; w->fullscreen_enabled = enabled; return true;
}
bool SDL_HideCursor() { ++hide_calls; return true; }
const char *SDL_GetCurrentVideoDriver() { return driver; }
const char *SDL_GetError() { return "scripted SDL error"; }
const char *test_getenv(const char *name)
{
    return !std::strcmp(name, "BEA_PORT_EXCLUSIVE") ? exclusive : nullptr;
}
#define getenv test_getenv
@FUNCTION@
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *test = argv[1];
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 1280; pp.BackBufferHeight = 800;
    pp.BackBufferFormat = 22; pp.BackBufferCount = 2;
    pp.MultiSampleType = 3; pp.MultiSampleQuality = 4; pp.SwapEffect = 5;
    pp.hDeviceWindow = handles[1]; pp.EnableAutoDepthStencil = 1;
    pp.AutoDepthStencilFormat = 75; pp.Flags = 17;
    pp.FullScreen_RefreshRateInHz = 144; pp.PresentationInterval = 0x80000000u;
    HWND focus = handles[0];
    bool null_params = false;
    if (!std::strcmp(test, "device-preferred")) {}
    else if (!std::strcmp(test, "focus-fallback")) pp.hDeviceWindow = nullptr;
    else if (!std::strcmp(test, "width-only")) windows[1].height = 800;
    else if (!std::strcmp(test, "height-only")) windows[1].width = 1280;
    else if (!std::strcmp(test, "same-size")) { windows[1].width = 1280; windows[1].height = 800; }
    else if (!std::strcmp(test, "null-parameters")) null_params = true;
    else if (!std::strcmp(test, "windowed")) pp.Windowed = TRUE;
    else if (!std::strcmp(test, "zero-width")) pp.BackBufferWidth = 0;
    else if (!std::strcmp(test, "zero-height")) pp.BackBufferHeight = 0;
    else if (!std::strcmp(test, "unowned-device")) pp.hDeviceWindow = &alien;
    else if (!std::strcmp(test, "unowned-focus")) { pp.hDeviceWindow = nullptr; focus = &alien; }
    else if (!std::strcmp(test, "offscreen")) driver = "offscreen";
    else if (!std::strcmp(test, "no-driver")) driver = nullptr;
    else if (!std::strcmp(test, "exclusive-opt-out")) exclusive = "1";
    else return 2;
    D3DPRESENT_PARAMETERS expected;
    std::memcpy(&expected, &pp, sizeof(pp));
#ifndef _WIN32
    if (!null_params && !pp.Windowed && driver && std::strcmp(driver, "offscreen") && !exclusive)
    {
        expected.Windowed = TRUE;
        expected.FullScreen_RefreshRateInHz = 0;
    }
#endif
    DesktopFullscreen(focus, null_params ? nullptr : &pp);
    int box_window = gBoxWindow == &windows[0] ? 0 : gBoxWindow == &windows[1] ? 1 : -1;
    std::printf("{\"parameters_ok\":%s,\"box\":[%u,%u,%d],\"hide_calls\":%u,\"windows\":[",
                std::memcmp(&pp, &expected, sizeof(pp)) ? "false" : "true", gBoxW, gBoxH, box_window, hide_calls);
    for (unsigned i = 0; i < 2; ++i)
    {
        const SDL_Window &w = windows[i];
        std::printf("%s{\"size\":[%d,%d],\"pending\":[%d,%d],\"resize_calls\":%u,\"sync_calls\":%u,"
                    "\"mode_calls\":%u,\"fullscreen_calls\":%u,\"desktop_mode\":%s,\"fullscreen_enabled\":%s}",
                    i ? "," : "", w.width, w.height, w.pending_width, w.pending_height, w.resize_calls,
                    w.sync_calls, w.mode_calls, w.fullscreen_calls, w.desktop_mode ? "true" : "false",
                    w.fullscreen_enabled ? "true" : "false");
    }
    std::puts("]}");
}
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def extract_function(source):
    # Nested scopes are indented; the closing brace at column zero ends the complete function, including #ifdefs.
    matches = list(re.finditer(r"^void DesktopFullscreen\(HWND focus, D3DPRESENT_PARAMETERS \*pp\)\n\{\n.*?^\}",
                              source, re.MULTILINE | re.DOTALL))
    if len(matches) != 1:
        raise RuntimeError("expected one complete DesktopFullscreen definition")
    return matches[0].group() + "\n"


def check_result(platform, case, actual):
    """Check observable effects, allowing repeated successful resize/sync calls where a resize is needed."""
    errors = []
    sizes = [[640, 480], [640, 480]]
    if case in ("width-only", "same-size"):
        sizes[1][1] = 800
    if case in ("height-only", "same-size"):
        sizes[1][0] = 1280
    target = 0 if case == "focus-fallback" else 1
    resize = platform == "windows" and case in ("device-preferred", "focus-fallback", "width-only", "height-only")
    convert = platform == "linux" and case in ("device-preferred", "focus-fallback", "unowned-device")
    owned_convert = convert and case != "unowned-device"
    if resize:
        sizes[target] = [1280, 800]
    box = [0, 0, 0] if case == "windowed" else [31, 37, 0]
    if convert:
        box = [1280, 800, target if owned_convert else -1]
    if actual.get("parameters_ok") is not True:
        errors.append("presentation parameters changed unexpectedly")
    if actual.get("box") != box:
        errors.append(f"desktop-fullscreen bookkeeping: expected {box}, got {actual.get('box')}")
    if (actual.get("hide_calls", 0) > 0) != owned_convert:
        errors.append("unexpected cursor visibility request")
    windows = actual.get("windows", [])
    if len(windows) != 2:
        return errors + ["missing window observations"]
    for index, window in enumerate(windows):
        if window.get("size") != sizes[index] or window.get("pending") != [0, 0]:
            errors.append(f"window {index}: expected completed size {sizes[index]}, got {window}")
        active_resize = resize and index == target
        active_convert = owned_convert and index == target
        if window.get("desktop_mode") != active_convert or window.get("fullscreen_enabled") != active_convert:
            errors.append(f"window {index}: desktop-fullscreen state differs from {active_convert}")
        for key, positive in (("resize_calls", active_resize), ("sync_calls", active_resize or active_convert),
                              ("mode_calls", active_convert), ("fullscreen_calls", active_convert)):
            count = window.get(key, -1)
            if (positive and count <= 0) or (not positive and count != 0):
                errors.append(f"window {index}: {key} expected {'positive' if positive else 'zero'}, got {count}")
    return errors


def run_case(binary, platform, case):
    try:
        proc = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=5)
    except subprocess.TimeoutExpired:
        return {"case": case, "timeout": True, "pass": False}
    try:
        actual = json.loads(proc.stdout)
    except json.JSONDecodeError:
        actual = {}
    errors = check_result(platform, case, actual)
    return {"case": case, "exit": proc.returncode, "actual": actual, "errors": errors,
            "stderr": proc.stderr, "pass": proc.returncode == 0 and not errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT,
                        help="checkout supplying port/platform/common/d3d9hooks.cpp (default: this checkout)")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"), help="host C++ compiler command")
    args = parser.parse_args()
    parent = ROOT / "local-data/port/fullscreen-window-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"source_root": str(args.source_root.resolve()), "platforms": {}}
    passed = False
    try:
        raw = (args.source_root / SOURCE).read_bytes()
        receipt["source_sha256"] = digest(raw)
        (out / "d3d9hooks.cpp").write_bytes(raw)
        function = extract_function(raw.decode("utf-8"))
        receipt["function_sha256"] = digest(function.encode())
        driver = out / "harness.cpp"
        driver.write_text(HARNESS.replace("@FUNCTION@", function))
        receipt["driver_sha256"] = digest(driver.read_bytes())
        cases = {
            "windows": ("device-preferred", "focus-fallback", "width-only", "height-only", "same-size",
                        "null-parameters", "windowed", "zero-width", "zero-height", "unowned-device", "unowned-focus"),
            "linux": ("device-preferred", "focus-fallback", "null-parameters", "windowed", "offscreen",
                      "no-driver", "exclusive-opt-out", "unowned-device"),
        }
        for platform, names in cases.items():
            binary = out / platform
            command = [*shlex.split(args.cxx), "-std=c++11", "-O0", "-Wall", "-Wextra"]
            if platform == "windows":
                command.append("-D_WIN32")
            command += [str(driver), "-o", str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30,
                                    env=dict(os.environ, TMPDIR=str(out)))
            record = {"compile": command, "compile_exit": result.returncode,
                      "compiler_output": result.stdout + result.stderr, "cases": []}
            receipt["platforms"][platform] = record
            if result.returncode:
                raise RuntimeError(f"{platform} harness did not compile")
            for case in names:
                observation = run_case(binary, platform, case)
                record["cases"].append(observation)
                print(f"{'PASS' if observation['pass'] else 'FAIL'} {platform}: {case}")
        passed = all(case["pass"] for record in receipt["platforms"].values() for case in record["cases"])
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
        print(f"FAIL {error}")
    receipt["pass"] = passed
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
