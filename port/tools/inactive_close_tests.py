#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual shell/platform quit path with bounded host message/device boundaries.

Needs Python, patch and a host C++ compiler; no game, retail inputs, display, audio or GPU. Snapshots the source,
applies the port patch queue, and extracts complete HandlingSystemStuff, Process and TriggerQuit definitions plus
the original WantsToQuit inline. No game-loop algorithm is reimplemented. Fake GetMessage throws when its queue
would block; a separate poll budget and process timeout bound other loops. Dispatch delivers TriggerQuit before
exposing the fake close-delivered latch. This checks shell control flow, not native window dispatch or shutdown.

Default admission expects a CE-only fix. --expect baseline documents the current defect without calling it fixed.
--patch adds an external candidate patch after the normal queue; --source-root selects another source checkout.
--shell-static NAME copies a declared boolean latch and its preprocessor guards from the patched shell source,
including C++ static zero-initialization when no explicit initializer is present.
The unchanged source is always exercised as a negative control. Compiler/runtime output stays under local-data/.
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
FILES = ("ltshell.cpp", "ltshell.h", "PCPlatform.cpp")
CASES = ("inactive-close", "inactive-unrelated-close", "inactive-close-unrelated", "active-close",
         "active-unrelated", "active-idle", "inactive-unrelated", "inactive-empty", "inactive-restore")

HARNESS = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
// Select the PC branch explicitly, as common.h does; do not admit undefined TARGET == undefined PC by accident.
#define PC 1
#define PS2 3
#define TARGET PC
using BOOL = int;
using FLOAT = float;
using HRESULT = int32_t;
using HWND = void *;
const int TRUE = 1, FALSE = 0, S_OK = 0;
const unsigned WM_CLOSE = 0x10, WM_QUIT = 0x12, WM_USER = 0x400, RESTORE = 0x500;
const unsigned PM_REMOVE = 1, TIMER_GETAPPTIME = 1, TIMER_GETELAPSEDTIME = 2;
const unsigned MAX_JOYPADS = 4;
const HRESULT D3DERR_DEVICELOST = static_cast<HRESULT>(0x88760868u);
const HRESULT D3DERR_DEVICENOTRESET = static_cast<HRESULT>(0x88760869u);
#define FAILED(value) ((value) < 0)
enum EQuitType { QT_NONE, QT_QUIT_TO_FRONTEND, QT_QUIT_TO_SYSTEM };
struct MSG { unsigned message = 0; };
struct WouldBlock {};
struct PollLimit {};
struct UnexpectedCall {};
struct Device {
    unsigned calls = 0;
    HRESULT TestCooperativeLevel() { ++calls; return S_OK; }
};
struct DisplayMode { int Format = 0; };
struct D3DAdapterInfo { DisplayMode d3ddmDesktop; };
struct D3D {
    void GetAdapterDisplayMode(unsigned, DisplayMode *) { throw UnexpectedCall{}; }
};
struct Parameters { int BackBufferFormat = 0; };
struct QuitOwner {
    int value = 0;
    unsigned calls = 0;
    void SetQuit(int next) { value = next; ++calls; }
} GAME, FRONTEND;
static unsigned peek_calls, get_calls, dispatch_calls, translate_calls, accelerator_calls, close_callbacks;
static unsigned poll_count, device_timer_calls, sleep_calls, first_joy_calls, later_joy_calls;
static bool close_delivered, callback_saw_undelivered = true;
static std::deque<unsigned> queue;
static std::vector<unsigned> messages;
static std::vector<unsigned> pads;
static unsigned sKeysRead = 0x1234u, spKeysRead = 0x5678u;
BOOL PeekMessage(MSG *, HWND, unsigned, unsigned, unsigned);
BOOL GetMessage(MSG *, HWND, unsigned, unsigned);
int TranslateAccelerator(HWND, void *, MSG *);
void TranslateMessage(MSG *);
void DispatchMessage(MSG *);
void SendMessage(HWND, unsigned, unsigned, unsigned);
void Sleep(unsigned) { ++sleep_calls; throw UnexpectedCall{}; }
FLOAT DXUtil_Timer(unsigned request) {
    ++device_timer_calls;
    if (request == TIMER_GETAPPTIME) return 7.0f;
    if (request == TIMER_GETELAPSEDTIME) return 0.25f;
    throw UnexpectedCall{};
}
@CE_LAYER@
// ltshell.cpp's own state the patched functions read (CE quit request) and a Windows type they use.
static bool sPortQuitRequested;
typedef unsigned UINT;
struct PCLTShell {
    BOOL m_bActive = TRUE, m_bReady = TRUE, m_bWindowed = TRUE;
    BOOL m_bFrameMoving = TRUE, m_bSingleStep = TRUE;
    FLOAT m_fTime = -1.0f, m_fElapsedTime = -2.0f;
    MSG mMsg;
    HWND m_hWnd = reinterpret_cast<HWND>(1);
    void *mhAccel = nullptr;
    Device *m_pd3dDevice = nullptr;
    D3D *m_pD3D = nullptr;
    D3DAdapterInfo m_Adapters[1];
    unsigned m_dwAdapter = 0;
    Parameters m_d3dpp;
    BOOL HandlingSystemStuff(bool first);
    void TriggerQuit();
    @WANTS_TO_QUIT@
    HWND GetHWnd() { return m_hWnd; }
    HRESULT Reset3DEnvironment(BOOL, BOOL) { throw UnexpectedCall{}; }
    void UpdateJoystick(unsigned pad, bool first) {
        pads.push_back(pad);
        if (first) ++first_joy_calls; else ++later_joy_calls;
    }
} LT;
struct CPCPlatform { EQuitType Process(); };
@SHELL_STATICS@
@TRIGGER_QUIT@
@HANDLING@
@PROCESS@
static void budget() { if (++poll_count > 12) throw PollLimit{}; }
BOOL PeekMessage(MSG *msg, HWND window, unsigned low, unsigned high, unsigned flags) {
    budget(); ++peek_calls;
    if (window || low || high || flags != PM_REMOVE) throw UnexpectedCall{};
    if (queue.empty()) return FALSE;
    msg->message = queue.front(); queue.pop_front(); return TRUE;
}
BOOL GetMessage(MSG *msg, HWND window, unsigned low, unsigned high) {
    budget(); ++get_calls;
    if (window || low || high) throw UnexpectedCall{};
    if (queue.empty()) throw WouldBlock{};
    msg->message = queue.front(); queue.pop_front(); return msg->message != WM_QUIT;
}
int TranslateAccelerator(HWND window, void *accelerator, MSG *) {
    ++accelerator_calls;
    if (window != LT.m_hWnd || accelerator) throw UnexpectedCall{};
    return 0;
}
void TranslateMessage(MSG *) { ++translate_calls; }
void DispatchMessage(MSG *msg) {
    ++dispatch_calls; messages.push_back(msg->message);
    if (msg->message == WM_CLOSE) {
        callback_saw_undelivered = callback_saw_undelivered && !close_delivered;
        ++close_callbacks;
        LT.TriggerQuit();
        close_delivered = true;
    } else if (msg->message == RESTORE) LT.m_bActive = TRUE;
    else if (msg->message != WM_USER) throw UnexpectedCall{};
}
void SendMessage(HWND, unsigned, unsigned, unsigned) { throw UnexpectedCall{}; }
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    setenv("BEA_PORT_CE_LAYER", argv[2], 1);
    const char *name = argv[1];
    LT.m_bActive = std::strncmp(name, "inactive-", 9) != 0;
    if (!std::strcmp(name, "inactive-close") || !std::strcmp(name, "active-close")) queue = {WM_CLOSE};
    else if (!std::strcmp(name, "inactive-unrelated-close")) queue = {WM_USER, WM_CLOSE};
    else if (!std::strcmp(name, "inactive-close-unrelated")) queue = {WM_CLOSE, WM_USER};
    else if (!std::strcmp(name, "inactive-unrelated") || !std::strcmp(name, "active-unrelated")) queue = {WM_USER};
    else if (!std::strcmp(name, "inactive-restore")) queue = {RESTORE};
    else if (std::strcmp(name, "active-idle") && std::strcmp(name, "inactive-empty")) return 2;
    Device device; D3D d3d; LT.m_pd3dDevice = &device; LT.m_pD3D = &d3d;
    int quit = -99; bool returned = false; const char *stop = "returned";
    try { quit = CPCPlatform{}.Process(); returned = true; }
    catch (const WouldBlock &) { stop = "would-block"; }
    catch (const PollLimit &) { stop = "poll-budget"; }
    catch (const UnexpectedCall &) { stop = "unexpected-call"; }
    bool pad_order = true;
    for (unsigned i = 0; i < pads.size(); ++i) pad_order = pad_order && pads[i] == i % MAX_JOYPADS;
    std::printf("{\"returned\":%s,\"stop\":\"%s\",\"quit\":%d,\"game_quit\":%d,\"frontend_quit\":%d,"
                "\"game_quit_calls\":%u,\"frontend_quit_calls\":%u,\"wants_quit\":%s,\"message\":%u,"
                "\"active\":%s,\"ready\":%s,\"peek_calls\":%u,\"get_calls\":%u,\"dispatch_calls\":%u,"
                "\"translate_calls\":%u,\"accelerator_calls\":%u,\"close_callbacks\":%u,"
                "\"close_delivered\":%s,\"callback_saw_undelivered\":%s,\"queued\":%zu,"
                "\"queue_front\":%u,\"device_calls\":%u,\"timer_calls\":%u,\"sleep_calls\":%u,"
                "\"first_joy_calls\":%u,\"later_joy_calls\":%u,\"pad_order\":%s,\"keys_read\":%u,"
                "\"time\":%.2f,\"elapsed\":%.2f,\"single_step\":%s}\n",
                returned ? "true" : "false", stop, quit, GAME.value, FRONTEND.value, GAME.calls, FRONTEND.calls,
                LT.WantsToQuit() ? "true" : "false", LT.mMsg.message, LT.m_bActive ? "true" : "false",
                LT.m_bReady ? "true" : "false", peek_calls, get_calls, dispatch_calls, translate_calls,
                accelerator_calls, close_callbacks, close_delivered ? "true" : "false",
                callback_saw_undelivered ? "true" : "false", queue.size(), queue.empty() ? 0 : queue.front(),
                device.calls, device_timer_calls, sleep_calls, first_joy_calls, later_joy_calls,
                pad_order ? "true" : "false", spKeysRead, LT.m_fTime, LT.m_fElapsedTime,
                LT.m_bSingleStep ? "true" : "false");
    return !std::strcmp(stop, "would-block") ? 77 : returned ? 0 : 78;
}
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def definition(source, signature):
    # These production methods end at an unindented closing brace. Preserve their whole text, including comments
    # and disabled branches; a changed/missing/duplicate definition is an extraction error, never an empty pass.
    matches = list(re.finditer(r"^" + signature + r"[^\n]*\n\{\n.*?^\}[ \t]*", source, re.MULTILINE | re.DOTALL))
    if len(matches) != 1:
        raise RuntimeError(f"expected one complete definition: {signature}")
    return matches[0].group() + "\n"


def shell_statics(source, names, required):
    result = []
    for name in names:
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise RuntimeError(f"invalid static identifier: {name}")
        matches = list(re.finditer(r"^static[ \t]+(?:bool|BOOL)[ \t]+" + re.escape(name)
                                  + r"(?:[ \t]*=[ \t]*(?:false|true|FALSE|TRUE|0|1))?[ \t]*;[ \t]*(?://[^\n]*)?$",
                                  source, re.MULTILINE))
        if not matches and not required:
            continue
        if len(matches) != 1:
            raise RuntimeError(f"expected one single-line shell boolean static: {name}")
        # Retain conditional declaration scope, including else/elif. Omit unrelated declarations, never the latch's
        # initializer or branch conditions. Only explicitly named boolean globals are admitted to this harness.
        guards = []
        for line in source[:matches[0].start()].splitlines():
            directive = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b", line)
            if not directive:
                continue
            kind = directive[1]
            if kind in ("if", "ifdef", "ifndef"):
                guards.append([line])
            elif kind == "endif":
                if not guards:
                    raise RuntimeError("unmatched preprocessor end before shell static")
                guards.pop()
            elif not guards:
                raise RuntimeError("unmatched preprocessor branch before shell static")
            else:
                guards[-1].append(line)
        result.extend(line for guard in guards for line in guard)
        result.append(matches[0].group())
        result.extend("#endif" for _ in guards)
    return "\n".join(result) + "\n"


def functions(sources, ce_source, static_names, required):
    inline = re.findall(r"\bBOOL\s+WantsToQuit\(\)\s*\{[^\n]*?\}", sources["ltshell.h"])
    if len(inline) != 1:
        raise RuntimeError("expected one complete WantsToQuit inline")
    return {
        "HANDLING": definition(sources["ltshell.cpp"], r"BOOL PCLTShell::HandlingSystemStuff\(bool first\)"),
        "TRIGGER_QUIT": definition(sources["ltshell.cpp"], r"void PCLTShell::TriggerQuit\(\)"),
        "PROCESS": definition(sources["PCPlatform.cpp"], r"EQuitType\s+CPCPlatform::Process\(\)"),
        "WANTS_TO_QUIT": inline[0],
        "CE_LAYER": definition(ce_source, r"bool PortCeLayer\(\)"),
        "SHELL_STATICS": shell_statics(sources["ltshell.cpp"], static_names, required),
    }


def expected(case, fixed):
    inactive = case.startswith("inactive-")
    closes = "close" in case
    effective_fix = fixed and closes
    restored = case == "inactive-restore"
    dispatched = 0 if case in ("active-idle", "inactive-empty") else 1
    if case in ("inactive-unrelated-close", "inactive-close-unrelated"):
        dispatched = 1 if effective_fix and case == "inactive-close-unrelated" else 2
    blocked = inactive and not restored and not effective_fix
    idle = not blocked and not effective_fix
    after_close = case == "inactive-close-unrelated" and not effective_fix
    last = 0 if not dispatched else 0x500 if restored else 0x400 if not closes or after_close else 0x10
    queued = int(effective_fix and case == "inactive-close-unrelated")
    completed_pumps = dispatched + int(idle)
    return {
        "returned": not blocked, "stop": "would-block" if blocked else "returned", "quit": -99 if blocked else 0,
        "game_quit": 1 if closes else 0, "frontend_quit": -1 if closes else 0,
        "game_quit_calls": int(closes), "frontend_quit_calls": int(closes), "wants_quit": False,
        "message": last, "active": not inactive or restored, "ready": True,
        "peek_calls": (dispatched + int(idle)) if not inactive else int(restored),
        "get_calls": (dispatched + int(blocked)) if inactive else 0,
        "dispatch_calls": dispatched, "translate_calls": dispatched, "accelerator_calls": dispatched,
        "close_callbacks": int(closes), "close_delivered": closes, "callback_saw_undelivered": True,
        "queued": queued, "queue_front": 0x400 if queued else 0,
        "device_calls": int(idle), "timer_calls": 2 * int(idle), "sleep_calls": 0,
        "first_joy_calls": 4 if completed_pumps else 0, "later_joy_calls": 4 * max(0, completed_pumps - 1),
        "pad_order": True, "keys_read": 0x1234 if completed_pumps else 0x5678,
        "time": 7.0 if idle else -1.0, "elapsed": 0.25 if idle else -2.0, "single_step": not idle,
    }


def run_case(binary, case, ce, fixed):
    want = expected(case, fixed)
    result = {"case": case, "ce_layer": ce, "expected": want}
    try:
        proc = subprocess.run([str(binary), case, "1" if ce else "0"], capture_output=True, text=True, timeout=5)
        try:
            actual = json.loads(proc.stdout)
        except json.JSONDecodeError:
            actual = {}
        code = 77 if want["stop"] == "would-block" else 0
        result.update(exit=proc.returncode, actual=actual, stderr=proc.stderr,
                      passed=proc.returncode == code and actual == want)
    except subprocess.TimeoutExpired:
        result.update(timeout=True, passed=False)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--patch", type=Path, action="append", default=[], help="additional candidate patch, in order")
    parser.add_argument("--shell-static", action="append", default=[], help="exact candidate TU boolean latch name")
    parser.add_argument("--expect", choices=("fixed", "baseline"), default="fixed")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    args = parser.parse_args()
    parent = ROOT / "local-data/port/inactive-close-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"source_root": str(args.source_root.resolve()), "expect": args.expect, "patches": [],
               "shell_static_names": args.shell_static,
               "builds": {}, "cases": [], "controls": [], "limits": [
                   "Actual extracted shell/platform/quit/admission code; fake OS, device, frontend/game setters and joystick.",
                   "OS dispatch is modeled; any explicitly selected TU latch and its function writes remain actual source.",
                   "No native window/device, real inactivity, frontend return loop, cleanup or process-exit acceptance."]}
    passed = False
    try:
        raw = {name: (args.source_root / "src" / name).read_bytes() for name in FILES}
        receipt["source_sha256"] = {name: digest(data) for name, data in raw.items()}
        for name, data in raw.items():
            (out / name).write_bytes(data)
            (out / ("original-" + name)).write_bytes(data)
        patches = [*sorted((args.source_root / "port/patches").glob("*.patch")), *args.patch]
        for patch in patches:
            data = patch.read_bytes()
            sections = [s for s in re.split(br"(?=^--- a/)", data, flags=re.MULTILINE)
                        if any(s.startswith(f"--- a/{name}\n+++ b/{name}\n".encode()) for name in FILES)]
            if not sections:
                if patch in args.patch:
                    raise RuntimeError(f"extra patch has no applicable source sections: {patch}")
                continue
            proc = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(out), "-r", "-"],
                                  input=b"".join(sections), capture_output=True, timeout=10)
            receipt["patches"].append({"name": patch.name, "sha256": digest(data), "exit": proc.returncode,
                                       "output": (proc.stdout + proc.stderr).decode(errors="replace")})
            if proc.returncode:
                raise RuntimeError(f"patch did not apply cleanly: {patch.name}")
        patched = {name: (out / name).read_bytes() for name in FILES}
        receipt["patched_sha256"] = {name: digest(data) for name, data in patched.items()}
        ce = (args.source_root / "port/platform/common/ce.cpp").read_bytes()
        receipt["ce_source_sha256"] = digest(ce)
        binaries = {}
        for label, sources, public in (("original-public", raw, True), ("patched-public", patched, True),
                                       ("patched-private", patched, False)):
            bodies = functions({name: data.decode("latin-1") for name, data in sources.items()}, ce.decode("latin-1"),
                               args.shell_static, label != "original-public")
            driver = HARNESS
            for name, body in bodies.items():
                driver = driver.replace("@" + name + "@", body)
            path = out / (label + ".cpp")
            path.write_text(driver)
            binary = out / label
            command = [*shlex.split(args.cxx), "-std=c++11", "-O0", "-Wall", "-Wextra",
                       *(['-DCE_VERSION="test"'] if public else []), str(path), "-o", str(binary)]
            proc = subprocess.run(command, capture_output=True, text=True, timeout=30,
                                  env=dict(os.environ, TMPDIR=str(out)))
            receipt["builds"][label] = {"functions_sha256": {n: digest(b.encode("latin-1")) for n, b in bodies.items()},
                                         "driver_sha256": digest(path.read_bytes()), "command": command,
                                         "exit": proc.returncode, "output": proc.stdout + proc.stderr}
            if proc.returncode:
                raise RuntimeError(f"harness did not compile: {label}")
            binaries[label] = binary
        for label, ce_enabled in (("patched-public", True), ("patched-public", False), ("patched-private", True)):
            for case in CASES:
                fixed = args.expect == "fixed" and label == "patched-public" and ce_enabled
                result = run_case(binaries[label], case, ce_enabled, fixed)
                result["variant"] = label
                receipt["cases"].append(result)
                print(f"{'PASS' if result['passed'] else 'FAIL'} {label} CE={int(ce_enabled)} {case}")
        for case in CASES:
            result = run_case(binaries["original-public"], case, True, False)
            receipt["controls"].append(result)
            print(f"{'PASS' if result['passed'] else 'FAIL'} unchanged control {case}")
        passed = all(r["passed"] for r in [*receipt["cases"], *receipt["controls"]])
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
        print(f"FAIL {error}")
    receipt["pass"] = passed
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
