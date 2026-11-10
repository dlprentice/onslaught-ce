#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual player-gate digest on authored inventories, with mutation controls.

This extracts the production helper and runs it against stubs. It does not run a game, check real asset
compatibility, test a network, or prove full-state equality.
"""
import hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
from network_world_tests import actual_body
ROOT=Path(__file__).resolve().parents[2]
parent=ROOT/'local-data/port/network-lockstep-gate-hash-tests';parent.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
source=ROOT/'port/game/ce_network_world.cpp';driver=ROOT/'port/tools/tests/network_lockstep_gate_hash_driver.cpp'
actual=actual_body(source.read_text(),'static bool LockstepPlayerGates(')
variants=[
 ('actual',None,None,None),
 ('omit-flight','word((uint32_t)e->mFlightModeActive);','/* flight omitted */','old 0/1 flight asymmetry detected'),
 ('ignore-reverse-owner','e->GetPlayer()!=p','false','unowned engine refused'),
 ('ignore-engine-binding','if(e && (e==first_engine || e->GetPlayer()!=p)) return false;','if(false) return false;','aliased player engines refused'),
 ('omit-player-power','word((uint32_t)e->mPoweredUp);','/* power omitted */','player power gate detected'),
 ('omit-player-active','word((uint32_t)e->mActive);','/* active omitted */','player active gate detected'),
 ('omit-battle-engine-mode','word((uint32_t)e->mState);','/* mode omitted */','battle engine mode detected'),
 ('omit-augmentation-selection','word((uint32_t)e->mAugActive);','/* augmentation selection omitted */','augmentation selection detected'),
 ('omit-walker-selection','word((uint32_t)e->mWalkerPart->mCurrentWeapon);','/* walker selection omitted */','walker selection detected'),
 ('omit-jet-selection','word((uint32_t)e->mJetPart->mCurrentWeapon);','/* jet selection omitted */','jet selection detected'),
 ('omit-weapon-active','word((uint32_t)w->mActive);','/* active omitted */','primary weapon active detected'),
 ('omit-primary','!weapon(e->mWalkerPart->mPrimaryWeapon)||','','primary weapon active detected'),
 ('omit-augmentation','!weapon(e->mWalkerPart->mAugWeapon)||','','augmentation active detected'),
 ('omit-walker-list','||!list(e->mWalkerPart->mWeapons)','','walker weapon active detected'),
 ('omit-jet-list','if(!list(e->mJetPart->mWeapons)) return false;','/* jet omitted */','jet weapon active detected'),
 ('omit-unit-list','if(!list(e->mWeapons)) return false;','/* unit omitted */','unit weapon active detected'),
 ('omit-weapon-name','bytes(name,n);','/* name omitted */','weapon names bind gate state'),
 ('allow-oversize','if(size<0||size>64) return false;','if(size<0) return false;','oversize inventory refused'),
 ('allow-truncated','return seen==size;','return true;','incomplete inventory traversal refused'),
]
compiler=shutil.which('clang++') or shutil.which('g++')
if not compiler:raise SystemExit('no local C++ compiler')
results=[]
for name,old,new,expected in variants:
    sub=out/name;sub.mkdir();body=actual
    if old is not None:
        if body.count(old)!=1:raise RuntimeError(name+': mutation is not unique')
        body=body.replace(old,new)
    inc=sub/'actual.inc';inc.write_text(body+'\n');binary=sub/'test'
    build=subprocess.run([compiler,'-std=c++17','-O0','-Wall','-Wextra','-DACTUAL_GATES="'+str(inc)+'"',str(driver),'-o',str(binary)],capture_output=True,text=True,timeout=30,env=dict(os.environ,TMPDIR=str(sub)))
    (sub/'build.log').write_text(build.stdout+build.stderr)
    if build.returncode:raise SystemExit(name+' build failed: '+build.stderr)
    trial=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
    (sub/'result.log').write_text(trial.stdout+trial.stderr)
    ok=trial.returncode==0 if expected is None else trial.returncode!=0 and ('FAIL '+expected) in trial.stdout
    print(('PASS' if ok else 'FAIL'),name)
    results.append({'name':name,'passed':ok,'returncode':trial.returncode,'expected_failure':expected})
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps({'scope':'actual selected-gates helper against authored world stubs',
  'inputs':{str(p.relative_to(ROOT)):sha(p) for p in (source,driver)},'compiler':compiler,'results':results},indent=2)+'\n')
print('evidence:',out)
raise SystemExit(0 if all(row['passed'] for row in results) else 1)
