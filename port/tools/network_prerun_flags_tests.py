#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the actual pre-run flag digest with Python BLAKE2b on authored data only."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import deps

ROOT=Path(__file__).resolve().parents[2]
source=ROOT/'port/network/prerun_flags.cpp'
driver=ROOT/'port/tools/tests/network_prerun_flags_driver.cpp'
crypto=deps.SRC/'monocypher-4.0.2/src'
pins={'monocypher.h':'f78bb31255cfb7beba66afd2137f5194c8a025cf40488b6cc1e295234d43f374',
      'monocypher.c':'afe2b098c8569577a84488e0b98d276d1fba6506adea68bb9241a52111734c59'}
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
for name,digest in pins.items():
    if not (crypto/name).is_file() or sha(crypto/name)!=digest:raise RuntimeError('pinned Monocypher source missing or changed: '+name)
parent=ROOT/'local-data/port/network-prerun-flags-tests';parent.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));cc=shutil.which('clang');cxx=shutil.which('clang++')
if not cc or not cxx:raise RuntimeError('local clang and clang++ are required; no downloads')
obj=out/'monocypher.o';built=subprocess.run([cc,'-std=c99','-O1','-I',str(crypto),'-c',str(crypto/'monocypher.c'),'-o',str(obj)],capture_output=True,text=True,timeout=60)
(out/'crypto-build.log').write_text(built.stdout+built.stderr)
if built.returncode:raise RuntimeError('Monocypher build failed; see '+str(out/'crypto-build.log'))
vectors={'zeros':bytes(256),'ones':bytes([1]*256),'alternating':bytes(i&1 for i in range(256))}
for i in range(256):
    value=bytearray(256);value[i]=1;vectors['single-'+str(i)]=bytes(value)
expected={name:hashlib.blake2b(data,digest_size=32).hexdigest() for name,data in vectors.items()}
variants=(('actual',None,None,None),
          ('accept-noncanonical','if (flags[i]>1) return 0;','if (false) return 0;','noncanonical'),
          ('accept-wrong-length','count != 256','count < 255 || count > 257','short'),
          ('truncate-input','out, 32, flags, count','out, 32, flags, count-1','digest'))
results=[];original=source.read_text()
for name,old,new,negative in variants:
    folder=out/name;folder.mkdir();text=original
    if old is not None:
        if text.count(old)!=1:raise RuntimeError('mutation is not unique: '+name)
        text=text.replace(old,new)
    src=folder/'prerun_flags.cpp';src.write_text(text);binary=folder/'test'
    proc=subprocess.run([cxx,'-std=c++17','-O1','-I',str(crypto),str(src),str(driver),str(obj),'-o',str(binary)],capture_output=True,text=True,timeout=60)
    (folder/'build.log').write_text(proc.stdout+proc.stderr)
    if proc.returncode:raise RuntimeError(name+' build failed; see '+str(folder/'build.log'))
    trial=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);(folder/'result.log').write_text(trial.stdout+trial.stderr)
    actual={};bad=[]
    for line in trial.stdout.splitlines():
        parts=line.split()
        if parts and parts[0]=='DIGEST' and len(parts)==3:
            if parts[1] in actual:bad.append('duplicate digest')
            actual[parts[1]]=parts[2]
        elif parts and parts[0]=='FAIL':bad.append(' '.join(parts[1:]))
    if trial.returncode:bad.append('process exit')
    if actual!=expected:bad.append('digest')
    expected_bad={'null flags','null output','short','long','noncanonical'}
    passed_names={' '.join(line.split()[1:]) for line in trial.stdout.splitlines() if line.startswith('PASS ')}
    if expected_bad-passed_names:bad.extend(sorted(expected_bad-passed_names))
    ok=not bad if negative is None else negative in bad
    print(('PASS' if ok else 'FAIL'),name,flush=True);results.append({'name':name,'passed':ok,'observed_failures':bad})
after={name:sha(crypto/name) for name in pins}
if after!=pins:raise RuntimeError('Monocypher input changed during the test')
receipt={'scope':'authored canonical flags, actual Monocypher BLAKE2b-256, Python hashlib oracle; not privacy or networking proof',
         'source_sha256':sha(source),'driver_sha256':sha(driver),'monocypher_sha256':pins,'results':results,'vectors':len(expected)}
(out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n');print('evidence:',out)
raise SystemExit(0 if all(x['passed'] for x in results) else 1)
