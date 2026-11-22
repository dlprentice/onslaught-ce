#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-compile and verify the native Windows NTFS mods acceptance fixture (mods-windows-ntfs.exe).

Builds a standalone 32-bit Windows executable directly from the REAL production port/platform/common/mods.cpp
(-DPORT_MODS_WINDOWS_ON=1, CE_VERSION, static zlib, sha256, and utf8.manifest), plus a default-fence control
executable (without PORT_MODS_WINDOWS_ON) to verify production Windows builds keep mods off until native NTFS
acceptance passes.

When Wine is available (and --build-only is not set), runs the fixture in isolated Wine dry-run mode
(--allow-wine) inside a fresh temporary directory / bubblewrap sandbox where available:
- Every core case (clean-snapshot, check-mods, hardlink-startup, hardlink-runtime, reserved-names-ads,
  case-collision-identity) MUST PASS.
- NTFS-only probes (junction, dir-symlink, file-symlink, junction-swap, case-exact) are exercised honestly:
  any probe Wine supports must pass, and any probe Wine lacks NTFS semantics for must emit structured
  UNSUPPORTED_OR_DENIED (exit 3) rather than a false pass.
- Emits structured ASCII JSONL receipts and done.txt (PASS/FAIL), ready for direct execution on a native
  Windows 10/11 NTFS guest without --allow-wine.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
DRIVER = PORT / "tools/tests/mods_windows_ntfs_driver.cpp"
MODS_CPP = PORT / "platform/common/mods.cpp"
SHA256_C = PORT / "updater/sha256.c"
UTF8_RC = PORT / "platform/windows/utf8.rc"

CORE_CASES = (
    "clean-snapshot",
    "check-mods",
    "hardlink-startup",
    "hardlink-runtime",
    "reserved-names-ads",
    "case-collision-identity",
)
NTFS_ONLY_CASES = (
    "junction",
    "dir-symlink",
    "file-symlink",
    "junction-swap",
    "case-exact",
)
ALL_CASES = CORE_CASES + NTFS_ONLY_CASES


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


def build_fixture(
    out: Path,
    cxx: Path,
    cc: Path,
    windres: Path,
    zlib_static: Path,
    *,
    windows_on: bool = True,
    name: str = "mods-windows-ntfs.exe",
) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    sha_obj = out / "sha256.o"
    if not sha_obj.is_file():
        r = subprocess.run(
            [str(cc), "-O2", "-fno-ident", f"-ffile-prefix-map={ROOT}=.", "-c", str(SHA256_C), "-o", str(sha_obj)],
            capture_output=True, text=True, timeout=120, preexec_fn=no_core,
        )
        if r.returncode:
            raise RuntimeError(f"sha256 compile failed:\n{r.stderr}")

    res_obj = out / "utf8.o"
    if not res_obj.is_file():
        r = subprocess.run(
            [str(windres), "--target=pe-i386", "-I", str(PORT / "platform/windows"),
             "-O", "coff", str(UTF8_RC), "-o", str(res_obj)],
            capture_output=True, text=True, timeout=60, preexec_fn=no_core,
        )
        if r.returncode:
            raise RuntimeError(f"utf8.rc compile failed:\n{r.stderr}")

    tag = "on" if windows_on else "off"
    mods_obj = out / f"mods-{tag}.o"
    driver_obj = out / f"driver-{tag}.o"
    exe = out / name
    defines = ['-DCE_VERSION="0.2.0"'] + (["-DPORT_MODS_WINDOWS_ON=1"] if windows_on else [])
    common = [
        "-std=gnu++17", "-O1", "-Wall", "-Wextra", "-Werror", "-fno-ident",
        f"-ffile-prefix-map={ROOT}=.", *defines,
        "-I", str(PORT / "platform/common"), "-I", str(PORT / "updater"),
    ]
    link_flags = [
        "-static", "-Wl,--dynamicbase", "-Wl,--nxcompat", "-Wl,--large-address-aware",
        "-Wl,--no-insert-timestamp", "-Wl,--build-id=none", "-s",
    ]

    steps = [
        [str(cxx), *common, "-c", str(MODS_CPP), "-o", str(mods_obj)],
        [str(cxx), *common, "-c", str(DRIVER), "-o", str(driver_obj)],
        [str(cxx), *link_flags, str(driver_obj), str(mods_obj), str(sha_obj), str(res_obj),
         str(zlib_static), "-lshell32", "-ladvapi32", "-o", str(exe)],
    ]
    for cmd in steps:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=180, preexec_fn=no_core)
        if r.returncode:
            raise RuntimeError(f"build step failed ({cmd[0]}):\n{r.stdout}\n{r.stderr}")
    return exe


def build_reproducible(target: Path, cxx: Path, cc: Path, windres: Path, zlib_static: Path) -> dict[str, object]:
    with tempfile.TemporaryDirectory(prefix="ce-mods-ntfs-repro-") as tmp:
        a = build_fixture(Path(tmp) / "a", cxx, cc, windres, zlib_static, windows_on=True, name=target.name)
        b = build_fixture(Path(tmp) / "b", cxx, cc, windres, zlib_static, windows_on=True, name=target.name)
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
    }


def clean_wine_env(prefix: Path) -> dict[str, str]:
    env = {
        k: v for k, v in os.environ.items()
        if not k.startswith(("BEA_", "WINE", "XDG_"))
        and k not in {
            "DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY",
            "DBUS_SESSION_BUS_ADDRESS", "PULSE_SERVER", "PIPEWIRE_REMOTE", "SSH_AUTH_SOCK",
        }
    }
    env.update({
        "WINEPREFIX": str(prefix),
        "WINEDEBUG": "-all",
        "WINEDLLOVERRIDES": "mscoree,mshtml,winemenubuilder.exe,winepulse.drv,winealsa.drv,wineoss.drv=",
        "BEA_REPORT_NO_OPEN": "1",
        "BROWSER": "/bin/true",
        "SDL_AUDIO_DRIVER": "dummy",
    })
    return env


def run_in_wine(wine: Path, bwrap: Path | None, work: Path, exe: Path, args: list[str],
                extra_env: dict[str, str] | None = None, timeout: int = 180) -> subprocess.CompletedProcess:
    if bwrap is not None:
        inside = "/run/ce-mods-ntfs"
        for sub in ("profile", "xdg-config", "xdg-data", "xdg-cache", "xdg-runtime"):
            (work / sub).mkdir(mode=0o700, exist_ok=True)
        env = clean_wine_env(Path(inside) / "wine-prefix")
        env.update(extra_env or {})
        rel_exe = exe.resolve().relative_to(work.resolve())
        cmd = [
            str(bwrap), "--die-with-parent", "--new-session", "--unshare-all",
            "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
            "--symlink", "usr/bin", "/bin", "--symlink", "usr/lib", "/lib",
            "--symlink", "usr/lib", "/lib64", "--proc", "/proc", "--dev", "/dev",
            "--tmpfs", "/tmp", "--dir", "/run",
            "--bind", str(work.resolve()), inside, "--chdir", inside,
            "--setenv", "HOME", inside + "/profile",
            "--setenv", "XDG_CONFIG_HOME", inside + "/xdg-config",
            "--setenv", "XDG_DATA_HOME", inside + "/xdg-data",
            "--setenv", "XDG_CACHE_HOME", inside + "/xdg-cache",
            "--setenv", "XDG_RUNTIME_DIR", inside + "/xdg-runtime",
            "--setenv", "WINEPREFIX", inside + "/wine-prefix",
            str(wine), f"{inside}/{rel_exe}", *args,
        ]
        return subprocess.run(cmd, env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                              timeout=timeout, preexec_fn=no_core)
    prefix = work / "wine-prefix"
    env = clean_wine_env(prefix)
    env.update(extra_env or {})
    return subprocess.run(
        [str(wine), str(exe), *args],
        env=env, cwd=str(work), stdin=subprocess.DEVNULL, capture_output=True, text=True,
        timeout=timeout, preexec_fn=no_core,
    )


def parse_jsonl(path: Path) -> list[dict]:
    records = []
    for raw in path.read_text(encoding="ascii").splitlines():
        if raw.strip():
            records.append(json.loads(raw))
    return records


class ModsWindowsNtfsFixtureTests(unittest.TestCase):
    cfg: dict[str, object] = {}

    @classmethod
    def setUpClass(cls) -> None:
        cfg = cls.cfg
        cls.out = Path(cfg["out"]).resolve()
        cls.out.mkdir(parents=True, exist_ok=True)
        cls.run_dir = Path(tempfile.mkdtemp(prefix="ntfs-fixture-", dir=cls.out))
        os.chmod(cls.run_dir, 0o700)
        cls.exe_on = build_fixture(
            cls.run_dir, cfg["cxx"], cfg["cc"], cfg["windres"], cfg["zlib_static"],
            windows_on=True, name="mods-windows-ntfs.exe",
        )
        cls.exe_off = build_fixture(
            cls.run_dir, cfg["cxx"], cfg["cc"], cfg["windres"], cfg["zlib_static"],
            windows_on=False, name="mods-windows-ntfs-fence.exe",
        )
        cls.receipt = {
            "mods_cpp_sha256": sha256_file(MODS_CPP),
            "driver_cpp_sha256": sha256_file(DRIVER),
            "executable": str(cls.exe_on),
            "executable_sha256": sha256_file(cls.exe_on),
            "executable_bytes": cls.exe_on.stat().st_size,
            "fence_executable_sha256": sha256_file(cls.exe_off),
            "cli": 'mods-windows-ntfs.exe --root "C:\\ce-native-RUNID\\cases\\mods-ntfs"',
            "native_windows_acceptance": False,
        }

    @classmethod
    def tearDownClass(cls) -> None:
        run_dir = getattr(cls, "run_dir", None)
        if run_dir is None:
            return
        for sub in ("wine-prefix", "profile", "xdg-config", "xdg-data", "xdg-cache", "xdg-runtime"):
            shutil.rmtree(run_dir / sub, ignore_errors=True)

    def test_01_production_switch_retained_and_binaries_standalone(self) -> None:
        source = MODS_CPP.read_text(encoding="utf-8")
        self.assertEqual(source.count("#elif defined(_WIN32) && !defined(PORT_MODS_WINDOWS_ON)"), 1)
        self.assertTrue(self.exe_on.is_file() and self.exe_on.stat().st_size > 100_000)
        self.assertTrue(self.exe_off.is_file() and self.exe_off.stat().st_size > 100_000)
        # Verify standalone PE imports contain no SDL3/zlib/libc++ DLLs (only Windows system DLLs).
        objdump = self.cfg["cxx"].with_name("i686-w64-mingw32-objdump")
        if objdump.is_file():
            r = subprocess.run([str(objdump), "-p", str(self.exe_on)], capture_output=True, text=True, timeout=30)
            self.assertEqual(r.returncode, 0, r.stderr)
            dlls = [line.split(":", 1)[1].strip().lower() for line in r.stdout.splitlines() if "DLL Name:" in line]
            self.assertTrue(dlls, "expected Windows system DLL imports")
            for forbidden in ("sdl3.dll", "libz.dll", "zlib1.dll", "libc++.dll", "libwinpthread-1.dll"):
                self.assertNotIn(forbidden, dlls, f"standalone NTFS fixture must not depend on {forbidden}: {dlls}")

    def test_02_fence_binary_refuses_check_mods_without_touching_disk(self) -> None:
        wine = self.cfg["wine"]
        if self.cfg["build_only"] or wine is None:
            self.skipTest("Wine execution disabled (--build-only) or wine not found")
        bwrap = self.cfg["bwrap"]
        r = run_in_wine(wine, bwrap, self.run_dir, self.exe_off, ["--check-mods"])
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))
        self.assertIn("off on Windows until the loader's checks on NTFS have passed", r.stdout)

    def test_03_wine_dry_run_exercises_core_cases_and_reports_ntfs_limits_honestly(self) -> None:
        wine = self.cfg["wine"]
        if self.cfg["build_only"] or wine is None:
            self.skipTest("Wine execution disabled (--build-only) or wine not found")
        bwrap = self.cfg["bwrap"]
        inside = "/run/ce-mods-ntfs" if bwrap is not None else str(self.run_dir)
        root_win = "Z:" + (inside + "/ntfs-run").replace("/", "\\")
        r = run_in_wine(wine, bwrap, self.run_dir, self.exe_on, ["--root", root_win, "--allow-wine"], timeout=240)
        run_root = self.run_dir / "ntfs-run"
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
                self.assertEqual(len(ads_probes), 1, f"missing ntfs-ads-on-disk probe record: {case_records}")
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
        self.receipt["wine_dry_run_summary"] = summary
        (self.run_dir / "receipt.json").write_text(json.dumps(self.receipt, indent=2) + "\n")

    def test_04_native_mode_rejects_wine_environment_honestly(self) -> None:
        # Without --allow-wine, running outside a local non-elevated UTF-8 NTFS Windows host must NOT falsely claim PASS
        # unless the environment genuinely satisfies native NTFS + junctions + normalized case rules.
        wine = self.cfg["wine"]
        if self.cfg["build_only"] or wine is None:
            self.skipTest("Wine execution disabled (--build-only) or wine not found")
        bwrap = self.cfg["bwrap"]
        inside = "/run/ce-mods-ntfs" if bwrap is not None else str(self.run_dir)
        root_win = "Z:" + (inside + "/native-neg").replace("/", "\\")
        r = run_in_wine(wine, bwrap, self.run_dir, self.exe_on, ["--root", root_win], timeout=120)
        run_root = self.run_dir / "native-neg"
        self.assertNotEqual(r.returncode, 0, "native mode must not falsely pass under Wine Z: drive")
        self.assertEqual((run_root / "done.txt").read_text(encoding="ascii"), "FAIL\n")
        records = parse_jsonl(run_root / "results.jsonl")
        self.assertNotIn("native_windows_acceptance", records[0])
        self.assertIs(records[-1].get("native_windows_acceptance"), False)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    data = resolve_data_root()
    mingw = Path(os.environ.get("BEA_LLVM_MINGW", data / "llvm-mingw/bin"))
    win32 = Path(os.environ.get("BEA_PORT_DEPS_WIN32", data / "prefix-win32"))
    ap.add_argument("--cxx", type=Path, default=mingw / "i686-w64-mingw32-clang++")
    ap.add_argument("--cc", type=Path, default=mingw / "i686-w64-mingw32-clang")
    ap.add_argument("--windres", type=Path, default=mingw / "i686-w64-mingw32-windres")
    ap.add_argument("--zlib-static", type=Path, default=win32 / "lib/libzs.a")
    ap.add_argument("--out", type=Path, default=ROOT / "local-data/mods-windows-ntfs")
    ap.add_argument("--wine", type=Path, default=Path(shutil.which("wine")) if shutil.which("wine") else None)
    ap.add_argument("--bwrap", type=Path, default=Path(shutil.which("bwrap")) if shutil.which("bwrap") else None)
    ap.add_argument("--build-only", action="store_true", help="only cross-compile and verify standalone PE; do not invoke Wine")
    ap.add_argument("--emit-exe", type=Path, default=None, help="reproducibly build twice and write standalone executable to this path")
    args, rest = ap.parse_known_args()

    missing = [str(p) for p in (args.cxx, args.cc, args.windres, args.zlib_static) if not p.is_file()]
    if missing:
        print(f"SKIP: missing pinned win32 build dependency: {', '.join(missing)}", file=sys.stderr)
        return 1

    if args.emit_exe is not None:
        info = build_reproducible(args.emit_exe.resolve(), args.cxx, args.cc, args.windres, args.zlib_static)
        print(json.dumps(info, indent=1, sort_keys=True))
        return 0

    ModsWindowsNtfsFixtureTests.cfg = {
        "cxx": args.cxx,
        "cc": args.cc,
        "windres": args.windres,
        "zlib_static": args.zlib_static,
        "out": args.out,
        "wine": args.wine,
        "bwrap": args.bwrap,
        "build_only": args.build_only,
    }
    program = unittest.main(argv=[sys.argv[0], "-v", *rest], exit=False)
    return 0 if program.result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
