#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded shared-FFmpeg producer/link/package controls, actual source consumers and PE/ELF import negatives.

Fresh subprocesses exercise host/sniper independently. Dependency builds/downloads are prohibited by the worker;
only tiny authored link controls are compiled against existing pinned libraries, and no binary is executed.
Optional --baseline-root retains old-source negatives; --candidate-pe supplies a known static negative PE to reject.
All generated source, binaries and receipts remain in ignored local-data/ffmpeg-prefix.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ABI = (("avformat", 61), ("avcodec", 61), ("avutil", 59))


def expected(data, mode, target, runtime=False):
    suffix = "-sniper" if mode == "sniper" and target == "linux" else ""
    prefix = data / ("ffmpeg-shared"+suffix) / target / "usr"
    if target == "windows":
        return [prefix / ("bin" if runtime else "lib") / (f"{name}-{abi}.dll" if runtime else f"lib{name}.dll.a") for name, abi in ABI]
    return [prefix / "lib" / (f"lib{name}.so.{abi}" if runtime else f"lib{name}.so") for name, abi in ABI]


def worker(args):
    sys.path.insert(0, str(args.worker / "port"))
    import build
    import deps
    import package
    target = args.case.rsplit("-", 1)[-1]
    if target not in ("linux", "windows"):
        target = "windows" if "windows" in args.case else "linux"
    links, runtime = expected(args.data, args.mode, target), expected(args.data, args.mode, target, True)
    for path in links+runtime:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"authored dependency-presence fixture\n")
    marker = package.WORK / "mirror/public-unreplaced.txt"
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text("")
    build.PUBLIC = True
    build.vorbis_lib = lambda target: args.data / "fixture-vorbis.a"
    if args.case.startswith("producer-"):
        deps.unpack = lambda row, dest: args.data / "authored-source"
        def forbidden(*args, **kwargs):
            raise AssertionError("producer ignored complete selected shared inputs and attempted a dependency build")
        deps.run = forbidden
        deps.build_ffmpeg_shared({"FFmpeg": {"version": "7.1.1"}}, target)
    elif args.case.startswith("link-"):
        flags = shlex.split(build.link_flags(target))
        assert all(str(path) in flags for path in links), "public linker did not name selected shared inputs explicitly"
        assert not any("-l"+name in flags for name, _ in ABI), "public linker permits static fallback"
    elif args.case.startswith("private-"):
        build.PUBLIC = False
        flags = shlex.split(build.link_flags(target))
        assert all("-l"+name in flags for name, _ in ABI), "private link contract changed"
    elif args.case.startswith("package-paths-"):
        selected = package.WINDOWS_DLLS if target == "windows" else [directory/name for directory, name in package.LINUX_LIBS]
        assert all(path in selected for path in runtime), "packager selected a different target's shared tree"
    elif args.case.startswith("missing-"):
        links[1].unlink()  # Owned authored presence fixture only; no real library is changed.
        try:
            if args.case.startswith("missing-link-"):
                build.link_flags(target)
            else:
                package.check_public(args.binary, target == "windows")
        except SystemExit as error:
            assert str(links[1]) in str(error), "refusal did not identify the missing selected shared input"
        else:
            raise AssertionError("missing public shared input was admitted")
    elif args.case == "preflight-no-output":
        build_dir = args.data / "authored-build"
        (build_dir/"linux").mkdir(parents=True)
        (build_dir/"linux/bea").symlink_to(args.binary)
        out = args.data / "package-output"
        package.source_epoch = lambda: 1
        def forbidden(*args, **kwargs):
            raise AssertionError("package began output before validating both selected shared trees")
        package.strip = forbidden
        sys.argv = ["package.py","test","--build",str(build_dir),"--out",str(out)]
        try:
            package.main()
        except SystemExit as error:
            assert "public windows FFmpeg shared inputs missing" in str(error), "preflight did not diagnose missing Windows inputs"
        else:
            raise AssertionError("incomplete package inputs admitted")
        assert not out.exists(), "failed preflight created partial package output"
    elif args.case.startswith("binary-"):
        rejected = False
        try:
            package.check_public(args.binary, target == "windows")
        except SystemExit as error:
            rejected = "FFmpeg shared dependencies" in str(error)
            if not rejected:
                raise
        should_reject = "static" in args.case or "partial" in args.case or "candidate" in args.case
        assert rejected == should_reject, "public binary dependency gate admitted static/partial linkage or rejected shared control"
    else:
        raise AssertionError("unknown test case")
    return 0


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-root", type=Path)
    parser.add_argument("--candidate-pe", type=Path, help="known static negative PE; this candidate must be rejected")
    parser.add_argument("--worker", type=Path)
    parser.add_argument("--case")
    parser.add_argument("--mode", choices=("host", "sniper"))
    parser.add_argument("--data", type=Path)
    parser.add_argument("--binary", type=Path)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if args.worker:
        return worker(args)
    sys.path.insert(0, str(ROOT / "port/tools"))
    import deps
    parent = ROOT / "local-data/ffmpeg-prefix"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="tests-", dir=parent))
    receipt = {"passed": False, "inputs": {}, "commands": [], "cases": [],
               "limits": ["Authored link-only executables; no Windows/Linux binary, game, VM or display is run.",
                          "Producer command execution is replaced by a rejecting boundary; no dependency is built, downloaded or moved.",
                          "Consumer presence fixtures are authored files. Actual pinned libraries are read only by tiny link controls."]}
    try:
        snapshots = []
        for label, source in (("current", ROOT), ("baseline", args.baseline_root)):
            if not source:
                continue
            snap = out / label
            for name in ("port/tools/deps.py", "port/build.py", "port/tools/package.py"):
                dest = snap / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source/name, dest)
                receipt["inputs"][str(dest.relative_to(out))] = sha(dest)
            for name in ("port/aliases.tsv", "port/bink/bink.cpp"):
                dest = snap / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT/name, dest)
                receipt["inputs"][str(dest.relative_to(out))] = sha(dest)
            snapshots.append((label, snap))
        common = "unsigned avformat_version(void); unsigned avcodec_version(void); unsigned avutil_version(void);\n"
        dynamic = common+"int main(void){return (int)(avformat_version()+avcodec_version()+avutil_version());}\n"
        static = "unsigned avformat_version(void){return 1;} unsigned avcodec_version(void){return 2;} unsigned avutil_version(void){return 3;}\n"+dynamic
        partial = "unsigned avutil_version(void){return 3;}\n"+dynamic
        for label, source in (("shared", dynamic), ("static", static), ("partial", partial)):
            (out/(label+".c")).write_text(source)
        binaries = {}
        for target in ("windows", "linux"):
            compiler = deps.DATA / ("llvm-mingw/bin/i686-w64-mingw32-clang" if target == "windows" else "llvm/bin/clang")
            # Always use the existing selected Linux sysroot for link controls; worker prefix controls cover both modes.
            flags = [] if target == "windows" else ["--target=i686-pc-linux-gnu", *deps.sysroot_link_flags(), "-Wl,-rpath,$ORIGIN/lib"]
            installed = deps.DATA / ("ffmpeg-shared/windows/usr/lib" if target == "windows" else "ffmpeg-shared-sniper/linux/usr/lib")
            libs = [installed/(f"lib{name}.dll.a" if target == "windows" else f"lib{name}.so") for name, _ in ABI]
            for label in ("shared", "static", "partial"):
                binary = out / (target+"-"+label+(".exe" if target == "windows" else ""))
                selected = libs if label == "shared" else libs[:2] if label == "partial" else []
                command = [str(compiler), *flags, str(out/(label+".c")), *map(str, selected), "-o", str(binary)]
                receipt["commands"].append(command)
                with (out/(target+"-"+label+".build.log")).open("w") as log:
                    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60)
                if result.returncode:
                    raise RuntimeError(f"link control failed: {target}-{label}.build.log")
                binaries[(target,label)] = binary
                receipt["inputs"][binary.name] = sha(binary)
        if args.candidate_pe:
            receipt["candidate_pe"] = {"path": str(args.candidate_pe), "sha256": sha(args.candidate_pe)}
        for label, snap in snapshots:
            for mode in ("host", "sniper"):
                cases = [(prefix+target, binaries[(target,"shared")]) for target in ("linux","windows")
                         for prefix in ("producer-","link-","private-","package-paths-","missing-link-","missing-package-")]
                cases += [("binary-"+kind+"-"+target, binaries[(target,kind)]) for target in ("linux","windows") for kind in ("shared","static","partial")]
                cases.append(("preflight-no-output",binaries[("linux","shared")]))
                if args.candidate_pe:
                    cases.append(("binary-candidate-windows",args.candidate_pe))
                for case, binary in cases:
                    folder = out/(label+"-"+mode+"-"+case)
                    folder.mkdir()
                    data = folder/"deps"
                    (data/"llvm-mingw").mkdir(parents=True)
                    (data/"llvm-mingw/bin").symlink_to(deps.DATA/"llvm-mingw/bin", target_is_directory=True)
                    (data/"llvm").mkdir()
                    (data/"llvm/bin").symlink_to(deps.DATA/"llvm/bin", target_is_directory=True)
                    command = [sys.executable,"-B",str(Path(__file__).resolve()),"--worker",str(snap),"--case",case,"--mode",mode,"--data",str(data),"--binary",str(binary)]
                    env = dict(os.environ,BEA_PORT_DATA=str(data),BEA_PORT_SYSROOT=mode)
                    result = subprocess.run(command,env=env,capture_output=True,text=True,timeout=20)
                    (folder/"stdout.txt").write_text(result.stdout)
                    (folder/"stderr.txt").write_text(result.stderr)
                    old_defect = label == "baseline" and (case.startswith("link-") or case.startswith("missing-") or "static" in case or "partial" in case or "candidate" in case or case=="preflight-no-output" or (mode=="sniper" and case in ("producer-windows","package-paths-windows")))
                    expected_code = 1 if old_defect else 0
                    ok = result.returncode == expected_code
                    receipt["cases"].append({"source":label,"mode":mode,"case":case,"expected":expected_code,"returncode":result.returncode,"passed":ok})
                    if not ok:
                        raise RuntimeError(f"{label}/{mode}/{case}: expected {expected_code}, got {result.returncode}")
        receipt["passed"] = True
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    (out/"result.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(f"{'PASS' if receipt['passed'] else 'FAIL'} {len(receipt['cases'])} cases: {out/'result.json'}")
    if "error" in receipt: print(receipt["error"])
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
