#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual savecard callers and strict save-name codec on authored private files, with ASan and old-source negatives.

The complete matching PCMemoryCard.cpp is compiled before/after patch0066; actual UMTexture converters serve its
unchanged path, and the production CRT FILE wrapper owns real buffering/closes. Native enumeration/path delivery
is a bounded host fixture. Windows Unicode APIs and native NTFS require their separate native gate.
Linux's unchanged legacy byte namespace is an explicit control. No game, real save/profile, assets or display.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

COMMON = r'''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint16_t WCHAR; typedef char TCHAR; typedef int BOOL; typedef uint32_t DWORD;
#define TRUE 1
#define FALSE 0
#define PC 1
#define TARGET PC
#define MAX_PATH 260
#define _A_HIDDEN 2
#define _A_SYSTEM 4
#define _A_SUBDIR 16
#define MCE_SUCCESS 0
#define MCE_FAILURE 1
#define MCE_FILEEXISTS 6
#define MCT_CARD 1
struct _finddata_t { unsigned attrib; char name[260]; };
class CMemoryCard {};
void SetLastError(DWORD);
#include "PCMemoryCard.h"
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--baseline-root", type=Path, help="optional unchanged Windows FindScan source for independent negatives")
    parser.add_argument("--blocked-patch", type=Path, help="optional frozen 0066 candidate for the destructive case-alias negative")
    parser.add_argument("--identity-baseline", type=Path, help="optional frozen files.cpp for 64-bit file-ID negative controls")
    parser.add_argument("--zero-error-baseline", type=Path, help="optional frozen files.cpp for native-FALSE/last-error-zero negatives")
    parser.add_argument("--query-baseline", type=Path, help="optional frozen files.cpp to reproduce unsupported-case-query caller refusal")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/save-name-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"inputs": {}, "commands": [], "cases": [], "passed": False,
               "limits": ["Authored host files; native Windows/NTFS/WinAPI behavior is not exercised.",
                          "Windows savecard branch is admitted in this host harness; Linux byte namespace remains unchanged.",
                          "Invalid/overlong names are refused; no real files are migrated, renamed or deleted."]}
    env = dict(os.environ, TMPDIR=str(out), ASAN_OPTIONS="halt_on_error=1:detect_leaks=1:exitcode=65")
    try:
        sources = ("src/PCMemoryCard.cpp", "src/PCMemoryCard.h", "src/UMTexture.cpp", "src/FEPSaveGame.cpp",
                   "port/platform/common/save_names.h", "port/platform/common/save_names.cpp",
                   "port/platform/common/crtfile.cpp", "port/platform/common/portlog.h",
                   "port/patches/0066-save-name-utf8.patch", "port/tools/tests/save_names_driver.cpp",
                   "port/platform/windows/files.cpp", "port/platform/windows/save_namespace.cpp",
                   "port/tools/tests/save_find_driver.cpp", "port/tools/tests/save_namespace_boundary.h",
                   "port/tools/tests/save_namespace_driver.cpp", "port/tools/tests/save_query_fixture.h", "port/tools/tests/save_overwrite_driver.cpp", "port/build.py")
        for name in sources:
            dest = out / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, dest)
            receipt["inputs"][name] = sha(dest)
        # The later loader copies names into32 units even though direct GetSaveName callers allocate100/256.
        for path, text in (("src/FEPDirectory.cpp", "WCHAR[256]"), ("src/FEPVirtualKeyboard.cpp", "WCHAR\tname[100]"),
                           ("src/FEPLoadGame.cpp", "wcscpy(FRONTEND.mFEPVirtualKeyboard.m_strData, mSaveGameName)"),
                           ("src/FEPVirtualKeyboard.h", "m_strData[32]")):
            source = ROOT / path
            if text not in source.read_text():
                raise RuntimeError(f"caller capacity evidence changed: {path}")
            receipt["inputs"][path] = sha(source)
        raw = (out / "src/UMTexture.cpp").read_text()
        start = raw.index("static char\t\tsFromWCHAR")
        end = raw.index("return sFromWCHAR[sFromWCHARNext];", start) + len("return sFromWCHAR[sFromWCHARNext];")
        (out / "converters.inc").write_text(raw[start:end] + "\n}\n")
        (out / "common.h").write_text(COMMON)
        (out / "windows.h").write_text('''#pragma once
#include <stdint.h>
typedef uint16_t WCHAR; typedef uint32_t DWORD; typedef int BOOL;
#define TRUE 1
#define ERROR_INVALID_NAME 123
#define CSTR_EQUAL 2
void SetLastError(DWORD);
DWORD GetLastError();
int CompareStringOrdinal(const WCHAR *, int, const WCHAR *, int, BOOL);
''')
        for name in ("memorycard.h", "Platform.h", "MemoryCard.h", "io.h", "direct.h", "mathsconst.h"):
            (out / name).write_text('#pragma once\n#include "common.h"\n')
        shutil.copyfile(out / "src/PCMemoryCard.h", out / "PCMemoryCard.h")
        shutil.copyfile(out / "src/PCMemoryCard.cpp", out / "baseline.cpp")
        shutil.copyfile(out / "src/PCMemoryCard.cpp", out / "PCMemoryCard.cpp")
        shutil.copyfile(out / "src/FEPSaveGame.cpp", out / "FEPSaveGame.cpp")
        patched = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-i",
                                  str(out / "port/patches/0066-save-name-utf8.patch")], cwd=out,
                                 capture_output=True, text=True, timeout=10)
        if patched.returncode or "fuzz" in patched.stdout:
            raise RuntimeError("savecard patch did not apply exactly: " + patched.stdout + patched.stderr)
        # Build's real include normalizer runs before patches. Raw-only admission missed an include-case context bug.
        build_tree=ast.parse((out / "port/build.py").read_text())
        definitions=[node for node in build_tree.body if isinstance(node,(ast.FunctionDef,ast.ClassDef)) and node.name in ("normalise","CaseIndex")]
        if len(definitions)!=2: raise RuntimeError("production normalizer definitions changed")
        namespace={"os":os,"Path":Path,"re":re,"SYSTEM_NAMES":{},
                   "INCLUDE":re.compile(r'^(\s*#\s*include\s*)([<"])([^>"]+)([>"])',re.M)}
        exec(compile(ast.Module(body=definitions,type_ignores=[]),"production-normalizer","exec"),namespace)
        normalized=out/"normalized"
        normalized.mkdir()
        tracked=subprocess.run(["git","ls-files","-z","src"],cwd=ROOT,capture_output=True,check=True).stdout.decode().strip('\0').split('\0')
        index=namespace["CaseIndex"]([normalized/Path(name).relative_to("src") for name in tracked if name])
        for name in ("PCMemoryCard.cpp","FEPSaveGame.cpp"):
            text=namespace["normalise"]((out/"src"/name).read_text(),normalized,index,[normalized,normalized/"MissionScript"])
            (normalized/name).write_text(text)
            receipt["inputs"]["normalized-before/"+name]=sha(normalized/name)
        admitted=subprocess.run(["patch","--batch","--forward","--fuzz=0","-p1","-i",str(out/"port/patches/0066-save-name-utf8.patch")],
                                cwd=normalized,capture_output=True,text=True,timeout=10)
        (out/"normalized-patch.log").write_text(admitted.stdout+admitted.stderr)
        ok=not admitted.returncode and "fuzz" not in admitted.stdout
        receipt["cases"].append({"binary":"patch","case":"production-normalized-source","returncode":admitted.returncode,"passed":ok})
        if not ok: raise RuntimeError("0066 failed after production include normalization")
        objects = [out / "port/platform/common/save_names.cpp", out / "port/platform/common/crtfile.cpp",
                   out / "port/platform/windows/save_namespace.cpp",
                   out / "port/tools/tests/save_names_driver.cpp"]
        def build(label, source, windows):
            binary = out / label
            command = [*shlex.split(args.cxx), "-std=c++17", "-O1", "-g", "-pthread", "-fsanitize=address",
                       "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
                       "-I" + str(out), "-I" + str(out / "port/platform/common"),
                       '-DSAVE_CARD_SOURCE="' + str(source) + '"', '-DCONVERTER_SOURCE="' + str(out / "converters.inc") + '"']
            if windows:
                command.append("-DTEST_WINDOWS_SAVES=1")
            command += [*map(str, objects), "-o", str(binary)]
            receipt["commands"].append(command)
            with (out / (label + ".build.log")).open("w") as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, timeout=90)
            if result.returncode:
                raise RuntimeError(f"compile failed: {label}.build.log")
            return binary
        def run(binary, case, expected=0):
            folder = out / (binary.name + "-" + case)
            folder.mkdir()
            result = subprocess.run([str(binary), case, str(folder)], capture_output=True, env=env, timeout=15)
            (folder / "stdout.txt").write_bytes(result.stdout)
            (folder / "stderr.txt").write_bytes(result.stderr)
            ok = result.returncode == expected
            receipt["cases"].append({"binary": binary.name, "case": case, "expected": expected,
                                     "returncode": result.returncode, "passed": ok})
            if not ok:
                raise RuntimeError(f"{binary.name}/{case}: expected {expected}, got {result.returncode}")
        current = build("windows", out / "PCMemoryCard.cpp", True)
        for case in ("codec", "ascii", "latin1", "literal-utf8-bytes", "bmp", "nonbmp", "length31", "invalid", "listing",
                     "case-admission", "ascii-case-admission", "exclusive-race", "distinct-names", "approval", "case-loss-regression", "native-refusal-preservation"):
            run(current, case)
        if args.blocked_patch:
            blocked = out / "blocked"
            blocked.mkdir()
            shutil.copyfile(out / "src/PCMemoryCard.cpp", blocked / "PCMemoryCard.cpp")
            shutil.copyfile(out / "src/FEPSaveGame.cpp", blocked / "FEPSaveGame.cpp")
            shutil.copyfile(args.blocked_patch, blocked / "0066.patch")
            receipt["inputs"]["blocked/0066.patch"] = sha(blocked / "0066.patch")
            result = subprocess.run(["patch","--batch","--fuzz=0","-p1","-i",str(blocked / "0066.patch")],cwd=blocked,
                                    capture_output=True,text=True,timeout=10)
            if result.returncode: raise RuntimeError("frozen blocked patch failed: "+result.stdout+result.stderr)
            binary = build("blocked-windows",blocked / "PCMemoryCard.cpp",True)
            run(binary,"case-loss-regression",1)
        baseline = build("unchanged", out / "baseline.cpp", True)
        run(baseline, "ascii")
        for case in ("latin1", "literal-utf8-bytes", "bmp", "nonbmp", "invalid"):
            run(baseline, case, 1)
        linux = build("linux", out / "PCMemoryCard.cpp", False)
        run(linux, "ascii")
        run(linux, "linux-legacy")
        def build_scan(label, source):
            pattern = r"^void FindScan\(Find \*f, const char \*spec\)\n\{\n.*?^\}"
            definitions = re.findall(pattern, source, re.M | re.S)
            if len(definitions) != 1:
                raise RuntimeError("expected one complete production FindScan")
            definition = out / (label + ".inc")
            definition.write_text(definitions[0] + "\n")
            receipt["inputs"][definition.name] = sha(definition)
            binary = out / label
            command = [*shlex.split(args.cxx), "-std=c++17", "-O1", "-g", "-fsanitize=address",
                       "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                       "-I" + str(out / "port/platform/common"), '-DFIND_SCAN_SOURCE="' + str(definition) + '"',
                       str(out / "port/platform/common/save_names.cpp"), str(out / "port/tools/tests/save_find_driver.cpp"),
                       "-o", str(binary)]
            receipt["commands"].append(command)
            with (out / (label + ".build.log")).open("w") as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, timeout=90)
            if result.returncode:
                raise RuntimeError(f"compile failed: {label}.build.log")
            return binary
        def scan_run(binary, case, expected=0):
            result = subprocess.run([str(binary), case], capture_output=True, env=env, timeout=10)
            (out / (binary.name + "-" + case + ".stderr.txt")).write_bytes(result.stderr)
            ok = result.returncode == expected
            receipt["cases"].append({"binary": binary.name, "case": case, "expected": expected,
                                     "returncode": result.returncode, "passed": ok})
            if not ok: raise RuntimeError(f"{binary.name}/{case}: expected {expected}, got {result.returncode}")
        scan = build_scan("find-scan", (out / "port/platform/windows/files.cpp").read_text())
        for case in ("valid", "resource", "invalid-spec", "long-spec", "decode-query", "decode-copy", "encode-query", "encode-copy", "native-open", "native-next", "case-sensitive"):
            scan_run(scan, case)
        production = (out / "port/platform/windows/files.cpp").read_text()
        def build_namespace(label, identity_source=None, namespace_source=None):
            definitions = []
            namespace_source = namespace_source or production
            for name in ("SaveNativeSameFile", "FindAdd", "SaveDirectoryCasePolicy", "FindScan", "FindNameOrder", "FindSort", "FindStart", "FindFree", "SaveFoldedNameAbsent", "PortSaveLookupUtf16", "PortSaveReserveUtf8"):
                source = identity_source if name == "SaveNativeSameFile" and identity_source else namespace_source
                found = re.findall(r"^[^\n]*\b" + re.escape(name) + r"\([^\n]*\)\n\{\n.*?^\}", source, re.M | re.S)
                if name=="SaveFoldedNameAbsent" and not found:continue
                if len(found) != 1: raise RuntimeError(f"expected complete production function: {name}")
                definitions.append(found[0])
            definition = out / (label+".inc")
            definition.write_text('\n'.join(definitions) + '\n')
            receipt["inputs"][definition.name] = sha(definition)
            binary = out / label
            command = [*shlex.split(args.cxx), "-std=c++17", "-O1", "-g", "-fsanitize=address", "-fno-omit-frame-pointer",
                       "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter", "-I"+str(out), "-I"+str(out / "port/platform/common"),
                       "-DSAVE_NAMESPACE_SOURCE=1", '-DSAVE_NAMESPACE_BOUNDARY="'+str(out / "port/tools/tests/save_namespace_boundary.h")+'"',
                       '-DSAVE_CARD_CREATE_SOURCE="'+str(out / "create-save.inc")+'"',
                       '-DCASE_QUERY_OUTPUT='+str(int('SaveDirectoryCase *policy' in namespace_source)),
                       '-DFIND_SCAN_SOURCE="'+str(definition)+'"', str(out / "port/platform/common/save_names.cpp"),
                       str(out / "port/platform/windows/save_namespace.cpp"), str(out / "port/tools/tests/save_namespace_driver.cpp"),
                       "-o",str(binary)]
            receipt["commands"].append(command)
            with (out / (label+".build.log")).open("w") as log:
                compiled = subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=90)
            if compiled.returncode: raise RuntimeError("compile failed: "+label+".build.log")
            return binary
        create = re.findall(r'^int CPCMemoryCard::CreateSave\([^\n]*\)[^\n]*\n\{\n.*?^\}',(out/'PCMemoryCard.cpp').read_text(),re.M|re.S)
        if len(create)!=1:raise RuntimeError('expected complete patched CreateSave caller')
        (out/'create-save.inc').write_text(create[0]+'\n');receipt['inputs']['create-save.inc']=sha(out/'create-save.inc')
        namespace_binary = build_namespace("namespace")
        for case in ("overlay","overlay-disagreement","same-root","distinct","ascii-overlay","resource-overlay","lookup","case-disagreement","lookup-error","identity-disagreement","identity-denied",
                     "reserve","already-exists","reserve-invalid","case-sensitive","directory-missing","directory-denied","query-denied","old-windows",
                     "fileid-same","fileid-upper64","fileid-volume-upper","fileid-unsupported","fileid-bytes",
                     "fileid-failure-zero","fileid-query-error-preserved","fileid-both-queries-zero"):
            scan_run(namespace_binary,case)
        for case in ('query-known','query120','case-flags2','query-zero','directory-open-zero','directory-close-zero','directory-close-denied','query-denied-close-zero'):
            scan_run(namespace_binary,case)
        coupled_cases=('unknown-valid','unknown-uppercase','unknown-same-root','unknown-cross-root','unknown-short-alias',
                       'unknown-partial-enum','unknown-enum-open-zero','unknown-enum-next-zero','unknown-enum-close-zero','unknown-enum-close-denied',
                       'unknown-hidden-alias','unknown-system-alias','unknown-directory-alias',
                       'unknown-between-prelookup-reserve','unknown-between-absence-create','unknown-after-reserve',
                       'unknown-query-denied','unknown-known-sensitive')
        for case in coupled_cases:run(namespace_binary,case)
        if args.query_baseline:
            copied=out/'query-baseline.cpp';shutil.copyfile(args.query_baseline,copied);receipt['inputs'][copied.name]=sha(copied)
            old_query=build_namespace('query-baseline',namespace_source=copied.read_text())
            run(old_query,'unknown-valid',1)
            stdout=(out/'query-baseline-unknown-valid/stdout.txt').read_text()
            if 'result=1 error=120' not in stdout:raise RuntimeError('old query caller failed without reproducing120')
        if args.identity_baseline:
            copied=out/"identity-baseline.cpp"
            shutil.copyfile(args.identity_baseline,copied)
            receipt["inputs"][copied.name]=sha(copied)
            old_identity=build_namespace("identity-baseline",copied.read_text())
            scan_run(old_identity,"fileid-same")
            for case in ("fileid-upper64","fileid-volume-upper"):
                scan_run(old_identity,case,1)
        if args.zero_error_baseline:
            copied=out/"zero-error-baseline.cpp"
            shutil.copyfile(args.zero_error_baseline,copied)
            receipt["inputs"][copied.name]=sha(copied)
            old_error=build_namespace("zero-error-baseline",copied.read_text())
            scan_run(old_error,"fileid-same")
            scan_run(old_error,"fileid-query-error-preserved")
            for case in ("fileid-failure-zero","fileid-both-queries-zero"):
                scan_run(old_error,case,1)
        ui_raw = (out / "FEPSaveGame.cpp").read_text()
        ui_functions = []
        for name in ("ButtonPressed", "Process", "CreateSave"):
            found = re.findall(r"^void[^\n]*CFEPSaveGame::"+name+r"\([^\n]*\)[^\n]*\n\{\n.*?^\}",ui_raw,re.M|re.S)
            if len(found)!=1: raise RuntimeError(f"expected complete save-page method: {name}")
            ui_functions.append(found[0])
        ui_source = out / "ui.inc"
        ui_source.write_text('\n'.join(ui_functions)+'\n')
        receipt["inputs"][ui_source.name]=sha(ui_source)
        constants = sorted(set(re.findall(r"\b(?:FEPS|FEMB|FEX|FEP|FONT|BUTTON|FES|MCE)_[A-Z0-9_]+\b",ui_source.read_text()))
                           - {"MCE_SUCCESS","MCE_FAILURE","MCE_FILEEXISTS"})
        (out / "ui_constants.inc").write_text("enum { "+", ".join(name+"="+str(100+i) for i,name in enumerate(constants))+" };\n")
        ui_binary=out/"overwrite-ui"
        command = [*shlex.split(args.cxx), "-std=c++17", "-O1", "-g", "-pthread", "-fsanitize=address", "-fno-omit-frame-pointer",
                   "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-missing-braces",
                   "-I"+str(out), "-I"+str(out/"port/platform/common"), "-DTEST_WINDOWS_SAVES=1",
                   '-DSAVE_CARD_SOURCE="'+str(out/"PCMemoryCard.cpp")+'"','-DCONVERTER_SOURCE="'+str(out/"converters.inc")+'"',
                   '-DUI_SOURCE="'+str(ui_source)+'"',str(out/"port/platform/common/save_names.cpp"),
                   str(out/"port/platform/common/crtfile.cpp"),str(out/"port/platform/windows/save_namespace.cpp"),
                   str(out/"port/tools/tests/save_overwrite_driver.cpp"),"-o",str(ui_binary)]
        receipt["commands"].append(command)
        with (out/"overwrite-ui.build.log").open("w") as log:
            compiled=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=90)
        if compiled.returncode: raise RuntimeError("compile failed: overwrite-ui.build.log")
        for case in ("unicode-yes","ascii-yes","admission-failure","mismatch","early-failure","cancel","back","missing-index"):
            run(ui_binary,case)
        receipt["limits"].append("Actual FindScan uses mocked WinAPI boundaries; iconv independently implements strict conversion in the host fixture.")
        receipt["limits"].append("Ordinal/native lookup uses an explicit ASCII+U+00C9/U+00E9 case model; native Windows volume/case/overlay behavior remains a separate gate. WriteSave is not atomic and its I/O failure behavior is unchanged.")
        receipt["documentation"]={
            "128-bit native identity":"https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info",
            "ReFS 64-bit nonuniqueness":"https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information",
            "FileIdInfo Windows8 minimum":"https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-file_info_by_handle_class"}
        if args.baseline_root:
            old = args.baseline_root / "port/platform/windows/files.cpp"
            copied = out / "baseline/port/platform/windows/files.cpp"
            copied.parent.mkdir(parents=True)
            shutil.copyfile(old, copied)
            receipt["inputs"][str(copied.relative_to(out))] = sha(copied)
            old_scan = build_scan("old-find-scan", copied.read_text())
            scan_run(old_scan, "resource")
            for case in ("valid", "invalid-spec", "decode-copy"):
                scan_run(old_scan, case, 1)
        receipt["passed"] = True
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"{'PASS' if receipt['passed'] else 'FAIL'} {len(receipt['cases'])} cases: {out / 'result.json'}")
    if "error" in receipt: print(receipt["error"])
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
