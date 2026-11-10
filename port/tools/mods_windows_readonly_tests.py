#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-build synthetic Windows mod controls; optionally run in isolated Wine.

The production switch is tested by the "fence" case: its build has mods off. The
other cases build the test flavour (PORT_MODS_WINDOWS_ON, as port/build.py
--mods-windows-test does) to exercise the loader before native Windows
acceptance. Every relative open is passed through a shim that holds it to the
documented NT contract. Inputs are generated fixtures, with no game installation,
profile, saves or real mods. A pass here is Wine evidence: it is never the NTFS
proof.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = ("load", "snapshot", "hash", "identity", "rename", "file-link",
         "directory-link", "hardlink", "directory-type", "mmio", "mmio-info", "mmio-failure",
         "mmio-no-selector", "mmio-unlisted")
PAYLOAD = b"synthetic-mod-snapshot"


def function(source: str, name: str) -> str:
    candidates = [match for match in re.finditer(
        r"(?m)^[^\n;{}]*\b" + re.escape(name) + r"\s*\([^;{}]*?\)\s*\{", source)
        if not re.search(r"\bif\b", match.group())]
    if len(candidates) != 1:
        raise RuntimeError("reader function was not found exactly once: " + name)
    start = candidates[0].start()
    opening = candidates[0].end() - 1
    depth, end = 1, opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end] + "\n"


def fixture(root: Path) -> None:
    data = root / "mods/fixture/data"
    data.mkdir(parents=True)
    (root / "synthetic-only.marker").write_text("synthetic-only\n")
    (data / "item.bin").write_bytes(PAYLOAD)
    base = root / "game/data"
    base.mkdir(parents=True)
    (base / "item.bin").write_bytes(b"synthetic-base-content")
    (base / "unlisted.bin").write_bytes(b"synthetic-base-unlisted")
    manifest = {
        "schema": 1, "id": "fixture", "name": "Synthetic fixture",
        "version": "1.0.0", "author": "Synthetic control",
        "files": [{"path": "data/item.bin", "size": len(PAYLOAD),
                   "sha256": hashlib.sha256(PAYLOAD).hexdigest()}],
    }
    (root / "mods/fixture/mod.json").write_text(json.dumps(manifest))


def captured(command: list[str], log: Path, env: dict[str, str] | None = None,
             timeout: int = 120) -> subprocess.CompletedProcess:
    result = subprocess.run(command, env=env, capture_output=True, timeout=timeout)
    log.write_bytes(result.stdout + result.stderr)
    return result


def wine_command(bwrap: Path, wine: Path, run: Path, executable: str,
                 case: str, user: Path, unicode_child: bool = False,
                 native_fixture: bool = False) -> list[str]:
    inside = "/run/ce-mod-tests"
    command = [str(bwrap), "--die-with-parent", "--new-session", "--unshare-all",
            "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
            "--symlink", "usr/bin", "/bin", "--symlink", "usr/lib", "/lib",
            "--symlink", "usr/lib", "/lib64", "--proc", "/proc", "--dev", "/dev",
            "--tmpfs", "/tmp", "--dir", "/run",
            "--bind", str(run), inside, "--chdir", inside,
            "--setenv", "HOME", inside + "/profile",
            "--setenv", "XDG_CONFIG_HOME", inside + "/xdg-config",
            "--setenv", "XDG_DATA_HOME", inside + "/xdg-data",
            "--setenv", "XDG_CACHE_HOME", inside + "/xdg-cache",
            "--setenv", "XDG_RUNTIME_DIR", inside + "/xdg-runtime",
            "--setenv", "WINEPREFIX", inside + "/wine-prefix",
            str(wine), inside + "/" + Path(executable).name,
            "Z:" + (inside + "/" + user.name).replace("/", "\\"), case]
    if native_fixture:
        command.append("make-fixture")
    if unicode_child:
        command.append("unicode-child")
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", required=True, type=Path)
    parser.add_argument("--cc", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "local-data/mods-windows-readonly")
    parser.add_argument("--runtime-source", type=Path,
                        default=ROOT / "port/platform/common/mods.cpp")
    parser.add_argument("--windows-files-source", type=Path,
                        default=ROOT / "port/platform/windows/files.cpp")
    parser.add_argument("--wine", type=Path)
    parser.add_argument("--bwrap", type=Path)
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--ascii", action="store_true")
    parser.add_argument("--unicode-child", action="store_true",
                        help="Use ASCII driver arguments with an internal Unicode fixture child")
    parser.add_argument("--native-fixture", action="store_true",
                        help="Have the Windows driver create a fresh owned fixture instead of staging one")
    args = parser.parse_args()
    if args.wine and not args.bwrap:
        parser.error("--wine requires --bwrap for filesystem and network isolation")
    args.out.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=args.out.resolve()))
    os.chmod(run, 0o700)
    source = args.runtime_source.read_text()
    if source.count('#elif defined(_WIN32) && !defined(PORT_MODS_WINDOWS_ON)') != 1:
        raise RuntimeError("production Windows switch was not found exactly once")
    mmio = run / "mmio-under-test.h"
    windows_source = args.windows_files_source.read_text()
    mmio.write_text(function(windows_source, "PortMmioOpenA"))
    helpers = run / "windows-readers-under-test.h"
    helpers.write_text("\n".join(function(windows_source, name) for name in
        ("Absolute", "Relative", "ModMayReplace", "ModRead")))
    driver = ROOT / "port/tools/tests/mods_windows_readonly_driver.cpp"
    cc = args.cc or args.cxx.with_name(args.cxx.name.removesuffix("++"))
    sha = run / "sha256.obj"
    result = captured([str(cc), "-O1", "-c", str(ROOT / "port/updater/sha256.c"),
                       "-o", str(sha)], run / "sha.compile.private.log")
    if result.returncode:
        print(json.dumps({"phase": "sha-build", "exit": result.returncode, "evidence": str(run)}))
        return 1
    builds = {}
    for name, switch in (("production-fence", []), ("readonly", ["-DPORT_MODS_WINDOWS_ON=1"])):
        executable = run / (name + ".exe")
        command = [str(args.cxx), "-std=c++17", "-O1", "-static", "-municode",
                   "-DCE_NETWORK_OFF", *switch, "-I" + str(ROOT / "port/platform/common"),
                   '-DCE_WINDOWS_MODS_SOURCE="' + str(args.runtime_source.resolve()) + '"',
                   '-DCE_WINDOWS_MODS_MMIO_WRAPPER="' + str(mmio) + '"',
                   '-DCE_WINDOWS_MODS_READER_HELPERS="' + str(helpers) + '"',
                   str(driver), str(sha), "-lwinmm", "-o", str(executable)]
        result = captured(command, run / (name + ".compile.private.log"))
        builds[name] = {"exit": result.returncode, "path": str(executable)}
        if result.returncode:
            print(json.dumps({"phase": "driver-build", "builds": builds, "evidence": str(run)}))
            return 1
    receipt = {
        "runtime_source_sha256": hashlib.sha256(args.runtime_source.read_bytes()).hexdigest(),
        "windows_files_source_sha256": hashlib.sha256(args.windows_files_source.read_bytes()).hexdigest(),
        "driver_source_sha256": hashlib.sha256(driver.read_bytes()).hexdigest(),
        "control_harness_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "mmio_wrapper_sha256": hashlib.sha256(mmio.read_bytes()).hexdigest(),
        "windows_reader_helpers_sha256": hashlib.sha256(helpers.read_bytes()).hexdigest(),
        "strict_documented_nt_options": True,
        "production_fence_retained": True, "builds": builds, "cases": {},
        "native_windows_acceptance": False,
    }
    if args.wine:
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("BEA_", "WINE", "XDG_")) and
               key not in {"DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS",
                           "PULSE_SERVER", "PIPEWIRE_REMOTE", "SSH_AUTH_SOCK"}}
        env.update({"WINEDEBUG": "-all", "WINEDLLOVERRIDES": "winemenubuilder.exe=d",
                    "BEA_REPORT_NO_OPEN": "1", "BROWSER": "/bin/true",
                    "SDL_AUDIO_DRIVER": "dummy"})
        for name in ("profile", "xdg-config", "xdg-data", "xdg-cache", "xdg-runtime"):
            (run / name).mkdir(mode=0o700)
        for case in ("fence", *(args.case or CASES)):
            # The legacy base MMIO reader remains ANSI; these controls isolate
            # selector/fallback behavior in an ASCII synthetic base directory.
            ascii_root = args.ascii or case in ("mmio-no-selector", "mmio-unlisted")
            unicode_child = args.unicode_child and not ascii_root
            user = run / (case + ("-user" if ascii_root or unicode_child else "-user-\u00e9\U0001f98a"))
            if not args.native_fixture:
                fixture(user / "user-\u00e9\U0001f98a" if unicode_child else user)
            executable = builds["production-fence" if case == "fence" else "readonly"]["path"]
            command = wine_command(args.bwrap, args.wine, run, executable, case, user,
                                   unicode_child=unicode_child, native_fixture=args.native_fixture)
            result = captured(command, run / (case + ".private.log"), env=env)
            facts = []
            for line in result.stdout.decode("utf-8", errors="replace").splitlines():
                try:
                    value = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict) and isinstance(value.get("synthetic_control_pass"), bool):
                    facts.append(value)
            passed = result.returncode == 0 and len(facts) == 1 and \
                facts[0].get("synthetic_control_pass") is True and \
                facts[0].get("invalid_directory_options") == 0 and \
                facts[0].get("missing_no_reparse") == 0
            receipt["cases"][case] = {"exit": result.returncode, "passed": passed,
                                      "control_facts": facts}
        receipt["wine_prefix"] = str(run / "wine-prefix")
    (run / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"phase": "complete", "builds_pass": True,
                      "cases": receipt["cases"], "native_windows_acceptance": False,
                      "evidence": str(run)}))
    return int(any(not case["passed"] for case in receipt["cases"].values()))


if __name__ == "__main__":
    raise SystemExit(main())
