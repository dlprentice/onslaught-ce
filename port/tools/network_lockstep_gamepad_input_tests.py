#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual lockstep action capture and validator with authored mouse/gamepad sequences.

Checks the centered-mouse plus gamepad case, source/ordering fences, unchanged captured input and strict
replica validation. It does not run the game, a real controller, a network transport or native Windows.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from ce_gamepad_tests import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'port/game/ce_network_world.cpp'
HEADER = ROOT / 'port/game/ce_network_world.h'
DRIVER = r'''
#include "ce_network_world.h"
#include <cstdio>
#include <cstring>
#include <limits>
@actions@
@rank@
@allowed@
@valid@
extern "C" @lockvalid@
struct CENetworkWorldAdapter {
    static CE_WorldInput *lock_sample;
    static bool lock_mouse_axis;
    @capture@
};
CE_WorldInput *CENetworkWorldAdapter::lock_sample;
bool CENetworkWorldAdapter::lock_mouse_axis;
static void capture(bool mouse, int button, float value) {
    CENetworkWorldAdapter::lock_mouse_axis=mouse;
    CENetworkWorldAdapter::LockstepCapture(button,value);
}
int main(int argc,char **argv) {
    if (argc!=2) return 3;
    const char *name=argv[1]; CE_WorldInput sample{};
    CENetworkWorldAdapter::lock_sample=&sample;
    if (!std::strcmp(name,"null")) { std::printf("%d 1 0\n",CE_WorldLockstepValidInput(nullptr)); return 0; }
    if (!std::strcmp(name,"empty")) {}
    else if (!std::strcmp(name,"mouse-zero")) capture(true,25,0.f);
    else if (!std::strcmp(name,"binding-only")) capture(false,25,-.75f);
    else if (!std::strcmp(name,"paired") || !std::strcmp(name,"replica-paired")) {capture(true,25,0.f);capture(false,25,-.75f);}
    else if (!std::strcmp(name,"two-pairs")) {capture(true,25,0.f);capture(false,25,-.75f);capture(true,27,0.f);capture(false,27,.5f);}
    else if (!std::strcmp(name,"different-in-order")) {capture(true,25,0.f);capture(false,27,.5f);}
    else if (!std::strcmp(name,"different-out-of-order")) {capture(true,27,0.f);capture(false,25,-.5f);}
    else if (!std::strcmp(name,"nonzero-mouse")) {capture(true,25,-.25f);capture(false,25,-.75f);}
    else if (!std::strcmp(name,"two-bindings")) {capture(false,25,0.f);capture(false,25,-.75f);}
    else if (!std::strcmp(name,"two-mice")) {capture(true,25,0.f);capture(true,25,-.75f);}
    else if (!std::strcmp(name,"reverse")) {capture(false,25,-.75f);capture(true,25,0.f);}
    else if (!std::strcmp(name,"triple")) {capture(true,25,0.f);capture(false,25,-.75f);capture(false,25,-.25f);}
    else if (!std::strcmp(name,"bad-value")) {capture(true,25,0.f);capture(false,25,2.f);}
    else if (!std::strcmp(name,"nan-value")) {capture(true,25,0.f);capture(false,25,std::numeric_limits<float>::quiet_NaN());}
    else if (!std::strcmp(name,"bad-flags")) {capture(true,25,0.f);capture(false,25,-.75f);sample.mouse_ground_aim=1;sample.walker_mouse_any=1;}
    else if (!std::strcmp(name,"bad-source")) {capture(true,25,0.f);capture(false,25,-.75f);sample.actions[1].source=2;}
    else if (!std::strcmp(name,"too-many")) sample.count=CE_WORLD_MAX_ACTIONS+1;
    else return 4;
    CE_WorldInput original=sample;
    bool result;
    if (!std::strcmp(name,"replica-paired")) {
        CE_WorldInput candidate=sample;candidate.epoch=1;candidate.sequence=1;candidate.tick=1;candidate.player=1;
        result=ValidInput(candidate);
    } else result=CE_WorldLockstepValidInput(&sample);
    bool unchanged=std::memcmp(&sample,&original,sizeof sample)==0;
    std::printf("%d %d %u\n",int(result),int(unchanged),sample.count);
    return 0;
}
'''


def main():
    compiler=shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('a C++ compiler is required')
    text=SOURCE.read_text()
    start=text.index('const uint32_t native_actions[]=')
    parts={'actions':text[start:text.index(';',start)+1], 'rank':function(text,'unsigned ActionRank('),
           'allowed':function(text,'bool ActionAllowed('), 'valid':function(text,'bool ValidInput('),
           'lockvalid':function(text,'int CE_WorldLockstepValidInput('), 'capture':function(text,'static void LockstepCapture(')}
    cases={'null':(0,1,0),'empty':(1,1,0),'mouse-zero':(1,1,1),'binding-only':(1,1,1),
           'paired':(1,1,2),'replica-paired':(0,1,2),'two-pairs':(1,1,4),'different-in-order':(1,1,2),
           'different-out-of-order':(0,1,2),'nonzero-mouse':(0,1,2),'two-bindings':(0,1,2),'two-mice':(0,1,2),
           'reverse':(0,1,2),'triple':(0,1,3),'bad-value':(0,1,2),'nan-value':(0,1,2),'bad-flags':(0,1,2),
           'bad-source':(0,1,2),'too-many':(0,1,33)}
    mutants=[('reject-valid-pair','a.value==0.f','false','paired',(0,1,2)),
             ('accept-nonzero-mouse','a.value==0.f','true','nonzero-mouse',(1,1,2)),
             ('accept-two-bindings','a.source==CE_WORLD_ACTION_MOUSE_AXIS','true','two-bindings',(1,1,2)),
             ('accept-two-mice','in->actions[i+1].source==CE_WORLD_ACTION_BINDING','true','two-mice',(1,1,2)),
             ('ignore-action-identity','in->actions[i+1].button==a.button','true','different-out-of-order',(1,1,2))]
    parent=ROOT/'local-data/port/network-lockstep-gamepad-tests';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));results=[]
    variants=[('actual',parts['lockvalid'],None,None)]
    for name,old,new,case,mutant_result in mutants:
        if parts['lockvalid'].count(old)!=1:raise RuntimeError('mutant anchor not unique: '+name)
        variants.append((name,parts['lockvalid'].replace(old,new,1),case,mutant_result))
    for name,validator,selected,mutant_result in variants:
        items=dict(parts,lockvalid=validator);program=DRIVER
        for key,value in items.items():
            token='@'+key+'@'
            if program.count(token)!=1:raise RuntimeError('fixture token mismatch: '+key)
            program=program.replace(token,value)
        cpp=out/(name+'.cpp');cpp.write_text(program);exe=out/name
        built=subprocess.run([compiler,'-std=c++17','-O1','-Wall','-Wextra','-Werror','-I',str(HEADER.parent),str(cpp),'-o',str(exe)],
                             capture_output=True,text=True,timeout=60,env={'PATH':'/usr/bin:/bin','LANG':'C'})
        (out/(name+'-build.log')).write_text(built.stdout+built.stderr)
        if built.returncode:raise RuntimeError('fixture build failed: '+name)
        for case in cases if selected is None else (selected,):
            ran=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=5,env={'PATH':'/usr/bin:/bin','LANG':'C'})
            try:got=tuple(int(x) for x in ran.stdout.strip().split())
            except ValueError:got=()
            correct=ran.returncode==0 and got==cases[case]
            results.append({'variant':name,'case':case,'exit':ran.returncode,
                            'passed':correct if selected is None else ran.returncode==0 and got==mutant_result})
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt={'scope':'actual captured action validator with authored events; no device, game or transport',
             'inputs':{str(p.relative_to(ROOT)):sha(p) for p in (SOURCE,HEADER)},'results':results}
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('%d checks, %d failed; evidence: %s' % (len(results),sum(not x['passed'] for x in results),out))
    return 0 if all(x['passed'] for x in results) else 1

if __name__=='__main__':raise SystemExit(main())
