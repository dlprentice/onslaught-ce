#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check actual recorder/window queue admission, staged publication and parser behavior without a game or SDL init.

The frozen source is an executable negative control. Normal tape bytes, callback order and pacing must stay equal;
failed captures keep bounded runtime FIFO delivery and a private .incomplete stage, and never publish a final tape.
Every source snapshot, compilation and observation is retained under ignored local-data/port/record-queue-tests/.
Only authored events and uniquely created fixture folders are used; no profile, device, display or GPU is opened.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile

import deps

ROOT = Path(__file__).resolve().parents[2]
SOURCE = "port/platform/common/replay.cpp"
NORMAL = ("normal", "pilot-normal", "atexit", "stage-collision", "environment-change")
OVERFLOW = ("key-up-overflow", "char-overflow", "mouse-button-overflow", "mouse-move-overflow",
            "pilot-overflow", "window-ring-overflow", "focus-overflow")
DEFERRED_MOUSE = ("deferred-mouse-coalesce", "deferred-mouse-mode", "deferred-mouse-resize")
FAULTS = ("open-error", "registration-error", "header-write-error", "write-error", "stream-error", "flush-error",
          "final-flush-error", "close-error", "publish-error", "probe-open-error", "probe-finalizer-error")
CASES = (*NORMAL, *OVERFLOW, *DEFERRED_MOUSE, "interrupted", "existing-destination", "publish-collision", *FAULTS)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check(condition, errors, message):
    if not condition:
        errors.append(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--observe", action="store_true", help="retain observations without asserting the candidate fix")
    parser.add_argument("--baseline-ref", required=True,
                        help="a source commit in this checkout before the queue repair (required negative control)")
    args = parser.parse_args()
    parent = ROOT / "local-data/port/record-queue-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"baseline_ref": args.baseline_ref, "builds": {}, "cases": [], "controls": [], "observe": args.observe,
               "limits": ["Actual production recorder and 255-slot window ring; synthetic SDL events/time and I/O faults.",
                          "No game, SDL initialization, physical input, profile, GPU or native Windows runtime."]}
    passed = False
    try:
        baseline = subprocess.run(["git", "show", f"{args.baseline_ref}:{SOURCE}"], cwd=ROOT,
                                  check=True, capture_output=True).stdout
        sources = {"baseline": baseline, "candidate": (ROOT / SOURCE).read_bytes()}
        records, binaries = {}, {}
        for label, source in sources.items():
            folder = out / label
            folder.mkdir()
            cpp = folder / "replay.cpp"
            cpp.write_bytes(source)
            window = subprocess.run(["git", "show", f"{args.baseline_ref}:port/platform/common/window.cpp"], cwd=ROOT,
                                    check=True, capture_output=True).stdout if label == "baseline" else (ROOT / "port/platform/common/window.cpp").read_bytes()
            window_path = folder / "window.cpp"
            window_path.write_bytes(window)
            driver = (ROOT / "port/tools/tests/record_queue_driver.cpp").read_text().replace("REPLAY_CPP", str(cpp)).replace("WINDOW_CPP", str(window_path))
            path = folder / "driver.cpp"
            path.write_text(driver)
            binary = folder / "driver"
            binaries[label] = binary
            command = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
                       "-DNOMINMAX", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar",
                       "-fms-extensions", "-fno-autolink", "-w", "-O0",
                       f"-I{deps.PORT / 'include/linux'}", f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}",
                       f"-I{deps.PREFIX / 'include'}", f"-I{ROOT / 'port/platform/common'}", str(path),
                       f"-L{deps.PREFIX / 'lib'}", "-lSDL3", f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60,
                                    env=dict(os.environ, TMPDIR=str(folder)))
            receipt["builds"][label] = {"source_sha256": sha(source), "window_sha256": sha(window),
                                         "driver_sha256": sha(path.read_bytes()), "command": command,
                                         "exit": result.returncode, "output": result.stdout + result.stderr}
            if result.returncode:
                raise RuntimeError(f"{label} fixture did not compile")
            for case in CASES:
                tape = folder / f"{case}.tape"
                env = {key: value for key, value in os.environ.items() if not key.startswith(("BEA_PORT_", "SDL_"))}
                env["BEA_PORT_RECORD"] = str(tape)
                result = subprocess.run([str(binary), case, str(tape)], env=env,
                                        capture_output=True, text=True, timeout=10)
                try:
                    actual = json.loads(result.stdout)
                except json.JSONDecodeError:
                    actual = {}
                content = tape.read_bytes() if tape.exists() else None
                stages = [{"name": stage.name, "bytes": stage.stat().st_size, "mode": stat.S_IMODE(stage.stat().st_mode),
                           "sha256": sha(stage.read_bytes())}
                          for stage in sorted(folder.glob(tape.name + ".recording-*.incomplete"))]
                record = {"variant": label, "case": case, "exit": result.returncode, "actual": actual,
                          "stderr": result.stderr, "tape_exists": tape.exists(), "stages": stages,
                          "tape_sha256": sha(content) if content is not None else None,
                          "tape_bytes": len(content) if content is not None else None, "errors": []}
                records[label, case] = record
                receipt["cases"].append(record)
        for case in CASES:
            before, after = records["baseline", case], records["candidate", case]
            actual, errors = after["actual"], after["errors"]
            startup = case == "probe-open-error"
            check(after["exit"] == (3 if startup else 0) and (startup or bool(actual)), errors, "fixture did not return through its expected path")
            if not startup:
                check(actual.get("pending") == 0 and actual.get("sdl_backlog") == 0, errors, "runtime input remained queued")
            failures = after["stderr"].count(": recording failed:")
            success = after["stderr"].count(f": recording finalized: final tape published {out / 'candidate' / (case + '.tape')}\n")
            if case in NORMAL:
                check(after["tape_sha256"] == before["tape_sha256"], errors, "normal tape bytes changed")
                check(actual == before["actual"], errors, "normal callbacks or pacing changed")
                check(failures == 0 and success == 1, errors, "normal finalizer status missing/duplicated")
                if case == "stage-collision":
                    check(len(after["stages"]) == 1 and after["stages"][0]["sha256"] == sha(b"OLD INCOMPLETE\n"), errors, "exclusive stage collision changed old file")
                else:
                    check(not after["stages"], errors, "published capture left a new incomplete stage")
            elif case == "interrupted":
                check(not after["tape_exists"] and len(after["stages"]) == 1, errors, "interrupted capture was published or lost its stage")
                check(success == 0, errors, "interrupted capture reported publication")
            else:
                check(failures == 1 and success == 0, errors, "failed capture status must occur once without success")
                if case in ("existing-destination", "publish-collision"):
                    expected = b"OLD RECORDING\n" if case == "existing-destination" else b"FOREIGN RECORDING\n"
                    check(after["tape_sha256"] == sha(expected), errors, "no-replace publication changed the existing destination")
                else:
                    check(not after["tape_exists"], errors, "failed capture published a final tape")
                check(len(after["stages"]) == (0 if case == "open-error" else 1), errors, "owned failed stage not retained")
                for stage in after["stages"]:
                    check(stage["mode"] == 0o600, errors, "stage is not owner-only")
                if case in OVERFLOW:
                    count = 1023 if case in ("mouse-button-overflow", "window-ring-overflow") else 1024
                    emitted = [message[1] for message in actual.get("messages", []) if message[0] == 0x102 and message[1] >= 0x100]
                    check(emitted == list(range(0x100, 0x100 + count)), errors, "runtime FIFO dropped/repeated/reordered accepted chars")
                    check(actual.get("w_up") == 1 and not actual.get("w_held"), errors, "runtime W release lost/duplicated")
                    check(actual.get("held_after") == 0, errors, "physical held tracking did not retire")
                    if case == "focus-overflow":
                        check(actual.get("held_before") == 26 and actual.get("key_up") == 26, errors, "focus repair lost pending lifetimes")
                    if case == "mouse-button-overflow":
                        check(actual.get("buttons") == 2 and actual.get("moves") == 2, errors, "deferred mouse edges lost/duplicated")
                    if case == "mouse-move-overflow":
                        check(actual.get("moves") == 1, errors, "deferred mouse move lost/duplicated")
                elif case in DEFERRED_MOUSE:
                    mouse = [message for message in actual.get("messages", []) if 0x200 <= message[0] <= 0x20a]
                    expected = [[0x200, 0, (20 << 16) | 10], [0x200, 0, (40 << 16) | 30],
                                [0x201, 1, (40 << 16) | 30], [0x200, 1, (40 << 16) | 30],
                                [0x202, 0, (40 << 16) | 30]] if case == "deferred-mouse-coalesce" else [[0x200, 0, (20 << 16) | 10], [0x201, 1, (20 << 16) | 10]]
                    check(mouse == expected, errors, "prepared mouse FIFO/cursor ownership changed")
                elif not startup:
                    check(actual == records["baseline", "normal"]["actual"], errors, "file failure changed normal runtime input/pacing")
            after["pass"] = not errors
            print(f"{'PASS' if not errors else 'FAIL'} {case}")
        # Reproduce the old loss as a negative, rather than accepting an empty selection or an unchanged implementation.
        for case in ("key-up-overflow", "window-ring-overflow", "focus-overflow"):
            old = records["baseline", case]
            receipt["controls"].append({"case": case, "pass": old["tape_exists"] and old["actual"].get("w_held") and old["actual"].get("w_up") == 0})
        # Actual parser and replay consumer: ordinary tape bytes produce the same posted calls and consumed mouse levels.
        env = {key: value for key, value in os.environ.items() if not key.startswith(("BEA_PORT_", "SDL_"))}
        replayed = {}
        for label, binary in binaries.items():
            tape = out / label / "normal.tape"
            run = subprocess.run([str(binary), "load", str(tape)], env=dict(env, BEA_PORT_TAPE=str(tape)),
                                 capture_output=True, text=True, timeout=10)
            replayed[label] = {"exit": run.returncode, "actual": json.loads(run.stdout), "stderr": run.stderr}
        mismatch = {}
        for label, binary in binaries.items():
            tape = out / label / "normal.tape"
            run = subprocess.run([str(binary), "load-resized", str(tape)], env=dict(env, BEA_PORT_TAPE=str(tape)), capture_output=True, text=True, timeout=10)
            mismatch[label] = {"exit": run.returncode, "stderr": run.stderr}
        receipt["replay_size_mismatch"] = mismatch
        receipt["controls"].append({"case": "unchanged-replay-size-rejection", "pass": all(item["exit"] == 3 and "Replay stopped." in item["stderr"] for item in mismatch.values())})
        receipt["replay"] = replayed
        receipt["controls"].append({"case": "unchanged-parser-replay", "pass": replayed["baseline"]["exit"] == replayed["candidate"]["exit"] == 0 and replayed["baseline"]["actual"] == replayed["candidate"]["actual"]})
        missing = out / "candidate/key-up-overflow.tape"
        run = subprocess.run([str(binaries["candidate"]), "load", str(missing)], env=dict(env, BEA_PORT_TAPE=str(missing)), capture_output=True, text=True, timeout=10)
        spec = importlib.util.spec_from_file_location("record_queue_replay_session", ROOT / "port/tools/replay_session.py")
        session = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(session)
        rejected = False
        try:
            session.last_tape_frame(missing)
        except FileNotFoundError:
            rejected = True
        receipt["controls"].append({"case": "missing-final-rejected", "exit": run.returncode, "stderr": run.stderr,
                                     "tool_rejected": rejected, "pass": run.returncode == 3 and rejected})
        passed = args.observe or (all(case["pass"] for case in receipt["cases"] if case["variant"] == "candidate") and all(case["pass"] for case in receipt["controls"]))
    except (OSError, RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
        receipt["error"] = str(error)
        print(f"FAIL {error}")
    receipt["pass"] = passed
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
