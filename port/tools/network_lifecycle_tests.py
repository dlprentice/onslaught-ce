#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual compact World codec/queue and target syntax against a frozen selected game-header fixture.

No game, native actor Init, particle/audio equivalence or transport execution is claimed by this gate.
The input fixture must be an existing World test result with its actual typed mirror and compiler command.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fixture', required=True, type=Path)
    args = ap.parse_args()
    fixture=args.fixture.resolve()
    prior=json.loads((fixture/'result.json').read_text())
    parent=ROOT/'local-data/port/network-lifecycle-tests'; parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    shutil.copytree(fixture/'mirror',out/'mirror')
    shutil.copytree(fixture/'production',out/'production')
    shutil.copytree(ROOT/'port/include',out/'production/include')
    shutil.copyfile(ROOT/'port/game/ce_network_session.h',out/'production/game/ce_network_session.h')
    for name in ('ce_network_world.cpp','ce_network_world.h'):
        shutil.copyfile(ROOT/'port/game'/name,out/'production/game'/name)
    for name in ('network_world_driver.cpp','network_world_lifecycle.cpp'):
        shutil.copyfile(ROOT/'port/tools/tests'/name,out/name)
    patch=ROOT/'port/patches/0079-network-compact-lifecycle.patch'
    applied=subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(patch)],cwd=out/'mirror',capture_output=True,text=True,timeout=15)
    (out/'patch.log').write_text(applied.stdout+applied.stderr)
    result={'fixture':str(fixture),'fixture_receipt_sha256':sha(fixture/'result.json'),'patch_sha256':sha(patch),'patch_exit':applied.returncode,
            'policy':'original authoritative source tick/order; native local FX begin once at event Apply, no clock override or host-age equivalence',
            'limits':'authored scalars and actual codecs/queue; no native Init, gameplay, transport, visual/audio or ABI execution'}
    def finish(ok):
        result['passed']=ok
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(out/'result.json',flush=True)
        return 0 if ok else 1
    if applied.returncode: return finish(False)
    from network_world_tests import add_round_sources
    add_round_sources(out/'mirror')
    rounds=subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(ROOT/'port/patches/0081-network-pulse-round-lifecycle.patch')],cwd=out/'mirror',capture_output=True,text=True,timeout=15)
    (out/'rounds-patch.log').write_text(rounds.stdout+rounds.stderr)
    result['round_patch_exit']=rounds.returncode
    result['round_patch_sha256']=sha(ROOT/'port/patches/0081-network-pulse-round-lifecycle.patch')
    if rounds.returncode: return finish(False)
    result['inputs']={str(p.relative_to(out)):sha(p) for folder in ('mirror','production') for p in sorted((out/folder).rglob('*')) if p.is_file()}
    bridge=[x.replace(str(fixture),str(out)) for x in prior['typeinfo_bridge_compile']['command']]
    proc=subprocess.run(bridge,capture_output=True,text=True,timeout=60); (out/'bridge.log').write_text(proc.stdout+proc.stderr)
    result['bridge']={'command':bridge,'exit':proc.returncode}
    if proc.returncode: return finish(False)
    command=[x.replace(str(fixture),str(out)).replace(str(fixture.parents[3]/'port/include'),str(out/'production/include')) for x in prior['compile']]
    command=[str(out/'network_world_lifecycle.cpp') if x==str(out/'network_world_driver.cpp') else x for x in command]
    result['compile']=command
    proc=subprocess.run(command,capture_output=True,text=True,timeout=60); (out/'build.log').write_text(proc.stdout+proc.stderr)
    result['compile_exit']=proc.returncode
    if proc.returncode: return finish(False)
    binary=out/'world-codecs'; proc=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
    result.update(exit=proc.returncode,stdout=proc.stdout,stderr=proc.stderr,binary_sha256=sha(binary))
    print(proc.stdout+proc.stderr,end='',flush=True)
    # Syntax checks use the exact copied World adapter against each target's selected compiler/header ABI.
    import sys
    sys.path.insert(0,str(ROOT/'port'))
    import build
    result['syntax']=[]
    for platform in ('linux','windows'):
        target=build.TARGETS[platform]
        includes=[build.PORT/'d3dx/include',out/'production/include',*target['includes'],out/'mirror',out/'mirror/MissionScript']
        syntax=[*target['cxx'],'-std=gnu++14',*build.MSCXX,*build.COMMON,*build.X87,*build.NO_MATH_BUILTINS,
                '-fsyntax-only','-include',str(out/'production/include/port.h'),*['-I'+str(p) for p in includes],str(out/'production/game/ce_network_world.cpp')]
        check=subprocess.run(syntax,capture_output=True,text=True,timeout=60); (out/(platform+'-syntax.log')).write_text(check.stdout+check.stderr)
        result['syntax'].append({'platform':platform,'command':syntax,'exit':check.returncode})
    return finish(not proc.returncode and all(not x['exit'] for x in result['syntax']))

if __name__=='__main__':
    raise SystemExit(main())
