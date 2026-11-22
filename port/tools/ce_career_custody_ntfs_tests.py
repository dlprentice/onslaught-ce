#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-compile and verify the native Windows Career Custody NTFS acceptance fixture (ce-career-custody-ntfs.exe).

Builds a standalone reproducible 32-bit Windows executable linking the REAL production
port/platform/windows/ce_career_restore.cpp, port/platform/windows/ce_career_publish.cpp, and
port/platform/windows/ce_backup_preview.cpp (with utf8.rc and a minimal SDL3 header shim so the binary
carries zero third-party DLL imports).

When Wine is available (and --build-only / --emit-exe is not set), runs the fixture in isolated Wine
dry-run mode (--allow-wine) inside a fresh disposable Wine prefix (never ~/.wine, no display):
- Every core case (backup-restore-roundtrip, external-publish-collision, backup-preview-readonly,
  reserved-names-ads, hardlink-refusal, readonly-rollback-preservation) MUST PASS.
- NTFS-only probes (junction-escape, symlink-refusal, case-sensitivity) are exercised honestly:
  any probe Wine supports must pass, and any probe Wine lacks NTFS semantics for must emit structured
  UNSUPPORTED_OR_DENIED (exit 3) rather than a false pass.
- Running without --allow-wine under Wine MUST fail honestly rather than claiming native NTFS acceptance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
DRIVER = PORT / "tools/tests/ce_career_custody_ntfs_driver.cpp"
RESTORE_CPP = PORT / "platform/windows/ce_career_restore.cpp"
PUBLISH_CPP = PORT / "platform/windows/ce_career_publish.cpp"
PREVIEW_CPP = PORT / "platform/windows/ce_backup_preview.cpp"
UTF8_RC = PORT / "platform/windows/utf8.rc"

CORE_CASES = (
    "backup-restore-roundtrip",
    "external-publish-collision",
    "backup-preview-readonly",
    "reserved-names-ads",
    "hardlink-refusal",
    "readonly-rollback-preservation",
)
NTFS_ONLY_CASES = (
    "junction-escape",
    "symlink-refusal",
    "case-sensitivity",
)
ALL_CASES = CORE_CASES + NTFS_ONLY_CASES

SDL_SHIM = """// Standalone fixture shim: SDL declarations referenced by ce_career_publish.cpp.
#pragma once
#define SDLCALL
extern "C" {
struct SDL_Window;
typedef struct SDL_DialogFileFilter { const char *name; const char *pattern; } SDL_DialogFileFilter;
typedef void (*SDL_DialogFileCallback)(void *userdata, const char *const *filelist, int filter);
const char *SDL_GetBasePath(void);
SDL_Window *SDL_GetKeyboardFocus(void);
void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
                            const SDL_DialogFileFilter *filters, int nfilters,
                            const char *default_location, bool allow_many);
}
"""


def no_core() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def resolve_data_root() -> Path:
    if os.environ.get("BEA_PORT_DATA"):
        return Path(os.environ["BEA_PORT_DATA"]).resolve()
    r = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
        capture_output=True, text=True,
    )
    if r.returncode == 0 and r.stdout.strip():
        cand = Path(r.stdout.strip()).parent / "local-data/port"
        if cand.is_dir():
            return cand
    return ROOT / "local-data/port"


def pe_facts(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise RuntimeError(f"{path.name}: not a PE image")
    machine, _, stamp, _, _, _, chars = struct.unpack_from("<HHIIIHH", data, e_lfanew + 4)
    opt = e_lfanew + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dll_chars = struct.unpack_from("<H", data, opt + 70)[0]
    return {
        "machine": hex(machine),
        "pe_format": {0x10B: "PE32", 0x20B: "PE32+"}.get(magic, hex(magic)),
        "timestamp": stamp,
        "dynamic_base": bool(dll_chars & 0x40),
        "nx_compat": bool(dll_chars & 0x100),
        "large_address_aware": bool(chars & 0x20),
    }


def build_fixture(
    out: Path,
    cxx: Path,
    windres: Path,
    *,
    name: str = "ce-career-custody-ntfs.exe",
) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    shim_dir = out / "shim"
    (shim_dir / "SDL3").mkdir(parents=True, exist_ok=True)
    (shim_dir / "SDL3/SDL.h").write_text(SDL_SHIM, encoding="ascii")

    res_obj = out / "utf8.o"
    r = subprocess.run(
        [str(windres), "--target=pe-i386", "-I", str(PORT / "platform/windows"),
         "-O", "coff", str(UTF8_RC), "-o", str(res_obj)],
        capture_output=True, text=True, timeout=60, preexec_fn=no_core,
    )
    if r.returncode:
        raise RuntimeError(f"utf8.rc compile failed:\n{r.stderr}")

    common_flags = [
        "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-ident",
        f"-ffile-prefix-map={ROOT}=.", f"-ffile-prefix-map={out}=<build>",
        "-I", str(shim_dir),
        "-I", str(PORT / "platform/windows"),
        "-I", str(PORT / "platform/common"),
        "-I", str(PORT / "game"),
    ]
    link_flags = [
        "-static", "-Wl,--dynamicbase", "-Wl,--nxcompat", "-Wl,--large-address-aware",
        "-Wl,--no-insert-timestamp", "-Wl,--build-id=none", "-s",
    ]

    units = (
        ("restore.o", RESTORE_CPP),
        ("publish.o", PUBLISH_CPP),
        ("preview.o", PREVIEW_CPP),
        ("driver.o", DRIVER),
    )
    objs: list[Path] = []
    for obj_name, src in units:
        obj = out / obj_name
        cmd = [str(cxx), *common_flags, "-c", str(src), "-o", str(obj)]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=180, preexec_fn=no_core)
        if r.returncode:
            raise RuntimeError(f"compile failed ({src.name}):\n{r.stdout}\n{r.stderr}")
        objs.append(obj)

    exe = out / name
    link_cmd = [str(cxx), *link_flags, *[str(o) for o in objs], str(res_obj), "-lshell32", "-ladvapi32", "-o", str(exe)]
    r = subprocess.run(link_cmd, capture_output=True, text=True, timeout=180, preexec_fn=no_core)
    if r.returncode:
        raise RuntimeError(f"link failed ({name}):\n{r.stdout}\n{r.stderr}")
    return exe


def build_reproducible(target: Path, cxx: Path, windres: Path) -> dict[str, object]:
    with tempfile.TemporaryDirectory(prefix="ce-career-ntfs-repro-") as tmp:
        a = build_fixture(Path(tmp) / "a", cxx, windres, name=target.name)
        b = build_fixture(Path(tmp) / "b", cxx, windres, name=target.name)
        sha_a, sha_b = sha256_file(a), sha256_file(b)
        if sha_a != sha_b:
            raise RuntimeError(f"{target.name}: non-reproducible build ({sha_a} != {sha_b})")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(a, target)
    return {
        "name": target.name,
        "path": str(target),
        "sha256": sha256_file(target),
        "bytes": target.stat().st_size,
        "reproducible": True,
        "pe": pe_facts(target),
    }


def relax_and_rmtree(path: Path) -> None:
    if not path.exists():
        return
    for folder, dirs, files in os.walk(path):
        for n in dirs + files:
            p = Path(folder, n)
            if not p.is_symlink():
                try:
                    p.chmod(0o755 if p.is_dir() else 0o644)
                except OSError:
                    pass
    shutil.rmtree(path, ignore_errors=True)


class DisposableWine:
    """Isolated disposable Wine prefix with no display, audio, or session bus."""

    def __init__(self, scratch: Path, wine: Path):
        self.wine = str(wine)
        self.wineserver = shutil.which("wineserver")
        self.wineboot = shutil.which("wineboot")
        if not (self.wineserver and self.wineboot):
            raise unittest.SkipTest("wineserver and wineboot are required")
        self.prefix = (scratch / "wine-prefix").resolve()
        if self.prefix == (Path.home() / ".wine").resolve():
            raise RuntimeError("refusing to touch ~/.wine")
        self.private = scratch / "wine-home"
        self.io_dir = scratch / "wine-io"
        self.io_dir.mkdir(parents=True, exist_ok=True)
        self.started = False

    def env(self) -> dict[str, str]:
        drop = {
            "DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY",
            "DBUS_SESSION_BUS_ADDRESS", "PULSE_SERVER", "PIPEWIRE_REMOTE", "SSH_AUTH_SOCK",
            "WINEPREFIX", "WINEARCH", "WINEDEBUG", "WINEDLLOVERRIDES", "BEA_GAME_DIR", "BEA_USER_DIR",
        }
        e = {k: v for k, v in os.environ.items() if k not in drop and not k.startswith(("XDG_", "WINE"))}
        home = self.private / "home"
        e.update(
            WINEPREFIX=str(self.prefix),
            WINEDEBUG="-all",
            WINEDLLOVERRIDES="mscoree,mshtml,winemenubuilder.exe,winepulse.drv,winealsa.drv,wineoss.drv=",
            BEA_REPORT_NO_OPEN="1",
            BROWSER="/bin/true",
            SDL_AUDIO_DRIVER="dummy",
            HOME=str(home),
            XDG_RUNTIME_DIR=str(self.private / "run"),
            XDG_CONFIG_HOME=str(home / ".config"),
            XDG_DATA_HOME=str(home / ".local/share"),
            XDG_CACHE_HOME=str(home / ".cache"),
            LC_ALL="C.UTF-8",
            LANG="C.UTF-8",
        )
        return e

    def _run(self, argv: list[str], timeout: int = 120) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryFile(dir=self.io_dir) as out_f, tempfile.TemporaryFile(dir=self.io_dir) as err_f:
            proc = subprocess.Popen(
                argv, stdin=subprocess.DEVNULL, stdout=out_f, stderr=err_f,
                env=self.env(), cwd=self.prefix, start_new_session=True, preexec_fn=no_core,
            )
            try:
                rc = proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
                raise
            out_f.seek(0)
            err_f.seek(0)
            return subprocess.CompletedProcess(
                argv, rc, out_f.read().decode("utf-8", "replace"), err_f.read().decode("utf-8", "replace")
            )

    def start(self) -> None:
        self.prefix.mkdir(parents=True, exist_ok=True)
        for sub in ("home", "run"):
            (self.private / sub).mkdir(parents=True, exist_ok=True)
        (self.private / "run").chmod(0o700)
        self.started = True
        if not (self.prefix / "system.reg").is_file():
            boot = self._run([self.wineboot, "-i"], timeout=90)
            if boot.returncode != 0:
                raise RuntimeError(f"wineboot -i failed ({boot.returncode}):\n{boot.stdout}{boot.stderr}")
            self._run([self.wineserver, "-w"], timeout=60)
        subprocess.run(
            [self.wineserver, "-p"], env=self.env(),
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30,
        )

    def run_exe(self, exe: str, args: list[str], timeout: int = 180) -> subprocess.CompletedProcess[str]:
        return self._run([self.wine, exe, *args], timeout=timeout)

    def stop(self) -> None:
        if self.started:
            for flag in ("-k", "-w"):
                subprocess.run(
                    [self.wineserver, flag], env=self.env(),
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60,
                )


def parse_jsonl(path: Path) -> list[dict]:
    records = []
    for raw in path.read_text(encoding="ascii").splitlines():
        if raw.strip():
            records.append(json.loads(raw))
    return records


class CareerCustodyWindowsNtfsTests(unittest.TestCase):
    cfg: dict[str, object] = {}

    @classmethod
    def setUpClass(cls) -> None:
        cfg = cls.cfg
        cls.out = Path(cfg["out"]).resolve()
        cls.out.mkdir(parents=True, exist_ok=True)
        cls.run_dir = Path(tempfile.mkdtemp(prefix="career-ntfs-fixture-", dir=cls.out))
        os.chmod(cls.run_dir, 0o700)
        cls.exe = cls.run_dir / "ce-career-custody-ntfs.exe"
        cls.build_info = build_reproducible(cls.exe, cfg["cxx"], cfg["windres"])
        cls.wine: DisposableWine | None = None
        if not cfg["build_only"] and cfg["wine"] is not None:
            cls.wine = DisposableWine(cls.run_dir, cfg["wine"])
            cls.wine.start()
            drive_c = cls.wine.prefix / "drive_c"
            (drive_c / "ce-career-bin").mkdir(parents=True, exist_ok=True)
            shutil.copyfile(cls.exe, drive_c / "ce-career-bin/ce-career-custody-ntfs.exe")

    @classmethod
    def tearDownClass(cls) -> None:
        if getattr(cls, "wine", None) is not None:
            cls.wine.stop()
        run_dir = getattr(cls, "run_dir", None)
        if run_dir is not None:
            for sub in ("wine-prefix", "wine-home", "wine-io"):
                relax_and_rmtree(run_dir / sub)

    def test_01_standalone_reproducible_pe_and_system_only_dlls(self) -> None:
        self.assertTrue(self.exe.is_file() and self.exe.stat().st_size > 100_000)
        self.assertIs(self.build_info["reproducible"], True)
        pe = self.build_info["pe"]
        self.assertEqual(pe["pe_format"], "PE32")
        self.assertEqual(pe["timestamp"], 0)
        self.assertTrue(pe["dynamic_base"])
        self.assertTrue(pe["nx_compat"])
        self.assertTrue(pe["large_address_aware"])
        objdump = self.cfg["cxx"].with_name("i686-w64-mingw32-objdump")
        if objdump.is_file():
            r = subprocess.run([str(objdump), "-p", str(self.exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(r.returncode, 0, r.stderr)
            dlls = [line.split(":", 1)[1].strip().lower() for line in r.stdout.splitlines() if "DLL Name:" in line]
            self.assertTrue(dlls, "expected Windows system DLL imports")
            for forbidden in ("sdl3.dll", "libz.dll", "zlib1.dll", "libc++.dll", "libwinpthread-1.dll"):
                self.assertNotIn(forbidden, dlls, f"standalone NTFS fixture must not depend on {forbidden}: {dlls}")

    def test_02_wine_dry_run_exercises_core_cases_and_reports_ntfs_limits_honestly(self) -> None:
        if self.wine is None:
            self.skipTest("Wine execution disabled (--build-only) or wine not found")
        r = self.wine.run_exe(
            "C:\\ce-career-bin\\ce-career-custody-ntfs.exe",
            ["--root", "C:\\ce-career-dryrun", "--allow-wine"],
            timeout=240,
        )
        run_root = self.wine.prefix / "drive_c/ce-career-dryrun"
        self.assertEqual(r.returncode, 0, f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        self.assertTrue((run_root / "done.txt").is_file(), "done.txt missing")
        self.assertEqual((run_root / "done.txt").read_text(encoding="ascii"), "PASS\n")

        records = parse_jsonl(run_root / "results.jsonl")
        self.assertGreaterEqual(len(records), len(ALL_CASES) + 2)
        header = records[0]
        self.assertEqual(header.get("runtime_mode"), "wine_dry_run")
        self.assertNotIn("native_windows_acceptance", header)

        summary = {rec["case"]: rec for rec in records if "case" in rec and "status" in rec}
        self.assertEqual(set(summary.keys()), set(ALL_CASES))
        for case in CORE_CASES:
            self.assertEqual(summary[case]["exit"], 0, f"core case {case} failed: {summary[case]}")
            self.assertEqual(summary[case]["status"], "PASS")
            self.assertIs(summary[case]["optional_not_exercised"], False)
            case_records = parse_jsonl(run_root / f"{case}.jsonl")
            self.assertTrue(any(rec.get("ok") is True for rec in case_records), case)
            self.assertFalse(any(rec.get("ok") is False for rec in case_records), case)
            if case == "reserved-names-ads":
                ads_probes = [rec for rec in case_records if rec.get("probe") == "ntfs-ads-on-disk"]
                self.assertEqual(len(ads_probes), 1, f"missing ntfs-ads-on-disk probe: {case_records}")
                self.assertIn(ads_probes[0].get("status"), ("EXERCISED", "UNSUPPORTED_OR_DENIED"))

        for case in NTFS_ONLY_CASES:
            rec = summary[case]
            self.assertIn(rec["status"], ("PASS", "UNSUPPORTED_OR_DENIED"), f"unexpected failure in {case}: {rec}")
            if rec["status"] == "UNSUPPORTED_OR_DENIED":
                self.assertEqual(rec["exit"], 3)
                self.assertIs(rec["optional_not_exercised"], True)
                case_records = parse_jsonl(run_root / f"{case}.jsonl")
                self.assertTrue(
                    any(c.get("status") == "UNSUPPORTED_OR_DENIED" and c.get("passed") is False for c in case_records),
                    f"missing structured UNSUPPORTED_OR_DENIED record for {case}: {case_records}",
                )

        tail = records[-1]
        self.assertEqual(tail.get("exit"), 0)
        self.assertEqual(tail.get("failures"), 0)
        self.assertIs(tail.get("native_windows_acceptance"), False)
        self.assertIs(tail.get("native_executed"), True)

        receipt = {
            "build": self.build_info,
            "restore_cpp_sha256": sha256_file(RESTORE_CPP),
            "publish_cpp_sha256": sha256_file(PUBLISH_CPP),
            "preview_cpp_sha256": sha256_file(PREVIEW_CPP),
            "driver_cpp_sha256": sha256_file(DRIVER),
            "wine_dry_run_summary": summary,
            "native_windows_acceptance": False,
        }
        (self.run_dir / "receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")

    def test_03_native_mode_rejects_wine_environment_honestly(self) -> None:
        if self.wine is None:
            self.skipTest("Wine execution disabled (--build-only) or wine not found")
        r = self.wine.run_exe(
            "C:\\ce-career-bin\\ce-career-custody-ntfs.exe",
            ["--root", "C:\\ce-career-native-neg"],
            timeout=120,
        )
        run_root = self.wine.prefix / "drive_c/ce-career-native-neg"
        self.assertNotEqual(r.returncode, 0, "native mode must not falsely pass under Wine")
        self.assertEqual((run_root / "done.txt").read_text(encoding="ascii"), "FAIL\n")
        records = parse_jsonl(run_root / "results.jsonl")
        self.assertEqual(records[0].get("runtime_mode"), "native_ntfs")
        self.assertEqual(records[0].get("wine_detected"), 1)
        self.assertIs(records[-1].get("native_windows_acceptance"), False)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    data = resolve_data_root()
    mingw = Path(os.environ.get("BEA_LLVM_MINGW", data / "llvm-mingw/bin"))
    ap.add_argument("--cxx", type=Path, default=mingw / "i686-w64-mingw32-clang++")
    ap.add_argument("--windres", type=Path, default=mingw / "i686-w64-mingw32-windres")
    ap.add_argument("--out", type=Path, default=ROOT / "local-data/ce-career-custody-ntfs")
    ap.add_argument("--wine", type=Path, default=Path(shutil.which("wine")) if shutil.which("wine") else None)
    ap.add_argument("--build-only", action="store_true", help="only cross-compile and verify standalone PE; do not invoke Wine")
    ap.add_argument("--emit-exe", type=Path, default=None, help="reproducibly build twice and write standalone executable to this path")
    args, rest = ap.parse_known_args()

    missing = [str(p) for p in (args.cxx, args.windres) if not p.is_file()]
    if missing:
        print(f"SKIP: missing pinned win32 build dependency: {', '.join(missing)}", file=sys.stderr)
        return 1

    if args.emit_exe is not None:
        info = build_reproducible(args.emit_exe.resolve(), args.cxx, args.windres)
        print(json.dumps(info, indent=1, sort_keys=True))
        return 0

    CareerCustodyWindowsNtfsTests.cfg = {
        "cxx": args.cxx,
        "windres": args.windres,
        "out": args.out,
        "wine": args.wine,
        "build_only": args.build_only,
    }
    program = unittest.main(argv=[sys.argv[0], "-v", *rest], exit=False)
    return 0 if program.result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
