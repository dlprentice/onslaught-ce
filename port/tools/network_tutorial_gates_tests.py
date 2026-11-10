#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test the prototype's tutorial player-gate selector and patched script calls with authored stubs.

Exercises the actual selector, lockstep stage/level predicate and six script method bodies. The game,
mission VM, real controllers, saves, sockets and retail content are not run by this test.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from network_world_tests import actual_body

ROOT=Path(__file__).resolve().parents[2]
parent=ROOT/'local-data/port/network-tutorial-gates-tests'
parent.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
mirror=out/'mirror/MissionScript'
mirror.mkdir(parents=True)
script_path=mirror/'IScript.cpp'
script_path.write_text((ROOT/'src/MissionScript/IScript.cpp').read_text(encoding='latin-1'),encoding='latin-1')
patch=ROOT/'port/patches/0143-lockstep-tutorial-gates.patch'
p=subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(patch)],cwd=out/'mirror',capture_output=True,text=True,timeout=15)
(out/'patch.log').write_text(p.stdout+p.stderr)
if p.returncode or 'fuzz' in p.stdout.lower():raise SystemExit('script patch did not apply without fuzz: '+p.stdout+p.stderr)
script=script_path.read_text(encoding='latin-1')
world=(ROOT/'port/game/ce_network_world.cpp').read_text()
lockstep=(ROOT/'port/game/ce_network_lockstep.cpp').read_text()
helper=actual_body(world,'void *CE_LockstepTutorialPartner(')
stage='extern "C" '+actual_body(lockstep,'int CE_LockstepSharing(void)')+'\nextern "C" '+actual_body(lockstep,'int CE_LockstepCoopPlayers(void)')+'\n'
names=('EnableWeapon','DisableWeapon','EnableFlightMode','DisableFlightMode','Activate','Deactivate')
methods='\n'.join(actual_body(script,'void\tIScript::'+name+'(') for name in names)+'\n'
driver=ROOT/'port/tools/tests/network_tutorial_gates_driver.cpp'
compiler=shutil.which('clang++') or shutil.which('g++')
if not compiler:raise SystemExit('no local C++ compiler')
variants=[
 ('actual',None,None,None),
 ('no-sharing-guard','!CE_LockstepCoopPlayers() || ','','offline or finished keeps partner unchanged'),
 ('no-object-identity','static_cast<void *>(static_cast<CThing *>(host)) != thing','false','unrelated thing does not affect players'),
 ('allow-alias','host == guest || ','','aliased engines refused'),
 ('allow-single-player',' || GAME.GetNPlayers() != 2','','single player refused'),
]
for name,call,reason in (
 ('omit-disable-flight','if (partner) ((CBattleEngine *)partner)->DisableFlightMode();','tutorial disables flight on both'),
 ('omit-enable-flight','if (partner) ((CBattleEngine *)partner)->EnableFlightMode();','tutorial enables flight on both'),
 ('omit-disable-weapon','if (partner) ((CUnit *)partner)->DisableWeapon(args[0]->GetString());','tutorial disables named weapons on both'),
 ('omit-enable-weapon','if (partner) ((CUnit *)partner)->EnableWeapon(args[0]->GetString());','tutorial enables named weapons on both'),
 ('omit-deactivate','if (partner) partner->Deactivate();','tutorial deactivates both'),
 ('omit-activate','if (partner) partner->Activate();','tutorial activates both'),
): variants.append((name,call,'/* deliberately omitted */',reason))
for name,call,reason in (
 ('double-disable-flight','if (partner) ((CBattleEngine *)partner)->DisableFlightMode();','flight disable once and in order'),
 ('double-enable-flight','if (partner) ((CBattleEngine *)partner)->EnableFlightMode();','flight enable once and in order'),
 ('double-disable-weapon','if (partner) ((CUnit *)partner)->DisableWeapon(args[0]->GetString());','weapon disable once and in order'),
 ('double-enable-weapon','if (partner) ((CUnit *)partner)->EnableWeapon(args[0]->GetString());','weapon enable once and in order'),
 ('double-deactivate','if (partner) partner->Deactivate();','deactivate once and in order'),
 ('double-activate','if (partner) partner->Activate();','activate once and in order'),
): variants.append((name,call,call+' '+call,reason))
results=[]
for name,old,new,expected in variants:
    sub=out/name;sub.mkdir()
    h=helper;s=methods
    if old is not None:
        if h.count(old)+s.count(old)!=1:raise RuntimeError(f'{name}: mutation not unique')
        h=h.replace(old,new);s=s.replace(old,new)
    snippets={'lockstep':stage,'partner':'extern "C" '+h+'\n','script':s}
    defines=[]
    for key,body in snippets.items():
        path=sub/(key+'.inc');path.write_text(body)
        defines.append('-DACTUAL_'+key.upper()+'="'+str(path)+'"')
    binary=sub/'test'
    cmd=[compiler,'-std=c++17','-O0','-Wall','-Wextra',*defines,str(driver),'-o',str(binary)]
    build=subprocess.run(cmd,capture_output=True,text=True,timeout=30,env=dict(os.environ,TMPDIR=str(sub)))
    (sub/'build.log').write_text(build.stdout+build.stderr)
    if build.returncode:
        print(name,'build failed:',build.stderr);raise SystemExit(1)
    trial=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
    (sub/'result.log').write_text(trial.stdout+trial.stderr)
    ok=(trial.returncode==0) if expected is None else (trial.returncode!=0 and ('FAIL '+expected) in trial.stdout)
    print(('PASS' if ok else 'FAIL'),name)
    results.append({'name':name,'passed':ok,'returncode':trial.returncode,'expected_failure':expected})
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
receipt={'scope':'actual selector, predicate and patched script methods; authored world stubs only',
         'inputs':{str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'src/MissionScript/IScript.cpp',patch,ROOT/'port/game/ce_network_world.cpp',ROOT/'port/game/ce_network_lockstep.cpp',driver)},
         'results':results,'compiler':compiler}
(out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('evidence:',out)
raise SystemExit(0 if all(row['passed'] for row in results) else 1)
