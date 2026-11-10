#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual World codec/readiness ordering against a frozen fully patched header fixture.

Current native resources are a counted boundary; no native Apply, actors, clocks, events, game or transport run.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from network_world_tests import actual_body

ROOT=Path(__file__).resolve().parents[2]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--fixture',required=True,type=Path);ap.add_argument('--source',type=Path);args=ap.parse_args()
    fixture=args.fixture.resolve();prior=json.loads((fixture/'result.json').read_text());source=(args.source or ROOT/'port/game/ce_network_world.cpp').resolve()
    parent=ROOT/'local-data/port/network-state-order-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    shutil.copytree(fixture/'mirror',out/'mirror');shutil.copytree(fixture/'production',out/'production');shutil.copyfile(source,out/'production/game/ce_network_world.cpp')
    for name in ('network_world_driver.cpp','network_world_state_order.cpp'):shutil.copyfile(ROOT/'port/tools/tests'/name,out/name)
    text=source.read_text();readiness=actual_body(text,'int LifeReadiness(const CE_WorldFrame &f)');wrapper=actual_body(text,'int CE_WorldFrameReadiness(')
    probe=wrapper.replace('CE_WorldFrameReadiness(','ProbeFrameReadiness(',1).replace('CENetworkWorldAdapter::FrameResources(*frame)','ResourceBoundary(*frame)')
    (out/'readiness-caller.inc').write_text(probe+'\n')
    result={'fixture':str(fixture),'fixture_sha256':sha(fixture/'result.json'),'source':str(source),'source_sha256':sha(source),
            'readiness_body_sha256':hashlib.sha256(readiness.encode()).hexdigest(),'wrapper_body_sha256':hashlib.sha256(wrapper.encode()).hexdigest(),
            'resource_boundary_probe_sha256':sha(out/'readiness-caller.inc'),'limits':__doc__,'passed':False}
    def finish():
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'result.json',flush=True);return 0 if result['passed'] else 1
    result['inputs']={str(p.relative_to(out)):sha(p) for folder in ('mirror','production') for p in sorted((out/folder).rglob('*')) if p.is_file()}
    bridge=[x.replace(str(fixture),str(out)) for x in prior['bridge']['command']];proc=subprocess.run(bridge,capture_output=True,text=True,timeout=30)
    (out/'bridge.log').write_text(proc.stdout+proc.stderr);result['bridge']={'command':bridge,'exit':proc.returncode}
    if proc.returncode:return finish()
    command=[x.replace(str(fixture),str(out)) for x in prior['compile']]
    command=[str(out/'network_world_state_order.cpp') if x==str(out/'network_world_lifecycle.cpp') else x for x in command]
    command.insert(1,'-DREADINESS_CALLER="'+str(out/'readiness-caller.inc')+'"');result['compile']=command
    proc=subprocess.run(command,capture_output=True,text=True,timeout=30);(out/'build.log').write_text(proc.stdout+proc.stderr);result['compile_exit']=proc.returncode
    if proc.returncode:return finish()
    binary=out/'world-codecs';proc=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
    result.update(exit=proc.returncode,stdout=proc.stdout,stderr=proc.stderr,binary_sha256=sha(binary));print(proc.stdout+proc.stderr,end='',flush=True)
    result['passed']=not proc.returncode;return finish()
if __name__=='__main__':raise SystemExit(main())
