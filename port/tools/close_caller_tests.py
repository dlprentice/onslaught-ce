#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual film/credits callers of the shell's CE close latch with bounded host boundaries.

The complete production Play, RollCredits, NextFrame, shell/platform methods and skip handlers are extracted
after applying the patch queue. Only OS, renderer, controller delivery, Bink and music boundaries are fake.
No game, retail file, display, audio device, network or port dependencies are used. The original 0059-only
caller is always compiled as a negative control; faithful/public and private variants retain their old behavior.
Results and all compiled inputs stay under ignored local-data. Run the separate inactive_close_tests.py with
--shell-static sPortQuitRequested as well: these caller checks do not replace or weaken its original cases.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess
import tempfile

import inactive_close_tests as shell_tests

ROOT = Path(__file__).resolve().parents[2]
FILES = (*shell_tests.FILES, "DXFMV.cpp", "DXFrontEndVideo.cpp", "fmv.cpp", "game.cpp")
CORRECTION = "0060-consume-ce-quit-in-film-and-credits.patch"
CASES = ("normal", "skip", "active-close", "minimized-close", "lost-close", "already-close", "close-next-call")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def definitions(sources, ce, corrected):
    result = shell_tests.functions(sources, ce, ["sPortQuitRequested"], True)
    result["QUIT_QUERY"] = (shell_tests.definition(sources["ltshell.cpp"], r"bool PortCeQuitRequested\(\)")
                            if corrected else "")
    result["FILM"] = shell_tests.definition(sources["DXFMV.cpp"], r"BOOL\s+CDXFMV::Play\(")
    result["NEXT_FRAME"] = shell_tests.definition(sources["DXFrontEndVideo.cpp"], r"int\s+CDXFrontEndVideo::NextFrame\(")
    result["FILM_SKIP"] = shell_tests.definition(sources["fmv.cpp"], r"void\s+CFMV::ReceiveButtonAction\(")
    result["CREDITS"] = shell_tests.definition(sources["game.cpp"], r"void\s+CGame::RollCredits\(\)")
    classes = re.findall(r"^class\s+CGameCreditControlHandler : public IController\n\{\n.*?^\};",
                         sources["game.cpp"], re.M | re.S)
    if len(classes) != 1:
        raise RuntimeError("expected one complete production credits input handler")
    result["CREDITS_HANDLER"] = classes[0]
    return result


def expected(case, fixed):
    close = "close" in case or case == "close-next-call"
    stuck = close and not fixed and case in ("minimized-close", "lost-close")
    return {"returned": not stuck, "stop": "budget" if stuck else "returned",
            "prompt_close": bool(close and fixed), "closed": close,
            "cleanup": not stuck, "entry_skipped": bool(fixed and case == "already-close")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/close-caller-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"source_root": str(args.source_root.resolve()), "patches": [], "builds": {}, "cases": [],
               "unchanged_controls": [],
               "limits": ["Actual extracted caller, shell, platform, NextFrame and skip code; fake OS and device boundaries.",
                          "Bink decode/render, native minimization, process exit and Windows ABI are not exercised."]}
    passed = False
    try:
        source_dir = out / "source"
        source_dir.mkdir()
        for name in FILES:
            (source_dir / name).write_bytes((args.source_root / "src" / name).read_bytes())
        before = None
        for patch in sorted((args.source_root / "port/patches").glob("*.patch")):
            if patch.name == CORRECTION:
                before = {name: (source_dir / name).read_text(encoding="latin-1") for name in FILES}
            raw = patch.read_bytes()
            sections = [s for s in re.split(br"(?=^--- a/)", raw, flags=re.M)
                        if any(s.startswith(f"--- a/{n}\n+++ b/{n}\n".encode()) for n in FILES)]
            if not sections:
                continue
            proc = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(source_dir), "-r", "-"],
                                  input=b"".join(sections), capture_output=True, timeout=10)
            receipt["patches"].append({"name": patch.name, "sha256": sha(raw), "exit": proc.returncode,
                                       "output": (proc.stdout + proc.stderr).decode(errors="replace")})
            if proc.returncode:
                raise RuntimeError(f"patch failed: {patch.name}")
        if before is None:
            raise RuntimeError("correction absent: cannot create the required old-source negative control")
        after = {name: (source_dir / name).read_text(encoding="latin-1") for name in FILES}
        ce = (args.source_root / "port/platform/common/ce.cpp").read_text()
        driver_path = args.source_root / "port/tools/tests/close_caller_driver.cpp"
        driver_text = driver_path.read_text()
        receipt["driver_sha256"] = sha(driver_path.read_bytes())
        receipt["ce_sha256"] = sha(ce.encode())
        for label, sources, public, corrected in (("before-public", before, True, False),
                                                  ("fixed-public", after, True, True),
                                                  ("fixed-private", after, False, True)):
            functions = definitions(sources, ce, corrected)
            text = driver_text.replace("@CORRECTED@", "yes" if corrected else "no")
            for name, body in functions.items():
                text = text.replace("@" + name + "@", body)
            if re.search(r"@[A-Z_]+@", text):
                raise RuntimeError("unexpanded driver marker")
            source = out / f"{label}.cpp"
            source.write_text(text)
            binary = out / label
            command = [*shlex.split(args.cxx), "-std=c++14", "-O0", "-g", "-Wall", "-Wextra",
                       *(['-DCE_VERSION="test"'] if public else []), str(source), "-o", str(binary)]
            proc = subprocess.run(command, capture_output=True, text=True, timeout=30,
                                  env=dict(os.environ, TMPDIR=str(out)))
            (out / f"{label}-build.log").write_text(proc.stdout + proc.stderr)
            receipt["builds"][label] = {"command": command, "exit": proc.returncode,
                                         "functions_sha256": {name: sha(body.encode()) for name, body in functions.items()}}
            if proc.returncode:
                raise RuntimeError(f"compile failed: {out / (label + '-build.log')}")
            receipt["builds"][label]["binary_sha256"] = sha(binary.read_bytes())
            admissions = (True, False) if public else (True,)
            for admitted in admissions:
                for caller in ("film", "credits"):
                    for case in CASES:
                        proc = subprocess.run([str(binary), caller, case, str(int(admitted))],
                                              capture_output=True, text=True, timeout=5)
                        try:
                            actual = json.loads(proc.stdout)
                        except json.JSONDecodeError:
                            actual = {}
                        fixed = corrected and public and admitted
                        want = expected(case, fixed)
                        # Only CE-admitted 0059 freezes the pump. Faithful/private paths still dispatch restore/skip.
                        if not (public and admitted):
                            want.update(returned=True, stop="returned", cleanup=True)
                        errors = [key for key, value in want.items() if actual.get(key) != value]
                        if fixed and "close" in case and caller == "film" and actual.get("film_result") is not True:
                            errors.append("film close did not return the existing skipped-film result")
                        passed_case = proc.returncode == 0 and not errors and actual.get("checks", 0) > 0
                        receipt["cases"].append({"variant": label, "ce": admitted, "caller": caller,
                                                 "case": case, "expected": want, "actual": actual, "errors": errors,
                                                 "exit": proc.returncode, "stderr": proc.stderr, "passed": passed_case})
                        if not passed_case:
                            print(f"FAIL {label} CE={int(admitted)} {caller} {case}: {errors} {proc.stderr}")
        indexed = {(c["variant"], c["ce"], c["caller"], c["case"]): c["actual"] for c in receipt["cases"]}
        for caller in ("film", "credits"):
            for case in CASES:
                baseline = indexed["before-public", False, caller, case]
                for label, admitted in (("fixed-public", False), ("fixed-private", True)):
                    same = baseline == indexed[label, admitted, caller, case]
                    receipt["unchanged_controls"].append({"variant": label, "caller": caller, "case": case, "passed": same})
            for case in ("normal", "skip"):
                same = indexed["before-public", True, caller, case] == indexed["fixed-public", True, caller, case]
                receipt["unchanged_controls"].append({"variant": "ordinary-ce", "caller": caller, "case": case, "passed": same})
        passed = bool(receipt["cases"]) and all(case["passed"] for case in [*receipt["cases"], *receipt["unchanged_controls"]])
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
        print(error)
    receipt["pass"] = passed
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"{sum(c['passed'] for c in receipt['cases'])}/{len(receipt['cases'])} expected outcomes")
    print(f"{sum(c['passed'] for c in receipt['unchanged_controls'])}/{len(receipt['unchanged_controls'])} unchanged control comparisons")
    print(out / "result.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
