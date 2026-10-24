#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check DisplayScreen's device-loss boundary using its actual source and port patches.

Requires Python, patch and a host C++ compiler; no game data, port dependencies, display or GPU. The test snapshots
ltshell.cpp, applies every patch section for that file in queue order, and extracts DisplayScreen unchanged into a
scripted-device harness. It also runs the unpatched source as a negative control. A polling budget and subprocess
timeout bound failures. This checks return to the message loop, not real-driver reset or window-focus behavior.
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

HARNESS = r"""
#include <cstdint>
#include <cstdio>
#include <cstring>
using HRESULT = int32_t;
const HRESULT D3D_OK = 0;
const HRESULT D3DERR_DEVICELOST = static_cast<HRESULT>(0x88760868u);
const HRESULT D3DERR_DEVICENOTRESET = static_cast<HRESULT>(0x88760869u);
const HRESULT D3DERR_DRIVERINTERNALERROR = static_cast<HRESULT>(0x88760827u);
#define SUCCEEDED(value) ((value) >= 0)
#define TRUE 1
struct PollLimit {};
static unsigned sleep_calls;
void Sleep(unsigned) { if (++sleep_calls == 4) throw PollLimit{}; }
struct Device
{
    HRESULT presents[2] = {D3D_OK, D3D_OK};
    HRESULT cooperative[2] = {D3DERR_DEVICELOST, D3DERR_DEVICENOTRESET};
    unsigned present_calls = 0, cooperative_calls = 0;
    bool null_arguments = true;
    HRESULT Present(void *src, void *dst, void *window, void *dirty)
    {
        null_arguments = null_arguments && !src && !dst && !window && !dirty;
        unsigned index = present_calls++;
        return presents[index < 2 ? index : 1];
    }
    HRESULT TestCooperativeLevel()
    {
        unsigned index = cooperative_calls++;
        return cooperative[index < 2 ? index : 1];
    }
};
char gDisplayResetState;
struct PCLTShell { Device *m_pd3dDevice; void DisplayScreen(); };
PCLTShell LT;
@FUNCTION@
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    Device device;
    unsigned calls = 1, returned = 0;
    if (!std::strcmp(argv[1], "success")) {}
    else if (!std::strcmp(argv[1], "lost-needs-reset")) device.presents[0] = D3DERR_DEVICELOST;
    else if (!std::strcmp(argv[1], "lost-then-ready"))
    {
        device.presents[0] = D3DERR_DEVICELOST;
        device.cooperative[0] = D3D_OK;
    }
    else if (!std::strcmp(argv[1], "other-error")) device.presents[0] = D3DERR_DRIVERINTERNALERROR;
    else if (!std::strcmp(argv[1], "pending-reset-success")) gDisplayResetState = 2;
    else if (!std::strcmp(argv[1], "pending-reset-loss"))
    {
        gDisplayResetState = 2;
        device.presents[0] = D3DERR_DEVICELOST;
    }
    else if (!std::strcmp(argv[1], "next-frame-after-loss"))
    {
        device.presents[0] = D3DERR_DEVICELOST;
        calls = 2;
    }
    else return 2;
    LT.m_pd3dDevice = &device;
    bool polling_limit = false;
    try { for (unsigned i = 0; i < calls; ++i) { LT.DisplayScreen(); ++returned; } }
    catch (const PollLimit &) { polling_limit = true; }
    std::printf("{\"returned\":%u,\"present_calls\":%u,\"cooperative_calls\":%u,"
                "\"sleep_calls\":%u,\"reset_flag\":%d,\"null_arguments\":%s,\"polling_limit\":%s}\n",
                returned, device.present_calls, device.cooperative_calls, sleep_calls, gDisplayResetState,
                device.null_arguments ? "true" : "false", polling_limit ? "true" : "false");
    return polling_limit ? 77 : 0;
}
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def extract_function(source):
    # The class method ends at the first closing brace at column zero; nested scopes are indented. Preserve the
    # entire original function, including its disabled statistics block, instead of reconstructing its statements.
    matches = list(re.finditer(r"^void PCLTShell::DisplayScreen\(\)[^\n]*\n\{\n.*?^\}", source,
                              re.MULTILINE | re.DOTALL))
    if len(matches) != 1:
        raise RuntimeError("expected one complete PCLTShell::DisplayScreen definition")
    return matches[0].group() + "\n"


def expected(case):
    count = 2 if case == "next-frame-after-loss" else 1
    return {"returned": count, "present_calls": count, "cooperative_calls": 0, "sleep_calls": 0,
            "reset_flag": 2 if case.startswith("pending-reset-") else 0,
            "null_arguments": True, "polling_limit": False}


def run_case(binary, case):
    try:
        proc = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=5)
    except subprocess.TimeoutExpired:
        return {"case": case, "timeout": True, "pass": False}
    try:
        actual = json.loads(proc.stdout)
    except json.JSONDecodeError:
        actual = {}
    return {"case": case, "exit": proc.returncode, "actual": actual, "expected": expected(case),
            "stderr": proc.stderr, "pass": proc.returncode == 0 and actual == expected(case)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT,
                        help="checkout supplying src/ltshell.cpp and port/patches (default: this checkout)")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"), help="host C++ compiler command")
    args = parser.parse_args()
    parent = ROOT / "local-data/port/device-loss-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"source_root": str(args.source_root.resolve()), "patches": [], "cases": [], "controls": []}
    passed = False
    try:
        raw = (args.source_root / "src/ltshell.cpp").read_bytes()
        receipt["source_sha256"] = digest(raw)
        (out / "original-ltshell.cpp").write_bytes(raw)
        target = out / "ltshell.cpp"
        target.write_bytes(raw)
        for patch in sorted((args.source_root / "port/patches").glob("*.patch")):
            data = patch.read_bytes()
            sections = [section for section in re.split(br"(?=^--- a/)", data, flags=re.MULTILINE)
                        if section.startswith(b"--- a/ltshell.cpp\n+++ b/ltshell.cpp\n")]
            if not sections:
                continue
            result = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(out),
                                     "-r", "-"], input=b"".join(sections), capture_output=True, timeout=10)
            receipt["patches"].append({"name": patch.name, "sha256": digest(data), "exit": result.returncode,
                                       "output": (result.stdout + result.stderr).decode(errors="replace")})
            if result.returncode:
                raise RuntimeError(f"{patch.name} did not apply cleanly")
        receipt["patched_sha256"] = digest(target.read_bytes())
        binaries = {}
        for variant, data in (("original", raw), ("patched", target.read_bytes())):
            function = extract_function(data.decode("latin-1"))
            driver = out / f"{variant}.cpp"
            driver.write_text(HARNESS.replace("@FUNCTION@", function))
            binary = out / variant
            command = [*shlex.split(args.cxx), "-std=c++11", "-O0", "-Wall", "-Wextra", str(driver), "-o", str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30,
                                    env=dict(os.environ, TMPDIR=str(out)))
            receipt[variant] = {"function_sha256": digest(function.encode("latin-1")),
                                "driver_sha256": digest(driver.read_bytes()), "compile": command,
                                "compile_exit": result.returncode, "compiler_output": result.stdout + result.stderr}
            if result.returncode:
                raise RuntimeError(f"{variant} harness did not compile")
            binaries[variant] = binary
        for case in ("success", "lost-needs-reset", "lost-then-ready", "other-error", "pending-reset-success",
                     "pending-reset-loss", "next-frame-after-loss"):
            result = run_case(binaries["patched"], case)
            receipt["cases"].append(result)
            print(f"{'PASS' if result['pass'] else 'FAIL'} {case}")
        for case in ("success", "lost-needs-reset", "lost-then-ready"):
            result = run_case(binaries["original"], case)
            if case == "lost-needs-reset":
                result["control_pass"] = (result.get("exit") == 77 and
                                          result.get("actual", {}).get("polling_limit") is True and
                                          result["actual"]["returned"] == 0)
            elif case == "lost-then-ready":
                result["control_pass"] = (result.get("exit") == 0 and not result["pass"] and
                                          result.get("actual", {}).get("reset_flag") == 1 and
                                          result["actual"]["cooperative_calls"] == 1)
            else:
                result["control_pass"] = result["pass"]
            receipt["controls"].append(result)
            print(f"{'PASS' if result['control_pass'] else 'FAIL'} original control: {case}")
        passed = (all(case["pass"] for case in receipt["cases"]) and
                  all(case["control_pass"] for case in receipt["controls"]))
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
        print(f"FAIL {error}")
    receipt["pass"] = passed
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
