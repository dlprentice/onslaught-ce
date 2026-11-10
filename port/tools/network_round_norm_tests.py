#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual RoundEntryValues and FVector magnitude controls; no native actors/factories/game or transport.

Host libm and pinned Linux i686/x87 PC24 are separate executions. Windows is compiled/linked, not executed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from network_world_tests import actual_body
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'port'))
import build

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path);args=ap.parse_args()
    source=(args.source or ROOT/'port/game/ce_network_world.cpp').resolve();world=source.read_text();math=(ROOT/'src/maths.h').read_text()
    parent=ROOT/'local-data/port/network-round-norm';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));pins={}
    def take(text,sig):
        body=actual_body(text,sig);pins[sig]=hashlib.sha256(body.encode()).hexdigest();return body
    replacements={'/* VECTOR_SET */':take(math,'void\tSet(float x, float y, float z)'),
                  '/* VECTOR_MAGNITUDE */':take(math,'float\tMagnitude() const'),
                  '/* TYPES */':'\n'.join(take(world,'struct '+name)+';' for name in ('Vec','Mat','RoundBirth')),
                  '/* HELPERS */':'\n'.join(take(world,sig) for sig in ('void PutVec(FVector &out, const Vec &v)','bool Range(float value,float low,float high)','bool VecBound(const Vec &v,float limit=1048576.f)')),
                  '/* ACTUAL_GUARD */':take(world,'    static bool RoundEntryValues(')}
    text=(ROOT/'port/tools/tests/network_round_norm.cpp.in').read_text()
    for key,value in replacements.items():assert text.count(key)==1;text=text.replace(key,value)
    cpp=out/'guard.cpp';cpp.write_text(text);(out/'source-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
    result={'source':str(source),'source_sha256':sha(source),'source_pins':pins,'cpp_sha256':sha(cpp),'math_source_sha256':sha(ROOT/'src/maths.h'),
            'x87_source_sha256':sha(ROOT/'port/platform/common/x87math.cpp'),
            'compiler_sha256':{name:sha(Path(target['cxx'][0])) for name,target in build.TARGETS.items()},'limits':__doc__,'passed':False,'stages':[]}
    def run(name,cmd,execute=False):
        proc=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/(name+'.stdout')).write_text(proc.stdout);(out/(name+'.stderr')).write_text(proc.stderr)
        row={'name':name,'command':cmd,'exit':proc.returncode};result['stages'].append(row)
        if execute:row['observations']=[json.loads(line) for line in proc.stdout.splitlines() if line.strip()];print(proc.stdout,end='')
        return proc.returncode==0
    common=['-std=gnu++14','-O2',*build.NO_MATH_BUILTINS]
    ok=run('host-build',['clang++',*common,str(cpp),'-lm','-o',str(out/'host')])
    if ok:ok=run('host',[str(out/'host')],True)
    # Preserve both target check results even if a selected old guard fails its expected numeric policy.
    for platform in ('linux','windows'):
        target=build.TARGETS[platform];obj=out/(platform+'.o');x87=out/(platform+'-x87.o');binary=out/('linux' if platform=='linux' else 'windows.exe')
        prefix='' if platform=='linux' else '_';flags=[*target['cxx'],*common,*build.X87]
        built=run(platform+'-build',[*flags,'-DUNIT_TARGET_X87','-c',str(cpp),'-o',str(obj)])
        if built:built=run(platform+'-rename',[build.objcopy(platform),'--redefine-sym',prefix+'sqrtf='+prefix+'bea_x87_sqrtf',str(obj)])
        if built:
            nm=str(Path(build.objcopy(platform)).with_name('llvm-nm'))
            built=run(platform+'-symbols',[nm,'--undefined-only',str(obj)])
            if built:built=(prefix+'bea_x87_sqrtf') in (out/(platform+'-symbols.stdout')).read_text()
        if built:built=run(platform+'-x87',[*flags,'-c',str(ROOT/'port/platform/common/x87math.cpp'),'-o',str(x87)])
        if built:built=run(platform+'-link',[*target['cxx'],*build.X87,str(obj),str(x87),*(['-no-pie'] if platform=='linux' else []),*(build.deps.sysroot_link_flags() if platform=='linux' else []),'-lm','-o',str(binary)])
        if built and platform=='linux':built=run('linux-execution',[str(binary)],True)
        ok=built and ok
    result['passed']=ok;(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'result.json',flush=True);return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
