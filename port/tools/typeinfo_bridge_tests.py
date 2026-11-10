#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pinned i686 RTTI bridge links and exact-type adverse controls, with the unchanged game /QIfist flags.

The old direct comparison must fail the Linux link; the bridge must link both targets. Linux runs only authored
CPU-only RTTI controls; Windows is link-only. Address-only/name-only/always-true bridge mutants must fail.
Optional --game-mirror compiles the actual network adapter against an existing normalized mirror, never edits it.
No full game build, game/VM/GPU, dependency fetch or compiler-flag change occurs.
"""
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

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-mirror", type=Path, help="existing normalized src/ mirror, read only")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    sys.path.insert(0, str(ROOT / "port"))
    import build
    parent = ROOT / "local-data/typeinfo-abi"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="tests-", dir=parent))
    receipt = {"passed": False, "inputs": {}, "commands": [], "cases": [],
               "limits": ["Authored Linux CPU-only RTTI controls; no game, real actor, transport or GPU is exercised.",
                          "Windows compiles/links only; no Windows runtime claim.",
                          "Game flags and /QIfist stages are retained; allocator-symbol renaming is unnecessary for this stack-only fixture.",
                          "Nonmerged/local descriptor controls use the selected libstdc++ protected constructor; they are not portable type_info construction."]}
    try:
        for name in ("port/platform/common/typeinfo_bridge.h", "port/platform/common/typeinfo_bridge.cpp",
                     "port/tools/tests/typeinfo_bridge_driver.cpp", "port/tools/qifist.py", "port/build.py", "port/game/ce_network_world.cpp"):
            dest = out / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT/name, dest)
            receipt["inputs"][name] = sha(dest)
        source = out / "port/tools/tests/typeinfo_bridge_driver.cpp"
        common = out / "port/platform/common"
        adapter = out / "port/game/ce_network_world.cpp"
        text = adapter.read_text()
        if text.count("PortTypeInfoEqual(typeid(") != 16 or text.count("dynamic_cast<") < 2:    # the exact-type guards of the co-op line (tutorial gates, engine ownership, selected player and weapon)
            raise RuntimeError("adapter lost an exact-type or dynamic_cast guard")
        def run(command, label, expected=0):
            receipt["commands"].append([str(value) for value in command])
            result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=90,
                                    env=dict(os.environ, TMPDIR=str(out)))
            (out/(label+".stdout.txt")).write_text(result.stdout)
            (out/(label+".stderr.txt")).write_text(result.stderr)
            ok = result.returncode == expected
            receipt["cases"].append({"case":label,"expected":expected,"returncode":result.returncode,"passed":ok})
            if not ok:
                raise RuntimeError(f"{label}: expected {expected}, got {result.returncode}")
            return result
        def flags(target):
            return [*build.TARGETS[target]["cxx"]]
        def game_object(target, source, label, direct=False, mirror=None):
            obj = out/(label+".o")
            ir = out/(label+".ll")
            inc = ["-I"+str(common), "-I"+str(ROOT/"port/d3dx/include"), "-I"+str(ROOT/"port/include"),
                   "-I"+str(ROOT/"port/game"), *["-I"+str(path) for path in build.TARGETS[target]["includes"]]]
            if mirror:
                inc += ["-I"+str(mirror),"-I"+str(mirror/"MissionScript")]
            first = [*flags(target),"-std=gnu++14",*build.MSCXX,*build.COMMON,*build.X87,*build.NO_MATH_BUILTINS,
                     "-include",str(ROOT/"port/include/port.h"),*inc]
            if direct:
                first.append("-DTEST_DIRECT_RTTI=1")
            run([*first,"-S","-emit-llvm","-Xclang","-disable-llvm-passes","-o",ir,source],label+"-ir")
            run([sys.executable,"-B",out/"port/tools/qifist.py",ir],label+"-qifist")
            run([*flags(target),*build.COMMON,*build.X87,"-c","-o",obj,ir],label+"-object")
            return obj
        layout = ["-std=gnu++17","-DNOMINMAX","-fms-extensions","-fno-autolink","-fno-strict-aliasing","-fwrapv","-mms-bitfields","-fshort-wchar","-O2","-g","-w"]
        callers = {}
        bridges = {}
        for target in ("linux","windows"):
            bridge = out/(target+"-bridge.o")
            run([*flags(target),*layout,"-I"+str(common),"-c",common/"typeinfo_bridge.cpp","-o",bridge],target+"-native-bridge")
            bridges[target] = bridge
            caller = game_object(target, source, target+"-caller")
            callers[target] = caller
            direct = game_object(target, source, target+"-direct",True)
            link_flags = build.deps.sysroot_link_flags() if target=="linux" else ["-static"]
            old_binary = out/(target+"-direct"+(".exe" if target=="windows" else ""))
            old = run([*flags(target),*link_flags,direct,"-o",old_binary],target+"-old-direct-link",1 if target=="linux" else 0)
            if target=="linux" and "std::type_info::operator==" not in old.stderr:
                raise RuntimeError("old Linux link failed for a different reason")
            binary = out/(target+"-bridged"+(".exe" if target=="windows" else ""))
            run([*flags(target),*link_flags,caller,bridge,"-o",binary],target+"-bridge-link")
            if target=="linux":
                run([binary],"linux-rtti-guards")
        for label, body in (("address-only","return &left == &right;"),
                            ("name-only","return __builtin_strcmp(left.name(),right.name()) == 0;"),
                            ("always-true","return true;")):
            mutant = out/(label+".cpp")
            mutant.write_text((common/"typeinfo_bridge.cpp").read_text().replace("return left == right;",body))
            obj = out/(label+".o")
            run([*flags("linux"),*layout,"-I"+str(common),"-c",mutant,"-o",obj],label+"-object")
            binary = out/label
            run([*flags("linux"),*build.deps.sysroot_link_flags(),callers["linux"],obj,"-o",binary],label+"-link")
            run([binary],label+"-negative",1)
        if args.game_mirror:
            for target in ("linux","windows"):
                obj = game_object(target,adapter,target+"-actual-network",mirror=args.game_mirror)
                nm = build.deps.DATA / ("llvm/bin/llvm-nm" if target=="linux" else "llvm-mingw/bin/llvm-nm")
                symbols = run([nm,"-u","-C",obj],target+"-actual-network-undefined")
                if "std::type_info::operator==" in symbols.stdout or "PortTypeInfoEqual" not in symbols.stdout:
                    raise RuntimeError("actual adapter still requests the unsupported equality symbol or lost bridge calls")
        receipt["passed"] = True
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    (out/"result.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(f"{'PASS' if receipt['passed'] else 'FAIL'} {len(receipt['cases'])} cases: {out/'result.json'}")
    if "error" in receipt:
        print(receipt["error"])
    return 0 if receipt["passed"] else 1


if __name__=="__main__":
    raise SystemExit(main())
