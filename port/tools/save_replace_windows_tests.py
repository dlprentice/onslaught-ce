#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Windows all-or-nothing replace (port/platform/windows/save_replace.cpp) run for real, under Wine on Linux.

    python3 port/tools/save_replace_windows_tests.py [-v]

THIS IS WINE ON LINUX, NOT NATIVE WINDOWS AND NOT NTFS. The unmodified save_replace.cpp and files.cpp (the writable-folder
overlay and PortResolveUserPath) are cross-built for i686 Windows with the pinned llvm-mingw, linked with the save-name codec and
tests/save_replace_windows_driver.cpp, and the .exe is run by Wine in a private prefix: no display, program output sent to
files (Wine's service processes keep inherited pipes open), one persistent wineserver that is killed afterwards. Wine turns the
file calls into host calls: a rename is rename(2) on the host file system, FlushFileBuffers an fsync, share modes are tracked
by Wine's own server. So this suite checks the logic of the replace against a real file system: the order of operations, every
failure path, the removal of only the call's own temporary file, the retry policy, containment in the writable folder, and what
a crash leaves behind. It cannot show how NTFS journals a rename, whether MOVEFILE_WRITE_THROUGH or FlushFileBuffers reach the
disk, what a virus scanner or indexer does while a save is replaced, how a power loss or a pulled drive ends, or Windows' own
share-mode corner cases (this Wine refuses to replace a file that another handle has open, whatever share mode that handle
asked for; Windows allows the replace when every other handle shares FILE_SHARE_DELETE). The read-back goes through the same
handle and so the OS cache here: it checks what was written, not the medium. Native Windows and NTFS behaviour still has to be
checked on a Windows machine.

Faults come from tests/save_replace_windows_hooks.h, force-included only into save_replace.cpp in this test build: it redefines
WriteFile, FlushFileBuffers, ReadFile, CloseHandle (the temporary file's), MoveFileExW, DeleteFileW and Sleep, and reads its
plan from SAVE_REPLACE_FAULTS. Nothing in the port reads a fault variable (checked below). The hooks also record the arguments
of the temporary file's CreateFileW and of MoveFileExW, so what Wine cannot show the effect of (an exclusive CREATE_NEW file,
MOVEFILE_WRITE_THROUGH, a move within one folder) is at least asserted. Every case compares both roots,
file by file (size, SHA-256, modification time), before and after, and the game folder's whole tree (modes and change times
too) must be identical after every case. Mutants of save_replace.cpp (no read-back, in-place write, ignored flush or close
errors, no cleanup, no retry, a clobbered temporary name, a move without write-through, no read-only or folder refusal,
writing into the game folder) are built from text edits of a copy and must be caught by these same checks.

Needs the pinned llvm-mingw (deps.py build windows) and wine, wineserver and wineboot on PATH (the suite skips without Wine).
BEA_SAVE_TEST_WINEPREFIX=DIR reuses a prefix of your own (created if absent, kept); BEA_SAVE_TEST_KEEP=1 keeps the scratch
folder. Fixtures are authored pseudo-random bytes, never careers.
"""
import atexit
import concurrent.futures
import dataclasses
import hashlib
import itertools
import math
import os
from pathlib import Path
import random
import re
import resource
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
import unittest

import deps

PORT = deps.PORT
REPO = PORT.parent
SCRATCH = deps.WORK / "tests"
TOOLCHAIN = deps.DATA / "llvm-mingw/bin"
HEADER = PORT / "platform/common/save_replace.h"

_names = re.findall(r"^\s*(PORT_REPLACE_[A-Z]+)", HEADER.read_text(), re.M)
CODES = dict(zip(_names, range(len(_names))))
OK, REFUSED, CREATE, WRITE, SYNC, VERIFY, PUBLISH = (
    CODES["PORT_REPLACE_" + n] for n in ("OK", "REFUSED", "CREATE", "WRITE", "SYNC", "VERIFY", "PUBLISH"))
# Win32 error values the faults and the expectations name.
PATH_NOT_FOUND, ACCESS_DENIED, NOT_SAME_DEVICE, CRC, DISK_FULL, FILE_EXISTS, INVALID_NAME, IO_DEVICE = 3, 5, 17, 23, 112, 80, 123, 1117
INVALID_PARAMETER = 87
SHARING_VIOLATION, LOCK_VIOLATION = 32, 33

PRODUCTION = ("port/platform/common/save_replace.h", "port/platform/common/portlog.h", "port/platform/common/platform.h",
              "port/platform/common/save_names.h", "port/platform/common/save_names.cpp", "port/platform/windows/files.cpp",
              "port/platform/windows/save_namespace.cpp", "port/platform/windows/save_replace.cpp",
              "port/platform/windows/utf8.rc", "port/platform/windows/utf8.manifest", "port/platform/common/mods.h")
TEST_SOURCES = ("port/tools/tests/save_replace_windows_driver.cpp", "port/tools/tests/save_replace_windows_hooks.h",
                "port/tools/tests/mods_none.cpp")
REPLACE_SOURCE = "port/platform/windows/save_replace.cpp"
TEMP = re.compile(r"(?:.*/)?\.ce-write-\d+-\d+\.tmp")
GAME_MTIME, USER_MTIME = 1_000_000_000 * 10**9, 1_100_000_000 * 10**9   # long ago, so any rewrite shows


def sha(data):
    return hashlib.sha256(data).hexdigest()


def payload(tag, size):
    """Deterministic authored bytes: a readable header with CR LF, Ctrl-Z and NUL in it, then pseudo-random data."""
    head = f"{tag} sentinel\r\n\x1a\x00".encode("latin-1")
    body = random.Random(tag).randbytes(max(0, size - len(head)))
    return (head + body)[:size]


def winpath(path):
    """The Windows name of a Linux path under Wine's Z: drive."""
    return "Z:" + str(Path(path).resolve()).replace("/", "\\")


def put(path, data, mtime=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    if mtime is not None:
        os.utime(path, ns=(mtime, mtime))


def snapshot(root, deep=False, ctime=True):
    """{relative path: (kind, size, sha256, mtime_ns, inode)}; deep adds modes and change times (the game folder)."""
    out = {}
    root = Path(root)
    if not root.exists():
        return out
    entries = [(".", root)] if deep else []

    def refuse(error):
        raise error
    for folder, dirs, files in os.walk(root, followlinks=False, onerror=refuse):
        entries += [(os.path.relpath(Path(folder, n), root), Path(folder, n)) for n in dirs + files]
    for rel, p in entries:
        st = p.lstat()
        extra = (stat.S_IMODE(st.st_mode), st.st_ctime_ns if ctime else 0) if deep else ()
        if stat.S_ISLNK(st.st_mode):
            out[rel] = ("link", os.readlink(p), "", 0, 0) + extra
        elif stat.S_ISDIR(st.st_mode):
            out[rel] = ("dir", 0, "", st.st_mtime_ns if deep else 0, 0) + extra
        elif stat.S_ISREG(st.st_mode):
            out[rel] = ("file", st.st_size, sha(p.read_bytes()), st.st_mtime_ns, st.st_ino) + extra
        else:
            out[rel] = ("other", st.st_mode, "", 0, 0) + extra
    return out


def describe(entry):
    if entry is None:
        return "absent"
    kind, size, digest = entry[0], entry[1], entry[2]
    return f"{kind} {size} bytes {digest[:12]}" if kind == "file" else f"{kind} {size}".strip()


def same(expected, actual):
    """Entries equal, where None in the expected entry means "any"."""
    return actual is not None and len(expected) == len(actual) and all(e is None or e == a for e, a in zip(expected, actual))


def relax(root):
    """Make a scratch tree deletable again (Wine maps the read-only attribute to missing write permission)."""
    for folder, dirs, files in os.walk(root):
        for n in dirs + files:
            p = Path(folder, n)
            if not p.is_symlink():
                try:
                    p.chmod(0o755 if p.is_dir() else 0o644)
                except OSError:
                    pass


def remove_tree(path):
    path = Path(path)
    if path.exists():
        relax(path)
        shutil.rmtree(path, ignore_errors=True)


def contained(path):
    """The name files.cpp makes of a game path: either separator, "." and empty parts dropped, ".." taking the previous part
    away and never climbing above the root."""
    parts = []
    for part in re.split(r"[\\/]", path):
        if part == "..":
            if parts:
                parts.pop()
        elif part not in ("", "."):
            parts.append(part)
    return "/".join(parts)


def kv(text):
    return dict(part.split("=", 1) for part in text.split() if "=" in part)


# ---------------------------------------------------------------------------------------------------------------------------
# Building the test executable and its mutants


class Build:
    FLAGS = ["-std=c++17", "-O1", "-g", "-DNOMINMAX", "-D_WIN32_WINNT=0x0a00", "-fshort-wchar", "-mms-bitfields", "-Wall", "-Wextra",
             "-Wno-unused-parameter", "-Wno-unused-function"]

    def __init__(self, out):
        self.out = Path(out)
        self.cxx = TOOLCHAIN / "i686-w64-mingw32-clang++"
        self.windres = TOOLCHAIN / "i686-w64-mingw32-windres"
        for tool in (self.cxx, self.windres):
            if not tool.is_file():
                raise FileNotFoundError(f"{tool}: the pinned Windows cross toolchain is missing (python3 port/tools/deps.py build windows)")
        self.sources = {}
        self.warnings = {}
        self.exe = self.out / "driver.exe"

    def copy_sources(self):
        for rel in PRODUCTION + TEST_SOURCES:
            dest = self.out / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO / rel, dest)
            self.sources[rel] = sha(dest.read_bytes())

    def compile(self, source, obj, hooks=False):
        cmd = ["nice", "-n", "10", str(self.cxx), *self.FLAGS, f"-I{self.out / 'port/platform/common'}"]
        if hooks:
            cmd += ["-include", str(self.out / TEST_SOURCES[1])]
        cmd += ["-c", str(source), "-o", str(obj)]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if r.returncode:
            raise RuntimeError(f"compiling {source.name} failed:\n{r.stdout}{r.stderr}")
        self.warnings[Path(source).name] = r.stderr.count("warning:")
        return obj

    def link(self, objects, exe):
        manifest = self.out / "utf8.o"
        if not manifest.exists():
            r = subprocess.run([str(self.windres), "-i", str(self.out / "port/platform/windows/utf8.rc"), "-o", str(manifest), "-I",
                                str(self.out / "port/platform/windows")], capture_output=True, text=True, timeout=60)
            if r.returncode:
                raise RuntimeError("resource compile failed:\n" + r.stdout + r.stderr)
        r = subprocess.run(["nice", "-n", "10", str(self.cxx), "-municode", "-static", *map(str, objects), str(manifest), "-lwinmm",
                            "-lbcrypt", "-ladvapi32", "-o", str(exe)], capture_output=True, text=True, timeout=300)
        if r.returncode:
            raise RuntimeError("link failed:\n" + r.stdout + r.stderr)
        return exe

    def build(self):
        self.copy_sources()
        obj = self.out / "obj"
        obj.mkdir()
        jobs = [(self.out / "port/platform/common/save_names.cpp", False), (self.out / "port/platform/windows/save_namespace.cpp", False),
                (self.out / "port/platform/windows/files.cpp", False), (self.out / TEST_SOURCES[0], False),
                (self.out / TEST_SOURCES[2], False), (self.out / REPLACE_SOURCE, True)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            futures = [pool.submit(self.compile, src, obj / (src.stem + ".o"), hooks) for src, hooks in jobs]
            self.objects = [f.result() for f in futures]
        self.link(self.objects, self.exe)

    def variant(self, name, edits):
        """An executable whose save_replace.cpp is the production file with the given (old, new) text edits, each of which must
        match exactly once so that a change in the production file cannot silently turn a mutant into the original."""
        text = (self.out / REPLACE_SOURCE).read_text()
        for old, new in edits:
            if text.count(old) != 1:
                raise AssertionError(f"mutation anchor for {name} no longer matches exactly once: {old!r}")
            text = text.replace(old, new)
        source = self.out / f"port/platform/windows/save_replace.{name}.cpp"
        source.write_text(text)
        replaced = self.compile(source, self.out / "obj" / f"save_replace.{name}.o", hooks=True)
        others = [o for o in self.objects if o.name != "save_replace.o"]
        return self.link(others + [replaced], self.out / f"driver.{name}.exe")


# ---------------------------------------------------------------------------------------------------------------------------
# Wine


class Run:
    def __init__(self, returncode, stdout, stderr, seconds):
        self.returncode, self.stdout, self.stderr, self.seconds = returncode, stdout, stderr, seconds
        self.results = [kv(line[7:]) for line in stdout.splitlines() if line.startswith("RESULT ")]
        self.entries = [kv(line[6:]) for line in stdout.splitlines() if line.startswith("ENTRY ")]
        self.fields = self.results[-1] if self.results else {}
        self.log = [line for line in stderr.splitlines() if line.startswith("bea-port:")]

    def int(self, key):
        return int(self.fields[key])


class Process:
    """A running Windows program: output goes to anonymous files, never to pipes."""

    def __init__(self, argv, env, cwd, scratch):
        self.out, self.err = tempfile.TemporaryFile(dir=scratch), tempfile.TemporaryFile(dir=scratch)
        self.argv, self.started = argv, time.monotonic()
        self.proc = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=self.out, stderr=self.err, env=env, cwd=cwd,
                                     start_new_session=True)

    def wait(self, timeout=60):
        try:
            code = self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            code = None
            self.kill()
        text = []
        for f in (self.out, self.err):
            f.seek(0)
            text.append(f.read().decode("utf-8", "replace"))
            f.close()
        return Run(code, text[0], text[1], time.monotonic() - self.started)

    def kill(self):
        # Only the process group this object created (start_new_session makes the child its leader), and only while it is
        # still a Wine program.
        pid = self.proc.pid
        try:
            cmdline = Path(f"/proc/{pid}/cmdline").read_bytes()
            own_group = os.getpgid(pid) == pid and os.getpgid(0) != pid
        except OSError:
            return
        if own_group and (b"wine" in cmdline or b".exe" in cmdline):
            os.killpg(pid, signal.SIGKILL)
        self.proc.wait()


class Wine:
    def __init__(self, base):
        self.wine, self.wineserver, self.wineboot = (shutil.which(n) for n in ("wine", "wineserver", "wineboot"))
        supplied = os.environ.get("BEA_SAVE_TEST_WINEPREFIX")
        self.owned = not supplied
        self.prefix = Path(supplied).resolve() if supplied else Path(base) / "wineprefix"
        if self.prefix == (Path.home() / ".wine").resolve():
            raise RuntimeError("refusing to use the default Wine prefix")
        if self.prefix.exists() and any(e.name != ".t" for e in self.prefix.iterdir()) and not (self.prefix / "system.reg").exists():
            raise RuntimeError(f"{self.prefix} exists and is not a Wine prefix")
        self.private = self.prefix / ".t"
        self.scratch = Path(base) / "wine-out"
        self.scratch.mkdir(parents=True, exist_ok=True)
        self.started = False
        self.runs = 0

    def env(self, extra=None):
        drop = {"DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS", "WINEPREFIX", "WINEARCH",
                "WINEDEBUG", "WINEDLLOVERRIDES", "SAVE_REPLACE_FAULTS", "BEA_GAME_DIR", "BEA_USER_DIR"}
        env = {k: v for k, v in os.environ.items() if k not in drop}
        home = self.private / "home"
        env.update(WINEPREFIX=str(self.prefix), WINEDEBUG="-all", WINEDLLOVERRIDES="mscoree,mshtml=", HOME=str(home),
                   XDG_RUNTIME_DIR=str(self.private / "run"), XDG_CONFIG_HOME=str(home / ".config"),
                   XDG_DATA_HOME=str(home / ".local/share"), XDG_CACHE_HOME=str(home / ".cache"), LC_ALL="C.UTF-8")
        env.update(extra or {})
        return env

    def start(self, warm_exe):
        for d in ("home", "run"):
            (self.private / d).mkdir(parents=True, exist_ok=True)
        (self.private / "run").chmod(0o700)
        self.started = True
        if not (self.prefix / "system.reg").exists():
            run = Process([self.wineboot, "-i"], self.env(), self.prefix, self.scratch).wait(60)
            if run.returncode != 0:
                raise RuntimeError(f"wineboot -i failed ({run.returncode}):\n{run.stdout}{run.stderr}")
        subprocess.run([self.wineserver, "-p"], env=self.env(), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=30)
        # The first client of a server session starts Wine's services; it must not have a pipe to inherit.
        run = self.run(warm_exe, ["info"], {}, self.prefix, 60)
        if run.returncode != 0 or "acp=" not in run.stdout:
            raise RuntimeError(f"the warm-up run failed ({run.returncode}):\n{run.stdout}{run.stderr}")
        return run

    def popen(self, exe, args, extra_env=None, cwd=None):
        self.runs += 1
        argv = ["nice", "-n", "10", self.wine, winpath(exe), *map(str, args)]
        return Process(argv, self.env(extra_env), cwd or self.prefix, self.scratch)

    def run(self, exe, args, extra_env=None, cwd=None, timeout=60):
        return self.popen(exe, args, extra_env, cwd).wait(timeout)

    def lingering(self):
        """Wine processes of this prefix: the environment names it exactly (an entry that merely ends in WINEPREFIX= does not
        count) and the program is Wine's own or a Windows program Wine started (its command line is a Windows path)."""
        entry = b"WINEPREFIX=" + str(self.prefix).encode()
        found = []
        for e in os.scandir("/proc"):
            if not e.name.isdigit() or int(e.name) == os.getpid():
                continue
            try:
                if entry not in Path(e.path, "environ").read_bytes().split(b"\0"):
                    continue
                comm = Path(e.path, "comm").read_text().strip()
                argv0 = Path(e.path, "cmdline").read_bytes().split(b"\0")[0].decode(errors="replace")
            except OSError:
                continue
            if comm.startswith("wine") or re.match(r"[A-Za-z]:\\", argv0):
                found.append(int(e.name))
        return found

    def stop(self):
        """Stops this prefix's wineserver (wineserver -k with this prefix's environment signals only that server)."""
        report = []
        if self.started:
            for flag in ("-k", "-w"):
                subprocess.run([self.wineserver, flag], env=self.env(), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=60)
            deadline = time.monotonic() + 15
            while self.lingering() and time.monotonic() < deadline:
                time.sleep(0.2)
            for pid in self.lingering():       # last resort, for what wineserver -k left behind
                try:
                    cmd = Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").decode(errors="replace").strip()
                    os.kill(pid, signal.SIGKILL)
                    report.append(f"killed leftover pid {pid}: {cmd}")
                except OSError:
                    pass
        return report


# ---------------------------------------------------------------------------------------------------------------------------
# Cases and the checks every case gets


class Case:
    def __init__(self, root):
        self.root = Path(root)
        self.game, self.user, self.aux = (self.root / n for n in ("game", "user", "aux"))
        for d in (self.game, self.user, self.aux):
            d.mkdir(parents=True)
        (self.aux / "probe").mkdir()
        self.override = {}      # a scenario's setup may point the program at other roots

    def env(self):
        return {"BEA_GAME_DIR": winpath(self.game), "BEA_USER_DIR": winpath(self.user), **self.override}

    def fill(self, text):
        return text.format(aux=winpath(self.aux), probe=winpath(self.aux / "probe"), user=winpath(self.user),
                           game=winpath(self.game))


@dataclasses.dataclass
class Scenario:
    name: str
    new: bytes = None
    user: dict = dataclasses.field(default_factory=dict)       # files in the writable folder before: "savegames/ALPHA.bes" -> bytes
    game: dict = dataclasses.field(default_factory=dict)       # files in the game folder before
    target: str = r"savegames\ALPHA.bes"
    faults: str = ""
    what: str = "career"
    want: int = OK                  # the PORT_REPLACE_* result
    error: int = None               # the Windows error the log line must carry
    exit_code: int = 0              # 99: the process was killed by the fault plan
    stale: int = 0                  # ".ce-write-*.tmp" files that must remain afterwards
    stale_bytes: object = None      # what every one of them must hold: bytes, or a function of the new bytes
    calls: dict = dataclasses.field(default_factory=dict)      # expected hook counters: int, or (">=", n)
    command: str = "replace"
    args: tuple = ()
    setup: object = None            # function(case) after the files exist
    pre: object = None              # function(case) -> driver argument lists run through Wine before the call
    post: object = None             # likewise, after (undoing attributes)
    verify: object = None           # function(env, scenario, case, outcome) -> problems
    log_lines: int = None
    game_ctime: bool = True
    min_ms: int = 0


class Outcome:
    def __init__(self, scenario, case, exe, run, problems):
        self.scenario, self.case, self.exe, self.run, self.problems = scenario, case, exe, run, problems
        self.fields = run.fields

    def report(self):
        return (f"{self.scenario.name}: " + "; ".join(self.problems) + f"\n--- driver stdout\n{self.run.stdout}"
                f"--- driver stderr (exit {self.run.returncode})\n{self.run.stderr}")


LOG = re.compile(r"bea-port: (?P<what>\w+) was not saved: (?P<reason>.+) \(Windows error (?P<code>\d+)\); the previous file is unchanged")


class Environment:
    def __init__(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        self.base = Path(tempfile.mkdtemp(prefix="swr-", dir=SCRATCH))
        self.build = Build(self.base / "build")
        self.wine = Wine(self.base)
        self.cases = self.base / "cases"
        self.cases.mkdir()
        self.counter = itertools.count(1)
        self.notes = []
        self.sources_at_build = {}
        self.finished = False

    def start(self):
        t0 = time.monotonic()
        prefix_existed = (self.wine.prefix / "system.reg").exists()
        self.build.build()
        self.sources_at_build = dict(self.build.sources)
        t1 = time.monotonic()
        info = self.wine.start(self.build.exe)
        self.notes.append(f"built {len(self.build.objects)} objects and linked in {t1 - t0:.1f} s; Wine prefix "
                          f"{'reused' if prefix_existed else 'created'}, server warm after {time.monotonic() - t1:.1f} s (ANSI code page "
                          f"{info.fields.get('acp')}, {Path(self.wine.wine).name} {self.wine_version()}, host file system {self.host_fs()})")
        for rel in (REPLACE_SOURCE, "port/platform/windows/files.cpp", "port/platform/common/save_replace.h"):
            self.notes.append(f"tested source {rel} sha256 {self.sources_at_build[rel]}")
        warned = {k: v for k, v in self.build.warnings.items() if v}
        if warned:
            self.notes.append(f"compiler warnings (-Wall -Wextra): {warned}")

    @staticmethod
    def host_fs():
        r = subprocess.run(["stat", "-f", "-c", "%T", str(SCRATCH)], capture_output=True, text=True, timeout=30)
        return r.stdout.strip() or "unknown"

    def wine_version(self):
        r = subprocess.run([self.wine.wine, "--version"], capture_output=True, text=True, timeout=30)
        return r.stdout.strip()

    def stale_sources(self):
        return [rel for rel in PRODUCTION + TEST_SOURCES if sha((REPO / rel).read_bytes()) != self.sources_at_build.get(rel)]

    def stop(self):
        report = self.wine.stop()
        if os.environ.get("BEA_SAVE_TEST_KEEP"):
            print(f"scratch kept: {self.base}", file=sys.stderr)
        else:
            remove_tree(self.base)
            if not self.wine.owned and self.wine.private.exists():
                remove_tree(self.wine.private)
        return report

    def new_case(self):
        return Case(self.cases / f"c{next(self.counter):03d}")

    def driver(self, case, exe, args, faults="", timeout=60):
        extra = case.env()
        if faults:
            extra["SAVE_REPLACE_FAULTS"] = faults
        return self.wine.run(exe or self.build.exe, args, extra, case.root, timeout)

    # -- one scenario ---------------------------------------------------------------------------------------------------
    def evaluate(self, sc, exe=None):
        exe = exe or self.build.exe
        case = self.new_case()
        try:
            return self._evaluate(sc, exe, case)
        finally:
            if sc.post:
                for argv in sc.post(case):
                    self.driver(case, exe, argv)
            relax(case.root)

    def _evaluate(self, sc, exe, case):
        for rel, data in sc.game.items():
            put(case.game / rel, data, GAME_MTIME)
        for rel, data in sc.user.items():
            put(case.user / rel, data, USER_MTIME)
        if sc.setup:
            sc.setup(case)
        if sc.pre:
            for argv in sc.pre(case):
                pre = self.driver(case, exe, argv)
                if pre.returncode != 0:
                    raise AssertionError(f"{sc.name}: setup step {argv} failed:\n{pre.stdout}{pre.stderr}")
        new = sc.new if sc.new is not None else NEW
        put(case.aux / "payload.bin", new)
        before_user = snapshot(case.user)
        before_game = snapshot(case.game, deep=True, ctime=sc.game_ctime)
        target = case.fill(sc.target)
        argv = [sc.command, target, winpath(case.aux / "payload.bin"), *(case.fill(a) for a in sc.args)]
        if sc.command == "replace":
            argv.append(sc.what)
        run = self.driver(case, exe, argv, sc.faults)
        problems = self.check(sc, case, run, before_user, before_game, new)
        outcome = Outcome(sc, case, exe, run, problems)
        if sc.exit_code == 99 and run.returncode == 99:
            problems += self.after_a_crash(sc, case, exe, new)
        if sc.verify:
            problems += sc.verify(self, sc, case, outcome) or []
        final_game = snapshot(case.game, deep=True, ctime=sc.game_ctime)
        if final_game != before_game:
            problems.append("the game folder changed by the end of the case: " + self.differences(before_game, final_game))
        return outcome

    def check(self, sc, case, run, before_user, before_game, new):
        v = []
        if run.returncode is None:
            return ["the driver timed out"]
        if run.returncode != sc.exit_code:
            v.append(f"exit code {run.returncode}, expected {sc.exit_code}")
        target_rel = contained(case.fill(sc.target))
        after_user = snapshot(case.user)
        after_game = snapshot(case.game, deep=True, ctime=sc.game_ctime)
        if after_game != before_game:
            v.append("the game folder changed: " + self.differences(before_game, after_game))
        top = sorted(os.listdir(case.root))
        if top != ["aux", "game", "user"]:
            v.append(f"something was created beside the two roots: {top}")
        # the writable folder: the previous files exactly, the new bytes only if the call said OK
        expected = dict(before_user)
        if sc.want == OK and sc.exit_code == 0:
            expected[target_rel] = ("file", len(new), sha(new), None, None)
        temps = {rel: e for rel, e in after_user.items() if TEMP.fullmatch(rel)}
        rest = {rel: e for rel, e in after_user.items() if rel not in temps}
        parent = target_rel
        while "/" in parent:        # the folder the game also has is made on demand; nothing else may appear
            parent = parent.rsplit("/", 1)[0]
            if parent not in before_user and rest.get(parent, ("",))[0] == "dir":
                del rest[parent]
        bad = [f"{rel}: expected {describe(expected.get(rel))}, found {describe(rest.get(rel))}"
               for rel in sorted(set(expected) | set(rest)) if rel not in expected or not same(expected[rel], rest.get(rel))]
        if bad:
            v.append("the writable folder differs: " + "; ".join(bad))
        if len(temps) != sc.stale:
            v.append(f"{len(temps)} temporary files left, expected {sc.stale}: {sorted(temps)}")
        want_bytes = sc.stale_bytes(new) if callable(sc.stale_bytes) else sc.stale_bytes
        for rel, e in temps.items():
            if want_bytes is not None and e[2] != sha(want_bytes):
                v.append(f"{rel} holds {e[1]} bytes {e[2][:12]}, expected {len(want_bytes)} bytes {sha(want_bytes)[:12]}")
        if run.returncode == 99:
            return v
        # the driver's own report and the production log line
        if "result" not in run.fields:
            return v + ["no RESULT line from the driver"]
        if run.int("result") != sc.want:
            v.append(f"result {run.int('result')}, expected {sc.want}")
        for key, want in sc.calls.items():
            got = run.int(key)
            if (got < want[1]) if isinstance(want, tuple) else (got != want):
                v.append(f"hook counter {key} = {got}, expected {'>= ' + str(want[1]) if isinstance(want, tuple) else want}")
        if run.int("elapsed_ms") < sc.min_ms:
            v.append(f"the call took {run.int('elapsed_ms')} ms, expected at least {sc.min_ms} ms")
        expected_lines = sc.log_lines if sc.log_lines is not None else (0 if sc.want == OK else 1)
        if len(run.log) != expected_lines:
            v.append(f"{len(run.log)} log lines, expected {expected_lines}: {run.log}")
        leak = [line for line in run.log if re.search(r"ALPHA|BETA|savegames|\.ce-write|\.bes|\\|/", line.split(": ", 1)[1])]
        if leak:
            v.append(f"the log names a file or a path: {leak}")
        if sc.want != OK and run.log:
            m = LOG.fullmatch(run.log[-1])
            if not m:
                v.append(f"unexpected failure line: {run.log[-1]!r}")
            else:
                if m["what"] != sc.what:
                    v.append(f"the log says {m['what']!r}, expected {sc.what!r}")
                if sc.error is not None and int(m["code"]) != sc.error:
                    v.append(f"the log carries Windows error {m['code']}, expected {sc.error}")
        return v

    @staticmethod
    def differences(before, after):
        fields = ("kind", "size or target", "sha256", "mtime", "inode", "mode", "ctime")
        out = []
        for rel in sorted(set(before) | set(after)):
            a, b = before.get(rel), after.get(rel)
            if a != b:
                what = "appeared" if a is None else "vanished" if b is None else "changed " + ", ".join(
                    f for f, x, y in zip(fields, a, b) if x != y)
                out.append(f"{rel}: {what} ({describe(a)} -> {describe(b)})")
        return "; ".join(out[:6])

    # -- after a process was killed between the steps ------------------------------------------------------------------
    def after_a_crash(self, sc, case, exe, new):
        v = []
        saves = lambda root: {f.lower(): f for f in (os.listdir(root / "savegames") if (root / "savegames").is_dir() else [])
                              if f.lower().endswith(".bes") and (root / "savegames" / f).is_file()}
        careers = {**saves(case.game), **saves(case.user)}
        listed = [e["name"] for e in self.driver(case, exe, ["list", r"savegames\*.bes"]).entries]
        if listed != sorted(careers.values(), key=str.upper):
            v.append(f"the game's own listing after the crash is {listed}, expected {sorted(careers.values(), key=str.upper)}")
        raw = self.driver(case, exe, ["rawlist", winpath(case.user / "savegames" / "*.bes")])
        if sorted(e["name"] for e in raw.entries) != sorted(saves(case.user).values()):
            v.append(f"the native *.bes listing is {[e['name'] for e in raw.entries]}, expected {sorted(saves(case.user).values())}")
        alpha = case.user / "savegames" / "ALPHA.bes"
        read = self.driver(case, exe, ["read", sc.target, winpath(case.aux / "read.bin")])
        have = alpha if alpha.exists() else case.game / "savegames" / "ALPHA.bes"
        if have.exists():
            if read.fields.get("read") != str(have.stat().st_size) or (case.aux / "read.bin").read_bytes() != have.read_bytes():
                v.append("the game's own read no longer returns the surviving career")
        elif read.fields.get("read") != "failed":
            v.append("the game can read a career that does not exist")
        # the next save works and leaves the stale temporary files alone
        stale_before = {rel: e for rel, e in snapshot(case.user).items() if TEMP.fullmatch(rel)}
        after = payload("after-crash", 1234)
        put(case.aux / "after.bin", after)
        again = self.driver(case, exe, ["replace", sc.target, winpath(case.aux / "after.bin"), sc.what])
        if again.fields.get("result") != "0":
            v.append(f"the save after the crash returned {again.fields.get('result')}: {again.stderr}")
        elif alpha.read_bytes() != after:
            v.append("the save after the crash did not write exactly the new bytes")
        stale_after = {rel: e for rel, e in snapshot(case.user).items() if TEMP.fullmatch(rel)}
        if stale_after != stale_before:
            v.append(f"the save after the crash touched a stale temporary file: {sorted(stale_before)} -> {sorted(stale_after)}")
        return v


# ---------------------------------------------------------------------------------------------------------------------------
# The scenarios

OLD = payload("old-career", 1500)
NEW = payload("new-career", 1777)
LARGE = payload("new-large", 200_003)          # three full 64 KiB read-back chunks and a tail
GAME_OLD = payload("game-original", 1100)
BETA = payload("beta-career", 900)
ALPHA, BETA_BES = "savegames/ALPHA.bes", "savegames/BETA.bes"
# What Wine cannot show the effect of is asserted at the call: the temporary file is created new (CREATE_NEW = 1) for reading and
# writing (GENERIC_READ | GENERIC_WRITE) and exclusively (share mode 0), and the move replaces (MOVEFILE_REPLACE_EXISTING = 1)
# with write-through (MOVEFILE_WRITE_THROUGH = 8) from a name in the target's own folder.
CONTRACT = {"create_access": 0xC0000000, "create_share": 0, "create_disposition": 1, "move_flags": 9, "move_same_folder": 1}
ONE_EACH = {"create": 1, "write": 1, "flush": 1, "read": 1, "close": 1, "move": 1, "delete": 0, "sleep": 0, **CONTRACT}

# Where the target stands before the call.
STATES = {
    "existing": dict(user={ALPHA: OLD, BETA_BES: BETA}, game={ALPHA: GAME_OLD}),
    "absent": dict(user={BETA_BES: BETA}, game={}),
    "game-only": dict(user={BETA_BES: BETA}, game={ALPHA: GAME_OLD}),
    "game-only-no-user-folder": dict(user={}, game={ALPHA: GAME_OLD, BETA_BES: BETA}),
}


def readonly(path_of, mode):
    return lambda case: [["attrib", winpath(path_of(case)), mode]]


def user_alpha(case):
    return case.user / "savegames" / "ALPHA.bes"


def game_alpha(case):
    return case.game / "savegames" / "ALPHA.bes"


def verify_game_only(env, sc, case, out):
    v = []
    read = env.driver(case, out.exe, ["read", sc.target, winpath(case.aux / "read.bin")])
    if read.fields.get("read") != str(len(NEW)) or (case.aux / "read.bin").read_bytes() != NEW:
        v.append("the game's own read does not return the new user copy")
    listed = [e["name"] for e in env.driver(case, out.exe, ["list", r"savegames\*.bes"]).entries]
    if listed != ["ALPHA.bes", "BETA.bes"]:
        v.append(f"the listing after the save is {listed}, expected each career once")
    return v


def verify_readonly_stays(env, sc, case, out):
    q = env.driver(case, out.exe, ["attrib", winpath(user_alpha(case)), "query"])
    return [] if q.fields.get("readonly") == "1" else ["the read-only attribute did not survive the refusal"]


def verify_dotdot(env, sc, case, out):
    return [] if (case.user / "escape.bes").read_bytes() == NEW else ["the contained name did not land in the writable folder root"]


def verify_unicode(env, sc, case, out):
    return [] if (case.user / "savegames" / "\u00e9\u20ac.bes").exists() else ["the UTF-8 name was not created as written"]


def verify_collide(env, sc, case, out):
    pid, v = out.fields.get("pid"), []
    for k in range(1, int(out.fields.get("planted", "0")) + 1):
        f = case.user / "savegames" / f".ce-write-{pid}-{k}.tmp"
        if not f.exists() or f.read_bytes() != f"STALE-{k}".encode():
            v.append(f"the planted stale file {f.name} was changed or removed")
    return v


def verify_sharing(want_released):
    def verify(env, sc, case, out):
        if out.fields.get("probe") != "enforced":
            return []        # reported by the test, which skips
        released = out.run.int("released_at_ms")
        # held through at least one refused move; that it was let go in time is the call's success
        if want_released and released < 50:
            return [f"the other handle was released at {released} ms, expected about 60"]
        return []
    return verify


def make_symlink(case):
    (case.user / "savegames").mkdir(parents=True, exist_ok=True)
    os.symlink(game_alpha(case), user_alpha(case))


def make_hardlink(case):
    (case.user / "savegames").mkdir(parents=True, exist_ok=True)
    os.link(game_alpha(case), user_alpha(case))


def make_directory_target(case):
    (case.user / "savegames" / "ALPHA.bes").mkdir(parents=True)
    put(case.user / "savegames" / "ALPHA.bes" / "inside.txt", b"inside")


def remove_user_root(case):
    shutil.rmtree(case.user)


def user_root_is_the_game_folder(case):
    case.override = {"BEA_USER_DIR": winpath(case.game)}


def user_root_is_inside_the_game_folder(case):
    # files.cpp makes its writable folder at start-up, so a missing one would be created inside the game folder by the path
    # layer itself, before this code runs (a gap of its own, listed in the report). The folder exists here, so what is checked
    # is that PortReplaceUserFile writes nothing into it.
    (case.game / "ce-user").mkdir()
    case.override = {"BEA_USER_DIR": winpath(case.game / "ce-user")}


def user_root_is_spelled_differently(case):
    # the same folder as the game's, with the other slash, a trailing slash and another case: still the game folder
    case.override = {"BEA_USER_DIR": winpath(case.game).replace("\\", "/").upper() + "/"}


def make_folder_unwritable(case):
    if os.geteuid() == 0:
        raise unittest.SkipTest("root ignores directory permissions")
    (case.user / "savegames").chmod(0o555)


def verify_link_replaced(env, sc, case, out):
    p = user_alpha(case)
    if p.is_symlink() or p.read_bytes() != NEW:
        return ["the link was not replaced by a regular file with the new bytes"]
    return []


SCENARIOS = {}


def add(sc):
    assert sc.name not in SCENARIOS, sc.name
    SCENARIOS[sc.name] = sc
    return sc


def succeeds():
    add(Scenario("new-file", user={BETA_BES: BETA}, calls=ONE_EACH))
    add(Scenario("replace-existing", **STATES["existing"], calls=ONE_EACH))
    add(Scenario("game-only", **STATES["game-only"], calls=ONE_EACH, verify=verify_game_only))
    add(Scenario("game-only-no-user-folder", **STATES["game-only-no-user-folder"], calls=ONE_EACH, verify=verify_game_only))
    add(Scenario("game-only-read-only-original", **STATES["game-only"], pre=readonly(game_alpha, "ro"), post=readonly(game_alpha, "rw"),
                 calls=ONE_EACH))
    add(Scenario("options-file", target="defaultoptions.bea", what="options", user={"defaultoptions.bea": b"options old"},
                 game={"defaultoptions.bea": b"options game"}, calls=ONE_EACH))
    add(Scenario("user-root-missing", game={ALPHA: GAME_OLD}, setup=remove_user_root, calls=ONE_EACH))
    add(Scenario("unicode-name", target="savegames\\\u00e9\u20ac.bes", user={BETA_BES: BETA}, verify=verify_unicode, calls=ONE_EACH))
    add(Scenario("dot-dot-stays-inside", target=r"savegames\..\..\escape.bes", user={BETA_BES: BETA}, verify=verify_dotdot))
    for n in (1, 65535, 65536, 65537, 131072):
        add(Scenario(f"size-{n}", **STATES["existing"], new=payload(f"size-{n}", n),
                     calls={"read": math.ceil(n / 65536), "move": 1, "write": 1 if n else 0, **CONTRACT}))
    # a short write is not an error: the loop writes the rest
    add(Scenario("short-writes-small", **STATES["existing"], faults="write:*:half", calls={"write": (">=", 8), "move": 1}))
    add(Scenario("short-writes-large", **STATES["existing"], new=LARGE, faults="write:*:half", calls={"write": (">=", 10), "move": 1}))
    add(Scenario("write-in-13-chunks", **STATES["existing"], new=LARGE, faults="write:*:limit:16384",
                 calls={"write": 13, "read": 4, "move": 1}))
    # the move is retried for a refusing destination, briefly
    add(Scenario("move-retried-after-access-denied", **STATES["existing"], faults="move:1-3:fail:5",
                 calls={"move": 4, "sleep": 3, "slept_ms": 75}))
    add(Scenario("move-retried-after-sharing-violation", **STATES["existing"], faults="move:1:fail:32", calls={"move": 2, "sleep": 1}))
    add(Scenario("move-retried-after-lock-violation", **STATES["existing"], faults="move:1:fail:33", calls={"move": 2, "sleep": 1}))
    add(Scenario("temp-name-taken-twice", **STATES["existing"], faults="create:1-2:fail:80", calls={"create": 3, "move": 1}))
    # temporary names that already exist (left by a crashed run, even with this process's own id) are never touched
    add(Scenario("stale-temps-with-this-process-id", **STATES["existing"], command="collide", args=("2",), stale=2,
                 stale_bytes=None, calls={"create": 3, "move": 1}, verify=verify_collide))
    add(Scenario("sixteen-names-taken", **STATES["existing"], command="collide", args=("16",), want=CREATE, error=FILE_EXISTS,
                 stale=16, calls={"create": 16, "write": 0, "move": 0, "delete": 0}, verify=verify_collide))
    # a user copy that is a link to the game's original is replaced itself, never written through
    add(Scenario("symlink-to-the-game-original", user={BETA_BES: BETA}, game={ALPHA: GAME_OLD}, setup=make_symlink,
                 calls=ONE_EACH, verify=verify_link_replaced))
    add(Scenario("hard-link-to-the-game-original", user={BETA_BES: BETA}, game={ALPHA: GAME_OLD}, setup=make_hardlink,
                 calls=ONE_EACH, game_ctime=False, verify=verify_link_replaced))


def fails():
    # every state of the target; each failure must leave the previous file (or its absence) exactly as it was
    table = [
        ("create-denied", "create:1:fail:5", NEW, CREATE, ACCESS_DENIED, 0, {"create": 1, "write": 0, "move": 0, "delete": 0}),
        ("create-exhausted", "create:*:fail:80", NEW, CREATE, FILE_EXISTS, 0, {"create": 16, "write": 0, "move": 0, "delete": 0}),
        ("write-disk-full-first-small", "write:1:fail:112", NEW, WRITE, DISK_FULL, 0, {"write": 1, "flush": 0, "move": 0, "delete": 1}),
        ("write-disk-full-first-large", "write:1:fail:112", LARGE, WRITE, DISK_FULL, 0, {"write": 1, "flush": 0, "move": 0, "delete": 1}),
        ("write-disk-full-later-small", "write:1:limit:700;write:2:fail:112", NEW, WRITE, DISK_FULL, 0,
         {"write": 2, "flush": 0, "move": 0, "delete": 1}),
        ("write-disk-full-later-large", "write:1:limit:65536;write:2:limit:65536;write:3:fail:112", LARGE, WRITE, DISK_FULL, 0,
         {"write": 3, "flush": 0, "move": 0, "delete": 1}),
        ("write-reports-zero-bytes", "write:1:zero", NEW, WRITE, None, 0, {"write": 1, "flush": 0, "move": 0, "delete": 1}),
        ("flush-fails", "flush:1:fail:1117", NEW, SYNC, IO_DEVICE, 0, {"flush": 1, "read": 0, "move": 0, "delete": 1}),
        ("verify-flip-small", "read:1:flip", NEW, VERIFY, CRC, 0, {"read": 1, "close": 1, "move": 0, "delete": 1}),
        ("verify-flip-later-chunk", "read:3:flip", LARGE, VERIFY, CRC, 0, {"read": 3, "move": 0, "delete": 1}),
        ("verify-drop-small", "write:1:drop", NEW, VERIFY, CRC, 0, {"write": 1, "read": 0, "move": 0, "delete": 1}),
        ("verify-drop-later-chunk", "write:1:limit:70000;write:2:drop", LARGE, VERIFY, CRC, 0, {"write": 2, "move": 0, "delete": 1}),
        ("verify-corrupt-small", "write:1:corrupt", NEW, VERIFY, CRC, 0, {"read": 1, "move": 0, "delete": 1}),
        ("verify-corrupt-later-chunk", "write:1:limit:100000;write:2:corrupt", LARGE, VERIFY, CRC, 0, {"read": 3, "move": 0}),
        ("verify-early-end-of-file", "read:1:eof", NEW, VERIFY, CRC, 0, {"read": 1, "move": 0, "delete": 1}),
        ("verify-read-error", "read:1:fail:1117", NEW, VERIFY, CRC, 0, {"read": 1, "move": 0, "delete": 1}),
        ("close-fails", "close:1:fail:1117", NEW, WRITE, IO_DEVICE, 0, {"close": 1, "move": 0, "delete": 1}),
        ("move-not-retried-for-another-device", "move:*:fail:17", NEW, PUBLISH, NOT_SAME_DEVICE, 0,
         {"move": 1, "sleep": 0, "delete": 1}),
        ("move-refused-eight-times", "move:*:fail:5", NEW, PUBLISH, ACCESS_DENIED, 0, {"move": 8, "sleep": 7, "slept_ms": 175, "delete": 1}),
        ("move-sharing-violation-eight-times", "move:*:fail:32", NEW, PUBLISH, SHARING_VIOLATION, 0, {"move": 8, "sleep": 7}),
        ("move-lock-violation-eight-times", "move:*:fail:33", NEW, PUBLISH, LOCK_VIOLATION, 0, {"move": 8, "sleep": 7}),
        ("move-retried-then-fails-for-good", "move:1-2:fail:5;move:3:fail:112", NEW, PUBLISH, DISK_FULL, 0, {"move": 3, "sleep": 2}),
        ("temp-cannot-be-removed-either", "write:1:fail:112;delete:1:fail:5", NEW, WRITE, DISK_FULL, 1, {"delete": 1}),
    ]
    for name, faults, new, want, error, stale, calls in table:
        for state, files in STATES.items():
            add(Scenario(f"{name}/{state}", **files, new=new, faults=faults, want=want, error=error, stale=stale, calls=calls,
                         log_lines=2 if stale else None))
    for name, faults, want, error in (("write-disk-full-first-small", "write:1:fail:112", WRITE, DISK_FULL),
                                      ("verify-flip-small", "read:1:flip", VERIFY, CRC)):
        add(Scenario(f"{name}/options-file", target="defaultoptions.bea", what="options", user={"defaultoptions.bea": b"options old"},
                     game={"defaultoptions.bea": b"options game"}, faults=faults, want=want, error=error))
    # targets that are not a plain writable file
    add(Scenario("refuse-folder-in-the-way", user={BETA_BES: BETA}, setup=make_directory_target, want=REFUSED, error=ACCESS_DENIED,
                 calls={"create": 0, "move": 0}))
    add(Scenario("refuse-read-only-career", **STATES["existing"], pre=readonly(user_alpha, "ro"), post=readonly(user_alpha, "rw"),
                 want=REFUSED, error=ACCESS_DENIED, calls={"create": 0, "write": 0, "flush": 0, "read": 0, "close": 0, "move": 0,
                                                           "delete": 0, "sleep": 0}, verify=verify_readonly_stays))
    add(Scenario("refuse-empty-payload", **STATES["existing"], new=b"", want=REFUSED, error=INVALID_PARAMETER,
                 calls={"create": 0, "write": 0, "move": 0}))
    # the writable folder may not be, or lie inside, the game folder: the game's own files are never written
    for name, setup in (("equal-to", user_root_is_the_game_folder), ("inside", user_root_is_inside_the_game_folder),
                        ("the-same-folder-spelled-differently-as", user_root_is_spelled_differently)):
        add(Scenario(f"refuse-writable-folder-{name}-the-game-folder", user={}, game={ALPHA: GAME_OLD}, setup=setup, want=REFUSED,
                     error=ACCESS_DENIED, calls={"create": 0, "write": 0, "move": 0}, log_lines=2))
    add(Scenario("refuse-absolute-path", user={BETA_BES: BETA}, target="{aux}\\abs-target.bes", want=REFUSED, error=INVALID_NAME,
                 calls={"create": 0}))
    add(Scenario("refuse-rooted-path", user={BETA_BES: BETA}, target="\\rooted.bes", want=REFUSED, error=INVALID_NAME, calls={"create": 0}))
    add(Scenario("refuse-empty-path", user={BETA_BES: BETA}, target="", want=REFUSED, error=INVALID_NAME, calls={"create": 0}))
    add(Scenario("refuse-overlong-name", user={BETA_BES: BETA}, target="savegames\\" + "A" * 300 + ".bes", want=REFUSED,
                 error=INVALID_NAME, calls={"create": 0}))
    add(Scenario("no-folder-in-either-root", target=r"savegames\ALPHA.bes", want=CREATE, error=PATH_NOT_FOUND,
                 calls={"create": 1, "write": 0, "move": 0}))
    # a real refusal by the file system, no hook: the writable savegames folder does not let the program create files
    add(Scenario("real-folder-not-writable", **STATES["existing"], setup=make_folder_unwritable, want=CREATE, error=ACCESS_DENIED,
                 calls={"create": 1, "write": 0, "move": 0, "delete": 0}))


def crashes():
    table = [
        ("crash-creating-the-temporary-file", "create:1:crash", 0, None),
        ("crash-before-the-first-write", "write:1:crash", 1, b""),
        ("crash-in-the-middle-of-the-write", "write:1:limit:700;write:2:crash", 1, lambda new: new[:700]),
        ("crash-before-the-flush", "flush:1:crash", 1, lambda new: new),
        ("crash-before-the-read-back", "read:1:crash", 1, lambda new: new),
        ("crash-before-the-close", "close:1:crash", 1, lambda new: new),
        ("crash-before-the-move", "move:1:crash", 1, lambda new: new),
    ]
    for name, faults, stale, content in table:
        for state in ("existing", "absent", "game-only"):
            add(Scenario(f"{name}/{state}", **STATES[state], faults=faults, exit_code=99, stale=stale, stale_bytes=content))


def sharing():
    add(Scenario("sharing-violation-never-released", **STATES["existing"], command="hold", args=("{probe}", "-1"), want=PUBLISH,
                 error=ACCESS_DENIED, calls={"move": 8, "sleep": 7, "slept_ms": 175, "delete": 1}, min_ms=150, verify=verify_sharing(False)))
    add(Scenario("sharing-violation-released-during-the-retries", **STATES["existing"], command="hold", args=("{probe}", "60"),
                 want=OK, calls={"move": (">=", 2), "sleep": (">=", 1), "delete": 0}, min_ms=50, verify=verify_sharing(True)))


succeeds()
fails()
crashes()
sharing()


# ---------------------------------------------------------------------------------------------------------------------------
# Mutants of save_replace.cpp: each must be caught by the scenarios named, and still pass its controls

CREATE_TEMP = "h = CreateFileW(temp, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);"
MOVE_CALL = "if (MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))"
ATTRIBUTES = "attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY)"
CONTROLS = ("replace-existing", "new-file")


@dataclasses.dataclass
class Mutant:
    name: str
    why: str
    edits: tuple
    catches: tuple
    controls: tuple = CONTROLS
    ignore: tuple = ()         # hook counters the mutant changes by design, not compared for its controls


MUTANTS = [
    Mutant("no-read-back", "skips the read-back comparison", (("if (!step && !ReadsBack(h, bytes, size))",
           "if (!step && false && !ReadsBack(h, bytes, size))"),),
           ("verify-flip-small/existing", "verify-flip-later-chunk/existing", "verify-drop-small/existing",
            "verify-drop-later-chunk/existing", "verify-corrupt-small/existing", "verify-corrupt-later-chunk/existing"),
           CONTROLS, ("read",)),
    Mutant("move-without-write-through", "asks for the move without MOVEFILE_WRITE_THROUGH (Wine cannot tell)",
           (("MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))", "MOVEFILE_REPLACE_EXISTING))"),), ("replace-existing",), CONTROLS,
           ("move_flags",)),
    Mutant("write-in-place", "writes straight into the target: no temporary file, no move",
           ((CREATE_TEMP, "h = CreateFileW(target, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, "
             "FILE_ATTRIBUTE_NORMAL, NULL);"),
            (MOVE_CALL, "if ((MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), true))")),
           ("write-disk-full-first-small/existing", "write-disk-full-later-small/existing", "write-disk-full-later-large/existing",
            "flush-fails/existing", "crash-before-the-move/existing", "crash-in-the-middle-of-the-write/existing"), CONTROLS,
           ("create_share", "create_disposition")),
    Mutant("flush-ignored", "ignores a failed FlushFileBuffers", (("if (!step && !FlushFileBuffers(h))",
           "if (!step && (FlushFileBuffers(h), false))"),), ("flush-fails/existing",)),
    Mutant("close-error-ignored", "ignores a failed CloseHandle", (("if (!CloseHandle(h) && !step)",
           "if ((CloseHandle(h), false) && !step)"),), ("close-fails/existing",)),
    Mutant("temp-left-behind", "does not remove its temporary file on failure", (("if (!DeleteFileW(temp))",
           "if (false && !DeleteFileW(temp))"),), ("write-disk-full-first-small/existing", "move-not-retried-for-another-device/existing")),
    Mutant("no-retry", "gives up on the first refused move", (("if (attempt >= 7 || (error != ERROR_ACCESS_DENIED",
           "if (attempt >= 0 || (error != ERROR_ACCESS_DENIED"),),
           ("move-retried-after-access-denied", "move-refused-eight-times/existing", "sharing-violation-released-during-the-retries"),
           CONTROLS + ("move-not-retried-for-another-device/existing",)),
    Mutant("clobbers-temp-names", "creates its temporary file over an existing one", ((CREATE_TEMP,
           CREATE_TEMP.replace("CREATE_NEW", "CREATE_ALWAYS")),), ("stale-temps-with-this-process-id", "sixteen-names-taken"),
           CONTROLS, ("create_disposition",)),
    Mutant("read-only-allowed", "no longer refuses a read-only career", ((ATTRIBUTES, "attributes & FILE_ATTRIBUTE_DIRECTORY"),),
           ("refuse-read-only-career",)),
    Mutant("folder-allowed", "no longer refuses a folder", ((ATTRIBUTES, "attributes & FILE_ATTRIBUTE_READONLY"),),
           ("refuse-folder-in-the-way",)),
    Mutant("containment-removed", "writes into a writable folder that is inside the game folder",
           (("if (Inside(PortUserDir(), PortGameDir()))", "if (false && Inside(PortUserDir(), PortGameDir()))"),),
           ("refuse-writable-folder-equal-to-the-game-folder", "refuse-writable-folder-inside-the-game-folder",
            "refuse-writable-folder-the-same-folder-spelled-differently-as-the-game-folder")),
    Mutant("empty-payload-allowed", "publishes an empty buffer over the career", (("if (!data || !size)", "if (!data)"),),
           ("refuse-empty-payload",)),
    Mutant("writes-the-game-folder", "resolves the target in the game folder, not the writable one",
           (("if (PortResolveUserPath(game_path, path, sizeof(path)) != 0)",
             'if ((snprintf(path, sizeof(path), "%s\\\\%s", getenv("BEA_GAME_DIR"), game_path), 0) != 0)'),),
           ("replace-existing", "game-only"), ()),
]


for _m in MUTANTS:
    for _name in _m.catches + _m.controls:
        assert _name in SCENARIOS, f"mutant {_m.name} names the unknown scenario {_name}"

# ---------------------------------------------------------------------------------------------------------------------------
# The tests

ENV = None


def setUpModule():
    global ENV
    missing = [n for n in ("wine", "wineserver", "wineboot") if not shutil.which(n)]
    if missing:
        raise unittest.SkipTest("Wine is not installed here (" + ", ".join(missing) + " not on PATH); this suite runs the Windows build under Wine")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    ENV = Environment()
    unittest.addModuleCleanup(finish)
    atexit.register(finish, quiet=True)      # an interrupted run still stops its wineserver and removes its scratch folder
    ENV.start()


def finish(quiet=False):
    env = ENV
    if env is None or env.finished:
        return
    env.finished = True
    try:
        report = env.stop()
    except Exception as error:      # still show what was learned
        report = [f"cleanup problem: {error!r}"]
    stale = env.stale_sources()
    env.notes.append(f"{env.wine.runs} Windows processes were started through Wine ({next(env.counter) - 1} cases)")
    if not quiet:
        print("\nEvidence", file=sys.stderr)
        for line in env.notes + report:
            print("  " + line, file=sys.stderr)
    if stale and not quiet:
        raise AssertionError(f"these sources changed while the suite ran, so this run tested stale binaries: {stale}")


def note(text):
    ENV.notes.append(text)


class WineCase(unittest.TestCase):
    def clean(self, out):
        if out.problems:
            self.fail(out.report())

    def scenario(self, name, exe=None):
        return ENV.evaluate(SCENARIOS[name], exe)

    def each(self, names):
        for name in names:
            with self.subTest(scenario=name):
                self.clean(self.scenario(name))


class SourceHygiene(WineCase):
    def test_production_code_reads_no_fault_variable_and_never_includes_the_hooks(self):
        for rel in ("port/platform/windows/save_replace.cpp", "port/platform/common/save_replace.h"):
            text = (REPO / rel).read_text()
            for banned in ("getenv", "GetEnvironmentVariable", "SAVE_REPLACE_FAULTS", "Hook_", "TerminateProcess", "save_replace_windows"):
                self.assertNotIn(banned, text, f"{rel} mentions {banned}")
        allowed = {REPO / rel for rel in TEST_SOURCES} | {Path(__file__).resolve()}
        for folder, _, files in os.walk(PORT):
            for name in files:
                p = Path(folder, name)
                if p.suffix in (".cpp", ".h", ".c", ".py", ".sh", ".patch", ".txt") and p.resolve() not in allowed:
                    try:
                        self.assertNotIn("save_replace_windows_hooks", p.read_text(errors="replace"), f"{p} includes the test hooks")
                    except OSError:
                        pass

    def test_the_test_executable_was_built_from_the_production_files_as_they_are(self):
        for rel in PRODUCTION:
            self.assertEqual(sha((REPO / rel).read_bytes()), ENV.sources_at_build[rel], rel)

    def test_the_driver_resolves_the_roots_it_was_given_and_runs_the_utf8_manifest(self):
        case = ENV.new_case()
        run = ENV.driver(case, None, ["info"])
        self.assertEqual(0, run.returncode, run.stderr)
        self.assertEqual((winpath(case.game), winpath(case.user)), (run.fields["game"], run.fields["user"]))
        self.assertEqual("65001", run.fields["acp"])


class Publishes(WineCase):
    def test_a_new_career_in_an_existing_savegames_folder(self):
        self.each(["new-file"])

    def test_replacing_an_existing_career_leaves_exactly_the_new_bytes(self):
        self.each(["replace-existing"])

    def test_a_career_that_exists_only_in_the_game_folder_gets_a_user_copy_and_the_original_is_untouched(self):
        self.each(["game-only", "game-only-no-user-folder", "game-only-read-only-original"])

    def test_the_options_file(self):
        self.each(["options-file"])

    def test_a_missing_writable_folder_is_made_and_a_utf8_name_survives(self):
        self.each(["user-root-missing", "unicode-name"])

    def test_dot_dot_cannot_climb_out_of_the_writable_folder(self):
        self.each(["dot-dot-stays-inside"])

    def test_sizes_around_the_read_back_chunk(self):
        self.each([f"size-{n}" for n in (1, 65535, 65536, 65537, 131072)])

    def test_short_writes_are_looped_until_everything_is_written(self):
        self.each(["short-writes-small", "short-writes-large", "write-in-13-chunks"])

    def test_a_refused_move_is_retried_and_succeeds(self):
        self.each(["move-retried-after-access-denied", "move-retried-after-sharing-violation", "move-retried-after-lock-violation"])

    def test_a_temporary_name_that_is_taken_is_skipped(self):
        self.each(["temp-name-taken-twice", "stale-temps-with-this-process-id"])

    def test_a_link_to_the_game_original_is_replaced_not_written_through(self):
        self.each(["symlink-to-the-game-original", "hard-link-to-the-game-original"])


class Failures(WineCase):
    def test_every_failure_leaves_the_previous_state_in_every_starting_state(self):
        names = [n for n in SCENARIOS if "/" in n and not n.startswith("crash-") and not n.endswith("/options-file")]
        self.assertEqual(23 * len(STATES), len(names))
        self.each(names)

    def test_options_file_failures_name_options_in_the_log(self):
        self.each([n for n in SCENARIOS if n.endswith("/options-file")])

    def test_sixteen_taken_names_fail_without_touching_any_of_them(self):
        self.each(["sixteen-names-taken"])

    def test_targets_that_cannot_be_written_are_refused_before_a_temporary_file_exists(self):
        self.each(["refuse-folder-in-the-way", "refuse-read-only-career", "refuse-absolute-path", "refuse-rooted-path",
                   "refuse-empty-path", "refuse-overlong-name"])

    def test_an_empty_buffer_and_a_writable_folder_inside_the_game_folder_are_refused(self):
        self.each(["refuse-empty-payload", "refuse-writable-folder-equal-to-the-game-folder",
                   "refuse-writable-folder-inside-the-game-folder",
                   "refuse-writable-folder-the-same-folder-spelled-differently-as-the-game-folder"])

    def test_a_savegames_folder_that_exists_nowhere_is_not_invented(self):
        self.each(["no-folder-in-either-root"])

    def test_a_folder_the_file_system_will_not_let_it_write_in_fails_without_touching_the_career(self):
        self.each(["real-folder-not-writable"])


class Crashes(WineCase):
    def test_a_process_killed_between_any_two_steps_keeps_the_old_career_and_leaves_no_listed_ghost(self):
        names = [n for n in SCENARIOS if n.startswith("crash-")]
        self.assertEqual(21, len(names))
        self.each(names)


class RealSharing(WineCase):
    """A handle opened without FILE_SHARE_DELETE holds the target; no hook is involved, the OS refuses the move."""

    def probe(self, name):
        out = ENV.evaluate(SCENARIOS[name])
        state = out.fields.get("probe", "no answer")
        if state != "enforced":
            self.skipTest(f"this Wine does not refuse to replace a file another handle holds open (probe: {state}, probe error "
                          f"{out.fields.get('probe_error')}); the call returned {out.fields.get('result')}. Not faked.")
        return out

    def test_a_target_held_open_without_delete_sharing_fails_the_publish_after_its_retries(self):
        out = self.probe("sharing-violation-never-released")
        note(f"sharing: probe says enforced (move over a held file fails with error {out.fields['probe_error']}); held for the whole "
             f"call -> result {out.fields['result']} after {out.fields['move']} move attempts and {out.fields['sleep']} sleeps, "
             f"{out.fields['elapsed_ms']} ms")
        self.clean(out)

    def test_the_same_target_released_during_the_retries_is_replaced(self):
        out = self.probe("sharing-violation-released-during-the-retries")
        note(f"sharing: released after {out.fields['released_at_ms']} ms -> result {out.fields['result']} on move attempt "
             f"{out.fields['move']} after {out.fields['elapsed_ms']} ms")
        self.clean(out)


def observe_pair(exe, count, pause_ms):
    """One process replaces the target `count` times alternating payloads A and B while another reads it in a loop (pausing
    pause_ms between reads) and classifies everything it reads. Returns (case, writer run, reader run, A, B, target)."""
    case = ENV.new_case()
    a, b = payload("reader-A", 20480), payload("reader-B", 20480)
    put(case.user / "savegames" / "ALPHA.bes", a, USER_MTIME)
    put(case.game / "savegames" / "BETA.bes", BETA, GAME_MTIME)
    put(case.aux / "a.bin", a)
    put(case.aux / "b.bin", b)
    target = case.user / "savegames" / "ALPHA.bes"
    ready, stop = case.aux / "ready.flag", case.aux / "stop.flag"
    reader = ENV.wine.popen(exe, ["reader", winpath(target), winpath(case.aux / "a.bin"), winpath(case.aux / "b.bin"),
                                  winpath(ready), winpath(stop), pause_ms], case.env(), case.root)
    try:
        deadline = time.monotonic() + 30
        while not ready.exists() and time.monotonic() < deadline and reader.proc.poll() is None:
            time.sleep(0.02)
        if not ready.exists():
            raise AssertionError("the reader never read the target")
        writer = ENV.wine.run(exe, ["writer", r"savegames\ALPHA.bes", winpath(case.aux / "a.bin"), winpath(case.aux / "b.bin"), count],
                              case.env(), case.root, timeout=60)
    finally:
        stop.write_bytes(b"1")
        read = reader.wait(60)
    return case, writer, read, a, b, target


class ConcurrentReader(WineCase):
    """Atomicity as another process sees it: whatever a reader manages to read while the career is replaced is complete."""

    def run_pair(self, count, pause_ms, min_ok):
        case, writer, read, a, b, target = observe_pair(ENV.build.exe, count, pause_ms)
        self.assertEqual(0, writer.returncode, writer.stderr)
        self.assertEqual(0, read.returncode, read.stderr)
        self.assertEqual("flag", read.fields["stopped_by"], read.stdout)
        w, r = writer.fields, read.fields
        # atomicity: whatever the reader saw was complete, and the writer only ever succeeded or was refused the move
        for key in ("empty", "short", "mixed", "read_errors"):
            self.assertEqual("0", r[key], f"the reader saw {key} files: {read.stdout}")
        self.assertEqual("0", w["other"], writer.stdout)
        self.assertEqual(count, int(w["ok"]) + int(w["publish"]), writer.stdout)
        self.assertGreaterEqual(int(w["ok"]), min_ok, f"too few replaces went through to mean anything: {writer.stdout}")
        # the reader really overlapped the replaces: it saw the career change from one payload to the other
        self.assertGreaterEqual(int(r["switches"]), int(w["ok"]) // 4, f"the reader saw too few changes: {read.stdout}")
        self.assertTrue(int(r["a"]) > 0 and int(r["b"]) > 0, f"the reader never saw both payloads: {read.stdout}")
        final = target.read_bytes()
        self.assertEqual({"A": a, "B": b, "-": a}[w["last_ok"]], final, "the target is not the last payload the writer published")
        self.assertEqual([], [p.name for p in (case.user / "savegames").iterdir() if p.name != "ALPHA.bes"], "temporary files left")
        self.assertEqual(sorted(os.listdir(case.root)), ["aux", "game", "user"])
        self.assertEqual({"savegames/BETA.bes"}, {k for k, e in snapshot(case.game).items() if e[0] == "file"})
        relax(case.root)
        return w, r

    def test_a_reader_never_sees_a_short_empty_or_mixed_career_while_it_is_replaced_200_times(self):
        w, r = self.run_pair(200, "1", 150)
        note(f"concurrent reader (1 ms between reads), 200 replaces: {r['iterations']} reads = {r['a']} A + {r['b']} B with "
             f"{r['switches']} changes, 0 short/empty/mixed, {r['open_failed']} failed opens (errors {r['open_errors']}); writer: "
             f"{w['ok']} replaced, {w['publish']} gave up on the move, {w['sleep']} refused moves retried, {w['elapsed_ms']} ms")

    def test_the_same_with_a_reader_that_never_pauses(self):
        w, r = self.run_pair(200, "0", 20)
        note(f"concurrent reader (tight loop), 200 replaces: {r['iterations']} reads = {r['a']} A + {r['b']} B with {r['switches']} "
             f"changes, 0 short/empty/mixed, {r['open_failed']} failed opens (errors {r['open_errors']}); writer: {w['ok']} replaced, "
             f"{w['publish']} gave up on the move (the old career then stays), {w['sleep']} refused moves retried (this Wine refuses "
             f"to replace a file another handle has open, even one opened with FILE_SHARE_DELETE), {w['elapsed_ms']} ms")


class MutantsAreCaught(WineCase):
    @classmethod
    def setUpClass(cls):
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            futures = {m.name: pool.submit(ENV.build.variant, m.name, m.edits) for m in MUTANTS}
            cls.exes = {name: f.result() for name, f in futures.items()}

    def test_each_mutant_fails_the_checks_meant_for_it_and_still_passes_its_controls(self):
        for m in MUTANTS:
            with self.subTest(mutant=m.name):
                caught = []
                for name in m.catches:
                    out = ENV.evaluate(SCENARIOS[name], self.exes[m.name])
                    if name.startswith("sharing") and out.fields.get("probe") != "enforced":
                        continue
                    self.assertTrue(out.problems, f"mutant {m.name} ({m.why}) was NOT caught by {name}:\n{out.run.stdout}{out.run.stderr}")
                    caught.append((name, out.problems[0]))
                for name in m.controls:
                    control = SCENARIOS[name]
                    control = dataclasses.replace(control, calls={k: v for k, v in control.calls.items() if k not in m.ignore})
                    out = ENV.evaluate(control, self.exes[m.name])
                    self.assertEqual([], out.problems, f"control {name} fails on mutant {m.name}:\n{out.report()}")
                note(f"mutant {m.name} ({m.why}): caught by {len(caught)} of {len(m.catches)} checks; first: {caught[0][0]}: "
                     f"{caught[0][1][:140]}" if caught else f"mutant {m.name}: its only check is skipped here")

    def test_the_in_place_mutant_is_also_caught_by_a_concurrent_reader(self):
        # The retail behaviour (open the career for writing, which cuts it to nothing first) shows a reader short files.
        case, writer, read, a, b, target = observe_pair(self.exes["write-in-place"], 60, "0")
        self.assertEqual(0, read.returncode, read.stderr)
        torn = {k: int(read.fields[k]) for k in ("empty", "short", "mixed", "read_errors")}
        self.assertGreater(sum(torn.values()), 0, f"a reader never saw the in-place writer's half-written career: {read.stdout}")
        note(f"mutant write-in-place seen by a concurrent reader: {torn} torn reads out of {read.fields['iterations']} "
             f"({read.fields['open_failed']} failed opens)")
        relax(case.root)


def load_tests(loader, tests, pattern):
    suite = unittest.TestSuite()
    for case in (SourceHygiene, Publishes, Failures, Crashes, RealSharing, ConcurrentReader, MutantsAreCaught):
        suite.addTests(loader.loadTestsFromTestCase(case))
    return suite


if __name__ == "__main__":
    unittest.main()
