#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the bounded ICE exchange fixture; no network or implicit downloads."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT=Path(__file__).resolve().parents[2]
MONOCYPHER_URL="https://monocypher.org/download/monocypher-4.0.2.tar.gz"
MONOCYPHER_SHA256="38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c"
MONOCYPHER_LICENSE="BSD-2-Clause OR CC0-1.0"

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--monocypher-archive",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--target",choices=("native","linux32","windows32"),default="native")
    p.add_argument("--toolchain-data",type=Path)
    p.add_argument("--run",action="store_true")
    a=p.parse_args(); out=a.output.resolve()
    if out.exists() or not out.is_relative_to((ROOT/"local-data").resolve()):p.error("new repo-local output required")
    if a.target!="native" and not a.toolchain_data:p.error("existing pinned toolchain directory required")
    if a.run and a.target=="windows32":p.error("native Windows execution is separate acceptance")
    if hashlib.sha256(a.monocypher_archive.read_bytes()).hexdigest()!=MONOCYPHER_SHA256:p.error("archive identity mismatch")
    out.mkdir(parents=True)
    with tarfile.open(a.monocypher_archive,"r:gz") as stream:stream.extractall(out,filter="data")
    mono=out/"monocypher-4.0.2/src"; flags=[]
    if a.target=="native":cc=shutil.which("cc");cxx=shutil.which("c++")
    elif a.target=="linux32":
        cc=str(a.toolchain_data/"llvm/bin/clang");cxx=str(a.toolchain_data/"llvm/bin/clang++")
        flags=["-m32","--sysroot="+str(a.toolchain_data/"sysroot-sniper")]
    else:
        cc=str(a.toolchain_data/"llvm-mingw/bin/i686-w64-mingw32-clang")
        cxx=str(a.toolchain_data/"llvm-mingw/bin/i686-w64-mingw32-clang++")
    binary=out/("ice_exchange_tests.exe" if a.target=="windows32" else "ice_exchange_tests")
    commands=[
        [cxx,*flags,"-std=c++17","-c",str(ROOT/"port/network/ice_exchange.cpp"),"-o",str(out/"disabled.o")],
        [cc,*flags,"-std=gnu11","-O2","-c",str(mono/"monocypher.c"),"-o",str(out/"monocypher.o")],
        [cxx,*flags,"-std=c++17","-O2","-Wall","-Wextra","-Werror","-DCE_NETWORK_ICE=1","-I"+str(mono),
         str(ROOT/"port/network/ice_exchange.cpp"),str(ROOT/"port/network/tests/ice_exchange_tests.cpp"),
         str(out/"monocypher.o"),"-o",str(binary)]]
    with (out/"build.log").open("w") as log:
        for command in commands:subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    print("Built",a.target,"exchange fixture")
    if a.run:
        fixture=out/"fixture";fixture.mkdir(mode=0o700)
        subprocess.run([str(binary),str(fixture)],check=True,timeout=30)

if __name__=="__main__":main()
