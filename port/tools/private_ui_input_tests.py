#!/usr/bin/env python3
"""Bounded authored private-FD/window-consumer controls; no game or service run."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("port/platform/common/private_ui_input.h", "port/platform/common/private_ui_input.cpp",
           "port/platform/common/window.cpp", "port/tools/tests/private_ui_input_driver.cpp")


def function(source, name):
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: "".join("\n" if c == "\n" else " " for c in m.group()), source, flags=re.S)
    match = re.search(r"(?m)^\s*(?:static\s+)?bool\s+" + name + r"\s*\([^;]*?\)\s*\{", masked)
    if not match:
        raise ValueError("missing owned window consumer: " + name)
    end = masked.find("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (masked[end] == "{") - (masked[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--word-size", choices=("32", "64", "both"), default="64")
    parser.add_argument("--no-controls", action="store_true")
    args = parser.parse_args()
    output = ROOT / "local-data/port/private-ui-input-tests"
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    pins = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in SOURCES}
    core = (ROOT / SOURCES[1]).read_text()
    window = (ROOT / SOURCES[2]).read_text()
    consumer = function(window, "InputLog") + "\n" + function(window, "PrivateUiCharacter")
    variants = {"positive": (core, consumer, None)}
    controls = {
        "shared-permissions": ("(st.st_mode&0077)", "false", "permissions"),
        "exec-reader-leak": ("F_DUPFD_CLOEXEC", "F_DUPFD", "exec"),
        "chooser-without-action": ("if (!state.folder) return;", "", "folder"),
        "stale-chooser": ("state.tag!=state.chooser", "false", "wrong-chooser"),
        "consume-under-pressure": ("if (!deliver(context,character)) return;", "deliver(context,character);", "backpressure"),
        "recording-allowed": ("if (ConflictingInput()) return;", "if (ConflictingInput() && false) return;", "recording"),
        "refusal-owner": ("TransferredInput inputOwner{input};", "", "refused-exec"),
        "refusal-seal": ("if (fcntl(input,F_SETFD,descriptorFlags|FD_CLOEXEC)) return;", "", "earliest-seal"),
        "partial-cancel-retained": ("if (state.folder) Refuse();", "if (state.folder) state.cancelled=true;", "partial-cancel"),
    }
    if not args.no_controls:
        for name, (before, after, case) in controls.items():
            if core.count(before) != 1:
                raise ValueError("source-bound control drift: " + name)
            variants[name] = (core.replace(before, after), consumer, case)
        variants["wrong-message-kind"] = (core, consumer.replace("WM_CHAR, character", "WM_KEYDOWN, character"), "text")
        guard = "    if (PortPrivateUiConfigured()) return false;"
        if consumer.count(guard) != 1:
            raise ValueError("source-bound privacy control drift")
        variants["input-log-allowed"] = (core, consumer.replace(guard, ""), "privacy")
    rows = []
    widths = ("32", "64") if args.word_size == "both" else (args.word_size,)
    clean_env = {k: v for k, v in os.environ.items() if not k.startswith("BEA_") and k not in ("DISPLAY", "WAYLAND_DISPLAY")}
    for bits in widths:
        for label, (source, snippet, case) in variants.items():
            view = work / (label + "-" + bits)
            view.mkdir()
            observer = '\nextern "C" unsigned PrivateUiRetainedForFixture() { unsigned result=0; for (size_t i=0;i<sizeof state.bytes;++i) result+=state.bytes[i]!=0; return result; }\n'
            (view / "private_ui_input.cpp").write_text(source + observer)
            (view / "private_ui_input.h").write_bytes((ROOT / SOURCES[0]).read_bytes())
            (view / "private_ui_window.inc").write_text(snippet)
            (view / "driver.cpp").write_bytes((ROOT / SOURCES[3]).read_bytes())
            exe = view / "fixture"
            command = ["g++", "-m" + bits, "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror",
                       "-I", str(view), str(view / "driver.cpp"), str(view / "private_ui_input.cpp"), "-Wl,--wrap=fcntl", "-o", str(exe)]
            with (view / "compile.log").open("w") as log:
                compiled = subprocess.run(command, env=clean_env, stdout=log, stderr=subprocess.STDOUT, timeout=30)
            row = {"case": label, "bits": bits, "compile_exit": compiled.returncode,
                   "variant_source_sha256": hashlib.sha256(source.encode()).hexdigest()}
            if compiled.returncode == 0:
                run = subprocess.run([str(exe)] + ([case] if case else []), env=clean_env,
                                     text=True, capture_output=True, timeout=10)
                (view / "stdout.log").write_text(run.stdout)
                (view / "stderr.log").write_text(run.stderr)
                row.update(run_exit=run.returncode, payload_not_diagnosed=not any(value in run.stdout + run.stderr for value in ("AUTHORED-", "/owned/authored-invitation", "/owned/stale-invitation")))
            row["passed"] = row["compile_exit"] == 0 and row.get("payload_not_diagnosed", False) and (
                row.get("run_exit") == 0 if label == "positive" else row.get("run_exit") == 1)
            rows.append(row)
    # The additive Windows build path is a no-op, without POSIX imports.
    with (work / "windows-noop.log").open("w") as log:
        windows = subprocess.run(["g++", "-D_WIN32", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-c",
                                  str(ROOT / SOURCES[1]), "-o", str(work / "windows-noop.o")],
                                 env=clean_env, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    unchanged = all(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == pin for name, pin in pins.items())
    result = {"source_pins": pins, "cases": rows, "windows_noop_compile_exit": windows.returncode,
              "source_unchanged": unchanged, "game_network_key_vm_operations": 0,
              "passed": all(row["passed"] for row in rows) and unchanged and windows.returncode == 0}
    (work / "result.private.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": result["passed"], "cases": len(rows), "receipt": str(work / "result.private.json")}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
