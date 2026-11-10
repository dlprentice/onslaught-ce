#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and optionally run authored record bridge/regression checks offline."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
from ice_exchange_tests import MONOCYPHER_SHA256

ROOT=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--monocypher-archive",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--target",choices=("native","linux32","windows32"),default="native")
    p.add_argument("--toolchain-data",type=Path)
    p.add_argument("--run",action="store_true")
    a=p.parse_args();out=a.output.resolve()
    if out.exists() or not out.is_relative_to((ROOT/"local-data").resolve()):p.error("fresh repo-local output required")
    if a.target!="native" and not a.toolchain_data:p.error("existing pinned toolchain required")
    if a.run and a.target=="windows32":p.error("Windows runtime acceptance is separate")
    if hashlib.sha256(a.monocypher_archive.read_bytes()).hexdigest()!=MONOCYPHER_SHA256:p.error("archive identity mismatch")
    out.mkdir(parents=True)
    with tarfile.open(a.monocypher_archive,"r:gz") as archive:archive.extractall(out,filter="data")
    mono=out/"monocypher-4.0.2/src";flags=[]
    if a.target=="native":cc=shutil.which("cc");cxx=shutil.which("c++")
    elif a.target=="linux32":
        cc=str(a.toolchain_data/"llvm/bin/clang");cxx=str(a.toolchain_data/"llvm/bin/clang++")
        flags=["-m32","--sysroot="+str(a.toolchain_data/"sysroot-sniper")]
    else:
        cc=str(a.toolchain_data/"llvm-mingw/bin/i686-w64-mingw32-clang")
        cxx=str(a.toolchain_data/"llvm-mingw/bin/i686-w64-mingw32-clang++")
    cases=(("ice_exchange_tests",r"PASS [1-9][0-9]* authored ICE exchange checks\n"),
           ("ice_exchange_bridge_driver",r"ICE_EXCHANGE_BRIDGE_PASS [1-9][0-9]*\n"))
    with (out/"build.log").open("w") as log:
        subprocess.run([cc,*flags,"-std=gnu11","-O2","-c",str(mono/"monocypher.c"),"-o",str(out/"mono.o")],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=90)
        for name,marker in cases:
            binary=out/(name+(".exe" if a.target=="windows32" else ""))
            subprocess.run([cxx,*flags,"-std=c++17","-O2","-Wall","-Wextra","-Werror","-DCE_NETWORK_ICE=1","-I"+str(mono),str(ROOT/"port/network/ice_exchange.cpp"),str(ROOT/"port/network/tests"/(name+".cpp")),str(out/"mono.o"),"-o",str(binary)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=90)
            if a.run:
                fixture=out/(name+"-data");fixture.mkdir(mode=0o700)
                result=subprocess.run([str(binary),str(fixture)],check=True,capture_output=True,text=True,timeout=30)
                if result.stderr or not re.fullmatch(marker,result.stdout):raise RuntimeError("fixture marker mismatch")
                print(result.stdout,end="")
    print("Built",a.target,"record bridge fixtures")

if __name__=="__main__":main()
