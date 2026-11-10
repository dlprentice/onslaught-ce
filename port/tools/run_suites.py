#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the suites that port/tools/suites.tsv lists, one process each, and record what each one did.

    python3 port/tools/run_suites.py --out DIR [--class release,optional] [--allow none,deps,game] [--only SUBSTRING ...]

The tables (port/tools/suites.tsv for port/, tools/suites.tsv for the repository tools) list every *_tests.py: path, how to start it (`script`, or `module`
for the tools/ files that import `tools.`), extra arguments, what it needs, its class, a time limit in seconds (`-` for the
--timeout default) and a note. Classes: release (gates the shipped build), optional (a suite the shipped build does not
depend on, such as the unfinished multiplayer line or decompilation tooling), witness (reproduces a defect against frozen
input and is not a pass or fail suite) and quarantined (known red or unrunnable; the note says why).

`needs` is `none`, or `+`-joined tokens: deps (the dependency cache, BEA_PORT_DATA), game (BEA_GAME_DIR, an isolated
game-data copy), mirror (BEA_SUITE_MIRROR), launcher (BEA_LAUNCHER: a launcher built with build.build_launcher), clone (a local bare clone of the
public release tags for updater_v2_tests), venv (local-data/venv's Python, or BEA_VENV_PYTHON), wine, xvfb, bwrap, vm and
fixture (an input this tree does not hold). A suite whose needs are not all in --allow is recorded as not-run, never as a pass.

Arguments may use {repo}, {data} (BEA_PORT_DATA), {game} (BEA_GAME_DIR), {mirror} (BEA_SUITE_MIRROR: the src folder of a
generated mirror, see premirror.py) {scratch} (a folder per suite under OUT/scratch, made for it) and {fresh} (a name inside that folder
that does not exist yet, for suites that insist on creating their own output folder).

Every row of OUT/result.tsv is written after its log is closed and carries the commit, the command, the exit code, the
seconds, the parsed unittest counts and the log's SHA-256, so a row and its log cannot come from different runs. A suite
fails when it exits non-zero, times out, ran zero tests or skipped every test it ran. A suite that is not a unittest run and
exits 0 is recorded as pass-exit-only: nothing in its output says how many checks ran, so that row is exit-code evidence and
nothing more (it must still leave a log, and a log line that begins with FAIL, FAILED, ERROR or a traceback fails it). A unittest
log whose summary says FAILED fails the suite whatever its exit code. Exit codes: 0 every selected suite passed, 1 a suite
failed, 3 nothing failed but a release-class suite was not run (a need not in --allow, or an input missing): the release table
is then incomplete. Output text is never copied into the
table: logs stay in OUT (private, ignored local-data) and a row keeps counts only. Run it inside the machine's heavy-job
wrappers (memory scope, heavy slot); it starts one suite at a time.
"""
import argparse
import hashlib
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TABLE = Path(__file__).resolve().with_name("suites.tsv")        # the port's suites; exported with port/
TOOLS_TABLE = ROOT / "tools/suites.tsv"                          # the repository tools' suites; present only where tools/ is
CLASSES = ("release", "optional", "witness", "quarantined")
NEEDS = ("none", "deps", "game", "mirror", "launcher", "clone", "venv", "wine", "xvfb", "bwrap", "vm", "fixture")
STYLES = ("script", "module")
COLUMNS = ("path", "style", "args", "needs", "class", "timeout", "note")
RESULT_COLUMNS = ("suite", "outcome", "exit", "seconds", "tests", "skipped", "commit", "command", "log_sha256", "log")


def default_tables():
    return [TABLE] + ([TOOLS_TABLE] if TOOLS_TABLE.exists() else [])


def read_tables(paths):
    rows = []
    for path in paths:
        rows += read_table(path)
    paths_seen = [r["path"] for r in rows]
    if len(paths_seen) != len(set(paths_seen)):
        raise ValueError("a suite is listed in two tables")
    return rows


def read_table(path=TABLE):
    rows = []
    for number, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        if len(cells) != len(COLUMNS):
            raise ValueError(f"{path}:{number}: {len(cells)} columns, expected {len(COLUMNS)}")
        row = dict(zip(COLUMNS, cells))
        needs = row["needs"].split("+")
        if row["class"] not in CLASSES or row["style"] not in STYLES or any(n not in NEEDS for n in needs):
            raise ValueError(f"{path}:{number}: unknown class, style or need in {row['path']}")
        if row["class"] in ("witness", "quarantined") and not row["note"].strip():
            raise ValueError(f"{path}:{number}: {row['class']} suite {row['path']} has no reason")
        if row["timeout"] != "-" and not row["timeout"].isdigit():
            raise ValueError(f"{path}:{number}: timeout must be seconds or -")
        row["needs"] = needs
        rows.append(row)
    paths = [r["path"] for r in rows]
    if len(paths) != len(set(paths)):
        raise ValueError(f"{path}: a suite is listed twice")
    return rows


def count_tests(text):
    """Sum the unittest summaries in a log: (ran, skipped), or None when the log holds no unittest summary."""
    ran = [int(n) for n in re.findall(r"^Ran (\d+) tests? in ", text, re.M)]
    if not ran:
        return None
    skipped = 0
    for group in re.findall(r"^(?:OK|FAILED) \(([^)]*)\)", text, re.M):
        skipped += sum(int(n) for n in re.findall(r"skipped=(\d+)", group))
    return sum(ran), skipped


FAILED_SUMMARY = re.compile(r"^FAILED \(", re.M)
FAILED_CASE = re.compile(r"^(?:FAIL|ERROR): \S+ \(", re.M)
SCRIPT_FAILURE = re.compile(r"^\W*(?:(?:FAIL|FAILED|ERROR)\b|Traceback \(most recent call last\))", re.M)


def judge(exit_code, text):
    """Outcome of one finished suite from its exit code and its log: (outcome, ran, skipped).

    The exit code alone never passes a suite: a unittest log that says FAILED (or names a failed case) fails it whatever
    the exit code, because unittest.main(exit=False) followed by sys.exit(0) hides a red run. A script that exits 0
    must leave a log, and one whose lines begin with FAIL, FAILED, ERROR or a traceback fails too. What is left,
    `pass-exit-only`, is exit-code evidence and nothing more."""
    counts = count_tests(text)
    ran, skipped = counts if counts else (None, None)
    if exit_code != 0:
        return "FAIL", ran, skipped
    if FAILED_SUMMARY.search(text) or FAILED_CASE.search(text):
        return "FAIL-reported", ran, skipped
    if counts is None:
        if not text.strip():
            return "FAIL-no-output", None, None
        if SCRIPT_FAILURE.search(text):
            return "FAIL-reported", None, None
        return "pass-exit-only", None, None
    if ran == 0:
        return "FAIL-zero-tests", ran, skipped
    if skipped >= ran:
        return "FAIL-all-skipped", ran, skipped
    return "pass", ran, skipped


def commit_id():
    head = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"],
                           capture_output=True, text=True).stdout.strip()
    return head + ("+dirty" if dirty else "")


def environment():
    env = dict(os.environ, BEA_REPORT_NO_OPEN="1", BROWSER="/bin/true", SDL_AUDIO_DRIVER="dummy",
               PYTHONDONTWRITEBYTECODE="1")
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(name, None)
    return env


def command_for(row, out, env, python):
    path = row["path"]
    if row["style"] == "module":
        base = [python, "-B", "-m", "unittest", path[:-3].replace("/", ".")]
    else:
        base = [python, "-B", path]
    scratch = out / "scratch" / path.replace("/", "__")
    fill = {"repo": str(ROOT), "data": env.get("BEA_PORT_DATA", ""), "game": env.get("BEA_GAME_DIR", ""),
            "scratch": str(scratch), "fresh": str(scratch / "run"), "mirror": env.get("BEA_SUITE_MIRROR", "")}
    extra = [word.format(**fill) for word in shlex.split(row["args"])] if row["args"] != "-" else []
    if "{scratch}" in row["args"] or "{fresh}" in row["args"]:
        scratch.mkdir(parents=True, exist_ok=True)       # {fresh} is a name inside it that does not exist yet
    return base + extra


def blocked(row, allow, env, venv_python):
    """Why this suite may not run here, or None."""
    missing = [n for n in row["needs"] if n != "none" and n not in allow]
    if missing:
        return "needs " + "+".join(missing) + " (not allowed in this run)"
    if "deps" in row["needs"] and not env.get("BEA_PORT_DATA"):
        return "needs deps: BEA_PORT_DATA is not set"
    if "game" in row["needs"] and not env.get("BEA_GAME_DIR"):
        return "needs game: BEA_GAME_DIR is not set"
    if "mirror" in row["needs"] and not env.get("BEA_SUITE_MIRROR"):
        return "needs mirror: BEA_SUITE_MIRROR is not set"
    if "clone" in row["needs"] and not (ROOT / "local-data/updater-v2-tests/onslaught-ce.git").is_dir():
        return "needs clone: local-data/updater-v2-tests/onslaught-ce.git (the public release tags) is missing"
    if "launcher" in row["needs"] and not Path(env.get("BEA_LAUNCHER", "")).is_file():
        return "needs launcher: BEA_LAUNCHER does not name a built launcher"
    if "venv" in row["needs"] and not venv_python.exists():
        return "needs venv: " + str(venv_python) + " is missing"
    return None


def run_one(row, out, env, python, timeout, commit):
    if row["timeout"] != "-":
        timeout = int(row["timeout"])
    command = command_for(row, out, env, python)
    log = out / "logs" / (row["path"].replace("/", "__") + ".log")
    log.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    with open(log, "wb") as stream:
        try:
            code = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, env=env,
                                  stdin=subprocess.DEVNULL, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            code = "TIMEOUT"
    seconds = int(time.monotonic() - started)
    data = log.read_bytes()
    if code == "TIMEOUT":
        outcome, ran, skipped = "FAIL-timeout", None, None
    else:
        outcome, ran, skipped = judge(code, data.decode("utf-8", "replace"))
    return dict(suite=row["path"], outcome=outcome, exit=code, seconds=seconds, tests="" if ran is None else ran,
                skipped="" if skipped is None else skipped, commit=commit, command=shlex.join(command[1:]),
                log_sha256=hashlib.sha256(data).hexdigest(), log=str(log))


def selected(rows, classes, only):
    chosen = [r for r in rows if r["class"] in classes]
    if only:
        chosen = [r for r in chosen if any(word in r["path"] for word in only)]
    return chosen


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--table", type=Path, action="append", help="a suites table (repeat for more); default: port/tools/suites.tsv and tools/suites.tsv where present")
    ap.add_argument("--out", type=Path, required=True, help="a new folder for result.tsv, logs/ and scratch/")
    ap.add_argument("--class", dest="classes", default="release", help="comma list of classes to run (default release)")
    ap.add_argument("--allow", default="none,deps,game", help="comma list of needs this run can meet (default none,deps,game)")
    ap.add_argument("--only", nargs="*", default=[], help="run only suites whose path contains one of these")
    ap.add_argument("--timeout", type=int, default=1800, help="seconds per suite without a time limit of its own in the table")
    ap.add_argument("--list", action="store_true", help="print the selection and exit")
    args = ap.parse_args(argv)
    rows = read_tables(args.table or default_tables())
    chosen = selected(rows, set(args.classes.split(",")), args.only)
    allow = set(args.allow.split(","))
    env = environment()
    venv_python = Path(os.environ.get("BEA_VENV_PYTHON", ROOT / "local-data/venv/bin/python"))
    if args.list:
        for r in chosen:
            print(r["path"], r["class"], "+".join(r["needs"]), blocked(r, allow, env, venv_python) or "runnable", sep="\t")
        return 0
    if args.out.exists() and any(args.out.iterdir()):
        print(f"{args.out} is not empty; use a fresh folder", file=sys.stderr)
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    commit = commit_id()
    result = args.out / "result.tsv"
    result.write_text("\t".join(RESULT_COLUMNS) + "\n")
    failed = 0
    unrun = []
    for row in chosen:
        why = blocked(row, allow, env, venv_python)
        if why:
            record = dict(suite=row["path"], outcome="not-run", exit="", seconds="", tests="", skipped="", commit=commit,
                          command=why, log_sha256="", log="")
        else:
            python = str(venv_python) if "venv" in row["needs"] else sys.executable
            record = run_one(row, args.out, env, python, args.timeout, commit)
        failed += record["outcome"].startswith("FAIL")
        if record["outcome"] == "not-run" and row["class"] == "release":
            unrun.append(row["path"])
        with open(result, "a") as stream:
            stream.write("\t".join(str(record[c]) for c in RESULT_COLUMNS) + "\n")
        print(record["outcome"], record["suite"], record["seconds"], flush=True)
    counts = {}
    for line in result.read_text().splitlines()[1:]:
        outcome = line.split("\t")[1]
        counts[outcome] = counts.get(outcome, 0) + 1
    print("outcomes:", ", ".join(f"{k} {v}" for k, v in sorted(counts.items())) or "none", flush=True)
    if failed:
        return 1
    if unrun:
        print(f"{len(unrun)} release suite(s) not run, so the release table is incomplete: " + ", ".join(unrun), file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
