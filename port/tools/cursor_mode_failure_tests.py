#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real D3D mode hooks with authored SDL and D3D responses, without a game or a display.

This compiles actual source functions and their real CE-layer decision. Both CreateDevice and Reset are exercised
on Linux and Windows preprocessor branches, with private, replay, record and CE modes, owned and foreign windows,
successful and failed D3D calls, and disappearing/replaced window owners. These are scripted APIs: this is not a
native D3D runtime test and does not show what a real player sees. Scratch and a result are retained in local-data.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/common/d3d9hooks.cpp"
LAYER = ROOT / "port/platform/common/ce.cpp"
HARNESS = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
using UINT = unsigned;
using DWORD = unsigned;
using HRESULT = std::int32_t;
using HWND = void *;
using D3DDEVTYPE = int;
using SDL_WindowID = std::uint32_t;
#define TRUE 1
#define STDMETHODCALLTYPE
#define PORT_LOG_PREFIX "[cursor-test] "
#define PORT_SESSION_GAME 0
#define PORT_SESSION_VIDEO 1
#define SUCCEEDED(hr) ((hr) >= 0)
#define FAILED(hr) ((hr) < 0)
#define D3DCREATE_FPU_PRESERVE 1
#define HIWORD(x) ((unsigned)(x) >> 16)
#define LOWORD(x) ((unsigned)(x) & 65535)
struct SDL_Window { int width = 640, height = 480; SDL_WindowID id = 1; };
struct D3DPRESENT_PARAMETERS {
    UINT BackBufferWidth, BackBufferHeight, BackBufferFormat;
    HWND hDeviceWindow;
    int Windowed;
    UINT FullScreen_RefreshRateInHz, MultiSampleType, MultiSampleQuality, PresentationInterval;
};
struct D3DDEVICE_CREATION_PARAMETERS { HWND hFocusWindow; };
struct D3DADAPTER_IDENTIFIER9 {
    char Description[513]; unsigned VendorId, DeviceId;
    struct { unsigned HighPart, LowPart; } DriverVersion;
};
SDL_Window first, second, replacement;
int foreign_window;
bool owner_alive = true, owner_replaced = false, disappear_on_call = false, replace_on_call = false;
bool mutate_on_call = false, reuse_on_call = false, change_handle_on_call = false, handle_changed = false;
bool zero_id_initial = false, zero_id_after = false, request_complete = false;
bool cursor_visible = true;
unsigned shows = 0, hides = 0, queries = 0, id_queries = 0;
HRESULT expected_result = -1;
UINT gBoxW, gBoxH, gBackW, gBackH;
unsigned gResets;
SDL_Window *gBoxWindow;
SDL_Window *PortSdlWindow(HWND h) {
    if (!owner_alive || handle_changed || (h != &first && h != &second)) return nullptr;
    if (owner_replaced) return &replacement;
    return h == &first ? &first : &second;
}
SDL_WindowID SDL_GetWindowID(SDL_Window *w) {
    ++id_queries;
    if (zero_id_initial || (zero_id_after && request_complete)) return 0;
    return w ? w->id : 0;
}
bool SDL_GetWindowSizeInPixels(SDL_Window *w, int *x, int *y) { *x=w->width; *y=w->height; return true; }
bool SDL_SetWindowSize(SDL_Window *w, int x, int y) { w->width=x; w->height=y; return true; }
bool SDL_SyncWindow(SDL_Window *) { return true; }
bool SDL_SetWindowFullscreenMode(SDL_Window *, void *) { return true; }
bool SDL_SetWindowFullscreen(SDL_Window *, bool) { return true; }
const char *SDL_GetCurrentVideoDriver() { return "x11"; }
const char *SDL_GetError() { return "scripted failure"; }
int SDL_GetVersion() { return 3002000; }
bool SDL_CursorVisible() { ++queries; return cursor_visible; }
bool SDL_ShowCursor() { ++shows; if (request_complete && std::getenv("CURSOR_TEST_FAIL_RESTORE")) return false; cursor_visible=true; return true; }
bool SDL_HideCursor() { ++hides; if (request_complete && std::getenv("CURSOR_TEST_FAIL_RESTORE")) return false; cursor_visible=false; return true; }
struct IDirect3DDevice9 {
    HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *p) { p->hFocusWindow=&first; return 0; }
};
struct IDirect3D9 { HRESULT GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9 *) { return -1; } };
using ResetFn = HRESULT (*)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
using CreateDeviceFn = HRESULT (*)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
using PresentFn = void (*)();
IDirect3DDevice9 device, *gDevice;
void on_request(D3DPRESENT_PARAMETERS *pp) {
    request_complete=true;
    if (disappear_on_call) owner_alive=false;
    if (replace_on_call) owner_replaced=true;
    if (reuse_on_call) { ++first.id; ++second.id; }
    if (change_handle_on_call) handle_changed=true;
    if (mutate_on_call && pp) pp->hDeviceWindow=&foreign_window;
}
HRESULT fakeReset(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *pp) { on_request(pp); return expected_result; }
HRESULT fakeCreate(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out) {
    on_request(pp); if (expected_result >= 0) *out=&device; return expected_result;
}
ResetFn gReset=fakeReset;
CreateDeviceFn gCreateDevice=fakeCreate;
PresentFn gPresent;
void Present() {}
#define DEFINE_HOOK(name) using name##Fn = void (*)(); name##Fn g##name; void name() {}
DEFINE_HOOK(SetRS) DEFINE_HOOK(SetTex) DEFINE_HOOK(SetTSS) DEFINE_HOOK(SetSampler)
DEFINE_HOOK(Draw) DEFINE_HOOK(DrawIdx) DEFINE_HOOK(SetVS) DEFINE_HOOK(SetVSC)
DEFINE_HOOK(SetStream) DEFINE_HOOK(SetTransform) DEFINE_HOOK(SetPS) DEFINE_HOOK(SetPSC)
DEFINE_HOOK(SetRT) DEFINE_HOOK(SetDS) DEFINE_HOOK(SetMat) DEFINE_HOOK(SetLight) DEFINE_HOOK(LightEnable)
const int kDeviceSlots=130;
void *hook_table[kDeviceSlots];
void **HookTable(IDirect3DDevice9 *, int) { return hook_table; }
void SetSlot(void **, int, void *) {}
void NoteDeviceTable(void **) {}
FILE *gLog;
int gLogFrame;
char gLogPath[128];
void PortWideBackBuffer(UINT, UINT) {}
bool PortWideModes() { return true; }
bool PortSessionEnabled() { return false; }
void PortSessionLog(int, bool, const char *, ...) {}
unsigned short ControlWord() { return 0x37f; }
void SetControlWord(unsigned short) {}
@LAYER@
#ifdef CE_VERSION
@CURSOR_GUARD@
#endif
@DESKTOP@
@RESET@
@CREATE@
int main(int argc, char **argv) {
    if (argc != 7) return 2;
    bool create = !std::strcmp(argv[1], "create");
    bool enter = !std::strcmp(argv[2], "enter");
    bool success = !std::strcmp(argv[3], "success");
    const char *owner = argv[4];
    cursor_visible = !std::strcmp(argv[5], "visible");
    if (!std::strcmp(owner, "disappear")) disappear_on_call=true;
    if (!std::strcmp(owner, "replace")) replace_on_call=true;
    if (!std::strcmp(owner, "mutate")) mutate_on_call=true;
    if (!std::strcmp(owner, "same-address-new-id")) reuse_on_call=true;
    if (!std::strcmp(owner, "changed-handle")) change_handle_on_call=true;
    if (!std::strcmp(owner, "zero-id-initial")) zero_id_initial=true;
    if (!std::strcmp(owner, "zero-id-after")) zero_id_after=true;
    bool no_params = !std::strcmp(owner, "null");
    HWND focus = &first;
    D3DPRESENT_PARAMETERS pp{1280, 800, 22, &second, enter ? 0 : TRUE, 0, 0, 0, 0};
    if (!std::strcmp(owner, "focus")) pp.hDeviceWindow=nullptr;
    if (!std::strcmp(owner, "foreign-device")) pp.hDeviceWindow=&foreign_window;
    if (!std::strcmp(owner, "foreign-focus")) { pp.hDeviceWindow=nullptr; focus=&foreign_window; }
    // Reset gets focus from GetCreationParameters, so only CreateDevice can receive an unrelated focus here.
    if (!create && !std::strcmp(owner, "foreign-focus")) return 3;
    expected_result = success ? 0 : -1;
    IDirect3DDevice9 *out=nullptr;
    IDirect3D9 d3d;
    HRESULT hr = create ? CreateDevice(&d3d, 0, 0, focus, D3DCREATE_FPU_PRESERVE, no_params ? nullptr : &pp, &out)
                        : Reset(&device, no_params ? nullptr : &pp);
    std::printf("{\"success\":%s,\"visible\":%s,\"shows\":%u,\"hides\":%u,\"queries\":%u,\"id_queries\":%u}\n",
        SUCCEEDED(hr) ? "true" : "false", cursor_visible ? "true" : "false", shows, hides, queries, id_queries);
    return 0;
}
'''


def extract(text: str, pattern: str) -> str:
    matches = re.findall(pattern, text, re.MULTILINE | re.DOTALL)
    if len(matches) != 1:
        raise ValueError("production function or guard did not have exactly one match")
    return matches[0]


def generate(source: str, layer: str, mutant: str | None = None) -> str:
    guard = extract(source, r"^struct CeCursorBeforeModeChange\n\{\n.*?^\};")
    desktop = extract(source, r"^void DesktopFullscreen\(HWND focus, D3DPRESENT_PARAMETERS \*pp\)\n\{\n.*?^\}")
    reset = extract(source, r"^HRESULT STDMETHODCALLTYPE Reset\(IDirect3DDevice9 \*device, D3DPRESENT_PARAMETERS \*pp\)\n\{\n.*?^\}")
    create = extract(source, r"^HRESULT STDMETHODCALLTYPE CreateDevice\(IDirect3D9 \*d3d,.*?^\}")
    if mutant == "reset":
        assert reset.count("cursorBefore.RestoreIfFailed(hr);") == 1
        reset = reset.replace("cursorBefore.RestoreIfFailed(hr);", "/* deliberate test mutant */")
    if mutant == "create":
        assert create.count("cursorBefore.RestoreIfFailed(hr);") == 1
        create = create.replace("cursorBefore.RestoreIfFailed(hr);", "/* deliberate test mutant */")
    if mutant == "identity":
        assert guard.count("SDL_GetWindowID(window) != windowId ||") == 1
        guard = guard.replace("SDL_GetWindowID(window) != windowId ||", "/* deliberate test mutant */")
    if mutant == "zero-id":
        assert guard.count("!windowId ||") == 1
        guard = guard.replace("!windowId ||", "/* deliberate test mutant */")
    result = HARNESS
    for key, value in (("@LAYER@", extract(layer, r"^bool PortCeLayer\(\)\n\{\n.*?^\}")),
                       ("@CURSOR_GUARD@", guard), ("@DESKTOP@", desktop), ("@RESET@", reset), ("@CREATE@", create)):
        result = result.replace(key, value)
    return result


def main() -> int:
    source_bytes = SOURCE.read_bytes()
    source, layer = source_bytes.decode(), LAYER.read_text()
    parent = ROOT / "local-data/port/cursor-mode-failure-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    cases = []
    baseline = ("BEA_PORT_CE_LAYER", "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
                "BEA_PORT_EXCLUSIVE", "BEA_PORT_D3DLOG", "CURSOR_TEST_FAIL_RESTORE")
    profiles = {"normal": {}, "tape": {"BEA_PORT_TAPE": "1"}, "record": {"BEA_PORT_RECORD": "1"},
                "clock": {"BEA_PORT_CLOCK": "1"}, "probes": {"BEA_PORT_PROBES": "1"},
                "disabled": {"BEA_PORT_CE_LAYER": "0"},
                "forced-tape": {"BEA_PORT_TAPE": "1", "BEA_PORT_CE_LAYER": "1"},
                "forced-record": {"BEA_PORT_RECORD": "1", "BEA_PORT_CE_LAYER": "1"}}
    mutants_caught = set()
    restore_errors_checked = 0
    for mutant in (None, "reset", "create", "identity", "zero-id"):
        cpp = out / f"{mutant or 'actual'}.cpp"
        cpp.write_text(generate(source, layer, mutant))
        for platform in ("linux", "windows"):
            for build in ("ce", "private"):
                # Mutants only need the CE variant; private compiles and runs in the unmodified round.
                if mutant and build == "private":
                    continue
                executable = out / f"{mutant or 'actual'}-{platform}-{build}"
                command = ["c++", "-std=c++17", "-O0", "-Wall", "-Wextra"]
                if platform == "windows": command.append("-D_WIN32")
                if build == "ce": command.append('-DCE_VERSION="test"')
                compiled = subprocess.run(command + [str(cpp), "-o", str(executable)], timeout=30,
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                if compiled.returncode:
                    raise RuntimeError(f"{platform} {build} {mutant or 'actual'} compile failed:\n{compiled.stderr}")
                for profile, overrides in profiles.items():
                    ce_on = build == "ce" and profile in ("normal", "forced-tape", "forced-record")
                    env = os.environ.copy()
                    for key in baseline: env.pop(key, None)
                    env.update(overrides)
                    for operation in ("reset", "create"):
                        if mutant in ("reset", "create") and operation != mutant: continue
                        for mode in ("enter", "leave"):
                            for outcome in ("success", "failure"):
                                for owner in ("device", "focus", "foreign-device", "foreign-focus", "null", "disappear", "replace", "mutate",
                                              "same-address-new-id", "changed-handle", "zero-id-initial", "zero-id-after"):
                                    if operation == "reset" and owner == "foreign-focus": continue
                                    for initial in (False, True):
                                        args = [str(executable), operation, mode, outcome, owner,
                                                "visible" if initial else "hidden", "reserved"]
                                        run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=5, check=True)
                                        got = json.loads(run.stdout)
                                        owned = owner in ("device", "focus", "disappear", "replace", "mutate", "same-address-new-id",
                                                          "changed-handle", "zero-id-initial", "zero-id-after")
                                        expected = initial
                                        if owned and mode == "enter" and (platform == "linux" or ce_on): expected = False
                                        if owned and mode == "leave" and ce_on: expected = True
                                        if outcome == "failure" and ce_on and owned and owner not in ("disappear", "replace", "same-address-new-id",
                                                                                                      "changed-handle", "zero-id-initial", "zero-id-after"):
                                            expected = initial
                                        valid = got["success"] == (outcome == "success") and got["visible"] == expected
                                        if not ce_on and (got["queries"] != 0 or got["id_queries"] != 0): valid = False
                                        if ce_on and owner == "zero-id-initial" and got["queries"] != 0: valid = False
                                        if mutant:
                                            if not valid: mutants_caught.add(mutant)
                                        elif not valid:
                                            raise AssertionError(f"{platform} {build} {profile} {args[1:6]}: {got}; expected visible={expected}")
                                        else:
                                            cases.append((platform, build, profile, operation, mode, outcome, owner, initial))
                if mutant is None and build == "ce":
                    env = os.environ.copy()
                    for key in baseline: env.pop(key, None)
                    env["CURSOR_TEST_FAIL_RESTORE"] = "1"
                    for operation in ("reset", "create"):
                        for mode in ("enter", "leave"):
                            initial = mode == "enter"
                            args = [str(executable), operation, mode, "failure", "device",
                                    "visible" if initial else "hidden", "reserved"]
                            run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=5, check=True)
                            got = json.loads(run.stdout)
                            if got["success"] or got["visible"] == initial or "failed to restore cursor after rejected display mode" not in run.stderr:
                                raise AssertionError(f"SDL restore failure was not surfaced: {platform} {args[1:]} {got} {run.stderr}")
                            restore_errors_checked += 1
    if mutants_caught != {"reset", "create", "identity", "zero-id"}:
        raise AssertionError(f"missing causality controls: {mutants_caught}")
    result = {"source_sha256": hashlib.sha256(source_bytes).hexdigest(), "actual_cases": len(cases),
              "mutants_caught": sorted(mutants_caught), "restore_errors_checked": restore_errors_checked,
              "scope": "scripted source hooks; no real SDL/D3D or game"}
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"pass": True, "result": str(out / "result.json"), **result}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
