#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host checks for the museum exhibition plan and the cinema film selector.

Compiles port/tools/tests/ce_museum_exhibition_driver.cpp against
port/game/ce_museum_exhibition.h and port/game/ce_museum_cinema_controls.h and
runs it. Those three files are the whole input: no engine source, game data,
network or display is used, and no command goes through a shell. Two builds
are made:

  normal    optimised; the driver's own checks decide
  sanitize  the same checks under AddressSanitizer and UndefinedBehaviorSanitizer

In each build the driver also prints the plan of its six-bay fixture twice, in
separate processes, and the two prints must be identical.

Binaries, logs and results.json go to a fresh run directory under
local-data/ce-museum-exhibition-tests/ at the top of the checkout. An explicit
--out must also be new and inside the checkout's local-data directory.

Exit status:
  0  every requested mode passed
  1  a build, a check, a run or a sanitizer failed, or a source file is missing
  2  bad command line
  3  nothing failed, but a requested mode could not run: UNAVAILABLE (no usable
     compiler) or SKIPPED (the compiler has no working sanitizer runtime here).
     --allow-unavailable turns this into 0 as long as one mode passed.

The compiler is --cxx, else $CXX, else the first of g++, clang++, c++ on PATH.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
PORT = TOOLS.parent
ROOT = PORT.parent
DRIVER = TOOLS / "tests" / "ce_museum_exhibition_driver.cpp"
HEADERS = (PORT / "game" / "ce_museum_exhibition.h", PORT / "game" / "ce_museum_cinema_controls.h")
OUT_BASE = ROOT / "local-data" / "ce-museum-exhibition-tests"
OUT = None
EXE = ".exe" if os.name == "nt" else ""

STANDARD = ["-std=c++14", "-Wall", "-Wextra", "-Wpedantic"]
MODES = {
    "normal": ["-O2"],
    "sanitize": ["-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all"],
}
SUMMARY = re.compile(r"^ce_museum_exhibition_driver: (\d+) checks, (\d+) failed$", re.M)
GROUP = re.compile(r"^(ok|FAIL) (\S+) \((\d+) checks\)$", re.M)
SANITIZER_REPORT = re.compile(r"ERROR: \w+Sanitizer|runtime error:")
# The plan checks alone are more than two thousand; fewer means the driver did
# not run what it should.
MIN_CHECKS = 3000
FIXTURE_BAYS = 6


def shown(path):
    """A path as written in messages and results: relative to the checkout."""
    try:
        return str(Path(path).relative_to(ROOT))
    except ValueError:
        return str(path)


def write_text(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def stop(process):
    """Stops verified members of the child session, never signals a whole group."""
    proc = Path("/proc")
    if not proc.is_dir():
        # Popen owns this process handle; no process-name lookup is involved.
        process.kill()
        return
    targets = []
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        fd = None
        try:
            pid = int(entry.name)
            if entry.stat().st_uid != os.geteuid():
                continue
            stat = (entry / "stat").read_text().rsplit(") ", 1)[1].split()
            if int(stat[2]) != process.pid or int(stat[3]) != process.pid:
                continue
            command = (entry / "cmdline").read_bytes().split(b"\0")
            if not command[0]:
                continue
            fd = os.pidfd_open(pid) if hasattr(os, "pidfd_open") else None
            current = (entry / "stat").read_text().rsplit(") ", 1)[1].split()
            if current[19] != stat[19] or current[2:4] != stat[2:4]:
                if fd is not None:
                    os.close(fd)
                continue
            targets.append((pid, fd))
        except (OSError, ValueError, IndexError):
            if fd is not None:
                os.close(fd)
    # Reap descendants before their owner; each command and session was checked.
    for pid, fd in sorted(targets, key=lambda item: item[0] == process.pid):
        try:
            if fd is not None:
                signal.pidfd_send_signal(fd, signal.SIGKILL)
            else:
                os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
        finally:
            if fd is not None:
                os.close(fd)


def run(command, log, timeout, env=None, cwd=None):
    """Runs one command, given as an argument list, and logs it.

    Returns (code, stdout, stderr, note). code is None when the command could
    not be started or was ended for running past the timeout; note says which.
    """
    started = time.monotonic()
    code, out, err, note = None, b"", b"", ""
    try:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, env=env, cwd=cwd, start_new_session=True)
    except OSError as error:
        note = "could not start: %s" % (error.strerror or error)
    else:
        try:
            out, err = process.communicate(timeout=timeout)
            code = process.returncode
        except subprocess.TimeoutExpired:
            stop(process)
            out, err = process.communicate()
            note = "timed out after %g s" % timeout
    text_out = out.decode("utf-8", "replace")
    text_err = err.decode("utf-8", "replace")
    write_text(log, "command: %s\nexit: %s%s\nseconds: %.2f\n\n--- stdout ---\n%s\n--- stderr ---\n%s" % (
        " ".join(shlex.quote(shown(word)) for word in command), code, " (%s)" % note if note else "",
        time.monotonic() - started, text_out, text_err))
    return code, text_out, text_err, note


def find_compiler(option):
    """Returns (argument list, where it was named), or (None, why there is none)."""
    for origin, value in (("--cxx", option), ("CXX", os.environ.get("CXX", ""))):
        if not value:
            continue
        try:
            words = shlex.split(value)
        except ValueError as error:
            return None, "%s cannot be read: %s" % (origin, error)
        if words and shutil.which(words[0]):
            return words, origin
        return None, "%s names %r, which is not a program on PATH" % (origin, value)
    for name in ("g++", "clang++", "c++"):
        if shutil.which(name):
            return [name], "PATH"
    return None, "no C++ compiler found: tried g++, clang++ and c++ (set CXX or pass --cxx)"


def build_env(folder):
    """The caller's environment, with plain messages and compiler scratch kept in our folder."""
    env = dict(os.environ)
    env["LC_ALL"] = "C"
    env["TMPDIR"] = str(folder)
    return env


def run_env():
    """What the driver is given: a path, a plain locale and sanitizer settings. No display.

    Leak checking is off because it cannot run where tracing is restricted, and
    would then fail a run that is sound; memory and undefined-behaviour errors
    still end the run at once with a report.
    """
    env = {"LC_ALL": "C",
           "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    for name in ("PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "SystemRoot"):
        if os.environ.get(name):
            env[name] = os.environ[name]
    return env


def sanitizer_probe(cxx, folder, timeout):
    """Returns "" when a trivial sanitized program builds and runs here, else why not."""
    source, binary = folder / "probe.cpp", folder / ("probe" + EXE)
    write_text(source, "int main() { return 0; }\n")
    code, _, _, note = run(cxx + STANDARD + MODES["sanitize"] + [str(source), "-o", str(binary)],
                           folder / "probe-build.log", timeout, env=build_env(folder), cwd=str(folder))
    if code != 0 or not binary.is_file():
        return note or "this compiler cannot build with -fsanitize=address,undefined (see %s)" % shown(
            folder / "probe-build.log")
    code, _, _, note = run([str(binary)], folder / "probe-run.log", timeout, env=run_env(), cwd=str(folder))
    if code != 0:
        return note or "a sanitized program does not run here (exit %s; see %s)" % (
            code, shown(folder / "probe-run.log"))
    return ""


def run_mode(mode, cxx, args):
    """Builds and runs the driver one way. Returns that mode's result record."""
    folder = OUT / mode
    folder.mkdir(parents=True, exist_ok=True)
    result = {"mode": mode, "status": "FAIL", "detail": "", "checks": 0, "failed": 0, "groups": {},
              "warnings": 0, "failures": [], "logs": shown(folder)}

    def finish(status, detail):
        result["status"], result["detail"] = status, detail
        return result

    if mode == "sanitize":
        why = sanitizer_probe(cxx, folder, args.build_timeout)
        if why:
            return finish("SKIPPED", why)

    # The output directory is fresh, so a failed build cannot run an old binary.
    binary = folder / ("ce_museum_exhibition_driver" + EXE)
    flags = STANDARD + MODES[mode] + (["-Werror"] if args.werror else []) + args.cxxflag
    result["flags"] = flags
    code, out, err, note = run(cxx + flags + [str(DRIVER), "-o", str(binary)], folder / "build.log",
                               args.build_timeout, env=build_env(folder), cwd=str(folder))
    result["warnings"] = len(re.findall(r"\bwarning:", out + err))
    if code != 0 or not binary.is_file():
        return finish("FAIL", "the driver did not build: %s (see %s)" % (
            note or "compiler exit %s" % code, shown(folder / "build.log")))

    # Run every check. The driver's exit status decides; its summary is read to
    # report the counts and to notice a run that checked too little.
    code, out, err, note = run([str(binary)], folder / "run.log", args.timeout, env=run_env(), cwd=str(folder))
    summary = SUMMARY.search(out)
    if summary:
        result["checks"], result["failed"] = int(summary.group(1)), int(summary.group(2))
    result["groups"] = {name: {"ok": state == "ok", "checks": int(count)}
                        for state, name, count in GROUP.findall(out)}
    result["failures"] = [line for line in out.splitlines() if line.startswith("FAIL line ")][:20]
    where = "see %s" % shown(folder / "run.log")
    if SANITIZER_REPORT.search(err) or SANITIZER_REPORT.search(out):
        return finish("FAIL", "a sanitizer reported an error (%s)" % where)
    if code is None:
        return finish("FAIL", "the driver %s (%s)" % (note, where))
    if code != 0:
        return finish("FAIL", "the driver exited %s with %s failed of %s checks (%s)" % (
            code, result["failed"], result["checks"], where))
    if not summary or result["failed"]:
        return finish("FAIL", "the driver exited 0 without a clean summary (%s)" % where)
    if result["checks"] < MIN_CHECKS:
        return finish("FAIL", "only %s checks ran, fewer than the %s expected (%s)" % (
            result["checks"], MIN_CHECKS, where))
    if not result["groups"] or not all(group["ok"] and group["checks"] > 0 for group in result["groups"].values()):
        return finish("FAIL", "a group of checks failed or checked nothing (%s)" % where)

    # The same plan from one process to the next.
    dumps = []
    for attempt in (1, 2):
        log = folder / ("dump-%d.log" % attempt)
        code, out, err, note = run([str(binary), "--dump"], log, args.timeout, env=run_env(), cwd=str(folder))
        if SANITIZER_REPORT.search(err) or SANITIZER_REPORT.search(out):
            return finish("FAIL", "a sanitizer reported an error while printing the plan (see %s)" % shown(log))
        if code != 0:
            return finish("FAIL", "printing the plan failed: %s (see %s)" % (note or "exit %s" % code, shown(log)))
        dumps.append(out)
    write_text(folder / "plan-dump.txt", dumps[0])
    bays = sum(1 for line in dumps[0].splitlines() if line.startswith("bay "))
    if not dumps[0].startswith("valid 1 ") or bays != FIXTURE_BAYS:
        return finish("FAIL", "the printed plan is not a valid six-bay plan (see %s)" % shown(folder / "plan-dump.txt"))
    if dumps[0] != dumps[1]:
        return finish("FAIL", "the printed plan differed between two runs (see %s)" % shown(folder / "dump-2.log"))
    result["plan_sha256"] = hashlib.sha256(dumps[0].encode("utf-8")).hexdigest()
    return finish("PASS", "%s checks in %s groups, 0 failed; plan print stable" % (
        result["checks"], len(result["groups"])))


def positive(text):
    try:
        value = float(text)
    except ValueError:
        value = 0.0
    if not 0.0 < value <= 86400.0:
        raise argparse.ArgumentTypeError("%r is not a number of seconds between 0 and 86400" % text)
    return value


def main(argv=None):
    global OUT
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("normal", "sanitize", "all"), default="all",
                        help="which build to make and run (default: all, meaning normal then sanitize)")
    parser.add_argument("--cxx", default="", metavar="COMPILER",
                        help="the C++ compiler to use, with any leading arguments; overrides $CXX")
    parser.add_argument("--cxxflag", action="append", default=[], metavar="FLAG",
                        help="one more compiler flag, written --cxxflag=FLAG; may be repeated")
    parser.add_argument("--werror", action="store_true", help="fail the build on any compiler warning")
    parser.add_argument("--timeout", type=positive, default=180.0, metavar="SECONDS",
                        help="limit for each run of the driver (default: 180)")
    parser.add_argument("--build-timeout", type=positive, default=300.0, metavar="SECONDS",
                        help="limit for each compile (default: 300)")
    parser.add_argument("--allow-unavailable", action="store_true",
                        help="exit 0 when a mode could not run, as long as another passed")
    parser.add_argument("--out", type=Path,
                        help="fresh output directory under local-data; existing paths are refused")
    args = parser.parse_args(argv)
    modes = ["normal", "sanitize"] if args.mode == "all" else [args.mode]

    missing = [shown(path) for path in (DRIVER,) + HEADERS if not path.is_file() or path.stat().st_size == 0]
    if missing:
        print("ce_museum_exhibition_tests: FAIL: missing or empty: %s" % ", ".join(missing))
        return 1
    if args.out is None:
        OUT_BASE.mkdir(parents=True, exist_ok=True)
        OUT = Path(tempfile.mkdtemp(prefix="run-", dir=str(OUT_BASE)))
    else:
        OUT = (args.out if args.out.is_absolute() else ROOT / args.out).resolve()
        if not OUT.is_relative_to((ROOT / "local-data").resolve()):
            parser.error("--out must be inside this checkout's local-data")
        try:
            OUT.mkdir(parents=True, exist_ok=False)
        except FileExistsError:
            parser.error("--out already exists; retain it and choose a fresh directory")

    cxx, origin = find_compiler(args.cxx)
    version, results = "", []
    if cxx is None:
        results = [{"mode": mode, "status": "UNAVAILABLE", "detail": origin} for mode in modes]
    else:
        code, out, err, note = run(cxx + ["--version"], OUT / "compiler.log", 60.0, env=build_env(OUT), cwd=str(OUT))
        lines = (out or err).strip().splitlines()
        if code != 0 or not lines:
            why = "the compiler %r does not run: %s" % (" ".join(cxx), note or "exit %s" % code)
            results = [{"mode": mode, "status": "UNAVAILABLE", "detail": why} for mode in modes]
        else:
            version = lines[0].strip()
            results = [run_mode(mode, cxx, args) for mode in modes]

    failed = [result for result in results if result["status"] == "FAIL"]
    passed = [result for result in results if result["status"] == "PASS"]
    absent = [result for result in results if result["status"] in ("UNAVAILABLE", "SKIPPED")]
    if failed:
        code, verdict = 1, "FAIL"
    elif absent and not (args.allow_unavailable and passed):
        code, verdict = 3, "INCOMPLETE"
    else:
        code, verdict = 0, "PASS"
    prints = set(result["plan_sha256"] for result in results if result.get("plan_sha256"))
    record = {"tool": "ce_museum_exhibition_tests", "verdict": verdict, "exit": code,
              "compiler": " ".join(cxx) if cxx else "", "compiler_named_by": origin if cxx else "",
              "compiler_version": version, "sources": [shown(path) for path in (DRIVER,) + HEADERS],
              "plan_print_same_in_every_mode": len(prints) <= 1, "modes": results}
    write_text(OUT / "results.json", json.dumps(record, indent=2, sort_keys=True) + "\n")

    print("ce_museum_exhibition_tests: compiler %s" % (
        "%s (named by %s): %s" % (" ".join(cxx), origin, version or "version unknown") if cxx else "none"))
    for result in results:
        print("  %-9s %-12s %s" % (result["mode"], result["status"], result["detail"]))
        if result.get("warnings"):
            print("  %-9s %-12s %s compiler warning(s), see %s" % (
                "", "note", result["warnings"], shown(OUT / result["mode"] / "build.log")))
        for line in result.get("failures", [])[:8]:
            print("            %s" % line)
    if len(prints) > 1:
        print("  note: the printed plan is not byte-identical between the builds (see plan-dump.txt in each)")
    print("  results   %s" % shown(OUT / "results.json"))
    print("ce_museum_exhibition_tests: %s (exit %d)" % (verdict, code))
    return code


if __name__ == "__main__":
    sys.exit(main())
