#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pinned native signaling compile/authored checks; no download or network calls."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
from ice_exchange_tests import MONOCYPHER_SHA256

ROOT=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--toolchain-data",type=Path,required=True)
    p.add_argument("--monocypher-archive",type=Path,required=True)
    p.add_argument("--interface",type=Path,required=True,help="Explicit frozen consumer ice_signaling.h")
    p.add_argument("--juice-include",type=Path,required=True,help="Explicit accepted pinned include directory")
    p.add_argument("--target",choices=("linux32","windows32"),required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--run",action="store_true")
    a=p.parse_args();out=a.output.resolve();data=a.toolchain_data.resolve()
    if out.exists() or not out.is_relative_to((ROOT/"local-data").resolve()):p.error("fresh repo-local output required")
    if a.run and a.target=="windows32":p.error("Windows runtime acceptance is separate")
    if hashlib.sha256(a.monocypher_archive.read_bytes()).hexdigest()!=MONOCYPHER_SHA256:p.error("Monocypher archive mismatch")
    interface=a.interface.read_bytes();juice=(a.juice_include/"juice/juice.h").read_bytes();out.mkdir(parents=True)
    include=out/"include";include.mkdir();(include/"ice_signaling.h").write_bytes(interface)
    (include/"juice").mkdir();(include/"juice/juice.h").write_bytes(juice)
    with tarfile.open(a.monocypher_archive,"r:gz") as archive:archive.extractall(out,filter="data")
    mono=out/"monocypher-4.0.2/src";flags=[];link=[]
    if a.target=="linux32":
        cc=data/"llvm/bin/clang";cxx=data/"llvm/bin/clang++";sysroot=data/"sysroot-sniper"
        flags=["-m32","--sysroot="+str(sysroot)]
        link=["--ld-path="+str(data/"llvm-mingw/bin/ld.lld"),"-L"+str(sysroot/"usr/lib/i386-linux-gnu"),"-lcurl-gnutls","-pthread"]
    else:
        cc=data/"llvm-mingw/bin/i686-w64-mingw32-clang";cxx=data/"llvm-mingw/bin/i686-w64-mingw32-clang++"
        # Match the production game's existing static C++ runtime policy.
        link=["-lwinhttp","-static-libstdc++","-static-libgcc"]
    binary=out/("checks.exe" if a.target=="windows32" else "checks")
    with (out/"build.log").open("w") as log:
        for source,destination in [(mono/"monocypher.c",out/"mono.o"),(ROOT/"port/updater/sha256.c",out/"sha.o")]:
            subprocess.run([str(cc),*flags,"-std=gnu11","-O2","-c",str(source),"-o",str(destination)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=90)
        subprocess.run([str(cxx),*flags,"-std=c++17","-O2","-Wall","-Wextra","-Werror","-DCE_NETWORK_ICE=1","-I"+str(include),"-I"+str(ROOT/"port/network"),"-I"+str(mono),str(ROOT/"port/network/signaling_provider.cpp"),str(ROOT/"port/network/tests/signaling_provider_driver.cpp"),str(out/"mono.o"),str(out/"sha.o"),*link,"-o",str(binary)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    receipt={"target":a.target,"interface_sha256":hashlib.sha256(interface).hexdigest(),"juice_header_sha256":hashlib.sha256(juice).hexdigest(),"binary_sha256":hashlib.sha256(binary.read_bytes()).hexdigest(),"network":False,"provider_calls":False,"real_credentials":False,"run":a.run}
    if a.run:
        loader=data/"sysroot-sniper/lib/ld-linux.so.2"
        libs=":".join(str(data/"sysroot-sniper"/path) for path in ("lib/i386-linux-gnu","usr/lib/i386-linux-gnu"))
        r=subprocess.run([str(loader),"--library-path",libs,str(binary)],capture_output=True,text=True,timeout=30)
        (out/"run.stdout").write_text(r.stdout);(out/"run.stderr").write_text(r.stderr)
        receipt["returncode"]=r.returncode
        if r.returncode or r.stderr or not re.fullmatch(r"NATIVE_SIGNALING_PASS [1-9][0-9]*\n",r.stdout):
            (out/"result.private.json").write_text(json.dumps(receipt,indent=2)+"\n");raise RuntimeError("authored native check failed; bounded diagnostics retained")
        print(r.stdout,end="")
    (out/"result.private.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print("Built",a.target,"native signaling fixture")

if __name__=="__main__":main()
