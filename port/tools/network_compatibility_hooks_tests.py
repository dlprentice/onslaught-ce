#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual fresh-process hook wiring with tiny authored files; no game, devices or retail payloads.

Host controls compile the production boot/core/platform/CRT/CE modules and exact patched entry/console/card-ID
caller bodies, with explicit game-object boundaries. Target syntax checks the complete affected callers through
the normalized public mirror. Windows API seam controls are host models, never Windows runtime acceptance.
Every invocation snapshots its source into a fresh retained repo-local owner.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
sys.path.insert(0, str(ROOT / "port"))
import build
import deps
from network_compatibility_tests import FILES


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def body(text, signature):
    start = text.index(signature)
    at = text.index("{", start)
    level, end = 1, at + 1
    while level:
        level += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


def execute(command, out, label, *, env=None, timeout=50, expected=0):
    proc = subprocess.run(command, capture_output=True, text=True, env=env, timeout=timeout)
    (out / (label + ".log")).write_text(proc.stdout + proc.stderr)
    record = {"command": list(map(str, command)), "exit": proc.returncode, "expected": expected}
    if proc.returncode != expected:
        raise RuntimeError(f"{label}: exit {proc.returncode}, expected {expected}\n{proc.stdout}{proc.stderr}")
    return record


def snapshot(out):
    product = out / "product"
    names = ["port/game/ce_network_boot.cpp", "port/game/ce_network_boot.h",
        "port/game/ce_project.cpp", "port/game/ce_project.h", "port/game/ce_network_stl.h", "port/platform/common/ce_netcode.h", "port/network/network.h", "port/network/compatibility.cpp", "port/network/native_posix_stat.cpp", "port/network/native_posix_stat.h",
             "port/network/compatibility.h", "port/platform/linux/files.cpp", "port/platform/linux/paths.cpp",
             "port/platform/linux/handles.h", "port/platform/linux/mmio.cpp", "port/platform/common/crtfile.cpp",
             "port/platform/common/ce.cpp", "port/platform/common/platform.h", "port/platform/windows/files.cpp",
             "port/d3dx/src/texture.cpp", "port/patches/0082-compatibility-hooks.patch", "port/platform/common/portlog.h",
             "port/platform/common/mods.h"]	# files.cpp includes the mod layer's header
    names += [str(p.relative_to(ROOT)) for p in sorted((ROOT / "port/updater").glob("*.h"))]
    for name in names:
        target = product / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)
    callers = out / "callers"
    callers.mkdir()
    for name in ("ltshell.cpp", "console.cpp", "game.cpp", "tweak.cpp", "DebugLog.cpp", "FEPOptions.cpp"):
        shutil.copyfile(ROOT / "src" / name, callers / name)
    patch = (product / "port/patches/0082-compatibility-hooks.patch").read_text()
    execute(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(callers), "-i",
             str(product / "port/patches/0082-compatibility-hooks.patch")], out, "raw-patch")
    stubs = out / "stubs"
    stubs.mkdir()
    (stubs / "common.h").write_text('#pragma once\n#include <windows.h>\n#include <strings.h>\nusing SINT = int;\n'
        '#define stricmp strcasecmp\n#define _stricmp strcasecmp\n#define _strnicmp strncasecmp\n')
    for name in ("CLIParams.h", "tweak.h"):
        shutil.copyfile(ROOT / "src" / name, stubs / name)
    shell = (callers / "ltshell.cpp").read_text()
    entry = shell[shell.index("\tCLIPARAMS.GetParams(lpCmdLine);"):]
    entry = entry[:entry.index("\tCAREER.Blank();") + len("\tCAREER.Blank();")]
    log = (callers / "DebugLog.cpp").read_text()
    logs = "\n".join(body(log, s) for s in ("void\tCDebugLog::AddMessage", "void\tCDebugLog::AddString"))
    tweak = (callers / "tweak.cpp").read_text()
    tweaks = "\n".join(body(tweak, s) for s in ("CTweak::CTweak", "CTweak::~CTweak", "static bool\tReadHexLine",
        "void\tLoadCardId", "CTweakSLONG::CTweakSLONG", "void\tCTweakSLONG::Set", "bool\tCTweakSLONG::IsSet",
        "void\tCTweakSLONG::SetDefault"))
    console = (callers / "console.cpp").read_text()
    consoles = "\n".join(body(console, s) for s in ("void CConsole::ExecuteCommand", "void CConsole::SetVariable", "void CConsole::Exec"))
    # The startup-script gate is the network's tail patch 0146 on main's console patch 0100, so game.cpp is the whole queue's text as a
    # build with the network sees it (patch 0082 alone no longer holds it).
    game = build.patched_text(["game.cpp"], network=True)["game.cpp"]
    automatic = game[game.index("\tint ce_script = CE_BootAutomaticScript();"):]
    # The script call ends the fragment; patch 0206 wraps it ((ceEditorAuthoring ? (void)0 : CONSOLE.Exec("autoexec.con"));), so the statement is read to its semicolon.
    call = automatic.index('CONSOLE.Exec("autoexec.con")')
    automatic = automatic[:automatic.index(";", call) + 1]
    template = (ROOT / "port/tools/tests/network_boot_driver.cpp.in").read_text()
    driver = template.replace("@ENTRY@", entry).replace("@LOG_CALLERS@", logs).replace("@TWEAK_CALLERS@", tweaks)
    driver = driver.replace("@CONSOLE_CALLERS@", consoles).replace("@AUTOMATIC@", automatic)
    # Only native caller fclose references are game-CRT-renamed; fixture setup uses ordinary host FILEs.
    for part in (logs, tweaks, consoles, entry):
        driver = driver.replace(part, part.replace("fclose(", "bea_crt_fclose("))
    (out / "driver.cpp").write_text(driver)
    return product, {str(p.relative_to(out)): sha(p) for p in out.rglob("*") if p.is_file()}


def host(out, product, *, negative=None, fake_windows=False):
    # The pinned production compiler does not ship an i386 sanitizer runtime. Host controls use the installed
    # system compiler/runtime; separate complete-target syntax keeps the production compiler boundary explicit.
    compiler = Path(shutil.which("clang++"))
    mc = deps.SRC / "monocypher-4.0.2/src"
    crypto = out / "monocypher.o"
    records = [execute([str(Path(shutil.which("clang"))), "-m32", "-O1", "-g", "-fsanitize=address,undefined", "-c",
                        str(mc / "monocypher.c"), "-o", str(crypto)], out, "crypto-build")]
    boot = product / "port/game/ce_network_boot.cpp"
    if fake_windows:
        boot = out / "windows-order-boot.cpp"
        text = (product / "port/game/ce_network_boot.cpp").read_text().replace('#include "ce_network_boot.h"', '#include "port/game/ce_network_boot.h"')
        text = text.replace('#include "../platform/common/platform.h"', '#include "port/platform/common/platform.h"')
        boot.write_text('extern "C" void PortInitializeContentRoots(void);\n' +
                        text.replace("#ifdef _WIN32", "#ifdef BOOT_TEST_WINDOWS"))
    sources = [out / "driver.cpp", boot, product / "port/game/ce_project.cpp", product / "port/network/compatibility.cpp", product / "port/network/native_posix_stat.cpp", product / "port/platform/linux/files.cpp",
               product / "port/platform/linux/paths.cpp", product / "port/platform/linux/mmio.cpp",
               product / "port/platform/common/crtfile.cpp", product / "port/platform/common/ce.cpp", ROOT / "port/tools/tests/mods_none.cpp", crypto]
    command = [str(compiler), "-m32", "-std=c++17", "-O1", "-g", "-pthread", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-DNOMINMAX", "-fms-extensions", "-fshort-wchar",
               "-fno-autolink", "-Wno-ignored-attributes", "-Wno-invalid-noreturn", "-Wno-writable-strings",
               '-DCE_VERSION="fixture"', '-DCE_NETWORK_SIMULATION_ID="fixture-simulation-v1"',
               "-I" + str(out / "stubs"), "-I" + str(product), "-I" + str(product / "port/game"),
               "-I" + str(product / "port/platform/linux"), "-I" + str(ROOT / "port/include/linux"),
               "-I" + str(product / "port/include"), "-I" + str(deps.DATA / "dxvk-native/usr/include/dxvk"), "-I" + str(mc)]
    if fake_windows:
        command.append("-DBOOT_TEST_WINDOWS")
    binary = out / "hooks"
    command += [*map(str, sources), "-o", str(binary)]
    records.append(execute(command, out, "host-build"))
    cases = ["boot", "lockstep-boot", "offline", "bad-level", "bad-options", "cardid-absolute", "build", "demo", "both-roles",
             "supplied-context", "forced-layer-off", "automation", "root-invalid", "console", "variable", "script",
             "auto-nonempty", "cheat", "role-removed", "role-switched", "crt-mutate", "native-mutate", "direct-mutate",
             "write", "native-write", "unknown", "mmio", "native-wide", "local-invalid", "local-hardlink", "local-symlink", "retire"]
    if negative: cases = [negative[0]]
    results = []
    for case in cases:
        folder = out / ("case-" + case)
        game, user = folder / "game", folder / "user"
        user.mkdir(parents=True)
        for name, data in FILES.items():
            path = game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        if case == "lockstep-boot":
            (game / "data/resources/856_res_pc.aya").write_bytes(b"lockstep level archive sentinel")
        (user / "defaultoptions.bea").write_bytes(b"OPTIONS!")
        (user / "ce.ini").write_bytes(b"record_sessions=1\n")
        (game / "cardid.txt").write_text("Vendor:1234\nDevice:5678\nTweak:DISABLE_SNOW 1\nTweak:FORCE_VSYNC 1\nTweak:future_ignored 5\n")
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "COMPAT_", "ADMISSION_"))}
        env.update(BEA_GAME_DIR=str(game), BEA_USER_DIR=str(user), BEA_NET_HOST="127.0.0.1:1234",
                   ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1", TMPDIR=str(folder))
        if negative: env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        row = execute([str(binary), case], out, "case-" + case, env=env, timeout=10, expected=2 if negative else 0)
        if negative and ("FAIL " + negative[1]) not in (out / ("case-" + case + ".log")).read_text():
            raise RuntimeError("mutant failed for an unrelated reason: " + case)
        row.update(case=case, options_preserved=(user / "defaultoptions.bea").read_bytes() == b"OPTIONS!")
        if not row["options_preserved"]:
            raise RuntimeError("caller changed options bytes: " + case)
        results.append(row)
    return {"compile": records, "cases": results, "binary_sha256": sha(binary), "pass": True}


def controls(out):
    """Remove one consequential guard at a time; its actual caller case must detect the precise regression."""
    specs = [
        ("early-boot", "driver.cpp", "\tif (!CE_BootStart()) return 1;", "", "boot", "actual early boot supplies context", False),
        ("console-before-effects", "driver.cpp", "\tif (!CE_CompatibilityConsoleOperation(CE_COMPAT_CONSOLE_EXECUTE, line, strlen(line), NULL, 0)) return;", "", "console", "armed command refused before callback", False),
        ("crt-open-proof", "product/port/platform/linux/files.cpp", "file && !writes && !CE_CompatibilityValidateOpenedFile", "false && file && !writes && !CE_CompatibilityValidateOpenedFile", "crt-mutate", "CRT opened object proof refuses changed bytes", False),
        ("native-open-proof", "product/port/platform/linux/files.cpp", "!write && !CE_CompatibilityValidateOpenedNative", "false && !write && !CE_CompatibilityValidateOpenedNative", "native-mutate", "native opened object proof refuses changed bytes", False),
        ("direct-open-proof", "product/port/platform/common/crtfile.cpp", "file && !writes && !CE_CompatibilityValidateOpenedFile", "false && file && !writes && !CE_CompatibilityValidateOpenedFile", "direct-mutate", "direct CRT opened object proof refuses changed bytes", False),
        ("windows-init-order", "product/port/game/ce_network_boot.cpp", "if (!CE_CompatibilityValidateLauncherRoots(error, sizeof error)) return Refuse(error);", "PortInitializeContentRoots();\n    if (!CE_CompatibilityValidateLauncherRoots(error, sizeof error)) return Refuse(error);", "root-invalid", "early refusal precedes native initialization and automation preparation", True),
    ]
    rows = []
    for label, relative, old, new, case, message, windows in specs:
        folder = out / label; folder.mkdir()
        product, pins = snapshot(folder)
        file = folder / relative; text = file.read_text()
        if text.count(old) != 1: raise RuntimeError("mutant requires exactly one guard: " + label)
        file.write_text(text.replace(old, new))
        row = host(folder, product, negative=(case, message), fake_windows=windows)
        rows.append({"control": label, "original_source_pins": pins, "mutated_source_sha256": sha(file), "result": row})
    return {"controls": rows, "pass": True}


def windows_writes(out, product, *, negative=None):
    """Execute the exact Windows writer and owner policy with explicit host-backed native API seams."""
    driver = (ROOT / "port/tools/tests/network_local_windows_driver.cpp.in").read_text()
    policy = body((product / "port/game/ce_network_boot.cpp").read_text(),
                  'extern "C" int CE_BootLocalFileAllowed')
    writer = body((product / "port/platform/windows/files.cpp").read_text(), "FILE *PortOpenLocalFile")
    if negative:
        old, new, case, message = negative
        if writer.count(old) != 1: raise RuntimeError("Windows mutant requires exactly one guard")
        writer = writer.replace(old, new)
    source = out / "windows-write.cpp"
    source.write_text(driver.replace("@POLICY@", policy).replace("@WRITER@", writer))
    binary = out / "windows-write"
    command = [shutil.which("clang++"), "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", str(source), "-o", str(binary)]
    compiled = execute(command, out, "windows-write-build")
    cases = ["normal", "new", "append", "card-read", "offline", "bad-name", "root-missing", "root-file", "root-link",
             "open-fails", "create-race", "pipe", "basic-info", "file-link", "file-directory", "hardlink", "opened-id",
             "named-open-fails", "named-id", "upper-id", "volume", "named-close", "root-replaced", "fd-convert", "fdopen", "seek", "truncate"]
    if negative: cases = [case]
    results = []
    for case in cases:
        folder = out / ("windows-" + case); folder.mkdir()
        row = execute([str(binary), case, str(folder)], out, "windows-" + case, expected=2 if negative else 0)
        if negative and ("FAIL " + message) not in (out / ("windows-" + case + ".log")).read_text():
            raise RuntimeError("Windows mutant failed for an unrelated reason")
        results.append(row)
    return {"source_sha256": sha(source), "compile": compiled, "cases": results, "pass": True,
            "limits": "Host API seams only; complete native source target syntax is separate; no Windows runtime claim."}


def windows_controls(out, product):
    specs = [("upper-identity", "sizeof opened.FileId.Identifier);", "8);", "upper-id", "transfer only after both identities and root recheck"),
             ("file-type", "GetFileType(file) == FILE_TYPE_DISK", "true", "pipe", "writer admission result"),
             ("hardlink-count", "basic.nNumberOfLinks == 1", "true", "hardlink", "writer admission result")]
    rows = []
    for label, old, new, case, message in specs:
        folder = out / label; folder.mkdir()
        rows.append({"control": label, "result": windows_writes(folder, product, negative=(old, new, case, message))})
    return {"pass": True, "controls": rows}


def targets(out):
    """Build the ordinary public mirror with every patch, then compile affected callers with both pinned targets."""
    spec = importlib.util.spec_from_file_location("hook_target_build", ROOT / "port/build.py")
    build = importlib.util.module_from_spec(spec); spec.loader.exec_module(build)
    build.PUBLIC = True
    build.MIRROR_ROOT = out / "public-mirror"; build.MIRROR = build.MIRROR_ROOT / "src"
    build.build_mirror()
    rows = []
    callers = ["ltshell.cpp", "console.cpp", "game.cpp", "tweak.cpp", "DebugLog.cpp", "FEPOptions.cpp"]
    for name in ("linux", "windows"):
        target = build.TARGETS[name]
        includes = [build.PORT / "d3dx/include", build.PORT / "include", build.PORT / "game", *target["includes"],
                    build.MIRROR, build.MIRROR / "MissionScript"]
        common = [*target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87,
                  *build.NO_MATH_BUILTINS, '-DCE_VERSION="fixture"', "-include", str(build.PORT / "include/port.h"),
                  *["-I" + str(p) for p in includes], "-fsyntax-only"]
        for caller in callers:
            rows.append(execute([*common, str(build.MIRROR / caller)], out, "target-" + name + "-" + caller, timeout=90))
        includes += [deps.PREFIX / "include" if name == "linux" else deps.PREFIX_WIN / "include",
                     deps.SRC / "monocypher-4.0.2/src"]
        modules = ["game/ce_network_boot.cpp", "platform/common/crtfile.cpp", "platform/common/ce.cpp",
                   "platform/" + name + "/files.cpp", "d3dx/src/texture.cpp"]
        if name == "linux": modules.append("platform/linux/mmio.cpp")
        for module in modules:
            if module.startswith("game/"):
                rows.append(execute([*common, str(build.PORT / module)], out,
                                    "target-" + name + "-" + module.replace("/", "-"), timeout=90))
                continue
            command = [*target["cxx"], "-std=gnu++17", "-DNOMINMAX", "-fms-extensions", "-fno-autolink",
                       "-fshort-wchar", "-mms-bitfields", "-Wno-ignored-attributes", "-Wno-microsoft-cast",
                       "-Wno-writable-strings", "-Wno-invalid-noreturn", "-fsyntax-only",
                       *["-I" + str(p) for p in includes], str(build.PORT / module)]
            rows.append(execute(command, out, "target-" + name + "-" + module.replace("/", "-"), timeout=90))
    return {"pass": True, "commands": rows,
            "mirror_inputs": {str(p.relative_to(build.MIRROR_ROOT)): sha(p) for p in build.MIRROR_ROOT.rglob("*") if p.is_file()},
            "limits": "Both pinned target full caller/module syntax, public normalized mirror; no link/game/runtime acceptance."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("host", "windows-order", "windows-writes", "windows-controls", "targets", "controls"), default="host")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    owner = ROOT / "local-data/port/network-compatibility-hooks"
    owner.mkdir(parents=True, exist_ok=True)
    if args.output:
        out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    else:
        out = Path(tempfile.mkdtemp(prefix="run-", dir=owner))
    receipt = {"phase": args.phase, "limits": __doc__, "pass": False}
    try:
        product, receipt["source_pins"] = snapshot(out)
        if args.phase == "controls": receipt["result"] = controls(out)
        elif args.phase == "targets": receipt["result"] = targets(out)
        elif args.phase == "windows-writes": receipt["result"] = windows_writes(out, product)
        elif args.phase == "windows-controls": receipt["result"] = windows_controls(out, product)
        else: receipt["result"] = host(out, product, fake_windows=args.phase == "windows-order")
        receipt["pass"] = True
    finally:
        (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(out / "result.json", flush=True)


if __name__ == "__main__":
    main()
